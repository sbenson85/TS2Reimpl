// Differential tests of the camera and shake blocks (`camera`, `shake`), their commands, the group commands
// (`cameraEffect`, `shakeEffect`) and the two registration functions (src/gz/EffectCommands.cpp) against the originals.
// The world is tests/gz_particle_commands.h's; the camera description's functions (not ours) are patched with stand-ins
// that log (the constructor writes a token and a real, empty select string; the destructor frees that string), the
// curve parse is the particle tests' stand-in, the parser's group functions log.
#include "gz_particle_commands.h"

#include "gz/EffectCommands.h"
#include "ts/cTSCompDescription.h"

using namespace ptest;

namespace {

using CamAdapters = cTSCompDescriptionAdapters<cTSCameraComponentDescription>;

// ---- the camera description's functions (constructor 0xbf4610, destructor 0xbf4720, move 0xbf4a50, copy 0xbf4d20) ----
constexpr uint32_t kTokenBase = 0xCA3E0000u;
constexpr size_t kSelect = 0xac; // the description's select string
std::string &SelectOf(void *d) { return *reinterpret_cast<std::string *>(static_cast<uint8_t *>(d) + kSelect); }

void *__fastcall CamConstruct(void *self, void *)
{
    ++gToken;
    auto *b = static_cast<uint8_t *>(self);
    for (size_t i = 4; i < 0xc8; ++i) b[i] = static_cast<uint8_t>(0x31 + i + gToken);
    *static_cast<uint32_t *>(self) = kTokenBase + gToken;
    new (b + kSelect) std::string();
    Log(80, Where(self), gToken);
    return self;
}
// A description the stand-in built (its token) has a real string to free; random bytes (a destructor's case) do not.
void __fastcall CamDestroy(void *self, void *)
{
    const uint32_t token = *static_cast<uint32_t *>(self);
    Log(81, Where(self), token);
    if ((token & 0xffff0000u) == kTokenBase) {
        ExtraText(SelectOf(self).c_str());
        SelectOf(self).~basic_string();
    }
}
void *__fastcall CamMove(void *self, void *, void *from)
{
    Log(82, Where(self), Where(from), *static_cast<const uint32_t *>(from));
    return self;
}
void *__fastcall CamCopy(void *self, void *, const void *from)
{
    Log(83, Where(self), Where(from), *static_cast<const uint32_t *>(from));
    return self;
}
// The parser's group handling (args, name, kind, 0) and its anonymous-description handling (args, component, 1).
void __fastcall Group(void *self, void *, void *args, const char *name, uint32_t kind, uint32_t zero)
{
    Log(84, Where(self), reinterpret_cast<uint32_t>(args), reinterpret_cast<uint32_t>(name), kind);
    ExtraWord(zero);
}
void __fastcall GroupDescription(void *self, void *, void *args, void *component, uint32_t one)
{
    Log(85, Where(self), reinterpret_cast<uint32_t>(args), component != nullptr, one);
    if (!component) return;
    const auto *w = static_cast<const uint32_t *>(component);
    ExtraWord(w[0]);
    ExtraWord(w[1]);
    ExtraWord(w[2]); // the count (1: the command's own reference)
    auto *d = static_cast<uint8_t *>(component) + 0xc;
    ExtraBytes(d, kSelect);
    ExtraBytes(d + kSelect + 0x18, 4);
    ExtraText(SelectOf(d).c_str());
}
// The manager's slot 7 for a camera component: its words and the description's token, then it is freed (its
// description's string by the destructor's stand-in).
uint32_t __fastcall CamManagerAdd(void *self, void *, const char *name, void *component)
{
    Record(self, 7, 0, component != nullptr);
    ExtraText(name);
    if (component) {
        const uint32_t *w = static_cast<const uint32_t *>(component);
        for (int k = 0; k < 4; ++k) ExtraWord(w[k]);
        ExtraWord(static_cast<uint32_t>(HeapSize(GetProcessHeap(), 0, component)));
        CamDestroy(static_cast<uint8_t *>(component) + 0xc, nullptr);
        ::operator delete(component);
    }
    return 0;
}

struct CameraStands {
    ParticleStands particles; // the curve parse (0xbd1a50) among them
    // The description's functions are ours (lane IW): the stand-ins over the image's and ours alike.
    shape::StandIn construct{0xbf4610u, &CamAdapters::ConstructDescription, &CamConstruct};
    shape::StandIn destroy{0xbf4720u, &CamAdapters::DestroyDescription, &CamDestroy};
    shape::StandIn move{0xbf4a50u, &CamAdapters::MoveDescription, &CamMove};
    shape::StandIn copy{0xbf4d20u, &CamAdapters::CopyDescription, &CamCopy};
    shape::CodePatch group{0xbcd940u, shape::CodeOf(&Group)};
    shape::CodePatch groupDescription{0xbcd810u, shape::CodeOf(&GroupDescription)};
};

const float kFloats[] = {0.5f, -2.0f, 0.0f, 3.0e38f, 1.0f, -0.0f, 7.25f, 1e-7f, 0.25f, 255.0f, 2.5e-3f, 1.0e-30f, 4.0f, -3.5f};
const char *const kChildWords[] = {"a", "bb", "ccc", "dddd", "nope", "", "a much longer name than fifteen", "x y"};
const char *const kSwitchNames[] = {"slave", "suspendController", "noRestoreController", "moveCamera", "orientCamera",
                                    "orientCameraZPole", "moveController", "orientController", "snapshot", "postSnapshot",
                                    "noStop", "other"};
const char *const kSwitchArgNames[] = {"attachRadius", "zoom", "rotate", "select", "fade", "other"};

// ---- the commands ------------------------------------------------------------------------------------------------------

constexpr size_t kCam = 0x100; // the description in the world's state block

// The description's select string: built (random text) before each side's run, logged and freed after.
struct SelectString {
    World &w;
    const char *text;
    uint8_t *At() { return w.state + kCam + kSelect; }
    explicit SelectString(World &world) : w(world)
    {
        text = kTexts[w.gen() % std::size(kTexts)];
        std::memset(At(), 0, 0x18);
    }
    void Before() { new (At()) std::string(text); }
    void After()
    {
        auto *s = reinterpret_cast<std::string *>(At());
        ExtraText(s->c_str());
        s->~basic_string();
        std::memset(At(), 0, 0x18);
    }
};

// One case's arguments: the positional count, switches and their arguments by name, the face's answers.
void RandomArguments(World &w, std::mt19937 &gen)
{
    const int32_t counts[] = {0, 1, 2, 3, 5, static_cast<int32_t>(0x80000002u)};
    for (auto &s : w.argv) s = kChildWords[gen() % std::size(kChildWords)];
    static const char *switchValues[4];
    for (auto &s : switchValues) s = kChildWords[gen() % std::size(kChildWords)];
    gPositionalCount = counts[gen() % std::size(counts)];
    const uint32_t density = gen() % 4;
    gSwitches.clear();
    for (const char *s : kSwitchNames)
        if (gen() % 4 < density) gSwitches.push_back({s, gen() % 3 == 0 ? 0x100u : 1u});
    gSwitchArgs.clear();
    for (const char *s : kSwitchArgNames)
        if (gen() % 4 < density) gSwitchArgs.push_back({s, switchValues});
    for (float &f : gFloats) f = kFloats[gen() % std::size(kFloats)];
    w.faceVt.ret[32] = w.RandomResult();
    w.faceVt.slots[31] = reinterpret_cast<void *>(&FaceEvalFloat);
    w.argsVt.slots[1] = reinterpret_cast<void *>(&ArgsTakeSwitch);
    w.argsVt.slots[3] = reinterpret_cast<void *>(&ArgsTakeSwitchArgs);
}

template <class C> void CameraChildCases(difftest::Context &ctx, int cases)
{
    auto orig = ctx.Original<ExecuteFn>(C::kExecute);
    CameraStands stands;
    EnumTableFill table(ctx, 0x10fd268, 0x20);
    std::mt19937 gen(C::kExecute);
    for (int i = 0; i < cases; ++i) {
        World w(gen);
        SelectString select(w);
        gObjectBase = w.cmd;
        gStateBase = w.state;
        *reinterpret_cast<void **>(w.cmd + 4) = w.face;
        *reinterpret_cast<void **>(w.cmd + 0xc) = w.state + kCam;
        RandomArguments(w, gen);
        // The description's plain values: often from the float table (negative zeros included), the flags random.
        for (size_t off = 8; off < kSelect; off += 4)
            if (gen() % 4 != 0) w.Put(kCam + off, Bits(kFloats[gen() % std::size(kFloats)]));
        w.Put(kCam + 4, gen());
        w.Put(kCam + 0xc4, Bits(kFloats[gen() % std::size(kFloats)]));
        w.before = [&] {
            gFloatNext = 0;
            gToken = 0;
            select.Before();
        };
        w.after = [&] { select.After(); };
        char what[64];
        std::snprintf(what, sizeof(what), "%08x case %d", C::kExecute, i);
        Run(
            ctx, what, w.Regions(), [&] { orig(w.cmd, w.args); },
            [&] { reinterpret_cast<C *>(w.cmd)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); }, [&] { w.After(); },
            [&] { w.Before(); });
    }
}

} // namespace

