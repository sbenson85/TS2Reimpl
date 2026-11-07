#include "hook/hooks.h"
#include "rz/RZTextTrail.h"

namespace {

// The trail byte counts (lane VU, O-c).
const HookSpec kHooks[] = {
    {"RZText_Utf8TrailCount", 0x1b2817, 32, 0x103f224b, reinterpret_cast<void *>(&RZText::Utf8TrailCount)},
    {"RZText_CodePageTrailCount", 0x1b0dc9, 25, 0x32d5c8a0, reinterpret_cast<void *>(&RZText::CodePageTrailCount)},
    {"RZText_TrailCount", 0x1b06a6, 32, 0xb6290f61, reinterpret_cast<void *>(&RZText::TrailCount)},
};
REGISTER_HOOKS(kHooks);

} // namespace
