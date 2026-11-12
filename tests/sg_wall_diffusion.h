// Shared by the cWallDiffusionManager tests (tests/sg_wall_diffusion.cpp, row LD2d; meant as it is for rows LD2e, LD2f and
// LD2h): the game's heap patched to ours, a manager built alike on both sides from a seed (its settings, runs of segments,
// key set and two change lists, each vector with spare capacity drawn from its own pool), its dump and comparison, and the
// stand-in for the world's tile-location level 0x7f2260.
// fakes: the world's level 0x7f2260 (StandCellQuery: a hash of the location words and the flag, logged); the game's operator
// new / sized delete / free (0x824632, 0x824624, 0x82540f) go to ours, so either side frees the other's blocks.
//
// Reuse: FillLight / Color / Spread / Intensity draw a light (rows LD2j, LD2f; LD2l's blends can take them too).
// Fill builds a whole manager; FillRun and FillSegment build one record (into a vector the caller owns), DumpRun /
// DumpSegment / Dump write them, `Shape` picks what the draws look like (counts, linked runs, angles near the eight
// directions, NaNs). The segment padding (+0x175..+0x177) is never written by the game's constructor or copy, so blocks a
// vector instance moved or grew hold different padding on the two sides: Dump leaves it out unless asked (`pads`).
#pragma once

#include "code_patch.h"
#include "difftest.h"
#include "sg_lighting_common.h"

#include "sg/cWallDiffusionManager.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace wallfx {

using nTSSG::cWallDiffusionManager;
using nTSSG::Fixed16;
using nTSSG::WallDiffusionChange;
using nTSSG::WallDiffusionRun;
using nTSSG::WallDiffusionSegment;
using Blob = std::vector<uint32_t>;

inline void *__cdecl GameNew(size_t n) { return ::operator new(n); }
inline void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
inline void __cdecl GameFree(void *p) { ::operator delete(p); }

struct Fixture {
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
    shape::CodePatch gameFree{0x82540fu, shape::CodeOf(&GameFree)};
};

// A block the size of T filled with junk from `junk` (alike for equal seeds).
template <class T> struct Block {
    alignas(8) uint8_t bytes[sizeof(T)];
    explicit Block(uint32_t junk)
    {
        std::mt19937 g(junk);
        for (auto &b : bytes) b = uint8_t(g());
    }
    T *Get() { return reinterpret_cast<T *>(bytes); }
};

inline void RandomBytes(void *p, size_t n, std::mt19937 &gen)
{
    auto *b = static_cast<uint8_t *>(p);
    for (size_t i = 0; i < n; ++i) b[i] = uint8_t(gen());
}

// A byte argument: `low` in the low byte, junk above it.
inline uint32_t ByteArg(std::mt19937 &gen, uint8_t low) { return (gen() & 0xffffff00u) | low; }

inline uint8_t Flag(std::mt19937 &gen)
{
    static const uint8_t kFlags[] = {0, 0, 1, 1, 2, 0x80, 0xff};
    return kFlags[gen() % std::size(kFlags)];
}

// What a manager's draws look like.
struct Shape {
    uint32_t maxRuns = 4;      // 0..maxRuns runs
    uint32_t maxSegments = 4;  // 0..maxSegments segments a run (minSegments..)
    uint32_t minSegments = 0;  // 1: no empty run (for the accessors that read the first or last segment unchecked)
    bool linked = false;       // the runs' prev / next indices -1 or another run's (else any word)
    bool directions = false;   // angles near the eight directions (multiples of pi/4, a few ulps or 0.03 pi off)
    bool nans = false;         // NaNs of their own payloads among the floats
    uint32_t maxKeys = 5;      // the key set's size (from a pool of 8 small values and random words)
    uint32_t maxChanges = 3;   // each change list's
};

// A float: Nice's families (tests/sg_lighting_common.h) scaled around `scale`, a NaN now and then with `nans`.
inline float Value(std::mt19937 &gen, const Shape &shape, float scale = 64.0f)
{
    if (shape.nans && gen() % 6 == 0) return NaN(gen);
    return Nice(gen, scale);
}

// An angle: with `directions` a multiple of pi/4 in (-pi, pi], a few ulps either way or 0.03 pi off; else any Value.
inline float Angle(std::mt19937 &gen, const Shape &shape)
{
    if (!shape.directions) return Value(gen, shape, 4.0f);
    const float base = float(int(gen() % 8) - 3) * 0.78539819f;
    switch (gen() % 5) {
    case 0: return base;
    case 1: return std::nextafter(base, base + 1.0f);
    case 2: return std::nextafter(base, base - 1.0f);
    case 3: return base + (gen() % 2 ? 0.0942478f : -0.0942478f);
    default: return base + (float(int(gen() % 2001) - 1000) / 1000.0f) * 0.4f;
    }
}

