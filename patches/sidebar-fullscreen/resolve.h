// What's found in Aside on every launch.
#pragma once

#include "aside-late.h"

#define MAX_REGIONS 6

typedef struct {
    DWORD browserVtable;
    int layoutSlot;
    DWORD boundsOffset, schedulePaint, setBounds, needsLayoutOffset;
    int setVisibleSlot;      // View::SetVisible.
    DWORD visibleOffset;     // Where a view keeps whether it's visible.
    // Where the browser may keep the sidebar's view: each view the layout checks this way. Which one it is, is seen
    // at runtime, as the one the full height of the window at its edge.
    DWORD regionOffsets[MAX_REGIONS];
    int regionCount;
} Bindings;

// Returns NULL, or what it couldn't find.
const char *ResolveBindings(const Lookup *lookup, Bindings *bindings);
