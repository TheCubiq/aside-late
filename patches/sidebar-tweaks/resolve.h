// What's found in Aside on every launch.
#pragma once

#include "aside-late.h"

extern DWORD EvaluateRva, GetInputRva, HideOnBlurRva;
extern DWORD ControllerOffset, StateOffset, LeaveTimeOffset;
extern LONGLONG NativeLeaveUs;

// Returns NULL, or what it couldn't find.
const char *ResolveController(const Lookup *lookup);
