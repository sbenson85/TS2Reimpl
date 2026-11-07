// Differential tests for the cTSPersistSystem functions of lane EW: OpenSegmentByInfo (cITSPersistSystem slot 20) and
// its helper 0x1e5260.
//
// The world: one primary object block (0x300 bytes, rebuilt from the seed before each side) whose segment lock (+0x68)
// and own vtable (slots 27 AddSegment and 47) are recording fakes, a real mNamedMap hash table at +0xf4 (cRZString keys,
// fake segments as values) built from the seed, mFieldF0 (+0xf0) and mFieldB5 (+0xb5) from the seed; the segment-info
// object (slots 3..9) as a fake that hands out game cRZStrings; the cTSAppCommonImpl object (cache 0x10cc040, slot 4:
// the content root option); the COM director static (0x10c9390) whose framework object's GetClassObject hands out the
// created segment fake. No file is touched: GetFileAttributesW (the game's wide-function global 0x10cb5f4),
// FindFirstFileA / FindClose (import slots) and CreateDirectoryW / RemoveDirectoryW (globals 0x10cb620 / 0x10cb624) are
// recording fakes that answer from the path's hash, and the original callees 0x1c2182 (can the file be written),
// 0x1c116e (open-or-create the file), 0x1e7230 (segment set-up, 6.4 KB) and 0x1dab60 (key filters) are recording
// stand-ins in the image for both sides. 0x1c2269 (path prefix test) and 0x1c5031 (DirectoryExists, else
// CreateDirectories) run as they are. Compared: the call log, the return byte, the out pointer and the primary block.
#include "difftest.h"
#include "app/AppDebugger.h"
#include "app_debugger_fakes.h"
#include "rz/cRZString.h"
#include "rz/nGZLog.h"
#include "simantics/ServiceRefVector.h"
#include "ts/cTSPersistSystem.h"
#include "ts/DirectoryListing.h"
#include "code_patch.h"
#include "ts_persist_system_shape.h"

#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <windows.h>

namespace {

using adt::Def;
using adt::gLog;

constexpr uint32_t kCommonCache = 0x10cc040, kDirectorStatic = 0x10c9390, kDirectorGuard = 0x10c93f4;
constexpr uint32_t kGetFileAttributesWGlobal = 0x10cb5f4, kCreateDirectoryWGlobal = 0x10cb620, kRemoveDirectoryWGlobal = 0x10cb624;
constexpr uint32_t kIatFindFirstFileA = 0xd9a1f8, kIatFindClose = 0xd9a1f4;
constexpr uint32_t kSub = 0xa8;

// Overwrites a function's first five bytes with a jump to a stand-in, for the scope of the test.
struct CodePatch {
    uint8_t *site;
    uint8_t saved[5];
    CodePatch(void *at, void *stand) : site(static_cast<uint8_t *>(at))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stand) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    CodePatch(const CodePatch &) = delete;
    CodePatch &operator=(const CodePatch &) = delete;
    ~CodePatch() { std::memcpy(site, saved, 5); }
};

// The code address of one of our member functions (lane FL: the set-up stand-in goes over our SetUpSegment too).
template <typename M> void *MemberCode(M m)
{
    static_assert(sizeof(m) == sizeof(void *), "plain member pointer");
    void *p;
    std::memcpy(&p, &m, sizeof(p));
    return p;
}

uint32_t gSeed = 0;
uint32_t gCalls = 0;
uint8_t gPrimary[0x300];

uint32_t Mix(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    return h;
}

uint32_t TextHash(const std::string &s)
{
    uint32_t h = 2166136261u;
    for (char c : s) h = (h ^ static_cast<uint8_t>(c)) * 16777619u;
    return h;
}

std::string Hex(uint32_t v)
{
    return adt::Hex(v);
}

std::string Narrow(const wchar_t *w)
{
    std::string out;
    for (; *w; ++w) out += static_cast<char>(*w);
    return out;
}

// The answer of the fake file system for a path: the same on both sides for the same seed and path.
bool Answer(const std::string &path, uint32_t salt)
{
    return Mix(gSeed ^ salt, TextHash(path)) % 3 != 0;
}

DWORD WINAPI FakeGetFileAttributesW(const wchar_t *path)
{
    std::string p = Narrow(path);
    gLog.push_back("GetFileAttributesW(" + p + ")");
    return Answer(p, 1) ? FILE_ATTRIBUTE_NORMAL : INVALID_FILE_ATTRIBUTES;
}

HANDLE WINAPI FakeFindFirstFileA(const char *pattern, WIN32_FIND_DATAA *data)
{
    gLog.push_back(std::string("FindFirstFileA(") + pattern + ")");
    if (!Answer(pattern, 2)) return INVALID_HANDLE_VALUE;
    std::memset(data, 0, sizeof(*data));
    data->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
    std::strcpy(data->cFileName, ".");
    return reinterpret_cast<HANDLE>(0x4242);
}

BOOL WINAPI FakeFindClose(HANDLE)
{
    gLog.push_back("FindClose");
    return TRUE;
}

BOOL WINAPI FakeCreateDirectoryW(const wchar_t *path, void *)
{
    std::string p = Narrow(path);
    gLog.push_back("CreateDirectoryW(" + p + ")");
    return Answer(p, 3) ? TRUE : FALSE;
}

BOOL WINAPI FakeRemoveDirectoryW(const wchar_t *path)
{
    gLog.push_back("RemoveDirectoryW(" + Narrow(path) + ")");
    return TRUE;
}

