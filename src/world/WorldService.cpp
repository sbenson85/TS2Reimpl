// The cached world DB of nTSWorld's helper module (lane RZ): src/world/WorldService.h.
#include "world/WorldService.h"

#include "game/game.h"
#include "rz/Singleton_20ddfb.h"

namespace worldsvc {

nTSWorld::cIWorldDB *CachedWorldDB()
{
    void **const cached = GameAddress<void *>(kWorldDbCell);
    if (*cached == nullptr) {
        ScopedRef ref{nullptr};
        if (void *const service = GetServiceObject()) {
            using Acquire = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, void **);
            VirtualCall<Acquire, 5>(service, nTSWorld::cWorldDB::kServiceId, nTSWorld::cWorldDB::kIid, &ref.mObject);
        }
        *cached = ref.mObject;
        ref.ReleaseObject();
    }
    return static_cast<nTSWorld::cIWorldDB *>(*cached);
}

} // namespace worldsvc
