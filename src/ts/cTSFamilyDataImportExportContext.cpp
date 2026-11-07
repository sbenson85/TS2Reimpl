#include "ts/cTSFamilyDataImportExportContext.h"

#include "game/game.h"
#include "game/vtable.h"

#include <algorithm>
#include <new>

namespace {

template <typename R, typename... A> R Call(const void *object, int slot, A... args)
{
    return VirtualSlot<R(__thiscall *)(const void *, A...)>(object, slot)(object, args...);
}

// Stream slots this class uses (reading and writing share the numbers): 9 a 32-bit word (the version), 8 a 32-bit word
// (counts and ids), 18 the error state.
constexpr int kStreamWordSlot = 9;
constexpr int kStreamIdSlot = 8;
constexpr int kStreamErrorSlot = 18;
constexpr uint32_t kWriteVersion = 2;
constexpr uint32_t kDestinationInterface = 0xdb5c0c2;

constexpr int kQueryInterfaceSlot = 0;
constexpr int kAddRefSlot = 1;
constexpr int kDeletingDestructorSlot = 28;

} // namespace

// ---- construction and destruction ----

cTSFamilyDataImportExportContext *cTSFamilyDataImportExportContext::Create()
{
    void *memory = ::operator new(kSize, std::nothrow);
    if (!memory) return nullptr;
    auto *self = static_cast<cTSFamilyDataImportExportContext *>(memory);
    self->mVTable = GameAddress(kVTable);
    self->mSerialVTable = GameAddress(kSerialVTable);
    self->mRefCount = 0;
    new (&self->mIdMap) FamilyIdMap();
    for (auto &word : self->mKey) word = 0;
    new (&self->mObjects) FamilyContextList();
    new (&self->mSource) NeighborDataRef();
    new (&self->mDestination) NeighborDataRef();
    self->mField4C = kNoMapping;
    return self;
}

