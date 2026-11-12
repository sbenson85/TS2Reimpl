// Differential test for nTSSG::cLightingManager's slot 6 SetupLot 0x2d10b0 (lane 261110r, scenegraph LG3c rest;
// src/sg/cLightingManagerLife.cpp). Hook shape: the original runs on the same object image as ours.
//
// The world (one draw for both sides): the manager is 0x548 random bytes with its vtable a fake (slot 22, the lighting
// file search, logs the text it is handed). The lit object (+0x18; slot 5 logs; null only where the original does not
// reach it), two terrain views (+0x1c is one of them): slot 87 answers a float on the x87 stack (counts, fractions that
// round differently at 53 and 24 bits, zero, negative, NaN, infinities, tiny and huge), 83 / 84 words (small, negative,
// 0x7fffffff), 39 the world (a counted reference or null), 99 fills the string it is handed (empty, in place, on the
// heap) and answers a bool with junk above its low byte, true with no text and false with one. A drawn mask makes each of
// the five fakes move the manager's terrain word to the other view, and the slot 84 fake changes mTerrainWidth and the
// slot 39 fake mWorld before returning on some draws (the code reads the terrain again before every call and the width and
// the old world back from the manager). The references (+0x20, +0x24, +0x28) are null, the current ones or others, with
// drawn counts; the lightmaps (the system's slot 48, 0x20defb on both sides), the old lerp set (+0x228) null or a fake; the
// byte 0xffde34 is drawn (saved and restored). The two builders, SelectTimeOfDayLightSet (0x2e4480: a stand-in of this
// file, tag 15), the lerp set's Construct 0x3c0ec0 and SetKeys 0x3c2e30 are stood in on both sides (tests/
// sg_lighting_fakes.h's): each logs its receiver and words. Every fake logs the manager's members the code changes around
// it: the references (by identity), mSunMode, mSunShadows, mSunMapDirty, mDirty, mFlag81, mFlag83 and the grid words.
// The image's operator new / delete (0x824632 / 0x824624) are ours on both sides.
//
// Compared: the fakes' log, every word of the manager, the references' counts, the new lerp sets, the byte 0xffde34 and the
// heap blocks and bytes each side allocated; at the x87's 53-bit and 24-bit precision.
#include "code_patch.h"
#include "difftest.h"
#include "sg_lighting_fakes.h"

#include "rz/cRZString.h"
#include "sg/cLerpLightSet.h"
#include "sg/cLightingManager.h"

#include <cstdint>
#include <cstring>
#include <iterator>
#include <new>
#include <random>
#include <string>

