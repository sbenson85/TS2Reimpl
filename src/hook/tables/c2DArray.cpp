#include "hook/hooks.h"
#include "ts/c2DArray.h"

namespace {

using nTSWorld::c2DArray;
using nTSWorld::tVertPaintInfo;

const HookSpec kHooks[] = {
    {"c2DArrayU8_Resize", 0x4b1d20, 32, 0x57fdbae6, MemberAddress(&c2DArray<uint8_t>::Resize)},
    {"c2DArrayF32_Resize", 0x4b1db0, 32, 0xd9c0a65d, MemberAddress(&c2DArray<float>::Resize)},
    {"AV_c2DArray_E_nTSWorld_Write", 0x4b2d80, 32, 0x7214056f, MemberAddress(&c2DArray<uint8_t>::Write)},
    {"AV_c2DArray_M_nTSWorld_Write", 0x4b2e90, 32, 0x7214056f, MemberAddress(&c2DArray<float>::Write)},
    {"AV_c2DArray_E_nTSWorld_Read", 0x4b0770, 32, 0xc2eb8d0a, MemberAddress(&c2DArray<uint8_t>::Read)},
    {"AV_c2DArray_M_nTSWorld_Read", 0x4b0870, 32, 0xc2eb8d0a, MemberAddress(&c2DArray<float>::Read)},
    {"AV_c2DArray_E_nTSWorld_vf6", 0x4ad200, 32, 0xbe2a4a0d, MemberAddress(&c2DArray<uint8_t>::DeletingDestructor)},
    {"AV_c2DArray_M_nTSWorld_vf6", 0x4ad220, 32, 0xccda931d, MemberAddress(&c2DArray<float>::DeletingDestructor)},
    // tVertPaintInfo (lane VP)
    {"AV_c2DArray_UtVertPaintInfo_cWorldTerrain_nTSWorld_nTSWorld_Write", 0x804d40, 32, 0x61411bef, MemberAddress(&c2DArray<tVertPaintInfo>::Write)},
    {"AV_c2DArray_UtVertPaintInfo_cWorldTerrain_nTSWorld_nTSWorld_Read", 0x803340, 32, 0x7c40d549, MemberAddress(&c2DArray<tVertPaintInfo>::Read)},
    {"AV_c2DArray_UtVertPaintInfo_cWorldTerrain_nTSWorld_nTSWorld_vf6", 0x801730, 32, 0x34651548, MemberAddress(&c2DArray<tVertPaintInfo>::DeletingDestructor)},
    {"c2DArrayPaint_Destruct", 0x801440, 32, 0x942e965f, MemberAddress(&c2DArray<tVertPaintInfo>::Destruct)},
    {"c2DArrayPaint_Resize", 0x8040b0, 32, 0x5bb87e6a, MemberAddress(&c2DArray<tVertPaintInfo>::Resize)},
    // shared_vf_4af570: slot 5 of the three copies only (their vtables 0xdcc124 / 0xdcc260 / 0xe0464c are its only references)
    {"c2DArray_GetGZCLSID", 0x4af570, 6, 0x015c5cc8, MemberAddress(&c2DArray<float>::GetGZCLSID)},
};
REGISTER_HOOKS(kHooks);

} // namespace
