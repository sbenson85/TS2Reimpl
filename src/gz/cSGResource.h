#pragma once
#include <cstddef>
#include <cstdint>

// nGZSceneGraph::cSGResource is a cGZPersistResource-derived object whose primary vtable (0xe695a4) is at
// +0 and which has a second, secondary vtable (0xe695dc, 25 slots) at +0x30. The functions below are that
// secondary subobject's own slots: their `this` is object + 0x30, and they are reached through the subobject
// vtable's thunks (`sub ecx, [ecx-4]` then a jump to the body, where [object+0x2c] is the vtordisp). The
// bodies are reimplemented here with `this` = the subobject, so a caller passes object + 0x30. Only the
// bodies are hooked; the thunks are original code.
//
// Layout relative to the subobject (S = object + 0x30), as far as these functions touch it:
//   S-0x30 (object +0x00): primary vtable pointer (0xe695a4)
//   S-0x14 (object +0x1c): virtual base table pointer; the virtual base is at (S-0x14) + vbtable[1]
//   S-0x10 (object +0x20): member not used here (a helper object; its methods are original code)
//   S-0x0c (object +0x24): resource pointer (`edi` below); refcounted by SetResource
//   S-0x08 (object +0x28): flag byte (ForwardKey checks it)
//   S-0x07 (object +0x29): flag byte (GetFlag / SetFlag)
//   S-0x04 (object +0x2c): vtordisp (read by the subobject's thunks, not used by these bodies)
//   S-0x0c..S-0x1c  primary object bytes at object +0x0c..+0x1b: a 16-byte key copied by the primary slot 3
//
// Slot numbers are for the subobject vtable (slot N is at byte offset 4N). RVAs are 1.18.0.187.
namespace nGZSceneGraph {

class cSGResourceSubobject {
public:
    // The game's vtables (secondary 0xe695dc at subobject +0, primary 0xe695a4 at object +0) are named in the tests only:
    // src/ holding them would keep them live for tools/callers.py.
    static constexpr int kPrimaryKeySlot = 3;            // primary slot 3 (0x18a670): copies the 16-byte key to its argument, ret 4
    static constexpr int kResourceForwardSlot = 21;      // slot 21 of the resource object: called with the key pointer

    void *GetOwner();                    // slot 7, 0xa3c3c0: object (this - 0x30); 4 bytes, not hooked
    int32_t CompareIdentity(void *other); // slot 11, 0xa3c3d0: ret 4; 0 if other and the virtual base agree under one IID
    uint8_t GetFlag();                   // slot 23, 0xa3c4a0: the flag byte at S-0x07; 4 bytes, not hooked
    void SetFlag(uint8_t value);         // slot 16, 0xa3c870: ret 4; stores the low byte of its argument at S-0x07
    void *GetVirtualBase(void *arg);     // slot 9, 0xa3c720: ret 4; the virtual base if arg is null and the object is not null
    void *CopyKeyTo(void *out);          // slot 22, 0xa3c770: ret 4; primary slot 3 fills out, returns out
    uint32_t ForwardKey();               // slot 24, 0xa3c6d0: the resource's slot 21 with the key, or -1
    bool SetResource(void *resource);    // slot 15, 0xa3cdd0: ret 4; swaps the resource at S-0x0c, AddRef new, Release old

    // Never constructed: `this` always points into an existing cSGResource (object + 0x30).
};

} // namespace nGZSceneGraph
