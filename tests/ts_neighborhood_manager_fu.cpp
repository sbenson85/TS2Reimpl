// Differential tests for the two cTSNeighborhoodManager helpers lane FU made ours (src/ts/cTSNeighborhoodManagerLoad.cpp):
// AddTutorial (0x4a27a0, Load's tutorial step) and DeleteFiles (0x4a2950, what Remove does to a neighbourhood's files).
//
// The world: a manager in a guarded buffer (the routes of tests/ts_neighborhood_manager_shape.h: the original RVA on a
// game-shaped object, our member on our object, the adapter on a game-shaped object), its slot 18 (Add) a logging fake, and
// the recording fakes of tests/ts_services_fixture.h: the neighbourhood, its name (cIGZString: text slots 5 / 6, length slot
// 7), its persist face, the persist system (cache 0x10faf6c: slot 3 the record database, slot 11 a group for a name, slot 25
// releases a file's segments), the database (slot 34 a record for a key), the record (slot 11 its file), the framework (slot
// 3 creates the neighbourhood). Strings the code passes are logged by content. Stood in on both sides by patching the image
// (ours calls these originals): the four path helpers (0xc0b29b, 0xc0b1b9, 0xc0b492, 0xc0b362; directories made up from the
// name), the directory scan (0x1c48da, 0x1c63d4, 0x1c5047, 0x1c509f, 0x1c49eb; made-up entries), the tree walk 0x1c628f, the
// tree delete 0x1c5fa0, the file delete 0x1c2e25, the "visited" cleanup 0x4a5570, and PathExists (0x1c1242 and ours). Every
// fake's choice comes from one generator in call order, so the sides see the same world only while they make the same calls.
// Compared: the event log, the fakes' counts and words, the manager's bytes.
#include "code_patch.h"
#include "ts_dir_scan_stand.h"
#include "ts_services_fixture.h"

#include "rz/cRZFile.h"
#include "rz/PathFileOps.h"
#include "rz/cRZString.h"
#include "ts/cTSNeighborhoodManager.h"
#include "ts_neighborhood_manager_shape.h"

#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{2, "dddd"}};
using namespace svcfx;
using namespace hoodmgrshape;
using Mgr = cTSNeighborhoodManager;

enum Fake : unsigned { kHood, kName, kRes, kPersist, kFramework, kDatabase, kRecord };
constexpr uint32_t kMgrTag = 0x20000000;
enum : uint32_t {
    evAdd = 0x90,
    evCreate,
    evHoodText,
    evGroupName,
    evKey,
    evPath,
    evScanNew,
    evScanFind,
    evScanEntry,
    evScanPath,
    evScanEnd,
    evPrepare,
    evDeleteTree,
    evDeleteFile,
    evForget,
    evExists,
    evRelease,
    evRecordKey,
    evRecordPath,
};

using shape::CodePatch;
using shape::StandIn;
template <typename F> const void *Code(F f) { return shape::CodeOf(f); }

struct Guarded {
    uint8_t before[16];
    alignas(8) uint8_t bytes[sizeof(Mgr)];
    uint8_t after[16];
};
Guarded gWork;
bool gAlive = false;
Mgr *M() { return reinterpret_cast<Mgr *>(gWork.bytes); }

std::mt19937 gGen; // the fakes' choices, in call order

void EvText(const char *text, size_t length)
{
    Ev(uint32_t(length));
    for (size_t i = 0; i < length; ++i) Ev(uint8_t(text[i]));
}
void EvCText(const char *text) { EvText(text, std::strlen(text)); }
void EvString(const void *string) // a cRZString-layout object
{
    const auto *s = static_cast<const NeighborhoodName *>(string);
    EvText(s->mString.data(), s->mString.size());
}

const char *const kNames[] = {"Tutorial", "N001", "Pleasantview", "a name longer than fifteen chars", "x", ""};
constexpr unsigned kNameCount = sizeof(kNames) / sizeof(kNames[0]);

