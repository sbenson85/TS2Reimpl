#include "hook/hooks.h"
#include "ts/cTSAppPropertyFiles.h"

namespace {

using ts::cTSAppPropertyFiles;

// Lane MA (F4d): the application's property files, the object cTSAppFW keeps at +0x60. Plain functions (no vtable);
// nothing else in the image calls them.
const HookSpec kHooks[] = {
    {"cTSAppPropertyFiles_Construct", 0xcb0890, 32, 0xc8dd8871, MemberAddress(&cTSAppPropertyFiles::Construct)},
    {"cTSAppPropertyFiles_Destruct", 0xcb0ad0, 32, 0xd45ed800, MemberAddress(&cTSAppPropertyFiles::Destruct)},
    {"cTSAppPropertyFiles_SetupPaths", 0xcb0f40, 32, 0xe3666050, MemberAddress(&cTSAppPropertyFiles::SetupPaths)},
    {"cTSAppPropertyFiles_LoadProperties", 0xcb18a0, 32, 0x343c5fea, MemberAddress(&cTSAppPropertyFiles::LoadProperties)},
    {"cTSAppPropertyFiles_LoadGlobalInto", 0xcb1780, 16, 0xe084efca, MemberAddress(&cTSAppPropertyFiles::LoadGlobalInto)},
    {"cTSAppPropertyFiles_LoadConfigProperties", 0xcb1790, 32, 0x21e23035, MemberAddress(&cTSAppPropertyFiles::LoadConfigProperties)},
    {"cTSAppPropertyFiles_Save", 0xcb28b0, 32, 0xf907ed35, MemberAddress(&cTSAppPropertyFiles::Save)},
    {"cTSAppPropertyFiles_LoadUserProperties", 0xcb1e60, 32, 0x4d49463d, MemberAddress(&cTSAppPropertyFiles::LoadUserProperties)},
    {"cTSAppPropertyFiles_LoadPropertyList", 0xcb2020, 32, 0x7692c310, MemberAddress(&cTSAppPropertyFiles::LoadPropertyList)},
    {"cTSAppPropertyFiles_LoadSavedKeyNames", 0xcb2240, 32, 0xb168e6e4, MemberAddress(&cTSAppPropertyFiles::LoadSavedKeyNames)},
    {"cTSAppPropertyFiles_CopyAll", 0xcb0da0, 32, 0xacb3f402, reinterpret_cast<void *>(&cTSAppPropertyFiles::CopyAll)},
    {"cTSAppPropertyFiles_CopyListed", 0xcb0e50, 32, 0x34319676, reinterpret_cast<void *>(&cTSAppPropertyFiles::CopyListed)},
    {"cTSAppPropertyFiles_LoadXml", 0xcb1cf0, 32, 0x10941670, reinterpret_cast<void *>(&cTSAppPropertyFiles::LoadXml)},
    {"cTSAppPropertyFiles_ReadKeyNames", 0xcb2400, 32, 0xd9591e99, reinterpret_cast<void *>(&cTSAppPropertyFiles::ReadKeyNames)},
};
REGISTER_HOOKS(kHooks);

} // namespace
