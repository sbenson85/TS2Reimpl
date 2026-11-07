#pragma once
// Fake world shared by the cTSNeighborGeneticDataTable difftests (tests/ts_neighbor_genetic_data_table*.cpp).
//
// Every object the table talks to is a fake with a recording vtable: calls append to `events`, objects hand out counted
// references (refs++), and the final reference counts are compared as well. The table under test is one guarded buffer;
// its map nodes are real heap blocks laid out like the game's.
#include "difftest.h"
#include "ts/cTSNeighborGeneticDataTable.h"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace ngdt {
using Table = cTSNeighborGeneticDataTable;

struct Obj { void **vt; uint32_t id; int32_t refs; };
struct Guarded { uint8_t before[16]; alignas(8) uint8_t bytes[0x2c]; uint8_t after[16]; };

constexpr unsigned kDataCount = 32;
// Ids of the fakes in the event log.
enum : uint32_t {
    kTableId = 1, kNewTableId = 2, kNewFaceId = 3, kServiceId = 10, kSegmentId = 11, kManagerId = 12, kStreamId = 13,
    kHolderId = 14, kContainerId = 15, kIdSourceId = 16, kFrameworkId = 17, kDataId = 20, kFaceId = 60, kTableFaceId = 0x1000
};
enum : uint32_t {
    evAddRef = 1, evRelease, evQuery, evGetKey, evSetKey, evGetResource, evSaveResource, evFindSegment, evFlush, evCreateResource,
    evGetSint32, evSetSint32, evMetadata, evGetIds, evMapId, evGetGroup, evGetInterface, evCreateInstance, evTableSet,
    evTableGet, evTableCreate, evTablePersist, evTableRead, evTableWrite, evSetProperty, evFirstProperty, evNextProperty,
    evDestroy
};

struct Config {
    // persist system
    bool serviceNull = false;
    uint32_t getResourceFail = 0;     // bit n: the n-th GetResource call returns false
    bool getResourceNull = false;     // success hands out null
    uint32_t findSegmentFail = 0;     // 1: FindSegment fails
    uint32_t saveResult[3] = {1, 1, 1}; // SaveResource results by call index (last repeats); full byte
    uint8_t flushResult = 1;
    // resource manager
    bool managerNull = false;
    uint32_t managerGetFail = 0;      // bit n: the n-th GetResource call fails
    bool managerCreateNull = false;
    // stream
    uint32_t streamFail = 0;          // bit n: the n-th transfer fails
    uint32_t version = 1, count = 0;
    uint32_t ids[16] = {};
    // holder / container
    bool holderFail = false;
    bool idSourceFail = false, segmentFail = false;
    uint32_t group = 0x1234;
    uint32_t idList[24] = {};
    uint32_t idCount = 0;
    bool idListEmptyBlock = false;
    uint32_t mapFail = 0;             // bit n: MapId of the n-th id fails
    uint32_t mapOffset = 100;
    uint32_t dataMissing = 0;         // bit n (id n): table Get returns null
    uint32_t emptyProps = 0;          // bit n (data n): no properties
    bool failWritesOut = false;       // a failing GetResource still hands out a counted object (the caller must release it)
    int throwAt = -1;                 // the n-th fake call that may throw does (counted from 0), or none
    uint32_t queryFail = 0;           // bit n (data n): QueryInterface for the persist face fails
    uint8_t tableReadResult = 0x5a, tableWriteResult = 0xa5;
};

struct World {
    Config cfg;
    Guarded work;
    Obj data[kDataCount], face[kDataCount];
    uint32_t key[kDataCount][4];
    std::vector<std::pair<std::string, uint32_t>> props[kDataCount];
    Obj newTable, newFace, loaded, service, segment, manager, stream, holder, container, idSource, framework;
    uint32_t newKey[4];
    std::vector<uint32_t> events;
    bool recording = true;
    // counters
    unsigned getResources = 0, managerGets = 0, transfers = 0, saves = 0, mapCalls = 0, copies = 0, creations = 0, frees = 0;
    int ticks = 0;
    bool sawThrow = false;
    std::vector<uint32_t> written;       // stream words written
    void *lastBlock = nullptr; uint32_t lastBytes = 0; // the heap buffer of the last property name handed out, from the game allocator
    void *idBlock = nullptr; uint32_t idBytes = 0;      // the block of the last id vector handed out
};

