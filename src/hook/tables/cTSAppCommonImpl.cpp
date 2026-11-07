#include "hook/hooks.h"
#include "ts/cTSAppCommonImpl.h"

namespace {

// ts::cTSAppCommonImpl in the standalone shape (lane EP): every entry is an adapter (docs/standalone-shape.md). The
// game's one creation site is cTSAppFW's constructor 0xc26147 (ours, hook-shaped: it builds the +0x44 part with
// appbase::cTSAppCommonImpl::Construct and the game's vtable 0xe98510), so in game these entries serve the application
// object's game vtable. Lane EV added SetupUserDataDirectories 0xca8710, ReadResourceConfiguration 0xca6c10 and its three
// section callbacks (five entries; bisect them together); lane FC FindProductDirectories 0xca75c0 and 0xca6910 (the last two).
using A = ts::cTSAppCommonImplAdapters;
const HookSpec kHooks[] = {
    {"cTSAppCommonImpl_Construct", 0xca4cb0, 32, 0x0b8add0f, reinterpret_cast<void *>(&A::Construct)},
    {"cTSAppCommonImpl_DestructBody", 0xca4de0, 32, 0xdf8a0019, reinterpret_cast<void *>(&A::Destruct)},
    {"cTSAppCommonImpl_DeletingDestructor", 0xca4f50, 32, 0x12e66615, reinterpret_cast<void *>(&A::DeletingDestructor)},
    {"cTSAppCommonImpl_QueryInterface", 0xca6ff0, 32, 0xce0d7623, reinterpret_cast<void *>(&A::QueryInterface)},
    {"cTSAppCommonImpl_AddOption", 0xca7230, 32, 0x89e22739, reinterpret_cast<void *>(&A::AddOption)},
    {"cTSAppCommonImpl_GetOptionText", 0xca5e40, 32, 0x73ba861a, reinterpret_cast<void *>(&A::GetOptionText)},
    {"cTSAppCommonImpl_GetOptionList", 0xca5e80, 32, 0x3c3b8e9b, reinterpret_cast<void *>(&A::GetOptionList)},
    {"cTSAppCommonImpl_HasOption", 0xca6bd0, 32, 0xca22434c, reinterpret_cast<void *>(&A::HasOption)},
    {"cTSAppCommonImpl_IsNameUnused", 0xca69f0, 32, 0x329eefd9, reinterpret_cast<void *>(&A::IsNameUnused)},
    {"cTSAppCommonImpl_IsOptionNameUnused", 0xca6920, 32, 0x086cefc1, reinterpret_cast<void *>(&A::IsOptionNameUnused)},
    {"cTSAppCommonImpl_ReadOptionText", 0xca5ed0, 32, 0x4418617b, reinterpret_cast<void *>(&A::ReadOptionText)},
    {"cTSAppCommonImpl_ReadOptionList", 0xca6200, 32, 0x7ab0d30c, reinterpret_cast<void *>(&A::ReadOptionList)},
    {"cTSAppCommonImpl_RegisterRegistryHandlers", 0xca5d20, 32, 0x40bd7e19, reinterpret_cast<void *>(&A::RegisterRegistryHandlers)},
    // Lane EV: slots 15 and 16 and the resource ini's section callbacks (only 0xca6c10 passes their addresses).
    {"cTSAppCommonImpl_SetupUserDataDirectories", 0xca8710, 32, 0x3f5270c4, reinterpret_cast<void *>(&A::SetupUserDataDirectories)},
    {"cTSAppCommonImpl_ReadResourceConfiguration", 0xca6c10, 32, 0x5b414590, reinterpret_cast<void *>(&A::ReadResourceConfiguration)},
    {"cTSAppCommonImpl_OnResourceFactory", 0xca74b0, 32, 0x2aafc74c, reinterpret_cast<void *>(&A::OnResourceFactory)},
    {"cTSAppCommonImpl_OnDBSegment", 0xca7020, 32, 0x2aafc74c, reinterpret_cast<void *>(&A::OnDBSegment)},
    {"cTSAppCommonImpl_OnDBSegmentDirectoryFile", 0xca7130, 32, 0x2aafc74c, reinterpret_cast<void *>(&A::OnDBSegmentDirectoryFile)},
    // Lane FC: slot 14 and its flag test (only 0xca75c0 calls 0xca6910).
    {"cTSAppCommonImpl_FindProductDirectories", 0xca75c0, 32, 0x7e013856, reinterpret_cast<void *>(&A::FindProductDirectories)},
    {"cTSAppCommonImpl_HasFlags", 0xca6910, 13, 0x76c240a1, reinterpret_cast<void *>(&A::HasFlags)},
    // Lane MB: the registry handlers (RegisterRegistryHandlers passes ours) and the segment builder (__cdecl).
    {"cTSAppCommonImpl_OnResourceFactoryEntry", 0xca5db0, 32, 0x8579282f, reinterpret_cast<void *>(&A::OnResourceFactoryEntry)},
    {"cTSAppCommonImpl_OnPathRegistryEntry", 0xca5170, 32, 0x4f15766b, reinterpret_cast<void *>(&A::OnPathRegistryEntry)},
    {"cTSAppCommonImpl_AddPathSegment", 0xca4f70, 32, 0x4dbdd2e2, reinterpret_cast<void *>(&A::AddPathSegment)},
};
REGISTER_HOOKS(kHooks);

} // namespace
