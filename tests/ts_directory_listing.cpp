// Differential tests for DirectoryListing (src/ts/DirectoryListing.*): the persist system's cached folder listing.
// Constructor 0x1f58f0, destructor 0x1f6090, First 0x1f6dd0, Next 0x1f61e0, EntryPath 0x1f64f0, EntryKind 0x1f65c0,
// FolderStamp 0x1f6870.
//
// The constructor's world: the primary (slot 47: the default segment or null), the segment (slot 22 OpenRecord), the
// cache record (2 Release, 6 Close, 7 Abort, 8 Read, 9 Write, 10 GetSize) are fakes logging every call; the directory scan
// (0x1c48da build, 0x1c5896 scan, 0x1c5047 entry, 0x1c49eb destroy: the image's and ours, lane LY) and FolderStamp (ours
// and the image's) are stand-ins. The game's operator new / delete go to ours. Compared: the call log, the block (bytes and
// capacity), the entry pointers (as offsets into the block, and capacity), the cursor, the path string. The record's
// written bytes are logged in full.
#include "code_patch.h"
#include "difftest.h"
#include "app_debugger_fakes.h"
#include "rz/DirectoryScan.h"
#include "rz/PathFileOps.h"
#include "rz/cRZString.h"
#include "ts/DirectoryListing.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

#include <windows.h>

