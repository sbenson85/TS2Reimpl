#include "ts/cTSPersistResKeyList.h"

#include "game/game.h"
#include "game/vtable.h"
#include "rz/RZAutoRef.h"
#include "rz/Singleton_20ddfb.h"

#include <new>

namespace {

// Virtual functions declared with their real narrow (bool) result: MSVC then loads the slot into a register for the call, as
// the original does.
template <int N, class R, class... A> struct NarrowSlot : vtable_detail::Pad<N> {
    virtual R __thiscall call(A...) = 0;
};

template <class R, class... A> R Call(void *object, unsigned slot, A... args)
{
    return VirtualSlot<R(__thiscall *)(void *, A...)>(object, slot)(object, args...);
}

// The stream's record and that record's key mapper, both counted references released when the call ends (record first).
// A stream that is a persist record answers kRecordId with its cIGZPersistDBRecord face, whose slot 15 forwards a query to the
// segment-side object (the key mapper): its slot 10 maps a key for writing, slot 11 for reading.
struct StreamKeyMapper {
    void *mapper = nullptr;
    void *record = nullptr;

    explicit StreamKeyMapper(void *stream)
    {
        if (static_cast<NarrowSlot<0, bool, uint32_t, void **> *>(stream)->call(cTSPersistResKeyList::kRecordId, &record)) {
            void *face = record;
            if (mapper) {
                void *old = mapper;
                mapper = nullptr;
                Call<uint32_t>(old, 2);
            }
            Call<uint32_t>(face, 15, cTSPersistResKeyList::kKeyMapperId, &mapper);
        }
    }
    ~StreamKeyMapper()
    {
        if (record) Call<uint32_t>(record, 2);
        if (mapper) Call<uint32_t>(mapper, 2);
    }
};

// A reference held in a local: released through slot 2 when it goes out of scope.
struct HeldSegment {
    void *object = nullptr;
    ~HeldSegment()
    {
        if (object) Call<uint32_t>(object, 2);
    }
};

// The persist system, held as the original's RZAutoRef local: released with RZAutoRef::ReleaseObject (0x1466e0).
struct HeldPersistSystem {
    RZAutoRef ref{nullptr};
    ~HeldPersistSystem() { ref.ReleaseObject(); }
};

} // namespace

// ---- construction and destruction ----

// Inline in the factory 0x1f4dc0: the compiler installs our three vtables (the +4 / +8 interfaces' own first, as the original's
// dead stores).
cTSPersistResKeyList::cTSPersistResKeyList() : mRefCount(0), mKeys(), mKey{} {}

// The scalar deleting destructor (slot 8, 0x1f4d20) is the compiler's: this destructor (the class's vtables stored back, the
// key vector freed through the allocator service, its three pointers left zero), then operator delete(this, 0x2c) when bit 0 of
// the flags is set; returns this.
cTSPersistResKeyList::~cTSPersistResKeyList() = default;

// 0x1f4dc0
cTSPersistResKeyList *cTSPersistResKeyList::Create()
{
    return new cTSPersistResKeyList;
}

// ---- cIGZUnknown ----

// 0x1f4e60: this for the list's own interface (no null mask), the +8 face for id 1 and the serializable id, the +4 face for the
// persist resource id (each with the null test of a base-class conversion); a reference is added through slot 1 on success.
bool cTSPersistResKeyList::QueryInterface(uint32_t riid, void **out)
{
    switch (riid) {
    case kInterfaceId:
        *out = static_cast<cITSPersistResKeyList *>(this);
        AddRef();
        return true;
    case 1:
    case kSerializableId:
        *out = static_cast<cIGZSerializable *>(this);
        break;
    case kPersistResourceId:
        *out = static_cast<cIGZPersistResource *>(this);
        break;
    default:
        return false;
    }
    AddRef();
    return true;
}

// slot 1 (folded 0x15e520)
uint32_t cTSPersistResKeyList::AddRef()
{
    return ++mRefCount;
}

// slot 2 (folded 0x1f5100)
uint32_t cTSPersistResKeyList::Release()
{
    if (mRefCount <= 1) {
        delete this; // slot 8 with 1
        return 0;
    }
    return --mRefCount;
}

// ---- cITSPersistResKeyList ----

// slot 3 (folded 0x1f5280): the keys are dropped (the capacity stays).
void cTSPersistResKeyList::Clear()
{
    mKeys.clear();
}

// 0x1f5130: saves the list as a persist resource (persist system slot 16, with the segment as given), then flushes the
// segment (its slot 10): the given one, or else the one the persist system finds for the list's key.
bool cTSPersistResKeyList::Commit(void *segment)
{
    HeldPersistSystem persist;
    if (void *service = GetServiceObject())
        Call<uint8_t>(service, 5, kPersistServiceId, kPersistServiceInterfaceId, &persist.ref.mObject);
    void *const system = persist.ref.mObject;
    cIGZPersistResource *const face = this; // the null test of a base-class conversion (neg/sbb/and)
    if (!static_cast<NarrowSlot<16, bool, void *, void *> *>(system)->call(face, segment)) return false;
    if (segment) return static_cast<NarrowSlot<10, bool> *>(segment)->call();
    HeldSegment found;
    // GetKey through the +4 face's vtable (the unmasked this + 4), into a local.
    const cGZPersistResourceKey key = GetKey();
    if (!static_cast<NarrowSlot<34, bool, const void *, uint32_t, void **> *>(system)->call(&key, kSegmentInterfaceId, &found.object))
        return false;
    return static_cast<NarrowSlot<10, bool> *>(found.object)->call();
}

// slot 5 (folded 0x1c04e0): `this ? this + 4 : 0`
cIGZPersistResource *cTSPersistResKeyList::GetPersistResource()
{
    return static_cast<cIGZPersistResource *>(this);
}

