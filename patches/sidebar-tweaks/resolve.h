// What's found in Aside on every launch.
#pragma once

#include "aside-late.h"

extern DWORD EvaluateRva, GetInputRva, HideOnBlurRva;
extern DWORD ControllerOffset, StateOffset, LeaveTimeOffset;
extern LONGLONG NativeLeaveUs;
// For showing the sidebar in fullscreen. RegionOffset is 0 if this build differs.
extern DWORD BrowserVtableRva, BoundsOffset, SchedulePaintRva, SetBoundsRva, RegionOffset, VisibleOffset, NeedsLayoutOffset;
extern int LayoutSlot, SetVisibleSlot;

// Returns NULL, or what it couldn't find.
const char *ResolveController(const Lookup *lookup);