// Stand-ins for the original callees that open files or set segments up.
uint32_t __cdecl WritableStand(const cRZString *path)
{
    ++gCalls;
    gLog.push_back("0x1c2182(S:" + adt::Describe(path) + ")");
    return (Mix(gSeed, 500 + gCalls) & 0xffffff00u) | (Mix(gSeed, 600 + gCalls) % 2);
}
uint32_t __cdecl TouchStand(const cRZString *path, uint32_t truncate)
{
    gLog.push_back("0x1c116e(S:" + adt::Describe(path) + ", " + Hex(truncate & 0xff) + ")");
    return Mix(gSeed, 700);
}
void __fastcall SetUpStand(void *self, void *, const cRZString *path, const cRZString *name, uint32_t a3, uint32_t type, void *segment)
{
    gLog.push_back("0x1e7230(" + std::string(self == gPrimary ? "primary" : "?") + ", S:" + adt::Describe(path) + ", S:" + adt::Describe(name) + ", " +
                   Hex(a3) + ", " + Hex(type) + ", " + adt::Tag(segment) + ")");
}
void __cdecl FiltersStand(void *created)
{
    gLog.push_back("0x1dab60(" + adt::Tag(created) + ")");
}

// The segment fakes' slot 6 (Open): the original passes the second argument's low byte with stack garbage above it at
// 0x1ddef8 (`mov byte ptr [ebp + 0x10], al; push dword ptr [ebp + 0x10]`, the upper bytes are the info pointer's); the
// callee's parameter is a bool, so only that byte is logged.
uint32_t __fastcall SegmentOpen(void *self, void *, uint32_t read, uint32_t write)
{
    gLog.push_back("seg.6(" + adt::Tag(self) + ", " + Hex(read) + ", " + Hex(write & 0xff) + ")");
    return Mix(gSeed, 800);
}

// mNamedMap's nodes as the game lays them out (0x2c bytes).
struct MapNode {
    MapNode *next;
    MapNode *prev;
    cRZString path;
    void *segment;
};
static_assert(sizeof(MapNode) == 0x2c, "map node");
struct MapBucket {
    MapNode *first;
    MapNode *last;
};

struct NamedMap {
    MapNode head{};
    std::vector<std::unique_ptr<MapNode>> nodes;
    MapBucket buckets[8];
    void Build(const std::vector<std::pair<std::string, void *>> &entries)
    {
        head.next = head.prev = &head;
        for (MapBucket &b : buckets) b.first = b.last = &head;
        std::vector<std::vector<MapNode *>> byBucket(8);
        for (const auto &e : entries) {
            nodes.emplace_back(new MapNode());
            MapNode *n = nodes.back().get();
            n->path.Construct(e.first.c_str());
            n->segment = e.second;
            byBucket[nGZLog::HashChars(e.first.c_str()) & 7].push_back(n);
        }
        MapNode *tail = &head;
        for (int b = 0; b < 8; ++b) {
            for (MapNode *n : byBucket[b]) {
                n->prev = tail;
                tail->next = n;
                tail = n;
            }
            if (!byBucket[b].empty()) {
                buckets[b].first = byBucket[b].front();
                buckets[b].last = byBucket[b].back();
            }
        }
        tail->next = &head;
        head.prev = tail;
    }
    ~NamedMap()
    {
        for (auto &n : nodes) n->path.Destruct();
    }
};

