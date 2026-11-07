// Differential tests for nTSSG::cTSSGSystem's resources by name (src/sg/cTSSGSystemResources.cpp).
// Hook shape: the original runs on the same object image as ours.
//
// The world (tests/ts_services_fixture.h): the object starts as random bytes with its vtable a table of logging functions
// (slots 25 and 98 log the key words, the text and the arguments, and answer from the seed); the resource manager
// (cached cell 0x10cc2d8) is a fake whose slots 3 and 15 log the parsed key's four words and answer from a seeded
// sequence (slot 3 also handing out a resource); the parser target is a block whose string vector holds real cRZStrings
// (their slot 5 is the game's own code, run on both sides). The key parser (ours, StringKeyParser::ParseKey) runs on
// both sides as each side's own code; the names avoid the forms that reach its service object.
//
// Compared after each run: the result, the event log, the object's 0x3b8 bytes (normalised), the out words, every
// fake's reference count and the cell.
#include "code_patch.h"
#include "difftest.h"
#include "ts_services_fixture.h"

#include "game/game.h"
#include "sg/SGLocalString.h"
#include "sg/cTSSGSystem.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <random>

namespace {

using namespace svcfx;
using nTSSG::cTSSGSystem;
using nTSSG::SGLocalString;

constexpr uint32_t kObjectSize = 0x3b8;
constexpr uint32_t kObjectTag = 0x5e000000;
constexpr uint32_t kBlockTag = 0x6d000000;
constexpr uint32_t kDataTag = 0x6e000000;
constexpr uint32_t kTargetTag = 0x6f000000;
constexpr uint32_t kManagerCell = 0x10cc2d8;
enum : uint32_t { evSelf = 0x80, evKey, evString };

enum : unsigned {
    fFramework = 0,
    fManager,
    fAnim,
    fLighting,
    fLightingObject,
    fModel,
    fRes,
    fRes0,
    fRes1,
    fRes2,
    fOther,
};

struct VBlock {
    uint32_t words[2];
    int32_t table[3];
    void Point(Obj *entry1, Obj *entry2)
    {
        words[0] = 0x0b1ec700;
        words[1] = P(table);
        table[0] = 0;
        table[1] = int32_t(P(entry1) - P(&words[1]));
        table[2] = int32_t(P(entry2) - P(&words[1]));
    }
};
VBlock gBlocks[3]; // resources slot 98 hands out (entry 1: fRes0..2)

alignas(16) uint8_t gObject[kObjectSize];
// gObject's words as the outcome logs them (svcfx::DumpWords): 'p' pointer, 'd' data, 'x' not compared.
constexpr char kObjectKinds[] =
    "pddddddd dddddddd dddddddp dddddddd dddddddd dpdddddd dddddddd dddddddd "
    "dddpdddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd dddddddd "
    "dddddddd dddddddd dddddddd dddddddd dddddddd dddddd";
alignas(16) uint32_t gData[16];
// gData's words as the outcome logs them (svcfx::DumpWords): 'p' pointer, 'd' data, 'x' not compared.
constexpr char kDataKinds[] = "dddddddd dddddddd";
alignas(16) uint8_t gTarget[0x1c0];
alignas(16) uint8_t gNameStore[6 * sizeof(SGLocalString)];
unsigned gNameCount;

void LogText(const char *text)
{
    Ev(evString);
    if (!text) return Ev(0xdead);
    for (; *text; ++text) Ev(uint8_t(*text));
}
void LogKey(uint32_t address, int words)
{
    Ev(evKey);
    for (int i = 0; i < words; ++i) Ev(reinterpret_cast<const uint32_t *>(uintptr_t(address))[i]);
}
void Put(uint32_t address, uint32_t value) { *reinterpret_cast<uint32_t *>(uintptr_t(address)) = value; }

// ---- the seed's answers ----
uint32_t gManagerAnswers[8];
uint32_t gSelfAnswers[8];
uint32_t gQueryAnswer;
unsigned gManagerAt, gSelfAt;

// ---- the object's own vtable ----
void *gSelfVTable[100];
uint32_t __fastcall Self25(void *self, void *, uint32_t key, uint32_t text, uint32_t a3, uint32_t out, uint32_t force)
{
    Ev(evSelf), Ev(25), Ev(Norm(P(self))), Ev(a3), Ev(Norm(out)), Ev(force & 0xff);
    LogKey(key, 2);
    LogText(reinterpret_cast<const char *>(uintptr_t(text)));
    return gSelfAnswers[gSelfAt++ % 8] & 1;
}
uint32_t __fastcall Self98(void *self, void *, uint32_t key, uint32_t text, uint32_t out)
{
    Ev(evSelf), Ev(98), Ev(Norm(P(self))), Ev(Norm(out));
    LogKey(key, 2);
    LogText(reinterpret_cast<const char *>(uintptr_t(text)));
    const uint32_t answer = gSelfAnswers[gSelfAt++ % 8];
    if (answer & 2) Put(out, P(&gBlocks[gSelfAt % 3]));
    return answer & 1;
}

// ---- fakes' custom slots ----
uint32_t ManagerHas(Obj *, uint32_t *args) // slot 15 (key)
{
    LogKey(args[0], 4);
    return gManagerAnswers[gManagerAt++ % 8] & 1;
}
uint32_t ManagerFind(Obj *, uint32_t *args) // slot 3 (key, type, &resource, 0, 0)
{
    LogKey(args[0], 4);
    const uint32_t answer = gManagerAnswers[gManagerAt++ % 8];
    if (answer & 2) Put(args[2], P(&F(fRes)));
    return answer & 1;
}
uint32_t ResourceObject(Obj *, uint32_t *args) // resource slot 15 (1, type, &object, 0)
{
    Put(args[2], P(&F(fModel)));
    return 1;
}
uint32_t ModelQuery(Obj *, uint32_t *args) // model slot 0 (iid, out)
{
    if (gQueryAnswer) Put(args[1], P(&F(fOther)));
    return gQueryAnswer;
}

struct Fixture {
    AllocatorScope allocator;
    Globals globals;
    Fixture()
    {
        gSelfVTable[25] = reinterpret_cast<void *>(&Self25);
        gSelfVTable[98] = reinterpret_cast<void *>(&Self98);
        globals.SetWord(kManagerCell, 0);
        gFrameworkIndex = fFramework;
    }
};

void Wire()
{
    F(fManager).Set(15, "p", 0u, &ManagerHas);
    F(fManager).Set(3, "pdp--", 0u, &ManagerFind);
    F(fRes).Set(15, "ddp-", 0u, &ResourceObject);
    F(fModel).Set(0, "dp", 0u, &ModelQuery);
    F(fLighting).Set(25, 0, &F(fLightingObject));
    F(fLightingObject).Set(9, "p");
    F(fAnim).Set(13, "p");
    for (unsigned i = 0; i < 3; ++i) gBlocks[i].Point(&F(fRes0 + i), &F(fOther));
}

Blob Outcome(uint32_t result)
{
    Blob b = gEv;
    b.push_back(0xeeee);
    b.push_back(result);
    svcfx::DumpWords(b, gObject, sizeof(gObject), kObjectKinds);
    svcfx::DumpWords(b, gData, sizeof(gData), kDataKinds);
    CaptureFakes(b);
    b.push_back(Norm(*GameAddress<uint32_t>(kManagerCell)));
    return b;
}

template <class Run>
void Compare(difftest::Context &ctx, const char *what, uint32_t seed, const uint8_t *start, const uint32_t *data, Run run)
{
    Blob outcome[2];
    for (int side = 0; side < 2; ++side) {
        ResetAll();
        gManagerAt = 0, gSelfAt = 0;
        Wire();
        *GameAddress<void *>(kManagerCell) = &F(fManager);
        std::memcpy(gObject, start, kObjectSize);
        std::memcpy(gData, data, sizeof(gData));
        AddRange(gObject, kObjectSize, kObjectTag);
        AddRange(gBlocks, sizeof(gBlocks), kBlockTag);
        AddRange(gData, sizeof(gData), kDataTag);
        AddRange(gTarget, sizeof(gTarget), kTargetTag);
        ctx.Mark(side ? "ours" : "original");
        const uint32_t result = run(side == 0);
        outcome[side] = Outcome(result);
    }
    Same(ctx, outcome[0], outcome[1], what, seed);
}

const char *const kNames[] = {"Walk",         "walk_anim",    "Run_ANIM",     "",          "nomodel",
                              "NoModel_cres", "chair_cres",   "lamp_shpe",    "Lamp",      "a_cres_x",
                              "sit_anim_anim", "LongerName_With_Many_Characters", "x", "nomodel_cres"};
constexpr unsigned kNameCount = sizeof(kNames) / sizeof(kNames[0]);

void DrawWorld(std::mt19937 &gen, uint8_t *start, uint32_t *data)
{
    for (uint32_t i = 0; i < kObjectSize; ++i) start[i] = uint8_t(gen());
    auto *sys = reinterpret_cast<cTSSGSystem *>(start);
    sys->mVTable = gSelfVTable;
    sys->mAnimSystem.mObject = &F(fAnim);
    sys->mLightingManager.mObject = &F(fLighting);
    sys->mParserTarget = gen() % 6 ? gTarget : nullptr;
    sys->mRenderOptions[0] = uint8_t(gen() % 4 ? 1 + gen() % 255 : 0);
    for (unsigned i = 0; i < 16; ++i) data[i] = gen();
    for (uint32_t &a : gManagerAnswers) a = gen() % 4;
    for (uint32_t &a : gSelfAnswers) a = gen() % 4;
    gQueryAnswer = gen() % 3 != 0;
}

// The parser target's names: 0..5 real cRZStrings.
void BuildNames(std::mt19937 &gen)
{
    gNameCount = gen() % 6;
    auto *names = reinterpret_cast<SGLocalString *>(gNameStore);
    for (unsigned i = 0; i < gNameCount; ++i) ::new (&names[i]) SGLocalString(kNames[gen() % kNameCount]);
    std::memset(gTarget, 0xcd, sizeof(gTarget));
    *reinterpret_cast<uint32_t *>(gTarget + 0x1ac) = P(names);
    *reinterpret_cast<uint32_t *>(gTarget + 0x1b0) = P(names + gNameCount);
}
void DropNames()
{
    auto *names = reinterpret_cast<SGLocalString *>(gNameStore);
    for (unsigned i = 0; i < gNameCount; ++i) names[i].~SGLocalString();
    gNameCount = 0;
}

cTSSGSystem *Ours() { return reinterpret_cast<cTSSGSystem *>(gObject); }

} // namespace

