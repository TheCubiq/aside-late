// Starts Aside with the mod loaded into that one process. Finding, patching and everything else
// happens inside, so nothing on disk changes and no offset is stored.

#include "launcher.h"

#include <objbase.h>
#include <propsys.h>
#include <shlobj.h>

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

// The shortcut's app ID is what the taskbar groups by, so its windows can be told to appear under its pin.
static void SetShortcutAppId(IShellLinkW *link, const wchar_t *appId) {
    static const IID storeId = {0x886d8eeb, 0x8cf2, 0x4446, {0x8d, 0x02, 0xcd, 0xba, 0x1d, 0xbd, 0xcf, 0x99}};
    static const PROPERTYKEY idKey = {{0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5};
    IPropertyStore *store = NULL;
    if (FAILED(link->lpVtbl->QueryInterface(link, &storeId, (void **)&store))) return;
    PROPVARIANT value;
    memset(&value, 0, sizeof value);
    value.vt = VT_LPWSTR;
    value.pwszVal = (wchar_t *)appId;
    store->lpVtbl->SetValue(store, &idKey, &value);
    store->lpVtbl->Commit(store);
    store->lpVtbl->Release(store);
}

// A shortcut to this program, so starting Aside the usual way can mean starting it with the patches. The window asks for one to
// itself instead, with --to, --arguments, --icon and --app-id.
static int MakeShortcut(int first, int argc, wchar_t **argv) {
    wchar_t path[MAX_PATH] = L"", self[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    const wchar_t *target = self, *arguments = NULL, *appId = NULL;
    wchar_t icon[MAX_PATH];
    ExpandEnvironmentStringsW(L"%ProgramFiles%\\Aside\\Application\\Aside.exe", icon, MAX_PATH);
    for (int i = first; i < argc; i++) {
        if (!wcscmp(argv[i], L"--to") && i + 1 < argc) target = argv[++i];
        else if (!wcscmp(argv[i], L"--arguments") && i + 1 < argc) arguments = argv[++i];
        else if (!wcscmp(argv[i], L"--icon") && i + 1 < argc) wcsncpy(icon, argv[++i], MAX_PATH - 1);
        else if (!wcscmp(argv[i], L"--app-id") && i + 1 < argc) appId = argv[++i];
        else wcsncpy(path, argv[i], MAX_PATH - 1);
    }
    if (!path[0]) {
        // The desktop can be somewhere else, such as in OneDrive.
        wchar_t desktop[MAX_PATH];
        if (FAILED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop))) return printf("couldn't find the desktop\n"), 1;
        swprintf(path, MAX_PATH, L"%ls\\Aside (patched).lnk", desktop);
    }

    IShellLinkW *link = NULL;
    IPersistFile *file = NULL;
    CoInitialize(NULL);
    HRESULT result = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&link);
    if (SUCCEEDED(result)) {
        link->lpVtbl->SetPath(link, target);
        if (arguments) link->lpVtbl->SetArguments(link, arguments);
        link->lpVtbl->SetIconLocation(link, icon, 0);
        link->lpVtbl->SetDescription(link, L"Aside with aside-late's patches");
        if (appId) SetShortcutAppId(link, appId);
        result = link->lpVtbl->QueryInterface(link, &IID_IPersistFile, (void **)&file);
    }
    if (SUCCEEDED(result)) result = file->lpVtbl->Save(file, path, TRUE);
    if (file) file->lpVtbl->Release(file);
    if (link) link->lpVtbl->Release(link);
    CoUninitialize();
    if (FAILED(result)) return printf("couldn't make the shortcut (error %#lx)\n", result), 1;
    printf("made %ls\n", path);
    return 0;
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
        } else if (!wcscmp(argv[first], L"--shortcut")) {
            return MakeShortcut(first + 1, argc, argv);
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
