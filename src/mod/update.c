// Updates Aside Late itself. The page finds a newer release on GitHub and asks for it. This downloads that release's exe, checks
// it against the SHA-256 GitHub lists for it, and puts it where shortcuts and pins start Aside Late from. Restarting is Aside's
// own, so the windows and tabs come back. Aside starting itself again, for that or for its own update, goes through Aside Late,
// so it comes back patched, by the newer version once there is one.

#include "internal.h"

#include <bcrypt.h>

#define REPO "TheCubiq/aside-late"
#define KEPT L"Aside Late.exe"
#define PUBLISH_EVERY_MS 250

typedef void (*AttemptRestart)(void);
typedef BOOL(WINAPI *CreateProcessFunction)(const wchar_t *, wchar_t *, SECURITY_ATTRIBUTES *, SECURITY_ATTRIBUTES *, BOOL, DWORD, void *, const wchar_t *, STARTUPINFOW *, PROCESS_INFORMATION *);

static BYTE *Base;
static char Version[64] = "dev";
static wchar_t Browser[PATH_SIZE];  // This Aside.exe.
static CreateProcessFunction OriginalCreateProcess;
static CRITICAL_SECTION Lock;
static volatile LONG Working;
static BOOL Staged;  // A newer Aside Late is in place of the kept copy.
static struct {
    const char *state;  // idle, downloading, ready, restarting or failed.
    char version[64], error[256];
    ULONGLONG received, total, publishedAt;
} Status = {.state = "idle"};

static void Publish(void) {
    char json[512];
    EnterCriticalSection(&Lock);
    int size = snprintf(json, sizeof json, "{\"state\":\"%s\",\"version\":\"%s\",\"received\":%llu,\"total\":%llu,\"error\":\"%s\"}\n",
                        Status.state, Status.version, Status.received, Status.total, Status.error);
    Status.publishedAt = GetTickCount64();
    LeaveCriticalSection(&Lock);
    PublishFile(L"update.json", json, size);
}

static void SetState(const char *state, const char *error) {
    EnterCriticalSection(&Lock);
    Status.state = state;
    snprintf(Status.error, sizeof Status.error, "%s", error ? error : "");
    LeaveCriticalSection(&Lock);
    Publish();
}

static void OnProgress(void *context, ULONGLONG received, ULONGLONG total) {
    (void)context;
    Status.received = received;
    Status.total = total;
    if (GetTickCount64() - Status.publishedAt >= PUBLISH_EVERY_MS) Publish();
}

static BOOL IsVersion(const char *text) {
    if (!*text || strlen(text) >= sizeof Status.version) return FALSE;
    for (; *text; text++)
        if (!((*text >= '0' && *text <= '9') || (*text >= 'a' && *text <= 'z') || (*text >= 'A' && *text <= 'Z') || *text == '.' || *text == '-')) return FALSE;
    return TRUE;
}

static BOOL IsSha256(const char *text) {
    if (strlen(text) != 64) return FALSE;
    for (; *text; text++)
        if (!((*text >= '0' && *text <= '9') || (*text >= 'a' && *text <= 'f'))) return FALSE;
    return TRUE;
}

static BOOL HashFile(const wchar_t *path, char *out) {
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    BYTE digest[32];
    BOOL done = FALSE;
    FILE *file = _wfopen(path, L"rb");
    if (!file) return FALSE;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) || BCryptCreateHash(algorithm, &hash, NULL, 0, NULL, 0, 0)) goto out;
    static BYTE buffer[1 << 16];
    size_t got;
    while ((got = fread(buffer, 1, sizeof buffer, file)))
        if (BCryptHashData(hash, buffer, (ULONG)got, 0)) goto out;
    if (BCryptFinishHash(hash, digest, sizeof digest, 0)) goto out;
    for (int i = 0; i < 32; i++) sprintf(out + i * 2, "%02x", digest[i]);
    done = TRUE;
out:
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    fclose(file);
    return done;
}

// A running exe can't be replaced, but it can be moved out of the way, and goes the next time round.
static BOOL PutInPlace(const wchar_t *from, const wchar_t *to) {
    if (MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING)) return TRUE;
    wchar_t old[PATH_SIZE];
    swprintf(old, PATH_SIZE, L"%ls.old-%llu", to, GetTickCount64());
    return MoveFileExW(to, old, 0) && MoveFileExW(from, to, 0);
}

static void RemoveOldCopies(void) {
    wchar_t pattern[PATH_SIZE], path[PATH_SIZE];
    HomePathW(pattern, KEPT L".old-*");
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        HomePathW(path, found.cFileName);
        DeleteFileW(path);
    } while (FindNextFileW(search, &found));
    FindClose(search);
}

typedef struct {
    char version[64], sha256[65];
} Wanted;

