// Changes the browser window's layout: the gap around it in fullscreen, the top bar, the inset around the page and its
// corners. Aside lays everything out in BrowserView::Layout, so this moves what that left, straight after it.

#include "resolve.h"

#include <math.h>

#define MAX_WINDOWS 32
#define WIDGET_CLASS "Chrome_WidgetWin_"
#define TICK_MS 16
#define FRAME_EVERY_TICKS 15
#define DWMWA_BORDER 34
#define DWMWA_CORNERS 33
#define BORDER_NONE 0xfffffffe
#define BORDER_DEFAULT 0xffffffff
#define CORNERS_SQUARE 1
#define REVEAL_PIXELS 4
#define HIDE_DELAY_MS 300
#define MOTION_MS 180
#define CLIP_INTERSECT 1
#define MAX_FADED_CLASSES 128
#define MAX_LOADING_BAR 4

typedef struct {
    int x, y, width, height;
} ViewRect;

typedef struct {
    float upperLeft, upperRight, lowerRight, lowerLeft;
} Radii;

typedef void (*Layout)(void *view, void *key);
typedef void (*SetBounds)(void *view, const ViewRect *rect);
typedef void (*SchedulePaint)(void *view);
typedef void (*Paint)(void *view, void *canvas);
typedef int (*CanvasSave)(void *paint);
typedef void (*CanvasRestore)(void *paint);
typedef void (*CanvasSaveAlpha)(void *paint, float alpha);
typedef void (*CanvasConcat)(void *paint, const float *matrix);
typedef void (*ClipRect)(void *canvas, const ViewRect *rect, int operation);
typedef void (*ApplyRadii)(void *view, const Radii *radii);
typedef void (*ClearRadii)(void *view);
typedef HRESULT (WINAPI *DwmGetAttribute)(HWND window, DWORD attribute, void *value, DWORD size);
typedef HRESULT (WINAPI *DwmSetAttribute)(HWND window, DWORD attribute, const void *value, DWORD size);

// A window whose frame this has changed, and its corners before that.
typedef struct {
    HWND window;
    DWORD corners;
} Framed;

typedef struct {
    BYTE *view;
    void *vtable;
    BYTE *top, *contents, *captions, *shadow, *frame, *toolbar, *address;
    ViewRect topRect, contentsRect, captionsRect, shadowRect;  // Where Aside's layout put them.
    int sidebarHeader;       // How tall the sidebar's header is with the bar hidden. 0 until that's been seen.
    BOOL revealed;
    ULONGLONG leftAt;
    double from, shown;      // How much of the top bar shows, and where that started moving from.
    int clipped;             // How much of the page's top is cut off, when it doesn't move for the bar.
    double alpha;            // How much of the bar shows through its fade.
    ULONGLONG movedAt;       // 0 when it isn't moving.
} Window;

static Bindings Native;
static BYTE *Base;
static Layout OriginalLayout, OriginalPageLayout, OriginalContentsLayout, OriginalTopLayout, OriginalHolderLayout, OriginalSidebarLayout;
static BYTE *HeldContents, *HeldTop;
static BOOL ContentsWaiting, TopWaiting;
static Paint OriginalFramePaint, OriginalTopBackground;
static BOOL PaintingBar;
static Window Windows[MAX_WINDOWS];
static int WindowCount;
static Framed FramedWindows[MAX_WINDOWS];
static int FramedCount;
static DwmGetAttribute GetFrame;
static DwmSetAttribute SetFrame;
static UINT_PTR Timer;
static DWORD ChildrenOffset;
static BOOL Enabled, TopRadiusEdited;

typedef enum { REVEAL_OVER, REVEAL_PUSH, REVEAL_UNCOVER } Reveal;

// A class of view in the bar, with what its paint did before.
typedef struct {
    void *vtable;
    Paint original;
} FadedClass;

static FadedClass FadedClasses[MAX_FADED_CLASSES], TintedClasses[MAX_FADED_CLASSES];
static int FadedClassCount, TintedClassCount;

static BOOL CloseGap = TRUE, CollapseTop, SquareCorners, BarTakesInset = TRUE, SidebarSideTakesInset = TRUE;
static Reveal RevealStyle;
static int Inset;

static BOOL Uncovers(void) {
    return CollapseTop && RevealStyle == REVEAL_UNCOVER;
}

// The inset's own corners are round, so there's nothing to square.
static BOOL SquaresCorners(void) {
    return SquareCorners && !Inset;
}

static void **ChildrenOf(const BYTE *view, size_t *count) {
    *count = *(const size_t *)(view + ChildrenOffset + 8);
    return *(void **const *)(view + ChildrenOffset);
}

