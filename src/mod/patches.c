// The list of patches and what state each one is in. Everything that takes time happens on one
// worker thread, and the settings page watches patches.json to see how it went.

#include "internal.h"

#define MAX_PATCHES 128
#define MAX_JOBS 64
#define TEXT_SIZE 512
#define SETTLE_MS 30000
#define ENABLE_SETTLE_MS 5000
#define MAX_OPTIONS 32
#define OPTION_SIZE 48

typedef const char *(*StartFunction)(const Lookup *lookup, BYTE *base);
typedef void (*SwitchFunction)(void);
typedef void (*OptionFunction)(const char *key, const char *value);

typedef struct {
    char key[OPTION_SIZE], value[OPTION_SIZE], fallback[OPTION_SIZE];
} Option;

typedef struct {
    char id[ID_SIZE], title[128], description[TEXT_SIZE], source[128], sha[64];
    wchar_t dll[PATH_SIZE];
    BOOL needsRestart, installed;
    BOOL wanted;     // The switch.
    BOOL running;
    BOOL started;    // Found what it needs in this build.
    BOOL crashed;    // Turned off because of a crash.
    char status[TEXT_SIZE], error[TEXT_SIZE];
    HMODULE module;
    const BYTE *moduleEnd;
    StartFunction start;
    SwitchFunction enable, disable;
    OptionFunction option;
    char *optionsJson;   // The manifest's list of options, for the page.
    Option options[MAX_OPTIONS];
    int optionCount;
} ModPatch;

typedef enum { JOB_SET, JOB_INSTALL, JOB_REMOVE, JOB_OPTION, JOB_RESET_OPTIONS } JobKind;

typedef struct {
    JobKind kind;
    BOOL on;
    char id[ID_SIZE], repo[128], sha[64], key[OPTION_SIZE], value[OPTION_SIZE];
} Job;

static ModPatch Patches[MAX_PATCHES];
static int PatchCount;
static BYTE *Base;
static BOOL SafeMode;
static CRITICAL_SECTION Lock;
static Job Jobs[MAX_JOBS];
static int JobCount;
static HANDLE JobSignal, Worker;
static BOOL MarkerSet;

static ModPatch *FindPatch(const char *id) {
    for (int i = 0; i < PatchCount; i++)
        if (!strcmp(Patches[i].id, id)) return &Patches[i];
    return NULL;
}

// Reads a string out of a manifest, unescaping it. An empty result means it isn't there.
static void ManifestField(const char *manifest, const char *key, char *out, size_t size) {
    char quoted[32];
    snprintf(quoted, sizeof quoted, "\"%s\"", key);
    size_t used = 0;
    const char *at = strstr(manifest, quoted);
    at = at ? strchr(at + strlen(quoted), ':') : NULL;
    at = at ? strchr(at, '"') : NULL;
    if (at) {
        for (at++; *at && *at != '"' && used + 1 < size; at++) {
            if (*at == '\\' && at[1]) at++;
            out[used++] = *at;
        }
    }
    out[used] = 0;
}

static BOOL ManifestTrue(const char *manifest, const char *key) {
    char quoted[32];
    snprintf(quoted, sizeof quoted, "\"%s\"", key);
    const char *at = strstr(manifest, quoted);
    if (!at) return FALSE;
    at += strlen(quoted);
    while (*at == ' ' || *at == ':' || *at == '\t') at++;
    return !strncmp(at, "true", 4);
}

static BOOL IsSkipped(const char *id) {
    char skipped[1024];
    DWORD len = GetEnvironmentVariableA("ASIDE_LATE_SKIP", skipped, sizeof skipped);
    if (!len || len >= sizeof skipped) return FALSE;
    for (char *name = strtok(skipped, ","); name; name = strtok(NULL, ","))
        if (!strcmp(name, id)) return TRUE;
    return FALSE;
}

static const char *SkipSpace(const char *at) {
    while (*at == ' ' || *at == '\t' || *at == '\r' || *at == '\n') at++;
    return at;
}