std::string Lower(std::string s)
{
    for (char &c : s) {
        if (c == '/') c = '\\';
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

struct SideResult {
    uint32_t ret = 0;
    std::string out;
    std::vector<std::string> log;
    std::vector<uint8_t> primary;
};

const char *const kDirs[] = {"C:\\Sims\\Downloads\\", "C:/Sims/Neighborhoods/N001/", "c:\\sims\\neighborhoods\\N001\\Lots\\", "D:\\Other Place\\",
                             "Relative/Dir", "C:\\Sims\\A Very Long Directory Name Past Sixteen\\", ""};
const char *const kNames[] = {"Gizmo", "N001_Neighborhood", "UPPER", "", "lot.package", "Characters"};
const char *const kRoots[] = {"C:/Sims", "c:/sims/neighborhoods", "D:/", "", "C:/Elsewhere"};
const uint32_t kTypes[] = {0, 1, 2, 3, 3, 4, 1, 0};

cRZString gDir, gName, gRoot;
uint32_t gTypeCalls = 0;

} // namespace

PS_TEST(TSPersistSystemEW_OpenSegmentByInfo, 0x1ddb10)
{
    using F = uint32_t(__thiscall *)(void *, uint32_t, void **, void *);
    auto orig = ctx.Original<F>(0x1ddb10);

    static void *comPtr = nullptr, *createdPtr = nullptr, *segAPtr = nullptr, *segBPtr = nullptr, *packedPtr = nullptr;
    static uint32_t markerWords[4] = {0x5eed0000, 0x5eed0001, 0x5eed0002, 0x5eed0003}; // objects the fakes hand out
    // The primary (kConsole): 27 AddSegment, 47 a value for the new segment's slot 17.
    Def(adt::kConsole, 27, 1, "p", [](void *, const uint32_t *) { return Mix(gSeed, 27); });
    Def(adt::kConsole, 47, 0, "", [](void *, const uint32_t *) { return Mix(gSeed, 47); });
    Def(adt::kLock, 1, 0, "");
    Def(adt::kLock, 2, 0, "");
    // The info object (kStreamBase).
    Def(adt::kStreamBase, 3, 0, "", [](void *, const uint32_t *) { return Mix(gSeed, 3); });
    Def(adt::kStreamBase, 4, 0, "", [](void *, const uint32_t *) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gDir)); });
    Def(adt::kStreamBase, 5, 0, "", [](void *, const uint32_t *) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gName)); });
    Def(adt::kStreamBase, 6, 0, "", [](void *, const uint32_t *) { return kTypes[Mix(gSeed, 60 + gTypeCalls++) % 8]; });
    Def(adt::kStreamBase, 7, 0, "", [](void *, const uint32_t *) { return Mix(gSeed, 7); });
    Def(adt::kStreamBase, 9, 1, "i", [](void *, const uint32_t *args) { return (Mix(gSeed, 90 + args[0]) & 0xffffff00u) | (Mix(gSeed, 95 + args[0]) % 2); });
    // The content root option.
    Def(adt::kCommonImpl, 4, 5, "iSiii", [](void *, const uint32_t *args) {
        reinterpret_cast<cRZString *>(args[1])->mString.assign(gRoot.mString);
        return Mix(gSeed, 4) | 1u;
    });
    // The COM director and the framework object.
    Def(adt::kAdapter, 12, 0, "", [](void *, const uint32_t *) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(comPtr)); });
    Def(adt::kLogService, 3, 3, "iio", [](void *, const uint32_t *args) {
        const uint32_t r = Mix(gSeed, 30) % 4;
        if (r != 0) *reinterpret_cast<void **>(args[2]) = createdPtr; // r == 1: handed out but reported as failing
        return (Mix(gSeed, 31) & 0xffffff00u) | (r >= 2 ? 1u : 0u);
    });
    // The created object (kConsoleService): 0 QueryInterface, 2 Release, 3 its segment.
    Def(adt::kConsoleService, 0, 2, "io", [](void *, const uint32_t *args) {
        void *answer = args[0] == 0x656b8f0c ? (Mix(gSeed, 40) % 3 ? segBPtr : nullptr) : static_cast<void *>(&markerWords[1]);
        *reinterpret_cast<void **>(args[1]) = answer;
        return (Mix(gSeed, 41) & 0xffffff00u) | (answer ? 1u : 0u);
    });
    Def(adt::kConsoleService, 2, 0, "");
    Def(adt::kConsoleService, 3, 0, "", [](void *, const uint32_t *) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(segAPtr)); });
    // Segments (kOther): 0 QueryInterface, 2 Release, 3 IsOpen, 6 Open (replaced below), 17.
    Def(adt::kOther, 0, 2, "io", [](void *self, const uint32_t *args) {
        const uint32_t which = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self)) & 0xff0;
        void *answer = args[0] == 0x286d4a6a ? (Mix(gSeed, 50 + which) % 3 ? packedPtr : nullptr) : static_cast<void *>(&markerWords[2]);
        *reinterpret_cast<void **>(args[1]) = answer;
        return (Mix(gSeed, 51) & 0xffffff00u) | (answer ? 1u : 0u);
    });
    Def(adt::kOther, 2, 0, "");
    Def(adt::kOther, 3, 0, "", [](void *, const uint32_t *) { return (Mix(gSeed, 33) & 0xffffff00u) | (Mix(gSeed, 34) % 3 ? 1u : 0u); });
    Def(adt::kOther, 6, 2, "ii");
    Def(adt::kOther, 17, 1, "i");
    // The packed-file face (kFramework): 2 Release, 26 the group.
    Def(adt::kFramework, 2, 0, "");
    Def(adt::kFramework, 26, 1, "i");

    adt::Fake com(adt::kLogService), created(adt::kConsoleService), commonImpl(adt::kCommonImpl), packed(adt::kFramework);
    std::vector<std::unique_ptr<adt::Fake>> segments;
    for (int i = 0; i < 4; ++i) {
        segments.emplace_back(new adt::Fake(adt::kOther));
        segments.back()->vtable[6] = reinterpret_cast<void *>(&SegmentOpen);
    }
    adt::Fake info(adt::kStreamBase);
    comPtr = com.get();
    createdPtr = created.get();
    segAPtr = segments[0]->get();
    segBPtr = segments[1]->get();
    packedPtr = packed.get();
    auto primaryVTable = adt::BuildVTable(adt::kConsole);
    auto lockVTable = adt::BuildVTable(adt::kLock);
    auto directorVTable = adt::BuildVTable(adt::kAdapter);
    adt::GlobalOverride<void *> commonCache(ctx, kCommonCache, commonImpl.get());
    adt::GlobalOverride<void *> director(ctx, kDirectorStatic, directorVTable.data());
    adt::GlobalOverride<int32_t> directorGuard(ctx, kDirectorGuard, INT32_MIN);
    adt::GlobalOverride<void *> attributes(ctx, kGetFileAttributesWGlobal, reinterpret_cast<void *>(&FakeGetFileAttributesW));
    adt::GlobalOverride<void *> createDir(ctx, kCreateDirectoryWGlobal, reinterpret_cast<void *>(&FakeCreateDirectoryW));
    adt::GlobalOverride<void *> removeDir(ctx, kRemoveDirectoryWGlobal, reinterpret_cast<void *>(&FakeRemoveDirectoryW));
    adt::GlobalOverride<void *> findFirst(ctx, kIatFindFirstFileA, reinterpret_cast<void *>(&FakeFindFirstFileA));
    adt::GlobalOverride<void *> findClose(ctx, kIatFindClose, reinterpret_cast<void *>(&FakeFindClose));
    CodePatch writablePatch(ctx.image.At<uint8_t>(0x1c2182), reinterpret_cast<void *>(&WritableStand));
    CodePatch touchPatch(ctx.image.At<uint8_t>(0x1c116e), reinterpret_cast<void *>(&TouchStand));
    CodePatch setUpPatch(ctx.image.At<uint8_t>(0x1e7230), reinterpret_cast<void *>(&SetUpStand));
    // Since lane FL our OpenSegmentByInfo calls our SetUpSegment, not the image: the same stand-in goes over it.
    CodePatch setUpOursPatch(shape::MemberCode(&cTSPersistSystem::SetUpSegment), reinterpret_cast<void *>(&SetUpStand));
    CodePatch filtersPatch(ctx.image.At<uint8_t>(0x1dab60), reinterpret_cast<void *>(&FiltersStand));
    adt::gFakes[gPrimary] = adt::kConsole;
    adt::gFakes[gPrimary + 0x68] = adt::kLock;
    adt::gFakes[ctx.image.At<uint8_t>(kDirectorStatic)] = adt::kAdapter;
    gDir.Construct();
    gName.Construct();
    gRoot.Construct();

    uint32_t found = 0, made = 0, grouped = 0;
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 rng(seed * 29 + 11);
        const std::string dir = kDirs[rng() % 7], name = kNames[rng() % 6];
        gDir.mString = dir;
        gName.mString = name;
        gRoot.mString = kRoots[rng() % 5];
        const uint32_t f0 = rng() % 2 ? 1u : static_cast<uint32_t>(rng() % 4);
        const uint8_t b5 = static_cast<uint8_t>(rng() % 3 ? rng() % 2 : rng());
        const bool nullInfo = seed % 50 == 7;
        // The table: maybe the key this case looks up (some extension), plus others.
        std::vector<std::pair<std::string, void *>> entries;
        static const char *const kExts[] = {"", ".package", ".files", "\\"};
        if (rng() % 2) entries.push_back({Lower(dir + name + kExts[rng() % 4]), segments[2 + rng() % 2]->get()});
        for (uint32_t n = rng() % 4; n > 0; --n) entries.push_back({Lower(std::string(kDirs[rng() % 7]) + kNames[rng() % 6]) + "x", segments[2]->get()});
        NamedMap map;
        map.Build(entries);
        const uint32_t iid = rng() % 2 ? 0x656b8f0c : rng();

        SideResult sides[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gCalls = 0;
            gTypeCalls = 0;
            adt::ResetSide();
            std::mt19937 fill(seed);
            for (auto &b : gPrimary) b = static_cast<uint8_t>(fill());
            auto put = [](uint32_t off, uint32_t v) { std::memcpy(gPrimary + off, &v, 4); };
            auto word = [](const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); };
            put(0, word(primaryVTable.data()));
            put(0x68, word(lockVTable.data()));
            put(0xf0, f0);
            gPrimary[0xb5] = b5;
            put(0xf8, word(&map.head));
            put(0x100, word(map.buckets));
            put(0x10c, 7);
            void *out = &markerWords[3];
            void *face = gPrimary + kSub;
            void *infoPtr = nullInfo ? nullptr : info.get();
            uint32_t r = side == 0 ? orig(face, iid, &out, infoPtr) : psshape::Call(PSX(OpenSegmentByInfo), face, iid, &out, infoPtr);
            sides[side].ret = r & 0xff;
            sides[side].out = adt::Tag(out);
            sides[side].log = gLog;
            sides[side].primary.assign(gPrimary, gPrimary + sizeof(gPrimary));
        }
        ++ctx.cases;
        for (const std::string &line : sides[0].log) {
            if (line.rfind("0x1c2182(", 0) == 0) ++found; // the reopen path asks whether the file can be written
            if (line.rfind("0x1e7230", 0) == 0) ++made;
            if (line.rfind("fw.26", 0) == 0) ++grouped;
        }
        if (sides[0].ret != sides[1].ret) ctx.Fail("seed %u: original returned 0x%x, ours 0x%x", seed, sides[0].ret, sides[1].ret);
        if (sides[0].out != sides[1].out) ctx.Fail("seed %u: out %s vs %s", seed, sides[0].out.c_str(), sides[1].out.c_str());
        if (sides[0].log != sides[1].log) ctx.Fail("seed %u (%s|%s): calls differ: %s", seed, dir.c_str(), name.c_str(), adt::FirstDifference(sides[0].log, sides[1].log).c_str());
        if (sides[0].primary != sides[1].primary) ctx.Fail("seed %u: primary block differs afterwards", seed);
    }
    if (found < 30 || made < 50 || grouped < 20) ctx.Fail("coverage: %u reopened, %u created, %u groups set", found, made, grouped);
    gDir.Destruct();
    gName.Destruct();
    gRoot.Destruct();
    adt::gFakes.erase(gPrimary);
    adt::gFakes.erase(gPrimary + 0x68);
    adt::gFakes.erase(ctx.image.At<uint8_t>(kDirectorStatic));
    for (int kind : {adt::kConsole, adt::kLock, adt::kStreamBase, adt::kCommonImpl, adt::kAdapter, adt::kLogService, adt::kConsoleService, adt::kOther, adt::kFramework})
        for (auto &d : adt::gSlots[kind]) d = adt::SlotDef();
}

