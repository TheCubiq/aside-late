// Finds the sidebar's hover controller and how it's asked, by the shape of the code around it.

#include "resolve.h"

#define HOVER_SOURCE "..\\..\\chrome\\browser\\ui\\views\\frame\\aside_sidebar_hover_controller.cc"

DWORD EvaluateRva;
DWORD GetInputRva;
DWORD ControllerOffset;
DWORD StateOffset;
DWORD LeaveTimeOffset;
DWORD HideOnBlurRva;
LONGLONG NativeLeaveUs;

static DWORD FindBytes(const Lookup *lookup, DWORD start, DWORD end, const void *bytes, size_t size) {
    for (DWORD rva = start; rva + size <= end; rva++)
        if (!memcmp(lookup->base + rva, bytes, size)) return rva;
    return 0;
}

static BOOL IsCallTo(const BYTE *p, DWORD rva, DWORD target) {
    return p[0] == 0xe8 && rva + 5 + *(const INT32 *)(p + 1) == target;
}

// The controller re-posts itself from the file this names, so that function is what evaluates it.
static const char *FindEvaluate(const Lookup *lookup) {
    DWORD strings[2];
    if (LookupStrings(lookup, HOVER_SOURCE, strings, 2) != 1) return "the sidebar's hover controller";
    size_t count;
    const Reference *sites = LookupCode(lookup, strings[0], &count);
    const RUNTIME_FUNCTION *fn = count == 1 ? LookupFunction(lookup, sites[0].site) : NULL;
    if (!fn) return "how the sidebar's hover is evaluated";
    EvaluateRva = fn->BeginAddress;

    // mov ebp,[rsi+state]: it starts by keeping the state it's in.
    DWORD at = FindBytes(lookup, fn->BeginAddress, fn->EndAddress, "\x8b\x6e", 2);
    if (!at) return "the hover's state";
    StateOffset = lookup->base[at + 2];

    // When it's closing: mov eax,<delay>; add rax,[rsi+time]
    for (DWORD rva = fn->BeginAddress; rva + 9 <= fn->EndAddress; rva++) {
        const BYTE *p = lookup->base + rva;
        if (p[0] != 0xb8 || memcmp(p + 5, "\x48\x03\x46", 3)) continue;
        NativeLeaveUs = *(const INT32 *)(p + 1);
        LeaveTimeOffset = p[8];
        break;
    }
    return LeaveTimeOffset ? NULL : "how long the sidebar takes to close";
}

// A browser view runs it when the mouse moves: mov rcx,[rsi+controller]; test rcx,rcx; je ...; call evaluate
// Whatever stores that field makes the controller, and binds its callback just before: lea rax,[method]; mov [r14+20h],rax
static const char *FindGetInput(const Lookup *lookup) {
    DWORD start = lookup->text->VirtualAddress, end = start + lookup->text->Misc.VirtualSize - 32;
    for (DWORD rva = start; rva < end && !ControllerOffset; rva++) {
        const BYTE *p = lookup->base + rva;
        if (memcmp(p, "\x48\x8b\x8e", 3) || memcmp(p + 7, "\x48\x85\xc9\x74", 4) || !IsCallTo(p + 12, rva + 12, EvaluateRva)) continue;
        ControllerOffset = *(const DWORD *)(p + 3);
    }
    // Others hold it behind another test, so the wider form is tried as well.
    for (DWORD rva = start; rva < end && !ControllerOffset; rva++) {
        const BYTE *p = lookup->base + rva;
        if (memcmp(p, "\x48\x8b\x8e", 3) || memcmp(p + 7, "\x48\x85\xc9\x0f\x84", 5)) continue;
        for (DWORD call = rva + 16; call < rva + 40; call++)
            if (IsCallTo(lookup->base + call, call, EvaluateRva)) ControllerOffset = *(const DWORD *)(p + 3);
    }
    if (!ControllerOffset) return "where a browser keeps the sidebar's controller";

    BYTE store[7] = {0x48, 0x89, 0x86}, keep[7] = {0x48, 0x8b, 0xbe};
    memcpy(store + 3, &ControllerOffset, 4);
    memcpy(keep + 3, &ControllerOffset, 4);
    for (DWORD rva = start; rva < end; rva++) {
        const BYTE *p = lookup->base + rva;
        if (memcmp(p, keep, sizeof keep) || memcmp(p + 7, store, sizeof store)) continue;
        for (DWORD back = rva; back > rva - 0x100; back--) {
            const BYTE *b = lookup->base + back;
            // mov [reg+20h],rax: it was r14 and is rbx. The nearest is the one the controller keeps first.
            if (memcmp(b, "\x48\x8d\x05", 3) || (b[7] & 0xfe) != 0x48 || b[8] != 0x89 || (b[9] & 0xf8) != 0x40 || (b[9] & 7) == 4 || b[10] != 0x20) continue;
            GetInputRva = back + 7 + *(const INT32 *)(b + 3);
            break;
        }
        if (GetInputRva) break;
    }
    if (!GetInputRva) return "what tells the sidebar's controller where the cursor is";

    // It clears the whole of what it fills: movups [rdx+20h]; movups [rdx+10h]; movups [rdx]
    const RUNTIME_FUNCTION *fn = LookupFunction(lookup, GetInputRva);
    if (!fn || !FindBytes(lookup, fn->BeginAddress, fn->BeginAddress + 0x40, "\x0f\x11\x42\x20\x0f\x11\x42\x10\x0f\x11\x02", 11)) return "the shape of what the controller asks for";
    return NULL;
}

// When a window loses focus a browser hides the sidebar outright, and a menu taking the focus is enough for that:
// mov rcx,[rsi+widget]; test rcx,rcx; je; call IsActive; test al,al; jne; cmp byte [rsi+expanded],1; je hide; jmp
// Without that jump it still closes, because the controller is told nothing by a window without focus.
static void FindHideOnBlur(const Lookup *lookup) {
    DWORD start = lookup->text->VirtualAddress, end = start + lookup->text->Misc.VirtualSize - 40, found = 0;
    for (DWORD rva = start; rva < end; rva++) {
        const BYTE *p = lookup->base + rva;
        if (memcmp(p, "\x48\x8b\x8e", 3)) continue;
        // The test for no widget went away in 1.0.1116.2, so the call can follow straight on.
        const BYTE *q = !memcmp(p + 7, "\x48\x85\xc9\x74", 4) ? p + 12 : p + 7;
        if (q[0] != 0xe8) continue;
        if (memcmp(q + 5, "\x84\xc0\x75", 3) || memcmp(q + 9, "\x80\xbe", 2) || q[15] != 1 || memcmp(q + 16, "\x0f\x84", 2) || q[22] != 0xe9) continue;
        // It lives with what evaluates the controller, and anything else of this shape doesn't.
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, rva);
        BOOL evaluates = FALSE;
        for (DWORD call = fn ? fn->BeginAddress : 0; fn && call + 5 <= rva && !evaluates; call++) evaluates = IsCallTo(lookup->base + call, call, EvaluateRva);
        if (!evaluates) continue;
        if (found) return;
        found = (DWORD)(q + 16 - lookup->base);
    }
    HideOnBlurRva = found;
}

const char *ResolveController(const Lookup *lookup) {
    const char *missing = FindEvaluate(lookup);
    if (!missing) missing = FindGetInput(lookup);
    if (!missing) FindHideOnBlur(lookup);
    return missing;
}
