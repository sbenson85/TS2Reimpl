// Differential tests for the particle effect descriptions (src/ts/cTSParticlesDescription.*, lane IX) and the curve's
// scaling, destructor and assignment (src/ts/cTSEffectCurve.cpp, lane IX's part).
//
// Each test runs the same random operations twice: mode 0 the originals at their RVAs (the image's own callees), mode 1 ours
// (the hooks' entries, our members); the component test adds mode 2 (ours through our vtables). After each operation the
// state is compared word for word: every field (padding left out, and the vtable words), strings, vectors (size, capacity,
// elements), curves (kind, count, capacity, values), the fake streams' call logs and the stand-ins' logs; the destructors'
// frees are compared at the end.
//
// Tapes: built field by field by the version's layout. Reads never fail where the original's temporary or field is unset (a
// failing read would leave memory both sides hold differently): the colour / pivot / warp / collider plane floats (new
// elements unset), the first flags word and counts read over unset stack words (vectors, the meta's colours, the attractor
// and path counts, the colour lists' counts), the old attractor's centre. The counts read over the shared temporary may fail
// where its last value is a small count (the particles' collider count after the warp count; the meta's warp and collider
// counts): the tape then lays out that many elements. Tapes are never cut short.
//
// Covered RVAs (verified): the parts: colliders 0xbe1530 (constructor), 0xbe1aa0 (destructor), 0xbe1490 (copy), 0xbde880 /
// 0xbde940 (serializers); attractors 0xbddd10, 0xbe1920, 0xbe13d0, 0xbde600 (SetCentre), 0xbde620 (SetCurve), 0xbde550 /
// 0xbde750 (serializers), 0xbddd80 / 0xbdde60 (their cdecl entries), 0xbddd90 / 0xbdde70 (the lists); warps 0xbfa580 /
// 0xbfa660; path points 0xbfa550 / 0xbfa620; paths 0xbfb430 / 0xbfb510 and their cdecl entries 0xbfa5b0 / 0xbfa6a0; walks
// 0xbfa500 / 0xbfa5c0; colour lists 0xbe02f0; strings 0xbde7b0. The base 0xbfb5a0 (constructor), 0xbfb990 (destructor),
// 0xbe1df0 (assignment). Particles 0xbe15d0, 0xbe1be0, 0xbe2320 (move), 0xbe2700 (copy), 0xbe72e0 / 0xbe7db0; meta
// particles 0xbecf80, 0xbed140, 0xbed200 (copy), 0xbf1100 / 0xbf1c80. The components: particles 0xbe7ba0 (factory),
// 0xbe58b0 (CreateEffect), 0xbe2be0 (CopyTo), 0xbe72a0 / 0xbe7d80 (Read / Write), 0xbe58c0 (GetSize), 0xbe2b20 (deleting
// destructor) and the +4 face's thunk 0xbe2b10; meta 0xbf1a70, 0xbefb70, 0xbed420, 0xbf10c0, 0xbf1c50, 0xbefb80, 0xbed330
// and the thunk 0xbed31a. The curve 0xbdc950 (Scale), 0xbdc9f0 (Divide), 0xbe1b80 (destructor), 0xbe21f0 (assignment).
// Stood in on both sides: the effect makers 0xbc0c50 (cTSParticlesEffect::Create, ours and the image's) / 0xc03910 (cTSMetaParticlesEffect::Create, the same) and atexit 0x824cbe (the string buffer's).
#include "ts_effects_collection.h"

#include "code_patch.h"
#include "free_recorder.h"
#include "rz/cRZString.h"
#include "ts/cTSParticlesDescription.h"
#include "ts/cTSMetaParticlesEffect.h"
#include "ts/cTSParticlesEffect.h"

#include <cmath>
#include <cstddef>
#include <new>

namespace {

using namespace fxtest;
using fxdesc::Color;
using fxdesc::Vector3;

enum : uint32_t {
    evGet = 0xd7000000,
    evPut = 0xd8000000,
    evCreateEffect = 0xd9000000,
    evTrap = 0xda000000,
};

const uint8_t *gObj = nullptr; // the object under test (pointers into it are logged as offsets)
size_t gObjSize = 0;
uint32_t Off(const void *p)
{
    const auto *q = static_cast<const uint8_t *>(p);
    if (gObj && q >= gObj && q < gObj + gObjSize) return 0x0f000000u | uint32_t(q - gObj);
    return p ? 0x0c000000u : 0;
}

// ---- streams (as tests/ts_comp_description.cpp's) ----

struct InStream {
    void *const *vt;
    std::vector<uint32_t> words;
    std::vector<uint8_t> fails;
    size_t pos = 0;
};
template <int Slot, int Bytes> bool __fastcall InGet(InStream *s, void *, void *out)
{
    const bool have = s->pos < s->words.size();
    const bool ok = have && !s->fails[s->pos];
    const uint32_t w = have ? s->words[s->pos] : 0;
    if (have) ++s->pos;
    Ev({evGet | uint32_t(Slot), uint32_t(ok), w});
    if (ok) std::memcpy(out, &w, Bytes);
    return ok;
}
void EvText(const char *text)
{
    const size_t n = std::strlen(text);
    Ev({uint32_t(n)});
    for (size_t i = 0; i < n; i += 4) {
        uint32_t w = 0;
        std::memcpy(&w, text + i, n - i < 4 ? n - i : 4);
        Ev({w});
    }
}
// Slot 15: the next word made into a text; a failing read writes nothing, except a run's first (the read buffers, the image's
// and ours, then always get a text, so what a later failing read leaves is the run's own).
bool gTextWritten = false;
bool __fastcall InGetText(InStream *s, void *, cRZString *out)
{
    const bool have = s->pos < s->words.size();
    const bool ok = (have && !s->fails[s->pos]) || !gTextWritten;
    gTextWritten = true;
    const uint32_t w = have ? s->words[s->pos] : 0;
    if (have) ++s->pos;
    Ev({evGet | 15u, uint32_t(ok), w});
    if (!ok) return false;
    const std::string text = w % 3 == 0 ? std::string(16 + w % 24, char('a' + w % 26)) : std::to_string(w);
    using FromChar = void(__thiscall *)(void *, const char *, uint32_t);
    reinterpret_cast<const FromChar *>(out->mVTable)[3](out, text.data(), uint32_t(text.size()));
    return true;
}
struct OutStream {
    void *const *vt;
};
template <int Slot, uint32_t Mask> bool __fastcall OutPut(OutStream *, void *, uint32_t v)
{
    Ev({evPut | uint32_t(Slot), v & Mask});
    return true;
}
bool __fastcall OutPutText(OutStream *, void *, const char *text)
{
    Ev({evPut | 14u});
    EvText(text);
    return true;
}
void __fastcall StreamTrap(void *, void *)
{
    Ev({evTrap});
}
struct StreamVTables {
    void *in[21];
    void *out[21];
    StreamVTables()
    {
        for (void *&p : in) p = reinterpret_cast<void *>(&StreamTrap);
        for (void *&p : out) p = reinterpret_cast<void *>(&StreamTrap);
        in[4] = reinterpret_cast<void *>(&InGet<4, 1>);
        in[5] = reinterpret_cast<void *>(&InGet<5, 1>);
        in[6] = reinterpret_cast<void *>(&InGet<6, 2>);
        in[7] = reinterpret_cast<void *>(&InGet<7, 2>);
        in[9] = reinterpret_cast<void *>(&InGet<9, 4>);
        in[12] = reinterpret_cast<void *>(&InGet<12, 4>);
        in[15] = reinterpret_cast<void *>(&InGetText);
        out[4] = reinterpret_cast<void *>(&OutPut<4, 0xffu>);
        out[5] = reinterpret_cast<void *>(&OutPut<5, 0xffu>);
        out[6] = reinterpret_cast<void *>(&OutPut<6, 0xffffu>);
        out[7] = reinterpret_cast<void *>(&OutPut<7, 0xffffu>);
        out[9] = reinterpret_cast<void *>(&OutPut<9, 0xffffffffu>);
        out[12] = reinterpret_cast<void *>(&OutPut<12, 0xffffffffu>);
        out[14] = reinterpret_cast<void *>(&OutPutText);
    }
};
const StreamVTables kStreams;

// ---- tapes ----

struct Tape {
    std::mt19937 &r;
    InStream s{kStreams.in};

