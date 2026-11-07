// Differential test of cTSParticlesRenderable::UpdateParticleChildren 0xbc1d10 (lane XB, src/ts/cTSParticlesChildren.cpp).
// The renderable is a shell of its bytes (the two slots the function calls, the effect pointer, the shader parameters), the
// effect a block of its bytes, the description real, the children tests/ts_child_nodes.h. Both sides run on the same objects;
// what they did to the children (order, arguments, floats by bits), the slots they called on the renderable and the drawn count
// are compared, at both x87 precisions. Each orientation kind has its own turn of the cases, the ones with no case included.
// RVAs these tests reach: 0xbc1d10, 0x1a6db0, 0x1a6c40, 0x1a7020, 0x1a7570, 0x19c080, 0x19cd80, 0xbb5620, the CRT's sin, cos, sqrt.

#include "ts_particles_world.h"

#include "ts_child_nodes.h"
#include "ts/ParticleCurve.h"

#include <bit>
#include <cmath>
#include <list>
#include <vector>

namespace {

using childnodes::ChildNode;
using childnodes::ChildNodes;

constexpr uint8_t kModes[] = {0, 2, 3, 4, 7, 8, 9, 1, 5, 6, 10, 255};
constexpr uint32_t kWorldFloats = 7;

alignas(16) uint8_t gShellBytes[Node::kSize];
alignas(16) uint8_t gEffectBytes[sizeof(Effect)];
void *gShellVT[92];
float gShellWorld[kWorldFloats];
uint32_t gRange[3];

void __fastcall ShellWorld(void *, void *, float *out)
{
    Ev({evTree | 54});
    std::memcpy(out, gShellWorld, sizeof(gShellWorld));
}
uint32_t *__fastcall ShellChildren(void *, void *, uint32_t *out)
{
    Ev({evTree | 42});
    std::memcpy(out, gRange, sizeof(gRange));
    return out;
}

// A component on either side of the near-zero test, and on it.
float EdgeOfNearZero(std::mt19937 &r)
{
    static const float kNear[] = {0.0f, 1e-6f, 0x1.0c6f7ap-20f, 0x1.0c6f7cp-20f, 0x1.0c6f7ep-20f, 0x1.0c6f70p-20f, 0x1p-21f};
    const float v = kNear[r() % std::size(kNear)];
    return r() % 2 ? v : -v;
}
float EdgeOfRoot(std::mt19937 &r)
{
    // Around the root of 1e-6.
    const float v = 0.001f;
    switch (r() % 4) {
    case 0: return v;
    case 1: return std::nextafter(v, 1.0f);
    case 2: return std::nextafter(v, 0.0f);
    default: return r() % 2 ? 0.00099999f : 0.00100001f;
    }
}

// Pairs whose squares add, in float, to exactly the float nearest 1e-6: that float is under the double the function tests
// against, so these are the one place where a test on the float would differ.
constexpr float kFloatNearZero = 1e-6f;
struct OnEdge {
    float a, b;
};
float Summed(float a, float b)
{
    return a * a + b * b;
}
const std::vector<OnEdge> &PairsOnEdge()
{
    static const std::vector<OnEdge> pairs = [] {
        std::vector<OnEdge> found;
        float a = 0.0009f;
        for (int i = 0; i < 4000; ++i, a = std::nextafter(a, 1.0f)) {
            const double rest = 1e-6 - double(a) * a;
            if (rest <= 0.0) break;
            float b = std::sqrt(float(rest));
            for (int step = 0; step < 8; ++step) b = std::nextafter(b, 0.0f);
            for (int step = 0; step < 16; ++step, b = std::nextafter(b, 1.0f))
                if (Summed(a, b) == kFloatNearZero) found.push_back({a, b});
        }
        return found;
    }();
    return pairs;
}

Vector3 Direction(std::mt19937 &r, unsigned kind)
{
    switch (kind) {
    case 0: return {Tame(r), Tame(r), Tame(r)};
    case 1: return {0.0f, 0.0f, 0.0f};
    case 2: return {EdgeOfNearZero(r), EdgeOfNearZero(r), EdgeOfNearZero(r)};
    case 3: return {EdgeOfRoot(r), r() % 2 ? 0.0f : EdgeOfNearZero(r), r() % 2 ? EdgeOfRoot(r) : Tame(r)};
    case 4: return {0.0f, 0.0f, r() % 2 ? Tame(r) : EdgeOfRoot(r)};
    case 5: { // along, or against, an axis the turns start from
        const float s = r() % 2 ? Positive(r) : -Positive(r);
        return r() % 2 ? Vector3{s, 0.0f, 0.0f} : Vector3{0.0f, s, 0.0f};
    }
    case 6: { // the flat square, or only the whole one, exactly on the float
        const std::vector<OnEdge> &pairs = PairsOnEdge();
        const OnEdge e = pairs.empty() ? OnEdge{0.001f, 0.0f} : pairs[r() % pairs.size()];
        return r() % 2 ? Vector3{e.a, e.b, 0.0f} : Vector3{e.a, 0.0f, e.b};
    }
    default: return {Wild(r), Wild(r), Wild(r)};
    }
}

void Curve(cTSEffectCurve &curve, std::mt19937 &r)
{
    switch (r() % 6) {
    case 0:
        curve.mKind = cTSEffectCurve::kWave;
        curve.mValues = {Tame(r), Tame(r), Tame(r), Tame(r)};
        break;
    case 1:
        curve.mKind = 5; // neither kind: zero
        curve.mValues = {Tame(r)};
        break;
    default: RandomCurve(curve, r, r() % 4 != 0);
    }
}

fxdesc::Vector3 Axis(std::mt19937 &r)
{
    switch (r() % 6) {
    case 0: return {1.0f, 0.0f, 0.0f};
    case 1: return {0.0f, 0.0f, 0.0f};
    case 2: return {Wild(r), Wild(r), Wild(r)};
    default: return {Tame(r), Tame(r), Tame(r)};
    }
}

uint32_t Parameter(std::mt19937 &r)
{
    static const uint32_t kWords[] = {0xffffffffu, 0x80000000u, 0u, 1u, 2u, 3u, 7u, 0x7fffffffu};
    return kWords[r() % std::size(kWords)];
}
uint32_t ListLength(std::mt19937 &r)
{
    static const uint32_t kWords[] = {0u, 1u, 2u, 3u, 4u, 8u, 0x7fffffffu, 0x80000000u, 0x80000001u, 0xffffffffu};
    return kWords[r() % std::size(kWords)];
}

constexpr int kParameterList[7] = {2, 0, 0, 3, 3, 3, 3};

struct Reached {
    uint32_t transforms[std::size(kModes)] = {}, rotations[4] = {}, stopped = 0, flatShort = 0, fullShort = 0, flatOnEdge = 0, fullOnEdge = 0, listEnded = 0, rangeEnded = 0,
             sent[7] = {}, atLength = 0, negative = 0, colourOne = 0, colourOnKey = 0, colourBetween = 0, noCount = 0;
};

} // namespace

