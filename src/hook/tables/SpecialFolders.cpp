#include "hook/hooks.h"
#include "app/SpecialFolders.h"

namespace {

const HookSpec kHooks[] = {
    {"GetSpecialFolderPath", 0x1c50d6, 32, 0x13b1812a, reinterpret_cast<void *>(&folders::GetSpecialFolderPath)},
    {"EnsureDirectory", 0x1c5031, 22, 0xeb5e4fb4, reinterpret_cast<void *>(&folders::EnsureDirectory)},
};
REGISTER_HOOKS(kHooks);

} // namespace
