// Differential tests for the COM directors (src/rz/COMDirectors.*, COMDirectorData.cpp).
//
//  - the base class: AddCls (0x1ae080), the variant with kind 2 (0x1ae0ad), AddDirector (0x1ae0da)
//    and the slots QueryInterface (0x1ae298), InitializeCOM (0x1ae24d), EnumClassObjects (0x1ae148),
//    GetClassObject (0x1ae199), CanUnloadNow (0x1ae11d), RemoveRef (0x1ae2f5), GetLibraryPath
//    (0x1ae22c), GetHeapAllocatedSize (0x1ae227), the shared stubs 0x145260, 0x1ae2f0, 0x148260,
//    0x144910, 0x46390, 0x147800, the deleting destructors and the +4 thunks;
//  - the 41 derived directors, table-driven (TEST_DIRECTORS below): every constructor that is a
//    function of its own, every getter (first use, with the whole tree of sub-directors that it
//    builds), the static initialiser of the MadCow director, the deleting destructor, the
//    destructor body and the id thunk of each, the OnStart (slot 4) of the fifteen directors that only
//    create and register services, and the two helpers the application director calls (0x140fe3,
//    0x140ff3).
//
// Method. Each test runs the original and the reimplementation on identical memory (the same
// random fill, built by the original's base constructor where an object is needed) and compares
// the whole object: the first 0x38 bytes, the sub-director list (the pointers), and the class map
// by its content in list order (class id, factory, kind) with the bucket vector described by node
// index instead of address. Calls the code makes through vtables go to fakes that log their
// arguments; the logs are compared. Factories are never called by the constructor tests (they are
// only stored); the GetClassObject tests use fakes of our own.
//
// Standalone shape (lane EO): the base, the root and the 41 directors are our classes (rz/RZCOMDllDirectorShape.h).
// Our side is built by our constructor (the constructor hooks' adapters, or a director class built without its steps,
// rz::NoSteps, where only the slots matter) and called through its own vtable; a second route calls each hook's adapter
// on a director built by the original, whose vtables are copies of the game's with the forwarded slot replaced by a
// trap of the slot's arity (an adapter that dispatched would hit it). The vtable words at +0 and +4 are the only bytes
// normalised (VCode: the game's and ours of one class and face map to one code). The +4 thunks are the compiler's:
// 0x144913 (QueryInterface), 0x1448e9 (AddRef), 0x144975 (Release), exercised by QueryInterface and RefCount.
//
// The directors' statics (the objects at the RVAs below and their guards) are reset before the
// getter tests and are left built by the constructor tests (the original builds the sub-directors
// a constructor adds; ours then finds them built). The image's atexit (0x824cbe) is patched to
// record instead of run while the original runs: its CRT is not initialised in the harness. Ours
// registers through gRegisterExitHandler, which is recorded as well.
#include "ts/cTSAppFW.h"
#include "app/AppCOMDirectors.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "hook/hooks.h" // MemberAddress only
#include "rz/COMDirectors.h"
#include "rz/RZCOMDllDirectorShape.h"
#include "rz/MiscHelpersA.h"
#include "rz/cRZCmdLine.h"
#include "rz/cRZString.h"

#include <intrin.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <set>
#include <cstring>
#include <deque>
#include <functional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

// The directors, as the original has them (generated from the binary; RVAs are 1.18.0.187):
// X(index, short name, constructor, getter, object, guard, id, id thunk, deleting destructor, destructor body)
// constructor is 0 when the constructor is inlined into the getter (or the initialiser, for MadCow);
// guard is 0 for the static initialiser; the destructor body is 0 when the destructor calls 0x1ae048.
#define TEST_DIRECTORS(X) \
    X(0, MadCow, 0x0, 0xc5a240, 0x10fff78, 0x0, 0xcc361e28, 0xc5a250, 0x18e5f0, 0x0) \
    X(1, Draw2D, 0x0, 0x145dd0, 0x10c9438, 0x10c949c, 0x9d309433, 0x145dc0, 0x145d90, 0x0) \
    X(2, Graphics, 0x0, 0x154290, 0x10c9650, 0x10c96b4, 0x3b63609c, 0x154280, 0x154250, 0x0) \
    X(3, CompGeom, 0x18e4f0, 0x18e620, 0x10caad8, 0x10cab3c, 0xdce48ec2, 0x18e610, 0x18e5f0, 0x0) \
    X(4, Event, 0x1a8346, 0x1a839b, 0x10cad50, 0x10cadb4, 0x3b1e8612, 0x1a8395, 0x1448ca, 0x0) \
    X(5, Expat, 0x1ab195, 0x1ab2c3, 0x10cae30, 0x10cae94, 0xcb6b39f6, 0x1ab2bd, 0x1ab22a, 0x0) \
    X(6, TSWeb, 0x1cb815, 0x1cb89d, 0x10cb6c0, 0x10cb724, 0x0c59fa79, 0x1cb897, 0x1cb878, 0x1cb866) \
    X(7, TSPersist, 0x0, 0x1d3b70, 0x10cc098, 0x10cc0fc, 0xa4fbe888, 0x1d3b50, 0x1d2410, 0x0) \
    X(8, PathPlan, 0x1fc3d0, 0x1fc490, 0x10cc150, 0x10cc1b4, 0xc93ef224, 0x1fc520, 0x1fc470, 0x1fc450) \
    X(9, TSSG, 0x244e30, 0x245430, 0x10cc7d0, 0x10cc834, 0xbb50c92d, 0x245420, 0x18e5f0, 0x0) \
    X(10, TSSim, 0x415ae0, 0x4162a0, 0x10da750, 0x10da7b4, 0xeb4d03f0, 0x416270, 0x416120, 0x0) \
    X(11, TSUI, 0x5b4a34, 0x5b6847, 0x10dbfe0, 0x10dc044, 0x63caeeec, 0x5b66a9, 0x5b61a4, 0x5b5c9e) \
    X(12, TSWorld, 0x7e5b50, 0x7e5c50, 0x10df3c8, 0x10df42c, 0x7d3f4d03, 0x7e5c40, 0x18e5f0, 0x0) \
    X(13, Script, 0x859fa6, 0x85a013, 0x10e67b8, 0x10e681c, 0xfa2e78c2, 0x85a00d, 0x1448ca, 0x0) \
    X(14, UIScript, 0x85b203, 0x85b2fb, 0x10e6828, 0x10e688c, 0x9a3559b0, 0x85b2f5, 0x85b260, 0x0) \
    X(15, GX, 0x87b39a, 0x87b45a, 0x10e68b0, 0x10e6914, 0x43cac0b9, 0x87b454, 0x87b3e6, 0x0) \
    X(16, Draw, 0x0, 0x8b7f30, 0x10ecc20, 0x10ecc84, 0x0b2ea939, 0x8b8020, 0x18e5f0, 0x0) \
    X(17, Font, 0x909f57, 0x909fee, 0x10ecc98, 0x10eccfc, 0x5aa40bd3, 0x909fe8, 0x1448ca, 0x0) \
    X(18, Win, 0x92f3a7, 0x92f880, 0x10f4d28, 0x10f4d8c, 0xa41740d1, 0x92f87a, 0x1448ca, 0x0) \
    X(19, Win32, 0x970cda, 0x970d63, 0x10f4e20, 0x10f4e84, 0x041740ba, 0x970d5d, 0x1448ca, 0x0) \
    X(20, Numerics, 0x98be40, 0x98beb0, 0x10f4f50, 0x10f4fb4, 0xfd21f3f0, 0x98bea0, 0x18e5f0, 0x0) \
    X(21, ResMan, 0x0, 0x9b3210, 0x10f5038, 0x10f509c, 0xc3caec3b, 0x9b3200, 0x18e5f0, 0x0) \
    X(22, ResourceW32, 0x0, 0x9b34b0, 0x10f50a0, 0x10f5104, 0xa431a334, 0x9b34a0, 0x18e5f0, 0x0) \
    X(23, XmlUtils, 0x9c51c7, 0x9c52d1, 0x10f5128, 0x10f518c, 0xcc7e8cd1, 0x9c52cb, 0x1ab22a, 0x0) \
    X(24, SceneDBAnim, 0x9d0c60, 0x9d1140, 0x10f5a40, 0x10f5aa4, 0xfb50af8f, 0x9d1130, 0x18e5f0, 0x0) \
    X(25, SceneGraph, 0xa33bf0, 0xa34180, 0x10f5ee8, 0x10f5f4c, 0xdb4ce149, 0xa34170, 0x18e5f0, 0x0) \
    X(26, CommandParser, 0x0, 0xb0a900, 0x10faa78, 0x10faadc, 0x69cfc8f9, 0xb0a8f0, 0xb0a8c0, 0x0) \
    X(27, Freshness, 0xb0e046, 0xb0e170, 0x10fab10, 0x10fab74, 0xfd53ff17, 0xb0e1ca, 0xb0e0b4, 0x0) \
    X(28, SndEa, 0xb12b68, 0xb12c05, 0x10fabc8, 0x10fac2c, 0x29a6f77e, 0xb12bff, 0x1448ca, 0x0) \
    X(29, SpeexCodec, 0xb167ef, 0xb1683f, 0x10faca8, 0x10fad0c, 0x6d4df6db, 0xb16839, 0x1448ca, 0x0) \
    X(30, Sound, 0xb22254, 0xb223c6, 0x10fad20, 0x10fad84, 0xe9d3a40b, 0xb223c0, 0x1448ca, 0x0) \
    X(31, HitMan, 0xb29d78, 0xb2a3c8, 0x10fadb0, 0x10fae14, 0x9b2d3d2f, 0xb2a3c2, 0x1448ca, 0x0) \
    X(32, HitMp3, 0xb3810a, 0xb38630, 0x10faef8, 0x10faf5c, 0x1cec20be, 0xb3862a, 0x1ab22a, 0x0) \
    X(33, TSEffects, 0xbb4710, 0xbb4840, 0x10fc770, 0x10fc7d4, 0xcb8b5e1e, 0xbb4830, 0xbb4810, 0xbb47f0) \
    X(34, TSMetaData, 0xc0c0b1, 0xc0c10b, 0x10fd4f0, 0x10fd554, 0x0a5a5504, 0xc0c17e, 0x1448ca, 0x0) \
    X(35, TSAppFW, 0xc2bba4, 0xc2be73, 0x10fdf60, 0x10fdfc4, 0x64ef0af8, 0xc2bee6, 0xc2bd0f, 0xc2bcce) \
    X(36, RZMessage, 0xc6eb80, 0xc6ebe7, 0x11031e0, 0x1103244, 0x252eba2e, 0xc6ebe1, 0x1448ca, 0x0) \
    X(37, Lua5, 0x0, 0xc76e40, 0x1103260, 0x11032c4, 0xab27e940, 0xc76e30, 0xc76d00, 0x0) \
    X(38, TSCommon, 0xcb4ec0, 0xcb4e00, 0x1107528, 0x110758c, 0xc93ef224, 0x1fc520, 0xcb50f0, 0xcb50d0) \
    X(39, TSCAS, 0xcb52d0, 0xcb5410, 0x1107598, 0x11075fc, 0xabe2fc1d, 0xcb5400, 0xcb53e0, 0xcb53c0) \
    X(40, TSUICommon, 0xd0acb0, 0xd0ad20, 0x1107d78, 0x1107ddc, 0xcde2eefc, 0xd0ad10, 0xd0acf0, 0xd0acd0)

