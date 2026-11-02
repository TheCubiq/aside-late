The first alpha of the Aside Late window.

Download the exe and run it. Press **Patch and start Aside**. The first run downloads the compiler (about 69 MB, checked against a pinned hash), builds the patches on your machine, then asks before closing an Aside that's running.

Afterwards, switch patches on and off in Aside's **Settings → Patched stuff → Mods**.

To undo everything, quit Aside and start it normally. To remove it, delete `%LOCALAPPDATA%\AsideLate`.

Known issues in this alpha:

- Windows SmartScreen may warn you, because the exe isn't signed.
- A Settings tab opened from the New Tab page can close by itself shortly after opening when Window tweaks has the top bar collapsed.
- The fullscreen sidebar reveal in Sidebar tweaks has no backdrop.
