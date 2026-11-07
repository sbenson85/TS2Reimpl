// Differential tests of nGZSceneGraph::cMaterialManager (lanes RD and RE, milestone O row M1a; src/sg/cMaterialManager.h).
//
//   SG_MaterialManager_Life  the object's life on each side in a block of the test's: the original's constructor 0xac9810
//       (the device buffers made through the COM getter, the graphics system's device event connected), state planted
//       through the typed view (the layout is the game's on both sides: entries with names, materials, parsed objects,
//       variants, keys and blobs; the index table with holes and its free list; entries waiting to go; variant jobs;
//       the two listener sets), then random calls through the interface: Start 0xacbae0, Shutdown 0xacd8b0, OnTick
//       0xacda40, FlushPending 0xacb630 (both with DestroyEntry 0xacb560 and ResetEntry 0xacb170), AddRefIndex 0xacb400,
//       ReleaseIndex 0xaccb20, Reset 0xacb260 (by name through NamedCell 0xae87d0, or all), MarkAllStale 0xacac60, IsLoaded
//       0xacba60, GetInfo 0xacc410, OnEvent 0xacba20 (the device event: OnDeviceEvent 0xacb780 and CreateDeviceObjects
//       0xacbe90; the connection-changed event; others), QueryInterface 0xacc510, AddRef / Release, and (lane RE, part 2)
//       FindOrLoad 0xacb7b0, AddMaterial (BuildEntry 0xacbaf0), ReloadAll 0xaccfb0 (ReloadEntry 0xacce00, the variant
//       builder 0xacadd0), Register 0xacc840, Unregister 0xacdbc0, LoadStale 0xacb470, slot 30 0xaccce0, IsAvailable
//       0xacc0d0 (LoadMaterial 0xacc640), GetInfoByName 0xacc320. The whole state is
//       noted after every call; at the end the destructor 0xac9ee0 (or, one seed in four, a block on the heap and the last
//       Release, reaching the deleting destructor 0xaca420), the fakes' counts, the pool's handle counts and the heap
//       (allocated minus freed, blocks and bytes).
//       Part 3 (lane RF): GetVariant (slot 22: a variant for a key, and the jobs for the keys it implies, with queue values
//       0 / 1 / 0x100 / others and the related-format arguments around the masks' limits), and the three ranges (slots 34-36)
//       with the game's iterator objects in test pools: each iterator's words, a clone walked through the iterators' own
//       Clone / IsEqual / Get / Next, and the releases.
//   SG_MaterialManager_Registry  the behaviour registry 0xacc550 (cMaterialManagerBehaviorTraits::RegistryFor), as
//       tests/gz_draw2d_registry.cpp does cDraw2D's: the original side starts from the state its first-use build leaves
//       (built by the test, the device event record ending in 4 as the code stores it), ours builds its own.
//   SG_MixHash3   0x98bf70 against ours on random and edge triples.
//   SG_MaterialManager_Get  the getter 0xacc040, one-sided on ours (the original's function-local static cannot be built
//       in the harness): the same object twice, built once (the device event connected once), not started.
//
// Stood in on both sides: the device getter 0x18ae40 (GetCurrentDeviceCached: a device fake or none), the COM getter
// 0x1ae18d (a COM fake handing out the 34 array fakes), the stopwatch's Start 0x1c982b and Elapsed 0x1c9256 (a scripted
// clock), the formats manager 0x18dcf0, and the material parser 0xae8980. The texture manager is a sink in 0xae8750's cache 0x10f8f94 (slot 21, which Reset
// calls); 0xae8750 and the name resolver 0xae7340 run for real on both sides (the image's and ours, sg/cTextureManager.h). The variant builder 0xacadd0 is NOT stood in: the original side runs the image's own
// (a __thiscall on the manager; lane RD's __stdcall stand-in hid that wrong call in OnTick). The framework (cell 0x10c9378) answers the graphics system fake; the string pool is sgfx's; the
// behaviour's event manager (cell 0x10f6998) is our connection manager.
//
// What the original leaves to the heap's layout, kept out: the waiting set is a std::set<Entry *> (ordered by address),
// so the entries it destroys come in address order. Waiting entries are given distinct countdowns (a release that would
// queue one beside another at 10 releases another index instead), so a tick destroys at most one; a FlushPending with
// more than one waiting has its log sorted and the free indices it added sorted, on both sides. The set is noted as its
// entries' indices, sorted. The jobs of the entries it destroys are erased from a deque, each erase copying the jobs
// behind it: with several waiting, only one keeps jobs (KeepJobsOfOneWaitingEntry), or the copies would follow the order.
#include "sgfx.h"

#include "code_patch.h"
#include "free_recorder.h"
#include "gz_registry_fx.h"

#include "gz/GraphicServices.h"
#include "gz/cGZEventReflector.h"
#include "misc/UtilitiesE.h"
#include "rz/StaticGetters.h"
#include "rz/cRZStopwatch.h"
#include "sg/cMaterialManager.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <malloc.h>
#include <new>
#include <random>
#include <string>
#include <tuple>
#include <typeinfo>
#include <vector>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;
using drawfx::Ret;
using drawfx::Slot;
using MM = cMaterialManager;

// ---- slot tables: unsettled but for the slots named ----
struct At {
    unsigned slot;
    Slot s;
};
template <size_t N, size_t K> constexpr std::array<Slot, N> Table(const At (&at)[K])
{
    std::array<Slot, N> t{};
    for (Slot &s : t) s = {0, Ret::Int, false, "", ""};
    t[0] = {2, Ret::Int, true, "QueryInterface", "ua"};
    t[1] = {0, Ret::Int, true, "AddRef", ""};
    t[2] = {0, Ret::Int, true, "Release", ""};
    for (const At &a : at) t[a.slot] = a.s;
    return t;
}

constexpr At kGraphicsAt[] = {{8, {3, Ret::Int, true, "Connect", "uau"}}, {9, {3, Ret::Int, true, "Disconnect", "uau"}}};
constexpr auto kGraphicsSlots = Table<10>(kGraphicsAt);
constexpr drawfx::Iface kGraphics{"GraphicSystem", 10, kGraphicsSlots.data()};

constexpr At kDeviceAt[] = {{35, {0, Ret::Int, true, "IsDeviceReady", ""}}};
constexpr auto kDeviceSlots = Table<36>(kDeviceAt);
constexpr drawfx::Iface kDevice{"Device", 36, kDeviceSlots.data()};

constexpr At kComAt[] = {{3, {3, Ret::Int, true, "GetClassObject", "uua"}}};
constexpr auto kComSlots = Table<4>(kComAt);
constexpr drawfx::Iface kCom{"COM", 4, kComSlots.data()};

constexpr At kArrayAt[] = {{3, {0, Ret::Int, true, "ReleaseResource", ""}},
                           {15, {3, Ret::Int, true, "Lock", "uuu"}},
                           {16, {0, Ret::Int, true, "Unlock", ""}},
                           {18, {0, Ret::Int, true, "GetSize", ""}},
                           {20, {3, Ret::Int, true, "Create", "auu"}}};
constexpr auto kArraySlots = Table<21>(kArrayAt);
constexpr drawfx::Iface kArray{"StaticVertexArray", 21, kArraySlots.data()};

// A listener (sets A and B) and a made variant alike: a variant's slot 10 asks whether it already serves a key.
constexpr At kListenerAt[] = {{3, {0, Ret::Int, true, "Slot3", ""}},
                              {4, {1, Ret::Int, true, "Slot4", "u"}},
                              {6, {0, Ret::Int, true, "Hash", ""}},
                              {7, {1, Ret::Int, true, "Equal", "a"}},
                              {10, {4, Ret::Int, true, "Serves", "W3W3W34a"}}};
constexpr auto kListenerSlots = Table<11>(kListenerAt);
constexpr drawfx::Iface kListener{"MaterialListener", 11, kListenerSlots.data()};

constexpr At kMaterialAt[] = {{10, {1, Ret::Int, true, "SetName", "s"}},
                              {18, {1, Ret::Int, true, "Same", "a"}},
                              {19, {0, Ret::Int, true, "Name", ""}},
                              {20, {0, Ret::Int, true, "Text", ""}}};
constexpr auto kMaterialSlots = Table<21>(kMaterialAt);
constexpr drawfx::Iface kMaterial{"Material", 21, kMaterialSlots.data()};

// The material parser (the image's cached service 0xae8980).
constexpr At kParserAt[] = {{4, {2, Ret::Int, true, "Parse", "aa"}},
                            {5, {0, Ret::Int, true, "Done", ""}},
                            {6, {4, Ret::Int, true, "Make", "aaW34W3"}},
                            {7, {5, Ret::Int, true, "Refine", "aaW34W3u"}}};
constexpr auto kParserSlots = Table<8>(kParserAt);
constexpr drawfx::Iface kParser{"MaterialParser", 8, kParserSlots.data()};

// The framework's resource service (key lookups) and texture service.
constexpr At kResourceAt[] = {{3, {5, Ret::Int, true, "Load", "W4uauu"}}, {15, {1, Ret::Int, true, "Exists", "W4"}}};
constexpr auto kResourceSlots = Table<16>(kResourceAt);
constexpr drawfx::Iface kResource{"ResourceService", 16, kResourceSlots.data()};

