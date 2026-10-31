// Hides Aside's own extension button from the toolbar. Every extension's button is the same class, and
// this one's identity is its extension's ID, found in the object by what it holds.

#include "aside-late.h"
#include "hold.h"

#define MAX_BUTTONS 256

static const char ExtensionId[] = "fjdhphbdlfjogobdofoaagnlnkoibdge";

static void **ButtonVtable;
static DWORD IdHolderField, IdField;

// std::string on the heap: pointer, size, capacity with the long-string bit.
static BOOL HoldsExtensionId(const BYTE *object, DWORD field) {
    struct {
        const char *data;
        size_t size, capacity;
    } string;
    if (!SafeRead(object + field, &string, sizeof string)) return FALSE;
    if (string.size != sizeof ExtensionId - 1 || !(string.capacity >> 63)) return FALSE;
    char text[sizeof ExtensionId - 1];
    return SafeRead(string.data, text, sizeof text) && !memcmp(text, ExtensionId, sizeof text);
}

// Any button shows where its ID lives: one of its fields points at an object holding a string that
// looks like an extension ID. Learning this from Aside's own button needs nothing more.
static BOOL IsOurButton(const BYTE *view) {
    if (IdHolderField) {
        const BYTE *holder;
        return SafeRead(view + IdHolderField, &holder, sizeof holder) && HoldsExtensionId(holder, IdField);
    }
    for (DWORD field = 0x100; field < 0x900; field += 8) {
        const BYTE *holder;
        if (!SafeRead(view + field, &holder, sizeof holder) || !holder) continue;
        for (DWORD id = 0; id < 0x80; id += 8) {
            if (!HoldsExtensionId(holder, id)) continue;
            IdHolderField = field;
            IdField = id;
            ModLog("ask-aside learned: id holder +%#lx, id +%#lx", field, id);
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL LoadsExtensionId(const Lookup *lookup, const RUNTIME_FUNCTION *fn) {
    for (DWORD rva = fn->BeginAddress; rva + 8 <= fn->EndAddress; rva++) {
        const BYTE *p = lookup->base + rva;
        if (p[0] != 0xf3 || p[1] != 0x0f || p[2] != 0x6f || (p[3] & 0xc7) != 0x05) continue;
        ULONGLONG target = (ULONGLONG)rva + 8 + *(const INT32 *)(p + 4);
        if (target >= lookup->rdata->VirtualAddress && target + 16 <= (ULONGLONG)lookup->rdata->VirtualAddress + lookup->rdata->Misc.VirtualSize &&
            !memcmp(lookup->base + target, ExtensionId, 16))
            return TRUE;
    }
    return FALSE;
}

// The button's update function shows it unless it's Aside's own: it loads the extension ID, and has
// mov edx,<any>; xor dl,1; mov rax,[reg]; mov rax,[rax+SetVisible]
static const RUNTIME_FUNCTION *FindUpdate(const Lookup *lookup) {
    DWORD start = lookup->text->VirtualAddress, end = start + lookup->text->Misc.VirtualSize - 16;
    const RUNTIME_FUNCTION *found = NULL;
    for (DWORD rva = start; rva < end; rva++) {
        const BYTE *p = lookup->base + rva;
        if (p[0] != 0x80 || p[1] != 0xf2 || p[2] != 0x01 || p[3] != 0x48 || p[4] != 0x8b) continue;
        if (p[6] != 0x48 || p[7] != 0x8b || p[8] != 0x80 || *(const DWORD *)(p + 9) != (DWORD)HoldSlot * 8) continue;
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, rva);
        if (!fn || !LoadsExtensionId(lookup, fn)) continue;
        if (found && found != fn) return NULL;
        found = fn;
    }
    return found;
}

// The update function reads a field of its own early on: mov rax,[rsi+field]
static DWORD FindOwnField(const Lookup *lookup, const RUNTIME_FUNCTION *update) {
    for (DWORD rva = update->BeginAddress; rva + 7 <= update->EndAddress; rva++) {
        const BYTE *p = lookup->base + rva;
        if (p[0] == 0x48 && p[1] == 0x8b && p[2] == 0x86) return *(const DWORD *)(p + 3);
    }
    return 0;
}

static BOOL StoresField(const Lookup *lookup, const RUNTIME_FUNCTION *fn, DWORD field) {
    for (DWORD rva = fn->BeginAddress; rva + 7 <= fn->EndAddress; rva++) {
        const BYTE *p = lookup->base + rva;
        if ((p[0] & 0xf0) == 0x40 && p[1] == 0x89 && (p[2] & 0xc0) == 0x80 && *(const DWORD *)(p + 3) == field) return TRUE;
    }
    return FALSE;
}

// The constructor is the one caller of the update function that also sets that field.
static DWORD FindButtonClass(const Lookup *lookup, const RUNTIME_FUNCTION *update) {
    DWORD field = FindOwnField(lookup, update);
    if (!field) return 0;
    DWORD start = lookup->text->VirtualAddress, end = start + lookup->text->Misc.VirtualSize - 5, found = 0;
    const RUNTIME_FUNCTION *last = NULL;
    for (DWORD rva = start; rva < end; rva++) {
        const BYTE *p = lookup->base + rva;
        if (p[0] != 0xe8 || rva + 5 + *(const INT32 *)(p + 1) != update->BeginAddress) continue;
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, rva);
        if (!fn || fn == last || !StoresField(lookup, fn, field)) continue;
        last = fn;
        DWORD vtable = LookupVtableInstalledBy(lookup, fn);
        if (!vtable) continue;
        if (found && found != vtable) return 0;
        found = vtable;
    }
    return found;
}

PATCH_START() {
    if (!LookupVisibility(lookup, &HoldSlot, &HoldVisibleOffset)) return "how views are shown and hidden";
    const RUNTIME_FUNCTION *update = FindUpdate(lookup);
    if (!update) return "the toolbar button's update function";
    DWORD vtable = FindButtonClass(lookup, update);
    if (!vtable) return "the toolbar button's class";
    ButtonVtable = (void **)(base + vtable);
    return NULL;
}

PATCH_ENABLE() {
    HoldShouldTake = IsOurButton;
    if (!HoldClass(ButtonVtable)) {
        ModLog("ask-aside couldn't hook the toolbar's buttons");
        return;
    }
    void *buttons[MAX_BUTTONS];
    int count = FindObjects(ButtonVtable, buttons, MAX_BUTTONS);
    for (int i = 0; i < count; i++)
        if (IsOurButton(buttons[i])) HoldView(buttons[i]);
    ModLog("ask-aside: %d buttons live, %d held", count, HoldViewCount);
}

PATCH_DISABLE() {
    HoldReleaseAll();
}
