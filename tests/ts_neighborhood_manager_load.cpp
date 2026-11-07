// Differential tests for the service-heavy cTSNeighborhoodManager members (src/ts/cTSNeighborhoodManager.h): Save (vf5),
// NextFreeName (vf17), Add (vf18), Remove (vf19) and the adjustor thunks of the two faces.
//
// A world is one manager in a guarded buffer, populated by our own code (the containers are the library's, the game's layout),
// with the shared recording fakes of tests/ts_services_fixture.h: neighbourhoods, their names (cIGZString fakes) and persist
// faces, the persist system (cache 0x10faf6c), the framework (through the director), a save listener, the message server (cache
// 0x10f519c; it records the posted message's fields and text), the common implementation (cache 0x10cc040, it supplies the path
// root the path helpers start from). The callees that touch the file system are stood in on both sides: 0x1c5031 (make sure a
// directory exists; the image's and ours), 0x4a2950 (delete a neighbourhood's files) and DirectoryExists (the image's
// 0x1c4f94 and ours); they record the paths they get. Two worlds built from one seed start the same; one runs the original, the
// other ours, and the results, the manager's bytes, the containers' contents, the fakes' counts and words and the event logs
// are compared.
#include "code_patch.h"
#include "ts_dir_scan_stand.h"
#include "ts_services_fixture.h"

#include "app/AppDebugger.h"
#include "app/AppServices2.h"
#include "app/SpecialFolders.h"
#include "rz/cRZFile.h"
#include "rz/cRZMessage2Standard.h"
#include "ts/cTSNeighborhoodManager.h"
#include "ts_neighborhood_manager_shape.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

// The fakes' value words read by CaptureFakes: (fake index, a kind per value word: 'd' data, 'p' pointer).
constexpr svcfx::ValueKinds kValueKinds[] = {{0, "d"}, {1, "d"}, {2, "d"}, {3, "d"}, {4, "d"}, {5, "-d"}, {6, "-d"}, {7, "-d"}, {8, "-d"}, {9, "-d"}, {10, "dddd"}, {11, "dddd"}, {12, "dddd"}, {13, "dddd"}, {14, "dddd"}, {15, "d"}, {16, "dd"}, {19, "d"}};
using namespace svcfx;
using namespace hoodmgrshape;
using Mgr = cTSNeighborhoodManager;

constexpr unsigned kHoods = 5;
enum Fake : unsigned {
    kHood0 = 0,
    kName0 = kHood0 + kHoods,
    kRes0 = kName0 + kHoods,
    kPersist = kRes0 + kHoods,
    kFramework,
    kListener,
    kServer,
    kCommon,
    kListed, // what the persist system puts into Save's list
    kDatabase, // Load: the persist system's record database
    kRecord,   // Load: a record (its file)
    kSegment,  // Load: an opened package segment
};
static_assert(kSegment < kFakes);
constexpr uint32_t kMgrTag = 0x20000000;
enum : uint32_t { evMgr = 0x70, evMessage = 0x71, evEnsure = 0x72, evDelete = 0x73, evExists = 0x74, evFormat = 0x75, evKey = 0x76 };

struct Guarded {
    uint8_t before[16];
    alignas(8) uint8_t bytes[sizeof(Mgr)];
    uint8_t after[16];
};
Guarded gWork;
bool gAlive = false;
// Remove of a main neighbourhood calls slot 12 on every entry of its record's list, null ones included (a crash), so its test keeps them out.
bool gNoNullEntries = false;
Mgr *M() { return reinterpret_cast<Mgr *>(gWork.bytes); }

// Names the fakes answer (static: the same pointers on both sides).
const char *const kNames[] = {"alpha", "ALPHA", "Bravo", "charlie", "N001", "n002", "a name longer than fifteen chars", "delta"};
constexpr unsigned kNameCount = sizeof(kNames) / sizeof(kNames[0]);
const char *const kRoots[] = {"", "C:/hoods/", "a root path that is longer than fifteen characters\\"};

void EvText(const char *text, size_t length)
{
    Ev(uint32_t(length));
    for (size_t i = 0; i < length; ++i) Ev(uint8_t(text[i]));
}
void EvString(const void *string) // a cRZString-layout object
{
    const auto *s = static_cast<const NeighborhoodName *>(string);
    EvText(s->mString.data(), s->mString.size());
}

