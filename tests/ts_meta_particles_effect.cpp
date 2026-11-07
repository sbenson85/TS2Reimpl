// cTSMetaParticlesEffect (src/ts/cTSMetaParticlesEffect.h), rendering row E7: original vs ours.
//
// Mode 0 builds the effect with the original Create 0xc03910, mode 1 with ours; slots run through the object's own vtable, the
// helpers through the original's RVA (mode 0) or our member (mode 1, what the hook runs). After every step: the results, the log
// of every fake's calls, both vtable words, every byte of the effect (pointers as ids), the particles, the pool's free list and
// the game's random generator.
//
//   Meta_Effect_Construct  0xbfd4f0 in place over random bytes, every byte compared, then the destructor
//   Meta_Effect_Slots      random steps on an effect and a follower, all the fakes; the end through the deleting slot
//   Meta_Effect_Frame      slot 5 (0xbfe210, ours) on our object, emitters and draw tick running for real: emission to retirement
//   Meta_Effect_Aim        event 5 on a new effect: the targets at the two zero lengths
//   Meta_Effect_Walks      the walk's helpers and the direction angles, on their own, at both x87 precisions
//   Meta_Effect_Pool       the particle's constructor and the pool
//
// Fakes: the effects world (tests/ts_effects_collection.h) and, on it, tests/ts_meta_particles_world.h. The emitters run for real
// on both sides (their own test is tests/ts_meta_particles_place.cpp); a wild description's emitter need not end, so the Slots
// step that calls Emit tames the emission fields first (TameEmission) and skips a once-only effect that would not end.
// The particle's first values 0xc006f0 (ours, tested by Meta_Effect_Init) run for real on our object.
//
// Covered RVAs: 0xc03910 (Create), 0xbfd4f0 (constructor), 0xbfd840 (destructor), 0xbfdad0 (deleting), 0xbfdac6 (its +4 thunk),
// 0xbffe20 (slot 0), 0xc02ea0 (1), 0xc035f0 (2), 0xc03770 (3), 0xc02bb0 (6), 0xc020c0 (7), 0xc02dd0 (8), 0xc02600 (9), 0xc02080
// (10), 0xc038f0 (11), 0xc00160 (Prepare), 0xc02a80 (ChooseSourceQuadrant), 0xc026a0 (Aim), 0xc02680 (SetEvent101), 0xbff510
// (PruneSpawned), 0xbff610 (RetireParticle), 0xbffbd0 (HandParticleOn), 0xbff4a0 (NewParticle), 0xbfdc30 (AddParticle), 0xbffb30
// (Emit), 0xc047e0 (TickWalks), 0xc038e0 (MarkFollowerSet), 0xbfe190 (CollideColliders), 0xbfd4a0 (particle), 0xbfda30, 0xbfdab0
// (the pool), 0xbfdb00, 0xbfffb0, 0xc03cb0 (the walk), 0xc016f0 (the angles), 0xc04a00 (Meta_Effect_Substeps), 0xc006f0 (Meta_Effect_Init),
// 0xbdd5e0 (Meta_Effect_Ellipsoid), 0xbfe210 (slot 5, Meta_Effect_Frame).
#include "ts_meta_particles_world.h"

#include <limits>

namespace {

uint32_t gSeed = 0;

// One step, on `t` (the effect or its follower), in ways that keep both sides alive: what would run off a null pointer in both
// (a detached effect's description, an unattached effect's owner) is not asked for.
void Step(int mode, EffectsWorld &w, Effect *t, Effect *follower, std::mt19937 &r, Blob &b, uint32_t counts[])
{
    const uint32_t op = r() % 26;
    b.push_back(op);
    char mark[64];
    std::snprintf(mark, sizeof(mark), "seed %u mode %d effect %d op %u", gSeed, mode, t == gEffects[0] ? 0 : 1, op);
    gCtx->Mark(mark);
    uint32_t result = 0xfeed;
    switch (op) {
    case 0:
        CallSlot<void>(t, 2, r() % 2 ? 0u : uint32_t(r()));
        break;
    case 1:
        CallSlot<void>(t, 3, r() % 2 ? 0u : uint32_t(r()));
        break;
    case 2: {
        EffectsTransform local, parent;
        RandomTransform(local, r);
        RandomTransform(parent, r);
        if (r() % 2) parent.flags |= EffectsTransform::kTranslated;
        CallSlot<void>(t, 6, &local, &parent);
        break;
    }
    case 3:
    case 4:
    case 5: {
        static const uint32_t kinds[] = {0, 1, 2, 3, 4, 5, 5, 5, 6, 7, 7, 8, 9, 9, 10, 11, 12, 13, 14, 14, 14, 15, 0x100, 0x101, 0x102, 0xffffffffu, 0x80000000u};
        const uint32_t kind = kinds[r() % std::size(kinds)];
        float values[30];
        for (float &v : values) v = r() % 8 ? Tame(r) : Wild(r);
        if (r() % 4 == 0) values[3] = values[4] = values[5] = values[1] = 0.0f;
        if (kind == 14 && r() % 2) {
            for (int i = 0; i < 3; ++i) values[6 + i] = values[3 + i]; // two points alike: no direction at the first
        }
        uintptr_t count = r();
        if (kind == 14) count = r() % 4 ? r() % 31 : uintptr_t(0) - r() % 12;
        if (kind == 14 && r() % 3 == 0) { // two points about the 1e-6 under which a piece has no direction
            static const float gaps[] = {0.5e-6f, 1e-6f, 1.5e-6f, 2e-6f, 5e-6f, 9e-6f};
            for (float &v : values) v = 0.0f;
            values[3] = gaps[r() % std::size(gaps)];
            count = 6;
        }
        result = CallSlot<uint8_t>(t, 7, kind, static_cast<void *>(values), reinterpret_cast<void *>(count));
        ++counts[0];
        break;
    }
    case 6: {
        struct FakeQuery {
            void *const *vt;
            cIGZString *answer;
            static uint32_t __fastcall Interface(FakeQuery *q, void *, uint32_t iid, void **out)
            {
                Ev({evQuery, iid});
                if (q->answer) {
                    q->answer->AddRef();
                    *out = q->answer;
                }
                return q->answer != nullptr;
            }
        };
        static void *const vt[1] = {reinterpret_cast<void *>(&FakeQuery::Interface)};
        alignas(8) uint8_t storage[sizeof(cRZString)];
        auto *text = reinterpret_cast<cRZString *>(storage);
        static const char *const names[] = {"", "tex_a", "TEX_A", "a_name_long_enough_to_leave_the_small_buffer"};
        text->Construct(names[r() % 4]);
        cIGZString *face = text->AsInterface();
        face->AddRef();
        FakeQuery query{vt, r() % 4 ? face : nullptr};
        const uint32_t key = r() % 3;
        if (key == 1) result = CallSlot<uint8_t>(t, 8, reinterpret_cast<void *>(uintptr_t(1)), static_cast<void *>(&gEffectFakes[r() % 6]));
        else if (key == 0) result = CallSlot<uint8_t>(t, 8, static_cast<void *>(nullptr), static_cast<void *>(&query));
        else result = CallSlot<uint8_t>(t, 8, reinterpret_cast<void *>(uintptr_t(2 + r() % 4)), static_cast<void *>(&query));
        b.push_back(text->mRefCount);
        text->Destruct();
        break;
    }
    case 7:
        CallSlot<void>(t, 9, r() % 2 != 0);
        break;
    case 8:
        if (t->mAttached && gDescriptions[2]) {
            gNextDescription = gDescriptions[r() % 3];
            t->mCollidersPlaced = true; // the step's mark: Prepare clears it when the description has colliders
            CallSlot<void>(t, 11, shape::P(&gDescriptionFace));
        }
        break;
    case 9:
    case 10:
    case 11: {
        Particle *const p = TakeParticle(r);
        if (!p) break;
        if (mode) t->AddParticle(p);
        else Helper<void(__thiscall *)(void *, Particle *)>(0xbfdc30, t, p);
        break;
    }
    case 12: {
        if (!t->mOwner.Get()) break; // a death effect would ask the null owner
        Particle *const p = TakeParticle(r);
        if (!p) break;
        if (mode) t->RetireParticle(p);
        else Helper<void(__thiscall *)(void *, Particle *)>(0xbff610, t, p);
        ++counts[1];
        break;
    }
    case 13: {
        if (!t->mFollower.Get() || !t->mOwner.Get()) break;
        Particle *const p = TakeParticle(r);
        if (!p) break;
        if (mode) t->HandParticleOn(p);
        else Helper<void(__thiscall *)(void *, Particle *)>(0xbffbd0, t, p);
        ++counts[2];
        break;
    }
    case 14:
        if (mode) t->PruneSpawned();
        else Helper<void(__thiscall *)(void *)>(0xbff510, t);
        break;
    case 15: {
        if (!t->mOwner.Get()) break;
        Particle *const p = mode ? t->NewParticle() : gCtx->Original<Particle *(__thiscall *)(void *)>(0xbff4a0)(t);
        b.push_back(ArenaOffset(p));
        if (r() % 2) {
            if (mode) t->AddParticle(p);
            else Helper<void(__thiscall *)(void *, Particle *)>(0xbfdc30, t, p);
        }
        ++counts[3];
        break;
    }
    case 16: {
        const float step = r() % 4 == 0 ? 0.0f : Positive(r);
        if (r() % 4 == 0) t->mDelay = step; // the delay runs out on this very step
        // The emitters run for real on both sides: the emission's own fields are made ones they end on for this call.
        const EmissionSave saved = TameEmission(t, r);
        if (t->mCount <= 100) {
            if (mode) t->Emit(step);
            else Helper<void(__thiscall *)(void *, float)>(0xbffb30, t, step);
            ++counts[4];
        }
        RestoreEmission(t, saved);
        break;
    }
    case 17: {
        const float step = r() % 4 == 0 ? 0.0f : Positive(r);
        if (mode) t->TickWalks(step);
        else Helper<void(__thiscall *)(void *, float)>(0xc047e0, t, step);
        break;
    }
    case 18: {
        if (!t->mOwner.Get()) break;
        Particle *const p = TakeParticle(r);
        if (!p) break;
        p->mSizeVary = Wild(r);
        const Vector3 move = {Wild(r), Wild(r), Wild(r)};
        gScratchBase = t->mColliderState.data();
        const bool lives = mode ? t->CollideColliders(p, &move) : gCtx->Original<bool(__thiscall *)(void *, Particle *, const Vector3 *)>(0xbfe190)(t, p, &move);
        result = lives;
        ++counts[4];
        auto *effect = reinterpret_cast<FakeNode **>(reinterpret_cast<uint8_t *>(p) + offsetof(Particle, mEffect));
        if (*effect) --(*effect)->refs;
        *effect = nullptr;
        break;
    }
    case 19: {
        auto *pair = reinterpret_cast<float *>(gSettings.mBefore);
        pair[0] = Wild(r);
        pair[1] = Wild(r);
        const uint32_t quadrant = r() % 3 ? r() % 6 : r();
        if (mode) t->ChooseSourceQuadrant(quadrant);
        else Helper<void(__thiscall *)(void *, uint32_t)>(0xc02a80, t, quadrant);
        break;
    }
    case 20:
        if (!t->mAttached) break;
        if (mode) t->Prepare();
        else Helper<void(__thiscall *)(void *)>(0xc00160, t);
        break;
    case 21:
        result = CallSlot<uint8_t>(t, 4);
        break;
    case 22:
        if (follower && t != follower && r() % 2) {
            CallSlot<void>(t, 10, shape::P(follower));
        } else if (t->mAttached) {
            CallSlot<void>(t, 0, static_cast<void *>(&w.owners[1])); // attached: nothing happens
        }
        break;
    case 23: { // a spawned effect by hand, as a particle would leave it
        if (t->mSpawned.size() > 6) break;
        FakeNode *const effect = &gEffectFakes[r() % 6];
        t->mSpawned.push_back(fx::CountedRef<cIGZUnknown>(reinterpret_cast<cIGZUnknown *>(effect)));
        gEv.clear();
        break;
    }
    case 24: { // the emitter's own scale of things: a running walk's clock run out
        for (cTSMetaWalk &walk : t->mWalks) walk.mClock = r() % 2 ? Wild(r) : -Positive(r);
        break;
    }
    case 25: {
        Vector3 force = {Wild(r), Wild(r), Wild(r)};
        cTSMetaWalk &walk = t->mWalks[r() % 2];
        if (mode) walk.Apply(&force);
        else Helper<void(__thiscall *)(void *, Vector3 *)>(0xbfdb00, &walk, &force);
        b.insert(b.end(), {Bits(force.x), Bits(force.y), Bits(force.z)});
        break;
    }
    }
    b.push_back(result);
}

} // namespace

