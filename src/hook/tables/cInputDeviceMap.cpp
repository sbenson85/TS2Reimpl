#include "hook/hooks.h"
#include "gz/cInputDeviceMap.h"

namespace {

const HookSpec kHooks[] = {
    {"nGZEvent_cInputDeviceMap_Create", 0x1a8d95, 22, 0xdee30fe3,
     reinterpret_cast<void *>(&nGZEvent::cInputDeviceMap::Create)},
    {"nGZEvent_cInputDeviceMap_Construct", 0x1a8af6, 32, 0xc44defbf,
     reinterpret_cast<void *>(&nGZEvent::cInputDeviceMapAdapters::Construct)},
};
REGISTER_HOOKS(kHooks);

} // namespace