// ---- the manager's own slots (AddRef, the deleting destructor; gPatch: over each route's own copies) ----
uint32_t __fastcall MgrAddRef(Mgr *m, void *) { Ev(evMgr), Ev(1); return ++m->mRefCount; }
void *__fastcall MgrDelete(Mgr *m, void *, uint32_t flags) { Ev(evMgr), Ev(20), Ev(flags); return m; }

// ---- custom handlers ----
uint32_t HoodSetId(Obj *self, uint32_t *args) { self->value[0] = args[0]; return 0; }
uint32_t HoodGetId(Obj *self, uint32_t *) { return self->value[0]; }
uint32_t ResSetKey(Obj *, uint32_t *args)
{
    const auto *key = reinterpret_cast<const uint32_t *>(args[0]);
    Ev(evKey);
    for (int i = 0; i < 4; ++i) Ev(key[i]);
    return 1;
}
uint32_t PersistCollect(Obj *self, uint32_t *args)
{
    if (self->value[0] & 1) reinterpret_cast<ServiceRefVector *>(args[1])->PushBack(&F(kListed));
    self->value[0] >>= 1;
    return 0;
}
uint32_t FrameworkCreate(Obj *self, uint32_t *args)
{
    if (self->value[0]) *reinterpret_cast<void **>(args[2]) = &F(kListener);
    return self->value[1];
}
uint32_t CommonQuery(Obj *self, uint32_t *args)
{
    reinterpret_cast<NeighborhoodName *>(args[1])->mString = kRoots[self->value[0] % 3];
    return 1;
}
uint32_t NameEquals(Obj *self, uint32_t *args)
{
    EvText(reinterpret_cast<const char *>(args[0]), std::strlen(reinterpret_cast<const char *>(args[0])));
    return self->value[1];
}
// The message server's slot 3, set directly (the message is a heap block the generic log would print raw).
uint32_t __fastcall ServerPost(Obj *self, void *, cRZMessage2Standard *m)
{
    Ev(evMessage);
    Ev(self->id);
    if (!m) return Ev(0), 0;
    Ev(m->mRefCount), Ev(m->mFlags), Ev(m->mId);
    svcfx::LogMessageValues(reinterpret_cast<const uint32_t *>(m)); // by the id's kinds (DeclareMessage)
    for (void *p : {m->mPtr20, m->mPtr24, m->mPtr28}) {
        if (!p) {
            Ev(0);
        } else if (*static_cast<const void *const *>(p) == GameAddress<void>(cRZString::kVTable)) {
            Ev(0x5717), Ev(static_cast<NeighborhoodName *>(p)->mRefCount), EvString(p);
        } else {
            Ev(Norm(P(p)));
        }
    }
    return 0;
}

// ---- stand-ins for the file-system callees (patched into the image, and into ours where ours is called directly) ----
uint64_t gExisting; // bit n: the directory of N%03d with number n exists
uint8_t __cdecl StandEnsure(void *path) { Ev(evEnsure), EvString(path); return 1; }
void __fastcall StandDeleteFiles(void *mgr, void *, void *hood) { Ev(evDelete), Ev(Norm(P(mgr))), Ev(Norm(P(hood))); }
bool __cdecl StandExists(cRZString *path)
{
    const std::string &text = reinterpret_cast<NeighborhoodName *>(path)->mString;
    Ev(evExists), EvText(text.data(), text.size());
    const size_t at = text.rfind('N');
    if (at == std::string::npos) return false;
    const unsigned number = unsigned(std::atoi(text.c_str() + at + 1));
    return number < 64 && (gExisting >> number & 1);
}

using shape::CodePatch;
using shape::StandIn;

