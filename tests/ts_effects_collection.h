#pragma once
// Shared pieces of the effects tests (tests/ts_effects_collection.cpp, tests/ts_effects_manager*.cpp): fake component
// descriptions with logged reference counts, random effects, and a dump of a cTSEffectsCollection's state (every container
// through its public interface: list order, buckets as node indices, capacities), read the same way on a game-built object
// (the layouts are the game's own standard-library types).
//
// The effects world (lane WO, for rows E1 onward): EffectsWorld holds what a component effect (src/ts/cTSEffectBase.h) talks
// to, with every call logged by the fake's id (the same on both sides), and EffectStep runs one random slot of the 14 on an
// effect through its own vtable. Tables are writable: a test overrides a slot (gFakeNodeVT[slot] = ...) for what its effect
// asks for; a slot nobody faked logs its number and returns 0, and it pops no arguments, so calling one with arguments
// crashes the run rather than passing.
// fakes: cIGZCOM (the director framework, GetClassObject), cITransformNode (scene-graph nodes), the effect owner
// (cTSVisualEffect's +4 face: slots 1, 2, 23, 42), cITSComponentDescription
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "rz/StaticGetters.h"
#include "test_helpers.h"
#include "ts/cTSEffectBase.h"
#include "ts/cTSEffectsCollection.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace fxtest {

using Blob = std::vector<uint32_t>;

inline Blob gEv;
inline void Ev(std::initializer_list<uint32_t> words) { gEv.insert(gEv.end(), words); }

enum : uint32_t { evGetType = 0xe1000000, evDescAddRef = 0xe2000000, evDescRelease = 0xe3000000, evDescOther = 0xe4000000 };

// ---- fake descriptions: slot 0 the type, 7 / 8 the reference count, every call logged with the fake's id ----
struct FakeDescription {
    void *const *vt;
    uint32_t id;
    int32_t type;
    uint32_t refs;
};
inline int32_t __fastcall FdGetType(FakeDescription *d, void *)
{
    Ev({evGetType | d->id});
    return d->type;
}
inline uint32_t __fastcall FdAddRef(FakeDescription *d, void *)
{
    Ev({evDescAddRef | d->id});
    return ++d->refs;
}
inline uint32_t __fastcall FdRelease(FakeDescription *d, void *)
{
    Ev({evDescRelease | d->id});
    return --d->refs;
}
inline uint32_t __fastcall FdOther(FakeDescription *d, void *)
{
    Ev({evDescOther | d->id});
    return 0;
}
inline void *const kFakeDescriptionVT[9] = {
    reinterpret_cast<void *>(&FdGetType), reinterpret_cast<void *>(&FdOther), reinterpret_cast<void *>(&FdOther),
    reinterpret_cast<void *>(&FdOther),   reinterpret_cast<void *>(&FdOther), reinterpret_cast<void *>(&FdOther),
    reinterpret_cast<void *>(&FdOther),   reinterpret_cast<void *>(&FdAddRef), reinterpret_cast<void *>(&FdRelease)};

// A side's descriptions (the same ids and types on both sides; each side counts its own references).
struct Descriptions;
inline const Descriptions *gLive[4];
struct Descriptions {
    FakeDescription d[16];
    Descriptions()
    {
        for (const Descriptions *&slot : gLive)
            if (!slot) {
                slot = this;
                break;
            }
    }
    ~Descriptions()
    {
        for (const Descriptions *&slot : gLive)
            if (slot == this) slot = nullptr;
    }
    Descriptions(const Descriptions &) = delete;
    void Build(uint32_t seed)
    {
        std::mt19937 r(seed * 31u + 7);
        for (uint32_t i = 0; i < 16; ++i) d[i] = {kFakeDescriptionVT, i + 1, int32_t(1 + r() % 13), 100};
        d[0].type = 1; // the first table always has one (index arithmetic at its edge)
    }
    cITSComponentDescription *At(uint32_t i) { return reinterpret_cast<cITSComponentDescription *>(&d[i % 16]); }
};
// A description pointer as an id (0 for null, the fake's id for a pointer into one of the live Descriptions, else the raw
// word). Descriptions register themselves (at most 4 at a time) so that no other pointer is read.
inline uint32_t DescId(const void *p)
{
    if (!p) return 0;
    for (const Descriptions *d : gLive)
        if (d && p >= static_cast<const void *>(d->d) && p < static_cast<const void *>(d->d + 16))
            return 0xd0000000u | static_cast<const FakeDescription *>(p)->id;
    return uint32_t(uintptr_t(p));
}