    // Returns whether the read will fail.
    bool Word(uint32_t w, bool mayFail)
    {
        s.words.push_back(w);
        s.fails.push_back(uint8_t(mayFail && r() % 12 == 0));
        return s.fails.back() != 0;
    }
    uint32_t RandomFloat()
    {
        const uint32_t k = r() % 16;
        float f = k == 0 ? 0.0f : k == 1 ? -0.0f : k == 2 ? 1.0f : float(int(r() % 2000) - 1000) / 8.0f;
        uint32_t w;
        std::memcpy(&w, &f, 4);
        return w;
    }
    void W(bool mayFail = true) { Word(r() % 2 ? r() % 8 : r(), mayFail); }
    void F(bool mayFail = true) { Word(RandomFloat(), mayFail); }
    void B(bool mayFail = true) { Word(r() % 3 ? r() % 4 : r(), mayFail); }
    void T() { Word(r() % 40, true); }
    void V2(bool mayFail = true) { F(mayFail), F(mayFail); }
    void V3(bool mayFail = true) { F(mayFail), F(mayFail), F(mayFail); }
    // A curve: its kind and count word (never failing: the original reads it over the stream argument's slot), then floats.
    void Curve()
    {
        const uint32_t kinds[] = {0u, 0u, 0x81000000u, 0x81000000u, 0x05000000u};
        const uint32_t kind = kinds[r() % 5];
        const uint32_t count = r() % 7;
        Word(kind | count, false);
        const uint32_t floats = kind == 0 ? count : kind == 0x81000000u ? (count < 4 ? count : 4) : 0;
        for (uint32_t i = 0; i < floats; ++i) F();
    }
    // A count 0..max; `fallback` (what a failing read leaves) or ~0 for never failing. Returns the count the reader uses.
    uint32_t Count(uint32_t max, uint32_t fallback = ~0u)
    {
        const uint32_t n = r() % (max + 1);
        const bool fails = Word(n, fallback != ~0u);
        return fails ? fallback : n;
    }
    void Triples() // a colour list: count and floats never failing
    {
        const uint32_t n = Count(4);
        for (uint32_t i = 0; i < n * 3; ++i) F(false);
    }
    void Warps(uint32_t n)
    {
        for (uint32_t i = 0; i < n; ++i) F(), V3(false), V3(false);
    }
    void Collider()
    {
        const uint32_t flags = r() % 2 ? r() % 4 : r();
        Word(flags, false);
        uint32_t extra = 0;
        if (flags & 2) {
            const uint32_t b = r() % 3 ? r() % 2 : r();
            if (!Word(b, true)) extra = b & 0xff;
        }
        T();
        V3(false), V3(false), V3(false);
        F(), F();
        if (extra) F();
        F(), F(), T(), T();
    }
    void Colliders(uint32_t n)
    {
        for (uint32_t i = 0; i < n; ++i) Collider();
    }
    void Attractor()
    {
        W(), B(), V3(), Curve(), F(), F();
    }
    void Attractors()
    {
        W();
        const uint32_t n = Count(3);
        for (uint32_t i = 0; i < n; ++i) Attractor();
    }
    void Points(uint32_t n)
    {
        for (uint32_t i = 0; i < n; ++i) V3(), V3(), F();
    }
    void Path()
    {
        W();
        Points(Count(3));
        W();
    }
    void Walk() { V2(), V2(), F(), F(), F(); }
    // The old attractor: the centre (never failing: unset), the curve, the strength (read over an argument's slot: the
    // particles' description pointer, the same stack block on both sides in the description tests, but not the components'
    // heap objects; the meta's 0).
    void OldAttractor() { V3(false), Curve(), F(gOldStrengthMayFail); }
    static inline bool gOldStrengthMayFail = true;
};

uint32_t RandomVersion(std::mt19937 &r, uint32_t newest)
{
    return r() % 3 ? newest : 1 + r() % newest;
}

// The particle description's layout by version (0xbe72e0).
InStream ParticlesTape(std::mt19937 &r, uint32_t v)
{
    Tape t{r};
    if (v < 5)
        t.W(false); // over an unset temporary
    else
        t.W(false), t.W();
    t.V2(), t.F(), t.V2(), t.V2(), t.V3(), t.V3(), t.V2(), t.V3(), t.V3();
    if (v > 2) t.F();
    t.Curve(), t.F(), t.W(), t.F(), t.Curve(), t.F(), t.Curve(), t.F(), t.V3(), t.V3(), t.B();
    if (v < 6)
        t.F(v == 5), t.F(); // the temporary: unset before version 5, then the flags' last word
    else
        t.F(), t.F(), t.F(), t.F();
    t.F(), t.F(), t.Curve(), t.Curve();
    if (v > 4) t.Triples(), t.V3();
    t.Curve(), t.F(), t.Triples(), t.V3(), t.T(), t.B(), t.B(), t.B(), t.B(), t.F(), t.F(), t.B(), t.B();
    t.V3(), t.F(), t.F(), t.V3(), t.F(), t.F(), t.F();
    const uint32_t warps = t.Count(4); // over the flags or a rotate value: never failing
    t.Warps(warps);
    t.B(), t.B(), t.B(), t.B();
    t.Colliders(t.Count(3, warps));
    t.T();
    if (v > 10) t.V3();
    t.F(), t.F(), t.F(), t.F(), t.F(), t.F(), t.F(), t.F(), t.V2(), t.V2(), t.V2(), t.F(), t.F();
    if (v < 8) {
        t.OldAttractor();
        if (v >= 7)
            t.Path();
        else if (v < 4)
            return t.s;
    } else {
        t.Attractors();
        t.Path();
    }
    t.T();
    if (v > 8) t.F();
    if (v > 10) t.Curve(), t.B(), t.F(), t.F(), t.Curve(), t.B(), t.F();
    return t.s;
}

// The meta-particle description's layout by version (0xbf1100).
InStream MetaTape(std::mt19937 &r, uint32_t v)
{
    Tape t{r};
    t.W(false), t.W();
    t.V2(), t.F(), t.V2(), t.V2(), t.V3(), t.V3(), t.V2(), t.V3(), t.V3();
    if (v > 1) t.F();
    t.Curve(), t.F(), t.W(), t.Curve(), t.F(), t.Curve(), t.F(), t.Curve(), t.F(), t.F();
    const uint32_t colors = t.Count(4); // over the flags' last word: never failing
    for (uint32_t i = 0; i < colors * 3; ++i) t.F(false);
    t.V3(), t.Curve(), t.F(), t.T(), t.B(), t.V3(), t.F(), t.F(), t.V3(), t.F(), t.F();
    const uint32_t warps = t.Count(4, colors);
    t.Warps(warps);
    t.B(), t.B(), t.B(), t.B();
    t.Colliders(t.Count(3, warps));
    t.F(), t.F(), t.F(), t.F(), t.F(), t.F(), t.F(), t.F(), t.V2();
    t.Walk(), t.Walk(), t.V3(), t.F(), t.F(), t.F();
    if (v < 4) {
        t.OldAttractor();
        if (v < 3)
            t.Points(t.Count(3));
        else
            t.Path();
    } else {
        t.Attractors();
        t.Path();
    }
    t.F();
    return t.s;
}

// ---- dumps ----

void PushWords(Blob &b, const void *p, size_t bytes)
{
    for (size_t i = 0; i < bytes; i += 4) {
        uint32_t w;
        std::memcpy(&w, static_cast<const uint8_t *>(p) + i, 4);
        b.push_back(w);
    }
}
void PushCurve(Blob &b, const cTSEffectCurve &c)
{
    b.push_back(c.mKind);
    b.push_back(uint32_t(c.mValues.size()));
    b.push_back(uint32_t(c.mValues.capacity()));
    PushWords(b, c.mValues.data(), c.mValues.size() * 4);
}
template <class T> void PushVector(Blob &b, const std::vector<T> &v)
{
    b.push_back(uint32_t(v.size()));
    b.push_back(uint32_t(v.capacity()));
}
void PushColors(Blob &b, const std::vector<Color> &v)
{
    PushVector(b, v);
    PushWords(b, v.data(), v.size() * sizeof(Color));
}
// `plane`: the plane's vectors (unset by the constructor) are compared.
void PushCollider(Blob &b, const cTSParticleCollider &c, bool plane = true)
{
    b.push_back(c.mFlags);
    PushString(b, c.mName);
    if (plane) PushWords(b, c.mPlane, sizeof c.mPlane);
    PushWords(b, &c.m40, 0x14);
    PushString(b, c.m54);
    PushString(b, c.m6C);
}
// +0x01..0x03 padding left out.
void PushAttractor(Blob &b, const cTSParticleAttractor &a)
{
    b.push_back(a.mHasCentre);
    PushWords(b, &a.mCentre, 0x18);
    PushCurve(b, a.mCurve);
    PushWords(b, &a.mStrength, 8);
}
void PushPath(Blob &b, const cTSParticlePath &p)
{
    PushVector(b, p.mPoints);
    PushWords(b, p.mPoints.data(), p.mPoints.size() * sizeof(cTSParticlePathPoint));
    b.push_back(p.mFlags);
}
// The vtable word left out, and the padding +0xed..0xef.
void PushBase(Blob &b, const cTSBaseParticlesDescription &d)
{
    PushWords(b, &d.mLife, 0xc);
    PushCurve(b, d.mRate);
    PushWords(b, &d.mRateVary, 0x54);
    PushCurve(b, d.mSize);
    PushWords(b, &d.mSizeVary, 4);
    PushCurve(b, d.mAlpha);
    PushWords(b, &d.mAlphaVary, 4);
    PushColors(b, d.mColors);
    PushWords(b, &d.mColorVary, 0xc);
    PushCurve(b, d.mAspect);
    PushWords(b, &d.mAspectVary, 0x28);
    b.push_back(d.mAlignment);
    PushWords(b, &d.mScrew, 4);
    PushVector(b, d.mWarps);
    PushWords(b, d.mWarps.data(), d.mWarps.size() * sizeof(cTSParticleWarp));
    PushWords(b, d.mBloom, 0x30);
    PushVector(b, d.mColliders);
    for (const cTSParticleCollider &c : d.mColliders) PushCollider(b, c);
    PushVector(b, d.mAttractors);
    for (const cTSParticleAttractor &a : d.mAttractors) PushAttractor(b, a);
    PushPath(b, d.mPath);
}
// The padding +0x17d..0x17f, +0x1eb, +0x1f6..0x1f7, +0x20d..0x20f, +0x229..0x22b and +0x284..0x287 left out.
void PushDescription(Blob &b, const cTSParticlesDescription &d)
{
    PushBase(b, d);
    PushWords(b, &d.mFlags, 0x24);
    b.push_back(d.mRotateCount);
    PushWords(b, d.mRotateVary, 0x18);
    PushCurve(b, d.mRotateCurve[0]);
    PushCurve(b, d.mRotateCurve[1]);
    PushColors(b, d.mPivots);
    PushWords(b, &d.mPivotVary, 0xc);
    PushString(b, d.mMaterial);
    b.push_back(d.mTile[0] | d.mTile[1] << 8 | d.mDraw << 16);
    PushWords(b, &d.mLayer, 8);
    b.push_back(d.mFrameStart | d.mFrameCount << 8);
    PushWords(b, &d.mStretch, 4);
    PushCurve(b, d.mScrewRadius);
    b.push_back(d.mScrewRadiusToSpeed);
    PushWords(b, &d.mScrewRadiusVary, 8);
    PushCurve(b, d.mScrewAngle);
    b.push_back(d.mScrewAngleToSpeed);
    PushWords(b, &d.mScrewAngleVary, 4);
    PushString(b, d.mConstrain);
    PushWords(b, &d.mConstrainOffset, 0x24);
    PushString(b, d.mDeathEffect);
}
// The padding +0x1e4..0x1e7 left out.
void PushDescription(Blob &b, const cTSMetaParticlesDescription &d)
{
    PushBase(b, d);
    PushWords(b, &d.mFlags, 8);
    PushCurve(b, d.mRotateCurve);
    PushWords(b, &d.mRotateVary, 8);
    PushString(b, d.mEffect);
    PushWords(b, d.mWalks, 0x54);
}

// ---- stand-ins ----

template <uint32_t kRva> void *__cdecl StandCreateEffect(void *d)
{
    Ev({evCreateEffect, Off(d), kRva});
    return reinterpret_cast<void *>(uintptr_t(kRva));
}
int __cdecl StubAtexit(void (*)())
{
    return 0;
}
struct StandIns {
    shape::CodePatch atexit{0x824cbeu, shape::CodeOf(&StubAtexit)};
    shape::StandIn particles{0xbc0c50u, &cTSParticlesEffect::Create, &StandCreateEffect<0xbc0c50u>};
    shape::StandIn meta{0xc03910u, &cTSMetaParticlesEffect::Create, &StandCreateEffect<0xc03910u>};
};

void Randomize(void *p, size_t n, std::mt19937 &r)
{
    for (size_t i = 0; i < n; ++i) static_cast<uint8_t *>(p)[i] = uint8_t(r());
}
void PushFrees(Blob &b, difftest::FreeRecorder &rec)
{
    rec.Stop();
    size_t sizes[128];
    const size_t n = rec.SortedSizes(sizes, 128);
    b.push_back(uint32_t(n));
    for (size_t i = 0; i < n && i < 128; ++i) b.push_back(uint32_t(sizes[i]));
}

using Parts = cTSParticlePartsAdapters;

// ---- the descriptions' own functions ----

struct ParticlesKind {
    using D = cTSParticlesDescription;
    static constexpr uint32_t kNewest = 11;
    static constexpr uint32_t construct = 0xbe15d0, destroy = 0xbe1be0, copy = 0xbe2700, move = 0xbe2320, read = 0xbe72e0,
                              write = 0xbe7db0, deleting = 0xbe2bb0;
    static InStream Tape(std::mt19937 &r, uint32_t v) { return ParticlesTape(r, v); }
    static D *Construct(void *m) { return Parts::ConstructParticles(m, nullptr); }
    static void Destroy(D *d) { Parts::DestroyParticles(d, nullptr); }
    static D *Delete(D *d, uint32_t flags) { return Parts::DeleteParticles(d, nullptr, flags); }
    static D *Copy(D *to, const D *from) { return Parts::CopyParticles(to, nullptr, from); }
    static D *Move(D *to, D *from) { return Parts::MoveParticles(to, nullptr, from); }
};
struct MetaKind {
    using D = cTSMetaParticlesDescription;
    static constexpr uint32_t kNewest = 4;
    static constexpr uint32_t construct = 0xbecf80, destroy = 0xbed140, copy = 0xbed200, move = 0, read = 0xbf1100,
                              write = 0xbf1c80, deleting = 0xbed3f0;
    static InStream Tape(std::mt19937 &r, uint32_t v) { return MetaTape(r, v); }
    static D *Construct(void *m) { return Parts::ConstructMeta(m, nullptr); }
    static void Destroy(D *d) { Parts::DestroyMeta(d, nullptr); }
    static D *Delete(D *d, uint32_t flags) { return Parts::DeleteMeta(d, nullptr, flags); }
    static D *Copy(D *to, const D *from) { return Parts::CopyMeta(to, nullptr, from); }
    static D *Move(D *, D *) { return nullptr; }
};

// Two descriptions built on garbage blocks, read from tapes of every version, copied (each way, onto itself), moved, their
// bases assigned (0xbe1df0), written, then destroyed: their frees compared.
template <class K> void RunDescriptions(difftest::Context &ctx, const char *name, uint32_t seeds)
{
    using D = typename K::D;
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            Blob &b = blobs[mode];
            gEv.clear();
            gTextWritten = false;
            std::mt19937 r(seed * 7u + K::kNewest);
            alignas(8) uint8_t blockA[sizeof(D)], blockB[sizeof(D)];
            Randomize(blockA, sizeof blockA, r);
            Randomize(blockB, sizeof blockB, r);
            void *gotA = mode == 0 ? ctx.Original<void *(__thiscall *)(void *)>(K::construct)(blockA) : K::Construct(blockA);
            void *gotB = mode == 0 ? ctx.Original<void *(__thiscall *)(void *)>(K::construct)(blockB) : K::Construct(blockB);
            b.push_back(uint32_t(gotA == blockA) | uint32_t(gotB == blockB) << 1);
            auto *a = reinterpret_cast<D *>(blockA);
            auto *bb = reinterpret_cast<D *>(blockB);
            PushDescription(b, *a);
            PushDescription(b, *bb);
            for (int step = 0; step < 10; ++step) {
                const uint32_t op = r() % 8;
                b.push_back(0x0b000000u | op);
                switch (op) {
                case 0:
                case 1:
                case 2: {
                    const uint32_t v = RandomVersion(r, K::kNewest);
                    InStream s = K::Tape(r, v);
                    D *d = op == 1 ? bb : a;
                    if (mode == 0)
                        ctx.Original<void(__cdecl *)(void *, uint32_t, D *)>(K::read)(&s, v, d);
                    else
                        D::Read(&s, v, d);
                    b.push_back(v);
                    b.push_back(uint32_t(s.pos));
                    b.push_back(uint32_t(s.words.size()));
                    break;
                }
                case 3: {
                    D *to = r() % 2 ? a : bb;
                    D *from = r() % 3 ? (to == a ? bb : a) : to;
                    void *got = mode == 0 ? ctx.Original<void *(__thiscall *)(void *, const void *)>(K::copy)(to, from) : K::Copy(to, from);
                    b.push_back(uint32_t(got == to));
                    break;
                }
                case 4: {
                    if (!K::move) break;
                    D *to = r() % 2 ? a : bb;
                    D *from = to == a ? bb : a;
                    void *got = mode == 0 ? ctx.Original<void *(__thiscall *)(void *, void *)>(K::move)(to, from) : K::Move(to, from);
                    b.push_back(uint32_t(got == to));
                    break;
                }
                case 5: {
                    D *to = r() % 2 ? a : bb;
                    D *from = r() % 3 ? (to == a ? bb : a) : to;
                    void *got = mode == 0 ? ctx.Original<void *(__thiscall *)(void *, const void *)>(0xbe1df0)(to, from)
                                          : Parts::AssignBase(to, nullptr, from);
                    b.push_back(uint32_t(got == to));
                    break;
                }
                default: {
                    OutStream s{kStreams.out};
                    const D *d = r() % 2 ? a : bb;
                    if (mode == 0)
                        ctx.Original<void(__cdecl *)(void *, const D *)>(K::write)(&s, d);
                    else
                        D::Write(&s, d);
                    break;
                }
                }
                b.insert(b.end(), gEv.begin(), gEv.end());
                gEv.clear();
                PushDescription(b, *a);
                PushDescription(b, *bb);
            }
            // `bb` on odd seeds through the deleting destructor (flags 0: a stack block).
            difftest::FreeRecorder rec;
            if (mode == 0) {
                ctx.Original<void(__thiscall *)(void *)>(K::destroy)(a);
                if (seed % 2)
                    b.push_back(uint32_t(ctx.Original<void *(__thiscall *)(void *, uint32_t)>(K::deleting)(bb, 0) == bb));
                else
                    ctx.Original<void(__thiscall *)(void *)>(K::destroy)(bb);
            } else {
                K::Destroy(a);
                if (seed % 2)
                    b.push_back(uint32_t(K::Delete(bb, 0) == bb));
                else
                    K::Destroy(bb);
            }
            PushFrees(b, rec);
        }
        Same(ctx, blobs[0], blobs[1], name, seed);
    }
}