// Past one JSON value, whatever it holds.
static const char *SkipValue(const char *at) {
    int depth = 0;
    BOOL quoted = FALSE;
    for (; *at; at++) {
        if (quoted) {
            if (*at == '\\' && at[1]) at++;
            else if (*at == '"') {
                quoted = FALSE;
                if (!depth) return at + 1;
            }
        } else if (*at == '"') {
            quoted = TRUE;
        } else if (*at == '{' || *at == '[') {
            depth++;
        } else if (*at == '}' || *at == ']') {
            if (!depth) return at;
            if (!--depth) return at + 1;
        } else if (*at == ',' && !depth) {
            return at;
        }
    }
    return at;
}

// Where the value of a key of this object starts, not counting anything nested in it, or NULL.
static const char *FindValue(const char *object, const char *key) {
    size_t size = strlen(key);
    const char *at = SkipSpace(object);
    if (*at != '{') return NULL;
    for (at = SkipSpace(at + 1); *at == '"'; at = SkipSpace(at + 1)) {
        const char *end = SkipValue(at), *value = SkipSpace(end);
        if (*value != ':') return NULL;
        value = SkipSpace(value + 1);
        if ((size_t)(end - at) == size + 2 && !memcmp(at + 1, key, size)) return value;
        at = SkipSpace(SkipValue(value));
        if (*at != ',') return NULL;
    }
    return NULL;
}

// A string without its quotes, or anything else as it's written.
static void ValueText(const char *value, char *out, size_t size) {
    const char *end = SkipValue(value);
    if (*value == '"') value++, end--;
    else while (end > value && strchr(" \t\r\n", end[-1])) end--;
    snprintf(out, size, "%.*s", (int)(end - value), value);
}

static void OptionsPath(const char *id, wchar_t *path) {
    swprintf(path, PATH_SIZE, L"%ls\\options\\%hs.tsv", Home, id);
}

