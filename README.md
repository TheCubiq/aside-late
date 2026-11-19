# aside-late

Small patches for [Aside](https://aside.com) on Windows that keep working when Aside updates.

Unofficial and not affiliated with Aside. Windows x64.

## Patches

Each patch changes one thing about Aside: it hides something you don't use, changes how something looks, or adds something
Aside doesn't do.

| Patch | What it changes |
|---|---|
| [Hide Ask Aside](patches/ask-aside/README.md) | Hides the Ask Aside toolbar button |
| [Hide Chats](patches/chats/README.md) | Hides the Chats section of the sidebar |
| [Bookmark folder appearance](patches/bookmark-folder-appearance/README.md) | Colors and icons for bookmark folders, from a right-click menu like the tab group one |
| [Sidebar tweaks](patches/sidebar-tweaks/README.md) | Keeps a collapsed sidebar open while its menus are, reveals it from the window's edge, and sets how quickly it opens and closes |
| [Sidebar in fullscreen](patches/sidebar-fullscreen/README.md) | Reveals a collapsed sidebar from the window's edge in fullscreen too |
| [Window tweaks](patches/window-tweaks/README.md) | Collapses the top bar until it's hovered, and sets the inset around the page |
| [Square corners beside the sidebar](patches/square-corners/README.md) | Squares the page's corners where it meets the sidebar |
| [Close the fullscreen gap](patches/fullscreen-gap/README.md) | Takes away the see-through border Windows leaves around a fullscreen window |
| [Window glass](patches/window-glass/README.md) | Makes the window frosted glass, with a background opacity slider like Windows Terminal's |

Patches start off. Turn them on and off in **Settings → Patched stuff → Mods**, where they change straight away. **Get more**
there installs patches from any GitHub repo with a `patches` folder.

## Install

1. Download `AsideLate-<version>.exe` from [Releases](https://github.com/TheCubiq/aside-late/releases) and run it.
2. Press **Patch Aside**. It takes seconds and leaves a running Aside alone.
3. Press **Start Aside**. It asks before closing an Aside that's running.

**Add to desktop** and **Add to Start menu** make a shortcut that starts Aside patched. Aside started that way shows on the
taskbar as **Aside (patched)**, so pinning it there gives you a pin that starts it patched too.

Windows SmartScreen may warn you, because the exe isn't signed.

## Updating

When a newer Aside Late is out, a card on a new tab and the top of **Mods** offer **Update**. It downloads in the background,
and **Restart Aside** brings back your windows and tabs, patched by the new version. Otherwise it's used the next time Aside
starts from a shortcut or pin.

Under **Patched stuff → About → Updates**, pick: ask first (the default), download automatically, or don't check.

When Aside updates itself, the patches find what they need in the new build on their own. A patch that can't turns itself off
and says so in **Mods** until it's fixed here, and the others carry on.

## FAQ

<details>
<summary><strong>Is it safe?</strong></summary>

Nothing of Aside's on disk changes. The patches run inside Aside only while it's started through Aside Late, and only those you
turn on. The exe is built by this repo's GitHub Actions from the code here, and updates are checked against the SHA-256 GitHub
lists for them. It goes online only to check for updates, and to read a GitHub repo you install patches from. It sends nothing
about you anywhere.

</details>

<details>
<summary><strong>What are the tradeoffs?</strong></summary>

- Patches run inside the browser with no limits, like any code Aside runs. Install patches only from repos you trust.
- The exe and its updates aren't signed. They're checked against the digest GitHub lists, so they're as trustworthy as this repo.
- Aside Late's files are in your user folder, so other software running as you can change what the patched Aside loads.

</details>

<details>
<summary><strong>How does it work?</strong></summary>

Aside Late starts Aside and loads a small add-on into it as it starts. Each patch then finds what it needs in the Aside that's
running, by what stays the same between builds, not by fixed offsets. [How it works →](docs/how-it-works.md)

</details>

<details>
<summary><strong>Does it run in the background?</strong></summary>

No. It only does anything while the patched Aside is open.

</details>

<details>
<summary><strong>Why is Aside sometimes not patched?</strong></summary>

Patches load only when Aside starts through Aside Late: its shortcut, its pin or **Start Aside**. Aside started any other way,
such as by opening a link while it's closed, is the normal browser. Aside restarting itself comes back patched.

</details>

<details>
<summary><strong>What if Aside keeps crashing?</strong></summary>

A patch that crashes Aside, or crashes it right after being turned on, turns itself off, and **Mods** says why. If Aside crashes
soon after starting with nothing to blame, the next start has the patches off, and **Mods** lets you turn them back on.
`%LOCALAPPDATA%\AsideLate\mod.log` says what happened.

</details>

<details>
<summary><strong>Can I build it myself?</strong></summary>

Yes. **Build from source** in the window compiles everything on your machine. It downloads a 69 MB compiler, checked against a
pinned hash, and takes about a minute after that. To build from a clone instead, see [Build](docs/how-it-works.md#build).

</details>

## Uninstall

To undo everything, quit Aside and start it normally. Closing its windows may not be enough, because Aside can keep running in
the background.

To remove it, delete `%LOCALAPPDATA%\AsideLate`, and the **Aside (patched)** shortcuts and pins.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). [Writing patches](docs/writing-patches.md) covers a patch's files, the SDK and options,
and [How it works](docs/how-it-works.md) what runs where.
