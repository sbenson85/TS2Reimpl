#pragma once
#include <cstdint>

// nGZSceneDBAnim::cAnimResource is a cSGResource-derived object. Its second subobject, at object +0xc4 (vtable
// 0xe65ec4, 25 slots), has the cSGResource secondary subobject's slots as thunks: each thunk computes
//   S = this - [this - 4] - 0x94
// (the dword before `this` is a vtordisp; 0x94 is the distance to the cSGResource secondary subobject, S = object
// + 0x30) and jumps to the body, which is the cSGResource code. The bodies in src/gz/cSGResource.cpp and
// src/sg/SGResourceHelpers.cpp are already verified with `this` = S, so the replacements here are forwarders:
// they compute S and call those bodies with the same arguments (`ret N` is the body's own N).
//
// Slot numbers are for this subobject vtable (slot N at byte offset 4N). The slots reimplemented here are the
// ones whose bodies are verified; the rest (slots 10 and 13: SubmitKeyToService and ResetAndSubmit) need the
// service fakes of tests/sg_resource_helpers.cpp and are not done yet.
namespace nGZSceneDBAnim {

class cAnimResourceSubobject {
public:
    static constexpr uint32_t kVTable = 0xe65ec4; // secondary vtable at subobject +0

    void *VirtualBaseIfObject();          // slot 6, thunk 0xa10195: body 0xa3c750
    void *GetOwner();                     // slot 7, thunk 0xa0f3ae: body 0xa3c3c0
    void *GetVirtualBase(void *arg);      // slot 9, thunk 0xa10187: body 0xa3c720, ret 4
    int32_t CompareIdentity(void *other); // slot 11, thunk 0xa0f554: body 0xa3c3d0, ret 4
    bool SetResource(void *resource);     // slot 15, thunk 0xa103ce: body 0xa3cdd0, ret 4
    void SetFlag(uint32_t value);         // slot 16, thunk 0xa101e0: body 0xa3c870, ret 4 (low byte stored)
    void *CopyKeyTo(void *out);           // slot 22, thunk 0xa101a3: body 0xa3c770, ret 4
    uint8_t GetFlag();                    // slot 23, thunk 0xa0f57b: body 0xa3c4a0 (al only)
    uint32_t ForwardKey();                // slot 24, thunk 0xa0fb19: body 0xa3c6d0
};

} // namespace nGZSceneDBAnim
