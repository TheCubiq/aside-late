#include "internal.h"

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

DWORD LookupVtableInstalledBy(const Lookup *lookup, const RUNTIME_FUNCTION *constructor) {
    for (DWORD rva = constructor->BeginAddress; rva + 10 <= constructor->EndAddress; rva++) {
        DWORD vtable = VtableStoredAtZero(lookup, rva);
        if (vtable) return vtable;
    }
    return 0;
}

#define MAX_VIEW_SLOTS 512

// Every view that doesn't override a method has it in the same slot. Walk back from each
// vtable entry to the start of its table and take the index most of them agree on.
BOOL LookupViewMethodSlot(const Lookup *lookup, const char *trace, int *slot, DWORD *method) {
    DWORD strings[4];
    if (LookupStrings(lookup, trace, strings, 4) != 1) return FALSE;
    size_t count;
    const Reference *references = LookupCode(lookup, strings[0], &count);
    int votes[MAX_VIEW_SLOTS] = {0}, best = 0;
    for (size_t i = 0; i < count; i++) {
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, references[i].site);
        size_t entries;
        const Reference *sites = fn ? LookupPointers(lookup, fn->BeginAddress, &entries) : NULL;
        for (size_t j = 0; sites && j < entries; j++) {
            int index = 0;
            while (index < MAX_VIEW_SLOTS && LookupPointerRva(lookup, lookup->base + sites[j].site - (index + 1) * 8)) index++;
            if (index < MAX_VIEW_SLOTS && ++votes[index] > best) {
                best = votes[index];
                *slot = index;
                *method = fn->BeginAddress;
            }
        }
    }
    return best >= 8;
}

// cmp dl,[rcx+visible]; je skip; mov rax,[rcx]; mov rax,[rax+SetVisible]
BOOL LookupVisibility(const Lookup *lookup, int *setVisibleSlot, DWORD *visibleOffset) {
    enum { MAX_KINDS = 16 };
    struct {
        DWORD visible, method;
        int votes;
    } kinds[MAX_KINDS] = {0};
    DWORD start = lookup->text->VirtualAddress, end = start + lookup->text->Misc.VirtualSize - 20;
    for (DWORD rva = start; rva < end; rva++) {
        const BYTE *p = lookup->base + rva;
        if (p[0] != 0x3a || p[1] != 0x91 || p[6] != 0x74 || p[8] != 0x48 || p[9] != 0x8b || p[10] != 0x01) continue;
        if (p[11] != 0x48 || p[12] != 0x8b || p[13] != 0x80) continue;
        DWORD visible = *(const DWORD *)(p + 2), method = *(const DWORD *)(p + 14);
        if (method % 8 || method > 0x1000 || visible > 0x1000) continue;
        for (int i = 0; i < MAX_KINDS; i++) {
            if (kinds[i].votes && (kinds[i].visible != visible || kinds[i].method != method)) continue;
            kinds[i].visible = visible;
            kinds[i].method = method;
            kinds[i].votes++;
            break;
        }
    }
    int best = 0, total = 0;
    for (int i = 0; i < MAX_KINDS; i++) {
        total += kinds[i].votes;
        if (kinds[i].votes > kinds[best].votes) best = i;
    }
    if (kinds[best].votes < 3 || kinds[best].votes * 2 <= total) return FALSE;
    *setVisibleSlot = kinds[best].method / 8;
    *visibleOffset = kinds[best].visible;
    return TRUE;
}

const RUNTIME_FUNCTION *LookupTraced(const Lookup *lookup, const char *trace) {
    DWORD strings[2];
    if (LookupStrings(lookup, trace, strings, 2) != 1) return NULL;
    size_t count;
    const Reference *sites = LookupCode(lookup, strings[0], &count);
    return count == 1 ? LookupFunction(lookup, sites[0].site) : NULL;
}

// SchedulePaint builds gfx::Rect(size()) on the stack and asks if the view has a custom
// Paint: mov qword [rsp+x],0; mov rax,[rcx+size]; mov [rsp+y],rax; mov rcx,[rcx]; mov rax,[rcx+50h]
static BOOL FindBoundsOffset(const Lookup *lookup, DWORD *boundsOffset, DWORD *schedulePaint) {
    static const BYTE tail[] = {0x48, 0x8b, 0x09, 0x48, 0x8b, 0x41, 0x50};
    DWORD start = lookup->text->VirtualAddress, end = start + lookup->text->Misc.VirtualSize - 32;
    BOOL found = FALSE;
    for (DWORD rva = start; rva < end; rva++) {
        const BYTE *p = lookup->base + rva;
        if (p[0] != 0x48 || p[1] != 0xc7 || p[2] != 0x44 || p[3] != 0x24 || *(const DWORD *)(p + 5)) continue;
        if (p[9] != 0x48 || p[10] != 0x8b || p[11] != 0x81) continue;
        if (p[16] != 0x48 || p[17] != 0x89 || p[18] != 0x44 || p[19] != 0x24 || memcmp(p + 21, tail, sizeof tail)) continue;
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, rva);
        if (!fn || rva - fn->BeginAddress > 64) continue;
        if (found) return FALSE;
        found = TRUE;
        *boundsOffset = *(const DWORD *)(p + 12) - 8;
        *schedulePaint = fn->BeginAddress;
    }
    return found;
}

