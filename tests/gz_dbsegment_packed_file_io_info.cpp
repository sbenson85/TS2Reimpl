// Differential tests for cGZDBSegmentPackedFile: the info slot, reads, the scratch copy, close and compact.
// Fixtures (the arena, the fake store and file objects, the comparison helpers): gz_dbsegment_io_fixtures.h.
#include "gz_dbsegment_io_fixtures.h"

// ---- The info slot (vf51) ----

namespace {

constexpr int64_t kSubLock = 110, kSubUnlock = 111, kHas35 = 112, kSize36 = 113, kOff30 = 114, kStore4 = 115, kStore5 = 116;
bool gHasDir = false;
uint32_t gSize36 = 0, gOff30 = 0, gStore4 = 0, gStore5 = 0;

void __fastcall RecSubLock13(void *self, void *)
{
    Push(kSubLock, Off(self), 0);
}

void __fastcall RecSubUnlock14(void *self, void *)
{
    Push(kSubUnlock, Off(self), 0);
}

uint8_t __fastcall RecSub35Has(void *self, void *, const uint32_t *key)
{
    Push(kHas35, Off(self), 0);
    for (int i = 0; i < 4; ++i) gLog.push_back(key[i]);
    return gHasDir;
}

uint32_t __fastcall RecSub36Size(void *self, void *, const uint32_t *key)
{
    Push(kSize36, Off(self), 0);
    for (int i = 0; i < 4; ++i) gLog.push_back(key[i]);
    return gSize36;
}

uint32_t __fastcall RecPrim30Off(void *self, void *, const uint32_t *key)
{
    Push(kOff30, Off(self), 0);
    for (int i = 0; i < 4; ++i) gLog.push_back(key[i]);
    return gOff30;
}

uint32_t __fastcall RecStore4Fn(void *self, void *)
{
    Push(kStore4, Off(self), 0);
    return gStore4;
}

uint32_t __fastcall RecStore5Fn(void *self, void *)
{
    Push(kStore5, Off(self), 0);
    return gStore5;
}

void DescribeInfo(const PackedFileInfo &info, std::vector<int64_t> &out)
{
    const uint32_t scalars[] = {info.fileLength, info.storeBegin, info.storeEnd, info.zero0C, info.headerBytes, info.extentA, info.extentALength,
                                info.extentB, info.extentBLength, info.indexOffset, info.indexSize, info.totalA, info.totalB};
    for (uint32_t v : scalars) out.push_back(v);
    for (const auto *vec : {&info.freeA, &info.freeB}) {
        out.push_back(static_cast<int64_t>(vec->size()));
        out.push_back(static_cast<int64_t>(vec->capacity()));
        for (const auto &p : *vec) {
            out.push_back(p.first);
            out.push_back(p.second);
        }
    }
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_GetInfo, 0x9ab980)
{
    InitRecordFakes();
    ServiceScope service(ctx);
    gStoreTab[4] = reinterpret_cast<void *>(&RecStore4Fn);
    gStoreTab[5] = reinterpret_cast<void *>(&RecStore5Fn);
    using Fn = void(__thiscall *)(void *, PackedFileInfo *);
    using Member = void (cGZDBSegmentPackedFile::*)(PackedFileInfo *);
    auto orig = ctx.Original<Fn>(0x9ab980);
    Tables plain = MakeTables(ctx);
    plain.sub[13] = reinterpret_cast<void *>(&RecSubLock13);
    plain.sub[14] = reinterpret_cast<void *>(&RecSubUnlock14);
    plain.sub[35] = reinterpret_cast<void *>(&RecSub35Has);
    plain.sub[36] = reinterpret_cast<void *>(&RecSub36Size);
    plain.primary[30] = reinterpret_cast<void *>(&RecPrim30Off);
    Tables routed = plain;
    routed.primary[51] = Addr(static_cast<Member>(&cGZDBSegmentPackedFile::GetInfo));
    std::mt19937 rng(0x9ab980);
    int withDir = 0, withStore = 0, grew = 0, inPlace = 0;
    for (int i = 0; i < 1000; ++i) {
        Image s = MakeImage(rng);
        Wire(s);
        if (rng() % 4 == 0) SetWord(s, 0x30, 0); // no store
        std::map<uint32_t, uint32_t> treeA, treeB;
        for (unsigned n = rng() % 7; n > 0; --n) treeA[static_cast<uint32_t>(rng() % 500)] = static_cast<uint32_t>(rng() % 90);
        for (unsigned n = rng() % 7; n > 0; --n) treeB[static_cast<uint32_t>(rng() % 500)] = static_cast<uint32_t>(rng() % 90);
        gHasDir = rng() % 2;
        gSize36 = static_cast<uint32_t>(rng() % 5000);
        gOff30 = static_cast<uint32_t>(rng());
        gStore4 = static_cast<uint32_t>(rng());
        gStore5 = static_cast<uint32_t>(rng());
        gLengths = {static_cast<uint32_t>(rng())};
        uint32_t infoSeed = static_cast<uint32_t>(rng());
        auto run = [&](const Tables &t, auto call) {
            return Run(t, s, [&](uint8_t *self) -> uint64_t {
                std::memcpy(self + 0x184, &treeA, 8);
                std::memcpy(self + 0x18c, &treeB, 8);
                // The vectors may hold entries and spare room already.
                std::mt19937 build(infoSeed);
                auto *info = new PackedFileInfo();
                for (auto *vec : {&info->freeA, &info->freeB}) {
                    unsigned pre = build() % 3, spare = build() % 4;
                    vec->reserve(pre + spare);
                    for (unsigned k = 0; k < pre; ++k) vec->emplace_back(build(), build());
                }
                // Whatever the object held before: every scalar is rewritten by the call.
                for (uint32_t *field : {&info->fileLength, &info->storeBegin, &info->storeEnd, &info->zero0C, &info->headerBytes, &info->extentA,
                                        &info->extentALength, &info->extentB, &info->extentBLength, &info->indexOffset, &info->indexSize, &info->totalA,
                                        &info->totalB})
                    *field = build();
                call(self, info);
                DescribeInfo(*info, gLog);
                delete info;
                std::memset(self + 0x184, 0, 16);
                return 0;
            });
        };
        auto a = run(plain, [&](uint8_t *self, PackedFileInfo *info) { orig(self, info); });
        auto b = run(plain, [&](uint8_t *self, PackedFileInfo *info) { Obj(self)->GetInfo(info); });
        auto c = run(routed, [&](uint8_t *self, PackedFileInfo *info) { VirtualSlot<Fn>(self, 51)(self, info); });
        Compare(ctx, "GetInfo", a, b, c);
        withDir += gHasDir;
        withStore += s[0x30] + s[0x31] + s[0x32] + s[0x33] != 0;
        // Growth versus in-place appends, from the vector capacities before and after (rebuilt with the same seed).
        std::mt19937 probe(infoSeed);
        for (int v = 0; v < 2; ++v) {
            unsigned pre = probe() % 3, spare = probe() % 4;
            for (unsigned k = 0; k < pre * 2; ++k) (void)probe();
            size_t add = (v == 0 ? treeA : treeB).size();
            if (add == 0) continue;
            if (pre + add > pre + spare) ++grew; else ++inPlace;
        }
    }
    if (withDir < 300 || withStore < 500 || grew < 500 || inPlace < 200)
        ctx.Fail("GetInfo: setup reaches too little (%d with the DIR record, %d with a store, %d growing appends, %d in-place appends)", withDir, withStore, grew, inPlace);
}

// ---- Reading a record's bytes (vf28) ----

namespace {

constexpr int64_t kSeek12 = 80, kRead14 = 81, kSub5 = 82;
std::vector<uint8_t> gPendingFlags{0};
uint32_t gSeekDelta = 0, gSeekMut = 0;
uint8_t gIoRet = 1;

uint32_t __fastcall RecSeek12(void *self, void *, uint32_t position)
{
    Push(kSeek12, Off(self), position);
    if (gSeekMut != 0) *reinterpret_cast<uint32_t *>(gArena + 0xf4) += gSeekMut;
    return position + gSeekDelta;
}

uint8_t __fastcall RecRead14(void *self, void *, const void *destination, uint32_t count)
{
    Push(kRead14, Off(self), destination == nullptr ? -1 : Off(destination));
    gLog.push_back(count);
    return gIoRet;
}

uint8_t __fastcall RecSub5Flags(void *self, void *)
{
    Push(kSub5, Off(self), 0);
    return gPendingFlags[RecIndex(self) % gPendingFlags.size()];
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_ReadRecordData, 0x9af7d0)
{
    InitRecordFakes();
    gFileTab[12] = reinterpret_cast<void *>(&RecSeek12);
    gFileTab[14] = reinterpret_cast<void *>(&RecRead14);
    gRecSub[5] = reinterpret_cast<void *>(&RecSub5Flags);
    using Fn = uint32_t(__thiscall *)(void *, const uint32_t *, void *, uint32_t *);
    using Member = uint32_t (cGZDBSegmentPackedFile::*)(const uint32_t *, void *, uint32_t *);
    auto orig = ctx.Original<Fn>(0x9af7d0);
    Tables plain = MakeTables(ctx);
    plain.sub[5] = reinterpret_cast<void *>(&RecOpen5);
    Tables routed = plain;
    routed.primary[28] = Addr(static_cast<Member>(&cGZDBSegmentPackedFile::ReadRecordData));
    std::mt19937 rng(0x9af7d0);
    int pendingHits = 0, noRecord = 0, sizeQueries = 0, reads = 0, shortReads = 0, seekMissed = 0, ioFailed = 0, unreadable = 0;
    for (int i = 0; i < 2500; ++i) {
        Image s = MakeImage(rng);
        Wire(s);
        std::vector<void *> addresses;
        PendingBuild b = MakeRecords(s, rng, rng() % 7, addresses);
        LocalPending map;
        for (size_t k = 0; k < b.keys.size(); ++k) map[b.keys[k]] = addresses[k];
        s[0x24] = PickFlag(rng);
        bool tryOpen = rng() % 3 == 0;
        s[0x28] = tryOpen ? 1 + rng() % 3 : 0;
        s[0x29] = tryOpen ? static_cast<uint8_t>(rng() % 3) : 0;
        gOpensFile = rng() % 2;
        gPendingFlags.clear();
        for (int k = 0; k < 3; ++k) gPendingFlags.push_back(static_cast<uint8_t>(rng() % 4));
        gFindRet = (rng() % 5 == 0) ? nullptr : static_cast<void *>(gArena + kHit);
        SetWord(s, kHit + 0, rng() % 4 == 0 ? PickWord(rng) : static_cast<uint32_t>(rng() % 1000));
        SetWord(s, kHit + 4, rng() % 3 == 0 ? static_cast<uint32_t>(rng() % 20) : PickWord(rng));
        SetWord(s, kHit + 8, PickWord(rng));
        SetWord(s, 0xf4, static_cast<uint32_t>(rng() % 100));
        gSeekDelta = rng() % 4 == 0 ? 1 : 0;
        gSeekMut = rng() % 4 == 0 ? 7 : 0;
        if (gSeekMut != 0 && rng() % 2) gSeekDelta = gSeekMut; // lands only when +0xf4 is read again
        gIoRet = rng() % 4 == 0 ? 0 : static_cast<uint8_t>(1 + rng() % 3);
        std::array<uint32_t, 4> key = (!b.keys.empty() && rng() % 2) ? b.keys[rng() % b.keys.size()] : SmallKey(rng);
        std::memcpy(s.data() + kRecIn, key.data(), 16);
        bool nullDestination = rng() % 4 == 0;
        SetWord(s, kDst + 8, rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : static_cast<uint32_t>(rng() % 30));
        auto call = [&](auto fn) {
            return [&, fn](uint8_t *self) -> uint64_t {
                std::memcpy(self + 0x110, &map, 8);
                gFindIdx = 0;
                uint64_t r = fn(self, reinterpret_cast<const uint32_t *>(self + kRecIn), nullDestination ? nullptr : static_cast<void *>(self + 0x390),
                                reinterpret_cast<uint32_t *>(self + kDst + 8));
                std::memset(self + 0x110, 0, 8);
                return r;
            };
        };
        auto a = Run(plain, s, call([&](uint8_t *self, const uint32_t *k, void *d, uint32_t *n) { return orig(self, k, d, n); }));
        auto bb = Run(plain, s, call([&](uint8_t *self, const uint32_t *k, void *d, uint32_t *n) { return Obj(self)->ReadRecordData(k, d, n); }));
        auto c = Run(routed, s, call([&](uint8_t *self, const uint32_t *k, void *d, uint32_t *n) { return VirtualSlot<Fn>(self, 28)(self, k, d, n); }));
        Compare(ctx, "ReadRecordData", a, bb, c);
        bool sawSub5 = false, sawRead = false, sawSeek = false, sawFind = false;
        for (int64_t v : a.log) {
            sawSub5 |= v == kSub5;
            sawRead |= v == kRead14;
            sawSeek |= v == kSeek12;
            sawFind |= v == kFind11;
        }
        pendingHits += sawSub5 && !sawFind;
        noRecord += sawFind && !sawSeek && !sawRead && a.result == 0xffffffffull;
        sizeQueries += sawFind && nullDestination && a.result != 0xffffffffull;
        reads += sawRead;
        shortReads += sawRead && static_cast<uint32_t>(a.log[a.log.size() - 1]) < 20;
        seekMissed += sawSeek && !sawRead;
        ioFailed += sawRead && a.result == 0;
        unreadable += (s[0x24] & 1) == 0;
    }
    if (pendingHits < 100 || noRecord < 100 || sizeQueries < 100 || reads < 300 || seekMissed < 100 || ioFailed < 50 || unreadable < 300)
        ctx.Fail("ReadRecordData: setup reaches too little (%d pending, %d no record, %d size queries, %d reads, %d missed seeks, %d failed reads, %d unreadable)",
                 pendingHits, noRecord, sizeQueries, reads, seekMissed, ioFailed, unreadable);
}

// ---- The scratch copy (vf34): the scratch file is a real constructed object whose game vtable slots are patched ----

namespace {

constexpr int64_t kScrAddRef = 70, kScrRelease = 71, kScrInit = 72, kScrShutdown = 73, kScrSetPath = 74, kScrOpen = 75, kScrClose = 76, kCopy35 = 77;
std::vector<const void *> gSeen;
bool gInitOk = true, gSetPathOk = true, gOpenOk = true;
uint32_t gCopyResult = 0;

// A small id per object, in order of first appearance in the run (the scratch file lives on the heap, so its
// address differs between runs).
int64_t IdOf(const void *p)
{
    for (size_t i = 0; i < gSeen.size(); ++i) if (gSeen[i] == p) return static_cast<int64_t>(i);
    gSeen.push_back(p);
    return static_cast<int64_t>(gSeen.size() - 1);
}

uint32_t __fastcall ScrAddRef(void *self, void *)
{
    Push(kScrAddRef, IdOf(self), 0);
    return 1;
}

uint32_t __fastcall ScrRelease(void *self, void *)
{
    Push(kScrRelease, IdOf(self), 0);
    return 1;
}

uint8_t __fastcall ScrInit(void *self, void *)
{
    Push(kScrInit, IdOf(self), 0);
    return gInitOk;
}

uint8_t __fastcall ScrShutdown(void *self, void *)
{
    Push(kScrShutdown, IdOf(self), 0);
    return 1;
}

uint8_t __fastcall ScrSetPath(void *self, void *, void *path)
{
    Push(kScrSetPath, IdOf(self), Off(path));
    return gSetPathOk;
}

uint8_t __fastcall ScrOpen(void *self, void *, uint32_t a, uint32_t b)
{
    Push(kScrOpen, IdOf(static_cast<uint8_t *>(self) - 4), a & 0xff);
    gLog.push_back(b & 0xff);
    return gOpenOk;
}

void __fastcall ScrClose(void *self, void *)
{
    Push(kScrClose, IdOf(static_cast<uint8_t *>(self) - 4), 0);
}

uint32_t __fastcall RecCopy35(void *self, void *, void *sub, uint32_t a2, uint32_t a3, uint32_t a4)
{
    Push(kCopy35, Off(self), IdOf(static_cast<uint8_t *>(sub) - 4));
    gLog.push_back(a2);
    gLog.push_back(a3);
    gLog.push_back(a4);
    return gCopyResult;
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_CopyToFile, 0x9a8f00)
{
    WorldScope world(ctx);
    InitFakes();
    // The scratch file's vtables are the game's: patch the slots the function calls.
    SlotPatch p1(ctx, cGZDBSegmentPackedFile::kVTable, 1, reinterpret_cast<void *>(&ScrAddRef));
    SlotPatch p2(ctx, cGZDBSegmentPackedFile::kVTable, 2, reinterpret_cast<void *>(&ScrRelease));
    SlotPatch p4(ctx, cGZDBSegmentPackedFile::kVTable, 4, reinterpret_cast<void *>(&ScrInit));
    SlotPatch p5(ctx, cGZDBSegmentPackedFile::kVTable, 5, reinterpret_cast<void *>(&ScrShutdown));
    SlotPatch p9(ctx, cGZDBSegmentPackedFile::kVTable, 9, reinterpret_cast<void *>(&ScrSetPath));
    SlotPatch s5(ctx, cGZDBSegmentPackedFile::kSubVTable, 5, reinterpret_cast<void *>(&ScrOpen));
    SlotPatch s9(ctx, cGZDBSegmentPackedFile::kSubVTable, 9, reinterpret_cast<void *>(&ScrClose));
    using Fn = uint32_t(__thiscall *)(void *, void *, uint32_t, uint32_t, uint32_t);
    using Member = uint32_t (cGZDBSegmentPackedFile::*)(void *, uint32_t, uint32_t, uint32_t);
    auto orig = ctx.Original<Fn>(0x9a8f00);
    Tables plain = MakeTables(ctx);
    plain.primary[35] = reinterpret_cast<void *>(&RecCopy35);
    Tables routed = plain;
    routed.primary[34] = Addr(static_cast<Member>(&cGZDBSegmentPackedFile::CopyToFile));
    std::mt19937 rng(0x9a8f00);
    int full = 0, initFailed = 0, pathFailed = 0, openFailed = 0, flagZero = 0, flagOne = 0;
    for (int i = 0; i < 1200; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        Wire(img);
        std::memcpy(gArena, img.data(), kBytes);
        gInitOk = rng() % 5 != 0;
        gSetPathOk = rng() % 5 != 0;
        gOpenOk = rng() % 5 != 0;
        gCopyResult = static_cast<uint32_t>(rng());
        uint32_t a2 = static_cast<uint32_t>(rng());
        // The bytes decide the Open flag; the rest of the dwords is arbitrary.
        uint32_t a3 = (static_cast<uint32_t>(rng()) & ~0xffu) | (rng() % 3 == 0 ? 0 : 1 + rng() % 255);
        uint32_t a4 = (static_cast<uint32_t>(rng()) & ~0xffu) | (rng() % 3 == 0 ? 0 : 1 + rng() % 255);
        void *path = gArena + kRecIn;
        Snapshot snap;
        snap.Take();
        auto run = [&](const Tables &t, auto call) {
            return WorldRun(snap, t, [&](uint8_t *self) -> uint64_t {
                gSeen.clear();
                return call(self);
            });
        };
        auto a = run(plain, [&](uint8_t *self) -> uint64_t { return orig(self, path, a2, a3, a4); });
        auto b = run(plain, [&](uint8_t *self) -> uint64_t { return Obj(self)->CopyToFile(path, a2, a3, a4); });
        auto c = run(routed, [&](uint8_t *self) -> uint64_t { return VirtualSlot<Fn>(self, 34)(self, path, a2, a3, a4); });
        WorldCompare(ctx, "CopyToFile", a, b, c);
        bool called35 = false;
        for (int64_t v : a.log) if (v == kCopy35) called35 = true;
        full += called35;
        initFailed += !gInitOk;
        pathFailed += gInitOk && !gSetPathOk;
        openFailed += gInitOk && gSetPathOk && !gOpenOk;
        bool flag = ((a3 & 0xff) == 0 || (a4 & 0xff) == 0);
        if (gInitOk && gSetPathOk) { flagOne += flag; flagZero += !flag; }
    }
    if (full < 300 || initFailed < 100 || pathFailed < 100 || openFailed < 100 || flagOne < 200 || flagZero < 100)
        ctx.Fail("CopyToFile: setup reaches too little (%d complete, %d init failures, %d path failures, %d open failures, %d flag 1, %d flag 0)", full, initFailed,
                 pathFailed, openFailed, flagOne, flagZero);
}

namespace {

// Close (0x9a80c0) and its subobject slot (0x9a8080): the same trials, three ways of making the call.
// `routedSub9` is ours for the slot-9 trial (the third side goes through the vtable), null otherwise.
template <typename RunA, typename RunB, typename RunC>
void CloseTrials(difftest::Context &ctx, const char *name, uint32_t seed, void *routedSub9, RunA runA, RunB runB, RunC runC)
{
    InitCloseFakes();
    HelperFakes helpers(ctx);
    Tables plain = MakeTables(ctx);
    plain.sub[10] = reinterpret_cast<void *>(&Flush10);
    Tables routed = plain;
    if (routedSub9 != nullptr) routed.sub[9] = routedSub9;
    std::mt19937 rng(seed);
    int closedFile = 0, readOnly = 0, flushed = 0, committed = 0, noFlush = 0, withStore = 0, withMaps = 0, treeNodes = 0;
    for (int i = 0; i < 1500; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        WireClose(img);
        static const uint8_t states[] = {0, 1, 2, 3, 3, 3, 0x83, 0xff, 0xfc};
        img[0x24] = states[rng() % 9];
        if (rng() % 4 == 0) SetWord(img, 0x30, 0);
        img[0x34] = rng() % 3 ? 0 : 1 + rng() % 200;
        img[0x19d] = rng() % 3 ? 0 : 1 + rng() % 200;
        img[0x178] = rng() % 3 ? 0 : 1 + rng() % 200;
        std::memcpy(gArena, img.data(), kBytes);
        auto r1 = BuildRanges(rng, 0x184, 8);
        auto r2 = BuildRanges(rng, 0x18c, 8);
        gFlushAnswer = rng() % 4 == 0 ? static_cast<uint8_t>(rng()) : 1;
        uint32_t flush = (static_cast<uint32_t>(rng()) & ~0xffu) | (rng() % 3 == 0 ? 0 : 1 + rng() % 255);
        bool hasMaps = rng() % 3 != 0;
        uint32_t mapSeed = static_cast<uint32_t>(rng());
        Snapshot snap;
        snap.Take();
        auto run = [&](const Tables &t, auto fn) {
            return WorldRun(snap, t, [&](uint8_t *self) -> uint64_t { return WithMaps(self, hasMaps, mapSeed, [&](uint8_t *p) -> uint64_t { return fn(p, flush) & 0xff; }); });
        };
        auto a = run(plain, runA);
        auto b = run(plain, runB);
        auto c = run(routed, runC);
        WorldCompare(ctx, name, a, b, c);
        bool open = (img[0x24] & 3) != 0, writable = (img[0x24] & 2) != 0;
        bool doFlush = open && writable && (flush & 0xff) != 0;
        closedFile += !open;
        readOnly += open && !writable;
        flushed += doFlush;
        noFlush += writable && open && !doFlush;
        bool pending = img[0x34] || img[0x19d] || img[0x178];
        committed += doFlush && pending;
        withStore += (img[0x30] | img[0x31] | img[0x32] | img[0x33]) != 0;
        withMaps += hasMaps;
        treeNodes += !r1.empty() && !r2.empty();
    }
    if (closedFile < 100 || readOnly < 100 || flushed < 200 || committed < 100 || noFlush < 50 || withStore < 800 || withMaps < 800 || treeNodes < 400)
        ctx.Fail("%s: setup reaches too little (%d closed, %d read-only, %d flushed, %d committed, %d no-flush, %d stores, %d maps, %d trees)", name, closedFile,
                 readOnly, flushed, committed, noFlush, withStore, withMaps, treeNodes);
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_Close, 0x9a80c0)
{
    WorldScope world(ctx);
    using Fn = uint8_t(__thiscall *)(void *, uint32_t);
    auto orig = ctx.Original<Fn>(0x9a80c0);
    CloseTrials(
        ctx, "Close", 0x9a80c0, nullptr, [&](uint8_t *p, uint32_t flush) -> uint64_t { return orig(p, flush); },
        [&](uint8_t *p, uint32_t flush) -> uint64_t { return Obj(p)->Close(flush); },
        [&](uint8_t *p, uint32_t flush) -> uint64_t { return Obj(p)->Close(flush); });
}

DIFF_TEST(cGZDBSegmentPackedFile_CloseSlot, 0x9a8080)
{
    WorldScope world(ctx);
    using Fn = uint8_t(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9a8080);
    CloseTrials(
        ctx, "CloseSlot", 0x9a8080, Addr(static_cast<uint8_t (SegmentView::*)()>(&SegmentView::Close)),
        [&](uint8_t *p, uint32_t) -> uint64_t { return orig(p + 4); },
        [&](uint8_t *p, uint32_t) -> uint64_t { return reinterpret_cast<SegmentView *>(p + 4)->Close(); },
        [&](uint8_t *p, uint32_t) -> uint64_t { return VirtualSlot<Fn>(p + 4, 9)(p + 4); });
}

DIFF_TEST(cGZDBSegmentPackedFile_Reopen, 0x9b0620)
{
    WorldScope world(ctx);
    InitCloseFakes();
    HelperFakes helpers(ctx);
    using Fn = uint8_t(__thiscall *)(void *);
    using Member = uint8_t (cGZDBSegmentPackedFile::*)();
    auto orig = ctx.Original<Fn>(0x9b0620);
    Tables plain = MakeTables(ctx);
    plain.sub[10] = reinterpret_cast<void *>(&Flush10);
    plain.sub[5] = reinterpret_cast<void *>(&ReopenOpen);
    Tables routed = plain;
    routed.primary[23] = Addr(static_cast<Member>(&cGZDBSegmentPackedFile::ReopenIfDirty));
    std::mt19937 rng(0x9b0620);
    int nothingPending = 0, notAllowed = 0, reopened = 0;
    for (int i = 0; i < 1500; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        WireClose(img);
        static const uint8_t states[] = {0, 1, 2, 3, 3, 3, 0x83, 0xff, 0xfc, 0x02};
        img[0x24] = states[rng() % 10];
        img[0x34] = rng() % 3 ? 0 : 1 + rng() % 200;
        img[0x19d] = rng() % 3 ? 0 : 1 + rng() % 200;
        img[0x178] = rng() % 3 ? 0 : 1 + rng() % 200;
        SetWord(img, 0x17c, (rng() % 2 ? 0x10u : 0u) | (static_cast<uint32_t>(rng()) & ~0x10u));
        SetPointer(img, 0x30, nullptr);
        SetWord(img, 0x1a0, 0);
        SetWord(img, 0x1a4, 0);
        std::memcpy(gArena, img.data(), kBytes);
        auto r1 = BuildRanges(rng, 0x184, 5);
        auto r2 = BuildRanges(rng, 0x18c, 5);
        gFlushAnswer = rng() % 4 == 0 ? static_cast<uint8_t>(rng()) : 1;
        gReopenOpenAnswer = rng() % 3 == 0 ? static_cast<uint8_t>(rng()) : 1;
        Snapshot snap;
        snap.Take();
        auto a = WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t { return orig(self) & 0xff; });
        auto b = WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t { return Obj(self)->ReopenIfDirty() & 0xff; });
        auto c = WorldRun(snap, routed, [&](uint8_t *self) -> uint64_t { return VirtualSlot<Fn>(self, 23)(self) & 0xff; });
        WorldCompare(ctx, "ReopenIfDirty", a, b, c);
        bool anyFlag = img[0x34] || img[0x19d] || img[0x178];
        nothingPending += !anyFlag;
        notAllowed += anyFlag && !(img[0x17c] & 0x10);
        for (size_t k = 0; k + 2 < a.log.size(); k += 3) {
            if (a.log[k] == kReopenOpen) { ++reopened; break; }
        }
        (void)r1;
        (void)r2;
    }
    if (nothingPending < 300 || notAllowed < 150 || reopened < 200) ctx.Fail("ReopenIfDirty: setup reaches too little (%d nothing pending, %d not allowed, %d reopened)", nothingPending, notAllowed, reopened);
}

DIFF_TEST(cGZDBSegmentPackedFile_Compact, 0x9a8370)
{
    InitCloseFakes();
    HelperFakes helpers(ctx);
    using Fn = uint8_t(__thiscall *)(void *);
    using Member = uint8_t (cGZDBSegmentPackedFile::*)();
    auto orig = ctx.Original<Fn>(0x9a8370);
    Tables plain = MakeTables(ctx);
    plain.sub[5] = reinterpret_cast<void *>(&RecOpen5);
    Tables routed = plain;
    routed.primary[36] = Addr(static_cast<Member>(&cGZDBSegmentPackedFile::Compact));
    std::mt19937 rng(0x9a8370);
    int ran = 0, busy = 0, closedFile = 0, opened = 0;
    for (int i = 0; i < 1500; ++i) {
        Image s = MakeImage(rng);
        WireClose(s);
        static const uint8_t states[] = {0, 1, 2, 3, 3, 0x83, 0xff, 0x80};
        s[0x24] = states[rng() % 8];
        s[0x19e] = rng() % 3 ? 0 : 1 + rng() % 200;
        bool tryOpen = rng() % 2 == 0;
        s[0x28] = tryOpen ? 1 + rng() % 3 : 0;
        s[0x29] = tryOpen ? static_cast<uint8_t>(rng() % 3) : 0;
        gOpensFile = rng() % 2;
        gCompactAnswer = rng() % 4 == 0 ? static_cast<uint8_t>(rng()) : 1;
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self) & 0xff; });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { return Obj(self)->Compact() & 0xff; });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t { return VirtualSlot<Fn>(self, 36)(self) & 0xff; });
        Compare(ctx, "Compact", a, b, c);
        for (size_t k = 0; k + 2 < a.log.size(); k += 3) {
            if (a.log[k] == kCompactCall) { ++ran; break; }
        }
        busy += s[0x19e] != 0;
        closedFile += (s[0x24] & 3) == 0;
        for (size_t k = 0; k + 2 < a.log.size(); k += 3) {
            if (a.log[k] == kOpen5) { ++opened; break; }
        }
    }
    if (ran < 300 || busy < 300 || closedFile < 100 || opened < 100) ctx.Fail("Compact: setup reaches too little (%d ran, %d busy, %d closed, %d auto-opened)", ran, busy, closedFile, opened);
}

