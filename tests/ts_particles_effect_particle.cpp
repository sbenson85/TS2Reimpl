// The particle's code (lane WV, rendering row E5): original vs ours, one test per step of the recipe in docs/subsystems/rendering.md.
//
//   Particles_Math      0xbbcd80 RandomInBox, 0xbc2f80 Normalize, 0xbdd2f0 RandomInEllipsoid, 0xbdd430 RandomOnRing: free functions,
//                       random boxes (NaNs and signs included), the results and the game's generator compared
//
// The effect-bound steps are in this file too, built on the world of tests/ts_particles_world.h (the original and ours called for
// real, one side each, from the same seed).
#include "ts_particles_world.h"

#include "stack_out.h"
#include "ts/ParticleMath.h"

namespace {

void SeedRandom(uint32_t seed)
{
    auto *random = GameAddress<cRZRandom>(0x10fd138);
    random->mIntegerSeed = seed * 2654435761u + 12345u;
    random->mRealSeed = int32_t(seed * 40503u + 77u);
}
void PushRandom(Blob &b)
{
    auto *random = GameAddress<cRZRandom>(0x10fd138);
    b.push_back(random->mIntegerSeed);
    b.push_back(uint32_t(random->mRealSeed));
}
void PushVector(Blob &b, const Vector3 &v) { b.insert(b.end(), {Bits(v.x), Bits(v.y), Bits(v.z)}); }

} // namespace

DIFF_TEST(Particles_Math, 0xbbcd80)
{
    gCtx = &ctx;
    using BoxFn = Vector3 *(__cdecl *)(Vector3 *, const float *);
    using RingFn = Vector3 *(__cdecl *)(Vector3 *, const float *, float);
    using NormalizeFn = Vector3 *(__cdecl *)(Vector3 *, const Vector3 *);
    const BoxFn inBox = ctx.Original<BoxFn>(0xbbcd80);
    const NormalizeFn normalize = ctx.Original<NormalizeFn>(0xbc2f80);
    const BoxFn inEllipsoid = ctx.Original<BoxFn>(0xbdd2f0);
    const RingFn onRing = ctx.Original<RingFn>(0xbdd430);
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 31u + 5);
            const bool tame = seed % 3 != 0;
            float box[6];
            for (float &v : box) v = tame ? Tame(r) : Wild(r);
            Vector3 in = {tame ? Tame(r) : Wild(r), tame ? Tame(r) : Wild(r), tame ? Tame(r) : Wild(r)};
            if (seed % 7 == 0) in = {Tame(r) * 1e-4f, 0.0f, Tame(r) * 1e-4f}; // around the threshold
            if (seed % 11 == 0) in = {1e-3f, 0.0f, 0.0f};                      // 1e-6 exactly, as a float's square
            const float ratio = tame ? Positive(r) * 0.5f : Wild(r);
            SeedRandom(seed);
            Vector3 out[4];
            for (Vector3 &o : out) o = {std::bit_cast<float>(0x7fa5a5a5u), 1.0f, 2.0f}; // the bytes it leaves alone are compared
            Vector3 *returned[4];
            if (mode) {
                returned[0] = fx::RandomInBox(&out[0], box);
                returned[1] = fx::Normalize(&out[1], &in);
                returned[2] = fx::RandomInEllipsoid(&out[2], box);
                returned[3] = fx::RandomOnRing(&out[3], box, ratio);
            } else {
                returned[0] = inBox(&out[0], box);
                returned[1] = normalize(&out[1], &in);
                returned[2] = inEllipsoid(&out[2], box);
                returned[3] = onRing(&out[3], box, ratio);
            }
            for (int i = 0; i < 4; ++i) {
                b.push_back(returned[i] == &out[i]);
                PushVector(b, out[i]);
            }
            PushRandom(b);
        }
        Same(ctx, blobs[0], blobs[1], "Particles_Math", seed);
    }
}

// ---- step 2: the forces ----

namespace {

// A random set of attractors: descriptions (the byte 0..5, the three factors, the curve, the range, the minimum distance) and the
// running states over them (the centre, the strength). Strengths and curves stay finite: the original multiplies them on the x87
// stack, where two NaNs of different payloads meet differently from SSE's.
struct AttractorSet {
    std::vector<cTSParticleAttractor> descriptions;
    std::vector<cTSParticleAttractorState> states;
    Vector3 position;
};
AttractorSet RandomAttractors(std::mt19937 &r, uint32_t seed)
{
    const bool tame = seed % 3 != 0;
    const auto value = [&] { return tame ? Tame(r) : Wild(r); };
    AttractorSet set;
    const unsigned count = r() % 5;
    set.descriptions.resize(count);
    set.states.resize(count);
    set.position = {value(), value(), value()};
    for (unsigned i = 0; i < count; ++i) {
        cTSParticleAttractor &a = set.descriptions[i];
        a.mHasCentre = uint8_t(r() % 6);
        a.m10 = value();
        a.m14 = value();
        a.m18 = value();
        RandomCurve(a.mCurve, r, true);
        a.mStrength = r() % 5 == 0 ? 0.0f : tame ? Positive(r) * 2.0f : Wild(r);
        a.m30 = r() % 3 == 0 ? 0.0f : r() % 3 == 0 ? -1.0f : Positive(r);
        cTSParticleAttractorState &s = set.states[i];
        s.mCentre = r() % 4 == 0 ? set.position : Vector3{value(), value(), value()};
        s.mStrength = r() % 5 == 0 ? 0.0f : Tame(r);
        s.mDescription = r() % 9 == 0 ? nullptr : &a;
        if (seed % 2 == 0 && r() % 4 == 0) { // the distance lands exactly on the description's minimum
            const float reach = r() % 2 ? 0.5f : 2.0f;
            set.position = {std::floor(set.position.x * 8.0f) / 8.0f, std::floor(set.position.y * 8.0f) / 8.0f,
                            std::floor(set.position.z * 8.0f) / 8.0f};
            a.m30 = reach;
            s.mCentre = {set.position.x + reach, set.position.y, set.position.z};
            s.mDescription = &a;
        }
    }
    return set;
}

} // namespace

DIFF_TEST(Particles_Attractors, 0xbb9840)
{
    gCtx = &ctx;
    using ForceFn = Vector3 *(__thiscall *)(const cTSParticleAttractorState *, Vector3 *, const Vector3 *);
    using SumFn = Vector3 *(__fastcall *)(std::vector<cTSParticleAttractorState> *, void *, Vector3 *, const Vector3 *, char *);
    const ForceFn force = ctx.Original<ForceFn>(0xbb9840);
    const SumFn sum = ctx.Original<SumFn>(0xbb9b90);
    for (uint32_t seed = 0; seed < 4000; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 17u + 3);
            AttractorSet set = RandomAttractors(r, seed);
            SeedRandom(seed);
            for (const cTSParticleAttractorState &s : set.states) {
                Vector3 out = {std::bit_cast<float>(0x7fa5a5a5u), 1.0f, 2.0f};
                const Vector3 *returned = mode ? s.Force(&out, &set.position) : force(&s, &out, &set.position);
                b.push_back(returned == &out);
                PushVector(b, out);
            }
            PushRandom(b);
            for (int pass = 0; pass < 3; ++pass) {
                char stop = char(0x5a);
                Vector3 out = {1.0f, 2.0f, 3.0f};
                const Vector3 *returned =
                    mode ? cTSParticleAttractorState::Sum(&set.states, nullptr, &out, &set.position, pass ? &stop : nullptr)
                         : sum(&set.states, nullptr, &out, &set.position, pass ? &stop : nullptr);
                b.push_back(returned == &out);
                PushVector(b, out);
                b.push_back(uint8_t(stop));
                PushRandom(b);
                // The next pass sees a point on one of the centres.
                if (!set.states.empty()) set.position = set.states[pass % set.states.size()].mCentre;
            }
        }
        Same(ctx, blobs[0], blobs[1], "Particles_Attractors", seed);
    }
}

