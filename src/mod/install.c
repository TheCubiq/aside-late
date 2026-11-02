// Installs a patch from a GitHub repo: its source goes into the home folder and gets compiled there
// against the SDK that matches this DLL. The compiler is fetched the first time one is needed.

#include "internal.h"

#include <winhttp.h>

#define COMPILER_VERSION "2.11.0"
#define COMPILER_URL "https://github.com/skeeto/w64devkit/releases/download/v" COMPILER_VERSION "/w64devkit-x64-" COMPILER_VERSION ".7z.exe"
#define MAX_SOURCES 256

typedef struct {
    char *data;
    size_t size;
} Download;

// A trial can stand in for GitHub. These are also what the settings page uses.
static void ApiBase(char *out, size_t size) {
    if (!GetEnvironmentVariableA("ASIDE_LATE_GITHUB_API", out, (DWORD)size)) snprintf(out, size, "https://api.github.com");
}

static void RawBase(char *out, size_t size) {
    if (!GetEnvironmentVariableA("ASIDE_LATE_GITHUB_RAW", out, (DWORD)size)) snprintf(out, size, "https://raw.githubusercontent.com");
}

// Follows redirects, and hands the body to a file or to memory.
static BOOL HttpGet(const char *url, const wchar_t *toFile, Download *toMemory, char *error, size_t errorSize) {
    wchar_t wide[2048], host[256], path[2048];
    swprintf(wide, 2048, L"%hs", url);
    URL_COMPONENTSW parts = {.dwStructSize = sizeof parts, .lpszHostName = host, .dwHostNameLength = 256, .lpszUrlPath = path, .dwUrlPathLength = 2048};
    if (!WinHttpCrackUrl(wide, 0, 0, &parts)) {
        snprintf(error, errorSize, "Bad address: %s", url);
        return FALSE;
    }
    BOOL done = FALSE;
    FILE *file = NULL;
    HINTERNET session = WinHttpOpen(L"aside-late", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, NULL, NULL, 0);
    HINTERNET connection = session ? WinHttpConnect(session, host, parts.nPort, 0) : NULL;
    HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", path, NULL, NULL, NULL, parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : NULL;
    if (!request || !WinHttpSendRequest(request, L"Accept: application/vnd.github+json\r\n", (DWORD)-1, NULL, 0, 0, 0) || !WinHttpReceiveResponse(request, NULL)) {
        snprintf(error, errorSize, "Couldn't reach %ls (error %lu)", host, GetLastError());
        goto out;
    }
    DWORD status = 0, statusSize = sizeof status;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &status, &statusSize, NULL);
    if (status != 200) {
        snprintf(error, errorSize, "%s answered %lu", url, status);
        goto out;
    }
    if (toFile && !(file = _wfopen(toFile, L"wb"))) {
        snprintf(error, errorSize, "Couldn't write the download");
        goto out;
    }
    if (toMemory) *toMemory = (Download){0};
    for (;;) {
        char buffer[65536];
        DWORD got = 0;
        if (!WinHttpReadData(request, buffer, sizeof buffer, &got)) {
            snprintf(error, errorSize, "The download stopped (error %lu)", GetLastError());
            goto out;
        }
        if (!got) break;
        if (file && fwrite(buffer, 1, got, file) != got) {
            snprintf(error, errorSize, "Couldn't write the download");
            goto out;
        }
        if (toMemory) {
            char *grown = realloc(toMemory->data, toMemory->size + got + 1);
            if (!grown) goto out;
            toMemory->data = grown;
            memcpy(grown + toMemory->size, buffer, got);
            toMemory->size += got;
            grown[toMemory->size] = 0;
        }
    }
    done = TRUE;
out:
    if (file) fclose(file);
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    return done;
}

// Runs something with no window, waits, and leaves what it printed in a file.
static BOOL RunHidden(wchar_t *commandLine, const wchar_t *folder, const wchar_t *logPath, DWORD *exitCode) {
    SECURITY_ATTRIBUTES inherit = {.nLength = sizeof inherit, .bInheritHandle = TRUE};
    HANDLE log = CreateFileW(logPath, GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS, 0, NULL);
    if (log == INVALID_HANDLE_VALUE) return FALSE;
    // CREATE_NO_WINDOW only covers a console, and the compiler's archive unpacks itself with a window of its own.
    STARTUPINFOW si = {.cb = sizeof si, .dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW, .wShowWindow = SW_HIDE, .hStdOutput = log, .hStdError = log};
    PROCESS_INFORMATION pi;
    BOOL started = CreateProcessW(NULL, commandLine, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, folder, &si, &pi);
    CloseHandle(log);
    if (!started) return FALSE;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return TRUE;
}

