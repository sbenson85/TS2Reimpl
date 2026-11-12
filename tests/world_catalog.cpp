// Differential tests for the catalog accessors of src/world/WorldCatalog.* (lane RY, 0x7ef640-0x7efa00), against the fake
// cITSGlobals / catalog / entry of tests/worldfx.h. Results (the byte the callers read for the flag functions, all four for
// the numbers) and the exact calls made through the fakes' slots are compared.
#include "difftest.h"
#include "worldfx.h"

#include "world/WorldCatalog.h"

#include <cstdint>
#include <random>

namespace {

using Fn = uint32_t(__cdecl *)(const uint32_t *);

struct Accessor {
    uint32_t rva;
    uint32_t (*ours)(const uint32_t *);
    uint32_t mask; // the bits of eax the callers read
    const char *name;
};

// Ours return uint8_t / bool / int32_t: the same words through one adapter type each.
template <class R, R(__cdecl *F)(const uint32_t *)> uint32_t Adapt(const uint32_t *id) { return static_cast<uint32_t>(F(id)); }

using namespace worldcatalog;

const Accessor kAccessors[] = {
    {0x7ef660, &Adapt<uint8_t, FlagSlot94OrTrue>, 0xff, "FlagSlot94OrTrue"},
    {0x7ef680, &Adapt<uint8_t, FlagSlot98OrTrue>, 0xff, "FlagSlot98OrTrue"},
    {0x7ef6a0, &Adapt<uint8_t, FlagSlot8cOrTrue>, 0xff, "FlagSlot8cOrTrue"},
    {0x7ef6c0, &Adapt<uint8_t, FlagSlot90OrTrue>, 0xff, "FlagSlot90OrTrue"},
    {0x7ef6e0, &Adapt<bool, Exists>, 0xff, "Exists"},
    {0x7ef700, &Adapt<bool, IsKind1Or3AndSlot94>, 0xff, "IsKind1Or3AndSlot94"},
    {0x7ef760, &Adapt<bool, IsKind1Or3>, 0xff, "IsKind1Or3"},
    {0x7ef790, &Adapt<bool, IsKind2>, 0xff, "IsKind2"},
    {0x7ef7b0, &Adapt<bool, IsKind3>, 0xff, "IsKind3"},
    {0x7ef7d0, &Adapt<bool, IdIs1d>, 0xff, "IdIs1d"},
    {0x7ef7e0, &Adapt<uint8_t, FlagSlotA0>, 0xff, "FlagSlotA0"},
    {0x7ef800, &Adapt<bool, IdIs12d>, 0xff, "IdIs12d"},
    {0x7ef810, &Adapt<bool, IsKind0Or3>, 0xff, "IsKind0Or3"},
    {0x7ef840, &Adapt<uint8_t, FlagSlot70>, 0xff, "FlagSlot70"},
    {0x7ef860, &Adapt<uint8_t, FlagSlot74>, 0xff, "FlagSlot74"},
    {0x7ef8a0, &Adapt<bool, Slot80Is1>, 0xff, "Slot80Is1"},
    {0x7ef8c0, &Adapt<bool, Slot80Is4>, 0xff, "Slot80Is4"},
    {0x7ef8e0, &Adapt<bool, Slot80Is2>, 0xff, "Slot80Is2"},
    {0x7ef920, &Adapt<uint8_t, FlagSlot68>, 0xff, "FlagSlot68"},
    {0x7ef940, &Adapt<uint8_t, FlagSlot6c>, 0xff, "FlagSlot6c"},
    {0x7ef990, &Adapt<uint8_t, FlagSlot64>, 0xff, "FlagSlot64"},
    {0x7ef9b0, &Adapt<int32_t, Slot80>, 0xffffffffu, "Slot80"},
    {0x7ef9d0, &Adapt<int32_t, Kind>, 0xffffffffu, "Kind"},
};

// The entry values a case programs: small numbers (so the kind and slot-0x80 tests hit), and flag bytes with set high bits.
uint32_t RandomValue(std::mt19937 &r)
{
    switch (r() % 5) {
    case 0: return 0;
    case 1: return r() % 6;
    case 2: return 0xffffffffu;
    case 3: return r() & 0xff;
    default: return r();
    }
}

} // namespace