// Heap descriptions read from a tape, then deleted through the deleting destructor with flags 1 (or 0, then freed here): the
// frees, the block's among them.
template <class K> void RunDeleting(difftest::Context &ctx, const char *name)
{
    using D = typename K::D;
    for (uint32_t seed = 0; seed < 20; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            Blob &b = blobs[mode];
            gEv.clear();
            gTextWritten = false;
            std::mt19937 r(seed * 5u + K::kNewest);
            void *block = ::operator new(sizeof(D));
            Randomize(block, sizeof(D), r);
            D *d = static_cast<D *>(mode == 0 ? ctx.Original<void *(__thiscall *)(void *)>(K::construct)(block) : K::Construct(block));
            const uint32_t v = RandomVersion(r, K::kNewest);
            InStream s = K::Tape(r, v);
            if (mode == 0)
                ctx.Original<void(__cdecl *)(void *, uint32_t, D *)>(K::read)(&s, v, d);
            else
                D::Read(&s, v, d);
            const uint32_t flags = seed % 3 ? 1u : 0u;
            difftest::FreeRecorder rec;
            void *got = mode == 0 ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(K::deleting)(d, flags) : K::Delete(d, flags);
            rec.Stop();
            b.push_back(uint32_t(got == block));
            b.push_back(uint32_t(rec.Freed(block)));
            PushFrees(b, rec);
            if (!flags) ::operator delete(block);
        }
        Same(ctx, blobs[0], blobs[1], name, seed);
    }
}

} // namespace