BOOL HasCompiler(void) {
    wchar_t gcc[PATH_SIZE];
    if (GetEnvironmentVariableW(L"ASIDE_LATE_GCC", gcc, PATH_SIZE)) return TRUE;
    HomePathW(gcc, L"compiler\\w64devkit\\bin\\gcc.exe");
    return GetFileAttributesW(gcc) != INVALID_FILE_ATTRIBUTES;
}

// w64devkit is one archive that unpacks itself into one folder.
static BOOL FindCompiler(const char *id, wchar_t *gcc, char *error, size_t errorSize) {
    if (GetEnvironmentVariableW(L"ASIDE_LATE_GCC", gcc, PATH_SIZE)) return TRUE;
    wchar_t folder[PATH_SIZE];
    HomePathW(folder, L"compiler");
    swprintf(gcc, PATH_SIZE, L"%ls\\w64devkit\\bin\\gcc.exe", folder);
    if (GetFileAttributesW(gcc) != INVALID_FILE_ATTRIBUTES) return TRUE;

    SetPatchProgress(id, "Downloading the compiler");
    CreateDirectoryW(folder, NULL);
    wchar_t archive[PATH_SIZE], logPath[PATH_SIZE], commandLine[PATH_SIZE * 2];
    swprintf(archive, PATH_SIZE, L"%ls\\w64devkit.exe", folder);
    swprintf(logPath, PATH_SIZE, L"%ls\\unpack.log", folder);
    if (!HttpGet(COMPILER_URL, archive, NULL, error, errorSize)) return FALSE;
    SetPatchProgress(id, "Unpacking the compiler");
    swprintf(commandLine, PATH_SIZE * 2, L"\"%ls\" -y \"-o%ls\"", archive, folder);
    DWORD exitCode = 1;
    BOOL ran = RunHidden(commandLine, folder, logPath, &exitCode);
    DeleteFileW(archive);
    if (!ran || exitCode || GetFileAttributesW(gcc) == INVALID_FILE_ATTRIBUTES) {
        snprintf(error, errorSize, "Couldn't unpack the compiler");
        return FALSE;
    }
    return TRUE;
}

// Makes a file's folders.
static void MakeFoldersFor(wchar_t *path) {
    for (wchar_t *c = path + 3; *c; c++) {
        if (*c != L'\\') continue;
        *c = 0;
        CreateDirectoryW(path, NULL);
        *c = L'\\';
    }
}

static const char *JsonValue(const char *at, const char *end, const char *key, char *out, size_t size) {
    char quoted[32];
    snprintf(quoted, sizeof quoted, "\"%s\"", key);
    const char *found = strstr(at, quoted);
    if (!found || (end && found >= end)) return NULL;
    found = strchr(found + strlen(quoted), ':');
    found = found ? strchr(found, '"') : NULL;
    if (!found) return NULL;
    const char *close = strchr(found + 1, '"');
    if (!close || (size_t)(close - found - 1) >= size) return NULL;
    memcpy(out, found + 1, close - found - 1);
    out[close - found - 1] = 0;
    return close;
}

// Everything under patches/<id>/ at that commit goes into its source folder.
static BOOL DownloadSource(const char *repo, const char *id, const char *sha, const wchar_t *folder, char *error, size_t errorSize) {
    char api[256], raw[256], url[2600], owner[128], name[128];
    ApiBase(api, sizeof api);
    RawBase(raw, sizeof raw);
    const char *slash = strchr(repo, '/');
    if (!slash || slash == repo) {
        snprintf(error, errorSize, "%s isn't a repo", repo);
        return FALSE;
    }
    snprintf(owner, sizeof owner, "%.*s", (int)(slash - repo), repo);
    snprintf(name, sizeof name, "%s", slash + 1);
    snprintf(url, sizeof url, "%s/repos/%s/%s/git/trees/%s?recursive=1", api, owner, name, sha);
    Download tree;
    if (!HttpGet(url, NULL, &tree, error, errorSize)) return FALSE;

    char prefix[128];
    int prefixSize = snprintf(prefix, sizeof prefix, "patches/%s/", id);
    int files = 0;
    BOOL ok = TRUE;
    const char *at = tree.data;
    char path[1024];
    while (ok && (at = JsonValue(at, NULL, "path", path, sizeof path))) {
        const char *close = strchr(at, '}');
        char type[16] = "";
        JsonValue(at, close, "type", type, sizeof type);
        if (strcmp(type, "blob") || strncmp(path, prefix, prefixSize)) continue;
        if (strstr(path, "..") || strchr(path, '\\') || strchr(path, ':')) {
            snprintf(error, errorSize, "%s has an unsafe file name", path);
            ok = FALSE;
            break;
        }
        wchar_t target[PATH_SIZE];
        swprintf(target, PATH_SIZE, L"%ls\\%hs", folder, path + prefixSize);
        for (wchar_t *c = target; *c; c++)
            if (*c == L'/') *c = L'\\';
        MakeFoldersFor(target);
        snprintf(url, sizeof url, "%s/%s/%s/%s/%s", raw, owner, name, sha, path);
        ok = HttpGet(url, target, NULL, error, errorSize);
        files++;
    }
    free(tree.data);
    if (ok && !files) {
        snprintf(error, errorSize, "%s has no patches/%s", repo, id);
        ok = FALSE;
    }
    return ok;
}

