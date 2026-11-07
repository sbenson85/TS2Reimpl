#include "ts/cTSNeighborhoodInfo.h"
#include "game/game.h"
#include "game/vtable.h"
#include "rz/ServiceGetters.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"
#include "simantics/TokenExpander.h"

#include <new>

namespace {

constexpr uint32_t kPersistInterface = 0x456b8f1d;      // the +8 face
constexpr uint32_t kSerializableInterface = 0xe4fda3d4; // the +4 face
constexpr uint32_t kDescribeInterface = 0x51f85085;     // the +0xc face

constexpr uint32_t kTextTableType = 0x43545353;    // the key type of the string table (versions 3 and up)
constexpr uint32_t kOldTextTableType = 0x53545223; // the key type of the older table (version 2)
constexpr uint32_t kRecordKeyType = 0xb21be28b;    // the key type of the record object, also its framework class id
constexpr uint32_t kResourceInterface = 0xfb4cb0de;
constexpr uint32_t kResourceClassId = 0xdb4cb0f9;  // what the framework creates for the string table
constexpr uint32_t kRecordInterface = 0xd21be2b7;
constexpr uint32_t kRecordManagerId = 0xd0590041;  // asked of the slot-23 service
constexpr uint32_t kNameFormatRva = 0xdcbe5c;      // the format the record's description is made with (game text)

// Slots of the persist system (the service behind 0xb390c0).
namespace persist {
constexpr int kGetResource = 5;     // (key, interface, out, 0, 0) -> found
constexpr int kGetRecords = 3;      // the object whose slot 10 takes a key
constexpr int kIsLoaded = 16;       // (face, 0)
} // namespace persist

// The string table resource's slots.
namespace table {
constexpr int kGetText = 7;       // (text, index, language)
constexpr int kSetText = 10;      // (text, index, language)
constexpr int kSerializable = 3;  // answers the object whose slot 4 takes a key
constexpr int kFlush = 21;
} // namespace table

// The serialization streams by slot (reads take a pointer, writes the value).
namespace io {
constexpr int kByte = 4;
constexpr int kInt32 = 8;
constexpr int kUint32 = 9;
constexpr int kText = 15;
constexpr int kErrorState = 18;
} // namespace io

template <typename R, typename... A> R Call(const void *object, int slot, A... args)
{
    return VirtualSlot<R(__thiscall *)(const void *, A...)>(object, slot)(object, args...);
}

void *PersistService()
{
    return CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
}

void AddRefObject(void *object) { Call<uint32_t>(object, 1); }
void ReleaseObject(void *object) { Call<uint32_t>(object, 2); }

using ResourceKey = cGZPersistResourceKey;

} // namespace

// ---- construction and destruction ----

// 0x4a6350 (and inline in Create 0x4a6720): the compiler installs our five vtables after cRZUnknown's constructor; the texts
// are built by cRZString's (the game's string vtable, an empty string, a zero count).
cTSNeighborhoodInfo::cTSNeighborhoodInfo()
    : mNumber(-1), mFlags(0), mVersion(kVersion), mCategory(0), mKind(1), mFieldAC(0), mKey{}, mRecord(nullptr),
      mSlotValues{1, 2, 3, 0}
{
    mName.Construct();
    mDescription.Construct();
    mFilePrefix.Construct();
    mTextD.Construct();
}

// 0x4a6500: the record object released, the texts destroyed in reverse order, then cRZUnknown's destructor (the compiler's).
cTSNeighborhoodInfo::~cTSNeighborhoodInfo()
{
    if (mRecord) ReleaseObject(mRecord);
    mTextD.Destruct();
    mFilePrefix.Destruct();
    mDescription.Destruct();
    mName.Destruct();
}

// 0x4a6720
cTSNeighborhoodInfo *cTSNeighborhoodInfo::Create()
{
    return new (std::nothrow) cTSNeighborhoodInfo;
}

// ---- cIGZUnknown ----