static ViewRect BoundsOf(const BYTE *view) {
    return *(const ViewRect *)(view + Native.boundsOffset);
}

static void MoveView(BYTE *view, ViewRect rect) {
    ((SetBounds)(Base + Native.setBounds))(view, &rect);
}

static BOOL IsA(const BYTE *view, DWORD vtable) {
    return view && *(void *const *)view == Base + vtable;
}

static BYTE *ChildOfClass(const BYTE *view, DWORD vtable) {
    size_t count;
    void **children = ChildrenOf(view, &count);
    for (size_t i = 0; i < count; i++)
        if (IsA(children[i], vtable)) return children[i];
    return NULL;
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
        if (child && SafeRead(child, &vtable, sizeof vtable) && vtable == Base + Native.topVtable) return TRUE;
    }
    return FALSE;
}

static BOOL LearnChildren(const BYTE *browser) {
    for (DWORD offset = 0x100; offset < 0x300; offset += 8) {
        if (!ListsTopBar(browser, offset)) continue;
        ChildrenOffset = offset;
        ModLog("window-tweaks learned: children +%#lx", offset);
        return TRUE;
    }
    return FALSE;
}

// A page that's inset or has no bar over it is a card, round all the way around. Beside the sidebar it can be square, and
// below a bar that's always there it's square at the top. Each container has what Aside just gave it, to take the radius from.
static void ApplyPageCorners(const Window *window) {
    if (!Inset && !CollapseTop && !SquaresCorners()) return;
    ViewRect full = BoundsOf(window->view), page = window->contentsRect;
    BOOL onLeft = page.x > 0, onRight = page.x + page.width < full.width;
    BYTE *const *containers = *(BYTE *const *const *)(window->contents + Native.containersOffset);
    size_t count = *(const size_t *)(window->contents + Native.containersOffset + 8);
    for (size_t i = 0; i < count; i++) {
        BYTE *container = containers[i];
        Radii *radii = (Radii *)(container + Native.radiiOffset);
        if (!Inset && !CollapseTop) {
            if (onLeft != onRight) {
                if (onLeft) radii->upperLeft = radii->lowerLeft = 0;
                else radii->upperRight = radii->lowerRight = 0;
            }
        } else {
            float radius = max(max(radii->upperLeft, radii->upperRight), max(radii->lowerRight, radii->lowerLeft));
            float upper = CollapseTop ? radius : 0;
            *radii = (Radii){upper, upper, radius, radius};
            if (SquaresCorners() && onLeft != onRight) {
                if (onLeft) radii->upperLeft = radii->lowerLeft = 0;
                else radii->upperRight = radii->lowerRight = 0;
            }
        }
        if (container[Native.radiiSetOffset]) ((ApplyRadii)(Base + Native.applyRadii))(container, radii);
        else ((ClearRadii)(Base + Native.clearRadii))(container);
    }
}

// The top bar's corners are in the layout's own code, so its two stores of the upper radius store the zero beside them instead.
static void ApplyTopCorners(void) {
    static const BYTE zero[] = {0xf3, 0x41, 0x0f, 0x11, 0x00, 0xf3, 0x41, 0x0f, 0x11, 0x40, 0x04};
    static const BYTE radius[] = {0xf3, 0x45, 0x0f, 0x11, 0x38, 0xf3, 0x45, 0x0f, 0x11, 0x78, 0x04};
    BOOL square = SquaresCorners();
    if (!Native.topRadiusSite || TopRadiusEdited == square) return;
    BYTE *site = Base + Native.topRadiusSite;
    if (EditCode(site, square ? radius : zero, square ? zero : radius, sizeof zero)) TopRadiusEdited = square;
    else ModLog("window-tweaks couldn't square the top bar's corners");
}

// The window's buttons sit beside the browser's view in its frame, and they're what else is there.
static BYTE *FindCaptions(const BYTE *browser) {
    const BYTE *frame = *(BYTE *const *)(browser + ChildrenOffset - sizeof(void *));
    size_t count;
    void **children = frame ? ChildrenOf(frame, &count) : NULL;
    for (size_t i = 0; children && i < count; i++)
        if (children[i] != browser) return children[i];
    return NULL;
}

static void ForEachContainer(const Window *window, void (*action)(BYTE *container, int clipped)) {
    BYTE *const *containers = *(BYTE *const *const *)(window->contents + Native.containersOffset);
    size_t count = *(const size_t *)(window->contents + Native.containersOffset + 8);
    for (size_t i = 0; i < count; i++) action(containers[i], window->clipped);
}

