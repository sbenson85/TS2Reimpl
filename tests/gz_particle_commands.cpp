// Differential tests of the particle effect script's block commands (`particles`, `path`) and the path's commands
// (src/gz/ParticleCommands.cpp) against the originals. The world is tests/gz_particle_commands.h's.
#include "gz_particle_commands.h"

using namespace ptest;


// ---- particles -----------------------------------------------------------------------------------------------------

DIFF_TEST(ParticleEffect_SetContext, 0xbe5b00)
{
    auto orig = ctx.Original<SetContextFn>(0xbe5b00);
    ParticleStands stands;
    FakeVTable blockVt{-3};
    blockVt.slots[10] = reinterpret_cast<void *>(&ParticleAddCommand);
    std::mt19937 gen(0xbe5b00);
    for (int i = 0; i < 20; ++i) {
        World w(gen);
        RandomObject(w, 0x2f0);
        *reinterpret_cast<void **>(gObject) = blockVt.slots;
        w.parserVt.ret[43] = w.RandomResult();
        void *stateInterface = gen() % 4 == 0 ? nullptr : w.state + 4;
        char what[64];
        std::snprintf(what, sizeof(what), "SetContext case %d", i);
        Run(
            ctx, what, With(w, gObject, 0x2f0), [&] { orig(gObject, w.parser, stateInterface); },
            [&] { reinterpret_cast<cParticleEffectCommand *>(gObject)->SetContext(w.parser, stateInterface); }, [&] { w.After(); },
            [&] { w.Before(); });
    }
}

namespace {
// The effect block's world: the object (its parser state: the world's state block, the manager at +0x1b0), its name.
struct EffectWorld {
    World &w;
    FakeVTable managerVt{40}, componentVt{41};
    uint32_t component[0x298 / 4] = {};
    uint32_t managerObject[4] = {};
    const char *name;
    explicit EffectWorld(World &world) : w(world)
    {
        RandomObject(w, 0x2f0);
        *reinterpret_cast<void **>(gObject + 0x40) = w.state;
        managerVt.slots[8] = reinterpret_cast<void *>(&ManagerFind);
        managerVt.slots[7] = reinterpret_cast<void *>(&ManagerAdd);
        componentVt.Set<8>(0);
        component[0] = reinterpret_cast<uint32_t>(componentVt.slots);
        component[4] = 0xC0DE0000u + w.gen() % 100; // the found description's token
        managerObject[0] = reinterpret_cast<uint32_t>(managerVt.slots);
        w.Put(0x1b0, w.gen() % 4 == 0 ? 0 : reinterpret_cast<uint32_t>(managerObject));
        name = kBlockWords[w.gen() % std::size(kBlockWords)];
        std::memset(gObject + 0x2d8, 0, 0x18);
        w.before = [this] {
            gToken = 0;
            new (gObject + 0x2d8) std::string(name);
        };
        w.after = [this] {
            auto *s = reinterpret_cast<std::string *>(gObject + 0x2d8);
            ExtraText(s->c_str());
            s->~basic_string();
            std::memset(gObject + 0x2d8, 0, 0x18);
        };
    }
};
} // namespace

DIFF_TEST(ParticleEffect_Execute, 0xbe2f30)
{
    auto orig = ctx.Original<ExecuteFn>(0xbe2f30);
    ParticleStands stands;
    std::mt19937 gen(0xbe2f30);
    const int32_t counts[] = {1, 2, 3, 3, 3, 0, static_cast<int32_t>(0x80000003u)};
    for (int i = 0; i < 300; ++i) {
        World w(gen);
        EffectWorld e(w);
        gPositionalCount = counts[gen() % std::size(counts)];
        for (auto &s : w.argv) s = kBlockWords[gen() % std::size(kBlockWords)];
        if (gen() % 2) w.argv[1] = ":";
        gFindResult = gen() % 4 == 0 ? 0 : (gen() % 3 == 0 ? 0x100 : 1);
        gFound = gFindResult & 0xff ? e.component : (gen() % 2 ? e.component : nullptr);
        char what[64];
        std::snprintf(what, sizeof(what), "Execute case %d", i);
        Run(
            ctx, what, With(w, gObject, 0x2f0), [&] { orig(gObject, w.args); },
            [&] { reinterpret_cast<cParticleEffectCommand *>(gObject)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); },
            [&] { w.After(); }, [&] { w.Before(); });
    }
}

DIFF_TEST(ParticleEffect_EndBlock, 0xbe59b0)
{
    auto orig = ctx.Original<VoidFn>(0xbe59b0);
    ParticleStands stands;
    std::mt19937 gen(0xbe59b0);
    for (int i = 0; i < 60; ++i) {
        World w(gen);
        EffectWorld e(w);
        char what[64];
        std::snprintf(what, sizeof(what), "EndBlock case %d", i);
        Run(
            ctx, what, With(w, gObject, 0x2f0), [&] { orig(gObject); },
            [&] { reinterpret_cast<cParticleEffectCommand *>(gObject)->EndBlock(); }, [&] { w.After(); }, [&] { w.Before(); });
    }
}

DIFF_TEST(ParticleEffect_Destructor, 0xbe2b50)
{
    DestructorCases<ParticleStands>(
        ctx, 0xbe2b50, sizeof(cParticleEffectCommand), 0x2d8,
        [](void *p, uint32_t f) -> void * { return reinterpret_cast<cParticleEffectCommand *>(p)->DeletingDestructor(f); }, 40);
}

// ---- path ----------------------------------------------------------------------------------------------------------

