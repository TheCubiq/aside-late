// Hides the Chats section of the sidebar, and its divider.

#include "aside-late.h"
#include "hold.h"

#define CHATS_PREF "aside.vertical_tabs.chats_section_expanded"
#define MAX_SIDEBARS 32

typedef void (*PaintBackground)(void *view, void *canvas);

static void **SidebarVtable;
static DWORD SidebarRva, ContainerField, DividerField;
static int PaintSlot;
static PaintBackground OriginalPaint;

// The largest function that reads the pref is the sidebar's constructor, and right after reading it
// the constructor stores the Chats container and then its divider.
static const char *FindSidebar(const Lookup *lookup) {
    DWORD strings[4];
    if (LookupStrings(lookup, CHATS_PREF, strings, 4) != 1) return "the Chats pref";
    size_t count;
    const Reference *sites = LookupCode(lookup, strings[0], &count);
    const RUNTIME_FUNCTION *largest = NULL;
    DWORD site = 0;
    for (size_t i = 0; i < count; i++) {
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, sites[i].site);
        if (fn && (!largest || fn->EndAddress - fn->BeginAddress > largest->EndAddress - largest->BeginAddress)) {
            largest = fn;
            site = sites[i].site;
        }
    }
    if (!largest) return "the sidebar's constructor";
    SidebarRva = LookupVtableInstalledBy(lookup, largest);
    if (!SidebarRva) return "the sidebar's class";
    int found = 0;
    for (DWORD rva = site; rva + 7 <= largest->EndAddress && found < 2; rva++) {
        const BYTE *p = lookup->base + rva;
        if ((p[0] != 0x48 && p[0] != 0x4c) || p[1] != 0x89 || (p[2] & 0xc7) != 0x86) continue;
        DWORD field = *(const DWORD *)(p + 3);
        if (!found) ContainerField = field;
        else if (field == ContainerField) continue;
        else DividerField = field;
        found++;
    }
    return found == 2 ? NULL : "the Chats fields";
}

static void HoldField(BYTE *sidebar, DWORD field) {
    BYTE *view;
    void *vtable;
    if (!SafeRead(sidebar + field, &view, sizeof view) || !view) return;
    if (!SafeRead(view, &vtable, sizeof vtable) || !IsInAside(vtable)) return;
    HoldView(view);
}

static void HoldSidebar(BYTE *sidebar) {
    HoldField(sidebar, ContainerField);
    HoldField(sidebar, DividerField);
}

// A new window's sidebar gets here when it first draws.
static void OnSidebarPaint(void *view, void *canvas) {
    HoldSidebar(view);
    OriginalPaint(view, canvas);
}

PATCH_START() {
    DWORD method;
    if (!LookupVisibility(lookup, &HoldSlot, &HoldVisibleOffset)) return "how views are shown and hidden";
    if (!LookupViewMethodSlot(lookup, "View::OnPaintBackground", &PaintSlot, &method)) return "how views draw";
    const char *missing = FindSidebar(lookup);
    if (missing) return missing;
    SidebarVtable = (void **)(base + SidebarRva);
    return NULL;
}

PATCH_ENABLE() {
    if (!HookVtable(SidebarVtable, PaintSlot, OnSidebarPaint, (void **)&OriginalPaint)) {
        ModLog("chats couldn't hook the sidebar");
        return;
    }
    void *sidebars[MAX_SIDEBARS];
    int count = FindObjects(SidebarVtable, sidebars, MAX_SIDEBARS);
    for (int i = 0; i < count; i++) HoldSidebar(sidebars[i]);
    ModLog("chats: %d sidebars live, %d views held, SetVisible slot %d, visible +%#lx", count, HoldViewCount, HoldSlot, HoldVisibleOffset);
}

PATCH_DISABLE() {
    HoldReleaseAll();
}
