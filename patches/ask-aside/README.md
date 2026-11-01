# Hide Ask Aside

Hides Aside's own extension button from the toolbar.

## How it works

1. Find the toolbar button's update function: it loads Aside's extension ID, and has
   `xor dl,1` before a `SetVisible`.
2. Its constructor is the caller that also sets the field it reads, and that gives the class.
3. Hook `SetVisible` for the class, and hold the button whose object leads to Aside's extension ID.
