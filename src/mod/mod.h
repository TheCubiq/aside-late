// The DLL side: patches that need to run code inside Aside, rather than only edit it.
#pragma once

#include "lookup.h"

typedef void (*ModStart)(const Lookup *lookup, BYTE *base);
void RegisterMod(const char *id, ModStart start);

#define MOD(id, start) \
    __attribute__((constructor)) static void RegisterThisMod(void) { RegisterMod(id, start); }

void ModLog(const char *format, ...);

// Replaces one method on every object of a class, like swizzling.
BOOL SwapVtableEntry(void **vtable, int slot, void *replacement, void **original);
// Replaces what one module calls when it uses a function it imports. Others are unaffected.
BOOL HookImport(BYTE *module, const char *dll, const char *name, void *replacement, void **original);
// Reads memory that might not be valid. Used only while learning an object's shape.
BOOL SafeRead(const void *from, void *to, size_t size);
