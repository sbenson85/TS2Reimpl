// The property-set core: cGZPropertySetT<uint32 keys> and cGZPropertySetString (lane DP; structure: lane DM).
// /O1 code in the original (0x1be000-0x1bf900), hence a .size.cpp.
#include "gz/cGZPropertySet.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"
#include "gz/cIGZString.h"
#include "ifaces/cIGZAny.h"
#include "ifaces/cIGZPropertySetString.h"
#include "ifaces/cIGZPropertySetUint32.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace {

constexpr uint32_t kIGZUnknownIid = 1;

// The cIGZAny calls the set makes, through the value's own vtable (interface facts: lane DQ, db/methods/cGZAny.jsonl).
uint32_t AnyGetType(void *any) { return As<ifaces::cIGZAny>(any)->GetType(); }
void *AnyClone(void *any) { return As<ifaces::cIGZAny>(any)->Clone(); }
// Assign's result is al (1 copied, 0 refused): the byte is what the set passes on.
bool AnyAssign(void *any, void *source) { return static_cast<uint8_t>(As<ifaces::cIGZAny>(any)->Assign(source)) != 0; }
void AnyDelete(void *any, uint32_t flags) { As<ifaces::cIGZAny>(any)->DeletingDestructor(flags); }

} // namespace

// ---- cGZPropertyEntry ----------------------------------------------------------------------------------------

cGZPropertyEntry *cGZPropertyEntry::Construct(uint32_t key, void *value)
{
    mKey = key;
    const InlineAny *scalar = static_cast<const InlineAny *>(value);
    switch (AnyGetType(value)) {
    case gzany::kTypeString:
        // AnyString's copy constructor shares the source's text object (AddRef).
        reinterpret_cast<AnyString *>(&mValue)->Construct(*static_cast<const AnyString *>(value));
        break;
    case gzany::kTypeSint32:
        mValue.mVTable = gzany::Sint32VTable();
        mValue.mValue = scalar->mValue;
        break;
    case gzany::kTypeFloat32:
        mValue.mVTable = gzany::Float32VTable();
        mValue.mValue = scalar->mValue;
        break;
    case gzany::kTypeBoolean:
        // Only the low byte is copied: the other three payload bytes keep what the entry's memory held.
        mValue.mVTable = gzany::BooleanVTable();
        *reinterpret_cast<uint8_t *>(&mValue.mValue) = *reinterpret_cast<const uint8_t *>(&scalar->mValue);
        break;
    case gzany::kTypeUint32:
        mValue.mVTable = gzany::Uint32VTable();
        mValue.mValue = scalar->mValue;
        break;
    default: {
        // Any other type is held as a clone inside an AnyAny.
        void *clone = AnyClone(value);
        mValue.mVTable = gzany::AnyAnyVTable();
        mValue.mValue = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(clone));
        break;
    }
    }
    return this;
}

void *cGZPropertyEntry::Value()
{
    void *stored = &mValue;
    if (AnyGetType(stored) == gzany::kTypeAnyAny) stored = reinterpret_cast<void *>(static_cast<uintptr_t>(mValue.mValue));
    return stored;
}

void cGZPropertyEntry::Destroy()
{
    if (AnyGetType(&mValue) == gzany::kTypeAnyAny) {
        if (void *held = reinterpret_cast<void *>(static_cast<uintptr_t>(mValue.mValue))) AnyDelete(held, 1);
    }
    AnyDelete(&mValue, 0);
}

// ---- gz::cGZPropertyTable: the bodies both sets run -----------------------------------------------------------