// The particle description (constructor 0xbe15d0, destructor 0xbe1be0, copy 0xbe2700, move 0xbe2320, the base's assignment
// 0xbe1df0, serializers 0xbe72e0 / 0xbe7db0 over versions 1..11; through them the parts' serializers 0xbde880, 0xbde940,
// 0xbfa580, 0xbfa660, 0xbe02f0, 0xbde7b0, 0xbddd90, 0xbdde70, 0xbddd80, 0xbdde60, 0xbde550, 0xbde750, 0xbfa5b0, 0xbfa6a0,
// 0xbfb430, 0xbfb510, 0xbfa550, 0xbfa620, 0xbde600, 0xbde620, the collider and attractor constructors, copies and destructors,
// and the base's constructor and destructor 0xbfb5a0 / 0xbfb990).
DIFF_TEST(ParticlesDescription_Values, 0xbe72e0)
{
    StandIns stands;
    RunDescriptions<ParticlesKind>(ctx, "particles", 160);
}

// The meta-particle description (constructor 0xbecf80, destructor 0xbed140, copy 0xbed200, 0xbe1df0, serializers 0xbf1100 /
// 0xbf1c80 over versions 1..4; through them also the walks' 0xbfa500 / 0xbfa5c0).
DIFF_TEST(ParticlesDescription_MetaValues, 0xbf1100)
{
    StandIns stands;
    RunDescriptions<MetaKind>(ctx, "meta particles", 160);
}

