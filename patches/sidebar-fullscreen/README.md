# Sidebar in fullscreen

Reveals a collapsed sidebar from the window's edge in a fullscreen window too, where Aside never shows it.

| File | What's in it |
| --- | --- |
| `patch.c` | Watching the cursor, and sliding the sidebar in |
| `resolve.c` | Finding where the browser keeps the sidebar. The browser itself is in `shared/browser.h` |

## How it works

Aside's hover controller isn't told anything in a fullscreen window, and only resets if it's answered for, so this doesn't go
through it. The layout zeroes and hides the sidebar's view there, so straight after it this slides the view in, as a card set
out like Aside's own hover. Showing a view tells its parent, which lays out again and hides it, so for that long it has no
parent. It goes by the cursor, with its own edge width and delays.

Outside fullscreen, each layout notes how wide the sidebar is and which side it's on, for when it's revealed.

It used to be the **Reveal in fullscreen** switch of Sidebar tweaks. The first time it's there, it's on if that switch was,
with the same edge width and delays.

## Limits

- **The sidebar has no backdrop**, so it's only easy to read over a dark page. Its view has no layer of its own there, so what
  it paints lands under the page, and only its rows, which do have layers, show. Aside's own hover creates a layer. This needs
  `View::SetPaintToLayer` found by shape, or the layout's fullscreen gate found and answered the way the old early patcher did
  for build 1.0.928.2.
- A window that has never been laid out since the patch was switched on isn't known yet. Any resize or going fullscreen does it.
