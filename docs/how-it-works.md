# How it works

Late-bound patches: they change no file of Aside's on disk and store no offsets. Every launch, each patch finds what it
needs in whatever build is loaded, and it can be switched on and off, or installed, while the browser runs. That's unlike
`aside/patcher`, which binds early: exact file offsets, pinned to one SHA-256 of `Aside.dll`.

1. `aside-late.exe` starts `Aside.exe` as a debugger, waits for the moment `Aside.dll` is
   mapped, has it load `aside-late-mod.dll`, and lets go.
2. The mod finds the patches next to itself and in its home folder. For those switched on, it
   loads the DLL, runs its start against a clean copy of `Aside.dll` read from disk, and enables it.
3. After that, the settings page asks for changes through paths that don't exist on disk. One
   worker thread does what takes time, and hands anything touching the browser to the UI thread.

## Installing a patch

The page reads a repo with GitHub's API. Install asks the mod to fetch `patches/<id>/` at that
commit into `patches\<id>\source` in its home folder, and compile it against the headers in
`build\sdk` and the mod's DLL. The compiler is [w64devkit](https://github.com/skeeto/w64devkit),
downloaded into the home folder the first time one is needed. `ASIDE_LATE_GCC` points at another.

New source stays in `source.new` until it has compiled, so a failed update leaves what you had.
An update loads the new DLL beside the old one, whose hooks are off. A loaded DLL can't safely be
unloaded, because Aside could be running its code, so the old file goes on the next launch.

## Crashes

The mod notes each risky moment in `session.txt`, and watches for faults with a patch on the stack.
The next launch reads what's left behind:

- **A fault in a patch** turns that patch off, and settings says why.
- **A crash within 5 seconds of turning a patch on** does the same.
- **A crash within 30 seconds of starting** with nothing to blame runs that one launch with no
  patches. Force-quitting that soon looks the same, except through `aside-late.exe`, which knows.

## The settings page

Built into the mod, because it's how everything else is reached. Aside's settings are an
extension page whose files its browser process reads, so:

1. Watch `Aside.dll`'s imports of `CreateFileW`, `GetFileAttributes*` and
   `GetFinalPathNameByHandleW`, and only act on paths inside the `AsideAgentManager`
   extension. Aside asks an open file where it really is, so a redirected file answers with
   the path it was opened as.
2. Serve `main.html` from a copy with one extra `<script>` tag. The copy is made from
   whatever that version of the extension ships.
3. Serve `/aside-late/` from `web` in the home folder. Aside can have a file open at any
   moment, and then it can't be replaced, so the files that change are written new each time
   and `patches.json` leads to the newest.
4. The script draws into the live page, copying a native navigation item and reusing the
   page's CSS classes. It uses none of the page's code, so it needs no per-build names.
5. The switch is Aside's own rebuilt: the same parts, classes, spring and glass as its component.
   The glass picture comes from a native switch on the page, and without one a press looks plainer.
   The popup, accordion and select are rebuilt the same way. Aside has no slider, so that one
   takes its colors and thumb from the switch.

## Updates

The page asks GitHub for releases at most every 6 hours, and the mod downloads `AsideLate-<version>.exe` from the newest one,
checks it against the SHA-256 GitHub lists for it, and puts it in place of `Aside Late.exe` in the home folder. **Restart
Aside** calls Aside's own restart, found by the settings it writes. Aside starting itself again, for that or after its own
update, is a command line of its own exe with no `--type`, and the mod's hook on `CreateProcessW` sends it through Aside Late:
the new copy with `--relaunch <pid>` if one is waiting, the launcher with `--after <pid>` otherwise. Either waits for the old
Aside to exit, then starts it patched.

## Limits

This is later binding than offsets, not real name lookup like Dia's Objective-C hooks:

- C++ keeps few names, and many classes are only reachable through code. A rebuild can change
  that code's shape without the feature changing, and then a patch refuses until its rule is updated.
- It runs on every launch, and opening Aside any other way gives you the normal browser. That
  includes opening a link while it's closed. Aside restarting itself, after its own update or Aside Late's, goes through
  Aside Late and comes back patched.
- Patches found on startup add about 3 s to it. Turning one on later takes about the same, off
  the UI thread, the first time.
- Windows swallows an exception in a timer callback, and a `CHECK` in Aside is one. A patch that calls Aside
  from its timer can fail there with nothing in `mod.log` and no crash.

## Build

Needs MinGW-w64 `gcc`:

```powershell
make
```

This works while a patched Aside is running. That Aside keeps what it has loaded, and the next
launch uses the new build.

The window is in `src/front`, in Rust with [GPUI Kit](https://gpui-kit.com):

```powershell
cd src/front
cargo build --release
```

This runs `make` as well, so it needs `make` and `gcc` too, and packs what that builds into the exe.
Its options are `--launch`, to start straight away and close when done, and `--exe <Aside.exe>`. Anything else goes to Aside.
A release comes from running the **Release** workflow by hand, and is named after the version in `src/front/Cargo.toml`.

## From the command line

After [building](#build), run:

```powershell
.\build\aside-late.exe
```

If Aside is already running on that profile, it asks whether to terminate it first,
including one in the background with no windows open. Answer no and it leaves Aside alone
and opens nothing. Terminating is a force-quit, so Aside may offer to restore your tabs.

Then open **Settings → Patched stuff → Mods**. Switches apply straight away, in the browser
that's running. **Get more** takes a GitHub repo as `owner/name`, lists its patches, and
installs one without a restart.

| Option | What it does |
| --- | --- |
| `--list` | List the patches |
| `--without <id>` | Leave a patch out of this launch. Repeat it for more. `settings` removes the settings page |
| `--exe <Aside.exe>` | Use another installation |
| `--after <pid>` | Wait for that process to exit first. The mod passes it when Aside restarts itself |
| `--scan <Aside.dll> ...` | Show whether each patch finds what it needs, without running anything |
| `--shortcut [path]` | Make a shortcut to `aside-late.exe`, on the desktop unless a path is given. `--to`, `--arguments`, `--icon` and `--app-id` change what it starts, its icon and its taskbar ID |

Anything else goes to Aside, such as `--user-data-dir=<path>`.

To undo everything, quit the patched Aside and start it normally. Closing its windows may
not be enough, because Aside can keep running in the background:

```powershell
Stop-Process -Name Aside
```

Everything aside-late keeps is in `%LOCALAPPDATA%\AsideLate`, including `mod.log`.
