#include "hook/hooks.h"
#include "rz/WinApiTables.h"

namespace {

const HookSpec kHooks[] = {
    {"WideFileFunctions_Construct", 0x1c0d9b, 32, 0xa79a414a, MemberAddress(&rz::WideFileFunctions::Construct)},
    {"WideDirectoryFunctions_Construct", 0x1c4830, 32, 0xeae23a7b, MemberAddress(&rz::WideDirectoryFunctions::Construct)},
    {"TryEnterCriticalSectionLoader_Construct", 0x1af60a, 32, 0x92d88c47, MemberAddress(&rz::TryEnterCriticalSectionLoader::Construct)},
    {"ShellExecuteLoader_Construct", 0x1afb32, 32, 0xabb3279b, MemberAddress(&rz::ShellExecuteLoader::Construct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
