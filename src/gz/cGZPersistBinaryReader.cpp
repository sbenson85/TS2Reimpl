#include "gz/cGZPersistBinaryReader.h"

#include "game/game.h"
#include "game/vtable.h"
#include "rz/RZAutoRef.h"
#include "rz/RZFrameworkHelpers.h"
#include "rz/StaticGetters.h"
#include "rz/cRZString.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace {

// Reads one value of a stream slot that takes a destination pointer (__thiscall, one stack argument).
// The result is ignored by every caller here, as in the original.
template <typename T> void StreamRead(void *stream, int slot, T *dst)
{
    VirtualSlot<bool(__thiscall *)(void *, void *)>(stream, slot)(stream, dst);
}

// The scalar getters read into the stack slot holding their pushed `this`, so when the stream read
// fails (and writes nothing) the result is bytes of the this pointer: the high byte for 8-bit
// values (read at [esp+3]), the low bytes for wider ones.
template <typename T> T ReadIntoThisSlot(const void *self, void *stream, int slot)
{
    uint32_t storage = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self));
    auto *at = reinterpret_cast<uint8_t *>(&storage) + (sizeof(T) == 1 ? 3 : 0);
    StreamRead(stream, slot, at);
    T v;
    std::memcpy(&v, at, sizeof(T));
    return v;
}

// Bool-valued stream read with the result normalised, like the original's setne. `seed` is what the
// original's result slot held before the read (see ReadIntoThisSlot).
bool StreamReadBool(void *stream, uint8_t &slotByte)
{
    StreamRead(stream, cGZPersistBinaryReader::kStreamUint8, &slotByte);
    return slotByte != 0;
}

// Calls the delegate's slot with (dst, count): the original's tail jump, so the delegate pops the args.
template <typename T> void Delegate(void *delegate, int slot, T *dst, uint32_t count)
{
    VirtualSlot<void(__thiscall *)(void *, void *, uint32_t)>(delegate, slot)(delegate, dst, count);
}

} // namespace

bool cGZPersistBinaryReader::QueryInterface(uint32_t id, void **out)
{
    if (out == nullptr) return false;
    if (id != kIdFirst && id != kIdSecond) return false;
    *out = this;
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(this); // AddRef
    return true;
}

bool cGZPersistBinaryReader::GetBool()
{
    return ReadIntoThisSlot<uint8_t>(this, mStream, kStreamUint8) != 0;
}

int8_t cGZPersistBinaryReader::GetSint8()
{
    return ReadIntoThisSlot<int8_t>(this, mStream, kStreamSint8);
}

uint8_t cGZPersistBinaryReader::GetUint8()
{
    return ReadIntoThisSlot<uint8_t>(this, mStream, kStreamUint8);
}

int16_t cGZPersistBinaryReader::GetSint16()
{
    return ReadIntoThisSlot<int16_t>(this, mStream, kStreamSint16);
}

int32_t cGZPersistBinaryReader::GetSint32()
{
    return ReadIntoThisSlot<int32_t>(this, mStream, kStreamSint32);
}

uint32_t cGZPersistBinaryReader::GetUint32()
{
    return ReadIntoThisSlot<uint32_t>(this, mStream, kStreamUint32);
}

float cGZPersistBinaryReader::GetFloat32()
{
    return ReadIntoThisSlot<float>(this, mStream, kStreamFloat32);
}

double cGZPersistBinaryReader::GetFloat64()
{
    double v;
    StreamRead(mStream, kStreamFloat64, &v);
    return v;
}

void cGZPersistBinaryReader::GetBoolArray(bool *dst, uint32_t count)
{
    // The original reads into its own count argument slot: a failed read repeats the previous byte,
    // starting with the low byte of count.
    for (uint32_t remaining = count; remaining != 0; --remaining) {
        StreamRead(mStream, kStreamUint8, reinterpret_cast<uint8_t *>(&count));
        *dst++ = *reinterpret_cast<uint8_t *>(&count) != 0;
    }
}

bool cGZPersistBinaryReader::GetVoid(void *dst, uint32_t n)
{
    return VirtualCall<bool(__thiscall *)(void *, void *, uint32_t), kStreamGetVoid>(mStream, dst, n);
}

