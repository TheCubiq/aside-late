// What's been chosen for each folder, kept by GUID in folders.tsv.

#include <stdio.h>
#include "rows.h"

#define MAX_FOLDERS 1024

static Folder Folders[MAX_FOLDERS];
int FolderCount;
static char StorePath[MAX_PATH];

// Folders store Aside's group colors, the same values its menu's buttons use.
static BOOL IsGroupColor(int color) {
    return color >= 0 && color < GROUP_COLOR_COUNT && Native.groupBackgrounds[color];
}

int FindIcon(const char *key, size_t size) {
    for (int i = 0; i < Native.iconCount; i++)
        if (Icons[i].keySize == size && !memcmp(Icons[i].key, key, size)) return i;
    return -1;
}

// Only once. The icon table holds pointers, and they only mean something in the loaded DLL, so this waits for that.
void LoadFolders(void) {
    if (StorePath[0]) return;
    HomeFile("folders.tsv", StorePath, MAX_PATH);
    FILE *file = fopen(StorePath, "r");
    if (!file) return;
    char line[128];
    while (FolderCount < MAX_FOLDERS && fgets(line, sizeof line, file)) {
        int color;
        if (strlen(line) < GUID_SIZE + 2 || line[GUID_SIZE] != '\t' || sscanf(line + GUID_SIZE + 1, "%d", &color) != 1) continue;
        if (color != -1 && !IsGroupColor(color)) continue;
        const char *key = strchr(line + GUID_SIZE + 1, '\t');
        memcpy(Folders[FolderCount].guid, line, GUID_SIZE);
        Folders[FolderCount].color = color;
        Folders[FolderCount++].icon = key ? FindIcon(key + 1, strcspn(key + 1, "\t\r\n")) : -1;
    }
    fclose(file);
}

static void SaveFolders(void) {
    FILE *file = fopen(StorePath, "w");
    if (!file) return;
    for (int i = 0; i < FolderCount; i++) {
        const Folder *folder = &Folders[i];
        fprintf(file, "%.36s\t%d\t", folder->guid, folder->color);
        if (folder->icon >= 0) fwrite(Icons[folder->icon].key, 1, Icons[folder->icon].keySize, file);
        fputc('\n', file);
    }
    fclose(file);
}

const Folder *FindFolder(const char *guid) {
    for (int i = 0; i < FolderCount; i++)
        if (!memcmp(Folders[i].guid, guid, GUID_SIZE)) return &Folders[i];
    return NULL;
}

int FindColor(const char *guid) {
    const Folder *folder = FindFolder(guid);
    return folder ? folder->color : -1;
}

void SetFolder(const char *guid, int color, int icon) {
    Folder *folder = (Folder *)FindFolder(guid);
    if (color < 0 && icon < 0) {
        if (folder) *folder = Folders[--FolderCount];
    } else {
        if (!folder) {
            if (FolderCount == MAX_FOLDERS) return;
            folder = &Folders[FolderCount++];
            memcpy(folder->guid, guid, GUID_SIZE);
        }
        folder->color = color;
        folder->icon = icon;
    }
    SaveFolders();
}
