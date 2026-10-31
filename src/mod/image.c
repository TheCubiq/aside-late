#include "internal.h"

static BOOL ReadAt(HANDLE file, ULONGLONG offset, void *out, DWORD size) {
    LARGE_INTEGER at = {.QuadPart = (LONGLONG)offset};
    DWORD got;
    return SetFilePointerEx(file, at, NULL, FILE_BEGIN) && ReadFile(file, out, size, &got, NULL) && got == size;
}

// Lays the file out the way the loader does, so patches work in RVAs.
BOOL LookupFromFile(const wchar_t *path, Lookup *lookup, BYTE **copy) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    BOOL done = FALSE;
    BYTE head[4096];
    *copy = NULL;
    if (!ReadAt(file, 0, head, sizeof head)) goto out;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)head;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew > (LONG)(sizeof head - sizeof(IMAGE_NT_HEADERS64))) goto out;
    const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(head + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) goto out;

    *copy = VirtualAlloc(NULL, nt->OptionalHeader.SizeOfImage, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!*copy) goto out;
    memcpy(*copy, head, sizeof head);
    nt = (const IMAGE_NT_HEADERS64 *)(*copy + dos->e_lfanew);
    const IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++) {
        DWORD raw = min(s->SizeOfRawData, s->Misc.VirtualSize);
        if (raw && !ReadAt(file, s->PointerToRawData, *copy + s->VirtualAddress, raw)) goto out;
    }
    done = LookupInit(lookup, *copy, nt->OptionalHeader.ImageBase);
out:
    CloseHandle(file);
    if (!done && *copy) {
        VirtualFree(*copy, 0, MEM_RELEASE);
        *copy = NULL;
    }
    return done;
}

void LookupFromFileFree(Lookup *lookup, BYTE *copy) {
    LookupFree(lookup);
    if (copy) VirtualFree(copy, 0, MEM_RELEASE);
}
