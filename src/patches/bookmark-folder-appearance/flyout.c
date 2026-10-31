#include "flyout.h"

#include <dwmapi.h>
#include <gdiplus.h>

#define CELL 28
#define DOT 18
#define PADDING 8
#define GAP 6
#define BUTTON_HEIGHT 28
#define ICON_COLUMNS 4
#define ICON_WIDTH 92
#define ICON_HEIGHT 26
#define WATCH_MS 40
#define MAX_KNOWN 64
#define WAIT_TICKS 50

// Cell 0 clears the color; the rest are the palette.
#define CELL_COUNT (COLOR_COUNT + 1)

enum { ITEM_NONE = -1, ITEM_COLORS = 0, ITEM_CHANGE_ICON = 100, ITEM_RESET = 101, ITEM_ICONS = 200 };

static HWND Window, Menu;
static FlyoutChoices Choices;
static BOOL Expanded;
static int Hovered = ITEM_NONE, WaitTicks;
static double Scale;
static RECT Anchor;
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

static int ContentWidth(void) {
    int strip = CELL * CELL_COUNT, grid = ICON_WIDTH * ICON_COLUMNS;
    return Expanded && grid > strip ? grid : strip;
}

static int IconRows(void) {
    return (Choices.iconCount + ICON_COLUMNS - 1) / ICON_COLUMNS;
}

static SIZE WindowSize(void) {
    int height = PADDING * 2 + CELL + BUTTON_HEIGHT;
    if (Expanded) height += GAP + IconRows() * ICON_HEIGHT;
    return (SIZE){Px(PADDING * 2 + ContentWidth()), Px(height)};
}

static RECT ButtonRect(int item) {
    int half = ContentWidth() / 2;
    int left = PADDING + (item == ITEM_RESET ? half : 0), top = PADDING + CELL;
    return (RECT){Px(left), Px(top), Px(left + half), Px(top + BUTTON_HEIGHT)};
}

