#include "ts/cTSFamily.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/FamilyInStream.h"
#include "ifaces/FamilyOutStream.h"
#include "ifaces/FamilyStringReader.h"
#include "ifaces/cITSFamily.h"
#include "misc/UtilitiesC.h"
#include "rz/StaticGetters.h"
#include "simantics/ServiceRefVector.h"

#include <cstring>
#include <new>

namespace {
constexpr uint32_t kUnknownInterface = 1;
constexpr uint32_t kFamilyInterfaceA = 0x7ab5bcbb;  // the family's own interface ids
constexpr uint32_t kSerializableInterface = 0xe4fda3d4;
constexpr uint32_t kPersistResourceInterface = 0x456b8f1d; // answered with the face at +4
constexpr uint32_t kStringReaderInterface = 0x3b2271c0;    // asked of an old-format stream for the family name
constexpr uint32_t kMemberObjectInterface = 0x0c560f33;    // what the group accessors ask a member object for

// A member object: a cIGZUnknown-style object whose slot 21 (0x54) gives its 16-byte resource key. Only the key getter is
// named here; the other member slots stay raw until a lane understands them.
using GetKeyFn = const FamilyKey *(__thiscall *)(void *);
const FamilyKey *KeyOf(void *object) { return VirtualCall<GetKeyFn, 0x54 / 4>(object); }

// A counted pointer held by a local of the original: released when it goes out of scope.
struct Held {
    void *object = nullptr;
    Held() = default;
    Held(const Held &) = delete;
    Held &operator=(const Held &) = delete;
    ~Held() noexcept(false) { if (object) As<cIGZUnknown>(object)->Release(); }
};

void ClearText(cRZString &text)
{
    text.mString.clear();
}
} // namespace

// ---- construction ----

cTSFamily::cTSFamily()
{
    mRefCount = 0;
    mField0C = 1;
    mIdCount = 0;
    mName.Construct();
    mDescription.Construct();
    mFamilyId = 0;
    mHomeLot = 0;
    mVacationLot = 0;
    mBusinessLot = 0;
    mUniqueId = 0;
    mField68 = 0xffffffff;
    mFunds = 0;
    mField70 = 0;
    mModified = 1;
    mGroup1Dirty = 0;
    mGroup1Loaded = 0;
    mGroup2Dirty = 0;
    mGroup2Loaded = 0;
    mFlag79 = 1;
    new (&mMemberIds) std::vector<uint32_t>();
    mFlags = 0;
    mRecordKey[0] = mRecordKey[1] = mRecordKey[2] = mRecordKey[3] = 0;
    new (&mIdSet) FamilyIdSet();
    mRecord = nullptr;
    new (&mGroup1First) FamilyObjectMap();
    new (&mGroup1Second) FamilyObjectMap();
    new (&mGroup2First) FamilyObjectMap();
    new (&mGroup2Second) FamilyObjectMap();
}

cTSFamily *cTSFamily::Create() { return new (std::nothrow) cTSFamily; }

cTSFamily::~cTSFamily()
{
    if (void *record = mRecord) {
        mRecord = nullptr;
        As<cIGZUnknown>(record)->Release();
    }
    mGroup2Second.~FamilyObjectMap();
    mGroup2First.~FamilyObjectMap();
    mGroup1Second.~FamilyObjectMap();
    mGroup1First.~FamilyObjectMap();
    mIdSet.~FamilyIdSet();
    mMemberIds.~vector();
    mDescription.Destruct();
    mName.Destruct();
}

// ---- cIGZUnknown / cIGZSerializable ----

bool cTSFamily::QueryInterface(uint32_t id, void **out)
{
    if (id == kFamilyInterfaceA || id == kUnknownInterface || id == kSerializableInterface)
        *out = static_cast<cITSFamily *>(this);
    else if (id == kPersistResourceInterface)
        *out = static_cast<cIGZPersistResource *>(this);
    else
        return false;
    AddRef();
    return true;
}

uint32_t cTSFamily::AddRef() { return ++mRefCount; }

uint32_t cTSFamily::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete this;
    return 0;
}

