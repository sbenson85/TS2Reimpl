#include "hook/hooks.h"
#include "gz/cRZCursor.h"

namespace {

const HookSpec kHooks[] = {
    {"cRZCursor_Create", 0x9b3460, 24, 0x0fbabb5a, reinterpret_cast<void *>(&cRZCursorAdapters::Create)},
    {"cRZCursor_Construct", 0x9d00ad, 32, 0x49d9c4e7, reinterpret_cast<void *>(&cRZCursorAdapters::Construct)},
    {"cRZCursorManager_Construct", 0x9d0158, 32, 0x5fb29cde,
     reinterpret_cast<void *>(&cRZCursorAdapters::ConstructManager)},
    {"AcquireGraphicSystem", 0x7ae7f4, 32, 0x2154a6c8, reinterpret_cast<void *>(&cRZCursorAdapters::AcquireGraphicSystem)},
    {"GetGraphicSystem", 0x9c5a05, 32, 0x2a5129c9, reinterpret_cast<void *>(&GetGraphicSystem)},
    {"ColorFormatBits", 0x5ba211, 32, 0x89a7de45, reinterpret_cast<void *>(&ColorFormatBits)},
};
REGISTER_HOOKS(kHooks);

} // namespace
