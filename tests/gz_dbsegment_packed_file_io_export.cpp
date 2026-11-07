// Differential tests for cGZDBSegmentPackedFile: export and import, the header, index, hole and compression-directory readers.
// Fixtures (the arena, the fake store and file objects, the comparison helpers): gz_dbsegment_io_fixtures.h.
#include "gz_dbsegment_io_fixtures.h"

// ---- ExportRecord (slot 54) and ImportFile (slot 21): the file system side ----
//
// The original works in the harness's sandbox directory (its file imports are sandboxed); both sides use the game's table of
// wide file functions, filled with the sandboxed functions or left empty (ANSI imports), as the cRZFile tests do. Each side
// gets its own directory holding the same starting files; afterwards the directories (names and contents) must match.
namespace {

const uint32_t kWideGlobalsHere[] = {0x10cb5e0, 0x10cb5e4, 0x10cb5e8, 0x10cb5ec, 0x10cb5f0, 0x10cb5f4};
const char *const kWideNamesHere[] = {"CreateFileW", "CopyFileW", "MoveFileExW", "FindFirstFileW", "DeleteFileW", "GetFileAttributesW"};

struct WideMode {
    difftest::Context &ctx;
    void *saved[6];
    explicit WideMode(difftest::Context &c) : ctx(c)
    {
        for (int i = 0; i < 6; ++i) saved[i] = *ctx.image.At<void *>(kWideGlobalsHere[i]);
    }
    void Set(bool wide)
    {
        for (int i = 0; i < 6; ++i) *ctx.image.At<void *>(kWideGlobalsHere[i]) = wide ? difftest::SandboxFunction(kWideNamesHere[i]) : nullptr;
    }
    ~WideMode()
    {
        for (int i = 0; i < 6; ++i) *ctx.image.At<void *>(kWideGlobalsHere[i]) = saved[i];
    }
};

std::string NarrowPath(const std::wstring &w)
{
    std::string s;
    for (wchar_t c : w) s.push_back(static_cast<char>(c));
    return s;
}

// A cRZString on raw memory holding `text`.
struct PathString {
    alignas(8) uint8_t bytes[sizeof(cRZString)];
    explicit PathString(const std::string &text) { string()->Construct(text.c_str()); }
    ~PathString() { string()->Destruct(); }
    cRZString *string() { return reinterpret_cast<cRZString *>(bytes); }
    cIGZString *interfacePointer() { return string()->AsInterface(); }
};

using Listing = std::map<std::wstring, std::vector<uint8_t>>;

void ListInto(const std::wstring &root, const std::wstring &relative, Listing &out)
{
    WIN32_FIND_DATAW data;
    HANDLE h = FindFirstFileW((root + relative + L"*").c_str(), &data);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring name = data.cFileName;
        if (name == L"." || name == L"..") continue;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            out[relative + name + L"\\"] = {};
            ListInto(root, relative + name + L"\\", out);
        } else {
            std::vector<uint8_t> bytes(data.nFileSizeLow);
            HANDLE f = CreateFileW((root + relative + name).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            DWORD got = 0;
            if (f != INVALID_HANDLE_VALUE) {
                if (!bytes.empty()) ReadFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &got, nullptr);
                CloseHandle(f);
            }
            out[relative + name] = bytes;
        }
    } while (FindNextFileW(h, &data));
    FindClose(h);
}

Listing ListDirectory(const std::wstring &root)
{
    Listing listing;
    ListInto(root, L"", listing);
    return listing;
}

void WriteTestFile(const std::wstring &path, const std::vector<uint8_t> &bytes)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD written = 0;
    if (!bytes.empty()) WriteFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(f);
}

constexpr int64_t kRecSize36 = 190, kOpen22 = 191, kQi0 = 192, kRel2Handle = 193, kRel2Stream = 194, kRead27 = 195, kCloseRec24 = 196;
constexpr size_t kHandleObj = 0x760, kStreamObj = 0x780;
void *gHandleTab[8];
void *gStreamTab[32];
uint32_t gSizeAnswer = 0;
bool gOpenRecOk = true, gQiOk = true;
int gReadFailAt = -1, gReadChunk = 0;
std::vector<uint8_t> gRecordBytes;
size_t gStreamPos = 0;

uint32_t __fastcall Size36(void *self, void *, const uint32_t *key)
{
    Push(kRecSize36, Off(self), 0);
    for (int i = 0; i < 4; ++i) gLog.push_back(key[i]);
    return gSizeAnswer;
}

bool __fastcall OpenRecord22(void *self, void *, const uint32_t *key, void **out, uint32_t mode)
{
    Push(kOpen22, Off(self), mode);
    for (int i = 0; i < 4; ++i) gLog.push_back(key[i]);
    if (gOpenRecOk) *out = gArena + kHandleObj;
    return gOpenRecOk;
}