// A bookmark row's Layout places its close button by hand, comparing its bounds before setting them:
// cmp [rcx+bounds],r8d; ...; lea rdx,[rsp+x]; call SetBoundsRect
BOOL LookupViewLayout(const Lookup *lookup, DWORD *boundsOffset, DWORD *schedulePaint, DWORD *setBounds, int *layoutSlot) {
    DWORD row = LookupClassVtable(lookup, "VerticalBookmarkRowView");
    if (!row || !FindBoundsOffset(lookup, boundsOffset, schedulePaint)) return FALSE;
    BYTE compare[7] = {0x44, 0x39, 0x81};
    memcpy(compare + 3, boundsOffset, 4);
    const ULONGLONG *vtable = LookupRva(lookup, row);
    for (int slot = 0; slot < 512; slot++) {
        DWORD method = LookupPointerRva(lookup, vtable + slot);
        if (!method) break;
        const RUNTIME_FUNCTION *fn = LookupFunction(lookup, method);
        if (!fn || fn->BeginAddress != method || fn->EndAddress - method > 1024) continue;
        for (DWORD at = method; at + 7 <= fn->EndAddress; at++) {
            if (memcmp(lookup->base + at, compare, sizeof compare)) continue;
            for (DWORD call = at; call + 10 <= fn->EndAddress; call++) {
                const BYTE *p = lookup->base + call;
                if (memcmp(p, "\x48\x8d\x54\x24", 4) || p[5] != 0xe8) continue;
                *setBounds = call + 10 + *(const INT32 *)(p + 6);
                *layoutSlot = slot;
                return TRUE;
            }
        }
    }
    return FALSE;
}

// cmp byte [rsi+flag],0; je <int3>
DWORD LookupNeedsLayout(const Lookup *lookup, const RUNTIME_FUNCTION *layout) {
    for (DWORD rva = layout->BeginAddress; rva + 13 <= layout->EndAddress; rva++) {
        const BYTE *p = lookup->base + rva;
        if (p[0] != 0x80 || p[1] != 0xbe || p[6] != 0 || p[7] != 0x0f || p[8] != 0x84) continue;
        if (lookup->base[rva + 13 + *(const INT32 *)(p + 9)] == 0xcc) return *(const DWORD *)(p + 2);
    }
    return 0;
}

// What patches have worked out about a build, by name, so the next patch asking the same skips the work. A build is told
// apart by its header, so this outlives the copy it was worked out on, and a scan of other builds keeps them apart.
#define MAX_REMEMBERED 256
#define REMEMBERED_NAME 64

typedef struct {
    DWORD stamp, size;
    char name[REMEMBERED_NAME];
    void *value;
    size_t valueSize;
} Remembered;

static Remembered Memory[MAX_REMEMBERED];
static int MemoryCount;
static SRWLOCK MemoryLock = SRWLOCK_INIT;

static void BuildOf(const Lookup *lookup, DWORD *stamp, DWORD *size) {
    const IMAGE_NT_HEADERS64 *nt = (const void *)(lookup->base + ((const IMAGE_DOS_HEADER *)lookup->base)->e_lfanew);
    *stamp = nt->FileHeader.TimeDateStamp;
    *size = nt->OptionalHeader.SizeOfImage;
}

static Remembered *FindRemembered(const Lookup *lookup, const char *name) {
    DWORD stamp, size;
    BuildOf(lookup, &stamp, &size);
    for (int i = 0; i < MemoryCount; i++)
        if (Memory[i].stamp == stamp && Memory[i].size == size && !strcmp(Memory[i].name, name)) return &Memory[i];
    return NULL;
}

BOOL LookupRecall(const Lookup *lookup, const char *name, void *value, size_t size) {
    AcquireSRWLockShared(&MemoryLock);
    const Remembered *found = FindRemembered(lookup, name);
    BOOL recalled = found && found->valueSize == size;
    if (recalled) memcpy(value, found->value, size);
    ReleaseSRWLockShared(&MemoryLock);
    return recalled;
}

void LookupRemember(const Lookup *lookup, const char *name, const void *value, size_t size) {
    if (strlen(name) >= REMEMBERED_NAME) return;
    void *copy = malloc(size);
    if (!copy) return;
    memcpy(copy, value, size);
    AcquireSRWLockExclusive(&MemoryLock);
    Remembered *slot = FindRemembered(lookup, name);
    if (!slot && MemoryCount < MAX_REMEMBERED) {
        slot = &Memory[MemoryCount++];
        BuildOf(lookup, &slot->stamp, &slot->size);
        strcpy(slot->name, name);
        slot->value = NULL;
    }
    if (slot) {
        free(slot->value);
        slot->value = copy;
        slot->valueSize = size;
        copy = NULL;
    }
    ReleaseSRWLockExclusive(&MemoryLock);
    free(copy);
}
