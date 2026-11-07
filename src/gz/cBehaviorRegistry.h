#pragma once
// The per-class event registry of a cAbstractBehaviorTypeT instantiation (lane PD): the same function-local static in each
// graphics class (cCanvasW32 0x156a70, cGZCanvas 0x149ef0, cGZGraphicSystem 0x148440, cGraphicSystemDX 0x15a250;
// cDraw2D's 0x1516f0 is written out in src/gz/cDraw2D.cpp). Built on first use: the base events are stored first (the
// holder's constructor calls 0x1464f0), the map is a copy of the base registry as it then stands, and the class's own
// events are stored in it. A non-zero `id` then stores `*attribute` under it. The original keeps a small leaked holder
// object beside the map that exists only to run that construction; ours does not.
#include "gz/cGZEventReflector.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace nGZEvent {

// One of a class's own events: the record {id, name, 1, word0C, word10, 8}.
struct ClassEvent {
    uint32_t id;
    const char *name;
    uint32_t word0C;
    uint32_t word10;
};

inline cEventAttributeMap *ClassRegistryFor(std::optional<cEventAttributeMap> &registry, const ClassEvent *events,
                                            size_t count, uint32_t id, const cEventAttribute *attribute)
{
    if (!registry) {
        cAbstractBehaviorType::RegisterBaseEvents();
        registry.emplace(*cAbstractBehaviorType::Registry());
        for (size_t i = 0; i < count; ++i)
            (*registry)[events[i].id] = {events[i].id, events[i].name, 1, events[i].word0C, events[i].word10, 8};
    }
    if (id != 0) (*registry)[id] = *attribute;
    return &*registry;
}

} // namespace nGZEvent
