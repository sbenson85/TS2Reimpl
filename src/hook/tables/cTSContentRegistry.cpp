// cTSContentRegistry (lane KT, src/ts/cTSContentRegistry.h): the factory the common COM director registers. Every content
// registry is ours; the constructor, destructor, slots and helpers are in effect through it.
#include "hook/hooks.h"
#include "ts/cTSContentRegistry.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSContentRegistry_Create", 0xb57de0, 32, 0xdc2f278c, reinterpret_cast<void *>(&cTSContentRegistry::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