// ---- random inputs ----
inline const char *const kNames[] = {
    "smoke", "Smoke", "SMOKE", "fire_a", "Fire_A", "", "x", "a_long_effect_name_past_sixteen", "A_Long_Effect_Name_Past_Sixteen",
    "water.drop", "spark", "SPARK2", "exactly15chars_", "Exactly16chars__", "mixed Case 1", "\xc4MBRE\xe9"};
constexpr unsigned kNameCount = sizeof(kNames) / sizeof(kNames[0]);
inline const char *Name(std::mt19937 &r) { return kNames[r() % kNameCount]; }

inline void FillComponent(EffectComponent &c, std::mt19937 &r)
{
    c.mName = Name(r);
    const uint32_t t = r() % 8;
    c.mType = t == 0 ? 0 : t == 1 ? -1 : int32_t(1 + r() % 13);
    uint8_t *raw = reinterpret_cast<uint8_t *>(&c) + 0x1c; // every byte past the type, padding too
    for (size_t i = 0; i < 0x68; ++i) raw[i] = uint8_t(r());
    c.mIndex = r() % 4 ? -1 - int32_t(r() % 3) : int32_t(r() % 5);
}
inline EffectEntry RandomEffect(std::mt19937 &r)
{
    EffectEntry e;
    e.mFlags = r();
    e.mVariant = uint16_t(r() % 3 ? r() % 3 : r());
    for (uint32_t &p : e.mParams) p = r();
    const unsigned n = r() % 4;
    for (unsigned i = 0; i < n; ++i) {
        EffectComponent c;
        FillComponent(c, r);
        e.mComponents.push_back(c);
    }
    return e;
}

// ---- dumps ----
inline void PushString(Blob &b, const std::string &s)
{
    b.push_back(uint32_t(s.size()));
    b.push_back(uint32_t(s.capacity()));
    for (size_t i = 0; i < s.size(); i += 4) {
        uint32_t w = 0;
        std::memcpy(&w, s.data() + i, s.size() - i < 4 ? s.size() - i : 4);
        b.push_back(w);
    }
}

// An unordered container: size, buckets, load factor, the nodes in list order (`value` pushes each), and each bucket's first
// and last node as list positions (empty buckets as ~0).
template <class Map, class Value> void PushHash(Blob &b, const Map &m, Value value)
{
    b.push_back(uint32_t(m.size()));
    b.push_back(uint32_t(m.bucket_count()));
    float lf = m.max_load_factor();
    uint32_t w;
    std::memcpy(&w, &lf, 4);
    b.push_back(w);
    std::vector<const void *> order;
    for (auto it = m.begin(); it != m.end(); ++it) {
        order.push_back(&*it);
        value(*it);
    }
    auto pos = [&](const void *p) {
        for (size_t i = 0; i < order.size(); ++i)
            if (order[i] == p) return uint32_t(i);
        return 0xfffffffeu;
    };
    for (size_t n = 0; n < m.bucket_count(); ++n) {
        if (m.bucket_size(n) == 0) {
            b.push_back(~0u);
            continue;
        }
        auto first = m.begin(n);
        auto last = first;
        for (auto it = first; it != m.end(n); ++it) last = it;
        b.push_back(pos(&*first));
        b.push_back(pos(&*last));
    }
}

inline void PushEffect(Blob &b, const EffectEntry &e)
{
    b.push_back(e.mFlags);
    b.push_back(e.mVariant);
    b.insert(b.end(), std::begin(e.mParams), std::end(e.mParams));
    b.push_back(uint32_t(e.mComponents.size()));
    b.push_back(uint32_t(e.mComponents.capacity()));
    for (const EffectComponent &c : e.mComponents) {
        PushString(b, c.mName);
        // Every field; the padding at +0x5a (never copied by the original's assignments) is left out.
        b.push_back(uint32_t(c.mType));
        // +0x21 is left out: a copy construction leaves it as the fresh element's memory had it.
        b.insert(b.end(), {c.mWord1c, uint32_t(c.mFlags20.mByte) | uint32_t(c.mFlags20.mCounter) << 16});
        b.insert(b.end(), std::begin(c.mWords24), std::end(c.mWords24));
        b.insert(b.end(), std::begin(c.mWords50), std::end(c.mWords50));
        b.push_back(uint32_t(c.mByte58) | uint32_t(c.mByte59) << 8);
        b.insert(b.end(), std::begin(c.mWords5c), std::end(c.mWords5c));
        b.push_back(uint32_t(c.mHalf7c) | uint32_t(c.mHalf7e) << 16);
        b.push_back(c.mWord80);
        b.push_back(uint32_t(c.mIndex));
    }
}

