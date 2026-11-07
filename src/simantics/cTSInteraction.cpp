#include "simantics/cTSInteraction.h"

#include "app/AppInit.h"
#include "game/game.h"
#include "game/vtable.h"
#include "gz/cIGZString.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"

#include <new>

// cTSInteraction (src/simantics/cTSInteraction.h). Every body was compiled /O2 in the original (tools/flagprofile.py).

namespace {

using Self = cTSInteraction;

// The flag word's bits the getters and setters name.
enum : uint32_t {
    kAutoFirstSelect = 1u << 0,
    kFlag1 = 1u << 1,
    kCheckPending = 1u << 2,
    kCheckPassed = 1u << 3,
    kFlag4 = 1u << 4,
    kBranch = 1u << 5,
    kFlag6 = 1u << 6,
    kNameSet = 1u << 7,
    kFlag8 = 1u << 8,
    kVetoed = 1u << 9,
    kFlag10 = 1u << 10,
    kFlag11 = 1u << 11,
    kFlag12 = 1u << 12,
    kFlag13 = 1u << 13,
    kFlag14 = 1u << 14,
};

constexpr uint32_t kPersonInterfaceId = 0x5a69d3d5; // cITSPerson (slot 17's query)
constexpr uint32_t kLanguageStreamId = 0xe56b8f03;  // Read: the stream interface that knows its resource
constexpr uint32_t kLanguageClassId = 0xcdcbe0e3;   // Read: the framework object made for the language lookup
constexpr uint32_t kLanguageObjectId = 0xcdcbe0dd;

// The object table: the cached singleton's slot 26; slot 29 looks an object up by id.
void *ObjectTable()
{
    return VirtualCall<void *(__thiscall *)(void *), 26>(GetCachedSingleton());
}
void *LookUpObject(int16_t id)
{
    return VirtualCall<void *(__thiscall *)(void *, int32_t), 29>(ObjectTable(), id);
}
// The same, reading the id only after the table is fetched (as the original's getters do).
void *LookUpObjectAt(const int16_t &id)
{
    void *const table = ObjectTable();
    return VirtualCall<void *(__thiscall *)(void *, int32_t), 29>(table, id);
}
// An object's id (cTSObject slot 47).
uint16_t ObjectId(void *object)
{
    return VirtualCall<uint16_t(__thiscall *)(void *), 47>(object);
}
// A person's object (cITSPerson slot 8).
void *PersonObject(void *person)
{
    return VirtualCall<void *(__thiscall *)(void *), 8>(person);
}
// The person's interactions (cITSPerson slots 132, 133; 126 / 129 the pending queue, 127 / 130 the action queue).
cITSInteraction *CurrentInteraction(void *person)
{
    return VirtualCall<cITSInteraction *(__thiscall *)(void *), 132>(person);
}
cITSInteraction *SelectedInteraction(void *person)
{
    return VirtualCall<cITSInteraction *(__thiscall *)(void *), 133>(person);
}
int16_t PendingCount(void *person)
{
    return VirtualCall<int16_t(__thiscall *)(void *), 126>(person);
}
int16_t QueuedCount(void *person)
{
    return VirtualCall<int16_t(__thiscall *)(void *), 127>(person);
}
cITSInteraction *PendingAt(void *person, int32_t index)
{
    return VirtualCall<cITSInteraction *(__thiscall *)(void *, int32_t), 129>(person, index);
}
cITSInteraction *QueuedAt(void *person, int32_t index)
{
    return VirtualCall<cITSInteraction *(__thiscall *)(void *, int32_t), 130>(person, index);
}
// The persons the cached singleton's object table lists (slot 45: a vector of person pointers).
struct PersonList {
    void **first;
    void **last;
};
PersonList *Persons()
{
    return VirtualCall<PersonList *(__thiscall *)(void *), 45>(ObjectTable());
}

// The table entry slots the set-up functions read (cITSTreeTableEntry).
struct Entry {
    void *self;
    uint32_t JoinIndex() const { return VirtualCall<uint32_t(__thiscall *)(void *), 37>(self); }
    uint16_t ActionTreeId() const { return VirtualCall<uint16_t(__thiscall *)(void *), 76>(self); }
    float AttenuationValue(uint8_t alternate) const // (the byte goes zero-extended)
    {
        return VirtualCall<float(__thiscall *)(void *, uint32_t), 84>(self, uint32_t{alternate});
    }
    cIGZString *NameString() const { return VirtualCall<cIGZString *(__thiscall *)(void *), 90>(self); }
    bool AutoFirstSelect() const { return VirtualCall<bool(__thiscall *)(void *), 41>(self); }
    bool AllowSubQueue() const { return VirtualCall<bool(__thiscall *)(void *), 19>(self); }
    bool TypeSubQueue() const { return VirtualCall<bool(__thiscall *)(void *), 27>(self); }
    int16_t UIDisplayType() const { return VirtualCall<int16_t(__thiscall *)(void *), 31>(self); }
    bool DebugOnly() const { return VirtualCall<bool(__thiscall *)(void *), 39>(self); }
    bool HasParameters() const { return VirtualCall<bool(__thiscall *)(void *), 50>(self); }
    uint32_t ParameterA() const { return VirtualCall<uint32_t(__thiscall *)(void *), 48>(self); }
    uint32_t ParameterB() const { return VirtualCall<uint32_t(__thiscall *)(void *), 46>(self); }
};
// The interaction table's entry at an index (slot 13).
void *TableEntryAt(void *table, uint32_t index)
{
    return VirtualCall<void *(__thiscall *)(void *, uint32_t), 13>(table, index);
}
// An object's interaction table (cTSObject slot 87, with 1).
void *InteractionTable(void *object)
{
    return VirtualCall<void *(__thiscall *)(void *, uint32_t), 87>(object, 1);
}
// The person's byte that picks the alternate attenuation (cITSPerson slot 30).
uint8_t AttenuationChoice(void *person)
{
    return VirtualCall<uint8_t(__thiscall *)(void *), 30>(person);
}
// cRZString's slot 16 (operator=) on the name, through its vtable as the original calls it.
void AssignName(cTSInteractionName &name, const cIGZString *value)
{
    VirtualCall<void *(__thiscall *)(void *, const cIGZString *), 16>(&name.mString, value);
}

int32_t &SerialCounter()
{
    return *GameAddress<int32_t>(Self::kSerialCounter);
}

} // namespace

