// Finds the browser's view, from patches/shared, and the views its layout checks are showing, among which the sidebar's.

#include "resolve.h"

#include "shared/browser.h"

// The layout asks whether a view is showing, the sidebar's among others:
// mov rax,[rsi+view]; test rax,rax; je; cmp byte [rax+visible],1; jne
// What follows changed between builds, and the sidebar's isn't the only one of this shape any more, so every field asked
// about this way is kept, and the patch sees which is the sidebar.
static void FindRegions(const Lookup *lookup, const RUNTIME_FUNCTION *layout, Bindings *bindings) {
    for (DWORD rva = layout->BeginAddress; rva + 24 <= layout->EndAddress && bindings->regionCount < MAX_REGIONS; rva++) {
        const BYTE *p = lookup->base + rva;
        if (memcmp(p, "\x48\x8b\x86", 3) || memcmp(p + 7, "\x48\x85\xc0", 3)) continue;
        const BYTE *q = p + 10;
        if (q[0] == 0x74) q += 2;
        else if (q[0] == 0x0f && q[1] == 0x84) q += 6;
        else continue;
        if (q[0] != 0x80 || q[1] != 0xb8 || *(const DWORD *)(q + 2) != bindings->visibleOffset || q[6] != 1) continue;
        DWORD offset = *(const DWORD *)(p + 3);
        BOOL known = FALSE;
        for (int i = 0; i < bindings->regionCount; i++) known |= bindings->regionOffsets[i] == offset;
        if (!known) bindings->regionOffsets[bindings->regionCount++] = offset;
    }
}

const char *ResolveBindings(const Lookup *lookup, Bindings *bindings) {
    BrowserBindings browser;
    if (!FindBrowser(lookup, &browser) || !browser.needsLayoutOffset) return "the browser's layout";
    bindings->browserVtable = browser.vtable;
    bindings->layoutSlot = browser.layoutSlot;
    bindings->boundsOffset = browser.boundsOffset;
    bindings->schedulePaint = browser.schedulePaint;
    bindings->setBounds = browser.setBounds;
    bindings->needsLayoutOffset = browser.needsLayoutOffset;
    if (!LookupVisibility(lookup, &bindings->setVisibleSlot, &bindings->visibleOffset)) return "how views are shown";
    const RUNTIME_FUNCTION *layout = LookupFunction(lookup, browser.layout);
    if (layout) FindRegions(lookup, layout, bindings);
    return bindings->regionCount ? NULL : "where the browser keeps the sidebar";
}
