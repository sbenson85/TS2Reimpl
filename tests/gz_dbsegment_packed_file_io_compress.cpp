// Differential tests for cGZDBSegmentPackedFile: compression, record reads and writes, record objects.
// Fixtures (the arena, the fake store and file objects, the comparison helpers): gz_dbsegment_io_fixtures.h.
#include "gz_dbsegment_io_fixtures.h"

DIFF_TEST(cGZDBSegmentPackedFile_Decompress, 0x9a9100)
{
    ImageAllocScope image(ctx);
    WorldScope world(ctx);
    using Fn = uint8_t(__stdcall *)(const void *, uint32_t, uint32_t, void **, uint32_t *);
    auto orig = ctx.Original<Fn>(0x9a9100);
    Tables plain = MakeTables(ctx);
    std::mt19937 rng(0x9a9100);
    int good = 0, suppliedGood = 0, mismatch = 0, tooSmall = 0, corrupt = 0, shortSource = 0, empty = 0;
    for (int i = 0; i < 2500; ++i) {
        ResetWorld();
        size_t n = rng() % 5 == 0 ? rng() % 8 : rng() % 6000;
        auto data = MakeData(rng, n, static_cast<int>(rng() % 4));
        auto packed = CompressWithTheGame(data);
        if (packed.empty()) continue;
        uint32_t srcLen = static_cast<uint32_t>(packed.size());
        switch (rng() % 10) {
        case 0: packed[rng() % packed.size()] ^= static_cast<uint8_t>(1 + rng() % 255); ++corrupt; break;
        case 1: srcLen = rng() % (srcLen + 1); ++shortSource; break;
        case 2: srcLen = rng() % 10; ++shortSource; break;
        case 3: srcLen = 0; ++shortSource; break;
        default: break;
        }
        packed.resize(packed.size() + 64); // a little slack behind the stream
        {
            // A damaged header can declare megabytes, which would make the allocator ask the OS for memory (addresses differ per run).
            cRZFastCompression3 sizer{};
            sizer.mVTable = GameAddress(cRZFastCompression3::kVTable);
            if (sizer.GetLengthOfDecompressedData(packed.data()) > 40000u) continue;
        }
        uint32_t expected = rng() % 6 == 0 ? static_cast<uint32_t>(n + 1 + rng() % 3) : static_cast<uint32_t>(n);
        mismatch += expected != n;
        std::memcpy(gArena + 0, MakeImage(rng).data(), kBytes);
        uint32_t supplied = 0, capacity = static_cast<uint32_t>(rng());
        bool useSupplied = rng() % 2;
        if (useSupplied) {
            static const int deltas[] = {0, 0, 0, 1, -1, 7, 100};
            int64_t cap = static_cast<int64_t>(n) + deltas[rng() % 7];
            capacity = static_cast<uint32_t>(cap < 0 ? 0 : cap);
            supplied = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(RZMemory::Malloc(capacity ? capacity : 1)));
            tooSmall += capacity < n;
        }
        std::memcpy(gArena + kBufPtrAt, &supplied, 4);
        std::memcpy(gArena + kSizeAt, &capacity, 4);
        Snapshot snap;
        snap.Take();
        auto run = [&](auto fn) {
            return WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
                return fn(self, reinterpret_cast<void **>(self + kBufPtrAt), reinterpret_cast<uint32_t *>(self + kSizeAt)) & 0xff;
            });
        };
        auto a = run([&](uint8_t *, void **buf, uint32_t *len) -> uint64_t { return orig(packed.data(), srcLen, expected, buf, len); });
        auto b = run([&](uint8_t *, void **buf, uint32_t *len) -> uint64_t { return PackedFileCompression::Decompress(packed.data(), srcLen, expected, buf, len); });
        WorldCompare(ctx, "Decompress", a, b, b);
        good += (a.result & 0xff) != 0;
        suppliedGood += (a.result & 0xff) != 0 && useSupplied;
        empty += n == 0;
    }
    if (good < 800 || suppliedGood < 300 || mismatch < 150 || tooSmall < 100 || corrupt < 150 || shortSource < 400 || empty < 25)
        ctx.Fail("Decompress: setup reaches too little (%d good, %d with a supplied buffer, %d expected-size mismatches, %d small buffers, %d corrupt, %d short sources, %d empty)", good,
                 suppliedGood, mismatch, tooSmall, corrupt, shortSource, empty);
}

namespace {

uint32_t MaxCompressedLength(uint32_t n)
{
    cRZFastCompression3 c{};
    c.mVTable = GameAddress(cRZFastCompression3::kVTable);
    return c.GetMaxLengthRequiredForCompressedData(n);
}

// One trial of Compress: `data` of `size` bytes, the output buffer pointer and size variable in the arena.
struct CompressTrial {
    const uint8_t *data;
    uint32_t size;
    uint32_t suppliedBuffer; // an address in the core (made before the snapshot), 0 for none
    uint32_t suppliedCapacity;
};

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_Compress, 0x9a8a00)
{
    ImageAllocScope image(ctx);
    WorldScope world(ctx);
    using Fn = uint8_t(__stdcall *)(const void *, uint32_t, void **, uint32_t *);
    auto orig = ctx.Original<Fn>(0x9a8a00);
    Tables plain = MakeTables(ctx);
    std::mt19937 rng(0x9a8a00);
    int good = 0, failed = 0, supplied = 0, tiny = 0, kinds[4] = {0, 0, 0, 0};
    static const uint32_t special[] = {0, 1, 2, 3, 4, 7, 8, 49, 50, 51, 255, 256, 4095};
    for (int i = 0; i < 2500; ++i) {
        ResetWorld();
        uint32_t n = rng() % 4 == 0 ? special[rng() % 13] : (rng() % 3 == 0 ? rng() % 30000 : rng() % 3000);
        int kind = static_cast<int>(rng() % 4);
        auto data = MakeData(rng, n, kind);
        // Now and then the data cannot be read: the compressor fails (its exception handler) and the call must undo its allocation.
        const void *dataPointer = (n >= 1 && rng() % 15 == 0) ? reinterpret_cast<const void *>(8) : data.data();
        std::memcpy(gArena, MakeImage(rng).data(), kBytes);
        uint32_t buffer = 0, capacity = static_cast<uint32_t>(rng());
        bool useSupplied = rng() % 2;
        if (useSupplied) {
            capacity = MaxCompressedLength(n) + (rng() % 3 == 0 ? 0 : rng() % 100);
            buffer = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(RZMemory::Malloc(capacity)));
        }
        std::memcpy(gArena + kBufPtrAt, &buffer, 4);
        std::memcpy(gArena + kSizeAt, &capacity, 4);
        Snapshot snap;
        snap.Take();
        auto run = [&](auto fn) {
            return WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
                return fn(reinterpret_cast<void **>(self + kBufPtrAt), reinterpret_cast<uint32_t *>(self + kSizeAt)) & 0xff;
            });
        };
        auto a = run([&](void **buf, uint32_t *len) -> uint64_t { return orig(dataPointer, n, buf, len); });
        auto b = run([&](void **buf, uint32_t *len) -> uint64_t { return PackedFileCompression::Compress(dataPointer, n, buf, len); });
        WorldCompare(ctx, "Compress", a, b, b);
        good += (a.result & 0xff) != 0;
        failed += (a.result & 0xff) == 0;
        supplied += useSupplied;
        tiny += n < 8;
        ++kinds[kind];
    }
    if (good < 1500 || failed < 30 || supplied < 800 || tiny < 100 || kinds[0] < 400 || kinds[1] < 400 || kinds[2] < 400 || kinds[3] < 400)
        ctx.Fail("Compress: setup reaches too little (%d compressed, %d failures, %d supplied, %d tiny)", good, failed, supplied, tiny);
}

