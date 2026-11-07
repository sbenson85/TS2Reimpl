#include "hook/hooks.h"
#include "world/cSerializableMap.h"

namespace {

using nTSWorld::cGuidMap;
using nTSWorld::cStringPairMap;

const HookSpec kHooks[] = {
    {"AV_cSerializableMap_KK_cGZGUIDLessThan_vf3", 0x81ac60, 32, 0x65ed218d, MemberAddress(&cGuidMap::DeletingDestructor)},
    {"AV_cSerializableMap_KK_cGZGUIDLessThan_Write", 0x81d0f0, 32, 0x73b0819c, MemberAddress(&cGuidMap::Face::Write)},
    {"AV_cSerializableMap_KK_cGZGUIDLessThan_Read", 0x81bc20, 32, 0x0132af88, MemberAddress(&cGuidMap::Face::Read)},
    {"AV_cSerializableMap_cRZString_cRZStringLessThanNoCase_vf3", 0x81aca0, 32, 0xe31ba8da, MemberAddress(&cStringPairMap::DeletingDestructor)},
    {"AV_cSerializableMap_cRZString_cRZStringLessThanNoCase_Write", 0x81d190, 32, 0x73b0819c, MemberAddress(&cStringPairMap::Face::Write)},
    {"AV_cSerializableMap_cRZString_cRZStringLessThanNoCase_Read", 0x81bd90, 32, 0x26e61f03, MemberAddress(&cStringPairMap::Face::Read)},
    {"nTSWorld_cSerializableMapString_InsertUnique", 0x81a8a0, 32, 0xee5f9e7e, MemberAddress(&cStringPairMap::Tree::Insert)},
    // The shared bodies below are referenced only from the two copies' vtables (0xe05168 / 0xe0518c, 0xe051a8 / 0xe051cc),
    // both copies ours. The string copy's slots reach them with the same layout (the map at +0xc is not touched by these).
    {"cSerializableMap_QueryInterface", 0x81bb90, 32, 0x833523ba, MemberAddress(&cGuidMap::QueryInterface)},
    {"cSerializableMap_Face_QueryInterface", 0x81bbcf, 8, 0x87fdbe55, MemberAddress(&cGuidMap::Face::QueryInterface)},
    {"cSerializableMap_GetGZCLSID", 0x81adb0, 6, 0x11812056, MemberAddress(&cGuidMap::Face::GetGZCLSID)},
};
REGISTER_HOOKS(kHooks);

} // namespace