namespace {

std::string gLog;
uint32_t gSeed;
uint32_t gCalls;

uint32_t Mix(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    return h;
}

void Log(const char *fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    gLog += buf;
    gLog += ';';
}

// A byte answer with garbage above it: 1 unless the draw for `salt` is 0 modulo `modulus`.
uint32_t Bit(uint32_t salt, uint32_t modulus)
{
    return (Mix(gSeed, salt + gCalls) & 0xffffff00u) | (Mix(gSeed, salt ^ 0x5a5a) % modulus != 0 ? 1u : 0u);
}

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

// ---- fakes with plain vtables (thiscall as __fastcall) ----

struct Fake {
    void **vtable;
};

void __fastcall BadSlot(void *self, void *) { Log("BAD SLOT on %p", self); }

struct FakeVTable {
    void *slots[64];
    FakeVTable()
    {
        for (auto &s : slots) s = reinterpret_cast<void *>(&BadSlot);
    }
};

FakeVTable gPrimaryVt, gStoreVt, gRecordVt;
Fake gPrimary{gPrimaryVt.slots}, gStore{gStoreVt.slots}, gRecord{gRecordVt.slots};

// What the store answers this case.
bool gHasStore;
uint32_t gOpenFail[3];        // per mode (1 read, 2 write): nonzero fails
bool gNullRecordOnFail;
std::vector<uint8_t> gCache;  // the record's contents for reads
size_t gReadPos;
uint32_t gReadFailAt;         // the n-th read fails (1-based), 0 never
uint32_t gReads, gWrites;
uint32_t gWriteFailAt;

void *__fastcall PrimaryStore(Fake *, void *)
{
    Log("primary.47");
    return gHasStore ? &gStore : nullptr;
}

uint32_t __fastcall StoreOpen(Fake *, void *, const uint32_t *key, void **record, uint32_t mode)
{
    ++gCalls;
    Log("store.open %08x %08x %08x %08x mode %u", key[0], key[1], key[2], key[3], mode);
    const bool fail = mode < 3 && gOpenFail[mode];
    if (fail) {
        if (!gNullRecordOnFail) *record = &gRecord; // a record handed out although the open failed: released
        return Mix(gSeed, 77) & 0xffffff00u;
    }
    *record = &gRecord;
    gReadPos = 0;
    gReads = gWrites = 0;
    return (Mix(gSeed, 78) & 0xffffff00u) | 1;
}

uint32_t __fastcall RecordRead(Fake *, void *, void *out, uint32_t n)
{
    ++gCalls;
    ++gReads;
    Log("record.read %u", n);
    if (gReads == gReadFailAt) return Mix(gSeed, 80) & 0xffffff00u;
    for (uint32_t i = 0; i < n; ++i) static_cast<uint8_t *>(out)[i] = gReadPos < gCache.size() ? gCache[gReadPos++] : 0x5c;
    return (Mix(gSeed, 81) & 0xffffff00u) | 1;
}

uint32_t __fastcall RecordWrite(Fake *, void *, const void *data, uint32_t n)
{
    ++gCalls;
    ++gWrites;
    std::string bytes;
    char hex[4];
    for (uint32_t i = 0; i < n; ++i) {
        std::snprintf(hex, sizeof(hex), "%02x", static_cast<const uint8_t *>(data)[i]);
        bytes += hex;
    }
    Log("record.write %u %s", n, bytes.c_str());
    if (gWrites == gWriteFailAt) return Mix(gSeed, 82) & 0xffffff00u;
    return (Mix(gSeed, 83) & 0xffffff00u) | 1;
}

uint32_t __fastcall RecordSize(Fake *, void *)
{
    Log("record.size");
    return static_cast<uint32_t>(gCache.size());
}

uint32_t __fastcall RecordClose(Fake *, void *) { Log("record.close"); return 1; }
uint32_t __fastcall RecordAbort(Fake *, void *) { Log("record.abort"); return 1; }
uint32_t __fastcall RecordRelease(Fake *, void *) { Log("record.release"); return 0; }

void SetUpFakes()
{
    gPrimaryVt.slots[47] = reinterpret_cast<void *>(&PrimaryStore);
    gStoreVt.slots[22] = reinterpret_cast<void *>(&StoreOpen);
    gRecordVt.slots[2] = reinterpret_cast<void *>(&RecordRelease);
    gRecordVt.slots[6] = reinterpret_cast<void *>(&RecordClose);
    gRecordVt.slots[7] = reinterpret_cast<void *>(&RecordAbort);
    gRecordVt.slots[8] = reinterpret_cast<void *>(&RecordRead);
    gRecordVt.slots[9] = reinterpret_cast<void *>(&RecordWrite);
    gRecordVt.slots[10] = reinterpret_cast<void *>(&RecordSize);
}

// ---- the directory scan and the folder stamp ----

// A scan entry as the listing reads it: the name at +8 (std::string), the kind byte at +0x24.
struct ScanEntry {
    uint8_t head[8];
    std::string name;
    uint8_t gap[4];
    uint8_t kind;
    uint8_t tail[3];
};
static_assert(offsetof(ScanEntry, name) == 8 && offsetof(ScanEntry, kind) == 0x24, "scan entry layout");

std::vector<ScanEntry> gEntries;

void *__fastcall ScanBuild(uint8_t *self, void *, const cRZString *path, uint32_t a, uint32_t b, uint32_t c)
{
    Log("scan.build '%s' %u %u %u", path->mString.c_str(), a, b, c);
    std::memset(self, 0xcd, 0x5c);
    const uint32_t count = static_cast<uint32_t>(gEntries.size());
    std::memcpy(self + 8, &count, 4);
    return self;
}
uint32_t __fastcall ScanScan(uint8_t *, void *)
{
    Log("scan.scan");
    return Mix(gSeed, 90);
}
void *__fastcall ScanEntryAt(uint8_t *, void *, uint32_t index)
{
    Log("scan.entry %u", index);
    return &gEntries[index];
}
void __fastcall ScanDestroy(uint8_t *, void *) { Log("scan.destroy"); }

uint32_t gStamp;
uint32_t __cdecl StampStand(const cRZString *folder)
{
    Log("stamp '%s'", folder->mString.c_str());
    return gStamp;
}

const char *const kPaths[] = {"C:\\Games\\Data", "C:/Games/Data/", "", "rel", "C:\\Games\\Data\\",
                              "D:\\a folder name that is longer than fifteen characters", "x\\", "Folder/Sub"};

std::string RandomName(std::mt19937 &rng)
{
    static const char kChars[] = "abcXYZ019 ._-";
    const uint32_t len = 1 + rng() % (rng() % 4 == 0 ? 40 : 10);
    std::string s;
    for (uint32_t i = 0; i < len; ++i) s += kChars[rng() % (sizeof(kChars) - 1)];
    return s;
}

// The cache record's contents: stamp, then a block for `path` (or another path), maybe cut short.
void MakeCache(std::mt19937 &rng, const std::string &path)
{
    gCache.clear();
    const uint32_t stamp = rng() % 3 == 0 ? gStamp ^ 1 : gStamp;
    gCache.resize(4);
    std::memcpy(gCache.data(), &stamp, 4);
    std::string first = path;
    const uint32_t which = rng() % 6;
    if (which == 0) first = "C:\\Other\\";
    if (which == 1)
        for (char &c : first)
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A'); // the compare ignores case
    for (char c : first) gCache.push_back(static_cast<uint8_t>(c));
    gCache.push_back(0);
    const uint32_t entries = rng() % 4;
    for (uint32_t i = 0; i < entries; ++i) {
        gCache.push_back(static_cast<uint8_t>((rng() % 2 ? 4 : 8) | 0x80));
        for (char c : RandomName(rng)) gCache.push_back(static_cast<uint8_t>(c));
        gCache.push_back(0);
    }
    if (rng() % 2) gCache.push_back(0); // the stored block's own end, or none (the reader adds two zeros)
    if (rng() % 7 == 0) gCache.resize(4 + rng() % 3); // short records: an empty block, or one byte of it
}

struct State {
    std::string log;
    std::string block, path;
    size_t blockCapacity = 0, entriesCapacity = 0, pathCapacity = 0;
    std::vector<intptr_t> entries;
    intptr_t cursor = 0;
    uint32_t pathRefCount = 0, pathVTable = 0;
};

State Capture(const DirectoryListing *l)
{
    State s;
    s.log = gLog;
    s.block.assign(l->mBuffer.data(), l->mBuffer.size());
    s.blockCapacity = l->mBuffer.capacity();
    for (const char *e : l->mEntries) s.entries.push_back(e - l->mBuffer.data());
    s.entriesCapacity = l->mEntries.capacity();
    s.cursor = reinterpret_cast<intptr_t>(l->mCursor);
    s.path = l->mPath.mString;
    s.pathCapacity = l->mPath.mString.capacity();
    s.pathRefCount = l->mPath.mRefCount;
    s.pathVTable = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(l->mPath.mVTable));
    return s;
}

