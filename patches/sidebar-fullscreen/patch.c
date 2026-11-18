// Reveals the sidebar in a fullscreen window, from the window's edge. Aside's hover controller isn't told anything in a
// fullscreen window, and answering for it only makes it reset, so this works from the cursor. The layout zeroes and hides the
// sidebar's view there, so straight after it this slides the view in, as a card set out like Aside's own hover.

#include "resolve.h"

#include <math.h>

#define MAX_BROWSERS 32
#define WIDGET_CLASS "Chrome_WidgetWin_"
#define TICK_MS 16
#define DEFAULT_REGION_WIDTH 280
#define CARD_MARGIN 8
#define MOTION_MS 180

typedef struct {
    int x, y, width, height;
} ViewRect;

typedef void (*Layout)(void *view, void *key);
typedef void (*SetVisible)(void *view, BOOL visible);
typedef void (*SetBounds)(void *view, const ViewRect *rect);
typedef void (*SchedulePaint)(void *view);

typedef struct {
    BYTE *browser;
    void *vtable;
    BOOL right;             // The sidebar is on the right.
    int regionWidth;        // How wide the sidebar last was outside fullscreen.
    BOOL revealed;
    ULONGLONG enteredAt, leftAt;
    double from, slid;      // How far it's slid in, and where that started from.
    ULONGLONG movedAt;      // 0 when it isn't moving.
} Watched;

static Bindings Native;
static BYTE *Base;
static Layout OriginalLayout;
static Watched Browsers[MAX_BROWSERS];
static int BrowserCount;
static UINT_PTR Timer;
static DWORD ParentOffset;
static DWORD RegionOffset;  // Which of the layout's views is the sidebar's, once seen. 0 until then.
static BOOL Enabled;
static int EdgeWidth = 8, EnterMs = 100, LeaveMs = 500;

static BOOL CALLBACK CheckForPopup(HWND window, LPARAM found) {
    char name[32];
    if (IsWindowVisible(window) && GetWindow(window, GW_OWNER) && GetClassNameA(window, name, sizeof name) && !strncmp(name, WIDGET_CLASS, sizeof WIDGET_CLASS - 1)) {
        *(BOOL *)found = TRUE;
        return FALSE;
    }
    return TRUE;
}

// A menu is a window the browser's window owns.
static BOOL HasPopup(void) {
    BOOL found = FALSE;
    EnumThreadWindows(GetCurrentThreadId(), CheckForPopup, (LPARAM)&found);
    return found;
}

typedef struct {
    const int *size;
    HWND window;
} FullscreenSearch;

// The fullscreen window that a view fills.
static BOOL CALLBACK FindFullscreen(HWND window, LPARAM out) {
    FullscreenSearch *search = (FullscreenSearch *)out;
    RECT rect;
    MONITORINFO monitor = {.cbSize = sizeof monitor};
    char name[32];
    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) || (GetWindowLongW(window, GWL_STYLE) & WS_CAPTION) == WS_CAPTION) return TRUE;
    if (!GetClassNameA(window, name, sizeof name) || strncmp(name, WIDGET_CLASS, sizeof WIDGET_CLASS - 1)) return TRUE;
    if (!GetWindowRect(window, &rect) || !GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor) || !EqualRect(&rect, &monitor.rcMonitor)) return TRUE;
    UINT dpi = GetDpiForWindow(window);
    if (!dpi || MulDiv(rect.right - rect.left, 96, dpi) != search->size[2] || MulDiv(rect.bottom - rect.top, 96, dpi) != search->size[3]) return TRUE;
    search->window = window;
    return FALSE;
}

static HWND FullscreenWindowOf(const BYTE *view) {
    FullscreenSearch search = {(const int *)(view + Native.boundsOffset), NULL};
    EnumThreadWindows(GetCurrentThreadId(), FindFullscreen, (LPARAM)&search);
    return search.window;
}

static BYTE *RegionOf(const Watched *watched) {
    return RegionOffset ? *(BYTE **)(watched->browser + RegionOffset) : NULL;
}

