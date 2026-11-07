#include "hook/hooks.h"
#include "rz/RZMemory.h"

namespace {

// 0x1be4c7 (folded with unrelated classes) and 0x1be4e8 (listed as library code) are not hooked.
const HookSpec kHooks[] = {
    {"RZMemory_Malloc", 0x1be3b0, 17, 0x3869f7b2, reinterpret_cast<void *>(&RZMemory::Malloc)},
    {"RZMemory_Free", 0x1be3a1, 15, 0x974f738a, reinterpret_cast<void *>(&RZMemory::Free)},
    {"RZMemory_Realloc", 0x1be3c1, 21, 0x7c627294, reinterpret_cast<void *>(&RZMemory::Realloc)},
    {"cGZDefaultAllocator_FreeIfNotNull", 0x1be4d4, 20, 0x9bc96ad6, MemberAddress(&RZMemory::Service::ServiceFreeIfNotNull)},
    {"cGZAllocatorService_Free", 0x1be654, 13, 0x6d0a333f, MemberAddress(&RZMemory::Service::ServiceFree)},
};
REGISTER_HOOKS(kHooks);

} // namespace
