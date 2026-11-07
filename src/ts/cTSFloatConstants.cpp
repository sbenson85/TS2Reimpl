#include "ts/cTSFloatConstants.h"

#include "game/vtable.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "ifaces/cIGZSerializable.h"
#include "ifaces/cITSObjectFileRef.h"
#include "ifaces/cITSPersistSystem.h"
#include "misc/UtilitiesC.h"
#include "rz/ServiceGetters.h"

#include <bit>
#include <new>

// ---- one entry ----

// An entry is 0, two empty strings (the shape the game's temporaries and the stream's fill value have).
cTSFloatConstantPair::cTSFloatConstantPair() : mValue(0.0f)
{
    mName.Construct();
    mDescription.Construct();
}

// 0x4df1c0 is the destructor body: the description then the name.
cTSFloatConstantPair::~cTSFloatConstantPair()
{
    mDescription.Destruct();
    mName.Destruct();
}

cTSFloatConstantPair::cTSFloatConstantPair(const cTSFloatConstantPair &other) : mValue(other.mValue)
{
    mName.Construct(other.mName, 0, UINT32_MAX);
    mDescription.Construct(other.mDescription, 0, UINT32_MAX);
}

// The shifts of Remove (0x4df760) copy the value and the text of both strings (SString::AssignText 0x26be10), nothing else.
void cTSFloatConstantPair::AssignContents(const cTSFloatConstantPair &other)
{
    mValue = other.mValue;
    mName.mString = other.mName.mString;
    mDescription.mString = other.mDescription.mString;
}

cTSFloatConstantPair &cTSFloatConstantPair::operator=(const cTSFloatConstantPair &other)
{
    AssignContents(other);
    return *this;
}

// 0x4df500: from the format version the stream's metadata holds (0x3f on) the name, the value and the description are
// record strings and a float; before it the name and (from version 1) the description are read by the stream's string
// reader (an interface the stream is asked for), the value from the stream itself. The result is "no stream error".
bool cTSFloatConstantPair::Read(ifaces::cIGZIStream *stream)
{
    // Slot 20 of the stream is its metadata object (the one Read of the array installed); its slot 78 points at the version.
    const uint32_t version = *RawSlot<78, const uint32_t *>(RawSlot<20, void *>(stream));
    if (version >= cTSFloatConstants::kNewStringVersion) {
        stream->GetGZStr(&mName);
        stream->GetFloat32(&mValue);
        stream->GetGZStr(&mDescription);
    } else {
        void *reader = nullptr;
        if (stream->QueryInterface(cTSFloatConstants::kStreamStringsId, &reader)) {
            RawSlot<20, uint32_t>(reader, &mName); // the reader's string slot (reads one string into the argument)
            stream->GetFloat32(&mValue);
            if (version >= 1) RawSlot<20, uint32_t>(reader, &mDescription);
        }
        if (reader) As<cIGZUnknown>(reader)->Release();
    }
    return stream->GetError() == 0;
}

// 0x4dfa90
bool cTSFloatConstantPair::Write(ifaces::cIGZOStream *stream)
{
    stream->SetGZStr(&mName);
    stream->SetFloat32(mValue);
    stream->SetGZStr(&mDescription);
    return stream->GetError() == 0;
}

// ---- construction and destruction ----

// 0x4df0c0: the compiler installs the vtable; the padding bytes at +9 are left as they were.
cTSFloatConstants::cTSFloatConstants() : mRefCount(0), mDirty(0), mEntries() {}

// The body 0x4df260 (the vtable, then the entries' destruction) and slot 19's scalar deleting form 0x4df2a0.
cTSFloatConstants::~cTSFloatConstants() = default;

// 0x4df2d0
cTSFloatConstants *cTSFloatConstants::Create()
{
    return new (std::nothrow) cTSFloatConstants;
}

// ---- cIGZUnknown ----

// 0x4df4d0
bool cTSFloatConstants::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1 && riid != kInterfaceId) return false;
    *out = static_cast<cITSFloatConstants *>(this);
    AddRef();
    return true;
}

