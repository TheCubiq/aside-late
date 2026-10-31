#include "aside-late.h"

#define CHATS_PREF "aside.vertical_tabs.chats_section_expanded"
#define SITE_WINDOW 0x100000

// After reading the pref, the sidebar constructor stores the Chats container and then its divider.
static BOOL FindFields(const Image *img, const Function *ctor, DWORD xref, DWORD fields[2]) {
    int found = 0;
    for (DWORD rva = xref; rva + 7 <= ctor->EndAddress && found < 2; rva++) {
        const BYTE *p = img->base + rva;
        if ((p[0] != 0x48 && p[0] != 0x4c) || p[1] != 0x89 || (p[2] & 0xc7) != 0x86) continue;
        DWORD field = *(const DWORD *)(p + 3);
        if (!found || field != fields[0]) fields[found++] = field;
    }
    return found == 2;
}

// mov rcx,[this+field]; test rcx,rcx; je skip; [cmp dl,[rcx+visible]; je skip;]
// mov rax,[rcx]; mov rax,[rax+SetVisible]; [mov edx,<any>;] call
static BOOL MatchSite(const Image *img, DWORD rva, BOOL divider, Edit *edit) {
    const BYTE *p = img->base + rva + 7;
    if (p[0] != 0x48 || p[1] != 0x85 || p[2] != 0xc9) return FALSE;
    p += 3;
    if (p[0] == 0x74) p += 2;
    else if (p[0] == 0x0f && p[1] == 0x84) p += 6;
    else return FALSE;

    if (p[0] == 0x3a && p[1] == 0x91 && p[6] == 0x74) {
        if (divider || !IsSetVisible(p + 8) || p[8 + SET_VISIBLE_SIZE] != 0xff) return FALSE;
        *edit = JumpAlways(img, (DWORD)(p + 6 - img->base), "never show on the visibility update");
        return TRUE;
    }

    if (!IsSetVisible(p)) return FALSE;
    p += SET_VISIBLE_SIZE;
    int size = MovEdxSize(p);
    if (!size || p[size] != 0xff) return FALSE;
    *edit = ClearEdx(img, (DWORD)(p - img->base), size, divider ? "hide the divider on layout" : "hide the container on layout");
    return TRUE;
}

static int Find(const Image *img, Edit *edits) {
    DWORD string = FindUniqueString(img, CHATS_PREF), xref = 0, fields[2];
    if (!string) return Refuse("\"%s\" isn't in the DLL exactly once", CHATS_PREF);
    const Function *ctor = FindLargestReferrer(img, string, &xref);
    if (!ctor) return Refuse("nothing references the Chats pref");
    if (!FindFields(img, ctor, xref, fields)) return Refuse("no Chats fields in the constructor");
    printf("  sidebar constructor %#lx, container +%#lx, divider +%#lx\n", ctor->BeginAddress, fields[0], fields[1]);

    int count = 0;
    const Function *functions[MAX_EDITS];
    DWORD end = min(ctor->BeginAddress + SITE_WINDOW, img->text->VirtualAddress + img->text->Misc.VirtualSize - 64);
    for (DWORD rva = ctor->BeginAddress; rva < end; rva++) {
        const BYTE *p = img->base + rva;
        if ((p[0] != 0x48 && p[0] != 0x49) || p[1] != 0x8b || (p[2] & 0xf8) != 0x88 || (p[2] & 7) == 4) continue;
        DWORD field = *(const DWORD *)(p + 3);
        if (field != fields[0] && field != fields[1]) continue;
        Edit edit;
        if (!MatchSite(img, rva, field == fields[1], &edit)) continue;
        if (count == MAX_EDITS) return Refuse("too many Chats visibility sites");
        functions[count] = FunctionAt(img, rva);
        edits[count++] = edit;
    }

    // One update site and one layout site for the container, and the divider beside it.
    if (count != 3 || functions[1] != functions[2] || edits[0].size != 1)
        return Refuse("expected 3 Chats visibility sites, found %d", count);
    return count;
}

PATCH("chats", Find)
