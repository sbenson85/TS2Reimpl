#include "gz/cGZPersistBinaryWriter.h"
#include "game/game.h"
#include "game/vtable.h"
#include "rz/StaticGetters.h"

#include <new>

namespace {

using CurrentFn = void *(__thiscall *)(void *);
using NoArgFn = uint32_t(__thiscall *)(void *);
using OutObjFn = void *(__thiscall *)(void *, void **);
using U32Fn = uint32_t(__thiscall *)(void *, uint32_t);
using U32PtrFn = uint32_t(__thiscall *)(void *, void *);
using F32Fn = uint32_t(__thiscall *)(void *, float);
using F64Fn = uint32_t(__thiscall *)(void *, double);
using RawFn = uint32_t(__thiscall *)(void *, const void *, uint32_t);

// cIGZOStream slots the writer forwards to (vtable offset / 4).
constexpr int kStreamSint8 = 4;
constexpr int kStreamUint8 = 5;
constexpr int kStreamSint16 = 6;
constexpr int kStreamUint16 = 7;
constexpr int kStreamSint32 = 8;
constexpr int kStreamUint32 = 9;
constexpr int kStreamFloat32 = 12;
constexpr int kStreamFloat64 = 13;
constexpr int kStreamRZCharStr = 14;
constexpr int kStreamGZStr = 15;
constexpr int kStreamRaw = 17;

// Free helpers for the array workers (they run outside member calls, see the naked stubs below).
void *CurrentOf(void *writer)
{
    return VirtualCall<CurrentFn, cGZPersistBinaryWriter::kCurrentSlot>(writer);
}

// Writes count elements of Elem, each converted to Arg. Count 0 returns entryEax, the value the caller
// left in eax (the original's loop never touches eax on that path).
template <typename Arg, typename Elem>
uint32_t ArrayWrite(void *writer, uint32_t entryEax, const Elem *buf, uint32_t count, int slot)
{
    if (count == 0) return entryEax;
    uint32_t ret = 0;
    for (uint32_t i = 0; i < count; ++i) {
        void *stream = CurrentOf(writer);
        ret = VirtualSlot<uint32_t(__thiscall *)(void *, Arg)>(stream, slot)(stream, static_cast<Arg>(buf[i]));
    }
    return ret;
}

// Workers called by the naked array stubs: cdecl, (this, entry eax, buffer, count).
uint32_t ArrayUint8(void *w, uint32_t eax, const uint8_t *buf, uint32_t n)
{
    return ArrayWrite<uint32_t>(w, eax, buf, n, kStreamUint8);
}
uint32_t ArraySint16(void *w, uint32_t eax, const uint16_t *buf, uint32_t n)
{
    return ArrayWrite<uint32_t>(w, eax, buf, n, kStreamSint16);
}
uint32_t ArrayUint16(void *w, uint32_t eax, const uint16_t *buf, uint32_t n)
{
    return ArrayWrite<uint32_t>(w, eax, buf, n, kStreamUint16);
}
uint32_t ArraySint32(void *w, uint32_t eax, const uint32_t *buf, uint32_t n)
{
    return ArrayWrite<uint32_t>(w, eax, buf, n, kStreamSint32);
}
uint32_t ArrayUint32(void *w, uint32_t eax, const uint32_t *buf, uint32_t n)
{
    return ArrayWrite<uint32_t>(w, eax, buf, n, kStreamUint32);
}
uint32_t ArrayFloat32(void *w, uint32_t eax, const float *buf, uint32_t n)
{
    return ArrayWrite<float>(w, eax, buf, n, kStreamFloat32);
}
uint32_t ArrayFloat64(void *w, uint32_t eax, const double *buf, uint32_t n)
{
    return ArrayWrite<double>(w, eax, buf, n, kStreamFloat64);
}

} // namespace

// Interface ids are compared directly, as in cRZOStream.
bool cGZPersistBinaryWriter::QueryInterface(uint32_t riid, void **ppvObj)
{
    if (!ppvObj) return false;
    if (riid != kGZIID_cIGZUnknown && riid != kGZIID_Unidentified1 && riid != kGZIID_Unidentified2) return false;
    *ppvObj = this;
    VirtualCall<NoArgFn, kAddRefSlot>(this);
    return true;
}

void *cGZPersistBinaryWriter::Stream()
{
    return VirtualCall<CurrentFn, kCurrentSlot>(this);
}

