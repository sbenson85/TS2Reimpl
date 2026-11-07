#pragma once
// Shared fixture for the cTSCASBodyPartSystem differential tests (tests/cas_body_part_system*.cpp): a whole system object
// buffer (0x180 bytes, the interface subobject at +0x1c) with canaries around it, a modifier stack and a redo stack built
// on the heap from plain specs, and a fake copy of the interface's vtable whose slots record their calls.
#include "difftest.h"
#include "cas/cTSCASBodyPartSystem.h"
#include "game/game.h"
#include "simantics_autonomy_records.h" // GameAllocatorFixture

#include <windows.h>

#include <array>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace bpt {

using Iface = nTSCASCore::BodyPartSystemIface;
using nTSCASCore::BodyPartEntry;
using nTSCASCore::BodyPartEntryVector;

constexpr int kObject = 0x180;      // whole system object buffer
constexpr int kIfaceOffset = 0x1c;  // the iface subobject inside it
constexpr int kSlots = 89;

// ---- The game's allocator ------------------------------------------------------------------------------------------

// While alive, the game's general allocator (0x10cb098) is built and the image's allocator service cache (0x10c95d4, guard
// 0x10c95d8) holds an object with the game's own allocator-service vtable (0xda3cd4), so the vectors the records allocate
// through the service can be allocated, copied and freed by the original's code.
struct Env {
    GameAllocatorFixture generalAllocator;
    void **cache;
    int *guard;
    void *savedCache;
    int savedGuard;
    alignas(8) uint8_t object[16] = {};
    static Env *&Current()
    {
        static Env *current = nullptr;
        return current;
    }
    explicit Env(difftest::Context &ctx) : cache(ctx.image.At<void *>(0x10c95d4)), guard(ctx.image.At<int>(0x10c95d8))
    {
        Current() = this;
        savedCache = *cache;
        savedGuard = *guard;
        const void *vtable = ctx.image.At<void>(0xda3cd4);
        std::memcpy(object, &vtable, sizeof(vtable));
        *cache = object;
        *guard = INT32_MIN;
    }
    Env(const Env &) = delete;
    ~Env()
    {
        Current() = nullptr;
        *cache = savedCache;
        *guard = savedGuard;
    }
};

// A block from the game's allocator service (slot 3, byte count), what the vectors of records are allocated with.
inline void *ServiceAlloc(size_t bytes)
{
    void *service = Env::Current()->object;
    auto alloc = reinterpret_cast<void *(__thiscall *)(void *, uint32_t)>((*reinterpret_cast<void ***>(service))[3]);
    return alloc(service, static_cast<uint32_t>(bytes));
}

// ---- Recorded calls ------------------------------------------------------------------------------------------------

using Event = std::array<uint32_t, 12>; // id (a slot number, or the RVA of a faked callee), the receiver, then the arguments
extern std::vector<Event> gEvents;
extern const uint8_t *gBase; // the side whose object the fakes record offsets from (set by Compare)

inline uint32_t Rel(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(gBase));
}

// While alive, the first 5 bytes of an original function in the mapped image jump to a fake with the same calling
// convention (the callee runs on both sides of a test, so a recording fake stands in for its body; see
// tests/rz_allocator_service.cpp AtexitRedirect for the first use of the pattern).
struct Redirect {
    uint8_t *site;
    uint8_t saved[5];
    Redirect(difftest::Context &ctx, uint32_t rva, const void *target) : site(ctx.image.At<uint8_t>(rva))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    Redirect(const Redirect &) = delete;
    ~Redirect() { std::memcpy(site, saved, 5); }
};

inline uint32_t FloatBits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// ---- Specs of the records ------------------------------------------------------------------------------------------

struct Spec {
    int32_t kind = 0;
    int16_t a = 0, b = 0;
    int32_t value = 0;
    float weight = 0, extra = 0;
    std::string name;
    int items = 0; // number of 0x44-byte items
};