DIFF_TEST(CameraCommand_Control, 0xbf4fe0) { CameraChildCases<cCameraControlCommand>(ctx, 400); } // and 0xbf5f20
DIFF_TEST(CameraCommand_Life, 0xbf5230) { CameraChildCases<cCameraLifeCommand>(ctx, 40); }
DIFF_TEST(CameraCommand_Curves, 0xbf5200)
{
    CameraChildCases<cCameraHeadingCommand>(ctx, 20);  // 0xbf5200
    CameraChildCases<cCameraPitchCommand>(ctx, 20);    // 0xbf52c0
    CameraChildCases<cCameraRollCommand>(ctx, 20);     // 0xbf52f0
    CameraChildCases<cCameraFOVCommand>(ctx, 20);      // 0xbf51a0
    CameraChildCases<cCameraNearClipCommand>(ctx, 20); // 0xbf5260
    CameraChildCases<cCameraFarClipCommand>(ctx, 20);  // 0xbf51d0
    CameraChildCases<cCameraOrbitCommand>(ctx, 20);    // 0xbf5290
}
DIFF_TEST(ShakeCommand_Length, 0xbf5680) { CameraChildCases<cShakeLengthCommand>(ctx, 200); }
DIFF_TEST(ShakeCommand_Values, 0xbf5460)
{
    CameraChildCases<cShakeAmplitudeCommand>(ctx, 20); // 0xbf5460
    CameraChildCases<cShakeFrequencyCommand>(ctx, 20); // 0xbf5660
    CameraChildCases<cShakeAspectCommand>(ctx, 40);    // 0xbf5480
    CameraChildCases<cShakeBaseTableCommand>(ctx, 80); // 0xbf54b0
}

