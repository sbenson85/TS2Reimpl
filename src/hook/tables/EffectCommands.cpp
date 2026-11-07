#include "hook/hooks.h"
#include "gz/EffectCommands.h"

namespace {

const HookSpec kHooks[] = {
    {"AUcCameraEffectCommand_A0x4da3b4cd_vf3", 0xbf57e0, 32, 0x6411f4ca, MemberAddress(&cCameraEffectCommand::SetContext)},
    {"AUcCameraEffectCommand_A0x4da3b4cd_vf4", 0xbf5020, 32, 0xf38760ae, MemberAddress(&cCameraEffectCommand::Execute)},
    {"AUcShakeEffectCommand_A0x4da3b4cd_vf3", 0xbf5c70, 32, 0xebf2a270, MemberAddress(&cShakeEffectCommand::SetContext)},
    {"AUcShakeEffectCommand_A0x4da3b4cd_vf4", 0xbf54e0, 32, 0xf38760ae, MemberAddress(&cShakeEffectCommand::Execute)},
    {"FUN_a0bf6300", 0xbf6300, 32, 0x1c93b68a, reinterpret_cast<void *>(&RegisterCameraEffectBlocks)},
    {"FUN_a0bf6420", 0xbf6420, 32, 0xd5adf461, reinterpret_cast<void *>(&RegisterCameraGroupCommands)},
    {"FUN_a0bf5f20", 0xbf5f20, 32, 0x0438448a, reinterpret_cast<void *>(&ParseCameraControl)},
    {"AUcCameraControlCommand_A0x4da3b4cd_vf4", 0xbf4fe0, 32, 0x6412c24e, MemberAddress(&cCameraControlCommand::Execute)},
    {"AUcCameraLifeCommand_A0x4da3b4cd_vf4", 0xbf5230, 32, 0x3f681c8a, MemberAddress(&cCameraLifeCommand::Execute)},
    {"AUcCameraHeadingCommand_A0x4da3b4cd_vf4", 0xbf5200, 32, 0xd2de3182, MemberAddress(&cCameraHeadingCommand::Execute)},
    {"AUcCameraPitchCommand_A0x4da3b4cd_vf4", 0xbf52c0, 32, 0x7719a818, MemberAddress(&cCameraPitchCommand::Execute)},
    {"AUcCameraRollCommand_A0x4da3b4cd_vf4", 0xbf52f0, 32, 0x27520153, MemberAddress(&cCameraRollCommand::Execute)},
    {"AUcCameraFOVCommand_A0x4da3b4cd_vf4", 0xbf51a0, 32, 0x60d3fee7, MemberAddress(&cCameraFOVCommand::Execute)},
    {"AUcCameraNearClipCommand_A0x4da3b4cd_vf4", 0xbf5260, 32, 0xa394b01f, MemberAddress(&cCameraNearClipCommand::Execute)},
    {"AUcCameraFarClipCommand_A0x4da3b4cd_vf4", 0xbf51d0, 32, 0x14fcca69, MemberAddress(&cCameraFarClipCommand::Execute)},
    {"AUcCameraOrbitCommand_A0x4da3b4cd_vf4", 0xbf5290, 32, 0xa360a36a, MemberAddress(&cCameraOrbitCommand::Execute)},
    {"AUcShakeLengthCommand_A0x4da3b4cd_vf4", 0xbf5680, 32, 0x0ad6d783, MemberAddress(&cShakeLengthCommand::Execute)},
    {"AUcShakeAmplitudeCommand_A0x4da3b4cd_vf4", 0xbf5460, 27, 0x86f4a5be, MemberAddress(&cShakeAmplitudeCommand::Execute)},
    {"AUcShakeFrequencyCommand_A0x4da3b4cd_vf4", 0xbf5660, 27, 0xcc1c77b3, MemberAddress(&cShakeFrequencyCommand::Execute)},
    {"AUcShakeAspectCommand_A0x4da3b4cd_vf4", 0xbf5480, 32, 0x3f681c8a, MemberAddress(&cShakeAspectCommand::Execute)},
    {"AUcShakeBaseTableCommand_A0x4da3b4cd_vf4", 0xbf54b0, 32, 0x3cb1ea85, MemberAddress(&cShakeBaseTableCommand::Execute)},
    {"AUcGroupShakeCommand_A0x4da3b4cd_vf4", 0xbf5430, 32, 0xd93545fe, MemberAddress(&cGroupShakeCommand::Execute)},
    {"AUcGroupCameraCommand_A0x4da3b4cd_vf4", 0xbf5320, 32, 0xb7daf8e4, MemberAddress(&cGroupCameraCommand::Execute)},
    // Slots 9 and 12 of both blocks (shared_vf_*): every reference is the camera or the shake block's vtable, both ours
    // (tools/xrefs.py; docs/verification.md, one class family). Entries by hand (tools/hookcrc.py checks them).
    {"shared_vf_bf5720", 0xbf5720, 32, 0x68d7216e, MemberAddress(&cCameraEffectBlock::EndBlock)},           // 187 bytes
    {"shared_vf_bf4f20", 0xbf4f20, 32, 0x32b1e3df, MemberAddress(&cCameraEffectBlock::DeletingDestructor)}, // 131 bytes
    {"AUcWaterEffectCommand_A0xb7fcad62_vf3", 0xbf6cf0, 32, 0x71fe0fbc, MemberAddress(&cWaterEffectCommand::SetContext)},
    {"AUcWaterEffectCommand_A0xb7fcad62_vf4", 0xbf6900, 32, 0x59d08568, MemberAddress(&cWaterEffectCommand::Execute)},
    {"AUcWaterEffectCommand_A0xb7fcad62_vf9", 0xbf6c50, 32, 0x89af51c4, MemberAddress(&cWaterEffectCommand::EndBlock)},
    {"AUcWaterEffectCommand_A0xb7fcad62_vf12", 0xbf67a0, 32, 0x77581382, MemberAddress(&cWaterEffectCommand::DeletingDestructor)},
    {"FUN_a0bf6ff0", 0xbf6ff0, 32, 0x6b832807, reinterpret_cast<void *>(&RegisterWaterEffectBlock)},
    {"AUcWaterDropCommand_A0xb7fcad62_vf4", 0xbf6890, 32, 0x52bc2c19, MemberAddress(&cWaterDropCommand::Execute)},
    {"AUcWaterSurfaceCommand_A0xb7fcad62_vf4", 0xbf6ab0, 32, 0xbec910b7, MemberAddress(&cWaterSurfaceCommand::Execute)},
    {"AUcGroupWaterCommand_A0xb7fcad62_vf4", 0xbf6860, 32, 0xc63f375f, MemberAddress(&cGroupWaterCommand::Execute)},
    {"FUN_a0bf70d0", 0xbf70d0, 32, 0xc7bf31e3, reinterpret_cast<void *>(&RegisterWaterGroupCommand)},
    {"AUcScreenEffectCommand_A0x95c6415c_vf3", 0xbf7d40, 32, 0xda83ffc5, MemberAddress(&cScreenEffectCommand::SetContext)},
    {"AUcScreenEffectCommand_A0x95c6415c_vf4", 0xbf7830, 32, 0xdf32ede6, MemberAddress(&cScreenEffectCommand::Execute)},
    {"AUcScreenEffectCommand_A0x95c6415c_vf9", 0xbf7c10, 32, 0xf54efd5a, MemberAddress(&cScreenEffectCommand::EndBlock)},
    {"AUcScreenEffectCommand_A0x95c6415c_vf12", 0xbf7490, 32, 0xae9c233f, MemberAddress(&cScreenEffectCommand::DeletingDestructor)},
    {"FUN_a0bf83e0", 0xbf83e0, 32, 0xf0b2e1c9, reinterpret_cast<void *>(&RegisterScreenEffectBlock)},
    {"AUcScreenModeCommand_A0x95c6415c_vf4", 0xbf7b60, 32, 0x9987b1fe, MemberAddress(&cScreenModeCommand::Execute)},
    {"AUcScreenStrengthCommand_A0x95c6415c_vf4", 0xbf7b90, 27, 0x66aab013, MemberAddress(&cScreenStrengthCommand::Execute)},
    {"AUcScreenLengthCommand_A0x95c6415c_vf4", 0xbf7ac0, 32, 0x0ad6d783, MemberAddress(&cScreenLengthCommand::Execute)},
    {"AUcScreenColorCommand_A0x95c6415c_vf4", 0xbf7580, 32, 0x13107ec3, MemberAddress(&cScreenColorCommand::Execute)},
    {"AUcScreenTextureCommand_A0x95c6415c_vf4", 0xbf7bb0, 32, 0x0bdc44f4, MemberAddress(&cScreenTextureCommand::Execute)},
    {"AUcGroupScreenCommand_A0x95c6415c_vf4", 0xbf7550, 32, 0xec271020, MemberAddress(&cGroupScreenCommand::Execute)},
    {"FUN_a0bf8480", 0xbf8480, 32, 0x5c5d2081, reinterpret_cast<void *>(&RegisterScreenGroupCommand)},
};
REGISTER_HOOKS(kHooks);

} // namespace
