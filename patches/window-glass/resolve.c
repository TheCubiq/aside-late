// Finds the browser's view and how it paints. Both are in patches/shared, which other patches use too.

#include "resolve.h"

#include "shared/browser.h"
#include "shared/canvas.h"

const char *ResolveBindings(const Lookup *lookup, Bindings *bindings) {
    BrowserBindings browser;
    if (!FindBrowser(lookup, &browser)) return "the browser's view";
    bindings->browserVtable = browser.vtable;
    bindings->layoutSlot = browser.layoutSlot;
    bindings->schedulePaint = browser.schedulePaint;

    CanvasBindings canvas;
    if (!FindCanvas(lookup, &canvas)) return "how a view paints";
    bindings->paintSlot = canvas.paintSlot;
    bindings->canvasPaintOffset = canvas.paintOffset;
    bindings->canvasConcatSlot = canvas.concatSlot;
    bindings->canvasRestoreSlot = canvas.restoreSlot;
    return NULL;
}
