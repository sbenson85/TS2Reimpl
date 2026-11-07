#include "hook/hooks.h"
#include "ts/cTSNeighborhoodConstants.h"

namespace {

const HookSpec kHooks[] = {
    {"FUN_a04df1b0", 0x4df1b0, 7, 0x788ea63c, MemberAddress(&cTSConstantsClient::Destruct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
