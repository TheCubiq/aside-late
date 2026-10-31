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
    printf("  mouse slot %d, right click %#lx, closed %#lx, flags +%#lx, location +%#lx, callback +%#lx, widget +%#lx, to screen %#lx\n", bindings.mousePressedSlot,
           bindings.rightClick, bindings.menuClosed, bindings.eventFlagsOffset, bindings.eventLocationOffset, bindings.rightClickOffset, bindings.widgetMethodOffset, bindings.convertToScreen);
    printf("  node type +%#lx, managed +%#lx, id +%#lx; owner command +%#lx, node +%#lx, has +%#lx, generation +%#lx\n", bindings.nodeTypeOffset, bindings.nodeManagedOffset,
           bindings.nodeIdOffset, bindings.ownerCommandOffset, bindings.ownerNodeIdOffset, bindings.ownerHasNodeOffset, bindings.ownerGenerationOffset);
    printf("  new %#lx, delete %#lx, allocator %#lx, allocate %#lx, view %#lx, add child %#lx, preferred %#lx, set layout %#lx\n", bindings.operatorNew, bindings.operatorDelete,
           bindings.viewAllocator, bindings.viewAllocate, bindings.viewConstruct, bindings.addChild, bindings.setPreferredSize, bindings.setLayout);
    printf("  layout %#lx, box %#lx %#lx, align +%#lx +%#lx, border %#lx, set border +%#lx\n", bindings.layoutConstruct, bindings.boxVtables[0], bindings.boxVtables[1],
           bindings.mainAlignOffset, bindings.crossAlignOffset, bindings.borderVtable, bindings.setBorderOffset);
    printf("  colors %#lx x%d, size %#lx, button %#lx, update %#lx, focus %#lx, highlight %#lx, id +%#lx, callback +%#lx, selected +%#lx, flag +%#lx, ink +%#lx\n", bindings.colorIds,
           bindings.colorCount, bindings.colorSize, bindings.buttonConstruct, bindings.updateColor, bindings.setFocusable, bindings.installHighlight, bindings.colorIdOffset,
           bindings.colorCallbackOffset, bindings.colorSelectedOffset, bindings.colorFlagOffset, bindings.colorInkOffset);
    printf("  color vtables:");
    for (int i = 0; i < bindings.colorVtableCount; i++) printf(" +%#lx=%#lx", bindings.colorVtables[i].offset, bindings.colorVtables[i].vtable);
    printf("\n  item %#lx size %#lx, themed %#lx; popup %#lx size %#lx, destruct %#lx, contents %#lx; widget %#lx, prepare %#lx, show %#lx, close %#lx; picker %#lx size %#lx\n", bindings.itemConstruct,
           bindings.itemSize, bindings.themedColorId, bindings.popupConstruct, bindings.popupSize, bindings.popupDestruct, bindings.popupSetContents, bindings.widgetCreate, bindings.widgetPrepare,
           bindings.widgetShow, bindings.widgetClose, bindings.pickerConstruct, bindings.pickerSize);
    printf("  group backgrounds");
    for (int i = 0; i < GROUP_COLOR_COUNT; i++) printf(" %#lx", bindings.groupBackgrounds[i]);
    printf(", foreground %#lx; get color %#lx, theme +%#lx, provider +%#lx\n", bindings.groupForeground, bindings.getColor, bindings.widgetThemeOffset, bindings.themeProviderOffset);
    return RUNS_IN_ASIDE;
}

PATCH_IN_ASIDE("bookmark-folder-appearance", Find)
