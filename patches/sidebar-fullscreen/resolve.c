// Finds the browser's view, from patches/shared, and where its layout keeps the sidebar, by the shape of the code around it.

#include "resolve.h"

#include "shared/browser.h"

// The layout asks if the sidebar's view is showing, and then the same of the horizontal tab strip:
// mov rax,[rsi+region]; test rax,rax; je; cmp byte [rax+visible],1; jne; mov rax,[rsi+x]; mov r14b,1
static DWORD FindRegion(const Lookup *lookup, const RUNTIME_FUNCTION *layout, DWORD visibleOffset) {
    for (DWORD rva = layout->BeginAddress; rva + 40 <= layout->EndAddress; rva++) {
        const BYTE *p = lookup->base + rva;
        if (memcmp(p, "\x48\x8b\x86", 3) || memcmp(p + 7, "\x48\x85\xc0\x0f\x84", 5) || memcmp(p + 16, "\x80\xb8", 2)) continue;
        if (*(const DWORD *)(p + 18) != visibleOffset || p[22] != 1 || memcmp(p + 23, "\x0f\x85", 2) || memcmp(p + 29, "\x48\x8b\x86", 3) || memcmp(p + 36, "\x41\xb6\x01", 3)) continue;
        return *(const DWORD *)(p + 3);
    }
    return 0;
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
    if (!layout || !(bindings->regionOffset = FindRegion(lookup, layout, bindings->visibleOffset))) return "where the browser keeps the sidebar";
    return NULL;
}
