// Colors bookmark folders and changes their icons, and arranges rows: their nesting, carets and close buttons. Aside's
// classes and methods are bound by name in resolve.c, and the shape of its objects is learned from the first row that paints.

#include "resolve.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include "menu.h"

#define MAX_FOLDERS 1024
#define MAX_CONTAINERS 32
#define MAX_ROWS 4096
#define GUID_SIZE 36
#define BUTTON_STATES 3
#define MAX_TRACKED_ROWS 256
#define MAX_MOTIONS 256
#define MAX_INDENTS 64
#define MAX_GHOSTS 64
#define MAX_GHOST_DEPTH 16
#define CLIP_INTERSECT 1
#define MOTION_MS 180
#define ANIMATOR_ROWS 0xb8
#define ANIMATION_VALUE_SLOT 4
#define CLOSE_BUTTON_SIZE 0x640
#define CLOSE_ICON_SIZE 16
#define PLAIN_ALPHA 0x1a000000u
#define PRESS_METHOD 0x20
#define PRESS_RECEIVER 0x30
#define CORNER_RADIUS 8.0f
#define TICK_MS 16
#define SCAN_LIMIT 0x700
#define ICON_SIZE 16
#define COLLAPSED_ALPHA 0x1a000000u
#define EXPANDED_ALPHA 0x33000000u
#define NEUTRAL_COLOR 0
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
    const BYTE *owner;
} Container;

typedef enum { HOVER_NEVER, HOVER_COLLAPSED, HOVER_ALWAYS } FolderHoverMode;

typedef enum { CARET_RIGHT, CARET_BEFORE_ICON, CARET_BEFORE_NAME, CARET_AFTER_NAME } CaretPosition;

typedef struct {
    int width, height;
} ViewSize;

typedef struct {
    int x, y, width, height;
} ViewRect;

// Extra margin a row has been given below it, and what it had before.
typedef struct {
    BYTE *row;
    const int *owned;
    int base[4];
    int added;
} RowPadding;

// A loaded row on its way to another level: how many levels from there it still is, and how far its parts have moved.
typedef struct {
    BYTE *row;
    double from, shown;
    int movedLeft, movedRight;
    ULONGLONG start;
} IndentMotion;

// A row that just went away because its folder folded, painted a little longer where it was. Either Aside was going to
// delete it, which then happens, or it's a folder this patch hid, which stays.
typedef struct {
    BYTE *row, *container;
    const BYTE *folded;
    int depth, color, foldedBottom;
    int rect[4];
    BOOL kept;
    ULONGLONG start;
} Ghost;

// A backdrop's height as it's shown on the way to what it should be. Aside removes a folded folder's rows at once, so
// it can't just follow them. It belongs to the folder's node: most rebuilds make every row again, and often at the same
// addresses in another order, so a row's address says nothing about what it showed.
typedef struct {
    const BYTE *node;
    int from, to, shown;
    ULONGLONG start;
} BlockMotion;

typedef void *(*SetProperty)(void *properties, const void *key, const char *name, void *deleter, void *value, void *unused);
typedef void (*Deleter)(void *value);
typedef void *(*RowDeleter)(void *row, unsigned flags);
typedef void (*Rebuild)(void *owner, BOOL animate);
typedef void (*CanvasClipRect)(void *canvas, const ViewRect *rect, int operation);
typedef void (*CanvasSaveLayerAlpha)(void *canvas, BYTE alpha, const ViewRect *rect);
typedef void (*ViewPaint)(void *view, void *canvas);
typedef void *(*OperatorNew)(size_t size);
typedef void *(*MeasureSize)(void *view, void *size, const void *bounds);
typedef double (*AnimationValue)(void *animation);
typedef void (*DeleteAccepted)(void *state, int result);
typedef void (*SetViewBounds)(void *view, const ViewRect *rect);
typedef void (*RowFunction)(void *row);
typedef void (*Layout)(void *view, void *key);
typedef void (*SetVisible)(void *view, BOOL visible);
typedef void (*SetButtonImage)(void *button, int state, void *image);
typedef void (*ClosePress)(void *row, void *a, void *b, void *c);
typedef int (*CanvasSave)(void *canvas);
typedef void (*CanvasRestore)(void *canvas);
typedef void (*CanvasConcat)(void *canvas, const float *matrix);
typedef void (*CalculateSize)(const void *view, ViewSize *size, const void *bounds);
typedef void (*SizeChanged)(void *view);
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
typedef void *(*GetWidget)(void *view);
typedef void *(*GetColorProvider)(void *theme);
typedef DWORD (*GetColor)(void *provider, DWORD id);

static Bindings Native;
static BYTE *Base;
static void *RowVtable;
static PaintBackground OriginalRowPaint, OriginalRowBackground, OriginalContainerPaint, OriginalContainerOnPaint, OriginalCaretPaint;
static Layout OriginalRowLayout, OriginalContainerLayout;
static MeasureSize OriginalContainerMeasure;
static ClosePress OriginalClosePress;
static MousePressed OriginalMousePressed;
static RowDeleter OriginalRowDelete;

// Learned from live objects.
static DWORD ParentOffset, ChildrenOffset, NodeOffset, GuidOffset, NodeParentOffset;
static DWORD ImageIconOffset, ImageColorOffset;
static DWORD ClosePressOffset;
static BOOL Learned;
static int LearnAttempts;

static Folder Folders[MAX_FOLDERS];
static int FolderCount;
static char StorePath[MAX_PATH];

static BOOL RowsNarrowed;
static Container Containers[MAX_CONTAINERS];
static int ContainerCount;
static void *Rounded;
static UINT_PTR Timer;

static const IconEntry *Icons;

// The patch's options.
static BOOL CaretShown = TRUE, CaretFlipped, BackdropShown = TRUE, BackdropTinted = TRUE;
static BOOL CloseRemoves, CloseConfirms = TRUE, CloseIsMinus;
static BOOL LoadedRise, ActiveFirst;
static FolderHoverMode FolderHover;
static int BottomOffset = 4;
static CaretPosition Position;
static int ClosedAngle, OpenAngle = 180;
static int LeftOffset = 14, RightOffset = 6;
static BOOL Enabled;

static BYTE *HiddenRows[MAX_TRACKED_ROWS];
static int HiddenCount;
static RowPadding Paddings[MAX_TRACKED_ROWS];
static int PaddingCount;
static BlockMotion Motions[MAX_MOTIONS];
static int MotionCount;
static BOOL BlocksMoving, Projecting;
static IndentMotion Indents[MAX_INDENTS];
static int IndentCount;
static Ghost Ghosts[MAX_GHOSTS];
static int GhostCount;
static BOOL OrderEdited, RebuildWanted;
static BYTE *DeleteOwner;
static LONGLONG DeleteNodeId;
static DWORD CloseIconOffset;
static BOOL CloseIconUnknown;

// Folders store Aside's group colors, the same values its menu's buttons use.
static BOOL IsGroupColor(int color) {
    return color >= 0 && color < GROUP_COLOR_COUNT && Native.groupBackgrounds[color];
}

// Icon, title and background IDs sit together for each color. The neutral one has its own.
static ULONGLONG IconColor(int color) {
    return color == NEUTRAL_COLOR ? Native.groupForeground : Native.groupBackgrounds[color] - 2;
}

static ULONGLONG TitleColor(int color) {
    return color == NEUTRAL_COLOR ? Native.groupForeground : Native.groupBackgrounds[color] - 1;
}

static int FindIcon(const char *key) {
    size_t size = strcspn(key, "\t\r\n");
    for (int i = 0; i < Native.iconCount; i++)
        if (Icons[i].keySize == size && !memcmp(Icons[i].key, key, size)) return i;
    return -1;
}

