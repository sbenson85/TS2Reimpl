// Differential tests of the `particles` block's commands (src/gz/ParticleCommands.cpp) against the originals: each runs
// with the world's state block as the particles state (real strings and vectors in the fields the commands use), the
// face's float / vector slots answering from tables, switches by name, and the game functions they hand work to
// patched with recording stand-ins (tests/gz_particle_commands.h).
#include "gz_particle_commands.h"

using namespace ptest;

namespace {


const char *const kChildWords[] = {"a", "bb", "ccc", "dddd", "nope", "", "a much longer name than fifteen", "smoke", "x y"};
const char *const kSwitchNames[] = {"base",        "radial",         "scaleExisting",  "light",          "reset",
                                    "acceptOverride", "acceptComposite", "pinToTerrain", "sticky",       "killOutsideTerrain",
                                    "point",       "scaleParticles", "resetIncoming"};
const char *const kSwitchArgNames[] = {
    "speed",       "count",           "start",          "delay",       "strength",      "turn",          "tile",
    "draw",        "layer",           "directLayer",    "bounce",      "death",         "deathByWater",  "limitHeight",
    "vary",        "multVaryRange",   "offset",         "axis",        "pivot",         "pivotVary",     "square",
    "quad",        "cube",            "box",            "circle",      "sphere",        "ellipse",       "ellipsoid",
    "ring",        "torus",           "constrain",      "constrainOffset", "belowHeight", "aboveHeight", "heightRange",
    "gravity",     "wind",            "globalWind",     "screw",       "screwRadius",   "screwRadiusToSpeed",
    "screwRadiusVary", "screwRadiusMax", "screwAngle",  "screwAngleToSpeed", "screwAngleVary", "bomb", "drag",
    "presetAttractor", "terrainRepel", "wiggle",        "wiggleDir",   "screenBloom",   "bloomAlpha",    "bloomSize"};

template <class C> void ChildCases(difftest::Context &ctx, int cases)
{
    auto orig = ctx.Original<ExecuteFn>(C::kExecute);
    ParticleStands stands;
    AlignmentTable alignment(ctx);
    FreeWatch watch;
    std::mt19937 gen(C::kExecute);
    const float floats[] = {0.5f, -2.0f, 0.0f, 3.0e38f, 1.0f, -0.0f, 7.25f, 1e-7f, 0.25f, 0.999f, 2.5e-3f, 1.0e-30f};
    const int32_t counts[] = {0, 1, 2, 3, 3, 4, 5, static_cast<int32_t>(0x80000002u)};
    for (int i = 0; i < cases; ++i) {
        World w(gen);
        StateContainers containers(w);
        gObjectBase = w.cmd;
        gStateBase = w.state;
        *reinterpret_cast<void **>(w.cmd + 4) = w.face;
        *reinterpret_cast<void **>(w.cmd + 0xc) = w.state;
        // GetName (slot 5 of the command's own vtable) for the material command's message.
        w.cmdVt.vt.ret[5] = reinterpret_cast<uint32_t>("material");
        w.faceVt.slots[31] = reinterpret_cast<void *>(&FaceEvalFloat);
        w.faceVt.slots[34] = reinterpret_cast<void *>(&FaceEvalVector2);
        w.faceVt.slots[35] = reinterpret_cast<void *>(&FaceEvalVector3);
        w.faceVt.ret[33] = gen() % 2 ? gen() % 300 : gen();
        w.faceVt.ret[32] = gen() % 2 ? static_cast<uint32_t>(static_cast<int32_t>(gen() % 200) - 100) : gen();
        w.argsVt.slots[1] = reinterpret_cast<void *>(&ArgsTakeSwitch);
        w.argsVt.slots[2] = reinterpret_cast<void *>(&ArgsTakeSwitchArgsRange);
        w.argsVt.slots[3] = reinterpret_cast<void *>(&ArgsTakeSwitchArgs);
        for (auto &s : w.argv) s = kChildWords[gen() % std::size(kChildWords)];
        for (auto &s : w.switchArgs) s = kChildWords[gen() % std::size(kChildWords)];
        const char *switchValues[8];
        for (auto &s : switchValues) s = kChildWords[gen() % std::size(kChildWords)];
        gPositionalCount = counts[gen() % std::size(counts)];
        // The flags: random, with the material / shape bits often clear.
        uint32_t flagsLow = gen();
        if (gen() % 2) flagsLow &= ~0x600000u;
        w.Put(0x160, flagsLow);
        w.Put(0x164, gen());
        w.state[0x184] = static_cast<uint8_t>(gen() % 4); // how many rotate axes so far (at most 2 in the game)
        // The force so far: often from the float table (negative zeros included: the original adds zeros to it).
        if (gen() % 2)
            for (int k = 0; k < 3; ++k) w.Put(0xd0 + 4 * k, Bits(floats[gen() % std::size(floats)]));
        // The game's zero vector (the source box's start), from the float table too.
        float origin[3];
        for (float &f : origin) f = floats[gen() % std::size(floats)];
        ZeroVector zero(ctx, origin);
        // How many switches are given: none at all in some cases, about half in others.
        const uint32_t density = gen() % 4;
        gSwitches.clear();
        for (const char *s : kSwitchNames)
            if (gen() % 6 < density) gSwitches.push_back({s, gen() % 3 == 0 ? 0x100u : 1u});
        gSwitchArgs.clear();
        gSwitchCounts.clear();
        for (const char *s : kSwitchArgNames)
            if (gen() % 6 < density) {
                gSwitchArgs.push_back({s, switchValues});
                // -pivot's count bounds an unsigned loop over the values: kept within them.
                const int32_t count = counts[gen() % std::size(counts)];
                gSwitchCounts.push_back({s, std::strcmp(s, "pivot") == 0 && (count < 0 || count > 8) ? 2 : count});
            }
        gAttractorParseResult = gen() % 2 != 0;
        for (float &f : gFloats) f = floats[gen() % std::size(floats)];
        for (auto &v : gVectors)
            for (float &f : v) f = floats[gen() % std::size(floats)];
        for (float &f : gElsewhere) f = floats[gen() % std::size(floats)];
        for (float &f : gRanged) f = floats[gen() % std::size(floats)];
        for (float &f : gEmitVolume) f = gen() % 2 ? floats[gen() % std::size(floats)] : 1e-7f * static_cast<float>(gen() % 20);
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

DIFF_TEST(ParticleCommand_Forwarders, 0xbe2ed0)
{
    ChildCases<cParticleColorCommand>(ctx, 20);        // 0xbe2ed0 (shared with the meta color command)
    ChildCases<cParticleAlphaCommand>(ctx, 20);        // 0xbe2c70 (shared with the meta alpha command)
    ChildCases<cParticleSizeCommand>(ctx, 20);         // 0xbe43c0 (shared with the meta size command)
    ChildCases<cParticleLifeCommand>(ctx, 20);         // 0xbe3ab0 (shared with the meta life command)
    ChildCases<cParticleAspectCommand>(ctx, 20);       // 0xbe2c90
    ChildCases<cParticleTerrainRepelCommand>(ctx, 20); // 0xbe4d30
}
DIFF_TEST(ParticleCommand_Emit, 0xbe30c0) { ChildCases<cParticleEmitCommand>(ctx, 150); }
DIFF_TEST(ParticleCommand_Rates, 0xbe3fc0)
{
    ChildCases<cParticleRateCommand>(ctx, 20);     // 0xbe3fc0
    ChildCases<cParticleInjectCommand>(ctx, 20);   // 0xbe3a60
    ChildCases<cParticleMaintainCommand>(ctx, 20); // 0xbe3ad0
}
DIFF_TEST(ParticleCommand_Stretch, 0xbe4b40) { ChildCases<cParticleStretchCommand>(ctx, 30); }
DIFF_TEST(ParticleCommand_DeathEffect, 0xbe2ef0) { ChildCases<cParticleDeathEffectCommand>(ctx, 40); }
DIFF_TEST(ParticleCommand_Alignment, 0xbe2c40) { ChildCases<cParticleAlignmentCommand>(ctx, 60); }
DIFF_TEST(ParticleCommand_Frames, 0xbe39c0) { ChildCases<cParticleFramesCommand>(ctx, 120); }
DIFF_TEST(ParticleCommand_Material, 0xbe3b20) { ChildCases<cParticleMaterialCommand>(ctx, 150); }
DIFF_TEST(ParticleCommand_Shape, 0xbe4300) { ChildCases<cParticleShapeCommand>(ctx, 150); }
DIFF_TEST(ParticleCommand_RandomWalk, 0xbe3dd0) { ChildCases<cParticleRandomWalkCommand>(ctx, 300); }
DIFF_TEST(ParticleCommand_Attractor, 0xbe2cc0) { ChildCases<cParticleAttractorCommand>(ctx, 100); }
DIFF_TEST(ParticleCommand_Collide, 0xbe2da0) { ChildCases<cParticleCollideCommand>(ctx, 150); }
DIFF_TEST(ParticleCommand_PlaneCollision, 0xbe3ca0) { ChildCases<cParticlePlaneCollisionCommand>(ctx, 100); }
DIFF_TEST(ParticleCommand_TerrainCollide, 0xbe4b80) { ChildCases<cParticleTerrainCollideCommand>(ctx, 150); }
DIFF_TEST(ParticleCommand_Texture, 0xbe4d70) { ChildCases<cParticleTextureCommand>(ctx, 300); }
DIFF_TEST(ParticleCommand_Rotate, 0xbe4000) { ChildCases<cParticleRotateCommand>(ctx, 300); }
DIFF_TEST(ParticleCommand_Source, 0xbe43e0) { ChildCases<cParticleSourceCommand>(ctx, 500); }
DIFF_TEST(ParticleCommand_Force, 0xbe31a0) { ChildCases<cParticleForceCommand>(ctx, 500); }
DIFF_TEST(ParticleCommand_Warp, 0xbe5050) { ChildCases<cParticleWarpCommand>(ctx, 400); }
