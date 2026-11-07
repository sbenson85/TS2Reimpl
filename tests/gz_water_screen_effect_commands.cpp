// Differential tests of the water and screen blocks (`water`, `screen`), their commands, the group commands
// (`waterEffect`, `screenEffect`) and the registration functions (src/gz/EffectCommands.cpp) against the originals. The
// world is tests/gz_particle_commands.h's. The water description is plain values (its component's constructor, not ours,
// is a logging stand-in); the screen description's functions (not ours) are stand-ins that build and free real containers.
#include "gz_particle_commands.h"

#include "gz/EffectCommands.h"
#include "ts/cTSCompDescription.h"

using namespace ptest;

namespace {

const float kFloats[] = {0.5f, -2.0f, 0.0f, 3.0e38f, 1.0f, -0.0f, 7.25f, 1e-7f, 0.25f, 255.0f, 2.5e-3f, 1.0e-30f, 4.0f, -3.5f};
const char *const kChildWords[] = {"a", "bb", "ccc", "dddd", "nope", "", "a much longer name than fifteen", "x y"};
const char *const kSwitchNames[] = {"loop", "sustain", "other"};
const char *const kSwitchArgNames[] = {"size",  "depth",        "style",     "speed", "damp",
                                       "largeWave", "stopResidual", "dropScale", "delay", "other"};

// ---- shared stand-ins --------------------------------------------------------------------------------------------------

// The parser's group handling (args, name, kind, 0).
void __fastcall Group(void *self, void *, void *args, const char *name, uint32_t kind, uint32_t zero)
{
    Log(84, Where(self), reinterpret_cast<uint32_t>(args), reinterpret_cast<uint32_t>(name), kind);
    ExtraWord(zero);
}
// The face's slot 39 (an RGB colour): the next of gVectors, `out` or elsewhere.
float *__fastcall FaceEvalColor(void *self, void *, float *out, const char *text)
{
    Record(self, 39, Arg(out), reinterpret_cast<uint32_t>(text));
    const float *v = gVectors[gVectorNext++ % 4];
    for (int k = 0; k < 3; ++k) out[k] = v[k];
    return gReturnElsewhere ? gElsewhere : out;
}

// One case's arguments: the positional count, switches and their arguments by name, the face's answers.
void RandomArguments(World &w, std::mt19937 &gen, bool dropSwitches)
{
    const int32_t counts[] = {0, 1, 2, 3, 5, 8, static_cast<int32_t>(0x80000002u)};
    for (auto &s : w.argv) s = kChildWords[gen() % std::size(kChildWords)];
    static const char *switchValues[4];
    for (auto &s : switchValues) s = kChildWords[gen() % std::size(kChildWords)];
    gPositionalCount = counts[gen() % std::size(counts)];
    const uint32_t density = gen() % 4;
    gSwitches.clear();
    for (const char *s : kSwitchNames)
        if (gen() % 4 < density) gSwitches.push_back({s, gen() % 3 == 0 ? 0x100u : 1u});
    gSwitchArgs.clear();
    for (const char *s : kSwitchArgNames) {
        // drop reads -size and -depth without testing them (the original reads through null without them).
        const bool always = dropSwitches && (std::strcmp(s, "size") == 0 || std::strcmp(s, "depth") == 0);
        if (always || gen() % 4 < density) gSwitchArgs.push_back({s, switchValues});
    }
    for (float &f : gFloats) f = kFloats[gen() % std::size(kFloats)];
    for (auto &v : gVectors)
        for (float &f : v) f = kFloats[gen() % std::size(kFloats)];
    for (float &f : gElsewhere) f = kFloats[gen() % std::size(kFloats)];
    gReturnElsewhere = gen() % 3 == 0;
    w.faceVt.ret[33] = w.RandomResult();
    w.faceVt.slots[31] = reinterpret_cast<void *>(&FaceEvalFloat);
    w.faceVt.slots[39] = reinterpret_cast<void *>(&FaceEvalColor);
    w.argsVt.slots[1] = reinterpret_cast<void *>(&ArgsTakeSwitch);
    w.argsVt.slots[3] = reinterpret_cast<void *>(&ArgsTakeSwitchArgs);
}

// A block's world: the object (its parser state: the world's state block, the manager at +0x1b0), its face at +4, its
// name (real, at `nameOffset`); the description's bytes random unless the case builds them.
struct BlockWorld {
    World &w;
    size_t nameOffset;
    FakeVTable managerVt{40}, componentVt{41};
    uint32_t component[0x60 / 4] = {};
    uint32_t managerObject[4] = {};
    const char *name;
    std::function<void()> before, after; // the case's own containers
    BlockWorld(World &world, size_t size, size_t nameAt, void *add) : w(world), nameOffset(nameAt)
    {
        RandomObject(w, size);
        *reinterpret_cast<void **>(gObject + 4) = w.face;
        *reinterpret_cast<void **>(gObject + 0x40) = w.state;
        managerVt.slots[8] = reinterpret_cast<void *>(&ManagerFind);
        managerVt.slots[7] = add;
        componentVt.Set<8>(0);
        for (auto &c : component) c = w.gen();
        component[0] = reinterpret_cast<uint32_t>(componentVt.slots);
        component[3] = 0xC0DE0000u + w.gen() % 100; // the found description's token
        managerObject[0] = reinterpret_cast<uint32_t>(managerVt.slots);
        w.Put(0x1b0, w.gen() % 4 == 0 ? 0 : reinterpret_cast<uint32_t>(managerObject));
        name = kBlockWords[w.gen() % std::size(kBlockWords)];
        std::memset(gObject + nameOffset, 0, 0x18);
        w.before = [this] {
            gToken = 0;
            new (gObject + nameOffset) std::string(name);
            if (before) before();
        };
        w.after = [this] {
            auto *s = reinterpret_cast<std::string *>(gObject + nameOffset);
            ExtraText(s->c_str());
            ExtraWord(static_cast<uint32_t>(s->capacity()));
            s->~basic_string();
            std::memset(gObject + nameOffset, 0, 0x18);
            if (after) after();
        };
    }
};

// The registration's parser slot 4 (and the block children's slot 10 for the group registrations, ParticleAddCommand):
// the block's words that are not heap pointers, its command name, its description's first words, its name; then the
// case's destroy runs, the block base is destroyed and the block freed.
inline void (*gDestroyDescription)(uint8_t *block) = nullptr;
inline size_t gNameOffset = 0;
uint32_t __fastcall RegisterAdd(void *self, void *, void *block)
{
    gLog.push_back({-1, 4, Where(self), {block != nullptr, 0, 0, 0}});
    if (!block) return 0;
    auto *b = static_cast<cp::cBlockCommandBase *>(block);
    auto *bytes = static_cast<uint8_t *>(block);
    const uint32_t size = static_cast<uint32_t>(HeapSize(GetProcessHeap(), 0, block));
    const uint32_t *w = static_cast<const uint32_t *>(block);
    ExtraWord(size);
    ExtraWord(w[0]);
    ExtraWord(w[3]);
    ExtraWord(b->mCommands.size);
    ExtraWord(b->mCommands.bucketCount);
    ExtraWord(w[0x44 / 4]);
    auto *name = reinterpret_cast<std::string *>(bytes + gNameOffset);
    ExtraText(name->c_str());
    ExtraWord(static_cast<uint32_t>(name->capacity()));
    gDestroyDescription(bytes);
    name->~basic_string();
    b->Destruct();
    std::memset(block, 0xee, size);
    ::operator delete(block);
    return 0;
}

template <class Orig, class Ours> void RegisterCases(difftest::Context &ctx, const char *what, Orig orig, Ours ours)
{
    FakeVTable parserVt{-4};
    parserVt.slots[4] = reinterpret_cast<void *>(&RegisterAdd);
    std::mt19937 gen(0x5eed);
    for (int i = 0; i < 4; ++i) {
        World w(gen);
        RandomObject(w, 0x10);
        *reinterpret_cast<void **>(gObject) = parserVt.slots;
        w.before = [&] { gToken = 0; };
        char text[64];
        std::snprintf(text, sizeof(text), "%s case %d", what, i);
        Run(
            ctx, text, With(w, gObject, 0x10), [&] { orig(gObject); }, [&] { ours(gObject); }, [&] { w.After(); },
            [&] { w.Before(); });
    }
}

template <class Orig, class Ours> void GroupRegisterCases(difftest::Context &ctx, const char *what, Orig orig, Ours ours)
{
    FakeVTable blockVt{-3};
    blockVt.slots[10] = reinterpret_cast<void *>(&ParticleAddCommand);
    std::mt19937 gen(0x6eed);
    for (int i = 0; i < 4; ++i) {
        World w(gen);
        RandomObject(w, 0x44);
        *reinterpret_cast<void **>(gObject) = blockVt.slots;
        char text[64];
        std::snprintf(text, sizeof(text), "%s case %d", what, i);
        Run(
            ctx, text, With(w, gObject, 0x44), [&] { orig(gObject); },
            [&] { ours(reinterpret_cast<cp::cBlockCommandBase *>(gObject)); }, [&] { w.After(); }, [&] { w.Before(); });
    }
}

template <class C, class Stands> void SetContextCases(difftest::Context &ctx, uint32_t rva)
{
    auto orig = ctx.Original<SetContextFn>(rva);
    Stands stands;
    FakeVTable blockVt{-3};
    blockVt.slots[10] = reinterpret_cast<void *>(&ParticleAddCommand);
    std::mt19937 gen(rva);
    for (int i = 0; i < 20; ++i) {
        World w(gen);
        RandomObject(w, sizeof(C));
        *reinterpret_cast<void **>(gObject) = blockVt.slots;
        w.parserVt.ret[43] = w.RandomResult();
        void *stateInterface = gen() % 4 == 0 ? nullptr : w.state + 4;
        char what[64];
        std::snprintf(what, sizeof(what), "SetContext %08x case %d", rva, i);
        Run(
            ctx, what, With(w, gObject, sizeof(C)), [&] { orig(gObject, w.parser, stateInterface); },
            [&] { reinterpret_cast<C *>(gObject)->SetContext(w.parser, stateInterface); }, [&] { w.After(); },
            [&] { w.Before(); });
    }
}

template <class C, class Stands> void GroupCases(difftest::Context &ctx, int cases)
{
    auto orig = ctx.Original<ExecuteFn>(C::kExecute);
    Stands stands;
    std::mt19937 gen(C::kExecute);
    for (int i = 0; i < cases; ++i) {
        World w(gen);
        gObjectBase = w.cmd;
        gStateBase = w.state;
        *reinterpret_cast<void **>(w.cmd + 4) = w.face;
        *reinterpret_cast<void **>(w.cmd + 0xc) = w.state;
        RandomArguments(w, gen, false);
        char what[64];
        std::snprintf(what, sizeof(what), "group %08x case %d", C::kExecute, i);
        Run(
            ctx, what, w.Regions(), [&] { orig(w.cmd, w.args); },
            [&] { reinterpret_cast<C *>(w.cmd)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); }, [&] { w.After(); },
            [&] { w.Before(); });
    }
}