// Everything but the vtable word.
inline void PushCollection(Blob &b, const cTSEffectsCollection *c)
{
    b.push_back(c->mRefCount);
    PushHash(b, c->mEffectsByName, [&](const auto &kv) {
        PushString(b, kv.first);
        b.push_back(kv.second);
    });
    b.push_back(uint32_t(c->mEffects.size()));
    b.push_back(uint32_t(c->mEffects.capacity()));
    for (const EffectEntry &e : c->mEffects) PushEffect(b, e);
    b.push_back(uint32_t(c->mTables.size()));
    b.push_back(uint32_t(c->mTables.capacity()));
    for (const EffectDescriptionTable &t : c->mTables) {
        PushHash(b, t.mNames, [&](const auto &kv) {
            PushString(b, kv.first);
            b.push_back(kv.second);
        });
        b.push_back(uint32_t(t.mDescriptions.size()));
        b.push_back(uint32_t(t.mDescriptions.capacity()));
        for (const EffectDescriptionRef &d : t.mDescriptions) b.push_back(DescId(d.Get()));
    }
    b.push_back(uint32_t(c->mMessageAliases.size()));
    b.push_back(uint32_t(c->mMessageAliases.capacity()));
    for (const EffectMessageAlias &a : c->mMessageAliases) {
        PushString(b, a.mEffect);
        b.push_back(a.mFirst);
        b.push_back(a.mSecond);
    }
    b.push_back(uint32_t(c->mMessageTriggers.size()));
    b.push_back(uint32_t(c->mMessageTriggers.capacity()));
    for (const EffectMessageTrigger &t : c->mMessageTriggers) {
        b.push_back(t.mMessage);
        PushString(b, t.mEffect);
    }
    b.push_back(*reinterpret_cast<const uint8_t *>(&c->mFlag58));
    PushString(b, c->mScratch);
}

inline void Same(difftest::Context &ctx, const Blob &a, const Blob &b, const char *what, uint32_t seed)
{
    ++ctx.cases;
    if (a == b) return;
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    // The last step marker (0x0bxxxxxx) before the difference and how far after it.
    size_t m = i;
    while (m > 0 && (a[m - 1] & 0xffffff00u) != 0x0b000000u) --m;
    ctx.Fail("%s seed %u: differ at word %u (sizes %u/%u): %08x vs %08x (marker %08x, +%u)", what, seed, unsigned(i),
             unsigned(a.size()), unsigned(b.size()), i < a.size() ? a[i] : 0, i < b.size() ? b[i] : 0,
             m > 0 ? a[m - 1] : 0, unsigned(i - m));
    if (std::getenv("FX_DUMP")) {
        for (size_t k = i > m + 24 ? i - 24 : m; k < i + 4 && k < a.size() && k < b.size(); ++k)
            std::printf("  [%u] %08x %08x\n", unsigned(k - m), a[k], b[k]);
    }
}

// Slot `slot` of the object's own vtable at `face`, called with this = face.
template <class R, class... A> R CallSlot(const void *face, int slot, A... args)
{
    using Fn = R(__thiscall *)(const void *, A...);
    return (*static_cast<Fn *const *>(face))[slot](face, args...);
}

// ---- the effects world ----

enum : uint32_t {
    evNodeAddRef = 0xe5000000,
    evNodeRelease = 0xe6000000,
    evNodeOther = 0xe7000000, // | the slot; the node's id follows
    evSetFlag = 0xe8000000,
    evGetParent = 0xe9000000,
    evAddChild = 0xea000000,
    evRemoveChild = 0xeb000000,
    evSetTranslation = 0xec000000,
    evSetRotation = 0xed000000,
    evOwnerAddRef = 0xee000000,
    evOwnerRelease = 0xef000000,
    evOwnerOther = 0xf0000000, // | the slot; the owner's id follows
    evOwnerKind = 0xf1000000,
    evOwnerParent = 0xf2000000,
    evFramework = 0xf3000000,
    evClassObject = 0xf4000000,
    evComOther = 0xf5000000,
};

inline uint32_t Bits(float f) { return std::bit_cast<uint32_t>(f); }

