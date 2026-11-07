// Differential tests for cGZDBSegmentPackedFile: Open, the cached index, Flush and committing records.
// Fixtures (the arena, the fake store and file objects, the comparison helpers): gz_dbsegment_io_fixtures.h.
#include "gz_dbsegment_io_fixtures.h"

DIFF_TEST(cGZDBSegmentPackedFile_WriteHeader, 0x9b1950)
{
    InitHeaderFakes();
    HeaderGlobal global(ctx);
    using Fn = uint8_t(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9b1950);
    Tables plain = MakeTables(ctx);
    std::mt19937 rng(0x9b1950);
    int readOnly = 0, notOpen = 0, clean = 0, seekFailed = 0, written = 0, writeFailed = 0;
    for (int i = 0; i < 1500; ++i) {
        Image s = MakeImage(rng);
        WireClose(s);
        static const uint8_t states[] = {0, 1, 2, 3, 3, 0x82, 0xff};
        s[0x24] = states[rng() % 7];
        s[0x178] = rng() % 3 ? 1 + rng() % 200 : 0;
        *global.global = rng() % 3 ? 0x1234 : static_cast<uint32_t>(rng());
        gFile4Answer = rng() % 4 ? 1 + rng() % 200 : 0;
        gSeekLands = rng() % 4 != 0;
        gWriteAnswer = rng() % 4 ? 1 + rng() % 200 : 0;
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self) & 0xff; });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { return Obj(self)->WriteHeader() & 0xff; });
        Compare(ctx, "WriteHeader", a, b, b);
        bool writable = (s[0x24] & 2) != 0;
        readOnly += !writable;
        notOpen += writable && gFile4Answer == 0;
        clean += writable && gFile4Answer != 0 && s[0x178] == 0;
        seekFailed += writable && gFile4Answer != 0 && s[0x178] != 0 && !gSeekLands;
        bool wrote = false;
        for (size_t k = 0; k + 2 < a.log.size(); k += 3) wrote |= a.log[k] == kWrite16;
        written += wrote && gWriteAnswer != 0;
        writeFailed += wrote && gWriteAnswer == 0;
    }
    if (readOnly < 100 || notOpen < 50 || clean < 50 || seekFailed < 50 || written < 300 || writeFailed < 50)
        ctx.Fail("WriteHeader: setup reaches too little (%d read-only, %d not open, %d clean, %d seek failures, %d written, %d write failures)", readOnly, notOpen, clean,
                 seekFailed, written, writeFailed);
}

DIFF_TEST(cGZDBSegmentPackedFile_InitHeader, 0x9b17e0)
{
    InitHeaderFakes();
    HeaderGlobal global(ctx);
    using Fn = uint8_t(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9b17e0);
    Tables plain = MakeTables(ctx);
    std::mt19937 rng(0x9b17e0);
    int written = 0, notWritten = 0, lengthAnswers = 0;
    for (int i = 0; i < 1500; ++i) {
        Image s = MakeImage(rng);
        WireClose(s);
        static const uint8_t states[] = {0, 1, 2, 3, 3, 0x82, 0xff};
        s[0x24] = states[rng() % 7];
        *global.global = rng() % 3 ? 0x1234 : static_cast<uint32_t>(rng());
        gFile4Answer = rng() % 4 ? 1 + rng() % 200 : 0;
        gSeekLands = rng() % 4 != 0;
        gWriteAnswer = rng() % 4 ? 1 + rng() % 200 : 0;
        gSetLenAnswers.assign(1, rng() % 3 ? static_cast<uint8_t>(1 + rng() % 200) : 0);
        gSetLenIdx = 0;
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self) & 0xff; });
        gSetLenIdx = 0;
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { return Obj(self)->InitHeader() & 0xff; });
        Compare(ctx, "InitHeader", a, b, b);
        bool wrote = false, sized = false;
        for (size_t k = 0; k + 2 < a.log.size(); k += 3) {
            wrote |= a.log[k] == kWrite16;
            sized |= a.log[k] == kSetLength;
        }
        written += sized;
        notWritten += !sized;
        lengthAnswers += sized && a.result == 0;
        (void)wrote;
    }
    if (written < 300 || notWritten < 300 || lengthAnswers < 30) ctx.Fail("InitHeader: setup reaches too little (%d sized, %d not written, %d zero answers)", written, notWritten, lengthAnswers);
}

namespace {

constexpr int64_t kOlder = 130, kFresh = -5;
uint8_t gOlderAnswers[4] = {0, 0, 0, 0};

template <int Which> uint8_t __fastcall FakeOlder(void *self, void *)
{
    Push(kOlder, Off(self), Which);
    if (Which == 3 && gLoadCreatesStore) {
        uint32_t store = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gArena + kStore));
        std::memcpy(gArena + 0x30, &store, sizeof(store));
    }
    return gOlderAnswers[Which];
}

struct OlderFakes {
    Redirect r0, r1, r2, r3, o0, o1, o2, o3;
    explicit OlderFakes(difftest::Context &ctx)
        : r0(ctx, 0x9af2a0, reinterpret_cast<void *>(&FakeOlder<0>)), r1(ctx, 0x9af590, reinterpret_cast<void *>(&FakeOlder<1>)),
          r2(ctx, 0x9af390, reinterpret_cast<void *>(&FakeOlder<2>)), r3(ctx, 0x9aea00, reinterpret_cast<void *>(&FakeOlder<3>)),
          o0(Addr(&cGZDBSegmentPackedFile::ReadHeader), reinterpret_cast<void *>(&FakeOlder<0>)),
          o1(Addr(&cGZDBSegmentPackedFile::ReadIndex), reinterpret_cast<void *>(&FakeOlder<1>)),
          o2(Addr(&cGZDBSegmentPackedFile::ReadHoles), reinterpret_cast<void *>(&FakeOlder<2>)),
          o3(Addr(&cGZDBSegmentPackedFile::ReadCompressionDirectory), reinterpret_cast<void *>(&FakeOlder<3>))
    {
    }
};

