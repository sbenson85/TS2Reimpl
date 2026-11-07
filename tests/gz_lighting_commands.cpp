// Differential tests for the lighting script's commands (src/gz/LightingCommands.cpp). The world is the material
// commands' (tests/gz_material_commands.h: a command, the parser state block with its cIBaseParser face, the argument
// list), with the lighting parser's fields here: its own vtable (slot 5 finds a named state), the named states (a real
// vector, rebuilt before each side's run and read after) and the current state.
#include "gz_material_commands.h"

#include "gz/LightingCommands.h"

#include <algorithm>

using namespace mattest;

namespace {

using SetContextFn = void(__thiscall *)(void *, void *, void *);
using ExecuteFn = void(__thiscall *)(void *, void *);
using VoidFn = void(__thiscall *)(void *);

const char *const kNames[] = {"day", "", "night", "a b", "sunsetGlow", "x"};

// A lighting state built in raw memory: the name (a short text: kept in the string itself), random values.
void MakeState(void *raw, const char *name, std::mt19937 &gen)
{
    auto *s = static_cast<LightingState *>(raw);
    s->mName.mVTable = GameAddress(cRZString::kVTable);
    new (&s->mName.mString) std::string(name);
    s->mName.mRefCount = gen() % 3;
    auto *v = reinterpret_cast<uint8_t *>(&s->mValues);
    for (size_t i = 0; i < sizeof(s->mValues); ++i) v[i] = static_cast<uint8_t>(gen());
}

// The parser's slot 5 (find a named state): answers in turn from gFind; a found state is copied to `out` and its
// index written.
struct FindAnswer {
    uint32_t found;    // the whole word (callers test al)
    uint32_t index;
    int templateIndex; // which of gTemplates is copied out
};
std::vector<FindAnswer> gFind;
size_t gFindNext = 0;
alignas(4) uint8_t gTemplates[3][sizeof(LightingState)];

uint32_t __fastcall StateFind(void *self, void *, const char *name, LightingState *out, uint32_t *index)
{
    Record(self, 5, reinterpret_cast<uint32_t>(name), reinterpret_cast<uint32_t>(out), Arg(index), 0);
    FindAnswer a = gFindNext < gFind.size() ? gFind[gFindNext] : FindAnswer{0, 0, 0};
    ++gFindNext;
    if (a.found & 0xff) {
        *out = *reinterpret_cast<LightingState *>(gTemplates[a.templateIndex]);
        *index = a.index;
    }
    return a.found;
}

// The lighting parts of the world: the parser's vtable, the named states, the current state.
struct LightingWorld {
    World &w;
    FakeVTable stateVt{46};
    std::vector<int> states; // the named states: indexes into gTemplates
    size_t capacity = 0;
    LightingState *block = nullptr;

    explicit LightingWorld(World &world) : w(world)
    {
        stateVt.slots[5] = reinterpret_cast<void *>(&StateFind);
        *reinterpret_cast<void **>(w.state) = stateVt.slots;
        std::memset(w.state + 0x1ac, 0, 12);
        MakeState(w.state + 0x1b8, kNames[w.gen() % std::size(kNames)], w.gen);
        w.before = [this] { Before(); };
        w.after = [this] { After(); };
    }
    void Before()
    {
        gFindNext = 0;
        block = capacity ? std::allocator<LightingState>().allocate(capacity) : nullptr;
        for (size_t i = 0; i < states.size(); ++i)
            new (&block[i]) LightingState(*reinterpret_cast<LightingState *>(gTemplates[states[i]]));
        w.Put(0x1ac, reinterpret_cast<uint32_t>(block));
        w.Put(0x1b0, reinterpret_cast<uint32_t>(block + states.size()));
        w.Put(0x1b4, reinterpret_cast<uint32_t>(block + capacity));
    }
    // The vector's size, capacity, whether the block was kept, each state's name text, count word and values.
    void After()
    {
        auto *b = *reinterpret_cast<LightingState **>(w.state + 0x1ac), *e = *reinterpret_cast<LightingState **>(w.state + 0x1b0),
             *c = *reinterpret_cast<LightingState **>(w.state + 0x1b4);
        ExtraWord(static_cast<uint32_t>(e - b));
        ExtraWord(static_cast<uint32_t>(c - b));
        ExtraWord(b == block);
        for (LightingState *p = b; p != e; ++p) {
            ExtraWord(reinterpret_cast<uint32_t>(p->mName.mVTable));
            ExtraText(p->mName.mString.c_str());
            ExtraWord(p->mName.mRefCount);
            ExtraBytes(&p->mValues, sizeof(p->mValues));
            p->~LightingState();
        }
        if (b) std::allocator<LightingState>().deallocate(b, static_cast<size_t>(c - b));
        std::memset(w.state + 0x1ac, 0, 12);
    }
};

struct Templates {
    explicit Templates(std::mt19937 &gen)
    {
        for (int i = 0; i < 3; ++i) MakeState(gTemplates[i], kNames[(i * 2 + gen()) % std::size(kNames)], gen);
    }
    ~Templates()
    {
        for (auto &t : gTemplates) reinterpret_cast<LightingState *>(t)->~LightingState();
    }
};

void RandomStates(LightingWorld &lw)
{
    size_t n = lw.w.gen() % 4 == 0 ? 0 : lw.w.gen() % 6;
    lw.states.clear();
    for (size_t i = 0; i < n; ++i) lw.states.push_back(static_cast<int>(lw.w.gen() % 3));
    lw.capacity = n + (lw.w.gen() % 3 == 0 ? 0 : lw.w.gen() % 4);
}

} // namespace