bool __fastcall HandleQuery(void *self, void *, uint32_t iid, void **out)
{
    Push(kQi0, Off(self), iid);
    if (gQiOk) *out = gArena + kStreamObj;
    return gQiOk;
}

uint32_t __fastcall HandleRelease(void *self, void *)
{
    Push(kRel2Handle, Off(self), 0);
    return 0;
}

uint32_t __fastcall StreamRelease(void *self, void *)
{
    Push(kRel2Stream, Off(self), 0);
    return 0;
}

uint8_t __fastcall StreamRead27(void *self, void *, void *buffer, uint32_t count)
{
    Push(kRead27, Off(self), count);
    int chunk = gReadChunk++;
    if (chunk == gReadFailAt) return 0;
    std::memset(buffer, 0, count);
    if (gStreamPos < gRecordBytes.size()) std::memcpy(buffer, gRecordBytes.data() + gStreamPos, (std::min<size_t>)(count, gRecordBytes.size() - gStreamPos));
    gStreamPos += count;
    return 1;
}

void __fastcall CloseRecord24(void *self, void *, void *handle)
{
    Push(kCloseRec24, Off(self), Off(handle));
}

} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_ExportRecordToFile, 0x9aa2f0)
{
    InitRecordFakes();
    WideMode wideMode(ctx);
    difftest::EmptySandbox();
    gHandleTab[0] = reinterpret_cast<void *>(&HandleQuery);
    gHandleTab[2] = reinterpret_cast<void *>(&HandleRelease);
    gStreamTab[2] = reinterpret_cast<void *>(&StreamRelease);
    gStreamTab[27] = reinterpret_cast<void *>(&StreamRead27);
    using Fn = uint8_t(__thiscall *)(void *, const uint32_t *, cIGZString *);
    using Member = uint8_t (cGZDBSegmentPackedFile::*)(const uint32_t *, cIGZString *);
    auto orig = ctx.Original<Fn>(0x9aa2f0);
    Tables plain = MakeTables(ctx);
    plain.sub[5] = reinterpret_cast<void *>(&RecOpen5);
    plain.sub[36] = reinterpret_cast<void *>(&Size36);
    plain.sub[22] = reinterpret_cast<void *>(&OpenRecord22);
    plain.sub[24] = reinterpret_cast<void *>(&CloseRecord24);
    Tables routed = plain;
    routed.primary[54] = Addr(static_cast<Member>(&cGZDBSegmentPackedFile::ExportRecord));
    std::mt19937 rng(0x9aa2f0);
    int exported = 0, noRecord = 0, noOpen = 0, noStream = 0, readFailed = 0, overwritten = 0, bigRecord = 0;
    for (int run = 0; run < 240; ++run) {
        bool wide = true; // (the ANSI branch of cRZFile is covered by the cRZFile tests: ours reaches the game's vtable slots, which refuse handles from the real API)
        wideMode.Set(wide);
        difftest::EmptySandbox();
        static const uint32_t sizes[] = {0, 1, 100, 0x3ff, 0x400, 0x401, 0x800, 3000};
        uint32_t n = rng() % 5 == 0 ? static_cast<uint32_t>(rng() % 2000) : sizes[rng() % 8];
        gRecordBytes.assign(n, 0);
        for (auto &b : gRecordBytes) b = static_cast<uint8_t>(rng());
        gSizeAnswer = rng() % 8 == 0 ? 0xffffffffu : n;
        gOpenRecOk = rng() % 6 != 0;
        gQiOk = rng() % 6 != 0;
        gReadFailAt = rng() % 5 == 0 ? static_cast<int>(rng() % 4) : -1;
        const bool existingDirectory = true; // (a missing directory makes CreateDirectories walk the parents above the sandbox, which the harness refuses for our calls through the game's import table too)
        bool existingFile = rng() % 2;
        std::vector<uint8_t> oldContent(rng() % 50, 0x77);
        const wchar_t sides[3] = {L'A', L'B', L'C'};
        std::wstring dirs[3];
        std::string targets[3];
        for (int s = 0; s < 3; ++s) {
            dirs[s] = difftest::SandboxPath(std::wstring(1, sides[s]) + std::to_wstring(run) + L"\\");
            CreateDirectoryW(dirs[s].c_str(), nullptr);
            std::wstring sub = dirs[s] + L"deep\\";
            if (existingDirectory) {
                CreateDirectoryW(sub.c_str(), nullptr);
                if (existingFile) WriteTestFile(sub + L"rec.bin", oldContent);
            }
            targets[s] = NarrowPath(sub + L"rec.bin");
        }
        Image img = MakeImage(rng);
        Wire(img);
        static const uint8_t states[] = {0, 1, 2, 3, 3, 3};
        img[0x24] = states[rng() % 6];
        img[0x28] = 0;
        img[0x29] = 0;
        gOpensFile = false;
        SetPointer(img, kHandleObj, gHandleTab);
        SetPointer(img, kStreamObj, gStreamTab);
        std::array<uint32_t, 4> key = SmallKey(rng);
        std::memcpy(img.data() + kRecIn, key.data(), 16);
        auto call = [&](int s, const Tables &t, auto fn) {
            PathString path(targets[s]);
            gStreamPos = 0;
            gReadChunk = 0;
            return Run(t, img, [&](uint8_t *self) -> uint64_t { return fn(self, reinterpret_cast<const uint32_t *>(self + kRecIn), path.interfacePointer()) & 0xff; });
        };
        auto a = call(0, plain, [&](uint8_t *self, const uint32_t *k, cIGZString *p) -> uint64_t { return orig(self, k, p); });
        auto b = call(1, plain, [&](uint8_t *self, const uint32_t *k, cIGZString *p) -> uint64_t { return Obj(self)->ExportRecord(k, p); });
        auto c = call(2, routed, [&](uint8_t *self, const uint32_t *k, cIGZString *p) -> uint64_t { return VirtualSlot<Fn>(self, 54)(self, k, p); });
        Compare(ctx, "ExportRecord", a, b, c);
        Listing la = ListDirectory(dirs[0]), lb = ListDirectory(dirs[1]), lc = ListDirectory(dirs[2]);
        ++ctx.cases;
        if (la != lb || la != lc) ctx.Fail("ExportRecord (run %d, %s): the directories differ afterwards (%zu / %zu / %zu entries)", run, wide ? "wide" : "ANSI", la.size(), lb.size(), lc.size());
        bool ok = (a.result & 0xff) != 0;
        exported += ok;
        noRecord += gSizeAnswer == 0xffffffffu;
        noOpen += gSizeAnswer != 0xffffffffu && !gOpenRecOk;
        noStream += gSizeAnswer != 0xffffffffu && gOpenRecOk && !gQiOk;
        readFailed += gSizeAnswer != 0xffffffffu && gOpenRecOk && gQiOk && gReadFailAt >= 0 && static_cast<uint32_t>(gReadFailAt) * 0x400 < n;
        overwritten += ok && existingFile;
        bigRecord += ok && n > 0x400;
    }
    difftest::EmptySandbox();
    if (exported < 60 || noRecord < 10 || noOpen < 10 || noStream < 10 || readFailed < 5 || overwritten < 10 || bigRecord < 15)
        ctx.Fail("ExportRecord: setup reaches too little (%d exported, %d without a record, %d open failures, %d stream failures, %d read failures, %d overwrites, %d big records)",
                 exported, noRecord, noOpen, noStream, readFailed, overwritten, bigRecord);
}

