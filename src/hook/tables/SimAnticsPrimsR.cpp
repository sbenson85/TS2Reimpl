#include "hook/hooks.h"
#include "simantics/PrimR.h"

namespace {

// SimAntics primitive handlers (called from cTSObject::vf332's switch), lane R: opcodes 0x0d, 0x0e, 0x10, 0x12,
// 0x1a and 0x1c.
const HookSpec kHooks[] = {
    {"Prim_0D", 0x5a0560, 32, 0xc0471eb8, MemberAddress(&PrimRObject::Prim0D)},
    {"Prim_0E", 0x58a2c0, 32, 0x303275b7, MemberAddress(&PrimRObject::Prim0E)},
    {"Prim_10", 0x58ae50, 32, 0xbd026eb6, MemberAddress(&PrimRObject::Prim10)},
    {"Prim_12", 0x59cba0, 32, 0xfbcdec1f, MemberAddress(&PrimRObject::Prim12)},
    {"Prim_1A", 0x59e6e0, 32, 0x79bc7073, MemberAddress(&PrimRObject::Prim1A)},
    {"Prim_1C", 0x53de40, 32, 0x3ed2142c, MemberAddress(&PrimRObject::Prim1C)},
};
REGISTER_HOOKS(kHooks);

} // namespace
