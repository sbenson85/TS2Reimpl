#include "hook/hooks.h"
#include "ts/ChangeRecordSet.h"

namespace {

// One group's cached resources in the persist system's change records (lane IP).
const HookSpec kHooks[] = {
    {"ChangeRecordSet_Construct", 0x1f5850, 32, 0x127fd36b, reinterpret_cast<void *>(&ChangeRecordSetAdapters::Construct)},
    {"ChangeRecordSet_Erase", 0x1f6410, 32, 0x1f0d5a4c, MemberAddress<&ChangeRecordSet::Erase>()},
    {"ChangeRecordSet_Take", 0x1f6780, 32, 0x6c8a6946, MemberAddress<&ChangeRecordSet::Take>()},
    {"ChangeRecordSet_Put", 0x1f6de0, 32, 0x9312c6df, MemberAddress<&ChangeRecordSet::Put>()},
    {"ChangeRecordSet_Read", 0x1f6bd0, 32, 0x714d7332, MemberAddress<&ChangeRecordSet::Read>()},
    {"ChangeRecordSet_Write", 0x1f6e90, 32, 0xb1e4c2b6, MemberAddress<&ChangeRecordSet::Write>()},
};
REGISTER_HOOKS(kHooks);

} // namespace