namespace {

using nTSSG::cLerpLightSet;
using nTSSG::cLightingManager;

using lightfx::FakeRef;
using lightfx::gLerps;
using lightfx::gNewLerpCount;
using lightfx::gNewLerps;
using lightfx::kLerps;
namespace life = lightfx::life;

namespace lot {

constexpr uint32_t kManagerSize = 0x548;
constexpr uint32_t kFadeSwitch = 0xffde34;
constexpr int kRefs = 8;
constexpr uint32_t kSelectTag = 15; // this file's builder tag: SelectTimeOfDayLightSet

alignas(16) uint8_t gManager[kManagerSize];
FakeRef gRefs[kRefs];

// A terrain view (class not identified).
struct Terrain {
    void **vt;
    float scale;       // slot 87
    int32_t width;     // slot 83
    int32_t height;    // slot 84
    void *world;       // slot 39
    const char *text;  // slot 99 fills it
    uint32_t answer;   // slot 99's answer (junk above the low byte)
    uint32_t index;
};
Terrain gT[2];
void *gTVT[110];
uint32_t gRedirect;    // bit n: the fake of the nth call moves the manager's terrain to the other view
uint32_t gSkewWidth;   // slot 84 stores it to mTerrainWidth (0: leaves it)
void *gSkewWorld;      // slot 39 stores it to mWorld when gSkewWorldSet
bool gSkewWorldSet;

cLightingManager *Manager() { return reinterpret_cast<cLightingManager *>(gManager); }

uint32_t Id(const void *p)
{
    if (!p) return 0;
    for (int i = 0; i < kRefs; ++i)
        if (p == &gRefs[i]) return 0x100 + i;
    for (int i = 0; i < kLerps; ++i)
        if (p == &gLerps[i]) return 0x1e0 + i;
    for (uint32_t i = 0; i < gNewLerpCount && i < std::size(gNewLerps); ++i)
        if (p == gNewLerps[i]) return 0x3c00 + i;
    for (const Terrain &t : gT)
        if (p == &t) return 0x7700 + t.index;
    if (p == &life::gLit) return 0x1100;
    if (p == gManager) return 0x5000;
    return life::RzId(p);
}

void *__cdecl GameNew(size_t n) { return ::operator new(n ? n : 1); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

// The manager's members the code changes around a call, logged by every fake.
void Held()
{
    for (uint32_t at : {0x1cu, 0x20u, 0x24u, 0x28u, 0x228u})
        L(Id(reinterpret_cast<const void *>(uintptr_t(Word<uint32_t>(gManager, at)))));
    L(Word<uint32_t>(gManager, 0x40)), L(Word<uint32_t>(gManager, 0x68)), L(Word<uint32_t>(gManager, 0x80));
    L(Word<uint32_t>(gManager, 0x200));
    for (uint32_t at = 0x204; at <= 0x210; at += 4) L(Word<uint32_t>(gManager, at));
}
void AfterBuilder(uint32_t) { Held(); }

// ---- the terrain's fakes ----

void Touch(Terrain *t, uint32_t call, uint32_t slot)
{
    L(0x7700 + slot), L(Id(t));
    Held();
    if (gRedirect >> call & 1) PutWord(gManager, 0x1c, P(&gT[t->index ^ 1]));
}
float __fastcall TerrainScale(Terrain *t, void *) { return Touch(t, 0, 87), t->scale; }
int32_t __fastcall TerrainWidth(Terrain *t, void *) { return Touch(t, 1, 83), t->width; }
int32_t __fastcall TerrainHeight(Terrain *t, void *)
{
    Touch(t, 2, 84);
    if (gSkewWidth) PutWord(gManager, 0x20c, gSkewWidth);
    return t->height;
}
void *__fastcall TerrainWorld(Terrain *t, void *)
{
    Touch(t, 3, 39);
    if (gSkewWorldSet) PutWord(gManager, 0x20, P(gSkewWorld));
    return t->world;
}
uint32_t __fastcall TerrainText(Terrain *t, void *, cRZString *out)
{
    Touch(t, 4, 99);
    L(*reinterpret_cast<const uint32_t *>(out)), L(uint32_t(out->mString.size())), L(uint32_t(out->mString.capacity()));
    if (t->text) out->mString.assign(t->text);
    return t->answer;
}

// ---- the lit object's slot 5, the manager's slot 22, SelectTimeOfDayLightSet's stand-in ----

void __fastcall LitSlot5(lightfx::Plain *p, void *) { L(0x1105), L(Id(p)), Held(); }
uint32_t __fastcall ManagerSlot22(lightfx::Plain *p, void *, const char *text)
{
    L(0x2200), L(Id(p)), L(Hash(text)), L(uint32_t(std::strlen(text)));
    Held();
    return 0xdead0001u; // (the code does not read it)
}
void __fastcall SelectStand(void *self, void *) { lightfx::Builder(kSelectTag, self, 0); }

// ---- the world ----

void *DrawRef(std::mt19937 &gen, uint32_t nullOdds) { return gen() % nullOdds ? &gRefs[gen() % kRefs] : nullptr; }

float ScaleFloat(std::mt19937 &gen)
{
    static const float kEdges[] = {1.0f,  2.0f,  0.5f, 3.0f, 7.0f,    10.0f,   0.1f,   -1.0f,
                                   -3.0f, 0.0f,  -0.0f, 1e-30f, 1e30f, 1e-38f, 3e38f, 65536.0f};
    switch (gen() % 4) {
    case 0: return kEdges[gen() % std::size(kEdges)];
    case 1: return Wide(gen, 100.0f);
    case 2: return float(1 + gen() % 300) * 0.25f;
    default: return Nice(gen, 50.0f);
    }
}

int32_t Count(std::mt19937 &gen)
{
    static const int32_t kEdges[] = {0, 1, 63, 64, 255, -1, 0x7fffffff, int32_t(0x80000000), 0x7ffffffe, 1000};
    switch (gen() % 4) {
    case 0: return kEdges[gen() % std::size(kEdges)];
    case 1: return int32_t(gen());
    default: return int32_t(gen() % 300);
    }
}

const char *const kTexts[] = {"", "a", "lighting.txt", "fifteen chars..", "sixteen chars...",
                              "C:\\Program Files\\EA GAMES\\The Sims 2\\TSData\\Res\\Lighting\\neighborhood.txt"};

void Draw(uint32_t draw)
{
    std::mt19937 gen(draw * 2654435761u + 0x2d10b0u);
    for (auto &b : gManager) b = uint8_t(gen());
    PutWord(gManager, 0, P(lightfx::gManagerVT));
    for (int i = 0; i < kRefs; ++i) {
        gRefs[i] = FakeRef{};
        gRefs[i].vt = lightfx::gNodeVT, gRefs[i].id = 0x100u + i, gRefs[i].refs = gen() % 4;
    }
    *GameAddress<uint8_t>(kFadeSwitch) = gen() % 2 ? 0 : uint8_t(1 + gen() % 255);
    const bool litNull = !*GameAddress<uint8_t>(kFadeSwitch) && gen() % 4 == 0; // slot 5 is not null-checked
    PutWord(gManager, 0x18, litNull ? 0 : P(&life::gLit));
    for (uint32_t i = 0; i < 2; ++i) {
        Terrain &t = gT[i];
        t.vt = gTVT, t.index = i;
        t.scale = ScaleFloat(gen), t.width = Count(gen), t.height = Count(gen);
        t.world = DrawRef(gen, 3);
        t.text = gen() % 3 ? kTexts[gen() % std::size(kTexts)] : nullptr;
        static const uint32_t kAnswers[] = {0, 1, 0x100, 0xff01, 0x7f, 0xabcdef00u, 0x80};
        t.answer = kAnswers[gen() % std::size(kAnswers)];
    }
    PutWord(gManager, 0x1c, P(&gT[gen() % 2]));
    gRedirect = gen() % 2 ? 0 : gen() & 31;
    gSkewWidth = gen() % 3 == 0 ? uint32_t(gen() | 1) : 0;
    gSkewWorldSet = gen() % 4 == 0;
    gSkewWorld = DrawRef(gen, 3);
    PutWord(gManager, 0x20, P(DrawRef(gen, 4)));
    PutWord(gManager, 0x24, P(DrawRef(gen, 4)));
    PutWord(gManager, 0x28, P(DrawRef(gen, 4)));
    for (auto &lerp : gLerps) lerp.vt = lightfx::gLerpVT;
    PutWord(gManager, 0x228, gen() % 4 ? P(&gLerps[gen() % kLerps]) : 0);
    const uint32_t pick = gen() % 3;
    life::gLightmaps = pick == 0 ? nullptr
                       : pick == 1 ? reinterpret_cast<void *>(uintptr_t(Word<uint32_t>(gManager, 0x24)))
                                   : DrawRef(gen, 4);
    gNewLerpCount = 0;
}

// The scene manager the call is given: null, the current one or a drawn one.
void *Argument(uint32_t draw)
{
    std::mt19937 gen(draw * 40503u + 0x2d10b0u);
    const uint32_t pick = gen() % 3;
    if (pick == 0) return nullptr;
    if (pick == 1) return reinterpret_cast<void *>(uintptr_t(Word<uint32_t>(gManager, 0x28)));
    return &gRefs[gen() % kRefs];
}

Blob Snapshot(const SavedGlobals &globals, long long blocks, long long bytes)
{
    Blob o = gLog;
    o.push_back(0xeeee);
    for (uint32_t i = 0; i < kManagerSize; i += 4) {
        const uint32_t w = Word<uint32_t>(gManager, i);
        const bool pointer = i == 0x1c || i == 0x20 || i == 0x24 || i == 0x28 || i == 0x228;
        o.push_back(pointer ? Id(reinterpret_cast<const void *>(uintptr_t(w))) : w);
    }
    for (const FakeRef &r : gRefs) o.push_back(r.refs);
    o.push_back(gNewLerpCount);
    globals.Append(o);
    o.push_back(uint32_t(blocks)), o.push_back(uint32_t(bytes));
    return o;
}

struct Stands {
    lightfx::Builders builders;
    shape::StandIn select{0x2e4480u, &cLightingManager::SelectTimeOfDayLightSet, &SelectStand};
    shape::CodePatch system{0x20defbu, shape::CodeOf(&lightfx::StandSystem)};
    shape::StandIn construct{0x3c0ec0u, &cLerpLightSet::Construct, &lightfx::LerpConstruct};
    shape::StandIn setKeys{0x3c2e30u, &cLerpLightSet::SetKeys, &lightfx::LerpSetKeys};
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
};

struct Scope {
    svcfx::AllocatorScope allocator;
    SavedGlobals globals;
    Stands stands;
    Scope()
    {
        globals.Save(kFadeSwitch, 1);
        lightfx::FillShared();
        lightfx::gManagerVT[22] = reinterpret_cast<void *>(&ManagerSlot22);
        life::gLitVT[5] = reinterpret_cast<void *>(&LitSlot5);
        gTVT[87] = reinterpret_cast<void *>(&TerrainScale);
        gTVT[83] = reinterpret_cast<void *>(&TerrainWidth);
        gTVT[84] = reinterpret_cast<void *>(&TerrainHeight);
        gTVT[39] = reinterpret_cast<void *>(&TerrainWorld);
        gTVT[99] = reinterpret_cast<void *>(&TerrainText);
        lightfx::gIdOf = &Id;
        lightfx::gOnRef = &Held, lightfx::gAfterBuilder = &AfterBuilder;
        gLog.reserve(1 << 14);
    }
    ~Scope() { lightfx::gOnRef = nullptr, lightfx::gAfterBuilder = nullptr; }
};

} // namespace lot

} // namespace

// 0x2d10b0 slot 6: see the header comment.
DIFF_TEST(sg_lighting_load_setup, 0x2d10b0)
{
    auto original = ctx.Original<void(__thiscall *)(void *, void *)>(0x2d10b0);
    lot::Scope scope;
    SidesAtBothPrecisions(ctx, "setup lot", 3000, true, [&](uint32_t draw, int side) {
        lot::Draw(draw);
        void *const sceneManager = lot::Argument(draw);
        gLog.clear();
        ctx.Mark(side ? "ours" : "original");
        difftest::AllocationCounter heap;
        if (side == 0) original(lot::gManager, sceneManager);
        else lot::Manager()->SetupLot(sceneManager);
        heap.Stop();
        Blob outcome = lot::Snapshot(scope.globals, heap.Blocks(), heap.Bytes());
        for (uint32_t i = 0; i < gNewLerpCount && i < std::size(gNewLerps); ++i) ::operator delete(gNewLerps[i]);
        gNewLerpCount = 0;
        return outcome;
    });
}
