#include "hook/hooks.h"
#include "simantics/SnapReport.h"

namespace {

// The error report of the snap tests (lane AJ): the message lookup and the report built from the snap state.
const HookSpec kHooks[] = {
    {"SnapErrorReport_LookupMessage", 0x7f0c00, 32, 0xb1e1ee7e, MemberAddress(&SnapErrorReport::LookupMessage)},
    {"SnapStateReports_BuildReport", 0x7f0ca0, 32, 0x08b9c57d, MemberAddress(&SnapStateReports::BuildReport)},
};
REGISTER_HOOKS(kHooks);

} // namespace