constexpr At kTextureAt[] = {{10, {1, Ret::Int, true, "Forget", "S"}}, {11, {1, Ret::Int, true, "Note", "S"}}};
constexpr auto kTextureSlots = Table<12>(kTextureAt);
constexpr drawfx::Iface kTexture{"TextureService", 12, kTextureSlots.data()};

// The vertex formats manager (0x18dcf0): a format by code.
constexpr At kFormatsMgrAt[] = {{8, {1, Ret::Int, true, "Format", "u"}}};
constexpr auto kFormatsMgrSlots = Table<9>(kFormatsMgrAt);
constexpr drawfx::Iface kFormatsMgr{"VertexFormatsManager", 9, kFormatsMgrSlots.data()};

constexpr At kSinkAt[] = {{21, {0, Ret::Int, true, "Slot21", ""}}};
constexpr auto kSinkSlots = Table<22>(kSinkAt);
constexpr drawfx::Iface kSink{"MaterialSink", 22, kSinkSlots.data()};

// ---- the world ----
constexpr unsigned kArrays = 34, kMaterials = 10, kExtras = 4, kMade = 40, kParsed = 4, kListeners = 6, kVariants = 8,
                   kFormats = 3, kMats = kMaterials + kExtras + kMade;
constexpr uint32_t kArrayId = 600, kMaterialId = 700, kExtraId = 710, kParsedId = 720, kListenerId = 740,
                   kVariantId = 760, kFormatId = 780, kMadeId = 800;
constexpr uint32_t kFrameworkCell = 0x10c9378;
constexpr uint32_t kBlockTag = 0x7a000000;
constexpr uint32_t kConnectionChanged = 0x5b51e806;
constexpr uint32_t kBufferBytes = 16;
constexpr uint32_t kResourceService = 0x056b906e, kTextureService = 0x8d127822;
constexpr uint32_t kMaterialClass = 0x49596978, kResetQuery = 0xcd2e1781, kVariantQuery = 0xfc17e651;

const char *const kNames[] = {"mat0", "mat1", "mat2", "mat3", "mat4", "mat5", "mat6", "mat7", "mat8", "mat9"};
// What the materials' slot 20 answers: the name in the other case now and then (the lookups ignore case).
const char *const kTexts[] = {"MAT0", "mat1", "MAT2", "mat3", "MAT4", "mat5", "MAT6", "mat7", "MAT8", "mat9"};
const char *const kExtraNames[] = {"extra0", "extra1", "extra2", "extra3"};
const char *const kExtraTexts[] = {"EXTRA0", "extra1", "extra2", "EXTRA3"};
const char *const kOtherNames[] = {"other0", "other1"};
// Names for the lookups that load: the "#" ones strip to a name after the bang (0xae7340).
const char *const kHashNames[] = {"#1!mat3", "#2!other1", "#7!zzz", "#5"};
// Variant keys: a few, so jobs find some made and some not; the third word's bits 4-10 pick the format.
constexpr MM::VariantKey kKeys[] = {{{1, 2, 3}},     {{1, 2, 4}},   {{0x10, 0, 0}}, {{0, 0, 0}},
                                    {{7, 7, 7}},     {{0xffffffff, 1, 2}}, {{5, 6, 0x10}}, {{5, 6, 0x25}}};
constexpr unsigned kKeyCount = sizeof kKeys / sizeof kKeys[0];

struct World {
    Fake framework, graphics, device, com, sink, connectionListener, resource, texture, parser, formatsManager;
    Fake arrays[kArrays];
    alignas(4) uint8_t buffers[kArrays][kBufferBytes];
    Fake materials[kMaterials], extras[kExtras], made[kMade], parsed[kParsed], listeners[kListeners], variants[kVariants],
        formats[kFormats];
    char madeName[kMade][40];
    bool madeNamed[kMade];
    uint32_t words[kMats][3];
    MM::Blob52 blobs[kMats][2];
    unsigned nextArray = 0, nextMade = 0;
    const char *want = nullptr; // the name a material the resource service loads next is given (null: its own)
    uint32_t lcg = 1;
    bool deviceUp = false;
    bool arraysLive = false; // the manager holds its arrays (made with a device, not shut down)
    uint32_t clock = 0, step = 0;
    bool heapBlock = false, listen = false;
    sgfx::Manager events;
    uint32_t Next()
    {
        lcg = lcg * 1664525u + 1013904223u;
        return lcg >> 8;
    }
};
World gw;
alignas(16) uint8_t gBlock[MM::kSize];

// A material's number: the planted ones, the extras the test adds, the ones the services make.
unsigned MatIndex(const Fake &f)
{
    if (f.id < kExtraId) return f.id - kMaterialId;
    if (f.id < kMadeId) return kMaterials + (f.id - kExtraId);
    return kMaterials + kExtras + (f.id - kMadeId);
}

uint32_t GetService(Fake &, const uint32_t *a)
{
    Fake *service = nullptr;
    if (a[0] == MM::kGraphicSystemId && a[1] == MM::kGraphicSystemIid) service = &gw.graphics;
    else if (a[0] == kResourceService) service = &gw.resource;
    else if (a[0] == kTextureService) service = &gw.texture;
    if (!service) return 0;
    ++service->refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = service;
    return 1;
}
// A material the services hand out, counted for the caller, called by the wanted name now and then.
Fake *NewMade()
{
    const unsigned n = gw.nextMade++ % kMade;
    gw.madeNamed[n] = false;
    if (gw.want && gw.Next() % 3 != 0) {
        std::snprintf(gw.madeName[n], sizeof gw.madeName[n], "%s", gw.want);
        gw.madeNamed[n] = true;
    }
    ++gw.made[n].refs;
    return &gw.made[n];
}
uint32_t GetClassObject(Fake &, const uint32_t *a)
{
    Fake *f;
    if (a[0] == kMaterialClass) {
        f = NewMade();
    } else {
        f = gw.nextArray < kArrays ? &gw.arrays[gw.nextArray++] : nullptr;
        if (f) ++f->refs;
    }
    *reinterpret_cast<void **>(uintptr_t(a[2])) = f;
    return f != nullptr;
}
uint32_t ArrayLock(Fake &self, const uint32_t *) { return P(gw.buffers[self.id - kArrayId]); }
uint32_t ArraySize(Fake &self, const uint32_t *) { return self.value[0]; }
uint32_t ListenerHash(Fake &self, const uint32_t *) { return self.value[0]; }
// Equal when the hashes agree (distinct objects can compare equal, as the set keys them by that comparison).
uint32_t ListenerEqual(Fake &self, const uint32_t *a) { return self.value[0] == reinterpret_cast<const Fake *>(a[0])->value[0]; }
// A variant (or a listener) says whether it serves the key: one time in three.
uint32_t VariantServes(Fake &, const uint32_t *) { return gw.Next() % 3 == 0; }
const char *MaterialNameOf(const Fake &f)
{
    const unsigned n = MatIndex(f);
    if (n < kMaterials) return kNames[n];
    if (n < kMaterials + kExtras) return kExtraNames[n - kMaterials];
    const unsigned m = n - kMaterials - kExtras;
    return gw.madeNamed[m] ? gw.madeName[m] : kNames[(m * 3) % kMaterials];
}
uint32_t MaterialName(Fake &self, const uint32_t *) { return P(MaterialNameOf(self)); }
uint32_t MaterialText(Fake &self, const uint32_t *)
{
    const unsigned n = MatIndex(self);
    if (n < kMaterials) return P(kTexts[n]);
    if (n < kMaterials + kExtras) return P(kExtraTexts[n - kMaterials]);
    return P(MaterialNameOf(self));
}
uint32_t MaterialSetName(Fake &self, const uint32_t *a)
{
    const unsigned n = MatIndex(self);
    if (n >= kMaterials + kExtras) {
        const unsigned m = n - kMaterials - kExtras;
        std::snprintf(gw.madeName[m], sizeof gw.madeName[m], "%s", reinterpret_cast<const char *>(uintptr_t(a[0])));
        gw.madeNamed[m] = true;
    }
    return 0;
}
// Whether the material is the one the manager's entry already holds: by the pair's numbers.
uint32_t MaterialSame(Fake &self, const uint32_t *a)
{
    const Fake *other = reinterpret_cast<const Fake *>(uintptr_t(a[0]));
    return other && (self.id + other->id) % 3 != 0;
}
// The parser reads a material: its parsed object, some words, some blobs, a flag byte.
struct ParsedOut {
    cIGZUnknown *parsed;
    uint32_t wordCount;
    const uint32_t *words;
    uint32_t blobCount;
    const MM::Blob52 *blobs;
    uint32_t flag;
};
uint32_t ParserParse(Fake &, const uint32_t *a)
{
    ParsedOut *out = reinterpret_cast<ParsedOut *>(uintptr_t(a[0]));
    const Fake &material = *reinterpret_cast<const Fake *>(uintptr_t(a[1]));
    const unsigned n = MatIndex(material);
    out->parsed = n % 5 == 4 ? nullptr : reinterpret_cast<cIGZUnknown *>(&gw.parsed[n % kParsed]);
    out->wordCount = n % 4;
    out->words = gw.words[n];
    out->blobCount = n % 3;
    out->blobs = gw.blobs[n];
    out->flag = n * 5 + 1;
    return 0;
}
// Making a variant: the parser hands one out (counted) or none; `more` asks for a second pass now and then.
uint32_t ParserMake(Fake &, const uint32_t *a)
{
    const uint32_t n = gw.Next();
    Fake *variant = n % 6 == 0 ? nullptr : &gw.variants[n % kVariants];
    if (variant) ++variant->refs;
    *reinterpret_cast<Fake **>(uintptr_t(a[0])) = variant;
    return (n >> 8) % 3 == 0 ? (n >> 12) % 5 + 1 : 0;
}
uint32_t ParserRefine(Fake &, const uint32_t *a)
{
    const uint32_t n = gw.Next();
    Fake *variant = n % 5 == 0 ? nullptr : &gw.variants[(n >> 4) % kVariants];
    if (variant) ++variant->refs;
    *reinterpret_cast<Fake **>(uintptr_t(a[0])) = variant;
    return 0;
}
uint32_t ResourceExists(Fake &, const uint32_t *) { return gw.Next() % 3 != 0; }
uint32_t ResourceLoad(Fake &, const uint32_t *a)
{
    if (gw.Next() % 4 == 0) return 0;
    *reinterpret_cast<Fake **>(uintptr_t(a[2])) = NewMade();
    return 1;
}
uint32_t FormatByCode(Fake &, const uint32_t *a) { return P(&gw.formats[a[0] % kFormats]); }