// ---- the manager's slot 18 (Add) and the fakes' handlers ----
uint32_t __fastcall MgrAdd(Mgr *, void *, void *hood) { Ev(evAdd), Ev(Norm(P(hood))); return gGen(); }
uint32_t FrameworkCreate(Obj *, uint32_t *args)
{
    // The original uses the neighbourhood unchecked, so one is always made.
    ++F(kHood).refs;
    *reinterpret_cast<void **>(args[2]) = &F(kHood);
    return gGen();
}
uint32_t __fastcall HoodSetText(Obj *self, void *, const char *text) { Ev(evHoodText), Ev(3), Ev(self->id), EvCText(text); return gGen(); }
uint32_t __fastcall HoodSetName(Obj *self, void *, const char *text) { Ev(evHoodText), Ev(7), Ev(self->id), EvCText(text); return gGen(); }
uint32_t PersistGroup(Obj *, uint32_t *args)
{
    Ev(evGroupName), EvString(reinterpret_cast<void *>(args[0]));
    return gGen();
}
uint32_t ResSetKey(Obj *, uint32_t *args)
{
    const auto *key = reinterpret_cast<const uint32_t *>(args[0]);
    Ev(evKey);
    for (int i = 0; i < 4; ++i) Ev(key[i]);
    return gGen();
}
uint32_t ResGetKey(Obj *self, uint32_t *args)
{
    auto *key = reinterpret_cast<uint32_t *>(args[0]);
    for (int i = 0; i < 4; ++i) key[i] = self->value[i];
    return args[0];
}
uint32_t PersistRelease(Obj *, uint32_t *args)
{
    Ev(evRelease), EvString(reinterpret_cast<void *>(args[0])), Ev(args[1]);
    return gGen();
}
uint32_t DatabaseRecord(Obj *, uint32_t *args)
{
    const auto *key = reinterpret_cast<const uint32_t *>(args[0]);
    Ev(evRecordKey);
    for (int i = 0; i < 4; ++i) Ev(key[i]);
    if (gGen() % 5 == 0) return gGen() % 2;
    ++F(kRecord).refs;
    *reinterpret_cast<void **>(args[1]) = &F(kRecord);
    return gGen() % 8 != 0;
}
const char *const kRecordFiles[] = {"C:/hoods/N001/N001_Neighborhood.package", "D:\\x\\Pleasantview_Neighborhood.package",
    "C:/a/b/a name longer than fifteen chars.package", "C:/noext", "E:/y/z/N002_Neighborhood"};
uint32_t RecordPath(Obj *, uint32_t *args)
{
    const char *text = kRecordFiles[gGen() % 5];
    reinterpret_cast<NeighborhoodName *>(args[0])->mString = text;
    Ev(evRecordPath), EvCText(text);
    return 0;
}

// ---- the stand-ins ----
template <int N> uint8_t __cdecl StandPath(const char *text, NeighborhoodName *out, uint32_t a, uint32_t b)
{
    static const char *const kRoots[] = {"C:/saves/", "C:/store/", "D:\\third\\", "E:/fourth/"};
    Ev(evPath), Ev(N), EvCText(text), Ev(a), Ev(b);
    out->mString = std::string(kRoots[N]) + text + (gGen() % 2 ? "/" : "");
    return uint8_t(gGen());
}