// ---- construction ----

cTSInteraction::cTSInteraction()
    : mRefCount(0), mSourceKind(0), mActorId(0), mStackObjectId(0), mGroupObjectId(0), mExtraObjectId(0),
      mInteractionIndex(-1), mArguments{0, 0, 0, 0}, mPriority(0), mActionTreeId(0), mAttenuation(0.0f), mAllowSubQueue(0),
      mTypeSubQueue(0), mUIDisplayType(0), mWord32(0), mWord34(0), mParameterA(0), mParameterB(0), mLinkId(0), mLinkNext(0),
      mName(), mSerial(0), mFlags(0)
{
}

cTSInteraction::~cTSInteraction() = default;

cTSInteraction *cTSInteraction::Create()
{
    return new cTSInteraction;
}

// ---- cIGZUnknown, cIGZSerializable ----

bool cTSInteraction::QueryInterface(uint32_t riid, void **out)
{
    if (out == nullptr) {
        return false;
    }
    switch (riid) {
    case 1:
    case kInterfaceId:
    case kSerializableId:
    case kClassId:
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

uint32_t cTSInteraction::AddRef()
{
    return ++mRefCount;
}

uint32_t cTSInteraction::Release()
{
    const uint32_t count = mRefCount - 1;
    mRefCount = count;
    if (count != 0) {
        return count;
    }
    delete this;
    return 0;
}

uint32_t cTSInteraction::GetGZCLSID()
{
    return kClassId;
}

bool cTSInteraction::Write(ifaces::cIGZOStream *stream)
{
    ObjectTable(); // asked for and not used
    stream->SetSint32(static_cast<int32_t>(mSourceKind));
    stream->SetSint32(static_cast<int32_t>(mSerial));
    stream->SetSint16(mActorId);
    stream->SetSint16(mStackObjectId);
    stream->SetSint16(mGroupObjectId);
    stream->SetSint16(mExtraObjectId);
    stream->SetSint32(mInteractionIndex);
    for (int i = 0; i < 4; ++i) {
        stream->SetSint16(mArguments[i]);
    }
    stream->SetSint32(static_cast<int32_t>(mPriority));
    stream->SetSint16(mActionTreeId);
    stream->SetFloat32(mAttenuation);
    stream->SetSint32(static_cast<int32_t>(mFlags));
    stream->SetSint16(mUIDisplayType);
    stream->SetSint16(static_cast<int16_t>(mAllowSubQueue));
    stream->SetSint16(static_cast<int16_t>(mTypeSubQueue));
    stream->SetUint16(mLinkId);
    stream->SetUint16(mLinkNext);
    stream->SetGZStr(&mName.mString);
    stream->SetSint32(static_cast<int32_t>(mParameterA));
    stream->SetSint32(static_cast<int32_t>(mParameterB));
    stream->SetSint16(mWord32);
    return !stream->GetError();
}

// The format version comes from the stream's owner (stream slot 20, its slot 78: a pointer to the version). Before 0x3e the
// flag word was not stored: five bools set flags through the setters instead. A name stored from 0x97 on is kept only
// when it was written in the language the game runs in (the resource's language, from a framework object made for it).
bool cTSInteraction::Read(ifaces::cIGZIStream *stream)
{
    void *const owner = VirtualCall<void *(__thiscall *)(void *), 20>(stream);
    const uint32_t version = *VirtualCall<uint32_t *(__thiscall *)(void *), 78>(owner);
    ObjectTable(); // asked for and not used
    uint32_t value32 = 0;
    stream->GetSint32(reinterpret_cast<int32_t *>(&value32));
    mSourceKind = value32;
    stream->GetSint32(reinterpret_cast<int32_t *>(&mSerial));
    stream->GetSint16(&mActorId);
    stream->GetSint16(&mStackObjectId);
    stream->GetSint16(&mGroupObjectId);
    if (version >= 0xa6) {
        stream->GetSint16(&mExtraObjectId);
    }
    int32_t index = 0;
    stream->GetSint32(&index);
    mInteractionIndex = index;
    for (int i = 0; i < 4; ++i) {
        stream->GetSint16(&mArguments[i]);
    }
    int32_t priority = 0;
    stream->GetSint32(&priority);
    mPriority = static_cast<uint32_t>(priority);
    stream->GetSint16(&mActionTreeId);
    stream->GetFloat32(&mAttenuation);
    if (version >= 0x3e) {
        uint32_t flags = version; // (the original reads into the slot that holds the version)
        stream->GetSint32(reinterpret_cast<int32_t *>(&flags));
        mFlags = flags;
        if (version >= 0x6f) {
            stream->GetSint16(&mUIDisplayType);
        }
        if (version >= 0x72) {
            stream->GetSint16(reinterpret_cast<int16_t *>(&mAllowSubQueue));
            stream->GetSint16(reinterpret_cast<int16_t *>(&mTypeSubQueue));
            if (version >= 0x7c) {
                stream->GetUint16(&mLinkId);
                stream->GetUint16(&mLinkNext);
            } else {
                mLinkNext = 0;
                mLinkId = 0;
            }
        } else {
            mAllowSubQueue = 0;
            mTypeSubQueue = 0;
            mLinkNext = 0;
            mLinkId = 0;
        }
        if (version >= 0x97) {
            stream->GetGZStr(&mName.mString);
            stream->GetSint32(reinterpret_cast<int32_t *>(&mParameterA));
            stream->GetSint32(reinterpret_cast<int32_t *>(&mParameterB));
            uint32_t language = 1;
            cIGZUnknown *resource = nullptr;
            if (VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(stream, kLanguageStreamId,
                                                                               reinterpret_cast<void **>(&resource))) {
                const uint32_t key = VirtualCall<const uint32_t *(__thiscall *)(void *), 3>(resource)[1];
                cIGZUnknown *lookup = nullptr;
                void *const framework = GetDirectorFrameworkObject();
                if (VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void **), 3>(
                        framework, kLanguageClassId, kLanguageObjectId, reinterpret_cast<void **>(&lookup))) {
                    VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t), 11>(lookup, key, 1);
                    VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t *), 6>(lookup, 2, &language);
                }
                if (lookup != nullptr) {
                    lookup->Release();
                }
            }
            if (VirtualCall<uint32_t(__thiscall *)(void *), 7>(appinit::GetLanguageService()) != language) {
                mName.mString.mString.clear();
            }
            if (resource != nullptr) {
                resource->Release();
            }
        }
        if (version >= 0xbb) {
            stream->GetSint16(&mWord32);
        }
    } else {
        uint8_t value = static_cast<uint8_t>(version >> 24); // (the original reads into the version's top byte)
        stream->GetUint8(&value);
        SetCheckPending(value != 0);
        stream->GetUint8(&value);
        SetBranchFlag(value != 0);
        if (GetCheckPending()) {
            stream->GetUint8(&value);
            SetCheckPassed(value != 0);
            stream->GetUint8(&value);
            SetFlag4(value != 0);
        }
        if (version >= 0x3c) {
            stream->GetUint8(&value);
            SetFlag1(value != 0);
        }
    }
    if (static_cast<int32_t>(mSerial) > SerialCounter()) {
        SerialCounter() = static_cast<int32_t>(mSerial);
    }
    return !stream->GetError();
}

