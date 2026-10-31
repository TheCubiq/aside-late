// Colors bookmark folders. Aside's classes and methods are bound by name in resolve.c, and
// the shape of its objects is learned here from the first row that paints.

#include "mod/mod.h"
#include "resolve.h"

#include <stdio.h>
#include "flyout.h"

#define MAX_FOLDERS 1024
#define MAX_CONTAINERS 32
#define GUID_SIZE 36
#define INDENT 14
#define CORNER_RADIUS 8.0f
#define TICK_MS 16
#define SCAN_LIMIT 0x700

typedef struct {
    char guid[GUID_SIZE];
    int color;
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

static Bindings Native;
static BYTE *Base;
static void *RowVtable;
static PaintBackground OriginalRowPaint, OriginalContainerPaint;

// Learned from live objects.
static DWORD ParentOffset, ChildrenOffset, NodeOffset, GuidOffset, NodeParentOffset;
static BOOL Learned;
static int LearnAttempts;

static Folder Folders[MAX_FOLDERS];
static int FolderCount;
static char StorePath[MAX_PATH];

static Container Containers[MAX_CONTAINERS];
static int ContainerCount;
static BYTE *LastPaintedContainer;
static void *Rounded;

static const DWORD FolderColors[COLOR_COUNT] = {
    0xDADCE0, 0x8AB4F8, 0xF28B82, 0xFDD663, 0x81C995, 0xFF8BCB, 0xC58AF9, 0x78D9EC,
};

static void LoadFolders(void) {
    if (!ExpandEnvironmentStringsA("%LOCALAPPDATA%\\AsideLate\\folders.tsv", StorePath, MAX_PATH)) return;
    FILE *file = fopen(StorePath, "r");
    if (!file) return;
    char line[128];
    while (FolderCount < MAX_FOLDERS && fgets(line, sizeof line, file)) {
        int color;
        if (strlen(line) < GUID_SIZE + 2 || line[GUID_SIZE] != '\t' || sscanf(line + GUID_SIZE + 1, "%d", &color) != 1) continue;
        if (color < 0 || color >= COLOR_COUNT) continue;
        memcpy(Folders[FolderCount].guid, line, GUID_SIZE);
        Folders[FolderCount++].color = color;
    }
    fclose(file);
}

static void SaveFolders(void) {
    FILE *file = fopen(StorePath, "w");
    if (!file) return;
    for (int i = 0; i < FolderCount; i++) fprintf(file, "%.36s\t%d\n", Folders[i].guid, Folders[i].color);
    fclose(file);
}

static int FindColor(const char *guid) {
    for (int i = 0; i < FolderCount; i++)
        if (!memcmp(Folders[i].guid, guid, GUID_SIZE)) return Folders[i].color;
    return -1;
}

static void SetColor(const char *guid, int color) {
    for (int i = 0; i < FolderCount; i++) {
        if (memcmp(Folders[i].guid, guid, GUID_SIZE)) continue;
        if (color < 0) Folders[i] = Folders[--FolderCount];
        else Folders[i].color = color;
        SaveFolders();
        return;
    }
    if (color < 0 || FolderCount == MAX_FOLDERS) return;
    memcpy(Folders[FolderCount].guid, guid, GUID_SIZE);
    Folders[FolderCount++].color = color;
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

// A bookmark's GURL keeps its spec in a string after the GUID. A folder has no URL.
static BOOL IsFolder(const BYTE *node) {
    for (DWORD offset = GuidOffset + 24; offset < GuidOffset + 0x100; offset += 8) {
        const BYTE *field = node + offset;
        const char *text = (const char *)field;
        size_t size = field[23];
        if (field[23] & 0x80) {
            text = *(const char *const *)field;
            size = *(const size_t *)(field + 8);
            char head[16];
            if (size < 8 || size > 1 << 20 || !SafeRead(text, head, sizeof head)) continue;
            text = head;
            size = sizeof head;
            for (size_t i = 0; i + 3 <= size; i++)
                if (!memcmp(text + i, "://", 3)) return FALSE;
            continue;
        }
        if (size < 4 || size > 22) continue;
        for (size_t i = 0; i + 3 <= size; i++)
            if (!memcmp(text + i, "://", 3)) return FALSE;
    }
    return TRUE;
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
    if (!FolderCount) return;
    for (int i = 0; i < ContainerCount; i++) {
        Container *container = &Containers[i];
        void *vtable;
        if (!SafeRead(container->view, &vtable, sizeof vtable) || vtable != container->vtable) {
            if (LastPaintedContainer == container->view) LastPaintedContainer = NULL;
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

static BOOL FindRowAt(HWND window, POINT client, BYTE **foundRow, BYTE **foundContainer) {
    double scale = GetDpiForWindow(window) / 96.0;
    int x = (int)(client.x / scale), y = (int)(client.y / scale);
    for (int attempt = 0; attempt < 2; attempt++) {
        for (int i = 0; i < ContainerCount; i++) {
            BYTE *container = Containers[i].view;
            // With several windows the sidebars overlap in client coordinates, but moving
            // the mouse onto a row repaints it, so the last container painted is the one.
            if (attempt == 0 && container != LastPaintedContainer) continue;
            int originX = 0, originY = 0;
            for (BYTE *view = container; view; view = *(BYTE **)(view + ParentOffset)) {
                RECT bounds;
                BoundsOf(view, &bounds);
                originX += bounds.left;
                originY += bounds.top;
            }
            size_t count;
            void **children = ChildrenOf(container, &count);
            for (size_t j = 0; j < count; j++) {
                BYTE *row = children[j];
                if (!IsRow(row) || !IsVisible(row)) continue;
                RECT bounds;
                BoundsOf(row, &bounds);
                POINT local = {x - originX, y - originY};
                if (!PtInRect(&bounds, local)) continue;
                *foundRow = row;
                *foundContainer = container;
                return TRUE;
            }
        }
    }
    return FALSE;
}

static BYTE *FlyoutContainer;
static char FlyoutGuid[GUID_SIZE];

static void OnColorPicked(int color) {
    SetColor(FlyoutGuid, color);
    ModLog("folder %.36s color %d", FlyoutGuid, color);
    for (int i = 0; i < ContainerCount; i++) Repaint(Containers[i].view);
}

static void OnRightClick(const MSG *message) {
    if (!Learned) return;
    POINT client = {GET_X_LPARAM(message->lParam), GET_Y_LPARAM(message->lParam)};
    BYTE *row, *container;
    if (!FindRowAt(message->hwnd, client, &row, &container)) return;
    BYTE *node = NodeOf(row);
    ModLog("right click on row %p: folder %d", row, node && IsFolder(node));
    if (!node || !IsFolder(node)) return;
    memcpy(FlyoutGuid, GuidOf(node), GUID_SIZE);
    FlyoutContainer = container;
    POINT screen = client;
    ClientToScreen(message->hwnd, &screen);
    FlyoutShow(message->hwnd, screen, FolderColors, FindColor(FlyoutGuid), OnColorPicked);
}

static LRESULT CALLBACK OnMessage(int code, WPARAM removed, LPARAM param) {
    if (code >= 0 && removed == PM_REMOVE) {
        const MSG *message = (const MSG *)param;
        if (message->message == WM_RBUTTONUP) OnRightClick(message);
    }
    return CallNextHookEx(NULL, code, removed, param);
}

static void StartOnUiThread(void) {
    ((CreateRounded)(Base + Native.roundedBackground))(&Rounded, 1ull << 32, CORNER_RADIUS, 0);
    if (!Rounded) ModLog("couldn't create the block background");
    SetTimer(NULL, 0, TICK_MS, Tick);
    SetWindowsHookExW(WH_GETMESSAGE, OnMessage, NULL, GetCurrentThreadId());
}

static void RowPaint(void *view, void *canvas) {
    OriginalRowPaint(view, canvas);
    BYTE *row = view;
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
        StartOnUiThread();
    }
    BYTE *parent = *(BYTE **)(row + ParentOffset);
    if (!parent) return;
    AddContainer(parent);
    LastPaintedContainer = parent;
}

static void Start(const Lookup *lookup, BYTE *base) {
    const char *missing = ResolveBindings(lookup, &Native);
    if (missing) {
        ModLog("bookmark-folder-appearance refused: didn't find %s", missing);
        return;
    }
    Base = base;
    RowVtable = base + Native.rowVtable;
    LoadFolders();
    if (!SwapVtableEntry(RowVtable, Native.paintBackgroundSlot, RowPaint, (void **)&OriginalRowPaint)) {
        ModLog("couldn't hook the row");
        return;
    }
    ModLog("bookmark-folder-appearance ready: row vtable %#lx, paint slot %d, bounds +%#lx, %d folders stored",
           Native.rowVtable, Native.paintBackgroundSlot, Native.boundsOffset, FolderCount);
}

MOD("bookmark-folder-appearance", Start)
