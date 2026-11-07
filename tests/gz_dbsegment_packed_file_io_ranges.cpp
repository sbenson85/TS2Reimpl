// Differential tests for cGZDBSegmentPackedFile: free ranges, filters, reference counts.
// Fixtures (the arena, the fake store and file objects, the comparison helpers): gz_dbsegment_io_fixtures.h.
#include "gz_dbsegment_io_fixtures.h"

DIFF_TEST(cGZDBSegmentPackedFile_LookupRecordEnd, 0x9abb50)
{
    InitFakes();
    auto orig = ctx.Original<U32RecFn>(0x9abb50);
    Tables plain = MakeTables(ctx);
    std::mt19937 rng(0x9abb50);
    for (int i = 0; i < 400; ++i) {
        Image s = MakeImage(rng);
        Wire(s);
        gFindRet = (rng() % 4 == 0) ? nullptr : static_cast<void *>(gArena + kHit);
        // The record's +4 word and the masked +8 word meet at the edge of the "above" test a third of the time.
        uint32_t w8 = PickWord(rng);
        uint32_t w4 = (rng() % 3 == 0) ? (w8 & 0x7fffffffu) : PickWord(rng);
        SetWord(s, kHit + 8, w8);
        SetWord(s, kHit + 4, w4);
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self, self + kRecIn); });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t {
            return Obj(self)->LookupRecordEnd(reinterpret_cast<uint32_t *>(self + kRecIn));
        });
        Compare(ctx, "LookupRecordEnd", a, b, b);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_CountRanges, 0x9abde0)
{
    InitFakes();
    auto orig = ctx.Original<BoolPtrsFn>(0x9abde0);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[47] = Addr(static_cast<BoolPtrsMember>(&cGZDBSegmentPackedFile::GetFreeRangeTotals));
    std::mt19937 rng(0x9abde0);
    for (int i = 0; i < 300; ++i) {
        Image s = MakeImage(rng);
        Wire(s);
        // Two trees with random shapes (including empty ones), built by the library with the default allocator.
        std::map<uint32_t, uint32_t> first, second;
        for (uint32_t n = rng() % 12; n > 0; --n) first[static_cast<uint32_t>(rng() % 1000)] = PickWord(rng);
        for (uint32_t n = rng() % 12; n > 0; --n) second[static_cast<uint32_t>(rng() % 1000)] = PickWord(rng);
        static_assert(sizeof(first) == 8, "map: head and size");
        std::memcpy(s.data() + 0x184, &first, 8);
        std::memcpy(s.data() + 0x18c, &second, 8);
        s[0x24] = (rng() % 3 == 0) ? 0 : PickFlag(rng); // closed a third of the time
        auto call = [&](auto fn) {
            return [&, fn](uint8_t *self) -> uint64_t {
                return fn(self, reinterpret_cast<uint32_t *>(self + kDst), reinterpret_cast<uint32_t *>(self + kDst + 4));
            };
        };
        auto a = Run(plain, s, call([&](uint8_t *self, uint32_t *x, uint32_t *y) { return orig(self, x, y); }));
        auto b = Run(plain, s, call([&](uint8_t *self, uint32_t *x, uint32_t *y) { return Obj(self)->GetFreeRangeTotals(x, y); }));
        auto c = Run(routed, s, call([&](uint8_t *self, uint32_t *x, uint32_t *y) {
            return VirtualSlot<BoolPtrsFn>(self, 47)(self, x, y);
        }));
        Compare(ctx, "GetFreeRangeTotals", a, b, c);
    }
}

