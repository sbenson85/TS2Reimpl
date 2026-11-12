// cTSModelEffect (src/ts/cTSModelEffect.h), lane XD: original vs ours.
//
// Mode 0 builds the effect with the original Create 0xc06c00, mode 1 with ours; every slot then runs through the object's own
// vtable, so mode 0 runs the image's bodies and thunks, and mode 1 ours. After each step: the result, the log of every fake's
// calls (every call, with the ids and the words it was given: colours as bits, names as hashes), both vtable words and every
// field. At the end the destructor through the +4 face's deleting slot (flags 0 leaves the block to compare, flags 1 frees
// it), with the frees by size, and the reference counts of everything the world handed out.
//
// Fakes (sizes and behaviour from the callees, not from our structs): the director framework's class factory (the group node,
// the shape-reference node and the shape are told apart by class id), the scene-graph nodes (tree walkers: TestFlag, children
// and siblings, layers, attributes, flags, Clone), the resource service and its manager (Load), the key parser, and the
// callees of the primitive path (the box, sphere and cone makers 0xaef5b0 / 0xaf05b0 / 0xaef950 (ours, verified) and 0xaef310
// and 0xc05be0, tested in ts_model_effect_parts.cpp, stood in on both sides; the material handles 0xae8820 / 0xae8870 run
// for real on each side against a fake material manager, GetCachedQueriedObject 0xae8900 stood in on both sides).
//
// The primitive path's cache (the original's global 0x10fd3cc, ours a never-freed table) cannot be reset, so its test runs one
// sequence per side. Names are always heap strings: the original's key is made from the name's raw bytes (see
// ORIGINAL-BUG in src/ts/cTSModelEffect.cpp), the first four read as a pointer, which a heap string holds validly.
//
// Covered RVAs: 0xc06c00 (Create), 0xc05970 (constructor), 0xc05a10 (destructor), 0xc05ab0 (deleting destructor), 0xc05a9c (its
// +4 thunk), 0xc05cf0 (Attach, both paths), 0xc06b80 (Detach), 0xc06710 (HandleEvent), 0xc069c0 (SetFlagged), 0xc06550
// (SetModel), and the tree walkers 0xc05b10, 0xc06630, 0xc066a0 through them; the bases' folded slots through each side's vtable.
#include "ts_effects_collection.h"

#include "free_recorder.h"
#include "game/game.h"
#include "gz/cVector4fEvent.h"
#include "misc/UtilitiesE.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/StringKeyParser.h"
#include "sg/SGNodeInterfaces.h"
#include "sg/SGUtilities.h"
#include "shape.h"
#include "ts/cTSCompDescription.h"
#include "ts/cTSModelEffect.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace fxtest;

constexpr uint32_t kGameTables[2] = {0xe9139c, 0xe913dc};
constexpr uint32_t kGameBaseTables[2] = {0xe8bd68, 0xda2fa8}; // the base's +0, then the count's own after its destructor

using CreateFn = void *(__cdecl *)(const void *);
using ConstructFn = void *(__thiscall *)(void *, const void *);

enum : uint32_t {
    evClass = 0xc1000000,
    evAddShape,
    evAddPart,
    evBind,
    evLoad,
    evParse,
    evQuery,
    evClone,
    evTest,
    evChildren,
    evNext,
    evLayer,
    evUnlayer,
    evAttr,
    evAddChild2,
    evSetFlag2,
    evAxes,
    evBox,
    evSphere,
    evCone,
    evMatId,
    evHandle,
    evHandleEnd,
    evService,
};

// What the runs reached, for the floors (a side's totals).
enum Hit : unsigned {
    hLoadOk,
    hLoadFail,
    hCloneOk,
    hCloneNull,
    hAttr,
    hLayer,
    hUnlayer,
    hFlag,
    hAxes,
    hBox,
    hSphere,
    hCone,
    hAxesNullPart,
    hBind,
    hAddShape,
    hTested,
    hCount
};
uint32_t gHit[hCount];

uint32_t Hash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ uint8_t(*s)) * 16777619u;
    return h;
}

// ---- the fake scene ----
// The first four words are FakeNode's (the shared reference-count slots use them); every node is told by its table.
struct MNode {
    void *const *vt;
    uint32_t id, refs;
    MNode *parent;
    MNode *first, *last, *next;
    uint32_t flags; // bit n answers TestFlag(n); bit 4 is "renderable"
};