void *cGZPersistBinaryWriter::BackElement()
{
    uint32_t index = mOffset + mSize - 1;
    return mBlocks[(index >> 2) & (mBlockCount - 1)][index & 3];
}

// Slot 29: the back element's own slot 29 answers, so nested writers delegate down the chain.
void *cGZPersistBinaryWriter::GetCurrent()
{
    void *back = BackElement();
    return VirtualCall<CurrentFn, kCurrentSlot>(back);
}

uint32_t cGZPersistBinaryWriter::SetUint8(uint32_t value)
{
    void *stream = Stream();
    return VirtualCall<U32Fn, kStreamUint8>(stream, value);
}

uint32_t cGZPersistBinaryWriter::SetSint8(uint32_t value)
{
    void *stream = Stream();
    return VirtualCall<U32Fn, kStreamSint8>(stream, value);
}

uint32_t cGZPersistBinaryWriter::SetSint16(uint32_t value)
{
    void *stream = Stream();
    return VirtualCall<U32Fn, kStreamSint16>(stream, value);
}

uint32_t cGZPersistBinaryWriter::SetUint16(uint32_t value)
{
    void *stream = Stream();
    return VirtualCall<U32Fn, kStreamUint16>(stream, value);
}

uint32_t cGZPersistBinaryWriter::SetSint32(uint32_t value)
{
    void *stream = Stream();
    return VirtualCall<U32Fn, kStreamSint32>(stream, value);
}

uint32_t cGZPersistBinaryWriter::SetUint32(uint32_t value)
{
    void *stream = Stream();
    return VirtualCall<U32Fn, kStreamUint32>(stream, value);
}

uint32_t cGZPersistBinaryWriter::SetFloat32(float value)
{
    void *stream = Stream();
    return VirtualCall<F32Fn, kStreamFloat32>(stream, value);
}

uint32_t cGZPersistBinaryWriter::SetFloat64(double value)
{
    void *stream = Stream();
    return VirtualCall<F64Fn, kStreamFloat64>(stream, value);
}

uint32_t cGZPersistBinaryWriter::SetVoid(const void *buf, uint32_t size)
{
    void *stream = Stream();
    return VirtualCall<RawFn, kStreamRaw>(stream, buf, size);
}

uint32_t cGZPersistBinaryWriter::WriteRZCharStr(const char *str)
{
    void *stream = Stream();
    return VirtualCall<U32Fn, kStreamRZCharStr>(stream, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(str)));
}

uint32_t cGZPersistBinaryWriter::WriteGZStr(const void *str)
{
    void *stream = Stream();
    return VirtualCall<U32Fn, kStreamGZStr>(stream, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(str)));
}

// Array stubs. The original reads its count and buffer from the stack and returns eax as it was on
// entry when count is 0, so each stub keeps eax, pushes (count, buffer, eax, this) and calls the
// worker cdecl; the worker's return value is the stub's. Stack on entry: [esp] ret, [esp+4] buf,
// [esp+8] count. Each push of [esp+8] copies the next argument from the caller's frame.
__declspec(naked) uint32_t cGZPersistBinaryWriter::SetUint8Array(const uint8_t *, uint32_t)
{
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push eax
        push ecx
        call ArrayUint8
        add esp, 16
        ret 8
    }
}

__declspec(naked) uint32_t cGZPersistBinaryWriter::SetSint16Array(const uint16_t *, uint32_t)
{
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push eax
        push ecx
        call ArraySint16
        add esp, 16
        ret 8
    }
}

__declspec(naked) uint32_t cGZPersistBinaryWriter::SetUint16Array(const uint16_t *, uint32_t)
{
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push eax
        push ecx
        call ArrayUint16
        add esp, 16
        ret 8
    }
}

__declspec(naked) uint32_t cGZPersistBinaryWriter::SetSint32Array(const uint32_t *, uint32_t)
{
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push eax
        push ecx
        call ArraySint32
        add esp, 16
        ret 8
    }
}

__declspec(naked) uint32_t cGZPersistBinaryWriter::SetUint32Array(const uint32_t *, uint32_t)
{
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push eax
        push ecx
        call ArrayUint32
        add esp, 16
        ret 8
    }
}

__declspec(naked) uint32_t cGZPersistBinaryWriter::SetFloat32Array(const float *, uint32_t)
{
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push eax
        push ecx
        call ArrayFloat32
        add esp, 16
        ret 8
    }
}