// A record store the call made (not the fake one): destroyed here through its own vtable so that the world ends up
// balanced, and replaced by a marker in the object.
void RetireFreshStore(uint8_t *self)
{
    uint32_t store;
    std::memcpy(&store, self + 0x30, sizeof(store));
    if (store == 0 || store == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gArena + kStore))) return;
    gLog.push_back(kFresh);
    void *object = reinterpret_cast<void *>(static_cast<uintptr_t>(store));
    VirtualSlot<void(__thiscall *)(void *, uint32_t)>(object, 0)(object, 1);
    uint32_t marker = 0xBEEF0000u;
    std::memcpy(self + 0x30, &marker, sizeof(marker));
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_Open, 0x9ad830)
{
    WorldScope world(ctx);
    InitHeaderFakes();
    HelperFakes helpers(ctx);
    OlderFakes older(ctx);
    HeaderGlobal global(ctx);
    using Fn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t);
    using Member = uint8_t (SegmentView::*)(uint32_t, uint32_t);
    auto orig = ctx.Original<Fn>(0x9ad830);
    Tables plain = MakeTables(ctx);
    Tables routed = plain;
    routed.sub[5] = Addr(static_cast<Member>(&SegmentView::Open));
    std::mt19937 rng(0x9ad830);
    int refused = 0, openFailed = 0, indexRead = 0, legacy = 0, fresh = 0, failedAll = 0, extended = 0, committed = 0, readOnlyOk = 0, emptyWritable = 0, emptyReadOnly = 0;
    for (int i = 0; i < 8000; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        WireClose(img);
        static const uint8_t states[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3};
        img[0x24] = states[rng() % 13];
        SetWord(img, 0x4c, rng() % 8 == 0 ? 0 : 1 + rng() % 40);
        img[0x1c] = rng() % 8 == 0 ? 0 : 1 + rng() % 3;
        img[0x2b] = rng() % 3 ? 1 : 0;
        bool nullStore = rng() % 2 == 0;
        if (nullStore) SetWord(img, 0x30, 0);
        gLoadCreatesStore = nullStore || rng() % 2;
        // Extents nearLen the file length.
        gLengths.clear();
        gLengths.push_back(rng() % 4 == 0 ? 0 : 1 + rng() % 400);
        gLengths.push_back(rng() % 5 == 0 ? PickWord(rng) : 1 + rng() % 600);
        auto nearLen = [&]() { return rng() % 3 ? static_cast<uint32_t>(rng() % 700) : PickWord(rng); };
        SetWord(img, 0x140, nearLen());
        SetWord(img, 0x144, nearLen());
        SetWord(img, 0x14c, nearLen());
        SetWord(img, 0x150, nearLen());
        std::memcpy(gArena, img.data(), kBytes);
        auto ranges = BuildRanges(rng, 0x184, 5);
        auto ranges2 = BuildRanges(rng, 0x18c, 4);
        (void)ranges2;
        // A last free range that reaches past the other extents now and then.
        if (rng() % 2) reinterpret_cast<TestMap *>(gArena + 0x184)->emplace(100 + rng() % 1200, 1 + rng() % 300);
        gLoadIndexAnswer = rng() % 2 ? 1 + rng() % 200 : 0;
        for (int k = 0; k < 4; ++k) gOlderAnswers[k] = rng() % 5 ? 1 + rng() % 200 : 0;
        if (rng() % 2) std::memset(gOlderAnswers, 1, 4);
        gOpen3Answer = rng() % 5 ? 1 + rng() % 200 : 0;
        gStore6Answer = rng() % 3 ? static_cast<uint32_t>(rng() % 700) : PickWord(rng);
        gFile4Answer = rng() % 10 ? 1 : 0;
        gSeekLands = rng() % 10 != 0;
        gWriteAnswer = rng() % 10 ? 1 : 0;
        gSetLenAnswers.assign(1, rng() % 6 ? static_cast<uint8_t>(1 + rng() % 200) : 0);
        *global.global = rng() % 3 ? 0x1234 : static_cast<uint32_t>(rng());
        uint32_t read = (static_cast<uint32_t>(rng()) & ~0xffu) | (rng() % 3 == 0 ? 0 : 1 + rng() % 255);
        uint32_t write = (static_cast<uint32_t>(rng()) & ~0xffu) | (rng() % 3 == 0 ? 0 : 1 + rng() % 255);
        Snapshot snap;
        snap.Take();
        auto run = [&](const Tables &t, auto fn) {
            return WorldRun(snap, t, [&](uint8_t *self) -> uint64_t {
                gLengthIdx = 0;
                gSetLenIdx = 0;
                gCopyIdx = 0;
                uint64_t r = fn(self) & 0xff;
                RetireFreshStore(self);
                return r;
            });
        };
        std::memset(gKindHits, 0, sizeof(gKindHits));
        auto a = run(plain, [&](uint8_t *self) -> uint64_t { return orig(self + 4, read, write); });
        int aHits[256];
        std::memcpy(aHits, gKindHits, sizeof(aHits));
        auto b = run(plain, [&](uint8_t *self) -> uint64_t { return reinterpret_cast<SegmentView *>(self + 4)->Open(read, write); });
        auto c = run(routed, [&](uint8_t *self) -> uint64_t { return VirtualSlot<Fn>(self + 4, 5)(self + 4, read, write); });
        WorldCompare(ctx, "Open", a, b, c);
        auto saw = [&](int64_t kind) { return kind < 0 ? std::find(a.log.begin(), a.log.end(), kind) != a.log.end() : aHits[kind] != 0; };
        bool didOpen = saw(kOpen3);
        refused += !didOpen;
        openFailed += didOpen && a.log.size() > 0 && !saw(kFileLength) && !saw(kOlder);
        indexRead += saw(kLoadIndexCall) && gLoadIndexAnswer != 0;
        legacy += saw(kOlder);
        fresh += saw(kFresh);
        failedAll += didOpen && saw(kFileClose5);
        extended += saw(kSetLength) && saw(kFileLength) && !saw(kWrite16);
        committed += saw(kCommitCall);
        readOnlyOk += didOpen && (write & 0xff) == 0 && a.result != 0;
        emptyWritable += didOpen && gLengths[0] == 0 && (write & 0xff) != 0;
        emptyReadOnly += didOpen && gLengths[0] == 0 && (write & 0xff) == 0;
        (void)ranges;
    }
    if (refused < 300 || indexRead < 300 || legacy < 300 || fresh < 80 || failedAll < 300 || extended < 120 || committed < 800 || readOnlyOk < 100 || emptyWritable < 150 || emptyReadOnly < 100)
        ctx.Fail("Open: setup reaches too little (%d refused, %d index reads, %d legacy, %d fresh, %d failed, %d extended, %d commits, %d read-only ok, %d empty writable, %d empty read-only)",
                 refused, indexRead, legacy, fresh, failedAll, extended, committed, readOnlyOk, emptyWritable, emptyReadOnly);
    (void)openFailed;
}

