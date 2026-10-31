#pragma once

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Asks before terminating an Aside already running on this profile. FALSE means don't launch.
BOOL ReadyToLaunch(const wchar_t *userDataDir, const wchar_t *home);
int Launch(const wchar_t *exe, wchar_t *commandLine, const wchar_t *skipped);
