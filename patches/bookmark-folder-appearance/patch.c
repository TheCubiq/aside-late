// Colors bookmark folders and changes their icons, and arranges rows: their nesting, carets and close buttons.
// This file is the patch's life: its options, turning it on and off, and the timer everything else hangs off.

#include "rows.h"

#define MAX_ROWS 4096
#define CORNER_RADIUS 8.0f
#define TICK_MS 16

typedef void (*CreateRounded)(void **result, ULONGLONG color, float radius, int inset);

Bindings Native;
BYTE *Base;
void *RowVtable;
static PaintBackground OriginalRowBackground;
static UINT_PTR Timer;
const IconEntry *Icons;
BOOL CaretShown = TRUE;
BOOL CaretFlipped;
BOOL BackdropShown = TRUE;
BOOL BackdropTinted = TRUE;
BOOL CloseRemoves;
BOOL CloseConfirms = TRUE;
BOOL CloseIsMinus;
BOOL LoadedRise;
static BOOL ActiveFirst;
FolderHoverMode FolderHover;
FolderUnloadMode FolderUnload;
BOOL UnloadAnimated = TRUE;
int BottomOffset = 4;
int BelowGap = 6;
CaretPosition Position;
int ClosedAngle;
int OpenAngle = 180;
int LeftOffset = 14;
int RightOffset = 6;
BOOL Enabled;
static BOOL OrderEdited;
BOOL RebuildWanted;
BOOL SnapDrag = TRUE;

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

// What can add or remove a view's layer can't happen while it paints: the compositor is going through them all then, and
// finding one gone takes the browser with it. So what a hover shows, and what a drag's level lays out, is seen to from here.
static void TendRows(const BYTE *container) {
    size_t count;
    void **children = ChildrenOf(container, &count);
    for (size_t i = 0; i < count; i++) {
        BYTE *row = children[i];
        if (!IsRow(row) || !NodeOf(row)) continue;
        StyleClose(row);
        TakeDragMove(row);
        BYTE *layout = *(BYTE **)(row + Native.rowLayoutOffset);
        if (!row[Native.dragActiveOffset] || !layout || *(int *)(layout + Native.insetLeftOffset) == Native.indentBase + RowDepth(row) * LeftOffset) continue;
        NotifySizeChanged(row);
    }
}

static void CALLBACK Tick(HWND window, UINT message, UINT_PTR timer, DWORD time) {
    (void)window, (void)message, (void)timer, (void)time;
    MenuTick();
    if (DeleteOwner) DeleteBookmark();
    if (RebuildWanted && Native.rebuild) {
        RebuildWanted = FALSE;
        RebuildAllRows();
    }
    if (HeldOwner) ReleaseHeldRebuilds(FALSE);
    AdvanceIndents();
    AdvanceGhosts(FALSE);
    for (int i = 0; i < ContainerCount; i++) {
        if (!ContainerIsLive(Containers[i].view)) continue;
        PlaceHiddenRows(Containers[i].view);
        if (Learned) TendRows(Containers[i].view);
    }
    RepaintMovedContainers();
}

static void StartOnUiThread(void) {
    if (!Rounded) ((CreateRounded)(Base + Native.roundedBackground))(&Rounded, 1ull << 32, CORNER_RADIUS, 0);
    if (!Rounded) ModLog("couldn't create the block background");
    if (!Timer) Timer = SetTimer(NULL, 0, TICK_MS, Tick);
}

static void RowBackground(void *view, void *canvas) {
    BYTE *row = view;
    OriginalRowBackground(view, canvas);
    if (!Learned && !LearnRowShape(row)) return;
    StartOnUiThread();
    BYTE *parent = ParentOf(row);
    if (parent) AddContainer(parent, OwnerOf(row));
}

// Rows lay themselves out again, which puts back whatever isn't moved any more, and repaint.
static void ArrangeRows(void) {
    for (int i = 0; i < ContainerCount; i++) {
        if (!ContainerIsLive(Containers[i].view)) continue;
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

// Everything the patch changed on a container's rows, back as Aside had it.
static void RestoreRows(const BYTE *container) {
    size_t count;
    void **children = ChildrenOf(container, &count);
    for (size_t i = 0; i < count; i++) {
        BYTE *row = children[i];
        if (!IsRow(row)) continue;
        RestoreRow(row);
        ((RowFunction)(Base + Native.updateIndent))(row);
        SetRowHidden(row, FALSE);
        SetRowPadding(row, 0);
        RestoreCloseIcon(row);
    }
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
    else if (!strcmp(key, "nesting.below")) BelowGap = atoi(value);
    else if (!strcmp(key, "nesting.loaded")) LoadedRise = on;
    else if (!strcmp(key, "nesting.snap")) SnapDrag = on;
    else if (!strcmp(key, "nesting.active-first")) {
        ActiveFirst = on;
        if (Enabled) {
            ApplyActiveOrder();
            RebuildWanted = TRUE;
        }
    }
    else if (!strcmp(key, "bookmark.unload-folder")) FolderUnload = !strcmp(value, "always") ? UNLOAD_ALWAYS : !strcmp(value, "collapsed") ? UNLOAD_COLLAPSED : UNLOAD_NEVER;
    else if (!strcmp(key, "bookmark.unload-animated")) UnloadAnimated = on;
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
    LoadFolders();
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
        NotifySizeChanged(rows[i]);
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
    if (HeldOwner) ReleaseHeldRebuilds(TRUE);
    for (int i = 0; i < ContainerCount; i++)
        if (ContainerIsLive(Containers[i].view)) RestoreRows(Containers[i].view);
    ArrangeRows();
    ContainerCount = 0;
}
