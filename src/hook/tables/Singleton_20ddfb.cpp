#include "hook/hooks.h"
#include "rz/Singleton_20ddfb.h"

namespace {

const HookSpec kHooks[] = {
    {"Singleton_20ddfb_GetService", 0x1427b5, 6, 0xb9be616a, reinterpret_cast<void *>(&GetServiceObject)},
    {"ScopedRef_ReleaseObject", 0x1cbd79, 32, 0x0dee69db, MemberAddress(&ScopedRef::ReleaseObject)},
    {"ScopedRef_AcquireFromService", 0x20dc91, 32, 0x8d17888c, MemberAddress(&ScopedRef::AcquireFromService)},
    {"Singleton_20ddfb_Get", 0x20ddfb, 32, 0xd10f39cd, reinterpret_cast<void *>(&GetCachedSingleton)},
};
REGISTER_HOOKS(kHooks);

} // namespace
