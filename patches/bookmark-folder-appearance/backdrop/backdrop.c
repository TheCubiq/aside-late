// The rounded tint behind a colored folder and what's open inside it.

#include "rows.h"

#define ANIMATOR_ROWS 0xb8
#define ANIMATION_VALUE_SLOT 4
#define PLAIN_ALPHA 0x1a000000u
#define COLLAPSED_ALPHA 0x1a000000u
#define EXPANDED_ALPHA 0x33000000u

typedef double (*AnimationValue)(void *animation);
typedef void (*Paint)(void *self, void *canvas, void *view);
typedef void *(*GetWidget)(void *view);
typedef void *(*GetColorProvider)(void *theme);
typedef DWORD (*GetColor)(void *provider, DWORD id);

void *Rounded;
BlockMotion Motions[MAX_MOTIONS];
int MotionCount;
BOOL BlocksMoving;

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

void PaintBlock(BYTE *container, void *canvas, const RECT *rect, int left, int right, int color, BOOL expanded) {
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
BOOL HasBackdrop(const BYTE *row) {
    const BYTE *node = NodeOf(row);
    if (!BackdropShown || !node) return FALSE;
    int color = FindColor(GuidOf(node));
    return color >= 0 && color != NEUTRAL_COLOR;
}

// The last showing row inside a folder, or the folder itself.
size_t LastInside(void **children, size_t count, size_t index) {
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
        if (IsShownRow(children[k]) && HasBackdrop(children[k]) && LastInside(children, count, k) == last) footer += BottomOffset;
    return footer;
}

// From wherever it's showing now, so turning back halfway stays smooth, on about the curve Aside moves its rows along.
static int ShownHeight(const BYTE *container, const BYTE *node, int target, int firstHeight, BOOL jump) {
    BlockMotion *motion = NULL;
    for (int i = 0; i < MotionCount && !motion; i++)
        if (Motions[i].node == node && Motions[i].container == container) motion = &Motions[i];
    ULONGLONG now = GetTickCount64();
    if (!motion) {
        if (MotionCount == MAX_MOTIONS) MotionCount = 0;
        motion = &Motions[MotionCount++];
        *motion = (BlockMotion){container, node, firstHeight, target, firstHeight, now};
    }
    if (jump) *motion = (BlockMotion){container, node, target, target, target, now};
    if (motion->to != target) {
        motion->from = motion->shown;
        motion->to = target;
        motion->start = now;
    }
    if (now - motion->start >= MOTION_MS || motion->from == motion->to) {
        motion->shown = motion->to;
        return motion->shown;
    }
    motion->shown = motion->from + (int)((motion->to - motion->from) * Eased(motion->start));
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
BOOL RowPlace(const BYTE *row, RECT *now, RECT *end) {
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

void PaintFolders(BYTE *container, void *canvas) {
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
        int height = ShownHeight(container, NodeOf(row), target, moving ? rowHeight : target, dragged);
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
