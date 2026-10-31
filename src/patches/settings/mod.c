// Aside's settings are an extension page, and Aside's browser process reads that page's files.
// Watching those reads adds a script to main.html, and gives the page a folder of its own at
// /aside-late/ that doesn't exist on disk. Nothing in Aside's files changes.

#include "mod/mod.h"

#include <stdio.h>
#include <wchar.h>

#define PATH_SIZE (MAX_PATH * 2)
#define MAX_DISABLED 64
#define MAX_ID 64
#define MAX_RECENT 32

__asm__(".section .rdata,\"dr\"\n.Lscript:\n.incbin \"src/patches/settings/settings.js\"\n.Lscript_end:\n.text");

static const wchar_t Extension[] = L"\\AsideAgentManager\\";
static const wchar_t Folder[] = L"\\aside-late\\";
static const wchar_t Page[] = L"\\main.html";
static const char Tag[] = "<script type=\"module\" src=\"/aside-late/settings.js\"></script>";

typedef struct {
    HANDLE file;
    wchar_t asked[PATH_SIZE], actual[PATH_SIZE];
} Opened;

static CRITICAL_SECTION Lock;
static wchar_t Home[MAX_PATH];
static Opened Recent[MAX_RECENT];
static unsigned NextRecent;

static BOOL IsId(const wchar_t *id) {
    if (!*id) return FALSE;
    for (; *id; id++)
        if (!((*id >= L'a' && *id <= L'z') || (*id >= L'0' && *id <= L'9') || *id == L'-')) return FALSE;
    return TRUE;
}

static void HomePath(wchar_t *out, const wchar_t *name) {
    swprintf(out, PATH_SIZE, L"%ls\\%ls", Home, name);
}

static BOOL WriteWhole(const wchar_t *path, const void *data, size_t size) {
    FILE *file = _wfopen(path, L"wb");
    if (!file) return FALSE;
    BOOL wrote = fwrite(data, 1, size, file) == size;
    return fclose(file) == 0 && wrote;
}

// disabled.txt lists one patch per line. The launcher reads it before it patches anything.
static void SetPatchEnabled(const wchar_t *id, BOOL enabled) {
    char lines[MAX_DISABLED][MAX_ID], wanted[MAX_ID];
    int count = 0;
    snprintf(wanted, sizeof wanted, "%.63ls", id);
    wchar_t path[PATH_SIZE];
    HomePath(path, L"disabled.txt");

    EnterCriticalSection(&Lock);
    FILE *file = _wfopen(path, L"r");
    if (file) {
        while (count < MAX_DISABLED && fgets(lines[count], MAX_ID, file)) {
            lines[count][strcspn(lines[count], "\r\n")] = 0;
            if (lines[count][0] && strcmp(lines[count], wanted)) count++;
        }
        fclose(file);
    }
    if (!enabled && count < MAX_DISABLED) strcpy(lines[count++], wanted);
    if ((file = _wfopen(path, L"w"))) {
        for (int i = 0; i < count; i++) fprintf(file, "%s\n", lines[i]);
        fclose(file);
    } else {
        ModLog("couldn't save disabled.txt");
    }
    LeaveCriticalSection(&Lock);
}

// /aside-late/set/<id>/<0 or 1>/<anything>. Asking twice does no harm, and Aside looks at a file
// before it opens it.
static void RunSet(const wchar_t *request) {
    wchar_t id[MAX_ID];
    const wchar_t *slash = wcschr(request, L'\\');
    if (!slash || slash - request >= MAX_ID || (slash[1] != L'0' && slash[1] != L'1')) return;
    wmemcpy(id, request, slash - request);
    id[slash - request] = 0;
    if (IsId(id)) SetPatchEnabled(id, slash[1] == L'1');
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
        if (!wcsncmp(request, L"set\\", 4)) {
            RunSet(request + 4);
            HomePath(out, L"web\\ok.txt");
        } else if (!wcscmp(request, L"disabled.txt")) {
            HomePath(out, L"disabled.txt");
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

static void Start(const Lookup *lookup, BYTE *base) {
    (void)lookup;
    const char *script, *end;
    __asm__("leaq .Lscript(%%rip), %0" : "=r"(script));
    __asm__("leaq .Lscript_end(%%rip), %0" : "=r"(end));

    if (!GetEnvironmentVariableW(L"ASIDE_LATE_HOME", Home, MAX_PATH)) {
        ModLog("settings refused: ASIDE_LATE_HOME isn't set");
        return;
    }
    InitializeCriticalSection(&Lock);
    wchar_t path[PATH_SIZE];
    HomePath(path, L"web");
    CreateDirectoryW(path, NULL);
    HomePath(path, L"web\\settings.js");
    BOOL wrote = WriteWhole(path, script, end - script);
    HomePath(path, L"web\\ok.txt");
    wrote = WriteWhole(path, "ok", 2) && wrote;
    SaveBrowserVersion(base);
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

MOD("settings", Start)
