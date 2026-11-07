// Differential tests of the meta-particle block (`metaParticles`), its path block and its commands
// (src/gz/ParticleCommands.cpp) against the originals. The world is tests/gz_particle_commands.h's; the meta
// description's functions are patched with stand-ins that build and check its containers (the rotate values, the effect
// name), since the block's Execute moves them inline.
#include "gz_particle_commands.h"

using namespace ptest;

namespace {

// ---- the meta description's functions ----------------------------------------------------------------------------------
// The constructor (0xbecf80) writes a token, the flags, gMetaValues rotate values (a heap block), the effect name
// gMetaEffect and a pattern over the plain values; the destructor (0xbed140) logs the token and what is left in the
// containers, and destroys them (only logs when gMetaDestroy is false: the destructor test's object is random bytes).
int gMetaValues = 0;
const char *gMetaEffect = "";
bool gMetaDestroy = true;

void *__fastcall MetaDescConstruct(void *self, void *)
{
    auto *b = static_cast<uint8_t *>(self);
    ++gToken;
    *reinterpret_cast<uint32_t *>(b) = 0xDE5C0000u + gToken;
    std::memset(b + 4, 0x5a, 0x158 - 4);
    *reinterpret_cast<uint64_t *>(b + 0x158) = 0x1111000000000000ull + gToken;
    *reinterpret_cast<uint32_t *>(b + 0x160) = 0xC0C00000u + gToken;
    auto *values = new (b + 0x164) std::vector<float>();
    for (int i = 0; i < gMetaValues; ++i) values->push_back(0.5f * static_cast<float>(i) + static_cast<float>(gToken));
    *reinterpret_cast<float *>(b + 0x170) = 0.25f * static_cast<float>(gToken);
    *reinterpret_cast<float *>(b + 0x174) = -0.75f * static_cast<float>(gToken);
    new (b + 0x178) std::string(gMetaEffect);
    for (size_t o = 0x190; o < 0x1e8; ++o) b[o] = static_cast<uint8_t>(o * 7 + gToken);
    Log(60, Where(self), gToken);
    return self;
}
void MetaDescriptionLeft(void *self)
{
    auto *b = static_cast<uint8_t *>(self);
    auto *values = reinterpret_cast<std::vector<float> *>(b + 0x164);
    auto *effect = reinterpret_cast<std::string *>(b + 0x178);
    ExtraWord(static_cast<uint32_t>(values->size()));
    ExtraWord(static_cast<uint32_t>(values->capacity()));
    for (float f : *values) ExtraWord(Bits(f));
    ExtraText(effect->c_str());
    ExtraWord(static_cast<uint32_t>(effect->capacity()));
    values->~vector();
    effect->~basic_string();
}
void __fastcall MetaDescDestroy(void *self, void *)
{
    Log(61, Where(self), *static_cast<uint32_t *>(self));
    if (gMetaDestroy) MetaDescriptionLeft(self);
}
void *__fastcall MetaBaseAssign(void *self, void *, const void *from)
{
    Log(62, Where(self), Where(from), *static_cast<const uint32_t *>(from));
    return self;
}
void *__fastcall MetaDescCopy(void *self, void *, const void *from)
{
    Log(63, Where(self), Where(from), *static_cast<const uint32_t *>(from));
    return self;
}
void __fastcall MetaDescFinish(void *self, void *) { Log(64, Where(self), *static_cast<const uint32_t *>(self)); }
void __fastcall MetaRateSet(void *self, void *, float rate) { Log(65, Where(self), Bits(rate)); }
void __cdecl MetaRateSwitches(void *args, void *description, void *face)
{
    Log(66, reinterpret_cast<uint32_t>(args), Where(description), reinterpret_cast<uint32_t>(face));
}
void __fastcall MetaGroup(void *self, void *, void *args, const char *name, uint32_t kind, uint32_t zero)
{
    Log(67, Where(self), reinterpret_cast<uint32_t>(args), reinterpret_cast<uint32_t>(name), kind);
    ExtraWord(zero);
}
// The manager's slot 7 for a meta component: its words, the description's token and containers (then destroyed).
uint32_t __fastcall MetaManagerAdd(void *self, void *, const char *name, void *component)
{
    Record(self, 7, 0, component != nullptr);
    ExtraText(name);
    if (component) {
        const uint32_t *w = static_cast<const uint32_t *>(component);
        ExtraWord(w[0]);
        ExtraWord(w[1]);
        ExtraWord(w[2]);
        ExtraWord(w[4]);
        MetaDescriptionLeft(static_cast<uint8_t *>(component) + 0x10);
        ::operator delete(component);
    }
    return 0;
}

struct MetaStands {
    ParticleStands particles;
    // The description's constructor, destructor, base assignment and copy are ours (lane IX): over the image's and ours.
    shape::StandIn construct{0xbecf80u, &cTSParticlePartsAdapters::ConstructMeta, &MetaDescConstruct};
    shape::StandIn destroy{0xbed140u, &cTSParticlePartsAdapters::DestroyMeta, &MetaDescDestroy};
    shape::StandIn baseAssign{0xbe1df0u, &cTSParticlePartsAdapters::AssignBase, &MetaBaseAssign};
    shape::StandIn copy{0xbed200u, &cTSParticlePartsAdapters::CopyMeta, &MetaDescCopy};
    shape::CodePatch finish{0xbefb90u, shape::CodeOf(&MetaDescFinish)};
    // The curve's SetConstant is ours (lane IW): the stand-in over the image's and ours alike.
    shape::StandIn rateSet{0xbdcd10u, &cTSEffectCurve::SetConstant, &MetaRateSet};
    shape::CodePatch rateSwitches{0xbf0ef0u, shape::CodeOf(&MetaRateSwitches)};
    shape::CodePatch group{0xbcd940u, shape::CodeOf(&MetaGroup)};
};

// A vector of floats in the state (the path's points, the warp forces): its size, spare room and values drawn; after a
// run its size, capacity, whether its block was kept (told only when the capacity is unchanged) and values are logged,
// then it is freed.
struct FloatVector {
    size_t offset, size, count, capacity;
    std::vector<float> values;
    uint8_t *block = nullptr;
    FloatVector(size_t at, size_t elementSize, std::mt19937 &gen, const float *table, size_t n) : offset(at), size(elementSize)
    {
        count = gen() % 4;
        capacity = count + gen() % 3;
        values.resize(count * size / 4);
        for (float &f : values) f = table[gen() % n];
    }
    RawVector *In(uint8_t *state) const { return reinterpret_cast<RawVector *>(state + offset); }
    void Before(uint8_t *state)
    {
        block = capacity ? std::allocator<uint8_t>().allocate(capacity * size) : nullptr;
        if (!values.empty()) std::memcpy(block, values.data(), values.size() * 4);
        *In(state) = {block, block + count * size, block + capacity * size};
    }
    void After(uint8_t *state)
    {
        RawVector *v = In(state);
        const size_t cap = static_cast<size_t>(v->mCapacity - v->mBegin);
        ExtraWord(static_cast<uint32_t>(v->mEnd - v->mBegin));
        ExtraWord(static_cast<uint32_t>(cap));
        ExtraWord(cap == capacity * size ? v->mBegin == block : 2);
        if (v->mBegin != v->mEnd) ExtraBytes(v->mBegin, static_cast<size_t>(v->mEnd - v->mBegin));
        if (v->mBegin) std::allocator<uint8_t>().deallocate(v->mBegin, cap);
        std::memset(v, 0, sizeof(*v));
    }
};

// The meta state's containers for its commands: the effect name, the colliders, the attractors, the path's points, the
// warp forces.
struct MetaContainers {
    World &w;
    const char *effect;
    size_t colliders = 0, colliderCapacity = 0, attractors = 0, attractorCapacity = 0;
    uint8_t *colliderBlock = nullptr, *attractorBlock = nullptr;
    FloatVector points, warps;
    static constexpr size_t kEffect = 0x180;

