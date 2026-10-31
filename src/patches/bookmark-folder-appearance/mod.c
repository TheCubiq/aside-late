// Colors bookmark folders and changes their icons. Aside's classes and methods are bound by name in resolve.c, and
// the shape of its objects is learned here from the first row that paints.

#include "mod/mod.h"
#include "resolve.h"

#include <math.h>
#include <stdio.h>
#include "menu.h"

#define MAX_FOLDERS 1024
#define MAX_CONTAINERS 32
#define GUID_SIZE 36
#define INDENT 14
#define CORNER_RADIUS 8.0f
#define TICK_MS 16
#define SCAN_LIMIT 0x700
#define ICON_SIZE 16
#define NEUTRAL_ICON_COLOR 0xB0B3B8
#define COLOR_COUNT 9
#define BUTTON_MASK 0xf800
#define RIGHT_BUTTON 0x2000

typedef struct {
    char guid[GUID_SIZE];
    int color, icon;  // -1 when unset.
} Folder;

typedef struct {
    BYTE *view;
    void *vtable;
    unsigned lastBounds;
} Container;

typedef void (*CreateRounded)(void **result, ULONGLONG color, float radius, int inset);
typedef void (*SchedulePaint)(void *view);
typedef void (*Paint)(void *self, void *canvas, void *view);
typedef void (*PaintBackground)(void *view, void *canvas);
typedef void (*CreateImage)(void *result, const void *icon, ULONGLONG color, int size, void *badge);
typedef void (*SetImage)(void *view, const void *image);
typedef void (*DestroyImage)(void *image);
typedef void (*UpdateIcons)(void *row);
typedef void (*SetTitleColor)(void *label, ULONGLONG color);
typedef BOOL (*MousePressed)(void *row, const void *event);
typedef void (*ConvertToScreen)(const void *view, POINT *point);

static Bindings Native;
static BYTE *Base;
static void *RowVtable;
static PaintBackground OriginalRowPaint, OriginalContainerPaint;
static MousePressed OriginalMousePressed;

// Learned from live objects.
static DWORD ParentOffset, ChildrenOffset, NodeOffset, GuidOffset, NodeParentOffset;
static DWORD ImageIconOffset, ImageColorOffset;
static BOOL Learned;
static int LearnAttempts;

static Folder Folders[MAX_FOLDERS];
static int FolderCount;
static char StorePath[MAX_PATH];

static Container Containers[MAX_CONTAINERS];
static int ContainerCount;
static void *Rounded;

static const IconEntry *Icons;
// Indexed by Aside's own color IDs, which is what's stored and what the menu's buttons use.
static const DWORD FolderColors[COLOR_COUNT] = {
    0xDADCE0, 0x8AB4F8, 0xF28B82, 0xFDD663, 0x81C995, 0xFF8BCB, 0xC58AF9, 0x78D9EC, 0xFCAD70,
};

static int FindIcon(const char *key) {
    size_t size = strcspn(key, "\t\r\n");
    for (int i = 0; i < Native.iconCount; i++)
        if (Icons[i].keySize == size && !memcmp(Icons[i].key, key, size)) return i;
    return -1;
}

static void LoadFolders(void) {
    if (!ExpandEnvironmentStringsA("%LOCALAPPDATA%\\AsideLate\\folders.tsv", StorePath, MAX_PATH)) return;
    FILE *file = fopen(StorePath, "r");
    if (!file) return;
    char line[128];
    while (FolderCount < MAX_FOLDERS && fgets(line, sizeof line, file)) {
        int color;
        if (strlen(line) < GUID_SIZE + 2 || line[GUID_SIZE] != '\t' || sscanf(line + GUID_SIZE + 1, "%d", &color) != 1) continue;
        if (color < -1 || color >= COLOR_COUNT) continue;
        const char *key = strchr(line + GUID_SIZE + 1, '\t');
        memcpy(Folders[FolderCount].guid, line, GUID_SIZE);
        Folders[FolderCount].color = color;
        Folders[FolderCount++].icon = key ? FindIcon(key + 1) : -1;
    }
    fclose(file);
}

static void SaveFolders(void) {
    FILE *file = fopen(StorePath, "w");
    if (!file) return;
    for (int i = 0; i < FolderCount; i++) {
        const Folder *folder = &Folders[i];
        fprintf(file, "%.36s\t%d\t", folder->guid, folder->color);
        if (folder->icon >= 0) fwrite(Icons[folder->icon].key, 1, Icons[folder->icon].keySize, file);
        fputc('\n', file);
    }
    fclose(file);
}