// The directory scan's choices (tests/ts_dir_scan_stand.h): a separator after the directory now and then, up to four
// entries, mostly packages.
unsigned ScanCount(std::string &dir, const std::string &)
{
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += gGen() % 2 ? "/" : "\\";
    return gGen() % 5;
}
void ScanMake(dirscan::Entry &e, const std::string &dir, const std::string &, unsigned i)
{
    static const uint8_t kFlags[] = {4, 8, 0, 12};
    e.flags = gGen() % 4 ? 8 : kFlags[gGen() % 4];
    const std::string base = gGen() % 6 == 0 ? std::string("Z:/elsewhere/") : dir;
    e.path = base + kNames[1 + gGen() % (kNameCount - 2)] + std::to_string(i) + ".package";
}
uint8_t __cdecl StandPrepare(const NeighborhoodName *dir, uint32_t a, uint32_t b)
{
    Ev(evPrepare), EvString(dir), Ev(a), Ev(b);
    return uint8_t(gGen());
}
uint8_t __cdecl StandDeleteTree(const NeighborhoodName *dir, uint32_t a)
{
    Ev(evDeleteTree), EvString(dir), Ev(a);
    return uint8_t(gGen());
}
uint8_t __cdecl StandDeleteFile(const NeighborhoodName *path)
{
    Ev(evDeleteFile), EvString(path);
    return uint8_t(gGen());
}
uint8_t __cdecl StandForget(const char *list, const char *name)
{
    Ev(evForget), EvCText(list), EvCText(name);
    return uint8_t(gGen());
}
bool __cdecl StandPathExists(const NeighborhoodName *path)
{
    Ev(evExists), EvString(path);
    return gGen() % 3 != 0;
}

// ---- the world ----
void Teardown()
{
    if (!gAlive) return;
    gAlive = false;
    Destroy(gWork.bytes);
}

void Build(uint32_t seed)
{
    Teardown();
    ResetAll();
    std::mt19937 r(seed * 2654435761u + 23);
    for (auto &b : gWork.before) b = uint8_t(r());
    for (auto &b : gWork.after) b = uint8_t(r());
    for (auto &b : gWork.bytes) b = uint8_t(r());
    AddRange(gWork.bytes, sizeof(Mgr), kMgrTag);

    Obj &hood = F(kHood);
    static const uint32_t kKinds[] = {1, 1, 1, 0, 2, 0x101};
    hood.Set(18, 0, kKinds[r() % 6]);
    hood.Set(8, 0, &F(kName));
    hood.table[3] = reinterpret_cast<void *>(&HoodSetText);
    hood.table[7] = reinterpret_cast<void *>(&HoodSetName);
    hood.Set(11, 1);
    hood.Set(13, "dd");
    hood.Set(24, 0, &F(kRes));
    Obj &name = F(kName);
    const char *text = kNames[r() % kNameCount];
    name.Set(5, 0, text);
    name.Set(6, 0, text);
    name.Set(7, 0, uint32_t(std::strlen(text)));
    Obj &res = F(kRes);
    res.Set(3, "p", 0, ResGetKey);
    res.Set(4, "p", 0, ResSetKey);
    for (int k = 0; k < 4; ++k) res.value[k] = r();
    Obj &persist = F(kPersist);
    persist.Set(3, 0, &F(kDatabase));
    persist.Set(11, "p", 0, PersistGroup);
    persist.Set(25, "pd", 0, PersistRelease);
    F(kDatabase).Set(34, "pp", 0, DatabaseRecord);
    F(kRecord).Set(11, "p", 0, RecordPath);
    F(kFramework).Set(3, "ddp", 0, FrameworkCreate);
    gFrameworkIndex = kFramework;

    Construct(gWork.bytes);
    gAlive = true;
    Install(gWork.bytes);
    dirscan::Reset();
    gGen.seed(r());
    gEv.clear();
}

void Capture(Blob &b)
{
    b.push_back(0xb10b);
    for (auto v : gWork.before) b.push_back(v);
    for (auto v : gWork.after) b.push_back(v);
    uint8_t copy[sizeof(Mgr)];
    std::memcpy(copy, gWork.bytes, sizeof(copy));
    auto word = [&](unsigned off) -> uint32_t & { return *reinterpret_cast<uint32_t *>(copy + off); };
    for (unsigned off : {0x10u, 0x18u, 0x24u}) word(off) = 0; // the map heads and the cursor (heap pointers of each route's own)
    for (unsigned f = 0; f < kFaces; ++f) word(kFaceOffset[f]) = VtCode(f, word(kFaceOffset[f]));
    for (unsigned i = 0; i < sizeof(copy) / 4; ++i) b.push_back(reinterpret_cast<uint32_t *>(copy)[i]);
    CaptureFakes(b, kValueKinds);
    b.push_back(0xe7e7);
    b.insert(b.end(), gEv.begin(), gEv.end());
}