// The sizes that need megabytes: the quick mode above 250,000 bytes and the stored (uncompressed) copy from 40,000,000.
DIFF_TEST(cGZDBSegmentPackedFile_CompressLarge, 0x9a8a00)
{
    ImageAllocScope image(ctx);
    BigCoreScope bigCore;
    WorldScope world(ctx);
    using Fn = uint8_t(__stdcall *)(const void *, uint32_t, void **, uint32_t *);
    auto orig = ctx.Original<Fn>(0x9a8a00);
    Tables plain = MakeTables(ctx);
    std::mt19937 rng(0x9a8a01);
    struct Case {
        uint32_t size;
        int kind;
        int supplied; // 0 none, 1 exactly enough, 2 one byte short (stored sizes only)
    };
    static const Case cases[] = {
        {250000, 1, 0}, {250001, 1, 0}, {250001, 3, 0}, {262144, 1, 1}, {300000, 3, 0},
        {40000000, 2, 0}, {40000000, 2, 1}, {40000000, 2, 2}, {40000001, 2, 0}, {40000001, 2, 2},
    };
    int stored = 0, quick = 0;
    for (const Case &c : cases) {
        ResetWorld();
        auto data = MakeData(rng, c.size, c.kind);
        if (c.size >= 40000000u) {
            for (size_t k = 0; k < data.size(); k += 4099) data[k] = static_cast<uint8_t>(rng()); // not all zeros
        }
        std::memcpy(gArena, MakeImage(rng).data(), kBytes);
        uint32_t buffer = 0, capacity = static_cast<uint32_t>(rng());
        if (c.supplied != 0) {
            capacity = c.size >= 40000000u ? c.size - (c.supplied == 2 ? 1 : 0) : MaxCompressedLength(c.size);
            buffer = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(RZMemory::Malloc(capacity)));
        }
        std::memcpy(gArena + kBufPtrAt, &buffer, 4);
        std::memcpy(gArena + kSizeAt, &capacity, 4);
        Snapshot snap;
        snap.Take();
        auto run = [&](auto fn) {
            return WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
                return fn(reinterpret_cast<void **>(self + kBufPtrAt), reinterpret_cast<uint32_t *>(self + kSizeAt)) & 0xff;
            });
        };
        auto a = run([&](void **buf, uint32_t *len) -> uint64_t { return orig(data.data(), c.size, buf, len); });
        auto b = run([&](void **buf, uint32_t *len) -> uint64_t { return PackedFileCompression::Compress(data.data(), c.size, buf, len); });
        WorldCompare(ctx, "CompressLarge", a, b, b);
        stored += c.size >= 40000000u;
        quick += c.size > 250000u && c.size < 40000000u;
    }
    if (stored < 5 || quick < 4) ctx.Fail("CompressLarge: setup reaches too little");
}

DIFF_TEST(cGZDBSegmentPackedFile_TryCompress, 0x9b0470)
{
    ImageAllocScope image(ctx);
    WorldScope world(ctx);
    using Fn = uint8_t(__stdcall *)(const void *, uint32_t, void **, uint32_t *);
    auto orig = ctx.Original<Fn>(0x9b0470);
    Tables plain = MakeTables(ctx);
    std::mt19937 rng(0x9b0470);
    // Compressed lengths right at the game's limit (the float at 0xdb13e8): data of 1000 bytes whose compressed length is
    // one below, at and one above 0.8 * 1000.
    std::vector<std::vector<uint8_t>> edge;
    {
        std::vector<uint8_t> prefix(1000);
        for (auto &b : prefix) b = static_cast<uint8_t>(rng());
        for (uint32_t cut = 600; cut < 1000 && edge.size() < 3; ++cut) {
            std::vector<uint8_t> d(1000, 0);
            std::copy(prefix.begin(), prefix.begin() + cut, d.begin());
            size_t length = CompressWithTheGame(d).size();
            if (length >= 799 && length <= 801 && (edge.empty() || CompressWithTheGame(edge.back()).size() != length)) edge.push_back(d);
        }
    }
    if (edge.size() != 3) ctx.Fail("TryCompress: no data at the compression limit found (%zu)", edge.size());
    int kept = 0, rejectedRatio = 0, rejectedSize = 0, atLimit = 0;
    static const uint32_t special[] = {0, 1, 49, 50, 51, 52, 100, 255};
    static const uint32_t refused[] = {16000001u, 16000002u, 0xffffffffu, 0x80000000u, 0, 1, 49};
    for (int i = 0; i < 2000; ++i) {
        ResetWorld();
        uint32_t n;
        std::vector<uint8_t> data;
        int mode = static_cast<int>(rng() % 10);
        if (mode == 0 && edge.size() == 3) {
            data = edge[rng() % 3];
            n = 1000;
            ++atLimit;
        } else if (mode == 1) {
            n = refused[rng() % 7];
            data = n < 1000 ? MakeData(rng, n, 1) : std::vector<uint8_t>(16);
        } else {
            n = rng() % 3 == 0 ? special[rng() % 8] : rng() % 6000;
            data = MakeData(rng, n, static_cast<int>(rng() % 4));
        }
        std::memcpy(gArena, MakeImage(rng).data(), kBytes);
        uint32_t buffer = static_cast<uint32_t>(rng()), capacity = static_cast<uint32_t>(rng());
        std::memcpy(gArena + kBufPtrAt, &buffer, 4);
        std::memcpy(gArena + kSizeAt, &capacity, 4);
        Snapshot snap;
        snap.Take();
        auto run = [&](auto fn) {
            return WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
                return fn(reinterpret_cast<void **>(self + kBufPtrAt), reinterpret_cast<uint32_t *>(self + kSizeAt)) & 0xff;
            });
        };
        auto a = run([&](void **buf, uint32_t *len) -> uint64_t { return orig(data.data(), n, buf, len); });
        auto b = run([&](void **buf, uint32_t *len) -> uint64_t { return PackedFileCompression::TryCompress(data.data(), n, buf, len); });
        WorldCompare(ctx, "TryCompress", a, b, b);
        bool ok = (a.result & 0xff) != 0;
        kept += ok;
        rejectedSize += !ok && (n - 0x32u > 0xf423ceu);
        rejectedRatio += !ok && !(n - 0x32u > 0xf423ceu);
    }
    if (kept < 300 || rejectedSize < 150 || rejectedRatio < 300 || atLimit < 100) ctx.Fail("TryCompress: setup reaches too little (%d kept, %d refused by size, %d by ratio, %d at the limit)", kept, rejectedSize, rejectedRatio, atLimit);
}

// The size limits of TryCompress that need megabytes: 16,000,000 bytes is still tried.
DIFF_TEST(cGZDBSegmentPackedFile_TryCompressLarge, 0x9b0470)
{
    ImageAllocScope image(ctx);
    BigCoreScope bigCore;
    WorldScope world(ctx);
    using Fn = uint8_t(__stdcall *)(const void *, uint32_t, void **, uint32_t *);
    auto orig = ctx.Original<Fn>(0x9b0470);
    Tables plain = MakeTables(ctx);
    std::mt19937 rng(0x9b0471);
    int tried = 0;
    for (uint32_t n : {16000000u, 16000001u}) {
        ResetWorld();
        auto data = MakeData(rng, n, 1);
        std::memcpy(gArena, MakeImage(rng).data(), kBytes);
        Snapshot snap;
        snap.Take();
        auto run = [&](auto fn) {
            return WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
                return fn(reinterpret_cast<void **>(self + kBufPtrAt), reinterpret_cast<uint32_t *>(self + kSizeAt)) & 0xff;
            });
        };
        auto a = run([&](void **buf, uint32_t *len) -> uint64_t { return orig(data.data(), n, buf, len); });
        auto b = run([&](void **buf, uint32_t *len) -> uint64_t { return PackedFileCompression::TryCompress(data.data(), n, buf, len); });
        WorldCompare(ctx, "TryCompressLarge", a, b, b);
        tried += n == 16000000u && (a.result & 0xff) != 0;
    }
    if (tried < 1) ctx.Fail("TryCompressLarge: the 16,000,000-byte case was not accepted");
}

