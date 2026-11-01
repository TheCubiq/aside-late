// One place that changes Aside at runtime, so patches can come and go in any order.

#include "internal.h"

#include <tlhelp32.h>

#define MAX_HOOKS 4096
#define MAX_EDITS 256
#define MAX_EDIT_SIZE 64
#define SCAN_CHUNK (1 << 20)

typedef struct {
    void **slot;
    void *replacement, *next;
    void **original;
    HMODULE owner;
} Hook;

typedef struct {
    BYTE *at;
    size_t size;
    BYTE before[MAX_EDIT_SIZE];
    HMODULE owner;
} CodeEdit;

static CRITICAL_SECTION Lock;
static Hook Hooks[MAX_HOOKS];
static int HookCount;
static CodeEdit Edits[MAX_EDITS];
static int EditCount;

void HooksInit(void) {
    InitializeCriticalSection(&Lock);
}

static HMODULE ModuleAt(const void *address) {
    HMODULE module = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, address, &module);
    return module;
}

static BOOL WriteSlot(void **slot, void *value) {
    DWORD protect;
    if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &protect)) return FALSE;
    *slot = value;
    VirtualProtect(slot, sizeof *slot, protect, &protect);
    return TRUE;
}

static BOOL AddHook(void **slot, void *replacement, void **original, HMODULE owner) {
    EnterCriticalSection(&Lock);
    BOOL done = FALSE;
    for (int i = 0; i < HookCount; i++) {
        if (Hooks[i].slot == slot && Hooks[i].replacement == replacement) {
            done = TRUE;
            goto out;
        }
    }
    if (HookCount == MAX_HOOKS) goto out;
    // The original has to be right before anything can call the replacement.
    void *next = *slot;
    *original = next;
    if (!WriteSlot(slot, replacement)) goto out;
    Hooks[HookCount++] = (Hook){slot, replacement, next, original, owner};
    done = TRUE;
out:
    LeaveCriticalSection(&Lock);
    return done;
}

BOOL HookVtable(void **vtable, int slot, void *replacement, void **original) {
    return AddHook(vtable + slot, replacement, original, ModuleAt(__builtin_return_address(0)));
}

BOOL HookImport(BYTE *module, const char *dll, const char *name, void *replacement, void **original) {
    HMODULE owner = ModuleAt(__builtin_return_address(0));
    const IMAGE_NT_HEADERS64 *nt = (const void *)(module + ((const IMAGE_DOS_HEADER *)module)->e_lfanew);
    const IMAGE_DATA_DIRECTORY *directory = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory->VirtualAddress) return FALSE;
    const IMAGE_IMPORT_DESCRIPTOR *import = (const void *)(module + directory->VirtualAddress);
    for (; import->Name; import++) {
        if (_stricmp((const char *)module + import->Name, dll) || !import->OriginalFirstThunk) continue;
        const IMAGE_THUNK_DATA64 *names = (const void *)(module + import->OriginalFirstThunk);
        IMAGE_THUNK_DATA64 *slots = (void *)(module + import->FirstThunk);
        for (size_t i = 0; names[i].u1.AddressOfData; i++) {
            if (IMAGE_SNAP_BY_ORDINAL64(names[i].u1.Ordinal)) continue;
            const IMAGE_IMPORT_BY_NAME *entry = (const void *)(module + names[i].u1.AddressOfData);
            if (strcmp((const char *)entry->Name, name)) continue;
            return AddHook((void **)&slots[i].u1.Function, replacement, original, owner);
        }
    }
    return FALSE;
}

static BOOL WriteCode(BYTE *at, const void *bytes, size_t size) {
    DWORD protect;
    if (!VirtualProtect(at, size, PAGE_EXECUTE_READWRITE, &protect)) return FALSE;
    memcpy(at, bytes, size);
    VirtualProtect(at, size, protect, &protect);
    return FlushInstructionCache(GetCurrentProcess(), at, size);
}

BOOL EditCode(void *at, const void *before, const void *after, size_t size) {
    HMODULE owner = ModuleAt(__builtin_return_address(0));
    if (size > MAX_EDIT_SIZE) return FALSE;
    BYTE *start = at;
    EnterCriticalSection(&Lock);
    BOOL done = FALSE;
    if (memcmp(start, before, size)) goto out;
    for (int i = 0; i < EditCount; i++) {
        if (start >= Edits[i].at + Edits[i].size || Edits[i].at >= start + size) continue;
        // Editing something back to what it was takes the edit away.
        if (Edits[i].at != start || Edits[i].size != size || Edits[i].owner != owner || memcmp(Edits[i].before, after, size)) goto out;
        done = WriteCode(start, after, size);
        if (done) Edits[i] = Edits[--EditCount];
        goto out;
    }
    if (EditCount == MAX_EDITS) goto out;
    if (!WriteCode(start, after, size)) {
        ModLog("couldn't write to code at %p (error %lu)", start, GetLastError());
        goto out;
    }
    CodeEdit *edit = &Edits[EditCount++];
    *edit = (CodeEdit){.at = start, .size = size, .owner = owner};
    memcpy(edit->before, before, size);
    done = TRUE;
out:
    LeaveCriticalSection(&Lock);
    return done;
}