DIFF_TEST(cGZDBSegmentPackedFile_ImportFileAsRecord, 0x9aa580)
{
    ImageAllocScope image(ctx);
    WorldScope world(ctx);
    InitRecordFakes();
    WideMode wideMode(ctx);
    SlotPatch p13(ctx, cGZDBPFIndexModifiable::kVTable, 13, reinterpret_cast<void *>(&RecStore13));
    OurStoreSlotPatch o13(13, reinterpret_cast<void *>(&RecStore13));
    SlotPatch p14(ctx, cGZDBPFIndexModifiable::kVTable, 14, reinterpret_cast<void *>(&RecStore14));
    OurStoreSlotPatch o14(14, reinterpret_cast<void *>(&RecStore14));
    gStoreTab[1] = reinterpret_cast<void *>(&RecStoreCurrent);
    gStoreTab[13] = reinterpret_cast<void *>(&RecStore13);
    gStoreTab[14] = reinterpret_cast<void *>(&RecStore14);
    difftest::EmptySandbox();
    using Fn = uint8_t(__thiscall *)(void *, const uint32_t *, cIGZString *);
    using Member = uint8_t (cGZDBSegmentPackedFile::*)(const uint32_t *, cIGZString *);
    auto orig = ctx.Original<Fn>(0x9aa580);
    Tables plain = MakeTables(ctx);
    plain.primary[44] = reinterpret_cast<void *>(&RecAlloc44);
    plain.primary[58] = reinterpret_cast<void *>(&WriteHashed58);
    plain.sub[5] = reinterpret_cast<void *>(&RecOpen5);
    Tables routed = plain;
    routed.primary[21] = Addr(static_cast<Member>(&cGZDBSegmentPackedFile::ImportFile));
    std::mt19937 rng(0x9aa580);
    int storedCompressed = 0, storedPlain = 0, chunked = 0, missingFile = 0, emptyFile = 0, notWritable = 0, pendingKey = 0, writeFailed = 0, allocFailed = 0, removedOld = 0, fellBack = 0;
    for (int run = 0; run < 1500; ++run) {
        ResetWorld();
        // Memory that was used before: the allocator hands out dirty blocks, so a byte the function should have written shows up.
        std::memset(CoreBytes() + 0x400, 0xAB, 0x10000);
        bool wide = true;
        wideMode.Set(wide);
        if (run % 25 == 0) difftest::EmptySandbox();
        // The file to import: sizes around the compression limit and the 0x4000-byte chunks.
        static const uint32_t sizes[] = {0, 1, 49, 50, 51, 200, 0x3fff, 0x4000, 0x4001, 0x8000, 0x8001, 20000};
        uint32_t n = rng() % 3 == 0 ? static_cast<uint32_t>(rng() % 6000) : sizes[rng() % 12];
        auto data = MakeData(rng, n, static_cast<int>(rng() % 4));
        bool missing = rng() % 12 == 0;
        std::wstring names[3];
        std::string paths[3];
        for (int s = 0; s < 3; ++s) {
            names[s] = difftest::SandboxPath(std::wstring(1, L"ABC"[s]) + std::to_wstring(run) + L".bin");
            if (!missing) WriteTestFile(names[s], data);
            paths[s] = NarrowPath(names[s]);
        }
        Image img = MakeImage(rng);
        Wire(img);
        std::vector<void *> addresses;
        PendingBuild pb = MakeRecords(img, rng, rng() % 4, addresses);
        static const uint8_t states[] = {0, 1, 2, 3, 3, 3, 3, 0x83};
        img[0x24] = states[rng() % 8];
        img[0x28] = 0;
        img[0x29] = 0;
        img[0x1aa] = rng() % 3 ? 1 : 0;
        SetWord(img, 0x1ac, 0);
        SetWord(img, 0x1b0, 0);
        gOpensFile = false;
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
        gAllocOk = rng() % 6 != 0;
        gAllocOffset = rng() % 8 == 0 ? 0 : static_cast<uint32_t>(rng() % 100);
        gWriteOk = rng() % 5 != 0;
        std::array<uint32_t, 4> key = (!pb.keys.empty() && rng() % 3 == 0) ? pb.keys[rng() % pb.keys.size()] : SmallKey(rng);
        std::memcpy(gArena + kRecIn, key.data(), 16);
        uint32_t mapSeed = static_cast<uint32_t>(rng());
        Snapshot snap;
        snap.Take();
        auto runSide = [&](int side, const Tables &t, auto call) {
            PathString path(paths[side]);
            return WorldRun(snap, t, [&](uint8_t *self) -> uint64_t {
                std::mt19937 build(mapSeed);
                RefMap *first = BuildRefMap(build);
                RefMap *second = BuildRefMap(build);
                std::memcpy(self + 0x1a0, &first, 4);
                std::memcpy(self + 0x1a4, &second, 4);
                uint64_t r = call(self, path.interfacePointer()) & 0xff;
                DescribeMap(first, gLog);
                DescribeMap(second, gLog);
                delete first;
                delete second;
                std::memset(self + 0x1a0, 0, 8);
                return r;
            });
        };
        std::memset(gKindHits, 0, sizeof(gKindHits));
        auto a = runSide(0, plain, [&](uint8_t *self, cIGZString *p) -> uint64_t { return orig(self, reinterpret_cast<const uint32_t *>(self + kRecIn), p); });
        int aHits[256];
        std::memcpy(aHits, gKindHits, sizeof(aHits));
        auto b = runSide(1, plain, [&](uint8_t *self, cIGZString *p) -> uint64_t { return Obj(self)->ImportFile(reinterpret_cast<const uint32_t *>(self + kRecIn), p); });
        auto c = runSide(2, routed, [&](uint8_t *self, cIGZString *p) -> uint64_t { return VirtualSlot<Fn>(self, 21)(self, reinterpret_cast<const uint32_t *>(self + kRecIn), p); });
        WorldCompare(ctx, "ImportFile", a, b, c);
        // The files the three sides worked on must be alike afterwards too (a wrong open mode creates the missing file).
        auto fileInfo = [&](int s) {
            WIN32_FILE_ATTRIBUTE_DATA info;
            bool exists = GetFileAttributesExW(names[s].c_str(), GetFileExInfoStandard, &info) != 0;
            return std::make_pair(exists, exists ? info.nFileSizeLow : 0u);
        };
        ++ctx.cases;
        if (fileInfo(0) != fileInfo(1) || fileInfo(0) != fileInfo(2)) ctx.Fail("ImportFile: the imported files differ afterwards (run %d)", run);
        bool writable = (img[0x24] & 3) != 0 && (img[0x24] & 2) != 0;
        bool inTree = tree->count(key) != 0;
        notWritable += !writable;
        pendingKey += writable && inTree;
        missingFile += writable && !inTree && missing;
        emptyFile += writable && !inTree && !missing && n == 0;
        removedOld += aHits[kStore14] != 0 && gRemoveOk;
        allocFailed += aHits[kAlloc44] != 0 && !gAllocOk;
        writeFailed += aHits[kWrite58h] != 0 && !gWriteOk;
        bool anyStore = aHits[kStore13] != 0;
        if (anyStore) {
            bool plainRecord = false;
            for (size_t k = 0; k + 8 < a.log.size(); ++k)
                if (a.log[k] == kStore13) plainRecord = static_cast<uint32_t>(a.log[k + 3 + 4 + 1]) == n;
            storedPlain += plainRecord;
            storedCompressed += !plainRecord;
            chunked += plainRecord && n > 0x4000;
        }
        fellBack += aHits[kAlloc44] >= 2;
    }
    difftest::EmptySandbox();
    if (storedCompressed < 100 || storedPlain < 100 || chunked < 20 || missingFile < 20 || emptyFile < 10 || notWritable < 300 || pendingKey < 30 || writeFailed < 60 || allocFailed < 60 || removedOld < 200 ||
        fellBack < 50)
        ctx.Fail("ImportFile: setup reaches too little (%d compressed, %d plain, %d chunked, %d missing files, %d empty files, %d not writable, %d pending, %d write failures, %d allocation failures, "
                 "%d removed, %d fell back to the plain path)",
                 storedCompressed, storedPlain, chunked, missingFile, emptyFile, notWritable, pendingKey, writeFailed, allocFailed, removedOld, fellBack);
}

