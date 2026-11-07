// Differential tests for cGZDBSegmentPackedFile: pending records, file space, record bytes.
// Fixtures (the arena, the fake store and file objects, the comparison helpers): gz_dbsegment_io_fixtures.h.
#include "gz_dbsegment_io_fixtures.h"

DIFF_TEST(cGZDBSegmentPackedFile_FlushOpenRecords, 0x9a8220)
{
    InitRecordFakes();
    using Fn = bool(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9a8220);
    Tables plain = MakeTables(ctx);
    InstallSub(plain, 24, reinterpret_cast<void *>(&RecClose24));
    std::mt19937 rng(0x9a8220);
    int flushed = 0, closed = 0, skippedBySize = 0, notOpen = 0;
    for (int i = 0; i < 800; ++i) {
        Image s = MakeImage(rng);
        Wire(s);
        std::vector<void *> addresses;
        PendingBuild b = MakeRecords(s, rng, rng() % 9, addresses);
        s[0x24] = PickFlag(rng);
        // The tree: records in key order; a null value sometimes.
        LocalPending map;
        for (size_t k = 0; k < b.keys.size(); ++k) map[b.keys[k]] = (rng() % 8 == 0) ? nullptr : addresses[k];
        uint32_t sizeField = rng() % 6 == 0 ? 0 : static_cast<uint32_t>(map.size());
        auto prepare = [&](uint8_t *self) {
            std::memcpy(self + 0x110, &map, 8);
            std::memcpy(self + 0x114, &sizeField, 4);
        };
        auto finish = [&](uint8_t *self) { std::memset(self + 0x110, 0, 8); };
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { prepare(self); uint64_t r = orig(self); finish(self); return r; });
        auto bb = Run(plain, s, [&](uint8_t *self) -> uint64_t { prepare(self); uint64_t r = Obj(self)->FlushOpenRecords(); finish(self); return r; });
        Compare(ctx, "FlushOpenRecords", a, bb, bb);
        if ((s[0x24] & 3) == 0) ++notOpen;
        else if (sizeField == 0) ++skippedBySize;
        else if (!map.empty()) { ++flushed; for (size_t k = 0; k + 2 < a.log.size(); k += 3) closed += a.log[k] == kClose24; }
    }
    if (flushed < 300 || closed < 500 || skippedBySize < 30 || notOpen < 100)
        ctx.Fail("FlushOpenRecords: setup reaches too little (%d flushed, %d closes, %d size-skipped, %d closed files)", flushed, closed, skippedBySize, notOpen);
}

DIFF_TEST(cGZDBSegmentPackedFile_DropRecord, 0x9a9420)
{
    InitRecordFakes();
    WorldScope world(ctx);
    using Fn = bool(__thiscall *)(void *, void *);
    using Member = bool (SegmentView::*)(void *);
    auto orig = ctx.Original<Fn>(0x9a9420);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.sub[40] = Addr(static_cast<Member>(&SegmentView::DoAbortRecord));
    std::mt19937 rng(0x9a9420);
    int dropped = 0, foreignSameKey = 0, foreignOtherKey = 0, closedFile = 0;
    for (int i = 0; i < 1200; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        Wire(img);
        std::vector<void *> addresses;
        unsigned n = 1 + rng() % 8;
        PendingBuild b = MakeRecords(img, rng, n, addresses);
        // Two records that are not in the tree: one with a key that is, one with another key.
        size_t foreignA = b.keys.size(), foreignB = b.keys.size() + 1;
        void *sameKey = MakeRecord(img, foreignA, b.keys[rng() % b.keys.size()]);
        void *otherKey = MakeRecord(img, foreignB, {7, 7, 7, static_cast<uint32_t>(rng())});
        static const uint8_t openStates[] = {1, 2, 3, 3, 0x83, 0xff};
        img[0x24] = rng() % 6 == 0 ? 0 : openStates[rng() % 6];
        std::memcpy(gArena, img.data(), kBytes);
        WorldPending *tree = new (gArena + 0x110) WorldPending();
        for (size_t k = 0; k < b.keys.size(); ++k) tree->emplace(b.keys[k], addresses[k]);
        void *handle;
        gKeyOverrideSelf = nullptr;
        switch (rng() % 5) {
        case 0: handle = static_cast<uint8_t *>(sameKey) + 4; break;
        case 1: handle = static_cast<uint8_t *>(otherKey) + 4; break;
        case 2:
            // A record that is in the tree, but whose key getter answers another record's key: the walk starts at
            // the other record and has to stop at the first key that differs.
            handle = static_cast<uint8_t *>(addresses[rng() % addresses.size()]) + 4;
            if (addresses.size() > 1) {
                gKeyOverrideSelf = handle;
                size_t other = rng() % b.keys.size();
                std::memcpy(gKeyOverrideKey, b.keys[other].data(), 16);
            }
            break;
        default: handle = static_cast<uint8_t *>(addresses[rng() % addresses.size()]) + 4; break;
        }
        Snapshot snap;
        snap.Take();
        auto a = WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t { return orig(self + 4, handle); });
        auto bb = WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t { return reinterpret_cast<SegmentView *>(self + 4)->DoAbortRecord(handle); });
        auto c = WorldRun(snap, routed, [&](uint8_t *self) -> uint64_t { return VirtualSlot<Fn>(self + 4, 40)(self + 4, handle); });
        WorldCompare(ctx, "DoAbortRecord", a, bb, c);
        dropped += (a.result & 0xff) != 0;
        foreignSameKey += handle == static_cast<uint8_t *>(sameKey) + 4;
        foreignOtherKey += handle == static_cast<uint8_t *>(otherKey) + 4;
        closedFile += (img[0x24] & 3) == 0;
    }
    if (dropped < 400 || foreignSameKey < 100 || foreignOtherKey < 100 || closedFile < 100)
        ctx.Fail("DoAbortRecord: setup reaches too little (%d dropped, %d same-key, %d other-key, %d closed)", dropped, foreignSameKey, foreignOtherKey, closedFile);
}

