// nTSSG::cTSSGSystem: resource lookups and the save state (lane IR). See src/sg/cTSSGSystem.h.
#include "sg/cTSSGSystem.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "rz/StringKeyParser.h"
#include "rz/cRZString.h"

#include <cstring>
#include <new>
#include <string>

namespace {

template <int kSlot, typename R = uint32_t, typename... A> __forceinline R Call(const void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), kSlot>(object, args...);
}

void *VirtualBase(void *object, uint32_t vbptr, int entry)
{
    auto *at = static_cast<uint8_t *>(object) + vbptr;
    const int32_t *table = *reinterpret_cast<int32_t *const *>(at);
    return at + table[entry];
}

// The object's own slots the code below reaches through its vtable.
enum : int {
    kSelfFindAnim = 25,
    kSelfClockTarget = 52,
    kSelfLoadEntry = 76,
    kSelfSaveEntry = 77,
    kSelfReadState = 96,
    kSelfWriteState = 97,
    kSelfFindPersist = 98,
};

// The save state's interface id and the stream type the persist service stores it as.
constexpr uint32_t kStateIid = 0xba353ce1;
constexpr uint32_t kStateStreamType = 0x3b2271c0;
constexpr uint32_t kStateVersion = 4;
// The class name written into the state (game text, 11 characters).
constexpr uint32_t kClassName = 0xdaf02c;
constexpr uint32_t kClassNameLength = 11;
// The three entries Save and Load handle, in order (game text).
constexpr uint32_t kEntryNames[] = {0xdae79c, 0xda62fc, 0xdae7a4};
// The neighbourhood animation record: the name the group comes from (game text, 19 characters) and the key type.
constexpr uint32_t kNeighborhoodName = 0xdaefdc;
constexpr uint32_t kNeighborhoodNameLength = 19;
constexpr uint32_t kNeighborhoodKeyType = 0x8cc0a14b;
// Entry records: the group-key type, the record type, the interfaces the entries read and write.
constexpr uint32_t kEntryKeyType = 0xec44bddc;
constexpr uint32_t kEntryRecordType = 0x656b8f0c;
constexpr uint32_t kEntryReadIid = 0x051b11bd;
constexpr uint32_t kEntryWriteIid = 0xc51b11cb;
// The persist-service resource key: two fixed words before the caller's two.
constexpr uint32_t kResourceKey0 = 0xfb00791e;
constexpr uint32_t kResourceKey1 = 0x1c0532fa;
constexpr uint32_t kResourceType = 0x7b007916;

// Stream slots: 8 and 9 read or write a dword (by address when reading), 15 a cIGZString.
constexpr int kStreamDword8 = 8;
constexpr int kStreamDword = 9;
constexpr int kStreamGroup = 11;
constexpr int kStreamString = 15;

// An interface reference released (slot 2) when it goes.
struct Held {
    void *object = nullptr;
    Held() = default;
    Held(const Held &) = delete;
    Held &operator=(const Held &) = delete;
    ~Held()
    {
        if (object) Call<2>(object);
    }
};

// A cRZString on the stack, as the original builds one (game vtable, reference count 0).
struct LocalString {
    cRZString value;
    explicit LocalString(const char *text = "", size_t length = 0)
    {
        value.mVTable = cRZString::VTable();
        ::new (&value.mString) std::string(text, length);
        value.mRefCount = 0;
    }
    LocalString(const LocalString &) = delete;
    LocalString &operator=(const LocalString &) = delete;
    ~LocalString() { value.mString.~basic_string(); }
};

// The entry `name` of the object list (mRefB4: slot 35 its index, slot 36 the entry), and its record key.
void *FindEntry(void *list, const char *name)
{
    return Call<36, void *>(list, Call<35>(list, name));
}
void EntryKey(uint32_t key[4], void *entry, uint32_t group)
{
    key[0] = kEntryKeyType;
    key[1] = group;
    key[2] = StringKeyParser::Crc24(Call<12, const char *>(entry), 1) & 0xffff;
    key[3] = 0;
}

// The object slot 52 gives: its virtual base (table at +4, entry 2) takes the animation clock.
void *ClockTarget(void *self) { return VirtualBase(Call<kSelfClockTarget, void *>(self), 4, 2); }

// Releases a resource held through its virtual base (table at +4, entry 1).
void ReleaseResource(void *object) { Call<2>(VirtualBase(object, 4, 1)); }

} // namespace