// ---- step 2: the collision sweep ----

namespace {

constexpr uint32_t kSweep = 0xbdf6a0, kSpawn = 0xbe01c0, kCollide = 0xbb6730;
using Keep = std::vector<fx::CountedRef<cIGZUnknown>>;
using Colliders = std::vector<cTSParticleColliderState>;
using SweepFn = bool(__cdecl *)(void *, Colliders *, std::vector<char> *, float, const Vector3 *, Vector3 *, Vector3 *, uint32_t, Keep *,
                                EffectsTransform *);
using SpawnFn = void(__cdecl *)(void *, const char *, const Vector3 *, uint32_t, Keep *, EffectsTransform *);
using CollideFn = bool(__thiscall *)(Effect *, cTSParticle *, Vector3);

// The fakes of this step. A collider's slot 5 (the original callee fills `normal`, the caller's three floats: 0xbdf6a0 reads
// its x, y and z) answers from a script; the owner's slot 8 holds or not; a spawned effect's slot 8 logs its argument.
std::mt19937 gHit;
const char *gScratchBase;
bool gHold;
bool __fastcall ColliderHit(FakeNode *n, void *, const cTSParticleCollider *d, char *state, float distance, const Vector3 *vector,
                            Vector3 *position, Vector3 *normal)
{
    Ev({evCollider | 5, n->id, uint32_t(state - gScratchBase), Bits(distance), Bits(vector->x), Bits(vector->y), Bits(vector->z),
        Bits(position->x), Bits(position->y), Bits(position->z), Bits(d->m50)});
    const bool hit = gHit() % 4 != 0;
    if (hit) *normal = {Tame(gHit) / 1024.0f, Tame(gHit) / 1024.0f, Tame(gHit) / 1024.0f};
    return hit;
}
uint32_t __fastcall OwnerHold(FakeOwner *o, void *, uint32_t a)
{
    Ev({evOwnerOther | 8, o->id, a});
    return gHold ? 0xabcdef01u : 0xabcdef00u; // only al counts
}
void __fastcall DeathArgument(FakeNode *n, void *, uint32_t a) { Ev({evDeath | 8, n->id, a}); }

void SetStepFakes()
{
    Overrides::Set(gColliderVT, 5, &ColliderHit);
    Overrides::Set(gDeathVT, 8, &DeathArgument);
    Overrides::Set(gFakeOwnerVT, 8, &OwnerHold);
}

struct Sweep {
    std::vector<cTSParticleCollider> descriptions;
    Colliders states;
    std::vector<char> scratch = std::vector<char>(64, char(0x33));
    Keep keep;
    bool haveKeep = false, haveParent = false;
    EffectsTransform parent;
    uint32_t argument = 0;
};
void RandomSweep(Sweep &s, std::mt19937 &r, const Vector3 &velocity)
{
    static const char *const texts[] = {"", "", "spark", "a_hit_effect_name_over_fifteen", "x"};
    static const float chances[] = {0.0f, 0.0f, 1.0f, 0.5f, 0.25f, 0.9f, -1.0f, 2.0f, std::bit_cast<float>(0x7fc00001u)};
    const unsigned count = r() % 5;
    s.descriptions.reserve(count);
    s.states.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        cTSParticleCollider &d = s.descriptions.emplace_back();
        d.m40 = Tame(r);
        d.m44 = Tame(r);
        const float lengthSquared = fx::Add(fx::Add(fx::Mul(velocity.x, velocity.x), fx::Mul(velocity.y, velocity.y)), fx::Mul(velocity.z, velocity.z));
        switch (r() % 7) {
        case 0: d.m48 = 0.0f; break;
        case 1: d.m48 = Positive(r) * 0.1f; break;
        case 2: d.m48 = lengthSquared; break;
        case 3: d.m48 = std::nextafter(lengthSquared, 1e30f); break;
        case 4: d.m48 = std::nextafter(lengthSquared, 0.0f); break;
        case 5: d.m48 = std::bit_cast<float>(0x7fc00002u); break;
        default: d.m48 = Positive(r) * 20.0f; break;
        }
        d.m50 = chances[r() % std::size(chances)];
        d.m54 = texts[r() % std::size(texts)];
        d.m6C = texts[r() % std::size(texts)];
        s.states.push_back({reinterpret_cast<cIGZUnknown *>(&gColliders[r() % 4]), &d, uint32_t(r() % 40)});
    }
    s.haveKeep = r() % 4 != 0;
    if (s.haveKeep) {
        s.keep.reserve(8);
        const unsigned before = r() % 3;
        for (unsigned i = 0; i < before; ++i) s.keep.emplace_back(reinterpret_cast<cIGZUnknown *>(&gDeaths[i]));
    }
    s.haveParent = r() % 2 != 0;
    RandomTransform(s.parent, r);
    s.parent.scale = 1.0f;
    s.argument = r() % 3 ? uint32_t(r() % 5) : r();
}
void PushKeep(Blob &b, const Keep &keep)
{
    b.push_back(uint32_t(keep.size()));
    for (const auto &k : keep) b.push_back(IdOf(k.Get()));
}
void PushDeaths(Blob &b)
{
    for (const FakeNode &n : gDeaths) b.push_back(n.refs);
}

} // namespace

