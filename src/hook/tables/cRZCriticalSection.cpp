#include "hook/hooks.h"
#include "rz/cRZCriticalSection.h"

namespace {

const HookSpec kHooks[] = {
    {"cRZCriticalSection_Construct", 0x1af64e, 32, 0x90c27f7f, MemberAddress(&cRZCriticalSection::Construct)},
    {"cRZCriticalSection_Destruct", 0x1af68a, 30, 0xcdc3c0ef, MemberAddress(&cRZCriticalSection::Destruct)},
    {"cRZCriticalSection_DeletingDestructor", 0x1af6a8, 31, 0x046eca98, MemberAddress(&cRZCriticalSection::DeletingDestructor)},
    {"cRZCriticalSection_Release", 0x1af6e2, 14, 0x0a6cbc9c, MemberAddress(&cRZCriticalSection::Release)},
    {"cRZCriticalSection_Lock", 0x1af6ce, 20, 0x5574d8d9, MemberAddress(&cRZCriticalSection::Lock)},
    {"cRZCriticalSection_Unlock", 0x1af715, 22, 0x04a9debe, MemberAddress(&cRZCriticalSection::Unlock)},
    {"cRZCriticalSection_TryLock", 0x1af6f0, 32, 0x583a1341, MemberAddress(&cRZCriticalSection::TryLock)},
    {"cRZCriticalSection_IsLocked", 0x1af6c7, 7, 0xec4da9d6, MemberAddress(&cRZCriticalSection::IsLocked)},
};
REGISTER_HOOKS(kHooks);

} // namespace