static DWORD WINAPI FetchUpdate(void *argument) {
    Wanted *wanted = argument;
    char web[256], url[512], error[256] = "", sha256[65];
    if (!GetEnvironmentVariableA("ASIDE_LATE_GITHUB_WEB", web, sizeof web)) strcpy(web, "https://github.com");
    snprintf(url, sizeof url, "%s/%s/releases/download/v%s/AsideLate-%s.exe", web, REPO, wanted->version, wanted->version);
    wchar_t part[PATH_SIZE], kept[PATH_SIZE], versionPath[PATH_SIZE];
    HomePathW(part, KEPT L".part");
    HomePathW(kept, KEPT);
    HomePathW(versionPath, L"Aside Late.version");
    ModLog("update: downloading %s", url);
    if (!HttpFetch(url, part, NULL, OnProgress, NULL, error, sizeof error)) {
        ModLog("update: %s", error);
        SetState("failed", "Couldn't download it. Try again in a bit.");
    } else if (!HashFile(part, sha256) || strcmp(sha256, wanted->sha256)) {
        ModLog("update: the download's SHA-256 is %s, not %s", sha256, wanted->sha256);
        SetState("failed", "The download didn't match what GitHub lists, so it wasn't used.");
    } else if (!PutInPlace(part, kept)) {
        ModLog("update: couldn't replace %ls (error %lu)", kept, GetLastError());
        SetState("failed", "Couldn't put the new version in place.");
    } else {
        WriteWholeFile(versionPath, wanted->version, strlen(wanted->version));
        Staged = TRUE;
        ModLog("update: %s is ready", wanted->version);
        SetState("ready", NULL);
    }
    DeleteFileW(part);
    free(wanted);
    InterlockedExchange(&Working, 0);
    return 0;
}

void UpdateDownload(const char *version, const char *sha256) {
    if (!IsVersion(version) || !IsSha256(sha256) || InterlockedCompareExchange(&Working, 1, 0)) return;
    Wanted *wanted = calloc(1, sizeof *wanted);
    if (!wanted) {
        InterlockedExchange(&Working, 0);
        return;
    }
    strcpy(wanted->version, version);
    strcpy(wanted->sha256, sha256);
    EnterCriticalSection(&Lock);
    strcpy(Status.version, version);
    Status.received = Status.total = 0;
    LeaveCriticalSection(&Lock);
    SetState("downloading", NULL);
    HANDLE thread = CreateThread(NULL, 0, FetchUpdate, wanted, 0, NULL);
    if (thread) {
        CloseHandle(thread);
        return;
    }
    free(wanted);
    InterlockedExchange(&Working, 0);
    SetState("failed", "Couldn't start the download.");
}

// Aside's restart writes these settings, so that the next start brings everything back:
// lea rax,'was.restarted'; ... lea rax,'restart.in.background.on.shutdown'; ... lea rax,'restart.last.session.on.shutdown'
// What the restart menu calls is a stub in front of it that says to wait for unload handlers and to come back in front:
// xor ecx,ecx; xor edx,edx; jmp
static BOOL Mentions(const Lookup *lookup, const RUNTIME_FUNCTION *function, const char *string) {
    DWORD found[8];
    int count = LookupStrings(lookup, string, found, 8);
    for (int i = 0; i < count; i++) {
        size_t refs;
        const Reference *ref = LookupCode(lookup, found[i], &refs);
        for (size_t j = 0; j < refs; j++)
            if (LookupFunction(lookup, ref[j].site) == function) return TRUE;
    }
    return FALSE;
}

static DWORD FindRestart(const Lookup *lookup) {
    DWORD found[8];
    int count = LookupStrings(lookup, "restart.in.background.on.shutdown", found, 8);
    for (int i = 0; i < count; i++) {
        size_t refs;
        const Reference *ref = LookupCode(lookup, found[i], &refs);
        for (size_t j = 0; j < refs; j++) {
            const RUNTIME_FUNCTION *function = LookupFunction(lookup, ref[j].site);
            if (!function || !Mentions(lookup, function, "was.restarted") || !Mentions(lookup, function, "restart.last.session.on.shutdown")) continue;
            const BYTE *text = lookup->base + lookup->text->VirtualAddress;
            DWORD size = lookup->text->Misc.VirtualSize;
            for (DWORD at = 0; at + 9 <= size; at++) {
                const BYTE *p = text + at;
                if (p[4] != 0xe9 || (p[0] != 0x31 && p[0] != 0x33) || p[1] != 0xc9 || p[2] != p[0] || p[3] != 0xd2) continue;
                if (lookup->text->VirtualAddress + at + 9 + *(const int *)(p + 5) == function->BeginAddress) return lookup->text->VirtualAddress + at;
            }
        }
    }
    return 0;
}

static void CallOnUi(void *function) {
    ((AttemptRestart)function)();
}