DIFF_TEST(Particles_Colliders, kSweep)
{
    gCtx = &ctx;
    Overrides overrides;
    SetStepFakes();
    gEv.reserve(1 << 20);
    const SweepFn sweep = ctx.Original<SweepFn>(kSweep);
    const SpawnFn spawn = ctx.Original<SpawnFn>(kSpawn);
    for (uint32_t seed = 0; seed < 3000; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            char mark[64];
            std::snprintf(mark, sizeof(mark), "sweep seed %u mode %d", seed, mode);
            ctx.Mark(mark);
            EffectsWorld w;
            gEv.clear();
            ResetWorld(seed, w);
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 6151u + 7);
            gHit.seed(seed + 5);
            gHold = r() % 8 == 0;
            SeedRandom(seed);
            const bool tame = seed % 5 != 0;
            const auto value = [&] { return tame ? Tame(r) : Wild(r); };
            Vector3 vector = {value(), value(), value()}, position = {value(), value(), value()}, velocity = {Tame(r), Tame(r), Tame(r)};
            if (!tame) velocity = {Wild(r), Wild(r), Wild(r)};
            const float distance = value();
            {
                Sweep s;
                RandomSweep(s, r, velocity);
                gScratchBase = s.scratch.data();
                FakeOwner *const owner = &w.owners[1];
                if (seed % 4 == 3) { // SpawnEffect on its own
                    const char *name = seed % 8 == 3 ? "a_spawned_effect_name_over_fifteen" : "spark";
                    if (mode)
                        fx::SpawnEffect(reinterpret_cast<cIGZUnknown *>(owner), name, &position, s.argument, s.haveKeep ? &s.keep : nullptr,
                                        s.haveParent ? &s.parent : nullptr);
                    else
                        spawn(owner, name, &position, s.argument, s.haveKeep ? &s.keep : nullptr, s.haveParent ? &s.parent : nullptr);
                } else {
                    const bool lives =
                        mode ? fx::SweepColliders(reinterpret_cast<cIGZUnknown *>(owner), &s.states, &s.scratch, distance, &vector, &position,
                                                  &velocity, s.argument, s.haveKeep ? &s.keep : nullptr, s.haveParent ? &s.parent : nullptr)
                             : sweep(owner, &s.states, &s.scratch, distance, &vector, &position, &velocity, s.argument,
                                     s.haveKeep ? &s.keep : nullptr, s.haveParent ? &s.parent : nullptr);
                    b.push_back(lives);
                }
                PushVector(b, position);
                PushVector(b, velocity);
                PushVector(b, vector);
                PushRandom(b);
                PushKeep(b, s.keep);
                PushDeaths(b);
                Drain(b);
            }
            Drain(b); // the keep list's releases
            PushDeaths(b);
            b.push_back(w.owners[1].refs);
        }
        Same(ctx, blobs[0], blobs[1], "Particles_Colliders", seed);
    }
}

DIFF_TEST(Particles_CollideColliders, kCollide)
{
    gCtx = &ctx;
    Overrides overrides;
    SetStepFakes();
    NodeWorld nodes;
    gEv.reserve(1 << 20);
    shape::StandIn alloc(0x1be3b0, &RZMemory::Malloc, &ArenaMalloc);
    for (uint32_t seed = 0; seed < 2500; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            char mark[64];
            std::snprintf(mark, sizeof(mark), "collide seed %u mode %d", seed, mode);
            ctx.Mark(mark);
            std::unique_ptr<Precision> precision;
            if (seed % 2) precision = std::make_unique<Precision>();
            EffectsWorld w;
            gEv.clear();
            ResetWorld(seed, w);
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 9973u + 1);
            auto description = std::make_unique<cTSParticlesDescription>();
            RandomDescription(*description, r, true, 0);
            description->mAlphaVary = r() % 3 == 0 ? 0.0f : r() % 8 == 0 ? std::bit_cast<float>(0x7fc00003u) : Tame(r);
            gDescriptions[0] = description.get();
            void *const e = MakeEffect(mode, description.get());
            auto *object = static_cast<Effect *>(static_cast<cITSComponentEffect *>(e));
            gHit.seed(seed + 5);
            gHold = r() % 8 == 0;
            SeedRandom(seed);
            Vector3 vector = {Tame(r), Tame(r), Tame(r)};
            cTSParticle particle;
            std::memset(&particle, 0x5a, sizeof(particle));
            particle.mLife = r() % 4 ? Positive(r) : 1.0f;
            particle.mAge = float(r() % 1025) / 512.0f;
            particle.mSize = Tame(r);
            particle.mPosition = {Tame(r), Tame(r), Tame(r)};
            particle.mVelocity = {Tame(r), Tame(r), Tame(r)};
            object->mOwner.Assign(reinterpret_cast<cIGZUnknown *>(&w.owners[1]));
            object->mSizeScale = Tame(r);
            object->mWorld.scale = Tame(r);
            object->mVisible = r() % 2;
            Sweep s;
            RandomSweep(s, r, particle.mVelocity);
            object->mColliders = s.states;
            object->mColliderState = s.scratch;
            gScratchBase = object->mColliderState.data();
            b.push_back(mode ? object->CollideColliders(&particle, vector) : ctx.Original<CollideFn>(kCollide)(object, &particle, vector));
            b.insert(b.end(), reinterpret_cast<const uint32_t *>(&particle), reinterpret_cast<const uint32_t *>(&particle + 1));
            PushRandom(b);
            PushVector(b, vector);
            Colliders().swap(object->mColliders);
            std::vector<char>().swap(object->mColliderState);
            object->mOwner.Reset();
            object->mRefCount = 1;
            b.push_back(CallSlot<uint32_t>(e, 13));
            Drain(b);
            PushDeaths(b);
        }
        Same(ctx, blobs[0], blobs[1], "Particles_CollideColliders", seed);
    }
}

// ---- step 2c: the surface and the forces ----

