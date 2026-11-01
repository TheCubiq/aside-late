#pragma once

#include "aside-late.h"

#include <stdio.h>
#include <wchar.h>

#define PATH_SIZE (MAX_PATH * 2)
#define ID_SIZE 64

extern wchar_t Home[MAX_PATH];
extern HMODULE ThisModule;

void HomePathW(wchar_t *out, const wchar_t *name);
BOOL WriteWholeFile(const wchar_t *path, const void *data, size_t size);
// The whole file with a NUL after it, for free(), or NULL.
char *ReadWholeFile(const wchar_t *path, size_t *size);

// Files the page reads while they change. Aside can have a file open at any moment, and then it can't
// be replaced, so each version is a new file and the page's path leads to the newest.
void PublishStart(void);
void PublishPath(wchar_t *out);
void PublishCommit(const wchar_t *name, const wchar_t *path);
void PublishFile(const wchar_t *name, const void *data, size_t size);
BOOL PublishedPath(const wchar_t *name, wchar_t *out);

BOOL LookupInit(Lookup *lookup, BYTE *base, ULONGLONG pointerBase);
void LookupFree(Lookup *lookup);
// A clean copy of a DLL read from disk, indexed for lookups. BuildLookup is for the Aside.dll that's loaded.
BOOL LookupFromFile(const wchar_t *path, Lookup *lookup, BYTE **copy);
void LookupFromFileFree(Lookup *lookup, BYTE *copy);

void HooksInit(void);
// Takes off everything a module hooked or edited.
void UndoHooksOf(HMODULE owner);

// patches.c
void PatchesStart(BYTE *base);
void PatchesClean(void);
void QueueSet(const char *id, BOOL enabled);
void QueueInstall(const char *repo, const char *id, const char *sha);
void QueueRemove(const char *id);
void SavePatchList(void);
// Finds the patch's folder and loads it into the list after an install. Safe to call on an update.
BOOL AddInstalledPatch(const char *id);
void SetPatchProgress(const char *id, const char *text);

// install.c
BOOL InstallPatch(const char *repo, const char *id, const char *sha, char *error, size_t errorSize);
void CleanOldBuilds(const char *id, const char *keepSha);

// settings.c
void SettingsStart(BYTE *base);
