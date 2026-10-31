#include "flyout.h"

#include <dwmapi.h>
#include <gdiplus.h>

#define CELL 28
#define DOT 18
#define PADDING 8
#define GAP 6
#define WATCH_MS 40
#define MAX_KNOWN 64
#define WAIT_TICKS 50

// Slot 0 clears the color; the rest are the palette.
#define CELL_COUNT (COLOR_COUNT + 1)

static HWND Window, Menu;
static const DWORD *Colors;
static int Selected, Hovered = -1, WaitTicks;
static ColorPicked Picked;
static double Scale;
static HWND Known[MAX_KNOWN];
static int KnownCount;
static ULONG_PTR GdiplusToken;

static BOOL IsLightTheme(void) {
    DWORD value = 0, size = sizeof value;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &value, &size);
    return value != 0;
}

static int Px(int value) {
    return (int)(value * Scale + 0.5);
}

static void Dot(GpGraphics *graphics, int cell, ARGB color, BOOL fill) {
    float x = (float)Px(PADDING + cell * CELL + (CELL - DOT) / 2), y = (float)Px(PADDING + (CELL - DOT) / 2), size = (float)Px(DOT);
    if (fill) {
        GpSolidFill *brush;
        GdipCreateSolidFill(color, &brush);
        GdipFillEllipse(graphics, (GpBrush *)brush, x, y, size, size);
        GdipDeleteBrush((GpBrush *)brush);
    } else {
        GpPen *pen;
        GdipCreatePen1(color, (float)Px(1) * 1.5f, UnitPixel, &pen);
        GdipDrawEllipse(graphics, pen, x, y, size, size);
        GdipDeletePen(pen);
    }
}

static void Ring(GpGraphics *graphics, int cell, ARGB color) {
    float inset = (float)Px((CELL - DOT) / 2 - 3);
    float x = (float)Px(PADDING + cell * CELL) + inset, y = (float)Px(PADDING) + inset, size = (float)Px(CELL) - inset * 2;
    GpPen *pen;
    GdipCreatePen1(color, (float)Px(2), UnitPixel, &pen);
    GdipDrawEllipse(graphics, pen, x, y, size, size);
    GdipDeletePen(pen);
}

static void Paint(HDC dc, const RECT *client) {
    BOOL light = IsLightTheme();
    int width = client->right, height = client->bottom;
    HDC memory = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, width, height), previous = SelectObject(memory, bitmap);
    GpGraphics *graphics;
    GdipCreateFromHDC(memory, &graphics);
    GdipSetSmoothingMode(graphics, SmoothingModeAntiAlias);
    GdipGraphicsClear(graphics, light ? 0xFFF7F7F7 : 0xFF2B2B2B);

    ARGB ink = light ? 0xFF5F6368 : 0xFF9AA0A6;
    Dot(graphics, 0, ink, FALSE);
    float x1 = (float)Px(PADDING + (CELL - DOT) / 2 + 4), y1 = (float)Px(PADDING + (CELL + DOT) / 2 - 4);
    float x2 = (float)Px(PADDING + (CELL + DOT) / 2 - 4), y2 = (float)Px(PADDING + (CELL - DOT) / 2 + 4);
    GpPen *pen;
    GdipCreatePen1(ink, (float)Px(1) * 1.5f, UnitPixel, &pen);
    GdipDrawLine(graphics, pen, x1, y1, x2, y2);
    GdipDeletePen(pen);
    for (int i = 0; i < COLOR_COUNT; i++) Dot(graphics, i + 1, 0xFF000000 | Colors[i], TRUE);

    ARGB ring = light ? 0xFF202124 : 0xFFE8EAED;
    Ring(graphics, Selected + 1, ring);
    if (Hovered >= 0 && Hovered != Selected + 1) Ring(graphics, Hovered, (ring & 0x00FFFFFF) | 0x66000000);

    GdipDeleteGraphics(graphics);
    BitBlt(dc, 0, 0, width, height, memory, 0, 0, SRCCOPY);
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
}

static int CellAt(LPARAM position) {
    int x = GET_X_LPARAM(position), y = GET_Y_LPARAM(position);
    if (y < Px(PADDING) || y >= Px(PADDING + CELL) || x < Px(PADDING)) return -1;
    int cell = (x - Px(PADDING)) / Px(CELL);
    return cell < CELL_COUNT ? cell : -1;
}

static void Close(void) {
    if (Window) DestroyWindow(Window);
    Window = NULL;
    Menu = NULL;
}

static BOOL IsOurs(HWND window) {
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    return process == GetCurrentProcessId() && window != Window && IsWindowVisible(window);
}

