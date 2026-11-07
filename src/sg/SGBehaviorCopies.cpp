#include "sg/SGBehaviorCopies.h"
#include "sg/cShapeRefNode.h"
#include "gz/cAbstractBehaviorTypeMT.h" // behavior_mt::DefaultAttribute
#include "sg/SGEventRecords.h"         // kCurrentDeviceEvent
#include "sg/cViewerRefNode.h"

#include <optional>

namespace nGZSceneGraph::behavior_copies {

using Map = nGZEvent::cEventAttributeMap;
using Attribute = nGZEvent::cEventAttribute;

Map *__cdecl ShapeRefRegistryFor(uint32_t id, const Attribute *attribute)
{
    return cShapeRefNodeBehaviorTraits::RegistryFor(id, attribute); // 0xa585a0 (src/sg/cShapeRefNode.cpp)
}

Map *__cdecl LightRefRegistryFor(uint32_t id, const Attribute *attribute)
{
    return OriginalFunction<Map *(__cdecl *)(uint32_t, const Attribute *), 0xa633d0>()(id, attribute);
}

Map *__cdecl ViewerRefRegistryFor(uint32_t id, const Attribute *attribute)
{
    return cViewerRefNodeBehaviorTraits::RegistryFor(id, attribute); // 0xa68db0 (src/sg/cViewerRefNode.cpp)
}

namespace {
std::optional<Map> gDisplayListRegistry;
} // namespace

// Lane VS. Built on first use. The original runs its holder 0xa9a020 first (on every call, a once byte 0x10f662c skipping
// it after the first): the base events (0x1464f0), then 0xca7ccbbc (record 0x106df00, the graphics system's
// CurrentDeviceEvent) with its last word 4 through this function, whose map (0x10f661c) is made by that call as a copy of
// the base registry (0x1480d0). (The holder's 3-byte object, kept in the static 0x10f6630, is read by nothing.)
Map *__cdecl DisplayListRegistryFor(uint32_t id, const Attribute *attribute)
{
    if (!gDisplayListRegistry) {
        nGZEvent::cAbstractBehaviorType::RegisterBaseEvents();
        const Attribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
        gDisplayListRegistry.emplace(*nGZEvent::cAbstractBehaviorType::RegistryFor(0, &defaults));
        Attribute record = sgevents::kCurrentDeviceEvent;
        record.mWord14 = 4;
        (*gDisplayListRegistry)[record.mId] = record;
    }
    if (id != 0) (*gDisplayListRegistry)[id] = *attribute;
    return &*gDisplayListRegistry;
}

void ResetDisplayListRegistryForTest() { gDisplayListRegistry.reset(); }

Map *__cdecl CameraEffectsRegistryFor(uint32_t id, const Attribute *attribute)
{
    return OriginalFunction<Map *(__cdecl *)(uint32_t, const Attribute *), 0x361720>()(id, attribute);
}

} // namespace nGZSceneGraph::behavior_copies
