#include "hook/hooks.h"
#include "gz/GimexFshEncode.h"

namespace {

const HookSpec kHooks[] = {
    {"FUN_a0896e1e", 0x896e1e, 32, 0xd35c84d8, gimex::fsh::EncodeColourBlock},
    {"FUN_a08976f6", 0x8976f6, 32, 0x42803363, gimex::fsh::EncodeColourBlocks},
    {"FUN_a089779a", 0x89779a, 32, 0xdf56be0f, gimex::fsh::NearestOfFour},
    {"FUN_a08978a7", 0x8978a7, 32, 0x6c8e3aac, gimex::fsh::NearestOfThree},
    {"FUN_a0897987", 0x897987, 32, 0x73b7634a, gimex::fsh::ColourError},
    {"FUN_a0897a29", 0x897a29, 32, 0xafdd141a, gimex::fsh::EncodeColourRecord},
    {"FUN_a0897b56", 0x897b56, 32, 0x3c60a64f, gimex::fsh::FitAlpha},
    {"FUN_a0897e3b", 0x897e3b, 32, 0xacfaa7ea, gimex::fsh::FitColours},
    {"FUN_a08990a2", 0x8990a2, 32, 0x8a30a2c6, gimex::fsh::FitSingleColour},
    {"FUN_a08988f3", 0x8988f3, 32, 0x1d545240, gimex::fsh::EncodeBlocks},
};
REGISTER_HOOKS(kHooks);

} // namespace
