#include "menu.h"

#include <wchar.h>

#define COLOR_SIZE 24
#define ICON_SIZE 16
#define MAX_COLORS 16
#define MAX_RETIRED 8
#define ORIENTATION_VERTICAL 1
#define VIEW_SIZE 0x320
#define LAYOUT_SIZE 0x130
#define BORDER_SIZE 0x20
#define POPUP_REASON_CLOSED 5
#define ACTION_CHANGE_ICON -1
#define ACTION_RESET -2
#define ACTION_NEW_FOLDER -3
#define EDIT_DETAILS_SIZE 0x400

typedef struct {
    LONG references;
    void *invoke;
    void (*destroy)(void *state);
    int (*query)(void *state, int kind);
    int action;
} Callback;

// Chromium's std::u16string, which a menu item takes by value and frees itself.
typedef union {
    struct {
        wchar_t *data;
        size_t size;
        size_t capacity;
    } big;
    struct {
        wchar_t data[11];
        BYTE padding;
        BYTE size;
    } small;
} String16;

typedef struct {
    int top, left, bottom, right;
} Insets;

typedef struct {
    void *widget, *popup;
} Popup;

typedef void *(*OperatorNew)(size_t size);
typedef void (*OperatorDelete)(void *memory, size_t size);
typedef void *(*ViewAllocate)(void *allocator, size_t size, int flags);
typedef void (*ViewConstruct)(void *view);
typedef void *(*AddChild)(void *parent, void *child, size_t index);
typedef void (*SetPreferredSize)(void *view, const void *size);
typedef void (*SetLayoutManager)(void *view, void **layout);
typedef void (*SetBorder)(void *view, void **border);
typedef void (*ButtonConstruct)(void *button, void *callback);
typedef void (*ItemConstruct)(void *item, String16 *text, void *image, int arrow, void *callback, void *hover);
typedef void (*CreateImage)(void *result, const void *icon, ULONGLONG color, int size, void *badge);
typedef void (*DestroyImage)(void *image);
typedef void (*PopupConstruct)(void *popup, void *anchor, void *widget, const void *rect, int arrow, int shadow, int a, int b);
typedef void (*PopupDestruct)(void *popup);
typedef void (*PopupSetContents)(void *popup, void **view);
typedef void (*WidgetCreate)(void **widget, void *popup, void **closed);
typedef void (*WidgetPrepare)(void *widget, void *popup, void *content);
typedef void (*WidgetShow)(void *widget);
typedef void (*WidgetClose)(void *widget, int reason);
typedef void (*DeleteWidget)(void *widget, int flags);
typedef void *(*GetWidget)(void *view);
typedef void (*Update)(void *view);
typedef void (*SetInt)(void *view, int value);
typedef void (*PickerConstruct)(void *picker, const void *selected, void *callback);
typedef void (*MenuClosed)(void *owner, LONGLONG generation);
typedef void (*EditAddFolder)(void *details, const void *parent, size_t index);
typedef void (*EditDestroy)(void *details);
typedef void (*ShowEditor)(void *window, void *profile, const void *details, int type, void **callback);
typedef void *(*BrowserGet)(void *self);

static const Bindings *Native;
static BYTE *Base;
static DWORD ChildCountOffset;

static MenuChoices Choices;
static BYTE *Owner;
static LONGLONG Generation;
static int Command;
static BOOL OwnerPending;
static const BYTE *NewFolderParent;
static void *Colors[MAX_COLORS];
static int ColorCount;
static Popup Main, Picker;
static Popup Retired[MAX_RETIRED];
static int RetiredCount;

static void *Call(DWORD rva) {
    return Base + rva;
}

static int QueryNothing(void *state, int kind) {
    (void)state, (void)kind;
    return 0;
}

// Menus outlive nothing here: a callback holds no pointers, so it can run after its menu is gone.
static void DestroyCallback(void *state) {
    free(state);
}

static Callback *NewCallback(void *invoke, int action) {
    Callback *callback = calloc(1, sizeof *callback);
    callback->references = 1;
    callback->invoke = invoke;
    callback->destroy = DestroyCallback;
    callback->query = QueryNothing;
    callback->action = action;
    return callback;
}

static void *NewView(size_t size) {
    return ((ViewAllocate)Call(Native->viewAllocate))(*(void **)Call(Native->viewAllocator), size, 0);
}

static void Add(BYTE *parent, void *child, size_t childCountOffset) {
    ((AddChild)Call(Native->addChild))(parent, child, *(size_t *)(parent + childCountOffset));
}

static void SetSize(void *view, int width, int height) {
    struct {
        int width, height;
        BYTE present;
    } size = {width, height, 1};
    ((SetPreferredSize)Call(Native->setPreferredSize))(view, &size);
}

