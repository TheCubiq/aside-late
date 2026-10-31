// A small color strip beside a folder's native right-click menu. It's a plain Win32 window
// that never takes focus, so the menu stays open and stays entirely Aside's own.
#pragma once

#include <windows.h>
#include <windowsx.h>

#define COLOR_COUNT 8

typedef void (*ColorPicked)(int color);

void FlyoutShow(HWND owner, POINT cursor, const DWORD *colors, int selected, ColorPicked picked);