DIFF_TEST(Meta_Effect_Construct, kConstruct)
{
    gCtx = &ctx;
    Description description;
    gDescriptions[0] = &description;
    for (uint32_t seed = 0; seed < 24; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            Blob &b = blobs[mode];
            std::mt19937 r(seed + 1);
            alignas(16) uint8_t block[sizeof(Effect)];
            for (uint8_t &x : block) x = uint8_t(r());
            void *e = mode ? static_cast<void *>(::new (block) Effect(&description)) : ctx.Original<ConstructFn>(kConstruct)(block, &description);
            b.push_back(uint32_t(e == block));
            PushEffectState(b, reinterpret_cast<Effect *>(block));
            difftest::FreeRecorder rec;
            CallSlot<void *>(block + 4, 0, uint32_t(0));
            rec.Stop();
            size_t sizes[4] = {};
            const size_t freed = rec.SortedSizes(sizes, 4);
            b.push_back(uint32_t(freed));
            for (size_t i = 0; i < freed && i < 4; ++i) b.push_back(uint32_t(sizes[i]));
            b.push_back(TableCode(shape::Word(block, 0)));
            b.push_back(TableCode(shape::Word(block, 4)));
        }
        Same(ctx, blobs[0], blobs[1], "Meta_Effect_Construct", seed);
    }
}

DIFF_TEST(Meta_Effect_Slots, kCreate)
{
    gCtx = &ctx;
    MetaWorld world;
    gEv.reserve(1 << 20);
    uint32_t counts[5] = {};
    for (uint32_t seed = 0; seed < 600; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            char mark[64];
            gSeed = seed;
            std::snprintf(mark, sizeof(mark), "slots seed %u mode %d", seed, mode);
            ctx.Mark(mark);
            std::unique_ptr<Precision> precision;
            if (seed % 2) precision = std::make_unique<Precision>();
            EffectsWorld w;
            gEv.clear();
            ResetWorld(seed, w);
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 7919u + 3);
            auto first = std::make_unique<Description>();
            auto second = std::make_unique<Description>();
            auto third = std::make_unique<Description>();
            const auto low = [&r] {
                uint32_t f = 0;
                for (uint32_t bit : {1u, 2u, 4u, 0x40u, 0x1000u, 0x2000u, 0x4000u, 0x8000u, 0x800000u, 0x2000000u})
                    if (r() % 3 == 0) f |= bit;
                return f;
            };
            const auto high = [&r] {
                uint32_t f = 0;
                for (uint32_t bit : {1u, 0x40u})
                    if (r() % 3 == 0) f |= bit;
                return f;
            };
            RandomDescription(*first, r, false, low(), high());
            RandomDescription(*second, r, false, low(), high());
            RandomDescription(*third, r, false, low(), high());
            gDescriptions[0] = first.get();
            gDescriptions[1] = second.get();
            gDescriptions[2] = third.get();
            gHaveSurface = r() % 2 != 0;
            for (float &v : gSurfaceWords) v = Wild(r);
            FillPool(32, r);

            void *const e = MakeEffect(mode, first.get());
            void *const f = MakeEffect(mode, third.get());
            auto *object = static_cast<Effect *>(static_cast<cITSComponentEffect *>(e));
            auto *follower = static_cast<Effect *>(static_cast<cITSComponentEffect *>(f));
            gEffects[0] = object;
            gEffects[1] = follower;
            CallSlot<uint32_t>(e, 12); // the test's references
            CallSlot<uint32_t>(f, 12);
            Fingerprint(b);
            // Some steps before the attach: Start and Stop read the description only.
            if (seed % 5 == 0) {
                CallSlot<void>(e, 2, 0u);
                Fingerprint(b);
                CallSlot<void>(e, 3, 0u);
                Fingerprint(b);
            }
            CallSlot<void>(e, 0, static_cast<void *>(&w.owners[0]));
            CallSlot<void>(f, 0, static_cast<void *>(&w.owners[0]));
            Fingerprint(b);
            if (r() % 2) {
                CallSlot<void>(e, 10, shape::P(f));
                Fingerprint(b);
            }
            if (r() % 2) { // as the step leaves it: a node under a tree or not
                object->mNode.Assign(reinterpret_cast<cIGZUnknown *>(&w.nodes[0]));
                if (r() % 2) w.nodes[0].parent = &w.trees[0];
                Fingerprint(b);
            }
            for (uint32_t step = 0; step < 48; ++step) {
                b.push_back(0x0b000000u | step);
                Step(mode, w, r() % 3 == 0 ? follower : object, follower, r, b, counts);
                Fingerprint(b);
            }
            b.push_back(0x0b0000feu);
            if (r() % 2) { // Detach is the end of an effect: everything but the destructor needs its description
                CallSlot<void>(e, 1);
                Fingerprint(b);
                CallSlot<void>(e, 1);
                Fingerprint(b);
            }
            b.push_back(0x0b0000ffu);
            // The end: through the +4 face's deleting slot or the count's last release.
            gEffects[0] = nullptr;
            alignas(16) static uint8_t after[sizeof(Effect)];
            const bool free = seed % 2 != 0;
            {
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
                size_t sizes[16] = {};
                const size_t freed = rec.SortedSizes(sizes, 16);
                b.push_back(uint32_t(freed));
                b.push_back(uint32_t(rec.Freed(e)));
                for (size_t i = 0; i < freed && i < 16; ++i) b.push_back(uint32_t(sizes[i]));
            }
            if (!free) {
                b.push_back(TableCode(shape::Word(after, 0)));
                b.push_back(TableCode(shape::Word(after, 4)));
                ::operator delete(e);
            }
            gEffects[1] = nullptr;
            follower->mRefCount = 1;
            b.push_back(CallSlot<uint32_t>(f, 13));
            Drain(b);
            PushPool(b);
            // Every counted reference has gone back.
            for (const FakeOwner &o : w.owners) b.push_back(o.refs);
            for (const FakeNode &n : w.nodes) b.push_back(n.refs);
            for (const FakeNode *n : {&gSurface, &gColliders[0], &gColliders[1], &gEffectFakes[0], &gEffectFakes[1], &gEffectFakes[2], &gEffectFakes[3], &gEffectFakes[4], &gEffectFakes[5]}) b.push_back(n->refs);
            *GameAddress<void *>(kPoolFreeCell) = nullptr;
        }
        Same(ctx, blobs[0], blobs[1], "Meta_Effect_Slots", seed);
    }
    for (uint32_t i = 0; i < 5; ++i)
        if (counts[i] < 100) ctx.Fail("op group %u ran %u times", i, counts[i]);
}

// Slot 5, still the game's 0xbfe210, on our object: the game's emitters, integrators and retirement run on what our constructor and
// helpers leave. The per-particle draw tick 0xc03ea0 is stood in for on both sides (row E9).

