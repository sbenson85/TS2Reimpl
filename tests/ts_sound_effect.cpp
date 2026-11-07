// cTSSoundEffect (src/ts/cTSSoundEffect.h), lane WP: original vs ours.
//
// Mode 0 builds the effect with the original Create 0xc05670, mode 1 with ours; every slot then runs through the object's own
// vtable. After each step: the result, the log of every fake's calls, both vtable words and every field. At the end the
// destructor through the +4 face's deleting slot (flags 0 leaves the block to compare, flags 1 frees it), with the frees by size.
//
// Fakes: the effects world (tests/ts_effects_collection.h): the director framework hands out the sound messages (a pool node
// whose slots 5, 9, 12 and 18 are overridden here to log their arguments), and the sound renderer's probe (0x9c5b7e, original
// code both sides call) is stood in for with an owner-fake whose slot 12 takes the message. The effect's own address is its sound
// handle; the log shows it as a marker.
//
// Covered RVAs: 0xc05670 (Create), 0xc05090 (constructor), 0xc05110 (destructor), 0xc05180 (deleting destructor), 0xc05176
// (its +4 thunk), 0xc05410 (slot 2), 0xc05580 (slot 3), 0xc051a0 (slot 5), 0xc052d0 (slot 6); the shared slots through each
// side's vtable.
#include "ts_effects_collection.h"

#include "free_recorder.h"
#include "shape.h"
#include "ts/cTSCompDescription.h"
#include "ts/cTSSoundEffect.h"

#include <cstring>
#include <new>

namespace {

using namespace fxtest;

constexpr uint32_t kGameTables[2] = {0xe9132c, 0xe91368};
constexpr uint32_t kGameBaseTables[2] = {0xe8bd68, 0xda2fa8};

using CreateFn = void *(__cdecl *)(const void *);
using ConstructFn = void *(__thiscall *)(void *, const void *);

enum : uint32_t { evType = 0xc1000000, evWord = 0xc2000000, evFloat = 0xc3000000, evVector = 0xc4000000, evSend = 0xc5000000, evProbe = 0xc6000000 };

const void *gSelf = nullptr;
bool gProbeNull = false;
uint32_t Handle(uint32_t v) { return v == uint32_t(uintptr_t(gSelf)) ? 0x5e1f0000u : v; }

void __fastcall MsgSetType(FakeNode *n, void *, uint32_t type) { Ev({evType | n->id, type}); }
void __fastcall MsgSetFloat(FakeNode *n, void *, uint32_t key, float v) { Ev({evFloat | n->id, key, Bits(v)}); }
void __fastcall MsgSetWord(FakeNode *n, void *, uint32_t key, uint32_t v) { Ev({evWord | n->id, key, Handle(v)}); }
void __fastcall MsgSetVector(FakeNode *n, void *, uint32_t key, const float *v) { Ev({evVector | n->id, key, Bits(v[0]), Bits(v[1]), Bits(v[2])}); }
void __fastcall ServiceSend(FakeOwner *o, void *, FakeNode *message) { Ev({evSend | o->id, IdOf(message)}); }

FakeOwner *gService = nullptr;
void *__cdecl StandProbe()
{
    Ev({evProbe, uint32_t(!gProbeNull)});
    return gProbeNull ? nullptr : gService;
}

struct Overrides {
    void *nodeSaved[kFakeNodeSlots];
    void *ownerSaved[kFakeOwnerSlots];
    Overrides()
    {
        std::memcpy(nodeSaved, gFakeNodeVT, sizeof(nodeSaved));
        std::memcpy(ownerSaved, gFakeOwnerVT, sizeof(ownerSaved));
        gFakeNodeVT[5] = reinterpret_cast<void *>(&MsgSetType);
        gFakeNodeVT[9] = reinterpret_cast<void *>(&MsgSetFloat);
        gFakeNodeVT[12] = reinterpret_cast<void *>(&MsgSetWord);
        gFakeNodeVT[18] = reinterpret_cast<void *>(&MsgSetVector);
        gFakeOwnerVT[12] = reinterpret_cast<void *>(&ServiceSend);
    }
    ~Overrides()
    {
        std::memcpy(gFakeNodeVT, nodeSaved, sizeof(nodeSaved));
        std::memcpy(gFakeOwnerVT, ownerSaved, sizeof(ownerSaved));
    }
};

struct OurSoundTables {
    EffectVt live, destroyed;
};
const OurSoundTables &OurTables()
{
    static OurSoundTables t = [] {
        OurSoundTables v{};
        alignas(8) static uint8_t block[sizeof(cTSSoundEffect)];
        auto *e = ::new (block) cTSSoundEffect(nullptr);
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
    const OurSoundTables &ours = OurTables();
    const unsigned live = shape::FaceOf(word, 2, shape::GameTables{kGameTables}, ours.live);
    if (live != ~0u) return shape::kFaceCode | live;
    const unsigned destroyed = shape::FaceOf(word, 2, shape::GameTables{kGameBaseTables}, ours.destroyed);
    if (destroyed != ~0u) return shape::kFaceCode | 0x10 | destroyed;
    return word;
}

// All fields but the position (it is garbage until the first Update, and padding bytes with it).
void PushSound(Blob &b, const void *effect, const void *description)
{
    const auto *w = static_cast<const uint32_t *>(effect);
    b.push_back(TableCode(w[0]));
    b.push_back(TableCode(w[1]));
    b.push_back(w[2]);
    b.push_back(uint32_t(w[3] - uint32_t(reinterpret_cast<uintptr_t>(description))));
    b.push_back(w[7] & 0xff);
    b.push_back(Handle(w[8]));
    b.push_back(w[9]);
    b.push_back(w[10]);
    b.push_back(IdOf(reinterpret_cast<const void *>(uintptr_t(w[11]))));
}

void PushPosition(Blob &b, const void *effect)
{
    const auto *w = static_cast<const uint32_t *>(effect);
    for (int i = 4; i < 7; ++i) b.push_back(w[i]);
}

void Drain(Blob &b)
{
    b.insert(b.end(), gEv.begin(), gEv.end());
    gEv.clear();
}

void RandomDescription(cTSSoundDescription &d, std::mt19937 &r)
{
    d.mFlags = r();
    d.mSound = r();
    const auto value = [&r](bool allowNegative) {
        switch (r() % 6) {
        case 0: return 0.0f;
        case 1: return allowNegative ? -0.5f : 0.25f;
        case 2: return 0.5f;
        case 3: return 1.0f;
        case 4: return testhelp::FewOrUniform(r, 0.0f, 3.0f);
        default: return std::bit_cast<float>(0x7fc00000u | (r() & 0x3fffffu));
        }
    };
    d.mFloat8 = value(true);
    d.mFloatC = value(true);
    d.mFloat10 = value(false);
}

} // namespace

DIFF_TEST(SoundEffect_Construct, 0xc05090)
{
    cTSSoundDescription description;
    for (uint32_t seed = 0; seed < 40; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            Blob &b = blobs[mode];
            std::mt19937 r(seed + 1);
            alignas(8) uint8_t block[sizeof(cTSSoundEffect)];
            for (uint8_t &x : block) x = uint8_t(r());
            void *e = mode ? static_cast<void *>(::new (block) cTSSoundEffect(&description)) : ctx.Original<ConstructFn>(0xc05090)(block, &description);
            b.push_back(uint32_t(e == block));
            PushSound(b, block, &description);
            for (size_t i = 16; i < 0x1c; ++i) b.push_back(block[i]); // the position's garbage stays
            for (size_t i = 0x1d; i < 0x20; ++i) b.push_back(block[i]); // and the flag's padding
            CallSlot<void *>(block + 4, 0, uint32_t(0));
        }
        Same(ctx, blobs[0], blobs[1], "SoundEffect_Construct", seed);
    }
}