    MetaContainers(World &world, const float *table, size_t n)
        : w(world), points(0x150, 0x1c, world.gen, table, n), warps(0xfc, 0x1c, world.gen, table, n)
    {
        effect = kTexts[w.gen() % std::size(kTexts)];
        colliders = w.gen() % 3;
        colliderCapacity = colliders + w.gen() % 2;
        attractors = w.gen() % 3;
        attractorCapacity = attractors + w.gen() % 2;
        Clear();
    }
    void Clear()
    {
        std::memset(w.state + kEffect, 0, 0x18);
        std::memset(w.state + 0x138, 0, 0x18);
        std::memset(w.state + 0x150, 0, 0xc);
        std::memset(w.state + 0xfc, 0, 0xc);
    }
    void Before()
    {
        points.Before(w.state);
        warps.Before(w.state);
        new (w.state + kEffect) std::string(effect);
        auto *c = reinterpret_cast<RawVector *>(w.state + 0x138);
        colliderBlock = colliderCapacity ? std::allocator<uint8_t>().allocate(colliderCapacity * 0x84) : nullptr;
        for (size_t i = 0; i < colliders; ++i) {
            uint8_t *e = colliderBlock + i * 0x84;
            *reinterpret_cast<uint32_t *>(e) = 0xC0000000u + static_cast<uint32_t>(i);
            new (e + 4) std::string(kTexts[i]);
            std::memset(e + 0x1c, static_cast<int>(i), 0x84 - 0x1c);
        }
        *c = {colliderBlock, colliderBlock + colliders * 0x84, colliderBlock + colliderCapacity * 0x84};
        auto *a = reinterpret_cast<RawVector *>(w.state + 0x144);
        attractorBlock = attractorCapacity ? std::allocator<uint8_t>().allocate(attractorCapacity * 0x34) : nullptr;
        for (size_t i = 0; i < attractors; ++i) {
            std::memset(attractorBlock + i * 0x34, 0x60 + static_cast<int>(i), 0x34);
            std::memset(attractorBlock + i * 0x34 + 0x20, 0, 12);
        }
        *a = {attractorBlock, attractorBlock + attractors * 0x34, attractorBlock + attractorCapacity * 0x34};
    }
    void After()
    {
        auto *s = reinterpret_cast<std::string *>(w.state + kEffect);
        ExtraText(s->c_str());
        s->~basic_string();
        auto *c = reinterpret_cast<RawVector *>(w.state + 0x138);
        ExtraWord(static_cast<uint32_t>(c->mEnd - c->mBegin));
        ExtraWord(static_cast<uint32_t>(c->mCapacity - c->mBegin));
        ExtraWord(c->mBegin == colliderBlock);
        for (uint8_t *e = c->mBegin; e != c->mEnd; e += 0x84) {
            ExtraBytes(e, 4);
            ExtraText(ColliderName(e).c_str());
            ExtraBytes(e + 0x1c, 0x84 - 0x1c);
            ColliderName(e).~basic_string();
        }
        if (c->mBegin) std::allocator<uint8_t>().deallocate(c->mBegin, static_cast<size_t>(c->mCapacity - c->mBegin));
        auto *a = reinterpret_cast<RawVector *>(w.state + 0x144);
        ExtraWord(static_cast<uint32_t>(a->mEnd - a->mBegin));
        ExtraWord(static_cast<uint32_t>(a->mCapacity - a->mBegin));
        ExtraWord(a->mBegin == attractorBlock);
        CaptureAttractors(*a, attractorBlock);
        if (a->mBegin) std::allocator<uint8_t>().deallocate(a->mBegin, static_cast<size_t>(a->mCapacity - a->mBegin));
        points.After(w.state);
        warps.After(w.state);
        Clear();
    }
};

const float kFloats[] = {0.5f, -2.0f, 0.0f, 3.0e38f, 1.0f, -0.0f, 7.25f, 1e-7f, 0.25f, 0.999f, 2.5e-3f, 1.0e-30f, 4.0f, -3.5f};

// ---- the commands ------------------------------------------------------------------------------------------------------

const char *const kChildWords[] = {"a", "bb", "ccc", "dddd", "nope", "", "a much longer name than fifteen", "x y", "1 2 3"};
const char *const kSwitchNames[] = {"base",       "reset",     "pinToTerrain", "sticky", "killOutsideTerrain", "point",
                                    "scaleParticles", "resetIncoming", "pinToWater", "terrainOnly", "waterOnly", "sync",
                                    "wait",       "preferDir", "path"};
const char *const kSwitchArgNames[] = {
    "delay",       "strength",  "turn",        "mix",        "vary",       "offset",     "square",     "quad",
    "cube",        "box",       "circle",      "sphere",     "ellipse",    "ellipsoid",  "ring",       "torus",
    "terrain",     "terrainWindySide", "belowHeight", "aboveHeight", "heightRange", "gravity", "wind", "globalWind",
    "bomb",        "drag",      "tractor",     "tractorRel", "tractorResetSpeed", "tractorTimeScale", "terrainRepel",
    "screw",       "wiggle",    "wiggleDir",   "bloomAlpha", "bloomSize",  "bounce",     "death",      "deathByWater",
    "limitHeight", "damp",      "bank",        "windBank"};

template <class C> void MetaChildCases(difftest::Context &ctx, int cases)
{
    auto orig = ctx.Original<ExecuteFn>(C::kExecute);
    MetaStands stands;
    EnumTableFill alignment(ctx, 0x10fd204, 0x10);
    FreeWatch watch;
    std::mt19937 gen(C::kExecute);
    const int32_t counts[] = {0, 1, 2, 3, 3, 4, 5, static_cast<int32_t>(0x80000002u)};
    for (int i = 0; i < cases; ++i) {
        World w(gen);
        MetaContainers containers(w, kFloats, std::size(kFloats));
        gObjectBase = w.cmd;
        gStateBase = w.state;
        *reinterpret_cast<void **>(w.cmd + 4) = w.face;
        *reinterpret_cast<void **>(w.cmd + 0xc) = w.state;
        w.faceVt.slots[31] = reinterpret_cast<void *>(&FaceEvalFloat);
        w.faceVt.slots[34] = reinterpret_cast<void *>(&FaceEvalVector2);
        w.faceVt.slots[35] = reinterpret_cast<void *>(&FaceEvalVector3);
        w.argsVt.slots[1] = reinterpret_cast<void *>(&ArgsTakeSwitch);
        w.argsVt.slots[2] = reinterpret_cast<void *>(&ArgsTakeSwitchArgsRange);
        w.argsVt.slots[3] = reinterpret_cast<void *>(&ArgsTakeSwitchArgs);
        for (auto &s : w.argv) s = kChildWords[gen() % std::size(kChildWords)];
        const char *switchValues[8];
        for (auto &s : switchValues) s = kChildWords[gen() % std::size(kChildWords)];
        gPositionalCount = counts[gen() % std::size(counts)];
        w.Put(0x160, gen());
        w.Put(0x164, gen());
        // The state's values the commands compute from: often from the float table (negative zeros included).
        auto someFloats = [&](size_t offset, int n) {
            if (gen() % 4 != 0)
                for (int k = 0; k < n; ++k) w.Put(offset + 4 * k, Bits(kFloats[gen() % std::size(kFloats)]));
        };
        someFloats(0xc, 2);   // life
        someFloats(0x60, 6);  // the source box
        someFloats(0xd0, 3);  // the force
        someFloats(0x1f0, 2); // the time, the time scale
        w.Put(0x1f8, gen() % 3); // the random walk's index (a third walk writes over the description's later values)
        float origin[3];
        for (float &f : origin) f = kFloats[gen() % std::size(kFloats)];
        ZeroVector zero(ctx, origin, 0x10fd210);
        const uint32_t density = gen() % 4;
        gSwitches.clear();
        for (const char *s : kSwitchNames)
            if (gen() % 6 < density) gSwitches.push_back({s, gen() % 3 == 0 ? 0x100u : 1u});
        gSwitchArgs.clear();
        gSwitchCounts.clear();
        bool mix = false;
        for (const char *s : kSwitchArgNames)
            if (gen() % 6 < density) {
                gSwitchArgs.push_back({s, switchValues});
                gSwitchCounts.push_back({s, counts[gen() % std::size(counts)]});
                mix = mix || std::strcmp(s, "mix") == 0;
            }
        // randomWalk -preferDir reads -mix's argument (none given: a null read in both, which the harness cannot catch).
        bool preferDir = false;
        for (const auto &s : gSwitches) preferDir = preferDir || (s.first == "preferDir" && (s.second & 0xff));
        if (preferDir && !mix) gSwitchArgs.push_back({"mix", switchValues});
        // The source's terrain shapes come last in its chain: a third of its cases give only one of them (with any
        // count, past 2^31 included), which the random draws above seldom reach.
        if (C::kExecute == cMetaParticleSourceCommand::kExecute && gen() % 3 == 0) {
            const char *terrain = gen() % 2 ? "terrain" : "terrainWindySide";
            gSwitchArgs.clear();
            gSwitchCounts.clear();
            gSwitchArgs.push_back({terrain, switchValues});
            gSwitchCounts.push_back({terrain, counts[gen() % std::size(counts)]});
            for (auto &s : gSwitches)
                if (s.first == "point") s.second = 0;
        }
        gAttractorParseResult = gen() % 2 != 0;
        for (float &f : gFloats) f = kFloats[gen() % std::size(kFloats)];
        for (auto &v : gVectors)
            for (float &f : v) f = kFloats[gen() % std::size(kFloats)];
        for (float &f : gElsewhere) f = kFloats[gen() % std::size(kFloats)];
        for (float &f : gRanged) f = kFloats[gen() % std::size(kFloats)];
        for (float &f : gEmitVolume) f = kFloats[gen() % std::size(kFloats)];
        gReturnElsewhere = gen() % 3 == 0;
        gAttractorValues = static_cast<int>(gen() % 3);
        w.before = [&] {
            gFloatNext = 0;
            gVectorNext = 0;
            gRangedNext = 0;
            gToken = 0;
            gColliderPattern = 0;
            FreeWatch::Reset();
            containers.Before();
        };
        w.after = [&] {
            containers.After();
            FreeWatch::Capture();
        };
        char what[64];
        std::snprintf(what, sizeof(what), "%08x case %d", C::kExecute, i);
        Differential(
            ctx, what, w.Regions(), [&] { return RunGuarded([&] { orig(w.cmd, w.args); }); },
            [&] { return RunGuarded([&] { reinterpret_cast<C *>(w.cmd)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); }); },
            [&] { w.After(); }, [&] { w.Before(); });
    }
}

} // namespace

