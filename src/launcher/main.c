// Starts Aside with the mod loaded into that one process. Finding, patching and everything else
// happens inside, so nothing on disk changes and no offset is stored.

#include "launcher.h"

// Everything aside-late keeps lives here. A trial can point it somewhere else.
static void PrepareHome(wchar_t *home) {
    if (!GetEnvironmentVariableW(L"ASIDE_LATE_HOME", home, MAX_PATH)) {
        ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\AsideLate", home, MAX_PATH);
        SetEnvironmentVariableW(L"ASIDE_LATE_HOME", home);
    }
    wchar_t web[MAX_PATH];
    swprintf(web, MAX_PATH, L"%ls\\web", home);
    CreateDirectoryW(home, NULL);
    CreateDirectoryW(web, NULL);
}

static void FindUserDataDir(int first, int argc, wchar_t **argv, wchar_t *out) {
    static const wchar_t option[] = L"--user-data-dir=";
    size_t optionSize = wcslen(option);
    ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\Aside\\User Data", out, MAX_PATH);
    for (int i = first; i < argc; i++)
        if (!wcsncmp(argv[i], option, optionSize)) GetFullPathNameW(argv[i] + optionSize, MAX_PATH, out, NULL);
}

// The mod knows about patches, so a question about them goes to it.
static FARPROC ModFunction(const char *name) {
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t *slash = wcsrchr(path, L'\\');
    if (!len || len >= MAX_PATH || !slash) return NULL;
    wcscpy(slash + 1, L"aside-late-mod.dll");
    HMODULE mod = LoadLibraryW(path);
    return mod ? GetProcAddress(mod, name) : NULL;
}

int wmain(int argc, wchar_t **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    wchar_t exe[MAX_PATH];
    ExpandEnvironmentStringsW(L"%ProgramFiles%\\Aside\\Application\\Aside.exe", exe, MAX_PATH);
    wchar_t home[MAX_PATH];
    PrepareHome(home);

    wchar_t skipped[1024] = L"";
    int first = 1;
    while (first < argc) {
        if (!wcscmp(argv[first], L"--list")) {
            typedef int (*ListPatches)(const wchar_t *);
            ListPatches list = (ListPatches)(void *)ModFunction("ModListPatches");
            return list ? list(home) : (printf("couldn't load aside-late-mod.dll\n"), 1);
        } else if (!wcscmp(argv[first], L"--without") && first + 1 < argc) {
            wcsncat(skipped, argv[first + 1], 1022 - wcslen(skipped));
            wcscat(skipped, L",");
            first += 2;
        } else if (!wcscmp(argv[first], L"--exe") && first + 1 < argc) {
            wcsncpy(exe, argv[first + 1], MAX_PATH - 1);
            exe[MAX_PATH - 1] = 0;
            first += 2;
        } else if (!wcscmp(argv[first], L"--scan") && first + 1 < argc) {
            typedef int (*Scan)(const wchar_t *, const wchar_t *);
            Scan scan = (Scan)(void *)ModFunction("ModScan");
            if (!scan) return printf("couldn't load aside-late-mod.dll\n"), 1;
            int failed = 0;
            for (int i = first + 1; i < argc; i++) failed |= scan(home, argv[i]);
            return failed;
        } else {
            break;
        }
    }

    wchar_t userDataDir[MAX_PATH];
    FindUserDataDir(first, argc, argv, userDataDir);
    if (!ReadyToLaunch(userDataDir, home)) return 1;

    size_t size = wcslen(exe) + 3;
    for (int i = first; i < argc; i++) size += wcslen(argv[i]) + 3;
    wchar_t *commandLine = malloc(size * sizeof *commandLine);
    if (!commandLine) return 1;
    swprintf(commandLine, size, L"\"%ls\"", exe);
    for (int i = first; i < argc; i++) {
        wcscat(commandLine, L" \"");
        wcscat(commandLine, argv[i]);
        wcscat(commandLine, L"\"");
    }
    return Launch(exe, commandLine, skipped);
}
