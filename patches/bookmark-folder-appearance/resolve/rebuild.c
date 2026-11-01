// What rebuilds the rows, and what's read out of the function that builds them.

#include "resolve/rules.h"

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

// Everything asking for a rebuild sets its guard first, and clears whether another was asked for:
// mov byte [rsi+guard],1; mov dl,1; mov byte [rsi+guard+1],0; and dl,1; mov rcx,rsi; call rebuild
static void ResolveRebuildGuard(const Lookup *lookup, Bindings *bindings) {
    if (!bindings->rebuild) return;
    for (DWORD at = bindings->rebuild - 0x10000; at < bindings->rebuild + 0x20000; at++) {
        const BYTE *p = lookup->base + at;
        if (p[0] != 0xc6 || p[1] != 0x86 || p[6] != 1 || p[7] != 0xb2 || p[8] != 1 || p[9] != 0xc6 || p[10] != 0x86) continue;
        if (*(const DWORD *)(p + 11) != *(const DWORD *)(p + 2) + 1 || p[15] != 0 || memcmp(p + 16, "\x80\xe2\x01\x48\x89\xf1", 6) || !IsCall(p + 22)) continue;
        if (CallTarget(at + 22, p + 22) != bindings->rebuild) continue;
        bindings->rebuildGuardOffset = *(const DWORD *)(p + 2);
        return;
    }
}

// The builder binds each row's callbacks with itself as the receiver, and the first is what closes a bookmark's tab:
// lea rax,[method]; mov [rcx+20h],rax; mov dword [rcx+28h],0; mov [rcx+30h],rbx. That tests the node isn't a folder.
static void ResolveUnload(const Lookup *lookup, const RUNTIME_FUNCTION *build, Bindings *bindings) {
    BYTE type[7] = {0x83, 0xba};
    memcpy(type + 2, &bindings->nodeTypeOffset, 4);
    for (DWORD at = build->BeginAddress; at + 22 <= build->EndAddress; at++) {
        const BYTE *p = lookup->base + at;
        if (memcmp(p, "\x48\x8d\x05", 3) || memcmp(p + 7, "\x48\x89\x41\x20\xc7\x41\x28", 7) || memcmp(p + 18, "\x48\x89\x59\x30", 4)) continue;
        DWORD method = at + 7 + *(const INT32 *)(p + 3);
        if (FindBytes(lookup, method, method + 0x30, type, sizeof type)) bindings->unloadTab = method;
        return;
    }
}

// The builder binds each row's callbacks: lea rax,[method]; mov [reg+20h],rax. The one that moves a drag starts by testing
// for the left button: mov eax,0xf800; and eax,[r8+50h]; cmp eax,800h
static void ResolveDragMove(const Lookup *lookup, const RUNTIME_FUNCTION *build, Bindings *bindings) {
    DWORD move = 0;
    for (DWORD at = build->BeginAddress; at + 11 <= build->EndAddress && !move; at++) {
        const BYTE *p = lookup->base + at;
        if (memcmp(p, "\x48\x8d\x05", 3)) continue;
        BOOL stored = (p[7] == 0x48 || p[7] == 0x49) && p[8] == 0x89 && (p[9] & 0xc0) == 0x40 && p[10] == 0x20;
        if (!stored) continue;
        DWORD method = at + 7 + *(const INT32 *)(p + 3);
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, method);
        if (fn && fn->BeginAddress == method && FindBytes(lookup, method, method + 0x100, "\xb8\x00\xf8\x00\x00\x41\x23\x40\x50\x3d\x00\x08\x00\x00", 14)) move = method;
    }
    if (!move) return;
    const RUNTIME_FUNCTION *fn = LookupFunction(lookup, move);
    // mov rcx,r14; mov rdx,rdi; mov r9,rbx; call convert. Then whether it's dragging yet: cmp byte [rsi+dragging],0
    DWORD at = FindBytes(lookup, move, fn->EndAddress, "\x4c\x89\xf1\x48\x89\xfa\x49\x89\xd9\xe8", 10);
    if (!at) return;
    DWORD convert = CallTarget(at + 9, lookup->base + at + 9);
    const RUNTIME_FUNCTION *conversion = LookupFunction(lookup, convert);
    DWORD flag = FindBytes(lookup, at, at + 0x20, "\x80\xbe", 2);
    if (!conversion || !flag || lookup->base[flag + 6] != 0) return;
    // movsd xmm6,[r14+root]: the point it converts.
    DWORD root = FindBytes(lookup, convert, conversion->EndAddress, "\xf2\x41\x0f\x10\xb6", 5);
    if (!root) return;
    // add edx,-16; movsxd rdx,edx; imul rdx,rdx,<a fourteenth>: the nesting it's after.
    if (!FindBytes(lookup, move, fn->EndAddress, "\x83\xc2\xf0\x48\x63\xd2\x48\x69\xd2\x93\x24\x49\x92", 13)) return;
    // mov dl,1; mov r8d,r14d; mov r9d,ebp; call place
    DWORD place = FindBytes(lookup, move, fn->EndAddress, "\xb2\x01\x45\x89\xf0\x41\x89\xe9\xe8", 9);
    if (!place) return;
    // mov [rsi+target],r15; mov [rsi+target+8],r14, and past those whether there is one.
    DWORD target = 0;
    for (DWORD rva = move; rva + 14 <= fn->EndAddress && !target; rva++) {
        const BYTE *p = lookup->base + rva;
        if (memcmp(p, "\x4c\x89\xbe", 3) || memcmp(p + 7, "\x4c\x89\xb6", 3) || *(const DWORD *)(p + 10) != *(const DWORD *)(p + 3) + 8) continue;
        target = *(const DWORD *)(p + 3);
    }
    if (!target) return;
    bindings->dragConvert = convert;
    bindings->dragPlace = CallTarget(place + 8, lookup->base + place + 8);
    bindings->eventRootOffset = *(const DWORD *)(lookup->base + root + 5);
    bindings->draggingOffset = *(const DWORD *)(lookup->base + flag + 2);
    bindings->dropTargetOffset = target;
    bindings->dragMove = move;
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
const char *ResolveExpanded(const Lookup *lookup, Bindings *bindings) {
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
        ResolveRebuildGuard(lookup, bindings);
        ResolveUnload(lookup, build, bindings);
        ResolveDragMove(lookup, build, bindings);
        ResolveActiveOrder(lookup, build, bindings);
        return NULL;
    }
    return "the list of open folders";
}