namespace {

using namespace comdir;

constexpr size_t kSize = 0x64;
constexpr size_t kBlock = 0x84; // the object, then canary bytes
constexpr uint8_t kCanary = 0xCD;

// ---------------------------------------------------------------------------------------------
// The table

struct TestDirector {
    unsigned index;
    const char *name;
    uint32_t ctor, getter, object, guard, id, thunk, deleting, body;
};

#define COMDIR_ROW(i, name, ctor, getter, object, guard, id, thunk, deleting, body) \
    {i, #name, ctor, getter, object, guard, id, thunk, deleting, body},
const TestDirector kTest[] = {TEST_DIRECTORS(COMDIR_ROW)};
#undef COMDIR_ROW
constexpr unsigned kTestCount = sizeof(kTest) / sizeof(kTest[0]);
static_assert(kTestCount == kDirectorCount, "the test table lists every director");

// Each director's game vtables (primary, and the subobject's at +4), in kDirectorSpecs order: the tests map them to
// ours. Kept here, not in comdir::kDirectorSpecs, so the host holds no game vtable it never stores (tools/callers.py
// counts every vtable our build holds as live).
struct GameVTables {
    uint32_t vtable;
    uint32_t vtable4;
};
const GameVTables kGameVTables[] = {
    {0xe9d1b0, 0xe9d1f8}, // kDir_MadCow
    {0xd9ba40, 0xd9ba88}, // kDir_Draw2D
    {0xd9c8fc, 0xd9c944}, // kDir_Graphics
    {0xda1148, 0xda1190}, // kDir_CompGeom
    {0xda2134, 0xda217c}, // kDir_Event
    {0xda2524, 0xda256c}, // kDir_Expat
    {0xda467c, 0xda46c4}, // kDir_TSWeb
    {0xda51cc, 0xda5214}, // kDir_TSPersist
    {0xda73dc, 0xda7424}, // kDir_PathPlan
    {0xdadf74, 0xdadfbc}, // kDir_TSSG
    {0xdc2c88, 0xdc2cd0}, // kDir_TSSim
    {0xdd8804, 0xdd884c}, // kDir_TSUI
    {0xe037c0, 0xe03808}, // kDir_TSWorld
    {0xe47da4, 0xe47dec}, // kDir_Script
    {0xe47ee0, 0xe47f28}, // kDir_UIScript
    {0xe4a31c, 0xe4a364}, // kDir_GX
    {0xe4ebf8, 0xe4ec40}, // kDir_Draw
    {0xe50760, 0xe507a8}, // kDir_Font
    {0xe52ed8, 0xe52f20}, // kDir_Win
    {0xe5b188, 0xe5b1d0}, // kDir_Win32
    {0xe5d004, 0xe5d04c}, // kDir_Numerics
    {0xe60044, 0xe6008c}, // kDir_ResMan
    {0xe600c4, 0xe6010c}, // kDir_ResourceW32
    {0xe6166c, 0xe616b4}, // kDir_XmlUtils
    {0xe61d30, 0xe61d78}, // kDir_SceneDBAnim
    {0xe686ec, 0xe68734}, // kDir_SceneGraph
    {0xe7f7c4, 0xe7f80c}, // kDir_CommandParser
    {0xe7fa34, 0xe7fa7c}, // kDir_Freshness
    {0xe7fee4, 0xe7ff2c}, // kDir_SndEa
    {0xe8049c, 0xe804e4}, // kDir_SpeexCodec
    {0xe809e8, 0xe80a30}, // kDir_Sound
    {0xe815f8, 0xe81640}, // kDir_HitMan
    {0xe838e4, 0xe8392c}, // kDir_HitMp3
    {0xe8bc94, 0xe8bcdc}, // kDir_TSEffects
    {0xe91aac, 0xe91af4}, // kDir_TSMetaData
    {0xe991fc, 0xe99244}, // kDir_TSAppFW
    {0xe9eedc, 0xe9ef24}, // kDir_RZMessage
    {0xea05e0, 0xea0628}, // kDir_Lua5
    {0xeb7400, 0xeb7448}, // kDir_TSCommon
    {0xeb747c, 0xeb74c4}, // kDir_TSCAS
    {0xebb274, 0xebb2bc}, // kDir_TSUICommon
};
static_assert(sizeof(kGameVTables) / sizeof(kGameVTables[0]) == kDirectorCount, "one row per director");

#define COMDIR_CHECK(i, name, ctor, getter, object, guard, id, thunk, deleting, body) \
    static_assert(kDir_##name == i, "index of " #name);
TEST_DIRECTORS(COMDIR_CHECK)
#undef COMDIR_CHECK

using Base = rz::cRZCOMDllDirector;
using BaseAdapters = rz::cRZCOMDllDirectorAdapters;

// Our constructor of each director (the constructor hook's adapter).
using OursCtor = void *(*)(void *);
#define COMDIR_OURS_CTOR(i, name, ctor, getter, object, guard, id, thunk, deleting, body) \
    [](void *p) -> void * { return rz::cCOMDirectorAdapters<i>::Construct(p, nullptr); },
const OursCtor kOursCtor[] = {TEST_DIRECTORS(COMDIR_OURS_CTOR)};
#undef COMDIR_OURS_CTOR

// Our class of each director, built without its registration steps (for the slots that do not read them).
using OursNoSteps = Base *(*)(void *);
#define COMDIR_OURS_NOSTEPS(i, name, ctor, getter, object, guard, id, thunk, deleting, body) \
    [](void *p) -> Base * { return ::new (p) rz::cCOMDirectorT<i>(rz::NoSteps{}); },
const OursNoSteps kOursNoSteps[] = {TEST_DIRECTORS(COMDIR_OURS_NOSTEPS)};
#undef COMDIR_OURS_NOSTEPS

// The adapters of each director's hooks (id thunk, OnStart, destructor body, deleting destructor).
struct DirectorAdapters {
    uint32_t(__fastcall *id)(cIGZCOMDirector *, void *);
    bool(__fastcall *onStart)(cIGZCOMDirector *, void *, cIGZCOM *);
    void(__fastcall *destruct)(cIGZCOMDirector *, void *);
    void *(__fastcall *deleting)(cIGZCOMDirector *, void *, uint32_t);
};
#define COMDIR_ADAPTERS(i, name, ctor, getter, object, guard, id, thunk, deleting, body) \
    {&rz::cCOMDirectorAdapters<i>::GetDirectorID, &rz::cCOMDirectorAdapters<i>::OnStart, &rz::cCOMDirectorAdapters<i>::Destruct, \
     reinterpret_cast<void *(__fastcall *)(cIGZCOMDirector *, void *, uint32_t)>(&rz::cCOMDirectorAdapters<i>::DeletingDestructor)},
const DirectorAdapters kAdapters[] = {TEST_DIRECTORS(COMDIR_ADAPTERS)};
#undef COMDIR_ADAPTERS

// Hooks dropped (lane GP): OnStart and deleting-destructor slots only the game's vtables reach; no director carries those
// while its constructor (or getter) hook is in, so the originals are trapped under that switch ("in_effect": "callers").
// They have no adapter route; ours (through our vtable) stays.
constexpr uint32_t kDroppedAdapters[] = {0x145f20, 0x145d90, 0x154460, 0x154250, 0x1a83f5, 0x1d2410, 0x8b8030, 0x90a048,
    0x92f8da, 0x9b33b0, 0x9d11d0, 0xa34210, 0xb0a8c0, 0xb12c5f, 0xb22420, 0xb2a422, 0xc6ec41, 0x1d3e20, 0x2454c0,
    // lane GT: slots resting on two hooks (constructor and destructor body, or constructor and getter), trapped under both
    0x1cb910, 0x1cb878, 0x1fc470, 0x416120, 0x5b61a4, 0x87b3e6, 0xbb4810, 0xc2bd0f, 0xcb50f0, 0xcb53e0, 0xd0acf0, 0xcb54a0,
    0x5b7722, 0x5b72e7, 0x5b763c};
bool Dropped(uint32_t rva)
{
    for (uint32_t r : kDroppedAdapters)
        if (r == rva) return true;
    return false;
}

// ---------------------------------------------------------------------------------------------
// Memory and canonical form

using OrigBase = void *(__thiscall *)(void *);

uint8_t *NewBlock(uint32_t seed)
{
    auto *p = static_cast<uint8_t *>(::operator new(kBlock));
    std::mt19937 rng(seed);
    for (size_t i = 0; i < kSize; ++i) p[i] = static_cast<uint8_t>(rng());
    std::memset(p + kSize, kCanary, kBlock - kSize);
    return p;
}

bool CanaryIntact(const uint8_t *p)
{
    for (size_t i = kSize; i < kBlock; ++i)
        if (p[i] != kCanary) return false;
    return true;
}

// A director built by the original's base constructor (0x1adffa) on a random fill.
uint8_t *MakeDirector(difftest::Context &ctx, uint32_t seed)
{
    uint8_t *p = NewBlock(seed);
    ctx.Original<OrigBase>(0x1adffa)(p);
    return p;
}

// Our base director (the 0x1adffa hook's adapter: our constructor) on the same fill.
uint8_t *MakeOurs(uint32_t seed)
{
    uint8_t *p = NewBlock(seed);
    BaseAdapters::Construct(p, nullptr);
    return p;
}

// Our director `index` built without its steps (kDirectorCount: our base director).
uint8_t *MakeOursOf(uint32_t seed, unsigned index)
{
    if (index >= kDirectorCount) return MakeOurs(seed);
    uint8_t *p = NewBlock(seed);
    kOursNoSteps[index](p);
    return p;
}

// Either side's director: the base destructor (a qualified call: no dispatch), then the block.
void FreeDirector(uint8_t *p)
{
    reinterpret_cast<Base *>(p)->Base::~cRZCOMDllDirector();
    ::operator delete(p);
}

// Traps for the adapter route, one per stack-argument count.
bool gTrapHit = false;
uint32_t __fastcall Trap0(void *, void *) { gTrapHit = true; return 0x7a7a; }
uint32_t __fastcall Trap1(void *, void *, void *) { gTrapHit = true; return 0x7a7a; }
uint32_t __fastcall Trap2(void *, void *, void *, void *) { gTrapHit = true; return 0x7a7a; }
uint32_t __fastcall Trap3(void *, void *, void *, void *, void *) { gTrapHit = true; return 0x7a7a; }
void *TrapOf(int arity)
{
    switch (arity) {
    case 0: return reinterpret_cast<void *>(&Trap0);
    case 1: return reinterpret_cast<void *>(&Trap1);
    case 2: return reinterpret_cast<void *>(&Trap2);
    default: return reinterpret_cast<void *>(&Trap3);
    }
}
void *gTrapVt0[18], *gTrapVt4[11];
uint32_t gTrapRva0 = 0xda265c, gTrapRva4 = 0xda26a4;

// The adapter route's object: built by the original's base constructor, carrying copies of the game's vtables `vt0` /
// `vt4` with slot `slot` of face `face` (0 or 4) replaced by a trap of `arity` stack arguments.
uint8_t *Trapped(uint8_t *p, int face, int slot, int arity, uint32_t vt0 = 0xda265c, uint32_t vt4 = 0xda26a4)
{
    std::memcpy(gTrapVt0, GameAddress<void *>(vt0), vt0 == 0xd9b754 ? 18 * 4 : 17 * 4);
    std::memcpy(gTrapVt4, GameAddress<void *>(vt4), sizeof(gTrapVt4));
    (face == 0 ? gTrapVt0 : gTrapVt4)[slot] = TrapOf(arity);
    *reinterpret_cast<void **>(p) = gTrapVt0;
    *reinterpret_cast<void **>(p + 4) = gTrapVt4;
    gTrapRva0 = vt0;
    gTrapRva4 = vt4;
    gTrapHit = false;
    return p;
}
uint8_t *MakeTrapped(difftest::Context &ctx, uint32_t seed, int face, int slot, int arity)
{
    return Trapped(MakeDirector(ctx, seed), face, slot, arity);
}
// The game's vtables back (the trap copies would not be recognised by Take).
void Untrap(uint8_t *p)
{
    if (*reinterpret_cast<void **>(p) == gTrapVt0) *reinterpret_cast<void **>(p) = GameAddress<void>(gTrapRva0);
    if (*reinterpret_cast<void **>(p + 4) == gTrapVt4) *reinterpret_cast<void **>(p + 4) = GameAddress<void>(gTrapRva4);
}

struct Canon {
    uint8_t fixed[0x38];
    std::string text; // the path string's text
    std::vector<void *> subs;
    float maxLoad = 0;
    uint32_t size = 0, mask = 0, maxIdx = 0, bucketBytes = 0;
    bool headLinked = false, endIsLast = false;
    std::vector<std::array<uint32_t, 3>> nodes; // class id, factory, kind in list order
    std::vector<int> prevIdx, nextIdx, buckets;  // node indexes (-1: the sentinel)
};

template <typename T> T Read(const uint8_t *p, size_t off)
{
    T v;
    std::memcpy(&v, p + off, sizeof(v));
    return v;
}

// Vtable words: the game's and ours of one class and face map to one code; anything else stays raw.
std::unordered_map<uint32_t, uint32_t> gVCode;
constexpr uint32_t kBaseCode = 0xc0de0000, kRootCode = 0xc0de0010;
uint32_t W(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

void *__fastcall NullComObject(void *, void *) { return nullptr; }
void LearnVTables()
{
    if (!gVCode.empty()) return;
    alignas(8) static uint8_t buf[0x64];
    auto learn = [](uint32_t code) {
        gVCode[Read<uint32_t>(buf, 0)] = code;
        gVCode[Read<uint32_t>(buf, 4)] = code + 1;
        reinterpret_cast<Base *>(buf)->Base::~cRZCOMDllDirector();
    };
    gVCode[W(GameAddress<void>(0xda265c))] = kBaseCode;
    gVCode[W(GameAddress<void>(0xda26a4))] = kBaseCode + 1;
    gVCode[W(GameAddress<void>(0xd9b754))] = kRootCode;
    gVCode[W(GameAddress<void>(0xd9b7a0))] = kRootCode + 1;
    for (unsigned i = 0; i < kDirectorCount; ++i) {
        gVCode[W(GameAddress<void>(kGameVTables[i].vtable))] = 0xc0de1000 + 2 * i;
        gVCode[W(GameAddress<void>(kGameVTables[i].vtable4))] = 0xc0de1001 + 2 * i;
    }
    BaseAdapters::Construct(buf, nullptr);
    learn(kBaseCode);
    for (unsigned i = 0; i < kDirectorCount; ++i) {
        kOursNoSteps[i](buf);
        learn(0xc0de1000 + 2 * i);
    }
    // The root's constructor asks the framework (the service object) for its COM object: a fake answering null.
    static void *comVtable[32];
    comVtable[23] = reinterpret_cast<void *>(&NullComObject);
    static void *fake[1] = {comVtable};
    void **global = GameAddress<void *>(0x10c9378);
    void *saved = *global;
    *global = fake;
    rz::cGZCOMDirectorAdapters::Construct(buf, nullptr);
    *global = saved;
    learn(kRootCode);
    // Distinct game vtables may hold the same code twice only if ours were the game's: they never are.
}

uint32_t VCode(uint32_t word)
{
    LearnVTables();
    auto it = gVCode.find(word);
    return it == gVCode.end() ? word : it->second;
}

void NormaliseVTables(uint8_t *bytes)
{
    for (size_t off : {size_t(0), size_t(4)}) {
        uint32_t code = VCode(Read<uint32_t>(bytes, off));
        std::memcpy(bytes + off, &code, 4);
    }
}

Base *AsBase(uint8_t *p) { return reinterpret_cast<Base *>(p); }
std::vector<Base *> &Subs(uint8_t *p) { return AsBase(p)->mChildren; }
ClassMap &ClassesOf(uint8_t *p) { return AsBase(p)->mClasses; }
cIGZCOMDirector *Face0(uint8_t *p) { return reinterpret_cast<cIGZCOMDirector *>(p); }
cIGZFrameWorkHooks *Face4(uint8_t *p) { return reinterpret_cast<cIGZFrameWorkHooks *>(p + 4); }

// Slot `slot` of the face at `face`, called through the face's own vtable.
template <typename R, typename... A> R CallSlot(const void *face, int slot, A... args)
{
    auto fn = reinterpret_cast<R(__thiscall *)(const void *, A...)>((*static_cast<void *const *const *>(face))[slot]);
    return fn(face, args...);
}

Canon Take(const uint8_t *d)
{
    Canon c;
    std::memcpy(c.fixed, d, sizeof(c.fixed));
    NormaliseVTables(c.fixed);
    // The path string (std::string at +0x14): its text, and the inline buffer cleared when the text
    // lives on the heap (the buffer then holds a heap pointer).
    uint32_t len = Read<uint32_t>(d, 0x24), cap = Read<uint32_t>(d, 0x28);
    const char *data = cap > 15 ? Read<const char *>(d, 0x14) : reinterpret_cast<const char *>(d + 0x14);
    c.text.assign(data, len);
    if (cap > 15) std::memset(c.fixed + 0x14, 0, 16);
    auto **first = Read<void **>(d, 0x38);
    auto **last = Read<void **>(d, 0x3c);
    for (auto **p = first; p != last; ++p) c.subs.push_back(*p);

    c.maxLoad = Read<float>(d, 0x44);
    void *head = Read<void *>(d, 0x48);
    c.size = Read<uint32_t>(d, 0x4c);
    auto **vfirst = Read<void **>(d, 0x50);
    auto **vlast = Read<void **>(d, 0x54);
    auto **vend = Read<void **>(d, 0x58);
    c.mask = Read<uint32_t>(d, 0x5c);
    c.maxIdx = Read<uint32_t>(d, 0x60);
    c.bucketBytes = static_cast<uint32_t>(reinterpret_cast<uint8_t *>(vlast) - reinterpret_cast<uint8_t *>(vfirst));
    c.endIsLast = vend == vlast;
    if (head == nullptr) return c;
    auto **sentinel = static_cast<void **>(head);
    c.headLinked = sentinel[0] == head && sentinel[1] == head;

    std::unordered_map<void *, int> index;
    index[head] = -1;
    std::vector<void **> order;
    for (void **n = static_cast<void **>(sentinel[0]); n != sentinel && order.size() < 100000; n = static_cast<void **>(n[0])) {
        index[n] = static_cast<int>(order.size());
        order.push_back(n);
    }
    for (void **n : order) {
        c.nodes.push_back({static_cast<uint32_t>(reinterpret_cast<uintptr_t>(n[2])), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(n[3])),
                           static_cast<uint32_t>(reinterpret_cast<uintptr_t>(n[4]))});
        auto find = [&](void *p) {
            auto it = index.find(p);
            return it == index.end() ? -2 : it->second;
        };
        c.nextIdx.push_back(find(n[0]));
        c.prevIdx.push_back(find(n[1]));
    }
    for (auto **p = vfirst; p != vlast; ++p) {
        auto it = index.find(*p);
        c.buckets.push_back(it == index.end() ? -2 : it->second);
    }
    return c;
}

// Describes the first difference, or returns an empty string.
std::string Diff(const Canon &a, const Canon &b)
{
    char buf[200];
    if (std::memcmp(a.fixed, b.fixed, sizeof(a.fixed)) != 0) {
        for (size_t i = 0; i < sizeof(a.fixed); ++i)
            if (a.fixed[i] != b.fixed[i]) {
                std::snprintf(buf, sizeof(buf), "byte +0x%zx: original %02x, ours %02x", i, a.fixed[i], b.fixed[i]);
                return buf;
            }
    }
    if (a.text != b.text) return "path string differs";
    if (a.subs != b.subs) {
        std::snprintf(buf, sizeof(buf), "sub-directors: original %zu, ours %zu", a.subs.size(), b.subs.size());
        return buf;
    }
    if (a.size != b.size || a.nodes.size() != b.nodes.size()) {
        std::snprintf(buf, sizeof(buf), "class count: original %u (%zu nodes), ours %u (%zu nodes)", a.size, a.nodes.size(), b.size, b.nodes.size());
        return buf;
    }
    for (size_t i = 0; i < a.nodes.size(); ++i)
        if (a.nodes[i] != b.nodes[i]) {
            std::snprintf(buf, sizeof(buf), "node %zu: original (%08x, %08x, %u), ours (%08x, %08x, %u)", i, a.nodes[i][0], a.nodes[i][1],
                          a.nodes[i][2], b.nodes[i][0], b.nodes[i][1], b.nodes[i][2]);
            return buf;
        }
    if (a.nextIdx != b.nextIdx || a.prevIdx != b.prevIdx) return "list links differ";
    if (a.buckets != b.buckets) return "bucket vector differs";
    if (a.maxLoad != b.maxLoad || a.mask != b.mask || a.maxIdx != b.maxIdx || a.bucketBytes != b.bucketBytes || a.headLinked != b.headLinked ||
        a.endIsLast != b.endIsLast) {
        std::snprintf(buf, sizeof(buf), "set shape: mask %u/%u max %u/%u buckets %u/%u", a.mask, b.mask, a.maxIdx, b.maxIdx, a.bucketBytes, b.bucketBytes);
        return buf;
    }
    return std::string();
}

// A destroyed director: the 0x64 bytes with the list head word (freed) and the string's upper three
// bytes of its freed pointer masked.
struct Snap {
    uint8_t bytes[kSize];
};

Snap SnapDestroyed(const uint8_t *object)
{
    Snap s;
    std::memcpy(s.bytes, object, sizeof(s.bytes));
    NormaliseVTables(s.bytes);
    std::memset(s.bytes + 0x48, 0, 4);
    std::memset(s.bytes + 0x15, 0, 3);
    return s;
}

bool SameSnap(const Snap &a, const Snap &b)
{
    return std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) == 0;
}

// ---------------------------------------------------------------------------------------------
// The image's atexit and ours

std::vector<void (*)()> gImageHandlers;
int __cdecl StubAtexit(void (*handler)())
{
    gImageHandlers.push_back(handler);
    return 0;
}

std::vector<void (*)()> gOursHandlers;
int RecordOurRegistration(void (*handler)())
{
    gOursHandlers.push_back(handler);
    return 0;
}

struct AtexitRedirect {
    uint8_t *site;
    uint8_t saved[5];
    explicit AtexitRedirect(difftest::Context &ctx) : site(ctx.image.At<uint8_t>(0x824cbe))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&StubAtexit) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
        gImageHandlers.clear();
    }
    ~AtexitRedirect() { std::memcpy(site, saved, 5); }
};

// Redirects our registrations while alive.
struct OursRedirect {
    OursRedirect()
    {
        gOursHandlers.clear();
        gRegisterExitHandler = &RecordOurRegistration;
    }
    ~OursRedirect() { gRegisterExitHandler = &atexit; }
};

// The thread's TLS copy of the statics epoch (offset 4 of the image's TLS block): -1 makes a zero
// guard take the slow path; a built static's guard (a large negative number) stays on the fast path.
struct EpochScope {
    int *tls;
    int saved;
    explicit EpochScope(difftest::Context &ctx)
    {
        uint32_t index = *ctx.image.At<uint32_t>(0x10df814);
        auto **array = reinterpret_cast<int **>(__readfsdword(0x2c));
        tls = array[index];
        saved = tls[1];
        tls[1] = -1;
    }
    void Reset() { tls[1] = -1; }
    ~EpochScope() { tls[1] = saved; }
};

uint8_t *StaticOf(difftest::Context &ctx, unsigned i) { return ctx.image.At<uint8_t>(kTest[i].object); }

void ResetStatics(difftest::Context &ctx)
{
    for (unsigned i = 0; i < kTestCount; ++i) {
        std::memset(StaticOf(ctx, i), 0xCD, kSize);
        if (kTest[i].guard) *ctx.image.At<int>(kTest[i].guard) = 0;
    }
}

// A static is built once its +0 word is a vtable: ResetStatics fills it with 0xCD, and the image's own storage is zero
// until something builds it (a fresh image, before any test has reset the statics: EnsureMadCow must build the MadCow
// director then too, or the application director's constructor adds a director with a null vtable and the original's
// AddDirector faults at 0x1ae0f1).
bool IsBuilt(difftest::Context &ctx, unsigned i)
{
    uint32_t word = Read<uint32_t>(StaticOf(ctx, i), 0);
    return word != 0xCDCDCDCD && word != 0;
}

// The MadCow director is built by a static initialiser before main in the game, and the application
// director's constructor adds it: the harness runs no initialisers, so build it with the original's
// (its atexit registration is discarded).
void EnsureMadCow(difftest::Context &ctx)
{
    if (IsBuilt(ctx, kDir_MadCow)) return;
    AtexitRedirect redirect(ctx);
    ctx.Original<void(__cdecl *)()>(0x1408d0)();
    gImageHandlers.clear();
}

// ---------------------------------------------------------------------------------------------
// Fakes: logs and objects called through vtables

std::vector<uint32_t> gLog;
void Log(std::initializer_list<uint32_t> values) { gLog.insert(gLog.end(), values); }
uint32_t U(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

const void *gExpectedPath = nullptr; // the address of the director's own string
void **gExpectedOut = nullptr;
EnumCallback gExpectedCallback = nullptr;
std::unordered_map<const void *, bool> gSubResult; // what a fake sub-director returns
void *const kOutMarker = reinterpret_cast<void *>(0x5a5a7777);
void *const kFrameworkMarker = reinterpret_cast<void *>(0x6b6b1234);

// A fake sub-director: a director-shaped object (so AddDirector can read its sub list) with a
// fake vtable. Slots: 1 AddRef, 2 Release, 3 InitializeCOM, 5 EnumClassObjects, 6 GetClassObject,
// 7 CanUnloadNow.
bool __fastcall FakeInit(void *self, void *, void *framework, void *path)
{
    Log({1, U(self), U(framework), path == gExpectedPath ? 1u : 0u});
    return true;
}
void __fastcall FakeEnum(void *self, void *, EnumCallback cb, void *context)
{
    Log({2, U(self), cb == gExpectedCallback ? 1u : 0u, U(context)});
}
bool __fastcall FakeGet(void *self, void *, uint32_t clsid, uint32_t iid, void **out)
{
    bool result = gSubResult[self];
    Log({3, U(self), clsid, iid, out == gExpectedOut ? 1u : 0u, result ? 1u : 0u});
    if (result) *out = kOutMarker;
    return result;
}
bool __fastcall FakeCan(void *self, void *)
{
    bool result = gSubResult[self];
    Log({4, U(self), result ? 1u : 0u});
    return result;
}
uint32_t __fastcall FakeUnexpected(void *self, void *)
{
    Log({0xdead, U(self)});
    return 0;
}

void *gSubVtable[32];

void InitFakeVtable()
{
    for (auto &slot : gSubVtable) slot = reinterpret_cast<void *>(&FakeUnexpected);
    gSubVtable[3] = reinterpret_cast<void *>(&FakeInit);
    gSubVtable[5] = reinterpret_cast<void *>(&FakeEnum);
    gSubVtable[6] = reinterpret_cast<void *>(&FakeGet);
    gSubVtable[7] = reinterpret_cast<void *>(&FakeCan);
}

// A fake sub-director with the given children (their pointers are put in its sub list).
uint8_t *MakeFakeSub(difftest::Context &ctx, uint32_t seed, const std::vector<uint8_t *> &children, bool result)
{
    InitFakeVtable();
    uint8_t *p = MakeDirector(ctx, seed);
    for (uint8_t *c : children) Subs(p).push_back(AsBase(c));
    *reinterpret_cast<void **>(p) = gSubVtable;
    gSubResult[p] = result;
    return p;
}

// Frees a fake sub-director (the fake vtable is put back first: Destruct restores the base ones).
void FreeFakeSub(uint8_t *p)
{
    gSubResult.erase(p);
    FreeDirector(p);
}

// A fake object returned by a kind 1 factory: slots 0 QueryInterface, 1 AddRef, 2 Release.
bool gQiResult = false;
bool __fastcall ComQI(void *, void *, uint32_t iid, void **out)
{
    Log({10, iid, out == gExpectedOut ? 1u : 0u});
    if (gQiResult) *out = kOutMarker;
    return gQiResult;
}
uint32_t __fastcall ComAddRef(void *, void *)
{
    Log({11});
    return 7;
}
uint32_t __fastcall ComRelease(void *, void *)
{
    Log({12});
    return 6;
}
void *gComVtable[3] = {reinterpret_cast<void *>(&ComQI), reinterpret_cast<void *>(&ComAddRef), reinterpret_cast<void *>(&ComRelease)};
struct FakeCom {
    void **vtable = gComVtable;
};
FakeCom gFakeCom;

void *__cdecl FactoryObject1()
{
    Log({13, 1});
    return &gFakeCom;
}
void *__cdecl FactoryObject2()
{
    Log({13, 2});
    return &gFakeCom;
}
bool gCreateResult = false;
bool __cdecl FactoryCreate(uint32_t iid, void **out)
{
    Log({14, iid, out == gExpectedOut ? 1u : 0u});
    if (gCreateResult) *out = kOutMarker;
    return gCreateResult;
}

// A fake cIGZString for GetLibraryPath: slot 3 receives the text.
std::string gReceivedText;
void __fastcall FakeStringAssign(void *, void *, const char *text, uint32_t len)
{
    gReceivedText.assign(text, len);
    Log({20, len});
}
void *gStringVtable[8] = {};

} // namespace

// =============================================================================================
// The class map

namespace {

void RunAddCls(difftest::Context &ctx, uint32_t rva, bool direct)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, void *)>(rva);
    std::mt19937 rng(direct ? 77 : 33);
    const unsigned counts[] = {0, 1, 2, 7, 8, 9, 10, 17, 33, 64, 65, 129, 300};
    for (unsigned count : counts) {
        uint8_t *a = MakeDirector(ctx, 5 + count);
        uint8_t *b = MakeOurs(5 + count);
        uint8_t *c = MakeDirector(ctx, 5 + count); // the adapter's route
        auto *ours = AsBase(b);
        auto adapter = direct ? &BaseAdapters::AddClsDirect : &BaseAdapters::AddCls;
        for (unsigned k = 0; k < count; ++k) {
            // Ids from a pool smaller than the count, so some repeat (the first registration wins);
            // factories are arbitrary words, never called.
            uint32_t id = 0x1000u + (rng() % (count < 4 ? 4 : count * 3 / 4 + 3)) * 0x9e3779b1u;
            void *factory = reinterpret_cast<void *>(rng() | 1);
            orig(a, id, factory);
            if (direct)
                ours->AddClsDirect(id, factory);
            else
                ours->AddCls(id, factory);
            adapter(AsBase(c), nullptr, id, factory);
            ++ctx.cases;
            if (k == 0 || k == 7 || k == 8 || k == 9 || k == 16 || k == 17) {
                std::string d = Diff(Take(a), Take(b));
                if (!d.empty()) ctx.Fail("after %u of %u registrations: %s", k + 1, count, d.c_str());
                d = Diff(Take(a), Take(c));
                if (!d.empty()) ctx.Fail("after %u of %u registrations (adapter): %s", k + 1, count, d.c_str());
            }
        }
        std::string d = Diff(Take(a), Take(b));
        if (!d.empty()) ctx.Fail("%u registrations: %s", count, d.c_str());
        d = Diff(Take(a), Take(c));
        if (!d.empty()) ctx.Fail("%u registrations (adapter): %s", count, d.c_str());
        if (!CanaryIntact(a) || !CanaryIntact(b) || !CanaryIntact(c)) ctx.Fail("%u registrations: wrote past the object", count);
        FreeDirector(a);
        FreeDirector(b);
        FreeDirector(c);
    }
    // Ids registered with both kinds keep their first entry.
    uint8_t *a = MakeDirector(ctx, 99);
    uint8_t *b = MakeOurs(99);
    auto *ours = AsBase(b);
    auto origOther = ctx.Original<void(__thiscall *)(void *, uint32_t, void *)>(direct ? 0x1ae080 : 0x1ae0ad);
    orig(a, 0x42, reinterpret_cast<void *>(0x1111));
    origOther(a, 0x42, reinterpret_cast<void *>(0x2222));
    if (direct) {
        ours->AddClsDirect(0x42, reinterpret_cast<void *>(0x1111));
        ours->AddCls(0x42, reinterpret_cast<void *>(0x2222));
    } else {
        ours->AddCls(0x42, reinterpret_cast<void *>(0x1111));
        ours->AddClsDirect(0x42, reinterpret_cast<void *>(0x2222));
    }
    ++ctx.cases;
    std::string d = Diff(Take(a), Take(b));
    if (!d.empty()) ctx.Fail("same id, both kinds: %s", d.c_str());
    FreeDirector(a);
    FreeDirector(b);
}

} // namespace