// ---- set-up and copy ----

bool cTSInteraction::IsDefault()
{
    return mPriority == 0 && mActionTreeId == 0 && mInteractionIndex == -1 && mGroupObjectId == -1 && mSerial == 0 &&
           mActorId <= 0 && mStackObjectId <= 0;
}

// The source person's current interaction, joined: its copy, done by `actor`, at the entry the source's entry names as its
// join index (when the stack object's table has an entry there; otherwise the stack object is dropped).
bool cTSInteraction::SetUpBetween(void *actor, void *source)
{
    cITSInteraction *const joined = CurrentInteraction(source);
    CopyFrom(joined);
    mSourceKind = 1;
    mActorId = actor != nullptr ? static_cast<int16_t>(ObjectId(PersonObject(actor))) : int16_t(-1);
    mPriority = 0x32;
    SetCheckPending(false);
    SetFlag1(false);
    SetExtraObject(nullptr);
    bool found = false;
    if (void *const stackObject = GetStackObject()) {
        if (void *const table = InteractionTable(stackObject)) {
            const Entry entry{TableEntryAt(table, static_cast<uint32_t>(joined->GetInteractionIndex()))};
            if (entry.self != nullptr && TableEntryAt(table, entry.JoinIndex()) != nullptr) {
                const uint32_t join = entry.JoinIndex();
                if (join != 0xffffffffu) {
                    mInteractionIndex = static_cast<int32_t>(join);
                    found = true;
                }
            }
        }
    }
    if (!found) {
        mStackObjectId = -1;
    }
    mArguments[0] = mArguments[1] = mArguments[2] = mArguments[3] = 0;
    mSerial = 0;
    const Entry entry{GetTableEntry()};
    void *const person = GetActor();
    if (entry.self != nullptr && mActorId != 0) {
        mActionTreeId = static_cast<int16_t>(entry.ActionTreeId());
        mAttenuation = entry.AttenuationValue(AttenuationChoice(person));
        AssignName(mName, entry.NameString());
        SetAutoFirstSelect(entry.AutoFirstSelect());
        if (entry.HasParameters()) {
            const uint32_t a = entry.ParameterA();
            SetParameters(a, entry.ParameterB());
        } else {
            SetParameters(0, 0);
        }
        return true;
    }
    mActionTreeId = 0;
    mAttenuation = 0.0f;
    mName.mString.mString.assign("???", 3);
    SetAutoFirstSelect(false);
    return true;
}

