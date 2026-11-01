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

// The next call at or after rva, with what it calls. Leaves rva just past it.
static BOOL NextCall(const Lookup *lookup, DWORD *rva, DWORD end, DWORD *target) {
    for (; *rva + 5 <= end; ++*rva) {
        const BYTE *p = lookup->base + *rva;
        if (!IsCall(p)) continue;
        DWORD to = CallTarget(*rva, p);
        if (to < lookup->text->VirtualAddress || to >= lookup->text->VirtualAddress + lookup->text->Misc.VirtualSize) continue;
        // An 0xe8 can be part of another instruction. Real targets start a function, and only
        // small leaf functions have no unwind data, sitting after padding instead.
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, to);
        if (fn ? fn->BeginAddress != to : lookup->base[to - 1] != 0xcc) continue;
        *target = to;
        *rva += 5;
        return TRUE;
    }
    return FALSE;
}

static DWORD FindBytes(const Lookup *lookup, DWORD start, DWORD end, const void *bytes, size_t size) {
    for (DWORD rva = start; rva + size <= end; rva++)
        if (!memcmp(lookup->base + rva, bytes, size)) return rva;
    return 0;
}

// The function holding a reference to a UTF-16 string, which is how Aside writes its own menu labels.
static const RUNTIME_FUNCTION *FindByLabel(const Lookup *lookup, const wchar_t *label) {
    size_t size = wcslen(label) * 2 + 2;
    DWORD start = lookup->rdata->VirtualAddress, end = start + lookup->rdata->Misc.VirtualSize;
    for (DWORD rva = FindBytes(lookup, start, end, label, size); rva; rva = FindBytes(lookup, rva + 2, end, label, size)) {
        size_t count;
        const Reference *sites = LookupCode(lookup, rva, &count);
        if (count) return LookupFunction(lookup, sites[0].site);
    }
    return NULL;
}

// OnMousePressed starts: mov eax,0xf800; and eax,[rdx+flags]; cmp eax,0x2000; jne; mov rbx,[rdi+callback]
static const char *ResolveRightClick(const Lookup *lookup, Bindings *bindings) {
    static const BYTE start[] = {0xb8, 0x00, 0xf8, 0x00, 0x00, 0x23, 0x42};
    static const BYTE right[] = {0x3d, 0x00, 0x20, 0x00, 0x00, 0x0f, 0x85};
    const ULONGLONG *vtable = LookupRva(lookup, bindings->rowVtable);
    for (int slot = 0; slot < MAX_SLOTS; slot++) {
        DWORD method = LookupPointerRva(lookup, vtable + slot);
        if (!method) break;
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, method);
        if (!fn || fn->BeginAddress != method) continue;
        DWORD at = FindBytes(lookup, method, min(method + 32, fn->EndAddress), start, sizeof start);
        if (!at) continue;
        const BYTE *p = lookup->base + at;
        if (memcmp(p + 8, right, sizeof right) || p[19] != 0x48 || p[20] != 0x8b || p[21] != 0x9f) continue;
        bindings->mousePressedSlot = slot;
        bindings->eventFlagsOffset = p[7];
        bindings->rightClickOffset = *(const DWORD *)(p + 22);
        break;
    }
    if (!bindings->rightClickOffset) return "the row's OnMousePressed";

    const RUNTIME_FUNCTION *fn = FindByLabel(lookup, L"Manage Bookmarks");
    if (!fn) return "the folder's right click";
    bindings->rightClick = fn->BeginAddress;
    DWORD end = fn->EndAddress;

    // cmp dword [rax+type],0: folders aren't type 0.
    DWORD at = FindBytes(lookup, fn->BeginAddress, end, "\x83\xb8", 2);
    if (!at) return "the node's type";
    bindings->nodeTypeOffset = *(const DWORD *)(lookup->base + at + 2);
    // cmp byte [r15+managed],0
    if (!(at = FindBytes(lookup, at, end, "\x41\x80\xbf", 3))) return "the node's managed flag";
    bindings->nodeManagedOffset = *(const DWORD *)(lookup->base + at + 3);
    // inc qword [rsi+generation]
    if (!(at = FindBytes(lookup, at, end, "\x48\xff\x86", 3))) return "the menu's generation";
    bindings->ownerGenerationOffset = *(const DWORD *)(lookup->base + at + 3);
    // mov dword [rsi+command],0; mov rax,[r15+id]; mov [rsi+nodeId],rax; mov byte [rsi+hasNode],1
    for (at += 7; at + 28 <= end; at++) {
        const BYTE *p = lookup->base + at;
        if (memcmp(p, "\xc7\x86", 2) || *(const DWORD *)(p + 6)) continue;
        if (memcmp(p + 10, "\x49\x8b\x47", 3) || memcmp(p + 14, "\x48\x89\x86", 3) || memcmp(p + 21, "\xc6\x86", 2) || p[27] != 1) continue;
        bindings->ownerCommandOffset = *(const DWORD *)(p + 2);
        bindings->nodeIdOffset = p[13];
        bindings->ownerNodeIdOffset = *(const DWORD *)(p + 17);
        bindings->ownerHasNodeOffset = *(const DWORD *)(p + 23);
        break;
    }
    if (!bindings->ownerCommandOffset) return "the menu's command";

    // The close callback: lea rax,[method]; mov [r13+20h],rax. It comes after the labels.
    const BYTE closed[] = {0x48, 0x8d, 0x05};
    for (; at + 11 <= end; at++) {
        const BYTE *p = lookup->base + at;
        if (memcmp(p, closed, sizeof closed) || memcmp(p + 7, "\x49\x89\x45\x20", 4)) continue;
        bindings->menuClosed = at + 7 + *(const INT32 *)(p + 3);
        break;
    }
    if (!bindings->menuClosed) return "the menu's close callback";

    // movsd xmm6,[rax+location], rounded by two calls and then converted to the screen.
    if (!(at = FindBytes(lookup, at, end, "\xf2\x0f\x10\xb0", 4))) return "the event's location";
    bindings->eventLocationOffset = *(const DWORD *)(lookup->base + at + 4);
    DWORD ignored;
    if (!NextCall(lookup, &at, end, &ignored) || !NextCall(lookup, &at, end, &ignored) || !NextCall(lookup, &at, end, &bindings->convertToScreen))
        return "ConvertPointToScreen";

    // mov rax,[rsi]; mov rax,[rax+GetWidget]; mov rcx,rsi: the handler checks the owner has a widget.
    if (!(at = FindBytes(lookup, fn->BeginAddress, end, "\x48\x8b\x06\x48\x8b\x40", 6))) return "View::GetWidget";
    bindings->widgetMethodOffset = lookup->base[at + 6];
    return NULL;
}