// What holds the page only shows what's inside its parent, and the page itself keeps its size and is cut off to fit. So a
// holder that starts above its parent, by as much as it's taller, has the page's top clipped off without the page moving.
static void ClipPage(const BYTE *page, int clipped) {
    size_t count;
    void **children = ChildrenOf(page, &count);
    ViewRect size = BoundsOf(page);
    if (count && size.height) MoveView(children[0], (ViewRect){0, -clipped, size.width, size.height + clipped});
}

static void ClipContainer(BYTE *container, int clipped) {
    BYTE *page = ChildrenOffset ? ChildOfClass(container, Native.pageVtable) : NULL;
    if (page) ClipPage(page, clipped);
}

// The pane's shadow is a view just bigger than the pane on every side, by the same amount.
static BYTE *FindShadow(const BYTE *browser, ViewRect top, ViewRect page) {
    size_t count;
    void **children = ChildrenOf(browser, &count);
    for (size_t i = 0; i < count; i++) {
        ViewRect rect = BoundsOf(children[i]);
        int margin = top.x - rect.x;
        if (margin > 0 && rect.y == top.y - margin && rect.width == top.width + 2 * margin && rect.height == top.height + page.height + 2 * margin) return children[i];
    }
    return NULL;
}

// Views with layers of their own keep what they painted, whatever's done to their parent, and that has this fade's alpha in it.
static void RepaintAll(BYTE *view) {
    size_t count;
    void **children = ChildrenOf(view, &count);
    ((SchedulePaint)(Base + Native.schedulePaint))(view);
    for (size_t i = 0; i < count; i++) RepaintAll(children[i]);
}

// Moves what Aside laid out to where the options and the cursor want it. The top bar and the page are one pane, and it's the
// pane that's inset, though the bar can stay out of that. A collapsed bar slides in from above, over the page or pushing it down,
// or stays where it is and is uncovered as the page's top is cut away. Hidden, the bar and the buttons are somewhere nothing shows
// them or can click them, since an inset can be as tall as they are.
static void PlaceWindow(Window *window) {
    ViewRect top = window->topRect, page = window->contentsRect, captions = window->captionsRect, full = BoundsOf(window->view);
    if (!top.height) return;  // Fullscreen.
    double shown = CollapseTop ? window->shown : 1;
    int left = !SidebarSideTakesInset && top.x > 0 ? 0 : Inset;
    int right = !SidebarSideTakesInset && top.x + top.width < full.width ? 0 : Inset;
    ViewRect pane = {top.x + left, top.y + Inset, top.width - left - right, top.height + page.height - 2 * Inset};
    ViewRect bar = BarTakesInset ? (ViewRect){pane.x, pane.y, pane.width, top.height} : top;
    int height = top.height, slid = Uncovers() ? 0 : (int)lround(height * (1 - shown));
    int taken = (int)lround(max(bar.y + height - pane.y, 0) * shown);
    BOOL pushes = !CollapseTop || RevealStyle == REVEAL_PUSH;
    BOOL hide = shown <= 0;
    window->clipped = pushes ? 0 : taken;
    window->alpha = Uncovers() ? shown : 1;
    if (window->captions) {
        int x = BarTakesInset ? captions.x - right : captions.x, y = BarTakesInset ? captions.y + Inset : captions.y;
        MoveView(window->captions, (ViewRect){x, hide ? -captions.height - height : y - slid, captions.width, captions.height});
    }
    MoveView(window->top, (ViewRect){bar.x, hide ? -height : bar.y - slid, bar.width, height});
    // Aside's layout sizes the toolbar, first in the bar, to end where the buttons were, so it has to end where they are.
    size_t count;
    void **inBar = ChildrenOf(window->top, &count);
    if (count && BarTakesInset && Inset && window->captions) {
        ViewRect toolbar = BoundsOf(inBar[0]);
        MoveView(inBar[0], (ViewRect){toolbar.x, toolbar.y, captions.x - right - bar.x, toolbar.height});
    }
    MoveView(window->contents, (ViewRect){pane.x, pane.y + taken, pane.width, pane.height - taken});
    ForEachContainer(window, ClipContainer);
    if (window->shadow) {
        // It's a strip down the side of a pane that fills the window, and an inset pane has no use for that: the strip would run on
        // past the page's corner. Nor is a bar with no background part of what it's beside.
        ViewRect around = window->shadowRect;
        int above = Uncovers() ? taken : 0;
        if (Inset) MoveView(window->shadow, (ViewRect){around.x, around.y, 0, 0});
        else MoveView(window->shadow, (ViewRect){around.x, around.y + above, around.width, around.height - above});
    }
    // Its background is painted just once otherwise, for wherever it was then.
    if (window->frame) ((SchedulePaint)(Base + Native.schedulePaint))(window->frame);
    if (Uncovers()) {
        RepaintAll(window->top);
        if (window->captions) RepaintAll(window->captions);
    }
}

