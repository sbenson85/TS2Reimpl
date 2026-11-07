// Differential tests of the fog light cLinearFogLight and the draw-state light cDrawStateLight (lane OT, scenegraph N2;
// src/sg/cLinearFogLight.h, src/sg/cDrawStateLight.h): the game's lights (factories 0xa47c30 / 0xa47b70, constructors
// 0xa45fc0 / 0xa45e20, their vtables and the bodies behind them) against ours (Create, our vtables), each side building
// its own lights (of both classes, at random) with its own factories. The tests follow tests/sg_lights_standard.cpp (the
// common levels are checked there): the calls the lights make on the fakes (sgfx.h), the notes, every light's words but its
// vtable words, its vbptr and its referrer vector's three pointers (words 0, 7, 11, 13, the virtual base's vtable and the
// referrer vector 16-18; the vbtable's entries, the vector's size and capacity and the referrers by id are compared
// instead), the padding at +0x2a (word 10's upper half) and, for the draw-state light, the padding at +0x4e (word 19's
// upper half: its constructor stores one 16-bit word), and the blocks freed when the test lets go of the lights.
//
//   SG_FogLight_Factory / SG_DrawStateLight_Factory  new lights: their words, vbtables, counts.
//   SG_FogDrawStateLights_Slots    random operations through every face of random lights: QI with 11 iids, counts, the
//                                  fog's distances and colour, the flags, the constant slots (type, bound, strength at
//                                  points and over boxes), referrers, kinds, the resource part, type info, Write, clones.
//   SG_FogDrawStateLights_Persist  Write, then Read into a fresh light or a clone through either persist face.
//   SG_FogDrawStateLights_Events   connection managers in the two instantiations' cells (0x10f6148 / 0x10f614c): the
//                                  registry of events, connections and notifications to listeners and referrers.
//
// The class bodies of the game these tests run on the original side: the constructors 0xa45fc0 / 0xa45e20, destructors
// 0xa46700 / 0xa46670 (deleting 0xa46b20 / 0xa46ad0, node face thunks 0xa46862 / 0xa46852), QueryInterface 0xa489e0 /
// 0xa48900 (and the thunks 0xa48a29 / 0xa48a31 / 0xa489c9 and the draw state's), IsKindOf 0xa4a5b0 / 0xa4a570, Clone
// 0xa47280 / 0xa47180 and copy 0xa47f20 / 0xa47ed0, the fog's SetColor 0xa4a1a0, Read 0xa48d50 (shared), type info 0xa4aa20
// / 0xa4a870, Write 0xa4b060 / 0xa4aee0, record 19 0xa48176 / 0xa48156, type 0xa4a5f0, the constant strengths 0xa49500 /
// 0xa49510 (the draw state's share 0x3a5f40 / 0x3a5f50); the light template's, the referent node's and the object-graph
// node's bodies listed in tests/sg_lights_standard.cpp; the behaviour's 0xa47530 / 0xa474b0 (event manager cells), 0xa47e00
// / 0xa47dc0, 0xa47470 / 0xa47450, 0xa47650 / 0xa47630, 0xa46ec0 and the registry 0x3a5d00 (ours, stood over the planted map on both sides).
//
// Lane TC added the environment cube light cEnvironmentCubeLight (src/sg/cEnvironmentCubeLight.h) as a third kind, with a
// texture manager fake in 0xae8750's cache 0x10f8f94 (its handles' ids counted there) and its own connection manager cell
// 0x10f6144. SG_EnvironmentCubeLight_Factory is its factory test (0xa47bd0); the others run it among the fog and draw-state
// lights: SetTexture, SetColor / GetColor, the version-1 and version-2 reads. The cube's game bodies they run: the
// constructor 0xa45e80, destructor 0xa466b0 (deleting 0xa46af0, node face thunk 0xa4685a), QueryInterface 0xa48970 (thunks
// 0xa489b9 / 0xa489c1 / 0xa48959), IsKindOf 0xa4a590, Clone 0xa47200 and copy 0xa47ef0, SetTexture 0xa4a520, SetColor
// 0xa4a090, GetColor 0xa47420, Read 0xa48dc0 (virtual-base thunk 0xa48db1), type info 0xa4a940 (0xa4a926), Write 0xa4af60
// (0xa4af4e), record 19 0xa48166, the strengths 0x3c9080 / 0x3c9090 (shared), the light template's copies 0xa494a0 /
// 0xa494a8 / 0xa494b0 / 0xa49490 (Release), the referent copies 0xa49ed0 / 0xa47a70 and the behaviour's 0xa475b0, 0xa47e40,
// 0xa47490, 0xa47670, 0xa49390, 0xa49cc0, 0xa478d0.
//
// Lane TC: the three lights' other game bodies, in effect through the factories. Run on the original side: the class
// and template-level bodies the faces reach, the destructor chains, the adjustor thunks. Never run, by the tests or
// the game: the template levels' deleting destructors (and their thunks), held only by the levels' own vtables. All:
// 0xa45940, 0xa459c0, 0xa45ac0, 0xa45b40, 0xa45ba0, 0xa45c60, 0xa45cc0, 0xa45ce0, 0xa45d20, 0xa46330, 0xa46380,
// 0xa46420, 0xa46470, 0xa464b0, 0xa46530, 0xa46570, 0xa46590, 0xa465d0, 0xa46822, 0xa4682a, 0xa4683a, 0xa46910,
// 0xa46930, 0xa46970, 0xa46990, 0xa469b0, 0xa469f0, 0xa46a10, 0xa46a30, 0xa46a70, 0xa46bf2, 0xa46c21, 0xa479f0,
// 0xa47a30, 0xa48050, 0xa48087, 0xa48122, 0xa481d2, 0xa488e9, 0xa48949, 0xa48951, 0xa48d41, 0xa48f10, 0xa491f7,
// 0xa493dc, 0xa493e7, 0xa49400, 0xa49408, 0xa49410, 0xa49418, 0xa49423, 0xa49430, 0xa49438, 0xa49440, 0xa49485,
// 0xa494c0, 0xa494c5, 0xa494d0, 0xa494d5, 0xa494f0, 0xa494f5, 0xa49a1b, 0xa49a5d, 0xa49a89, 0xa49ac0, 0xa49db0,
// 0xa49de0, 0xa49e40, 0xa49e70, 0xa49ea0, 0xa49f20, 0xa4a156, 0xa4a1d7, 0xa4a283, 0xa4a2ba, 0xa4a506, 0xa4a858,
// 0xa4aa11, 0xa4aece, 0xa4b052.
#include "code_patch.h"
#include "difftest.h"
#include "gz_registry_fx.h"
#include "game/game.h"
#include "misc/SmallVirtuals.h" // nGZEvent::cNullEvent::kVTable
#include "sgfx.h"