// lea rax,[low]; movq xmm0,rax ... lea rax,[high]; movq xmmN,rax; punpcklqdq xmmN,xmm0, for every pair in a run.
static void ReadVtablePairs(const Lookup *lookup, DWORD rva, DWORD pairs[16][2]) {
    while (TRUE) {
        const BYTE *p = lookup->base + rva;
        if (memcmp(p, "\x48\x8d\x05", 3) || memcmp(p + 7, "\x66\x48\x0f\x6e\xc0", 5) || memcmp(p + 12, "\x48\x8d\x05", 3) || p[19] != 0x66 || p[21] != 0x0f || p[22] != 0x6e) return;
        int reg = ((p[23] >> 3) & 7) + (p[20] == 0x4c ? 8 : 0);
        pairs[reg][0] = rva + 12 + 7 + *(const INT32 *)(p + 15);
        pairs[reg][1] = rva + 7 + *(const INT32 *)(p + 3);
        rva += 12 + 7 + 5 + (p[24] == 0x66 && p[25] == 0x0f ? 4 : 5);
    }
}

// After the button's constructor the color button writes its vtables through r14, ending at its own fields:
// movdqu [r14+x],xmmN | lea rax,[vtable]; mov [r14+x],rax | mov [r14+id],esi
static const char *ReadColorVtables(const Lookup *lookup, DWORD rva, DWORD pairs[16][2], Bindings *bindings) {
    bindings->colorVtableCount = 0;
    for (int limit = 0; limit < 16; limit++) {
        const BYTE *p = lookup->base + rva;
        if (p[0] == 0xf3 && (p[1] & 0xfb) == 0x41 && p[2] == 0x0f && p[3] == 0x7f && (p[4] & 7) == 6) {
            int reg = ((p[4] >> 3) & 7) + (p[1] == 0x45 ? 8 : 0), mode = p[4] >> 6;
            DWORD offset = mode == 0 ? 0 : mode == 1 ? p[5] : *(const DWORD *)(p + 5);
            if (!pairs[reg][0] || bindings->colorVtableCount > 14) return "the color button's vtables";
            bindings->colorVtables[bindings->colorVtableCount].offset = offset;
            bindings->colorVtables[bindings->colorVtableCount++].vtable = pairs[reg][0];
            bindings->colorVtables[bindings->colorVtableCount].offset = offset + 8;
            bindings->colorVtables[bindings->colorVtableCount++].vtable = pairs[reg][1];
            rva += mode == 0 ? 5 : mode == 1 ? 6 : 9;
        } else if (!memcmp(p, "\x48\x8d\x05", 3) && p[7] == 0x49 && p[8] == 0x89 && (p[9] == 0x46 || p[9] == 0x86)) {
            if (bindings->colorVtableCount > 15) return "the color button's vtables";
            bindings->colorVtables[bindings->colorVtableCount].vtable = rva + 7 + *(const INT32 *)(p + 3);
            bindings->colorVtables[bindings->colorVtableCount++].offset = p[9] == 0x46 ? p[10] : *(const DWORD *)(p + 10);
            rva += p[9] == 0x46 ? 11 : 14;
        } else if (!memcmp(p, "\x41\x89\xb6", 3)) {
            bindings->colorIdOffset = *(const DWORD *)(p + 3);
            // mov rax,[rsp+x]; mov [r14+callback],rax; mov byte [r14+selected],0
            if (memcmp(p + 7, "\x48\x8b\x44\x24", 4) || memcmp(p + 12, "\x49\x89\x86", 3) || memcmp(p + 19, "\x41\xc6\x86", 3)) return "the color button's fields";
            bindings->colorCallbackOffset = *(const DWORD *)(p + 15);
            bindings->colorSelectedOffset = *(const DWORD *)(p + 22);
            return NULL;
        } else {
            return "the color button's vtables";
        }
    }
    return "the color button's fields";
}

