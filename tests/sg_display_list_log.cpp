// Differential tests for the display list's item log (src/sg/cDisplayListLog.cpp): LogDrawItems 0xaa1640 (DrawPass's
// call when the log is open) and the shader context's description it writes, DescribeShaderContext 0xaf4c90.
//
// Fakes, hand-made (the log's slots take floats and 16-bit words, the material manager returns through a hidden pointer):
// the log (slots 7, 8, 9, 12, 14: each call with its value, a text by its characters and, for the game's own strings,
// its address), the material manager in GetCachedQueriedObject's cell 0x10f8fcc (GetInfo 33: the index; the info's
// first word is the name text), the vertex formats manager in its cell 0x10caa24 (GetFormat 8: the id) and its formats
// (Describe 16: a name of 0-40 characters written with the string's FromChar), the item's record nodes and the light
// nodes (GetDetailLimit 88), the lights (GetStandardLightConst 34: itself or none; GetParams 50: the item asked about
// and eight floats). The strings the functions build are cRZStrings on both sides (the game's on the original's side,
// formatted by its own CRT): the text that reaches the log is compared character for character, with floats of every
// kind (NaN payloads and signs, infinities, denormals, values past 1e30, negative zero).
//
// The items: random words, sub keys with every flag combination (and bits past them), depths of every kind, records
// with nodes, light states with monotonic per-type counts (the first nibble sometimes not zero), light blocks with
// their ambient colour or none. DescribeShaderContext's string is a recording fake (Sprintf 31: the format's address
// and its text; Append 20 and 19: the text and the length / the other string's text).
//
// Compared: every call in order with its arguments; the items, records and blocks after (both take them const).
// Neither function has a structured handler; their C++ unwinding (the local strings) is not exercised (no fake throws).
#include "code_patch.h"
#include "difftest.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "gz/cIGZString.h"
#include "sg/cDisplayList.h"
#include "sg/cDisplayListLog.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using nGZSceneGraph::DrawItem;
using nGZSceneGraph::LightRecord;
using nGZSceneGraph::NodeRecord;