static void *NewBox(int orientation, Insets padding) {
    BYTE *view = NewView(VIEW_SIZE);
    ((ViewConstruct)Call(Native->viewConstruct))(view);

    BYTE *layout = ((OperatorNew)Call(Native->operatorNew))(LAYOUT_SIZE);
    ((ViewConstruct)Call(Native->layoutConstruct))(layout);
    *(void **)layout = Call(Native->boxVtables[0]);
    *(void **)(layout + 0x30) = Call(Native->boxVtables[1]);
    memset(layout + 0x100, 0, 0x30);
    *(int *)(layout + 0x100) = orientation;
    *(int *)(layout + Native->mainAlignOffset) = 3;
    *(int *)(layout + Native->crossAlignOffset) = 1;
    void *owned = layout;
    ((SetLayoutManager)Call(Native->setLayout))(view, &owned);

    BYTE *border = ((OperatorNew)Call(Native->operatorNew))(BORDER_SIZE);
    *(void **)border = Call(Native->borderVtable);
    *(DWORD *)(border + 8) = 0xffff0000;
    border[12] = 1;
    *(Insets *)(border + 16) = padding;
    void *ownedBorder = border;
    void **vtable = *(void ***)view;
    ((SetBorder)vtable[Native->setBorderOffset / 8])(view, &ownedBorder);
    return view;
}

static void ClosePopup(Popup *popup) {
    if (popup->widget) ((WidgetClose)Call(Native->widgetClose))(popup->widget, POPUP_REASON_CLOSED);
}

static void Retire(const Popup *popup) {
    if (RetiredCount == MAX_RETIRED) {
        ModLog("too many closed menus at once");
        return;
    }
    Retired[RetiredCount++] = *popup;
}

// Called once for each popup as its widget closes, with the widget still running.
static void OnClosed(Callback *state) {
    if (state->action == 0) {
        Retire(&Main);
        Main = (Popup){0};
        OwnerPending = TRUE;
        ClosePopup(&Picker);
    } else {
        Retire(&Picker);
        Picker = (Popup){0};
    }
}

static void ShowPopup(Popup *popup, void *view, void *anchor, void *parent, const RECT *at, int arrow, int shadow, BOOL main) {
    popup->popup = ((OperatorNew)Call(Native->operatorNew))(Native->popupSize);
    struct {
        int x, y, width, height;
    } rect = {at ? at->left : 0, at ? at->top : 0, 0, 0};
    ((PopupConstruct)Call(Native->popupConstruct))(popup->popup, anchor, parent, &rect, arrow, shadow, main, main);
    void *contents = view;
    ((PopupSetContents)Call(Native->popupSetContents))(popup->popup, &contents);
    void *closed = NewCallback(OnClosed, main ? 0 : 1);
    ((WidgetCreate)Call(Native->widgetCreate))(&popup->widget, popup->popup, &closed);
    ((WidgetPrepare)Call(Native->widgetPrepare))(popup->widget, popup->popup, view);
    ((WidgetShow)Call(Native->widgetShow))(popup->widget);
}

static void MarkSelectedColor(int color) {
    for (int i = 0; i < ColorCount; i++) {
        BYTE *button = Colors[i];
        button[Native->colorSelectedOffset] = *(int *)(button + Native->colorIdOffset) == color;
        ((Update)Call(Native->updateColor))(button);
        ((Update)Call(Native->schedulePaint))(button);
    }
}

static void OnColor(Callback *state) {
    Choices.color = state->action;
    Choices.pickColor(state->action);
    MarkSelectedColor(state->action);
}

static void FreeString(const BYTE *string) {
    if (!(string[23] & 0x80)) return;
    size_t capacity = *(const size_t *)(string + 16) & ~(1ull << 63);
    ((OperatorDelete)Call(Native->operatorDelete))(*(void *const *)string, capacity);
}

static void OnIconPicked(Callback *state, BYTE *key) {
    (void)state;
    const char *text = (const char *)key;
    size_t size = key[23];
    if (key[23] & 0x80) {
        text = *(const char *const *)key;
        size = *(const size_t *)(key + 8);
    }
    Choices.pickIcon(text, size);
    FreeString(key);
    ClosePopup(&Main);
}

static void OpenPicker(void *anchor) {
    if (Picker.widget) return;
    struct {
        const char *data;
        size_t size;
    } selected = {"", 0};
    if (Choices.icon) {
        selected.data = Choices.icon->key;
        selected.size = Choices.icon->keySize;
    }
    void *view = NewView(Native->pickerSize);
    ((PickerConstruct)Call(Native->pickerConstruct))(view, &selected, NewCallback(OnIconPicked, 0));
    ShowPopup(&Picker, view, anchor, Main.widget, NULL, 4, 6, FALSE);
}