static const char *ResolveColorButton(const Lookup *lookup, const RUNTIME_FUNCTION *show, Bindings *bindings) {
    DWORD begin = show->BeginAddress, end = show->EndAddress;
    DWORD strings[4];
    if (LookupStrings(lookup, "TabGroupColorButton", strings, 4) != 1) return "TabGroupColorButton";

    // lea r15,[colors]: an array of ints that the linker put just before the class's name.
    for (DWORD rva = begin; rva + 7 <= end && !bindings->colorIds; rva++) {
        const BYTE *p = lookup->base + rva;
        if (memcmp(p, "\x4c\x8d\x3d", 3)) continue;
        DWORD array = rva + 7 + *(const INT32 *)(p + 3);
        if (array >= strings[0] || strings[0] - array > 64 || (strings[0] - array) % 4) continue;
        bindings->colorIds = array;
        bindings->colorCount = (strings[0] - array) / 4;
        DWORD pairs[16][2] = {{0}};
        // The pairs follow a call that finds the colors' names.
        DWORD at = rva + 7, ignored;
        if (!NextCall(lookup, &at, end, &ignored)) return "the color button's vtables";
        at += 8;  // mov [rsp+x],rax
        ReadVtablePairs(lookup, at, pairs);

        // The button's constructor is the first thing a menu item calls.
        DWORD item = bindings->itemConstruct;
        if (!NextCall(lookup, &item, item + 0x100, &bindings->buttonConstruct)) return "Button's constructor";
        for (DWORD call = begin; call + 5 <= end; call++) {
            const BYTE *q = lookup->base + call;
            if (!IsCall(q) || CallTarget(call, q) != bindings->buttonConstruct) continue;
            const char *missing = ReadColorVtables(lookup, call + 5, pairs, bindings);
            if (missing) return missing;
            for (DWORD back = call; back > call - 0x100; back--) {
                const BYTE *b = lookup->base + back;
                if (b[0] != 0xba || b[3] || b[4] || memcmp(b + 5, "\x45\x31\xc0\xe8", 4)) continue;
                bindings->colorSize = *(const DWORD *)(b + 1);
                break;
            }
            DWORD after = call + 5;
            if (!FindBytes(lookup, after, after + 0x80, "\x41\xc6\x86", 3)) return "the color button's fields";
            after = FindBytes(lookup, after, after + 0x80, "\x41\xc6\x86", 3) + 8;
            if (!NextCall(lookup, &after, end, &bindings->setPreferredSize) || !NextCall(lookup, &after, end, &bindings->setFocusable)) return "the color button's setup";
            // mov byte [r14+flag],1 once, in the branch that first compares it.
            DWORD flag = FindBytes(lookup, after, after + 0x40, "\x41\xc6\x86", 3);
            if (!flag || lookup->base[flag + 7] != 1) return "the color button's flag";
            bindings->colorFlagOffset = *(const DWORD *)(lookup->base + flag + 3);
            DWORD ink = FindBytes(lookup, flag, flag + 0x40, "\x49\xc7\x86", 3);
            if (!ink) return "the color button's ink drop";
            bindings->colorInkOffset = *(const DWORD *)(lookup->base + ink + 3);
            break;
        }
        if (!bindings->colorSize || !bindings->colorIdOffset) return "the color button";
    }
    if (!bindings->colorIds) return "the menu's colors";

    size_t count;
    DWORD key[4];
    if (LookupStrings(lookup, "kHighlightPathGeneratorKey", key, 4) != 1) return "kHighlightPathGeneratorKey";
    const Reference *sites = LookupCode(lookup, key[0], &count);
    for (size_t i = 0; i < count && !bindings->installHighlight; i++) {
        if (sites[i].site < begin || sites[i].site >= end) continue;
        DWORD at = sites[i].site, ignored;
        if (!NextCall(lookup, &at, end, &ignored) || !NextCall(lookup, &at, end, &bindings->installHighlight)) return "the color button's highlight";
    }
    if (!bindings->installHighlight) return "the color button's highlight";
    // mov edx,0x8d: its accessibility role, set just before the button styles itself.
    DWORD role = FindBytes(lookup, begin, end, "\xba\x8d\x00\x00\x00", 5), ignored;
    if (!role || !NextCall(lookup, &role, end, &ignored) || !NextCall(lookup, &role, end, &bindings->updateColor)) return "the color button's update";
    return NULL;
}