// ---- the name argument of NextFreeName: slot 31 formats (__cdecl), slot 5 gives the text ----
struct NameArg {
    void **vt;
    char text[40];
};
NameArg gNameArg;
void *gNameVT[40];
void __cdecl NameFormat(NameArg *self, const char *format, int number)
{
    Ev(evFormat), Ev(format == GameAddress<const char>(0xdcbc6c)), Ev(uint32_t(number));
    std::snprintf(self->text, sizeof(self->text), format, number);
}
const char *__fastcall NameText(NameArg *self, void *) { return self->text; }

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
    std::mt19937 r(seed * 2654435761u + 11);
    for (auto &b : gWork.before) b = uint8_t(r());
    for (auto &b : gWork.after) b = uint8_t(r());
    for (auto &b : gWork.bytes) b = uint8_t(r());
    AddRange(gWork.bytes, sizeof(Mgr), kMgrTag);
    AddRange(&gNameArg, sizeof(gNameArg), 0x30000000);

    for (unsigned i = 0; i < kHoods; ++i) {
        Obj &h = F(kHood0 + i);
        const uint32_t ids[] = {0xffffffffu, 0, 1 + r() % 6, r()};
        h.value[0] = ids[r() % 4];
        h.Set(8, 0, &F(kName0 + i));
        h.Set(11, "d", 0, HoodSetId);
        h.Set(12, 0, 0, HoodGetId);
        h.Set(14, "d", r() % 3 == 0 ? (r() % 2 ? 1 : r() | 1) : (r() % 2 ? 0 : r() & 0xffffff00));
        h.Set(18, 0, r() % 3);
        h.Set(24, 0, &F(kRes0 + i));
        h.Set(27, 0, r() % 4 == 0 ? (r() & 0xffffff00) : (r() | 1));
        Obj &n = F(kName0 + i);
        const char *text = kNames[r() % kNameCount];
        n.Set(5, 0, text);
        n.Set(6, 0, text);
        n.Set(7, 0, uint32_t(std::strlen(text)));
        n.Set(9, "pdd", 0, NameEquals);
        n.value[1] = r() % 2 ? (r() | 1) : (r() & 0xffffff00);
        Obj &res = F(kRes0 + i);
        res.Set(6, 0, r() % 4 == 0 ? 0 : r());
        res.Set(4, "p", 1, ResSetKey);
    }
    Obj &persist = F(kPersist);
    persist.value[0] = r();
    persist.Set(48, "dp", 0, PersistCollect);
    persist.Set(49, "p");
    persist.Set(16, "p-", r() % 3 == 0 ? (r() & 0xffffff00) : (r() | 1));
    persist.Set(50, "pd", r() % 3 == 0 ? (r() & 0xffffff00) : (r() | 1));
    Obj &fw = F(kFramework);
    fw.value[0] = r() % 3 != 0;
    fw.value[1] = r();
    fw.Set(3, "ddp", 0, FrameworkCreate);
    F(kListener).Set(3, 0);
    F(kListener).Set(10, "dd");
    F(kServer).table[3] = reinterpret_cast<void *>(&ServerPost);
    F(kCommon).Set(4, "dpd-d", 1, CommonQuery);
    F(kCommon).value[0] = r();
    gFrameworkIndex = kFramework;
    gExisting = (uint64_t(r()) << 32 | r()) & (uint64_t(r()) << 32 | r());

    for (auto &p : gNameVT) p = nullptr;
    gNameVT[5] = reinterpret_cast<void *>(&NameText);
    gNameVT[31] = reinterpret_cast<void *>(&NameFormat);
    gNameArg.vt = gNameVT;
    std::memset(gNameArg.text, 0, sizeof(gNameArg.text));

    Mgr *m = Construct(gWork.bytes);
    gAlive = true;
    m->mRefCount = r() % 4;
    m->mNextId = r() % 4 ? r() % 8 : r();
    m->mKey = {r(), r(), r(), r()};
    auto hood = [&]() -> void * { return &F(kHood0 + r() % kHoods); };
    const unsigned ids = r() % 6;
    for (unsigned i = 0; i < ids; ++i) {
        const uint32_t key = r() % 4 ? r() % 8 : (r() % 2 ? r() : 0xffffffffu);
        auto &ref = m->mHoodsById[key];
        if (r() % 6) ref.Assign(hood());
    }
    // Name records: the hoods' own names (in some case), N-numbers and others.
    const unsigned names = r() % 6;
    for (unsigned i = 0; i < names; ++i) {
        char text[48];
        switch (r() % 3) {
        case 0: std::snprintf(text, sizeof(text), "%s", kNames[r() % kNameCount]); break;
        case 1: std::snprintf(text, sizeof(text), "N%03u", 1 + r() % 6); break;
        default: std::snprintf(text, sizeof(text), "%s", reinterpret_cast<const char *>(F(kName0 + r() % kHoods).ret[6])); break;
        }
        if (r() % 3 == 0)
            for (char *c = text; *c; ++c) *c = char(r() % 2 ? std::toupper(*c) : std::tolower(*c));
        NeighborhoodNameEntry &e = m->mHoodsByName.try_emplace(NeighborhoodName(text)).first->second;
        if (!e.mObject && r() % 4) {
            e.mObject = hood();
            static_cast<Obj *>(e.mObject)->refs++;
        }
        const unsigned n = r() % 4;
        for (unsigned k = 0; k < n; ++k) e.mHoods.PushBack(r() % 8 == 0 && !gNoNullEntries ? nullptr : hood());
    }
    Install(gWork.bytes);
    gEv.clear();
}