#include "sg/cDrawStateLight.h"
#include "sg/cEnvironmentCubeLight.h"
#include "sg/cLinearFogLight.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;

constexpr uint32_t kFogFactory = 0xa47c30, kDrawFactory = 0xa47b70, kEnvFactory = 0xa47bd0;
constexpr uint32_t kFogSize = 0x68, kDrawSize = 0x58, kEnvSize = 0x98;
constexpr uint32_t kNode = 0x2c;
constexpr uint32_t kVbptr = 0x1c;
constexpr uint32_t kLightTag = 0x7d000000;
constexpr unsigned kMax = 10;
constexpr unsigned kSeeds = 64;

// A real event body (a cNullEvent): the listeners note a body through its own slots, which a clone context fake would
// answer with whatever its caller left in the arguments.
void *NullEventBody()
{
    static const void *body;
    body = GameAddress<const void>(nGZEvent::cNullEvent::kVTable);
    return &body;
}
constexpr uint64_t kSkipCommon = (1u << 0) | (1u << 7) | (1u << 10) | (1u << 11) | (1u << 13) | (1u << 16) | (1u << 17) |
                                 (1u << 18);
constexpr unsigned kFogBaseWord = 0x64 / 4, kDrawBaseWord = 0x54 / 4, kEnvBaseWord = 0x94 / 4;

// The texture manager (a fake in 0xae8750's cache 0x10f8f94) the environment cube's handles count their ids in:
//   17 FindOrLoad(name): an id per name (none for "missing")     18 AddRefId(id)     19 ReleaseId(id)
//   24 GetInfo(out, id): {the name's text, a count} for a known id, {0, 0} otherwise
using drawfx::Ret;
using drawfx::Slot;
constexpr auto kMgrSlots = [] {
    std::array<Slot, 25> a{};
    for (Slot &s : a) s = {0, Ret::Int, false, "", ""};
    a[0] = {2, Ret::Int, true, "QueryInterface", "ui"};
    a[1] = {0, Ret::Int, true, "AddRef", ""};
    a[2] = {0, Ret::Int, true, "Release", ""};
    a[17] = {1, Ret::Int, true, "FindOrLoad", "s"};
    a[18] = {1, Ret::Int, true, "AddRefId", "u"};
    a[19] = {1, Ret::Int, true, "ReleaseId", "u"};
    a[24] = {2, Ret::Int, true, "GetInfo", "iu"};
    return a;
}();
constexpr drawfx::Iface kMgr{"TextureManager", 25, kMgrSlots.data()};
const char *const kTextures[] = {"env_a", "env_b", "missing", ""};
constexpr uint32_t kTextureIds[] = {5, 9, 0xffffffffu, 0xffffffffu};
uint32_t FindOrLoad(Fake &, const uint32_t *args)
{
    const char *name = reinterpret_cast<const char *>(uintptr_t(args[0]));
    for (unsigned i = 0; i < 4; ++i)
        if (!std::strcmp(name, kTextures[i])) return kTextureIds[i];
    return 0xffffffffu;
}
uint32_t GetInfo(Fake &, const uint32_t *args)
{
    auto *out = reinterpret_cast<uint32_t *>(uintptr_t(args[0]));
    out[0] = args[1] == 5 ? P(kTextures[0]) : args[1] == 9 ? P(kTextures[1]) : 0;
    out[1] = args[1] == 5 || args[1] == 9 ? 3 : 0;
    return args[0];
}