// 0x24bba0 PreloadAnimations: the option byte and the target present or not, 0..5 names, slot 98 finding or not.
DIFF_TEST(sg_tssg_system_preload_animations, 0x24bba0)
{
    Fixture fixture;
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x24bba0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x24bba0u);
        uint8_t start[kObjectSize];
        uint32_t data[16];
        DrawWorld(gen, start, data);
        BuildNames(gen);
        Compare(ctx, "preload_animations", seed, start, data, [&](bool useOriginal) -> uint32_t {
            if (useOriginal) original(gObject);
            else Ours()->PreloadAnimations();
            return 0;
        });
        DropNames();
    }
}

// 0x249fd0 HasModel: names with and without the suffix, "nomodel" in any case, a group or not, the manager's answers.
DIFF_TEST(sg_tssg_system_has_model, 0x249fd0)
{
    Fixture fixture;
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, const char *, const uint32_t *)>(0x249fd0);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 gen(seed * 104729u + 0x249fd0u);
        uint8_t start[kObjectSize];
        uint32_t data[16];
        DrawWorld(gen, start, data);
        const char *name = kNames[gen() % kNameCount];
        const bool grouped = gen() % 2;
        Compare(ctx, "has_model", seed, start, data, [&](bool useOriginal) -> uint32_t {
            const uint32_t *group = grouped ? &gData[2] : nullptr;
            if (useOriginal) return original(gObject, name, group);
            return Ours()->HasModel(name, group);
        });
    }
}

