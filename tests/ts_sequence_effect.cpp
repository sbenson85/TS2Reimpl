// cTSSequenceEffect (src/ts/cTSSequenceEffect.h), lane WP: original vs ours.
//
// Mode 0 builds the effect with the original Create 0xc07490, mode 1 with ours; every slot then runs through the object's own
// vtable. After each step: the result, the log of every fake's calls, both vtable words, every field (the vector by size,
// capacity and element ids) and the game's random generator. At the end the destructor through the +4 face's deleting slot
// (flags 0 leaves the block to compare, flags 1 frees it), with the frees by size over the whole run.
//
// Fakes: a factory (what Attach is given: slot 14 makes the named effect, the way the effects manager does) and the effects it
// makes (slots 3 start, 4 stop, 5 is-running, 8 set-flag, 9 / 10 take a placement), each living a seed-chosen number of
// is-running asks. The game's random generator (0x10fd138) is seeded alike on both sides before each run.
//
// Covered RVAs: 0xc07490 (Create), 0xc06c60 (constructor), 0xc06db0 (destructor), 0xc06e50 (deleting destructor), 0xc06e41
// (its +4 thunk), 0xc071d0 (slot 0), 0xc07310 (slot 2), 0xc07360 (slot 3), 0xc07210 (slot 4), 0xc06e80 (slot 5), 0xc07290
// (slot 6), 0xc07230 (slot 9), 0xc070d0 (the finished ones dropped); the shared slots through each side's vtable.
#include "ts_effects_collection.h"

#include "free_recorder.h"
#include "rz/cRZRandom.h"
#include "shape.h"
#include "ts/cTSCompDescription.h"
#include "ts/cTSSequenceEffect.h"

#include <cstring>
#include <new>
#include <utility>