// The descriptions' deleting destructors 0xbe2bb0 (particles), 0xbed3f0 (meta particles) and 0xbfbb30 (the base, on a base
// built by 0xbfb5a0), slot 0 of the game's vtables.
DIFF_TEST(ParticlesDescription_Deleting, 0xbe2bb0)
{
    StandIns stands;
    RunDeleting<ParticlesKind>(ctx, "particles deleting");
    RunDeleting<MetaKind>(ctx, "meta deleting");
    for (uint32_t seed = 0; seed < 8; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            Blob &b = blobs[mode];
            std::mt19937 r(seed);
            void *block = ::operator new(sizeof(cTSBaseParticlesDescription));
            Randomize(block, sizeof(cTSBaseParticlesDescription), r);
            auto *d = static_cast<cTSBaseParticlesDescription *>(
                mode == 0 ? ctx.Original<void *(__thiscall *)(void *)>(0xbfb5a0)(block) : Parts::ConstructBase(block, nullptr));
            const uint32_t flags = seed % 2;
            difftest::FreeRecorder rec;
            void *got = mode == 0 ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(0xbfbb30)(d, flags)
                                  : Parts::DeleteBase(d, nullptr, flags);
            rec.Stop();
            b.push_back(uint32_t(got == block));
            b.push_back(uint32_t(rec.Freed(block)));
            PushFrees(b, rec);
            if (!flags) ::operator delete(block);
        }
        Same(ctx, blobs[0], blobs[1], "base deleting", seed);
    }
}

// Every version's tape against the readers once (the version boundaries: 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 and the meta's 1..4),
// on fresh descriptions.
DIFF_TEST(ParticlesDescription_Versions, 0xbf1100)
{
    StandIns stands;
    for (uint32_t v = 1; v <= 11; ++v) {
        for (uint32_t seed = 0; seed < 8; ++seed) {
            Blob blobs[2];
            for (int mode = 0; mode < 2; ++mode) {
                Blob &b = blobs[mode];
                gEv.clear();
                gTextWritten = false;
                std::mt19937 r(seed * 131u + v);
                alignas(8) uint8_t block[sizeof(cTSParticlesDescription)];
                Randomize(block, sizeof block, r);
                auto *d = static_cast<cTSParticlesDescription *>(
                    mode == 0 ? ctx.Original<void *(__thiscall *)(void *)>(0xbe15d0)(block) : Parts::ConstructParticles(block, nullptr));
                InStream s = ParticlesTape(r, v);
                if (mode == 0)
                    ctx.Original<void(__cdecl *)(void *, uint32_t, void *)>(0xbe72e0)(&s, v, d);
                else
                    cTSParticlesDescription::Read(&s, v, d);
                b.push_back(uint32_t(s.pos));
                b.push_back(uint32_t(s.words.size()));
                b.insert(b.end(), gEv.begin(), gEv.end());
                PushDescription(b, *d);
                if (mode == 0)
                    ctx.Original<void(__thiscall *)(void *)>(0xbe1be0)(d);
                else
                    Parts::DestroyParticles(d, nullptr);
            }
            Same(ctx, blobs[0], blobs[1], "particles version", v * 100 + seed);
        }
    }
    for (uint32_t v = 1; v <= 4; ++v) {
        for (uint32_t seed = 0; seed < 8; ++seed) {
            Blob blobs[2];
            for (int mode = 0; mode < 2; ++mode) {
                Blob &b = blobs[mode];
                gEv.clear();
                gTextWritten = false;
                std::mt19937 r(seed * 137u + v);
                alignas(8) uint8_t block[sizeof(cTSMetaParticlesDescription)];
                Randomize(block, sizeof block, r);
                auto *d = static_cast<cTSMetaParticlesDescription *>(
                    mode == 0 ? ctx.Original<void *(__thiscall *)(void *)>(0xbecf80)(block) : Parts::ConstructMeta(block, nullptr));
                InStream s = MetaTape(r, v);
                if (mode == 0)
                    ctx.Original<void(__cdecl *)(void *, uint32_t, void *)>(0xbf1100)(&s, v, d);
                else
                    cTSMetaParticlesDescription::Read(&s, v, d);
                b.push_back(uint32_t(s.pos));
                b.push_back(uint32_t(s.words.size()));
                b.insert(b.end(), gEv.begin(), gEv.end());
                PushDescription(b, *d);
                if (mode == 0)
                    ctx.Original<void(__thiscall *)(void *)>(0xbed140)(d);
                else
                    Parts::DestroyMeta(d, nullptr);
            }
            Same(ctx, blobs[0], blobs[1], "meta version", v * 100 + seed);
        }
    }
}

// The base alone: its constructor 0xbfb5a0 on a garbage block (everything but the vtable word and the padding), the
// assignment 0xbe1df0 from a particle description's base, its destructor 0xbfb990 (the frees).
DIFF_TEST(ParticlesDescription_Base, 0xbfb5a0)
{
    StandIns stands;
    for (uint32_t seed = 0; seed < 40; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            Blob &b = blobs[mode];
            gEv.clear();
            gTextWritten = false;
            std::mt19937 r(seed + 5);
            alignas(8) uint8_t block[sizeof(cTSBaseParticlesDescription)], source[sizeof(cTSParticlesDescription)];
            Randomize(block, sizeof block, r);
            Randomize(source, sizeof source, r);
            void *got = mode == 0 ? ctx.Original<void *(__thiscall *)(void *)>(0xbfb5a0)(block) : Parts::ConstructBase(block, nullptr);
            b.push_back(uint32_t(got == block));
            auto *base = reinterpret_cast<cTSBaseParticlesDescription *>(block);
            PushBase(b, *base);
            auto *from = static_cast<cTSParticlesDescription *>(
                mode == 0 ? ctx.Original<void *(__thiscall *)(void *)>(0xbe15d0)(source) : Parts::ConstructParticles(source, nullptr));
            if (seed % 4) {
                InStream s = ParticlesTape(r, 11);
                if (mode == 0)
                    ctx.Original<void(__cdecl *)(void *, uint32_t, void *)>(0xbe72e0)(&s, 11, from);
                else
                    cTSParticlesDescription::Read(&s, 11, from);
            }
            got = mode == 0 ? ctx.Original<void *(__thiscall *)(void *, const void *)>(0xbe1df0)(block, from)
                            : Parts::AssignBase(base, nullptr, from);
            b.push_back(uint32_t(got == block));
            PushBase(b, *base);
            difftest::FreeRecorder rec;
            if (mode == 0)
                ctx.Original<void(__thiscall *)(void *)>(0xbfb990)(block);
            else
                Parts::DestroyBase(base, nullptr);
            PushFrees(b, rec);
            if (mode == 0)
                ctx.Original<void(__thiscall *)(void *)>(0xbe1be0)(from);
            else
                Parts::DestroyParticles(from, nullptr);
        }
        Same(ctx, blobs[0], blobs[1], "base", seed);
    }
}

