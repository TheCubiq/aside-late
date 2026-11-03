An alpha of the Aside Late window.

Download the exe and run it. Press **Patch Aside**. It takes seconds, downloads nothing and leaves a running Aside alone. Then press **Start Aside**, which asks before closing an Aside that's running.

Patches start off. Turn on the ones you want in Aside's **Settings → Patched stuff → Mods**. If you've used Aside Late before, your patches stay as you set them.

New in this alpha:

- Fixed: logged out of X, the hand didn't find the Follow button. It does now, and X's cookie and log-in bars stay out of its way while it points, then come back.
- **Add to desktop** and **Add to Start menu** make a shortcut that starts Aside patched, with no window of its own. Aside started that way shows on the taskbar as **Aside (patched)**, so a pin there starts it patched too.
- Opened again once Aside is patched, the window offers **Patch again** and **Build from source** as well.
- New patch, **Window glass**: the window turns frosted glass, blurring what's behind it, with a **Background opacity** slider like Windows Terminal's.
- The Aside Late window itself is frosted glass now.
- The first patched start opens a thank-you tab, once.

To undo everything, quit Aside and start it normally. To remove it, delete `%LOCALAPPDATA%\AsideLate`.

Known issues in this alpha:

- Windows SmartScreen may warn you, because the exe isn't signed.
- A Settings tab can close by itself shortly after opening. It's been seen most with Window tweaks collapsing the top bar, and the cause isn't known yet.
- The fullscreen sidebar reveal in Sidebar tweaks has no backdrop.
