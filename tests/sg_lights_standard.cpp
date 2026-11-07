// Differential tests of the standard lights cAmbientLight and cDirectionalLight (lane OR, scenegraph N2;
// src/sg/cStandardLight.h, src/sg/SGLightTemplates.h): the game's lights (factories 0xa47ab0 / 0xa47b10, constructors
// 0xa45d40 / 0xa45db0, their vtables and the bodies behind them) against ours (Create, our vtables), each side building
// its own lights (of both classes, at random) with its own factories. The tests drive the lights through the node face,
// the persist face (+0x34), the cSGResource part's face (+0) and the virtual base (+0x6c), and compare the calls the
// lights make on the fakes (sgfx.h: referrers, listeners, extensions, the reader and writer, the name pool, the resource
// manager), the notes, every light's words but its vtable words, its vbptr and its referrer vector's three pointers (the
// game's tables on one side, ours on the other: words 0, 7, 11, 13 and 27; the vbtable's two entries, the referrers by
// id, the size and the capacity are compared instead) and the padding at +0x2a (the upper half of word 10), and the
// blocks freed when the test lets go of the lights.
//
// Why +0x2a / +0x2b are left out: the cSGResource part's constructor 0xa3c270 clears its two flags (+0x28 / +0x29) with
// one 16-bit store and writes nothing at +0x2a, and neither light constructor (0xa46250 and the leaves) writes there, so
// both sides keep whatever the heap left (as in tests/sg_resource_node.cpp).
//
//   SG_StandardLights_Factory  new lights: their words, vbtables, counts.
//   SG_StandardLights_Slots    random operations through every face of random lights: interfaces, counts, the colour
//                              (ulp boundaries), intensity, factor, name, the parameters, the referrers and their
//                              notifications, extensions and tag, kinds, the resource part, records, clones.
//   SG_StandardLights_Persist  Write through either persist face, then Read into a fresh light or a clone.
//   SG_StandardLights_Events   the behaviour with a connection manager in the instantiation's cell 0x10da480.
//   SG_StandardLights_GameBuilt  the game's lights built on these (its point light, three nTSSG lights) beside standard
//                              lights, with every hooked body patched to ours on the second side (lane VW; see there).
//
// The class bodies of the game the first four tests run on the original side (reached through the game's vtables;
// GameBuilt runs the game's lights on both sides, with ours patched in on the second at every RVA lane VW hooked, so
// those bodies are ours in the game and the rest below are in effect through their callers, apart from the shared
// 0xa48ce0, 0xa4a610, 0xa4ad50 and 0x3a59c0, which other classes still reach; db/status/cStandardLight.jsonl): the leaves'
// constructors 0xa45d40 / 0xa45db0, destructors 0xa465f0 / 0xa46630, deleting destructors 0xa46a90 / 0xa46ab0 (and the
// node-face thunks 0xa46842 / 0xa4684a), OnLightChanged 0xa48230 / 0xa482b0, Clone 0xa47080 / 0xa47100, GetParams
// 0xa48ac0 / 0xa48b40, type info 0xa4a6d0 / 0xa4a790, Write 0xa4ade0 / 0xa4ae60, record 19 0xa48140 / 0xa48150, the
// shared copy 0xa47eb0 and Read 0xa48ce0; cStandardLightBase's 0xa46250, 0xa467e0, 0xa488a0, 0xa48a70, 0xa4a5d0,
// 0xa46c40, 0xa4a1f0, 0xa49f40, 0xa4a110, 0xa4a4c0, 0xa480a0, 0xa47400, 0xa48cc0, 0xa49070, 0xa4ac70, 0xa4b250,
// 0xa47ff0; cLightT's 0xa45a40, 0xa463d0, 0x3a5a60, 0x3c9030, 0x3a6460, 0x3a5c30, 0xa4a610, 0xa4ad50, 0x3a59c0; the
// referent node's 0xa45d00, 0xa465b0, 0x3a5b50, 0x3c9930, 0x3c87b0; the object-graph node's 0xa45c00, 0xa464f0; the
// behaviour's 0xa45920, 0xa46310, 0x3c87f0, 0x3c85d0, 0x3c8670, 0x3c85f0, 0xa481e0, 0xa46c50, 0xa46fa0.
//
// In effect through their callers (lane VW; GameBuilt and the tests above reach them through the lights they build and
// release, read, write and clone): the deleting destructors 0xa46ba0 (node thunk 0xa4687a) and the level's 0xa46950
// (node thunk 0xa46832), 0xa46a50, 0xa469d0, 0xa468d0; the persist thunks 0xa46bc0, 0xa481a6, 0xa48a39, 0xa4905a,
// 0xa49453, 0xa49a31, 0xa4ac56, 0xa4b236, 0xa4a6b9, 0xa4a77c, 0xa4adc7, 0xa4ae4e, 0xa48138, 0xa48146; and the 6-byte
// shared StrengthOverBox / StrengthAt 0xa49520 / 0xa49530 (the Slots strength op).
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "misc/SmallVirtuals.h" // nGZEvent::cNullEvent::kVTable
#include "misc/UtilitiesC.h"   // AffineTransform3
#include "sgfx.h"
#include "ts_services_fixture.h"

#include "sg/cLightLevels.h"
#include "sg/cStandardLight.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <list>
#include <optional>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;

