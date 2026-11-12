// Differential tests of nGZSceneGraph's free helpers (lane 261111b; src/sg/SGUtilities.h), part 1: the property
// readers and the transforms. The tree and extension helpers are in sg_utilities_tree.cpp.
//
//   SG_Utilities_ReadFloat / ReadInt / ReadBool / ReadVector3   0xaf7190 / 0xaf7210 / 0xaf7110 / 0xaf7290
//   SG_Utilities_RelativeTransform                              0xaeb2c0
//   SG_Utilities_AimNode                                        0xaeee30
//   SG_Utilities_ProjectBox                                     0xaf22d0
//
// The readers run against a parser fake (slot 27 FindVariable answers a seed's variable texts by name, or null; slot 23
// GetFunctions a real EvalFunctionTable, shared by both sides). Each side's evaluator is its own: the original reader
// reaches the game's 0xb0a870, ours our ExpressionEvaluator (verified, lane NO); the texts use no built-in names, so
// neither reaches a built-in. The texts (no exponents, no quotients: an evaluation error throws): fixed-point numbers
// exact in 8 places (small integers, integers up to 1e7, halves), -0 / +0, infinities as FLT_MAX's digits doubled and
// NaN as their difference, comma lists of 1-4 elements, and names with no variable; the parser is null in some calls.
// The old values are
// drawn alike (NaN, 0 / -0, the new value, its neighbours up to 40 units in the last place away), `changed` is null in
// one call in four and its byte starts random. Noted: al, the value words (and the word after them), the changed byte.
//
// The transforms run on node fakes whose slot 54 GetWorldTransform hands out the node's 7 floats (random, the rotation
// normalised); RelativeTransform's output is noted (with the word after it) and the slot order logged. AimNode logs the
// node's SetRotation (slot 65, four floats) for random directions (unit, long, zero, opposite to -Z). ProjectBox runs
// with each side's projector 0xaf25a0 (ours since lane 261111d, tested alone in sg_utilities_project.cpp), for random
// boxes and matrices (elements 13 / 14 zero in some, NaN in others), the two corners noted.
#include "sgfx.h"

#include "gz/EvalFunctionTable.h"
#include "sg/SGNodeInterfaces.h"
#include "sg/SGUtilities.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <type_traits>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

constexpr unsigned kSeeds = 64;
constexpr uint32_t kSse2Flag = 0x110a38c; // the CRT's SSE2 flag (gz_expression_evaluator.cpp): the game's state

// ---- the parser ----