// 0x4a6d00: the faces by interface id with the null test of a base-class conversion, counted through slot 1; the describe face
// counted with cRZUnknown's AddRef called directly; other ids go to cRZUnknown's answer (id 1).
bool cTSNeighborhoodInfo::QueryInterface(uint32_t riid, void **out)
{
    switch (riid) {
    case kInterfaceId:
        *out = static_cast<cITSNeighborhoodInfo *>(this);
        AddRef();
        return true;
    case kDescribeInterface:
        *out = static_cast<cIGZSelfDescribing *>(this);
        rz::cRZUnknown::AddRef();
        return true;
    case kPersistInterface:
        *out = static_cast<cIGZPersistResource *>(this);
        break;
    case kSerializableInterface:
        *out = static_cast<cIGZSerializable *>(this);
        break;
    default:
        return rz::cRZUnknown::QueryInterface(riid, out);
    }
    AddRef();
    return true;
}

// folded 0x233070 / 0x233700: cRZUnknown's (Release deletes through the destructor slot at a count of 1 or below).
uint32_t cTSNeighborhoodInfo::AddRef()
{
    return rz::cRZUnknown::AddRef();
}

uint32_t cTSNeighborhoodInfo::Release()
{
    return rz::cRZUnknown::Release();
}

// ---- cITSNeighborhoodInfo ----

void cTSNeighborhoodInfo::SetName(const char *text)
{
    mName.mString.assign(text);
}

cRZString *cTSNeighborhoodInfo::GetName()
{
    return &mName;
}

void cTSNeighborhoodInfo::SetDescription(const char *text)
{
    mDescription.mString.assign(text);
}

cRZString *cTSNeighborhoodInfo::GetDescription()
{
    return &mDescription;
}

void cTSNeighborhoodInfo::SetFilePrefix(const char *text)
{
    mFilePrefix.mString.assign(text);
}

cRZString *cTSNeighborhoodInfo::GetFilePrefix()
{
    return &mFilePrefix;
}

void cTSNeighborhoodInfo::SetTextD(const char *text)
{
    mTextD.mString.assign(text);
}

cRZString *cTSNeighborhoodInfo::GetTextD()
{
    return &mTextD;
}

void cTSNeighborhoodInfo::SetNumber(int32_t number)
{
    mNumber = number;
}

int32_t cTSNeighborhoodInfo::GetNumber()
{
    return mNumber;
}

void cTSNeighborhoodInfo::SetFlags(uint32_t mask, bool set)
{
    if (set)
        mFlags |= mask;
    else
        mFlags &= ~mask;
}

bool cTSNeighborhoodInfo::HasFlags(uint32_t mask)
{
    return (mFlags & mask) == mask;
}

bool cTSNeighborhoodInfo::IsNumber(int32_t number)
{
    return mNumber == number;
}

bool cTSNeighborhoodInfo::MatchesName(const char *text)
{
    using Compare = int32_t(__thiscall *)(void *, const char *, uint32_t, uint32_t);
    if (VirtualCall<Compare, 13>(&mName, text, 0xffffffffu, 0) != 0 &&
        VirtualCall<Compare, 13>(&mFilePrefix, text, 0xffffffffu, 0) != 0)
        return false;
    return true;
}

void cTSNeighborhoodInfo::SetKind(uint32_t kind)
{
    mKind = kind;
}

uint32_t cTSNeighborhoodInfo::GetKind()
{
    return mKind;
}

void cTSNeighborhoodInfo::SetFieldAC(uint32_t value)
{
    mFieldAC = value;
}

uint32_t cTSNeighborhoodInfo::GetFieldAC()
{
    return mFieldAC;
}

void cTSNeighborhoodInfo::SetCategory(int32_t category)
{
    mCategory = category;
}

int32_t cTSNeighborhoodInfo::GetCategory()
{
    return mCategory;
}

cIGZSerializable *cTSNeighborhoodInfo::AsSerializable()
{
    return static_cast<cIGZSerializable *>(this);
}

cIGZPersistResource *cTSNeighborhoodInfo::AsPersistResource()
{
    return static_cast<cIGZPersistResource *>(this);
}

