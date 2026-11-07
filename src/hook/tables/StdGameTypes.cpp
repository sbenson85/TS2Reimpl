#include "hook/hooks.h"
#include "std/StdGameTypes.h"

namespace {

using namespace StdGameTypes;

const HookSpec kHooks[] = {
    {"FwRefList_EmplaceOpConstruct", 0x141503, 32, 0xb5db8879, reinterpret_cast<void *>(&ConstructFwRefEmplaceOp)},
    {"FwRefList_EmplaceOpDestroy", 0x141f0c, 32, 0x7f6f05d2, reinterpret_cast<void *>(&DestroyFwRefEmplaceOp)},
    {"RegionMap_EraseNode", 0x1be496, 32, 0x6854b8f3, MemberAddress(&RegionMapAccess::EraseNode)},
    {"ProcessHeapDeallocate", 0x1be435, 20, 0x04467913, reinterpret_cast<void *>(&ProcessHeapDeallocate)},
    {"String_AppendCharMove", 0x15aa90, 32, 0xf1027d73, reinterpret_cast<void *>(&AppendCharMove)},
    {"String_FindText", 0x1adba0, 32, 0xa3590a78, MemberAddress(&StringAccess::FindText)},
    {"String_FindLastOfText", 0x1af5d6, 32, 0xa3590a78, MemberAddress(&StringAccess::FindLastOfText)},
    {"RegionMap_Emplace", 0x1bdc30, 32, 0x3a9bee70, MemberAddress(&RegionMapAccess::Emplace)},
    {"RegionMap_TempNode", 0x1bdbc9, 32, 0xe2159e3f, reinterpret_cast<void *>(&ConstructRegionMapTempNode)},
    {"ProcessHeapAllocate", 0x1be421, 20, 0xf1c6dfd3, reinterpret_cast<void *>(&ProcessHeapAllocate)},
    {"FwServiceMap_FreeNode", 0x1419c3, 27, 0x72de0351, reinterpret_cast<void *>(&FreeFwServiceMapNode)},
    {"FwRefList_FreeNode", 0x1419de, 27, 0x60470d78, reinterpret_cast<void *>(&FreeFwRefListNode)},
    {"FwPriorityMap_FreeNode", 0x1419f9, 27, 0xf082c795, reinterpret_cast<void *>(&FreeFwPriorityNode)},
    {"FwServicePair_Delete", 0x1421bb, 32, 0x37cf37c2, reinterpret_cast<void *>(&DeleteFwServicePair)},
    {"FwPriorityPair_Delete", 0x1421e1, 32, 0xfb1eb52b, reinterpret_cast<void *>(&DeleteFwPriorityPair)},
    {"ByteVector_ResizeReallocate", 0x1af86f, 32, 0x461ec9a3, MemberAddress(&ByteVector::ResizeReallocate)},
};
REGISTER_HOOKS(kHooks);

} // namespace