bool cTSInteraction::SetUp(void *actor, void *stackObject, int32_t index, uint32_t priority, void *extraObject)
{
    mPriority = priority;
    mFlags = 0;
    mSourceKind = 0;
    SetCheckPending(false);
    SetFlag1(false);
    mActorId = actor != nullptr ? static_cast<int16_t>(ObjectId(PersonObject(actor))) : int16_t(-1);
    SetExtraObject(extraObject);
    if (stackObject == nullptr) {
        mStackObjectId = -1;
        SetGroupObject(nullptr);
        mInteractionIndex = -1;
    } else {
        mStackObjectId = static_cast<int16_t>(ObjectId(stackObject));
        SetGroupObject(stackObject);
        mInteractionIndex = index;
    }
    mArguments[0] = mArguments[1] = mArguments[2] = mArguments[3] = 0;
    mSerial = 0;
    const Entry entry{GetTableEntry()};
    void *const person = GetActor();
    if (entry.self != nullptr && person != nullptr) {
        mActionTreeId = static_cast<int16_t>(entry.ActionTreeId());
        mAttenuation = entry.AttenuationValue(AttenuationChoice(person));
        SetAutoFirstSelect(entry.AutoFirstSelect());
        mAllowSubQueue = static_cast<uint8_t>(entry.AllowSubQueue());
        mTypeSubQueue = static_cast<uint8_t>(entry.TypeSubQueue());
        mUIDisplayType = entry.UIDisplayType();
        SetName(entry.NameString());
        if (entry.HasParameters()) {
            const uint32_t a = entry.ParameterA();
            SetParameters(a, entry.ParameterB());
        } else {
            SetParameters(0, 0);
        }
        return true;
    }
    mActionTreeId = 0;
    mAttenuation = 0.0f;
    SetAutoFirstSelect(false);
    mAllowSubQueue = 0;
    mTypeSubQueue = 0;
    mUIDisplayType = 0;
    return true;
}

// Another cTSInteraction (it answers the class id) is copied field by field; any other implementation through its slots,
// which leave the serial's flag bits 9..13 and the word at +0x34 alone.
bool cTSInteraction::CopyFrom(cITSInteraction *other)
{
    if (other == nullptr) {
        return false;
    }
    cTSInteraction *same = nullptr;
    if (other->QueryInterface(kClassId, reinterpret_cast<void **>(&same))) {
        mSourceKind = same->mSourceKind;
        mActorId = same->mActorId;
        mStackObjectId = same->mStackObjectId;
        mGroupObjectId = same->mGroupObjectId;
        mExtraObjectId = same->mExtraObjectId;
        mInteractionIndex = same->mInteractionIndex;
        mArguments[0] = same->mArguments[0];
        mArguments[1] = same->mArguments[1];
        mArguments[2] = same->mArguments[2];
        mArguments[3] = same->mArguments[3];
        mPriority = same->mPriority;
        mActionTreeId = same->mActionTreeId;
        mAttenuation = same->mAttenuation;
        mAllowSubQueue = same->mAllowSubQueue;
        mTypeSubQueue = same->mTypeSubQueue;
        mLinkId = same->mLinkId;
        mLinkNext = same->mLinkNext;
        mName.mString.mString = same->mName.mString.mString;
        mSerial = same->mSerial;
        mFlags = same->mFlags;
        mUIDisplayType = same->mUIDisplayType;
        mParameterA = same->mParameterA;
        mParameterB = same->mParameterB;
        mWord32 = same->mWord32;
    } else {
        mSourceKind = other->GetSourceKind();
        void *const person = other->GetActor();
        mActorId = person != nullptr ? static_cast<int16_t>(ObjectId(PersonObject(person))) : int16_t(-1);
        void *const stackObject = other->GetStackObject();
        mStackObjectId = stackObject != nullptr ? static_cast<int16_t>(ObjectId(stackObject)) : int16_t(-1);
        SetGroupObject(other->GetGroupObject());
        SetExtraObject(other->GetExtraObject());
        mInteractionIndex = other->GetInteractionIndex();
        mArguments[0] = other->GetArguments()[0];
        mArguments[1] = other->GetArguments()[1];
        mArguments[2] = other->GetArguments()[2];
        mArguments[3] = other->GetArguments()[3];
        mPriority = other->GetPriority();
        mActionTreeId = other->GetActionTreeId();
        mAttenuation = other->GetAttenuation();
        AssignName(mName, other->GetName());
        mSerial = other->GetSerial();
        SetAutoFirstSelect(other->GetAutoFirstSelect());
        SetFlag1(other->GetFlag1());
        SetFlag14(other->GetFlag14());
        SetCheckPending(other->GetCheckPending());
        SetCheckPassed(other->HasPassedCheck());
        SetFlag4(other->GetFlag4());
        SetBranchFlag(other->GetBranchFlag());
        SetFlag6(other->GetFlag6());
        SetNameSet(other->GetNameSet());
        SetFlag8(other->GetFlag8());
        mTypeSubQueue = other->GetTypeSubQueue();
        mAllowSubQueue = other->GetAllowSubQueue();
        mLinkId = other->GetLinkId();
        mLinkNext = other->GetLinkNext();
        other->GetParameters(&mParameterA, &mParameterB);
        mUIDisplayType = other->GetUIDisplayType();
        mWord32 = other->GetWord32();
    }
    if (same != nullptr) {
        same->Release();
    }
    return true;
}

