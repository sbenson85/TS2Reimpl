// cTSParticlesEffect (src/ts/cTSParticlesEffect.h), lane WR, rendering row E4: original vs ours.
//
// Mode 0 builds the effect with the original Create 0xbc0c50, mode 1 with ours; slots run through the object's own vtable, the
// helpers still called by game code through the original's RVA (mode 0) or our member (mode 1, what the hook runs). After every
// step: the results, the log of every fake's calls, both vtable words, every byte of the effect (pointers as ids), the particles
// and the pool's free list, and the game's random generator.
//
//   Particles_Effect_Construct  0xbb4a80 in place over random bytes, every byte compared, then the destructor
//   Particles_Effect_Slots      random steps on an effect and a follower, all the fakes; the end through the deleting slot
//   Particles_Effect_Frame      slot 5, our step (src/ts/cTSParticlesStep.cpp) against the game's 0xbb6830 on a twin object: emission, the
//                               integrators and retirement, compared frame by frame; with a shape, the renderable's children
//                               (AddParticleChild 0xbb6670, UpdateParticleChildren 0xbc1d10) run for real on tests/ts_child_nodes.h
//
// Fakes: the effects world (tests/ts_effects_collection.h) and, on it, the owner's slots 14, 21, 24, 28, 30, 43 (this file). The
// renderable is real on both sides (the game's constructor 0xbb5010 in mode 0, ours in mode 1, WQ's verified); Setup 0xbbad30
// and BuildMaterial 0xbc03d0 are stood in for on both sides (they receive the renderable's own side's objects and draw on the
// director's world, tests/ts_particles_renderable.cpp), and the stand-in swaps the renderable's vtable word for a copy with the
// tree slots 34, 42, 45, 46, 52, 54 faked, so its place in the tree is a logged fake. InitParticle 0xbbb390 receives our object: it
// runs for real, on both sides.
//
// Covered RVAs: 0xbc0c50 (Create), 0xbb4a80 (constructor), 0xbb5290 (destructor), 0xbb5860 (deleting), 0xbb5761 (its +4 thunk),
// 0xbba860 (slot 0), 0xbc07e0 (1), 0xbc0a10 (2), 0xbc0b90 (3), 0xbbd8f0 (6), 0xbbd0e0 (7), 0xbbdc90 (8), 0xbbd380 (9), 0xbbd0a0
// (10), 0xbc0c10, 0xbc0c20 (11), 0xbbab40 (Prepare), 0xbbd410 (Aim), 0xbbce70 (ClearParticles), 0xbb94d0 (RetireParticle),
// 0xbba740 (HandParticleOn), 0xbb5b30 (AddParticle), 0xbc1120 (ResetLife), 0xbc1160 (ResetLives), 0xbc1b10 (StepWind), 0xbb9350
// (box Center), 0xbb5bd0 (box Extend), 0xbddf10, 0xbde5a0, 0xbde5e0, 0xbde650 (the attractors), 0xbb6670, 0xbbcef0, 0xbc1d10 (the
// renderable's particle children).
#include "ts_particles_world.h"


