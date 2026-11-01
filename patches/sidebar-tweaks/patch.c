// Changes how a collapsed sidebar comes and goes, by changing what its hover controller is told and when it's asked.
//
// Aside's controller is a state machine that asks a callback for the cursor and some rects, and works out from those
// whether the sidebar should be showing. So nothing here shows or hides anything: it answers that callback differently.

#include <stdint.h>
#include "resolve.h"

#define WIDGET_CLASS "Chrome_WidgetWin_"
#define MAX_VIEWS 32
#define MAX_FOUND 64
#define TICK_MS 16
#define WATCH_MS 1000
// How far either side of the window's edge Aside's own strip reaches, and how far past the panel to say the cursor is
// instead of in it.
#define NATIVE_STRIP 32
#define PAST_PANEL 100
#define STATE_CLOSED 0
#define STATE_OPENING 1
#define STATE_CLOSING 3

// What the controller asks for. This is its shape as the callback clears it, which is checked when it's found.
typedef struct {
    int window[4];   // x, y, width, height: where the sidebar's edge is.
    int panel[4];    // Where the sidebar shows when it's open.
    int x, y;        // The cursor.
    BYTE right;      // The sidebar is on the right.
    BYTE enabled;
    BYTE overButton; // The cursor is over the button that opens it.
    BYTE shown;
    BYTE padding[4];
} Input;
typedef void *(*GetInput)(void *view, Input *input);
typedef void (*Evaluate)(void *controller);
typedef struct {
    void **slot;     // Where the callback keeps the method it runs.
    BYTE *view;
    void *vtable;
    Input last;      // What it was last told while its window was active.
    BOOL known;
    ULONGLONG entered;
    BOOL leaveMoved;
} Watched;

static BYTE *Base;
static BOOL KeepForMenus = TRUE;
static BOOL EdgeEnabled = TRUE;
static int EdgeWidth = 8;
static int EnterMs = 100;
static int LeaveMs = 500;
static Watched Views[MAX_VIEWS];
static int ViewCount;
static UINT_PTR Timer;
static volatile BOOL Enabled;
static HANDLE WatchThread;
static int LastWindowCount = -1;
static BOOL HideOnBlurEdited;


static void ApplyHideOnBlur(void) {
    static BYTE original[6];
    static const BYTE nothing[6] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    if (!HideOnBlurRva || HideOnBlurEdited == KeepForMenus) return;
    BYTE *site = Base + HideOnBlurRva;
    if (!HideOnBlurEdited) memcpy(original, site, sizeof original);
    if (EditCode(site, HideOnBlurEdited ? nothing : original, HideOnBlurEdited ? original : nothing, sizeof nothing)) HideOnBlurEdited = !HideOnBlurEdited;
    else ModLog("sidebar-tweaks couldn't change what happens when a window loses focus");
}

static BOOL CALLBACK CheckForPopup(HWND window, LPARAM found) {
    char name[32];
    if (!IsWindowVisible(window) || !GetWindow(window, GW_OWNER)) return TRUE;
    if (!GetClassNameA(window, name, sizeof name) || strncmp(name, WIDGET_CLASS, sizeof WIDGET_CLASS - 1)) return TRUE;
    *(BOOL *)found = TRUE;
    return FALSE;
}

// A menu, its submenus and anything else like them are windows owned by the browser's, and they take the mouse away.
static BOOL HasPopup(void) {
    BOOL found = FALSE;
    EnumThreadWindows(GetCurrentThreadId(), CheckForPopup, (LPARAM)&found);
    return found;
}

// A maximized window starts past the edge of the screen, and that's where Aside's own strip ends up. This one starts
// where the window can actually be seen.
static BOOL InEdge(const int *window, BOOL right, int x, int y) {
    POINT point = {x, y};
    MONITORINFO monitor = {.cbSize = sizeof monitor};
    if (!GetMonitorInfoW(MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST), &monitor)) return FALSE;
    HWND under = WindowFromPoint(point);
    DWORD thread = under ? GetWindowThreadProcessId(under, NULL) : 0;
    if (thread != GetCurrentThreadId()) return FALSE;
    if (y < monitor.rcMonitor.top || y >= window[1] + window[3]) return FALSE;
    if (right) {
        int edge = min(window[0] + window[2], monitor.rcMonitor.right);
        return x < edge && x >= edge - EdgeWidth;
    }
    int edge = max(window[0], monitor.rcMonitor.left);
    return x >= edge && x < edge + EdgeWidth;
}

