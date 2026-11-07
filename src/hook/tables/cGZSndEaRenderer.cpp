#include "hook/hooks.h"
#include "gz/cGZSndEaRenderer.h"

namespace {

// The factory only (standalone shape): every renderer is ours, so its slots are in effect through this hook.
const HookSpec kHooks[] = {
    {"cGZSndEaRenderer_Create", 0xb12be6, 25, 0x952428da, reinterpret_cast<void *>(&cGZSndEaRenderer::Create)},
};
REGISTER_HOOKS(kHooks);

} // namespace