__declspec(naked) uint32_t cGZPersistBinaryWriter::SetFloat64Array(const double *, uint32_t)
{
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push eax
        push ecx
        call ArrayFloat64
        add esp, 16
        ret 8
    }
}

// Writes a serializable object: obj->slot 4 returns a record whose +4 word is the size (after an
// out interface pointer, released below), the size goes to the stream as a uint32, and obj->slot 5 is
// then called with this writer. A null obj writes 0 instead.
uint32_t cGZPersistBinaryWriter::WriteSerialized(void *obj)
{
    if (!obj) {
        void *stream = Stream();
        return VirtualCall<U32Fn, kStreamUint32>(stream, 0);
    }
    void *out = nullptr;
    auto *record = static_cast<uint8_t *>(VirtualCall<OutObjFn, 4>(obj, &out));
    uint32_t size = *reinterpret_cast<const uint32_t *>(record + 4);
    void *stream = Stream();
    VirtualCall<U32Fn, kStreamUint32>(stream, size);
    if (out) VirtualCall<NoArgFn, 2>(out);
    return VirtualCall<U32PtrFn, 5>(obj, this);
}

// Writes a flag and the object's count: flag (obj != 0) as a byte, then if obj is non-null, the flag
// (count != back element's slot 32) as a byte, then either the back element's slot 35 with obj or its
// slot 38 with the count, written as a uint32. The second byte keeps the upper bits of obj, as the
// original's stack slot does (it stores only the low byte of its argument).
uint32_t cGZPersistBinaryWriter::WriteFlaggedObject(void *obj)
{
    void *stream = Stream();
    uint32_t ret = VirtualCall<U32Fn, kStreamUint8>(stream, obj != nullptr ? 1u : 0u);
    if (!obj) return ret;
    uint32_t count = VirtualCall<NoArgFn, 6>(obj);
    void *back = BackElement();
    uint32_t first = VirtualCall<NoArgFn, 32>(back);
    uint32_t flagWord = (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(obj)) & ~0xffu) | (first != count ? 1u : 0u);
    stream = Stream();
    VirtualCall<U32Fn, kStreamUint8>(stream, flagWord);
    back = BackElement();
    uint32_t second = VirtualCall<NoArgFn, 32>(back);
    uint32_t value = second == count ? VirtualCall<U32PtrFn, 35>(back, obj)
                                     : VirtualCall<U32Fn, 38>(back, count);
    stream = Stream();
    return VirtualCall<U32Fn, kStreamUint32>(stream, value);
}

// Writes a three-word record: rec[0] through the stream's SetGZStr, then rec[1] and rec[2] as uint32.
uint32_t cGZPersistBinaryWriter::WriteRecord(const uint32_t *rec)
{
    uint32_t first = rec[0];
    void *stream = Stream();
    VirtualCall<U32Fn, kStreamGZStr>(stream, first);
    uint32_t second = rec[1];
    stream = Stream();
    VirtualCall<U32Fn, kStreamUint32>(stream, second);
    uint32_t third = rec[2];
    stream = Stream();
    return VirtualCall<U32Fn, kStreamUint32>(stream, third);
}

// ---- WriteRoot and the object-level slots ----------------------------------------------------------------

namespace {

constexpr uint32_t kGroupContextIid = 0xad095a01;
constexpr uint32_t kWriteContextClass = 0xbb194c78;
constexpr uint32_t kWriteContextIid = 0x9b194c66;
constexpr uint32_t kVersionWord = 0xffff0001; // version 1: dependent keys carry the instance's high word

// The write context's slots (the deque's elements).
constexpr int kContextBegin = 3;          // (root, stream, context)
constexpr int kContextObjectCount = 33;
constexpr int kContextObject = 34;        // (index)
constexpr int kContextResourceCount = 36;
constexpr int kContextResource = 37;      // (index)

uint32_t CountOf(void *element, int slot)
{
    return VirtualSlot<uint32_t(__thiscall *)(void *)>(element, slot)(element);
}

void *ItemOf(void *element, int slot, uint32_t index)
{
    return VirtualSlot<void *(__thiscall *)(void *, uint32_t)>(element, slot)(element, index);
}

struct ResourceKey {
    uint32_t mType, mGroup, mInstance, mInstanceHi;
};

} // namespace

