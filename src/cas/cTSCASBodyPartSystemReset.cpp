// nTSCASCore::cTSCASBodyPartSystem (lane KA): slot 3, which empties the system's maps and stacks group by group. The
// face's `this` is the subobject at +0x1c; see cTSCASBodyPartSystem.h. The heap maps are still erased with the image's
// own erase functions (their node types are only partly known); what this file does itself is the header handling:
// a map is emptied (erase from the root, head linked to itself, size 0) or replaced (emptied, its head and header
// freed, a new header with a fresh head allocated).
#include "cas/cTSCASBodyPartSystem.h"

#include "game/original.h"
#include "game/vtable.h"

#include <new>

namespace nTSCASCore {

namespace {

using Erase = void(__thiscall *)(TreeHeader *, TreeHeader *, TreeHeader::Node *);

// Each map's erase from its root (the image's: values destroyed, nodes freed to the allocator service).
using EraseAllFn = void (*)(TreeHeader *);
void EraseAgeNames(TreeHeader *t) { OriginalFunction<Erase, 0xcdc280>()(t, t, t->mHead->mParent); }   // the per-age name maps
void EraseTree58(TreeHeader *t) { OriginalFunction<Erase, 0xcdc300>()(t, t, t->mHead->mParent); }     // +0x58
void EraseTree10(TreeHeader *t) { OriginalFunction<Erase, 0xcdb4a0>()(t, t, t->mHead->mParent); }     // +0x10
void EraseTreeC(TreeHeader *t) { OriginalFunction<Erase, 0xcdb5e0>()(t, t, t->mHead->mParent); }      // +0x0c
void EraseMaterials(TreeHeader *t) { OriginalFunction<Erase, 0xcdb540>()(t, t, t->mHead->mParent); }  // +0x2c
void EraseTree14(TreeHeader *t) { OriginalFunction<Erase, 0xcdb400>()(t, t, t->mHead->mParent); }     // +0x14
void EraseTree20(TreeHeader *t) { OriginalFunction<Erase, 0xcdb970>()(t, t, t->mHead->mParent); }     // +0x20
void EraseSnapshots(TreeHeader *t) { OriginalFunction<Erase, 0xcdb710>()(t, t, t->mHead->mParent); }  // +0x28

// The node sizes of the replaced maps (what their head allocators 0x3cff20, 0xcfca50 and 0xcfcaf0 ask for).
constexpr size_t kNode1C = 0x1c, kNode24 = 0x24, kNodeSnapshot = 0x78;

void Empty(TreeHeader *tree, EraseAllFn eraseAll)
{
    eraseAll(tree);
    TreeHeader::Node *const head = tree->mHead;
    head->mParent = head;
    head->mLeft = head;
    head->mRight = head;
    tree->mSize = 0;
}

// A heap map deleted: its nodes (none left after Empty), its head, then the header.
void Delete(TreeHeader *tree, EraseAllFn eraseAll)
{
    if (tree == nullptr) return;
    eraseAll(tree);
    GZServiceFree(tree->mHead);
    ::operator delete(tree, sizeof(TreeHeader));
}

TreeHeader *NewTree(size_t nodeBytes) { return NewHeapTree(nodeBytes); }

// Emptied, deleted, and a new one in its place.
void Replace(TreeHeader *&tree, EraseAllFn eraseAll, size_t nodeBytes)
{
    Empty(tree, eraseAll);
    Delete(tree, eraseAll);
    tree = NewTree(nodeBytes);
}

// The record destructor (0xcdf820) on each record; the vector keeps its buffer.
void DestroyRecords(BodyPartEntryVector *stack)
{
    if (stack->mFirst == stack->mLast) return;
    for (BodyPartEntry *e = stack->mFirst; e != stack->mLast; ++e) OriginalFunction<void(__thiscall *)(BodyPartEntry *), 0xcdf820>()(e);
    stack->mLast = stack->mFirst;
}

void DeleteStack(BodyPartEntryVector *stack)
{
    if (stack == nullptr) return;
    OriginalFunction<void(__thiscall *)(BodyPartEntryVector *), 0xcdf230>()(stack); // the vector's destructor
    ::operator delete(stack, sizeof(BodyPartEntryVector));
}

BodyPartEntryVector *NewStack()
{
    return new BodyPartEntryVector{nullptr, nullptr, nullptr};
}

TreeHeader *&AsTree(SnapshotMap *&map)
{
    return reinterpret_cast<TreeHeader *&>(map);
}
TreeHeader *&AsTree(PartMaterialTable *&map)
{
    return reinterpret_cast<TreeHeader *&>(map);
}

} // namespace

void InitTree(TreeHeader *tree, size_t nodeBytes)
{
    tree->mHead = nullptr;
    tree->mSize = 0;
    auto *const head = static_cast<TreeHeader::Node *>(GZServiceAllocate(nodeBytes));
    head->mLeft = head;
    head->mParent = head;
    head->mRight = head;
    uint8_t *const flags = reinterpret_cast<uint8_t *>(head) + 0xc;
    flags[0] = 1; // colour: black
    flags[1] = 1; // isnil
    tree->mHead = head;
}

TreeHeader *NewHeapTree(size_t nodeBytes)
{
    auto *const tree = static_cast<TreeHeader *>(::operator new(sizeof(TreeHeader)));
    InitTree(tree, nodeBytes);
    return tree;
}

void BodyPartSystemIface::ClearData(uint32_t flags)
{
    if (flags & 1) {
        for (TreeHeader &names : mByAge) Empty(&names, EraseAgeNames);
    }
    if (flags & 2) Empty(&mTree58, EraseTree58);
    if (flags & 8) {
        Replace(mTree10, EraseTree10, kNode1C);
        uint8_t *const self = reinterpret_cast<uint8_t *>(this);
        OriginalFunction<void(__thiscall *)(void *), 0xd0a9e0>()(self + 0x13c); // system +0x158
        OriginalFunction<void(__thiscall *)(void *), 0xcfebc0>()(self + 0x130); // system +0x14c, the locked objects
        Replace(mTreeC, EraseTreeC, kNode24);
        Replace(AsTree(mMaterials), EraseMaterials, kNode1C);
    }
    if (flags & 4) {
        Empty(mTree14, EraseTree14);
        Empty(mTree20, EraseTree20);
        Delete(mTree14, EraseTree14);
        Delete(mTree20, EraseTree20);
        mTree14 = NewTree(kNode1C);
        mTree20 = NewTree(kNode1C);
    }
    if (flags & 0x10) {
        VirtualCall<void(__thiscall *)(void *), 16>(this); // ClearStacks
        DestroyRecords(mRedoStack);
        DestroyRecords(mStack);
        Empty(AsTree(mSnapshots), EraseSnapshots);
        DeleteStack(mRedoStack);
        DeleteStack(mStack);
        Delete(AsTree(mSnapshots), EraseSnapshots);
        mRedoStack = NewStack();
        mStack = NewStack();
        AsTree(mSnapshots) = NewTree(kNodeSnapshot);
    }
}

} // namespace nTSCASCore