DIFF_TEST(cGZDBSegmentPackedFile_ReadAndDecompress, 0x9a9260)
{
    ImageAllocScope image(ctx);
    WorldScope world(ctx);
    using Fn = uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, void **, uint32_t *);
    auto orig = ctx.Original<Fn>(0x9a9260);
    Tables plain = MakeTables(ctx);
    plain.primary[56] = reinterpret_cast<void *>(&ReadAtFake);
    std::mt19937 rng(0x9a9260);
    int good = 0, viaBlock = 0, viaStack = 0, readFailed = 0, noOffset = 0, edge = 0;
    for (int i = 0; i < 2000; ++i) {
        ResetWorld();
        // Mostly small payloads; a share is incompressible and a few thousand bytes so that the stored size crosses 0xc00.
        uint32_t n = rng() % 4 == 0 ? 2800 + rng() % 1000 : rng() % 1500;
        auto data = MakeData(rng, n, rng() % 3 == 0 ? 0 : static_cast<int>(rng() % 4));
        auto packed = CompressWithTheGame(data);
        if (packed.empty()) continue;
        if (rng() % 8 == 0) {
            // Incompressible data whose stored length is right at the edge of the stack buffer (0xbff, 0xc00, 0xc01).
            static const size_t sizes[] = {0xbff, 0xc00, 0xc01};
            size_t want = sizes[rng() % 3];
            for (uint32_t m = 2800; m < 3400; ++m) {
                auto trial = MakeData(rng, m, 0);
                auto candidate = CompressWithTheGame(trial);
                if (candidate.size() == want) {
                    data = trial;
                    n = m;
                    packed = candidate;
                    ++edge;
                    break;
                }
            }
        }
        uint32_t offset = rng() % 10 == 0 ? 0 : 1 + rng() % 5000;
        gFileImage.assign(offset + packed.size() + 16, 0);
        for (auto &b : gFileImage) b = static_cast<uint8_t>(rng());
        if (offset != 0) std::copy(packed.begin(), packed.end(), gFileImage.begin() + offset);
        gReadOk = rng() % 8 != 0;
        uint32_t stored = static_cast<uint32_t>(packed.size());
        {
            cRZFastCompression3 sizer{};
            sizer.mVTable = GameAddress(cRZFastCompression3::kVTable);
            std::vector<uint8_t> tmp(packed);
            tmp.resize(tmp.size() + 64);
            if (sizer.GetLengthOfDecompressedData(tmp.data()) > 40000u) continue;
        }
        uint32_t expected = rng() % 6 == 0 ? n + 1 : n;
        std::memcpy(gArena, MakeImage(rng).data(), kBytes);
        uint32_t supplied = 0, capacity = static_cast<uint32_t>(rng());
        if (rng() % 2) {
            capacity = n + (rng() % 3 == 0 ? 0 : rng() % 20);
            supplied = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(RZMemory::Malloc(capacity ? capacity : 1)));
        }
        std::memcpy(gArena + kBufPtrAt, &supplied, 4);
        std::memcpy(gArena + kSizeAt, &capacity, 4);
        Snapshot snap;
        snap.Take();
        auto run = [&](auto fn) {
            return WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
                return fn(self, reinterpret_cast<void **>(self + kBufPtrAt), reinterpret_cast<uint32_t *>(self + kSizeAt)) & 0xff;
            });
        };
        auto a = run([&](uint8_t *self, void **buf, uint32_t *len) -> uint64_t { return orig(self, offset, stored, expected, buf, len); });
        auto b = run([&](uint8_t *self, void **buf, uint32_t *len) -> uint64_t { return Obj(self)->ReadAndDecompress(offset, stored, expected, buf, len); });
        WorldCompare(ctx, "ReadAndDecompress", a, b, b);
        good += (a.result & 0xff) != 0;
        noOffset += offset == 0;
        readFailed += offset != 0 && !gReadOk;
        if (offset != 0) {
            viaBlock += stored > 0xc00;
            viaStack += stored <= 0xc00;
        }
    }
    if (good < 500 || viaBlock < 150 || viaStack < 500 || readFailed < 100 || noOffset < 100 || edge < 100)
        ctx.Fail("ReadAndDecompress: setup reaches too little (%d good, %d via a block, %d via the stack, %d read failures, %d offset 0, %d at the buffer edge)", good, viaBlock, viaStack,
                 readFailed, noOffset, edge);
}

