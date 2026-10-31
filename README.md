# aside-late

Late-bound patches for Aside on Windows x64. They change no file on disk and store no
offsets.

`aside/patcher` binds early: exact file offsets, pinned to one SHA-256 of `Aside.dll`.
This binds late: every launch, each patch finds its own code in whatever build is
loaded, then edits that one running process.

## Use

Close Aside, then run:

```powershell
.\build\aside-late.exe
```

| Option | What it does |
| --- | --- |
| `--list` | List the patches |
| `--without <id>` | Leave a patch out. Repeat it for more |
| `--exe <Aside.exe>` | Use another installation |
| `--scan <Aside.dll> ...` | Show what each patch would edit, without running anything |

Anything else goes to Aside, such as `--user-data-dir=<path>`. To undo everything, start
Aside normally.

If Aside is already running on that profile, the new process hands off to it and the
patches don't take. The launcher tells you when that happens.

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

## Limits

This is later binding than offsets, not real name lookup like Dia's Objective-C hooks:

- Patches still assume things that could change: strings, the order of stores in a
  constructor, instruction shapes, and `SetVisible` being vtable slot `0x90`. A change
  there means a refusal or a rule update.
- It runs on every launch, and opening Aside any other way gives you the normal browser.
  It adds about 0.5 s to startup.
- Reopening a collapsed sidebar hasn't been tested with `chats`, because UI Automation
  couldn't re-expand it.