// The shared map 0x3a5d00 builds (guard 0x10da034) and its holder (once byte 0x10da03c, guard 0x10da044).
constexpr sgfx::RegistryStatics kSimpleRegistry{0x10da02c, 0x10da034, 0x10da03c, 0x10da044};

// 0x3a5d00 is ours now: both sides stand over the planted map (as the original runs over it).
struct SimpleStand {
    shape::StandIn stand{0x3a5d00, &nGZSceneGraph::cSimpleLightBehaviorTraits::RegistryFor,
                         &pdfx::StandRegistry<0x10da02c>};
};

enum Kind : uint8_t { kFog, kDraw, kEnv };
using Factory = cILight *(__cdecl *)();
cILight *__cdecl MakeFog() { return cLinearFogLight::Create(); }
cILight *__cdecl MakeDraw() { return cDrawStateLight::Create(); }
cILight *__cdecl MakeEnv() { return cEnvironmentCubeLight::Create(); }

struct Makers {
    Factory fog, draw, env;
    Factory For(Kind k) const { return k == kFog ? fog : k == kDraw ? draw : env; }
};
Makers MakersFor(difftest::Context &ctx, bool original)
{
    if (original)
        return {ctx.Original<Factory>(kFogFactory), ctx.Original<Factory>(kDrawFactory), ctx.Original<Factory>(kEnvFactory)};
    return {&MakeFog, &MakeDraw, &MakeEnv};
}

uint32_t SizeOf(Kind k) { return k == kFog ? kFogSize : k == kDraw ? kDrawSize : kEnvSize; }
unsigned BaseWordOf(Kind k) { return k == kFog ? kFogBaseWord : k == kDraw ? kDrawBaseWord : kEnvBaseWord; }
uint8_t *Object(cILight *n) { return reinterpret_cast<uint8_t *>(n) - kNode; }
cIGZPersistResource *PartFace(cILight *n) { return reinterpret_cast<cIGZPersistResource *>(Object(n)); }
cIGZPersistSerializableReferent *PersistFace(cILight *n) { return n->GetReferent(); }
uint32_t CountOf(cILight *n) { return reinterpret_cast<const uint32_t *>(n)[1]; }

struct World {
    cILight *lights[kMax];
    Kind kinds[kMax];
    unsigned count = 0;
    Fake ext[3];
    Fake referrer[3];
    Fake context;
    Fake textures;
    sgfx::Manager manager[3];
};
World w;

void Build(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 rng(seed);
    sgfx::gPool.Reset(cells);
    sgfx::gPool.Handle("alpha");
    sgfx::gPool.Handle("sun_lght");
    sgfx::gReader.Reset();
    sgfx::gWriter.Reset();
    sgfx::gWriter.fake.Returns(23, 0x2300u + seed);
    sgfx::gResources.Reset(cells);
    for (unsigned i = 0; i < 3; ++i) sgfx::MakeExtension(w.ext[i], 200 + i, rng() % 2);
    for (unsigned i = 0; i < 3; ++i) {
        w.referrer[i].Reset<sgfx::kBehavior>(600 + i);
        w.referrer[i].Returns(4, uint32_t(rng() % 2));
    }
    w.textures.Reset<kMgr>(700);
    w.textures.On(17, &FindOrLoad);
    w.textures.On(24, &GetInfo);
    cells.Set(0x10f8f94, static_cast<const void *>(&w.textures)); // 0xae8750's cache
    w.count = 0;
    for (sgfx::Manager &m : w.manager) m.Drop();
    w.manager[0].Use(cells, 0x10f6148); // 0xa47530's cache
    w.manager[1].Use(cells, 0x10f614c); // 0xa474b0's cache
    w.manager[2].Use(cells, 0x10f6144); // 0xa475b0's cache
}

void DropManagers()
{
    for (sgfx::Manager &m : w.manager) m.Drop();
}