namespace {

constexpr uint32_t kSurfaceCollide = 0xbb85b0, kForces = 0xbb82f0;
using SurfaceCollideFn = bool(__thiscall *)(Effect *, cTSParticle *, const Vector3 *, float);
using ForcesFn = bool(__thiscall *)(Effect *, cTSParticle *, const Vector3 *, float);

// The surface object's fakes (a height field; the concrete class is not identified). Slot 5 is a thiscall (x, y) whose float
// comes back on the x87 stack (0xbb85b0 reads it with fstp); slot 6 is a thiscall (out, x, y) that fills `out` and returns it
// (the callers of 0xbb85b0 read all 12 bytes of what it writes: three floats). Both answer from a script.
std::mt19937 gSurf;
float __fastcall SurfaceHeightFake(FakeNode *n, void *, float x, float y)
{
    float h = Tame(gSurf) / 2.0f;
    if (gSurf() % 40 == 0) h = std::bit_cast<float>(0x7fc00000u);
    Ev({evSurface | 5, n->id, Bits(x), Bits(y), Bits(h)});
    return h;
}
Vector3 *__fastcall SurfaceNormalFake(FakeNode *n, void *, Vector3 *out, float x, float y)
{
    *out = {Tame(gSurf) / 4.0f, Tame(gSurf) / 4.0f, Tame(gSurf) / 4.0f};
    Ev({evSurface | 6, n->id, Bits(x), Bits(y), Bits(out->x), Bits(out->y), Bits(out->z)});
    return out;
}
void SetSurfaceFakes()
{
    Overrides::Set(gSurfaceVT, 5, &SurfaceHeightFake);
    Overrides::Set(gSurfaceVT, 6, &SurfaceNormalFake);
}

constexpr uint32_t kFlagBits[] = {0x8000, 0x10000, 0x20000, 0x40000, 0x8, 0x10};

// One side's effect, particle and surface for a seed; both sides build the same from the same seed.
struct Rig {
    std::unique_ptr<cTSParticlesDescription> description = std::make_unique<cTSParticlesDescription>();
    void *e = nullptr;
    Effect *object = nullptr;
    cTSParticle particle;
    Vector3 vector;
    float step = 0.0f;
    Sweep sweep;
    AttractorSet attractors;
};

void BuildRig(Rig &g, int mode, uint32_t seed, std::mt19937 &r, EffectsWorld &w, bool forces)
{
    cTSParticlesDescription &d = *g.description;
    uint32_t flags = 0;
    for (uint32_t bit : kFlagBits)
        if (r() % 2) flags |= bit;
    if (!forces) flags &= ~0x18u;
    RandomDescription(d, r, true, flags);
    d.mDrag = r() % 3 ? Tame(r) / 4.0f : 0.0f;
    d.mBounce = r() % 3 == 0 ? 0.0f : r() % 4 == 0 ? -1.0f : Tame(r);
    d.mDeath = r() % 3 == 0 ? 0.0f : r() % 4 == 0 ? 1.0f : r() % 8 == 0 ? std::bit_cast<float>(0x7fc00004u) : Positive(r) / 2.0f;
    d.mTerrainRepel[0] = Positive(r) * 2.0f;
    d.mTerrainRepel[1] = Tame(r);
    d.mTerrainRepel[2] = Tame(r);
    d.mTerrainRepel[3] = Tame(r);
    d.mTerrainRepel[4] = r() % 3 ? -1.0e9f : -Positive(r) * 2.0f;
    d.mAlphaVary = r() % 3 == 0 ? 0.0f : Tame(r);
    gDescriptions[0] = g.description.get();
    g.e = MakeEffect(mode, g.description.get());
    g.object = static_cast<Effect *>(static_cast<cITSComponentEffect *>(g.e));
    gHit.seed(seed + 5);
    gSurf.seed(seed + 77);
    gHold = r() % 8 == 0;
    SeedRandom(seed);
    g.vector = {Tame(r), Tame(r), Tame(r)};
    g.step = r() % 4 ? Positive(r) / 8.0f : Tame(r);
    std::memset(&g.particle, 0x5a, sizeof(g.particle));
    g.particle.mAlive = uint8_t(1 + r() % 3);
    g.particle.mLife = r() % 4 ? Positive(r) : 1.0f;
    g.particle.mAge = float(r() % 1025) / 512.0f;
    g.particle.mSize = Tame(r);
    g.particle.mPosition = {Tame(r), Tame(r), Tame(r)};
    const float speed = r() % 3 == 0 ? 6.0f : 1.0f;
    g.particle.mVelocity = {Tame(r) * speed, Tame(r) * speed, Tame(r) * speed};
    Effect &o = *g.object;
    o.mOwner.Assign(reinterpret_cast<cIGZUnknown *>(&w.owners[1]));
    o.mSizeScale = Tame(r);
    o.mVisible = r() % 2;
    RandomTransform(o.mWorld, r);
    if (r() % 2) { // keep the surface in reach: a plain place over it
        o.mWorld.scale = 1.0f;
        o.mWorld.translation = {Positive(r) * 3.0f, Positive(r) * 3.0f, Tame(r)};
        if (r() % 2) o.mWorld.flags = 0;
    }
    o.mSurfaceRange[0] = r() % 6 == 0 ? Tame(r) : Positive(r) * 6.0f;
    o.mSurfaceRange[1] = r() % 6 == 0 ? Tame(r) : Positive(r) * 6.0f;
    if (r() % 10 == 0) o.mSurfaceRange[1 - r() % 2] = std::bit_cast<float>(0x7fc00005u);
    if (r() % 12) o.mSurface.Assign(reinterpret_cast<cIGZUnknown *>(&gSurface));
    if (forces) {
        g.attractors = RandomAttractors(r, seed);
        o.mAttractors = g.attractors.states;
        if (r() % 2) { // a particle on an attractor's centre
            if (!o.mAttractors.empty()) g.particle.mPosition = o.mAttractors[0].mCentre;
        }
        RandomSweep(g.sweep, r, g.particle.mVelocity);
        o.mColliders = g.sweep.states;
        o.mColliderState = g.sweep.scratch;
        gScratchBase = o.mColliderState.data();
    }
}

void EndRig(Rig &g, Blob &b)
{
    Effect &o = *g.object;
    PushVector(b, g.vector);
    b.insert(b.end(), reinterpret_cast<const uint32_t *>(&g.particle), reinterpret_cast<const uint32_t *>(&g.particle + 1));
    PushRandom(b);
    Colliders().swap(o.mColliders);
    std::vector<char>().swap(o.mColliderState);
    std::vector<cTSParticleAttractorState>().swap(o.mAttractors);
    o.mOwner.Reset();
    o.mSurface.Reset();
    o.mRefCount = 1;
    b.push_back(CallSlot<uint32_t>(g.e, 13));
    b.push_back(gSurface.refs);
    Drain(b);
    PushDeaths(b);
}

} // namespace

DIFF_TEST(Particles_Surface, kSurfaceCollide)
{
    gCtx = &ctx;
    Overrides overrides;
    SetStepFakes();
    SetSurfaceFakes();
    NodeWorld nodes;
    gEv.reserve(1 << 20);
    shape::StandIn alloc(0x1be3b0, &RZMemory::Malloc, &ArenaMalloc);
    for (uint32_t seed = 0; seed < 4000; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            char mark[64];
            std::snprintf(mark, sizeof(mark), "surface seed %u mode %d", seed, mode);
            ctx.Mark(mark);
            std::unique_ptr<Precision> precision;
            if (seed % 2) precision = std::make_unique<Precision>();
            EffectsWorld w;
            gEv.clear();
            ResetWorld(seed, w);
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 7919u + 11);
            Rig g;
            BuildRig(g, mode, seed, r, w, false);
            Vector3 where = {Tame(r), Tame(r), Tame(r)};
            {
                // The comparisons that only a value equal to their edge tells apart: the case builds the equality from what
                // the surface's script will answer first (a copy of its generator), the same on both sides.
                std::mt19937 peek = gSurf;
                const float h = Tame(peek) / 2.0f;
                const bool usable = peek() % 40 != 0;
                cTSParticlesDescription &d = *g.description;
                Effect &o = *g.object;
                const uint64_t clear = ~uint64_t(0x30000); // sticky and repel
                const float negZero = -0.0f;
                if (usable) switch (seed % 6) {
                case 1: // a particle exactly on the surface, no bounce
                    d.mBounce = 0.0f;
                    d.mFlags &= clear;
                    where.z = h;
                    break;
                case 2: // no bounce, a velocity of negative zeros: the speed into the surface is a zero of either sign
                    d.mBounce = 0.0f;
                    d.mFlags &= clear;
                    g.particle.mVelocity = {negZero, negZero, negZero};
                    where.z = h - 1.0f;
                    break;
                case 3:
                case 4: { // the repel field, the particle at the very depth that kills it (3) or the field's reach (4)
                    d.mFlags = (d.mFlags & clear) | 0x20000;
                    o.mSurface.Assign(reinterpret_cast<cIGZUnknown *>(&gSurface));
                    o.mWorld.flags = 0;
                    o.mWorld.scale = 1.0f;
                    o.mWorld.translation = {0.0f, 0.0f, 0.0f};
                    o.mSurfaceRange[0] = o.mSurfaceRange[1] = 1000.0f;
                    g.particle.mVelocity = {0.0f, 0.0f, 0.0f};
                    g.particle.mPosition = {1.5f + Positive(r), 1.5f + Positive(r), Tame(r)};
                    const float over = g.particle.mPosition.z - h;
                    if (seed % 6 == 3) {
                        d.mTerrainRepel[4] = over;
                        d.mTerrainRepel[0] = over + 1.0f;
                    } else {
                        d.mTerrainRepel[4] = -1.0e9f;
                        d.mTerrainRepel[0] = over;
                    }
                    break;
                }
                case 5: { // a bounce, the death chance exactly what the next random number is
                    d.mFlags &= clear;
                    if (d.mBounce == 0.0f) d.mBounce = 0.5f;
                    where.z = h - 1.0f;
                    SeedRandom(seed);
                    const float v = static_cast<float>(fx::GameRandom().RandomDoubleUniform());
                    if (v != 0.0f && v != 1.0f) d.mDeath = v;
                    SeedRandom(seed);
                    break;
                }
                default: break;
                }
            }
            const Vector3 before = where;
            const bool lives = mode ? g.object->CollideSurface(&g.particle, &where, g.step)
                                    : ctx.Original<SurfaceCollideFn>(kSurfaceCollide)(g.object, &g.particle, &where, g.step);
            b.push_back(lives);
            PushVector(b, where);
            b.push_back(where.x == before.x && where.y == before.y && where.z == before.z);
            EndRig(g, b);
        }
        Same(ctx, blobs[0], blobs[1], "Particles_Surface", seed);
    }
}