template <class V> void Reserve(V &v, std::mt19937 &gen)
{
    if (gen() % 3 == 0) v.reserve(v.size() + 1 + gen() % 3);
}

// A segment reference: none ({-1, -1, -1}), small indices (a level 0..3, a run and segment below 6), or any words.
inline nTSSG::WallSegmentRef Ref(std::mt19937 &gen)
{
    switch (gen() % 4) {
    case 0: return {-1, -1, -1};
    case 1: return {int32_t(gen()), int32_t(gen()), int32_t(gen())};
    default: return {int32_t(gen() % 4), int32_t(gen() % 6), int32_t(gen() % 6)};
    }
}

// One segment, every field drawn (the padding too).
inline void FillSegment(WallDiffusionSegment &s, std::mt19937 &gen, const Shape &shape)
{
    for (float &f : s.mStart) f = Value(gen, shape);
    for (float &f : s.mEnd) f = Value(gen, shape);
    for (uint32_t &w : s.mLocation) w = gen() % 4 ? gen() % 64 : gen();
    for (uint32_t &w : s.mPacked) w = gen();
    RandomBytes(s.mCurrent, sizeof(s.mCurrent), gen);
    RandomBytes(s.mPrevious, sizeof(s.mPrevious), gen);
    s.mAbove = Ref(gen);
    s.mBelow = Ref(gen);
    s.mFlag = Flag(gen);
    RandomBytes(s.mPad175, sizeof(s.mPad175), gen);
}

// One run: its segments (with spare capacity now and then), indices among `runs` runs, angle and flags.
inline void FillRun(WallDiffusionRun &r, std::mt19937 &gen, const Shape &shape, uint32_t runs)
{
    const uint32_t span = shape.maxSegments >= shape.minSegments ? shape.maxSegments - shape.minSegments + 1 : 1;
    const uint32_t n = shape.minSegments + gen() % span;
    Reserve(r.mSegments, gen);
    for (uint32_t i = 0; i < n; ++i) {
        r.mSegments.emplace_back();
        FillSegment(r.mSegments.back(), gen, shape);
        if (gen() % 4 == 0) Reserve(r.mSegments, gen);
    }
    auto index = [&]() -> int32_t {
        if (!shape.linked) return int32_t(gen());
        return gen() % 3 == 0 || runs == 0 ? -1 : int32_t(gen() % runs);
    };
    r.mPrev = index();
    r.mNext = index();
    r.mAngle = Angle(gen, shape);
    r.mDiagonal = Flag(gen);
    r.mFlag19 = Flag(gen);
    r.mFlag1A = Flag(gen);
    r.mFlag1B = Flag(gen);
}

inline void FillChange(WallDiffusionChange &c, std::mt19937 &gen, const Shape &shape)
{
    c.mA[0] = Value(gen, shape);
    c.mA[1] = Value(gen, shape);
    c.mA2 = gen();
    c.mB[0] = Value(gen, shape);
    c.mB[1] = Value(gen, shape);
    c.mB2 = gen();
}

// The same manager on both sides from `seed`: on a constructed manager (ours), every setting random, then the runs, the
// key set and the change lists.
inline void Fill(cWallDiffusionManager &m, uint32_t seed, const Shape &shape)
{
    std::mt19937 gen(seed * 0x9e3779b9u + 0x4c2d);
    m.mThreshold = Value(gen, shape, 1.0f);
    m.mInsideWallAmbient = Value(gen, shape, 1.0f);
    m.mOutsideWallAmbient = Value(gen, shape, 1.0f);
    for (float &f : m.mInsideAmbientColor) f = Value(gen, shape, 1.0f);
    for (float &f : m.mOutsideAmbientColor) f = Value(gen, shape, 1.0f);
    m.mOutsideAmbientEnabled = Flag(gen);
    m.mPropagationSteps = gen() % 4 ? int32_t(gen() % 21) : int32_t(gen());
    const uint32_t runs = gen() % (shape.maxRuns + 1);
    Reserve(m.mRuns, gen);
    for (uint32_t i = 0; i < runs; ++i) {
        m.mRuns.emplace_back();
        FillRun(m.mRuns.back(), gen, shape, runs);
    }
    const uint32_t keys = gen() % (shape.maxKeys + 1);
    for (uint32_t i = 0; i < keys; ++i) m.mMarkedRuns.insert(gen() % 2 ? gen() % 8 : gen());
    for (auto *list : {&m.mChanges, &m.mChanges2}) {
        const uint32_t n = gen() % (shape.maxChanges + 1);
        Reserve(*list, gen);
        for (uint32_t i = 0; i < n; ++i) {
            list->emplace_back();
            FillChange(list->back(), gen, shape);
        }
    }
}

