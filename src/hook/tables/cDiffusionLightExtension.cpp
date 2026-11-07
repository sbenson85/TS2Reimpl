#include "hook/hooks.h"
#include "sg/cDiffusionLightExtension.h"

namespace {

using Sub = nTSSG::cIDiffusionLightExtensionSub;

const HookSpec kHooks[] = {
    {"nTSSG_cDiffusionLightExtension_vf180_0", 0x2ea27b, 32, 0x660684c7, MemberAddress(&Sub::ResetFlags)},
    {"nTSSG_cDiffusionLightExtension_vf180_1", 0x2ea948, 30, 0x142cb16f, MemberAddress(&Sub::SetPairP1)},
    {"nTSSG_cDiffusionLightExtension_vf180_3", 0x2ea842, 26, 0xf7739d53, MemberAddress(&Sub::SetFloatF3)},
    {"nTSSG_cDiffusionLightExtension_vf180_4", 0x2ea749, 32, 0xab7815bc, MemberAddress(&Sub::SetVec3V4)},
    {"nTSSG_cDiffusionLightExtension_vf180_5", 0x2ea8ba, 22, 0x3f756140, MemberAddress(&Sub::SetByteB5)},
    {"nTSSG_cDiffusionLightExtension_vf180_8", 0x2ea966, 26, 0xfa5b620d, MemberAddress(&Sub::SetFloatF8)},
    {"nTSSG_cDiffusionLightExtension_vf180_10", 0x2ea87e, 26, 0x89cfc922, MemberAddress(&Sub::SetFloatF10)},
    {"nTSSG_cDiffusionLightExtension_vf180_11", 0x2ea926, 32, 0x0fb5d9c3, MemberAddress(&Sub::SetPairD11)},
    {"nTSSG_cDiffusionLightExtension_vf180_12", 0x2ea8de, 32, 0x24853d9f, MemberAddress(&Sub::SetPairD12)},
    {"nTSSG_cDiffusionLightExtension_vf180_14", 0x2ea728, 32, 0xbb0bd49d, MemberAddress(&Sub::SetVec3D14)},
    {"nTSSG_cDiffusionLightExtension_vf180_15", 0x2ea8a2, 18, 0x0e33b583, MemberAddress(&Sub::SetByteD15)},
    {"nTSSG_cDiffusionLightExtension_vf180_21", 0x2ea531, 25, 0x40899c0f, MemberAddress(&Sub::GetPointerP1OrD11)},
    {"nTSSG_cDiffusionLightExtension_vf180_23", 0x2ea4ae, 25, 0xf0556a81, MemberAddress(&Sub::GetFloatF3OrDefault)},
    {"nTSSG_cDiffusionLightExtension_vf180_24", 0x2ea436, 25, 0xfb6fe7fe, MemberAddress(&Sub::GetVec3V4OrD14)},
    {"nTSSG_cDiffusionLightExtension_vf180_25", 0x2ea4ee, 22, 0xe2024425, MemberAddress(&Sub::GetByteB5OrD15)},
};
REGISTER_HOOKS(kHooks);

} // namespace