namespace {

// The record store at +0x30 may be absent (or created by the fake index load); the first load decides.
void PrepareStoreTrial(Image &s, std::mt19937 &rng)
{
    WireClose(s);
    static const uint8_t states[] = {0, 1, 2, 3, 3, 0x83};
    s[0x24] = states[rng() % 6];
    if (rng() % 2) SetWord(s, 0x30, 0);
    bool tryOpen = rng() % 2 == 0;
    s[0x28] = tryOpen ? 1 + rng() % 3 : 0;
    s[0x29] = tryOpen ? static_cast<uint8_t>(rng() % 3) : 0;
    gOpensFile = rng() % 2;
    gLoadIndexAnswer = rng() % 2;
    gLoadCreatesStore = rng() % 2;
    gCountAnswer = static_cast<uint32_t>(rng());
    gStore10Answer = static_cast<uint32_t>(rng());
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_GetRecordCount, 0x9a9880)
{
    InitCloseFakes();
    HelperFakes helpers(ctx);
    using Fn = uint32_t(__thiscall *)(void *, uint32_t);
    using Member = uint32_t (SegmentView::*)(uint32_t);
    auto orig = ctx.Original<Fn>(0x9a9880);
    Tables plain = MakeTables(ctx);
    plain.sub[5] = reinterpret_cast<void *>(&RecOpen5);
    Tables routed = plain;
    routed.sub[44] = Addr(static_cast<Member>(&SegmentView::DoGetRecordCount));
    std::mt19937 rng(0x9a9880);
    int filtered = 0, loaded = 0, counted = 0, noStore = 0;
    for (int i = 0; i < 1500; ++i) {
        Image s = MakeImage(rng);
        PrepareStoreTrial(s, rng);
        uint32_t filter = rng() % 2 ? 0 : static_cast<uint32_t>(rng());
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self + 4, filter); });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { return reinterpret_cast<SegmentView *>(self + 4)->DoGetRecordCount(filter); });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t { return VirtualSlot<Fn>(self + 4, 44)(self + 4, filter); });
        Compare(ctx, "DoGetRecordCount", a, b, c);
        filtered += filter != 0;
        for (size_t k = 0; k + 2 < a.log.size(); k += 3) {
            if (a.log[k] == kLoadIndexCall) { ++loaded; break; }
        }
        for (size_t k = 0; k + 2 < a.log.size(); k += 3) {
            if (a.log[k] == kStore3) { ++counted; break; }
        }
        noStore += filter == 0 && a.result == 0;
    }
    if (filtered < 500 || loaded < 200 || counted < 300 || noStore < 50) ctx.Fail("DoGetRecordCount: setup reaches too little (%d filtered, %d loads, %d counted, %d no store)", filtered, loaded, counted, noStore);
}

