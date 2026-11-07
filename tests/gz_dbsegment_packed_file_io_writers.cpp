// Differential tests for cGZDBSegmentPackedFile: FindHeader, the index and hole writers, StoreWrittenRecord, CopyOneRecord.
// Fixtures (the arena, the fake store and file objects, the comparison helpers): gz_dbsegment_io_fixtures.h.
#include "gz_dbsegment_io_fixtures.h"

// ---- FindHeader (0x9aadf0) ----
//
// The file object (+0x58) is a small file over gFindFile: open or not (Open succeeding or not), a position moved by
// SeekToBegin, SeekToRelativePosition, SeekToPosition, ReadWithCount and Read, every call logged with its arguments.
// The file holds 0..4 markers (whole, cut by a chunk edge, or in the 16 bytes chunks share), each followed by "DBPF" or
// something else; ReadWithCount can fail at a chosen call. Compared: the result, the calls and the arena.
namespace {

constexpr int64_t kFfIsOpen = 190, kFfSetPath = 191, kFfOpen = 192, kFfClose = 193, kFfPosition = 194, kFfLength = 195,
                  kFfBegin = 196, kFfRelative = 197, kFfSeek = 198, kFfRead = 199, kFfReadCount = 200;
struct FakeFile {
    std::vector<uint8_t> bytes;
    int32_t pos = 0;
    bool open = false, opens = true;
    int countCalls = 0, failCountAt = -1;
} gFf;

uint8_t __fastcall FfIsOpen(void *self, void *)
{
    Push(kFfIsOpen, Off(self), gFf.open);
    return gFf.open ? 1 : 0;
}

void __fastcall FfSetPath(void *self, void *, void *path)
{
    Push(kFfSetPath, Off(self), Off(path));
}

uint8_t __fastcall FfOpen(void *self, void *, uint32_t access, uint32_t creation, uint32_t sharing)
{
    Push(kFfOpen, Off(self), access);
    gLog.push_back(creation);
    gLog.push_back(sharing);
    gFf.open = gFf.opens;
    gFf.pos = 0;
    return gFf.opens ? 1 : 0;
}

uint8_t __fastcall FfClose(void *self, void *)
{
    Push(kFfClose, Off(self), 0);
    gFf.open = false;
    return 1;
}

int32_t __fastcall FfPosition(void *self, void *)
{
    Push(kFfPosition, Off(self), gFf.pos);
    return gFf.pos;
}

uint32_t __fastcall FfLength(void *self, void *)
{
    Push(kFfLength, Off(self), 0);
    return static_cast<uint32_t>(gFf.bytes.size());
}

int32_t __fastcall FfBegin(void *self, void *)
{
    Push(kFfBegin, Off(self), 0);
    gFf.pos = 0;
    return 0;
}

int32_t __fastcall FfRelative(void *self, void *, int32_t offset)
{
    Push(kFfRelative, Off(self), offset);
    gFf.pos = (std::max)(0, gFf.pos + offset);
    return gFf.pos;
}

int32_t __fastcall FfSeek(void *self, void *, int32_t position)
{
    Push(kFfSeek, Off(self), position);
    gFf.pos = position;
    return position;
}

uint32_t FfTake(void *to, uint32_t want)
{
    const uint32_t size = static_cast<uint32_t>(gFf.bytes.size());
    const uint32_t from = static_cast<uint32_t>(gFf.pos);
    const uint32_t got = from >= size ? 0 : (std::min)(want, size - from);
    if (got != 0) std::memcpy(to, gFf.bytes.data() + from, got);
    gFf.pos += static_cast<int32_t>(got);
    return got;
}

uint8_t __fastcall FfRead(void *self, void *, void *to, uint32_t length)
{
    Push(kFfRead, Off(self), length);
    gLog.push_back(gFf.pos);
    uint8_t buffer[64] = {};
    const uint32_t got = FfTake(buffer, (std::min)(length, 64u));
    if (got != length) return 0;
    std::memcpy(to, buffer, length);
    return 1;
}

uint8_t __fastcall FfReadCount(void *self, void *, void *to, uint32_t *length)
{
    Push(kFfReadCount, Off(self), *length);
    gLog.push_back(gFf.pos);
    if (gFf.countCalls++ == gFf.failCountAt) return 0;
    *length = FfTake(to, *length);
    return 1;
}

struct SavedFileTable {
    void *saved[64];
    SavedFileTable() { std::memcpy(saved, gFileTab, sizeof(saved)); }
    ~SavedFileTable() { std::memcpy(gFileTab, saved, sizeof(saved)); }
};

const uint8_t kMarkerBytes[16] = {0x80, 0x9d, 0x88, 0xec, 0x8f, 0x24, 0x03, 0x6c, 0xc9, 0xa6, 0x31, 0x56, 0x5b, 0xcf, 0x77, 0x22};

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_FindHeader, 0x9aadf0)
{
    WorldScope world(ctx);
    InitCloseFakes();
    SavedFileTable savedFile;
    gFileTab[3] = reinterpret_cast<void *>(&FfOpen);
    gFileTab[4] = reinterpret_cast<void *>(&FfIsOpen);
    gFileTab[5] = reinterpret_cast<void *>(&FfClose);
    gFileTab[6] = reinterpret_cast<void *>(&FfPosition);
    gFileTab[7] = reinterpret_cast<void *>(&FfLength);
    gFileTab[9] = reinterpret_cast<void *>(&FfBegin);
    gFileTab[11] = reinterpret_cast<void *>(&FfRelative);
    gFileTab[12] = reinterpret_cast<void *>(&FfSeek);
    gFileTab[14] = reinterpret_cast<void *>(&FfRead);
    gFileTab[15] = reinterpret_cast<void *>(&FfReadCount);
    gFileTab[29] = reinterpret_cast<void *>(&FfSetPath);
    using Fn = uint32_t(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9aadf0);
    Tables plain = MakeTables(ctx);
    std::mt19937 rng(0x9aadf0);
    int hit = 0, missedMagic = 0, none = 0, opened = 0, openFailed = 0, tiny = 0, failedRead = 0, straddled = 0, later = 0;
    for (int i = 0; i < 3000; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        WireClose(img);
        const uint32_t sizePick = rng() % 10;
        const uint32_t size = sizePick == 0 ? rng() % 0x12 : sizePick == 1 ? 0x200 * (1 + rng() % 3) : 0x20 + rng() % 0x900;
        FakeFile file;
        file.bytes.resize(size);
        for (auto &b : file.bytes) b = static_cast<uint8_t>(rng() % 4 == 0 ? 0x80 : rng()); // 0x80 often: memchr finds false starts
        bool cut = false;
        for (uint32_t n = rng() % 5; n > 0 && size >= 24; --n) {
            uint32_t at = rng() % (size - 15);
            if (rng() % 4 == 0 && size > 0x200 + 8) at = 0x1f0 + rng() % 12; // across the first chunk's end
            const uint32_t keep = rng() % 8 == 0 ? 8 + rng() % 7 : 16; // sometimes only part of the marker
            std::memcpy(file.bytes.data() + at, kMarkerBytes, (std::min)(keep, size - at));
            cut = cut || (at < 0x200 && at + 16 > 0x200);
            if (at + 20 <= size) std::memcpy(file.bytes.data() + at + 16, rng() % 2 ? "DBPF" : "DBPX", 4);
        }
        file.open = rng() % 3 != 0;
        file.opens = rng() % 6 != 0;
        file.failCountAt = rng() % 6 == 0 ? static_cast<int>(rng() % 3) : -1;
        std::memcpy(gArena, img.data(), kBytes);
        Snapshot snap;
        snap.Take();
        auto run = [&](auto fn) {
            return WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
                gFf = file;
                const uint64_t r = fn(self);
                gLog.push_back(gFf.open);
                gLog.push_back(gFf.pos);
                return r;
            });
        };
        auto a = run([&](uint8_t *self) -> uint64_t { return orig(self); });
        auto b = run([&](uint8_t *self) -> uint64_t { return Obj(self)->FindHeader(); });
        WorldCompare(ctx, "FindHeader", a, b, b);
        const uint32_t found = static_cast<uint32_t>(a.result);
        const bool magic = found != 0xffffffffu && found + 4 <= size && std::memcmp(file.bytes.data() + found, "DBPF", 4) == 0;
        hit += magic;
        missedMagic += found != 0xffffffffu && !magic;
        none += found == 0xffffffffu && size > 0x10;
        opened += !file.open && file.opens;
        openFailed += !file.open && !file.opens;
        tiny += size <= 0x10;
        failedRead += file.failCountAt >= 0;
        straddled += cut && found != 0xffffffffu;
        later += found != 0xffffffffu && found >= 0x200;
    }
    std::printf("  FindHeader coverage: DBPF found %d, a marker without DBPF %d, nothing %d, opened here %d, open failed %d, tiny files %d, "
                "read failures %d, a marker across chunks %d, past the first chunk %d\n",
                hit, missedMagic, none, opened, openFailed, tiny, failedRead, straddled, later);
    if (hit < 400 || missedMagic < 200 || none < 300 || opened < 400 || openFailed < 100 || tiny < 100 || failedRead < 200 || straddled < 100 ||
        later < 300)
        ctx.Fail("FindHeader: setup reaches too little");
}

