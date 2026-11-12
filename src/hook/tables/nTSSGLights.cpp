#include "hook/hooks.h"
#include "sg/nTSSGLights.h"

namespace {

// The six nTSSG light constructors (our compiler's, by their /alternatename names in src/sg/nTSSGLights.cpp): every one
// of these lights is ours, whoever makes it (the lerp light set calls ours directly; the lighting manager's 0x2ccf10,
// 0x2cfd60 and 0x2d5430 through these hooks), so the game's vtables of the six classes are stored by nothing that runs.
const HookSpec kHooks[] = {
    {"nTSSG_cAmbientFromLightMapLight_Construct", 0x3c7d30, 32, 0x3dbfeab8, const_cast<uint8_t *>(&cAmbientFromLightMapLight_Constructor)},
    {"nTSSG_cHeightMapShadowLight_Construct", 0x3c7fd0, 32, 0x45fb16b6, const_cast<uint8_t *>(&cHeightMapShadowLight_Constructor)},
    {"nTSSG_cHeightMapBinaryShadowLight_Construct", 0x3c7e80, 32, 0x659ae624, const_cast<uint8_t *>(&cHeightMapBinaryShadowLight_Constructor)},
    {"nTSSG_cHalfspacePointLight_Construct", 0x3c7de0, 32, 0xc005475c, const_cast<uint8_t *>(&cHalfspacePointLight_Constructor)},
    {"nTSSG_cShadeLight_Construct", 0x3c80b0, 32, 0x011d901e, const_cast<uint8_t *>(&cShadeLight_Constructor)},
    {"nTSSG_cTerrainShadeLight_Construct", 0x3c8120, 32, 0x3feafefc, const_cast<uint8_t *>(&cTerrainShadeLight_Constructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