static Window *FindWindow_(const BYTE *view) {
    for (int i = 0; i < WindowCount; i++)
        if (Windows[i].view == view) return &Windows[i];
    if (WindowCount == MAX_WINDOWS) return NULL;
    Window *window = &Windows[WindowCount++];
    *window = (Window){.view = (BYTE *)view, .vtable = *(void *const *)view};
    return window;
}

static BOOL IsLive(const Window *window) {
    void *vtable;
    return SafeRead(window->view, &vtable, sizeof vtable) && vtable == window->vtable;
}

// Where the cursor is in a window's view, if it's over that window.
static BOOL CursorIn(const Window *window, POINT *point) {
    if (!GetCursorPos(point)) return FALSE;
    HWND under = WindowFromPoint(*point);
    HWND root = under ? GetAncestor(under, GA_ROOT) : NULL;
    if (!root || GetWindowThreadProcessId(root, NULL) != GetCurrentThreadId()) return FALSE;
    RECT client;
    ViewRect size = BoundsOf(window->view);
    UINT dpi = GetDpiForWindow(root);
    if (!GetClientRect(root, &client) || !dpi || MulDiv(client.right, 96, dpi) != size.width || MulDiv(client.bottom, 96, dpi) != size.height) return FALSE;
    ScreenToClient(root, point);
    point->x = MulDiv(point->x, 96, dpi);
    point->y = MulDiv(point->y, 96, dpi);
    return TRUE;
}

static BOOL CALLBACK CheckForPopup(HWND window, LPARAM found) {
    char name[32];
    if (!IsWindowVisible(window) || !GetWindow(window, GW_OWNER)) return TRUE;
    if (!GetClassNameA(window, name, sizeof name) || strncmp(name, WIDGET_CLASS, sizeof WIDGET_CLASS - 1)) return TRUE;
    *(BOOL *)found = TRUE;
    return FALSE;
}

// A menu or the address bar's suggestions, which would be left hanging with nothing under them.
static BOOL HasPopup(void) {
    BOOL found = FALSE;
    EnumThreadWindows(GetCurrentThreadId(), CheckForPopup, (LPARAM)&found);
    return found;
}

static double Eased(ULONGLONG start) {
    double done = min((double)(GetTickCount64() - start) / MOTION_MS, 1.0);
    return done * done * (3 - 2 * done);
}

static void UpdateReveal(Window *window) {
    if (!CollapseTop || !window->topRect.height) return;
    POINT point;
    ViewRect top = window->topRect;
    int reach = window->revealed ? (BarTakesInset ? Inset : 0) + top.height : max(Inset, REVEAL_PIXELS);
    BOOL over = CursorIn(window, &point) && point.x >= top.x && point.x < top.x + top.width && point.y < top.y + reach;
    if (window->revealed && HasPopup()) over = TRUE;
    ULONGLONG now = GetTickCount64();
    if (over) window->leftAt = 0;
    else if (window->revealed && !window->leftAt) window->leftAt = now;
    BOOL revealed = over || (window->revealed && now - window->leftAt < HIDE_DELAY_MS);
    if (revealed != window->revealed) {
        window->revealed = revealed;
        window->from = window->shown;
        window->movedAt = now;
    }
    if (!window->movedAt) return;
    double eased = Eased(window->movedAt);
    window->shown = window->from + ((revealed ? 1 : 0) - window->from) * eased;
    if (eased >= 1) window->movedAt = 0;
    PlaceWindow(window);
}

static BOOL IsFullscreen(HWND window) {
    RECT rect;
    MONITORINFO monitor = {.cbSize = sizeof monitor};
    if ((GetWindowLongW(window, GWL_STYLE) & WS_CAPTION) == WS_CAPTION || !GetWindowRect(window, &rect)) return FALSE;
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) return FALSE;
    return EqualRect(&rect, &monitor.rcMonitor);
}

static void RestoreFrame(int index) {
    const Framed *framed = &FramedWindows[index];
    if (IsWindow(framed->window)) {
        static const DWORD border = BORDER_DEFAULT;
        SetFrame(framed->window, DWMWA_BORDER, &border, sizeof border);
        SetFrame(framed->window, DWMWA_CORNERS, &framed->corners, sizeof framed->corners);
    }
    FramedWindows[index] = FramedWindows[--FramedCount];
}

