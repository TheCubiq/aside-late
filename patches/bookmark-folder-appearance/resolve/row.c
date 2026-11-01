// VerticalBookmarkRowView: its icons, title, layout, drag state, caret and close button.

#include "resolve/rules.h"

// mov <reg>,[rsi+offset]: how the row loads a member.
static BOOL IsRowLoad(const BYTE *p, DWORD *offset) {
    if (p[0] != 0x48 && p[0] != 0x4c) return FALSE;
    if (p[1] != 0x8b || (p[2] & 0xc7) != 0x86) return FALSE;
    *offset = *(const DWORD *)(p + 3);
    return TRUE;
}

// The row sets its folder icon with: mov rbx,[rsi+leading]; lea rdx,[icon]; create the image;
// set it; destroy it. Then the same for the caret. The names and shape give all of it.
const char *ResolveIcons(const Lookup *lookup, Bindings *bindings) {
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
const char *ResolveTitle(const Lookup *lookup, Bindings *bindings) {
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
const char *ResolveIconTable(const Lookup *lookup, Bindings *bindings) {
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

// The row's Layout positions its close button by hand, comparing its bounds before setting them:
// mov rcx,[rsi+close] ... cmp [rcx+bounds],r8d ... lea rdx,[rsp+x]; call SetBoundsRect
const char *ResolveLayout(const Lookup *lookup, Bindings *bindings) {
    BYTE compare[7] = {0x44, 0x39, 0x81};
    memcpy(compare + 3, &bindings->boundsOffset, 4);
    const ULONGLONG *vtable = LookupRva(lookup, bindings->rowVtable);
    for (int slot = 0; slot < MAX_SLOTS; slot++) {
        DWORD method = LookupPointerRva(lookup, vtable + slot);
        if (!method) break;
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, method);
        if (!fn || fn->BeginAddress != method || fn->EndAddress - method > 1024) continue;
        DWORD at = FindBytes(lookup, method, fn->EndAddress, compare, sizeof compare);
        if (!at) continue;
        DWORD call = FindBytes(lookup, at, fn->EndAddress, "\x48\x8d\x54\x24", 4);
        DWORD close = FindBytes(lookup, method, at, "\x48\x8b\x8e", 3);
        if (!call || !close || !IsCall(lookup->base + call + 5)) continue;
        bindings->layoutSlot = slot;
        bindings->setBoundsRect = CallTarget(call + 5, lookup->base + call + 5);
        bindings->closeOffset = *(const DWORD *)(lookup->base + close + 3);
        return NULL;
    }
    return "the row's Layout";
}

// mov eax,[rsi+depth]; lea ecx,[rax+rax]; shl eax,4; sub eax,ecx; add eax,<base>. That's depth * 14, and then:
// cmp [rcx+top],6; jne; cmp [rcx+left],eax
const char *ResolveIndent(const Lookup *lookup, Bindings *bindings) {
    static const BYTE step[] = {0x8d, 0x0c, 0x00, 0xc1, 0xe0, 0x04, 0x29, 0xc8, 0x83, 0xc0};
    DWORD at = FindBytes(lookup, bindings->updateIcons - 0x2000, bindings->updateIcons + 0x2000, step, sizeof step);
    if (!at || lookup->base[at - 6] != 0x8b) return "the row's indent";
    const RUNTIME_FUNCTION *fn = LookupFunction(lookup, at);
    if (!fn) return "the row's indent";
    bindings->updateIndent = fn->BeginAddress;
    bindings->depthOffset = *(const DWORD *)(lookup->base + at - 4);
    bindings->indentBase = lookup->base[at + sizeof step];
    DWORD layout = FindBytes(lookup, fn->BeginAddress, at, "\x48\x8b\x89", 3);
    DWORD collapsed = FindBytes(lookup, fn->BeginAddress, at, "\x8a\x96", 2);
    DWORD left = FindBytes(lookup, at, fn->EndAddress, "\x39\x81", 2);
    if (!layout || !collapsed || !left) return "the row's insets";
    bindings->rowLayoutOffset = *(const DWORD *)(lookup->base + layout + 3);
    bindings->collapsedOffset = *(const DWORD *)(lookup->base + collapsed + 2);
    bindings->insetLeftOffset = *(const DWORD *)(lookup->base + left + 2);
    return NULL;
}

// The row's background tests that it's selected and then that it's dragged: cmp byte [rbx+selected],0; ...; cmp byte [rbx+dragged],1
// Whether a drag is active and how far it's moved sit on either side of that, where the function setting them keeps them.
const char *ResolveDrag(const Lookup *lookup, Bindings *bindings) {
    const ULONGLONG *vtable = LookupRva(lookup, bindings->rowVtable);
    DWORD method = LookupPointerRva(lookup, vtable + bindings->paintBackgroundSlot);
    const RUNTIME_FUNCTION *fn = method ? LookupFunction(lookup, method) : NULL;
    if (!fn) return "the row's background";
    BYTE selected[6] = {0x80, 0xbb};
    memcpy(selected + 2, &bindings->selectedOffset, 4);
    DWORD at = FindBytes(lookup, fn->BeginAddress, fn->EndAddress, selected, sizeof selected);
    if (!at) return "how a row is dragged";
    DWORD next = FindBytes(lookup, at + 7, at + 40, "\x80\xbb", 2);
    if (!next || lookup->base[next + 6] != 1) return "how a row is dragged";
    bindings->draggedOffset = *(const DWORD *)(lookup->base + next + 2);
    bindings->dragActiveOffset = bindings->draggedOffset - 1;
    bindings->dragOffsetX = bindings->draggedOffset + 4;
    bindings->dragOffsetY = bindings->draggedOffset + 8;
    return NULL;
}

// OnPaint: save; movsd xmm1,[rdi+progress]; mulsd xmm1,[degrees]; ...; concat; ImageView::OnPaint; restore.
const char *ResolveCaret(const Lookup *lookup, Bindings *bindings) {
    if (!(bindings->caretVtable = LookupClassVtable(lookup, "TabExpandChevronView"))) return "TabExpandChevronView's vtable";
    const ULONGLONG *vtable = LookupRva(lookup, bindings->caretVtable);
    for (int slot = 0; slot < MAX_SLOTS; slot++) {
        DWORD method = LookupPointerRva(lookup, vtable + slot);
        if (!method) break;
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, method);
        if (!fn || fn->BeginAddress != method || fn->EndAddress - method > 1024) continue;
        DWORD at = FindBytes(lookup, method, fn->EndAddress, "\xf2\x0f\x10\x8f", 4);
        if (!at || memcmp(lookup->base + at + 8, "\xf2\x0f\x59\x0d", 4)) continue;
        DWORD canvas = FindBytes(lookup, method, at, "\x48\x8b\x8e", 3);
        DWORD save = FindBytes(lookup, method, at, "\x48\x8b\x40", 3);
        DWORD concat = FindBytes(lookup, at, fn->EndAddress, "\x48\x8b\x80", 3);
        DWORD restore = concat ? FindBytes(lookup, concat, fn->EndAddress, "\x48\x8b\x40", 3) : 0;
        if (!canvas || !save || !concat || !restore) return "how the caret paints";
        bindings->caretPaintSlot = slot;
        bindings->caretProgressOffset = *(const DWORD *)(lookup->base + at + 4);
        bindings->turnDegrees = *(const double *)LookupRva(lookup, at + 16 + *(const INT32 *)(lookup->base + at + 12));
        bindings->canvasPaintOffset = *(const DWORD *)(lookup->base + canvas + 3);
        bindings->canvasSaveSlot = lookup->base[save + 3] / 8;
        bindings->canvasConcatSlot = *(const DWORD *)(lookup->base + concat + 3) / 8;
        bindings->canvasRestoreSlot = lookup->base[restore + 3] / 8;
        return NULL;
    }
    return "how the caret paints";
}

// The function that shows the close button reads the row's caret first, and it's what says when:
// and al,[rsi+hovered]; ...; mov dil,[rsi+loaded]
const char *ResolveClose(const Lookup *lookup, Bindings *bindings) {
    BYTE load[7] = {0x48, 0x8b, 0x8e};
    memcpy(load + 3, &bindings->caretOffset, 4);
    DWORD end = bindings->updateIcons;
    const RUNTIME_FUNCTION *showClose = NULL;
    for (DWORD at = FindBytes(lookup, end - 0x1000, end, load, sizeof load); at; at = FindBytes(lookup, at + 1, end, load, sizeof load)) {
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, at);
        if (!fn) continue;
        DWORD hover = FindBytes(lookup, fn->BeginAddress, at, "\x22\x86", 2), loaded = FindBytes(lookup, fn->BeginAddress, at, "\x40\x8a\xbe", 3);
        if (!hover || !loaded) continue;
        bindings->hoverOffset = *(const DWORD *)(lookup->base + hover + 2);
        bindings->loadedOffset = *(const DWORD *)(lookup->base + loaded + 3);
        showClose = fn;
        break;
    }
    if (!bindings->hoverOffset) return "when the close button shows";
    const char *missing = ResolveMargins(lookup, showClose, bindings);
    if (missing) return missing;

    // mov r8d,[rsi+color] ... mov rax,[rsi]; mov rax,[rax+SetImageModel]; mov rcx,rsi; xor edx,edx
    DWORD icon = FindVectorIcon(lookup, "kAsideCloseIcon");
    if (!icon) return "kAsideCloseIcon";
    size_t count;
    const Reference *sites = LookupCode(lookup, icon, &count);
    for (size_t i = 0; i < count; i++) {
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, sites[i].site);
        if (!fn) continue;
        DWORD color = FindBytes(lookup, fn->BeginAddress, fn->EndAddress, "\x44\x8b\x86", 3);
        DWORD set = FindBytes(lookup, fn->BeginAddress, fn->EndAddress, "\x48\x8b\x06\x48\x8b\x80", 6);
        if (!color || !set || memcmp(lookup->base + set + 10, "\x48\x89\xf1\x31\xd2", 5)) continue;
        bindings->closeUpdateImage = fn->BeginAddress;
        bindings->closeColorOffset = *(const DWORD *)(lookup->base + color + 3);
        bindings->closeSetImageSlot = *(const DWORD *)(lookup->base + set + 6) / 8;
        break;
    }
    if (!bindings->closeUpdateImage) return "the close button's icon";
    bindings->minusIcon = FindVectorIcon(lookup, "kRemoveIcon");
    return NULL;
}
