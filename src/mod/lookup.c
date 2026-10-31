#include "lookup.h"

static const IMAGE_SECTION_HEADER *FindSection(const IMAGE_NT_HEADERS64 *nt, const char *name) {
    const IMAGE_SECTION_HEADER *s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++)
        if (!strncmp((const char *)s->Name, name, IMAGE_SIZEOF_SHORT_NAME)) return s;
    return NULL;
}

static int CompareReferences(const void *a, const void *b) {
    const Reference *left = a, *right = b;
    if (left->target != right->target) return left->target < right->target ? -1 : 1;
    return left->site < right->site ? -1 : left->site > right->site;
}

static BOOL IndexCode(Lookup *lookup) {
    DWORD start = lookup->text->VirtualAddress, end = start + lookup->text->Misc.VirtualSize - 7;
    size_t capacity = 1 << 20;
    lookup->code = malloc(capacity * sizeof *lookup->code);
    if (!lookup->code) return FALSE;
    const BYTE *base = lookup->base;
    for (DWORD rva = start; rva < end; rva++) {
        const BYTE *p = base + rva;
        if (p[1] != 0x8d || (p[0] != 0x48 && p[0] != 0x4c) || (p[2] & 0xc7) != 0x05) continue;
        if (lookup->codeCount == capacity) {
            capacity *= 2;
            Reference *grown = realloc(lookup->code, capacity * sizeof *lookup->code);
            if (!grown) return FALSE;
            lookup->code = grown;
        }
        lookup->code[lookup->codeCount++] = (Reference){rva + 7 + *(const INT32 *)(p + 3), rva};
    }
    qsort(lookup->code, lookup->codeCount, sizeof *lookup->code, CompareReferences);
    return TRUE;
}

static BOOL IndexPointers(Lookup *lookup) {
    ULONGLONG low = lookup->pointerBase + lookup->text->VirtualAddress;
    ULONGLONG high = low + lookup->text->Misc.VirtualSize;
    size_t capacity = 1 << 20;
    lookup->pointers = malloc(capacity * sizeof *lookup->pointers);
    if (!lookup->pointers) return FALSE;
    DWORD start = lookup->rdata->VirtualAddress, end = start + lookup->rdata->Misc.VirtualSize - 8;
    for (DWORD rva = start; rva <= end; rva += 8) {
        ULONGLONG value = *(const ULONGLONG *)(lookup->base + rva);
        if (value < low || value >= high) continue;
        if (lookup->pointerCount == capacity) {
            capacity *= 2;
            Reference *grown = realloc(lookup->pointers, capacity * sizeof *lookup->pointers);
            if (!grown) return FALSE;
            lookup->pointers = grown;
        }
        lookup->pointers[lookup->pointerCount++] = (Reference){(DWORD)(value - lookup->pointerBase), rva};
    }
    qsort(lookup->pointers, lookup->pointerCount, sizeof *lookup->pointers, CompareReferences);
    return TRUE;
}