static const Folder *FindFolder(const char *guid) {
    for (int i = 0; i < FolderCount; i++)
        if (!memcmp(Folders[i].guid, guid, GUID_SIZE)) return &Folders[i];
    return NULL;
}

static int FindColor(const char *guid) {
    const Folder *folder = FindFolder(guid);
    return folder ? folder->color : -1;
}

static void SetFolder(const char *guid, int color, int icon) {
    Folder *folder = (Folder *)FindFolder(guid);
    if (color < 0 && icon < 0) {
        if (folder) *folder = Folders[--FolderCount];
    } else {
        if (!folder) {
            if (FolderCount == MAX_FOLDERS) return;
            folder = &Folders[FolderCount++];
            memcpy(folder->guid, guid, GUID_SIZE);
        }
        folder->color = color;
        folder->icon = icon;
    }
    SaveFolders();
}

static BOOL IsGuid(const char *text) {
    for (int i = 0; i < GUID_SIZE; i++) {
        char c = text[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return FALSE;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return FALSE;
        }
    }
    return TRUE;
}

// A heap std::string holding a GUID: pointer, size 36, capacity with the long-string bit.
static BOOL ReadGuidAt(const BYTE *object, DWORD offset, char *guid) {
    struct {
        const char *data;
        size_t size;
        size_t capacity;
    } string;
    if (!SafeRead(object + offset, &string, sizeof string)) return FALSE;
    if (string.size != GUID_SIZE || !(string.capacity >> 63)) return FALSE;
    return SafeRead(string.data, guid, GUID_SIZE) && IsGuid(guid);
}

// Aside's lists are stored as pointer, count, capacity.
static BOOL ListContains(const BYTE *object, DWORD offset, const void *item) {
    struct {
        void **data;
        size_t count;
        size_t capacity;
    } list;
    if (!SafeRead(object + offset, &list, sizeof list)) return FALSE;
    if (!list.data || !list.count || list.count > list.capacity || list.capacity > 100000) return FALSE;
    for (size_t i = 0; i < list.count; i++) {
        void *entry;
        if (!SafeRead(list.data + i, &entry, sizeof entry)) return FALSE;
        if (entry == item) return TRUE;
    }
    return FALSE;
}

// View::parent_ points at something whose children_ contains this row.
static BOOL LearnParent(const BYTE *row) {
    for (DWORD parent = 0x100; parent < 0x300; parent += 8) {
        BYTE *candidate;
        if (!SafeRead(row + parent, &candidate, sizeof candidate) || !candidate) continue;
        for (DWORD children = 0x100; children < 0x300; children += 8) {
            if (!ListContains(candidate, children, row)) continue;
            ParentOffset = parent;
            ChildrenOffset = children;
            return TRUE;
        }
    }
    return FALSE;
}

// The row points at its BookmarkNode: the object with a GUID whose parent's children include it.
static BOOL LearnNode(const BYTE *row) {
    char guid[GUID_SIZE];
    for (DWORD node = Native.boundsOffset; node < SCAN_LIMIT; node += 8) {
        BYTE *candidate;
        if (!SafeRead(row + node, &candidate, sizeof candidate) || !candidate) continue;
        for (DWORD uuid = 8; uuid < 0x200; uuid += 8) {
            if (!ReadGuidAt(candidate, uuid, guid)) continue;
            for (DWORD parent = 8; parent < uuid; parent += 8) {
                BYTE *up;
                if (!SafeRead(candidate + parent, &up, sizeof up) || !up) continue;
                if (!ListContains(up, parent + 8, candidate)) continue;
                NodeOffset = node;
                GuidOffset = uuid;
                NodeParentOffset = parent;
                return TRUE;
            }
        }
    }
    return FALSE;
}

static BYTE *NodeOf(const BYTE *row) {
    return *(BYTE *const *)(row + NodeOffset);
}

static BYTE *NodeParent(const BYTE *node) {
    return *(BYTE *const *)(node + NodeParentOffset);
}

static const char *GuidOf(const BYTE *node) {
    return *(const char *const *)(node + GuidOffset);
}

static int DepthOf(const BYTE *node) {
    int depth = 0;
    while ((node = NodeParent(node)) && depth < 64) depth++;
    return depth;
}

static BOOL IsInside(const BYTE *node, const BYTE *ancestor) {
    for (int depth = 0; node && depth < 64; depth++) {
        node = NodeParent(node);
        if (node == ancestor) return TRUE;
    }
    return FALSE;
}

