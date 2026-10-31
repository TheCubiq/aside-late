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

static BOOL InImage(const Lookup *lookup, ULONGLONG value, DWORD *rva) {
    const IMAGE_NT_HEADERS64 *nt = (const void *)(lookup->base + ((const IMAGE_DOS_HEADER *)lookup->base)->e_lfanew);
    if (value < lookup->pointerBase || value >= lookup->pointerBase + nt->OptionalHeader.SizeOfImage) return FALSE;
    *rva = (DWORD)(value - lookup->pointerBase);
    return TRUE;
}

// A gfx::VectorIcon holds its own name: reps, rep count, name.
static DWORD FindVectorIcon(const Lookup *lookup, const char *name) {
    DWORD strings[4], found = 0;
    if (LookupStrings(lookup, name, strings, 4) != 1) return 0;
    ULONGLONG wanted = lookup->pointerBase + strings[0];
    DWORD start = lookup->rdata->VirtualAddress + 16, end = lookup->rdata->VirtualAddress + lookup->rdata->Misc.VirtualSize - 8;
    for (DWORD rva = start; rva <= end; rva += 8) {
        if (*(const ULONGLONG *)(lookup->base + rva) != wanted) continue;
        if (*(const ULONGLONG *)(lookup->base + rva - 8) > 16) continue;
        if (found) return 0;
        found = rva - 16;
    }
    return found;
}

static BOOL IsCall(const BYTE *p) {
    return p[0] == 0xe8;
}

static DWORD CallTarget(DWORD rva, const BYTE *p) {
    return rva + 5 + *(const INT32 *)(p + 1);
}

// mov <reg>,[rsi+offset]: how the row loads a member.
static BOOL IsRowLoad(const BYTE *p, DWORD *offset) {
    if (p[0] != 0x48 && p[0] != 0x4c) return FALSE;
    if (p[1] != 0x8b || (p[2] & 0xc7) != 0x86) return FALSE;
    *offset = *(const DWORD *)(p + 3);
    return TRUE;
}

// The row sets its folder icon with: mov rbx,[rsi+leading]; lea rdx,[icon]; create the image;
// set it; destroy it. Then the same for the caret. The names and shape give all of it.
static const char *ResolveIcons(const Lookup *lookup, Bindings *bindings) {
    if (!(bindings->folderIcon = FindVectorIcon(lookup, "kAsideBookmarkFolderIcon"))) return "kAsideBookmarkFolderIcon";
    size_t count;
    const Reference *sites = LookupCode(lookup, bindings->folderIcon, &count);
    if (count != 1) return "the row's icon function";
    DWORD site = sites[0].site;
    const RUNTIME_FUNCTION *fn = LookupFunction(lookup, site);
    if (!fn) return "the row's icon function";
    bindings->updateIcons = fn->BeginAddress;

    for (DWORD rva = site; rva > fn->BeginAddress && site - rva < 48; rva--)
        if (IsRowLoad(lookup->base + rva, &bindings->leadingOffset)) break;
    if (!bindings->leadingOffset) return "the row's leading image";

    DWORD calls[3];
    int callCount = 0;
    DWORD rva = site + 7;
    while (callCount < 3 && rva + 5 <= fn->EndAddress) {
        if (IsCall(lookup->base + rva) && LookupFunction(lookup, CallTarget(rva, lookup->base + rva))) {
            calls[callCount++] = CallTarget(rva, lookup->base + rva);
            rva += 5;
        } else {
            rva++;
        }
    }
    if (callCount != 3) return "the image calls";
    bindings->createImage = calls[0];
    bindings->setImage = calls[1];
    bindings->destroyImage = calls[2];

    for (; rva + 7 <= fn->EndAddress; rva++) {
        const BYTE *p = lookup->base + rva;
        if (!bindings->caretOffset) IsRowLoad(p, &bindings->caretOffset);
        if (p[0] == 0x48 && p[1] == 0x8d && p[2] == 0x15) {
            bindings->caretIcon = rva + 7 + *(const INT32 *)(p + 3);
            break;
        }
    }
    return bindings->caretOffset && bindings->caretIcon ? NULL : "the row's caret";
}