// The function goes on to do for a bookmark what it did for a folder: inc qword [rsi+generation], then
// mov dword [rsi+command],0; mov rax,[rbx+id]; mov [rsi+nodeId],rax; mov byte [rsi+hasNode],1, then the close callback.
static const char *ResolveBookmarkMenu(const Lookup *lookup, Bindings *bindings) {
    const RUNTIME_FUNCTION *fn = LookupFunction(lookup, bindings->rightClick);
    if (!fn) return "the bookmark's menu";
    DWORD end = fn->EndAddress, sites[2], found = 0;
    for (DWORD at = fn->BeginAddress; at + 11 <= end && found < 2; at++) {
        const BYTE *p = lookup->base + at;
        if (!memcmp(p, "\x48\x8d\x05", 3) && !memcmp(p + 7, "\x49\x89\x45\x20", 4)) sites[found++] = at;
    }
    if (found < 2) return "the bookmark's close callback";
    bindings->bookmarkMenuClosed = sites[1] + 7 + *(const INT32 *)(lookup->base + sites[1] + 3);
    DWORD inc = FindBytes(lookup, sites[0], sites[1], "\x48\xff\x86", 3);
    if (!inc) return "the bookmark menu's generation";
    bindings->bookmarkGenerationOffset = *(const DWORD *)(lookup->base + inc + 3);
    for (DWORD at = inc; at + 28 <= sites[1]; at++) {
        const BYTE *p = lookup->base + at;
        if (memcmp(p, "\xc7\x86", 2) || *(const DWORD *)(p + 6) || memcmp(p + 10, "\x48\x8b\x43", 3) || memcmp(p + 14, "\x48\x89\x86", 3) || memcmp(p + 21, "\xc6\x86", 2) || p[27] != 1) continue;
        bindings->bookmarkCommandOffset = *(const DWORD *)(p + 2);
        bindings->bookmarkNodeIdOffset = *(const DWORD *)(p + 17);
        bindings->bookmarkHasNodeOffset = *(const DWORD *)(p + 23);
        return NULL;
    }
    return "the bookmark menu's command";
}

// The label leads to where the dialog is built, and the first callback bound after it is its answer:
// lea rcx,[invoke]; mov [rax+8],rcx, with the owner and the node's ID stored at +30h and +38h.
static const char *ResolveBookmarkDelete(const Lookup *lookup, Bindings *bindings) {
    const wchar_t *label = L"Delete this bookmark?";
    size_t size = wcslen(label) * 2 + 2;
    DWORD start = lookup->rdata->VirtualAddress, end = start + lookup->rdata->Misc.VirtualSize;
    DWORD text = FindBytes(lookup, start, end, label, size);
    if (!text) return "the bookmark's delete dialog";
    size_t count;
    const Reference *sites = LookupCode(lookup, text, &count);
    if (!count) return "the bookmark's delete dialog";
    const RUNTIME_FUNCTION *fn = LookupFunction(lookup, sites[0].site);
    if (!fn) return "the bookmark's delete dialog";
    for (DWORD at = sites[0].site; at + 40 <= fn->EndAddress; at++) {
        const BYTE *p = lookup->base + at;
        if (memcmp(p, "\x48\x8d\x0d", 3) || memcmp(p + 7, "\x48\x89\x48\x08", 4)) continue;
        bindings->deleteAccepted = at + 7 + *(const INT32 *)(p + 3);
        return NULL;
    }
    return "the bookmark's delete dialog";
}

// Popups, built in this order after the allocation:
// new, construct, destruct the old one, delete it, the content view, then the widget.
static const char *ResolvePopup(const Lookup *lookup, const RUNTIME_FUNCTION *show, Bindings *bindings) {
    DWORD begin = show->BeginAddress, end = show->EndAddress;
    enum {NEW, CONSTRUCT, DESTRUCT, DELETE_, VIEW_ALLOCATE, VIEW_CONSTRUCT, GUARD, UNGUARD, NEW_LAYOUT, LAYOUT, SET_LAYOUT, ADD_CHILD, SET_CONTENTS, GUARD2, NEW_STATE, CREATE, PREPARE, SKIP1, SKIP2, SHOW, CALLS};
    DWORD calls[CALLS];
    // Other large allocations don't go on to make these calls in this order.
    for (DWORD rva = begin; rva + 10 <= end && !bindings->popupSize; rva++) {
        const BYTE *p = lookup->base + rva;
        if (p[0] != 0xb9 || p[3] || p[4] || !IsCall(p + 5) || *(const DWORD *)(p + 1) < 0x400 || *(const DWORD *)(p + 1) > 0x800) continue;
        DWORD at = rva;
        BOOL complete = TRUE;
        for (int i = 0; i < CALLS && complete; i++) complete = NextCall(lookup, &at, end, &calls[i]);
        if (!complete || calls[NEW] != calls[NEW_LAYOUT] || calls[NEW] != calls[NEW_STATE] || calls[GUARD] != calls[GUARD2]) continue;
        bindings->popupSize = *(const DWORD *)(p + 1);
    }
    if (!bindings->popupSize) return "the popup's allocation";
    bindings->operatorNew = calls[NEW];
    bindings->popupConstruct = calls[CONSTRUCT];
    bindings->popupDestruct = calls[DESTRUCT];
    bindings->operatorDelete = calls[DELETE_];
    bindings->viewAllocate = calls[VIEW_ALLOCATE];
    bindings->viewConstruct = calls[VIEW_CONSTRUCT];
    bindings->layoutConstruct = calls[LAYOUT];
    bindings->setLayout = calls[SET_LAYOUT];
    bindings->addChild = calls[ADD_CHILD];
    bindings->popupSetContents = calls[SET_CONTENTS];
    bindings->widgetCreate = calls[CREATE];
    bindings->widgetPrepare = calls[PREPARE];
    bindings->widgetShow = calls[SHOW];

    // mov rcx,[rip+allocator]; test rcx,rcx; je; mov edx,size; xor r8d,r8d; call allocate
    for (DWORD rva = begin; rva + 9 <= end; rva++) {
        const BYTE *p = lookup->base + rva;
        if (p[0] != 0xba || p[3] || p[4] || memcmp(p + 5, "\x45\x31\xc0\xe8", 4)) continue;
        if (CallTarget(rva + 8, p + 8) != bindings->viewAllocate) continue;
        for (DWORD back = rva; back > rva - 24; back--) {
            const BYTE *b = lookup->base + back;
            if (memcmp(b, "\x48\x8b\x0d", 3)) continue;
            bindings->viewAllocator = back + 7 + *(const INT32 *)(b + 3);
            break;
        }
        if (bindings->viewAllocator) break;
    }
    return bindings->viewAllocator ? NULL : "the view allocator";
}