namespace {

// The distinct-count slots: the tree is a library map in this scope, its head and size copied into the object.
template <int Slot, typename Member>
void CountTest(difftest::Context &ctx, const char *name, uint32_t rva, Member member, uint32_t seed)
{
    InitRecordFakes();
    using Fn = uint32_t(__thiscall *)(void *);
    using M = uint32_t (cGZDBSegmentPackedFile::*)();
    auto orig = ctx.Original<Fn>(rva);
    Tables plain = MakeTables(ctx);
    Tables routed = MakeTables(ctx);
    routed.primary[Slot] = Addr(static_cast<M>(member));
    std::mt19937 rng(seed);
    int fromMap = 0, computed = 0, overlap = 0;
    for (int i = 0; i < 800; ++i) {
        Image s = MakeImage(rng);
        Wire(s);
        std::vector<void *> addresses;
        PendingBuild b = MakeRecords(s, rng, rng() % 9, addresses);
        LocalPending map;
        for (size_t k = 0; k < b.keys.size(); ++k) map[b.keys[k]] = addresses[k];
        gSetFill.clear();
        for (unsigned n = rng() % 6; n > 0; --n) gSetFill.push_back(rng() % 6);
        gSetFill2.clear();
        for (unsigned n = rng() % 6; n > 0; --n) gSetFill2.push_back(rng() % 6);
        MapState f{true, rng() % 3 == 0, static_cast<uint32_t>(rng())};
        auto call = [&](auto fn) {
            return [&, fn](uint8_t *self) -> uint64_t {
                std::memcpy(self + 0x110, &map, 8);
                uint64_t r = fn(self);
                std::memset(self + 0x110, 0, 8);
                return r;
            };
        };
        auto a = RunMaps(plain, s, f, call([&](uint8_t *self) { return orig(self); }));
        auto bb = RunMaps(plain, s, f, call([&](uint8_t *self) { return (Obj(self)->*member)(); }));
        auto c = RunMaps(routed, s, f, call([&](uint8_t *self) { return VirtualSlot<Fn>(self, Slot)(self); }));
        Compare(ctx, name, a, bb, c);
        if (f.hasMaps) ++fromMap; else ++computed;
        overlap += (!f.hasMaps && !gSetFill.empty() && !map.empty());
    }
    if (fromMap < 150 || computed < 400 || overlap < 300) ctx.Fail("%s: setup reaches too little (%d from the map, %d computed, %d with both sources)", name, fromMap, computed, overlap);
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_CountTypes, 0x9acfd0)
{
    CountTest<31>(ctx, "CountTypes", 0x9acfd0, &cGZDBSegmentPackedFile::CountTypes, 0x9acfd0);
}

DIFF_TEST(cGZDBSegmentPackedFile_CountGroups, 0x9aceb0)
{
    CountTest<32>(ctx, "CountGroups", 0x9aceb0, &cGZDBSegmentPackedFile::CountGroups, 0x9aceb0);
}

DIFF_TEST(cGZDBSegmentPackedFile_RecordSize, 0x9a98d0)
{
    InitRecordFakes();
    using Fn = uint32_t(__thiscall *)(void *, const uint32_t *);
    using Member = uint32_t (SegmentView::*)(const uint32_t *);
    auto orig = ctx.Original<Fn>(0x9a98d0);
    Tables plain = MakeTables(ctx);
    plain.primary[41] = reinterpret_cast<void *>(&RecCompare41);
    plain.sub[5] = reinterpret_cast<void *>(&RecOpen5);
    Tables routed = plain;
    routed.sub[36] = Addr(static_cast<Member>(&SegmentView::DoGetRecordSize));
    std::mt19937 rng(0x9a98d0);
    int pending = 0, comparedYes = 0, storeHit = 0, none = 0, closed = 0;
    for (int i = 0; i < 1500; ++i) {
        Image s = MakeImage(rng);
        Wire(s);
        std::vector<void *> addresses;
        PendingBuild b = MakeRecords(s, rng, rng() % 9, addresses);
        LocalPending map;
        for (size_t k = 0; k < b.keys.size(); ++k) map[b.keys[k]] = addresses[k];
        gRecSizes.clear();
        for (int k = 0; k < 4; ++k) gRecSizes.push_back(rng() % 3 == 0 ? 0xffffffffu : static_cast<uint32_t>(rng() % 5000));
        s[0x24] = PickFlag(rng);
        bool tryOpen = rng() % 2 == 0; // with a closed file, EnsureLoaded then calls the (fake) Open
        s[0x28] = tryOpen ? 1 + rng() % 3 : 0;
        s[0x29] = tryOpen ? static_cast<uint8_t>(rng() % 3) : 0;
        gOpensFile = rng() % 2;
        gCompare41 = rng() % 2;
        gFindRet = (rng() % 4 == 0) ? nullptr : static_cast<void *>(gArena + kHit);
        SetWord(s, kHit + 4, PickWord(rng));
        SetWord(s, kHit + 8, PickWord(rng));
        // The key: one in the tree (about half the time) or an arbitrary one.
        std::array<uint32_t, 4> key = (!b.keys.empty() && rng() % 2) ? b.keys[rng() % b.keys.size()] : SmallKey(rng);
        std::memcpy(s.data() + kRecIn, key.data(), 16);
        auto call = [&](auto fn) {
            return [&, fn](uint8_t *self) -> uint64_t {
                std::memcpy(self + 0x110, &map, 8);
                uint64_t r = fn(self);
                std::memset(self + 0x110, 0, 8);
                return r;
            };
        };
        auto a = Run(plain, s, call([&](uint8_t *self) { return orig(self + 4, reinterpret_cast<const uint32_t *>(self + kRecIn)); }));
        auto bb = Run(plain, s, call([&](uint8_t *self) {
            return reinterpret_cast<SegmentView *>(self + 4)->DoGetRecordSize(reinterpret_cast<const uint32_t *>(self + kRecIn));
        }));
        auto c = Run(routed, s, call([&](uint8_t *self) {
            return VirtualSlot<Fn>(self + 4, 36)(self + 4, reinterpret_cast<const uint32_t *>(self + kRecIn));
        }));
        Compare(ctx, "DoGetRecordSize", a, bb, c);
        bool open = (s[0x24] & 3) != 0, writable = (s[0x24] & 2) != 0;
        bool inTree = map.count(key) != 0;
        closed += !open;
        bool answered = open && writable && inTree && gRecSizes[0] != 0;  // (rough: some answer was not -1)
        (void)answered;
        for (size_t k = 0; k + 2 < a.log.size(); k += 3) {
            if (a.log[k] == kRec52) { ++pending; break; }
        }
        for (size_t k = 0; k + 2 < a.log.size(); k += 3) {
            if (a.log[k] == kCompare41 && gCompare41) { ++comparedYes; break; }
        }
        none += (a.result == 0xffffffffull);
        storeHit += (open && !gCompare41 && gFindRet != nullptr);
    }
    if (pending < 100 || comparedYes < 100 || storeHit < 100 || none < 100 || closed < 100)
        ctx.Fail("DoGetRecordSize: setup reaches too little (%d pending, %d compare, %d store, %d none, %d closed)", pending, comparedYes, storeHit, none, closed);
}

DIFF_TEST(cGZDBSegmentPackedFile_RangeTreeErase, 0x9b2bb0)
{
    WorldScope world(ctx);
    using Fn = void **(__thiscall *)(void *, void **, void *);
    auto orig = ctx.Original<Fn>(0x9b2bb0);
    Tables plain = MakeTables(ctx);
    std::mt19937 rng(0x9b2bb0);
    for (int i = 0; i < 1000; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        std::memcpy(gArena, img.data(), kBytes);
        auto ranges = BuildRanges(rng, 0x184, 14);
        if (ranges.empty()) continue;
        TestMap &map = *reinterpret_cast<TestMap *>(gArena + 0x184);
        auto victim = std::next(map.begin(), rng() % map.size());
        void *node = NodePointer(victim);
        Snapshot snap;
        snap.Take();
        auto a = WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<uintptr_t>(orig(self + 0x184, reinterpret_cast<void **>(self + kDst), node));
        });
        auto b = WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<uintptr_t>(reinterpret_cast<RangeTree *>(self + 0x184)->Erase(reinterpret_cast<void **>(self + kDst), node));
        });
        WorldCompare(ctx, "RangeTree::Erase", a, b, b);
    }
}