uint32_t cTSFamily::GetGZCLSID() { return kClassId; }

// ---- the folded accessors and the persist face ----

cIGZPersistResource *cTSFamily::GetPersistFace() { return static_cast<cIGZPersistResource *>(this); }

cGZPersistResourceKey cTSFamily::GetKey()
{
    cGZPersistResourceKey key;
    std::memcpy(&key, mRecordKey, sizeof(key));
    return key;
}

bool cTSFamily::SetKey(const cGZPersistResourceKey &key)
{
    std::memcpy(mRecordKey, &key, sizeof(mRecordKey));
    return true;
}

bool cTSFamily::Write(ifaces::cIGZOStream *stream)
{
    auto *out = As<ifaces::FamilyOutStream>(stream);
    auto *metadata = static_cast<UtilObjectB *>(::operator new(sizeof(UtilObjectB)));
    if (metadata) metadata->Construct(kVersion);
    out->SetMetadata(metadata);
    out->SetUint32(GetGZCLSID());
    out->SetUint32(kVersion);
    out->SetUint32(0);
    out->SetSint32(mHomeLot);
    out->SetSint32(mBusinessLot);
    out->SetSint32(mVacationLot);
    out->SetSint32(mField68);
    out->SetSint32(uint32_t(GetFunds()));
    out->SetSint32(mIdCount);
    out->SetSint32(mFlags);
    const uint32_t count = uint32_t(mMemberIds.size());
    out->SetUint32(count);
    for (uint32_t i = 0; i < count; ++i) {
        out->SetSint32(mMemberIds[i]);
        out->GetError();
    }
    out->SetUint32(mUniqueId);
    out->SetUint32(mField0C);
    out->SetSint32(mField70);
    return out->GetError() == 0;
}

bool cTSFamily::Read(ifaces::cIGZIStream *stream)
{
    auto *in = As<ifaces::FamilyInStream>(stream);
    // The three header words and the scratch values are left unwritten by a failing read (stack bytes in the original);
    // they start at zero here and the failing reads leave them alone.
    uint32_t classId = 0, version = 0, spare = 0, count = 0;
    in->GetUint32(&classId);
    in->GetUint32(&version);
    in->GetUint32(&spare);
    auto *metadata = static_cast<UtilObjectB *>(::operator new(sizeof(UtilObjectB)));
    if (metadata) metadata->Construct(version);
    in->SetMetadata(metadata);
    in->GetSint32(&mHomeLot);
    if (version < 0x50) mBusinessLot = mHomeLot;
    else in->GetSint32(&mBusinessLot);
    if (version < 0x55) mVacationLot = 0;
    else in->GetSint32(&mVacationLot);
    if (version < 5) {
        Held reader;
        if (in->QueryInterface(kStringReaderInterface, &reader.object))
            As<ifaces::FamilyStringReader>(reader.object)->ReadStringInto(&mName);
    }
    if (version != 0) in->GetSint32(&mField68);
    if (version > 1) {
        in->GetSint32(&mFunds);
        if (version < 0x4d) {
            uint32_t discarded = 0;
            in->GetSint32(&discarded);
        }
        in->GetSint32(&mIdCount);
    }
    if (version < 7) {
        if (version < 3) {
            SetFlagBit0(1);
        } else {
            uint8_t flag = 0;
            in->GetUint8(&flag);
            SetFlagBit0(flag != 0);
        }
        if (version > 3) {
            uint8_t flag = 0;
            in->GetUint8(&flag);
            SetFlagBit1(flag != 0);
        }
        if (version > 5) {
            // ORIGINAL-BUG: (probably) the version 6 byte goes to bit 1 again, replacing the version 4 byte.
            uint8_t flag = 0;
            in->GetUint8(&flag);
            SetFlagBit1(flag != 0);
        }
    } else {
        in->GetSint32(&mFlags);
    }
    in->GetUint32(&count);
    mMemberIds.clear();
    mMemberIds.insert(mMemberIds.begin(), count, 0u);
    for (uint32_t i = 0; i < count; ++i) {
        in->GetSint32(&mMemberIds[i]);
        in->GetError();
    }
    if (version > 0x4a) in->GetUint32(&mUniqueId);
    if (version > 0x4e) in->GetUint32(&mField0C);
    if (version < 0x51) mField70 = uint32_t(mFunds);
    else in->GetSint32(&mField70);
    return in->GetError() == 0;
}