DIFF_TEST(SoundEffect_Slots, 0xc05670)
{
    const CreateFn original = ctx.Original<CreateFn>(0xc05670);
    for (uint32_t seed = 0; seed < 400; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            EffectsWorld w;
            Overrides overrides;
            shape::CodePatch probe(0x9c5b7e, reinterpret_cast<const void *>(&StandProbe));
            gService = &w.owners[1];
            gEv.clear();
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 7919u + 3);
            cTSSoundDescription description;
            RandomDescription(description, r);
            void *e = mode ? static_cast<void *>(cTSSoundEffect::Create(&description)) : original(&description);
            gSelf = e;
            auto *object = static_cast<cTSSoundEffect *>(static_cast<cITSComponentEffect *>(e));
            PushSound(b, e, &description);
            CallSlot<uint32_t>(static_cast<uint8_t *>(e) + 4, 2); // the caller's reference
            // The position is set once before anything reads it.
            {
                fx::EffectsTransform local, parent;
                RandomTransform(local, r);
                RandomTransform(parent, r);
                CallSlot<void>(e, 6, &local, &parent);
                PushPosition(b, e);
            }
            for (uint32_t step = 0; step < 40; ++step) {
                b.push_back(0x0b000000u | step);
                gProbeNull = r() % 6 == 0;
                if (r() % 3 == 0) {
                    // A sensible time step.
                    const float dt = testhelp::FewOrUniform(r, 0.0f, 1.5f);
                    CallSlot<void>(e, 5, reinterpret_cast<void *>(uintptr_t(Bits(dt))), reinterpret_cast<void *>(uintptr_t(r())));
                    b.push_back(0xd7);
                } else if (r() % 4 == 0) {
                    CallSlot<void>(e, 3, uint32_t(r() % 3));
                    b.push_back(0xd3);
                } else if (r() % 4 == 0) {
                    CallSlot<void>(e, 2, uint32_t(r()));
                    b.push_back(0xd2);
                } else {
                    EffectStep(b, e, w, r, true, true);
                }
                Drain(b);
                PushSound(b, e, &description);
                PushPosition(b, e);
            }
            b.push_back(0x0b0000ffu);
            // The end: the deleting destructor through the count's face.
            const bool free = seed % 2 != 0;
            alignas(8) uint8_t after[sizeof(cTSSoundEffect)];
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
                // Word 7 is the running byte and three bytes of padding; SoundEffect_Construct compares the padding.
                for (int i = 2; i < 12; ++i) {
                    if (i == 3) b.push_back(uint32_t(words[i] - uint32_t(reinterpret_cast<uintptr_t>(&description))));
                    else if (i == 7) b.push_back(words[i] & 0xff);
                    else if (i == 8) b.push_back(Handle(words[i]));
                    else if (i == 11) b.push_back(IdOf(reinterpret_cast<const void *>(uintptr_t(words[i]))));
                    else b.push_back(words[i]);
                }
                ::operator delete(e);
            }
            Drain(b);
            // Every counted reference the effect took has gone back.
            for (const FakeOwner &o : w.owners) b.push_back(o.refs);
            for (const FakeNode &n : w.nodes) b.push_back(n.refs);
            gSelf = nullptr;
        }
        Same(ctx, blobs[0], blobs[1], "SoundEffect_Slots", seed);
    }
}
