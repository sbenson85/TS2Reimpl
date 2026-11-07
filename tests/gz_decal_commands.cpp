// Differential tests of the decal block (`decal`) and its commands (src/gz/ParticleCommands.cpp) against the originals.
// The world is tests/gz_particle_commands.h's; the decal description's functions (ours and the original's) stand in with ones
// that log, and the description's own containers (the texture name, the colours) are real.
#include "gz_particle_commands.h"

#include "ts/cTSCompDescription.h"

using namespace ptest;

namespace {

using DecalAdapters = cTSCompDescriptionAdapters<cTSDecalComponentDescription>;

// ---- the decal description's functions (constructor 0xbeb2c0, destructor 0xbeb410, move 0xbeb5a0, copy 0xbeb760) and
// the curve's scaling (0xbdc9f0): they log (the constructor writes a token in the first word).
void *__fastcall DecalConstruct(void *self, void *)
{
    ++gToken;
    *static_cast<uint32_t *>(self) = 0xDECA0000u + gToken;
    Log(70, Where(self), gToken);
    return self;
}
void __fastcall DecalDestroy(void *self, void *) { Log(71, Where(self), *static_cast<uint32_t *>(self)); }
void *__fastcall DecalMove(void *self, void *, void *from)
{
    Log(72, Where(self), Where(from), *static_cast<const uint32_t *>(from));
    return self;
}
void *__fastcall DecalCopy(void *self, void *, const void *from)
{
    Log(73, Where(self), Where(from), *static_cast<const uint32_t *>(from));
    return self;
}
void __fastcall CurveScale(void *self, void *, float by) { Log(74, Where(self), Bits(by)); }
// The manager's slot 7 for a decal component: its words and the description's token, then it is freed.
uint32_t __fastcall DecalManagerAdd(void *self, void *, const char *name, void *component)
{
    Record(self, 7, 0, component != nullptr);
    ExtraText(name);
    if (component) {
        const uint32_t *w = static_cast<const uint32_t *>(component);
        for (int k = 0; k < 4; ++k) ExtraWord(w[k]);
        ExtraWord(static_cast<uint32_t>(HeapSize(GetProcessHeap(), 0, component)));
        ::operator delete(component);
    }
    return 0;
}
// The face's slot 39 (an RGB colour): the next of gVectors, `out` or elsewhere.
float *__fastcall FaceEvalColor(void *self, void *, float *out, const char *text)
{
    Record(self, 39, Arg(out), reinterpret_cast<uint32_t>(text));
    const float *v = gVectors[gVectorNext++ % 4];
    for (int k = 0; k < 3; ++k) out[k] = v[k];
    return gReturnElsewhere ? gElsewhere : out;
}

struct DecalStands {
    ParticleStands particles;
    shape::StandIn construct{0xbeb2c0u, &DecalAdapters::ConstructDescription, &DecalConstruct};
    shape::StandIn destroy{0xbeb410u, &DecalAdapters::DestroyDescription, &DecalDestroy};
    shape::StandIn move{0xbeb5a0u, &DecalAdapters::MoveDescription, &DecalMove};
    shape::StandIn copy{0xbeb760u, &DecalAdapters::CopyDescription, &DecalCopy};
    shape::StandIn scale{0xbdc9f0u, &cTSEffectCurve::Divide, &CurveScale}; // ours since lane IX
};

const float kFloats[] = {0.5f, -2.0f, 0.0f, 3.0e38f, 1.0f, -0.0f, 7.25f, 1e-7f, 0.25f, 255.0f, 2.5e-3f, 1.0e-30f, 4.0f, -3.5f};

// ---- the commands ------------------------------------------------------------------------------------------------------

constexpr size_t kDecal = 0x100; // the description in the world's state block
const char *const kChildWords[] = {"a", "bb", "ccc", "dddd", "nope", "", "a much longer name than fifteen", "x y"};
const char *const kCommandNames[] = {"color", "color255", "colour255", "alpha255", "alpha", "size", "x255", "255", "2555", "255 "};
const char *const kSwitchNames[] = {"terrainScale", "static", "loop", "single", "sustain", "light", "water", "ring"};
const char *const kSwitchArgNames[] = {"vary", "draw", "repeat", "offset"};

// The description's containers: its texture name (+8) and colours (+0x58, 12-byte elements). Before each side's run
// they are built (the colours' block from the STL allocator, spare room drawn); after it, logged (text, size, capacity,
// whether the colours' block was kept, the colours) and freed.
struct DecalContainers {
    World &w;
    const char *texture;
    std::vector<float> colors;
    size_t count, capacity;
    uint8_t *block = nullptr;
    uint8_t *At(size_t offset) { return w.state + kDecal + offset; }
    explicit DecalContainers(World &world) : w(world)
    {
        texture = kTexts[w.gen() % std::size(kTexts)];
        count = w.gen() % 4;
        capacity = count + w.gen() % 3;
        colors.resize(count * 3);
        for (float &f : colors) f = kFloats[w.gen() % std::size(kFloats)];
        Clear();
    }
    void Clear()
    {
        std::memset(At(8), 0, 0x18);
        std::memset(At(0x58), 0, 12);
    }
    void Before()
    {
        new (At(8)) std::string(texture);
        block = capacity ? std::allocator<uint8_t>().allocate(capacity * 12) : nullptr;
        if (count) std::memcpy(block, colors.data(), count * 12);
        *reinterpret_cast<RawVector *>(At(0x58)) = {block, block + count * 12, block + capacity * 12};
    }
    void After()
    {
        auto *s = reinterpret_cast<std::string *>(At(8));
        ExtraText(s->c_str());
        s->~basic_string();
        auto *v = reinterpret_cast<RawVector *>(At(0x58));
        const size_t cap = static_cast<size_t>(v->mCapacity - v->mBegin);
        ExtraWord(static_cast<uint32_t>(v->mEnd - v->mBegin));
        ExtraWord(static_cast<uint32_t>(cap));
        ExtraWord(cap == capacity * 12 ? v->mBegin == block : 2);
        if (v->mBegin != v->mEnd) ExtraBytes(v->mBegin, static_cast<size_t>(v->mEnd - v->mBegin));
        if (v->mBegin) std::allocator<uint8_t>().deallocate(v->mBegin, cap);
        Clear();
    }
};

template <class C> void DecalChildCases(difftest::Context &ctx, int cases)
{
    auto orig = ctx.Original<ExecuteFn>(C::kExecute);
    DecalStands stands;
    EnumTableFill draw(ctx, 0x10fd1e8, 0x20);
    std::mt19937 gen(C::kExecute);
    const int32_t counts[] = {0, 1, 2, 3, 5, 8, static_cast<int32_t>(0x80000002u)};
    for (int i = 0; i < cases; ++i) {
        World w(gen);
        DecalContainers containers(w);
        gObjectBase = w.cmd;
        gStateBase = w.state;
        *reinterpret_cast<void **>(w.cmd + 4) = w.face;
        *reinterpret_cast<void **>(w.cmd + 0xc) = w.state + kDecal;
        w.faceVt.slots[31] = reinterpret_cast<void *>(&FaceEvalFloat);
        w.faceVt.slots[34] = reinterpret_cast<void *>(&FaceEvalVector2);
        w.faceVt.slots[39] = reinterpret_cast<void *>(&FaceEvalColor);
        w.argsVt.slots[1] = reinterpret_cast<void *>(&ArgsTakeSwitch);
        w.argsVt.slots[3] = reinterpret_cast<void *>(&ArgsTakeSwitchArgs);
        w.argsVt.ret[4] = reinterpret_cast<uint32_t>(kCommandNames[gen() % std::size(kCommandNames)]); // the command's name
        for (auto &s : w.argv) s = kChildWords[gen() % std::size(kChildWords)];
        const char *switchValues[4];
        for (auto &s : switchValues) s = kChildWords[gen() % std::size(kChildWords)];
        gPositionalCount = counts[gen() % std::size(counts)];
        // The colours read one argument per count: at most the eight the world has.
        if (C::kExecute == cDecalColorCommand::kExecute && (gPositionalCount < 0 || gPositionalCount > 8)) gPositionalCount = 8;
        // The description's plain values: often from the float table (negative zeros included).
        for (size_t off = 0x20; off < 0x8c; off += 4)
            if (gen() % 4 != 0) w.Put(kDecal + off, Bits(kFloats[gen() % std::size(kFloats)]));
        w.Put(kDecal + 4, gen()); // the flags
        const uint32_t density = gen() % 4;
        gSwitches.clear();
        for (const char *s : kSwitchNames)
            if (gen() % 4 < density) gSwitches.push_back({s, gen() % 3 == 0 ? 0x100u : 1u});
        gSwitchArgs.clear();
        for (const char *s : kSwitchArgNames)
            if (gen() % 4 < density) gSwitchArgs.push_back({s, switchValues});
        for (float &f : gFloats) f = kFloats[gen() % std::size(kFloats)];
        for (auto &v : gVectors)
            for (float &f : v) f = kFloats[gen() % std::size(kFloats)];
        for (float &f : gElsewhere) f = kFloats[gen() % std::size(kFloats)];
        for (float &f : gRanged) f = kFloats[gen() % std::size(kFloats)];
        gReturnElsewhere = gen() % 3 == 0;
        w.before = [&] {
            gFloatNext = 0;
            gVectorNext = 0;
            gRangedNext = 0;
            gToken = 0;
            containers.Before();
        };
        w.after = [&] { containers.After(); };
        char what[64];
        std::snprintf(what, sizeof(what), "%08x case %d", C::kExecute, i);
        Differential(
            ctx, what, w.Regions(), [&] { return RunGuarded([&] { orig(w.cmd, w.args); }); },
            [&] { return RunGuarded([&] { reinterpret_cast<C *>(w.cmd)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); }); },
            [&] { w.After(); }, [&] { w.Before(); });
    }
}

} // namespace