uint8_t cTSFamily::SetFamilyId(uint32_t id)
{
    mFamilyId = id;
    ClearText(mName);
    ClearText(mDescription);
    mModified = 1;
    return 1;
}

void cTSFamily::AddMemberId(uint32_t id)
{
    for (uint32_t member : mMemberIds)
        if (member == id) return;
    mMemberIds.push_back(id);
}

void cTSFamily::RemoveMemberId(uint32_t id)
{
    for (auto it = mMemberIds.begin(); it != mMemberIds.end(); ++it) {
        if (*it == id) {
            mMemberIds.erase(it);
            return;
        }
    }
}

uint8_t cTSFamily::HasObjectInGroup1(const FamilyKey *key)
{
    if (!mGroup1Loaded) LoadFirstGroup();
    return mGroup1First.find(*key) != mGroup1First.end();
}

uint8_t cTSFamily::HasObjectInGroup2(const FamilyKey *key)
{
    if (!mGroup2Loaded) LoadSecondGroup();
    return mGroup2First.find(*key) != mGroup2First.end();
}

namespace {
// The body shared by AddObjectToGroup1/2 (they differ only in the group's fields).
void AddObject(cTSFamily &family, FamilyObjectMap &first, FamilyObjectMap &second, uint8_t &dirty, void *object)
{
    (void)family;
    Held local;
    if (!object) return;
    if (As<cIGZUnknown>(object)->QueryInterface(kMemberObjectInterface, &local.object)) {
        auto found = first.find(*KeyOf(local.object));
        if (found != first.end()) first.erase(found);
        dirty = 1;
        NeighborDataRef &slot = second[*KeyOf(local.object)];
        slot.Assign(local.object);
    }
}
} // namespace

void cTSFamily::AddObjectToGroup1(void *object)
{
    if (!mGroup1Loaded) LoadFirstGroup();
    AddObject(*this, mGroup1First, mGroup1Second, mGroup1Dirty, object);
}

void cTSFamily::AddObjectToGroup2(void *object)
{
    if (!mGroup2Loaded) LoadSecondGroup();
    AddObject(*this, mGroup2First, mGroup2Second, mGroup2Dirty, object);
}

void cTSFamily::CollectObjectsOfGroup1(ServiceRefVector *out)
{
    if (!mGroup1Loaded) LoadFirstGroup();
    for (auto &entry : mGroup1First) {
        void *object = entry.second.Get();
        // A counted local, then the vector's own count (growing the vector counts the copy again).
        if (object) As<cIGZUnknown>(object)->AddRef();
        if (out->mLast != out->mEnd) {
            *out->mLast = object;
            if (object) As<cIGZUnknown>(object)->AddRef();
            ++out->mLast;
        } else {
            void *const copy = object;
            out->InsertGrown(out->mLast, &copy);
        }
        if (object) As<cIGZUnknown>(object)->Release();
    }
}

uint8_t cTSFamily::GetFlag79() { return mFlag79; }
void cTSFamily::SetFlag79(uint8_t value) { mFlag79 = value; }

uint32_t *cTSFamily::GetMemberIdSlot(int32_t index)
{
    if (index < 0 || uint32_t(index) >= mMemberIds.size()) return nullptr;
    return &mMemberIds[uint32_t(index)];
}

uint32_t cTSFamily::GetMemberId(int32_t index)
{
    if (index < 0 || uint32_t(index) >= mMemberIds.size()) return 0;
    return mMemberIds[uint32_t(index)];
}

uint32_t *cTSFamily::FindMemberId(uint32_t id)
{
    for (auto it = mMemberIds.begin(); it != mMemberIds.end(); ++it)
        if (*it == id) return &*it;
    return nullptr;
}

uint8_t cTSFamily::HasMemberId(uint32_t id)
{
    for (uint32_t member : mMemberIds)
        if (member == id) return 1;
    return 0;
}