// ---- dumps ------------------------------------------------------------------------------------------------------------

inline void Words(Blob &b, const void *p, size_t bytes)
{
    const auto *w = static_cast<const uint8_t *>(p);
    for (size_t i = 0; i + 4 <= bytes; i += 4) {
        uint32_t v;
        std::memcpy(&v, w + i, 4);
        b.push_back(v);
    }
}

// A segment's fields (the padding with `pads`: see the top of the file).
inline void DumpSegment(Blob &b, const WallDiffusionSegment &s, bool pads)
{
    Words(b, &s, offsetof(WallDiffusionSegment, mFlag));
    b.push_back(s.mFlag);
    if (pads) b.push_back(uint32_t(s.mPad175[0]) | uint32_t(s.mPad175[1]) << 8 | uint32_t(s.mPad175[2]) << 16);
}

inline void DumpRun(Blob &b, const WallDiffusionRun &r, bool pads)
{
    b.push_back(uint32_t(r.mSegments.size()));
    b.push_back(uint32_t(r.mSegments.capacity()));
    for (const WallDiffusionSegment &s : r.mSegments) DumpSegment(b, s, pads);
    Words(b, &r.mPrev, sizeof(WallDiffusionRun) - offsetof(WallDiffusionRun, mPrev));
}

// The key set's tree, as MSVC's _Tree_node<unsigned> (the image's and ours alike: 0x14 bytes, the constructor's head).
struct Node {
    Node *left, *parent, *right;
    uint8_t color, isnil;
    uint32_t value;
};

inline void Walk(Blob &b, const Node *n, int depth)
{
    if (depth > 64) {
        b.push_back(0xbad);
        return;
    }
    if (n->isnil) {
        b.push_back(0xffff0000u);
        return;
    }
    b.push_back(n->value);
    b.push_back(uint32_t(n->color) | uint32_t(n->isnil) << 8);
    Walk(b, n->left, depth + 1);
    Walk(b, n->right, depth + 1);
}

template <class V> void Vector(Blob &b, const V &v)
{
    b.push_back(uint32_t(v.size()));
    b.push_back(uint32_t(v.capacity()));
    Words(b, v.data(), v.size() * sizeof(*v.data()));
}

// Our vtable's address (read from a manager ours constructed).
inline uint32_t OurVTable()
{
    static const uint32_t vt = [] {
        alignas(8) uint8_t bytes[sizeof(cWallDiffusionManager)];
        auto *m = cWallDiffusionManager::Construct(bytes, nullptr);
        uint32_t w;
        std::memcpy(&w, bytes, 4);
        m->~cWallDiffusionManager();
        return w;
    }();
    return vt;
}

// The vtable word as a class: 0x7ab1e for the class's (the image's 0xdc24f4 or ours: each side installs its own), else
// the word itself.
inline uint32_t VTableClass(const void *object)
{
    uint32_t w;
    std::memcpy(&w, object, 4);
    if (w == P(GameAddress<void>(cWallDiffusionManager::kVTable)) || w == OurVTable()) return 0x7ab1eu;
    return w;
}

// The manager: the vtable (as VTableClass) and count words, the runs, the settings, the key set (size, head flags,
// leftmost / rightmost, the tree in shape) and both change lists. No pointers.
inline Blob Dump(const cWallDiffusionManager &m, bool pads = false)
{
    Blob b;
    b.push_back(VTableClass(&m));
    b.push_back(m.mRefCount);
    b.push_back(uint32_t(m.mRuns.size()));
    b.push_back(uint32_t(m.mRuns.capacity()));
    for (const WallDiffusionRun &r : m.mRuns) DumpRun(b, r, pads);
    Words(b, &m.mThreshold, offsetof(cWallDiffusionManager, mMarkedRuns) - offsetof(cWallDiffusionManager, mThreshold));
    const Node *head = *reinterpret_cast<const Node *const *>(&m.mMarkedRuns);
    b.push_back(uint32_t(m.mMarkedRuns.size()));
    b.push_back(uint32_t(head->color) | uint32_t(head->isnil) << 8);
    b.push_back(head->left == head ? 0xeeeeeeeeu : head->left->value);
    b.push_back(head->right == head ? 0xeeeeeeeeu : head->right->value);
    Walk(b, head->parent, 0);
    Vector(b, m.mChanges);
    Vector(b, m.mChanges2);
    return b;
}

