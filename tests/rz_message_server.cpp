// Differential test for the cached message server getter (lane RW; src/rz/RZMessageServer.h), 0x9c5a89.
//
// The getter reads the cache cell 0x10f519c; when it is empty it asks the framework object in game global 0x10c9378
// (slot 5, the ids 0x04fa845b and 0x652294c7, a holder as output), keeps the answer and releases the holder. The test
// points the global at a fake whose slot 5 hands out a fake held object, or at null; every fake slot records its call
// (object, arguments), so the call sequences are compared along with the result and the cell.
#include "difftest.h"

#include "game/game.h"
#include "rz/RZMessageServer.h"

#include <cstring>
#include <vector>

namespace {

using GetterFn = void *(__cdecl *)();

struct Call {
    int slot;
    const void *self;
    uint32_t a, b;
    const void *wrote;
};

std::vector<Call> gCalls;
bool gReleaseThrows = false;

struct FakeObject {
    void *vtable;
    uint32_t pad;
};

void *gServiceVTable[8];
void *gHeldVTable[8];
FakeObject gService{}, gHeld{}, gCached{};

uint32_t __fastcall RecordQuery(void *self, void * /*edx*/, uint32_t first, uint32_t second, void **out)
{
    gCalls.push_back({5, self, first, second, &gHeld});
    *out = &gHeld;
    return 0;
}

uint32_t __fastcall RecordRelease(void *self, void * /*edx*/)
{
    gCalls.push_back({2, self, 0, 0, nullptr});
    if (gReleaseThrows) throw 7;
    return 0;
}

void SetupFakes()
{
    std::memset(gServiceVTable, 0, sizeof(gServiceVTable));
    std::memset(gHeldVTable, 0, sizeof(gHeldVTable));
    gServiceVTable[5] = reinterpret_cast<void *>(&RecordQuery);
    gHeldVTable[2] = reinterpret_cast<void *>(&RecordRelease);
    gService.vtable = gServiceVTable;
    gHeld.vtable = gHeldVTable;
    gCached.vtable = gHeldVTable;
}

bool SameCalls(const std::vector<Call> &a, const std::vector<Call> &b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const Call &x = a[i], &y = b[i];
        if (x.slot != y.slot || x.self != y.self || x.a != y.a || x.b != y.b || x.wrote != y.wrote) return false;
    }
    return true;
}

} // namespace

DIFF_TEST(GetCachedMessageServer, 0x9c5a89)
{
    SetupFakes();
    auto orig = ctx.Original<GetterFn>(0x9c5a89);

    struct Case {
        bool service;
        void *cache;  // the cell before the call
        bool repeat;  // call a second time without touching the cell
        bool throws;  // the held object's Release throws a C++ exception
    };
    const Case cases[] = {
        {false, nullptr, false, false}, // no framework: nothing is cached, null comes back
        {false, nullptr, true, false},  // asked again the next time
        {true, nullptr, true, false},   // queried, cached, released; the second call hits the cache
        {true, nullptr, false, true},   // the release throws: swallowed, the cache is set
        {true, &gCached, false, false}, // already cached: no query at all
        {false, &gCached, true, false}, // cached, no framework: returned as is
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const Case &c = cases[i];
        gReleaseThrows = c.throws;
        void *ret[2];
        void *cell[2];
        std::vector<Call> calls[2];
        for (int side = 0; side < 2; ++side) {
            *ctx.image.At<void *>(0x10c9378) = c.service ? &gService : nullptr;
            *GameAddress<void *>(0x10f519c) = c.cache;
            gCalls.clear();
            ret[side] = side == 0 ? orig() : GetCachedMessageServer();
            if (c.repeat) {
                ret[side] = side == 0 ? orig() : GetCachedMessageServer();
            }
            cell[side] = *GameAddress<void *>(0x10f519c);
            calls[side] = gCalls;
        }
        ++ctx.cases;
        if (ret[0] != ret[1] || cell[0] != cell[1] || !SameCalls(calls[0], calls[1]))
            ctx.Fail("case %zu: returned %p / %p, cell %p / %p, %zu / %zu calls", i, ret[0], ret[1], cell[0], cell[1],
                     calls[0].size(), calls[1].size());
    }
    gReleaseThrows = false;
    *ctx.image.At<void *>(0x10c9378) = nullptr;
    *GameAddress<void *>(0x10f519c) = nullptr;
}

// Our getter and the image's cell are one object in the hooked build (and in this harness).
DIFF_TEST(GetCachedMessageServer_Cell, 0x9c5a89)
{
    ++ctx.cases;
    if (static_cast<void *>(&ts2_rva_10f519c) != static_cast<void *>(GameAddress<void *>(0x10f519c)))
        ctx.Fail("ts2_rva_10f519c is %p, the image's cell %p", static_cast<void *>(&ts2_rva_10f519c),
                 static_cast<void *>(GameAddress<void *>(0x10f519c)));
}