DIFF_TEST(cGZDBSegmentPackedFile_ReadRecord, 0x9aa0f0)
{
    ImageAllocScope image(ctx);
    WorldScope world(ctx);
    InitRecordFakes();
    gRecSub[5] = reinterpret_cast<void *>(&RecSub5);
    gFileTab[12] = reinterpret_cast<void *>(&SeekRecording12);
    gFileTab[14] = reinterpret_cast<void *>(&Read14);
    using Fn = uint32_t(__thiscall *)(void *, const uint32_t *, void *, uint32_t *);
    using Member = uint32_t (SegmentView::*)(const uint32_t *, void *, uint32_t *);
    auto orig = ctx.Original<Fn>(0x9aa0f0);
    Tables plain = MakeTables(ctx);
    plain.primary[56] = reinterpret_cast<void *>(&ReadAtFake);
    plain.sub[5] = reinterpret_cast<void *>(&RecOpen5);
    Tables routed = plain;
    routed.sub[42] = Addr(static_cast<Member>(&SegmentView::DoReadRecord));
    std::mt19937 rng(0x9aa0f0);
    int unreadable = 0, pendingBusy = 0, missing = 0, sizeOnly = 0, compressedOk = 0, compressedFailed = 0, againMissing = 0, plainRead = 0, seekFailed = 0, readFailed = 0, shortCount = 0;
    for (int i = 0; i < 4000; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        Wire(img);
        std::vector<void *> addresses;
        PendingBuild pb = MakeRecords(img, rng, rng() % 4, addresses);
        static const uint8_t states[] = {0, 1, 2, 3, 3, 3, 3, 0x83, 0x02};
        img[0x24] = states[rng() % 9];
        img[0x28] = 0;
        img[0x29] = 0;
        SetWord(img, 0xf4, rng() % 3 ? 0 : rng() % 40);
        // The record in the store: compressed (full size above the stored size) or plain.
        bool compressedRecord = rng() % 2;
        uint32_t n = rng() % 5 == 0 ? rng() % 4 : 1 + rng() % 600;
        auto data = MakeData(rng, n, compressedRecord ? 1 + static_cast<int>(rng() % 2) : static_cast<int>(rng() % 4));
        auto packed = CompressWithTheGame(data);
        if (packed.empty()) continue;
        uint32_t offset = rng() % 20;
        uint32_t stored, full = n;
        std::vector<uint8_t> bytes;
        if (compressedRecord) {
            stored = static_cast<uint32_t>(packed.size());
            bytes = packed;
            if (full <= stored) full = stored + 1 + rng() % 5; // keep it "compressed" (a declared length that decompression will refuse)
            else {
                // normal: full is the real length
            }
        } else {
            stored = n + rng() % 3;
            bytes = data;
            bytes.resize(stored);
            full = rng() % 4 == 0 ? stored : n; // full <= stored
            if (full > stored) full = stored;
        }
        {
            cRZFastCompression3 sizer{};
            sizer.mVTable = GameAddress(cRZFastCompression3::kVTable);
            std::vector<uint8_t> tmp(bytes);
            tmp.resize(tmp.size() + 64);
            if (compressedRecord && sizer.GetLengthOfDecompressedData(tmp.data()) > 40000u) continue;
        }
        uint32_t fileBase = img[0xf4] | (img[0xf5] << 8) | (img[0xf6] << 16) | (static_cast<uint32_t>(img[0xf7]) << 24);
        gFileImage.assign(fileBase + offset + bytes.size() + 16, 0);
        for (auto &b : gFileImage) b = static_cast<uint8_t>(rng());
        std::copy(bytes.begin(), bytes.end(), gFileImage.begin() + fileBase + offset);
        // (the compressed path reads through the fake slot 56, which takes the offset as it is)
        if (compressedRecord) {
            gFileImage.assign(offset + bytes.size() + 16, 0);
            std::copy(bytes.begin(), bytes.end(), gFileImage.begin() + offset);
            if (fileBase != 0) SetWord(img, 0xf4, 0);
        }
        SetWord(img, kHit, (compressedRecord && rng() % 3 == 0) ? offset + 7 : offset); // (the first lookup may point elsewhere: only the second one is used for the data)
        SetWord(img, kHit + 4, stored);
        SetWord(img, kHit + 8, (rng() % 4 == 0 ? 0x80000000u : 0u) | full);
        // The second lookup (compressed records only): the same record, or another shape of it.
        SetWord(img, kHit + 0x10, offset);
        SetWord(img, kHit + 0x14, stored);
        SetWord(img, kHit + 0x18, full);
        gFindRets.clear();
        if (rng() % 8 == 0) {
            gFindRets = {nullptr};
        } else if (rng() % 6 == 0) {
            gFindRets = {gArena + kHit, nullptr};
        } else if (rng() % 6 == 0) {
            SetWord(img, kHit + 0x18, stored); // the second lookup says "not compressed"
            gFindRets = {gArena + kHit, gArena + kHit + 0x10};
        } else {
            gFindRets = {gArena + kHit, gArena + kHit + 0x10};
        }
        gFindIdx = 0;
        gRecFlagAnswers.clear();
        for (int k = 0; k < 4; ++k) gRecFlagAnswers.push_back(rng() % 3 == 0 ? 2 : static_cast<uint8_t>(rng() % 2));
        std::array<uint32_t, 4> key = (!pb.keys.empty() && rng() % 2) ? pb.keys[rng() % pb.keys.size()] : SmallKey(rng);
        std::memcpy(img.data() + kRecIn, key.data(), 16);
        gSeekLands = rng() % 6 != 0;
        gRead14Answer = rng() % 6 != 0;
        gOpensFile = false;
        std::memcpy(gArena, img.data(), kBytes);
        LocalPending pending;
        WorldPending *tree = new (gArena + 0x110) WorldPending();
        for (size_t k = 0; k < pb.keys.size(); ++k) tree->emplace(pb.keys[k], addresses[k]);
        // The caller's buffer: none, or a block in the core of an interesting capacity.
        uint32_t destination = 0, capacity = static_cast<uint32_t>(rng());
        if (rng() % 6 != 0) {
            static const int deltas[] = {0, 0, 0, 1, -1, 5, 700};
            int64_t cap = static_cast<int64_t>(n) + deltas[rng() % 7];
            capacity = static_cast<uint32_t>(cap < 0 ? 0 : cap);
            destination = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(RZMemory::Malloc(capacity ? capacity : 1)));
        }
        std::memcpy(gArena + kBufPtrAt, &destination, 4);
        std::memcpy(gArena + kSizeAt, &capacity, 4);
        Snapshot snap;
        snap.Take();
        auto run = [&](const Tables &t, auto fn) {
            return WorldRun(snap, t, [&](uint8_t *self) -> uint64_t {
                gFindIdx = 0;
                gLengthIdx = 0;
                void *dest = nullptr;
                std::memcpy(&dest, self + kBufPtrAt, 4);
                return fn(self, reinterpret_cast<const uint32_t *>(self + kRecIn), dest, reinterpret_cast<uint32_t *>(self + kSizeAt));
            });
        };
        std::memset(gKindHits, 0, sizeof(gKindHits));
        auto a = run(plain, [&](uint8_t *self, const uint32_t *k, void *d, uint32_t *s) -> uint64_t { return orig(self + 4, k, d, s); });
        int aHits[256];
        std::memcpy(aHits, gKindHits, sizeof(aHits));
        auto b = run(plain, [&](uint8_t *self, const uint32_t *k, void *d, uint32_t *s) -> uint64_t { return reinterpret_cast<SegmentView *>(self + 4)->DoReadRecord(k, d, s); });
        auto c = run(routed, [&](uint8_t *self, const uint32_t *k, void *d, uint32_t *s) -> uint64_t { return VirtualSlot<Fn>(self + 4, 42)(self + 4, k, d, s); });
        WorldCompare(ctx, "DoReadRecord", a, b, c);
        bool open = (img[0x24] & 3) != 0 && (img[0x24] & 1) != 0;
        unreadable += !open;
        pendingBusy += aHits[kRecFlags5] != 0;
        missing += open && aHits[kFind11] != 0 && a.result == 0xffffffffull;
        sizeOnly += open && destination == 0 && aHits[kFind11] != 0 && a.result != 0xffffffffull;
        compressedOk += aHits[kRead56] != 0 && a.result != 0;
        compressedFailed += aHits[kRead56] != 0 && a.result == 0;
        againMissing += aHits[kFind11] >= 2 && aHits[kRead56] == 0;
        plainRead += aHits[kReadFile14] != 0;
        seekFailed += aHits[kSeekPos] != 0 && aHits[kReadFile14] == 0;
        readFailed += aHits[kReadFile14] != 0 && a.result == 0;
        shortCount += aHits[kReadFile14] != 0 && capacity < stored;
        (void)tree;
    }
    if (unreadable < 300 || pendingBusy < 100 || missing < 100 || sizeOnly < 100 || compressedOk < 200 || compressedFailed < 100 || againMissing < 50 || plainRead < 200 || seekFailed < 50 ||
        readFailed < 30 || shortCount < 50)
        ctx.Fail("DoReadRecord: setup reaches too little (%d unreadable, %d pending busy, %d missing, %d size only, %d compressed ok, %d compressed failed, %d second lookup refused, %d plain reads, %d seek failures, "
                 "%d read failures, %d short counts)",
                 unreadable, pendingBusy, missing, sizeOnly, compressedOk, compressedFailed, againMissing, plainRead, seekFailed, readFailed, shortCount);
}