DIFF_TEST(Meta_Effect_Frame, 0xbfe210)
{
    gCtx = &ctx;
    MetaWorld world;
    gEv.reserve(1 << 20);
    int framesWithParticles = 0; // a frame with none compares nothing
    for (uint32_t seed = 0; seed < 120; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            gSeed = seed;
            char mark[64];
            std::snprintf(mark, sizeof(mark), "frame seed %u mode %d", seed, mode);
            ctx.Mark(mark);
            EffectsWorld w;
            gEv.clear();
            ResetWorld(seed, w);
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 104729u + 11);
            auto first = std::make_unique<Description>();
            uint32_t low = 0;
            for (uint32_t bit : {1u, 2u, 0x800000u, 0x2000000u})
                if (r() % 3 == 0) low |= bit;
            RandomDescription(*first, r, true, low, 0);
            RandomLook(*first, r, false);
            first->mGlobalWind = r() % 2 ? 0.0f : Tame(r);
            if (r() % 2) RandomColliders(*first, r, 1 + r() % 3);
            gCollidersFound = true;
            gDescriptions[0] = first.get();
            void *const e = MakeEffect(mode, first.get());
            auto *object = static_cast<Effect *>(static_cast<cITSComponentEffect *>(e));
            gEffects[0] = object;
            gEffects[1] = nullptr;
            CallSlot<uint32_t>(e, 12);
            CallSlot<void>(e, 0, static_cast<void *>(&w.owners[1]));
            EffectsTransform local{}, parent{};
            local.scale = 1.0f;
            parent.scale = float(1 + r() % 3) * 0.5f;
            local.translation = {Tame(r), Tame(r), Tame(r)};
            parent.translation = {Tame(r), Tame(r), Tame(r)};
            local.m[0][0] = local.m[1][1] = local.m[2][2] = 1.0f;
            parent.m[0][0] = parent.m[1][1] = parent.m[2][2] = 1.0f;
            CallSlot<void>(e, 6, &local, &parent);
            CallSlot<void>(e, 2, r() % 2);
            Fingerprint(b);
            uint32_t stats[8] = {};
            for (uint32_t frame = 0; frame < 32; ++frame) {
                b.push_back(0x0b000000u | frame);
                const float step = float(1 + r() % 8) / 32.0f;
                if (!object->mRunning) break; // the scene steps a running effect only: Start may not have run its body
                auto *const wind = reinterpret_cast<float *>(gSettings.mBefore);
                for (int i = 0; i < 3; ++i) wind[i] = Tame(r);
                if (r() % 5 == 0) object->mPrerollPending = true; // the preroll with particles in the list
                if (r() % 4 == 0) object->mFollowerSet = !object->mFollowerSet; // a handed-on effect emits nothing
                CallSlot<void>(e, 5, reinterpret_cast<void *>(uintptr_t(Bits(step))), static_cast<void *>(stats));
                framesWithParticles += object->mCount > 0;
                b.insert(b.end(), std::begin(stats), std::end(stats));
                Fingerprint(b);
                if (r() % 8 == 0) {
                    CallSlot<void>(e, 3, r() % 4 ? 0u : 1u);
                    Fingerprint(b);
                }
            }
            gEffects[0] = nullptr;
            object->mRefCount = 1;
            b.push_back(CallSlot<uint32_t>(e, 13));
            Drain(b);
            PushPool(b);
            for (const FakeOwner &o : w.owners) b.push_back(o.refs);
            *GameAddress<void *>(kPoolFreeCell) = nullptr;
            gCollidersFound = false;
        }
        Same(ctx, blobs[0], blobs[1], "Meta_Effect_Frame", seed);
    }
    if (framesWithParticles < 500) ctx.Fail("only %d frames had particles", framesWithParticles);
}

// Aim on a new effect: no force, no placement, so these targets reach the two zero lengths Meta_Effect_Slots' random ones miss.
DIFF_TEST(Meta_Effect_Aim, 0xc026a0)
{
    gCtx = &ctx;
    MetaWorld world;
    const float nan = std::bit_cast<float>(0x7fc12345u);
    const Vector3 targets[] = {{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},  {0.0f, -2.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
                               {0.0f, 0.0f, 1.0f}, {-0.0f, 0.0f, 0.0f}, {nan, 0.0f, 0.0f},   {0.0f, nan, 0.0f}};
    for (uint32_t seed = 0; seed < uint32_t(std::size(targets)) * 4; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            ctx.Mark("aim");
            EffectsWorld w;
            gEv.clear();
            ResetWorld(seed, w);
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 7919u + 3);
            auto first = std::make_unique<Description>();
            RandomDescription(*first, r, true, 0, 0);
            gDescriptions[0] = first.get();
            void *const e = MakeEffect(mode, first.get());
            auto *object = static_cast<Effect *>(static_cast<cITSComponentEffect *>(e));
            gEffects[0] = object;
            gEffects[1] = nullptr;
            CallSlot<uint32_t>(e, 12);
            CallSlot<void>(e, 0, static_cast<void *>(&w.owners[1]));
            Vector3 target = targets[seed % std::size(targets)];
            b.push_back(CallSlot<uint8_t>(e, 7, 5u, static_cast<void *>(&target), nullptr));
            Fingerprint(b);
            gEffects[0] = nullptr;
            object->mRefCount = 1;
            b.push_back(CallSlot<uint32_t>(e, 13));
            Drain(b);
            *GameAddress<void *>(kPoolFreeCell) = nullptr;
        }
        Same(ctx, blobs[0], blobs[1], "Meta_Effect_Aim", seed);
    }
}

DIFF_TEST(Meta_Effect_Walks, 0xc03cb0)
{
    gCtx = &ctx;
    using AnglesFn = void(__cdecl *)(const Vector3 *, float *, float *);
    using InitFn = void(__thiscall *)(void *, const cTSParticleWalk *, const Vector3 *, bool);
    using NextFn = void(__thiscall *)(void *, const cTSParticleWalk *);
    using ApplyFn = void(__thiscall *)(const void *, Vector3 *);
    uint32_t flat = 0, down = 0, up = 0;
    for (uint32_t seed = 0; seed < 8000; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            std::unique_ptr<Precision> precision;
            if (seed % 2) precision = std::make_unique<Precision>();
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 31u + 5);
            auto *random = GameAddress<cRZRandom>(kRandomCell);
            random->mIntegerSeed = seed * 2654435761u + 12345u;
            random->mRealSeed = int32_t(seed * 40503u + 77u);
            Vector3 direction = {Wild(r), Wild(r), Wild(r)};
            if (r() % 4 == 0) direction.x = direction.y = r() % 2 ? 0.0f : 1e-4f;
            if (r() % 4 == 0) { // about the 1e-6 of the squares that counts as straight up
                direction.x = r() % 4 ? 1e-3f * (0.8f + float(r() % 41) / 100.0f) : std::sqrt(1e-6f);
                direction.y = 0.0f;
            }
            if (r() % 8 == 0) direction.z = r() % 2 ? 0.0f : -0.0f;
            cTSParticleWalk walk;
            walk.mDelay = {Wild(r), Wild(r)};
            walk.mStrength = {Wild(r), Wild(r)};
            walk.mTurn[0] = Wild(r);
            walk.mTurn[1] = Wild(r);
            static const float kMixes[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 0.4999999f, 0.99999994f};
            walk.mMix = r() % 6 == 0 ? Wild(r) : kMixes[r() % 7];
            float heading = 7.0f, pitch = 7.0f;
            if (mode) fx::MetaDirectionAngles(&direction, &heading, &pitch);
            else ctx.Original<AnglesFn>(0xc016f0)(&direction, &heading, &pitch);
            b.insert(b.end(), {Bits(heading), Bits(pitch)});
            if (heading == 0.0f && pitch == 7.0f) ++flat;
            down += pitch < 0.0f;
            up += pitch > 0.0f;
            cTSMetaWalk state;
            for (float *field : {&state.mClock, &state.mVertical, &state.mHorizontal, &state.mHeading, &state.mPitch}) *field = Wild(r);
            const bool randomStart = r() % 2 != 0;
            if (mode) state.Init(&walk, &direction, randomStart);
            else ctx.Original<InitFn>(0xbfffb0)(&state, &walk, &direction, randomStart);
            const auto push = [&] {
                const auto *w = reinterpret_cast<const uint32_t *>(&state);
                b.insert(b.end(), w, w + 5);
                b.push_back(random->mIntegerSeed);
                b.push_back(uint32_t(random->mRealSeed));
            };
            push();
            for (int i = 0; i < 3; ++i) {
                if (mode) state.Next(&walk);
                else ctx.Original<NextFn>(0xc03cb0)(&state, &walk);
                push();
                Vector3 force = {Wild(r), Wild(r), Wild(r)};
                if (r() % 3 == 0) state.mVertical = 0.0f;
                if (r() % 3 == 0) state.mHorizontal = r() % 2 ? 0.0f : -0.0f;
                if (mode) state.Apply(&force);
                else ctx.Original<ApplyFn>(0xbfdb00)(&state, &force);
                b.insert(b.end(), {Bits(force.x), Bits(force.y), Bits(force.z)});
            }
        }
        Same(ctx, blobs[0], blobs[1], "Meta_Effect_Walks", seed);
    }
    if (down < 100 || up < 100) ctx.Fail("angles: %u down, %u up", down, up);
    (void)flat;
}

DIFF_TEST(Meta_Effect_Pool, 0xbfda30)
{
    gCtx = &ctx;
    shape::StandIn alloc{kMalloc, &RZMemory::Malloc, &ArenaMalloc};
    using AllocateFn = void *(__cdecl *)(size_t);
    using FreeFn = void(__cdecl *)(void *);
    using ConstructParticleFn = void *(__thiscall *)(void *);
    uint32_t refills = 0;
    gEv.reserve(1 << 16);
    for (uint32_t seed = 0; seed < 400; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            EffectsWorld w;
            gEv.clear();
            ResetWorld(seed, w);
            Blob &b = blobs[mode];
            std::mt19937 r(seed + 1);
            std::vector<void *> held;
            for (uint32_t i = 0; i < 64; ++i) {
                const uint32_t op = r() % 5;
                if (op < 3) {
                    void *const p = mode ? Particle::Allocate(sizeof(Particle)) : ctx.Original<AllocateFn>(0xbfda30)(sizeof(Particle));
                    b.push_back(ArenaOffset(p));
                    held.push_back(p);
                    if (op == 2) {
                        std::memset(p, 0xee, sizeof(Particle));
                        void *const made = mode ? static_cast<void *>(::new (p) Particle()) : ctx.Original<ConstructParticleFn>(0xbfd4a0)(p);
                        b.push_back(uint32_t(made == p));
                        const auto *words = static_cast<const uint32_t *>(p);
                        b.insert(b.end(), words, words + sizeof(Particle) / 4);
                    }
                } else if (op == 3 && !held.empty()) {
                    const size_t at = r() % held.size();
                    if (mode) Particle::Free(held[at]);
                    else ctx.Original<FreeFn>(0xbfdab0)(held[at]);
                    held.erase(held.begin() + at);
                } else {
                    if (mode) Particle::Free(nullptr);
                    else ctx.Original<FreeFn>(0xbfdab0)(nullptr);
                }
                PushPool(b);
                b.push_back(ArenaOffset(*GameAddress<void *>(kPoolListCell)) + 1);
                if (gEv.size()) ++refills;
                Drain(b);
            }
            // The blocks' chain and the words in the arena.
            b.insert(b.end(), reinterpret_cast<const uint32_t *>(gArena), reinterpret_cast<const uint32_t *>(gArena + gArenaUsed));
            *GameAddress<void *>(kPoolFreeCell) = nullptr;
        }
        Same(ctx, blobs[0], blobs[1], "Meta_Effect_Pool", seed);
    }
    if (refills < 100) ctx.Fail("the pool grew %u times", refills);
}