static const RECT *BoundsOf(const BYTE *view, RECT *out) {
    const int *bounds = (const int *)(view + Native.boundsOffset);
    out->left = bounds[0];
    out->top = bounds[1];
    out->right = bounds[0] + bounds[2];
    out->bottom = bounds[1] + bounds[3];
    return out;
}

// Follows bounds_ in View, and shows a row as present.
static BOOL IsVisible(const BYTE *view) {
    return view[Native.boundsOffset + 16] != 0;
}

static void **ChildrenOf(const BYTE *view, size_t *count) {
    *count = *(const size_t *)(view + ChildrenOffset + 8);
    return *(void **const *)(view + ChildrenOffset);
}

static BOOL IsRow(const BYTE *view) {
    return view && *(void *const *)view == RowVtable;
}

static void Repaint(void *view) {
    ((SchedulePaint)(Base + Native.schedulePaint))(view);
}

static void SetImageView(void *view, const void *icon, ULONGLONG color) {
    BYTE image[64] = {0};
    ((CreateImage)(Base + Native.createImage))(image, icon, color, ICON_SIZE, NULL);
    ((SetImage)(Base + Native.setImage))(view, image);
    ((DestroyImage)(Base + Native.destroyImage))(image);
}

// An image view keeps the icon and color it was given. Finding those values straight after
// setting them shows where they live, so later paints can tell if anything changed them.
static void LearnImageShape(const BYTE *view, const void *icon, ULONGLONG color) {
    for (DWORD offset = 0x100; offset < 0x800; offset += 8) {
        const void *value;
        if (!SafeRead(view + offset, &value, sizeof value) || value != icon) continue;
        for (DWORD other = offset + 4; other < offset + 0x40; other += 4) {
            ULONGLONG candidate;
            if (!SafeRead(view + other, &candidate, sizeof candidate) || candidate != color) continue;
            ImageIconOffset = offset;
            ImageColorOffset = other;
            ModLog("learned: image icon +%#lx, color +%#lx", offset, other);
            return;
        }
    }
    ModLog("couldn't learn the image view's shape");
}

static BOOL ImageIsCurrent(const BYTE *view, const void *icon, ULONGLONG color) {
    return *(const void *const *)(view + ImageIconOffset) == icon && *(const ULONGLONG *)(view + ImageColorOffset) == color;
}

static void SetRowTitleColor(const BYTE *row, ULONGLONG color) {
    void *title = *(void *const *)(row + Native.titleOffset);
    if (!title) return;
    void **vtable = *(void ***)title;
    ((SetTitleColor)vtable[Native.setTitleColorOffset / 8])(title, color);
}

// Skia colors are tagged in the top half of a ColorVariant. Native color IDs aren't.
static ULONGLONG SkiaColor(DWORD rgb) {
    return (1ull << 32) | 0xFF000000u | rgb;
}

// Runs on every paint, so it only touches what's out of date. Setting an image repaints.
static void StyleRow(BYTE *row) {
    BYTE *node = NodeOf(row);
    const Folder *folder = node ? FindFolder(GuidOf(node)) : NULL;
    if (!folder) return;
    ULONGLONG color = SkiaColor(folder->color >= 0 ? FolderColors[folder->color] : NEUTRAL_ICON_COLOR);
    const void *glyph = folder->icon >= 0 ? Icons[folder->icon].icon : Base + Native.folderIcon;
    const void *caretGlyph = Base + Native.caretIcon;
    BYTE *leading = *(BYTE **)(row + Native.leadingOffset), *caret = *(BYTE **)(row + Native.caretOffset);
    if (!leading) return;
    if (ImageIconOffset && ImageIsCurrent(leading, glyph, color) && (!caret || ImageIsCurrent(caret, caretGlyph, color))) return;
    SetImageView(leading, glyph, color);
    if (!ImageIconOffset) LearnImageShape(leading, glyph, color);
    if (caret) SetImageView(caret, caretGlyph, color);
    if (folder->color >= 0) SetRowTitleColor(row, color);
}

// Back to exactly what Aside would draw, using its own function and its own color.
static void RestoreRow(BYTE *row) {
    ((UpdateIcons)(Base + Native.updateIcons))(row);
    SetRowTitleColor(row, Native.titleColorId + (row[Native.selectedOffset] ? 3 : 0));
}

static void RefreshRows(const char *guid) {
    for (int i = 0; i < ContainerCount; i++) {
        size_t count;
        void **children = ChildrenOf(Containers[i].view, &count);
        for (size_t j = 0; j < count; j++) {
            BYTE *row = children[j];
            if (!IsRow(row) || !NodeOf(row) || memcmp(GuidOf(NodeOf(row)), guid, GUID_SIZE)) continue;
            RestoreRow(row);
            StyleRow(row);
        }
        Repaint(Containers[i].view);
    }
}