void cTSFamily::SetName(const void *text)
{
    VirtualCall<void(__thiscall *)(void *, const void *), 17>(&mName, text);
    mModified = 1;
}

void cTSFamily::SetDescription(const void *text)
{
    VirtualCall<void(__thiscall *)(void *, const void *), 17>(&mDescription, text);
    mModified = 1;
}

void cTSFamily::AddToIdSet(uint32_t id)
{
    mIdSet.insert(id);
    mIdCount = uint32_t(mIdSet.size());
}

void cTSFamily::ClearIdSet()
{
    mIdSet.clear();
    mIdCount = 0;
}

uint8_t cTSFamily::IsFamilyLot(uint32_t lot)
{
    return lot && (lot == mHomeLot || lot == mVacationLot);
}

uint32_t cTSFamily::GetUniqueId(uint8_t create)
{
    if (mUniqueId == 0 && create) {
        void *framework = GetDirectorFrameworkObject();
        VirtualCall<void(__thiscall *)(void *, uint32_t *), 7>(framework, &mUniqueId);
    }
    return mUniqueId;
}

void cTSFamily::SetFlagBit0(uint8_t on) { mFlags &= ~1u; if (on) mFlags |= 1u; }
uint8_t cTSFamily::GetFlagBit0() { return mFlags & 1; }
void cTSFamily::SetFlagBit3(uint8_t on) { mFlags &= ~8u; if (on) mFlags |= 8u; }
uint8_t cTSFamily::GetFlagBit3() { return (mFlags >> 3) & 1; }
void cTSFamily::SetFlagBit1(uint8_t on) { mFlags &= ~2u; if (on) mFlags |= 2u; }
uint8_t cTSFamily::GetFlagBit1() { return (mFlags >> 1) & 1; }
void cTSFamily::SetFlagBit2(uint8_t on) { mFlags &= ~4u; if (on) mFlags |= 4u; }
uint8_t cTSFamily::GetFlagBit2() { return (mFlags >> 2) & 1; }

// ---- adapters (the RVA hooks) ----

using Adapters = cTSFamilyAdapters;

// 0x49b340: the constructor on the game's memory.
Adapters::Self *__fastcall Adapters::Construct(void *memory, void *) { return ::new (memory) Self; }

// 0x49b690: the destructor (its only game caller is the deleting destructor 0x49ba50, itself hooked).
void __fastcall Adapters::Destruct(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->Self::~Self();
}

// 0x49ba50: the destructor and, with bit 0, the sized delete (the compiler's scalar deleting destructor, by hand). As the
// original, it calls the destructor out of line: through the Destruct adapter (0x49b690's hook; a __fastcall (self, edx)
// function called as __thiscall, the same ABI without loading the dummy edx), which keeps its byte match.
Adapters::Self *__fastcall Adapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    reinterpret_cast<void(__thiscall *)(Self *)>(&Adapters::Destruct)(self);
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}

bool __fastcall Adapters::QueryInterface(Self *self, void *, uint32_t id, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::QueryInterface(id, out);
}

uint32_t __fastcall Adapters::Release(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::Release();
}

uint32_t __fastcall Adapters::GetGZCLSID(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetGZCLSID();
}

bool __fastcall Adapters::Write(Self *self, void *, ifaces::cIGZOStream *stream)
{
    [[msvc::forceinline_calls]] return self->Self::Write(stream);
}

bool __fastcall Adapters::Read(Self *self, void *, ifaces::cIGZIStream *stream)
{
    [[msvc::forceinline_calls]] return self->Self::Read(stream);
}

uint8_t __fastcall Adapters::SetFamilyId(Self *self, void *, uint32_t id)
{
    [[msvc::forceinline_calls]] return self->Self::SetFamilyId(id);
}

void __fastcall Adapters::AddMemberId(Self *self, void *, uint32_t id)
{
    [[msvc::forceinline_calls]] return self->Self::AddMemberId(id);
}

void __fastcall Adapters::RemoveMemberId(Self *self, void *, uint32_t id)
{
    [[msvc::forceinline_calls]] return self->Self::RemoveMemberId(id);
}