// slots 6 and 7 (both folded 0x147780)
PersistResKeyVector *cTSPersistResKeyList::GetKeys()
{
    return &mKeys;
}

PersistResKeyVector *cTSPersistResKeyList::GetKeys2()
{
    return &mKeys;
}

// ---- cIGZPersistResource face (the key) ----

// slot 3 (folded 0x1f4e50)
cGZPersistResourceKey cTSPersistResKeyList::GetKey()
{
    return mKey;
}

// slot 4 (folded 0x1f5260)
bool cTSPersistResKeyList::SetKey(const cGZPersistResourceKey &key)
{
    mKey = key;
    return true;
}

// slots 5-7 (folded 0x148ba0, 0x147d00, 0x15e550)
uint32_t cTSPersistResKeyList::GetType()
{
    return mKey.type;
}

uint32_t cTSPersistResKeyList::GetGroup()
{
    return mKey.group;
}

uint32_t cTSPersistResKeyList::GetInstance()
{
    return mKey.instance;
}

// slots 8-10 (folded 0x1f5270, 0x1f5240, 0x1f5250)
bool cTSPersistResKeyList::SetType(uint32_t type)
{
    mKey.type = type;
    return true;
}

bool cTSPersistResKeyList::SetGroup(uint32_t group)
{
    mKey.group = group;
    return true;
}

bool cTSPersistResKeyList::SetInstance(uint32_t instance)
{
    mKey.instance = instance;
    return true;
}

// slot 11 (folded 0x46390)
uint32_t cTSPersistResKeyList::GetRefCount()
{
    return mRefCount;
}

// ---- cIGZSerializable face ----

// 0x1f5290: marker, version 2, count, then the four words of each key (mapped for writing when the stream is a persist record).
// True when the stream reports no error.
bool cTSPersistResKeyList::Write(ifaces::cIGZOStream *out)
{
    void *const stream = out;
    const StreamKeyMapper held(stream);
    Call<uint8_t>(stream, 9, kMarker);
    Call<uint8_t>(stream, 9, kWriteVersion);
    Call<uint8_t>(stream, 9, uint32_t(mKeys.size()));
    for (uint32_t i = 0; i < mKeys.size(); ++i) {
        if (held.mapper) {
            PersistResKey mapped = {0, 0, 0, 0};
            Call<uint8_t>(held.mapper, 10, &mKeys.data()[i], &mapped);
            Call<uint8_t>(stream, 9, mapped.type);
            Call<uint8_t>(stream, 9, mapped.group);
            Call<uint8_t>(stream, 9, mapped.instance);
            Call<uint8_t>(stream, 9, mapped.instanceHigh);
        } else {
            Call<uint8_t>(stream, 9, mKeys.data()[i].type);
            Call<uint8_t>(stream, 9, mKeys.data()[i].group);
            Call<uint8_t>(stream, 9, mKeys.data()[i].instance);
            Call<uint8_t>(stream, 9, mKeys.data()[i].instanceHigh);
        }
    }
    return static_cast<NarrowSlot<18, uint8_t> *>(stream)->call() == 0;
}

// 0x1f4ed0: the list is resized to the stored count (new keys zero, extra ones dropped) and the keys are read over it: three
// words each, a fourth from version 2 on (older streams leave the high word as it was); each key is then mapped for reading when
// the stream is a persist record. A stream without the marker starts with the count itself (version 0). True when the stream
// reports no error.
bool cTSPersistResKeyList::Read(ifaces::cIGZIStream *in)
{
    void *const stream = in;
    const StreamKeyMapper held(stream);
    // Failed reads leave these as they were (uninitialised stack words in the original): they start at zero.
    uint32_t first = 0;
    uint32_t count = 0;
    int32_t version = 0;
    Call<uint8_t>(stream, 9, &first);
    if (first == kMarker) {
        Call<uint8_t>(stream, 8, &version);
        Call<uint8_t>(stream, 9, &count);
    } else {
        count = first;
    }
    mKeys.resize(count);
    uint32_t word = 0; // one local reused for every word read, as in the original
    for (uint32_t i = 0; i < count; ++i) {
        Call<uint8_t>(stream, 9, &word);
        mKeys.data()[i].type = word;
        Call<uint8_t>(stream, 9, &word);
        mKeys.data()[i].group = word;
        Call<uint8_t>(stream, 9, &word);
        mKeys.data()[i].instance = word;
        if (version >= 2) {
            Call<uint8_t>(stream, 9, &word);
            mKeys.data()[i].instanceHigh = word;
        }
        if (held.mapper) {
            PersistResKey mapped = {0, 0, 0, 0};
            if (static_cast<NarrowSlot<11, bool, void *, void *> *>(held.mapper)->call(&mKeys.data()[i], &mapped))
                mKeys.data()[i] = mapped;
        }
    }
    return static_cast<NarrowSlot<18, uint8_t> *>(stream)->call() == 0;
}

// 0x1f4e40: the record class id of the list.
uint32_t cTSPersistResKeyList::GetGZCLSID()
{
    return kClassId;
}

// ---- the RVA hooks' adapters ----
// Qualified calls (no virtual dispatch: a list built with the game's vtables would reach these very hooks again), bodies forced
// inline so each adapter is the function itself (matchcheck compares the adapter). A base-to-derived static_cast finds the
// object from a face.

using Adapters = cTSPersistResKeyListAdapters;

bool __fastcall Adapters::QueryInterface(Self *self, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return self->Self::QueryInterface(riid, out);
}

uint32_t __fastcall Adapters::GetGZCLSID(cIGZSerializable *face, void *)
{
    [[msvc::forceinline_calls]] return static_cast<Self *>(face)->Self::GetGZCLSID();
}