DIFF_TEST(cGZDBSegmentPackedFile_CheckExtents, 0x9b0dc0)
{
    InitFakes();
    auto orig = ctx.Original<BoolFn>(0x9b0dc0);
    Tables plain = MakeTables(ctx);
    uint32_t *global = ctx.image.At<uint32_t>(0x10f5030);
    uint32_t savedGlobal = *global;
    std::mt19937 rng(0x9b0dc0);
    int accepted = 0, reachedLast = 0;
    for (int i = 0; i < 1500; ++i) {
        Image s = MakeImage(rng);
        Wire(s);
        // Four file lengths, one per call; the object's fields sit around the length of "their" call so every
        // comparison is exercised at its edge. Most trials pass the earlier tests, to reach the later ones.
        gLengths.clear();
        for (int k = 0; k < 4; ++k) gLengths.push_back(rng() % 4 == 0 ? PickWord(rng) : 100 + rng() % 1000);
        *global = rng() % 2 ? 0x1234 : PickWord(rng);
        bool good = rng() % 4 != 0;
        SetWord(s, 0x118, good ? *global : static_cast<uint32_t>(rng()));
        SetWord(s, 0x11c, good ? rng() % 2 : (rng() % 5 == 0 ? 2 : PickWord(rng)));
        SetWord(s, 0x13c, rng() % 6 == 0 ? 0x7ffffff - (rng() % 3) + 1 : rng() % 1000);
        SetWord(s, 0x148, rng() % 6 == 0 ? 0x7ffffff - (rng() % 3) + 1 : rng() % 1000);
        auto around = [&](uint32_t len) { return len + static_cast<uint32_t>(static_cast<int>(rng() % 3) - 1); };
        uint32_t f140 = rng() % 2 ? around(gLengths[0]) : static_cast<uint32_t>(rng() % 100);
        uint32_t sum1 = around(gLengths[1]);
        uint32_t f144 = rng() % 9 == 0 ? static_cast<uint32_t>(rng()) : sum1 - f140;
        uint32_t f14C = rng() % 2 ? around(gLengths[2]) : static_cast<uint32_t>(rng() % 100);
        uint32_t sum2 = around(gLengths[3]);
        uint32_t f150 = rng() % 9 == 0 ? static_cast<uint32_t>(rng()) : sum2 - f14C;
        SetWord(s, 0x140, f140);
        SetWord(s, 0x144, f144);
        SetWord(s, 0x14c, f14C);
        SetWord(s, 0x150, f150);
        SetWord(s, 0x154, PickWord(rng));
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self); });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { return Obj(self)->CheckExtents(); });
        Compare(ctx, "CheckExtents", a, b, b);
        if (a.result & 0xff) ++accepted;
        if (a.log.size() / 3 >= 4) ++reachedLast;
    }
    // Setup check: enough trials get through every test, and enough get as far as the last length test.
    if (accepted < 100 || reachedLast < 100) ctx.Fail("CheckExtents: setup reaches too little (%d accepted, %d to the last test)", accepted, reachedLast);
    *global = savedGlobal;
}

DIFF_TEST(cGZDBSegmentPackedFile_RangeTreeAddRange, 0x9ad570)
{
    WorldScope world(ctx);
    auto orig = ctx.Original<AddRangeFn>(0x9ad570);
    Tables plain = MakeTables(ctx);
    std::mt19937 rng(0x9ad570);
    int plain1 = 0, oneMerge = 0, twoMerge = 0;
    for (int i = 0; i < 1500; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        std::memcpy(gArena, img.data(), kBytes);
        auto ranges = BuildRanges(rng, 0x184, 12);
        // The new range: next to an existing one on either side, filling a gap exactly, or arbitrary.
        uint32_t offset, length = 1 + rng() % 6;
        int kind = static_cast<int>(rng() % 8);
        if (ranges.empty() || kind == 0) {
            offset = static_cast<uint32_t>(rng() % 60);
        } else {
            auto pick = ranges[rng() % ranges.size()];
            switch (kind) {
            case 1: offset = pick.first + pick.second; break;                                    // starts where one ends
            case 2: offset = pick.first - length; break;                                         // ends where one starts
            case 3: offset = pick.first; break;                                                  // same start (duplicate key)
            case 4: offset = pick.first + pick.second; length = 1 + rng() % 3; break;
            case 5: // fills the gap after `pick` exactly, when there is a following range
                offset = pick.first + pick.second;
                for (auto &r : ranges) if (r.first > pick.first) { length = r.first - offset; break; }
                break;
            case 6: offset = PickWord(rng); length = PickWord(rng); break;                      // wrapping sums
            default: offset = pick.first + pick.second + (rng() % 3); break;
            }
        }
        if (rng() % 15 == 0) length = 0;
        Snapshot snap;
        snap.Take();
        auto a = WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t { orig(self + 0x184, offset, length); return 0; });
        auto b = WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
            RangeTree::AddRange(reinterpret_cast<RangeTree *>(self + 0x184), offset, length);
            return 0;
        });
        WorldCompare(ctx, "RangeTree::AddRange", a, b, b);
        uint32_t before = *reinterpret_cast<const uint32_t *>(snap.arena.data() + 0x188);
        uint32_t after = *reinterpret_cast<const uint32_t *>(a.bytes.data() + 0x188); // the arena comes first
        int delta = static_cast<int>(after - before);
        if (delta == 1) ++plain1;
        if (delta == 0) ++oneMerge;
        if (delta == -1) ++twoMerge;
    }
    // Setup check: the merge paths are reached.
    if (plain1 < 100 || oneMerge < 100 || twoMerge < 30)
        ctx.Fail("AddRange: setup reaches too little (%d plain, %d one-merge, %d two-merge)", plain1, oneMerge, twoMerge);
}