// slot 1, folded 0x148fb0
uint32_t cTSFloatConstants::AddRef()
{
    return ++mRefCount;
}

// slot 2, folded 0x44cc60: the object goes at the last reference (or when the count is 0)
uint32_t cTSFloatConstants::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete this;
    return 0;
}

// slot 18, 0x4df870
uint32_t cTSFloatConstants::DropReference()
{
    return --mRefCount;
}

// ---- cIGZSerializable ----

namespace {
// The stream's metadata object the format version travels in (UtilObjectB, 0x14 bytes), given to the stream (SetMetadata
// keeps it). A failed allocation hands the stream a null.
UtilObjectB *NewMetadata(uint32_t version)
{
    auto *metadata = static_cast<UtilObjectB *>(::operator new(sizeof(UtilObjectB)));
    if (metadata) metadata = metadata->Construct(version);
    return metadata;
}
} // namespace

// 0x4dfae0: metadata (0x55), class id, 0x55, 0, the count, then each entry's own record.
bool cTSFloatConstants::Write(ifaces::cIGZOStream *stream)
{
    stream->SetMetadata(NewMetadata(kWriteVersion));
    stream->SetUint32(GetGZCLSID());
    stream->SetUint32(kWriteVersion);
    stream->SetUint32(0);
    const uint32_t count = static_cast<uint32_t>(mEntries.size());
    stream->SetUint32(count);
    for (uint32_t i = 0; i < count; ++i) mEntries[i].Write(stream);
    return stream->GetError() == 0;
}

// 0x4df5e0: a word (the class id, ignored), the version, a word, the metadata object for that version, the count; the
// entries are dropped (the dirty byte untouched) and that many empty ones read their records. A read that fails leaves
// its word as it was (a stack word in the original; here zero).
bool cTSFloatConstants::Read(ifaces::cIGZIStream *stream)
{
    uint32_t first = 0, version = 0, count = 0;
    stream->GetUint32(&first);
    stream->GetUint32(&version);
    stream->GetUint32(&first);
    stream->SetMetadata(NewMetadata(version));
    stream->GetUint32(&count);
    mEntries.clear();
    mEntries.insert(mEntries.end(), count, cTSFloatConstantPair());
    for (uint32_t i = 0; i < count; ++i) mEntries[i].Read(stream);
    return stream->GetError() == 0;
}

// 0x4df3f0
uint32_t cTSFloatConstants::GetGZCLSID()
{
    return kClassId;
}

// ---- the table ----

// The first entry whose name equals the text, case-insensitively (the string's slot 10, caseSensitive false).
cTSFloatConstantPair *cTSFloatConstants::Find(const char *name)
{
    for (cTSFloatConstantPair &entry : mEntries)
        if (entry.mName.IsEqual(name, false)) return &entry;
    return nullptr;
}

// 0x4df400
const char *cTSFloatConstants::GetName(int32_t index)
{
    if (static_cast<uint32_t>(index) >= mEntries.size()) return nullptr; // (a negative index is a huge unsigned one)
    return mEntries[static_cast<uint32_t>(index)].mName.ToChar();
}

// 0x4df320: a missing name is added with the default (through SetFloat, slot 8) when asked to; the default is the result.
float cTSFloatConstants::GetFloat(const char *name, float defaultValue, bool add)
{
    if (const cTSFloatConstantPair *entry = Find(name)) return entry->mValue;
    if (add) SetFloat(name, defaultValue);
    return defaultValue;
}

// 0x4df8c0: a different value (or any NaN) is stored and marks the table dirty; a new name appends an entry with no
// description (dirty as well). An equal value changes nothing.
void cTSFloatConstants::SetFloat(const char *name, float value)
{
    if (cTSFloatConstantPair *entry = Find(name)) {
        if (!(entry->mValue == value)) {
            entry->mValue = value;
            mDirty = 1;
        }
        return;
    }
    cTSFloatConstantPair fresh;
    fresh.mName.FromChar(name);
    fresh.mValue = value;
    mEntries.push_back(fresh);
    mDirty = 1;
}