DIFF_TEST(Particles_Forces, kForces)
{
    gCtx = &ctx;
    Overrides overrides;
    SetStepFakes();
    SetSurfaceFakes();
    NodeWorld nodes;
    gEv.reserve(1 << 20);
    shape::StandIn alloc(0x1be3b0, &RZMemory::Malloc, &ArenaMalloc);
    for (uint32_t seed = 0; seed < 4000; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            char mark[64];
            std::snprintf(mark, sizeof(mark), "forces seed %u mode %d", seed, mode);
            ctx.Mark(mark);
            std::unique_ptr<Precision> precision;
            if (seed % 2) precision = std::make_unique<Precision>();
            EffectsWorld w;
            gEv.clear();
            ResetWorld(seed, w);
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 6007u + 13);
            Rig g;
            BuildRig(g, mode, seed, r, w, true);
            const bool lives = mode ? g.object->ApplyForces(&g.particle, &g.vector, g.step)
                                    : ctx.Original<ForcesFn>(kForces)(g.object, &g.particle, &g.vector, g.step);
            b.push_back(lives);
            EndRig(g, b);
        }
        Same(ctx, blobs[0], blobs[1], "Particles_Forces", seed);
    }
}

// ---- step 3: the path and the integrators ----

namespace {

constexpr uint32_t kPathEvaluate = 0xbba1b0, kSteer = 0xbb8c00, kIntegrate = 0xbc1190, kSimple = 0xbc12a0, kSubsteps = 0xbc13c0,
                   kSteeredSubsteps = 0xbc19f0, kSteered = 0xbc1530, kOnPath = 0xbbcb70, kOnPathPlaced = 0xbc08b0;
using PathFn = bool(__thiscall *)(const cTSParticlePath *, float, cTSParticlePathPoint *);
using StepFn = bool(__thiscall *)(Effect *, cTSParticle *, float);
using SteerFn = void(__thiscall *)(Effect *, cTSParticle *, float);

const float kNaN = std::bit_cast<float>(0x7fc00003u);

// One to five points, times rising by steps that include none, a hair and the very limit the interpolation refuses (1e-6); the
// first point often at 0 (a time of 0 means something to the steering); a NaN time now and then.
void RandomPath(cTSParticlePath &p, std::mt19937 &r)
{
    p.mPoints.assign(1 + r() % 5, cTSParticlePathPoint{});
    float t = r() % 3 == 0 ? 0.0f : Tame(r) / 4.0f;
    for (cTSParticlePathPoint &k : p.mPoints) {
        k.mPosition = {Tame(r), Tame(r), Tame(r)};
        k.mVelocity = {Tame(r), Tame(r), Tame(r)};
        k.mTime = t;
        const uint32_t pick = r() % 12;
        t += pick == 0 ? 0.0f : pick == 1 ? 5.0e-7f : pick == 2 ? 1.0e-6f : pick == 3 ? 2.0e-6f : Positive(r) / 2.0f;
        if (r() % 60 == 0) k.mTime = kNaN;
    }
    p.mFlags = r() % 4;
}

// A time to ask about: on a point, before the first, after the last, NaN, or between.
float RandomProbe(const cTSParticlePath &p, std::mt19937 &r)
{
    const float first = p.mPoints.front().mTime, last = p.mPoints.back().mTime;
    switch (r() % 8) {
    case 0: return p.mPoints[r() % p.mPoints.size()].mTime;
    case 1: return first - Positive(r) / 4.0f;
    case 2: return last + Positive(r) / 4.0f;
    case 3: return r() % 4 ? first : kNaN;
    case 4: return last;
    default: return first + (last - first) * float(r() % 1025) / 1024.0f;
    }
}

float RandomStep(std::mt19937 &r)
{
    switch (r() % 12) {
    case 0: return 0.0f;
    case 1: return r() % 3 ? -Positive(r) / 8.0f : kNaN;
    case 2: return 0.5f;
    case 3: return 1.5f;
    case 4: return 0.06666667f;
    case 5: return 0.13333334f;
    case 6: return 0.0666f;
    default: return Positive(r) / 16.0f;
    }
}

// The rig of the path's tests: a path, a local place, the path's last offsets and an age that puts the particle at a probe time
// after the step.
void SetupPathRig(Rig &g, std::mt19937 &r, float step)
{
    cTSParticlesDescription &d = *g.description;
    RandomPath(d.mPath, r);
    Effect &o = *g.object;
    RandomTransform(o.mLocal, r);
    if (r() % 3 == 0) o.mLocal.flags = 0;
    cTSParticle &p = g.particle;
    p.mPathPosition = {Tame(r), Tame(r), Tame(r)};
    p.mPathVelocity = {Tame(r), Tame(r), Tame(r)};
    p.mAge = fx::Sub(RandomProbe(d.mPath, r), step);
    switch (r() % 7) {
    case 0: p.mLife = fx::Add(step, p.mAge); break; // the age lands on the life
    case 1: p.mLife = p.mAge - 1.0f; break;
    case 2: p.mLife = kNaN; break;
    default: p.mLife = p.mAge + step + Positive(r); break;
    }
    if (r() % 5 == 0) p.mAge = kNaN;
}

void RunPathCase(difftest::Context &ctx, const char *name, uint32_t rva, int which)
{
    gCtx = &ctx;
    Overrides overrides;
    SetStepFakes();
    SetSurfaceFakes();
    NodeWorld nodes;
    gEv.reserve(1 << 20);
    shape::StandIn alloc(0x1be3b0, &RZMemory::Malloc, &ArenaMalloc);
    for (uint32_t seed = 0; seed < 4000; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            char mark[64];
            std::snprintf(mark, sizeof(mark), "%s seed %u mode %d", name, seed, mode);
            ctx.Mark(mark);
            std::unique_ptr<Precision> precision;
            if (seed % 2) precision = std::make_unique<Precision>();
            EffectsWorld w;
            gEv.clear();
            ResetWorld(seed, w);
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 4099u + 29);
            Rig g;
            BuildRig(g, mode, seed, r, w, seed % 3 != 0);
            cTSParticlesDescription &d = *g.description;
            const float step = RandomStep(r);
            SetupPathRig(g, r, step);
            g.object->mGravity = r() % 3 ? fx::Vector3{Tame(r), Tame(r), Tame(r)} : fx::Vector3{0.0f, 0.0f, 0.0f};
            if (which == 0) { // the dispatcher: the flags choose, the drag decides the plain ones
                d.mFlags = (d.mFlags & 0xffffffffull) | (uint64_t(r() % 16) << 32);
                // the low flag bits too: the ones that pick the substeps, and the ones that do not
                for (uint32_t bit : {0x40u, 0x80u, 0x4000u, 0x10000u, 0x40000u, 0x04000000u, 0x08000000u, 0x00100000u})
                    if (r() % 3 == 0) d.mFlags ^= bit;
                const uint32_t pick = r() % 4;
                d.mDrag = pick == 0 ? 0.0f : pick == 1 ? Positive(r) * 4.0f : pick == 2 ? 0.1f : Tame(r) / 4.0f; // 0.1 with the step 0.5 is the limit exactly
            } else if (which == 1) { // the plain step: drag of every kind
                d.mDrag = r() % 3 ? Tame(r) / 4.0f : 0.0f;
            }
            Effect *const o = g.object;
            cTSParticle *const p = &g.particle;
            bool result = true;
            if (which == 8) {
                if (mode) o->SteerAlongPath(p, step);
                else ctx.Original<SteerFn>(rva)(o, p, step);
            } else {
                StepFn original = ctx.Original<StepFn>(rva);
                switch (which) {
                case 0: result = mode ? o->IntegrateParticle(p, step) : original(o, p, step); break;
                case 1: result = mode ? o->IntegrateSimple(p, step) : original(o, p, step); break;
                case 2: result = mode ? o->IntegrateSubsteps(p, step) : original(o, p, step); break;
                case 3: result = mode ? o->IntegrateSteeredSubsteps(p, step) : original(o, p, step); break;
                case 4: result = mode ? o->IntegrateSteered(p, step) : original(o, p, step); break;
                case 5: result = mode ? o->IntegrateOnPath(p, step) : original(o, p, step); break;
                default: result = mode ? o->IntegrateOnPathPlaced(p, step) : original(o, p, step); break;
                }
            }
            b.push_back(result);
            EndRig(g, b);
        }
        Same(ctx, blobs[0], blobs[1], name, seed);
    }
}

} // namespace