DIFF_TEST(WorldCatalog_Accessors, 0x7ef640)
{
    worldfx::Scope scope;
    worldfx::Catalog catalog;
    catalog.Plug(scope);
    std::mt19937 r(0x7ef640);

    for (int i = 0; i < 3000; ++i) {
        catalog.Rebuild();
        const uint32_t ids[] = {0x1d, 0x12d, r() % 400, r()};
        for (unsigned e = 0; e < worldfx::Catalog::kEntries; ++e) {
            worldfx::Fake &entry = catalog.Entry(e);
            for (unsigned slot = 24; slot <= 40; ++slot) entry.Set(slot, 0, RandomValue(r));
        }
        for (unsigned k = 0; k < 4; ++k) catalog.Map(ids[k], r() % (worldfx::Catalog::kEntries + 2));
        worldfx::gLog.clear();

        for (const Accessor &a : kAccessors) {
            const uint32_t id = ids[r() % 4];
            const auto original = ctx.Original<Fn>(a.rva);
            worldfx::gLog.clear();
            const uint32_t ra = original(&id) & a.mask;
            const std::vector<uint32_t> logA = worldfx::gLog;
            worldfx::gLog.clear();
            const uint32_t rb = a.ours(&id) & a.mask;
            ++ctx.cases;
            if (ra != rb) ctx.Fail("case %d: %s id %#x: %#x vs %#x", i, a.name, id, ra, rb);
            if (logA != worldfx::gLog) ctx.Fail("case %d: %s id %#x: slot calls differ (%zu vs %zu words)", i, a.name, id, logA.size(), worldfx::gLog.size());
        }
        const uint32_t id = ids[r() % 4];
        worldfx::gLog.clear();
        void *const ea = ctx.Original<void *(__cdecl *)(const uint32_t *)>(0x7ef640)(&id);
        const std::vector<uint32_t> logA = worldfx::gLog;
        worldfx::gLog.clear();
        void *const eb = worldcatalog::Entry(&id);
        ++ctx.cases;
        if (ea != eb || logA != worldfx::gLog) ctx.Fail("case %d: Entry id %#x", i, id);
    }
}

// ---- lane RZ: the wall-product and partition helpers 0x7ef3f0-0x7ef590 ----

namespace {

// The products the catalog's slots 26 / 31 / 27 answer, and the holder slot 31 answers: its +4 is a vbtable pointer whose
// entry 6 (+0x18) is the offset from +4 to the product face.
worldfx::Fake gProductA, gProductB, gProductC;
struct Holder {
    uint32_t head;
    const int32_t *vbtable;
};
Holder gHolder;
int32_t gVbtable[8];
const char kEmptyName[] = "";
const char kName[] = "wall";

// The partitions object (cIWorldDB slot 39) and its records: slot 56 copies record i into the caller's 24 bytes, slot 50
// answers the id of a record. The record pointer differs between the two sides, so both slots replace it in the log with
// what it points at.
worldfx::Fake gPartitions;
uint32_t gPartitionIds[6];

uint32_t CopyRecord(worldfx::Fake *, const uint32_t *args)
{
    uint32_t *const record = reinterpret_cast<uint32_t *>(uintptr_t(args[0]));
    for (unsigned w = 0; w < 6; ++w) record[w] = gPartitionIds[args[1] % 6] + w;
    worldfx::gLog[worldfx::gLog.size() - 2] = 0x5ec0cd00u;
    return 1;
}
uint32_t RecordId(worldfx::Fake *, const uint32_t *args)
{
    const uint32_t *const record = reinterpret_cast<const uint32_t *>(uintptr_t(args[0]));
    worldfx::gLog.back() = record[0];
    for (unsigned w = 1; w < 6; ++w) worldfx::gLog.push_back(record[w]);
    return record[0];
}
float PartitionFloat(worldfx::Fake *, const uint32_t *args) { return static_cast<float>(args[0] % 1000) * 0.25f + 0.5f; }

uint32_t ProductValue(std::mt19937 &r) { return r() % 3 == 0 ? 0 : r() % 2 ? 1 : r(); }

void ProgramProduct(std::mt19937 &r, worldfx::Fake &p, uint32_t id)
{
    p.Reset(id);
    const uint32_t names[] = {0, uint32_t(uintptr_t(kEmptyName)), uint32_t(uintptr_t(kName)), uint32_t(uintptr_t(kName))};
    p.Set(8, 0, names[r() % 4]);
    p.Set(9, 0, ProductValue(r));
    p.Set(10, 0, r() % 2 ? (r() & 0xff) : 0);
    p.Set(11, 0, r() % 2 ? (r() & 0xff) : 0);
}

} // namespace