constexpr unsigned kParserSlotCount = 28;
constexpr std::array<Slot, kParserSlotCount> ParserSlots()
{
    std::array<Slot, kParserSlotCount> r{};
    for (unsigned i = 0; i < kParserSlotCount; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[0] = {2, Ret::Int, true, "QueryInterface", "ui"};
    r[1] = {0, Ret::Int, true, "AddRef", ""};
    r[2] = {0, Ret::Int, true, "Release", ""};
    r[23] = {0, Ret::Int, true, "GetFunctions", ""};
    r[27] = {1, Ret::Int, true, "FindVariable", "s"};
    return r;
}
constexpr auto kParserSlots = ParserSlots();
constexpr drawfx::Iface kParser{"Parser", kParserSlotCount, kParserSlots.data()};

constexpr unsigned kVariables = 6;
const char *const kNames[kVariables + 1] = {"alpha", "beta", "gamma", "delta", "epsilon", "zeta", "missing"};

struct ParserWorld {
    Fake parser;
    std::string texts[kVariables];
    float first[kVariables][4]; // each variable's elements as strtof reads them
    nGZCommandParser::EvalFunctionTable *functions = nullptr;
} gp;

uint32_t FindVariable(Fake &, const uint32_t *a)
{
    const char *name = reinterpret_cast<const char *>(uintptr_t(a[0]));
    for (unsigned i = 0; i < kVariables; ++i)
        if (std::strcmp(name, kNames[i]) == 0) return drawfx::P(gp.texts[i].c_str());
    return 0;
}

uint32_t GetFunctions(Fake &, const uint32_t *) { return drawfx::P(gp.functions); }

// One element's text and its value.
std::string ElementText(std::mt19937 &rng, float &value)
{
    // FLT_MAX in digits (the number reader takes no exponent), doubled in float: infinity; less itself: NaN.
    static const std::string kInf = "(340282346638528859811704183484516925440*2)";
    char buf[64];
    switch (rng() % 10) {
    case 0: value = std::numeric_limits<float>::quiet_NaN(); return kInf + "-" + kInf;
    case 1: value = (rng() & 1) ? INFINITY : -INFINITY; return (value > 0 ? "" : "-") + kInf;
    case 2: value = (rng() & 1) ? -0.0f : 0.0f; return std::signbit(value) ? "-0" : "0";
    case 3: value = float(int(rng() % 9) - 4); break;
    case 4: value = float(int(rng() % 20000001) - 10000000); break; // integers past the rounding's exact range
    case 5: value = (float(rng() % 200001) - 100000.0f) * 0.5f; break; // halves: ties
    default: value = std::ldexp(float(int(rng() % 2000001) - 1000000), -int(rng() % 9)); break; // exact in 8 places
    }
    std::snprintf(buf, sizeof buf, "%.8f", double(value));
    return buf;
}

void BuildParser(uint32_t seed, drawfx::Cells &cells)
{
    cells.Set(kSse2Flag, 1u);
    gp.parser.Reset<kParser>(500);
    gp.parser.On(23, &GetFunctions);
    gp.parser.On(27, &FindVariable);
    std::mt19937 rng(seed * 2654435761u + 0xaf7190u);
    for (unsigned i = 0; i < kVariables; ++i) {
        const unsigned count = (rng() % 4 == 0) ? 1 + rng() % 4 : (rng() & 1) ? 1 : 3 + rng() % 2;
        std::string text;
        for (unsigned k = 0; k < count; ++k) {
            float v;
            const std::string element = ElementText(rng, v);
            if (k < 4) gp.first[i][k] = v;
            text += (k ? ", " : "") + element;
        }
        for (unsigned k = count; k < 4; ++k) gp.first[i][k] = 0;
        gp.texts[i] = text;
    }
}

float Neighbour(std::mt19937 &rng, float v)
{
    const int32_t step = int32_t(rng() % 81) - 40;
    return std::bit_cast<float>(std::bit_cast<uint32_t>(v) + uint32_t(step));
}

// A value `step` units in the last place from v on the line through zero (-0 and +0 one point), so steps from a zero
// or a denormal cross the sign; the steps near the vector reader's 31 / 32 boundary come up most.
float OrderedNeighbour(std::mt19937 &rng, float v)
{
    static const int32_t kSteps[] = {31, 32, -31, -32, 30, 33, -30, -33, 1, -1};
    const int32_t step = rng() % 3 ? kSteps[rng() % 10] : int32_t(rng() % 81) - 40;
    const uint32_t u = std::bit_cast<uint32_t>(v);
    const int32_t line = (u >> 31) ? -int32_t(u & 0x7fffffffu) : int32_t(u);
    const int32_t moved = line + step;
    return std::bit_cast<float>(moved < 0 ? 0x80000000u | uint32_t(-moved) : uint32_t(moved));
}

// An old value for element k of variable `which` (or of a missing one).
float OldValue(std::mt19937 &rng, unsigned which, unsigned k)
{
    const float now = which < kVariables ? gp.first[which][k] : 1.0f;
    if (now == 0.0f && rng() % 2) { // a denormal across zero, at the vector reader's boundary (31 in, 32 out)
        static const int32_t kAcross[] = {31, 31, 31, 32, 30, 33};
        const uint32_t ulps = uint32_t(kAcross[rng() % 6]);
        return std::bit_cast<float>(rng() % 4 ? 0x80000000u | ulps : ulps);
    }
    switch (rng() % 10) {
    case 0: return std::numeric_limits<float>::quiet_NaN();
    case 1: return (rng() & 1) ? -0.0f : 0.0f;
    case 2: case 3: return now;
    case 4: case 5: return Neighbour(rng, now);
    case 6: case 7: return OrderedNeighbour(rng, now);
    default: return std::bit_cast<float>(uint32_t(rng()));
    }
}

template <class T> using Reader = uint8_t(__cdecl *)(void *, const char *, T *, uint8_t *);

// `words` value words (with a guard word after them), each call's result noted.
template <class T, unsigned Words, class Ours>
void CheckReader(difftest::Context &ctx, const char *what, uint32_t rva, Ours ours)
{
    const auto original = ctx.Original<Reader<T>>(rva);
    nGZCommandParser::EvalFunctionTable table; // the image's built-ins (verified constructor, lane HL), for both sides
    gp.functions = &table;
    sgfx::CheckSides(ctx, what, kSeeds, [](uint32_t seed, drawfx::Cells &cells) { BuildParser(seed, cells); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         for (unsigned call = 0; call < 24; ++call) {
                             const unsigned which = rng() % (kVariables + 1);
                             void *parser = rng() % 10 ? static_cast<void *>(&gp.parser) : nullptr;
                             uint32_t words[Words + 1];
                             for (unsigned k = 0; k < Words; ++k)
                                 words[k] = std::bit_cast<uint32_t>(OldValue(rng, which, k));
                             if constexpr (std::is_same_v<T, int32_t>)
                                 if (rng() & 1) words[0] = uint32_t(int32_t(gp.first[which % kVariables][0] + 0.5f));
                             if constexpr (std::is_same_v<T, uint8_t>) words[0] = rng() % 4 ? rng() & 1 : rng();
                             words[Words] = 0xa5a5a5a5u;
                             uint8_t changedByte = uint8_t(rng());
                             uint8_t *changed = rng() % 4 ? &changedByte : nullptr;
                             drawfx::Note("call", {which, parser != nullptr, changed != nullptr, words[0]});
                             T *value = reinterpret_cast<T *>(words);
                             const uint8_t al = isOriginal ? original(parser, kNames[which], value, changed)
                                                           : uint8_t(ours(parser, kNames[which], value, changed));
                             drawfx::Note("al", al);
                             for (unsigned k = 0; k <= Words; ++k) drawfx::Note("word", words[k]);
                             drawfx::Note("changed", changedByte);
                         }
                     });
}

// ---- the transforms ----

constexpr unsigned kXfSlotCount = 67;
constexpr std::array<Slot, kXfSlotCount> XfSlots()
{
    std::array<Slot, kXfSlotCount> r{};
    for (unsigned i = 0; i < 61; ++i) r[i] = sgfx::kNodeSlots[i];
    for (unsigned i = 61; i < kXfSlotCount; ++i) r[i] = {0, Ret::Int, false, "", ""};
    r[65] = {1, Ret::Int, true, "SetRotation", "F4"};
    return r;
}
constexpr auto kXfSlots = XfSlots();
constexpr drawfx::Iface kXfNode{"XfNode", kXfSlotCount, kXfSlots.data()};

struct XfWorld {
    Fake nodes[2];
    float transforms[2][7];
} gx;

// Slot 54 (hidden out): the node's transform (value[0] its index).
uint32_t WorldTransform(Fake &f, const uint32_t *a)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), gx.transforms[f.value[0]], 7 * sizeof(float));
    return a[0];
}