// The particle's motion (rendering row E8): the path code, on a particle of its own and an effect with a local transform, a
// path (its own or the description's) and the description's flags. Every word of the particle and the effect is compared.
DIFF_TEST(Meta_Effect_Motion, 0xbfed50)
{
    gCtx = &ctx;
    using SteerFn = void(__thiscall *)(Effect *, Particle *, float);
    using FollowFn = bool(__thiscall *)(Effect *, Particle *, float);
    using BackFn = void(__thiscall *)(const EffectsTransform *, Vector3 *);
    uint32_t straight = 0, reset = 0, surfaceZ = 0, followTrue = 0, followFalse = 0, flat = 0, rotated = 0;
    MetaWorld world;
    for (uint32_t seed = 0; seed < 6000; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            ctx.Mark("motion");
            EffectsWorld w;
            gEv.clear();
            ResetWorld(seed, w);
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 6007u + 11);
            const auto pathPoints = [&](std::vector<cTSParticlePathPoint> &points) {
                points.clear();
                float time = 0.0f;
                const unsigned n = 1 + r() % 4;
                for (unsigned i = 0; i < n; ++i) {
                    cTSParticlePathPoint p;
                    p.mPosition = {Wild(r), Wild(r), Wild(r)};
                    p.mVelocity = {Wild(r), Wild(r), Wild(r)};
                    time += r() % 6 == 0 ? 0.0f : 0.25f * float(1 + r() % 4);
                    p.mTime = r() % 12 == 0 ? Wild(r) : r() % 10 == 0 ? 0.0f : time;
                    points.push_back(p);
                }
            };
            auto first = std::make_unique<Description>();
            const bool edge = seed % 21 == 7;    // the tractor's distance exactly the reach: (3, 4, 0) from the origin, 5 * 1
            const bool nans = seed % 21 == 10;   // two NaNs in one product: the first operand's payload wins
            RandomDescription(*first, r, true, !edge && r() % 2 ? 0x8000u : 0u, 0);
            first->mTractorResetSpeed = r() % 3 == 0 ? 0.0f : r() % 4 == 0 ? Wild(r) : 0.5f * float(r() % 9);
            pathPoints(first->mPath.mPoints);
            if (edge) {
                first->mTractorResetSpeed = 5.0f;
                first->mPath.mPoints.assign(1, cTSParticlePathPoint{});
                first->mPath.mPoints[0].mPosition = {3.0f, 4.0f, 0.0f};
                first->mPath.mPoints[0].mTime = 1.0f;
            }
            if (nans) {
                first->mPath.mPoints.assign(1, cTSParticlePathPoint{});
                first->mPath.mPoints[0].mVelocity = {std::bit_cast<float>(0x7fc00123u), 0.0f, 0.0f};
                first->mPath.mPoints[0].mTime = 1.0f;
            }
            gDescriptions[0] = first.get();
            void *const e = MakeEffect(mode, first.get());
            auto *object = static_cast<Effect *>(static_cast<cITSComponentEffect *>(e));
            gEffects[0] = object;
            gEffects[1] = nullptr;
            CallSlot<uint32_t>(e, 12);
            CallSlot<void>(e, 0, static_cast<void *>(&w.owners[1]));
            RandomTransform(object->mLocal, r);
            RandomTransform(object->mWorld, r);
            object->mSurfaceBase = Wild(r);
            if (r() % 2) pathPoints(object->mPath);
            rotated += (object->mLocal.flags & EffectsTransform::kRotated) != 0;
            uint32_t words[sizeof(Particle) / 4] = {};
            auto *p = reinterpret_cast<Particle *>(words);
            const auto &list = object->mPath.empty() ? first->mPath.mPoints : object->mPath;
            p->mLife = r() % 2 ? 1.0f : Wild(r);
            p->mAge = r() % 3 == 0 ? Wild(r) : r() % 3 == 0 ? list.back().mTime + 0.125f : float(r() % 20) * 0.125f;
            p->mPosition = {Wild(r), Wild(r), Wild(r)};
            p->mVelocity = {Wild(r), Wild(r), Wild(r)};
            float step = r() % 8 == 0 ? Wild(r) : float(1 + r() % 4) * 0.0625f;
            if (edge) {
                object->mLocal = EffectsTransform{}, object->mLocal.scale = 1.0f;
                object->mPath.clear();
                p->mAge = 0.5f;
                p->mLife = 1.0f;
                p->mPosition = {0.0f, 0.0f, 0.0f};
                step = 0.5f;
            }
            if (nans) {
                object->mLocal = EffectsTransform{}, object->mLocal.scale = 1.0f;
                object->mPath.clear();
                p->mAge = 0.5f;
                step = std::bit_cast<float>(0x7fc00456u);
            }
            const float age = p->mAge;
            const uint32_t op = seed % 3;
            b.push_back(op);
            if (op == 0) {
                Vector3 v = {Wild(r), Wild(r), Wild(r)};
                if (mode) fx::MetaRotateBack(&object->mWorld, nullptr, &v);
                else ctx.Original<BackFn>(0xbff400)(&object->mWorld, &v);
                b.insert(b.end(), {Bits(v.x), Bits(v.y), Bits(v.z)});
                flat += (object->mWorld.flags & EffectsTransform::kRotated) == 0;
            } else if (op == 1) {
                const Vector3 was = p->mPosition;
                if (mode) object->Steer(p, step);
                else ctx.Original<SteerFn>(0xbfed50)(object, p, step);
                straight += p->mPosition.x == fx::Add(fx::Mul(p->mVelocity.x, step), was.x);
                reset += Bits(p->mAge) != Bits(age);
                surfaceZ += (first->mFlags & 0x8000) != 0;
            } else {
                bool result;
                if (mode) result = object->FollowPath(p, step);
                else result = ctx.Original<FollowFn>(0xc02f30)(object, p, step);
                b.push_back(result);
                followTrue += result;
                followFalse += !result;
            }
            b.insert(b.end(), words, words + sizeof(Particle) / 4);
            Fingerprint(b);
            gEffects[0] = nullptr;
            object->mRefCount = 1;
            b.push_back(CallSlot<uint32_t>(e, 13));
            Drain(b);
            *GameAddress<void *>(kPoolFreeCell) = nullptr;
        }
        Same(ctx, blobs[0], blobs[1], "Meta_Effect_Motion", seed);
    }
    if (straight < 100 || reset < 50 || surfaceZ < 500 || followTrue < 300 || followFalse < 100 || flat < 300 || rotated < 1000)
        ctx.Fail("coverage: %u straight, %u reset, %u surfaceZ, %u/%u follow, %u flat, %u rotated", straight, reset, surfaceZ, followTrue,
                 followFalse, flat, rotated);
}

// One step of a particle's life (rendering row E8, 0xc04840): the age and its edge, the path, the plain Euler step and the drag that
// sends it to the sub-stepped integrator (0xc04a00, ours; Meta_Effect_Substeps).
DIFF_TEST(Meta_Effect_Integrate, 0xc04840)
{
    gCtx = &ctx;
    using IntegrateFn = bool(__thiscall *)(Effect *, Particle *, float);
    uint32_t died = 0, followed = 0, plain = 0, substeps = 0, dragEdge = 0, ageEdge = 0;
    MetaWorld world;
    for (uint32_t seed = 0; seed < 6000; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            ctx.Mark("integrate");
            EffectsWorld w;
            gEv.clear();
            ResetWorld(seed, w);
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 6011u + 13);
            const bool ageEdgeCase = seed % 7 == 0, dragEdgeCase = seed % 7 == 1;
            uint32_t low = 0;
            if (!dragEdgeCase) {
                if (r() % 4 == 0) low |= 0x40000000u;
                if (r() % 4 == 0) low |= 0x20000000u;
                if (r() % 4 == 0) low |= 0x1000000u;
                if (r() % 4 == 0) low |= 0x20u;
                if (r() % 4 == 0) low |= 0x40u;
            }
            auto first = std::make_unique<Description>();
            RandomDescription(*first, r, true, low, 0);
            first->mDrag = dragEdgeCase ? 0.1f : r() % 3 == 0 ? 0.0f : r() % 3 == 0 ? Wild(r) : float(r() % 5) * 0.03f;
            first->mPath.mPoints.assign(1, cTSParticlePathPoint{});
            first->mPath.mPoints[0].mPosition = {Wild(r), Wild(r), Wild(r)};
            first->mPath.mPoints[0].mTime = 1.0f;
            gDescriptions[0] = first.get();
            void *const e = MakeEffect(mode, first.get());
            auto *object = static_cast<Effect *>(static_cast<cITSComponentEffect *>(e));
            gEffects[0] = object;
            gEffects[1] = nullptr;
            CallSlot<uint32_t>(e, 12);
            CallSlot<void>(e, 0, static_cast<void *>(&w.owners[1]));
            RandomTransform(object->mLocal, r);
            RandomTransform(object->mWorld, r);
            object->mForce = {Wild(r), Wild(r), Wild(r)};
            uint32_t words[sizeof(Particle) / 4] = {};
            auto *p = reinterpret_cast<Particle *>(words);
            p->mAlive = 1;
            p->mLife = r() % 4 == 0 ? Wild(r) : 1.0f;
            p->mAge = r() % 4 == 0 ? Wild(r) : float(r() % 6) * 0.125f;
            p->mPosition = {Wild(r), Wild(r), Wild(r)};
            p->mVelocity = {Wild(r), Wild(r), Wild(r)};
            float step = r() % 8 == 0 ? Wild(r) : float(1 + r() % 4) * 0.0625f;
            if (ageEdgeCase) {
                p->mAge = 0.5f;
                p->mLife = 0.75f;
                step = 0.25f;
                ++ageEdge;
            }
            if (dragEdgeCase) {
                p->mAge = 0.125f;
                p->mLife = 1.0f;
                step = 0.5f;
                ++dragEdge;
            }
            bool result;
            if (mode) result = object->Integrate(p, step);
            else result = ctx.Original<IntegrateFn>(0xc04840)(object, p, step);
            b.push_back(result);
            died += !result;
            followed += result && (low & 0x40000000u) != 0;
            plain += result && (low & 0x29000360u) == 0 && !(low & 0x40000000u);
            substeps += result && (low & 0x29000360u) != 0 && !(low & 0x40000000u);
            b.insert(b.end(), words, words + sizeof(Particle) / 4);
            Fingerprint(b);
            gEffects[0] = nullptr;
            object->mRefCount = 1;
            b.push_back(CallSlot<uint32_t>(e, 13));
            Drain(b);
            *GameAddress<void *>(kPoolFreeCell) = nullptr;
        }
        Same(ctx, blobs[0], blobs[1], "Meta_Effect_Integrate", seed);
    }
    if (died < 300 || followed < 300 || plain < 300 || substeps < 300 || dragEdge < 100 || ageEdge < 100)
        ctx.Fail("coverage: %u died, %u path, %u plain, %u sub-stepped, %u drag edge, %u age edge", died, followed, plain, substeps, dragEdge,
                 ageEdge);
}

