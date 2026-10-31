// Keeps views hidden whatever Aside asks of them, remembers what it asked, and hands that back when
// the patch is turned off. Include this in one file of a patch.
#pragma once

#include "aside-late.h"

#define HOLD_MAX_VIEWS 64
#define HOLD_MAX_CLASSES 8

typedef void (*HoldSetVisible)(void *view, BOOL visible);

static struct {
    BYTE *view;
    void *vtable;
    BOOL asked;
} HoldViews[HOLD_MAX_VIEWS];
static int HoldViewCount;
static struct {
    void *vtable;
    HoldSetVisible original;
} HoldClasses[HOLD_MAX_CLASSES];
static int HoldClassCount, HoldSlot;
static DWORD HoldVisibleOffset;
// Lets a patch take in views it hasn't met yet, such as those of a new window.
static BOOL (*HoldShouldTake)(const BYTE *view);

// A closed window's views are gone, and a freed object doesn't keep its vtable.
static void HoldForgetDead(void) {
    for (int i = 0; i < HoldViewCount; i++) {
        void *vtable;
        if (!SafeRead(HoldViews[i].view, &vtable, sizeof vtable) || vtable != HoldViews[i].vtable) HoldViews[i--] = HoldViews[--HoldViewCount];
    }
}

static int HoldFind(const BYTE *view) {
    for (int i = 0; i < HoldViewCount; i++)
        if (HoldViews[i].view == view) return i;
    return -1;
}

static int HoldAdd(BYTE *view, BOOL asked) {
    HoldForgetDead();
    if (HoldViewCount == HOLD_MAX_VIEWS) return -1;
    HoldViews[HoldViewCount].view = view;
    HoldViews[HoldViewCount].vtable = *(void **)view;
    HoldViews[HoldViewCount].asked = asked;
    return HoldViewCount++;
}

static HoldSetVisible HoldOriginal(const void *vtable) {
    for (int i = 0; i < HoldClassCount; i++)
        if (HoldClasses[i].vtable == vtable) return HoldClasses[i].original;
    return NULL;
}

static void HoldOnSetVisible(void *self, BOOL visible) {
    BYTE *view = self;
    int held = HoldFind(view);
    if (held < 0 && HoldShouldTake && HoldShouldTake(view)) held = HoldAdd(view, visible);
    if (held >= 0) HoldViews[held].asked = visible;
    HoldOriginal(*(void **)view)(view, held >= 0 ? FALSE : visible);
}

// Hooks SetVisible for one class. A class is only hooked once.
static BOOL HoldClass(void **vtable) {
    if (HoldOriginal(vtable)) return TRUE;
    if (HoldClassCount == HOLD_MAX_CLASSES) return FALSE;
    HoldClasses[HoldClassCount].vtable = vtable;
    if (!HookVtable(vtable, HoldSlot, HoldOnSetVisible, (void **)&HoldClasses[HoldClassCount].original)) return FALSE;
    HoldClassCount++;
    return TRUE;
}

// Hides a view that's already there.
static void HoldView(BYTE *view) {
    if (HoldFind(view) >= 0 || !HoldClass(*(void ***)view)) return;
    BOOL visible = view[HoldVisibleOffset] != 0;
    if (HoldAdd(view, visible) >= 0 && visible) HoldOriginal(*(void **)view)(view, FALSE);
}

// For PATCH_DISABLE, when the hooks have already been taken off.
static void HoldReleaseAll(void) {
    HoldForgetDead();
    for (int i = 0; i < HoldViewCount; i++) {
        void **vtable = *(void ***)HoldViews[i].view;
        if (HoldViews[i].asked) ((HoldSetVisible)vtable[HoldSlot])(HoldViews[i].view, TRUE);
    }
    HoldViewCount = 0;
    HoldClassCount = 0;
}
