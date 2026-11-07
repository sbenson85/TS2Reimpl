#include "app/IniFile.h"
#include "hook/hooks.h"

namespace {

const HookSpec kHooks[] = {
    {"IniFile_IsAsciiLetter", 0x1c0f23, 24, 0xe19a26cc, reinterpret_cast<void *>(&inifile::IsAsciiLetter)},
    {"IniFile_IsCanonicalAbsolutePath", 0x1c2215, 32, 0xf4373095, reinterpret_cast<void *>(&inifile::IsCanonicalAbsolutePath)},
    {"IniFile_IsCanonicalAbsolutePathString", 0x1c21fe, 23, 0x243770f3, reinterpret_cast<void *>(&inifile::IsCanonicalAbsolutePathString)},
    {"IniFile_CombinePath", 0x1c148f, 32, 0xd38eb6bd, reinterpret_cast<void *>(&inifile::CombinePath)},
    {"IniFile_MakeAbsolutePath", 0x1c1655, 32, 0xceff3ba5, reinterpret_cast<void *>(&inifile::MakeAbsolutePath)},
    {"IniFile_AbsolutePathString", 0x1c1760, 32, 0x2d73041a, reinterpret_cast<void *>(&inifile::AbsolutePathString)},
    {"IniFile_CombinePathString", 0x1c17bd, 32, 0xe49a9f36, reinterpret_cast<void *>(&inifile::CombinePathString)},
    {"IniFile_ResolvePathString", 0x1c33df, 32, 0x79547735, reinterpret_cast<void *>(&inifile::ResolvePathString)},
    {"cRZIniFile_Construct", 0x9c5d11, 32, 0x20c06431, MemberAddress(&cRZIniFile::Construct)},
    {"cRZIniFile_Destruct", 0x9c5e5d, 32, 0xbf8b01a3, MemberAddress(&cRZIniFile::Destruct)},
    {"cRZIniFile_DeletingDestructor", 0x9c5eac, 31, 0x11b1b34e, MemberAddress(&cRZIniFile::DeletingDestructor)},
    {"cRZIniFile_GetString", 0x9c679f, 32, 0x0cbf02a1, MemberAddress(&cRZIniFile::GetString)},
    {"cRZIniFile_TrimLeading", 0x1ad11d, 30, 0xc76b6077, MemberAddress(&cRZIniStringHelpers::TrimLeading)},
    {"cRZIniFile_TrimTrailing", 0x1ad307, 29, 0xea935b76, MemberAddress(&cRZIniStringHelpers::TrimTrailing)},
    {"cRZIniFile_SplitOff", 0x1ad555, 32, 0xa764690e, MemberAddress(&cRZIniStringHelpers::SplitOff)},
    {"IniFile_ConcatMove", 0x2f5500, 32, 0xa1c661a8, reinterpret_cast<void *>(&inifile::ConcatMove)},
};
REGISTER_HOOKS(kHooks);

} // namespace