namespace {

using namespace fxtest;

constexpr uint32_t kGameTables[2] = {0xe91494, 0xe914d0};
constexpr uint32_t kGameBaseTables[2] = {0xe8bd68, 0xda2fa8};

using CreateFn = void *(__cdecl *)(const void *);
using ConstructFn = void *(__thiscall *)(void *, const void *);

enum : uint32_t { evChild = 0xc7000000, evFactory = 0xc8000000 };

struct FakeChild {
    void *const *vt;
    uint32_t id;
    uint32_t refs;
    bool running;
    uint32_t life;
    uint32_t asked;
};
struct FakeFactory {
    void *const *vt;
    uint32_t id;
    uint32_t refs;
    uint32_t failPeriod;
    uint32_t failOffset;
    uint32_t calls;
};

constexpr unsigned kChildren = 64;
FakeChild gChildren[kChildren];
uint32_t gNextChild = 0;
uint32_t gLifeSeed = 0;

uint32_t Checksum(const uint32_t *w, unsigned n)
{
    uint32_t h = 0x811c9dc5u;
    for (unsigned i = 0; i < n; ++i) h = (h ^ w[i]) * 16777619u;
    return h;
}

uint32_t __fastcall ChAddRef(FakeChild *c, void *) { Ev({evChild | 1, c->id}); return ++c->refs; }
uint32_t __fastcall ChRelease(FakeChild *c, void *) { Ev({evChild | 2, c->id}); return --c->refs; }
void __fastcall ChStart(FakeChild *c, void *, uint32_t flag)
{
    c->running = true;
    c->asked = 0;
    Ev({evChild | 3, c->id, flag});
}
void __fastcall ChStop(FakeChild *c, void *, uint32_t how)
{
    c->running = false;
    Ev({evChild | 4, c->id, how});
}
uint32_t __fastcall ChRunning(FakeChild *c, void *)
{
    if (c->running && ++c->asked > c->life) c->running = false;
    Ev({evChild | 5, c->id, uint32_t(c->running)});
    return c->running ? 1u : 0u;
}
void __fastcall ChFlag(FakeChild *c, void *, uint32_t v) { Ev({evChild | 8, c->id, v & 0xff}); }
template <unsigned Slot> void __fastcall ChPlace(FakeChild *c, void *, const uint32_t *t)
{
    // The word with the flags byte holds a padding byte that stays as the heap had it.
    uint32_t words[14];
    std::memcpy(words, t, sizeof(words));
    words[0] &= 0xffff00ffu;
    Ev({evChild | Slot, c->id, Checksum(words, 14)});
}
template <unsigned Slot> uint32_t __fastcall ChOther(FakeChild *c, void *)
{
    Ev({evChild | 0x80 | Slot, c->id});
    return 0;
}

uint32_t __fastcall FaAddRef(FakeFactory *f, void *) { Ev({evFactory | 1, f->id}); return ++f->refs; }
uint32_t __fastcall FaRelease(FakeFactory *f, void *) { Ev({evFactory | 2, f->id}); return --f->refs; }
uint32_t __fastcall FaMake(FakeFactory *f, void *, const char *name, void **out)
{
    uint32_t h = 0x811c9dc5u;
    for (const char *p = name; *p; ++p) h = (h ^ uint8_t(*p)) * 16777619u;
    const uint32_t n = f->calls++;
    const bool fail = (f->failPeriod && (n + f->failOffset) % f->failPeriod == 0) || gNextChild >= kChildren;
    Ev({evFactory | 14, f->id, h, uint32_t(!fail)});
    if (fail) return 0;
    FakeChild *c = &gChildren[gNextChild++];
    c->refs++;
    *out = c;
    return 1;
}
template <unsigned Slot> uint32_t __fastcall FaOther(FakeFactory *f, void *)
{
    Ev({evFactory | 0x80 | Slot, f->id});
    return 0;
}

template <class T, size_t... I> void Fill(void **vt, std::index_sequence<I...>)
{
    ((vt[I] = reinterpret_cast<void *>(&ChOther<unsigned(I)>)), ...);
}
template <size_t... I> void FillFactory(void **vt, std::index_sequence<I...>)
{
    ((vt[I] = reinterpret_cast<void *>(&FaOther<unsigned(I)>)), ...);
}

constexpr unsigned kSlots = 24;
void *gChildVT[kSlots];
void *gFactoryVT[kSlots];
const bool gTables = [] {
    Fill<void>(gChildVT, std::make_index_sequence<kSlots>());
    gChildVT[1] = reinterpret_cast<void *>(&ChAddRef);
    gChildVT[2] = reinterpret_cast<void *>(&ChRelease);
    gChildVT[3] = reinterpret_cast<void *>(&ChStart);
    gChildVT[4] = reinterpret_cast<void *>(&ChStop);
    gChildVT[5] = reinterpret_cast<void *>(&ChRunning);
    gChildVT[8] = reinterpret_cast<void *>(&ChFlag);
    gChildVT[9] = reinterpret_cast<void *>(&ChPlace<9>);
    gChildVT[10] = reinterpret_cast<void *>(&ChPlace<10>);
    FillFactory(gFactoryVT, std::make_index_sequence<kSlots>());
    gFactoryVT[1] = reinterpret_cast<void *>(&FaAddRef);
    gFactoryVT[2] = reinterpret_cast<void *>(&FaRelease);
    gFactoryVT[14] = reinterpret_cast<void *>(&FaMake);
    return true;
}();

void ResetChildren(uint32_t seed)
{
    gNextChild = 0;
    for (unsigned i = 0; i < kChildren; ++i) {
        gChildren[i] = FakeChild{gChildVT, 0x100 + i, 0, false, 1 + (i * 7 + seed) % 6, 0};
    }
}

struct OurSeqTables {
    EffectVt live, destroyed;
};
const OurSeqTables &OurTables()
{
    static OurSeqTables t = [] {
        OurSeqTables v{};
        alignas(8) static uint8_t block[sizeof(cTSSequenceEffect)];
        static cTSSequenceDescription scratch; // the constructor reads the flags
        auto *e = ::new (block) cTSSequenceEffect(&scratch);
        v.live = EffectVt::Of(block);
        CallSlot<void *>(block + 4, 0, uint32_t(0));
        (void)e;
        v.destroyed = EffectVt::Of(block);
        return v;
    }();
    return t;
}

uint32_t TableCode(uint32_t word)
{
    const OurSeqTables &ours = OurTables();
    const unsigned live = shape::FaceOf(word, 2, shape::GameTables{kGameTables}, ours.live);
    if (live != ~0u) return shape::kFaceCode | live;
    const unsigned destroyed = shape::FaceOf(word, 2, shape::GameTables{kGameBaseTables}, ours.destroyed);
    if (destroyed != ~0u) return shape::kFaceCode | 0x10 | destroyed;
    return word;
}

void PushSequence(Blob &b, const void *effect, const void *description)
{
    const auto *w = static_cast<const uint32_t *>(effect);
    b.push_back(TableCode(w[0]));
    b.push_back(TableCode(w[1]));
    b.push_back(w[2]);
    b.push_back(uint32_t(w[3] - uint32_t(reinterpret_cast<uintptr_t>(description))));
    b.push_back(w[4] & 0xffff); // running, restart; two padding bytes stay as the heap had them
    b.push_back(w[5]);
    b.push_back(w[6]);
    b.push_back(w[7]);
    b.push_back(w[8] & 0xffff);
    for (int i = 9; i < 37; ++i) b.push_back(i == 9 || i == 23 ? (w[i] & 0xffff00ffu) : w[i]);
    b.push_back(w[37]);
    b.push_back(w[38]);
    b.push_back(IdOf(reinterpret_cast<const void *>(uintptr_t(w[39]))));
    // The vector: size, capacity and the ids it holds.
    const auto *begin = reinterpret_cast<const uint32_t *>(uintptr_t(w[40]));
    const uint32_t size = (w[41] - w[40]) / 4, capacity = (w[42] - w[40]) / 4;
    b.push_back(size);
    b.push_back(capacity);
    for (uint32_t i = 0; i < size; ++i) b.push_back(IdOf(reinterpret_cast<const void *>(uintptr_t(begin[i]))));
    b.push_back(IdOf(reinterpret_cast<const void *>(uintptr_t(w[43]))));
}

void Drain(Blob &b)
{
    b.insert(b.end(), gEv.begin(), gEv.end());
    gEv.clear();
}

void SeedRandom(uint32_t seed)
{
    auto *g = GameAddress<cRZRandom>(0x10fd138);
    g->mIntegerSeed = seed * 2654435761u + 12345u;
    g->mRealSeed = int32_t(seed * 40503u + 77u);
}

void PushRandom(Blob &b)
{
    const auto *g = GameAddress<cRZRandom>(0x10fd138);
    b.push_back(g->mIntegerSeed);
    b.push_back(uint32_t(g->mRealSeed));
}

void RandomDescription(cTSSequenceDescription &d, std::mt19937 &r)
{
    d.mFlags = r() & 0xf;
    const unsigned n = r() % 5; // sometimes none
    d.mEntries.clear();
    for (unsigned i = 0; i < n; ++i) {
        cTSSequenceEntry e;
        const unsigned kind = r() % 4;
        if (kind == 0) { // waits for the effect before
            e.mPosition.x = -1.0f;
            e.mPosition.y = 0.0f;
        } else {
            e.mPosition.x = testhelp::FewOrUniform(r, 0.0f, 1.5f);
            e.mPosition.y = e.mPosition.x + testhelp::FewOrUniform(r, 0.0f, 1.5f);
        }
        if (r() % 6 != 0) e.mName = Name(r);
        d.mEntries.push_back(e);
    }
}

} // namespace