void *__cdecl StandDevice() { return gw.deviceUp ? &gw.device : nullptr; }
void *StandCom() { return &gw.com; }
void *__cdecl StandParser() { return &gw.parser; }
void *__cdecl StandFormatsManager() { return &gw.formatsManager; }
void __fastcall StandStart(cRZStopwatch *, void *) { drawfx::Note("stopwatch start", 0u); }
uint32_t __fastcall StandElapsed(const cRZStopwatch *, void *)
{
    const uint32_t now = gw.clock;
    drawfx::Note("elapsed", now);
    gw.clock += gw.step;
    return now;
}
// The variant builder 0xacadd0 is NOT stood in (part 1's test did, as a __stdcall: the call that went wrong in game was
// OnTick's, which the image's body then ran with a garbage `this`). The original side runs the image's builder through
// the image's own slots, on an object the image built; ours runs ours. The stands are below it: the parser service
// 0xae8980 and the formats manager 0x18dcf0.
struct Stands {
    shape::StandIn device{0x18ae40, &nGZGraphic4::GetCurrentDeviceCached, &StandDevice};
    shape::StandIn com{0x1ae18d, &GetDirectorFrameworkObject, &StandCom};
    shape::StandIn start{0x1c982b, &cRZStopwatch::Start, &StandStart};
    shape::StandIn elapsed{0x1c9256, &cRZStopwatch::Elapsed, &StandElapsed};
    shape::StandIn formats{0x18dcf0, &nGZGraphic4::GetVertexFormatsManagerCached, &StandFormatsManager};
    shape::StandIn parser{0xae8980, &GetMaterialParserCached, &StandParser};
};

cIGZUnknown *U(Fake &f) { return reinterpret_cast<cIGZUnknown *>(&f); }

void Build(uint32_t seed, drawfx::Cells &cells)
{
    gw.framework.Reset<sgfx::kFramework>(460);
    gw.framework.On(5, &GetService);
    gw.graphics.Reset<kGraphics>(520);
    gw.device.Reset<kDevice>(521);
    gw.device.Returns(35, uint32_t(seed % 5 != 0));
    gw.com.Reset<kCom>(522);
    gw.com.On(3, &GetClassObject);
    gw.sink.Reset<kSink>(523);
    cells.Set(0x10f8f94, static_cast<const void *>(&gw.sink)); // the texture manager's cache (0xae8750)
    sgfx::MakeListener(gw.connectionListener, 524);
    for (unsigned i = 0; i < kArrays; ++i) {
        Fake &a = gw.arrays[i];
        a.Reset<kArray>(kArrayId + i);
        a.On(15, &ArrayLock);
        a.On(18, &ArraySize);
        a.value[0] = 4 + 4 * ((seed + i) % 4); // 4..16 bytes
        // One seed in six: one array's Create fails (the buffers are then not ready).
        a.Returns(20, uint32_t(!(seed % 6 == 5 && i == seed % kArrays)));
        std::memset(gw.buffers[i], 0xa5, kBufferBytes);
    }
    gw.resource.Reset<kResource>(525);
    gw.resource.On(3, &ResourceLoad);
    gw.resource.On(15, &ResourceExists);
    gw.texture.Reset<kTexture>(526);
    gw.parser.Reset<kParser>(527);
    gw.parser.On(4, &ParserParse);
    gw.parser.On(6, &ParserMake);
    gw.parser.On(7, &ParserRefine);
    gw.formatsManager.Reset<kFormatsMgr>(528);
    gw.formatsManager.On(8, &FormatByCode);
    auto material = [](Fake &m, uint32_t id) {
        m.Reset<kMaterial>(id);
        m.On(10, &MaterialSetName);
        m.On(18, &MaterialSame);
        m.On(19, &MaterialName);
        m.On(20, &MaterialText);
    };
    for (unsigned i = 0; i < kMaterials; ++i) material(gw.materials[i], kMaterialId + i);
    for (unsigned i = 0; i < kExtras; ++i) material(gw.extras[i], kExtraId + i);
    for (unsigned i = 0; i < kMade; ++i) {
        material(gw.made[i], kMadeId + i);
        gw.madeNamed[i] = false;
    }
    for (unsigned i = 0; i < kMats; ++i) {
        for (unsigned k = 0; k < 3; ++k) gw.words[i][k] = 0x100 * i + k;
        for (unsigned b = 0; b < 2; ++b)
            for (unsigned k = 0; k < 13; ++k) gw.blobs[i][b].w[k] = 0x1000 * i + 0x100 * b + k;
    }
    for (unsigned i = 0; i < kParsed; ++i) gw.parsed[i].Reset<sgfx::kUnknown>(kParsedId + i);
    for (unsigned i = 0; i < kListeners; ++i) {
        gw.listeners[i].Reset<kListener>(kListenerId + i);
        gw.listeners[i].On(6, &ListenerHash);
        gw.listeners[i].On(7, &ListenerEqual);
        gw.listeners[i].On(10, &VariantServes);
        gw.listeners[i].value[0] = i % 3; // hashes collide
    }
    // The listeners and the variants answer the reset query (0xcd2e1781) with another listener now and then, and the
    // variant query (0xfc17e651) with one more.
    for (unsigned i = 0; i < kListeners; ++i) {
        if (i % 2 == 0) gw.listeners[i].AddFace(kResetQuery, gw.listeners[(i + 1) % kListeners]);
        gw.listeners[i].AddFace(kVariantQuery, gw.listeners[(i + 2) % kListeners]);
    }
    for (unsigned i = 0; i < kVariants; ++i) {
        Fake &variant = gw.variants[i];
        variant.Reset<kListener>(kVariantId + i);
        variant.On(6, &ListenerHash);
        variant.On(7, &ListenerEqual);
        variant.On(10, &VariantServes);
        variant.value[0] = i % 3;
        if (i % 2 == 0) variant.AddFace(kResetQuery, gw.variants[(i + 1) % kVariants]);
        variant.AddFace(kVariantQuery, gw.variants[(i + 3) % kVariants]);
    }
    // A made material always answers the variant query (the variant it is registered as).
    for (unsigned i = 0; i < kMade; ++i) gw.made[i].AddFace(kVariantQuery, gw.variants[i % kVariants]);
    for (unsigned i = 0; i < kFormats; ++i) gw.formats[i].Reset<sgfx::kUnknown>(kFormatId + i);
    gw.nextArray = gw.nextMade = 0;
    gw.want = nullptr;
    gw.lcg = seed * 2654435761u + 7;
    gw.deviceUp = seed % 3 != 0;
    gw.arraysLive = gw.deviceUp;
    gw.clock = gw.step = 0;
    gw.heapBlock = seed % 4 == 3;
    gw.listen = seed % 3 == 1;
    cells.Set(kFrameworkCell, &gw.framework);
    sgfx::gPool.Reset(cells);
    gw.events.Use(cells, cMaterialManagerBehaviorTraits::kEventManagerCell);
}

// ---- planting state through the typed view ----
const MM::NameCell &CellOf(const ServiceCell &raw) { return reinterpret_cast<const MM::NameCell &>(raw); }

// A key in a small space, so the keys the implied-key code derives from it (its bits field replaced, the flag bits
// cleared, the related formats put in, the blob keys) now and then equal a variant already there, or each other.
MM::VariantKey SmallKey(std::mt19937 &rng)
{
    static constexpr uint32_t kWords[] = {0, 1, 5, 6, 0xffffffffu, 0x100, 0x01000000};
    MM::VariantKey key{};
    key.w[0] = kWords[rng() % 7];
    key.w[1] = kWords[rng() % 7];
    uint32_t w2 = (rng() % 4) << 4 | (rng() % 4) << 11 | (rng() % 8);
    if (rng() % 3 == 0) w2 |= rng() & 0xfffe0000u;
    if (rng() % 8 == 0) w2 |= (rng() % 128) << 4;
    key.w[2] = w2;
    return key;
}