static void OnItemHovered(Callback *state, void *view) {
    if (state->action == ACTION_CHANGE_ICON) OpenPicker(view);
    else ClosePopup(&Picker);
}

static void *ItemAnchors[8];
static const BYTE *Node;

static void OnItem(Callback *state) {
    switch (state->action) {
        case ACTION_CHANGE_ICON:
            OpenPicker(ItemAnchors[0]);
            return;
        case ACTION_RESET:
            Choices.color = -1;
            Choices.icon = NULL;
            Choices.reset();
            MarkSelectedColor(-1);
            return;
        case ACTION_NEW_FOLDER:
            NewFolderParent = Node;
            ClosePopup(&Main);
            return;
        default:
            Command = state->action;
            ClosePopup(&Main);
    }
}

static void *NewColorButton(int color) {
    BYTE *button = NewView(Native->colorSize);
    struct {
        void *state;
        ULONGLONG kind;
    } pressed = {NewCallback(OnColor, color), 1};
    ((ButtonConstruct)Call(Native->buttonConstruct))(button, &pressed);
    for (int i = 0; i < Native->colorVtableCount; i++)
        *(void **)(button + Native->colorVtables[i].offset) = Call(Native->colorVtables[i].vtable);
    *(int *)(button + Native->colorIdOffset) = color;
    *(void **)(button + Native->colorCallbackOffset) = NULL;
    button[Native->colorSelectedOffset] = color == Choices.color;
    SetSize(button, COLOR_SIZE, COLOR_SIZE);
    ((SetInt)Call(Native->setFocusable))(button, 1);
    button[Native->colorFlagOffset] = 1;
    *(void **)(button + Native->colorInkOffset) = NULL;
    ((Update)Call(Native->installHighlight))(button);
    ((Update)Call(Native->updateColor))(button);
    return button;
}

static void NewString(String16 *string, const wchar_t *text) {
    size_t size = wcslen(text);
    memset(string, 0, sizeof *string);
    if (size <= 10) {
        wmemcpy(string->small.data, text, size);
        string->small.size = (BYTE)size;
        return;
    }
    size_t capacity = (size + 8) & ~7ull;
    string->big.data = ((OperatorNew)Call(Native->operatorNew))(capacity * 2);
    wmemcpy(string->big.data, text, size + 1);
    string->big.size = size;
    string->big.capacity = capacity | (1ull << 63);
}

static void *NewItem(const wchar_t *label, DWORD icon, int action, BOOL arrow) {
    void *item = NewView(Native->itemSize);
    String16 text;
    NewString(&text, label);
    BYTE image[64] = {0};
    ((CreateImage)Call(Native->createImage))(image, Call(icon), Native->themedColorId, ICON_SIZE, NULL);
    image[40] = 1;  // optional<ImageModel>'s engaged flag: the constructor copies it from here.
    struct {
        void *state;
        ULONGLONG kind;
    } pressed = {NewCallback(OnItem, action), 1};
    ((ItemConstruct)Call(Native->itemConstruct))(item, &text, image, arrow, &pressed, NewCallback(OnItemHovered, action));
    ((DestroyImage)Call(Native->destroyImage))(image);
    return item;
}

void MenuInit(const Bindings *bindings, BYTE *base, DWORD childCountOffset) {
    ChildCountOffset = childCountOffset;
    Native = bindings;
    Base = base;
}