// mov r8d,color; mov r9d,16: the menu's own icons, each 16 wide, with the color they're themed with.
static const char *ResolveItems(const Lookup *lookup, const RUNTIME_FUNCTION *show, Bindings *bindings) {
    DWORD begin = show->BeginAddress, end = show->EndAddress;
    DWORD at = FindBytes(lookup, begin, end, "\x41\xb9\x10\x00\x00\x00", 6);
    for (; at; at = FindBytes(lookup, at + 1, end, "\x41\xb9\x10\x00\x00\x00", 6)) {
        const BYTE *p = lookup->base + at;
        if (p[-6] != 0x41 || p[-5] != 0xb8) continue;
        bindings->themedColorId = *(const DWORD *)(p - 4);
        break;
    }
    if (!bindings->themedColorId) return "the menu icons' color";

    // mov r9b,1; call: a menu item with an arrow, which is what Change icon is.
    DWORD call = FindBytes(lookup, begin, end, "\x41\xb1\x01\xe8", 4);
    if (!call) return "the menu item's constructor";
    bindings->itemConstruct = CallTarget(call + 3, lookup->base + call + 3);
    for (DWORD back = call; back > call - 0x180 && !bindings->itemSize; back--) {
        const BYTE *b = lookup->base + back;
        if (b[0] == 0xba && !b[3] && !b[4] && !memcmp(b + 5, "\x45\x31\xc0\xe8", 4)) bindings->itemSize = *(const DWORD *)(b + 1);
    }
    if (!bindings->itemSize) return "the menu item's size";

    // In its constructor: a border with insets, then mov rax,[rsi]; mov rax,[rax+SetBorder].
    DWORD item = bindings->itemConstruct, itemEnd = item + 0x200;
    DWORD border = FindBytes(lookup, item, itemEnd, "\xc7\x40\x08\x00\x00\xff\xff", 7);
    if (!border) return "the menu's border";
    DWORD vtable = FindBytes(lookup, border, border + 32, "\x48\x8d\x0d", 3);
    if (!vtable) return "the menu's border";
    bindings->borderVtable = vtable + 7 + *(const INT32 *)(lookup->base + vtable + 3);
    DWORD set = FindBytes(lookup, vtable, itemEnd, "\x48\x8b\x06\x48\x8b\x80", 6);
    if (!set) return "View::SetBorder";
    bindings->setBorderOffset = *(const DWORD *)(lookup->base + set + 6);

    // mov ecx,<layout size>; new; mov rdi,rax; mov rcx,rax; construct; lea rbx,[vtable]; mov [rdi],rbx;
    // lea rax,[vtable]; mov [rdi+30h],rax. The first of these is the box layout.
    for (DWORD rva = begin; rva + 40 <= end && !bindings->boxVtables[0]; rva++) {
        const BYTE *p = lookup->base + rva;
        if (p[0] != 0xb9 || !IsCall(p + 5) || memcmp(p + 10, "\x48\x89\xc7\x48\x89\xc1", 6) || !IsCall(p + 16)) continue;
        if (memcmp(p + 21, "\x48\x8d\x1d", 3) || memcmp(p + 28, "\x48\x89\x1f", 3) || memcmp(p + 31, "\x48\x8d\x05", 3)) continue;
        bindings->boxVtables[0] = rva + 28 + *(const INT32 *)(p + 24);
        bindings->boxVtables[1] = rva + 38 + *(const INT32 *)(p + 34);
    }
    if (!bindings->boxVtables[0]) return "the box layout";
    // mov [rdi+main],3; mov [rdi+cross],0, in the box built without a helper.
    for (DWORD rva = begin; rva + 20 <= end; rva++) {
        const BYTE *p = lookup->base + rva;
        if (memcmp(p, "\xc7\x87", 2) || *(const DWORD *)(p + 6) != 3 || memcmp(p + 10, "\x48\xc7\x87", 3) || *(const DWORD *)(p + 17)) continue;
        bindings->mainAlignOffset = *(const DWORD *)(p + 2);
        bindings->crossAlignOffset = *(const DWORD *)(p + 13);
        break;
    }
    return bindings->mainAlignOffset ? NULL : "the box layout's alignment";
}