// A scene-graph node, and any counted cIGZUnknown a test needs (slots 1 and 2 count).
struct FakeNode {
    void *const *vt;
    uint32_t id;
    uint32_t refs;
    FakeNode *parent;
};
struct FakeOwner {
    void *const *vt;
    uint32_t id;
    uint32_t refs;
    FakeNode *tree; // slot 42's answer
    uint32_t kind;  // slot 23's answer
};
inline uint32_t IdOf(const void *fake) { return fake ? static_cast<const FakeNode *>(fake)->id : 0; }

inline uint32_t __fastcall NodeAddRef(FakeNode *n, void *)
{
    Ev({evNodeAddRef | n->id});
    return ++n->refs;
}
inline uint32_t __fastcall NodeRelease(FakeNode *n, void *)
{
    Ev({evNodeRelease | n->id});
    return --n->refs;
}
inline void __fastcall NodeSetFlag(FakeNode *n, void *, uint32_t bit, bool value)
{
    Ev({evSetFlag | n->id, bit, uint32_t(value)});
}
inline FakeNode *__fastcall NodeGetParent(FakeNode *n, void *)
{
    Ev({evGetParent | n->id});
    return n->parent;
}
inline void __fastcall NodeAddChild(FakeNode *n, void *, FakeNode *child)
{
    Ev({evAddChild | n->id, IdOf(child)});
    child->parent = n;
}
inline void __fastcall NodeRemoveChild(FakeNode *n, void *, FakeNode *child)
{
    Ev({evRemoveChild | n->id, IdOf(child)});
    child->parent = nullptr;
}
inline void __fastcall NodeSetTranslation(FakeNode *n, void *, const float *t)
{
    Ev({evSetTranslation | n->id, Bits(t[0]), Bits(t[1]), Bits(t[2])});
}
inline void __fastcall NodeSetRotation(FakeNode *n, void *, const float *q)
{
    Ev({evSetRotation | n->id, Bits(q[0]), Bits(q[1]), Bits(q[2]), Bits(q[3])});
}
template <unsigned Slot> uint32_t __fastcall NodeOther(FakeNode *n, void *)
{
    Ev({evNodeOther | Slot, n->id});
    return 0;
}
inline uint32_t __fastcall OwnerAddRef(FakeOwner *o, void *)
{
    Ev({evOwnerAddRef | o->id});
    return ++o->refs;
}
inline uint32_t __fastcall OwnerRelease(FakeOwner *o, void *)
{
    Ev({evOwnerRelease | o->id});
    return --o->refs;
}
inline uint32_t __fastcall OwnerKind(FakeOwner *o, void *)
{
    Ev({evOwnerKind | o->id});
    return o->kind;
}
inline FakeNode *__fastcall OwnerParent(FakeOwner *o, void *)
{
    Ev({evOwnerParent | o->id, IdOf(o->tree)});
    return o->tree;
}
template <unsigned Slot> uint32_t __fastcall OwnerOther(FakeOwner *o, void *)
{
    Ev({evOwnerOther | Slot, o->id});
    return 0;
}

constexpr unsigned kFakeNodeSlots = 100, kFakeOwnerSlots = 64;
inline void *gFakeNodeVT[kFakeNodeSlots];
inline void *gFakeOwnerVT[kFakeOwnerSlots];
template <size_t... I> void FillNodeOthers(std::index_sequence<I...>)
{
    ((gFakeNodeVT[I] = reinterpret_cast<void *>(&NodeOther<unsigned(I)>)), ...);
}
template <size_t... I> void FillOwnerOthers(std::index_sequence<I...>)
{
    ((gFakeOwnerVT[I] = reinterpret_cast<void *>(&OwnerOther<unsigned(I)>)), ...);
}
inline const bool gFakeTablesFilled = [] {
    FillNodeOthers(std::make_index_sequence<kFakeNodeSlots>());
    FillOwnerOthers(std::make_index_sequence<kFakeOwnerSlots>());
    gFakeNodeVT[1] = reinterpret_cast<void *>(&NodeAddRef);
    gFakeNodeVT[2] = reinterpret_cast<void *>(&NodeRelease);
    gFakeNodeVT[29] = reinterpret_cast<void *>(&NodeSetFlag);
    gFakeNodeVT[34] = reinterpret_cast<void *>(&NodeGetParent);
    gFakeNodeVT[45] = reinterpret_cast<void *>(&NodeAddChild);
    gFakeNodeVT[46] = reinterpret_cast<void *>(&NodeRemoveChild);
    gFakeNodeVT[64] = reinterpret_cast<void *>(&NodeSetTranslation);
    gFakeNodeVT[65] = reinterpret_cast<void *>(&NodeSetRotation);
    gFakeOwnerVT[1] = reinterpret_cast<void *>(&OwnerAddRef);
    gFakeOwnerVT[2] = reinterpret_cast<void *>(&OwnerRelease);
    gFakeOwnerVT[23] = reinterpret_cast<void *>(&OwnerKind);
    gFakeOwnerVT[42] = reinterpret_cast<void *>(&OwnerParent);
    return true;
}();

