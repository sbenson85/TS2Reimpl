#include "hook/hooks.h"
#include "rz/StaticGetters2.h"

namespace {

// 0x20df6f is also patched by TS2-Extender (GetNodeTextInputField, at 0x20df74); native mod DLLs are
// no longer a compatibility target (CLAUDE.md), so it's hooked like any other.
const HookSpec kHooks[] = {
    {"FUN_a020df6f", 0x20df6f, 32, 0xd5dde591, reinterpret_cast<void *>(&GetCachedSingletonC)},
    {"StaticGetters2_AcquireServiceRefB", 0x20dcb6, 32, 0xa3f12b81, reinterpret_cast<void *>(&AcquireServiceRefB)},
    {"StaticGetters2_AcquireServiceRefC", 0x20dd00, 32, 0xd9b115fd, reinterpret_cast<void *>(&AcquireServiceRefC)},
    {"StaticGetters2_GetCachedSingletonB", 0x20de51, 32, 0x3c2b3b29, reinterpret_cast<void *>(&GetCachedSingletonB)},
    {"StaticGetters2_GetCachedSlot23", 0x20dde0, 27, 0xb8459f02, reinterpret_cast<void *>(&GetCachedSlot23)},
    {"StaticGetters2_GetCachedObjectB", 0xaeb820, 32, 0x87a98565, reinterpret_cast<void *>(&GetCachedObjectB)},
    {"StaticGetters2_Dispatch8", 0xaeb800, 19, 0x80d2585e, reinterpret_cast<void *>(&Dispatch8)},
    {"StaticGetters2_Dispatch8Adjusted", 0xa3c4b0, 8, 0xf1672009, reinterpret_cast<void *>(&Dispatch8Adjusted)},
    {"StaticGetters2_ServiceCellConstructFrom", 0xaeb710, 29, 0x151ce8e7, MemberAddress(&ServiceCell::ConstructFrom)},
    {"StaticGetters2_ServiceCellConstructCopy", 0xaeb6f0, 31, 0xbbb4595a, MemberAddress(&ServiceCell::ConstructCopy)},
    {"StaticGetters2_ServiceCellOpen", 0xaeb730, 32, 0xbdb31004, MemberAddress(&ServiceCell::Open)},
    {"StaticGetters2_ServiceCellAssign", 0xaeb790, 32, 0x27fa2719, MemberAddress(&ServiceCell::Assign)},
    {"StaticGetters2_ServiceCellReset", 0xaeb7e0, 30, 0x3827cfa5, MemberAddress(&ServiceCell::Reset)},
    {"StaticGetters2_ServiceCellClose", 0xaeb760, 32, 0x4c0e615e, MemberAddress(&ServiceCell::Close)},
};
REGISTER_HOOKS(kHooks);

} // namespace
