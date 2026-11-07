#include "hook/hooks.h"
#include "ts/cTSMetaParticlesEffect.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSMetaParticleEffect_Create", 0xc03910, 32, 0x9cd2ee6d, reinterpret_cast<void *>(&cTSMetaParticlesEffect::Create)},
    {"cTSMetaParticle_Allocate", 0xbfda30, 32, 0x77657107, reinterpret_cast<void *>(&cTSMetaParticle::Allocate)},
    {"cTSMetaParticle_Free", 0xbfdab0, 22, 0x13f7166d, reinterpret_cast<void *>(&cTSMetaParticle::Free)},
    {"cTSMetaWalk_Apply", 0xbfdb00, 32, 0x76dce0c2, MemberAddress(&cTSMetaWalk::Apply)},
    {"cTSMetaWalk_Init", 0xbfffb0, 32, 0xf8ac9779, MemberAddress(&cTSMetaWalk::Init)},
    {"cTSMetaWalk_Next", 0xc03cb0, 32, 0x2b22baa3, MemberAddress(&cTSMetaWalk::Next)},
    {"fx_MetaDirectionAngles", 0xc016f0, 32, 0x19380d9c, reinterpret_cast<void *>(&fx::MetaDirectionAngles)},
    {"cTSMetaParticlesEffect_AddParticle", 0xbfdc30, 32, 0x12ea43de, MemberAddress<&cTSMetaParticlesEffect::AddParticle>()},
    {"cTSMetaParticlesEffect_CollideColliders", 0xbfe190, 32, 0xc9170179, MemberAddress<&cTSMetaParticlesEffect::CollideColliders>()},
    {"cTSMetaParticlesEffect_NewParticle", 0xbff4a0, 32, 0x4482e566, MemberAddress<&cTSMetaParticlesEffect::NewParticle>()},
    {"cTSMetaParticlesEffect_PruneSpawned", 0xbff510, 32, 0x824b2187, MemberAddress<&cTSMetaParticlesEffect::PruneSpawned>()},
    {"cTSMetaParticlesEffect_RetireParticle", 0xbff610, 32, 0x2e6f99a1, MemberAddress<&cTSMetaParticlesEffect::RetireParticle>()},
    {"cTSMetaParticlesEffect_Emit", 0xbffb30, 32, 0x890440ab, MemberAddress<&cTSMetaParticlesEffect::Emit>()},
    {"cTSMetaParticlesEffect_HandParticleOn", 0xbffbd0, 32, 0x245d44e0, MemberAddress<&cTSMetaParticlesEffect::HandParticleOn>()},
    {"cTSMetaParticlesEffect_Prepare", 0xc00160, 32, 0xb2facb4e, MemberAddress<&cTSMetaParticlesEffect::Prepare>()},
    {"cTSMetaParticlesEffect_SetEvent101", 0xc02680, 31, 0x1eeb0730, MemberAddress<&cTSMetaParticlesEffect::SetEvent101>()},
    {"cTSMetaParticlesEffect_Aim", 0xc026a0, 32, 0x3665eb0f, MemberAddress<&cTSMetaParticlesEffect::Aim>()},
    {"cTSMetaParticlesEffect_ChooseSourceQuadrant", 0xc02a80, 32, 0xdde20ad1, MemberAddress<&cTSMetaParticlesEffect::ChooseSourceQuadrant>()},
    {"cTSMetaParticlesEffect_TickWalks", 0xc047e0, 32, 0x90c639a8, MemberAddress<&cTSMetaParticlesEffect::TickWalks>()},
    {"cTSMetaParticlesEffect_RotateByTransform", 0xbff400, 32, 0x20291104, reinterpret_cast<void *>(&fx::MetaRotateBack)},
    {"cTSMetaParticlesEffect_Steer", 0xbfed50, 32, 0x7412244f, MemberAddress<&cTSMetaParticlesEffect::Steer>()},
    {"cTSMetaParticlesEffect_FollowPath", 0xc02f30, 32, 0xd16a860e, MemberAddress<&cTSMetaParticlesEffect::FollowPath>()},
    {"cTSMetaParticlesEffect_Integrate", 0xc04840, 32, 0x2d74f448, MemberAddress<&cTSMetaParticlesEffect::Integrate>()},
    {"cTSMetaParticlesEffect_SpawnSurfaceEffect", 0xc03970, 32, 0xb7e54aa5, MemberAddress<&cTSMetaParticlesEffect::SpawnSurfaceEffect>()},
    {"cTSMetaParticlesEffect_CollideSurface", 0xbfe480, 32, 0xc1545a90, MemberAddress<&cTSMetaParticlesEffect::CollideSurface>()},
    {"cTSMetaParticlesEffect_SnapToSurface", 0xbff8d0, 32, 0xc991f179, MemberAddress<&cTSMetaParticlesEffect::SnapToSurface>()},
    {"cTSMetaParticlesEffect_IntegrateSubsteps", 0xc04a00, 32, 0x2072a696, MemberAddress<&cTSMetaParticlesEffect::IntegrateSubsteps>()},
    {"cTSMetaParticlesEffect_InitParticle", 0xc006f0, 32, 0xd3c456c5, MemberAddress<&cTSMetaParticlesEffect::InitParticle>()},
    {"fx_MetaRandomInEllipsoid", 0xbdd5e0, 32, 0xe56ccbc1, reinterpret_cast<void *>(&fx::MetaRandomInEllipsoid)},
    {"fx_BasisFromDirection2", 0xbdce90, 32, 0xa7c921c8, reinterpret_cast<void *>(&fx::MetaBasisA)},
    {"fx_BasisFromDirection3", 0xbdcfd0, 32, 0x624f2b86, reinterpret_cast<void *>(&fx::MetaBasisB)},
    {"fx_BasisFromDirection4", 0xbdd110, 32, 0xc98f8ebc, reinterpret_cast<void *>(&fx::MetaBasisC)},
    {"cTSMetaParticlesEffect_RotateZ", 0xc017e0, 32, 0x322e590f, reinterpret_cast<void *>(&fx::MetaRotateZ)},
    {"cTSMetaParticlesEffect_BuildParticlePlace", 0xc018f0, 32, 0xafcb9ad5, MemberAddress<&cTSMetaParticlesEffect::BuildParticlePlace>()},
    {"cTSMetaParticlesEffect_TickParticleEffect", 0xc03ea0, 32, 0xdfe955b8, MemberAddress<&cTSMetaParticlesEffect::TickParticles>()},
    {"cTSMetaParticlesEffect_EmitOnce", 0xc01590, 32, 0xa78f0acd, MemberAddress<&cTSMetaParticlesEffect::EmitOnce>()},
    {"cTSMetaParticlesEffect_EmitBurst", 0xc01310, 32, 0x2dd40065, MemberAddress<&cTSMetaParticlesEffect::EmitBurst>()},
    {"cTSMetaParticlesEffect_EmitContinuous", 0xbfdcb0, 32, 0x8a0e7fe6, MemberAddress<&cTSMetaParticlesEffect::EmitContinuous>()},
};
REGISTER_HOOKS(kHooks);

} // namespace