constexpr uint32_t kRenderable = 1u << 4;

void *gTreeVT[kFakeNodeSlots], *gRefVT[kFakeNodeSlots], *gShapeVT[kFakeNodeSlots], *gPartVT[kFakeNodeSlots];
void *gMgrVT[8], *gSvcVT[8];

struct World;
World *gW = nullptr;

struct World {
    static constexpr uint32_t kCap = 30000;
    FakeCom com;
    MNode nodes[kCap];
    uint32_t used = 0;
    FakeOwner owners[3];
    MNode svc, mgr;
    bool loadOk = true, cloneOk = true, partOk = true;
    uint32_t matId = 0x40;
    std::mt19937 rng;

    explicit World(uint32_t seed) : rng(seed * 2654435761u + 11)
    {
        com.vt = nullptr; // set by Init
        std::memset(nodes, 0, sizeof(nodes));
        for (uint32_t i = 0; i < 3; ++i) owners[i] = FakeOwner{gFakeOwnerVT, 0x20 + i, 0, nullptr, 0};
        svc = MNode{gSvcVT, 0x7e, 1, nullptr, nullptr, nullptr, nullptr, 0};
        mgr = MNode{gMgrVT, 0x7f, 1, nullptr, nullptr, nullptr, nullptr, 0};
    }
    MNode *Make(void *const *vt, uint32_t flags)
    {
        MNode *n = &nodes[used < kCap ? used : kCap - 1];
        ++used;
        *n = MNode{vt, used, 0, nullptr, nullptr, nullptr, nullptr, flags};
        return n;
    }
    MNode *MakePart()
    {
        MNode *p = Make(gPartVT, 0);
        p->refs = 1;
        return p;
    }
    static void Append(MNode *n, MNode *child)
    {
        child->parent = n;
        child->next = nullptr;
        if (!n->first) n->first = child;
        else n->last->next = child;
        n->last = child;
    }
    // A model's copy: a few nodes, each renderable by chance, down to `depth` levels.
    MNode *MakeTree(unsigned depth)
    {
        MNode *n = Make(gTreeVT, (rng() % 2) ? kRenderable : 0);
        const unsigned kids = depth ? rng() % 4 : 0;
        for (unsigned i = 0; i < kids; ++i) Append(n, MakeTree(depth - 1));
        return n;
    }
};

uint32_t __fastcall MSetFlag(MNode *n, void *, uint32_t bit, bool value)
{
    Ev({evSetFlag2, n->id, bit, uint32_t(value)});
    ++gHit[hFlag];
    return 0;
}
uint8_t __fastcall MTestFlag(MNode *n, void *, uint32_t bit)
{
    const bool r = bit < 32 && ((n->flags >> bit) & 1);
    Ev({evTest, n->id, bit, uint32_t(r)});
    ++gHit[hTested];
    return r;
}
MNode *__fastcall MNext(MNode *n, void *)
{
    Ev({evNext, n->id});
    return n->next;
}
uint32_t *__fastcall MChildren(MNode *n, void *, uint32_t *out)
{
    Ev({evChildren, n->id});
    out[0] = out[1] = uint32_t(uintptr_t(n->first));
    out[2] = 0;
    return out;
}
void __fastcall MAddChild(MNode *n, void *, MNode *child)
{
    Ev({evAddChild2, n->id, child->id});
    World::Append(n, child);
}
void __fastcall MLayer(MNode *n, void *, const char *name)
{
    Ev({evLayer, n->id, Hash(name)});
    ++gHit[hLayer];
}
void __fastcall MUnlayer(MNode *n, void *, const char *name)
{
    Ev({evUnlayer, n->id, Hash(name)});
    ++gHit[hUnlayer];
}
// Slot 7 reads the event object's four floats after its first word (the vtable).
void __fastcall MSetAttribute(MNode *n, void *, uint32_t id, uint32_t index, const uint32_t *event)
{
    Ev({evAttr, n->id, id, index, event[1], event[2], event[3], event[4]});
    ++gHit[hAttr];
}
void __fastcall MClone(MNode *n, void *, uint32_t mode, uint32_t iid, void **out, void *context)
{
    World &w = *gW;
    Ev({evClone, n->id, mode, iid, uint32_t(context != nullptr), uint32_t(w.cloneOk)});
    if (!w.cloneOk) {
        ++gHit[hCloneNull];
        return;
    }
    ++gHit[hCloneOk];
    MNode *root = w.MakeTree(2);
    root->refs = 1;
    *out = root;
}
void __fastcall MAddShape(MNode *n, void *, MNode *shape)
{
    Ev({evAddShape, n->id, IdOf(shape)});
    ++gHit[hAddShape];
}
uint32_t __fastcall MBind(MNode *n, void *, const char *material, const char *subset)
{
    Ev({evBind, n->id, Hash(material), Hash(subset)});
    ++gHit[hBind];
    return 1;
}
uint32_t __fastcall MAddPart(MNode *n, void *, MNode *part, int32_t lod)
{
    Ev({evAddPart, n->id, IdOf(part), uint32_t(lod)});
    return 1;
}
uint32_t __fastcall MgrLoad(MNode *, void *, const uint32_t *key, uint32_t iid, void **out, uint32_t a, uint32_t b)
{
    World &w = *gW;
    Ev({evLoad, key[0], key[1], key[2], key[3], iid, a, b, uint32_t(w.loadOk)});
    if (!w.loadOk) {
        ++gHit[hLoadFail];
        return 0;
    }
    ++gHit[hLoadOk];
    MNode *model = w.Make(gTreeVT, 0);
    model->refs = 1;
    *out = model;
    return 1;
}
uint32_t __fastcall SvcQuery(MNode *, void *, uint32_t a, uint32_t b, void **out)
{
    World &w = *gW;
    Ev({evQuery, a, b});
    ++w.mgr.refs;
    *out = &w.mgr;
    return 1;
}

