#include "hook/hooks.h"
#include "simantics/PrimDispatch.h"

namespace {

// The SimAntics primitive dispatch glue: the opcode switches of cTSObject and cTSPerson (slot 332) and the
// person's operand reader (slot 7 of its cTSTreeSim subobject), lane V.
const HookSpec kHooks[] = {
    {"cTSObject_vf332", 0x534650, 32, 0x68ec902d, MemberAddress(&PrimDispatchObject::Dispatch)},
    {"cTSPerson_vf332", 0x51e4c0, 32, 0xce6c089c, MemberAddress(&PrimDispatchPerson::Dispatch)},
    {"cTSPerson_vf4_7", 0x51b870, 32, 0xa25bec4a, MemberAddress(&PersonOperandSource::Read)},
};
REGISTER_HOOKS(kHooks);

} // namespace