DIFF_TEST(COMDirectors_AddCls, 0x1ae080)
{
    RunAddCls(ctx, 0x1ae080, false);
}

DIFF_TEST(COMDirectors_AddClsDirect, 0x1ae0ad)
{
    RunAddCls(ctx, 0x1ae0ad, true);
}

// AddDirector: the sub-director's InitializeCOM gets the framework and the director's path string;
// its own sub-directors are added first (recursively); the sub-director comes last.
DIFF_TEST(COMDirectors_AddDirector, 0x1ae0da)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *)>(0x1ae0da);
    for (void *framework : {static_cast<void *>(nullptr), kFrameworkMarker}) {
        uint8_t *a = MakeDirector(ctx, 11);
        uint8_t *b = MakeOurs(11);
        uint8_t *c = MakeDirector(ctx, 11); // the adapter's route
        for (uint8_t *p : {a, b, c}) *reinterpret_cast<void **>(p + 0x30) = framework;
        // sub1 { sub2, sub3 { sub4 } }, then sub5.
        uint8_t *sub4 = MakeFakeSub(ctx, 4, {}, true);
        uint8_t *sub3 = MakeFakeSub(ctx, 3, {sub4}, true);
        uint8_t *sub2 = MakeFakeSub(ctx, 2, {}, true);
        uint8_t *sub1 = MakeFakeSub(ctx, 1, {sub2, sub3}, true);
        uint8_t *sub5 = MakeFakeSub(ctx, 5, {}, true);
        for (uint8_t *sub : {sub1, sub5}) {
            gLog.clear();
            gExpectedPath = a + 0x10;
            orig(a, sub);
            std::vector<uint32_t> logA = gLog;
            gLog.clear();
            gExpectedPath = b + 0x10;
            AsBase(b)->AddDirector(AsBase(sub));
            ++ctx.cases;
            if (gLog != logA) ctx.Fail("AddDirector (framework %p): the InitializeCOM calls differ (%zu vs %zu words)", framework, logA.size(), gLog.size());
            if (logA.empty()) ctx.Fail("AddDirector: no InitializeCOM call logged");
            std::string d = Diff(Take(a), Take(b));
            if (!d.empty()) ctx.Fail("AddDirector (framework %p): %s", framework, d.c_str());
            gLog.clear();
            gExpectedPath = c + 0x10;
            BaseAdapters::AddDirector(AsBase(c), nullptr, AsBase(sub));
            if (gLog != logA) ctx.Fail("AddDirector adapter (framework %p): the InitializeCOM calls differ", framework);
            d = Diff(Take(a), Take(c));
            if (!d.empty()) ctx.Fail("AddDirector adapter (framework %p): %s", framework, d.c_str());
        }
        // The order: sub2, sub4, sub3, sub1, sub5.
        auto &list = Subs(b);
        if (list.size() != 5 || list[0] != AsBase(sub2) || list[1] != AsBase(sub4) || list[2] != AsBase(sub3) || list[3] != AsBase(sub1))
            ctx.Fail("AddDirector: unexpected order");
        for (uint8_t *p : {a, b, c}) {
            Subs(p).clear();
            FreeDirector(p);
        }
        for (uint8_t *s : {sub1, sub2, sub3, sub4, sub5}) {
            Subs(s).clear();
            FreeFakeSub(s);
        }
    }
}

// =============================================================================================
// The slots

DIFF_TEST(COMDirectors_QueryInterface, 0x1ae298)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(0x1ae298);
    auto origHooks = ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(0x144913);
    const uint32_t iids[] = {1, 0x3fa40bf, 0xa21ee941, 0, 2, 0x3fa40be, 0xa21ee940, 0xffffffff, 0x03fa40bf ^ 0x80000000};
    for (uint32_t iid : iids) {
        for (int via = 0; via < 4; ++via) {
            uint8_t *a = MakeDirector(ctx, 21);
            uint8_t *b = via < 2 ? MakeOurs(21) : MakeTrapped(ctx, 21, via == 2 ? 0 : 4, 0, 2);
            void *outA = reinterpret_cast<void *>(0xdeadbeef), *outB = reinterpret_cast<void *>(0xdeadbeef);
            bool ra, rb;
            if (via == 0) {
                ra = orig(a, iid, &outA);
                rb = CallSlot<bool>(b, 0, iid, &outB);
            } else if (via == 1) {
                // The thunk of the subobject at +4 (0x144913, the compiler's): the same answer, for the director.
                ra = origHooks(a + 4, iid, &outA);
                rb = CallSlot<bool>(b + 4, 0, iid, &outB);
            } else if (via == 2) {
                ra = orig(a, iid, &outA);
                rb = BaseAdapters::QueryInterface(Face0(b), nullptr, iid, &outB);
            } else {
                ra = origHooks(a + 4, iid, &outA);
                rb = BaseAdapters::HooksQueryInterface(Face4(b), nullptr, iid, &outB);
            }
            if (gTrapHit) ctx.Fail("QueryInterface(%08x) via %d: the adapter dispatched to its own slot", iid, via);
            Untrap(b);
            ++ctx.cases;
            if (ra != rb) ctx.Fail("QueryInterface(%08x) via %d: original %d, ours %d", iid, via, ra, rb);
            if (ra) {
                uint32_t offA = U(outA) - U(a), offB = U(outB) - U(b);
                if (offA != offB) ctx.Fail("QueryInterface(%08x) via %d: out offset original %u, ours %u", iid, via, offA, offB);
            } else if (outA != reinterpret_cast<void *>(0xdeadbeef) || outB != reinterpret_cast<void *>(0xdeadbeef)) {
                ctx.Fail("QueryInterface(%08x): out written on failure", iid);
            }
            std::string d = Diff(Take(a), Take(b));
            if (!d.empty()) ctx.Fail("QueryInterface(%08x) via %d: %s (the reference count)", iid, via, d.c_str());
            FreeDirector(a);
            FreeDirector(b);
        }
    }
}

// The reference count slots (the shared stubs 0x145260 AddRef, 0x1ae2f0 Release, 0x46390, 0x147800)
// and RemoveRef (0x1ae2f5), the +4 thunks 0x1448e9 and 0x144975, and the returns-true stubs.
DIFF_TEST(COMDirectors_RefCount, 0x1ae2f5)
{
    auto origAdd = ctx.Original<uint32_t(__thiscall *)(void *)>(0x145260);
    auto origRelease = ctx.Original<uint32_t(__thiscall *)(void *)>(0x1ae2f0);
    auto origRemove = ctx.Original<uint32_t(__thiscall *)(void *)>(0x1ae2f5);
    auto origGet = ctx.Original<uint32_t(__thiscall *)(void *)>(0x46390);
    auto origField34 = ctx.Original<uint32_t(__thiscall *)(void *)>(0x147800);
    auto origHookAdd = ctx.Original<uint32_t(__thiscall *)(void *)>(0x1448e9);
    auto origHookRelease = ctx.Original<uint32_t(__thiscall *)(void *)>(0x144975);
    auto origField30 = ctx.Original<uint32_t(__thiscall *)(void *)>(0x1457d0);
    const uint32_t counts[] = {0, 1, 2, 5, 0xffffffff, 0x80000000};
    for (uint32_t count : counts) {
        for (int what = 0; what < 11; ++what) {
            uint8_t *a = MakeDirector(ctx, 31);
            uint8_t *b = what < 8 ? MakeOurs(31) : MakeDirector(ctx, 31);
            for (uint8_t *p : {a, b}) {
                std::memcpy(p + 8, &count, 4);
                uint32_t f34 = 0x34343434 + count, f30 = 0x30303030 + count;
                std::memcpy(p + 0x34, &f34, 4);
                std::memcpy(p + 0x30, &f30, 4);
            }
            uint32_t ra = 0, rb = 0;
            switch (what) {
            case 0: ra = origAdd(a); rb = CallSlot<uint32_t>(b, 1); break;
            case 1: ra = origRelease(a); rb = CallSlot<uint32_t>(b, 2); break;
            case 2: ra = origRemove(a); rb = CallSlot<uint32_t>(b, 10); break;
            case 3: ra = origGet(a); rb = CallSlot<uint32_t>(b, 9); break;
            case 4: ra = origField34(a); rb = CallSlot<uint32_t>(b, 11); break;
            case 5: ra = origHookAdd(a + 4); rb = CallSlot<uint32_t>(b + 4, 1); break;     // thunk 0x1448e9
            case 6: ra = origHookRelease(a + 4); rb = CallSlot<uint32_t>(b + 4, 2); break; // thunk 0x144975
            case 7: ra = origField30(a); rb = CallSlot<uint32_t>(b, 12); break;
            // The adapters (traps on the forwarded slot).
            case 8: ra = origRemove(a); rb = BaseAdapters::RemoveRef(Face0(Trapped(b, 0, 10, 0)), nullptr); break;
            case 9: // both faces' Release trapped: a dispatching adapter would reach either
                ra = origHookRelease(a + 4);
                Trapped(b, 4, 2, 0);
                gTrapVt0[2] = TrapOf(0);
                rb = BaseAdapters::HooksRelease(Face4(b), nullptr);
                break;
            case 10: ra = origHookAdd(a + 4); rb = BaseAdapters::HooksAddRef(Face4(Trapped(b, 4, 1, 0)), nullptr); break;
            }
            if (gTrapHit) ctx.Fail("slot %d: the adapter dispatched to its own slot", what);
            Untrap(b);
            ++ctx.cases;
            if (ra != rb) ctx.Fail("slot %d with count %u: original %u, ours %u", what, count, ra, rb);
            if (Read<uint32_t>(a, 8) != Read<uint32_t>(b, 8)) ctx.Fail("slot %d with count %u: count after %u vs %u", what, count, Read<uint32_t>(a, 8), Read<uint32_t>(b, 8));
            FreeDirector(a);
            FreeDirector(b);
        }
    }
}

// Release (0x1ae2f0) and AddRef through QueryInterface go through the object's own vtable (slot 10
// and slot 1): a director whose vtable overrides them must see the calls. The vtable here is a copy
// of the base one with those two slots replaced by recorders.
namespace {
uint32_t __fastcall RoutedAddRef(void *self, void *)
{
    Log({40, U(self)});
    return 0x1111;
}
uint32_t __fastcall RoutedSlot10(void *self, void *)
{
    Log({41, U(self)});
    return 0x2222;
}
} // namespace

DIFF_TEST(COMDirectors_VtableRouting, 0x1ae2f0)
{
    static void *vtable[17];
    std::memcpy(vtable, ctx.image.At<void *>(0xda265c), sizeof(vtable));
    vtable[1] = reinterpret_cast<void *>(&RoutedAddRef);
    vtable[10] = reinterpret_cast<void *>(&RoutedSlot10);
    auto origRelease = ctx.Original<uint32_t(__thiscall *)(void *)>(0x1ae2f0);
    auto origHookRelease = ctx.Original<uint32_t(__thiscall *)(void *)>(0x144975);
    auto origQi = ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(0x1ae298);
    // The adapters' route (what 5..8): the same recording vtable, with the forwarded slots trapped (slot 0 here; the
    // +4 face's Release in a copy of the game's +4 vtable). The original calls are direct, so the traps are not in its way.
    vtable[0] = TrapOf(2);
    static void *vtable4[11];
    std::memcpy(vtable4, ctx.image.At<void *>(0xda26a4), sizeof(vtable4));
    vtable4[2] = TrapOf(0);
    static void *ours[17];
    for (int what = 0; what < 9; ++what) {
        uint8_t *a = MakeDirector(ctx, 131);
        uint8_t *b = what < 5 ? MakeOurs(131) : MakeDirector(ctx, 131);
        void **pristine = *reinterpret_cast<void ***>(b);
        void **pristine4 = *reinterpret_cast<void ***>(b + 4);
        if (what < 5) {
            std::memcpy(ours, pristine, sizeof(ours));
            ours[1] = reinterpret_cast<void *>(&RoutedAddRef);
            ours[10] = reinterpret_cast<void *>(&RoutedSlot10);
        }
        *reinterpret_cast<void **>(a) = vtable;
        *reinterpret_cast<void **>(b) = what < 5 ? ours : vtable;
        if (what >= 5) *reinterpret_cast<void **>(b + 4) = vtable4;
        gTrapHit = false;
        void *outA = nullptr, *outB = nullptr;
        uint32_t ra = 0, rb = 0;
        gLog.clear();
        switch (what % 5 + (what >= 5 ? 1 : 0)) {
        case 0: ra = origRelease(a); break;
        case 1: ra = origHookRelease(a + 4); break;
        case 2: ra = origQi(a, 1, &outA); break;
        case 3: ra = origQi(a, 0xa21ee941, &outA); break;
        case 4: ra = origQi(a, 0x3fa40bf, &outA); break;
        }
        std::vector<uint32_t> logA = gLog;
        for (auto &w : logA) w = w == U(a) ? 1u : w; // the object's own address
        gLog.clear();
        using Rel = uint32_t(__thiscall *)(void *);
        using Qi = bool(__thiscall *)(void *, uint32_t, void **);
        switch (what) {
        case 0: rb = reinterpret_cast<Rel>(pristine[2])(b); break;
        case 1: rb = reinterpret_cast<Rel>(pristine4[2])(b + 4); break; // thunk 0x144975
        case 2: rb = reinterpret_cast<Qi>(pristine[0])(b, 1, &outB); break;
        case 3: rb = reinterpret_cast<Qi>(pristine[0])(b, 0xa21ee941, &outB); break;
        case 4: rb = reinterpret_cast<Qi>(pristine[0])(b, 0x3fa40bf, &outB); break;
        case 5: rb = BaseAdapters::HooksRelease(Face4(b), nullptr); break;
        case 6: rb = BaseAdapters::QueryInterface(Face0(b), nullptr, 1, &outB); break;
        case 7: rb = BaseAdapters::QueryInterface(Face0(b), nullptr, 0xa21ee941, &outB); break;
        case 8: rb = BaseAdapters::QueryInterface(Face0(b), nullptr, 0x3fa40bf, &outB); break;
        }
        if (gTrapHit) ctx.Fail("routing %d: the adapter dispatched to its own slot", what);
        for (auto &w : gLog) w = w == U(b) ? 1u : w;
        ++ctx.cases;
        if ((what < 2 || what == 5) && ra != rb) ctx.Fail("routing %d: original returned %x, ours %x", what, ra, rb);
        if (gLog != logA || logA.size() != 2) ctx.Fail("routing %d: logged %zu vs %zu words", what, logA.size(), gLog.size());
        *reinterpret_cast<void **>(a) = ctx.image.At<void>(0xda265c);
        *reinterpret_cast<void **>(b) = what < 5 ? pristine : ctx.image.At<void *>(0xda265c);
        *reinterpret_cast<void **>(b + 4) = what < 5 ? pristine4 : ctx.image.At<void *>(0xda26a4);
        FreeDirector(a);
        FreeDirector(b);
    }
}

DIFF_TEST(COMDirectors_TrivialSlots, 0x148260)
{
    auto origStart = ctx.Original<bool(__thiscall *)(void *, void *)>(0x148260);
    auto origTrue = ctx.Original<bool(__thiscall *)(void *)>(0x144910);
    auto origHeap = ctx.Original<uint32_t(__thiscall *)(void *)>(0x1ae227);
    uint8_t *p = MakeDirector(ctx, 41);
    uint8_t *q = MakeOurs(41);
    ++ctx.cases;
    for (void *arg : {static_cast<void *>(nullptr), kFrameworkMarker})
        if (origStart(p, arg) != CallSlot<bool>(q, 4, arg) || !CallSlot<bool>(q, 4, arg)) ctx.Fail("OnStart default (0x148260)");
    if (origTrue(p) != CallSlot<bool>(q, 8) || !CallSlot<bool>(q, 8)) ctx.Fail("OnUnload, the returns-true stub (0x144910)");
    for (int slot = 3; slot <= 10; ++slot)
        if (origTrue(p) != CallSlot<bool>(q + 4, slot)) ctx.Fail("hooks face slot %d (0x144910)", slot);
    if (origHeap(p) != CallSlot<uint32_t>(q, 15)) ctx.Fail("GetHeapAllocatedSize (0x1ae227): original %u, ours %u", origHeap(p), CallSlot<uint32_t>(q, 15));
    std::string d = Diff(Take(p), Take(q));
    if (!d.empty()) ctx.Fail("trivial slots: %s", d.c_str());
    FreeDirector(p);
    FreeDirector(q);
}

// Every director class's vtables against the game's (lane EO): a slot the game's vtable shares with the base (the
// stubs and getters, OnStart 0x148260, the hooks face's 0x144910) is called on both sides and compared; a slot the game
// overrides (OnStart, the hooks-face slots of TSUI, Sound and HitMan) must be an override in ours too (not our base's
// function). Slot 6 of TSPersist (0x1d3b30, a jump to the base's 0x1ae199) counts as the base's.
DIFF_TEST(COMDirectors_DirectorSlots, 0x144910)
{
    uint8_t *base = MakeOurs(42);
    void **ours0 = *reinterpret_cast<void ***>(base), **ours4 = *reinterpret_cast<void ***>(base + 4);
    void **game0 = ctx.image.At<void *>(0xda265c), **game4 = ctx.image.At<void *>(0xda26a4);
    void *persistSlot6 = ctx.image.At<void>(0x1d3b30);
    unsigned overrides = 0;
    for (unsigned i = 0; i < kDirectorCount; ++i) {
        void **g0 = ctx.image.At<void *>(kGameVTables[i].vtable), **g4 = ctx.image.At<void *>(kGameVTables[i].vtable4);
        uint8_t *p = MakeDirector(ctx, 42 + i);
        *reinterpret_cast<void **>(p) = g0;
        *reinterpret_cast<void **>(p + 4) = g4;
        uint8_t *q = MakeOursOf(42 + i, i);
        void **o0 = *reinterpret_cast<void ***>(q), **o4 = *reinterpret_cast<void ***>(q + 4);
        ++ctx.cases;
        for (int slot = 3; slot < 16; ++slot) {
            if (slot == 13) continue; // the id: COMDirectors_IdThunk_*
            bool shared = g0[slot] == game0[slot] || (g0[slot] == persistSlot6 && slot == 6);
            if (!shared) {
                ++overrides;
                if (o0[slot] == ours0[slot]) ctx.Fail("%s slot %d: the game overrides it, ours is the base's", kTest[i].name, slot);
                continue;
            }
            if (g0[slot] == persistSlot6 || slot == 6) {
                if (o0[slot] != ours0[slot]) ctx.Fail("%s slot 6: ours is not the base's GetClassObject", kTest[i].name);
                continue;
            }
            uint32_t ra, rb, mask = 0xffffffff;
            switch (slot) {
            case 4:
                ra = reinterpret_cast<uint32_t(__thiscall *)(void *, void *)>(g0[slot])(p, kFrameworkMarker);
                rb = CallSlot<uint32_t>(q, slot, kFrameworkMarker);
                mask = 0xff;
                break;
            case 7: case 8: case 9: case 11: case 12: case 15:
                ra = reinterpret_cast<uint32_t(__thiscall *)(void *)>(g0[slot])(p);
                rb = CallSlot<uint32_t>(q, slot);
                if (slot == 7 || slot == 8) mask = 0xff;
                break;
            default: // 3, 5, 10, 14: the base's bodies (their own tests); ours must be the base's too
                if (o0[slot] != ours0[slot]) ctx.Fail("%s slot %d: the game's is the base's, ours is not", kTest[i].name, slot);
                continue;
            }
            if ((ra & mask) != (rb & mask)) ctx.Fail("%s slot %d: original %x, ours %x", kTest[i].name, slot, ra & mask, rb & mask);
        }
        for (int slot = 0; slot < 11; ++slot) {
            if (g4[slot] != game4[slot]) {
                ++overrides;
                if (o4[slot] == ours4[slot]) ctx.Fail("%s hooks slot %d: the game overrides it, ours is the base's", kTest[i].name, slot);
                continue;
            }
            if (slot < 3) {
                if (o4[slot] != ours4[slot]) ctx.Fail("%s hooks slot %d: not the base's thunk", kTest[i].name, slot);
                continue;
            }
            uint32_t ra = reinterpret_cast<uint32_t(__thiscall *)(void *)>(g4[slot])(p + 4) & 0xff;
            uint32_t rb = CallSlot<uint32_t>(q + 4, slot) & 0xff;
            if (ra != rb) ctx.Fail("%s hooks slot %d: original %x, ours %x", kTest[i].name, slot, ra, rb);
        }
        std::string d = Diff(Take(p), Take(q));
        if (!d.empty()) ctx.Fail("%s slots: %s", kTest[i].name, d.c_str());
        FreeDirector(p);
        FreeDirector(q);
    }
    // 22 OnStarts and six hooks-face slots.
    if (overrides != 28) ctx.Fail("%u overridden slots in the game's director vtables, expected 28", overrides);
    FreeDirector(base);
}