// The picker's owner frees the key it's given, and closes its widget with reason 5.
static const char *ResolvePicker(const Lookup *lookup, const RUNTIME_FUNCTION *show, Bindings *bindings) {
    DWORD vtable = LookupClassVtable(lookup, "IconPickerView");
    if (!vtable) return "IconPickerView's vtable";
    size_t count;
    const Reference *sites = LookupCode(lookup, vtable, &count);
    DWORD begin = show->BeginAddress > 0x10000 ? show->BeginAddress - 0x10000 : 0, end = show->EndAddress + 0x10000;
    // Its destructor also writes the vtable, so the constructor is the one the menu calls.
    for (size_t i = 0; i < count && !bindings->pickerSize; i++) {
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, sites[i].site);
        if (!fn) continue;
        for (DWORD rva = begin; rva + 5 <= end && !bindings->pickerSize; rva++) {
            const BYTE *p = lookup->base + rva;
            if (!IsCall(p) || CallTarget(rva, p) != fn->BeginAddress) continue;
            for (DWORD back = rva; back > rva - 0x60; back--) {
                const BYTE *b = lookup->base + back;
                if (b[0] != 0xba || b[3] || b[4] || memcmp(b + 5, "\x45\x31\xc0\xe8", 4)) continue;
                bindings->pickerSize = *(const DWORD *)(b + 1);
                bindings->pickerConstruct = fn->BeginAddress;
                break;
            }
        }
    }
    if (!bindings->pickerSize) return "IconPickerView's constructor";

    // mov edx,5; call: Widget::CloseWithReason. The picker's callback is its only use nearby.
    for (DWORD rva = begin; rva + 10 <= end; rva++) {
        const BYTE *p = lookup->base + rva;
        if (memcmp(p, "\xba\x05\x00\x00\x00\xe8", 6) || p[-7] != 0x48 || p[-6] != 0x8b) continue;
        DWORD target = CallTarget(rva + 5, p + 5);
        if (bindings->widgetClose && bindings->widgetClose != target) return "Widget::Close (more than one match)";
        bindings->widgetClose = target;
    }
    return bindings->widgetClose ? NULL : "Widget::Close";
}