DIFF_TEST(cGZDBSegmentPackedFile_LoadCachedIndex, 0x9ae5c0)
{
    WorldScope world(ctx);
    ServiceScope service(ctx);
    HeaderGlobal global(ctx);
    InitCloseFakes();
    InitMapFakes();
    gCacheTab[22] = reinterpret_cast<void *>(&CacheOpen);
    gCacheRecordTab[2] = reinterpret_cast<void *>(&CacheRelease);
    gCacheRecordTab[6] = reinterpret_cast<void *>(&CacheClose);
    gCacheRecordTab[8] = reinterpret_cast<void *>(&CacheRead);
    gNsTab[3] = reinterpret_cast<void *>(&NsMap);
    gNsTab[4] = reinterpret_cast<void *>(&NsMap);
    gGameCompressedVTable = ctx.image.At(cGZDBPFIndexCompressed::kVTable);
    gGameStaticVTable = ctx.image.At(cGZDBPFIndexStatic::kVTable);
    using Fn = uint8_t(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9ae5c0);
    Tables plain = MakeTables(ctx);
    plain.sub[29] = reinterpret_cast<void *>(&ChangeId);
    std::mt19937 rng(0x9ae5c0);
    int loaded = 0, compressed = 0, statics = 0, noCache = 0, refused = 0, badVersion = 0, badStamp = 0, badHeader = 0, cut = 0,
        badHoles = 0, tracked = 0, repeats = 0;
    int edgeLoaded[5] = {};
    for (int i = 0; i < 3000; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        WireClose(img);
        const uint32_t length = 0x200 + rng() % 0x10000;
        gLengths.assign(1, length);
        const bool hasCache = rng() % 12 != 0;
        SetPointer(img, 0x14, hasCache ? gArena + kCache : nullptr);
        SetPointer(img, kCache, gCacheTab);
        SetPointer(img, kCacheRecord, gCacheRecordTab);
        SetPointer(img, kNs, gNsTab);
        SetPointer(img, 0x18, rng() % 3 == 0 ? gArena + kNs : nullptr);
        if (rng() % 3 == 0) SetWord(img, 0x30, 0);
        gCacheOpenAnswer = rng() % 10 == 0 ? 0 : static_cast<uint8_t>(1 + rng() % 200);
        gCacheOpenSetsOut = gCacheOpenAnswer != 0 || rng() % 2;
        gChangeIdAnswer = PickWord(rng);
        *global.global = 0x46504244;

        // The cache entry.
        const int broken = rng() % 4 == 0 ? static_cast<int>(1 + rng() % 7) : 0;
        gCacheStream.clear();
        PutWord(gCacheStream, broken == 1 ? 2 + rng() % 3 : 3);
        PutWord(gCacheStream, broken == 2 ? gChangeIdAnswer ^ (1 + rng() % 0xff) : gChangeIdAnswer);
        const uint32_t count = rng() % 2 ? 0x40 + rng() % 90 : rng() % 0x40;
        const uint32_t holes = rng() % 6;
        uint32_t header[24] = {};
        header[0] = broken == 3 ? 0x12345678 : 0x46504244;
        header[1] = rng() % 2;
        header[2] = rng() % 3;
        for (int k = 3; k < 9; ++k) header[k] = static_cast<uint32_t>(rng());
        header[8] = 7;
        header[9] = count;
        header[10] = 0x60 + rng() % (length / 2);
        header[11] = rng() % (length - header[10]);
        header[12] = holes;
        header[13] = rng() % 5 == 0 ? 0 : (rng() % 8 == 0 ? rng() % 0x60 : 0x60 + rng() % (length - 0x60));
        if (broken == 4) header[13] = 1 + rng() % 0x5f; // inside the file, below the header: CheckExtents takes it
        header[14] = broken == 5 ? (holes + 1) * 8 : holes * 8;
        if (header[13] + header[14] > length) header[13] = 0;
        // The edges of the final check (0x60 and the length; offset 0x5f is CheckExtents' to refuse or take).
        const int edge = rng() % 6 == 0 ? static_cast<int>(rng() % 4) : -1;
        if (edge == 0) header[13] = 0x60;
        if (edge == 1) header[13] = 0x5f;
        if (edge == 2) header[13] = length;
        if (edge == 3) header[13] = length - header[14];
        header[15] = 1 + rng() % 2;
        for (int k = 16; k < 24; ++k) header[k] = rng() % 4 == 0 ? static_cast<uint32_t>(rng()) : 0;
        for (uint32_t w : header) PutWord(gCacheStream, w);
        const uint32_t types = 1 + rng() % 4, groups = 1 + rng() % 4;
        for (uint32_t k = 0; k < count; ++k) {
            PutWord(gCacheStream, 0x1000 + rng() % types);
            PutWord(gCacheStream, 0x2000 + rng() % groups);
            PutWord(gCacheStream, static_cast<uint32_t>(rng()));
            PutWord(gCacheStream, rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : 0);
            PutWord(gCacheStream, 0x60 + rng() % length);
            PutWord(gCacheStream, rng() % 10 == 0 ? 0x10000 + rng() % 0x10000 : rng() % 0x2000);
            PutWord(gCacheStream, (rng() % 10 == 0 ? 0x10000 + rng() % 0x10000 : rng() % 0x3000) | (rng() % 2 ? 0x80000000u : 0));
        }
        const uint32_t given = holes + (rng() % 4 == 0 ? rng() % 3 : 0);
        PutWord(gCacheStream, given);
        uint32_t start = 0x60 + rng() % 0x100;
        bool repeated = false;
        for (uint32_t k = 0; k < given; ++k) {
            if (k > 0 && rng() % 6 == 0) {
                repeated = true; // the same start again: the first one keeps its length
            } else {
                start += 1 + rng() % 0x80;
            }
            PutWord(gCacheStream, start);
            PutWord(gCacheStream, 1 + rng() % 0x40);
        }
        gCacheLimit = broken == 6 || broken == 7 ? rng() % gCacheStream.size() : gCacheStream.size();

        std::memcpy(gArena, img.data(), kBytes);
        BuildRanges(rng, 0x184, 5);
        BuildRanges(rng, 0x18c, 4);
        const bool track = rng() % 3 == 0;
        gArena[0x19f] = track ? 1 : 0;
        const uint32_t mapSeed = static_cast<uint32_t>(rng());
        Snapshot snap;
        snap.Take();
        auto run = [&](auto fn) {
            return WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
                gCachePos = 0;
                gLengthIdx = 0;
                uint64_t r = WithMaps(self, track, mapSeed, [&](uint8_t *s) -> uint64_t { return fn(s) & 0xff; });
                DescribeIndex(self);
                return r;
            });
        };
        auto a = run([&](uint8_t *self) -> uint64_t { return orig(self); });
        auto b = run([&](uint8_t *self) -> uint64_t { return Obj(self)->LoadCachedIndex(); });
        WorldCompare(ctx, "LoadCachedIndex", a, b, b);
        const bool opened = hasCache && gCacheOpenAnswer != 0;
        const auto kindAt = std::find(a.log.begin(), a.log.end(), kIndexKind);
        const int64_t kind = kindAt + 1 < a.log.end() ? *(kindAt + 1) : -9;
        loaded += a.result != 0;
        compressed += kind == 2;
        statics += kind == 1;
        noCache += !hasCache;
        refused += hasCache && gCacheOpenAnswer == 0;
        badVersion += broken == 1 && opened;
        badStamp += broken == 2 && opened;
        badHeader += broken == 3 && a.result == 0;
        cut += (broken == 6 || broken == 7) && opened && a.result == 0;
        badHoles += (broken == 4 || broken == 5) && kind > 0;
        tracked += track && kind > 0;
        repeats += repeated && a.result != 0;
        edgeLoaded[edge + 1] += edge >= 0 && a.result != 0;
    }
    std::printf("  LoadCachedIndex edges loaded: 0x60 %d, 0x5f %d, at the length %d, below it %d\n", edgeLoaded[1], edgeLoaded[2],
                edgeLoaded[3], edgeLoaded[4]);
    std::printf("  LoadCachedIndex coverage: loaded %d, compressed %d, static %d, no cache %d, refused %d, bad version %d, bad stamp %d, "
                "bad header %d, cut %d, bad holes %d, tracked %d, repeated starts %d\n",
                loaded, compressed, statics, noCache, refused, badVersion, badStamp, badHeader, cut, badHoles, tracked, repeats);
    if (loaded < 800 || compressed < 500 || statics < 500 || noCache < 100 || refused < 100 || badVersion < 50 || badStamp < 50 ||
        badHeader < 50 || cut < 100 || badHoles < 50 || tracked < 300 || repeats < 100)
        ctx.Fail("LoadCachedIndex: setup reaches too little");
}

