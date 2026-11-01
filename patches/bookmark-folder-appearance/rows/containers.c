// The views holding the rows, one per window, and their hooks.

#include "rows.h"

typedef void *(*MeasureSize)(void *view, void *size, const void *bounds);

static PaintBackground OriginalContainerPaint;
static PaintBackground OriginalContainerOnPaint;
static Layout OriginalContainerLayout;
static MeasureSize OriginalContainerMeasure;
BOOL RowsNarrowed;
Container Containers[MAX_CONTAINERS];
int ContainerCount;

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

static void ContainerLayout(void *view, void *key) {
    ArrangeForLayout(view);
    OriginalContainerLayout(view, key);
}

static void *ContainerMeasure(void *view, void *size, const void *bounds) {
    ArrangeForLayout(view);
    return OriginalContainerMeasure(view, size, bounds);
}

void AddContainer(BYTE *view, const BYTE *owner) {
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

BOOL ContainerIsLive(const BYTE *view) {
    for (int i = 0; i < ContainerCount; i++) {
        void *vtable;
        if (Containers[i].view == view) return SafeRead(view, &vtable, sizeof vtable) && vtable == Containers[i].vtable;
    }
    return FALSE;
}

const Container *ContainerOfOwner(const BYTE *owner) {
    for (int i = 0; i < ContainerCount; i++)
        if (Containers[i].owner == owner && ContainerIsLive(Containers[i].view)) return &Containers[i];
    return NULL;
}

BOOL RowIsLive(const BYTE *row) {
    for (int i = 0; i < ContainerCount; i++) {
        if (!ContainerIsLive(Containers[i].view)) continue;
        size_t count;
        void **children = ChildrenOf(Containers[i].view, &count);
        for (size_t j = 0; j < count; j++)
            if (children[j] == row) return TRUE;
    }
    return FALSE;
}

void RebuildAllRows(void) {
    for (int i = 0; i < ContainerCount; i++)
        if (Containers[i].owner && ContainerIsLive(Containers[i].view)) ((Rebuild)(Base + Native.rebuild))((void *)Containers[i].owner, FALSE);
}

// A hash of where a container's rows are, up and down.
static unsigned RowsHash(const BYTE *container) {
    size_t count;
    void **children = ChildrenOf(container, &count);
    unsigned hash = 17;
    for (size_t i = 0; i < count; i++) {
        if (!IsRow(children[i])) continue;
        const unsigned *bounds = (const unsigned *)((BYTE *)children[i] + Native.boundsOffset);
        hash = hash * 31 + bounds[1];
        hash = hash * 31 + bounds[3];
        hash = hash * 31 + IsVisible(children[i]);
    }
    return hash;
}

// Rows move without their container repainting, and the backdrops have to follow them.
// Containers aren't told they're gone, but a freed one no longer has this vtable.
void RepaintMovedContainers(void) {
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
        unsigned hash = RowsHash(container->view);
        if (hash == container->lastBounds && !moving) continue;
        container->lastBounds = hash;
        Repaint(container->view);
    }
}