DIFF_TEST(LightingStateCommand_SetContext, 0xb3f150)
{
    auto orig = ctx.Original<SetContextFn>(0xb3f150);
    Stands stands;
    std::mt19937 gen(0xb3f150);
    for (int i = 0; i < 30; ++i) {
        World w(gen);
        w.AsBlock();
        void *iface = i % 5 == 4 ? nullptr : w.state + 4; // the state is not touched after the base's slot 3
        char what[64];
        std::snprintf(what, sizeof(what), "SetContext case %d", i);
        Compare(
            ctx, w, what, [&] { orig(w.cmd, w.parser, iface); },
            [&] { reinterpret_cast<cLightingStateCommand *>(w.cmd)->SetContext(w.parser, iface); });
    }
}

DIFF_TEST(LightingStateCommand_Execute, 0xb3e3a0)
{
    auto orig = ctx.Original<ExecuteFn>(0xb3e3a0);
    Stands stands;
    std::mt19937 gen(0xb3e3a0);
    Templates templates(gen);
    for (int i = 0; i < 300; ++i) {
        World w(gen);
        w.AsBlock();
        LightingWorld lw(w);
        RandomStates(lw);
        // The positional words: 1..4, none, and a count past 2^31 (the original's test is signed).
        const int32_t counts[] = {1, 1, 2, 3, 3, 3, 4, 0, static_cast<int32_t>(0x80000003u)};
        gPositionalCount = counts[gen() % std::size(counts)];
        for (int k = 0; k < 3; ++k) w.argv[k] = kNames[gen() % std::size(kNames)];
        const char *const seps[] = {":", ":", ":", "::", "", "from"};
        w.argv[1] = seps[gen() % std::size(seps)];
        gFind.clear();
        for (int k = 0; k < 2; ++k) {
            const uint32_t founds[] = {0, 1, 1, 0x100, 0x201};
            gFind.push_back({founds[gen() % std::size(founds)], gen() % 6, static_cast<int>(gen() % 3)});
        }
        char what[64];
        std::snprintf(what, sizeof(what), "Execute case %d", i);
        Compare(
            ctx, w, what, [&] { orig(w.cmd, w.args); },
            [&] { reinterpret_cast<cLightingStateCommand *>(w.cmd)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); });
    }
}

DIFF_TEST(LightingStateCommand_GetName, 0xb3f020)
{
    auto orig = ctx.Original<const char *(__thiscall *)(void *)>(0xb3f020);
    uint8_t object[0x50] = {};
    const char *a = orig(object);
    const char *b = reinterpret_cast<cLightingStateCommand *>(object)->GetName();
    ++ctx.cases;
    if (a != b || std::strcmp(b, "lightingState") != 0) ctx.Fail("GetName: %p '%s' vs %p '%s'", a, a, b, b);
}