DIFF_TEST(SequenceEffect_Construct, 0xc06c60)
{
    cTSSequenceDescription description;
    for (uint32_t seed = 0; seed < 40; ++seed) {
        Blob blobs[2];
        description.mFlags = seed;
        for (int mode = 0; mode < 2; ++mode) {
            Blob &b = blobs[mode];
            std::mt19937 r(seed + 1);
            alignas(8) uint8_t block[sizeof(cTSSequenceEffect)];
            for (uint8_t &x : block) x = uint8_t(r());
            void *e = mode ? static_cast<void *>(::new (block) cTSSequenceEffect(&description)) : ctx.Original<ConstructFn>(0xc06c60)(block, &description);
            b.push_back(uint32_t(e == block));
            const auto *w = reinterpret_cast<const uint32_t *>(block);
            b.push_back(TableCode(w[0]));
            b.push_back(TableCode(w[1]));
            b.push_back(uint32_t(w[3] - uint32_t(reinterpret_cast<uintptr_t>(&description))));
            for (size_t i = 8; i < sizeof(block); ++i) b.push_back(block[i]); // every byte, the garbage that stays too
            CallSlot<void *>(block + 4, 0, uint32_t(0));
        }
        Same(ctx, blobs[0], blobs[1], "SequenceEffect_Construct", seed);
    }
}