DIFF_TEST(Particles_PathEvaluate, kPathEvaluate)
{
    for (uint32_t seed = 0; seed < 8000; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            char mark[64];
            std::snprintf(mark, sizeof(mark), "path seed %u mode %d", seed, mode);
            ctx.Mark(mark);
            std::unique_ptr<Precision> precision;
            if (seed % 2) precision = std::make_unique<Precision>();
            std::mt19937 r(seed * 6203u + 5);
            cTSParticlePath path;
            RandomPath(path, r);
            const float t = RandomProbe(path, r);
            cTSParticlePathPoint out;
            std::memset(&out, 0x5a, sizeof(out));
            const bool ok = mode ? path.Evaluate(t, &out) : ctx.Original<PathFn>(kPathEvaluate)(&path, t, &out);
            Blob &b = blobs[mode];
            b.push_back(ok);
            b.insert(b.end(), reinterpret_cast<const uint32_t *>(&out), reinterpret_cast<const uint32_t *>(&out + 1));
        }
        Same(ctx, blobs[0], blobs[1], "Particles_PathEvaluate", seed);
    }
}

DIFF_TEST(Particles_Integrate, kIntegrate) { RunPathCase(ctx, "Particles_Integrate", kIntegrate, 0); }
DIFF_TEST(Particles_IntegrateSimple, kSimple) { RunPathCase(ctx, "Particles_IntegrateSimple", kSimple, 1); }
DIFF_TEST(Particles_IntegrateSubsteps, kSubsteps) { RunPathCase(ctx, "Particles_IntegrateSubsteps", kSubsteps, 2); }
DIFF_TEST(Particles_IntegrateSteeredSubsteps, kSteeredSubsteps)
{
    RunPathCase(ctx, "Particles_IntegrateSteeredSubsteps", kSteeredSubsteps, 3);
}
DIFF_TEST(Particles_IntegrateSteered, kSteered) { RunPathCase(ctx, "Particles_IntegrateSteered", kSteered, 4); }
DIFF_TEST(Particles_IntegrateOnPath, kOnPath) { RunPathCase(ctx, "Particles_IntegrateOnPath", kOnPath, 5); }
DIFF_TEST(Particles_IntegrateOnPathPlaced, kOnPathPlaced) { RunPathCase(ctx, "Particles_IntegrateOnPathPlaced", kOnPathPlaced, 6); }
DIFF_TEST(Particles_Steer, kSteer) { RunPathCase(ctx, "Particles_Steer", kSteer, 8); }

// ParticlePosition 0xbb9ea0 (cdecl): the position turned about the velocity by the description's screw.
DIFF_TEST(Particles_Position, 0xbb9ea0)
{
    using PositionFn = void(__cdecl *)(fx::Vector3 *, const cTSParticle *, const cTSParticlesDescription *, float);
    const PositionFn original = ctx.Original<PositionFn>(0xbb9ea0);
    for (uint32_t seed = 0; seed < 6000; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            char mark[64];
            std::snprintf(mark, sizeof(mark), "position seed %u mode %d", seed, mode);
            ctx.Mark(mark);
            std::unique_ptr<Precision> precision;
            if (seed % 2) precision = std::make_unique<Precision>();
            std::mt19937 r(seed * 7919u + 5);
            auto description = std::make_unique<cTSParticlesDescription>();
            cTSParticlesDescription &d = *description;
            const bool tame = seed % 3 != 0;
            RandomCurve(d.mScrewRadius, r, tame);
            RandomCurve(d.mScrewAngle, r, tame);
            d.mScrewRadiusToSpeed = uint8_t(r() % 4);
            d.mScrewAngleToSpeed = uint8_t(r() % 4);
            d.mScrewRadiusMax = r() % 4 == 0 ? Positive(r) / 8.0f : r() % 8 == 0 ? kNaN : Positive(r) * 4.0f;
            cTSParticle p;
            std::memset(&p, 0, sizeof(p));
            const auto value = [&] { return tame ? Tame(r) : Wild(r); };
            p.mPosition = {value(), value(), value()};
            switch (r() % 6) {
            case 0: p.mVelocity = {0.0f, 0.0f, value()}; break;       // straight along z: no side
            case 1: p.mVelocity = {0.0f, 0.0f, 0.0f}; break;          // still
            case 2: p.mVelocity = {1.0e-4f, 0.0f, 0.0f}; break;       // near the speed limit
            case 3: p.mVelocity = {-0.0f, -0.0f, value()}; break;
            default: p.mVelocity = {value(), value(), value()}; break;
            }
            p.mScrewRadius = r() % 6 == 0 ? 0.0f : r() % 8 == 0 ? kNaN : Positive(r);
            p.mScrewAngle = r() % 6 == 0 ? 0.0f : Positive(r);
            const float t = r() % 10 == 0 ? kNaN : Tame(r) / 8.0f;
            Blob &b = blobs[mode];
            fx::Vector3 out = {std::bit_cast<float>(0x7fa5a5a5u), 1.0f, 2.0f};
            if (mode) fx::ParticlePosition(&out, &p, &d, t);
            else original(&out, &p, &d, t);
            PushVector(b, out);
        }
        Same(ctx, blobs[0], blobs[1], "Particles_Position", seed);
    }
}