DIFF_TEST(cGZDBSegmentPackedFile_WriteRecord, 0x9aa940)
{
    ImageAllocScope image(ctx);
    WorldScope world(ctx);
    InitRecordFakes();
    SlotPatch p13(ctx, cGZDBPFIndexModifiable::kVTable, 13, reinterpret_cast<void *>(&RecStore13));
    OurStoreSlotPatch o13(13, reinterpret_cast<void *>(&RecStore13));
    SlotPatch p14(ctx, cGZDBPFIndexModifiable::kVTable, 14, reinterpret_cast<void *>(&RecStore14));
    OurStoreSlotPatch o14(14, reinterpret_cast<void *>(&RecStore14));
    gStoreTab[1] = reinterpret_cast<void *>(&RecStoreCurrent);
    gStoreTab[13] = reinterpret_cast<void *>(&RecStore13);
    gStoreTab[14] = reinterpret_cast<void *>(&RecStore14);
    using Fn = uint8_t(__thiscall *)(void *, const uint32_t *, const void *, uint32_t);
    using Member = bool (SegmentView::*)(const uint32_t *, const void *, uint32_t);
    auto orig = ctx.Original<Fn>(0x9aa940);
    Tables plain = MakeTables(ctx);
    plain.primary[44] = reinterpret_cast<void *>(&RecAlloc44);
    plain.primary[58] = reinterpret_cast<void *>(&WriteHashed58);
    plain.sub[5] = reinterpret_cast<void *>(&RecOpen5);
    Tables routed = plain;
    routed.sub[43] = Addr(static_cast<Member>(&SegmentView::DoWriteRecord));
    std::mt19937 rng(0x9aa940);
    int stored = 0, compressedStores = 0, plainStores = 0, writeFailed = 0, allocFailed = 0, filteredOut = 0, pendingKey = 0, notWritable = 0, removedOld = 0, tooSmall = 0;
    for (int i = 0; i < 3000; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        Wire(img);
        std::vector<void *> addresses;
        PendingBuild pb = MakeRecords(img, rng, rng() % 4, addresses);
        static const uint8_t states[] = {0, 1, 2, 3, 3, 3, 3, 0x83};
        img[0x24] = states[rng() % 8];
        img[0x28] = 0;
        img[0x29] = 0;
        img[0x1aa] = rng() % 3 ? 1 : 0; // "accept everything"; the sets stay empty
        SetWord(img, 0x1ac, 0);
        SetWord(img, 0x1b0, 0);
        gOpensFile = false;
        // The record data in the arena at 0x390: sizes around 50 (the compression limit) and up to what fits.
        uint32_t size = rng() % 5 == 0 ? static_cast<uint32_t>(rng() % 100) : static_cast<uint32_t>(rng() % 1100);
        auto data = MakeData(rng, size, static_cast<int>(rng() % 4));
        std::copy(data.begin(), data.end(), img.begin() + 0x390);
        std::memcpy(gArena, img.data(), kBytes);
        WorldPending *tree = new (gArena + 0x110) WorldPending();
        for (size_t k = 0; k < pb.keys.size(); ++k) tree->emplace(pb.keys[k], addresses[k]);
        BuildRanges(rng, 0x184, 6);
        BuildRanges(rng, 0x18c, 4);
        BuildRanges(rng, 0x194, 4);
        gArena[0x19f] = rng() % 2;
        gArena[0x19d] = 0;
        gArena[0x34] = 0;
        gRemoveOk = rng() % 2;
        for (auto &w : gRemoved) w = rng() % 3 == 0 ? PickWord(rng) : static_cast<uint32_t>(rng() % 40);
        gAllocOk = rng() % 5 != 0;
        gAllocOffset = rng() % 8 == 0 ? 0 : static_cast<uint32_t>(rng() % 100);
        gWriteOk = rng() % 3 != 0;
        std::array<uint32_t, 4> key = (!pb.keys.empty() && rng() % 3 == 0) ? pb.keys[rng() % pb.keys.size()] : SmallKey(rng);
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
                uint64_t r = call(self) & 0xff;
                DescribeMap(first, gLog);
                DescribeMap(second, gLog);
                delete first;
                delete second;
                std::memset(self + 0x1a0, 0, 8);
                return r;
            });
        };
        std::memset(gKindHits, 0, sizeof(gKindHits));
        auto a = run(plain, [&](uint8_t *self) -> uint64_t { return orig(self + 4, reinterpret_cast<const uint32_t *>(self + kRecIn), self + 0x390, size); });
        int aHits[256];
        std::memcpy(aHits, gKindHits, sizeof(aHits));
        auto b = run(plain, [&](uint8_t *self) -> uint64_t { return reinterpret_cast<SegmentView *>(self + 4)->DoWriteRecord(reinterpret_cast<const uint32_t *>(self + kRecIn), self + 0x390, size); });
        auto c = run(routed, [&](uint8_t *self) -> uint64_t { return VirtualSlot<Fn>(self + 4, 43)(self + 4, reinterpret_cast<const uint32_t *>(self + kRecIn), self + 0x390, size); });
        WorldCompare(ctx, "DoWriteRecord", a, b, c);
        bool writable = (img[0x24] & 3) != 0 && (img[0x24] & 2) != 0;
        bool inTree = tree->count(key) != 0;
        notWritable += !writable;
        pendingKey += writable && inTree;
        removedOld += aHits[kStore14] != 0 && gRemoveOk;
        allocFailed += aHits[kAlloc44] != 0 && !gAllocOk;
        writeFailed += aHits[kWrite58h] != 0 && !gWriteOk;
        stored += aHits[kStore13] != 0;
        if (aHits[kStore13] != 0) {
            // The stored length (word 1 of the record) tells compressed from plain.
            bool wasCompressed = false;
            for (size_t k = 0; k + 8 < a.log.size(); ++k)
                if (a.log[k] == kStore13) wasCompressed = static_cast<uint32_t>(a.log[k + 3 + 4 + 1]) != size;
            compressedStores += wasCompressed;
            plainStores += !wasCompressed;
        }
        filteredOut += aHits[kAlloc44] != 0 && (size < 50 || img[0x1aa] == 0);
        tooSmall += aHits[kAlloc44] != 0 && size < 50;
    }
    if (stored < 400 || compressedStores < 150 || plainStores < 100 || writeFailed < 100 || allocFailed < 100 || filteredOut < 200 || pendingKey < 50 || notWritable < 400 || removedOld < 200 || tooSmall < 50)
        ctx.Fail("DoWriteRecord: setup reaches too little (%d stored, %d compressed, %d plain, %d write failures, %d allocation failures, %d not compressed by filter or size, %d pending, %d not writable, "
                 "%d removed, %d small)",
                 stored, compressedStores, plainStores, writeFailed, allocFailed, filteredOut, pendingKey, notWritable, removedOld, tooSmall);
}

// ---- Record objects: the pending tree's insert, DoCreateNewRecord (slot 38), DoOpenRecord (slot 37), OpenRecordForWrite (slot 53) ----
//
// The constructors of the record objects (0x9a70f0, 0x9a6e60, 0x9a6de0 and the base 0x99b7c0) stay original code; the
// tests redirect them, on both sides, to fakes that log their arguments and make an object with fake vtables.

