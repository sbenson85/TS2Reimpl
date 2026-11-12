#include "hook/hooks.h"
#include "sg/cDiffusionLightExtension.h"

namespace {

// The COM director's factory for class 0xaa561e2e, the constructor 0x2ea080's only caller: every diffusion light
// extension is ours from here (its other bodies are reached only through the vtables our objects replace;
// db/status/cDiffusionLightExtension.jsonl).
const HookSpec kHooks[] = {
    {"nTSSG_cDiffusionLightExtension_Create", 0x2ea3b0, 32, 0x9f514e64,
     reinterpret_cast<void *>(&nTSSG::cDiffusionLightExtension::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