// 0x1e5260 directly: null segments, segments with and without the packed face, groups past 16 bits.
DIFF_TEST(TSPersistSystemEW_SetPackedFileGroup, 0x1e5260)
{
    using F = uint32_t(__cdecl *)(void *, uint32_t);
    auto orig = ctx.Original<F>(0x1e5260);
    static void *packedPtr = nullptr;
    Def(adt::kOther, 0, 2, "io", [](void *, const uint32_t *args) {
        void *answer = Mix(gSeed, 1) % 3 ? packedPtr : nullptr;
        *reinterpret_cast<void **>(args[1]) = answer;
        return Mix(gSeed, 2);
    });
    Def(adt::kFramework, 2, 0, "");
    Def(adt::kFramework, 26, 1, "i");
    adt::Fake segment(adt::kOther), packed(adt::kFramework);
    packedPtr = packed.get();
    for (uint32_t seed = 0; seed < 200; ++seed) {
        const uint32_t group = Mix(seed, 3);
        SideResult sides[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            adt::ResetSide();
            void *seg = seed % 10 == 0 ? nullptr : segment.get();
            uint32_t r = side == 0 ? orig(seg, group) : PersistSetPackedFileGroup(seg, static_cast<uint16_t>(group));
            sides[side].ret = r & 0xff;
            sides[side].log = gLog;
        }
        ++ctx.cases;
        if (sides[0].ret != sides[1].ret) ctx.Fail("seed %u: original returned 0x%x, ours 0x%x", seed, sides[0].ret, sides[1].ret);
        if (sides[0].log != sides[1].log) ctx.Fail("seed %u: calls differ: %s", seed, adt::FirstDifference(sides[0].log, sides[1].log).c_str());
    }
    for (int kind : {adt::kOther, adt::kFramework})
        for (auto &d : adt::gSlots[kind]) d = adt::SlotDef();
}