namespace {

// SaveCachedIndex, 0x9b0670: the same cache and record fakes as LoadCachedIndex (OpenRecord answering or not, with or
// without the out pointer), plus the record's SetSize (slot 11), Write (slot 9: logs the size and every byte written, as
// words; the trial's write number k fails) and Abort (slot 7). The index at +0x30 is the store fake whose CopyEntries
// (slot 19) logs the namespace and fills the entries from the trial's seed. The free-range tree at +0x184 has 0..6 ranges.
// Compared: the calls (with everything written), the arena and the world's allocator and core.
constexpr int64_t kCacheSetSize = 170, kCacheWrite = 171, kCacheAbort = 172, kStoreCopy19 = 173;
int gWriteFailAt = -1, gWriteNumber = 0;
uint32_t gCopySeed = 0;

uint32_t __fastcall CacheSetSize(void *self, void *, uint32_t size)
{
    Push(kCacheSetSize, Off(self), size);
    return 1;
}

uint8_t __fastcall CacheWrite(void *self, void *, const void *from, uint32_t size)
{
    Push(kCacheWrite, Off(self), size);
    const int number = gWriteNumber++;
    if (number == gWriteFailAt) return 0;
    const uint8_t *bytes = static_cast<const uint8_t *>(from);
    for (uint32_t k = 0; k < size; k += 4) {
        uint32_t w = 0;
        std::memcpy(&w, bytes + k, (std::min)(4u, size - k));
        gLog.push_back(w);
    }
    return 1;
}

uint8_t __fastcall CacheAbort(void *self, void *)
{
    Push(kCacheAbort, Off(self), 0);
    return 1;
}

void __fastcall StoreCopy19(void *self, void *, uint32_t *out, void *ns)
{
    Push(kStoreCopy19, Off(self), ns == nullptr ? 0 : Off(ns));
    uint32_t count;
    std::memcpy(&count, gArena + 0x13c, sizeof(count));
    std::mt19937 fill(gCopySeed);
    for (uint32_t k = 0; k < count * 7; ++k) out[k] = static_cast<uint32_t>(fill());
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_SaveCachedIndex, 0x9b0670)
{
    WorldScope world(ctx);
    InitCloseFakes();
    gCacheTab[22] = reinterpret_cast<void *>(&CacheOpen);
    gCacheRecordTab[2] = reinterpret_cast<void *>(&CacheRelease);
    gCacheRecordTab[6] = reinterpret_cast<void *>(&CacheClose);
    gCacheRecordTab[7] = reinterpret_cast<void *>(&CacheAbort);
    gCacheRecordTab[9] = reinterpret_cast<void *>(&CacheWrite);
    gCacheRecordTab[11] = reinterpret_cast<void *>(&CacheSetSize);
    gStoreTab[19] = reinterpret_cast<void *>(&StoreCopy19);
    using Fn = void(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9b0670);
    Tables plain = MakeTables(ctx);
    plain.sub[29] = reinterpret_cast<void *>(&ChangeId);
    std::mt19937 rng(0x9b0670);
    int written = 0, aborted = 0, noCache = 0, refused = 0, noEntries = 0, noRanges = 0;
    for (int i = 0; i < 2000; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        WireClose(img);
        const bool hasCache = rng() % 12 != 0;
        SetPointer(img, 0x14, hasCache ? gArena + kCache : nullptr);
        SetPointer(img, kCache, gCacheTab);
        SetPointer(img, kCacheRecord, gCacheRecordTab);
        SetPointer(img, 0x18, rng() % 3 == 0 ? gArena + kNs : nullptr);
        const uint32_t count = rng() % 4 == 0 ? 0 : 1 + rng() % 60;
        SetWord(img, 0x13c, count);
        gCacheOpenAnswer = rng() % 10 == 0 ? 0 : static_cast<uint8_t>(1 + rng() % 200);
        gCacheOpenSetsOut = gCacheOpenAnswer != 0 || rng() % 2;
        gChangeIdAnswer = PickWord(rng);
        gCopySeed = static_cast<uint32_t>(rng());
        gWriteFailAt = rng() % 3 == 0 ? static_cast<int>(rng() % 6) : -1;
        std::memcpy(gArena, img.data(), kBytes);
        auto ranges = BuildRanges(rng, 0x184, rng() % 4 == 0 ? 0 : 6);
        Snapshot snap;
        snap.Take();
        auto run = [&](auto fn) {
            return WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
                gWriteNumber = 0;
                fn(self);
                return 0;
            });
        };
        auto a = run([&](uint8_t *self) { orig(self); });
        auto b = run([&](uint8_t *self) { Obj(self)->SaveCachedIndex(); });
        WorldCompare(ctx, "SaveCachedIndex", a, b, b);
        const bool opened = hasCache && gCacheOpenAnswer != 0;
        const int writes = count != 0 ? 6 : 5;
        written += opened && (gWriteFailAt < 0 || gWriteFailAt >= writes);
        aborted += opened && gWriteFailAt >= 0 && gWriteFailAt < writes;
        noCache += !hasCache;
        refused += hasCache && gCacheOpenAnswer == 0;
        noEntries += opened && count == 0;
        noRanges += opened && ranges.empty();
    }
    std::printf("  SaveCachedIndex coverage: written %d, aborted %d, no cache %d, refused %d, no entries %d, no ranges %d\n", written,
                aborted, noCache, refused, noEntries, noRanges);
    if (written < 800 || aborted < 300 || noCache < 80 || refused < 80 || noEntries < 200 || noRanges < 200)
        ctx.Fail("SaveCachedIndex: setup reaches too little");
}

