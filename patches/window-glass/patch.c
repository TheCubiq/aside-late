// Makes the browser window glassy. Aside asks Windows for Mica, which only tints the window from the wallpaper. This asks
// for acrylic instead, which blurs whatever is behind the window, and puts Mica back when turned off.

#include "aside-late.h"

#include <string.h>

#define MAX_WINDOWS 64
#define DWMWA_BACKDROP 38
#define BACKDROP_MICA 2
#define BACKDROP_ACRYLIC 3

typedef HRESULT (WINAPI *DwmGetAttribute)(HWND window, DWORD attribute, void *value, DWORD size);
typedef HRESULT (WINAPI *DwmSetAttribute)(HWND window, DWORD attribute, const void *value, DWORD size);

static void **SetAttributeSlot;  // Where Aside.dll calls DwmSetWindowAttribute through.
static DwmGetAttribute GetAttribute;
static DwmSetAttribute SetAttribute;
static HWND Glazed[MAX_WINDOWS];
static int GlazedCount;
static BOOL Enabled;

// dwmapi is delay-loaded, so its functions are in the delay-load table rather than the imports.
static void **FindDelayedImport(BYTE *module, const char *dll, const char *name) {
    const IMAGE_NT_HEADERS64 *nt = (const void *)(module + ((const IMAGE_DOS_HEADER *)module)->e_lfanew);
    const IMAGE_DATA_DIRECTORY *directory = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    if (!directory->VirtualAddress) return NULL;
    for (const IMAGE_DELAYLOAD_DESCRIPTOR *entry = (const void *)(module + directory->VirtualAddress); entry->DllNameRVA; entry++) {
        if (_stricmp((const char *)module + entry->DllNameRVA, dll)) continue;
        const IMAGE_THUNK_DATA64 *names = (const void *)(module + entry->ImportNameTableRVA);
        void **slots = (void **)(module + entry->ImportAddressTableRVA);
        for (size_t i = 0; names[i].u1.AddressOfData; i++) {
            if (IMAGE_SNAP_BY_ORDINAL64(names[i].u1.Ordinal)) continue;
            const IMAGE_IMPORT_BY_NAME *import = (const void *)(module + names[i].u1.AddressOfData);
            if (!strcmp((const char *)import->Name, name)) return &slots[i];
        }
    }
    return NULL;
}

static void Remember(HWND window) {
    for (int i = 0; i < GlazedCount; i++)
        if (Glazed[i] == window) return;
    for (int i = 0; i < GlazedCount; i++)
        if (!IsWindow(Glazed[i])) Glazed[i--] = Glazed[--GlazedCount];
    if (GlazedCount < MAX_WINDOWS) Glazed[GlazedCount++] = window;
}

static void Glaze(HWND window, BOOL on) {
    DWORD backdrop = on ? BACKDROP_ACRYLIC : BACKDROP_MICA;
    SetAttribute(window, DWMWA_BACKDROP, &backdrop, sizeof backdrop);
}

// Aside asks for Mica on each window it wants a backdrop for, as it makes it, so that's where a window gets glass instead.
// The slot may still hold the delay-load stub, which would write the real function over this hook, so this never calls it.
static HRESULT WINAPI OnSetAttribute(HWND window, DWORD attribute, const void *value, DWORD size) {
    if (attribute == DWMWA_BACKDROP && size == sizeof(DWORD) && *(const DWORD *)value == BACKDROP_MICA) {
        Remember(window);
        static const DWORD acrylic = BACKDROP_ACRYLIC;
        return SetAttribute(window, attribute, &acrylic, sizeof acrylic);
    }
    return SetAttribute(window, attribute, value, size);
}

// Windows that are already open when the patch is switched on: the ones Aside gave Mica.
static BOOL CALLBACK FindOpen(HWND window, LPARAM unused) {
    (void)unused;
    DWORD process = 0, backdrop = 0;
    GetWindowThreadProcessId(window, &process);
    if (process == GetCurrentProcessId() && SUCCEEDED(GetAttribute(window, DWMWA_BACKDROP, &backdrop, sizeof backdrop)) &&
        backdrop == BACKDROP_MICA)
        Remember(window);
    return TRUE;
}

PATCH_START() {
    (void)lookup;
    SetAttributeSlot = FindDelayedImport(base, "dwmapi.dll", "DwmSetWindowAttribute");
    return SetAttributeSlot ? NULL : "DwmSetWindowAttribute";
}

PATCH_ENABLE() {
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    GetAttribute = dwm ? (DwmGetAttribute)(void *)GetProcAddress(dwm, "DwmGetWindowAttribute") : NULL;
    SetAttribute = dwm ? (DwmSetAttribute)(void *)GetProcAddress(dwm, "DwmSetWindowAttribute") : NULL;
    void *unused;
    if (!GetAttribute || !SetAttribute || !HookVtable(SetAttributeSlot, 0, OnSetAttribute, &unused)) {
        ModLog("window-glass couldn't hook DwmSetWindowAttribute");
        return;
    }
    Enabled = TRUE;
    EnumWindows(FindOpen, 0);
    for (int i = 0; i < GlazedCount; i++) Glaze(Glazed[i], TRUE);
}

PATCH_DISABLE() {
    if (Enabled)
        for (int i = 0; i < GlazedCount; i++)
            if (IsWindow(Glazed[i])) Glaze(Glazed[i], FALSE);
    GlazedCount = 0;
    Enabled = FALSE;
}
