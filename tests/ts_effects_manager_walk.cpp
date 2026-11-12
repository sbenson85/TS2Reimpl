// Differential tests for the effects manager's walk (src/ts/cTSEffectsManager.cpp, lane RX): ApplyDetail 0xbcb4c0, MakeChild
// 0xbc6050, SendChildBlend 0xbcb260, and the child record's copies (src/ts/cTSVisualEffect.cpp): 0xbc4540, 0xbc41e0, 0xbc57a0,
// 0xbc5380, the vector's growth 0xbc30e0.
//
// The walk runs on a plain block the size of the manager: only the fields it reads are built, on both sides by our code. The
// random generator is real; the name lookups 0xbc8590 / 0xbc8620, the pool 0xbc9ab0 and the replay 0xbe8c80 (each verified
// elsewhere) are stood in, on both sides, and log. The effects are cTSVisualEffect objects whose virtuals log, the children
// are logging fakes, so the same objects meet both sides; compared are the log (every call, the placement each Update got, the
// six blend values), the children vector word for word (byte +1 of a placement is the storage's leftover in both, so
// masked), the effect's fields, the generator's state, and the blocks allocated.
#include "ts_effects_collection.h"

#include "difftest.h"
#include "free_recorder.h"
#include "test_helpers.h"
#include "ts/cTSEffectsManager.h"
#include "ts/cTSVisualEffect.h"

#include <bit>
#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

using fx::EffectsTransform;
using Mgr = cTSEffectsManager;
using Log = std::vector<uint32_t>;

Log gLog;
void Note(uint32_t a) { gLog.push_back(a); }
void Note(uint32_t a, uint32_t b)
{
    gLog.push_back(a);
    gLog.push_back(b);
}

constexpr uint32_t kChildren = 32, kEffects = 8;
struct World;
World *gWorld = nullptr;

void NoteTransform(const EffectsTransform *t)
{
    const auto *w = reinterpret_cast<const uint32_t *>(t);
    Note(w[0] & 0xffff00ffu); // the byte at +1 is a leftover in the original
    for (int i = 1; i < 14; ++i) Note(w[i]);
}

enum : uint32_t {
    evAttach = 0xa1000000,
    evDetach = 0xa2000000,
    evStart = 0xa3000000,
    evStop = 0xa4000000,
    evRunning = 0xa5000000,
    evUpdate = 0xa6000000,
    evEvent = 0xa7000000,
    evFlagged = 0xa8000000,
    evSlot10 = 0xa9000000,
    evAddRef = 0xaa000000,
    evRelease = 0xab000000,
    evFindName = 0xac000000,
    evFindAlias = 0xad000000,
    evAcquire = 0xae000000,
    evReplay = 0xaf000000,
    evCreate = 0xb0000000,
    evCamera = 0xb1000000,
};

uint32_t IdOf(const cITSComponentEffect *effect);
uint32_t OwnerWord(const void *owner);
uint32_t ParentWord(const EffectsTransform *parent);

// How a fake answers IsRunning: from its id and how many times it was asked.
bool RunningAnswer(uint32_t id, uint32_t &asked)
{
    const uint32_t mix = (id * 2654435761u + ++asked * 40503u) >> 13;
    return (mix & 3) != 0;
}

