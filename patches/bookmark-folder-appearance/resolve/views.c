// What any view, canvas or background does, and Aside's theme colors.

#include "resolve/rules.h"

// The Background's insets and color, written in this order right after it's allocated: mov [rax+N..N+0Ch],<int>; mov [rax+8],<color>
// N was 20h, and is 24h since a flag went in at 20h.
static BOOL IsRoundedBackgroundStores(const BYTE *p) {
    BYTE first = p[2];
    if (first != 0x20 && first != 0x24) return FALSE;
    for (int i = 0; i < 4; i++)
        if (p[i * 3] != 0x89 || (p[i * 3 + 1] & 0xc7) != 0x40 || p[i * 3 + 2] != first + i * 4) return FALSE;
    return (p[12] & 0xf8) == 0x48 && p[13] == 0x89 && (p[14] & 0xc7) == 0x40 && p[15] == 0x08;
}

// It also stores the radii it was given, all four at once: movups [rax+10h],xmm. Another factory makes the same class with none.
static BOOL StoresRadii(const Lookup *lookup, const RUNTIME_FUNCTION *fn) {
    for (DWORD rva = fn->BeginAddress; rva + 4 <= fn->EndAddress; rva++) {
        const BYTE *p = lookup->base + rva;
        if (p[0] == 0x0f && p[1] == 0x11 && (p[2] & 0xc7) == 0x40 && p[3] == 0x10) return TRUE;
    }
    return FALSE;
}

DWORD FindRoundedBackground(const Lookup *lookup) {
    DWORD start = lookup->text->VirtualAddress, end = start + lookup->text->Misc.VirtualSize - 16;
    DWORD found = 0;
    for (DWORD rva = start; rva < end; rva++) {
        const BYTE *p = lookup->base + rva;
        if (p[0] != 0x89 || !IsRoundedBackgroundStores(p)) continue;
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, rva);
        if (!fn || !StoresRadii(lookup, fn)) continue;
        if (found && found != fn->BeginAddress) return 0;
        found = fn->BeginAddress;
    }
    return found;
}

// SchedulePaint builds gfx::Rect(size()) on the stack and asks if the view has a custom
// Paint: mov qword [rsp+x],0; mov rax,[rcx+size]; mov [rsp+y],rax; mov rcx,[rcx]; mov rax,[rcx+50h]
DWORD FindSchedulePaint(const Lookup *lookup, DWORD *boundsOffset) {
    static const BYTE tail[] = {0x48, 0x8b, 0x09, 0x48, 0x8b, 0x41, 0x50};
    DWORD start = lookup->text->VirtualAddress, end = start + lookup->text->Misc.VirtualSize - 32;
    DWORD found = 0;
    for (DWORD rva = start; rva < end; rva++) {
        const BYTE *p = lookup->base + rva;
        if (p[0] != 0x48 || p[1] != 0xc7 || p[2] != 0x44 || p[3] != 0x24 || *(const DWORD *)(p + 5)) continue;
        if (p[9] != 0x48 || p[10] != 0x8b || p[11] != 0x81) continue;
        if (p[16] != 0x48 || p[17] != 0x89 || p[18] != 0x44 || p[19] != 0x24 || memcmp(p + 21, tail, sizeof tail)) continue;
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, rva);
        if (!fn || rva - fn->BeginAddress > 64) continue;
        if (found) return 0;
        found = fn->BeginAddress;
        *boundsOffset = *(const DWORD *)(p + 12) - 8;
    }
    return found;
}

// OnPaintBackground's only job is background_->Paint(canvas, this):
// mov rax,[rcx]; mov rax,[rax+slot]
int FindBackgroundPaintOffset(const Lookup *lookup, DWORD method) {
    const RUNTIME_FUNCTION *fn = LookupFunction(lookup, method);
    if (!fn) return -1;
    for (DWORD rva = fn->BeginAddress; rva + 7 <= fn->EndAddress; rva++) {
        const BYTE *p = lookup->base + rva;
        if (!memcmp(p, "\x48\x8b\x01\x48\x8b\x40", 6)) return p[6];
    }
    return -1;
}

