// Shared by the cFloorColorGrid tests (tests/sg_floor_color_grid.cpp, rows LD2a and LD2b; tests/sg_floor_color_grid_step.cpp,
// row LD2c): the game's heap patched to ours, a grid built alike on both sides from a seed, its dump and comparison, the
// colour and scale generators and the stopwatch's stand-in.
#pragma once

#include "code_patch.h"
#include "difftest.h"

#include "sg/cFloorColorGrid.h"
#include "sg/cLightMapManager.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <random>
#include <vector>

namespace floorfx {

using nTSSG::cFloorColorGrid;
using nTSSG::DiffusionTile;
using nTSSG::FloorColorCell;
using nTSSG::FloorColorLink;
using nTSSG::FloorBlob;
using nTSSG::FloorKey;
using nTSSG::FloorLightCell;
using nTSSG::FloorLightHalf;
using nTSSG::FloorUse;
using Blob = std::vector<uint32_t>;

inline void *__cdecl GameNew(size_t n) { return ::operator new(n); }
inline void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }
inline void __cdecl GameFree(void *p) { ::operator delete(p); }

struct Fixture {
    shape::CodePatch gameNew{0x824632u, shape::CodeOf(&GameNew)};
    shape::CodePatch gameDelete{0x824624u, shape::CodeOf(&GameDelete)};
    shape::CodePatch gameFree{0x82540fu, shape::CodeOf(&GameFree)};
};

struct Storage {
    alignas(8) uint8_t bytes[sizeof(cFloorColorGrid)];
    explicit Storage(uint32_t junk)
    {
        std::mt19937 g(junk);
        for (auto &b : bytes) b = uint8_t(g());
    }
    cFloorColorGrid *Get() { return reinterpret_cast<cFloorColorGrid *>(bytes); }
};

inline void RandomBytes(void *p, size_t n, std::mt19937 &gen)
{
    auto *b = static_cast<uint8_t *>(p);
    for (size_t i = 0; i < n; ++i) b[i] = uint8_t(gen());
}

inline float NaNOf(uint32_t h) { return std::bit_cast<float>(0x7f800000u | (h & 0x80000000u) | ((h >> 3) & 0x400000u) | (1 + h % 0x3fffffu)); }

// A float: zeros of both signs, small integers, full significands, denormals, infinities and NaNs of both kinds.
inline float Nice(std::mt19937 &gen)
{
    switch (gen() % 12) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return float(int(gen() % 9) - 4);
    case 3: return NaNOf(gen());
    case 4: return std::bit_cast<float>(gen() & 0x807fffffu); // denormal
    case 5: return gen() % 2 ? std::numeric_limits<float>::infinity() : -std::numeric_limits<float>::infinity();
    case 6: return std::bit_cast<float>((gen() & 0x807fffffu) | ((100u + gen() % 50) << 23));
    default: return std::bit_cast<float>((gen() & 0x807fffffu) | ((120u + gen() % 14) << 23));
    }
}

inline uint8_t Flag(std::mt19937 &gen)
{
    static const uint8_t kFlags[] = {0, 0, 1, 1, 2, 0x80, 0xff};
    return kFlags[gen() % std::size(kFlags)];
}

inline int16_t LevelKey(std::mt19937 &gen, int16_t key)
{
    static const int16_t kKeys[] = {0, 1, -1, 2, 0x7fff, -0x8000, 3};
    if (gen() % 2) return key;
    return gen() % 4 ? kKeys[gen() % std::size(kKeys)] : int16_t(gen());
}

template <class V> void Reserve(V &v, std::mt19937 &gen)
{
    if (gen() % 3 == 0) v.reserve(v.size() + 1 + gen() % 4);
}

// What a case builds: `markable` keeps the tiles at least 1 x 1 and every link's cells inside the cell vector.
struct Shape {
    bool markable = false;
    int16_t key = 0; // the level Mark is asked about: about half the samples and set entries carry it
    bool nans = false; // half the samples' colours NaNs (Mark's sums: which operand's payload survives)
    // LD2b: a grid the light passes can walk: 1..5 tiles a side, exactly four cells a tile (the 2w x 2h sub-cell grid),
    // a link a tile with its cells in range and kinds 0, 1, 2 or others, no m18 bit pointing out of the sub-cell grid,
    // and every light cell's tile inside the grid.
    bool lightable = false;
};

// The sub-cell grid's directions (as the class's kDirX / kDirY: 0 up, then counter-clockwise).
constexpr int32_t kDirX[8] = {0, -1, -1, -1, 0, 1, 1, 1};
constexpr int32_t kDirY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