void CaptureList(Blob &b, const ServiceRefVector &v)
{
    b.push_back(uint32_t(v.mLast - v.mFirst));
    b.push_back(uint32_t(v.mEnd - v.mFirst));
    for (void **p = v.mFirst; p != v.mLast; ++p) b.push_back(Norm(P(*p)));
}

void Capture(Blob &b)
{
    b.push_back(0xb10b);
    for (auto v : gWork.before) b.push_back(v);
    for (auto v : gWork.after) b.push_back(v);
    uint8_t copy[sizeof(Mgr)];
    std::memcpy(copy, gWork.bytes, sizeof(copy));
    auto word = [&](unsigned off) -> uint32_t & { return *reinterpret_cast<uint32_t *>(copy + off); };
    for (unsigned off : {0x10u, 0x18u, 0x24u, 0x28u, 0x2cu, 0x30u}) word(off) = 0; // heap pointers (contents below)
    for (unsigned f = 0; f < kFaces; ++f) word(kFaceOffset[f]) = VtCode(f, word(kFaceOffset[f]));
    for (unsigned i = 0; i < sizeof(copy) / 4; ++i) b.push_back(reinterpret_cast<uint32_t *>(copy)[i]);
    Mgr *m = M();
    b.push_back(0xc0de);
    for (auto &e : m->mHoodsById) b.push_back(e.first), b.push_back(Norm(P(e.second.Get())));
    b.push_back(0xc0df);
    for (auto &e : m->mHoodsByName) {
        b.push_back(uint32_t(e.first.mString.size()));
        for (char c : e.first.mString) b.push_back(uint8_t(c));
        b.push_back(e.first.mRefCount);
        b.push_back(Norm(P(e.first.mVTable)));
        b.push_back(Norm(P(e.second.mObject)));
        CaptureList(b, e.second.mHoods);
    }
    b.push_back(0xc0e0);
    CaptureList(b, m->mHoods);
    b.push_back(m->mCursor == nullptr ? 0xc001 : m->mCursor->mIsNil ? 0xc002 : 0xc003);
    for (char c : gNameArg.text) b.push_back(uint8_t(c));
    CaptureFakes(b, kValueKinds);
    b.push_back(0xe7e7);
    b.insert(b.end(), gEv.begin(), gEv.end());
}

// The scope of a test: globals, allocator, patches.
struct World {
    Globals globals;
    AllocatorScope allocator;
    StandIn ensure, deleteFiles, exists;
    explicit World(difftest::Context &)
        : ensure(0x1c5031, &folders::EnsureDirectory, &StandEnsure), deleteFiles(0x4a2950, &cTSNeighborhoodManager::DeleteFiles, &StandDeleteFiles),
          exists(0x1c4f94, &as2::DirectoryExists, &StandExists)
    {
        ResetAll();
        globals.Set(kPersistCell, &F(kPersist));
        globals.Set(0x10cc040, &F(kCommon));
        globals.Set(0x10f519c, &F(kServer));
        for (auto &face : gPatch)
            for (void *&slot : face) slot = nullptr;
        gPatch[0][1] = reinterpret_cast<void *>(&MgrAddRef);
        gPatch[0][20] = reinterpret_cast<void *>(&MgrDelete);
    }
    ~World() { Teardown(); gNoNullEntries = false; }
};

