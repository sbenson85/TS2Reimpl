#include "hook/hooks.h"
#include "rz/cRZMemoryReport.h"

namespace {

const HookSpec kHooks[] = {
    {"cRZMemoryHook_Construct", 0xc696ce, 11, 0xf6f6c341, MemberAddress(&cRZMemoryHook::Construct)},
    {"cRZMemoryReport_Construct", 0xc696d9, 32, 0x100084fa, MemberAddress(&cRZMemoryReport::Construct)},
    {"CountFromOne_Construct", 0x1baa70, 9, 0x6d9ef061, MemberAddress(&CountFromOne::Construct)},
    {"LoadGlobalDbgHelp", 0x1bcc62, 10, 0x5269f458, reinterpret_cast<void *>(&LoadGlobalDbgHelp)},
};
REGISTER_HOOKS(kHooks);

} // namespace
