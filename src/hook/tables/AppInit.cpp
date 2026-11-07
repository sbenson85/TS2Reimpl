#include "app/AppInit.h"
#include "hook/hooks.h"

namespace {

// Startup helpers (src/app/AppInit.*). 0x9c5a5d is the CachedServiceGetter instantiation.
const HookSpec kHooks[] = {
    {"FUN_a09c5a5d", 0x9c5a5d, 32, 0xad8ecc89, reinterpret_cast<void *>(&CachedServiceGetter<appinit::kLanguageServiceCacheRva, appinit::kLanguageServiceFirstId, appinit::kLanguageServiceSecondId>)},
    {"FUN_a01d0c00", 0x1d0c00, 32, 0xf3a943b2, reinterpret_cast<void *>(&appinit::CommonImplSlot4)},
    {"FUN_a01d0c60", 0x1d0c60, 32, 0x0d9afbf2, reinterpret_cast<void *>(&appinit::CommonImplSlot5)},
    {"FUN_a01d0d10", 0x1d0d10, 32, 0x17e41c94, reinterpret_cast<void *>(&appinit::CommonImplSlot3)},
    {"FUN_a01bec45", 0x1bec45, 26, 0xebed8e61, MemberAddress(&appinit::cGZPropertySetBase::Construct)},
    // 0x22b157 (cGZPropertySetString's constructor) moved to src/hook/tables/cGZPropertySet.cpp (lane EQ: it builds our class).
    // 0xc26045 (the property manager's constructor) moved to src/hook/tables/cGZPropertyManager.cpp (lane PU: it builds our class).
    // 0xc644aa (cRZNetSSLManager's constructor) moved to src/hook/tables/cRZNetSSLManager.cpp (lane PW: it builds our class).
    // 0xd0b6c0 (nTSWorld::cWorldDB's constructor) moved to src/hook/tables/cWorldDB.cpp (lane JC: it builds our class).
    {"FUN_a0b39380", 0xb39380, 32, 0x42018bdc, reinterpret_cast<void *>(&appinit::GetOptionMask)},
    {"FUN_a0b39470", 0xb39470, 25, 0x57977431, reinterpret_cast<void *>(&appinit::HighestOptionBit)},
    {"FUN_a0b39490", 0xb39490, 17, 0x06eb15c2, reinterpret_cast<void *>(&appinit::HasOptionBits)},
    {"FUN_a0c274e3", 0xc274e3, 32, 0xc049eca6, reinterpret_cast<void *>(&appinit::ReadApplicationOptions)},
    {"FUN_a0c29a0b", 0xc29a0b, 32, 0xc0dddc80, reinterpret_cast<void *>(&appinit::InitLanguage)},
};
REGISTER_HOOKS(kHooks);

} // namespace