// ---- the parts through their own entries ----

namespace {

template <class Fn> Fn Orig(difftest::Context &ctx, uint32_t rva)
{
    return ctx.Original<Fn>(rva);
}

} // namespace

// The collider's constructor 0xbe1530 (on garbage: everything but the unset plane), serializers 0xbde880 / 0xbde940, copy
// constructor 0xbe1490 (onto garbage) and destructor 0xbe1aa0 (the frees); the attractor's constructor 0xbddd10, SetCentre
// 0xbde600, SetCurve 0xbde620, serializers 0xbde550 / 0xbde750 (this in ecx) and 0xbddd80 / 0xbdde60 (cdecl), copy 0xbe13d0,
// destructor 0xbe1920; the lists 0xbddd90 / 0xbdde70; the warp 0xbfa580 / 0xbfa660, path point 0xbfa550 / 0xbfa620, path
// 0xbfb430 / 0xbfb510 (this in ecx) and 0xbfa5b0 / 0xbfa6a0 (cdecl), walk 0xbfa500 / 0xbfa5c0, colour list 0xbe02f0 and string
// 0xbde7b0 readers.
DIFF_TEST(ParticlesDescription_Parts, 0xbde880)
{
    StandIns stands;
    using Cdecl2 = void(__cdecl *)(void *, void *);
    for (uint32_t seed = 0; seed < 200; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            Blob &b = blobs[mode];
            gEv.clear();
            gTextWritten = false;
            std::mt19937 r(seed * 3u + 1);
            const bool orig = mode == 0;
            // collider
            alignas(8) uint8_t cA[sizeof(cTSParticleCollider)], cB[sizeof(cTSParticleCollider)];
            Randomize(cA, sizeof cA, r);
            Randomize(cB, sizeof cB, r);
            auto *c = static_cast<cTSParticleCollider *>(orig ? Orig<void *(__thiscall *)(void *)>(ctx, 0xbe1530)(cA)
                                                               : Parts::ConstructCollider(cA, nullptr));
            b.push_back(uint32_t(c == reinterpret_cast<void *>(cA)));
            PushCollider(b, *c, false);
            {
                Tape t{r};
                t.Collider();
                if (orig)
                    Orig<Cdecl2>(ctx, 0xbde880)(&t.s, c);
                else
                    cTSParticleCollider::Read(&t.s, c);
                b.push_back(uint32_t(t.s.pos));
            }
            PushCollider(b, *c);
            auto *c2 = static_cast<cTSParticleCollider *>(orig ? Orig<void *(__thiscall *)(void *, const void *)>(ctx, 0xbe1490)(cB, c)
                                                                : Parts::CopyCollider(cB, nullptr, c));
            b.push_back(uint32_t(c2 == reinterpret_cast<void *>(cB)));
            PushCollider(b, *c2);
            {
                OutStream s{kStreams.out};
                if (orig)
                    Orig<Cdecl2>(ctx, 0xbde940)(&s, c2);
                else
                    cTSParticleCollider::Write(&s, c2);
            }
            {
                difftest::FreeRecorder rec;
                if (orig) {
                    Orig<void(__thiscall *)(void *)>(ctx, 0xbe1aa0)(c);
                    Orig<void(__thiscall *)(void *)>(ctx, 0xbe1aa0)(c2);
                } else {
                    Parts::DestroyCollider(c, nullptr);
                    Parts::DestroyCollider(c2, nullptr);
                }
                PushFrees(b, rec);
            }
            // attractor
            alignas(8) uint8_t aA[sizeof(cTSParticleAttractor)], aB[sizeof(cTSParticleAttractor)];
            Randomize(aA, sizeof aA, r);
            Randomize(aB, sizeof aB, r);
            auto *a = static_cast<cTSParticleAttractor *>(orig ? Orig<void *(__thiscall *)(void *)>(ctx, 0xbddd10)(aA)
                                                                : Parts::ConstructAttractor(aA, nullptr));
            b.push_back(uint32_t(a == reinterpret_cast<void *>(aA)));
            PushAttractor(b, *a);
            for (int step = 0; step < 6; ++step) {
                const uint32_t op = r() % 6;
                b.push_back(0x0b000000u | op);
                switch (op) {
                case 0: {
                    const Vector3 centre{float(r() % 9), -float(r() % 9), 0.5f};
                    if (orig)
                        Orig<void(__thiscall *)(void *, const void *)>(ctx, 0xbde600)(a, &centre);
                    else
                        Parts::SetCentre(a, nullptr, &centre);
                    break;
                }
                case 1: {
                    cTSEffectCurve curve(float(r() % 7));
                    if (r() % 2) curve.mValues.assign(r() % 6, 2.5f);
                    if (orig)
                        Orig<void(__thiscall *)(void *, const void *)>(ctx, 0xbde620)(a, &curve);
                    else
                        Parts::SetCurve(a, nullptr, &curve);
                    break;
                }
                case 2: {
                    Tape t{r};
                    t.Attractor();
                    const bool viaCdecl = r() % 2;
                    if (orig)
                        viaCdecl ?Orig<Cdecl2>(ctx, 0xbddd80)(&t.s, a) : Orig<void(__thiscall *)(void *, void *)>(ctx, 0xbde550)(a, &t.s);
                    else
                        viaCdecl ?Parts::ReadAttractorOf(&t.s, a) : Parts::ReadAttractor(a, nullptr, &t.s);
                    b.push_back(uint32_t(t.s.pos));
                    break;
                }
                case 3: {
                    OutStream s{kStreams.out};
                    const bool viaCdecl = r() % 2;
                    if (orig)
                        viaCdecl ?Orig<Cdecl2>(ctx, 0xbdde60)(&s, a) : Orig<void(__thiscall *)(void *, void *)>(ctx, 0xbde750)(a, &s);
                    else
                        viaCdecl ?Parts::WriteAttractorOf(&s, a) : Parts::WriteAttractor(a, nullptr, &s);
                    break;
                }
                default: break;
                }
                b.insert(b.end(), gEv.begin(), gEv.end());
                gEv.clear();
                PushAttractor(b, *a);
            }
            auto *a2 = static_cast<cTSParticleAttractor *>(orig ? Orig<void *(__thiscall *)(void *, const void *)>(ctx, 0xbe13d0)(aB, a)
                                                                 : Parts::CopyAttractor(aB, nullptr, a));
            b.push_back(uint32_t(a2 == reinterpret_cast<void *>(aB)));
            PushAttractor(b, *a2);
            {
                difftest::FreeRecorder rec;
                if (orig) {
                    Orig<void(__thiscall *)(void *)>(ctx, 0xbe1920)(a);
                    Orig<void(__thiscall *)(void *)>(ctx, 0xbe1920)(a2);
                } else {
                    Parts::DestroyAttractor(a, nullptr);
                    Parts::DestroyAttractor(a2, nullptr);
                }
                PushFrees(b, rec);
            }
            // the lists, path, warp, point, walk, colours, string
            std::vector<cTSParticleAttractor> attractors(r() % 3);
            cTSParticlePath path;
            path.mPoints.resize(r() % 3);
            cTSParticleWarp warp;
            Randomize(&warp, sizeof warp, r);
            cTSParticlePathPoint point{};
            cTSParticleWalk walk{};
            std::vector<Color> colors(r() % 3, Color(1.0f, 2.0f, 3.0f));
            std::string text(r() % 2 ? 3 : 20, 'q');
            for (int step = 0; step < 8; ++step) {
                const uint32_t op = r() % 8;
                b.push_back(0x0b000000u | op);
                Tape t{r};
                OutStream o{kStreams.out};
                const bool write = r() % 3 == 0;
                switch (op) {
                case 0:
                    if (write) {
                        orig ? Orig<Cdecl2>(ctx, 0xbdde70)(&o, &attractors) : Parts::WriteAttractors(&o, &attractors);
                    } else {
                        t.Attractors();
                        orig ? Orig<Cdecl2>(ctx, 0xbddd90)(&t.s, &attractors) : Parts::ReadAttractors(&t.s, &attractors);
                    }
                    break;
                case 1: {
                    const bool viaCdecl = r() % 2;
                    if (write) {
                        if (orig)
                            viaCdecl ?Orig<Cdecl2>(ctx, 0xbfa6a0)(&o, &path) : Orig<void(__thiscall *)(void *, void *)>(ctx, 0xbfb510)(&path, &o);
                        else
                            viaCdecl ?Parts::WritePathOf(&o, &path) : Parts::WritePath(&path, nullptr, &o);
                    } else {
                        t.Path();
                        if (orig)
                            viaCdecl ?Orig<Cdecl2>(ctx, 0xbfa5b0)(&t.s, &path) : Orig<void(__thiscall *)(void *, void *)>(ctx, 0xbfb430)(&path, &t.s);
                        else
                            viaCdecl ?Parts::ReadPathOf(&t.s, &path) : Parts::ReadPath(&path, nullptr, &t.s);
                    }
                    break;
                }
                case 2:
                    if (write) {
                        orig ? Orig<Cdecl2>(ctx, 0xbfa660)(&o, &warp) : cTSParticleWarp::Write(&o, &warp);
                    } else {
                        t.F(), t.V3(), t.V3(); // a warp's own fields are all set here
                        orig ? Orig<Cdecl2>(ctx, 0xbfa580)(&t.s, &warp) : cTSParticleWarp::Read(&t.s, &warp);
                    }
                    break;
                case 3:
                    if (write) {
                        orig ? Orig<Cdecl2>(ctx, 0xbfa620)(&o, &point) : cTSParticlePathPoint::Write(&o, &point);
                    } else {
                        t.Points(1);
                        orig ? Orig<Cdecl2>(ctx, 0xbfa550)(&t.s, &point) : cTSParticlePathPoint::Read(&t.s, &point);
                    }
                    break;
                case 4:
                    if (write) {
                        orig ? Orig<Cdecl2>(ctx, 0xbfa5c0)(&o, &walk) : cTSParticleWalk::Write(&o, &walk);
                    } else {
                        t.Walk();
                        orig ? Orig<Cdecl2>(ctx, 0xbfa500)(&t.s, &walk) : cTSParticleWalk::Read(&t.s, &walk);
                    }
                    break;
                case 5: {
                    t.Triples();
                    void *got = orig ? Orig<void *(__cdecl *)(void *, void *)>(ctx, 0xbe02f0)(&t.s, &colors) : ReadParticleTriples(&t.s, &colors);
                    b.push_back(uint32_t(got == &t.s));
                    break;
                }
                default: {
                    t.T();
                    void *got = orig ? Orig<void *(__cdecl *)(void *, void *)>(ctx, 0xbde7b0)(&t.s, &text) : ReadParticleText(&t.s, &text);
                    b.push_back(uint32_t(got == &t.s));
                    break;
                }
                }
                b.push_back(uint32_t(t.s.pos));
                b.insert(b.end(), gEv.begin(), gEv.end());
                gEv.clear();
                PushVector(b, attractors);
                for (const cTSParticleAttractor &x : attractors) PushAttractor(b, x);
                PushPath(b, path);
                PushWords(b, &warp, sizeof warp);
                PushWords(b, &point, sizeof point);
                PushWords(b, &walk, sizeof walk);
                PushColors(b, colors);
                PushString(b, text);
            }
        }
        Same(ctx, blobs[0], blobs[1], "parts", seed);
    }
}