// Windows 11 draws a thin border around a window, and goes on drawing it around one that fills the screen. It's mostly see
// through, so it looks like a gap in front of the desktop. Rounded corners cut the last few pixels off the same way.
static BOOL CALLBACK UpdateFrame(HWND window, LPARAM unused) {
    (void)unused;
    char name[32];
    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) || !GetClassNameA(window, name, sizeof name) || strncmp(name, WIDGET_CLASS, sizeof WIDGET_CLASS - 1)) return TRUE;
    int found = -1;
    for (int i = 0; i < FramedCount && found < 0; i++)
        if (FramedWindows[i].window == window) found = i;
    BOOL wanted = CloseGap && IsFullscreen(window);
    if (wanted == (found >= 0)) return TRUE;
    if (!wanted) {
        RestoreFrame(found);
        return TRUE;
    }
    Framed framed = {.window = window};
    if (FramedCount == MAX_WINDOWS || FAILED(GetFrame(window, DWMWA_CORNERS, &framed.corners, sizeof framed.corners))) return TRUE;
    static const DWORD none = BORDER_NONE, square = CORNERS_SQUARE;
    SetFrame(window, DWMWA_BORDER, &none, sizeof none);
    SetFrame(window, DWMWA_CORNERS, &square, sizeof square);
    FramedWindows[FramedCount++] = framed;
    return TRUE;
}

static void CALLBACK Tick(HWND hwnd, UINT message, UINT_PTR timer, DWORD time) {
    (void)hwnd, (void)message, (void)timer, (void)time;
    static int ticks;
    if (GetFrame && !(++ticks % FRAME_EVERY_TICKS)) EnumThreadWindows(GetCurrentThreadId(), UpdateFrame, 0);
    for (int i = 0; i < WindowCount; i++) {
        if (!IsLive(&Windows[i])) {
            Windows[i--] = Windows[--WindowCount];
            continue;
        }
        UpdateReveal(&Windows[i]);
    }
}

static BOOL MovesBar(void) {
    return Inset || CollapseTop;
}

// The window's frame paints the bar's background, tinted from the page, at the place Aside's layout has for the bar. The bar
// isn't there any more, so the frame stops, and the bar has that paint done on itself.
static void FramePaint(void *view, void *canvas) {
    if (!Enabled || !MovesBar() || PaintingBar) OriginalFramePaint(view, canvas);
}

static void TopBackground(void *view, void *canvas) {
    if (!Enabled || !Uncovers()) OriginalTopBackground(view, canvas);
    if (!Enabled || !MovesBar() || Uncovers()) return;
    for (int i = 0; i < WindowCount; i++) {
        if (Windows[i].top != view || !Windows[i].frame) continue;
        void *paint = *(void **)((BYTE *)canvas + Native.canvasPaintOffset);
        void **vtable = *(void ***)paint;
        // An SkM44, by columns.
        const float move[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, (float)-Windows[i].topRect.x, (float)-Windows[i].topRect.y, 0, 1};
        ViewRect bar = BoundsOf(view);
        ViewRect inside = {0, 0, bar.width, bar.height};
        ((CanvasSave)vtable[Native.canvasSaveSlot])(paint);
        ((ClipRect)(Base + Native.clipRect))(canvas, &inside, CLIP_INTERSECT);
        ((CanvasConcat)vtable[Native.canvasConcatSlot])(paint, move);
        PaintingBar = TRUE;
        OriginalFramePaint(Windows[i].frame, canvas);
        PaintingBar = FALSE;
        ((CanvasRestore)vtable[Native.canvasRestoreSlot])(paint);
        return;
    }
}

// The bar and the buttons have no layers of their own to fade, and a layer would put them over the page, which has to cover them
// as it slides off. So every view in them paints through its own alpha. A view is only ever in one window.
static double AlphaOf(const BYTE *view) {
    for (const BYTE *up = view; up; up = *(BYTE *const *)(up + ChildrenOffset - sizeof(void *)))
        for (int i = 0; i < WindowCount; i++)
            if (up == Windows[i].top || up == Windows[i].captions) return Windows[i].alpha;
    return 1;
}

static void FadedPaint(void *view, void *canvas) {
    Paint original = NULL;
    for (int i = 0; i < FadedClassCount && !original; i++)
        if (FadedClasses[i].vtable == *(void **)view) original = FadedClasses[i].original;
    // The bar's own paint is its background, and the loading bar along its bottom draws a line there even with nothing loading.
    // All that's wanted of the bar here is what's on it.
    const BYTE *parent = *(BYTE **)((BYTE *)view + ChildrenOffset - sizeof(void *));
    for (int i = 0; i < WindowCount; i++)
        if (Enabled && Uncovers() && (view == Windows[i].top || (parent == Windows[i].top && BoundsOf(view).height <= MAX_LOADING_BAR))) return;
    double alpha = Enabled && Native.canvasAlphaSlot ? AlphaOf(view) : 1;
    if (alpha >= 1) {
        original(view, canvas);
        return;
    }
    void *paint = *(void **)((BYTE *)canvas + Native.canvasPaintOffset);
    void **vtable = *(void ***)paint;
    ((CanvasSaveAlpha)vtable[Native.canvasAlphaSlot])(paint, (float)alpha);
    original(view, canvas);
    ((CanvasRestore)vtable[Native.canvasRestoreSlot])(paint);
}