void InitTables()
{
    static bool done = false;
    if (done) return;
    done = true;
    std::memcpy(gTreeVT, gFakeNodeVT, sizeof(gTreeVT));
    gTreeVT[7] = reinterpret_cast<void *>(&MSetAttribute);
    gTreeVT[15] = reinterpret_cast<void *>(&MClone);
    gTreeVT[29] = reinterpret_cast<void *>(&MSetFlag);
    gTreeVT[38] = reinterpret_cast<void *>(&MNext);
    gTreeVT[42] = reinterpret_cast<void *>(&MChildren);
    gTreeVT[45] = reinterpret_cast<void *>(&MAddChild);
    gTreeVT[48] = reinterpret_cast<void *>(&MTestFlag);
    gTreeVT[77] = reinterpret_cast<void *>(&MLayer);
    gTreeVT[79] = reinterpret_cast<void *>(&MUnlayer);
    std::memcpy(gRefVT, gTreeVT, sizeof(gRefVT));
    gRefVT[94] = reinterpret_cast<void *>(&MAddShape);
    std::memcpy(gShapeVT, gFakeNodeVT, sizeof(gShapeVT));
    gShapeVT[34] = reinterpret_cast<void *>(&MBind);
    gShapeVT[41] = reinterpret_cast<void *>(&MAddPart);
    std::memcpy(gPartVT, gFakeNodeVT, sizeof(gPartVT));
    std::memcpy(gMgrVT, gFakeNodeVT, sizeof(gMgrVT));
    gMgrVT[3] = reinterpret_cast<void *>(&MgrLoad);
    std::memcpy(gSvcVT, gFakeNodeVT, sizeof(gSvcVT));
    gSvcVT[5] = reinterpret_cast<void *>(&SvcQuery);
}

// The class factory: the three classes the effect asks for, each with one reference.
uint32_t __fastcall ComGet(FakeCom *, void *, uint32_t cls, uint32_t iid, void **out)
{
    World &w = *gW;
    MNode *n = nullptr;
    if (cls == 0x65246462) n = w.Make(gTreeVT, 0);
    else if (cls == 0x65245517) n = w.Make(gRefVT, kRenderable);
    else if (cls == 0xfc6eb1f7) n = w.Make(gShapeVT, 0);
    Ev({evClass, cls, iid, IdOf(n)});
    if (!n) return 0;
    n->refs = 1;
    *out = n;
    return 1;
}
void *const kComVT[8] = {reinterpret_cast<void *>(&ComOther<0>), reinterpret_cast<void *>(&ComOther<1>),
                         reinterpret_cast<void *>(&ComOther<2>), reinterpret_cast<void *>(&ComGet),
                         reinterpret_cast<void *>(&ComOther<4>), reinterpret_cast<void *>(&ComOther<5>),
                         reinterpret_cast<void *>(&ComOther<6>), reinterpret_cast<void *>(&ComOther<7>)};

