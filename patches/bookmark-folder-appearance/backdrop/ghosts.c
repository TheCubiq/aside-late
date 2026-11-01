// A folding folder's rows, painted a little longer while they fade.

#include "rows.h"

#define MAX_GHOSTS 64
#define MAX_GHOST_DEPTH 16
#define CLIP_INTERSECT 1

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

typedef void (*CanvasClipRect)(void *canvas, const ViewRect *rect, int operation);
typedef void (*CanvasSaveLayerAlpha)(void *canvas, BYTE alpha, const ViewRect *rect);
typedef void (*ViewPaint)(void *view, void *canvas);

RowDeleter OriginalRowDelete;
static Ghost Ghosts[MAX_GHOSTS];
int GhostCount;

// A row that's on its way out is its icon, title and caret, painted where they are in it. Not the row itself or its other
// parts: each normally has a layer of its own, and some clear what they paint on, which here is the whole container.
// A view that's been taken out of the tree has also forgotten its widget, and Aside stops the browser if an icon that needs
// colors has none to ask. So for this long the row and its parts belong to the container's.
static void PaintGhostRow(const Ghost *ghost, void *canvas, int shift) {
    BYTE *row = ghost->row, *parent = ParentOf(row);
    void *widget = *(void **)(ghost->container + Native.viewWidgetOffset);
    if (!widget) return;
    BYTE *parts[] = {PartOf(row, Native.leadingOffset), PartOf(row, Native.titleOffset), PartOf(row, Native.caretOffset)};
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
        const int *bounds = ViewBounds(part);
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
void PaintGhosts(BYTE *container, void *canvas) {
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

// Whether a row that's going away has a folded folder to fade into, and if so it does.
BOOL HoldGhost(BYTE *row, BOOL kept) {
    if (!Enabled || !Learned || !Native.canvasClipRect || !Native.canvasSaveLayerAlpha || !Native.viewWidgetOffset || GhostCount == MAX_GHOSTS) return FALSE;
    const int *bounds = ViewBounds(row);
    int depth = StoredDepth(row);
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

// Aside deletes the rows of a folder that just folded at the end of its rebuild. Called from anywhere else, it's not
// that: a window closing leaves nothing to paint them into.
void *RowDelete(void *view, unsigned flags) {
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
    if (rebuilding && HoldGhost(view, FALSE)) return view;
    return OriginalRowDelete(view, flags);
}

// A ghost is done when its time is up, or as soon as its folder opens again. With its window gone, what it would
// tell on its way out is too, so it's left alone.
void AdvanceGhosts(BOOL finish) {
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
