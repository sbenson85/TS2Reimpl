// Differential tests of the point and spot lights cPointLight and cSpotLight (lane OT, scenegraph N2; src/sg/cPointLight.h,
// src/sg/cSpotLight.h, src/sg/SGLightShapes.h): the game's lights (factories 0xa47c90 / 0xa47cf0, constructors 0xa46060 /
// 0xa46140, their vtables and the bodies behind them) against ours (Create, our vtables), each side building its own lights
// (of both classes, at random) with its own factories. The tests follow tests/sg_lights_standard.cpp (the lights'
// common levels are checked there; what is new here is each class's own bodies and the embedded shapes): the calls the
// lights make on the fakes (sgfx.h, and a fake of the configuration service's settings object, which OnLightChanged reads
// "lightRadianceRangeCutoff" from), the notes, every light's words but its vtable words, its vbptr and its referrer
// vector's three pointers (the game's tables on one side, ours on the other: words 0, 7, 11, 13, the virtual base's vtable
// and, in the dump, nothing else) and the padding at +0x2a, and the blocks freed when the test lets go of the lights.
// The shapes' vtable words are the game's on both sides and compare directly, their data and counts with them.
//
//   SG_PointLight_Factory / SG_SpotLight_Factory  new lights: their words, vbtables, counts.
//   SG_PointSpotLights_Slots    random operations through every face of random lights: the falloff, range and cone
//                               (NaN, signs, the angle limit), the colour and intensity, the strength at points and over
//                               boxes (random affine transforms), the parameters, the bound shape, clones, type info,
//                               Write, the referrers, kinds, the resource part.
//   SG_PointSpotLights_Persist  Write, then Read into a fresh light or a clone through either persist face.
//   SG_PointSpotLights_Events   a connection manager in the standard lights' cell: the bound-changed and changed events
//                               announced to listeners and referrers.
//
// The class bodies of the game these tests run on the original side: the constructors 0xa46060 / 0xa46140, the point
// light's destructor 0xa46740 / deleting 0xa46b40 (node face thunk 0xa4686a), OnLightChanged 0xa48330, Clone 0xa47300 and
// copy 0xa47f50, slot 38 0x3c90a0, slot 39 0xa49540, SetRange 0xa4a2d0, GetParams 0xa48bc0, Read 0xa48f30, type info
// 0xa4aaf0, Write 0xa4b0e0, record 19 0xa48190; the spot light's 0xa46790 / 0xa46b70 / 0xa46872, 0xa48540, 0xa47380 and
// 0xa47f90, 0xa495b0, 0xa49900, 0xa4a370, 0xa48c40, 0xa48fc0, 0xa4abb0, 0xa4b180, 0xa48196 and the setters 0xa4a0b0 /
// 0xa4a0e0; the shared SetFalloff 0xa4a170 and Update 0xa47440; and the standard lights' bodies listed in
// tests/sg_lights_standard.cpp.
//
// Lane TL: the spot light's resource-face thunks the SGLights group once hooked, now in effect through the factory and
// tested here against our class on both sides (Slots, op 13 and the cone ops): 0xa46c2c (slots 7 / 8, GetPersistResource),
// 0xa49a26 (slot 9, GetSubResource), 0xa4805b (slot 11, CompareIdentity), 0xa4a511 (slot 15, SetSegment), 0xa4a161 (slot
// 16, SetFlag).
//
// Lane TC: the spot light's other game bodies, in effect through the factory and run on the original side (its slots,
// the resource-face thunks, the adjustor thunks): 0xa46be4, 0xa47e96, 0xa48092, 0xa4812d, 0xa481c7, 0xa48a5d,
// 0xa48fb2, 0xa49202, 0xa49477, 0xa49a52, 0xa49a94, 0xa49acb, 0xa49f2b, 0xa4a1e2, 0xa4a28e, 0xa4a2c5, 0xa4ab9e,
// 0xa4b172.
#include "difftest.h"
#include "game/game.h"
#include "misc/SmallVirtuals.h" // nGZEvent::cNullEvent::kVTable
#include "sgfx.h"
#include "ts_services_fixture.h"

#include "sg/cPointLight.h"
#include "sg/cSpotLight.h"

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
using drawfx::Slot;