DIFF_TEST(ParticlePath_SetContext, 0xbe6a30)
{
    auto orig = ctx.Original<SetContextFn>(0xbe6a30);
    ParticleStands stands;
    FakeVTable blockVt{-3};
    blockVt.slots[10] = reinterpret_cast<void *>(&ParticleAddCommand);
    std::mt19937 gen(0xbe6a30);
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
            [&] { reinterpret_cast<cParticlePathCommand *>(gObject)->SetContext(w.parser, state); }, [&] { w.After(); },
            [&] { w.Before(); });
    }
}

DIFF_TEST(ParticlePath_Execute, 0xbe3be0)
{
    auto orig = ctx.Original<ExecuteFn>(0xbe3be0);
    ParticleStands stands;
    std::mt19937 gen(0xbe3be0);
    for (int i = 0; i < 120; ++i) {
        World w(gen);
        RandomObject(w, 0x84);
        *reinterpret_cast<void **>(gObject + 4) = w.face;
        *reinterpret_cast<void **>(gObject + 0x40) = w.state;
        w.argsVt.slots[1] = reinterpret_cast<void *>(&ArgsTakeSwitch);
        gSwitches.clear();
        for (const char *s : {"tractor", "noSnap", "preExtend", "postExtend"})
            if (gen() % 2) gSwitches.push_back({s, gen() % 3 == 0 ? 0x100u : (gen() % 2 ? 1u : 0x201u)});
        char what[64];
        std::snprintf(what, sizeof(what), "Execute case %d", i);
        Run(
            ctx, what, With(w, gObject, 0x84), [&] { orig(gObject, w.args); },
            [&] { reinterpret_cast<cParticlePathCommand *>(gObject)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); },
            [&] { w.After(); }, [&] { w.Before(); });
    }
}

DIFF_TEST(ParticlePath_EndBlock, 0xbe5a90)
{
    auto orig = ctx.Original<VoidFn>(0xbe5a90);
    ParticleStands stands;
    std::mt19937 gen(0xbe5a90);
    for (int i = 0; i < 60; ++i) {
        World w(gen);
        RandomObject(w, 0x84);
        *reinterpret_cast<void **>(gObject + 0x40) = w.state;
        const uint8_t flags[] = {0, 1, 0x80, 0x100 & 0xff};
        gObject[0x80] = flags[gen() % 4];
        gObject[0x81] = flags[gen() % 4];
        char what[64];
        std::snprintf(what, sizeof(what), "EndBlock case %d", i);
        Run(
            ctx, what, With(w, gObject, 0x84), [&] { orig(gObject); },
            [&] { reinterpret_cast<cParticlePathCommand *>(gObject)->EndBlock(); }, [&] { w.After(); }, [&] { w.Before(); });
    }
}

DIFF_TEST(ParticlePath_Destructor, 0xbe2b80)
{
    DestructorCases<ParticleStands>(
        ctx, 0xbe2b80, sizeof(cParticlePathCommand), 0,
        [](void *p, uint32_t f) -> void * { return reinterpret_cast<cParticlePathCommand *>(p)->DeletingDestructor(f); }, 30);
}

// ---- the path's commands -------------------------------------------------------------------------------------------

namespace {
template <class C> void PathCommandCases(difftest::Context &ctx, int cases)
{
    auto orig = ctx.Original<ExecuteFn>(C::kExecute);
    ParticleStands stands;
    std::mt19937 gen(C::kExecute);
    const float floats[] = {0.5f, -2.0f, 0.0f, 3.0e38f, 1.0f, -0.0f, 7.25f};
    for (int i = 0; i < cases; ++i) {
        World w(gen);
        gObjectBase = w.cmd;
        gStateBase = w.state;
        *reinterpret_cast<void **>(w.cmd + 4) = w.face;
        *reinterpret_cast<void **>(w.cmd + 0xc) = w.state;
        w.faceVt.slots[31] = reinterpret_cast<void *>(&FaceEvalFloat);
        w.faceVt.slots[35] = reinterpret_cast<void *>(&FaceEvalVector3);
        for (float &f : gFloats) f = floats[gen() % std::size(floats)];
        for (auto &v : gVectors)
            for (float &f : v) f = floats[gen() % std::size(floats)];
        for (float &f : gElsewhere) f = floats[gen() % std::size(floats)];
        gReturnElsewhere = gen() % 3 == 0;
        w.before = [] {
            gFloatNext = 0;
            gVectorNext = 0;
        };
        char what[64];
        std::snprintf(what, sizeof(what), "%08x case %d", C::kExecute, i);
        Run(
            ctx, what, w.Regions(), [&] { orig(w.cmd, w.args); },
            [&] { reinterpret_cast<C *>(w.cmd)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); }, [&] { w.After(); },
            [&] { w.Before(); });
    }
}
} // namespace

DIFF_TEST(ParticlePath_Commands, 0xbe57b0)
{
    PathCommandCases<cPathPointCommand>(ctx, 40);      // 0xbe57b0 (each body shared with the meta path's)
    PathCommandCases<cPathDeltaPointCommand>(ctx, 40); // 0xbe5730
    PathCommandCases<cPathVelocityCommand>(ctx, 40);   // 0xbe5870
    PathCommandCases<cPathSpeedCommand>(ctx, 30);      // 0xbe57f0
    PathCommandCases<cPathTimeCommand>(ctx, 30);       // 0xbe5830
    PathCommandCases<cPathDeltaTimeCommand>(ctx, 30);  // 0xbe5770
}
