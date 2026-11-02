# Sidebar tweaks

Keeps a collapsed sidebar open while its menus are, reveals it from the window's edge, and sets how
quickly it opens and closes.

| File | What's in it |
| --- | --- |
| `patch.c` | What the controller is told, and the patch's options |
| `resolve.c` | Finding the controller in Aside |

## How it works

Aside's `aside_sidebar_hover_controller.cc` is a state machine. It asks a callback for the cursor and some
rects, and works out from those whether a collapsed sidebar should show. The callback is an object holding
a method, so looking for that method's address finds every window's, and swapping it changes the answers.
Nothing here shows or hides anything.

- **Stay open while a menu is open** holds the cursor over the panel while the browser's window owns another
  window, which covers menus and their submenus. A menu also takes the focus, so the answer is what it was
  when the window last had it. A browser losing focus hides the sidebar outright as well. That jump is
  edited out, and the sidebar still closes on losing focus, because the controller is then told nothing.
- **Reveal from the window's edge** answers that the cursor is over the button. Aside has its own strip at the
  window's edge, but a maximized window starts past the edge of the screen, so that's where the strip is too.
  This one starts where the window can be seen, and keeps the cursor out of Aside's.
- **Reveal the sidebar in fullscreen** doesn't go through the controller, which isn't told anything in a fullscreen
  window and only resets if it's answered for. The layout zeroes and hides the sidebar's view there, so straight after it
  this slides the view in, as a card set out like Aside's own hover. Showing a view tells its parent, which lays out again
  and hides it, so for that long it has no parent. It goes by the cursor, with the same width and delays.
- **Delay on enter** holds that answer back. Asking only happens as the mouse moves, so a timer asks too.
- **Delay on leave** moves the time the controller started closing from, so its own comparison comes out right.

## Limits

- **In fullscreen the sidebar has no backdrop**, so it's only easy to read over a dark page, which is why it's off by default.
  Its view has no layer of its own there, so what it paints lands under the page, and only its rows, which do have layers,
  show. Aside's own hover creates a layer. This needs `View::SetPaintToLayer` found by shape, or the layout's fullscreen gate
  found and answered the way the old early patcher did for build 1.0.928.2.