DIFF_TEST(MetaParticleCommand_Forwarders, 0xbed5c0)
{
    MetaChildCases<cMetaParticleAspectCommand>(ctx, 20);       // 0xbed5c0
    MetaChildCases<cMetaParticleTerrainRepelCommand>(ctx, 20); // 0xbef7a0
    MetaChildCases<cMetaParticleEffectBaseCommand>(ctx, 40);   // 0xbed9a0
    MetaChildCases<cMetaParticleEmitCommand>(ctx, 40);         // 0xbedd30
}
DIFF_TEST(MetaParticleCommand_Rates, 0xbeebe0)
{
    MetaChildCases<cMetaParticleRateCommand>(ctx, 20);     // 0xbeebe0
    MetaChildCases<cMetaParticleInjectCommand>(ctx, 30);   // 0xbee7a0
    MetaChildCases<cMetaParticleMaintainCommand>(ctx, 80); // 0xbee800
}
DIFF_TEST(MetaParticleCommand_Rotate, 0xbeec20) { MetaChildCases<cMetaParticleRotateCommand>(ctx, 80); }
DIFF_TEST(MetaParticleCommand_Alignment, 0xbed480) { MetaChildCases<cMetaParticleAlignmentCommand>(ctx, 150); }
DIFF_TEST(MetaParticleCommand_Attractor, 0xbed5e0) { MetaChildCases<cMetaParticleAttractorCommand>(ctx, 80); }
DIFF_TEST(MetaParticleCommand_Collide, 0xbed6c0) { MetaChildCases<cMetaParticleCollisionCommand>(ctx, 300); }
DIFF_TEST(MetaParticleCommand_TerrainCollide, 0xbef5f0) { MetaChildCases<cMetaParticleTerrainCollideCommand>(ctx, 150); }
DIFF_TEST(MetaParticleCommand_RandomWalk, 0xbee8e0) { MetaChildCases<cMetaParticleRandomWalkCommand>(ctx, 300); }
DIFF_TEST(MetaParticleCommand_Warp, 0xbef7d0) { MetaChildCases<cMetaParticleWarpCommand>(ctx, 300); }
DIFF_TEST(MetaParticleCommand_Source, 0xbeecc0) { MetaChildCases<cMetaParticleSourceCommand>(ctx, 600); }
DIFF_TEST(MetaParticleCommand_Force, 0xbedd80) { MetaChildCases<cMetaParticleForceCommand>(ctx, 800); }