namespace {

// One step, on `t` (the effect or its follower), in ways that keep both sides alive: what would run off a null pointer in both
// (an unattached effect's renderable, a detached effect's description) is not asked for.
void Step(int mode, EffectsWorld &w, Effect *t, cITSComponentEffect *follower, std::mt19937 &r, Blob &b)
{
    const uint32_t op = r() % 24;
    b.push_back(op);
    char mark[64];
    std::snprintf(mark, sizeof(mark), "mode %d effect %d op %u", mode, t == gEffects[0] ? 0 : 1, op);
    gCtx->Mark(mark);
    uint32_t result = 0xfeed;
    switch (op) {
    case 0:
        CallSlot<void>(t, 2, r() % 2 ? 0u : uint32_t(r()));
        break;
    case 1:
        CallSlot<void>(t, 3, r() % 2 ? 0u : uint32_t(r()));
        break;
    case 2:
    case 3: {
        EffectsTransform local, parent;
        RandomTransform(local, r);
        RandomTransform(parent, r);
        CallSlot<void>(t, 6, &local, &parent);
        break;
    }
    case 4:
    case 5:
    case 6: {
        static const uint32_t kinds[] = {0, 1, 2, 3, 4, 5, 5, 5, 6, 7, 8, 9, 9, 10, 11, 12, 13, 14, 0x100, 0x101, 0x102, 0xffffffffu, 0x80000000u};
        const uint32_t kind = kinds[r() % std::size(kinds)];
        float values[6];
        for (float &v : values) v = Wild(r);
        if (r() % 4 == 0) values[1] = values[4] = values[2] = 0.0f;
        result = CallSlot<uint8_t>(t, 7, kind, static_cast<void *>(values), reinterpret_cast<void *>(uintptr_t(r())));
        break;
    }
    case 7: {
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
        static const char *const names[] = {"", "tex_a", "TEX_A", "tex_b"};
        text->Construct(names[r() % 4]);
        cIGZString *face = text->AsInterface();
        face->AddRef();
        FakeQuery query{vt, r() % 4 ? face : nullptr};
        result = CallSlot<uint8_t>(t, 8, reinterpret_cast<void *>(uintptr_t(r() % 4 ? 0 : r())), static_cast<void *>(&query));
        b.push_back(text->mRefCount);
        text->Destruct();
        break;
    }
    case 8:
        if (t->mRenderable.Get()) CallSlot<void>(t, 9, r() % 2 != 0);
        break;
    case 9:
        if (t->mRenderable.Get() && gDescriptions[2]) {
            gNextDescription = gDescriptions[r() % 3];
            t->mCollidersPlaced = true; // the step's mark: Prepare clears it when the description has colliders
            CallSlot<void>(t, 11, shape::P(&gDescriptionFace));
        }
        break;
    case 10:
    case 11:
    case 12: {
        if (w.nextNode > 18) w.nextNode = 0;
        cTSParticle *const p = TakeParticle(r);
        if (!p) break;
        if (mode) t->AddParticle(p);
        else Helper<void(__thiscall *)(void *, cTSParticle *)>(0xbb5b30, t, p);
        break;
    }
    case 13: {
        if (!t->mOwner.Get()) break; // a death effect would ask the null owner
        cTSParticle *const p = TakeParticle(r);
        if (!p) break;
        if (mode) t->RetireParticle(p);
        else Helper<void(__thiscall *)(void *, cTSParticle *)>(0xbb94d0, t, p);
        break;
    }
    case 14:
        if (!t->mParticles.empty()) {
            cTSParticle *const p = t->mParticles.back();
            if (mode) t->ResetLife(p);
            else Helper<void(__thiscall *)(void *, cTSParticle *)>(0xbc1120, t, p);
        }
        break;
    case 15:
        if (mode) t->ResetLives();
        else Helper<void(__thiscall *)(void *)>(0xbc1160, t);
        break;
    case 16: {
        const Vector3 point = {Wild(r), Wild(r), Wild(r)};
        auto *box = reinterpret_cast<cTSParticleBox *>(t->mBound);
        if (mode) box->Extend(&point);
        else Helper<void(__thiscall *)(void *, const Vector3 *)>(0xbb5bd0, box, &point);
        break;
    }
    case 17: {
        Vector3 out = {1.0f, 2.0f, 3.0f};
        auto *box = reinterpret_cast<cTSParticleBox *>(t->mBound);
        if (mode) out = box->Center();
        else Helper<void(__thiscall *)(void *, Vector3 *)>(0xbb9350, box, &out);
        b.insert(b.end(), {Bits(out.x), Bits(out.y), Bits(out.z)});
        break;
    }
    case 18: {
        const float step = r() % 4 == 0 ? 0.0f : Positive(r);
        if (mode) t->StepWind(step);
        else Helper<void(__thiscall *)(void *, float)>(0xbc1b10, t, step);
        break;
    }
    case 19:
        if (!t->mOwner.Get()) break;
        if (mode) t->ClearParticles();
        else Helper<void(__thiscall *)(void *)>(0xbbce70, t);
        break;
    case 20: {
        if (!t->mFollower.Get()) break;
        cTSParticle *const p = TakeParticle(r);
        if (!p) break;
        if (mode) t->HandParticleOn(p);
        else Helper<void(__thiscall *)(void *, cTSParticle *)>(0xbba740, t, p);
        break;
    }
    case 21: {
        if (t->mAttractors.empty() || t->mDescription->mAttractors.empty()) break;
        cTSParticleAttractorState *const a = &t->mAttractors[r() % t->mAttractors.size()];
        EffectsTransform place;
        RandomTransform(place, r);
        const Vector3 point = {Wild(r), Wild(r), Wild(r)};
        const cTSParticleAttractor *const source = &t->mDescription->mAttractors[r() % t->mDescription->mAttractors.size()];
        const uint32_t which = r() % 4;
        b.push_back(which);
        if (which == 0) {
            if (mode) a->Follow(&place);
            else Helper<void(__thiscall *)(void *, const EffectsTransform *)>(0xbde650, a, &place);
        } else if (which == 1) {
            if (mode) a->SetCentre(&point);
            else Helper<void(__thiscall *)(void *, const Vector3 *)>(0xbde5e0, a, &point);
        } else {
            const EffectsTransform *const transform = which == 2 ? nullptr : &place;
            if (mode) a->Set(source, transform);
            else Helper<void(__thiscall *)(void *, const cTSParticleAttractor *, const EffectsTransform *)>(0xbde5a0, a, source, transform);
        }
        break;
    }
    case 22:
        result = CallSlot<uint8_t>(t, 4);
        break;
    case 23:
        if (follower && t != static_cast<void *>(follower) && r() % 2) {
            CallSlot<void>(t, 10, shape::P(follower));
        } else if (t->mAttached) {
            CallSlot<void>(t, 0, static_cast<void *>(&w.owners[1])); // attached: nothing happens
        }
        break;
    }
    b.push_back(result);
}

} // namespace