namespace {

// SpawnSurfaceEffect builds its name in a function-static string (0x107ef18, guard 0x10fd384): the image's start-up never ran,
// so the guard is set as taken and the string left empty (size 0, capacity 15), the epoch high so the guard reads as done.
struct StaticName {
    difftest::Context &ctx;
    unsigned char saved[0x18];
    int guard, epoch;
    int *tls;
    explicit StaticName(difftest::Context &c) : ctx(c)
    {
        auto *s = ctx.image.At<unsigned char>(0x107ef18);
        std::memcpy(saved, s, sizeof(saved));
        std::memset(s, 0, sizeof(saved));
        *reinterpret_cast<uint32_t *>(s + 0x14) = 15;
        guard = *ctx.image.At<int>(0x10fd384);
        *ctx.image.At<int>(0x10fd384) = 5;
        tls = reinterpret_cast<int **>(__readfsdword(0x2c))[*ctx.image.At<uint32_t>(0x10df814)];
        epoch = tls[1];
        tls[1] = 0x7fffffff;
    }
    ~StaticName()
    {
        tls[1] = epoch;
        *ctx.image.At<int>(0x10fd384) = guard;
        std::memcpy(ctx.image.At<unsigned char>(0x107ef18), saved, sizeof(saved));
    }
};

} // namespace

// The surface (rendering row E8): the collision with the height field (0xbfe480), the placing of a new particle on it (0xbff8d0) and
// the one-off effect where a particle hits (0xc03970). The surface is a fake: the height one number, the normal random; the
// transforms are tame (dyadic values, so the equalities the code tells apart can be built), a chaotic one now and then.
DIFF_TEST(Meta_Effect_Surface, 0xbfe480)
{
    gCtx = &ctx;
    using CollideFn = bool(__thiscall *)(Effect *, Particle *, const Vector3 *, float);
    using SnapFn = bool(__thiscall *)(Effect *, Particle *);
    using SpawnFn = void(__thiscall *)(Effect *, const char *, const Vector3 *);
    uint32_t cover[24] = {};
    MetaWorld world;
    StaticName staticName(ctx);
    for (uint32_t seed = 0; seed < 9000; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            ctx.Mark("surface");
            std::unique_ptr<Precision> precision;
            if (seed % 2) precision = std::make_unique<Precision>();
            EffectsWorld w;
            gEv.clear();
            ResetWorld(seed, w);
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 6029u + 17);
            const uint32_t op = seed % 3, variant = (seed / 3) % 7;
            uint32_t low = 0, high = 0;
            if (op == 0) {
                static const uint32_t kLows[7] = {0x400, 0x100, 0x100 | 0x1000000, 0, 0x80, 0, 0};
                low = kLows[variant];
                if (r() % 3 == 0) high |= 2;
            } else if (op == 1) {
                if (r() % 2) low |= 0x10000;
                if (r() % 2) low |= 0x4000;
                if (r() % 2) low |= 0x8000;
            }
            auto first = std::make_unique<Description>();
            RandomDescription(*first, r, true, low, high);
            const auto tame = [&] { return Tame(r); };
            first->mBounce = op == 0 && (variant == 3 || variant == 4) ? 0.0f : r() % 4 == 0 ? Wild(r) : 0.25f * float(1 + r() % 6);
            if (op == 0 && variant == 4) first->mBounce = 0.0f;
            first->mDeath = r() % 3 == 0 ? 0.0f : r() % 6 == 0 ? 1.0f : r() % 6 == 0 ? Nan(r) : r() % 4 == 0 ? Wild(r) : float(r() % 9) / 8.0f;
            first->mLimitHeight = tame();
            for (float &v : first->mTerrainRepel) v = tame();
            first->mTerrainRepel[0] = 4.0f + Positive(r) * 4.0f;
            first->mTerrainRepel[4] = r() % 3 == 0 ? tame() - 4.0f : -1.0e9f;
            if (r() % 6 == 0) first->mTerrainRepel[1] = Wild(r);
            first->mHeightRange = {r() % 3 == 0 ? Wild(r) : tame() - 2.0f, r() % 3 == 0 ? Wild(r) : tame() + 2.0f};
            gDescriptions[0] = first.get();
            void *const e = MakeEffect(mode, first.get());
            auto *object = static_cast<Effect *>(static_cast<cITSComponentEffect *>(e));
            gEffects[0] = object;
            gEffects[1] = nullptr;
            CallSlot<uint32_t>(e, 12);
            CallSlot<void>(e, 0, static_cast<void *>(&w.owners[1]));
            const auto simple = [&](EffectsTransform &t) {
                static const float kM[5] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
                t = EffectsTransform{};
                t.flags = r() % 2 ? EffectsTransform::kRotated : 0;
                for (auto &row : t.m)
                    for (float &c : row) c = kM[r() % 5];
                static const float kS[3] = {1.0f, 0.5f, 2.0f};
                t.scale = kS[r() % 3];
                t.translation = {Positive(r) * 4.0f, Positive(r) * 4.0f, tame()};
            };
            const bool chaos = seed % 29 == 5;
            if (chaos) {
                RandomTransform(object->mLocal, r);
                RandomTransform(object->mWorld, r);
            } else {
                simple(object->mLocal);
                simple(object->mWorld);
            }
            object->mSurface.Assign(reinterpret_cast<cIGZUnknown *>(&gSurface));
            if (seed % 31 == 3) object->mSurface.Reset();
            object->mSurfaceRange[0] = 10.0f + Positive(r) * 4.0f;
            object->mSurfaceRange[1] = 10.0f + Positive(r) * 4.0f;
            object->mSurfaceBase = tame();
            object->mSource.mMin.z = tame();
            object->mSource.mMax.z = r() % 5 == 0 ? object->mSource.mMin.z : object->mSource.mMin.z + Positive(r);
            object->mVisible = r() % 4 != 0;
            reinterpret_cast<uint8_t *>(object)[0x177] = r() % 2;
            gSurfaceWords[0] = tame() / 2.0f;
            uint32_t words[sizeof(Particle) / 4] = {};
            auto *p = reinterpret_cast<Particle *>(words);
            p->mAlive = 1;
            p->mPosition = {tame(), tame(), tame()};
            p->mVelocity = {tame(), tame(), tame()};
            if (r() % 9 == 0) p->mVelocity.z = Wild(r);
            static const float kWalk[5] = {0.0f, 0.001f, 0.5f, 1.0f, -1.0f};
            for (cTSMetaWalk &walk : p->mWalks) {
                walk.mHorizontal = r() % 5 == 0 ? Wild(r) : kWalk[r() % 5];
                walk.mVertical = r() % 5 == 0 ? Wild(r) : kWalk[r() % 5];
                walk.mHeading = tame();
                walk.mPitch = tame();
            }
            const Particle was = *p;
            const size_t spawnedBefore = object->mSpawned.size();
            if (op == 0) {
                Vector3 where = {tame(), tame(), tame()};
                float step = r() % 8 == 0 ? Wild(r) : float(1 + r() % 4) * 0.0625f;
                if (r() % 7 == 0) where.z = gSurfaceWords[0]; // on the surface exactly
                if (r() % 11 == 0) where.z = first->mLimitHeight;
                if (variant == 5) { // a bounce with the death chance exactly what the next random number is
                    auto *random = GameAddress<cRZRandom>(kRandomCell);
                    const uint32_t s0 = random->mIntegerSeed;
                    const int32_t s1 = random->mRealSeed;
                    const float v = static_cast<float>(random->RandomDoubleUniform());
                    random->mIntegerSeed = s0;
                    random->mRealSeed = s1;
                    first->mBounce = 0.5f;
                    if (v != 0.0f && v != 1.0f) first->mDeath = v;
                    where.z = gSurfaceWords[0] - 1.0f;
                }
                if (variant == 6) { // a bounce under the surface
                    first->mBounce = 0.5f;
                    where.z = gSurfaceWords[0] - 1.0f;
                    if (seed % 5 == 0) { // NaNs of different payloads in the velocity: the bounce keeps each component's own
                        first->mDeath = 0.0f;
                        p->mVelocity = {Nan(r), Nan(r), Nan(r)};
                    }
                }
                if (variant == 1 || variant == 2) {
                    if (r() % 5 == 0) p->mVelocity = {0.0f, 0.0f, 0.0f};
                    if (r() % 4 == 0) { // the reach or the kill depth exactly the height over the surface
                        Vector3 q = p->mPosition;
                        object->mWorld.Apply(&q);
                        const float over = q.z - gSurfaceWords[0];
                        if (r() % 2) first->mTerrainRepel[0] = over;
                        else first->mTerrainRepel[4] = over;
                    }
                }
                if ((variant == 1 || variant == 2) && r() % 8 == 0) { // the repelled position exactly on an edge of the surface
                    EffectsTransform &wt = object->mWorld;
                    wt.flags = 0;
                    wt.scale = 1.0f;
                    wt.translation.x = 2.0f;
                    wt.translation.y = 3.0f;
                    object->mSurfaceRange[0] = 12.0f;
                    object->mSurfaceRange[1] = 13.0f;
                    p->mVelocity = {0.0f, 0.0f, 0.0f};
                    p->mPosition = {5.0f, 5.0f, p->mPosition.z};
                    switch (r() % 4) {
                    case 0: p->mPosition.x = -2.0f; break;
                    case 1: p->mPosition.y = -3.0f; break;
                    case 2: p->mPosition.x = 10.0f; break;
                    default: p->mPosition.y = 10.0f; break;
                    }
                    first->mTerrainRepel[0] = 1.0e6f;
                }
                if (variant == 6 && r() % 2) { // a bounce with the particle exactly on the surface
                    gSurfaceWords[0] = 1.5f;
                    where.z = 1.5f;
                }
                bool result;
                if (mode) result = object->CollideSurface(p, &where, step);
                else result = ctx.Original<CollideFn>(0xbfe480)(object, p, &where, step);
                b.push_back(result);
                const bool moved = std::memcmp(&was.mPosition, &p->mPosition, 12) != 0;
                const bool pushed = std::memcmp(&was.mVelocity, &p->mVelocity, 12) != 0;
                const bool turned = std::memcmp(&was.mWalks, &p->mWalks, sizeof(was.mWalks)) != 0;
                if (object->mSurface.Get()) {
                    cover[0] += variant == 0 && moved;
                    cover[1] += (variant == 1 || variant == 2) && !result;
                    cover[2] += variant == 1 && pushed && result;
                    cover[3] += variant == 2 && turned;
                    cover[4] += variant == 3 && moved;
                    cover[5] += variant == 4 && pushed;
                    cover[6] += variant == 3 && pushed && moved;
                    cover[7] += (variant == 5 || variant == 6) && moved && pushed;
                    cover[8] += (variant == 5 || variant == 6) && !result;
                    cover[9] += (variant == 5 || variant == 6) && object->mSpawned.size() != spawnedBefore;
                    cover[10] += variant == 3 && !moved;
                    cover[11] += (high & 2) && (moved || pushed);
                    cover[12] += variant == 4 && moved;
                }
            } else if (op == 1) {
                if (seed % 4 == 1) { // the edges: a position making the surface x exactly nought or the range, the height range exactly hit
                    EffectsTransform &wt = object->mWorld;
                    wt.flags = 0;
                    wt.scale = 1.0f;
                    p->mPosition = {-wt.translation.x, r() % 2 ? 0.0f : object->mSurfaceRange[1] - wt.translation.y, tame()};
                    if (r() % 2) std::swap(p->mPosition.x, p->mPosition.y), std::swap(wt.translation.x, wt.translation.y);
                    if (r() % 2) p->mPosition.x = object->mSurfaceRange[0] - wt.translation.x;
                    const float rel = gSurfaceWords[0] - object->mSurfaceBase;
                    if (r() % 2) first->mHeightRange.x = rel;
                    else first->mHeightRange.y = rel;
                }
                const bool result = mode ? object->SnapToSurface(p) : ctx.Original<SnapFn>(0xbff8d0)(object, p);
                b.push_back(result);
                const bool moved = std::memcmp(&was.mPosition, &p->mPosition, 12) != 0;
                cover[13] += !result;
                cover[14] += result && moved;
                cover[15] += result && !moved;
                cover[16] += result && moved && (low & 0x4000) && (low & 0x8000);
                cover[17] += result && moved && !(low & 0x4000) && (low & 0x8000);
                cover[18] += !result && (low & 0x10000);
                cover[19] += !result && !(low & 0x10000) && object->mSurface.Get();
                cover[20] += !result && !object->mSurface.Get();
            } else {
                static const char *const kSuffix[4] = {"_terrain", "", "_x", "_a_suffix_that_leaves_the_small_buffer_behind"};
                const Vector3 place = {tame(), tame(), tame()};
                if (mode) object->SpawnSurfaceEffect(kSuffix[seed / 3 % 4], &place);
                else ctx.Original<SpawnFn>(0xc03970)(object, kSuffix[seed / 3 % 4], &place);
                cover[21] += object->mSpawned.size() != spawnedBefore;
                cover[22] += !object->mVisible;
            }
            b.insert(b.end(), words, words + sizeof(Particle) / 4);
            Fingerprint(b);
            object->mSurface.Reset();
            gEffects[0] = nullptr;
            object->mRefCount = 1;
            b.push_back(CallSlot<uint32_t>(e, 13));
            Drain(b);
            *GameAddress<void *>(kPoolFreeCell) = nullptr;
        }
        Same(ctx, blobs[0], blobs[1], "Meta_Effect_Surface", seed);
    }
    const uint32_t kNeed[23] = {100, 100, 100, 100, 100, 100, 100, 100, 100, 50, 100, 100, 100, 200, 200, 100, 100, 100, 100, 100, 100, 100, 50};
    for (unsigned i = 0; i < 23; ++i)
        if (cover[i] < kNeed[i]) ctx.Fail("coverage: counter %u is %u, wanted %u", i, cover[i], kNeed[i]);
}