void cTSNeighborhoodInfo::LoadTexts()
{
    void *service = PersistService();
    if (mVersion >= 3) {
        const ResourceKey key = {kTextTableType, GetGroup(), 1, 0};
        void *resource = nullptr;
        if (Call<uint8_t>(service, persist::kGetResource, &key, kResourceInterface, &resource, 0, 0)) {
            Call<void>(resource, table::kGetText, &mName, 1, -1);
            Call<void>(resource, table::kGetText, &mDescription, 2, -1);
        }
        if (resource) ReleaseObject(resource);
    } else if (mVersion >= 2) {
        const uint32_t instance = GetInstance();
        const uint32_t group = GetGroup();
        const ResourceKey key = {kOldTextTableType, group, instance, 0};
        void *resource = nullptr;
        Call<uint8_t>(service, persist::kGetResource, &key, kResourceInterface, &resource, 0, 0);
        if (resource) {
            Call<void>(resource, table::kGetText, &mName, 1, -1);
            Call<void>(resource, table::kGetText, &mDescription, 2, -1);
            ReleaseObject(resource);
        }
    }
}

void cTSNeighborhoodInfo::Unload()
{
    void *service = PersistService();
    const ResourceKey current = GetKey();
    if (current.type == 0 && current.group == 0 && current.instance == 0 && current.resource == 0) return;
    ResourceKey keyA;
    keyA.type = kRecordKeyType;
    keyA.group = GetGroup();
    keyA.instance = 1;
    keyA.resource = 1;
    const ResourceKey keyX = keyA;
    void *records = Call<void *>(service, persist::kGetRecords);
    Call<void>(records, 10, &keyX);
    if (mRecord) {
        void *record = mRecord;
        mRecord = nullptr;
        ReleaseObject(record);
    }
    ResourceKey keyB;
    keyB.type = kTextTableType;
    keyB.group = GetGroup();
    keyB.instance = 1;
    keyB.resource = 0;
    records = Call<void *>(service, persist::kGetRecords);
    Call<void>(records, 10, &keyB);
    records = Call<void *>(service, persist::kGetRecords);
    const auto recordsVTable = *static_cast<void *const *const *>(records); // read before the key getter runs
    const ResourceKey key = GetKey();
    reinterpret_cast<void(__thiscall *)(void *, const void *)>(recordsVTable[10])(records, &key);
}

bool cTSNeighborhoodInfo::StoreTexts()
{
    void *service = PersistService();
    bool ok = Call<uint8_t>(service, persist::kIsLoaded, static_cast<cIGZPersistResource *>(this), uint32_t(0)) & 1;
    ResourceKey key = {kTextTableType, 0, 1, 0};
    key.group = GetGroup();
    void *resource = nullptr;
    if (!Call<uint8_t>(service, persist::kGetResource, &key, kResourceInterface, &resource, 0, 0)) {
        void *framework = GetDirectorFrameworkObject();
        if (resource) {
            void *old = resource;
            resource = nullptr;
            ReleaseObject(old);
        }
        Call<void>(framework, 3, kResourceClassId, kResourceInterface, &resource);
        // ORIGINAL-BUG: the created resource is used without a null check; a framework that can't create it crashes here.
        void *serializable = Call<void *>(resource, table::kSerializable);
        Call<void>(serializable, 4, &key);
        Call<void>(resource, table::kSetText, &mName, 1, 0);
        Call<void>(resource, table::kSetText, &mDescription, 2, 0);
    }
    if (resource) {
        Call<void>(resource, table::kSetText, &mName, 1, -1);
        Call<void>(resource, table::kSetText, &mDescription, 2, -1);
        void *language = tokens::GetSecondLanguageService();
        if (Call<uint32_t>(language, 7) != 1) {
            Call<void>(resource, table::kSetText, &mName, 1, 0);
            Call<void>(resource, table::kSetText, &mDescription, 2, 0);
        }
        if (Call<uint32_t>(resource, table::kFlush) != 0) ok = false;
        ReleaseObject(resource);
    }
    return ok;
}

