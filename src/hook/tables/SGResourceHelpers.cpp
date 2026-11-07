#include "hook/hooks.h"
#include "sg/SGResourceHelpers.h"

namespace {

const HookSpec kHooks[] = {
    {"SGResourceHelpers_SignBitBeforeThis", 0xa3bdb0, 7, 0x0d0a36cc, MemberAddress(&SGResourceHelpers::cSignBitBeforeThis::SignBit)},
    {"SGResourceHelpers_SubmitKeyToService", 0xa3c790, 32, 0x55d32d28, MemberAddress(&SGResourceHelpers::cSGSecondary::SubmitKeyToService)},
    {"SGResourceHelpers_VirtualBaseIfObject", 0xa3c750, 20, 0x6b4f39ef, MemberAddress(&SGResourceHelpers::cSGSecondary::VirtualBaseIfObject)},
    {"SGResourceHelpers_ResetAndSubmit", 0xa3c890, 32, 0x10327104, MemberAddress(&SGResourceHelpers::cSGSecondary::ResetAndSubmit)},
    {"SGResourceHelpers_OpenAssignAndSubmit", 0xa3ca00, 32, 0x4895b18a, MemberAddress(&SGResourceHelpers::cSGSecondary::OpenAssignAndSubmit)},
    {"SGResourceHelpers_AssignAndSubmit", 0xa3cbf0, 32, 0xe3dd2c3c, MemberAddress(&SGResourceHelpers::cSGSecondary::AssignAndSubmit)},
};
REGISTER_HOOKS(kHooks);

} // namespace
