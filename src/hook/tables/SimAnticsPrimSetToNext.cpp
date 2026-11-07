#include "hook/hooks.h"
#include "simantics/PrimSetToNext.h"

namespace {

// SimAntics Set To Next primitive (opcode 0x1f of cTSObject::vf332), handler(this = object, frame, param), ret 8.
const HookSpec kHooks[] = {
    {"Prim_SetToNext", 0x5a12c0, 32, 0xfb5d25ac, MemberAddress(&SetToNextObject::SetToNext)},
};
REGISTER_HOOKS(kHooks);

} // namespace
