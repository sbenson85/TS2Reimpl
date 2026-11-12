#include "hook/hooks.h"
#include "sg/cDiffusionManager.h"

// Standalone shape: the constructor's only caller is cLightingManager::Init 0x2d98c0 (the original's), so every
// diffusion manager is built by ours and carries our vtables; the slots, DoMessage, the destructor and the helpers are
// in effect through it.
namespace {

const HookSpec kHooks[] = {
    {"nTSSG_cDiffusionManager_cDiffusionManager", 0x3c4140, 32, 0x7d4a241c, &nTSSG::cDiffusionManager::Construct},
    {"nTSSG_cDiffusionManager_PortalOf", 0x3c5a60, 32, 0x2f516ef9, reinterpret_cast<void *>(&nTSSG::cDiffusionManager::PortalOf)},
    {"nTSSG_cDiffusionManager_LotValueBelow100", 0x3c5c30, 32, 0xd9e3bfd1, MemberAddress<&nTSSG::cDiffusionManager::LotValueBelow100>()},
};

REGISTER_HOOKS(kHooks);

} // namespace
