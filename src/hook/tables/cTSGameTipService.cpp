// cTSGameTipService (lane KX, src/ts/cTSGameTipService.h): the factory the COM director registers, so every game tip
// service is ours (and through it every cTSGameTip and the tip resource factory); the constructor, destructor, slots and
// helpers are in effect through it. The cached getter of the service is hooked too (its callers are still game code).
#include "hook/hooks.h"
#include "ts/cTSGameTipService.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSGameTipService_Create", 0xcb5ce0, 32, 0x6bd8ffa6, reinterpret_cast<void *>(&cTSGameTipService::Create)},
    {"GetGameTipService", 0xb39020, 32, 0x8dfba668, reinterpret_cast<void *>(&GetGameTipService)},
};
REGISTER_HOOKS(kHooks);

} // namespace
