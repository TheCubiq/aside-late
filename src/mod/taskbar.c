// Windows groups taskbar buttons by app ID, and Aside's windows say "Aside". When a shortcut of our own started this
// Aside, its windows take that shortcut's ID, so they appear under its pin. Aside can set its ID again at any point after
// making a window, so this looks again now and then as well as when a window appears.

#define COBJMACROS
#include "internal.h"

#include <propsys.h>
#include <shellapi.h>

#define LOOK_AGAIN_MS 2000
#define ID_SIZE_ 128

static wchar_t AppId[ID_SIZE_], RelaunchCommand[PATH_SIZE], RelaunchIcon[PATH_SIZE];
static DWORD Process;

static const IID StoreId = {0x886d8eeb, 0x8cf2, 0x4446, {0x8d, 0x02, 0xcd, 0xba, 0x1d, 0xbd, 0xcf, 0x99}};
#define APP_KEY(number) {{0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, number}
static const PROPERTYKEY IdKey = APP_KEY(5), CommandKey = APP_KEY(2), IconKey = APP_KEY(3), NameKey = APP_KEY(4);

static void SetText(IPropertyStore *store, const PROPERTYKEY *key, const wchar_t *text) {
    PROPVARIANT value;
    memset(&value, 0, sizeof value);
    value.vt = VT_LPWSTR;
    value.pwszVal = (wchar_t *)text;
    IPropertyStore_SetValue(store, key, &value);
}

static void Claim(HWND window) {
    wchar_t class[32];
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process != Process || GetAncestor(window, GA_ROOT) != window || GetWindow(window, GW_OWNER)) return;
    if (!GetClassNameW(window, class, 32) || wcscmp(class, L"Chrome_WidgetWin_1")) return;

    IPropertyStore *store = NULL;
    if (FAILED(SHGetPropertyStoreForWindow(window, &StoreId, (void **)&store))) return;
    PROPVARIANT current;
    memset(&current, 0, sizeof current);
    BOOL same = SUCCEEDED(IPropertyStore_GetValue(store, &IdKey, &current)) && current.vt == VT_LPWSTR && !wcscmp(current.pwszVal, AppId);
    PropVariantClear(&current);
    if (!same) {
        // Pinning from the window should give the same shortcut, not plain Aside.
        SetText(store, &CommandKey, RelaunchCommand);
        SetText(store, &NameKey, L"Aside (patched)");
        SetText(store, &IconKey, RelaunchIcon);
        SetText(store, &IdKey, AppId);
        IPropertyStore_Commit(store);
    }
    IPropertyStore_Release(store);
}

static void CALLBACK OnWindow(HWINEVENTHOOK hook, DWORD event, HWND window, LONG object, LONG child, DWORD thread, DWORD time) {
    (void)hook, (void)event, (void)thread, (void)time;
    if (window && object == OBJID_WINDOW && child == CHILDID_SELF) Claim(window);
}

static BOOL CALLBACK ClaimEach(HWND window, LPARAM unused) {
    (void)unused;
    Claim(window);
    return TRUE;
}

static DWORD WINAPI TaskbarMain(void *unused) {
    (void)unused;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, NULL, OnWindow, Process, 0, WINEVENT_OUTOFCONTEXT);
    SetTimer(NULL, 0, LOOK_AGAIN_MS, NULL);
    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        if (message.message == WM_TIMER) EnumWindows(ClaimEach, 0);
        else DispatchMessageW(&message);
    }
    return 0;
}

void TaskbarStart(void) {
    if (!GetEnvironmentVariableW(L"ASIDE_LATE_APP_ID", AppId, ID_SIZE_)) return;
    Process = GetCurrentProcessId();
    swprintf(RelaunchCommand, PATH_SIZE, L"\"%ls\\Aside Late.exe\" --launch", Home);
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    swprintf(RelaunchIcon, PATH_SIZE, L"%ls,0", self);
    CloseHandle(CreateThread(NULL, 0, TaskbarMain, NULL, 0, NULL));
}