void cTSInteraction::ClearObjectIds()
{
    mActorId = 0;
    mStackObjectId = 0;
    mGroupObjectId = 0;
    mExtraObjectId = 0;
}

// ---- getters and setters ----

uint32_t cTSInteraction::GetSourceKind() { return mSourceKind; }
float cTSInteraction::GetAttenuation() { return mAttenuation; }
int16_t cTSInteraction::GetActionTreeId() { return mActionTreeId; }
void cTSInteraction::SetActionTreeId(int16_t id) { mActionTreeId = id; }
int16_t *cTSInteraction::GetArguments() { return mArguments; }

void *cTSInteraction::GetStackObject()
{
    return LookUpObjectAt(mStackObjectId);
}

// The actor's cITSPerson; the reference the query adds is dropped before returning.
void *cTSInteraction::GetActor()
{
    void *const object = LookUpObject(mActorId);
    if (object == nullptr) {
        return nullptr;
    }
    cIGZUnknown *person = nullptr;
    VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(object, kPersonInterfaceId, reinterpret_cast<void **>(&person));
    cIGZUnknown *const result = person;
    if (person != nullptr) {
        person->Release();
    }
    return result;
}

int32_t cTSInteraction::GetInteractionIndex() { return mInteractionIndex; }
uint16_t cTSInteraction::GetAllowSubQueue() { return mAllowSubQueue; }
uint16_t cTSInteraction::GetTypeSubQueue() { return mTypeSubQueue; }
int16_t cTSInteraction::GetUIDisplayType() { return mUIDisplayType; }
uint32_t cTSInteraction::GetPriority() { return mPriority; }
void cTSInteraction::Ignore(uint32_t) {}

void *cTSInteraction::GetTableEntry()
{
    void *const stackObject = GetStackObject();
    if (stackObject != nullptr) {
        void *const table = InteractionTable(stackObject);
        if (table != nullptr) {
            return TableEntryAt(table, static_cast<uint32_t>(mInteractionIndex));
        }
    }
    return nullptr;
}

// An empty name is built from the table entry's: "*" first for a debug-only entry.
cIGZString *cTSInteraction::GetName()
{
    if (mName.mString.mString.size() == 0) {
        const Entry entry{GetTableEntry()};
        if (entry.self != nullptr) {
            std::string name(mName.mString.mString);
            if (entry.DebugOnly()) {
                name = "*";
            }
            cIGZString *const entryName = entry.NameString();
            const uint32_t length = entryName->Strlen();
            const std::string text(entryName->Data(), length);
            name.append(text.c_str(), text.size());
            mName.mString.mString = name;
        }
    }
    SetNameSet(false);
    return reinterpret_cast<cIGZString *>(&mName.mString);
}

void cTSInteraction::SetName(const cIGZString *name)
{
    AssignName(mName, name);
    SetNameSet(true);
}

void cTSInteraction::SetArguments(const int16_t *arguments)
{
    mArguments[0] = arguments[0];
    mArguments[1] = arguments[1];
    mArguments[2] = arguments[2];
    mArguments[3] = arguments[3];
}

void cTSInteraction::AssignSerial()
{
    mSerial = static_cast<uint32_t>(++SerialCounter());
}

bool cTSInteraction::HasSerial() { return mSerial != 0; }
uint32_t cTSInteraction::GetSerial() { return mSerial; }