bool Hold(cILight *n, Kind kind, bool counted)
{
    if (w.count >= kMax) {
        if (counted) n->Release();
        return false;
    }
    drawfx::AddRange(Object(n), SizeOf(kind), kLightTag | (w.count << 8));
    if (!counted) n->AddRef();
    w.kinds[w.count] = kind;
    w.lights[w.count++] = n;
    return true;
}

cILight *Make(const Makers &m, std::mt19937 &rng)
{
    const Kind kind = Kind(rng() % 3);
    cILight *n = m.For(kind)();
    return Hold(n, kind, false) ? n : nullptr;
}

void MakeLights(const Makers &m, std::mt19937 &rng)
{
    const unsigned n = 1 + rng() % 5;
    for (unsigned i = 0; i < n; ++i) Make(m, rng);
}

void NoteLights(const char *label)
{
    for (unsigned i = 0; i < w.count; ++i) {
        const uint32_t *words = reinterpret_cast<const uint32_t *>(Object(w.lights[i]));
        const bool draw = w.kinds[i] == kDraw;
        const uint64_t skip = kSkipCommon | (uint64_t(1) << BaseWordOf(w.kinds[i]));
        drawfx::Call c;
        c.note = label;
        c.words.push_back(i);
        for (unsigned k = 0; k < SizeOf(w.kinds[i]) / 4; ++k) {
            if (skip >> k & 1) continue;
            // The draw state's word 19 holds the two flags and two bytes of padding.
            c.words.push_back(drawfx::Norm(draw && k == 0x4c / 4 ? words[k] & 0xffff : words[k]));
        }
        c.words.push_back(words[10] & 0xffff);
        const int32_t *vbtable = *reinterpret_cast<const int32_t *const *>(Object(w.lights[i]) + kVbptr);
        c.words.push_back(uint32_t(vbtable[0]));
        c.words.push_back(uint32_t(vbtable[1]));
        const auto *first = reinterpret_cast<Fake *const *>(uintptr_t(words[16]));
        const auto *last = reinterpret_cast<Fake *const *>(uintptr_t(words[17]));
        const auto *end = reinterpret_cast<Fake *const *>(uintptr_t(words[18]));
        c.words.push_back(first != nullptr);
        c.words.push_back(uint32_t(last - first));
        c.words.push_back(uint32_t(end - first));
        for (auto *p = first; p && p != last; ++p) c.words.push_back((*p)->id);
        drawfx::gLog.push_back(std::move(c));
    }
}

void ReleaseAll()
{
    difftest::FreeRecorder freed;
    for (unsigned i = 0; i < w.count; ++i) w.lights[i]->Release();
    w.count = 0;
    freed.Stop();
    size_t sizes[64];
    const size_t n = freed.SortedSizes(sizes, 64);
    drawfx::Call c;
    c.note = "freed";
    for (size_t i = 0; i < n; ++i) c.words.push_back(uint32_t(sizes[i]));
    drawfx::gLog.push_back(std::move(c));
    for (const Fake &e : w.ext) drawfx::Note("extension", {e.id, uint32_t(e.refs), drawfx::Norm(e.value[0]), drawfx::Norm(e.value[1])});
    for (const Fake &r : w.referrer) drawfx::Note("referrer", {r.id, uint32_t(r.refs)});
    drawfx::Note("texture manager refs", uint32_t(w.textures.refs));
    sgfx::gPool.NoteCounts();
    sgfx::gResources.NoteRefs();
}

cIGZUnknown *Segment(std::mt19937 &rng)
{
    const unsigned k = rng() % 4;
    return k < 3 ? reinterpret_cast<cIGZUnknown *>(&sgfx::gResources.segments[k]) : nullptr;
}

uint32_t Bits(float f) { return std::bit_cast<uint32_t>(f); }

float Value(std::mt19937 &rng)
{
    const float values[] = {0.0f, -0.0f, 1.0f, 0.25f, 10.0f, 100.0f, -3.0f, 1e-40f, 2500.0f, std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity()};
    return values[rng() % 11];
}

float Coord(std::mt19937 &rng)
{
    const float values[] = {0.0f, -0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 2.0f, -2.0f, 10.0f, -10.0f, 1e-3f, -100.0f, 3.5f, -0.1f};
    return values[rng() % 14];
}

void NoteFloats(const char *label, const float *f, unsigned n)
{
    drawfx::Call c;
    c.note = label;
    for (unsigned i = 0; i < n; ++i) c.words.push_back(Bits(f[i]));
    drawfx::gLog.push_back(std::move(c));
}
void NoteFloats(const char *label, std::initializer_list<float> f) { NoteFloats(label, f.begin(), unsigned(f.size())); }

