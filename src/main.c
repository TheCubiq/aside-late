// Starts Aside, stops it the moment Aside.dll is mapped, finds the Chats sidebar code
// from names that survive rebuilds, patches that process only, and lets go.
// Nothing on disk changes and no offset is stored.

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHATS_PREF "aside.vertical_tabs.chats_section_expanded"
#define SITE_WINDOW 0x100000
#define MAX_SITES 8

typedef struct {
    BYTE *base;
    DWORD size;
    const IMAGE_SECTION_HEADER *text, *rdata, *pdata;
} Image;

typedef struct {
    DWORD rva, fn;
    int len;
    BYTE before[3], after[3];
    const char *what;
} Patch;

static const IMAGE_SECTION_HEADER *FindSection(const IMAGE_NT_HEADERS64 *nt, const char *name) {
    const IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++)
        if (!strncmp((const char *)s->Name, name, IMAGE_SIZEOF_SHORT_NAME)) return s;
    return NULL;
}

static BOOL ReadAt(HANDLE file, ULONGLONG offset, void *out, DWORD size) {
    LARGE_INTEGER at = {.QuadPart = (LONGLONG)offset};
    DWORD got;
    return SetFilePointerEx(file, at, NULL, FILE_BEGIN) && ReadFile(file, out, size, &got, NULL) && got == size;
}

// Lays the file out the way the loader does, so everything below works in RVAs.
static BOOL LoadImageFile(HANDLE file, Image *img) {
    BYTE head[4096];
    if (!ReadAt(file, 0, head, sizeof head)) return FALSE;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)head;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew > (LONG)(sizeof head - sizeof(IMAGE_NT_HEADERS64))) return FALSE;
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(head + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) return FALSE;

    img->size = nt->OptionalHeader.SizeOfImage;
    img->base = VirtualAlloc(NULL, img->size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!img->base) return FALSE;
    memcpy(img->base, head, sizeof head);
    nt = (const IMAGE_NT_HEADERS64 *)(img->base + dos->e_lfanew);

    const IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++) {
        DWORD raw = min(s->SizeOfRawData, s->Misc.VirtualSize);
        if (raw && !ReadAt(file, s->PointerToRawData, img->base + s->VirtualAddress, raw)) return FALSE;
    }
    img->text = FindSection(nt, ".text");
    img->rdata = FindSection(nt, ".rdata");
    img->pdata = FindSection(nt, ".pdata");
    return img->text && img->rdata && img->pdata;
}

static DWORD FindUniqueString(const Image *img, const char *string) {
    const BYTE *start = img->base + img->rdata->VirtualAddress;
    const BYTE *end = start + img->rdata->Misc.VirtualSize;
    size_t len = strlen(string) + 1;
    DWORD found = 0;
    for (const BYTE *p = start; (p = memchr(p, string[0], end - p)) && p + len <= end; p++) {
        if (memcmp(p, string, len)) continue;
        if (found) return 0;
        found = (DWORD)(p - img->base);
    }
    return found;
}

static const IMAGE_RUNTIME_FUNCTION_ENTRY *FunctionAt(const Image *img, DWORD rva) {
    const IMAGE_RUNTIME_FUNCTION_ENTRY *table = (const void *)(img->base + img->pdata->VirtualAddress);
    size_t low = 0, high = img->pdata->Misc.VirtualSize / sizeof *table;
    while (low < high) {
        size_t mid = (low + high) / 2;
        if (rva < table[mid].BeginAddress) high = mid;
        else if (rva >= table[mid].EndAddress) low = mid + 1;
        else return &table[mid];
    }
    return NULL;
}

// Several small callbacks read this pref too. The sidebar constructor is the big one.
static const IMAGE_RUNTIME_FUNCTION_ENTRY *FindConstructor(const Image *img, DWORD string, DWORD *xref) {
    const IMAGE_RUNTIME_FUNCTION_ENTRY *best = NULL;
    DWORD start = img->text->VirtualAddress, end = start + img->text->Misc.VirtualSize - 7;
    for (DWORD rva = start; rva < end; rva++) {
        const BYTE *p = img->base + rva;
        if (p[1] != 0x8d || (p[0] != 0x48 && p[0] != 0x4c) || (p[2] & 0xc7) != 0x05) continue;
        if (rva + 7 + *(const INT32 *)(p + 3) != string) continue;
        const IMAGE_RUNTIME_FUNCTION_ENTRY *fn = FunctionAt(img, rva);
        if (fn && (!best || fn->EndAddress - fn->BeginAddress > best->EndAddress - best->BeginAddress)) {
            best = fn;
            *xref = rva;
        }
    }
    return best;
}

