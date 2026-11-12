#include "hook/hooks.h"
#include "ts/cTSOverlayManager.h"

// Standalone shape (docs/standalone-shape.md): the two factories, which the COM director's table holds by address, are the
// only ways in: cTSEffectsManager::Init (ours) makes its managers with our constructor, and overlays and decals are made
// only by a manager's slots. With both hooks every object carries our vtables; the rest needs no adapters.
namespace {

const HookSpec kHooks[] = {
    {"cTSOverlayManager_Create", 0xbdaf50, 32, 0x89da6802, &cTSOverlayManager::Create},
    {"cTSTestMap_Create", 0xbd9700, 32, 0xe9aa5b98, &cTSTestMap::Create},
};

REGISTER_HOOKS(kHooks);

} // namespace
