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
