# Square corners beside the sidebar

Squares the page's corners where it meets the sidebar, and the top bar's upper corners. Windows rounds the window's own.

| File | What's in it |
| --- | --- |
| `patch.c` | All of it. What it finds in Aside is in `shared/browser.h` and `shared/corners.h` |

## How it works

`BrowserView::Layout` hands each page container its corners, all round. Straight after it, this takes away the corners on
the sidebar's side and applies them again. The top bar's corners are in the layout's own code, as a rounded rect built from an
upper and a lower radius, so its two stores of the upper one are edited to store the zero beside them. That edit only takes
effect as the layout runs, so changing it lays the window out once more.

Only a page that touches one side of the window has a sidebar side. An inset from Window tweaks moves the page off both, and
then nothing here changes. Window tweaks keeps the squared side when it rounds a page whose top bar is collapsed.

It used to be the **Square the corners beside the sidebar** switch of Window tweaks. The first time it's there, it's on if
that switch was.