namespace {

// A tree of free ranges around a file of length `fileLength`: some inside, some outside, sometimes with the last
// one reaching exactly to the end.
struct SpaceScene {
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
    uint32_t fileLength;
};

SpaceScene BuildSpace(std::mt19937 &rng)
{
    SpaceScene s;
    s.ranges = BuildRanges(rng, 0x184, 12);
    uint32_t top = s.ranges.empty() ? 20 : s.ranges.back().first + s.ranges.back().second;
    switch (rng() % 5) {
    case 0: s.fileLength = top; break;                                   // the last range reaches the end exactly
    case 1: s.fileLength = top + 1 + rng() % 10; break;                  // everything inside, room after the last
    case 2: s.fileLength = top > 8 ? top - 1 - rng() % (top / 2) : top; break; // the end cuts into the ranges
    case 3: s.fileLength = 0; break;                                     // an empty file: every range is outside
    default: s.fileLength = 1 + rng() % (top + 20); break;
    }
    // Zero-length ranges: one at the file's end (outside, since its start is not below the length) and one inside.
    TestMap &map = *reinterpret_cast<TestMap *>(gArena + 0x184);
    if (rng() % 6 == 0) map.emplace(s.fileLength, 0);
    if (rng() % 6 == 0 && s.fileLength > 1) map.emplace(1 + rng() % (s.fileLength - 1), 0);
    return s;
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_AllocateSpace, 0x9a7e00)
{
    WorldScope world(ctx);
    InitSpaceFakes();
    using Fn = bool(__thiscall *)(void *, uint32_t *, uint32_t);
    auto orig = ctx.Original<Fn>(0x9a7e00);
    Tables plain = MakeTables(ctx);
    std::mt19937 rng(0x9a7e00);
    int viaFree = 0, grewLast = 0, grewEnd = 0, refused = 0, retried = 0, dropped = 0, notWritable = 0, split = 0;
    for (int i = 0; i < 2500; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        std::memcpy(gArena, img.data(), kBytes);
        SpaceScene sc = BuildSpace(rng);
        // The object's own fields, over the random image.
        static const uint8_t states[] = {0, 1, 2, 3, 3, 3, 2, 0x83};
        gArena[0x24] = states[rng() % 8];
        uint32_t flags = static_cast<uint32_t>(rng());
        if (rng() % 4 != 0) flags |= 2; else flags &= ~2u;
        std::memcpy(gArena + 0x17c, &flags, 4);
        auto put = [&](size_t at, const void *p) {
            uint32_t v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
            std::memcpy(gArena + at, &v, 4);
        };
        put(0x58, gFileTab);
        put(0x0c, gLockTab);
        put(0x30, gArena + kStore);
        put(kStore, gStoreTab);
        gLengths = {sc.fileLength};
        gSetLenAnswers.clear();
        for (int k = 0; k < 2; ++k) gSetLenAnswers.push_back(rng() % 3 == 0 ? 0 : 1);
        uint32_t length = rng() % 2 == 0 ? 5 + rng() % 40 : 1 + rng() % 6;
        Snapshot snap;
        snap.Take();
        auto run = [&](auto call) {
            return WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
                gSetLenIdx = 0;
                gLengthIdx = 0;
                return call(self);
            });
        };
        auto a = run([&](uint8_t *self) -> uint64_t { return orig(self, reinterpret_cast<uint32_t *>(self + kDst), length); });
        auto b = run([&](uint8_t *self) -> uint64_t { return Obj(self)->AllocateSpaceUnlocked(reinterpret_cast<uint32_t *>(self + kDst), length); });
        WorldCompare(ctx, "AllocateSpaceUnlocked", a, b, b);
        // Which paths ran (from the first run's log and result).
        bool writable = (gArena[0x24] & 2) != 0;
        (void)writable;
        int sets = 0;
        for (size_t k = 0; k + 2 < a.log.size(); k += 3) sets += a.log[k] == kSetLength;
        uint32_t sizeBefore = *reinterpret_cast<const uint32_t *>(snap.arena.data() + 0x188);
        uint32_t sizeAfter = *reinterpret_cast<const uint32_t *>(a.bytes.data() + 0x188);
        if ((snap.arena[0x24] & 2) == 0) { ++notWritable; continue; }
        if (a.log.empty()) continue;
        if (sets == 0 && (a.result & 0xff)) { ++viaFree; split += (sizeAfter == sizeBefore); }
        if (sets == 1 && (a.result & 0xff)) { if (sizeAfter + 1 == sizeBefore) ++grewLast; else ++grewEnd; }
        if (sets == 2) ++retried;
        if (!(a.result & 0xff)) ++refused;
        if (sizeAfter + 1 < sizeBefore && sets == 0) ++dropped;
    }
    if (viaFree < 150 || grewLast < 60 || grewEnd < 100 || refused < 100 || retried < 20 || dropped < 50 || split < 30 || notWritable < 200)
        ctx.Fail("AllocateSpaceUnlocked: setup reaches too little (%d free, %d grew-last, %d grew-end, %d refused, %d retried, %d dropped, %d split, %d not writable)",
                 viaFree, grewLast, grewEnd, refused, retried, dropped, split, notWritable);
}

