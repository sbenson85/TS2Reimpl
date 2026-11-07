#include "hook/hooks.h"
#include "misc/UtilitiesD.h"

namespace {

// 0x5f921a and 0x5b7da4 are library code (std::map<int, int> _Find_lower_bound and
// _Lower_bound_duplicate, db/library-matches.jsonl) and are not hooked; FindNode (0x5f91ea) uses our
// own copies. The ValueForKey owner (0x93d6eb) reads the map at its +0x100.
const HookSpec kHooks[] = {
    {"UtilitiesD_FindChildAndSetEnabled", 0x9351fd, 32, 0x23c9f693, reinterpret_cast<void *>(&FindChildAndSetEnabled)},
    {"UtilitiesD_Origin_SubtractOrigin", 0xd13780, 32, 0xa838c67d, MemberAddress(&cUtilitiesDOrigin::SubtractOrigin)},
    {"UtilitiesD_TreeOwner_ValueForKey", 0x93d6eb, 32, 0xb6b4df63, MemberAddress(&cUtilitiesDTreeOwner::ValueForKey)},
};
REGISTER_HOOKS(kHooks);

} // namespace