// ---- the components ----

namespace {

struct ComponentInfo {
    const char *name;
    int32_t type;
    size_t size;
    uint32_t factory, createEffect, copy, read, write, getSize, dtor;
    cITSComponentDescription *(__cdecl *create)();
    void *aCreateEffect, *aCopy, *aRead, *aWrite, *aGetSize, *aDtor;
    uint32_t newest;
    InStream (*tape)(std::mt19937 &, uint32_t);
    void (*push)(Blob &, const void *);
};

template <class T> ComponentInfo Component(const char *name, uint32_t factory, uint32_t createEffect, uint32_t copy, uint32_t read,
                                           uint32_t write, uint32_t getSize, uint32_t dtor, uint32_t newest,
                                           InStream (*tape)(std::mt19937 &, uint32_t))
{
    using A = cTSCompDescriptionAdapters<T>;
    return {name,
            T::kComponentType,
            sizeof(T),
            factory,
            createEffect,
            copy,
            read,
            write,
            getSize,
            dtor,
            &T::Create,
            reinterpret_cast<void *>(&A::CreateEffect),
            reinterpret_cast<void *>(&A::CopyTo),
            reinterpret_cast<void *>(&A::Read),
            reinterpret_cast<void *>(&A::Write),
            reinterpret_cast<void *>(&A::GetSize),
            reinterpret_cast<void *>(&A::DeletingDestructor),
            newest,
            tape,
            [](Blob &b, const void *d) { PushDescription(b, *static_cast<const typename T::Description *>(d)); }};
}

const ComponentInfo kComponents[] = {
    Component<cTSParticlesComponentDescription>("particles", 0xbe7ba0, 0xbe58b0, 0xbe2be0, 0xbe72a0, 0xbe7d80, 0xbe58c0, 0xbe2b20, 11,
                                                &ParticlesTape),
    Component<cTSMetaParticlesComponentDescription>("meta particles", 0xbf1a70, 0xbefb70, 0xbed420, 0xbf10c0, 0xbf1c50, 0xbefb80,
                                                    0xbed330, 4, &MetaTape),
};

void *Face4(void *obj)
{
    return static_cast<uint8_t *>(obj) + 4;
}
void PushObject(Blob &b, const ComponentInfo &t, void *obj)
{
    t.push(b, static_cast<uint8_t *>(obj) + 0x10);
    b.push_back(*reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(obj) + 8)); // the count
}

// The slot: the original at `rva` (mode 0), our vtable (2), the adapter (1).
template <class R, class... Args> R Slot(difftest::Context &ctx, int mode, void *obj, uint32_t rva, void *adapter, int slot, Args... args)
{
    if (mode == 0) return ctx.Original<R(__thiscall *)(void *, Args...)>(rva)(obj, args...);
    if (mode == 1) return reinterpret_cast<R(__fastcall *)(void *, void *, Args...)>(adapter)(obj, nullptr, args...);
    return CallSlot<R>(obj, slot, args...);
}
void *Create(difftest::Context &ctx, int mode, const ComponentInfo &t)
{
    return mode == 0 ? ctx.Original<void *(__cdecl *)()>(t.factory)() : static_cast<void *>(t.create());
}

} // namespace

