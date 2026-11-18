// How a view paints: its OnPaint slot, and the canvas it's handed. Found from one view whose OnPaint turns an image, and
// remembered for the build. Include it as "shared/canvas.h".
#pragma once

#include "aside-late.h"

typedef struct {
    BOOL found;
    int paintSlot;      // View::OnPaint.
    DWORD paintOffset;  // Where a gfx::Canvas keeps its cc::PaintCanvas.
    // Slots of cc::PaintCanvas.
    int saveSlot, concatSlot, restoreSlot;
    int alphaSlot;      // saveLayerAlphaf(float). 0 if this build differs.
} CanvasBindings;

static inline DWORD CanvasFindIn(const Lookup *lookup, DWORD start, DWORD end, const char *bytes, size_t size) {
    for (DWORD rva = start; rva + size <= end; rva++)
        if (!memcmp(lookup->base + rva, bytes, size)) return rva;
    return 0;
}

// The caret on a tab group turns its image in OnPaint: save; movsd xmm1,[rdi+progress]; mulsd; ...; concat; ImageView::OnPaint; restore.
static inline BOOL FindCanvas(const Lookup *lookup, CanvasBindings *canvas) {
    if (LookupRecall(lookup, "canvas/1", canvas, sizeof *canvas)) return canvas->found;
    memset(canvas, 0, sizeof *canvas);
    DWORD caret = LookupClassVtable(lookup, "TabExpandChevronView");
    const ULONGLONG *vtable = caret ? LookupRva(lookup, caret) : NULL;
    for (int slot = 0; vtable && slot < 512 && !canvas->found; slot++) {
        DWORD method = LookupPointerRva(lookup, vtable + slot);
        if (!method) break;
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, method);
        if (!fn || fn->BeginAddress != method || fn->EndAddress - method > 1024) continue;
        DWORD at = CanvasFindIn(lookup, method, fn->EndAddress, "\xf2\x0f\x10\x8f", 4);
        if (!at || memcmp(lookup->base + at + 8, "\xf2\x0f\x59\x0d", 4)) continue;
        DWORD paint = CanvasFindIn(lookup, method, at, "\x48\x8b\x8e", 3);
        DWORD save = CanvasFindIn(lookup, method, at, "\x48\x8b\x40", 3);
        DWORD concat = CanvasFindIn(lookup, at, fn->EndAddress, "\x48\x8b\x80", 3);
        DWORD restore = concat ? CanvasFindIn(lookup, concat, fn->EndAddress, "\x48\x8b\x40", 3) : 0;
        if (!paint || !save || !concat || !restore) break;
        canvas->paintSlot = slot;
        canvas->paintOffset = *(const DWORD *)(lookup->base + paint + 3);
        canvas->saveSlot = lookup->base[save + 3] / 8;
        canvas->concatSlot = *(const DWORD *)(lookup->base + concat + 3) / 8;
        canvas->restoreSlot = lookup->base[restore + 3] / 8;
        // Between those are the two saveLayers, one with a rect, the two with an alpha, and a last one.
        if (canvas->restoreSlot - canvas->saveSlot == 6) canvas->alphaSlot = canvas->restoreSlot - 2;
        canvas->found = TRUE;
    }
    LookupRemember(lookup, "canvas/1", canvas, sizeof *canvas);
    return canvas->found;
}

// Canvas::ClipRect: mov rcx,[rcx+paint]; then the Rect as an SkRect: movq xmm0,[rdx]; movq xmm1,[rdx+8]; paddd; ...
// It reads all of Aside's code, so it's only worth asking for when it's needed. 0 if it isn't there.
static inline DWORD FindClipRect(const Lookup *lookup, DWORD paintOffset) {
    struct {
        DWORD paintOffset, found;
    } kept;
    if (LookupRecall(lookup, "canvas.clip/1", &kept, sizeof kept) && kept.paintOffset == paintOffset) return kept.found;
    static const BYTE rect[] = {0xf3, 0x0f, 0x7e, 0x02, 0xf3, 0x0f, 0x7e, 0x4a, 0x08, 0x66, 0x0f, 0xfe, 0xc8, 0x66, 0x0f, 0x6c, 0xc1, 0x0f, 0x5b, 0xc0, 0x48, 0x8d, 0x54, 0x24};
    DWORD start = lookup->text->VirtualAddress, end = start + lookup->text->Misc.VirtualSize - 64, found = 0;
    for (DWORD rva = start; rva < end; rva++) {
        const BYTE *p = lookup->base + rva;
        if (p[0] != 0x48 || p[1] != 0x8b || p[2] != 0x89 || *(const DWORD *)(p + 3) != paintOffset || memcmp(p + 7, rect, sizeof rect)) continue;
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, rva);
        if (!fn || (found && found != fn->BeginAddress)) {
            found = 0;
            break;
        }
        found = fn->BeginAddress;
    }
    kept.paintOffset = paintOffset;
    kept.found = found;
    LookupRemember(lookup, "canvas.clip/1", &kept, sizeof kept);
    return found;
}