template <typename Base> struct Logged : Base {
    uint32_t id = 0;
    uint32_t refs = 0;
    uint32_t asked = 0;
    void Attach(cIGZUnknown *owner) override { Note(evAttach | id, OwnerWord(owner)); }
    void Detach() override { Note(evDetach | id); }
    void Start(uint32_t a) override { Note(evStart | id, a); }
    void Stop(uint32_t a) override { Note(evStop | id, a); }
    bool IsRunning() override
    {
        const bool running = RunningAnswer(id, asked);
        Note(evRunning | id, running);
        return running;
    }
    void Update(const EffectsTransform *local, const EffectsTransform *parent) override
    {
        Note(evUpdate | id, ParentWord(parent));
        NoteTransform(local);
    }
    bool HandleEvent(uint32_t kind, void *values, void *count) override
    {
        Note(evEvent | id, kind);
        const auto n = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(count));
        Note(n);
        for (uint32_t i = 0; i < n && i < 6; ++i) Note(static_cast<const uint32_t *>(values)[i]);
        return false;
    }
    void SetFlagged(bool value) override { Note(evFlagged | id, value); }
    void Slot10(uint32_t a) override { Note(evSlot10 | id, IdOf(reinterpret_cast<cITSComponentEffect *>(uintptr_t(a)))); }
    uint32_t AddRef() override
    {
        Note(evAddRef | id);
        return ++refs;
    }
    uint32_t Release() override
    {
        Note(evRelease | id);
        return --refs;
    }
};

struct ChildBase : cITSComponentEffect {
    void AddToScene(void *, void *) override {}
    bool Query(void *, void *) override { return false; }
    void Slot11(uint32_t) override {}
    void Attach(cIGZUnknown *) override {}
    void Detach() override {}
    void Start(uint32_t) override {}
    void Stop(uint32_t) override {}
    bool IsRunning() override { return false; }
    void Update(const EffectsTransform *, const EffectsTransform *) override {}
    bool HandleEvent(uint32_t, void *, void *) override { return false; }
    void SetFlagged(bool) override {}
    void Slot10(uint32_t) override {}
    uint32_t AddRef() override { return 0; }
    uint32_t Release() override { return 0; }
};
using FakeChild = Logged<ChildBase>;
using FakeEffect = Logged<cTSVisualEffect>;

struct Description : cITSComponentDescription {
    FakeChild *made = nullptr; // what CreateEffect hands out
    uint32_t id = 0;
    uint32_t refs = 0;
    int32_t GetType() override { return 0; }
    cITSComponentEffect *CreateEffect() override
    {
        Note(evCreate | id);
        return made;
    }
    void CopyTo(cITSComponentDescription *) override {}
    void *GetDescription() override { return nullptr; }
    void Read(void *) override {}
    void Write(void *) override {}
    uint32_t GetSize() override { return 0; }
    uint32_t AddRef() override { return ++refs; }
    uint32_t Release() override { return --refs; }
};

struct World {
    alignas(8) uint8_t block[sizeof(Mgr)];
    Mgr *m;
    FakeChild children[kChildren];
    FakeEffect effects[kEffects]; // [0] the walked effect, the rest what the pool hands out
    uint32_t nextEffect = 1;
    Description descriptions[3][4];
    int32_t aliasIndex = -1;

    World()
    {
        std::memset(block, 0, sizeof(block));
        m = reinterpret_cast<Mgr *>(block);
        ::new (&m->mEffects) std::vector<EffectEntry>;
        ::new (&m->mTables) std::vector<EffectDescriptionTable>;
        m->mTables.resize(3);
        uint32_t next = 0x40;
        for (uint32_t i = 0; i < kChildren; ++i) children[i].id = 0x100 + i;
        for (uint32_t i = 0; i < kEffects; ++i) effects[i].id = 0x200 + i;
        for (uint32_t t = 0; t < 3; ++t) {
            for (uint32_t d = 0; d < 4; ++d) {
                descriptions[t][d].id = next++;
                m->mTables[t].mDescriptions.push_back(EffectDescriptionRef(&descriptions[t][d]));
            }
        }
    }
    ~World()
    {
        m->mEffects.~vector();
        m->mTables.~vector();
    }
    World(const World &) = delete;
};