// A blob whose words 1-12 give constant, small, medium and full-range sums (the packed nibbles then fit, or spill over).
MM::Blob52 MakeBlob(std::mt19937 &rng)
{
    MM::Blob52 b{};
    const unsigned mode = rng() % 4;
    for (unsigned k = 0; k < 13; ++k) b.w[k] = mode == 0 ? k : mode == 1 ? rng() % 3 : mode == 2 ? rng() % 40 : rng();
    return b;
}

void Plant(MM *v, std::mt19937 &rng)
{
    // The names interned in a random order (the map is ordered by handle).
    unsigned order[kMaterials];
    for (unsigned i = 0; i < kMaterials; ++i) order[i] = i;
    for (unsigned i = kMaterials; i > 1; --i) std::swap(order[i - 1], order[rng() % i]);
    for (unsigned i = 0; i < kMaterials; ++i) sgfx::gPool.Handle(kNames[order[i]]);

    const unsigned slots = rng() % 11;
    unsigned waiting = 0;
    for (unsigned i = 0; i < slots; ++i) {
        if (rng() % 4 == 0) {
            v->mByIndex.push_back(nullptr);
            v->mFreeIndices.push_back(i);
            continue;
        }
        const ServiceCell raw{sgfx::gPool.Handle(kNames[i])};
        MM::Entry &e = v->mEntries
                           .emplace(std::piecewise_construct, std::forward_as_tuple(CellOf(raw)),
                                    std::forward_as_tuple(i, CellOf(raw)))
                           .first->second;
        e.material.Assign(U(gw.materials[i]));
        if (rng() % 3) e.parsed.Assign(U(gw.parsed[rng() % kParsed]));
        e.flags = rng() % 8;
        e.references = int32_t(rng() % 4) - 1;
        for (unsigned k = rng() % 4; k-- > 0;)
            e.variants[rng() % 2 ? kKeys[rng() % kKeyCount] : SmallKey(rng)].Assign(U(gw.variants[rng() % kVariants]));
        for (unsigned k = rng() % 3; k-- > 0;) e.keys.emplace(U(gw.formats[rng() % kFormats]));
        for (unsigned k = rng() % 5; k-- > 0;) e.words.push_back(rng() % 3 ? rng() % 4 : rng());
        for (unsigned k = rng() % 4; k-- > 0;) e.blobs.push_back(MakeBlob(rng));
        e.byte5c = rng() % 3 ? uint8_t(1 + rng() % 255) : 0;
        if (rng() % 8 == 0) { // an entry without a name (never destroyed by name: that needs flag 2)
            e.name.mValue = 0xffffffffu;
            e.flags &= ~2u;
        }
        v->mByIndex.push_back(&e);
        // Waiting entries: deleted when unused (flag 2), or now and then not (DestroyEntry then keeps the entry).
        if (e.references == 0 && rng() % 2 && ((e.flags & 2) || rng() % 4 == 0)) {
            e.countdown = int32_t(1 + 2 * waiting++); // distinct: 1, 3, 5, ...
            v->mPending.insert(&e);
        }
    }
    if (rng() % 2) std::reverse(v->mFreeIndices.begin(), v->mFreeIndices.end());
    // Jobs for entries that are there.
    std::vector<MM::Entry *> live;
    for (MM::Entry *e : v->mByIndex)
        if (e && e->name.mValue != 0xffffffffu) live.push_back(e);
    if (!live.empty())
        for (unsigned k = rng() % 6; k-- > 0;) {
            MM::Entry *e = live[rng() % live.size()];
            MM::Job &job = v->mJobs.emplace_back(e->name, kKeys[rng() % kKeyCount]);
            if (rng() % 2) job.format.Assign(U(gw.formats[rng() % kFormats]));
        }
    for (unsigned k = rng() % 4; k-- > 0;) v->mListenersA.insert(U(gw.listeners[rng() % kListeners]));
    for (unsigned k = rng() % 4; k-- > 0;) v->mListenersB.insert(U(gw.listeners[rng() % kListeners]));
}

// ---- noting the state ----
uint32_t IndexOf(const MM::Entry *e) { return e ? e->index : 0xffffffffu; }

void NoteState(const MM *v)
{
    difftest::NotCounted bookkeeping;
    const uint32_t *w = reinterpret_cast<const uint32_t *>(v);
    drawfx::Note("refs, ready, started", {w[1], uint32_t(v->mBuffersReady), uint32_t(v->mStarted)});
    for (const auto &[key, e] : v->mEntries) {
        drawfx::Call c;
        c.note = "entry";
        c.words = {key.mValue, e.index, e.name.mValue, drawfx::Ptr(e.material.p), drawfx::Ptr(e.parsed.p), e.flags,
                   uint32_t(e.references), uint32_t(e.countdown), uint32_t(e.variants.size())};
        for (const auto &[k, ref] : e.variants) c.words.insert(c.words.end(), {k.w[0], k.w[1], k.w[2], drawfx::Ptr(ref.p)});
        c.words.push_back(uint32_t(e.keys.size()));
        for (const MM::Held &k : e.keys) c.words.push_back(drawfx::Ptr(k.p));
        c.words.push_back(uint32_t(e.words.size()));
        c.words.insert(c.words.end(), e.words.begin(), e.words.end());
        c.words.push_back(uint32_t(e.blobs.size()));
        for (const MM::Blob52 &b : e.blobs) c.words.insert(c.words.end(), std::begin(b.w), std::end(b.w));
        c.words.push_back(e.byte5c);
        drawfx::gLog.push_back(std::move(c));
    }
    drawfx::Call c;
    c.note = "by index, free, waiting";
    for (const MM::Entry *e : v->mByIndex) c.words.push_back(IndexOf(e));
    c.words.push_back(0xfeedu);
    c.words.insert(c.words.end(), v->mFreeIndices.begin(), v->mFreeIndices.end());
    c.words.push_back(0xfeedu);
    std::vector<uint32_t> waiting;
    for (const MM::Entry *e : v->mPending) waiting.push_back(IndexOf(e));
    std::sort(waiting.begin(), waiting.end());
    c.words.insert(c.words.end(), waiting.begin(), waiting.end());
    drawfx::gLog.push_back(std::move(c));
    drawfx::Call l;
    l.note = "listeners A, B";
    for (cIGZUnknown *x : v->mListenersA) l.words.push_back(drawfx::Ptr(x));
    l.words.push_back(0xfeedu);
    for (cIGZUnknown *x : v->mListenersB) l.words.push_back(drawfx::Ptr(x));
    drawfx::gLog.push_back(std::move(l));
    drawfx::Call j;
    j.note = "jobs";
    for (const MM::Job &job : v->mJobs)
        j.words.insert(j.words.end(), {job.name.mValue, job.key.w[0], job.key.w[1], job.key.w[2], drawfx::Ptr(job.format.p)});
    drawfx::gLog.push_back(std::move(j));
    drawfx::Call a;
    a.note = "arrays, buffers";
    for (const MM::VertexRef &r : v->mVerticesA) a.words.push_back(drawfx::Ptr(r.p));
    for (const MM::VertexRef &r : v->mVerticesB) a.words.push_back(drawfx::Ptr(r.p));
    a.words.push_back(drawfx::Ptr(v->mIndicesA.p));
    a.words.push_back(drawfx::Ptr(v->mIndicesB.p));
    for (unsigned i = 0; i < kArrays; ++i) {
        uint32_t words[kBufferBytes / 4];
        std::memcpy(words, gw.buffers[i], kBufferBytes);
        a.words.insert(a.words.end(), std::begin(words), std::end(words));
    }
    drawfx::gLog.push_back(std::move(a));
}

void NoteFakes()
{
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "fake refs";
    for (Fake *f : {&gw.graphics, &gw.device, &gw.com, &gw.sink, &gw.connectionListener, &gw.resource, &gw.texture,
                    &gw.parser, &gw.formatsManager})
        c.words.push_back(uint32_t(f->refs));
    for (const Fake &f : gw.arrays) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.materials) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.extras) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.made) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.parsed) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.listeners) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.variants) c.words.push_back(uint32_t(f.refs));
    for (const Fake &f : gw.formats) c.words.push_back(uint32_t(f.refs));
    drawfx::gLog.push_back(std::move(c));
    sgfx::gPool.NoteCounts();
}

// An index to pass: -1 one time in eight, else a live entry's (or -1 when there is none). Not started, any index is
// fine (the call does nothing), so a dead one is passed then too.
int32_t PickIndex(const MM *v, std::mt19937 &rng)
{
    const uint32_t r = rng();
    if (r % 8 == 0) return -1;
    std::vector<int32_t> live;
    for (size_t i = 0; i < v->mByIndex.size(); ++i)
        if (v->mByIndex[i]) live.push_back(int32_t(i));
    if (!v->mStarted && !v->mByIndex.empty() && r % 8 == 1) return int32_t(rng() % v->mByIndex.size());
    if (live.empty()) return -1;
    return live[rng() % live.size()];
}

