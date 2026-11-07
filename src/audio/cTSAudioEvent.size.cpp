// cTSAudioEvent and nGZNumerics::AnyVector3 (lane IY): src/audio/cTSAudioEvent.h. /O1 code in the original (_EH_prolog).
#include "audio/cTSAudioEvent.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cIGZString.h"
#include "rz/RZFastFixedAllocators.h"
#include "rz/RZFrameworkHelpers.h" // RZUnknownSlot

#include <intrin.h>
#include <new>

// ---- nGZNumerics::AnyVector3 ----------------------------------------------------------------------------------------

namespace nGZNumerics {

bool AnyVector3::Assign(const cIGZAny &source)
{
    const bool same = source.GetType() == GetType();
    if (same) {
        const auto &other = static_cast<const AnyVector3 &>(source);
        mValue[0] = other.mValue[0];
        mValue[1] = other.mValue[1];
        mValue[2] = other.mValue[2];
    }
    return same;
}

// Not an ordering: -1 when any of other's components is above this one's, else 1 when any of this one's is above, else 0
// (NaN compares false). other is read as an AnyVector3 whatever its type.
int32_t AnyVector3::Compare(const cIGZAny &other) const
{
    const float *o = static_cast<const AnyVector3 &>(other).mValue;
    if (o[0] > mValue[0] || o[1] > mValue[1] || o[2] > mValue[2]) return -1;
    if (mValue[0] > o[0] || mValue[1] > o[1] || mValue[2] > o[2]) return 1;
    return 0;
}

cIGZAny *AnyVector3::Clone() const
{
    return new AnyVector3(*this);
}

uint32_t AnyVector3::GetType() const
{
    return kType;
}

const char *AnyVector3::GetTypeName() const
{
    return "AnyVector3";
}

} // namespace nGZNumerics

// ---- cTSAudioEvent --------------------------------------------------------------------------------------------------

namespace {

// The pool's lock (a cRZCriticalSection): slot 1 Lock, slot 2 Unlock, through its vtable as the original.
struct PoolLock {
    void *lock = GameAddress<void>(cTSAudioEvent::kPoolLock);
    PoolLock() { VirtualCall<int32_t(__thiscall *)(void *), 1>(lock); }
    ~PoolLock() { VirtualCall<int32_t(__thiscall *)(void *), 2>(lock); }
};

} // namespace

cTSAudioEvent::cTSAudioEvent(uint32_t eventId) : mEventId(eventId) {}

cTSAudioEvent::cTSAudioEvent() : mEventId(0) {}

cTSAudioEvent::~cTSAudioEvent()
{
    if (mObject) mObject->Release();
}

void *cTSAudioEvent::operator new(size_t size)
{
    if (size != kSize) return ::operator new(size);
    PoolLock lock;
    return GameAddress<RZFixedAllocatorBase>(kPool)->Alloc();
}

void cTSAudioEvent::operator delete(void *block, size_t size)
{
    if (!block) return;
    if (size != kSize) {
        ::operator delete(block);
        return;
    }
    PoolLock lock;
    GameAddress<RZFastFixed0x34>(kPool)->Free(block);
}

bool cTSAudioEvent::QueryInterface(uint32_t riid, void **out)
{
    switch (riid) {
    case 1:
    case cITSAudioEvent::kIid:
    case cITSAudioEvent::kSndEventIid:
        *out = static_cast<cITSAudioEvent *>(this);
        break;
    case cIGZMessage2Standard::kIid:
    case cIGZMessage2::kIid:
        *out = static_cast<cIGZMessage2Standard *>(this);
        break;
    case cGZPropertySetUint32::kIid:
        *out = static_cast<cGZPropertySetUint32 *>(this);
        break;
    default:
        return false;
    }
    AddRef();
    return true;
}

uint32_t cTSAudioEvent::AddRef()
{
    return uint32_t(_InterlockedIncrement(reinterpret_cast<volatile long *>(&mRefCount)));
}

uint32_t cTSAudioEvent::Release()
{
    if (_InterlockedDecrement(reinterpret_cast<volatile long *>(&mRefCount)) != 0) return mRefCount;
    delete static_cast<cGZPropertySetUint32 *>(this); // the set face's slot 13, as the original
    return 0;
}

// ---- cITSAudioEvent --------------------------------------------------------------------------------------------------

bool cTSAudioEvent::IsEvent()
{
    return true;
}

uint32_t cTSAudioEvent::GetEventId()
{
    return mEventId;
}

uint32_t cTSAudioEvent::SetEventId(uint32_t id)
{
    mEventId = id;
    return id;
}

cIGZMessage2Standard *cTSAudioEvent::AsMessage()
{
    return this;
}

// Slots 7 / 10 / 13 / 16 / 19 are one body in the original: slot 27 on the key.
bool cTSAudioEvent::HasFloat(uint32_t key)
{
    return HasProperty(&key);
}

bool cTSAudioEvent::HasUint32(uint32_t key)
{
    return HasProperty(&key);
}

