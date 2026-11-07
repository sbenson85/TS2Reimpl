// Differential tests of the game's copies of cShaderComponentT<I> / cShaderControllerT<I> (lane TG, scenegraph M3b;
// src/sg/SGShaderControllers.h): one test per body (constructors, destructors, deleting destructors, both levels'
// QueryInterface, IsEquivalent, Init, Release), one case per copy, the image's body against ours (cShaderControllerCopy's
// adapter, or the template's constructor / destructor by its decorated name).
//
// The object is a hand-made part: a vtable as long as the copy's leaf (slots 0 QueryInterface, 1 AddRef, 6 GetHash, the
// deleting destructor d, HashParams d + 1 and EqualParams d + 2 test functions that note their calls; every other slot
// a trap), its +4 count, +8 registered byte and +0xc hash the bodies keep. Three such objects (the one under test, the
// "other" IsEquivalent asks and a third its QueryInterface may answer); a material manager fake in GetCachedQueriedObject's
// cache (0x10f8fcc) noting Unregister (slot 27).
#include "difftest.h"
#include "free_recorder.h"

#include "hook/hooks.h"
#include "sg/SGShaderControllers.h"

#include <cstring>
#include <new>
#include <random>
#include <vector>

using namespace nGZSceneGraph;

// The component level's constructor and destructor per instantiation, by their decorated names (the game reaches ours
// only through the controller's constructor and destructor and the component's deleting destructor).
#pragma comment(linker, "/alternatename:_TGT_AnimColorScalarAlpha_CompConstruct=??0?$cShaderComponentT@VcIAnimColorScalarAlpha@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_TGT_AnimColorScalarAlpha_CompDestruct=??1?$cShaderComponentT@VcIAnimColorScalarAlpha@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
extern "C" const uint8_t TGT_AnimColorScalarAlpha_CompConstruct, TGT_AnimColorScalarAlpha_CompDestruct;
#pragma comment(linker, "/alternatename:_TGT_AnimatedTextureMatrix_CompConstruct=??0?$cShaderComponentT@VcIAnimatedTextureMatrix@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_TGT_AnimatedTextureMatrix_CompDestruct=??1?$cShaderComponentT@VcIAnimatedTextureMatrix@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
extern "C" const uint8_t TGT_AnimatedTextureMatrix_CompConstruct, TGT_AnimatedTextureMatrix_CompDestruct;
#pragma comment(linker, "/alternatename:_TGT_AnimatedTiledTexture_CompConstruct=??0?$cShaderComponentT@VcIAnimatedTiledTexture@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_TGT_AnimatedTiledTexture_CompDestruct=??1?$cShaderComponentT@VcIAnimatedTiledTexture@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
extern "C" const uint8_t TGT_AnimatedTiledTexture_CompConstruct, TGT_AnimatedTiledTexture_CompDestruct;
#pragma comment(linker, "/alternatename:_TGT_ApplyColorScalarRadiance_CompConstruct=??0?$cShaderComponentT@VcIApplyColorScalarRadiance@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_TGT_ApplyColorScalarRadiance_CompDestruct=??1?$cShaderComponentT@VcIApplyColorScalarRadiance@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
extern "C" const uint8_t TGT_ApplyColorScalarRadiance_CompConstruct, TGT_ApplyColorScalarRadiance_CompDestruct;
#pragma comment(linker, "/alternatename:_TGT_ApplyFFLightStates_CompConstruct=??0?$cShaderComponentT@VcIApplyFFLightStates@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_TGT_ApplyFFLightStates_CompDestruct=??1?$cShaderComponentT@VcIApplyFFLightStates@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
extern "C" const uint8_t TGT_ApplyFFLightStates_CompConstruct, TGT_ApplyFFLightStates_CompDestruct;
#pragma comment(linker, "/alternatename:_TGT_ApplyForceDepthTestFailCondition_CompConstruct=??0?$cShaderComponentT@VcIApplyForceDepthTestFailCondition@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_TGT_ApplyForceDepthTestFailCondition_CompDestruct=??1?$cShaderComponentT@VcIApplyForceDepthTestFailCondition@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
extern "C" const uint8_t TGT_ApplyForceDepthTestFailCondition_CompConstruct, TGT_ApplyForceDepthTestFailCondition_CompDestruct;
#pragma comment(linker, "/alternatename:_TGT_ApplyNonStandardLightState_CompConstruct=??0?$cShaderComponentT@VcIApplyNonStandardLightState@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_TGT_ApplyNonStandardLightState_CompDestruct=??1?$cShaderComponentT@VcIApplyNonStandardLightState@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
extern "C" const uint8_t TGT_ApplyNonStandardLightState_CompConstruct, TGT_ApplyNonStandardLightState_CompDestruct;
#pragma comment(linker, "/alternatename:_TGT_ApplyRenderTarget_CompConstruct=??0?$cShaderComponentT@VcIApplyRenderTarget@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_TGT_ApplyRenderTarget_CompDestruct=??1?$cShaderComponentT@VcIApplyRenderTarget@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
extern "C" const uint8_t TGT_ApplyRenderTarget_CompConstruct, TGT_ApplyRenderTarget_CompDestruct;
#pragma comment(linker, "/alternatename:_TGT_ApplyShaderProgramConstants_CompConstruct=??0?$cShaderComponentT@VcIApplyShaderProgramConstants@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_TGT_ApplyShaderProgramConstants_CompDestruct=??1?$cShaderComponentT@VcIApplyShaderProgramConstants@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
extern "C" const uint8_t TGT_ApplyShaderProgramConstants_CompConstruct, TGT_ApplyShaderProgramConstants_CompDestruct;
#pragma comment(linker, "/alternatename:_TGT_ApplyStencilStateForOverlappingReflections_CompConstruct=??0?$cShaderComponentT@VcIApplyStencilStateForOverlappingReflections@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_TGT_ApplyStencilStateForOverlappingReflections_CompDestruct=??1?$cShaderComponentT@VcIApplyStencilStateForOverlappingReflections@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
extern "C" const uint8_t TGT_ApplyStencilStateForOverlappingReflections_CompConstruct, TGT_ApplyStencilStateForOverlappingReflections_CompDestruct;
#pragma comment(linker, "/alternatename:_TGT_AttributeColorScalar_CompConstruct=??0?$cShaderComponentT@VcIAttributeColorScalar@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_TGT_AttributeColorScalar_CompDestruct=??1?$cShaderComponentT@VcIAttributeColorScalar@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
extern "C" const uint8_t TGT_AttributeColorScalar_CompConstruct, TGT_AttributeColorScalar_CompDestruct;
#pragma comment(linker, "/alternatename:_TGT_AttributeFFMaterial_CompConstruct=??0?$cShaderComponentT@VcIAttributeFFMaterial@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_TGT_AttributeFFMaterial_CompDestruct=??1?$cShaderComponentT@VcIAttributeFFMaterial@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
extern "C" const uint8_t TGT_AttributeFFMaterial_CompConstruct, TGT_AttributeFFMaterial_CompDestruct;
#pragma comment(linker, "/alternatename:_TGT_AttributeFFScaleModelToViewTransform_CompConstruct=??0?$cShaderComponentT@VcIAttributeFFScaleModelToViewTransform@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_TGT_AttributeFFScaleModelToViewTransform_CompDestruct=??1?$cShaderComponentT@VcIAttributeFFScaleModelToViewTransform@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
extern "C" const uint8_t TGT_AttributeFFScaleModelToViewTransform_CompConstruct, TGT_AttributeFFScaleModelToViewTransform_CompDestruct;
#pragma comment(linker, "/alternatename:_TGT_AttributeFFTextureMatrix_CompConstruct=??0?$cShaderComponentT@VcIAttributeFFTextureMatrix@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_TGT_AttributeFFTextureMatrix_CompDestruct=??1?$cShaderComponentT@VcIAttributeFFTextureMatrix@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
extern "C" const uint8_t TGT_AttributeFFTextureMatrix_CompConstruct, TGT_AttributeFFTextureMatrix_CompDestruct;
#pragma comment(linker, "/alternatename:_TGT_FFApplyTransforms_CompConstruct=??0?$cShaderComponentT@VcIFFApplyTransforms@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_TGT_FFApplyTransforms_CompDestruct=??1?$cShaderComponentT@VcIFFApplyTransforms@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
extern "C" const uint8_t TGT_FFApplyTransforms_CompConstruct, TGT_FFApplyTransforms_CompDestruct;
#pragma comment(linker, "/alternatename:_TGT_FFDepthOffset_CompConstruct=??0?$cShaderComponentT@VcIFFDepthOffset@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_TGT_FFDepthOffset_CompDestruct=??1?$cShaderComponentT@VcIFFDepthOffset@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
extern "C" const uint8_t TGT_FFDepthOffset_CompConstruct, TGT_FFDepthOffset_CompDestruct;
#pragma comment(linker, "/alternatename:_TGT_ShaderControllerNotify_CompConstruct=??0?$cShaderComponentT@VcIShaderControllerNotify@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_TGT_ShaderControllerNotify_CompDestruct=??1?$cShaderComponentT@VcIShaderControllerNotify@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
extern "C" const uint8_t TGT_ShaderControllerNotify_CompConstruct, TGT_ShaderControllerNotify_CompDestruct;
#pragma comment(linker, "/alternatename:_TGT_ValidateRenderShaderContext_CompConstruct=??0?$cShaderComponentT@VcIValidateRenderShaderContext@nGZSceneGraph@@@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_TGT_ValidateRenderShaderContext_CompDestruct=??1?$cShaderComponentT@VcIValidateRenderShaderContext@nGZSceneGraph@@@nGZSceneGraph@@UAE@XZ")
extern "C" const uint8_t TGT_ValidateRenderShaderContext_CompConstruct, TGT_ValidateRenderShaderContext_CompDestruct;