DIFF_TEST(cGZDBSegmentPackedFile_CommitAndAllocate, 0x9a7d20)
{
    WorldScope world(ctx);
    InitSpaceFakes();
    gMagicAddress = ctx.image.At(0xe5f440);
    using Fn = bool(__thiscall *)(void *, uint32_t *, uint32_t);
    using Member = bool (cGZDBSegmentPackedFile::*)(uint32_t *, uint32_t);
    auto orig = ctx.Original<Fn>(0x9a7d20);
    Tables plain = MakeTables(ctx);
    plain.primary[58] = reinterpret_cast<void *>(&RecWrite58);
    Tables routed = plain;
    routed.primary[44] = Addr(static_cast<Member>(&cGZDBSegmentPackedFile::AllocateSpace));
    std::mt19937 rng(0x9a7d20);
    int committed = 0, wroteHeader = 0, firstPending = 0, secondPending = 0, plainPath = 0;
    for (int i = 0; i < 2000; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        std::memcpy(gArena, img.data(), kBytes);
        SpaceScene sc = BuildSpace(rng);
        auto put = [&](size_t at, const void *p) {
            uint32_t v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
            std::memcpy(gArena + at, &v, 4);
        };
        auto putWord = [&](size_t at, uint32_t v) { std::memcpy(gArena + at, &v, 4); };
        put(0x58, gFileTab);
        put(0x0c, gLockTab);
        gArena[0x24] = 3;
        // +0x17c: bit 1 (use the free list) and bit 3 (commit), each present three times in four.
        uint32_t flags = static_cast<uint32_t>(rng()) & ~0xau;
        if (rng() % 4 != 0) flags |= 2;
        if (rng() % 4 != 0) flags |= 8;
        putWord(0x17c, flags);
        gArena[0x178] = rng() % 3 == 0;
        gArena[0x19d] = rng() % 3 == 0;
        gArena[0x34] = rng() % 3 == 0;
        // The pending extents: zero length half the time; offsets small so that they touch the existing ranges.
        auto extent = [&](size_t at, size_t lengthAt) {
            putWord(at, rng() % 30);
            putWord(lengthAt, rng() % 2 ? 0 : 1 + rng() % 6);
        };
        extent(0x140, 0x144);
        extent(0x14c, 0x150);
        gLengths = {sc.fileLength};
        gSetLenAnswers.clear();
        for (int k = 0; k < 2; ++k) gSetLenAnswers.push_back(rng() % 3 == 0 ? 0 : 1);
        uint32_t length = rng() % 2 == 0 ? 5 + rng() % 40 : 1 + rng() % 6;
        Snapshot snap;
        snap.Take();
        auto run = [&](const Tables &t, auto call) {
            return WorldRun(snap, t, [&](uint8_t *self) -> uint64_t {
                gSetLenIdx = 0;
                gLengthIdx = 0;
                return call(self);
            });
        };
        auto a = run(plain, [&](uint8_t *self) -> uint64_t { return orig(self, reinterpret_cast<uint32_t *>(self + kDst), length); });
        auto b = run(plain, [&](uint8_t *self) -> uint64_t { return Obj(self)->AllocateSpace(reinterpret_cast<uint32_t *>(self + kDst), length); });
        auto c = run(routed, [&](uint8_t *self) -> uint64_t { return VirtualSlot<Fn>(self, 44)(self, reinterpret_cast<uint32_t *>(self + kDst), length); });
        WorldCompare(ctx, "AllocateSpace", a, b, c);
        bool bits = (flags & 0xa) == 0xa;
        committed += bits;
        plainPath += !bits;
        for (size_t k = 0; k + 2 < a.log.size(); k += 3) {
            if (a.log[k] == kWrite58) ++wroteHeader;
        }
        firstPending += bits && snap.arena[0x150] + snap.arena[0x151] + snap.arena[0x152] + snap.arena[0x153] != 0;
        secondPending += bits && snap.arena[0x144] + snap.arena[0x145] + snap.arena[0x146] + snap.arena[0x147] != 0;
    }
    if (committed < 1000 || wroteHeader < 200 || firstPending < 300 || secondPending < 300 || plainPath < 300)
        ctx.Fail("AllocateSpace: setup reaches too little (%d committed, %d header writes, %d + %d pending extents, %d plain)", committed, wroteHeader,
                 firstPending, secondPending, plainPath);
}

