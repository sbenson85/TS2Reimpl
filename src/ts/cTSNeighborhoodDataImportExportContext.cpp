#include "ts/cTSNeighborhoodDataImportExportContext.h"

#include "game/game.h"
#include "game/vtable.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/cRZRefCount.h"

#include <new>

namespace {

// Slots of a counted object (cIGZUnknown).
using QueryFn = uint8_t(__thiscall *)(void *, uint32_t, void **);
using RefFn = uint32_t(__thiscall *)(void *);
constexpr int kQueryInterfaceSlot = 0;
constexpr int kAddRefSlot = 1;
constexpr int kReleaseSlot = 2;

// Slots of the context's own vtables.
constexpr int kFamilyAddIdSlot = 22;
constexpr int kCopyResolvedSlot = 29;

// Stream slots this class uses (reading and writing share the numbers): 9 a 32-bit word (version and counts), 8 a 32-bit word
// (ids and values), 18 the error state.
using StreamPutFn = uint8_t(__thiscall *)(void *, uint32_t);
using StreamGetFn = uint8_t(__thiscall *)(void *, void *);
using StreamErrorFn = uint8_t(__thiscall *)(void *);
constexpr int kStreamIdSlot = 8;
constexpr int kStreamCountSlot = 9;
constexpr int kStreamErrorSlot = 18;
constexpr uint32_t kWriteVersion = 2;

__forceinline void WriteIdSet(void *stream, const NeighborIdSet &ids)
{
    VirtualCall<StreamPutFn, kStreamCountSlot>(stream, uint32_t(ids.size()));
    for (int32_t id : ids) VirtualCall<StreamPutFn, kStreamIdSlot>(stream, uint32_t(id));
}

// The count goes through `countSlot`: the first two tables write theirs as an id word (slot 8, read back as a signed count),
// the others through the count slot.
template <int countSlot> __forceinline void WriteIdMap(void *stream, const NhIdMap &map)
{
    VirtualCall<StreamPutFn, countSlot>(stream, uint32_t(map.size()));
    for (auto it = map.begin(); it != map.end(); ++it) {
        VirtualCall<StreamPutFn, kStreamIdSlot>(stream, it->first);
        VirtualCall<StreamPutFn, kStreamIdSlot>(stream, it->second);
    }
}

// Reads a count and that many ids into the set (an id already there is skipped).
void ReadIdSet(void *stream, NeighborIdSet &ids)
{
    uint32_t count = 0;
    VirtualCall<StreamGetFn, kStreamCountSlot>(stream, &count);
    for (uint32_t i = 0; i < count; ++i) {
        int32_t id = 0;
        VirtualCall<StreamGetFn, kStreamIdSlot>(stream, &id);
        if (ids.find(id) == ids.end()) ids.insert(id);
    }
}

// Reads a count and that many (key, value) pairs into the table.
void ReadIdMap(void *stream, NhIdMap &map)
{
    uint32_t count = 0;
    VirtualCall<StreamGetFn, kStreamCountSlot>(stream, &count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t key = 0, value = 0;
        VirtualCall<StreamGetFn, kStreamIdSlot>(stream, &key);
        VirtualCall<StreamGetFn, kStreamIdSlot>(stream, &value);
        map[key] = value;
    }
}

} // namespace

// ---- construction and destruction ----

cTSNeighborhoodDataImportExportContext *cTSNeighborhoodDataImportExportContext::Construct()
{
    reinterpret_cast<RZUnknownLayout *>(this)->Construct();
    mVTable = GameAddress(kVTable);
    mHoodVTable = GameAddress(kHoodVTable);
    mFamilyVTable = GameAddress(kFamilyVTable);
    mNeighborVTable = GameAddress(kNeighborVTable);
    mSerialVTable = GameAddress(kSerialVTable);
    new (&mHoodIds) NhIdMap();
    new (&mAux) NhIdMap();
    mPairKey = kNoValue;
    mPairValue = 1;
    new (&mFamilyIds) NhIdMap();
    new (&mResolved) NeighborIdSet();
    new (&mPending) NeighborIdSet();
    new (&mMapA) NhIdMap();
    new (&mMapB) NhIdMap();
    new (&mMapC) NhIdMap();
    for (auto &word : mKey.words) word = 0;
    new (&mObjects) NeighborContextList();
    mHeld = nullptr;
    mField10C = kNoValue;
    return this;
}

void *cTSNeighborhoodDataImportExportContext::Create()
{
    void *memory = ::operator new(kSize, std::nothrow);
    auto *self = memory ? static_cast<cTSNeighborhoodDataImportExportContext *>(memory)->Construct() : nullptr;
    return self ? static_cast<void *>(&self->mHoodVTable) : nullptr;
}

