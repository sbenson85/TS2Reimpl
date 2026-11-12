// nTSSG::cShadowManager (scenegraph LG6c, LG6d): the factory, and the two drawing helpers other code calls (the camera
// fit and the root bone); everything else is in effect through them (src/sg/cShadowManager.h).
#include "hook/hooks.h"
#include "sg/cShadowManager.h"

namespace {

const HookSpec kHooks[] = {
    {"cShadowManager_Create", 0x2e7ba0, 32, 0xa3054eae, &nTSSG::cShadowManager::Create},
    {"FitShadowCamera", 0x3ad080, 32, 0x16a5183f, &nTSSG::FitShadowCamera},
    {"FindRootBone", 0x3ace40, 32, 0x38dbeac0, &nTSSG::FindRootBone},
};
REGISTER_HOOKS(kHooks);

} // namespace