float Draw(std::mt19937 &rng, float span) { return (float(rng() % 2000001) / 1000000.0f - 1.0f) * span; }

void BuildTransforms(uint32_t seed)
{
    std::mt19937 rng(seed * 2654435761u + 0xaeb2c0u);
    for (unsigned i = 0; i < 2; ++i) {
        Fake &n = gx.nodes[i];
        n.Reset<kXfNode>(600 + i);
        n.value[0] = i;
        n.On(54, &WorldTransform);
        float *t = gx.transforms[i];
        for (unsigned k = 0; k < 3; ++k) t[k] = Draw(rng, 100.0f);
        float len = 0;
        for (unsigned k = 3; k < 7; ++k) len += (t[k] = Draw(rng, 1.0f)) * t[k];
        len = std::sqrt(len);
        for (unsigned k = 3; k < 7; ++k) t[k] = len > 0 ? t[k] / len : (k == 6 ? 1.0f : 0.0f);
        if (rng() % 8 == 0) for (unsigned k = 3; k < 7; ++k) t[k] = k == 6 ? 1.0f : 0.0f; // identity rotation
    }
}

cICompositionTreeNode *AsNode(Fake &f) { return reinterpret_cast<cICompositionTreeNode *>(&f); }

// ---- the projector ----

struct ProjectWorld {
    float box[6];
    float matrix[16];
} gj;

void BuildProjection(uint32_t seed, std::mt19937 &rng)
{
    (void)seed;
    for (unsigned k = 0; k < 3; ++k) {
        const float a = Draw(rng, 50.0f), b = Draw(rng, 50.0f);
        gj.box[k] = std::fmin(a, b);
        gj.box[3 + k] = std::fmax(a, b);
    }
    for (unsigned k = 0; k < 16; ++k) gj.matrix[k] = Draw(rng, 2.0f);
    switch (rng() % 5) {
    case 0: gj.matrix[13] = gj.matrix[14] = 0.0f; break;
    case 1: gj.matrix[13] = 0.0f; break;
    case 2: gj.matrix[14] = 0.0f; break;
    case 3: gj.matrix[rng() & 1 ? 13 : 14] = std::numeric_limits<float>::quiet_NaN(); break;
    default: break;
    }
}

} // namespace

DIFF_TEST(SG_Utilities_ReadFloat, 0xaf7190)
{
    CheckReader<float, 1>(ctx, "ReadFloatProperty", 0xaf7190, &ReadFloatProperty);
}