// patch.json says what a patch's options are and their defaults, and a file keeps what's been changed.
static void ReadOptions(ModPatch *patch, const char *manifest) {
    free(patch->optionsJson);
    patch->optionsJson = NULL;
    patch->optionCount = 0;
    const char *list = FindValue(manifest, "options");
    if (!list || *list != '[') return;
    const char *end = SkipValue(list);
    patch->optionsJson = malloc(end - list + 1);
    memcpy(patch->optionsJson, list, end - list);
    patch->optionsJson[end - list] = 0;
    for (const char *at = SkipSpace(list + 1); *at == '{' && patch->optionCount < MAX_OPTIONS;) {
        const char *key = FindValue(at, "key"), *fallback = FindValue(at, "default");
        if (key && fallback) {
            Option *option = &patch->options[patch->optionCount++];
            ValueText(key, option->key, OPTION_SIZE);
            ValueText(fallback, option->fallback, OPTION_SIZE);
            strcpy(option->value, option->fallback);
        }
        at = SkipSpace(SkipValue(at));
        if (*at == ',') at = SkipSpace(at + 1);
    }
    wchar_t path[PATH_SIZE];
    OptionsPath(patch->id, path);
    char *saved = ReadWholeFile(path, NULL);
    if (!saved) return;
    for (char *line = strtok(saved, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        char *value = strchr(line, '\t');
        if (!value) continue;
        *value++ = 0;
        for (int i = 0; i < patch->optionCount; i++)
            if (!strcmp(patch->options[i].key, line)) snprintf(patch->options[i].value, OPTION_SIZE, "%s", value);
    }
    free(saved);
}

static void SaveOptions(const ModPatch *patch) {
    wchar_t path[PATH_SIZE];
    OptionsPath(patch->id, path);
    FILE *file = _wfopen(path, L"w");
    if (!file) {
        ModLog("couldn't save %s's options", patch->id);
        return;
    }
    for (int i = 0; i < patch->optionCount; i++)
        if (strcmp(patch->options[i].value, patch->options[i].fallback)) fprintf(file, "%s\t%s\n", patch->options[i].key, patch->options[i].value);
    fclose(file);
}

// Adds or refreshes the patch in a folder holding patch.json and a DLL.
static ModPatch *AddPatchFolder(const wchar_t *folder, BOOL installed) {
    wchar_t path[PATH_SIZE];
    swprintf(path, PATH_SIZE, L"%ls\\patch.json", folder);
    char *manifest = ReadWholeFile(path, NULL);
    if (!manifest) return NULL;

    char id[ID_SIZE], sha[64] = "", source[128] = "";
    ManifestField(manifest, "id", id, sizeof id);
    if (installed) {
        swprintf(path, PATH_SIZE, L"%ls\\installed.txt", folder);
        char *about = ReadWholeFile(path, NULL);
        if (about) {
            sscanf(about, "%63s %127s", sha, source);
            free(about);
        }
        swprintf(path, PATH_SIZE, L"%ls\\patch-%hs.dll", folder, sha);
    } else {
        swprintf(path, PATH_SIZE, L"%ls\\patch.dll", folder);
    }
    ModPatch *patch = NULL;
    if (id[0] && GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
        patch = FindPatch(id);
        if (patch && !installed) patch = NULL;  // Something installed takes the place of a built-in one.
        else if (!patch && PatchCount < MAX_PATCHES) {
            patch = &Patches[PatchCount++];
            memset(patch, 0, sizeof *patch);
            strcpy(patch->id, id);
            patch->wanted = TRUE;
        }
    }
    if (patch) {
        ManifestField(manifest, "title", patch->title, sizeof patch->title);
        ManifestField(manifest, "description", patch->description, sizeof patch->description);
        if (!patch->title[0]) strcpy(patch->title, patch->id);
        patch->needsRestart = ManifestTrue(manifest, "restart");
        patch->installed = installed;
        strcpy(patch->sha, sha);
        strcpy(patch->source, installed ? source : "built-in");
        wcscpy(patch->dll, path);
        ReadOptions(patch, manifest);
        // Nothing has loaded yet on the first pass, so anything an update left behind can go.
        if (installed && !patch->module) CleanOldBuilds(id, sha);
    }
    free(manifest);
    return patch;
}

static void AddPatchesIn(const wchar_t *folder, BOOL installed) {
    wchar_t pattern[PATH_SIZE], path[PATH_SIZE];
    swprintf(pattern, PATH_SIZE, L"%ls\\*", folder);
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || found.cFileName[0] == L'.') continue;
        swprintf(path, PATH_SIZE, L"%ls\\%ls", folder, found.cFileName);
        AddPatchFolder(path, installed);
    } while (FindNextFileW(search, &found));
    FindClose(search);
}

