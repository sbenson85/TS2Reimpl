#include "hook/hooks.h"
#include "ts/cTSParticleEffectDescriptor.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSParticleEffectDescriptor_Create", 0x4db0c0, 32, 0x8b71d9a3, reinterpret_cast<void *>(&cTSParticleEffectDescriptor::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
