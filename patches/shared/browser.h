// The browser window's view, BrowserView, and how views lay out, found by name. Shared by the patches that change the window,
// and remembered for the build, so only the first of them does the work. Include it as "shared/browser.h".
#pragma once

#include "aside-late.h"

#define BROWSER_LAYOUT_TRACE "BrowserView::Layout"

typedef struct {
    BOOL found;
    DWORD vtable;            // BrowserView's.
    DWORD layout;            // BrowserView::Layout. LookupFunction gives its extent.
    int layoutSlot;          // View::Layout, in any view.
    DWORD boundsOffset;      // Where a view keeps its bounds.
    DWORD schedulePaint;     // View::SchedulePaint.
    DWORD setBounds;         // View::SetBoundsRect.
    DWORD needsLayoutOffset; // Layout stops the browser if this isn't set. 0 if this build differs.
} BrowserBindings;

// The function holding the trace is what BrowserView runs from its Layout slot, and that entry gives away its vtable.
static inline BOOL FindBrowser(const Lookup *lookup, BrowserBindings *browser) {
    if (LookupRecall(lookup, "browser/1", browser, sizeof *browser)) return browser->found;
    memset(browser, 0, sizeof *browser);
    const RUNTIME_FUNCTION *layout = LookupTraced(lookup, BROWSER_LAYOUT_TRACE);
    size_t count = 0;
    const Reference *entries = layout ? LookupPointers(lookup, layout->BeginAddress, &count) : NULL;
    if (count == 1 && LookupViewLayout(lookup, &browser->boundsOffset, &browser->schedulePaint, &browser->setBounds, &browser->layoutSlot)) {
        browser->layout = layout->BeginAddress;
        browser->vtable = entries[0].site - browser->layoutSlot * sizeof(void *);
        browser->needsLayoutOffset = LookupNeedsLayout(lookup, layout);
        browser->found = TRUE;
    }
    LookupRemember(lookup, "browser/1", browser, sizeof *browser);
    return browser->found;
}