// After reading the pref, the constructor stores the Chats container and then its divider.
static BOOL FindChatsFields(const Image *img, const IMAGE_RUNTIME_FUNCTION_ENTRY *ctor, DWORD xref, DWORD fields[2]) {
    int found = 0;
    for (DWORD rva = xref; rva + 7 <= ctor->EndAddress && found < 2; rva++) {
        const BYTE *p = img->base + rva;
        if ((p[0] != 0x48 && p[0] != 0x4c) || p[1] != 0x89 || (p[2] & 0xc7) != 0x86) continue;
        DWORD field = *(const DWORD *)(p + 3);
        if (!found || field != fields[0]) fields[found++] = field;
    }
    return found == 2;
}

// mov rcx,[this+field]; test rcx,rcx; je skip; [cmp dl,[rcx+visible]; je skip;]
// mov rax,[rcx]; mov rax,[rax+SetVisible]; [mov edx,<any>;] call
// The register holding the argument moves between builds, so it isn't part of the match.
static BOOL MatchSite(const Image *img, DWORD rva, BOOL divider, Patch *patch) {
    static const BYTE vcall[] = {0x48, 0x8b, 0x01, 0x48, 0x8b, 0x80, 0x90, 0x00, 0x00, 0x00};
    const BYTE *p = img->base + rva + 7;
    if (p[0] != 0x48 || p[1] != 0x85 || p[2] != 0xc9) return FALSE;
    p += 3;
    if (p[0] == 0x74) p += 2;
    else if (p[0] == 0x0f && p[1] == 0x84) p += 6;
    else return FALSE;

    if (p[0] == 0x3a && p[1] == 0x91 && p[6] == 0x74) {
        if (divider || memcmp(p + 8, vcall, sizeof vcall) || p[8 + sizeof vcall] != 0xff) return FALSE;
        *patch = (Patch){.rva = (DWORD)(p + 6 - img->base), .len = 1, .before = {0x74}, .after = {0xeb},
                         .what = "never show on the visibility update"};
        return TRUE;
    }

    if (memcmp(p, vcall, sizeof vcall)) return FALSE;
    p += sizeof vcall;
    int len = p[0] == 0x44 ? 3 : 2;
    const BYTE *mov = p + len - 2;
    if (mov[0] != 0x89 || (mov[1] & 0xc7) != 0xc2 || mov[2] != 0xff) return FALSE;
    *patch = (Patch){.rva = (DWORD)(p - img->base), .len = len, .after = {0x31, 0xd2, 0x90},
                     .what = divider ? "hide the divider on layout" : "hide the container on layout"};
    memcpy(patch->before, p, len);
    return TRUE;
}

static int FindPatches(const Image *img, Patch *patches) {
    DWORD string = FindUniqueString(img, CHATS_PREF), xref = 0, fields[2];
    if (!string) return printf("refused: \"%s\" isn't in the DLL exactly once\n", CHATS_PREF), 0;
    const IMAGE_RUNTIME_FUNCTION_ENTRY *ctor = FindConstructor(img, string, &xref);
    if (!ctor) return printf("refused: nothing references the Chats pref\n"), 0;
    if (!FindChatsFields(img, ctor, xref, fields)) return printf("refused: no Chats fields in the constructor\n"), 0;
    printf("sidebar constructor %#lx, Chats container +%#lx, divider +%#lx\n", ctor->BeginAddress, fields[0], fields[1]);

    int count = 0;
    DWORD end = min(ctor->BeginAddress + SITE_WINDOW, img->text->VirtualAddress + img->text->Misc.VirtualSize - 64);
    for (DWORD rva = ctor->BeginAddress; rva < end; rva++) {
        const BYTE *p = img->base + rva;
        if ((p[0] != 0x48 && p[0] != 0x49) || p[1] != 0x8b || (p[2] & 0xf8) != 0x88 || (p[2] & 7) == 4) continue;
        DWORD field = *(const DWORD *)(p + 3);
        if (field != fields[0] && field != fields[1]) continue;
        Patch patch;
        if (!MatchSite(img, rva, field == fields[1], &patch)) continue;
        if (count == MAX_SITES) return printf("refused: too many Chats visibility sites\n"), 0;
        patch.fn = FunctionAt(img, rva) ? FunctionAt(img, rva)->BeginAddress : 0;
        patches[count++] = patch;
    }

    // One update site and one layout site for the container, and the divider beside it.
    if (count != 3 || patches[1].fn != patches[2].fn || patches[0].len != 1)
        return printf("refused: expected 3 Chats visibility sites, found %d\n", count), 0;
    for (int i = 0; i < count; i++) printf("  %#lx  %s\n", patches[i].rva, patches[i].what);
    return count;
}

static BOOL ApplyPatches(HANDLE process, BYTE *base, const Patch *patches, int count) {
    for (int i = 0; i < count; i++) {
        BYTE *at = base + patches[i].rva, current[3];
        if (!ReadProcessMemory(process, at, current, patches[i].len, NULL) || memcmp(current, patches[i].before, patches[i].len)) return FALSE;
    }
    for (int i = 0; i < count; i++) {
        BYTE *at = base + patches[i].rva;
        DWORD protect;
        if (!VirtualProtectEx(process, at, patches[i].len, PAGE_EXECUTE_READWRITE, &protect)) return FALSE;
        BOOL wrote = WriteProcessMemory(process, at, patches[i].after, patches[i].len, NULL);
        VirtualProtectEx(process, at, patches[i].len, protect, &protect);
        if (!wrote) return FALSE;
    }
    return FlushInstructionCache(process, base, 0);
}