// Per type: a component and a copy target (its type or the other), random slot calls (GetType through the vtable, GetSize,
// CreateEffect, CopyTo, Read with versions 0..newest + 1, Write, the counts), then the deletion (the last Release, or the deleting
// destructor with flags 1 or 0: mode 2 through the +4 face's thunk).
DIFF_TEST(ParticlesDescription_Components, 0xbe7ba0)
{
    StandIns stands;
    struct OldStrength {
        OldStrength() { Tape::gOldStrengthMayFail = false; }
        ~OldStrength() { Tape::gOldStrengthMayFail = true; }
    } oldStrength;
    for (const ComponentInfo &t : kComponents) {
        for (uint32_t seed = 0; seed < 60; ++seed) {
            Blob blobs[3];
            for (int mode = 0; mode < 3; ++mode) {
                Blob &b = blobs[mode];
                gEv.clear();
                gTextWritten = false;
                std::mt19937 r(seed * 31u + uint32_t(t.type));
                void *obj = Create(ctx, mode, t);
                const ComponentInfo &tt = r() % 3 ? t : kComponents[t.type == 1 ? 1 : 0];
                void *target = Create(ctx, mode, tt);
                gObj = static_cast<uint8_t *>(obj);
                gObjSize = t.size;
                CallSlot<uint32_t>(obj, 7);
                CallSlot<uint32_t>(target, 7);
                PushObject(b, t, obj);
                PushObject(b, tt, target);
                for (int step = 0; step < 16; ++step) {
                    const uint32_t op = r() % 7;
                    b.push_back(0x0b000000u | op);
                    switch (op) {
                    case 0:
                        b.push_back(uint32_t(CallSlot<int32_t>(obj, 0)));
                        b.push_back(Off(CallSlot<void *>(obj, 3)));
                        b.push_back(Slot<uint32_t>(ctx, mode, obj, t.getSize, t.aGetSize, 6));
                        break;
                    case 1: b.push_back(uint32_t(uintptr_t(Slot<void *>(ctx, mode, obj, t.createEffect, t.aCreateEffect, 1)))); break;
                    case 2: Slot<void, void *>(ctx, mode, obj, t.copy, t.aCopy, 2, target); break;
                    case 3:
                    case 4: {
                        const uint32_t k = r() % 8;
                        const uint32_t v = k == 0 ? 0 : k == 1 ? t.newest + 1 : k == 2 ? 0xffffu : RandomVersion(r, t.newest);
                        InStream s = (v >= 1 && v <= t.newest) ? t.tape(r, v) : InStream{kStreams.in};
                        s.words.insert(s.words.begin(), v);
                        s.fails.insert(s.fails.begin(), 0);
                        Slot<void, void *>(ctx, mode, obj, t.read, t.aRead, 4, &s);
                        b.push_back(uint32_t(s.pos));
                        break;
                    }
                    case 5: {
                        OutStream s{kStreams.out};
                        Slot<void, void *>(ctx, mode, obj, t.write, t.aWrite, 5, &s);
                        break;
                    }
                    default: {
                        const bool face4 = r() % 2;
                        b.push_back(face4 ? CallSlot<uint32_t>(Face4(obj), 2) : CallSlot<uint32_t>(obj, 7));
                        b.push_back(face4 ? CallSlot<uint32_t>(Face4(obj), 1) : CallSlot<uint32_t>(obj, 8));
                        break;
                    }
                    }
                    b.insert(b.end(), gEv.begin(), gEv.end());
                    gEv.clear();
                    PushObject(b, t, obj);
                    PushObject(b, tt, target);
                }
                const uint32_t how = seed % 3;
                difftest::FreeRecorder rec;
                if (how == 0) {
                    const bool face4 = r() % 2;
                    b.push_back(face4 ? CallSlot<uint32_t>(Face4(obj), 1) : CallSlot<uint32_t>(obj, 8));
                } else {
                    const uint32_t flags = how == 1 ? 1u : 0u;
                    void *got = mode == 0   ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(t.dtor)(obj, flags)
                                : mode == 1 ? reinterpret_cast<void *(__fastcall *)(void *, void *, uint32_t)>(t.aDtor)(obj, nullptr, flags)
                                            : CallSlot<void *>(Face4(obj), 0, flags);
                    b.push_back(uint32_t(got == obj));
                }
                rec.Stop();
                b.push_back(uint32_t(rec.Freed(obj)));
                PushFrees(b, rec);
                if (how == 2) ::operator delete(obj);
                b.push_back(CallSlot<uint32_t>(target, 8));
                b.insert(b.end(), gEv.begin(), gEv.end());
                gEv.clear();
                gObj = nullptr;
            }
            char what[64];
            std::snprintf(what, sizeof what, "%s (adapters)", t.name);
            Same(ctx, blobs[0], blobs[1], what, seed);
            std::snprintf(what, sizeof what, "%s (ours)", t.name);
            Same(ctx, blobs[0], blobs[2], what, seed);
        }
    }
}

// ---- the curve (lane IX's part) ----

// Scale 0xbdc950 and Divide 0xbdc9f0 on lists (0-6 values), waves and other kinds, by factors and divisors that are not 0
// (a 0 throws the game's cTSCurveError through _CxxThrowException, an import the harness traps: not run here); NaN, the
// infinities, tiny and huge values included. The assignment 0xbe21f0 (onto itself too) and the destructor 0xbe1b80 (the
// frees).
DIFF_TEST(ParticlesDescription_Curve, 0xbdc950)
{
    const float specials[] = {std::nanf(""), INFINITY, -INFINITY, 1e-38f, 3e38f, -1.0f, 0.5f, 255.0f, -7.25f};
    for (uint32_t seed = 0; seed < 300; ++seed) {
        Blob blobs[2];
        for (int mode = 0; mode < 2; ++mode) {
            Blob &b = blobs[mode];
            std::mt19937 r(seed + 11);
            cTSEffectCurve curves[2];
            for (cTSEffectCurve &c : curves) {
                const uint32_t kinds[] = {0u, 0u, 0x81000000u, 0x05000000u};
                c.mKind = kinds[r() % 4];
                c.mValues.resize(c.mKind == 0x81000000u ? 4 : r() % 7);
                for (float &f : c.mValues) f = float(int(r() % 200) - 100) / 4.0f;
            }
            for (int step = 0; step < 8; ++step) {
                const uint32_t op = r() % 5;
                b.push_back(0x0b000000u | op);
                cTSEffectCurve &c = curves[r() % 2];
                float f = r() % 3 ? specials[r() % 9] : float(int(r() % 200) - 100) / 8.0f;
                if (f == 0.0f) f = 3.0f;
                switch (op) {
                case 0: {
                    void *got = mode == 0 ? ctx.Original<void *(__thiscall *)(void *, float)>(0xbdc950)(&c, f) : &c.Scale(f);
                    b.push_back(uint32_t(got == &c));
                    break;
                }
                case 1: {
                    if (std::isinf(f) && c.mKind == 0x81000000u) f = 2.0f; // 1 / inf is 0: a wave would throw
                    void *got = mode == 0 ? ctx.Original<void *(__thiscall *)(void *, float)>(0xbdc9f0)(&c, f) : &c.Divide(f);
                    b.push_back(uint32_t(got == &c));
                    break;
                }
                case 2: {
                    cTSEffectCurve &from = curves[r() % 2];
                    void *got = mode == 0 ? ctx.Original<void *(__thiscall *)(void *, const void *)>(0xbe21f0)(&c, &from)
                                          : cTSEffectCurveAdapters::Assign(&c, nullptr, &from);
                    b.push_back(uint32_t(got == &c));
                    break;
                }
                default: break;
                }
                for (const cTSEffectCurve &x : curves) PushCurve(b, x);
            }
            difftest::FreeRecorder rec;
            for (cTSEffectCurve &c : curves) {
                if (mode == 0)
                    ctx.Original<void(__thiscall *)(void *)>(0xbe1b80)(&c);
                else
                    cTSEffectCurveAdapters::Destroy(&c, nullptr);
                ::new (&c) cTSEffectCurve; // for the array's own destructor
            }
            PushFrees(b, rec);
        }
        Same(ctx, blobs[0], blobs[1], "curve", seed);
    }
}