static void DeleteFolder(const wchar_t *folder) {
    wchar_t pattern[PATH_SIZE], path[PATH_SIZE];
    swprintf(pattern, PATH_SIZE, L"%ls\\*", folder);
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(pattern, &found);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(found.cFileName, L".") || !wcscmp(found.cFileName, L"..")) continue;
            swprintf(path, PATH_SIZE, L"%ls\\%ls", folder, found.cFileName);
            if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) DeleteFolder(path);
            else DeleteFileW(path);
        } while (FindNextFileW(search, &found));
        FindClose(search);
    }
    RemoveDirectoryW(folder);
}

// A build that's still loaded can't be deleted. It goes on a later launch.
void CleanOldBuilds(const char *id, const char *keepSha) {
    wchar_t pattern[PATH_SIZE], path[PATH_SIZE], keep[128];
    swprintf(pattern, PATH_SIZE, L"%ls\\patches\\%hs\\patch-*.dll", Home, id);
    swprintf(keep, 128, L"patch-%hs.dll", keepSha);
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (!_wcsicmp(found.cFileName, keep)) continue;
        swprintf(path, PATH_SIZE, L"%ls\\patches\\%hs\\%ls", Home, id, found.cFileName);
        DeleteFileW(path);
    } while (FindNextFileW(search, &found));
    FindClose(search);
}

static BOOL AppendSources(const wchar_t *folder, wchar_t *commandLine, size_t size) {
    wchar_t pattern[PATH_SIZE], path[PATH_SIZE];
    swprintf(pattern, PATH_SIZE, L"%ls\\*", folder);
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return FALSE;
    BOOL any = FALSE;
    do {
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (wcscmp(found.cFileName, L".") && wcscmp(found.cFileName, L"..")) {
                swprintf(path, PATH_SIZE, L"%ls\\%ls", folder, found.cFileName);
                any |= AppendSources(path, commandLine, size);
            }
            continue;
        }
        size_t length = wcslen(found.cFileName);
        if (length < 3 || _wcsicmp(found.cFileName + length - 2, L".c")) continue;
        swprintf(path, PATH_SIZE, L" \"%ls\\%ls\"", folder, found.cFileName);
        if (wcslen(commandLine) + wcslen(path) + 1 >= size) break;
        wcscat(commandLine, path);
        any = TRUE;
    } while (FindNextFileW(search, &found));
    FindClose(search);
    return any;
}

static BOOL CompileSource(const char *id, const wchar_t *source, const wchar_t *dll, const wchar_t *logPath, char *error, size_t errorSize);