static DWORD WINAPI Restart(void *unused) {
    (void)unused;
    static DWORD restart;
    wchar_t dll[PATH_SIZE];
    Lookup lookup;
    BYTE *copy = NULL;
    if (!restart && GetModuleFileNameW((HMODULE)Base, dll, PATH_SIZE) && LookupFromFile(dll, &lookup, &copy)) {
        restart = FindRestart(&lookup);
        LookupFromFileFree(&lookup, copy);
    }
    if (!restart) {
        ModLog("update: couldn't find Aside's restart");
        SetState("failed", "Couldn't restart Aside. Quit it and start it again.");
    } else {
        ModLog("update: restarting Aside");
        RunOnUiThread(CallOnUi, Base + restart);
    }
    InterlockedExchange(&Working, 0);
    return 0;
}

void UpdateRestart(void) {
    if (InterlockedCompareExchange(&Working, 1, 0)) return;
    SetState("restarting", NULL);
    HANDLE thread = CreateThread(NULL, 0, Restart, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
    else InterlockedExchange(&Working, 0);
}

// Aside starting itself again is a command line of its own exe with no --type, which is what its other processes have. Started
// directly, it'd come back without the patches, so it's started through Aside Late, once this Aside has gone:
// the newer Aside Late if one's waiting, which unpacks itself first, and the launcher this mod came with otherwise.
static wchar_t *Relaunch(const wchar_t *program, const wchar_t *commandLine) {
    if (!commandLine || wcsstr(commandLine, L"--type=") || wcsstr(commandLine, L"--uninstall")) return NULL;
    const wchar_t *rest;
    wchar_t first[PATH_SIZE];
    if (*commandLine == L'"') {
        const wchar_t *end = wcschr(commandLine + 1, L'"');
        if (!end || end - commandLine - 1 >= PATH_SIZE) return NULL;
        swprintf(first, PATH_SIZE, L"%.*ls", (int)(end - commandLine - 1), commandLine + 1);
        rest = end + 1;
    } else {
        rest = commandLine + wcscspn(commandLine, L" \t");
        if (rest - commandLine >= PATH_SIZE) return NULL;
        swprintf(first, PATH_SIZE, L"%.*ls", (int)(rest - commandLine), commandLine);
    }
    if (_wcsicmp(first, Browser) || (program && _wcsicmp(program, Browser))) return NULL;

    wchar_t through[PATH_SIZE];
    const wchar_t *wait;
    if (Staged) {
        HomePathW(through, KEPT);
        wait = L"--relaunch";
    } else {
        if (!GetModuleFileNameW(ThisModule, through, PATH_SIZE)) return NULL;
        wchar_t *slash = wcsrchr(through, L'\\');
        if (!slash) return NULL;
        wcscpy(slash + 1, L"aside-late.exe");
        wait = L"--after";
    }
    size_t size = wcslen(through) + wcslen(Browser) + wcslen(rest) + 64;
    wchar_t *changed = malloc(size * sizeof *changed);
    if (changed) swprintf(changed, size, L"\"%ls\" %ls %lu --exe \"%ls\"%ls", through, wait, GetCurrentProcessId(), Browser, rest);
    return changed;
}

static BOOL WINAPI OnCreateProcess(const wchar_t *program, wchar_t *commandLine, SECURITY_ATTRIBUTES *process, SECURITY_ATTRIBUTES *thread, BOOL inherit, DWORD flags, void *environment, const wchar_t *folder, STARTUPINFOW *startup, PROCESS_INFORMATION *started) {
    wchar_t *changed = Relaunch(program, commandLine);
    if (!changed) return OriginalCreateProcess(program, commandLine, process, thread, inherit, flags, environment, folder, startup, started);
    ModLog("update: Aside starts itself again, through %ls", Staged ? L"the new Aside Late" : L"the launcher");
    BOOL done = OriginalCreateProcess(NULL, changed, process, thread, inherit, flags, environment, folder, startup, started);
    free(changed);
    return done;
}

// Beside the mod, the window that unpacked it left its version.
static void ReadVersion(void) {
    wchar_t path[PATH_SIZE];
    if (!GetModuleFileNameW(ThisModule, path, PATH_SIZE)) return;
    wchar_t *slash = wcsrchr(path, L'\\');
    if (!slash) return;
    wcscpy(slash + 1, L"version.txt");
    char *text = ReadWholeFile(path, NULL);
    if (text) {
        text[strcspn(text, "\r\n")] = 0;
        if (IsVersion(text)) strcpy(Version, text);
        free(text);
    }
}

void UpdateStart(BYTE *base) {
    Base = base;
    InitializeCriticalSection(&Lock);
    ReadVersion();
    RemoveOldCopies();
    char json[256];
    int size = snprintf(json, sizeof json, "{\"version\":\"%s\",\"repo\":\"%s\"}\n", Version, REPO);
    wchar_t path[PATH_SIZE];
    HomePathW(path, L"web\\late.json");
    WriteWholeFile(path, json, size);
    Publish();
    GetModuleFileNameW(NULL, Browser, PATH_SIZE);
    if (!HookImport(base, "KERNEL32.dll", "CreateProcessW", OnCreateProcess, (void **)&OriginalCreateProcess))
        ModLog("update: couldn't watch Aside starting itself again, so a restart comes back without patches");
}