bool cTSInteraction::GetAutoFirstSelect() { return mFlags & 1; }
void cTSInteraction::SetAutoFirstSelect(bool on)
{
    mFlags &= ~kAutoFirstSelect;
    if (on) mFlags |= kAutoFirstSelect;
}
bool cTSInteraction::GetFlag1() { return (mFlags >> 1) & 1; }
void cTSInteraction::SetFlag1(bool on)
{
    mFlags &= ~kFlag1;
    if (on) mFlags |= kFlag1;
}
bool cTSInteraction::GetFlag14() { return (mFlags >> 14) & 1; }
void cTSInteraction::SetFlag14(bool on)
{
    mFlags &= ~kFlag14;
    if (on) mFlags |= kFlag14;
}
bool cTSInteraction::GetCheckPending() { return (mFlags >> 2) & 1; }
void cTSInteraction::SetCheckPending(bool on)
{
    mFlags &= ~kCheckPending;
    if (on) mFlags |= kCheckPending;
}
bool cTSInteraction::HasPassedCheck() { return (mFlags >> 3) & 1; }
void cTSInteraction::SetCheckPassed(bool on)
{
    mFlags &= ~kCheckPassed;
    if (on) mFlags |= kCheckPassed;
}
bool cTSInteraction::GetFlag4() { return (mFlags >> 4) & 1; }
void cTSInteraction::SetFlag4(bool on)
{
    mFlags &= ~kFlag4;
    if (on) mFlags |= kFlag4;
}
bool cTSInteraction::GetBranchFlag() { return (mFlags >> 5) & 1; }
void cTSInteraction::SetBranchFlag(bool on)
{
    mFlags &= ~kBranch;
    if (on) mFlags |= kBranch;
}
bool cTSInteraction::GetFlag6() { return (mFlags >> 6) & 1; }
void cTSInteraction::SetFlag6(bool on)
{
    mFlags &= ~kFlag6;
    if (on) mFlags |= kFlag6;
}
bool cTSInteraction::GetFlag10() { return (mFlags >> 10) & 1; }
void cTSInteraction::SetFlag10(bool on)
{
    mFlags &= ~kFlag10;
    if (on) mFlags |= kFlag10;
}
bool cTSInteraction::GetFlag11() { return (mFlags >> 11) & 1; }
void cTSInteraction::SetFlag11(bool on)
{
    mFlags &= ~kFlag11;
    if (on) mFlags |= kFlag11;
}
bool cTSInteraction::GetNameSet() { return (mFlags >> 7) & 1; }
void cTSInteraction::SetNameSet(bool on)
{
    mFlags &= ~kNameSet;
    if (on) mFlags |= kNameSet;
}
bool cTSInteraction::GetFlag8() { return (mFlags >> 8) & 1; }
void cTSInteraction::SetFlag8(bool on)
{
    mFlags &= ~kFlag8;
    if (on) mFlags |= kFlag8;
}
bool cTSInteraction::IsVetoed() { return (mFlags >> 9) & 1; }
void cTSInteraction::SetVetoed(bool on)
{
    mFlags &= ~kVetoed;
    if (on) mFlags |= kVetoed;
}
bool cTSInteraction::GetFlag12() { return (mFlags >> 12) & 1; }
void cTSInteraction::SetFlag12(bool on)
{
    mFlags &= ~kFlag12;
    if (on) mFlags |= kFlag12;
}
bool cTSInteraction::GetFlag13() { return (mFlags >> 13) & 1; }
void cTSInteraction::SetFlag13(bool on)
{
    mFlags &= ~kFlag13;
    if (on) mFlags |= kFlag13;
}

void *cTSInteraction::GetExtraObject()
{
    return LookUpObjectAt(mExtraObjectId);
}
int16_t cTSInteraction::GetExtraObjectId() { return mExtraObjectId; }
void *cTSInteraction::GetGroupObject()
{
    return LookUpObjectAt(mGroupObjectId);
}
int16_t cTSInteraction::GetGroupObjectId() { return mGroupObjectId; }

void cTSInteraction::SetGroupObject(void *object)
{
    mGroupObjectId = object != nullptr ? static_cast<int16_t>(ObjectId(object)) : int16_t(-1);
}

void cTSInteraction::SetExtraObject(void *object)
{
    mExtraObjectId = object != nullptr ? static_cast<int16_t>(ObjectId(object)) : int16_t(-1);
}

// A new link id: the cached singleton's slot 3 object, its slot 43.
void cTSInteraction::AssignLinkId()
{
    void *const source = VirtualCall<void *(__thiscall *)(void *), 3>(GetCachedSingleton());
    mLinkId = VirtualCall<uint16_t(__thiscall *)(void *), 43>(source);
}

// Follows the chain from this interaction's next link, over every person but `exceptPerson`: a linked interaction that is a
// person's current or selected one sets that person's data 0x6e to 1 (person slot 11); one in a queue is removed (person
// slot 123, by serial). Either way the chain goes on from its next link, which is cleared; it ends when a link is not
// found or is 0. A chain that comes back to this interaction is only caught on the first link.
void cTSInteraction::CancelLinked(void *exceptPerson)
{
    PersonList *const persons = Persons();
    uint16_t next = mLinkNext;
    mLinkNext = 0;
    if (next == mLinkId || next == 0) {
        return;
    }
    bool found = true;
    while (found && next != 0) {
        found = false;
        for (void **it = persons->first; it != persons->last && !found; ++it) {
            void *const person = *it;
            if (person == exceptPerson) {
                continue;
            }
            cITSInteraction *running = CurrentInteraction(person);
            if (running == nullptr || running->GetLinkId() != next) {
                running = SelectedInteraction(person);
                if (running != nullptr && running->GetLinkId() != next) {
                    running = nullptr;
                }
            }
            if (running != nullptr) {
                VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t), 11>(person, 0x6e, 1, 1);
                next = running->GetLinkNext();
                running->SetLinkNext(0);
                found = true;
                break;
            }
            cITSInteraction *queued = nullptr;
            const int32_t pending = PendingCount(person);
            for (int32_t i = 0; i < pending && queued == nullptr; ++i) {
                cITSInteraction *const candidate = PendingAt(person, i);
                if (candidate != nullptr && candidate->GetLinkId() == next) {
                    queued = candidate;
                }
            }
            if (queued == nullptr) {
                const int32_t count = QueuedCount(person);
                for (int32_t i = 0; i < count && queued == nullptr; ++i) {
                    cITSInteraction *const candidate = QueuedAt(person, i);
                    if (candidate != nullptr && candidate->GetLinkId() == next) {
                        queued = candidate;
                    }
                }
            }
            if (queued != nullptr) {
                const uint16_t following = queued->GetLinkNext();
                queued->SetLinkNext(0);
                VirtualCall<void(__thiscall *)(void *, uint32_t), 123>(person, queued->GetSerial());
                next = following;
                found = true;
            }
        }
    }
}