uint8_t __fastcall Adapters::HasObjectInGroup1(Self *self, void *, const FamilyKey *key)
{
    [[msvc::forceinline_calls]] return self->Self::HasObjectInGroup1(key);
}

uint8_t __fastcall Adapters::HasObjectInGroup2(Self *self, void *, const FamilyKey *key)
{
    [[msvc::forceinline_calls]] return self->Self::HasObjectInGroup2(key);
}

void __fastcall Adapters::AddObjectToGroup1(Self *self, void *, void *object)
{
    [[msvc::forceinline_calls]] return self->Self::AddObjectToGroup1(object);
}

void __fastcall Adapters::AddObjectToGroup2(Self *self, void *, void *object)
{
    [[msvc::forceinline_calls]] return self->Self::AddObjectToGroup2(object);
}

void __fastcall Adapters::CollectObjectsOfGroup1(Self *self, void *, ServiceRefVector *refVector)
{
    [[msvc::forceinline_calls]] return self->Self::CollectObjectsOfGroup1(refVector);
}

void __fastcall Adapters::SetFlag79(Self *self, void *, uint8_t value)
{
    [[msvc::forceinline_calls]] return self->Self::SetFlag79(value);
}

uint32_t *__fastcall Adapters::GetMemberIdSlot(Self *self, void *, int32_t index)
{
    [[msvc::forceinline_calls]] return self->Self::GetMemberIdSlot(index);
}

uint32_t __fastcall Adapters::GetMemberId(Self *self, void *, int32_t index)
{
    [[msvc::forceinline_calls]] return self->Self::GetMemberId(index);
}

uint32_t *__fastcall Adapters::FindMemberId(Self *self, void *, uint32_t id)
{
    [[msvc::forceinline_calls]] return self->Self::FindMemberId(id);
}

uint8_t __fastcall Adapters::HasMemberId(Self *self, void *, uint32_t id)
{
    [[msvc::forceinline_calls]] return self->Self::HasMemberId(id);
}

void __fastcall Adapters::SetName(Self *self, void *, const void *text)
{
    [[msvc::forceinline_calls]] return self->Self::SetName(text);
}

void __fastcall Adapters::SetDescription(Self *self, void *, const void *text)
{
    [[msvc::forceinline_calls]] return self->Self::SetDescription(text);
}

void __fastcall Adapters::AddToIdSet(Self *self, void *, uint32_t id)
{
    [[msvc::forceinline_calls]] return self->Self::AddToIdSet(id);
}

void __fastcall Adapters::ClearIdSet(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::ClearIdSet();
}

uint8_t __fastcall Adapters::IsFamilyLot(Self *self, void *, uint32_t lot)
{
    [[msvc::forceinline_calls]] return self->Self::IsFamilyLot(lot);
}

uint32_t __fastcall Adapters::GetUniqueId(Self *self, void *, uint8_t create)
{
    [[msvc::forceinline_calls]] return self->Self::GetUniqueId(create);
}

void __fastcall Adapters::SetFlagBit0(Self *self, void *, uint8_t on)
{
    [[msvc::forceinline_calls]] return self->Self::SetFlagBit0(on);
}

uint8_t __fastcall Adapters::GetFlagBit0(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetFlagBit0();
}

void __fastcall Adapters::SetFlagBit3(Self *self, void *, uint8_t on)
{
    [[msvc::forceinline_calls]] return self->Self::SetFlagBit3(on);
}

uint8_t __fastcall Adapters::GetFlagBit3(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetFlagBit3();
}

void __fastcall Adapters::SetFlagBit1(Self *self, void *, uint8_t on)
{
    [[msvc::forceinline_calls]] return self->Self::SetFlagBit1(on);
}

uint8_t __fastcall Adapters::GetFlagBit1(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetFlagBit1();
}

void __fastcall Adapters::SetFlagBit2(Self *self, void *, uint8_t on)
{
    [[msvc::forceinline_calls]] return self->Self::SetFlagBit2(on);
}

uint8_t __fastcall Adapters::GetFlagBit2(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetFlagBit2();
}