const char *FirstDiff(const std::string &a, const std::string &b)
{
    static std::string out;
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    const size_t from = i > 40 ? i - 40 : 0;
    out = "at " + std::to_string(i) + ": orig ..." + a.substr(from, 120) + " | ours ..." + b.substr(from, 120);
    return out.c_str();
}

// Draws the case: the store's answers, the cache, the scan's entries, the stamp.
void DrawCase(uint32_t seed, const std::string &path)
{
    std::mt19937 rng(seed * 7919 + 13);
    gSeed = seed;
    gCalls = 0;
    gHasStore = rng() % 5 != 0;
    gStamp = rng() % 6 == 0 ? 0 : (rng() | 1);
    gOpenFail[1] = rng() % 5 == 0;
    gOpenFail[2] = rng() % 4 == 0;
    gNullRecordOnFail = rng() % 2 == 0;
    gReadFailAt = rng() % 6 == 0 ? 1 + rng() % 2 : 0;
    gWriteFailAt = rng() % 5 == 0 ? 1 + rng() % 2 : 0;
    std::string withSeparator = path;
    if (!withSeparator.empty() && withSeparator.back() != '\\' && withSeparator.back() != '/') withSeparator += '\\';
    MakeCache(rng, withSeparator);
    gEntries.clear();
    const uint32_t count = rng() % 5 == 0 ? 0 : rng() % 7;
    gEntries.resize(count);
    for (auto &e : gEntries) {
        std::memset(e.head, 0xee, sizeof(e.head));
        e.name = RandomName(rng);
        e.kind = static_cast<uint8_t>(rng() % 3 == 0 ? rng() : (rng() % 2 ? 4 : 8));
    }
}

} // namespace

