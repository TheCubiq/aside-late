# aside-late

Late-bound patches for Aside on Windows x64. They change no file of Aside's on disk and store
no offsets. Every launch, each patch finds what it needs in whatever build is loaded, and it
can be switched on and off, or installed, while the browser runs.

`aside/patcher` binds early: exact file offsets, pinned to one SHA-256 of `Aside.dll`.

## Use

Download `AsideLate-<version>.exe` from [Releases](https://github.com/TheCubiq/aside-late/releases) and run it. It's about
9 MB, and holds everything below already compiled, as well as its source. Press **Patch Aside**. That takes seconds, downloads
nothing and leaves a running Aside alone. Then press **Start Aside**, which asks before closing an Aside that's running.
**Add to desktop** and **Add to Start menu** make a shortcut that starts Aside patched with no window of its own. If Aside is
already running with the patches, the shortcut opens a new window in it.

Aside started by that shortcut or by **Start Aside** shows on the taskbar as **Aside (patched)**, apart from plain Aside. Pin it
from there and the pin starts it patched. Started any other way, Aside groups under its own icon as before.

The shortcuts and pins start a copy of the program kept in `%LOCALAPPDATA%\AsideLate`. Patching with a newer version replaces
that copy, so existing shortcuts and pins stay as they are.

Opened again once Aside is patched, the window offers the same buttons, plus **Patch again** and **Build from source**. Both
close a running Aside first, after asking.

Patches start off. Turn on the ones you want in **Settings → Patched stuff → Mods**.

The first start with the patches also opens a thank-you tab, once. It never comes back, and **nah, maybe later** or Esc closes
it. Its hand follows a click on **Star** or **Follow** to GitHub or X and points at the real button there for a few seconds, and
once both are clicked it waves goodbye. Delete `said-hello.txt` in `%LOCALAPPDATA%\AsideLate` to see it again.

**Build from source instead** compiles it all on your machine. That downloads a 69 MB compiler, checked against a pinned hash and
about 740 MB once unpacked, and takes about a minute after that. The same compiler is downloaded the first time you install a
patch from a GitHub repo, or update one, so you never need it otherwise.

Windows may warn about the download, since it isn't signed.

### From the command line

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
| `--scan <Aside.dll> ...` | Show whether each patch finds what it needs, without running anything |
| `--shortcut [path]` | Make a shortcut to `aside-late.exe`, on the desktop unless a path is given. `--to`, `--arguments`, `--icon` and `--app-id` change what it starts, its icon and its taskbar ID |

Anything else goes to Aside, such as `--user-data-dir=<path>`.

To undo everything, quit the patched Aside and start it normally. Closing its windows may
not be enough, because Aside can keep running in the background:

```powershell
Stop-Process -Name Aside
```

Everything aside-late keeps is in `%LOCALAPPDATA%\AsideLate`, including `mod.log`.

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

## Patches

A patch is a folder in `patches/`, here or in any GitHub repo. The ones here come compiled inside
the exe. One from anywhere else is delivered as source, and compiled on your machine when you install it.

```text
patches/hide-something/
  patch.json    id, title, description, version, sdk, the builds it has been run on
  patch.c       any number of .c files, in folders too
  README.md     what it does and how it works
```

A patch's files include each other from its top folder, like `#include "resolve/resolve.h"`.

Each patch here explains itself:

- [bookmark-folder-appearance](patches/bookmark-folder-appearance/README.md)
- [sidebar-tweaks](patches/sidebar-tweaks/README.md)
- [window-tweaks](patches/window-tweaks/README.md)
- [window-glass](patches/window-glass/README.md)
- [chats](patches/chats/README.md)
- [ask-aside](patches/ask-aside/README.md)

Add `"restart": true` to `patch.json` if a patch can only work from startup. Settings then
says it applies the next time Aside starts.

### Versions

```json
{ "id": "hide-something", "version": "1.2.0", "sdk": 1 }
```

`version` is `x.y.z`, compared part by part. Mods offers **Update** when the version on the repo's default branch is higher than
yours, so raise it when a change is ready, and until then nothing reaches anyone. Here, a check fails if a patch changes without
that. A patch with no version is compared by commit instead, so any new commit in its repo offers an update.

`sdk` is the `ASIDE_LATE_SDK` the patch was written for, from `aside-late.h`. That number goes up only when a change could break
existing patches. If it differs from your Aside Late, Mods warns and offers **Install anyway** or **Update anyway**, since compiling
catches most mismatches.

Official patches update the same way, from this repo. If what you've compiled and what came with the exe differ, the higher version
runs. **Use bundled version** goes back to the exe's copy. A patch that was compiled against another SDK turns off after you update
Aside Late, and **Rebuild** compiles it again from the source it kept.

A patch includes `aside-late.h` and exports up to three functions:

```c
#include "aside-late.h"

// Off the UI thread, once. Finds what it needs in a clean copy of Aside.dll.
PATCH_START() {
    DWORD vtable = LookupClassVtable(lookup, "SomeView");
    if (!vtable) return "SomeView";       // Refuses. The other patches are unaffected.
    SomeView = (void **)(base + vtable);
    return NULL;
}

// On the UI thread. Turns it on, including for what's already on screen.
PATCH_ENABLE() {
    HookVtable(SomeView, slot, OnPaint, (void **)&OriginalPaint);
}

// On the UI thread, after its hooks and code edits have been taken off for it.
PATCH_DISABLE() {
}
```

Patches run inside the browser with no limits. What the SDK adds:

| | |
| --- | --- |
| `HookVtable` | Replace a method on every object of a class, including ones that already exist |
| `HookImport` | Replace what a module calls when it uses an import |
| `EditCode` | Write over Aside's code, if it holds the bytes expected. Editing it back takes the edit away |
| `FindObjects` | Find the live objects of a class, by looking for its vtable in the heap |
| `RunOnUiThread` | Run something on the browser's UI thread |
| `Lookup*` | Find classes, strings, functions, method slots and how views hide, by what stays the same |
| `hold.h` | Keep a view hidden, remember what Aside asked for, and hand that back when turned off |

Hooks stack, so patches can share a method and come off in any order. Two patches can't edit
the same bytes: the second one's edit is refused.

Prefer classes and methods to code patterns. They're what survives a rebuild of Aside.

### Options

A patch that lists `options` in `patch.json` gets a configure button next to its switch, which
opens a popup. Options that share a `group` go in an accordion, and `when` disables an option
while a switch is off, or, written as `"key=value"`, while another option has any other value.

```json
"options": [
  { "group": "Look", "key": "look.show", "type": "switch", "title": "Show", "default": true },
  { "group": "Look", "key": "look.side", "type": "select", "title": "Side", "when": "look.show", "default": "left",
    "choices": [ { "value": "left", "title": "Left" }, { "value": "right", "title": "Right" } ] },
  { "group": "Look", "key": "look.gap", "type": "slider", "title": "Gap", "when": "look.show", "default": 4, "min": 0, "max": 16, "unit": "px" }
]
```

Keys and values can use letters, digits and `-_.~`. Changes apply as they're made. The patch
gets each value as text, before `PATCH_ENABLE` and again whenever one changes while it's on:

```c
PATCH_OPTION() {
    if (!strcmp(key, "look.gap")) Gap = atoi(value);
    // Redraw here if the patch is on.
}
```

Values that differ from the defaults are kept in `options\<id>.tsv` in the home folder. A patch
with `"restart": true` gets changes the next time Aside starts, and its popup says so.

## How it works

1. `aside-late.exe` starts `Aside.exe` as a debugger, waits for the moment `Aside.dll` is
   mapped, has it load `aside-late-mod.dll`, and lets go.
2. The mod finds the patches next to itself and in its home folder. For those switched on, it
   loads the DLL, runs its start against a clean copy of `Aside.dll` read from disk, and enables it.
3. After that, the settings page asks for changes through paths that don't exist on disk. One
   worker thread does what takes time, and hands anything touching the browser to the UI thread.

### Installing

The page reads a repo with GitHub's API. Install asks the mod to fetch `patches/<id>/` at that
commit into `patches\<id>\source` in its home folder, and compile it against the headers in
`build\sdk` and the mod's DLL. The compiler is [w64devkit](https://github.com/skeeto/w64devkit),
downloaded into the home folder the first time one is needed. `ASIDE_LATE_GCC` points at another.

New source stays in `source.new` until it has compiled, so a failed update leaves what you had.
An update loads the new DLL beside the old one, whose hooks are off. A loaded DLL can't safely be
unloaded, because Aside could be running its code, so the old file goes on the next launch.

### Crashes

The mod notes each risky moment in `session.txt`, and watches for faults with a patch on the stack.
The next launch reads what's left behind:

- **A fault in a patch** turns that patch off, and settings says why.
- **A crash within 5 seconds of turning a patch on** does the same.
- **A crash within 30 seconds of starting** with nothing to blame runs that one launch with no
  patches. Force-quitting that soon looks the same, except through `aside-late.exe`, which knows.

### settings

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

## Limits

This is later binding than offsets, not real name lookup like Dia's Objective-C hooks:

- C++ keeps few names, and many classes are only reachable through code. A rebuild can change
  that code's shape without the feature changing, and then a patch refuses until its rule is updated.
- It runs on every launch, and opening Aside any other way gives you the normal browser. That
  includes opening a link while it's closed, and Aside restarting itself after an update.
- Patches found on startup add about 3 s to it. Turning one on later takes about the same, off
  the UI thread, the first time.
- Windows swallows an exception in a timer callback, and a `CHECK` in Aside is one. A patch that calls Aside
  from its timer can fail there with nothing in `mod.log` and no crash.
