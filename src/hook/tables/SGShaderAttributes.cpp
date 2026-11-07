// Hooks for the shader's attribute and apply parts (src/sg/SGShaderAttributes.h): the factories, which make ours, and the wave the animated parts share. The
// image's constructors, destructors and slot bodies are in effect through them (only the factories construct the
// classes, so only our objects, with our vtables, exist).
#include "hook/hooks.h"
#include "sg/SGShaderAttributes.h"

using namespace nGZSceneGraph;

namespace {

const HookSpec kHooks[] = {
    {"nGZSceneGraph_AnimationWave", 0xac6820, 32, 0x79d6f317, reinterpret_cast<void *>(&AnimationWave)},
    {"nGZSceneGraph_cAnimatedTiledTexture_Create", 0xac3100, 32, 0xa05a261e, reinterpret_cast<void *>(&cAnimatedTiledTexture::Create)},
    {"nGZSceneGraph_cAnimColorScalarAlpha_Create", 0xac30c0, 32, 0x02cd8563, reinterpret_cast<void *>(&cAnimColorScalarAlpha::Create)},
    {"nGZSceneGraph_cAttributeFFMaterial_Create", 0xac3260, 32, 0xf0b43356, reinterpret_cast<void *>(&cAttributeFFMaterial::Create)},
    {"nGZSceneGraph_cAttributeFFScaleModelToViewTransform_Create", 0xac3280, 32, 0x233a0e3d, reinterpret_cast<void *>(&cAttributeFFScaleModelToViewTransform::Create)},
    {"nGZSceneGraph_cFFApplyTransforms_Create", 0xac32d0, 32, 0x62fc65f8, reinterpret_cast<void *>(&cFFApplyTransforms::Create)},
    {"nGZSceneGraph_cFFDepthOffset_Create", 0xac32f0, 32, 0x393c8118, reinterpret_cast<void *>(&cFFDepthOffset::Create)},
    {"nGZSceneGraph_cAttributeColorScalar_Create", 0xac3240, 32, 0xcf21ba53, reinterpret_cast<void *>(&cAttributeColorScalar::Create)},
    {"nGZSceneGraph_cAnimatedTextureMatrix_Create", 0xac30e0, 32, 0x7160cd3a, reinterpret_cast<void *>(&cAnimatedTextureMatrix::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
