// What's found in Aside on every launch.
#pragma once

#include "aside-late.h"

typedef struct {
    DWORD browserVtable;
    DWORD schedulePaint;     // View::SchedulePaint.
    int layoutSlot;          // View::Layout, in any view. Only used to find the browser's vtable.
    int paintSlot;           // View::OnPaint.
    DWORD canvasPaintOffset; // Where a gfx::Canvas keeps its cc::PaintCanvas.
    int canvasRestoreSlot, canvasConcatSlot;
} Bindings;

// Returns NULL, or what it couldn't find.
const char *ResolveBindings(const Lookup *lookup, Bindings *bindings);
