#include "hook/hooks.h"
#include "audio/MmxResampleTables.h"

namespace {

const HookSpec kHooks[] = {
    {"MmxResampleTables_Construct", 0xc94c78, 29, 0x4970e86e, MemberAddress(&MmxResampleTables::Construct)},
    {"MmxResampleTables_DetectMmx", 0xc94db1, 32, 0x5b5593a2, reinterpret_cast<void *>(&MmxResampleTables::DetectMmx)},
    {"MmxResampleTables_BuildTables", 0xc98e00, 32, 0x87163514, reinterpret_cast<void *>(&MmxResampleTables::BuildTables)},
};
REGISTER_HOOKS(kHooks);

} // namespace