DIFF_TEST(MetaParticleCommand_Group, 0xbed450)
{
    auto orig = ctx.Original<ExecuteFn>(0xbed450);
    MetaStands stands;
    std::mt19937 gen(0xbed450);
    for (int i = 0; i < 20; ++i) {
        World w(gen);
        gObjectBase = w.cmd;
        gStateBase = w.state;
        *reinterpret_cast<void **>(w.cmd + 0xc) = w.state;
        for (auto &s : w.argv) s = kChildWords[gen() % std::size(kChildWords)];
        char what[64];
        std::snprintf(what, sizeof(what), "group case %d", i);
        Run(
            ctx, what, w.Regions(), [&] { orig(w.cmd, w.args); },
            [&] { reinterpret_cast<cGroupMetaParticlesCommand *>(w.cmd)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); },
            [&] { w.After(); }, [&] { w.Before(); });
    }
}

// ---- metaParticles ---------------------------------------------------------------------------------------------------

DIFF_TEST(MetaParticleEffect_SetContext, 0xbefd10)
{
    auto orig = ctx.Original<SetContextFn>(0xbefd10);
    MetaStands stands;
    FakeVTable blockVt{-3};
    blockVt.slots[10] = reinterpret_cast<void *>(&ParticleAddCommand);
    std::mt19937 gen(0xbefd10);
    for (int i = 0; i < 20; ++i) {
        World w(gen);
        RandomObject(w, 0x260);
        *reinterpret_cast<void **>(gObject) = blockVt.slots;
        w.parserVt.ret[43] = w.RandomResult();
        void *stateInterface = gen() % 4 == 0 ? nullptr : w.state + 4;
        char what[64];
        std::snprintf(what, sizeof(what), "SetContext case %d", i);
        Run(
            ctx, what, With(w, gObject, 0x260), [&] { orig(gObject, w.parser, stateInterface); },
            [&] { reinterpret_cast<cMetaParticleEffectCommand *>(gObject)->SetContext(w.parser, stateInterface); },
            [&] { w.After(); }, [&] { w.Before(); });
    }
}

