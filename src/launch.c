#include "aside-late.h"

static BOOL ApplyEdits(HANDLE process, BYTE *base, const Edit *edits, int count) {
    for (int i = 0; i < count; i++) {
        BYTE current[MAX_EDIT_SIZE];
        if (!ReadProcessMemory(process, base + edits[i].rva, current, edits[i].size, NULL) || memcmp(current, edits[i].before, edits[i].size)) return FALSE;
    }
    for (int i = 0; i < count; i++) {
        BYTE *at = base + edits[i].rva;
        DWORD protect;
        if (!VirtualProtectEx(process, at, edits[i].size, PAGE_EXECUTE_READWRITE, &protect)) return FALSE;
        BOOL wrote = WriteProcessMemory(process, at, edits[i].after, edits[i].size, NULL);
        VirtualProtectEx(process, at, edits[i].size, protect, &protect);
        if (!wrote || !FlushInstructionCache(process, at, edits[i].size)) return FALSE;
    }
    return TRUE;
}

// One patch refusing doesn't stop the others. Returns how many patches landed.
static int PatchLoadedDll(HANDLE process, HANDLE file, BYTE *base, int *wanted) {
    ULONGLONG start = GetTickCount64();
    Image img = {0};
    int applied = 0;
    *wanted = 0;
    if (LoadImageFile(file, &img)) {
        for (int i = 0; i < PatchCount; i++) {
            if (Patches[i].skipped) continue;
            ++*wanted;
            Edit edits[MAX_EDITS];
            int count = FindAndPrintEdits(&Patches[i], &img, edits);
            if (!count) continue;
            if (ApplyEdits(process, base, edits, count)) applied++;
            else Refuse("the code in memory didn't match the DLL");
        }
    } else {
        printf("couldn't read Aside.dll\n");
    }
    FreeImageFile(&img);
    printf("patched %d of %d in %llu ms\n", applied, *wanted, GetTickCount64() - start);
    return applied;
}

static BOOL IsAsideDll(HANDLE file) {
    wchar_t path[MAX_PATH * 2];
    DWORD len = GetFinalPathNameByHandleW(file, path, MAX_PATH * 2, 0);
    return len >= 10 && len < MAX_PATH * 2 && !_wcsicmp(path + len - 10, L"\\Aside.dll");
}

// Every Chromium app uses this window class, and its title is the profile that app owns.
static DWORD FindRunningAside(const wchar_t *userDataDir) {
    HWND window = NULL;
    while ((window = FindWindowExW(HWND_MESSAGE, window, L"Chrome_MessageWindow", NULL))) {
        wchar_t title[MAX_PATH];
        if (!GetWindowTextW(window, title, MAX_PATH) || _wcsicmp(title, userDataDir)) continue;
        DWORD pid = 0;
        GetWindowThreadProcessId(window, &pid);
        return pid;
    }
    return 0;
}

static BOOL CALLBACK CountOpenWindow(HWND window, LPARAM param) {
    DWORD *counts = (DWORD *)param, pid = 0;
    wchar_t name[32];
    GetWindowThreadProcessId(window, &pid);
    if (pid == counts[0] && IsWindowVisible(window) && GetClassNameW(window, name, 32) && !wcscmp(name, L"Chrome_WidgetWin_1")) counts[1]++;
    return TRUE;
}

BOOL ReadyToLaunch(const wchar_t *userDataDir) {
    DWORD pid = FindRunningAside(userDataDir);
    if (!pid) return TRUE;

    DWORD counts[2] = {pid, 0};
    EnumWindows(CountOpenWindow, (LPARAM)counts);
    printf("Aside is already running with %lu window%s open. Terminate it so the patches can apply? [y/N] ", counts[1], counts[1] == 1 ? "" : "s");
    char answer[16];
    if (!fgets(answer, sizeof answer, stdin) || (answer[0] != 'y' && answer[0] != 'Y')) {
        printf("Left Aside running. Nothing was patched.\n");
        return FALSE;
    }

    HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    BOOL stopped = process && TerminateProcess(process, 0) && WaitForSingleObject(process, 10000) == WAIT_OBJECT_0;
    if (process) CloseHandle(process);
    if (!stopped) printf("Couldn't terminate Aside (error %lu). Nothing was patched.\n", GetLastError());
    return stopped;
}

// Starts Aside as a debugger, patches it the moment Aside.dll is mapped, and lets go.
int Launch(const wchar_t *exe, wchar_t *commandLine) {
    STARTUPINFOW si = {.cb = sizeof si};
    PROCESS_INFORMATION pi;
    // A process started under a debugger otherwise keeps the slow debug heap after detaching.
    SetEnvironmentVariableW(L"_NO_DEBUG_HEAP", L"1");
    if (!CreateProcessW(exe, commandLine, NULL, NULL, FALSE, DEBUG_ONLY_THIS_PROCESS, NULL, NULL, &si, &pi)) {
        printf("couldn't start %ls (error %lu)\n", exe, GetLastError());
        return 1;
    }
    DebugSetProcessKillOnExit(FALSE);

    int applied = 0, wanted = 0;
    BOOL done = FALSE;
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
                    applied = PatchLoadedDll(pi.hProcess, event.u.LoadDll.hFile, event.u.LoadDll.lpBaseOfDll, &wanted);
                    done = TRUE;
                }
                CloseHandle(event.u.LoadDll.hFile);
            }
            break;
        case EXCEPTION_DEBUG_EVENT:
            if (event.u.Exception.ExceptionRecord.ExceptionCode != EXCEPTION_BREAKPOINT) status = DBG_EXCEPTION_NOT_HANDLED;
            break;
        case EXIT_PROCESS_DEBUG_EVENT:
            // Aside.exe hands off to a running Aside before it loads the DLL.
            printf("Aside is already running, so nothing was patched. It may have no windows open. Quit it and try again:\n  Stop-Process -Name Aside\n");
            ContinueDebugEvent(event.dwProcessId, event.dwThreadId, status);
            return 1;
        }
        ContinueDebugEvent(event.dwProcessId, event.dwThreadId, status);
    }
    DebugActiveProcessStop(pi.dwProcessId);

    // An Aside already running on this profile takes over and this process quits straight away.
    if (applied && WaitForSingleObject(pi.hProcess, 5000) == WAIT_OBJECT_0) {
        printf("Aside handed off to a copy that's already running, so the patches didn't take. Close Aside and try again.\n");
        return 1;
    }
    return applied == wanted ? 0 : 1;
}
