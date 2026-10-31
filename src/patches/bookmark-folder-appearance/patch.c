#include "aside-late.h"
#include "resolve.h"

// The work happens inside Aside. This only says whether a build still has what it needs.
static int Find(const Image *img, Edit *edits) {
    (void)edits;
    const IMAGE_NT_HEADERS64 *nt = (const void *)(img->base + ((const IMAGE_DOS_HEADER *)img->base)->e_lfanew);
    Lookup lookup;
    if (!LookupInit(&lookup, img->base, nt->OptionalHeader.ImageBase)) return Refuse("couldn't index the DLL");
    Bindings bindings;
    const char *missing = ResolveBindings(&lookup, &bindings);
    LookupFree(&lookup);
    if (missing) return Refuse("didn't find %s", missing);
    printf("  row vtable %#lx, paint slot %d, bounds +%#lx, icons %#lx (create %#lx, set %#lx, destroy %#lx)\n", bindings.rowVtable,
           bindings.paintBackgroundSlot, bindings.boundsOffset, bindings.updateIcons, bindings.createImage, bindings.setImage, bindings.destroyImage);
    printf("  leading +%#lx, caret +%#lx, title +%#lx, selected +%#lx, title color slot %#lx id %#lx, %d folder icons\n", bindings.leadingOffset,
           bindings.caretOffset, bindings.titleOffset, bindings.selectedOffset, bindings.setTitleColorOffset, bindings.titleColorId, bindings.iconCount);
    return RUNS_IN_ASIDE;
}

PATCH_IN_ASIDE("bookmark-folder-appearance", Find)