namespace {
// The meta block's world: the object (its parser state: the world's state block, the manager at +0x1b0), its face at
// +4, its name (+0x248), its description's rotate values (+0x1b4), effect name (+0x1c8) and path points (+0x198).
struct MetaEffectWorld {
    World &w;
    FakeVTable managerVt{40}, componentVt{41};
    uint32_t component[0x1f8 / 4] = {};
    uint32_t managerObject[4] = {};
    const char *name, *effect;
    std::vector<float> values, points;
    size_t valuesCapacity = 0, pointCount = 0;
    explicit MetaEffectWorld(World &world) : w(world)
    {
        RandomObject(w, 0x260);
        *reinterpret_cast<void **>(gObject + 4) = w.face;
        *reinterpret_cast<void **>(gObject + 0x40) = w.state;
        managerVt.slots[8] = reinterpret_cast<void *>(&ManagerFind);
        managerVt.slots[7] = reinterpret_cast<void *>(&MetaManagerAdd);
        componentVt.Set<8>(0);
        component[0] = reinterpret_cast<uint32_t>(componentVt.slots);
        component[4] = 0xC0DE0000u + w.gen() % 100; // the found description's token
        managerObject[0] = reinterpret_cast<uint32_t>(managerVt.slots);
        w.Put(0x1b0, w.gen() % 4 == 0 ? 0 : reinterpret_cast<uint32_t>(managerObject));
        name = kBlockWords[w.gen() % std::size(kBlockWords)];
        effect = kTexts[w.gen() % std::size(kTexts)];
        values.resize(w.gen() % 3);
        for (float &f : values) f = kFloats[w.gen() % std::size(kFloats)];
        valuesCapacity = values.size() + w.gen() % 2;
        pointCount = w.gen() % 3;
        points.resize(pointCount * 7);
        for (float &f : points) f = kFloats[w.gen() % std::size(kFloats)];
        gMetaValues = static_cast<int>(w.gen() % 3);
        gMetaEffect = kTexts[w.gen() % std::size(kTexts)];
        for (size_t off : {0x248u, 0x1c8u, 0x1b4u, 0x198u}) std::memset(gObject + off, 0, off == 0x1b4u || off == 0x198u ? 12 : 0x18);
        w.before = [this] {
            gToken = 0;
            FreeWatch::Reset();
            new (gObject + 0x248) std::string(name);
            auto *s = new (gObject + 0x1c8) std::string(effect);
            if (s->capacity() > 15) Watch(3, s->data());
            auto *v = new (gObject + 0x1b4) std::vector<float>();
            v->reserve(valuesCapacity);
            v->assign(values.begin(), values.end());
            if (v->data()) Watch(2, v->data());
            auto *p = new (gObject + 0x198) std::vector<float>(points);
            (void)p;
        };
        w.after = [this] {
            auto *s = reinterpret_cast<std::string *>(gObject + 0x248);
            ExtraText(s->c_str());
            s->~basic_string();
            auto *e = reinterpret_cast<std::string *>(gObject + 0x1c8);
            ExtraText(e->c_str());
            ExtraWord(static_cast<uint32_t>(e->capacity()));
            e->~basic_string();
            auto *v = reinterpret_cast<std::vector<float> *>(gObject + 0x1b4);
            ExtraWord(static_cast<uint32_t>(v->size()));
            ExtraWord(static_cast<uint32_t>(v->capacity()));
            for (float f : *v) ExtraWord(Bits(f));
            v->~vector();
            reinterpret_cast<std::vector<float> *>(gObject + 0x198)->~vector();
            FreeWatch::Capture();
            for (size_t off : {0x248u, 0x1c8u, 0x1b4u, 0x198u})
                std::memset(gObject + off, 0, off == 0x1b4u || off == 0x198u ? 12 : 0x18);
        };
    }
};
} // namespace