uint16_t cTSInteraction::GetLinkId() { return mLinkId; }
uint16_t cTSInteraction::GetLinkNext() { return mLinkNext; }
void cTSInteraction::SetLinkNext(uint16_t id) { mLinkNext = id; }

// The interaction with link id `id` among every person's current, selected, pending and queued ones. When none has it,
// the result is the last interaction looked at (null for none at all): the original returns the variable it searched with.
cITSInteraction *cTSInteraction::FindByLinkId(int16_t id)
{
    PersonList *const persons = Persons();
    cITSInteraction *found = nullptr;
    if (id == 0) {
        return found;
    }
    for (void **it = persons->first; it != persons->last; ++it) {
        void *const person = *it;
        found = CurrentInteraction(person);
        if (found != nullptr && static_cast<int16_t>(found->GetLinkId()) == id) {
            return found;
        }
        found = SelectedInteraction(person);
        if (found != nullptr && static_cast<int16_t>(found->GetLinkId()) == id) {
            return found;
        }
        const int32_t pending = PendingCount(person);
        for (int32_t i = 0; i < pending; ++i) {
            found = PendingAt(person, i);
            if (found != nullptr && static_cast<int16_t>(found->GetLinkId()) == id) {
                return found;
            }
        }
        const int32_t count = QueuedCount(person);
        for (int32_t i = 0; i < count; ++i) {
            found = QueuedAt(person, i);
            if (found != nullptr && static_cast<int16_t>(found->GetLinkId()) == id) {
                return found;
            }
        }
    }
    return found; // ORIGINAL-BUG: not found returns the last interaction examined, not null
}

void cTSInteraction::SetParameters(uint32_t a, uint32_t b)
{
    mParameterA = a;
    mParameterB = b;
}

void cTSInteraction::GetParameters(uint32_t *a, uint32_t *b)
{
    *a = mParameterA;
    *b = mParameterB;
}

int16_t cTSInteraction::GetWord32() { return mWord32; }
void cTSInteraction::SetWord32(int16_t value) { mWord32 = value; }
int16_t cTSInteraction::GetWord34() { return mWord34; }
void cTSInteraction::SetWord34(int16_t value) { mWord34 = value; }

bool cTSInteraction::HasGroup()
{
    if (GetGroupObject() == nullptr && (mParameterA == 0 || mParameterA == 0xffffffffu)) {
        return false;
    }
    return true;
}

// ---- adapters: only the byte-matched slots (the others are reached only through our vtable) ----

bool __fastcall cTSInteractionAdapters::QueryInterface(Self *self, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::QueryInterface(riid, out);
}

uint32_t __fastcall cTSInteractionAdapters::Release(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::Release();
}

bool __fastcall cTSInteractionAdapters::Write(Self *self, void *, ifaces::cIGZOStream *stream)
{
    [[msvc::forceinline_calls]] return self->Self::Write(stream);
}

uint32_t __fastcall cTSInteractionAdapters::GetGZCLSID(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetGZCLSID();
}

bool __fastcall cTSInteractionAdapters::IsDefault(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::IsDefault();
}

void __fastcall cTSInteractionAdapters::ClearObjectIds(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::ClearObjectIds();
}

void *__fastcall cTSInteractionAdapters::GetStackObject(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetStackObject();
}

uint16_t __fastcall cTSInteractionAdapters::GetTypeSubQueue(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetTypeSubQueue();
}

void __fastcall cTSInteractionAdapters::SetName(Self *self, void *, const cIGZString *name)
{
    [[msvc::forceinline_calls]] return self->Self::SetName(name);
}

void __fastcall cTSInteractionAdapters::SetArguments(Self *self, void *, const int16_t *arguments)
{
    [[msvc::forceinline_calls]] return self->Self::SetArguments(arguments);
}

void __fastcall cTSInteractionAdapters::AssignSerial(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::AssignSerial();
}

bool __fastcall cTSInteractionAdapters::GetAutoFirstSelect(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetAutoFirstSelect();
}

void __fastcall cTSInteractionAdapters::SetAutoFirstSelect(Self *self, void *, bool on)
{
    [[msvc::forceinline_calls]] return self->Self::SetAutoFirstSelect(on);
}

bool __fastcall cTSInteractionAdapters::GetFlag1(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetFlag1();
}

void __fastcall cTSInteractionAdapters::SetFlag1(Self *self, void *, bool on)
{
    [[msvc::forceinline_calls]] return self->Self::SetFlag1(on);
}

bool __fastcall cTSInteractionAdapters::GetFlag14(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetFlag14();
}

void __fastcall cTSInteractionAdapters::SetFlag14(Self *self, void *, bool on)
{
    [[msvc::forceinline_calls]] return self->Self::SetFlag14(on);
}

bool __fastcall cTSInteractionAdapters::GetCheckPending(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetCheckPending();
}

