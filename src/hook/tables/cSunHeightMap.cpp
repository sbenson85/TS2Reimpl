#include "hook/hooks.h"
#include "sg/cSunHeightMap.h"

namespace {

const HookSpec kHooks[] = {
    {"nTSSG_cSunHeightMap_ctor", 0x3be100, 32, 0xd895900f, MemberAddress(&nTSSG::cSunHeightMap::Construct)},
    {"nTSSG_cSunHeightMap_ResetFlat", 0x3be1a0, 27, 0xc466bf8b, MemberAddress(&nTSSG::cSunHeightMap::ResetFlat)},
    {"nTSSG_cSunHeightMap_AddAwnings", 0x3be1c0, 32, 0x96aedbcf, reinterpret_cast<void *>(&nTSSG::cSunHeightMap::AddAwnings)},
    {"nTSSG_cSunHeightMap_AddRoofs", 0x3be5c0, 32, 0xc6a3a75d, reinterpret_cast<void *>(&nTSSG::cSunHeightMap::AddRoofs)},
    {"nTSSG_cSunHeightMap_BuildGround", 0x3bea70, 32, 0xd86c7894, reinterpret_cast<void *>(&nTSSG::cSunHeightMap::BuildGround)},
    {"nTSSG_cSunHeightMap_AddWalls", 0x3bef50, 32, 0x9feb60c4, reinterpret_cast<void *>(&nTSSG::cSunHeightMap::AddWalls)},
    {"nTSSG_cSunHeightMap_BuildShadow", 0x3bfde0, 32, 0x9864bca0, MemberAddress(&nTSSG::cSunHeightMap::BuildShadow)},
    {"nTSSG_cSunHeightMap_SweepX", 0x3bfe60, 32, 0x2805cb71, MemberAddress(&nTSSG::cSunHeightMap::SweepX)},
    {"nTSSG_cSunHeightMap_SweepY", 0x3c0280, 32, 0xd4e5c811, MemberAddress(&nTSSG::cSunHeightMap::SweepY)},
    {"nTSSG_cSunHeightMap_FloorVisible", 0x3c06e0, 32, 0x8fc6cb46, reinterpret_cast<void *>(&nTSSG::cSunHeightMap::FloorVisible)},
    {"nTSSG_cSunHeightMap_Resize", 0x3c0750, 32, 0x4680efd5, MemberAddress(&nTSSG::cSunHeightMap::Resize)},
};
REGISTER_HOOKS(kHooks);

} // namespace
