// Makes the browser window glassy, like Windows Terminal. Aside asks Windows for Mica, which only tints the window from the
// wallpaper. This asks for acrylic instead, which blurs whatever is behind the window, and lays the theme's background colour
// over it at the opacity that's set. Turned off, Mica comes back.

#include "resolve.h"

#include <stdlib.h>
#include <string.h>

#define MAX_WINDOWS 64
#define MAX_CANVASES 8
#define MAX_BROWSERS 32
#define DWMWA_DARK 20
#define DWMWA_BACKDROP 38
#define BACKDROP_MICA 2
#define BACKDROP_ACRYLIC 3
#define DRAW_COLOR_AFTER_CONCAT 7
#define BLEND_SOURCE_OVER 3

typedef HRESULT (WINAPI *DwmGetAttribute)(HWND window, DWORD attribute, void *value, DWORD size);
typedef HRESULT (WINAPI *DwmSetAttribute)(HWND window, DWORD attribute, const void *value, DWORD size);
typedef void (*Paint)(void *view, void *canvas);
typedef void (*SchedulePaint)(void *view);
typedef void (*DrawColor)(void *paint, const float *color, int mode);  // cc::PaintCanvas::drawColor(SkColor4f, SkBlendMode).

static Bindings Native;
static BYTE *Base;
static void **SetAttributeSlot;  // Where Aside.dll calls DwmSetWindowAttribute through.
static DwmGetAttribute GetAttribute;
static DwmSetAttribute SetAttribute;
static Paint OriginalPaint;
static HWND Glazed[MAX_WINDOWS];
static int GlazedCount;
static BOOL Enabled, Dark = TRUE;
static int Opacity = 40;

// The colours of Aside's own background, light and dark, as an SkColor4f.
static const float LightTint[3] = {0xea / 255.f, 0xeb / 255.f, 0xee / 255.f};
static const float DarkTint[3] = {0x2d / 255.f, 0x2d / 255.f, 0x2d / 255.f};

// Canvases whose drawColor has been checked, or found not to be where it's expected.
typedef struct {
    void **vtable;
    BOOL good;
} Checked;
static Checked Canvases[MAX_CANVASES];
static int CanvasCount;

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

// Recording a colour is: mov byte [rax],8 (the op), movups [rax+4],xmm0 (the colour), then the mode at [rax+14h]. drawColor
// stores the mode it was given, and the clear right after it stores 1.
static BOOL RecordsColor(const BYTE *method, BOOL clear) {
    static const BYTE op[] = {0xc6, 0x00, 0x08, 0x0f, 0x11, 0x40, 0x04};
    static const BYTE source[] = {0xc7, 0x40, 0x14, 0x01, 0x00, 0x00, 0x00};
    BYTE code[0x80];
    if (!SafeRead(method, code, sizeof code)) return FALSE;
    for (size_t i = 0; i + sizeof op + sizeof source <= sizeof code; i++)
        if (!memcmp(code + i, op, sizeof op)) return (memcmp(code + i + sizeof op, source, sizeof source) == 0) == clear;
    return FALSE;
}

static DrawColor DrawColorOf(void *paint) {
    void **vtable = *(void ***)paint;
    int slot = Native.canvasConcatSlot + DRAW_COLOR_AFTER_CONCAT;
    for (int i = 0; i < CanvasCount; i++)
        if (Canvases[i].vtable == vtable) return Canvases[i].good ? (DrawColor)vtable[slot] : NULL;
    BOOL good = RecordsColor(vtable[slot], FALSE) && RecordsColor(vtable[slot + 1], TRUE);
    if (!good) ModLog("window-glass found no drawColor on a canvas at +%#llx", (unsigned long long)((BYTE *)vtable - Base));
    if (CanvasCount < MAX_CANVASES) Canvases[CanvasCount++] = (Checked){vtable, good};
    return good ? (DrawColor)vtable[slot] : NULL;
}

// The browser's view is under the sidebar, the top bar and the page, so its colour is what lies over the acrylic.
static void BrowserPaint(void *view, void *canvas) {
    if (Enabled && Opacity > 0) {
        void *paint = *(void **)((BYTE *)canvas + Native.canvasPaintOffset);
        DrawColor draw = paint ? DrawColorOf(paint) : NULL;
        const float *tint = Dark ? DarkTint : LightTint;
        const float color[4] = {tint[0], tint[1], tint[2], Opacity / 100.f};
        if (draw) draw(paint, color, BLEND_SOURCE_OVER);
    }
    OriginalPaint(view, canvas);
}

static void RepaintAll(void) {
    void *found[MAX_BROWSERS];
    int count = FindObjects(Base + Native.browserVtable, found, MAX_BROWSERS);
    for (int i = 0; i < count; i++) {
        void *vtable;
        if (SafeRead(found[i], &vtable, sizeof vtable) && vtable == Base + Native.browserVtable)
            ((SchedulePaint)(Base + Native.schedulePaint))(found[i]);
    }
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
// It also says here whether the window is dark, which picks the tint. The slot may still hold the delay-load stub, which
// would write the real function over this hook, so this never calls it.
static HRESULT WINAPI OnSetAttribute(HWND window, DWORD attribute, const void *value, DWORD size) {
    if (attribute == DWMWA_BACKDROP && size == sizeof(DWORD) && *(const DWORD *)value == BACKDROP_MICA) {
        Remember(window);
        static const DWORD acrylic = BACKDROP_ACRYLIC;
        return SetAttribute(window, attribute, &acrylic, sizeof acrylic);
    }
    if (attribute == DWMWA_DARK && size == sizeof(BOOL) && !*(const BOOL *)value != !Dark) {
        Dark = *(const BOOL *)value;
        RepaintAll();
    }
    return SetAttribute(window, attribute, value, size);
}

// Windows that are already open when the patch is switched on: the ones Aside gave Mica.
static BOOL CALLBACK FindOpen(HWND window, LPARAM unused) {
    (void)unused;
    DWORD process = 0, backdrop = 0;
    GetWindowThreadProcessId(window, &process);
    if (process == GetCurrentProcessId() && SUCCEEDED(GetAttribute(window, DWMWA_BACKDROP, &backdrop, sizeof backdrop)) &&
        backdrop == BACKDROP_MICA) {
        Remember(window);
        BOOL dark = TRUE;
        if (SUCCEEDED(GetAttribute(window, DWMWA_DARK, &dark, sizeof dark))) Dark = dark;
    }
    return TRUE;
}

PATCH_OPTION() {
    if (!strcmp(key, "opacity")) Opacity = atoi(value);
    if (Enabled) RepaintAll();
}

PATCH_START() {
    Base = base;
    SetAttributeSlot = FindDelayedImport(base, "dwmapi.dll", "DwmSetWindowAttribute");
    if (!SetAttributeSlot) return "DwmSetWindowAttribute";
    return ResolveBindings(lookup, &Native);
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
    HookVtable((void **)(Base + Native.browserVtable), Native.paintSlot, BrowserPaint, (void **)&OriginalPaint);
    Enabled = TRUE;
    EnumWindows(FindOpen, 0);
    for (int i = 0; i < GlazedCount; i++) Glaze(Glazed[i], TRUE);
    RepaintAll();
}

PATCH_DISABLE() {
    if (Enabled)
        for (int i = 0; i < GlazedCount; i++)
            if (IsWindow(Glazed[i])) Glaze(Glazed[i], FALSE);
    GlazedCount = 0;
    Enabled = FALSE;
    // The paint hook is already off, so this draws Aside's own.
    if (Native.browserVtable) RepaintAll();
}