DIFF_TEST(SG_Utilities_ReadInt, 0xaf7210)
{
    CheckReader<int32_t, 1>(ctx, "ReadIntProperty", 0xaf7210, &ReadIntProperty);
}

DIFF_TEST(SG_Utilities_ReadBool, 0xaf7110)
{
    CheckReader<uint8_t, 1>(ctx, "ReadBoolProperty", 0xaf7110, &ReadBoolProperty);
}

DIFF_TEST(SG_Utilities_ReadVector3, 0xaf7290)
{
    CheckReader<float, 4>(ctx, "ReadVector3Property", 0xaf7290, &ReadVector3Property);
}

DIFF_TEST(SG_Utilities_RelativeTransform, 0xaeb2c0)
{
    using Fn = const float *(__cdecl *)(float *, void *, void *);
    const Fn original = ctx.Original<Fn>(0xaeb2c0);
    sgfx::CheckSides(ctx, "RelativeTransform", kSeeds, [](uint32_t seed, drawfx::Cells &) { BuildTransforms(seed); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         for (unsigned call = 0; call < 3; ++call) {
                             const unsigned bone = rng() & 1, node = rng() % 3 ? 1 - bone : bone;
                             uint32_t out[8];
                             for (uint32_t &w : out) w = 0x5a5a5a5au;
                             drawfx::Note("call", {bone, node});
                             const float *r = isOriginal ? original(reinterpret_cast<float *>(out), &gx.nodes[bone],
                                                                    &gx.nodes[node])
                                                         : RelativeTransform(reinterpret_cast<float *>(out),
                                                                             AsNode(gx.nodes[bone]), AsNode(gx.nodes[node]));
                             drawfx::Note("result", r == reinterpret_cast<float *>(out));
                             for (uint32_t w : out) drawfx::Note("word", w);
                         }
                     });
}

DIFF_TEST(SG_Utilities_AimNode, 0xaeee30)
{
    using Fn = void(__cdecl *)(void *, const float *);
    const Fn original = ctx.Original<Fn>(0xaeee30);
    sgfx::CheckSides(ctx, "AimNode", kSeeds, [](uint32_t seed, drawfx::Cells &) { BuildTransforms(seed); },
                     [&](bool isOriginal, std::mt19937 &rng) {
                         for (unsigned call = 0; call < 4; ++call) {
                             float dir[3];
                             switch (rng() % 6) {
                             case 0: dir[0] = 0, dir[1] = 0, dir[2] = 1; break;  // opposite to -Z
                             case 1: dir[0] = 0, dir[1] = 0, dir[2] = -1; break; // along it
                             case 2: dir[0] = dir[1] = dir[2] = 0; break;
                             default: {
                                 for (float &d : dir) d = Draw(rng, 3.0f);
                                 if (rng() & 1) {
                                     const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
                                     if (len > 0) for (float &d : dir) d /= len;
                                 }
                             }
                             }
                             Fake &node = gx.nodes[rng() & 1];
                             drawfx::Note("call", {node.id, std::bit_cast<uint32_t>(dir[0]),
                                                   std::bit_cast<uint32_t>(dir[1]), std::bit_cast<uint32_t>(dir[2])});
                             if (isOriginal)
                                 original(&node, dir);
                             else
                                 AimNode(reinterpret_cast<cITransformNode *>(&node), dir);
                         }
                     });
}

DIFF_TEST(SG_Utilities_ProjectBox, 0xaf22d0)
{
    using Fn = void(__cdecl *)(float *, float *, const void *, const float *);
    const Fn original = ctx.Original<Fn>(0xaf22d0);
    sgfx::CheckSides(ctx, "ProjectBox", 256, [](uint32_t, drawfx::Cells &) {},
                     [&](bool isOriginal, std::mt19937 &rng) {
                         BuildProjection(0, rng);
                         uint32_t lo[4], hi[4];
                         for (unsigned k = 0; k < 4; ++k) lo[k] = hi[k] = 0x5a5a5a5au;
                         if (isOriginal)
                             original(reinterpret_cast<float *>(lo), reinterpret_cast<float *>(hi), gj.box, gj.matrix);
                         else
                             ProjectBox(reinterpret_cast<float *>(lo), reinterpret_cast<float *>(hi), gj.box, gj.matrix);
                         for (unsigned k = 0; k < 4; ++k) drawfx::Note("lo", lo[k]);
                         for (unsigned k = 0; k < 4; ++k) drawfx::Note("hi", hi[k]);
                     });
}
