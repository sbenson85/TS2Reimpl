#include "hook/hooks.h"
#include "simantics/PrimPerson.h"

namespace {

// The person's SimAntics primitive handlers (called from cTSPerson::vf332 and, for 0x522110/0x522300, vtable slots 361/362),
// lane Y.
const HookSpec kHooks[] = {
    {"Prim_03_Person", 0x521860, 32, 0x64dfdf40, MemberAddress(&PrimPersonObject::Prim03)},
    {"Prim_11_IdleForInput", 0x521b90, 32, 0x4ea17423, MemberAddress(&PrimPersonObject::IdleForInput)},
    {"cTSPerson_MotiveEmplaceReallocate", 0x50da10, 32, 0x0c9a01b5, reinterpret_cast<void *>(&MotiveEmplaceReallocate)},
    {"cTSPerson_MotiveReallocateExactly", 0x50f4d0, 32, 0x39d3cf08, reinterpret_cast<void *>(&MotiveReallocateExactly)},
    {"cTSPerson_MotiveInsertN", 0x524d30, 32, 0x95f123b3, reinterpret_cast<void *>(&MotiveInsertN)},
    {"cTSPerson_vf361_Prim1D", 0x522110, 32, 0x80eb4a8e, MemberAddress(&PrimPersonObject::Prim1D)},
    {"cTSPerson_vf362_Prim25", 0x522300, 32, 0x611c152f, MemberAddress(&PrimPersonObject::Prim25)},
    {"Prim_1E_Selector0", 0x525520, 32, 0x1e4048bd, MemberAddress(&PrimPersonObject::Prim1E0)},
};
REGISTER_HOOKS(kHooks);

} // namespace
