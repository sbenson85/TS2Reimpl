#include "hook/hooks.h"
#include "rz/cGZAllocatorService.h"

namespace {

// The standalone shape (lane KD): the constructor builds our class (our vtables on all three faces); the destructor, the
// deleting destructor, QueryInterface and the thunks 0x1be62d, 0x1be683, 0x1be68b are reached only through the game's
// vtables, which nothing stores any more (in effect through callers, trapped by tools/callers.py --write-traps).
const HookSpec kHooks[] = {
    {"cGZAllocatorService_Construct", 0x1be59c, 32, 0x7ff5d871, reinterpret_cast<void *>(&cGZAllocatorServiceAdapters::Construct)},
    {"AllocatorServiceHolder_Acquire", 0x1be577, 32, 0xa590334a, MemberAddress(&AllocatorServiceHolder::Acquire)},
    {"AllocatorServiceHolder_Release", 0x1be5d1, 10, 0xca78d202, MemberAddress(&AllocatorServiceHolder::Release)},
    {"GetAllocatorService", 0x1be693, 32, 0x861ff080, reinterpret_cast<void *>(&GetAllocatorService)},
    {"FreeThroughAllocatorService", 0x197100, 32, 0x5c93ffe2, reinterpret_cast<void *>(&FreeThroughAllocatorService)},
    {"FUN_a01d0903", 0x1d0903, 32, 0x1bb8f55f, MemberAddress(&GZServiceAllocatorBase::Deallocate)},
};
REGISTER_HOOKS(kHooks);

} // namespace
