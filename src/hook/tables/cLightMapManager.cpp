#include "hook/hooks.h"
#include "sg/cLightMapManager.h"

// Standalone shape (docs/standalone-shape.md): the factory, which the COM director's table holds by address, is the only
// constructor call, so every light map manager carries our vtables; the slots, DoMessage, the destructor and the helpers
// are in effect through it.
namespace {

const HookSpec kHooks[] = {
    {"cLightMapManager_Create", 0x2c73f0, 32, 0xe2e698fd, &nTSSG::cLightMapManager::Create},
};

REGISTER_HOOKS(kHooks);

} // namespace