void cGZPersistBinaryReader::GetSint16Array(int16_t *dst, uint32_t count)
{
    if (mDelegate != nullptr) return Delegate(mDelegate, kDelegateSint16Array, dst, count);
    for (uint32_t i = 0; i < count; ++i) StreamRead(mStream, kStreamSint16, dst + i);
}

void cGZPersistBinaryReader::GetSint32Array(int32_t *dst, uint32_t count)
{
    if (mDelegate != nullptr) return Delegate(mDelegate, kDelegateSint32Array, dst, count);
    for (uint32_t i = 0; i < count; ++i) StreamRead(mStream, kStreamSint32, dst + i);
}

void cGZPersistBinaryReader::GetFloat32Array(float *dst, uint32_t count)
{
    if (mDelegate != nullptr) return Delegate(mDelegate, kDelegateFloat32Array, dst, count);
    for (uint32_t i = 0; i < count; ++i) StreamRead(mStream, kStreamFloat32, dst + i);
}

bool cGZPersistBinaryReader::GetObjectRef(uint32_t id, void **out)
{
    // The flag is read first, as in the original. Without it the result is null and the return value is
    // whatever the original leaves in al: the low byte of out. Callers test al, so this mirrors that.
    // These flags (and the index below) are read into uninitialised stack in the original: on a
    // failed read their values are undefined there, so any value is as faithful as another.
    uint8_t presentByte = 0, secondByte = 0;
    const bool present = StreamReadBool(mStream, presentByte);

    // The entry at the back of the ring: index (count + head - 1) mod capacity.
    const uint32_t index = (mCount + (mHead - 1)) & (mCapacity - 1);
    RingEntry *entry = mBuffer[index];

    if (!present) {
        *out = nullptr;
        return (reinterpret_cast<uintptr_t>(out) & 0xff) != 0;
    }

    const bool second = StreamReadBool(mStream, secondByte);
    uint32_t element_index;
    StreamRead(mStream, kStreamUint32, &element_index);

    *out = nullptr;
    void **table = second ? entry->mFirst : entry->mSecond;
    void *element = table[element_index];
    if (element == nullptr) return (element_index & 0xff) != 0; // al as left by the original, see above

    return VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(element, id, out);
}

// ---- the counted reference -----------------------------------------------------------------------------

void cGZPersistBinaryReader::Ref::Hold(void *object)
{
    if (object != nullptr) VirtualCall<uint32_t(__thiscall *)(void *), 1>(object);
}

void cGZPersistBinaryReader::Ref::Drop(void *object)
{
    if (object != nullptr) VirtualCall<uint32_t(__thiscall *)(void *), 2>(object);
}

// ---- small slots -----------------------------------------------------------------------------------------

uint16_t cGZPersistBinaryReader::GetUint16()
{
    return ReadIntoThisSlot<uint16_t>(this, mStream, 7);
}

void cGZPersistBinaryReader::GetUint16Array(uint16_t *dst, uint32_t count)
{
    if (mDelegate != nullptr) return Delegate(mDelegate, 8, dst, count);
    for (uint32_t i = 0; i < count; ++i) StreamRead(mStream, 7, dst + i);
}

void cGZPersistBinaryReader::GetUint32Array(uint32_t *dst, uint32_t count)
{
    if (mDelegate != nullptr) return Delegate(mDelegate, 12, dst, count);
    for (uint32_t i = 0; i < count; ++i) StreamRead(mStream, kStreamUint32, dst + i);
}

void cGZPersistBinaryReader::GetFloat64Array(double *dst, uint32_t count)
{
    if (mDelegate != nullptr) return Delegate(mDelegate, 22, dst, count);
    for (uint32_t i = 0; i < count; ++i) StreamRead(mStream, kStreamFloat64, dst + i);
}

bool cGZPersistBinaryReader::GetGZStr(void *str)
{
    return VirtualCall<bool(__thiscall *)(void *, void *), 15>(mStream, str);
}

bool cGZPersistBinaryReader::Skip(uint32_t n)
{
    return VirtualCall<bool(__thiscall *)(void *, uint32_t), 3>(mStream, n);
}

