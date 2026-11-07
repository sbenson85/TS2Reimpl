#include "hook/hooks.h"
#include "gz/cConnectionManager.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZEvent_cConnectionManager_Construct", 0x1aa630, 32, 0x8a1269b3,
     reinterpret_cast<void *>(&nGZEvent::cConnectionManagerAdapters::Construct)},
    {"nGZEvent_cConnectionManager_Destruct", 0x1aa69a, 32, 0x017d92fe,
     reinterpret_cast<void *>(&nGZEvent::cConnectionManagerAdapters::Destruct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
