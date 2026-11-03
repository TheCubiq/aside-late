// Aside's settings are an extension page, and Aside's browser process reads that page's files.
// Watching those reads adds a script to main.html, and gives the page a folder of its own at
// /aside-late/ that doesn't exist on disk. Nothing in Aside's files changes.

#include "internal.h"

#define MAX_ID ID_SIZE
#define MAX_RECENT 32

__asm__(".section .rdata,\"dr\"\n.Lscript:\n.incbin \"src/mod/settings.js\"\n.Lscript_end:\n.text");
// The thank-you page the launcher opens once, at /aside-late/hello.html, and the hand it sends to GitHub and X.
__asm__(".section .rdata,\"dr\"\n.Lhello_page:\n.incbin \"src/mod/hello.html\"\n.Lhello_page_end:\n.Lhello_script:\n.incbin \"src/mod/hello.js\"\n.Lhello_script_end:\n.Lhand_script:\n.incbin \"src/mod/hand.js\"\n.Lhand_script_end:\n.text");

static const wchar_t Extension[] = L"\\AsideAgentManager\\";
static const wchar_t Folder[] = L"\\aside-late\\";
static const wchar_t Page[] = L"\\main.html";
static const char Tag[] = "<script type=\"module\" src=\"/aside-late/settings.js\"></script>";

typedef struct {
    HANDLE file;
    wchar_t asked[PATH_SIZE], actual[PATH_SIZE];
} Opened;

static CRITICAL_SECTION Lock;
static Opened Recent[MAX_RECENT];
static unsigned NextRecent;

#define HomePath HomePathW
#define WriteWhole WriteWholeFile

// Requests from the page look like /aside-late/<verb>/<...>/<nonce>. Aside looks at a file before it
// opens it, so one request arrives more than once, and only the first of a nonce counts. Others can
// arrive in between, so it takes more than the last one to tell.
static BOOL IsNewRequest(const wchar_t *request) {
    static wchar_t seen[MAX_RECENT][PATH_SIZE];
    static unsigned next;
    EnterCriticalSection(&Lock);
    BOOL fresh = TRUE;
    for (int i = 0; i < MAX_RECENT && fresh; i++) fresh = wcscmp(seen[i], request) != 0;
    if (fresh) wcsncpy(seen[next++ % MAX_RECENT], request, PATH_SIZE - 1);
    LeaveCriticalSection(&Lock);
    return fresh;
}

static BOOL IsName(const char *text) {
    if (!*text) return FALSE;
    for (; *text; text++)
        if (!((*text >= 'a' && *text <= 'z') || (*text >= 'A' && *text <= 'Z') || (*text >= '0' && *text <= '9') || strchr("-_.~", *text))) return FALSE;
    return TRUE;
}

static int SplitRequest(const wchar_t *request, char parts[][128], int max) {
    int count = 0;
    while (count < max) {
        const wchar_t *slash = wcschr(request, L'\\');
        size_t size = slash ? (size_t)(slash - request) : wcslen(request);
        if (size >= 128) return 0;
        for (size_t i = 0; i < size; i++) parts[count][i] = request[i] < 128 ? (char)request[i] : '?';
        parts[count++][size] = 0;
        if (!slash) break;
        request = slash + 1;
    }
    return count;
}

// repos.txt holds one owner/name per line.
static void ChangeRepos(const char *repo, BOOL add) {
    wchar_t path[PATH_SIZE];
    HomePath(path, L"repos.txt");
    char *text = ReadWholeFile(path, NULL);
    FILE *file = _wfopen(path, L"w");
    if (file) {
        if (text)
            for (char *line = strtok(text, "\r\n"); line; line = strtok(NULL, "\r\n"))
                if (strcmp(line, repo)) fprintf(file, "%s\n", line);
        if (add) fprintf(file, "%s\n", repo);
        fclose(file);
    }
    free(text);
}