inline size_t FirstDiff(const Blob &a, const Blob &b)
{
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    return i;
}

inline void Compare(difftest::Context &ctx, const char *what, uint32_t seed, const Blob &a, const Blob &b)
{
    ++ctx.cases;
    if (a != b) {
        const size_t i = FirstDiff(a, b);
        ctx.Fail("%s seed %#x: differs at word %zu (%#x vs %#x; %zu vs %zu words)", what, seed, i,
                 i < a.size() ? a[i] : 0, i < b.size() ? b[i] : 0, a.size(), b.size());
    }
}

// Two managers built alike (constructed by ours on junk blocks, then filled from the seed), destroyed by ours.
struct Pair {
    Block<cWallDiffusionManager> s[2]{Block<cWallDiffusionManager>(1), Block<cWallDiffusionManager>(1)};
    Pair(uint32_t seed, const Shape &shape)
    {
        for (auto &st : s) {
            cWallDiffusionManager::Construct(st.bytes, nullptr);
            Fill(*st.Get(), seed, shape);
        }
    }
    ~Pair()
    {
        for (auto &st : s) st.Get()->~cWallDiffusionManager();
    }
    Pair(const Pair &) = delete;
    Pair &operator=(const Pair &) = delete;
    cWallDiffusionManager *operator[](int i) { return s[i].Get(); }
};

// ---- the world's level 0x7f2260 ---------------------------------------------------------------------------------------

// The stand-in's answer: a hash of the case's salt, the location's five words and the flag, logged (gLog of
// tests/sg_lighting_common.h) with the location's offset from gQueryBase (the case sets it, e.g. to a run's first segment).
// Row LD2h's cases over whole managers set gQueryWords (the location's five words logged instead of its offset: the
// runs' blocks lie apart differently on the two sides) and gQueryZeroLow (the answer's low 16 bits cleared when its bit
// 16 is set, so the callers' inside / outside test, the low half against 0, goes both ways).
// Static (one copy per test file), like gLog: an inline one was a single copy for the whole runner, logging into one
// file's gLog only (lane 261110d found LD2h's cases logging nothing).
inline uint32_t gQuerySalt = 0;
inline const void *gQueryBase = nullptr;
inline bool gQueryWords = false;
inline bool gQueryZeroLow = false;
[[maybe_unused]] static uint32_t __cdecl StandCellQuery(const uint32_t *location, uint32_t flag)
{
    uint32_t h = gQuerySalt ^ 0x7f2260u;
    for (int i = 0; i < 5; ++i) h = Mix(h, location[i]);
    h = Mix(h, flag);
    L(0x7f2260u);
    if (gQueryWords)
        for (int i = 0; i < 5; ++i) L(location[i]);
    else
        L(P(location) - P(gQueryBase));
    L(flag);
    if (gQueryZeroLow && (h & 0x10000u)) h &= 0xffff0000u;
    return h;
}

// Per test file (unnamed: it names this file's StandCellQuery).
namespace {

// The cases over whole managers: the world's level stood in, its locations logged by their words (the runs' blocks lie
// apart differently on the two sides), the answer's low half zeroed now and then (the inside test both ways).
struct QueryModes {
    shape::CodePatch level{0x7f2260u, shape::CodeOf(&StandCellQuery)};
    QueryModes() { gQueryWords = gQueryZeroLow = true; }
    ~QueryModes() { gQueryWords = gQueryZeroLow = false; }
    QueryModes(const QueryModes &) = delete;
    QueryModes &operator=(const QueryModes &) = delete;
};

// A fake interface object: its vtable an array the test fills with __fastcall fakes (this in ecx, edx unused).
struct FakeObject {
    void **vt;
};

} // namespace

// ---- row LD2h: packed colours, references, segment pointers -----------------------------------------------------------

