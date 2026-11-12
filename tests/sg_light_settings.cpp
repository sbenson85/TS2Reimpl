// Differential tests of nTSSG::cLightSettings (lane 261108v, scenegraph LG7b; src/sg/cLightSettings.h).
//
//   SG_LightSettingsLife  one light settings object's life on each side: built by the constructor 0x3c6a30 on a block
//       filled alike (the name table noted: its 17 names in their sorted order), then random calls through its two
//       vtables: QueryInterface (+0 slot 0 and its +4 thunk) with its own id, 1 and another (the out word noted as the
//       face it got, or untouched), AddRef / Release (never the last), Init / Shutdown, Clear, SetName (short, long past
//       the string's buffer, empty, the same name in other case), ParseAttribute with each of the 17 attributes on keys
//       that are tags, "name:tag" in either case, or long, CopySettings between such keys (from = to too), GetName, and
//       Apply on a fake node tree (3-8 nodes, linked first child / next sibling; each node with the light flag 2 or not,
//       a tag or none from the same keys, a light with a standard light, a light without one, or none, and a
//       FindExtension that finds the diffusion face A, misses, or misses having written face B into `out`; the COM
//       director makes face B, or nothing only when the object holds no diffusion values: the original's null call).
//       After each call both maps are noted: each key's text and its values (the id, the words the record always holds,
//       and the value its kind writes), then the name and its count word. At the end the object is deleted (the last
//       Release, or the +4 face's slot 3 with flags 1, or with 0 and the block freed by the test), then the fakes'
//       counts and the heap.
//
// Stood in on both sides: the COM director 0x1ae18d (the COM fake: slot 3 GetClassObject), the game's operator new 0x824632
// / delete 0x824624 / free 0x82540f (ours, so the original's blocks are counted like ours), and the image's atexit 0x824cbe
// (a no-op: the original's empty-values static registers its destructor once). The name table's constructor
// (0xaf6560, ours verified), the enumeration lookup (0xaf6df0, ours verified) and the cRZString bodies run on each side as
// each side's code calls them.
// The parser face fake: EvalBool / EvalFloat (30 / 31) answer from a sequence; EvalVector2 / EvalVector3 / EvalColorRGB
// (34 / 35 / 39) write the next floats to `out` and return it, or one time in three return a buffer of their own (the
// code must read what they return).
#include "sgfx.h"

#include "code_patch.h"

#include "rz/StaticGetters.h"
#include "sg/cLightSettings.h"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <tuple>

