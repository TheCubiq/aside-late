# aside-late

Late-bound patches for Aside on Windows x64. They change no file of Aside's on disk and store
no offsets. Every launch, each patch finds what it needs in whatever build is loaded, and it
can be switched on and off, or installed, while the browser runs.

`aside/patcher` binds early: exact file offsets, pinned to one SHA-256 of `Aside.dll`.

## Use

Run:

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
| `--shortcut [path]` | Make a shortcut to `aside-late.exe`, on the desktop unless a path is given |

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

## Patches

A patch is a folder in `patches/`, here or in any GitHub repo. Patches are always delivered
as source: `make` compiles the ones here, and an installed one is compiled on your machine.

```text
patches/hide-something/
  patch.json    id, title, description, the builds it has been run on
  patch.c       any number of .c files
```

Add `"restart": true` to `patch.json` if a patch can only work from startup. Settings then
says it applies the next time Aside starts.

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
while a switch is off.

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

### chats

1. The largest function that references `aside.vertical_tabs.chats_section_expanded` is the
   sidebar's constructor. The first vtable it installs is the sidebar's class.
2. After that reference, its first two stores to `[rsi+field]` are the Chats container and its
   divider.
3. Hold both hidden. The live sidebar comes from `FindObjects`, and a new window's when it first draws.

### ask-aside

1. Find the toolbar button's update function: it loads Aside's extension ID, and has
   `xor dl,1` before a `SetVisible`.
2. Its constructor is the caller that also sets the field it reads, and that gives the class.
3. Hook `SetVisible` for the class, and hold the button whose object leads to Aside's extension ID.

### bookmark-folder-appearance

Right-click a bookmark folder for a menu built like Aside's tab group menu: its colors, its
icon picker, a reset, and the folder's own Rename, Delete and Manage Bookmarks. A colored
folder and its open contents get a rounded tint, and its icon and title take the color.
Choices are kept by folder GUID in `folders.tsv` in the home folder.

Its options arrange every row, folder or not:

- **Nesting** sets how far each level moves its children from either side. The row's Layout works
  from its layout's left inset and from its own width, so those are changed around that call. The
  row and its container paint the highlight and outline around the row's bounds, so a nested row's
  bounds are nested for those two calls, and Aside draws them itself.
- **Bottom offset** is extra margin on the last row inside a folder that has a backdrop. It's set
  before the container measures or lays out, so Aside's own animation heads for the right place.
  **Gap below a folder** is more margin on that row, past the end of the outermost backdrop.
- **Collapsed loaded nesting** builds on Aside keeping a loaded row, and every folder above it,
  when a folder folds. It hides those folders and indents the row one level under the folded one,
  moving its parts there gradually. Aside animates a row from wherever it last was, so a hidden row is
  kept with no height just above the first row inside it, and comes out from there.
- **Unloading from a collapsed folder** is animated by holding back the rebuild it causes. Closing a tab
  rebuilds the rows with no animation, a moment later. While Aside's own guard against rebuilding during a
  rebuild is up, that only notes it's wanted, so the patch lowers the guard and runs an animated one itself.
- **The unload button** is a folder row's own close button, shown while it's hovered and there's a loaded
  tab inside. Aside puts a close button last and moves a caret aligned right before it, so those two swap.
- **Selected tab first when collapsed** is what Aside does on its own: a folded folder lists its
  active tab first, and then the last one it was. Off, a jump over that code leaves bookmark order.
- **A dragged row** takes the nesting of the row after the spot it's over, and the top level
  below the last row. **Snap dragged rows to their nesting** locks it across instead, and how far the
  cursor has come sideways since the press is the nesting. Aside's own drag still does the work: its move
  callback is swapped for one that tells it the point meaning that nesting, then puts the row back under
  the cursor. It only offers a folder on the folder's lower half, so a row going deeper below a folder is
  told it's there, and one going shallower in the middle of a folder is told it's at the folder's end, so
  it lands after the folder. While it's dragged, a row's level comes from Aside's drop target.
- **What a hover or a drag changes** is tended to from the patch's timer, not while a row paints. The
  close button gets a layer once it's hovered, and removing a layer while the compositor goes through them
  takes the browser down.
- **The caret** is moved with `View::SetBoundsRect` once the row has laid itself out. Its paint
  turns the image by a progress value times 180°, so setting that value just before it paints
  gives any two angles, either direction and Aside's own animation.
- **The backdrop** can be hidden or untinted. Untinted uses the theme's text color at 10%, which
  contrasts in light and dark mode. Aside animates rows with a transform and only changes their
  bounds at the end, so a backdrop's position comes from each row's record in Aside's animator.
  Aside removes a folded folder's rows at once, so its height eases on its own along about the
  same curve. That height belongs to the folder's node and its window, because most rebuilds make every row
  again, often at the same addresses in another order, and each window's sidebar has its own rows for the
  same nodes. A dragged folder's backdrop moves by the row's drag offset.