namespace {

using Blob = std::vector<uint32_t>;
using svcfx::Ev;

constexpr uint32_t evLog = 0x10600, evInfo = 0x1a0033, evFormat = 0x1a0008, evDescribe = 0x1a0016,
                   evDetail = 0x1a0088, evStandard = 0x1a0034, evParams = 0x1a0050, evSprintf = 0x5f0031,
                   evAppend = 0x5f0020, evAppendString = 0x5f0019, evText = 0x7e7e00;

uint32_t gImageBase, gImageSize;

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

uint32_t Bits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
float FromBits(uint32_t b)
{
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

// A text: the game's own by its address (an RVA), any other as 0; then its characters.
void Text(const char *text, size_t length = SIZE_MAX)
{
    const uint32_t p = svcfx::P(text);
    Ev(p - gImageBase < gImageSize ? p - gImageBase : 0);
    for (size_t i = 0; i < length && text[i]; ++i) Ev(static_cast<uint8_t>(text[i]));
    Ev(evText);
}

// ---- the log ------------------------------------------------------------------------------------------------------------
struct FakeLog {
    void *const *vtable;
};
template <unsigned Slot> uint32_t __fastcall LogWord(FakeLog *, void *, uint32_t value)
{
    Ev(evLog + Slot);
    Ev(Slot == 7 ? value & 0xffff : value); // SetUint16's upper half is the caller's stack
    return 0x100 + Slot;
}
uint32_t __fastcall LogText(FakeLog *, void *, const char *text)
{
    Ev(evLog + 14);
    Text(text);
    return 0xabcdef01; // only al is the result
}
void *const *LogVTable()
{
    static void *table[15] = {};
    table[7] = reinterpret_cast<void *>(&LogWord<7>);
    table[8] = reinterpret_cast<void *>(&LogWord<8>);
    table[9] = reinterpret_cast<void *>(&LogWord<9>);
    table[12] = reinterpret_cast<void *>(&LogWord<12>);
    table[14] = reinterpret_cast<void *>(&LogText);
    return table;
}
FakeLog gLog;

// ---- the material manager, the formats ----------------------------------------------------------------------------------
struct FakeObject {
    void *const *vtable;
    uint32_t id;
};

const char *const kNames[] = {"", "a", "lightingMaterial_skin", "floorTile_0042_with_a_rather_long_material_name", "%s%d",
                              "x y\tz"};
constexpr unsigned kNameCount = sizeof kNames / sizeof kNames[0];

nGZSceneGraph::MaterialInfo *__fastcall MaterialInfo(FakeObject *, void *, nGZSceneGraph::MaterialInfo *out,
                                                     int32_t index)
{
    Ev(evInfo);
    Ev(static_cast<uint32_t>(index));
    out->nameHash = svcfx::P(kNames[static_cast<uint32_t>(index) % kNameCount]);
    out->material = nullptr;
    out->parsed = nullptr;
    out->references = index;
    out->variants = 3;
    return out;
}
void *const *MaterialVTable()
{
    static void *table[40] = {};
    table[33] = reinterpret_cast<void *>(&MaterialInfo);
    return table;
}
FakeObject gMaterials;

constexpr unsigned kFormats = 5;
FakeObject gFormats[kFormats];
FakeObject gFormatsManager;
const char *const kFormatNames[kFormats] = {"", "id:1{pos norm}", "id:7{pos norm tc tc tc blendindices blendweights}",
                                            "%d%f", "id:3"};

void __fastcall FormatDescribe(FakeObject *self, void *, cIGZString *text)
{
    Ev(evDescribe);
    Ev(self->id);
    text->FromChar(kFormatNames[self->id]);
}
void *__fastcall FormatsGet(FakeObject *, void *, uint32_t id)
{
    Ev(evFormat);
    Ev(id);
    return &gFormats[id % kFormats];
}
void *const *FormatVTable()
{
    static void *table[20] = {};
    table[16] = reinterpret_cast<void *>(&FormatDescribe);
    return table;
}
void *const *FormatsVTable()
{
    static void *table[10] = {};
    table[8] = reinterpret_cast<void *>(&FormatsGet);
    return table;
}

// ---- nodes and lights ---------------------------------------------------------------------------------------------------
struct FakeNode {
    void *const *vtable;
    uint32_t id;
    uint32_t limit;
};
uint32_t __fastcall NodeDetail(FakeNode *self, void *)
{
    Ev(evDetail);
    Ev(self->id);
    return self->limit;
}
void *const *NodeVTable()
{
    static void *table[100] = {};
    table[88] = reinterpret_cast<void *>(&NodeDetail);
    return table;
}

struct FakeLight {
    void *const *vtable;
    uint32_t id;
    bool standard;
    float params[8];
};
const DrawItem *gItems; // the items of the call (GetParams' second argument is the item)

void *__fastcall LightStandard(FakeLight *self, void *)
{
    Ev(evStandard);
    Ev(self->id);
    return self->standard ? self : nullptr;
}
void __fastcall LightParams(FakeLight *self, void *, float *out, uint32_t item)
{
    Ev(evParams);
    Ev(self->id);
    Ev(item - svcfx::P(gItems));
    for (unsigned i = 0; i < 8; ++i) out[i] = self->params[i];
}
void *const *LightVTable()
{
    static void *table[64] = {};
    table[34] = reinterpret_cast<void *>(&LightStandard);
    table[50] = reinterpret_cast<void *>(&LightParams);
    return table;
}

constexpr unsigned kNodes = 6, kLights = 10, kItems = 5, kRecords = 4;
FakeNode gNodes[kNodes];
FakeLight gLights[kLights];
LightRecord gLightRecords[kLights];
NodeRecord gRecords[kRecords];
DrawItem gItemArray[kItems];
uint32_t gBlocks[kItems][3 + 16]; // per item: the ambient colour, then up to 15 light record pointers (and a spare)

// A float for the log's formats: ordinary values, zero of both signs, denormals, huge, infinite, NaN with payloads.
float AnyFloat(std::mt19937 &gen)
{
    switch (gen() % 12) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return FromBits(gen() % 0x800000u | (gen() % 2 ? 0x80000000u : 0u)); // denormal
    case 3: return FromBits(0x7f800000u | (gen() % 2 ? 0x80000000u : 0u));        // infinity
    case 4: return FromBits(0x7f800000u | (1 + gen() % 0x7fffffu) | (gen() % 2 ? 0x80000000u : 0u)); // NaN
    case 5: return FromBits(0xffc00000u);                                           // the default NaN
    case 6: return std::ldexp(static_cast<float>(gen() % 1000000), static_cast<int>(gen() % 90)); // up to ~1e33
    case 7: return 0.0005f * static_cast<float>(gen() % 3 + 1) - 0.001f;             // around the rounding edge
    case 8: return FromBits(static_cast<uint32_t>(gen()));
    default: return (static_cast<float>(gen() % 200001) / 100000.0f - 1.0f) * static_cast<float>(1 + gen() % 300);
    }
}

// The state's light nibbles: per-type counts over types 1-12 (through-counts in nibbles 1-12) from a first nibble,
// at most 15 in all; the nibbles past 12 random.
void LightState(std::mt19937 &gen, uint32_t state[3], uint32_t &first, uint32_t &total)
{
    state[0] = 0;
    state[1] = gen() & 0xfff00000u;
    first = gen() % 5 == 0 ? 1 + gen() % 3 : 0;
    uint32_t through = first;
    const uint32_t wanted = gen() % 4 == 0 ? 0 : gen() % 4 == 0 ? gen() % (16 - first) : gen() % 4;
    uint32_t per[13] = {};
    for (uint32_t i = 0; i < wanted; ++i) ++per[1 + (gen() % 3 == 0 ? gen() % 12 : gen() % 3)];
    state[0] |= first;
    for (uint32_t type = 1; type <= 12; ++type) {
        through += per[type];
        state[type >> 3] |= through << ((type & 7) * 4);
    }
    total = through - first;
}

// The third word: flags 0-3 (every combination), the format id at 4, the counts at 11, 15 and 22, the rest random.
uint32_t ContextWord(std::mt19937 &gen) { return static_cast<uint32_t>(gen()); }

void World(std::mt19937 &gen)
{
    gLog.vtable = LogVTable();
    gMaterials = {MaterialVTable(), 0x33};
    gFormatsManager = {FormatsVTable(), 0x88};
    for (unsigned i = 0; i < kFormats; ++i) gFormats[i] = {FormatVTable(), i};
    for (unsigned i = 0; i < kNodes; ++i) {
        const uint32_t kind = gen() % 4;
        gNodes[i] = {NodeVTable(), 0x600 + i, kind == 0 ? 0xffffffffu : kind == 1 ? gen() % 4 : static_cast<uint32_t>(gen())};
    }
    for (unsigned i = 0; i < kLights; ++i) {
        FakeLight &light = gLights[i];
        light.vtable = LightVTable();
        light.id = 0x500 + i;
        light.standard = gen() % 4 != 0;
        for (float &p : light.params) p = AnyFloat(gen);
        LightRecord &record = gLightRecords[i];
        std::memset(&record, 0x3c, sizeof record);
        record.mLight = reinterpret_cast<nGZSceneGraph::cILight *>(&light);
        record.mNode = reinterpret_cast<nGZSceneGraph::cILightRefNode *>(&gNodes[gen() % kNodes]);
    }
    for (NodeRecord &record : gRecords) {
        std::memset(&record, 0x4d, sizeof record);
        record.mNode = reinterpret_cast<nGZSceneGraph::cIRenderableNode *>(&gNodes[gen() % kNodes]);
    }
    for (unsigned i = 0; i < kItems; ++i) {
        DrawItem &item = gItemArray[i];
        auto *const words = reinterpret_cast<uint32_t *>(&item);
        for (unsigned k = 0; k < sizeof(DrawItem) / 4; ++k) words[k] = static_cast<uint32_t>(gen());
        const uint32_t flags = gen() % 5 == 0 ? static_cast<uint32_t>(gen()) >> 16 : gen() % 16;
        item.mSubKey = flags << 16 | (gen() & 0xffff);
        item.mDepth = AnyFloat(gen);
        if (gen() % 3 == 0) item.mSortKey = static_cast<int32_t>(gen() % 8) - 2;
        if (gen() % 3 == 0) item.mMaterialIndex = static_cast<int32_t>(gen() % kNameCount);
        item.mRecord = &gRecords[gen() % kRecords];
        uint32_t first, total;
        LightState(gen, item.mState, first, total);
        item.mState[2] = ContextWord(gen);
        uint32_t *const block = gBlocks[i];
        for (unsigned k = 0; k < 3; ++k) block[k] = Bits(AnyFloat(gen));
        for (unsigned k = 3; k < 3 + 16; ++k) block[k] = 0xdeadbeef;
        // The block's records: indexes from the first nibble's count (what the selection writes after it).
        for (uint32_t k = 0; k < total; ++k) block[3 + first + k] = svcfx::P(&gLightRecords[gen() % kLights]);
        item.mWord3c = total || gen() % 2 ? svcfx::P(block + 3) : 0;
    }
}

// The items, the records and the blocks (unchanged: both take them const).
void Capture(Blob &o)
{
    o.push_back(0x17e3);
    for (const DrawItem &item : gItemArray) {
        const auto *const words = reinterpret_cast<const uint32_t *>(&item);
        o.insert(o.end(), words, words + sizeof(DrawItem) / 4);
    }
    for (const NodeRecord &record : gRecords) {
        const auto *const words = reinterpret_cast<const uint32_t *>(&record);
        o.insert(o.end(), words, words + sizeof(NodeRecord) / 4);
    }
    for (const auto &block : gBlocks) o.insert(o.end(), std::begin(block), std::end(block));
}

struct Fixture {
    shape::CodePatch gameNew{GameAddress<void>(0x824632), shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{GameAddress<void>(0x824624), shape::CodeOf(&GameDelete)};
    svcfx::Globals cells;
    explicit Fixture(const difftest::Context &ctx)
    {
        gImageBase = svcfx::P(ctx.image.At(0));
        gImageSize = ctx.image.Size();
        cells.Set(0x10f8fcc, &gMaterials);       // GetCachedQueriedObject's cache
        cells.Set(0x10caa24, &gFormatsManager);  // the cached vertex formats manager
    }
};

bool Same(difftest::Context &ctx, const Blob &a, const Blob &b, const char *what, uint32_t seed)
{
    ++ctx.cases;
    if (a == b) return true;
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    ctx.Fail("%s seed %u: outcomes differ at word %zu of %zu / %zu (%08x vs %08x)", what, seed, i, a.size(), b.size(),
             i < a.size() ? a[i] : 0, i < b.size() ? b[i] : 0);
    return false;
}

// ---- DescribeShaderContext's string -------------------------------------------------------------------------------------
struct FakeString {
    void *const *vtable;
};
FakeString *__cdecl StringSprintf(FakeString *self, const char *fmt, ...)
{
    Ev(evSprintf);
    Text(fmt);
    char text[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof text, fmt, args);
    va_end(args);
    Text(text);
    return self;
}
FakeString *__fastcall StringAppend(FakeString *self, void *, const char *text, uint32_t length)
{
    Ev(evAppend);
    Ev(length);
    Text(text, length);
    return self;
}
FakeString *__fastcall StringAppendString(FakeString *self, void *, const cIGZString &other)
{
    Ev(evAppendString);
    Ev(other.Strlen());
    Text(other.ToChar());
    return self;
}
void *const *StringVTable()
{
    static void *table[33] = {};
    table[19] = reinterpret_cast<void *>(&StringAppendString);
    table[20] = reinterpret_cast<void *>(&StringAppend);
    table[31] = reinterpret_cast<void *>(&StringSprintf);
    return table;
}

using LogFn = void(__cdecl *)(void *, const DrawItem *, int32_t);
using DescribeFn = void(__cdecl *)(void *, const uint32_t *);

} // namespace

// 0xaa1640: 0-5 items logged (the count sometimes 0, the items as World draws them). Every call in order (the text that
// reaches the log character for character), then the items, records and blocks.
DIFF_TEST(sg_display_list_log_items, 0xaa1640)
{
    Fixture fixture(ctx);
    for (uint32_t seed = 0; seed < 4000; ++seed) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 gen(seed * 7919u + 0xaa1640u);
            svcfx::ResetAll();
            World(gen);
            const int32_t count = static_cast<int32_t>(gen() % (kItems + 1));
            gItems = gItemArray;
            if (side == 0) ctx.Original<LogFn>(0xaa1640)(&gLog, gItemArray, count);
            else nGZSceneGraph::LogDrawItems(reinterpret_cast<nGZSceneGraph::cIDisplayListLog *>(&gLog), gItemArray, count);
            Blob &o = outcome[side];
            o = svcfx::gEv;
            Capture(o);
        }
        if (!Same(ctx, outcome[0], outcome[1], "log items", seed)) break;
    }
}

// 0xaf4c90: the state words as World draws them (every flag combination, light counts per type with and without a first
// nibble, none). Every call into the string and the formats in order.
DIFF_TEST(sg_display_list_describe_context, 0xaf4c90)
{
    Fixture fixture(ctx);
    FakeString out{StringVTable()};
    for (uint32_t seed = 0; seed < 4000; ++seed) {
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 gen(seed * 104729u + 0xaf4c90u);
            svcfx::ResetAll();
            World(gen);
            uint32_t state[3], first, total;
            LightState(gen, state, first, total);
            state[2] = ContextWord(gen);
            const uint32_t saved[3] = {state[0], state[1], state[2]};
            if (side == 0) ctx.Original<DescribeFn>(0xaf4c90)(&out, state);
            else nGZSceneGraph::DescribeShaderContext(reinterpret_cast<cIGZString *>(&out), state);
            Blob &o = outcome[side];
            o = svcfx::gEv;
            o.push_back((state[0] ^ saved[0]) | (state[1] ^ saved[1]) | (state[2] ^ saved[2]));
        }
        if (!Same(ctx, outcome[0], outcome[1], "describe", seed)) break;
    }
}