template <class Setup, class Run> void Pair(difftest::Context &ctx, const char *what, uint32_t seed, Setup setup, Run run)
{
    Blob blobs[3];
    for (int route = 0; route < 3; ++route) {
        gRoute = route;
        gTrapHits = 0;
        Build(seed);
        std::mt19937 rng(seed * 7919u + 5);
        setup(rng);
        gEv.clear();
        const uint32_t result = uint32_t(run());
        blobs[route].push_back(result);
        Capture(blobs[route]);
        if (gTrapHits) ctx.Fail("%s seed %u route %d: %u trap hits", what, seed, route, gTrapHits);
    }
    gRoute = 0;
    Same(ctx, blobs[0], blobs[1], (std::string(what) + " (vtable route)").c_str(), seed);
    Same(ctx, blobs[0], blobs[2], (std::string(what) + " (adapter route)").c_str(), seed);
}
auto NoSetup = [](std::mt19937 &) {};
constexpr uint32_t kSeeds = 300;
#define ADAPTER(name) HOODMGR_ADAPTER(name)
// Calls the function at `rva` of face `face` on the world's manager, the route's way.
template <class Fn, class... A> auto Run(difftest::Context &ctx, uint32_t rva, unsigned face, void *adapter, A... args)
{
    return Call<Fn>(ctx, gWork.bytes, rva, face, adapter, args...);
}
} // namespace

DIFF_TEST(NeighborhoodManager_Save, 0x4a5aa0)
{
    World world(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed)
        Pair(ctx, "Save", seed, NoSetup, [&]() { return Run<uint8_t(__thiscall *)(Mgr *)>(ctx, 0x4a5aa0, 0, NO_ADAPTER); });
}

DIFF_TEST(NeighborhoodManager_NextFreeName, 0x4a3cf0)
{
    World world(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed)
        Pair(ctx, "NextFreeName", seed, NoSetup, [&]() {
            Run<void(__thiscall *)(Mgr *, void *)>(ctx, 0x4a3cf0, 0, NO_ADAPTER, static_cast<void *>(&gNameArg));
            return 0;
        });
}

DIFF_TEST(NeighborhoodManager_Add, 0x4a2030)
{
    World world(ctx);
    for (uint32_t seed = 0; seed < kSeeds * 2; ++seed) {
        const unsigned which = seed % kHoods;
        Pair(ctx, "Add", seed, NoSetup, [&]() {
            void *hood = &F(kHood0 + which);
            return Run<uint8_t(__thiscall *)(Mgr *, void *)>(ctx, 0x4a2030, 0, NO_ADAPTER, hood);
        });
    }
}

DIFF_TEST(NeighborhoodManager_Remove, 0x4a5740)
{
    World world(ctx);
    gNoNullEntries = true;
    for (uint32_t seed = 0; seed < kSeeds * 2; ++seed) {
        const unsigned which = seed % (kHoods + 1);
        Pair(ctx, "Remove", seed, NoSetup, [&]() {
            void *hood = which == kHoods ? nullptr : &F(kHood0 + which);
            return Run<uint8_t(__thiscall *)(Mgr *, void *)>(ctx, 0x4a5740, 0, NO_ADAPTER, hood);
        });
    }
}

