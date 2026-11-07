#include "hook/hooks.h"
#include "rz/FoundationsHelpers.h"

namespace {

const HookSpec kHooks[] = {
    {"RefCountHolder_Destroy", 0x99e700, 32, 0x93d9c7a1, MemberAddress(&ServiceRefHolder::Destroy)},
    {"cTSAppFW_GetGZCOMClassObject_9b3798f7", 0xc260bc, 32, 0xd44c1e27, MemberAddress(&ServiceRefHolder::AcquireSettingsDelegate)},
    {"cGZFramework_KeyRefPair_Construct", 0x141533, 32, 0xda61e8c1, MemberAddress(&KeyRefPair::Construct)},
    {"nGZLog_FindLevelOrDefault", 0xc9bff9, 32, 0x2b0177e9, reinterpret_cast<void *>(&nGZLog::FindLogTableEntry)},
    {"cTSAppCommonImpl_FindListEntryMatching", 0xca4ac0, 32, 0xb1a73eb9, reinterpret_cast<void *>(&FindOptionEntry)},
};
REGISTER_HOOKS(kHooks);

} // namespace
