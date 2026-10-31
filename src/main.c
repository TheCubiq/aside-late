// Starts Aside and patches that one process from a list of patches that find their own
// code in whatever build is loaded. Nothing on disk changes and no offset is stored.

#include "aside-late.h"

Patch Patches[MAX_PATCHES];
int PatchCount;

static const char *ManifestField(const char *manifest, const char *key) {
    char quoted[32];
    snprintf(quoted, sizeof quoted, "\"%s\"", key);
    const char *at = strstr(manifest, quoted);
    at = at ? strchr(at + strlen(quoted), '"') : NULL;
    const char *end = at ? strchr(at + 1, '"') : NULL;
    if (!end) {
        printf("patch.json has no \"%s\":\n%s\n", key, manifest);
        exit(1);
    }
    size_t len = end - at - 1;
    char *value = malloc(len + 1);
    if (!value) exit(1);
    memcpy(value, at + 1, len);
    value[len] = 0;
    return value;
}

void RegisterPatch(const char *manifest, FindEdits find, BOOL inAside) {
    if (PatchCount == MAX_PATCHES) exit(1);
    Patches[PatchCount++] = (Patch){
        .id = ManifestField(manifest, "id"),
        .title = ManifestField(manifest, "title"),
        .description = ManifestField(manifest, "description"),
        .find = find,
        .inAside = inAside,
    };
}

static int Scan(const wchar_t *path) {
    printf("%ls\n", path);
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return printf("couldn't open it\n"), 1;
    Image img = {0};
    int failed = !LoadImageFile(file, &img);
    CloseHandle(file);
    for (int i = 0; !failed && i < PatchCount; i++) {
        if (Patches[i].skipped) continue;
        Edit edits[MAX_EDITS];
        if (!FindAndPrintEdits(&Patches[i], &img, edits)) failed = 1;  // RUNS_IN_ASIDE is fine.
    }
    FreeImageFile(&img);
    return failed;
}

static BOOL Skip(const wchar_t *id) {
    for (int i = 0; i < PatchCount; i++) {
        wchar_t name[64];
        swprintf(name, 64, L"%hs", Patches[i].id);
        if (!wcscmp(name, id)) return Patches[i].skipped = TRUE;
    }
    printf("no patch called %ls\n", id);
    return FALSE;
}

static void FindUserDataDir(int first, int argc, wchar_t **argv, wchar_t *out) {
    static const wchar_t option[] = L"--user-data-dir=";
    size_t optionSize = wcslen(option);
    ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\Aside\\User Data", out, MAX_PATH);
    for (int i = first; i < argc; i++)
        if (!wcsncmp(argv[i], option, optionSize)) GetFullPathNameW(argv[i] + optionSize, MAX_PATH, out, NULL);
}

int wmain(int argc, wchar_t **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    wchar_t exe[MAX_PATH];
    ExpandEnvironmentStringsW(L"%ProgramFiles%\\Aside\\Application\\Aside.exe", exe, MAX_PATH);

    int first = 1;
    while (first < argc) {
        if (!wcscmp(argv[first], L"--list")) {
            for (int i = 0; i < PatchCount; i++) printf("%-28s %s\n", Patches[i].id, Patches[i].description);
            return 0;
        } else if (!wcscmp(argv[first], L"--without") && first + 1 < argc) {
            if (!Skip(argv[first + 1])) return 1;
            first += 2;
        } else if (!wcscmp(argv[first], L"--exe") && first + 1 < argc) {
            wcsncpy(exe, argv[first + 1], MAX_PATH - 1);
            exe[MAX_PATH - 1] = 0;
            first += 2;
        } else if (!wcscmp(argv[first], L"--scan") && first + 1 < argc) {
            int failed = 0;
            for (int i = first + 1; i < argc; i++) failed |= Scan(argv[i]);
            return failed;
        } else {
            break;
        }
    }

    wchar_t userDataDir[MAX_PATH];
    FindUserDataDir(first, argc, argv, userDataDir);
    if (!ReadyToLaunch(userDataDir)) return 1;

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
    return Launch(exe, commandLine);
}
