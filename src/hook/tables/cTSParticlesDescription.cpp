#include "hook/hooks.h"
#include "ts/cTSParticlesDescription.h"

// The particle effect descriptions (lane IX): the parts' serializers and special members, the base, the particle (type 1) and
// meta-particle (type 2) descriptions and their components. Not hooked: 0xbde010 (ClearCentre, 4 bytes), 0x4b21f0 (the
// strength setter, folded across classes), the components' folded slots 0, 3, 7 and 8, and the +4 faces' adjustor thunks
// 0xbe2b10 / 0xbed31a (they jump to the hooked deleting destructors; our objects carry our own).
namespace {

using Parts = cTSParticlePartsAdapters;
using Particles = cTSCompDescriptionAdapters<cTSParticlesComponentDescription>;
using Meta = cTSCompDescriptionAdapters<cTSMetaParticlesComponentDescription>;

const HookSpec kHooks[] = {
    // colliders
    {"cTSParticleCollider_Read", 0xbde880, 32, 0x68ab186a, reinterpret_cast<void *>(&cTSParticleCollider::Read)},
    {"cTSParticleCollider_Write", 0xbde940, 32, 0x3ffab9de, reinterpret_cast<void *>(&cTSParticleCollider::Write)},
    {"cTSParticleCollider_Construct", 0xbe1530, 32, 0xbb22379a, reinterpret_cast<void *>(&Parts::ConstructCollider)},
    {"cTSParticleCollider_Destroy", 0xbe1aa0, 32, 0x4a3bbc60, reinterpret_cast<void *>(&Parts::DestroyCollider)},
    {"cTSParticleCollider_Copy", 0xbe1490, 32, 0x199a8770, reinterpret_cast<void *>(&Parts::CopyCollider)},
    // attractors
    {"cTSParticleAttractor_Construct", 0xbddd10, 32, 0x30332e82, reinterpret_cast<void *>(&Parts::ConstructAttractor)},
    {"cTSParticleAttractor_Destroy", 0xbe1920, 32, 0xc222a466, reinterpret_cast<void *>(&Parts::DestroyAttractor)},
    {"cTSParticleAttractor_Copy", 0xbe13d0, 32, 0x899d1b77, reinterpret_cast<void *>(&Parts::CopyAttractor)},
    {"cTSParticleAttractor_SetCentre", 0xbde600, 25, 0x91dc61db, reinterpret_cast<void *>(&Parts::SetCentre)},
    {"cTSParticleAttractor_SetCurve", 0xbde620, 32, 0x1279d612, reinterpret_cast<void *>(&Parts::SetCurve)},
    {"cTSParticleAttractor_Read", 0xbde550, 32, 0xf7842a41, reinterpret_cast<void *>(&Parts::ReadAttractor)},
    {"cTSParticleAttractor_Write", 0xbde750, 32, 0x38ff8bef, reinterpret_cast<void *>(&Parts::WriteAttractor)},
    {"cTSParticleAttractor_ReadOf", 0xbddd80, 14, 0x6857b0eb, reinterpret_cast<void *>(&Parts::ReadAttractorOf)},
    {"cTSParticleAttractor_WriteOf", 0xbdde60, 14, 0xf1f58fb9, reinterpret_cast<void *>(&Parts::WriteAttractorOf)},
    {"cTSParticleAttractor_ReadList", 0xbddd90, 32, 0x34e99b1d, reinterpret_cast<void *>(&Parts::ReadAttractors)},
    {"cTSParticleAttractor_WriteList", 0xbdde70, 32, 0xef383cbf, reinterpret_cast<void *>(&Parts::WriteAttractors)},
    // warps, paths, walks, colour lists
    {"cTSParticleWarp_Read", 0xbfa580, 32, 0xb97c1a5a, reinterpret_cast<void *>(&cTSParticleWarp::Read)},
    {"cTSParticleWarp_Write", 0xbfa660, 32, 0x68fb6743, reinterpret_cast<void *>(&cTSParticleWarp::Write)},
    {"cTSParticlePathPoint_Read", 0xbfa550, 32, 0x7dc43c91, reinterpret_cast<void *>(&cTSParticlePathPoint::Read)},
    {"cTSParticlePathPoint_Write", 0xbfa620, 32, 0xff87ab37, reinterpret_cast<void *>(&cTSParticlePathPoint::Write)},
    {"cTSParticlePath_Read", 0xbfb430, 32, 0x7d5e419b, reinterpret_cast<void *>(&Parts::ReadPath)},
    {"cTSParticlePath_Write", 0xbfb510, 32, 0xeb4ff598, reinterpret_cast<void *>(&Parts::WritePath)},
    {"cTSParticlePath_ReadOf", 0xbfa5b0, 14, 0x0598f775, reinterpret_cast<void *>(&Parts::ReadPathOf)},
    {"cTSParticlePath_WriteOf", 0xbfa6a0, 14, 0x657860f7, reinterpret_cast<void *>(&Parts::WritePathOf)},
    {"cTSParticleWalk_Read", 0xbfa500, 32, 0x17d32a82, reinterpret_cast<void *>(&cTSParticleWalk::Read)},
    {"cTSParticleWalk_Write", 0xbfa5c0, 32, 0xf4950226, reinterpret_cast<void *>(&cTSParticleWalk::Write)},
    {"ReadParticleTriples", 0xbe02f0, 32, 0x7618b083, reinterpret_cast<void *>(&ReadParticleTriples)},
    {"ReadParticleText", 0xbde7b0, 32, 0x8ae2a3aa, reinterpret_cast<void *>(&ReadParticleText)},
    // the base
    {"cTSBaseParticlesDescription_Construct", 0xbfb5a0, 32, 0xa9589450, reinterpret_cast<void *>(&Parts::ConstructBase)},
    {"cTSBaseParticlesDescription_Destroy", 0xbfb990, 32, 0xfc27f67c, reinterpret_cast<void *>(&Parts::DestroyBase)},
    {"cTSBaseParticlesDescription_Assign", 0xbe1df0, 32, 0x5096ecb4, reinterpret_cast<void *>(&Parts::AssignBase)},
    {"cTSBaseParticlesDescription_DeletingDestructor", 0xbfbb30, 32, 0xeaa054aa, reinterpret_cast<void *>(&Parts::DeleteBase)},
    {"cTSParticlesDescription_DeletingDestructor", 0xbe2bb0, 32, 0xbbc2e786, reinterpret_cast<void *>(&Parts::DeleteParticles)},
    {"cTSMetaParticlesDescription_DeletingDestructor", 0xbed3f0, 32, 0x76b56f13, reinterpret_cast<void *>(&Parts::DeleteMeta)},
    // particles (type 1)
    {"cTSParticlesDescription_Construct", 0xbe15d0, 32, 0xa9bf71b9, reinterpret_cast<void *>(&Parts::ConstructParticles)},
    {"cTSParticlesDescription_Destroy", 0xbe1be0, 32, 0x6b274da2, reinterpret_cast<void *>(&Parts::DestroyParticles)},
    {"cTSParticlesDescription_Move", 0xbe2320, 32, 0xdbf04b27, reinterpret_cast<void *>(&Parts::MoveParticles)},
    {"cTSParticlesDescription_Copy", 0xbe2700, 32, 0x8c814aff, reinterpret_cast<void *>(&Parts::CopyParticles)},
    {"cTSParticlesDescription_Read", 0xbe72e0, 32, 0x8861d293, reinterpret_cast<void *>(&cTSParticlesDescription::Read)},
    {"cTSParticlesDescription_Write", 0xbe7db0, 32, 0xbab2f297, reinterpret_cast<void *>(&cTSParticlesDescription::Write)},
    {"cTSParticlesComponent_Create", 0xbe7ba0, 32, 0xd09c17c4, reinterpret_cast<void *>(&cTSParticlesComponentDescription::Create)},
    {"cTSParticlesComponent_CreateEffect", 0xbe58b0, 13, 0x8d03cddd, reinterpret_cast<void *>(&Particles::CreateEffect)},
    {"cTSParticlesComponent_CopyTo", 0xbe2be0, 32, 0xea4ced21, reinterpret_cast<void *>(&Particles::CopyTo)},
    {"cTSParticlesComponent_Read", 0xbe72a0, 32, 0x46fb4926, reinterpret_cast<void *>(&Particles::Read)},
    {"cTSParticlesComponent_Write", 0xbe7d80, 32, 0x97b3c5fb, reinterpret_cast<void *>(&Particles::Write)},
    {"cTSParticlesComponent_GetSize", 0xbe58c0, 6, 0x0e4907eb, reinterpret_cast<void *>(&Particles::GetSize)},
    {"cTSParticlesComponent_DeletingDestructor", 0xbe2b20, 32, 0x6b461b41, reinterpret_cast<void *>(&Particles::DeletingDestructor)},
    // meta particles (type 2)
    {"cTSMetaParticlesDescription_Construct", 0xbecf80, 32, 0x822f66a9, reinterpret_cast<void *>(&Parts::ConstructMeta)},
    {"cTSMetaParticlesDescription_Destroy", 0xbed140, 32, 0xfeb4bbe9, reinterpret_cast<void *>(&Parts::DestroyMeta)},
    {"cTSMetaParticlesDescription_Copy", 0xbed200, 32, 0x8b6a704b, reinterpret_cast<void *>(&Parts::CopyMeta)},
    {"cTSMetaParticlesDescription_Read", 0xbf1100, 32, 0xc36eb6f3, reinterpret_cast<void *>(&cTSMetaParticlesDescription::Read)},
    {"cTSMetaParticlesDescription_Write", 0xbf1c80, 32, 0xbab2f297, reinterpret_cast<void *>(&cTSMetaParticlesDescription::Write)},
    {"cTSMetaParticlesComponent_Create", 0xbf1a70, 32, 0x7bcf7453, reinterpret_cast<void *>(&cTSMetaParticlesComponentDescription::Create)},
    {"cTSMetaParticlesComponent_CreateEffect", 0xbefb70, 13, 0xfd1e429c, reinterpret_cast<void *>(&Meta::CreateEffect)},
    {"cTSMetaParticlesComponent_CopyTo", 0xbed420, 32, 0xea4ced21, reinterpret_cast<void *>(&Meta::CopyTo)},
    {"cTSMetaParticlesComponent_Read", 0xbf10c0, 32, 0x3f27f182, reinterpret_cast<void *>(&Meta::Read)},
    {"cTSMetaParticlesComponent_Write", 0xbf1c50, 32, 0xe1b31e73, reinterpret_cast<void *>(&Meta::Write)},
    {"cTSMetaParticlesComponent_GetSize", 0xbefb80, 6, 0x85cedf48, reinterpret_cast<void *>(&Meta::GetSize)},
    {"cTSMetaParticlesComponent_DeletingDestructor", 0xbed330, 32, 0x8eff0d47, reinterpret_cast<void *>(&Meta::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
