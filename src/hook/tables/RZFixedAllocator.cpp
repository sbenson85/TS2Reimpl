#include "hook/hooks.h"
#include "rz/RZFixedAllocator.h"

namespace {

// Non-virtual members of rz::cRZFastFixedAllocator2Common (single inheritance: hooked by member address).
const HookSpec kHooks[] = {
    {"RZFixedAllocator_TakeFreePage", 0x1be2bc, 32, 0xc829a3e1, MemberAddress<&rz::cRZFastFixedAllocator2Common::TakeFreePage>()},
    {"RZFixedAllocator_ReturnFullPage", 0x1be31b, 32, 0x54042e60, MemberAddress<&rz::cRZFastFixedAllocator2Common::ReturnFullPage>()},
    {"RZFixedAllocator_ReleaseEmptyPage", 0x1be02b, 32, 0x8ef45c72, MemberAddress<&rz::cRZFastFixedAllocator2Common::ReleaseEmptyPage>()},
};
REGISTER_HOOKS(kHooks);

} // namespace