static BOOL PatchLoadedDll(HANDLE process, HANDLE file, BYTE *base) {
    ULONGLONG start = GetTickCount64();
    Image img = {0};
    Patch patches[MAX_SITES];
    BOOL ok = FALSE;
    if (LoadImageFile(file, &img)) {
        int count = FindPatches(&img, patches);
        ok = count && ApplyPatches(process, base, patches, count);
        if (count && !ok) printf("refused: the code in memory didn't match the DLL\n");
    } else {
        printf("refused: couldn't read Aside.dll\n");
    }
    if (img.base) VirtualFree(img.base, 0, MEM_RELEASE);
    printf("%s in %llu ms\n", ok ? "patched" : "running Aside unpatched", GetTickCount64() - start);
    return ok;
}

static BOOL IsAsideDll(HANDLE file) {
    wchar_t path[MAX_PATH * 2];
    DWORD len = GetFinalPathNameByHandleW(file, path, MAX_PATH * 2, 0);
    return len >= 10 && len < MAX_PATH * 2 && !_wcsicmp(path + len - 10, L"\\Aside.dll");
}

static int Launch(const wchar_t *exe, wchar_t *commandLine) {
    STARTUPINFOW si = {.cb = sizeof si};
    PROCESS_INFORMATION pi;
    // A process started under a debugger otherwise keeps the slow debug heap after detaching.
    SetEnvironmentVariableW(L"_NO_DEBUG_HEAP", L"1");
    if (!CreateProcessW(exe, commandLine, NULL, NULL, FALSE, DEBUG_ONLY_THIS_PROCESS, NULL, NULL, &si, &pi)) {
        printf("couldn't start %ls (error %lu)\n", exe, GetLastError());
        return 1;
    }
    DebugSetProcessKillOnExit(FALSE);

    BOOL patched = FALSE, done = FALSE;
    while (!done) {
        DEBUG_EVENT event;
        if (!WaitForDebugEvent(&event, INFINITE)) return 1;
        DWORD status = DBG_CONTINUE;
        switch (event.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
            if (event.u.CreateProcessInfo.hFile) CloseHandle(event.u.CreateProcessInfo.hFile);
            break;
        case LOAD_DLL_DEBUG_EVENT:
            if (event.u.LoadDll.hFile) {
                if (IsAsideDll(event.u.LoadDll.hFile)) {
                    patched = PatchLoadedDll(pi.hProcess, event.u.LoadDll.hFile, event.u.LoadDll.lpBaseOfDll);
                    done = TRUE;
                }
                CloseHandle(event.u.LoadDll.hFile);
            }
            break;
        case EXCEPTION_DEBUG_EVENT:
            if (event.u.Exception.ExceptionRecord.ExceptionCode != EXCEPTION_BREAKPOINT) status = DBG_EXCEPTION_NOT_HANDLED;
            break;
        case EXIT_PROCESS_DEBUG_EVENT:
            printf("Aside exited before loading Aside.dll\n");
            ContinueDebugEvent(event.dwProcessId, event.dwThreadId, status);
            return 1;
        }
        ContinueDebugEvent(event.dwProcessId, event.dwThreadId, status);
    }
    DebugActiveProcessStop(pi.dwProcessId);

    // An Aside already running on this profile takes over and this process quits straight away.
    if (patched && WaitForSingleObject(pi.hProcess, 5000) == WAIT_OBJECT_0) {
        printf("Aside handed off to a copy that's already running, so the patch didn't take. Close Aside and try again.\n");
        return 1;
    }
    return patched ? 0 : 1;
}

static int Scan(const wchar_t *path) {
    printf("%ls\n", path);
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return printf("couldn't open it\n"), 1;
    Image img = {0};
    Patch patches[MAX_SITES];
    int count = LoadImageFile(file, &img) ? FindPatches(&img, patches) : 0;
    CloseHandle(file);
    if (img.base) VirtualFree(img.base, 0, MEM_RELEASE);
    return count ? 0 : 1;
}

int wmain(int argc, wchar_t **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc >= 3 && !wcscmp(argv[1], L"--scan")) {
        int failed = 0;
        for (int i = 2; i < argc; i++) failed |= Scan(argv[i]);
        return failed;
    }

    wchar_t exe[MAX_PATH];
    int first = 1;
    if (argc >= 3 && !wcscmp(argv[1], L"--exe")) {
        wcsncpy(exe, argv[2], MAX_PATH - 1);
        exe[MAX_PATH - 1] = 0;
        first = 3;
    } else {
        ExpandEnvironmentStringsW(L"%ProgramFiles%\\Aside\\Application\\Aside.exe", exe, MAX_PATH);
    }

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