// ---- the group commands ------------------------------------------------------------------------------------------------

template <class C> void GroupCases(difftest::Context &ctx, int cases)
{
    auto orig = ctx.Original<ExecuteFn>(C::kExecute);
    CameraStands stands;
    std::mt19937 gen(C::kExecute);
    for (int i = 0; i < cases; ++i) {
        World w(gen);
        gObjectBase = w.cmd;
        gStateBase = w.state;
        *reinterpret_cast<void **>(w.cmd + 4) = w.face;
        *reinterpret_cast<void **>(w.cmd + 0xc) = w.state;
        RandomArguments(w, gen);
        w.before = [&] {
            gFloatNext = 0;
            gToken = 0;
        };
        char what[64];
        std::snprintf(what, sizeof(what), "group %08x case %d", C::kExecute, i);
        Run(
            ctx, what, w.Regions(), [&] { orig(w.cmd, w.args); },
            [&] { reinterpret_cast<C *>(w.cmd)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); }, [&] { w.After(); },
            [&] { w.Before(); });
    }
}

DIFF_TEST(CameraGroup_Shake, 0xbf5430) { GroupCases<cGroupShakeCommand>(ctx, 30); }
DIFF_TEST(CameraGroup_Camera, 0xbf5320) { GroupCases<cGroupCameraCommand>(ctx, 300); } // and 0xbf5f20

