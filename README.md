# aside-late

A late-patching demo for Aside on Windows x64. It hides the Chats section from the
sidebar without changing any file on disk and without storing any offset.

`aside/patcher` binds early: exact file offsets, pinned to one SHA-256 of `Aside.dll`.
This binds late: every launch it finds the code from a string that survives rebuilds,
then patches that one running process.

## Use

Close Aside, then run:

```powershell
.\aside-late.exe
```

Anything else you pass goes to Aside, such as `--user-data-dir=<path>`. Use
`--exe <Aside.exe>` for another installation. To undo it, start Aside normally.

If Aside is already running on that profile, the new process hands off to it and the
patch doesn't take. The launcher tells you when that happens.

To see what it would patch in a DLL, without running anything:

```powershell
.\aside-late.exe --scan <Aside.dll> [<Aside.dll> ...]
```

## Build

Needs MinGW-w64 `gcc`:

```powershell
make
```

## How it works

1. Start `Aside.exe` as a debugger and wait for the moment `Aside.dll` is mapped, before
   any of its code runs.
2. Find the string `aside.vertical_tabs.chats_section_expanded`. The largest function
   that references it is the sidebar constructor.
3. After that reference, the constructor's first two stores to `[rsi+field]` are the
   Chats container and its divider. Those field offsets are worked out, not stored.
4. Near the constructor, find the `SetVisible` calls on those two fields by their
   instruction shape, ignoring which register holds the argument.
5. Patch the process and detach. Layout calls now pass false, and the visibility update
   skips its call.

If it doesn't find exactly the three expected sites, it changes nothing and Aside runs
unpatched.

## Tested

| Build | Result |
| --- | --- |
| 1.0.1022.1 | Runs. Chats stays hidden through resizing and in a second window. Reopening a collapsed sidebar wasn't covered, because UI Automation couldn't re-expand it |
| 1.0.928.2 | Runs. Chats hidden. Sites match the ones verified by hand in `aside/patcher/docs/RESEARCH.md` |
| 1.0.928.1, 1.0.922.1 | `--scan` only |

## Limits

This is later binding than offsets, not real name lookup like Dia's Objective-C hooks:

- It still assumes things that could change: the string, the store order in the
  constructor, the instruction shapes, and `SetVisible` being vtable slot `0x90`. A
  change there means a refusal or a rule update.
- It runs on every launch, and opening Aside any other way gives you the normal Chats
  section. It adds about 0.5 s to startup.