// ---------------------------------------------------------------------------------------------------------
// CopyResource (cITSPersistSystem slot 37; `this` unused). The world: source and target segments (kOther fakes:
// 0 QueryInterface, 2, 10, 22 open record, 23 create record, 24 commit, 25 abandon), their packed faces (kFramework:
// 2, 3 the database, 27 size, 28 read, 29 write, 41 compressed, 42 flags), the database (kStreamBase: 21), the source
// record (kConsoleService: 2, 6, 8 read, 10 size) and the target record (kCommonImpl: 2, 9 write). Buffers are logged by
// size and a hash of their bytes, never by address (the original's in-place buffer is on its own stack). Sizes cover
// -1, 0, small, 0x7ff, 0x800 and larger (the heap block). Compared: the call log and the return byte.

namespace {

uint32_t BytesHash(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}

const uint32_t kSizes[] = {0xffffffffu, 0, 5, 0x7ff, 0x800, 0x1234, 64};

uint32_t __fastcall PackedFlags(void *self, void *, const uint32_t *key, uint32_t compressed, uint32_t extra)
{
    // 0x1dd630: `push dword ptr [ebp - 0x1c]` after storing only its low byte (al of slot 41); the callee's parameter is a
    // bool, so only that byte is logged.
    gLog.push_back("packed.42(" + adt::Tag(self) + ", key " + Hex(key[0]) + ", " + Hex(compressed & 0xff) + ", " + Hex(extra) + ")");
    return Mix(gSeed, 42);
}

} // namespace

PS_TEST(TSPersistSystemEW_CopyResource, 0x1dd4f0)
{
    using F = uint32_t(__thiscall *)(void *, const void *, void *, const void *, void *);
    auto orig = ctx.Original<F>(0x1dd4f0);
    static void *srcPacked = nullptr, *dstPacked = nullptr, *srcSeg = nullptr, *databasePtr = nullptr, *recordPtr = nullptr, *outputPtr = nullptr;
    static uint32_t answer = 0;
    Def(adt::kOther, 0, 2, "io", [](void *self, const uint32_t *args) {
        const bool isSource = self == srcSeg;
        void *face = args[0] == 0x286d4a6a && ((answer >> (isSource ? 20 : 21)) & 1) ? (isSource ? srcPacked : dstPacked) : nullptr;
        *reinterpret_cast<void **>(args[1]) = face;
        return (Mix(gSeed, 1) & 0xffffff00u) | (face ? 1u : 0u);
    });
    Def(adt::kOther, 2, 0, "");
    Def(adt::kOther, 22, 3, "poi", [](void *, const uint32_t *args) {
        const bool works = (answer >> 22) & 1;
        *reinterpret_cast<void **>(args[1]) = works || ((answer >> 23) & 1) ? recordPtr : nullptr;
        return (Mix(gSeed, 22) & 0xffffff00u) | (works ? 1u : 0u);
    });
    Def(adt::kOther, 23, 2, "po", [](void *, const uint32_t *args) {
        const bool works = (answer >> 24) & 1;
        *reinterpret_cast<void **>(args[1]) = works || ((answer >> 25) & 1) ? outputPtr : nullptr;
        return (Mix(gSeed, 23) & 0xffffff00u) | (works ? 1u : 0u);
    });
    Def(adt::kOther, 24, 1, "p", [](void *, const uint32_t *) { return (Mix(gSeed, 24) & 0xffffff00u) | ((answer >> 26) & 1); });
    Def(adt::kOther, 25, 1, "p");
    Def(adt::kFramework, 2, 0, "");
    Def(adt::kFramework, 3, 0, "", [](void *, const uint32_t *) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(databasePtr)); });
    Def(adt::kFramework, 27, 1, "p", [](void *, const uint32_t *) { return kSizes[Mix(gSeed, 27) % 7]; });
    Def(adt::kFramework, 28, 3, "poo", [](void *, const uint32_t *args) {
        auto *buffer = reinterpret_cast<uint8_t *>(args[1]);
        auto *size = reinterpret_cast<uint32_t *>(args[2]);
        for (uint32_t i = 0; i < *size; ++i) buffer[i] = static_cast<uint8_t>(Mix(gSeed, i));
        gLog.push_back("  read size " + Hex(*size));
        if ((answer >> 27) & 1) *size = *size / 2;
        return (answer >> 28) & 1 ? 0u : Mix(gSeed, 28) | 1u;
    });
    Def(adt::kFramework, 29, 3, "poi", [](void *, const uint32_t *args) {
        gLog.push_back("  wrote " + Hex(BytesHash(reinterpret_cast<const uint8_t *>(args[1]), args[2])));
        return (answer >> 29) & 1 ? 0u : Mix(gSeed, 29) | 1u;
    });
    Def(adt::kFramework, 41, 1, "p", [](void *, const uint32_t *) { return Mix(gSeed, 41); });
    Def(adt::kFramework, 42, 3, "pii");
    Def(adt::kStreamBase, 21, 1, "p", [](void *, const uint32_t *) { return Mix(gSeed, 21); });
    Def(adt::kConsoleService, 2, 0, "");
    Def(adt::kConsoleService, 6, 0, "");
    Def(adt::kConsoleService, 10, 0, "", [](void *, const uint32_t *) { return kSizes[1 + Mix(gSeed, 10) % 6]; });
    Def(adt::kConsoleService, 8, 2, "oi", [](void *, const uint32_t *args) {
        auto *buffer = reinterpret_cast<uint8_t *>(args[0]);
        for (uint32_t i = 0; i < args[1]; ++i) buffer[i] = static_cast<uint8_t>(Mix(gSeed, 7 * i));
        return Mix(gSeed, 8);
    });
    Def(adt::kCommonImpl, 2, 0, "");
    Def(adt::kCommonImpl, 9, 2, "oi", [](void *, const uint32_t *args) {
        gLog.push_back("  record wrote " + Hex(BytesHash(reinterpret_cast<const uint8_t *>(args[0]), args[1])));
        return Mix(gSeed, 9);
    });
    adt::Fake source(adt::kOther), target(adt::kOther), sp(adt::kFramework), dp(adt::kFramework), database(adt::kStreamBase);
    adt::Fake record(adt::kConsoleService), output(adt::kCommonImpl);
    sp.vtable[42] = dp.vtable[42] = reinterpret_cast<void *>(&PackedFlags);
    srcSeg = source.get();
    srcPacked = sp.get();
    dstPacked = dp.get();
    databasePtr = database.get();
    recordPtr = record.get();
    outputPtr = output.get();
    uint32_t packedCopies = 0, recordCopies = 0;
    for (uint32_t seed = 0; seed < 800; ++seed) {
        const uint32_t key[4] = {Mix(seed, 1), Mix(seed, 2), Mix(seed, 3), Mix(seed, 4)};
        const uint32_t newKey[4] = {Mix(seed, 5), Mix(seed, 6), Mix(seed, 7), Mix(seed, 8)};
        SideResult sides[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            answer = Mix(seed, 99);
            if (seed % 3 == 0) answer |= 3u << 20; // both packed faces answer
            adt::ResetSide();
            void *face = gPrimary + kSub;
            uint32_t r = side == 0 ? orig(face, key, source.get(), newKey, target.get())
                                   : psshape::Call(PSX(CopyResource), face, key, source.get(), newKey, target.get());
            sides[side].ret = r & 0xff;
            sides[side].log = gLog;
        }
        ++ctx.cases;
        if (sides[0].ret) ++((answer >> 20 & 3) == 3 ? packedCopies : recordCopies);
        if (sides[0].ret != sides[1].ret) ctx.Fail("seed %u: original returned 0x%x, ours 0x%x", seed, sides[0].ret, sides[1].ret);
        if (sides[0].log != sides[1].log) ctx.Fail("seed %u: calls differ: %s", seed, adt::FirstDifference(sides[0].log, sides[1].log).c_str());
    }
    if (packedCopies < 30 || recordCopies < 30) ctx.Fail("coverage: %u packed copies, %u record copies", packedCopies, recordCopies);
    for (int kind : {adt::kOther, adt::kFramework, adt::kStreamBase, adt::kConsoleService, adt::kCommonImpl})
        for (auto &d : adt::gSlots[kind]) d = adt::SlotDef();
}