// ==== water =================================================================================================================

// The water component's constructor (0xbf66f0): its words a pattern and a token (the block copies the description in).
void *__fastcall WaterComponentConstruct(void *self, void *)
{
    auto *w = static_cast<uint32_t *>(self);
    ++gToken;
    for (int k = 0; k < 0x34 / 4; ++k) w[k] = 0x3A7E0000u + gToken * 0x100 + k;
    Log(90, Where(self), gToken);
    return self;
}
// The manager's slot 7 for a water component: all its words, then it is freed.
uint32_t __fastcall WaterManagerAdd(void *self, void *, const char *name, void *component)
{
    Record(self, 7, 0, component != nullptr);
    ExtraText(name);
    if (component) {
        ExtraBytes(component, 0x34);
        ExtraWord(static_cast<uint32_t>(HeapSize(GetProcessHeap(), 0, component)));
        ::operator delete(component);
    }
    return 0;
}

struct WaterStands {
    ParticleStands particles;
    // 0xbf66f0 is ours (lane IT): the stand-in over the image's and ours alike.
    shape::StandIn component{0xbf66f0u, &cTSCompDescriptionAdapters<cTSWaterComponentDescription>::Construct,
                             &WaterComponentConstruct};
    shape::CodePatch group{0xbcd940u, shape::CodeOf(&Group)};
};