// ---- Flush's commit steps: WriteIndex (0x9b1d50) and WriteHoles (0x9b1a70) ----
//
// The real AddFreeRangeTo, AllocateSpaceUnlocked and RangeTree::AddRange run on the arena's trees (+0x184, +0x18c,
// +0x194, built with RZ nodes); the file's length (slot 7) is the trial's, its SetLength (slot 8) logs and answers from
// a schedule; own slot 58 (WriteAt) logs the size and a hash of the bytes and answers gWriteOk. Compared: the result,
// the calls, the arena and the world's allocator and core.
namespace {

constexpr int64_t kWiCount = 210, kWiEntries = 211;
uint32_t gWiCount = 0;
std::vector<uint32_t> gWiWords;

uint32_t __fastcall WiCount(void *self, void *)
{
    Push(kWiCount, Off(self), gWiCount);
    return gWiCount;
}

void __fastcall WiEntries(void *self, void *, std::vector<uint32_t> *out, void *ns, uint32_t version)
{
    Push(kWiEntries, Off(self), version);
    gLog.push_back(ns == nullptr ? 0 : Off(ns));
    out->assign(gWiWords.begin(), gWiWords.end());
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_WriteIndex, 0x9b1d50)
{
    WorldScope world(ctx);
    InitCloseFakes();
    StoreSlots slots;
    gStoreTab[3] = reinterpret_cast<void *>(&WiCount);
    gStoreTab[21] = reinterpret_cast<void *>(&WiEntries);
    gFileTab[8] = reinterpret_cast<void *>(&RecSetLength);
    using Fn = uint8_t(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9b1d50);
    Tables plain = MakeTables(ctx);
    plain.primary[58] = reinterpret_cast<void *>(&WriteHashed58);
    std::mt19937 rng(0x9b1d50);
    int idle = 0, emptied = 0, inPlace = 0, moved = 0, pending = 0, allocFailed = 0, written = 0, writeFailed = 0;
    for (int i = 0; i < 3000; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        WireClose(img);
        const uint32_t mode = rng() % 6 == 0 ? rng() % 3 : 3;
        SetWord(img, 0x24, mode);
        img[0x34] = rng() % 6 == 0 ? 0 : static_cast<uint8_t>(1 + rng() % 3);
        const uint32_t version = rng() % 6 == 0 ? (rng() % 2 ? 0 : 3) : 1 + rng() % 2;
        SetWord(img, 0x154, version);
        const uint32_t words = version == 1 ? 5 : version == 2 ? 6 : 0;
        gWiCount = rng() % 6 == 0 ? 0 : 1 + rng() % 40;
        const uint32_t size = gWiCount * words * 4;
        const uint32_t length = 0x2000 + rng() % 0x4000;
        gLengths.assign(1, length);
        const uint32_t oldSize = rng() % 3 == 0 ? size : (rng() % 4 == 0 ? 0 : 4 * (1 + rng() % 200));
        SetWord(img, 0x13c, rng() % 50);
        SetWord(img, 0x140, rng() % 5 == 0 ? 0 : 0x60 + rng() % (length - 0x1000));
        SetWord(img, 0x144, oldSize);
        uint32_t options = rng() % 4 == 0 ? 0x10 : 0;
        options |= rng() % 3 != 0 ? 2 : 0; // AllocateSpaceUnlocked looks in the tree with bit 1
        options |= rng() % 2 ? 1 : 0;
        SetWord(img, 0x17c, options);
        gWiWords.resize(size / 4);
        for (auto &w : gWiWords) w = static_cast<uint32_t>(rng());
        gSetLenAnswers = {static_cast<uint8_t>(rng() % 5 != 0), static_cast<uint8_t>(rng() % 3 != 0)};
        gWriteOk = rng() % 4 != 0;
        std::memcpy(gArena, img.data(), kBytes);
        BuildRanges(rng, 0x184, 6);
        BuildRanges(rng, 0x18c, 3);
        BuildRanges(rng, 0x194, 2);
        Snapshot snap;
        snap.Take();
        auto run = [&](auto fn) {
            return WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
                gLengthIdx = 0;
                gSetLenIdx = 0;
                return fn(self) & 0xff;
            });
        };
        auto a = run([&](uint8_t *self) -> uint64_t { return orig(self); });
        auto b = run([&](uint8_t *self) -> uint64_t { return Obj(self)->WriteIndex(); });
        WorldCompare(ctx, "WriteIndex", a, b, b);
        const bool active = (mode & 2) != 0 && img[0x34] != 0;
        const bool wrote = std::find(a.log.begin(), a.log.end(), kWrite58h) != a.log.end();
        idle += !active;
        emptied += active && size == 0;
        inPlace += active && size != 0 && size == oldSize && (options & 0x10) == 0;
        moved += active && size != 0 && (size != oldSize || (options & 0x10) != 0);
        pending += active && size != 0 && (options & 0x10) != 0 && oldSize != 0;
        allocFailed += active && size != 0 && !wrote && a.result == 0;
        written += wrote && a.result != 0;
        writeFailed += wrote && a.result == 0;
    }
    std::printf("  WriteIndex coverage: idle %d, emptied %d, in place %d, moved %d, old extent to +0x18c %d, no room %d, written %d, "
                "write failed %d\n",
                idle, emptied, inPlace, moved, pending, allocFailed, written, writeFailed);
    if (idle < 300 || emptied < 200 || inPlace < 300 || moved < 1000 || pending < 200 || allocFailed < 30 || written < 800 || writeFailed < 300)
        ctx.Fail("WriteIndex: setup reaches too little");
}