// ---------------------------------------------------------------------------------------------------------
// CopySegmentResources (cITSPersistSystem slot 38). `this` is a fake ITS face (kConsoleFace: 37 CopyResource, logged with
// the key and mapped key by value since the key vector's address differs by side); source and target segments (kOther:
// 0 QueryInterface for 0x3b2271bb, 2, 10, 19 the key list, allocated as a service block), their segment faces
// (kStreamBase: 2, 4 the group, path-tagged 0x6f... or not), the mapper (kConsoleService: 3) and the filter (a fake passed
// through). Compared: the call log (allocator fake included) and the return byte.

namespace {

adt::AllocatorFake *gAlloc = nullptr;
uint32_t gAnswer = 0;
void *gSourceSeg = nullptr, *gSourceFace = nullptr, *gTargetFace = nullptr;

std::string KeyText(const uint32_t *k)
{
    return Hex(k[0]) + "/" + Hex(k[1]) + "/" + Hex(k[2]) + "/" + Hex(k[3]);
}

uint32_t __fastcall CopyStand(void *self, void *, const uint32_t *key, void *source, const uint32_t *mapped, void *target)
{
    ++gCalls;
    gLog.push_back("copy(" + std::string(self == gPrimary + kSub ? "its" : "?") + ", " + KeyText(key) + ", " + adt::Tag(source) + ", " + KeyText(mapped) +
                   ", " + adt::Tag(target) + ")");
    return (Mix(gSeed, 370 + gCalls) & 0xffffff00u) | (Mix(gSeed, 380 + gCalls) % 7 ? 1u : 0u);
}

} // namespace

