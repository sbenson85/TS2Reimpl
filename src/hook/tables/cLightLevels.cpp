#include "hook/hooks.h"
#include "sg/cLightLevels.h"

namespace {

// The light level of the game-derived lights (src/sg/cLightLevels.h, lane TL): the two cLightT constructors the game's
// own classes (cStandardLightBase 0xa46250 and so the nTSSG lights built on the standard lights; nTSSG::cShadeLight and
// cTerrainShadeLight) still call as base parts with the most-derived flag 0. Like cSGResource's 0xa3c270 (lane OE) the hook
// is our compiler's own constructor of the template. The two are twins (the same 32 bytes begin them), so the hook
// checks them by their addresses.
const HookSpec kHooks[] = {
    {"cLightT_StandardLight_Construct", 0xa45a40, 32, 0x39346336, const_cast<uint8_t *>(&cLightT_StandardLight_Constructor)},
    {"cLightT_ShadeLight_Construct", 0x3c7cb0, 32, 0x39346336, const_cast<uint8_t *>(&cLightT_ShadeLight_Constructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
