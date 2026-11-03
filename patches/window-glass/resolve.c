// Finds the browser's view and how a view paints, by name and by the shape of the code around it. The same rules as
// window-tweaks.

#include "resolve.h"

#define LAYOUT_TRACE "BrowserView::Layout"

// The function holding the trace is what BrowserView runs from its Layout slot, and that entry gives away its vtable.
static BOOL FindBrowser(const Lookup *lookup, Bindings *bindings) {
    const RUNTIME_FUNCTION *fn = LookupTraced(lookup, LAYOUT_TRACE);
    if (!fn) return FALSE;
    size_t count;
    const Reference *entries = LookupPointers(lookup, fn->BeginAddress, &count);
    if (count != 1) return FALSE;
    bindings->browserVtable = entries[0].site - bindings->layoutSlot * sizeof(void *);
    return TRUE;
}

static DWORD FindIn(const Lookup *lookup, DWORD start, DWORD end, const char *bytes, size_t size) {
    for (DWORD rva = start; rva + size <= end; rva++)
        if (!memcmp(lookup->base + rva, bytes, size)) return rva;
    return 0;
}

// The caret on a tab group turns its image in OnPaint: save; movsd xmm1,[rdi+progress]; mulsd; ...; concat; ImageView::OnPaint; restore.
static BOOL FindCanvas(const Lookup *lookup, Bindings *bindings) {
    DWORD caret = LookupClassVtable(lookup, "TabExpandChevronView");
    if (!caret) return FALSE;
    const ULONGLONG *vtable = LookupRva(lookup, caret);
    for (int slot = 0; slot < 512; slot++) {
        DWORD method = LookupPointerRva(lookup, vtable + slot);
        if (!method) break;
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, method);
        if (!fn || fn->BeginAddress != method || fn->EndAddress - method > 1024) continue;
        DWORD at = FindIn(lookup, method, fn->EndAddress, "\xf2\x0f\x10\x8f", 4);
        if (!at || memcmp(lookup->base + at + 8, "\xf2\x0f\x59\x0d", 4)) continue;
        DWORD canvas = FindIn(lookup, method, at, "\x48\x8b\x8e", 3);
        DWORD concat = FindIn(lookup, at, fn->EndAddress, "\x48\x8b\x80", 3);
        DWORD restore = concat ? FindIn(lookup, concat, fn->EndAddress, "\x48\x8b\x40", 3) : 0;
        if (!canvas || !concat || !restore) return FALSE;
        bindings->paintSlot = slot;
        bindings->canvasPaintOffset = *(const DWORD *)(lookup->base + canvas + 3);
        bindings->canvasConcatSlot = *(const DWORD *)(lookup->base + concat + 3) / 8;
        bindings->canvasRestoreSlot = lookup->base[restore + 3] / 8;
        return TRUE;
    }
    return FALSE;
}

const char *ResolveBindings(const Lookup *lookup, Bindings *bindings) {
    DWORD boundsOffset, setBounds;
    if (!LookupViewLayout(lookup, &boundsOffset, &bindings->schedulePaint, &setBounds, &bindings->layoutSlot)) return "how views lay out";
    if (!FindBrowser(lookup, bindings)) return "the browser's view";
    if (!FindCanvas(lookup, bindings)) return "how a view paints";
    return NULL;
}
