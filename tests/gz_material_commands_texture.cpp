// Differential tests for the material script's texture command (src/gz/MaterialCommands.cpp). The world:
// tests/gz_material_commands.h, plus the texture manager, its textures and the texture generators here.
#include "gz_material_commands.h"

#include "rz/cRZString.h"
#include "sg/cTextureManager.h"

#include <algorithm>
#include <cmath>

using namespace mattest;

namespace {

// ---- the texture manager, its textures, the generators ---------------------------------------------------------------

uint32_t gTexManager[4], gTexture[4], gLevel[4];
void *gHandleTexture = nullptr; // what a handle's Get (0xae8730) answers for a valid id
uint32_t gNamedId = 0;          // the id a handle made from a name (0xae8280) holds
void *gGenerated = nullptr;     // what the generators write to their out-parameter
float gFloats[8];               // what the face's EvalFloat answers, in turn
size_t gFloatNext = 0;
float gVectors[8][3];           // what EvalVector2 / 3 write, in turn
size_t gVectorNext = 0;

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// The face's slot 31 (EvalFloat) answers in st0; slots 34 / 35 (EvalVector2 / 3) write their out-parameter.
float __fastcall FaceEvalFloat(void *self, void *, const char *text)
{
    Record(self, 31, reinterpret_cast<uint32_t>(text), 0, 0, 0);
    return gFloats[gFloatNext++ % std::size(gFloats)];
}
template <int S, int N> uint32_t __fastcall FaceEvalVector(void *self, void *, float *out, const char *text, uint32_t least)
{
    uint32_t r = Record(self, S, Arg(out), reinterpret_cast<uint32_t>(text), least, 0);
    const float *v = gVectors[gVectorNext++ % std::size(gVectors)];
    for (int k = 0; k < N; ++k) out[k] = v[k];
    return r;
}

// A name the command passes may be in a string on its stack or in a heap block of it (the two sides' differ): only
// its text is logged.
uint32_t __fastcall ManagerId(void *self, void *, const char *name)
{
    uint32_t r = Record(self, 17, 0, 0, 0, 0);
    ExtraText(name);
    return r;
}
uint32_t __fastcall ManagerHas(void *self, void *, const char *name)
{
    uint32_t r = Record(self, 25, 0, 0, 0, 0);
    ExtraText(name);
    return r;
}
uint32_t __fastcall ManagerAdd(void *self, void *, uint32_t image, const char *name, uint32_t flag)
{
    uint32_t r = Record(self, 20, image, flag, 0, 0);
    ExtraText(name);
    return r;
}
// The filter states: the bytes the original sets (not the pad bytes).
void ExtraFilterState(const uint8_t *state)
{
    ExtraBytes(state, 5);
    if (state[4] == 0x1e)
        ExtraBytes(state + 6, 8);
    else
        ExtraBytes(state + 6, 7);
    ExtraBytes(state + 0x10, 4);
}
uint32_t __fastcall ManagerSetStates(void *self, void *, const uint8_t *filter, const uint8_t *mip, uint32_t texture)
{
    uint32_t r = Record(self, 27, Arg(filter), Arg(mip), texture, 0);
    ExtraFilterState(filter);
    ExtraFilterState(mip);
    return r;
}
// The shader's slot 11 (pass, state) and slot 13 (pass, sampler, name, color buffer index).
uint32_t __fastcall ShaderSetState(void *self, void *, uint32_t pass, const uint8_t *state)
{
    uint32_t r = Record(self, 11, pass, Arg(state), 0, 0);
    ExtraFilterState(state);
    return r;
}
uint32_t __fastcall ShaderSetTexture(void *self, void *, uint32_t pass, uint32_t sampler, const char *name, uint32_t index)
{
    uint32_t r = Record(self, 13, pass, sampler, index, 0);
    ExtraText(name);
    return r;
}

void *StandTexManager()
{
    gLog.push_back({40, 0, 0, {0, 0, 0, 0}});
    return gTexManager;
}
// 0xae7340: the name within the group: "g:" and the name (through our cRZString, the same layout).
void __cdecl StandResolve(cRZString *string, const char *name, uint32_t group, uint32_t flag)
{
    gLog.push_back({41, 0, Arg(string), {group, flag, 0, 0}});
    ExtraText(name);
    std::string text = std::string("g:") + name;
    string->FromChar(text.c_str());
}
// The texture handle (0xae8260 from an id, 0xae8280 from a name, 0xae82b0 its destructor, 0xae8730 Get).
void *__fastcall StandHandleFromId(uint32_t *self, void *, uint32_t id)
{
    gLog.push_back({50, 0, Arg(self), {id, 0, 0, 0}});
    *self = id;
    return self;
}
void *__fastcall StandHandleFromName(uint32_t *self, void *, const char *name)
{
    gLog.push_back({51, 0, Arg(self), {0, 0, 0, 0}});
    ExtraText(name);
    *self = gNamedId;
    return self;
}
void __fastcall StandHandleRelease(uint32_t *self, void *)
{
    gLog.push_back({52, 0, Arg(self), {*self, 0, 0, 0}});
    *self = 0xdddddddd;
}
void *__fastcall StandHandleGet(uint32_t *self, void *)
{
    gLog.push_back({53, 0, Arg(self), {*self, 0, 0, 0}});
    return *self == 0xffffffffu ? nullptr : gHandleTexture;
}

void ExtraVec(const float *v, int n)
{
    for (int k = 0; k < n; ++k) ExtraWord(Bits(v[k]));
}
void __cdecl StandDensity(int32_t w, int32_t h, float a, float b, void **out)
{
    gLog.push_back({42, 0, Arg(out), {static_cast<uint32_t>(w), static_cast<uint32_t>(h), Bits(a), Bits(b)}});
    *out = gGenerated;
}
void __cdecl StandNoise(int32_t x, int32_t y, int32_t octaves, float sx, float sy, void **out)
{
    gLog.push_back({43, 0, Arg(out), {static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(octaves), Bits(sx)}});
    ExtraWord(Bits(sy));
    *out = gGenerated;
}
template <int Id> void __cdecl StandRamp(int32_t x, int32_t y, const float *from, const float *to, void **out)
{
    gLog.push_back({Id, 0, Arg(out), {static_cast<uint32_t>(x), static_cast<uint32_t>(y), Arg(from), Arg(to)}});
    ExtraVec(from, 3);
    ExtraVec(to, 3);
    *out = gGenerated;
}
void __cdecl StandMipTest(int32_t x, int32_t y, void **out)
{
    gLog.push_back({46, 0, Arg(out), {static_cast<uint32_t>(x), static_cast<uint32_t>(y), 0, 0}});
    *out = gGenerated;
}
void __cdecl StandNormalCube(uint32_t size, void **out)
{
    gLog.push_back({47, 0, Arg(out), {size, 0, 0, 0}});
    *out = gGenerated;
}
void __cdecl StandFalloffCube(uint32_t size, const float *range, const float *color, int32_t power, void **out)
{
    gLog.push_back({48, 0, Arg(out), {size, Arg(range), Arg(color), static_cast<uint32_t>(power)}});
    ExtraVec(range, 2);
    ExtraVec(color, 3);
    *out = gGenerated;
}

struct TextureStands {
    shape::StandIn manager{0xae8750u, &nGZSceneGraph::TextureManager, &StandTexManager}; // the image's callee and ours both stood in
    shape::StandIn resolve{0xae7340u, &nGZSceneGraph::ResolveResourceName, &StandResolve};
    shape::StandIn fromId{0xae8260u, &nGZSceneGraph::cTextureHandle::InitFromId, &StandHandleFromId};
    shape::StandIn fromName{0xae8280u, &nGZSceneGraph::cTextureHandle::InitFromName, &StandHandleFromName};
    shape::StandIn release{0xae82b0u, &nGZSceneGraph::cTextureHandle::Destroy, &StandHandleRelease};
    shape::StandIn get{0xae8730u, &nGZSceneGraph::cTextureHandle::Get, &StandHandleGet};
    shape::CodePatch density{0xaec1a0u, shape::CodeOf(&StandDensity)};
    shape::CodePatch noise{0xaec6b0u, shape::CodeOf(&StandNoise)};
    shape::CodePatch rampH{0xaec8e0u, shape::CodeOf(&StandRamp<44>)};
    shape::CodePatch rampV{0xaecba0u, shape::CodeOf(&StandRamp<45>)};
    shape::CodePatch mipTest{0xaec310u, shape::CodeOf(&StandMipTest)};
    shape::CodePatch normalCube{0xaec4a0u, shape::CodeOf(&StandNormalCube)};
    shape::CodePatch falloffCube{0xaebd70u, shape::CodeOf(&StandFalloffCube)};
};

// The vtables of the texture world (the manager, a texture, its first level), set up per case.
struct TextureWorld {
    FakeVTable managerVt{43}, textureVt{44}, levelVt{45};
    TextureWorld(World &w)
    {
        *reinterpret_cast<void **>(gTexManager) = managerVt.slots;
        *reinterpret_cast<void **>(gTexture) = textureVt.slots;
        *reinterpret_cast<void **>(gLevel) = levelVt.slots;
        managerVt.slots[17] = reinterpret_cast<void *>(&ManagerId);
        managerVt.slots[25] = reinterpret_cast<void *>(&ManagerHas);
        managerVt.slots[20] = reinterpret_cast<void *>(&ManagerAdd);
        managerVt.slots[27] = reinterpret_cast<void *>(&ManagerSetStates);
        textureVt.Set<8>(1);
        levelVt.Set<17>(1);
        levelVt.Set<18>(1);
        w.faceVt.slots[31] = reinterpret_cast<void *>(&FaceEvalFloat);
        w.faceVt.slots[34] = reinterpret_cast<void *>(&FaceEvalVector<34, 2>);
        w.faceVt.slots[35] = reinterpret_cast<void *>(&FaceEvalVector<35, 3>);
        FakeVTable &shader = w.kindVt[kShader];
        shader.Set<25>(2);
        shader.slots[11] = reinterpret_cast<void *>(&ShaderSetState);
        shader.slots[13] = reinterpret_cast<void *>(&ShaderSetTexture);
    }
};

using ExecuteFn = void(__thiscall *)(void *, void *);

} // namespace

