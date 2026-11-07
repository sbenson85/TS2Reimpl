#pragma once
#include <cstdint>

// Slot bodies of nGZSceneGraph::cGeometryNode (SGR geometry node, GMND resource) and of the cShape resource
// (SHPE). `this` in each class is the subobject the vtable slot is reached through, as in the original, so a
// caller passes object + offset. RVAs are 1.18.0.187. Only the bodies are reimplemented; the thunks in the
// vtables are original code.
//
// cGeometryNode layout as far as these bodies touch it (object offsets):
//   +0x00 primary vtable 0xe6987c          +0x08 subobject vtable 0xe6994c (7 slots)
//   +0x14 cSGResource (its secondary subobject is at +0x44, see SGResourceHelpers::cSGSecondary)
//   +0x40 cIGeometryRender subobject, vtable 0xe699a4
//   +0x78 base of the element array; an element is 0x7c bytes, its dword at +0x38 is the element's value
//   +0xd4 / +0xd8 begin / end of a vector of interface pointers (elements of this node)
//   +0xe0 refcounted pointer (AddRef on store, Release on replace), +0xe4..+0xf3 a 16-byte block
//   +0x10c, +0x118 base pointers of dword arrays (Dispatch8 reads the dword at base + 4 * index)
//   +0x124 dword field (SetField124 compares and stores it)
//   +0x2a8 interface pointer of an object owning a render interface (ForwardElement calls its slot 32)
//
// cShape layout (object offsets): the cIShape subobject is at +0x2c (vtable 0xe6f54c, 62 slots). Its +0x20 is
// the begin pointer of a record vector (records are 0x20 bytes, the dword at +4 is the record's field) and +0x24
// its end. A record's +4 is the target object for QueryPair, whose slots 47 and 48 are called.
namespace nGZSceneGraph {

class cGeometryNodeObject {
public:
    static constexpr uint32_t kPrimaryVTable = 0xe6987c;

    // 0xa42980 (`ret 4`, 19 bytes): Dispatch8 (0xaeb800) on the element's dword at +0x38. Element = array + index * 0x7c.
    uint32_t Dispatch8OfElement(uint32_t index);
    // 0xa42800 (`ret 4`, 21 bytes): Dispatch8 on array[index] where the array base is the dword at +0x10c.
    uint32_t Dispatch8OfWordAt10c(uint32_t index);
    // 0xa427e0 (`ret 4`, 21 bytes): as above with the array base at +0x118.
    uint32_t Dispatch8OfWordAt118(uint32_t index);
    // 0xa429a0 (`ret 4`, 15 bytes): the dword at +0x38 of element `index`.
    uint32_t ElementWordAt38(uint32_t index);
    // 0xa43510 (`ret 4`, 29 bytes): stores the argument in +0x124 if it differs, and then calls this's slot 34.
    void SetField124(uint32_t value);
    // 0xa42ac0 (`ret 8`, 75 bytes): calls slot 0 (a QueryInterface-shaped slot) of each element of the vector
    // at +0xd4 with (a1, a2), in order, until one returns non-zero. Returns 1 for a non-zero al, else 0 (the upper
    // bits of eax are not defined here; callers use al). The end pointer is re-read after every call, as the
    // original does.
    uint8_t AnyElementAccepts(uint32_t a1, uint32_t a2);
    // 0xa43530 (`ret 4`, 103 bytes): replaces the refcounted pointer at +0xe0 with `value`: AddRef (slot 1) on
    // a new non-null value before the store, Release (slot 2) on the old one after it. Zeroes +0xe4..+0xf3.
    void ReplaceRefE0(void *value);
};

// The secondary subobject at object +0x08 (7-slot vtable 0xe6994c).
class cGeometryNodeSecondary8 {
public:
    // 0xa431c0 (8 bytes: `add ecx,0x3c; jmp 0xa3c750`): VirtualBaseIfObject of the cSGResource secondary subobject at
    // this + 0x3c.
    void *VirtualBaseOfResource();
};

// The cIGeometryRender subobject at object +0x40 (vtable 0xe699a4).
class cGeometryNodeRenderSubobject {
public:
    // 0xa42970 (`ret 4`, 11 bytes): element `index` (array + index * 0x7c).
    void *ElementPointer(uint32_t index);
    // 0xa423c0 (`ret 0xc`, 46 bytes): calls slot 32 of owner->+0x2a8 with (elem+0x4c, elem+4, arg, 0, elem+0, elem+0x50)
    // where elem is element `index`. Returns the callee's full eax.
    uint32_t ForwardElement(uint32_t index, void *owner, uint32_t arg);
};

// The cIShape subobject of nGZSceneGraph::cShape at object +0x2c (vtable 0xe6f54c).
class cShapeIShapeSubobject {
public:
    // 0xa5d420 (no arguments, 15 bytes): the dword at +4 of the record at +0x20 when +0x20 != +0x24, else 0.
    uint32_t FirstRecordWord();
    // 0xa5e170 (`ret 8`, 54 bytes): the record at this +0x20 gives a target (its +4); out[0] = target slot 47 (arg),
    // out[1] = target slot 48 (arg). Returns out (the hidden result pointer, a1).
    void *QueryPair(void *out, uint32_t arg);
};

} // namespace nGZSceneGraph