DIFF_TEST(LightingStateCommand_EndBlock, 0xb3f0d0)
{
    auto orig = ctx.Original<VoidFn>(0xb3f0d0);
    Stands stands;
    std::mt19937 gen(0xb3f0d0);
    Templates templates(gen);
    for (int i = 0; i < 200; ++i) {
        World w(gen);
        w.AsBlock();
        LightingWorld lw(w);
        RandomStates(lw);
        // Replacing (any nonzero byte) a state that is there, or adding (the vector full or not).
        const uint8_t replacing[] = {0, 0, 1, 1, 0x80};
        w.cmd[0x44] = lw.states.empty() ? 0 : replacing[gen() % std::size(replacing)];
        *reinterpret_cast<uint32_t *>(w.cmd + 0x48) = lw.states.empty() ? gen() : static_cast<uint32_t>(gen() % lw.states.size());
        char what[64];
        std::snprintf(what, sizeof(what), "EndBlock case %d", i);
        Compare(ctx, w, what, [&] { orig(w.cmd); }, [&] { reinterpret_cast<cLightingStateCommand *>(w.cmd)->EndBlock(); });
    }
}

// ---- the lightingState block's commands --------------------------------------------------------------------------------
//
// The command (16 bytes): the parser's face at +4 (the world's face object), the state at +0xc. The face's float slots
// answer from tables; slots 35 and 39 write their out-parameter and return it, or (per case) a pointer to other values:
// the original copies from the returned pointer. The parser's two name tables (light types 0x10fb00c, modifier types
// 0x10fb018; unset in the harness's image) are filled for the tests and put back after.