namespace {

constexpr int64_t kRemove41 = 140, kWriteDir43 = 141, kCountRanges47 = 142, kCompact36 = 143, kVersion48 = 144, kStore22 = 145, kStore23 = 146,
                  kStep1 = 147, kStep2 = 148;
uint8_t gDirWriteAnswer = 1, gStep1Answer = 1, gStep2Answer = 1;
uint32_t gCountReported = 0, gTotalReported = 0, gVersionAnswer = 0;
std::vector<uint32_t> gDirWords;

bool __fastcall Remove41(void *self, void *, const uint32_t *key)
{
    Push(kRemove41, Off(self), 0);
    for (int i = 0; i < 4; ++i) gLog.push_back(key[i]);
    return true;
}

uint8_t __fastcall WriteDir43(void *self, void *, const uint32_t *key, const uint32_t *data, uint32_t size)
{
    Push(kWriteDir43, Off(self), size);
    for (int i = 0; i < 4; ++i) gLog.push_back(key[i]);
    for (uint32_t i = 0; i < size / 4; ++i) gLog.push_back(data[i]);
    return gDirWriteAnswer;
}

bool __fastcall CountRanges47(void *self, void *, uint32_t *count, uint32_t *total)
{
    Push(kCountRanges47, Off(self), 0);
    *count = gCountReported;
    *total = gTotalReported;
    return true;
}

uint8_t __fastcall Compact36(void *self, void *)
{
    Push(kCompact36, Off(self), 0);
    return 1;
}

uint32_t __fastcall Version48(void *self, void *)
{
    Push(kVersion48, Off(self), 0);
    return gVersionAnswer;
}

void __fastcall StoreFill22(void *self, void *, std::vector<uint32_t> *vec, void *ns, uint32_t version)
{
    Push(kStore22, Off(self), version);
    gLog.push_back(ns == nullptr ? 0 : 1);
    vec->assign(gDirWords.begin(), gDirWords.end());
}

void __fastcall Store23(void *self, void *)
{
    Push(kStore23, Off(self), 0);
}

template <int Which> uint8_t __fastcall FakeStep(void *self, void *)
{
    Push(Which == 1 ? kStep1 : kStep2, Off(self), 0);
    return Which == 1 ? gStep1Answer : gStep2Answer;
}

struct StepFakes {
    Redirect s1, s2, o1, o2;
    explicit StepFakes(difftest::Context &ctx)
        : s1(ctx, 0x9b1d50, reinterpret_cast<void *>(&FakeStep<1>)), s2(ctx, 0x9b1a70, reinterpret_cast<void *>(&FakeStep<2>)),
          o1(Addr(&cGZDBSegmentPackedFile::WriteIndex), reinterpret_cast<void *>(&FakeStep<1>)),
          o2(Addr(&cGZDBSegmentPackedFile::WriteHoles), reinterpret_cast<void *>(&FakeStep<2>))
    {
    }
};

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_Flush, 0x9ab4f0)
{
    WorldScope world(ctx);
    ServiceScope service(ctx);
    InitHeaderFakes();
    StepFakes steps(ctx);
    HeaderGlobal global(ctx);
    using Fn = uint8_t(__thiscall *)(void *);
    using Member = uint8_t (SegmentView::*)();
    auto orig = ctx.Original<Fn>(0x9ab4f0);
    Tables plain = MakeTables(ctx);
    plain.sub[13] = reinterpret_cast<void *>(&SubLock13);
    plain.sub[14] = reinterpret_cast<void *>(&SubUnlock14);
    plain.sub[41] = reinterpret_cast<void *>(&Remove41);
    plain.sub[43] = reinterpret_cast<void *>(&WriteDir43);
    plain.sub[5] = reinterpret_cast<void *>(&RecOpen5);
    plain.primary[47] = reinterpret_cast<void *>(&CountRanges47);
    plain.primary[36] = reinterpret_cast<void *>(&Compact36);
    plain.primary[48] = reinterpret_cast<void *>(&Version48);
    gStoreTab[10] = reinterpret_cast<void *>(&RecFill10);
    gStoreTab[22] = reinterpret_cast<void *>(&StoreFill22);
    gStoreTab[23] = reinterpret_cast<void *>(&Store23);
    Tables routed = plain;
    routed.sub[10] = Addr(static_cast<Member>(&SegmentView::Flush));
    std::mt19937 rng(0x9ab4f0);
    int idle = 0, listed = 0, dirWritten = 0, dirFailed = 0, compacted = 0, ratioSeen = 0, stepFailed1 = 0, stepFailed2 = 0, headerFailed = 0, slot18Failed = 0, slot19Failed = 0,
        done = 0, swapped = 0, changedKind = 0;
    for (int i = 0; i < 4000; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        WireClose(img);
        static const uint8_t states[] = {0, 1, 2, 3, 3, 3, 3, 0x82};
        img[0x24] = states[rng() % 8];
        img[0x34] = rng() % 3 ? 1 + rng() % 200 : 0;
        img[0x19d] = rng() % 3 ? 0 : 1 + rng() % 200;
        img[0x178] = rng() % 3 ? 0 : 1 + rng() % 200;
        img[0x19c] = rng() % 2 ? 1 + rng() % 200 : 0;
        SetWord(img, 0x17c, rng() % 2 ? (static_cast<uint32_t>(rng()) | 1u) : (static_cast<uint32_t>(rng()) & ~1u));
        SetWord(img, 0x114, rng() % 3 ? 0 : 1 + rng() % 5);
        gLengths.assign(1, 100 + rng() % 1000);
        uint32_t length = gLengths[0];
        gTotalReported = rng() % 6 == 0 ? static_cast<uint32_t>(length + rng() % 5) : static_cast<uint32_t>(rng() % length);
        gCountReported = static_cast<uint32_t>(rng() % 20);
        float threshold = static_cast<float>(rng() % 100) / 50.0f;
        // Sometimes exactly the ratio, to tell "above" from "at or above".
        if (gTotalReported < length && rng() % 4 == 0) threshold = static_cast<float>(gTotalReported) / static_cast<float>(length - gTotalReported);
        std::memcpy(img.data() + 0x180, &threshold, sizeof(threshold));
        SetWord(img, 0xfc, static_cast<uint32_t>(rng()));
        SetWord(img, 0x100, static_cast<uint32_t>(rng()));
        std::memcpy(gArena, img.data(), kBytes);
        BuildRanges(rng, 0x184, 5);
        BuildRanges(rng, 0x18c, 5);
        BuildRanges(rng, 0x194, 5);
        FillTrial(rng);
        for (auto &e : gEntries)
            if (rng() % 3) e[3] = 0;
        gDirWords.clear();
        for (unsigned n = rng() % 9; n > 0; --n) gDirWords.push_back(static_cast<uint32_t>(rng()));
        gVersionAnswer = static_cast<uint32_t>(rng());
        gDirWriteAnswer = rng() % 4 ? 1 + rng() % 200 : 0;
        gStep1Answer = rng() % 5 ? 1 + rng() % 200 : 0;
        gStep2Answer = rng() % 5 ? 1 + rng() % 200 : 0;
        gFile4Answer = rng() % 5 ? 1 : 0;
        gSeekLands = rng() % 5 != 0;
        gWriteAnswer = rng() % 5 ? 1 : 0;
        gFile18Answer = rng() % 5 ? 1 + rng() % 200 : 0;
        gFile19Answer = rng() % 5 ? 1 + rng() % 200 : 0;
        *global.global = 0x1234;
        Snapshot snap;
        snap.Take();
        auto run = [&](const Tables &t, auto fn) {
            return WorldRun(snap, t, [&](uint8_t *self) -> uint64_t {
                gLengthIdx = 0;
                gFilledBlock = nullptr;
                return fn(self) & 0xff;
            });
        };
        std::memset(gKindHits, 0, sizeof(gKindHits));
        auto a = run(plain, [&](uint8_t *self) -> uint64_t { return orig(self + 4); });
        int aHits[256];
        std::memcpy(aHits, gKindHits, sizeof(aHits));
        auto b = run(plain, [&](uint8_t *self) -> uint64_t { return reinterpret_cast<SegmentView *>(self + 4)->Flush(); });
        auto c = run(routed, [&](uint8_t *self) -> uint64_t { return VirtualSlot<Fn>(self + 4, 10)(self + 4); });
        WorldCompare(ctx, "Flush", a, b, c);
        auto saw = [&](int64_t kind) { return kind < 0 ? std::find(a.log.begin(), a.log.end(), kind) != a.log.end() : aHits[kind] != 0; };
        idle += !saw(kFill10);
        listed += saw(kFill10);
        dirWritten += saw(kWriteDir43) && gDirWriteAnswer != 0;
        dirFailed += saw(kWriteDir43) && gDirWriteAnswer == 0;
        compacted += saw(kCompact36);
        ratioSeen += saw(kCountRanges47);
        stepFailed1 += saw(kStep1) && gStep1Answer == 0;
        stepFailed2 += saw(kStep2) && gStep2Answer == 0;
        headerFailed += saw(kStep2) && gStep2Answer != 0 && !saw(kFile18) && saw(kFile4);
        slot18Failed += saw(kFile18) && gFile18Answer == 0;
        slot19Failed += saw(kFile19) && gFile19Answer == 0;
        done += saw(kStore23);
        swapped += saw(kStore23) && img[0x19c] != 0;
        changedKind += saw(kRemove41);
    }
    if (idle < 300 || listed < 1000 || dirWritten < 200 || dirFailed < 50 || compacted < 30 || ratioSeen < 300 || stepFailed1 < 50 || stepFailed2 < 50 || headerFailed < 20 ||
        slot18Failed < 20 || slot19Failed < 20 || done < 300 || swapped < 100 || changedKind < 300)
        ctx.Fail("Flush: setup reaches too little (%d idle, %d listed, %d dir written, %d dir failed, %d compacted, %d ratio, %d step1 failed, %d step2 failed, %d header failed, "
                 "%d slot18, %d slot19, %d done, %d swapped, %d removals)",
                 idle, listed, dirWritten, dirFailed, compacted, ratioSeen, stepFailed1, stepFailed2, headerFailed, slot18Failed, slot19Failed, done, swapped, changedKind);
}

