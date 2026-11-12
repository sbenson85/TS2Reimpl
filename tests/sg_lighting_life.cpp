// Differential tests for nTSSG::cLightingManager's small lifecycle slots (lane 261108h, scenegraph LG3;
// src/sg/cLightingManager.cpp): CurrentSet 0x2d37c0, slots 12, 14, 15 (an object's "ObjectLighting" extension), 33 (the
// ambient image), 19 (a light set by name), 36 and 37 (the lot values). Hook shape: the original runs on the same object
// image as ours.
//
// The world: the object is 0x548 random bytes with its vtable a fake (slots 13 and 14 log), mWorld null now and then,
// mFlag4D any byte, 0..3 light sets (random words, mEnabled's bit 12 drawn) and the current index in and out of range;
// the extension finder 0xaeb5b0 (ours too: a StandIn on both sides) a fake that logs and answers the extension or null; the extension's slots
// 3 (set a value) and 15 (get one: drawn answer, drawn value near the strength) log; its virtual base (through the vbptr
// at +4) and that base's slot 18 handler (slot 23) log; the object's slot 134 and the source's slot 5 answer drawn
// words. Slot 19's callees 0x2e4480, 0x2e10a0 and 0x2d0730 (all ours: on both sides) are redirected to fakes that log (0x2e10a0 answers a drawn
// word: only al counts). Slots 36 / 37 reach the cached slot-23 object (the cell 0x10cc2a8, a fake for the test) whose
// slots 9 / 10 give fakes with slots 55 / 56.
//
// Compared: results, out buffers, the fakes' log, the object's bytes and the light sets' bytes; at the x87's 53-bit and
// 24-bit precision where floats are computed.
#include "code_patch.h"
#include "sg_lighting_common.h"

#include "sg/SGUtilities.h"
#include "sg/cLightingManager.h"

#include <windows.h>

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>