// The adjustor thunks: QueryInterface (0x4a54d7 / 0x4a54df) and Release (0x4a5557 / 0x4a555f) through the +4 and +8 faces. In
// the shape they are the compiler's thunks in our vtables (no hook): route 0 calls the original thunk, route 1 our vtable's slot
// of that face, route 2 what the game's thunk reaches with the hooks on (it jumps to slot 0 / 2's function, i.e. the adapter, on
// the object pointer), with the trap in the primary slot.
DIFF_TEST(NeighborhoodManager_FaceThunks, 0x4a54d7)
{
    World world(ctx);
    constexpr uint32_t ids[] = {1, 0xcc8a6a62, 0xe4fda3d4, 0x456b8f1d, 0, 2, 0x456b8f1c, 0xcc8a6a69, 0xffffffff};
    for (uint32_t seed = 0; seed < 120; ++seed) {
        const bool serial = seed & 1;
        const unsigned face = serial ? 2 : 1;
        const uint32_t id = ids[(seed >> 1) % (sizeof(ids) / 4)];
        const uint32_t qiThunk = serial ? 0x4a54df : 0x4a54d7, releaseThunk = serial ? 0x4a555f : 0x4a5557;
        ++ctx.cases;
        if (shape::SlotOf(kGameVt[face], kFaceSlots[face], qiThunk) != 0 || shape::SlotOf(kGameVt[face], kFaceSlots[face], releaseThunk) != 2)
            ctx.Fail("thunk RVAs are not the face's slots 0 / 2");
        Pair(ctx, "QueryInterface thunk", seed, NoSetup, [&]() {
            void *out = reinterpret_cast<void *>(0x5a5a5a5a);
            using Fn = uint8_t(__thiscall *)(void *, uint32_t, void **);
            uint8_t r;
            if (gRoute == 0) r = ctx.Original<Fn>(qiThunk)(gWork.bytes + kFaceOffset[face], id, &out);
            else if (gRoute == 1) r = reinterpret_cast<Fn>(OurVt(face)[0])(gWork.bytes + kFaceOffset[face], id, &out);
            else r = Run<Fn>(ctx, 0x4a5480, 0, ADAPTER(QueryInterface), id, &out);
            return uint32_t(r) | (NormOut(P(out), 0x5a5a5a5a) << 8);
        });
        Pair(ctx, "Release thunk", seed, [&](std::mt19937 &rng) { M()->mRefCount = rng() % 4 == 0 ? rng() : rng() % 4; },
            [&]() {
                using Fn = uint32_t(__thiscall *)(void *);
                if (gRoute == 0) return ctx.Original<Fn>(releaseThunk)(gWork.bytes + kFaceOffset[face]);
                if (gRoute == 1) return reinterpret_cast<Fn>(OurVt(face)[2])(gWork.bytes + kFaceOffset[face]);
                return Run<Fn>(ctx, 0x4a5540, 0, ADAPTER(Release));
            });
    }
}

