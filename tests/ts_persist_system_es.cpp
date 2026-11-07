// Differential tests for the cTSPersistSystem functions of lane ES: OpenContentPaths (cITSPersistSystem slot 43).
//
// The world: one primary object block (0x300 bytes, the same buffer for both sides, rebuilt from the seed before each
// side) whose ITS face (+0xa8) and segment lock (+0x68) are recording fakes, a category map at +0x23c (a real
// std::map<int, uint32_t> built from the seed), the cTSAppCommonImpl object (cache 0x10cc040: slots 4, 5 and 6), the
// framework's service object (0x10c9378: slot 21 gives a command line whose slot 9 answers switches), the allocator
// service (cache 0x10c95d4) the path list's nodes come from, and GetCurrentThreadId's import slot. The original callees
// 0x1e3c70 (opens one content directory), 0x1ea760 and 0x1e9eb0 are replaced by recording stand-ins in the image for
// both sides. Every call is logged with its arguments (strings by text, the category by its value, list nodes and
// blocks by number); the logs, the return byte and the primary block afterwards must match. No file is touched: the
// directory work all happens in 0x1e3c70, which is the stand-in here.
#include "difftest.h"
#include "app/AppDebugger.h"
#include "app_debugger_fakes.h"
#include "code_patch.h"
#include "gz/cGZPersistCacheStrategyBasic.h"
#include "rz/cRZStopwatch.h"
#include "rz/cRZString.h"
#include "simantics/ServiceRefVector.h"
#include "ts/cTSPersistSystem.h"
#include "ts_persist_system_shape.h"

#include <cstdio>
#include <memory>
#include <cstring>
#include <list>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include <windows.h>

namespace {

using adt::Def;
using adt::gLog;

constexpr uint32_t kFrameworkGlobal = 0x10c9378, kCommonCache = 0x10cc040, kIatGetCurrentThreadId = 0xd9a194;
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

// The code address of one of our member functions (lane FQ: the 0x1e3c70 stand-ins go over our OpenContentFolder too).
template <typename M> void *MemberCode(M m)
{
    static_assert(sizeof(m) == sizeof(void *), "plain member pointer");
    void *p;
    std::memcpy(&p, &m, sizeof(p));
    return p;
}

struct ListNode {
    ListNode *next;
    ListNode *prev;
    std::string text;
};
struct ListHead {
    ListNode *head;
    uint32_t size;
};

uint32_t gSeed = 0;
uint32_t gCalls = 0;
uint32_t gThreadId = 0;
uint8_t gPrimary[0x300];

uint32_t Mix(uint32_t a, uint32_t b)
{
    uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    return h;
}

std::string Hex(uint32_t v)
{
    return adt::Hex(v);
}

DWORD WINAPI FakeThreadId()
{
    return gThreadId;
}

// 0x1e3c70: thiscall on the primary (mode, path, flag, category, a5, a6), ret 0x18.
uint32_t __fastcall AddPathStand(void *self, void *, uint32_t mode, const cRZString *path, uint32_t flag, const uint32_t *category, uint32_t a5, uint32_t a6)
{
    ++gCalls;
    gLog.push_back("add(" + std::string(self == gPrimary ? "primary" : "?") + ", " + Hex(mode) + ", S:" + adt::Describe(path) + ", " + Hex(flag) + ", " +
                   (category ? "cat:" + Hex(*category) : std::string("null")) + ", " + Hex(a5) + ", " + Hex(a6) + ")");
    static const uint32_t answers[] = {1, 1, 1, 1, 3, 0, 2, 0xff, 1, 0x101};
    return (Mix(gSeed, gCalls) & 0xffffff00u) | answers[Mix(gSeed ^ 0x55, gCalls) % 10];
}
void __fastcall BeforeStand(void *self, void *)
{
    gLog.push_back(std::string("0x1ea760(") + (self == gPrimary ? "primary" : "?") + ")");
}
void __fastcall AfterStand(void *self, void *)
{
    gLog.push_back(std::string("0x1e9eb0(") + (self == gPrimary ? "primary" : "?") + ")");
}

std::string PathText(uint32_t pathId, uint32_t i)
{
    char buf[64];
    if (Mix(gSeed, pathId + i) % 5 == 0)
        std::snprintf(buf, sizeof(buf), "C:/Long/Content/Dir_%08x_%u/", pathId, i); // past the small-string size
    else
        std::snprintf(buf, sizeof(buf), "d%02x_%u", pathId & 0xff, i);
    return buf;
}

// The category map's values (std::map<int, uint32_t>: key at node+0x10, value at node+0x14).
std::map<int, uint32_t> *gCategories = nullptr;

void BuildPrimary()
{
    std::mt19937 rng(gSeed);
    for (auto &b : gPrimary) b = static_cast<uint8_t>(rng());
}

void DefineFakes()
{
    using adt::kCommonImpl;
    // Slot 4: one path into the cRZString.
    Def(kCommonImpl, 4, 5, "iSiii", [](void *, const uint32_t *args) {
        reinterpret_cast<cRZString *>(args[1])->mString.assign(PathText(args[0], 99));
        return Mix(gSeed, args[0]) | 1u;
    });
    // Slot 5: appends 0..3 directories (the count depends on the id, which and mask and on how often it was asked).
    Def(kCommonImpl, 5, 5, "iqiii", [](void *, const uint32_t *args) {
        ++gCalls;
        auto *list = reinterpret_cast<ListHead *>(args[1]);
        uint32_t n = Mix(gSeed ^ args[0], args[3] * 7 + args[4] + gCalls) % 4;
        for (uint32_t i = 0; i < n; ++i) {
            auto *node = static_cast<ListNode *>(appdebugger::AllocateListNodes(1, 0));
            new (&node->text) std::string(PathText(args[0] ^ args[4], gCalls * 4 + i));
            node->next = list->head;
            node->prev = list->head->prev;
            list->head->prev->next = node;
            list->head->prev = node;
            ++list->size;
            gLog.push_back("  entry " + node->text);
        }
        return 0u;
    });
    // Slot 6: whether the id has a list.
    Def(kCommonImpl, 6, 1, "i", [](void *, const uint32_t *args) { return (Mix(gSeed, args[0]) & 0xffffff00u) | (Mix(gSeed, args[0] ^ 3) % 3 == 0 ? 0u : 1u); });
    // Framework service object slot 21: the command line; its slot 9 answers a switch.
    Def(adt::kFramework, 21, 0, "");
    Def(adt::kCommandLine, 9, 1, "S", [](void *, const uint32_t *args) {
        const std::string &s = reinterpret_cast<const cRZString *>(args[0])->mString;
        uint32_t r = Mix(gSeed, static_cast<uint32_t>(s.size()));
        return (r & 0xffffff00u) | (r % 4 == 0 ? 1u : 0u);
    });
    // The ITS face (kind kConsoleFace) and the lock (kind kLock).
    Def(adt::kConsoleFace, 39, 0, "");
    Def(adt::kConsoleFace, 44, 2, "is", [](void *, const uint32_t *) { return 0xabcdef01u; });
    Def(adt::kLock, 1, 0, "");
    Def(adt::kLock, 2, 0, "", [](void *, const uint32_t *) { return 0x12345678u; });
}

struct SideResult {
    uint32_t ret = 0;
    std::vector<std::string> log;
    std::vector<uint8_t> primary;
};

} // namespace