bool cTSNeighborhoodInfo::GetRecord(void **out)
{
    if (!mRecord) {
        const ResourceKey key = {kRecordKeyType, GetGroup(), 1, 1};
        void *service = GetCachedSlot23();
        void *manager = Call<void *>(service, 34, kRecordManagerId);
        if (!manager) return false;
        if (mRecord) {
            void *old = mRecord;
            mRecord = nullptr;
            ReleaseObject(old);
        }
        Call<void>(manager, 3, &key, kRecordInterface, &mRecord);
        if (!mRecord) {
            void *framework = GetDirectorFrameworkObject();
            Call<void>(framework, 3, kRecordKeyType, kRecordInterface, &mRecord);
            if (!mRecord) return false;
            cRZString text;
            text.Construct();
            cRZString *name = GetName();
            const char *nameText = Call<const char *>(name, 5);
            text.Sprintf(GameAddress<const char>(kNameFormatRva), nameText);
            Call<void>(mRecord, 13, &text);
            Call<void>(mRecord, 4, &key);
            Call<void>(manager, 4, mRecord, 0, 0, -1);
            text.Destruct();
        }
    }
    *out = mRecord;
    AddRefObject(mRecord);
    return true;
}

int32_t cTSNeighborhoodInfo::GetSlotValue(uint32_t index)
{
    return mSlotValues[index];
}

void cTSNeighborhoodInfo::SetSlotValue(uint32_t value, uint32_t index)
{
    if (value <= 3 && index <= 3) mSlotValues[index] = int32_t(value);
}

bool cTSNeighborhoodInfo::IsKind567()
{
    return mKind == 5 || mKind == 6 || mKind == 7;
}

// `&&` normalises the slot's byte to 0 / 1 (test al, al; setne al), as the original.
bool cTSNeighborhoodInfo::IsCategory13WithFlag4()
{
    return mCategory == 13 && HasFlags(4);
}

bool cTSNeighborhoodInfo::IsCategory16WithFlag4()
{
    return mCategory == 16 && HasFlags(4);
}

// ---- cIGZSerializable ----

bool cTSNeighborhoodInfo::Write(ifaces::cIGZOStream *stream)
{
    const void *out = stream;
    Call<void>(out, io::kUint32, kVersion);
    Call<void>(out, io::kText, &mFilePrefix);
    Call<void>(out, io::kInt32, mNumber);
    Call<void>(out, io::kUint32, mKind);
    Call<void>(out, io::kText, &mTextD);
    Call<void>(out, io::kUint32, mFieldAC);
    Call<void>(out, io::kUint32, kMaxStoredValue);
    Call<void>(out, io::kUint32, uint32_t(mCategory));
    Call<void>(out, io::kUint32, mFlags);
    for (int i = 0; i < 4; ++i) Call<void>(out, io::kByte, uint32_t(uint8_t(mSlotValues[i])));
    return Call<uint8_t>(out, io::kErrorState) == 0;
}

bool cTSNeighborhoodInfo::Read(ifaces::cIGZIStream *stream)
{
    const void *in = stream;
    Call<void>(in, io::kUint32, &mVersion);
    if (mVersion < 2) Call<void>(in, io::kText, &mName);
    Call<void>(in, io::kText, &mFilePrefix);
    Call<void>(in, io::kInt32, &mNumber);
    // The original reads these words through one stack local that later byte reads reuse: a failed read leaves the previous value.
    uint32_t scratch = 0;
    if (mVersion >= 4) {
        scratch = mKind;
        Call<void>(in, io::kUint32, &scratch);
        mKind = scratch;
    }
    if (mVersion >= 5) Call<void>(in, io::kText, &mTextD);
    if (mVersion >= 6) {
        scratch = mFieldAC;
        Call<void>(in, io::kUint32, &scratch);
        mFieldAC = scratch;
    } else if (mKind == 3) {
        mFieldAC = 1;
    }
    if (mVersion >= 7) {
        Call<void>(in, io::kUint32, &scratch);
        if (scratch > kMaxStoredValue) return false;
    }
    if (mVersion >= 8) {
        Call<void>(in, io::kUint32, &mCategory);
    } else {
        switch (mKind) {
        case 2: mCategory = 1; break;
        case 3: mCategory = 2; break;
        case 5:
        case 6:
        case 7: mCategory = 10; break;
        default: mCategory = 0; break; // including kind 4
        }
    }
    if (mVersion >= 9) Call<void>(in, io::kUint32, &mFlags);
    if (mVersion >= 10) {
        for (int i = 0; i < 4; ++i) {
            Call<void>(in, io::kByte, &scratch);
            mSlotValues[i] = int8_t(scratch);
        }
    } else {
        mSlotValues[0] = 1;
        mSlotValues[1] = 2;
        mSlotValues[2] = 3;
        mSlotValues[3] = 0;
    }
    return Call<uint8_t>(in, io::kErrorState) == 0;
}