DIFF_TEST(Particles_Effect_Construct, kConstruct)
{
    gCtx = &ctx;
    NodeWorld nodes;
    cTSParticlesDescription description;
    for (uint32_t seed = 0; seed < 24; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            Blob &b = blobs[mode];
            std::mt19937 r(seed + 1);
            alignas(16) uint8_t block[sizeof(Effect)];
            for (uint8_t &x : block) x = uint8_t(r());
            void *e = mode ? static_cast<void *>(::new (block) Effect(&description)) : ctx.Original<ConstructFn>(kConstruct)(block, &description);
            b.push_back(uint32_t(e == block));
            gDescriptions[0] = &description;
            PushEffectState(b, reinterpret_cast<Effect *>(block));
            // +0x34, +0x41.. and the others the walk reads as raw words compare with the block's bytes as the constructor left them.
            for (size_t i : {size_t(0xc5), size_t(0xfd), size_t(0x1f1), size_t(0x1f2), size_t(0x1f3), size_t(0x209)}) b.push_back(block[i]);
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
        Same(ctx, blobs[0], blobs[1], "Particles_Effect_Construct", seed);
    }
}

DIFF_TEST(Particles_Effect_Slots, kCreate)
{
    gCtx = &ctx;
    Overrides overrides;
    NodeWorld nodes;
    gEv.reserve(1 << 20);
    for (uint32_t seed = 0; seed < 600; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            char mark[64];
            std::snprintf(mark, sizeof(mark), "slots seed %u mode %d", seed, mode);
            ctx.Mark(mark);
            std::unique_ptr<Precision> precision;
            if (seed % 2) precision = std::make_unique<Precision>();
            EffectsWorld w;
            gEv.clear();
            ResetWorld(seed, w);
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 7919u + 3);
            auto first = std::make_unique<cTSParticlesDescription>();
            auto second = std::make_unique<cTSParticlesDescription>();
            auto third = std::make_unique<cTSParticlesDescription>();
            const auto flags = [&r] {
                uint32_t f = 0;
                for (uint32_t bit : {4u, 0x200u, 0x100000u, 0x40000000u})
                    if (r() % 3 == 0) f |= bit;
                return f;
            };
            RandomDescription(*first, r, false, flags());
            RandomDescription(*second, r, false, flags());
            RandomDescription(*third, r, false, flags() | (r() % 3 == 0 ? 0x20000000u : 0u));
            gDescriptions[0] = first.get();
            gDescriptions[1] = second.get();
            gDescriptions[2] = third.get();
            gHaveSurface = r() % 2 != 0;
            gGiveShape = r() % 3 == 0;
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
            const bool followerAttached = seed % 4 != 0;
            if (followerAttached) CallSlot<void>(f, 0, static_cast<void *>(&w.owners[0]));
            Fingerprint(b);
            if (r() % 2) { // as the step leaves it: with its parent, under it or not
                object->mParent.Assign(reinterpret_cast<nGZSceneGraph::cICompositionTreeNode *>(&w.trees[0]));
                if (r() % 2) RenderableOf(object->mRenderable.Get())->parent = &w.trees[0];
                Fingerprint(b);
            }
            if (r() % 2) {
                CallSlot<void>(e, 10, shape::P(f));
                Fingerprint(b);
            }
            for (uint32_t step = 0; step < 48; ++step) {
                b.push_back(0x0b000000u | step);
                Step(mode, w, followerAttached && r() % 3 == 0 ? follower : object, static_cast<cITSComponentEffect *>(follower), r, b);
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
            for (const FakeNode *n : {&gSurface, &gConstraint, &gShape, &gColliders[0], &gColliders[1], &gDeaths[0], &gDeaths[1], &gDeaths[2], &gDeaths[3]}) b.push_back(n->refs);
            *GameAddress<void *>(Effect::kPoolFreeCell) = nullptr;
        }
        Same(ctx, blobs[0], blobs[1], "Particles_Effect_Slots", seed);
    }
}


