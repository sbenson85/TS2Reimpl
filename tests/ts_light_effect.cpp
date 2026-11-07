// cTSLightEffect and the node-effect slots it shares with the model effect (src/ts/cTSLightEffect.h, cTSEffectBase.h), lane
// WO: original vs ours.
//
// Mode 0 builds the effect with the original Create 0xc0a140, mode 1 with ours; every slot then runs through the object's
// own vtable, so mode 0 runs the image's bodies and thunks, and mode 1 ours. After each step: the result, the log of every
// fake's calls, both vtable words and every field. At the end the destructor through the +4 face's deleting slot (flags 0
// leaves the block to compare, flags 1 frees it), with the frees by size.
//
// Fakes: tests/ts_effects_collection.h (the effects world). The product and the quaternion Update uses are the real ones on
// both sides, and nGZCompGeom::MatrixProduct's and QuaternionFromMatrix's own tests cover them.
//
// Covered RVAs: 0xc0a140 (Create), 0xc09f40 (constructor), 0xc09fd0 (destructor), 0xc0a060 (deleting destructor), 0xc0a049
// (its +4 thunk), 0xc0a080 (slot 0), 0xc0a120 (slot 1), 0xc06bb0 (slot 2), 0xc06bc0 (slot 3), 0xc05ad0 (slot 5), 0xc069e0
// (slot 6), 0xc0a0e0 (slot 7), 0xc0a100 (slot 9); the bases' folded slots and the count's through each side's vtable.
#include "ts_effects_collection.h"

#include "free_recorder.h"
#include "shape.h"
#include "ts/cTSCompDescription.h"
#include "ts/cTSLightEffect.h"

#include <cstring>
#include <new>

namespace {

using namespace fxtest;

constexpr uint32_t kGameTables[2] = {0xe918dc, 0xe91918};
constexpr uint32_t kGameBaseTables[2] = {0xe8bd68, 0xda2fa8}; // the base's +0, then the count's own after its destructor

using CreateFn = void *(__cdecl *)(const void *);
using ConstructFn = void *(__thiscall *)(void *, const void *);

// Our tables, from a scratch effect: the two the constructor stores and the two the destructor leaves.
struct OurLightTables {
    EffectVt live, destroyed;
};
const OurLightTables &OurTables()
{
    static OurLightTables t = [] {
        OurLightTables v{};
        alignas(8) static uint8_t block[sizeof(cTSLightEffect)];
        auto *e = ::new (block) cTSLightEffect(nullptr);
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
    const OurLightTables &ours = OurTables();
    const unsigned live = shape::FaceOf(word, 2, shape::GameTables{kGameTables}, ours.live);
    if (live != ~0u) return shape::kFaceCode | live;
    const unsigned destroyed = shape::FaceOf(word, 2, shape::GameTables{kGameBaseTables}, ours.destroyed);
    if (destroyed != ~0u) return shape::kFaceCode | 0x10 | destroyed;
    return word;
}

void PushLight(Blob &b, const void *effect, const void *description)
{
    const auto *w = static_cast<const uint32_t *>(effect);
    b.push_back(TableCode(w[0]));
    b.push_back(TableCode(w[1]));
    PushNodeEffect(b, effect, description);
    for (int i = 7; i < 11; ++i) b.push_back(w[i]);
}

void Drain(Blob &b)
{
    b.insert(b.end(), gEv.begin(), gEv.end());
    gEv.clear();
}

} // namespace

DIFF_TEST(LightEffect_Construct, 0xc09f40)
{
    FakeDescription description{};
    for (uint32_t seed = 0; seed < 40; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            Blob &b = blobs[mode];
            std::mt19937 r(seed + 1);
            alignas(8) uint8_t block[sizeof(cTSLightEffect)];
            for (uint8_t &x : block) x = uint8_t(r());
            void *e = mode ? static_cast<void *>(::new (block) cTSLightEffect(reinterpret_cast<const cTSLightDescription *>(&description)))
                           : ctx.Original<ConstructFn>(0xc09f40)(block, &description);
            b.push_back(uint32_t(e == block));
            PushLight(b, block, &description);
            for (size_t i = 8; i < sizeof(block); ++i) b.push_back(block[i]); // the padding's garbage stays on both sides
            CallSlot<void *>(block + 4, 0, uint32_t(0));
        }
        Same(ctx, blobs[0], blobs[1], "LightEffect_Construct", seed);
    }
}

DIFF_TEST(LightEffect_Slots, 0xc0a140)
{
    FakeDescription description{};
    const CreateFn original = ctx.Original<CreateFn>(0xc0a140);
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            EffectsWorld w;
            gEv.clear();
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 7919u + 3);
            void *e = mode ? static_cast<void *>(cTSLightEffect::Create(reinterpret_cast<const cTSLightDescription *>(&description)))
                           : original(&description);
            auto *object = static_cast<cTSLightEffect *>(static_cast<cITSComponentEffect *>(e));
            PushLight(b, e, &description);
            CallSlot<uint32_t>(static_cast<uint8_t *>(e) + 4, 2);
            for (uint32_t step = 0; step < 40; ++step) {
                b.push_back(0x0b000000u | step);
                // Owners differ from their trees in the world, so a node without a parent can go under one or none.
                if (r() % 4 == 0) w.owners[r() % EffectsWorld::kOwners].kind = r() % 5;
                EffectStep(b, e, w, r, object->mNode.Get() != nullptr, object->mOwner.Get() != nullptr);
                Drain(b);
                PushLight(b, e, &description);
            }
            b.push_back(0x0b0000ffu);
            // The end: the deleting destructor through the count's face.
            const bool free = seed % 2 != 0;
            alignas(8) uint8_t after[sizeof(cTSLightEffect)];
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
                // Word 4 is the running byte and three bytes of padding; LightEffect_Construct compares the padding.
                for (int i = 2; i < 11; ++i) b.push_back(i == 3 ? uint32_t(words[i] - uint32_t(reinterpret_cast<uintptr_t>(&description))) : i == 4 ? words[i] & 0xff : words[i]);
                ::operator delete(e);
            }
            Drain(b);
            // Every counted reference the effect took has gone back.
            for (const FakeOwner &o : w.owners) b.push_back(o.refs);
            for (const FakeNode &n : w.nodes) b.push_back(n.refs);
        }
        Same(ctx, blobs[0], blobs[1], "LightEffect_Slots", seed);
    }
}