PS_TEST(TSPersistSystemES_OpenContentPaths, 0x1dae90)
{
    using F = uint32_t(__thiscall *)(void *, uint32_t);
    auto orig = ctx.Original<F>(0x1dae90);

    DefineFakes();
    adt::AllocatorFake alloc(ctx);
    adt::Fake commonImpl(adt::kCommonImpl), framework(adt::kFramework), commandLine(adt::kCommandLine);
    void *commandLinePtr = commandLine.get();
    Def(adt::kFramework, 21, 0, "", [commandLinePtr](void *, const uint32_t *) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(commandLinePtr)); });
    adt::GlobalOverride<void *> commonCache(ctx, kCommonCache, commonImpl.get());
    adt::GlobalOverride<void *> frameworkGlobal(ctx, kFrameworkGlobal, framework.get());
    adt::GlobalOverride<void *> threadIat(ctx, kIatGetCurrentThreadId, reinterpret_cast<void *>(&FakeThreadId));
    auto itsVTable = adt::BuildVTable(adt::kConsoleFace);
    auto lockVTable = adt::BuildVTable(adt::kLock);
    CodePatch addPatch(ctx.image.At<uint8_t>(0x1e3c70), reinterpret_cast<void *>(&AddPathStand));
    CodePatch addOursPatch(shape::MemberCode(&cTSPersistSystem::OpenContentFolder), reinterpret_cast<void *>(&AddPathStand));
    CodePatch beforePatch(ctx.image.At<uint8_t>(0x1ea760), reinterpret_cast<void *>(&BeforeStand));
    CodePatch afterPatch(ctx.image.At<uint8_t>(0x1e9eb0), reinterpret_cast<void *>(&AfterStand));
    CodePatch beforeOurs(shape::MemberCode(&cTSPersistSystem::ParkOpenSegments), reinterpret_cast<void *>(&BeforeStand));
    CodePatch afterOurs(shape::MemberCode(&cTSPersistSystem::RelistParkedSegments), reinterpret_cast<void *>(&AfterStand));
    adt::gFakes[gPrimary + kSub] = adt::kConsoleFace;
    adt::gFakes[gPrimary + 0x68] = adt::kLock;

    static const uint32_t kMasks[] = {0xf, 0xf0, 0xf00, 0xf000, 0xf0000, 0xf00000};
    for (uint32_t seed = 0; seed < 400; ++seed) {
        std::mt19937 rng(seed * 31 + 7);
        // The categories present: each standard mask half the time, plus some others.
        std::map<int, uint32_t> categories;
        for (uint32_t m : kMasks)
            if (rng() % 2) categories[static_cast<int>(m)] = 0xc0000000u | (m >> 4) | (rng() & 0xff00);
        if (rng() % 2) categories[0x1] = 0x11;
        if (rng() % 2) categories[0xff] = 0xff;
        if (rng() % 2) categories[0x7fffffff] = 0x7f;
        uint32_t mask = 0;
        switch (seed % 5) {
        case 0: mask = static_cast<uint32_t>(rng()); break;
        case 1: mask = 0xffffffffu; break;
        case 2: mask = (rng() % 2 ? 0xfu : (1u << (rng() % 4))) | (static_cast<uint32_t>(rng()) & 0xfffff0u); break;
        case 3: mask = kMasks[rng() % 6] & static_cast<uint32_t>(rng() | 0x111111u); break;
        default: mask = static_cast<uint32_t>(rng()) & 0xff00000fu; break;
        }
        if (seed == 1) mask = 0;
        const bool owner = rng() % 2;

        SideResult sides[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gCalls = 0;
            gThreadId = 0x4400 + seed;
            adt::ResetSide();
            BuildPrimary();
            std::memcpy(gPrimary + 0x00, "\0\0\0\0", 4); // the primary vtable is not used
            uint32_t v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(itsVTable.data()));
            std::memcpy(gPrimary + kSub, &v, 4);
            v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lockVTable.data()));
            std::memcpy(gPrimary + 0x68, &v, 4);
            // The map's head: a copy of the std::map's head pointer and size (the nodes are only read).
            std::memcpy(gPrimary + 0x23c, &categories, 8);
            v = owner ? gThreadId : gThreadId + 1;
            std::memcpy(gPrimary + 0x244, &v, 4);
            void *face = gPrimary + kSub;
            uint32_t r = side == 0 ? orig(face, mask) : psshape::Call(PSX(OpenContentPaths), face, mask);
            sides[side].ret = r & 0xff;
            sides[side].log = gLog;
            sides[side].primary.assign(gPrimary, gPrimary + sizeof(gPrimary));
            alloc.Release();
        }
        ++ctx.cases;
        if (sides[0].ret != sides[1].ret) ctx.Fail("seed %u mask 0x%x: original returned 0x%x, ours 0x%x", seed, mask, sides[0].ret, sides[1].ret);
        if (sides[0].log != sides[1].log)
            ctx.Fail("seed %u mask 0x%x: calls differ: %s", seed, mask, adt::FirstDifference(sides[0].log, sides[1].log).c_str());
        if (sides[0].primary != sides[1].primary) {
            size_t i = 0;
            while (sides[0].primary[i] == sides[1].primary[i]) ++i;
            ctx.Fail("seed %u mask 0x%x: primary byte +0x%zx differs afterwards", seed, mask, i);
        }
        if (seed == 2 && sides[0].log.size() < 10) ctx.Fail("seed 2: only %zu calls logged (the fakes are not reached)", sides[0].log.size());
    }
    adt::gFakes.erase(gPrimary + kSub);
    adt::gFakes.erase(gPrimary + 0x68);
    for (int kind : {adt::kCommonImpl, adt::kFramework, adt::kCommandLine, adt::kConsoleFace, adt::kLock})
        for (auto &d : adt::gSlots[kind]) d = adt::SlotDef();
}