namespace {

using namespace nTSSG;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

constexpr uint32_t kSize = 0x48;

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

// The light node: kNode's slots 0-60, then cILightRefNode's GetLight at 95; the slots Apply calls with their kinds.
constexpr std::array<Slot, 96> MakeNodeSlots()
{
    std::array<Slot, 96> t{};
    for (Slot &s : t) s = {0, Ret::Int, false, "", ""};
    for (unsigned i = 0; i < 61; ++i) t[i] = sgfx::kNodeSlots[i];
    t[22] = {1, Ret::Int, true, "AddExtension", "a"};
    t[24] = {3, Ret::Int, true, "FindExtension", "uaa"};
    t[42] = {1, Ret::Int, true, "GetChildren", "a"};
    t[48] = {1, Ret::Int, true, "TestFlag", "u"};
    t[95] = {0, Ret::Int, true, "GetLight", ""};
    return t;
}
constexpr auto kNodeSlots = MakeNodeSlots();
constexpr drawfx::Iface kLightNode{"LightNode", 96, kNodeSlots.data(), false, drawfx::kTestTable};

// nGZSceneGraph::cILight: 35 GetStandardLight.
constexpr At kLightAt[] = {{35, {0, Ret::Int, true, "GetStandardLight", ""}}};
constexpr auto kLightSlots = Table<36>(kLightAt);
constexpr drawfx::Iface kLight{"Light", 36, kLightSlots.data(), false, drawfx::kTestTable};

// nGZSceneGraph::cIStandardLight: the setters 41-45, 47, 48.
constexpr At kStandardAt[] = {{41, {1, Ret::Int, true, "SetColor", "F3"}},
                              {42, {1, Ret::Int, true, "SetIntensity", "f"}},
                              {43, {1, Ret::Int, true, "SetFactor", "f"}},
                              {44, {1, Ret::Int, true, "SetFalloff", "f"}},
                              {45, {1, Ret::Int, true, "SetRange", "f"}},
                              {47, {1, Ret::Int, true, "SetConeAngle", "f"}},
                              {48, {1, Ret::Int, true, "SetConeFalloffAngle", "f"}}};
constexpr auto kStandardSlots = Table<49>(kStandardAt);
constexpr drawfx::Iface kStandard{"StandardLight", 49, kStandardSlots.data(), false, drawfx::kTestTable};

// nTSSG::cIDiffusionLightExtension's own slots (src/sg/cIDiffusionLightExtension.h): the defaults 11-20.
constexpr std::array<Slot, 31> MakeDiffusionSlots()
{
    std::array<Slot, 31> t{};
    for (Slot &s : t) s = {0, Ret::Int, false, "", ""};
    t[11] = {1, Ret::Int, true, "SetDefaultSourceMin", "F2"};
    t[12] = {1, Ret::Int, true, "SetDefaultSourceMax", "F2"};
    t[13] = {1, Ret::Int, true, "SetDefaultSourceIntensity", "f"};
    t[14] = {1, Ret::Int, true, "SetDefaultSourceColor", "F3"};
    t[15] = {1, Ret::Int, true, "SetDefaultSourceIsDirectional", "b"};
    t[16] = {1, Ret::Int, true, "SetDefaultSourceFalloff", "f"};
    t[17] = {1, Ret::Int, true, "SetDefaultSourceDirectionalFalloff", "f"};
    t[18] = {1, Ret::Int, true, "SetDefaultSourceWallIntensityScale", "f"};
    t[19] = {1, Ret::Int, true, "SetDefaultSourceFloorIntensityScale", "f"};
    t[20] = {1, Ret::Int, true, "SetDefaultSourceInterFloorFalloff", "f"};
    return t;
}
constexpr auto kDiffusionSlots = MakeDiffusionSlots();
constexpr drawfx::Iface kDiffusion{"DiffusionFace", 31, kDiffusionSlots.data(), false, drawfx::kTestTable};

// The parser face (nGZSceneGraph::ParserFace, src/gz/MaterialCommands.h): 30, 31, 34, 35, 39.
constexpr At kParserAt[] = {{30, {1, Ret::Int, true, "EvalBool", "s"}},
                            {31, {1, Ret::Float, true, "EvalFloat", "s"}},
                            {34, {3, Ret::Int, true, "EvalVector2", "asu"}},
                            {35, {3, Ret::Int, true, "EvalVector3", "asu"}},
                            {39, {2, Ret::Int, true, "EvalColorRGB", "as"}}};
constexpr auto kParserSlots = Table<40>(kParserAt);
constexpr drawfx::Iface kParser{"Parser", 40, kParserSlots.data(), false, drawfx::kTestTable};

// cIGZCOM: 3 GetClassObject (class, iid, out).
constexpr At kComAt[] = {{3, {3, Ret::Int, true, "GetClassObject", "uua"}}};
constexpr auto kComSlots = Table<4>(kComAt);
constexpr drawfx::Iface kCom{"COM", 4, kComSlots.data(), false, drawfx::kTestTable};

constexpr unsigned kMaxNodes = 8;
constexpr unsigned kTagWord = sgfx::kReferentWord;        // the node's tag text (slot 26), or 0
constexpr unsigned kLightWord = sgfx::kFirstExtensionWord; // the node's light (slot 95), or 0
constexpr unsigned kFindWord = sgfx::kLimitWord;           // FindExtension: 0 miss, 1 face A, 2 miss with face B written

// The diffusion face: its own vtable (an embedded fake's thunks), a vbptr whose second entry leads to the extension fake
// (the virtual base cIExtension), as MSVC lays out `public virtual cIExtension`.
struct DiffusionFace {
    void *vt;
    const int32_t *vbptr;
    int32_t vbtable[2];
};

struct World {
    Fake nodes[kMaxNodes];
    unsigned nodeCount = 0;
    Fake lightWith, lightWithout, standard;
    Fake extA, extB, faceFakeA, faceFakeB;
    DiffusionFace faceA, faceB;
    Fake com, parser;
    bool comNull = false;
    uint32_t sequence = 0;
    float buffer[3] = {};