DIFF_TEST(COMDirectors_InitializeCOM, 0x1ae24d)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *, void *, void *)>(0x1ae24d);
    // The framework: slot 4 returns a marker.
    static void *frameworkVtable[8];
    static uint32_t slot4Calls;
    struct Slot4 {
        static uint32_t __fastcall Get(void *, void *)
        {
            ++slot4Calls;
            return 0x77aa1234;
        }
    };
    frameworkVtable[4] = reinterpret_cast<void *>(&Slot4::Get);
    struct Fake {
        void **vtable;
    } framework{frameworkVtable};

    for (int route = 0; route < 2; ++route) {
    for (int variant = 0; variant < 4; ++variant) {
        for (bool withFramework : {false, true}) {
            uint8_t *a = MakeDirector(ctx, 51);
            uint8_t *b = route == 0 ? MakeOurs(51) : MakeTrapped(ctx, 51, 0, 3, 2);
            std::vector<uint8_t *> subs;
            if (variant > 0) {
                subs.push_back(MakeFakeSub(ctx, 52, {}, true));
                subs.push_back(MakeFakeSub(ctx, 53, {}, true));
            }
            for (uint8_t *s : subs)
                for (uint8_t *p : {a, b}) Subs(p).push_back(AsBase(s));
            cRZString path;
            path.Construct(variant == 3 ? "a path that is longer than fifteen characters" : "short path");
            slot4Calls = 0;
            gLog.clear();
            gExpectedPath = &path;
            bool ra = orig(a, withFramework ? &framework : nullptr, &path);
            uint32_t callsA = slot4Calls;
            std::vector<uint32_t> logA = gLog;
            slot4Calls = 0;
            gLog.clear();
            auto *com = reinterpret_cast<cIGZCOM *>(withFramework ? &framework : nullptr);
            bool rb = route == 0 ? CallSlot<bool>(b, 3, com, static_cast<void *>(&path))
                                 : BaseAdapters::InitializeCOM(Face0(b), nullptr, com, *path.AsInterface());
            if (gTrapHit) ctx.Fail("InitializeCOM: the adapter dispatched to its own slot");
            Untrap(b);
            ++ctx.cases;
            if (ra != rb) ctx.Fail("InitializeCOM: original %d, ours %d", ra, rb);
            if (callsA != slot4Calls) ctx.Fail("InitializeCOM: slot 4 calls %u vs %u", callsA, slot4Calls);
            if (withFramework && (callsA != 1 || ra != true)) ctx.Fail("InitializeCOM with a framework: %u slot 4 calls, result %d", callsA, ra);
            if (!withFramework && (callsA != 0 || ra)) ctx.Fail("InitializeCOM without a framework: %u slot 4 calls, result %d", callsA, ra);
            if (gLog != logA) ctx.Fail("InitializeCOM: sub-director calls differ");
            if (withFramework && variant > 0 && logA.size() != 8) ctx.Fail("InitializeCOM: %zu logged words, expected two calls", logA.size());
            std::string d = Diff(Take(a), Take(b));
            if (!d.empty()) ctx.Fail("InitializeCOM variant %d: %s", variant, d.c_str());
            if (withFramework && Read<uint32_t>(b, 0x34) != 0x77aa1234) ctx.Fail("InitializeCOM: field 0x34 is %08x", Read<uint32_t>(b, 0x34));
            path.Destruct();
            for (uint8_t *p : {a, b}) Subs(p).clear();
            FreeDirector(a);
            FreeDirector(b);
            for (uint8_t *s : subs) FreeFakeSub(s);
        }
    }
    }
}

static void EnumCollect(uint32_t clsid, uint32_t zero, void *context)
{
    Log({30, clsid, zero, U(context)});
}

DIFF_TEST(COMDirectors_EnumClassObjects, 0x1ae148)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, EnumCallback, void *)>(0x1ae148);
    for (int route = 0; route < 2; ++route)
    for (int variant = 0; variant < 4; ++variant) {
        uint8_t *a = MakeDirector(ctx, 61);
        uint8_t *b = route == 0 ? MakeOurs(61) : MakeTrapped(ctx, 61, 0, 5, 2);
        std::vector<uint8_t *> subs;
        if (variant & 1) {
            subs.push_back(MakeFakeSub(ctx, 62, {}, true));
            subs.push_back(MakeFakeSub(ctx, 63, {}, true));
        }
        for (uint8_t *p : {a, b}) {
            for (uint8_t *s : subs) Subs(p).push_back(AsBase(s));
            if (variant & 2)
                for (uint32_t k = 0; k < 20; ++k) AsBase(p)->AddCls(0x500 + k * 7, reinterpret_cast<void *>(0x10 + k));
        }
        gExpectedCallback = &EnumCollect;
        void *context = reinterpret_cast<void *>(0x4d2);
        gLog.clear();
        orig(a, &EnumCollect, context);
        std::vector<uint32_t> logA = gLog;
        gLog.clear();
        if (route == 0)
            CallSlot<void>(b, 5, reinterpret_cast<void *>(&EnumCollect), context);
        else
            BaseAdapters::EnumClassObjects(Face0(b), nullptr, reinterpret_cast<void *>(&EnumCollect), context);
        if (gTrapHit) ctx.Fail("EnumClassObjects: the adapter dispatched to its own slot");
        Untrap(b);
        ++ctx.cases;
        if (gLog != logA) ctx.Fail("EnumClassObjects variant %d: logs differ (%zu vs %zu words)", variant, logA.size(), gLog.size());
        if (((variant & 1) || (variant & 2)) && logA.empty()) ctx.Fail("EnumClassObjects variant %d: nothing logged", variant);
        if ((variant & 2) && logA.size() < 20 * 4) ctx.Fail("EnumClassObjects variant %d: classes missing", variant);
        for (uint8_t *p : {a, b}) Subs(p).clear();
        FreeDirector(a);
        FreeDirector(b);
        for (uint8_t *s : subs) FreeFakeSub(s);
    }
}

static void TestGetClassObject(difftest::Context &ctx, uint32_t rva, unsigned index)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t, uint32_t, void **)>(rva);
    // Class ids: 0x100 kind 1 (the object answers), 0x101 kind 1 (it does not), 0x102 kind 2 (true),
    // 0x103 kind 2 (false), 0x104 an entry of an unknown kind, 0x999 absent.
    const uint32_t ids[] = {0x100, 0x101, 0x102, 0x103, 0x104, 0x999};
    for (uint32_t id : ids) {
        for (int subs = 0; subs < 4; ++subs) {
            for (int rep = 0; rep < 4; ++rep) {
                // rep 0/1: our class through its vtable; 2/3: the base's adapter (slot 6 trapped; the Persist slot 0x1d3b30
                // is a 5-byte jump, not hooked, so its rows run the vtable route twice).
                bool adapterRoute = rep >= 2 && index >= kDirectorCount;
                uint8_t *a = MakeDirector(ctx, 71);
                if (index < kDirectorCount) {
                    *reinterpret_cast<void **>(a) = ctx.image.At<void>(kGameVTables[index].vtable);
                    *reinterpret_cast<void **>(a + 4) = ctx.image.At<void>(kGameVTables[index].vtable4);
                }
                uint8_t *b = adapterRoute ? MakeTrapped(ctx, 71, 0, 6, 3) : MakeOursOf(71, index);
                std::vector<uint8_t *> list;
                if (subs > 0) {
                    // subs 1: both refuse; 2: the first answers; 3: the second answers.
                    list.push_back(MakeFakeSub(ctx, 72, {}, subs == 2));
                    list.push_back(MakeFakeSub(ctx, 73, {}, subs == 3));
                }
                for (uint8_t *p : {a, b}) {
                    auto *d = AsBase(p);
                    for (uint8_t *s : list) d->mChildren.push_back(AsBase(s));
                    d->AddCls(0x100, reinterpret_cast<void *>(&FactoryObject1));
                    d->AddCls(0x101, reinterpret_cast<void *>(&FactoryObject2));
                    d->AddClsDirect(0x102, reinterpret_cast<void *>(&FactoryCreate));
                    d->AddClsDirect(0x103, reinterpret_cast<void *>(&FactoryCreate));
                    d->mClasses.try_emplace(0x104, ClassEntry{reinterpret_cast<void *>(&FactoryCreate), 3});
                }
                gQiResult = rep % 2 == 0;
                gCreateResult = (id == 0x102) == (rep % 2 == 0);
                void *outA = reinterpret_cast<void *>(0xdeadbeef), *outB = reinterpret_cast<void *>(0xdeadbeef);
                gLog.clear();
                gExpectedOut = &outA;
                bool ra = orig(a, id, 0xabcd, &outA);
                std::vector<uint32_t> logA = gLog;
                gLog.clear();
                gExpectedOut = &outB;
                bool rb = adapterRoute ? BaseAdapters::GetClassObject(Face0(b), nullptr, id, 0xabcd, &outB)
                                       : CallSlot<bool>(b, 6, id, 0xabcdu, &outB);
                if (gTrapHit) ctx.Fail("GetClassObject: the adapter dispatched to its own slot");
                Untrap(b);
                if (index < kDirectorCount) *reinterpret_cast<void **>(a) = ctx.image.At<void>(kGameVTables[index].vtable);
                ++ctx.cases;
                if (ra != rb) ctx.Fail("GetClassObject(%x, subs %d, rep %d): original %d, ours %d", id, subs, rep, ra, rb);
                if (outA != outB) ctx.Fail("GetClassObject(%x, subs %d, rep %d): out %p vs %p", id, subs, rep, outA, outB);
                if (gLog != logA) ctx.Fail("GetClassObject(%x, subs %d, rep %d): the calls differ (%zu vs %zu words)", id, subs, rep, logA.size(), gLog.size());
                std::string diff = Diff(Take(a), Take(b));
                if (!diff.empty()) ctx.Fail("GetClassObject(%x): %s", id, diff.c_str());
                for (uint8_t *p : {a, b}) Subs(p).clear();
                FreeDirector(a);
                FreeDirector(b);
                for (uint8_t *s : list) FreeFakeSub(s);
            }
        }
    }
}

DIFF_TEST(COMDirectors_GetClassObject, 0x1ae199)
{
    TestGetClassObject(ctx, 0x1ae199, kDirectorCount);
}

// The Persist director overrides slot 6 with a tail jump to the base GetClassObject (0x1d3b30, not hookable: a thunk).
DIFF_TEST(COMDirectors_PersistGetClassObject, 0x1d3b30)
{
    TestGetClassObject(ctx, 0x1d3b30, kDir_TSPersist); // our cTSPersistCOMDirector inherits the base's slot 6
}

DIFF_TEST(COMDirectors_CanUnloadNow, 0x1ae11d)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *)>(0x1ae11d);
    for (uint32_t count : {0u, 1u, 3u}) {
        for (int pattern = 0; pattern < 16; ++pattern) {
            bool adapterRoute = pattern >= 8;
            uint8_t *a = MakeDirector(ctx, 81);
            uint8_t *b = adapterRoute ? MakeTrapped(ctx, 81, 0, 7, 0) : MakeOurs(81);
            std::vector<uint8_t *> list;
            for (int k = 0; k < 3; ++k) list.push_back(MakeFakeSub(ctx, 82 + k, {}, (pattern >> k) & 1));
            if (adapterRoute) Trapped(b, 0, 7, 0); // MakeFakeSub rebuilt the shared fake vtable, not the trap
            for (uint8_t *p : {a, b}) {
                std::memcpy(p + 8, &count, 4);
                for (uint8_t *s : list) Subs(p).push_back(AsBase(s));
            }
            gLog.clear();
            bool ra = orig(a);
            std::vector<uint32_t> logA = gLog;
            gLog.clear();
            bool rb = adapterRoute ? BaseAdapters::CanUnloadNow(Face0(b), nullptr) : CallSlot<bool>(b, 7);
            if (gTrapHit) ctx.Fail("CanUnloadNow: the adapter dispatched to its own slot");
            Untrap(b);
            ++ctx.cases;
            if (ra != rb || gLog != logA) ctx.Fail("CanUnloadNow(count %u, pattern %d): original %d, ours %d, calls %zu vs %zu words", count, pattern, ra, rb, logA.size(), gLog.size());
            for (uint8_t *p : {a, b}) Subs(p).clear();
            FreeDirector(a);
            FreeDirector(b);
            for (uint8_t *s : list) FreeFakeSub(s);
        }
    }
}

DIFF_TEST(COMDirectors_GetLibraryPath, 0x1ae22c)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *, void *)>(0x1ae22c);
    gStringVtable[3] = reinterpret_cast<void *>(&FakeStringAssign);
    struct FakeString {
        void **vtable;
    } fake{gStringVtable};
    for (int route = 0; route < 2; ++route)
    for (const char *text : {"", "x", "C:\\Games\\a short one", "a library path that is certainly longer than fifteen characters"}) {
        uint8_t *a = MakeDirector(ctx, 91);
        uint8_t *b = route == 0 ? MakeOurs(91) : MakeTrapped(ctx, 91, 0, 14, 1);
        for (uint8_t *p : {a, b}) AsBase(p)->mLibraryPath.value.mString.assign(text);
        gLog.clear();
        gReceivedText = "unset";
        bool ra = orig(a, &fake);
        std::string textA = gReceivedText;
        std::vector<uint32_t> logA = gLog;
        gLog.clear();
        gReceivedText = "unset";
        bool rb = route == 0 ? CallSlot<bool>(b, 14, static_cast<void *>(&fake))
                             : BaseAdapters::GetLibraryPath(Face0(b), nullptr, *reinterpret_cast<cIGZString *>(&fake));
        if (gTrapHit) ctx.Fail("GetLibraryPath: the adapter dispatched to its own slot");
        Untrap(b);
        ++ctx.cases;
        if (ra != rb || textA != gReceivedText || gLog != logA || textA != text)
            ctx.Fail("GetLibraryPath(\"%s\"): original %d \"%s\", ours %d \"%s\"", text, ra, textA.c_str(), rb, gReceivedText.c_str());
        FreeDirector(a);
        FreeDirector(b);
    }
}

// =============================================================================================
// Destructors

namespace {

// A director with heap contents for the destructor to free: a long path, three sub-directors, classes.
uint8_t *Load(uint8_t *p)
{
    auto *d = AsBase(p);
    d->mLibraryPath.value.mString.assign(40, 'x');
    for (int k = 0; k < 3; ++k) d->mChildren.push_back(reinterpret_cast<Base *>(0x1000 + k));
    for (uint32_t k = 0; k < 12; ++k) d->AddCls(0x300 + k, reinterpret_cast<void *>(0x40 + k));
    return p;
}
uint8_t *MakeLoaded(difftest::Context &ctx, uint32_t seed) { return Load(MakeDirector(ctx, seed)); }

} // namespace

// The deleting destructor slot: the copies of the original differ only in what they store before the
// base destructor runs (their own vtables), which the base destructor overwrites at once.
// Ours: slot 16 of our class's vtable (the first director using `rva`, or the base for 0x1448ca); then, where `rva` is
// hooked, its adapter on a director built by the original. 0x18e5f0 and 0x1ab22a are shared by several directors and not
// hooked (folded bodies): only our vtable's slot 16 stands for them.
static void TestDeletingDestructor(difftest::Context &ctx, uint32_t rva)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(rva);
    unsigned index = kDirectorCount;
    if (rva != 0x1448ca)
        for (unsigned i = 0; i < kTestCount; ++i)
            if (kTest[i].deleting == rva) {
                index = i;
                break;
            }
    bool hooked = rva != 0x18e5f0 && rva != 0x1ab22a && !Dropped(rva);
    for (int route = 0; route < (hooked ? 2 : 1); ++route)
    for (uint32_t flags : {0u, 1u, 2u, 3u}) {
        uint8_t *a = MakeLoaded(ctx, 101);
        uint8_t *b = route == 0 ? Load(MakeOursOf(101, index)) : MakeLoaded(ctx, 101);
        // Each side's frees are recorded across its own call (FreeRecorder: exact, whatever reuses the block).
        difftest::FreeRecorder recA;
        void *ra = orig(a, flags);
        recA.Stop();
        bool freedA = recA.Freed(a);
        difftest::FreeRecorder recB;
        void *rb = route == 0                 ? CallSlot<void *>(b, 16, flags)
                   : index == kDirectorCount ? static_cast<void *>(BaseAdapters::DeletingDestructor(Face0(b), nullptr, flags))
                                             : kAdapters[index].deleting(Face0(b), nullptr, flags);
        recB.Stop();
        bool freedB = recB.Freed(b);
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("destructor 0x%x flags %u: returned %p/%p for %p/%p", rva, flags, ra, rb, a, b);
        if (freedA != ((flags & 1) != 0) || freedB != ((flags & 1) != 0)) ctx.Fail("destructor 0x%x flags %u: freed original %d, ours %d", rva, flags, freedA, freedB);
        if (!(flags & 1)) {
            if (!SameSnap(SnapDestroyed(a), SnapDestroyed(b))) ctx.Fail("destructor 0x%x flags %u: the destroyed objects differ", rva, flags);
            if (!CanaryIntact(a) || !CanaryIntact(b)) ctx.Fail("destructor 0x%x flags %u: wrote past the object", rva, flags);
            ::operator delete(a);
            ::operator delete(b);
        }
    }
}

// The destructor body (0x1fc450 and the like): stores the director's vtables and runs the base destructor. Ours: our
// class's destructor (slot 16 with flags 0), and the hook's adapter on a director built by the original.
static void TestDestructorBody(difftest::Context &ctx, unsigned i)
{
    uint32_t rva = kTest[i].body;
    auto orig = ctx.Original<void(__thiscall *)(void *)>(rva);
    for (int route = 0; route < 2; ++route) {
        uint8_t *a = MakeLoaded(ctx, 111);
        uint8_t *b = route == 0 ? Load(MakeOursOf(111, i)) : MakeLoaded(ctx, 111);
        orig(a);
        if (route == 0) {
            if (CallSlot<void *>(b, 16, 0u) != b) ctx.Fail("destructor body 0x%x: our slot 16 returned another pointer", rva);
        } else {
            kAdapters[i].destruct(Face0(b), nullptr);
        }
        ++ctx.cases;
        if (!SameSnap(SnapDestroyed(a), SnapDestroyed(b))) ctx.Fail("destructor body 0x%x (route %d): the destroyed objects differ", rva, route);
        ::operator delete(a);
        ::operator delete(b);
    }
}

DIFF_TEST(COMDirectors_DeletingDestructorBase, 0x1448ca)
{
    TestDeletingDestructor(ctx, 0x1448ca);
}

// The id (slot 13): ours through our class's vtable, and the hook's adapter on a director built by the original (slot 13
// trapped). 0x1fc520 is shared by PathPlan and TSCommon (not hooked); its adapter is still each class's own.
static void TestIdThunk(difftest::Context &ctx, unsigned i)
{
    uint8_t *p = MakeDirector(ctx, 121);
    uint8_t *q = MakeOursOf(121, i);
    uint8_t *r = MakeTrapped(ctx, 121, 0, 13, 0);
    uint32_t a = ctx.Original<uint32_t(__thiscall *)(void *)>(kTest[i].thunk)(p);
    uint32_t b = CallSlot<uint32_t>(q, 13);
    uint32_t c = kAdapters[i].id(Face0(r), nullptr);
    if (gTrapHit) ctx.Fail("%s id: the adapter dispatched to its own slot", kTest[i].name);
    Untrap(r);
    ++ctx.cases;
    if (a != b || a != c || a != kTest[i].id || kDirectorSpecs[i].id != a || kDirectorIds[i] != a)
        ctx.Fail("%s id thunk 0x%x: original %08x, ours %08x, adapter %08x, table %08x, spec %08x", kTest[i].name, kTest[i].thunk, a, b, c, kTest[i].id, kDirectorSpecs[i].id);
    FreeDirector(p);
    FreeDirector(q);
    FreeDirector(r);
}

// =============================================================================================
// Constructors, getters, the static initialiser, the helpers