// ---------------------------------------------------------------------------------------------------------
// The service Init (cIGZSystemService slot 8, this = primary + 4).
//
// Kinds: the primary (+0) is kConsole (slots 20 RegisterFactory, 41 AddCacheStrategy), the ITS face kConsoleFace (19),
// GetServiceObject's object kFramework (21; also 7 and 9 when it becomes the cached service), the cached service
// (0x10f5194) kConsoleService (7, 9), the COM director static (0x10c9390) kAdapter (12), the framework COM object
// kLogService (3 GetClassObject), objects it hands out and the fields' previous objects kStreamBase (1, 2, 3), the cache
// strategies made by the constructor stand-ins kOther (1, 2, 5, 20, 21). The Version.sig probe goes to the harness's
// sandbox through the game's wide file functions: one folder there has the file, one does not, others do not exist.
// Stand-ins: 0x1f3b70 (a global set-up), the two cache constructors 0x1fb300 / 0x9b35f0 (on both sides: ours is
// cGZPersistCacheStrategyBasic::Construct) and the path-group table's
// try_emplace 0x1d7390 (its nodes are a scratch array whose group words are compared). The stopwatch reads a fake
// performance counter (the import slot for the original, the test override for ours).

namespace {

constexpr uint32_t kCachedServiceGlobal = 0x10f5194, kDirectorStatic = 0x10c9390, kDirectorGuard = 0x10c93f4, kIatQpc = 0xd9a24c;

struct PathGroupNodeFake {
    uint8_t pad[0x28];
    uint32_t group;
};
PathGroupNodeFake gGroupNodes[32];
uint32_t gGroupNext = 0;
std::vector<std::string> gDirs;
void *gCacheVTable = nullptr;

struct InsertResult {
    void *node;
    uint8_t inserted;
};
InsertResult *__fastcall EmplaceStand(void *table, void *, InsertResult *out, const cRZString *key)
{
    gLog.push_back("emplace(" + std::string(table == gPrimary + 0x1e8 ? "groups" : "?") + ", S:" + adt::Describe(key) + ")");
    out->node = &gGroupNodes[gGroupNext++ % 32];
    out->inserted = 1;
    return out;
}
void __cdecl SetupStand()
{
    gLog.push_back("0x1f3b70()");
}
std::vector<void *> gCreated; // the cache objects the constructors made (heap addresses differ between the sides)
void *__fastcall CacheCtor78(void *self, void *)
{
    gCreated.push_back(self);
    gLog.push_back("ctor78");
    *static_cast<void **>(self) = gCacheVTable;
    return self;
}
void *__fastcall CacheCtor34(void *self, void *)
{
    gCreated.push_back(self);
    gLog.push_back("ctor34");
    *static_cast<void **>(self) = gCacheVTable;
    return self;
}
BOOL WINAPI FakeQpc(LARGE_INTEGER *out)
{
    out->QuadPart = 0x123456789abcLL;
    return TRUE;
}
void FakeQpcOverride(uint64_t *out)
{
    *out = 0x123456789abcULL;
}

std::string Narrow(const std::wstring &w)
{
    return std::string(w.begin(), w.end());
}

} // namespace