// ---- Load (vf3, 0x4a4160) and HasPackage (0x4a6000) ----
//
// The world above plus: the persist system's record database (slot 3; its slot 19 fills the key vector from the allocator
// service, slot 34 hands out a record whose slot 11 gives the record's file), the resource lookup (persist slot 5: one of the
// hoods, or none, logged by key), the segment opener (persist slot 22, logged by its drive and file name texts), the common
// implementation's directory list (slot 5: short strings, so the nodes' strings need no heap), and the hoods' SetName (slot 7,
// which renames the hood's name fake). Stood in on both sides by patching the image (ours calls these originals): the
// directory scan (0x1c48da build, 0x1c63d4 find, 0x1c5047 entry, 0x1c509f entry path, 0x1c49eb destroy; entries made up from
// the scan's directory and the hoods' names), the tutorial (0x4a27a0) and PathExists (0x1c1242 and ours). Every fake's choice
// comes from one generator in call order, so both sides see the same world only while they make the same calls.
namespace {
enum : uint32_t {
    evScanNew = 0x80,
    evScanFind,
    evScanEntry,
    evScanPath,
    evScanEnd,
    evTutorial,
    evPathExists,
    evSetName,
    evSegment,
    evGetResource,
    evRecordKey,
    evRecordPath,
};

std::mt19937 gLoad;          // the fakes' choices, in call order
char gHoodNames[kHoods][48]; // what the name fakes answer (SetName changes it)
unsigned gCurrent = 0;       // the hood the persist system handed out last
constexpr uint32_t kKeysTag = 0x40000000;

void SetHoodName(unsigned i, const char *text)
{
    std::snprintf(gHoodNames[i], sizeof(gHoodNames[i]), "%s", text);
    Obj &n = F(kName0 + i);
    n.Set(5, 0, gHoodNames[i]);
    n.Set(6, 0, gHoodNames[i]);
    n.Set(7, 0, uint32_t(std::strlen(gHoodNames[i])));
}

// A directory name made from a hood's name: upper case, as is, another name, or mixed case.
std::string Vary(const char *text)
{
    std::string s(text);
    switch (gLoad() % 4) {
    case 0: for (char &c : s) c = char(std::toupper(uint8_t(c))); break;
    case 1: break;
    case 2: s = kNames[gLoad() % kNameCount]; break;
    default: for (char &c : s) c = char(gLoad() % 2 ? std::toupper(uint8_t(c)) : std::tolower(uint8_t(c))); break;
    }
    return s;
}

uint32_t __fastcall HoodSetName(Obj *self, void *, const char *text)
{
    Ev(evSetName), Ev(self->id), EvText(text, std::strlen(text));
    SetHoodName(self->id - 200 - kHood0, text);
    return 0;
}
uint32_t ResGetKey(Obj *self, uint32_t *args)
{
    auto *key = reinterpret_cast<uint32_t *>(args[0]);
    for (int i = 0; i < 4; ++i) key[i] = self->value[i];
    return args[0];
}
uint32_t PersistGetResource(Obj *, uint32_t *args)
{
    const auto *key = reinterpret_cast<const uint32_t *>(args[0]);
    Ev(evGetResource);
    for (int i = 0; i < 4; ++i) Ev(key[i]);
    if (gLoad() % 6 == 0) return 0;
    gCurrent = gLoad() % kHoods;
    Obj &h = F(kHood0 + gCurrent);
    ++h.refs;
    *reinterpret_cast<void **>(args[2]) = &h;
    return gLoad() % 8 != 0; // now and then hands one out and says it failed
}
uint32_t PersistOpenSegment(Obj *, uint32_t *args)
{
    Ev(evSegment), EvString(reinterpret_cast<void *>(args[3])), EvString(reinterpret_cast<void *>(args[4]));
    ++F(kSegment).refs;
    *reinterpret_cast<void **>(args[1]) = &F(kSegment);
    return 1;
}
uint32_t DatabaseKeys(Obj *, uint32_t *args)
{
    auto *keys = reinterpret_cast<ServiceBlockVector *>(args[0]);
    const unsigned n = gLoad() % 5;
    if (!n) return gLoad() % 3 ? 0 : 1 + gLoad() % 2; // a count without keys
    auto *block = static_cast<uint32_t *>(appdebugger::AllocateListNodes((n * 16 + 31) / 32, 0));
    for (unsigned i = 0; i < n * 4; ++i) block[i] = i % 4 == 0 ? 0xac8a7a2e : gLoad();
    keys->mFirst = block;
    keys->mLast = block + n * 4;
    keys->mEnd = keys->mLast;
    AddRange(block, n * 16, kKeysTag);
    return gLoad() % 4 ? n : gLoad() % (n + 1);
}
uint32_t DatabaseRecord(Obj *, uint32_t *args)
{
    const auto *key = reinterpret_cast<const uint32_t *>(args[0]);
    Ev(evRecordKey);
    for (int i = 0; i < 4; ++i) Ev(key[i]);
    if (gLoad() % 5 == 0) return 0;
    ++F(kRecord).refs;
    *reinterpret_cast<void **>(args[1]) = &F(kRecord);
    return gLoad() % 8 != 0;
}
uint32_t RecordPath(Obj *, uint32_t *args)
{
    std::string text = gLoad() % 5 == 0 ? std::string("Q:/elsewhere/") : std::string(kRoots[F(kCommon).value[0] % 3]);
    text += Vary(gHoodNames[gCurrent]);
    if (gLoad() % 4) text += "/";
    text += "x_Neighborhood.package";
    reinterpret_cast<NeighborhoodName *>(args[0])->mString = text;
    Ev(evRecordPath), EvText(text.data(), text.size());
    return 0;
}

struct DirNode {
    DirNode *next;
    DirNode *prev;
    std::string text;
};
const char *const kDirectories[] = {"D:/hoods", "D:/hoods/", "E:\\x\\", "C:/a/b/"};
uint32_t CommonDirectories(Obj *, uint32_t *args)
{
    auto *list = reinterpret_cast<appdebugger::ListHeader *>(args[1]);
    auto *sentinel = static_cast<DirNode *>(list->mHead);
    const unsigned n = gLoad() % 3;
    for (unsigned i = 0; i < n; ++i) {
        auto *node = static_cast<DirNode *>(appdebugger::AllocateListNodes(1, 0));
        ::new (&node->text) std::string(kDirectories[gLoad() % 4]);
        node->next = sentinel;
        node->prev = sentinel->prev;
        sentinel->prev->next = node;
        sentinel->prev = node;
        ++list->mSize;
    }
    return 1;
}

// ---- the directory scan's choices (tests/ts_dir_scan_stand.h): up to three entries; "*.package" finds list packages, the
// others folders named after the hoods ----
unsigned ScanCount(std::string &, const std::string &) { return gLoad() % 4; }
void ScanMake(dirscan::Entry &e, const std::string &dir, const std::string &pattern, unsigned i)
{
    const bool packages = pattern == "*.package";
    static const uint8_t kFlags[] = {4, 8, 0, 12};
    e.flags = gLoad() % 4 ? (packages ? 8 : 4) : kFlags[gLoad() % 4];
    std::string base = gLoad() % 6 == 0 ? std::string("Z:/elsewhere") : dir;
    if (gLoad() % 2) base += gLoad() % 2 ? "/" : "\\";
    e.path = packages ? base + "h" + std::to_string(i) + ".package" : base + Vary(gHoodNames[gLoad() % kHoods]);
}
void __fastcall StandTutorial(void *mgr, void *) { Ev(evTutorial), Ev(Norm(P(mgr))); }
bool __cdecl StandPathExists(const NeighborhoodName *path)
{
    Ev(evPathExists), EvString(path);
    return gLoad() % 3 != 0;
}

void SetupLoad(std::mt19937 &rng)
{
    gLoad.seed(rng());
    dirscan::Reset();
    for (unsigned i = 0; i < kHoods; ++i) {
        SetHoodName(i, reinterpret_cast<const char *>(F(kName0 + i).ret[6]));
        Obj &h = F(kHood0 + i);
        h.Set(25, 0);
        h.Set(13, "dd");
        h.table[7] = reinterpret_cast<void *>(&HoodSetName);
        Obj &res = F(kRes0 + i);
        res.Set(3, "p", 0, ResGetKey);
        for (int k = 0; k < 4; ++k) res.value[k] = rng();
    }
    Obj &persist = F(kPersist);
    persist.Set(3, 0, &F(kDatabase));
    persist.Set(5, "pdp--", 0, PersistGetResource);
    persist.Set(22, "dpdppdd", 0, PersistOpenSegment);
    F(kDatabase).Set(19, "pd", 0, DatabaseKeys);
    F(kDatabase).Set(34, "pp", 0, DatabaseRecord);
    F(kRecord).Set(11, "p", 0, RecordPath);
    F(kSegment).Set(4, 0, rng());
    F(kCommon).Set(5, "dpd-d", 1, CommonDirectories);
}

struct LoadPatches {
    StandIn exists;
    explicit LoadPatches(difftest::Context &) : exists(0x1c1242, &cRZFile::PathExists, &StandPathExists) {}
};
} // namespace