cTSNeighborhoodDataImportExportContext *cTSNeighborhoodDataImportExportContext::DeletingDestructor(uint32_t flags)
{
    mVTable = GameAddress(kVTable);
    mHoodVTable = GameAddress(kHoodVTable);
    mFamilyVTable = GameAddress(kFamilyVTable);
    mNeighborVTable = GameAddress(kNeighborVTable);
    mSerialVTable = GameAddress(kSerialVTable);
    if (mHeld) VirtualCall<RefFn, kReleaseSlot>(mHeld);
    mObjects.~NeighborContextList();
    mMapC.~NhIdMap();
    mMapB.~NhIdMap();
    mMapA.~NhIdMap();
    mPending.~NeighborIdSet();
    mResolved.~NeighborIdSet();
    mFamilyIds.~NhIdMap();
    mAux.~NhIdMap();
    mHoodIds.~NhIdMap();
    reinterpret_cast<cRZUnknown *>(this)->Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

uint8_t cTSNeighborhoodDataImportExportContext::QueryInterface(uint32_t id, void **out)
{
    if (!out) return 0;
    void *face;
    switch (id) {
    case 0x6db5bf2e:
    case 0x4e9c59c7:
    case 0x456b8f1d: face = &mHoodVTable; break;
    case 0xdb5c0c2: face = &mNeighborVTable; break;
    case 0x6db6f3ff: face = &mFamilyVTable; break;
    case 0xe4fda3d4: face = &mSerialVTable; break;
    default: return reinterpret_cast<RZUnknownQueryInterface *>(this)->QueryInterfaceUnknown(id, out);
    }
    *out = face;
    VirtualCall<RefFn, kAddRefSlot>(this);
    return 1;
}

// ---- the +8 face ----

void NeighborhoodContextHoodFace::GetKey(NeighborhoodResourceKey *out)
{
    *out = Owner()->mKey;
}

uint8_t NeighborhoodContextHoodFace::SetKey(const NeighborhoodResourceKey *key)
{
    Owner()->mKey = *key;
    return 1;
}

uint8_t NeighborhoodContextHoodFace::SetType(uint32_t type)
{
    Owner()->mKey.words[0] = type;
    return 1;
}

uint8_t NeighborhoodContextHoodFace::SetInstance(uint32_t instance)
{
    Owner()->mKey.words[2] = instance;
    return 1;
}

uint8_t NeighborhoodContextHoodFace::QueryHeld(uint32_t id, void **out)
{
    void *held = Owner()->mHeld;
    if (held) return VirtualSlot<QueryFn>(held, kQueryInterfaceSlot)(held, id, out);
    return 0;
}

void NeighborhoodContextHoodFace::SetHeld(void *object)
{
    auto *owner = Owner();
    void *old = owner->mHeld;
    if (object == old) return;
    if (object) VirtualCall<RefFn, kAddRefSlot>(object);
    owner->mHeld = object;
    if (old) VirtualCall<RefFn, kReleaseSlot>(old);
}

void NeighborhoodContextHoodFace::SetField10C(uint32_t value)
{
    Owner()->mField10C = value;
}

void NeighborhoodContextHoodFace::AddObject(void *object)
{
    const NeighborDataRef held(object); // counted for the call, released at the end
    Owner()->mObjects.push_back(held);
}

void NeighborhoodContextHoodFace::RemoveObject(void *object)
{
    auto &objects = Owner()->mObjects;
    for (auto it = objects.begin(); it != objects.end(); ++it) {
        if (it->Get() == object) {
            objects.erase(it);
            return;
        }
    }
}

void NeighborhoodContextHoodFace::ClearObjects()
{
    Owner()->mObjects.clear();
}

void NeighborhoodContextHoodFace::CopyObjects(ServiceWordVector *out)
{
    for (const auto &entry : Owner()->mObjects) out->push_back(uint32_t(reinterpret_cast<uintptr_t>(entry.Get())));
}

uint32_t NeighborhoodContextHoodFace::GetClassId()
{
    return cTSNeighborhoodDataImportExportContext::kClassId;
}

void NeighborhoodContextHoodFace::AddId(uint32_t id)
{
    auto &ids = Owner()->mHoodIds;
    if (ids.find(id) == ids.end()) ids[id] = cTSNeighborhoodDataImportExportContext::kNoValue;
}

void NeighborhoodContextHoodFace::CopyIds(ServiceWordVector *out)
{
    for (const auto &entry : Owner()->mHoodIds) out->push_back(entry.first);
}

uint8_t NeighborhoodContextHoodFace::SetMappedId(uint32_t id, uint32_t mapped)
{
    auto &ids = Owner()->mHoodIds;
    auto found = ids.find(id);
    if (found == ids.end()) return 0;
    found->second = mapped;
    return 1;
}

uint8_t NeighborhoodContextHoodFace::GetMappedId(uint32_t id, uint32_t *mapped)
{
    auto &ids = Owner()->mHoodIds;
    auto found = ids.find(id);
    if (found == ids.end()) return 0;
    *mapped = found->second;
    return found->second != cTSNeighborhoodDataImportExportContext::kNoValue;
}

void NeighborhoodContextHoodFace::SetPairKey(uint32_t key, uint32_t)
{
    Owner()->mPairKey = key;
}

uint8_t NeighborhoodContextHoodFace::GetPairValue(uint32_t key, uint32_t *value)
{
    auto *owner = Owner();
    if (owner->mPairKey == key) {
        *value = owner->mPairValue;
        return 1;
    }
    return 0;
}

void NeighborhoodContextHoodFace::AddAux(uint32_t id, uint32_t value)
{
    auto &aux = Owner()->mAux;
    if (aux.find(id) == aux.end()) aux[id] = value;
}

uint8_t NeighborhoodContextHoodFace::GetAux(uint32_t id, uint32_t *value)
{
    auto &aux = Owner()->mAux;
    auto found = aux.find(id);
    if (found == aux.end()) return 0;
    *value = found->second;
    return 1;
}

// ---- the +0xc face ----

void NeighborhoodContextFamilyFace::AddId(uint32_t id)
{
    auto &ids = Owner()->mFamilyIds;
    if (ids.find(id) == ids.end()) ids[id] = cTSNeighborhoodDataImportExportContext::kNoValue;
}

void NeighborhoodContextFamilyFace::CopyIds(ServiceWordVector *out)
{
    for (const auto &entry : Owner()->mFamilyIds) out->push_back(entry.first);
}

uint8_t NeighborhoodContextFamilyFace::SetMappedId(uint32_t id, uint32_t mapped)
{
    auto &ids = Owner()->mFamilyIds;
    auto found = ids.find(id);
    if (found == ids.end()) return 0;
    found->second = mapped;
    return 1;
}

uint8_t NeighborhoodContextFamilyFace::GetMappedId(uint32_t id, uint32_t *mapped)
{
    auto &ids = Owner()->mFamilyIds;
    auto found = ids.find(id);
    if (found == ids.end()) return 0;
    *mapped = found->second;
    return found->second != cTSNeighborhoodDataImportExportContext::kNoValue;
}

uint8_t NeighborhoodContextFamilyFace::QueryOwner(uint32_t id, void **out)
{
    auto *owner = Owner();
    return VirtualSlot<QueryFn>(owner, kQueryInterfaceSlot)(owner, id, out);
}

// ---- the +0x10 face ----

void NeighborhoodContextNeighborFace::AddListedId(uint32_t id)
{
    VirtualCall<void(__thiscall *)(void *, uint32_t), kFamilyAddIdSlot>(Owner()->FamilyFace(), id);
}

void NeighborhoodContextNeighborFace::AddResolved(int32_t id)
{
    auto *owner = Owner();
    owner->mResolved.insert(id);
    auto pending = owner->mPending.find(id);
    if (pending != owner->mPending.end()) owner->mPending.erase(pending);
}

void NeighborhoodContextNeighborFace::AddPending(int32_t id)
{
    auto *owner = Owner();
    if (owner->mResolved.find(id) == owner->mResolved.end()) owner->mPending.insert(id);
}

void NeighborhoodContextNeighborFace::RemoveId(int32_t id)
{
    auto *owner = Owner();
    owner->mResolved.erase(id);
    owner->mPending.erase(id);
}

uint8_t NeighborhoodContextNeighborFace::IsResolved(int32_t id)
{
    auto *owner = Owner();
    return owner->mResolved.find(id) != owner->mResolved.end();
}

uint8_t NeighborhoodContextNeighborFace::IsKnown(int32_t id)
{
    auto *owner = Owner();
    if (owner->mPending.find(id) != owner->mPending.end()) return 1;
    return owner->mResolved.find(id) != owner->mResolved.end();
}

void NeighborhoodContextNeighborFace::CopyResolved(ServiceWordVector *out)
{
    for (int32_t id : Owner()->mResolved) out->push_back(uint32_t(id));
}

void NeighborhoodContextNeighborFace::CopyAll(ServiceWordVector *out)
{
    VirtualCall<void(__thiscall *)(void *, ServiceWordVector *), kCopyResolvedSlot>(this, out);
    for (int32_t id : Owner()->mPending) out->push_back(uint32_t(id));
}

uint8_t NeighborhoodContextNeighborFace::SetMapA(int32_t id, uint32_t value)
{
    auto *owner = Owner();
    if (owner->mResolved.find(id) == owner->mResolved.end() && owner->mPending.find(id) == owner->mPending.end()) return 0;
    owner->mMapA[uint32_t(id)] = value;
    return 1;
}

uint8_t NeighborhoodContextNeighborFace::GetMapA(uint32_t key, uint32_t *value)
{
    auto &map = Owner()->mMapA;
    auto found = map.find(key);
    if (found == map.end()) return 0;
    *value = found->second;
    return 1;
}

uint32_t NeighborhoodContextNeighborFace::GetMapC(uint32_t key)
{
    auto &map = Owner()->mMapC;
    auto found = map.find(key);
    return found == map.end() ? cTSNeighborhoodDataImportExportContext::kNoValue : found->second;
}

uint8_t NeighborhoodContextNeighborFace::AddMapC(int32_t id, uint32_t value)
{
    auto *owner = Owner();
    if (owner->mPending.find(id) == owner->mPending.end() && owner->mResolved.find(id) == owner->mResolved.end()) return 0;
    if (owner->mMapC.find(uint32_t(id)) != owner->mMapC.end()) return 0;
    owner->mMapC[uint32_t(id)] = value;
    return 1;
}

uint8_t NeighborhoodContextNeighborFace::SetMapBForValue(uint32_t a, uint32_t b)
{
    auto *owner = Owner();
    for (const auto &entry : owner->mMapC) {
        if (entry.second == a) {
            owner->mMapB[a] = b;
            return 1;
        }
    }
    return 0;
}

uint8_t NeighborhoodContextNeighborFace::GetMapB(uint32_t key, uint32_t *value)
{
    auto &map = Owner()->mMapB;
    auto found = map.find(key);
    if (found == map.end()) return 0;
    *value = found->second;
    return 1;
}

// ---- the serialize face ----

uint8_t NeighborhoodContextSerialFace::Write(void *stream)
{
    auto *owner = Owner();
    VirtualCall<StreamPutFn, kStreamCountSlot>(stream, kWriteVersion);
    WriteIdMap<kStreamIdSlot>(stream, owner->mHoodIds);
    WriteIdMap<kStreamIdSlot>(stream, owner->mFamilyIds);
    WriteIdSet(stream, owner->mResolved);
    WriteIdSet(stream, owner->mPending);
    WriteIdMap<kStreamCountSlot>(stream, owner->mMapC);
    WriteIdMap<kStreamCountSlot>(stream, owner->mMapA);
    WriteIdMap<kStreamCountSlot>(stream, owner->mMapB);
    return VirtualCall<StreamErrorFn, kStreamErrorSlot>(stream) == 0;
}

uint8_t NeighborhoodContextSerialFace::Read(void *stream)
{
    auto *owner = Owner();
    owner->mHoodIds.clear();
    owner->mFamilyIds.clear();
    owner->mResolved.clear();
    owner->mPending.clear();
    owner->mMapA.clear();
    owner->mMapB.clear();
    owner->mMapC.clear();
    // A failed read leaves the locals as they were (stack words in the original): they start at zero.
    uint32_t version = 0;
    VirtualCall<StreamGetFn, kStreamCountSlot>(stream, &version);
    if (version >= 1) {
        // The first two tables take a signed count (a negative one reads nothing).
        int32_t count = 0;
        VirtualCall<StreamGetFn, kStreamIdSlot>(stream, &count);
        for (int32_t i = 0; i < count; ++i) {
            uint32_t key = 0, value = 0;
            VirtualCall<StreamGetFn, kStreamIdSlot>(stream, &key);
            VirtualCall<StreamGetFn, kStreamIdSlot>(stream, &value);
            owner->mHoodIds[key] = value;
        }
    }
    if (version >= 2) {
        int32_t count = 0;
        VirtualCall<StreamGetFn, kStreamIdSlot>(stream, &count);
        for (int32_t i = 0; i < count; ++i) {
            uint32_t key = 0, value = 0;
            VirtualCall<StreamGetFn, kStreamIdSlot>(stream, &key);
            VirtualCall<StreamGetFn, kStreamIdSlot>(stream, &value);
            owner->mFamilyIds[key] = value;
        }
        ReadIdSet(stream, owner->mResolved);
        ReadIdSet(stream, owner->mPending);
        ReadIdMap(stream, owner->mMapC);
        ReadIdMap(stream, owner->mMapA);
        ReadIdMap(stream, owner->mMapB);
    }
    return VirtualCall<StreamErrorFn, kStreamErrorSlot>(stream) == 0;
}