void *StandCom()
{
    Ev({evFramework});
    return &gW->com;
}
void *StandService()
{
    Ev({evService});
    return &gW->svc;
}
bool __cdecl StandParse(StringKeyParser::Result *out, const char *key, uint32_t preset, const char *ext)
{
    const uint32_t h = Hash(key);
    Ev({evParse, h, preset, uint32_t(ext != nullptr), out->value, out->id, out->hash24, out->crc32});
    *out = StringKeyParser::Result{preset, h, h * 3u, h ^ 0x5a5a5a5au};
    return true;
}

// The original-only callees (cdecl, the part through the out pointer with one reference, as 0xaef310's own code leaves it).
void **__cdecl StandAxes(void **out, float f)
{
    Ev({evAxes, Bits(f)});
    ++gHit[hAxes];
    if (!gW->partOk) ++gHit[hAxesNullPart];
    *out = gW->partOk ? gW->MakePart() : nullptr;
    return out;
}
void **__cdecl StandBox(void **out, const float *lo, const float *hi)
{
    Ev({evBox, Bits(lo[0]), Bits(lo[1]), Bits(lo[2]), Bits(hi[0]), Bits(hi[1]), Bits(hi[2])});
    ++gHit[hBox];
    *out = gW->partOk ? gW->MakePart() : nullptr;
    return out;
}
void **__cdecl StandSphere(void **out, float r, int segments, const char *subset, int flag)
{
    Ev({evSphere, Bits(r), uint32_t(segments), Hash(subset), uint32_t(flag)});
    ++gHit[hSphere];
    *out = gW->partOk ? gW->MakePart() : nullptr;
    return out;
}
void **__cdecl StandCone(void **out, float height, float diameter, int a, int b)
{
    Ev({evCone, Bits(height), Bits(diameter), uint32_t(a), uint32_t(b)});
    ++gHit[hCone];
    *out = gW->partOk ? gW->MakePart() : nullptr;
    return out;
}
uint32_t __cdecl StandMatId()
{
    Ev({evMatId, gW->matId});
    return gW->matId++;
}
// The material manager the handles (0xae8820 / 0xae8870 and the cache's assignment) ask for: its slots 20 (AddRefIndex) and
// 21 (ReleaseIndex) log the index.
void *gMatVT[kFakeNodeSlots];
MNode gMat;
void __fastcall MatAddRef(MNode *, void *, uint32_t id) { Ev({evHandle, id}); }
void __fastcall MatRelease(MNode *, void *, uint32_t id) { Ev({evHandleEnd, id}); }
void *StandMatMgr()
{
    std::memcpy(gMatVT, gFakeNodeVT, sizeof(gMatVT));
    gMatVT[20] = reinterpret_cast<void *>(&MatAddRef);
    gMatVT[21] = reinterpret_cast<void *>(&MatRelease);
    gMat = MNode{gMatVT, 0x7d, 1, nullptr, nullptr, nullptr, nullptr, 0};
    return &gMat;
}

// Everything a test patches, for as long as it lives.
struct Stands {
    shape::StandIn framework{0x1ae18d, &GetDirectorFrameworkObject, &StandCom};
    shape::StandIn service{0x14323f, &GetServiceObject, &StandService};
    shape::StandIn parse{0xae6e60, &StringKeyParser::ParseKey, &StandParse};
    shape::StandIn axes{0xaef310u, &cTSModelEffect::MakeAxesPart, &StandAxes};
    shape::StandIn box{0xaef5b0u, &nGZSceneGraph::MakeBoxMesh, &StandBox};
    shape::StandIn sphere{0xaf05b0u, &nGZSceneGraph::MakeSphereMesh, &StandSphere};
    shape::StandIn cone{0xaef950u, &nGZSceneGraph::MakeConeMesh, &StandCone};
    shape::StandIn matId{0xc05be0u, &cTSModelEffect::GetWhiteMaterial, &StandMatId};
    shape::StandIn materials{0xae8900u, &GetCachedQueriedObject, &StandMatMgr};
};

void Prepare(World &w)
{
    InitTables();
    w.com.vt = kComVT;
    gW = &w;
    gEv.clear();
}

