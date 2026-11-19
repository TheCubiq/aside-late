# Writing patches

A patch is a folder in `patches/`, here or in any GitHub repo. The ones here come compiled inside
the exe. One from anywhere else is delivered as source, and compiled on your machine when you install it.

```text
patches/hide-something/
  patch.json    id, title, description, version, sdk, the builds it has been run on
  patch.c       any number of .c files, in folders too
  README.md     what it does and how it works
```

A patch's files include each other from its top folder, like `#include "resolve/resolve.h"`.

What several patches find in Aside the same way is in `patches/shared`, which isn't a patch. They include it as
`#include "shared/browser.h"`, so a rule that stops matching a new build is fixed in one place. It's headers only, since a patch
compiles every `.c` file in its folder. Installing a patch from GitHub fetches `patches/shared` from the same repo too, and a
change there needs the version of every patch that includes it raised. It's local to each repo: a patch elsewhere can copy
these into its own repo's `patches/shared`, but can't use or add to this one. See [patches/shared](../patches/shared/README.md).

Each patch here explains itself, and is a working example:

- [bookmark-folder-appearance](../patches/bookmark-folder-appearance/README.md)
- [sidebar-tweaks](../patches/sidebar-tweaks/README.md)
- [sidebar-fullscreen](../patches/sidebar-fullscreen/README.md)
- [window-tweaks](../patches/window-tweaks/README.md)
- [square-corners](../patches/square-corners/README.md)
- [fullscreen-gap](../patches/fullscreen-gap/README.md)
- [window-glass](../patches/window-glass/README.md)
- [chats](../patches/chats/README.md)
- [ask-aside](../patches/ask-aside/README.md)

Small patches are better than big ones. A patch that can't find something in a new build of Aside refuses as a whole, so a
part that still works shouldn't wait on one that doesn't.

A patch split out of another says so, and the first time it's there it takes over what it used to be:

```json
"carries": {"from": "window-tweaks", "switch": "fullscreen.gap", "default": true, "options": ["edge.width"]}
```

It's on if `from` was on with its `switch` on, `default` being that switch's default, and the listed options keep their values
under the same keys.

Add `"restart": true` to `patch.json` if a patch can only work from startup. Settings then
says it applies the next time Aside starts.

## Versions

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

## The SDK

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
| `LookupRemember` | Keep what a patch worked out about this build under a name, so another patch gets it with `LookupRecall` |

Hooks stack, so patches can share a method and come off in any order. Two patches can't edit
the same bytes: the second one's edit is refused.

Prefer classes and methods to code patterns. They're what survives a rebuild of Aside.

## Options

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

To try one, see [Testing](../CONTRIBUTING.md#testing). [How it works](how-it-works.md) covers what runs where.