// ---- The record walk (vf37) ----

namespace {

constexpr size_t kHitBase = 0x700; // 12 hit records of 16 bytes for the walk
std::vector<std::pair<bool, size_t>> gVisitAnswers; // slot 37: (answer, object index or -1 as a large value)
size_t gVisitIdx = 0;

bool __fastcall RecVisit37(void *self, void *, const uint32_t *entry, void **object, uint32_t one)
{
    Push(kVisit37, Off(self), one);
    for (int i = 0; i < 4; ++i) gLog.push_back(entry[i]);
    auto answer = gVisitAnswers[gVisitIdx++ % gVisitAnswers.size()];
    *object = answer.second == size_t(-1) ? nullptr : static_cast<void *>(gArena + kRecBase + answer.second * kRecStride);
    return answer.first;
}

void __fastcall RecObj6(void *self, void *)
{
    Push(kObj6, Off(self), 0);
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_VisitAllRecords, 0x9b0c70)
{
    InitRecordFakes();
    ServiceScope service(ctx);
    gStoreTab[10] = reinterpret_cast<void *>(&RecFill10);
    gRecPrim[6] = reinterpret_cast<void *>(&RecObj6);
    using Fn = bool(__thiscall *)(void *);
    using Member = bool (cGZDBSegmentPackedFile::*)();
    auto orig = ctx.Original<Fn>(0x9b0c70);
    Tables plain = MakeTables(ctx);
    plain.sub[37] = reinterpret_cast<void *>(&RecVisit37);
    plain.sub[5] = reinterpret_cast<void *>(&RecOpen5);
    Tables routed = plain;
    routed.primary[37] = Addr(static_cast<Member>(&cGZDBSegmentPackedFile::VisitAllRecords));
    std::mt19937 rng(0x9b0c70);
    int visited = 0, withData = 0, failed = 0, stoppedEarly = 0, closedFiles = 0;
    for (int i = 0; i < 1500; ++i) {
        Image s = MakeImage(rng);
        Wire(s);
        // Twelve object slots (primary vtable only) and twelve hit records.
        for (size_t k = 0; k < 12; ++k) SetPointer(s, kRecBase + k * kRecStride, gRecPrim);
        FillTrial(rng);
        gFindRets.clear();
        for (size_t k = 0; k < 12; ++k) {
            uint32_t start = rng() % 100, end = rng() % 3 == 0 ? start : start + (rng() % 3) ;
            uint32_t topBit = (rng() % 2) << 31;
            SetWord(s, kHitBase + 16 * k + 4, start);
            SetWord(s, kHitBase + 16 * k + 8, (end & 0x7fffffffu) | topBit);
            gFindRets.push_back(gArena + kHitBase + 16 * k);
        }
        gVisitAnswers.clear();
        for (unsigned k = 0; k < 4; ++k) gVisitAnswers.push_back({rng() % 5 != 0, rng() % 6 == 0 ? size_t(-1) : static_cast<size_t>(rng() % 12)});
        // A successful visit with no object would call slot 6 on null (ORIGINAL-BUG): give those an object.
        for (auto &answer : gVisitAnswers) if (answer.first && answer.second == size_t(-1)) answer.second = 0;
        s[0x24] = PickFlag(rng);
        bool tryOpen = rng() % 2 == 0;
        s[0x28] = tryOpen ? 1 + rng() % 3 : 0;
        s[0x29] = tryOpen ? static_cast<uint8_t>(rng() % 3) : 0;
        gOpensFile = rng() % 2;
        auto run = [&](const Tables &t, auto call) {
            return Run(t, s, [&](uint8_t *self) -> uint64_t {
                gFindIdx = 0;
                gVisitIdx = 0;
                return call(self);
            });
        };
        auto a = run(plain, [&](uint8_t *self) -> uint64_t { return orig(self); });
        auto b = run(plain, [&](uint8_t *self) -> uint64_t { return Obj(self)->VisitAllRecords(); });
        auto c = run(routed, [&](uint8_t *self) -> uint64_t { return VirtualSlot<Fn>(self, 37)(self); });
        Compare(ctx, "VisitAllRecords", a, b, c);
        int visits = 0, obj6 = 0;
        for (size_t k = 0; k + 2 < a.log.size(); ++k) {
            if (a.log[k] == kVisit37) ++visits;
            if (a.log[k] == kObj6) ++obj6;
        }
        visited += visits > 0;
        withData += obj6 > 0;
        failed += !(a.result & 0xff);
        stoppedEarly += visits > 0 && !(a.result & 0xff);
        closedFiles += (s[0x24] & 3) == 0;
    }
    gFindRets.clear();
    if (visited < 500 || withData < 300 || failed < 300 || stoppedEarly < 100 || closedFiles < 200)
        ctx.Fail("VisitAllRecords: setup reaches too little (%d visited, %d with an object, %d false, %d stopped early, %d closed)", visited, withData, failed,
                 stoppedEarly, closedFiles);
}

// ---- Replacing the record store (0x9ad750) ----

namespace {

constexpr int64_t kOldDelete = 90, kOld1 = 91, kOld3 = 92, kOld19 = 93, kNew18 = 94;
uint8_t gOldSlot1 = 0;
uint32_t gOldCount = 0;
uint32_t gFillSeed = 0;

struct Entry28 {
    uint32_t w[7];
};

void __fastcall RecOldDelete(void *self, void *, uint32_t flags)
{
    Push(kOldDelete, Off(self), flags);
}

uint8_t __fastcall RecOldSlot1(void *self, void *)
{
    Push(kOld1, Off(self), 0);
    return gOldSlot1;
}

uint32_t __fastcall RecOldCount(void *self, void *)
{
    Push(kOld3, Off(self), 0);
    return gOldCount;
}

void __fastcall RecOldFill(void *self, void *, Entry28 *entries, void *ns)
{
    Push(kOld19, Off(self), static_cast<int64_t>(reinterpret_cast<uintptr_t>(ns)));
    for (uint32_t i = 0; i < gOldCount; ++i)
        for (uint32_t j = 0; j < 7; ++j) entries[i].w[j] = gFillSeed + 0x1000 * i + j;
}

// The new store is a real RecordStore (game vtable): its slot 18 is patched in the image.
void __fastcall RecNew18(void *, void *, Entry28 *entries, uint32_t count, void *ns)
{
    Push(kNew18, count, static_cast<int64_t>(reinterpret_cast<uintptr_t>(ns)));
    for (uint32_t i = 0; i < count; ++i)
        for (uint32_t j = 0; j < 7; ++j) gLog.push_back(entries[i].w[j]);
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_EnsureStore, 0x9ad750)
{
    WorldScope world(ctx);
    InitFakes();
    SlotPatch p18(ctx, cGZDBPFIndexModifiable::kVTable, 18, reinterpret_cast<void *>(&RecNew18));
    OurStoreSlotPatch o18(18, reinterpret_cast<void *>(&RecNew18));
    gStoreTab[0] = reinterpret_cast<void *>(&RecOldDelete);
    gStoreTab[1] = reinterpret_cast<void *>(&RecOldSlot1);
    gStoreTab[3] = reinterpret_cast<void *>(&RecOldCount);
    gStoreTab[19] = reinterpret_cast<void *>(&RecOldFill);
    using Fn = void(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9ad750);
    Tables plain = MakeTables(ctx);
    std::mt19937 rng(0x9ad750);
    int noStore = 0, kept = 0, replacedEmpty = 0, replacedFilled = 0;
    for (int i = 0; i < 1200; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        Wire(img);
        bool hasOld = rng() % 4 != 0;
        if (!hasOld) SetWord(img, 0x30, 0);
        std::memcpy(gArena, img.data(), kBytes);
        gOldSlot1 = rng() % 4 == 0 ? static_cast<uint8_t>(1 + rng() % 3) : 0;
        gOldCount = rng() % 4 == 0 ? 0 : rng() % 7;
        gFillSeed = static_cast<uint32_t>(rng());
        Snapshot snap;
        snap.Take();
        // The new store's address is a heap address: the pointer word is replaced by 0 (new store) or kept.
        auto run = [&](auto call) {
            return WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
                call(self);
                uint32_t now;
                std::memcpy(&now, self + 0x30, 4);
                uint32_t before;
                std::memcpy(&before, snap.arena.data() + 0x30, 4);
                if (now != before) {
                    const uint32_t marker = 0xdeadbeefu;
                    std::memcpy(self + 0x30, &marker, 4);
                }
                return 0;
            });
        };
        auto a = run([&](uint8_t *self) { orig(self); });
        auto b = run([&](uint8_t *self) { Obj(self)->EnsureStore(); });
        WorldCompare(ctx, "EnsureStore", a, b, b);
        if (!hasOld) ++noStore;
        else if (gOldSlot1) ++kept;
        else if (gOldCount == 0) ++replacedEmpty;
        else ++replacedFilled;
    }
    if (noStore < 200 || kept < 100 || replacedEmpty < 100 || replacedFilled < 300)
        ctx.Fail("EnsureStore: setup reaches too little (%d no store, %d kept, %d replaced empty, %d replaced with entries)", noStore, kept, replacedEmpty, replacedFilled);
}

