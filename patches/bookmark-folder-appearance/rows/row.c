// A single row: where its parts go when it lays out, and how it paints at its nesting.

#include "rows.h"

#define FOLDER_BUTTON_GAP 4

typedef struct {
    int width, height;
} ViewSize;

typedef void (*SetViewBounds)(void *view, const ViewRect *rect);
typedef void (*CalculateSize)(const void *view, ViewSize *size, const void *bounds);

PaintBackground OriginalRowPaint;
PaintBackground OriginalCaretPaint;
Layout OriginalRowLayout;

void SetBounds(BYTE *view, int x, int width) {
    const int *bounds = ViewBounds(view);
    ViewRect rect = {x, bounds[1], width, bounds[3]};
    if (bounds[0] != x || bounds[2] != width) ((SetViewBounds)(Base + Native.setBoundsRect))(view, &rect);
}

// Aside puts the close button at the row's very end and moves a caret aligned right before it. The caret is what stays,
// so the two swap places. Anything else that's there makes room.
static void PlaceFolderButton(BYTE *row) {
    const BYTE *node = LearnedNode(row);
    BYTE *close = PartOf(row, Native.closeOffset), *caret = PartOf(row, Native.caretOffset), *title = PartOf(row, Native.titleOffset);
    if (!node || !IsFolder(node) || !close || !title || !IsVisible(close)) return;
    const int *button = ViewBounds(close), *name = ViewBounds(title);
    const int *mark = caret ? ViewBounds(caret) : NULL;
    BOOL caretShows = caret && CaretShown && IsVisible(caret);
    if (caretShows && Position == CARET_RIGHT) {
        int caretMiddle = mark[0] + mark[2] / 2, buttonMiddle = button[0] + button[2] / 2;
        int caretWidth = mark[2], buttonWidth = button[2];
        SetBounds(caret, buttonMiddle - caretWidth / 2, caretWidth);
        SetBounds(close, caretMiddle - buttonWidth / 2, buttonWidth);
    }
    int limit = button[0] - FOLDER_BUTTON_GAP;
    if (caretShows && Position == CARET_AFTER_NAME && mark[0] + mark[2] > limit) {
        int over = mark[0] + mark[2] - limit;
        SetBounds(title, name[0], max(name[2] - over, 0));
        SetBounds(caret, mark[0] - over, mark[2]);
    } else if (name[0] + name[2] > limit) {
        SetBounds(title, name[0], max(limit - name[0], 0));
    }
}

// Rearranges what Aside just laid out, keeping the gaps it left.
static void PlaceCaret(BYTE *row) {
    BYTE *leading = PartOf(row, Native.leadingOffset), *caret = PartOf(row, Native.caretOffset), *title = PartOf(row, Native.titleOffset);
    if (!leading || !caret || !title || !IsVisible(caret) || (CaretShown && Position == CARET_RIGHT)) return;
    const int *icon = ViewBounds(leading), *name = ViewBounds(title), *mark = ViewBounds(caret);
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
void RowLayout(void *view, void *key) {
    BYTE *row = view;
    BYTE *layout = *(BYTE **)(row + Native.rowLayoutOffset);
    int depth = RowDepth(row);
    int *bounds = (int *)(row + Native.boundsOffset), width = bounds[2], right = depth * RightOffset;
    if (layout && !row[Native.collapsedOffset]) *(int *)(layout + Native.insetLeftOffset) = Native.indentBase + depth * LeftOffset;
    if (width > right) bounds[2] = width - right;
    OriginalRowLayout(view, key);
    bounds[2] = width;
    PlaceCaret(row);
    PlaceFolderButton(row);
    IndentMotion *motion = FindIndent(row);
    if (motion) {
        motion->movedLeft = motion->movedRight = 0;
        MoveRowParts(row, motion);
    }
}

// The caret turns by a share of a half turn that Aside animates. Giving it another share while it paints is all an
// angle takes: its own animation then runs between any two, in either direction.
void CaretPaint(void *view, void *canvas) {
    BYTE *caret = view;
    double *progress = (double *)(caret + Native.caretProgressOffset), share = *progress;
    if (Learned && IsRow(ParentOf(caret))) {
        int turn = (((OpenAngle - ClosedAngle) % 360) + 360) % 360;
        if (CaretFlipped && turn) turn -= 360;
        *progress = (ClosedAngle + share * turn) / Native.turnDegrees;
    }
    OriginalCaretPaint(view, canvas);
    *progress = share;
}

// A row paints its highlight and outline across its whole width. So for that long it's as wide, and as far along, as
// its nesting.
void RowPaint(void *view, void *canvas) {
    BYTE *row = view;
    if (Learned) StyleRow(row);
    int depth = RowDepth(row), left = depth * LeftOffset, right = depth * RightOffset;
    int *bounds = (int *)(row + Native.boundsOffset), width = bounds[2];
    // A folder's own hover is drawn with the row's, so for this long it isn't hovered.
    const BYTE *node = LearnedNode(row), *owner = node ? OwnerOf(row) : NULL;
    BYTE hovered = row[Native.hoverOffset];
    if (node && IsFolder(node) && (FolderHover == HOVER_NEVER || (FolderHover == HOVER_COLLAPSED && owner && FolderIsOpen(owner, node)))) row[Native.hoverOffset] = 0;
    left = max(left + IndentLeft(row), 0);
    right = max(right + IndentRight(row), 0);
    if ((!left && !right) || width <= left + right) {
        OriginalRowPaint(view, canvas);
        row[Native.hoverOffset] = hovered;
        return;
    }
    CanvasPush(canvas);
    Translate(canvas, left, 0);
    bounds[2] = width - left - right;
    OriginalRowPaint(view, canvas);
    bounds[2] = width;
    row[Native.hoverOffset] = hovered;
    CanvasPop(canvas);
}