namespace {

using nTSSG::cLightingManager;
using nTSSG::cLightSet;

constexpr uint32_t kObjectSize = 0x548;
constexpr uint32_t kSlot23Cell = 0x10cc2a8;
constexpr uint32_t kDefaultName = 0xdaefa0;

alignas(16) uint8_t gObject[kObjectSize];
alignas(16) cLightSet gSets[3];
void Put(uint32_t offset, uint32_t value) { std::memcpy(gObject + offset, &value, 4); }
void PutF(uint32_t offset, float value) { Put(offset, U(value)); }

// --- fakes ---------------------------------------------------------------------------------------------------------

struct Fake {
    void **vtable;
    uint32_t salt;
};

// The manager's own slots 13 and 14 (the object's vtable is this fake's).
void *gManagerVT[16];
void __fastcall ManagerSlot13(void *self, void *, void *object, uint32_t value)
{
    gLog.push_back(0x13), gLog.push_back(self == gObject), gLog.push_back(P(object)), gLog.push_back(value);
}
void __fastcall ManagerSlot14(void *self, void *, void *object, uint32_t a, float f, uint32_t b)
{
    gLog.push_back(0x14), gLog.push_back(self == gObject), gLog.push_back(P(object)), gLog.push_back(a),
        gLog.push_back(U(f)), gLog.push_back(b);
}

// The extension: slot 3 sets a value, slot 15 gets one; the vbptr at +4 leads to gBase.
struct FakeExtension {
    void **vtable;
    const int32_t *vbptr;
};
FakeExtension gExtension;
int32_t gVbtable[2];
Fake gBase, gHandler, gTarget, gSource;
bool gExtensionFound;
uint32_t gGetAnswer, gSlot134Answer, gSourceValue;
float gCurrent;
void *__cdecl FindExtension(void *object, const char *name, void *after)
{
    gLog.push_back(0xaeb5b0), gLog.push_back(P(object)), gLog.push_back(P(name)), gLog.push_back(P(after));
    return gExtensionFound ? &gExtension : nullptr;
}
void __fastcall ExtensionSet(FakeExtension *self, void *, const char *name, float value)
{
    gLog.push_back(0xe03), gLog.push_back(self == &gExtension), gLog.push_back(P(name)), gLog.push_back(U(value));
}
uint32_t __fastcall ExtensionGet(FakeExtension *self, void *, const char *name, float *value)
{
    gLog.push_back(0xe15), gLog.push_back(self == &gExtension), gLog.push_back(P(name));
    *value = gCurrent;
    return gGetAnswer;
}
void *gExtensionVT[16];
void *__fastcall BaseHandler(Fake *self, void *)
{
    gLog.push_back(0xb18), gLog.push_back(self == &gBase);
    return &gHandler;
}
void *gBaseVT[19];
void __fastcall HandlerSlot23(Fake *self, void *, void *base)
{
    gLog.push_back(0xa23), gLog.push_back(self == &gHandler), gLog.push_back(P(base));
}
void *gHandlerVT[24];
uint32_t __fastcall TargetSlot134(Fake *self, void *)
{
    gLog.push_back(0x134), gLog.push_back(P(self));
    return gSlot134Answer;
}
void *gTargetVT[135];
uint32_t __fastcall SourceSlot5(Fake *self, void *)
{
    gLog.push_back(0x5), gLog.push_back(P(self));
    return gSourceValue;
}
void *gSourceVT[6];

// The ambient image's slot 38 (texel x, y at level 0 into the buffer).
Fake gImage;
float *__fastcall ImageTexel(Fake *self, void *, float *buffer, uint32_t x, uint32_t y, uint32_t level)
{
    gLog.push_back(0x38), gLog.push_back(P(self)), gLog.push_back(x), gLog.push_back(y), gLog.push_back(level);
    for (uint32_t c = 0; c < 3; ++c) buffer[c] = Fl(Mix(Mix(self->salt, x * 3 + c), y));
    return buffer;
}
void *gImageVT[39];

// Slot 19's callees.
uint32_t gPickAnswer;
void __fastcall PickByTime(void *self, void *) { gLog.push_back(0x2e4480), gLog.push_back(self == gObject); }
uint32_t __fastcall PickByName(void *self, void *, const char *name)
{
    gLog.push_back(0x2e10a0), gLog.push_back(self == gObject), gLog.push_back(P(name));
    return gPickAnswer;
}
void __fastcall AfterPick(void *self, void *) { gLog.push_back(0x2d0730), gLog.push_back(self == gObject); }

// The cached slot-23 object and the two it gives.
Fake gSlot23, gLotA, gLotB;
void *__fastcall Slot23Get9(Fake *self, void *)
{
    gLog.push_back(0x2309), gLog.push_back(self == &gSlot23);
    return &gLotA;
}
void *__fastcall Slot23Get10(Fake *self, void *)
{
    gLog.push_back(0x2310), gLog.push_back(self == &gSlot23);
    return &gLotB;
}
uint32_t __fastcall LotGet(Fake *self, void *, uint32_t id)
{
    gLog.push_back(0x55), gLog.push_back(P(self)), gLog.push_back(id);
    return Mix(self->salt, id);
}
uint32_t __fastcall LotSet(Fake *self, void *, uint32_t id, uint32_t value)
{
    gLog.push_back(0x56), gLog.push_back(P(self)), gLog.push_back(id), gLog.push_back(value);
    return Mix(Mix(self->salt, id), value);
}
void *gSlot23VT[11];
void *gLotVT[57];

void FillFakes()
{
    gManagerVT[13] = reinterpret_cast<void *>(&ManagerSlot13);
    gManagerVT[14] = reinterpret_cast<void *>(&ManagerSlot14);
    gExtensionVT[3] = reinterpret_cast<void *>(&ExtensionSet);
    gExtensionVT[15] = reinterpret_cast<void *>(&ExtensionGet);
    gBaseVT[18] = reinterpret_cast<void *>(&BaseHandler);
    gHandlerVT[23] = reinterpret_cast<void *>(&HandlerSlot23);
    gTargetVT[134] = reinterpret_cast<void *>(&TargetSlot134);
    gSourceVT[5] = reinterpret_cast<void *>(&SourceSlot5);
    gImageVT[38] = reinterpret_cast<void *>(&ImageTexel);
    gSlot23VT[9] = reinterpret_cast<void *>(&Slot23Get9);
    gSlot23VT[10] = reinterpret_cast<void *>(&Slot23Get10);
    gLotVT[55] = reinterpret_cast<void *>(&LotGet);
    gLotVT[56] = reinterpret_cast<void *>(&LotSet);
    gExtension = {gExtensionVT, gVbtable};
    gVbtable[0] = -4;
    gVbtable[1] = int32_t(P(&gBase) - (P(&gExtension) + 4));
    gBase = {gBaseVT, 0};
    gHandler = {gHandlerVT, 0};
    gTarget = {gTargetVT, 0};
    gSource = {gSourceVT, 0};
    gImage = {gImageVT, 0};
    gSlot23 = {gSlot23VT, 0};
    gLotA = {gLotVT, 0xa11a};
    gLotB = {gLotVT, 0xb22b};
}

// --- the world -----------------------------------------------------------------------------------------------------

void Build(uint32_t seed)
{
    std::mt19937 gen(seed);
    for (auto &b : gObject) b = uint8_t(gen());
    for (auto &s : gSets) {
        auto *w = reinterpret_cast<uint32_t *>(&s);
        for (size_t i = 0; i < sizeof(cLightSet) / 4; ++i) w[i] = gen();
    }
    Put(0, P(gManagerVT));
    const unsigned sets = gen() % 4;
    Put(0x98, P(gSets)), Put(0x9c, P(gSets + sets)), Put(0xa0, P(gSets + 3));
    static const uint32_t kIndexes[] = {0, 1, 2, 3, 0xffffffff, 0x80000000};
    Put(0xd0, kIndexes[gen() % 6]);
    if (gen() % 8 == 0) Put(0x20, 0);
    if (gen() % 3 == 0) gObject[0x4d] = 0;
    else if (gen() % 2) gObject[0x4d] = 1;
    // +0x500 against +0x508 * 1000: random, NaN, equal (500 = 0.5 * 1000), and the signed zeros both ways (the max's
    // operand order shows only there).
    static const float kZeros[] = {0.0f, -0.0f};
    const uint32_t pick500 = gen() % 8, pick508 = gen() % 8;
    PutF(0x500, pick500 < 5 ? Nice(gen, 2000.0f) : pick500 == 5 ? NaN(gen) : pick500 == 6 ? 500.0f : kZeros[gen() % 2]);
    PutF(0x508, pick508 < 5 ? Nice(gen, 2.0f) : pick508 == 5 ? NaN(gen) : pick508 == 6 ? 0.5f : kZeros[gen() % 2]);
}

cLightingManager *Ours() { return reinterpret_cast<cLightingManager *>(gObject); }

Blob End(const uint32_t *values, unsigned count)
{
    Blob o = gLog;
    o.push_back(0xeeee);
    for (unsigned i = 0; i < count; ++i) o.push_back(values[i]);
    for (uint32_t i = 0; i < kObjectSize; i += 4) {
        uint32_t w;
        std::memcpy(&w, gObject + i, 4);
        o.push_back(w);
    }
    const auto *s = reinterpret_cast<const uint32_t *>(gSets);
    for (size_t i = 0; i < sizeof(gSets) / 4; ++i) o.push_back(s[i]);
    return o;
}

} // namespace