constexpr size_t kDesc = 0x100; // a child's description in the world's state block

template <class C> void WaterChildCases(difftest::Context &ctx, int cases)
{
    auto orig = ctx.Original<ExecuteFn>(C::kExecute);
    WaterStands stands;
    EnumTableFill table(ctx, 0x10fd284, 0x20);
    std::mt19937 gen(C::kExecute);
    for (int i = 0; i < cases; ++i) {
        World w(gen);
        gObjectBase = w.cmd;
        gStateBase = w.state;
        *reinterpret_cast<void **>(w.cmd + 4) = w.face;
        *reinterpret_cast<void **>(w.cmd + 0xc) = w.state + kDesc;
        RandomArguments(w, gen, C::kExecute == cWaterDropCommand::kExecute);
        for (size_t off = 8; off < 0x28; off += 4)
            if (gen() % 4 != 0) w.Put(kDesc + off, Bits(kFloats[gen() % std::size(kFloats)]));
        w.before = [&] { gFloatNext = 0; };
        char what[64];
        std::snprintf(what, sizeof(what), "%08x case %d", C::kExecute, i);
        Run(
            ctx, what, w.Regions(), [&] { orig(w.cmd, w.args); },
            [&] { reinterpret_cast<C *>(w.cmd)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); }, [&] { w.After(); },
            [&] { w.Before(); });
    }
}

} // namespace

