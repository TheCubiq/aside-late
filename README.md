# aside-late

Late-bound patches for Aside on Windows x64. They change no file on disk and store no
offsets.

`aside/patcher` binds early: exact file offsets, pinned to one SHA-256 of `Aside.dll`.
This binds late: every launch, each patch finds its own code in whatever build is
loaded, then edits that one running process.

## Use

Run:

```powershell
.\build\aside-late.exe
```

If Aside is already running on that profile, it asks whether to terminate it first,
including one in the background with no windows open. Answer no and it leaves Aside alone
and opens nothing. Terminating is a force-quit, so Aside may offer to restore your tabs.

| Option | What it does |
| --- | --- |
| `--list` | List the patches |
| `--without <id>` | Leave a patch out. Repeat it for more |
| `--exe <Aside.exe>` | Use another installation |
| `--scan <Aside.dll> ...` | Show what each patch would edit, without running anything |

Anything else goes to Aside, such as `--user-data-dir=<path>`.

To undo everything, quit the patched Aside and start it normally. Closing its windows may
not be enough, because Aside can keep running in the background:

```powershell
Stop-Process -Name Aside
```

## Build

Needs MinGW-w64 `gcc`:

```powershell
make
```

## Patches

Each patch is a folder in `src/patches`. Adding a folder is all it takes to register one:

```text
src/patches/chats/
  patch.json    id, title, description, and the builds it has been run on
  patch.c       how to find the code
```

`patch.c` looks for what stays the same between builds, such as a string or the shape of
a call, and declares itself with `PATCH("<folder>", Find)`. `Find` returns its edits, or
`Refuse(...)` if the build doesn't look as expected.

A patch that refuses changes nothing, and the other patches still apply. Each edit is
checked against the bytes in memory before anything is written.

`tested` is only a record. It doesn't stop a patch from running on other builds, and
`--scan` is how to see whether one still finds its code.

Patches that need to run code inside Aside add `mod.c`, built into
`build/aside-late-mod.dll`, and use `PATCH_IN_ASIDE`. Their log is
`%LOCALAPPDATA%\AsideLate\mod.log`.

## How it works

1. Start `Aside.exe` as a debugger and wait for the moment `Aside.dll` is mapped, before
   any of its code runs.
2. Run each patch's `Find` against a copy of the DLL.
3. Write the edits into the process and detach.

### chats

1. The largest function that references `aside.vertical_tabs.chats_section_expanded` is
   the sidebar constructor.
2. After that reference, its first two stores to `[rsi+field]` are the Chats container
   and its divider.
3. Near the constructor, find the `SetVisible` calls on those fields, ignoring which
   register holds the argument. Layout calls now pass false, and the visibility update
   skips its call.

### ask-aside

1. Find `mov edx,<any>; xor dl,1; SetVisible` in a function that also has a
   `mov dl,1; SetVisible` call and loads Aside's extension ID.
2. Require exactly one such function. Both calls now pass false.

### bookmark-folder-appearance

Right-click a bookmark folder for a small panel beside Aside's own menu, with colors, icons
and a reset. A colored folder and its open contents get a rounded tint, and its icon and
title take the color. Choices are kept by folder GUID in
`%LOCALAPPDATA%\AsideLate\folders.tsv`. Aside's menu stays untouched.

Nothing is stored per build:

- **Classes** come from names. `VerticalBookmarkRowView` leads to its `MetaData()`, the
  vtable holding that, the constructor installing that vtable, and the primary vtable it
  installs. Methods are swapped in the vtable, like swizzling.
- **Method slots** come from trace strings. `View::OnPaintBackground`'s slot is voted on
  across every vtable that uses it.
- **Icons** name themselves. `kAsideBookmarkFolderIcon` leads to the row's icon function,
  and that function's calls are the image create, set and destroy functions. The picker's
  names come from Aside's own icon table, found by its shape.
- **Object layouts** are learned from the first row that paints, by shape: a parent lists
  the row among its children, a node holds a GUID and is listed by its parent, and an image
  view holds the icon and color it was just given.
- **Reset** calls the row's own icon function, so a folder goes back to whatever Aside draws.

## Limits

This is later binding than offsets, not real name lookup like Dia's Objective-C hooks:

- Patches still assume things that could change: strings, the order of stores in a
  constructor, instruction shapes, and `SetVisible` being vtable slot `0x90`. A change
  there means a refusal or a rule update.
- It runs on every launch, and opening Aside any other way gives you the normal browser.
  Byte patches add about 0.5 s to startup, and mods about 1.5 s more.
- The icon picker shows names rather than the icons themselves.
- Reopening a collapsed sidebar hasn't been tested with `chats`, because UI Automation
  couldn't re-expand it.