BOOL LookupInit(Lookup *lookup, BYTE *base, ULONGLONG pointerBase) {
    memset(lookup, 0, sizeof *lookup);
    lookup->base = base;
    lookup->pointerBase = pointerBase;
    const IMAGE_NT_HEADERS64 *nt = (const void *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
    lookup->text = FindSection(nt, ".text");
    lookup->rdata = FindSection(nt, ".rdata");
    lookup->pdata = FindSection(nt, ".pdata");
    return lookup->text && lookup->rdata && lookup->pdata && IndexCode(lookup) && IndexPointers(lookup);
}

void LookupFree(Lookup *lookup) {
    free(lookup->code);
    free(lookup->pointers);
    memset(lookup, 0, sizeof *lookup);
}

const RUNTIME_FUNCTION *LookupFunction(const Lookup *lookup, DWORD rva) {
    const RUNTIME_FUNCTION *table = (const void *)(lookup->base + lookup->pdata->VirtualAddress);
    size_t low = 0, high = lookup->pdata->Misc.VirtualSize / sizeof *table;
    while (low < high) {
        size_t mid = (low + high) / 2;
        if (rva < table[mid].BeginAddress) high = mid;
        else if (rva >= table[mid].EndAddress) low = mid + 1;
        else return &table[mid];
    }
    return NULL;
}

int LookupStrings(const Lookup *lookup, const char *string, DWORD *found, int max) {
    const BYTE *start = lookup->base + lookup->rdata->VirtualAddress;
    const BYTE *end = start + lookup->rdata->Misc.VirtualSize;
    size_t len = strlen(string) + 1;
    int count = 0;
    for (const BYTE *p = start; (p = memchr(p, string[0], end - p)) && p + len <= end; p++) {
        if (memcmp(p, string, len)) continue;
        // A suffix of a longer string isn't this string.
        if (p > start && p[-1]) continue;
        if (count < max) found[count] = (DWORD)(p - lookup->base);
        count++;
    }
    return count;
}

static const Reference *FindRange(const Reference *table, size_t tableCount, DWORD target, size_t *count) {
    size_t low = 0, high = tableCount;
    while (low < high) {
        size_t mid = (low + high) / 2;
        if (table[mid].target < target) low = mid + 1;
        else high = mid;
    }
    size_t end = low;
    while (end < tableCount && table[end].target == target) end++;
    *count = end - low;
    return table + low;
}

const Reference *LookupCode(const Lookup *lookup, DWORD target, size_t *count) {
    return FindRange(lookup->code, lookup->codeCount, target, count);
}

const Reference *LookupPointers(const Lookup *lookup, DWORD target, size_t *count) {
    return FindRange(lookup->pointers, lookup->pointerCount, target, count);
}

const void *LookupRva(const Lookup *lookup, DWORD rva) {
    return lookup->base + rva;
}

DWORD LookupPointerRva(const Lookup *lookup, const void *at) {
    ULONGLONG value = *(const ULONGLONG *)at;
    ULONGLONG low = lookup->pointerBase + lookup->text->VirtualAddress;
    if (value < low || value >= low + lookup->text->Misc.VirtualSize) return 0;
    return (DWORD)(value - lookup->pointerBase);
}

// ClassMetaData's constructor is followed by: lea rax,[name]; mov [reg+8],rax
static DWORD FindMetaDataFunction(const Lookup *lookup, const char *className) {
    DWORD strings[8], function = 0;
    int stringCount = LookupStrings(lookup, className, strings, 8);
    for (int i = 0; i < min(stringCount, 8); i++) {
        size_t count;
        const Reference *references = LookupCode(lookup, strings[i], &count);
        for (size_t j = 0; j < count; j++) {
            const BYTE *next = lookup->base + references[j].site + 7;
            if (next[0] != 0x48 || next[1] != 0x89 || (next[2] & 0xc0) != 0x40 || next[3] != 0x08) continue;
            const RUNTIME_FUNCTION *fn = LookupFunction(lookup, references[j].site);
            if (!fn) continue;
            if (function && function != fn->BeginAddress) return 0;
            function = fn->BeginAddress;
        }
    }
    return function;
}

// lea reg,[vtable]; mov [this],reg. Returns the vtable RVA if this stores to offset 0.
static DWORD VtableStoredAtZero(const Lookup *lookup, DWORD rva) {
    const BYTE *p = lookup->base + rva;
    if ((p[0] != 0x48 && p[0] != 0x4c) || p[1] != 0x8d || (p[2] & 0xc7) != 0x05) return 0;
    if ((p[7] & 0xf8) != 0x48 || p[8] != 0x89 || (p[9] & 0xc0) || (p[9] & 7) == 4 || (p[9] & 7) == 5) return 0;
    if (((p[9] >> 3) & 7) != ((p[2] >> 3) & 7)) return 0;
    DWORD target = rva + 7 + *(const INT32 *)(p + 3);
    DWORD start = lookup->rdata->VirtualAddress;
    return target >= start && target < start + lookup->rdata->Misc.VirtualSize ? target : 0;
}

// MetaData sits in the MetaDataProvider vtable, which starts after a null. The constructor
// that installs that vtable installs the primary one at offset 0 close by.
DWORD LookupClassVtable(const Lookup *lookup, const char *className) {
    DWORD function = FindMetaDataFunction(lookup, className);
    if (!function) return 0;
    size_t count;
    const Reference *slots = LookupPointers(lookup, function, &count);
    if (count != 1) return 0;
    DWORD start = slots[0].site;
    while (*(const ULONGLONG *)(lookup->base + start - 8) && slots[0].site - start < 64) start -= 8;

    const Reference *sites = LookupCode(lookup, start, &count);
    DWORD best = 0, bestDistance = MAXDWORD;
    for (size_t i = 0; i < count; i++) {
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, sites[i].site);
        if (!fn) continue;
        for (DWORD rva = fn->BeginAddress; rva + 10 <= fn->EndAddress; rva++) {
            DWORD vtable = VtableStoredAtZero(lookup, rva);
            if (!vtable) continue;
            DWORD distance = rva > sites[i].site ? rva - sites[i].site : sites[i].site - rva;
            if (distance < bestDistance) {
                bestDistance = distance;
                best = vtable;
            }
        }
    }
    return best;
}
