// Differential tests of the shader program constants' sources (src/sg/ShaderConstantSources.h): FillFloatConstants
// 0xaba8f0 and FillIntConstants 0xab7cd0, each against the image's on the same drawn world, every source 0..0x2f.
//
// The world (drawn per seed, the same for both sides):
//   - a context of drawn words with chosen floats where the sources read (+4 the frame number, +8 the time, the frames
//     at +0xdc and +0x110, the matrices at +0x144, +0x184 and +0x1c4) and +0x284 null or an outer context drawn alike;
//   - an item: a bone source (+0xc, slot 5 notes the set and gives a bone set of drawn transform indices), the set
//     (+0x14), values (+0x20, null now and then), scalars (+0x24), 16 transforms (+0x28), an event source (+0x2c: slot
//     10 notes the event's vtable, words and question and fills it, slot 54 notes and gives a placement), the run
//     nibbles (+0x30: mostly in order, now and then any for the sources whose loops the count bounds), the counts word
//     (+0x38) and a holder table (+0x3c) of 256 holders after three ambient floats (null now and then for 0x16);
//   - each holder: a light (slot 50 notes its id and whether it was given the item, fills eight drawn parameters; slot
//     42 a modulator's colour (2 arguments) or, at nibble 4, a depth range (none)), a node (slot 54) and 17 drawn words;
//   - the detail object in its cell (0x10f9028; slots 45..47 note and give a matrix and two vectors) and the light
//     direction 0x106f428;
//   - the placements: a drawn rigid, given as it is or copied into the caller's scratch.
// The real matrix helpers (0x19c0e0, 0x19c1c0, 0x1a5eb0, 0x1a6410, 0x1a7200, 0x19c800) run on both sides; the frame
// helpers, QuaternionToMatrix and GetDetailObject are the image's on one side and ours on the other.
// Compared: every fake call in order, then the whole destination (1024 words, filled with a pattern first).
#include "difftest.h"

#include "sg/ShaderConstantSources.h"

#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

using namespace nGZSceneGraph;

