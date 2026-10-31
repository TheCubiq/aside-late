#include "aside-late.h"

// The toolbar update compares against Aside's own extension ID. Many functions load it,
// but only this one also has both SetVisible calls below.
static const char ExtensionId[16] = "fjdhphbdlfjogobd";

static BOOL LoadsExtensionId(const Image *img, const Function *fn) {
    for (DWORD rva = fn->BeginAddress; rva + 8 <= fn->EndAddress; rva++) {
        const BYTE *p = img->base + rva;
        if (p[0] != 0xf3 || p[1] != 0x0f || p[2] != 0x6f || (p[3] & 0xc7) != 0x05) continue;
        ULONGLONG target = (ULONGLONG)rva + 8 + *(const INT32 *)(p + 4);
        if (target + sizeof ExtensionId <= img->size && !memcmp(img->base + target, ExtensionId, sizeof ExtensionId)) return TRUE;
    }
    return FALSE;
}

// mov rcx,<any>
static BOOL IsMovRcx(const BYTE *p) {
    return (p[0] == 0x48 || p[0] == 0x4c) && p[1] == 0x89 && (p[2] & 0xc7) == 0xc1;
}

// SetVisible; [mov bpl,1;] mov rcx,<any>; mov dl,1; call
static DWORD FindShowCall(const Image *img, const Function *fn) {
    for (DWORD rva = fn->BeginAddress; rva + 32 <= fn->EndAddress; rva++) {
        const BYTE *p = img->base + rva;
        if (!IsSetVisible(p)) continue;
        p += SET_VISIBLE_SIZE;
        if ((p[0] == 0x40 || p[0] == 0x41) && (p[1] & 0xf8) == 0xb0 && p[2] == 0x01) p += 3;
        if (IsMovRcx(p) && p[3] == 0xb2 && p[4] == 0x01 && p[5] == 0xff) return (DWORD)(p + 3 - img->base);
    }
    return 0;
}

static int Find(const Image *img, Edit *edits) {
    int matches = 0;
    DWORD start = img->text->VirtualAddress + 3, end = start + img->text->Misc.VirtualSize - 32;
    // mov edx,<any>; xor dl,1; SetVisible; mov rcx,<any>; call
    for (DWORD rva = start; rva < end; rva++) {
        const BYTE *p = img->base + rva;
        if (p[0] != 0x80 || p[1] != 0xf2 || p[2] != 0x01 || !IsSetVisible(p + 3)) continue;
        if (!IsMovRcx(p + 3 + SET_VISIBLE_SIZE) || p[6 + SET_VISIBLE_SIZE] != 0xff) continue;
        int mov = MovEdxSize(p - 3) == 3 ? 3 : MovEdxSize(p - 2) == 2 ? 2 : 0;
        const Function *fn = FunctionAt(img, rva);
        if (!mov || !fn) continue;
        DWORD show = FindShowCall(img, fn);
        if (!show || !LoadsExtensionId(img, fn)) continue;
        if (matches++) return Refuse("more than one function looks like the toolbar update");
        edits[0] = ClearEdx(img, rva - mov, mov + 3, "never show when the page changes");
        edits[1] = ClearEdx(img, show, 2, "never show on the other path");
    }
    return matches ? 2 : Refuse("didn't find the toolbar update");
}

PATCH("ask-aside", Find)