namespace nTSSG {

bool cTSSGSystem::FindAnimResource(const uint32_t *key, const char *name, uint32_t arg3, void **out, uint8_t force)
{
    const bool looked = Call<17, uint8_t>(mAnimSystem.mObject, key, name, out) != 0;
    if (*out) {
        if (Call<59, uint8_t>(*out)) ++mCacheHitsSlot59[mStatBucket];
        else ++mCacheHits[mStatBucket];
    } else if (!looked || force) {
        if (!Call<kSelfFindPersist, uint8_t>(this, key, name, out)) Call<14>(mAnimSystem.mObject, key);
    }
    if (*out) Call<15>(mAnimSystem.mObject, *out, arg3);
    return *out != nullptr;
}

bool cTSSGSystem::FindPersistResource(const uint32_t *key, const char *name, void **out)
{
    const uint32_t start = mStartupWatch.Elapsed();
    uint32_t fullKey[4] = {kResourceKey0, kResourceKey1, key[0], key[1]};
    uint8_t flagged;
    Call<7>(mPersistService.mObject, fullKey, kResourceType, out, &flagged);
    const uint32_t spent = mStartupWatch.Elapsed() - start;
    if (!*out) return false;
    if (name && _stricmp(name, Call<17, const char *>(VirtualBase(*out, 4, 1))) != 0) {
        *out = nullptr;
        return false;
    }
    if (flagged) {
        ++mPersistHitsFlagged[mStatBucket];
        mPersistTimeFlagged[mStatBucket] += spent;
    } else {
        ++mPersistHits[mStatBucket];
        mPersistTime[mStatBucket] += spent;
    }
    return *out != nullptr;
}

bool cTSSGSystem::LoadEntry(uint32_t group, const char *name)
{
    if (!mRefB4.mObject) return false;
    void *entry = FindEntry(mRefB4.mObject, name);
    if (!entry) return false;
    uint32_t key[4];
    EntryKey(key, entry, group);
    bool loaded = false;
    Held record;
    void *persist = Call<3, void *>(mPersistService.mObject);
    if (Call<39, uint8_t>(persist, key, &record.object, 0u)) {
        Held data;
        if (Call<0, uint8_t>(record.object, kEntryReadIid, &data.object)) {
            const uint8_t read = Call<11, uint8_t>(entry, data.object);
            loaded = (read & Call<6, uint8_t>(record.object)) != 0;
        }
    }
    return loaded;
}

bool cTSSGSystem::SaveEntry(uint32_t group, const char *name)
{
    if (!mRefB4.mObject) return false;
    void *entry = FindEntry(mRefB4.mObject, name);
    if (!entry) return false;
    uint32_t key[4];
    EntryKey(key, entry, group);
    bool saved = false;
    Held holder;
    if (Call<34, uint8_t>(mPersistService.mObject, key, kEntryRecordType, &holder.object)) {
        Held record;
        if (Call<22, uint8_t>(holder.object, key, &record.object, 2u)) {
            Held data;
            if (Call<0, uint8_t>(record.object, kEntryWriteIid, &data.object))
                saved = Call<10, uint8_t>(entry, data.object) != 0;
            Call<24>(holder.object, record.object);
            Call<10>(holder.object);
        }
    }
    return saved;
}

bool cTSSGSystem::Save(void *stream)
{
    for (uint32_t name : kEntryNames)
        Call<kSelfSaveEntry>(this, Call<kStreamGroup>(stream), GameAddress<const char>(name));
    Held state;
    Call<70>(mPersistService.mObject, kStateStreamType, &state.object);
    const bool written = Call<kSelfWriteState, uint8_t>(this, Call<3, void *>(state.object)) != 0;
    Call<71>(mPersistService.mObject, stream, state.object, kStateIid, 0u, 0u);
    return written;
}

bool cTSSGSystem::Load(void *stream)
{
    for (uint32_t name : kEntryNames)
        Call<kSelfLoadEntry>(this, Call<kStreamGroup>(stream), GameAddress<const char>(name));
    Held state;
    if (Call<69, uint8_t>(mPersistService.mObject, stream, kStateIid, 0u, kStateStreamType, &state.object, 0u)) {
        Call<kSelfReadState>(this, Call<4, void *>(state.object));
    } else {
        void *household = OriginalFunction<void *(__cdecl *)(), 0x20df9a>()();
        const int32_t counter = Call<18, int32_t>(household) * 50;
        mCounter194 = counter;
        Call<2>(ClockTarget(this), counter);
        mCounter198 = 0;
    }
    return true;
}

bool cTSSGSystem::ReadState(void *stream)
{
    uint32_t iid, version;
    Call<kStreamDword>(stream, &iid);
    Call<kStreamDword>(stream, &version);
    LocalString name;
    Call<kStreamString>(stream, &name.value);
    int32_t counter;
    Call<kStreamDword8>(stream, &counter);
    mCounter194 = counter;
    Call<2>(ClockTarget(this), counter);
    mCounter198 = 0;
    if (version < 3) return true;

    int32_t offset;
    Call<kStreamDword>(stream, &offset);
    const int32_t clock = Call<33, int32_t>(mAnimSystem.mObject);
    Call<20>(mAnimSystem.mObject, offset + clock);
    void *resource = nullptr;
    uint32_t key[2] = {0, 0};
    uint32_t count;
    Call<kStreamDword>(stream, &count);
    if (version < 4) {
        for (uint32_t i = 0; i < count; ++i) Call<kStreamDword>(stream, &key[0]);
    } else {
        for (uint32_t i = 0; i < count; ++i) {
            Call<kStreamDword>(stream, &key[0]);
            Call<kStreamDword>(stream, &key[1]);
            if (void *old = resource) {
                resource = nullptr;
                ReleaseResource(old);
            }
            Call<kSelfFindAnim>(this, key, static_cast<const char *>(nullptr), 0u, &resource, 0u);
        }
    }
    Call<20>(mAnimSystem.mObject, clock);
    if (resource) ReleaseResource(resource);
    return true;
}

bool cTSSGSystem::WriteState(void *stream)
{
    Call<kStreamDword>(stream, kStateIid);
    Call<kStreamDword>(stream, kStateVersion);
    LocalString name(GameAddress<const char>(kClassName), kClassNameLength);
    Call<kStreamString>(stream, &name.value);
    Call<kStreamDword8>(stream, Call<11>(ClockTarget(this)));
    Call<kStreamDword>(stream, Call<34>(mAnimSystem.mObject));
    Call<kStreamDword>(stream, Call<36>(mAnimSystem.mObject));
    for (void *item = Call<19, void *>(mAnimSystem.mObject); item; item = Call<61, void *>(item)) {
        uint32_t key[2];
        Call<32>(item, key);
        Call<kStreamDword>(stream, key[0]);
        Call<kStreamDword>(stream, key[1]);
    }
    return true;
}

void cTSSGSystem::LoadNeighborhoodAnimation()
{
    uint32_t group;
    {
        LocalString name(GameAddress<const char>(kNeighborhoodName), kNeighborhoodNameLength);
        group = Call<11>(mPersistService.mObject, &name.value);
    }
    uint32_t key[4] = {kNeighborhoodKeyType, group, kNeighborhoodKeyType, 0};
    Held holder;
    if (Call<34, uint8_t>(mPersistService.mObject, key, kEntryRecordType, &holder.object)) {
        Held record;
        void *persist = Call<3, void *>(mPersistService.mObject);
        if (Call<39, uint8_t>(persist, key, &record.object, 0u)) {
            Held data;
            if (Call<0, uint8_t>(record.object, kEntryReadIid, &data.object)) Call<27>(mAnimSystem.mObject, data.object);
            Call<6>(record.object);
        }
    }
    uint8_t scratch[32];
    Call<40>(mAnimSystem.mObject, scratch);
}

void cTSSGSystem::SaveNeighborhoodAnimation()
{
    uint32_t group;
    {
        LocalString name(GameAddress<const char>(kNeighborhoodName), kNeighborhoodNameLength);
        group = Call<11>(mPersistService.mObject, &name.value);
    }
    uint32_t key[4] = {kNeighborhoodKeyType, group, kNeighborhoodKeyType, 0};
    Held holder;
    if (!Call<34, uint8_t>(mPersistService.mObject, key, kEntryRecordType, &holder.object)) return;
    Held record;
    if (!Call<22, uint8_t>(holder.object, key, &record.object, 2u)) {
        if (void *old = record.object) {
            record.object = nullptr;
            Call<2>(old);
        }
        Call<23>(holder.object, key, &record.object);
    }
    if (record.object) {
        Held data;
        if (Call<0, uint8_t>(record.object, kEntryWriteIid, &data.object)) {
            Call<28>(mAnimSystem.mObject, data.object);
            uint8_t scratch[32];
            Call<40>(mAnimSystem.mObject, scratch);
        }
        Call<24>(holder.object, record.object);
    }
    Call<10>(holder.object);
}

} // namespace nTSSG
