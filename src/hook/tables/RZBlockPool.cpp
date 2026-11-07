#include "hook/hooks.h"
#include "rz/RZBlockPool.h"

namespace {

const HookSpec kHooks[] = {
    {"RZBlockPool_ConstructBase", 0x1bdd0e, 32, 0x49e1fdc1, MemberAddress(&RZBlockPool::ConstructBase)},
    {"RZBlockPool_ConstructSmallBase", 0x1bdce5, 32, 0xbacc2672, MemberAddress(&RZBlockPool::ConstructSmallBase)},
    {"RZBlockPool_ConstructSmall", 0x1bdd4e, 27, 0xfc7eba4f, MemberAddress(&RZBlockPool::ConstructSmall)},
    {"RZBlockPool_AllocSmall", 0x1bdd69, 32, 0x6e66a225, MemberAddress(&RZBlockPool::AllocSmall)},
    {"RZBlockPool_AllocLarge", 0x1bdeb8, 32, 0x50ec2f9f, MemberAddress(&RZBlockPool::AllocLarge)},
    {"RZBlockPool_FreeSmall", 0x1be050, 32, 0xdea73be9, MemberAddress(&RZBlockPool::FreeSmall)},
    {"RZBlockPool_FreeLarge", 0x1be133, 32, 0xafa5be34, MemberAddress(&RZBlockPool::FreeLarge)},
    {"cRZSlabAllocatorLarge_NewRegion", 0x1be007, 23, 0x54c09723, MemberAddress(&RZBlockPool::LargeNewRegion)},
    {"cRZSlabAllocatorLarge_ReleaseRegion", 0x1be216, 20, 0x317efdf2, MemberAddress(&RZBlockPool::LargeReleaseRegion)},
    {"cRZSlabAllocatorSmall_NewRegion", 0x1be01e, 13, 0x3db53ea6, MemberAddress(&RZBlockPool::SmallNewRegion)},
    {"cRZSlabAllocatorSmall_ReleaseRegion", 0x1be22a, 17, 0x1f5098ad, MemberAddress(&RZBlockPool::SmallReleaseRegion)},
    {"RZBlockPool_ClearSmallBits", 0x1be26e, 32, 0x5547220c, MemberAddress(&RZBlockPool::ClearSmallBits)},
    {"RZBlockPool_ClearLargeBits", 0x1be295, 32, 0xdd2bb3ac, MemberAddress(&RZBlockPool::ClearLargeBits)},
    {"RZBlockPoolsCreate", 0x1be449, 32, 0x3f2bcf7b, reinterpret_cast<void *>(&RZBlockPoolsCreate)},
    {"RZBlockPoolAlloc", 0x1be354, 32, 0x55916049, reinterpret_cast<void *>(&RZBlockPoolAlloc)},
    {"RZBlockPoolFree", 0x1be3d6, 32, 0xefc64c26, reinterpret_cast<void *>(&RZBlockPoolFree)},
};
REGISTER_HOOKS(kHooks);

} // namespace