constexpr uint32_t kAmbientFactory = 0xa47ab0, kDirectionalFactory = 0xa47b10;
constexpr uint32_t kSize = 0x70;
constexpr uint32_t kNode = 0x2c;    // the node face
constexpr uint32_t kVbptr = 0x1c;   // the cSGResource part's vbptr
constexpr uint32_t kLightTag = 0x7d000000; // kLightTag | index << 8 | offset
constexpr unsigned kMax = 10;       // lights a test holds
constexpr unsigned kSeeds = 64;

// A real event body (a cNullEvent): the listeners note a body through its own slots, which a clone context fake would
// answer with whatever its caller left in the arguments (lane OT: this flaked in a full run).
void *NullEventBody()
{
    static const void *body;
    body = GameAddress<const void>(nGZEvent::cNullEvent::kVTable);
    return &body;
}
// Left out of the word dump: the vtables and the vbptr (words 0, 7, 11, 13, 27), the referrer vector (16-18: heap
// pointers; noted by content instead) and word 10 (+0x28), whose upper half is padding (see the top): its low half is
// noted instead.
constexpr uint32_t kSkipWords = (1u << 0) | (1u << 7) | (1u << 10) | (1u << 11) | (1u << 13) | (1u << 16) | (1u << 17) |
                                (1u << 18) | (1u << 27);

// The lights' event registry (0xa49210): its static map, guards and holder.
constexpr sgfx::RegistryStatics kLightRegistry{0x10f6138, 0x10f6140, 0x10f6150, 0x10f6158};

using Factory = cIStandardLight *(__cdecl *)();
cIStandardLight *__cdecl MakeAmbient() { return cAmbientLight::Create(); }
cIStandardLight *__cdecl MakeDirectional() { return cDirectionalLight::Create(); }

struct Makers {
    Factory ambient, directional;
};
Makers MakersFor(difftest::Context &ctx, bool original)
{
    if (original) return {ctx.Original<Factory>(kAmbientFactory), ctx.Original<Factory>(kDirectionalFactory)};
    return {&MakeAmbient, &MakeDirectional};
}

uint8_t *Object(cIStandardLight *n) { return reinterpret_cast<uint8_t *>(n) - kNode; }
cIGZPersistResource *PartFace(cIStandardLight *n) { return reinterpret_cast<cIGZPersistResource *>(Object(n)); }
cIGZPersistSerializableReferent *PersistFace(cIStandardLight *n) { return n->GetReferent(); }
uint32_t CountOf(cIStandardLight *n) { return reinterpret_cast<const uint32_t *>(n)[1]; }

// What a held light is: one of the standard lights (each side's own vtables), or a light the game's constructors
// built on both sides (the game's vtables on both: every word compared but the referrer vector and the padding).
enum Kind : uint8_t { kPlain, kGamePoint, kAmbientFromLightMap, kBinaryShadow, kHalfspacePoint };
constexpr uint32_t kKindSize[] = {kSize, 0x90, 0x7c, 0xb0, 0xa0};
constexpr uint32_t kGameSkipWords = (1u << 10) | (1u << 16) | (1u << 17) | (1u << 18);

struct World {
    cIStandardLight *lights[kMax];
    Kind kind[kMax];
    unsigned count = 0;
    Fake ext[3];      // ids 200-202
    Fake referrer[3]; // ids 600-602
    Fake context;
    sgfx::Manager manager;
};
World w;

// The configuration service's settings object the game's point light reads its cutoff from (GameBuilt; as
// tests/sg_lights_point_spot.cpp's): slot 9 answers a programmed cutoff (value[0], while value[1]) or leaves the any alone.
constexpr drawfx::Slot kSettingsSlots[] = {
    {2, drawfx::Ret::Int, true, "QueryInterface", "ui"}, {0, drawfx::Ret::Int, true, "AddRef", ""},
    {0, drawfx::Ret::Int, true, "Release", ""},          {0, drawfx::Ret::Int, false, "", ""},
    {0, drawfx::Ret::Int, false, "", ""},                {0, drawfx::Ret::Int, false, "", ""},
    {0, drawfx::Ret::Int, false, "", ""},                {0, drawfx::Ret::Int, false, "", ""},
    {0, drawfx::Ret::Int, false, "", ""},                {2, drawfx::Ret::Int, true, "GetProperty", "ii"},
};
constexpr drawfx::Iface kSettings{"Settings", 10, kSettingsSlots};
Fake settings;

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
        *reinterpret_cast<void **>(uintptr_t(a[2])) = &settings;
        return 1;
    }
    return sgfx::detail::GetSystemService(self, a);
}

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
    settings.Reset<kSettings>(463);
    settings.On(9, &SettingsGet);
    const float cutoffs[] = {0.25f, 0.5f, 2.0f, 0.0f};
    settings.value[0] = std::bit_cast<uint32_t>(cutoffs[seed % 4]);
    settings.value[1] = seed % 3 != 0;
    sgfx::gResources.framework.On(5, &FrameworkService);
    for (unsigned i = 0; i < 3; ++i) sgfx::MakeExtension(w.ext[i], 200 + i, rng() % 2);
    for (unsigned i = 0; i < 3; ++i) {
        w.referrer[i].Reset<sgfx::kBehavior>(600 + i);
        w.referrer[i].Returns(4, uint32_t(rng() % 2));
    }
    w.count = 0;
    w.manager.Drop(); // the other side's
    w.manager.Use(cells, cStandardLightBehaviorTraits::kEventManagerCell);
}