// The sub-stepped integrator (rendering row E8, 0xc04a00): a step cut into pieces of 1/15 s, each the move or the steering, the force,
// the walks, the attractors with their stop, the drag, the colliders and then the surface over the new position (the surface's own
// fake, the edges of its range, flags 0x40, 0x100 and 0x200). Run on its own: the integrator 0xc04840 reaches it only by flags.
DIFF_TEST(Meta_Effect_Substeps, 0xc04a00)
{
    gCtx = &ctx;
    using SubstepsFn = bool(__thiscall *)(Effect *, Particle *, float);
    uint32_t cover[12] = {};
    MetaWorld world;
    StaticName staticName(ctx);
    for (uint32_t seed = 0; seed < 9000; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            ctx.Mark("substeps");
            std::unique_ptr<Precision> precision;
            if (seed % 2) precision = std::make_unique<Precision>();
            EffectsWorld w;
            gEv.clear();
            ResetWorld(seed, w);
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 6043u + 29);
            uint32_t low = 0;
            if (r() % 4 == 0) low |= 0x20000000u;
            if (r() % 3 == 0) low |= 0x1000000u;
            if (r() % 3 == 0) low |= 0x40u;
            if (r() % 3 == 0) low |= 0x100u;
            if (r() % 3 == 0) low |= 0x200u;
            if (r() % 5 == 0) low |= 0x80u;
            if (r() % 6 == 0) low |= 0x1u; // an unrelated bit beside the walk flag
            auto first = std::make_unique<Description>();
            RandomDescription(*first, r, seed % 3 != 0, low, 0);
            for (cTSParticleAttractor &a : first->mAttractors) { // the minimum distance an attractor stops a particle inside
                static const float kStop[4] = {-1.0f, 0.0f, 0.5f, 100.0f};
                a.m30 = kStop[r() % 4];
            }
            first->mDrag = r() % 3 == 0 ? 0.0f : r() % 5 == 0 ? Wild(r) : float(r() % 5) * 0.25f;
            first->mBounce = r() % 3 == 0 ? 0.0f : 0.25f * float(1 + r() % 6);
            first->mDeath = r() % 3 == 0 ? 0.0f : float(r() % 9) / 8.0f;
            first->mLimitHeight = Tame(r);
            for (float &v : first->mTerrainRepel) v = Tame(r);
            first->mTerrainRepel[0] = 4.0f + Positive(r) * 4.0f;
            first->mTerrainRepel[4] = -1.0e9f;
            const auto pathPoints = [&](std::vector<cTSParticlePathPoint> &points) {
                points.clear();
                float time = 0.0f;
                const unsigned n = 1 + r() % 4;
                for (unsigned i = 0; i < n; ++i) {
                    cTSParticlePathPoint p;
                    p.mPosition = {Tame(r), Tame(r), Tame(r)};
                    p.mVelocity = {Tame(r), Tame(r), Tame(r)};
                    time += 0.25f * float(1 + r() % 4);
                    p.mTime = time;
                    points.push_back(p);
                }
            };
            pathPoints(first->mPath.mPoints);
            gDescriptions[0] = first.get();
            void *const e = MakeEffect(mode, first.get());
            auto *object = static_cast<Effect *>(static_cast<cITSComponentEffect *>(e));
            gEffects[0] = object;
            gEffects[1] = nullptr;
            CallSlot<uint32_t>(e, 12);
            CallSlot<void>(e, 0, static_cast<void *>(&w.owners[1]));
            if (r() % 3 == 0) pathPoints(object->mPath);
            const auto simple = [&](EffectsTransform &t) {
                static const float kM[5] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
                t = EffectsTransform{};
                t.flags = r() % 2 ? EffectsTransform::kRotated : 0;
                for (auto &row : t.m)
                    for (float &c : row) c = kM[r() % 5];
                static const float kS[3] = {1.0f, 0.5f, 2.0f};
                t.scale = kS[r() % 3];
                t.translation = {Positive(r) * 4.0f, Positive(r) * 4.0f, Tame(r)};
            };
            if (seed % 29 == 5) {
                RandomTransform(object->mLocal, r);
                RandomTransform(object->mWorld, r);
            } else {
                simple(object->mLocal);
                simple(object->mWorld);
            }
            object->mSurface.Assign(reinterpret_cast<cIGZUnknown *>(&gSurface));
            if (seed % 31 == 3) object->mSurface.Reset();
            object->mSurfaceRange[0] = 4.0f + Positive(r) * 4.0f;
            object->mSurfaceRange[1] = 4.0f + Positive(r) * 4.0f;
            object->mSurfaceBase = Tame(r);
            reinterpret_cast<uint8_t *>(object)[0x177] = r() % 2;
            object->mVisible = r() % 4 != 0;
            object->mForce = {Tame(r), Tame(r), Tame(r)};
            gSurfaceWords[0] = Tame(r) / 2.0f;
            uint32_t words[sizeof(Particle) / 4] = {};
            auto *p = reinterpret_cast<Particle *>(words);
            p->mAlive = seed % 17 == 7 ? 0 : 1; // a dead one comes out dead
            p->mLife = 1.0f;
            p->mAge = float(r() % 4) * 0.125f;
            p->mPosition = {Tame(r), Tame(r), Tame(r)};
            p->mVelocity = {Tame(r), Tame(r), Tame(r)};
            if (seed % 37 == 11) { // NaNs of different payloads meeting in the move, the force and the bounce
                p->mPosition.x = Nan(r);
                p->mVelocity.x = Nan(r);
                p->mVelocity.y = Nan(r);
                object->mForce.y = Nan(r);
            }
            static const float kClock[5] = {0.0f, 0.03f, 0.0666667f, 0.5f, -1.0f};
            static const float kWalk[5] = {0.0f, 0.001f, 0.5f, 1.0f, -1.0f};
            for (cTSMetaWalk &walk : p->mWalks) {
                walk.mClock = r() % 5 == 0 ? Tame(r) : kClock[r() % 5];
                walk.mHorizontal = kWalk[r() % 5];
                walk.mVertical = kWalk[r() % 5];
                walk.mHeading = Tame(r);
                walk.mPitch = Tame(r);
            }
            if (r() % 4 == 0) { // the new position's surface x exactly nought or the range
                EffectsTransform &wt = object->mWorld;
                wt.flags = 0;
                wt.scale = 1.0f;
                wt.translation.x = 2.0f;
                wt.translation.y = 3.0f;
                object->mSurfaceRange[0] = 12.0f;
                object->mSurfaceRange[1] = 13.0f;
                p->mVelocity = {0.0f, 0.0f, 0.0f};
                object->mForce = {0.0f, 0.0f, 0.0f};
                first->mDrag = 0.0f;
                p->mPosition = {5.0f, 5.0f, p->mPosition.z};
                switch (r() % 4) {
                case 0: p->mPosition.x = -2.0f; break;
                case 1: p->mPosition.y = -3.0f; break;
                case 2: p->mPosition.x = 10.0f; break;
                default: p->mPosition.y = 10.0f; break;
                }
            }
            float step;
            switch (r() % 12) {
            case 0: step = 0.0f; break;
            case 1: step = -0.25f; break;
            case 2: step = Nan(r); break;
            case 3: step = 0.06666667f; break; // exactly a piece
            case 4: step = 0.06666667f * 2.0f; break;
            case 5: step = 0.0625f; break; // less than a piece
            default: step = float(1 + r() % 24) * 0.0625f; break;
            }
            p->mSizeVary = Tame(r);
            gScratchBase = object->mColliderState.data();
            const Particle was = *p;
            bool result;
            if (mode) result = object->IntegrateSubsteps(p, step);
            else result = ctx.Original<SubstepsFn>(0xc04a00)(object, p, step);
            b.push_back(result);
            const bool moved = std::memcmp(&was.mPosition, &p->mPosition, 12) != 0;
            cover[0] += result;
            cover[1] += !result;
            cover[2] += !result && (low & 0x200);
            cover[3] += !result && !(low & 0x200) && object->mSurface.Get();
            cover[4] += result && (low & 0x20000000u);
            cover[5] += result && (low & 0x1000000u) && std::memcmp(&was.mWalks, &p->mWalks, sizeof(was.mWalks)) != 0;
            cover[6] += result && !object->mAttractors.empty();
            cover[7] += !result && !object->mAttractors.empty();
            cover[8] += result && moved && (low & 0x140);
            cover[9] += !object->mColliders.empty();
            cover[10] += result && step > 0.07f;
            b.insert(b.end(), words, words + sizeof(Particle) / 4);
            Fingerprint(b);
            object->mSurface.Reset();
            gEffects[0] = nullptr;
            object->mRefCount = 1;
            b.push_back(CallSlot<uint32_t>(e, 13));
            Drain(b);
            *GameAddress<void *>(kPoolFreeCell) = nullptr;
        }
        Same(ctx, blobs[0], blobs[1], "Meta_Effect_Substeps", seed);
    }
    const uint32_t kNeed[11] = {500, 500, 100, 100, 300, 300, 300, 50, 300, 100, 1000};
    for (unsigned i = 0; i < 11; ++i)
        if (cover[i] < kNeed[i]) ctx.Fail("coverage: counter %u is %u, wanted %u", i, cover[i], kNeed[i]);
}