DIFF_TEST(cGZDBSegmentPackedFile_WriteRecordData, 0x9b23b0)
{
    WorldScope world(ctx);
    InitRecordFakes();
    SlotPatch p13(ctx, cGZDBPFIndexModifiable::kVTable, 13, reinterpret_cast<void *>(&RecStore13));
    OurStoreSlotPatch o13(13, reinterpret_cast<void *>(&RecStore13));
    SlotPatch p14(ctx, cGZDBPFIndexModifiable::kVTable, 14, reinterpret_cast<void *>(&RecStore14));
    OurStoreSlotPatch o14(14, reinterpret_cast<void *>(&RecStore14));
    gStoreTab[1] = reinterpret_cast<void *>(&RecStoreCurrent);
    gStoreTab[13] = reinterpret_cast<void *>(&RecStore13);
    gStoreTab[14] = reinterpret_cast<void *>(&RecStore14);
    using Fn = bool(__thiscall *)(void *, const uint32_t *, const void *, uint32_t);
    using Member = bool (cGZDBSegmentPackedFile::*)(const uint32_t *, const void *, uint32_t);
    auto orig = ctx.Original<Fn>(0x9b23b0);
    Tables plain = MakeTables(ctx);
    plain.primary[44] = reinterpret_cast<void *>(&RecAlloc44);
    plain.primary[58] = reinterpret_cast<void *>(&RecWrite58b);
    plain.sub[5] = reinterpret_cast<void *>(&RecOpen5);
    Tables routed = plain;
    routed.primary[29] = Addr(static_cast<Member>(&cGZDBSegmentPackedFile::WriteRecordData));
    std::mt19937 rng(0x9b23b0);
    int stored = 0, writeFailed = 0, allocFailed = 0, removedOld = 0, pendingKey = 0, notWritable = 0, newStore = 0, returned = 0;
    for (int i = 0; i < 2500; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        Wire(img);
        std::vector<void *> addresses;
        PendingBuild b = MakeRecords(img, rng, rng() % 5, addresses);
        bool noStore = rng() % 5 == 0;
        if (noStore) SetWord(img, 0x30, 0);
        static const uint8_t states[] = {0, 1, 2, 3, 3, 3, 3, 0x83};
        img[0x24] = states[rng() % 8];
        bool tryOpen = rng() % 3 == 0; // a closed file with auto-open bytes makes EnsureLoaded call the (fake) Open
        img[0x28] = tryOpen ? 1 + rng() % 3 : 0;
        img[0x29] = tryOpen ? static_cast<uint8_t>(rng() % 3) : 0;
        gOpensFile = rng() % 2;
        gOpenMask = rng() % 2 ? 3 : 1;
        std::memcpy(gArena, img.data(), kBytes);
        WorldPending *tree = new (gArena + 0x110) WorldPending();
        for (size_t k = 0; k < b.keys.size(); ++k) tree->emplace(b.keys[k], addresses[k]);
        BuildRanges(rng, 0x184, 6);
        BuildRanges(rng, 0x18c, 4); // (AddFreeRangeTo may pick any of the three trees)
        BuildRanges(rng, 0x194, 4);
        gArena[0x19f] = rng() % 2; // reference counting on or off
        gArena[0x19d] = 0;
        gArena[0x34] = 0;
        gRemoveOk = rng() % 2;
        for (auto &w : gRemoved) w = rng() % 3 == 0 ? PickWord(rng) : static_cast<uint32_t>(rng() % 40);
        gAllocOk = rng() % 5 != 0;
        gAllocOffset = rng() % 8 == 0 ? 0 : static_cast<uint32_t>(rng() % 100);
        gWriteOk = rng() % 3 != 0;
        uint32_t size = rng() % 6 == 0 ? 0 : (rng() % 3 == 0 ? (0x80000000u | static_cast<uint32_t>(rng() % 50)) : static_cast<uint32_t>(1 + rng() % 60));
        std::array<uint32_t, 4> key = (!b.keys.empty() && rng() % 3 == 0) ? b.keys[rng() % b.keys.size()] : SmallKey(rng);
        std::memcpy(gArena + kRecIn, key.data(), 16);
        uint32_t mapSeed = static_cast<uint32_t>(rng());
        Snapshot snap;
        snap.Take();
        // The reference-count maps are heap objects built per run; the new store (when EnsureStore makes one) too.
        auto run = [&](const Tables &t, auto call) {
            return WorldRun(snap, t, [&](uint8_t *self) -> uint64_t {
                std::mt19937 build(mapSeed);
                RefMap *first = BuildRefMap(build);
                RefMap *second = BuildRefMap(build);
                std::memcpy(self + 0x1a0, &first, 4);
                std::memcpy(self + 0x1a4, &second, 4);
                uint64_t r = call(self);
                DescribeMap(first, gLog);
                DescribeMap(second, gLog);
                delete first;
                delete second;
                std::memset(self + 0x1a0, 0, 8);
                uint32_t now, before;
                std::memcpy(&now, self + 0x30, 4);
                std::memcpy(&before, snap.arena.data() + 0x30, 4);
                if (now != before) {
                    const uint32_t marker = 0xdeadbeefu;
                    std::memcpy(self + 0x30, &marker, 4);
                }
                return r;
            });
        };
        const uint32_t *keyPointer = nullptr;
        auto a = run(plain, [&](uint8_t *self) -> uint64_t {
            keyPointer = reinterpret_cast<const uint32_t *>(self + kRecIn);
            return orig(self, keyPointer, self + 0x390, size);
        });
        auto bb = run(plain, [&](uint8_t *self) -> uint64_t {
            return Obj(self)->WriteRecordData(reinterpret_cast<const uint32_t *>(self + kRecIn), self + 0x390, size);
        });
        auto c = run(routed, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<Fn>(self, 29)(self, reinterpret_cast<const uint32_t *>(self + kRecIn), self + 0x390, size);
        });
        WorldCompare(ctx, "WriteRecordData", a, bb, c);
        bool writable = (snap.arena[0x24] & 3) != 0 && (snap.arena[0x24] & 2) != 0;
        bool inTree = tree->count(key) != 0;
        bool sawStore14 = false, sawAlloc = false, sawWrite = false, sawStore13 = false;
        for (int64_t v : a.log) {
            sawStore14 |= v == kStore14;
            sawAlloc |= v == kAlloc44;
            sawWrite |= v == kWrite58b;
            sawStore13 |= v == kStore13;
        }
        notWritable += !writable;
        pendingKey += writable && inTree;
        newStore += writable && !inTree && noStore;
        removedOld += sawStore14 && gRemoveOk;
        allocFailed += sawAlloc && !gAllocOk;
        writeFailed += sawWrite && !gWriteOk;
        stored += sawStore13;
        returned += (a.result & 0xff) != 0;
    }
    if (stored < 300 || writeFailed < 100 || allocFailed < 100 || removedOld < 300 || pendingKey < 50 || notWritable < 400 || newStore < 50)
        ctx.Fail("WriteRecordData: setup reaches too little (%d stored, %d write failures, %d allocation failures, %d removed, %d pending, %d not writable, %d new stores)",
                 stored, writeFailed, allocFailed, removedOld, pendingKey, notWritable, newStore);
}

