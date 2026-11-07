#include "hook/hooks.h"
#include "ts/cTSParticlesGeometry.h"

namespace {

using A = cTSParticlesGeometryAdapters;

// The constructor (the one site then builds ours), the destructor, and the two leaves of the vertex code: the curve reading
// (22 sites) and the rotation. Everything else runs through our vtable (db/status/cTSParticlesGeometry.jsonl).
const HookSpec kHooks[] = {
    {"cTSParticlesGeometry_Construct", 0xbb4e90, 32, 0xa17e7f0d, reinterpret_cast<void *>(&A::Construct)},
    {"cTSParticlesGeometry_Destruct", 0xbb5490, 32, 0x2bc289f9, reinterpret_cast<void *>(&A::Destruct)},
    {"cTSEffectCurve_Evaluate", 0xbb5620, 32, 0xbeaf25ec, reinterpret_cast<void *>(&A::EvaluateCurve)},
    {"fx_EffectsTransform_Rotate", 0xbc0f80, 32, 0x60572179, reinterpret_cast<void *>(&A::Rotate)},
};
REGISTER_HOOKS(kHooks);

} // namespace
