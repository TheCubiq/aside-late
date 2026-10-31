#include "resolve.h"

#define MAX_SLOTS 512

// The Background's rect and color, written in this order right after it's allocated:
// mov [rax+20h..2Ch],<int>; mov [rax+8],<color>
static BOOL IsRoundedBackgroundStores(const BYTE *p) {
    static const BYTE insets[] = {0x20, 0x24, 0x28, 0x2c};
    for (int i = 0; i < 4; i++)
        if (p[i * 3] != 0x89 || (p[i * 3 + 1] & 0xc7) != 0x40 || p[i * 3 + 2] != insets[i]) return FALSE;
    return (p[12] & 0xf8) == 0x48 && p[13] == 0x89 && (p[14] & 0xc7) == 0x40 && p[15] == 0x08;
}

static DWORD FindRoundedBackground(const Lookup *lookup) {
    DWORD start = lookup->text->VirtualAddress, end = start + lookup->text->Misc.VirtualSize - 16;
    DWORD found = 0;
    for (DWORD rva = start; rva < end; rva++) {
        const BYTE *p = lookup->base + rva;
        if (p[0] != 0x89 || p[2] != 0x20 || !IsRoundedBackgroundStores(p)) continue;
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, rva);
        if (!fn) continue;
        if (found && found != fn->BeginAddress) return 0;
        found = fn->BeginAddress;
    }
    return found;
}

// SchedulePaint builds gfx::Rect(size()) on the stack and asks if the view has a custom
// Paint: mov qword [rsp+x],0; mov rax,[rcx+size]; mov [rsp+y],rax; mov rcx,[rcx]; mov rax,[rcx+50h]
static DWORD FindSchedulePaint(const Lookup *lookup, DWORD *boundsOffset) {
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

// Every view that doesn't override a method has it in the same slot. Walk back from each
// vtable entry to the start of its table and take the index most of them agree on.
static BOOL FindViewMethodSlot(const Lookup *lookup, const char *trace, int *slot, DWORD *method) {
    DWORD strings[4];
    if (LookupStrings(lookup, trace, strings, 4) != 1) return FALSE;
    size_t count;
    const Reference *references = LookupCode(lookup, strings[0], &count);
    int votes[MAX_SLOTS] = {0}, best = 0;
    for (size_t i = 0; i < count; i++) {
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, references[i].site);
        size_t entries;
        const Reference *sites = fn ? LookupPointers(lookup, fn->BeginAddress, &entries) : NULL;
        for (size_t j = 0; sites && j < entries; j++) {
            int index = 0;
            while (index < MAX_SLOTS && LookupPointerRva(lookup, lookup->base + sites[j].site - (index + 1) * 8)) index++;
            if (index < MAX_SLOTS && ++votes[index] > best) {
                best = votes[index];
                *slot = index;
                *method = fn->BeginAddress;
            }
        }
    }
    return best >= 8;
}

// OnPaintBackground's only job is background_->Paint(canvas, this):
// mov rax,[rcx]; mov rax,[rax+slot]
static int FindBackgroundPaintOffset(const Lookup *lookup, DWORD method) {
    const RUNTIME_FUNCTION *fn = LookupFunction(lookup, method);
    if (!fn) return -1;
    for (DWORD rva = fn->BeginAddress; rva + 7 <= fn->EndAddress; rva++) {
        const BYTE *p = lookup->base + rva;
        if (!memcmp(p, "\x48\x8b\x01\x48\x8b\x40", 6)) return p[6];
    }
    return -1;
}

const char *ResolveBindings(const Lookup *lookup, Bindings *bindings) {
    memset(bindings, 0, sizeof *bindings);
    if (!(bindings->rowVtable = LookupClassVtable(lookup, "VerticalBookmarkRowView"))) return "VerticalBookmarkRowView's vtable";
    DWORD paintBackground = 0;
    if (!FindViewMethodSlot(lookup, "View::OnPaintBackground", &bindings->paintBackgroundSlot, &paintBackground)) return "View::OnPaintBackground's slot";
    if ((bindings->backgroundPaintOffset = FindBackgroundPaintOffset(lookup, paintBackground)) < 0) return "Background::Paint";
    if (!(bindings->roundedBackground = FindRoundedBackground(lookup))) return "CreateRoundedRectBackground";
    if (!(bindings->schedulePaint = FindSchedulePaint(lookup, &bindings->boundsOffset))) return "View::SchedulePaint";
    return NULL;
}