// The original keeps its shapes in a table built by a static initialiser the harness does not run.
void InitOriginalCache(difftest::Context &ctx)
{
    if (*GameAddress<uint32_t>(0x10fd3d0) != 0) return;
    testhelp::AtexitRedirect redirect(ctx);
    ctx.Original<void(__cdecl *)()>(0x1403f0)();
}

// ---- dumps ----
struct OurModelTables {
    EffectVt live, destroyed;
};
const OurModelTables &OurTables()
{
    static OurModelTables t = [] {
        OurModelTables v{};
        alignas(8) static uint8_t block[sizeof(cTSModelEffect)];
        ::new (block) cTSModelEffect(nullptr);
        v.live = EffectVt::Of(block);
        CallSlot<void *>(block + 4, 0, uint32_t(0));
        v.destroyed = EffectVt::Of(block);
        return v;
    }();
    return t;
}

uint32_t TableCode(uint32_t word)
{
    const OurModelTables &ours = OurTables();
    const unsigned live = shape::FaceOf(word, 2, shape::GameTables{kGameTables}, ours.live);
    if (live != ~0u) return shape::kFaceCode | live;
    const unsigned destroyed = shape::FaceOf(word, 2, shape::GameTables{kGameBaseTables}, ours.destroyed);
    if (destroyed != ~0u) return shape::kFaceCode | 0x10 | destroyed;
    return word;
}

void PushModel(Blob &b, const void *effect, const void *description)
{
    const auto *w = static_cast<const uint32_t *>(effect);
    b.push_back(TableCode(w[0]));
    b.push_back(TableCode(w[1]));
    PushNodeEffect(b, effect, description);
    for (int i = 7; i < 12; ++i) b.push_back(i == 7 ? IdOf(reinterpret_cast<const void *>(uintptr_t(w[i]))) : w[i]);
}

void Drain(Blob &b)
{
    b.insert(b.end(), gEv.begin(), gEv.end());
    gEv.clear();
}

void PushWorld(Blob &b, const World &w)
{
    for (uint32_t i = 0; i < w.used && i < World::kCap; ++i) b.push_back(w.nodes[i].refs);
    b.push_back(w.svc.refs);
    b.push_back(w.mgr.refs);
    for (const FakeOwner &o : w.owners) b.push_back(o.refs);
}

// A heap string (capacity past 15) holding `text`, whose bytes 4..11 (unused by a heap string, read as the "%f" argument by the
// original's key) are `raw`.
void FillName(std::string &s, const std::string &text, double raw)
{
    s.reserve(40);
    s.assign(text);
    std::memcpy(reinterpret_cast<char *>(&s) + 4, &raw, 8);
}

float Colour(std::mt19937 &r)
{
    switch (r() % 5) {
    case 0:
    case 1:
        return 1.0f;
    case 2:
        return 0.5f;
    case 3:
        return 2.0f;
    default:
        return testhelp::FewOrUniform(r, -2.0f, 2.0f);
    }
}
// A factor: exact ones, ones on both edges of the "nearly one" window (-32 .. 31 ulps) and random ones.
float Factor(std::mt19937 &r)
{
    switch (r() % 7) {
    case 0:
        return 1.0f;
    case 1:
        return 0.5f;
    case 2:
        return 2.0f;
    case 3:
    case 4:
        return std::bit_cast<float>(uint32_t(0x3f800000 + int(r() % 71) - 36));
    default:
        return testhelp::FewOrUniform(r, -2.0f, 2.0f);
    }
}

void FillDescription(cTSModelDescription &d, const std::string &name, double raw, std::mt19937 &r)
{
    FillName(d.mName, name, raw);
    const bool plain = r() % 2 == 0;
    d.mFloat20 = testhelp::FewOrUniform(r, 0.0f, 4.0f);
    d.mVector24.x = plain ? 1.0f : Colour(r);
    d.mVector24.y = plain ? 1.0f : Colour(r);
    d.mVector24.z = plain ? 1.0f : Colour(r);
    d.mFloat30 = plain ? 1.0f : Colour(r);
}