constexpr uint32_t kPointFactory = 0xa47c90, kSpotFactory = 0xa47cf0;
constexpr uint32_t kPointSize = 0x90, kSpotSize = 0xa4;
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
// Left out of the word dump: the vtables and the vbptr (words 0, 7, 11, 13), the referrer vector (16-18) and word 10 (the
// padding's half); the virtual base's vtable word is left out per class (kPointBaseWord / kSpotBaseWord).
constexpr uint64_t kSkipCommon = (1u << 0) | (1u << 7) | (1u << 10) | (1u << 11) | (1u << 13) | (1u << 16) | (1u << 17) |
                                 (1u << 18);
constexpr unsigned kPointBaseWord = 0x8c / 4, kSpotBaseWord = 0xa0 / 4;

constexpr sgfx::RegistryStatics kLightRegistry{0x10f6138, 0x10f6140, 0x10f6150, 0x10f6158};

enum Kind : uint8_t { kPoint, kSpot };
using Factory = cIStandardLight *(__cdecl *)();
cIStandardLight *__cdecl MakePoint() { return cPointLight::Create(); }
cIStandardLight *__cdecl MakeSpot() { return cSpotLight::Create(); }

struct Makers {
    Factory point, spot;
    Factory For(Kind k) const { return k == kPoint ? point : spot; }
};
Makers MakersFor(difftest::Context &ctx, bool original)
{
    if (original) return {ctx.Original<Factory>(kPointFactory), ctx.Original<Factory>(kSpotFactory)};
    return {&MakePoint, &MakeSpot};
}

uint32_t SizeOf(Kind k) { return k == kPoint ? kPointSize : kSpotSize; }
uint8_t *Object(cIStandardLight *n) { return reinterpret_cast<uint8_t *>(n) - kNode; }
cIGZPersistResource *PartFace(cIStandardLight *n) { return reinterpret_cast<cIGZPersistResource *>(Object(n)); }
cIGZPersistSerializableReferent *PersistFace(cIStandardLight *n) { return n->GetReferent(); }
uint32_t CountOf(cIStandardLight *n) { return reinterpret_cast<const uint32_t *>(n)[1]; }

// The configuration service's settings object, the one slot OnLightChanged uses: 9 reads a property into an any (value at
// +4); the fake answers a programmed cutoff (value[0], while value[1]) or leaves the any alone (the default 0.5).
constexpr Slot kSettingsSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"}, {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},          {0, drawfx::Ret::Int, false, "", ""},
    {0, drawfx::Ret::Int, false, "", ""},                {0, drawfx::Ret::Int, false, "", ""},
    {0, drawfx::Ret::Int, false, "", ""},                {0, drawfx::Ret::Int, false, "", ""},
    {0, drawfx::Ret::Int, false, "", ""},                {2, drawfx::Ret::Int, true, "GetProperty", "ii"},
};
constexpr drawfx::Iface kSettings{"Settings", 10, kSettingsSlots};

Fake settings;
unsigned settingsAsked = 0;

uint32_t SettingsGet(Fake &self, const uint32_t *a)
{
    if (!self.value[1]) return 0;
    *reinterpret_cast<uint32_t *>(uintptr_t(a[1]) + 4) = self.value[0];
    return 1;
}
uint32_t FrameworkService(Fake &self, const uint32_t *a)
{
    if (a[0] == 0x0ac1887e && a[1] == 0x4bb0d269) {
        ++settings.refs;
        ++settingsAsked;
        *reinterpret_cast<void **>(uintptr_t(a[2])) = &settings;
        return 1;
    }
    return sgfx::detail::GetSystemService(self, a);
}

