#pragma once
// Fakes for the services the cTSFamily members reach (tests/ts_family_services*.cpp): the singleton cached at 0x10cc288 and
// the objects it hands out, the persist system (cache 0x10faf6c), the framework (director static 0x10c9390) and the caches
// 0x10cc2a8 / 0x10cc2ac in front of singleton slots 23 / 3.
//
// A service fake (SObj) answers any slot set on it with a recording function of the right argument count: every call appends
// {evSvc, object id, slot, argument count, arguments...} to the world's event log (pointers to fakes become their ids,
// pointers into the family its offset, stack addresses a marker) and returns the slot's programmed value, or what a custom
// handler computes (for out-parameters).
#include "ts_family.h"

namespace tsfam::svc {

constexpr unsigned kSlots = 72;
enum : uint32_t { evSvc = 0x60, evFamSlot = 0x61 };

struct SObj;
using Custom = uint32_t (*)(SObj *self, uint32_t *args);

struct SObj {
    void **vt;
    uint32_t id;
    int32_t refs;
    uint32_t value[8]; // free words for custom handlers
    uint32_t ret[kSlots];
    Custom custom[kSlots];
    void *table[kSlots];

    void Reset(uint32_t newId);
    // Makes `slot` take `args` dword arguments and return `result` (or what `fn` computes).
    void Set(unsigned slot, unsigned args, uint32_t result = 0, Custom fn = nullptr);
    void Set(unsigned slot, unsigned args, const void *result) { Set(slot, args, uint32_t(uintptr_t(result))); }
};

// Every service fake: ids 200 + index.
enum Fake : unsigned {
    kApp, kLot, kActive, kHousehold, kLotManager, kLotObject, kRegistry, kNeighbors, kNeighborA, kNeighborB,
    kPersist, kCache, kFramework, kResource, kResourceB, kHelper, kHelperB, kStore, kFakeCount
};
SObj &S(Fake which);
uint32_t Norm(uint32_t value);

// Points the game globals at the fakes for the lifetime of the scope (restored afterwards).
struct Globals {
    explicit Globals(difftest::Context &ctx);
    ~Globals();
    difftest::Context &ctx;
    void *saved[8];
    int32_t savedGuard, savedAllocatorGuard;
    // The allocator service (0x10c95d4): the game's service vtable over the game's general allocator, so blocks our side
    // allocates (RZMemory) and the original frees through the service meet (as in the cTSNeighborGeneticDataTable tests).
    alignas(8) uint8_t allocatorService[16];
    bool builtAllocator = false;
};
// Resets every fake (no slots set, refs 1000), the caches 0x10cc2a8 / 0x10cc2ac (to `currentLot` / `household`, null to make
// the original getters ask the singleton) and the family slots the members call (6, 22, 23, 26, 29, 41, 42).
void Reset(void *currentLot, void *household);
uint32_t *Cache(uint32_t rva);

// Adds the fakes' counts to a blob.
void CaptureFakes(Blob &b);

// Runs `setup(rng)` then `run(original)` on two identically built worlds and compares returned word, state and events.
template <class Setup, class Run>
bool Pair2(difftest::Context &ctx, const char *what, uint32_t seed, uint32_t fill, Setup setup, Run run)
{
    bool ok = true;
    for (int route = 0; route < 2; ++route) {
        gRoute = Route(route);
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            gOurSide = side != 0;
            Build(W(), seed, fill);
            gTrapHits = 0;
            std::mt19937 rng(seed * 7919u + 5);
            setup(rng);
            const uint32_t returned = uint32_t(run(side == 0));
            blobs[side].push_back(returned);
            blobs[side].push_back(uint32_t(gTrapHits));
            Capture(blobs[side]);
            CaptureFakes(blobs[side]);
        }
        ok &= Same(ctx, blobs[0], blobs[1], what, RouteSeed(seed));
    }
    return ok;
}
} // namespace tsfam::svc
