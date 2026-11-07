#include "hook/hooks.h"
#include "ts/cTSEffectsMaps.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSEffectsMaps_Create", 0xbd96a0, 32, 0x2dd8ea54, reinterpret_cast<void *>(&cTSEffectsMaps::Create)},
    {"cTSEffectsMaps_Construct", 0xbd95d0, 32, 0x759ec0de, reinterpret_cast<void *>(&cTSEffectsMapsAdapters::Construct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