uint32_t cGZPersistBinaryReader::ReadObject(uint32_t iid, void **out)
{
    uint32_t classId = 0; // uninitialised stack in the original; a failed read leaves it as it was
    StreamRead(mStream, kStreamUint32, &classId);
    if (classId == 0) {
        *out = nullptr;
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(out));
    }
    void *object = nullptr;
    void *framework = GetDirectorFrameworkObject();
    VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void **), 3>(framework, classId, kReadObjectIid, &object);
    // The created object is used without a null test, as in the original.
    VirtualCall<bool(__thiscall *)(void *, void *), 3>(object, this);
    VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(object, iid, out);
    return VirtualCall<uint32_t(__thiscall *)(void *), 2>(object);
}

void *cGZPersistBinaryReader::ReadStringRecord(void *record)
{
    auto *holder = static_cast<RZUnknownSlot *>(record);
    auto *words = static_cast<uint32_t *>(record);
    holder->mObject = nullptr;
    void *memory = ::operator new(sizeof(cRZString));
    cRZString *text = memory != nullptr ? static_cast<cRZString *>(memory)->Construct() : nullptr;
    holder->Assign(text);
    VirtualCall<bool(__thiscall *)(void *, void *), 15>(mStream, holder->mObject);
    StreamRead(mStream, kStreamUint32, words + 1);
    StreamRead(mStream, kStreamUint32, words + 2);
    return record;
}

cGZPersistBinaryReader *cGZPersistBinaryReader::Construct()
{
    mVTable = VTable();
    mRefCount = 0;
    new (&Ring()) EntryRing(); // the proxy node; the other ring fields start empty
    mStream = nullptr;
    mDelegate = nullptr;
    mGroup = 0;
    return this;
}

uint32_t cGZPersistBinaryReader::Release()
{
    if (--mRefCount != 0) return mRefCount;
    VirtualCall<void *(__thiscall *)(void *, uint32_t), 29>(this, 1); // the deleting destructor
    return 0;
}

cGZPersistBinaryReader *cGZPersistBinaryReader::DeletingDestructor(uint32_t flags)
{
    mVTable = VTable();
    Ring().~EntryRing();
    if (flags & 1) ::operator delete(this, sizeof(*this));
    return this;
}

// ---- ReadRoot ----------------------------------------------------------------------------------------------

namespace {

using Key = cGZPersistBinaryReader::Key;

uint32_t ReadWord(void *stream, uint32_t *dst)
{
    return VirtualCall<bool(__thiscall *)(void *, uint32_t *), cGZPersistBinaryReader::kStreamUint32>(stream, dst);
}

// The resource manager's slot 3: the resource under `key` asked for interface `iid` into *out (null when absent).
void FindResource(void *manager, Key *key, void **out)
{
    VirtualCall<bool(__thiscall *)(void *, Key *, uint32_t, void **, uint32_t, uint32_t), 3>(
        manager, key, cGZPersistBinaryReader::kResourceIid, out, 0u, 0u);
}

} // namespace

