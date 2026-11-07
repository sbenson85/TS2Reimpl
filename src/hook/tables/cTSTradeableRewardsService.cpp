// cTSTradeableRewardsService (lane KZ, src/ts/cTSTradeableRewardsService.h): the factory the COM director registers, so
// every tradeable rewards service is ours (and through it every reward info it hands out); the constructor, destructor,
// slots and helpers are in effect through it.
#include "hook/hooks.h"
#include "ts/cTSTradeableRewardsService.h"

namespace {

const HookSpec kHooks[] = {
    {"cTSTradeableRewardsService_Create", 0xcb7b50, 32, 0xcf5e5e64, reinterpret_cast<void *>(&cTSTradeableRewardsService::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
