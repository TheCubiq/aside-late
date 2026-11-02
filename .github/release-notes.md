An alpha of the Aside Late window.

Download the exe and run it. Press **Patch and start Aside**. It takes seconds and downloads nothing, then asks before closing an Aside that's running.

Patches now start off. Turn on the ones you want in Aside's **Settings → Patched stuff → Mods**. If you've used Aside Late before, your patches stay as you set them.

New in this alpha:

- No compiler for normal use. Everything comes built inside the exe.
- **Build from source instead** is there if you'd rather compile it yourself. That downloads a compiler, 69 MB and about 740 MB on disk.
- Patches have versions. Mods offers **Update** only when a patch's version rises, for official patches too.
- Installing or updating a patch from GitHub compiles it, so the first time downloads the same compiler. Mods says so before you press the button.
- Unpacking the compiler no longer opens a window.

To undo everything, quit Aside and start it normally. To remove it, delete `%LOCALAPPDATA%\AsideLate`.

Known issues in this alpha:

- Windows SmartScreen may warn you, because the exe isn't signed.
- A Settings tab can close by itself shortly after opening. It's been seen most with Window tweaks collapsing the top bar, and the cause isn't known yet.
- The fullscreen sidebar reveal in Sidebar tweaks has no backdrop.