// The same grid on both sides from `seed`: on a constructed grid (ours), every scalar random, then the vectors and set.
inline void Fill(cFloorColorGrid &g, uint32_t seed, const Shape &shape)
{
    std::mt19937 gen(seed);
    auto *words = reinterpret_cast<uint32_t *>(&g);
    for (int i = 1; i < 0x40 / 4; ++i) words[i] = gen() % 4 ? std::bit_cast<uint32_t>(Nice(gen)) : gen();
    words[0x40 / 4] = gen() % 2 ? 1 : gen();
    g.mLevel = int32_t(gen() % 7) - 3;
    const bool sized = shape.markable || shape.lightable;
    g.mWidth = sized ? 1 + gen() % 5 : gen() % 6;
    g.mHeight = sized ? 1 + gen() % 5 : gen() % 6;
    const uint32_t tiles = g.mWidth * g.mHeight;

    g.mCells.resize(shape.lightable ? tiles * 4 : gen() % 5 == 0 ? gen() % (tiles * 4 + 3) : tiles * 4);
    if (shape.markable && g.mCells.empty()) g.mCells.resize(1);
    Reserve(g.mCells, gen);
    for (FloorColorCell &c : g.mCells) {
        RandomBytes(&c, sizeof c, gen);
        for (auto &s : c.mSamples) {
            for (float &f : s.mColor) f = shape.nans && gen() % 2 ? NaNOf(gen()) : Nice(gen);
            s.mLit = Flag(gen);
            s.mLevel = LevelKey(gen, shape.key);
        }
    }

    if (shape.lightable) {
        const int32_t w2 = int32_t(g.mWidth * 2), h2 = int32_t(g.mHeight * 2);
        for (int32_t cy = 0; cy < h2; ++cy)
            for (int32_t cx = 0; cx < w2; ++cx)
                for (auto &s : g.mCells[uint32_t(cx + cy * w2)].mSamples) {
                    s.m1a = gen() % 2 ? 0 : Flag(gen);
                    for (uint32_t d = 0; d < 8; ++d)
                        if (cx + kDirX[d] < 0 || cx + kDirX[d] >= w2 || cy + kDirY[d] < 0 || cy + kDirY[d] >= h2)
                            s.m18 &= uint8_t(~(1u << d));
                }
    }

    g.mLinks.resize(sized ? tiles + gen() % 2 : (gen() % 5 == 0 ? gen() % (tiles + 3) : tiles));
    Reserve(g.mLinks, gen);
    for (FloorColorLink &l : g.mLinks) {
        RandomBytes(&l, sizeof l, gen);
        if (sized)
            for (uint32_t &c : l.mCells) c = gen() % uint32_t(g.mCells.size());
        if (shape.lightable) {
            static const uint8_t kKinds[] = {0, 0, 1, 2, 3, 0xff};
            const uint8_t kind = gen() % 8 ? kKinds[gen() % std::size(kKinds)] : uint8_t(gen());
            l.m0 = int16_t(uint16_t(kind << 8 | (gen() & 0xff)));
        }
    }

    g.m68 = int32_t(gen());
    g.mLights.resize(gen() % 7);
    Reserve(g.mLights, gen);
    for (FloorLightCell &l : g.mLights) {
        RandomBytes(&l, sizeof l, gen);
        for (auto &h : l.mHalves) {
            h.mSave = Flag(gen);
            for (auto &v : h.mCurrent) v = {Nice(gen), Nice(gen), Nice(gen)};
        }
        if (shape.lightable) {
            for (auto &h : l.mHalves)
                for (auto &v : h.mCurrent)
                    if (gen() % 3 == 0) v = {NaNOf(gen()), Nice(gen), NaNOf(gen())};
            l.mX = int32_t(gen() % g.mWidth);
            l.mY = int32_t(gen() % g.mHeight);
        } else {
            l.mX = gen() % 8 == 0 ? int32_t(gen()) : int32_t(gen() % (g.mWidth + 5)) - 2;
            l.mY = gen() % 8 == 0 ? int32_t(gen()) : int32_t(gen() % (g.mHeight + 5)) - 2;
        }
        l.mActive = Flag(gen);
        l.mFlagA = Flag(gen);
        l.mFlagB = Flag(gen);
    }

    const uint32_t levels = gen() % 6;
    for (uint32_t i = 0; i < levels; ++i) g.mLevels.insert(LevelKey(gen, shape.key));
    g.mSteps = int32_t(gen());
}

// The level set's tree, as MSVC's _Tree_node<short> (the image's and ours alike).
struct Node {
    Node *left, *parent, *right;
    uint8_t color, isnil;
    int16_t value;
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
    b.push_back(uint32_t(uint16_t(n->value)) | uint32_t(n->color) << 16 | uint32_t(n->isnil) << 24);
    Walk(b, n->left, depth + 1);
    Walk(b, n->right, depth + 1);
}