DIFF_TEST(Particles_Children, 0xbc1d10)
{
    gCtx = &ctx;
    using UpdateFn = void(__thiscall *)(void *, void *);
    auto orig = ctx.Original<UpdateFn>(0xbc1d10);
    auto children = std::make_unique<ChildNodes>();
    std::memcpy(gShellVT, gFakeNodeVT, sizeof(gShellVT));
    gShellVT[42] = reinterpret_cast<void *>(&ShellChildren);
    gShellVT[54] = reinterpret_cast<void *>(&ShellWorld);
    std::memset(gShellBytes, 0, sizeof(gShellBytes));
    std::memset(gEffectBytes, 0, sizeof(gEffectBytes));
    *reinterpret_cast<void ***>(gShellBytes) = gShellVT;
    auto *const shell = reinterpret_cast<Node *>(gShellBytes);
    auto *const effect = reinterpret_cast<Effect *>(gEffectBytes);
    shell->mEffect = effect;
    auto *const particles = new (&effect->mParticles) std::list<cTSParticle *>();
    auto description = std::make_unique<cTSParticlesDescription>();
    cTSParticlesDescription *const d = description.get();
    cTSParticle pool[8];
    Reached reached;
    gEv.reserve(1 << 16);

    constexpr uint32_t kCases = 120000;
    for (uint32_t seed = 0; seed < kCases; ++seed) {
        char mark[48];
        std::snprintf(mark, sizeof(mark), "children case %u", seed);
        ctx.Mark(mark);
        std::mt19937 r(seed * 2654435761u + 0xbc1d10);
        const unsigned modeIndex = seed % std::size(kModes);
        const uint8_t mode = kModes[modeIndex];
        const bool valid = modeIndex < 7;

        d->mAlignment = mode;
        d->mFlags = (uint64_t(r()) << 32 | r()) & ~(uint64_t(0x20) << 32);
        if (r() % 2) d->mFlags |= uint64_t(0x20) << 32;
        d->mRotateCount = uint8_t(r() % 4);
        for (int k = 0; k < 2; ++k) {
            d->mRotateAxis[k] = Axis(r);
            d->mRotateOffset[k] = r() % 8 ? Tame(r) : Wild(r);
            Curve(d->mRotateCurve[k], r);
        }
        Curve(d->mSize, r);
        Curve(d->mAlpha, r);
        Curve(d->mAspect, r);
        d->mFrameSpeed = Wild(r);
        const unsigned colourCount = r() % 3 ? 1u << (r() % 3) : 1 + r() % 5; // keys at powers of two land exactly
        d->mColors.clear();
        for (unsigned i = 0; i < colourCount; ++i) d->mColors.push_back({Wild(r), Wild(r), Wild(r)});

        for (uint32_t &p : shell->mParams) p = Parameter(r);
        for (float &f : gShellWorld) f = r() % 8 ? Tame(r) : Wild(r);
        for (auto &row : effect->mWorld.m)
            for (float &f : row) f = r() % 16 ? Tame(r) : Wild(r);
        if (r() % 2) { // a rotation about z, as a real effect's world
            const float a = Tame(r), c = std::cos(a), s = std::sin(a);
            const float m[3][3] = {{c, s, 0.0f}, {-s, c, 0.0f}, {0.0f, 0.0f, 1.0f}};
            std::memcpy(effect->mWorld.m, m, sizeof(m));
        }
        effect->mWorld.scale = r() % 4 ? Positive(r) : Wild(r);
        effect->mWorld.translation = {Tame(r), Tame(r), Tame(r)};
        effect->mEmitClock = r() % 8 ? double(Tame(r)) * 1.000000123 : double(Wild(r));
        effect->mSizeScale = r() % 4 ? Positive(r) : Wild(r);
        effect->mAlphaScale = r() % 4 ? Positive(r) : Wild(r);
        effect->mColorScale = {Wild(r), Wild(r), Wild(r)};
        effect->mFramesDrawn = uint16_t(r());

        const unsigned particleCount = r() % 8 ? 1 + r() % 5 : r() % 8;
        const unsigned childCount = r() % 3 ? particleCount : r() % 8;
        particles->clear();
        for (unsigned i = 0; i < particleCount; ++i) {
            cTSParticle &p = pool[i];
            std::memset(&p, 0x5a, sizeof(p));
            if (colourCount > 1 || r() % 2) { // the keys are read at t: an age past the life reads past them
                const unsigned steps = colourCount > 1 ? colourCount - 1 : 1;
                p.mLife = r() % 2 ? 1.0f : Positive(r);
                p.mAge = r() % 2 ? float(r() % (steps + 1)) / float(steps) * p.mLife : p.mLife * float(r() % 1025) / 1024.0f;
                if (p.mAge > p.mLife) p.mAge = p.mLife;
            } else {
                p.mLife = Wild(r);
                p.mAge = Wild(r);
            }
            const unsigned kind = r() % 8;
            p.mPosition = {Tame(r), Tame(r), Tame(r)};
            p.mVelocity = Direction(r, kind);
            p.mAxis = Direction(r, r() % 2 ? 0 : r() % 8);
            p.mSize = r() % 4 ? Positive(r) : Wild(r);
            p.mAspect = r() % 4 ? Positive(r) : Wild(r);
            p.mRotation = r() % 4 ? Tame(r) : Wild(r);
            p.mRotationSpeed = r() % 4 ? Tame(r) : Wild(r);
            p.mAlpha = r() % 4 ? Positive(r) : Wild(r);
            p.mColor = {Wild(r), Wild(r), Wild(r)};
            particles->push_back(&p);
        }

        children->Reset();
        std::vector<ChildNode *> order;
        for (unsigned i = 0; i < childCount; ++i) {
            uint32_t lists[4];
            for (uint32_t &l : lists) l = ListLength(r);
            order.push_back(children->Make(0x2000 + i, lists));
        }
        ChildNode *const end = r() % 4 == 0 ? children->Make(0x2fff) : nullptr; // a range may end on a node, not the null
        ChildNodes::Link(order, end);
        gRange[0] = shape::P(order.empty() ? end : order.front());
        gRange[1] = 0x5a5a5a5au; // the range's middle word is never read
        gRange[2] = shape::P(end);

        // What this case can reach, from the test's side.
        const unsigned walked = childCount < particleCount ? childCount : particleCount;
        if (childCount != 0) {
            if (particleCount < childCount) ++reached.listEnded;
            else ++reached.rangeEnded;
        }
        if (!valid && walked != 0) ++reached.noCount;
        if (valid && walked != 0) {
            if (mode == 0) ++reached.rotations[d->mRotateCount];
            for (unsigned i = 0; i < walked; ++i) {
                const cTSParticle &p = pool[i];
                const float t = p.mAge / p.mLife;
                if (mode != 0 && (d->mFlags >> 37 & 1) && std::fabs(p.mVelocity.x) < 1e-6 && std::fabs(p.mVelocity.y) < 1e-6 &&
                    std::fabs(p.mVelocity.z) < 1e-6)
                    ++reached.stopped;
                const double flat = double(p.mVelocity.x) * p.mVelocity.x + double(p.mVelocity.y) * p.mVelocity.y;
                if (flat > 0.0 && flat < 1.5e-6) ++reached.flatShort;
                if (flat + double(p.mVelocity.z) * p.mVelocity.z > 0.0 && flat + double(p.mVelocity.z) * p.mVelocity.z < 1.5e-6) ++reached.fullShort;
                const float flatSquare = Summed(p.mVelocity.x, p.mVelocity.y);
                if (flatSquare == kFloatNearZero) ++reached.flatOnEdge;
                if (flatSquare + p.mVelocity.z * p.mVelocity.z == kFloatNearZero) ++reached.fullOnEdge;
                if (colourCount == 1) ++reached.colourOne;
                else if (fx::FindKey(float(colourCount - 1) * t).mFraction > 0.0f) ++reached.colourBetween;
                else ++reached.colourOnKey;
                for (int e = 0; e < 7; ++e) {
                    const uint32_t word = shell->mParams[e], length = order[i]->lists[kParameterList[e]];
                    if (int32_t(word) < 0) ++reached.negative;
                    else if (word == length) ++reached.atLength;
                    else if (word < length) ++reached.sent[e];
                }
            }
        }

        Blob logs[2];
        std::unique_ptr<Precision> precision;
        if (seed & 2) precision = std::make_unique<Precision>();
        const uint16_t drawn = effect->mFramesDrawn;
        const uint32_t transformsBefore = childnodes::gTransformSets;
        for (int side = 0; side < 2; ++side) {
            gEv.clear();
            effect->mFramesDrawn = drawn;
            if (side == 0) orig(shell, d);
            else shell->UpdateParticleChildren(d);
            logs[side] = gEv;
            logs[side].push_back(effect->mFramesDrawn);
            logs[side].push_back(uint32_t(effect->mFramesDrawn - drawn));
        }
        reached.transforms[modeIndex] += (childnodes::gTransformSets - transformsBefore) / 2;
        ++ctx.cases;
        Same(ctx, logs[0], logs[1], "Particles_Children", seed);
    }
    particles->~list();

    for (size_t m = 0; m < 7; ++m)
        if (reached.transforms[m] < 2000) ctx.Fail("mode %u: only %u children placed", kModes[m], reached.transforms[m]);
    for (size_t m = 7; m < std::size(kModes); ++m)
        if (reached.transforms[m] != 0) ctx.Fail("mode %u placed %u children", kModes[m], reached.transforms[m]);
    for (int c = 0; c < 4; ++c)
        if (reached.rotations[c] < 500) ctx.Fail("rotation count %d: only %u cases", c, reached.rotations[c]);
    for (int e = 0; e < 7; ++e)
        if (reached.sent[e] < 2000) ctx.Fail("parameter %d: only %u sent", e, reached.sent[e]);
    for (int k = 0; k < 4; ++k)
        if (k != 1 && childnodes::gParameterSets[k] < 2000) ctx.Fail("list %d: only %u sets", k, childnodes::gParameterSets[k]);
    if (reached.stopped < 500 || reached.flatShort < 500 || reached.fullShort < 500 || reached.listEnded < 500 || reached.rangeEnded < 500 ||
        reached.atLength < 500 || reached.negative < 500 || reached.colourOne < 500 || reached.colourOnKey < 500 ||
        reached.colourBetween < 500 || reached.noCount < 500 || reached.flatOnEdge < 500 || reached.fullOnEdge < 500)
        ctx.Fail("cases ran dry: stopped %u flat %u full %u list %u range %u length %u negative %u colour %u %u %u no-count %u edge %u %u", reached.stopped,
                 reached.flatShort, reached.fullShort, reached.listEnded, reached.rangeEnded, reached.atLength, reached.negative,
                 reached.colourOne, reached.colourOnKey, reached.colourBetween, reached.noCount, reached.flatOnEdge, reached.fullOnEdge);
}
