#include "sg/cSceneDBAnimSystem.h"

#include "game/vtable.h"
#include "rz/QueryInterfaces.h"
#include "rz/cRZRandom.h"

#include <cstdint>

namespace nGZSceneDBAnim {

namespace {

// A field of the object at a fixed offset from `this`.
template <typename T> T &At(cSceneDBAnimSystem *self, int offset)
{
    return *reinterpret_cast<T *>(reinterpret_cast<uint8_t *>(self) + offset);
}

using SlotReturn = uint32_t(__thiscall *)(void *);
using SlotIndexed = uint32_t(__thiscall *)(void *, uint32_t);
using SlotVoidArg = void(__thiscall *)(void *, uint32_t);

// AddRef through the object's own vtable (slot 1).
void AddRef(void *object)
{
    VirtualCall<SlotReturn, 1>(object);
}

// The element at `index` of a list whose begin pointer is stored as a 32-bit value (no bounds check, as in
// the original: the index is scaled and added in 32 bits).
uint32_t ElementOf(uint32_t begin, uint32_t index)
{
    return *reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(begin + index * 4u));
}

// Index of the first element equal to `value` among `count` elements from `begin`, or -1. The original
// scans with an unsigned counter against the signed count, so a negative count scans a huge range; callers
// never pass one.
int32_t IndexOf(uint32_t begin, int32_t count, uint32_t value)
{
    if (count == 0) return -1;
    uint32_t n = static_cast<uint32_t>(count);
    for (uint32_t i = 0; i < n; ++i) {
        if (ElementOf(begin, i) == value) return static_cast<int32_t>(i);
    }
    return -1;
}

// Count of a list from its begin and end words (bytes between them divided by 4, rounded towards minus
// infinity: sar).
int32_t CountOf(uint32_t begin, uint32_t end)
{
    return static_cast<int32_t>(end - begin) >> 2;
}

} // namespace

// 0x9db320 (61 bytes, ret 8). A null ppv fails. riid 0x5afeed72 or 0xfafeed6c stores this in *ppv, AddRefs it
// through slot 1 and returns 1. Anything else tail-jumps to cRZSystemService::QueryInterface (0x1b9650) with
// this + 4, which is the shared function in src/rz/QueryInterfaces.cpp.
bool cSceneDBAnimSystem::QueryInterface(uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid == 0x5afeed72 || riid == 0xfafeed6c) {
        *ppv = this;
        AddRef(this);
        return true;
    }
    return qi::SystemServiceQueryInterface(reinterpret_cast<uint8_t *>(this) + kSubobjectOffset, nullptr, riid, ppv);
}

// 0x9dac40 (13 bytes, ret 4): mov ecx,[ecx+0x20]; mov eax,[esp+4]; mov eax,[ecx+eax*4]; ret 4
uint32_t cSceneDBAnimSystem::ListAElementAt(uint32_t index)
{
    return ElementOf(At<uint32_t>(this, 0x20), index);
}

// 0x9dabe0 (13 bytes, ret 4): as ListAElementAt, with the list at +0x2c.
uint32_t cSceneDBAnimSystem::ListBElementAt(uint32_t index)
{
    return ElementOf(At<uint32_t>(this, 0x2c), index);
}

// 0x9dabf0 (80 bytes, ret 4). count = slot 32 (ListBCount). For each i below count: obj = slot 8 (ListBElementAt(i)),
// and if obj->slot 13() equals value, returns slot 8 (ListBElementAt(i)) again. Otherwise returns 0. Calls go
// through this's vtable and through the element's, as the original does.
uint32_t cSceneDBAnimSystem::FindVirtualByValue(uint32_t value)
{
    int32_t count = static_cast<int32_t>(VirtualCall<SlotReturn, 32>(this));
    for (int32_t i = 0; i < count; ++i) {
        uint32_t element = VirtualCall<SlotIndexed, 8>(this, static_cast<uint32_t>(i));
        void *object = reinterpret_cast<void *>(static_cast<uintptr_t>(element));
        if (VirtualCall<SlotReturn, 13>(object) == value) {
            return VirtualCall<SlotIndexed, 8>(this, static_cast<uint32_t>(i));
        }
    }
    return 0;
}