cTSFamilyDataImportExportContext *cTSFamilyDataImportExportContext::DeletingDestructor(uint32_t flags)
{
    mVTable = GameAddress(kVTable);
    mSerialVTable = GameAddress(kSerialVTable);
    mDestination.~NeighborDataRef();
    mSource.~NeighborDataRef();
    mObjects.~FamilyContextList();
    mIdMap.~FamilyIdMap();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

uint8_t cTSFamilyDataImportExportContext::QueryInterface(uint32_t id, void **out)
{
    if (!out) return 0;
    if (id == 0x6db5bf2e || id == 1 || id == 0x456b8f1d || id == 0x6db6f3ff) {
        *out = this;
    } else if (id == 0xe4fda3d4) {
        *out = SerialFace();
    } else {
        return 0;
    }
    Call<uint32_t>(this, kAddRefSlot);
    return 1;
}

uint32_t cTSFamilyDataImportExportContext::Release()
{
    if (mRefCount > 1) return --mRefCount;
    Call<void *>(this, kDeletingDestructorSlot, uint32_t(1));
    return 0;
}

// ---- forwarding to the held objects ----

uint8_t cTSFamilyDataImportExportContext::QuerySource(uint32_t id, void **out)
{
    void *source = mSource.Get();
    if (!source) return 0;
    return Call<uint8_t>(source, kQueryInterfaceSlot, id, out);
}

uint8_t cTSFamilyDataImportExportContext::QueryDestination(uint32_t id, void **out)
{
    void *destination = mDestination.Get();
    if (!destination) return 0;
    return Call<uint8_t>(destination, kQueryInterfaceSlot, id, out);
}

void cTSFamilyDataImportExportContext::SetSource(void *object)
{
    mSource.Assign(object);
}

void cTSFamilyDataImportExportContext::SetDestinationFrom(void *object)
{
    if (!object) return;
    mDestination.Assign(nullptr);
    Call<uint8_t>(object, kQueryInterfaceSlot, kDestinationInterface, reinterpret_cast<void **>(&mDestination));
}

uint32_t cTSFamilyDataImportExportContext::GetClassId()
{
    return kClassId;
}

// ---- the object list ----

void cTSFamilyDataImportExportContext::AddObject(void *object)
{
    const NeighborDataRef held(object); // counted for the call, released at the end
    mObjects.push_back(held);
}

void cTSFamilyDataImportExportContext::RemoveObject(void *object)
{
    for (auto it = mObjects.begin(); it != mObjects.end(); ++it) {
        if (it->Get() == object) {
            mObjects.erase(it);
            return;
        }
    }
}

void cTSFamilyDataImportExportContext::ClearObjects()
{
    mObjects.clear();
}

void cTSFamilyDataImportExportContext::CopyObjects(ServiceWordVector *out)
{
    for (const auto &entry : mObjects) out->push_back(uint32_t(reinterpret_cast<uintptr_t>(entry.Get())));
}

// ---- the id table ----

void cTSFamilyDataImportExportContext::AddId(uint32_t id)
{
    if (mIdMap.find(id) == mIdMap.end()) mIdMap[id] = kNoMapping;
}

void cTSFamilyDataImportExportContext::CopyIds(ServiceWordVector *out)
{
    for (const auto &entry : mIdMap) out->push_back(entry.first);
}

uint8_t cTSFamilyDataImportExportContext::SetMappedId(uint32_t id, uint32_t mapped)
{
    auto found = mIdMap.find(id);
    if (found == mIdMap.end()) return 0;
    found->second = mapped;
    return 1;
}

uint8_t cTSFamilyDataImportExportContext::GetMappedId(uint32_t id, uint32_t *mapped)
{
    auto found = mIdMap.find(id);
    if (found == mIdMap.end()) return 0;
    *mapped = found->second;
    return found->second != kNoMapping;
}

// ---- the serialize face ----

cTSFamilyDataImportExportContext *FamilyContextSerialFace::Owner()
{
    return reinterpret_cast<cTSFamilyDataImportExportContext *>(reinterpret_cast<uint8_t *>(this) - 4);
}

uint8_t FamilyContextSerialFace::QueryInterface(uint32_t id, void **out)
{
    return Owner()->QueryInterface(id, out);
}

uint32_t FamilyContextSerialFace::Release()
{
    return Owner()->Release();
}

uint32_t FamilyContextSerialFace::GetGZCLSID()
{
    return Owner()->GetClassId();
}

uint8_t FamilyContextSerialFace::Write(void *stream)
{
    auto *owner = Owner();
    Call<uint8_t>(stream, kStreamWordSlot, kWriteVersion);
    Call<uint8_t>(stream, kStreamIdSlot, uint32_t(owner->mIdMap.size()));
    for (const auto &entry : owner->mIdMap) {
        Call<uint8_t>(stream, kStreamIdSlot, entry.first);
        Call<uint8_t>(stream, kStreamIdSlot, entry.second);
    }
    return Call<uint8_t>(stream, kStreamErrorSlot) == 0;
}

uint8_t FamilyContextSerialFace::Read(void *stream)
{
    auto *owner = Owner();
    // Failed reads leave the locals as they were (stack words in the original): they start at zero.
    uint32_t version = 0;
    Call<uint8_t>(stream, kStreamWordSlot, &version);
    if (version >= 2) {
        int32_t count = 0;
        Call<uint8_t>(stream, kStreamIdSlot, &count);
        uint32_t id = 0, mapped = 0;
        for (int32_t i = 0; i < count; ++i) {
            Call<uint8_t>(stream, kStreamIdSlot, &id);
            Call<uint8_t>(stream, kStreamIdSlot, &mapped);
            owner->mIdMap[id] = mapped;
        }
    } else if (version >= 1) {
        uint32_t id = 0;
        Call<uint8_t>(stream, kStreamIdSlot, &id);
        owner->mIdMap[id] = cTSFamilyDataImportExportContext::kNoMapping;
    }
    return Call<uint8_t>(stream, kStreamErrorSlot) == 0;
}
