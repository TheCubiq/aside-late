// Squares the page's corners beside the sidebar, and the top bar's upper corners. Aside rounds a page all the way around,
// even where it meets the sidebar. BrowserView::Layout gives each page container its corners and the top bar its own, so
// straight after it this takes the sidebar's side away from the page's, and the bar's are edited in the layout's code.
//
// Only a page touching one side of the window has a sidebar side. An inset from Window tweaks moves it off both, and then
// this leaves everything as it is.

#include "shared/browser.h"
#include "shared/corners.h"

#define MAX_BROWSERS 32

typedef struct {
    int x, y, width, height;
} ViewRect;

typedef struct {
    float upperLeft, upperRight, lowerRight, lowerLeft;
} Radii;

typedef void (*Layout)(void *view, void *key);
typedef void (*ApplyRadii)(void *view, const Radii *radii);
typedef void (*ClearRadii)(void *view);

static BrowserBindings Browser;
static CornerBindings Corners;
static DWORD TopVtable, ChildrenOffset;
static BYTE *Base;
static Layout OriginalLayout;
static BOOL Enabled, TopEdited, LayingOutAgain;

static ViewRect BoundsOf(const BYTE *view) {
    return *(const ViewRect *)(view + Browser.boundsOffset);
}

// The top bar's corners are in the layout's own code, so its two stores of the upper radius store the zero beside them instead.
static BOOL EditTopCorners(BOOL square) {
    static const BYTE zero[] = {0xf3, 0x41, 0x0f, 0x11, 0x00, 0xf3, 0x41, 0x0f, 0x11, 0x40, 0x04};
    static const BYTE radius[] = {0xf3, 0x45, 0x0f, 0x11, 0x38, 0xf3, 0x45, 0x0f, 0x11, 0x78, 0x04};
    if (!Corners.topRadiusSite || TopEdited == square) return FALSE;
    if (!EditCode(Base + Corners.topRadiusSite, square ? radius : zero, square ? zero : radius, sizeof zero)) {
        ModLog("square-corners couldn't edit the top bar's corners");
        return FALSE;
    }
    TopEdited = square;
    return TRUE;
}

// Whether a list at this offset, of pointer, count, capacity, holds the top bar. Nothing here is assumed to be valid.
static BOOL ListsTopBar(const BYTE *browser, DWORD offset) {
    struct {
        void **data;
        size_t count, capacity;
    } list;
    if (!SafeRead(browser + offset, &list, sizeof list) || !list.data || !list.count || list.count > list.capacity || list.count > 256) return FALSE;
    for (size_t i = 0; i < list.count; i++) {
        void *child, *vtable;
        if (!SafeRead(list.data + i, &child, sizeof child)) return FALSE;
        if (child && SafeRead(child, &vtable, sizeof vtable) && vtable == Base + TopVtable) return TRUE;
    }
    return FALSE;
}

static BOOL IsRealBrowser(const BYTE *browser) {
    void *vtable;
    if (!SafeRead(browser, &vtable, sizeof vtable) || vtable != Base + Browser.vtable) return FALSE;
    for (DWORD offset = ChildrenOffset ? ChildrenOffset : 0x100; offset < (ChildrenOffset ? ChildrenOffset + 1 : 0x300); offset += 8)
        if (ListsTopBar(browser, offset)) {
            ChildrenOffset = offset;
            return TRUE;
        }
    return FALSE;
}

static void LayOut(BYTE *browser) {
    browser[Browser.needsLayoutOffset] = 1;
    ((Layout)(*(void ***)browser)[Browser.layoutSlot])(browser, NULL);
}

static void SquarePage(BYTE *browser) {
    BYTE *contents = *(BYTE **)(browser + Corners.contentsOffset);
    if (!contents) return;
    ViewRect full = BoundsOf(browser), page = BoundsOf(contents);
    BOOL gapLeft = page.x > 0, gapRight = page.x + page.width < full.width;
    BOOL square = Enabled && gapLeft != gapRight;
    // The bar's corners are only read as the layout runs, so a change needs it once more.
    if (EditTopCorners(square) && !LayingOutAgain) {
        LayingOutAgain = TRUE;
        LayOut(browser);
        LayingOutAgain = FALSE;
        return;
    }
    if (!square) return;
    BYTE *const *containers = *(BYTE *const *const *)(contents + Corners.containersOffset);
    size_t count = *(const size_t *)(contents + Corners.containersOffset + 8);
    for (size_t i = 0; i < count; i++) {
        BYTE *container = containers[i];
        Radii *radii = (Radii *)(container + Corners.radiiOffset);
        if (gapLeft) radii->upperLeft = radii->lowerLeft = 0;
        else radii->upperRight = radii->lowerRight = 0;
        if (container[Corners.radiiSetOffset]) ((ApplyRadii)(Base + Corners.applyRadii))(container, radii);
        else ((ClearRadii)(Base + Corners.clearRadii))(container);
    }
}

static void BrowserLayout(void *view, void *key) {
    OriginalLayout(view, key);
    SquarePage(view);
}

static void LayOutAll(void) {
    void *found[MAX_BROWSERS];
    int count = FindObjects(Base + Browser.vtable, found, MAX_BROWSERS);
    for (int i = 0; i < count; i++)
        if (IsRealBrowser(found[i])) LayOut(found[i]);
}

PATCH_START() {
    Base = base;
    if (!(TopVtable = LookupClassVtable(lookup, "TopContainerView"))) return "TopContainerView";
    if (!FindBrowser(lookup, &Browser) || !Browser.needsLayoutOffset) return "the browser's layout";
    if (!FindCorners(lookup, &Browser, &Corners)) return "how the page's corners are set";
    if (!Corners.topRadiusSite) ModLog("square-corners can't square the top bar in this build");
    return NULL;
}

PATCH_ENABLE() {
    if (!HookVtable((void **)(Base + Browser.vtable), Browser.layoutSlot, BrowserLayout, (void **)&OriginalLayout)) {
        ModLog("square-corners couldn't hook the browser's layout");
        return;
    }
    Enabled = TRUE;
    LayOutAll();
}

// The hook is already off, so laying out puts Aside's corners back.
PATCH_DISABLE() {
    Enabled = FALSE;
    EditTopCorners(FALSE);
    LayOutAll();
}