// 0x2d37c0 CurrentSet: begin / end words any multiple of 0x214 apart (end before begin included: the original's
// count is a signed division compared unsigned), the index in and out of range.
DIFF_TEST(sg_lighting_life_current, 0x2d37c0)
{
    auto original = ctx.Original<const cLightSet *(__thiscall *)(void *)>(0x2d37c0);
    for (uint32_t seed = 0; seed < 4000; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x2d37c0u);
        Build(gen());
        const uint32_t begin = gen() % 2 ? P(gSets) : gen();
        static const int32_t kCounts[] = {0, 1, 2, 3, 4, 0x7fff, -1, -2, 0x3e0000, -0x3e0000};
        const int32_t count = gen() % 3 ? int32_t(gen() % 6) : kCounts[gen() % 10];
        Put(0x98, begin), Put(0x9c, begin + uint32_t(count) * 0x214u);
        static const uint32_t kIndexes[] = {0, 1, 2, 3, 4, 5, 0x7ffe, 0x7fff, 0xffffffff, 0x80000000, 0x3dffff, 0xfffffffe};
        Put(0xd0, kIndexes[gen() % std::size(kIndexes)]);
        const cLightSet *a = original(gObject);
        const cLightSet *b = &Ours()->CurrentSet();
        ++ctx.cases;
        if (a != b) ctx.Fail("CurrentSet seed %u: %p vs %p (count %d, index %08x)", seed, a, b, count, *reinterpret_cast<uint32_t *>(gObject + 0xd0));
    }
}