static void FadeClassesIn(const BYTE *view) {
    void *vtable = *(void *const *)view;
    int found = 0;
    while (found < FadedClassCount && FadedClasses[found].vtable != vtable) found++;
    if (found == FadedClassCount && FadedClassCount < MAX_FADED_CLASSES) {
        FadedClass *entry = &FadedClasses[FadedClassCount];
        entry->vtable = vtable;
        if (HookVtable(vtable, Native.paintSlot, FadedPaint, (void **)&entry->original)) FadedClassCount++;
    }
    size_t count;
    void **children = ChildrenOf(view, &count);
    for (size_t i = 0; i < count; i++) FadeClassesIn(children[i]);
}

// How much of a page is clipped, if this is one.
static int ClippedOf(const BYTE *page) {
    if (!IsA(page, Native.pageVtable)) return 0;
    const BYTE *container = *(BYTE **)(page + ChildrenOffset - sizeof(void *));
    const BYTE *contents = container ? *(BYTE **)(container + ChildrenOffset - sizeof(void *)) : NULL;
    for (int i = 0; i < WindowCount; i++)
        if (Windows[i].contents == contents) return Windows[i].clipped;
    return 0;
}

// What holds a page is what tells the page its size. A page's view sizes it to fill as soon as its own size changes, and that's
// put right after, so while the page is clipped it takes no notice of any other height.
static void HolderLayout(void *view, void *key) {
    const BYTE *page = *(BYTE **)((BYTE *)view + ChildrenOffset - sizeof(void *));
    int clipped = page ? ClippedOf(page) : 0;
    if (clipped && BoundsOf(view).height != BoundsOf(page).height + clipped) return;
    OriginalHolderLayout(view, key);
}

// The toolbar has the page's tint behind it, up to the window's buttons, which have none, and so does the address bar and what's
// in it. A class can be elsewhere too, so this looks at where the view is.
static BOOL IsTinted(const BYTE *view) {
    for (int i = 0; i < WindowCount; i++)
        if (view == Windows[i].toolbar) return TRUE;
    for (const BYTE *up = view; up; up = *(BYTE *const *)(up + ChildrenOffset - sizeof(void *)))
        for (int i = 0; i < WindowCount; i++)
            if (up == Windows[i].address) return TRUE;
    return FALSE;
}

static void TintedBackground(void *view, void *canvas) {
    Paint original = NULL;
    for (int i = 0; i < TintedClassCount && !original; i++)
        if (TintedClasses[i].vtable == *(void **)view) original = TintedClasses[i].original;
    if (!Enabled || !Uncovers() || !IsTinted(view)) original(view, canvas);
}

static void HookTinted(const BYTE *view) {
    void *vtable = *(void *const *)view;
    int found = 0;
    while (found < TintedClassCount && TintedClasses[found].vtable != vtable) found++;
    if (found < TintedClassCount || TintedClassCount == MAX_FADED_CLASSES) return;
    FadedClass *entry = &TintedClasses[TintedClassCount];
    entry->vtable = vtable;
    if (HookVtable(vtable, Native.paintBackgroundSlot, TintedBackground, (void **)&entry->original)) TintedClassCount++;
}

static void HookTintedIn(const BYTE *view) {
    HookTinted(view);
    size_t count;
    void **children = ChildrenOf(view, &count);
    for (size_t i = 0; i < count; i++) HookTintedIn(children[i]);
}

// The toolbar is first in the bar, and the address bar is the widest thing in the toolbar.
static void FindTinted(Window *window) {
    size_t count;
    void **children = ChildrenOf(window->top, &count);
    window->toolbar = count ? children[0] : NULL;
    window->address = NULL;
    if (!window->toolbar) return;
    children = ChildrenOf(window->toolbar, &count);
    for (size_t i = 0; i < count; i++)
        if (!window->address || BoundsOf(children[i]).width > BoundsOf(window->address).width) window->address = children[i];
    HookTinted(window->toolbar);
    if (window->address) HookTintedIn(window->address);
}

// A page's view lays what holds it out to fill it, whenever it likes.
static void PageLayout(void *view, void *key) {
    OriginalPageLayout(view, key);
    if (!Enabled || !ChildrenOffset) return;
    size_t count;
    void **children = ChildrenOf(view, &count);
    if (count && !OriginalHolderLayout) HookVtable(*(void ***)children[0], Native.layoutSlot, HolderLayout, (void **)&OriginalHolderLayout);
    int clipped = ClippedOf(view);
    if (clipped) ClipPage(view, clipped);
}

