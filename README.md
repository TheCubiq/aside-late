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

Right-click a bookmark folder to pick a color from a small strip beside Aside's own menu.
The folder and its open contents get a rounded tint. Colors are kept by folder GUID in
`%LOCALAPPDATA%\AsideLateolders.tsv`.

- Classes are found by name: `VerticalBookmarkRowView` leads to its `MetaData()` function,
  the vtable holding that, the constructor installing that vtable, and the primary vtable
  it installs. Methods are swapped in the vtable, like swizzling.
- `View::OnPaintBackground`'s slot comes from its trace string, voted on across every
  vtable that uses it.
- Object layouts are learned from the first row that paints, by shape: a parent lists the
  row among its children, and a node holds a GUID and is listed by its parent.

## Limits

This is later binding than offsets, not real name lookup like Dia's Objective-C hooks:

- Patches still assume things that could change: strings, the order of stores in a
  constructor, instruction shapes, and `SetVisible` being vtable slot `0x90`. A change
  there means a refusal or a rule update.
- It runs on every launch, and opening Aside any other way gives you the normal browser.
  It adds about 0.5 s to startup.
- Reopening a collapsed sidebar hasn't been tested with `chats`, because UI Automation
  couldn't re-expand it.
