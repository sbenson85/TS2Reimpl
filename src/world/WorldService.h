#pragma once
// The world DB as nTSWorld's helper module reaches it (lane RZ): the cIWorldDB face cached in game global 0x10cc45c, acquired on
// first use from the framework's service object (slot 5, service 0x4a037ee3, iid 0x49ff7d6f) and stored without keeping the
// reference (the acquiring local is released right after). Every helper inlines this; it has no RVA of its own.
#include "world/cWorldDB.h"

namespace worldsvc {

constexpr uint32_t kWorldDbCell = 0x10cc45c;

nTSWorld::cIWorldDB *CachedWorldDB();

} // namespace worldsvc
