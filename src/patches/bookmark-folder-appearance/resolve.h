#pragma once

#include "mod/lookup.h"

// Everything this patch binds to in Aside, found by name or shape on every launch.
typedef struct {
    DWORD rowVtable;           // VerticalBookmarkRowView's primary vtable.
    int paintBackgroundSlot;   // View::OnPaintBackground in the vtable of any view.
    int backgroundPaintOffset; // Background::Paint in a Background's vtable.
    DWORD roundedBackground;   // views::CreateRoundedRectBackground.
    DWORD schedulePaint;       // View::SchedulePaint.
    DWORD boundsOffset;        // View::bounds_, read out of SchedulePaint.

    DWORD updateIcons;         // The row's own function that sets its icons.
    DWORD folderIcon, caretIcon;
    DWORD createImage, setImage, destroyImage;
    DWORD leadingOffset, caretOffset;  // The row's two image views.

    DWORD titleOffset, selectedOffset;
    DWORD setTitleColorOffset; // Label::SetEnabledColor in a Label's vtable.
    DWORD titleColorId;        // The native color of an unselected title.

    DWORD iconTable;           // Aside's table of folder icons.
    int iconCount;

    // How a right click reaches a row, and what the row's owner does with it.
    int mousePressedSlot;      // VerticalBookmarkRowView::OnMousePressed.
    DWORD eventFlagsOffset, eventLocationOffset;
    DWORD widgetMethodOffset;  // View::GetWidget in a view's vtable.
    DWORD convertToScreen;     // View::ConvertPointToScreen.
    DWORD rightClickOffset;    // The row's right click callback.
    DWORD rightClick;          // What the owner binds to it.
    DWORD nodeTypeOffset, nodeManagedOffset, nodeIdOffset;
    DWORD ownerCommandOffset, ownerNodeIdOffset, ownerHasNodeOffset, ownerGenerationOffset;
    DWORD menuClosed;          // Runs the command once a folder's menu has closed.

    // Parts of Aside's tab group menu, read out of the function that builds it.
    DWORD operatorNew, operatorDelete;
    DWORD viewAllocator, viewAllocate, viewConstruct;
    DWORD addChild, setPreferredSize, setLayout;
    DWORD layoutConstruct, boxVtables[2], crossAlignOffset, mainAlignOffset;
    DWORD borderVtable, setBorderOffset;

    DWORD colorIds;            // The menu's colors, in its order.
    int colorCount;
    DWORD colorSize, buttonConstruct, updateColor, setFocusable, installHighlight;
    DWORD colorIdOffset, colorCallbackOffset, colorSelectedOffset, colorFlagOffset, colorInkOffset;
    struct {
        DWORD offset, vtable;
    } colorVtables[16];
    int colorVtableCount;

    DWORD itemSize, itemConstruct;
    DWORD themedColorId;       // What the menu's own icons are colored with.

    DWORD popupSize, popupConstruct, popupDestruct, popupSetContents;
    DWORD widgetCreate, widgetPrepare, widgetShow, widgetClose;
    DWORD pickerSize, pickerConstruct;

    // By name, each falling back to the folder icon.
    DWORD changeIconIcon, resetIcon, renameIcon, deleteIcon, manageIcon;
} Bindings;

// One entry of the icon table.
typedef struct {
    const char *key;
    size_t keySize;
    const void *icon;
    const wchar_t *label;
    size_t labelSize;
} IconEntry;

// Returns NULL, or the name of the rule that didn't match.
const char *ResolveBindings(const Lookup *lookup, Bindings *bindings);