static void PaintBlock(BYTE *container, void *canvas, const RECT *rect, int inset, int color, BOOL expanded) {
    RECT area;
    BoundsOf(container, &area);
    int *insets = (int *)((BYTE *)Rounded + 0x20);
    insets[0] = rect->top;
    insets[1] = rect->left + inset;
    insets[2] = (area.bottom - area.top) - rect->bottom;
    insets[3] = (area.right - area.left) - rect->right + (inset ? 6 : 0);
    ULONGLONG argb = (expanded ? 0x33000000u : 0x1a000000u) | FolderColors[color];
    *(ULONGLONG *)((BYTE *)Rounded + 8) = (1ull << 32) | argb;
    void **vtable = *(void ***)Rounded;
    ((Paint)vtable[Native.backgroundPaintOffset / 8])(Rounded, canvas, container);
}

static void PaintFolders(BYTE *container, void *canvas) {
    if (!Rounded || !FolderCount) return;
    size_t count;
    void **children = ChildrenOf(container, &count);
    int baseDepth = 1 << 30;
    for (size_t i = 0; i < count; i++) {
        BYTE *row = children[i];
        if (!IsRow(row) || !NodeOf(row)) continue;
        baseDepth = min(baseDepth, DepthOf(NodeOf(row)));
    }
    for (size_t i = 0; i < count; i++) {
        BYTE *row = children[i];
        if (!IsRow(row) || !IsVisible(row) || !NodeOf(row)) continue;
        BYTE *node = NodeOf(row);
        int color = FindColor(GuidOf(node));
        if (color < 0) continue;
        RECT block;
        BoundsOf(row, &block);
        if (block.bottom <= block.top) continue;
        BOOL expanded = FALSE;
        for (size_t j = i + 1; j < count; j++) {
            BYTE *child = children[j];
            if (!IsRow(child) || !NodeOf(child) || !IsInside(NodeOf(child), node)) break;
            RECT bounds;
            BoundsOf(child, &bounds);
            if (!IsVisible(child) || bounds.bottom <= bounds.top) continue;
            block.bottom = max(block.bottom, bounds.bottom);
            expanded = TRUE;
        }
        PaintBlock(container, canvas, &block, (DepthOf(node) - baseDepth) * INDENT, color, expanded);
    }
}

static void ContainerPaint(void *view, void *canvas) {
    OriginalContainerPaint(view, canvas);
    PaintFolders(view, canvas);
}

static void AddContainer(BYTE *view) {
    for (int i = 0; i < ContainerCount; i++)
        if (Containers[i].view == view) return;
    if (ContainerCount == MAX_CONTAINERS) return;
    void **vtable = *(void ***)view;
    if (!SwapVtableEntry(vtable, Native.paintBackgroundSlot, ContainerPaint, (void **)&OriginalContainerPaint)) {
        ModLog("couldn't hook the rows container");
        return;
    }
    Containers[ContainerCount++] = (Container){.view = view, .vtable = vtable};
    ModLog("rows container %p", view);
    Repaint(view);
}

// Rows move without their container repainting, and the blocks have to follow them.
// Containers aren't told they're gone, but a freed one no longer has this vtable.
static void CALLBACK Tick(HWND window, UINT message, UINT_PTR timer, DWORD time) {
    (void)window, (void)message, (void)timer, (void)time;
    MenuTick();
    if (!FolderCount) return;
    for (int i = 0; i < ContainerCount; i++) {
        Container *container = &Containers[i];
        void *vtable;
        if (!SafeRead(container->view, &vtable, sizeof vtable) || vtable != container->vtable) {
            *container = Containers[--ContainerCount];
            i--;
            continue;
        }
        size_t count;
        void **children = ChildrenOf(container->view, &count);
        unsigned hash = 17;
        for (size_t j = 0; j < count; j++) {
            if (!IsRow(children[j])) continue;
            const unsigned *bounds = (const unsigned *)((BYTE *)children[j] + Native.boundsOffset);
            hash = hash * 31 + bounds[1];
            hash = hash * 31 + bounds[3];
            hash = hash * 31 + IsVisible(children[j]);
        }
        if (hash == container->lastBounds) continue;
        container->lastBounds = hash;
        Repaint(container->view);
    }
}

static char MenuGuid[GUID_SIZE];

static void PickColor(int color) {
    const Folder *folder = FindFolder(MenuGuid);
    SetFolder(MenuGuid, color, folder ? folder->icon : -1);
    RefreshRows(MenuGuid);
}