PS_TEST(TSPersistSystemES_Init, 0x1e0d60)
{
    using F = uint32_t(__thiscall *)(void *);
    auto orig = ctx.Original<F>(0x1e0d60);
    if (difftest::SandboxDir().empty()) {
        ctx.Fail("no sandbox directory");
        return;
    }
    CreateDirectoryW(difftest::SandboxPath(L"esA").c_str(), nullptr);
    CreateDirectoryW(difftest::SandboxPath(L"esB").c_str(), nullptr);
    {
        HANDLE h = CreateFileW(difftest::SandboxPath(L"esA\\Version.sig").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    const std::string dirWith = Narrow(difftest::SandboxPath(L"esA\\")), dirWithout = Narrow(difftest::SandboxPath(L"esB\\"));
    const std::string dirMissing = Narrow(difftest::SandboxPath(L"esMissing\\"));

    adt::AllocatorFake alloc(ctx);
    static void *comPtr = nullptr;
    static void *handedPtr[3] = {};
    static uint32_t created = 0;

    Def(adt::kConsole, 20, 3, "iip", [](void *, const uint32_t *) { return 1u; });
    Def(adt::kConsole, 41, 1, "p", [](void *, const uint32_t *) { return 1u; });
    Def(adt::kConsoleFace, 19, 1, "i");
    Def(adt::kFramework, 21, 0, "");
    Def(adt::kFramework, 7, 1, "p");
    Def(adt::kFramework, 9, 1, "p");
    Def(adt::kConsoleService, 7, 1, "p");
    Def(adt::kConsoleService, 9, 1, "p");
    Def(adt::kAdapter, 12, 0, "", [](void *, const uint32_t *) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(comPtr)); });
    Def(adt::kLogService, 3, 3, "iio", [](void *, const uint32_t *args) {
        *reinterpret_cast<void **>(args[2]) = handedPtr[created++ % 3];
        return 1u;
    });
    for (int s : {1, 2, 3}) Def(adt::kStreamBase, s, 0, "");
    Def(adt::kOther, 1, 0, "");
    Def(adt::kOther, 2, 0, "");
    Def(adt::kOther, 5, 1, "p");
    Def(adt::kOther, 20, 1, "i");
    Def(adt::kOther, 21, 1, "i");
    Def(adt::kCommonImpl, 5, 5, "iqiii", [](void *, const uint32_t *args) {
        auto *list = reinterpret_cast<ListHead *>(args[1]);
        for (const std::string &dir : gDirs) {
            auto *node = static_cast<ListNode *>(appdebugger::AllocateListNodes(1, 0));
            new (&node->text) std::string(dir);
            node->next = list->head;
            node->prev = list->head->prev;
            list->head->prev->next = node;
            list->head->prev = node;
            ++list->size;
        }
        return 0u;
    });
    // (fakes build their vtables from the slots defined so far)
    adt::Fake framework(adt::kFramework), cachedService(adt::kConsoleService), com(adt::kLogService), commonImpl(adt::kCommonImpl);
    adt::Fake handed[3] = {adt::Fake(adt::kStreamBase), adt::Fake(adt::kStreamBase), adt::Fake(adt::kStreamBase)};
    adt::Fake previous[4] = {adt::Fake(adt::kStreamBase), adt::Fake(adt::kStreamBase), adt::Fake(adt::kStreamBase), adt::Fake(adt::kStreamBase)};
    comPtr = com.get();
    for (int i = 0; i < 3; ++i) handedPtr[i] = handed[i].get();
    auto primaryVTable = adt::BuildVTable(adt::kConsole);
    auto itsVTable = adt::BuildVTable(adt::kConsoleFace);
    auto cacheVTable = adt::BuildVTable(adt::kOther);
    gCacheVTable = cacheVTable.data();
    auto directorVTable = adt::BuildVTable(adt::kAdapter);

    adt::GlobalOverride<void *> commonCache(ctx, kCommonCache, commonImpl.get());
    adt::GlobalOverride<void *> frameworkGlobal(ctx, kFrameworkGlobal, framework.get());
    adt::GlobalOverride<void *> serviceGlobal(ctx, kCachedServiceGlobal);
    adt::GlobalOverride<void *> director(ctx, kDirectorStatic, directorVTable.data());
    adt::GlobalOverride<int32_t> directorGuard(ctx, kDirectorGuard, INT32_MIN);
    adt::GlobalOverride<void *> qpc(ctx, kIatQpc, reinterpret_cast<void *>(&FakeQpc));
    static const uint32_t kWideGlobals[] = {0x10cb5e0, 0x10cb5e4, 0x10cb5e8, 0x10cb5ec, 0x10cb5f0, 0x10cb5f4};
    static const char *const kWideNames[] = {"CreateFileW", "CopyFileW", "MoveFileExW", "FindFirstFileW", "DeleteFileW", "GetFileAttributesW"};
    std::vector<std::unique_ptr<adt::GlobalOverride<void *>>> wide;
    for (int i = 0; i < 6; ++i) wide.emplace_back(new adt::GlobalOverride<void *>(ctx, kWideGlobals[i], difftest::SandboxFunction(kWideNames[i])));
    cRZStopwatch::sPerformanceCounterOverride = &FakeQpcOverride;
    CodePatch setupPatch(ctx.image.At<uint8_t>(0x1f3b70), reinterpret_cast<void *>(&SetupStand));
    CodePatch ctor78Patch(ctx.image.At<uint8_t>(0x1fb300), reinterpret_cast<void *>(&CacheCtor78));
    shape::StandIn ctor34Patch(0x9b35f0, &cGZPersistCacheStrategyBasic::Construct, &CacheCtor34); // ours since lane QQ
    CodePatch emplacePatch(ctx.image.At<uint8_t>(0x1d7390), reinterpret_cast<void *>(&EmplaceStand));
    adt::gFakes[gPrimary] = adt::kConsole;
    adt::gFakes[gPrimary + kSub] = adt::kConsoleFace;
    adt::gFakes[ctx.image.At<uint8_t>(kDirectorStatic)] = adt::kAdapter;

    for (uint32_t seed = 0; seed < 120; ++seed) {
        std::mt19937 rng(seed * 13 + 5);
        std::vector<std::string> dirs;
        for (uint32_t n = rng() % 4, i = 0; i < n; ++i) {
            uint32_t k = rng() % 4;
            dirs.push_back(k == 1 ? dirWith : k == 2 ? dirWithout : k == 0 ? dirMissing : dirWith + "sub\\");
        }
        const uint32_t fieldsMask = rng();
        const bool serviceCached = rng() % 2;
        SideResult sides[2];
        std::vector<uint32_t> groups[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gDirs = dirs;
            gGroupNext = 0;
            created = 0;
            std::memset(gGroupNodes, 0, sizeof(gGroupNodes));
            adt::ResetSide();
            BuildPrimary();
            auto put = [](uint32_t off, const void *p) {
                uint32_t v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
                std::memcpy(gPrimary + off, &v, 4);
            };
            put(0, primaryVTable.data());
            put(kSub, itsVTable.data());
            const uint32_t fields[] = {0xcc, 0xd0, 0x1c0, 0xe0, 0xe4, 0xe8, 0xec};
            for (int i = 0; i < 7; ++i) put(fields[i], (fieldsMask >> i) & 1 ? previous[i % 4].get() : nullptr);
            serviceGlobal.Set(serviceCached ? cachedService.get() : nullptr);
            void *face = gPrimary + 4;
            gCreated.clear();
            uint32_t r = side == 0 ? orig(face) : psshape::Call(PSX(Init), face);
            sides[side].ret = r & 0xff;
            sides[side].log = gLog;
            // Fields holding a created cache: its creation index instead of its address.
            for (size_t off = 0; off + 4 <= sizeof(gPrimary); off += 4) {
                uint32_t v;
                std::memcpy(&v, gPrimary + off, 4);
                for (size_t i = 0; i < gCreated.size(); ++i)
                    if (v == static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gCreated[i]))) {
                        v = 0xcace0000u | static_cast<uint32_t>(i);
                        std::memcpy(gPrimary + off, &v, 4);
                    }
            }
            sides[side].primary.assign(gPrimary, gPrimary + sizeof(gPrimary));
            for (uint32_t i = 0; i < gGroupNext && i < 32; ++i) groups[side].push_back(gGroupNodes[i].group);
            alloc.Release();
        }
        ++ctx.cases;
        if (sides[0].ret != sides[1].ret) ctx.Fail("seed %u: original returned 0x%x, ours 0x%x", seed, sides[0].ret, sides[1].ret);
        if (sides[0].log != sides[1].log) ctx.Fail("seed %u: calls differ: %s", seed, adt::FirstDifference(sides[0].log, sides[1].log).c_str());
        if (groups[0] != groups[1]) ctx.Fail("seed %u: path group values differ", seed);
        if (sides[0].primary != sides[1].primary) {
            size_t i = 0;
            while (sides[0].primary[i] == sides[1].primary[i]) ++i;
            ctx.Fail("seed %u: primary byte +0x%zx differs afterwards", seed, i);
        }
        if (seed == 0 && sides[0].log.size() < 50) ctx.Fail("seed 0: only %zu calls logged", sides[0].log.size());
    }
    cRZStopwatch::sPerformanceCounterOverride = nullptr;
    adt::gFakes.erase(gPrimary);
    adt::gFakes.erase(gPrimary + kSub);
    adt::gFakes.erase(ctx.image.At<uint8_t>(kDirectorStatic));
    for (int kind : {adt::kConsole, adt::kConsoleFace, adt::kFramework, adt::kConsoleService, adt::kAdapter, adt::kLogService, adt::kStreamBase, adt::kOther, adt::kCommonImpl})
        for (auto &d : adt::gSlots[kind]) d = adt::SlotDef();
    DeleteFileW(difftest::SandboxPath(L"esA\\Version.sig").c_str());
    RemoveDirectoryW(difftest::SandboxPath(L"esA").c_str());
    RemoveDirectoryW(difftest::SandboxPath(L"esB").c_str());
}

