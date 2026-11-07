// cDeviceDX's event attribute registry 0x16a170 (lane RJ): the behaviour template's registry, built on first use.
#include "gz/cDeviceDX.h"

#include <optional>

namespace nGZGraphic4 {

namespace {

std::optional<nGZEvent::cEventAttributeMap> gEventRegistry;

} // namespace

// 0x16a170 (lane RJ).
nGZEvent::cEventAttributeMap *__cdecl cDeviceDX::EventRegistryFor(uint32_t id, const nGZEvent::cEventAttribute *attribute)
{
    if (!gEventRegistry) {
        // The shared 0x1464f0 registers the base events in the base registry first; the copy then holds them.
        nGZEvent::cAbstractBehaviorType::RegisterBaseEvents();
        gEventRegistry.emplace(*nGZEvent::cAbstractBehaviorType::Registry());
        nGZEvent::cEventAttributeMap &map = *gEventRegistry;
        map[cDeviceEvent::kEventId] = {cDeviceEvent::kEventId, "DeviceEvent", 1, cDeviceEvent::kType, 2, 8};
        map[cDeviceRenderSceneEvent::kEventId] = {cDeviceRenderSceneEvent::kEventId, "DeviceRenderSceneEvent", 1,
                                                  cDeviceRenderSceneEvent::kType, 2, 8};
        map[kCanvasEvent] = {kCanvasEvent, "CanvasEvent", 1, 0x7b637514, 2, 4};
        map[kResizeEvent] = {kResizeEvent, "CanvasSizeMoveEvent", 1, 0xfb6dec65, 2, 4};
    }
    if (id != 0) (*gEventRegistry)[id] = *attribute;
    return &*gEventRegistry;
}

void cDeviceDX::ResetEventRegistryForTest() { gEventRegistry.reset(); }

} // namespace nGZGraphic4