DIFF_TEST(DecalCommand_Color, 0xbebb60) { DecalChildCases<cDecalColorCommand>(ctx, 200); }
DIFF_TEST(DecalCommand_Alpha, 0xbeb990) { DecalChildCases<cDecalAlphaCommand>(ctx, 120); }
DIFF_TEST(DecalCommand_Curves, 0xbec050)
{
    DecalChildCases<cDecalSizeCommand>(ctx, 80);   // 0xbec050
    DecalChildCases<cDecalAspectCommand>(ctx, 20); // 0xbebb40
    DecalChildCases<cDecalRotateCommand>(ctx, 60); // 0xbebff0
}
DIFF_TEST(DecalCommand_Life, 0xbebf30) { DecalChildCases<cDecalLifeCommand>(ctx, 150); }
DIFF_TEST(DecalCommand_Texture, 0xbec0d0) { DecalChildCases<cDecalTextureCommand>(ctx, 250); }

// ---- decal ---------------------------------------------------------------------------------------------------------------

DIFF_TEST(DecalEffect_SetContext, 0xbec320)
{
    auto orig = ctx.Original<SetContextFn>(0xbec320);
    DecalStands stands;
    FakeVTable blockVt{-3};
    blockVt.slots[10] = reinterpret_cast<void *>(&ParticleAddCommand);
    std::mt19937 gen(0xbec320);
    for (int i = 0; i < 20; ++i) {
        World w(gen);
        RandomObject(w, sizeof(cDecalEffectCommand));
        *reinterpret_cast<void **>(gObject) = blockVt.slots;
        w.parserVt.ret[43] = w.RandomResult();
        void *stateInterface = gen() % 4 == 0 ? nullptr : w.state + 4;
        char what[64];
        std::snprintf(what, sizeof(what), "SetContext case %d", i);
        Run(
            ctx, what, With(w, gObject, sizeof(cDecalEffectCommand)), [&] { orig(gObject, w.parser, stateInterface); },
            [&] { reinterpret_cast<cDecalEffectCommand *>(gObject)->SetContext(w.parser, stateInterface); },
            [&] { w.After(); }, [&] { w.Before(); });
    }
}