DIFF_TEST(WaterCommand_Drop, 0xbf6890) { WaterChildCases<cWaterDropCommand>(ctx, 60); }
DIFF_TEST(WaterCommand_Surface, 0xbf6ab0) { WaterChildCases<cWaterSurfaceCommand>(ctx, 300); }
DIFF_TEST(WaterGroup_Water, 0xbf6860) { GroupCases<cGroupWaterCommand, WaterStands>(ctx, 30); }
DIFF_TEST(WaterGroup_Register, 0xbf70d0)
{
    WaterStands stands;
    GroupRegisterCases(ctx, "water group", ctx.Original<void(__cdecl *)(void *)>(kRegisterWaterGroupCommand),
                       &RegisterWaterGroupCommand);
}
DIFF_TEST(WaterEffect_Register, 0xbf6ff0)
{
    WaterStands stands;
    gNameOffset = 0x70;
    gDestroyDescription = [](uint8_t *block) { ExtraBytes(block + 0x48, 0x28); };
    RegisterCases(ctx, "water block", ctx.Original<void(__cdecl *)(void *)>(kRegisterWaterEffectBlock), &RegisterWaterEffectBlock);
}
DIFF_TEST(WaterEffect_SetContext, 0xbf6cf0) { SetContextCases<cWaterEffectCommand, WaterStands>(ctx, 0xbf6cf0); }

