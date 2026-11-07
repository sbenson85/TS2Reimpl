#include "ts/cTSNeighborDataImportExportContext.h"

#include "game/game.h"
#include "game/vtable.h"

#include <algorithm>
#include <new>

namespace {

template <typename R, typename... A> R Call(const void *object, int slot, A... args)
{
    return VirtualSlot<R(__thiscall *)(const void *, A...)>(object, slot)(object, args...);
}

// Stream slots this class uses (reading and writing share the numbers): 9 a 32-bit word (version and counts), 8 a 32-bit word
// (ids and values), 18 the error state.
constexpr int kStreamCountSlot = 9;
constexpr int kStreamIdSlot = 8;
constexpr int kStreamErrorSlot = 18;
constexpr uint32_t kWriteVersion = 2;

constexpr int kQueryInterfaceSlot = 0;
constexpr int kAddRefSlot = 1;
constexpr int kCopyResolvedSlot = 29;
constexpr int kDeletingDestructorSlot = 37;

void WriteIdSet(void *stream, const NeighborIdSet &ids)
{
    Call<uint8_t>(stream, kStreamCountSlot, uint32_t(ids.size()));
    for (int32_t id : ids) Call<uint8_t>(stream, kStreamIdSlot, uint32_t(id));
}

void WriteIdMap(void *stream, const NeighborIdMap &map)
{
    Call<uint8_t>(stream, kStreamCountSlot, uint32_t(map.size()));
    for (const auto &entry : map) {
        Call<uint8_t>(stream, kStreamIdSlot, entry.first);
        Call<uint8_t>(stream, kStreamIdSlot, entry.second);
    }
}

// Reads a count and that many ids into the set (an id already there is skipped).
void ReadIdSet(void *stream, NeighborIdSet &ids)
{
    uint32_t count = 0;
    Call<uint8_t>(stream, kStreamCountSlot, &count);
    for (uint32_t i = 0; i < count; ++i) {
        int32_t id = 0;
        Call<uint8_t>(stream, kStreamIdSlot, &id);
        if (ids.find(id) == ids.end()) ids.insert(id);
    }
}

// Reads a count and that many (key, value) pairs into the table.
void ReadIdMap(void *stream, NeighborIdMap &map)
{
    uint32_t count = 0;
    Call<uint8_t>(stream, kStreamCountSlot, &count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t key = 0, value = 0;
        Call<uint8_t>(stream, kStreamIdSlot, &key);
        Call<uint8_t>(stream, kStreamIdSlot, &value);
        map[key] = value;
    }
}

} // namespace

// ---- construction and destruction ----

cTSNeighborDataImportExportContext *cTSNeighborDataImportExportContext::Create()
{
    void *memory = ::operator new(kSize, std::nothrow);
    if (!memory) return nullptr;
    auto *self = static_cast<cTSNeighborDataImportExportContext *>(memory);
    self->mVTable = GameAddress(kVTable);
    self->mSerialVTable = GameAddress(kSerialVTable);
    self->mRefCount = 0;
    new (&self->mListedIds) ServiceWordVector();
    for (auto &word : self->mField18) word = 0;
    new (&self->mObjects) NeighborContextList();
    new (&self->mResolved) NeighborIdSet();
    new (&self->mPending) NeighborIdSet();
    new (&self->mMapA) NeighborIdMap();
    new (&self->mMapB) NeighborIdMap();
    new (&self->mMapC) NeighborIdMap();
    new (&self->mHeld) NeighborDataRef();
    self->mField1 = kNoValue;
    return self;
}