// Showing the view tells its parent, which lays out again and hides it, for ever. So for that long it has no parent to tell.
static void ShowRegion(BYTE *region, const BYTE *browser, BOOL visible) {
    if (!region || !region[Native.visibleOffset] == !visible) return;
    if (!ParentOffset) {
        for (DWORD offset = 0x100; offset < 0x300 && !ParentOffset; offset += 8) {
            const BYTE *held;
            if (SafeRead(region + offset, &held, sizeof held) && held == browser) ParentOffset = offset;
        }
        if (!ParentOffset) return;
    }
    const BYTE **parent = (const BYTE **)(region + ParentOffset);
    void **vtable = *(void ***)region;
    *parent = NULL;
    ((SetVisible)vtable[Native.setVisibleSlot])(region, visible);
    *parent = browser;
}

// Set out as Aside's own hover does: a card with a margin around it.
static void PlaceRevealed(const Watched *watched) {
    BYTE *region = RegionOf(watched);
    if (!region) return;
    const int *window = (const int *)(watched->browser + Native.boundsOffset);
    int width = watched->regionWidth ? watched->regionWidth : DEFAULT_REGION_WIDTH;
    int away = (int)lround((width + CARD_MARGIN) * (1 - watched->slid));
    int x = watched->right ? window[2] - width - CARD_MARGIN + away : CARD_MARGIN - away;
    ViewRect rect = {x, CARD_MARGIN, width, window[3] - 2 * CARD_MARGIN};
    ShowRegion(region, watched->browser, TRUE);
    ((SetBounds)(Base + Native.setBounds))(region, &rect);
    ((SchedulePaint)(Base + Native.schedulePaint))(region);
}

static void LayOut(BYTE *browser) {
    browser[Native.needsLayoutOffset] = 1;
    ((Layout)(*(void ***)browser)[Native.layoutSlot])(browser, NULL);
}

static Watched *Watch(BYTE *browser) {
    for (int i = 0; i < BrowserCount; i++)
        if (Browsers[i].browser == browser) return &Browsers[i];
    if (BrowserCount == MAX_BROWSERS) return NULL;
    Watched *watched = &Browsers[BrowserCount++];
    *watched = (Watched){.browser = browser, .vtable = *(void **)browser};
    return watched;
}

// Of the views the layout checks, the sidebar's is the one running the full height of a window that isn't fullscreen, along
// one of its edges.
static void LearnRegion(const BYTE *browser) {
    const int *window = (const int *)(browser + Native.boundsOffset);
    for (int i = 0; i < Native.regionCount && !RegionOffset; i++) {
        const BYTE *view;
        int b[4];
        if (!SafeRead(browser + Native.regionOffsets[i], &view, sizeof view) || !view || !SafeRead(view + Native.boundsOffset, b, sizeof b)) continue;
        if (b[2] <= 0 || b[3] != window[3] || (b[0] != 0 && b[0] + b[2] != window[2]) || b[2] * 2 > window[2]) continue;
        RegionOffset = Native.regionOffsets[i];
        ModLog("sidebar-fullscreen: the sidebar is at +%#lx", RegionOffset);
    }
}

// Outside fullscreen the layout says how wide the sidebar is and which side it's on, for when it's revealed.
static void BrowserLayout(void *view, void *key) {
    OriginalLayout(view, key);
    if (!Enabled) return;
    BYTE *browser = view;
    if (!RegionOffset && !FullscreenWindowOf(browser)) LearnRegion(browser);
    Watched *watched = RegionOffset && *(BYTE **)(browser + RegionOffset) ? Watch(browser) : NULL;
    if (!watched) return;
    const int *bounds = (const int *)(RegionOf(watched) + Native.boundsOffset);
    if (!watched->revealed && !watched->movedAt) {
        if (bounds[2] && !FullscreenWindowOf(browser)) {
            watched->regionWidth = bounds[2];
            watched->right = bounds[0] > ((const int *)(browser + Native.boundsOffset))[2] / 2;
        }
        return;
    }
    PlaceRevealed(watched);
}