DIFF_TEST(WaterEffect_Execute, 0xbf6900)
{
    auto orig = ctx.Original<ExecuteFn>(0xbf6900);
    WaterStands stands;
    std::mt19937 gen(0xbf6900);
    const int32_t counts[] = {1, 2, 3, 3, 3, 0, static_cast<int32_t>(0x80000003u)};
    for (int i = 0; i < 200; ++i) {
        World w(gen);
        BlockWorld e(w, sizeof(cWaterEffectCommand), 0x70, reinterpret_cast<void *>(&WaterManagerAdd));
        e.before = [&] {
            // The block's face (+4) another face than the state's: the block is opened through +4 (0xbf6900).
            *reinterpret_cast<const void **>(gObject + 0x200) = w.faceVt.slots;
            *reinterpret_cast<void **>(gObject + 4) = gObject + 0x200;
        };
        gPositionalCount = counts[gen() % std::size(counts)];
        for (auto &s : w.argv) s = kBlockWords[gen() % std::size(kBlockWords)];
        if (gen() % 2) w.argv[1] = ":";
        gFindResult = gen() % 4 == 0 ? 0 : (gen() % 3 == 0 ? 0x100 : 1);
        gFound = gFindResult & 0xff ? e.component : (gen() % 2 ? e.component : nullptr);
        char what[64];
        std::snprintf(what, sizeof(what), "Execute case %d", i);
        Run(
            ctx, what, With(w, gObject, sizeof(cWaterEffectCommand)), [&] { orig(gObject, w.args); },
            [&] { reinterpret_cast<cWaterEffectCommand *>(gObject)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); },
            [&] { w.After(); }, [&] { w.Before(); });
    }
}

DIFF_TEST(WaterEffect_EndBlock, 0xbf6c50)
{
    auto orig = ctx.Original<VoidFn>(0xbf6c50);
    WaterStands stands;
    std::mt19937 gen(0xbf6c50);
    for (int i = 0; i < 60; ++i) {
        World w(gen);
        BlockWorld e(w, sizeof(cWaterEffectCommand), 0x70, reinterpret_cast<void *>(&WaterManagerAdd));
        char what[64];
        std::snprintf(what, sizeof(what), "EndBlock case %d", i);
        Run(
            ctx, what, With(w, gObject, sizeof(cWaterEffectCommand)), [&] { orig(gObject); },
            [&] { reinterpret_cast<cWaterEffectCommand *>(gObject)->EndBlock(); }, [&] { w.After(); }, [&] { w.Before(); });
    }
}

DIFF_TEST(WaterEffect_Destructor, 0xbf67a0)
{
    DestructorCases<WaterStands>(
        ctx, 0xbf67a0, sizeof(cWaterEffectCommand), 0x70,
        [](void *p, uint32_t f) -> void * { return reinterpret_cast<cWaterEffectCommand *>(p)->DeletingDestructor(f); }, 40);
}

// ==== screen ================================================================================================================