cTSNeighborDataImportExportContext *cTSNeighborDataImportExportContext::DeletingDestructor(uint32_t flags)
{
    mVTable = GameAddress(kVTable);
    mSerialVTable = GameAddress(kSerialVTable);
    mHeld.~NeighborDataRef();
    mMapC.~NeighborIdMap();
    mMapB.~NeighborIdMap();
    mMapA.~NeighborIdMap();
    mPending.~NeighborIdSet();
    mResolved.~NeighborIdSet();
    mObjects.~NeighborContextList();
    mListedIds.~ServiceWordVector();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

uint8_t cTSNeighborDataImportExportContext::QueryInterface(uint32_t id, void **out)
{
    if (!out) return 0;
    if (id == 0x456b8f1d || id == 1 || id == 0xdb5c0c2 || id == 0x6db5bf2e) {
        *out = this;
    } else if (id == 0xe4fda3d4) {
        *out = SerialFace();
    } else {
        return 0;
    }
    Call<uint32_t>(this, kAddRefSlot);
    return 1;
}

uint32_t cTSNeighborDataImportExportContext::Release()
{
    if (mRefCount > 1) return --mRefCount;
    Call<void *>(this, kDeletingDestructorSlot, uint32_t(1));
    return 0;
}

uint32_t cTSNeighborDataImportExportContext::GetClassId()
{
    return kClassId;
}

// ---- the held object ----

uint8_t cTSNeighborDataImportExportContext::QueryHeld(uint32_t id, void **out)
{
    void *held = mHeld.Get();
    if (!held) return 0;
    return Call<uint8_t>(held, kQueryInterfaceSlot, id, out);
}

void cTSNeighborDataImportExportContext::SetHeld(void *object)
{
    mHeld.Assign(object);
}

// ---- the object list ----

void cTSNeighborDataImportExportContext::AddObject(void *object)
{
    const NeighborDataRef held(object); // counted for the call, released at the end
    mObjects.push_back(held);
}

void cTSNeighborDataImportExportContext::RemoveObject(void *object)
{
    for (auto it = mObjects.begin(); it != mObjects.end(); ++it) {
        if (it->Get() == object) {
            mObjects.erase(it);
            return;
        }
    }
}

void cTSNeighborDataImportExportContext::ClearObjects()
{
    mObjects.clear();
}

void cTSNeighborDataImportExportContext::CopyObjects(ServiceWordVector *out)
{
    for (const auto &entry : mObjects) out->push_back(uint32_t(reinterpret_cast<uintptr_t>(entry.Get())));
}

// ---- the vector of ids ----

void cTSNeighborDataImportExportContext::AddListedId(uint32_t id)
{
    if (std::find(mListedIds.begin(), mListedIds.end(), id) == mListedIds.end()) mListedIds.push_back(id);
}

void cTSNeighborDataImportExportContext::CopyListedIds(ServiceWordVector *out)
{
    *out = mListedIds;
}

// ---- the id sets ----

void cTSNeighborDataImportExportContext::AddResolved(int32_t id)
{
    mResolved.insert(id);
    auto pending = mPending.find(id);
    if (pending != mPending.end()) mPending.erase(pending);
}

void cTSNeighborDataImportExportContext::AddPending(int32_t id)
{
    if (mResolved.find(id) == mResolved.end()) mPending.insert(id);
}

void cTSNeighborDataImportExportContext::RemoveId(int32_t id)
{
    mResolved.erase(id);
    mPending.erase(id);
}

uint8_t cTSNeighborDataImportExportContext::IsResolved(int32_t id)
{
    return mResolved.find(id) != mResolved.end();
}

uint8_t cTSNeighborDataImportExportContext::IsKnown(int32_t id)
{
    if (mPending.find(id) != mPending.end()) return 1;
    return mResolved.find(id) != mResolved.end();
}

void cTSNeighborDataImportExportContext::CopyResolved(ServiceWordVector *out)
{
    for (int32_t id : mResolved) out->push_back(uint32_t(id));
}

void cTSNeighborDataImportExportContext::CopyAll(ServiceWordVector *out)
{
    Call<void>(this, kCopyResolvedSlot, out);
    for (int32_t id : mPending) out->push_back(uint32_t(id));
}

// ---- the tables ----

uint8_t cTSNeighborDataImportExportContext::SetMapA(int32_t id, uint32_t value)
{
    if (!IsKnown(id)) return 0;
    mMapA[uint32_t(id)] = value;
    return 1;
}

uint8_t cTSNeighborDataImportExportContext::GetMapA(uint32_t key, uint32_t *value)
{
    auto found = mMapA.find(key);
    if (found == mMapA.end()) return 0;
    *value = found->second;
    return 1;
}

uint32_t cTSNeighborDataImportExportContext::GetMapC(uint32_t key)
{
    auto found = mMapC.find(key);
    return found == mMapC.end() ? kNoValue : found->second;
}

uint8_t cTSNeighborDataImportExportContext::AddMapC(int32_t id, uint32_t value)
{
    if (!IsKnown(id)) return 0;
    if (mMapC.find(uint32_t(id)) != mMapC.end()) return 0;
    mMapC[uint32_t(id)] = value;
    return 1;
}

uint8_t cTSNeighborDataImportExportContext::SetMapBForValue(uint32_t a, uint32_t b)
{
    for (const auto &entry : mMapC) {
        if (entry.second == a) {
            mMapB[a] = b;
            return 1;
        }
    }
    return 0;
}

uint8_t cTSNeighborDataImportExportContext::GetMapB(uint32_t key, uint32_t *value)
{
    auto found = mMapB.find(key);
    if (found == mMapB.end()) return 0;
    *value = found->second;
    return 1;
}

// ---- the serialize face ----

cTSNeighborDataImportExportContext *NeighborContextSerialFace::Owner()
{
    return reinterpret_cast<cTSNeighborDataImportExportContext *>(reinterpret_cast<uint8_t *>(this) - 4);
}

uint8_t NeighborContextSerialFace::QueryInterface(uint32_t id, void **out)
{
    return Owner()->QueryInterface(id, out);
}

uint32_t NeighborContextSerialFace::Release()
{
    return Owner()->Release();
}

uint32_t NeighborContextSerialFace::GetGZCLSID()
{
    return Owner()->GetClassId();
}

uint8_t NeighborContextSerialFace::Write(void *stream)
{
    auto *owner = Owner();
    Call<uint8_t>(stream, kStreamCountSlot, kWriteVersion);
    WriteIdSet(stream, owner->mResolved);
    WriteIdSet(stream, owner->mPending);
    WriteIdMap(stream, owner->mMapC);
    WriteIdMap(stream, owner->mMapA);
    WriteIdMap(stream, owner->mMapB);
    return Call<uint8_t>(stream, kStreamErrorSlot) == 0;
}

uint8_t NeighborContextSerialFace::Read(void *stream)
{
    auto *owner = Owner();
    owner->mResolved.clear();
    owner->mPending.clear();
    owner->mMapA.clear();
    owner->mMapB.clear();
    owner->mMapC.clear();
    // A failed read leaves the locals as they were (stack words in the original): they start at zero.
    uint32_t version = 0;
    Call<uint8_t>(stream, kStreamCountSlot, &version);
    if (version >= 1) {
        ReadIdSet(stream, owner->mResolved);
        ReadIdSet(stream, owner->mPending);
        ReadIdMap(stream, owner->mMapC);
        if (version >= 2) {
            ReadIdMap(stream, owner->mMapA);
            ReadIdMap(stream, owner->mMapB);
        }
    }
    return Call<uint8_t>(stream, kStreamErrorSlot) == 0;
}