// Would ReleaseIndex(index) queue its entry beside another already at 10 (two destroyed on the same tick)?
bool QueuesBesideAnother(const MM *v, int32_t index)
{
    if (!v->mStarted || index == -1) return false;
    const MM::Entry *e = v->mByIndex[size_t(index)];
    if (e->references != 1 || !(e->flags & 2)) return false;
    for (const MM::Entry *other : v->mPending)
        if (other != e && other->countdown == MM::kDeleteDelay) return true;
    return false;
}

const char *PickName(std::mt19937 &rng)
{
    const uint32_t r = rng() % 14;
    if (r < 10) return kNames[r];
    if (r < 12) return kOtherNames[r - 10];
    return nullptr;
}

// A name for the lookups that load: the planted and other names, the "#" ones, the extras; never null.
const char *PickLoadName(std::mt19937 &rng)
{
    const uint32_t r = rng() % 20;
    if (r < 10) return kNames[r];
    if (r < 12) return kOtherNames[r - 10];
    if (r < 16) return kHashNames[r - 12];
    return kExtraNames[r - 16];
}

// A text for the slot-20 lookups (either case); null now and then.
const char *PickText(std::mt19937 &rng)
{
    static const char *const texts[] = {"mat0", "MAT1", "mat2", "MAT3", "Mat4", "mat5", "MAT6", "mat7", "mat8", "MAT9",
                                        "extra0", "EXTRA1", "extra2", "EXTRA3", "nothing", nullptr};
    return texts[rng() % (sizeof texts / sizeof texts[0])];
}

cIGZUnknown *PickListener(std::mt19937 &rng)
{
    return rng() % 2 ? U(gw.listeners[rng() % kListeners]) : U(gw.variants[rng() % kVariants]);
}

// Part 2's slots: FindOrLoad 19, AddMaterial 23, ReloadAll 24, Register 26, Unregister 27, LoadStale 29, 30, IsAvailable 32,
// GetInfoByName 37 (and through them 0xacc640, 0xacbaf0, 0xacce00 and the variant builder 0xacadd0).
void StepLoading(cIMaterialManager *m, std::mt19937 &rng, unsigned op)
{
    switch (op) {
    case 16:
    case 27: {
        const char *name = PickLoadName(rng);
        gw.want = rng() % 4 ? name : nullptr;
        const uint32_t index = m->FindOrLoad(name);
        gw.want = nullptr;
        drawfx::Note("find or load", index);
        break;
    }
    case 17: {
        Fake &material = rng() % 2 ? gw.materials[rng() % kMaterials] : gw.extras[rng() % kExtras];
        drawfx::Note("add material", m->AddMaterial(U(material), rng() % 8));
        break;
    }
    case 18:
    case 25: m->ReloadAll(rng() % 3 ? nullptr : PickName(rng), rng() % 4); break;
    case 19:
    case 26: {
        static constexpr uint32_t kIids[] = {kVariantQuery, kResetQuery, 1, 0x1234};
        void *out = nullptr;
        const uint32_t result = m->Register(PickListener(rng), kIids[rng() % 4], &out);
        drawfx::Note("register", {result, drawfx::Ptr(out)});
        break;
    }
    case 20: m->Unregister(PickListener(rng)); break;
    case 21: m->LoadStale(); break;
    case 22: {
        const char *text = PickText(rng);
        m->Slot30(P(text)); // its result is whatever the last cleanup left in eax: nothing to compare
        drawfx::Note("slot 30", 0u);
        break;
    }
    case 23: drawfx::Note("is available", uint32_t(m->IsAvailable(PickLoadName(rng))) & 0xff); break;
    default: {
        const MaterialInfo info = m->GetInfoByName(rng() % 5 ? PickText(rng) : PickName(rng));
        drawfx::Note("info by name", {info.nameHash, drawfx::Ptr(info.material), drawfx::Ptr(info.parsed),
                                      uint32_t(info.references), info.variants});
        break;
    }
    }
}

// ---- slots 34-36: the ranges, and 22: the variant for a key ----
// The iterator objects are the game's classes on both sides (pool-allocated; their slots are the image's, called through
// the object's vtable), so nothing here is stood in: each range's iterators are noted (the count and the position words
// past the vtable), a clone of begin walks to end through Clone, IsEqual, Get and Next, and all are released (the
// image's deleting destructor frees the pool block).
class cIHandleIterator : public cIGZUnknown {
public:
    virtual cIHandleIterator *Clone() = 0;                    // 3
    virtual bool IsEqual(const cIHandleIterator *other) = 0;  // 4
    virtual uint32_t Get() = 0;                               // 5: the index
    virtual void Next() = 0;                                  // 6: on to the next index that holds an entry
    virtual ~cIHandleIterator() = default;                    // 7
};
class cIVariantIterator : public cIGZUnknown {
public:
    virtual cIVariantIterator *Clone() = 0;
    virtual bool IsEqual(const cIVariantIterator *other) = 0;
    virtual void *Get(uint32_t *out) = 0; // 5: copies {key, variant} out (returns the pointer)
    virtual void Next() = 0;
    virtual ~cIVariantIterator() = default;
};
class cIListenerIterator : public cIGZUnknown {
public:
    virtual cIListenerIterator *Clone() = 0;
    virtual bool IsEqual(const cIListenerIterator *other) = 0;
    virtual cIGZUnknown *Get() = 0; // 5: the listener's answer to the reset query
    virtual void Next() = 0;
    virtual ~cIListenerIterator() = default;
};

enum class Kind { Handle, Variant, Listener };

// The pools (cRZFastFixedAllocator2 instances at 0x10f6818, 0x10f6824, 0x10f6830, each a pointer to its current page at
// +4) point at a test page each: 0x800-aligned, header +8 the free list, +0xc the used count, set far from empty and from
// full so neither side reaches the page bookkeeping (game code not under test).
struct RangePools {
    static constexpr uint32_t kPools[3] = {0x10f681c, 0x10f6828, 0x10f6834};
    static constexpr uint32_t kBlocks[3] = {0x10, 0xc, 0xc};
    static constexpr uint32_t kPage = 0x800, kTags[3] = {0x7b200000, 0x7b300000, 0x7b400000};
    uint8_t *pages[3];
    uint32_t saved[3];
    RangePools()
    {
        for (int i = 0; i < 3; ++i) {
            pages[i] = static_cast<uint8_t *>(_aligned_malloc(kPage, kPage));
            saved[i] = *GameAddress<uint32_t>(kPools[i]);
            *GameAddress<uint32_t>(kPools[i]) = P(pages[i]);
        }
    }
    ~RangePools()
    {
        for (int i = 0; i < 3; ++i) {
            *GameAddress<uint32_t>(kPools[i]) = saved[i];
            _aligned_free(pages[i]);
        }
    }
    void Reset()
    {
        for (int i = 0; i < 3; ++i) {
            std::memset(pages[i], 0, kPage);
            uint32_t *header = reinterpret_cast<uint32_t *>(pages[i]);
            header[3] = 1000;
            uint32_t next = 0;
            for (uint32_t at = kPage - kBlocks[i]; at >= 0x10; at -= kBlocks[i]) {
                *reinterpret_cast<uint32_t *>(pages[i] + at) = next;
                next = P(pages[i] + at);
            }
            header[2] = next;
            drawfx::AddRange(pages[i], kPage, kTags[i]);
        }
    }
    void Note() const
    {
        for (int i = 0; i < 3; ++i) {
            const uint32_t *header = reinterpret_cast<const uint32_t *>(pages[i]);
            drawfx::Note("pool", {drawfx::Ptr(header[2]), header[3]});
        }
    }
};
RangePools *gRangePools = nullptr;

uint32_t WordOf(const cIGZUnknown *it, unsigned k) { return reinterpret_cast<const uint32_t *>(it)[k]; }

void NoteIterator(const char *label, const cIGZUnknown *it, Kind kind)
{
    drawfx::Call c;
    c.note = label;
    if (!it) {
        c.words.push_back(0);
    } else if (kind == Kind::Handle) { // count, index, the vector
        c.words = {WordOf(it, 1), WordOf(it, 2), drawfx::Ptr(WordOf(it, 3))};
    } else { // count, the list node
        c.words = {WordOf(it, 1), drawfx::Ptr(WordOf(it, 2))};
    }
    drawfx::gLog.push_back(std::move(c));
}

// Does the listener a listener iterator stands on answer the reset query (its Get asks it; a listener that does not
// leaves the iterator's own stack word, which is not the object's to compare)?
bool ListenerAnswers(const cIGZUnknown *it)
{
    const Fake *listener = reinterpret_cast<const Fake *>(reinterpret_cast<const uint32_t *>(WordOf(it, 2))[2]);
    for (unsigned i = 0; i < listener->faceCount; ++i)
        if (listener->faces[i].iid == kResetQuery) return true;
    return false;
}