namespace {

constexpr uint32_t kScreenToken = 0x5C2E0000u;
ScreenDescription &ScreenAt(void *p) { return *static_cast<ScreenDescription *>(p); }

// The screen description's constructor (0xbf7280): a token, a pattern, real containers whose contents follow the token
// (some colours and strength values, a texture name; long ones on the heap).
void *__fastcall ScreenConstruct(void *self, void *)
{
    ++gToken;
    auto *b = static_cast<uint8_t *>(self);
    std::memset(b, 0x40 + gToken, sizeof(ScreenDescription));
    ScreenDescription &d = ScreenAt(self);
    d.mTypeId = kScreenToken + gToken;
    new (&d.mColors) std::vector<Vec3>();
    for (uint32_t k = 0; k < gToken % 3; ++k) d.mColors.push_back({k + 0.5f, -1.0f * k, 2.0f});
    new (&d.mStrength.mValues) std::vector<float>();
    for (uint32_t k = 0; k < (gToken + 1) % 3; ++k) d.mStrength.mValues.push_back(k * 3.0f);
    new (&d.mTexture) std::string(kTexts[gToken % std::size(kTexts)]);
    Log(91, Where(self), gToken);
    return self;
}
// Destructor (0xbf7320): logs what is left in a description it built (moved-from containers are empty), then frees it.
void __fastcall ScreenDestroy(void *self, void *)
{
    const uint32_t token = *static_cast<uint32_t *>(self);
    Log(92, Where(self), token);
    if ((token & 0xffff0000u) != kScreenToken) return;
    ScreenDescription &d = ScreenAt(self);
    ExtraWord(static_cast<uint32_t>(d.mColors.size()));
    ExtraWord(static_cast<uint32_t>(d.mStrength.mValues.size()));
    ExtraText(d.mTexture.c_str());
    d.mColors.~vector();
    d.mStrength.mValues.~vector();
    d.mTexture.~basic_string();
}
void *__fastcall ScreenCopy(void *self, void *, const void *from)
{
    Log(93, Where(self), Where(from), *static_cast<const uint32_t *>(from));
    return self;
}
// The manager's slot 7 for a screen component: its words and the description's token, then it is freed (its containers
// by the destructor's stand-in).
uint32_t __fastcall ScreenManagerAdd(void *self, void *, const char *name, void *component)
{
    Record(self, 7, 0, component != nullptr);
    ExtraText(name);
    if (component) {
        const uint32_t *w = static_cast<const uint32_t *>(component);
        for (int k = 0; k < 4; ++k) ExtraWord(w[k]);
        ExtraWord(static_cast<uint32_t>(HeapSize(GetProcessHeap(), 0, component)));
        ScreenDestroy(static_cast<uint8_t *>(component) + 0xc, nullptr);
        ::operator delete(component);
    }
    return 0;
}

struct ScreenStands {
    ParticleStands particles; // the curve parse (0xbd1a50) among them
    // The description's functions are ours (lane IW): the stand-ins over the image's and ours alike.
    shape::StandIn construct{0xbf7280u, &cTSCompDescriptionAdapters<cTSScreenComponentDescription>::ConstructDescription,
                             &ScreenConstruct};
    shape::StandIn destroy{0xbf7320u, &cTSCompDescriptionAdapters<cTSScreenComponentDescription>::DestroyDescription,
                           &ScreenDestroy};
    shape::StandIn copy{0xbf73c0u, &cTSCompDescriptionAdapters<cTSScreenComponentDescription>::CopyDescription, &ScreenCopy};
    shape::CodePatch group{0xbcd940u, shape::CodeOf(&Group)};
};

// A screen description's containers at `at` (a block's or the world's): built before each side's run from drawn
// contents, logged (sizes, capacities, contents) and freed after.
struct ScreenContainers {
    uint8_t *at = nullptr;
    size_t colors = 0, colorCapacity = 0, values = 0;
    const char *texture = "";
    void Draw(std::mt19937 &gen, uint8_t *where)
    {
        at = where;
        colors = gen() % 4;
        colorCapacity = colors + gen() % 3;
        values = gen() % 3;
        texture = kTexts[gen() % std::size(kTexts)];
        Clear();
    }
    void Clear()
    {
        std::memset(at + 0xc, 0, 0xc);
        std::memset(at + 0x1c, 0, 0xc);
        std::memset(at + 0x30, 0, 0x18);
    }
    void Before()
    {
        ScreenDescription &d = ScreenAt(at);
        new (&d.mColors) std::vector<Vec3>();
        d.mColors.reserve(colorCapacity);
        for (size_t k = 0; k < colors; ++k) d.mColors.push_back({k * 2.0f, 0.25f, -0.0f});
        new (&d.mStrength.mValues) std::vector<float>();
        for (size_t k = 0; k < values; ++k) d.mStrength.mValues.push_back(k + 0.125f);
        new (&d.mTexture) std::string(texture);
    }
    void After()
    {
        ScreenDescription &d = ScreenAt(at);
        ExtraWord(static_cast<uint32_t>(d.mColors.size()));
        ExtraWord(static_cast<uint32_t>(d.mColors.capacity()));
        if (!d.mColors.empty()) ExtraBytes(d.mColors.data(), d.mColors.size() * sizeof(Vec3));
        ExtraWord(static_cast<uint32_t>(d.mStrength.mValues.size()));
        if (!d.mStrength.mValues.empty()) ExtraBytes(d.mStrength.mValues.data(), d.mStrength.mValues.size() * 4);
        ExtraText(d.mTexture.c_str());
        ExtraWord(static_cast<uint32_t>(d.mTexture.capacity()));
        d.mColors.~vector();
        d.mStrength.mValues.~vector();
        d.mTexture.~basic_string();
        Clear();
    }
};

const char *const kCommandNames[] = {"color", "color255", "colour255", "colour", "x255", "255", "2555", "255 "};

template <class C> void ScreenChildCases(difftest::Context &ctx, int cases)
{
    auto orig = ctx.Original<ExecuteFn>(C::kExecute);
    ScreenStands stands;
    EnumTableFill table(ctx, 0x10fd2a0, 0x20);
    std::mt19937 gen(C::kExecute);
    for (int i = 0; i < cases; ++i) {
        World w(gen);
        ScreenContainers containers;
        containers.Draw(gen, w.state + kDesc);
        gObjectBase = w.cmd;
        gStateBase = w.state;
        *reinterpret_cast<void **>(w.cmd + 4) = w.face;
        *reinterpret_cast<void **>(w.cmd + 0xc) = w.state + kDesc;
        RandomArguments(w, gen, false);
        w.argsVt.ret[4] = reinterpret_cast<uint32_t>(kCommandNames[gen() % std::size(kCommandNames)]);
        // The colours read one argument per count: at most the eight the world has.
        if (C::kExecute == cScreenColorCommand::kExecute && (gPositionalCount < 0 || gPositionalCount > 8)) gPositionalCount = 8;
        for (size_t off : {0x4u, 0x8u, 0x18u, 0x28u, 0x2cu})
            if (gen() % 4 != 0) w.Put(kDesc + off, Bits(kFloats[gen() % std::size(kFloats)]));
        w.before = [&] {
            gFloatNext = 0;
            gVectorNext = 0;
            containers.Before();
        };
        w.after = [&] { containers.After(); };
        char what[64];
        std::snprintf(what, sizeof(what), "%08x case %d", C::kExecute, i);
        Run(
            ctx, what, w.Regions(), [&] { orig(w.cmd, w.args); },
            [&] { reinterpret_cast<C *>(w.cmd)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); }, [&] { w.After(); },
            [&] { w.Before(); });
    }
}

} // namespace

