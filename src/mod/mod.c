// The DLL loaded into Aside. It loads the patches, and gives them the SDK in aside-late.h.

#include "internal.h"

#include <stdarg.h>

wchar_t Home[MAX_PATH];
HMODULE ThisModule;

static FILE *Log;

void ModLog(const char *format, ...) {
    if (!Log) return;
    va_list args;
    va_start(args, format);
    vfprintf(Log, format, args);
    va_end(args);
    fputc('\n', Log);
    fflush(Log);
}

void HomePathW(wchar_t *out, const wchar_t *name) {
    swprintf(out, PATH_SIZE, L"%ls\\%ls", Home, name);
}

void HomeFile(const char *name, char *out, size_t size) {
    snprintf(out, size, "%ls\\%s", Home, name);
}

BOOL WriteWholeFile(const wchar_t *path, const void *data, size_t size) {
    FILE *file = _wfopen(path, L"wb");
    if (!file) return FALSE;
    BOOL wrote = fwrite(data, 1, size, file) == size;
    return fclose(file) == 0 && wrote;
}

char *ReadWholeFile(const wchar_t *path, size_t *size) {
    FILE *file = _wfopen(path, L"rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *data = length >= 0 ? malloc((size_t)length + 1) : NULL;
    if (data && fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        data = NULL;
    }
    fclose(file);
    if (!data) return NULL;
    data[length] = 0;
    if (size) *size = (size_t)length;
    return data;
}

// Runs while Aside's loader is waiting, so every patch is in place before any window exists.
static void Start(void) {
    BYTE *base = (BYTE *)GetModuleHandleW(L"Aside.dll");
    if (!base) return;  // The launcher loads this too, to ask it about patches.
    if (!GetEnvironmentVariableW(L"ASIDE_LATE_HOME", Home, MAX_PATH)) return;
    wchar_t path[PATH_SIZE];
    HomePathW(path, L"mod.log");
    Log = _wfopen(path, L"w");
    HooksInit();
    SettingsStart(base);
    PatchesStart(base);
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        ThisModule = instance;
        DisableThreadLibraryCalls(instance);
        Start();
    } else if (reason == DLL_PROCESS_DETACH && Home[0]) {
        PatchesClean();
    }
    return TRUE;
}
