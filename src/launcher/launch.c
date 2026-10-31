#include "launcher.h"

// LoadLibrary in a new thread. It can't take the loader lock until Aside.dll has finished
// loading, and Aside can't load anything further until the mod has finished starting.
static BOOL LoadMod(HANDLE process) {
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, path, MAX_PATH);
    wchar_t *slash = wcsrchr(path, L'\\');
    if (!len || len >= MAX_PATH || !slash) return FALSE;
    wcscpy(slash + 1, L"aside-late-mod.dll");
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) return FALSE;

    SIZE_T size = (wcslen(path) + 1) * sizeof *path;
    void *remote = VirtualAllocEx(process, NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote || !WriteProcessMemory(process, remote, path, size, NULL)) return FALSE;
    FARPROC load = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HANDLE thread = CreateRemoteThread(process, NULL, 0, (LPTHREAD_START_ROUTINE)(void *)load, remote, 0, NULL);
    if (!thread) return FALSE;
    CloseHandle(thread);
    return TRUE;
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

BOOL ReadyToLaunch(const wchar_t *userDataDir, const wchar_t *home) {
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
    if (!stopped) {
        printf("Couldn't terminate Aside (error %lu). Nothing was patched.\n", GetLastError());
        return FALSE;
    }
    // That wasn't a crash, however soon after a launch it came.
    wchar_t marker[MAX_PATH];
    swprintf(marker, MAX_PATH, L"%ls\\session.txt", home);
    DeleteFileW(marker);
    return TRUE;
}

// Starts Aside as a debugger, loads the mod the moment Aside.dll is mapped, and lets go.
int Launch(const wchar_t *exe, wchar_t *commandLine, const wchar_t *skipped) {
    STARTUPINFOW si = {.cb = sizeof si};
    PROCESS_INFORMATION pi;
    // A process started under a debugger otherwise keeps the slow debug heap after detaching.
    SetEnvironmentVariableW(L"_NO_DEBUG_HEAP", L"1");
    SetEnvironmentVariableW(L"ASIDE_LATE_SKIP", skipped);
    if (!CreateProcessW(exe, commandLine, NULL, NULL, FALSE, DEBUG_ONLY_THIS_PROCESS, NULL, NULL, &si, &pi)) {
        printf("couldn't start %ls (error %lu)\n", exe, GetLastError());
        return 1;
    }
    DebugSetProcessKillOnExit(FALSE);

    BOOL loaded = FALSE, done = FALSE;
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
                    loaded = LoadMod(pi.hProcess);
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

    if (!loaded) {
        printf("couldn't load aside-late-mod.dll\n");
        return 1;
    }
    // An Aside already running on this profile takes over and this process quits straight away.
    if (WaitForSingleObject(pi.hProcess, 5000) == WAIT_OBJECT_0) {
        printf("Aside handed off to a copy that's already running, so the patches didn't take. Close Aside and try again.\n");
        return 1;
    }
    printf("Aside is running with aside-late. Open Settings, then Patched stuff.\n");
    return 0;
}