// ---------------------------------------------------------------------------------------------------------
// AddCategoryPath (cITSPersistSystem slot 44). The world: the primary block from the seed with a real category map at
// +0x23c (std::map<int, std::list<cRZString by value>>, the game's layout; the game's operator new and ours share the
// process heap, so both sides' nodes are freed by the test). Each case starts from the same map (0..4 masks with 0..3
// names each), then makes 1..5 calls with masks that are present, absent, negative or zero and names that are null,
// empty, mixed case, past the small-string size or with non-ASCII bytes. Compared: the map afterwards (keys in order;
// each name's vtable, text and reference count), its size word, and every other byte of the primary block.

namespace {

struct CatString {
    const void *vtable;
    std::string text;
    uint32_t refCount;
};
using CatMap = std::map<int32_t, std::list<CatString>>;

std::string DescribeMap(const CatMap &map, const void *vtable)
{
    std::string out;
    for (const auto &entry : map) {
        out += "[" + Hex(static_cast<uint32_t>(entry.first)) + ":";
        for (const CatString &s : entry.second)
            out += " " + std::string(s.vtable == vtable ? "" : "badvt ") + "'" + s.text + "'/" + Hex(s.refCount);
        out += "]";
    }
    return out;
}

} // namespace

PS_TEST(TSPersistSystemES_AddCategoryPath, 0x1da360)
{
    using F = void(__thiscall *)(void *, int32_t, const char *);
    auto orig = ctx.Original<F>(0x1da360);
    const void *stringVTable = ctx.image.At<uint8_t>(cRZString::kVTable);
    static const char *const kNames[] = {"Neighborhoods", "", "MixedCase\xc4Name", "UPPER", "already lower",
                                         "A Much Longer Folder Name Past Sixteen", "x", "Downloads\\Sub\\DIR"};
    static const int32_t kKeys[] = {0xf, 0xf0, 0xf00, 0xf000, 0, -1, 0x7fffffff, static_cast<int32_t>(0x80000000u), 0x123};
    for (uint32_t seed = 0; seed < 500; ++seed) {
        std::mt19937 rng(seed * 17 + 3);
        struct Call { int32_t key; const char *name; };
        std::vector<Call> calls;
        const uint32_t n = 1 + rng() % 5;
        for (uint32_t i = 0; i < n; ++i) calls.push_back({kKeys[rng() % 9], rng() % 6 == 0 ? nullptr : kNames[rng() % 8]});
        std::vector<std::pair<int32_t, std::vector<std::string>>> start;
        for (uint32_t i = rng() % 5; i > 0; --i) {
            std::vector<std::string> names;
            for (uint32_t j = rng() % 4; j > 0; --j) names.push_back("pre" + std::to_string(rng() % 100));
            start.push_back({kKeys[rng() % 9], names});
        }

        std::string maps[2];
        std::vector<uint8_t> primaries[2];
        uint32_t sizes[2] = {};
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            BuildPrimary();
            CatMap map;
            for (const auto &entry : start)
                for (const std::string &name : entry.second) map[entry.first].push_back({stringVTable, name, 0});
            for (const auto &entry : start) map[entry.first]; // masks with no names
            std::memcpy(gPrimary + 0x23c, &map, 8);
            void *face = gPrimary + kSub;
            for (const Call &c : calls) {
                if (side == 0)
                    orig(face, c.key, c.name);
                else
                    psshape::Call(PSX(AddCategoryPath), face, c.key, c.name);
            }
            std::memcpy(&sizes[side], gPrimary + 0x240, 4);
            std::memcpy(&map, gPrimary + 0x23c, 8); // the head is unchanged; the size word comes back
            maps[side] = DescribeMap(map, stringVTable);
            std::memset(gPrimary + 0x23c, 0, 8);
            primaries[side].assign(gPrimary, gPrimary + sizeof(gPrimary));
        }
        ++ctx.cases;
        if (maps[0] != maps[1]) ctx.Fail("seed %u: map differs: original %s, ours %s", seed, maps[0].c_str(), maps[1].c_str());
        if (sizes[0] != sizes[1]) ctx.Fail("seed %u: map size %u vs %u", seed, sizes[0], sizes[1]);
        if (primaries[0] != primaries[1]) ctx.Fail("seed %u: primary block differs outside the map", seed);
        if (seed == 0 && maps[0].find('\'') == std::string::npos) ctx.Fail("seed 0: no names added (%s)", maps[0].c_str());
    }
}