DIFF_TEST(NeighborhoodManager_HasPackage, 0x4a6000)
{
    World world(ctx);
    LoadPatches patches(ctx);
    for (uint32_t seed = 0; seed < kSeeds; ++seed) {
        const unsigned which = seed % (kHoods + 1);
        Pair(ctx, "HasPackage", seed, SetupLoad, [&]() {
            void *hood = which == kHoods ? nullptr : &F(kHood0 + which);
            using Call = uint8_t(__thiscall *)(Mgr *, void *);
            if (gRoute == 0) return uint32_t(ctx.Original<Call>(0x4a6000)(M(), hood));
            if (gRoute == 1) return uint32_t(M()->HasPackage(hood));
            return uint32_t(reinterpret_cast<Call>(ADAPTER(HasPackage))(M(), hood));
        });
    }
}

DIFF_TEST(NeighborhoodManager_Load, 0x4a4160)
{
    World world(ctx);
    LoadPatches patches(ctx);
    StandIn tutorial(0x4a27a0, &cTSNeighborhoodManager::AddTutorial, &StandTutorial);
    dirscan::gEvBase = evScanNew;
    dirscan::gCount = &ScanCount;
    dirscan::gMake = &ScanMake;
    dirscan::Patches scan;
    for (uint32_t seed = 0; seed < kSeeds * 2; ++seed)
        Pair(ctx, "Load", seed, SetupLoad, [&]() {
            Run<void(__thiscall *)(Mgr *)>(ctx, 0x4a4160, 0, NO_ADAPTER);
            return 0;
        });
}
