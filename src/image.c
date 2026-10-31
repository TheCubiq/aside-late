#include "aside-late.h"

#include <stdarg.h>

int Refuse(const char *format, ...) {
    va_list args;
    va_start(args, format);
    printf("  refused: ");
    vprintf(format, args);
    printf("\n");
    va_end(args);
    return 0;
}

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

// Lays the file out the way the loader does, so patches work in RVAs.
BOOL LoadImageFile(HANDLE file, Image *img) {
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

void FreeImageFile(Image *img) {
    if (img->base) VirtualFree(img->base, 0, MEM_RELEASE);
    img->base = NULL;
}

int FindAndPrintEdits(const Patch *patch, const Image *img, Edit *edits) {
    printf("%s: %s\n", patch->id, patch->title);
    int count = patch->find(img, edits);
    for (int i = 0; i < count; i++) printf("  %#lx  %s\n", edits[i].rva, edits[i].what);
    return count;
}

const Function *FunctionAt(const Image *img, DWORD rva) {
    const Function *table = (const void *)(img->base + img->pdata->VirtualAddress);
    size_t low = 0, high = img->pdata->Misc.VirtualSize / sizeof *table;
    while (low < high) {
        size_t mid = (low + high) / 2;
        if (rva < table[mid].BeginAddress) high = mid;
        else if (rva >= table[mid].EndAddress) low = mid + 1;
        else return &table[mid];
    }
    return NULL;
}

DWORD FindUniqueString(const Image *img, const char *string) {
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

const Function *FindLargestReferrer(const Image *img, DWORD string, DWORD *xref) {
    const Function *best = NULL;
    DWORD start = img->text->VirtualAddress, end = start + img->text->Misc.VirtualSize - 7;
    for (DWORD rva = start; rva < end; rva++) {
        const BYTE *p = img->base + rva;
        if (p[1] != 0x8d || (p[0] != 0x48 && p[0] != 0x4c) || (p[2] & 0xc7) != 0x05) continue;
        if (rva + 7 + *(const INT32 *)(p + 3) != string) continue;
        const Function *fn = FunctionAt(img, rva);
        if (fn && (!best || fn->EndAddress - fn->BeginAddress > best->EndAddress - best->BeginAddress)) {
            best = fn;
            *xref = rva;
        }
    }
    return best;
}

BOOL IsSetVisible(const BYTE *p) {
    static const BYTE slot[] = {0x48, 0x8b, 0x80, 0x90, 0x00, 0x00, 0x00};
    return p[0] == 0x48 && p[1] == 0x8b && p[2] <= 0x07 && p[2] != 0x04 && p[2] != 0x05 && !memcmp(p + 3, slot, sizeof slot);
}

int MovEdxSize(const BYTE *p) {
    int prefix = p[0] == 0x44;
    return p[prefix] == 0x89 && (p[prefix + 1] & 0xc7) == 0xc2 ? prefix + 2 : 0;
}

Edit JumpAlways(const Image *img, DWORD rva, const char *what) {
    Edit edit = {.rva = rva, .size = 1, .after = {0xeb}, .what = what};
    edit.before[0] = img->base[rva];
    return edit;
}

Edit ClearEdx(const Image *img, DWORD rva, int size, const char *what) {
    Edit edit = {.rva = rva, .size = size, .what = what};
    memcpy(edit.before, img->base + rva, size);
    memset(edit.after, 0x90, size);
    edit.after[0] = 0x31;
    edit.after[1] = 0xd2;
    return edit;
}