template <Kind K, class It> void WalkRange(const MaterialRange &r)
{
    auto *begin = reinterpret_cast<It *>(r.begin);
    auto *end = reinterpret_cast<It *>(r.end);
    if (!begin || !end) return;
    It *it = begin->Clone();
    NoteIterator("clone", reinterpret_cast<cIGZUnknown *>(it), K);
    if (!it) return;
    drawfx::Note("clone addref", it->AddRef());
    drawfx::Note("begin is end", uint32_t(begin->IsEqual(end)));
    for (int k = 0; k < 24 && !it->IsEqual(end); ++k) {
        drawfx::Call c;
        c.note = "item";
        if constexpr (K == Kind::Handle) {
            c.words.push_back(it->Get());
        } else if constexpr (K == Kind::Variant) {
            uint32_t out[4] = {0xdead0000u, 0xdead0001u, 0xdead0002u, 0xdead0003u};
            const void *back = it->Get(out);
            c.words = {out[0], out[1], out[2], drawfx::Ptr(out[3]), uint32_t(back == out)};
        } else if (ListenerAnswers(reinterpret_cast<cIGZUnknown *>(it))) {
            cIGZUnknown *got = it->Get();
            c.words.push_back(drawfx::Ptr(got));
            if (got) got->Release();
        } else {
            c.words.push_back(0xbad);
        }
        drawfx::gLog.push_back(std::move(c));
        it->Next();
    }
    drawfx::Note("clone at end", uint32_t(it->IsEqual(end)));
    drawfx::Note("clone released", it->Release());
}

template <Kind K, class It> void NoteRange(const char *label, const MaterialRange &r)
{
    drawfx::Note(label, {drawfx::Ptr(r.cursor), drawfx::Ptr(r.begin), drawfx::Ptr(r.end)});
    NoteIterator("begin", r.begin, K);
    NoteIterator("end", r.end, K);
    WalkRange<K, It>(r);
    for (cIGZUnknown *it : {r.cursor, r.begin, r.end})
        if (it) drawfx::Note("iterator released", it->Release());
    gRangePools->Note();
}

// Slot 22 on a live entry: the key is one the entry has, a fixed one, or one from a small space (so the keys derived from
// it meet the variants already there); the flag arguments sit around the masks' limits. A key the parser could not make a
// variant for is a miss: the original returns an uninitialised word then (ORIGINAL-BUG in the source), so only the state
// is compared.
MM::VariantKey PickVariantKey(const MM::Entry &entry, std::mt19937 &rng)
{
    const uint32_t r = rng() % 8;
    if (r == 0 && !entry.variants.empty()) {
        auto it = entry.variants.begin();
        std::advance(it, rng() % entry.variants.size());
        return it->first;
    }
    if (r == 1) return kKeys[rng() % kKeyCount];
    return SmallKey(rng);
}

void StepVariant(cIMaterialManager *m, MM *v, std::mt19937 &rng)
{
    static constexpr uint32_t kQueues[] = {0, 1, 0x100, 0xff, 0x80000000u, 2};
    static constexpr uint32_t kRefs[] = {0, 1, 2, 3, 0x3f, 0x40, 0x7f, 0x80, 0xff, 0x1234, 0xffffffffu};
    const uint32_t queue = rng() % 6 ? kQueues[rng() % 6] : rng();
    // The jobs a call queues name the entry, and a tick looks it up by that name: an entry without a name is only
    // used when nothing is queued (the low byte of the queue argument zero).
    std::vector<uint32_t> live;
    for (size_t i = 0; i < v->mByIndex.size(); ++i)
        if (v->mByIndex[i] && (v->mByIndex[i]->name.mValue != 0xffffffffu || (queue & 0xff) == 0)) live.push_back(uint32_t(i));
    if (live.empty()) {
        drawfx::Note("variant: no entry", 0u);
        return;
    }
    const uint32_t index = live[rng() % live.size()];
    const MM::Entry *entry = v->mByIndex[index];
    const MM::VariantKey key = PickVariantKey(*entry, rng);
    const uint32_t a = rng() % 4 ? kRefs[rng() % 11] : rng();
    const uint32_t b = rng() % 4 ? kRefs[rng() % 11] : rng();
    cIGZUnknown *const got = m->GetVariant(index, key.w, queue, a, b);
    const auto found = entry->variants.find(key);
    if (found == entry->variants.end()) drawfx::Note("variant miss", 0u);
    else drawfx::Note("variant", {drawfx::Ptr(got), drawfx::Ptr(found->second.p)});
}

// A FlushPending with several entries waiting destroys them in address order, which differs between the sides and
// between runs. Each destroyed entry drops its jobs from the job deque (an erase that copies the jobs behind it, each
// copy a handle count on the pool), and what those copies are depends on which entry went first. So that the calls do
// not depend on the order, only one waiting entry (the lowest index with jobs) keeps its jobs here; the others' jobs
// are erased before the call, by index and so alike on both sides (noted like any other call). One entry's jobs
// erased among others' that are not is order-free.
void KeepJobsOfOneWaitingEntry(MM *v)
{
    uint32_t keep = 0xffffffffu, keepName = 0;
    for (const MM::Entry *e : v->mPending)
        for (const MM::Job &job : v->mJobs)
            if (job.name.mValue == e->name.mValue && e->index < keep) {
                keep = e->index;
                keepName = e->name.mValue;
            }
    std::vector<uint32_t> waitingNames; // sorted: one pass over the jobs in their own order, not in the set's
    for (const MM::Entry *e : v->mPending)
        if (e->name.mValue != keepName) waitingNames.push_back(e->name.mValue);
    std::sort(waitingNames.begin(), waitingNames.end());
    for (size_t i = 0; i < v->mJobs.size();) {
        if (std::binary_search(waitingNames.begin(), waitingNames.end(), v->mJobs[i].name.mValue))
            v->mJobs.erase(v->mJobs.begin() + ptrdiff_t(i));
        else
            ++i;
    }
}

void Step(difftest::Context &ctx, cIMaterialManager *m, MM *v, std::mt19937 &rng, unsigned step)
{
    const unsigned op = rng() % 32;
    char mark[48];
    std::snprintf(mark, sizeof mark, "step %u, op %u", step, op);
    sgfx::MarkStep(ctx, mark);
    drawfx::Note("op", op);
    switch (op) {
    case 0: m->Start(); break;
    case 1:
        if (rng() % 3) {
            m->Start();
            break;
        }
        m->Shutdown();
        gw.arraysLive = false;
        break;
    case 2:
    case 3: {
        const unsigned ticks = 1 + rng() % 3;
        for (unsigned t = 0; t < ticks; ++t) {
            gw.clock = rng() % 30;
            gw.step = rng() % 12;
            m->OnTick();
        }
        break;
    }
    case 4: {
        const size_t mark0 = drawfx::gLog.size(), free0 = v->mFreeIndices.size();
        const bool several = v->mPending.size() > 1;
        if (several) KeepJobsOfOneWaitingEntry(v);
        m->FlushPending();
        if (several) {
            difftest::NotCounted bookkeeping;
            std::stable_sort(drawfx::gLog.begin() + ptrdiff_t(mark0), drawfx::gLog.end(),
                             [](const drawfx::Call &a, const drawfx::Call &b) { return drawfx::Describe(a) < drawfx::Describe(b); });
            std::sort(v->mFreeIndices.begin() + ptrdiff_t(free0), v->mFreeIndices.end());
        }
        break;
    }
    case 5: m->AddRefIndex(PickIndex(v, rng)); break;
    case 6:
    case 7: {
        int32_t index = PickIndex(v, rng);
        if (QueuesBesideAnother(v, index)) index = -1;
        m->ReleaseIndex(index);
        break;
    }
    case 8: {
        const char *name = rng() % 3 ? PickName(rng) : nullptr;
        m->Reset(name, rng() % 4);
        break;
    }
    case 9:
        if (rng() % 3 == 0) m->MarkAllStale();
        else drawfx::Note("is loaded", uint32_t(m->IsLoaded(PickName(rng))) & 0xff);
        break;
    case 10: {
        const MaterialInfo info = m->GetInfo(PickIndex(v, rng));
        drawfx::Note("info", {info.nameHash, drawfx::Ptr(info.material), drawfx::Ptr(info.parsed), uint32_t(info.references),
                              info.variants});
        break;
    }
    case 11:
    case 12: {
        // The device event: the device there or not, ready or not (never ready once the arrays are gone: the original
        // would call them), the arrays' Create succeeding or (one in ten) not.
        gw.deviceUp = rng() % 4 != 0;
        gw.device.Returns(35, uint32_t(gw.arraysLive && rng() % 3 != 0));
        for (Fake &a : gw.arrays) a.Returns(20, uint32_t(rng() % 40 != 0));
        uint32_t data[2] = {rng(), rng() % 3};
        m->OnEvent(MM::kDeviceEvent, rng() % 2, data, 0);
        break;
    }
    case 13: {
        uint32_t data[2] = {0, 0};
        m->OnEvent(rng() % 2 ? kConnectionChanged : rng(), 0, data, 0);
        break;
    }
    case 14: {
        static constexpr uint32_t kIids[] = {cIMaterialManager::kIid, 1, MM::kIidBehavior2, MM::kIidBehaviorType,
                                             0xdeadbeef, kConnectionChanged};
        const uint32_t iid = kIids[rng() % 6];
        void *out = nullptr;
        const uint32_t before = reinterpret_cast<const uint32_t *>(v)[1];
        const bool got = m->QueryInterface(iid, rng() % 5 ? &out : nullptr);
        drawfx::Note("query", {iid, uint32_t(got), drawfx::Ptr(out)});
        if (reinterpret_cast<const uint32_t *>(v)[1] != before) m->Release();
        break;
    }
    case 15: {
        const uint32_t a = m->AddRef();
        const uint32_t b = m->Release();
        drawfx::Note("addref, release", {a, b});
        break;
    }
    case 28: StepVariant(m, v, rng); break;
    case 29: NoteRange<Kind::Handle, cIHandleIterator>("entry range", m->EntryRange()); break;
    case 30: {
        std::vector<uint32_t> live;
        for (size_t i = 0; i < v->mByIndex.size(); ++i)
            if (v->mByIndex[i]) live.push_back(uint32_t(i));
        if (live.empty()) drawfx::Note("variant range: no entry", 0u);
        else NoteRange<Kind::Variant, cIVariantIterator>("variant range", m->VariantRange(live[rng() % live.size()]));
        break;
    }
    case 31: NoteRange<Kind::Listener, cIListenerIterator>("listener range", m->ListenerRange()); break;
    default: StepLoading(m, rng, op); break;
    }
    NoteState(v);
}

