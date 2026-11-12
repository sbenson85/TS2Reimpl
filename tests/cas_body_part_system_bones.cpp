// Differential test for nTSCASCore::cTSCASBodyPartSystem, lane JZ: ApplyBoneAdjusts (slot 82, 0xce0e60). Fakes: the
// entry helper 0xcf04b0 (an entry whose bone adjust records are fake polymorphic objects), the bone lookup 0xae7490 and
// the extension iterator 0xaeb5b0 (scripted; ours, so on both sides), the target, its bone nodes and extensions, the framework object (0x1ae18d
// on both sides: ours is a stand-in too) and the extension it creates (with a virtual base behind a vbptr). The image's
// operator new / sized delete (0x824632 / 0x824624) stand in for ours: the original frees the tag strings with them.
#include "cas_body_part_system.h"
#include "code_patch.h"
#include "rz/StaticGetters.h"
#include "sg/SGUtilities.h"

namespace bpt_bones {

using namespace bpt;

uint32_t TextHash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
    return h;
}
uint32_t FloatsHash(const float *f, int n)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; ++i) h = (h ^ FloatBits(f[i])) * 16777619u;
    return h;
}

uint32_t gScriptSeed;
uint32_t Pick()
{
    uint32_t x = gScriptSeed ^ (static_cast<uint32_t>(gEvents.size()) * 0x9e3779b9u);
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

const char *const kNames[] = {"spine", "head", "a_bone_name_longer_than_fifteen", "", "Head"};

// ---- the bone adjust records ----
struct Bone {
    const void *vtable;
    std::string name;
    uint32_t field;
    float translation[3];
    float rotation[4];
};
static_assert(sizeof(Bone) == 0x3c && offsetof(Bone, translation) == 0x20, "bone");
Bone gBones[6];
std::array<const void *, 8> gBoneVt;
const char *__fastcall BoneName(Bone *self, void *)
{
    gEvents.push_back({0x7005, static_cast<uint32_t>(self - gBones)});
    return self->name.c_str();
}

uint8_t gEntry[0x40];

uint8_t *__fastcall F_cf04b0(void *self, void *, uint32_t a, uint32_t b, uint32_t id, uint32_t partId)
{
    gEvents.push_back({0xcf04b0, Rel(self), a, b, id, partId});
    return gEntry;
}

// ---- nodes and extensions ----
struct Node {
    const void *vtable;
} gNodes[3];
std::array<const void *, 70> gNodeVt;
void __fastcall NodeSet64(Node *self, void *, const float *t) { gEvents.push_back({0x8040, static_cast<uint32_t>(self - gNodes), FloatsHash(t, 3)}); }
void __fastcall NodeSet65(Node *self, void *, const float *r) { gEvents.push_back({0x8041, static_cast<uint32_t>(self - gNodes), FloatsHash(r, 4)}); }

struct Ext {
    const void *vtable;
    const int32_t *vbptr;
    const void *baseVtable; // the virtual base (the created one's)
};
Ext gExts[5];
Ext gCreated;
int gExtCount;
bool gTagOk[5];
const char *gTags[5];
std::array<const void *, 24> gExtVt, gBaseVt;
const int32_t kVbTable[2] = {-4, 4}; // the base at vbptr + 4 = object + 8

uint32_t ExtIndex(const void *p)
{
    if (p == nullptr) return 0xffu;
    if (p == &gCreated) return 100;
    return static_cast<uint32_t>(static_cast<const Ext *>(p) - gExts);
}
uint8_t __fastcall ExtTag(Ext *self, void *, const char *name, cRZString *out)
{
    const uint32_t k = ExtIndex(self);
    gEvents.push_back({0x9012, k, TextHash(name)});
    if (k >= 5) return 0;
    out->mString.assign(gTags[k]);
    return gTagOk[k] ? 1 : 0;
}
void __fastcall ExtSet5(Ext *self, void *, const char *name, const float *t)
{
    gEvents.push_back({0x9005, ExtIndex(self), TextHash(name), FloatsHash(t, 3)});
}
void __fastcall ExtSet7(Ext *self, void *, const char *name, const float *r)
{
    gEvents.push_back({0x9007, ExtIndex(self), TextHash(name), FloatsHash(r, 4)});
}
void __fastcall ExtSetType(Ext *self, void *, const char *name) { gEvents.push_back({0x9000, ExtIndex(self), TextHash(name)}); }
void __fastcall ExtSet6(Ext *self, void *, const char *name, const char *value)
{
    gEvents.push_back({0x9006, ExtIndex(self), TextHash(name), TextHash(value)});
}
uint32_t __fastcall BaseRelease(void *self, void *)
{
    gEvents.push_back({0x9102, static_cast<uint32_t>(static_cast<uint8_t *>(self) - reinterpret_cast<uint8_t *>(&gCreated))});
    return 1;
}

void *__cdecl F_ae7490(const char *name, void *target, uint32_t flags)
{
    gEvents.push_back({0xae7490, TextHash(name), static_cast<uint32_t>(target != nullptr), flags});
    const uint32_t p = Pick();
    return p % 4 == 0 ? nullptr : &gNodes[p % 3];
}
void *__cdecl F_aeb5b0(void *target, const char *type, void *after)
{
    gEvents.push_back({0xaeb5b0, static_cast<uint32_t>(target != nullptr), TextHash(type), ExtIndex(after)});
    const int next = after == nullptr ? 0 : static_cast<int>(ExtIndex(after)) + 1;
    return next < gExtCount ? &gExts[next] : nullptr;
}

// ---- the target and the framework ----
struct Fake {
    const void *const *vtable;
};
std::array<const void *, 24> gTargetVt, gFrameworkVt;
Fake gTarget, gFramework;
void __fastcall TargetAdd(Fake *, void *, void *child)
{
    gEvents.push_back({0xa022, static_cast<uint32_t>(static_cast<uint8_t *>(child) - reinterpret_cast<uint8_t *>(&gCreated))});
}
uint8_t __fastcall FrameworkCreate(Fake *, void *, uint32_t cls, uint32_t iid, void **out)
{
    gEvents.push_back({0xb003, cls, iid, static_cast<uint32_t>(*out == nullptr)});
    *out = &gCreated;
    return 1;
}
void *__cdecl StandFramework()
{
    gEvents.push_back({0x1ae18d});
    return &gFramework;
}

void *__cdecl ImageNew(size_t size) { return ::operator new(size); }
void __cdecl ImageSizedDelete(void *p, size_t size) { ::operator delete(p, size); }

float RandomFloat(std::mt19937 &rng) { return (static_cast<float>(rng() % 20001) - 10000.0f) / 97.0f; }

} // namespace bpt_bones