namespace gz {

cGZPropertyEntry *cGZPropertyTable::Find(uint32_t key)
{
    auto at = std::lower_bound(mEntries.begin(), mEntries.end(), key,
                               [](const cGZPropertyEntry &entry, uint32_t k) { return entry.mKey < k; });
    return at != mEntries.end() && at->mKey == key ? &*at : nullptr;
}

bool cGZPropertyTable::SetEntry(uint32_t key, void *value)
{
    auto at = std::lower_bound(mEntries.begin(), mEntries.end(), key,
                               [](const cGZPropertyEntry &entry, uint32_t k) { return entry.mKey < k; });
    // An existing key takes the new value through Assign, which refuses another type (result 0).
    if (at != mEntries.end() && at->mKey == key) return AnyAssign(at->Value(), value);
    // A new key: an entry built on the stack (its last dword is never written) is copied in as 16 raw bytes.
    cGZPropertyEntry entry;
    entry.Construct(key, value);
    mEntries.emplace(at, entry);
    return true;
}

bool cGZPropertyTable::RemoveEntry(uint32_t key)
{
    cGZPropertyEntry *entry = Find(key);
    if (!entry) return false;
    entry->Destroy();
    mEntries.erase(mEntries.begin() + (entry - mEntries.data()));
    return true;
}

void cGZPropertyTable::ClearEntries()
{
    // The cursor is left as it was (pointing into the emptied vector).
    for (cGZPropertyEntry &entry : mEntries) entry.Destroy();
    mEntries.clear();
}

bool cGZPropertyTable::FirstEntry(uint32_t *key, void **value)
{
    if (!key || !value) return false;
    mCursor = mEntries.data();
    if (mCursor == mEntries.data() + mEntries.size()) return false;
    *key = mCursor->mKey;
    *value = mCursor->Value();
    return true;
}

bool cGZPropertyTable::NextEntry(uint32_t *key, void **value)
{
    if (!key || !value) return false;
    // ORIGINAL-BUG: the cursor steps before the end test, so GetNext once the cursor is at the end (an empty set, or
    // after a GetNext that returned 0) reads past the entries; so does a cursor left stale by Clear or a reallocation.
    ++mCursor;
    if (mCursor == mEntries.data() + mEntries.size()) return false;
    *key = mCursor->mKey;
    *value = mCursor->Value();
    return true;
}

void *cGZPropertyTable::EntryValue(uint32_t key)
{
    cGZPropertyEntry *entry = Find(key);
    return entry ? entry->Value() : nullptr;
}

bool cGZPropertyTable::EntryValueInto(uint32_t key, void *out)
{
    cGZPropertyEntry *entry = Find(key);
    return entry ? AnyAssign(out, entry->Value()) : false;
}

} // namespace gz

// ---- cGZPropertySetUint32 ------------------------------------------------------------------------------------

// 0x1bed39 (the compiler's vtable store first; the entries' storage is the base's member, freed after Clear).
cGZPropertySetUint32::~cGZPropertySetT()
{
    ClearEntries();
}

bool cGZPropertySetUint32::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kIGZUnknownIid && riid != kIid) return false;
    *out = this;
    AddRef(); // through the vtable, as the original
    return true;
}

uint32_t cGZPropertySetUint32::AddRef()
{
    return ++mRefCount;
}

uint32_t cGZPropertySetUint32::Release()
{
    // Unsigned compare (ja): a count of 0 or 1 deletes through the object's own destructor slot.
    if (mRefCount <= 1) {
        delete this;
        return 0;
    }
    return --mRefCount;
}

bool cGZPropertySetUint32::SetProperty(const uint32_t *key, void *value) { return SetEntry(*key, value); }
bool cGZPropertySetUint32::RemoveProperty(const uint32_t *key) { return RemoveEntry(*key); }
void cGZPropertySetUint32::Clear() { ClearEntries(); }
bool cGZPropertySetUint32::GetFirst(uint32_t *key, void **value) { return FirstEntry(key, value); }
bool cGZPropertySetUint32::GetNext(uint32_t *key, void **value) { return NextEntry(key, value); }
void *cGZPropertySetUint32::GetProperty(const uint32_t *key) { return EntryValue(*key); }
bool cGZPropertySetUint32::GetPropertyValue(const uint32_t *key, void *out) { return EntryValueInto(*key, out); }
bool cGZPropertySetUint32::HasProperty(const uint32_t *key) { return Find(*key) != nullptr; }
uint32_t cGZPropertySetUint32::GetCount() { return EntryCount(); }

void cGZPropertySetUint32::CopyFrom(cIGZPropertySetUint32 *other)
{
    // All through the vtables: this set's Clear and SetProperty, the source's GetFirst / GetNext.
    Clear();
    uint32_t key;
    void *value;
    for (bool more = other->GetFirst(&key, &value); more; more = other->GetNext(&key, &value)) SetProperty(&key, value);
}

const char *cGZPropertySetUint32::KeyToString(uint32_t key, cIGZString *out) { return FormatKey(&key, out); }

const char *__cdecl cGZPropertySetUint32::FormatKey(const uint32_t *key, cIGZString *out)
{
    out->Sprintf(GameAddress<const char>(0xda3e60), *key);
    return out->ToChar();
}

// ---- cGZPropertySetString ------------------------------------------------------------------------------------

// 0x1777e0: the key text first, then the base part (0x1bed22).
cGZPropertySetString::~cGZPropertySetString()
{
    mKeyText.~vector();
    ClearEntries();
}

void cGZPropertySetString::DestroyEntries()
{
    ClearEntries();
    mEntries.~EntryVector();
}

bool cGZPropertySetString::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kIGZUnknownIid && riid != kIid) return false;
    *out = this;
    AddRef(); // through the vtable, as the original
    return true;
}

uint32_t cGZPropertySetString::AddRef()
{
    return ++mRefCount;
}

