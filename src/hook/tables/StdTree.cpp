#include "hook/hooks.h"
#include "std/StdTree.h"

namespace {

// 0x144c95 and 0x144d39 are library code (db/library-matches.jsonl) and are not hooked.
const HookSpec kHooks[] = {
    {"FUN_a01d5d10", 0x1d5d10, 32, 0x383c39f1, MemberAddress(&StdTreeHead::FindLowerBoundKey4)},
};
REGISTER_HOOKS(kHooks);

} // namespace