uint32_t IdOf(const cITSComponentEffect *effect)
{
    if (!effect) return 0;
    for (const FakeChild &c : gWorld->children)
        if (&c == effect) return c.id;
    for (const FakeEffect &e : gWorld->effects)
        if (static_cast<const cITSComponentEffect *>(&e) == effect) return e.id;
    return 0xdead;
}
uint32_t OwnerWord(const void *owner)
{
    return owner == static_cast<void *>(static_cast<cITSEffectsManager *>(gWorld->m)) ? 1 : 0xdead;
}
uint32_t ParentWord(const EffectsTransform *parent) { return parent == &gWorld->effects[0].mBlockC ? 1 : 0xdead; }

// ---- the stand-ins, both sides ----

int32_t __fastcall StandFindName(void *, void *, const std::string &key)
{
    Note(evFindName, uint32_t(key.size()));
    for (char c : key) Note(uint8_t(c));
    if (key.size() != 3 || key[0] != 'f') return -1;
    return key[2] - '0';
}
int32_t __fastcall StandFindAlias(void *, void *, uint32_t first, uint32_t second)
{
    Note(evFindAlias, first);
    Note(second);
    return gWorld->aliasIndex;
}
cITSVisualEffect *__fastcall StandAcquire(void *, void *)
{
    Note(evAcquire);
    return &gWorld->effects[gWorld->nextEffect++];
}
void __fastcall StandReplay(void *, void *) { Note(evReplay); }

// The camera the pool's effects ask their owner for (0xbe9670, slot 43 of the owner the fake's field holds).
cIGZUnknown *__fastcall OwnerCamera(void *, void *)
{
    Note(evCamera);
    return nullptr;
}
uint32_t __fastcall OwnerCount(void *, void *) { return 1; }
void *gOwnerTable[44];
struct RawOwner {
    void **table = gOwnerTable;
} gOwner;

struct Stands {
    shape::StandIn findName{0xbc8590u, &Mgr::FindEffectIndex, &StandFindName};
    shape::StandIn findAlias{0xbc8620u, &Mgr::FindAliasIndex, &StandFindAlias};
    shape::StandIn acquire{0xbc9ab0u, &Mgr::AcquireEffect, &StandAcquire};
    shape::StandIn replay{0xbe8c80u, &cTSVisualEffect::ReplayEvents, &StandReplay};
};

// ---- the scenario ----

float Small(std::mt19937 &r)
{
    static const float kValues[] = {0.0f, 1.0f, -1.0f, 0.5f, 0.25f, 2.0f, -3.5f, 7.0f};
    return r() % 3 ? kValues[r() % 8] : static_cast<float>(int32_t(r() % 2001) - 1000) / 64.0f;
}

void FillTransform(EffectsTransform &t, std::mt19937 &r)
{
    t.flags = uint8_t(r() & 7);
    t.pad[0] = 0;
    t.Version() = uint16_t(r() % 5);
    for (auto &row : t.m)
        for (float &v : row) v = Small(r);
    t.scale = Small(r);
    t.translation = {Small(r), Small(r), Small(r)};
}

// The first component of each effect is the anchor: always shown, so a component that takes events (bit 3) always has a child
// before it (the original reads before the vector's start otherwise, ORIGINAL-BUG in the source, and the game's data has none).
void FillComponent(EffectComponent &c, std::mt19937 &r, bool anchor)
{
    c.mName = (r() % 2 ? "fx" : "c") + std::to_string(r() % 2);
    static const int32_t kTypes[] = {1, 1, 2, 3, 3, 2, 0, -1};
    c.mType = kTypes[r() % 8];
    c.mWord1c = r() & 0x37;
    if (r() % 3 == 0) c.mWord1c |= 8;
    EffectsTransform t;
    FillTransform(t, r);
    std::memcpy(static_cast<void *>(&c.mFlags20), &t, sizeof(t));
    c.mByte58 = uint8_t(r() % 4);
    c.mByte59 = uint8_t(r() % 7);
    for (uint32_t i = 0; i < 6; ++i) c.mWords5c[i] = std::bit_cast<uint32_t>(Small(r));
    c.mWords5c[7] = r() % 3 ? 0 : (r() % 2 ? 1u : 3u);
    c.mWords5c[6] = c.mWords5c[7] ? (r() & c.mWords5c[7]) : 0;
    c.mHalf7c = uint16_t(r() % 2 ? 0 : 1 + r() % 3);
    static const uint16_t kWeights[] = {0, 0x4000, 0x8000, 0xc000, 0xffff, 1};
    c.mHalf7e = kWeights[r() % 6];
    c.mWord80 = std::bit_cast<uint32_t>(Small(r));
    c.mIndex = int32_t(r() % 7) - 1; // -1, or past the tables' four
    if (anchor) {
        c.mType = 1;
        c.mIndex = 0;
        c.mWord1c &= ~8u;
        c.mByte58 = 0;
        c.mByte59 = 200;
        c.mWords5c[6] = c.mWords5c[7] = 0;
        c.mHalf7c = 0;
    }
}

