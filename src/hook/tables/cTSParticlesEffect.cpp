#include "hook/hooks.h"
#include "ts/ParticleMath.h"
#include "ts/cTSParticlesDescription.h"
#include "ts/cTSParticlesEffect.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSParticlesEffect_Create", 0xbc0c50, 32, 0x04317174, &cTSParticlesEffect::Create},
    {"cTSParticlesEffect_Prepare", 0xbbab40, 32, 0x14b91ed8, MemberAddress<&cTSParticlesEffect::Prepare>()},
    {"cTSParticlesEffect_Aim", 0xbbd410, 32, 0x7f79e6ef, MemberAddress<&cTSParticlesEffect::Aim>()},
    {"cTSParticlesEffect_ClearParticles", 0xbbce70, 32, 0xe9c3360d, MemberAddress<&cTSParticlesEffect::ClearParticles>()},
    {"cTSParticlesEffect_RetireParticle", 0xbb94d0, 32, 0x5d1e78d2, MemberAddress<&cTSParticlesEffect::RetireParticle>()},
    {"cTSParticlesEffect_HandParticleOn", 0xbba740, 32, 0x5276bdc0, MemberAddress<&cTSParticlesEffect::HandParticleOn>()},
    {"cTSParticlesEffect_AddParticle", 0xbb5b30, 32, 0x12ea43de, MemberAddress<&cTSParticlesEffect::AddParticle>()},
    {"cTSParticlesEffect_ResetLife", 0xbc1120, 32, 0xc88af7fb, MemberAddress<&cTSParticlesEffect::ResetLife>()},
    {"cTSParticlesEffect_ResetLives", 0xbc1160, 32, 0xed1219be, MemberAddress<&cTSParticlesEffect::ResetLives>()},
    {"cTSParticlesEffect_StepWind", 0xbc1b10, 32, 0x0f0cfda8, MemberAddress<&cTSParticlesEffect::StepWind>()},
    {"cTSParticleBox_Center", 0xbb9350, 32, 0xfe4d8052, MemberAddress(&cTSParticleBox::Center)},
    {"cTSParticleBox_Extend", 0xbb5bd0, 32, 0xe932a343, MemberAddress(&cTSParticleBox::Extend)},
    {"cTSParticleAttractorState_Assign", 0xbddf10, 32, 0x1e5045c4, reinterpret_cast<void *>(&cTSParticleAttractorState::Assign)},
    {"cTSParticleAttractorState_Set", 0xbde5a0, 32, 0x7d190fe1, MemberAddress(&cTSParticleAttractorState::Set)},
    {"cTSParticleAttractorState_SetCentre", 0xbde5e0, 21, 0x3f6a95fe, MemberAddress(&cTSParticleAttractorState::SetCentre)},
    {"cTSParticleAttractorState_Follow", 0xbde650, 32, 0xd6286888, MemberAddress(&cTSParticleAttractorState::Follow)},
    {"cTSParticlesEffect_AttractorForce", 0xbb9840, 32, 0xb4d568d7, MemberAddress(&cTSParticleAttractorState::Force)},
    {"cTSParticlesEffect_SumAttractors", 0xbb9b90, 32, 0x160789f9, reinterpret_cast<void *>(&cTSParticleAttractorState::Sum)},
    {"fx_SweepColliders", 0xbdf6a0, 32, 0x85b0595e, reinterpret_cast<void *>(&fx::SweepColliders)},
    {"fx_ParticlePosition", 0xbb9ea0, 32, 0xb8446642, reinterpret_cast<void *>(&fx::ParticlePosition)},
    {"FUN_a0be01c0", 0xbe01c0, 32, 0xb94b38ef, reinterpret_cast<void *>(&fx::SpawnEffect)},
    {"cTSParticlesEffect_CollideColliders", 0xbb6730, 32, 0x40216a6b, MemberAddress<&cTSParticlesEffect::CollideColliders>()},
    {"cTSParticlesEffect_CollideSurface", 0xbb85b0, 32, 0x39842d15, MemberAddress<&cTSParticlesEffect::CollideSurface>()},
    {"cTSParticlesEffect_ApplyForces", 0xbb82f0, 32, 0xbd8a02d0, MemberAddress<&cTSParticlesEffect::ApplyForces>()},
    {"cTSParticlesEffect_RandomInBox", 0xbbcd80, 32, 0x07408654, reinterpret_cast<void *>(&fx::RandomInBox)},
    {"fx_Normalize", 0xbc2f80, 32, 0x0ffdac3e, reinterpret_cast<void *>(&fx::Normalize)},
    {"cTSParticlesEffect_RandomInEllipsoid", 0xbdd2f0, 32, 0xb44923e0, reinterpret_cast<void *>(&fx::RandomInEllipsoid)},
    {"cTSParticlesEffect_RandomOnRing", 0xbdd430, 32, 0xef850cbd, reinterpret_cast<void *>(&fx::RandomOnRing)},
    {"cTSParticlePath_Evaluate", 0xbba1b0, 32, 0xf8e6d6d2, MemberAddress(&cTSParticlePath::Evaluate)},
    {"cTSParticlesEffect_SteerAlongPath", 0xbb8c00, 32, 0x4e935058, MemberAddress<&cTSParticlesEffect::SteerAlongPath>()},
    {"cTSParticlesEffect_InitParticle", 0xbbb390, 32, 0xae381747, MemberAddress<&cTSParticlesEffect::InitParticle>()},
    {"cTSParticlesEffect_IntegrateParticle", 0xbc1190, 32, 0x71da8fe8, MemberAddress<&cTSParticlesEffect::IntegrateParticle>()},
    {"cTSParticlesEffect_IntegrateSimple", 0xbc12a0, 32, 0xf5e69ea9, MemberAddress<&cTSParticlesEffect::IntegrateSimple>()},
    {"cTSParticlesEffect_IntegrateSubsteps", 0xbc13c0, 32, 0xe09fd465, MemberAddress<&cTSParticlesEffect::IntegrateSubsteps>()},
    {"cTSParticlesEffect_IntegrateSteeredSubsteps", 0xbc19f0, 32, 0xe93ec9e0,
     MemberAddress<&cTSParticlesEffect::IntegrateSteeredSubsteps>()},
    {"cTSParticlesEffect_IntegrateSteered", 0xbc1530, 32, 0x84692a54, MemberAddress<&cTSParticlesEffect::IntegrateSteered>()},
    {"cTSParticlesEffect_IntegrateOnPath", 0xbbcb70, 32, 0x65d698e4, MemberAddress<&cTSParticlesEffect::IntegrateOnPath>()},
    {"cTSParticlesEffect_IntegrateOnPathPlaced", 0xbc08b0, 32, 0x65d698e4,
     MemberAddress<&cTSParticlesEffect::IntegrateOnPathPlaced>()},
    {"cTSParticleBox_Transform", 0xbc0cb0, 32, 0xc13d1033, MemberAddress<&cTSParticleBox::Transform>()},
    {"cTSParticlesEffect_Emit", 0xbb97a0, 32, 0x1f79f882, MemberAddress<&cTSParticlesEffect::Emit>()},
    {"cTSParticlesEffect_EmitContinuous", 0xbb5c70, 32, 0x3809451c, MemberAddress<&cTSParticlesEffect::EmitContinuous>()},
    {"cTSParticlesEffect_EmitBursts", 0xbbbfa0, 32, 0xa64aa255, MemberAddress<&cTSParticlesEffect::EmitBursts>()},
    {"cTSParticlesEffect_EmitOnce", 0xbbc560, 32, 0xdf70868f, MemberAddress<&cTSParticlesEffect::EmitOnce>()},
};
REGISTER_HOOKS(kHooks);

} // namespace