namespace {

// The constructor (a function of its own): original and ours on identical fills. The original builds
// the sub-directors the constructor adds (through its image's getters, with atexit recorded); ours
// then finds them built.
void TestConstructor(difftest::Context &ctx, unsigned i)
{
    const TestDirector &t = kTest[i];
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(t.ctor);
    EpochScope epoch(ctx);
    EnsureMadCow(ctx);
    for (uint32_t seed : {201u, 202u}) {
        uint8_t *a = NewBlock(seed);
        uint8_t *b = NewBlock(seed);
        void *ra, *rb;
        {
            AtexitRedirect redirect(ctx);
            ra = orig(a);
        }
        epoch.Reset();
        {
            OursRedirect redirect;
            rb = kOursCtor[i](b);
            if (!gOursHandlers.empty()) ctx.Fail("%s constructor registered %zu exit handlers", t.name, gOursHandlers.size());
        }
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("%s constructor returned %p/%p for %p/%p", t.name, ra, rb, a, b);
        if (!CanaryIntact(a) || !CanaryIntact(b)) ctx.Fail("%s constructor wrote past the object", t.name);
        Canon ca = Take(a), cb = Take(b);
        std::string d = Diff(ca, cb);
        if (!d.empty()) ctx.Fail("%s constructor: %s", t.name, d.c_str());
        // Not an empty director unless it registers nothing.
        if (ca.nodes.empty() && ca.subs.empty() && t.index != kDir_TSUICommon) ctx.Fail("%s constructor registered nothing", t.name);
        FreeDirector(a);
        FreeDirector(b);
    }
}

// Runs the handlers a run registered, in order, and returns which static each one destroyed
// (the static whose vtable became the base one), or -1.
std::vector<int> RunHandlers(difftest::Context &ctx, const std::vector<void (*)()> &handlers)
{
    std::vector<int> destroyed;
    for (void (*handler)() : handlers) {
        std::vector<uint32_t> before;
        for (unsigned j = 0; j < kTestCount; ++j) before.push_back(Read<uint32_t>(StaticOf(ctx, j), 0));
        handler();
        int which = -1;
        for (unsigned j = 0; j < kTestCount; ++j)
            if (before[j] != 0xCDCDCDCD && VCode(Read<uint32_t>(StaticOf(ctx, j), 0)) == kBaseCode && VCode(before[j]) != kBaseCode) which = static_cast<int>(j);
        destroyed.push_back(which);
    }
    return destroyed;
}

struct StaticsState {
    std::vector<bool> built;
    std::vector<Canon> canon;
    std::vector<int> guardState; // 0 none, 1 built
};

StaticsState SnapshotStatics(difftest::Context &ctx)
{
    StaticsState s;
    for (unsigned j = 0; j < kTestCount; ++j) {
        bool built = IsBuilt(ctx, j);
        s.built.push_back(built);
        s.canon.push_back(built ? Take(StaticOf(ctx, j)) : Canon());
        int g = kTest[j].guard ? *ctx.image.At<int>(kTest[j].guard) : 1;
        s.guardState.push_back(g != 0 && g != -1 ? 1 : 0);
    }
    return s;
}

std::vector<Snap> SnapshotDestroyed(difftest::Context &ctx)
{
    std::vector<Snap> v;
    for (unsigned j = 0; j < kTestCount; ++j) v.push_back(SnapDestroyed(StaticOf(ctx, j)));
    return v;
}

// The getter on first use, the whole tree. All statics are reset, the original's getter runs (its
// atexit registrations recorded), the statics it built are captured; the same for ours. The
// registered exit handlers are then run in order on each side: the same statics must be destroyed,
// leaving the same bytes.
void TestGetter(difftest::Context &ctx, unsigned i)
{
    const TestDirector &t = kTest[i];
    const DirectorSpec &spec = kDirectorSpecs[i];
    if (spec.getterRva != t.getter || spec.staticRva != t.object || spec.guardRva != t.guard || (spec.flags & kInlineCtor ? 0 : spec.ctorRva) != t.ctor)
        ctx.Fail("%s: the director table's addresses differ from the test's", t.name);
    EpochScope epoch(ctx);

    ResetStatics(ctx);
    EnsureMadCow(ctx);
    epoch.Reset();
    void *a;
    {
        AtexitRedirect redirect(ctx);
        a = ctx.Original<void *(__cdecl *)()>(t.getter)();
    }
    StaticsState stateA = SnapshotStatics(ctx);
    std::vector<void (*)()> handlersA = gImageHandlers;
    std::vector<int> orderA = RunHandlers(ctx, handlersA);
    std::vector<Snap> destroyedA = SnapshotDestroyed(ctx);

    ResetStatics(ctx);
    EnsureMadCow(ctx);
    epoch.Reset();
    void *b;
    std::vector<void (*)()> handlersB;
    {
        OursRedirect redirect;
        b = GetDirector(i);
        handlersB = gOursHandlers;
    }
    StaticsState stateB = SnapshotStatics(ctx);
    std::vector<int> orderB = RunHandlers(ctx, handlersB);
    std::vector<Snap> destroyedB = SnapshotDestroyed(ctx);
    ++ctx.cases;

    if (a != StaticOf(ctx, i) || b != a) ctx.Fail("%s getter returned %p (original %p), expected %p", t.name, b, a, StaticOf(ctx, i));
    unsigned builtCount = 0;
    for (unsigned j = 0; j < kTestCount; ++j) {
        if (j == kDir_MadCow) continue; // built beforehand by its initialiser
        if (stateA.built[j] != stateB.built[j]) ctx.Fail("%s getter: %s %s by the original, %s by ours", t.name, kTest[j].name, stateA.built[j] ? "built" : "not built", stateB.built[j] ? "built" : "not built");
        if (stateA.guardState[j] != stateB.guardState[j]) ctx.Fail("%s getter: guard of %s differs (%d vs %d)", t.name, kTest[j].name, stateA.guardState[j], stateB.guardState[j]);
        if (stateA.built[j] && stateB.built[j]) {
            ++builtCount;
            std::string d = Diff(stateA.canon[j], stateB.canon[j]);
            if (!d.empty()) ctx.Fail("%s getter: %s differs: %s", t.name, kTest[j].name, d.c_str());
        }
    }
    if (builtCount == 0 || !stateB.built[i]) ctx.Fail("%s getter built nothing", t.name);
    if (kTest[i].guard && stateB.guardState[i] != 1) ctx.Fail("%s getter left guard state %d", t.name, stateB.guardState[i]);
    if (handlersA.size() != handlersB.size()) ctx.Fail("%s getter: the original registered %zu exit handlers, ours %zu", t.name, handlersA.size(), handlersB.size());
    if (handlersB.size() != builtCount) ctx.Fail("%s getter: %zu handlers for %u built statics", t.name, handlersB.size(), builtCount);
    if (orderA != orderB) ctx.Fail("%s getter: the handlers destroy the statics in a different order", t.name);
    for (int j : orderB)
        if (j < 0) {
            ctx.Fail("%s getter: a handler destroyed no static", t.name);
            break;
        }
    for (int j : orderB)
        if (j >= 0 && !SameSnap(destroyedA[j], destroyedB[j])) ctx.Fail("%s getter: %s destroyed differently", t.name, kTest[j].name);

    // The fast path: once built, both return the static without registering anything.
    ResetStatics(ctx);
    EnsureMadCow(ctx);
    epoch.Reset();
    {
        AtexitRedirect redirect(ctx);
        ctx.Original<void *(__cdecl *)()>(t.getter)();
    }
    StaticsState before = SnapshotStatics(ctx);
    void *a2, *b2;
    {
        AtexitRedirect redirect(ctx);
        a2 = ctx.Original<void *(__cdecl *)()>(t.getter)();
        size_t originalRegistrations = gImageHandlers.size();
        OursRedirect ours;
        b2 = GetDirector(i);
        ++ctx.cases;
        if (originalRegistrations != 0 || !gOursHandlers.empty()) ctx.Fail("%s getter: the second use registered handlers (%zu, %zu)", t.name, originalRegistrations, gOursHandlers.size());
    }
    StaticsState after = SnapshotStatics(ctx);
    if (a2 != b2 || a2 != StaticOf(ctx, i)) ctx.Fail("%s getter fast path returned %p/%p", t.name, a2, b2);
    for (unsigned j = 0; j < kTestCount; ++j)
        if (before.built[j] != after.built[j] || (before.built[j] && !Diff(before.canon[j], after.canon[j]).empty())) {
            ctx.Fail("%s getter: the fast path changed %s", t.name, kTest[j].name);
            break;
        }
    ResetStatics(ctx);
}

// ---------------------------------------------------------------------------------------------
// OnStart (slot 4) of the service-registering directors
//
// A fake world: the framework objects (the argument, and the one the director's own slot 12 returns:
// both create service objects through slot 3, as configured), the service object in the image's
// global 0x10c9378 (slot 3 AddSystemService, slot 7 AddHook, slot 11), and the created objects
// (slot 2 Release). Every call is logged with the identities of its objects. Each scenario picks
// which creations return an object and which return true, and optionally makes the k-th
// AddSystemService or the k-th Release throw (a C++ exception, which escapes through the original's
// frames and ours: the references still held must be released the same way).

namespace {

struct FakeObject {
    void **vtable;
    int id;
};

FakeObject gComArg{}, gComThis{}, gService{}, gRefObjects[8]{};
void *gComVtable[8], *gServiceVtable[16], *gRefVtable[4], *gDirectorVtable[17], *gSubStartVtable[8];
uint8_t *gDirectorNow = nullptr;
const void *gFontStatic = nullptr, *gDrawStatic = nullptr;
bool gCreateOut[8], gCreateRet[8];
int gCreateCalls = 0, gAddCalls = 0, gReleaseCalls = 0, gThrowAdd = -1, gThrowRelease = -1;

uint32_t ComId(const void *p) { return p == &gComArg ? 1 : p == &gComThis ? 2 : 9; }
uint32_t RefId(const void *p)
{
    for (int k = 0; k < 8; ++k)
        if (p == &gRefObjects[k]) return 10 + k;
    return p ? 99 : 0;
}
uint32_t HookId(const void *p) { return !p ? 0 : p == gDirectorNow + 4 ? 4 : p == gDirectorNow ? 1 : 9; }

bool __fastcall CreateFake(void *self, void *, uint32_t clsid, uint32_t iid, void **out)
{
    int k = gCreateCalls++;
    Log({70, ComId(self), clsid, iid, static_cast<uint32_t>(k)});
    if (k < 8 && gCreateOut[k]) *out = &gRefObjects[k];
    return k < 8 && gCreateRet[k];
}
uint32_t __fastcall ReleaseFake(void *self, void *)
{
    int k = gReleaseCalls++;
    Log({71, RefId(self)});
    if (k == gThrowRelease) throw 8;
    return 0;
}
void __fastcall AddFake(void *, void *, void *object)
{
    int k = gAddCalls++;
    Log({72, RefId(object)});
    if (k == gThrowAdd) throw 7;
}
void __fastcall AddHookFake(void *, void *, void *hooks) { Log({73, HookId(hooks)}); }
void __fastcall Slot11Fake(void *, void *, void *object) { Log({74, RefId(object)}); }
void *__fastcall FrameworkFake(void *, void *)
{
    Log({75});
    return &gComThis;
}
bool __fastcall SubStartFake(void *self, void *, void *argument)
{
    Log({76, self == gFontStatic ? 1u : self == gDrawStatic ? 2u : 9u, argument == &gComArg ? 1u : 9u});
    return true;
}

struct StartCall {
    void *fn;
    uint8_t *director;
    void *argument;
    bool result;
};
void RunStartCall(StartCall *c)
{
    c->result = reinterpret_cast<bool(__thiscall *)(void *, void *)>(c->fn)(c->director, c->argument);
}
// No C++ objects here (__try): the code of the exception that escapes the call, or 0.
unsigned long EscapedStart(StartCall *c)
{
    __try {
        RunStartCall(c);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
    return 0;
}

struct StartTest {
    const char *name;
    uint32_t rva;
    unsigned index;
    unsigned creates;
    bool framed; // the frame releases its references when an exception escapes; a Release that throws ends the process
};
#define COMDIR_START(name, rva, creates, framed) {#name, rva, kDir_##name, creates, framed},
const StartTest kStartTests[] = {
    COMDIR_START(Event, 0x1a83f5, 1, false) COMDIR_START(Draw, 0x8b8030, 1, false) COMDIR_START(Font, 0x90a048, 1, false)
    COMDIR_START(UIScript, 0x85b355, 1, false) COMDIR_START(RZMessage, 0xc6ec41, 1, false) COMDIR_START(ResMan, 0x9b33b0, 1, false)
    COMDIR_START(SceneDBAnim, 0x9d11d0, 1, true) COMDIR_START(SceneGraph, 0xa34210, 1, true) COMDIR_START(TSWeb, 0x1cb910, 1, true)
    COMDIR_START(SndEa, 0xb12c5f, 1, false) COMDIR_START(Win, 0x92f8da, 2, false) COMDIR_START(HitMan, 0xb2a422, 2, false)
    COMDIR_START(Sound, 0xb22420, 2, false) COMDIR_START(Graphics, 0x154460, 2, true) COMDIR_START(Draw2D, 0x145f20, 3, true)};
#undef COMDIR_START

void TestOnStart(difftest::Context &ctx, const StartTest &t)
{
    // The fake world.
    for (auto &s : gComVtable) s = reinterpret_cast<void *>(&FakeUnexpected);
    gComVtable[3] = reinterpret_cast<void *>(&CreateFake);
    for (auto &s : gServiceVtable) s = reinterpret_cast<void *>(&FakeUnexpected);
    gServiceVtable[3] = reinterpret_cast<void *>(&AddFake);
    gServiceVtable[7] = reinterpret_cast<void *>(&AddHookFake);
    gServiceVtable[11] = reinterpret_cast<void *>(&Slot11Fake);
    for (auto &s : gRefVtable) s = reinterpret_cast<void *>(&FakeUnexpected);
    gRefVtable[2] = reinterpret_cast<void *>(&ReleaseFake);
    for (auto &s : gSubStartVtable) s = reinterpret_cast<void *>(&FakeUnexpected);
    gSubStartVtable[4] = reinterpret_cast<void *>(&SubStartFake);
    gComArg.vtable = gComThis.vtable = gComVtable;
    gService.vtable = gServiceVtable;
    for (int k = 0; k < 8; ++k) gRefObjects[k] = {gRefVtable, k};
    std::memcpy(gDirectorVtable, ctx.image.At<void *>(0xda265c), sizeof(gDirectorVtable));
    gDirectorVtable[12] = reinterpret_cast<void *>(&FrameworkFake);

    void **serviceGlobal = ctx.image.At<void *>(0x10c9378);
    void *savedService = *serviceGlobal;
    *serviceGlobal = &gService;

    bool draw2d = std::strcmp(t.name, "Draw2D") == 0;
    EpochScope epoch(ctx);
    if (draw2d) {
        // The Font and Draw directors it starts: built, then given a vtable that records slot 4.
        ResetStatics(ctx);
        for (unsigned index : {static_cast<unsigned>(kDir_Font), static_cast<unsigned>(kDir_Draw)}) {
            AtexitRedirect redirect(ctx);
            ctx.Original<void *(__cdecl *)()>(kTest[index].getter)();
            *reinterpret_cast<void **>(StaticOf(ctx, index)) = gSubStartVtable;
        }
        gFontStatic = StaticOf(ctx, kDir_Font);
        gDrawStatic = StaticOf(ctx, kDir_Draw);
    }

    // Ours: a copy of our class's vtable with slot 12 recorded, our pristine slot 4 called. The adapter's route: the
    // recording game vtable with slot 4 trapped.
    static void *oursVt[17], *adapterVt[17];
    void **pristine = nullptr;
    {
        uint8_t *probe = MakeOursOf(400, t.index);
        pristine = *reinterpret_cast<void ***>(probe);
        FreeDirector(probe);
    }
    std::memcpy(oursVt, pristine, sizeof(oursVt));
    oursVt[12] = reinterpret_cast<void *>(&FrameworkFake);
    std::memcpy(adapterVt, gDirectorVtable, sizeof(adapterVt));
    adapterVt[4] = TrapOf(1);

    const unsigned n = t.creates;
    for (unsigned outMask = 0; outMask < (1u << n); ++outMask) {
        for (unsigned retMask = 0; retMask < (1u << n); ++retMask) {
            for (int mode = -1; mode < static_cast<int>(t.framed ? n : 2 * n); ++mode) {
                std::vector<uint32_t> logs[3];
                unsigned long escaped[3];
                bool results[3];
                const int sides = Dropped(t.rva) ? 2 : 3; // a dropped adapter: no adapter side
                for (int side = 0; side < sides; ++side) {
                    uint8_t *d = side == 1 ? MakeOursOf(401, t.index) : MakeDirector(ctx, 401);
                    void *saved4 = *reinterpret_cast<void **>(d + 4);
                    *reinterpret_cast<void **>(d) = side == 0 ? gDirectorVtable : side == 1 ? oursVt : adapterVt;
                    *reinterpret_cast<void **>(d + 0x30) = &gComArg; // the field differs from what slot 12 returns
                    gDirectorNow = d;
                    for (unsigned k = 0; k < 8; ++k) {
                        gCreateOut[k] = (outMask >> k) & 1;
                        gCreateRet[k] = (retMask >> k) & 1;
                    }
                    gCreateCalls = gAddCalls = gReleaseCalls = 0;
                    gThrowAdd = mode >= 0 && mode < static_cast<int>(n) ? mode : -1;
                    gThrowRelease = mode >= static_cast<int>(n) ? mode - static_cast<int>(n) : -1;
                    gLog.clear();
                    // The adapter is __fastcall (face, unused edx, com): called as __thiscall, as the game's vtable calls it.
                    void *fn = side == 0 ? ctx.image.At(t.rva) : side == 1 ? pristine[4] : reinterpret_cast<void *>(kAdapters[t.index].onStart);
                    gTrapHit = false;
                    StartCall call{fn, d, &gComArg, false};
                    escaped[side] = EscapedStart(&call);
                    results[side] = call.result;
                    logs[side] = gLog;
                    if (gTrapHit) ctx.Fail("%s OnStart: the adapter dispatched to its own slot", t.name);
                    *reinterpret_cast<void **>(d) = side == 1 ? static_cast<void *>(pristine) : ctx.image.At<void>(0xda265c);
                    *reinterpret_cast<void **>(d + 4) = saved4;
                    FreeDirector(d);
                }
                ++ctx.cases;
                for (int side = 1; side < sides; ++side) {
                    bool same = escaped[0] == escaped[side] && logs[0] == logs[side] && (escaped[0] != 0 || results[0] == results[side]);
                    if (!same)
                        ctx.Fail("%s OnStart %s (out %x, ret %x, mode %d): escaped %lx/%lx, result %d/%d, log %zu/%zu words", t.name,
                                 side == 1 ? "ours" : "adapter", outMask, retMask, mode, escaped[0], escaped[side], results[0], results[side],
                                 logs[0].size(), logs[side].size());
                }
            }
        }
    }
    gThrowAdd = gThrowRelease = -1;
    *serviceGlobal = savedService;
    if (draw2d) ResetStatics(ctx);
}

} // namespace

struct Registered {
    std::deque<std::string> names;
    Registered()
    {
        auto add = [&](const std::string &name, uint32_t rva, std::function<void(difftest::Context &)> body) {
            names.push_back(name);
            new difftest::Registrar(names.back().c_str(), rva, std::move(body));
        };
        for (unsigned i = 0; i < kTestCount; ++i) {
            const TestDirector &t = kTest[i];
            if (t.ctor) add(std::string("COMDirectors_Construct_") + t.name, t.ctor, [i](difftest::Context &ctx) { TestConstructor(ctx, i); });
            if (t.index != kDir_MadCow) add(std::string("COMDirectors_Getter_") + t.name, t.getter, [i](difftest::Context &ctx) { TestGetter(ctx, i); });
            add(std::string("COMDirectors_IdThunk_") + t.name, t.thunk, [i](difftest::Context &ctx) { TestIdThunk(ctx, i); });
            if (t.body) add(std::string("COMDirectors_DestructorBody_") + t.name, t.body, [i](difftest::Context &ctx) { TestDestructorBody(ctx, i); });
        }
        for (const StartTest &s : kStartTests)
            add(std::string("COMDirectors_OnStart_") + s.name, s.rva, [&s](difftest::Context &ctx) { TestOnStart(ctx, s); });
        // The deleting destructors: each distinct function once.
        std::vector<uint32_t> seen = {0x1448ca};
        for (unsigned i = 0; i < kTestCount; ++i) {
            uint32_t rva = kTest[i].deleting;
            bool dup = false;
            for (uint32_t s : seen) dup |= s == rva;
            if (dup) continue;
            seen.push_back(rva);
            char hex[16];
            std::snprintf(hex, sizeof(hex), "%x", rva);
            add(std::string("COMDirectors_DeletingDestructor_") + hex, rva, [rva](difftest::Context &ctx) { TestDeletingDestructor(ctx, rva); });
        }
    }
};
Registered gRegistered;

} // namespace

// The MadCow director is a plain global built by a static initialiser (0x1408d0), not a getter; its
// accessor (0xc5a240) only returns the address.
DIFF_TEST(COMDirectors_InitMadCow, 0x1408d0)
{
    const TestDirector &t = kTest[kDir_MadCow];
    ResetStatics(ctx);
    {
        AtexitRedirect redirect(ctx);
        ctx.Original<void(__cdecl *)()>(0x1408d0)();
    }
    ++ctx.cases;
    if (gImageHandlers.size() != 1) ctx.Fail("MadCow initialiser: the original registered %zu handlers", gImageHandlers.size());
    Canon ca = Take(StaticOf(ctx, kDir_MadCow));
    std::vector<void (*)()> handlersA = gImageHandlers;
    RunHandlers(ctx, handlersA);
    Snap postA = SnapDestroyed(StaticOf(ctx, kDir_MadCow));

    ResetStatics(ctx);
    {
        OursRedirect redirect;
        InitMadCow();
        if (gOursHandlers.size() != 1) ctx.Fail("MadCow initialiser: ours registered %zu handlers", gOursHandlers.size());
        Canon cb = Take(StaticOf(ctx, kDir_MadCow));
        std::string d = Diff(ca, cb);
        if (!d.empty()) ctx.Fail("MadCow initialiser: %s", d.c_str());
        if (ca.nodes.size() != 4) ctx.Fail("MadCow initialiser: %zu classes", ca.nodes.size());
        RunHandlers(ctx, gOursHandlers);
    }
    if (!SameSnap(postA, SnapDestroyed(StaticOf(ctx, kDir_MadCow)))) ctx.Fail("MadCow initialiser: destroyed differently");
    // The accessor.
    void *a = ctx.Original<void *(__cdecl *)()>(t.getter)();
    if (a != StaticOf(ctx, kDir_MadCow) || GetDirector(kDir_MadCow) != a) ctx.Fail("MadCow accessor returned %p / %p", a, GetDirector(kDir_MadCow));
    ResetStatics(ctx);
}

DIFF_TEST(COMDirectors_MadCowAccessor, 0xc5a240)
{
    void *a = ctx.Original<void *(__cdecl *)()>(0xc5a240)();
    ++ctx.cases;
    if (GetDirector(kDir_MadCow) != a) ctx.Fail("MadCow accessor: ours %p, original %p", GetDirector(kDir_MadCow), a);
}

// The helpers the application director's constructor calls (cdecl, the director on the stack).
namespace {
void TestHelper(difftest::Context &ctx, uint32_t rva, void (*ours)(Base *))
{
    auto orig = ctx.Original<void(__cdecl *)(void *)>(rva);
    EpochScope epoch(ctx);
    for (int rep = 0; rep < 2; ++rep) {
        uint8_t *a = MakeDirector(ctx, 301);
        uint8_t *b = MakeOurs(301);
        {
            AtexitRedirect redirect(ctx);
            orig(a);
        }
        epoch.Reset();
        {
            OursRedirect redirect;
            ours(AsBase(b));
            if (!gOursHandlers.empty()) ctx.Fail("helper 0x%x: ours built a director", rva);
        }
        ++ctx.cases;
        Canon ca = Take(a), cb = Take(b);
        std::string d = Diff(ca, cb);
        if (!d.empty()) ctx.Fail("helper 0x%x: %s", rva, d.c_str());
        if (ca.subs.empty()) ctx.Fail("helper 0x%x added no director", rva);
        for (uint8_t *p : {a, b}) Subs(p).clear();
        FreeDirector(a);
        FreeDirector(b);
    }
}
} // namespace

// The root director's own slots: the id thunk 0x1448f1 and slot 17 (0x1448f7).
DIFF_TEST(COMDirectors_RootDirectorSlots, 0x1448f7)
{
    void **serviceGlobal = ctx.image.At<void *>(0x10c9378);
    void *saved = *serviceGlobal;
    static FakeObject service{};
    auto orig = ctx.Original<bool(__thiscall *)(void *)>(0x1448f7);
    // Our root: built by our constructor (with a framework whose COM object is null, so it stores nothing at +0x30).
    static void *comVtable[32];
    comVtable[23] = reinterpret_cast<void *>(&NullComObject);
    static void *fakeFramework[1] = {comVtable};
    auto makeOurRoot = [&](uint32_t seed) {
        uint8_t *q = NewBlock(seed);
        *serviceGlobal = fakeFramework;
        rz::cGZCOMDirectorAdapters::Construct(q, nullptr);
        return q;
    };
    for (int route = 0; route < 2; ++route)
    for (void *value : {static_cast<void *>(nullptr), static_cast<void *>(&service), kFrameworkMarker}) {
        uint8_t *a = MakeDirector(ctx, 501);
        // The original's side carries the root's game vtables (0xd9b754 / 0xd9b7a0), as the original's constructor leaves them.
        *reinterpret_cast<void **>(a) = ctx.image.At<void>(0xd9b754);
        *reinterpret_cast<void **>(a + 4) = ctx.image.At<void>(0xd9b7a0);
        uint8_t *b = route == 0 ? makeOurRoot(501) : Trapped(MakeDirector(ctx, 501), 0, 17, 0, 0xd9b754, 0xd9b7a0);
        for (uint8_t *p : {a, b}) std::memset(p + 0x30, 0x5a, 4); // the same +0x30 on both sides
        *serviceGlobal = value;
        bool ra = orig(a);
        bool rb = route == 0 ? CallSlot<bool>(b, 17) : rz::cGZCOMDirectorAdapters::StoreFrameWork(Face0(b), nullptr);
        *serviceGlobal = saved;
        if (gTrapHit) ctx.Fail("slot 17: the adapter dispatched to its own slot");
        Untrap(b);
        ++ctx.cases;
        if (ra != rb || ra != (value != nullptr)) ctx.Fail("slot 17 (route %d) with service %p: original %d, ours %d", route, value, ra, rb);
        if (Read<uint32_t>(a, 0x34) != Read<uint32_t>(b, 0x34) || Read<uint32_t>(b, 0x34) != U(value)) ctx.Fail("slot 17 with service %p: field 0x34 is %08x / %08x", value, Read<uint32_t>(a, 0x34), Read<uint32_t>(b, 0x34));
        std::string d = Diff(Take(a), Take(b));
        if (!d.empty()) ctx.Fail("slot 17 (route %d): %s", route, d.c_str());
        FreeDirector(a);
        FreeDirector(b);
    }
    *serviceGlobal = saved;
    // The id (0x1448f1): our root's slot 13, and the adapter with slot 13 trapped.
    uint8_t *p = MakeDirector(ctx, 502);
    uint8_t *q = makeOurRoot(502);
    *serviceGlobal = saved;
    uint8_t *r = Trapped(MakeDirector(ctx, 502), 0, 13, 0, 0xd9b754, 0xd9b7a0);
    uint32_t id = ctx.Original<uint32_t(__thiscall *)(void *)>(0x1448f1)(p);
    uint32_t ours = CallSlot<uint32_t>(q, 13);
    uint32_t adapter = rz::cGZCOMDirectorAdapters::GetDirectorID(Face0(r), nullptr);
    if (gTrapHit) ctx.Fail("root id: the adapter dispatched to its own slot");
    Untrap(r);
    ++ctx.cases;
    if (id != 0xa3cad79a || ours != id || adapter != id) ctx.Fail("root director id thunk 0x1448f1: %08x, ours %08x, adapter %08x", id, ours, adapter);
    for (uint8_t *x : {p, q, r}) FreeDirector(x);
}

DIFF_TEST(COMDirectors_AddTSSGHelper, 0x140fe3)
{
    TestHelper(ctx, 0x140fe3, &AddTSSGHelper);
}

DIFF_TEST(COMDirectors_AddEngineHelper, 0x140ff3)
{
    TestHelper(ctx, 0x140ff3, &AddEngineHelper);
}

// 0x416530: the probe the Sim director's constructor makes. Only non-zero words are run: with a zero word
// the original recurses until the stack overflows (see SimProbe).
DIFF_TEST(COMDirectors_SimProbe, 0x416530)
{
    int *word = ctx.image.At<int>(0x10118ac);
    int saved = *word;
    for (int value : {6, 1, -1, 0x7fffffff}) {
        *word = value;
        bool a = ctx.Original<bool(__cdecl *)()>(0x416530)() & 1;
        bool b = SimProbe();
        ++ctx.cases;
        if (a != b || !a) ctx.Fail("SimProbe with word %d: original %d, ours %d", value, a, b);
    }
    *word = saved;
}

// =============================================================================================
// The audio director (cTSAudioCOMDirector, 0xa8 bytes)
//
// Same method as above, on 0xa8-byte blocks: the constructor (with the statics of the six
// sub-directors it adds, and with each of them throwing from InitializeCOM), the getter (the audio
// static is checked and destroyed with the other 41), the destructor and the deleting destructor (with
// ten fake references, a set with nodes and the heap blocks checked), the id thunk, OnStart (fakes for
// the framework objects, the service object, the created object, the five sub-directors started and the
// log object cRZStd), PreAppInit / PostAppShutdown (the message-id walk and the log lines) and the
// thunks of the +4 and +0x64 subobjects.

namespace {

constexpr size_t kAudioSize = 0xa8;
constexpr size_t kAudioBlock = 0xc8;
constexpr uint32_t kAudioStatic = 0x10cc328, kAudioGuard = 0x10cc3d0;
constexpr unsigned kAudioSubs[] = {kDir_Sound, kDir_HitMan, kDir_SndEa, kDir_Freshness, kDir_HitMp3, kDir_SpeexCodec};

uint8_t *NewAudioBlock(uint32_t seed)
{
    auto *p = static_cast<uint8_t *>(::operator new(kAudioBlock));
    std::mt19937 rng(seed);
    for (size_t i = 0; i < kAudioSize; ++i) p[i] = static_cast<uint8_t>(rng());
    std::memset(p + kAudioSize, kCanary, kAudioBlock - kAudioSize);
    return p;
}

bool AudioCanaryIntact(const uint8_t *p)
{
    for (size_t i = kAudioSize; i < kAudioBlock; ++i)
        if (p[i] != kCanary) return false;
    return true;
}

AudioDirector *AsAudio(uint8_t *p) { return reinterpret_cast<AudioDirector *>(p); }

void FreeAudio(uint8_t *p)
{
    AsAudio(p)->Destruct();
    ::operator delete(p);
}

// The preorder of the tree nodes of the id set (key and colour), for comparing the shape.
void SetShape(const uint8_t *node, std::vector<uint32_t> &out)
{
    if (node == nullptr || node[0xd] != 0 || out.size() > 1000) return;
    out.push_back(Read<uint32_t>(node, 0x10));
    out.push_back(node[0xc]);
    SetShape(Read<const uint8_t *>(node, 0), out);
    SetShape(Read<const uint8_t *>(node, 8), out);
}

struct AudioCanon {
    Canon base;
    uint8_t tail[0x3c]; // +0x64..+0xa0: the vtable of the message target, ten references, four words
    std::vector<uint32_t> ids, shape;
    uint32_t idCount = 0;
};

AudioCanon TakeAudio(const uint8_t *d)
{
    AudioCanon c;
    c.base = Take(d);
    std::memcpy(c.tail, d + 0x64, sizeof(c.tail));
    const auto &set = *reinterpret_cast<const std::set<uint32_t> *>(d + 0xa0);
    c.idCount = Read<uint32_t>(d, 0xa4);
    for (uint32_t id : set) c.ids.push_back(id);
    const uint8_t *head = Read<const uint8_t *>(d, 0xa0);
    SetShape(Read<const uint8_t *>(head, 4), c.shape);
    return c;
}

std::string AudioDiff(const AudioCanon &a, const AudioCanon &b)
{
    std::string d = Diff(a.base, b.base);
    if (!d.empty()) return d;
    char buf[200];
    for (size_t i = 0; i < sizeof(a.tail); ++i)
        if (a.tail[i] != b.tail[i]) {
            std::snprintf(buf, sizeof(buf), "byte +0x%zx: original %02x, ours %02x", 0x64 + i, a.tail[i], b.tail[i]);
            return buf;
        }
    if (a.idCount != b.idCount || a.ids != b.ids) return "id set contents differ";
    if (a.shape != b.shape) return "id set shape differs";
    return std::string();
}

// A destroyed audio director: all 0xa8 bytes, with the freed words masked (the list head at +0x48, the
// freed string pointer's upper bytes, the id set's freed head at +0xa0).
struct AudioSnap {
    uint8_t bytes[kAudioSize];
};

AudioSnap SnapAudio(const uint8_t *object)
{
    AudioSnap s;
    std::memcpy(s.bytes, object, sizeof(s.bytes));
    std::memset(s.bytes + 0x48, 0, 4);
    std::memset(s.bytes + 0x15, 0, 3);
    std::memset(s.bytes + 0xa0, 0, 4);
    return s;
}

bool SameAudioSnap(const AudioSnap &a, const AudioSnap &b) { return std::memcmp(a.bytes, b.bytes, sizeof(a.bytes)) == 0; }

// An exception escaping a call: its code, or 0 (no C++ objects in this function: __try).
unsigned long AudioEscaped(void (*fn)(void *), void *arg)
{
    __try {
        fn(arg);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Constructor

void *(__thiscall *gAudioOrigCtor)(void *) = nullptr;
void AudioCallOriginalCtor(void *p) { gAudioOrigCtor(p); }
void *(__cdecl *gAudioOrigGetter)() = nullptr;
void *ctxOriginalGetter() { return gAudioOrigGetter(); }
void AudioCallOurCtor(void *p) { AsAudio(static_cast<uint8_t *>(p))->Construct(); }

void *gAudioThrowVtable[17];
void __fastcall AudioThrowInit(void *, void *, void *, void *) { throw 5; }

} // namespace

DIFF_TEST(COMDirectors_Construct_Audio, 0x20e164)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0x20e164);
    EpochScope epoch(ctx);
    EnsureMadCow(ctx);
    for (uint32_t seed : {301u, 302u}) {
        uint8_t *a = NewAudioBlock(seed);
        uint8_t *b = NewAudioBlock(seed);
        void *ra, *rb;
        {
            AtexitRedirect redirect(ctx);
            ra = orig(a);
        }
        epoch.Reset();
        {
            OursRedirect redirect;
            rb = AsAudio(b)->Construct();
            if (!gOursHandlers.empty()) ctx.Fail("audio constructor registered %zu exit handlers", gOursHandlers.size());
        }
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("audio constructor returned %p/%p for %p/%p", ra, rb, a, b);
        if (!AudioCanaryIntact(a) || !AudioCanaryIntact(b)) ctx.Fail("audio constructor wrote past the object");
        AudioCanon ca = TakeAudio(a), cb = TakeAudio(b);
        std::string d = AudioDiff(ca, cb);
        if (!d.empty()) ctx.Fail("audio constructor: %s", d.c_str());
        if (ca.base.nodes.size() != kAudioClassCount || ca.base.subs.size() < 6 || ca.ids.size() != 3)
            ctx.Fail("audio constructor registered %zu classes, %zu directors, %zu ids", ca.base.nodes.size(), ca.base.subs.size(), ca.ids.size());
        FreeAudio(a);
        FreeAudio(b);
    }