uint32_t cTSNeighborhoodInfo::GetGZCLSID()
{
    return kClassId;
}

// ---- cIGZPersistResource ----

cGZPersistResourceKey cTSNeighborhoodInfo::GetKey()
{
    return mKey;
}

bool cTSNeighborhoodInfo::SetKey(const cGZPersistResourceKey &key)
{
    mKey = key;
    return true;
}

uint32_t cTSNeighborhoodInfo::GetType()
{
    return mKey.type;
}

uint32_t cTSNeighborhoodInfo::GetGroup()
{
    return mKey.group;
}

uint32_t cTSNeighborhoodInfo::GetInstance()
{
    return mKey.instance;
}

bool cTSNeighborhoodInfo::SetType(uint32_t type)
{
    mKey.type = type;
    return true;
}

bool cTSNeighborhoodInfo::SetGroup(uint32_t group)
{
    mKey.group = group;
    return true;
}

bool cTSNeighborhoodInfo::SetInstance(uint32_t instance)
{
    mKey.instance = instance;
    return true;
}

uint32_t cTSNeighborhoodInfo::GetRefCount()
{
    return rz::cRZUnknown::RefCount();
}

// ---- cIGZSelfDescribing ----

bool cTSNeighborhoodInfo::Describe(void *target)
{
    VirtualCall<void(__thiscall *)(void *, cRZString *), 16>(target, GetName());
    return true;
}

bool cTSNeighborhoodInfo::DescribeMore(void *)
{
    return false;
}

// ---- the RVA hooks' adapters ----
// Qualified calls (no virtual dispatch: a record built with the game's vtables would reach these very hooks again), bodies
// forced inline so each adapter is the function itself (matchcheck compares the adapter). A base-to-derived static_cast of a reference finds
// the object from a face (a pointer cast would add the null test the original's slots don't have).

using Adapters = cTSNeighborhoodInfoAdapters;

Adapters::Self *__fastcall Adapters::Construct(Self *memory, void *)
{
    return ::new (memory) Self;
}

void __fastcall Adapters::Destruct(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->Self::~Self();
}

// The deleting destructor has no name to call without dispatch: its two steps by hand. (The destructor stores our vtables, not
// the game's, into an object built by the game: only seen if the object were used after destruction with flags 0.)
Adapters::Self *__fastcall Adapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    [[msvc::forceinline_calls]] self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}

Adapters::Self *__fastcall Adapters::UnknownDeletingDestructor(rz::cRZUnknown *face, void *, uint32_t flags)
{
    Self *const self = &static_cast<Self &>(*face);
    [[msvc::forceinline_calls]] self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}

bool __fastcall Adapters::QueryInterface(Self *self, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::QueryInterface(riid, out);
}

bool __fastcall Adapters::SerialQueryInterface(cIGZSerializable *face, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return static_cast<Self &>(*face).Self::QueryInterface(riid, out);
}

bool __fastcall Adapters::PersistQueryInterface(cIGZPersistResource *face, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return static_cast<Self &>(*face).Self::QueryInterface(riid, out);
}

bool __fastcall Adapters::DescribeQueryInterface(cIGZSelfDescribing *face, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return static_cast<Self &>(*face).Self::QueryInterface(riid, out);
}

bool __fastcall Adapters::UnknownQueryInterface(rz::cRZUnknown *face, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return static_cast<Self &>(*face).Self::QueryInterface(riid, out);
}