// The sidebar's header grows to reach the bottom of the bar, which an inset takes past its usual height, and what's under it moves
// down. A collapsed bar comes and goes, so all of that goes back to where it is with the bar hidden.
static void SidebarLayout(void *view, void *key) {
    OriginalSidebarLayout(view, key);
    if (!Enabled || !CollapseTop || !ChildrenOffset) return;
    const BYTE *browser = *(BYTE **)((BYTE *)view + ChildrenOffset - sizeof(void *));
    Window *window = NULL;
    for (int i = 0; i < WindowCount && !window; i++)
        if (Windows[i].view == browser) window = &Windows[i];
    BYTE *header = window ? ChildOfClass(view, Native.sidebarPartVtable) : NULL;
    if (!header) return;
    ViewRect rect = BoundsOf(header);
    if (window->shown <= 0 && !window->movedAt) window->sidebarHeader = rect.height;
    int extra = rect.height - window->sidebarHeader;
    if (!window->sidebarHeader || !extra) return;
    size_t count;
    void **children = ChildrenOf(view, &count);
    for (size_t i = 0; i < count; i++) {
        ViewRect below = BoundsOf(children[i]);
        if (children[i] == header || below.y < rect.y + rect.height) continue;
        BOOL reachesBottom = below.y + below.height >= BoundsOf(view).height;
        MoveView(children[i], (ViewRect){below.x, below.y - extra, below.width, reachesBottom ? below.height + extra : below.height});
    }
    MoveView(header, (ViewRect){rect.x, rect.y, rect.width, window->sidebarHeader});
    // Its buttons are centred in the height it asked for, not the height it has.
    children = ChildrenOf(header, &count);
    for (size_t i = 0; i < count; i++) {
        ViewRect button = BoundsOf(children[i]);
        MoveView(children[i], (ViewRect){button.x, (window->sidebarHeader - button.height) / 2, button.width, button.height});
    }
}

static void HookSidebar(const BYTE *browser) {
    size_t count;
    void **children = ChildrenOf(browser, &count);
    for (size_t i = 0; i < count; i++) {
        if (!ChildOfClass(children[i], Native.sidebarPartVtable)) continue;
        HookVtable(*(void ***)children[i], Native.layoutSlot, SidebarLayout, (void **)&OriginalSidebarLayout);
        return;
    }
}

// Aside's layout gives the page its own size and this then moves it. A page told both would lay itself out for both, and
// show it. So these two views wait until they're where they're going, and only ever see that.
static void ContentsLayout(void *view, void *key) {
    if (view == HeldContents) {
        ContentsWaiting = TRUE;
        return;
    }
    OriginalContentsLayout(view, key);
}

static void TopLayout(void *view, void *key) {
    if (view == HeldTop) {
        TopWaiting = TRUE;
        return;
    }
    OriginalTopLayout(view, key);
}

static void AfterLayout(void *view) {
    BYTE *browser = view;
    if (!ChildrenOffset && !LearnChildren(browser)) return;
    Window *window = FindWindow_(browser);
    if (!window) return;
    window->top = ChildOfClass(browser, Native.topVtable);
    window->contents = *(BYTE **)(browser + Native.contentsOffset);
    window->captions = FindCaptions(browser);
    window->frame = *(BYTE **)(browser + ChildrenOffset - sizeof(void *));
    if (window->frame && !OriginalFramePaint) {
        HookVtable(*(void ***)window->frame, Native.paintSlot, FramePaint, (void **)&OriginalFramePaint);
        HookVtable((void **)(Base + Native.topVtable), Native.paintBackgroundSlot, TopBackground, (void **)&OriginalTopBackground);
    }
    if (!window->top || !window->contents) return;
    if (Uncovers()) FindTinted(window);
    if (Uncovers() && Native.canvasAlphaSlot) {
        FadeClassesIn(window->top);
        if (window->captions) FadeClassesIn(window->captions);
    }
    window->topRect = BoundsOf(window->top);
    if (window->captions) window->captionsRect = BoundsOf(window->captions);
    window->contentsRect = BoundsOf(window->contents);
    window->shadow = FindShadow(browser, window->topRect, window->contentsRect);
    if (window->shadow) window->shadowRect = BoundsOf(window->shadow);
    if (!Timer) Timer = SetTimer(NULL, 0, TICK_MS, Tick);
    if (!OriginalPageLayout) HookVtable((void **)(Base + Native.pageVtable), Native.layoutSlot, PageLayout, (void **)&OriginalPageLayout);
    if (!OriginalContentsLayout) HookVtable(*(void ***)window->contents, Native.layoutSlot, ContentsLayout, (void **)&OriginalContentsLayout);
    if (!OriginalTopLayout) HookVtable((void **)(Base + Native.topVtable), Native.layoutSlot, TopLayout, (void **)&OriginalTopLayout);
    if (!OriginalSidebarLayout && Native.sidebarPartVtable) HookSidebar(browser);
    PlaceWindow(window);
    ApplyPageCorners(window);
}

