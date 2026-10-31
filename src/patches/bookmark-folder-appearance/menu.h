// A folder's right-click menu, assembled from the same controls as Aside's tab group menu.
#pragma once

#include "resolve.h"

#define COMMAND_RENAME 1
#define COMMAND_DELETE 2
#define COMMAND_MANAGE 3

typedef struct {
    int color;                 // Native color ID, or -1.
    const IconEntry *icon;     // Or NULL.
    void (*pickColor)(int color);
    void (*pickIcon)(const char *key, size_t size);
    void (*reset)(void);
} MenuChoices;

void MenuInit(const Bindings *bindings, BYTE *base, DWORD childCountOffset);
// Shows the menu for a row, at a point in screen coordinates. The owner receives its commands.
void MenuOpen(BYTE *row, BYTE *owner, const BYTE *node, POINT screen, const MenuChoices *choices);
// Finishes what can't be done while a closing widget is still on the stack.
void MenuTick(void);
