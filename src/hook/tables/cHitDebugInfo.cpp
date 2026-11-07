// cHitDebugInfo (lane LH, src/audio/cHitDebugInfo.h): the factory the hit manager's COM director registers, so every HIT
// debug information service is ours; the constructor, destructor, slots and the modules' members are in effect through it.
#include "audio/cHitDebugInfo.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"cHitDebugInfo_Create", 0xb2a30c, 32, 0xa0b54eb8, reinterpret_cast<void *>(&cHitDebugInfo::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