// The uint32 set's body (one folded function 0x1bf485): a count of 0 or 1 deletes through slot 13.
uint32_t cGZPropertySetString::Release()
{
    if (mRefCount <= 1) {
        delete this;
        return 0;
    }
    return --mRefCount;
}

uint32_t cGZPropertySetString::FindKey(const GZServiceString *key)
{
    const char *text = key->c_str();
    const size_t length = key->size();
    char *begin = mKeyText.data();
    char *end = begin + mKeyText.size();
    for (char *stored = begin; stored != end; stored += std::strlen(stored) + 1) {
        if (std::strlen(stored) == length && _memicmp(stored, text, length) == 0) return static_cast<uint32_t>(stored - begin);
    }
    return kNoKey;
}

uint32_t cGZPropertySetString::InternKey(const GZServiceString *key)
{
    const uint32_t found = FindKey(key);
    if (found != kNoKey) return found;
    // A new key: its text and terminator go on the end.
    const uint32_t offset = static_cast<uint32_t>(mKeyText.size());
    const char *text = key->c_str();
    mKeyText.insert(mKeyText.end(), text, text + key->size() + 1);
    return offset;
}

bool cGZPropertySetString::SetProperty(const GZServiceString *key, void *value)
{
    return SetEntry(InternKey(key), value);
}

bool cGZPropertySetString::RemoveProperty(const GZServiceString *key)
{
    const uint32_t offset = FindKey(key);
    if (offset == kNoKey) return false;
    // Text without an entry (left by Clear) stays.
    if (!RemoveEntry(offset)) return false;
    const size_t removed = std::strlen(mKeyText.data() + offset) + 1;
    mKeyText.erase(mKeyText.begin() + offset, mKeyText.begin() + offset + removed);
    // The texts after it moved down: so do their entries' offsets (the order is unchanged).
    for (cGZPropertyEntry &entry : mEntries) {
        if (entry.mKey > offset) entry.mKey -= static_cast<uint32_t>(removed);
    }
    return true;
}

void cGZPropertySetString::Clear() { ClearEntries(); }

void cGZPropertySetString::CopyKeyText(uint32_t offset, GZServiceString *key)
{
    // Through a temporary, as the original (0x1bec0e construct, 0x1bed67 assign, 0x1bf753 tidy).
    const GZServiceString text(mKeyText.data() + offset);
    *key = text;
}

bool cGZPropertySetString::GetFirst(GZServiceString *key, void **value)
{
    if (!key || !value) return false;
    uint32_t offset;
    if (!FirstEntry(&offset, value)) return false;
    CopyKeyText(offset, key);
    return true;
}

bool cGZPropertySetString::GetNext(GZServiceString *key, void **value)
{
    if (!key || !value) return false;
    uint32_t offset;
    if (!NextEntry(&offset, value)) return false;
    CopyKeyText(offset, key);
    return true;
}

void *cGZPropertySetString::GetProperty(const GZServiceString *key)
{
    const uint32_t offset = FindKey(key);
    return offset == kNoKey ? nullptr : EntryValue(offset);
}

bool cGZPropertySetString::GetPropertyValue(const GZServiceString *key, void *out)
{
    const uint32_t offset = FindKey(key);
    return offset == kNoKey ? false : EntryValueInto(offset, out);
}

bool cGZPropertySetString::HasProperty(const GZServiceString *key)
{
    const uint32_t offset = FindKey(key);
    return offset != kNoKey && Find(offset) != nullptr;
}

uint32_t cGZPropertySetString::GetCount() { return EntryCount(); }

void cGZPropertySetString::CopyFrom(cIGZPropertySetString *other)
{
    // All through the vtables: this set's Clear and SetProperty, the source's GetFirst / GetNext.
    Clear();
    GZServiceString key;
    void *value;
    for (bool more = other->GetFirst(&key, &value); more; more = other->GetNext(&key, &value)) SetProperty(&key, value);
}

const char *cGZPropertySetString::KeyToString(uint32_t keyOffset, cIGZString *out)
{
    out->FromChar(mKeyText.data() + keyOffset);
    return out->ToChar();
}

// ---- the RVA hooks' adapters (qualified calls, bodies inlined) -------------------------------------------------

using U32Adapters = cGZPropertySetUint32Adapters;

