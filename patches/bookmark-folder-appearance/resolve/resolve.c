// Finds everything the patch binds to in Aside, by name or shape, on every launch. The rules are by what they're about:
// the other files here. This one runs them, and has what they all use.

#include "resolve/rules.h"

BOOL InImage(const Lookup *lookup, ULONGLONG value, DWORD *rva) {
    const IMAGE_NT_HEADERS64 *nt = (const void *)(lookup->base + ((const IMAGE_DOS_HEADER *)lookup->base)->e_lfanew);
    if (value < lookup->pointerBase || value >= lookup->pointerBase + nt->OptionalHeader.SizeOfImage) return FALSE;
    *rva = (DWORD)(value - lookup->pointerBase);
    return TRUE;
}

// A gfx::VectorIcon holds its own name: reps, rep count, name.
DWORD FindVectorIcon(const Lookup *lookup, const char *name) {
    DWORD strings[4], found = 0;
    if (LookupStrings(lookup, name, strings, 4) != 1) return 0;
    ULONGLONG wanted = lookup->pointerBase + strings[0];
    DWORD start = lookup->rdata->VirtualAddress + 16, end = lookup->rdata->VirtualAddress + lookup->rdata->Misc.VirtualSize - 8;
    for (DWORD rva = start; rva <= end; rva += 8) {
        if (*(const ULONGLONG *)(lookup->base + rva) != wanted) continue;
        if (*(const ULONGLONG *)(lookup->base + rva - 8) > 16) continue;
        if (found) return 0;
        found = rva - 16;
    }
    return found;
}

BOOL IsCall(const BYTE *p) {
    return p[0] == 0xe8;
}

DWORD CallTarget(DWORD rva, const BYTE *p) {
    return rva + 5 + *(const INT32 *)(p + 1);
}

// The next call at or after rva, with what it calls. Leaves rva just past it.
BOOL NextCall(const Lookup *lookup, DWORD *rva, DWORD end, DWORD *target) {
    for (; *rva + 5 <= end; ++*rva) {
        const BYTE *p = lookup->base + *rva;
        if (!IsCall(p)) continue;
        DWORD to = CallTarget(*rva, p);
        if (to < lookup->text->VirtualAddress || to >= lookup->text->VirtualAddress + lookup->text->Misc.VirtualSize) continue;
        // An 0xe8 can be part of another instruction. Real targets start a function, and only
        // small leaf functions have no unwind data, sitting after padding instead.
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, to);
        if (fn ? fn->BeginAddress != to : lookup->base[to - 1] != 0xcc) continue;
        *target = to;
        *rva += 5;
        return TRUE;
    }
    return FALSE;
}

DWORD FindBytes(const Lookup *lookup, DWORD start, DWORD end, const void *bytes, size_t size) {
    for (DWORD rva = start; rva + size <= end; rva++)
        if (!memcmp(lookup->base + rva, bytes, size)) return rva;
    return 0;
}

// The function holding a reference to a UTF-16 string, which is how Aside writes its own menu labels.
const RUNTIME_FUNCTION *FindByLabel(const Lookup *lookup, const wchar_t *label) {
    size_t size = wcslen(label) * 2 + 2;
    DWORD start = lookup->rdata->VirtualAddress, end = start + lookup->rdata->Misc.VirtualSize;
    for (DWORD rva = FindBytes(lookup, start, end, label, size); rva; rva = FindBytes(lookup, rva + 2, end, label, size)) {
        size_t count;
        const Reference *sites = LookupCode(lookup, rva, &count);
        if (count) return LookupFunction(lookup, sites[0].site);
    }
    return NULL;
}

const char *ResolveBindings(const Lookup *lookup, Bindings *bindings) {
    memset(bindings, 0, sizeof *bindings);
    if (!(bindings->rowVtable = LookupClassVtable(lookup, "VerticalBookmarkRowView"))) return "VerticalBookmarkRowView's vtable";
    DWORD paintBackground = 0;
    if (!LookupViewMethodSlot(lookup, "View::OnPaintBackground", &bindings->paintBackgroundSlot, &paintBackground)) return "View::OnPaintBackground's slot";
    if ((bindings->backgroundPaintOffset = FindBackgroundPaintOffset(lookup, paintBackground)) < 0) return "Background::Paint";
    if (!(bindings->roundedBackground = FindRoundedBackground(lookup))) return "CreateRoundedRectBackground";
    if (!(bindings->schedulePaint = FindSchedulePaint(lookup, &bindings->boundsOffset))) return "View::SchedulePaint";
    const char *missing = ResolveIcons(lookup, bindings);
    if (!missing) missing = ResolveTitle(lookup, bindings);
    if (!missing) missing = ResolveIconTable(lookup, bindings);
    if (!missing) missing = ResolveMenu(lookup, bindings);
    return missing;
}