DIFF_TEST(SequenceEffect_Slots, 0xc07490)
{
    const CreateFn original = ctx.Original<CreateFn>(0xc07490);
    for (uint32_t seed = 0; seed < 400; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 7919u + 3);
            gEv.clear();
            gEv.reserve(1 << 16);
            b.reserve(1 << 16);
            ResetChildren(seed);
            SeedRandom(seed);
            FakeFactory factories[2] = {{gFactoryVT, 0x300, 0, 0, 0, 0}, {gFactoryVT, 0x301, 0, 0, 0, 0}};
            for (FakeFactory &f : factories) {
                f.failPeriod = r() % 4 == 0 ? 1 + r() % 4 : 0;
                f.failOffset = r() % 4;
            }
            cTSSequenceDescription description;
            RandomDescription(description, r);
            difftest::FreeRecorder rec;
            void *e = mode ? static_cast<void *>(cTSSequenceEffect::Create(&description)) : original(&description);
            auto *object = static_cast<cTSSequenceEffect *>(static_cast<cITSComponentEffect *>(e));
            PushSequence(b, e, &description);
            CallSlot<uint32_t>(static_cast<uint8_t *>(e) + 4, 2); // the caller's reference
            if (r() % 5 != 0) CallSlot<void>(e, 0, &factories[0]);
            PushSequence(b, e, &description);
            for (uint32_t step = 0; step < 60; ++step) {
                b.push_back(0x0b000000u | step);
                const uint32_t op = r() % 100;
                if (op < 55) {
                    const uint32_t kind = r() % 10;
                    const float dt = kind == 0 ? 0.0f : kind == 1 ? std::bit_cast<float>(0x7fc00000u | (r() & 0x3fffffu)) : kind == 2 ? -0.25f
                                     : std::uniform_real_distribution<float>(0.0f, 1.2f)(r);
                    // ORIGINAL-BUG (not reproduced): a Start that does not restart after the last entry leaves the index past the end,
                    // and a due entry that is not past time zero then reads the entry there (garbage). Such a run starts over.
                    if (object->mIndex >= description.mEntries.size()) {
                        CallSlot<void>(e, 2, uint32_t(0));
                        b.push_back(0xd2);
                    }
                    CallSlot<void>(e, 5, reinterpret_cast<void *>(uintptr_t(Bits(dt))), reinterpret_cast<void *>(uintptr_t(r())));
                    b.push_back(0xd5);
                } else if (op < 63) {
                    CallSlot<void>(e, 2, uint32_t(r() % 3));
                    b.push_back(0xd2);
                } else if (op < 71) {
                    CallSlot<void>(e, 3, uint32_t(r() % 3));
                    b.push_back(0xd3);
                } else if (op < 77) {
                    b.push_back(CallSlot<uint8_t>(e, 4));
                } else if (op < 85) {
                    fx::EffectsTransform local, parent;
                    RandomTransform(local, r);
                    RandomTransform(parent, r);
                    CallSlot<void>(e, 6, &local, &parent);
                    b.push_back(0xd6);
                } else if (op < 94) {
                    CallSlot<void>(e, 9, uint32_t(r() % 2));
                    b.push_back(0xd9);
                } else {
                    CallSlot<void>(e, 0, &factories[r() % 2]);
                    b.push_back(0xd0);
                }
                Drain(b);
                PushSequence(b, e, &description);
                PushRandom(b);
            }
            b.push_back(0x0b0000ffu);
            // The end: the deleting destructor through the count's face.
            const bool free = seed % 2 != 0;
            alignas(8) uint8_t after[sizeof(cTSSequenceEffect)];
            if (free) {
                object->mRefCount = 1;
                b.push_back(CallSlot<uint32_t>(e, 13));
            } else {
                b.push_back(uint32_t(CallSlot<void *>(static_cast<uint8_t *>(e) + 4, 0, uint32_t(0)) == e));
                std::memcpy(after, e, sizeof(after));
            }
            rec.Stop();
            Drain(b);
            size_t sizes[8] = {};
            const size_t freed = rec.SortedSizes(sizes, 8);
            b.push_back(uint32_t(freed));
            b.push_back(uint32_t(rec.Freed(e)));
            for (size_t i = 0; i < freed && i < 8; ++i) b.push_back(uint32_t(sizes[i]));
            if (!free) {
                const auto *words = reinterpret_cast<const uint32_t *>(after);
                b.push_back(TableCode(words[0]));
                b.push_back(TableCode(words[1]));
                b.push_back(uint32_t(words[3] - uint32_t(reinterpret_cast<uintptr_t>(&description))));
                b.push_back(words[4] & 0xffff);
                b.push_back(IdOf(reinterpret_cast<const void *>(uintptr_t(words[39]))));
                b.push_back(words[41] - words[40]);
                b.push_back(IdOf(reinterpret_cast<const void *>(uintptr_t(words[43]))));
                ::operator delete(e);
            }
            Drain(b);
            for (const FakeFactory &f : factories) b.push_back(f.refs);
            for (const FakeChild &c : gChildren) b.push_back(c.refs);
        }
        Same(ctx, blobs[0], blobs[1], "SequenceEffect_Slots", seed);
    }
}