    uint32_t Next()
    {
        sequence = sequence * 1664525u + 1013904223u;
        return sequence >> 8;
    }
    float NextFloat()
    {
        static const float kOdd[] = {0.0f, -0.0f, 90.0f, 1e30f, std::bit_cast<float>(0x7fc00001u), -45.5f, 1.0f / 3.0f};
        const uint32_t r = Next();
        if (r % 7 == 0) return kOdd[(r / 7) % std::size(kOdd)];
        return float(int32_t(r % 2001) - 1000) * 0.0625f;
    }
};
World gw;

const char *const kTags[] = {"lamp", "LAMP", "sun", "spot", "nothing", "a_long_tag_past_the_buffer_x"};
const char *const kNames[] = {"rig", "RIG", "night", "", "a_rather_long_settings_name"};
const char *const kKeys[] = {"lamp",        "Lamp",        "sun",         "spot",  "rig:lamp", "RIG:sun",
                             "night:spot",  "rig:LAMP",    "a_long_tag_past_the_buffer_x",       ":lamp",
                             "a_rather_long_settings_name:lamp", "other"};
const char *const kAttributes[] = {"intensity",
                                   "relSpecIntensity",
                                   "color",
                                   "falloffRate",
                                   "range",
                                   "coneAngle",
                                   "conePenumbra",
                                   "diffusionSourceMin",
                                   "diffusionSourceMax",
                                   "diffusionSourceIntensity",
                                   "diffusionSourceColor",
                                   "diffusionSourceIsDirectional",
                                   "diffusionSourceFalloff",
                                   "diffusionSourceDirectionalFalloff",
                                   "diffusionSourceWallIntensityScale",
                                   "diffusionSourceFloorIntensityScale",
                                   "diffusionSourceInterFloorFalloff"};
const char *const kValues[] = {"1", "0.5 0.25", "1 2 3", "true", "-4"};

// ---- stand-ins ----
void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
void __cdecl GameFree(void *p) { ::operator delete(p); }
// The image's atexit (library code, 0x824cbe) works on its CRT's exit table, which the harness never initialises: the
// original's first lookup builds its empty vector (a thread-safe static, 0x10da42c) and registers its destructor there.
int __cdecl NoAtexit(void (*)()) { return 0; }
void *__cdecl StandCom()
{
    drawfx::Note("com", 0);
    return &gw.com;
}

// ---- fake handlers ----
uint32_t ComClassObject(Fake &, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[2]));
    if (gw.comNull) {
        *out = nullptr;
        return 0;
    }
    ++gw.extB.refs;
    *out = &gw.faceB;
    return 1;
}
uint32_t NodeFind(Fake &self, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[1]));
    switch (self.value[kFindWord]) {
    case 1: ++gw.extA.refs; *out = &gw.faceA; return 1;
    case 2: ++gw.extB.refs; *out = &gw.faceB; return 0;
    default: return 0;
    }
}
uint32_t ParserBool(Fake &, const uint32_t *) { return gw.Next() % 2; }
uint32_t ParserFloat(Fake &, const uint32_t *) { return std::bit_cast<uint32_t>(gw.NextFloat()); }
uint32_t Fill(unsigned count, uint32_t outWord)
{
    float *const out = reinterpret_cast<float *>(uintptr_t(outWord));
    float *const to = gw.Next() % 3 == 0 ? gw.buffer : out;
    for (unsigned i = 0; i < count; ++i) to[i] = gw.NextFloat();
    return drawfx::P(to);
}
uint32_t ParserVector2(Fake &, const uint32_t *a) { return Fill(2, a[0]); }
uint32_t ParserVector3(Fake &, const uint32_t *a) { return Fill(3, a[0]); }