namespace {

using Binding = cApplyShaderProgramConstants::Binding;

std::vector<uint32_t> gLog;
uintptr_t gImageBase;

void Note(std::initializer_list<uint32_t> words)
{
    gLog.insert(gLog.end(), words.begin(), words.end());
    gLog.push_back(0xeeeeeeee);
}

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// Chosen floats: zeros, ones, halves, values whose integer conversion overflows, infinities and NaNs; or a drawn one.
float PickFloat(std::mt19937 &rng)
{
    static const float picks[] = {0.0f,   -0.0f, 1.0f,    -1.0f,  0.5f,   2.5f,    -3.75f, 1000.0f, 1e-3f,
                                  3e9f,   -3e9f, 2.1e9f,  8388609.5f, std::numeric_limits<float>::infinity(),
                                  -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()};
    constexpr unsigned n = sizeof(picks) / sizeof(picks[0]);
    if (rng() % 3 == 0) return picks[rng() % n];
    return static_cast<float>(static_cast<int32_t>(rng() % 200001) - 100000) / static_cast<float>(1 + rng() % 997);
}
// Mostly ordinary (the matrix helpers' sums stay finite), now and then a chosen one.
float PickTame(std::mt19937 &rng)
{
    if (rng() % 16 == 0) return PickFloat(rng);
    return static_cast<float>(static_cast<int32_t>(rng() % 20001) - 10000) / static_cast<float>(1 + rng() % 97);
}

// ---- the world ----
alignas(16) uint8_t gContext[2][0x2c0];
alignas(16) uint8_t gItem[0x50];
uint32_t gTransforms[16][13];
alignas(4) uint8_t gValues[0x120];
float gScalars[64];
float gRigids[4][8];

void *gFakeVTable[60]; // every slot a trap unless set
struct Fake {
    void **vt;
    uint32_t id;
};

uint32_t __fastcall Trap(Fake *self, void *)
{
    Note({0xdead, self->id});
    return 0;
}

// The bone source (slot 5).
struct BoneSet {
    uint8_t before[0x14];
    const int32_t *indices;
};
int32_t gBoneIndices[64];
BoneSet gBoneSet;
void *gBoneVTable[8];
Fake gBones{gBoneVTable, 0xb0};
const BoneSet *__fastcall BoneGet(Fake *self, void *, uint32_t set)
{
    Note({5, self->id, set});
    return &gBoneSet;
}

// Placements (slot 54) of the event source and the nodes: a drawn rigid, or a copy of it in the scratch.
uint32_t gPlacementMode[300];
const void *__fastcall GetPlacement(Fake *self, void *, float *scratch)
{
    Note({54, self->id});
    const float *const rigid = gRigids[self->id % 4];
    if (gPlacementMode[self->id % 300] == 0) return rigid;
    std::memcpy(scratch, rigid, sizeof gRigids[0]);
    return scratch;
}

// The event source (slot 10): notes the event's vtable (an RVA), its words and the question; fills its four floats.
float gEventOut[4];
void *gEventVTable[60];
Fake gEventSource{gEventVTable, 0xe0};
Fake *gEventHolder = &gEventSource;
void __fastcall GetEvent(Fake *self, void *, uint32_t *event, uint32_t type, int32_t index)
{
    Note({10, self->id, event[0] - static_cast<uint32_t>(gImageBase), event[1], event[2], event[3], event[4], type,
          static_cast<uint32_t>(index)});
    std::memcpy(event + 1, gEventOut, sizeof gEventOut);
}

// The holders: a light (slot 50; slot 42 a modulator's or a depth range's), a node (slot 54) and 17 words.
struct Holder {
    Fake *light;
    Fake *node;
    uint32_t words[17];
};
constexpr int kHolders = 256;
Holder gHolders[kHolders];
Fake gLights[kHolders];
Fake gNodes[kHolders];
float gParams[kHolders][8];
float gColours[kHolders][4][4];
float gRange[2];
void *gModulatorVTable[60];
void *gRangeVTable[60];
void *gNodeVTable[60];
struct HolderTable {
    float ambient[3];
    Holder *entries[kHolders];
};
HolderTable gTable;

void __fastcall GetParams(Fake *self, void *, float *out, uint32_t item)
{
    Note({50, self->id, uint32_t(item == reinterpret_cast<uintptr_t>(gItem))});
    std::memcpy(out, gParams[self->id % kHolders], sizeof gParams[0]);
}
const float *__fastcall GetColour(Fake *self, void *, float *colour, int32_t index)
{
    Note({42, self->id, static_cast<uint32_t>(index)});
    std::memcpy(colour, gColours[self->id % kHolders][index & 3], 16);
    return colour;
}
const float *__fastcall GetRange(Fake *self, void *)
{
    Note({0x42a, self->id});
    return gRange;
}

// The detail object (slots 45..47).
float gDetailMatrix[16];
float gDetailA[4];
float gDetailB[4];
void *gDetailVTable[60];
Fake gDetail{gDetailVTable, 0xd0};
const float *__fastcall DetailMatrix(Fake *, void *)
{
    Note({45});
    return gDetailMatrix;
}
const float *__fastcall DetailA(Fake *, void *)
{
    Note({46});
    return gDetailA;
}
const float *__fastcall DetailB(Fake *, void *)
{
    Note({47});
    return gDetailB;
}

constexpr uint32_t kDetailCell = 0x10f9028;
constexpr uint32_t kLightDirection = 0x106f428;

template <class T> void Put(uint8_t *at, T v) { std::memcpy(at, &v, sizeof v); }

void FillVTable(void **vt, int slots)
{
    for (int i = 0; i < slots; ++i) vt[i] = reinterpret_cast<void *>(&Trap);
}

// A frame: flags then 12 floats.
void DrawFrame(std::mt19937 &rng, uint8_t *at)
{
    Put<uint32_t>(at, rng() % 4 ? rng() % 8 : rng());
    for (int i = 0; i < 12; ++i) Put(at + 4 + 4 * i, PickTame(rng));
}

void DrawContext(std::mt19937 &rng, uint8_t *c)
{
    for (int i = 0; i < 0x2c0; ++i) c[i] = static_cast<uint8_t>(rng());
    Put<uint32_t>(c + 4, rng() % 2 ? rng() % 100000 : rng());
    float time;
    switch (rng() % 6) {
    case 0: time = PickFloat(rng); break;
    case 1: time = static_cast<float>(rng() % 100000); break;                    // whole
    case 2: time = static_cast<float>(rng() % 100000) + 0.5f; break;             // halves
    case 3: time = static_cast<float>(1 + rng() % 0x7fffff) * 2.0f; break;       // beyond 2^23 now and then
    default: time = static_cast<float>(rng() % 1000000) / 64.0f; break;
    }
    Put(c + 8, time);
    DrawFrame(rng, c + 0xdc);
    DrawFrame(rng, c + 0x110);
    for (int i = 0; i < 48; ++i) Put(c + 0x144 + 4 * i, PickTame(rng));
    Put<const void *>(c + 0x284, nullptr);
}

// Sources whose light loops the count bounds (any run nibbles are safe there).
bool CountBounded(uint8_t source)
{
    return source == 0x1b || source == 0x1c || source == 0x1d || (source >= 0x1f && source <= 0x26) ||
           source == 0x28 || source == 0x29 || source > 0x2a;
}

Binding DrawBinding(std::mt19937 &rng)
{
    Binding b;
    uint32_t w[8];
    for (uint32_t &x : w) x = rng();
    std::memcpy(&b, w, sizeof b);
    b.mSource = static_cast<uint8_t>(rng() % 16 ? rng() % 0x2b : 0x2b + rng() % 0xd5);
    const uint8_t s = b.mSource;
    if (s <= 5) b.mCount = 1 + rng() % 3; // a count of 0 runs on in the image
    else if ((s >= 6 && s <= 0xe) || s == 0x12 || s == 0x13) b.mCount = rng() % 5;
    else if (s == 0x1e) b.mCount = rng() % 4 ? 0x20 : rng() % 64;
    else if (s == 0x27) b.mCount = rng() % 4 ? 8 : rng() % 16;
    else if (s == 0x2a) b.mCount = 3;
    else if (s == 0x1d) b.mCount = rng() % 4 ? 4 * (rng() % 10) : rng() % 40;
    else b.mCount = rng() % 4 ? rng() % 12 : rng() % 40;
    b.mChannel = static_cast<uint8_t>(rng() % 6 ? rng() % 5 : rng() % 256);
    b.mDimension = static_cast<uint8_t>(rng() % 6 ? rng() % (5 - (b.mChannel < 5 ? b.mChannel : 4)) : rng() % 256);
    b.mOffset = static_cast<int32_t>(rng() % 0x100);
    for (float &v : b.mValues) v = PickFloat(rng);
    return b;
}

void BuildWorld(std::mt19937 &rng, const Binding &b)
{
    DrawContext(rng, gContext[0]);
    DrawContext(rng, gContext[1]);
    if (rng() % 2) Put<const void *>(gContext[0] + 0x284, gContext[1]);

    for (auto &t : gTransforms) {
        DrawFrame(rng, reinterpret_cast<uint8_t *>(t));
        if (rng() % 8 == 0) t[1 + rng() % 12] = rng(); // any word
    }
    for (uint8_t &v : gValues) v = static_cast<uint8_t>(rng());
    for (int i = 0; i < 0x120; i += 4)
        if (rng() % 2) Put(gValues + i, PickFloat(rng));
    for (float &f : gScalars) f = PickFloat(rng);
    for (auto &r : gRigids)
        for (float &f : r) f = PickTame(rng);
    for (uint32_t &m : gPlacementMode) m = rng() % 2;
    for (int32_t &i : gBoneIndices) i = static_cast<int32_t>(rng() % 16);
    gBoneSet.indices = gBoneIndices;
    for (float &f : gEventOut) f = PickFloat(rng);

    FillVTable(gFakeVTable, 60);
    FillVTable(gBoneVTable, 8);
    gBoneVTable[5] = reinterpret_cast<void *>(&BoneGet);
    FillVTable(gEventVTable, 60);
    gEventVTable[10] = reinterpret_cast<void *>(&GetEvent);
    gEventVTable[54] = reinterpret_cast<void *>(&GetPlacement);
    FillVTable(gModulatorVTable, 60);
    gModulatorVTable[42] = reinterpret_cast<void *>(&GetColour);
    gModulatorVTable[50] = reinterpret_cast<void *>(&GetParams);
    FillVTable(gRangeVTable, 60);
    gRangeVTable[42] = reinterpret_cast<void *>(&GetRange);
    gRangeVTable[50] = reinterpret_cast<void *>(&GetParams);
    FillVTable(gNodeVTable, 60);
    gNodeVTable[54] = reinterpret_cast<void *>(&GetPlacement);
    FillVTable(gDetailVTable, 60);
    gDetailVTable[45] = reinterpret_cast<void *>(&DetailMatrix);
    gDetailVTable[46] = reinterpret_cast<void *>(&DetailA);
    gDetailVTable[47] = reinterpret_cast<void *>(&DetailB);

    // The run nibbles: mostly in order (some runs empty), any for the sources the count bounds.
    uint32_t runs = 0;
    if (CountBounded(b.mSource) && rng() % 4 == 0) {
        runs = rng() & 0xffffff;
    } else {
        uint32_t at = 0;
        for (int k = 0; k < 6; ++k) {
            if (rng() % 3) at += rng() % (k == 3 ? 5 : 4);
            if (at > 15) at = 15;
            runs |= at << (4 * k);
        }
    }
    runs |= rng() & 0xff000000u;
    const uint32_t rangeAt = runs >> 16 & 0xf;
    for (int i = 0; i < kHolders; ++i) {
        gLights[i] = {i == static_cast<int>(rangeAt) ? gRangeVTable : gModulatorVTable, static_cast<uint32_t>(i)};
        gNodes[i] = {gNodeVTable, static_cast<uint32_t>(0x100 + i)};
        gHolders[i].light = &gLights[i];
        gHolders[i].node = &gNodes[i];
        for (uint32_t &w : gHolders[i].words) w = Bits(PickFloat(rng));
        gTable.entries[i] = &gHolders[i];
        for (float &p : gParams[i]) p = rng() % 3 ? PickTame(rng) : PickFloat(rng);
        for (auto &c : gColours[i])
            for (float &f : c) f = PickFloat(rng);
    }
    for (float &f : gTable.ambient) f = PickFloat(rng);
    for (float &f : gRange) f = rng() % 8 ? PickTame(rng) : PickFloat(rng);
    if (rng() % 8 == 0) gRange[1] = gRange[0]; // an empty range

    for (uint8_t &x : gItem) x = static_cast<uint8_t>(rng());
    Put<const void *>(gItem + 0xc, &gBones);
    Put<uint16_t>(gItem + 0x14, static_cast<uint16_t>(rng()));
    Put<const void *>(gItem + 0x20, rng() % 4 ? gValues : nullptr);
    Put<const void *>(gItem + 0x24, gScalars);
    Put<const void *>(gItem + 0x28, gTransforms);
    Put<const void *>(gItem + 0x2c, &gEventHolder);
    Put<uint32_t>(gItem + 0x30, runs);
    Put<uint32_t>(gItem + 0x38, rng());
    Put<const void *>(gItem + 0x3c, b.mSource == 0x16 && rng() % 3 == 0 ? nullptr : gTable.entries);

    for (float &f : gDetailMatrix) f = PickTame(rng);
    for (float &f : gDetailA) f = PickFloat(rng);
    for (float &f : gDetailB) f = PickFloat(rng);
    *reinterpret_cast<void **>(gImageBase + kDetailCell) = &gDetail;
    float *const direction = reinterpret_cast<float *>(gImageBase + kLightDirection);
    for (int i = 0; i < 3; ++i) direction[i] = rng() % 8 ? PickTame(rng) : PickFloat(rng);
    if (rng() % 16 == 0) direction[0] = direction[1] = direction[2] = 0.0f;
}

constexpr uint32_t kDstWords = 1024;
uint32_t gDst[kDstWords];

template <class T>
using Fill = void(__cdecl *)(T *, const Binding *, const cShaderApplyContext *, const cShaderApplyItem *);

template <class T>
void CheckFill(difftest::Context &ctx, const char *name, unsigned seeds, Fill<T> original, Fill<T> ours)
{
    gImageBase = ctx.image.Base();
    // The two game globals the world draws are put back after the run: the texture matrix tests read the light direction
    // as the image has it, and once failed after this test in a shuffled order (VH, difftest --shuffle 11).
    struct RestoreGlobals {
        void *cell;
        float direction[3];
        RestoreGlobals()
        {
            cell = *reinterpret_cast<void **>(gImageBase + kDetailCell);
            std::memcpy(direction, reinterpret_cast<const void *>(gImageBase + kLightDirection), sizeof direction);
        }
        ~RestoreGlobals()
        {
            *reinterpret_cast<void **>(gImageBase + kDetailCell) = cell;
            std::memcpy(reinterpret_cast<void *>(gImageBase + kLightDirection), direction, sizeof direction);
        }
    } restore;
    gLog.reserve(1 << 14);
    unsigned perSource[0x2b] = {};
    for (unsigned seed = 0; seed < seeds; ++seed) {
        std::vector<uint32_t> logs[2];
        uint8_t source = 0;
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rng(seed * 7919u + 11u);
            const Binding b = DrawBinding(rng);
            source = b.mSource;
            BuildWorld(rng, b);
            for (uint32_t i = 0; i < kDstWords; ++i) gDst[i] = 0xcd000000u | i;
            gLog.clear();
            char mark[96];
            std::snprintf(mark, sizeof mark, "%s seed %u source %#x %s", name, seed, b.mSource,
                          side == 0 ? "original" : "ours");
            ctx.Mark(mark);
            (side == 0 ? original : ours)(reinterpret_cast<T *>(gDst), &b,
                                          reinterpret_cast<const cShaderApplyContext *>(gContext[0]),
                                          reinterpret_cast<const cShaderApplyItem *>(gItem));
            gLog.insert(gLog.end(), gDst, gDst + kDstWords);
            logs[side] = gLog;
        }
        if (source < 0x2b) ++perSource[source];
        ++ctx.cases;
        if (logs[0] != logs[1]) {
            size_t i = 0;
            while (i < logs[0].size() && i < logs[1].size() && logs[0][i] == logs[1][i]) ++i;
            const size_t fromEnd = logs[0].size() - i;
            ctx.Fail("%s seed %u source %#x: logs differ at word %zu (%zu from the end; original %zu words, ours %zu): "
                     "%08x vs %08x",
                     name, seed, source, i, fromEnd, logs[0].size(), logs[1].size(),
                     i < logs[0].size() ? logs[0][i] : 0, i < logs[1].size() ? logs[1][i] : 0);
            return;
        }
    }
    for (unsigned s = 0; s < 0x2b; ++s)
        if (perSource[s] < 40) ctx.Fail("%s: source %#x drawn only %u times", name, s, perSource[s]);
}


} // namespace

DIFF_TEST(sg_fill_float_constants, 0xaba8f0)
{
    CheckFill<float>(ctx, "float", 6000, ctx.Original<Fill<float>>(0xaba8f0), &FillFloatConstants);
}

DIFF_TEST(sg_fill_int_constants, 0xab7cd0)
{
    CheckFill<int32_t>(ctx, "int", 6000, ctx.Original<Fill<int32_t>>(0xab7cd0), &FillIntConstants);
}