// The deleting destructor through the count's face (flags 1) or the +4 face's slot 0 (flags 0, the block left to read).
void Finish(Blob &b, void *e, cTSModelEffect *object, const void *description, uint32_t seed)
{
    const bool free = seed % 2 != 0;
    alignas(8) uint8_t after[sizeof(cTSModelEffect)];
    difftest::FreeRecorder rec;
    if (free) {
        object->mRefCount = 1;
        b.push_back(CallSlot<uint32_t>(e, 13));
    } else {
        b.push_back(uint32_t(CallSlot<void *>(static_cast<uint8_t *>(e) + 4, 0, uint32_t(0)) == e));
        std::memcpy(after, e, sizeof(after));
    }
    rec.Stop();
    Drain(b);
    size_t sizes[4] = {};
    const size_t freed = rec.SortedSizes(sizes, 4);
    b.push_back(uint32_t(freed));
    b.push_back(uint32_t(rec.Freed(e)));
    for (size_t i = 0; i < freed && i < 4; ++i) b.push_back(uint32_t(sizes[i]));
    if (!free) {
        const auto *words = reinterpret_cast<const uint32_t *>(after);
        b.push_back(TableCode(words[0]));
        b.push_back(TableCode(words[1]));
        // Word 4 is the running byte and three bytes of padding (ModelEffect_Construct compares the padding).
        for (int i = 2; i < 12; ++i)
            b.push_back(i == 3   ? uint32_t(words[i] - uint32_t(reinterpret_cast<uintptr_t>(description)))
                        : i == 4 ? words[i] & 0xff
                        : i >= 5 && i <= 7 ? IdOf(reinterpret_cast<const void *>(uintptr_t(words[i])))
                                 : words[i]);
        ::operator delete(e);
    }
    Drain(b);
}

} // namespace

DIFF_TEST(ModelEffect_Construct, 0xc05970)
{
    cTSModelDescription description;
    for (uint32_t seed = 0; seed < 40; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            Blob &b = blobs[mode];
            std::mt19937 r(seed + 1);
            alignas(8) uint8_t block[sizeof(cTSModelEffect)];
            for (uint8_t &x : block) x = uint8_t(r());
            void *e = mode ? static_cast<void *>(::new (block) cTSModelEffect(&description)) : ctx.Original<ConstructFn>(0xc05970)(block, &description);
            b.push_back(uint32_t(e == block));
            PushModel(b, block, &description);
            for (size_t i = 8; i < sizeof(block); ++i) b.push_back(block[i]); // the padding's garbage stays on both sides
            CallSlot<void *>(block + 4, 0, uint32_t(0));
        }
        Same(ctx, blobs[0], blobs[1], "ModelEffect_Construct", seed);
    }
}

