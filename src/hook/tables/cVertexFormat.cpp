#include "hook/hooks.h"
#include "gz/cVertexFormat.h"

namespace {

// The getters are the managers' switch (docs/standalone-shape.md): the graphic system's slots 17 / 18 jump to them, so
// every format and declaration the game uses is ours and every slot is in effect through its callers.
const HookSpec kHooks[] = {
    {"nGZGraphic4_cVertexFormatsManager_Instance", 0x16db40, 32, 0x568c2582,
     reinterpret_cast<void *>(&nGZGraphic4::cVertexFormatsManager::Instance)},
    {"nGZGraphic4_cVertexDeclarationsManagerDX_Instance", 0x16e760, 32, 0x5160c039,
     reinterpret_cast<void *>(&nGZGraphic4::cVertexDeclarationsManagerDX::Instance)},
    {"nGZGraphic4_VertexUsageName", 0x18dbe0, 32, 0x63cc0eba, reinterpret_cast<void *>(&nGZGraphic4::VertexUsageName)},
};
REGISTER_HOOKS(kHooks);

} // namespace