// A switch from a group color to its background's color ID:
// cmp ecx,9; ja; mov eax,ecx; lea r8,[table]; movsxd rax,[r8+rax*4]; add rax,r8; jmp rax
// where each case is mov eax,<id>; ret.
const char *ResolveGroupColors(const Lookup *lookup, Bindings *bindings) {
    DWORD start = lookup->text->VirtualAddress, end = start + lookup->text->Misc.VirtualSize - 32;
    BOOL found = FALSE;
    for (DWORD rva = start; rva < end; rva++) {
        const BYTE *p = lookup->base + rva;
        if (memcmp(p, "\x83\xf9\x09\x77", 4) || memcmp(p + 5, "\x89\xc8\x4c\x8d\x05", 5)) continue;
        DWORD table = rva + 7 + 7 + *(const INT32 *)(p + 10);
        if (!LookupRva(lookup, table)) continue;
        DWORD ids[GROUP_COLOR_COUNT] = {0}, highest = 0;
        int count = 0;
        for (int i = 0; i < GROUP_COLOR_COUNT; i++) {
            DWORD target = table + *(const INT32 *)(lookup->base + table + i * 4);
            if (target < start || target >= end) continue;
            const BYTE *q = lookup->base + target;
            if (q[0] != 0xb8 || q[5] != 0xc3) continue;
            ids[i] = *(const DWORD *)(q + 1);
            highest = max(highest, ids[i]);
            count++;
        }
        if (count < 8) continue;
        if (found) return "the group colors (more than one match)";
        found = TRUE;
        memcpy(bindings->groupBackgrounds, ids, sizeof ids);
        bindings->groupForeground = highest + 1;
    }
    return found ? NULL : "the group colors";
}

// In Background::Paint, for a color that's an ID:
// mov rax,[rcx+theme]; add rcx,theme; mov rax,[rax+provider]; ...; mov edx,edi; call GetColor
static BOOL ReadColorProvider(const Lookup *lookup, DWORD paint, Bindings *bindings) {
    const RUNTIME_FUNCTION *fn = LookupFunction(lookup, paint);
    if (!fn || fn->BeginAddress != paint) return FALSE;
    DWORD at = FindBytes(lookup, paint, fn->EndAddress, "\x48\x8b\x41", 3);
    for (; at; at = FindBytes(lookup, at + 1, fn->EndAddress, "\x48\x8b\x41", 3)) {
        const BYTE *p = lookup->base + at;
        if (memcmp(p + 4, "\x48\x83\xc1", 3) || p[7] != p[3] || memcmp(p + 8, "\x48\x8b\x40", 3)) continue;
        DWORD call = FindBytes(lookup, at, fn->EndAddress, "\x89\xfa\xe8", 3);
        if (!call) return FALSE;
        bindings->widgetThemeOffset = p[3];
        bindings->themeProviderOffset = p[11];
        bindings->getColor = CallTarget(call + 2, lookup->base + call + 2);
        return TRUE;
    }
    return FALSE;
}

const char *ResolveColorProvider(const Lookup *lookup, Bindings *bindings) {
    const RUNTIME_FUNCTION *fn = LookupFunction(lookup, bindings->roundedBackground);
    if (!fn) return "the color provider";
    for (DWORD rva = fn->BeginAddress; rva + 7 <= fn->EndAddress; rva++) {
        const BYTE *p = lookup->base + rva;
        if ((p[0] != 0x48 && p[0] != 0x4c) || p[1] != 0x8d || (p[2] & 0xc7) != 0x05) continue;
        DWORD vtable = rva + 7 + *(const INT32 *)(p + 3);
        if (vtable < lookup->rdata->VirtualAddress || vtable >= lookup->rdata->VirtualAddress + lookup->rdata->Misc.VirtualSize) continue;
        DWORD paint = LookupPointerRva(lookup, lookup->base + vtable + bindings->backgroundPaintOffset);
        if (paint && ReadColorProvider(lookup, paint, bindings)) return NULL;
    }
    return "the color provider";
}

