// A row's close button: it unloads a folder, removes a bookmark, or does what Aside's does.

#include "rows.h"

#define BUTTON_STATES 3
#define UNLOAD_WAIT_MS 500
#define UNLOAD_SETTLE_MS 48
#define CLOSE_BUTTON_SIZE 0x640
#define CLOSE_ICON_SIZE 16
#define PRESS_RECEIVER 0x30

typedef void (*UnloadTab)(void *owner, const void *node);
typedef void (*DeleteAccepted)(void *state, int result);
typedef void (*SetButtonImage)(void *button, int state, void *image);
typedef void (*ClosePress)(void *row, void *a, void *b, void *c);

static ClosePress OriginalClosePress;
static DWORD ClosePressOffset;
BYTE *HeldOwner;
static ULONGLONG HeldUntil;
static BOOL RebuildAnimated;
BYTE *DeleteOwner;
static LONGLONG DeleteNodeId;
static DWORD CloseIconOffset;
static BOOL CloseIconUnknown;

static BOOL IsLoadedInside(const BYTE *view, const BYTE *folder) {
    return IsRow(view) && view[Native.loadedOffset] && NodeOf(view) && IsInside(NodeOf(view), folder);
}

// Whether a folder shows the button that unloads what's in it. There has to be something to unload.
static BOOL ShowsUnloadButton(const BYTE *row, const BYTE *node, const BYTE *owner) {
    if (FolderUnload == UNLOAD_NEVER || !Native.unloadTab || !Native.rebuildGuardOffset || !owner) return FALSE;
    if (FolderUnload == UNLOAD_COLLAPSED && FolderIsOpen(owner, node)) return FALSE;
    const BYTE *parent = ParentOf(row);
    if (!parent) return FALSE;
    size_t count;
    void **children = ChildrenOf(parent, &count);
    for (size_t i = 0; i < count; i++) {
        const BYTE *other = children[i];
        if (IsLoadedInside(other, node)) return TRUE;
    }
    return FALSE;
}

// Answers Aside's "Delete this bookmark?" without asking it. The answer's state holds a weak flag, which it releases.
void DeleteBookmark(void) {
    struct {
        LONG references;
        BYTE invalid;
    } flag = {2, 0};
    BYTE state[0x40] = {0};
    *(void **)(state + 0x28) = &flag;
    *(BYTE **)(state + 0x30) = DeleteOwner;
    *(LONGLONG *)(state + 0x38) = DeleteNodeId;
    DeleteOwner = NULL;
    ((DeleteAccepted)(Base + Native.deleteAccepted))(state, 1);
}

// A tab closes a moment after it's asked to, and that rebuilds the rows with no animation. While the guard is up a rebuild
// only notes that it's wanted, so it can happen when the tab has gone, the way it should. The tick lets go of it.
static void HoldRebuilds(BYTE *owner, BOOL animated) {
    owner[Native.rebuildGuardOffset] = 1;
    HeldOwner = owner;
    HeldUntil = GetTickCount64() + UNLOAD_WAIT_MS;
    RebuildAnimated = animated;
}

static void UnloadFolder(const BYTE *row, const BYTE *node, BYTE *owner) {
    size_t count;
    void **children = ChildrenOf(ParentOf(row), &count);
    HoldRebuilds(owner, UnloadAnimated);
    for (size_t i = 0; i < count; i++)
        if (IsLoadedInside(children[i], node)) ((UnloadTab)(Base + Native.unloadTab))(owner, NodeOf(children[i]));
}

// What a close button runs. On a folder it's the button that unloads what's in it. On a bookmark that isn't loaded there's
// no tab to close, so it deletes the bookmark. Without asking, that destroys the row and this button, so it waits until the
// button's press is over.
static void ClosePressed(void *view, void *a, void *b, void *c) {
    BYTE *row = view;
    BYTE *node = NodeOf(row), *owner = OwnerOf(row);
    if (Enabled && node && owner && IsFolder(node)) {
        if (!Native.unloadTab || !Native.rebuildGuardOffset) return;
        UnloadFolder(row, node, owner);
    } else if (Enabled && UnloadAnimated && Native.rebuildGuardOffset && Native.rebuild && owner && row[Native.loadedOffset] && FoldedAbove(row, owner)) {
        HoldRebuilds(owner, TRUE);
        OriginalClosePress(view, a, b, c);
    } else if (Enabled && CloseRemoves && !row[Native.loadedOffset] && node && owner && !node[Native.nodeManagedOffset]) {
        if (CloseConfirms) {
            MenuAskDeleteBookmark(owner, node);
            return;
        }
        DeleteOwner = owner;
        DeleteNodeId = *(const LONGLONG *)(node + Native.nodeIdOffset);
    } else {
        OriginalClosePress(view, a, b, c);
    }
}