DIFF_TEST(cGZDBSegmentPackedFile_GetResourceKeyList, 0x9a99b0)
{
    InitCloseFakes();
    HelperFakes helpers(ctx);
    using Fn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
    using Member = uint32_t (SegmentView::*)(uint32_t, uint32_t);
    auto orig = ctx.Original<Fn>(0x9a99b0);
    Tables plain = MakeTables(ctx);
    plain.sub[5] = reinterpret_cast<void *>(&RecOpen5);
    Tables routed = plain;
    routed.sub[45] = Addr(static_cast<Member>(&SegmentView::DoGetResourceKeyList));
    std::mt19937 rng(0x9a99b0);
    int loaded = 0, listed = 0, noStore = 0;
    for (int i = 0; i < 1500; ++i) {
        Image s = MakeImage(rng);
        PrepareStoreTrial(s, rng);
        uint32_t x = static_cast<uint32_t>(rng()), y = static_cast<uint32_t>(rng());
        auto a = Run(plain, s, [&](uint8_t *self) -> uint64_t { return orig(self + 4, x, y); });
        auto b = Run(plain, s, [&](uint8_t *self) -> uint64_t { return reinterpret_cast<SegmentView *>(self + 4)->DoGetResourceKeyList(x, y); });
        auto c = Run(routed, s, [&](uint8_t *self) -> uint64_t { return VirtualSlot<Fn>(self + 4, 45)(self + 4, x, y); });
        Compare(ctx, "DoGetResourceKeyList", a, b, c);
        for (size_t k = 0; k + 2 < a.log.size(); k += 3) {
            if (a.log[k] == kLoadIndexCall) { ++loaded; break; }
        }
        for (size_t k = 0; k + 2 < a.log.size(); k += 3) {
            if (a.log[k] == kStore10) { ++listed; break; }
        }
        noStore += a.result == 0 && a.log.size() < 12;
    }
    if (loaded < 200 || listed < 300 || noStore < 50) ctx.Fail("DoGetResourceKeyList: setup reaches too little (%d loads, %d listed, %d no store)", loaded, listed, noStore);
}