static Watched *FindWatched(const BYTE *view) {
    for (int i = 0; i < ViewCount; i++)
        if (Views[i].view == view) return &Views[i];
    return NULL;
}

// Aside moves the time it started closing from, so that its own comparison comes out at this delay.
static void MoveLeaveTime(Watched *watched, BYTE *controller, int state) {
    if (state != STATE_CLOSING) {
        watched->leaveMoved = FALSE;
        return;
    }
    if (watched->leaveMoved) return;
    watched->leaveMoved = TRUE;
    *(LONGLONG *)(controller + LeaveTimeOffset) += (LONGLONG)LeaveMs * 1000 - NativeLeaveUs;
}

static void HoldOverPanel(Input *input) {
    input->x = input->panel[0] + input->panel[2] / 2;
    input->y = input->panel[1] + input->panel[3] / 2;
    input->overButton = 0;
}

static BOOL IsLive(const Watched *watched) {
    void *vtable;
    return SafeRead(watched->view, &vtable, sizeof vtable) && vtable == watched->vtable;
}

// The controller only asks again when the mouse moves over the window, or while it's waiting for something. A delay
// needs asking again without either, and so does a menu having closed.
static void CALLBACK Tick(HWND window, UINT message, UINT_PTR timer, DWORD time) {
    (void)window, (void)message, (void)timer, (void)time;
    POINT cursor;
    BOOL haveCursor = GetCursorPos(&cursor);
    for (int i = 0; i < ViewCount; i++) {
        Watched *watched = &Views[i];
        if (!IsLive(watched)) {
            *watched = Views[--ViewCount];
            i--;
            continue;
        }
        BYTE *controller = *(BYTE **)(watched->view + ControllerOffset);
        if (!controller) continue;
        int state = *(const int *)(controller + StateOffset);
        BOOL atEdge = EdgeEnabled && haveCursor && watched->known && InEdge(watched->last.window, watched->last.right, cursor.x, cursor.y);
        if (state != STATE_CLOSED || watched->entered || atEdge) ((Evaluate)(Base + EvaluateRva))(controller);
    }
}

// A menu takes the focus, and a window without it reports nothing at all, which closes the sidebar. It's still where it was.
static void AnswerWithoutFocus(const Watched *watched, Input *input, BOOL hidden) {
    if (!KeepForMenus || !watched->known || hidden || !HasPopup()) return;
    *input = watched->last;
    HoldOverPanel(input);
}

static void AnswerWhileHidden(Watched *watched, Input *input) {
    BOOL edge = EdgeEnabled && InEdge(input->window, input->right, input->x, input->y);
    if (input->overButton || edge) {
        if (!watched->entered) watched->entered = GetTickCount64();
    } else {
        watched->entered = 0;
    }
    BOOL ready = watched->entered && GetTickCount64() - watched->entered >= (ULONGLONG)EnterMs;
    input->overButton = (BYTE)ready;
    // Aside's own strip mustn't open it first. It can still be on its way out, so that's away from the panel too.
    int edgeX = input->window[0] + (input->right ? input->window[2] : 0);
    if (EdgeEnabled && !ready && abs(input->x - edgeX) < NATIVE_STRIP) input->x = input->right ? input->panel[0] - PAST_PANEL : input->panel[0] + input->panel[2] + PAST_PANEL;
}

// Between the edge and the panel is still on the way to it, and so is anywhere while a menu is up.
static void AnswerWhileShowing(Input *input) {
    BOOL beside = input->right ? input->x >= input->panel[0] + input->panel[2] : input->x < input->panel[0];
    BOOL inGap = EdgeEnabled && beside && input->y >= input->panel[1] && input->y < input->panel[1] + input->panel[3];
    if ((KeepForMenus && HasPopup()) || inGap) HoldOverPanel(input);
}

