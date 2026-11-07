#include "hook/hooks.h"
#include "gz/cGZDrawContextRenderToHardware.h"

namespace {

const HookSpec kHooks[] = {
    {"LegacyCalculateUIScale", 0x8b8390, 32, 0xa75bdf34, LegacyCalculateUIScale},
};
REGISTER_HOOKS(kHooks);

} // namespace