template <class V> void Vector(Blob &b, const V &v)
{
    b.push_back(uint32_t(v.size()));
    b.push_back(uint32_t(v.capacity()));
    const auto *w = reinterpret_cast<const uint32_t *>(v.data());
    b.insert(b.end(), w, w + v.size() * sizeof(*v.data()) / 4);
}

inline Blob Dump(const cFloorColorGrid &g)
{
    Blob b;
    const auto *w = reinterpret_cast<const uint32_t *>(&g);
    b.insert(b.end(), w, w + 0x50 / 4);
    Vector(b, g.mCells);
    // The links' padding (+2, +5..+7): the constructor 0x4058b0 writes the fields alone, so a new link's padding is
    // whatever the block held.
    b.push_back(uint32_t(g.mLinks.size()));
    b.push_back(uint32_t(g.mLinks.capacity()));
    for (const FloorColorLink &l : g.mLinks) {
        b.push_back(uint16_t(l.m0));
        b.push_back(l.m4);
        b.insert(b.end(), l.mCells, l.mCells + 4);
    }
    b.push_back(uint32_t(g.m68));
    Vector(b, g.mLights);
    const Node *head = *reinterpret_cast<const Node *const *>(&g.mLevels);
    b.push_back(uint32_t(g.mLevels.size()));
    b.push_back(head->isnil);
    b.push_back(head->left == head ? 0xeeee : uint16_t(head->left->value));
    b.push_back(head->right == head ? 0xeeee : uint16_t(head->right->value));
    Walk(b, head->parent, 0);
    b.push_back(uint32_t(g.mSteps));
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

// Two grids built alike (constructed by ours, then filled from the seed), destroyed by ours.
struct Pair {
    Storage s[2]{Storage(1), Storage(1)};
    Pair(uint32_t seed, const Shape &shape)
    {
        for (auto &st : s) {
            cFloorColorGrid::Construct(st.bytes, nullptr);
            Fill(*st.Get(), seed, shape);
        }
    }
    ~Pair()
    {
        for (auto &st : s) cFloorColorGrid::Destroy(st.Get(), nullptr);
    }
    cFloorColorGrid *operator[](int i) { return s[i].Get(); }
};

// A colour channel: mostly the light map's range, sometimes any float, sometimes (with `nans`) a NaN of its own.
inline float Colour(std::mt19937 &gen, bool nans)
{
    const uint32_t r = gen() % 8;
    if (nans && r < 3) return NaNOf(gen());
    if (r < 5) return float(gen() % 4096) / 2048.0f;
    return Nice(gen);
}

inline void FillHalf(FloorLightHalf &h, std::mt19937 &gen, bool nans)
{
    RandomBytes(&h, sizeof h, gen);
    for (auto &v : h.mCurrent) v = {Colour(gen, nans), Colour(gen, nans), Colour(gen, nans)};
}

// A byte argument: `low` in the low byte, junk above it.
inline uint32_t ByteArg(std::mt19937 &gen, uint8_t low) { return (gen() & 0xffffff00u) | low; }

inline float Scale(std::mt19937 &gen)
{
    switch (gen() % 6) {
    case 0: return NaNOf(gen());
    case 1: return Nice(gen);
    default: return float(1 + gen() % 400) / 256.0f;
    }
}

// The grid's m40 (whether the outside ambient is taken off; Fill leaves it mostly non-zero) and, with `nans`, some of
// its ambient colours and factors NaNs of their own payloads, alike on both sides.
inline void Ambients(cFloorColorGrid *a, cFloorColorGrid *b, std::mt19937 &gen, bool nans)
{
    a->m40 = b->m40 = gen() % 2 ? 0 : Flag(gen);
    if (!nans) return;
    float *const fa[] = {a->mInsideAmbientColor, a->mInsideAmbientColor + 1, a->mInsideAmbientColor + 2,
                         a->mOutsideAmbientColor, a->mOutsideAmbientColor + 1, a->mOutsideAmbientColor + 2,
                         &a->mInsideAmbient, &a->mOutsideAmbient};
    float *const fb[] = {b->mInsideAmbientColor, b->mInsideAmbientColor + 1, b->mInsideAmbientColor + 2,
                         b->mOutsideAmbientColor, b->mOutsideAmbientColor + 1, b->mOutsideAmbientColor + 2,
                         &b->mInsideAmbient, &b->mOutsideAmbient};
    for (size_t i = 0; i < std::size(fa); ++i)
        if (gen() % 3 == 0) *fa[i] = *fb[i] = NaNOf(gen());
}

// The stopwatch's Restart 0x1c95ec stood in (counted).
inline int gRestarts = 0;
inline void __fastcall StandRestart(void *, void *) { ++gRestarts; }

} // namespace floorfx