// The stream holds: [0xffff0000 | version] (optional), the dependent resource count, per resource its group,
// instance, (instance high word from version 1 on) and type; then the object count, and a class id per object (the
// first is the root's and is not used). Each dependent resource is looked up through the resource manager in the
// reader's group (when one is set, differs, and there is a context), then its own group (unless it is the fallback
// group), then the fallback group; a type of 0 or a resource found nowhere gives a null entry. The original also
// formats "Null dependent resource ..." messages into a local cRZString on those paths and destroys it unused (a
// stripped log): no effect outside its own stack and heap block, left out here.
void cGZPersistBinaryReader::ReadRoot(void *root, void *stream, void *context)
{
    void *resource = VirtualCall<void *(__thiscall *)(void *), 8>(root);
    if (resource == nullptr) return;
    Key key; // filled by the resource (all 16 bytes)
    VirtualCall<void(__thiscall *)(void *, Key *), 3>(resource, &key);

    mGroup = 0;
    void *groupFace = nullptr;
    bool groupSetHere = false;
    if (context != nullptr &&
        VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(context, kGroupContextIid, &groupFace)) {
        mGroup = VirtualCall<uint32_t(__thiscall *)(void *), 5>(groupFace);
        if (mGroup == 0xffffffffu) {
            mGroup = key.mGroup;
            VirtualCall<void(__thiscall *)(void *, uint32_t), 6>(groupFace, key.mGroup);
            groupSetHere = true;
        }
    }

    EntryRing &ring = Ring();
    {
        const Ref held(stream);
        ring.emplace_back(held);
    }
    Entry &entry = ring.back();
    entry.mRecord.Reset();
    VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(stream, kSerialRecordIid, entry.mRecord.Out());
    mStream = stream;
    mDelegate = entry.mRecord.Get();

    RZAutoRef manager{nullptr};
    void *framework = reinterpret_cast<void *>(static_cast<uintptr_t>(RZGetGlobal10C9378()));
    if (framework != nullptr)
        VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void **), 5>(framework, 0x56b906eu, 0x656b8efcu, &manager.mObject);
    // The manager is used without a null test, as in the original.
    const bool known = VirtualCall<bool(__thiscall *)(void *, Key *), 11>(manager.mObject, &key);
    if (!known) {
        void *face = VirtualCall<void *(__thiscall *)(void *), 8>(root);
        VirtualCall<bool(__thiscall *)(void *, Key *, void *), 9>(manager.mObject, &key, face);
    }

    uint32_t first = 0;
    ReadWord(stream, &first);
    uint32_t version = 0;
    uint32_t resourceCount = first;
    if ((first & 0xffff0000u) == 0xffff0000u) {
        version = first & 0xffffu;
        ReadWord(stream, &resourceCount);
    }

    uint32_t word = 0; // one stack word for every field, as in the original
    for (uint32_t i = 0; i < resourceCount; ++i) {
        Key listed{0, 0, 0, 0};
        ReadWord(stream, &word);
        listed.mGroup = word;
        ReadWord(stream, &word);
        listed.mInstance = word;
        if (version >= 1) {
            ReadWord(stream, &word);
            listed.mInstanceHi = word;
        }
        ReadWord(stream, &word);
        listed.mType = word;

        Key wanted = listed;
        if (groupFace != nullptr) VirtualCall<void(__thiscall *)(void *, Key *, Key *), 4>(groupFace, &listed, &wanted);

        Ref found;
        if (wanted.mType != 0) {
            const uint32_t ownGroup = wanted.mGroup;
            // (mGroup is only non-zero when a context gave it, so no separate context test is needed)
            if (mGroup != ownGroup && mGroup != 0) {
                wanted.mGroup = mGroup;
                FindResource(manager.mObject, &wanted, found.Out());
            }
            if (found.Get() == nullptr && ownGroup != kFallbackGroup) {
                wanted.mGroup = ownGroup;
                FindResource(manager.mObject, &wanted, found.Out());
            }
            if (found.Get() == nullptr) {
                wanted.mGroup = kFallbackGroup;
                FindResource(manager.mObject, &wanted, found.Out());
            }
        }
        entry.mResources.push_back(found);
    }

    uint32_t objectCount = 0;
    ReadWord(stream, &objectCount);
    {
        const Ref held(root);
        entry.mObjects.push_back(held);
    }
    uint32_t classId = 0;
    ReadWord(stream, &classId); // the root's own class id: read past
    for (uint32_t i = 1; i < objectCount; ++i) {
        ReadWord(stream, &classId);
        if (classId == 0) {
            entry.mObjects.push_back(Ref());
            continue;
        }
        Ref created;
        void *objectFramework = GetDirectorFrameworkObject();
        VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void **), 3>(objectFramework, classId, kSerializableIid, created.Out());
        entry.mObjects.push_back(created);
    }
    for (uint32_t i = 0; i < objectCount; ++i) {
        void *object = entry.mObjects.data()[i].Get();
        if (object != nullptr) VirtualCall<bool(__thiscall *)(void *, void *), 3>(object, this);
    }

    if (!known) VirtualCall<bool(__thiscall *)(void *, Key *), 10>(manager.mObject, &key);

    ring.pop_back();
    if (ring.empty()) {
        mStream = nullptr;
        mDelegate = nullptr;
    } else {
        mStream = ring.back().mStream.Get();
        mDelegate = ring.back().mRecord.Get();
    }

    if (groupSetHere) VirtualCall<void(__thiscall *)(void *, uint32_t), 6>(groupFace, 0xffffffffu);
    manager.ReleaseObject();
    if (groupFace != nullptr) VirtualCall<uint32_t(__thiscall *)(void *), 2>(groupFace);
}