void Build(uint32_t seed)
{
    std::mt19937 rng(seed * 31u + 7u);
    gw.sequence = seed;
    gw.comNull = false;
    gw.com.Reset<kCom>(460);
    gw.com.On(3, &ComClassObject);
    gw.parser.Reset<kParser>(470);
    gw.parser.On(30, &ParserBool);
    gw.parser.On(31, &ParserFloat);
    gw.parser.On(34, &ParserVector2);
    gw.parser.On(35, &ParserVector3);
    gw.parser.On(39, &ParserVector3);
    gw.standard.Reset<kStandard>(150);
    gw.lightWith.Reset<kLight>(151);
    gw.lightWith.Returns(35, &gw.standard);
    gw.lightWithout.Reset<kLight>(152);
    for (auto [ext, extId, faceFake, faceId, face] :
         {std::tuple{&gw.extA, 200u, &gw.faceFakeA, 300u, &gw.faceA}, std::tuple{&gw.extB, 201u, &gw.faceFakeB, 301u, &gw.faceB}}) {
        sgfx::MakeExtension(*ext, extId);
        faceFake->Reset<kDiffusion>(faceId);
        face->vbptr = face->vbtable;
        face->vbtable[0] = -4;
        face->vbtable[1] = int32_t(drawfx::P(ext) - drawfx::P(&face->vbptr));
        faceFake->Embed(face);
        drawfx::AddRange(face, sizeof(*face), 0x7d000000 | (faceId << 8));
    }
    gw.nodeCount = 3 + rng() % (kMaxNodes - 2);
    for (unsigned i = 0; i < gw.nodeCount; ++i) {
        Fake &n = gw.nodes[i];
        sgfx::MakeFlagNode<kLightNode>(n, 100 + i);
        n.On(24, &NodeFind);
        n.On(26, &sgfx::detail::GetWord<kTagWord>);
        n.On(95, &sgfx::detail::GetWord<kLightWord>);
        n.value[sgfx::kFlagsWord] = (rng() % 4 ? 4u : 0u) | (rng() % 2 ? 1u : 0u) | (rng() % 2 ? 8u : 0u);
        n.value[kTagWord] = rng() % 5 ? drawfx::P(kTags[rng() % std::size(kTags)]) : 0;
        const uint32_t light = rng() % 5;
        n.value[kLightWord] = light < 3 ? drawfx::P(&gw.lightWith) : light == 3 ? drawfx::P(&gw.lightWithout) : 0;
        n.value[kFindWord] = rng() % 3;
        if (i > 0) {
            Fake &parent = gw.nodes[rng() % i];
            n.value[sgfx::kNextWord] = parent.value[sgfx::kFirstChildWord];
            parent.value[sgfx::kFirstChildWord] = drawfx::P(&n);
        }
    }
}

// ---- calls ----
uint8_t *Bytes(void *p) { return static_cast<uint8_t *>(p); }
template <unsigned Slot, class R, class... A> R Virtual(void *face, A... args)
{
    using Fn = R(__thiscall *)(void *, A...);
    return reinterpret_cast<Fn>((*static_cast<void ***>(face))[Slot])(face, args...);
}
uint32_t RefCount(void *o) { return *reinterpret_cast<uint32_t *>(Bytes(o) + 8); }