// ---------------------------------------------------------------------------------------------------------
// CollectPathGroups (cITSPersistSystem slot 47). The world: the primary block from the seed with the ITS face
// (kConsoleFace: 39 AgeCaches) and the segment lock (kLock) as recording fakes, the owner thread word and
// GetCurrentThreadId's import slot, the allocator service fake, and a real std::set<uint32_t> (allocator-service nodes)
// pre-filled with 0..4 groups. The path is a game cRZString: empty, a sandbox folder with a file in it (with and without
// the trailing separator), a file in it (not a folder) or a missing folder; DirectoryExists reaches the sandbox through
// the game's wide file functions. 0x1e3c70 (opens the folder's segments) is a recording stand-in on both sides: it
// puts 0..3 segment fakes (kOther: 19 GetResourceKeyList, 2 Release) in the vector and answers a seeded byte. The key
// lists hold 0..5 keys whose groups repeat and overlap the set's. Compared: the call log (allocations and frees by
// the allocator fake included), the return byte, the set (in order) and its size, and the primary block.

namespace {

adt::AllocatorFake *gTracker = nullptr; // blocks the test makes go with the fake's, freed by Release()

template <class T> struct TrackedAllocator {
    using value_type = T;
    TrackedAllocator() = default;
    template <class U> TrackedAllocator(const TrackedAllocator<U> &) {}
    T *allocate(size_t n)
    {
        void *p = ::operator new(n * sizeof(T));
        gTracker->Track(p);
        return static_cast<T *>(p);
    }
    void deallocate(T *, size_t) {}
    template <class U> bool operator==(const TrackedAllocator<U> &) const { return true; }
    template <class U> bool operator!=(const TrackedAllocator<U> &) const { return false; }
};
using TestGroupSet = std::set<uint32_t, std::less<uint32_t>, TrackedAllocator<uint32_t>>;

uint32_t gSegmentCount = 0;
void *gSegments[3] = {};

uint32_t __fastcall OpenDirStand(void *self, void *, uint32_t mode, const cRZString *path, uint32_t flag, void *category,
                                 std::vector<void *> *out, uint32_t a6)
{
    ++gCalls;
    gLog.push_back("open(" + std::string(self == gPrimary ? "primary" : "?") + ", " + Hex(mode) + ", S:" + adt::Describe(path) + ", " +
                   Hex(flag) + ", " + (category ? "cat" : "null") + ", " + (out ? "out" : "null") + ", " + Hex(a6) + ")");
    for (uint32_t i = 0; i < gSegmentCount; ++i) out->push_back(gSegments[i]);
    static const uint32_t answers[] = {1, 1, 0, 0x101, 2, 0x100};
    return (Mix(gSeed, 77) & 0xffffff00u) | answers[Mix(gSeed, 78) % 6];
}

} // namespace