// movzx eax,[row+selected]; lea rdx,[rax+rax*2]; add rdx,<color>; mov rax,[rcx]; mov rax,[rax+slot]
static const char *ResolveTitle(const Lookup *lookup, Bindings *bindings) {
    static const BYTE shape[] = {0x48, 0x8d, 0x14, 0x40, 0x48, 0x81, 0xc2};
    DWORD start = bindings->updateIcons - 0x2000, end = bindings->updateIcons + 0x2000;
    BOOL found = FALSE;
    for (DWORD rva = start; rva < end; rva++) {
        const BYTE *p = lookup->base + rva;
        if (memcmp(p, shape, sizeof shape)) continue;
        if (memcmp(p + 11, "\x48\x8b\x01\x48\x8b\x80", 6)) continue;
        if (p[-7] != 0x0f || p[-6] != 0xb6 || p[-5] != 0x86) continue;
        if (found) return "the title color (more than one match)";
        found = TRUE;
        bindings->selectedOffset = *(const DWORD *)(p - 4);
        bindings->titleColorId = *(const DWORD *)(p + 7);
        bindings->setTitleColorOffset = *(const DWORD *)(p + 17);
        for (DWORD back = rva - 7; back > rva - 40; back--) {
            if (!IsRowLoad(lookup->base + back, &bindings->titleOffset)) continue;
            break;
        }
    }
    return found && bindings->titleOffset ? NULL : "the title color";
}

static BOOL ReadIconEntry(const Lookup *lookup, DWORD rva) {
    const ULONGLONG *entry = (const ULONGLONG *)(lookup->base + rva);
    DWORD key, icon, label, name;
    if (!InImage(lookup, entry[0], &key) || !InImage(lookup, entry[2], &icon) || !InImage(lookup, entry[3], &label)) return FALSE;
    if (!entry[1] || entry[1] > 32 || !entry[4] || entry[4] > 40) return FALSE;
    if (lookup->base[key + entry[1]] != 0) return FALSE;
    for (size_t i = 0; i < entry[1]; i++) {
        BYTE c = lookup->base[key + i];
        if (c < 0x20 || c > 0x7e) return FALSE;
    }
    const ULONGLONG *vectorIcon = (const ULONGLONG *)(lookup->base + icon);
    if (vectorIcon[1] > 16 || !InImage(lookup, vectorIcon[2], &name)) return FALSE;
    return lookup->base[name] == 'k';
}

// A run of key, key size, icon, label, label size, where every icon names itself.
static const char *ResolveIconTable(const Lookup *lookup, Bindings *bindings) {
    DWORD start = lookup->rdata->VirtualAddress, end = start + lookup->rdata->Misc.VirtualSize - 40;
    int best = 0;
    for (DWORD rva = start; rva < end; rva += 8) {
        int run = 0;
        while (rva + run * 40 < end && ReadIconEntry(lookup, rva + run * 40)) run++;
        if (run > best) {
            best = run;
            bindings->iconTable = rva;
        }
        if (run) rva += run * 40 - 8;
    }
    bindings->iconCount = best;
    return best >= 16 ? NULL : "the icon table";
}

const char *ResolveBindings(const Lookup *lookup, Bindings *bindings) {
    memset(bindings, 0, sizeof *bindings);
    if (!(bindings->rowVtable = LookupClassVtable(lookup, "VerticalBookmarkRowView"))) return "VerticalBookmarkRowView's vtable";
    DWORD paintBackground = 0;
    if (!FindViewMethodSlot(lookup, "View::OnPaintBackground", &bindings->paintBackgroundSlot, &paintBackground)) return "View::OnPaintBackground's slot";
    if ((bindings->backgroundPaintOffset = FindBackgroundPaintOffset(lookup, paintBackground)) < 0) return "Background::Paint";
    if (!(bindings->roundedBackground = FindRoundedBackground(lookup))) return "CreateRoundedRectBackground";
    if (!(bindings->schedulePaint = FindSchedulePaint(lookup, &bindings->boundsOffset))) return "View::SchedulePaint";
    const char *missing = ResolveIcons(lookup, bindings);
    if (!missing) missing = ResolveTitle(lookup, bindings);
    if (!missing) missing = ResolveIconTable(lookup, bindings);
    return missing;
}