// 0x2d39f0 slot 12, 0x2d2610 slot 14, 0x2d2720 slot 15: an extension or none, mWorld null or not, the strength kept
// or 1, the current value equal, within 0.001 and outside it (and NaN), mField500 raised or kept (NaNs on both sides).
DIFF_TEST(sg_lighting_life_strength, 0x2d2610)
{
    FillFakes();
    shape::StandIn finder{0xaeb5b0, &nGZSceneGraph::FindDataList, &FindExtension};
    auto slot12 = ctx.Original<void(__thiscall *)(void *, void *)>(0x2d39f0);
    auto slot14 = ctx.Original<void(__thiscall *)(void *, void *, uint32_t, float, uint32_t)>(0x2d2610);
    auto slot15 = ctx.Original<void(__thiscall *)(void *, void *, void *, float, uint32_t)>(0x2d2720);
    AtBothPrecisions(ctx, "strength", 4000, [&](uint32_t seed, const char *label) {
        std::mt19937 gen(seed * 7919u + 0x2d2610u);
        const uint32_t world = gen();
        const unsigned which = seed % 3;
        const uint32_t a2 = gen(), a4 = gen();
        float strength = gen() % 6 == 0 ? NaN(gen) : gen() % 3 == 0 ? 1.0f : Nice(gen, 2.0f);
        static const float kDeltas[] = {0.0f, 0.001f, -0.001f, 0.00099999994f, -0.00099999994f, 0.0009765625f,
                                        -0.0009765625f, 0.0010000001f, 0.5f, -1.0f, 1e-8f};
        const float delta = kDeltas[gen() % std::size(kDeltas)];
        const unsigned closeTo = gen() % 8;
        const bool found = gen() % 6 != 0;
        const uint32_t getAnswer = gen() % 4 == 0 ? (gen() & 0xffffff00u) : gen() % 2 ? 1u : (gen() | 1u);
        const uint32_t slot134 = gen() % 3 == 0 ? (gen() & 0xffffff00u) : gen();
        const uint32_t sourceValue = gen();
        // the current value: near the strength or near 1 (the strength the unkept case writes), or a NaN
        const float base = closeTo < 3 ? strength : closeTo < 6 ? 1.0f : closeTo == 6 ? NaN(gen) : Nice(gen, 2.0f);
        const float current = base + delta;
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            Build(world);
            gLog.clear();
            gExtensionFound = found;
            gGetAnswer = getAnswer;
            gSlot134Answer = slot134;
            gSourceValue = sourceValue;
            gCurrent = current;
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) {
                if (which == 0) slot12(gObject, &gTarget);
                else if (which == 1) slot14(gObject, &gTarget, a2, strength, a4);
                else slot15(gObject, &gTarget, &gSource, strength, a4);
            } else {
                if (which == 0) Ours()->ResetObjectLighting(&gTarget);
                else if (which == 1) Ours()->SetObjectStrength(&gTarget, a2, strength, a4);
                else Ours()->UpdateObjectStrength(&gTarget, &gSource, strength, a4);
            }
            outcome[side] = End(nullptr, 0);
        }
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
}

// 0x2df290 slot 33: no image; scales and coordinates giving cells below 0, inside and past the terrain size (sizes 0,
// 1, small, negative, 2^31); NaNs and infinities (the floor's 0x80000000).
DIFF_TEST(sg_lighting_life_image, 0x2df290)
{
    FillFakes();
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, const float *, float *)>(0x2df290);
    AtBothPrecisions(ctx, "image", 4000, [&](uint32_t seed, const char *label) {
        std::mt19937 gen(seed * 7919u + 0x2df290u);
        const uint32_t world = gen();
        float xy[2];
        for (auto &f : xy) f = gen() % 10 == 0 ? NaN(gen) : gen() % 3 ? float(int32_t(gen() % 40000) - 4000) / 64.0f : Nice(gen, 200.0f);
        static const float kScales[] = {1.0f, 0.5f, 0.25f, 2.0f, 0.1f, 1.0f / 3.0f};
        const float scale = gen() % 8 == 0 ? NaN(gen) : gen() % 3 ? kScales[gen() % 6] : Nice(gen, 4.0f);
        static const uint32_t kSizes[] = {0, 1, 2, 64, 0xffffffff, 0x80000000, 0x7fffffff, 0xfffffff0};
        const uint32_t width = gen() % 3 ? gen() % 300 : kSizes[gen() % 8];
        const uint32_t height = gen() % 3 ? gen() % 300 : kSizes[gen() % 8];
        const bool image = gen() % 6 != 0;
        const uint32_t salt = gen();
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            Build(world);
            gLog.clear();
            gImage.salt = salt;
            Put(0x3c, image ? P(&gImage) : 0);
            PutF(0x200, scale), Put(0x20c, width), Put(0x210, height);
            float out[4] = {Fl(0xabababab), Fl(0xabababab), Fl(0xabababab), Fl(0xabababab)};
            ctx.Mark(side ? "ours" : "original");
            uint32_t result;
            if (side == 0) result = original(gObject, xy, out) & 0xff;
            else result = Ours()->AmbientImageAt(xy, out);
            const uint32_t values[5] = {result, U(out[0]), U(out[1]), U(out[2]), U(out[3])};
            outcome[side] = End(values, 5);
        }
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
}