// The constructor (and with it the parsing and the cache read and write).
DIFF_TEST(DirectoryListing_Construct, 0x1f58f0)
{
    SetUpFakes();
    shape::CodePatch newPatch(0x824632u, reinterpret_cast<const void *>(&GameNew));
    shape::CodePatch deletePatch(0x824624u, reinterpret_cast<const void *>(&GameDelete));
    shape::StandIn build(0x1c48dau, &DirectoryScanAdapters::Construct, &ScanBuild);
    shape::StandIn scan(0x1c5896u, &DirectoryScan::Scan, &ScanScan);
    shape::StandIn entry(0x1c5047u, &DirectoryScan::EntryAt, &ScanEntryAt);
    shape::StandIn destroy(0x1c49ebu, &DirectoryScanAdapters::Destruct, &ScanDestroy);
    shape::StandIn stamp(0x1f6870u, &DirectoryListing::FolderStamp, &StampStand);
    auto original = ctx.Original<void *(__thiscall *)(void *, const cRZString *, void *)>(0x1f58f0);
    auto originalDtor = ctx.Original<void(__thiscall *)(void *)>(0x1f6090);

    uint32_t cachedHits = 0, scans = 0, writes = 0;
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        const std::string path = kPaths[seed % (sizeof(kPaths) / sizeof(kPaths[0]))];
        const bool withPrimary = seed % 9 != 0;
        State sides[2];
        void *returned[2] = {};
        for (int side = 0; side < 2; ++side) {
            DrawCase(seed, path);
            gLog.clear();
            cRZString input;
            input.Construct(path.c_str());
            alignas(8) uint8_t storage[sizeof(DirectoryListing)];
            std::memset(storage, 0xa5, sizeof(storage));
            auto *listing = reinterpret_cast<DirectoryListing *>(storage);
            ctx.Mark(side == 0 ? "construct: original" : "construct: ours");
            if (side == 0)
                returned[side] = original(storage, &input, withPrimary ? &gPrimary : nullptr);
            else
                returned[side] = DirectoryListingAdapters::Construct(listing, nullptr, &input, withPrimary ? &gPrimary : nullptr);
            sides[side] = Capture(listing);
            if (side == 0) originalDtor(storage); else listing->~DirectoryListing();
            input.Destruct();
            if (returned[side] != storage) ctx.Fail("seed %u side %d: returned %p, not this", seed, side, returned[side]);
        }
        ++ctx.cases;
        if (sides[0].log.find("scan.build") != std::string::npos) ++scans; else ++cachedHits;
        if (sides[0].log.find("record.write") != std::string::npos) ++writes;
        const State &a = sides[0], &b = sides[1];
        if (a.log != b.log) ctx.Fail("seed %u: calls differ %s", seed, FirstDiff(a.log, b.log));
        if (a.block != b.block) ctx.Fail("seed %u: block differs %s", seed, FirstDiff(a.block, b.block));
        if (a.blockCapacity != b.blockCapacity) ctx.Fail("seed %u: block capacity %zu vs %zu", seed, a.blockCapacity, b.blockCapacity);
        if (a.entries != b.entries) ctx.Fail("seed %u: entries differ (%zu vs %zu)", seed, a.entries.size(), b.entries.size());
        if (a.entriesCapacity != b.entriesCapacity) ctx.Fail("seed %u: entries capacity %zu vs %zu", seed, a.entriesCapacity, b.entriesCapacity);
        if (a.cursor != b.cursor) ctx.Fail("seed %u: cursor differs", seed);
        if (a.path != b.path || a.pathCapacity != b.pathCapacity || a.pathRefCount != b.pathRefCount || a.pathVTable != b.pathVTable)
            ctx.Fail("seed %u: path differs '%s' vs '%s'", seed, a.path.c_str(), b.path.c_str());
    }
    if (cachedHits < 50 || scans < 300 || writes < 200) ctx.Fail("coverage: cached %u, scanned %u, written %u", cachedHits, scans, writes);
}

