#include "hook/hooks.h"
#include "std/StdHash.h"

namespace {

// The 32-byte prefix of these lookups is the same code in all of them (the key comparison comes
// later), so several entries share a CRC. Each entry still checks the bytes at its own RVA.
const HookSpec kHooks[] = {
    {"FUN_a01f10b0", 0x1f10b0, 32, 0x1f99484b, MemberAddress(&StdHashTable::Find64)},
    {"FUN_a02c43b0", 0x2c43b0, 32, 0x77e59e47, MemberAddress(&StdHashTable::Find96)},
    {"FUN_a04eba70", 0x4eba70, 32, 0x1f99484b, MemberAddress(&StdHashTable::Find96)},
    {"FUN_a01f5570", 0x1f5570, 32, 0x77e59e47, MemberAddress(&StdHashTable::Find128)},
    {"FUN_a01d5c20", 0x1d5c20, 32, 0x6af1f9c4, MemberAddress(&StdHashTable::FindStringAt12)},
    {"FUN_a04ebb00", 0x4ebb00, 32, 0x774981ac, MemberAddress(&StdHashTable::FindStringAt12)},
    {"FUN_a0cd8b30", 0xcd8b30, 32, 0xe807882b, MemberAddress(&StdHashTable::FindCString)},
};
REGISTER_HOOKS(kHooks);

} // namespace