// The director framework: GetClassObject hands out the next pool node with one reference, or fails (nothing written) when
// `failClasses` is set. Node ids 1.., the owners' 0x20.., the tree nodes' 0x30.. .
struct EffectsWorld;
inline EffectsWorld *gWorld = nullptr;
struct FakeCom {
    void *const *vt;
};
struct EffectsWorld {
    static constexpr uint32_t kNodes = 24, kOwners = 3, kTrees = 3;
    FakeCom com;
    FakeNode nodes[kNodes];
    FakeOwner owners[kOwners];
    FakeNode trees[kTrees];
    uint32_t nextNode = 0;
    bool failClasses = false;
    shape::StandIn framework;
    explicit EffectsWorld();
    ~EffectsWorld() { gWorld = nullptr; }
    EffectsWorld(const EffectsWorld &) = delete;
    EffectsWorld &operator=(const EffectsWorld &) = delete;
};
inline void *StandFramework()
{
    Ev({evFramework});
    return &gWorld->com;
}
inline uint32_t __fastcall ComGetClassObject(FakeCom *, void *, uint32_t classId, uint32_t iid, void **out)
{
    EffectsWorld *w = gWorld;
    const bool ok = !w->failClasses && w->nextNode < EffectsWorld::kNodes;
    Ev({evClassObject, classId, iid, uint32_t(ok)});
    if (!ok) return 0;
    FakeNode *n = &w->nodes[w->nextNode++];
    ++n->refs;
    *out = n;
    return 1;
}
template <unsigned Slot> uint32_t __fastcall ComOther(FakeCom *, void *)
{
    Ev({evComOther | Slot});
    return 0;
}
inline void *const kFakeComVT[8] = {
    reinterpret_cast<void *>(&ComOther<0>), reinterpret_cast<void *>(&ComOther<1>),
    reinterpret_cast<void *>(&ComOther<2>), reinterpret_cast<void *>(&ComGetClassObject),
    reinterpret_cast<void *>(&ComOther<4>), reinterpret_cast<void *>(&ComOther<5>),
    reinterpret_cast<void *>(&ComOther<6>), reinterpret_cast<void *>(&ComOther<7>)};
inline EffectsWorld::EffectsWorld() : framework(0x1ae18d, &GetDirectorFrameworkObject, &StandFramework)
{
    gWorld = this;
    com.vt = kFakeComVT;
    for (uint32_t i = 0; i < kNodes; ++i) nodes[i] = FakeNode{gFakeNodeVT, 1 + i, 0, nullptr};
    for (uint32_t i = 0; i < kOwners; ++i) owners[i] = FakeOwner{gFakeOwnerVT, 0x20 + i, 0, nullptr, 0};
    for (uint32_t i = 0; i < kTrees; ++i) trees[i] = FakeNode{gFakeNodeVT, 0x30 + i, 0, nullptr};
    owners[1].tree = &trees[0];
    owners[2].tree = &trees[1];
}

// An effect's two vtable words on one side, as an indexable list for shape::VtCode / FaceOf.
struct EffectVt {
    void *word[2];
    void *operator[](unsigned face) const { return word[face]; }
    static EffectVt Of(const void *effect)
    {
        const auto *w = static_cast<void *const *>(effect);
        return EffectVt{{w[0], w[1]}};
    }
};

// A random transform: near-rotations from a few exact values, sometimes a NaN (the operand order shows on them).
inline void RandomTransform(fx::EffectsTransform &t, std::mt19937 &r)
{
    const auto value = [&r] {
        if (r() % 16 == 0) return std::bit_cast<float>(0x7fc00000u | (r() & 0x3fffffu) | (r() << 31));
        return testhelp::FewOrUniform(r, -2.0f, 2.0f);
    };
    t = fx::EffectsTransform{};
    t.flags = r() % 2 ? fx::EffectsTransform::kRotated : 0;
    for (auto &row : t.m)
        for (float &c : row) c = value();
    t.scale = r() % 3 ? value() : 1.0f;
    t.translation = fx::Vector3{value(), value(), value()};
}

