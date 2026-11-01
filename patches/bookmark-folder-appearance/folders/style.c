// A folder's icon and title color, and the right click that opens its menu.

#include <math.h>
#include "rows.h"

#define ICON_SIZE 16
#define BUTTON_MASK 0xf800
#define RIGHT_BUTTON 0x2000

typedef void (*SetImage)(void *view, const void *image);
typedef void (*UpdateIcons)(void *row);
typedef void (*SetTitleColor)(void *label, ULONGLONG color);
typedef void (*ConvertToScreen)(const void *view, POINT *point);

MousePressed OriginalMousePressed;
static DWORD ImageIconOffset;
static DWORD ImageColorOffset;
static char MenuGuid[GUID_SIZE];

// Icon, title and background IDs sit together for each color. The neutral one has its own.
static ULONGLONG IconColor(int color) {
    return color == NEUTRAL_COLOR ? Native.groupForeground : Native.groupBackgrounds[color] - 2;
}

static ULONGLONG TitleColor(int color) {
    return color == NEUTRAL_COLOR ? Native.groupForeground : Native.groupBackgrounds[color] - 1;
}

static void SetImageView(void *view, const void *icon, ULONGLONG color) {
    BYTE image[64] = {0};
    ((CreateImage)(Base + Native.createImage))(image, icon, color, ICON_SIZE, NULL);
    ((SetImage)(Base + Native.setImage))(view, image);
    ((DestroyImage)(Base + Native.destroyImage))(image);
}

// An image view keeps the icon and color it was given. Finding those values straight after
// setting them shows where they live, so later paints can tell if anything changed them.
static void LearnImageShape(const BYTE *view, const void *icon, ULONGLONG color) {
    for (DWORD offset = 0x100; offset < 0x800; offset += 8) {
        const void *value;
        if (!SafeRead(view + offset, &value, sizeof value) || value != icon) continue;
        for (DWORD other = offset + 4; other < offset + 0x40; other += 4) {
            ULONGLONG candidate;
            if (!SafeRead(view + other, &candidate, sizeof candidate) || candidate != color) continue;
            ImageIconOffset = offset;
            ImageColorOffset = other;
            ModLog("learned: image icon +%#lx, color +%#lx", offset, other);
            return;
        }
    }
    ModLog("couldn't learn the image view's shape");
}

static BOOL ImageIsCurrent(const BYTE *view, const void *icon, ULONGLONG color) {
    return *(const void *const *)(view + ImageIconOffset) == icon && *(const ULONGLONG *)(view + ImageColorOffset) == color;
}

static void SetRowTitleColor(const BYTE *row, ULONGLONG color) {
    void *title = *(void *const *)(row + Native.titleOffset);
    if (!title) return;
    void **vtable = *(void ***)title;
    ((SetTitleColor)vtable[Native.setTitleColorOffset / 8])(title, color);
}

// Runs on every paint, so it only touches what's out of date. Setting an image repaints.
void StyleRow(BYTE *row) {
    BYTE *node = NodeOf(row);
    const Folder *folder = node ? FindFolder(GuidOf(node)) : NULL;
    if (!folder) return;
    // Icons are themed like Aside's own until the folder has a color.
    ULONGLONG color = folder->color >= 0 ? IconColor(folder->color) : Native.themedColorId - row[Native.selectedOffset];
    const void *glyph = folder->icon >= 0 ? Icons[folder->icon].icon : Base + Native.folderIcon;
    const void *caretGlyph = Base + Native.caretIcon;
    BYTE *leading = PartOf(row, Native.leadingOffset), *caret = PartOf(row, Native.caretOffset);
    if (!leading) return;
    if (ImageIconOffset && ImageIsCurrent(leading, glyph, color) && (!caret || ImageIsCurrent(caret, caretGlyph, color))) return;
    SetImageView(leading, glyph, color);
    if (!ImageIconOffset) LearnImageShape(leading, glyph, color);
    if (caret) SetImageView(caret, caretGlyph, color);
    if (folder->color >= 0) SetRowTitleColor(row, TitleColor(folder->color));
}

// Back to exactly what Aside would draw, using its own function and its own color.
void RestoreRow(BYTE *row) {
    ((UpdateIcons)(Base + Native.updateIcons))(row);
    SetRowTitleColor(row, Native.titleColorId + (row[Native.selectedOffset] ? 3 : 0));
}

static void RefreshRows(const char *guid) {
    for (int i = 0; i < ContainerCount; i++) {
        size_t count;
        void **children = ChildrenOf(Containers[i].view, &count);
        for (size_t j = 0; j < count; j++) {
            BYTE *row = children[j];
            if (!IsRow(row) || !NodeOf(row) || memcmp(GuidOf(NodeOf(row)), guid, GUID_SIZE)) continue;
            RestoreRow(row);
            StyleRow(row);
        }
        Repaint(Containers[i].view);
    }
}

static void PickColor(int color) {
    const Folder *folder = FindFolder(MenuGuid);
    SetFolder(MenuGuid, color, folder ? folder->icon : -1);
    RefreshRows(MenuGuid);
}

static void PickIcon(const char *key, size_t size) {
    int icon = FindIcon(key, size);
    if (icon < 0) return;
    const Folder *folder = FindFolder(MenuGuid);
    SetFolder(MenuGuid, folder ? folder->color : -1, icon);
    RefreshRows(MenuGuid);
}

static void ResetFolder(void) {
    SetFolder(MenuGuid, -1, -1);
    RefreshRows(MenuGuid);
}

BOOL RowMousePressed(void *view, const void *event) {
    BYTE *row = view;
    const BYTE *press = event;
    BYTE *node = LearnedNode(row);
    BYTE *owner = node ? OwnerOf(row) : NULL;
    if ((*(const DWORD *)(press + Native.eventFlagsOffset) & BUTTON_MASK) != RIGHT_BUTTON || !owner ||
        !IsFolder(node) || node[Native.nodeManagedOffset])
        return OriginalMousePressed(view, event);
    const float *location = (const float *)(press + Native.eventLocationOffset);
    POINT screen = {lroundf(location[0]), lroundf(location[1])};
    ((ConvertToScreen)(Base + Native.convertToScreen))(row, &screen);
    memcpy(MenuGuid, GuidOf(node), GUID_SIZE);
    const Folder *folder = FindFolder(MenuGuid);
    MenuChoices choices = {
        .color = folder ? folder->color : -1,
        .icon = folder && folder->icon >= 0 ? &Icons[folder->icon] : NULL,
        .pickColor = PickColor,
        .pickIcon = PickIcon,
        .reset = ResetFolder,
    };
    MenuOpen(row, owner, node, screen, &choices);
    return TRUE;
}