namespace {

constexpr int64_t kObjAddRef = 180, kObjSubAddRef = 181, kCtorWrite = 182, kCtorMemory = 183, kCtorBig = 184, kBaseCtor = 185, kSlot53Fake = 186, kSlot38Fake = 187;
void *gObjPrimTab[64];
void *gObjSubTab[64];

int64_t CoreOff(const void *p)
{
    auto q = static_cast<const uint8_t *>(p);
    return (q >= CoreBytes() && q < CoreBytes() + CoreSize()) ? q - CoreBytes() : -1;
}

int64_t NormValue(uint32_t v)
{
    return CoreOff(reinterpret_cast<const void *>(static_cast<uintptr_t>(v))) >= 0 ? 0x100000000ll + CoreOff(reinterpret_cast<const void *>(static_cast<uintptr_t>(v))) : v;
}

uint32_t __fastcall ObjAddRef(void *self, void *)
{
    Push(kObjAddRef, CoreOff(self), 0);
    return 1;
}

uint32_t __fastcall ObjSubAddRef(void *self, void *)
{
    Push(kObjSubAddRef, CoreOff(static_cast<uint8_t *>(self) - 4), 0);
    return 1;
}

template <int64_t Kind> void *__fastcall FakeRecordCtor(void *self, void *, uint32_t a1, uint32_t a2, const uint32_t *key, void *primary, uint32_t a5)
{
    Push(Kind, CoreOff(self), NormValue(a1));
    gLog.push_back(NormValue(a2));
    for (int i = 0; i < 4; ++i) gLog.push_back(key[i]);
    gLog.push_back(Off(primary));
    gLog.push_back(a5);
    auto *words = static_cast<void **>(self);
    words[0] = gObjPrimTab;
    words[1] = gObjSubTab;
    return self;
}

void *__fastcall FakeBaseCtor(void *self, void *, const uint32_t *key, void *sub, uint32_t flag)
{
    Push(kBaseCtor, CoreOff(self), Off(sub));
    for (int i = 0; i < 4; ++i) gLog.push_back(key[i]);
    gLog.push_back(flag);
    auto *words = static_cast<void **>(self);
    words[0] = gObjPrimTab;
    words[1] = gObjSubTab;
    std::memcpy(static_cast<uint8_t *>(self) + 0x0c, key, 16);
    return self;
}

struct CtorFakes {
    Redirect write, writeOurs;
    std::optional<Redirect> memory, big; // the read records' constructors are ours now (cGZDBReadRecord.cpp): their tests run the real ones on both sides
    std::optional<Redirect> base; // the base constructor 0x99b7c0 is ours now (OpenRecordForWrite calls it directly): its test runs the real one on both sides
    explicit CtorFakes(difftest::Context &ctx, bool fakeBase = true, bool fakeReads = true)
        : write(ctx, 0x9a70f0, reinterpret_cast<void *>(&FakeRecordCtor<kCtorWrite>)),
          writeOurs(Addr(&cGZDBWriteRecordPackedFile::Construct), reinterpret_cast<void *>(&FakeRecordCtor<kCtorWrite>))
    {
        if (fakeReads) {
            memory.emplace(ctx, 0x9a6e60, reinterpret_cast<void *>(&FakeRecordCtor<kCtorMemory>));
            big.emplace(ctx, 0x9a6de0, reinterpret_cast<void *>(&FakeRecordCtor<kCtorBig>));
        }
        if (fakeBase) base.emplace(ctx, 0x99b7c0, reinterpret_cast<void *>(&FakeBaseCtor));
        gObjPrimTab[1] = reinterpret_cast<void *>(&ObjAddRef);
        gObjSubTab[1] = reinterpret_cast<void *>(&ObjSubAddRef);
    }
};

// Pending record objects in the arena: 0x80 bytes each from 0x400. +0 and +4 the (fake) vtables used elsewhere, +8 the
// key, +0x44 the kind (1 file-backed, 2 memory-backed, 3 anything else), +0x4c / +0x54 the data and its size.
constexpr size_t kWideBase = 0x400, kWideStride = 0x80;

void *MakeWideRecord(Image &s, size_t index, const std::array<uint32_t, 4> &key, uint32_t kind, uint32_t data, uint32_t size)
{
    size_t at = kWideBase + index * kWideStride;
    SetPointer(s, at, gRecPrim);
    SetPointer(s, at + 4, gRecSub);
    std::memcpy(s.data() + at + 8, key.data(), 16);
    SetWord(s, at + 0x44, kind);
    SetWord(s, at + 0x4c, data);
    SetWord(s, at + 0x54, size);
    return gArena + at;
}

bool __fastcall Slot53Fake(void *self, void *, const uint32_t *key, void **out)
{
    Push(kSlot53Fake, Off(self), 0);
    for (int i = 0; i < 4; ++i) gLog.push_back(key[i]);
    *out = reinterpret_cast<void *>(static_cast<uintptr_t>(0x5353));
    return true;
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_PendingTreeInsert, 0x9a6980)
{
    WorldScope world(ctx);
    using Fn = void **(__thiscall *)(void *, void **, const uint8_t *);
    auto orig = ctx.Original<Fn>(0x9a6980);
    Tables plain = MakeTables(ctx);
    std::mt19937 rng(0x9a6980);
    int duplicates = 0, inserted = 0;
    for (int i = 0; i < 1500; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        std::memcpy(gArena, img.data(), kBytes);
        // A tree of 0..12 records, then one more with a key that may or may not be in it already.
        using Multi = std::multimap<std::array<uint32_t, 4>, void *, TestOrder, TestAllocator<std::pair<const std::array<uint32_t, 4>, void *>>>;
        auto *tree = new (gArena + 0x110) Multi();
        std::vector<std::array<uint32_t, 4>> keys;
        for (unsigned n = rng() % 13; n > 0; --n) {
            auto k = SmallKey(rng);
            keys.push_back(k);
            tree->emplace(k, reinterpret_cast<void *>(static_cast<uintptr_t>(rng())));
        }
        std::array<uint32_t, 4> key = (!keys.empty() && rng() % 3 == 0) ? keys[rng() % keys.size()] : SmallKey(rng);
        duplicates += std::find(keys.begin(), keys.end(), key) != keys.end();
        uint8_t pair[0x14];
        std::memcpy(pair, key.data(), 16);
        uint32_t value = static_cast<uint32_t>(rng());
        std::memcpy(pair + 0x10, &value, 4);
        Snapshot snap;
        snap.Take();
        auto a = WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
            void *node = nullptr;
            orig(self + 0x110, &node, pair);
            return CoreOff(node);
        });
        auto b = WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
            void *node = nullptr;
            reinterpret_cast<KeyTree *>(self + 0x110)->Insert(&node, pair);
            return CoreOff(node);
        });
        WorldCompare(ctx, "KeyTree::Insert", a, b, b);
        inserted += a.result != 0;
    }
    if (duplicates < 300 || inserted < 1400) ctx.Fail("KeyTree::Insert: setup reaches too little (%d duplicate keys, %d inserted)", duplicates, inserted);
}

DIFF_TEST(cGZDBSegmentPackedFile_CreateRecord, 0x9a9640)
{
    WorldScope world(ctx);
    CtorFakes ctors(ctx);
    InitRecordFakes();
    using Fn = bool(__thiscall *)(void *, const uint32_t *, void **);
    using Member = bool (SegmentView::*)(const uint32_t *, void **);
    auto orig = ctx.Original<Fn>(0x9a9640);
    Tables plain = MakeTables(ctx);
    plain.sub[5] = reinterpret_cast<void *>(&RecOpen5);
    Tables routed = plain;
    routed.sub[38] = Addr(static_cast<Member>(&SegmentView::DoCreateNewRecord));
    std::mt19937 rng(0x9a9640);
    int created = 0, notWritable = 0, existsInStore = 0, pending = 0;
    for (int i = 0; i < 2500; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        Wire(img);
        std::vector<void *> addresses;
        PendingBuild pb = MakeRecords(img, rng, rng() % 5, addresses);
        static const uint8_t states[] = {0, 1, 2, 3, 3, 3, 3, 0x83};
        img[0x24] = states[rng() % 8];
        img[0x28] = 0;
        img[0x29] = 0;
        gOpensFile = false;
        gFindRet = rng() % 4 == 0 ? static_cast<void *>(gArena + kHit) : nullptr;
        gFindRets.clear();
        std::array<uint32_t, 4> key = (!pb.keys.empty() && rng() % 3 == 0) ? pb.keys[rng() % pb.keys.size()] : SmallKey(rng);
        std::memcpy(img.data() + kRecIn, key.data(), 16);
        std::memcpy(gArena, img.data(), kBytes);
        WorldPending *tree = new (gArena + 0x110) WorldPending();
        for (size_t k = 0; k < pb.keys.size(); ++k) tree->emplace(pb.keys[k], addresses[k]);
        uint32_t outValue = static_cast<uint32_t>(rng());
        std::memcpy(gArena + kBufPtrAt, &outValue, 4);
        Snapshot snap;
        snap.Take();
        auto run = [&](const Tables &t, auto fn) {
            return WorldRun(snap, t, [&](uint8_t *self) -> uint64_t {
                gFindIdx = 0;
                return fn(self, reinterpret_cast<const uint32_t *>(self + kRecIn), reinterpret_cast<void **>(self + kBufPtrAt)) & 0xff;
            });
        };
        auto a = run(plain, [&](uint8_t *self, const uint32_t *k, void **o) -> uint64_t { return orig(self + 4, k, o); });
        auto b = run(plain, [&](uint8_t *self, const uint32_t *k, void **o) -> uint64_t { return reinterpret_cast<SegmentView *>(self + 4)->DoCreateNewRecord(k, o); });
        auto c = run(routed, [&](uint8_t *self, const uint32_t *k, void **o) -> uint64_t { return VirtualSlot<Fn>(self + 4, 38)(self + 4, k, o); });
        WorldCompare(ctx, "DoCreateNewRecord", a, b, c);
        bool writable = (img[0x24] & 3) != 0 && (img[0x24] & 2) != 0;
        created += (a.result & 0xff) != 0;
        notWritable += !writable;
        existsInStore += writable && gFindRet != nullptr;
        pending += writable && gFindRet == nullptr && tree->count(key) != 0;
    }
    if (created < 500 || notWritable < 400 || existsInStore < 200 || pending < 80) ctx.Fail("DoCreateNewRecord: setup reaches too little (%d created, %d not writable, %d in the store, %d pending)", created, notWritable, existsInStore, pending);
}

