// A right click, and the parts of Aside's tab group menu and bookmark menus that a folder's menu is made from.

#include "resolve/rules.h"

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

// After its metric, the bookmark bar's New Folder makes a do-nothing callback, works out where the folder goes, and then:
// AddFolder(details, parent, index); Show(window, profile, details, 0, callback); ~EditDetails(details)
static void ResolveNewFolder(const Lookup *lookup, Bindings *bindings) {
    DWORD strings[2];
    if (LookupStrings(lookup, "BookmarkBar_ContextMenu_NewFolder", strings, 2) != 1) return;
    size_t count;
    const Reference *sites = LookupCode(lookup, strings[0], &count);
    const RUNTIME_FUNCTION *fn = count == 1 ? LookupFunction(lookup, sites[0].site) : NULL;
    if (!fn) return;
    DWORD at = sites[0].site, calls[8];
    for (int i = 0; i < 8; i++)
        if (!NextCall(lookup, &at, fn->EndAddress, &calls[i])) return;
    // lea rcx,[function]; mov [rax+8|10h|18h],rcx
    DWORD found[3] = {0};
    for (DWORD rva = sites[0].site; rva + 11 <= fn->EndAddress && !found[2]; rva++) {
        const BYTE *p = lookup->base + rva;
        if (memcmp(p, "\x48\x8d\x0d", 3) || memcmp(p + 7, "\x48\x89\x48", 3)) continue;
        if (p[10] == 0x08 || p[10] == 0x10 || p[10] == 0x18) found[p[10] / 8 - 1] = rva + 7 + *(const INT32 *)(p + 3);
    }
    if (!found[0] || !found[1] || !found[2]) return;

    // A caller of Show gets the window from a browser: mov rax,[rax+window] ... mov rcx,[rax]; mov rdx,[rcx+native]; mov rcx,rax; mov rax,rdx
    DWORD show = calls[6];
    DWORD start = lookup->text->VirtualAddress, end = start + lookup->text->Misc.VirtualSize - 8;
    int windowSlot = 0, nativeSlot = 0, profileSlot = 0;
    for (DWORD rva = start; rva < end && !profileSlot; rva++) {
        const BYTE *p = lookup->base + rva;
        if (p[0] != 0x48 || p[1] != 0x8b || p[2] != 0x08 || p[3] != 0x48 || p[4] != 0x8b || p[5] != 0x51 || memcmp(p + 7, "\x48\x89\xc1\x48\x89\xd0", 6)) continue;
        DWORD call = rva + 13, target;
        if (!NextCall(lookup, &call, rva + 0x30, &target) || target != show) continue;
        // And the same function starts by asking it for the profile it passes:
        // mov rax,[rcx]; mov rax,[rax+profile]; call; mov r12,rax
        const RUNTIME_FUNCTION *caller = LookupFunction(lookup, rva);
        if (!caller) continue;
        for (DWORD at = caller->BeginAddress; at + 19 <= rva && !profileSlot; at++) {
            const BYTE *q = lookup->base + at;
            if (memcmp(q, "\x48\x8b\x01\x48\x8b\x40", 6) || memcmp(q + 7, "\xff\x15", 2) || memcmp(q + 13, "\x49\x89\xc4", 3)) continue;
            profileSlot = q[6] / 8;
        }
        if (!profileSlot) continue;
        for (DWORD back = rva; back > rva - 0x20; back--) {
            const BYTE *b = lookup->base + back;
            if (memcmp(b, "\x48\x8b\x40", 3)) continue;
            windowSlot = b[3] / 8;
            nativeSlot = p[6] / 8;
            break;
        }
    }
    if (!windowSlot) return;

    // A row's owner asks its browser for the same thing:
    // mov rcx,[rsi+browser]; test rcx,rcx; je; mov rax,[rcx]; mov rax,[rax+profile]
    for (DWORD rva = bindings->rebuild - 0x4000; rva < bindings->rebuild + 0x8000; rva++) {
        const BYTE *p = lookup->base + rva;
        if (memcmp(p, "\x48\x8b\x8e", 3) || memcmp(p + 7, "\x48\x85\xc9\x0f\x84", 5) || memcmp(p + 16, "\x48\x8b\x01\x48\x8b\x40", 6) || p[22] / 8 != profileSlot) continue;
        bindings->ownerBrowserOffset = *(const DWORD *)(p + 3);
        break;
    }
    if (!bindings->ownerBrowserOffset) return;
    bindings->browserProfileSlot = profileSlot;
    bindings->browserWindowSlot = windowSlot;
    bindings->windowNativeSlot = nativeSlot;
    bindings->editorCallbackInvoke = found[0];
    bindings->editorCallbackDestroy = found[1];
    bindings->editorCallbackQuery = found[2];
    bindings->editorAddFolder = calls[5];
    bindings->editorDetailsDestroy = calls[7];
    // A folder's children are listed right after its parent, as learned from the live node, and Aside reads them at +40h.
    bindings->nodeChildrenOffset = 0x40;
    bindings->editorShow = show;
}

static DWORD IconOr(const Lookup *lookup, const char *name, DWORD fallback) {
    DWORD icon = FindVectorIcon(lookup, name);
    return icon ? icon : fallback;
}

const char *ResolveMenu(const Lookup *lookup, Bindings *bindings) {
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
    ResolveCanvas(lookup, bindings);
    if ((missing = ResolveClose(lookup, bindings))) return missing;
    bindings->changeIconIcon = IconOr(lookup, "kAsideEmojiAddReactionStrokeIcon", bindings->folderIcon);
    bindings->resetIcon = IconOr(lookup, "kAsideReloadChromeRefreshIcon", bindings->folderIcon);
    bindings->renameIcon = IconOr(lookup, "kAsideEditChromeRefreshIcon", bindings->folderIcon);
    bindings->deleteIcon = IconOr(lookup, "kTrashCanRefreshOldIcon", bindings->folderIcon);
    bindings->manageIcon = IconOr(lookup, "kBookmarksManagerOldIcon", bindings->folderIcon);
    bindings->newFolderIcon = IconOr(lookup, "kAsideAddIcon", bindings->folderIcon);
    ResolveNewFolder(lookup, bindings);
    return NULL;
}