static void BrowserLayout(void *view, void *key) {
    BOOL hold = Enabled && MovesBar();
    BYTE *contents = *(BYTE **)((BYTE *)view + Native.contentsOffset);
    HeldContents = hold && OriginalContentsLayout ? contents : NULL;
    HeldTop = hold && OriginalTopLayout && ChildrenOffset ? ChildOfClass(view, Native.topVtable) : NULL;
    BYTE *top = HeldTop;
    OriginalLayout(view, key);
    HeldContents = HeldTop = NULL;
    AfterLayout(view);
    // Moving them lays them out if that changed their size. If it didn't, that's still owed.
    if (ContentsWaiting) OriginalContentsLayout(contents, NULL);
    if (TopWaiting) OriginalTopLayout(top, NULL);
    ContentsWaiting = TopWaiting = FALSE;
}

// Looking in the heap can turn up a stale copy, and only a real one still lists its top bar.
static void LayOutAll(void) {
    void *found[MAX_WINDOWS];
    int count = FindObjects(Base + Native.browserVtable, found, MAX_WINDOWS);
    for (int i = 0; i < count; i++) {
        BYTE *browser = found[i];
        void *vtable;
        if (!SafeRead(browser, &vtable, sizeof vtable) || vtable != Base + Native.browserVtable) continue;
        if (!ChildrenOffset ? !LearnChildren(browser) : !ListsTopBar(browser, ChildrenOffset)) continue;
        void **slots = vtable;
        browser[Native.needsLayoutOffset] = 1;
        ((Layout)slots[Native.layoutSlot])(browser, NULL);
    }
}

PATCH_OPTION() {
    BOOL on = !strcmp(value, "true");
    if (!strcmp(key, "fullscreen.gap")) CloseGap = on;
    else if (!strcmp(key, "top.collapse")) CollapseTop = on;
    else if (!strcmp(key, "top.reveal")) RevealStyle = !strcmp(value, "push") ? REVEAL_PUSH : !strcmp(value, "uncover") ? REVEAL_UNCOVER : REVEAL_OVER;
    else if (!strcmp(key, "top.inset")) BarTakesInset = on;
    else if (!strcmp(key, "inset.sidebar")) SidebarSideTakesInset = on;
    else if (!strcmp(key, "inset")) Inset = atoi(value);
    else if (!strcmp(key, "corners.square")) SquareCorners = on;
    if (!Enabled) return;
    for (int i = 0; i < WindowCount; i++) Windows[i].sidebarHeader = 0;
    ApplyTopCorners();
    LayOutAll();
}

PATCH_START() {
    Base = base;
    return ResolveBindings(lookup, &Native);
}

PATCH_ENABLE() {
    if (!HookVtable((void **)(Base + Native.browserVtable), Native.layoutSlot, BrowserLayout, (void **)&OriginalLayout)) {
        ModLog("window-tweaks couldn't hook the browser's layout");
        return;
    }
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    GetFrame = dwm ? (DwmGetAttribute)(void *)GetProcAddress(dwm, "DwmGetWindowAttribute") : NULL;
    SetFrame = dwm ? (DwmSetAttribute)(void *)GetProcAddress(dwm, "DwmSetWindowAttribute") : NULL;
    if (!SetFrame) GetFrame = NULL;
    Enabled = TRUE;
    TopRadiusEdited = FALSE;
    ApplyTopCorners();
    LayOutAll();
    ModLog("window-tweaks ready: layout slot %d, radii +%#lx, set +%#lx", Native.layoutSlot, Native.radiiOffset, Native.radiiSetOffset);
}

PATCH_DISABLE() {
    Enabled = FALSE;
    for (int i = 0; i < WindowCount; i++) {
        if (!IsLive(&Windows[i]) || !Windows[i].clipped) continue;
        Windows[i].clipped = 0;
        ForEachContainer(&Windows[i], ClipContainer);
    }
    if (Timer) KillTimer(NULL, Timer);
    Timer = 0;
    OriginalPageLayout = OriginalContentsLayout = OriginalTopLayout = OriginalHolderLayout = OriginalSidebarLayout = NULL;
    OriginalFramePaint = NULL;
    while (FramedCount) RestoreFrame(0);
    FadedClassCount = TintedClassCount = 0;
    WindowCount = 0;
    // The hook's gone, so this puts everything back where Aside wants it.
    LayOutAll();
}