static void Update(Watched *watched) {
    HWND window = FullscreenWindowOf(watched->browser);
    BOOL wanted = FALSE;
    POINT cursor;
    RECT rect;
    ULONGLONG now = GetTickCount64();
    if (window && GetCursorPos(&cursor) && GetWindowRect(window, &rect)) {
        int width = MulDiv((watched->regionWidth ? watched->regionWidth : DEFAULT_REGION_WIDTH) + 2 * CARD_MARGIN, GetDpiForWindow(window), 96);
        int fromEdge = watched->right ? rect.right - 1 - cursor.x : cursor.x - rect.left;
        HWND under = WindowFromPoint(cursor);
        BOOL onWindow = under && GetAncestor(under, GA_ROOT) == window;
        if (watched->revealed) {
            if (HasPopup() || (onWindow && fromEdge >= 0 && fromEdge < width)) watched->leftAt = 0;
            else if (!watched->leftAt) watched->leftAt = now;
            wanted = !watched->leftAt || now - watched->leftAt < (ULONGLONG)LeaveMs;
        } else {
            if (!onWindow || fromEdge < 0 || fromEdge >= EdgeWidth) watched->enteredAt = 0;
            else if (!watched->enteredAt) watched->enteredAt = now;
            wanted = watched->enteredAt && now - watched->enteredAt >= (ULONGLONG)EnterMs;
        }
    }
    if (wanted != watched->revealed) {
        watched->revealed = wanted;
        watched->enteredAt = watched->leftAt = 0;
        watched->from = watched->slid;
        watched->movedAt = now;
    }
    if (!watched->movedAt) return;
    double done = min((double)(now - watched->movedAt) / MOTION_MS, 1.0);
    watched->slid = watched->from + ((wanted ? 1 : 0) - watched->from) * done * done * (3 - 2 * done);
    if (done >= 1) watched->movedAt = 0;
    // Still a window that's fullscreen, or it left that and the layout has the view back.
    if (window || wanted || watched->movedAt) PlaceRevealed(watched);
    if (watched->movedAt || wanted) return;
    ShowRegion(RegionOf(watched), watched->browser, FALSE);
    LayOut(watched->browser);
}

static void CALLBACK Tick(HWND hwnd, UINT message, UINT_PTR timer, DWORD time) {
    (void)hwnd, (void)message, (void)timer, (void)time;
    for (int i = 0; i < BrowserCount; i++) {
        void *vtable;
        if (!SafeRead(Browsers[i].browser, &vtable, sizeof vtable) || vtable != Browsers[i].vtable) {
            Browsers[i--] = Browsers[--BrowserCount];
            continue;
        }
        Update(&Browsers[i]);
    }
}

PATCH_OPTION() {
    if (!strcmp(key, "edge.width")) EdgeWidth = atoi(value);
    else if (!strcmp(key, "delay.enter")) EnterMs = atoi(value);
    else if (!strcmp(key, "delay.leave")) LeaveMs = atoi(value);
}

PATCH_START() {
    Base = base;
    return ResolveBindings(lookup, &Native);
}

PATCH_ENABLE() {
    if (!HookVtable((void **)(Base + Native.browserVtable), Native.layoutSlot, BrowserLayout, (void **)&OriginalLayout)) {
        ModLog("sidebar-fullscreen couldn't hook the browser's layout");
        return;
    }
    Enabled = TRUE;
    Timer = SetTimer(NULL, 0, TICK_MS, Tick);
}

PATCH_DISABLE() {
    Enabled = FALSE;
    if (Timer) KillTimer(NULL, Timer);
    Timer = 0;
    for (int i = 0; i < BrowserCount; i++) {
        void *vtable;
        if (!SafeRead(Browsers[i].browser, &vtable, sizeof vtable) || vtable != Browsers[i].vtable) continue;
        if (!Browsers[i].revealed && !Browsers[i].movedAt) continue;
        ShowRegion(RegionOf(&Browsers[i]), Browsers[i].browser, FALSE);
        LayOut(Browsers[i].browser);
    }
    BrowserCount = 0;
}