// 0x24a650 FindModel: name empty or not, a fallback or not, a group or not, the manager's answers (found, with or without
// a resource), the model's query answer.
DIFF_TEST(sg_tssg_system_find_model, 0x24a650)
{
    Fixture fixture;
    auto original =
        ctx.Original<void(__thiscall *)(void *, const char *, const uint32_t *, const char *, uint32_t, void **, uint32_t)>(
            0x24a650);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        std::mt19937 gen(seed * 15485863u + 0x24a650u);
        uint8_t start[kObjectSize];
        uint32_t data[16];
        DrawWorld(gen, start, data);
        const char *name = gen() % 4 ? kNames[gen() % kNameCount] : "";
        const char *fallback = gen() % 2 ? kNames[gen() % kNameCount] : nullptr;
        const bool grouped = gen() % 2;
        const uint32_t iid = gen(), sixth = gen();
        Compare(ctx, "find_model", seed, start, data, [&](bool useOriginal) -> uint32_t {
            const uint32_t *group = grouped ? &gData[2] : nullptr;
            void **out = reinterpret_cast<void **>(&gData[5]);
            if (useOriginal) original(gObject, name, group, fallback, iid, out, sixth);
            else Ours()->FindModel(name, group, fallback, iid, out, sixth);
            return 0;
        });
    }
}

// 0x24a970 FindShape: names with and without the suffix, the manager's answers, the shape's query answer.
DIFF_TEST(sg_tssg_system_find_shape, 0x24a970)
{
    Fixture fixture;
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, const char *, void **)>(0x24a970);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 gen(seed * 2654435761u + 0x24a970u);
        uint8_t start[kObjectSize];
        uint32_t data[16];
        DrawWorld(gen, start, data);
        const char *name = kNames[gen() % kNameCount];
        Compare(ctx, "find_shape", seed, start, data, [&](bool useOriginal) -> uint32_t {
            void **out = reinterpret_cast<void **>(&gData[5]);
            if (useOriginal) return original(gObject, name, out);
            return Ours()->FindShape(name, out);
        });
    }
}

// 0x24a510 FindAnimation: null and other names, force values, slot 25's answers.
DIFF_TEST(sg_tssg_system_find_animation, 0x24a510)
{
    Fixture fixture;
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, const char *, void **, uint8_t)>(0x24a510);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        std::mt19937 gen(seed * 7919u + 0x24a510u);
        uint8_t start[kObjectSize];
        uint32_t data[16];
        DrawWorld(gen, start, data);
        const char *name = gen() % 5 ? kNames[gen() % kNameCount] : nullptr;
        const uint8_t force = uint8_t(gen() % 3);
        Compare(ctx, "find_animation", seed, start, data, [&](bool useOriginal) -> uint32_t {
            void **out = reinterpret_cast<void **>(&gData[5]);
            if (useOriginal) return original(gObject, name, out, force);
            return Ours()->FindAnimation(name, out, force);
        });
    }
}