struct Scenario {
    bool activate;
    uint32_t bits;
};

Scenario Build(World &w, std::mt19937 &r)
{
    Mgr *m = w.m;
    Scenario s{r() % 3 == 0, r() % 4 == 0 ? (r() % 2 ? 0x800u : 0x80000u) : uint32_t(r() % 2)};
    m->mReloading = r() % 4 == 0;
    m->mWordA64 = r() % 3 == 0;
    m->mDetail = int32_t(r() % 6);
    m->mPreviousDetail = int32_t(r() % 6);
    m->mDetailIsMax = r() % 2;
    m->mMaxDetail = r() % 2;
    m->mDebugBits = r() & 3;
    m->mPreviousDebugBits = r() & 3;
    m->mDetailLevel = float(r() % 3);
    m->mPreviousDetailLevel = r() % 2 ? m->mDetailLevel : float(r() % 3);
    m->mRandom.Construct(r());

    const bool madeFirst = s.activate || m->mReloading;
    const uint32_t entries = 2 + r() % 3;
    m->mEffects.resize(entries);
    for (EffectEntry &e : m->mEffects) {
        e.mFlags = r() % 2 ? 0 : 4; // bit 2: the description asks the owner for its camera
        e.mParams[1] = r();
        e.mParams[2] = r();
        e.mComponents.resize(1 + r() % 8);
        for (size_t i = 0; i < e.mComponents.size(); ++i) FillComponent(e.mComponents[i], r, i == 0);
    }

    FakeEffect &effect = w.effects[0];
    for (uint32_t i = 0; i < kEffects; ++i) {
        std::fill_n(reinterpret_cast<void **>(&w.effects[i].mOwner), 1, reinterpret_cast<void *>(&gOwner));
        w.effects[i].mDescription = nullptr;
    }
    effect.mEffectIndex = r() % 6 == 0 ? -1 : int32_t(r() % entries);
    effect.mName = r() % 3 == 0 ? "" : (r() % 5 == 0 ? "none" : "fx" + std::to_string(r() % entries));
    effect.mUnknown2c = r();
    effect.mUnknown30 = r();
    w.aliasIndex = r() % 4 == 0 ? -1 : int32_t(r() % entries);
    effect.mFlag3a = uint8_t(r() % 2); // the constructor leaves it alone
    effect.mRunning = r() % 4 != 0;
    effect.mStopping = r() % 4 == 0;
    effect.mFlags = r() & 3;
    FillTransform(effect.mBlockC, r);
    FillTransform(effect.mBlockD, r);
    if (r() % 2) effect.mEntries.resize(1);
    if (r() % 2) effect.mResources.resize(1);

    // The children on the effect, each counted once.
    uint32_t nextChild = 0;
    if (effect.mEffectIndex >= 0) {
        const auto &components = m->mEffects[effect.mEffectIndex].mComponents;
        const uint32_t count = madeFirst ? r() % 5 : 1 + r() % 5;
        for (uint32_t i = 0; i < count; ++i) {
            FakeChild &fake = w.children[nextChild++];
            effect.mChildren.emplace_back();
            cTSVisualChild &child = effect.mChildren.back();
            FillTransform(child.placement, r);
            child.effect = &fake;
            fake.refs = 1;
            child.unknown3c = i == 0 ? 0 : uint32_t(r() % components.size());
            child.weight = Small(r);
            child.flags = r() & 0x3f;
            for (uint32_t k = r() % 3; k > 0; --k) child.indexes.push_back(int32_t(r() % 9));
        }
    }
    // What the descriptions hand out: mostly a new fake each, sometimes none (not the anchor's: first of table 0).
    for (uint32_t t = 0; t < 3; ++t) {
        for (uint32_t d = 0; d < 4; ++d) {
            const bool none = !(t == 0 && d == 0) && r() % 5 == 0;
            FakeChild *fake = nextChild < kChildren ? &w.children[nextChild++] : nullptr;
            w.descriptions[t][d].made = none ? nullptr : fake;
        }
    }
    w.descriptions[0][0].made = &w.children[kChildren - 1];
    return s;
}

