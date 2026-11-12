// nTSSG::cShadow (scenegraph LG7d): the factory, and the helpers original code still calls (ShadowFootprint and
// FindSubsetMaterial from other callers); everything else is in effect through them (src/sg/cShadow.h).
#include "hook/hooks.h"
#include "sg/cShadow.h"

namespace {

const HookSpec kHooks[] = {
    {"cShadow_Create", 0x355700, 32, 0x7af9041c, &nTSSG::cShadow::Create},
    {"ShadowFootprint", 0x3aced0, 32, 0x366dd842, &nTSSG::ShadowFootprint},
    {"FindSubsetMaterial", 0x3acd30, 32, 0x264a0c0e, &nTSSG::FindSubsetMaterial},
};
REGISTER_HOOKS(kHooks);

} // namespace
