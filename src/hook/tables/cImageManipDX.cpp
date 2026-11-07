#include "hook/hooks.h"
#include "gz/cImageManipDX.h"

namespace {

// The getter is the class's switch (docs/standalone-shape.md): cGraphicSystemDX's slot 20 hands out our instance, so the
// game's vtable 0xd9e844 is never reached and every slot is in effect through its callers (status "in_effect":
// "callers", trap Hooks.nGZGraphic4_GetImageManip=1).
const HookSpec kHooks[] = {
    {"nGZGraphic4_GetImageManip", 0x16c0c0, 32, 0x9857bc30, reinterpret_cast<void *>(&nGZGraphic4::cImageManipDX::Instance)},
};
REGISTER_HOOKS(kHooks);

} // namespace