namespace {

enum Body { kCompCtor, kCtrlCtor, kCompDtor, kCtrlDtor, kCompDel, kCtrlDel, kCompQI, kCtrlQI, kEquiv, kInit, kBodies };

struct Copy {
    const char *name;
    int slot;     // the deleting destructor's (I::kSlots)
    uint32_t iid; // I::kIid
    uint32_t rva[kBodies];
    uint32_t release; // the Release body for this slot count
    void *ours[kBodies];
    void *oursRelease;
};

template <class I>
Copy Make(const char *name, std::initializer_list<uint32_t> rvas, uint32_t release, const uint8_t &compCtor,
          const uint8_t &ctrlCtor, const uint8_t &compDtor, const uint8_t &ctrlDtor)
{
    using C = cShaderControllerCopy<I>;
    Copy c{name, I::kSlots, I::kIid, {}, release, {}, MemberAddress(&C::HookRelease)};
    std::copy(rvas.begin(), rvas.end(), c.rva);
    c.ours[kCompCtor] = const_cast<uint8_t *>(&compCtor);
    c.ours[kCtrlCtor] = const_cast<uint8_t *>(&ctrlCtor);
    c.ours[kCompDtor] = const_cast<uint8_t *>(&compDtor);
    c.ours[kCtrlDtor] = const_cast<uint8_t *>(&ctrlDtor);
    c.ours[kCompDel] = MemberAddress(&C::HookComponentDestroy);
    c.ours[kCtrlDel] = MemberAddress(&C::HookControllerDestroy);
    c.ours[kCompQI] = MemberAddress(&C::HookComponentQueryInterface);
    c.ours[kCtrlQI] = MemberAddress(&C::HookQueryInterface);
    c.ours[kEquiv] = MemberAddress(&C::HookIsEquivalent);
    c.ours[kInit] = MemberAddress(&C::HookInit);
    return c;
}

// The rows: component ctor, controller ctor, component dtor, controller dtor, the two deleting destructors, the two
// QueryInterfaces, IsEquivalent, Init; then the Release body.
#define ROW(N, a, b, c, d, e, f, g, h, i, j, rel)                                                                       \
    Make<cI##N>(#N, {a, b, c, d, e, f, g, h, i, j}, rel, TGT_##N##_CompConstruct, ShaderControllerT_##N##_Construct,                   \
                TGT_##N##_CompDestruct, ShaderControllerT_##N##_Destruct)

const std::vector<Copy> &Copies()
{
    static const std::vector<Copy> copies = {
        ROW(AnimColorScalarAlpha, 0xabd180, 0xabd3c0, 0xabdf80, 0xabe0a0, 0xabe3f0, 0xabe630, 0xac55c0, 0xac5920, 0xac4c00, 0xac36f0, 0xac5ec0),
        ROW(AnimatedTextureMatrix, 0xabd1a0, 0xabd3e0, 0xabdf90, 0xabe0b0, 0xabe410, 0xabe650, 0xac55f0, 0xac5970, 0xac4c50, 0xac3730, 0xac5ef0),
        ROW(AnimatedTiledTexture, 0xabd1c0, 0xabd400, 0xabdfa0, 0xabe0c0, 0xabe430, 0xabe670, 0xac5620, 0xac59c0, 0xac4ca0, 0xac3770, 0xac5f20),
        ROW(ApplyColorScalarRadiance, 0xabd1e0, 0xabd420, 0xabdfb0, 0xabe0d0, 0xabe450, 0xabe690, 0xac5650, 0xac5a10, 0xac4cf0, 0xac37b0, 0xac5f50),
        ROW(ApplyFFLightStates, 0xabd200, 0xabd440, 0xabdfc0, 0xabe0e0, 0xabe470, 0xabe6b0, 0xac5680, 0xac5a60, 0xac4d40, 0xac37f0, 0x254440),
        ROW(ApplyForceDepthTestFailCondition, 0xabd220, 0xabd460, 0xabdfd0, 0xabe0f0, 0xabe490, 0xabe6d0, 0xac56b0, 0xac5ab0, 0xac4d90, 0xac3830, 0xac5ec0),
        ROW(ApplyNonStandardLightState, 0xabd240, 0xabd480, 0xabdfe0, 0xabe100, 0xabe4b0, 0xabe6f0, 0xac56e0, 0xac5b00, 0xac4de0, 0xac3870, 0xac5f50),
        ROW(ApplyRenderTarget, 0xabd260, 0xabd4a0, 0xabdff0, 0xabe110, 0xabe4d0, 0xabe710, 0xac5710, 0xac5b50, 0xac4e30, 0xac38b0, 0xac5f80),
        ROW(ApplyShaderProgramConstants, 0xabd280, 0xabd4c0, 0xabe000, 0xabe120, 0xabe4f0, 0xabe730, 0xac5740, 0xac5ba0, 0xac4e80, 0xac38f0, 0xac5f50),
        ROW(ApplyStencilStateForOverlappingReflections, 0xabd2a0, 0xabd4e0, 0xabe010, 0xabe130, 0xabe510, 0xabe750, 0xac5770, 0xac5bf0, 0xac4ed0, 0xac3930, 0x254440),
        ROW(AttributeColorScalar, 0xabd2c0, 0xabd500, 0xabe020, 0xabe140, 0xabe530, 0xabe770, 0xac57a0, 0xac5c40, 0xac4f20, 0xac3970, 0xac5fb0),
        ROW(AttributeFFMaterial, 0xabd2e0, 0xabd520, 0xabe030, 0xabe150, 0xabe550, 0xabe790, 0xac57d0, 0xac5c90, 0xac4f70, 0xac39b0, 0xac5fe0),
        ROW(AttributeFFScaleModelToViewTransform, 0xabd300, 0xabd540, 0xabe040, 0xabe160, 0xabe570, 0xabe7b0, 0xac5800, 0xac5ce0, 0xac4fc0, 0xac39f0, 0xac5f50),
        ROW(AttributeFFTextureMatrix, 0xabd320, 0xabd560, 0xabe050, 0xabe170, 0xabe590, 0xabe7d0, 0xac5830, 0xac5d30, 0xac5010, 0xac3a30, 0xac5fe0),
        ROW(FFApplyTransforms, 0xabd340, 0xabd580, 0xabe060, 0xabe180, 0xabe5b0, 0xabe7f0, 0xac5860, 0xac5d80, 0xac5060, 0xac3a70, 0xac5f50),
        ROW(FFDepthOffset, 0xabd360, 0xabd5a0, 0xabe070, 0xabe190, 0xabe5d0, 0xabe810, 0xac5890, 0xac5dd0, 0xac50b0, 0xac3ab0, 0xac5f50),
        ROW(ShaderControllerNotify, 0xabd380, 0xabd5c0, 0xabe080, 0xabe1a0, 0xabe5f0, 0xabe830, 0xac58c0, 0xac5e20, 0xac5100, 0xac3af0, 0xac5f50),
        ROW(ValidateRenderShaderContext, 0xabd3a0, 0xabd5e0, 0xabe090, 0xabe1b0, 0xabe610, 0xabe850, 0xac58f0, 0xac5e70, 0xac5150, 0xac3b30, 0xac5f80),
    };
    return copies;
}

// ---- the objects ----
struct Object {
    void **vt;
    uint32_t count;     // +4
    uint32_t registered; // +8 (the byte; the rest a pattern)
    uint32_t hash;      // +0xc
    // the fakes' own fields
    uint32_t id;
    uint32_t refs;
    uint32_t params;    // HashParams' answer
    uint32_t qiIid;     // QueryInterface answers this iid ...
    Object *qiAnswer;   // ... with this ...
    uint32_t qiOk;      // ... and this result
    uint32_t equal;     // EqualParams' answer
};
Object gObjects[3];
void *gVTable[32];
std::vector<uint32_t> gLog;

void Note(std::initializer_list<uint32_t> words)
{
    gLog.insert(gLog.end(), words.begin(), words.end());
    gLog.push_back(0xeeeeeeee);
}
uint32_t IdOf(const void *p)
{
    for (const Object &o : gObjects)
        if (p == &o) return o.id;
    return p ? 0xbad0bad0 : 0;
}

bool __fastcall FakeQuery(Object *self, void *, uint32_t iid, void **out)
{
    Note({1, self->id, iid});
    if (iid != self->qiIid) return false;
    *out = self->qiAnswer;
    return self->qiOk != 0;
}
uint32_t __fastcall FakeAddRef(Object *self, void *)
{
    Note({2, self->id});
    return ++self->refs;
}
uint32_t __fastcall FakeGetHash(Object *self, void *)
{
    Note({6, self->id});
    return self->hash;
}
void *__fastcall FakeDeleting(Object *self, void *, uint32_t flags)
{
    Note({0xd, self->id, flags, self->count, self->registered});
    return self;
}
uint32_t __fastcall FakeHashParams(Object *self, void *)
{
    Note({0xe, self->id});
    return self->params;
}
bool __fastcall FakeEqualParams(Object *self, void *, void *other)
{
    Note({0xf, self->id, IdOf(other)});
    return self->equal != 0;
}
void __fastcall FakeTrap(Object *self, void *) { Note({0xdead, self->id}); }

struct Manager {
    void **vt;
};
void *gManagerVTable[40];
Manager gManager{gManagerVTable};
void __fastcall ManagerUnregister(Manager *, void *, void *part) { Note({27, IdOf(part)}); }
void __fastcall ManagerTrap(Manager *, void *) { Note({0xdeae}); }

void Build(const Copy &c, std::mt19937 &rng)
{
    for (void *&s : gVTable) s = reinterpret_cast<void *>(&FakeTrap);
    gVTable[0] = reinterpret_cast<void *>(&FakeQuery);
    gVTable[1] = reinterpret_cast<void *>(&FakeAddRef);
    gVTable[6] = reinterpret_cast<void *>(&FakeGetHash);
    gVTable[c.slot] = reinterpret_cast<void *>(&FakeDeleting);
    gVTable[c.slot + 1] = reinterpret_cast<void *>(&FakeHashParams);
    gVTable[c.slot + 2] = reinterpret_cast<void *>(&FakeEqualParams);
    for (void *&s : gManagerVTable) s = reinterpret_cast<void *>(&ManagerTrap);
    gManagerVTable[27] = reinterpret_cast<void *>(&ManagerUnregister);
    for (uint32_t i = 0; i < 3; ++i) {
        Object &o = gObjects[i];
        o.vt = gVTable;
        o.id = 300 + i;
        static const uint32_t counts[] = {1, 2, 3, 0x80000001u, 0};
        o.count = counts[rng() % 5];
        o.registered = (rng() & 0xffffff00u) | (rng() % 3 == 0 ? 0 : rng() % 2 ? 1 : 0x80);
        const uint32_t hashes[] = {0x11111111, 0x22222222, rng()};
        o.hash = hashes[rng() % 3];
        o.refs = 50;
        o.params = rng() % 2 ? rng() : rng() % 4;
        o.qiIid = rng() % 4 ? c.iid + 1 : rng() % 2 ? c.iid : 0x2d2df503;
        o.qiAnswer = &gObjects[rng() % 3];
        o.qiOk = rng() % 5 != 0;
        o.equal = rng() % 2;
    }
}

void NoteObjects()
{
    for (const Object &o : gObjects) Note({o.count, o.registered, o.hash, o.refs});
}

// Runs both sides on the same world (rebuilt from the seed), each with its function, and compares the logs.
template <class Run> void CheckSides(difftest::Context &ctx, const Copy &c, unsigned seeds, Run &&run)
{
    gLog.reserve(4096);
    for (unsigned seed = 0; seed < seeds; ++seed) {
        std::vector<uint32_t> logs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 rng(seed * 977u + 13u);
            Build(c, rng);
            gLog.clear();
            run(side == 0, rng);
            NoteObjects();
            logs[side] = gLog;
        }
        ++ctx.cases;
        if (logs[0] != logs[1]) {
            size_t i = 0;
            while (i < logs[0].size() && i < logs[1].size() && logs[0][i] == logs[1][i]) ++i;
            ctx.Fail("%s seed %u: logs differ at word %zu (original %zu words, ours %zu): %08x vs %08x", c.name, seed, i,
                     logs[0].size(), logs[1].size(), i < logs[0].size() ? logs[0][i] : 0,
                     i < logs[1].size() ? logs[1][i] : 0);
        }
    }
}

