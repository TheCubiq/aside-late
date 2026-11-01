// A row being dragged: the level it's heading for, and locking it across while sideways movement sets that level.

#include <limits.h>
#include <math.h>
#include "rows.h"

// Aside's nesting from a point: (x - 16) / 14. What's halfway through a level, so that rounding doesn't matter.
#define DRAG_NEST_START 16
#define DRAG_NEST_STEP 14
#define DRAG_START_BEFORE_FLAG 8
#define ROW_CALLBACKS_START 0x3e0
#define ROW_CALLBACKS_END 0x440

typedef BOOL (*DragMove)(void *owner, void *row, BYTE *event);
typedef void (*DragConvert)(int *point, void *row, void *container, const void *event);
typedef void (*DragPlace)(void *row, BOOL active, int x, int y, BOOL dragged);

static int SnapStartDepth;
static int SnapChange;
static DWORD DragCallbackOffset;

// A row that's showing and isn't part of the drag, so one that a dragged row can land among.
static BOOL IsResting(const BYTE *view) {
    return IsRow(view) && IsVisible(view) && !view[Native.dragActiveOffset];
}

// How much a drag changes the nesting of the rows in it: the first one lands at the level of the row after the spot
// it's over, and there's only the top level after the last one. A row that snaps shows where Aside would drop it.
int DragDepthChange(const BYTE *row) {
    const BYTE *parent = ParentOf(row), *first = NULL;
    if (!parent) return 0;
    size_t count;
    void **children = ChildrenOf(parent, &count);
    for (size_t i = 0; i < count && !first; i++)
        if (IsRow(children[i]) && ((const BYTE *)children[i])[Native.dragActiveOffset]) first = children[i];
    if (!first) return 0;
    const BYTE *owner = OwnerOf(first);
    if (SnapDrag && Native.dragMove && owner) {
        if (owner[Native.dropTargetOffset + 0x10]) {
            const BYTE *target = *(const BYTE *const *)(owner + Native.dropTargetOffset);
            const BYTE *targetRow = target ? FindRowOf(parent, target) : NULL;
            int depth = targetRow ? StoredDepth(targetRow) + 1 : 0;
            SnapChange = depth - StoredDepth(first);
        }
        return SnapChange;
    }
    int y = ViewBounds(first)[1] + *(const int *)(first + Native.dragOffsetY), best = INT_MAX, landing = 0;
    for (size_t i = 0; i < count; i++) {
        const BYTE *other = children[i];
        if (!IsResting(other)) continue;
        int top = ViewBounds(other)[1];
        if (top <= y || top >= best) continue;
        best = top;
        landing = RowDepth(other);
    }
    return landing - StoredDepth(first);
}

// The row that isn't being dragged and comes first after a height, if there is one.
static const BYTE *SnapNextRow(const BYTE *container, int top) {
    size_t count;
    void **children = ChildrenOf(container, &count);
    const BYTE *next = NULL;
    for (size_t i = 0; i < count; i++) {
        const BYTE *other = children[i];
        if (!IsResting(other)) continue;
        int otherTop = ViewBounds(other)[1];
        if (otherTop > top && (!next || otherTop < ViewBounds(next)[1])) next = other;
    }
    return next;
}

// Where to tell Aside the cursor is, up and down, for the nesting a snapping row is after. It only offers a folder while the
// cursor is on that folder's lower half, and a row is usually a row or more below it. And a row coming out of the middle of a
// folder lands after the folder, so it's told the folder's end.
static int SnapY(const BYTE *container, int y, int wanted) {
    size_t count;
    void **children = ChildrenOf(container, &count);
    const BYTE *above = NULL;
    int aboveMiddle = INT_MIN;
    for (size_t i = 0; i < count; i++) {
        const BYTE *other = children[i];
        if (!IsResting(other)) continue;
        const int *bounds = ViewBounds(other);
        int middle = bounds[1] + bounds[3] / 2;
        if (middle <= y && middle > aboveMiddle) {
            above = other;
            aboveMiddle = middle;
        }
    }
    const BYTE *node = above ? NodeOf(above) : NULL;
    if (!node) return y;
    const BYTE *spot = above;
    if (wanted > StoredDepth(above)) {
        if (!IsFolder(node)) return y;
    } else {
        for (const BYTE *next; (next = SnapNextRow(container, ViewBounds(spot)[1])) && StoredDepth(next) > wanted;) spot = next;
        if (spot == above) return y;
    }
    const int *bounds = ViewBounds(spot);
    return bounds[1] + bounds[3] * 3 / 4;
}

// Moving a dragged row. Aside takes the nesting it's after from where the cursor is across the sidebar, and moves the row
// with it. Here the row stays where it is, and how far the cursor has come since the press is the nesting: Aside is told
// the point that means that, and then the row is put back where the cursor really has it.
static BOOL DragMoved(void *ownerView, void *rowView, BYTE *event) {
    DragMove original = (DragMove)(Base + Native.dragMove);
    BYTE *owner = ownerView, *row = rowView;
    if (!Enabled || !SnapDrag) return original(owner, row, event);
    float *root = (float *)(event + Native.eventRootOffset);
    if (!owner[Native.draggingOffset]) {
        BOOL result = original(owner, row, event);
        if (!owner[Native.draggingOffset]) return result;
        SnapStartDepth = StoredDepth(row);
        SnapChange = 0;
    }
    BYTE *container = ParentOf(row);
    int point[2] = {0};
    if (!container) return original(owner, row, event);
    ((DragConvert)(Base + Native.dragConvert))(point, row, container, event);
    int startX = *(const int *)(owner + Native.draggingOffset - DRAG_START_BEFORE_FLAG);
    int wanted = max(SnapStartDepth + (int)lroundf((float)(point[0] - startX) / max(LeftOffset, DRAG_NEST_STEP)), 0);
    int y = SnapY(container, point[1], wanted);
    float real[2] = {root[0], root[1]};
    root[0] = real[0] + (DRAG_NEST_START + DRAG_NEST_STEP * wanted + DRAG_NEST_STEP / 2 - point[0]);
    root[1] = real[1] + (y - point[1]);
    BOOL result = original(owner, row, event);
    root[0] = real[0];
    root[1] = real[1];
    size_t count;
    void **children = ChildrenOf(container, &count);
    for (size_t i = 0; i < count; i++) {
        BYTE *dragged = children[i];
        if (IsRow(dragged) && dragged[Native.dragActiveOffset]) ((DragPlace)(Base + Native.dragPlace))(dragged, TRUE, 0, *(const int *)(dragged + Native.dragOffsetY) + point[1] - y, dragged[Native.draggedOffset]);
    }
    return result;
}

// A row's callbacks are objects holding the method they run, so this swaps it the way a close button's is.
void TakeDragMove(const BYTE *row) {
    if (!Native.dragMove) return;
    void *method = Base + Native.dragMove;
    if (!DragCallbackOffset) {
        for (DWORD offset = ROW_CALLBACKS_START; offset < ROW_CALLBACKS_END && !DragCallbackOffset; offset += 8) {
            const BYTE *state;
            void *held;
            if (SafeRead(row + offset, &state, sizeof state) && state && SafeRead(state + PRESS_METHOD, &held, sizeof held) && (held == method || held == (void *)DragMoved)) DragCallbackOffset = offset;
        }
        if (!DragCallbackOffset) return;
    }
    BYTE *state = *(BYTE *const *)(row + DragCallbackOffset);
    if (state && *(void **)(state + PRESS_METHOD) == method) *(void **)(state + PRESS_METHOD) = (void *)DragMoved;
}