void MenuOpen(BYTE *row, BYTE *owner, const BYTE *node, POINT screen, const MenuChoices *choices) {
    ClosePopup(&Main);
    if (*(void **)Call(Native->viewAllocator) == NULL) return;
    Choices = *choices;
    Owner = owner;
    Command = 0;
    Node = node;
    NewFolderParent = NULL;

    // The same state Aside's own menu sets up, so its commands act on this folder.
    *(LONGLONG *)(owner + Native->ownerNodeIdOffset) = *(const LONGLONG *)(node + Native->nodeIdOffset);
    owner[Native->ownerHasNodeOffset] = 1;
    *(int *)(owner + Native->ownerCommandOffset) = 0;
    Generation = ++*(LONGLONG *)(owner + Native->ownerGenerationOffset);


    Insets none = {0, 0, 0, 0}, colors = {4, 8, 4, 8};
    BYTE *view = NewBox(ORIENTATION_VERTICAL, none);
    BYTE *palette = NewBox(0, colors);
    ColorCount = min(Native->colorCount, MAX_COLORS);
    const int *ids = (const int *)Call(Native->colorIds);
    for (int i = 0; i < ColorCount; i++) {
        Colors[i] = NewColorButton(ids[i]);
        Add(palette, Colors[i], ChildCountOffset);
    }
    Add(view, palette, ChildCountOffset);
    void *changeIcon = NewItem(L"Change icon", Native->changeIconIcon, ACTION_CHANGE_ICON, TRUE);
    ItemAnchors[0] = changeIcon;
    Add(view, changeIcon, ChildCountOffset);
    if (Choices.color >= 0 || Choices.icon) Add(view, NewItem(L"Reset appearance", Native->resetIcon, ACTION_RESET, FALSE), ChildCountOffset);
    if (Native->editorShow) Add(view, NewItem(L"New folder", Native->newFolderIcon, ACTION_NEW_FOLDER, FALSE), ChildCountOffset);
    Add(view, NewItem(L"Rename", Native->renameIcon, COMMAND_RENAME, FALSE), ChildCountOffset);
    Add(view, NewItem(L"Delete", Native->deleteIcon, COMMAND_DELETE, FALSE), ChildCountOffset);
    Add(view, NewItem(L"Manage Bookmarks", Native->manageIcon, COMMAND_MANAGE, FALSE), ChildCountOffset);

    void **vtable = *(void ***)row;
    void *widget = ((GetWidget)vtable[Native->widgetMethodOffset / 8])(row);
    RECT at = {screen.x, screen.y, screen.x, screen.y};
    ShowPopup(&Main, view, row, widget, &at, 0, 0, TRUE);
}

void MenuAskDeleteBookmark(BYTE *owner, const BYTE *node) {
    *(LONGLONG *)(owner + Native->bookmarkNodeIdOffset) = *(const LONGLONG *)(node + Native->nodeIdOffset);
    owner[Native->bookmarkHasNodeOffset] = 1;
    LONGLONG generation = ++*(LONGLONG *)(owner + Native->bookmarkGenerationOffset);
    *(int *)(owner + Native->bookmarkCommandOffset) = COMMAND_DELETE;
    ((MenuClosed)Call(Native->bookmarkMenuClosed))(owner, generation);
}

// Chromium's own editor, the one its bookmark bar opens for a new folder, set to put it at the end of this one.
static void OpenNewFolder(const BYTE *parent) {
    void *browser = *(void **)(Owner + Native->ownerBrowserOffset);
    if (!browser) {
        ModLog("new folder: the window has no browser");
        return;
    }
    void **vtable = *(void ***)browser;
    void *profile = ((BrowserGet)vtable[Native->browserProfileSlot])(browser);
    void *window = ((BrowserGet)vtable[Native->browserWindowSlot])(browser);
    if (!profile || !window) {
        ModLog("new folder: no profile or window");
        return;
    }
    vtable = *(void ***)window;
    void *nativeWindow = ((BrowserGet)vtable[Native->windowNativeSlot])(window);
    _Alignas(16) static BYTE details[EDIT_DETAILS_SIZE];
    ((EditAddFolder)Call(Native->editorAddFolder))(details, parent, *(const size_t *)(parent + Native->nodeChildrenOffset + 8));
    // Bound to do nothing, as Aside binds it for this.
    void **state = ((OperatorNew)Call(Native->operatorNew))(0x28);
    *(DWORD *)state = 1;
    state[1] = Call(Native->editorCallbackInvoke);
    state[2] = Call(Native->editorCallbackDestroy);
    state[3] = Call(Native->editorCallbackQuery);
    void *callback = state;
    ((ShowEditor)Call(Native->editorShow))(nativeWindow, profile, details, 0, &callback);
    ((EditDestroy)Call(Native->editorDetailsDestroy))(details);
}

void MenuTick(void) {
    for (int i = 0; i < RetiredCount; i++) {
        Popup popup = Retired[i];
        void **vtable = *(void ***)popup.widget;
        ((DeleteWidget)vtable[0])(popup.widget, 1);
        ((PopupDestruct)Call(Native->popupDestruct))(popup.popup);
        ((OperatorDelete)Call(Native->operatorDelete))(popup.popup, Native->popupSize);
    }
    RetiredCount = 0;
    if (!OwnerPending) return;
    OwnerPending = FALSE;
    // Aside's own close path: it checks the generation, then runs the command as a task.
    if (*(LONGLONG *)(Owner + Native->ownerGenerationOffset) != Generation) return;
    *(int *)(Owner + Native->ownerCommandOffset) = Command;
    ((MenuClosed)Call(Native->menuClosed))(Owner, Generation);
    if (NewFolderParent) {
        const BYTE *parent = NewFolderParent;
        NewFolderParent = NULL;
        OpenNewFolder(parent);
    }
}