namespace {

enum class StoreShape { None, Compressed, SmallPlain, BigPlain };

// Sets up the store's answer for `key` (record at kHit) and the file bytes behind it. Returns whether the record is compressed.
struct RecordSetup {
    StoreShape shape = StoreShape::None;
    uint32_t offset = 0, stored = 0, full = 0;
};

RecordSetup SetupStoreRecord(Image &img, std::mt19937 &rng, uint32_t smallLimit)
{
    RecordSetup r;
    switch (rng() % 5) {
    case 0: r.shape = StoreShape::None; break;
    case 1:
    case 2: r.shape = StoreShape::Compressed; break;
    case 3: r.shape = StoreShape::SmallPlain; break;
    default: r.shape = StoreShape::BigPlain; break;
    }
    r.offset = 1 + rng() % 30;
    if (r.shape == StoreShape::None) {
        gFindRet = nullptr;
        return r;
    }
    std::vector<uint8_t> bytes;
    if (r.shape == StoreShape::Compressed) {
        uint32_t n = 20 + rng() % 500;
        auto data = MakeData(rng, n, 1 + static_cast<int>(rng() % 2));
        bytes = CompressWithTheGame(data);
        r.stored = static_cast<uint32_t>(bytes.size());
        r.full = n;
        if (r.full <= r.stored) {
            r.shape = StoreShape::SmallPlain; // did not shrink: a plain record after all
            r.full = r.stored;
        }
    } else if (r.shape == StoreShape::SmallPlain) {
        r.stored = 1 + rng() % smallLimit;
        r.full = rng() % 4 == 0 ? r.stored : r.stored; // (the original allocates `full` bytes and reads `stored`)
        bytes.assign(r.stored, 0);
        for (auto &b : bytes) b = static_cast<uint8_t>(rng());
    } else {
        r.stored = smallLimit + 1 + rng() % 100;
        r.full = r.stored;
        bytes.assign(r.stored, 0);
        for (auto &b : bytes) b = static_cast<uint8_t>(rng());
    }
    gFileImage.assign(r.offset + bytes.size() + 16, 0);
    std::copy(bytes.begin(), bytes.end(), gFileImage.begin() + r.offset);
    SetWord(img, kHit, r.offset);
    SetWord(img, kHit + 4, r.stored);
    SetWord(img, kHit + 8, r.full);
    gFindRet = gArena + kHit;
    return r;
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_OpenRecord, 0x9a9a60)
{
    ImageAllocScope image(ctx);
    WorldScope world(ctx);
    CtorFakes ctors(ctx, false, false); // the real read-record constructors and base on both sides
    InitRecordFakes();
    gRecSub[5] = reinterpret_cast<void *>(&RecSub5);
    using Fn = bool(__thiscall *)(void *, const uint32_t *, void **, uint32_t);
    using Member = bool (SegmentView::*)(const uint32_t *, void **, uint32_t);
    auto orig = ctx.Original<Fn>(0x9a9a60);
    Tables plain = MakeTables(ctx);
    plain.sub[5] = reinterpret_cast<void *>(&RecOpen5);
    plain.primary[53] = reinterpret_cast<void *>(&Slot53Fake);
    plain.primary[56] = reinterpret_cast<void *>(&ReadAtFake);
    Tables routed = plain;
    routed.sub[37] = Addr(static_cast<Member>(&SegmentView::DoOpenRecord));
    std::mt19937 rng(0x9a9a60);
    int writeMode = 0, closed = 0, readOpened = 0, pendingBusy = 0, pendingMemory = 0, pendingBig = 0, pendingOther = 0, compressed = 0, smallOnes = 0, big = 0, noRecord = 0, readFailed = 0,
        noReadBit = 0;
    for (int i = 0; i < 4000; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        Wire(img);
        static const uint8_t states[] = {0, 1, 2, 3, 3, 3, 3, 3, 0x83};
        img[0x24] = states[rng() % 9];
        img[0x28] = 0;
        img[0x29] = 0;
        gOpensFile = false;
        uint32_t smallLimit = rng() % 2 ? 0x200 : 0x40 + rng() % 100;
        SetWord(img, 0x10c, smallLimit);
        // Pending records (wide ones in the arena).
        std::vector<std::array<uint32_t, 4>> keys;
        std::vector<void *> addresses;
        std::vector<uint32_t> keyKinds;
        for (size_t k = 0, n = rng() % 4; k < n; ++k) {
            auto key = SmallKey(rng);
            if (std::find(keys.begin(), keys.end(), key) != keys.end()) continue;
            static const uint32_t kinds[] = {1, 2, 2, 1, 3, 0};
            uint32_t data = rng() % 2 ? static_cast<uint32_t>(rng() % 100000) : 0;
            uint32_t kind = kinds[rng() % 6];
            keyKinds.push_back(kind);
            // A memory-backed record's data is a counted block: the real constructor adds a reference to it.
            if (kind == 2) data = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gArena + 0x600 + keys.size() * 8));
            addresses.push_back(MakeWideRecord(img, keys.size(), key, kind, data, static_cast<uint32_t>(rng() % 400)));
            keys.push_back(key);
        }
        auto key = (!keys.empty() && rng() % 2) ? keys[rng() % keys.size()] : SmallKey(rng);
        std::memcpy(img.data() + kRecIn, key.data(), 16);
        RecordSetup record = SetupStoreRecord(img, rng, smallLimit);
        gFindRets.clear();
        gFindIdx = 0;
        gReadOk = rng() % 8 != 0;
        gRecFlagAnswers.clear();
        for (int k = 0; k < 4; ++k) gRecFlagAnswers.push_back(rng() % 4 == 0 ? 2 : static_cast<uint8_t>(rng() % 2));
        static const uint32_t modes[] = {0, 1, 1, 1, 1, 2, 3, 0x10, 0x11, 0x12, 0xff};
        uint32_t mode = modes[rng() % 11];
        std::memcpy(gArena, img.data(), kBytes);
        WorldPending *tree = new (gArena + 0x110) WorldPending();
        for (size_t k = 0; k < keys.size(); ++k) tree->emplace(keys[k], addresses[k]);
        uint32_t outValue = static_cast<uint32_t>(rng());
        std::memcpy(gArena + kBufPtrAt, &outValue, 4);
        Snapshot snap;
        snap.Take();
        auto run = [&](const Tables &t, auto fn) {
            return WorldRun(snap, t, [&](uint8_t *self) -> uint64_t {
                gFindIdx = 0;
                return fn(self, reinterpret_cast<const uint32_t *>(self + kRecIn), reinterpret_cast<void **>(self + kBufPtrAt)) & 0xff;
            });
        };
        std::memset(gKindHits, 0, sizeof(gKindHits));
        auto a = run(plain, [&](uint8_t *self, const uint32_t *k, void **o) -> uint64_t { return orig(self + 4, k, o, mode); });
        int aHits[256];
        std::memcpy(aHits, gKindHits, sizeof(aHits));
        auto b = run(plain, [&](uint8_t *self, const uint32_t *k, void **o) -> uint64_t { return reinterpret_cast<SegmentView *>(self + 4)->DoOpenRecord(k, o, mode); });
        auto c = run(routed, [&](uint8_t *self, const uint32_t *k, void **o) -> uint64_t { return VirtualSlot<Fn>(self + 4, 37)(self + 4, k, o, mode); });
        WorldCompare(ctx, "DoOpenRecord", a, b, c);
        bool open = (img[0x24] & 3) != 0;
        closed += !open;
        writeMode += open && (mode & 2) != 0;
        noReadBit += open && (mode & 3) == 0;
        bool reading = open && (mode & 2) == 0 && (mode & 1) != 0;
        bool inTree = std::find(keys.begin(), keys.end(), key) != keys.end();
        readOpened += reading && (a.result & 0xff) != 0;
        pendingBusy += reading && inTree && aHits[kRecFlags5] != 0;
        uint32_t pendingKind = 0;
        if (inTree) pendingKind = keyKinds[std::find(keys.begin(), keys.end(), key) - keys.begin()];
        bool made = (a.result & 0xff) != 0;
        pendingMemory += reading && inTree && pendingKind == 2 && made;
        pendingBig += reading && inTree && pendingKind == 1 && made;
        pendingOther += reading && inTree && aHits[kRecFlags5] != 0 && (a.result & 0xff) == 0;
        if (reading && !inTree) {
            compressed += record.shape == StoreShape::Compressed && made;
            smallOnes += record.shape == StoreShape::SmallPlain && made;
            big += record.shape == StoreShape::BigPlain && made;
            noRecord += record.shape == StoreShape::None;
            readFailed += (record.shape == StoreShape::Compressed || record.shape == StoreShape::SmallPlain) && !made;
        }
    }
    if (closed < 200 || writeMode < 400 || readOpened < 500 || pendingBusy < 100 || pendingMemory < 100 || pendingBig < 100 || pendingOther < 100 || compressed < 150 || smallOnes < 100 || big < 100 ||
        noRecord < 100 || readFailed < 50 || noReadBit < 50)
        ctx.Fail("DoOpenRecord: setup reaches too little (%d closed, %d write mode, %d opened, %d busy, %d pending memory, %d pending big, %d pending refused, %d compressed, %d small, %d big, %d none, %d failed reads, %d no read bit)",
                 closed, writeMode, readOpened, pendingBusy, pendingMemory, pendingBig, pendingOther, compressed, smallOnes, big, noRecord, readFailed, noReadBit);
}

