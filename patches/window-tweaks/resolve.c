// Finds how the browser window lays itself out and paints. Most of it is in patches/shared, which other patches use too.

#include "resolve.h"

#include "shared/browser.h"
#include "shared/canvas.h"
#include "shared/corners.h"

const char *ResolveBindings(const Lookup *lookup, Bindings *bindings) {
    if (!(bindings->topVtable = LookupClassVtable(lookup, "TopContainerView"))) return "TopContainerView";
    if (!(bindings->pageVtable = LookupClassVtable(lookup, "ContentsWebView"))) return "ContentsWebView";
    bindings->sidebarPartVtable = LookupClassVtable(lookup, "VerticalTabStripTopContainer");

    BrowserBindings browser;
    if (!FindBrowser(lookup, &browser)) return "the browser's layout";
    if (!browser.needsLayoutOffset) return "how a layout is asked for";
    bindings->browserVtable = browser.vtable;
    bindings->layoutSlot = browser.layoutSlot;
    bindings->boundsOffset = browser.boundsOffset;
    bindings->schedulePaint = browser.schedulePaint;
    bindings->setBounds = browser.setBounds;
    bindings->needsLayoutOffset = browser.needsLayoutOffset;

    CornerBindings corners;
    if (!FindCorners(lookup, &browser, &corners)) return "how the page's corners are set";
    bindings->contentsOffset = corners.contentsOffset;
    bindings->containersOffset = corners.containersOffset;
    bindings->radiiOffset = corners.radiiOffset;
    bindings->radiiSetOffset = corners.radiiSetOffset;
    bindings->applyRadii = corners.applyRadii;
    bindings->clearRadii = corners.clearRadii;

    CanvasBindings canvas;
    if (!FindCanvas(lookup, &canvas)) return "how a canvas moves";
    bindings->paintSlot = canvas.paintSlot;
    bindings->canvasPaintOffset = canvas.paintOffset;
    bindings->canvasSaveSlot = canvas.saveSlot;
    bindings->canvasConcatSlot = canvas.concatSlot;
    bindings->canvasRestoreSlot = canvas.restoreSlot;
    bindings->canvasAlphaSlot = canvas.alphaSlot;
    if (!(bindings->clipRect = FindClipRect(lookup, canvas.paintOffset))) return "how a canvas clips";

    DWORD method;
    if (!LookupViewMethodSlot(lookup, "View::OnPaintBackground", &bindings->paintBackgroundSlot, &method)) return "how views draw";
    return NULL;
}