static void ReadDisabled(void) {
    wchar_t path[PATH_SIZE];
    HomePathW(path, L"disabled.txt");
    char *text = ReadWholeFile(path, NULL);
    if (!text) return;
    for (char *line = strtok(text, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        ModPatch *patch = FindPatch(line);
        if (patch) patch->wanted = FALSE;
    }
    free(text);
}

static void SaveDisabled(void) {
    wchar_t path[PATH_SIZE];
    HomePathW(path, L"disabled.txt");
    FILE *file = _wfopen(path, L"w");
    if (!file) {
        ModLog("couldn't save disabled.txt");
        return;
    }
    for (int i = 0; i < PatchCount; i++)
        if (!Patches[i].wanted) fprintf(file, "%s\n", Patches[i].id);
    fclose(file);
}

static void WriteJsonText(FILE *file, const char *text) {
    fputc('"', file);
    for (; *text; text++) {
        if (*text == '"' || *text == '\\') fprintf(file, "\\%c", *text);
        else if ((BYTE)*text < 0x20) fprintf(file, "\\u%04x", *text);
        else fputc(*text, file);
    }
    fputc('"', file);
}

// For the settings page.
void SavePatchList(void) {
    wchar_t path[PATH_SIZE];
    PublishPath(path);
    EnterCriticalSection(&Lock);
    FILE *file = _wfopen(path, L"w");
    if (file) {
        fprintf(file, "{\"safeMode\":%s,\"patches\":[", SafeMode ? "true" : "false");
        for (int i = 0; i < PatchCount; i++) {
            const ModPatch *patch = &Patches[i];
            fprintf(file, "%s{\"id\":", i ? "," : "");
            WriteJsonText(file, patch->id);
            fputs(",\"title\":", file);
            WriteJsonText(file, patch->title);
            fputs(",\"description\":", file);
            WriteJsonText(file, patch->description);
            fputs(",\"source\":", file);
            WriteJsonText(file, patch->source);
            fputs(",\"sha\":", file);
            WriteJsonText(file, patch->sha);
            fputs(",\"status\":", file);
            WriteJsonText(file, patch->status);
            fputs(",\"error\":", file);
            WriteJsonText(file, patch->error);
            // As text, so a mistake in one patch's manifest can't break the whole list.
            fputs(",\"options\":", file);
            WriteJsonText(file, patch->optionsJson ? patch->optionsJson : "[]");
            fputs(",\"values\":{", file);
            for (int j = 0; j < patch->optionCount; j++) {
                fputs(j ? "," : "", file);
                WriteJsonText(file, patch->options[j].key);
                fputc(':', file);
                WriteJsonText(file, patch->options[j].value);
            }
            fputc('}', file);
            fprintf(file, ",\"wanted\":%s,\"running\":%s,\"needsRestart\":%s,\"installed\":%s,\"crashed\":%s}", patch->wanted ? "true" : "false",
                    patch->running ? "true" : "false", patch->needsRestart ? "true" : "false", patch->installed ? "true" : "false",
                    patch->crashed ? "true" : "false");
        }
        fputs("]}\n", file);
        fclose(file);
        PublishCommit(L"patches.json", path);
    } else {
        ModLog("couldn't save the patch list");
    }
    LeaveCriticalSection(&Lock);
}

// A patch being installed for the first time has no row yet, so the page follows this instead.
static void SaveInstallState(const char *id, const char *state, const char *text) {
    char line[TEXT_SIZE + ID_SIZE + 16];
    int size = snprintf(line, sizeof line, "%s\t%s\t%s", id, state, text);
    PublishFile(L"install.txt", line, min(size, (int)sizeof line - 1));
}

void SetPatchProgress(const char *id, const char *text) {
    SaveInstallState(id, "running", text);
    ModPatch *patch = FindPatch(id);
    if (!patch) return;
    snprintf(patch->status, sizeof patch->status, "%s", text);
    SavePatchList();
}

static BOOL LoadPatchModule(ModPatch *patch) {
    if (patch->module) return TRUE;
    patch->module = LoadLibraryW(patch->dll);
    if (!patch->module) {
        snprintf(patch->error, sizeof patch->error, "Couldn't load it (error %lu)", GetLastError());
        return FALSE;
    }
    const BYTE *module = (const BYTE *)patch->module;
    patch->moduleEnd = module + ((const IMAGE_NT_HEADERS64 *)(module + ((const IMAGE_DOS_HEADER *)module)->e_lfanew))->OptionalHeader.SizeOfImage;
    patch->start = (StartFunction)(void *)GetProcAddress(patch->module, "PatchStart");
    patch->enable = (SwitchFunction)(void *)GetProcAddress(patch->module, "PatchEnable");
    patch->disable = (SwitchFunction)(void *)GetProcAddress(patch->module, "PatchDisable");
    patch->option = (OptionFunction)(void *)GetProcAddress(patch->module, "PatchOption");
    if (!patch->enable) {
        snprintf(patch->error, sizeof patch->error, "It doesn't export PatchEnable");
        return FALSE;
    }
    return TRUE;
}

// Finds what the patch needs. The lookup is built when the first patch wants one.
static BOOL StartPatch(ModPatch *patch, Lookup *lookup, BYTE **copy) {
    if (patch->started) return TRUE;
    if (!LoadPatchModule(patch)) return FALSE;
    if (patch->start) {
        if (!*copy) {
            wchar_t dll[PATH_SIZE];
            GetModuleFileNameW((HMODULE)Base, dll, PATH_SIZE);
            if (!LookupFromFile(dll, lookup, copy)) {
                snprintf(patch->error, sizeof patch->error, "Couldn't read Aside.dll");
                return FALSE;
            }
        }
        const char *missing = patch->start(lookup, Base);
        if (missing) {
            snprintf(patch->error, sizeof patch->error, "This build of Aside doesn't have %s", missing);
            ModLog("%s refused: didn't find %s", patch->id, missing);
            return FALSE;
        }
    }
    patch->started = TRUE;
    return TRUE;
}

static void WriteMarker(const char *text) {
    wchar_t path[PATH_SIZE];
    HomePathW(path, L"session.txt");
    WriteWholeFile(path, text, strlen(text));
}

static void ClearMarker(void) {
    wchar_t path[PATH_SIZE];
    HomePathW(path, L"session.txt");
    DeleteFileW(path);
}

// A patch has every option's value before it's enabled.
static void GiveOptions(const ModPatch *patch) {
    if (!patch->option) return;
    for (int i = 0; i < patch->optionCount; i++) patch->option(patch->options[i].key, patch->options[i].value);
}

static void DoEnable(void *argument) {
    ModPatch *patch = argument;
    GiveOptions(patch);
    patch->enable();
    patch->running = TRUE;
}

static void DoDisable(void *argument) {
    ModPatch *patch = argument;
    UndoHooksOf(patch->module);
    if (patch->disable) patch->disable();
    patch->running = FALSE;
}

// Runs on the worker. A crash soon after this blames the patch being turned on.
static void TurnOn(ModPatch *patch) {
    Lookup lookup;
    BYTE *copy = NULL;
    patch->error[0] = 0;
    snprintf(patch->status, sizeof patch->status, "Starting");
    SavePatchList();
    BOOL ready = StartPatch(patch, &lookup, &copy);
    if (copy) LookupFromFileFree(&lookup, copy);
    patch->status[0] = 0;
    if (!ready) {
        SavePatchList();
        return;
    }
    char marker[ID_SIZE + 16];
    snprintf(marker, sizeof marker, "enabling %s\n", patch->id);
    WriteMarker(marker);
    RunOnUiThread(DoEnable, patch);
    SavePatchList();
    ModLog("%s on", patch->id);
    MarkerSet = TRUE;
}

static void TurnOff(ModPatch *patch) {
    if (patch->running) RunOnUiThread(DoDisable, patch);
    patch->error[0] = 0;
    patch->status[0] = 0;
    SavePatchList();
    ModLog("%s off", patch->id);
}

static void RunSet(const Job *job) {
    ModPatch *patch = FindPatch(job->id);
    if (!patch) return;
    patch->wanted = job->on;
    patch->crashed = FALSE;
    SaveDisabled();
    if (patch->needsRestart) patch->status[0] = 0;
    else if (job->on && !patch->running) return TurnOn(patch);
    else if (!job->on) return TurnOff(patch);
    patch->status[0] = 0;
    SavePatchList();
}

static void DoGiveOptions(void *argument) {
    GiveOptions(argument);
}

// A patch that's off, or that only works from startup, gets the new values when it's next enabled.
static void RunOption(const Job *job) {
    ModPatch *patch = FindPatch(job->id);
    if (!patch) return;
    for (int i = 0; i < patch->optionCount; i++) {
        Option *option = &patch->options[i];
        if (job->kind == JOB_RESET_OPTIONS) strcpy(option->value, option->fallback);
        else if (!strcmp(option->key, job->key)) snprintf(option->value, OPTION_SIZE, "%s", job->value);
    }
    SaveOptions(patch);
    if (patch->running && !patch->needsRestart && patch->option) RunOnUiThread(DoGiveOptions, patch);
    SavePatchList();
}

static void RunInstall(const Job *job) {
    char error[TEXT_SIZE] = "";
    ModPatch *old = FindPatch(job->id);
    if (old && old->running && !old->needsRestart) TurnOff(old);
    if (old) {
        // The old version stays loaded but inert until the next launch.
        old->started = FALSE;
        old->module = NULL;
        old->start = NULL;
        old->enable = old->disable = NULL;
        old->option = NULL;
        old->error[0] = 0;
    }
    if (!InstallPatch(job->repo, job->id, job->sha, error, sizeof error) || (!AddInstalledPatch(job->id) && snprintf(error, sizeof error, "Its folder doesn't look right"))) {
        // An update that failed leaves the old version as it was, off until it's switched on again.
        ModPatch *patch = FindPatch(job->id);
        if (patch) {
            patch->status[0] = 0;
            snprintf(patch->error, sizeof patch->error, "Couldn't install it: %s", error);
        }
        SaveInstallState(job->id, "failed", error);
        SavePatchList();
        ModLog("couldn't install %s from %s: %s", job->id, job->repo, error);
        return;
    }
    ModPatch *patch = FindPatch(job->id);
    patch->status[0] = 0;
    if (patch->wanted && !patch->needsRestart) TurnOn(patch);
    else SavePatchList();
    SaveInstallState(job->id, "done", "");
}

static void RunRemove(const Job *job) {
    ModPatch *patch = FindPatch(job->id);
    if (!patch || !patch->installed) return;
    patch->status[0] = 0;
    TurnOff(patch);
    wchar_t path[PATH_SIZE];
    swprintf(path, PATH_SIZE, L"%ls\\patches\\%hs\\installed.txt", Home, job->id);
    DeleteFileW(path);
    CleanOldBuilds(job->id, "");
    OptionsPath(job->id, path);
    DeleteFileW(path);
    EnterCriticalSection(&Lock);
    free(patch->optionsJson);
    *patch = Patches[--PatchCount];
    LeaveCriticalSection(&Lock);
    SaveDisabled();
    SavePatchList();
}

BOOL AddInstalledPatch(const char *id) {
    wchar_t folder[PATH_SIZE];
    swprintf(folder, PATH_SIZE, L"%ls\\patches\\%hs", Home, id);
    EnterCriticalSection(&Lock);
    ModPatch *patch = AddPatchFolder(folder, TRUE);
    LeaveCriticalSection(&Lock);
    return patch != NULL;
}

static DWORD WINAPI WorkerMain(void *unused) {
    (void)unused;
    for (;;) {
        // A patch that's been on for a few seconds with nothing wrong is past blaming for a crash.
        if (WaitForSingleObject(JobSignal, MarkerSet ? ENABLE_SETTLE_MS : INFINITE) == WAIT_TIMEOUT) {
            ClearMarker();
            MarkerSet = FALSE;
        }
        for (;;) {
            EnterCriticalSection(&Lock);
            Job job;
            BOOL any = JobCount > 0;
            if (any) {
                job = Jobs[0];
                memmove(&Jobs[0], &Jobs[1], (--JobCount) * sizeof *Jobs);
            }
            LeaveCriticalSection(&Lock);
            if (!any) break;
            if (job.kind == JOB_SET) RunSet(&job);
            else if (job.kind == JOB_INSTALL) RunInstall(&job);
            else if (job.kind == JOB_REMOVE) RunRemove(&job);
            else RunOption(&job);
        }
    }
    return 0;
}

static void PushJob(const Job *job) {
    EnterCriticalSection(&Lock);
    if (JobCount < MAX_JOBS) Jobs[JobCount++] = *job;
    else ModLog("too many things to do at once");
    if (!Worker) {
        JobSignal = CreateEventW(NULL, FALSE, FALSE, NULL);
        Worker = CreateThread(NULL, 0, WorkerMain, NULL, 0, NULL);
    }
    SetEvent(JobSignal);
    LeaveCriticalSection(&Lock);
}

void QueueSet(const char *id, BOOL enabled) {
    ModPatch *patch = FindPatch(id);
    if (!patch) return;
    // The page sees the switch move straight away, and the result when it's done.
    snprintf(patch->status, sizeof patch->status, "Applying");
    Job job = {.kind = JOB_SET, .on = enabled};
    strcpy(job.id, id);
    PushJob(&job);
    SavePatchList();
}

void QueueOption(const char *id, const char *key, const char *value) {
    Job job = {.kind = JOB_OPTION};
    snprintf(job.id, sizeof job.id, "%s", id);
    snprintf(job.key, sizeof job.key, "%s", key);
    snprintf(job.value, sizeof job.value, "%s", value);
    PushJob(&job);
}

void QueueResetOptions(const char *id) {
    Job job = {.kind = JOB_RESET_OPTIONS};
    snprintf(job.id, sizeof job.id, "%s", id);
    PushJob(&job);
}

void QueueInstall(const char *repo, const char *id, const char *sha) {
    // What's there might be from the last time this patch was installed.
    SaveInstallState(id, "running", "Starting");
    Job job = {.kind = JOB_INSTALL};
    snprintf(job.id, sizeof job.id, "%s", id);
    snprintf(job.repo, sizeof job.repo, "%s", repo);
    snprintf(job.sha, sizeof job.sha, "%s", sha);
    PushJob(&job);
}

void QueueRemove(const char *id) {
    Job job = {.kind = JOB_REMOVE};
    snprintf(job.id, sizeof job.id, "%s", id);
    PushJob(&job);
}

// A crash names the first patch on the stack, for the next launch to deal with. Nothing here can
// wait for a lock, because the thread that crashed could be holding it.
static LONG CALLBACK OnException(EXCEPTION_POINTERS *info) {
    DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_PRIV_INSTRUCTION && code != EXCEPTION_INT_DIVIDE_BY_ZERO)
        return EXCEPTION_CONTINUE_SEARCH;
    void *frames[48];
    USHORT count = RtlCaptureStackBackTrace(0, 48, frames, NULL);
    for (USHORT i = 0; i <= count; i++) {
        const BYTE *at = i == count ? (const BYTE *)info->ContextRecord->Rip : frames[i];
        for (int j = 0; j < PatchCount; j++) {
            const BYTE *module = (const BYTE *)Patches[j].module;
            if (!module || at < module || at >= Patches[j].moduleEnd) continue;
            wchar_t path[PATH_SIZE];
            swprintf(path, PATH_SIZE, L"%ls\\blame.txt", Home);
            HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
            if (file != INVALID_HANDLE_VALUE) {
                DWORD wrote;
                WriteFile(file, Patches[j].id, (DWORD)strlen(Patches[j].id), &wrote, NULL);
                CloseHandle(file);
            }
            return EXCEPTION_CONTINUE_SEARCH;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static char *ReadHomeText(const wchar_t *name) {
    wchar_t path[PATH_SIZE];
    HomePathW(path, name);
    return ReadWholeFile(path, NULL);
}

// What the last run left behind says what to leave out of this one.
static void RecoverFromCrash(void) {
    char *blame = ReadHomeText(L"blame.txt"), *marker = ReadHomeText(L"session.txt");
    char id[ID_SIZE] = "";
    if (blame) snprintf(id, sizeof id, "%s", blame);
    else if (marker && !strncmp(marker, "enabling ", 9)) sscanf(marker + 9, "%63s", id);
    if (id[0]) {
        ModPatch *patch = FindPatch(id);
        if (patch) {
            patch->wanted = FALSE;
            patch->crashed = TRUE;
            snprintf(patch->error, sizeof patch->error, "Turned off because Aside crashed while it was running");
            SaveDisabled();
            ModLog("%s was turned off after a crash", id);
        }
    } else if (marker) {
        // Something died while starting, and nothing says what. One launch with nothing on.
        SafeMode = TRUE;
        ModLog("the last launch crashed while starting, so no patches run this time");
    }
    free(blame);
    free(marker);
    wchar_t path[PATH_SIZE];
    HomePathW(path, L"blame.txt");
    DeleteFileW(path);
}

// The launch counts as having worked once it's been up for a while.
static DWORD WINAPI WatchStart(void *unused) {
    (void)unused;
    Sleep(SETTLE_MS);
    ClearMarker();
    return 0;
}

void PatchesClean(void) {
    wchar_t path[PATH_SIZE];
    HomePathW(path, L"blame.txt");
    DeleteFileW(path);
    ClearMarker();
}

// The compiled patches next to this DLL, then anything installed.
static void FindPatches(void) {
    wchar_t folder[PATH_SIZE];
    GetModuleFileNameW(ThisModule, folder, PATH_SIZE);
    wchar_t *slash = wcsrchr(folder, L'\\');
    if (slash) wcscpy(slash + 1, L"patches");
    AddPatchesIn(folder, FALSE);
    HomePathW(folder, L"patches");
    AddPatchesIn(folder, TRUE);
}

void PatchesStart(BYTE *base) {
    Base = base;
    InitializeCriticalSection(&Lock);
    ULONGLONG started = GetTickCount64();
    wchar_t folder[PATH_SIZE];
    HomePathW(folder, L"options");
    CreateDirectoryW(folder, NULL);
    FindPatches();
    ReadDisabled();
    RecoverFromCrash();
    AddVectoredExceptionHandler(1, OnException);
    WriteMarker("starting\n");

    Lookup lookup;
    BYTE *copy = NULL;
    for (int i = 0; i < PatchCount; i++) {
        ModPatch *patch = &Patches[i];
        if (!patch->wanted || SafeMode || IsSkipped(patch->id)) continue;
        if (StartPatch(patch, &lookup, &copy)) {
            GiveOptions(patch);
            patch->enable();
            patch->running = TRUE;
            ModLog("%s on", patch->id);
        }
    }
    if (copy) LookupFromFileFree(&lookup, copy);
    SavePatchList();
    CloseHandle(CreateThread(NULL, 0, WatchStart, NULL, 0, NULL));
    ModLog("ready in %llu ms", GetTickCount64() - started);
}

// For the launcher, which loads this DLL without Aside.
__declspec(dllexport) int ModListPatches(const wchar_t *home) {
    wcsncpy(Home, home, MAX_PATH - 1);
    FindPatches();
    for (int i = 0; i < PatchCount; i++) printf("%-28s %s\n", Patches[i].id, Patches[i].description);
    return 0;
}

__declspec(dllexport) int ModScan(const wchar_t *home, const wchar_t *dll) {
    wcsncpy(Home, home, MAX_PATH - 1);
    if (!PatchCount) FindPatches();
    printf("%ls\n", dll);
    Lookup lookup;
    BYTE *copy;
    if (!LookupFromFile(dll, &lookup, &copy)) return printf("couldn't read it\n"), 1;
    int failed = 0;
    for (int i = 0; i < PatchCount; i++) {
        ModPatch *patch = &Patches[i];
        // Finding things is all a scan does, so it doesn't need Aside running.
        const char *missing = LoadPatchModule(patch) ? (patch->start ? patch->start(&lookup, copy) : NULL) : patch->error;
        printf("%-28s %s%s\n", patch->id, missing ? "refused: " : "found what it needs", missing ? missing : "");
        failed |= missing != NULL;
    }
    LookupFromFileFree(&lookup, copy);
    return failed;
}
