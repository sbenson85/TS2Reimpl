#include "hook/hooks.h"
#include "gz/GimexFshDecode.h"

namespace {

const HookSpec kHooks[] = {
    {"GimexFsh_ExtractRecord", 0x887967, 32, 0x9bec315e, reinterpret_cast<void *>(&gimex::fsh::ExtractRecord)},
    {"GimexFsh_ParseEntry", 0x8871b5, 32, 0x94d45f68, reinterpret_cast<void *>(&gimex::fsh::ParseEntry)},
    {"GimexFsh_BuildDirectory", 0x885f9b, 32, 0xf4c2c1ab, reinterpret_cast<void *>(&gimex::fsh::BuildDirectory)},
    {"GimexFsh_ReadDirectoryStream", 0x887a06, 32, 0xb5760523, reinterpret_cast<void *>(&gimex::fsh::ReadDirectoryStream)},
};
REGISTER_HOOKS(kHooks);

} // namespace
