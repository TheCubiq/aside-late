// Finds things in Aside.dll by name. Works on a copy read from a file, so what it reads is never
// something a patch has already hooked, and the same rules can be checked against any build.
#pragma once

#include <windows.h>
#include <stdlib.h>
#include <string.h>

#ifndef SDK
#ifdef ASIDE_LATE_MOD
#define SDK __declspec(dllexport)
#else
#define SDK __declspec(dllimport)
#endif
#endif

typedef struct {
    DWORD target, site;
} Reference;

typedef struct {
    BYTE *base;
    ULONGLONG pointerBase;  // What an absolute pointer to RVA 0 looks like in this copy.
    const IMAGE_SECTION_HEADER *text, *rdata, *pdata;
    Reference *code;        // Every lea target in .text, sorted.
    size_t codeCount;
    Reference *pointers;    // Every pointer into .text from .rdata, sorted.
    size_t pointerCount;
} Lookup;

SDK const RUNTIME_FUNCTION *LookupFunction(const Lookup *lookup, DWORD rva);
// Every copy of a NUL-terminated string in .rdata. Returns how many were found.
SDK int LookupStrings(const Lookup *lookup, const char *string, DWORD *found, int max);
// All references to an RVA, as a range of the sorted table.
SDK const Reference *LookupCode(const Lookup *lookup, DWORD target, size_t *count);
SDK const Reference *LookupPointers(const Lookup *lookup, DWORD target, size_t *count);
SDK const void *LookupRva(const Lookup *lookup, DWORD rva);
// The RVA a pointer in this copy points at, or 0 if it's outside the image.
SDK DWORD LookupPointerRva(const Lookup *lookup, const void *at);

// A views class's primary vtable from its name, or 0.
SDK DWORD LookupClassVtable(const Lookup *lookup, const char *className);
// The vtable a constructor installs for its own class: the first one it stores in its object, or 0.
SDK DWORD LookupVtableInstalledBy(const Lookup *lookup, const RUNTIME_FUNCTION *constructor);
// A method of views::View, and its slot in every view's vtable, from the name of a trace inside it.
SDK BOOL LookupViewMethodSlot(const Lookup *lookup, const char *trace, int *slot, DWORD *method);
// How every view is shown and hidden, worked out from the many places doing
// if (visible != x) SetVisible(x). FALSE if the build gives no clear answer.
SDK BOOL LookupVisibility(const Lookup *lookup, int *setVisibleSlot, DWORD *visibleOffset);