template <class Fn> Fn Pick(difftest::Context &ctx, const Copy &c, Body body, bool original)
{
    return original ? ctx.Original<Fn>(c.rva[body]) : reinterpret_cast<Fn>(c.ours[body]);
}

constexpr unsigned kSeeds = 60;

// The iids QueryInterface is asked: the component's, the controller's, I's and I's controller iid, a neighbour's and
// junk.
uint32_t Iid(const Copy &c, std::mt19937 &rng)
{
    switch (rng() % 8) {
    case 0: return 1;
    case 1: return 0x9c544810;
    case 2: return 0x2d2df503;
    case 3: return c.iid;
    case 4: return c.iid + 1;
    case 5: return c.iid - 1;
    case 6: return Copies()[rng() % Copies().size()].iid + rng() % 2;
    default: return rng();
    }
}

void QueryCase(difftest::Context &ctx, Body body)
{
    using Fn = bool(__thiscall *)(void *, uint32_t, void **);
    for (const Copy &c : Copies())
        CheckSides(ctx, c, kSeeds, [&](bool original, std::mt19937 &rng) {
            const Fn fn = Pick<Fn>(ctx, c, body, original);
            for (int k = 0; k < 6; ++k) {
                const uint32_t iid = Iid(c, rng);
                void *out = &gObjects[2];
                const bool withOut = rng() % 6 != 0;
                const bool r = fn(&gObjects[0], iid, withOut ? &out : nullptr);
                Note({0x51, iid, uint32_t(r), IdOf(out)});
            }
        });
}

} // namespace