DIFF_TEST(TextureCommand_Execute, 0xad6f30)
{
    auto orig = ctx.Original<ExecuteFn>(0xad6f30);
    Stands stands;
    TextureStands textureStands;
    const char *const switches[] = {"noise", "rampH", "rampV", "mipTest", "normalizationCubeMap", "alphaFalloffCubeMap",
                                    "colorBufferIndex"};
    // Past 32 bits (1e10: the 64-bit truncation's low word differs from the 32-bit one) and past 64 bits (1e20).
    const float floats[] = {0.5f, 3.7f, -2.2f, 7.0f, 1000.25f, -0.9f, 1e10f, 16.0f, -3e9f, 1e20f};
    const int32_t sizes[] = {0, -1, 3, 4, 100, 256, 12345};
    std::mt19937 gen(0xad6f30);
    for (int i = 0; i < 400; ++i) {
        World w(gen);
        TextureWorld tw(w);
        w.SaneFields();
        w.argsVt.slots[3] = reinterpret_cast<void *>(&ArgsTakeSwitchArgs); // switches answered by name
        gSwitchArgs.clear();
        *reinterpret_cast<void **>(w.cmd + 0xc) = w.state; // a plain command: the state at +0xc
        w.Put(0x1d0, gen() % 8 == 0 ? gen() % 7 : 4);
        const uint32_t groups[] = {0, 0x1c0532fa, 0x12345678};
        w.Put(0x1d4, groups[gen() % std::size(groups)]);
        w.Put(0x1cc, gen() % 2 ? (gen() | 0x7800u) : gen());
        w.PutObject(0x1fc, kShader, false);
        w.Put(0x27c, gen() % 4);
        w.Put(0x284, gen() % 6 == 0 ? 0 : 1 + gen() % 3);
        for (uint32_t k = 0; k < 16; ++k) w.Put(0x288 + 4 * k, gen() % 16);
        w.kindVt[kShader].ret[25] = gen() % 12 == 0 ? (gen() % 2 ? 1 : 0x100) : 0;
        // The switches: each one sometimes given; more than one sometimes.
        size_t given = 0;
        for (const char *s : switches)
            if (gen() % 5 == 0) {
                gSwitchArgs.push_back({s, w.switchArgs});
                ++given;
            }
        w.argsVt.ret[9] = gen() % 6 == 0 ? static_cast<uint32_t>(given + gen() % 2) : (given > 1 ? 1 : static_cast<uint32_t>(given));
        w.faceVt.ret[32] = gen() % 4;
        w.faceVt.ret[33] = gen() % 3 == 0 ? 0 : 1 + gen() % 64;
        // The texture manager: the name's id, the texture, its first level.
        tw.managerVt.ret[17] = gen() % 4 == 0 ? 0xffffffffu : 5 + gen() % 3;
        gHandleTexture = gen() % 5 == 0 ? nullptr : gTexture;
        const uint32_t types[] = {4, 4, 4, 3, 5, 0xffffffffu}; // the texture's type: 4 a render target
        tw.textureVt.ret[5] = types[gen() % std::size(types)];
        tw.textureVt.ret[8] = gen() % 5 == 0 ? 0 : reinterpret_cast<uint32_t>(gLevel);
        tw.levelVt.ret[6] = gen() % 4 == 0 ? (gen() % 2 ? 0 : 0x100) : 1;
        tw.levelVt.ret[17] = static_cast<uint32_t>(sizes[gen() % std::size(sizes)]);
        tw.levelVt.ret[18] = static_cast<uint32_t>(sizes[gen() % std::size(sizes)]);
        tw.managerVt.ret[25] = gen() % 3 == 0 ? (gen() % 2 ? 1 : 0x100) : 0;
        gNamedId = gen() % 4 == 0 ? 0xffffffffu : 9;
        gGenerated = w.Object(kOther);
        for (float &f : gFloats) f = floats[gen() % std::size(floats)];
        for (auto &v : gVectors)
            for (float &f : v) f = floats[gen() % std::size(floats)];
        w.before = [&] {
            gFloatNext = 0;
            gVectorNext = 0;
        };
        char what[64];
        std::snprintf(what, sizeof(what), "Execute case %d", i);
        Compare(
            ctx, w, what, [&] { orig(w.cmd, w.args); },
            [&] { reinterpret_cast<sg::cTextureCommand *>(w.cmd)->Execute(reinterpret_cast<cp::cIArguments *>(w.args)); });
    }
}

DIFF_TEST(TextureCommand_GetName, 0xad99a0)
{
    auto orig = ctx.Original<const char *(__thiscall *)(void *)>(0xad99a0);
    uint8_t object[0x10] = {};
    const char *a = orig(object);
    const char *b = reinterpret_cast<sg::cTextureCommand *>(object)->GetName();
    ++ctx.cases;
    if (a != b || std::strcmp(b, "texture") != 0) ctx.Fail("GetName: %p '%s' vs %p '%s'", a, a, b, b);
}