struct World {
    cIStandardLight *lights[kMax];
    Kind kinds[kMax];
    unsigned count = 0;
    Fake ext[3];
    Fake referrer[3];
    Fake context;
    sgfx::Manager manager;
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
    // The settings: no answer (0.5), or a cutoff from a few values (zero gives an infinite ratio, a negative one a
    // negative ratio, NaN poisons everything).
    settings.Reset<kSettings>(463);
    settings.On(9, &SettingsGet);
    const float cutoffs[] = {0.25f, 0.5f, 1.0f, 2.0f, 0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), 0.1f};
    settings.value[0] = std::bit_cast<uint32_t>(cutoffs[seed % 8]);
    settings.value[1] = seed % 4 != 0;
    settingsAsked = 0;
    sgfx::gResources.framework.On(5, &FrameworkService);
    for (unsigned i = 0; i < 3; ++i) sgfx::MakeExtension(w.ext[i], 200 + i, rng() % 2);
    for (unsigned i = 0; i < 3; ++i) {
        w.referrer[i].Reset<sgfx::kBehavior>(600 + i);
        w.referrer[i].Returns(4, uint32_t(rng() % 2));
    }
    w.count = 0;
    w.manager.Drop();
    w.manager.Use(cells, cStandardLightBehaviorTraits::kEventManagerCell);
}

bool Hold(cIStandardLight *n, Kind kind, bool counted)
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

cIStandardLight *Make(const Makers &m, std::mt19937 &rng)
{
    const Kind kind = rng() % 2 ? kPoint : kSpot;
    cIStandardLight *n = m.For(kind)();
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
        const uint64_t skip = kSkipCommon | (1ull << (w.kinds[i] == kPoint ? kPointBaseWord : kSpotBaseWord));
        drawfx::Call c;
        c.note = label;
        c.words.push_back(i);
        for (unsigned k = 0; k < SizeOf(w.kinds[i]) / 4; ++k)
            if (!(skip >> k & 1)) c.words.push_back(drawfx::Norm(words[k]));
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
    drawfx::Note("settings", {uint32_t(settings.refs), settingsAsked});
    sgfx::gPool.NoteCounts();
    sgfx::gResources.NoteRefs();
}

cIGZUnknown *Segment(std::mt19937 &rng)
{
    const unsigned k = rng() % 4;
    return k < 3 ? reinterpret_cast<cIGZUnknown *>(&sgfx::gResources.segments[k]) : nullptr;
}

uint32_t Bits(float f) { return std::bit_cast<uint32_t>(f); }

float Component(std::mt19937 &rng, float current)
{
    const float values[] = {0.0f, -0.0f, 1.0f, 0.25f, 0.8f, -0.5f, 1e-40f, 3.0f};
    if (rng() % 2) return values[rng() % 8];
    const int32_t steps[] = {0, 1, 5, 31, 32, 33, -1, -31, -32, -33, 64};
    return std::bit_cast<float>(Bits(current) + uint32_t(steps[rng() % 11]));
}

float Scalar(std::mt19937 &rng, float current)
{
    const float values[] = {0.0f, -0.0f, 0.5f, 1.0f, 2.5f, -1.0f, std::numeric_limits<float>::quiet_NaN()};
    return rng() % 3 == 0 ? current : values[rng() % 7];
}

// Falloffs, ranges and angles: signs, zero, NaN, the cone limit (3.1101768) and values about it.
float Wide(std::mt19937 &rng, float current)
{
    const float values[] = {0.0f,  -0.0f, 0.4f,  1.0f,   2.5f,  -1.0f,   0.001f, 100.0f, 1e30f, 1.55f,
                            1.56f, 3.0f,  3.2f,  -2.0f,  0.3f,  10000.0f, std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity()};
    return rng() % 4 == 0 ? current : values[rng() % 18];
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
    cIStandardLight *n = w.lights[index];
    const Kind kind = w.kinds[index];
    const uint32_t iids[] = {cIStandardLight::kIid, cILight::kIid, sgfx::kIidGraph, cIReferentNode::kIid, 0x12345678};
    const uint32_t mode = rng() % 3, iid = iids[rng() % 5];
    if (rng() % 2) {
        // Poke the bound shape's data words first (the game's consumers may): the copy must carry every one.
        float *words = reinterpret_cast<float *>(n->GetBoundShape());
        for (unsigned i = 1; i <= (kind == kPoint ? 4u : 5u); ++i) words[i] = float(int(rng() % 9) - 4);
    }
    sgfx::MakeCloneContext(w.context, nullptr);
    void *out = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
    n->Clone(mode, iid, &out, rng() % 2 ? reinterpret_cast<cINodeCloneContext *>(&w.context) : nullptr);
    const bool answered = out != reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
    drawfx::Note("clone", {mode, iid, uint8_t(answered), answered && out ? CountOf(static_cast<cIStandardLight *>(out)) : 0});
    if (answered && out) {
        cIStandardLight *clone = static_cast<cIStandardLight *>(out);
        if (Hold(clone, kind, true)) drawfx::Note("clone held", w.count - 1);
    }
}

