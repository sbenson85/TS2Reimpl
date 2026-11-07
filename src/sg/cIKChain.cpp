#include "sg/cIKChain.h"

#include "game/vtable.h"

#include <cstring>

namespace nGZSceneDBAnim {

namespace {

// Typed field access by byte offset. The fields are not all aligned, so they are copied rather than dereferenced.
template <typename T> T Load(const void *object, intptr_t offset)
{
    T value;
    std::memcpy(&value, static_cast<const uint8_t *>(object) + offset, sizeof(value));
    return value;
}

template <typename T> void Store(void *object, intptr_t offset, T value)
{
    std::memcpy(static_cast<uint8_t *>(object) + offset, &value, sizeof(value));
}

// Slots of this object and of its elements, as the original calls them (__thiscall; the arguments are on the stack).
using CountFn = uint32_t(__thiscall *)(void *);                   // slot 47 of the proxy
using ElementFn = uint32_t(__thiscall *)(void *, uint32_t);       // slot 23 of the proxy
using ElementKeyFn = uint32_t(__thiscall *)(void *);              // slot 15 of an element
using ElementTaggedFn = bool(__thiscall *)(void *, uint32_t, uint32_t); // slot 28 of an element
using ElementValueFn = bool(__thiscall *)(void *, uint32_t);      // slot 29 of an element

constexpr uint32_t kElementStride = 0x2b;
constexpr uint32_t kTagA = 0xfb5b3383u; // compared with +0x2e
constexpr uint32_t kTagB = 0xe9c7f1adu; // compared with +0x3d

} // namespace

// Slot 0 (QueryInterface) of the proxy. The original accepts riid 1, 0x1d55b138 and 0x5b0aaa74 (the three compares
// add up to 0x5b0aaa74), stores this into *ppv, AddRefs through slot 1 and returns al = 1. A null ppv or another
// riid returns al = 0. Only al is defined: the other bits of eax on the false paths are left over from the arguments.
bool cIKChainConstProxy::QueryInterface(uint32_t riid, void **ppv)
{
    if (ppv == nullptr) return false;
    if (riid != 1 && riid != 0x1d55b138u && riid != 0x5b0aaa74u) return false;
    *ppv = this;
    VirtualCall<uint32_t(__thiscall *)(void *), 1>(this);
    return true;
}

// Body: imul eax,[esp+4],0x2b; add eax,[ecx+0x42]; ret 4.
uint32_t cIKChainConstProxy::ElementAddress(uint32_t index)
{
    return index * kElementStride + Load<uint32_t>(this, 0x42);
}

// Body: movzx eax,word [ecx+8]; eax = [[ecx+4]+0x48] + 28 * word; ret. No arguments.
uint32_t cIKChainConstProxy::EntryAddress()
{
    const uint32_t word = Load<uint16_t>(this, 8);
    const void *holder = Load<const void *>(this, 4);
    return Load<uint32_t>(holder, 0x48) + word * 28;
}

// Body: count = slot 47; for i < count: if slot 23 (i) == value, return i. Returns -1 when absent.
int32_t cIKChainConstProxy::IndexOf(uint32_t value)
{
    const uint32_t count = VirtualCall<CountFn, 47>(this);
    for (uint32_t i = 0; i < count; ++i) {
        if (VirtualCall<ElementFn, 23>(this, i) == value) return static_cast<int32_t>(i);
    }
    return -1;
}

// Body: a tag equal to 0xfb5b3383 (or 0xe9c7f1ad) first compares value with +0x2e (or +0x3d) and returns true on a
// match; otherwise, and for any other tag, every element's slot 28 is called with (value, tag) until one returns
// true. The arguments reach the element in the order (value, tag), as the original pushes them.
bool cIKChainConstProxy::FindTagged(uint32_t value, uint32_t tag)
{
    if (tag == kTagA) {
        if (value == Load<uint32_t>(this, 0x2e)) return true;
    } else if (tag == kTagB) {
        if (value == Load<uint32_t>(this, 0x3d)) return true;
    }
    const uint32_t count = VirtualCall<CountFn, 47>(this);
    for (uint32_t i = 0; i < count; ++i) {
        void *element = reinterpret_cast<void *>(VirtualCall<ElementFn, 23>(this, i));
        if (VirtualCall<ElementTaggedFn, 28>(element, value, tag)) return true;
    }
    return false;
}

// Body: if [+0x33] == 0 and value == [+0x2e] return true; otherwise every element's slot 29 is called with value.
bool cIKChainConstProxy::AnyElementAccepts(uint32_t value)
{
    if (Load<uint32_t>(this, 0x33) == 0 && value == Load<uint32_t>(this, 0x2e)) return true;
    const uint32_t count = VirtualCall<CountFn, 47>(this);
    for (uint32_t i = 0; i < count; ++i) {
        void *element = reinterpret_cast<void *>(VirtualCall<ElementFn, 23>(this, i));
        if (VirtualCall<ElementValueFn, 29>(element, value)) return true;
    }
    return false;
}

// Body: if value == 1 and [+0x33] != 0 return true; otherwise true when some element's slot 15 returns value.
bool cIKChainConstProxy::ContainsValue(uint32_t value)
{
    if (value == 1 && Load<uint32_t>(this, 0x33) != 0) return true;
    const uint32_t count = VirtualCall<CountFn, 47>(this);
    for (uint32_t i = 0; i < count; ++i) {
        void *element = reinterpret_cast<void *>(VirtualCall<ElementFn, 23>(this, i));
        if (VirtualCall<ElementKeyFn, 15>(element) == value) return true;
    }
    return false;
}

namespace {

// The real `this` of a thunk: the subobject minus the vtordisp stored at subobject - 4. The thunk body (a `sub ecx,
// [ecx-4]` followed by a jump) has no constant adjustment, so this is the whole computation.
uintptr_t RealOf(const cIKChainSubobject *self)
{
    const uintptr_t address = reinterpret_cast<uintptr_t>(self);
    const uint32_t displacement = Load<uint32_t>(reinterpret_cast<const void *>(address - 4), 0);
    return address - displacement;
}

} // namespace

// Thunk 0x9e7cb0 (22 bytes): sub ecx,[ecx-4]; jmp 0x9e7cc0. Body: mov eax,[ecx-0x164]; inc eax; mov [ecx-0x164],eax;
// ret. The word is at real - 0x164; the new value is returned.
uint32_t cIKChainSubobject::IncrementWord()
{
    void *real = reinterpret_cast<void *>(RealOf(this));
    const uint32_t value = Load<uint32_t>(real, -0x164) + 1;
    Store<uint32_t>(real, -0x164, value);
    return value;
}

// Thunk 0x9e7cce (15 bytes): sub ecx,[ecx-4]; jmp 0x9e7ce0. Body: mov eax,[ecx-0x160]; ret.
uint32_t cIKChainSubobject::GetWord0C()
{
    return Load<uint32_t>(reinterpret_cast<const void *>(RealOf(this)), -0x160);
}

} // namespace nGZSceneDBAnim
