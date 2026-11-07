#include "hook/hooks.h"
#include "ts/cPreloadedRecord.h"

namespace {

const HookSpec kHooks[] = {
    {"AVcPreloadedRecord_A0x0565dbc6_QueryInterface", 0x1f82b0, 32, 0xb96a7386, MemberAddress(&cPreloadedRecord::QueryInterface)},
    {"AVcPreloadedRecord_A0x0565dbc6_GetFieldVoid", 0x1f7d50, 32, 0x98874b86, MemberAddress(&cPreloadedRecord::GetFieldVoid)},
    {"AVcPreloadedRecord_A0x0565dbc6_SeekAbsolute", 0x1f8c00, 22, 0xec3790c5, MemberAddress(&cPreloadedRecord::SeekAbsolute)},
    {"AVcPreloadedRecord_A0x0565dbc6_SeekRelative", 0x1f8c20, 32, 0xf862822c, MemberAddress(&cPreloadedRecord::SeekRelative)},
    {"FUN_a01f74b0", 0x1f74b0, 32, 0x8a35a8e8, MemberAddress(&cPreloadedRecord::DeletingDestructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