// A random affine transform: a matrix of a few values (rotations among them) and an offset.
AffineTransform3 RandomTransform(std::mt19937 &rng)
{
    AffineTransform3 t{};
    const float values[] = {0.0f, 1.0f, -1.0f, 0.5f, 0.866f, -0.866f, 2.0f};
    const bool identity = rng() % 3 == 0;
    for (unsigned i = 0; i < 9; ++i) t.mMatrix[i] = identity ? float(i % 4 == 0) : values[rng() % 7];
    for (float &o : t.mOffset) o = identity ? 0.0f : Coord(rng);
    t.mUnknown0 = rng();
    return t;
}

// The strength about a light: at some points, and over some boxes.
void NoteStrengths(cIStandardLight *n, std::mt19937 &rng, unsigned points, unsigned boxes)
{
    for (unsigned i = 0; i < points; ++i) {
        const float p[3] = {Coord(rng), Coord(rng), Coord(rng)};
        NoteFloats("strength at", {n->StrengthAt(p)});
    }
    for (unsigned i = 0; i < boxes; ++i) {
        float box[6];
        for (float &b : box) b = Coord(rng);
        AffineTransform3 t = RandomTransform(rng);
        NoteFloats("strength over", {n->StrengthOverBox(box, &t)});
    }
}