DIFF_TEST(cGZDBSegmentPackedFile_WriteHoles, 0x9b1a70)
{
    WorldScope world(ctx);
    InitCloseFakes();
    gFileTab[8] = reinterpret_cast<void *>(&RecSetLength);
    using Fn = uint8_t(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9b1a70);
    Tables plain = MakeTables(ctx);
    plain.primary[58] = reinterpret_cast<void *>(&WriteHashed58);
    std::mt19937 rng(0x9b1a70);
    int idle = 0, cut = 0, resized = 0, allocFailed = 0, written = 0, writeFailed = 0, nothing = 0, merged = 0;
    for (int i = 0; i < 3000; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        WireClose(img);
        const uint32_t mode = rng() % 6 == 0 ? rng() % 3 : 3;
        SetWord(img, 0x24, mode);
        img[0x19d] = rng() % 6 == 0 ? 0 : static_cast<uint8_t>(1 + rng() % 3);
        uint32_t options = rng() % 4 == 0 ? 0x10 : 0;
        options |= rng() % 3 != 0 ? 2 : 0;
        SetWord(img, 0x17c, options);
        gSetLenAnswers = {static_cast<uint8_t>(rng() % 5 != 0), static_cast<uint8_t>(rng() % 3 != 0)};
        gWriteOk = rng() % 4 != 0;
        std::memcpy(gArena, img.data(), kBytes);
        auto free = BuildRanges(rng, 0x184, rng() % 5 == 0 ? 0 : 7);
        auto pend = BuildRanges(rng, 0x18c, rng() % 2 ? 0 : 4);
        BuildRanges(rng, 0x194, 3);
        // The file ends inside, at or after the last free range.
        uint32_t lastEnd = free.empty() ? 0 : free.back().first + free.back().second;
        const uint32_t pick = rng() % 4;
        const uint32_t length = free.empty() || pick == 3 ? 0x100 + rng() % 0x100 : pick == 0 ? lastEnd : pick == 1 ? lastEnd - 1 : lastEnd + 1 + rng() % 8;
        gLengths.assign(1, length);
        const uint32_t ranges = static_cast<uint32_t>(free.size() + pend.size());
        uint32_t holeSize = rng() % 3 == 0 ? ranges * 8 : (rng() % 3 == 0 ? 0 : 8 * (rng() % 10));
        uint32_t f[3] = {rng() % 12, rng() % 4 == 0 ? 0 : 0x60 + rng() % 0x400, holeSize};
        std::memcpy(gArena + 0x148, f, sizeof(f));
        Snapshot snap;
        snap.Take();
        auto run = [&](auto fn) {
            return WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
                gLengthIdx = 0;
                gSetLenIdx = 0;
                return fn(self) & 0xff;
            });
        };
        auto a = run([&](uint8_t *self) -> uint64_t { return orig(self); });
        auto b = run([&](uint8_t *self) -> uint64_t { return Obj(self)->WriteHoles(); });
        WorldCompare(ctx, "WriteHoles", a, b, b);
        const bool active = (mode & 2) != 0 && img[0x19d] != 0;
        const bool wrote = std::find(a.log.begin(), a.log.end(), kWrite58h) != a.log.end();
        const bool setLength = std::find(a.log.begin(), a.log.end(), kSetLength) != a.log.end();
        idle += !active;
        cut += active && !free.empty() && pick <= 1 && setLength;
        resized += active && (ranges * 8 != holeSize || (options & 0x10) != 0);
        allocFailed += active && !wrote && a.result == 0;
        written += wrote && a.result != 0;
        writeFailed += wrote && a.result == 0;
        nothing += active && !wrote && a.result != 0;
        merged += wrote && !pend.empty();
    }
    std::printf("  WriteHoles coverage: idle %d, last range cut %d, resized %d, no room %d, written %d, write failed %d, nothing to write %d, "
                "with pending ranges %d\n",
                idle, cut, resized, allocFailed, written, writeFailed, nothing, merged);
    if (idle < 300 || cut < 600 || resized < 1000 || allocFailed < 30 || written < 800 || writeFailed < 300 || nothing < 100 || merged < 400)
        ctx.Fail("WriteHoles: setup reaches too little");
}

