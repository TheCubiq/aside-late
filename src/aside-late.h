#pragma once

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PATCHES 64
#define MAX_EDITS 8
#define MAX_EDIT_SIZE 8

typedef struct {
    BYTE *base;
    DWORD size;
    const IMAGE_SECTION_HEADER *text, *rdata, *pdata;
} Image;

typedef IMAGE_RUNTIME_FUNCTION_ENTRY Function;

typedef struct {
    DWORD rva;
    int size;
    BYTE before[MAX_EDIT_SIZE], after[MAX_EDIT_SIZE];
    const char *what;
} Edit;

// Finds this patch's code in a build. Returns how many edits it wrote, or Refuse(...).
typedef int (*FindEdits)(const Image *img, Edit *edits);

typedef struct {
    const char *id, *title, *description;
    FindEdits find;
    BOOL skipped;
} Patch;

extern Patch Patches[];
extern int PatchCount;
void RegisterPatch(const char *manifest, FindEdits find);

// Declares a patch from its folder under src/patches, embedding that folder's patch.json.
#define PATCH(folder, find)                                                                                     \
    __asm__(".section .rdata,\"dr\"\n.Lmanifest:\n.incbin \"src/patches/" folder "/patch.json\"\n.byte 0\n.text"); \
    __attribute__((constructor)) static void RegisterThisPatch(void) {                                          \
        const char *manifest;                                                                                   \
        __asm__("leaq .Lmanifest(%%rip), %0" : "=r"(manifest));                                                 \
        RegisterPatch(manifest, find);                                                                          \
    }

int Refuse(const char *format, ...);

BOOL LoadImageFile(HANDLE file, Image *img);
void FreeImageFile(Image *img);
int FindAndPrintEdits(const Patch *patch, const Image *img, Edit *edits);

const Function *FunctionAt(const Image *img, DWORD rva);
DWORD FindUniqueString(const Image *img, const char *string);
const Function *FindLargestReferrer(const Image *img, DWORD string, DWORD *xref);

// mov rax,[reg]; mov rax,[rax+SetVisible]
#define SET_VISIBLE_SIZE 10
BOOL IsSetVisible(const BYTE *p);
// mov edx,<any register>. Returns its size, or 0.
int MovEdxSize(const BYTE *p);

// Makes a short conditional jump always taken.
Edit JumpAlways(const Image *img, DWORD rva, const char *what);
// Replaces an instruction with xor edx,edx, padded with nops.
Edit ClearEdx(const Image *img, DWORD rva, int size, const char *what);

int Launch(const wchar_t *exe, wchar_t *commandLine);