// A repo travels in a path as owner~name. A nonce always comes last.
static void RunRequest(const wchar_t *request) {
    if (!IsNewRequest(request)) return;
    char parts[5][128] = {{0}};
    int count = SplitRequest(request, parts, 5);
    char *tilde;
    if (count == 4 && !strcmp(parts[0], "set") && IsName(parts[1])) {
        QueueSet(parts[1], parts[2][0] == '1');
    } else if (count == 5 && !strcmp(parts[0], "option") && IsName(parts[1]) && IsName(parts[2]) && IsName(parts[3])) {
        QueueOption(parts[1], parts[2], parts[3]);
    } else if (count == 3 && !strcmp(parts[0], "reset") && IsName(parts[1])) {
        QueueResetOptions(parts[1]);
    } else if (count == 5 && !strcmp(parts[0], "install") && IsName(parts[1]) && IsName(parts[2]) && IsName(parts[3])) {
        if ((tilde = strchr(parts[1], '~'))) *tilde = '/';
        QueueInstall(parts[1], parts[2], parts[3]);
    } else if (count == 3 && !strcmp(parts[0], "rebuild") && IsName(parts[1])) {
        QueueRebuild(parts[1]);
    } else if (count == 3 && !strcmp(parts[0], "remove") && IsName(parts[1])) {
        QueueRemove(parts[1]);
    } else if (count == 4 && !strcmp(parts[0], "repo") && IsName(parts[2])) {
        if ((tilde = strchr(parts[2], '~'))) *tilde = '/';
        ChangeRepos(parts[2], !strcmp(parts[1], "add"));
    }
}

// The addresses the page should use to reach GitHub.
static void SaveGithubAddresses(void) {
    char api[256], raw[256], text[600];
    if (!GetEnvironmentVariableA("ASIDE_LATE_GITHUB_API", api, sizeof api)) strcpy(api, "https://api.github.com");
    if (!GetEnvironmentVariableA("ASIDE_LATE_GITHUB_RAW", raw, sizeof raw)) strcpy(raw, "https://raw.githubusercontent.com");
    int size = snprintf(text, sizeof text, "{\"api\":\"%s\",\"raw\":\"%s\"}\n", api, raw);
    wchar_t path[PATH_SIZE];
    HomePath(path, L"web\\github.json");
    WriteWhole(path, text, size);
}

// A copy of main.html with the script added, kept per version of the extension.
static BOOL PrepareMainPage(const wchar_t *original, wchar_t *out) {
    const wchar_t *end = original + wcslen(original) - wcslen(Page), *start = end;
    while (start > original && start[-1] != L'\\') start--;
    wchar_t name[MAX_PATH];
    swprintf(name, MAX_PATH, L"web\\main-%.*ls.html", (int)(end - start), start);
    HomePath(out, name);

    EnterCriticalSection(&Lock);
    BOOL ready = GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES;
    FILE *file = ready ? NULL : _wfopen(original, L"rb");
    if (file) {
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);
        char *html = size > 0 ? malloc(size + 1) : NULL;
        if (html && fread(html, 1, size, file) == (size_t)size) {
            html[size] = 0;
            const char *head = strstr(html, "</head>");
            FILE *copy = head ? _wfopen(out, L"wb") : NULL;
            if (copy) {
                ready = fwrite(html, 1, head - html, copy) == (size_t)(head - html) &&
                        fwrite(Tag, 1, strlen(Tag), copy) == strlen(Tag) &&
                        fputs(head, copy) >= 0;
                if (fclose(copy)) ready = FALSE;
            }
            if (!ready) ModLog("couldn't prepare main.html");
        }
        free(html);
        fclose(file);
    }
    LeaveCriticalSection(&Lock);
    return ready;
}