// A held light: named and counted by the test; false (the light released) when the test holds kMax already.
bool Hold(cIStandardLight *n, bool counted, Kind kind = kPlain)
{
    if (w.count >= kMax) {
        if (counted) n->Release();
        return false;
    }
    drawfx::AddRange(Object(n), kKindSize[kind], kLightTag | (w.count << 8));
    if (!counted) n->AddRef();
    w.kind[w.count] = kind;
    w.lights[w.count++] = n;
    return true;
}

Kind KindOf(cIStandardLight *n)
{
    for (unsigned i = 0; i < w.count; ++i)
        if (w.lights[i] == n) return w.kind[i];
    return kPlain;
}

cIStandardLight *Make(const Makers &m, std::mt19937 &rng)
{
    cIStandardLight *n = rng() % 2 ? m.ambient() : m.directional();
    return Hold(n, false) ? n : nullptr;
}

void MakeLights(const Makers &m, std::mt19937 &rng)
{
    const unsigned n = 1 + rng() % 5;
    for (unsigned i = 0; i < n; ++i) Make(m, rng);
}

// Each light's words (kSkipWords left out), the part's flags, its vbtable's entries, its referrers.
void NoteLights(const char *label)
{
    for (unsigned i = 0; i < w.count; ++i) {
        const uint32_t *words = reinterpret_cast<const uint32_t *>(Object(w.lights[i]));
        drawfx::Call c;
        c.note = label;
        c.words.push_back(i);
        const Kind kind = w.kind[i];
        const uint32_t skip = kind == kPlain ? kSkipWords : kGameSkipWords;
        for (unsigned k = 0; k < kKindSize[kind] / 4; ++k)
            if (!(skip >> k & 1)) c.words.push_back(drawfx::Norm(words[k]));
        c.words.push_back(words[10] & 0xffff);
        const int32_t *vbtable = *reinterpret_cast<const int32_t *const *>(Object(w.lights[i]) + kVbptr);
        c.words.push_back(uint32_t(vbtable[0]));
        c.words.push_back(uint32_t(vbtable[1]));
        // The referrer vector (+0x40 first, +0x44 last, +0x48 end): the referrers by id, the size and the capacity.
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

// Releases the test's references, noting the sizes of the blocks freed and what the lights held.
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
    sgfx::gPool.NoteCounts();
    sgfx::gResources.NoteRefs();
}

cIGZUnknown *Segment(std::mt19937 &rng)
{
    const unsigned k = rng() % 4;
    return k < 3 ? reinterpret_cast<cIGZUnknown *>(&sgfx::gResources.segments[k]) : nullptr;
}

uint32_t Bits(float f) { return std::bit_cast<uint32_t>(f); }

// A colour component: one of a few values, or the light's current one moved by a number of ulps about the 32-ulp edge.
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

void NoteFloats(const char *label, const float *f, unsigned n)
{
    drawfx::Call c;
    c.note = label;
    for (unsigned i = 0; i < n; ++i) c.words.push_back(Bits(f[i]));
    drawfx::gLog.push_back(std::move(c));
}
void NoteFloats(const char *label, std::initializer_list<float> f) { NoteFloats(label, f.begin(), unsigned(f.size())); }

// A clone of `n` (mode, iid; a context the light does not ask), held when answered.
void CloneOf(cIStandardLight *n, std::mt19937 &rng)
{
    const uint32_t iids[] = {cIStandardLight::kIid, cILight::kIid, sgfx::kIidGraph, cIReferentNode::kIid, 0x12345678};
    const uint32_t mode = rng() % 3, iid = iids[rng() % 5];
    sgfx::MakeCloneContext(w.context, nullptr);
    // The ambient and directional Clones make standard lights (the nTSSG lights' included); the point light's (the
    // half-space light's too) makes the game's point light.
    const Kind from = KindOf(n);
    const Kind made = from == kGamePoint || from == kHalfspacePoint ? kGamePoint : kPlain;
    void *out = reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
    n->Clone(mode, iid, &out, rng() % 2 ? reinterpret_cast<cINodeCloneContext *>(&w.context) : nullptr);
    // Every iid asked for is answered with the node face, or not at all (out untouched: the clone stays uncounted).
    const bool answered = out != reinterpret_cast<void *>(uintptr_t(0x5e5e5e5e));
    drawfx::Note("clone", {mode, iid, uint8_t(answered), answered && out ? CountOf(static_cast<cIStandardLight *>(out)) : 0});
    if (answered && out) {
        cIStandardLight *clone = static_cast<cIStandardLight *>(out);
        if (Hold(clone, true, made)) drawfx::Note("clone held", w.count - 1);
    }
}

// The height-map shadow light's strengths (0x3c93b0 / 0x3c9570) ask its height map, which the test's have not.
bool Unlit(cIStandardLight *n) { return KindOf(n) == kBinaryShadow; }

// One random operation on a random light through one of its faces, its results noted.
void Operate(difftest::Context &ctx, const Makers &m, int k, std::mt19937 &rng)
{
    cIStandardLight *n = w.lights[rng() % w.count];
    cISGResource *face = n->GetResource();
    const unsigned op = rng() % 20;
    drawfx::Note("op", op);
    char step[32];
    std::snprintf(step, sizeof(step), "step %d, op %u", k, op);
    sgfx::MarkStep(ctx, step);
    switch (op) {
    case 0: { // QueryInterface through any face
        const uint32_t iids[] = {cIStandardLight::kIid, cILight::kIid, cIReferentNode::kIid, sgfx::kIidGraph, 0x5b0aaa74,
                                 0xbb11a53e, 1, 0x3b253dc7, 0xfb859324, cISGResource::kIid, 0x9b11bf70,
                                 cGZPersistResource::kIid, 0x12345678, sgfx::kIidTree};
        const uint32_t iid = iids[rng() % 14];
        const unsigned via = rng() % 4;
        cIGZUnknown *on = via == 0   ? static_cast<cIGZUnknown *>(n)
                          : via == 1 ? static_cast<cIGZUnknown *>(PersistFace(n))
                          : via == 2 ? static_cast<cIGZUnknown *>(PartFace(n))
                                     : static_cast<cIGZUnknown *>(face);
        void *out = nullptr;
        const uint32_t before = CountOf(n);
        const bool r = rng() % 8 ? on->QueryInterface(iid, &out) : on->QueryInterface(iid, nullptr);
        drawfx::Note("query", {via, iid, uint8_t(r), drawfx::Norm(P(out)), CountOf(n) - before});
        if (out && CountOf(n) != before) static_cast<cIGZUnknown *>(out)->Release();
        break;
    }
    case 1: { // AddRef / Release through any face, the count through the part's
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
    case 2: { // the colour
        float now[3];
        drawfx::Note("colour returns", {n->GetColor(now) == now ? 1u : 0u});
        float rgb[3];
        for (unsigned i = 0; i < 3; ++i) rgb[i] = Component(rng, now[i]);
        n->SetColor(rgb);
        n->GetColor(now);
        NoteFloats("colour", now, 3);
        float dir[3] = {0.0f, 1.0f, 0.0f};
        // A whole box (min, max) and a whole transform (the game's point lights read all of both; the standard lights
        // neither): no stack past an array read.
        const float box[6] = {-0.5f, 0.25f, -1.0f, 0.5f, 1.0f, 0.75f};
        AffineTransform3 toLight{0, {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}, {0.5f, 1.0f, -0.25f}};
        if (!Unlit(n))
            NoteFloats("strength", {n->StrengthOverBox(box, &toLight), n->StrengthAt(dir)});
        break;
    }
    case 3: { // intensity and factor
        if (rng() % 2)
            n->SetIntensity(Scalar(rng, n->GetIntensity()));
        else
            n->SetFactor(Scalar(rng, n->GetFactor()));
        float dir[3] = {1.0f, 0.0f, 0.0f};
        if (Unlit(n))
            NoteFloats("scalars", {n->GetIntensity(), n->GetFactor()});
        else
            NoteFloats("scalars", {n->GetIntensity(), n->GetFactor(), n->StrengthOverBox(dir, reinterpret_cast<AffineTransform3 *>(dir)), n->StrengthAt(dir)});
        break;
    }
    case 4: { // the name
        const char *names[] = {"alpha", "sun_lght", "", "beta"};
        n->SetLightName(names[rng() % 4]);
        sgfx::NoteText("name", n->GetLightName());
        break;
    }
    case 5: { // the parameters and the constant slots
        float params[9];
        for (float &p : params) p = -7.0f;
        const uint32_t unused = rng();
        if (KindOf(n) != kAmbientFromLightMap) n->GetParams(params, unused); // its own reads the light map (none here)
        NoteFloats("params", params, 9);
        sgfx::NoteText("name", n->GetLightName()); // null until op 4 names the light
        NoteFloats("constants", {n->GetFalloff(), n->GetRange(), n->GetConeAngle(), n->GetConeFalloffAngle()});
        drawfx::Note("light", {n->GetLightType(), drawfx::Norm(P(n->GetBoundShape())), drawfx::Norm(P(n->GetStandardLight())),
                               drawfx::Norm(P(n->GetStandardLightConst()))});
        break;
    }
    case 6: { // the no-op setters, then a change: what the light announces
        n->SetFalloff(2.0f);
        n->SetRange(5.0f);
        n->Update();
        n->SetConeAngle(0.5f);
        n->SetConeFalloffAngle(0.75f);
        NoteFloats("after no-ops", {n->GetFalloff(), n->GetRange(), n->GetConeAngle(), n->GetConeFalloffAngle()});
        break;
    }
    case 7: { // referrers: added, removed (only one in the list), counted
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
    case 8: { // notifications and queries: the referrers first, then the listeners
        const uint32_t events[] = {cIStandardLight::kChangedEvent, 0x1234};
        n->NotifyListeners(events[rng() % 2], rng() % 3, rng() % 2 ? NullEventBody() : nullptr);
        drawfx::Note("asked", uint8_t(n->AskListeners(events[rng() % 2], rng() % 3, nullptr)));
        drawfx::Note("queried", uint8_t(n->OnQuery(0x1234, 0, nullptr)));
        break;
    }
    case 9: { // extensions and the tag
        Fake &e = w.ext[rng() % 3];
        auto *ext = reinterpret_cast<cIExtension *>(&e);
        if (n->HasExtension(ext))
            n->RemoveExtension(ext);
        else if (!e.value[sgfx::kExtOwnerWord])
            n->AddExtension(ext);
        const char *texts[] = {"alpha", "sun_lght", "", nullptr};
        n->SetTag(texts[rng() % 4]);
        sgfx::NoteText("tag", n->GetTag());
        drawfx::Note("extensions", {drawfx::Norm(P(n->GetFirstExtension())), uint8_t(n->HasExtension(ext)), n->GetTagHandle()});
        break;
    }
    case 10: { // kinds
        const uint32_t iids[] = {cIStandardLight::kIid, cILight::kIid, cIReferentNode::kIid, sgfx::kIidGraph,
                                 cISGResource::kIid, 0x3b253dc7, 0x5b0aaa74, 1, 0, sgfx::kIidTree, 0xc9c81b9b};
        for (uint32_t iid : iids) drawfx::Note("kind", {iid, uint8_t(n->IsKindOf(iid))});
        break;
    }
    case 11: { // the virtual base: segment, flag, the three assignments, name, key
        switch (rng() % 5) {
        case 0: drawfx::Note("set segment", uint8_t(face->SetSegment(Segment(rng)))); break;
        case 1: face->SetFlag(uint8_t(rng())); break;
        case 2: drawfx::Note("assign", face->AssignAndSubmit(rng() % 2 ? "sun_lght" : "beta", Segment(rng))); break;
        case 3: drawfx::Note("open assign", face->OpenAssignAndSubmit("sun_lght", rng(), Segment(rng))); break;
        default: drawfx::Note("reset", face->ResetAndSubmit(rng(), rng())); break;
        }
        sgfx::NoteText("resource name", face->GetName());
        cGZPersistResourceKey key{};
        face->CopyKeyTo(&key);
        drawfx::Note("resource", {face->GetFlag(), face->HasKey(), drawfx::Norm(P(face->GetSegment())), face->ForwardKey(),
                                  key.type, key.group, key.instance, key.resource});
        break;
    }
    case 12: { // the virtual base's other slots, the resource getters
        sgfx::NoteText("record 18", static_cast<const char *>(face->GetRecord18()));
        sgfx::NoteText("record 19", static_cast<const char *>(face->GetRecord19()));
        cISGResource *other = w.lights[rng() % w.count]->GetResource();
        drawfx::Note("misc", {drawfx::Norm(P(face->GetSubResource(rng() % 2))), drawfx::Norm(P(face->GetPersistResourceConst())),
                              uint32_t(face->CompareIdentity(other)), face->SubmitKeyToService(),
                              drawfx::Norm(P(n->GetResourceConst())), drawfx::Norm(P(n->GetReferentConst()))});
        break;
    }
    case 13: { // the part's key slots
        cIGZPersistResource *part = PartFace(n);
        part->SetGroup(rng());
        part->SetInstance(rng() % 2 ? rng() : 0);
        const cGZPersistResourceKey key = part->GetKey();
        drawfx::Note("key", {key.type, key.group, key.instance, key.resource});
        break;
    }
    case 14: // the owner, through both persist faces
        drawfx::Note("owner info", {drawfx::Norm(PersistFace(n)->GetOwnerInfo()), drawfx::Norm(face->GetOwnerInfo())});
        break;
    case 15: { // a type-info record, through either persist face
        cPersistTypeInfo info = rng() % 2 ? PersistFace(n)->GetTypeInfo() : face->GetTypeInfo();
        sgfx::NoteTypeInfo("type", &info);
        if (info.mName) info.mName->Release();
        break;
    }
    case 16: // written, through either persist face
        if (rng() % 2)
            PersistFace(n)->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        else
            face->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        break;
    case 17: CloneOf(n, rng); break;
    case 18: // a new light
        Make(m, rng);
        break;
    default: { // an event to the light itself
        n->OnEvent(rng() % 2 ? 0x5b51e806u : 0x777u, 0, nullptr, 0);
        drawfx::Note("active", n->IsActive());
        break;
    }
    }
}

// A light's record for the reader: the leaf's version, cStandardLightBase's, the resource's, the levels', the name and
// the five floats.
void FeedLightRecord(std::mt19937 &rng)
{
    sgfx::Reader &r = sgfx::gReader;
    // A fresh queue for each record (GameBuilt feeds several per case; the queues hold kMax): what an earlier read left
    // unread is dropped. The type names and the fake stay as they are.
    r.versionCount = r.versionAt = r.wordCount = r.wordAt = r.refCount = r.refAt = r.textCount = r.textAt = 0;
    r.Version(1);                 // the leaf's record
    r.Version(rng() % 2 ? 0xb : rng() % 16); // cStandardLightBase's
    r.Version(rng() % 3);         // cSGResource's (first)
    r.Text(rng() % 2 ? "sun_lght" : "");
    r.Version(1);                 // cLightT's
    r.Version(rng() % 3);         // cSGResource's (again)
    r.Text(rng() % 2 ? "sun_lght" : "beta");
    r.Version(1);                 // the referent Impl's
    r.Version(4);                 // the object-graph Impl's
    const uint32_t extensions = rng() % 2;
    r.Word(extensions);
    if (extensions) r.Ref(&w.ext[1]);
    r.Text(rng() % 2 ? "alpha" : "");
    r.Text(rng() % 2 ? "sun_lght" : "beta"); // the light's name
    const float values[] = {1.5f, 0.75f, 0.2f, 0.4f, 0.6f, 0.0f, -2.0f};
    for (unsigned i = 0; i < 5; ++i) r.Float(values[rng() % 7]);
}

// The record GetEventAttribute copies for an unknown event (0x106ab68, this instantiation's) is all zeros in the
// unstarted image, so a wrong record address would read zeros as well: the Events test fills it with a pattern.
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

} // namespace

DIFF_TEST(SG_StandardLights_Factory, kAmbientFactory)
{
    sgfx::RegistryScope registry(kLightRegistry);
    sgfx::CheckSides(ctx, "factory", 4, Build, [&](bool original, std::mt19937 &) {
        const Makers m = MakersFor(ctx, original);
        cIStandardLight *a = m.ambient();
        cIStandardLight *d = m.directional();
        drawfx::Note("counts", {CountOf(a), CountOf(d)});
        sgfx::NoteText("unnamed", a->GetLightName()); // null before a name is set
        sgfx::NoteText("unnamed", d->GetLightName());
        Hold(a, false);
        Hold(d, false);
        NoteLights("new light");
        ReleaseAll();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_StandardLights_Slots, kAmbientFactory)
{
    sgfx::RegistryScope registry(kLightRegistry);
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

// The light level as a base part of the game's own lights (lane TL): cLightT<cIStandardLight>'s constructor 0xa45a40, called
// by cStandardLightBase's constructor 0xa46250 inside the game's ambient and directional constructors. Both sides build
// the game's lights with the game's factories and run the same operations on them; on the second side 0xa45a40 is patched
// to our compiler's constructor of the template (cLightT_StandardLight_Constructor), which with the most-derived flag 0
// builds the cSGResource part, the node and the referent exactly where the game's does. Every word the lights hold
// (the vtables the game's derived constructors store are the same on both sides) is compared, as are the blocks freed.
DIFF_TEST(SG_StandardLights_Level, 0xa45a40)
{
    sgfx::RegistryScope registry(kLightRegistry);
    sgfx::CheckSides(ctx, "level", kSeeds, Build, [&](bool original, std::mt19937 &rng) {
        std::optional<shape::CodePatch> level;
        if (!original) level.emplace(0xa45a40u, &cLightT_StandardLight_Constructor);
        const Makers m{ctx.Original<Factory>(kAmbientFactory), ctx.Original<Factory>(kDirectionalFactory)};
        MakeLights(m, rng);
        NoteLights("built");
        for (int k = 0; k < 24; ++k) Operate(ctx, m, k, rng);
        NoteLights("after");
        sgfx::gReader.NoteNames();
        ReleaseAll();
    });
    w.manager.Drop();
}

DIFF_TEST(SG_StandardLights_Persist, kAmbientFactory)
{
    sgfx::RegistryScope registry(kLightRegistry);
    sgfx::CheckSides(ctx, "persist", kSeeds, Build, [&](bool original, std::mt19937 &rng) {
        const Makers m = MakersFor(ctx, original);
        MakeLights(m, rng);
        cIStandardLight *n = w.lights[rng() % w.count];
        if (rng() % 2) n->SetTag("alpha");
        if (rng() % 2) n->AddExtension(reinterpret_cast<cIExtension *>(&w.ext[0]));
        if (rng() % 2) n->SetLightName(rng() % 2 ? "sun_lght" : "");
        const float rgb[3] = {0.5f, 0.25f, 1.0f};
        if (rng() % 2) n->SetColor(rgb);
        if (rng() % 2) n->SetIntensity(2.0f);
        if (rng() % 2) n->GetResource()->AssignAndSubmit("sun_lght", nullptr);
        if (rng() % 2)
            PersistFace(n)->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        else
            n->GetResource()->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));

        // Read into a fresh light (or a clone of a held one), through either persist face.
        cIStandardLight *fresh = nullptr;
        if (rng() % 3 == 0) {
            const unsigned before = w.count;
            CloneOf(w.lights[rng() % w.count], rng);
            if (w.count > before) fresh = w.lights[w.count - 1];
        }
        drawfx::Note("read into", {fresh != nullptr});
        if (!fresh) fresh = Make(m, rng);
        if (!fresh) return;
        sgfx::Reader &r = sgfx::gReader;
        FeedLightRecord(rng);
        if (rng() % 2)
            PersistFace(fresh)->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
        else
            fresh->GetResource()->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
        drawfx::Note("read", {r.wordAt, r.refAt, r.textAt, r.versionAt});
        sgfx::NoteText("name", fresh->GetLightName());
        NoteLights("after");
        sgfx::gReader.NoteNames();
        PersistFace(fresh)->Write(reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        ReleaseAll();
    });
    w.manager.Drop();
}

// The behaviour with a connection manager (ours, in the instantiation's cell): connections made and dropped through the
// lights' slots 8 / 9, changes announced to the listeners and the referrers, queries, the registry's slots, and the last
// Release disconnecting everything.
DIFF_TEST(SG_StandardLights_Events, kAmbientFactory)
{
    sgfx::RegistryScope registry(kLightRegistry);
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
        const uint32_t events[] = {cIStandardLight::kChangedEvent, 0x1234};
        for (int k = 0; k < 12; ++k) {
            cIStandardLight *n = w.lights[rng() % w.count];
            Fake &l = listeners[rng() % 2];
            const uint32_t event = events[rng() % 2];
            const unsigned op = rng() % 7;
            drawfx::Note("op", op);
            switch (op) {
            case 0: n->Connect(event, reinterpret_cast<cIGZUnknown *>(&l), 0x500 + (rng() % 2)); break;
            case 1: n->Disconnect(event, reinterpret_cast<cIGZUnknown *>(&l), 0x500 + (rng() % 2)); break;
            case 2: { // a change (the light announces it)
                const float rgb[3] = {float(rng() % 4), 0.5f, 0.25f};
                n->SetColor(rgb);
                break;
            }
            case 3: drawfx::Note("asked", uint8_t(n->AskListeners(event, rng(), nullptr))); break;
            case 4: // the active bit shares the count word: each face's count must mask it
                drawfx::Note("active", {n->IsActive(), CountOf(n), PartFace(n)->GetRefCount(),
                                         n->GetResource()->GetPersistResource()->GetRefCount()});
                break;
            case 5: n->AddReferrer(reinterpret_cast<nGZEvent::cIAbstractBehavior *>(&w.referrer[rng() % 3])); break;
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

namespace {

// The game's lights built on the standard lights (lane VW): the point light (factory 0xa47c90, constructor 0xa46060 over
// cStandardLightBase's 0xa46250) and three nTSSG lights, each built by the game's own constructor in a block from the
// game's operator new 0x824632: cAmbientFromLightMapLight 0x3c7d30 (over cAmbientLight's 0xa45d40),
// cHeightMapBinaryShadowLight 0x3c7e80 (over cDirectionalLight's 0xa45db0) and cHalfspacePointLight 0x3c7de0 (over the
// point light's). Their vtables are the game's on both sides and hold the standard lights' bodies, so on the second side
// every body hooked to ours is patched to ours (the table below, the hook table's rows): the constructors and destructors
// the game's run as base parts, and the slot bodies the game's vtables reach.
using Adapters = cStandardLightAdapters;
struct Patch {
    uint32_t rva;
    const void *ours;
};
const Patch kPatches[] = {
    {0xa46250, &cStandardLightBase_Constructor},
    {0xa45d40, &cAmbientLight_Constructor},
    {0xa45db0, &cDirectionalLight_Constructor},
    {0xa467e0, &cStandardLightBase_Destructor},
    {0xa465f0, &cAmbientLight_Destructor},
    {0xa46630, &cDirectionalLight_Destructor},
    {0xa48a70, reinterpret_cast<const void *>(&Adapters::QueryInterface)},
    {0x3c9030, reinterpret_cast<const void *>(&Adapters::Release)},
    {0xa488a0, reinterpret_cast<const void *>(&Adapters::OnLightChanged)},
    {0xa48230, reinterpret_cast<const void *>(&Adapters::AmbientOnLightChanged)},
    {0xa482b0, reinterpret_cast<const void *>(&Adapters::DirectionalOnLightChanged)},
    {0x3c89e3, reinterpret_cast<const void *>(&Adapters::NodeQueryInterface)},
    {0x3c9038, reinterpret_cast<const void *>(&Adapters::NodeRelease)},
    {0x3c9930, reinterpret_cast<const void *>(&Adapters::NotifyListeners)},
    {0x3c87b0, reinterpret_cast<const void *>(&Adapters::AskListeners)},
    {0xa481e0, reinterpret_cast<const void *>(&Adapters::GetEventCount)},
    {0xa46c50, reinterpret_cast<const void *>(&Adapters::GetEventIds)},
    {0xa4a5d0, reinterpret_cast<const void *>(&Adapters::IsKindOf)},
    {0xa46c40, reinterpret_cast<const void *>(&Adapters::GetStandardLight)},
    {0xa4a1f0, reinterpret_cast<const void *>(&Adapters::SetLightName)},
    {0xa49f40, reinterpret_cast<const void *>(&Adapters::SetColor)},
    {0xa4a110, reinterpret_cast<const void *>(&Adapters::SetIntensity)},
    {0xa4a4c0, reinterpret_cast<const void *>(&Adapters::SetFactor)},
    {0xa480a0, reinterpret_cast<const void *>(&Adapters::GetLightName)},
    {0xa47400, reinterpret_cast<const void *>(&Adapters::GetColor)},
    {0xa48cc0, reinterpret_cast<const void *>(&Adapters::GetRange)},
    {0xa47080, reinterpret_cast<const void *>(&Adapters::AmbientClone)},
    {0xa47100, reinterpret_cast<const void *>(&Adapters::DirectionalClone)},
    {0xa48b40, reinterpret_cast<const void *>(&Adapters::DirectionalGetParams)},
    {0x3c89eb, reinterpret_cast<const void *>(&Adapters::PersistQueryInterface)},
    {0x3c9040, reinterpret_cast<const void *>(&Adapters::PersistRelease)},
    {0xa49070, reinterpret_cast<const void *>(&Adapters::Read)},
    {0xa4b250, reinterpret_cast<const void *>(&Adapters::Write)},
    {0xa4a6d0, reinterpret_cast<const void *>(&Adapters::AmbientGetTypeInfo)},
    {0xa4ade0, reinterpret_cast<const void *>(&Adapters::AmbientWrite)},
    {0xa4a790, reinterpret_cast<const void *>(&Adapters::DirectionalGetTypeInfo)},
    {0xa4ae60, reinterpret_cast<const void *>(&Adapters::DirectionalWrite)},
    {0xa48140, reinterpret_cast<const void *>(&Adapters::AmbientRecord19)},
    {0xa48150, reinterpret_cast<const void *>(&Adapters::DirectionalRecord19)},
    {0xa47ff0, reinterpret_cast<const void *>(&Adapters::CopyTo)},
};

constexpr uint32_t kGameNew = 0x824632, kPointFactory = 0xa47c90;
constexpr uint32_t kAmbientFromLightMapConstructor = 0x3c7d30, kBinaryShadowConstructor = 0x3c7e80,
                   kHalfspacePointConstructor = 0x3c7de0;
using GameNew = void *(__cdecl *)(size_t);
// The constructors (thiscall: `this` in ecx, edx unused; the last argument the most-derived flag).
using AmbientFromLightMapCtor = void *(__fastcall *)(void *self, void *, cIGZUnknown *map, float a, float b, int flag);
using BinaryShadowCtor = void *(__fastcall *)(void *self, void *, cIGZUnknown *map, const float *direction, float a,
                                              float b, int flag);
using HalfspacePointCtor = void *(__fastcall *)(void *self, void *, const float *plane, const float *point, int flag);

cIStandardLight *NodeOf(void *object) { return reinterpret_cast<cIStandardLight *>(static_cast<uint8_t *>(object) + kNode); }

// A light of any kind: a standard light (each side's own factory), the game's point light, or an nTSSG light.
void MakeAny(difftest::Context &ctx, const Makers &m, std::mt19937 &rng)
{
    const Kind kind = Kind(rng() % 5);
    const float values[] = {0.0f, 0.5f, 1.0f, -2.0f, 3.25f};
    const float a = values[rng() % 5], b = values[rng() % 5];
    const float direction[3] = {values[rng() % 5], -0.75f, values[rng() % 5]};
    const float plane[3] = {values[rng() % 5], 1.0f, values[rng() % 5]};
    const GameNew alloc = ctx.Original<GameNew>(kGameNew);
    cIStandardLight *n = nullptr;
    switch (kind) {
    case kPlain: n = rng() % 2 ? m.ambient() : m.directional(); break;
    case kGamePoint: n = ctx.Original<Factory>(kPointFactory)(); break;
    case kAmbientFromLightMap:
        n = NodeOf(ctx.Original<AmbientFromLightMapCtor>(kAmbientFromLightMapConstructor)(alloc(kKindSize[kind]), nullptr,
                                                                                          nullptr, a, b, 1));
        break;
    case kBinaryShadow:
        n = NodeOf(ctx.Original<BinaryShadowCtor>(kBinaryShadowConstructor)(alloc(kKindSize[kind]), nullptr, nullptr,
                                                                            direction, a, b, 1));
        break;
    default:
        n = NodeOf(ctx.Original<HalfspacePointCtor>(kHalfspacePointConstructor)(alloc(kKindSize[kind]), nullptr, plane,
                                                                                direction, 1));
        break;
    }
    drawfx::Note("made", {uint32_t(kind), CountOf(n)});
    // The constructors leave the count at 0 (the factories' AddRef is the caller's): held uncounted, then released once.
    Hold(n, false, kind);
}

// A walked and released id range.
void NoteIdRange(nGZEvent::cAttributeIDRange range)
{
    unsigned steps = 0;
    while (steps < 12 && !range.mCurrent->IsEqual(range.mEnd)) {
        drawfx::Note("id", range.mCurrent->Get());
        range.mCurrent->Next();
        ++steps;
    }
    nGZEvent::ReleaseIteratorRange(reinterpret_cast<cIGZUnknown *const *>(&range));
}

} // namespace

DIFF_TEST(SG_StandardLights_GameBuilt, 0xa46250)
{
    sgfx::RegistryScope registry(kLightRegistry);
    svcfx::AllocatorScope allocator; // the point light's settings key is a service string
    sgfx::CheckSides(ctx, "game built", kSeeds, Build, [&](bool original, std::mt19937 &rng) {
        std::list<shape::CodePatch> patches;
        if (!original)
            for (const Patch &p : kPatches) patches.emplace_back(p.rva, p.ours);
        const Makers m = MakersFor(ctx, original);
        const unsigned count = 1 + rng() % 5;
        for (unsigned i = 0; i < count; ++i) MakeAny(ctx, m, rng);
        NoteLights("built");
        for (int k = 0; k < 24; ++k) {
            cIStandardLight *n = w.lights[rng() % w.count];
            switch (rng() % 5) {
            case 0: // the event ids
                drawfx::Note("event count", n->GetEventCount());
                NoteIdRange(n->GetEventIds());
                break;
            case 1: { // read into a new light of any kind (as the game reads: into a fresh object), through either face
                const unsigned before = w.count;
                MakeAny(ctx, m, rng);
                if (w.count == before) break;
                n = w.lights[w.count - 1];
                FeedLightRecord(rng);
                sgfx::Reader &r = sgfx::gReader;
                if (rng() % 2)
                    PersistFace(n)->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
                else
                    n->GetResource()->Read(reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
                drawfx::Note("read", {r.wordAt, r.refAt, r.textAt, r.versionAt});
                sgfx::NoteText("name", n->GetLightName());
                break;
            }
            case 2: MakeAny(ctx, m, rng); break;
            default: Operate(ctx, m, k, rng); break;
            }
        }
        NoteLights("after");
        sgfx::gReader.NoteNames();
        ReleaseAll();
    });
    w.manager.Drop();
}