static RECT IconRect(int icon) {
    int left = PADDING + icon % ICON_COLUMNS * ICON_WIDTH, top = PADDING + CELL + BUTTON_HEIGHT + GAP + icon / ICON_COLUMNS * ICON_HEIGHT;
    return (RECT){Px(left), Px(top), Px(left + ICON_WIDTH), Px(top + ICON_HEIGHT)};
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

static void FillRounded(GpGraphics *graphics, const RECT *rect, ARGB color) {
    GpPath *path;
    GpSolidFill *brush;
    float x = (float)rect->left + 1, y = (float)rect->top + 1, w = (float)(rect->right - rect->left - 2), h = (float)(rect->bottom - rect->top - 2), r = (float)Px(6);
    GdipCreatePath(FillModeAlternate, &path);
    GdipAddPathArc(path, x, y, r, r, 180, 90);
    GdipAddPathArc(path, x + w - r, y, r, r, 270, 90);
    GdipAddPathArc(path, x + w - r, y + h - r, r, r, 0, 90);
    GdipAddPathArc(path, x, y + h - r, r, r, 90, 90);
    GdipClosePathFigure(path);
    GdipCreateSolidFill(color, &brush);
    GdipFillPath(graphics, (GpBrush *)brush, path);
    GdipDeleteBrush((GpBrush *)brush);
    GdipDeletePath(path);
}

static void Label(HDC dc, const wchar_t *text, int size, RECT rect, COLORREF color) {
    SetTextColor(dc, color);
    DrawTextW(dc, text, size, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
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

    ARGB ink = light ? 0xFF5F6368 : 0xFF9AA0A6, hover = light ? 0x14000000 : 0x1AFFFFFF, chosen = light ? 0x29000000 : 0x33FFFFFF;
    Dot(graphics, 0, ink, FALSE);
    GpPen *pen;
    GdipCreatePen1(ink, (float)Px(1) * 1.5f, UnitPixel, &pen);
    GdipDrawLine(graphics, pen, (float)Px(PADDING + (CELL - DOT) / 2 + 4), (float)Px(PADDING + (CELL + DOT) / 2 - 4),
                 (float)Px(PADDING + (CELL + DOT) / 2 - 4), (float)Px(PADDING + (CELL - DOT) / 2 + 4));
    GdipDeletePen(pen);
    for (int i = 0; i < COLOR_COUNT; i++) Dot(graphics, i + 1, 0xFF000000 | Choices.colors[i], TRUE);

    ARGB ring = light ? 0xFF202124 : 0xFFE8EAED;
    Ring(graphics, Choices.color + 1, ring);
    if (Hovered >= ITEM_COLORS && Hovered < CELL_COUNT && Hovered != Choices.color + 1) Ring(graphics, Hovered, (ring & 0x00FFFFFF) | 0x66000000);

    if (Hovered == ITEM_CHANGE_ICON || Expanded) {
        RECT rect = ButtonRect(ITEM_CHANGE_ICON);
        FillRounded(graphics, &rect, Expanded ? chosen : hover);
    }
    if (Hovered == ITEM_RESET) {
        RECT rect = ButtonRect(ITEM_RESET);
        FillRounded(graphics, &rect, hover);
    }
    if (Expanded) {
        if (Choices.icon >= 0) {
            RECT rect = IconRect(Choices.icon);
            FillRounded(graphics, &rect, chosen);
        }
        if (Hovered >= ITEM_ICONS && Hovered - ITEM_ICONS != Choices.icon) {
            RECT rect = IconRect(Hovered - ITEM_ICONS);
            FillRounded(graphics, &rect, hover);
        }
    }
    GdipDeleteGraphics(graphics);

    HFONT font = CreateFontW(-Px(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(memory, font);
    SetBkMode(memory, TRANSPARENT);
    COLORREF text = light ? RGB(32, 33, 36) : RGB(232, 234, 237);
    Label(memory, L"Change icon", -1, ButtonRect(ITEM_CHANGE_ICON), text);
    Label(memory, L"Reset", -1, ButtonRect(ITEM_RESET), text);
    if (Expanded)
        for (int i = 0; i < Choices.iconCount; i++)
            Label(memory, Choices.iconNames[i], (int)Choices.iconNameSizes[i], IconRect(i), text);
    SelectObject(memory, oldFont);
    DeleteObject(font);

    BitBlt(dc, 0, 0, width, height, memory, 0, 0, SRCCOPY);
    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
}

static int ItemAt(LPARAM position) {
    POINT point = {GET_X_LPARAM(position), GET_Y_LPARAM(position)};
    if (point.y >= Px(PADDING) && point.y < Px(PADDING + CELL) && point.x >= Px(PADDING)) {
        int cell = (point.x - Px(PADDING)) / Px(CELL);
        return cell < CELL_COUNT ? ITEM_COLORS + cell : ITEM_NONE;
    }
    RECT rect = ButtonRect(ITEM_CHANGE_ICON);
    if (PtInRect(&rect, point)) return ITEM_CHANGE_ICON;
    rect = ButtonRect(ITEM_RESET);
    if (PtInRect(&rect, point)) return ITEM_RESET;
    for (int i = 0; Expanded && i < Choices.iconCount; i++) {
        rect = IconRect(i);
        if (PtInRect(&rect, point)) return ITEM_ICONS + i;
    }
    return ITEM_NONE;
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

static void Place(void) {
    SIZE size = WindowSize();
    MONITORINFO monitor = {.cbSize = sizeof monitor};
    GetMonitorInfoW(MonitorFromRect(&Anchor, MONITOR_DEFAULTTONEAREST), &monitor);
    int x = Anchor.left, y = Anchor.top - size.cy - Px(GAP);
    if (y < monitor.rcWork.top) y = Anchor.bottom + Px(GAP);
    if (x + size.cx > monitor.rcWork.right) x = monitor.rcWork.right - size.cx;
    SetWindowPos(Window, HWND_TOPMOST, x, y, size.cx, size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

// The panel appears once Aside's menu does and leaves with it.
static void Watch(void) {
    if (!Menu) {
        HWND found = NULL;
        EnumWindows(FindNewPopup, (LPARAM)&found);
        if (found) {
            GetWindowRect(found, &Anchor);
            Menu = found;
            Place();
            return;
        }
        if (--WaitTicks <= 0) Close();
        return;
    }
    if (!IsWindow(Menu) || !IsWindowVisible(Menu)) Close();
}

static void Click(int item) {
    if (item == ITEM_NONE) return;
    if (item == ITEM_CHANGE_ICON) {
        Expanded = !Expanded;
        Place();
    } else if (item == ITEM_RESET) {
        Choices.color = Choices.icon = -1;
        Choices.reset();
    } else if (item >= ITEM_ICONS) {
        Choices.icon = item - ITEM_ICONS;
        Choices.pickIcon(Choices.icon);
    } else {
        Choices.color = item - ITEM_COLORS - 1;
        Choices.pickColor(Choices.color);
    }
    InvalidateRect(Window, NULL, FALSE);
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
        int item = ItemAt(lParam);
        if (item != Hovered) {
            Hovered = item;
            InvalidateRect(window, NULL, FALSE);
            TRACKMOUSEEVENT track = {sizeof track, TME_LEAVE, window, 0};
            TrackMouseEvent(&track);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        Hovered = ITEM_NONE;
        InvalidateRect(window, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        Click(ItemAt(lParam));
        return 0;
    case WM_NCDESTROY:
        if (Window == window) Window = NULL;
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void FlyoutBeforeMenu(void) {
    KnownCount = 0;
    EnumWindows(Snapshot, 0);
}

void FlyoutShow(HWND owner, POINT cursor, const FlyoutChoices *choices) {
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
    Choices = *choices;
    Expanded = FALSE;
    Hovered = ITEM_NONE;
    Scale = GetDpiForWindow(owner) / 96.0;
    WaitTicks = WAIT_TICKS;

    SIZE size = WindowSize();
    Window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"AsideLateFlyout", L"", WS_POPUP,
                             cursor.x, cursor.y, size.cx, size.cy, owner, NULL, instance, NULL);
    if (!Window) return;
    DWORD corners = 2;  // DWMWCP_ROUND
    DwmSetWindowAttribute(Window, 33, &corners, sizeof corners);
    SetTimer(Window, 1, WATCH_MS, NULL);
}
