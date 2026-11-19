An alpha of the Aside Late window.

Download the exe and run it. Press **Patch Aside**. It takes seconds, downloads nothing and leaves a running Aside alone. Then press **Start Aside**, which asks before closing an Aside that's running.

Patches start off. Turn on the ones you want in Aside's **Settings → Patched stuff → Mods**. If you've used Aside Late before, your patches stay as you set them.

New in this alpha:

- Works with Aside 1.0.1116.2. Every patch finds what it needs again.
- Aside Late updates itself from now on. When a newer version is out, a card on a new tab and the top of **Mods** offer **Update**. It downloads in the background, is checked against the SHA-256 GitHub lists for it, and **Restart Aside** brings back your windows and tabs, patched by the new version. **Patched stuff → About → Updates** can make it download automatically or stop checking. This alpha is the first that can do this, so the next version is the first you'll be offered.
- Aside restarting itself, after its own update or a restart, now comes back patched.
- Some patches are split into smaller ones, so each does one thing. Your switches and options carry over:
  - **Close the fullscreen gap** and **Square corners beside the sidebar** are out of **Window tweaks**.
  - **Sidebar in fullscreen** is out of **Sidebar tweaks**.
- **Made with aside-late** in **About** opens this repo.

To undo everything, quit Aside and start it normally. To remove it, delete `%LOCALAPPDATA%\AsideLate`.

Known issues in this alpha:

- Windows SmartScreen may warn you, because the exe isn't signed.
- A Settings tab can close by itself shortly after opening. It's been seen most with Window tweaks collapsing the top bar, and the cause isn't known yet.
- Bookmark folder appearance changed the most for Aside 1.0.1116.2, and its right-click menu, colors and New folder haven't been tried by hand on that version yet.