// 0x2e1020 slot 19: the default name (the game's text and a copy), names near it, others; 0x2e10a0 picking a set or
// not (only al counts).
DIFF_TEST(sg_lighting_life_select, 0x2e1020)
{
    FillFakes();
    // 0x2e4480 and 0x2e10a0 are ours (lane 261110q) and 0x2d0730 too (ApplyLightSet, lane 261110o): the same fakes on
    // both sides.
    shape::StandIn byTime{0x2e4480, &cLightingManager::SelectTimeOfDayLightSet, &PickByTime};
    shape::StandIn byName{0x2e10a0, &cLightingManager::SelectHoodLightSet, &PickByName};
    shape::StandIn after{0x2d0730, &cLightingManager::ApplyLightSet, &AfterPick};
    auto original = ctx.Original<void(__thiscall *)(void *, const char *)>(0x2e1020);
    const char *gameDefault = ctx.image.At<const char>(kDefaultName);
    char copy[64], longer[72], shorter[64];
    std::snprintf(copy, sizeof(copy), "%s", gameDefault);
    std::snprintf(longer, sizeof(longer), "%sx", gameDefault);
    std::snprintf(shorter, sizeof(shorter), "%s", gameDefault);
    if (shorter[0]) shorter[std::strlen(shorter) - 1] = 0;
    char flipped[64]; // the default name with every letter's case changed (strcmp, not _stricmp)
    std::snprintf(flipped, sizeof(flipped), "%s", gameDefault);
    for (char *c = flipped; *c; ++c) *c = std::isupper(uint8_t(*c)) ? char(std::tolower(uint8_t(*c))) : char(std::toupper(uint8_t(*c)));
    const char *names[] = {gameDefault, copy, longer, shorter, "", "day", "night", flipped};
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x2e1020u);
        const uint32_t world = gen();
        const char *name = names[gen() % std::size(names)];
        static const uint32_t kAnswers[] = {0, 1, 0x100, 0xffffff01, 0x12345600};
        const uint32_t answer = kAnswers[gen() % std::size(kAnswers)];
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            Build(world);
            gLog.clear();
            gPickAnswer = answer;
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) original(gObject, name);
            else Ours()->SelectLightSet(name);
            outcome[side] = End(nullptr, 0);
        }
        svcfx::Same(ctx, outcome[0], outcome[1], "select", seed);
    }
}

// 0x2d9480 slot 36 and 0x2e29b0 slot 37: the cached slot-23 object's slots 9 / 10, then 55 / 56 with the arguments.
DIFF_TEST(sg_lighting_life_lot, 0x2d9480)
{
    FillFakes();
    auto get = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x2d9480);
    auto set = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t, uint32_t)>(0x2e29b0);
    void *saved = *GameAddress<void *>(kSlot23Cell);
    *GameAddress<void *>(kSlot23Cell) = &gSlot23;
    for (uint32_t seed = 0; seed < 400; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x2d9480u);
        const uint32_t world = gen(), id = gen(), value = gen();
        const bool setting = seed % 2;
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            Build(world);
            gLog.clear();
            ctx.Mark(side ? "ours" : "original");
            uint32_t result;
            if (side == 0) result = setting ? set(gObject, id, value) : get(gObject, id);
            else result = setting ? Ours()->SetLotValue(id, value) : Ours()->LotValue(id);
            outcome[side] = End(&result, 1);
        }
        svcfx::Same(ctx, outcome[0], outcome[1], setting ? "lot set" : "lot get", seed);
    }
    *GameAddress<void *>(kSlot23Cell) = saved;
}