    // One of the six sub-directors throws from InitializeCOM (slot 3): the unwind destroys the id set
    // and the base on both sides, and the exception escapes.
    std::memcpy(gAudioThrowVtable, ctx.image.At<void *>(0xda265c), sizeof(gAudioThrowVtable));
    gAudioThrowVtable[3] = reinterpret_cast<void *>(&AudioThrowInit);
    gAudioOrigCtor = orig;
    for (unsigned k = 0; k < 6; ++k) {
        uint8_t *sub = StaticOf(ctx, kAudioSubs[k]);
        void *savedVtable = *reinterpret_cast<void **>(sub);
        *reinterpret_cast<void **>(sub) = gAudioThrowVtable;
        uint8_t *a = NewAudioBlock(310 + k);
        uint8_t *b = NewAudioBlock(310 + k);
        unsigned long codeA, codeB;
        bool headFreedA, headFreedB;
        // Frees recorded across each side's call. The head is allocated inside the call, so Freed() would also
        // be true for a leaked head at the address of an earlier temporary freed in the same call; the snapshot
        // comparison below still sees the object state.
        {
            AtexitRedirect redirect(ctx);
            difftest::FreeRecorder rec;
            codeA = AudioEscaped(&AudioCallOriginalCtor, a);
            rec.Stop();
            headFreedA = rec.Freed(Read<void *>(a, 0xa0));
        }
        {
            OursRedirect redirect;
            difftest::FreeRecorder rec;
            codeB = AudioEscaped(&AudioCallOurCtor, b);
            rec.Stop();
            headFreedB = rec.Freed(Read<void *>(b, 0xa0));
        }
        *reinterpret_cast<void **>(sub) = savedVtable;
        ++ctx.cases;
        if (!headFreedA || !headFreedB) ctx.Fail("audio constructor with sub-director %u throwing: id set head freed by the unwind: original %d, ours %d", k, headFreedA, headFreedB);
        if (codeA == 0 || codeA != codeB) ctx.Fail("audio constructor with sub-director %u throwing: escaped %lx / %lx", k, codeA, codeB);
        if (!SameAudioSnap(SnapAudio(a), SnapAudio(b))) ctx.Fail("audio constructor with sub-director %u throwing: the unwound objects differ", k);
        ::operator delete(a);
        ::operator delete(b);
    }
}

// ---------------------------------------------------------------------------------------------
// Getter

namespace {

// The 41 statics and the audio static: which are built, their canonical forms, the guards.
struct AudioStaticsState {
    StaticsState dirs;
    bool audioBuilt = false;
    AudioCanon audio;
    int audioGuard = 0;
};

bool AudioBuilt(difftest::Context &ctx) { return Read<uint32_t>(ctx.image.At<uint8_t>(kAudioStatic), 0) != 0xCDCDCDCD; }

AudioStaticsState SnapshotAudioStatics(difftest::Context &ctx)
{
    AudioStaticsState s;
    s.dirs = SnapshotStatics(ctx);
    s.audioBuilt = AudioBuilt(ctx);
    if (s.audioBuilt) s.audio = TakeAudio(ctx.image.At<uint8_t>(kAudioStatic));
    int g = *ctx.image.At<int>(kAudioGuard);
    s.audioGuard = g != 0 && g != -1 ? 1 : 0;
    return s;
}

void ResetAudioStatics(difftest::Context &ctx)
{
    ResetStatics(ctx);
    std::memset(ctx.image.At<uint8_t>(kAudioStatic), 0xCD, kAudioSize);
    *ctx.image.At<int>(kAudioGuard) = 0;
}

// Runs the exit handlers in order; which static each destroyed (an index into the 41, 41 for the audio static), or -1.
std::vector<int> RunAudioHandlers(difftest::Context &ctx, const std::vector<void (*)()> &handlers)
{
    std::vector<int> destroyed;
    for (void (*handler)() : handlers) {
        std::vector<uint32_t> before;
        for (unsigned j = 0; j < kTestCount; ++j) before.push_back(Read<uint32_t>(StaticOf(ctx, j), 0));
        before.push_back(Read<uint32_t>(ctx.image.At<uint8_t>(kAudioStatic), 0));
        handler();
        int which = -1;
        for (unsigned j = 0; j <= kTestCount; ++j) {
            uint32_t now = Read<uint32_t>(j < kTestCount ? StaticOf(ctx, j) : ctx.image.At<uint8_t>(kAudioStatic), 0);
            if (before[j] != 0xCDCDCDCD && VCode(now) == kBaseCode && VCode(before[j]) != kBaseCode) which = static_cast<int>(j);
        }
        destroyed.push_back(which);
    }
    return destroyed;
}

} // namespace

DIFF_TEST(COMDirectors_Getter_Audio, 0x20efaf)
{
    EpochScope epoch(ctx);
    ResetAudioStatics(ctx);
    EnsureMadCow(ctx);
    epoch.Reset();
    void *a;
    {
        AtexitRedirect redirect(ctx);
        a = ctx.Original<void *(__cdecl *)()>(0x20efaf)();
    }
    AudioStaticsState stateA = SnapshotAudioStatics(ctx);
    std::vector<void (*)()> handlersA = gImageHandlers;
    std::vector<int> orderA = RunAudioHandlers(ctx, handlersA);
    std::vector<Snap> destroyedA = SnapshotDestroyed(ctx);
    AudioSnap audioDestroyedA = SnapAudio(ctx.image.At<uint8_t>(kAudioStatic));

    ResetAudioStatics(ctx);
    EnsureMadCow(ctx);
    epoch.Reset();
    void *b;
    std::vector<void (*)()> handlersB;
    {
        OursRedirect redirect;
        b = GetAudioDirector();
        handlersB = gOursHandlers;
    }
    AudioStaticsState stateB = SnapshotAudioStatics(ctx);
    std::vector<int> orderB = RunAudioHandlers(ctx, handlersB);
    std::vector<Snap> destroyedB = SnapshotDestroyed(ctx);
    AudioSnap audioDestroyedB = SnapAudio(ctx.image.At<uint8_t>(kAudioStatic));
    ++ctx.cases;

    uint8_t *object = ctx.image.At<uint8_t>(kAudioStatic);
    if (a != object || b != a) ctx.Fail("audio getter returned %p (original %p), expected %p", b, a, object);
    unsigned builtCount = 0;
    for (unsigned j = 0; j < kTestCount; ++j) {
        if (j == kDir_MadCow) continue;
        if (stateA.dirs.built[j] != stateB.dirs.built[j]) ctx.Fail("audio getter: %s %s by the original, %s by ours", kTest[j].name, stateA.dirs.built[j] ? "built" : "not built", stateB.dirs.built[j] ? "built" : "not built");
        if (stateA.dirs.guardState[j] != stateB.dirs.guardState[j]) ctx.Fail("audio getter: guard of %s differs", kTest[j].name);
        if (stateA.dirs.built[j] && stateB.dirs.built[j]) {
            ++builtCount;
            std::string d = Diff(stateA.dirs.canon[j], stateB.dirs.canon[j]);
            if (!d.empty()) ctx.Fail("audio getter: %s differs: %s", kTest[j].name, d.c_str());
        }
    }
    if (!stateA.audioBuilt || !stateB.audioBuilt) ctx.Fail("audio getter: audio static built %d / %d", stateA.audioBuilt, stateB.audioBuilt);
    if (stateB.audioGuard != 1 || stateA.audioGuard != 1) ctx.Fail("audio getter left guard states %d / %d", stateA.audioGuard, stateB.audioGuard);
    std::string d = AudioDiff(stateA.audio, stateB.audio);
    if (!d.empty()) ctx.Fail("audio getter: the audio static differs: %s", d.c_str());
    if (handlersA.size() != handlersB.size()) ctx.Fail("audio getter: the original registered %zu exit handlers, ours %zu", handlersA.size(), handlersB.size());
    if (handlersB.size() != builtCount + 1) ctx.Fail("audio getter: %zu handlers for %u built statics and the audio one", handlersB.size(), builtCount);
    if (orderA != orderB) ctx.Fail("audio getter: the handlers destroy the statics in a different order");
    for (int j : orderB)
        if (j < 0) {
            ctx.Fail("audio getter: a handler destroyed no static");
            break;
        }
    if (orderB.empty() || orderB.back() != static_cast<int>(kTestCount)) ctx.Fail("audio getter: the audio static is not destroyed last");
    for (int j : orderB)
        if (j >= 0 && j < static_cast<int>(kTestCount) && !SameSnap(destroyedA[j], destroyedB[j])) ctx.Fail("audio getter: %s destroyed differently", kTest[j].name);
    if (!SameAudioSnap(audioDestroyedA, audioDestroyedB)) ctx.Fail("audio getter: the audio static destroyed differently");

    // The fast path: built, both return the static and register nothing.
    ResetAudioStatics(ctx);
    EnsureMadCow(ctx);
    epoch.Reset();
    {
        AtexitRedirect redirect(ctx);
        ctx.Original<void *(__cdecl *)()>(0x20efaf)();
    }
    AudioStaticsState before = SnapshotAudioStatics(ctx);
    void *a2, *b2;
    {
        AtexitRedirect redirect(ctx);
        a2 = ctx.Original<void *(__cdecl *)()>(0x20efaf)();
        size_t originalRegistrations = gImageHandlers.size();
        OursRedirect ours;
        b2 = GetAudioDirector();
        ++ctx.cases;
        if (originalRegistrations != 0 || !gOursHandlers.empty()) ctx.Fail("audio getter: the second use registered handlers (%zu, %zu)", originalRegistrations, gOursHandlers.size());
    }
    AudioStaticsState after = SnapshotAudioStatics(ctx);
    if (a2 != b2 || a2 != object) ctx.Fail("audio getter fast path returned %p/%p", a2, b2);
    if (!AudioDiff(before.audio, after.audio).empty()) ctx.Fail("audio getter: the fast path changed the audio static");
    ResetAudioStatics(ctx);
}

// The getter when the constructor throws (a sub-director's InitializeCOM does): the exception escapes,
// the guard is abandoned (_Init_thread_abort: back to 0, so the next use tries again), nothing is
// registered for exit; then, with the sub-director repaired, the next use builds the static.
DIFF_TEST(COMDirectors_Getter_Audio_Throws, 0x20efaf)
{
    EpochScope epoch(ctx);
    std::memcpy(gAudioThrowVtable, ctx.image.At<void *>(0xda265c), sizeof(gAudioThrowVtable));
    gAudioThrowVtable[3] = reinterpret_cast<void *>(&AudioThrowInit);
    for (unsigned k = 0; k < 6; k += 5) {
        struct Side {
            unsigned long escaped = 0;
            int guardAfterThrow = 0, guardAfterRetry = 0;
            size_t handlersAfterThrow = 0, handlersAfterRetry = 0;
            AudioSnap snap{};
            AudioCanon retry;
            void *retryResult = nullptr;
        } sides[2];
        for (int side = 0; side < 2; ++side) {
            ResetAudioStatics(ctx);
            EnsureMadCow(ctx);
            epoch.Reset();
            uint8_t *sub = StaticOf(ctx, kAudioSubs[k]);
            void *savedVtable = nullptr;
            {
                AtexitRedirect redirect(ctx);
                for (unsigned s : kAudioSubs) ctx.Original<void *(__cdecl *)()>(kTest[s].getter)();
                savedVtable = *reinterpret_cast<void **>(sub);
            }
            *reinterpret_cast<void **>(sub) = gAudioThrowVtable;
            gOursHandlers.clear();
            Side &r = sides[side];
            if (side == 0) {
                AtexitRedirect redirect(ctx);
                gAudioOrigGetter = ctx.Original<void *(__cdecl *)()>(0x20efaf);
                r.escaped = AudioEscaped([](void *) { ctxOriginalGetter(); }, nullptr);
                r.handlersAfterThrow = gImageHandlers.size();
            } else {
                OursRedirect redirect;
                r.escaped = AudioEscaped([](void *) { GetAudioDirector(); }, nullptr);
                r.handlersAfterThrow = gOursHandlers.size();
            }
            r.guardAfterThrow = *ctx.image.At<int>(kAudioGuard);
            r.snap = SnapAudio(ctx.image.At<uint8_t>(kAudioStatic));
            *reinterpret_cast<void **>(sub) = savedVtable;
            epoch.Reset();
            if (side == 0) {
                AtexitRedirect redirect(ctx);
                r.retryResult = ctxOriginalGetter();
                r.handlersAfterRetry = gImageHandlers.size();
            } else {
                OursRedirect redirect;
                r.retryResult = GetAudioDirector();
                r.handlersAfterRetry = gOursHandlers.size();
            }
            r.guardAfterRetry = *ctx.image.At<int>(kAudioGuard);
            r.retry = TakeAudio(ctx.image.At<uint8_t>(kAudioStatic));
        }
        ++ctx.cases;
        const Side &a = sides[0], &b = sides[1];
        if (a.escaped == 0 || a.escaped != b.escaped) ctx.Fail("audio getter with sub-director %u throwing: escaped %lx / %lx", k, a.escaped, b.escaped);
        if (a.guardAfterThrow != b.guardAfterThrow || a.guardAfterThrow != 0) ctx.Fail("audio getter after the throw: guard %d / %d (expected 0)", a.guardAfterThrow, b.guardAfterThrow);
        if (a.handlersAfterThrow != b.handlersAfterThrow) ctx.Fail("audio getter after the throw: %zu / %zu exit handlers", a.handlersAfterThrow, b.handlersAfterThrow);
        if (!SameAudioSnap(a.snap, b.snap)) ctx.Fail("audio getter after the throw: the unwound statics differ");
        if (a.handlersAfterRetry != b.handlersAfterRetry || b.handlersAfterRetry == 0) ctx.Fail("audio getter retry: %zu / %zu exit handlers", a.handlersAfterRetry, b.handlersAfterRetry);
        if (a.guardAfterRetry == 0 || (a.guardAfterRetry != 0) != (b.guardAfterRetry != 0)) ctx.Fail("audio getter retry: guard %d / %d", a.guardAfterRetry, b.guardAfterRetry);
        std::string d = AudioDiff(a.retry, b.retry);
        if (!d.empty()) ctx.Fail("audio getter retry: %s", d.c_str());
    }
    ResetAudioStatics(ctx);
}

// ---------------------------------------------------------------------------------------------
// Destructors and the id thunk

namespace {

struct AudioRef {
    void **vtable;
    int index;
};
AudioRef gAudioRefs[10];
void *gAudioRefVtable[4];
bool gAudioReleaseNoted = false;

uint32_t __fastcall AudioRefRelease(void *self, void *)
{
    Log({90, static_cast<uint32_t>(static_cast<AudioRef *>(self)->index)});
    return 0;
}

struct LoadedAudio {
    uint8_t *p;
};

AudioDirectorHooks *AsAudioHooks(uint8_t *p) { return reinterpret_cast<AudioDirectorHooks *>(p); }

// An audio director with everything the destructor frees: a long path, sub-directors, classes, the
// references of `refMask` (bit k: field k), and an id set with three nodes.
LoadedAudio MakeLoadedAudio(difftest::Context &ctx, uint32_t seed, uint32_t refMask, const std::vector<uint32_t> &ids)
{
    for (auto &s : gAudioRefVtable) s = reinterpret_cast<void *>(&FakeUnexpected);
    gAudioRefVtable[2] = reinterpret_cast<void *>(&AudioRefRelease);
    for (int k = 0; k < 10; ++k) gAudioRefs[k] = {gAudioRefVtable, k};
    LoadedAudio l;
    l.p = NewAudioBlock(seed);
    ctx.Original<OrigBase>(0x1adffa)(l.p);
    auto *d = AsAudio(l.p);
    AsBase(l.p)->mLibraryPath.value.mString.assign(40, 'x');
    for (int k = 0; k < 3; ++k) AsBase(l.p)->mChildren.push_back(reinterpret_cast<Base *>(0x1000 + k));
    for (uint32_t k = 0; k < 12; ++k) d->AddCls(0x300 + k, reinterpret_cast<void *>(0x40 + k));
    for (int k = 0; k < 10; ++k) d->mRefs[k] = (refMask >> k) & 1 ? &gAudioRefs[k] : nullptr;
    new (&d->mMessageIds) std::set<uint32_t>();
    for (uint32_t id : ids) d->mMessageIds.insert(id);
    return l;
}

// The nodes of the set (excluding the head), collected before the destructor frees them.
std::vector<void *> SetNodes(uint8_t *p)
{
    std::vector<void *> nodes;
    auto &set = *reinterpret_cast<std::set<uint32_t> *>(p + 0xa0);
    void *head = Read<void *>(p, 0xa0);
    // The nodes are reached from the head: its left link is the smallest, then each node's successor
    // is found through the iterator (the node address is the iterator's pointer, which the set exposes
    // only through the key's address minus 0x10).
    for (const uint32_t &key : set) nodes.push_back(const_cast<uint8_t *>(reinterpret_cast<const uint8_t *>(&key)) - 0x10);
    nodes.push_back(head);
    return nodes;
}

bool AnyAllocated(const difftest::FreeRecorder &rec, const std::vector<void *> &blocks)
{
    for (void *b : blocks)
        if (!rec.Freed(b)) return true;
    return false;
}

void TestAudioDestructors(difftest::Context &ctx, bool deleting)
{
    auto origBody = ctx.Original<void(__thiscall *)(void *)>(0x20e676);
    auto origDeleting = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0x20e790);
    const std::vector<std::vector<uint32_t>> idSets = {{}, {0xca}, {0xca, 0xcb, 0xeded3807}, {5, 3, 8, 1, 9, 2, 7}};
    const uint32_t masks[] = {0, 0x3ff, 0x155, 0x2aa, 1, 0x200, 0x0f0};
    int seed = 600;
    for (const auto &ids : idSets) {
        for (uint32_t mask : masks) {
            for (uint32_t flags : deleting ? std::vector<uint32_t>{0, 1, 2, 3} : std::vector<uint32_t>{0}) {
                LoadedAudio a = MakeLoadedAudio(ctx, seed, mask, ids);
                LoadedAudio b = MakeLoadedAudio(ctx, seed, mask, ids);
                ++seed;
                std::vector<void *> nodesA = SetNodes(a.p), nodesB = SetNodes(b.p);
                void *ra = nullptr, *rb = nullptr;
                gLog.clear();
                difftest::FreeRecorder recA;
                if (deleting)
                    ra = origDeleting(a.p, flags);
                else
                    origBody(a.p);
                recA.Stop();
                bool setLeakA = AnyAllocated(recA, nodesA);
                bool objectFreedA = deleting && recA.Freed(a.p);
                std::vector<uint32_t> logA = gLog;
                gLog.clear();
                difftest::FreeRecorder recB;
                if (deleting)
                    rb = AsAudio(b.p)->DeletingDestructor(flags);
                else
                    AsAudio(b.p)->Destruct();
                recB.Stop();
                bool setLeakB = AnyAllocated(recB, nodesB);
                bool objectFreedB = deleting && recB.Freed(b.p);
                std::vector<uint32_t> logB = gLog;
                ++ctx.cases;
                if (deleting && (ra != a.p || rb != b.p)) ctx.Fail("audio deleting destructor flags %u: returned %p/%p for %p/%p", flags, ra, rb, a.p, b.p);
                if (setLeakA || setLeakB) ctx.Fail("audio destructor (%zu ids, refs %x): set blocks still allocated, original %d, ours %d", ids.size(), mask, setLeakA, setLeakB);
                if (deleting && (objectFreedA != ((flags & 1) != 0) || objectFreedB != ((flags & 1) != 0)))
                    ctx.Fail("audio deleting destructor flags %u: freed original %d, ours %d", flags, objectFreedA, objectFreedB);
                if (logA != logB) ctx.Fail("audio destructor (%zu ids, refs %x): release calls differ (%zu vs %zu words)", ids.size(), mask, logA.size(), logB.size());
                unsigned expected = 0;
                for (int k = 0; k < 10; ++k) expected += (mask >> k) & 1;
                if (logB.size() != 2 * expected) ctx.Fail("audio destructor (refs %x): %zu words logged, expected %u releases", mask, logB.size(), expected);
                if (!deleting || !(flags & 1)) {
                    if (!SameAudioSnap(SnapAudio(a.p), SnapAudio(b.p))) ctx.Fail("audio destructor (%zu ids, refs %x): the destroyed objects differ", ids.size(), mask);
                    if (!AudioCanaryIntact(a.p) || !AudioCanaryIntact(b.p)) ctx.Fail("audio destructor wrote past the object");
                    ::operator delete(a.p);
                    ::operator delete(b.p);
                }
            }
        }
    }
}

} // namespace

