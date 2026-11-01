# Bookmark folder appearance

Right-click a bookmark folder for a menu built like Aside's tab group menu: its colors, its
icon picker, a reset, and the folder's own Rename, Delete and Manage Bookmarks. A colored
folder and its open contents get a rounded tint, and its icon and title take the color.
Choices are kept by folder GUID in `folders.tsv` in the home folder.

## Where things are

| Where | What's in it |
| --- | --- |
| `patch.c` | The patch's life: options, turning on and off, and the timer |
| `rows.h` | What the files share, by the file that owns it |
| `resolve/` | Finding everything in Aside: `resolve.h` is what's bound, `resolve.c` runs the rules, and the rest are the rules by what they're about |
| `rows/shape.c` | Object layouts learned from the first row that paints |
| `rows/row.c` | One row's layout, caret and paint |
| `rows/nesting.c` | How far in a row sits, and moving between levels |
| `rows/arrange.c` | Margins and hidden folders, set before Aside measures rows |
| `rows/containers.c` | The view holding the rows, one per window |
| `rows/close.c` | The close button: unloading and removing |
| `rows/drag.c` | A dragged row's level, and snap dragging |
| `folders/folders.c` | What's been chosen for each folder |
| `folders/style.c` | A folder's icon and title color, and the right click |
| `folders/menu.c` | The folder's menu, and New folder |
| `backdrop/backdrop.c` | The tint behind a folder |
| `backdrop/ghosts.c` | A folding folder's rows, fading out |

## Options

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
- **A folder's hover highlight** can be never, only while it's collapsed, or always, by not counting
  it as hovered while it paints.
- **A bookmark's X** is the button Aside shows on a loaded bookmark. Showing it on one that isn't
  loaded makes it delete, through Aside's own "Delete this bookmark?" dialog, or straight to that
  dialog's answer with **Ask before removing** off. On a loaded bookmark it can be a minus, and
  still does what Aside's X does.

## Other behaviour

- **A folding folder's rows** are ones Aside deletes at the end of its rebuild, or folders that
  **Collapsed loaded nesting** just hid. The row's deleting destructor holds on to the first kind when
  a rebuild called it, and the container paints the icon, title and caret of both, clipped to what
  hasn't folded away yet and fading, then deletes the first kind. It doesn't paint the row itself,
  because something in a row's paint clears the canvas, which would take the container's with it.
- **What a hover or a drag changes** is tended to from the patch's timer, not while a row paints. The
  close button gets a layer once it's hovered, and removing a layer while the compositor goes through them
  takes the browser down.

## How it's found

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

## Limits

- The icon picker shows names rather than the icons themselves.
- A backdrop's height follows its own timing rather than Aside's animation, so it can be a few
  pixels from the rows beside it while they move. Its position is exact.
- Only the X and the unload button animate an unload. A tab closed any other way goes as it does in Aside.
- With **Ask before removing** off, the X on an unloaded bookmark deletes it at once, and nothing
  undoes that here.