DIFF_TEST(WorldCatalog_ProductHelpers, 0x7ef510)
{
    worldfx::Scope scope;
    worldfx::Catalog catalog;
    catalog.Plug(scope);
    scope.Set(worldfx::kWorldDbCell, &worldfx::gWorldDb);
    std::mt19937 r(0x7ef510);

    using IntFn = uint32_t(__cdecl *)(const uint32_t *);
    struct IntCase {
        uint32_t rva;
        uint32_t (*ours)(const uint32_t *);
        uint32_t mask;
        const char *name;
    };
    const IntCase kInts[] = {
        {0x7ef3f0, &Adapt<uint8_t, ProductFlag10>, 0xff, "ProductFlag10"},
        {0x7ef430, &Adapt<uint8_t, ProductFlag11>, 0xff, "ProductFlag11"},
        {0x7ef510, &Adapt<uint32_t, ProductResource>, 0xffffffffu, "ProductResource"},
        {0x7ef590, &Adapt<bool, AnyPartitionHas>, 0xff, "AnyPartitionHas"},
    };

    for (int i = 0; i < 4000; ++i) {
        catalog.Rebuild();
        const uint32_t ids[] = {r() % 400, r() % 400, 0x2d0ea907u, r()};
        for (unsigned e = 0; e < worldfx::Catalog::kEntries; ++e) catalog.Entry(e).Set(24, 0, r() % 3 ? r() % 4 : r());
        for (unsigned k = 0; k < 4; ++k) catalog.Map(ids[k], r() % (worldfx::Catalog::kEntries + 2));

        ProgramProduct(r, gProductA, 400);
        ProgramProduct(r, gProductB, 401);
        ProgramProduct(r, gProductC, 402);
        gHolder.vbtable = gVbtable;
        gVbtable[6] = int32_t(reinterpret_cast<intptr_t>(&gProductB) - reinterpret_cast<intptr_t>(&gHolder.vbtable));
        worldfx::Fake &cat = catalog.CatalogFake();
        cat.Set(26, 2, r() % 2 ? uint32_t(uintptr_t(&gProductA)) : 0);
        cat.Set(31, 1, r() % 2 ? uint32_t(uintptr_t(&gHolder)) : 0);
        cat.Set(27, 0, r() % 2 ? uint32_t(uintptr_t(&gProductC)) : 0);

        gPartitions.Reset(410);
        const unsigned count = r() % 4 ? r() % 6 : 0;
        for (unsigned k = 0; k < 6; ++k) gPartitionIds[k] = r() % 2 ? ids[r() % 4] : r();
        gPartitions.Set(55, 0, count);
        gPartitions.Set(56, 2, 0, &CopyRecord);
        gPartitions.Set(50, 1, 0, &RecordId);
        gPartitions.SetFloatFn(84, 1, &PartitionFloat);
        worldfx::gWorldDb.Reset(420);
        worldfx::gWorldDb.Set(39, 0, r() % 8 ? uint32_t(uintptr_t(&gPartitions)) : 0);

        for (const IntCase &c : kInts) {
            const uint32_t id = ids[r() % 4];
            worldfx::gLog.clear();
            const uint32_t ra = ctx.Original<IntFn>(c.rva)(&id) & c.mask;
            const std::vector<uint32_t> logA = worldfx::gLog;
            worldfx::gLog.clear();
            const uint32_t rb = c.ours(&id) & c.mask;
            ++ctx.cases;
            if (ra != rb) ctx.Fail("case %d: %s id %#x: %#x vs %#x", i, c.name, id, ra, rb);
            if (logA != worldfx::gLog) ctx.Fail("case %d: %s id %#x: slot calls differ (%zu vs %zu words)", i, c.name, id, logA.size(), worldfx::gLog.size());
        }

        // 0x7ef470 needs the partitions object (it calls slot 84 on it unchecked).
        worldfx::gWorldDb.Set(39, 0, uint32_t(uintptr_t(&gPartitions)));
        const uint32_t id = ids[r() % 4];
        worldfx::gLog.clear();
        const float fa = ctx.Original<float(__cdecl *)(const uint32_t *)>(0x7ef470)(&id);
        const std::vector<uint32_t> logA = worldfx::gLog;
        worldfx::gLog.clear();
        const float fb = PartitionValue(&id);
        ++ctx.cases;
        if (std::memcmp(&fa, &fb, sizeof(fa)) != 0) ctx.Fail("case %d: PartitionValue id %#x: %g vs %g", i, id, fa, fb);
        if (logA != worldfx::gLog) ctx.Fail("case %d: PartitionValue id %#x: slot calls differ", i, id);
    }
}