// The block of one side's results.
void Push(Log &b, World &w)
{
    Mgr *m = w.m;
    FakeEffect &effect = w.effects[0];
    b.insert(b.end(), gLog.begin(), gLog.end());
    gLog.clear();
    b.push_back(0x0b000001);
    b.push_back(uint32_t(effect.mEffectIndex));
    b.push_back(uint32_t(reinterpret_cast<const uint8_t *>(effect.mDescription) ? reinterpret_cast<const uint8_t *>(effect.mDescription) - reinterpret_cast<const uint8_t *>(m->mEffects.data()) : 0xffffffffu));
    b.push_back(effect.mFlag3a);
    b.push_back(uint32_t(effect.mChildren.size()));
    b.push_back(uint32_t(effect.mChildren.capacity()));
    for (const cTSVisualChild &c : effect.mChildren) {
        const auto *words = reinterpret_cast<const uint32_t *>(&c);
        b.push_back(words[0] & 0xffff00ffu);
        for (int i = 1; i < 14; ++i) b.push_back(words[i]);
        b.push_back(IdOf(c.effect));
        b.push_back(c.unknown3c);
        b.push_back(std::bit_cast<uint32_t>(c.weight));
        b.push_back(c.flags);
        b.push_back(uint32_t(c.indexes.size()));
        b.push_back(uint32_t(c.indexes.capacity()));
        for (int32_t v : c.indexes) b.push_back(uint32_t(v));
    }
    b.push_back(0x0b000002);
    for (uint32_t i = 0; i < kEffects; ++i) {
        const FakeEffect &e = w.effects[i];
        b.push_back(e.refs);
        b.push_back(uint32_t(e.mEffectIndex));
        b.push_back(e.mUnknown38);
        b.push_back(uint32_t(e.mName.size()));
        b.push_back(e.mParameter48);
        b.push_back(e.mParameter4c);
        b.push_back(uint32_t(e.mDescription ? reinterpret_cast<const uint8_t *>(e.mDescription) - reinterpret_cast<const uint8_t *>(m->mEffects.data()) : 0xffffffffu));
    }
    for (const FakeChild &c : w.children) b.push_back(c.refs);
    b.push_back(uint32_t(m->mRandom.mLeft));
    b.push_back(m->mRandom.mNext ? uint32_t(m->mRandom.mNext - m->mRandom.mState) : 0xffffffffu);
}

void Clean(World &w)
{
    for (uint32_t i = 0; i < kEffects; ++i) {
        FakeEffect &e = w.effects[i];
        for (cTSVisualChild &c : e.mChildren) c.effect = nullptr;
        e.mChildren.clear();
        std::fill_n(reinterpret_cast<void **>(&e.mOwner), 1, nullptr);
    }
}