void __fastcall Adapters::SetName(Self *self, void *, const char *text)
{
    [[msvc::forceinline_calls]] self->Self::SetName(text);
}

void __fastcall Adapters::SetTextD(Self *self, void *, const char *text)
{
    [[msvc::forceinline_calls]] self->Self::SetTextD(text);
}

void __fastcall Adapters::SetNumber(Self *self, void *, int32_t number)
{
    [[msvc::forceinline_calls]] self->Self::SetNumber(number);
}

bool __fastcall Adapters::IsNumber(Self *self, void *, int32_t number)
{
    [[msvc::forceinline_calls]] return self->Self::IsNumber(number);
}

void __fastcall Adapters::SetFlags(Self *self, void *, uint32_t mask, bool set)
{
    [[msvc::forceinline_calls]] self->Self::SetFlags(mask, set);
}

bool __fastcall Adapters::HasFlags(Self *self, void *, uint32_t mask)
{
    [[msvc::forceinline_calls]] return self->Self::HasFlags(mask);
}

bool __fastcall Adapters::MatchesName(Self *self, void *, const char *text)
{
    [[msvc::forceinline_calls]] return self->Self::MatchesName(text);
}

int32_t __fastcall Adapters::GetSlotValue(Self *self, void *, uint32_t index)
{
    [[msvc::forceinline_calls]] return self->Self::GetSlotValue(index);
}

void __fastcall Adapters::SetSlotValue(Self *self, void *, uint32_t value, uint32_t index)
{
    [[msvc::forceinline_calls]] self->Self::SetSlotValue(value, index);
}

bool __fastcall Adapters::IsKind567(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::IsKind567();
}

bool __fastcall Adapters::IsCategory13WithFlag4(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::IsCategory13WithFlag4();
}

bool __fastcall Adapters::IsCategory16WithFlag4(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::IsCategory16WithFlag4();
}

void __fastcall Adapters::LoadTexts(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->Self::LoadTexts();
}

void __fastcall Adapters::Unload(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->Self::Unload();
}

bool __fastcall Adapters::StoreTexts(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::StoreTexts();
}

bool __fastcall Adapters::GetRecord(Self *self, void *, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::GetRecord(out);
}

uint32_t __fastcall Adapters::GetGZCLSID(cIGZSerializable *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self &>(*face).Self::GetGZCLSID();
}

bool __fastcall Adapters::Write(cIGZSerializable *face, void *, ifaces::cIGZOStream *stream)
{
    [[msvc::forceinline_calls]] return static_cast<Self &>(*face).Self::Write(stream);
}

bool __fastcall Adapters::Read(cIGZSerializable *face, void *, ifaces::cIGZIStream *stream)
{
    [[msvc::forceinline_calls]] return static_cast<Self &>(*face).Self::Read(stream);
}

// The key by value: the hidden return pointer is the stack argument, returned in eax (as the thiscall slot).
cGZPersistResourceKey *__fastcall Adapters::GetKey(cIGZPersistResource *face, void *, cGZPersistResourceKey *out)
{
    __assume(face != nullptr); // a slot's this is never null: no null test in the face-to-object-to-face adjustment
    [[msvc::forceinline_calls]] *out = static_cast<Self &>(*face).Self::GetKey();
    return out;
}

bool __fastcall Adapters::SetKey(cIGZPersistResource *face, void *, const cGZPersistResourceKey *key)
{
    __assume(face != nullptr);
    [[msvc::forceinline_calls]] return static_cast<Self &>(*face).Self::SetKey(*key);
}

bool __fastcall Adapters::SetInstance(cIGZPersistResource *face, void *, uint32_t instance)
{
    __assume(face != nullptr);
    [[msvc::forceinline_calls]] return static_cast<Self &>(*face).Self::SetInstance(instance);
}

bool __fastcall Adapters::Describe(cIGZSelfDescribing *face, void *, void *target)
{
    __assume(face != nullptr);
    [[msvc::forceinline_calls]] return static_cast<Self &>(*face).Self::Describe(target);
}