// The stream gets: the version word 0xffff0001, the dependent resource count, per resource its key (after the group
// face's slot 3 maps it) as group, instance, instance high word, type; the object count, a class id per object (the
// word at +4 of what the object's slot 4 returns); then each object writes itself (its slot 5 with this), the stream is
// flushed (slot 3), and every dependent resource gets its slot 10. Counts and items are asked of the context again on
// every step, as the original does.
void cGZPersistBinaryWriter::WriteRoot(void *root, void *stream, void *context)
{
    void *groupFace = nullptr;
    mGroup = 0;
    if (context != nullptr && VirtualCall<bool(__thiscall *)(void *, uint32_t, void **), 0>(context, kGroupContextIid, &groupFace))
        mGroup = VirtualCall<uint32_t(__thiscall *)(void *), 5>(groupFace);

    void *created = nullptr;
    void *framework = GetDirectorFrameworkObject();
    VirtualCall<bool(__thiscall *)(void *, uint32_t, uint32_t, void **), 3>(framework, kWriteContextClass, kWriteContextIid, &created);
    // The deque's element is a counted copy of `created` (stored, then AddRef when non-null); `created` itself is
    // released at the end.
    Ring().emplace_back(created);
    VirtualCall<void(__thiscall *)(void *, void *, void *, void *), kContextBegin>(BackElement(), root, stream, context);

    VirtualCall<U32Fn, kStreamUint32>(Stream(), kVersionWord);
    uint32_t count = CountOf(BackElement(), kContextResourceCount);
    VirtualCall<U32Fn, kStreamUint32>(Stream(), count);

    ResourceKey key;   // what the resource reports (uninitialised until the first resource)
    ResourceKey mapped; // its mapped copy; the type word is also the class-info slot below, as in the original
    for (uint32_t i = 0; i < CountOf(BackElement(), kContextResourceCount); ++i) {
        void *entry = ItemOf(BackElement(), kContextResource, i);
        void *resource = VirtualCall<void *(__thiscall *)(void *), 7>(entry);
        VirtualCall<void(__thiscall *)(void *, ResourceKey *), 3>(resource, &key);
        mapped = key;
        if (groupFace != nullptr) VirtualCall<void(__thiscall *)(void *, ResourceKey *, ResourceKey *), 3>(groupFace, &key, &mapped);
        void *out = Stream();
        VirtualCall<U32Fn, kStreamUint32>(out, mapped.mGroup);
        VirtualCall<U32Fn, kStreamUint32>(out, mapped.mInstance);
        VirtualCall<U32Fn, kStreamUint32>(out, mapped.mInstanceHi);
        VirtualCall<U32Fn, kStreamUint32>(out, mapped.mType);
    }

    count = CountOf(BackElement(), kContextObjectCount);
    VirtualCall<U32Fn, kStreamUint32>(Stream(), count);
    for (uint32_t i = 0; i < CountOf(BackElement(), kContextObjectCount); ++i) {
        void *object = ItemOf(BackElement(), kContextObject, i);
        // Slot 4 fills a counted out reference (the word that held the mapped type) and returns a block whose +4 is
        // the class id.
        void **info = reinterpret_cast<void **>(&mapped.mType);
        const auto *block = VirtualCall<const uint32_t *(__thiscall *)(void *, void **), 4>(object, info);
        VirtualCall<U32Fn, kStreamUint32>(Stream(), block[1]);
        if (*info != nullptr) VirtualCall<NoArgFn, 2>(*info);
    }
    for (uint32_t i = 0; i < CountOf(BackElement(), kContextObjectCount); ++i) {
        void *object = ItemOf(BackElement(), kContextObject, i);
        VirtualCall<bool(__thiscall *)(void *, void *), 5>(object, this);
    }
    VirtualCall<NoArgFn, 3>(Stream());
    for (uint32_t i = 0; i < CountOf(BackElement(), kContextResourceCount); ++i) {
        void *entry = ItemOf(BackElement(), kContextResource, i);
        VirtualCall<NoArgFn, 10>(entry);
    }

    Ring().pop_back();
    if (created != nullptr) VirtualCall<NoArgFn, 2>(created);
    if (groupFace != nullptr) VirtualCall<NoArgFn, 2>(groupFace);
}

cGZPersistBinaryWriter *cGZPersistBinaryWriter::DeletingDestructor(uint32_t flags)
{
    mVTable = GameAddress<void>(kVTable);
    Ring().~ElementRing();
    if (flags & 1) ::operator delete(this, sizeof(*this));
    return this;
}