DIFF_TEST(cGZDBSegmentPackedFile_TestForRecord, 0x9aa4d0)
{
    InitCloseFakes();
    HelperFakes helpers(ctx);
    using Fn = bool(__thiscall *)(void *, const uint32_t *);
    using Member = bool (SegmentView::*)(const uint32_t *);
    auto orig = ctx.Original<Fn>(0x9aa4d0);
    Tables plain = MakeTables(ctx);
    plain.sub[5] = reinterpret_cast<void *>(&RecOpen5);
    Tables routed = plain;
    routed.sub[35] = Addr(static_cast<Member>(&SegmentView::DoTestForRecord));
    std::mt19937 rng(0x9aa4d0);
    int stored = 0, pendingYes = 0, pendingNo = 0, noStore = 0;
    for (int i = 0; i < 1500; ++i) {
        Image s = MakeImage(rng);
        PrepareStoreTrial(s, rng);
        std::vector<void *> addresses;
        PendingBuild pb = MakeRecords(s, rng, rng() % 9, addresses);
        LocalPending map;
        for (size_t k = 0; k < pb.keys.size(); ++k) map[pb.keys[k]] = addresses[k];
        gFindRet = (rng() % 3 == 0) ? static_cast<void *>(gArena + kHit) : nullptr;
        std::array<uint32_t, 4> key = (!pb.keys.empty() && rng() % 2) ? pb.keys[rng() % pb.keys.size()] : SmallKey(rng);
        std::memcpy(s.data() + kRecIn, key.data(), 16);
        auto call = [&](auto fn) {
            return [&, fn](uint8_t *self) -> uint64_t {
                std::memcpy(self + 0x110, &map, 8);
                uint64_t r = fn(self);
                std::memset(self + 0x110, 0, 8);
                return r;
            };
        };
        auto a = Run(plain, s, call([&](uint8_t *self) { return static_cast<uint64_t>(orig(self + 4, reinterpret_cast<const uint32_t *>(self + kRecIn))) & 0xff; }));
        auto b = Run(plain, s, call([&](uint8_t *self) {
            return static_cast<uint64_t>(reinterpret_cast<SegmentView *>(self + 4)->DoTestForRecord(reinterpret_cast<const uint32_t *>(self + kRecIn))) & 0xff;
        }));
        auto c = Run(routed, s, call([&](uint8_t *self) {
            return static_cast<uint64_t>(VirtualSlot<Fn>(self + 4, 35)(self + 4, reinterpret_cast<const uint32_t *>(self + kRecIn))) & 0xff;
        }));
        Compare(ctx, "DoTestForRecord", a, b, c);
        bool inTree = map.count(key) != 0;
        bool anyFind = false;
        for (size_t k = 0; k + 2 < a.log.size(); k += 3) anyFind |= a.log[k] == kFind11;
        stored += anyFind && gFindRet != nullptr && a.result == 1;
        pendingYes += anyFind && gFindRet == nullptr && inTree && a.result == 1;
        pendingNo += anyFind && gFindRet == nullptr && !inTree && a.result == 0;
        noStore += !anyFind;
    }
    if (stored < 100 || pendingYes < 100 || pendingNo < 100 || noStore < 100) ctx.Fail("DoTestForRecord: setup reaches too little (%d stored, %d pending, %d absent, %d no store)", stored, pendingYes, pendingNo, noStore);
}

