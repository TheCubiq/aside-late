// What a patch can use. A patch is a DLL built from the source in its folder, and it runs inside
// Aside with nothing held back.
#pragma once

#include "lookup.h"

#ifdef ASIDE_LATE_MOD
#define SDK __declspec(dllexport)
#else
#define SDK __declspec(dllimport)
#endif

// A patch exports these. Start runs once, off the UI thread, to find what it needs in a clean copy of
// Aside.dll that's gone afterwards. It returns what it couldn't find, or NULL. Enable and Disable run
// on the UI thread, and turn the patch on and off in the running browser, including what's already on
// screen. The hooks and code edits a patch made are undone for it just before Disable runs.
#define PATCH_START() __declspec(dllexport) const char *PatchStart(const Lookup *lookup, BYTE *base)
#define PATCH_ENABLE() __declspec(dllexport) void PatchEnable(void)
#define PATCH_DISABLE() __declspec(dllexport) void PatchDisable(void)
// Gets each option in patch.json before Enable, and any that changes while the patch is on. It runs
// where Enable does. Values are text: a choice's value, "true" or "false", or a number.
#define PATCH_OPTION() __declspec(dllexport) void PatchOption(const char *key, const char *value)

SDK void ModLog(const char *format, ...);
// A path inside aside-late's home folder, for a patch's own data.
SDK void HomeFile(const char *name, char *out, size_t size);

// Replaces one method on every object of a class, like swizzling, including objects that already
// exist. Hooks stack: *original is the next one down, and stays right as other patches' hooks come and
// go. A hook belongs to the module that asked for it.
SDK BOOL HookVtable(void **vtable, int slot, void *replacement, void **original);
// Replaces what one module calls when it uses a function it imports. Others are unaffected.
SDK BOOL HookImport(BYTE *module, const char *dll, const char *name, void *replacement, void **original);
// Writes over Aside's code, if it holds the bytes expected and no other patch has edited them. Editing it
// back to the original bytes takes the edit away.
SDK BOOL EditCode(void *at, const void *before, const void *after, size_t size);

// Runs a function on the browser's UI thread and waits for it.
SDK void RunOnUiThread(void (*function)(void *), void *argument);
// Finds objects of a class that already exist, by looking for its vtable in the heap. A result could
// be a stale copy, so check it looks right before using it. Returns how many were found.
SDK int FindObjects(const void *vtable, void **found, int max);
// Whether a pointer is to something inside Aside.dll, such as a vtable.
SDK BOOL IsInAside(const void *pointer);

// Reads memory that might not be valid.
SDK BOOL SafeRead(const void *from, void *to, size_t size);