// The component level's QueryInterface (slot 0): 1, cIShaderComponent's and I's iid give the object, counted; a null out
// or another iid nothing.
DIFF_TEST(sg_shader_component_query_interface, 0xac55c0) { QueryCase(ctx, kCompQI); }

// The controller level's QueryInterface (slot 0): the controller iid counted, I's controller iid uncounted, the rest the
// component's.
DIFF_TEST(sg_shader_controller_query_interface, 0xac5920) { QueryCase(ctx, kCtrlQI); }

// IsEquivalent (slot 7): the other part asked for I's controller iid; the same object, or an equal hash (asked of the
// other) and EqualParams (slot d + 2) on what it answered.
DIFF_TEST(sg_shader_controller_is_equivalent, 0xac4c00)
{
    using Fn = bool(__thiscall *)(void *, void *);
    for (const Copy &c : Copies())
        CheckSides(ctx, c, kSeeds, [&](bool original, std::mt19937 &rng) {
            const Fn fn = Pick<Fn>(ctx, c, kEquiv, original);
            for (int k = 0; k < 3; ++k) {
                Object &other = gObjects[1 + rng() % 2];
                const bool r = fn(&gObjects[0], &other);
                Note({0x57, other.id, uint32_t(r)});
                gObjects[1].qiAnswer = &gObjects[rng() % 3];
                gObjects[1].equal = rng() % 2;
            }
        });
}

