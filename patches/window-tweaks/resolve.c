// Finds how the browser window lays itself out, by name and by the shape of the code around it.

#include "resolve.h"

#define LAYOUT_TRACE "BrowserView::Layout"

static BOOL IsCall(const BYTE *p) {
    return p[0] == 0xe8;
}

static DWORD CallTarget(DWORD rva, const BYTE *p) {
    return rva + 5 + *(const INT32 *)(p + 1);
}

// The function holding the trace is what BrowserView runs from its Layout slot, and that entry gives away its vtable.
static const RUNTIME_FUNCTION *FindLayout(const Lookup *lookup, Bindings *bindings) {
    const RUNTIME_FUNCTION *fn = LookupTraced(lookup, LAYOUT_TRACE);
    if (!fn) return NULL;
    size_t count;
    const Reference *entries = LookupPointers(lookup, fn->BeginAddress, &count);
    if (count != 1) return NULL;
    bindings->browserVtable = entries[0].site - bindings->layoutSlot * sizeof(void *);
    return fn;
}

// Going through the page containers: mov rax,[rsi+contents]; ...; mov rcx,[rax+list+8]; lea rdx,[rcx*8]; test rdx,rdx; je; mov rdi,[rax+list]
static BOOL FindContainers(const Lookup *lookup, const RUNTIME_FUNCTION *layout, DWORD site, Bindings *bindings) {
    for (DWORD rva = site; rva > layout->BeginAddress && rva + 0x100 > site; rva--) {
        const BYTE *p = lookup->base + rva;
        if (memcmp(p, "\x48\x8b\x88", 3) || memcmp(p + 7, "\x48\x8d\x14\xcd\x00\x00\x00\x00\x48\x85\xd2\x0f\x84", 13) || memcmp(p + 24, "\x48\x8b\xb8", 3)) continue;
        bindings->containersOffset = *(const DWORD *)(p + 27);
        for (DWORD back = rva; back > layout->BeginAddress && back + 0x40 > rva; back--) {
            const BYTE *q = lookup->base + back;
            if (memcmp(q, "\x48\x8b\x86", 3)) continue;
            bindings->contentsOffset = *(const DWORD *)(q + 3);
            return TRUE;
        }
    }
    return FALSE;
}

// For each page container: movdqu [rcx+radii],xmm0; cmp byte [rcx+set],0; jne; call clear; jmp; mov rdx,rbx; call apply
static BOOL FindRadii(const Lookup *lookup, const RUNTIME_FUNCTION *layout, Bindings *bindings) {
    for (DWORD rva = layout->BeginAddress; rva + 32 <= layout->EndAddress; rva++) {
        const BYTE *p = lookup->base + rva;
        if (memcmp(p, "\xf3\x0f\x7f\x81", 4) || memcmp(p + 8, "\x80\xb9", 2) || p[14] != 0 || p[15] != 0x75 || !IsCall(p + 17)) continue;
        if (p[22] != 0xeb || memcmp(p + 24, "\x48\x89\xda", 3) || !IsCall(p + 27)) continue;
        bindings->radiiOffset = *(const DWORD *)(p + 4);
        bindings->radiiSetOffset = *(const DWORD *)(p + 10);
        bindings->clearRadii = CallTarget(rva + 17, p + 17);
        bindings->applyRadii = CallTarget(rva + 27, p + 27);
        return FindContainers(lookup, layout, rva, bindings);
    }
    return FALSE;
}

// The top bar is clipped to a rounded rect, with its upper radius twice and its lower one twice, and then zeros:
// movss [r8],xmm15; movss [r8+4],xmm15; movss [r8+8],xmm14; movss [r8+0Ch],xmm14; movups [r8+10h],xmm0
static DWORD FindTopRadius(const Lookup *lookup, const RUNTIME_FUNCTION *layout) {
    static const BYTE stores[] = {0xf3, 0x45, 0x0f, 0x11, 0x38, 0xf3, 0x45, 0x0f, 0x11, 0x78, 0x04, 0xf3, 0x45, 0x0f, 0x11, 0x70, 0x08, 0xf3, 0x45, 0x0f, 0x11, 0x70, 0x0c, 0x41, 0x0f, 0x11, 0x40, 0x10};
    for (DWORD rva = layout->BeginAddress; rva + sizeof stores <= layout->EndAddress; rva++)
        if (!memcmp(lookup->base + rva, stores, sizeof stores)) return rva;
    return 0;
}