// The path Aside should really use, and the clean form of the path it asked for. Runs on every
// file Aside opens, so it stops early.
static const wchar_t *Redirect(const wchar_t *path, wchar_t *out, wchar_t *clean) {
    if (!path || !wcsstr(path, Extension) || wcslen(path) >= PATH_SIZE) return path;
    wcscpy(clean, path);
    for (wchar_t *c = clean; *c; c++)
        if (*c == L'/') *c = L'\\';

    const wchar_t *folder = wcsstr(clean, Folder);
    if (folder) {
        const wchar_t *request = folder + wcslen(Folder);
        if (wcsstr(request, L"..")) return path;
        if (!wcsncmp(request, L"set\\", 4) || !wcsncmp(request, L"install\\", 8) || !wcsncmp(request, L"rebuild\\", 8) || !wcsncmp(request, L"remove\\", 7) || !wcsncmp(request, L"repo\\", 5) || !wcsncmp(request, L"option\\", 7) || !wcsncmp(request, L"reset\\", 6)) {
            RunRequest(request);
            HomePath(out, L"web\\ok.txt");
        } else if (PublishedPath(request, out)) {
            // The newest version.
        } else if (!wcscmp(request, L"repos.txt")) {
            HomePath(out, L"repos.txt");
            if (GetFileAttributesW(out) == INVALID_FILE_ATTRIBUTES) WriteWhole(out, "", 0);
        } else {
            wchar_t name[MAX_PATH];
            swprintf(name, MAX_PATH, L"web\\%ls", request);
            HomePath(out, name);
        }
        return out;
    }
    size_t length = wcslen(clean), page = wcslen(Page);
    if (length > page && !_wcsicmp(clean + length - page, Page) && PrepareMainPage(clean, out)) return out;
    return path;
}

// Aside asks an open file where it really is, and refuses anything outside the extension. So a
// file opened from elsewhere answers with the path it was opened as. Handles get reused, so an
// answer only counts while the handle is still that same file.
static void Remember(HANDLE file, const wchar_t *asked) {
    wchar_t actual[PATH_SIZE];
    if (!GetFinalPathNameByHandleW(file, actual, PATH_SIZE, 0)) return;
    EnterCriticalSection(&Lock);
    Opened *entry = &Recent[NextRecent++ % MAX_RECENT];
    entry->file = file;
    wcscpy(entry->asked, asked);
    wcscpy(entry->actual, actual);
    LeaveCriticalSection(&Lock);
}