void Operate(difftest::Context &ctx, const Makers &m, int k, std::mt19937 &rng)
{
    const unsigned index = rng() % w.count;
    cIStandardLight *n = w.lights[index];
    cISGResource *face = n->GetResource();
    const unsigned op = rng() % 22;
    drawfx::Note("op", op);
    char step[32];
    std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
    sgfx::MarkStep(ctx, step);
    switch (op) {
    case 0: { // QueryInterface through any face
        const uint32_t iids[] = {cIStandardLight::kIid, cILight::kIid, cIReferentNode::kIid, sgfx::kIidGraph, 0x5b0aaa74,
                                 1, 0x3b253dc7, cISGResource::kIid, cGZPersistResource::kIid, 0x12345678};
        const uint32_t iid = iids[rng() % 10];
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
    case 2: { // the colour: the range and the strength follow
        float now[3];
        drawfx::Note("colour returns", {n->GetColor(now) == now ? 1u : 0u});
        float rgb[3];
        for (unsigned i = 0; i < 3; ++i) rgb[i] = Component(rng, now[i]);
        n->SetColor(rgb);
        n->GetColor(now);
        NoteFloats("colour", now, 3);
        NoteFloats("reach", {n->GetRange(), n->GetFalloff()});
        NoteStrengths(n, rng, 2, 1);
        break;
    }
    case 3: { // intensity and factor
        if (rng() % 2)
            n->SetIntensity(Scalar(rng, n->GetIntensity()));
        else
            n->SetFactor(Scalar(rng, n->GetFactor()));
        NoteFloats("scalars", {n->GetIntensity(), n->GetFactor(), n->GetRange()});
        NoteStrengths(n, rng, 2, 1);
        break;
    }
    case 4: { // the falloff
        n->SetFalloff(Wide(rng, n->GetFalloff()));
        NoteFloats("falloff", {n->GetFalloff(), n->GetRange()});
        NoteStrengths(n, rng, 3, 0);
        break;
    }
    case 5: { // the range: stored as it is, announced, the bound follows
        n->SetRange(Wide(rng, n->GetRange()));
        NoteFloats("range", {n->GetRange(), n->GetFalloff()});
        NoteStrengths(n, rng, 3, 1);
        break;
    }
    case 6: { // the cone (a no-op on the point light)
        if (rng() % 4 == 0) {
            // The two angles summing to exactly the largest allowed sum (3.1101768): not reset; one bit more is. The
            // bit is taken on the sum (Sterbenz: both differences from 3.0f are exact), since one bit more on the small
            // angle rounds away when added to 3.0f.
            const float cone = 3.0f, falloff = 3.1101768f - 3.0f, over = std::nextafter(3.1101768f, 4.0f) - 3.0f;
            n->SetConeAngle(cone);
            n->SetConeFalloffAngle(rng() % 2 ? falloff : over);
        } else if (rng() % 2)
            n->SetConeAngle(Wide(rng, n->GetConeAngle()));
        else
            n->SetConeFalloffAngle(Wide(rng, n->GetConeFalloffAngle()));
        NoteFloats("cone", {n->GetConeAngle(), n->GetConeFalloffAngle(), n->GetRange()});
        NoteStrengths(n, rng, 4, 1);
        break;
    }
    case 7: { // the parameters and the constant slots
        float params[9];
        for (float &p : params) p = -7.0f;
        n->GetParams(params, rng());
        NoteFloats("params", params, 9);
        NoteFloats("getters", {n->GetFalloff(), n->GetRange(), n->GetConeAngle(), n->GetConeFalloffAngle(), n->GetIntensity()});
        drawfx::Note("light", {n->GetLightType(), drawfx::Norm(P(n->GetBoundShape())), drawfx::Norm(P(n->GetStandardLight())),
                               drawfx::Norm(P(n->GetStandardLightConst()))});
        break;
    }
    case 8: // Update: slot 13 again
        n->Update();
        NoteFloats("updated", {n->GetRange(), n->GetConeAngle(), n->GetConeFalloffAngle()});
        if (rng() % 2) {
            // Poke one data word of the bound shape (the game's consumers may; the copy must carry every word).
            float *words = reinterpret_cast<float *>(n->GetBoundShape());
            for (unsigned i = 1; i <= (w.kinds[index] == kPoint ? 4u : 5u); ++i) words[i] = float(int(rng() % 9) - 4);
        }
        break;
    case 9: { // referrers
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
    case 10: { // notifications and queries
        const uint32_t events[] = {cIStandardLight::kChangedEvent, cIStandardLight::kBoundChangedEvent, 0x1234};
        n->NotifyListeners(events[rng() % 3], rng() % 3, rng() % 2 ? NullEventBody() : nullptr);
        drawfx::Note("asked", uint8_t(n->AskListeners(events[rng() % 3], rng() % 3, nullptr)));
        break;
    }
    case 11: { // kinds
        const uint32_t iids[] = {cIStandardLight::kIid, cILight::kIid, cIReferentNode::kIid, sgfx::kIidGraph,
                                 cISGResource::kIid, 0x3b253dc7, 1, 0, 0xc9c81b9b, 0xc9c81ba9, 0xc9c81bad};
        for (uint32_t iid : iids) drawfx::Note("kind", {iid, uint8_t(n->IsKindOf(iid))});
        break;
    }
    case 12: { // the name
        const char *names[] = {"alpha", "sun_lght", "", "beta"};
        n->SetLightName(names[rng() % 4]);
        sgfx::NoteText("name", n->GetLightName());
        break;
    }
    case 13: { // the virtual base
        {
            // The identity thunks of the resource face (slots 7 / 8, 9, 11): the owner, the sub resource, the comparison.
            const unsigned pick = rng() % 3;
            const unsigned peer = rng() % w.count;
            const uint32_t subIndex = 1 + rng() % 3;
            cIGZUnknown *other = pick == 0   ? static_cast<cIGZUnknown *>(PersistFace(n))
                                 : pick == 1 ? static_cast<cIGZUnknown *>(w.lights[peer])
                                             : static_cast<cIGZUnknown *>(face);
            const int32_t same = face->CompareIdentity(other);
            drawfx::Note("identity", {drawfx::Norm(P(face->GetPersistResource())), drawfx::Norm(P(face->GetPersistResourceConst())),
                                      drawfx::Norm(P(face->GetSubResource(0))), drawfx::Norm(P(face->GetSubResource(subIndex))),
                                      uint32_t(same)});
        }
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
    case 14: { // a type-info record, through either persist face
        cPersistTypeInfo info = rng() % 2 ? PersistFace(n)->GetTypeInfo() : face->GetTypeInfo();
        sgfx::NoteTypeInfo("type", &info);
        if (info.mName) info.mName->Release();
        break;
    }
    case 15: // written, through either persist face
        if (rng() % 2)
            PersistFace(n)->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        else
            face->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        break;
    case 16:
    case 17: CloneOf(index, rng); break;
    case 18: // a new light
        Make(m, rng);
        break;
    case 19: { // strength only; first make the light bright with a modest reach, so points and boxes land inside it
        if (rng() % 4 == 0) {
            // A radiance of exactly the default cutoff (0.5): no reach, even with no falloff to bound it.
            const float rgb[3] = {0.5f, 0.0f, 0.0f};
            n->SetColor(rgb);
            n->SetIntensity(1.0f);
            n->SetFalloff(rng() % 2 ? 0.0f : -1.0f);
            NoteFloats("range edge", {n->GetRange(), n->GetFalloff()});
        }
        if (rng() % 2) {
            const float rgb[3] = {1.0f, float(rng() % 3) * 0.5f, 0.5f};
            n->SetColor(rgb);
            n->SetIntensity(float(1 + rng() % 3));
        }
        if (rng() % 2) n->SetFalloff(float(rng() % 4) * 0.5f);
        if (rng() % 2) n->SetRange(float(1 + rng() % 6));
        NoteStrengths(n, rng, 6, 3);
        const float r = n->GetRange();
        const float edge[][3] = {{r, 0.0f, 0.0f}, {0.0f, r, 0.0f}, {0.0f, 0.0f, r}, {0.0f, 0.0f, -r}, {3.0f, 4.0f, 0.0f},
                              {0.0f, 3.0f, 4.0f}};
        for (const float *p : edge) NoteFloats("strength edge", {n->StrengthAt(p)});
        // A cone of no angle: a point on its axis sits exactly on the edge (the outer cosine is 1) and gets no light.
        n->SetConeFalloffAngle(0.0f);
        n->SetConeAngle(0.0f);
        n->SetRange(10.0f);
        for (const float z : {-1.0f, -2.0f, -3.0f}) {
            const float p[3] = {0.0f, 0.0f, z};
            NoteFloats("strength axis edge", {n->StrengthAt(p)});
        }
        // Points in the cone's blend between its inner and outer angle (a falloff angle opens the outer one).
        n->SetConeAngle(0.4f);
        n->SetConeFalloffAngle(0.4f);
        n->SetRange(10.0f);
        for (const float theta : {0.0f, 0.1f, 0.25f, 0.35f, 0.5f, 0.7f}) {
            const float p[3] = {2.0f * std::sin(theta), 0.5f * std::sin(theta), -2.0f * std::cos(theta)};
            NoteFloats("strength blend", {n->StrengthAt(p)});
        }
        // Wide, thin boxes about a point down the cone's axis: the corners fall outside a narrow cone and only the centre
        // (the spot light's ninth sample) is inside it.
        n->SetConeFalloffAngle(0.0f);
        n->SetConeAngle(0.4f + 0.2f * float(rng() % 4));
        n->SetRange(10.0f);
        for (unsigned i = 0; i < 4; ++i) {
            const float cx = float(int(rng() % 5) - 2), cy = float(int(rng() % 5) - 2), cz = -float(2 + rng() % 4);
            const float hx = float(3 + rng() % 3), hy = float(3 + rng() % 3), hz = 0.5f * float(1 + rng() % 3);
            const float box[6] = {cx - hx, cy - hy, cz - hz, cx + hx, cy + hy, cz + hz};
            AffineTransform3 identity{};
            identity.mMatrix[0] = identity.mMatrix[4] = identity.mMatrix[8] = 1.0f;
            NoteFloats("strength axis", {n->StrengthOverBox(box, &identity), n->StrengthAt(&box[0])});
        }
        // Small whole-number boxes: any corner may be the nearest to the light, and the transform moves them about.
        for (unsigned i = 0; i < 3; ++i) {
            float box[6];
            for (float &b : box) b = float(int(rng() % 9) - 4);
            AffineTransform3 t = RandomTransform(rng);
            NoteFloats("strength near", {n->StrengthOverBox(box, &t)});
        }
        break;
    }
    case 20: { // owner info, extensions and the tag
        drawfx::Note("owner info", {drawfx::Norm(PersistFace(n)->GetOwnerInfo()), drawfx::Norm(face->GetOwnerInfo())});
        Fake &e = w.ext[rng() % 3];
        auto *ext = reinterpret_cast<cIExtension *>(&e);
        if (n->HasExtension(ext))
            n->RemoveExtension(ext);
        else if (!e.value[sgfx::kExtOwnerWord])
            n->AddExtension(ext);
        drawfx::Note("extensions", {drawfx::Norm(P(n->GetFirstExtension())), uint8_t(n->HasExtension(ext))});
        break;
    }
    default: { // an event to the light itself
        n->OnEvent(rng() % 2 ? 0x5b51e806u : 0x777u, 0, nullptr, 0);
        drawfx::Note("active", n->IsActive());
        break;
    }
    }
}

struct MissingRecordScope {
    static constexpr uint32_t kAt = 0x106ab68;
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
    sgfx::RegistryScope registry(kLightRegistry);
    svcfx::AllocatorScope allocator; // the settings key is a service string: the game's allocator service
    sgfx::CheckSides(ctx, "factory", 8, Build, [&](bool original, std::mt19937 &) {
        const Makers m = MakersFor(ctx, original);
        cIStandardLight *a = m.For(kind)();
        cIStandardLight *b = m.For(kind)();
        drawfx::Note("counts", {CountOf(a), CountOf(b)});
        sgfx::NoteText("unnamed", a->GetLightName());
        Hold(a, kind, false);
        Hold(b, kind, false);
        NoteLights("new light");
        ReleaseAll();
    });
    w.manager.Drop();
}

} // namespace

DIFF_TEST(SG_PointLight_Factory, kPointFactory) { FactoryBody(ctx, kPoint); }

DIFF_TEST(SG_SpotLight_Factory, kSpotFactory) { FactoryBody(ctx, kSpot); }

DIFF_TEST(SG_PointSpotLights_Slots, kPointFactory)
{
    sgfx::RegistryScope registry(kLightRegistry);
    svcfx::AllocatorScope allocator; // the settings key is a service string: the game's allocator service
    sgfx::CheckSides(ctx, "slots", kSeeds, Build, [&](bool original, std::mt19937 &rng) {
        const Makers m = MakersFor(ctx, original);
        MakeLights(m, rng);
        NoteLights("built");
        for (int k = 0; k < 24; ++k) Operate(ctx, m, k, rng);
        NoteLights("after");
        sgfx::gReader.NoteNames();
        ReleaseAll();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_PointSpotLights_Persist, kPointFactory)
{
    sgfx::RegistryScope registry(kLightRegistry);
    svcfx::AllocatorScope allocator; // the settings key is a service string: the game's allocator service
    sgfx::CheckSides(ctx, "persist", kSeeds, Build, [&](bool original, std::mt19937 &rng) {
        const Makers m = MakersFor(ctx, original);
        MakeLights(m, rng);
        const unsigned source = rng() % w.count;
        cIStandardLight *n = w.lights[source];
        if (rng() % 2) n->SetTag("alpha");
        if (rng() % 2) n->AddExtension(reinterpret_cast<cIExtension *>(&w.ext[0]));
        if (rng() % 2) n->SetLightName(rng() % 2 ? "sun_lght" : "");
        const float rgb[3] = {0.5f, 0.25f, 1.0f};
        if (rng() % 2) n->SetColor(rgb);
        if (rng() % 2) n->SetIntensity(2.0f);
        if (rng() % 2) n->SetFalloff(Wide(rng, 0.4f));
        if (rng() % 2) n->SetConeAngle(Wide(rng, 0.0f));
        if (rng() % 2) n->SetConeFalloffAngle(Wide(rng, 0.0f));
        if (rng() % 2) n->GetResource()->AssignAndSubmit("sun_lght", nullptr);
        if (rng() % 2)
            PersistFace(n)->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        else
            n->GetResource()->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));

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
        cIStandardLight *fresh = w.lights[target];
        const Kind kind = w.kinds[target];
        sgfx::Reader &r = sgfx::gReader;
        r.Version(5);                            // the leaf's record
        r.Version(rng() % 2 ? 0xb : rng() % 16); // cStandardLightBase's
        r.Version(rng() % 3);                    // cSGResource's (first)
        r.Text(rng() % 2 ? "sun_lght" : "");
        r.Version(1);                            // cLightT's
        r.Version(rng() % 3);                    // cSGResource's (again)
        r.Text(rng() % 2 ? "sun_lght" : "beta");
        r.Version(1);                            // the referent Impl's
        r.Version(4);                            // the object-graph Impl's
        const uint32_t extensions = rng() % 2;
        r.Word(extensions);
        if (extensions) r.Ref(&w.ext[1]);
        r.Text(rng() % 2 ? "alpha" : "");
        r.Text(rng() % 2 ? "sun_lght" : "beta"); // the light's name
        const float values[] = {1.5f, 0.75f, 0.2f, 0.4f, 0.6f, 0.0f, -2.0f};
        for (unsigned i = 0; i < 5; ++i) r.Float(values[rng() % 7]);
        for (unsigned i = 0; i < (kind == kPoint ? 2u : 4u); ++i) r.Float(Wide(rng, 0.4f)); // the light's own floats
        if (rng() % 2)
            PersistFace(fresh)->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
        else
            fresh->GetResource()->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
        drawfx::Note("read", {r.wordAt, r.refAt, r.textAt, r.versionAt});
        sgfx::NoteText("name", fresh->GetLightName());
        NoteFloats("reach", {fresh->GetFalloff(), fresh->GetRange(), fresh->GetConeAngle(), fresh->GetConeFalloffAngle()});
        NoteLights("after");
        sgfx::gReader.NoteNames();
        PersistFace(fresh)->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        ReleaseAll();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_PointSpotLights_Events, kPointFactory)
{
    sgfx::RegistryScope registry(kLightRegistry);
    svcfx::AllocatorScope allocator; // the settings key is a service string: the game's allocator service
    MissingRecordScope missing;
    static Fake listeners[2];
    sgfx::Manager &manager = w.manager;
    auto build = [&](uint32_t seed, drawfx::Cells &cells) {
        Build(seed, cells);
        sgfx::MakeListener(listeners[0], 500, true);
        sgfx::MakeListener(listeners[1], 501, seed % 2 != 0);
    };
    sgfx::CheckSides(ctx, "events", 48, build, [&](bool original, std::mt19937 &rng) {
        MakeLights(MakersFor(ctx, original), rng);
        const uint32_t events[] = {cIStandardLight::kChangedEvent, cIStandardLight::kBoundChangedEvent, 0x1234};
        for (int k = 0; k < 14; ++k) {
            cIStandardLight *n = w.lights[rng() % w.count];
            Fake &l = listeners[rng() % 2];
            const uint32_t event = events[rng() % 3];
            const unsigned op = rng() % 9;
            drawfx::Note("op", op);
            switch (op) {
            case 0: n->Connect(event, reinterpret_cast<cIGZUnknown *>(&l), 0x500 + (rng() % 2)); break;
            case 1: n->Disconnect(event, reinterpret_cast<cIGZUnknown *>(&l), 0x500 + (rng() % 2)); break;
            case 2: { // a change of colour (the light announces the bound, then the change)
                const float rgb[3] = {float(rng() % 4), 0.5f, 0.25f};
                n->SetColor(rgb);
                break;
            }
            case 3: drawfx::Note("asked", uint8_t(n->AskListeners(event, rng(), nullptr))); break;
            case 4: n->SetRange(float(rng() % 5)); break;
            case 5: n->SetFalloff(float(rng() % 4) * 0.5f); break;
            case 6: n->AddReferrer(reinterpret_cast<nGZEvent::cIAbstractBehavior *>(&w.referrer[rng() % 3])); break;
            case 7: n->Update(); break;
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
        drawfx::Note("connected", {uint8_t(manager->HasConnectionsFrom(reinterpret_cast<cIGZUnknown *>(&listeners[0]))),
                                   uint8_t(manager->HasConnectionsFrom(reinterpret_cast<cIGZUnknown *>(&listeners[1])))});
    });
    w.manager.Drop();
}
