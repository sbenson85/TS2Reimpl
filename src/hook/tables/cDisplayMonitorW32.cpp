#include "gz/cDisplayMonitorW32.h"
#include "hook/hooks.h"

namespace {

// The display monitor's factory (lane VU, O-c): every monitor the game makes is ours, with our vtable, so the image's
// slot bodies (QueryInterface 0x1547b0, 0x154790, SetMonitor 0x154620, the destructor 0x154550) are reached by nothing.
const HookSpec kHooks[] = {
    {"nGZGraphic4_cDisplayMonitorW32_Create", 0x1545c0, 32, 0x502377a4,
     reinterpret_cast<void *>(&nGZGraphic4::cDisplayMonitorW32::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
