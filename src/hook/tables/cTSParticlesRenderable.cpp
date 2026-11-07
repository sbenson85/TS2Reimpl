#include "hook/hooks.h"
#include "ts/cTSParticlesRenderable.h"

namespace {

using A = nGZSceneGraph::cTSParticlesRenderableAdapters;

// The constructor and Setup (the effect's one site still builds the node), the destructor, and the material builder. The
// slots are ours through the vtables the constructor sets (db/status/cTSParticlesRenderable.jsonl).
const HookSpec kHooks[] = {
    {"cTSParticlesRenderable_Construct", 0xbb5010, 32, 0x9fc04d3d, reinterpret_cast<void *>(&A::Construct)},
    {"cTSParticlesRenderable_Destruct", 0xbb54f0, 32, 0x5394841a, reinterpret_cast<void *>(&A::Destruct)},
    {"cTSParticlesRenderable_Setup", 0xbbad30, 32, 0x8d615d2f, reinterpret_cast<void *>(&A::Setup)},
    {"cTSParticlesRenderable_BuildMaterial", 0xbc03d0, 32, 0xa50f2a80, reinterpret_cast<void *>(&A::BuildMaterial)},
    {"cTSParticlesRenderable_AddParticleChild", 0xbb6670, 32, 0xb7880e94, MemberAddress<&nGZSceneGraph::cTSParticlesRenderable::AddParticleChild>()},
    {"cTSParticlesRenderable_RemoveParticleChild", 0xbbcef0, 32, 0xc5008a4f, MemberAddress<&nGZSceneGraph::cTSParticlesRenderable::RemoveParticleChild>()},
    {"cTSParticlesRenderable_UpdateParticleChildren", 0xbc1d10, 32, 0x30058156, MemberAddress<&nGZSceneGraph::cTSParticlesRenderable::UpdateParticleChildren>()},
};
REGISTER_HOOKS(kHooks);

} // namespace