// Init (slot 8): the hash of HashParams (slot d + 1) and I's iid in +0xc; true.
DIFF_TEST(sg_shader_controller_init, 0xac36f0)
{
    using Fn = bool(__thiscall *)(void *, void *, void *, void *, void *);
    for (const Copy &c : Copies())
        CheckSides(ctx, c, kSeeds, [&](bool original, std::mt19937 &rng) {
            const Fn fn = Pick<Fn>(ctx, c, kInit, original);
            const uint32_t args[4] = {rng(), rng(), rng(), rng()};
            const bool r = fn(&gObjects[0], reinterpret_cast<void *>(uintptr_t(args[0])),
                              reinterpret_cast<void *>(uintptr_t(args[1])), reinterpret_cast<void *>(uintptr_t(args[2])),
                              reinterpret_cast<void *>(uintptr_t(args[3])));
            Note({0x58, uint32_t(r)});
        });
}

// Release (slot 2, one body per slot count): the count; at zero a registered part is unregistered from the material
// manager (GetCachedQueriedObject, slot 27), then the deleting destructor (slot d) with 1.
DIFF_TEST(sg_shader_component_release, 0xac5ec0)
{
    using Fn = uint32_t(__thiscall *)(void *);
    void **const cache = ctx.image.At<void *>(0x10f8fcc);
    void *const saved = *cache;
    *cache = &gManager;
    for (const Copy &c : Copies())
        CheckSides(ctx, c, kSeeds, [&](bool original, std::mt19937 &rng) {
            const Fn fn = original ? ctx.Original<Fn>(c.release) : reinterpret_cast<Fn>(c.oursRelease);
            for (int k = 0; k < 3; ++k) {
                Note({0x52, fn(&gObjects[0])});
                if (gObjects[0].count == 0) gObjects[0].count = 1 + rng() % 2;
                gObjects[0].registered ^= rng() % 2;
            }
        });
    *cache = saved;
}

