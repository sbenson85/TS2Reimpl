#include "hook/hooks.h"
#include "gz/ParticleCommands.h"

namespace {

const HookSpec kHooks[] = {
    {"cParticleEffectCommand_vf3", 0xbe5b00, 32, 0xfa4d899d, MemberAddress(&cParticleEffectCommand::SetContext)},
    {"cParticleEffectCommand_vf4", 0xbe2f30, 32, 0x2e19fefa, MemberAddress(&cParticleEffectCommand::Execute)},
    {"cParticleEffectCommand_vf9", 0xbe59b0, 32, 0x7bc6708a, MemberAddress(&cParticleEffectCommand::EndBlock)},
    {"cParticleEffectCommand_vf12", 0xbe2b50, 32, 0x8a6d8fbf, MemberAddress(&cParticleEffectCommand::DeletingDestructor)},
    {"cParticlePathCommand_vf3", 0xbe6a30, 32, 0x5cf88453, MemberAddress(&cParticlePathCommand::SetContext)},
    {"cParticlePathCommand_vf4", 0xbe3be0, 32, 0x5d09b18b, MemberAddress(&cParticlePathCommand::Execute)},
    {"cParticlePathCommand_vf9", 0xbe5a90, 32, 0x255e9dfd, MemberAddress(&cParticlePathCommand::EndBlock)},
    {"cParticleAspectCommand_vf4", 0xbe2c90, 32, 0x9e8b9b9d, MemberAddress(&cParticleAspectCommand::Execute)},
    {"cParticleEmitCommand_vf4", 0xbe30c0, 32, 0x153fa768, MemberAddress(&cParticleEmitCommand::Execute)},
    {"cParticleAttractorCommand_vf4", 0xbe2cc0, 32, 0x92ceae2f, MemberAddress(&cParticleAttractorCommand::Execute)},
    {"cParticleRandomWalkCommand_vf4", 0xbe3dd0, 32, 0xb3b2ee0d, MemberAddress(&cParticleRandomWalkCommand::Execute)},
    {"cParticleStretchCommand_vf4", 0xbe4b40, 32, 0x3f681c8a, MemberAddress(&cParticleStretchCommand::Execute)},
    {"cParticleRateCommand_vf4", 0xbe3fc0, 32, 0xcd8ad7af, MemberAddress(&cParticleRateCommand::Execute)},
    {"cParticleInjectCommand_vf4", 0xbe3a60, 32, 0xcd8ad7af, MemberAddress(&cParticleInjectCommand::Execute)},
    {"cParticleMaintainCommand_vf4", 0xbe3ad0, 32, 0xcd8ad7af, MemberAddress(&cParticleMaintainCommand::Execute)},
    {"cParticleMaterialCommand_vf4", 0xbe3b20, 32, 0xdfa4837d, MemberAddress(&cParticleMaterialCommand::Execute)},
    {"cParticleShapeCommand_vf4", 0xbe4300, 32, 0xdfa4837d, MemberAddress(&cParticleShapeCommand::Execute)},
    {"cParticleFramesCommand_vf4", 0xbe39c0, 32, 0x4ff242d9, MemberAddress(&cParticleFramesCommand::Execute)},
    {"cParticleAlignmentCommand_vf4", 0xbe2c40, 32, 0xa6b17e66, MemberAddress(&cParticleAlignmentCommand::Execute)},
    {"cParticleCollideCommand_vf4", 0xbe2da0, 32, 0x02cbfddc, MemberAddress(&cParticleCollideCommand::Execute)},
    {"cParticleTerrainRepelCommand_vf4", 0xbe4d30, 32, 0x148356b6, MemberAddress(&cParticleTerrainRepelCommand::Execute)},
    {"cParticlePlaneCollisionCommand_vf4", 0xbe3ca0, 32, 0xbf8bb52d, MemberAddress(&cParticlePlaneCollisionCommand::Execute)},
    {"cParticleDeathEffectCommand_vf4", 0xbe2ef0, 32, 0x0bdc44f4, MemberAddress(&cParticleDeathEffectCommand::Execute)},
    {"cParticleRotateCommand_vf4", 0xbe4000, 32, 0x7ba47cb8, MemberAddress(&cParticleRotateCommand::Execute)},
    {"cParticleSourceCommand_vf4", 0xbe43e0, 32, 0x0bcf6281, MemberAddress(&cParticleSourceCommand::Execute)},
    {"cParticleForceCommand_vf4", 0xbe31a0, 32, 0xb5eb24b9, MemberAddress(&cParticleForceCommand::Execute)},
    {"cParticleWarpCommand_vf4", 0xbe5050, 32, 0x3f22c724, MemberAddress(&cParticleWarpCommand::Execute)},
    {"cParticleTextureCommand_vf4", 0xbe4d70, 32, 0xf79dbcc3, MemberAddress(&cParticleTextureCommand::Execute)},
    {"cParticleTerrainCollideCommand_vf4", 0xbe4b80, 32, 0xa0e37f6a, MemberAddress(&cParticleTerrainCollideCommand::Execute)},
    {"cMetaParticleEffectCommand_vf3", 0xbefd10, 32, 0xb31cd241, MemberAddress(&cMetaParticleEffectCommand::SetContext)},
    {"cMetaParticleEffectCommand_vf4", 0xbed9e0, 32, 0x42670587, MemberAddress(&cMetaParticleEffectCommand::Execute)},
    {"cMetaParticleEffectCommand_vf9", 0xbefbc0, 32, 0x5b12cbbe, MemberAddress(&cMetaParticleEffectCommand::EndBlock)},
    {"cMetaParticleEffectCommand_vf12", 0xbed360, 32, 0xe86db315, MemberAddress(&cMetaParticleEffectCommand::DeletingDestructor)},
    {"cMetaParticlePathCommand_vf3", 0xbf0900, 32, 0x5cf88453, MemberAddress(&cMetaParticlePathCommand::SetContext)},
    {"cMetaParticlePathCommand_vf4", 0xbee8a0, 32, 0x0d8f68b0, MemberAddress(&cMetaParticlePathCommand::Execute)},
    {"cMetaParticlePathCommand_vf9", 0xbefca0, 32, 0x52633ec3, MemberAddress(&cMetaParticlePathCommand::EndBlock)},
    {"cMetaParticleAspectCommand_vf4", 0xbed5c0, 21, 0xd1bdf0d1, MemberAddress(&cMetaParticleAspectCommand::Execute)},
    {"cMetaParticleRotateCommand_vf4", 0xbeec20, 32, 0xb214eaa4, MemberAddress(&cMetaParticleRotateCommand::Execute)},
    {"cMetaParticleSourceCommand_vf4", 0xbeecc0, 32, 0xc9e0595a, MemberAddress(&cMetaParticleSourceCommand::Execute)},
    {"cMetaParticleEmitCommand_vf4", 0xbedd30, 32, 0x431d25a5, MemberAddress(&cMetaParticleEmitCommand::Execute)},
    {"cMetaParticleForceCommand_vf4", 0xbedd80, 32, 0xd5ddd9c6, MemberAddress(&cMetaParticleForceCommand::Execute)},
    {"cMetaParticleAttractorCommand_vf4", 0xbed5e0, 32, 0x92ceae2f, MemberAddress(&cMetaParticleAttractorCommand::Execute)},
    {"cMetaParticleWarpCommand_vf4", 0xbef7d0, 32, 0x3f22c724, MemberAddress(&cMetaParticleWarpCommand::Execute)},
    {"cMetaParticleRandomWalkCommand_vf4", 0xbee8e0, 32, 0x56112671, MemberAddress(&cMetaParticleRandomWalkCommand::Execute)},
    {"cMetaParticleRateCommand_vf4", 0xbeebe0, 32, 0xcd8ad7af, MemberAddress(&cMetaParticleRateCommand::Execute)},
    {"cMetaParticleInjectCommand_vf4", 0xbee7a0, 32, 0x1885fb8a, MemberAddress(&cMetaParticleInjectCommand::Execute)},
    {"cMetaParticleMaintainCommand_vf4", 0xbee800, 32, 0xf492c528, MemberAddress(&cMetaParticleMaintainCommand::Execute)},
    {"cMetaParticleAlignmentCommand_vf4", 0xbed480, 32, 0x8050498a, MemberAddress(&cMetaParticleAlignmentCommand::Execute)},
    {"cMetaParticleCollisionCommand_vf4", 0xbed6c0, 32, 0xdda0995f, MemberAddress(&cMetaParticleCollisionCommand::Execute)},
    {"cMetaParticleTerrainCollideCommand_vf4", 0xbef5f0, 32, 0xa0e37f6a, MemberAddress(&cMetaParticleTerrainCollideCommand::Execute)},
    {"cMetaParticleTerrainRepelCommand_vf4", 0xbef7a0, 32, 0x2fed7090, MemberAddress(&cMetaParticleTerrainRepelCommand::Execute)},
    {"cMetaParticleEffectBaseCommand_vf4", 0xbed9a0, 32, 0x0bdc44f4, MemberAddress(&cMetaParticleEffectBaseCommand::Execute)},
    {"cGroupMetaParticlesCommand_vf4", 0xbed450, 32, 0x2631281d, MemberAddress(&cGroupMetaParticlesCommand::Execute)},
    {"cDecalEffectCommand_vf3", 0xbec320, 32, 0xd29e02fe, MemberAddress(&cDecalEffectCommand::SetContext)},
    {"cDecalEffectCommand_vf4", 0xbebdb0, 32, 0x72293ecb, MemberAddress(&cDecalEffectCommand::Execute)},
    {"cDecalEffectCommand_vf9", 0xbec260, 32, 0x95fb7fca, MemberAddress(&cDecalEffectCommand::EndBlock)},
    {"cDecalEffectCommand_vf12", 0xbeb8d0, 32, 0x55b38f98, MemberAddress(&cDecalEffectCommand::DeletingDestructor)},
    {"cDecalColorCommand_vf4", 0xbebb60, 32, 0xe68f41f1, MemberAddress(&cDecalColorCommand::Execute)},
    {"cDecalAlphaCommand_vf4", 0xbeb990, 32, 0xc7f24038, MemberAddress(&cDecalAlphaCommand::Execute)},
    {"cDecalSizeCommand_vf4", 0xbec050, 32, 0x1fd26f78, MemberAddress(&cDecalSizeCommand::Execute)},
    {"cDecalAspectCommand_vf4", 0xbebb40, 27, 0x185a94d2, MemberAddress(&cDecalAspectCommand::Execute)},
    {"cDecalRotateCommand_vf4", 0xbebff0, 32, 0xe9b56500, MemberAddress(&cDecalRotateCommand::Execute)},
    {"cDecalLifeCommand_vf4", 0xbebf30, 32, 0x35e0fd4e, MemberAddress(&cDecalLifeCommand::Execute)},
    {"cDecalTextureCommand_vf4", 0xbec0d0, 32, 0x3c3a7525, MemberAddress(&cDecalTextureCommand::Execute)},
    // Bodies the particles block shares with the meta-particle block (shared_vf_*): every reference is one of the two
    // families' vtables, both ours (tools/xrefs.py; docs/verification.md, one class family). Entries by hand
    // (tools/hookcrc.py checks them).
    {"shared_vf_be2b80", 0xbe2b80, 32, 0xd3adf2c9, MemberAddress(&cParticlePathCommand::DeletingDestructor)}, // 45 bytes
    {"shared_vf_be57b0", 0xbe57b0, 32, 0x012468c9, MemberAddress(&cPathPointCommand::Execute)},             // 61 bytes
    {"shared_vf_be5730", 0xbe5730, 32, 0x012468c9, MemberAddress(&cPathDeltaPointCommand::Execute)},        // 61 bytes
    {"shared_vf_be5870", 0xbe5870, 32, 0x012468c9, MemberAddress(&cPathVelocityCommand::Execute)},          // 61 bytes
    {"shared_vf_be57f0", 0xbe57f0, 32, 0xccbf620d, MemberAddress(&cPathSpeedCommand::Execute)},             // 50 bytes
    {"shared_vf_be5830", 0xbe5830, 32, 0xccbf620d, MemberAddress(&cPathTimeCommand::Execute)},              // 50 bytes
    {"shared_vf_be5770", 0xbe5770, 32, 0xccbf620d, MemberAddress(&cPathDeltaTimeCommand::Execute)},         // 50 bytes
    {"shared_vf_be2ed0", 0xbe2ed0, 21, 0x6286f6d3, MemberAddress(&cParticleColorCommand::Execute)},         // 21 bytes
    {"shared_vf_be2c70", 0xbe2c70, 21, 0xc467cbdd, MemberAddress(&cParticleAlphaCommand::Execute)},         // 21 bytes
    {"shared_vf_be43c0", 0xbe43c0, 21, 0x10ca7304, MemberAddress(&cParticleSizeCommand::Execute)},          // 21 bytes
    {"shared_vf_be3ab0", 0xbe3ab0, 21, 0xaea03b1e, MemberAddress(&cParticleLifeCommand::Execute)},          // 21 bytes
};
REGISTER_HOOKS(kHooks);

} // namespace