static void LoadFolders(void) {
    HomeFile("folders.tsv", StorePath, MAX_PATH);
    FILE *file = fopen(StorePath, "r");
    if (!file) return;
    char line[128];
    while (FolderCount < MAX_FOLDERS && fgets(line, sizeof line, file)) {
        int color;
        if (strlen(line) < GUID_SIZE + 2 || line[GUID_SIZE] != '\t' || sscanf(line + GUID_SIZE + 1, "%d", &color) != 1) continue;
        if (color != -1 && !IsGroupColor(color)) continue;
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
    // A row holds more than one thing of this shape once it's loaded. Its own node is stored just before its depth.
    for (DWORD at = Native.boundsOffset - 8; at < SCAN_LIMIT; at += 8) {
        DWORD node = at < Native.boundsOffset ? Native.depthOffset - 8 : at;
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

// Runs on every paint, so it only touches what's out of date. Setting an image repaints.
static void StyleRow(BYTE *row) {
    BYTE *node = NodeOf(row);
    const Folder *folder = node ? FindFolder(GuidOf(node)) : NULL;
    if (!folder) return;
    // Icons are themed like Aside's own until the folder has a color.
    ULONGLONG color = folder->color >= 0 ? IconColor(folder->color) : Native.themedColorId - row[Native.selectedOffset];
    const void *glyph = folder->icon >= 0 ? Icons[folder->icon].icon : Base + Native.folderIcon;
    const void *caretGlyph = Base + Native.caretIcon;
    BYTE *leading = *(BYTE **)(row + Native.leadingOffset), *caret = *(BYTE **)(row + Native.caretOffset);
    if (!leading) return;
    if (ImageIconOffset && ImageIsCurrent(leading, glyph, color) && (!caret || ImageIsCurrent(caret, caretGlyph, color))) return;
    SetImageView(leading, glyph, color);
    if (!ImageIconOffset) LearnImageShape(leading, glyph, color);
    if (caret) SetImageView(caret, caretGlyph, color);
    if (folder->color >= 0) SetRowTitleColor(row, TitleColor(folder->color));
}

static BYTE *OwnerOf(const BYTE *row);

static BOOL FolderIsOpen(const BYTE *owner, const BYTE *node) {
    const LONGLONG *ids = *(const LONGLONG *const *)(owner + Native.expandedOffset);
    size_t low = 0, count = *(const size_t *)(owner + Native.expandedOffset + 8), high = count;
    LONGLONG id = *(const LONGLONG *)(node + Native.nodeIdOffset);
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (ids[middle] < id) low = middle + 1;
        else high = middle;
    }
    return low < count && ids[low] == id;
}

// How many levels above a row its outermost folded folder is, or 0. Aside only keeps a row under a folded folder if
// it's loaded, or on the way to one that is.
static int LevelsToFolded(const BYTE *row) {
    const BYTE *node = Learned ? NodeOf(row) : NULL, *owner = node ? OwnerOf(row) : NULL;
    if (!LoadedRise || !owner) return 0;
    int levels = 0, depth = *(const int *)(row + Native.depthOffset);
    const BYTE *up = NodeParent(node);
    for (int level = 1; level <= depth && up; level++, up = NodeParent(up))
        if (!FolderIsOpen(owner, up)) levels = level;
    return levels;
}

static int RowDepth(const BYTE *row);

// How much a drag changes the nesting of the rows in it: the first one lands at the level of the row after the spot
// it's over, and there's only the top level after the last one.
static int DragDepthChange(const BYTE *row) {
    const BYTE *parent = *(const BYTE *const *)(row + ParentOffset), *first = NULL;
    if (!parent) return 0;
    size_t count;
    void **children = ChildrenOf(parent, &count);
    for (size_t i = 0; i < count && !first; i++)
        if (IsRow(children[i]) && ((const BYTE *)children[i])[Native.dragActiveOffset]) first = children[i];
    if (!first) return 0;
    int y = ((const int *)(first + Native.boundsOffset))[1] + *(const int *)(first + Native.dragOffsetY), best = INT_MAX, landing = 0;
    for (size_t i = 0; i < count; i++) {
        const BYTE *other = children[i];
        if (!IsRow(other) || !IsVisible(other) || other[Native.dragActiveOffset]) continue;
        int top = ((const int *)(other + Native.boundsOffset))[1];
        if (top <= y || top >= best) continue;
        best = top;
        landing = RowDepth(other);
    }
    return landing - *(const int *)(first + Native.depthOffset);
}

// How far in a row's nesting puts it. A collapsed sidebar shows no nesting, a loaded row under a folded folder sits
// directly under that folder, and a dragged row is on its way to somewhere else.
static int RowDepth(const BYTE *row) {
    if (row[Native.collapsedOffset]) return 0;
    if (Learned && row[Native.dragActiveOffset]) return max(*(const int *)(row + Native.depthOffset) + DragDepthChange(row), 0);
    int levels = LevelsToFolded(row);
    return *(const int *)(row + Native.depthOffset) - (levels > 1 ? levels - 1 : 0);
}

static void SetBounds(BYTE *view, int x, int width) {
    const int *bounds = (const int *)(view + Native.boundsOffset);
    ViewRect rect = {x, bounds[1], width, bounds[3]};
    if (bounds[0] != x || bounds[2] != width) ((SetViewBounds)(Base + Native.setBoundsRect))(view, &rect);
}

static IndentMotion *FindIndent(const BYTE *row) {
    for (int i = 0; i < IndentCount; i++)
        if (Indents[i].row == row) return &Indents[i];
    return NULL;
}

// How far along a row is from where its nesting puts it, while it moves to another level.
static int IndentLeft(const BYTE *row) {
    const IndentMotion *motion = FindIndent(row);
    return motion ? lround(motion->shown * LeftOffset) : 0;
}

static int IndentRight(const BYTE *row) {
    const IndentMotion *motion = FindIndent(row);
    return motion ? lround(motion->shown * RightOffset) : 0;
}

// The row has already laid itself out for where it's going, so its parts move by what's left. What's aligned to the
// right moves with the right offset.
static void MoveRowParts(BYTE *row, IndentMotion *motion) {
    int left = IndentLeft(row), right = IndentRight(row);
    const BYTE *caret = *(BYTE **)(row + Native.caretOffset), *close = *(BYTE **)(row + Native.closeOffset);
    size_t count;
    void **children = ChildrenOf(row, &count);
    for (size_t i = 0; i < count; i++) {
        BYTE *child = children[i];
        BOOL aligned = child == close || (child == caret && CaretShown && Position == CARET_RIGHT);
        int move = aligned ? motion->movedRight - right : left - motion->movedLeft;
        const int *bounds = (const int *)(child + Native.boundsOffset);
        if (move) SetBounds(child, bounds[0] + move, bounds[2]);
    }
    motion->movedLeft = left;
    motion->movedRight = right;
}

static void StartIndent(BYTE *row, int inset, int target) {
    const int *bounds = (const int *)(row + Native.boundsOffset);
    IndentMotion *motion = FindIndent(row);
    double shown = motion ? motion->shown : 0;
    // Only a loaded row changes level by folding, and one that's just been made has nowhere to come from.
    if (!LoadedRise || !LeftOffset || !row[Native.loadedOffset] || !bounds[2] || row[Native.dragActiveOffset]) return;
    if (!motion) {
        if (IndentCount == MAX_INDENTS) return;
        motion = &Indents[IndentCount++];
        *motion = (IndentMotion){.row = row};
    }
    motion->from = motion->shown = shown + (double)(inset - target) / LeftOffset;
    motion->start = GetTickCount64();
}

// Rearranges what Aside just laid out, keeping the gaps it left.
static void PlaceCaret(BYTE *row) {
    BYTE *leading = *(BYTE **)(row + Native.leadingOffset), *caret = *(BYTE **)(row + Native.caretOffset), *title = *(BYTE **)(row + Native.titleOffset);
    if (!leading || !caret || !title || !IsVisible(caret) || (CaretShown && Position == CARET_RIGHT)) return;
    const int *icon = (const int *)(leading + Native.boundsOffset), *name = (const int *)(title + Native.boundsOffset), *mark = (const int *)(caret + Native.boundsOffset);
    int start = icon[0], iconWidth = icon[2], caretWidth = mark[2];
    int iconGap = name[0] - (start + iconWidth), caretGap = mark[0] - (name[0] + name[2]), end = mark[0] + caretWidth;
    int iconX = start, nameX = start + iconWidth + iconGap, caretX, nameEnd = end;
    if (!CaretShown) {
        caretX = end;
        caretWidth = 0;
    } else if (Position == CARET_BEFORE_ICON) {
        caretX = start;
        iconX = start + caretWidth + iconGap;
        nameX = iconX + iconWidth + iconGap;
    } else if (Position == CARET_BEFORE_NAME) {
        caretX = nameX;
        nameX = caretX + caretWidth + iconGap;
    } else {
        ViewSize size = {0};
        void **vtable = *(void ***)title;
        ((CalculateSize)vtable[Native.calculateSizeSlot])(title, &size, Base + Native.unboundedSize);
        nameEnd = min(nameX + size.width, end - caretWidth - caretGap);
        caretX = nameEnd + caretGap;
    }
    SetBounds(leading, iconX, iconWidth);
    SetBounds(title, nameX, max(nameEnd - nameX, 0));
    SetBounds(caret, caretX, caretWidth);
}

// Aside lays a row out from the left inset of its layout and its width, and places the close button from that width too.
static void RowLayout(void *view, void *key) {
    BYTE *row = view;
    BYTE *layout = *(BYTE **)(row + Native.rowLayoutOffset);
    int depth = RowDepth(row);
    int *bounds = (int *)(row + Native.boundsOffset), width = bounds[2], right = depth * RightOffset;
    if (layout && !row[Native.collapsedOffset]) *(int *)(layout + Native.insetLeftOffset) = Native.indentBase + depth * LeftOffset;
    if (width > right) bounds[2] = width - right;
    OriginalRowLayout(view, key);
    bounds[2] = width;
    PlaceCaret(row);
    IndentMotion *motion = FindIndent(row);
    if (motion) {
        motion->movedLeft = motion->movedRight = 0;
        MoveRowParts(row, motion);
    }
}

// The caret turns by a share of a half turn that Aside animates. Giving it another share while it paints is all an
// angle takes: its own animation then runs between any two, in either direction.
static void CaretPaint(void *view, void *canvas) {
    BYTE *caret = view;
    double *progress = (double *)(caret + Native.caretProgressOffset), share = *progress;
    if (Learned && IsRow(*(BYTE **)(caret + ParentOffset))) {
        int turn = (((OpenAngle - ClosedAngle) % 360) + 360) % 360;
        if (CaretFlipped && turn) turn -= 360;
        *progress = (ClosedAngle + share * turn) / Native.turnDegrees;
    }
    OriginalCaretPaint(view, canvas);
    *progress = share;
}

static void StyleRow(BYTE *row);
static void StyleClose(BYTE *row);

// A row paints its highlight and outline across its whole width. So for that long it's as wide, and as far along, as
// its nesting.
static void RowPaint(void *view, void *canvas) {
    BYTE *row = view;
    if (Learned) {
        StyleRow(row);
        StyleClose(row);
    }
    int depth = RowDepth(row), left = depth * LeftOffset, right = depth * RightOffset;
    int *bounds = (int *)(row + Native.boundsOffset), width = bounds[2];
    // A drag doesn't lay the row out, and it can change the level it's at.
    BYTE *layout = *(BYTE **)(row + Native.rowLayoutOffset);
    if (row[Native.dragActiveOffset] && layout && *(int *)(layout + Native.insetLeftOffset) != Native.indentBase + left) {
        void **rowVtable = *(void ***)row;
        ((SizeChanged)rowVtable[Native.sizeChangedSlot])(row);
    }
    // A folder's own hover is drawn with the row's, so for this long it isn't hovered.
    const BYTE *node = Learned ? NodeOf(row) : NULL, *owner = node ? OwnerOf(row) : NULL;
    BYTE hovered = row[Native.hoverOffset];
    if (node && *(const int *)(node + Native.nodeTypeOffset) && (FolderHover == HOVER_NEVER || (FolderHover == HOVER_COLLAPSED && owner && FolderIsOpen(owner, node)))) row[Native.hoverOffset] = 0;
    left = max(left + IndentLeft(row), 0);
    right = max(right + IndentRight(row), 0);
    if ((!left && !right) || width <= left + right) {
        OriginalRowPaint(view, canvas);
        row[Native.hoverOffset] = hovered;
        return;
    }
    void *paint = *(void **)((BYTE *)canvas + Native.canvasPaintOffset);
    void **vtable = *(void ***)paint;
    // An SkM44, by columns.
    const float move[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, (float)left, 0, 0, 1};
    ((CanvasSave)vtable[Native.canvasSaveSlot])(paint);
    ((CanvasConcat)vtable[Native.canvasConcatSlot])(paint, move);
    bounds[2] = width - left - right;
    OriginalRowPaint(view, canvas);
    bounds[2] = width;
    row[Native.hoverOffset] = hovered;
    ((CanvasRestore)vtable[Native.canvasRestoreSlot])(paint);
}

// Answers Aside's "Delete this bookmark?" without asking it. The answer's state holds a weak flag, which it releases.
static void DeleteBookmark(void) {
    struct {
        LONG references;
        BYTE invalid;
    } flag = {2, 0};
    BYTE state[0x40] = {0};
    *(void **)(state + 0x28) = &flag;
    *(BYTE **)(state + 0x30) = DeleteOwner;
    *(LONGLONG *)(state + 0x38) = DeleteNodeId;
    DeleteOwner = NULL;
    ((DeleteAccepted)(Base + Native.deleteAccepted))(state, 1);
}

// What a close button runs. On a bookmark that isn't loaded there's no tab to close, so it deletes the bookmark. Without
// asking, that destroys the row and this button, so it waits until the button's press is over.
static void ClosePressed(void *view, void *a, void *b, void *c) {
    BYTE *row = view;
    BYTE *node = NodeOf(row), *owner = OwnerOf(row);
    if (Enabled && CloseRemoves && !row[Native.loadedOffset] && node && owner && !node[Native.nodeManagedOffset]) {
        if (CloseConfirms) {
            MenuAskDeleteBookmark(owner, node);
            return;
        }
        DeleteOwner = owner;
        DeleteNodeId = *(const LONGLONG *)(node + Native.nodeIdOffset);
    } else {
        OriginalClosePress(view, a, b, c);
    }
}

// The button keeps a bound callback that holds a method and the row it runs on.
static void TakeClosePress(BYTE *row, BYTE *close) {
    for (DWORD offset = ClosePressOffset ? ClosePressOffset : 0x1d0; offset < (ClosePressOffset ? ClosePressOffset + 1 : 0x658); offset += 8) {
        BYTE *state;
        void *method, *receiver;
        if (!SafeRead(close + offset, &state, sizeof state) || !state) continue;
        if (!SafeRead(state + PRESS_RECEIVER, &receiver, sizeof receiver) || receiver != row) continue;
        if (!SafeRead(state + PRESS_METHOD, &method, sizeof method)) continue;
        if (method == ClosePressed) return;
        if (!IsInAside(method)) continue;
        ClosePressOffset = offset;
        OriginalClosePress = method;
            *(void **)(state + PRESS_METHOD) = ClosePressed;
        return;
    }
}

static void SetCloseIcon(BYTE *close, const void *icon, DWORD color) {
    void **vtable = *(void ***)close;
    for (int state = 0; state < BUTTON_STATES; state++) {
        BYTE image[64] = {0};
        ((CreateImage)(Base + Native.createImage))(image, icon, (1ull << 32) | color, CLOSE_ICON_SIZE, NULL);
        image[40] = 1;  // optional<ImageModel>'s engaged flag.
        ((SetButtonImage)vtable[Native.closeSetImageSlot])(close, state, image);
        ((DestroyImage)(Base + Native.destroyImage))(image);
    }
}

// An image button keeps the icon it was given. Finding it straight after setting it shows where it lives.
static void LearnCloseIcon(const BYTE *close, const void *icon) {
    for (DWORD offset = 0x200; offset < CLOSE_BUTTON_SIZE; offset += 8) {
        const void *value;
        if (!SafeRead(close + offset, &value, sizeof value) || value != icon) continue;
        CloseIconOffset = offset;
        ModLog("learned: close button icon +%#lx", offset);
        return;
    }
    // Setting it on every paint would never end, so this goes without.
    CloseIconUnknown = TRUE;
    ModLog("couldn't learn the close button's shape, so its icon stays");
}

// Aside only shows the button on a loaded bookmark, and hides it again when the hover ends either way.
static void StyleClose(BYTE *row) {
    BYTE *node = NodeOf(row), *close = *(BYTE **)(row + Native.closeOffset);
    if (!node || !close || *(const int *)(node + Native.nodeTypeOffset) || !row[Native.hoverOffset]) return;
    BOOL loaded = row[Native.loadedOffset];
    if (CloseRemoves && !loaded && !IsVisible(close)) {
        void **vtable = *(void ***)close;
        ((SetVisible)vtable[Native.setVisibleSlot])(close, TRUE);
    }
    if (CloseRemoves) TakeClosePress(row, close);

    if (!Native.minusIcon || CloseIconUnknown) return;
    // Aside gives the button its icon back whenever it likes, so what counts is what the button holds now.
    const void *minus = Base + Native.minusIcon;
    BOOL wanted = CloseIsMinus && loaded, has = CloseIconOffset && *(const void *const *)(close + CloseIconOffset) == minus;
    if (wanted == has || (!wanted && !CloseIconOffset)) return;
    if (wanted) SetCloseIcon(close, minus, *(const DWORD *)(close + Native.closeColorOffset));
    else ((RowFunction)(Base + Native.closeUpdateImage))(close);
    if (!CloseIconOffset) LearnCloseIcon(close, minus);
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

// A background resolves an ID the same way, but a tint needs its own alpha on top.
static BOOL ResolveTint(BYTE *view, int color, BOOL expanded, DWORD *argb) {
    void **vtable = *(void ***)view;
    BYTE *widget = ((GetWidget)vtable[Native.widgetMethodOffset / 8])(view);
    if (!widget) return FALSE;
    void *theme = widget + Native.widgetThemeOffset;
    void **themeVtable = *(void ***)theme;
    void *provider = ((GetColorProvider)themeVtable[Native.themeProviderOffset / 8])(theme);
    if (!provider) return FALSE;
    // Plain is the theme's own text color, which is what contrasts in both light and dark mode.
    DWORD resolved = ((GetColor)(Base + Native.getColor))(provider, BackdropTinted ? Native.groupBackgrounds[color] : Native.titleColorId);
    *argb = (resolved & 0xffffff) | (BackdropTinted ? (expanded ? EXPANDED_ALPHA : COLLAPSED_ALPHA) : PLAIN_ALPHA);
    return TRUE;
}

static void PaintBlock(BYTE *container, void *canvas, const RECT *rect, int left, int right, int color, BOOL expanded) {
    DWORD argb;
    if (!ResolveTint(container, color, expanded, &argb)) return;
    RECT area;
    BoundsOf(container, &area);
    int *insets = (int *)((BYTE *)Rounded + 0x20);
    insets[0] = rect->top;
    insets[1] = rect->left + left;
    insets[2] = (area.bottom - area.top) - rect->bottom;
    insets[3] = (area.right - area.left) - rect->right + right;
    *(ULONGLONG *)((BYTE *)Rounded + 8) = (1ull << 32) | argb;
    void **vtable = *(void ***)Rounded;
    ((Paint)vtable[Native.backgroundPaintOffset / 8])(Rounded, canvas, container);
}

// A folder that shows a backdrop, which is one with a color. Neutral is like Aside's neutral groups, with no fill of its own.
static BOOL HasBackdrop(const BYTE *row) {
    const BYTE *node = NodeOf(row);
    if (!BackdropShown || !node) return FALSE;
    int color = FindColor(GuidOf(node));
    return color >= 0 && color != NEUTRAL_COLOR;
}

// The last showing row inside a folder, or the folder itself.
static size_t LastInside(void **children, size_t count, size_t index) {
    const BYTE *node = NodeOf(children[index]);
    size_t last = index;
    for (size_t j = index + 1; j < count; j++) {
        const BYTE *child = children[j];
        if (!IsRow(child) || !NodeOf(child) || !IsInside(NodeOf(child), node)) break;
        if (IsVisible(child)) last = j;
    }
    return last;
}

// The space a folder's bottom has inside it: its own, and that of each folder inside it ending on the same row.
static int FooterOf(void **children, size_t count, size_t index) {
    size_t last = LastInside(children, count, index);
    if (last == index) return 0;
    int footer = 0;
    for (size_t k = index; k < last; k++)
        if (IsRow(children[k]) && IsVisible(children[k]) && NodeOf(children[k]) && HasBackdrop(children[k]) && LastInside(children, count, k) == last) footer += BottomOffset;
    return footer;
}

// From wherever it's showing now, so turning back halfway stays smooth, on about the curve Aside moves its rows along.
static int ShownHeight(const BYTE *node, int target, int firstHeight, BOOL jump) {
    BlockMotion *motion = NULL;
    for (int i = 0; i < MotionCount && !motion; i++)
        if (Motions[i].node == node) motion = &Motions[i];
    ULONGLONG now = GetTickCount64();
    if (!motion) {
        if (MotionCount == MAX_MOTIONS) MotionCount = 0;
        motion = &Motions[MotionCount++];
        *motion = (BlockMotion){node, firstHeight, target, firstHeight, now};
    }
    if (jump) *motion = (BlockMotion){node, target, target, target, now};
    if (motion->to != target) {
        motion->from = motion->shown;
        motion->to = target;
        motion->start = now;
    }
    ULONGLONG elapsed = now - motion->start;
    if (elapsed >= MOTION_MS || motion->from == motion->to) {
        motion->shown = motion->to;
        return motion->shown;
    }
    double done = (double)elapsed / MOTION_MS;
    motion->shown = motion->from + (int)((motion->to - motion->from) * done * done * (3 - 2 * done));
    BlocksMoving = TRUE;
    return motion->shown;
}

// Aside moves rows with a transform while it animates them, and only changes their bounds at the end. Its animator
// keeps a record for each: where the row started, where it's going, and the animation. Nothing here is assumed to
// still be valid, so a build that's changed this just has the backdrop follow the bounds.
static const BYTE *RowMotion(const BYTE *row) {
    const BYTE *owner = OwnerOf(row), *animator, *entry;
    if (!owner || !SafeRead(owner + Native.animatorOffset, &animator, sizeof animator) || !animator) return NULL;
    const BYTE *root = animator + ANIMATOR_ROWS;
    if (!SafeRead(root, &entry, sizeof entry)) return NULL;
    for (int guard = 0; entry && entry != root && guard < 64; guard++) {
        const BYTE *key;
        if (!SafeRead(entry + 0x20, &key, sizeof key)) return NULL;
        if (key == row) return entry;
        if (!SafeRead(entry + (row > key ? 8 : 0), &entry, sizeof entry)) return NULL;
    }
    return NULL;
}

// A row's rect where it's showing now and where it ends up, and whether it's on its way.
static BOOL RowPlace(const BYTE *row, RECT *now, RECT *end) {
    BoundsOf(row, now);
    *end = *now;
    const BYTE *motion = RowMotion(row);
    int from[4], to[4];
    const BYTE *animation;
    void **vtable;
    if (!motion || !SafeRead(motion + 0x28, from, sizeof from) || !SafeRead(motion + 0x38, to, sizeof to) || !SafeRead(motion + 0x48, &animation, sizeof animation) || !animation) return FALSE;
    if (!SafeRead(animation, &vtable, sizeof vtable) || !IsInAside(vtable)) return FALSE;
    void *method;
    if (!SafeRead(vtable + ANIMATION_VALUE_SLOT, &method, sizeof method) || !IsInAside(method)) return FALSE;
    double value = ((AnimationValue)method)((void *)animation);
    if (!(value >= 0.0 && value <= 1.0)) return FALSE;
    now->left = from[0] + (int)((to[0] - from[0]) * value);
    now->top = from[1] + (int)((to[1] - from[1]) * value);
    now->right = now->left + from[2] + (int)((to[2] - from[2]) * value);
    now->bottom = now->top + from[3] + (int)((to[3] - from[3]) * value);
    *end = (RECT){to[0], to[1], to[0] + to[2], to[1] + to[3]};
    BlocksMoving = TRUE;
    return TRUE;
}

static void PaintFolders(BYTE *container, void *canvas) {
    if (!Rounded || !FolderCount || !BackdropShown) return;
    size_t count;
    void **children = ChildrenOf(container, &count);
    for (size_t i = 0; i < count; i++) {
        BYTE *row = children[i];
        if (!IsRow(row) || !IsVisible(row) || !HasBackdrop(row)) continue;
        int color = FindColor(GuidOf(NodeOf(row)));
        RECT block, end;
        BOOL moving = RowPlace(row, &block, &end);
        if (end.bottom <= end.top) continue;
        int rowHeight = end.bottom - end.top, bottom = end.bottom;
        for (size_t j = i + 1, last = LastInside(children, count, i); j <= last; j++) {
            if (!IsRow(children[j]) || !IsVisible(children[j])) continue;
            RECT childNow, childEnd;
            RowPlace(children[j], &childNow, &childEnd);
            if (childEnd.bottom > childEnd.top) bottom = max(bottom, childEnd.bottom);
        }
        // A dragged folder takes its backdrop with it, right where it is.
        BOOL dragged = row[Native.dragActiveOffset];
        // A row Aside is still bringing in grows from itself. Anything already there has nowhere it was coming from.
        int target = bottom - end.top + FooterOf(children, count, i);
        int height = ShownHeight(NodeOf(row), target, moving ? rowHeight : target, dragged);
        block.bottom = block.top + height;
        if (dragged) {
            block.left += *(const int *)(row + Native.dragOffsetX);
            block.right += *(const int *)(row + Native.dragOffsetX);
            block.top += *(const int *)(row + Native.dragOffsetY);
            block.bottom += *(const int *)(row + Native.dragOffsetY);
        }
        int left = RowDepth(row) * LeftOffset, right = RowDepth(row) * RightOffset;
        // Bounds can already be as narrow as their nesting here, if this is inside the container's own paint.
        if (RowsNarrowed && !moving) {
            block.left -= left;
            block.right += right;
        }
        PaintBlock(container, canvas, &block, left, right, color, height > rowHeight);
    }
}

// Whether a node's folder is open, for a node that might be gone.
static BOOL NodeIsOpen(const BYTE *owner, const BYTE *node) {
    LONGLONG id;
    if (!SafeRead(node + Native.nodeIdOffset, &id, sizeof id)) return TRUE;
    const LONGLONG *ids = *(const LONGLONG *const *)(owner + Native.expandedOffset);
    size_t count = *(const size_t *)(owner + Native.expandedOffset + 8);
    for (size_t i = 0; i < count; i++)
        if (ids[i] == id) return TRUE;
    return FALSE;
}

static const BYTE *FindRowOf(const BYTE *container, const BYTE *node) {
    size_t count;
    void **children = ChildrenOf(container, &count);
    for (size_t i = 0; i < count; i++)
        if (IsRow(children[i]) && NodeOf(children[i]) == node) return children[i];
    return NULL;
}

static double Eased(ULONGLONG start) {
    double done = min((double)(GetTickCount64() - start) / MOTION_MS, 1.0);
    return done * done * (3 - 2 * done);
}

static void Translate(void *canvas, int x, int y) {
    void *paint = *(void **)((BYTE *)canvas + Native.canvasPaintOffset);
    void **vtable = *(void ***)paint;
    const float move[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, (float)x, (float)y, 0, 1};
    ((CanvasConcat)vtable[Native.canvasConcatSlot])(paint, move);
}

static void CanvasPush(void *canvas) {
    void *paint = *(void **)((BYTE *)canvas + Native.canvasPaintOffset);
    void **vtable = *(void ***)paint;
    ((CanvasSave)vtable[Native.canvasSaveSlot])(paint);
}

static void CanvasPop(void *canvas) {
    void *paint = *(void **)((BYTE *)canvas + Native.canvasPaintOffset);
    void **vtable = *(void ***)paint;
    ((CanvasRestore)vtable[Native.canvasRestoreSlot])(paint);
}

// A row that's on its way out is its icon, title and caret, painted where they are in it. Not the row itself or its other
// parts: each normally has a layer of its own, and some clear what they paint on, which here is the whole container.
// A view that's been taken out of the tree has also forgotten its widget, and Aside stops the browser if an icon that needs
// colors has none to ask. So for this long the row and its parts belong to the container's.
static void PaintGhostRow(const Ghost *ghost, void *canvas, int shift) {
    BYTE *row = ghost->row, *parent = *(BYTE **)(row + ParentOffset);
    void *widget = *(void **)(ghost->container + Native.viewWidgetOffset);
    if (!widget) return;
    BYTE *parts[] = {*(BYTE **)(row + Native.leadingOffset), *(BYTE **)(row + Native.titleOffset), *(BYTE **)(row + Native.caretOffset)};
    void *rowWidget = *(void **)(row + Native.viewWidgetOffset), *partWidgets[sizeof parts / sizeof *parts];
    *(void **)(row + Native.viewWidgetOffset) = widget;
    for (size_t i = 0; i < sizeof parts / sizeof *parts; i++) {
        if (!parts[i]) continue;
        partWidgets[i] = *(void **)(parts[i] + Native.viewWidgetOffset);
        *(void **)(parts[i] + Native.viewWidgetOffset) = widget;
    }
    *(BYTE **)(row + ParentOffset) = ghost->container;
    // A rebuild gives a row back Aside's icons, and only a row that paints gets its own again. Its node may be gone.
    char guid[GUID_SIZE];
    if (ReadGuidAt(NodeOf(row), GuidOffset, guid)) StyleRow(row);
    for (size_t i = 0; i < sizeof parts / sizeof *parts; i++) {
        BYTE *part = parts[i];
        if (!part || !IsVisible(part)) continue;
        const int *bounds = (const int *)(part + Native.boundsOffset);
        void **vtable = *(void ***)part;
        CanvasPush(canvas);
        Translate(canvas, ghost->rect[0] + bounds[0], ghost->rect[1] + shift + bounds[1]);
        ((ViewPaint)vtable[Native.caretPaintSlot])(part, canvas);
        CanvasPop(canvas);
    }
    *(BYTE **)(row + ParentOffset) = parent;
    for (size_t i = 0; i < sizeof parts / sizeof *parts; i++)
        if (parts[i]) *(void **)(parts[i] + Native.viewWidgetOffset) = partWidgets[i];
    *(void **)(row + Native.viewWidgetOffset) = rowWidget;
}

static BOOL SameGroup(const Ghost *a, const Ghost *b) {
    return a->folded == b->folded && a->start == b->start;
}

// A folded folder's rows stay where they were, in the part of its backdrop that hasn't folded away yet, and fade. Rows
// that folded together are one group, painted into one layer so their backdrops don't add up as they fade.
static void PaintGhosts(BYTE *container, void *canvas) {
    for (int i = 0; i < GhostCount; i++) {
        const Ghost *first = &Ghosts[i];
        BOOL painted = first->container != container;
        for (int j = 0; j < i && !painted; j++) painted = Ghosts[j].container == container && SameGroup(&Ghosts[j], first);
        if (painted) continue;
        const BYTE *folder = FindRowOf(container, first->folded);
        if (!folder) continue;
        int bottom = first->foldedBottom;
        for (int j = i; j < GhostCount; j++) {
            if (!SameGroup(&Ghosts[j], first)) continue;
            bottom = max(bottom, Ghosts[j].rect[1] + Ghosts[j].rect[3]);
        }
        RECT now, end, area;
        RowPlace(folder, &now, &end);
        BoundsOf(container, &area);
        double eased = Eased(first->start);
        int shift = now.bottom - first->foldedBottom, height = (int)((bottom + BottomOffset - first->foldedBottom) * (1 - eased));
        ViewRect clip = {0, now.bottom, area.right - area.left, height};
        if (height <= 0) continue;
        CanvasPush(canvas);
        ((CanvasClipRect)(Base + Native.canvasClipRect))(canvas, &clip, CLIP_INTERSECT);
        ((CanvasSaveLayerAlpha)(Base + Native.canvasSaveLayerAlpha))(canvas, (BYTE)(255 * (1 - eased)), &clip);
        // A folder's backdrop runs to the next row that isn't inside it, outermost first.
        for (int depth = 0; depth < MAX_GHOST_DEPTH && Rounded; depth++) {
            for (int j = i; j < GhostCount; j++) {
                const Ghost *ghost = &Ghosts[j];
                if (!SameGroup(ghost, first) || ghost->depth != depth || ghost->color < 0) continue;
                const int *bounds = ghost->rect;
                int blockEnd = bottom + BottomOffset;
                for (int k = i; k < GhostCount; k++) {
                    const int *other = Ghosts[k].rect;
                    if (SameGroup(&Ghosts[k], first) && Ghosts[k].depth <= depth && other[1] > bounds[1] && other[1] < blockEnd) blockEnd = other[1];
                }
                RECT block = {bounds[0], bounds[1] + shift, bounds[0] + bounds[2], blockEnd + shift};
                PaintBlock(container, canvas, &block, depth * LeftOffset, depth * RightOffset, ghost->color, TRUE);
            }
        }
        for (int j = i; j < GhostCount; j++)
            if (SameGroup(&Ghosts[j], first)) PaintGhostRow(&Ghosts[j], canvas, shift);
        CanvasPop(canvas);
        CanvasPop(canvas);
    }
}

static void ContainerPaint(void *view, void *canvas) {
    OriginalContainerPaint(view, canvas);
    PaintFolders(view, canvas);
    if (GhostCount) PaintGhosts(view, canvas);
}

// The container draws the outline of a selected or dragged row itself, around that row's bounds. Children paint after
// this returns, so for this long each row can simply be where its nesting puts it.
static void NarrowRows(BYTE *container, int sign) {
    size_t count;
    void **children = ChildrenOf(container, &count);
    for (size_t i = 0; i < count; i++) {
        BYTE *row = children[i];
        if (!IsRow(row)) continue;
        int depth = RowDepth(row), left = max(depth * LeftOffset + IndentLeft(row), 0), right = max(depth * RightOffset + IndentRight(row), 0);
        int *bounds = (int *)(row + Native.boundsOffset);
        bounds[0] += sign * left;
        bounds[2] -= sign * (left + right);
    }
}

static void ContainerOnPaint(void *view, void *canvas) {
    NarrowRows(view, 1);
    RowsNarrowed = TRUE;
    OriginalContainerOnPaint(view, canvas);
    RowsNarrowed = FALSE;
    NarrowRows(view, -1);
}

static const int *RowMargins(const BYTE *row) {
    const void *key = Base + Native.marginsKey;
    const BYTE *end = row + Native.propertiesOffset + 0x10, *node = *(const BYTE *const *)end;
    while (node) {
        const void *at = *(const void *const *)(node + 0x20);
        if (at == key) return *(const int *const *)(node + 0x30);
        node = *(const BYTE *const *)(node + (key > at ? 8 : 0));
    }
    return NULL;
}

static void SetRowPadding(BYTE *row, int padding) {
    const int *current = RowMargins(row);
    RowPadding *entry = NULL;
    for (int i = 0; i < PaddingCount && !entry; i++)
        if (Paddings[i].row == row) entry = &Paddings[i];
    // Aside can replace them, or clear them in place, when it rebuilds.
    if (entry && (current != entry->owned || !current || current[2] != entry->base[2] + entry->added)) {
        *entry = Paddings[--PaddingCount];
        entry = NULL;
    }
    if (!entry && !padding) return;
    if (!entry) {
        if (PaddingCount == MAX_TRACKED_ROWS) PaddingCount = 0;
        entry = &Paddings[PaddingCount++];
        *entry = (RowPadding){.row = row};
        if (current) memcpy(entry->base, current, sizeof entry->base);
    }
    if (entry->added == padding) return;
    int *value = ((OperatorNew)(Base + Native.operatorNew))(sizeof entry->base);
    memcpy(value, entry->base, sizeof entry->base);
    value[2] += padding;
    void *old = ((SetProperty)(Base + Native.setProperty))(row + Native.propertiesOffset, Base + Native.marginsKey, (const char *)(Base + Native.marginsName), Base + Native.marginsDeleter, value, NULL);
    if (old) ((Deleter)(Base + Native.marginsDeleter))(old);
    entry->owned = value;
    entry->added = padding;
    void **vtable = *(void ***)row;
    ((SizeChanged)vtable[Native.sizeChangedSlot])(row);
}

static void HoldGhost(BYTE *row, BOOL kept);

static void SetRowHidden(BYTE *row, BOOL hidden) {
    int found = -1;
    for (int i = 0; i < HiddenCount && found < 0; i++)
        if (HiddenRows[i] == row) found = i;
    void **vtable = *(void ***)row;
    if (hidden) {
        if (found < 0 && HiddenCount < MAX_TRACKED_ROWS) HiddenRows[HiddenCount++] = row;
        if (IsVisible(row)) {
            HoldGhost(row, TRUE);
            ((SetVisible)vtable[Native.setVisibleSlot])(row, FALSE);
        }
    } else if (found >= 0) {
        HiddenRows[found] = HiddenRows[--HiddenCount];
        ((SetVisible)vtable[Native.setVisibleSlot])(row, TRUE);
    }
}

// Aside remembers where every row was, to animate it from there, and a hidden row has been nowhere since it was last
// laid out, or ever. So it's kept where Aside would start a new row: at the bottom of the folder it's under, with no height.
static void PlaceHiddenRows(const BYTE *container) {
    if (!HiddenCount) return;
    size_t count;
    void **children = ChildrenOf(container, &count);
    for (size_t i = 0; i < count; i++) {
        BYTE *row = children[i];
        if (!IsRow(row) || IsVisible(row) || !NodeOf(row)) continue;
        BOOL hidden = FALSE;
        for (int j = 0; j < HiddenCount && !hidden; j++) hidden = HiddenRows[j] == row;
        int levels = hidden ? LevelsToFolded(row) : 0;
        if (!levels) continue;
        const BYTE *up = NodeOf(row);
        while (levels-- && up) up = NodeParent(up);
        const BYTE *folder = up ? FindRowOf(container, up) : NULL;
        if (!folder) continue;
        RECT now, end;
        BOOL moving = BlocksMoving;
        RowPlace(folder, &now, &end);
        BlocksMoving = moving;
        int *bounds = (int *)(row + Native.boundsOffset);
        bounds[0] = end.left;
        bounds[1] = end.bottom;
        bounds[2] = end.right - end.left;
        bounds[3] = 0;
    }
}

// Everything that changes how tall rows are has to be in place before Aside measures them, so its own animation heads
// for the right place.
static void ArrangeForLayout(BYTE *container) {
    if (Projecting || !Learned || !Enabled) return;
    Projecting = TRUE;
    size_t count;
    void **children = ChildrenOf(container, &count);
    for (size_t i = 0; i < count; i++) {
        BYTE *row = children[i];
        if (!IsRow(row) || !NodeOf(row)) continue;
        // A folder under a folded folder is only there because of a loaded row, which now sits under the folded one.
        SetRowHidden(row, *(const int *)(NodeOf(row) + Native.nodeTypeOffset) && LevelsToFolded(row) && !row[Native.dragActiveOffset]);
        BYTE *layout = *(BYTE **)(row + Native.rowLayoutOffset);
        int inset = Native.indentBase + RowDepth(row) * LeftOffset;
        // Only a showing row lays itself out and takes this up.
        if (layout && IsVisible(row) && !row[Native.collapsedOffset] && *(int *)(layout + Native.insetLeftOffset) != inset) {
            StartIndent(row, *(int *)(layout + Native.insetLeftOffset), inset);
            *(int *)(layout + Native.insetLeftOffset) = inset;
            void **vtable = *(void ***)row;
            ((SizeChanged)vtable[Native.sizeChangedSlot])(row);
            Repaint(row);
        }
    }
    for (size_t i = 0; i < count; i++) {
        BYTE *row = children[i];
        if (!IsRow(row) || !NodeOf(row)) continue;
        int padding = 0;
        // What ends on this row: each folder it's the last row of.
        if (IsVisible(row))
            for (size_t k = 0; k < i; k++)
                if (IsRow(children[k]) && IsVisible(children[k]) && NodeOf(children[k]) && HasBackdrop(children[k]) && LastInside(children, count, k) == i) padding += BottomOffset;
        SetRowPadding(row, padding);
    }
    PlaceHiddenRows(container);
    Projecting = FALSE;
}

static void ContainerLayout(void *view, void *key) {
    ArrangeForLayout(view);
    OriginalContainerLayout(view, key);
}

static void *ContainerMeasure(void *view, void *size, const void *bounds) {
    ArrangeForLayout(view);
    return OriginalContainerMeasure(view, size, bounds);
}

static void AddContainer(BYTE *view, const BYTE *owner) {
    for (int i = 0; i < ContainerCount; i++) {
        if (Containers[i].view != view) continue;
        if (owner) Containers[i].owner = owner;
        return;
    }
    if (ContainerCount == MAX_CONTAINERS) return;
    void **vtable = *(void ***)view;
    if (!HookVtable(vtable, Native.paintBackgroundSlot, ContainerPaint, (void **)&OriginalContainerPaint)) {
        ModLog("couldn't hook the rows container");
        return;
    }
    if (!HookVtable(vtable, Native.caretPaintSlot, ContainerOnPaint, (void **)&OriginalContainerOnPaint)) ModLog("couldn't hook the rows container's paint");
    if (!HookVtable(vtable, Native.layoutSlot, ContainerLayout, (void **)&OriginalContainerLayout)) ModLog("couldn't hook the rows container's layout");
    if (!HookVtable(vtable, Native.calculateSizeSlot, ContainerMeasure, (void **)&OriginalContainerMeasure)) ModLog("couldn't hook the rows container's size");
    ((SizeChanged)vtable[Native.sizeChangedSlot])(view);
    Containers[ContainerCount++] = (Container){.view = view, .vtable = vtable, .owner = owner};
    ModLog("rows container %p", view);
    Repaint(view);
}

static BOOL ContainerIsLive(const BYTE *view) {
    for (int i = 0; i < ContainerCount; i++) {
        void *vtable;
        if (Containers[i].view == view) return SafeRead(view, &vtable, sizeof vtable) && vtable == Containers[i].vtable;
    }
    return FALSE;
}

static const Container *ContainerOfOwner(const BYTE *owner) {
    for (int i = 0; i < ContainerCount; i++)
        if (Containers[i].owner == owner && ContainerIsLive(Containers[i].view)) return &Containers[i];
    return NULL;
}

static BOOL RowIsLive(const BYTE *row) {
    for (int i = 0; i < ContainerCount; i++) {
        if (!ContainerIsLive(Containers[i].view)) continue;
        size_t count;
        void **children = ChildrenOf(Containers[i].view, &count);
        for (size_t j = 0; j < count; j++)
            if (children[j] == row) return TRUE;
    }
    return FALSE;
}

// The outermost folded folder a row is under, or NULL. The row's node may be gone if its bookmark was deleted.
static const BYTE *FoldedAbove(const BYTE *row, const BYTE *owner) {
    const BYTE *node = NodeOf(row), *folded = NULL;
    int depth = *(const int *)(row + Native.depthOffset);
    for (int level = 0; level < depth && node; level++) {
        if (!SafeRead(node + NodeParentOffset, &node, sizeof node) || !node) break;
        if (!NodeIsOpen(owner, node)) folded = node;
    }
    return folded;
}

// Whether a row that's going away has a folded folder to fade into, and if so it does.
static BOOL TryHoldGhost(BYTE *row, BOOL kept) {
    if (!Enabled || !Learned || !Native.canvasClipRect || !Native.canvasSaveLayerAlpha || !Native.viewWidgetOffset || GhostCount == MAX_GHOSTS) return FALSE;
    const int *bounds = (const int *)(row + Native.boundsOffset);
    int depth = *(const int *)(row + Native.depthOffset);
    if (!IsVisible(row) || row[Native.collapsedOffset] || row[Native.dragActiveOffset] || bounds[3] <= 0 || depth >= MAX_GHOST_DEPTH) return FALSE;
    const BYTE *owner = OwnerOf(row);
    const Container *container = owner ? ContainerOfOwner(owner) : NULL;
    const BYTE *folded = container ? FoldedAbove(row, owner) : NULL;
    const BYTE *folder = folded ? FindRowOf(container->view, folded) : NULL;
    if (!folder) return FALSE;
    RECT now, end;
    RowPlace(folder, &now, &end);
    char guid[GUID_SIZE];
    int color = BackdropShown && ReadGuidAt(NodeOf(row), GuidOffset, guid) ? FindColor(guid) : -1;
    ULONGLONG start = GetTickCount64();
    // A folder opening again ends its ghosts, so any it still has folded with this one.
    for (int i = 0; i < GhostCount; i++)
        if (Ghosts[i].folded == folded) start = Ghosts[i].start;
    Ghost *ghost = &Ghosts[GhostCount++];
    *ghost = (Ghost){row, container->view, folded, depth, color == NEUTRAL_COLOR ? -1 : color, now.bottom, .kept = kept, .start = start};
    memcpy(ghost->rect, bounds, sizeof ghost->rect);
    Repaint(container->view);
    return TRUE;
}

static void HoldGhost(BYTE *row, BOOL kept) {
    TryHoldGhost(row, kept);
}

// Aside deletes the rows of a folder that just folded at the end of its rebuild. Called from anywhere else, it's not
// that: a window closing leaves nothing to paint them into.
static void *RowDelete(void *view, unsigned flags) {
    const BYTE *caller = __builtin_return_address(0);
    BOOL rebuilding = (flags & 1) && caller >= Base + Native.rebuild && caller < Base + Native.rebuildEnd;
    for (int i = 0; i < HiddenCount; i++) {
        if (HiddenRows[i] != view) continue;
        HiddenRows[i] = HiddenRows[--HiddenCount];
        break;
    }
    // Aside lays out before it rebuilds, so a folder under the one that folded can have been hidden on its way here.
    for (int i = 0; i < GhostCount; i++) {
        if (Ghosts[i].row != view || !Ghosts[i].kept) continue;
        if (rebuilding) {
            Ghosts[i].kept = FALSE;
            return view;
        }
        Ghosts[i] = Ghosts[--GhostCount];
        i--;
    }
    if (rebuilding && TryHoldGhost(view, FALSE)) return view;
    return OriginalRowDelete(view, flags);
}

static void AdvanceIndents(void) {
    for (int i = 0; i < IndentCount; i++) {
        IndentMotion *motion = &Indents[i];
        if (!RowIsLive(motion->row)) {
            *motion = Indents[--IndentCount];
            i--;
            continue;
        }
        double eased = Eased(motion->start);
        motion->shown = motion->from * (1 - eased);
        MoveRowParts(motion->row, motion);
        Repaint(motion->row);
        BlocksMoving = TRUE;
        if (eased < 1.0) continue;
        *motion = Indents[--IndentCount];
        i--;
    }
}

// A ghost is done when its time is up, or as soon as its folder opens again. With its window gone, what it would
// tell on its way out is too, so it's left alone.
static void AdvanceGhosts(BOOL finish) {
    for (int i = 0; i < GhostCount; i++) {
        Ghost *ghost = &Ghosts[i];
        BOOL live = ContainerIsLive(ghost->container);
        const BYTE *owner = live ? OwnerOf(ghost->row) : NULL;
        if (live && !finish && GetTickCount64() - ghost->start < MOTION_MS && (!owner || !NodeIsOpen(owner, ghost->folded))) {
            BlocksMoving = TRUE;
            continue;
        }
        BYTE *row = ghost->row, *container = ghost->container;
        BOOL kept = ghost->kept;
        *ghost = Ghosts[--GhostCount];
        i--;
        if (!live) continue;
        Repaint(container);
        if (kept) continue;
        void **vtable = *(void ***)row;
        ((RowDeleter)vtable[Native.rowDeleteSlot])(row, 1);
    }
}

// Aside puts the active tab first by looking for it just before its list of loaded tabs. Jumping over that leaves the list.
static void ApplyActiveOrder(void) {
    if (!Native.activeOrderSite || OrderEdited == !ActiveFirst) return;
    static BYTE original[5];
    BYTE *site = Base + Native.activeOrderSite;
    BYTE jump[5] = {0xe9};
    *(INT32 *)(jump + 1) = (INT32)(Native.activeOrderSkip - (Native.activeOrderSite + 5));
    if (!OrderEdited) memcpy(original, site, sizeof original);
    if (EditCode(site, OrderEdited ? jump : original, OrderEdited ? original : jump, sizeof jump)) OrderEdited = !OrderEdited;
    else ModLog("couldn't change the order of a folded folder's rows");
}

// Rows move without their container repainting, and the blocks have to follow them.
// Containers aren't told they're gone, but a freed one no longer has this vtable.
static void CALLBACK Tick(HWND window, UINT message, UINT_PTR timer, DWORD time) {
    (void)window, (void)message, (void)timer, (void)time;
    MenuTick();
    if (DeleteOwner) DeleteBookmark();
    if (RebuildWanted && Native.rebuild) {
        RebuildWanted = FALSE;
        for (int i = 0; i < ContainerCount; i++)
            if (Containers[i].owner && ContainerIsLive(Containers[i].view)) ((Rebuild)(Base + Native.rebuild))((void *)Containers[i].owner, FALSE);
    }
    AdvanceIndents();
    AdvanceGhosts(FALSE);
    for (int i = 0; i < ContainerCount; i++)
        if (ContainerIsLive(Containers[i].view)) PlaceHiddenRows(Containers[i].view);
    BOOL moving = BlocksMoving;
    BlocksMoving = FALSE;
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
        if (hash == container->lastBounds && !moving) continue;
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
    if (!Rounded) ((CreateRounded)(Base + Native.roundedBackground))(&Rounded, 1ull << 32, CORNER_RADIUS, 0);
    if (!Rounded) ModLog("couldn't create the block background");
    if (!Timer) Timer = SetTimer(NULL, 0, TICK_MS, Tick);
}

static void RowBackground(void *view, void *canvas) {
    BYTE *row = view;
    OriginalRowBackground(view, canvas);
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
    }
    StartOnUiThread();
    BYTE *parent = *(BYTE **)(row + ParentOffset);
    if (!parent) return;
    AddContainer(parent, OwnerOf(row));
}

// Rows lay themselves out again, which puts back whatever isn't moved any more, and repaint.
static void ArrangeRows(void) {
    for (int i = 0; i < ContainerCount; i++) {
        void *vtable;
        if (!SafeRead(Containers[i].view, &vtable, sizeof vtable) || vtable != Containers[i].vtable) continue;
        size_t count;
        void **children = ChildrenOf(Containers[i].view, &count);
        for (size_t j = 0; j < count; j++) {
            if (!IsRow(children[j])) continue;
            BYTE *row = children[j];
            void **rowVtable = *(void ***)row;
            // An option changing is no reason to move anything gradually.
            BYTE *layout = *(BYTE **)(row + Native.rowLayoutOffset);
            if (Enabled && Learned && layout && !row[Native.collapsedOffset]) *(int *)(layout + Native.insetLeftOffset) = Native.indentBase + RowDepth(row) * LeftOffset;
            ((SizeChanged)rowVtable[Native.sizeChangedSlot])(row);
            Repaint(row);
        }
        Repaint(Containers[i].view);
    }
}

static int AngleOf(const char *direction) {
    return !strcmp(direction, "left") ? 90 : !strcmp(direction, "up") ? 180 : !strcmp(direction, "right") ? 270 : 0;
}

PATCH_OPTION() {
    BOOL on = !strcmp(value, "true");
    if (!strcmp(key, "indicator.show")) CaretShown = on;
    else if (!strcmp(key, "indicator.closed")) ClosedAngle = AngleOf(value);
    else if (!strcmp(key, "indicator.open")) OpenAngle = AngleOf(value);
    else if (!strcmp(key, "indicator.flip")) CaretFlipped = on;
    else if (!strcmp(key, "backdrop.show")) BackdropShown = on;
    else if (!strcmp(key, "backdrop.tinted")) BackdropTinted = on;
    else if (!strcmp(key, "nesting.left")) LeftOffset = atoi(value);
    else if (!strcmp(key, "nesting.right")) RightOffset = atoi(value);
    else if (!strcmp(key, "nesting.bottom")) BottomOffset = atoi(value);
    else if (!strcmp(key, "nesting.loaded")) LoadedRise = on;
    else if (!strcmp(key, "nesting.active-first")) {
        ActiveFirst = on;
        if (Enabled) {
            ApplyActiveOrder();
            RebuildWanted = TRUE;
        }
    }
    else if (!strcmp(key, "backdrop.hover")) FolderHover = on ? HOVER_ALWAYS : !strcmp(value, "collapsed") ? HOVER_COLLAPSED : HOVER_NEVER;
    else if (!strcmp(key, "bookmark.remove")) CloseRemoves = on;
    else if (!strcmp(key, "bookmark.confirm")) CloseConfirms = on;
    else if (!strcmp(key, "bookmark.minus")) CloseIsMinus = on;
    else if (!strcmp(key, "indicator.position")) {
        Position = !strcmp(value, "before-icon") ? CARET_BEFORE_ICON : !strcmp(value, "before-name") ? CARET_BEFORE_NAME : !strcmp(value, "after-name") ? CARET_AFTER_NAME : CARET_RIGHT;
    }
    if (Enabled) ArrangeRows();
}

PATCH_START() {
    const char *missing = ResolveBindings(lookup, &Native);
    if (missing) return missing;
    Base = base;
    RowVtable = base + Native.rowVtable;
    Icons = (const IconEntry *)(base + Native.iconTable);
    return NULL;
}

PATCH_ENABLE() {
    // The icon table holds pointers, and they only mean something in the loaded DLL.
    if (!StorePath[0]) LoadFolders();
    if (!HookVtable(RowVtable, Native.caretPaintSlot, RowPaint, (void **)&OriginalRowPaint)) ModLog("couldn't hook the row's paint");
    if (!HookVtable(RowVtable, Native.paintBackgroundSlot, RowBackground, (void **)&OriginalRowBackground)) {
        ModLog("couldn't hook the row");
        return;
    }
    if (!HookVtable(RowVtable, Native.mousePressedSlot, RowMousePressed, (void **)&OriginalMousePressed)) ModLog("couldn't hook the row's clicks");
    if (!HookVtable(RowVtable, Native.layoutSlot, RowLayout, (void **)&OriginalRowLayout)) ModLog("couldn't hook the row's layout");
    if (!HookVtable((void **)(Base + Native.caretVtable), Native.caretPaintSlot, CaretPaint, (void **)&OriginalCaretPaint)) ModLog("couldn't hook the caret");
    if (Native.rebuild && !HookVtable(RowVtable, Native.rowDeleteSlot, RowDelete, (void **)&OriginalRowDelete)) ModLog("couldn't hook how a row is deleted");
    if (!Native.rebuild || !Native.canvasClipRect || !Native.canvasSaveLayerAlpha || !Native.viewWidgetOffset) ModLog("a folding folder's rows won't fade in this build");
    OrderEdited = FALSE;
    ApplyActiveOrder();
    // Rows on screen paint again, and that brings back everything else.
    void *rows[MAX_ROWS];
    int count = FindObjects(RowVtable, rows, MAX_ROWS);
    for (int i = 0; i < count; i++) {
        void **rowVtable = *(void ***)rows[i];
        ((SizeChanged)rowVtable[Native.sizeChangedSlot])(rows[i]);
        Repaint(rows[i]);
    }
    Enabled = TRUE;
    ModLog("bookmark-folder-appearance ready: row vtable %#lx, paint slot %d, bounds +%#lx, %d folders stored, %d rows live",
           Native.rowVtable, Native.paintBackgroundSlot, Native.boundsOffset, FolderCount, count);
}

PATCH_DISABLE() {
    Enabled = FALSE;
    if (Timer) KillTimer(NULL, Timer);
    Timer = 0;
    MenuTick();
    AdvanceGhosts(TRUE);
    IndentCount = 0;
    OrderEdited = FALSE;
    RebuildWanted = FALSE;
    for (int i = 0; i < ContainerCount; i++) {
        void *vtable;
        if (!SafeRead(Containers[i].view, &vtable, sizeof vtable) || vtable != Containers[i].vtable) continue;
        size_t count;
        void **children = ChildrenOf(Containers[i].view, &count);
        for (size_t j = 0; j < count; j++) {
            if (!IsRow(children[j])) continue;
            RestoreRow(children[j]);
            ((RowFunction)(Base + Native.updateIndent))(children[j]);
            SetRowHidden(children[j], FALSE);
            SetRowPadding(children[j], 0);
            BYTE *close = *(BYTE **)((BYTE *)children[j] + Native.closeOffset);
            if (close && Native.minusIcon && CloseIconOffset && *(const void *const *)(close + CloseIconOffset) == Base + Native.minusIcon)
                ((RowFunction)(Base + Native.closeUpdateImage))(close);
        }
    }
    ArrangeRows();
    ContainerCount = 0;
}