// The constructors and destructors of both levels: the fields they write (the vtable pointer aside: the image's or
// ours), the pattern elsewhere left alone; constructors return the object.
DIFF_TEST(sg_shader_controller_construct_destruct, 0xabd3c0)
{
    using Ctor = void *(__thiscall *)(void *);
    using Dtor = void(__thiscall *)(void *);
    for (const Copy &c : Copies())
        for (Body body : {kCompCtor, kCtrlCtor, kCompDtor, kCtrlDtor})
            CheckSides(ctx, c, 8, [&](bool original, std::mt19937 &rng) {
                uint32_t block[8];
                for (uint32_t &w : block) w = rng();
                if (body == kCompCtor || body == kCtrlCtor) {
                    void *r = Pick<Ctor>(ctx, c, body, original)(block);
                    Note({0x53, uint32_t(r == block)});
                } else {
                    block[0] = 0x7a7a7a7a;
                    Pick<Dtor>(ctx, c, body, original)(block);
                }
                Note({0x54, uint32_t(body)});
                gLog.insert(gLog.end(), block + 1, block + 8);
            });
}

// The deleting destructors of both levels (ret 4): the level's destructor, the block freed (0xc / 0x10) with flag 1;
// the object returned.
DIFF_TEST(sg_shader_controller_deleting_destructors, 0xabe630)
{
    using Fn = void *(__thiscall *)(void *, uint32_t);
    for (const Copy &c : Copies())
        for (Body body : {kCompDel, kCtrlDel})
            CheckSides(ctx, c, 8, [&](bool original, std::mt19937 &rng) {
                const size_t size = body == kCompDel ? 0xc : 0x10;
                const uint32_t flags = rng() % 2 ? 1 : rng() % 2 ? 0 : 2;
                uint32_t stack[4];
                uint32_t *block = flags & 1 ? static_cast<uint32_t *>(::operator new(size)) : stack;
                for (size_t i = 0; i < size / 4; ++i) block[i] = rng();
                difftest::FreeRecorder freed;
                void *r = Pick<Fn>(ctx, c, body, original)(block, flags);
                freed.Stop();
                Note({0x55, uint32_t(body), flags, uint32_t(r == block), uint32_t(freed.Count()),
                      uint32_t(freed.Freed(block))});
                if (!(flags & 1)) gLog.insert(gLog.end(), block + 1, block + size / 4);
            });
}