// The fields the base defines: the count, the description (as an offset to `description`), the running flag. Vtable words
// are the caller's: they differ per class.
inline void PushEffectBase(Blob &b, const void *effect, const void *description)
{
    const auto *e = static_cast<const cTSEffectBase *>(static_cast<const cITSComponentEffect *>(effect));
    const auto *words = static_cast<const uint32_t *>(effect);
    b.push_back(words[2]);
    b.push_back(uint32_t(static_cast<const uint8_t *>(e->mDescription) - static_cast<const uint8_t *>(description)));
    b.push_back(*reinterpret_cast<const uint8_t *>(&e->mRunning));
}
inline void PushNodeEffect(Blob &b, const void *effect, const void *description)
{
    PushEffectBase(b, effect, description);
    const auto *e = static_cast<const cTSNodeEffectBase *>(static_cast<const cITSComponentEffect *>(effect));
    b.push_back(IdOf(e->mOwner.Get()));
    b.push_back(IdOf(e->mNode.Get()));
}

// One random slot of the 14, through the effect's own vtable. Results and the log go to `b`. For a node effect: `node` and
// `owner` are read back from the object, and a slot that needs them is skipped without them.
inline void EffectStep(Blob &b, void *effect, EffectsWorld &w, std::mt19937 &r, bool hasNode, bool hasOwner)
{
    fx::EffectsTransform local, parent;
    const uint32_t op = r() % 20;
    uint32_t result = 0xfeed;
    switch (op) {
    case 0:
    case 1: {
        FakeOwner *owner = r() % 8 ? &w.owners[r() % EffectsWorld::kOwners] : nullptr;
        w.failClasses = r() % 8 == 0;
        CallSlot<void>(effect, 0, static_cast<void *>(owner));
        break;
    }
    case 2:
        if (r() % 3 == 0) CallSlot<void>(effect, 1);
        break;
    case 3:
    case 4:
        CallSlot<void>(effect, 2, uint32_t(r()));
        break;
    case 5:
    case 6:
        if (hasNode) CallSlot<void>(effect, 3, uint32_t(r()));
        break;
    case 7:
        result = CallSlot<uint8_t>(effect, 4);
        break;
    case 8:
    case 9:
        if (hasNode && hasOwner) CallSlot<void>(effect, 5, reinterpret_cast<void *>(uintptr_t(r())), reinterpret_cast<void *>(uintptr_t(r())));
        break;
    case 10:
    case 11:
        if (hasNode) {
            RandomTransform(local, r);
            RandomTransform(parent, r);
            CallSlot<void>(effect, 6, &local, &parent);
        }
        break;
    case 12:
    case 13: {
        const uint32_t kind = r() % 7;
        b.push_back(kind);
        result = CallSlot<uint8_t>(effect, 7, kind, reinterpret_cast<void *>(uintptr_t(r())), reinterpret_cast<void *>(uintptr_t(r())));
        break;
    }
    case 14:
        result = CallSlot<uint8_t>(effect, 8, reinterpret_cast<void *>(uintptr_t(r())), reinterpret_cast<void *>(uintptr_t(r())));
        break;
    case 15:
        if (hasNode) CallSlot<void>(effect, 9, bool(r() % 2));
        break;
    case 16:
        CallSlot<void>(effect, 10, uint32_t(r()));
        break;
    case 17:
        CallSlot<void>(effect, 11, uint32_t(r()));
        break;
    case 18: // the count through both faces; the caller's reference stays
        result = CallSlot<uint32_t>(effect, 12);
        b.push_back(CallSlot<uint32_t>(effect, 13));
        b.push_back(CallSlot<uint32_t>(static_cast<uint8_t *>(effect) + 4, 2));
        b.push_back(CallSlot<uint32_t>(static_cast<uint8_t *>(effect) + 4, 1));
        break;
    case 19:
        CallSlot<uint32_t>(static_cast<uint8_t *>(effect) + 4, 2);
        b.push_back(CallSlot<uint32_t>(static_cast<uint8_t *>(effect) + 4, 3));
        result = CallSlot<uint32_t>(static_cast<uint8_t *>(effect) + 4, 4);
        break;
    }
    b.push_back(op);
    b.push_back(result);
}

} // namespace fxtest
