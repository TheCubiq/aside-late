// What BrowserView::Layout does with the corners of the page and the top bar, found by the shape of its code and remembered
// for the build. Include it as "shared/corners.h", after "shared/browser.h".
#pragma once

#include "shared/browser.h"

typedef struct {
    BOOL found;
    // Where the browser keeps the view holding its pages, and that view its page containers.
    DWORD contentsOffset, containersOffset;
    // A page container's corners, and whether they've been set.
    DWORD radiiOffset, radiiSetOffset;
    DWORD applyRadii, clearRadii;
    DWORD topRadiusSite;     // Where the top bar's corners are set. 0 if this build differs.
} CornerBindings;

static inline DWORD CornersCallTarget(DWORD rva, const BYTE *p) {
    return rva + 5 + *(const INT32 *)(p + 1);
}

// Going through the page containers: mov rax,[rsi+contents]; ...; mov rcx,[rax+list+8]; lea rdx,[rcx*8]; test rdx,rdx; je; mov rdi,[rax+list]
static inline BOOL FindPageContainers(const Lookup *lookup, const RUNTIME_FUNCTION *layout, DWORD site, CornerBindings *corners) {
    for (DWORD rva = site; rva > layout->BeginAddress && rva + 0x100 > site; rva--) {
        const BYTE *p = lookup->base + rva;
        if (memcmp(p, "\x48\x8b\x88", 3) || memcmp(p + 7, "\x48\x8d\x14\xcd\x00\x00\x00\x00\x48\x85\xd2\x0f\x84", 13) || memcmp(p + 24, "\x48\x8b\xb8", 3)) continue;
        corners->containersOffset = *(const DWORD *)(p + 27);
        for (DWORD back = rva; back > layout->BeginAddress && back + 0x40 > rva; back--) {
            const BYTE *q = lookup->base + back;
            if (memcmp(q, "\x48\x8b\x86", 3)) continue;
            corners->contentsOffset = *(const DWORD *)(q + 3);
            return TRUE;
        }
    }
    return FALSE;
}

// For each page container: movdqu [rcx+radii],xmm0; cmp byte [rcx+set],0; jne; call clear; jmp; mov rdx,rbx; call apply
static inline BOOL FindPageRadii(const Lookup *lookup, const RUNTIME_FUNCTION *layout, CornerBindings *corners) {
    for (DWORD rva = layout->BeginAddress; rva + 32 <= layout->EndAddress; rva++) {
        const BYTE *p = lookup->base + rva;
        if (memcmp(p, "\xf3\x0f\x7f\x81", 4) || memcmp(p + 8, "\x80\xb9", 2) || p[14] != 0 || p[15] != 0x75 || p[17] != 0xe8) continue;
        if (p[22] != 0xeb || memcmp(p + 24, "\x48\x89\xda", 3) || p[27] != 0xe8) continue;
        corners->radiiOffset = *(const DWORD *)(p + 4);
        corners->radiiSetOffset = *(const DWORD *)(p + 10);
        corners->clearRadii = CornersCallTarget(rva + 17, p + 17);
        corners->applyRadii = CornersCallTarget(rva + 27, p + 27);
        return FindPageContainers(lookup, layout, rva, corners);
    }
    return FALSE;
}

// The top bar is clipped to a rounded rect, with its upper radius twice and its lower one twice, and then zeros:
// movss [r8],xmm15; movss [r8+4],xmm15; movss [r8+8],xmm14; movss [r8+0Ch],xmm14; movups [r8+10h],xmm0
static inline DWORD FindTopRadius(const Lookup *lookup, const RUNTIME_FUNCTION *layout) {
    static const BYTE stores[] = {0xf3, 0x45, 0x0f, 0x11, 0x38, 0xf3, 0x45, 0x0f, 0x11, 0x78, 0x04, 0xf3, 0x45, 0x0f, 0x11, 0x70, 0x08, 0xf3, 0x45, 0x0f, 0x11, 0x70, 0x0c, 0x41, 0x0f, 0x11, 0x40, 0x10};
    for (DWORD rva = layout->BeginAddress; rva + sizeof stores <= layout->EndAddress; rva++)
        if (!memcmp(lookup->base + rva, stores, sizeof stores)) return rva;
    return 0;
}

static inline BOOL FindCorners(const Lookup *lookup, const BrowserBindings *browser, CornerBindings *corners) {
    if (LookupRecall(lookup, "corners/1", corners, sizeof *corners)) return corners->found;
    memset(corners, 0, sizeof *corners);
    const RUNTIME_FUNCTION *layout = LookupFunction(lookup, browser->layout);
    if (layout && FindPageRadii(lookup, layout, corners)) {
        corners->topRadiusSite = FindTopRadius(lookup, layout);
        corners->found = TRUE;
    }
    LookupRemember(lookup, "corners/1", corners, sizeof *corners);
    return corners->found;
}
