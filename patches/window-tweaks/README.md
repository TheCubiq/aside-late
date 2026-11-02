# Window tweaks

Closes the gap around a fullscreen window, collapses the top bar until it's hovered, sets the inset around the page, and
squares the page's corners beside the sidebar.

| File | What's in it |
| --- | --- |
| `patch.c` | What moves after each layout, the frame in fullscreen, and the patch's options |
| `resolve.c` | Finding the layout and what it does with corners |

## How it works

Aside lays the window out in `BrowserView::Layout`, so this hooks that and moves what it left, straight after it. The top
bar and the view holding the page are what move. Aside's own numbers are kept, so turning a tweak off is another layout.

A page told two sizes lays itself out for both and shows it, and that's a flicker, so the page must only ever hear the last one.
While Aside's layout runs, the view of pages and the top bar put off their own layouts until they've been moved. While a page is
clipped, what holds it takes no notice of any height but the clipped one, since its view sizes it as soon as its own size changes.

- **Close the gap around a fullscreen window** isn't about the layout at all. Windows 11 draws a thin border around a window
  and goes on drawing it around one that fills the screen, mostly see through, so the desktop shows along each edge. Rounded
  corners cut off the last few pixels the same way. A timer turns both off while a window is fullscreen, through
  `DWMWA_BORDER_COLOR` and `DWMWA_WINDOW_CORNER_PREFERENCE`, and puts them back when it isn't.
- **Collapse the top bar** hides the bar and the window's buttons until the cursor is on the top of the pane, and they come in
  over 180 ms. A menu or the address bar's suggestions keep it open. Hidden, they're above the window, where nothing shows them
  or can click them, however big the inset is. There are three ways in:
  - **Slide over the page**: the bar slides down, and the page doesn't move or reflow. Its top is cut off under the bar. What holds
    a page only shows what's inside its parent, and the page keeps its size, so a holder that starts above its parent has its
    top clipped.
  - **Push the page down**: the bar slides down and the page is resized.
  - **Uncover it, like Zen**: the bar stays where it is with no background, and the page's top is cut away in the same way to show
    it, while it fades in. It has no layer to fade, and a layer would put it over the page. So every class of view in the bar
    has its paint hooked to go through `saveLayerAlphaf`, and is repainted on each step.
- **The sidebar stays still** while a collapsed bar comes and goes. Its header grows to reach the bottom of the bar, which an inset
  takes past its usual height, so after its layout the header goes back to its height with the bar hidden, and what's under it
  and its buttons go with it.
- **The inset** takes the whole pane in on all four sides, like Dia: the top bar, the page, the window's buttons and the pane's
  shadow all move, and the page is a card with rounded corners. The window's frame paints the bar's background, tinted from the
  page, at the place Aside's layout has for the bar. So while the bar moves, the frame stops, and the bar has that paint done on
  itself, clipped to it and moved to where it is. Aside sizes the toolbar to end where the buttons were, so it's cut to end where
  they are. **Move the top bar with the inset** can be off, for a bar that stays along the top of the window with the inset
  under it. **Inset the side next to the sidebar** can be off too, so the page stays against the sidebar.
- **Square the corners beside the sidebar** is only there with no inset. It takes away the side the sidebar is on from what the layout stores in each page
  container and hands to one function. The top bar's are in the layout's own code, as a rounded rect built from an upper and a
  lower radius, so its two stores of the upper one are edited to store the zero beside them. That squares both of its upper
  corners, and Windows rounds the window's own.

## Limits

- The address bar can't be seen while the bar's collapsed, so Ctrl+L types into something hidden until the suggestions open or
  the cursor goes up there.
- The inset doesn't apply in fullscreen.
- Windows are told apart from the cursor by their size, so hovering the top of one also reveals the bar of another of exactly the
  same size.
- Whether the window's buttons really can't be clicked while they're hidden hasn't been tried with a real mouse.