static void *AnswerInput(void *view, Input *input) {
    void *result = ((GetInput)(Base + GetInputRva))(view, input);
    Watched *watched = FindWatched(view);
    BYTE *controller = *(BYTE **)((BYTE *)view + ControllerOffset);
    if (!Timer) Timer = SetTimer(NULL, 0, TICK_MS, Tick);
    if (!watched || !controller) return result;
    int state = *(const int *)(controller + StateOffset);
    BOOL hidden = state == STATE_CLOSED || state == STATE_OPENING;
    MoveLeaveTime(watched, controller, state);
    if (!input->enabled) {
        AnswerWithoutFocus(watched, input, hidden);
        return result;
    }
    watched->last = *input;
    watched->known = TRUE;
    if (hidden) {
        AnswerWhileHidden(watched, input);
    } else {
        watched->entered = 0;
        AnswerWhileShowing(input);
    }
    return result;
}

// Every window's callback is an object holding this method, so looking for the method finds them all.
static void FindViews(void *unused) {
    (void)unused;
    if (!Enabled) return;
    void *found[MAX_FOUND];
    int count = FindObjects(Base + GetInputRva, found, MAX_FOUND);
    for (int i = 0; i < count && ViewCount < MAX_VIEWS; i++) {
        void **slot = found[i];
        BYTE *state = (BYTE *)slot - 0x20, *view;
        void *vtable, *invoke;
        // A result could be a stale copy, and a live one holds what it runs and what it runs it on.
        if (!SafeRead(state + 8, &invoke, sizeof invoke) || !IsInAside(invoke)) continue;
        if (!SafeRead(state + 0x30, &view, sizeof view) || !view || !SafeRead(view, &vtable, sizeof vtable) || !IsInAside(vtable)) continue;
        BYTE *controller;
        if (!SafeRead(view + ControllerOffset, &controller, sizeof controller) || !controller || *(void **)controller != state) continue;
        Views[ViewCount++] = (Watched){.slot = slot, .view = view, .vtable = vtable};
        *slot = AnswerInput;
    }
    ModLog("sidebar-tweaks: %d windows", ViewCount);
}

static BOOL CALLBACK CountWindow(HWND window, LPARAM count) {
    DWORD process;
    char name[32];
    GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window, GW_OWNER)) return TRUE;
    if (GetClassNameA(window, name, sizeof name) && !strncmp(name, WIDGET_CLASS, sizeof WIDGET_CLASS - 1)) ++*(int *)count;
    return TRUE;
}

// Nothing says a window has opened, and until one has there's nothing to find. Looking takes a moment, so it's only
// done when the number of them goes up.
static DWORD WINAPI WatchWindows(void *unused) {
    (void)unused;
    while (Enabled) {
        int count = 0;
        EnumWindows(CountWindow, (LPARAM)&count);
        if (count > LastWindowCount && count) RunOnUiThread(FindViews, NULL);
        LastWindowCount = count;
        Sleep(WATCH_MS);
    }
    return 0;
}

PATCH_OPTION() {
    BOOL on = !strcmp(value, "true");
    if (!strcmp(key, "menus.keep")) {
        KeepForMenus = on;
        if (Enabled) ApplyHideOnBlur();
    }
    else if (!strcmp(key, "edge.enabled")) EdgeEnabled = on;
    else if (!strcmp(key, "edge.width")) EdgeWidth = atoi(value);
    else if (!strcmp(key, "delay.enter")) EnterMs = atoi(value);
    else if (!strcmp(key, "delay.leave")) LeaveMs = atoi(value);
}

PATCH_START() {
    Base = base;
    return ResolveController(lookup);
}

PATCH_ENABLE() {
    Enabled = TRUE;
    LastWindowCount = -1;
    HideOnBlurEdited = FALSE;
    ApplyHideOnBlur();
    if (!HideOnBlurRva) ModLog("sidebar-tweaks can't keep the sidebar open for a menu in this build");
    WatchThread = CreateThread(NULL, 0, WatchWindows, NULL, 0, NULL);
}

PATCH_DISABLE() {
    Enabled = FALSE;
    HideOnBlurEdited = FALSE;
    if (Timer) KillTimer(NULL, Timer);
    Timer = 0;
    for (int i = 0; i < ViewCount; i++)
        if (IsLive(&Views[i])) *Views[i].slot = (void *)(Base + GetInputRva);
    ViewCount = 0;
    if (WatchThread) CloseHandle(WatchThread);
    WatchThread = NULL;
}
