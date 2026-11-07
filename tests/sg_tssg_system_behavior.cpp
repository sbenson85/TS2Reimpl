// Differential tests for nTSSG::cTSSGSystem's event behaviour type slots 12-14 (src/sg/cTSSGSystemBehavior.cpp):
// AttributeCount, AttributeIds and AttributeInfo. They do not read the object.
//
// The registry getter 0x253de0 (a function-local static, thread-safe init the harness has no CRT for) is stood in over
// the image: it logs its arguments and the probe entry and returns a test std::map from id to a 24-byte entry, of 0..6
// ids or of 1100 (the id vector then crosses the 4 KB aligned-allocation size). AttributeIds' objects are the game's
// (its vtables; the original's AddRef / Release / copy run on both sides): each of the three results is described by
// its vtable, reference count, the id vector (contents, count) and its position, then released.
#include "code_patch.h"
#include "difftest.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "game/vtable.h"
#include "sg/cTSSGSystem.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <random>
#include <vector>

namespace {

using namespace svcfx;
using nTSSG::cTSSGSystem;

enum : uint32_t { evStand = 0x81 };

struct Entry {
    uint32_t words[6];
};
std::map<uint32_t, Entry> gMap;

void *__cdecl StandRegistry(uint32_t id, const uint32_t *probe)
{
    Ev(evStand), Ev(0x253de0), Ev(id);
    for (int i = 0; i < 6; ++i) Ev(probe[i]);
    return &gMap;
}

struct Fixture {
    // The registry is ours now (lane SM): the stand-in covers the image's function and ours, so both sides read the test map.
    shape::StandIn registry{0x253de0, &nTSSG::cTSSGSystemBehaviorTraits::RegistryFor, &StandRegistry};
};

void Fill(std::mt19937 &gen)
{
    gMap.clear();
    const unsigned count = gen() % 8 == 0 ? 1100 : gen() % 7;
    while (gMap.size() < count) {
        Entry e;
        for (uint32_t &w : e.words) w = gen();
        gMap[gen() % 3 ? gen() % 64 : gen()] = e;
    }
}

uint8_t gObject[0x3b8];
cTSSGSystem *Ours() { return reinterpret_cast<cTSSGSystem *>(gObject); }

// One of AttributeIds' results: vtable, count, its id vector's contents and count, the position; then released.
void Describe(Blob &b, void *object, const void *ids)
{
    if (!object) return b.push_back(0);
    const auto *w = static_cast<const uint32_t *>(object);
    b.push_back(w[0]);
    b.push_back(w[1]);
    const auto *vec = reinterpret_cast<const uint32_t *>(uintptr_t(w[2]));
    b.push_back(vec == ids);
    if (vec) {
        const auto *first = reinterpret_cast<const uint32_t *>(uintptr_t(vec[1]));
        const auto *last = reinterpret_cast<const uint32_t *>(uintptr_t(vec[2]));
        const auto *cap = reinterpret_cast<const uint32_t *>(uintptr_t(vec[3]));
        b.push_back(uint32_t(last - first));
        b.push_back(cap >= last);
        for (const uint32_t *p = first; p != last; ++p) b.push_back(*p);
        b.push_back(vec[4]);
        b.push_back(uint32_t(int32_t(reinterpret_cast<const uint32_t *>(uintptr_t(w[3])) - first)));
    }
}

} // namespace

// 0x24be80 AttributeCount: registries of 0..6 and 1100 ids.
DIFF_TEST(sg_tssg_system_attribute_count, 0x24be80)
{
    Fixture fixture;
    auto original = ctx.Original<uint32_t(__thiscall *)(void *)>(0x24be80);
    for (uint32_t seed = 0; seed < 200; ++seed) {
        std::mt19937 gen(seed + 0x24be80u);
        Fill(gen);
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            ResetAll();
            ctx.Mark(side ? "ours" : "original");
            const uint32_t n = side == 0 ? original(gObject) : Ours()->AttributeCount();
            outcome[side] = gEv;
            outcome[side].push_back(n);
        }
        Same(ctx, outcome[0], outcome[1], "attribute_count", seed);
    }
}

// 0x247c10 AttributeIds: the same registries; the three results described and released.
DIFF_TEST(sg_tssg_system_attribute_ids, 0x247c10)
{
    Fixture fixture;
    auto original = ctx.Original<void *(__thiscall *)(void *, void *)>(0x247c10);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 gen(seed + 0x247c10u);
        Fill(gen);
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            ResetAll();
            void *out[4] = {nullptr, nullptr, nullptr, reinterpret_cast<void *>(uintptr_t(0x0de1c0de))};
            ctx.Mark(side ? "ours" : "original");
            void *r = side == 0 ? original(gObject, out) : Ours()->AttributeIds(out);
            Blob b = gEv;
            b.push_back(r == out);
            b.push_back(P(out[3]));
            const void *ids = out[1] ? reinterpret_cast<void *>(uintptr_t(static_cast<uint32_t *>(out[1])[2])) : nullptr;
            for (int i = 0; i < 3; ++i) Describe(b, out[i], ids);
            for (int i = 0; i < 3; ++i)
                if (out[i]) VirtualCall<uint32_t(__thiscall *)(void *), 2>(out[i]);
            outcome[side] = b;
        }
        Same(ctx, outcome[0], outcome[1], "attribute_ids", seed);
    }
}

// 0x247e80 AttributeInfo: an id the registry has (first, middle, last), one it lacks (below, between, above all), 0 and
// 0xffffffff; the 24 bytes out (with canaries around them) and the returned pointer.
DIFF_TEST(sg_tssg_system_attribute_info, 0x247e80)
{
    Fixture fixture;
    auto original = ctx.Original<void *(__thiscall *)(void *, void *, uint32_t)>(0x247e80);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 gen(seed + 0x247e80u);
        Fill(gen);
        uint32_t id = gen();
        if (!gMap.empty() && gen() % 2) {
            auto it = gMap.begin();
            std::advance(it, gen() % gMap.size());
            id = it->first;
            if (gen() % 3 == 0) id += 1;
        } else if (gen() % 2) {
            id = gen() % 2 ? 0 : 0xffffffffu;
        }
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            ResetAll();
            uint32_t out[8];
            for (uint32_t &w : out) w = 0xc0c0c0c0u;
            ctx.Mark(side ? "ours" : "original");
            void *r = side == 0 ? original(gObject, out + 1, id) : Ours()->AttributeInfo(out + 1, id);
            Blob b = gEv;
            b.push_back(r == out + 1);
            for (uint32_t w : out) b.push_back(w);
            outcome[side] = b;
        }
        Same(ctx, outcome[0], outcome[1], "attribute_info", seed);
    }
}