static void PickIcon(const char *key, size_t size) {
    for (int i = 0; i < Native.iconCount; i++) {
        if (Icons[i].keySize != size || memcmp(Icons[i].key, key, size)) continue;
        const Folder *folder = FindFolder(MenuGuid);
        SetFolder(MenuGuid, folder ? folder->color : -1, i);
        RefreshRows(MenuGuid);
        return;
    }
}

static void ResetFolder(void) {
    SetFolder(MenuGuid, -1, -1);
    RefreshRows(MenuGuid);
}

// A right click runs a callback the owner bound with itself as the receiver.
static BYTE *OwnerOf(const BYTE *row) {
    const BYTE *state = *(const BYTE *const *)(row + Native.rightClickOffset);
    if (!state || *(void *const *)(state + 0x20) != Base + Native.rightClick) return NULL;
    return *(BYTE *const *)(state + 0x30);
}

static BOOL RowMousePressed(void *view, const void *event) {
    BYTE *row = view;
    const BYTE *press = event;
    BYTE *node = Learned ? NodeOf(row) : NULL;
    BYTE *owner = node ? OwnerOf(row) : NULL;
    if ((*(const DWORD *)(press + Native.eventFlagsOffset) & BUTTON_MASK) != RIGHT_BUTTON || !owner ||
        !*(const int *)(node + Native.nodeTypeOffset) || node[Native.nodeManagedOffset])
        return OriginalMousePressed(view, event);
    const float *location = (const float *)(press + Native.eventLocationOffset);
    POINT screen = {lroundf(location[0]), lroundf(location[1])};
    ((ConvertToScreen)(Base + Native.convertToScreen))(row, &screen);
    memcpy(MenuGuid, GuidOf(node), GUID_SIZE);
    const Folder *folder = FindFolder(MenuGuid);
    MenuChoices choices = {
        .color = folder ? folder->color : -1,
        .icon = folder && folder->icon >= 0 ? &Icons[folder->icon] : NULL,
        .pickColor = PickColor,
        .pickIcon = PickIcon,
        .reset = ResetFolder,
    };
    MenuOpen(row, owner, node, screen, &choices);
    return TRUE;
}

static void StartOnUiThread(void) {
    ((CreateRounded)(Base + Native.roundedBackground))(&Rounded, 1ull << 32, CORNER_RADIUS, 0);
    if (!Rounded) ModLog("couldn't create the block background");
    SetTimer(NULL, 0, TICK_MS, Tick);
}

static void RowPaint(void *view, void *canvas) {
    BYTE *row = view;
    if (Learned) StyleRow(row);
    OriginalRowPaint(view, canvas);
    if (!Learned) {
        if (LearnAttempts >= 64) return;
        LearnAttempts++;
        BOOL parent = LearnParent(row), node = parent && LearnNode(row);
        if (!parent || !node) {
            if (LearnAttempts == 1 || LearnAttempts == 64) ModLog("couldn't learn the row's shape yet: parent %d, node %d", parent, node);
            return;
        }
        ModLog("learned: parent +%#lx, children +%#lx, node +%#lx, guid +%#lx, node parent +%#lx",
               ParentOffset, ChildrenOffset, NodeOffset, GuidOffset, NodeParentOffset);
        Learned = TRUE;
        MenuInit(&Native, Base, ChildrenOffset + 8);
        StartOnUiThread();
    }
    BYTE *parent = *(BYTE **)(row + ParentOffset);
    if (!parent) return;
    AddContainer(parent);
}

static void Start(const Lookup *lookup, BYTE *base) {
    const char *missing = ResolveBindings(lookup, &Native);
    if (missing) {
        ModLog("bookmark-folder-appearance refused: didn't find %s", missing);
        return;
    }
    Base = base;
    RowVtable = base + Native.rowVtable;
    Icons = (const IconEntry *)(base + Native.iconTable);
    LoadFolders();
    if (!SwapVtableEntry(RowVtable, Native.paintBackgroundSlot, RowPaint, (void **)&OriginalRowPaint)) {
        ModLog("couldn't hook the row");
        return;
    }
    if (!SwapVtableEntry(RowVtable, Native.mousePressedSlot, RowMousePressed, (void **)&OriginalMousePressed)) ModLog("couldn't hook the row's clicks");
    ModLog("bookmark-folder-appearance ready: row vtable %#lx, paint slot %d, bounds +%#lx, %d folders stored",
           Native.rowVtable, Native.paintBackgroundSlot, Native.boundsOffset, FolderCount);
}

MOD("bookmark-folder-appearance", Start)