namespace {
struct InitQuery {
    void *const *vt;
    cIGZString *answer;
    static uint32_t __fastcall Interface(InitQuery *q, void *, uint32_t iid, void **out)
    {
        Ev({evQuery, iid});
        if (q->answer) {
            q->answer->AddRef();
            *out = q->answer;
        }
        return q->answer != nullptr;
    }
};
} // namespace

// A new particle (rendering row E8, 0xc006f0): the life, then the start from the path's first point or from the source (ring,
// ellipsoid or box) with the direction, the speed and a bomb, into the effect's space, the snap to a surface, the nudge along the
// velocity, the randoms around 1, the spin and heading by alignment, the particle's own effect (what the owner made, in the order
// of its calls) and the walks. Run on its own, from the allocator's cell to the particle's words.
DIFF_TEST(Meta_Effect_Init, 0xc006f0)
{
    gCtx = &ctx;
    using InitFn = void(__thiscall *)(Effect *, Particle *);
    uint32_t cover[16] = {};
    MetaWorld world;
    for (uint32_t seed = 0; seed < 9000; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            ctx.Mark("init");
            std::unique_ptr<Precision> precision;
            if (seed % 2) precision = std::make_unique<Precision>();
            EffectsWorld w;
            gEv.clear();
            ResetWorld(seed, w);
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 6101u + 3);
            uint32_t low = 0;
            if (r() % 3 == 0) low |= 0x40000000u;
            if (r() % 4 == 0) low |= 0x800u;
            if (r() % 3 == 0) low |= 0x10u;
            if (r() % 4 == 0) low |= 0x400000u;
            if (r() % 3 == 0) low |= 0x1000000u;
            if (r() % 2 == 0) low |= 0x2000000u;
            if (r() % 3 == 0) low |= 0x4000u << (r() % 5); // one of the snap flags
            if (r() % 8 == 0) low |= 0x7c000u;
            if (r() % 6 == 0) low |= 0x1u;
            auto first = std::make_unique<Description>();
            RandomDescription(*first, r, seed % 3 != 0, low, 0);
            static const float kRing[4] = {-1.0f, -1.0f, 0.0f, 0.75f};
            first->mSourceRing = r() % 4 == 0 ? Tame(r) : kRing[r() % 4];
            static const float kBomb[5] = {0.0f, 0.0f, 1.0f, -1.0f, 0.5f};
            first->mBomb = r() % 5 == 0 ? Tame(r) : kBomb[r() % 5];
            first->mBombCentre = {Tame(r), Tame(r), Tame(r)};
            first->mAlignment = r() % 3 == 0 ? uint8_t(r() % 7) : uint8_t(r() % 5);
            if (r() % 8 == 0) first->mAlignment = uint8_t(254 + r() % 3); // wraps under the "- 2 <= 2" test
            first->mRotateVary = r() % 3 == 0 ? 0.0f : r() % 4 == 0 ? Wild(r) : Positive(r);
            first->mSizeVary = r() % 3 == 0 ? 0.0f : Tame(r);
            first->mAlphaVary = Tame(r);
            first->mAspectVary = Tame(r);
            first->mColorVary = {Tame(r), r() % 3 == 0 ? 0.0f : Tame(r), Tame(r)};
            if (r() % 5 == 0) first->m38 = first->m44 = {0.0f, 0.0f, 0.0f};
            if (r() % 7 == 0) first->mEmitVolume = {0.0f, 0.0f};
            const auto pathPoints = [&](std::vector<cTSParticlePathPoint> &points) {
                points.clear();
                float time = 0.0f;
                const unsigned n = 1 + r() % 4;
                for (unsigned i = 0; i < n; ++i) {
                    cTSParticlePathPoint p;
                    p.mPosition = {Tame(r), Tame(r), Tame(r)};
                    p.mVelocity = {Tame(r), Tame(r), Tame(r)};
                    time += r() % 5 == 0 ? 0.0f : 0.25f * float(1 + r() % 4);
                    p.mTime = r() % 6 == 0 ? -0.5f : time;
                    if (i == 0 && r() % 3) p.mTime = 0.0f;
                    points.push_back(p);
                }
            };
            pathPoints(first->mPath.mPoints);
            gDescriptions[0] = first.get();
            void *const e = MakeEffect(mode, first.get());
            auto *object = static_cast<Effect *>(static_cast<cITSComponentEffect *>(e));
            gEffects[0] = object;
            gEffects[1] = nullptr;
            CallSlot<uint32_t>(e, 12);
            CallSlot<void>(e, 0, static_cast<void *>(&w.owners[1]));
            if (r() % 3 == 0) pathPoints(object->mPath);
            const auto simple = [&](EffectsTransform &t) {
                static const float kM[5] = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};
                t = EffectsTransform{};
                t.flags = r() % 2 ? EffectsTransform::kRotated : 0;
                for (auto &row : t.m)
                    for (float &c : row) c = kM[r() % 5];
                static const float kS[3] = {1.0f, 0.5f, 2.0f};
                t.scale = kS[r() % 3];
                t.translation = {Positive(r) * 4.0f, Positive(r) * 4.0f, Tame(r)};
            };
            if (seed % 29 == 5) {
                RandomTransform(object->mLocal, r);
                RandomTransform(object->mWorld, r);
            } else {
                simple(object->mLocal);
                simple(object->mWorld);
            }
            // the source box: a box, a flat one, or a point
            const Vector3 centre = {Tame(r), Tame(r), Tame(r)};
            const float reach[3] = {r() % 4 == 0 ? 0.0f : Positive(r), r() % 4 == 0 ? 0.0f : Positive(r), r() % 4 == 0 ? 0.0f : Positive(r)};
            object->mSource.mMin = {centre.x - reach[0], centre.y - reach[1], centre.z - reach[2]};
            object->mSource.mMax = {centre.x + reach[0], centre.y + reach[1], centre.z + reach[2]};
            if (seed % 41 == 9 && first->mBomb > 0.0f) { // the bomb's distance about the 1e-6 under which it has no direction
                static const float kStep[5] = {0.0009f, 0.00099f, 0.001f, 0.00101f, 0.0011f};
                object->mSource.mMin = object->mSource.mMax = {0.0f, 0.0f, 0.0f};
                first->mBombCentre = {kStep[r() % 5], 0.0f, 0.0f};
                object->mLocal = EffectsTransform{};
                first->m38 = first->m44 = {1.0f, 0.0f, 0.0f};
                low = uint32_t(first->mFlags);
            }
            const bool nanEdge = seed % 53 == 17;
            const unsigned nanKind = r() % 4;
            if (nanEdge) { // NaNs of different payloads meeting in one sum or product: the first operand's wins
                low &= ~(0x40000000u | 0x800u | 0x7c000u | 0x10u);
                first->mSourceRing = -1.0f;
                first->mBomb = 0.0f;
                first->mAlignment = 0;
                object->mLocal = EffectsTransform{};
                const float point[3] = {1.0f, 2.0f, 3.0f};
                object->mSource.mMin = object->mSource.mMax = {point[0], point[1], point[2]};
                const unsigned axis = r() % 3;
                if (nanKind == 0) { // the bomb: 0 * inf is a NaN beside the velocity's own
                    first->mBomb = std::numeric_limits<float>::infinity();
                    first->m38 = first->m44 = {Nan(r), Nan(r), Nan(r)};
                    float c[3] = {point[0], point[1], point[2]};
                    c[axis] += 1.0f;
                    first->mBombCentre = {c[0], c[1], c[2]};
                } else if (nanKind == 1) { // the nudge along the velocity: 0 / 0 beside a NaN position
                    low |= 0x10u;
                    first->m38 = first->m44 = {0.0f, 0.0f, 0.0f};
                    first->mEmitVolume = {0.0f, 0.0f};
                    float *box = &object->mSource.mMin.x;
                    box[axis] = box[axis + 3] = Nan(r);
                } else if (nanKind == 2) { // alignment 1: the heading's NaN beside the spin's
                    first->mAlignment = 1;
                    first->mRotateVary = std::numeric_limits<float>::infinity(); // -inf + inf: the default NaN as the spin
                    object->mLocal.m[0][0] = Nan(r);
                } else { // the speed: a NaN draw times a NaN scale
                    first->mEmitVolume = {Nan(r), Nan(r)};
                    object->mSpeedScale = Nan(r);
                }
                first->mFlags = low;
            }
            object->mSurface.Assign(reinterpret_cast<cIGZUnknown *>(&gSurface));
            if (seed % 7 == 3) object->mSurface.Reset();
            object->mSurfaceRange[0] = 4.0f + Positive(r) * 4.0f;
            object->mSurfaceRange[1] = 4.0f + Positive(r) * 4.0f;
            object->mSurfaceBase = Tame(r);
            gSurfaceWords[0] = Tame(r) / 2.0f;
            reinterpret_cast<uint8_t *>(object)[0x177] = r() % 2;
            object->mVisible = r() % 3 != 0;
            if (!(nanEdge && nanKind == 3)) object->mSpeedScale = r() % 4 == 0 ? 1.0f : Tame(r);
            object->mLifeScale = r() % 4 == 0 ? 1.0f : Tame(r);
            object->mEvent101[1] = r() % 4 == 0 ? 1.0f : Tame(r);
            object->mFirstFrame = r() % 2 ? -1.0f : r() % 3 ? Positive(r) : 0.0f;
            if (r() % 3 == 0) {
                object->mHasSpawnArgument = true;
                object->mSpawnArgument.Assign(reinterpret_cast<cIGZUnknown *>(&gEffectFakes[r() % 6]));
            }
            if (r() % 3 == 0) { // a name override, through the effect's own query slot
                static void *const vt[1] = {reinterpret_cast<void *>(&InitQuery::Interface)};
                alignas(8) uint8_t storage[sizeof(cRZString)];
                auto *text = reinterpret_cast<cRZString *>(storage);
                static const char *const names[] = {"", "tex_a", "a_name_long_enough_to_leave_the_small_buffer"};
                text->Construct(names[r() % 3]);
                cIGZString *face = text->AsInterface();
                face->AddRef();
                InitQuery query{vt, face};
                CallSlot<uint8_t>(e, 8, static_cast<void *>(nullptr), static_cast<void *>(&query));
                text->Destruct();
            }
            uint32_t words[sizeof(Particle) / 4] = {};
            auto *p = reinterpret_cast<Particle *>(words);
            p->mAlive = r() % 2;
            p->mLife = Wild(r);
            p->mAge = Wild(r);
            if (r() % 3 == 0) { // an effect already on the particle
                FakeNode *const old = &gEffectFakes[r() % 6];
                ++old->refs;
                *reinterpret_cast<FakeNode **>(reinterpret_cast<uint8_t *>(p) + offsetof(Particle, mEffect)) = old;
            }
            for (cTSMetaWalk &walk : p->mWalks) {
                walk.mClock = Tame(r);
                walk.mHorizontal = Tame(r);
                walk.mVertical = Tame(r);
                walk.mHeading = Tame(r);
                walk.mPitch = Tame(r);
            }
            gScratchBase = object->mColliderState.data();
            if (mode) object->InitParticle(p);
            else ctx.Original<InitFn>(0xc006f0)(object, p);
            const std::vector<cTSParticlePathPoint> &points = object->mPath.empty() ? first->mPath.mPoints : object->mPath;
            const bool fromPath = (low & 0x40000000u) && !points.empty() && points.front().mTime == 0.0f;
            cover[0] += fromPath;
            cover[1] += fromPath && points.back().mTime > 0.0f;
            cover[2] += !fromPath && first->mSourceRing >= 0.0f;
            cover[3] += !fromPath && first->mSourceRing < 0.0f && (low & 0x800);
            cover[4] += !fromPath && first->mBomb > 0.0f;
            cover[5] += p->mAlive == 1;
            cover[6] += p->mAlive != 1;
            cover[7] += p->mHeading == -999.0f;
            cover[8] += !fromPath && (low & 0x1000000u);
            cover[9] += (low & 0x400000u) != 0;
            cover[10] += object->mHasSpawnArgument && p->mAlive == 1;
            cover[11] += first->mAlignment == 1;
            cover[12] += !fromPath && (low & 0x10u) && p->mAlive == 1;
            cover[13] += !object->mVisible && p->mAlive == 1;
            b.insert(b.end(), words, words + sizeof(Particle) / 4);
            Fingerprint(b);
            if (void *const own = *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(p) + offsetof(Particle, mEffect)))
                --static_cast<FakeNode *>(own)->refs;
            object->mSurface.Reset();
            object->mSpawnArgument.Reset();
            gEffects[0] = nullptr;
            object->mRefCount = 1;
            b.push_back(CallSlot<uint32_t>(e, 13));
            Drain(b);
            *GameAddress<void *>(kPoolFreeCell) = nullptr;
        }
        Same(ctx, blobs[0], blobs[1], "Meta_Effect_Init", seed);
    }
    const uint32_t kNeed[14] = {300, 100, 300, 100, 300, 2000, 100, 1000, 300, 500, 300, 500, 300, 300};
    for (unsigned i = 0; i < 14; ++i)
        if (cover[i] < kNeed[i]) ctx.Fail("coverage: counter %u is %u, wanted %u", i, cover[i], kNeed[i]);
}