DIFF_TEST(cGZDBSegmentPackedFile_RemoveRecord, 0x9a9760)
{
    WorldScope world(ctx);
    InitRecordFakes();
    SlotPatch p13(ctx, cGZDBPFIndexModifiable::kVTable, 13, reinterpret_cast<void *>(&RecStore13));
    OurStoreSlotPatch o13(13, reinterpret_cast<void *>(&RecStore13));
    SlotPatch p14(ctx, cGZDBPFIndexModifiable::kVTable, 14, reinterpret_cast<void *>(&RecStore14));
    OurStoreSlotPatch o14(14, reinterpret_cast<void *>(&RecStore14));
    gStoreTab[1] = reinterpret_cast<void *>(&RecStoreCurrent);
    gStoreTab[14] = reinterpret_cast<void *>(&RecStore14);
    using Fn = bool(__thiscall *)(void *, const uint32_t *);
    using Member = bool (SegmentView::*)(const uint32_t *);
    auto orig = ctx.Original<Fn>(0x9a9760);
    Tables plain = MakeTables(ctx);
    plain.sub[5] = reinterpret_cast<void *>(&RecOpen5);
    Tables routed = plain;
    routed.sub[41] = Addr(static_cast<Member>(&SegmentView::DoDeleteRecord));
    std::mt19937 rng(0x9a9760);
    int removedOld = 0, absent = 0, pendingKey = 0, notWritable = 0, newStore = 0;
    for (int i = 0; i < 2000; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        Wire(img);
        std::vector<void *> addresses;
        PendingBuild b = MakeRecords(img, rng, rng() % 5, addresses);
        bool noStore = rng() % 5 == 0;
        if (noStore) SetWord(img, 0x30, 0);
        static const uint8_t states[] = {0, 1, 2, 3, 3, 3, 3, 0x83};
        img[0x24] = states[rng() % 8];
        bool tryOpen = rng() % 3 == 0;
        img[0x28] = tryOpen ? 1 + rng() % 3 : 0;
        img[0x29] = tryOpen ? static_cast<uint8_t>(rng() % 3) : 0;
        gOpensFile = rng() % 2;
        gOpenMask = rng() % 2 ? 3 : 1;
        std::memcpy(gArena, img.data(), kBytes);
        WorldPending *tree = new (gArena + 0x110) WorldPending();
        for (size_t k = 0; k < b.keys.size(); ++k) tree->emplace(b.keys[k], addresses[k]);
        BuildRanges(rng, 0x184, 6);
        BuildRanges(rng, 0x18c, 4);
        BuildRanges(rng, 0x194, 4);
        gArena[0x19f] = rng() % 2;
        gArena[0x19d] = 0;
        gArena[0x34] = 0;
        gRemoveOk = rng() % 2;
        for (auto &w : gRemoved) w = rng() % 3 == 0 ? PickWord(rng) : static_cast<uint32_t>(rng() % 40);
        std::array<uint32_t, 4> key = (!b.keys.empty() && rng() % 3 == 0) ? b.keys[rng() % b.keys.size()] : SmallKey(rng);
        std::memcpy(gArena + kRecIn, key.data(), 16);
        uint32_t mapSeed = static_cast<uint32_t>(rng());
        Snapshot snap;
        snap.Take();
        auto run = [&](const Tables &t, auto call) {
            return WorldRun(snap, t, [&](uint8_t *self) -> uint64_t {
                std::mt19937 build(mapSeed);
                RefMap *first = BuildRefMap(build);
                RefMap *second = BuildRefMap(build);
                std::memcpy(self + 0x1a0, &first, 4);
                std::memcpy(self + 0x1a4, &second, 4);
                uint64_t r = call(self);
                DescribeMap(first, gLog);
                DescribeMap(second, gLog);
                delete first;
                delete second;
                std::memset(self + 0x1a0, 0, 8);
                uint32_t now, before;
                std::memcpy(&now, self + 0x30, 4);
                std::memcpy(&before, snap.arena.data() + 0x30, 4);
                if (now != before) {
                    const uint32_t marker = 0xdeadbeefu;
                    std::memcpy(self + 0x30, &marker, 4);
                }
                return r;
            });
        };
        auto a = run(plain, [&](uint8_t *self) -> uint64_t { return orig(self + 4, reinterpret_cast<const uint32_t *>(self + kRecIn)); });
        auto bb = run(plain, [&](uint8_t *self) -> uint64_t {
            return reinterpret_cast<SegmentView *>(self + 4)->DoDeleteRecord(reinterpret_cast<const uint32_t *>(self + kRecIn));
        });
        auto c = run(routed, [&](uint8_t *self) -> uint64_t {
            return VirtualSlot<Fn>(self + 4, 41)(self + 4, reinterpret_cast<const uint32_t *>(self + kRecIn));
        });
        WorldCompare(ctx, "DoDeleteRecord", a, bb, c);
        bool writable = (snap.arena[0x24] & 3) != 0 && (snap.arena[0x24] & 2) != 0;
        bool inTree = tree->count(key) != 0;
        notWritable += !writable;
        pendingKey += writable && inTree;
        newStore += writable && !inTree && noStore;
        removedOld += (a.result & 0xff) != 0;
        absent += writable && !inTree && !(a.result & 0xff);
    }
    if (removedOld < 300 || absent < 100 || pendingKey < 50 || notWritable < 400 || newStore < 50)
        ctx.Fail("DoDeleteRecord: setup reaches too little (%d removed, %d absent, %d pending, %d not writable, %d new stores)", removedOld, absent, pendingKey, notWritable, newStore);
}