void Prepare()
{
    gOwnerTable[1] = reinterpret_cast<void *>(&OwnerCount);
    gOwnerTable[2] = reinterpret_cast<void *>(&OwnerCount);
    gOwnerTable[43] = reinterpret_cast<void *>(&OwnerCamera);
    gLog.reserve(1 << 16);
    gLog.clear();
}

} // namespace

// 0xbcb4c0 with its callees 0xbc6050, 0xbcb260 (original on one side, ours on the other, each reaching its own).
DIFF_TEST(EffectsManager_ApplyDetail, 0xbcb4c0)
{
    Stands stands;
    Prepare();
    for (uint32_t seed = 0; seed < 6000; ++seed) {
        Log blobs[2];
        for (int side = 0; side < 2; ++side) {
            auto world = std::make_unique<World>();
            World &w = *world;
            gWorld = &w;
            std::mt19937 r(seed * 31 + 11);
            const Scenario s = Build(w, r);
            gLog.clear();
            Log &b = blobs[side];
            difftest::AllocationCounter counter;
            if (side == 0)
                ctx.Original<void(__thiscall *)(void *, void *, uint32_t, uint32_t)>(0xbcb4c0)(
                    static_cast<cITSEffectsManager *>(w.m), static_cast<cITSVisualEffect *>(&w.effects[0]),
                    uint32_t(s.activate), s.bits);
            else
                w.m->ApplyDetail(&w.effects[0], s.activate, s.bits);
            counter.Stop();
            b.push_back(uint32_t(counter.Blocks()));
            b.push_back(uint32_t(counter.Bytes()));
            Push(b, w);
            Clean(w);
            gWorld = nullptr;
        }
        fxtest::Same(ctx, blobs[0], blobs[1], "ApplyDetail", seed);
    }
}

namespace {

using Child = cTSVisualChild;

void FillChild(Child &c, FakeChild *fake, std::mt19937 &r)
{
    FillTransform(c.placement, r);
    c.placement.pad[0] = uint8_t(r());
    c.effect = fake;
    c.unknown3c = r();
    c.weight = Small(r);
    c.flags = r();
    c.indexes.clear();
    for (uint32_t k = r() % 5; k > 0; --k) c.indexes.push_back(int32_t(r()));
}

void PushChild(Log &b, const Child &c, const std::vector<FakeChild> &fakes, uint32_t firstMask = 0xffff00ffu)
{
    const auto *words = reinterpret_cast<const uint32_t *>(&c);
    b.push_back(words[0] & firstMask); // a copy leaves the byte at +1 as it was
    for (int i = 1; i < 14; ++i) b.push_back(words[i]);
    b.push_back(c.effect ? uint32_t(static_cast<const FakeChild *>(c.effect) - fakes.data()) : 0xffffffffu);
    b.push_back(c.unknown3c);
    b.push_back(std::bit_cast<uint32_t>(c.weight));
    b.push_back(c.flags);
    b.push_back(uint32_t(c.indexes.size()));
    b.push_back(uint32_t(c.indexes.capacity()));
    for (int32_t v : c.indexes) b.push_back(uint32_t(v));
}

} // namespace