// The final path of a file inside the extension. /aside-late/ doesn't exist, so its files borrow
// the path of the extension's manifest.
static BOOL ExtensionFinalPath(const wchar_t *asked, wchar_t *out, DWORD flags) {
    wchar_t probe[PATH_SIZE];
    const wchar_t *folder = wcsstr(asked, Folder);
    if (folder) swprintf(probe, PATH_SIZE, L"%.*ls\\manifest.json", (int)(folder - asked), asked);
    else wcscpy(probe, asked);
    HANDLE file = CreateFileW(probe, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    DWORD length = GetFinalPathNameByHandleW(file, out, PATH_SIZE, flags);
    CloseHandle(file);
    if (!length || length >= PATH_SIZE) return FALSE;
    if (!folder) return TRUE;
    wchar_t *name = wcsrchr(out, L'\\');
    if (!name || (size_t)(name - out) + wcslen(folder) >= PATH_SIZE) return FALSE;
    wcscpy(name, folder);
    return TRUE;
}

static DWORD WINAPI OnGetFinalPath(HANDLE file, wchar_t *out, DWORD size, DWORD flags) {
    wchar_t asked[PATH_SIZE] = L"", actual[PATH_SIZE], answer[PATH_SIZE];
    EnterCriticalSection(&Lock);
    for (int i = 0; i < MAX_RECENT && !asked[0]; i++) {
        if (Recent[i].file != file) continue;
        if (GetFinalPathNameByHandleW(file, actual, PATH_SIZE, 0) && !wcscmp(actual, Recent[i].actual)) wcscpy(asked, Recent[i].asked);
    }
    LeaveCriticalSection(&Lock);
    if (!asked[0] || !ExtensionFinalPath(asked, answer, flags)) return GetFinalPathNameByHandleW(file, out, size, flags);
    DWORD length = (DWORD)wcslen(answer);
    if (length >= size) return length + 1;
    wcscpy(out, answer);
    return length;
}

static HANDLE WINAPI OnCreateFile(const wchar_t *path, DWORD access, DWORD share, SECURITY_ATTRIBUTES *security, DWORD disposition, DWORD flags, HANDLE from) {
    wchar_t buffer[PATH_SIZE], clean[PATH_SIZE];
    const wchar_t *target = Redirect(path, buffer, clean);
    HANDLE file = CreateFileW(target, access, share, security, disposition, flags, from);
    if (target != path && file != INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        Remember(file, clean);
        SetLastError(error);
    }
    return file;
}

static DWORD WINAPI OnGetAttributes(const wchar_t *path) {
    wchar_t buffer[PATH_SIZE], clean[PATH_SIZE];
    return GetFileAttributesW(Redirect(path, buffer, clean));
}

static BOOL WINAPI OnGetAttributesEx(const wchar_t *path, GET_FILEEX_INFO_LEVELS level, void *info) {
    wchar_t buffer[PATH_SIZE], clean[PATH_SIZE];
    return GetFileAttributesExW(Redirect(path, buffer, clean), level, info);
}

// Aside.dll sits in a folder named after its version.
static void SaveBrowserVersion(BYTE *base) {
    wchar_t dll[PATH_SIZE], path[PATH_SIZE];
    if (!GetModuleFileNameW((HMODULE)base, dll, PATH_SIZE)) return;
    wchar_t *slash = wcsrchr(dll, L'\\');
    if (!slash) return;
    *slash = 0;
    slash = wcsrchr(dll, L'\\');
    if (!slash) return;
    char version[64];
    int size = snprintf(version, sizeof version, "%ls", slash + 1);
    HomePath(path, L"web\\version.txt");
    if (size > 0) WriteWhole(path, version, size);
}

void SettingsStart(BYTE *base) {
    const char *script, *end;
    __asm__("leaq .Lscript(%%rip), %0" : "=r"(script));
    __asm__("leaq .Lscript_end(%%rip), %0" : "=r"(end));

    char skipped[1024];
    if (GetEnvironmentVariableA("ASIDE_LATE_SKIP", skipped, sizeof skipped) && strstr(skipped, "settings,")) return;
    InitializeCriticalSection(&Lock);
    wchar_t path[PATH_SIZE];
    HomePath(path, L"web");
    CreateDirectoryW(path, NULL);
    HomePath(path, L"web\\settings.js");
    BOOL wrote = WriteWhole(path, script, end - script);
    HomePath(path, L"web\\ok.txt");
    wrote = WriteWhole(path, "ok", 2) && wrote;
    const char *page, *page_end, *hello, *hello_end;
    __asm__("leaq .Lhello_page(%%rip), %0" : "=r"(page));
    __asm__("leaq .Lhello_page_end(%%rip), %0" : "=r"(page_end));
    __asm__("leaq .Lhello_script(%%rip), %0" : "=r"(hello));
    __asm__("leaq .Lhello_script_end(%%rip), %0" : "=r"(hello_end));
    HomePath(path, L"web\\hello.html");
    WriteWhole(path, page, page_end - page);
    HomePath(path, L"web\\hello.js");
    WriteWhole(path, hello, hello_end - hello);
    const char *hand, *hand_end;
    __asm__("leaq .Lhand_script(%%rip), %0" : "=r"(hand));
    __asm__("leaq .Lhand_script_end(%%rip), %0" : "=r"(hand_end));
    HomePath(path, L"web\\hand.js");
    WriteWhole(path, hand, hand_end - hand);
    SaveBrowserVersion(base);
    SaveGithubAddresses();
    if (!wrote) {
        ModLog("settings refused: couldn't write its files");
        return;
    }

    void *unused;
    if (!HookImport(base, "KERNEL32.dll", "CreateFileW", OnCreateFile, &unused) ||
        !HookImport(base, "KERNEL32.dll", "GetFileAttributesW", OnGetAttributes, &unused) ||
        !HookImport(base, "KERNEL32.dll", "GetFileAttributesExW", OnGetAttributesEx, &unused) ||
        !HookImport(base, "KERNEL32.dll", "GetFinalPathNameByHandleW", OnGetFinalPath, &unused)) {
        ModLog("settings refused: Aside.dll doesn't import the file functions it needs");
        return;
    }
    ModLog("settings ready");
}