namespace {
// The decal block's world: the object (its parser state: the world's state block, the manager at +0x1b0), its face at
// +4, its name (+0xd4, real); the description's bytes are random (only the stand-ins see them).
struct DecalEffectWorld {
    World &w;
    FakeVTable managerVt{40}, componentVt{41};
    uint32_t component[0x98 / 4] = {};
    uint32_t managerObject[4] = {};
    const char *name;
    explicit DecalEffectWorld(World &world) : w(world)
    {
        RandomObject(w, sizeof(cDecalEffectCommand));
        *reinterpret_cast<void **>(gObject + 4) = w.face;
        *reinterpret_cast<void **>(gObject + 0x40) = w.state;
        managerVt.slots[8] = reinterpret_cast<void *>(&ManagerFind);
        managerVt.slots[7] = reinterpret_cast<void *>(&DecalManagerAdd);
        componentVt.Set<8>(0);
        component[0] = reinterpret_cast<uint32_t>(componentVt.slots);
        component[3] = 0xC0DE0000u + w.gen() % 100; // the found description's token
        managerObject[0] = reinterpret_cast<uint32_t>(managerVt.slots);
        w.Put(0x1b0, w.gen() % 4 == 0 ? 0 : reinterpret_cast<uint32_t>(managerObject));
        name = kBlockWords[w.gen() % std::size(kBlockWords)];
        std::memset(gObject + 0xd4, 0, 0x18);
        w.before = [this] {
            gToken = 0;
            new (gObject + 0xd4) std::string(name);
        };
        w.after = [this] {
            auto *s = reinterpret_cast<std::string *>(gObject + 0xd4);
            ExtraText(s->c_str());
            s->~basic_string();
            std::memset(gObject + 0xd4, 0, 0x18);
        };
    }
};
} // namespace

