// A small panel beside a folder's native right-click menu. It's a plain Win32 window that
// never takes focus, so the menu stays open and stays entirely Aside's own.
#pragma once

#include <windows.h>
#include <windowsx.h>

#define COLOR_COUNT 8

typedef struct {
    const DWORD *colors;
    const wchar_t **iconNames;  // Each name is NUL-free: see iconNameSizes.
    const size_t *iconNameSizes;
    int iconCount;
    int color, icon;            // Current choices, or -1.
    void (*pickColor)(int color);
    void (*pickIcon)(int icon);
    void (*reset)(void);
} FlyoutChoices;

// Aside opens its menu on button down, so this has to run before that.
void FlyoutBeforeMenu(void);
void FlyoutShow(HWND owner, POINT cursor, const FlyoutChoices *choices);