DIFF_TEST(ScreenCommand_Color, 0xbf7580) { ScreenChildCases<cScreenColorCommand>(ctx, 200); }
DIFF_TEST(ScreenCommand_Length, 0xbf7ac0) { ScreenChildCases<cScreenLengthCommand>(ctx, 150); }
DIFF_TEST(ScreenCommand_Values, 0xbf7b60)
{
    ScreenChildCases<cScreenModeCommand>(ctx, 60);     // 0xbf7b60
    ScreenChildCases<cScreenStrengthCommand>(ctx, 20); // 0xbf7b90
    ScreenChildCases<cScreenTextureCommand>(ctx, 40);  // 0xbf7bb0
}
DIFF_TEST(ScreenGroup_Screen, 0xbf7550) { GroupCases<cGroupScreenCommand, ScreenStands>(ctx, 30); }
DIFF_TEST(ScreenGroup_Register, 0xbf8480)
{
    ScreenStands stands;
    GroupRegisterCases(ctx, "screen group", ctx.Original<void(__cdecl *)(void *)>(kRegisterScreenGroupCommand),
                       &RegisterScreenGroupCommand);
}
DIFF_TEST(ScreenEffect_Register, 0xbf83e0)
{
    ScreenStands stands;
    gNameOffset = 0x90;
    gDestroyDescription = [](uint8_t *block) { ScreenDestroy(block + 0x48, nullptr); };
    RegisterCases(ctx, "screen block", ctx.Original<void(__cdecl *)(void *)>(kRegisterScreenEffectBlock), &RegisterScreenEffectBlock);
}
DIFF_TEST(ScreenEffect_SetContext, 0xbf7d40) { SetContextCases<cScreenEffectCommand, ScreenStands>(ctx, 0xbf7d40); }

