#include "hook/hooks.h"
#include "rz/cRZRefCount.h"

namespace {

const HookSpec kHooks[] = {
    {"cRZUnknown_Release", 0x1b94e9, 23, 0x5d225f32, MemberAddress(&cRZUnknown::Release)},
    {"cRZUnknown_DeletingDestructor", 0x1b90f0, 31, 0x6164a914, MemberAddress(&cRZUnknown::DeletingDestructor)},
    {"cRZUnknown_Destruct", 0x1b9096, 7, 0x025af2ea, MemberAddress(&cRZUnknown::Destruct)},
    {"cRZRefCount_Release", 0x1b94af, 22, 0x5795594b, MemberAddress(&cRZRefCount::Release)},
    {"cRZRefCount_DeletingDestructor", 0x1b90b2, 31, 0x42edbb44, MemberAddress(&cRZRefCount::DeletingDestructor)},
    {"cRZRefCount_Destruct", 0x1b9088, 7, 0xcbf859e4, MemberAddress(&cRZRefCount::Destruct)},
    {"cRZRefCountMT_Release", 0x1b94c5, 32, 0x1aec2315, MemberAddress(&cRZRefCountMT::Release)},
    {"cRZRefCountMT_DeletingDestructor", 0x1b90d1, 31, 0x9c9bbea3, MemberAddress(&cRZRefCountMT::DeletingDestructor)},
    {"cRZRefCountMT_Destruct", 0x1b908f, 7, 0x62ba6568, MemberAddress(&cRZRefCountMT::Destruct)},
    {"RZAtomicAdd", 0x1bdbad, 17, 0x08e4393b, reinterpret_cast<void *>(&RZAtomicAdd)},
};
REGISTER_HOOKS(kHooks);

} // namespace
