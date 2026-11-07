#pragma once
// Stand-ins for the behaviour registries (lane PD). The graphics classes' tests plant a map over the original's registry static
// (the original's first-use build cannot run in the harness) and compare the class's slots over it. The registry functions are
// ours now, so a StandIn keeps both sides on the planted map alike: the stand returns the map at `Map` (a game RVA) and stores
// `*attribute` under a non-zero id, as the original does.
//
//   shape::StandIn registryStand{0x149ef0, &cGZCanvasBehaviorTraits::RegistryFor, &pdfx::StandRegistry<0x10c9574>};
#include "code_patch.h"

#include "game/game.h"
#include "gz/cGZEventReflector.h"

#include <cstdint>

namespace pdfx {

template <uint32_t Map>
nGZEvent::cEventAttributeMap *__cdecl StandRegistry(uint32_t id, const nGZEvent::cEventAttribute *attribute)
{
    auto *const map = GameAddress<nGZEvent::cEventAttributeMap>(Map);
    if (id != 0) (*map)[id] = *attribute;
    return map;
}

// The base registry (0x1485f0), planted at 0x10c94f4.
inline constexpr uint32_t kBaseRegistryMap = 0x10c94f4;

} // namespace pdfx