void CloneOf(unsigned index, std::mt19937 &rng)
{
    cILight *n = w.lights[index];
    const Kind kind = w.kinds[index];
    const uint32_t iids[] = {cILinearFogLight::kIid, cIDrawStateLight::kIid, cITextureLight::kIid, cILight::kIid,
                             sgfx::kIidGraph, 0x12345678};
    const uint32_t mode = rng() % 3, iid = iids[rng() % 6];
    sgfx::MakeCloneContext(w.context, nullptr);
    void *out = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
    n->Clone(mode, iid, &out, rng() % 2 ? reinterpret_cast<cINodeCloneContext *>(&w.context) : nullptr);
    const bool answered = out != reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
    drawfx::Note("clone", {mode, iid, uint8_t(answered), answered && out ? CountOf(static_cast<cILight *>(out)) : 0});
    if (answered && out) {
        cILight *clone = static_cast<cILight *>(out);
        if (Hold(clone, kind, true)) drawfx::Note("clone held", w.count - 1);
    }
}

AffineTransform3 RandomTransform(std::mt19937 &rng)
{
    AffineTransform3 t{};
    const float values[] = {0.0f, 1.0f, -1.0f, 0.5f, 0.866f, -0.866f, 2.0f};
    for (unsigned i = 0; i < 9; ++i) t.mMatrix[i] = values[rng() % 7];
    for (float &o : t.mOffset) o = Coord(rng);
    t.mUnknown0 = rng();
    return t;
}

void NoteStrengths(cILight *n, std::mt19937 &rng)
{
    const float p[3] = {Coord(rng), Coord(rng), Coord(rng)};
    NoteFloats("strength at", {n->StrengthAt(p)});
    float box[6];
    for (float &b : box) b = Coord(rng);
    AffineTransform3 t = RandomTransform(rng);
    NoteFloats("strength over", {n->StrengthOverBox(box, &t)});
}