namespace {

// The object with three range trees (at +0x184, +0x18c, +0x194) and the selector fields of AddFreeRangeTo.
struct FreeRangeScene {
    std::vector<std::pair<uint32_t, uint32_t>> ranges[3];
};

FreeRangeScene BuildThreeTrees(std::mt19937 &rng)
{
    FreeRangeScene f;
    f.ranges[0] = BuildRanges(rng, 0x184, 8);
    f.ranges[1] = BuildRanges(rng, 0x18c, 8);
    f.ranges[2] = BuildRanges(rng, 0x194, 8);
    return f;
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_AddFreeRange, 0x9ab860)
{
    WorldScope world(ctx);
    using Fn = bool(__thiscall *)(void *, uint32_t, uint32_t, uint8_t);
    auto orig = ctx.Original<Fn>(0x9ab860);
    Tables plain = MakeTables(ctx);
    std::mt19937 rng(0x9ab860);
    int perTree[3] = {0, 0, 0}, skipped = 0;
    for (int i = 0; i < 1500; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        std::memcpy(gArena, img.data(), kBytes);
        FreeRangeScene f = BuildThreeTrees(rng);
        // Bit 4 of +0x17c picks "use the byte"; the selector byte is 0, 1 or 3 (selecting +0x18c, +0x184, +0x194; 2 would select +0x19c, which is no tree).
        uint32_t flags = static_cast<uint32_t>(rng()) & ~0x10u;
        if (rng() % 2) flags |= 0x10;
        std::memcpy(gArena + 0x17c, &flags, 4);
        gArena[0x19d] = static_cast<uint8_t>(rng() % 3 == 0 ? 1 : 0);
        static const uint8_t kSelectors[] = {0, 1, 3};
        uint8_t which = kSelectors[rng() % 3];
        int chosen = ((flags & 0x10) ? (which ^ 1) : 1);

        uint32_t offset, length;
        PickNewRange(rng, f.ranges[chosen], offset, length);
        if (rng() % 6 == 0) offset = 0;
        if (rng() % 6 == 0) length = 0;
        Snapshot snap;
        snap.Take();
        auto a = WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t { return orig(self, offset, length, which); });
        auto b = WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t { return Obj(self)->AddFreeRangeTo(offset, length, which); });
        WorldCompare(ctx, "AddFreeRangeTo", a, b, b);
        if (offset == 0 || length == 0) ++skipped; else ++perTree[chosen];
    }
    if (skipped < 100 || perTree[0] < 100 || perTree[1] < 100 || perTree[2] < 100) ctx.Fail("AddFreeRangeTo: setup reaches too little");
}

