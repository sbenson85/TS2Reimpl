#include "hook/hooks.h"
#include "sg/cSceneDBAnimSystem.h"

namespace {

using Sys = nGZSceneDBAnim::cSceneDBAnimSystem;

const HookSpec kHooks[] = {
    {"nGZSceneDBAnim_cSceneDBAnimSystem_QueryInterface", 0x9db320, 32, 0xed8b35cb, MemberAddress(&Sys::QueryInterface)},
    {"nGZSceneDBAnim_cSceneDBAnimSystem_vf3", 0x9dac40, 13, 0x553c226d, MemberAddress(&Sys::ListAElementAt)},
    {"nGZSceneDBAnim_cSceneDBAnimSystem_vf7", 0x9dabe0, 13, 0x5f26b0aa, MemberAddress(&Sys::ListBElementAt)},
    {"nGZSceneDBAnim_cSceneDBAnimSystem_vf9", 0x9dabf0, 32, 0x9b5c8576, MemberAddress(&Sys::FindVirtualByValue)},
    {"nGZSceneDBAnim_cSceneDBAnimSystem_vf20", 0x9dba10, 32, 0x75df88e9, MemberAddress(&Sys::SetCounter64AndDrain)},
    {"nGZSceneDBAnim_cSceneDBAnimSystem_vf21", 0x9dada0, 32, 0x284bcafb, MemberAddress(&Sys::DrainWhileCounter68)},
    {"nGZSceneDBAnim_cSceneDBAnimSystem_vf22", 0x9dadd0, 32, 0xfdadec40, MemberAddress(&Sys::DrainWhileCounter54)},
    {"nGZSceneDBAnim_cSceneDBAnimSystem_vf26", 0x9db870, 21, 0x6b399b6f, MemberAddress(&Sys::ResetWords)},
    {"nGZSceneDBAnim_cSceneDBAnimSystem_vf29", 0x9daef0, 32, 0x33ca8d90, MemberAddress(&Sys::FindInListA)},
    {"nGZSceneDBAnim_cSceneDBAnimSystem_vf31", 0x9daec0, 32, 0x59957224, MemberAddress(&Sys::FindInListB)},
    {"nGZSceneDBAnim_cSceneDBAnimSystem_vf32", 0x9db270, 10, 0xf9b9f290, MemberAddress(&Sys::ListBCount)},
    {"nGZSceneDBAnim_cSceneDBAnimSystem_vf44", 0x9db390, 8, 0xfee46ecc, MemberAddress(&Sys::RandomUint32)},
    {"nGZSceneDBAnim_cSceneDBAnimSystem_vf45", 0x9db370, 17, 0x91420ef8, MemberAddress(&Sys::RandomFloat)},
};
REGISTER_HOOKS(kHooks);

} // namespace
