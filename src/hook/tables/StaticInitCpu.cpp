#include "hook/hooks.h"
#include "app/StaticInitContainers.h"

// Lane II: the CPU feature detection slot 765's entry calls (db/status/StaticInitCpu.jsonl).

namespace {

const HookSpec kHooks[] = {
    {"DetectCpuFeatures", 0xee0d1, 32, 0xa2f23343, reinterpret_cast<void *>(static_cast<uint32_t(__cdecl *)()>(&staticinit::DetectCpuFeatures))},
    {"CpuidAvailable", 0xee07f, 32, 0xf7d2f512, reinterpret_cast<void *>(&staticinit::CpuidAvailable)},
    {"CpuidQuery", 0xee0a9, 32, 0xa51938e1, reinterpret_cast<void *>(&staticinit::CpuidQuery)},
};
REGISTER_HOOKS(kHooks);

} // namespace
