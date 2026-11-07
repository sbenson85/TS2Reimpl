// nTSCASCore::cTSCASBodyPartSystem (lane JY): the age, the per-age name maps, the snapshots and the morph-record purge.
#include "cas/cTSCASBodyPartSystem.h"

#include "game/original.h"
#include "game/vtable.h"

#include <cstring>

namespace nTSCASCore {

namespace {

cTSCASBodyPartSystem *SystemOf(BodyPartSystemIface *iface)
{
    return reinterpret_cast<cTSCASBodyPartSystem *>(reinterpret_cast<uint8_t *>(iface) - 0x1c);
}

const uint8_t *ZeroGuid()
{
    return OriginalFunction<const uint8_t *(__cdecl *)(), 0x4f8e50>()();
}

// 16 bytes copied like the original's movups load-then-store.
void CopyBlock16(void *dest, const void *src)
{
    uint8_t block[16];
    std::memcpy(block, src, sizeof(block));
    std::memcpy(dest, block, sizeof(block));
}

// The modifier stack's vector assign (0xcd9ab0, ret 8): `count` records from `first`.
void AssignRecords(BodyPartEntryVector *dest, const BodyPartEntryVector &src)
{
    OriginalFunction<void(__thiscall *)(BodyPartEntryVector *, BodyPartEntry *, int32_t), 0xcd9ab0>()(
        dest, src.mFirst, static_cast<int32_t>(src.mLast - src.mFirst));
}

void RefreshAgeFlags(cTSCASBodyPartSystem *system)
{
    OriginalFunction<void(__thiscall *)(void *), 0xcf8670>()(system);
}

using Erase = void(__thiscall *)(TreeHeader *, TreeHeader *, TreeHeader::Node *);

} // namespace

AgeNameTable::~AgeNameTable()
{
    OriginalFunction<Erase, 0xcdb830>()(&mTree, &mTree, mTree.mHead->mParent);
    GZServiceFree(mTree.mHead);
}

// ---- Per-age name maps (slots 6, 7) ----------------------------------------------------------------------------------

void BodyPartSystemIface::EraseAgeName(const char *name)
{
    const SurfaceName key(name, std::strlen(name));
    AgeNameMap &map = *reinterpret_cast<AgeNameMap *>(AgeMap());
    const auto it = map.find(key);
    if (it != map.end()) map.erase(it);
}

bool BodyPartSystemIface::HasAgeName(const char *name)
{
    const SurfaceName key(name, std::strlen(name));
    AgeNameMap &map = *reinterpret_cast<AgeNameMap *>(AgeMap());
    return map.find(key) != map.end();
}

// ---- Snapshots (slots 12, 13) ----------------------------------------------------------------------------------------

void BodyPartSystemIface::SaveSnapshot(int32_t id)
{
    BodyPartSnapshot &snapshot = (*mSnapshots)[id];
    snapshot.mAgeMask = OriginalFunction<uint32_t(__cdecl *)(uint32_t), 0x267350>()(mAge);
    CopyBlock16(snapshot.mBlocks[0], mBlockB8);
    CopyBlock16(snapshot.mBlocks[1], mBlockC8);
    CopyBlock16(snapshot.mBlocks[2], mBlockD8);
    if (&snapshot.mStack != mStack) AssignRecords(&snapshot.mStack, *mStack);
    CopyBlock16(snapshot.mKeys[0], mKeys[0]);
    CopyBlock16(snapshot.mKeys[1], mKeys[1]);
    snapshot.mDword108 = mDword108;
}

bool BodyPartSystemIface::RestoreSnapshot(int32_t id)
{
    const auto it = mSnapshots->find(id);
    if (it == mSnapshots->end()) return false;
    BodyPartSnapshot &snapshot = it->second;
    cTSCASBodyPartSystem *const system = SystemOf(this);
    OriginalFunction<void(__thiscall *)(void *), 0xce9530>()(system);
    OriginalFunction<void(__thiscall *)(void *), 0xcebb70>()(system);
    VirtualCall<void(__thiscall *)(void *, uint32_t), 71>(this, snapshot.mAgeMask);
    VirtualCall<void(__thiscall *)(void *, const void *), 54>(this, snapshot.mBlocks[0]);
    VirtualCall<void(__thiscall *)(void *, const void *), 74>(this, snapshot.mBlocks[1]);
    VirtualCall<void(__thiscall *)(void *, const void *), 56>(this, snapshot.mBlocks[2]);
    if (mStack != &snapshot.mStack) AssignRecords(mStack, snapshot.mStack);
    CopyBlock16(mKeys[0], snapshot.mKeys[0]);
    CopyBlock16(mKeys[1], snapshot.mKeys[1]);
    mDword108 = snapshot.mDword108;
    RefreshAgeFlags(system);
    return true;
}

// ---- The age (slot 71) -----------------------------------------------------------------------------------------------

void BodyPartSystemIface::SetAge(uint32_t code)
{
    const uint32_t age = OriginalFunction<uint32_t(__cdecl *)(uint32_t), 0xb5b700>()(code);
    if (age == mAge) return;
    mAge = age;
    CopyBlock16(mBlockD8, ZeroGuid());
    cTSCASBodyPartSystem *const system = SystemOf(this);
    system->ResetKeys();
    RefreshAgeFlags(system);
    VirtualCall<void(__thiscall *)(void *), 10>(this);
    VirtualCall<void(__thiscall *)(void *), 9>(this);
    mByte10C = 0;
    TreeHeader *const resources = &mResources;
    TreeHeader::Node *const head = resources->mHead;
    OriginalFunction<Erase, 0xcdbc10>()(resources, resources, head->mParent);
    head->mParent = head;
    head->mLeft = head;
    head->mRight = head;
    resources->mSize = 0;
    system->InitCategoryLists();
    system->InitTargetSurfaceNames();
}

// ---- Face-0 slot 13 --------------------------------------------------------------------------------------------------

void cTSCASBodyPartSystem::RemoveMorphRecords()
{
    BodyPartEntryVector *stack = mStack;
    BodyPartEntry *it = stack->mFirst;
    while (it != stack->mLast) {
        const int32_t kind = it->mKind;
        if (kind == 7 || kind == 2 || kind == 1 || kind == 0) {
            BodyPartEntry *const last = stack->mLast;
            for (BodyPartEntry *next = it + 1; next != last; ++next)
                OriginalFunction<void(__thiscall *)(BodyPartEntry *, const BodyPartEntry *), 0xce01a0>()(next - 1, next);
            OriginalFunction<void(__thiscall *)(BodyPartEntry *), 0xcdf820>()(stack->mLast - 1);
            stack->mLast = stack->mLast - 1;
        } else {
            ++it;
        }
        stack = mStack;
    }
    OriginalFunction<void(__thiscall *)(void *), 0xcebb70>()(this);
}

} // namespace nTSCASCore