void Life(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    difftest::AllocationCounter counter;
    void *block = gw.heapBlock ? ::operator new(MM::kSize) : gBlock;
    drawfx::AddRange(block, MM::kSize, kBlockTag);
    std::memset(block, 0xcd, MM::kSize);
    if (original)
        ctx.Original<void *(__thiscall *)(void *)>(0xac9810)(block);
    else
        ::new (block) MM();
    cIMaterialManager *m = static_cast<cIMaterialManager *>(reinterpret_cast<MM *>(block));
    MM *v = reinterpret_cast<MM *>(block);
    NoteState(v);
    if (gw.listen) gw.events.Listen(block, MM::kDeviceEvent, gw.connectionListener, 0x1234);
    Plant(v, rng);
    NoteState(v);
    const unsigned steps = 4 + rng() % 24;
    for (unsigned s = 0; s < steps; ++s) Step(ctx, m, v, rng, s);
    sgfx::MarkStep(ctx, "destroying");
    if (gw.heapBlock) {
        drawfx::Note("last release", m->Release());
    } else if (original) {
        ctx.Original<void(__thiscall *)(void *)>(0xac9ee0)(block);
    } else {
        v->~MM();
    }
    NoteFakes();
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
    gw.events.Drop();
}

} // namespace

// Verifies: the constructor 0xac9810, destructor 0xac9ee0, deleting destructor 0xaca420, QueryInterface 0xacc510, OnEvent
// 0xacba20, OnDeviceEvent 0xacb780, CreateDeviceObjects 0xacbe90, Start 0xacbae0, Shutdown 0xacd8b0, OnTick 0xacda40,
// FlushPending 0xacb630, AddRefIndex 0xacb400, ReleaseIndex 0xaccb20, Reset 0xacb260, MarkAllStale 0xacac60, IsLoaded
// 0xacba60, GetInfo 0xacc410, DestroyEntry 0xacb560, ResetEntry 0xacb170; part 2: BuildVariant 0xacadd0, FindOrLoad
// 0xacb7b0, LoadStale 0xacb470, BuildEntry 0xacbaf0 (AddMaterial), LoadMaterial 0xacc640, IsAvailable 0xacc0d0,
// GetInfoByName 0xacc320, slot 30 0xaccce0, Register 0xacc840, ReloadEntry 0xacce00, ReloadAll 0xaccfb0, Unregister
// 0xacdbc0; part 3: GetVariant 0xacd2d0 (slot 22, with the implied-key helpers 0xaca4d0, 0xaca5a0 and 0xaca6f0 inside it),
// EntryRange 0xaca850, VariantRange 0xaccc00, ListenerRange 0xacd7e0.
DIFF_TEST(SG_MaterialManager_Life, 0xac9810)
{
    Stands stands;
    RangePools pools;
    gRangePools = &pools;
    sgfx::CheckSides(
        ctx, "material manager", 400,
        [&](uint32_t seed, drawfx::Cells &cells) {
            Build(seed, cells);
            pools.Reset();
        },
        [&](bool original, std::mt19937 &rng) {
            Life(ctx, original, rng);
            ++ctx.cases;
        });
    gRangePools = nullptr;
}

// ---- the registry ----
namespace {

using nGZEvent::cEventAttributeMap;
constexpr uint32_t kBaseMap = 0x10c94f4, kBaseMapGuard = 0x10c94fc, kBaseOnce = 0x10c9510, kBaseHolderGuard = 0x10c9518;
constexpr uint32_t kMap = 0x10f698c, kMapGuard = 0x10f6994, kOnce = 0x10f699c, kHolderGuard = 0x10f69a4;
constexpr uint32_t kRecord = 0x106fb18; // the device event's record in the image: id, name, three words (and 8)
constexpr uint32_t kBaseEventA = 0x5b51e806, kBaseEventB = 0xe9dd42cf;
char gNames[8][12];

struct Saved {
    uint32_t rva;
    unsigned size;
    uint8_t bytes[8];
};

class RegistryScope {
public:
    RegistryScope()
    {
        for (const auto &[rva, size] : {std::pair{kBaseMap, 8u}, {kBaseMapGuard, 4u}, {kBaseOnce, 1u}, {kBaseHolderGuard, 4u},
                                        {kMap, 8u}, {kMapGuard, 4u}, {kOnce, 1u}, {kHolderGuard, 4u}}) {
            Saved s{rva, size, {}};
            std::memcpy(s.bytes, GameAddress<void>(rva), size);
            saved_.push_back(s);
        }
        base_ = ::new (GameAddress<void>(kBaseMap)) nGZEvent::cEventAttributeMap;
        *GameAddress<int32_t>(kBaseMapGuard) = INT_MIN;
        *GameAddress<uint8_t>(kBaseOnce) = 1;
        *GameAddress<int32_t>(kBaseHolderGuard) = INT_MIN;
        for (unsigned i = 0; i < 3; ++i) {
            std::snprintf(gNames[i], sizeof gNames[i], "base%u", i);
            (*base_)[0x10 + i] = {0x10 + i, gNames[i], i, 0x100u + i, 0x200u + i, 0x300u + i};
        }
    }
    // The original side: the state its first-use build leaves (the base events stored, the base copied, the device
    // event's record with 4 in its last word), the guards and the holder's flag marked done.
    void PlantOriginal(difftest::Context &ctx)
    {
        ctx.Original<void(__thiscall *)(void *)>(0x1464f0)(nullptr);
        mine_ = ::new (GameAddress<void>(kMap)) nGZEvent::cEventAttributeMap(*base_);
        const uint32_t *record = GameAddress<uint32_t>(kRecord);
        (*mine_)[record[0]] = {record[0], reinterpret_cast<const char *>(uintptr_t(record[1])), record[2], record[3],
                               record[4], 4};
        *GameAddress<uint8_t>(kOnce) = 1;
        *GameAddress<int32_t>(kHolderGuard) = INT_MIN;
        *GameAddress<int32_t>(kMapGuard) = INT_MIN;
    }
    ~RegistryScope()
    {
        if (mine_) mine_->~cEventAttributeMap();
        base_->~cEventAttributeMap();
        for (auto it = saved_.rbegin(); it != saved_.rend(); ++it) std::memcpy(GameAddress<void>(it->rva), it->bytes, it->size);
    }
    RegistryScope(const RegistryScope &) = delete;
    RegistryScope &operator=(const RegistryScope &) = delete;

private:
    nGZEvent::cEventAttributeMap *base_ = nullptr, *mine_ = nullptr;
    std::vector<Saved> saved_;
};

std::string Snapshot(const nGZEvent::cEventAttributeMap *map)
{
    std::string out;
    char line[160];
    for (const auto &[key, a] : *map) {
        std::snprintf(line, sizeof line, "%08x:%08x:%s:%u:%u:%u:%u;", key, a.mId, a.mName ? a.mName : "<null>", a.mWord08,
                      a.mWord0C, a.mWord10, a.mWord14);
        out += line;
    }
    return out;
}

using RegistryFn = nGZEvent::cEventAttributeMap *(__cdecl *)(uint32_t, const nGZEvent::cEventAttribute *);

std::vector<std::string> RunRegistry(difftest::Context &ctx, uint32_t seed, bool ours, bool &stable)
{
    std::mt19937 rng(seed * 1103515245u + 12345u);
    RegistryScope scope;
    shape::StandIn baseStand{0x1485f0, &nGZEvent::cAbstractBehaviorType::RegistryFor, &pdfx::StandRegistry<kBaseMap>};
    if (ours)
        cMaterialManagerBehaviorTraits::ResetRegistryForTest();
    else
        scope.PlantOriginal(ctx);
    const uint32_t ids[] = {0,           0,           0x10,        0x11, 0x12,   kBaseEventA, kBaseEventB,
                            MM::kDeviceEvent, 0x77, 0x1000, 0xfffffff0u, rng(), rng()};
    for (unsigned i = 0; i < 8; ++i) std::snprintf(gNames[i], sizeof gNames[i], "n%u_%u", seed % 100, i);
    std::vector<std::string> out;
    nGZEvent::cEventAttributeMap *first = nullptr;
    stable = true;
    const unsigned calls = 1 + rng() % 6;
    for (unsigned c = 0; c < calls; ++c) {
        const uint32_t id = ids[rng() % (sizeof ids / sizeof ids[0])];
        nGZEvent::cEventAttribute attribute{rng() % 5 == 0 ? id : rng(), gNames[rng() % 8], rng() % 4, rng(), rng() % 3,
                                            rng() % 16};
        nGZEvent::cEventAttributeMap *map = ours ? cMaterialManagerBehaviorTraits::RegistryFor(id, &attribute)
                                                 : ctx.Original<RegistryFn>(0xacc550)(id, &attribute);
        if (!first) first = map;
        stable = stable && map == first;
        out.push_back(Snapshot(map));
    }
    if (ours) {
        out.push_back(Snapshot(cMaterialManagerBehaviorTraits::Registry()));
    } else {
        nGZEvent::cEventAttribute defaults{0, "", 0, 0, 0, 0};
        out.push_back(Snapshot(ctx.Original<RegistryFn>(0xacc550)(0, &defaults)));
    }
    if (ours) cMaterialManagerBehaviorTraits::ResetRegistryForTest(); // ours copied the planted base: not kept
    return out;
}

} // namespace