DIFF_TEST(ScreenEffect_Execute, 0xbf7830)
{
    auto orig = ctx.Original<ExecuteFn>(0xbf7830);
    ScreenStands stands;
    std::mt19937 gen(0xbf7830);
    const int32_t counts[] = {1, 2, 3, 3, 3, 0, static_cast<int32_t>(0x80000003u)};
    const char *const separators[] = {":", ":", ":", ";", "::", ""};
    for (int i = 0; i < 200; ++i) {
        World w(gen);
        BlockWorld e(w, sizeof(cScreenEffectCommand), 0x90, reinterpret_cast<void *>(&ScreenManagerAdd));
        ScreenContainers containers;
        containers.Draw(gen, gObject + 0x48);
        e.before = [&] {
            containers.Before();
            // The block's face (+4) another face than the state's: the block is opened through the state's (0xbf7830
            // reads it through the state's vbtable, where the camera and water blocks use +4).
            *reinterpret_cast<const void **>(gObject + 0x200) = w.faceVt.slots;
            *reinterpret_cast<void **>(gObject + 4) = gObject + 0x200;
        };
        e.after = [&] { containers.After(); };
        gPositionalCount = counts[gen() % std::size(counts)];
        for (auto &s : w.argv) s = kBlockWords[gen() % std::size(kBlockWords)];
        w.argv[1] = separators[gen() % std::size(separators)];
        gFindResult = gen() % 4 == 0 ? 0 : (gen() % 3 == 0 ? 0x100 : 1);
        gFound = gFindResult & 0xff ? e.component : (gen() % 2 ? e.component : nullptr);
        char what[64];
        std::snprintf(what, sizeof(what), "Execute case %d", i);
        Run(
            ctx, what, With(w, gObject, sizeof(cScreenEffectCommand)), [&] { orig(gObject, w.args); },
            [&] { reinterpret_cast<cScreenEffectCommand *>(gObject)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); },
            [&] { w.After(); }, [&] { w.Before(); });
    }
}

DIFF_TEST(ScreenEffect_EndBlock, 0xbf7c10)
{
    auto orig = ctx.Original<VoidFn>(0xbf7c10);
    ScreenStands stands;
    std::mt19937 gen(0xbf7c10);
    for (int i = 0; i < 80; ++i) {
        World w(gen);
        BlockWorld e(w, sizeof(cScreenEffectCommand), 0x90, reinterpret_cast<void *>(&ScreenManagerAdd));
        ScreenContainers containers;
        containers.Draw(gen, gObject + 0x48);
        e.before = [&] { containers.Before(); };
        e.after = [&] { containers.After(); };
        char what[64];
        std::snprintf(what, sizeof(what), "EndBlock case %d", i);
        Run(
            ctx, what, With(w, gObject, sizeof(cScreenEffectCommand)), [&] { orig(gObject); },
            [&] { reinterpret_cast<cScreenEffectCommand *>(gObject)->EndBlock(); }, [&] { w.After(); }, [&] { w.Before(); });
    }
}

DIFF_TEST(ScreenEffect_Destructor, 0xbf7490)
{
    DestructorCases<ScreenStands>(
        ctx, 0xbf7490, sizeof(cScreenEffectCommand), 0x90,
        [](void *p, uint32_t f) -> void * { return reinterpret_cast<cScreenEffectCommand *>(p)->DeletingDestructor(f); }, 40);
}