// A colour channel for the packers (nTSSG::PackWallColor): the rounding points (k + 0.5) / 256 and k / 256 around 0 and
// 255, 1-2 ulps either side; 1, 255 / 256; the magic window's edges (|c| = 16384, the sum leaving [2^15, 2^16)); the
// sums just below -49152 (c just below -98304: the bits less the magic in 0x80000000..0x800000ff, where 255 less them
// overflows to 255); wide values, zeros, infinities, NaNs of both signs, denormals.
inline float Channel(std::mt19937 &gen)
{
    static const int kPoints[] = {-2, -1, 0, 1, 2, 126, 127, 128, 253, 254, 255, 256, 257};
    static const float kEdges[] = {16384.0f, -16384.0f, -32768.0f, -49152.0f, -65536.0f, -98304.0f, -98304.5f,
                                   -98304.99f, -98305.0f, 1.0f, 255.0f / 256.0f, 1.0f + 1.0f / 512.0f, 128.0f};
    auto ulps = [&](float f) {
        switch (gen() % 5) {
        case 0: return f;
        case 1: return std::nextafter(f, 1e30f);
        case 2: return std::nextafter(f, -1e30f);
        case 3: return std::nextafter(std::nextafter(f, 1e30f), 1e30f);
        default: return std::nextafter(std::nextafter(f, -1e30f), -1e30f);
        }
    };
    switch (gen() % 12) {
    case 0:
    case 1:
    case 2: return ulps((float(kPoints[gen() % std::size(kPoints)]) + 0.5f) / 256.0f);
    case 3: return ulps(float(kPoints[gen() % std::size(kPoints)]) / 256.0f);
    case 4: return ulps(kEdges[gen() % std::size(kEdges)]);
    case 5: return Wide(gen, 1e6f);
    case 6: return NaN(gen);
    case 7: return gen() % 2 ? 0.0f : -0.0f;
    case 8: return Fl(0x7f800000u | (gen() & 0x80000000u));
    default: return Nice(gen, 1.5f);
    }
}

inline void Channels(float *rgb, std::mt19937 &gen)
{
    for (int i = 0; i < 3; ++i) rgb[i] = Channel(gen);
}

// A reference no pass follows: {-1, -1, -1}, or a level word with the run or the segment (or both) negative.
inline nTSSG::WallSegmentRef InvalidRef(std::mt19937 &gen)
{
    const int32_t level = int32_t(gen() % 4);
    switch (gen() % 5) {
    case 0: return {-1, -1, -1};
    case 1: return {level, -1, int32_t(gen() % 3)};
    case 2: return {level, int32_t(gen() % 3), -1};
    case 3: return {level, int32_t(0x80000000u | gen()), int32_t(gen() % 3)};
    default: return {level, int32_t(gen() % 3), int32_t(0x80000000u | gen())};
    }
}

// Every segment's two references set again so the passes can follow them: a third invalid (InvalidRef), the rest a run
// that has segments and one of its segments (any run, or with `climbing` only later runs, so a walk ends; none later:
// invalid). With `edges`, the segment index is now and then the target's count (one past its last), and runs without
// segments are targets too (index 0: Neighbours' unsigned count - 1 bound, 0x410006 / 0x40ff1e, lets s + 1 through).
inline void ValidRefs(cWallDiffusionManager &m, std::mt19937 &gen, bool climbing, bool edges = false)
{
    const uint32_t runs = uint32_t(m.mRuns.size());
    for (uint32_t r = 0; r < runs; ++r)
        for (WallDiffusionSegment &s : m.mRuns[r].mSegments)
            for (nTSSG::WallSegmentRef *ref : {&s.mAbove, &s.mBelow}) {
                std::vector<uint32_t> targets;
                for (uint32_t t = climbing ? r + 1 : 0; t < runs; ++t)
                    if (edges || !m.mRuns[t].mSegments.empty()) targets.push_back(t);
                if (targets.empty() || gen() % 3 == 0) {
                    *ref = InvalidRef(gen);
                    continue;
                }
                const uint32_t t = targets[gen() % targets.size()];
                const uint32_t count = uint32_t(m.mRuns[t].mSegments.size());
                const uint32_t seg = edges && (count == 0 || gen() % 6 == 0) ? count : gen() % count;
                *ref = {int32_t(gen() % 4), int32_t(t), int32_t(seg)};
            }
}