// Verifies the registry 0xacc550 with its holder's first-use build 0xac8940 (whose result the original side is planted
// with: the harness can't run the static's guard).
DIFF_TEST(SG_MaterialManager_Registry, 0xacc550)
{
    for (uint32_t seed = 1; seed <= 600; ++seed) {
        bool stableOriginal = false, stableOurs = false;
        const std::vector<std::string> original = RunRegistry(ctx, seed, false, stableOriginal);
        const std::vector<std::string> ours = RunRegistry(ctx, seed, true, stableOurs);
        ++ctx.cases;
        if (!stableOriginal || !stableOurs || original.size() != ours.size()) {
            ctx.Fail("seed %u: the returned map moved (original %d, ours %d) or the call counts differ (%zu / %zu)", seed,
                     stableOriginal, stableOurs, original.size(), ours.size());
            return;
        }
        for (size_t i = 0; i < original.size(); ++i)
            if (original[i] != ours[i]) {
                ctx.Fail("seed %u call %zu:\n  original %s\n  ours     %s", seed, i, original[i].c_str(), ours[i].c_str());
                return;
            }
    }
}

DIFF_TEST(SG_MixHash3, 0x98bf70)
{
    using Fn = uint32_t(__cdecl *)(uint32_t, uint32_t, uint32_t);
    const Fn original = ctx.Original<Fn>(0x98bf70);
    std::mt19937 rng(77);
    const uint32_t edges[] = {0, 1, 0x7fffffff, 0x80000000u, 0xffffffffu, 0x9e3779b9u, 0x12345678};
    for (unsigned i = 0; i < 20000; ++i) {
        uint32_t w[3];
        for (uint32_t &x : w) x = rng() % 4 == 0 ? edges[rng() % 7] : rng();
        ++ctx.cases;
        const uint32_t a = original(w[0], w[1], w[2]), b = MixHash3(w[0], w[1], w[2]);
        if (a != b) {
            ctx.Fail("MixHash3(%08x, %08x, %08x): original %08x, ours %08x", w[0], w[1], w[2], a, b);
            return;
        }
    }
}

// The getter 0xacc040 (as lane RB's PixelFormatsManager_Instance). The original with its guard 0x10f6988 set to
// "initialised" (INT_MIN: the thread-safe static's fast path) hands out its static 0x10f6878 every time; what that static
// holds is what the original constructor builds on it, so ours is compared with a manager the original constructor 0xac9810
// builds in the same world (no device: the graphics system's device event connected, nothing else): ours hands out one
// object every time, not the image's, with our vtable, and the same contents (the refcount, the flags, every container's
// size, the two vectors' reserves, the listener sets' buckets, the 34 arrays) and the same calls while building (when this
// runner builds it here; the object outlives the test, a function-local static of ours).
namespace {

void GetWorld(drawfx::Cells &cells)
{
    drawfx::ResetAll();
    gw.framework.Reset<sgfx::kFramework>(460);
    gw.framework.On(5, &GetService);
    gw.graphics.Reset<kGraphics>(520);
    gw.deviceUp = false;
    cells.Set(kFrameworkCell, &gw.framework);
    sgfx::gPool.Reset(cells);
}

void NoteFresh(const MM *v)
{
    NoteState(v);
    difftest::NotCounted bookkeeping;
    drawfx::Call c;
    c.note = "fresh";
    c.words = {uint32_t(v->mEntries.size()), uint32_t(v->mByIndex.capacity()), uint32_t(v->mFreeIndices.capacity()),
               uint32_t(v->mPending.size()), uint32_t(v->mListenersA.size()), uint32_t(v->mListenersA.bucket_count()),
               uint32_t(v->mListenersB.size()), uint32_t(v->mListenersB.bucket_count()), uint32_t(v->mJobs.size())};
    for (const MM::VertexRef &a : v->mVerticesA) c.words.push_back(drawfx::Ptr(a.p));
    for (const MM::VertexRef &a : v->mVerticesB) c.words.push_back(drawfx::Ptr(a.p));
    c.words.push_back(drawfx::Ptr(v->mIndicesA.p));
    c.words.push_back(drawfx::Ptr(v->mIndicesB.p));
    drawfx::gLog.push_back(std::move(c));
}

// The calls while building log the object's own address as an unregistered pointer's record (ours is not known before
// the getter returns, so neither side's is registered): it reads "self" on both sides.
std::vector<std::string> TakeLog(const void *self)
{
    uint32_t record = 0;
    if (!normwords::Pointer(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self)), drawfx::gRanges, record)) record = 0;
    char hex[16];
    std::snprintf(hex, sizeof hex, "0x%08x", record);
    std::vector<std::string> out;
    for (const drawfx::Call &c : drawfx::gLog) {
        std::string line = drawfx::Describe(c);
        for (size_t at; (at = line.find(hex)) != std::string::npos;) line.replace(at, std::strlen(hex), "self");
        out.push_back(line);
    }
    drawfx::gLog.clear();
    return out;
}

} // namespace

DIFF_TEST(SG_MaterialManager_Get, 0xacc040)
{
    constexpr uint32_t kStatic = 0x10f6878, kGuard = 0x10f6988, kImageVTable = 0xe78514;
    static bool built = false;
    Stands stands;

    // The original getter, its static initialised.
    int32_t *const guard = ctx.image.At<int32_t>(kGuard);
    const int32_t savedGuard = *guard;
    *guard = INT_MIN;
    const auto getter = ctx.Original<void *(__cdecl *)()>(0xacc040);
    void *const a1 = getter(), *const a2 = getter();
    *guard = savedGuard;
    ++ctx.cases;
    if (a1 != a2 || a1 != static_cast<void *>(ctx.image.At<uint8_t>(kStatic))) ctx.Fail("original getter: %p %p", a1, a2);

    // What the original constructor builds.
    std::vector<std::string> original, ours;
    {
        drawfx::Cells cells;
        GetWorld(cells);
        std::memset(gBlock, 0xcd, MM::kSize);
        ctx.Original<void *(__thiscall *)(void *)>(0xac9810)(gBlock);
        if (built) drawfx::gLog.clear(); // ours is built already: the contents only
        NoteFresh(reinterpret_cast<MM *>(gBlock));
        original = TakeLog(gBlock);
        ctx.Original<void(__thiscall *)(void *)>(0xac9ee0)(gBlock);
    }
    // Ours.
    {
        drawfx::Cells cells;
        GetWorld(cells);
        MM *const a = MM::Get();
        MM *const b = MM::Get();
        ++ctx.cases;
        if (!a || a != b) ctx.Fail("Get answered %p then %p", static_cast<void *>(a), static_cast<void *>(b));
        else if (static_cast<void *>(a) == static_cast<void *>(ctx.image.At<uint8_t>(kStatic))) ctx.Fail("Get: the image's static");
        else if (typeid(*a) != typeid(MM) || *reinterpret_cast<void *const *>(a) == static_cast<void *>(ctx.image.At<uint8_t>(kImageVTable)))
            ctx.Fail("Get's object does not have our vtable");
        else {
            NoteFresh(a);
            ours = TakeLog(a);
            if (gw.graphics.refs != 1000) ctx.Fail("Get left the graphics system counted (%d)", gw.graphics.refs);
        }
        built = true;
    }
    ++ctx.cases;
    if (ours.size() != original.size()) ctx.Fail("Get: %zu log lines, the original constructor's %zu", ours.size(), original.size());
    for (size_t i = 0; i < ours.size() && i < original.size(); ++i)
        if (ours[i] != original[i]) {
            ctx.Fail("Get: line %zu: original [%s], ours [%s]", i, original[i].c_str(), ours[i].c_str());
            break;
        }
    drawfx::ResetAll();
}
