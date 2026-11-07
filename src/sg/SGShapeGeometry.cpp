#include "sg/SGShapeGeometry.h"
#include "game/vtable.h"
#include "rz/StaticGetters2.h"
#include "sg/SGResourceHelpers.h"

#include <cstring>

namespace nGZSceneGraph {

namespace {

uint8_t *Bytes(const void *self)
{
    return reinterpret_cast<uint8_t *>(const_cast<void *>(self));
}

// A dword or pointer field at `off` (the originals read these with a plain `mov`).
template <typename T> T Load(const uint8_t *base, uint32_t off)
{
    T v;
    std::memcpy(&v, base + off, sizeof(v));
    return v;
}

template <typename T> void Store(uint8_t *base, uint32_t off, T v)
{
    std::memcpy(base + off, &v, sizeof(v));
}

// Element `index` of an array whose elements are 0x7c bytes: `imul eax,[esp+4],0x7c; add eax,base`. The product wraps
// at 32 bits, as it does in the original.
uint8_t *ElementAt(const uint8_t *array, uint32_t index)
{
    return reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(reinterpret_cast<uintptr_t>(array)) +
                                       static_cast<uint32_t>(index * 0x7cu));
}

} // namespace

uint32_t cGeometryNodeObject::Dispatch8OfElement(uint32_t index)
{
    uint8_t *self = Bytes(this);
    uint8_t *element = ElementAt(Load<uint8_t *>(self, 0x78), index);
    // The original passes element + 0x38 in ecx to Dispatch8 (0xaeb800), which reads the dword there.
    return Dispatch8(element + 0x38);
}

uint32_t cGeometryNodeObject::Dispatch8OfWordAt10c(uint32_t index)
{
    uint8_t *self = Bytes(this);
    uint8_t *word = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(reinterpret_cast<uintptr_t>(Load<uint8_t *>(self, 0x10c))) +
                                                static_cast<uint32_t>(index * 4u));
    return Dispatch8(word);
}

uint32_t cGeometryNodeObject::Dispatch8OfWordAt118(uint32_t index)
{
    uint8_t *self = Bytes(this);
    uint8_t *word = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(reinterpret_cast<uintptr_t>(Load<uint8_t *>(self, 0x118))) +
                                                static_cast<uint32_t>(index * 4u));
    return Dispatch8(word);
}

uint32_t cGeometryNodeObject::ElementWordAt38(uint32_t index)
{
    uint8_t *self = Bytes(this);
    return Load<uint32_t>(ElementAt(Load<uint8_t *>(self, 0x78), index), 0x38);
}

void cGeometryNodeObject::SetField124(uint32_t value)
{
    uint8_t *self = Bytes(this);
    if (Load<uint32_t>(self, 0x124) == value) return;
    Store<uint32_t>(self, 0x124, value);
    // Slot 34 of this object's own primary vtable, called with no arguments.
    VirtualCall<void(__thiscall *)(void *), 34>(this);
}

uint8_t cGeometryNodeObject::AnyElementAccepts(uint32_t a1, uint32_t a2)
{
    uint8_t *self = Bytes(this);
    uint8_t *const *it = Load<uint8_t *const *>(self, 0xd4);
    if (it == Load<uint8_t *const *>(self, 0xd8)) return 0;
    do {
        void *element = *it;
        // Slot 0 of the element, called with (a1, a2) and the element as this. Only al is the result.
        uint8_t accepted = VirtualCall<uint8_t(__thiscall *)(void *, uint32_t, uint32_t), 0>(element, a1, a2);
        if (accepted != 0) return 1;
        ++it;
    } while (it != Load<uint8_t *const *>(self, 0xd8));
    return 0;
}

void cGeometryNodeObject::ReplaceRefE0(void *value)
{
    uint8_t *self = Bytes(this);
    void *old = Load<void *>(self, 0xe0);
    if (value != old) {
        if (value != nullptr) VirtualCall<void(__thiscall *)(void *), 1>(value);
        Store<void *>(self, 0xe0, value);
        if (old != nullptr) VirtualCall<void(__thiscall *)(void *), 2>(old);
    }
    std::memset(self + 0xe4, 0, 16);
}

void *cGeometryNodeSecondary8::VirtualBaseOfResource()
{
    // The thunk adds 0x3c to this: that is the cSGResource secondary subobject of the node.
    auto *resource = reinterpret_cast<SGResourceHelpers::cSGSecondary *>(Bytes(this) + 0x3c);
    return resource->VirtualBaseIfObject();
}

void *cGeometryNodeRenderSubobject::ElementPointer(uint32_t index)
{
    // The subobject's +0x38 is the object's +0x78, the element array base.
    return ElementAt(Load<uint8_t *>(Bytes(this), 0x38), index);
}

uint32_t cGeometryNodeRenderSubobject::ForwardElement(uint32_t index, void *owner, uint32_t arg)
{
    uint8_t *element = ElementAt(Load<uint8_t *>(Bytes(this), 0x38), index);
    void *render = Load<void *>(Bytes(owner), 0x2a8);
    // Pushes in the original, first to last: elem+0x50, elem+0, 0, arg, elem+4, elem+0x4c. Callee-cleaned (ret 0x18).
    return VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t), 32>(render, Load<uint32_t>(element, 0x4c), Load<uint32_t>(element, 4), arg, 0u, Load<uint32_t>(element, 0), Load<uint32_t>(element, 0x50));
}

uint32_t cShapeIShapeSubobject::FirstRecordWord()
{
    uint8_t *self = Bytes(this);
    const uint8_t *begin = Load<uint8_t *>(self, 0x20);
    if (begin == Load<uint8_t *>(self, 0x24)) return 0;
    return Load<uint32_t>(begin, 4);
}

void *cShapeIShapeSubobject::QueryPair(void *out, uint32_t arg)
{
    uint8_t *self = Bytes(this);
    void *target = Load<void *>(Load<uint8_t *>(self, 0x20), 4);
    // Both calls use the same target and argument; the first result is kept while the second call is made.
    const uint32_t first = VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 47>(target, arg);
    const uint32_t second = VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 48>(target, arg);
    uint8_t *result = static_cast<uint8_t *>(out);
    Store<uint32_t>(result, 0, first);
    Store<uint32_t>(result, 4, second);
    return out;
}

} // namespace nGZSceneGraph