// The scope of a test: globals and patches.
struct World {
    Globals globals;
    CodePatch path1, path2, path3, path4;
    dirscan::Patches scan;
    CodePatch prepare, deleteTree, forget;
    StandIn deleteFile, exists;
    explicit World(difftest::Context &)
        : path1(0xc0b29b, Code(&StandPath<0>)), path2(0xc0b1b9, Code(&StandPath<1>)), path3(0xc0b492, Code(&StandPath<2>)),
          path4(0xc0b362, Code(&StandPath<3>)), prepare(0x1c628f, Code(&StandPrepare)), deleteTree(0x1c5fa0, Code(&StandDeleteTree)),
          forget(0x4a5570, Code(&StandForget)), deleteFile(0x1c2e25, &PathDelete, &StandDeleteFile), exists(0x1c1242, &cRZFile::PathExists, &StandPathExists)
    {
        dirscan::gEvBase = evScanNew;
        dirscan::gCount = &ScanCount;
        dirscan::gMake = &ScanMake;
        ResetAll();
        globals.Set(kPersistCell, &F(kPersist));
        for (auto &face : gPatch)
            for (void *&slot : face) slot = nullptr;
        gPatch[0][18] = reinterpret_cast<void *>(&MgrAdd);
    }
    ~World() { Teardown(); }
};

template <class Run> void Pair(difftest::Context &ctx, const char *what, uint32_t seed, Run run)
{
    Blob blobs[3];
    for (int route = 0; route < 3; ++route) {
        gRoute = route;
        gTrapHits = 0;
        Build(seed);
        run();
        Capture(blobs[route]);
        if (gTrapHits) ctx.Fail("%s seed %u route %d: %u trap hits", what, seed, route, gTrapHits);
    }
    gRoute = 0;
    Same(ctx, blobs[0], blobs[1], (std::string(what) + " (member)").c_str(), seed);
    Same(ctx, blobs[0], blobs[2], (std::string(what) + " (adapter)").c_str(), seed);
}
} // namespace

DIFF_TEST(NeighborhoodManager_AddTutorial, 0x4a27a0)
{
    World world(ctx);
    for (uint32_t seed = 0; seed < 300; ++seed)
        Pair(ctx, "AddTutorial", seed, [&]() {
            using Fn = void(__thiscall *)(Mgr *);
            if (gRoute == 0) ctx.Original<Fn>(0x4a27a0)(M());
            else if (gRoute == 1) M()->AddTutorial();
            else reinterpret_cast<Fn>(HOODMGR_ADAPTER(AddTutorial))(M());
        });
}

DIFF_TEST(NeighborhoodManager_DeleteFiles, 0x4a2950)
{
    World world(ctx);
    uint32_t main = 0, other = 0;
    for (uint32_t seed = 0; seed < 600; ++seed) {
        Pair(ctx, "DeleteFiles", seed, [&]() {
            void *hood = &F(kHood);
            using Fn = void(__thiscall *)(Mgr *, void *);
            if (gRoute == 0) {
                const bool isMain = F(kHood).ret[18] == 1;
                main += isMain;
                other += !isMain;
                ctx.Original<Fn>(0x4a2950)(M(), hood);
            } else if (gRoute == 1) {
                M()->DeleteFiles(hood);
            } else {
                reinterpret_cast<Fn>(HOODMGR_ADAPTER(DeleteFiles))(M(), hood);
            }
        });
    }
    if (!main || !other) ctx.Fail("coverage: main %u other %u", main, other);
}
