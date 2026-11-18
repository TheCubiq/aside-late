// Closes the gap around a fullscreen window. Windows 11 draws a thin border around a window, and goes on drawing it around
// one that fills the screen. It's mostly see through, so it looks like a gap in front of the desktop, and rounded corners cut
// the last few pixels off the same way. This turns both off while a window is fullscreen, and puts them back when it isn't.
// It needs nothing from Aside, so a new build of Aside can't break it.

#include "aside-late.h"

#define MAX_WINDOWS 32
#define WIDGET_CLASS "Chrome_WidgetWin_"
#define TICK_MS 250
#define DWMWA_BORDER 34
#define DWMWA_CORNERS 33
#define BORDER_NONE 0xfffffffe
#define BORDER_DEFAULT 0xffffffff
#define CORNERS_SQUARE 1

typedef HRESULT (WINAPI *DwmGetAttribute)(HWND window, DWORD attribute, void *value, DWORD size);
typedef HRESULT (WINAPI *DwmSetAttribute)(HWND window, DWORD attribute, const void *value, DWORD size);

// A window whose frame this has changed, and its corners before that.
typedef struct {
    HWND window;
    DWORD corners;
} Framed;

static Framed FramedWindows[MAX_WINDOWS];
static int FramedCount;
static DwmGetAttribute GetFrame;
static DwmSetAttribute SetFrame;
static UINT_PTR Timer;

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

static BOOL CALLBACK UpdateFrame(HWND window, LPARAM unused) {
    (void)unused;
    char name[32];
    if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) || !GetClassNameA(window, name, sizeof name) || strncmp(name, WIDGET_CLASS, sizeof WIDGET_CLASS - 1)) return TRUE;
    int found = -1;
    for (int i = 0; i < FramedCount && found < 0; i++)
        if (FramedWindows[i].window == window) found = i;
    BOOL wanted = IsFullscreen(window);
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
    for (int i = 0; i < FramedCount; i++)
        if (!IsWindow(FramedWindows[i].window)) FramedWindows[i--] = FramedWindows[--FramedCount];
    EnumThreadWindows(GetCurrentThreadId(), UpdateFrame, 0);
}

PATCH_ENABLE() {
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    GetFrame = dwm ? (DwmGetAttribute)(void *)GetProcAddress(dwm, "DwmGetWindowAttribute") : NULL;
    SetFrame = dwm ? (DwmSetAttribute)(void *)GetProcAddress(dwm, "DwmSetWindowAttribute") : NULL;
    if (!GetFrame || !SetFrame) {
        ModLog("fullscreen-gap couldn't reach DWM");
        return;
    }
    Tick(NULL, 0, 0, 0);
    Timer = SetTimer(NULL, 0, TICK_MS, Tick);
}

PATCH_DISABLE() {
    if (Timer) KillTimer(NULL, Timer);
    Timer = 0;
    while (FramedCount) RestoreFrame(0);
}
