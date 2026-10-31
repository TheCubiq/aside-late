#include "mod.h"

#include <stdarg.h>
#include <stdio.h>

#define MAX_MODS 32

static struct {
    const char *id;
    ModStart start;
} Mods[MAX_MODS];
static int ModCount;
static FILE *Log;

void RegisterMod(const char *id, ModStart start) {
    if (ModCount < MAX_MODS) {
        Mods[ModCount].id = id;
        Mods[ModCount++].start = start;
    }
}

void ModLog(const char *format, ...) {
    if (!Log) return;
    va_list args;
    va_start(args, format);
    vfprintf(Log, format, args);
    va_end(args);
    fputc('\n', Log);
    fflush(Log);
}

BOOL SwapVtableEntry(void **vtable, int slot, void *replacement, void **original) {
    if (vtable[slot] == replacement) return TRUE;
    DWORD protect;
    if (!VirtualProtect(vtable + slot, sizeof *vtable, PAGE_READWRITE, &protect)) return FALSE;
    *original = vtable[slot];
    vtable[slot] = replacement;
    VirtualProtect(vtable + slot, sizeof *vtable, protect, &protect);
    return TRUE;
}

BOOL HookImport(BYTE *module, const char *dll, const char *name, void *replacement, void **original) {
    const IMAGE_NT_HEADERS64 *nt = (const void *)(module + ((const IMAGE_DOS_HEADER *)module)->e_lfanew);
    const IMAGE_DATA_DIRECTORY *directory = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory->VirtualAddress) return FALSE;
    const IMAGE_IMPORT_DESCRIPTOR *import = (const void *)(module + directory->VirtualAddress);
    for (; import->Name; import++) {
        if (_stricmp((const char *)module + import->Name, dll) || !import->OriginalFirstThunk) continue;
        const IMAGE_THUNK_DATA64 *names = (const void *)(module + import->OriginalFirstThunk);
        IMAGE_THUNK_DATA64 *slots = (void *)(module + import->FirstThunk);
        for (size_t i = 0; names[i].u1.AddressOfData; i++) {
            if (IMAGE_SNAP_BY_ORDINAL64(names[i].u1.Ordinal)) continue;
            const IMAGE_IMPORT_BY_NAME *entry = (const void *)(module + names[i].u1.AddressOfData);
            if (strcmp((const char *)entry->Name, name)) continue;
            DWORD protect;
            if (!VirtualProtect(&slots[i], sizeof slots[i], PAGE_READWRITE, &protect)) return FALSE;
            *original = (void *)slots[i].u1.Function;
            slots[i].u1.Function = (ULONGLONG)replacement;
            VirtualProtect(&slots[i], sizeof slots[i], protect, &protect);
            return TRUE;
        }
    }
    return FALSE;
}

BOOL SafeRead(const void *from, void *to, size_t size) {
    SIZE_T got;
    return ReadProcessMemory(GetCurrentProcess(), from, to, size, &got) && got == size;
}

static void OpenLog(void) {
    char path[MAX_PATH];
    if (!GetEnvironmentVariableA("ASIDE_LATE_HOME", path, MAX_PATH)) return;
    strncat(path, "\\mod.log", MAX_PATH - strlen(path) - 1);
    Log = fopen(path, "w");
}

static BOOL IsSkipped(const char *id) {
    char skipped[1024];
    DWORD len = GetEnvironmentVariableA("ASIDE_LATE_SKIP", skipped, sizeof skipped);
    if (!len || len >= sizeof skipped) return FALSE;
    for (char *name = strtok(skipped, ","); name; name = strtok(NULL, ","))
        if (!strcmp(name, id)) return TRUE;
    return FALSE;
}

// Runs while Aside's loader is waiting, so every mod is in place before any window exists.
static void Start(void) {
    OpenLog();
    BYTE *base = (BYTE *)GetModuleHandleW(L"Aside.dll");
    if (!base) {
        ModLog("Aside.dll isn't loaded");
        return;
    }
    ULONGLONG started = GetTickCount64();
    Lookup lookup;
    if (!LookupInit(&lookup, base, (ULONGLONG)base)) {
        ModLog("couldn't index Aside.dll");
        return;
    }
    for (int i = 0; i < ModCount; i++)
        if (!IsSkipped(Mods[i].id)) Mods[i].start(&lookup, base);
    LookupFree(&lookup);
    ModLog("ready in %llu ms", GetTickCount64() - started);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        Start();
    }
    return TRUE;
}