void UndoHooksOf(HMODULE owner) {
    EnterCriticalSection(&Lock);
    for (int i = HookCount - 1; i >= 0; i--) {
        if (Hooks[i].owner != owner) continue;
        // Whatever sat on top now calls whatever this one was calling.
        Hook *above = NULL;
        for (int j = i + 1; j < HookCount && !above; j++)
            if (Hooks[j].slot == Hooks[i].slot) above = &Hooks[j];
        if (above) {
            *above->original = Hooks[i].next;
            above->next = Hooks[i].next;
        } else {
            WriteSlot(Hooks[i].slot, Hooks[i].next);
        }
        memmove(&Hooks[i], &Hooks[i + 1], (HookCount - i - 1) * sizeof *Hooks);
        HookCount--;
    }
    for (int i = EditCount - 1; i >= 0; i--) {
        if (Edits[i].owner != owner) continue;
        WriteCode(Edits[i].at, Edits[i].before, Edits[i].size);
        Edits[i] = Edits[--EditCount];
    }
    LeaveCriticalSection(&Lock);
}

BOOL SafeRead(const void *from, void *to, size_t size) {
    SIZE_T got;
    return ReadProcessMemory(GetCurrentProcess(), from, to, size, &got) && got == size;
}

BOOL IsInAside(const void *pointer) {
    static const BYTE *base;
    static size_t size;
    if (!base) {
        const BYTE *module = (const BYTE *)GetModuleHandleW(L"Aside.dll");
        if (!module) return FALSE;
        size = ((const IMAGE_NT_HEADERS64 *)(module + ((const IMAGE_DOS_HEADER *)module)->e_lfanew))->OptionalHeader.SizeOfImage;
        base = module;
    }
    return (const BYTE *)pointer >= base && (const BYTE *)pointer < base + size;
}

int FindObjects(const void *vtable, void **found, int max) {
    ULONGLONG *chunk = VirtualAlloc(NULL, SCAN_CHUNK, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!chunk) return 0;
    int count = 0;
    MEMORY_BASIC_INFORMATION info;
    for (BYTE *at = NULL; count < max && VirtualQuery(at, &info, sizeof info); at = (BYTE *)info.BaseAddress + info.RegionSize) {
        if (info.State != MEM_COMMIT || info.Type != MEM_PRIVATE || info.Protect != PAGE_READWRITE) continue;
        if ((BYTE *)info.BaseAddress == (BYTE *)chunk) continue;
        for (size_t done = 0; done < info.RegionSize && count < max; done += SCAN_CHUNK) {
            size_t size = min(info.RegionSize - done, (size_t)SCAN_CHUNK);
            BYTE *from = (BYTE *)info.BaseAddress + done;
            // Other threads can release memory at any moment, and this copes with that.
            if (!SafeRead(from, chunk, size)) continue;
            for (size_t i = 0; i < size / sizeof *chunk && count < max; i++)
                if (chunk[i] == (ULONGLONG)vtable) found[count++] = from + i * sizeof *chunk;
        }
    }
    VirtualFree(chunk, 0, MEM_RELEASE);
    // The caller's own array holds nothing yet, but its copy of the vtable pointer might be anywhere.
    return count;
}

// The UI thread owns Chromium's message-only window. A hook on its message queue is the one way in
// that doesn't depend on any of Aside's code.
static CRITICAL_SECTION UiLock;
static LONG UiLockReady;
static void (*volatile UiFunction)(void *);
static void *UiArgument;
static HANDLE UiDone;
static DWORD UiThread;

static LRESULT CALLBACK OnUiMessage(int code, WPARAM wparam, LPARAM lparam) {
    void (*function)(void *) = UiFunction;
    if (function) {
        UiFunction = NULL;
        function(UiArgument);
        SetEvent(UiDone);
    }
    return CallNextHookEx(NULL, code, wparam, lparam);
}

// The browser's UI thread is the process's first.
static DWORD FindFirstThread(void) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 entry = {.dwSize = sizeof entry};
    ULONGLONG earliest = ~0ull;
    DWORD first = 0;
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != GetCurrentProcessId()) continue;
        HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
        FILETIME created, exited, kernel, user;
        if (thread && GetThreadTimes(thread, &created, &exited, &kernel, &user)) {
            ULONGLONG time = (ULONGLONG)created.dwHighDateTime << 32 | created.dwLowDateTime;
            if (time < earliest) {
                earliest = time;
                first = entry.th32ThreadID;
            }
        }
        if (thread) CloseHandle(thread);
    }
    CloseHandle(snapshot);
    return first;
}

// It's ready once it has a message window. Other threads have those too, such as the one capturing video, and a view
// touched from one of those stops the browser.
static DWORD FindUiThread(void) {
    DWORD first = FindFirstThread();
    HWND window = NULL;
    while (first && (window = FindWindowExW(HWND_MESSAGE, window, L"Chrome_MessageWindow", NULL)))
        if (GetWindowThreadProcessId(window, NULL) == first) return first;
    return 0;
}

void RunOnUiThread(void (*function)(void *), void *argument) {
    if (!InterlockedCompareExchange(&UiLockReady, 1, 0)) {
        InitializeCriticalSection(&UiLock);
        UiDone = CreateEventW(NULL, FALSE, FALSE, NULL);
        InterlockedExchange(&UiLockReady, 2);
    }
    while (UiLockReady != 2) Sleep(1);
    while (!UiThread && !(UiThread = FindUiThread())) Sleep(50);
    if (GetCurrentThreadId() == UiThread) {
        function(argument);
        return;
    }
    EnterCriticalSection(&UiLock);
    HHOOK hook = SetWindowsHookExW(WH_GETMESSAGE, OnUiMessage, ThisModule, UiThread);
    if (hook) {
        UiArgument = argument;
        UiFunction = function;
        // The thread might be asleep with nothing to do.
        do PostThreadMessageW(UiThread, WM_NULL, 0, 0);
        while (WaitForSingleObject(UiDone, 100) == WAIT_TIMEOUT);
        UnhookWindowsHookEx(hook);
    } else {
        ModLog("couldn't reach the UI thread (error %lu)", GetLastError());
    }
    LeaveCriticalSection(&UiLock);
}