DIFF_TEST(ModelEffect_Slots, 0xc06c00)
{
    static const char *const kModelNames[] = {"model_cres", "x_cres_tail_extra_text", "plain_unknown_name_long", "Upper_CRES_name_here_x", "_cres",
                                              "no_resource_suffix_at_all_here"};
    static const char *const kTexts[] = {"a_cres", "*!thing_cres", "", "k", "name!res_cres", "#12!x"};
    const CreateFn original = ctx.Original<CreateFn>(0xc06c00);
    InitOriginalCache(ctx);
    Stands stands;
    uint32_t totals[2][hCount] = {};
    uint32_t steps[2][4] = {};
    for (uint32_t seed = 0; seed < 400; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            auto world = std::make_unique<World>(seed);
            World &w = *world;
            Prepare(w);
            std::memset(gHit, 0, sizeof(gHit));
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 7919u + 3), rd(seed * 31u + 5);
            cTSModelDescription description;
            FillDescription(description, kModelNames[seed % (sizeof(kModelNames) / sizeof(kModelNames[0]))], 0.5, rd);
            void *e = mode ? static_cast<void *>(cTSModelEffect::Create(&description)) : original(&description);
            auto *object = static_cast<cTSModelEffect *>(static_cast<cITSComponentEffect *>(e));
            PushModel(b, e, &description);
            CallSlot<uint32_t>(static_cast<uint8_t *>(e) + 4, 2); // the caller's reference: the count starts at 0
            for (uint32_t step = 0; step < 40; ++step) {
                b.push_back(0x0b000000u | step);
                const uint32_t op = r() % 13;
                uint32_t result = 0xfeed;
                switch (op) {
                case 0:
                case 1:
                case 2: {
                    FakeOwner *owner = r() % 6 ? &w.owners[r() % 3] : nullptr;
                    w.loadOk = r() % 4 != 0;
                    w.cloneOk = r() % 5 != 0;
                    CallSlot<void>(e, 0, static_cast<void *>(owner));
                    ++steps[mode][0];
                    break;
                }
                case 3:
                    CallSlot<void>(e, 1);
                    break;
                case 4:
                case 5:
                case 6:
                case 7: {
                    const uint32_t kind = r() % 7;
                    float factors[3] = {Factor(r), Factor(r), Factor(r)};
                    b.push_back(kind);
                    result = CallSlot<uint8_t>(e, 7, kind, static_cast<void *>(factors), static_cast<void *>(nullptr));
                    ++steps[mode][kind == 3 ? 1 : kind == 4 ? 2 : 3];
                    break;
                }
                case 8:
                    CallSlot<void>(e, 9, bool(r() % 2));
                    break;
                case 9:
                case 10:
                    w.loadOk = r() % 4 != 0;
                    result = CallSlot<uint8_t>(e, 14, kTexts[r() % (sizeof(kTexts) / sizeof(kTexts[0]))]);
                    break;
                case 11: // the count through both faces; the caller's reference stays
                    result = CallSlot<uint32_t>(e, 12);
                    b.push_back(CallSlot<uint32_t>(e, 13));
                    b.push_back(CallSlot<uint32_t>(static_cast<uint8_t *>(e) + 4, 2));
                    b.push_back(CallSlot<uint32_t>(static_cast<uint8_t *>(e) + 4, 1));
                    break;
                default:
                    CallSlot<uint8_t>(e, 4);
                    break;
                }
                b.push_back(op);
                b.push_back(result);
                Drain(b);
                PushModel(b, e, &description);
                PushWorld(b, w);
            }
            b.push_back(0x0b0000ffu);
            Finish(b, e, object, &description, seed);
            PushWorld(b, w);
            for (unsigned i = 0; i < hCount; ++i) totals[mode][i] += gHit[i];
        }
        Same(ctx, blobs[0], blobs[1], "ModelEffect_Slots", seed);
    }
    // What the runs exercised: each branch the comparison covers, on both sides.
    static const char *const kHitNames[hCount] = {"Load ok", "Load failed", "Clone ok", "Clone null", "SetAttribute", "AddLayer", "RemoveLayer", "SetFlag",
                                                  "axes", "box", "sphere", "cone", "axes null part", "BindMaterial", "AddShape", "TestFlag"};
    static const uint32_t kFloor[hCount] = {400, 200, 400, 100, 2000, 2000, 60, 200, 0, 0, 0, 0, 0, 0, 0, 8000};
    for (int mode = 0; mode < 2; ++mode)
        for (unsigned i = 0; i < hCount; ++i)
            if (totals[mode][i] < kFloor[i]) ctx.Fail("ModelEffect_Slots: side %d reached %s only %u times (floor %u)", mode, kHitNames[i], totals[mode][i], kFloor[i]);
    for (int mode = 0; mode < 2; ++mode) {
        if (steps[mode][0] < 2000) ctx.Fail("ModelEffect_Slots: side %d attached only %u times", mode, steps[mode][0]);
        if (steps[mode][1] < 300 || steps[mode][2] < 300 || steps[mode][3] < 300)
            ctx.Fail("ModelEffect_Slots: side %d events %u / %u / %u (kind 3, 4, other)", mode, steps[mode][1], steps[mode][2], steps[mode][3]);
    }
}

