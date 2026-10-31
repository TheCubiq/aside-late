#include "aside-late.h"

// The work happens in the page's own DOM, so a build has nothing here to check.
static int Find(const Image *img, Edit *edits) {
    (void)img, (void)edits;
    return RUNS_IN_ASIDE;
}

PATCH_IN_ASIDE("settings", Find)