DIFF_TEST(CameraGroup_Register, 0xbf6420)
{
    auto orig = ctx.Original<void(__cdecl *)(void *)>(kRegisterCameraGroupCommands);
    CameraStands stands;
    FakeVTable blockVt{-3};
    blockVt.slots[10] = reinterpret_cast<void *>(&ParticleAddCommand);
    std::mt19937 gen(0xbf6420);
    for (int i = 0; i < 5; ++i) {
        World w(gen);
        RandomObject(w, 0x44);
        *reinterpret_cast<void **>(gObject) = blockVt.slots;
        char what[64];
        std::snprintf(what, sizeof(what), "register groups case %d", i);
        Run(
            ctx, what, With(w, gObject, 0x44), [&] { orig(gObject); },
            [&] { RegisterCameraGroupCommands(reinterpret_cast<cp::cBlockCommandBase *>(gObject)); }, [&] { w.After(); },
            [&] { w.Before(); });
    }
}

// ---- the blocks ----------------------------------------------------------------------------------------------------------

namespace {
// The registration's parser slot 4: the block's words (not heap pointers), its description's token and its name, then
// it is destroyed (the description's string, the name, the block base) and freed.
uint32_t __fastcall RegisterAdd(void *self, void *, void *block)
{
    gLog.push_back({-1, 4, Where(self), {block != nullptr, 0, 0, 0}});
    if (!block) return 0;
    auto *b = static_cast<cCameraEffectBlock *>(block);
    const uint32_t size = static_cast<uint32_t>(HeapSize(GetProcessHeap(), 0, block));
    const uint32_t *w = static_cast<const uint32_t *>(block);
    ExtraWord(size);
    ExtraWord(w[0]);
    ExtraWord(w[3]);
    ExtraWord(b->mCommands.size);
    ExtraWord(b->mCommands.bucketCount);
    ExtraWord(reinterpret_cast<uint32_t>(b->mCommandName));
    ExtraWord(w[0x48 / 4]);
    ExtraText(b->mName.c_str());
    ExtraWord(static_cast<uint32_t>(b->mName.capacity()));
    CamDestroy(&b->mDescription, nullptr);
    b->mName.~basic_string();
    b->Destruct();
    std::memset(block, 0xee, size);
    ::operator delete(block);
    return 0;
}
} // namespace

DIFF_TEST(CameraEffect_Register, 0xbf6300)
{
    auto orig = ctx.Original<void(__cdecl *)(void *)>(kRegisterCameraEffectBlocks);
    CameraStands stands;
    FakeVTable parserVt{-4};
    parserVt.slots[4] = reinterpret_cast<void *>(&RegisterAdd);
    std::mt19937 gen(0xbf6300);
    for (int i = 0; i < 5; ++i) {
        World w(gen);
        RandomObject(w, 0x10);
        *reinterpret_cast<void **>(gObject) = parserVt.slots;
        w.before = [&] { gToken = 0; };
        char what[64];
        std::snprintf(what, sizeof(what), "register blocks case %d", i);
        Run(
            ctx, what, With(w, gObject, 0x10), [&] { orig(gObject); }, [&] { RegisterCameraEffectBlocks(gObject); },
            [&] { w.After(); }, [&] { w.Before(); });
    }
}

