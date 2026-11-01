// What Aside's objects look like, learned from the first row that paints, and questions about nodes that need it.

#include "rows.h"

#define MAX_LEARN_ATTEMPTS 64

#define SCAN_LIMIT 0x700

DWORD ParentOffset;
DWORD ChildrenOffset;
DWORD NodeOffset;
DWORD GuidOffset;
DWORD NodeParentOffset;
BOOL Learned;

static BOOL IsGuid(const char *text) {
    for (int i = 0; i < GUID_SIZE; i++) {
        char c = text[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return FALSE;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return FALSE;
        }
    }
    return TRUE;
}

// A heap std::string holding a GUID: pointer, size 36, capacity with the long-string bit.
BOOL ReadGuidAt(const BYTE *object, DWORD offset, char *guid) {
    struct {
        const char *data;
        size_t size;
        size_t capacity;
    } string;
    if (!SafeRead(object + offset, &string, sizeof string)) return FALSE;
    if (string.size != GUID_SIZE || !(string.capacity >> 63)) return FALSE;
    return SafeRead(string.data, guid, GUID_SIZE) && IsGuid(guid);
}

// Aside's lists are stored as pointer, count, capacity.
static BOOL ListContains(const BYTE *object, DWORD offset, const void *item) {
    struct {
        void **data;
        size_t count;
        size_t capacity;
    } list;
    if (!SafeRead(object + offset, &list, sizeof list)) return FALSE;
    if (!list.data || !list.count || list.count > list.capacity || list.capacity > 100000) return FALSE;
    for (size_t i = 0; i < list.count; i++) {
        void *entry;
        if (!SafeRead(list.data + i, &entry, sizeof entry)) return FALSE;
        if (entry == item) return TRUE;
    }
    return FALSE;
}

// View::parent_ points at something whose children_ contains this row.
static BOOL LearnParent(const BYTE *row) {
    for (DWORD parent = 0x100; parent < 0x300; parent += 8) {
        BYTE *candidate;
        if (!SafeRead(row + parent, &candidate, sizeof candidate) || !candidate) continue;
        for (DWORD children = 0x100; children < 0x300; children += 8) {
            if (!ListContains(candidate, children, row)) continue;
            ParentOffset = parent;
            ChildrenOffset = children;
            return TRUE;
        }
    }
    return FALSE;
}

// The row points at its BookmarkNode: the object with a GUID whose parent's children include it.
static BOOL LearnNode(const BYTE *row) {
    char guid[GUID_SIZE];
    // A row holds more than one thing of this shape once it's loaded. Its own node is stored just before its depth.
    for (DWORD at = Native.boundsOffset - 8; at < SCAN_LIMIT; at += 8) {
        DWORD node = at < Native.boundsOffset ? Native.depthOffset - 8 : at;
        BYTE *candidate;
        if (!SafeRead(row + node, &candidate, sizeof candidate) || !candidate) continue;
        for (DWORD uuid = 8; uuid < 0x200; uuid += 8) {
            if (!ReadGuidAt(candidate, uuid, guid)) continue;
            for (DWORD parent = 8; parent < uuid; parent += 8) {
                BYTE *up;
                if (!SafeRead(candidate + parent, &up, sizeof up) || !up) continue;
                if (!ListContains(up, parent + 8, candidate)) continue;
                NodeOffset = node;
                GuidOffset = uuid;
                NodeParentOffset = parent;
                return TRUE;
            }
        }
    }
    return FALSE;
}

BOOL IsInside(const BYTE *node, const BYTE *ancestor) {
    for (int depth = 0; node && depth < 64; depth++) {
        node = NodeParent(node);
        if (node == ancestor) return TRUE;
    }
    return FALSE;
}

// Which folders are open is a sorted list of IDs in the row's owner.
static BOOL IdIsOpen(const BYTE *owner, LONGLONG id) {
    const LONGLONG *ids = *(const LONGLONG *const *)(owner + Native.expandedOffset);
    size_t low = 0, count = *(const size_t *)(owner + Native.expandedOffset + 8), high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (ids[middle] < id) low = middle + 1;
        else high = middle;
    }
    return low < count && ids[low] == id;
}

BOOL FolderIsOpen(const BYTE *owner, const BYTE *node) {
    return IdIsOpen(owner, *(const LONGLONG *)(node + Native.nodeIdOffset));
}

// Whether a node's folder is open, for a node that might be gone.
BOOL NodeIsOpen(const BYTE *owner, const BYTE *node) {
    LONGLONG id;
    return !SafeRead(node + Native.nodeIdOffset, &id, sizeof id) || IdIsOpen(owner, id);
}

const BYTE *FindRowOf(const BYTE *container, const BYTE *node) {
    size_t count;
    void **children = ChildrenOf(container, &count);
    for (size_t i = 0; i < count; i++)
        if (IsRow(children[i]) && NodeOf(children[i]) == node) return children[i];
    return NULL;
}

// The outermost folded folder a row is under, or NULL. The row's node may be gone if its bookmark was deleted.
const BYTE *FoldedAbove(const BYTE *row, const BYTE *owner) {
    const BYTE *node = NodeOf(row), *folded = NULL;
    int depth = StoredDepth(row);
    for (int level = 0; level < depth && node; level++) {
        if (!SafeRead(node + NodeParentOffset, &node, sizeof node) || !node) break;
        if (!NodeIsOpen(owner, node)) folded = node;
    }
    return folded;
}

// A right click runs a callback the owner bound with itself as the receiver.
BYTE *OwnerOf(const BYTE *row) {
    const BYTE *state = *(const BYTE *const *)(row + Native.rightClickOffset);
    if (!state || *(void *const *)(state + 0x20) != Base + Native.rightClick) return NULL;
    return *(BYTE *const *)(state + 0x30);
}

// Tries again with each paint, since a row may not be in place yet.
BOOL LearnRowShape(const BYTE *row) {
    static int attempts;
    if (attempts >= MAX_LEARN_ATTEMPTS) return FALSE;
    attempts++;
    BOOL parent = LearnParent(row), node = parent && LearnNode(row);
    if (!parent || !node) {
        if (attempts == 1 || attempts == MAX_LEARN_ATTEMPTS) ModLog("couldn't learn the row's shape yet: parent %d, node %d", parent, node);
        return FALSE;
    }
    ModLog("learned: parent +%#lx, children +%#lx, node +%#lx, guid +%#lx, node parent +%#lx",
           ParentOffset, ChildrenOffset, NodeOffset, GuidOffset, NodeParentOffset);
    Learned = TRUE;
    // Aside keeps the height it measured when it built these rows, which was before any of them had their margins.
    RebuildWanted = TRUE;
    MenuInit(&Native, Base, ChildrenOffset + 8);
    return TRUE;
}