// A switch from a group color to its background's color ID:
// cmp ecx,9; ja; mov eax,ecx; lea r8,[table]; movsxd rax,[r8+rax*4]; add rax,r8; jmp rax
// where each case is mov eax,<id>; ret.
static const char *ResolveGroupColors(const Lookup *lookup, Bindings *bindings) {
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

static const char *ResolveColorProvider(const Lookup *lookup, Bindings *bindings) {
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
static const char *ResolveViewSize(const Lookup *lookup, Bindings *bindings) {
    const BYTE *set = lookup->base + bindings->setPreferredSize;
    if (memcmp(set, "\x48\x8d\x81", 3)) return "where a view keeps its size";
    DWORD has = *(const DWORD *)(set + 3) + 8;
    DWORD changed = FindBytes(lookup, bindings->setPreferredSize, bindings->setPreferredSize + 0x60, "\x48\x8b\x01\x48\x8b\x80", 6);
    if (!changed) return "View::PreferredSizeChanged";
    bindings->sizeChangedSlot = *(const DWORD *)(lookup->base + changed + 6) / 8;

    int votes[MAX_SLOTS] = {0}, best = 0;
    DWORD start = lookup->text->VirtualAddress, end = start + lookup->text->Misc.VirtualSize - 40;
    for (DWORD rva = start; rva < end; rva++) {
        const BYTE *p = lookup->base + rva;
        if (p[0] != 0x80 || (p[1] & 0xf8) != 0xb8 || *(const DWORD *)(p + 2) != has || p[6] != 1 || p[7] != 0x0f || p[8] != 0x84) continue;
        // mov rax,[view] and the method, and then the bounds, each possibly with something else between.
        for (DWORD at = rva + 13; at < rva + 20; at++) {
            const BYTE *q = lookup->base + at;
            if ((q[0] != 0x48 && q[0] != 0x4c) || q[1] != 0x8b || (q[2] & 0xc7) != 0x80) continue;
            DWORD offset = *(const DWORD *)(q + 3);
            DWORD bounds = FindBytes(lookup, at + 7, at + 16, "\x4c\x8d\x05", 3);
            if (!bounds || offset % 8 || offset / 8 >= MAX_SLOTS) break;
            if (++votes[offset / 8] > best) {
                best = votes[offset / 8];
                bindings->calculateSizeSlot = offset / 8;
                bindings->unboundedSize = bounds + 7 + *(const INT32 *)(lookup->base + bounds + 3);
            }
            break;
        }
    }
    return best >= 8 ? NULL : "View::CalculatePreferredSize";
}

// The row's Layout positions its close button by hand, comparing its bounds before setting them:
// mov rcx,[rsi+close] ... cmp [rcx+bounds],r8d ... lea rdx,[rsp+x]; call SetBoundsRect
static const char *ResolveLayout(const Lookup *lookup, Bindings *bindings) {
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
static const char *ResolveIndent(const Lookup *lookup, Bindings *bindings) {
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

// What rebuilds the rows is what calls the function that builds them, apart from that function itself. Rows it has no
// more use for it deletes at its end: mov rax,[rcx]; mov rax,[rax+delete]; mov edx,1
static void ResolveRebuild(const Lookup *lookup, const RUNTIME_FUNCTION *build, Bindings *bindings) {
    for (DWORD rva = build->BeginAddress - 0x8000; rva + 5 <= build->BeginAddress; rva++) {
        const BYTE *p = lookup->base + rva;
        if (!IsCall(p) || CallTarget(rva, p) != build->BeginAddress) continue;
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, rva);
        if (!fn) continue;
        DWORD at = FindBytes(lookup, fn->BeginAddress, fn->EndAddress, "\x48\x8b\x01\x48\x8b\x40", 6);
        for (; at; at = FindBytes(lookup, at + 1, fn->EndAddress, "\x48\x8b\x01\x48\x8b\x40", 6)) {
            if (memcmp(lookup->base + at + 7, "\xba\x01\x00\x00\x00", 5)) continue;
            bindings->rebuild = fn->BeginAddress;
            bindings->rebuildEnd = fn->EndAddress;
            bindings->rowDeleteSlot = lookup->base[at + 6] / 8;
            return;
        }
    }
}

// For a folded folder, the builder lists its active tab, then the last one it was, then every loaded tab in order:
// cmp byte [rbx+has],1; jne; mov rdx,[rbx+active]; mov rcx,r12; call
// The list in order starts: movaps [rsp+x],xmm9; mov qword [rsp+y],0; mov rcx,rbx; mov rdx,r12; lea r8,[rsp+x]
static void ResolveActiveOrder(const Lookup *lookup, const RUNTIME_FUNCTION *build, Bindings *bindings) {
    for (DWORD at = build->BeginAddress; at + 24 <= build->EndAddress; at++) {
        const BYTE *p = lookup->base + at;
        if (memcmp(p, "\x80\xbb", 2) || p[6] != 1 || p[7] != 0x75 || memcmp(p + 9, "\x48\x8b\x93", 3) || memcmp(p + 16, "\x4c\x89\xe1\xe8", 4)) continue;
        for (DWORD skip = at; skip + 34 <= build->EndAddress && skip < at + 0x400; skip++) {
            const BYTE *q = lookup->base + skip;
            if (memcmp(q, "\x44\x0f\x29\x8c\x24", 5) || memcmp(q + 9, "\x48\xc7\x84\x24", 4) || memcmp(q + 21, "\x48\x89\xd9\x4c\x89\xe2\x4c\x8d\x84\x24", 10)) continue;
            bindings->activeOrderSite = at;
            bindings->activeOrderSkip = skip;
            return;
        }
    }
}

// The function that builds the rows is the largest calling the row's indent function. It checks each folder against:
// mov rdx,[rbx+ids]; mov r8,[rbx+ids+8]
static const char *ResolveExpanded(const Lookup *lookup, Bindings *bindings) {
    const RUNTIME_FUNCTION *build = NULL;
    DWORD begin = bindings->updateIcons - 0x20000, end = bindings->updateIcons;
    for (DWORD rva = begin; rva + 5 <= end; rva++) {
        const BYTE *p = lookup->base + rva;
        if (!IsCall(p) || CallTarget(rva, p) != bindings->updateIndent) continue;
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, rva);
        if (fn && (!build || fn->EndAddress - fn->BeginAddress > build->EndAddress - build->BeginAddress)) build = fn;
    }
    if (!build) return "the function that builds the rows";
    // Rows are placed with: mov rcx,[rsi+animator]; test rcx,rcx; jne animate; mov rcx,rdx; mov rdx,r8; call SetBoundsRect
    for (DWORD rva = begin; rva + 26 <= end && !bindings->animatorOffset; rva++) {
        const BYTE *p = lookup->base + rva;
        if (memcmp(p, "\x48\x8b\x8e", 3) || memcmp(p + 7, "\x48\x85\xc9\x0f\x85", 5) || memcmp(p + 16, "\x48\x89\xd1\x4c\x89\xc2", 6) || !IsCall(p + 22)) continue;
        if (CallTarget(rva + 22, p + 22) == bindings->setBoundsRect) bindings->animatorOffset = *(const DWORD *)(p + 3);
    }
    if (!bindings->animatorOffset) return "what animates the rows";
    for (DWORD at = build->BeginAddress; at + 14 <= build->EndAddress; at++) {
        const BYTE *p = lookup->base + at;
        if (memcmp(p, "\x48\x8b\x93", 3) || memcmp(p + 7, "\x4c\x8b\x83", 3)) continue;
        if (*(const DWORD *)(p + 10) != *(const DWORD *)(p + 3) + 8) continue;
        bindings->expandedOffset = *(const DWORD *)(p + 3);
        ResolveRebuild(lookup, build, bindings);
        ResolveActiveOrder(lookup, build, bindings);
        return NULL;
    }
    return "the list of open folders";
}

// The row's background tests that it's selected and then that it's dragged: cmp byte [rbx+selected],0; ...; cmp byte [rbx+dragged],1
// Whether a drag is active and how far it's moved sit on either side of that, where the function setting them keeps them.
static const char *ResolveDrag(const Lookup *lookup, Bindings *bindings) {
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

// lea rdx,[key]; lea r8,["kMarginsKey"]; lea r9,[deleter]; mov rcx,rsi; call SetPropertyInternal, in the function that
// shows the row's close button, on the row's own property list: lea rsi,[rax+properties]
static const char *ResolveMargins(const Lookup *lookup, const RUNTIME_FUNCTION *fn, Bindings *bindings) {
    static const char name[] = "kMarginsKey";
    for (DWORD at = fn->BeginAddress; at + 24 <= fn->EndAddress; at++) {
        const BYTE *p = lookup->base + at;
        if (memcmp(p, "\x48\x8d\x15", 3) || memcmp(p + 7, "\x4c\x8d\x05", 3) || memcmp(p + 14, "\x4c\x8d\x0d", 3) || !IsCall(p + 24)) continue;
        DWORD text = at + 14 + *(const INT32 *)(p + 10);
        if (memcmp(lookup->base + text, name, sizeof name)) continue;
        bindings->marginsKey = at + 7 + *(const INT32 *)(p + 3);
        bindings->marginsName = text;
        bindings->marginsDeleter = at + 21 + *(const INT32 *)(p + 17);
        bindings->setProperty = CallTarget(at + 24, p + 24);
        DWORD list = FindBytes(lookup, fn->BeginAddress, at, "\x48\x8d\xb0", 3);
        if (!list) return "a view's properties";
        bindings->propertiesOffset = *(const DWORD *)(lookup->base + list + 3);
        return NULL;
    }
    return "kMarginsKey";
}

// OnPaint: save; movsd xmm1,[rdi+progress]; mulsd xmm1,[degrees]; ...; concat; ImageView::OnPaint; restore.
static const char *ResolveCaret(const Lookup *lookup, Bindings *bindings) {
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
static const char *ResolveClose(const Lookup *lookup, Bindings *bindings) {
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

static DWORD IconOr(const Lookup *lookup, const char *name, DWORD fallback) {
    DWORD icon = FindVectorIcon(lookup, name);
    return icon ? icon : fallback;
}

static const char *ResolveMenu(const Lookup *lookup, Bindings *bindings) {
    const char *missing = ResolveRightClick(lookup, bindings);
    if (missing) return missing;
    if ((missing = ResolveBookmarkMenu(lookup, bindings))) return missing;
    if ((missing = ResolveBookmarkDelete(lookup, bindings))) return missing;
    const RUNTIME_FUNCTION *show = NULL;
    DWORD strings[4];
    size_t count;
    if (LookupStrings(lookup, "TabGroupColorButton", strings, 4) != 1) return "TabGroupColorButton";
    const Reference *sites = LookupCode(lookup, strings[0], &count);
    for (size_t i = 0; i < count && !show; i++) {
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, sites[i].site);
        if (fn && fn->EndAddress - fn->BeginAddress > 4096) show = fn;
    }
    if (!show) return "the tab group menu";
    if ((missing = ResolvePopup(lookup, show, bindings))) return missing;
    if ((missing = ResolveItems(lookup, show, bindings))) return missing;
    if ((missing = ResolveColorButton(lookup, show, bindings))) return missing;
    if ((missing = ResolvePicker(lookup, show, bindings))) return missing;
    if ((missing = ResolveGroupColors(lookup, bindings))) return missing;
    if ((missing = ResolveColorProvider(lookup, bindings))) return missing;
    if ((missing = ResolveViewSize(lookup, bindings))) return missing;
    DWORD visibleOffset;
    if (!LookupVisibility(lookup, &bindings->setVisibleSlot, &visibleOffset)) return "how views are shown and hidden";
    if ((missing = ResolveLayout(lookup, bindings))) return missing;
    if ((missing = ResolveIndent(lookup, bindings))) return missing;
    if ((missing = ResolveExpanded(lookup, bindings))) return missing;
    if ((missing = ResolveDrag(lookup, bindings))) return missing;
    if ((missing = ResolveCaret(lookup, bindings))) return missing;
    if ((missing = ResolveClose(lookup, bindings))) return missing;
    bindings->changeIconIcon = IconOr(lookup, "kAsideEmojiAddReactionStrokeIcon", bindings->folderIcon);
    bindings->resetIcon = IconOr(lookup, "kAsideReloadChromeRefreshIcon", bindings->folderIcon);
    bindings->renameIcon = IconOr(lookup, "kAsideEditChromeRefreshIcon", bindings->folderIcon);
    bindings->deleteIcon = IconOr(lookup, "kTrashCanRefreshOldIcon", bindings->folderIcon);
    bindings->manageIcon = IconOr(lookup, "kBookmarksManagerOldIcon", bindings->folderIcon);
    return NULL;
}

const char *ResolveBindings(const Lookup *lookup, Bindings *bindings) {
    memset(bindings, 0, sizeof *bindings);
    if (!(bindings->rowVtable = LookupClassVtable(lookup, "VerticalBookmarkRowView"))) return "VerticalBookmarkRowView's vtable";
    DWORD paintBackground = 0;
    if (!LookupViewMethodSlot(lookup, "View::OnPaintBackground", &bindings->paintBackgroundSlot, &paintBackground)) return "View::OnPaintBackground's slot";
    if ((bindings->backgroundPaintOffset = FindBackgroundPaintOffset(lookup, paintBackground)) < 0) return "Background::Paint";
    if (!(bindings->roundedBackground = FindRoundedBackground(lookup))) return "CreateRoundedRectBackground";
    if (!(bindings->schedulePaint = FindSchedulePaint(lookup, &bindings->boundsOffset))) return "View::SchedulePaint";
    const char *missing = ResolveIcons(lookup, bindings);
    if (!missing) missing = ResolveTitle(lookup, bindings);
    if (!missing) missing = ResolveIconTable(lookup, bindings);
    if (!missing) missing = ResolveMenu(lookup, bindings);
    return missing;
}
