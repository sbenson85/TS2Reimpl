#include "hook/hooks.h"
#include "rz/RZAutoRef.h"

namespace {

// Sites that share a body (identical bytes, same CRC) are hooked separately, each to the same
// reimplementation.
const HookSpec kHooks[] = {
    {"RZAutoRef_ReleaseObject", 0x1466e0, 32, 0x33f4b5e7, MemberAddress(&RZAutoRef::ReleaseObject)},
    {"RZAutoRef_ReleaseUnguarded", 0x1cbd45, 32, 0xc855ad76, MemberAddress(&RZAutoRef::ReleaseUnguarded)},
    {"RZAutoRef_ReleaseUnguarded_2", 0x145d50, 32, 0xc855ad76, MemberAddress(&RZAutoRef::ReleaseUnguarded)},
    {"RZAutoRef_DeletingDestructor", 0x142196, 32, 0x13d3f8a6, MemberAddress(&RZAutoRef::DeletingDestructor)},
    {"RZAutoRef_Reset", 0x1dae60, 24, 0x1c2caf07, MemberAddress(&RZAutoRef::Reset)},
    {"RZAutoRef_ReleaseVirtualBase", 0x210cfc, 32, 0xb2dc0307, MemberAddress(&RZAutoRef::ReleaseVirtualBase)},
    {"RZAutoRef_ReleaseVirtualBase_2", 0x1fcd10, 32, 0xb2dc0307, MemberAddress(&RZAutoRef::ReleaseVirtualBase)},
    {"RZAutoRef_ResetVirtualBase", 0x2296b4, 30, 0x3d7b5898, MemberAddress(&RZAutoRef::ResetVirtualBase)},
    {"RZAutoRef_ResetVirtualBase_2", 0x258c70, 32, 0x14d3b229, MemberAddress(&RZAutoRef::ResetVirtualBase)},
    {"RZRefField10_ReleaseRef", 0x416c90, 32, 0x9456aacb, MemberAddress(&RZRefField<0x10>::ReleaseRef)},
    {"RZRefField10_AssignRef", 0x9981d0, 32, 0x4ae87f89, MemberAddress(&RZRefField<0x10>::AssignRef)},
    {"RZRefField10_AssignRefAlways", 0x991253, 32, 0x48652857, MemberAddress(&RZRefField<0x10>::AssignRefAlways)},
    {"RZRefField28_AssignRef", 0x9d0c32, 32, 0xe3d1c8c4, MemberAddress(&RZRefField<0x28>::AssignRef)},
};
REGISTER_HOOKS(kHooks);

} // namespace
