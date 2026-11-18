# Close the fullscreen gap

Takes away the gap around a fullscreen window.

| File | What's in it |
| --- | --- |
| `patch.c` | All of it |

## How it works

Windows 11 draws a thin border around a window, and goes on drawing it around one that fills the screen. It's mostly see
through, so the desktop shows along each edge. Rounded corners cut off the last few pixels the same way. A timer turns both off
while a window is fullscreen, through `DWMWA_BORDER_COLOR` and `DWMWA_WINDOW_CORNER_PREFERENCE`, and puts them back when it
isn't.

It finds nothing in Aside, so a new build of Aside can't break it.

It used to be the **Close the gap around a fullscreen window** switch of Window tweaks. The first time it's there, it's on if
that switch was.
