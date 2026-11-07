#include "app/ReportFinish.h"
#include "hook/hooks.h"

namespace {

using namespace appbase;

using VersionConstruct = cRZVersion *(cRZVersion::*)();

const HookSpec kHooks[] = {
    {"ReportFinish_WriteCallStack", 0x1bc29e, 32, 0x144e3638, reinterpret_cast<void *>(&WriteCallStack)},
    {"cRZVersion_ConstructFromPath", 0x1c98ab, 32, 0xca0e0941, MemberAddress(&cRZVersion::ConstructFromPath)},
    {"cRZVersion_Construct", 0x1c98d6, 32, 0x0f18513c, MemberAddress(static_cast<VersionConstruct>(&cRZVersion::Construct))},
    {"cRZVersion_Destruct", 0x1c9901, 32, 0x417c923c, MemberAddress(&cRZVersion::Destruct)},
    {"cRZVersion_DeletingDestructor", 0x1c992d, 31, 0xa72b5c37, MemberAddress(&cRZVersion::DeletingDestructor)},
    {"cRZVersion_Load", 0x1c9da3, 32, 0xf721e063, MemberAddress(&cRZVersion::Load)},
    {"cRZVersion_GetPath", 0x1c9b58, 21, 0xddf38013, MemberAddress(&cRZVersion::GetPath)},
    {"cRZVersion_GetDescription", 0x1c9ad1, 32, 0x7402fb91, MemberAddress(&cRZVersion::GetDescription)},
    {"cRZVersion_GetFileVersion", 0x1c9b6d, 32, 0xa7afffe5, MemberAddress(&cRZVersion::GetFileVersion)},
    {"cRZVersion_GetDebugVersion", 0x1c9a4a, 32, 0x72ace485, MemberAddress(&cRZVersion::GetDebugVersion)},
    {"cRZVersion_GetSystemVersion", 0x1c9c6b, 32, 0x707eb237, MemberAddress(&cRZVersion::GetSystemVersion)},
    {"cRZVersion_GetMemory", 0x1c9c01, 32, 0xbf0f7f26, MemberAddress(&cRZVersion::GetMemory)},
    {"ReportFinish_VerifyWindowsVersion", 0x1c9d0b, 32, 0xa86a68cb, reinterpret_cast<void *>(&VerifyWindowsVersion)},
    {"ReportFinish_WriteSystemInfo", 0x1bb94c, 32, 0x0c69f17a, reinterpret_cast<void *>(&WriteSystemInfo)},
};
REGISTER_HOOKS(kHooks);

} // namespace