// SetPreferredSize keeps its value in an optional, and ends by saying the size changed:
// lea rax,[rcx+size]; mov r9b,[rcx+size+8] ... mov rax,[rcx]; mov rax,[rax+changed]; jmp
// Everything that reads it then has this shape, with the method that works it out otherwise:
// cmp byte [view+size+8],1; je; mov rax,[view]; mov rax,[rax+calculate]; lea r8,[no limits]
const char *ResolveViewSize(const Lookup *lookup, Bindings *bindings) {
    const BYTE *set = lookup->base + bindings->setPreferredSize;
    if (memcmp(set, "\x48\x8d\x81", 3)) return "where a view keeps its size";
    DWORD has = *(const DWORD *)(set + 3) + 8;
    DWORD changed = FindBytes(lookup, bindings->setPreferredSize, bindings->setPreferredSize + 0x60, "\x48\x8b\x01\x48\x8b\x80", 6);
    if (!changed) return "View::PreferredSizeChanged";
    bindings->sizeChangedSlot = *(const DWORD *)(lookup->base + changed + 6) / 8;

    int votes[MAX_SLOTS] = {0}, best = 0;
    DWORD start = lookup->text->VirtualAddress, end = start + lookup->text->Misc.VirtualSize - 40;
    DWORD rdata = lookup->rdata->VirtualAddress, rdataEnd = rdata + lookup->rdata->Misc.VirtualSize;
    for (DWORD rva = start; rva < end; rva++) {
        const BYTE *p = lookup->base + rva;
        if (p[0] != 0x80 || (p[1] & 0xf8) != 0xb8 || *(const DWORD *)(p + 2) != has || p[6] != 1) continue;
        if (p[7] != 0x74 && (p[7] != 0x0f || p[8] != 0x84)) continue;
        // mov rax,[reg+method], and then the bounds in r8 or rdx, each possibly with something else between.
        for (DWORD at = rva + 9; at < rva + 28; at++) {
            const BYTE *q = lookup->base + at;
            if ((q[0] != 0x48 && q[0] != 0x4c) || q[1] != 0x8b || (q[2] & 0xc0) != 0x80 || (q[2] & 7) == 4) continue;
            DWORD offset = *(const DWORD *)(q + 3), bounds = 0;
            for (DWORD b = at + 7; b < at + 20 && !bounds; b++) {
                const BYTE *l = lookup->base + b;
                if ((l[0] == 0x4c && l[1] == 0x8d && l[2] == 0x05) || (l[0] == 0x48 && l[1] == 0x8d && l[2] == 0x15)) bounds = b;
            }
            if (!bounds || offset % 8 || offset / 8 >= MAX_SLOTS) break;
            DWORD unbounded = bounds + 7 + *(const INT32 *)(lookup->base + bounds + 3);
            votes[offset / 8]++;
            // The bounds with no limits are a constant. Some callers pass bounds they work out instead.
            if (unbounded >= rdata && unbounded < rdataEnd && votes[offset / 8] >= best) {
                best = votes[offset / 8];
                bindings->calculateSizeSlot = offset / 8;
                bindings->unboundedSize = unbounded;
            }
            break;
        }
    }
    if (bindings->unboundedSize) best = votes[bindings->calculateSizeSlot];
    return best >= 3 ? NULL : "View::CalculatePreferredSize";
}

