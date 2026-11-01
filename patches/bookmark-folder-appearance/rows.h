// What the patch's files share. resolve.h is what's bound in Aside. This is what's learned and kept on top of it, by the
// file that owns each part.
#pragma once

#include "folders/menu.h"
#include "resolve/resolve.h"

#define GUID_SIZE 36
#define NEUTRAL_COLOR 0
#define MAX_CONTAINERS 32
#define MAX_TRACKED_ROWS 256
#define MAX_MOTIONS 256
#define MOTION_MS 180
#define PRESS_METHOD 0x20  // Where a bound callback's state holds the method it runs.

typedef enum { HOVER_NEVER, HOVER_COLLAPSED, HOVER_ALWAYS } FolderHoverMode;
typedef enum { UNLOAD_NEVER, UNLOAD_COLLAPSED, UNLOAD_ALWAYS } FolderUnloadMode;
typedef enum { CARET_RIGHT, CARET_BEFORE_ICON, CARET_BEFORE_NAME, CARET_AFTER_NAME } CaretPosition;

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

typedef struct {
    int x, y, width, height;
} ViewRect;

// A loaded row on its way to another level: how many levels from there it still is, and how far its parts have moved.
typedef struct {
    BYTE *row;
    double from, shown;
    int movedLeft, movedRight;
    ULONGLONG start;
} IndentMotion;

// A backdrop's height as it's shown on the way to what it should be. Aside removes a folded folder's rows at once, so
// it can't just follow them. It belongs to the folder's node: most rebuilds make every row again, and often at the same
// addresses in another order, so a row's address says nothing about what it showed. It belongs to its window too, because
// another window can have the same folder open differently.
typedef struct {
    const BYTE *container, *node;
    int from, to, shown;
    ULONGLONG start;
} BlockMotion;

typedef void *(*RowDeleter)(void *row, unsigned flags);
typedef void (*Rebuild)(void *owner, BOOL animate);
typedef void (*RowFunction)(void *row);
typedef void (*Layout)(void *view, void *key);
typedef void (*SetVisible)(void *view, BOOL visible);
typedef int (*CanvasSave)(void *canvas);
typedef void (*CanvasRestore)(void *canvas);
typedef void (*CanvasConcat)(void *canvas, const float *matrix);
typedef void (*SizeChanged)(void *view);
typedef void (*SchedulePaint)(void *view);
typedef void (*PaintBackground)(void *view, void *canvas);
typedef void (*CreateImage)(void *result, const void *icon, ULONGLONG color, int size, void *badge);
typedef void (*DestroyImage)(void *image);
typedef BOOL (*MousePressed)(void *row, const void *event);

// patch.c: the bindings, and the patch's options.
extern Bindings Native;
extern BYTE *Base;
extern void *RowVtable;
extern const IconEntry *Icons;
extern BOOL Enabled, RebuildWanted;
extern BOOL CaretShown, CaretFlipped, BackdropShown, BackdropTinted;
extern BOOL CloseRemoves, CloseConfirms, CloseIsMinus;
extern BOOL LoadedRise, UnloadAnimated, SnapDrag;
extern FolderHoverMode FolderHover;
extern FolderUnloadMode FolderUnload;
extern CaretPosition Position;
extern int BottomOffset, BelowGap, ClosedAngle, OpenAngle, LeftOffset, RightOffset;

// shape.c: where things are in Aside's objects, learned from the first row that paints.
extern DWORD ParentOffset, ChildrenOffset, NodeOffset, GuidOffset, NodeParentOffset;
extern BOOL Learned;
BOOL LearnRowShape(const BYTE *row);
BOOL ReadGuidAt(const BYTE *object, DWORD offset, char *guid);
BOOL IsInside(const BYTE *node, const BYTE *ancestor);
BOOL FolderIsOpen(const BYTE *owner, const BYTE *node);
BOOL NodeIsOpen(const BYTE *owner, const BYTE *node);
const BYTE *FindRowOf(const BYTE *container, const BYTE *node);
const BYTE *FoldedAbove(const BYTE *row, const BYTE *owner);
BYTE *OwnerOf(const BYTE *row);

static inline const int *ViewBounds(const void *view) {
    return (const int *)((const BYTE *)view + Native.boundsOffset);
}

static inline const RECT *BoundsOf(const BYTE *view, RECT *out) {
    const int *bounds = ViewBounds(view);
    *out = (RECT){bounds[0], bounds[1], bounds[0] + bounds[2], bounds[1] + bounds[3]};
    return out;
}

// Follows bounds_ in View, and shows a row as present.
static inline BOOL IsVisible(const BYTE *view) {
    return view[Native.boundsOffset + 16] != 0;
}

static inline BYTE *ParentOf(const BYTE *view) {
    return *(BYTE *const *)(view + ParentOffset);
}

static inline void **ChildrenOf(const BYTE *view, size_t *count) {
    *count = *(const size_t *)(view + ChildrenOffset + 8);
    return *(void **const *)(view + ChildrenOffset);
}

static inline void Repaint(void *view) {
    ((SchedulePaint)(Base + Native.schedulePaint))(view);
}

static inline void NotifySizeChanged(void *view) {
    void **vtable = *(void ***)view;
    ((SizeChanged)vtable[Native.sizeChangedSlot])(view);
}

static inline void SetViewVisible(void *view, BOOL visible) {
    void **vtable = *(void ***)view;
    ((SetVisible)vtable[Native.setVisibleSlot])(view, visible);
}

static inline BOOL IsRow(const BYTE *view) {
    return view && *(void *const *)view == RowVtable;
}

// One of a row's child views, by the offset Aside keeps it at.
static inline BYTE *PartOf(const BYTE *row, DWORD offset) {
    return *(BYTE *const *)(row + offset);
}