bool cTSAudioEvent::HasSint32(uint32_t key)
{
    return HasProperty(&key);
}

bool cTSAudioEvent::HasVector3(uint32_t key)
{
    return HasProperty(&key);
}

bool cTSAudioEvent::HasText(uint32_t key)
{
    return HasProperty(&key);
}

float cTSAudioEvent::GetFloat(uint32_t key)
{
    gz::AnyFloat32 value(0.0f);
    GetPropertyValue(&key, &value);
    return value.mValue;
}

void cTSAudioEvent::SetFloat(uint32_t key, float value)
{
    gz::AnyFloat32 any(value);
    SetProperty(&key, &any);
}

uint32_t cTSAudioEvent::GetUint32(uint32_t key)
{
    gz::AnyUint32 value(0);
    GetPropertyValue(&key, &value);
    return value.mValue;
}

void cTSAudioEvent::SetUint32(uint32_t key, uint32_t value)
{
    gz::AnyUint32 any(value);
    SetProperty(&key, &any);
}

// AnySint32 is still the game's class: a value with its vtable.
int32_t cTSAudioEvent::GetSint32(uint32_t key)
{
    AnySint32 value{GameAddress<const void>(gzany::kSint32VTable), 0};
    GetPropertyValue(&key, &value);
    return value.mValue;
}

void cTSAudioEvent::SetSint32(uint32_t key, int32_t value)
{
    AnySint32 any{GameAddress<const void>(gzany::kSint32VTable), value};
    SetProperty(&key, &any);
}

bool cTSAudioEvent::GetVector3(uint32_t key, float *out)
{
    nGZNumerics::AnyVector3 value;
    const bool found = GetPropertyValue(&key, &value);
    if (found) {
        out[0] = value.mValue[0];
        out[1] = value.mValue[1];
        out[2] = value.mValue[2];
    }
    return found;
}

void cTSAudioEvent::SetVector3(uint32_t key, const float *value)
{
    nGZNumerics::AnyVector3 any(value);
    SetProperty(&key, &any);
}

bool cTSAudioEvent::GetText(uint32_t key, cIGZString *out)
{
    AnyString value{GameAddress<const void>(gzany::kStringVTable), nullptr};
    const bool found = GetPropertyValue(&key, &value);
    if (found) out->Copy(*value.mString);
    value.Destruct();
    return found;
}

void cTSAudioEvent::SetText(uint32_t key, const cIGZString *text)
{
    AnyString any;
    any.Construct(*text);
    SetProperty(&key, &any);
    any.Destruct();
}

bool cTSAudioEvent::GetFirst(uint32_t *key, void **value)
{
    return cGZPropertySetUint32::GetFirst(key, value);
}

bool cTSAudioEvent::GetNext(uint32_t *key, void **value)
{
    return cGZPropertySetUint32::GetNext(key, value);
}

bool cTSAudioEvent::SetProperty(const uint32_t *key, void *value)
{
    return cGZPropertySetUint32::SetProperty(key, value);
}

bool cTSAudioEvent::GetPropertyValue(const uint32_t *key, void *out)
{
    return cGZPropertySetUint32::GetPropertyValue(key, out);
}

void *cTSAudioEvent::GetProperty(const uint32_t *key)
{
    return cGZPropertySetUint32::GetProperty(key);
}

bool cTSAudioEvent::HasProperty(const uint32_t *key)
{
    return cGZPropertySetUint32::HasProperty(key);
}

uint32_t cTSAudioEvent::GetUserWord()
{
    return mUserWord;
}

uint32_t cTSAudioEvent::SetUserWord(uint32_t value)
{
    mUserWord = value;
    return value;
}

// ---- cIGZMessage2Standard --------------------------------------------------------------------------------------------

bool cTSAudioEvent::Create(uint32_t, void **)
{
    return false;
}

uint32_t cTSAudioEvent::GetType()
{
    return mMessageType;
}

uint32_t cTSAudioEvent::SetType(uint32_t type)
{
    mMessageType = type;
    return type;
}

// Both read other's type first, then this one's (each through its vtable).
bool cTSAudioEvent::IsEqual(cIGZMessage2 *other)
{
    const uint32_t theirs = other->GetType();
    return static_cast<cIGZMessage2 *>(this)->GetType() == theirs;
}

bool cTSAudioEvent::IsLess(cIGZMessage2 *other)
{
    const uint32_t theirs = other->GetType();
    return static_cast<cIGZMessage2 *>(this)->GetType() < theirs;
}

// The data words: uint32 properties under fixed keys, through the event face's slots 27 / 25 / 24.
bool cTSAudioEvent::HasKey(uint32_t key)
{
    return static_cast<cITSAudioEvent *>(this)->HasProperty(&key);
}

uint32_t cTSAudioEvent::GetWord(uint32_t key)
{
    gz::AnyUint32 value(0);
    static_cast<cITSAudioEvent *>(this)->GetPropertyValue(&key, &value);
    return value.mValue;
}

