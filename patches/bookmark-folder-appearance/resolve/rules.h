// What the rules share. resolve.h is what they find.
#pragma once

#include "resolve/resolve.h"

#define MAX_SLOTS 512

// resolve.c: what the rules all use.
BOOL InImage(const Lookup *lookup, ULONGLONG value, DWORD *rva);
DWORD FindVectorIcon(const Lookup *lookup, const char *name);
BOOL IsCall(const BYTE *p);
DWORD CallTarget(DWORD rva, const BYTE *p);
BOOL NextCall(const Lookup *lookup, DWORD *rva, DWORD end, DWORD *target);
DWORD FindBytes(const Lookup *lookup, DWORD start, DWORD end, const void *bytes, size_t size);
const RUNTIME_FUNCTION *FindByLabel(const Lookup *lookup, const wchar_t *label);

// views.c
DWORD FindRoundedBackground(const Lookup *lookup);
DWORD FindSchedulePaint(const Lookup *lookup, DWORD *boundsOffset);
int FindBackgroundPaintOffset(const Lookup *lookup, DWORD method);
const char *ResolveGroupColors(const Lookup *lookup, Bindings *bindings);
const char *ResolveColorProvider(const Lookup *lookup, Bindings *bindings);
const char *ResolveViewSize(const Lookup *lookup, Bindings *bindings);
const char *ResolveMargins(const Lookup *lookup, const RUNTIME_FUNCTION *fn, Bindings *bindings);
void ResolveCanvas(const Lookup *lookup, Bindings *bindings);

// row.c
const char *ResolveIcons(const Lookup *lookup, Bindings *bindings);
const char *ResolveTitle(const Lookup *lookup, Bindings *bindings);
const char *ResolveIconTable(const Lookup *lookup, Bindings *bindings);
const char *ResolveLayout(const Lookup *lookup, Bindings *bindings);
const char *ResolveIndent(const Lookup *lookup, Bindings *bindings);
const char *ResolveDrag(const Lookup *lookup, Bindings *bindings);
const char *ResolveCaret(const Lookup *lookup, Bindings *bindings);
const char *ResolveClose(const Lookup *lookup, Bindings *bindings);

// rebuild.c
const char *ResolveExpanded(const Lookup *lookup, Bindings *bindings);

// menu.c
const char *ResolveMenu(const Lookup *lookup, Bindings *bindings);