DIFF_TEST(MetaParticleEffect_Execute, 0xbed9e0)
{
    auto orig = ctx.Original<ExecuteFn>(0xbed9e0);
    MetaStands stands;
    FreeWatch watch;
    gMetaDestroy = true;
    std::mt19937 gen(0xbed9e0);
    const int32_t counts[] = {1, 2, 3, 3, 3, 0, static_cast<int32_t>(0x80000003u)};
    for (int i = 0; i < 300; ++i) {
        World w(gen);
        MetaEffectWorld e(w);
        gPositionalCount = counts[gen() % std::size(counts)];
        for (auto &s : w.argv) s = kBlockWords[gen() % std::size(kBlockWords)];
        if (gen() % 2) w.argv[1] = ":";
        gFindResult = gen() % 4 == 0 ? 0 : (gen() % 3 == 0 ? 0x100 : 1);
        gFound = gFindResult & 0xff ? e.component : (gen() % 2 ? e.component : nullptr);
        char what[64];
        std::snprintf(what, sizeof(what), "Execute case %d", i);
        Run(
            ctx, what, With(w, gObject, 0x260), [&] { orig(gObject, w.args); },
            [&] { reinterpret_cast<cMetaParticleEffectCommand *>(gObject)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); },
            [&] { w.After(); }, [&] { w.Before(); });
    }
}