DIFF_TEST(cGZDBSegmentPackedFile_ReadHeader, 0x9af2a0)
{
    WorldScope world(ctx);
    HeaderGlobal global(ctx);
    InitCloseFakes();
    gFileTab[12] = reinterpret_cast<void *>(&SeekRecording12);
    gFileTab[14] = reinterpret_cast<void *>(&Read14);
    Redirect find(ctx, 0x9aadf0, reinterpret_cast<void *>(&FakeFindHeader));
    Redirect findOurs(Addr(&cGZDBSegmentPackedFile::FindHeader), reinterpret_cast<void *>(&FakeFindHeader));
    using Fn = uint8_t(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9af2a0);
    Tables plain = MakeTables(ctx);
    std::mt19937 rng(0x9af2a0);
    int first = 0, found = 0, notFound = 0, twice = 0, seekMissed = 0, readFailed = 0, upgraded = 0;
    for (int i = 0; i < 3000; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        WireClose(img);
        *global.global = 0x46504244;
        const uint32_t length = 0x400 + rng() % 0x2000;
        gLengths.assign(1, length);
        gFileImage.assign(length, 0);
        for (auto &b : gFileImage) b = static_cast<uint8_t>(rng());
        const uint32_t base = rng() % 3 == 0 ? 0 : rng() % 0x200;
        SetWord(img, 0xf4, base);
        PutHeader(rng, base, length, rng() % 2 == 0);
        // Where the search lands: nothing, a good header, a bad one (searched again: the guard stops it).
        gFindAnswers.clear();
        for (int k = 0; k < 2; ++k) {
            const uint32_t pick = rng() % 4;
            if (pick == 0) {
                gFindAnswers.push_back(0xffffffffu);
            } else {
                const uint32_t at = 0x200 + rng() % 0x100;
                PutHeader(rng, at, length, pick != 3);
                gFindAnswers.push_back(at);
            }
        }
        gSeekLands = rng() % 10 != 0;
        gRead14Answer = rng() % 10 != 0;
        std::memcpy(gArena, img.data(), kBytes);
        Snapshot snap;
        snap.Take();
        auto run = [&](auto fn) {
            return WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
                gFindIndex = 0;
                gLengthIdx = 0;
                gSeekPos = 0;
                return fn(self) & 0xff;
            });
        };
        auto a = run([&](uint8_t *self) -> uint64_t { return orig(self); });
        auto b = run([&](uint8_t *self) -> uint64_t { return Obj(self)->ReadHeader(); });
        WorldCompare(ctx, "ReadHeader", a, b, b);
        const int searches = static_cast<int>(std::count(a.log.begin(), a.log.end(), kFindHeader));
        first += a.result != 0 && searches == 0;
        found += a.result != 0 && searches == 1;
        notFound += searches >= 1 && gFindAnswers[searches - 1] == 0xffffffffu;
        twice += searches == 2;
        seekMissed += !gSeekLands;
        readFailed += gSeekLands && !gRead14Answer;
        uint32_t minorAfter;
        std::memcpy(&minorAfter, gArena + 0x154, 4);
        upgraded += a.result != 0 && minorAfter == 1;
    }
    std::printf("  ReadHeader coverage: first time %d, found by the search %d, nothing found %d, searched twice %d, seek missed %d, "
                "read failed %d, ok with minor version 1 %d\n",
                first, found, notFound, twice, seekMissed, readFailed, upgraded);
    if (first < 300 || found < 150 || notFound < 200 || twice < 100 || seekMissed < 150 || readFailed < 150 || upgraded < 200)
        ctx.Fail("ReadHeader: setup reaches too little");
}