template <class C> void SetContextCases(difftest::Context &ctx, uint32_t rva)
{
    auto orig = ctx.Original<SetContextFn>(rva);
    CameraStands stands;
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

DIFF_TEST(CameraEffect_SetContext, 0xbf57e0) { SetContextCases<cCameraEffectCommand>(ctx, 0xbf57e0); }
DIFF_TEST(ShakeEffect_SetContext, 0xbf5c70) { SetContextCases<cShakeEffectCommand>(ctx, 0xbf5c70); }

namespace {
// A block's world: the object (its parser state: the world's state block, the manager at +0x1b0), its face at +4, its
// name (+0x110, real); the description's bytes are random (only the stand-ins see them).
struct BlockWorld {
    World &w;
    FakeVTable managerVt{40}, componentVt{41};
    uint32_t component[0xd4 / 4] = {};
    uint32_t managerObject[4] = {};
    const char *name;
    explicit BlockWorld(World &world) : w(world)
    {
        RandomObject(w, sizeof(cCameraEffectBlock));
        *reinterpret_cast<void **>(gObject + 4) = w.face;
        *reinterpret_cast<void **>(gObject + 0x40) = w.state;
        managerVt.slots[8] = reinterpret_cast<void *>(&ManagerFind);
        managerVt.slots[7] = reinterpret_cast<void *>(&CamManagerAdd);
        componentVt.Set<8>(0);
        component[0] = reinterpret_cast<uint32_t>(componentVt.slots);
        component[3] = 0xC0DE0000u + w.gen() % 100; // the found description's token
        managerObject[0] = reinterpret_cast<uint32_t>(managerVt.slots);
        w.Put(0x1b0, w.gen() % 4 == 0 ? 0 : reinterpret_cast<uint32_t>(managerObject));
        name = kBlockWords[w.gen() % std::size(kBlockWords)];
        std::memset(gObject + 0x110, 0, 0x18);
        w.before = [this] {
            gToken = 0;
            new (gObject + 0x110) std::string(name);
        };
        w.after = [this] {
            auto *s = reinterpret_cast<std::string *>(gObject + 0x110);
            ExtraText(s->c_str());
            s->~basic_string();
            std::memset(gObject + 0x110, 0, 0x18);
        };
    }
};

template <class C> void ExecuteCases(difftest::Context &ctx, uint32_t rva)
{
    auto orig = ctx.Original<ExecuteFn>(rva);
    CameraStands stands;
    std::mt19937 gen(rva);
    const int32_t counts[] = {1, 2, 3, 3, 3, 0, static_cast<int32_t>(0x80000003u)};
    for (int i = 0; i < 200; ++i) {
        World w(gen);
        BlockWorld e(w);
        // The block's face (+4) another face than the state's: the block is opened through +4.
        w.before = [&, before = w.before] {
            before();
            *reinterpret_cast<const void **>(gObject + 0x200) = w.faceVt.slots;
            *reinterpret_cast<void **>(gObject + 4) = gObject + 0x200;
        };
        gPositionalCount = counts[gen() % std::size(counts)];
        for (auto &s : w.argv) s = kBlockWords[gen() % std::size(kBlockWords)];
        if (gen() % 2) w.argv[1] = ":";
        gFindResult = gen() % 4 == 0 ? 0 : (gen() % 3 == 0 ? 0x100 : 1);
        gFound = gFindResult & 0xff ? e.component : (gen() % 2 ? e.component : nullptr);
        char what[64];
        std::snprintf(what, sizeof(what), "Execute %08x case %d", rva, i);
        Run(
            ctx, what, With(w, gObject, sizeof(cCameraEffectBlock)), [&] { orig(gObject, w.args); },
            [&] { reinterpret_cast<C *>(gObject)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); }, [&] { w.After(); },
            [&] { w.Before(); });
    }
}
} // namespace

DIFF_TEST(CameraEffect_Execute, 0xbf5020) { ExecuteCases<cCameraEffectCommand>(ctx, 0xbf5020); }
DIFF_TEST(ShakeEffect_Execute, 0xbf54e0) { ExecuteCases<cShakeEffectCommand>(ctx, 0xbf54e0); }

DIFF_TEST(CameraEffect_EndBlock, 0xbf5720)
{
    auto orig = ctx.Original<VoidFn>(0xbf5720);
    CameraStands stands;
    std::mt19937 gen(0xbf5720);
    for (int i = 0; i < 60; ++i) {
        World w(gen);
        BlockWorld e(w);
        char what[64];
        std::snprintf(what, sizeof(what), "EndBlock case %d", i);
        Run(
            ctx, what, With(w, gObject, sizeof(cCameraEffectBlock)), [&] { orig(gObject); },
            [&] { reinterpret_cast<cCameraEffectBlock *>(gObject)->EndBlock(); }, [&] { w.After(); }, [&] { w.Before(); });
    }
}

DIFF_TEST(CameraEffect_Destructor, 0xbf4f20)
{
    DestructorCases<CameraStands>(
        ctx, 0xbf4f20, sizeof(cCameraEffectBlock), 0x110,
        [](void *p, uint32_t f) -> void * { return reinterpret_cast<cCameraEffectBlock *>(p)->DeletingDestructor(f); }, 40);
}