DIFF_TEST(cTSCASBodyPartSystem_ApplyBoneAdjusts, 0xce0e60)
{
    using namespace bpt_bones;
    std::mt19937 rng(0xce0e60);
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, void *)>(0xce0e60);
    const VTable vt = CopyVTable(ctx);
    Env env(ctx);
    Redirect r1(ctx, 0xcf04b0, reinterpret_cast<const void *>(&F_cf04b0));
    shape::StandIn r2(0xae7490, &nGZSceneGraph::FindNodeByName, &F_ae7490);
    shape::StandIn r3(0xaeb5b0, &nGZSceneGraph::FindDataList, &F_aeb5b0);
    Redirect newStand(ctx, 0x824632, reinterpret_cast<const void *>(&ImageNew));
    Redirect deleteStand(ctx, 0x824624, reinterpret_cast<const void *>(&ImageSizedDelete));
    shape::StandIn framework(0x1ae18d, &GetDirectorFrameworkObject, &StandFramework);
    gBoneVt = {};
    gBoneVt[5] = reinterpret_cast<const void *>(&BoneName);
    gNodeVt = {};
    gNodeVt[64] = reinterpret_cast<const void *>(&NodeSet64);
    gNodeVt[65] = reinterpret_cast<const void *>(&NodeSet65);
    for (auto &n : gNodes) n.vtable = gNodeVt.data();
    gExtVt = {};
    gExtVt[0] = reinterpret_cast<const void *>(&ExtSetType);
    gExtVt[5] = reinterpret_cast<const void *>(&ExtSet5);
    gExtVt[6] = reinterpret_cast<const void *>(&ExtSet6);
    gExtVt[7] = reinterpret_cast<const void *>(&ExtSet7);
    gExtVt[18] = reinterpret_cast<const void *>(&ExtTag);
    gBaseVt = {};
    gBaseVt[2] = reinterpret_cast<const void *>(&BaseRelease);
    for (auto &e : gExts) e = {gExtVt.data(), kVbTable, gBaseVt.data()};
    gCreated = {gExtVt.data(), kVbTable, gBaseVt.data()};
    gTargetVt = {};
    gTargetVt[22] = reinterpret_cast<const void *>(&TargetAdd);
    gTarget.vtable = gTargetVt.data();
    gFrameworkVt = {};
    gFrameworkVt[3] = reinterpret_cast<const void *>(&FrameworkCreate);
    gFramework.vtable = gFrameworkVt.data();
    gEvents.reserve(512);
    for (auto &b : gBones) b.vtable = gBoneVt.data();
    for (int i = 0; i < 3000; ++i) {
        gScriptSeed = static_cast<uint32_t>(rng());
        const int bones = i < 12 ? i % 6 : static_cast<int>(rng() % 6); // 0..5
        for (int k = 0; k < 6; ++k) {
            Bone &b = gBones[k];
            b.name = kNames[rng() % 5];
            b.field = static_cast<uint32_t>(rng());
            for (float &f : b.translation) f = RandomFloat(rng);
            for (float &f : b.rotation) f = RandomFloat(rng);
        }
        for (auto &x : gEntry) x = static_cast<uint8_t>(rng());
        Bone *first = gBones, *last = gBones + bones;
        std::memcpy(gEntry + 0x18, &first, 4);
        std::memcpy(gEntry + 0x1c, &last, 4);
        gExtCount = i < 10 ? i % 5 : static_cast<int>(rng() % 5); // 0..4
        for (int k = 0; k < 5; ++k) {
            gTagOk[k] = rng() % 4 != 0;
            gTags[k] = rng() % 3 == 0 ? "other_tag_name_for_the_heap" : kNames[rng() % 5];
        }
        const uint32_t kind = rng() % 3 == 0 ? 7 : static_cast<uint32_t>(rng() % 9);
        const uint32_t key = static_cast<uint32_t>(rng());
        const uint32_t id = rng() % 2 ? 0 : static_cast<uint32_t>(rng() % 5);
        void *const target = rng() % 10 == 0 ? nullptr : &gTarget;
        Scenario sc;
        for (auto &b : sc.bytes) b = static_cast<uint8_t>(rng());
        Compare(ctx, "ApplyBoneAdjusts", sc, vt, [&](World &w, bool original) -> uint64_t {
            return original ? orig(w.S(), kind, key, id, target) : w.S()->ApplyBoneAdjusts(kind, key, id, target);
        });
    }
}