// The button keeps a bound callback that holds a method and the row it runs on.
static void TakeClosePress(BYTE *row, BYTE *close) {
    for (DWORD offset = ClosePressOffset ? ClosePressOffset : 0x1d0; offset < (ClosePressOffset ? ClosePressOffset + 1 : 0x658); offset += 8) {
        BYTE *state;
        void *method, *receiver;
        if (!SafeRead(close + offset, &state, sizeof state) || !state) continue;
        if (!SafeRead(state + PRESS_RECEIVER, &receiver, sizeof receiver) || receiver != row) continue;
        if (!SafeRead(state + PRESS_METHOD, &method, sizeof method)) continue;
        if (method == ClosePressed) return;
        if (!IsInAside(method)) continue;
        ClosePressOffset = offset;
        OriginalClosePress = method;
        *(void **)(state + PRESS_METHOD) = ClosePressed;
        return;
    }
}

static void SetCloseIcon(BYTE *close, const void *icon, DWORD color) {
    void **vtable = *(void ***)close;
    for (int state = 0; state < BUTTON_STATES; state++) {
        BYTE image[64] = {0};
        ((CreateImage)(Base + Native.createImage))(image, icon, (1ull << 32) | color, CLOSE_ICON_SIZE, NULL);
        image[40] = 1;  // optional<ImageModel>'s engaged flag.
        ((SetButtonImage)vtable[Native.closeSetImageSlot])(close, state, image);
        ((DestroyImage)(Base + Native.destroyImage))(image);
    }
}

// An image button keeps the icon it was given. Finding it straight after setting it shows where it lives.
static void LearnCloseIcon(const BYTE *close, const void *icon) {
    for (DWORD offset = 0x200; offset < CLOSE_BUTTON_SIZE; offset += 8) {
        const void *value;
        if (!SafeRead(close + offset, &value, sizeof value) || value != icon) continue;
        CloseIconOffset = offset;
        ModLog("learned: close button icon +%#lx", offset);
        return;
    }
    // Setting it on every paint would never end, so this goes without.
    CloseIconUnknown = TRUE;
    ModLog("couldn't learn the close button's shape, so its icon stays");
}

// Aside only shows the button on a loaded bookmark, and hides it again when the hover ends either way.
void StyleClose(BYTE *row) {
    BYTE *node = NodeOf(row), *close = PartOf(row, Native.closeOffset);
    if (!node || !close) return;
    BOOL folder = IsFolder(node) != 0, loaded = row[Native.loadedOffset];
    if (!row[Native.hoverOffset]) {
        if (folder && IsVisible(close)) SetViewVisible(close, FALSE);
        return;
    }
    BOOL shows = folder ? ShowsUnloadButton(row, node, OwnerOf(row)) : CloseRemoves && !loaded;
    if (shows && !IsVisible(close)) SetViewVisible(close, TRUE);
    if (folder && !shows) {
        if (IsVisible(close)) SetViewVisible(close, FALSE);
        return;
    }
    TakeClosePress(row, close);

    if (!Native.minusIcon || CloseIconUnknown) return;
    // Aside gives the button its icon back whenever it likes, so what counts is what the button holds now.
    const void *minus = Base + Native.minusIcon;
    BOOL wanted = folder || (CloseIsMinus && loaded), has = CloseIconOffset && *(const void *const *)(close + CloseIconOffset) == minus;
    if (wanted == has || (!wanted && !CloseIconOffset)) return;
    if (wanted) SetCloseIcon(close, minus, *(const DWORD *)(close + Native.closeColorOffset));
    else ((RowFunction)(Base + Native.closeUpdateImage))(close);
    if (!CloseIconOffset) LearnCloseIcon(close, minus);
}

// Once a rebuild has been asked for, the rest of the tabs going together get a moment to do the same.
void ReleaseHeldRebuilds(BOOL now) {
    if (!ContainerOfOwner(HeldOwner)) {
        HeldOwner = NULL;
        return;
    }
    BYTE *guard = HeldOwner + Native.rebuildGuardOffset;
    ULONGLONG time = GetTickCount64();
    if (guard[1] && HeldUntil > time + UNLOAD_SETTLE_MS) HeldUntil = time + UNLOAD_SETTLE_MS;
    if (!now && time < HeldUntil) return;
    BYTE *owner = HeldOwner;
    BOOL wanted = guard[1];
    guard[0] = guard[1] = guard[2] = 0;
    HeldOwner = NULL;
    if (wanted) ((Rebuild)(Base + Native.rebuild))(owner, RebuildAnimated && !now);
}

void RestoreCloseIcon(const BYTE *row) {
    BYTE *close = PartOf(row, Native.closeOffset);
    if (close && Native.minusIcon && CloseIconOffset && *(const void *const *)(close + CloseIconOffset) == Base + Native.minusIcon)
        ((RowFunction)(Base + Native.closeUpdateImage))(close);
}
