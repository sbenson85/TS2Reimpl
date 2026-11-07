#include "hook/hooks.h"
#include "numerics/PerlinNoise.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZNumerics_cPerlinNoise_Create", 0x98c450, 32, 0xf93605f5, reinterpret_cast<void *>(&nGZNumerics::cPerlinNoise::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