void __fastcall cTSInteractionAdapters::SetCheckPending(Self *self, void *, bool on)
{
    [[msvc::forceinline_calls]] return self->Self::SetCheckPending(on);
}

bool __fastcall cTSInteractionAdapters::HasPassedCheck(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::HasPassedCheck();
}

void __fastcall cTSInteractionAdapters::SetCheckPassed(Self *self, void *, bool on)
{
    [[msvc::forceinline_calls]] return self->Self::SetCheckPassed(on);
}

bool __fastcall cTSInteractionAdapters::GetFlag4(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetFlag4();
}

void __fastcall cTSInteractionAdapters::SetFlag4(Self *self, void *, bool on)
{
    [[msvc::forceinline_calls]] return self->Self::SetFlag4(on);
}

bool __fastcall cTSInteractionAdapters::GetBranchFlag(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetBranchFlag();
}

void __fastcall cTSInteractionAdapters::SetBranchFlag(Self *self, void *, bool on)
{
    [[msvc::forceinline_calls]] return self->Self::SetBranchFlag(on);
}

bool __fastcall cTSInteractionAdapters::GetFlag6(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetFlag6();
}

void __fastcall cTSInteractionAdapters::SetFlag6(Self *self, void *, bool on)
{
    [[msvc::forceinline_calls]] return self->Self::SetFlag6(on);
}

bool __fastcall cTSInteractionAdapters::GetFlag10(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetFlag10();
}

void __fastcall cTSInteractionAdapters::SetFlag10(Self *self, void *, bool on)
{
    [[msvc::forceinline_calls]] return self->Self::SetFlag10(on);
}

bool __fastcall cTSInteractionAdapters::GetFlag11(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetFlag11();
}

void __fastcall cTSInteractionAdapters::SetFlag11(Self *self, void *, bool on)
{
    [[msvc::forceinline_calls]] return self->Self::SetFlag11(on);
}

bool __fastcall cTSInteractionAdapters::GetNameSet(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetNameSet();
}

void __fastcall cTSInteractionAdapters::SetNameSet(Self *self, void *, bool on)
{
    [[msvc::forceinline_calls]] return self->Self::SetNameSet(on);
}

bool __fastcall cTSInteractionAdapters::GetFlag8(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetFlag8();
}

void __fastcall cTSInteractionAdapters::SetFlag8(Self *self, void *, bool on)
{
    [[msvc::forceinline_calls]] return self->Self::SetFlag8(on);
}

bool __fastcall cTSInteractionAdapters::IsVetoed(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::IsVetoed();
}

void __fastcall cTSInteractionAdapters::SetVetoed(Self *self, void *, bool on)
{
    [[msvc::forceinline_calls]] return self->Self::SetVetoed(on);
}

bool __fastcall cTSInteractionAdapters::GetFlag12(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetFlag12();
}

void __fastcall cTSInteractionAdapters::SetFlag12(Self *self, void *, bool on)
{
    [[msvc::forceinline_calls]] return self->Self::SetFlag12(on);
}

bool __fastcall cTSInteractionAdapters::GetFlag13(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetFlag13();
}

void __fastcall cTSInteractionAdapters::SetFlag13(Self *self, void *, bool on)
{
    [[msvc::forceinline_calls]] return self->Self::SetFlag13(on);
}

void *__fastcall cTSInteractionAdapters::GetExtraObject(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetExtraObject();
}

void *__fastcall cTSInteractionAdapters::GetGroupObject(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetGroupObject();
}

void __fastcall cTSInteractionAdapters::SetGroupObject(Self *self, void *, void *object)
{
    [[msvc::forceinline_calls]] return self->Self::SetGroupObject(object);
}

void __fastcall cTSInteractionAdapters::AssignLinkId(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::AssignLinkId();
}

uint16_t __fastcall cTSInteractionAdapters::GetLinkId(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetLinkId();
}

uint16_t __fastcall cTSInteractionAdapters::GetLinkNext(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::GetLinkNext();
}

void __fastcall cTSInteractionAdapters::SetLinkNext(Self *self, void *, uint16_t id)
{
    [[msvc::forceinline_calls]] return self->Self::SetLinkNext(id);
}

void __fastcall cTSInteractionAdapters::SetParameters(Self *self, void *, uint32_t a, uint32_t b)
{
    [[msvc::forceinline_calls]] return self->Self::SetParameters(a, b);
}

void __fastcall cTSInteractionAdapters::GetParameters(Self *self, void *, uint32_t *a, uint32_t *b)
{
    [[msvc::forceinline_calls]] return self->Self::GetParameters(a, b);
}

void __fastcall cTSInteractionAdapters::SetWord32(Self *self, void *, int16_t value)
{
    [[msvc::forceinline_calls]] return self->Self::SetWord32(value);
}

void __fastcall cTSInteractionAdapters::SetWord34(Self *self, void *, int16_t value)
{
    [[msvc::forceinline_calls]] return self->Self::SetWord34(value);
}

bool __fastcall cTSInteractionAdapters::HasGroup(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Self::HasGroup();
}

void __fastcall cTSInteractionAdapters::SetExtraObject(Self *self, void *, void *object)
{
    [[msvc::forceinline_calls]] return self->Self::SetExtraObject(object);
}