BOOL InstallPatch(const char *repo, const char *id, const char *sha, BOOL rebuild, char *error, size_t errorSize) {
    wchar_t patchFolder[PATH_SIZE], source[PATH_SIZE], dll[PATH_SIZE], logPath[PATH_SIZE];
    wchar_t patches[PATH_SIZE];
    HomePathW(patches, L"patches");
    CreateDirectoryW(patches, NULL);
    swprintf(patchFolder, PATH_SIZE, L"%ls\\%hs", patches, id);
    CreateDirectoryW(patchFolder, NULL);
    // New source stays beside what's kept until it has compiled, so that a failed update can't take away what Rebuild needs.
    swprintf(source, PATH_SIZE, L"%ls\\source%ls", patchFolder, rebuild ? L"" : L".new");
    if (!rebuild) {
        DeleteFolder(source);
        CreateDirectoryW(source, NULL);
        SetPatchProgress(id, "Downloading");
        if (!DownloadSource(repo, id, sha, source, error, errorSize)) return FALSE;
    }

    swprintf(dll, PATH_SIZE, L"%ls\\patch-%hs.dll", patchFolder, sha);
    swprintf(logPath, PATH_SIZE, L"%ls\\compile.log", patchFolder);
    // This commit, built by this mod and still loaded from earlier, is what compiling would give. It also can't be overwritten.
    if (DeleteFileW(dll) || GetLastError() != ERROR_ACCESS_DENIED) {
        if (!CompileSource(id, source, dll, logPath, error, errorSize)) return FALSE;
    }

    wchar_t from[PATH_SIZE], to[PATH_SIZE];
    swprintf(from, PATH_SIZE, L"%ls\\patch.json", source);
    swprintf(to, PATH_SIZE, L"%ls\\patch.json", patchFolder);
    if (!CopyFileW(from, to, FALSE)) {
        snprintf(error, errorSize, "The patch has no patch.json");
        return FALSE;
    }
    char about[256];
    int aboutSize = snprintf(about, sizeof about, "%s %s %d\n", sha, repo, ASIDE_LATE_SDK);
    swprintf(to, PATH_SIZE, L"%ls\\installed.txt", patchFolder);
    WriteWholeFile(to, about, aboutSize);
    CleanOldBuilds(id, sha);
    if (!rebuild) {
        swprintf(to, PATH_SIZE, L"%ls\\source", patchFolder);
        DeleteFolder(to);
        if (!MoveFileW(source, to)) ModLog("couldn't keep %s's source (error %lu)", id, GetLastError());
    }
    return TRUE;
}

static BOOL CompileSource(const char *id, const wchar_t *source, const wchar_t *dll, const wchar_t *logPath, char *error, size_t errorSize) {
    wchar_t gcc[PATH_SIZE], mod[PATH_SIZE], sdk[PATH_SIZE];
    if (!FindCompiler(id, gcc, error, errorSize)) return FALSE;
    SetPatchProgress(id, "Compiling");
    GetModuleFileNameW(ThisModule, mod, PATH_SIZE);
    wcscpy(sdk, mod);
    wchar_t *slash = wcsrchr(sdk, L'\\');
    if (!slash) return FALSE;
    wcscpy(slash + 1, L"sdk");

    size_t size = 32768;
    wchar_t *commandLine = malloc(size * sizeof *commandLine);
    if (!commandLine) return FALSE;
    swprintf(commandLine, size, L"\"%ls\" -O2 -static -shared -D_WIN32_WINNT=0x0A00 \"-I%ls\" \"-I%ls\" -o \"%ls\"", gcc, sdk, source, dll);
    if (!AppendSources(source, commandLine, size)) {
        free(commandLine);
        snprintf(error, errorSize, "The patch has no C files");
        return FALSE;
    }
    wcsncat(commandLine, L" \"", size - wcslen(commandLine) - 1);
    wcsncat(commandLine, mod, size - wcslen(commandLine) - 1);
    wcsncat(commandLine, L"\"", size - wcslen(commandLine) - 1);

    // gcc finds its own parts by looking along PATH from where it runs.
    wchar_t bin[PATH_SIZE];
    wcscpy(bin, gcc);
    slash = wcsrchr(bin, L'\\');
    if (slash) *slash = 0;
    wchar_t oldPath[8192] = L"", newPath[8192 + PATH_SIZE];
    GetEnvironmentVariableW(L"PATH", oldPath, 8192);
    swprintf(newPath, 8192 + PATH_SIZE, L"%ls;%ls", bin, oldPath);
    SetEnvironmentVariableW(L"PATH", newPath);
    DWORD exitCode = 1;
    BOOL ran = RunHidden(commandLine, source, logPath, &exitCode);
    SetEnvironmentVariableW(L"PATH", oldPath);
    free(commandLine);
    if (!ran || exitCode) {
        // The whole of it is in compile.log. The first error is what fits on the page.
        char *log = ReadWholeFile(logPath, NULL);
        const char *first = log ? strstr(log, "error:") : NULL;
        if (first) snprintf(error, errorSize, "It didn't compile: %.*s", (int)strcspn(first, "\r\n"), first);
        else snprintf(error, errorSize, "It didn't compile. The compiler's output is in compile.log");
        free(log);
        return FALSE;
    }
    return TRUE;
}