namespace {

constexpr int64_t kVectorLog = 188;

// After vf53 made a memory-backed record (a std::vector<uint8_t> at +0x4c on the heap): logs what is in it and frees it,
// so that the heap addresses do not reach the comparison.
void RetireMemoryRecord(uint8_t *self, void *handle)
{
    if (handle == nullptr) return;
    uint8_t *object = static_cast<uint8_t *>(handle) - 4;
    uint32_t kind;
    std::memcpy(&kind, object + 0x44, sizeof(kind));
    if (kind != 3) return;
    auto *bytes = reinterpret_cast<std::vector<uint8_t> *>(object + 0x4c);
    gLog.push_back(kVectorLog);
    gLog.push_back(static_cast<int64_t>(bytes->size()));
    gLog.push_back(static_cast<int64_t>(bytes->capacity()));
    uint32_t hash = 17;
    for (uint8_t b : *bytes) hash = hash * 31 + b;
    gLog.push_back(hash);
    bytes->~vector();
    std::memset(object + 0x4c, 0, 12);
    (void)self;
}

bool gSlot38Answer = true;

bool __fastcall Slot38Fake(void *self, void *, const uint32_t *key, void **out)
{
    Push(kSlot38Fake, Off(self), 0);
    for (int i = 0; i < 4; ++i) gLog.push_back(key[i]);
    *out = reinterpret_cast<void *>(static_cast<uintptr_t>(0x3838));
    return gSlot38Answer;
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_OpenRecordForWrite, 0x9a9d80)
{
    ImageAllocScope image(ctx);
    WorldScope world(ctx);
    CtorFakes ctors(ctx, false);
    InitRecordFakes();
    using Fn = bool(__thiscall *)(void *, const uint32_t *, void **);
    using Member = bool (cGZDBSegmentPackedFile::*)(const uint32_t *, void **);
    auto orig = ctx.Original<Fn>(0x9a9d80);
    Tables plain = MakeTables(ctx);
    plain.sub[5] = reinterpret_cast<void *>(&RecOpen5);
    plain.sub[38] = reinterpret_cast<void *>(&Slot38Fake);
    plain.primary[56] = reinterpret_cast<void *>(&ReadAtFake);
    Tables routed = plain;
    routed.primary[53] = Addr(static_cast<Member>(&cGZDBSegmentPackedFile::OpenRecordForWrite));
    std::mt19937 rng(0x9a9d80);
    int notWritable = 0, pendingKey = 0, created = 0, compressed = 0, compressedFailed = 0, plainRecord = 0;
    for (int i = 0; i < 3000; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        Wire(img);
        static const uint8_t states[] = {0, 1, 2, 3, 3, 3, 3, 0x83};
        img[0x24] = states[rng() % 8];
        img[0x28] = 0;
        img[0x29] = 0;
        gOpensFile = false;
        std::vector<void *> addresses;
        PendingBuild pb = MakeRecords(img, rng, rng() % 4, addresses);
        std::array<uint32_t, 4> key = (!pb.keys.empty() && rng() % 3 == 0) ? pb.keys[rng() % pb.keys.size()] : SmallKey(rng);
        std::memcpy(img.data() + kRecIn, key.data(), 16);
        RecordSetup record = SetupStoreRecord(img, rng, 0x200);
        gSlot38Answer = rng() % 2;
        gReadOk = rng() % 8 != 0;
        gFindRets.clear();
        std::memcpy(gArena, img.data(), kBytes);
        WorldPending *tree = new (gArena + 0x110) WorldPending();
        for (size_t k = 0; k < pb.keys.size(); ++k) tree->emplace(pb.keys[k], addresses[k]);
        uint32_t outValue = static_cast<uint32_t>(rng());
        std::memcpy(gArena + kBufPtrAt, &outValue, 4);
        Snapshot snap;
        snap.Take();
        auto run = [&](const Tables &t, auto fn) {
            return WorldRun(snap, t, [&](uint8_t *self) -> uint64_t {
                gFindIdx = 0;
                uint64_t r = fn(self, reinterpret_cast<const uint32_t *>(self + kRecIn), reinterpret_cast<void **>(self + kBufPtrAt)) & 0xff;
                void *handle;
                std::memcpy(&handle, self + kBufPtrAt, 4);
                if (CoreOff(handle) >= 0) RetireMemoryRecord(self, handle);
                return r;
            });
        };
        std::memset(gKindHits, 0, sizeof(gKindHits));
        auto a = run(plain, [&](uint8_t *self, const uint32_t *k, void **o) -> uint64_t { return orig(self, k, o); });
        int aHits[256];
        std::memcpy(aHits, gKindHits, sizeof(aHits));
        auto b = run(plain, [&](uint8_t *self, const uint32_t *k, void **o) -> uint64_t { return Obj(self)->OpenRecordForWrite(k, o); });
        auto c = run(routed, [&](uint8_t *self, const uint32_t *k, void **o) -> uint64_t { return VirtualSlot<Fn>(self, 53)(self, k, o); });
        WorldCompare(ctx, "OpenRecordForWrite", a, b, c);
        bool writable = (img[0x24] & 3) != 0 && (img[0x24] & 2) != 0;
        bool inTree = pb.keys.end() != std::find(pb.keys.begin(), pb.keys.end(), key);
        notWritable += !writable;
        pendingKey += writable && inTree;
        created += aHits[kSlot38Fake] != 0;
        if (writable && !inTree) {
            compressed += record.shape == StoreShape::Compressed && (a.result & 0xff) != 0;
            compressedFailed += record.shape == StoreShape::Compressed && (a.result & 0xff) == 0;
            plainRecord += (record.shape == StoreShape::SmallPlain || record.shape == StoreShape::BigPlain) && aHits[kCtorWrite] != 0;
        }
    }
    if (notWritable < 300 || pendingKey < 50 || created < 150 || compressed < 150 || compressedFailed < 30 || plainRecord < 300)
        ctx.Fail("OpenRecordForWrite: setup reaches too little (%d not writable, %d pending, %d created, %d compressed, %d compressed failed, %d plain)", notWritable, pendingKey, created, compressed,
                 compressedFailed, plainRecord);
}