U32Adapters::Self *__fastcall U32Adapters::Construct(void *memory, void *) { return ::new (memory) Self; }
void __fastcall U32Adapters::Destruct(Self *self, void *) { [[msvc::forceinline_calls]] self->Self::~Self(); }
U32Adapters::Self *__fastcall U32Adapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    [[msvc::forceinline_calls]] self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}
bool __fastcall U32Adapters::QueryInterface(Self *self, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::QueryInterface(riid, out);
}
bool __fastcall U32Adapters::SetProperty(Self *self, void *, const uint32_t *key, void *value)
{
    [[msvc::forceinline_calls]] return self->Self::SetProperty(key, value);
}
bool __fastcall U32Adapters::RemoveProperty(Self *self, void *, const uint32_t *key)
{
    [[msvc::forceinline_calls]] return self->Self::RemoveProperty(key);
}
bool __fastcall U32Adapters::GetFirst(Self *self, void *, uint32_t *key, void **value)
{
    [[msvc::forceinline_calls]] return self->Self::GetFirst(key, value);
}
bool __fastcall U32Adapters::GetNext(Self *self, void *, uint32_t *key, void **value)
{
    [[msvc::forceinline_calls]] return self->Self::GetNext(key, value);
}
void *__fastcall U32Adapters::GetProperty(Self *self, void *, const uint32_t *key)
{
    [[msvc::forceinline_calls]] return self->Self::GetProperty(key);
}
bool __fastcall U32Adapters::GetPropertyValue(Self *self, void *, const uint32_t *key, void *out)
{
    [[msvc::forceinline_calls]] return self->Self::GetPropertyValue(key, out);
}
bool __fastcall U32Adapters::HasProperty(Self *self, void *, const uint32_t *key)
{
    [[msvc::forceinline_calls]] return self->Self::HasProperty(key);
}
void __fastcall U32Adapters::CopyFrom(Self *self, void *, cIGZPropertySetUint32 *other)
{
    [[msvc::forceinline_calls]] self->Self::CopyFrom(other);
}

using StrAdapters = cGZPropertySetStringAdapters;

StrAdapters::Self *__fastcall StrAdapters::Construct(void *memory, void *) { return ::new (memory) Self; }
void __fastcall StrAdapters::Destruct(Self *self, void *) { [[msvc::forceinline_calls]] self->Self::~Self(); }
StrAdapters::Self *__fastcall StrAdapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    [[msvc::forceinline_calls]] self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}
void __fastcall StrAdapters::DestroyEntries(Self *self, void *) { [[msvc::forceinline_calls]] self->DestroyEntries(); }
StrAdapters::Self *__fastcall StrAdapters::DeletingDestructorBase(Self *self, void *, uint32_t flags)
{
    [[msvc::forceinline_calls]] self->DestroyEntries();
    if (flags & 1) ::operator delete(self, sizeof(gz::cGZPropertyTable) + sizeof(void *)); // the RTTI base's size, 0x18
    return self;
}
bool __fastcall StrAdapters::QueryInterface(Self *self, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::QueryInterface(riid, out);
}
bool __fastcall StrAdapters::SetProperty(Self *self, void *, const GZServiceString *key, void *value)
{
    [[msvc::forceinline_calls]] return self->Self::SetProperty(key, value);
}
bool __fastcall StrAdapters::RemoveProperty(Self *self, void *, const GZServiceString *key)
{
    [[msvc::forceinline_calls]] return self->Self::RemoveProperty(key);
}
bool __fastcall StrAdapters::GetFirst(Self *self, void *, GZServiceString *key, void **value)
{
    [[msvc::forceinline_calls]] return self->Self::GetFirst(key, value);
}
bool __fastcall StrAdapters::GetNext(Self *self, void *, GZServiceString *key, void **value)
{
    [[msvc::forceinline_calls]] return self->Self::GetNext(key, value);
}
void *__fastcall StrAdapters::GetProperty(Self *self, void *, const GZServiceString *key)
{
    [[msvc::forceinline_calls]] return self->Self::GetProperty(key);
}
bool __fastcall StrAdapters::GetPropertyValue(Self *self, void *, const GZServiceString *key, void *out)
{
    [[msvc::forceinline_calls]] return self->Self::GetPropertyValue(key, out);
}
bool __fastcall StrAdapters::HasProperty(Self *self, void *, const GZServiceString *key)
{
    [[msvc::forceinline_calls]] return self->Self::HasProperty(key);
}
void __fastcall StrAdapters::CopyFrom(Self *self, void *, cIGZPropertySetString *other)
{
    [[msvc::forceinline_calls]] self->Self::CopyFrom(other);
}
const char *__fastcall StrAdapters::KeyToString(Self *self, void *, uint32_t keyOffset, cIGZString *out)
{
    [[msvc::forceinline_calls]] return self->Self::KeyToString(keyOffset, out);
}
uint32_t __fastcall StrAdapters::InternKey(Self *self, void *, const GZServiceString *key)
{
    [[msvc::forceinline_calls]] return self->InternKey(key);
}
uint32_t __fastcall StrAdapters::FindKey(Self *self, void *, const GZServiceString *key)
{
    [[msvc::forceinline_calls]] return self->FindKey(key);
}