// The frame test's added seeds (120 and on): the description's high flags (the path integrators, the steered ones, the placed
// variants), the stretch, drag, wind, screw, colliders and the preroll, and the effect's scales, on a second random stream so the
// first 120 seeds are what they were. Each integrator class gets every sixth seed.
namespace fr {
std::mt19937 gHit;
const char *gScratch;
bool __fastcall ColliderPlace(FakeNode *n, void *, const cTSParticleCollider *d, const EffectsTransform *local,
                              const EffectsTransform *world, char *state)
{
    // 0xbb6830 calls slot 4 (thiscall, ret 0x10) with the collider's description, the effect's two transforms and its scratch.
    Ev({evCollider | 4, n->id, uint32_t(state - gScratch), Bits(d->m50), Bits(local->translation.x), Bits(local->scale),
        Bits(world->translation.y), Bits(world->scale), uint32_t(local->flags) | uint32_t(world->flags) << 8});
    for (uint32_t i = 0; i < (n->id & 7); ++i) state[i] = char(Bits(local->translation.x) >> i);
    return true;
}
bool __fastcall ColliderHit(FakeNode *n, void *, const cTSParticleCollider *d, char *state, float distance, const Vector3 *vector,
                            Vector3 *position, Vector3 *normal)
{
    Ev({evCollider | 5, n->id, uint32_t(state - gScratch), Bits(distance), Bits(vector->x), Bits(vector->y), Bits(vector->z),
        Bits(position->x), Bits(position->y), Bits(position->z), Bits(d->m50)});
    const bool hit = gHit() % 4 != 0;
    if (hit) *normal = {Tame(gHit) / 1024.0f, Tame(gHit) / 1024.0f, Tame(gHit) / 1024.0f};
    return hit;
}
uint32_t __fastcall OwnerHold(FakeOwner *o, void *, uint32_t a)
{
    Ev({evOwnerOther | 8, o->id, a});
    return 0xabcdef00u;
}
void __fastcall DeathArgument(FakeNode *n, void *, uint32_t a) { Ev({evDeath | 8, n->id, a}); }

// Which integrator the description picks: 0 simple, 1 substeps, 2 steered, 3 steered substeps, 4 on the path, 5 placed on it.
int Class(const cTSParticlesDescription &d)
{
    const uint32_t low = uint32_t(d.mFlags), high = uint32_t(d.mFlags >> 32);
    if (high & 4) return high & 8 ? 5 : 4;
    if (high & 2) return high & 8 ? 3 : 2;
    return (low & 0x04028018u) || 0.05f < fx::Mul(0.25f, d.mDrag) ? 1 : 0;
}

constexpr uint32_t kOnlyFlag[5] = {0x8, 0x10, 0x8000, 0x20000, 0x4000000};

// A transform with a random scale, translation and (in some) a turn about z (a 3-4-5 triangle: exact).
void Place(EffectsTransform &t, std::mt19937 &rx)
{
    t.scale = rx() % 3 == 0 ? 1.0f : Positive(rx);
    if (rx() % 3) {
        t.flags |= EffectsTransform::kTranslated;
        t.translation = {Tame(rx), Tame(rx), Tame(rx)};
    }
    if (rx() % 3 == 0) {
        t.flags |= EffectsTransform::kRotated;
        t.m[0][0] = t.m[1][1] = 0.6f;
        t.m[0][1] = -0.8f;
        t.m[1][0] = 0.8f;
    }
}

// Returns true when the velocity is built to be exactly the "too slow to stretch" limit (the compare's edge).
bool Describe(cTSParticlesDescription &d, uint32_t seed, std::mt19937 &rx)
{
    uint32_t low = uint32_t(d.mFlags), high = 0;
    for (uint32_t bit : {1u, 2u, 0x4000u, 0x10000000u, 0x400000u, 0x8u, 0x10u, 0x8000u, 0x20000u, 0x4000000u})
        if (rx() % 4 == 0) low |= bit;
    switch (seed % 6) {
    case 1: high = 4; break;
    case 2: high = 4 | 8; break;
    case 3: high = 2; break;
    case 4: high = 2 | 8; break;
    case 5: high = rx() % 16; break;
    default: break;
    }
    d.mFlags = uint64_t(high) << 32 | low;
    if ((high & 6) || (low & 0x140)) {
        d.mPath.mPoints.clear();
        float t = rx() % 2 ? 0.0f : Positive(rx) / 4.0f;
        for (unsigned i = 0, n = 1 + rx() % 4; i < n; ++i) {
            cTSParticlePathPoint pt{};
            pt.mPosition = {Tame(rx), Tame(rx), Tame(rx)};
            pt.mVelocity = {Tame(rx), Tame(rx), Tame(rx)};
            pt.mTime = t;
            t += Positive(rx) / 2.0f;
            d.mPath.mPoints.push_back(pt);
        }
    }
    d.mStretch = rx() % 5 == 0 ? 0.0f : rx() % 5 == 0 ? Tame(rx) : Positive(rx);
    d.mDrag = rx() % 3 ? Tame(rx) / 4.0f : 0.0f;
    if (rx() % 6 == 0) {
        d.mDrag = 0.2f; // times a step of 0.25: the simple integrator's limit exactly (every frame), on the simple integrator
        d.mPreroll = 0.0f;
        d.mFlags &= ~(uint64_t(0x04028018u) | (uint64_t(0xf) << 32));
    }
    else if (!(high & 6) && rx() % 4 == 0) {
        d.mDrag = 0.25f; // a step under 0.2 stays under the limit, the same step and the preroll (0.5) over it
        d.mPreroll = 0.5f;
        d.mFlags &= ~(uint64_t(0x04028018u) | (uint64_t(0xf) << 32)); // (on the simple integrator, the limit decides)
    } else if (rx() % 4 == 0) {
        d.mPreroll = 0.5f; // a preroll on any integrator, whichever the drag
    }
    if (!(high & 6) && d.mDrag != 0.2f && d.mDrag != 0.25f && rx() % 4 != 0) {
        // Exactly one of the flags that send a particle to the substeps (each alone, with a drag under the limit).
        d.mFlags &= ~uint64_t(0x04028018u);
        d.mFlags |= kOnlyFlag[seed / 6 % 5];
        d.mDrag = 0.0f;
    }
    d.mGlobalWind = rx() % 3 == 0 ? 0.0f : Tame(rx);
    // Velocities and forces with zero components (the longest component is not the first), some of them the same.
    const auto zeroed = [&] { return rx() % 2 ? Tame(rx) : 0.0f; };
    d.m38 = {zeroed(), zeroed(), zeroed()};
    d.m44 = rx() % 4 == 0 ? d.m38 : fxdesc::Vector3{zeroed(), zeroed(), zeroed()};
    if (rx() % 3 == 0) d.mForce = {zeroed(), zeroed(), zeroed()};
    bool tiny = false;
    if (rx() % 5 == 0 && !(high & 6)) { // nothing changes the velocity: it is the speed along x, exactly the stretch's limit
        d.m38 = d.m44 = {0.0f, 0.0f, 0.0f};
        d.mEmitVolume = {1e-6f, 1e-6f};
        d.mForce = {0.0f, 0.0f, 0.0f};
        d.mGlobalWind = 0.0f;
        d.mDrag = 0.0f;
        d.mStretch = Positive(rx);
        tiny = true;
    } else if (rx() % 4 == 0 && !(high & 6)) { // a velocity along one axis only (y or z): the longest component is not the first
        const float v = Tame(rx);
        d.m38 = d.m44 = rx() % 2 ? fxdesc::Vector3{0.0f, v, 0.0f} : fxdesc::Vector3{0.0f, 0.0f, v};
        d.mEmitVolume = {1.0f, 1.0f};
        d.mForce = {0.0f, 0.0f, 0.0f};
        d.mGlobalWind = 0.0f;
        d.mDrag = 0.0f;
        d.mStretch = Positive(rx);
        tiny = true;
    }
    // The calm ones go by the simple integrator with a velocity kept as it is made (no path, no substeps, no stretch of the start).
    if (tiny) d.mBomb = 0.0f;
    if (tiny) d.mPath.mPoints.clear(); // a path's first point gives the velocity instead
    if (tiny) d.mFlags &=uint64_t(0xffffffffu) & ~uint64_t(0x04028018u | 0x60u);
    if (rx() % 3 == 0) {
        RandomCurve(d.mScrewRadius, rx, true);
        RandomCurve(d.mScrewAngle, rx, true);
        if (rx() % 3 == 0) d.mScrewRadius.mValues[0] = 0.0f; // a first zero in a longer curve is not the fast path's
    }
    if (rx() % 8 == 0) {
        d.mRate.mKind = cTSEffectCurve::kList;
        d.mRate.mValues.assign(1, 20.0f); // the rate limit's edge
    }
    d.mColliders.clear();
    if (rx() % 3 == 0)
        for (unsigned i = 0, n = 1 + rx() % 2; i < n; ++i) {
            cTSParticleCollider c;
            c.mName = "death";
            d.mColliders.push_back(c);
        }
    return tiny;
}

// The effect's own values (what events set), after Start: the ranges, the scales, the wind, the box limits, the previous place
// (a squared distance of exactly 1 in some).
void Tweak(Effect &o, std::mt19937 &rx, bool tiny)
{
    const auto scale = [&] { return rx() % 4 ? Positive(rx) / 2.0f : Tame(rx); };
    // The rate's scales stay positive: a negative one makes a count the game's emitters allocate for ever on.
    const auto rate = [&] { return rx() % 8 == 0 ? 0.0f : Positive(rx) / 2.0f; };
    for (float &v : o.mRange) v = scale();
    o.mRange[0] = rate();
    o.mRange[1] = rx() % 4 ? rate() : o.mRange[0];
    o.mRateScale = rx() % 4 ? 1.0f : rate();
    o.mLifeScale = rx() % 4 ? 1.0f : scale();
    o.mEvent3 = rx() % 4 ? 1.0f : scale();
    o.mEvent4 = {scale(), scale(), scale()};
    o.mMaxAlpha = rx() % 2 ? 1.0f : Positive(rx);
    o.mWind = {Tame(rx), Tame(rx), Tame(rx)};
    if (tiny) {
        o.mWind = {0.0f, 0.0f, 0.0f};
        o.mSpeedScale = 1.0f;
    }
    o.mPrerollPending = rx() % 3 == 0;
    o.mVisible = rx() % 5 != 0;
    switch (rx() % 4) {
    case 0: o.mPreviousPosition = {o.mLocal.translation.x - 1.0f, o.mLocal.translation.y, o.mLocal.translation.z}; break;
    case 1: o.mPreviousPosition = {Tame(rx), Tame(rx), Tame(rx)}; break;
    default: break;
    }
    if (rx() % 3 == 0) o.mLocal.scale = rx() % 2 ? 2.0f : Positive(rx);
    if (rx() % 10 == 0) o.MarkFollowerSet();
    o.mFramesDrawn = uint16_t(rx() % 3);
    o.mWord142 = uint16_t(rx() % 3);
}

float Step(std::mt19937 &rx)
{
    switch (rx() % 12) {
    case 0: return 0.0f;
    case 1: return -Positive(rx) / 16.0f; // (a NaN step makes the continuous emitter allocate for ever, in the game too)
    case 2: return 0.25f;
    case 3: return 0.5f;
    case 4: return 0.1f; // not a sum of few bits: the clock's double sum shows
    case 5: return 1.0f / 30.0f;
    default: return float(1 + rx() % 8) / 32.0f;
    }
}
} // namespace fr