static BOOL CALLBACK Snapshot(HWND window, LPARAM unused) {
    (void)unused;
    if (KnownCount < MAX_KNOWN && IsOurs(window)) Known[KnownCount++] = window;
    return TRUE;
}

// Aside's menu is a WinUI popup with its own thread. A small host window opens with it.
static BOOL CALLBACK FindNewPopup(HWND window, LPARAM found) {
    if (!IsOurs(window)) return TRUE;
    for (int i = 0; i < KnownCount; i++)
        if (Known[i] == window) return TRUE;
    RECT rect, best;
    GetWindowRect(window, &rect);
    HWND current = *(HWND *)found;
    if (rect.right - rect.left <= 40 || rect.bottom - rect.top <= 40) return TRUE;
    if (current && GetWindowRect(current, &best) &&
        (best.right - best.left) * (best.bottom - best.top) >= (rect.right - rect.left) * (rect.bottom - rect.top)) return TRUE;
    *(HWND *)found = window;
    return TRUE;
}

static void Place(const RECT *anchor) {
    RECT own;
    GetWindowRect(Window, &own);
    int width = own.right - own.left, height = own.bottom - own.top;
    MONITORINFO monitor = {.cbSize = sizeof monitor};
    GetMonitorInfoW(MonitorFromRect(anchor, MONITOR_DEFAULTTONEAREST), &monitor);
    int x = anchor->left, y = anchor->top - height - Px(GAP);
    if (y < monitor.rcWork.top) y = anchor->bottom + Px(GAP);
    if (x + width > monitor.rcWork.right) x = monitor.rcWork.right - width;
    SetWindowPos(Window, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

// The flyout appears once Aside's menu does, follows it, and leaves with it.
static void Watch(void) {
    if (!Menu) {
        HWND found = NULL;
        EnumWindows(FindNewPopup, (LPARAM)&found);
        if (found) {
            RECT anchor;
            GetWindowRect(found, &anchor);
            Menu = found;
            Place(&anchor);
            return;
        }
        if (--WaitTicks <= 0) Close();
        return;
    }
    if (!IsWindow(Menu) || !IsWindowVisible(Menu)) Close();
}

static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_TIMER:
        Watch();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PRINTCLIENT: {
        RECT client;
        GetClientRect(window, &client);
        Paint((HDC)wParam, &client);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint;
        RECT client;
        HDC dc = BeginPaint(window, &paint);
        GetClientRect(window, &client);
        Paint(dc, &client);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_MOUSEMOVE: {
        int cell = CellAt(lParam);
        if (cell != Hovered) {
            Hovered = cell;
            InvalidateRect(window, NULL, FALSE);
            TRACKMOUSEEVENT track = {sizeof track, TME_LEAVE, window, 0};
            TrackMouseEvent(&track);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        Hovered = -1;
        InvalidateRect(window, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        int cell = CellAt(lParam);
        if (cell < 0) return 0;
        Selected = cell - 1;
        InvalidateRect(window, NULL, FALSE);
        Picked(Selected);
        return 0;
    }
    case WM_NCDESTROY:
        if (Window == window) Window = NULL;
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void FlyoutShow(HWND owner, POINT cursor, const DWORD *colors, int selected, ColorPicked picked) {
    Close();
    static BOOL registered;
    HINSTANCE instance = GetModuleHandleW(NULL);
    if (!registered) {
        WNDCLASSW windowClass = {.lpfnWndProc = WindowProc, .hInstance = instance, .lpszClassName = L"AsideLateFlyout", .hCursor = LoadCursor(NULL, IDC_HAND)};
        RegisterClassW(&windowClass);
        GdiplusStartupInput input = {1, NULL, FALSE, FALSE};
        GdiplusStartup(&GdiplusToken, &input, NULL);
        registered = TRUE;
    }
    Colors = colors;
    Selected = selected;
    Picked = picked;
    Hovered = -1;
    Scale = GetDpiForWindow(owner) / 96.0;
    WaitTicks = WAIT_TICKS;
    KnownCount = 0;
    EnumWindows(Snapshot, 0);

    int width = Px(PADDING * 2 + CELL * CELL_COUNT), height = Px(PADDING * 2 + CELL);
    Window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"AsideLateFlyout", L"", WS_POPUP,
                             cursor.x, cursor.y, width, height, owner, NULL, instance, NULL);
    if (!Window) return;
    DWORD corners = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(Window, 33, &corners, sizeof corners);
    SetTimer(Window, 1, WATCH_MS, NULL);
}