// First, Next, EntryKind and EntryPath over listings our constructor built (the same on both sides).
DIFF_TEST(DirectoryListing_Walk, 0x1f6dd0)
{
    SetUpFakes();
    shape::CodePatch newPatch(0x824632u, reinterpret_cast<const void *>(&GameNew));
    shape::CodePatch deletePatch(0x824624u, reinterpret_cast<const void *>(&GameDelete));
    shape::StandIn build(0x1c48dau, &DirectoryScanAdapters::Construct, &ScanBuild);
    shape::StandIn scan(0x1c5896u, &DirectoryScan::Scan, &ScanScan);
    shape::StandIn entry(0x1c5047u, &DirectoryScan::EntryAt, &ScanEntryAt);
    shape::StandIn destroy(0x1c49ebu, &DirectoryScanAdapters::Destruct, &ScanDestroy);
    shape::StandIn stamp(0x1f6870u, &DirectoryListing::FolderStamp, &StampStand);
    auto first = ctx.Original<uint32_t(__thiscall *)(void *)>(0x1f6dd0);
    auto next = ctx.Original<uint32_t(__thiscall *)(void *)>(0x1f61e0);
    auto kind = ctx.Original<uint32_t(__thiscall *)(void *)>(0x1f65c0);
    auto entryPath = ctx.Original<cRZString *(__thiscall *)(void *, cRZString *)>(0x1f64f0);
    // Registered under 0x1f6dd0 (First); it also covers 0x1f61e0 Next, 0x1f65c0 EntryKind, 0x1f64f0 EntryPath.

    uint32_t steps = 0;
    for (uint32_t seed = 0; seed < 800; ++seed) {
        const std::string path = kPaths[seed % (sizeof(kPaths) / sizeof(kPaths[0]))];
        std::string out[2];
        for (int side = 0; side < 2; ++side) {
            DrawCase(seed, path);
            gLog.clear();
            cRZString input;
            input.Construct(path.c_str());
            ScopedDirectoryListing listing(&input, seed % 3 ? &gPrimary : nullptr);
            DirectoryListing *l = listing.Get();
            std::string &o = out[side];
            char buf[64];
            const uint32_t f = side == 0 ? first(l) : l->First();
            std::snprintf(buf, sizeof(buf), "first %02x cursor %d;", f & 0xff, int(l->mCursor - l->mEntries.data()));
            o += buf;
            if (f & 0xff) {
                for (int guard = 0; guard < 64; ++guard) {
                    const uint32_t k = side == 0 ? kind(l) : l->EntryKind();
                    alignas(8) uint8_t raw[sizeof(cRZString) + 8];
                    std::memset(raw, 0x3c, sizeof(raw));
                    auto *name = reinterpret_cast<cRZString *>(raw);
                    cRZString *r = side == 0 ? entryPath(l, name) : l->EntryPath(name);
                    std::snprintf(buf, sizeof(buf), "kind %08x ret %d ref %u vt %08x cap %u tail %02x;", k, int(r == name), name->mRefCount,
                                  static_cast<uint32_t>(reinterpret_cast<uintptr_t>(name->mVTable)), unsigned(name->mString.capacity()),
                                  raw[sizeof(cRZString)]);
                    o += buf;
                    o += name->mString + ";";
                    name->Destruct();
                    const uint32_t n = side == 0 ? next(l) : l->Next();
                    std::snprintf(buf, sizeof(buf), "next %02x cursor %d;", n & 0xff, int(l->mCursor - l->mEntries.data()));
                    o += buf;
                    ++steps;
                    if (!(n & 0xff)) break;
                }
            }
            input.Destruct();
        }
        ++ctx.cases;
        if (out[0] != out[1]) ctx.Fail("seed %u: walk differs %s", seed, FirstDiff(out[0], out[1]));
    }
    if (steps < 1000) ctx.Fail("coverage: %u steps", steps);
}

// The destructor: what it frees and what it leaves in the object.
DIFF_TEST(DirectoryListing_Destruct, 0x1f6090)
{
    SetUpFakes();
    shape::CodePatch newPatch(0x824632u, reinterpret_cast<const void *>(&GameNew));
    shape::CodePatch deletePatch(0x824624u, reinterpret_cast<const void *>(&GameDelete));
    shape::StandIn build(0x1c48dau, &DirectoryScanAdapters::Construct, &ScanBuild);
    shape::StandIn scan(0x1c5896u, &DirectoryScan::Scan, &ScanScan);
    shape::StandIn entry(0x1c5047u, &DirectoryScan::EntryAt, &ScanEntryAt);
    shape::StandIn destroy(0x1c49ebu, &DirectoryScanAdapters::Destruct, &ScanDestroy);
    shape::StandIn stamp(0x1f6870u, &DirectoryListing::FolderStamp, &StampStand);
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x1f6090);
    for (uint32_t seed = 0; seed < 400; ++seed) {
        const std::string path = seed % 2 ? std::string(kPaths[seed % 8]) : "a path well past the small-string buffer\\" + std::to_string(seed);
        std::string after[2];
        size_t sizes[2][8] = {}, counts[2] = {};
        for (int side = 0; side < 2; ++side) {
            DrawCase(seed, path);
            cRZString input;
            input.Construct(path.c_str());
            alignas(8) uint8_t storage[sizeof(DirectoryListing)];
            auto *l = DirectoryListingAdapters::Construct(reinterpret_cast<DirectoryListing *>(storage), nullptr, &input, nullptr);
            if (seed % 3 == 0) l->mPath.mRefCount = seed; // the destructor leaves the count alone
            {
                difftest::FreeRecorder frees;
                if (side == 0) original(storage); else DirectoryListingAdapters::Destruct(l, nullptr);
                frees.Stop();
                counts[side] = frees.SortedSizes(sizes[side], 8);
            }
            // The words the destructor writes: the vectors' pointers, the string's size and capacity and first byte, the
            // vtable word. The string's buffer word past its first byte is left as it was (stale heap pointer bytes).
            uint32_t words[15];
            std::memcpy(words, storage, sizeof(words));
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%08x %08x %08x %08x %08x %08x vt %08x b %02x size %u cap %u ref %u", words[0], words[1], words[2],
                          words[3], words[4], words[5], words[7], storage[0x20], words[12], words[13], words[14]);
            after[side] = buf;
            input.Destruct();
        }
        ++ctx.cases;
        if (after[0] != after[1]) ctx.Fail("seed %u: object after: orig %s / ours %s", seed, after[0].c_str(), after[1].c_str());
        if (counts[0] != counts[1] || std::memcmp(sizes[0], sizes[1], sizeof(sizes[0])) != 0)
            ctx.Fail("seed %u: frees differ (%zu vs %zu)", seed, counts[0], counts[1]);
    }
}