// ---- A side (original or ours) -------------------------------------------------------------------------------------

struct World {
    uint8_t front[32];
    uint8_t full[kObject];
    uint8_t back[32];
    BodyPartEntryVector *stack = nullptr; // also at iface +0x1c
    BodyPartEntryVector *redo = nullptr;  // also at iface +0x18

    Iface *S() { return reinterpret_cast<Iface *>(full + kIfaceOffset); }
    uint8_t *Base() { return full; }
};

struct Scenario {
    uint8_t bytes[kObject]; // the random object contents
    std::vector<Spec> stack, redo;
    int stackSpare = 0, redoSpare = 0;
};

// Builds one stack on the heap from specs using the original record constructor and copy constructor.
BodyPartEntryVector *BuildStack(difftest::Context &ctx, const std::vector<Spec> &specs, int spare);

// Fills a world from a scenario. `vtable` goes into the iface's vtable slot.
void Setup(difftest::Context &ctx, World &w, const Scenario &sc, const void *vtable);

// A serial form of a stack: per record its fields and name, then the size and spare capacity.
std::vector<uint8_t> Snapshot(const BodyPartEntryVector *v);

// The object bytes with the pointer fields that differ between sides zeroed.
std::vector<uint8_t> Normalised(World &w);

bool CanariesIntact(const World &w);

// Random specs: kinds, names from a small pool (so queries match), weights including zero, negative and NaN.
Spec RandomSpec(std::mt19937 &rng);
std::vector<Spec> RandomSpecs(std::mt19937 &rng, int maxCount);
const char *RandomName(std::mt19937 &rng);

Scenario RandomScenario(std::mt19937 &rng);

// The fake vtable: a copy of the original's with the slots the test fakes replaced.
struct VTable {
    const void *slots[kSlots];
};
VTable CopyVTable(const difftest::Context &ctx);

// Runs the operation on a fresh world per side and compares result, stacks, object bytes, recorded calls, canaries.
// run(world, true) runs the original, run(world, false) ours.
template <typename Run>
void Compare(difftest::Context &ctx, const char *name, const Scenario &sc, const VTable &vt, Run run)
{
    static World a, b;
    Setup(ctx, a, sc, &vt);
    Setup(ctx, b, sc, &vt);
    gEvents.clear();
    gBase = a.full;
    const uint64_t ra = run(a, true);
    const std::vector<Event> ea = gEvents;
    gEvents.clear();
    gBase = b.full;
    const uint64_t rb = run(b, false);
    const std::vector<Event> eb = gEvents;
    ++ctx.cases;
    if (ra != rb) {
        ctx.Fail("%s: result original %llx, ours %llx", name, static_cast<unsigned long long>(ra),
                 static_cast<unsigned long long>(rb));
    }
    if (ea != eb) {
        size_t k = 0;
        while (k < ea.size() && k < eb.size() && ea[k] == eb[k]) ++k;
        if (k < ea.size() && k < eb.size()) {
            ctx.Fail("%s: calls made differ at %zu: original id %x self %x args %x %x %x, ours id %x self %x args %x %x %x", name, k,
                     ea[k][0], ea[k][1], ea[k][2], ea[k][3], ea[k][4], eb[k][0], eb[k][1], eb[k][2], eb[k][3], eb[k][4]);
        } else {
            ctx.Fail("%s: calls made differ (original %zu, ours %zu)", name, ea.size(), eb.size());
        }
    }
    if (Snapshot(a.stack) != Snapshot(b.stack)) ctx.Fail("%s: modifier stack differs", name);
    if (Snapshot(a.redo) != Snapshot(b.redo)) ctx.Fail("%s: redo stack differs", name);
    if (Normalised(a) != Normalised(b)) ctx.Fail("%s: object state differs", name);
    if (!CanariesIntact(a) || !CanariesIntact(b)) ctx.Fail("%s: canaries overwritten", name);
}

} // namespace bpt