DIFF_TEST(cGZDBSegmentPackedFile_CopyRecordsTo, 0x9a8fe0)
{
    WorldScope world(ctx);
    ServiceScope service(ctx);
    InitCloseFakes();
    HelperFakes helpers(ctx);
    using Fn = uint32_t(__thiscall *)(void *, void *, uint32_t, uint32_t, uint32_t);
    using Member = uint32_t (cGZDBSegmentPackedFile::*)(void *, uint32_t, uint32_t, uint32_t);
    auto orig = ctx.Original<Fn>(0x9a8fe0);
    Tables plain = MakeTables(ctx);
    plain.sub[13] = reinterpret_cast<void *>(&SubLock13);
    plain.sub[14] = reinterpret_cast<void *>(&SubUnlock14);
    plain.sub[45] = reinterpret_cast<void *>(&SubList45);
    Tables routed = plain;
    routed.primary[35] = Addr(static_cast<Member>(&cGZDBSegmentPackedFile::CopyRecordsTo));
    std::mt19937 rng(0x9a8fe0);
    int fromOther = 0, fromSelf = 0, copied = 0, empty = 0, freedList = 0;
    for (int i = 0; i < 1200; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        WireClose(img);
        std::memcpy(gArena, img.data(), kBytes);
        gEntries.clear();
        for (unsigned n = rng() % 7; n > 0; --n) gEntries.push_back({static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng())});
        gExtraCapacity = rng() % 3;
        gCopyAnswers.clear();
        for (int k = 0; k < 5; ++k) gCopyAnswers.push_back(rng() % 3 == 0 ? static_cast<uint8_t>(rng()) : static_cast<uint8_t>(rng() % 2));
        uint32_t filter = static_cast<uint32_t>(rng());
        uint32_t a3 = (static_cast<uint32_t>(rng()) & ~0xffu) | (rng() % 3 == 0 ? 0 : 1 + rng() % 255);
        uint32_t a4 = static_cast<uint32_t>(rng());
        void *other = gArena + kOther;
        Snapshot snap;
        snap.Take();
        auto run = [&](const Tables &t, auto fn) {
            return WorldRun(snap, t, [&](uint8_t *self) -> uint64_t {
                gCopyIdx = 0;
                gFilledBlock = nullptr;
                return fn(self);
            });
        };
        auto a = run(plain, [&](uint8_t *self) -> uint64_t { return orig(self, other, filter, a3, a4); });
        auto b = run(plain, [&](uint8_t *self) -> uint64_t { return Obj(self)->CopyRecordsTo(other, filter, a3, a4); });
        auto c = run(routed, [&](uint8_t *self) -> uint64_t { return VirtualSlot<Fn>(self, 35)(self, other, filter, a3, a4); });
        WorldCompare(ctx, "CopyRecordsTo", a, b, c);
        bool useOther = (a3 & 0xff) != 0;
        fromOther += useOther;
        fromSelf += !useOther;
        copied += a.result != 0;
        empty += gEntries.empty();
        for (size_t k = 0; k + 2 < a.log.size(); k += 3) {
            if (a.log[k] == kSvcFree && a.log[k + 1] == 1) { ++freedList; break; }
        }
    }
    if (fromOther < 300 || fromSelf < 300 || copied < 400 || empty < 100 || freedList < 300)
        ctx.Fail("CopyRecordsTo: setup reaches too little (%d from the other file, %d from this one, %d with copies, %d empty, %d list blocks freed)", fromOther, fromSelf, copied, empty,
                 freedList);
}