// The caret on a tab group turns its image in OnPaint: save; movsd xmm1,[rdi+progress]; mulsd; ...; concat; ImageView::OnPaint; restore.
static DWORD FindIn(const Lookup *lookup, DWORD start, DWORD end, const char *bytes, size_t size) {
    for (DWORD rva = start; rva + size <= end; rva++)
        if (!memcmp(lookup->base + rva, bytes, size)) return rva;
    return 0;
}

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
        DWORD save = FindIn(lookup, method, at, "\x48\x8b\x40", 3);
        DWORD concat = FindIn(lookup, at, fn->EndAddress, "\x48\x8b\x80", 3);
        DWORD restore = concat ? FindIn(lookup, concat, fn->EndAddress, "\x48\x8b\x40", 3) : 0;
        if (!canvas || !save || !concat || !restore) return FALSE;
        bindings->paintSlot = slot;
        bindings->canvasPaintOffset = *(const DWORD *)(lookup->base + canvas + 3);
        bindings->canvasSaveSlot = lookup->base[save + 3] / 8;
        bindings->canvasConcatSlot = *(const DWORD *)(lookup->base + concat + 3) / 8;
        bindings->canvasRestoreSlot = lookup->base[restore + 3] / 8;
        // Between those are the two saveLayers, one with a rect, the two with an alpha, and a last one.
        if (bindings->canvasRestoreSlot - bindings->canvasSaveSlot == 6) bindings->canvasAlphaSlot = bindings->canvasRestoreSlot - 2;
        return TRUE;
    }
    return FALSE;
}

// Canvas::ClipRect: mov rcx,[rcx+paint]; then the Rect as an SkRect: movq xmm0,[rdx]; movq xmm1,[rdx+8]; paddd; ...
static DWORD FindClipRect(const Lookup *lookup, DWORD paintOffset) {
    static const BYTE rect[] = {0xf3, 0x0f, 0x7e, 0x02, 0xf3, 0x0f, 0x7e, 0x4a, 0x08, 0x66, 0x0f, 0xfe, 0xc8, 0x66, 0x0f, 0x6c, 0xc1, 0x0f, 0x5b, 0xc0, 0x48, 0x8d, 0x54, 0x24};
    DWORD start = lookup->text->VirtualAddress, end = start + lookup->text->Misc.VirtualSize - 64, found = 0;
    for (DWORD rva = start; rva < end; rva++) {
        const BYTE *p = lookup->base + rva;
        if (p[0] != 0x48 || p[1] != 0x8b || p[2] != 0x89 || *(const DWORD *)(p + 3) != paintOffset || memcmp(p + 7, rect, sizeof rect)) continue;
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, rva);
        if (!fn || (found && found != fn->BeginAddress)) return 0;
        found = fn->BeginAddress;
    }
    return found;
}

const char *ResolveBindings(const Lookup *lookup, Bindings *bindings) {
    if (!(bindings->topVtable = LookupClassVtable(lookup, "TopContainerView"))) return "TopContainerView";
    if (!(bindings->pageVtable = LookupClassVtable(lookup, "ContentsWebView"))) return "ContentsWebView";
    bindings->sidebarPartVtable = LookupClassVtable(lookup, "VerticalTabStripTopContainer");
    if (!LookupViewLayout(lookup, &bindings->boundsOffset, &bindings->schedulePaint, &bindings->setBounds, &bindings->layoutSlot)) return "how views lay out";
    const RUNTIME_FUNCTION *layout = FindLayout(lookup, bindings);
    if (!layout) return "the browser's layout";
    if (!(bindings->needsLayoutOffset = LookupNeedsLayout(lookup, layout))) return "how a layout is asked for";
    if (!FindRadii(lookup, layout, bindings)) return "how the page's corners are set";
    bindings->topRadiusSite = FindTopRadius(lookup, layout);
    if (!FindCanvas(lookup, bindings)) return "how a canvas moves";
    if (!(bindings->clipRect = FindClipRect(lookup, bindings->canvasPaintOffset))) return "how a canvas clips";
    DWORD method;
    if (!LookupViewMethodSlot(lookup, "View::OnPaintBackground", &bindings->paintBackgroundSlot, &method)) return "how views draw";
    return NULL;
}