// lea rdx,[key]; lea r8,["kMarginsKey"]; lea r9,[deleter]; mov rcx,rsi; call SetPropertyInternal, in the function that
// shows the row's close button, on the row's own property list: lea rsi,[rax+properties]
const char *ResolveMargins(const Lookup *lookup, const RUNTIME_FUNCTION *fn, Bindings *bindings) {
    static const char name[] = "kMarginsKey";
    // Aside now checks the margins first and sets them through a wrapper, but the row's functions next to it still call
    // SetPropertyInternal itself, so those count too.
    for (DWORD at = fn->BeginAddress - 0x2000; at + 24 <= fn->EndAddress + 0x2000; at++) {
        const BYTE *p = lookup->base + at;
        if (memcmp(p, "\x48\x8d\x15", 3) || memcmp(p + 7, "\x4c\x8d\x05", 3) || memcmp(p + 14, "\x4c\x8d\x0d", 3) || !IsCall(p + 24)) continue;
        DWORD text = at + 14 + *(const INT32 *)(p + 10);
        if (memcmp(lookup->base + text, name, sizeof name)) continue;
        bindings->marginsKey = at + 7 + *(const INT32 *)(p + 3);
        bindings->marginsName = text;
        bindings->marginsDeleter = at + 21 + *(const INT32 *)(p + 17);
        bindings->setProperty = CallTarget(at + 24, p + 24);
        // The list is what's in rcx for the call, from lea reg,[view+properties] before it. It was rsi and is rbx.
        const BYTE *move = p + 21;
        if ((move[0] & 0xfa) != 0x48 || move[1] != 0x89 || (move[2] & 0xc7) != 0xc1) return "a view's properties";
        int reg = (move[2] >> 3 & 7) | (move[0] & 4 ? 8 : 0);
        const RUNTIME_FUNCTION *site = LookupFunction(lookup, at);
        if (!site) continue;
        for (DWORD back = at; back > site->BeginAddress && back + 0x400 > at; back--) {
            const BYTE *q = lookup->base + back;
            if ((q[0] & 0xfa) != 0x48 || q[1] != 0x8d || (q[2] & 0xc0) != 0x80 || (q[2] & 7) == 4) continue;
            if (((q[2] >> 3 & 7) | (q[0] & 4 ? 8 : 0)) != reg) continue;
            bindings->propertiesOffset = *(const DWORD *)(q + 3);
            return NULL;
        }
        return "a view's properties";
    }
    return "kMarginsKey";
}

// Canvas::ClipRect: mov rcx,[rcx+paint]; then the Rect as an SkRect: movq xmm0,[rdx]; movq xmm1,[rdx+8]; paddd; ...
// Canvas::SaveLayerAlphaWithBounds: movzx eax,dl; cvtsi2ss xmm2,eax; mov rcx,[rcx+paint]; then divided by 255.
// Each has to be the only function of its shape.
void ResolveCanvas(const Lookup *lookup, Bindings *bindings) {
    static const BYTE alpha[] = {0x0f, 0xb6, 0xc2, 0xf3, 0x0f, 0x2a, 0xd0, 0x48, 0x8b, 0x89};
    static const BYTE rect[] = {0xf3, 0x0f, 0x7e, 0x02, 0xf3, 0x0f, 0x7e, 0x4a, 0x08, 0x66, 0x0f, 0xfe, 0xc8, 0x66, 0x0f, 0x6c, 0xc1, 0x0f, 0x5b, 0xc0, 0x48, 0x8d, 0x54, 0x24};
    DWORD start = lookup->text->VirtualAddress, end = start + lookup->text->Misc.VirtualSize - 64, clip = 0, layer = 0;
    for (DWORD rva = start; rva < end; rva++) {
        const BYTE *p = lookup->base + rva;
        BOOL isClip = p[0] == 0x48 && p[1] == 0x8b && p[2] == 0x89 && *(const DWORD *)(p + 3) == bindings->canvasPaintOffset && !memcmp(p + 7, rect, sizeof rect);
        BOOL isLayer = p[0] == 0x0f && !memcmp(p, alpha, sizeof alpha) && *(const DWORD *)(p + sizeof alpha) == bindings->canvasPaintOffset;
        if (!isClip && !isLayer) continue;
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, rva);
        DWORD *found = isClip ? &clip : &layer;
        if (!fn || (*found && *found != fn->BeginAddress)) return;
        *found = fn->BeginAddress;
    }
    bindings->canvasClipRect = clip;
    bindings->canvasSaveLayerAlpha = layer;

    // View::GetWidget, which a row doesn't override, ends: mov rax,[rcx+widget]; ret
    const ULONGLONG *vtable = LookupRva(lookup, bindings->rowVtable);
    DWORD getWidget = LookupPointerRva(lookup, vtable + bindings->widgetMethodOffset / 8);
    for (DWORD at = getWidget; getWidget && at < getWidget + 0x30; at++) {
        const BYTE *p = lookup->base + at;
        if (memcmp(p, "\x48\x8b\x81", 3) || p[7] != 0xc3) continue;
        bindings->viewWidgetOffset = *(const DWORD *)(p + 3);
        break;
    }
}