void cTSAudioEvent::SetWord(uint32_t key, uint32_t value)
{
    gz::AnyUint32 any(value);
    static_cast<cITSAudioEvent *>(this)->SetProperty(&key, &any);
}

bool cTSAudioEvent::HasData1() { return HasKey(kData1Key); }
uint32_t cTSAudioEvent::GetData1() { return GetWord(kData1Key); }
void *cTSAudioEvent::GetVoid1() { return reinterpret_cast<void *>(uintptr_t(GetWord(kData1Key))); }
void cTSAudioEvent::SetData1(uint32_t value) { SetWord(kData1Key, value); }
void cTSAudioEvent::SetVoid1(void *value) { SetWord(kData1Key, uint32_t(uintptr_t(value))); }
bool cTSAudioEvent::HasData2() { return HasKey(kData2Key); }
uint32_t cTSAudioEvent::GetData2() { return GetWord(kData2Key); }
void *cTSAudioEvent::GetVoid2() { return reinterpret_cast<void *>(uintptr_t(GetWord(kData2Key))); }
void cTSAudioEvent::SetData2(uint32_t value) { SetWord(kData2Key, value); }
void cTSAudioEvent::SetVoid2(void *value) { SetWord(kData2Key, uint32_t(uintptr_t(value))); }
bool cTSAudioEvent::HasData3() { return HasKey(kData3Key); }
uint32_t cTSAudioEvent::GetData3() { return GetWord(kData3Key); }
void *cTSAudioEvent::GetVoid3() { return reinterpret_cast<void *>(uintptr_t(GetWord(kData3Key))); }
void cTSAudioEvent::SetData3(uint32_t value) { SetWord(kData3Key, value); }
void cTSAudioEvent::SetVoid3(void *value) { SetWord(kData3Key, uint32_t(uintptr_t(value))); }
bool cTSAudioEvent::HasData4() { return HasKey(kData4Key); }
uint32_t cTSAudioEvent::GetData4() { return GetWord(kData4Key); }
void *cTSAudioEvent::GetVoid4() { return reinterpret_cast<void *>(uintptr_t(GetWord(kData4Key))); }
void cTSAudioEvent::SetData4(uint32_t value) { SetWord(kData4Key, value); }
void cTSAudioEvent::SetVoid4(void *value) { SetWord(kData4Key, uint32_t(uintptr_t(value))); }

bool cTSAudioEvent::HasString()
{
    return HasKey(kStringKey);
}

// ORIGINAL-BUG: the text is used unchecked; with no string under the key (or a null one) this calls through null.
bool cTSAudioEvent::GetString(uint32_t iid, void **out)
{
    const uint32_t key = kStringKey;
    AnyString value{GameAddress<const void>(gzany::kStringVTable), nullptr};
    static_cast<cITSAudioEvent *>(this)->GetPropertyValue(&key, &value);
    const bool result = value.mString->QueryInterface(iid, out);
    value.Destruct();
    return result;
}

void cTSAudioEvent::SetString(const cIGZString *text)
{
    if (!text) return;
    const uint32_t key = kStringKey;
    AnyString any;
    any.Construct(*text);
    static_cast<cITSAudioEvent *>(this)->SetProperty(&key, &any);
    any.Destruct();
}

bool cTSAudioEvent::HasIGZUnknown()
{
    return mObject != nullptr;
}

cIGZUnknown *cTSAudioEvent::GetIGZUnknown()
{
    return mObject;
}

void cTSAudioEvent::SetIGZUnknown(cIGZUnknown *object)
{
    reinterpret_cast<RZUnknownSlot *>(&mObject)->Assign(object);
}

bool cTSAudioEvent::HasExtra()
{
    return HasKey(kExtraKey);
}

// The extra object is kept as its address in a uint32 property (not counted: SetExtra releases the reference it took).
// ORIGINAL-BUG: the address is used unchecked; with nothing under the key this calls through null.
bool cTSAudioEvent::GetExtra(uint32_t iid, void **out)
{
    auto *object = reinterpret_cast<cIGZUnknown *>(uintptr_t(GetWord(kExtraKey)));
    return object->QueryInterface(iid, out);
}

void cTSAudioEvent::SetExtra(cIGZUnknown *object)
{
    cIGZUnknown *unknown = nullptr;
    if (object->QueryInterface(1, reinterpret_cast<void **>(&unknown))) SetWord(kExtraKey, uint32_t(uintptr_t(unknown)));
    if (unknown) unknown->Release();
}

// ---- hook entry points -----------------------------------------------------------------------------------------------

cTSAudioEvent *__fastcall cTSAudioEventAdapters::Construct(void *memory, void *, uint32_t eventId)
{
    return ::new (memory) cTSAudioEvent(eventId);
}

cTSAudioEvent *__fastcall cTSAudioEventAdapters::ConstructDefault(void *memory, void *)
{
    return ::new (memory) cTSAudioEvent();
}
