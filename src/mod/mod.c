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

#define MAX_PUBLISHED 4
#define MAX_RETIRED 64

static CRITICAL_SECTION PublishLock;
static struct {
    wchar_t name[32], path[PATH_SIZE];
} Published[MAX_PUBLISHED];
static int PublishedCount;
static wchar_t Retired[MAX_RETIRED][PATH_SIZE];
static int RetiredCount;
static unsigned PublishNumber;

static void DeleteAllIn(const wchar_t *folder) {
    wchar_t pattern[PATH_SIZE], path[PATH_SIZE];
    swprintf(pattern, PATH_SIZE, L"%ls\\*", folder);
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        swprintf(path, PATH_SIZE, L"%ls\\%ls", folder, found.cFileName);
        DeleteFileW(path);
    } while (FindNextFileW(search, &found));
    FindClose(search);
}

void PublishStart(void) {
    InitializeCriticalSection(&PublishLock);
    wchar_t folder[PATH_SIZE];
    HomePathW(folder, L"web\\live");
    CreateDirectoryW(folder, NULL);
    DeleteAllIn(folder);
}

void PublishPath(wchar_t *out) {
    wchar_t name[64];
    swprintf(name, 64, L"web\\live\\%u", (unsigned)InterlockedIncrement((LONG *)&PublishNumber));
    HomePathW(out, name);
}

// Anything a reader still has open is tried again the next time round.
void PublishCommit(const wchar_t *name, const wchar_t *path) {
    EnterCriticalSection(&PublishLock);
    int slot = 0;
    while (slot < PublishedCount && wcscmp(Published[slot].name, name)) slot++;
    if (slot == PublishedCount && PublishedCount < MAX_PUBLISHED) wcscpy(Published[PublishedCount++].name, name);
    if (slot < MAX_PUBLISHED) {
        if (Published[slot].path[0] && RetiredCount < MAX_RETIRED) wcscpy(Retired[RetiredCount++], Published[slot].path);
        wcscpy(Published[slot].path, path);
    }
    for (int i = RetiredCount - 1; i >= 0; i--)
        if (DeleteFileW(Retired[i])) wcscpy(Retired[i], Retired[--RetiredCount]);
    LeaveCriticalSection(&PublishLock);
}

void PublishFile(const wchar_t *name, const void *data, size_t size) {
    wchar_t path[PATH_SIZE];
    PublishPath(path);
    if (WriteWholeFile(path, data, size)) PublishCommit(name, path);
}

BOOL PublishedPath(const wchar_t *name, wchar_t *out) {
    EnterCriticalSection(&PublishLock);
    BOOL found = FALSE;
    for (int i = 0; i < PublishedCount && !found; i++) {
        if (wcscmp(Published[i].name, name)) continue;
        wcscpy(out, Published[i].path);
        found = TRUE;
    }
    LeaveCriticalSection(&PublishLock);
    return found;
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
    PublishStart();
    SettingsStart(base);
    PatchesStart(base);
    TaskbarStart();
    // Says this Aside has the mod, for as long as it runs, so a shortcut can open a window in it instead of asking to close it.
    wchar_t mark[64];
    swprintf(mark, 64, L"Local\\AsideLateMod-%lu", GetCurrentProcessId());
    CreateMutexW(NULL, FALSE, mark);
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
