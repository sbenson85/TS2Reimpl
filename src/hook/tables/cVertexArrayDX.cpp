#include "hook/hooks.h"
#include "gz/cVertexArrayDX.h"

namespace {

// The creators are the classes' switch (docs/standalone-shape.md): every vertex and index array the game makes is ours,
// so the game's vtables are never reached and every slot is in effect through its callers.
const HookSpec kHooks[] = {
    {"nGZGraphic4_NewStaticVertexComponentArrayDX", 0x165fa0, 32, 0xc1a31693,
     reinterpret_cast<void *>(&nGZGraphic4::cStaticVertexComponentArrayDX::New)},
    {"nGZGraphic4_NewDynamicVertexComponentArrayDX", 0x165ea0, 32, 0x3461c418,
     reinterpret_cast<void *>(&nGZGraphic4::cDynamicVertexComponentArrayDX::New)},
    {"nGZGraphic4_NewStaticVertexIndexArrayDX", 0x166000, 32, 0x4e0b63f9,
     reinterpret_cast<void *>(&nGZGraphic4::cStaticVertexIndexArrayDX::New)},
    {"nGZGraphic4_NewDynamicVertexIndexArrayDX", 0x165f20, 32, 0xce02e1e2,
     reinterpret_cast<void *>(&nGZGraphic4::cDynamicVertexIndexArrayDX::New)},
};
REGISTER_HOOKS(kHooks);

} // namespace
