// How far in a row sits, and a loaded row moving gradually to another level.

#include <math.h>
#include "rows.h"

#define MAX_INDENTS 64

static IndentMotion Indents[MAX_INDENTS];
int IndentCount;

// How many levels above a row its outermost folded folder is, or 0. Aside only keeps a row under a folded folder if
// it's loaded, or on the way to one that is.
int LevelsToFolded(const BYTE *row) {
    const BYTE *node = LearnedNode(row), *owner = node ? OwnerOf(row) : NULL;
    if (!LoadedRise || !owner) return 0;
    int levels = 0, depth = StoredDepth(row);
    const BYTE *up = NodeParent(node);
    for (int level = 1; level <= depth && up; level++, up = NodeParent(up))
        if (!FolderIsOpen(owner, up)) levels = level;
    return levels;
}

// How far in a row's nesting puts it. A collapsed sidebar shows no nesting, a loaded row under a folded folder sits
// directly under that folder, and a dragged row is on its way to somewhere else.
int RowDepth(const BYTE *row) {
    if (row[Native.collapsedOffset]) return 0;
    if (Learned && row[Native.dragActiveOffset]) return max(StoredDepth(row) + DragDepthChange(row), 0);
    int levels = LevelsToFolded(row);
    return StoredDepth(row) - (levels > 1 ? levels - 1 : 0);
}

IndentMotion *FindIndent(const BYTE *row) {
    for (int i = 0; i < IndentCount; i++)
        if (Indents[i].row == row) return &Indents[i];
    return NULL;
}

// How far along a row is from where its nesting puts it, while it moves to another level.
int IndentLeft(const BYTE *row) {
    const IndentMotion *motion = FindIndent(row);
    return motion ? lround(motion->shown * LeftOffset) : 0;
}

int IndentRight(const BYTE *row) {
    const IndentMotion *motion = FindIndent(row);
    return motion ? lround(motion->shown * RightOffset) : 0;
}

// The row has already laid itself out for where it's going, so its parts move by what's left. What's aligned to the
// right moves with the right offset.
void MoveRowParts(BYTE *row, IndentMotion *motion) {
    int left = IndentLeft(row), right = IndentRight(row);
    const BYTE *caret = PartOf(row, Native.caretOffset), *close = PartOf(row, Native.closeOffset);
    size_t count;
    void **children = ChildrenOf(row, &count);
    for (size_t i = 0; i < count; i++) {
        BYTE *child = children[i];
        BOOL aligned = child == close || (child == caret && CaretShown && Position == CARET_RIGHT);
        int move = aligned ? motion->movedRight - right : left - motion->movedLeft;
        const int *bounds = ViewBounds(child);
        if (move) SetBounds(child, bounds[0] + move, bounds[2]);
    }
    motion->movedLeft = left;
    motion->movedRight = right;
}

void StartIndent(BYTE *row, int inset, int target) {
    const int *bounds = ViewBounds(row);
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

void AdvanceIndents(void) {
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