// ---- InitParticle 0xbbb390 ----

namespace {

constexpr uint32_t kInit = 0xbbb390;
using InitFn = void(__thiscall *)(Effect *, cTSParticle *);

// The scene object's fake: slot 140 (vtable +0x230) is a thiscall (out, 0) that fills `out` with the view's transform (0x1c bytes,
// measured by FillOut from the callers' frames; position then rotation quaternion) and returns a pointer to the transform it
// answers with (the original reads the answer, never `out`). The fake answers with a script's transform and leaves `out` full of
// the 0xa5 pattern, so a replacement that read `out` would diverge.
void *gInitSceneVT[141];
FakeNode gInitScene;
float gInitView[7];
const float *__fastcall SceneViewFake(FakeNode *n, void *, float *out, int zero)
{
    Ev({evScene | 140, n->id, uint32_t(zero)});
    difftest::FillOut(out, 0x1c);
    for (float v : gInitView) gEv.push_back(Bits(v));
    return gInitView;
}
// The constrain object's slot 3 is a thiscall (local, world, position, offset) with ret 0x10: it may move the position in
// place. The fake logs the two transforms' words (byte +1 left out as the game's helper leaves it unset), the position and the
// offset, then moves the position.
void __fastcall InitConstrainFake(FakeNode *n, void *, const EffectsTransform *local, const EffectsTransform *world, float *position,
                                  const Vector3 *offset)
{
    Ev({evConstraint | 3, n->id});
    PushPlace(local);
    PushPlace(world);
    Ev({Bits(position[0]), Bits(position[1]), Bits(position[2]), Bits(offset->x), Bits(offset->y), Bits(offset->z)});
    position[0] = fx::Add(position[0], 0.25f);
}

struct InitCover {
    uint32_t ring = 0, ellipsoid = 0, box = 0, fromPosition = 0, tiny = 0, bomb = 0, bombNan = 0, bombCentre = 0, path = 0,
             placed = 0, unplaced = 0, axis = 0, stretch = 0, stretchNoScene = 0, constrain = 0, counts[4] = {}, rangeRotation = 0,
             flatRotation = 0, tinyEdge = 0, onCentre = 0;
};

} // namespace