// 0x9dba10 (38 bytes, ret 4): stores the argument at +0x64. While the +0x68 counter is above the +0x64 value,
// calls slot 16 with the +0x74 word as its argument. The first test compares with the argument, the loop
// compares with the stored +0x64 word, which the callee may change.
void cSceneDBAnimSystem::SetCounter64AndDrain(uint32_t value)
{
    At<uint32_t>(this, 0x64) = value;
    if (At<uint32_t>(this, 0x68) > value) {
        do {
            VirtualCall<SlotVoidArg, 16>(this, At<uint32_t>(this, 0x74));
        } while (At<uint32_t>(this, 0x68) > At<uint32_t>(this, 0x64));
    }
}

// 0x9dada0 (34 bytes): while the +0x68 counter is non-zero, calls slot 16 with the +0x74 word.
void cSceneDBAnimSystem::DrainWhileCounter68()
{
    while (At<uint32_t>(this, 0x68) > 0) {
        VirtualCall<SlotVoidArg, 16>(this, At<uint32_t>(this, 0x74));
    }
}

// 0x9dadd0 (34 bytes): while the +0x54 counter is non-zero, calls slot 16 with the +0x60 word.
void cSceneDBAnimSystem::DrainWhileCounter54()
{
    while (At<uint32_t>(this, 0x54) > 0) {
        VirtualCall<SlotVoidArg, 16>(this, At<uint32_t>(this, 0x60));
    }
}

// 0x9db870 (21 bytes): the words at +0xb8 and +0xbc are set to zero.
void cSceneDBAnimSystem::ResetWords()
{
    At<uint32_t>(this, 0xb8) = 0;
    At<uint32_t>(this, 0xbc) = 0;
}

// 0x9daef0 (41 bytes, ret 4): index of the first list A element equal to the argument, or -1.
int32_t cSceneDBAnimSystem::FindInListA(uint32_t value)
{
    uint32_t begin = At<uint32_t>(this, 0x20);
    int32_t count = CountOf(begin, At<uint32_t>(this, 0x24));
    return IndexOf(begin, count, value);
}

// 0x9daec0 (41 bytes, ret 4): as FindInListA, on list B (+0x2c, end +0x30).
int32_t cSceneDBAnimSystem::FindInListB(uint32_t value)
{
    uint32_t begin = At<uint32_t>(this, 0x2c);
    int32_t count = CountOf(begin, At<uint32_t>(this, 0x30));
    return IndexOf(begin, count, value);
}

// 0x9db270 (10 bytes): (end - begin) >> 2 for list B, signed.
int32_t cSceneDBAnimSystem::ListBCount()
{
    return CountOf(At<uint32_t>(this, 0x2c), At<uint32_t>(this, 0x30));
}

// 0x9db390 (8 bytes): add ecx,0x38; jmp cRZRandom::RandomUint32Uniform (0x1b8d75). Forwards to the cRZRandom
// at +0x38 with no arguments.
uint32_t cSceneDBAnimSystem::RandomUint32()
{
    return reinterpret_cast<cRZRandom *>(reinterpret_cast<uint8_t *>(this) + 0x38)->RandomUint32Uniform();
}

// 0x9db370 (17 bytes): calls cRZRandom::RandomDoubleUniform (0x1b8c78) on the cRZRandom at +0x38, stores the
// double as a single-precision float (fstp dword) and reloads it. The result is returned in st(0).
float cSceneDBAnimSystem::RandomFloat()
{
    double value = reinterpret_cast<cRZRandom *>(reinterpret_cast<uint8_t *>(this) + 0x38)->RandomDoubleUniform();
    return static_cast<float>(value);
}

} // namespace nGZSceneDBAnim