DIFF_TEST(cGZDBSegmentPackedFile_AddFreeRangeDefault, 0x9ab820)
{
    WorldScope world(ctx);
    using Fn = bool(__thiscall *)(void *, uint32_t, uint32_t);
    using Member = bool (cGZDBSegmentPackedFile::*)(uint32_t, uint32_t);
    auto orig = ctx.Original<Fn>(0x9ab820);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[45] = Addr(static_cast<Member>(&cGZDBSegmentPackedFile::AddFreeRange));
    std::mt19937 rng(0x9ab820);
    for (int i = 0; i < 1000; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        std::memcpy(gArena, img.data(), kBytes);
        FreeRangeScene f = BuildThreeTrees(rng);
        gArena[0x19d] = static_cast<uint8_t>(rng() % 3 == 0 ? 1 : 0);
        uint32_t offset, length;
        PickNewRange(rng, f.ranges[0], offset, length);
        if (rng() % 6 == 0) offset = 0;
        if (rng() % 6 == 0) length = 0;
        Snapshot snap;
        snap.Take();
        auto a = WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t { return orig(self, offset, length); });
        auto b = WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t { return Obj(self)->AddFreeRange(offset, length); });
        auto c = WorldRun(snap, routed, [&](uint8_t *self) -> uint64_t { return VirtualSlot<Fn>(self, 45)(self, offset, length); });
        WorldCompare(ctx, "AddFreeRange", a, b, c);
    }
}

// ---- The type and group filters (unordered_set<unsigned> at +0x1ac and +0x1b0) ----