// The record words: the id, +8..+0x1f (cleared by both), then what the kind writes.
void NoteValues(const char *label, const cLightSettings::ValueMap &map, bool diffusion)
{
    drawfx::Note(label, uint32_t(map.size()));
    for (const auto &[key, values] : map) {
        sgfx::NoteText("key", key.c_str());
        drawfx::Note("values", uint32_t(values.size()));
        for (const LightSettingValue &v : values) {
            const uint32_t *w = reinterpret_cast<const uint32_t *>(&v);
            const uint32_t id = v.id;
            uint32_t extra0 = 0, extra1 = 0;
            if (id == lightsetting::kSourceMin || id == lightsetting::kSourceMax) extra0 = w[8], extra1 = w[9];
            else if (id == lightsetting::kSourceIsDirectional) extra0 = v.flag;
            else if (id != lightsetting::kColor && id != lightsetting::kSourceColor) extra0 = w[1];
            drawfx::Note(diffusion ? "diffusion value" : "light value",
                         {id, w[2], w[3], w[4], w[5], w[6], w[7], extra0, extra1});
        }
    }
}
void NoteObject(void *o)
{
    NoteValues("light values", *reinterpret_cast<const cLightSettings::ValueMap *>(Bytes(o) + 0xc), false);
    NoteValues("diffusion values", *reinterpret_cast<const cLightSettings::ValueMap *>(Bytes(o) + 0x14), true);
    const std::string &name = *reinterpret_cast<const std::string *>(Bytes(o) + 0x20);
    sgfx::NoteText("name", name.c_str());
    drawfx::Note("words", {RefCount(o), *reinterpret_cast<uint32_t *>(Bytes(o) + 0x1c + 0x1c)});
}
void NoteNames(void *o)
{
    const auto &names = *reinterpret_cast<const NameValueTable *>(Bytes(o) + 0x3c);
    drawfx::Note("names", uint32_t(names.mEntries.size()));
    for (const auto &[text, value] : names.mEntries) {
        sgfx::NoteText("name text", text.c_str());
        drawfx::Note("name value", value);
    }
}
void NoteFakes()
{
    drawfx::Note("refs", {uint32_t(gw.extA.refs), uint32_t(gw.extB.refs), uint32_t(gw.standard.refs),
                          uint32_t(gw.lightWith.refs), uint32_t(gw.com.refs), uint32_t(gw.parser.refs)});
}
uint32_t FaceOf(void *o, void *out, void *untouched)
{
    if (out == untouched) return 0xdead;
    if (out == o) return 0;
    if (out == Bytes(o) + 4) return 4;
    return 0xbad;
}

bool HasDiffusion(void *o) { return !reinterpret_cast<const cLightSettings::ValueMap *>(Bytes(o) + 0x14)->empty(); }

void Step(difftest::Context &ctx, void *o, std::mt19937 &rng)
{
    const uint32_t op = rng() % 16;
    char mark[48];
    std::snprintf(mark, sizeof mark, "op %u", op);
    sgfx::MarkStep(ctx, mark);
    switch (op) {
    case 0: { // QueryInterface on either face
        static const uint32_t kIids[] = {cILightSettings::kIid, 1, 0x8a561e23, 0};
        const uint32_t iid = kIids[rng() % std::size(kIids)];
        void *const face = rng() % 3 ? o : Bytes(o) + 4;
        int untouchedMark = 0;
        void *out = &untouchedMark;
        const bool found = Virtual<0, bool>(face, iid, &out); // receiver: the face's slot 0
        drawfx::Note("query", {uint32_t(face != o), iid, uint32_t(found), FaceOf(o, out, &untouchedMark)});
        break;
    }
    case 1: drawfx::Note("addref", Virtual<1, uint32_t>(rng() % 2 ? o : Bytes(o) + 4)); break;
    case 2:
        if (RefCount(o) > 1) drawfx::Note("release", Virtual<2, uint32_t>(rng() % 2 ? o : Bytes(o) + 4));
        break;
    case 3: drawfx::Note("init", uint32_t(Virtual<3, bool>(o))); break;
    case 4: drawfx::Note("shutdown", uint32_t(Virtual<4, bool>(o))); break;
    case 5:
        if (rng() % 3 == 0) Virtual<5, void>(o);
        break;
    case 6: // the answer: the name string (the original's tail-called assign leaves it in eax)
        drawfx::Note("set name", uint32_t(Virtual<6, uint8_t *>(o, kNames[rng() % std::size(kNames)]) - Bytes(o)));
        break;
    case 7:
    case 8:
    case 9:
    case 10: {
        const char *key = kKeys[rng() % std::size(kKeys)];
        const char *attribute = kAttributes[rng() % std::size(kAttributes)];
        const char *value = kValues[rng() % std::size(kValues)];
        Virtual<7, void>(o, static_cast<void *>(&gw.parser), key, attribute, value);
        break;
    }
    case 11:
    case 12: {
        const char *from = kKeys[rng() % std::size(kKeys)];
        const char *to = rng() % 4 == 0 ? from : kKeys[rng() % std::size(kKeys)];
        Virtual<8, void>(o, from, to);
        break;
    }
    case 13:
    case 14: {
        // The COM director makes nothing only when no diffusion value can reach the null face.
        gw.comNull = !HasDiffusion(o) && rng() % 2;
        Fake &start = gw.nodes[rng() % 3 ? 0 : rng() % gw.nodeCount];
        Virtual<9, void>(o, static_cast<void *>(&start));
        NoteFakes();
        break;
    }
    default: sgfx::NoteText("get name", Virtual<10, const char *>(o)); break;
    }
}