DIFF_TEST(cGZDBSegmentPackedFile_ReadIndex, 0x9af590)
{
    WorldScope world(ctx);
    ServiceScope service(ctx);
    InitCloseFakes();
    InitMapFakes();
    StoreSlots slots;
    gStoreTab[0] = reinterpret_cast<void *>(&StoreDelete);
    gFileTab[12] = reinterpret_cast<void *>(&SeekRecording12);
    gFileTab[14] = reinterpret_cast<void *>(&Read14);
    gNsTab[3] = reinterpret_cast<void *>(&NsMap);
    gNsTab[4] = reinterpret_cast<void *>(&NsMap);
    gGameCompressedVTable = ctx.image.At(cGZDBPFIndexCompressed::kVTable);
    gGameStaticVTable = ctx.image.At(cGZDBPFIndexStatic::kVTable);
    using Fn = uint8_t(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9af590);
    Tables plain = MakeTables(ctx);
    std::mt19937 rng(0x9af590);
    int empty = 0, compressed = 0, statics = 0, refusedBig = 0, doubleDelete = 0, seekMissed = 0, readFailed = 0, six = 0, kept = 0;
    for (int i = 0; i < 2500; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        WireClose(img);
        const bool hasOld = rng() % 2 == 0;
        if (!hasOld) SetWord(img, 0x30, 0);
        SetPointer(img, kNs, gNsTab);
        SetPointer(img, 0x18, rng() % 3 == 0 ? gArena + kNs : nullptr);
        const uint32_t pick = rng() % 8;
        const uint32_t count = pick == 0 ? 0 : pick < 4 ? 1 + rng() % 0x3f : 0x40 + rng() % 100;
        // Versions 0 and 3 read no words; they go with no entries, a seek that misses or a read that refuses (the index
        // classes would read nothing from a null buffer; a refused read still shows the length asked for).
        uint32_t version = 1 + rng() % 2;
        gSeekLands = rng() % 10 != 0;
        const bool readOk = rng() % 10 != 0;
        if ((count == 0 || !gSeekLands || !readOk) && rng() % 3 == 0) version = rng() % 2 ? 0 : 3;
        SetWord(img, 0x154, version);
        const uint32_t words = version == 1 ? 5 : version == 2 ? 6 : 0;
        const uint32_t length = 0x1000 + rng() % 0x8000;
        gLengths.assign(1, length);
        const uint32_t base = rng() % 3 == 0 ? 0 : rng() % 0x100;
        const uint32_t offset = 0x60 + rng() % 0x200;
        SetWord(img, 0xf4, base);
        SetWord(img, 0x13c, count);
        SetWord(img, 0x140, offset);
        gFileImage.assign(base + offset, 0);
        const uint32_t types = 1 + rng() % 4, groups = 1 + rng() % 4;
        for (uint32_t k = 0; k < count; ++k) {
            const size_t at = base + offset + k * words * 4;
            if (words == 0) break;
            uint32_t w[6];
            w[0] = 0x1000 + rng() % types;
            w[1] = 0x2000 + rng() % groups;
            w[2] = static_cast<uint32_t>(rng());
            w[3] = rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : 0;
            w[words - 2] = 0x60 + rng() % length;
            w[words - 1] = rng() % 10 == 0 ? 0x10000 + rng() % 0x10000 : rng() % 0x2000;
            for (uint32_t m = 0; m < words; ++m) PutAt(at + 4 * m, w[m]);
        }
        gRead14Answer = readOk;
        std::memcpy(gArena, img.data(), kBytes);
        const bool track = rng() % 3 == 0;
        gArena[0x19f] = track ? 1 : 0;
        const uint32_t mapSeed = static_cast<uint32_t>(rng());
        Snapshot snap;
        snap.Take();
        auto run = [&](auto fn) {
            return WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
                gLengthIdx = 0;
                gSeekPos = 0;
                uint64_t r = WithMaps(self, track, mapSeed, [&](uint8_t *s) -> uint64_t { return fn(s) & 0xff; });
                DescribeIndex(self);
                return r;
            });
        };
        auto a = run([&](uint8_t *self) -> uint64_t { return orig(self); });
        auto b = run([&](uint8_t *self) -> uint64_t { return Obj(self)->ReadIndex(); });
        WorldCompare(ctx, "ReadIndex", a, b, b);
        const auto kindAt = std::find(a.log.begin(), a.log.end(), kIndexKind);
        const int64_t kind = kindAt + 1 < a.log.end() ? *(kindAt + 1) : -9;
        const int deletes = static_cast<int>(std::count(a.log.begin(), a.log.end(), kStoreDelete));
        empty += count == 0 && kind == 9;
        compressed += kind == 2;
        statics += kind == 1 && count < 0x40;
        refusedBig += kind == 1 && count >= 0x40;
        doubleDelete += deletes == 2;
        seekMissed += count != 0 && !gSeekLands;
        readFailed += count != 0 && gSeekLands && !gRead14Answer;
        six += words == 6 && kind > 0;
        kept += count == 0 && hasOld;
    }
    std::printf("  ReadIndex coverage: new modifiable %d, compressed %d, static %d, big but static %d, old deleted twice %d, seek missed %d, "
                "read failed %d, six words %d, kept the old one %d\n",
                empty, compressed, statics, refusedBig, doubleDelete, seekMissed, readFailed, six, kept);
    if (empty < 50 || compressed < 500 || statics < 400 || doubleDelete < 200 || seekMissed < 100 || readFailed < 100 || six < 400 ||
        kept < 50)
        ctx.Fail("ReadIndex: setup reaches too little");
}

