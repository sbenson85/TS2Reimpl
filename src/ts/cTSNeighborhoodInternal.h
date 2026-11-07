#pragma once
// Helpers shared by the cTSNeighborhood translation units that lane CQ wrote (lot files, lot loading, validation, import).
#include "game/game.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/ComFramework.h"
#include "ifaces/cIGZPersistResource.h"
#include "ifaces/cITSGameStateController.h"
#include "ifaces/cITSPersistSystem.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"

#include <cstdint>

namespace nbhood {

constexpr uint32_t kPersistContextBusiness = 0xd0590041;
constexpr uint32_t kBusinessRecordType = 0x104f6a6e;
constexpr uint32_t kPersistSystemCache = 0x10faf6c;
constexpr uint32_t kPersistSystemIdA = 0x056b906e;
constexpr uint32_t kPersistSystemIdB = 0x9af5e924;

// A counted pointer held by a local of the original: released when it goes out of scope.
struct Ref {
    void *object = nullptr;
    Ref() = default;
    Ref(const Ref &) = delete;
    Ref &operator=(const Ref &) = delete;
    ~Ref() noexcept(false)
    {
        if (object) As<cIGZUnknown>(object)->Release();
    }
};

// The 16-byte resource key of a lot's business record: {type, group, lot number, 0}.
struct BusinessKey {
    uint32_t type, group, instance, zero;
};

inline ifaces::cITSPersistSystem *PersistSystem()
{
    return As<ifaces::cITSPersistSystem>(CachedServiceGetter<kPersistSystemCache, kPersistSystemIdA, kPersistSystemIdB>());
}

inline ifaces::cITSGameStateController *StateController() { return As<ifaces::cITSGameStateController>(GetCachedSlot23()); }

inline uint32_t GroupOf(void *persistFace) { return As<ifaces::cIGZPersistResource>(persistFace)->GetGroup(); }

} // namespace nbhood