static inline BYTE *NodeOf(const BYTE *row) {
    return *(BYTE *const *)(row + NodeOffset);
}

static inline BYTE *LearnedNode(const BYTE *row) {
    return Learned ? NodeOf(row) : NULL;
}

static inline BOOL IsShownRow(const BYTE *view) {
    return IsRow(view) && IsVisible(view) && NodeOf(view);
}

// The nesting Aside gave a row, before anything here changes how it shows.
static inline int StoredDepth(const BYTE *row) {
    return *(const int *)(row + Native.depthOffset);
}

static inline BYTE *NodeParent(const BYTE *node) {
    return *(BYTE *const *)(node + NodeParentOffset);
}

static inline const char *GuidOf(const BYTE *node) {
    return *(const char *const *)(node + GuidOffset);
}

static inline BOOL IsFolder(const BYTE *node) {
    return *(const int *)(node + Native.nodeTypeOffset) != 0;
}

// A view's canvas is a gfx::Canvas, and these go through the cc::PaintCanvas in it.
static inline void CanvasPush(void *canvas) {
    void *paint = *(void **)((BYTE *)canvas + Native.canvasPaintOffset);
    void **vtable = *(void ***)paint;
    ((CanvasSave)vtable[Native.canvasSaveSlot])(paint);
}

static inline void CanvasPop(void *canvas) {
    void *paint = *(void **)((BYTE *)canvas + Native.canvasPaintOffset);
    void **vtable = *(void ***)paint;
    ((CanvasRestore)vtable[Native.canvasRestoreSlot])(paint);
}

static inline void Translate(void *canvas, int x, int y) {
    void *paint = *(void **)((BYTE *)canvas + Native.canvasPaintOffset);
    void **vtable = *(void ***)paint;
    // An SkM44, by columns.
    const float move[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, (float)x, (float)y, 0, 1};
    ((CanvasConcat)vtable[Native.canvasConcatSlot])(paint, move);
}

// How far along a motion that started then is, on about the curve Aside moves its rows along.
static inline double Eased(ULONGLONG start) {
    double done = min((double)(GetTickCount64() - start) / MOTION_MS, 1.0);
    return done * done * (3 - 2 * done);
}

// folders.c: what's been chosen for each folder.
extern int FolderCount;
void LoadFolders(void);
int FindIcon(const char *key, size_t size);
const Folder *FindFolder(const char *guid);
int FindColor(const char *guid);
void SetFolder(const char *guid, int color, int icon);

// style.c: a folder's icon and title color, and its menu.
extern MousePressed OriginalMousePressed;
void StyleRow(BYTE *row);
void RestoreRow(BYTE *row);
BOOL RowMousePressed(void *view, const void *event);

// nesting.c: how far in a row sits.
extern int IndentCount;
int LevelsToFolded(const BYTE *row);
int RowDepth(const BYTE *row);
IndentMotion *FindIndent(const BYTE *row);
int IndentLeft(const BYTE *row);
int IndentRight(const BYTE *row);
void MoveRowParts(BYTE *row, IndentMotion *motion);
void StartIndent(BYTE *row, int inset, int target);
void AdvanceIndents(void);

// row.c: one row's layout and paint.
extern PaintBackground OriginalRowPaint, OriginalCaretPaint;
extern Layout OriginalRowLayout;
void SetBounds(BYTE *view, int x, int width);
void RowLayout(void *view, void *key);
void CaretPaint(void *view, void *canvas);
void RowPaint(void *view, void *canvas);

// close.c: the close button, what it unloads and what it deletes.
extern BYTE *HeldOwner, *DeleteOwner;
void DeleteBookmark(void);
void StyleClose(BYTE *row);
void RestoreCloseIcon(const BYTE *row);
void ReleaseHeldRebuilds(BOOL now);

// backdrop.c: the tint behind a colored folder.
extern void *Rounded;
extern BlockMotion Motions[MAX_MOTIONS];
extern int MotionCount;
extern BOOL BlocksMoving;
void PaintBlock(BYTE *container, void *canvas, const RECT *rect, int left, int right, int color, BOOL expanded);
BOOL HasBackdrop(const BYTE *row);
size_t LastInside(void **children, size_t count, size_t index);
BOOL RowPlace(const BYTE *row, RECT *now, RECT *end);
void PaintFolders(BYTE *container, void *canvas);

// ghosts.c: a folding folder's rows fading out.
extern RowDeleter OriginalRowDelete;
extern int GhostCount;
void PaintGhosts(BYTE *container, void *canvas);
BOOL HoldGhost(BYTE *row, BOOL kept);
void *RowDelete(void *view, unsigned flags);
void AdvanceGhosts(BOOL finish);

// arrange.c: what has to be in place before Aside measures rows.
extern BYTE *HiddenRows[MAX_TRACKED_ROWS];
extern int HiddenCount;
void SetRowPadding(BYTE *row, int padding);
void SetRowHidden(BYTE *row, BOOL hidden);
void PlaceHiddenRows(const BYTE *container);
void ArrangeForLayout(BYTE *container);

// containers.c: the view holding the rows, one per window.
extern Container Containers[MAX_CONTAINERS];
extern int ContainerCount;
extern BOOL RowsNarrowed;
void AddContainer(BYTE *view, const BYTE *owner);
BOOL ContainerIsLive(const BYTE *view);
const Container *ContainerOfOwner(const BYTE *owner);
BOOL RowIsLive(const BYTE *row);
void RebuildAllRows(void);
void RepaintMovedContainers(void);

// drag.c: a row being dragged.
int DragDepthChange(const BYTE *row);
void TakeDragMove(const BYTE *row);
