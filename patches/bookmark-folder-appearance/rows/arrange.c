// What has to be in place before Aside measures rows: bottom margins, and folders hidden under a folded one.

#include "rows.h"

// Extra margin a row has been given below it, and what it had before.
typedef struct {
    BYTE *row;
    const int *owned;
    int base[4];
    int added;
} RowPadding;

typedef void *(*SetProperty)(void *properties, const void *key, const char *name, void *deleter, void *value, void *unused);
typedef void (*Deleter)(void *value);
typedef void *(*OperatorNew)(size_t size);

BYTE *HiddenRows[MAX_TRACKED_ROWS];
int HiddenCount;
static RowPadding Paddings[MAX_TRACKED_ROWS];
static int PaddingCount;
static BOOL Projecting;

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

void SetRowPadding(BYTE *row, int padding) {
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
    NotifySizeChanged(row);
}

void SetRowHidden(BYTE *row, BOOL hidden) {
    int found = -1;
    for (int i = 0; i < HiddenCount && found < 0; i++)
        if (HiddenRows[i] == row) found = i;
    if (hidden) {
        if (found < 0 && HiddenCount < MAX_TRACKED_ROWS) HiddenRows[HiddenCount++] = row;
        if (IsVisible(row)) {
            HoldGhost(row, TRUE);
            SetViewVisible(row, FALSE);
            // When it's back, its backdrop grows with it.
            for (int i = 0; i < MotionCount; i++)
                if (Motions[i].node == NodeOf(row)) Motions[i] = Motions[--MotionCount];
        }
    } else if (found >= 0) {
        HiddenRows[found] = HiddenRows[--HiddenCount];
        SetViewVisible(row, TRUE);
    }
}

// Aside remembers where every row was, to animate it from there, and a hidden row has been nowhere since it was last
// laid out, or ever. So it's kept with no height just above the first row inside it, which it'll be above again.
void PlaceHiddenRows(const BYTE *container) {
    if (!HiddenCount) return;
    size_t count;
    void **children = ChildrenOf(container, &count);
    for (size_t i = 0; i < count; i++) {
        BYTE *row = children[i];
        if (!IsRow(row) || IsVisible(row) || !NodeOf(row)) continue;
        BOOL hidden = FALSE;
        for (int j = 0; j < HiddenCount && !hidden; j++) hidden = HiddenRows[j] == row;
        if (!hidden) continue;
        const BYTE *inside = NULL;
        for (size_t j = i + 1; j < count && !inside; j++)
            if (IsShownRow(children[j]) && IsInside(NodeOf(children[j]), NodeOf(row))) inside = children[j];
        if (!inside) continue;
        RECT now, end;
        BOOL moving = BlocksMoving;
        RowPlace(inside, &now, &end);
        BlocksMoving = moving;
        int *bounds = (int *)(row + Native.boundsOffset);
        bounds[0] = end.left;
        bounds[1] = end.top;
        bounds[2] = end.right - end.left;
        bounds[3] = 0;
    }
}

// Everything that changes how tall rows are has to be in place before Aside measures them, so its own animation heads
// for the right place.
void ArrangeForLayout(BYTE *container) {
    if (Projecting || !Learned || !Enabled) return;
    Projecting = TRUE;
    size_t count;
    void **children = ChildrenOf(container, &count);
    for (size_t i = 0; i < count; i++) {
        BYTE *row = children[i];
        if (!IsRow(row) || !NodeOf(row)) continue;
        // A folder under a folded folder is only there because of a loaded row, which now sits under the folded one.
        SetRowHidden(row, IsFolder(NodeOf(row)) && LevelsToFolded(row) && !row[Native.dragActiveOffset]);
        BYTE *layout = *(BYTE **)(row + Native.rowLayoutOffset);
        int inset = Native.indentBase + RowDepth(row) * LeftOffset;
        // Only a showing row lays itself out and takes this up.
        if (layout && IsVisible(row) && !row[Native.collapsedOffset] && *(int *)(layout + Native.insetLeftOffset) != inset) {
            StartIndent(row, *(int *)(layout + Native.insetLeftOffset), inset);
            *(int *)(layout + Native.insetLeftOffset) = inset;
            NotifySizeChanged(row);
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
                if (IsShownRow(children[k]) && HasBackdrop(children[k]) && LastInside(children, count, k) == i) padding += BottomOffset;
        // Past that, outside any backdrop, is what separates the outermost one from whatever comes next.
        SetRowPadding(row, padding ? padding + BelowGap : 0);
    }
    PlaceHiddenRows(container);
    Projecting = FALSE;
}
