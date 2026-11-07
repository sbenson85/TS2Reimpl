#include "hook/hooks.h"
#include "gz/GraphicServices.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZGraphic4_GetDraw2DCached", 0x18c000, 32, 0x78c2a75f, nGZGraphic4::GetDraw2DCached},
};
REGISTER_HOOKS(kHooks);

} // namespace