- **A folding folder's rows** are ones Aside deletes at the end of its rebuild, or folders that
  **Collapsed loaded nesting** just hid. The row's deleting destructor holds on to the first kind when
  a rebuild called it, and the container paints the icon, title and caret of both, clipped to what
  hasn't folded away yet and fading, then deletes the first kind. It doesn't paint the row itself,
  because something in a row's paint clears the canvas, which would take the container's with it.
- **A folder's hover highlight** can be never, only while it's collapsed, or always, by not counting
  it as hovered while it paints.
- **A bookmark's X** is the button Aside shows on a loaded bookmark. Showing it on one that isn't
  loaded makes it delete, through Aside's own "Delete this bookmark?" dialog, or straight to that
  dialog's answer with **Ask before removing** off. On a loaded bookmark it can be a minus, and
  still does what Aside's X does.

Nothing is stored per build:

- **Classes** come from names. `VerticalBookmarkRowView` leads to its `MetaData()`, the
  vtable holding that, the constructor installing that vtable, and the primary vtable it
  installs. Methods are swapped in the vtable, like swizzling.
- **Method slots** come from trace strings. `View::OnPaintBackground`'s slot is voted on
  across every vtable that uses it.
- **Icons** name themselves. `kAsideBookmarkFolderIcon` leads to the row's icon function,
  and that function's calls are the image create, set and destroy functions.
- **The right click** is the row method that tests for the right button. The label
  `Manage Bookmarks` finds what Aside would have run, and that shows how it sets up a
  folder's commands, so the menu's items end in Aside's own handlers.
- **The menu's parts** are read out of the function building the tab group menu, found by
  `TabGroupColorButton`. It can't be called for a folder because it needs a real tab group,
  but it shows each control's size, vtables and constructor, and how a popup is made.
- **Object layouts** are learned from the first row that paints, by shape: a parent lists
  the row among its children, a node holds a GUID and is listed by its parent, and an image
  view holds the icon and color it was just given. Aside has measured the rows by then, without their
  margins, so they're built once more.
- **Colors** are Aside's theme colors, so they follow light and dark mode. A small switch
  maps each group color to its background ID, with the icon and title IDs just before it.
  `Background::Paint` shows how to resolve an ID, for the tint's own alpha.
- **Reset** calls the row's own icon function, so a folder goes back to whatever Aside draws. It's only
  in the menu when there's something to reset.
- **New folder** opens Chromium's bookmark editor, the one its bookmark bar uses for this. Its functions
  are read in order after the metric that command records, `BookmarkBar_ContextMenu_NewFolder`.

### sidebar-tweaks

Aside's `aside_sidebar_hover_controller.cc` is a state machine. It asks a callback for the cursor and some
rects, and works out from those whether a collapsed sidebar should show. The callback is an object holding
a method, so looking for that method's address finds every window's, and swapping it changes the answers.
Nothing here shows or hides anything.

- **Stay open while a menu is open** holds the cursor over the panel while the browser's window owns another
  window, which covers menus and their submenus. A menu also takes the focus, so the answer is what it was
  when the window last had it. A browser losing focus hides the sidebar outright as well. That jump is
  edited out, and the sidebar still closes on losing focus, because the controller is then told nothing.
- **Reveal from the window's edge** answers that the cursor is over the button. Aside has its own strip at the
  window's edge, but a maximized window starts past the edge of the screen, so that's where the strip is too.
  This one starts where the window can be seen, and keeps the cursor out of Aside's.
- **Delay on enter** holds that answer back. Asking only happens as the mouse moves, so a timer asks too.
- **Delay on leave** moves the time the controller started closing from, so its own comparison comes out right.

## Limits

This is later binding than offsets, not real name lookup like Dia's Objective-C hooks:

- C++ keeps few names, and many classes are only reachable through code. A rebuild can change
  that code's shape without the feature changing, and then a patch refuses until its rule is updated.
- It runs on every launch, and opening Aside any other way gives you the normal browser. That
  includes opening a link while it's closed, and Aside restarting itself after an update.
- Patches found on startup add about 3 s to it. Turning one on later takes about the same, off
  the UI thread, the first time.
- A new window's sidebar draws once before Chats is hidden, so Chats could flash.
- The icon picker shows names rather than the icons themselves.
- A backdrop's height follows its own timing rather than Aside's animation, so it can be a few
  pixels from the rows beside it while they move. Its position is exact.
- Windows swallows an exception in a timer callback, and a `CHECK` in Aside is one. A patch that calls Aside
  from its timer can fail there with nothing in `mod.log` and no crash.
- Only the X and the unload button animate an unload. A tab closed any other way goes as it does in Aside.
- With **Ask before removing** off, the X on an unloaded bookmark deletes it at once, and nothing
  undoes that here.
- Reopening a collapsed sidebar hasn't been tested with `chats`, because UI Automation
  couldn't re-expand it.
