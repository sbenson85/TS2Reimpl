#include "hook/hooks.h"
#include "gz/cGZCanvas.h"

// The GZ canvas's creator (the graphics COM director's class 0x445fabbb) is the switch: every slot of the canvas and of
// the three messages it sends (cGZCanvasMessage, cGZMouseMessage, cGZKeyMessage, made only by the canvas and by each
// other's factories) is reached through our vtables (in effect through callers; trap Hooks.nGZGraphic4_cGZCanvas_Create=1).
namespace {

const HookSpec kHooks[] = {
    {"nGZGraphic4_cGZCanvas_Create", 0x1495f0, 32, 0x8bc96c14, reinterpret_cast<void *>(&nGZGraphic4::cGZCanvas::Create)},
    // The event registry (lane PD): the template instantiation's function-local static, built on first use.
    {"nGZGraphic4_cGZCanvas_EventRegistry", 0x149ef0, 32, 0x322fe85c,
     reinterpret_cast<void *>(&nGZGraphic4::cGZCanvasBehaviorTraits::RegistryFor)},
    // The fixed-size pool allocator (lane RB): the messages' pools and cTSOverlayManager's (through 0xbda2e0 / 0xbda2f0).
    {"nGZGraphic4_cGZMessagePool_Allocate", 0x149040, 32, 0xa1ca2fed, MemberAddress(&nGZGraphic4::cGZMessagePool::Allocate)},
};
REGISTER_HOOKS(kHooks);

} // namespace
