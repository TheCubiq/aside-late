// What's found in Aside on every launch.
#pragma once

#include "aside-late.h"

typedef struct {
    DWORD browserVtable, topVtable, pageVtable;
    DWORD sidebarPartVtable; // The header of the sidebar, whose own view has no name to find. 0 if this build differs.
    DWORD boundsOffset;
    DWORD setBounds;         // View::SetBoundsRect.
    DWORD schedulePaint;     // View::SchedulePaint.
    int layoutSlot;          // View::Layout, in any view.
    DWORD needsLayoutOffset; // Layout stops the browser if this isn't set.
    // In that function: where the browser keeps the view holding its pages, and that view its page containers.
    DWORD contentsOffset, containersOffset;
    // What it does with a page container's corners.
    DWORD radiiOffset, radiiSetOffset;
    DWORD applyRadii, clearRadii;
    DWORD topRadiusSite;     // Where the top bar's corners are set. 0 if this build differs.
    // How a view paints, and how a canvas is moved for that.
    int paintSlot, paintBackgroundSlot;
    DWORD canvasPaintOffset, clipRect;
    int canvasSaveSlot, canvasConcatSlot, canvasRestoreSlot;
    int canvasAlphaSlot;     // PaintCanvas::saveLayerAlphaf(float). 0 if this build differs.
} Bindings;

// Returns NULL, or what it couldn't find.
const char *ResolveBindings(const Lookup *lookup, Bindings *bindings);
