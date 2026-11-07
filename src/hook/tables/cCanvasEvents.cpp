#include "hook/hooks.h"
#include "gz/cCanvasEvents.h"

// The window service's events: our window code builds ours (our vtables); the game's vtables stay in use where other
// classes' code copies a received event onto its stack, so each class's own slot bodies are hooked here. The folded
// bodies (shared with other event classes) are not.
namespace {

using nGZGraphic4::cCanvasEventsAdapters;

const HookSpec kHooks[] = {
    {"nGZGraphic4_cCanvasEvent_vf3", 0x1576d0, 6, 0x0aed1719, reinterpret_cast<void *>(&cCanvasEventsAdapters::CanvasEventType)},
    {"nGZGraphic4_cCanvasEvent_vf6", 0x155900, 19, 0x7b47759f, reinterpret_cast<void *>(&cCanvasEventsAdapters::CanvasCloneInto)},
    {"nGZGraphic4_cCanvasSizeMoveEvent_vf0", 0x154d10, 32, 0x4332b93b, reinterpret_cast<void *>(&cCanvasEventsAdapters::SizeMoveDeletingDestructor)},
    {"nGZGraphic4_cCanvasSizeMoveEvent_vf1", 0x155090, 21, 0xe4044875, reinterpret_cast<void *>(&cCanvasEventsAdapters::SizeMoveAssign)},
    {"nGZGraphic4_cCanvasSizeMoveEvent_vf3", 0x1576e0, 6, 0x3f0ac509, reinterpret_cast<void *>(&cCanvasEventsAdapters::SizeMoveEventType)},
    {"nGZGraphic4_cCanvasSizeMoveEvent_vf4", 0x1559d0, 6, 0x15a67ff2, reinterpret_cast<void *>(&cCanvasEventsAdapters::SizeMoveSize)},
    {"nGZGraphic4_cCanvasSizeMoveEvent_vf6", 0x155920, 27, 0x09095947, reinterpret_cast<void *>(&cCanvasEventsAdapters::SizeMoveCloneInto)},
    {"nGZGraphic4_cMouseEvent_vf0", 0x154ee0, 32, 0x8f581547, reinterpret_cast<void *>(&cCanvasEventsAdapters::MouseDeletingDestructor)},
    {"nGZGraphic4_cMouseEvent_vf1", 0x1550d0, 32, 0x98e100fa, reinterpret_cast<void *>(&cCanvasEventsAdapters::MouseAssign)},
    {"nGZGraphic4_cMouseEvent_vf3", 0x157700, 6, 0xd65b867c, reinterpret_cast<void *>(&cCanvasEventsAdapters::MouseEventType)},
    {"nGZGraphic4_cMouseEvent_vf6", 0x155970, 32, 0xe7b77781, reinterpret_cast<void *>(&cCanvasEventsAdapters::MouseCloneInto)},
    {"nGZGraphic4_cKeyEvent_vf3", 0x1576f0, 6, 0x40be8d63, reinterpret_cast<void *>(&cCanvasEventsAdapters::KeyEventType)},
    {"nGZGraphic4_cKeyEvent_vf6", 0x155940, 32, 0xfe6e5eb6, reinterpret_cast<void *>(&cCanvasEventsAdapters::KeyCloneInto)},
};
REGISTER_HOOKS(kHooks);

} // namespace
