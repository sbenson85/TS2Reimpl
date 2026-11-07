#include "hook/hooks.h"
#include "ts/UserTools.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSUserToolBase_vf22", 0x78d745, 16, 0xe7b7ac1d, MemberAddress(&cTSUserToolBase::ForwardSlot37)},
    {"cTSUserToolBase_vf23", 0x78d916, 15, 0xc8a25bd4, MemberAddress(&cTSUserToolBase::ForwardSlot17)},
    {"cTSUserToolBase_vf29", 0x78d789, 22, 0xf2901e85, MemberAddress(&cTSUserToolBase::ForwardSlot23)},
    {"cTSUserToolBase_vf30", 0x78d64d, 17, 0x76ec1884, MemberAddress(&cTSUserToolBase::ForwardSlot24)},
    {"cTSUserToolBase_vf31", 0x78d6c3, 32, 0xedcc7fe5, MemberAddress(&cTSUserToolBase::ForwardSlot25Flag)},
    {"cTSUserToolBase_vf32", 0x78d6f0, 32, 0x9b990390, MemberAddress(&cTSUserToolBase::ForwardSlot26Flag)},
    {"cTSUserToolBase_vf9", 0x78d755, 20, 0xc216cd16, MemberAddress(&cTSUserToolBase::SelfSlot18)},
    {"cTSUserToolBase_vf8", 0x78d769, 32, 0x685ab111, MemberAddress(&cTSUserToolBase::StoreAndSelfSlot17)},
    {"cTSUserToolBuyBuild_vf44", 0x78e747, 7, 0x9de19e64, MemberAddress(&cTSUserToolBuyBuild::ClearByte4C)},
    {"cTSUserToolGroundBased_vf38", 0x78eb3c, 15, 0x048c4a6b, MemberAddress(&cTSUserToolGroundBased::EnsureFlag50)},
    {"cTSUserToolGroundBased_vf44", 0x78ec49, 32, 0x7f94673d,
     MemberAddress(&cTSUserToolGroundBased::ClearByte4CAndNotify)},
    {"cTSUserToolGroundBased_vf49", 0x78eb4b, 23, 0x988fcc31,
     MemberAddress(&cTSUserToolGroundBased::ClearFlagsAfterSlot60)},
    {"cTSUserToolGroundBased_vf59", 0x67d331, 5, 0xf41559c5, MemberAddress(&cTSUserToolGroundBased::SetFlag50)},
    {"cTSUserToolGroundBased_vf60", 0x67d336, 5, 0xed0e6884, MemberAddress(&cTSUserToolGroundBased::ClearFlag50)},
    {"cTSUserToolGroundBased_vf62", 0x78eca1, 10, 0xfa0d1b0a, MemberAddress(&cTSUserToolGroundBased::SetByte61)},
};
REGISTER_HOOKS(kHooks);

} // namespace