DIFF_TEST(MetaParticleEffect_EndBlock, 0xbefbc0)
{
    auto orig = ctx.Original<VoidFn>(0xbefbc0);
    MetaStands stands;
    FreeWatch watch;
    gMetaDestroy = true;
    std::mt19937 gen(0xbefbc0);
    for (int i = 0; i < 60; ++i) {
        World w(gen);
        MetaEffectWorld e(w);
        char what[64];
        std::snprintf(what, sizeof(what), "EndBlock case %d", i);
        Run(
            ctx, what, With(w, gObject, 0x260), [&] { orig(gObject); },
            [&] { reinterpret_cast<cMetaParticleEffectCommand *>(gObject)->EndBlock(); }, [&] { w.After(); }, [&] { w.Before(); });
    }
}

DIFF_TEST(MetaParticleEffect_Destructor, 0xbed360)
{
    gMetaDestroy = false; // the object's description is random bytes: its destructor (a stand-in) only logs
    DestructorCases<MetaStands>(
        ctx, 0xbed360, sizeof(cMetaParticleEffectCommand), 0x248,
        [](void *p, uint32_t f) -> void * { return reinterpret_cast<cMetaParticleEffectCommand *>(p)->DeletingDestructor(f); }, 40);
    gMetaDestroy = true;
}