World &W();
Table *T();
uint32_t IdOf(const void *p);
uint32_t Norm(const void *p);
void Ev(std::initializer_list<uint32_t> items);
void Tick(); // a fake call that may throw (Config::throwAt)

// The recording vtables (filled by Reset).
extern void *tableVT[22], *persistFaceVT[12], *dataVT[8], *faceVT[12], *serviceVT[40], *segmentVT[12], *managerVT[8],
    *readerVT[12], *writerVT[24], *holderVT[4], *containerVT[16], *idSourceVT[34], *directorVT[13], *frameworkVT[4];

// Deterministic state for a seed: random table bytes (vtables set), `entries` entries with distinct ids drawn from a
// small range plus a few extreme ones, each pointing at a data fake. Recording is off while building.
struct Seeded {
    unsigned seed;
    unsigned entries;
    bool modified;
    uint32_t refCount;
};
void Reset(const Seeded &s, const Config &cfg);
// Releases the table's entries without recording and destroys its map.
void Cleanup();
void SetStreamMode(bool reading);
void MarkTableFreed(); // the table freed itself: Cleanup / Capture must not touch it

// A comparable record of everything observable.
struct Blob {
    std::vector<uint32_t> w;
    std::vector<std::pair<size_t, const char *>> marks;
    void Mark(const char *name) { marks.emplace_back(w.size(), name); }
    void Add(uint32_t v) { w.push_back(v); }
};
// Snapshot of the guarded table, its map, the fakes' counts and the events.
void Capture(Blob &b, bool tableAlive = true);
bool Same(difftest::Context &ctx, const Blob &a, const Blob &b, const char *what, unsigned seed);

// Thread-safe-statics bypass for the cached getters, restored on exit.
struct Globals {
    explicit Globals(difftest::Context &ctx);
    ~Globals();
    void *saved[8];
    alignas(8) uint8_t allocatorService[16];
    bool builtAllocator = false;
    int32_t savedGuard[2];
    difftest::Context &ctx;
    void SetService(bool null);
    void SetManager(bool null);
};

// Counts the exception positions a test swept (see Pair).
inline unsigned gThrown = 0;
struct SweepReport {
    const char *name;
    unsigned before = gThrown;
    explicit SweepReport(const char *n) : name(n) {}
    ~SweepReport() { std::printf("  %s: %u exception positions swept (a fake throws at call 0..n)\n", name, gThrown - before); }
};

// Runs `run(original, blob)` on two identically seeded worlds and compares everything observable, including an exception
// escaping from a fake. Every 16th seed repeats the case with the n-th throwing fake call throwing, for each n until none does.
template <class Run>
bool RunPair(difftest::Context &ctx, const char *what, unsigned seed, const Seeded &s, const Config &cfg, Run &run)
{
    Blob left, right;
    for (int side = 0; side < 2; ++side) {
        Reset(s, cfg);
        Blob &b = side == 0 ? left : right;
        try { run(side == 0, b); }
        catch (int) { W().sawThrow = true; b.Mark("thrown"); b.Add(0x7e57); }
        Capture(b);
        Cleanup();
    }
    return Same(ctx, left, right, what, seed);
}
template <class Run>
bool Pair(difftest::Context &ctx, const char *what, unsigned seed, const Seeded &s, const Config &cfg, Run run)
{
    bool ok = RunPair(ctx, what, seed, s, cfg, run);
    if (seed % 16 == 5) {
        Config swept = cfg;
        for (int k = 0; k < 60; ++k) {
            swept.throwAt = k;
            ok &= RunPair(ctx, what, seed * 100 + k, s, swept, run);
            if (!W().sawThrow) break;
            ++gThrown;
        }
    }
    return ok;
}
} // namespace ngdt