namespace {

float gFloats[8];
size_t gFloatNext = 0;
float gVectors[4][3];
size_t gVectorNext = 0;
float gElsewhere[3];      // what slots 35 / 39 return a pointer to when gReturnElsewhere
bool gReturnElsewhere = false;

float __fastcall FaceEvalFloat(void *self, void *, const char *text)
{
    Record(self, 31, reinterpret_cast<uint32_t>(text), 0, 0, 0);
    return gFloats[gFloatNext++ % std::size(gFloats)];
}
float *FaceVector(float *out)
{
    const float *v = gVectors[gVectorNext++ % std::size(gVectors)];
    for (int k = 0; k < 3; ++k) out[k] = v[k];
    return gReturnElsewhere ? gElsewhere : out;
}
float *__fastcall FaceEvalVector3(void *self, void *, float *out, const char *text, uint32_t least)
{
    Record(self, 35, Arg(out), reinterpret_cast<uint32_t>(text), least, 0);
    return FaceVector(out);
}
float *__fastcall FaceEvalColorRGB(void *self, void *, float *out, const char *text)
{
    Record(self, 39, Arg(out), reinterpret_cast<uint32_t>(text), 0, 0);
    return FaceVector(out);
}

struct LightingTables {
    difftest::Context &ctx;
    uint8_t saved[2][sizeof(std::vector<cp::EnumEntry>)];
    static constexpr uint32_t kTables[2] = {0x10fb00c, 0x10fb018};
    explicit LightingTables(difftest::Context &c) : ctx(c)
    {
        // Names of increasing length (the table order). Light types past the seven lights and past the fourteen bits (the
        // light is still written: type 15's ends at +0x3a4, inside the test's state).
        const std::vector<cp::EnumEntry> entries[2] = {
            {{"a", 0}, {"bb", 1}, {"ccc", 6}, {"dddd", 7}, {"eeeee", 13}, {"ffffff", 14}, {"ggggggg", 15}},
            {{"m", 0}, {"mm", 2}, {"mmm", 5}},
        };
        for (int i = 0; i < 2; ++i) {
            std::memcpy(saved[i], ctx.image.At(kTables[i]), sizeof(saved[i]));
            new (ctx.image.At(kTables[i])) std::vector<cp::EnumEntry>(entries[i]);
        }
    }
    ~LightingTables()
    {
        for (int i = 0; i < 2; ++i) {
            using V = std::vector<cp::EnumEntry>;
            ctx.image.At<V>(kTables[i])->~V();
            std::memcpy(ctx.image.At(kTables[i]), saved[i], sizeof(saved[i]));
        }
    }
};

// A thrown exception, the standard ones included: their objects are {vtable, what, do_free} and their vtables are each
// module's own (the game's STL and ours), so for them the description is what() and the catchable type names.
struct AnyThrown {
    Thrown thrown;
    bool isStd = false;
    char what[200] = {};
    char types[300] = {};
};
int CaptureAny(EXCEPTION_POINTERS *p, AnyThrown *t)
{
    const EXCEPTION_RECORD *r = p->ExceptionRecord;
    if (r->ExceptionCode != 0xE06D7363 || r->NumberParameters < 3) return EXCEPTION_CONTINUE_SEARCH;
    const uint32_t *throwInfo = reinterpret_cast<const uint32_t *>(r->ExceptionInformation[2]);
    const uint32_t *types = reinterpret_cast<const uint32_t *>(throwInfo[3]);
    for (uint32_t i = 0; i < types[0]; ++i) {
        const uint32_t *ct = reinterpret_cast<const uint32_t *>(types[1 + i]);
        if (std::strcmp(reinterpret_cast<const char *>(ct[1]) + 8, ".?AVexception@std@@") == 0) t->isStd = true;
    }
    if (!t->isStd) return CaptureThrown(p, &t->thrown);
    const uint32_t *obj = reinterpret_cast<const uint32_t *>(r->ExceptionInformation[1]);
    std::snprintf(t->what, sizeof(t->what), "%s", obj[1] ? reinterpret_cast<const char *>(obj[1]) : "(null)");
    size_t used = 0;
    for (uint32_t i = 0; i < types[0]; ++i) {
        const uint32_t *ct = reinterpret_cast<const uint32_t *>(types[1 + i]);
        used += std::snprintf(t->types + used, sizeof(t->types) - used, "%s;", reinterpret_cast<const char *>(ct[1]) + 8);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
bool CallGuardedAny(void (*fn)(void *), void *ctx, AnyThrown *t)
{
    __try {
        fn(ctx);
        return false;
    } __except (CaptureAny(GetExceptionInformation(), t)) {
        return true;
    }
}
template <class F> Outcome RunGuardedAny(F f)
{
    AnyThrown t;
    Outcome o;
    o.threw = CallGuardedAny([](void *p) { (*static_cast<F *>(p))(); }, &f, &t);
    if (o.threw) {
        char buf[2800];
        if (t.isStd)
            std::snprintf(buf, sizeof(buf), "std what '%s' types %s", t.what, t.types);
        else
            std::snprintf(buf, sizeof(buf), "vtable %08x size %u text '%s' types %s", t.thrown.vtable, t.thrown.size, t.thrown.text,
                          t.thrown.types);
        o.detail = buf;
    }
    return o;
}

// The words a case's positional arguments and switch arguments are taken from: names the commands look for (and
// case variants), table names, others.
const char *const kWordsLit[] = {"insideFloorAmbient", "INSIDEWALLAMBIENT", "insideAmbientColor", "outsideFloorAmbient",
                                 "outsideWallAmbient", "OutsideAmbientColor", "floorThreshold", "wallThreshold",
                                 "nothing", "a", "bb", "ccc", "dddd", "eeeee", "ffffff", "m", "mm", "mmm", "0x1F",
                                 "  abc", "zz", ""};
const char *const kSwitchNames[] = {"debugPortals", "edith", "optimize", "sims", "objects", "indoors", "strength", "tint",
                                    "threshold"};

// One case of a block command: a world with the command shaped as a cCommandT of the lighting parser.
template <class C> void CommandTest(difftest::Context &ctx, const char *name, const char *firstWords[], size_t firstCount,
                                    int cases)
{
    auto orig = ctx.Original<ExecuteFn>(C::kExecute);
    Stands stands;
    LightingTables tables(ctx);
    std::mt19937 gen(C::kExecute);
    const float floats[] = {0.5f, 2.0f, -1.5f, 0.0f, 3.0e38f, 1.0f, -0.0f, 7.25f};
    const int32_t counts[] = {0, 1, 2, 2, 3, 3, 4, 4, 5, static_cast<int32_t>(0x80000002u)};
    for (int i = 0; i < cases; ++i) {
        World w(gen);
        *reinterpret_cast<void **>(w.cmd + 4) = w.face;
        *reinterpret_cast<void **>(w.cmd + 0xc) = w.state;
        w.faceVt.slots[31] = reinterpret_cast<void *>(&FaceEvalFloat);
        w.faceVt.slots[35] = reinterpret_cast<void *>(&FaceEvalVector3);
        w.faceVt.slots[39] = reinterpret_cast<void *>(&FaceEvalColorRGB);
        w.faceVt.ret[30] = gen() % 3 == 0 ? 0 : (gen() % 2 ? 1 : 0x100);
        w.argsVt.slots[3] = reinterpret_cast<void *>(&ArgsTakeSwitchArgs);
        // The current state's values: plain random floats (no NaN: a NaN's payload depends on the operand order).
        for (size_t off = 0x1d8 + 4; off < 0x3cc; off += 4) {
            const float f = static_cast<float>(static_cast<int32_t>(gen() % 2001) - 1000) / 64.0f;
            std::memcpy(w.state + off, &f, 4);
        }
        w.Put(0x1d8, gen()); // the enabled bits (and the word's upper bits)
        gPositionalCount = counts[gen() % std::size(counts)];
        for (auto &s : w.argv) s = kWordsLit[gen() % std::size(kWordsLit)];
        if (firstCount) w.argv[0] = firstWords[gen() % firstCount];
        gSwitchArgs.clear();
        for (const char *s : kSwitchNames)
            if (gen() % 3 == 0) gSwitchArgs.push_back({s, w.switchArgs});
        for (float &f : gFloats) f = floats[gen() % std::size(floats)];
        for (auto &v : gVectors)
            for (float &f : v) f = gen() % 7 == 0 ? 0.0f : floats[gen() % std::size(floats)];
        for (float &f : gElsewhere) f = floats[gen() % std::size(floats)];
        gReturnElsewhere = gen() % 3 == 0;
        w.before = [] {
            gFloatNext = 0;
            gVectorNext = 0;
        };
        char what[96];
        std::snprintf(what, sizeof(what), "%s case %d", name, i);
        Differential(
            ctx, what, w.Regions(), [&] { return RunGuardedAny([&] { orig(w.cmd, w.args); }); },
            [&] { return RunGuardedAny([&] { reinterpret_cast<C *>(w.cmd)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); }); },
            [&] { w.After(); }, [&] { w.Before(); });
    }
}

const char *kDiffusionNames[] = {"insideFloorAmbient", "INSIDEFLOORAMBIENT", "insideWallAmbient", "InsideWallAmbient", "InsideAmbientColor",
                                 "insideambientcolor", "OUTSIDEfloorAmbient", "outsideWALLAmbient", "outsideAMBIENTColor",
                                 "outsideFloorAmbient", "outsideWallAmbient", "outsideAmbientColor", "floorThreshold",
                                 "WallThreshold", "FLOORthreshold", "wall", "insideFloorAmbientX", ""};
const char *kLightTypeNames[] = {"a", "bb", "ccc", "dddd", "eeeee", "ffffff", "ggggggg", "nope"};
const char *kModifierNames[] = {"m", "mm", "mmm", "mmmm"};
const char *kHex[] = {"0x1F", "ff", "  12AB", "", "zz", "0XdeadBEEF1"};

} // namespace

DIFF_TEST(LightingCommand_Control, 0xb3dab0) { CommandTest<cControlCommand>(ctx, "control", nullptr, 0, 120); }
DIFF_TEST(LightingCommand_DiffParam, 0xb3db90)
{
    CommandTest<cDiffusionParamCommand>(ctx, "diffParam", kDiffusionNames, std::size(kDiffusionNames), 400);
}
DIFF_TEST(LightingCommand_Environment, 0xb3dd60) { CommandTest<cEnvironmentShadeCommand>(ctx, "environment", nullptr, 0, 120); }
DIFF_TEST(LightingCommand_FlatShade, 0xb3de10) { CommandTest<cFlatShadeCommand>(ctx, "flatShade", nullptr, 0, 200); }
DIFF_TEST(LightingCommand_Fog, 0xb3df50) { CommandTest<cFogCommand>(ctx, "fog", nullptr, 0, 100); }
DIFF_TEST(LightingCommand_Light, 0xb3dff0)
{
    CommandTest<cLightCommand>(ctx, "light", kLightTypeNames, std::size(kLightTypeNames), 300);
}
DIFF_TEST(LightingCommand_LightMap, 0xb3e1b0) { CommandTest<cLightMapCommand>(ctx, "lightMap", kHex, std::size(kHex), 150); }
DIFF_TEST(LightingCommand_Modifiers, 0xb3e2c0)
{
    CommandTest<cLightingModifiersCommand>(ctx, "modifiers", kModifierNames, std::size(kModifierNames), 200);
}
DIFF_TEST(LightingCommand_ObjectLightThreshold, 0xb3e890)
{
    CommandTest<cObjectLightThresholdCommand>(ctx, "objectLightThreshold", nullptr, 0, 40);
}
DIFF_TEST(LightingCommand_Particles, 0xb3e8c0) { CommandTest<cParticleShadeCommand>(ctx, "particles", nullptr, 0, 120); }
DIFF_TEST(LightingCommand_PortalSaturation, 0xb3e970) { CommandTest<cPortalSaturationCommand>(ctx, "portalSaturation", nullptr, 0, 40); }
DIFF_TEST(LightingCommand_ProjectiveShadowStrength, 0xb3e9a0)
{
    CommandTest<cProjectiveShadowStrengthCommand>(ctx, "projectiveShadowStrength", nullptr, 0, 40);
}
DIFF_TEST(LightingCommand_Skylight, 0xb3e9d0) { CommandTest<cSkylightShadeCommand>(ctx, "skylight", nullptr, 0, 120); }
DIFF_TEST(LightingCommand_TransitionTimes, 0xb3ebc0) { CommandTest<cTransitionTimesCommand>(ctx, "transitionTimes", nullptr, 0, 40); }
DIFF_TEST(LightingCommand_UIShade, 0xb3ec20) { CommandTest<cUIShadeCommand>(ctx, "UIShade", nullptr, 0, 120); }
DIFF_TEST(LightingCommand_UnlitRoomColor, 0xb3ecc0) { CommandTest<cUnlitRoomColorCommand>(ctx, "unlitRoomColor", nullptr, 0, 150); }

namespace {
template <class C> void NameCase(difftest::Context &ctx, const char *expected)
{
    auto orig = ctx.Original<const char *(__thiscall *)(void *)>(C::kGetName);
    uint8_t object[0x10] = {};
    const char *a = orig(object);
    const char *b = reinterpret_cast<C *>(object)->GetName();
    ++ctx.cases;
    if (a != b || std::strcmp(b, expected) != 0) ctx.Fail("GetName %08x: %p '%s' vs %p '%s'", C::kGetName, a, a, b, b);
}
} // namespace

DIFF_TEST(LightingCommand_Names, 0xb3efc0)
{
    // Each case runs the class's kGetName (the RVA in its comment) against ours.
    NameCase<cControlCommand>(ctx, "control");                                   // 0xb3efc0
    NameCase<cDiffusionParamCommand>(ctx, "diffParam");                          // 0xb3efd0
    NameCase<cEnvironmentShadeCommand>(ctx, "environment");                      // 0xb3efe0
    NameCase<cFlatShadeCommand>(ctx, "flatShade");                               // 0xb3eff0
    NameCase<cFogCommand>(ctx, "fog"); // 0xad96e0: not hooked (a folded body), compared all the same
    NameCase<cLightCommand>(ctx, "light"); // 0x2ea380: likewise
    NameCase<cLightMapCommand>(ctx, "lightMap");                                 // 0xb3f000
    NameCase<cLightingModifiersCommand>(ctx, "modifiers");                       // 0xb3f010
    NameCase<cObjectLightThresholdCommand>(ctx, "objectLightThreshold");         // 0xb3f030
    NameCase<cParticleShadeCommand>(ctx, "particles");                           // 0xb3f040
    NameCase<cPortalSaturationCommand>(ctx, "portalSaturation");                 // 0xb3f050
    NameCase<cProjectiveShadowStrengthCommand>(ctx, "projectiveShadowStrength"); // 0xb3f060
    NameCase<cSkylightShadeCommand>(ctx, "skylight");                            // 0xb3f070
    NameCase<cTransitionTimesCommand>(ctx, "transitionTimes");                   // 0xb3f090
    NameCase<cUIShadeCommand>(ctx, "UIShade");                                   // 0xb3f0a0
    NameCase<cUnlitRoomColorCommand>(ctx, "unlitRoomColor");                     // 0xb3f0b0
}