void Operate(difftest::Context &ctx, const Makers &m, int k, std::mt19937 &rng)
{
    const unsigned index = rng() % w.count;
    cILight *n = w.lights[index];
    const Kind kind = w.kinds[index];
    cISGResource *face = n->GetResource();
    const unsigned op = rng() % 17;
    drawfx::Note("op", op);
    char step[32];
    std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
    sgfx::MarkStep(ctx, step);
    switch (op) {
    case 0: { // QueryInterface through any face
        const uint32_t iids[] = {cILinearFogLight::kIid, cIDrawStateLight::kIid, cILight::kIid, cIReferentNode::kIid,
                                 sgfx::kIidGraph, 0x5b0aaa74, 1, 0x3b253dc7, cISGResource::kIid, cGZPersistResource::kIid,
                                 cIStandardLight::kIid, 0x12345678, cITextureLight::kIid};
        const uint32_t iid = iids[rng() % 13];
        const unsigned via = rng() % 4;
        cIGZUnknown *on = via == 0   ? static_cast<cIGZUnknown *>(n)
                          : via == 1 ? static_cast<cIGZUnknown *>(PersistFace(n))
                          : via == 2 ? static_cast<cIGZUnknown *>(PartFace(n))
                                     : static_cast<cIGZUnknown *>(face);
        void *out = nullptr;
        const uint32_t before = CountOf(n);
        const bool r = on->QueryInterface(iid, &out);
        drawfx::Note("query", {via, iid, uint8_t(r), drawfx::Norm(P(out)), CountOf(n) - before});
        if (out && CountOf(n) != before) static_cast<cIGZUnknown *>(out)->Release();
        break;
    }
    case 1: { // AddRef / Release through any face
        const unsigned via = rng() % 4;
        cIGZUnknown *on = via == 0   ? static_cast<cIGZUnknown *>(n)
                          : via == 1 ? static_cast<cIGZUnknown *>(PersistFace(n))
                          : via == 2 ? static_cast<cIGZUnknown *>(PartFace(n))
                                     : static_cast<cIGZUnknown *>(face);
        const uint32_t a = on->AddRef();
        const uint32_t c = PartFace(n)->GetRefCount();
        const uint32_t r = on->Release();
        drawfx::Note("counts", {via, a, c, r, face->GetPersistResource()->GetRefCount()});
        break;
    }
    case 2: { // the first setter: the fog's distances, the draw state's first flag, the cube's texture
        if (kind == kEnv) {
            static_cast<cITextureLight *>(n)->SetTexture(kTextures[rng() % 4]);
            drawfx::Note("texture", Object(n)[0x4c] | Object(n)[0x4d] << 8 | Object(n)[0x4e] << 16 | Object(n)[0x4f] << 24);
        } else if (kind == kFog) {
            auto *fog = static_cast<cILinearFogLight *>(n);
            fog->SetNearFar(Value(rng), Value(rng));
            const float *nf = fog->GetNearFar();
            NoteFloats("near far", nf, 2);
            drawfx::Note("at", drawfx::Norm(P(nf)));
        } else {
            auto *draw = static_cast<cIDrawStateLight *>(n);
            draw->SetFirstFlag(uint8_t(rng() % 3 == 0 ? rng() : rng() % 2));
            drawfx::Note("flags", {draw->GetFlags()[0], draw->GetFlags()[1], drawfx::Norm(P(draw->GetFlags()))});
        }
        break;
    }
    case 3: { // the second setter: the fog's colour, the draw state's second flag, a cube colour
        if (kind == kEnv) {
            auto *env = static_cast<cITextureLight *>(n);
            const float rgba[4] = {Value(rng), Value(rng), Value(rng), Value(rng)};
            env->SetColor(rng() % 4, rgba);
            float out[4] = {7, 7, 7, 7};
            const float *r = env->GetColor(out, rng() % 4);
            NoteFloats("cube colour", out, 4);
            drawfx::Note("returned", uint8_t(r == out));
        } else if (kind == kFog) {
            auto *fog = static_cast<cILinearFogLight *>(n);
            const float rgb[3] = {Value(rng), Value(rng), Value(rng)};
            fog->SetColor(rgb);
        } else {
            auto *draw = static_cast<cIDrawStateLight *>(n);
            draw->SetSecondFlag(uint8_t(rng() % 3 == 0 ? rng() : rng() % 2));
            drawfx::Note("flags", {draw->GetFlags()[0], draw->GetFlags()[1]});
        }
        break;
    }
    case 4: { // the constant slots
        drawfx::Note("light", {n->GetLightType(), drawfx::Norm(P(n->GetBoundShape())), drawfx::Norm(P(n->GetStandardLight())),
                               drawfx::Norm(P(n->GetStandardLightConst()))});
        NoteStrengths(n, rng);
        break;
    }
    case 5: { // referrers
        Fake &r = w.referrer[rng() % 3];
        auto *b = reinterpret_cast<nGZEvent::cIAbstractBehavior *>(&r);
        bool listed = false;
        const uint32_t *words = reinterpret_cast<const uint32_t *>(Object(n));
        for (auto *p = reinterpret_cast<Fake *const *>(uintptr_t(words[16]));
             p && p != reinterpret_cast<Fake *const *>(uintptr_t(words[17])); ++p)
            listed |= *p == &r;
        if (listed && rng() % 2)
            n->RemoveReferrer(b);
        else
            n->AddReferrer(b);
        drawfx::Note("referrers", n->GetReferrerCount());
        break;
    }
    case 6: { // notifications and queries
        const uint32_t events[] = {cIStandardLight::kChangedEvent, cIStandardLight::kBoundChangedEvent, 0x1234};
        n->NotifyListeners(events[rng() % 3], rng() % 3, rng() % 2 ? NullEventBody() : nullptr);
        drawfx::Note("asked", uint8_t(n->AskListeners(events[rng() % 3], rng() % 3, nullptr)));
        break;
    }
    case 7: { // kinds
        const uint32_t iids[] = {cILinearFogLight::kIid, cIDrawStateLight::kIid, cILight::kIid, cIReferentNode::kIid,
                                 sgfx::kIidGraph, cISGResource::kIid, 0x3b253dc7, 1, 0, cIStandardLight::kIid, 0xac06a66f,
                                 cITextureLight::kIid};
        for (uint32_t iid : iids) drawfx::Note("kind", {iid, uint8_t(n->IsKindOf(iid))});
        break;
    }
    case 8: { // the virtual base
        switch (rng() % 4) {
        case 0: drawfx::Note("set segment", uint8_t(face->SetSegment(Segment(rng)))); break;
        case 1: face->SetFlag(uint8_t(rng())); break;
        case 2: drawfx::Note("assign", face->AssignAndSubmit(rng() % 2 ? "sun_lght" : "beta", Segment(rng))); break;
        default: drawfx::Note("reset", face->ResetAndSubmit(rng(), rng())); break;
        }
        sgfx::NoteText("resource name", face->GetName());
        sgfx::NoteText("record 18", static_cast<const char *>(face->GetRecord18()));
        sgfx::NoteText("record 19", static_cast<const char *>(face->GetRecord19()));
        break;
    }
    case 9: { // a type-info record, through either persist face
        cPersistTypeInfo info = rng() % 2 ? PersistFace(n)->GetTypeInfo() : face->GetTypeInfo();
        sgfx::NoteTypeInfo("type", &info);
        if (info.mName) info.mName->Release();
        break;
    }
    case 10: // written, through either persist face
        if (rng() % 2)
            PersistFace(n)->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        else
            face->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        break;
    case 11:
    case 12: CloneOf(index, rng); break;
    case 13: // a new light
        Make(m, rng);
        break;
    case 14: { // owner info, extensions and the tag
        drawfx::Note("owner info", {drawfx::Norm(PersistFace(n)->GetOwnerInfo()), drawfx::Norm(face->GetOwnerInfo())});
        Fake &e = w.ext[rng() % 3];
        auto *ext = reinterpret_cast<cIExtension *>(&e);
        if (n->HasExtension(ext))
            n->RemoveExtension(ext);
        else if (!e.value[sgfx::kExtOwnerWord])
            n->AddExtension(ext);
        drawfx::Note("extensions", {drawfx::Norm(P(n->GetFirstExtension())), uint8_t(n->HasExtension(ext))});
        if (rng() % 2) n->SetTag("alpha");
        sgfx::NoteText("tag", n->GetTag());
        break;
    }
    case 15: { // an event to the light itself
        n->OnEvent(rng() % 2 ? 0x5b51e806u : 0x777u, 0, nullptr, 0);
        drawfx::Note("active", n->IsActive());
        break;
    }
    default: // strengths only
        NoteStrengths(n, rng);
        break;
    }
}

