// Finds things in Aside.dll by name. Works on the loaded DLL or a copy read from a file,
// so the same rules can be checked against any build with --scan.
#pragma once

#include <windows.h>
#include <stdlib.h>
#include <string.h>

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

BOOL LookupInit(Lookup *lookup, BYTE *base, ULONGLONG pointerBase);
void LookupFree(Lookup *lookup);

const RUNTIME_FUNCTION *LookupFunction(const Lookup *lookup, DWORD rva);
// Every copy of a NUL-terminated string in .rdata. Returns how many were found.
int LookupStrings(const Lookup *lookup, const char *string, DWORD *found, int max);
// All references to an RVA, as a range of the sorted table.
const Reference *LookupCode(const Lookup *lookup, DWORD target, size_t *count);
const Reference *LookupPointers(const Lookup *lookup, DWORD target, size_t *count);
const void *LookupRva(const Lookup *lookup, DWORD rva);
// The RVA a pointer in this copy points at, or 0 if it's outside the image.
DWORD LookupPointerRva(const Lookup *lookup, const void *at);

// A views class's primary vtable from its name, or 0.
DWORD LookupClassVtable(const Lookup *lookup, const char *className);