// ---- StoreWrittenRecord (0x9b1fe0) ----
//
// The record is a fake in the arena (+0x6c0; kind word at +0x44): GetSize (slot 52), SeekAbsolute (55), GetFieldVoid (27,
// data from the trial: repetitive or random, so TryCompress keeps or drops it) and the memory record's copy into the file
// (64) log and answer from the trial. The index is the store fake (RemoveRecord 14, AddRecord 13); own slots 44
// (AllocateSpace: an offset from the trial, answers from a schedule) and 58 (WriteAt) are fakes. The real PassesFilter,
// TryCompress (on the image's GeneralAllocator), AdjustRefs (with or without the reference maps), AddFreeRangeTo and
// RangeTree::AddRange run. Compared: the result, the calls, the arena and the world's allocator and core.
namespace {

constexpr size_t kCrRecord = 0x6c0, kCrKey = 0x7e0; // the record object (to +0x48), the key (16 bytes)
constexpr int64_t kCrSize = 220, kCrSeek = 221, kCrRead = 222, kCrCopy = 223, kCrAlloc = 224;
void *gCrTab[72];
uint32_t gCrSize = 0;
uint8_t gCrSeekOk = 1, gCrReadOk = 1, gCrCopyOk = 1;
bool gCrRepetitive = true;
uint32_t gCrSeed = 0;
std::vector<uint8_t> gCrAllocAnswers;
size_t gCrAllocIndex = 0;
uint32_t gCrOffsets[2] = {};

uint32_t __fastcall CrSize(void *self, void *)
{
    Push(kCrSize, Off(self), gCrSize);
    return gCrSize;
}

bool __fastcall CrSeek(void *self, void *, uint32_t position)
{
    Push(kCrSeek, Off(self), position);
    return gCrSeekOk != 0;
}

bool __fastcall CrRead(void *self, void *, void *to, uint32_t size)
{
    Push(kCrRead, Off(self), size);
    std::mt19937 fill(gCrSeed);
    auto *bytes = static_cast<uint8_t *>(to);
    for (uint32_t k = 0; k < size; ++k) bytes[k] = gCrRepetitive ? static_cast<uint8_t>("abcabcabd"[k % 9]) : static_cast<uint8_t>(fill());
    return gCrReadOk != 0;
}

uint8_t __fastcall CrCopy(void *self, void *, uint32_t offset, uint32_t size)
{
    Push(kCrCopy, Off(self), offset);
    gLog.push_back(size);
    return gCrCopyOk;
}

uint8_t __fastcall CrAlloc(void *self, void *, uint32_t *out, uint32_t length)
{
    const size_t call = gCrAllocIndex++;
    Push(kCrAlloc, Off(self), length);
    gLog.push_back(*out);
    *out = gCrOffsets[(std::min<size_t>)(call, 1)];
    return gCrAllocAnswers[(std::min)(call, gCrAllocAnswers.size() - 1)];
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_StoreWrittenRecord, 0x9b1fe0)
{
    ImageAllocScope image(ctx);
    WorldScope world(ctx);
    InitRecordFakes();
    // EnsureStore may swap the fake store for a fresh modifiable index: its add and remove slots are the same fakes.
    SlotPatch p13(ctx, cGZDBPFIndexModifiable::kVTable, 13, reinterpret_cast<void *>(&RecStore13));
    OurStoreSlotPatch o13(13, reinterpret_cast<void *>(&RecStore13));
    SlotPatch p14(ctx, cGZDBPFIndexModifiable::kVTable, 14, reinterpret_cast<void *>(&RecStore14));
    OurStoreSlotPatch o14(14, reinterpret_cast<void *>(&RecStore14));
    gStoreTab[1] = reinterpret_cast<void *>(&RecStoreCurrent);
    gStoreTab[13] = reinterpret_cast<void *>(&RecStore13);
    gStoreTab[14] = reinterpret_cast<void *>(&RecStore14);
    gCrTab[27] = reinterpret_cast<void *>(&CrRead);
    gCrTab[52] = reinterpret_cast<void *>(&CrSize);
    gCrTab[55] = reinterpret_cast<void *>(&CrSeek);
    gCrTab[64] = reinterpret_cast<void *>(&CrCopy);
    using Fn = uint8_t(__thiscall *)(void *, void *, const uint32_t *);
    auto orig = ctx.Original<Fn>(0x9b1fe0);
    Tables plain = MakeTables(ctx);
    plain.primary[44] = reinterpret_cast<void *>(&CrAlloc);
    plain.primary[58] = reinterpret_cast<void *>(&WriteHashed58);
    std::mt19937 rng(0x9b1fe0);
    int idle = 0, removedOld = 0, empty = 0, compressed = 0, compressFailedWrite = 0, plainStored = 0, plainRefused = 0, noRoom = 0,
        filtered = 0, tracked = 0;
    for (int i = 0; i < 3000; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        Wire(img);
        const uint32_t mode = rng() % 8 == 0 ? rng() % 3 : 3;
        SetWord(img, 0x24, mode);
        img[0x1a8] = static_cast<uint8_t>(rng() % 2);
        img[0x1a9] = static_cast<uint8_t>(rng() % 2);
        img[0x1aa] = 1; // accept every type and group (the sets at +0x1ac / +0x1b0 stay out of this test)
        SetPointer(img, 0x1ac, nullptr);
        SetPointer(img, 0x1b0, nullptr);
        SetWord(img, 0x17c, rng() % 2 ? 0x10 : 0);
        SetPointer(img, kCrRecord, gCrTab);
        SetWord(img, kCrRecord + 0x44, rng() % 4 == 0 ? static_cast<uint32_t>(rng() % 3) : 3);
        // Types and groups mostly among the reference maps' small keys, so their counts show the adjustments.
        uint32_t key[4] = {rng() % 8 == 0 ? 0xe86b1eefu : rng() % 24, rng() % 24, static_cast<uint32_t>(rng()), rng() % 3};
        std::memcpy(img.data() + kCrKey, key, sizeof(key));
        const uint32_t pick = rng() % 10;
        gCrSize = pick == 0 ? 0 : pick == 1 ? 1 + rng() % 49 : 50 + rng() % 3000;
        gCrRepetitive = rng() % 3 != 0;
        gCrSeed = static_cast<uint32_t>(rng());
        gCrSeekOk = rng() % 10 != 0;
        gCrReadOk = rng() % 10 != 0;
        gCrCopyOk = rng() % 5 != 0 ? static_cast<uint8_t>(1 + rng() % 3) : 0;
        gRemoveOk = rng() % 2 == 0;
        gRemoved[0] = rng() % 4 == 0 ? 0 : 0x100 + rng() % 0x1000;
        gRemoved[1] = rng() % 4 == 0 ? 0 : 1 + rng() % 0x100;
        gRemoved[2] = static_cast<uint32_t>(rng()) | (rng() % 2 ? 0x80000000u : 0);
        gCrAllocAnswers = {static_cast<uint8_t>(rng() % 6 != 0), static_cast<uint8_t>(rng() % 6 != 0)};
        gCrOffsets[0] = rng() % 6 == 0 ? 0 : 0x60 + rng() % 0x10000;
        gCrOffsets[1] = rng() % 6 == 0 ? 0 : 0x60 + rng() % 0x10000;
        gWriteOk = rng() % 4 != 0;
        std::memcpy(gArena, img.data(), kBytes);
        BuildRanges(rng, 0x184, 5);
        BuildRanges(rng, 0x18c, 3);
        BuildRanges(rng, 0x194, 2);
        const bool track = rng() % 2 == 0;
        gArena[0x19f] = track ? 1 : 0;
        const uint32_t mapSeed = static_cast<uint32_t>(rng());
        Snapshot snap;
        snap.Take();
        auto run = [&](auto fn) {
            return WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
                gCrAllocIndex = 0;
                return WithMaps(self, track, mapSeed, [&](uint8_t *s) -> uint64_t { return fn(s) & 0xff; });
            });
        };
        auto a = run([&](uint8_t *self) -> uint64_t { return orig(self, self + kCrRecord, reinterpret_cast<const uint32_t *>(self + kCrKey)); });
        auto b = run([&](uint8_t *self) -> uint64_t {
            return Obj(self)->StoreWrittenRecord(self + kCrRecord, reinterpret_cast<const uint32_t *>(self + kCrKey));
        });
        WorldCompare(ctx, "StoreWrittenRecord", a, b, b);
        auto saw = [&](int64_t kind) { return std::find(a.log.begin(), a.log.end(), kind) != a.log.end(); };
        const bool active = (mode & 2) != 0;
        const int allocs = static_cast<int>(std::count(a.log.begin(), a.log.end(), kCrAlloc));
        idle += !active;
        removedOld += active && gRemoveOk;
        empty += active && gCrSize == 0;
        compressed += active && a.result != 0 && saw(kWrite58h) && gWriteOk;
        compressFailedWrite += active && saw(kWrite58h) && !gWriteOk;
        plainStored += active && saw(kCrCopy) && a.result != 0;
        plainRefused += active && allocs > 0 && !saw(kCrCopy) && !saw(kWrite58h) && a.result == 0;
        noRoom += active && allocs > 0 && a.result == 0 && !saw(kCrCopy);
        filtered += active && gCrSize != 0 && !saw(kCrSeek);
        tracked += active && track && a.result != 0;
    }
    std::printf("  StoreWrittenRecord coverage: idle %d, old entry removed %d, empty %d, stored compressed %d, compressed write failed %d, "
                "stored plain %d, plain refused %d, no room %d, filtered out %d, with reference maps %d\n",
                idle, removedOld, empty, compressed, compressFailedWrite, plainStored, plainRefused, noRoom, filtered, tracked);
    if (idle < 150 || removedOld < 1000 || empty < 200 || compressed < 400 || compressFailedWrite < 100 || plainStored < 400 || plainRefused < 100 ||
        noRoom < 150 || filtered < 300 || tracked < 800)
        ctx.Fail("StoreWrittenRecord: setup reaches too little");
}