// The ellipsoid helper 0xbdd5e0 (cdecl) on its own: a box, a flat one and a point, at both x87 precisions. A NaN extent loops on in
// both, so only finite boxes are asked.
DIFF_TEST(Meta_Effect_Ellipsoid, 0xbdd5e0)
{
    gCtx = &ctx;
    using EllipsoidFn = Vector3 *(__cdecl *)(Vector3 *, const float *);
    MetaWorld world;
    for (uint32_t seed = 0; seed < 6000; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            ctx.Mark("ellipsoid");
            std::unique_ptr<Precision> precision;
            if (seed % 2) precision = std::make_unique<Precision>();
            EffectsWorld w;
            gEv.clear();
            ResetWorld(seed, w);
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 6151u + 5);
            float box[6];
            for (int i = 0; i < 3; ++i) {
                const float c = Tame(r);
                const float h = r() % 4 == 0 ? 0.0f : r() % 5 == 0 ? Positive(r) * 1.0e-3f : Positive(r) * 4.0f;
                box[i] = c - h;
                box[i + 3] = r() % 11 == 0 ? c - h : c + h; // a point's worth now and then
            }
            if (seed % 13 == 4) box[3] = box[0] = box[1] = box[4] = 0.0f; // flat on two axes
            Vector3 out = {7.0f, 8.0f, 9.0f};
            Vector3 *const result = mode ? fx::MetaRandomInEllipsoid(&out, box) : ctx.Original<EllipsoidFn>(0xbdd5e0)(&out, box);
            b.push_back(result == &out);
            b.push_back(Bits(out.x));
            b.push_back(Bits(out.y));
            b.push_back(Bits(out.z));
            Fingerprint(b);
        }
        Same(ctx, blobs[0], blobs[1], "Meta_Effect_Ellipsoid", seed);
    }
}