void RunLife(difftest::Context &ctx, bool original, std::mt19937 &rng)
{
    difftest::AllocationCounter counter;
    void *const o = ::operator new(kSize);
    for (uint32_t i = 0; i < kSize; ++i) Bytes(o)[i] = uint8_t(rng());
    if (original) ctx.Original<void *(__thiscall *)(void *)>(0x3c6a30)(o);
    else cLightSettings::Construct(o, nullptr);
    NoteNames(o);
    NoteObject(o);
    drawfx::Note("addref", Virtual<1, uint32_t>(o));
    const unsigned steps = 10 + rng() % 70;
    for (unsigned i = 0; i < steps; ++i) {
        Step(ctx, o, rng);
        NoteObject(o);
    }
    sgfx::MarkStep(ctx, "delete");
    switch (rng() % 3) {
    case 0:
        while (RefCount(o) > 1) Virtual<2, uint32_t>(o);
        drawfx::Note("released", Virtual<2, uint32_t>(rng() % 2 ? o : Bytes(o) + 4));
        break;
    case 1: {
        // receiver: the +4 rz::cRZUnknown face, slot 3 (the deleting destructor's +4 thunk)
        void *const result = Virtual<3, void *>(Bytes(o) + 4, 1u);
        drawfx::Note("deleted", uint32_t(Bytes(result) - Bytes(o)));
        break;
    }
    default: {
        void *const result = Virtual<3, void *>(Bytes(o) + 4, 0u);
        drawfx::Note("destroyed", uint32_t(Bytes(result) - Bytes(o)));
        ::operator delete(o);
        break;
    }
    }
    NoteFakes();
    counter.Stop();
    drawfx::Note("heap", {uint32_t(counter.Blocks()), uint32_t(counter.Bytes())});
}

} // namespace

// Verifies: the constructor 0x3c6a30, the destructor 0x3c6b40 with the deleting destructor 0x3c6de0 and its +4 thunk
// 0x3c6dd0, QueryInterface 0x3c7c70 and its +4 thunk 0x3c7c97, Clear 0x3c7540, SetName 0x3c7ca0, ParseAttribute
// 0x3c6ec0 with the helpers 0x3c70c0 and 0x3c6e00, CopySettings 0x3c75a0, the lookups 0x3c79c0 and 0x3c7710, Apply
// 0x3c7180 and GetName 0x3c7590.
DIFF_TEST(SG_LightSettingsLife, 0x3c7180)
{
    shape::StandIn com{0x1ae18du, &GetDirectorFrameworkObject, &StandCom};
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
    shape::CodePatch gameFree{0x82540fu, shape::CodeOf(&GameFree)};
    shape::CodePatch atexitPatch{0x824cbeu, shape::CodeOf(&NoAtexit)};
    uint32_t current = 0;
    sgfx::CheckSides(
        ctx, "light settings", 400,
        [&](uint32_t seed, drawfx::Cells &) {
            current = seed;
            Build(seed);
        },
        [&](bool original, std::mt19937 &rng) {
            RunLife(ctx, original, rng);
            ++ctx.cases;
        });
    (void)current;
}