// A segment pointer as {run, index} among the manager's runs (in range first, then -1..count), 0xffffffff for null,
// 0xbad for anything else: the same on both sides however the blocks lie.
inline uint32_t SegmentId(const cWallDiffusionManager &m, const WallDiffusionSegment *p)
{
    if (!p) return 0xffffffffu;
    const uint32_t runs = uint32_t(m.mRuns.size());
    for (int pass = 0; pass < 2; ++pass)
        for (uint32_t r = 0; r < runs; ++r) {
            const WallDiffusionRun &run = m.mRuns[r];
            const int32_t count = int32_t(run.mSegments.size());
            for (int32_t k = pass ? -1 : 0; k < (pass ? count + 1 : count); ++k)
                if (P(p) == P(run.mSegments.data()) + uint32_t(k) * sizeof(WallDiffusionSegment))
                    return r << 16 | (uint32_t(k) & 0xffffu);
        }
    return 0xbadu;
}

// SegmentId is ambiguous when one run's -1..count grid meets another's (two blocks a whole number of segments apart, a
// chance of the heap's layout, so different on the two sides): moves runs to new blocks until no two grids meet. The
// moved-from blocks stay allocated (`keep`) until the case ends, so a move lands elsewhere.
inline void SeparateRuns(cWallDiffusionManager &m, std::vector<std::vector<WallDiffusionSegment>> &keep)
{
    constexpr int64_t kSize = int64_t(sizeof(WallDiffusionSegment));
    for (int round = 0; round < 64; ++round) {
        bool moved = false;
        for (size_t a = 0; a < m.mRuns.size() && !moved; ++a)
            for (size_t b = a + 1; b < m.mRuns.size() && !moved; ++b) {
                const auto &sa = m.mRuns[a].mSegments, &sb = m.mRuns[b].mSegments;
                if (sa.empty() || sb.empty()) continue; // null blocks: their grids are the same on both sides
                const int64_t d = int64_t(P(sb.data())) - int64_t(P(sa.data()));
                if (d % kSize || d / kSize > int64_t(sa.size()) + 2 || -d / kSize > int64_t(sb.size()) + 2) continue;
                std::vector<WallDiffusionSegment> copy(sb.begin(), sb.end());
                m.mRuns[b].mSegments.swap(copy);
                keep.push_back(std::move(copy));
                moved = true;
            }
        if (!moved) return;
    }
}

// ---- rows LD2j and LD2f: lights ---------------------------------------------------------------------------------------

// A colour: Channels' rounding points and edges or plain light colours in [0, 1].
inline void Color(float *rgb, std::mt19937 &gen)
{
    if (gen() % 2) {
        Channels(rgb, gen);
        return;
    }
    for (int i = 0; i < 3; ++i) rgb[i] = float(gen() % 1001) / 1000.0f;
}

// A light record's fields Light 0x40c040 reads, drawn: the colour, the directional byte (Flag: 0, 1, 2, 0x80, 0xff), the
// angle (Angle's eight directions or any), the focus around its clamp's edges (below 0, 0, 0..1, 1, above, NaNs of their
// own payloads). The rest as it was (a Block's junk).
inline void FillLight(nTSSG::WallLight &l, std::mt19937 &gen)
{
    Color(l.mColor, gen);
    l.mDirectional = Flag(gen);
    Shape shape;
    shape.directions = gen() % 2;
    shape.nans = gen() % 4 == 0;
    l.mAngle = Angle(gen, shape);
    static const float kFocus[] = {-0.5f, -0.0f, 0.0f, 0.25f, 0.5f, 1.0f, 1.5f, 1e-30f};
    switch (gen() % 5) {
    case 0: l.mFocus = kFocus[gen() % std::size(kFocus)]; break;
    case 1: l.mFocus = NaN(gen); break;
    case 2: l.mFocus = Wide(gen, 2.0f); break;
    default: l.mFocus = float(gen() % 1001) / 1000.0f; break;
    }
}

// Light's spread (the falloff's distance and the pow base's 1 / (spread + 1)): 0, small positives, near -1, negatives,
// wide, NaNs of their own payloads, infinities.
inline float Spread(std::mt19937 &gen)
{
    switch (gen() % 8) {
    case 0: return gen() % 2 ? 0.0f : -0.0f;
    case 1: return std::nextafter(-1.0f, gen() % 2 ? 0.0f : -2.0f);
    case 2: return -float(gen() % 3000) / 1000.0f;
    case 3: return Wide(gen, 10.0f);
    case 4: return NaN(gen);
    default: return float(gen() % 4000) / 1000.0f;
    }
}

inline float Intensity(std::mt19937 &gen)
{
    switch (gen() % 6) {
    case 0: return Wide(gen, 4.0f);
    case 1: return NaN(gen);
    default: return float(gen() % 2001) / 1000.0f;
    }
}

} // namespace wallfx