struct MissingRecordScope {
    static constexpr uint32_t kAt = 0x106ab68; // slot 14's record for an unknown event
    uint32_t saved[6];
    nGZEvent::cEventAttribute savedOurs = nGZEvent::behavior_mt::gMissingRecord;
    MissingRecordScope()
    {
        std::memcpy(saved, GameAddress<void>(kAt), sizeof saved);
        for (uint32_t i = 0; i < 6; ++i) {
            GameAddress<uint32_t>(kAt)[i] = 0x5ec0de00 + i;
            reinterpret_cast<uint32_t *>(&nGZEvent::behavior_mt::gMissingRecord)[i] = 0x5ec0de00 + i; // ours reads its own record
        }
    }
    ~MissingRecordScope() { std::memcpy(GameAddress<void>(kAt), saved, sizeof saved);
        nGZEvent::behavior_mt::gMissingRecord = savedOurs;
    }
};

void FactoryBody(difftest::Context &ctx, Kind kind)
{
    sgfx::RegistryScope registry(kSimpleRegistry);
    SimpleStand simpleStand;
    sgfx::CheckSides(ctx, "factory", 8, Build, [&](bool original, std::mt19937 &) {
        const Makers m = MakersFor(ctx, original);
        cILight *a = m.For(kind)();
        cILight *b = m.For(kind)();
        drawfx::Note("counts", {CountOf(a), CountOf(b)});
        Hold(a, kind, false);
        Hold(b, kind, false);
        NoteLights("new light");
        ReleaseAll();
    });
    DropManagers();
}

} // namespace

DIFF_TEST(SG_FogLight_Factory, kFogFactory) { FactoryBody(ctx, kFog); }

DIFF_TEST(SG_DrawStateLight_Factory, kDrawFactory) { FactoryBody(ctx, kDraw); }

DIFF_TEST(SG_EnvironmentCubeLight_Factory, kEnvFactory) { FactoryBody(ctx, kEnv); }

DIFF_TEST(SG_FogDrawStateLights_Slots, kFogFactory)
{
    sgfx::RegistryScope registry(kSimpleRegistry);
    SimpleStand simpleStand;
    sgfx::CheckSides(ctx, "slots", kSeeds, Build, [&](bool original, std::mt19937 &rng) {
        const Makers m = MakersFor(ctx, original);
        MakeLights(m, rng);
        NoteLights("built");
        for (int k = 0; k < 24; ++k) Operate(ctx, m, k, rng);
        NoteLights("after");
        sgfx::gReader.NoteNames();
        ReleaseAll();
    });
    DropManagers();
}