// The primitive path through one sequence per side (the cache is the sequence's state).
DIFF_TEST(ModelEffect_Primitives, 0xc05cf0)
{
    static const char *const kPrimitives[] = {"axes", "cube", "sphere", "cone", "teapot_unknown_primitive"};
    static const double kRaw[] = {0.5, 1.0, 2.25, -3.0, 0.0, 123456.789};
    const CreateFn original = ctx.Original<CreateFn>(0xc06c00);
    InitOriginalCache(ctx);
    Stands stands;
    constexpr uint32_t kCases = 500;
    Blob blobs[2];
    uint32_t totals[2][hCount] = {};
    uint32_t hits[2] = {}, repeats[2] = {};
    for (int mode = 0; mode < 2; ++mode) {
        World *world = new World(99); // kept: the cache holds shapes of it to the end of the process
        World &w = *world;
        Prepare(w);
        std::memset(gHit, 0, sizeof(gHit));
        Blob &b = blobs[mode];
        std::mt19937 r(12345);
        std::vector<std::string> keys; // the keys the side's cache holds, as the original forms them
        std::vector<std::pair<std::string, double>> made;
        for (uint32_t c = 0; c < kCases; ++c) {
            b.push_back(0x0b000000u | c);
            cTSModelDescription description;
            std::string name;
            double raw = kRaw[r() % (sizeof(kRaw) / sizeof(kRaw[0]))];
            const uint32_t pick = r() % 10;
            bool expectHit = false;
            if (pick < 3 && !keys.empty()) {
                name = keys[r() % keys.size()]; // a name that is a key already: the cache answers
                expectHit = true;
            } else if (pick < 4 && !made.empty()) {
                name = made[r() % made.size()].first; // the same primitive again: the key is replaced
                raw = made[r() % made.size()].second;
                if (std::find_if(made.begin(), made.end(), [&](auto &m) { return m.first == name && m.second == raw; }) != made.end()) ++repeats[mode];
            } else {
                name = kPrimitives[r() % (sizeof(kPrimitives) / sizeof(kPrimitives[0]))];
            }
            FillDescription(description, name, raw, r);
            w.partOk = r() % 8 != 0;
            w.cloneOk = true;
            void *e = mode ? static_cast<void *>(cTSModelEffect::Create(&description)) : original(&description);
            auto *object = static_cast<cTSModelEffect *>(static_cast<cITSComponentEffect *>(e));
            FakeOwner *owner = &w.owners[r() % 3];
            const uint32_t before = w.used;
            CallSlot<void>(e, 0, static_cast<void *>(owner));
            b.push_back(w.used - before);
            Drain(b);
            PushModel(b, e, &description);
            // A key is made only by a path that stored a shape: a known primitive with a part.
            const bool stored = (name == "axes" || name == "cube" || name == "sphere" || name == "cone") && w.partOk;
            if (stored) {
                char text[96];
                std::snprintf(text, sizeof(text), "%s_%f", name.c_str(), raw);
                keys.push_back(text);
                made.emplace_back(name, raw);
            }
            (void)expectHit;
            CallSlot<void>(e, 1);
            Drain(b);
            const bool free = c % 2 != 0;
            if (free) {
                object->mRefCount = 1;
                CallSlot<uint32_t>(e, 13);
            } else {
                CallSlot<void *>(static_cast<uint8_t *>(e) + 4, 0, uint32_t(0));
                ::operator delete(e);
            }
            Drain(b);
            // The shapes the cache holds keep their references to the end: every node's count tells which were taken and let go.
            PushWorld(b, w);
        }
        for (unsigned i = 0; i < hCount; ++i) totals[mode][i] = gHit[i];
        hits[mode] = uint32_t(std::count(b.begin(), b.end(), evAddShape));
    }
    Same(ctx, blobs[0], blobs[1], "ModelEffect_Primitives", 0);
    for (int mode = 0; mode < 2; ++mode) {
        if (totals[mode][hAxes] < 20 || totals[mode][hBox] < 20 || totals[mode][hSphere] < 20 || totals[mode][hCone] < 20)
            ctx.Fail("ModelEffect_Primitives: side %d made axes %u, boxes %u, spheres %u, cones %u", mode, totals[mode][hAxes], totals[mode][hBox],
                     totals[mode][hSphere], totals[mode][hCone]);
        if (totals[mode][hAxesNullPart] < 3) ctx.Fail("ModelEffect_Primitives: side %d axes with no part only %u times", mode, totals[mode][hAxesNullPart]);
        if (totals[mode][hBind] < 100) ctx.Fail("ModelEffect_Primitives: side %d bound only %u materials", mode, totals[mode][hBind]);
        if (repeats[mode] < 2) ctx.Fail("ModelEffect_Primitives: side %d repeated a key only %u times", mode, repeats[mode]);
        // A cache hit adds a shape without making one: more shapes added than the primitives made.
        const uint32_t made = totals[mode][hAxes] + totals[mode][hBox] + totals[mode][hSphere] + totals[mode][hCone];
        if (totals[mode][hAddShape] < made + 15) ctx.Fail("ModelEffect_Primitives: side %d cache answered only %u times", mode, totals[mode][hAddShape] - made);
    }
    (void)hits;
}