PS_TEST(TSPersistSystemEW_CopySegmentResources, 0x1dd7a0)
{
    using F = uint32_t(__thiscall *)(void *, void *, void *, void *, void *);
    auto orig = ctx.Original<F>(0x1dd7a0);
    adt::AllocatorFake alloc(ctx);
    gAlloc = &alloc;
    Def(adt::kOther, 0, 2, "io", [](void *self, const uint32_t *args) {
        const bool isSource = self == gSourceSeg;
        void *face = args[0] == 0x3b2271bb && ((gAnswer >> (isSource ? 1 : 2)) & 1) ? (isSource ? gSourceFace : gTargetFace) : nullptr;
        *reinterpret_cast<void **>(args[1]) = face;
        return (Mix(gSeed, 3) & 0xffffff00u) | (face ? 1u : 0u);
    });
    Def(adt::kOther, 2, 0, "");
    Def(adt::kOther, 10, 0, "");
    Def(adt::kOther, 19, 2, "op", [](void *, const uint32_t *args) {
        auto *vec = reinterpret_cast<ServiceBlockVector *>(args[0]);
        if ((gAnswer >> 3) & 1) return 0u; // no list
        const uint32_t n = Mix(gSeed, 4) % 5;
        auto *keys = static_cast<uint32_t *>(::operator new((n + 1) * 16));
        gAlloc->Track(keys);
        const uint32_t sourceGroup = (gAnswer >> 4) & 1 ? 0x6f000123u : 0x00000123u;
        for (uint32_t i = 0; i < n * 4; ++i) keys[i] = Mix(gSeed, 10 + i);
        for (uint32_t i = 0; i < n; ++i)
            if (Mix(gSeed, 30 + i) % 2) keys[i * 4 + 1] = sourceGroup;
        vec->mFirst = keys;
        vec->mLast = keys + n * 4;
        vec->mEnd = keys + (n + 1) * 4;
        return Mix(gSeed, 5) | 1u;
    });
    Def(adt::kStreamBase, 2, 0, "");
    Def(adt::kStreamBase, 4, 0, "", [](void *self, const uint32_t *) {
        if (self == gSourceFace) return (gAnswer >> 4) & 1 ? 0x6f000123u : 0x00000123u;
        return (gAnswer >> 5) & 1 ? 0x6f000456u : 0x00000456u;
    });
    Def(adt::kConsoleService, 3, 2, "oo", [](void *, const uint32_t *args) {
        const auto *key = reinterpret_cast<const uint32_t *>(args[0]);
        auto *out = reinterpret_cast<uint32_t *>(args[1]);
        gLog.push_back("  map " + KeyText(key) + " over " + KeyText(out));
        for (int i = 0; i < 4; ++i) out[i] = key[i] ^ Mix(gSeed, 50 + i);
        return 0u;
    });
    Def(adt::kConsoleFace, 37, 4, "oooo");
    adt::Fake source(adt::kOther), target(adt::kOther), sourceFace(adt::kStreamBase), targetFace(adt::kStreamBase), mapper(adt::kConsoleService),
        filter(adt::kCommonImpl);
    gSourceSeg = source.get();
    gSourceFace = sourceFace.get();
    gTargetFace = targetFace.get();
    auto itsVTable = adt::BuildVTable(adt::kConsoleFace);
    itsVTable[37] = reinterpret_cast<void *>(&CopyStand);
    adt::gFakes[gPrimary + kSub] = adt::kConsoleFace;
    uint32_t copiedAll = 0, mapped = 0, tagged = 0;
    for (uint32_t seed = 0; seed < 500; ++seed) {
        SideResult sides[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gCalls = 0;
            gAnswer = Mix(seed, 77);
            adt::ResetSide();
            const uint32_t v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(itsVTable.data()));
            std::memcpy(gPrimary + kSub, &v, 4);
            void *face = gPrimary + kSub;
            void *map = (gAnswer >> 6) & 1 ? mapper.get() : nullptr;
            void *filt = (gAnswer >> 7) & 1 ? filter.get() : nullptr;
            uint32_t r = side == 0 ? orig(face, target.get(), source.get(), map, filt)
                                   : psshape::Call(PSX(CopySegmentResources), face, target.get(), source.get(), map, filt);
            sides[side].ret = r & 0xff;
            sides[side].log = gLog;
            alloc.Release();
        }
        ++ctx.cases;
        if (sides[0].ret && sides[0].log.size() > 6) ++copiedAll;
        if ((gAnswer >> 6) & 1) ++mapped;
        if ((gAnswer & 0x36) == 0x36) ++tagged;
        if (sides[0].ret != sides[1].ret) ctx.Fail("seed %u: original returned 0x%x, ours 0x%x", seed, sides[0].ret, sides[1].ret);
        if (sides[0].log != sides[1].log) ctx.Fail("seed %u: calls differ: %s", seed, adt::FirstDifference(sides[0].log, sides[1].log).c_str());
    }
    if (copiedAll < 40 || mapped < 100 || tagged < 20) ctx.Fail("coverage: %u full copies, %u mapped, %u tagged", copiedAll, mapped, tagged);
    gAlloc = nullptr;
    adt::gFakes.erase(gPrimary + kSub);
    for (int kind : {adt::kOther, adt::kStreamBase, adt::kConsoleService, adt::kConsoleFace})
        for (auto &d : adt::gSlots[kind]) d = adt::SlotDef();
}

// ---------------------------------------------------------------------------------------------------------
// IsSegmentWritable (cITSPersistSystem slot 28). The segment (kOther: 0 QueryInterface, 2) answers the packed-file query
// (kFramework face: 2, 3 its database, 8 GetPath) or the directory query (kCommonImpl face: 2, 8 GetPath) or neither; the
// database (kStreamBase: 8). The directory listing's original methods 0x1f58f0 (constructor), 0x1f6dd0 (first), 0x1f64f0
// (the entry's path into a cRZString), 0x1f65c0 (kind), 0x1f61e0 (next) and 0x1f6090 (destructor) and 0x1c2182 (can the
// file be written) are recording stand-ins; the listing keeps its entry count and position in its own bytes. No file is
// touched. Compared: the call log and the return byte.

