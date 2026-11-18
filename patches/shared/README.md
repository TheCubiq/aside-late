# Shared lookups

Not a patch. What several patches find in Aside the same way, kept here so a rule that stops matching a new build of Aside
is fixed once. A patch includes what it needs as `"shared/browser.h"`, from one of its `.c` files.

| File | What it finds |
| --- | --- |
| `browser.h` | `BrowserView`: its vtable, its `Layout`, and how views lay out |
| `canvas.h` | How a view paints: its `OnPaint` slot, the canvas it's handed, and `Canvas::ClipRect` |
| `corners.h` | What the browser's layout does with the corners of the page and the top bar |

Each one keeps what it found with `LookupRemember`, under a name like `browser/1`, so the next patch to ask gets it with
`LookupRecall` instead of reading Aside again. Change the number in the name when the rule or what's kept changes.

Installing a patch from GitHub fetches this folder too, from the same repo, into its source as `shared`, and a change here
counts as a change to every patch that includes it, so each of them needs its version raised.

It belongs to this repo. A patch in another repo only gets that repo's own `patches/shared`, if it has one, so to use these it
copies them there. A copy that's left as it is keeps the same names, and then whichever patch runs first does the work for the
others too. A copy that's changed needs its names changed with it, or it could be handed what the original found.