DIFF_TEST(SG_FogDrawStateLights_Persist, kFogFactory)
{
    sgfx::RegistryScope registry(kSimpleRegistry);
    SimpleStand simpleStand;
    sgfx::CheckSides(ctx, "persist", kSeeds, Build, [&](bool original, std::mt19937 &rng) {
        const Makers m = MakersFor(ctx, original);
        MakeLights(m, rng);
        const unsigned source = rng() % w.count;
        cILight *n = w.lights[source];
        if (rng() % 2) n->SetTag("alpha");
        if (rng() % 2) n->AddExtension(reinterpret_cast<cIExtension *>(&w.ext[0]));
        if (rng() % 2) n->GetResource()->AssignAndSubmit("sun_lght", nullptr);
        {
            // The type-info name each Write releases after WriteTypeInfo is visible only as a free.
            difftest::FreeRecorder freed;
            if (rng() % 2)
                PersistFace(n)->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
            else
                n->GetResource()->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
            freed.Stop();
            drawfx::Note("write frees", uint32_t(freed.Count()));
        }

        // Read into a fresh light (or a clone of a held one), through either persist face.
        unsigned target = ~0u;
        if (rng() % 3 == 0) {
            const unsigned before = w.count;
            CloneOf(rng() % w.count, rng);
            if (w.count > before) target = w.count - 1;
        }
        drawfx::Note("read into", {target != ~0u});
        if (target == ~0u) {
            if (!Make(m, rng)) return;
            target = w.count - 1;
        }
        cILight *fresh = w.lights[target];
        sgfx::Reader &r = sgfx::gReader;
        r.Version(1);          // the leaf's record
        r.Version(rng() % 3);  // cLightT's
        r.Version(rng() % 3);  // cSGResource's
        r.Text(rng() % 2 ? "sun_lght" : "beta");
        r.Version(1);          // the referent Impl's
        r.Version(4);          // the object-graph Impl's
        const uint32_t extensions = rng() % 2;
        r.Word(extensions);
        if (extensions) r.Ref(&w.ext[1]);
        r.Text(rng() % 2 ? "alpha" : "");
        if (w.kinds[target] == kEnv) {
            r.versions[0] = 1 + rng() % 2; // the leaf's record: version 1 has no colours
            r.Text(kTextures[rng() % 4]);
            for (unsigned i = 0; i < 16; ++i) r.Float(Value(rng));
        }
        if (rng() % 2)
            PersistFace(fresh)->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
        else
            fresh->GetResource()->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
        drawfx::Note("read", {r.wordAt, r.refAt, r.textAt, r.versionAt});
        NoteLights("after");
        sgfx::gReader.NoteNames();
        PersistFace(fresh)->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        ReleaseAll();
    });
    DropManagers();
}

DIFF_TEST(SG_FogDrawStateLights_Events, kFogFactory)
{
    sgfx::RegistryScope registry(kSimpleRegistry);
    SimpleStand simpleStand;
    MissingRecordScope missing;
    static Fake listeners[2];
    auto build = [&](uint32_t seed, drawfx::Cells &cells) {
        Build(seed, cells);
        sgfx::MakeListener(listeners[0], 500, true);
        sgfx::MakeListener(listeners[1], 501, seed % 2 != 0);
    };
    sgfx::CheckSides(ctx, "events", 48, build, [&](bool original, std::mt19937 &rng) {
        MakeLights(MakersFor(ctx, original), rng);
        const uint32_t events[] = {cIStandardLight::kChangedEvent, cIStandardLight::kBoundChangedEvent, 0x1234};
        for (int k = 0; k < 14; ++k) {
            const unsigned index = rng() % w.count;
            cILight *n = w.lights[index];
            sgfx::Manager &manager = w.manager[w.kinds[index]];
            Fake &l = listeners[rng() % 2];
            const uint32_t event = events[rng() % 3];
            const unsigned op = rng() % 7;
            drawfx::Note("op", op);
            switch (op) {
            case 0: n->Connect(event, reinterpret_cast<cIGZUnknown *>(&l), 0x500 + (rng() % 2)); break;
            case 1: n->Disconnect(event, reinterpret_cast<cIGZUnknown *>(&l), 0x500 + (rng() % 2)); break;
            case 2: n->NotifyListeners(event, rng() % 3, rng() % 2 ? NullEventBody() : nullptr); break;
            case 3: drawfx::Note("asked", uint8_t(n->AskListeners(event, rng(), nullptr))); break;
            case 4: n->AddReferrer(reinterpret_cast<nGZEvent::cIAbstractBehavior *>(&w.referrer[rng() % 3])); break;
            case 5:
                drawfx::Note("connected", {uint8_t(manager->HasConnectionsFrom(reinterpret_cast<cIGZUnknown *>(&listeners[0]))),
                                           uint8_t(manager->HasConnectionsFrom(reinterpret_cast<cIGZUnknown *>(&listeners[1])))});
                break;
            default: {
                drawfx::Note("event count", n->GetEventCount());
                const nGZEvent::cEventAttribute a = n->GetEventAttribute(rng() % 3 ? event : 0x5678u);
                drawfx::Note("event attribute", {a.mId, drawfx::Norm(P(a.mName)), a.mWord08, a.mWord0C, a.mWord10, a.mWord14});
                break;
            }
            }
        }
        NoteLights("after");
        ReleaseAll();
        for (unsigned i = 0; i < 3; ++i)
            drawfx::Note("connected", {uint8_t(w.manager[i]->HasConnectionsFrom(reinterpret_cast<cIGZUnknown *>(&listeners[0]))),
                                       uint8_t(w.manager[i]->HasConnectionsFrom(reinterpret_cast<cIGZUnknown *>(&listeners[1])))});
    });
    DropManagers();
}
