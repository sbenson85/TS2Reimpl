#include "hook/hooks.h"
#include "gz/MultiMon.h"

namespace {

// The stubs' initialiser, shared with game code that reads the same statics, and GetSystemMetrics (lane RB); the other
// two (0x154950, 0x154ae0) are reached only from ours (status "in_effect": "callers").
const HookSpec kHooks[] = {
    {"FUN_a01547f0", 0x1547f0, 32, 0x0c5766db, reinterpret_cast<void *>(&nMultiMon::InitMultipleMonitorStubs)},
    {"nMultiMon_GetSystemMetricsStub", 0x154a80, 32, 0xe9e52e5c, reinterpret_cast<void *>(&nMultiMon::GetSystemMetricsStub)},
};
REGISTER_HOOKS(kHooks);

} // namespace