// 0x4df380
const char *cTSFloatConstants::GetDescription(const char *name)
{
    const cTSFloatConstantPair *entry = Find(name);
    return entry ? entry->mDescription.ToChar() : nullptr;
}

// 0x4dfa30: the dirty byte is not touched.
void cTSFloatConstants::SetDescription(const char *name, const char *text)
{
    if (cTSFloatConstantPair *entry = Find(name)) entry->mDescription.FromChar(text);
}

// 0x4df460
bool cTSFloatConstants::Has(const char *name)
{
    return Find(name) != nullptr;
}

// 0x4df760: every entry with the name goes (the ones after it shift down by copying value and texts, the last is destroyed),
// the table is marked dirty for each.
void cTSFloatConstants::Remove(const char *name)
{
    for (size_t i = 0; i != mEntries.size();) {
        if (mEntries[i].mName.IsEqual(name, false)) {
            for (size_t j = i + 1; j < mEntries.size(); ++j) mEntries[j - 1].AssignContents(mEntries[j]);
            mEntries.pop_back();
            mDirty = 1;
        } else {
            ++i;
        }
    }
}

// 0x4df840
void cTSFloatConstants::Clear()
{
    mEntries.clear();
    mDirty = 1;
}

// ---- the resource service ----

namespace {
ifaces::cITSPersistSystem *PersistSystem()
{
    return As<ifaces::cITSPersistSystem>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>());
}
} // namespace

// 0x4df4a0: the table is emptied (slot 13), then the service reads the resource of the segment into it; clean afterwards.
void cTSFloatConstants::Load(void *segment, uint32_t instance)
{
    Clear();
    PersistSystem()->ReadResourceFromSegment(static_cast<cIGZSerializable *>(this), segment, instance, 0); // slot 73 takes the resource as a plain pointer
    mDirty = 0;
}

// 0x4df880: only into a file reference that says (its slot 9) it can be written.
void cTSFloatConstants::Save(void *fileRef, uint32_t instance)
{
    if (!fileRef) return;
    if (!RawSlot<9, bool>(fileRef)) return;
    PersistSystem()->WriteResourceToFileRef(reinterpret_cast<ifaces::cIGZSerializable *>(static_cast<cIGZSerializable *>(this)),
                                            static_cast<ifaces::cITSObjectFileRef *>(fileRef), instance);
}

// slots 16 and 17 (folded 0x1480a0 / 0x1489f0): the byte as it is
bool cTSFloatConstants::IsDirty()
{
    return std::bit_cast<bool>(mDirty);
}

void cTSFloatConstants::SetDirty(bool dirty)
{
    mDirty = std::bit_cast<uint8_t>(dirty);
}

// ---- the RVA hooks' adapters ----
// Qualified calls (no virtual dispatch), bodies forced inline: each adapter is the original entry point itself.

using Adapters = cTSFloatConstantsAdapters;

cTSFloatConstants *__fastcall Adapters::Construct(void *memory, void *)
{
    return ::new (memory) cTSFloatConstants;
}

void __fastcall Adapters::Destruct(Self *self, void *)
{
    self->Self::~cTSFloatConstants();
}

float __fastcall Adapters::GetFloat(Self *self, void *, const char *name, float defaultValue, bool add)
{
    [[msvc::forceinline_calls]] return self->Self::GetFloat(name, defaultValue, add);
}

void __fastcall Adapters::Remove(Self *self, void *, const char *name)
{
    [[msvc::forceinline_calls]] self->Self::Remove(name);
}

void __fastcall Adapters::Load(Self *self, void *, void *segment, uint32_t instance)
{
    [[msvc::forceinline_calls]] self->Self::Load(segment, instance);
}

void __fastcall Adapters::Save(Self *self, void *, void *fileRef, uint32_t instance)
{
    [[msvc::forceinline_calls]] self->Self::Save(fileRef, instance);
}
