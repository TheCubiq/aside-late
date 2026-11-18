// What's found in Aside on every launch.
#pragma once

#include "aside-late.h"

typedef struct {
    DWORD browserVtable;
    int layoutSlot;
    DWORD boundsOffset, schedulePaint, setBounds, needsLayoutOffset;
    int setVisibleSlot;      // View::SetVisible.
    DWORD visibleOffset;     // Where a view keeps whether it's visible.
    DWORD regionOffset;      // Where the browser keeps the sidebar's view.
} Bindings;

// Returns NULL, or what it couldn't find.
const char *ResolveBindings(const Lookup *lookup, Bindings *bindings);