// ---- CopyOneRecord (0x9a8be0) ----
//
// `other` is a fake segment in the arena (+0x7a0) whose QueryInterface hands out a fake packed-file object (+0x7c0).
// The segment slots (20, 21, 26, 27, 28) are shared fakes for `other` and this file's subobject, the packed-file slots
// (2, 27, 28, 29, 41, 42) shared fakes for the packed object and this file's own table; each answers from the trial by
// which object it was called on. LookupRecordEnd (0x9abb50 and ours) is a fake as well. The readers fill the block and
// may shorten the size; the writers log a hash of what they were given. Answers carry high garbage bits where the
// original tests only al, and none where it tests the whole word.
namespace {

constexpr size_t kCoOther = 0x7a0, kCoPacked = 0x7c0, kCoKey = 0x7e0;
constexpr int64_t kCoQuery = 240, kCoRelease = 241, kCoTest = 242, kCoSize = 243, kCoDelete = 244, kCoRead = 245, kCoWrite = 246,
                  kCoStored = 247, kCoReadData = 248, kCoWriteData = 249, kCoCompressed = 250, kCoFull = 251, kCoLookup = 252;
void *gCoOtherTab[64], *gCoPackedTab[64];

// Index 1: `other` or its packed object; index 0: this file (subobject or primary).
struct CoSide {
    uint32_t test, size, read, write, stored, readData, writeData, compressed, shrink;
};
CoSide gCo[2];
uint8_t gCoQueryOk = 0;
uint32_t gCoLookupAnswer = 0;

int CoSideOf(void *self)
{
    return (self == gArena + kCoOther || self == gArena + kCoPacked) ? 1 : 0;
}

uint32_t CoHash(const void *data, uint32_t size)
{
    uint32_t h = 17;
    for (uint32_t i = 0; i < size; ++i) h = h * 31 + static_cast<const uint8_t *>(data)[i];
    return h;
}

void CoFill(void *dst, uint32_t *size, uint32_t seed, uint32_t shrink)
{
    for (uint32_t i = 0; i < *size; ++i) static_cast<uint8_t *>(dst)[i] = static_cast<uint8_t>(seed + i * 7);
    if (shrink != 0 && *size >= shrink) *size -= shrink;
}

uint8_t __fastcall CoQuery(void *self, void *, uint32_t iid, void **out)
{
    Push(kCoQuery, Off(self), iid);
    gLog.push_back(*out == nullptr ? 0 : Off(*out));
    if (gCoQueryOk != 0) *out = gArena + kCoPacked;
    return gCoQueryOk;
}

uint32_t __fastcall CoRelease(void *self, void *)
{
    Push(kCoRelease, Off(self), 0);
    return 0x5a;
}

uint32_t __fastcall CoTest(void *self, void *, const uint32_t *key)
{
    Push(kCoTest, Off(self), key[0]);
    return gCo[CoSideOf(self)].test;
}

uint32_t __fastcall CoSize(void *self, void *, const uint32_t *key)
{
    Push(kCoSize, Off(self), key[1]);
    return gCo[CoSideOf(self)].size;
}

void __fastcall CoDelete(void *self, void *, const uint32_t *key)
{
    Push(kCoDelete, Off(self), key[2]);
}

uint32_t __fastcall CoRead(void *self, void *, const uint32_t *key, void *dst, uint32_t *size)
{
    Push(kCoRead, Off(self), *size);
    CoFill(dst, size, key[3] + 1, gCo[CoSideOf(self)].shrink);
    return gCo[CoSideOf(self)].read;
}

uint32_t __fastcall CoWrite(void *self, void *, const uint32_t *key, const void *data, uint32_t size)
{
    Push(kCoWrite, Off(self), size);
    gLog.push_back(CoHash(data, size));
    gLog.push_back(key[0]);
    return gCo[CoSideOf(self)].write;
}

uint32_t __fastcall CoStored(void *self, void *, const uint32_t *key)
{
    Push(kCoStored, Off(self), key[1]);
    return gCo[CoSideOf(self)].stored;
}

uint32_t __fastcall CoReadData(void *self, void *, const uint32_t *key, void *dst, uint32_t *size)
{
    Push(kCoReadData, Off(self), *size);
    CoFill(dst, size, key[2] + 3, gCo[CoSideOf(self)].shrink);
    return gCo[CoSideOf(self)].readData;
}

uint32_t __fastcall CoWriteData(void *self, void *, const uint32_t *key, const void *data, uint32_t size)
{
    Push(kCoWriteData, Off(self), size);
    gLog.push_back(CoHash(data, size));
    gLog.push_back(key[3]);
    return gCo[CoSideOf(self)].writeData;
}

uint32_t __fastcall CoCompressed(void *self, void *, const uint32_t *key)
{
    Push(kCoCompressed, Off(self), key[0]);
    return gCo[CoSideOf(self)].compressed;
}

void __fastcall CoFull(void *self, void *, const uint32_t *key, uint32_t useStored, uint32_t size)
{
    Push(kCoFull, Off(self), useStored & 0xff);
    gLog.push_back(size);
    gLog.push_back(key[1]);
}

uint32_t __fastcall CoLookup(void *self, void *, const uint32_t *key)
{
    Push(kCoLookup, Off(self), key[0]);
    return gCoLookupAnswer;
}

uint32_t CoPickSize(std::mt19937 &rng)
{
    const uint32_t pick = rng() % 8;
    return pick == 0 ? 0xffffffffu : pick == 1 ? 0 : 1 + rng() % 300;
}

// An answer whose low byte decides: zero or not, with garbage in the upper bytes either way.
uint32_t CoByteAnswer(std::mt19937 &rng, bool yes)
{
    return (yes ? 1 + rng() % 0xff : 0) | (rng() % 2 ? 0 : (static_cast<uint32_t>(rng()) & 0xffffff00u));
}

// An answer tested as a whole word: zero, a value with only high bits, or a small one.
uint32_t CoWordAnswer(std::mt19937 &rng)
{
    const uint32_t pick = rng() % 4;
    return pick == 0 ? 0 : pick == 1 ? 0x100u << (rng() % 24) : 1 + rng() % 0xff;
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_CopyOneRecord, 0x9a8be0)
{
    ImageAllocScope image(ctx);
    WorldScope world(ctx);
    InitFakes();
    Redirect lookup(ctx, 0x9abb50, reinterpret_cast<void *>(&CoLookup));
    Redirect lookupOurs(Addr(&cGZDBSegmentPackedFile::LookupRecordEnd), reinterpret_cast<void *>(&CoLookup));
    gCoOtherTab[0] = reinterpret_cast<void *>(&CoQuery);
    gCoOtherTab[20] = reinterpret_cast<void *>(&CoTest);
    gCoOtherTab[21] = reinterpret_cast<void *>(&CoSize);
    gCoOtherTab[26] = reinterpret_cast<void *>(&CoDelete);
    gCoOtherTab[27] = reinterpret_cast<void *>(&CoRead);
    gCoOtherTab[28] = reinterpret_cast<void *>(&CoWrite);
    gCoPackedTab[2] = reinterpret_cast<void *>(&CoRelease);
    gCoPackedTab[27] = reinterpret_cast<void *>(&CoStored);
    gCoPackedTab[28] = reinterpret_cast<void *>(&CoReadData);
    gCoPackedTab[29] = reinterpret_cast<void *>(&CoWriteData);
    gCoPackedTab[41] = reinterpret_cast<void *>(&CoCompressed);
    gCoPackedTab[42] = reinterpret_cast<void *>(&CoFull);
    using Fn = uint8_t(__thiscall *)(void *, void *, const uint32_t *, uint32_t, uint32_t);
    auto orig = ctx.Original<Fn>(0x9a8be0);
    Tables plain = MakeTables(ctx);
    plain.sub[20] = reinterpret_cast<void *>(&CoTest);
    plain.sub[21] = reinterpret_cast<void *>(&CoSize);
    plain.sub[26] = reinterpret_cast<void *>(&CoDelete);
    plain.sub[27] = reinterpret_cast<void *>(&CoRead);
    plain.sub[28] = reinterpret_cast<void *>(&CoWrite);
    plain.primary[27] = reinterpret_cast<void *>(&CoStored);
    plain.primary[28] = reinterpret_cast<void *>(&CoReadData);
    plain.primary[29] = reinterpret_cast<void *>(&CoWriteData);
    plain.primary[41] = reinterpret_cast<void *>(&CoCompressed);
    plain.primary[42] = reinterpret_cast<void *>(&CoFull);
    std::mt19937 rng(0x9a8be0);
    int missing = 0, packedMoved = 0, packedFailed = 0, notCompressed = 0, noPacked = 0, plainMoved = 0, plainFailed = 0, kept = 0,
        deleted = 0, intoThis = 0, fromThis = 0;
    for (int i = 0; i < 4000; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        Wire(img);
        SetPointer(img, kCoOther, gCoOtherTab);
        SetPointer(img, kCoPacked, gCoPackedTab);
        const uint32_t key[4] = {static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng())};
        std::memcpy(img.data() + kCoKey, key, sizeof(key));
        std::memcpy(gArena, img.data(), kBytes);
        for (CoSide &side : gCo) {
            side.test = CoByteAnswer(rng, rng() % 8 != 0);
            side.size = CoPickSize(rng);
            side.read = CoWordAnswer(rng);
            side.write = CoByteAnswer(rng, rng() % 4 != 0);
            side.stored = CoPickSize(rng);
            const uint32_t readPick = rng() % 5;
            side.readData = readPick == 0 ? 0xffffffffu : readPick == 1 ? 0 : CoWordAnswer(rng);
            side.writeData = rng() % 4 == 0 ? 0 : CoWordAnswer(rng) | 1u << (rng() % 32);
            side.compressed = CoByteAnswer(rng, rng() % 3 != 0);
            side.shrink = rng() % 3 == 0 ? 1 + rng() % 4 : 0;
        }
        gCoQueryOk = rng() % 4 != 0;
        gCoLookupAnswer = rng() % 4 == 0 ? 0xffffffffu : static_cast<uint32_t>(rng() % 5000);
        const uint32_t direction = CoByteAnswer(rng, rng() % 2 == 0);
        const uint32_t keep = CoByteAnswer(rng, rng() % 3 == 0);
        Snapshot snap;
        snap.Take();
        auto a = WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
            return orig(self, self + kCoOther, reinterpret_cast<const uint32_t *>(self + kCoKey), direction, keep) & 0xff;
        });
        auto b = WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
            return Obj(self)->CopyOneRecord(self + kCoOther, reinterpret_cast<const uint32_t *>(self + kCoKey), direction, keep);
        });
        WorldCompare(ctx, "CopyOneRecord", a, b, b);
        auto saw = [&](int64_t kind) { return std::find(a.log.begin(), a.log.end(), kind) != a.log.end(); };
        const bool into = (direction & 0xff) != 0;
        const CoSide &src = gCo[into ? 1 : 0];
        intoThis += into && a.result != 0;
        fromThis += !into && a.result != 0;
        missing += (src.test & 0xff) == 0;
        noPacked += (src.test & 0xff) != 0 && !gCoQueryOk;
        notCompressed += gCoQueryOk && (src.test & 0xff) != 0 && (src.compressed & 0xff) == 0;
        packedMoved += saw(kCoFull);
        packedFailed += saw(kCoReadData) && !saw(kCoFull);
        plainMoved += saw(kCoWrite) && a.result != 0;
        plainFailed += saw(kCoRead) && a.result == 0;
        kept += a.result != 0 && (keep & 0xff) != 0;
        deleted += saw(kCoDelete);
    }
    std::printf("  CopyOneRecord coverage: into this %d, from this %d, source lacks it %d, no packed interface %d, not compressed %d, "
                "moved packed %d, packed move failed %d, moved plain %d, plain failed %d, kept %d, deleted %d\n",
                intoThis, fromThis, missing, noPacked, notCompressed, packedMoved, packedFailed, plainMoved, plainFailed, kept, deleted);
    if (intoThis < 300 || fromThis < 300 || missing < 200 || noPacked < 400 || notCompressed < 400 || packedMoved < 300 || packedFailed < 300 ||
        plainMoved < 300 || plainFailed < 300 || kept < 200 || deleted < 400)
        ctx.Fail("CopyOneRecord: setup reaches too little");
}