namespace {

using IdSet = std::unordered_set<uint32_t>;

IdSet *BuildSet(std::mt19937 &rng)
{
    auto *set = new IdSet();
    for (unsigned n = rng() % 11; n > 0; --n) set->insert(rng() % 5 == 0 ? static_cast<uint32_t>(rng()) : static_cast<uint32_t>(rng() % 16));
    return set;
}

// Size, bucket count, load factor and the elements in iteration order: the container's observable state.
void Describe(const IdSet *set, std::vector<int64_t> &out)
{
    if (set == nullptr) {
        out.push_back(-1);
        return;
    }
    out.push_back(static_cast<int64_t>(set->size()));
    out.push_back(static_cast<int64_t>(set->bucket_count()));
    out.push_back(static_cast<int64_t>(set->max_load_factor() * 1000));
    for (uint32_t v : *set) out.push_back(v);
}

IdSet *PointerAt(const uint8_t *arena, size_t at)
{
    uint32_t v;
    std::memcpy(&v, arena + at, sizeof(v));
    return reinterpret_cast<IdSet *>(static_cast<uintptr_t>(v));
}

struct FilterState {
    bool hasTypes, hasGroups;
    uint32_t seed;
};

// Runs one call with two freshly built sets (the same ones every time for a seed), appends the sets' state to the
// log afterwards and frees them. The set pointers themselves are not compared (they differ between runs).
template <typename Call> Outcome RunFilter(const Tables &t, const Image &s, const FilterState &f, Call call)
{
    std::memcpy(gArena, s.data(), kBytes);
    const void *primary = t.primary;
    const void *sub = t.sub;
    std::memcpy(gArena + 0, &primary, sizeof(primary));
    std::memcpy(gArena + 4, &sub, sizeof(sub));
    std::mt19937 build(f.seed);
    IdSet *types = f.hasTypes ? BuildSet(build) : nullptr;
    IdSet *groups = f.hasGroups ? BuildSet(build) : nullptr;
    std::memcpy(gArena + 0x1ac, &types, 4);
    std::memcpy(gArena + 0x1b0, &groups, 4);
    gLog.clear();
    Outcome out;
    difftest::FreeRecorder rec;
    out.result = call(gArena);
    rec.Stop();
    out.log = gLog;
    out.log.push_back(-2);
    IdSet *typesAfter = PointerAt(gArena, 0x1ac);
    IdSet *groupsAfter = PointerAt(gArena, 0x1b0);
    // A set that the call dropped (pointer nulled) must have been freed, not leaked (FreeRecorder across the call).
    auto freed = [&rec](IdSet *before, IdSet *after) -> int64_t {
        if (before == nullptr || after != nullptr) return -3;
        return rec.Freed(before) ? 1 : 0;
    };
    out.log.push_back(freed(types, typesAfter));
    out.log.push_back(freed(groups, groupsAfter));
    Describe(typesAfter, out.log);
    Describe(groupsAfter, out.log);
    delete typesAfter;
    delete groupsAfter;
    std::memset(gArena, 0, 8);
    std::memset(gArena + 0x1ac, 0, 8);
    out.bytes.assign(gArena, gArena + kBytes);
    return out;
}

template <int Slot, typename Member>
void EditTest(difftest::Context &ctx, const char *name, uint32_t rva, Member member, size_t slotOffset, uint32_t seed)
{
    InitFakes();
    using Fn = bool(__thiscall *)(void *, uint32_t, bool);
    using M = bool (cGZDBSegmentPackedFile::*)(uint32_t, bool);
    auto orig = ctx.Original<Fn>(rva);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[Slot] = Addr(static_cast<M>(member));
    std::mt19937 rng(seed);
    int created = 0, emptied = 0, kept = 0;
    for (int i = 0; i < 800; ++i) {
        Image s = MakeImage(rng);
        Wire(s);
        bool add = rng() % 2;
        FilterState f{rng() % 2 == 0, rng() % 2 == 0, static_cast<uint32_t>(rng())};
        bool &mine = (slotOffset == 0x1ac) ? f.hasTypes : f.hasGroups;
        // Removing needs a set (the original dereferences the null pointer otherwise).
        if (!add) mine = true;
        bool existed = mine;
        // A value from the small domain, so that it is often present; sometimes a fresh one.
        uint32_t value = rng() % 4 == 0 ? static_cast<uint32_t>(rng()) : static_cast<uint32_t>(rng() % 16);
        auto a = RunFilter(plain, s, f, [&](uint8_t *self) -> uint64_t { return orig(self, value, add); });
        auto b = RunFilter(plain, s, f, [&](uint8_t *self) -> uint64_t { return (Obj(self)->*member)(value, add); });
        auto c = RunFilter(routed, s, f, [&](uint8_t *self) -> uint64_t { return VirtualSlot<Fn>(self, Slot)(self, value, add); });
        Compare(ctx, name, a, b, c);
        // The edited set's description starts after the -2 marker; the types' comes first, then the groups'.
        size_t marker = std::find(a.log.begin(), a.log.end(), -2) - a.log.begin();
        size_t at = marker + 3; // after the two "freed" flags
        if (slotOffset == 0x1b0) at += (a.log[at] == -1) ? 1 : 3 + static_cast<size_t>(a.log[at]);
        bool present = a.log[at] != -1;
        if (add && !existed) ++created;
        if (!add && !present) ++emptied;
        if (!add && present) ++kept;
    }
    if (created < 50 || emptied < 20 || kept < 100) ctx.Fail("%s: setup reaches too little (%d created, %d emptied, %d kept)", name, created, emptied, kept);
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_EditTypeFilter, 0x9aac00)
{
    EditTest<39>(ctx, "EditCompressedTypes", 0x9aac00, &cGZDBSegmentPackedFile::EditCompressedTypes, 0x1ac, 0x9aac00);
}

DIFF_TEST(cGZDBSegmentPackedFile_EditGroupFilter, 0x9aab40)
{
    EditTest<40>(ctx, "EditCompressedGroups", 0x9aab40, &cGZDBSegmentPackedFile::EditCompressedGroups, 0x1b0, 0x9aab40);
}

DIFF_TEST(cGZDBSegmentPackedFile_PassesFilter, 0x9b03a0)
{
    InitFakes();
    using Fn = bool(__thiscall *)(void *, const uint32_t *, uint32_t);
    auto orig = ctx.Original<Fn>(0x9b03a0);
    Tables plain = MakeTables(ctx);
    std::mt19937 rng(0x9b03a0);
    int accepted = 0, viaTypes = 0, viaGroups = 0, rejectedLate = 0;
    for (int i = 0; i < 2000; ++i) {
        Image s = MakeImage(rng);
        Wire(s);
        s[0x1aa] = (rng() % 2 == 0) ? static_cast<uint8_t>(rng() % 3) : 0;
        FilterState f{rng() % 4 != 0, rng() % 4 != 0, static_cast<uint32_t>(rng())};
        uint32_t id[2];
        id[0] = rng() % 8 == 0 ? 0xe86b1eefu : static_cast<uint32_t>(rng() % 16);
        id[1] = static_cast<uint32_t>(rng() % 16);
        static const uint32_t sizes[] = {0, 0x31, 0x32, 0x33, 100, 15999999, 16000000, 16000001, 0xfffffff0u, 0xffffffffu, 16000000, 0x32};
        uint32_t size = rng() % 2 == 0 ? sizes[rng() % 12] : 50 + rng() % 1000;
        std::memcpy(s.data() + kRecIn, id, sizeof(id));
        auto a = RunFilter(plain, s, f, [&](uint8_t *self) -> uint64_t {
            return orig(self, reinterpret_cast<const uint32_t *>(self + kRecIn), size);
        });
        auto b = RunFilter(plain, s, f, [&](uint8_t *self) -> uint64_t {
            return Obj(self)->PassesFilter(reinterpret_cast<const uint32_t *>(self + kRecIn), size);
        });
        Compare(ctx, "PassesFilter", a, b, b);
        // Which path accepted: recompute from the same sets.
        std::mt19937 probe(f.seed);
        IdSet *types = f.hasTypes ? BuildSet(probe) : nullptr;
        IdSet *groups = f.hasGroups ? BuildSet(probe) : nullptr;
        bool inTypes = types && types->count(id[0]);
        bool inGroups = groups && groups->count(id[1]);
        bool sizeOk = size - 0x32u <= 0xf423ceu && id[0] != 0xe86b1eefu;
        if (a.result & 0xff) ++accepted;
        if (sizeOk && s[0x1aa] == 0 && (a.result & 0xff) && inTypes) ++viaTypes;
        if (sizeOk && s[0x1aa] == 0 && (a.result & 0xff) && !inTypes && inGroups) ++viaGroups;
        if (sizeOk && s[0x1aa] == 0 && !(a.result & 0xff) && (types || groups)) ++rejectedLate;
        delete types;
        delete groups;
    }
    if (accepted < 200 || viaTypes < 50 || viaGroups < 50 || rejectedLate < 50)
        ctx.Fail("PassesFilter: setup reaches too little (%d accepted, %d via types, %d via groups, %d late rejections)", accepted, viaTypes,
                 viaGroups, rejectedLate);
}

DIFF_TEST(cGZDBSegmentPackedFile_AdjustRefs, 0x9ad0f0)
{
    InitMapFakes();
    using Fn = void(__thiscall *)(void *, uint32_t, uint32_t, bool);
    auto orig = ctx.Original<Fn>(0x9ad0f0);
    Tables plain = MakeTables(ctx);
    std::mt19937 rng(0x9ad0f0);
    int erased = 0, inserted = 0, decremented = 0, incremented = 0, idle = 0;
    for (int i = 0; i < 1500; ++i) {
        Image s = MakeImage(rng);
        Wire(s);
        MapState f{rng() % 6 != 0, true, static_cast<uint32_t>(rng())};
        bool increment = rng() % 2;
        // Keys: mostly ones the maps hold (found in rebuilt copies), sometimes fresh ones.
        std::mt19937 probe(f.seed);
        RefMap *m1 = BuildRefMap(probe);
        RefMap *m2 = BuildRefMap(probe);
        auto pickKey = [&](RefMap *m) -> uint32_t {
            if (!m->empty() && rng() % 5 != 0) return std::next(m->begin(), rng() % m->size())->first;
            return rng() % 4 == 0 ? static_cast<uint32_t>(rng()) : static_cast<uint32_t>(rng() % 24);
        };
        uint32_t k1 = pickKey(m1), k2 = pickKey(m2);
        auto a = RunMaps(plain, s, f, [&](uint8_t *self) -> uint64_t { orig(self, k1, k2, increment); return 0; });
        auto b = RunMaps(plain, s, f, [&](uint8_t *self) -> uint64_t { Obj(self)->AdjustRefs(k1, k2, increment); return 0; });
        Compare(ctx, "AdjustRefs", a, b, b);
        if (!f.tracking) ++idle;
        else if (increment && m1->count(k1)) ++incremented;
        else if (increment) ++inserted;
        else if (m1->count(k1) && static_cast<int32_t>(m1->at(k1)) > 1) ++decremented;
        else if (m1->count(k1)) ++erased;
        delete m1;
        delete m2;
    }
    if (idle < 100 || erased < 50 || inserted < 100 || decremented < 50 || incremented < 100)
        ctx.Fail("AdjustRefs: setup reaches too little (%d idle, %d erased, %d inserted, %d decremented, %d incremented)", idle, erased, inserted,
                 decremented, incremented);
}

DIFF_TEST(cGZDBSegmentPackedFile_HashMapEmplace, 0x9a65b0)
{
    InitMapFakes();
    using Fn = void *(__thiscall *)(void *, void *, const uint32_t *);
    auto orig = ctx.Original<Fn>(0x9a65b0);
    Tables plain = MakeTables(ctx);
    std::mt19937 rng(0x9a65b0);
    int added = 0, existing = 0, grown = 0;
    for (int i = 0; i < 2000; ++i) {
        Image s = MakeImage(rng);
        MapState f{true, true, static_cast<uint32_t>(rng())};
        std::mt19937 preview(f.seed);
        RefMap *held = BuildRefMap(preview);
        uint32_t key = (!held->empty() && rng() % 3 == 0) ? std::next(held->begin(), rng() % held->size())->first
                       : (rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : static_cast<uint32_t>(rng() % 30));
        delete held;
        uint32_t pair[2] = {key, PickWord(rng)};
        // The result structure sits in the arena, pre-filled by the random image (its padding bytes must survive).
        auto run = [&](auto call) {
            return RunMaps(plain, s, f, [&](uint8_t *self) -> uint64_t {
                RefMap *map = MapAt(self, 0x1a0);
                uint8_t *out = self + kDst;
                call(map, out, pair);
                // The node is reported as its position in the table's iteration order.
                void *node;
                std::memcpy(&node, out, sizeof(node));
                int64_t position = -1, at = 0;
                for (auto it = map->begin(); it != map->end(); ++it, ++at) {
                    void *p;
                    std::memcpy(&p, &it, sizeof(p));
                    if (p == node) position = at;
                }
                std::memset(out, 0, 4); // the pointer itself differs between runs
                return static_cast<uint64_t>(position + 1);
            });
        };
        auto a = run([&](RefMap *map, uint8_t *out, const uint32_t *p) { orig(map, out, p); });
        auto b = run([&](RefMap *map, uint8_t *out, const uint32_t *p) {
            reinterpret_cast<HashMap32 *>(map)->Emplace(reinterpret_cast<HashMap32::EmplaceResult *>(out), p);
        });
        Compare(ctx, "HashMap32::Emplace", a, b, b);
        std::mt19937 probe(f.seed);
        RefMap *m1 = BuildRefMap(probe);
        size_t buckets = m1->bucket_count();
        bool present = m1->count(pair[0]) != 0;
        added += !present;
        existing += present;
        // The size before and the bucket count after tell whether the insertion grew the table.
        size_t marker = std::find(a.log.begin(), a.log.end(), -2) - a.log.begin();
        grown += static_cast<size_t>(a.log[marker + 4]) != buckets;
        delete m1;
    }
    if (added < 500 || existing < 200 || grown < 20) ctx.Fail("Emplace: setup reaches too little (%d added, %d existing, %d grown)", added, existing, grown);
}

DIFF_TEST(cGZDBSegmentPackedFile_RebuildRefs, 0x9ad3b0)
{
    InitMapFakes();
    ServiceScope service(ctx);
    using Fn = void(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9ad3b0);
    Tables plain = MakeTables(ctx);
    std::mt19937 rng(0x9ad3b0);
    int worked = 0, freedBlocks = 0;
    for (int i = 0; i < 1000; ++i) {
        Image s = MakeImage(rng);
        Wire(s);
        FillTrial(rng);
        MapState f{rng() % 5 != 0, true, static_cast<uint32_t>(rng())};
        auto a = RunMaps(plain, s, f, [&](uint8_t *self) -> uint64_t { orig(self); return 0; });
        auto b = RunMaps(plain, s, f, [&](uint8_t *self) -> uint64_t { Obj(self)->RebuildRefs(); return 0; });
        Compare(ctx, "RebuildRefs", a, b, b);
        worked += f.tracking && !gEntries.empty();
        for (size_t k = 0; k + 3 < a.log.size(); k += 3) freedBlocks += (a.log[k] == kSvcFree && a.log[k + 1] == 1);
    }
    if (worked < 500 || freedBlocks < 400) ctx.Fail("RebuildRefs: setup reaches too little (%d rebuilt, %d blocks freed)", worked, freedBlocks);
}

DIFF_TEST(cGZDBSegmentPackedFile_SetRefTracking, 0x9aacf0)
{
    InitMapFakes();
    ServiceScope service(ctx);
    using Fn = uint32_t(__thiscall *)(void *, uint8_t);
    using Member = uint32_t (cGZDBSegmentPackedFile::*)(uint8_t);
    auto orig = ctx.Original<Fn>(0x9aacf0);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[33] = Addr(static_cast<Member>(&cGZDBSegmentPackedFile::SetRefTracking));
    std::mt19937 rng(0x9aacf0);
    int disabled = 0, enabled = 0, unchanged = 0;
    for (int i = 0; i < 1000; ++i) {
        Image s = MakeImage(rng);
        Wire(s);
        FillTrial(rng);
        gUnlockResult = static_cast<uint32_t>(rng());
        static const uint8_t values[] = {0, 1, 2, 0x80, 0xff};
        uint8_t current = values[rng() % 5], wanted = rng() % 3 == 0 ? current : values[rng() % 5];
        // Maps exist when tracking is on (and sometimes when it is off, to see them deleted anyway).
        MapState f{false, current != 0 || rng() % 3 == 0, static_cast<uint32_t>(rng())};
        auto call = [&](auto fn) {
            return [&, fn](uint8_t *self) -> uint64_t {
                self[0x19f] = current;
                return fn(self);
            };
        };
        auto a = RunMaps(plain, s, f, call([&](uint8_t *self) { return orig(self, wanted); }));
        auto b = RunMaps(plain, s, f, call([&](uint8_t *self) { return Obj(self)->SetRefTracking(wanted); }));
        auto c = RunMaps(routed, s, f, call([&](uint8_t *self) { return VirtualSlot<Fn>(self, 33)(self, wanted); }));
        Compare(ctx, "SetRefTracking", a, b, c);
        if (wanted == current) ++unchanged;
        else if (wanted == 0) ++disabled;
        else ++enabled;
    }
    if (disabled < 100 || enabled < 100 || unchanged < 100) ctx.Fail("SetRefTracking: setup reaches too little (%d disabled, %d enabled, %d unchanged)", disabled, enabled, unchanged);
}

DIFF_TEST(cGZDBSegmentPackedFile_RecordStoreConstruct, 0x9a6cf0)
{
    WorldScope world(ctx);
    using Fn = void *(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9a6cf0);
    Tables plain = MakeTables(ctx);
    std::mt19937 rng(0x9a6cf0);
    for (int i = 0; i < 200; ++i) {
        ResetWorld();
        // Earlier allocations and frees, so that the store's blocks come from varying places.
        std::vector<void *> blocks;
        for (unsigned n = rng() % 20; n > 0; --n) blocks.push_back(RZMemory::Malloc(8 + rng() % 200));
        for (void *p : blocks) if (rng() % 2) RZMemory::Free(p);
        Image img = MakeImage(rng);
        std::memcpy(gArena, img.data(), kBytes);
        Snapshot snap;
        snap.Take();
        auto a = WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<uintptr_t>(orig(self + kStore)) - reinterpret_cast<uintptr_t>(self);
        });
        // Ours is cGZDBPFIndexModifiable's constructor through its hook's adapter: the object carries our vtable, which is the
        // one word normalised (to the game's) before the world is compared.
        void *const gameVTable = ctx.image.At<uint8_t>(cGZDBPFIndexModifiable::kVTable);
        auto b = WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
            auto *store = reinterpret_cast<cGZDBPFIndexModifiable *>(self + kStore);
            const uintptr_t r = reinterpret_cast<uintptr_t>(cGZDBPFIndexModifiableAdapters::Construct(store, nullptr));
            void **vtable = reinterpret_cast<void **>(self + kStore);
            if (*vtable == gameVTable || *vtable == nullptr) ctx.Fail("RecordStore::Construct: our object carries the game's vtable");
            *vtable = gameVTable;
            return r - reinterpret_cast<uintptr_t>(self);
        });
        WorldCompare(ctx, "RecordStore::Construct", a, b, b);
    }
}
