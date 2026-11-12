#include "hook/hooks.h"
#include "sg/cLightRigManager.h"

namespace {

// The light rig manager's factory (the COM director's entry for class 0x696d3daf, its only way in), so every manager is
// ours and its slots, constructor and destructors are in effect through it; and the rig record's constructor (our
// compiler's, by its /alternatename name in src/sg/cLightRigManager.cpp), which the unreplaced rig file parser 0x2c21a0
// calls on the map nodes it allocates.
const HookSpec kHooks[] = {
    {"nTSSG_cLightRigManager_Create", 0x2c2af0, 32, 0x5c2e4690, reinterpret_cast<void *>(&nTSSG::cLightRigManager::Create)},
    {"nTSSG_LightRig_Construct", 0x2c19b0, 32, 0x921cadd9, const_cast<uint8_t *>(&LightRig_Constructor)},
};
REGISTER_HOOKS(kHooks);

} // namespace
