#include "hook/hooks.h"
#include "rz/MiscHelpersA.h"

namespace {

const HookSpec kHooks[] = {
    {"cRZStd_Construct", 0x1bf920, 32, 0x5166de8a, MemberAddress(&cRZStd::Construct)},
    {"cTSAudioAmbienceGlobalPrimitiveBase_Construct", 0x2439f4, 7, 0x72f7b68a, MemberAddress(&cTSAudioAmbienceGlobalPrimitiveBase::Construct)},
    {"TreeEraser_EraseSubtree", 0x1fc790, 32, 0x59d5e22b, MemberAddress(&TreeEraser::EraseSubtree)},
    {"QueryServiceValue", 0x18de70, 32, 0xeafe3e66, reinterpret_cast<void *>(&QueryServiceValue)},
    {"ServiceCallWithHeld", 0xb41750, 32, 0xff039456, reinterpret_cast<void *>(&ServiceCallWithHeld)},
    {"FUN_a02439de", 0x2439de, 22, 0xf19e2b67, MemberAddress(&cTSAudioAmbienceGlobalPrimitiveBase::ConstructWithArg)},
    {"FUN_a018def0", 0x18def0, 32, 0x1131bd35, reinterpret_cast<void *>(&LogD3DError)},
};
REGISTER_HOOKS(kHooks);

} // namespace