DIFF_TEST(cGZDBSegmentPackedFile_CommitRecord, 0x9a9520)
{
    InitRecordFakes();
    WorldScope world(ctx);
    Redirect commitHelper(ctx, 0x9b1fe0, reinterpret_cast<void *>(&FakeCommitRecord));
    Redirect commitHelperOurs(Addr(&cGZDBSegmentPackedFile::StoreWrittenRecord), reinterpret_cast<void *>(&FakeCommitRecord));
    gRecSub[5] = reinterpret_cast<void *>(&RecSub5);
    using Fn = bool(__thiscall *)(void *, void *);
    using Member = bool (SegmentView::*)(void *);
    auto orig = ctx.Original<Fn>(0x9a9520);
    Tables plain = MakeTables(ctx);
    Tables routed = plain;
    routed.sub[39] = Addr(static_cast<Member>(&SegmentView::DoCloseRecord));
    std::mt19937 rng(0x9a9520);
    int committed = 0, plainDrop = 0, foreignSameKey = 0, foreignOtherKey = 0, closedFile = 0;
    for (int i = 0; i < 1500; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        Wire(img);
        std::vector<void *> addresses;
        unsigned n = 1 + rng() % 8;
        PendingBuild b = MakeRecords(img, rng, n, addresses);
        size_t foreignA = b.keys.size(), foreignB = b.keys.size() + 1;
        void *sameKey = MakeRecord(img, foreignA, b.keys[rng() % b.keys.size()]);
        void *otherKey = MakeRecord(img, foreignB, {7, 7, 7, static_cast<uint32_t>(rng())});
        static const uint8_t openStates[] = {1, 2, 3, 3, 0x83, 0xff};
        img[0x24] = rng() % 6 == 0 ? 0 : openStates[rng() % 6];
        std::memcpy(gArena, img.data(), kBytes);
        WorldPending *tree = new (gArena + 0x110) WorldPending();
        for (size_t k = 0; k < b.keys.size(); ++k) tree->emplace(b.keys[k], addresses[k]);
        gRecFlagAnswers.clear();
        for (int k = 0; k < 4; ++k) gRecFlagAnswers.push_back(rng() % 2 ? static_cast<uint8_t>(rng()) : static_cast<uint8_t>(rng() % 4));
        void *handle;
        gKeyOverrideSelf = nullptr;
        switch (rng() % 5) {
        case 0: handle = static_cast<uint8_t *>(sameKey) + 4; break;
        case 1: handle = static_cast<uint8_t *>(otherKey) + 4; break;
        case 2:
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
        std::memset(gKindHits, 0, sizeof(gKindHits));
        auto a = WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t { return orig(self + 4, handle); });
        int aHits[256];
        std::memcpy(aHits, gKindHits, sizeof(aHits));
        auto bb = WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t { return reinterpret_cast<SegmentView *>(self + 4)->DoCloseRecord(handle); });
        auto c = WorldRun(snap, routed, [&](uint8_t *self) -> uint64_t { return VirtualSlot<Fn>(self + 4, 39)(self + 4, handle); });
        WorldCompare(ctx, "DoCloseRecord", a, bb, c);
        bool ok = (a.result & 0xff) != 0;
        committed += ok && aHits[kCommit9b1fe0] != 0;
        plainDrop += ok && aHits[kCommit9b1fe0] == 0;
        foreignSameKey += handle == static_cast<uint8_t *>(sameKey) + 4;
        foreignOtherKey += handle == static_cast<uint8_t *>(otherKey) + 4;
        closedFile += (img[0x24] & 3) == 0;
    }
    if (committed < 200 || plainDrop < 200 || foreignSameKey < 100 || foreignOtherKey < 100 || closedFile < 100)
        ctx.Fail("DoCloseRecord: setup reaches too little (%d committed, %d dropped without the helper, %d same-key, %d other-key, %d closed)", committed, plainDrop,
                 foreignSameKey, foreignOtherKey, closedFile);
}