namespace {

struct ListingState {
    uint32_t count;
    uint32_t at;
};

void *__fastcall ListingCtor(void *self, void *, const cRZString *path, void *primary)
{
    auto *s = static_cast<ListingState *>(self);
    s->count = Mix(gSeed, 900) % 5;
    s->at = 0;
    gLog.push_back("listing(S:" + adt::Describe(path) + ", " + (primary == gPrimary ? "primary" : "?") + ") " + Hex(s->count));
    return self;
}
uint32_t __fastcall ListingFirst(void *self, void *)
{
    auto *s = static_cast<ListingState *>(self);
    s->at = 0;
    gLog.push_back("first");
    return (Mix(gSeed, 901) & 0xffffff00u) | (s->count ? 1u : 0u);
}
void *__fastcall ListingPath(void *self, void *, cRZString *out)
{
    auto *s = static_cast<ListingState *>(self);
    out->Construct(("C:\\Dir\\file" + std::to_string(s->at) + (Mix(gSeed, 902 + s->at) % 3 ? "" : "_with_a_long_name.package")).c_str());
    gLog.push_back("path " + std::to_string(s->at));
    return out;
}
uint32_t __fastcall ListingKind(void *self, void *)
{
    auto *s = static_cast<ListingState *>(self);
    const uint32_t r = Mix(gSeed, 910 + s->at);
    return (r & 0xfffffff0u) | (r % 3 == 0 ? 4u : r % 16);
}
uint32_t __fastcall ListingNext(void *self, void *)
{
    auto *s = static_cast<ListingState *>(self);
    ++s->at;
    gLog.push_back("next");
    return (Mix(gSeed, 920) & 0xffffff00u) | (s->at < s->count ? 1u : 0u);
}
void __fastcall ListingDtor(void *, void *)
{
    gLog.push_back("~listing");
}
uint32_t __cdecl WritableStandByName(const cRZString *path)
{
    gLog.push_back("0x1c2182(S:" + adt::Describe(path) + ")");
    return (Mix(gSeed, 930 + TextHash(path->mString)) & 0xffffff00u) | (Mix(gSeed, 940 + TextHash(path->mString)) % 4 == 0 ? 1u : 0u);
}

} // namespace

PS_TEST(TSPersistSystemEW_IsSegmentWritable, 0x1e1d70)
{
    using F = uint32_t(__thiscall *)(void *, void *);
    auto orig = ctx.Original<F>(0x1e1d70);
    static void *packedPtr = nullptr, *directoryPtr = nullptr, *databasePtr = nullptr;
    Def(adt::kOther, 0, 2, "io", [](void *, const uint32_t *args) {
        const uint32_t which = Mix(gSeed, args[0] == 0x286d4a6a ? 1 : 2) % 3;
        void *face = which == 0 ? nullptr : args[0] == 0x286d4a6a ? packedPtr : args[0] == 0x856c8cf7 ? directoryPtr : nullptr;
        *reinterpret_cast<void **>(args[1]) = which == 2 || face ? face : nullptr;
        return (Mix(gSeed, 3) & 0xffffff00u) | (face && which == 1 ? 1u : 0u); // which 2: handed out but not answered
    });
    Def(adt::kOther, 2, 0, "");
    auto getPath = [](void *, const uint32_t *args) {
        reinterpret_cast<cRZString *>(args[0])->mString = Mix(gSeed, 5) % 2 ? "C:\\Sims\\Downloads\\x.package" : "D:/A Longer Path/To Somewhere Else/";
        return 0u;
    };
    Def(adt::kFramework, 2, 0, "");
    Def(adt::kFramework, 3, 0, "", [](void *, const uint32_t *) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(databasePtr)); });
    Def(adt::kFramework, 8, 1, "S", getPath);
    Def(adt::kCommonImpl, 2, 0, "");
    Def(adt::kCommonImpl, 8, 1, "S", getPath);
    Def(adt::kStreamBase, 8, 0, "", [](void *, const uint32_t *) { return (Mix(gSeed, 6) & 0xffffff00u) | (Mix(gSeed, 7) % 3 == 0 ? 1u : 0u); });
    adt::Fake segment(adt::kOther), packed(adt::kFramework), directory(adt::kCommonImpl), database(adt::kStreamBase);
    packedPtr = packed.get();
    directoryPtr = directory.get();
    databasePtr = database.get();
    // The listing is ours now (DirectoryListing): stand in for the image's methods and ours.
    shape::StandIn ctorPatch(0x1f58f0, &DirectoryListingAdapters::Construct, &ListingCtor);
    shape::StandIn firstPatch(0x1f6dd0, &DirectoryListing::First, &ListingFirst);
    shape::StandIn pathPatch(0x1f64f0, &DirectoryListing::EntryPath, &ListingPath);
    shape::StandIn kindPatch(0x1f65c0, &DirectoryListing::EntryKind, &ListingKind);
    shape::StandIn nextPatch(0x1f61e0, &DirectoryListing::Next, &ListingNext);
    shape::StandIn dtorPatch(0x1f6090, &DirectoryListingAdapters::Destruct, &ListingDtor);
    CodePatch writablePatch(ctx.image.At<uint8_t>(0x1c2182), reinterpret_cast<void *>(&WritableStandByName));
    uint32_t listed = 0, packedSeen = 0;
    for (uint32_t seed = 0; seed < 600; ++seed) {
        SideResult sides[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            adt::ResetSide();
            void *face = gPrimary + kSub;
            uint32_t r = side == 0 ? orig(face, segment.get()) : psshape::Call(PSX(IsSegmentWritable), face, segment.get());
            sides[side].ret = r & 0xff;
            sides[side].log = gLog;
        }
        ++ctx.cases;
        for (const std::string &line : sides[0].log) {
            if (line == "next") ++listed;
            if (line.rfind("fw.3", 0) == 0) ++packedSeen;
        }
        if (sides[0].ret != sides[1].ret) ctx.Fail("seed %u: original returned 0x%x, ours 0x%x", seed, sides[0].ret, sides[1].ret);
        if (sides[0].log != sides[1].log) ctx.Fail("seed %u: calls differ: %s", seed, adt::FirstDifference(sides[0].log, sides[1].log).c_str());
    }
    if (listed < 100 || packedSeen < 50) ctx.Fail("coverage: %u listing steps, %u packed files", listed, packedSeen);
    for (int kind : {adt::kOther, adt::kFramework, adt::kCommonImpl, adt::kStreamBase})
        for (auto &d : adt::gSlots[kind]) d = adt::SlotDef();
}