// FolderStamp: the volume's answer (GetVolumeInformationA through the game's import slot) and the folder's time.
namespace {
std::string gRoot;
uint32_t gVolume; // 0 fails, 1 NTFS, 2 FAT32, 3 "NTFSX", 4 "ntfs"

BOOL WINAPI FakeVolume(LPCSTR root, LPSTR name, DWORD nameSize, LPDWORD serial, LPDWORD maxComponent, LPDWORD flags, LPSTR fs, DWORD fsSize)
{
    Log("volume %s %p %u %p fsSize %u", root ? root : "(null)", name, nameSize, serial, fsSize);
    if (gVolume == 0) return FALSE;
    *maxComponent = 255;
    *flags = 0x12345;
    const char *const kNames[] = {"", "NTFS", "FAT32", "NTFSX", "ntfs"};
    std::memset(fs, 0x41, fsSize);
    std::strcpy(fs, kNames[gVolume]);
    return TRUE;
}

uint32_t __cdecl FileTimeStand(const cRZString *path, uint32_t which)
{
    Log("filetime '%s' %u", path->mString.c_str(), which);
    return Mix(gSeed, 5) | 1;
}
} // namespace

DIFF_TEST(DirectoryListing_FolderStamp, 0x1f6870)
{
    adt::GlobalOverride<void *> volume(ctx, 0xd9a220, reinterpret_cast<void *>(&FakeVolume));
    shape::StandIn fileTime(0x1c2068u, &PathFileTime, &FileTimeStand);
    auto original = ctx.Original<uint32_t(__cdecl *)(const cRZString *)>(0x1f6870);
    // Absolute paths below a drive's root only: cRZFileName completes a relative one with GetCurrentDirectoryW and asks
    // GetDriveTypeW about a bare root (trapped imports).
    const char *const paths[] = {"C:\\Games\\", "D:/x", "C:\\Games", "\\\\server\\share\\dir\\", "C:\\a\\\\",
                                 "E:\\a folder name that is longer than fifteen characters\\sub/", "c:/lower/case/"};
    constexpr uint32_t kCount = sizeof(paths) / sizeof(paths[0]);
    for (uint32_t seed = 0; seed < 5 * kCount * 3; ++seed) {
        const std::string path = paths[seed % kCount];
        gVolume = (seed / kCount) % 5;
        uint32_t results[2];
        std::string logs[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gLog.clear();
            cRZString folder;
            folder.Construct(path.c_str());
            results[side] = side == 0 ? original(&folder) : DirectoryListing::FolderStamp(&folder);
            logs[side] = gLog + "|" + folder.mString;
            folder.Destruct();
        }
        ++ctx.cases;
        if (results[0] != results[1]) ctx.Fail("seed %u '%s': stamp %08x vs %08x", seed, path.c_str(), results[0], results[1]);
        if (logs[0] != logs[1]) ctx.Fail("seed %u: calls differ %s", seed, FirstDiff(logs[0], logs[1]));
    }
}