namespace {

// A small file in the sandbox with `size` bytes; returns its narrow path.
std::string MakeStampFile(const std::wstring &name, uint32_t size)
{
    std::wstring wide = difftest::SandboxPath(name);
    HANDLE h = CreateFileW(wide.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    std::vector<uint8_t> bytes(size, 0x5a);
    DWORD written = 0;
    if (size != 0) WriteFile(h, bytes.data(), size, &written, nullptr);
    CloseHandle(h);
    std::string narrow;
    for (wchar_t c : wide) narrow.push_back(static_cast<char>(c));
    return narrow;
}

} // namespace

namespace {

constexpr int64_t kStat1c2068 = 152;

// 0x1c2068 (the path's stat time) is original code that needs imports the harness does not pass; both sides get this
// stand-in, which answers a function of the path text and the time selector.
uint32_t __cdecl FakeStatTime(const void *path, int which)
{
    const char *text = reinterpret_cast<const cRZString *>(path)->Data();
    uint32_t hash = 17;
    for (const char *p = text; *p; ++p) hash = hash * 31 + static_cast<uint8_t>(*p);
    Push(kStat1c2068, which, hash);
    return hash ^ (static_cast<uint32_t>(which) << 24);
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_FileStamp, 0x9a99f0)
{
    InitHeaderFakes();
    Redirect statTime(ctx, 0x1c2068, reinterpret_cast<void *>(&FakeStatTime));
    Redirect statTimeOurs(reinterpret_cast<void *>(&PathFileTime), reinterpret_cast<void *>(&FakeStatTime));
    difftest::EmptySandbox();
    using Fn = uint32_t(__thiscall *)(void *);
    using Member = uint32_t (SegmentView::*)();
    auto orig = ctx.Original<Fn>(0x9a99f0);
    Tables plain = MakeTables(ctx);
    Tables routed = plain;
    routed.sub[46] = Addr(static_cast<Member>(&SegmentView::DoGetSegmentChangeID));
    // Files of several sizes (the length is shifted left by 16, so 65536 and more wrap) and a missing one.
    static const uint32_t sizes[] = {0, 1, 100, 65535, 65536, 70000, 131073};
    std::vector<std::string> paths;
    for (int k = 0; k < 7; ++k) paths.push_back(MakeStampFile(L"stamp" + std::to_wstring(k) + L".bin", sizes[k]));
    paths.push_back(difftest::SandboxDir().empty() ? std::string() : MakeStampFile(L"gone.bin", 1));
    DeleteFileW(difftest::SandboxPath(L"gone.bin").c_str());
    std::mt19937 rng(0x9a99f0);
    int dirty = 0, cached = 0, openLength = 0, pathLength = 0, missing = 0;
    for (int i = 0; i < 1500; ++i) {
        Image s = MakeImage(rng);
        WireClose(s);
        size_t which = rng() % paths.size();
        alignas(8) uint8_t stringBytes[0x20];
        reinterpret_cast<cRZString *>(stringBytes)->Construct(paths[which].c_str());
        std::memcpy(s.data() + 0x38, stringBytes, sizeof(stringBytes));
        s[0x34] = rng() % 4 == 0 ? 1 + rng() % 200 : 0;
        s[0x19d] = rng() % 8 == 0 ? 1 + rng() % 200 : 0;
        s[0x178] = rng() % 8 == 0 ? 1 + rng() % 200 : 0;
        SetWord(s, 0x100, rng() % 4 == 0 ? PickWord(rng) : 0);
        gFile4Answer = rng() % 2 ? 1 + rng() % 200 : 0;
        gLengths.assign(1, rng() % 5 == 0 ? PickWord(rng) : static_cast<uint32_t>(rng() % 100000));
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self + 4); });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { return reinterpret_cast<SegmentView *>(self + 4)->DoGetSegmentChangeID(); });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t { return VirtualSlot<Fn>(self + 4, 46)(self + 4); });
        Compare(ctx, "DoGetSegmentChangeID", a, b, c);
        bool anyDirty = s[0x34] || s[0x19d] || s[0x178];
        uint32_t cache;
        std::memcpy(&cache, s.data() + 0x100, sizeof(cache));
        dirty += anyDirty;
        cached += !anyDirty && cache != 0;
        bool computes = !anyDirty && cache == 0;
        openLength += computes && gFile4Answer != 0;
        pathLength += computes && gFile4Answer == 0 && which + 1 < paths.size();
        missing += computes && gFile4Answer == 0 && which + 1 == paths.size();
    }
    difftest::EmptySandbox();
    if (dirty < 150 || cached < 150 || openLength < 150 || pathLength < 200 || missing < 30)
        ctx.Fail("DoGetSegmentChangeID: setup reaches too little (%d dirty, %d cached, %d open files, %d paths, %d missing paths)", dirty, cached, openLength, pathLength, missing);
}
