#include "world/cStringMap.h"

#include "game/vtable.h"
#include "gz/cIGZCOM.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "rz/Singleton_20ddfb.h" // GetCachedSingleton
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "rz/cRZString.h"

#include <cstring>
#include <new>

namespace nTSWorld {
namespace {
// A local cRZString of the original's, destroyed when the scope ends, also when a stream call throws (its EH frame).
struct RecordName {
    cRZString mString;
    RecordName() { mString.Construct(); }
    explicit RecordName(const char *text) { mString.Construct(text); }
    ~RecordName() { mString.Destruct(); }
    const char *Text() const { return mString.ToChar(); }
};

constexpr uint32_t kVersion = 1;

// The catalog entry a name is made into when the catalog lacks it (nTSMetaData's director, factory 0xc14f1b).
constexpr uint32_t kPatternEntryClass = 0x2ac90e94;
constexpr uint32_t kPatternEntryIid = 0xe9da450e;

// The entry's virtual base `index` through its vbtable pointer at +4: 1 is its IGZUnknown, 2 the base Read sets up.
void *VirtualBase(void *entry, int index)
{
    char *const vbptr = static_cast<char *>(entry) + 4;
    return vbptr + (*reinterpret_cast<const int32_t *const *>(vbptr))[index];
}

// The entry the framework makes, released (through its IGZUnknown) when the scope ends, also when a call throws.
struct NewEntry {
    void *mEntry = nullptr;
    NewEntry() = default;
    NewEntry(const NewEntry &) = delete;
    NewEntry &operator=(const NewEntry &) = delete;
    ~NewEntry()
    {
        if (mEntry) RawSlot<2>(VirtualBase(mEntry, 1));
    }
};
} // namespace

bool PatternNameLess::operator()(const char *a, const char *b) const
{
    if (!a || !b) return !a && b;
    return _stricmp(a, b) < 0;
}

cStringMap::cStringMap() {}

cStringMap::~cStringMap() = default;

bool cStringMap::QueryInterface(uint32_t riid, void **out)
{
    if (!out || riid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cStringMap::AddRef() { return ++mRefCount; }

uint32_t cStringMap::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

uint32_t cStringMap::GetGZCLSID() { return kClassId; }

uint16_t cStringMap::IdOf(const char *name)
{
    if (!name) return kNoId;
    const auto found = mUses.find(name);
    return found == mUses.end() ? kNoId : found->second.id;
}

const char *cStringMap::NameOf(const uint16_t *id)
{
    const auto found = mNames.find(*id);
    return found == mNames.end() ? nullptr : found->second;
}

uint16_t cStringMap::NextId()
{
    uint16_t next = mByteC == 0 ? 1 : 0;
    // ORIGINAL-BUG: every id up to the candidate moves it on, so ids below the start (0 when the byte is clear) skip one.
    for (auto it = mNames.begin(); it != mNames.end() && it->first <= next; ++it) ++next;
    return next;
}

bool cStringMap::Write(ifaces::cIGZOStream *stream)
{
    stream->SetUint32(GetGZCLSID());
    stream->SetUint32(kVersion);
    RecordName name("cStringMap"); // alive to the end, as the original's
    stream->SetGZStr(&name.mString);
    stream->SetSint32(static_cast<int32_t>(mUses.size()));
    for (const auto &[text, use] : mUses) {
        RecordName entry(text);
        stream->SetGZStr(&entry.mString);
        stream->SetUint16(use.id);
        stream->SetUint32(use.count);
    }
    return true;
}

bool cStringMap::Read(ifaces::cIGZIStream *stream)
{
    // The locals are the original's uninitialised ones (zero here): a failed read compares or stores what they held.
    uint32_t classId = 0;
    stream->GetUint32(&classId);
    if (classId != GetGZCLSID()) return false;
    int32_t version = 0;
    stream->GetSint32(&version);
    if (version != static_cast<int32_t>(kVersion)) return false;
    RecordName name; // alive to the end, as the original's
    stream->GetGZStr(&name.mString);
    mUses.clear();
    mNames.clear();
    void *const catalog = RawSlot<16, void *>(GetCachedSingleton());
    int32_t count = 0;
    stream->GetSint32(&count);
    // One id and one use count for the whole loop (the original's): a failed read keeps the previous entry's.
    uint16_t id = 0;
    uint32_t uses = 0;
    for (int32_t i = 0; i < count; ++i) {
        RecordName text;
        stream->GetGZStr(&text.mString);
        stream->GetUint16(&id);
        stream->GetUint32(&uses);
        const char *key = nullptr;
        if (void *const entry = RawSlot<42, void *>(catalog, mWord8, text.Text(), 0u)) {
            key = RawSlot<7, const char *>(entry);
        } else {
            // The catalog lacks the name: a new entry for it, filed under the table word, and its string is the key.
            NewEntry made;
            static_cast<cIGZCOM *>(GetDirectorFrameworkObject())->GetClassObject(kPatternEntryClass, kPatternEntryIid, &made.mEntry);
            const uint32_t table = mWord8;
            RawSlot<6>(VirtualBase(made.mEntry, 2), 1u);
            RawSlot<1>(VirtualBase(made.mEntry, 2), 0u);
            RawSlot<0>(made.mEntry, table != 0 ? 1u : 2u);
            RawSlot<1>(made.mEntry, text.Text());
            RawSlot<10>(catalog, mWord8, made.mEntry);
            key = RawSlot<7, const char *>(made.mEntry);
        }
        mUses[key].id = id;
        mUses[key].count = uses;
        mNames[id] = key;
    }
    return true;
}

// ---- the adapters ----

cStringMap *__fastcall cStringMapAdapters::Construct(Self *self, void *) { return new (self) Self(); }
cStringMap *__fastcall cStringMapAdapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    self->Self::~Self();
    if (flags & 1) ::operator delete(self, sizeof(Self));
    return self;
}
bool __fastcall cStringMapAdapters::Write(Self *self, void *, ifaces::cIGZOStream *stream)
{
    [[msvc::forceinline_calls]] return self->Self::Write(stream);
}
bool __fastcall cStringMapAdapters::Read(Self *self, void *, ifaces::cIGZIStream *stream)
{
    [[msvc::forceinline_calls]] return self->Self::Read(stream);
}
uint32_t __fastcall cStringMapAdapters::GetGZCLSID(Self *self, void *) { return self->Self::GetGZCLSID(); }
uint16_t __fastcall cStringMapAdapters::IdOf(Self *self, void *, const char *name) { return self->Self::IdOf(name); }
const char *__fastcall cStringMapAdapters::NameOf(Self *self, void *, const uint16_t *id) { return self->Self::NameOf(id); }
uint16_t __fastcall cStringMapAdapters::NextId(Self *self, void *) { return self->Self::NextId(); }

} // namespace nTSWorld
