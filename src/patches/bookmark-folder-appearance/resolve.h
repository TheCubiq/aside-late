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
} Bindings;

// Returns NULL, or the name of the rule that didn't match.
const char *ResolveBindings(const Lookup *lookup, Bindings *bindings);