DIFF_TEST(COMDirectors_DestructorBody_Audio, 0x20e676)
{
    TestAudioDestructors(ctx, false);
}

DIFF_TEST(COMDirectors_DeletingDestructor_Audio, 0x20e790)
{
    TestAudioDestructors(ctx, true);
}

DIFF_TEST(COMDirectors_IdThunk_Audio, 0x20f022)
{
    uint8_t *p = MakeDirector(ctx, 321);
    uint32_t a = ctx.Original<uint32_t(__thiscall *)(void *)>(0x20f022)(p);
    uint32_t b = reinterpret_cast<const FixedId<AudioDirector::kId> *>(p)->Get();
    ++ctx.cases;
    if (a != b || a != AudioDirector::kId) ctx.Fail("audio id thunk 0x20f022: original %08x, ours %08x", a, b);
    FreeDirector(p);
}

// ---------------------------------------------------------------------------------------------
// The thunks of the +4 and +0x64 subobjects: they adjust `this` and reach the base slots.

DIFF_TEST(COMDirectors_Audio_Thunks, 0x20f5f1)
{
    auto origQi4 = ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(0x20f5e9);
    auto origQi64 = ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(0x20f5f1);
    auto origAdd4 = ctx.Original<uint32_t(__thiscall *)(void *)>(0x20e7b7);
    auto origAdd64 = ctx.Original<uint32_t(__thiscall *)(void *)>(0x20e7bf);
    auto origRel4 = ctx.Original<uint32_t(__thiscall *)(void *)>(0x20f5fe);
    auto origRel64 = ctx.Original<uint32_t(__thiscall *)(void *)>(0x20f606);
    // The jump thunks reached through the vtable: slot 0, 1, 2 of the primary vtable.
    auto origQi0 = ctx.Original<bool(__thiscall *)(void *, uint32_t, void **)>(0x20f5e4);
    auto origAdd0 = ctx.Original<uint32_t(__thiscall *)(void *)>(0x20e7b2);
    auto origRel0 = ctx.Original<uint32_t(__thiscall *)(void *)>(0x20f5f9);
    const uint32_t iids[] = {1, 0x3fa40bf, 0xa21ee941, 0, 2, 0x3fa40be, 0xffffffff};
    const uint32_t counts[] = {0, 1, 7, 0xffffffff};
    for (int which = 0; which < 9; ++which) {
        for (uint32_t iid : iids) {
            for (uint32_t count : counts) {
                uint8_t *a = MakeDirector(ctx, 331);
                uint8_t *b = MakeDirector(ctx, 331);
                for (uint8_t *p : {a, b}) std::memcpy(p + 8, &count, 4);
                void *outA = reinterpret_cast<void *>(0xdeadbeef), *outB = reinterpret_cast<void *>(0xdeadbeef);
                uint32_t ra = 0, rb = 0;
                switch (which) {
                case 0: ra = origQi4(a + 4, iid, &outA); rb = BaseAdapters::HooksQueryInterface(Face4(b), nullptr, iid, &outB); break;
                case 1: ra = origQi64(a + 0x64, iid, &outA); rb = reinterpret_cast<AudioMessageTarget *>(b + 0x64)->QueryInterface(iid, &outB); break;
                case 2: ra = origAdd4(a + 4); rb = BaseAdapters::HooksAddRef(Face4(b), nullptr); break;
                case 3: ra = origAdd64(a + 0x64); rb = reinterpret_cast<AudioMessageTarget *>(b + 0x64)->AddRef(); break;
                case 4: ra = origRel4(a + 4); rb = BaseAdapters::HooksRelease(Face4(b), nullptr); break;
                case 5: ra = origRel64(a + 0x64); rb = reinterpret_cast<AudioMessageTarget *>(b + 0x64)->Release(); break;
                case 6: ra = origQi0(a, iid, &outA); rb = AsBase(b)->Base::QueryInterface(iid, &outB); break;
                case 7: ra = origAdd0(a); rb = AsBase(b)->Base::AddRef(); break;
                case 8: ra = origRel0(a); rb = AsBase(b)->Base::Release(); break;
                }
                ++ctx.cases;
                bool qi = which == 0 || which == 1 || which == 6;
                if (qi) {
                    if ((ra & 0xff) != (rb & 0xff)) ctx.Fail("thunk %d iid %08x: original %u, ours %u", which, iid, ra & 0xff, rb & 0xff);
                    if ((ra & 1) && (U(outA) - U(a) != U(outB) - U(b))) ctx.Fail("thunk %d iid %08x: out offset differs", which, iid);
                    if (!(ra & 1) && (outA != reinterpret_cast<void *>(0xdeadbeef) || outB != reinterpret_cast<void *>(0xdeadbeef))) ctx.Fail("thunk %d: out written on failure", which);
                } else if (ra != rb) {
                    ctx.Fail("thunk %d count %u: original %u, ours %u", which, count, ra, rb);
                }
                if (Read<uint32_t>(a, 8) != Read<uint32_t>(b, 8)) ctx.Fail("thunk %d iid %08x count %u: count after %u vs %u", which, iid, count, Read<uint32_t>(a, 8), Read<uint32_t>(b, 8));
                FreeDirector(a);
                FreeDirector(b);
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// OnStart, PreAppInit, PostAppShutdown: the fake world

namespace {

struct AuObject {
    void **vtable;
    int id;
};

AuObject gAuFwArg{}, gAuFwThis{}, gAuMade{}, gAuService{}, gAuMsg{};
void *gAuFwVtable[8], *gAuMadeVtable[8], *gAuServiceVtable[16], *gAuMsgVtable[8], *gAuSubVtable[8], *gAuDirectorVtable[17];
uint8_t *gAuDirector = nullptr; // the director under test (or the block, for the walk)
const uint8_t *gAuStatics[5];
bool gAuCreateOut = false, gAuCreateRet = false, gAuMsgGiven = true;
uint32_t gAuSlot5 = 0;
int gAuThrowHook = 0, gAuThrowSub = -1;
const uint8_t *gAuImage = nullptr;

uint32_t AuId(const void *p)
{
    if (p == &gAuFwArg) return 1;
    if (p == &gAuFwThis) return 2;
    if (p == &gAuMade) return 3;
    if (p == &gAuMsg) return 4;
    return p ? 9 : 0;
}

bool __fastcall AuCreate(void *self, void *, uint32_t clsid, uint32_t iid, void **out)
{
    Log({70, AuId(self), clsid, iid});
    if (gAuCreateOut) *out = &gAuMade;
    return gAuCreateRet;
}
uint32_t __fastcall AuMadeRelease(void *self, void *)
{
    Log({71, AuId(self)});
    return 0;
}
uint32_t __fastcall AuMadeSlot5(void *self, void *)
{
    Log({72, AuId(self)});
    return gAuSlot5;
}
void __fastcall AuAddHook(void *, void *, void *hooks)
{
    Log({73, !hooks ? 0u : hooks == gAuDirector + 4 ? 4u : hooks == gAuDirector ? 1u : 9u});
    if (gAuThrowHook) throw 7;
}
void *__fastcall AuFramework(void *, void *)
{
    Log({75});
    return &gAuFwThis;
}
bool __fastcall AuSubStart(void *self, void *, void *argument)
{
    unsigned which = 9;
    for (unsigned k = 0; k < 5; ++k)
        if (self == gAuStatics[k]) which = k;
    Log({76, which, AuId(argument)});
    if (static_cast<int>(which) == gAuThrowSub) throw 8;
    return true;
}
// The log object's slot 2 (cdecl, this first): the object as the constructor left it.
void __cdecl AuLogLine(cRZStd *self, uint32_t channel, uint32_t level, const char *text)
{
    Log({77, self->mVTable == gAuImage + 0xda3e6c ? 1u : 0u, self->mFirst - U(gAuImage), self->mSecond, self->mFlags, channel, level, U(text) - U(gAuImage)});
}
// The message service: slot 5 hands out the message object.
uint32_t __fastcall AuQuery(void *self, void *, uint32_t first, uint32_t second, void **out)
{
    Log({78, AuId(self), first, second});
    if (gAuMsgGiven) *out = &gAuMsg;
    return 0;
}
uint32_t __fastcall AuMsgRelease(void *self, void *)
{
    Log({79, AuId(self)});
    return 0;
}
uint32_t __fastcall AuMsgKey(void *self, void *, uint32_t arg, uint32_t key)
{
    Log({80, AuId(self), arg - U(gAuDirector), key});
    return 0;
}
uint32_t __fastcall AuMsgKey6(void *self, void *, uint32_t arg, uint32_t key)
{
    Log({81, AuId(self), arg - U(gAuDirector), key});
    return 0;
}

void SetupAudioWorld(difftest::Context &ctx)
{
    gAuImage = ctx.image.At<uint8_t>(0);
    for (auto &s : gAuFwVtable) s = reinterpret_cast<void *>(&FakeUnexpected);
    gAuFwVtable[3] = reinterpret_cast<void *>(&AuCreate);
    for (auto &s : gAuMadeVtable) s = reinterpret_cast<void *>(&FakeUnexpected);
    gAuMadeVtable[2] = reinterpret_cast<void *>(&AuMadeRelease);
    gAuMadeVtable[5] = reinterpret_cast<void *>(&AuMadeSlot5);
    for (auto &s : gAuServiceVtable) s = reinterpret_cast<void *>(&FakeUnexpected);
    gAuServiceVtable[5] = reinterpret_cast<void *>(&AuQuery);
    gAuServiceVtable[7] = reinterpret_cast<void *>(&AuAddHook);
    for (auto &s : gAuMsgVtable) s = reinterpret_cast<void *>(&FakeUnexpected);
    gAuMsgVtable[2] = reinterpret_cast<void *>(&AuMsgRelease);
    gAuMsgVtable[5] = reinterpret_cast<void *>(&AuMsgKey);
    gAuMsgVtable[6] = reinterpret_cast<void *>(&AuMsgKey6);
    for (auto &s : gAuSubVtable) s = reinterpret_cast<void *>(&FakeUnexpected);
    gAuSubVtable[4] = reinterpret_cast<void *>(&AuSubStart);
    gAuFwArg.vtable = gAuFwThis.vtable = gAuFwVtable;
    gAuMade.vtable = gAuMadeVtable;
    gAuService.vtable = gAuServiceVtable;
    gAuMsg.vtable = gAuMsgVtable;
    std::memcpy(gAuDirectorVtable, ctx.image.At<void *>(0xda265c), sizeof(gAuDirectorVtable));
    gAuDirectorVtable[12] = reinterpret_cast<void *>(&AuFramework);
}

// Patches slot 2 of the log object's vtable (0xda3e6c) while alive.
struct LogVtablePatch {
    void **slot;
    void *saved;
    explicit LogVtablePatch(difftest::Context &ctx) : slot(ctx.image.At<void *>(0xda3e6c + 8))
    {
        DWORD old;
        VirtualProtect(slot, 4, PAGE_READWRITE, &old);
        saved = *slot;
        *slot = reinterpret_cast<void *>(&AuLogLine);
    }
    ~LogVtablePatch() { *slot = saved; }
};

struct AuStart {
    void *fn;
    uint8_t *director;
    void *argument;
    bool result;
};
void AuRunStart(void *p)
{
    auto *c = static_cast<AuStart *>(p);
    c->result = reinterpret_cast<bool(__thiscall *)(void *, void *)>(c->fn)(c->director, c->argument);
}

void *AuOursOnStartFn() { return MemberAddress(&AudioDirector::OnStart); }

} // namespace

DIFF_TEST(COMDirectors_OnStart_Audio, 0x20f454)
{
    SetupAudioWorld(ctx);
    void **serviceGlobal = ctx.image.At<void *>(0x10c9378);
    void *savedService = *serviceGlobal;
    *serviceGlobal = &gAuService;
    uint32_t *logGlobal = ctx.image.At<uint32_t>(0xff5f10);
    uint32_t savedLogGlobal = *logGlobal;
    *logGlobal = 0x5a5a0001;
    LogVtablePatch patch(ctx);

    // The five directors it starts: built, then given a vtable that records slot 4.
    EpochScope epoch(ctx);
    ResetStatics(ctx);
    const unsigned started[5] = {kDir_Sound, kDir_HitMan, kDir_SndEa, kDir_Freshness, kDir_HitMp3};
    for (unsigned k = 0; k < 5; ++k) {
        AtexitRedirect redirect(ctx);
        ctx.Original<void *(__cdecl *)()>(kTest[started[k]].getter)();
        *reinterpret_cast<void **>(StaticOf(ctx, started[k])) = gAuSubVtable;
        gAuStatics[k] = StaticOf(ctx, started[k]);
    }

    const uint32_t slot5Values[] = {0, 1, 0x100, 0x1ff, 0xffffff00u, 2, 0x80};
    for (int made = 0; made < 2; ++made)
        for (int ret = 0; ret < 2; ++ret)
            for (uint32_t slot5 : slot5Values)
                for (int mode = -1; mode < 6; ++mode) {
                    std::vector<uint32_t> logs[2];
                    unsigned long escaped[2];
                    bool results[2];
                    for (int side = 0; side < 2; ++side) {
                        uint8_t *d = MakeDirector(ctx, 411);
                        *reinterpret_cast<void **>(d) = gAuDirectorVtable;
                        *reinterpret_cast<void **>(d + 0x30) = &gAuFwArg; // differs from what slot 12 returns
                        gAuDirector = d;
                        gAuCreateOut = made != 0;
                        gAuCreateRet = ret != 0;
                        gAuSlot5 = slot5;
                        gAuThrowHook = mode == 0;
                        gAuThrowSub = mode > 0 ? mode - 1 : -1;
                        gLog.clear();
                        AuStart call{side == 0 ? ctx.image.At(0x20f454) : AuOursOnStartFn(), d, &gAuFwArg, false};
                        escaped[side] = AudioEscaped(&AuRunStart, &call);
                        results[side] = call.result;
                        logs[side] = gLog;
                        *reinterpret_cast<void **>(d) = ctx.image.At<void>(0xda265c);
                        FreeDirector(d);
                    }
                    ++ctx.cases;
                    bool same = escaped[0] == escaped[1] && logs[0] == logs[1] && (escaped[0] != 0 || results[0] == results[1]);
                    if (!same)
                        ctx.Fail("audio OnStart (made %d, ret %d, slot5 %x, mode %d): escaped %lx/%lx, result %d/%d, log %zu/%zu words", made, ret, slot5, mode, escaped[0],
                                 escaped[1], results[0], results[1], logs[0].size(), logs[1].size());
                    // Sanity on the original's own behaviour (no exception): the started path runs the five
                    // sub-directors and returns true, the other logs one line and returns false.
                    if (mode < 0) {
                        bool startedPath = made && (slot5 & 0xff);
                        unsigned subStarts = 0, lines = 0;
                        for (size_t i = 0; i < logs[1].size(); ++i) {
                            subStarts += logs[1][i] == 76;
                            lines += logs[1][i] == 77;
                        }
                        if (startedPath != results[1] || subStarts != (startedPath ? 5u : 0u) || lines != (startedPath ? 0u : 1u))
                            ctx.Fail("audio OnStart (made %d, slot5 %x): result %d, %u sub-director starts, %u log lines", made, slot5, results[1], subStarts, lines);
                    }
                }
    *logGlobal = savedLogGlobal;
    *serviceGlobal = savedService;
    ResetStatics(ctx);
}

namespace {

// A block with the id set filled, for the walks.
uint8_t *MakeWalkBlock(difftest::Context &ctx, uint32_t seed, const std::vector<uint32_t> &ids)
{
    uint8_t *p = NewAudioBlock(seed);
    new (p + 0xa0) std::set<uint32_t>();
    for (uint32_t id : ids) reinterpret_cast<std::set<uint32_t> *>(p + 0xa0)->insert(id);
    (void)ctx;
    return p;
}

void FreeWalkBlock(uint8_t *p)
{
    reinterpret_cast<std::set<uint32_t> *>(p + 0xa0)->~set();
    ::operator delete(p);
}

void TestWalk(difftest::Context &ctx, uint32_t rva, bool preAppInit)
{
    SetupAudioWorld(ctx);
    void **serviceGlobal = ctx.image.At<void *>(0x10c9378);
    void *savedService = *serviceGlobal;
    uint32_t *logGlobal = ctx.image.At<uint32_t>(0xff5f10);
    LogVtablePatch patch(ctx);
    auto orig = ctx.Original<bool(__thiscall *)(void *)>(rva);
    const std::vector<std::vector<uint32_t>> idSets = {{}, {0xca}, {0xca, 0xcb, 0xeded3807}, {5, 3, 8, 1, 9, 2, 7, 100, 50, 0xffffffff}};
    // Service modes: a message object; a service that gives none; no service at all. The first walk of
    // PreAppInit with no object and ids calls through a null pointer in the original (ORIGINAL-BUG in
    // KeyMapSlot5Walker): only the empty set is run in those modes.
    for (int mode = 0; mode < 3; ++mode)
        for (const auto &ids : idSets) {
            if (preAppInit && mode > 0 && !ids.empty()) continue;
            uint8_t *a = MakeWalkBlock(ctx, 701, ids);
            uint8_t *b = MakeWalkBlock(ctx, 701, ids);
            *serviceGlobal = mode == 2 ? nullptr : &gAuService;
            gAuMsgGiven = mode == 0;
            gAuDirector = a;
            gLog.clear();
            bool ra = orig(a + 4);
            std::vector<uint32_t> logA = gLog;
            gAuDirector = b;
            gLog.clear();
            bool rb = preAppInit ? AsAudioHooks(b + 4)->PreAppInit() : AsAudioHooks(b + 4)->PostAppShutdown();
            std::vector<uint32_t> logB = gLog;
            ++ctx.cases;
            if (ra != rb || !rb) ctx.Fail("audio %s (mode %d, %zu ids): result %d / %d", preAppInit ? "PreAppInit" : "PostAppShutdown", mode, ids.size(), ra, rb);
            if (logA != logB) ctx.Fail("audio %s (mode %d, %zu ids): logs differ (%zu vs %zu words)", preAppInit ? "PreAppInit" : "PostAppShutdown", mode, ids.size(), logA.size(), logB.size());
            if (mode == 0 && logB.size() < 4 + ids.size() * 4) ctx.Fail("audio walk (%zu ids): only %zu words logged", ids.size(), logB.size());
            if (std::memcmp(a + 0x64, b + 0x64, 0x3c) != 0) ctx.Fail("audio walk: the object changed differently");
            FreeWalkBlock(a);
            FreeWalkBlock(b);
        }
    *serviceGlobal = savedService;
    (void)logGlobal;
}

} // namespace

DIFF_TEST(COMDirectors_PreAppInit_Audio, 0x20f571)
{
    TestWalk(ctx, 0x20f571, true);
}

DIFF_TEST(COMDirectors_PostAppShutdown_Audio, 0x20f555)
{
    TestWalk(ctx, 0x20f555, false);
}

// =============================================================================================
// The application's director registration: 0x140f40 (the audio director behind the `nosound`
// switch) and cTSAppFW::AddCOMDirectorsHere (0xc26bf5).
//
// The framework service object (global 0x10c9378) is a fake: slot 21 hands out a real cRZCmdLine
// (built from a command line text, so IsSwitchPresent is the real one), slot 23 gives the root
// director its framework object (a second fake, whose slot 4 InitializeCOM calls). The directors the
// functions add (the application director, the audio director) are built beforehand by the original's
// getters and given a vtable whose slots 3 (InitializeCOM) and 4 (OnStart) record: the recursion into
// their sub-directors and the root director's list are the real ones. The root director is rebuilt
// for each side. Compared: the log, the root director, every static, the exit handlers registered.

namespace {

struct ApObject {
    void **vtable;
    int id;
};

ApObject gApFramework{}, gApService{}, gApLanguage{};
void *gApFrameworkVtable[8], *gApServiceVtable[32], *gApLanguageVtable[24], *gApDirectorVtable[17];
void *gApCmdLine = nullptr;
const void *gApApp = nullptr, *gApAudio = nullptr, *gApRoot = nullptr;
std::function<void()> gApBody;

uint32_t ApId(const void *p)
{
    if (p == &gApFramework) return 1;
    if (p == &gApService) return 2;
    if (p == gApRoot) return 3;
    if (p == gApApp) return 4;
    if (p == gApAudio) return 5;
    return p ? 9 : 0;
}

void *__fastcall ApCmdLine(void *self, void *)
{
    Log({110, ApId(self)});
    return gApCmdLine;
}
void *__fastcall ApFrameworkForRoot(void *self, void *)
{
    Log({111, ApId(self)});
    return &gApFramework;
}
uint32_t __fastcall ApFrameworkSlot4(void *self, void *)
{
    Log({112, ApId(self)});
    return 0x77;
}
bool __fastcall ApInit(void *self, void *, void *framework, void *path)
{
    Log({113, ApId(self), ApId(framework), U(path) - U(gApRoot)});
    return true;
}
bool __fastcall ApStart(void *self, void *, void *argument)
{
    Log({114, ApId(self), ApId(argument)});
    return true;
}
uint32_t __fastcall ApLangSet(void *self, void *, int32_t id)
{
    Log({115, ApId(self), static_cast<uint32_t>(id)});
    return 0;
}
uint32_t __fastcall ApLangLookup(void *self, void *, const cRZString *name, int32_t *id)
{
    uint32_t hash = 0;
    for (char c : name->mString) hash = hash * 31 + static_cast<uint8_t>(c);
    Log({116, hash});
    *id = 4;
    return 1;
}

void SetupAppWorld(difftest::Context &ctx)
{
    for (auto &s : gApFrameworkVtable) s = reinterpret_cast<void *>(&FakeUnexpected);
    gApFrameworkVtable[4] = reinterpret_cast<void *>(&ApFrameworkSlot4);
    for (auto &s : gApServiceVtable) s = reinterpret_cast<void *>(&FakeUnexpected);
    gApServiceVtable[21] = reinterpret_cast<void *>(&ApCmdLine);
    gApServiceVtable[23] = reinterpret_cast<void *>(&ApFrameworkForRoot);
    for (auto &s : gApLanguageVtable) s = reinterpret_cast<void *>(&FakeUnexpected);
    gApLanguageVtable[8] = reinterpret_cast<void *>(&ApLangSet);
    gApLanguageVtable[18] = reinterpret_cast<void *>(&ApLangLookup);
    gApFramework.vtable = gApFrameworkVtable;
    gApService.vtable = gApServiceVtable;
    gApLanguage.vtable = gApLanguageVtable;
    std::memcpy(gApDirectorVtable, ctx.image.At<void *>(0xda265c), sizeof(gApDirectorVtable));
    gApDirectorVtable[3] = reinterpret_cast<void *>(&ApInit);
    gApDirectorVtable[4] = reinterpret_cast<void *>(&ApStart);
    gApRoot = ctx.image.At<uint8_t>(0x10c9390);
    gApAudio = ctx.image.At<uint8_t>(kAudioStatic);
    gApApp = StaticOf(ctx, kDir_TSAppFW);
}

void ApCall(void *) { gApBody(); }

struct AppResult {
    std::vector<uint32_t> log;
    unsigned long escaped = 0;
    bool rootBuilt = false;
    Canon root;
    AudioStaticsState statics;
    size_t handlers = 0;
};

// Rebuilds the world, runs `body` (the original or ours) and captures what is compared.
AppResult RunApp(difftest::Context &ctx, EpochScope &epoch, bool withApp, bool original, const std::function<void()> &body)
{
    ResetAudioStatics(ctx);
    std::memset(ctx.image.At<uint8_t>(0x10c9390), 0xCD, kSize);
    *ctx.image.At<int>(0x10c93f4) = 0;
    EnsureMadCow(ctx);
    epoch.Reset();
    {
        AtexitRedirect redirect(ctx);
        if (withApp) {
            ctx.Original<void *(__cdecl *)()>(0xc2be73)();
            *reinterpret_cast<void **>(const_cast<void *>(gApApp)) = gApDirectorVtable;
        }
        ctx.Original<void *(__cdecl *)()>(0x20efaf)();
        *reinterpret_cast<void **>(const_cast<void *>(gApAudio)) = gApDirectorVtable;
    }
    epoch.Reset();
    AppResult r;
    gLog.clear();
    gApBody = body;
    if (original) {
        AtexitRedirect redirect(ctx);
        r.escaped = AudioEscaped(&ApCall, nullptr);
        r.handlers = gImageHandlers.size();
    } else {
        OursRedirect redirect;
        r.escaped = AudioEscaped(&ApCall, nullptr);
        r.handlers = gOursHandlers.size();
    }
    r.log = gLog;
    // The root director exists only when something asked for it (an unbuilt one is the 0xCD fill).
    r.rootBuilt = Read<uint32_t>(ctx.image.At<uint8_t>(0x10c9390), 0) != 0xCDCDCDCD;
    if (r.rootBuilt) r.root = Take(ctx.image.At<uint8_t>(0x10c9390));
    r.statics = SnapshotAudioStatics(ctx);
    return r;
}

std::string CompareApp(const AppResult &a, const AppResult &b, bool withApp)
{
    char buf[200];
    if (a.escaped != b.escaped) {
        std::snprintf(buf, sizeof(buf), "escaped %lx / %lx", a.escaped, b.escaped);
        return buf;
    }
    if (a.log != b.log) {
        std::snprintf(buf, sizeof(buf), "logs differ (%zu vs %zu words)", a.log.size(), b.log.size());
        return buf;
    }
    if (a.handlers != b.handlers) {
        std::snprintf(buf, sizeof(buf), "exit handlers registered %zu / %zu", a.handlers, b.handlers);
        return buf;
    }
    if (a.rootBuilt != b.rootBuilt) return "the root director is built on one side only";
    std::string d = Diff(a.root, b.root);
    if (!d.empty()) return "root director: " + d;
    for (unsigned j = 0; j < kTestCount; ++j) {
        if (a.statics.dirs.built[j] != b.statics.dirs.built[j]) return std::string("built state of ") + kTest[j].name;
        if (a.statics.dirs.built[j]) {
            d = Diff(a.statics.dirs.canon[j], b.statics.dirs.canon[j]);
            if (!d.empty()) return std::string(kTest[j].name) + ": " + d;
        }
    }
    d = AudioDiff(a.statics.audio, b.statics.audio);
    if (!d.empty()) return "audio static: " + d;
    (void)withApp;
    return std::string();
}

cRZCmdLine *NewCmdLine(const char *line)
{
    auto *cmd = static_cast<cRZCmdLine *>(::operator new(sizeof(cRZCmdLine)));
    std::memset(cmd, 0xCD, sizeof(cRZCmdLine));
    cmd->Construct(line);
    return cmd;
}

} // namespace

// 0x140f40: with and without the switch, in several spellings.
DIFF_TEST(AppCOMDirectors_AddAudioDirector, 0x140f40)
{
    SetupAppWorld(ctx);
    void **serviceGlobal = ctx.image.At<void *>(0x10c9378);
    void *savedService = *serviceGlobal;
    *serviceGlobal = &gApService;
    EpochScope epoch(ctx);
    const char *lines[] = {"game.exe", "game.exe -nosound", "game.exe /nosound", "game.exe -w -nosound -x", "game.exe -NoSound", "game.exe -nosoundx", "game.exe -nosoun",
                           "game.exe -nosound:1", "game.exe nosound", "game.exe -w", ""};
    auto orig = ctx.Original<void(__cdecl *)()>(0x140f40);
    for (const char *line : lines) {
        cRZCmdLine *cmd = NewCmdLine(line);
        gApCmdLine = cmd;
        AppResult a = RunApp(ctx, epoch, false, true, [&] { orig(); });
        AppResult b = RunApp(ctx, epoch, false, false, [&] { appcom::AddAudioDirector(); });
        ++ctx.cases;
        std::string d = CompareApp(a, b, false);
        if (!d.empty()) ctx.Fail("AddAudioDirector with <%s>: %s", line, d.c_str());
        // Sanity on the original's own behaviour: a plain command line adds the audio director, the
        // switch does not.
        bool added = std::find(b.root.subs.begin(), b.root.subs.end(), const_cast<void *>(gApAudio)) != b.root.subs.end();
        if ((std::strcmp(line, "game.exe") == 0 || std::strcmp(line, "game.exe -w") == 0) && !added) ctx.Fail("AddAudioDirector with <%s>: did not add the audio director", line);
        if ((std::strcmp(line, "game.exe -nosound") == 0 || std::strcmp(line, "game.exe /nosound") == 0) && added) ctx.Fail("AddAudioDirector with <%s>: added the audio director", line);
        cmd->Destruct();
        ::operator delete(cmd);
    }
    *serviceGlobal = savedService;
    ResetAudioStatics(ctx);
    std::memset(ctx.image.At<uint8_t>(0x10c9390), 0xCD, kSize);
    *ctx.image.At<int>(0x10c93f4) = 0;
}

// 0xc26bf5: the whole sequence, with the language switch known to a fake language service.
DIFF_TEST(AppCOMDirectors_AddCOMDirectorsHere, 0xc26bf5)
{
    SetupAppWorld(ctx);
    void **serviceGlobal = ctx.image.At<void *>(0x10c9378);
    void *savedService = *serviceGlobal;
    *serviceGlobal = &gApService;
    void **languageCache = ctx.image.At<void *>(0x10f51c8);
    void *savedLanguage = *languageCache;
    *languageCache = &gApLanguage;
    EpochScope epoch(ctx);
    const char *lines[] = {"game.exe -l:german", "game.exe -l:german -nosound", "game.exe -w -l:french"};
    static uint8_t face[0x40];
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0xc26bf5);
    for (const char *line : lines) {
        cRZCmdLine *cmd = NewCmdLine(line);
        gApCmdLine = cmd;
        AppResult a = RunApp(ctx, epoch, true, true, [&] { orig(face + 0x1c); });
        AppResult b = RunApp(ctx, epoch, true, false, [&] { ts::cTSAppFWAdapters::AddCOMDirectorsHere(reinterpret_cast<cIGZApp *>(face + 0x1c), nullptr); });
        ++ctx.cases;
        std::string d = CompareApp(a, b, true);
        if (!d.empty()) ctx.Fail("AddCOMDirectorsHere with <%s>: %s", line, d.c_str());
        // The application director comes first in the root's list; its OnStart and the language call are in the log.
        bool hasStart = false, hasLanguage = false;
        for (size_t i = 0; i + 2 < b.log.size(); ++i) {
            hasStart |= b.log[i] == 114 && b.log[i + 1] == 4;
            hasLanguage |= b.log[i] == 115 && b.log[i + 2] == 4;
        }
        if (!hasStart || !hasLanguage) ctx.Fail("AddCOMDirectorsHere with <%s>: log lacks the application OnStart (%d) or the language call (%d)", line, hasStart, hasLanguage);
        cmd->Destruct();
        ::operator delete(cmd);
    }
    *languageCache = savedLanguage;
    *serviceGlobal = savedService;
    ResetAudioStatics(ctx);
    ResetStatics(ctx);
    std::memset(ctx.image.At<uint8_t>(0x10c9390), 0xCD, kSize);
    *ctx.image.At<int>(0x10c93f4) = 0;
}