DIFF_TEST(DecalEffect_Execute, 0xbebdb0)
{
    auto orig = ctx.Original<ExecuteFn>(0xbebdb0);
    DecalStands stands;
    std::mt19937 gen(0xbebdb0);
    const int32_t counts[] = {1, 2, 3, 3, 3, 0, static_cast<int32_t>(0x80000003u)};
    for (int i = 0; i < 200; ++i) {
        World w(gen);
        DecalEffectWorld e(w);
        gPositionalCount = counts[gen() % std::size(counts)];
        for (auto &s : w.argv) s = kBlockWords[gen() % std::size(kBlockWords)];
        if (gen() % 2) w.argv[1] = ":";
        gFindResult = gen() % 4 == 0 ? 0 : (gen() % 3 == 0 ? 0x100 : 1);
        gFound = gFindResult & 0xff ? e.component : (gen() % 2 ? e.component : nullptr);
        char what[64];
        std::snprintf(what, sizeof(what), "Execute case %d", i);
        Run(
            ctx, what, With(w, gObject, sizeof(cDecalEffectCommand)), [&] { orig(gObject, w.args); },
            [&] { reinterpret_cast<cDecalEffectCommand *>(gObject)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); },
            [&] { w.After(); }, [&] { w.Before(); });
    }
}

DIFF_TEST(DecalEffect_EndBlock, 0xbec260)
{
    auto orig = ctx.Original<VoidFn>(0xbec260);
    DecalStands stands;
    std::mt19937 gen(0xbec260);
    for (int i = 0; i < 60; ++i) {
        World w(gen);
        DecalEffectWorld e(w);
        char what[64];
        std::snprintf(what, sizeof(what), "EndBlock case %d", i);
        Run(
            ctx, what, With(w, gObject, sizeof(cDecalEffectCommand)), [&] { orig(gObject); },
            [&] { reinterpret_cast<cDecalEffectCommand *>(gObject)->EndBlock(); }, [&] { w.After(); }, [&] { w.Before(); });
    }
}

DIFF_TEST(DecalEffect_Destructor, 0xbeb8d0)
{
    DestructorCases<DecalStands>(
        ctx, 0xbeb8d0, sizeof(cDecalEffectCommand), 0xd4,
        [](void *p, uint32_t f) -> void * { return reinterpret_cast<cDecalEffectCommand *>(p)->DeletingDestructor(f); }, 40);
}