PS_TEST(TSPersistSystemES_CollectPathGroups, 0x1da600)
{
    using F = uint32_t(__thiscall *)(void *, void *, void *);
    auto orig = ctx.Original<F>(0x1da600);
    if (difftest::SandboxDir().empty()) {
        ctx.Fail("no sandbox directory");
        return;
    }
    CreateDirectoryW(difftest::SandboxPath(L"esC").c_str(), nullptr);
    {
        HANDLE h = CreateFileW(difftest::SandboxPath(L"esC\\f.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    const std::string paths[] = {"", Narrow(difftest::SandboxPath(L"esC")), Narrow(difftest::SandboxPath(L"esC\\")),
                                 Narrow(difftest::SandboxPath(L"esC\\f.txt")), Narrow(difftest::SandboxPath(L"esMissing")),
                                 Narrow(difftest::SandboxPath(L"esC/"))};

    adt::AllocatorFake alloc(ctx);
    gTracker = &alloc;
    Def(adt::kConsoleFace, 39, 0, "");
    Def(adt::kLock, 1, 0, "");
    Def(adt::kLock, 2, 0, "", [](void *, const uint32_t *) { return 0x12345678u; });
    Def(adt::kOther, 2, 0, "", [](void *, const uint32_t *) { return 0x55u; });
    Def(adt::kOther, 19, 2, "qq", [](void *self, const uint32_t *args) {
        auto *vec = reinterpret_cast<ServiceBlockVector *>(args[0]);
        uint32_t which = 0;
        while (which < 3 && gSegments[which] != self) ++which;
        const uint32_t n = Mix(gSeed, 100 + which) % 6;
        if (n == 0 && Mix(gSeed, 200 + which) % 2) return 0u; // no block at all
        auto *keys = static_cast<uint32_t *>(::operator new((n + 1) * 16));
        gTracker->Track(keys);
        for (uint32_t i = 0; i < n * 4; ++i) keys[i] = Mix(gSeed, 300 + which * 32 + i);
        for (uint32_t i = 0; i < n; ++i) keys[i * 4 + 1] = Mix(gSeed, 400 + which * 8 + i) % 9 * 0x1000;
        vec->mFirst = keys;
        vec->mLast = keys + n * 4;
        vec->mEnd = keys + (n + 1) * 4;
        return 0u;
    });
    std::vector<std::unique_ptr<adt::Fake>> segments;
    for (int i = 0; i < 3; ++i) {
        segments.emplace_back(new adt::Fake(adt::kOther));
        gSegments[i] = segments.back()->get();
    }
    auto itsVTable = adt::BuildVTable(adt::kConsoleFace);
    auto lockVTable = adt::BuildVTable(adt::kLock);
    adt::GlobalOverride<void *> threadIat(ctx, kIatGetCurrentThreadId, reinterpret_cast<void *>(&FakeThreadId));
    static const uint32_t kWideGlobals[] = {0x10cb5e0, 0x10cb5e4, 0x10cb5e8, 0x10cb5ec, 0x10cb5f0, 0x10cb5f4};
    static const char *const kWideNames[] = {"CreateFileW", "CopyFileW", "MoveFileExW", "FindFirstFileW", "DeleteFileW", "GetFileAttributesW"};
    std::vector<std::unique_ptr<adt::GlobalOverride<void *>>> wide;
    for (int i = 0; i < 6; ++i) wide.emplace_back(new adt::GlobalOverride<void *>(ctx, kWideGlobals[i], difftest::SandboxFunction(kWideNames[i])));
    CodePatch openPatch(ctx.image.At<uint8_t>(0x1e3c70), reinterpret_cast<void *>(&OpenDirStand));
    CodePatch openOursPatch(shape::MemberCode(&cTSPersistSystem::OpenContentFolder), reinterpret_cast<void *>(&OpenDirStand));
    adt::gFakes[gPrimary + kSub] = adt::kConsoleFace;
    adt::gFakes[gPrimary + 0x68] = adt::kLock;

    uint32_t opened = 0, grouped = 0;
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 rng(seed * 13 + 5);
        const std::string &pathText = paths[seed % 4 == 0 ? 1 + rng() % 2 : rng() % 6];
        const bool owner = rng() % 2;
        std::vector<uint32_t> start;
        for (uint32_t i = rng() % 5; i > 0; --i) start.push_back(rng() % 9 * 0x1000);

        SideResult sides[2];
        std::string sets[2];
        for (int side = 0; side < 2; ++side) {
            gSeed = seed;
            gCalls = 0;
            gThreadId = 0x5500 + seed;
            gSegmentCount = Mix(seed, 9) % 4;
            adt::ResetSide();
            BuildPrimary();
            uint32_t v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(itsVTable.data()));
            std::memcpy(gPrimary + kSub, &v, 4);
            v = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lockVTable.data()));
            std::memcpy(gPrimary + 0x68, &v, 4);
            v = owner ? gThreadId : gThreadId + 1;
            std::memcpy(gPrimary + 0x244, &v, 4);
            alignas(TestGroupSet) uint8_t setBuffer[sizeof(TestGroupSet)];
            auto *set = new (setBuffer) TestGroupSet(); // never destroyed: its nodes go with the allocator fake's blocks
            for (uint32_t g : start) set->insert(g);
            cRZString path;
            path.Construct(pathText.c_str());
            void *face = gPrimary + kSub;
            uint32_t r = side == 0 ? orig(face, &path, set)
                                   : psshape::Call(PSX(CollectPathGroups), face, path.AsInterface(), set);
            path.Destruct();
            sides[side].ret = r & 0xff;
            sides[side].log = gLog;
            sides[side].primary.assign(gPrimary, gPrimary + sizeof(gPrimary));
            for (uint32_t g : *set) sets[side] += Hex(g) + " ";
            sets[side] += "size " + std::to_string(set->size());
            alloc.Release();
        }
        ++ctx.cases;
        if (sides[0].log.size() > 3) ++opened;
        if (sides[0].ret && gSegmentCount) ++grouped;
        if (sides[0].ret != sides[1].ret) ctx.Fail("seed %u (%s): original returned 0x%x, ours 0x%x", seed, pathText.c_str(), sides[0].ret, sides[1].ret);
        if (sides[0].log != sides[1].log)
            ctx.Fail("seed %u (%s): calls differ: %s", seed, pathText.c_str(), adt::FirstDifference(sides[0].log, sides[1].log).c_str());
        if (sets[0] != sets[1]) ctx.Fail("seed %u: groups differ: original %s, ours %s", seed, sets[0].c_str(), sets[1].c_str());
        if (sides[0].primary != sides[1].primary) ctx.Fail("seed %u: primary block differs afterwards", seed);
    }
    if (opened < 50 || grouped < 30) ctx.Fail("coverage: %u cases opened the folder, %u collected groups", opened, grouped);
    gTracker = nullptr;
    adt::gFakes.erase(gPrimary + kSub);
    adt::gFakes.erase(gPrimary + 0x68);
    for (int kind : {adt::kConsoleFace, adt::kLock, adt::kOther})
        for (auto &d : adt::gSlots[kind]) d = adt::SlotDef();
    DeleteFileW(difftest::SandboxPath(L"esC\\f.txt").c_str());
    RemoveDirectoryW(difftest::SandboxPath(L"esC").c_str());
}