// 0xbc4540 (copy), 0xbc57a0 (assignment), 0xbc5380 (destructor) on one record: an empty target and a used one, with an effect or none.
DIFF_TEST(VisualChild_Copies, 0xbc4540)
{
    using CopyFn = void *(__thiscall *)(void *, const void *);
    using EndFn = void(__thiscall *)(void *);
    const auto copy = ctx.Original<CopyFn>(0xbc4540);
    const auto assign = ctx.Original<CopyFn>(0xbc57a0);
    const auto destroy = ctx.Original<EndFn>(0xbc5380);
    gLog.reserve(1 << 12);
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        Log blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::vector<FakeChild> fakes(3);
            for (uint32_t i = 0; i < 3; ++i) fakes[i].id = 0x300 + i, fakes[i].refs = 5;
            std::mt19937 r(seed * 17 + 3);
            alignas(4) uint8_t store[2][sizeof(Child)];
            auto *source = reinterpret_cast<Child *>(store[0]);
            ::new (source) Child;
            FillChild(*source, r() % 4 ? &fakes[0] : nullptr, r);
            const uint32_t pad = r() & 0xff;
            std::memset(store[1], int(pad), sizeof(Child));
            auto *target = reinterpret_cast<Child *>(store[1]);
            gLog.clear();
            Log &b = blobs[side];
            {
                difftest::AllocationCounter counter;
                if (side == 0) copy(target, source);
                else ::new (target) Child(*source);
                counter.Stop();
                b.push_back(uint32_t(counter.Blocks()));
            }
            PushChild(b, *target, fakes, 0xffffffffu);
            // Assignment over the copy: from a second record (or the same one).
            alignas(4) uint8_t third[sizeof(Child)];
            auto *other = reinterpret_cast<Child *>(third);
            ::new (other) Child;
            FillChild(*other, r() % 4 ? &fakes[r() % 2 + 1] : nullptr, r);
            if (other->effect) other->effect->AddRef();
            const Child &from = r() % 8 ? *other : *target;
            {
                difftest::AllocationCounter counter;
                if (side == 0) assign(target, &from);
                else *target = from;
                counter.Stop();
                b.push_back(uint32_t(counter.Blocks()));
            }
            PushChild(b, *target, fakes, 0xffffffffu);
            {
                difftest::AllocationCounter counter;
                if (side == 0) destroy(target);
                else target->~Child();
                counter.Stop();
                b.push_back(uint32_t(counter.Blocks()));
            }
            b.insert(b.end(), gLog.begin(), gLog.end());
            for (const FakeChild &f : fakes) b.push_back(f.refs);
            other->~Child();
            source->~Child();
            gLog.clear();
        }
        fxtest::Same(ctx, blobs[0], blobs[1], "child record", seed);
    }
}

// 0xbc30e0 (the vector's insert when it is full) with the range move 0xbc41e0 it uses for a middle insert.
DIFF_TEST(VisualChild_VectorGrowth, 0xbc30e0)
{
    using InsertFn = void *(__thiscall *)(void *, void *, const void *);
    const auto insert = ctx.Original<InsertFn>(0xbc30e0);
    gLog.reserve(1 << 12);
    for (uint32_t seed = 0; seed < 2000; ++seed) {
        Log blobs[2];
        for (int side = 0; side < 2; ++side) {
            std::vector<FakeChild> fakes(6);
            for (uint32_t i = 0; i < 6; ++i) fakes[i].id = 0x300 + i, fakes[i].refs = 3;
            std::mt19937 r(seed * 19 + 5);
            auto children = std::make_unique<std::vector<Child>>();
            const uint32_t count = r() % 6;
            children->reserve(count);
            for (uint32_t i = 0; i < count; ++i) {
                children->emplace_back();
                FillChild(children->back(), &fakes[i], r);
                fakes[i].AddRef();
            }
            Child value;
            FillChild(value, &fakes[5], r);
            fakes[5].AddRef();
            const uint32_t at = r() % 2 ? count : r() % (count + 1);
            gLog.clear();
            Log &b = blobs[side];
            {
                difftest::AllocationCounter counter;
                if (side == 0) {
                    insert(children.get(), children->data() + at, &value);
                } else {
                    children->insert(children->begin() + at, value);
                }
                counter.Stop();
                b.push_back(uint32_t(counter.Blocks()));
            }
            b.insert(b.end(), gLog.begin(), gLog.end());
            gLog.clear();
            b.push_back(uint32_t(children->size()));
            b.push_back(uint32_t(children->capacity()));
            for (const Child &c : *children) PushChild(b, c, fakes);
            for (const FakeChild &f : fakes) b.push_back(f.refs);
            children.reset();
            value.effect->Release();
            value.effect = nullptr;
            gLog.clear();
        }
        fxtest::Same(ctx, blobs[0], blobs[1], "child vector", seed);
    }
}