// ---- the meta block's path -------------------------------------------------------------------------------------------

DIFF_TEST(MetaParticlePath_SetContext, 0xbf0900)
{
    auto orig = ctx.Original<SetContextFn>(0xbf0900);
    MetaStands stands;
    FakeVTable blockVt{-3};
    blockVt.slots[10] = reinterpret_cast<void *>(&ParticleAddCommand);
    std::mt19937 gen(0xbf0900);
    for (int i = 0; i < 20; ++i) {
        World w(gen);
        RandomObject(w, 0x84);
        *reinterpret_cast<void **>(gObject) = blockVt.slots;
        w.parserVt.ret[43] = w.RandomResult();
        void *state = gen() % 4 == 0 ? nullptr : w.state;
        char what[64];
        std::snprintf(what, sizeof(what), "SetContext case %d", i);
        Run(
            ctx, what, With(w, gObject, 0x84), [&] { orig(gObject, w.parser, state); },
            [&] { reinterpret_cast<cMetaParticlePathCommand *>(gObject)->SetContext(w.parser, state); }, [&] { w.After(); },
            [&] { w.Before(); });
    }
}

DIFF_TEST(MetaParticlePath_Execute, 0xbee8a0)
{
    auto orig = ctx.Original<ExecuteFn>(0xbee8a0);
    MetaStands stands;
    std::mt19937 gen(0xbee8a0);
    for (int i = 0; i < 60; ++i) {
        World w(gen);
        RandomObject(w, 0x84);
        *reinterpret_cast<void **>(gObject + 4) = w.face;
        *reinterpret_cast<void **>(gObject + 0x40) = w.state;
        w.argsVt.slots[1] = reinterpret_cast<void *>(&ArgsTakeSwitch);
        gSwitches.clear();
        if (gen() % 2) gSwitches.push_back({"tractor", gen() % 3 == 0 ? 0x100u : (gen() % 2 ? 1u : 0x201u)});
        char what[64];
        std::snprintf(what, sizeof(what), "Execute case %d", i);
        Run(
            ctx, what, With(w, gObject, 0x84), [&] { orig(gObject, w.args); },
            [&] { reinterpret_cast<cMetaParticlePathCommand *>(gObject)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); },
            [&] { w.After(); }, [&] { w.Before(); });
    }
}

DIFF_TEST(MetaParticlePath_EndBlock, 0xbefca0)
{
    auto orig = ctx.Original<VoidFn>(0xbefca0);
    MetaStands stands;
    std::mt19937 gen(0xbefca0);
    for (int i = 0; i < 60; ++i) {
        World w(gen);
        RandomObject(w, 0x84);
        *reinterpret_cast<void **>(gObject + 0x40) = w.state;
        const uint8_t flags[] = {0, 1, 0x80, 0x100 & 0xff};
        gObject[0x80] = flags[gen() % 4];
        char what[64];
        std::snprintf(what, sizeof(what), "EndBlock case %d", i);
        Run(
            ctx, what, With(w, gObject, 0x84), [&] { orig(gObject); },
            [&] { reinterpret_cast<cMetaParticlePathCommand *>(gObject)->EndBlock(); }, [&] { w.After(); }, [&] { w.Before(); });
    }
}