DIFF_TEST(Particles_Init, kInit)
{
    gCtx = &ctx;
    Overrides overrides;
    SetStepFakes();
    NodeWorld nodes;
    gEv.reserve(1 << 20);
    shape::StandIn alloc(0x1be3b0, &RZMemory::Malloc, &ArenaMalloc);
    std::memcpy(gInitSceneVT, gFakeNodeVT, sizeof(gFakeNodeVT));
    gInitSceneVT[140] = reinterpret_cast<void *>(&SceneViewFake);
    Overrides::Set(gConstraintVT, 3, &InitConstrainFake);
    const InitFn original = ctx.Original<InitFn>(kInit);
    InitCover cover;
    for (uint32_t seed = 0; seed < 6000; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            char mark[64];
            std::snprintf(mark, sizeof(mark), "init seed %u mode %d", seed, mode);
            ctx.Mark(mark);
            std::unique_ptr<Precision> precision;
            if (seed % 2) precision = std::make_unique<Precision>();
            EffectsWorld w;
            gEv.clear();
            ResetWorld(seed, w);
            gInitScene = FakeNode{gInitSceneVT, 0x45, 0, nullptr};
            Blob &b = blobs[mode];
            std::mt19937 r(seed * 7919u + 23);
            const bool count = mode == 1;
            auto description = std::make_unique<cTSParticlesDescription>();
            cTSParticlesDescription &d = *description;
            RandomDescription(d, r, true, 0);
            const auto tame3 = [&] { return fxdesc::Vector3{Tame(r), Tame(r), Tame(r)}; };
            const auto vary = [&]() -> float { return r() % 5 == 0 ? 0.0f : r() % 7 == 0 ? Positive(r) * 4.0f : Positive(r); };
            uint32_t low = 0, high = 0;
            if (r() % 3 == 0) low |= 0x40;
            if (r() % 3 == 0) low |= 0x100;
            if (r() % 3 == 0) low |= 0x20;
            if (r() % 3 == 0) high |= 0x8;
            if (r() % 3 == 0) high |= 0x20;
            d.mFlags = uint64_t(high) << 32 | low;
            switch (r() % 4) {
            case 0: d.mSourceRing = Positive(r); break;
            case 1: d.mSourceRing = 0.0f; break;
            case 2: d.mSourceRing = -0.0f; break;
            default: d.mSourceRing = -Positive(r); break;
            }
            d.m38 = tame3();
            d.m44 = tame3();
            if (r() % 6 == 0) d.m38 = {0.0f, 0.0f, 0.0f}; // the tiny-length velocity (a zero box, unless a flag takes the position)
            if (r() % 6 == 0) d.m44 = d.m38;
            d.mEmitVolume = {r() % 6 == 0 ? 0.0f : Tame(r), Tame(r)};
            switch (r() % 5) {
            case 0: d.mBomb = 0.0f; break;
            case 1: d.mBomb = -0.0f; break;
            case 2: d.mBomb = std::bit_cast<float>(0x7fc00000u | (r() & 0x3fffffu)); break;
            default: d.mBomb = Tame(r); break;
            }
            d.mBombCentre = r() % 6 == 0 ? fxdesc::Vector3{0.0f, 0.0f, 0.0f} : tame3();
            d.mSizeVary = vary();
            d.mAlphaVary = vary();
            d.mAspectVary = vary();
            d.mScrewRadiusVary = vary();
            d.mScrewAngleVary = vary();
            d.mColorVary = {vary(), vary(), vary()};
            d.mPivotVary = {vary(), vary(), vary()};
            d.mStretch = r() % 5 == 0 ? 0.0f : r() % 5 == 0 ? Tame(r) : Positive(r);
            d.mConstrainOffset = tame3();
            d.mRotateCount = uint8_t(r() % 4);
            for (auto &pair : d.mRotateVary) {
                switch (r() % 4) {
                case 0: pair = {Tame(r), Tame(r)}; break;                    // a range, or a reversed one
                case 1: pair = {Tame(r), 0.0f}; pair.y = pair.x; break;      // equal bounds
                default: pair = {Tame(r), 0.0f}; pair.y = pair.x + Positive(r); break;
                }
            }
            d.mRotateOffset[0] = Tame(r);
            d.mRotateOffset[1] = Tame(r);
            d.mPath.mPoints.clear();
            if (r() % 3 == 0) {
                const unsigned points = 1 + r() % 3;
                for (unsigned i = 0; i < points; ++i) {
                    cTSParticlePathPoint pt{};
                    pt.mPosition = tame3();
                    pt.mVelocity = tame3();
                    pt.mTime = i == 0 && r() % 2 ? 0.0f : Positive(r);
                    d.mPath.mPoints.push_back(pt);
                }
            }
            gDescriptions[0] = description.get();
            void *const e = MakeEffect(mode, description.get());
            Effect &o = *static_cast<Effect *>(static_cast<cITSComponentEffect *>(e));
            o.mOwner.Assign(reinterpret_cast<cIGZUnknown *>(&w.owners[1]));
            o.mSpeedScale = r() % 6 == 0 ? 1.0f : Tame(r);
            o.mFirstFrame = r() % 0x400;
            o.mSpawnSize = r() % 6 == 0 ? 1.0f : Tame(r);
            o.mSource.mMin = {Tame(r), Tame(r), Tame(r)};
            o.mSource.mMax = {Tame(r), Tame(r), Tame(r)};
            if (r() % 6 == 0) o.mSource.mMax = o.mSource.mMin;
            if (r() % 8 == 0) o.mSource.mMin.x = std::bit_cast<float>(0x7fc00000u);
            // The comparisons that only a value on their edge tells apart, built on purpose: a velocity box whose squared length is
            // exactly the tiny threshold (0x2f03126f squared, rounded to a float, is the constant), and a particle placed exactly
            // on the bomb's centre (a zero distance).
            bool tinyEdge = false, onCentre = false;
            if (r() % 10 == 0) {
                d.mPath.mPoints.clear();
                d.mFlags &= ~uint64_t(0x40);
                d.m38 = d.m44 = {std::bit_cast<float>(0x2f03126fu), 0.0f, 0.0f};
                tinyEdge = true;
            } else if (r() % 10 == 0) {
                d.mPath.mPoints.clear();
                d.mFlags &= ~uint64_t(0x100);
                d.mSourceRing = -1.0f;
                if (d.mBomb == 0.0f || d.mBomb != d.mBomb) d.mBomb = 1.5f;
                o.mSource.mMin = o.mSource.mMax = {d.mBombCentre.x, d.mBombCentre.y, d.mBombCentre.z};
                onCentre = true;
            }
            RandomTransform(o.mLocal, r);
            RandomTransform(o.mWorld, r);
            if (r() % 3 == 0) { o.mLocal.flags = 0; o.mLocal.scale = 1.0f; }
            if (count) {
                cover.tinyEdge += tinyEdge;
                cover.onCentre += onCentre;
            }
            for (float &v : gInitView) v = Tame(r) / 8.0f;
            if (r() % 4) { // a rotation about a random axis
                const float len = std::sqrt(gInitView[3] * gInitView[3] + gInitView[4] * gInitView[4] + gInitView[5] * gInitView[5] +
                                            gInitView[6] * gInitView[6]);
                if (len > 1e-3f)
                    for (int k = 3; k < 7; ++k) gInitView[k] /= len;
                else
                    gInitView[6] = 1.0f;
            }
            const bool scene = r() % 4 != 0;
            const bool constrain = r() % 3 == 0;
            if (scene) o.mScene.Assign(reinterpret_cast<cIGZUnknown *>(&gInitScene));
            if (constrain) o.mConstrain.Assign(reinterpret_cast<cIGZUnknown *>(&gConstraint));
            SeedRandom(seed);
            cTSParticle p;
            std::memset(&p, 0x5a, sizeof(p));
            if (count) {
                const bool hasPath = !d.mPath.mPoints.empty();
                const bool isPlaced = hasPath && (high & 8) && d.mPath.mPoints[0].mTime == 0.0f;
                cover.path += hasPath;
                cover.placed += isPlaced;
                cover.unplaced += !isPlaced;
                if (!isPlaced) {
                    cover.ring += d.mSourceRing >= 0.0f;
                    cover.ellipsoid += !(d.mSourceRing >= 0.0f) && (low & 0x100);
                    cover.box += !(d.mSourceRing >= 0.0f) && !(low & 0x100);
                    if (!hasPath) {
                        cover.fromPosition += (low & 0x40) != 0;
                        cover.bomb += !(d.mBomb == 0.0f);
                        cover.bombNan += d.mBomb != d.mBomb;
                        cover.bombCentre += d.mBombCentre.x == 0.0f && d.mBombCentre.y == 0.0f && d.mBombCentre.z == 0.0f;
                        cover.tiny += d.m38.x == 0.0f && d.m38.y == 0.0f && d.m38.z == 0.0f && d.m44.x == 0.0f && !(low & 0x40);
                    }
                }
                cover.axis += (high & 0x20) != 0;
                cover.stretch += (low & 0x20) && scene;
                cover.stretchNoScene += (low & 0x20) && !scene;
                cover.constrain += constrain;
                ++cover.counts[d.mRotateCount];
                cover.rangeRotation += d.mRotateVary[0].y > d.mRotateVary[0].x;
                cover.flatRotation += !(d.mRotateVary[0].y > d.mRotateVary[0].x);
            }
            if (mode) o.InitParticle(&p);
            else original(&o, &p);
            b.insert(b.end(), reinterpret_cast<const uint32_t *>(&p), reinterpret_cast<const uint32_t *>(&p + 1));
            PushRandom(b);
            Drain(b);
            o.mOwner.Reset();
            o.mScene.Reset();
            o.mConstrain.Reset();
            o.mRefCount = 1;
            b.push_back(CallSlot<uint32_t>(e, 13));
            b.push_back(gInitScene.refs);
            b.push_back(gConstraint.refs);
            Drain(b);
            ctx.Mark("init done");
        }
        Same(ctx, blobs[0], blobs[1], "Particles_Init", seed);
    }
    // A test counts what it exercised: a branch the seeds never reached fails the test (an unreached branch proves nothing).
    const bool all = cover.ring && cover.ellipsoid && cover.box && cover.fromPosition && cover.tiny && cover.bomb && cover.bombNan &&
                     cover.bombCentre && cover.path && cover.placed && cover.unplaced && cover.axis && cover.stretch &&
                     cover.stretchNoScene && cover.constrain && cover.counts[0] && cover.counts[1] && cover.counts[2] &&
                     cover.counts[3] && cover.rangeRotation && cover.flatRotation && cover.tinyEdge && cover.onCentre;
    Blob reached = {all ? 1u : 0u}, expected = {1u};
    Same(ctx, reached, expected, "Particles_Init coverage", 0);
}