DIFF_TEST(cGZDBSegmentPackedFile_ReadHoles, 0x9af390)
{
    WorldScope world(ctx);
    InitCloseFakes();
    gFileTab[12] = reinterpret_cast<void *>(&SeekRecording12);
    gFileTab[14] = reinterpret_cast<void *>(&Read14);
    using Fn = uint8_t(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9af390);
    Tables plain = MakeTables(ctx);
    std::mt19937 rng(0x9af390);
    int none = 0, loaded = 0, refused = 0, forced = 0, seekMissed = 0, readFailed = 0, repeats = 0, edges = 0;
    for (int i = 0; i < 3000; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        WireClose(img);
        const uint32_t length = 0x400 + rng() % 0x4000;
        gLengths.assign(1, length);
        const uint32_t holes = rng() % 8 == 0 ? 0 : 1 + rng() % 10;
        uint32_t offset = 0x60 + rng() % (length - 0x60 - 0x60);
        const int edge = rng() % 4 == 0 ? static_cast<int>(rng() % 5) : -1;
        if (edge == 0) offset = 0x60;
        if (edge == 1) offset = 0x5f;
        if (edge == 2) offset = length;
        if (edge == 3) offset = length - 1;
        if (edge == 4) offset = 0;
        const uint32_t base = rng() % 3 == 0 ? 0 : rng() % 0x80;
        SetWord(img, 0xf4, base);
        SetWord(img, 0x148, holes);
        SetWord(img, 0x14c, offset);
        SetWord(img, 0x150, rng() % 5 == 0 ? (holes + 1 + rng() % 3) * 8 : holes * 8);
        img[0x2a] = rng() % 4 == 0 ? static_cast<uint8_t>(1 + rng() % 255) : 0;
        gFileImage.assign(base + offset + holes * 8 + 8, 0);
        uint32_t start = 0x60 + rng() % 0x100;
        bool repeated = false;
        for (uint32_t k = 0; k < holes; ++k) {
            if (k > 0 && rng() % 6 == 0) {
                repeated = true;
            } else {
                start += 1 + rng() % 0x80;
            }
            PutAt(base + offset + 8 * k, start);
            PutAt(base + offset + 8 * k + 4, 1 + rng() % 0x40);
        }
        gSeekLands = rng() % 10 != 0;
        gRead14Answer = rng() % 10 != 0;
        std::memcpy(gArena, img.data(), kBytes);
        BuildRanges(rng, 0x184, 5);
        BuildRanges(rng, 0x18c, 4);
        Snapshot snap;
        snap.Take();
        auto run = [&](auto fn) {
            return WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
                gLengthIdx = 0;
                gSeekPos = 0;
                return fn(self) & 0xff;
            });
        };
        auto a = run([&](uint8_t *self) -> uint64_t { return orig(self); });
        auto b = run([&](uint8_t *self) -> uint64_t { return Obj(self)->ReadHoles(); });
        WorldCompare(ctx, "ReadHoles", a, b, b);
        const bool reachedCheck = holes != 0 && gSeekLands && gRead14Answer;
        none += holes == 0;
        loaded += reachedCheck && a.result != 0;
        refused += reachedCheck && a.result == 0;
        forced += reachedCheck && a.result != 0 && gArena[0x2a] != 0;
        seekMissed += holes != 0 && !gSeekLands;
        readFailed += holes != 0 && gSeekLands && !gRead14Answer;
        repeats += reachedCheck && repeated;
        edges += reachedCheck && edge >= 0;
    }
    std::printf("  ReadHoles coverage: none %d, loaded %d, refused %d, loaded through +0x2a %d, seek missed %d, read failed %d, "
                "repeated starts %d, edge offsets %d\n",
                none, loaded, refused, forced, seekMissed, readFailed, repeats, edges);
    if (none < 200 || loaded < 800 || refused < 300 || forced < 100 || seekMissed < 150 || readFailed < 150 || repeats < 300 || edges < 400)
        ctx.Fail("ReadHoles: setup reaches too little");
}