namespace fr {
// The game's factory as AddParticleChild reaches it (cIGZCOM slot 3): a counted child with the shader's list lengths.
int gAddedChildren = 0; // both sides' calls, counted for the floor
childnodes::ChildNodes *gChildNodes = nullptr;
uint32_t gChildrenMade = 0;
uint32_t __fastcall FrameworkMake(FakeCom *com, void *, uint32_t clsid, uint32_t iid, FakeNode **out)
{
    if (clsid != 0x65245517) return ComGetClassObject(com, nullptr, clsid, iid, reinterpret_cast<void **>(out));
    ++gAddedChildren;
    Ev({evMake, clsid, iid});
    uint32_t lists[4];
    for (uint32_t &l : lists) l = gAnswers() % 4;
    *out = gChildNodes->Make(0x1000 + gChildrenMade++ % 4096, lists);
    return 1;
}
void *gFrameworkVT[8];
} // namespace fr

DIFF_TEST(Particles_Effect_Frame, 0xbb6830)
{
    int framesWithParticles = 0; // a frame with none compares nothing; settings of zero pass that way
    int classFrames[6] = {};     // frames with particles on the original's side, by the integrator the description picks
    int placedFrames = 0;        // frames in which the colliders were placed on the original's side
    int oneFlag[5] = {};         // frames with particles that went to the substeps by exactly one of the five flags, drag zero
    int tinyEdge = 0;           // particles (frame by frame) whose longest velocity component is exactly the stretch's limit
    int axisOnly = 0;            // particles moving along y or z only
    gCtx = &ctx;
    Overrides overrides;
    Overrides::Set(gColliderVT, 4, &fr::ColliderPlace);
    Overrides::Set(gColliderVT, 5, &fr::ColliderHit);
    Overrides::Set(gDeathVT, 8, &fr::DeathArgument);
    Overrides::Set(gFakeOwnerVT, 8, &fr::OwnerHold);
    std::memcpy(fr::gFrameworkVT, kFakeComVT, sizeof(fr::gFrameworkVT));
    fr::gFrameworkVT[3] = reinterpret_cast<void *>(&fr::FrameworkMake);
    auto childNodes = std::make_unique<childnodes::ChildNodes>(); // copies the node table: after the overrides
    fr::gChildNodes = childNodes.get();
    gChildNodesLinked = true;
    const uint32_t transformsBefore = childnodes::gTransformSets;
    NodeWorld nodes;
    gEv.reserve(1 << 20);
    shape::StandIn alloc(0x1be3b0, &RZMemory::Malloc, &ArenaMalloc);
    for (uint32_t seed = 0; seed < 1200; ++seed) {
        const bool extra = seed >= 120;
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            char mark[64];
            std::snprintf(mark, sizeof(mark), "frame seed %u mode %d", seed, mode);
            ctx.Mark(mark);
            EffectsWorld w;
            w.com.vt = fr::gFrameworkVT;
            gEv.clear();
            fr::gChildrenMade = 0;
            childNodes->Reset();
            ResetWorld(seed, w);
            gGiveShape = seed >= 120 && seed % 3 == 0;
            for (float &v : gNodeWorld) v = Tame(gAnswers);
            for (uint32_t &v : gNodeParameters) v = gAnswers() % 4 ? gAnswers() % 4 : 0xffffffffu;
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 104729u + 11);
            auto first = std::make_unique<cTSParticlesDescription>();
            uint32_t flags = 0;
            for (uint32_t bit : {4u, 0x40u, 0x100u, 0x100000u})
                if (r() % 3 == 0) flags |= bit;
            RandomDescription(*first, r, true, flags);
            std::mt19937 rx(seed * 2654435761u + 7);
            bool tiny = false;
            if (extra) {
                tiny = fr::Describe(*first, seed, rx);
                // A shape on a third of the seeds: the shape-only branches (add and erase a child, the children's update) run for
                // real on both sides, on tests/ts_child_nodes.h's children.
                gSettings.mRateFactor = rx() % 2 ? 1.0f : Positive(rx) / 2.0f;
                const fxdesc::Vector3 s3 = {Tame(rx), Tame(rx), Tame(rx)};
                std::memcpy(gSettings.mBefore, &s3, sizeof(s3));
                fr::gHit.seed(seed + 5);
            }
            gDescriptions[0] = first.get();
            void *const e = MakeEffect(mode, first.get());
            auto *object = static_cast<Effect *>(static_cast<cITSComponentEffect *>(e));
            gEffects[0] = object;
            gEffects[1] = nullptr;
            CallSlot<uint32_t>(e, 12);
            CallSlot<void>(e, 0, static_cast<void *>(&w.owners[1]));
            {
                EffectsTransform local, parent;
                local = EffectsTransform{};
                parent = EffectsTransform{};
                local.scale = parent.scale = 1.0f;
                local.m[0][0] = local.m[1][1] = local.m[2][2] = 1.0f;
                parent.m[0][0] = parent.m[1][1] = parent.m[2][2] = 1.0f;
                if (extra) {
                    fr::Place(local, rx);
                    fr::Place(parent, rx);
                }
                CallSlot<void>(e, 6, &local, &parent);
            }
            CallSlot<void>(e, 2, r() % 2);
            if (extra) {
                fr::Tweak(*object, rx, tiny);
                fr::gScratch = object->mColliderState.data();
            }
            Fingerprint(b);
            uint32_t stats[8] = {};
            for (uint32_t frame = 0; frame < 32; ++frame) {
                b.push_back(0x0b000000u | frame);
                float step = float(1 + r() % 8) / 32.0f;
                if (extra) {
                    step = first->mDrag == 0.2f ? 0.25f
                         : first->mDrag == 0.25f && first->mPreroll == 0.5f ? float(1 + rx() % 5) / 32.0f
                                                                           : fr::Step(rx);
                    if (rx() % 6 == 0) object->mPrerollPending = true; // the preroll with particles in the list
                    object->mFramesDrawn += uint16_t(rx() % 2);
                    if (rx() % 8 == 0 && !object->mParticles.empty()) object->mParticles.front()->mAlive = 0; // dead before the step
                    fr::gScratch = object->mColliderState.data();
                }
                const bool placedBefore = object->mCollidersPlaced;
                CallSlot<void>(e, 5,reinterpret_cast<void *>(uintptr_t(Bits(step))), static_cast<void *>(stats));
                framesWithParticles += object->mCount > 0;
                if (mode == 0 && object->mCount > 0) ++classFrames[fr::Class(*first)];
                if (mode == 0 && object->mCount > 0 && !(first->mFlags >> 32 & 6) && first->mDrag == 0.0f)
                    for (int k = 0; k < 5; ++k)
                        if ((uint32_t(first->mFlags) & 0x04028018u) == fr::kOnlyFlag[k]) ++oneFlag[k];
                if (mode == 0)
                    for (const cTSParticle *p : object->mParticles) {
                        const float vx = std::fabs(p->mVelocity.x), vy = std::fabs(p->mVelocity.y), vz = std::fabs(p->mVelocity.z);
                        tinyEdge += (vx > vy ? (vx > vz ? vx : vz) : (vy > vz ? vy : vz)) == 1e-6f;
                        axisOnly += vx == 0.0f && ((vy == 0.0f) != (vz == 0.0f)) && (vy > 1e-6f || vz > 1e-6f);
                    }
                if (mode == 0 && !placedBefore && object->mCollidersPlaced && !object->mColliders.empty()) ++placedFrames;
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
            for (const FakeOwner &o : w.owners) b.push_back(o.refs);
            for (const FakeNode &n : w.trees) b.push_back(n.refs);
        }
        Same(ctx, blobs[0], blobs[1], "Particles_Effect_Frame", seed);
    }
    if (framesWithParticles < 1000) ctx.Fail("only %d frames had particles", framesWithParticles);
    for (int c = 0; c < 6; ++c)
        if (classFrames[c] < 100) ctx.Fail("integrator class %d: only %d frames with particles", c, classFrames[c]);
    if (placedFrames < 20) ctx.Fail("only %d frames placed colliders", placedFrames);
    for (int k = 0; k < 5; ++k)
        if (oneFlag[k] < 20) ctx.Fail("substep flag %#x alone: only %d frames with particles", fr::kOnlyFlag[k], oneFlag[k]);
    if (tinyEdge < 20) ctx.Fail("only %d particle-frames at the stretch's speed limit", tinyEdge);
    if (axisOnly < 20) ctx.Fail("only %d particle-frames along one axis", axisOnly);
    gChildNodesLinked = false;
    const uint32_t childTransforms = childnodes::gTransformSets - transformsBefore;
    if (childTransforms < 400) ctx.Fail("only %u particle children placed (both sides)", childTransforms);
    if (fr::gAddedChildren < 400) ctx.Fail("only %d particle children added (both sides)", fr::gAddedChildren);
    std::printf("frame test: classes %d %d %d %d %d %d, colliders placed %d, tiny edge %d, axis only %d, children placed %u, children added %d\n",
                classFrames[0], classFrames[1], classFrames[2], classFrames[3], classFrames[4], classFrames[5], placedFrames, tinyEdge,
                axisOnly, childTransforms, fr::gAddedChildren);
}