namespace {

std::vector<uint32_t> gDirBlock;
uint32_t gDirFirst = 0, gDirSizeOut = 0, gDirSizeHint = 0;
uint8_t gDirSecond = 1;
int gDirCall = 0;

uint32_t __fastcall FakeDirRead(void *self, void *, const uint32_t *key, void *dst, uint32_t *size)
{
    Push(kDirRead, Off(self), dst == nullptr ? 0 : 1);
    for (int i = 0; i < 4; ++i) gLog.push_back(key[i]);
    gLog.push_back(*size);
    if (gDirCall++ == 0) {
        *size = gDirSizeHint;
        return gDirFirst;
    }
    std::memcpy(dst, gDirBlock.data(), gDirFirst);
    *size = gDirSizeOut;
    return gDirSecond;
}

} // namespace

namespace {
// The namespace's slot 3, which the directory reader must not use: it logs and leaves the key alone.
void __fastcall NsWrongSlot(void *self, void *, const uint32_t *, uint32_t *)
{
    Push(230, Off(self), 0);
}
} // namespace

DIFF_TEST(cGZDBSegmentPackedFile_ReadCompressionDirectory, 0x9aea00)
{
    ImageAllocScope image(ctx);
    WorldScope world(ctx);
    InitCloseFakes();
    StoreSlots slots;
    gStoreTab[15] = reinterpret_cast<void *>(&StoreMemSize);
    gNsTab[3] = reinterpret_cast<void *>(&NsWrongSlot);
    gNsTab[4] = reinterpret_cast<void *>(&NsMap);
    using Fn = uint8_t(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(0x9aea00);
    Tables plain = MakeTables(ctx);
    plain.sub[42] = reinterpret_cast<void *>(&FakeDirRead);
    std::mt19937 rng(0x9aea00);
    int absent = 0, failed = 0, read = 0, mapped = 0, five = 0, empty = 0, ragged = 0;
    for (int i = 0; i < 2500; ++i) {
        ResetWorld();
        Image img = MakeImage(rng);
        WireClose(img);
        SetPointer(img, kNs, gNsTab);
        const bool hasNs = rng() % 2 == 0;
        SetPointer(img, 0x18, hasNs ? gArena + kNs : nullptr);
        const uint32_t version = rng() % 5 == 0 ? (rng() % 2 ? 0 : 3) : 1 + rng() % 2;
        SetWord(img, 0x154, version);
        const uint32_t stride = version == 2 ? 20 : 16;
        const uint32_t entries = rng() % 6 == 0 ? 0 : 1 + rng() % 40;
        const bool isRagged = rng() % 5 == 0;
        gDirFirst = rng() % 8 == 0 ? 0xffffffffu : entries * stride + (isRagged ? 1 + rng() % (stride - 1) : 0);
        gDirSizeHint = rng() % 2 ? 0 : PickWord(rng);
        gDirSecond = rng() % 8 == 0 ? 0 : static_cast<uint8_t>(1 + rng() % 200);
        gDirSizeOut = gDirFirst;
        if (gDirFirst != 0xffffffffu && rng() % 6 == 0) gDirSizeOut = rng() % (gDirFirst + 1);
        gDirBlock.assign(gDirFirst == 0xffffffffu ? 1 : gDirFirst / 4 + 1, 0);
        for (auto &w : gDirBlock) w = rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : rng() % 0x100;
        std::memcpy(gArena, img.data(), kBytes);
        Snapshot snap;
        snap.Take();
        auto run = [&](auto fn) {
            return WorldRun(snap, plain, [&](uint8_t *self) -> uint64_t {
                gDirCall = 0;
                return fn(self) & 0xff;
            });
        };
        auto a = run([&](uint8_t *self) -> uint64_t { return orig(self); });
        auto b = run([&](uint8_t *self) -> uint64_t { return Obj(self)->ReadCompressionDirectory(); });
        WorldCompare(ctx, "ReadCompressionDirectory", a, b, b);
        const int sizes = static_cast<int>(std::count(a.log.begin(), a.log.end(), kMemSize));
        absent += gDirFirst == 0xffffffffu;
        failed += gDirFirst != 0xffffffffu && gDirSecond == 0;
        read += sizes > 0;
        mapped += sizes > 0 && hasNs;
        five += sizes > 0 && version == 2;
        empty += gDirFirst != 0xffffffffu && gDirSecond != 0 && sizes == 0;
        ragged += sizes > 0 && isRagged;
    }
    std::printf("  ReadCompressionDirectory coverage: absent %d, read failed %d, sizes set %d, through the namespace %d, five words %d, "
                "nothing to set %d, ragged sizes %d\n",
                absent, failed, read, mapped, five, empty, ragged);
    if (absent < 150 || failed < 150 || read < 1000 || mapped < 400 || five < 400 || empty < 150 || ragged < 200)
        ctx.Fail("ReadCompressionDirectory: setup reaches too little");
}
