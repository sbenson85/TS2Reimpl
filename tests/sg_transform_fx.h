#pragma once
// The world of the transform-family Impl difftests (lane OB, scenegraph N0 part 3): cTransformNodeImpl (0x3c bytes),
// cBoundedNodeImpl (0x60) and cExplicitBoundNodeImpl (0x64) blocks, with the outer node and the block's parent as
// state-backed sgfx fakes (flag words the tree code's propagation reads and writes), the string pool, a persist reader
// and writer. Each side runs the original at its RVA or our member on the same block; the calls (the propagation's
// included: the tree code 0xa39690 / 0xa39ae0 is original code on both sides), the block, every fake's state and the
// pool's counts are compared.
//
// What the world leaves out: the propagation's second argument (the node's slot 32) is always null and no fake has flag 6
// set, so the propagation's calls on the scene side (0xa39920's slot 95 on that argument and slot 77 under flag 6) never
// run: nothing in the Impl members reaches them. Flag 12 (the cached world transform is stale) is clear in the blocks
// unless a test sets it: with it set the bounded members call the tree code's refresh (0xa39b70), which needs real nodes
// (the pilot tests).
#include "difftest.h"
#include "sgfx.h"

#include <bit>
#include <random>

namespace sgtx {

using drawfx::Fake;
using drawfx::P;

constexpr uint32_t kBlockTag = 0x7b000000;
constexpr unsigned kSeeds = 96;

inline float RandomFloat(std::mt19937 &rng) { return float(int(rng() % 8001) - 4000) / 1000.0f; }

inline void NoteFloats(const char *label, const float *p, unsigned n)
{
    drawfx::Call c;
    c.note = label;
    for (unsigned i = 0; i < n; ++i) c.words.push_back(std::bit_cast<uint32_t>(p[i]));
    drawfx::gLog.push_back(std::move(c));
}

inline nGZSceneGraph::cRigidTransform RandomRigid(std::mt19937 &rng)
{
    nGZSceneGraph::cRigidTransform r;
    for (float &v : r.mValues) v = RandomFloat(rng);
    return r;
}
inline nGZSceneGraph::cTransform34 RandomFrame(std::mt19937 &rng)
{
    nGZSceneGraph::cTransform34 t;
    float *f = reinterpret_cast<float *>(&t);
    for (unsigned i = 0; i < sizeof(t) / 4; ++i) f[i] = RandomFloat(rng);
    return t;
}

// A block of Words words. Words 0-14 are the transform node's Impl (extension, tag handle, parent, previous and next
// sibling, first child, flags, limit, translation, rotation); a bounded block adds the holder, its data and the cached
// world transform (words 15-23), an explicit-bound block the shape (word 24).
template <unsigned Words> struct World {
    static constexpr unsigned kWords = Words;
    Fake outer, parent, shape;
    alignas(8) uint32_t block[Words];

    nGZSceneGraph::cICompositionTreeNode *Node(Fake &f) { return reinterpret_cast<nGZSceneGraph::cICompositionTreeNode *>(&f); }
    nGZSceneGraph::cICompositionTreeNode *Outer() { return Node(outer); }
    template <class T> T *Impl() { return reinterpret_cast<T *>(block); }
    template <class T> T *OuterAs() { return reinterpret_cast<T *>(&outer); }

    void Build(uint32_t seed, drawfx::Cells &cells)
    {
        std::mt19937 rng(seed);
        sgfx::gPool.Reset(cells);
        sgfx::gReader.Reset();
        sgfx::gWriter.Reset();
        sgfx::MakeFlagNode<sgfx::kExplicitBoundNode>(outer, 100);
        sgfx::MakeFlagNode<sgfx::kExplicitBoundNode>(parent, 120);
        shape.Reset<sgfx::kUnknown>(130);
        const uint32_t limits[] = {0x7fffffff, 0, 0x7fffffff, uint32_t(rng()), 5};
        // The fakes' flags and limits (the propagation reads them; flag 6 stays clear, see the top).
        outer.value[sgfx::kFlagsWord] = uint32_t(rng()) & ~0x40u;
        parent.value[sgfx::kFlagsWord] = uint32_t(rng()) & ~0x40u;
        outer.value[sgfx::kLimitWord] = limits[rng() % 5];
        parent.value[sgfx::kLimitWord] = limits[rng() % 5];
        block[0] = 0;
        block[1] = sgfx::kNoHandle;
        if (rng() % 2) {
            block[1] = sgfx::gPool.Handle("alpha");
            ++sgfx::gPool.counts[0];
        }
        block[2] = rng() % 4 != 0 ? P(&parent) : 0;
        outer.value[sgfx::kParentWord] = block[2];
        block[3] = block[4] = block[5] = 0;
        block[6] = uint32_t(rng()) & ~0x1000u; // (flag 12 clear)
        block[7] = limits[rng() % 5];
        for (unsigned i = 8; i < 15; ++i) block[i] = std::bit_cast<uint32_t>(RandomFloat(rng));
        if constexpr (Words > 15) {
            block[15] = rng() % 2 ? 0 : 0x10000u + (rng() & 0xfffc);
            block[16] = uint32_t(rng());
            for (unsigned i = 17; i < 24; ++i) block[i] = std::bit_cast<uint32_t>(RandomFloat(rng));
        }
        if constexpr (Words > 24) {
            block[24] = rng() % 2 ? P(&shape) : 0;
            if (block[24]) ++shape.refs; // (the block's own count)
        }
        drawfx::AddRange(block, sizeof(block), kBlockTag);
    }

    void NoteWorld()
    {
        drawfx::Call c;
        c.note = "impl";
        // By kind (lane RK): the extension, the links and the shape are pointers; the handle, flags, limit, floats,
        // the holder (a test value, never a pointer here) and its data are data, as they are. A renderable block's words
        // from 24 on (World<28>: the renderable node's own, not the shape) are not declared yet: the legacy guess.
        constexpr unsigned kDeclared = Words == 25 ? 25 : Words < 24 ? Words : 24;
        drawfx::Words(c.words, block, kDeclared, "auaaaauuuuuuuuuuuuuuuuuua");
        for (unsigned i = kDeclared; i < Words; ++i) c.words.push_back(drawfx::Norm(block[i]));
        drawfx::gLog.push_back(std::move(c));
        for (Fake *f : {&outer, &parent})
            drawfx::Note("fake", {f->id, uint32_t(f->refs), f->value[sgfx::kFlagsWord], f->value[sgfx::kLimitWord]});
        drawfx::Note("shape", {shape.id, uint32_t(shape.refs)});
        sgfx::gPool.NoteCounts();
        sgfx::gReader.NoteNames();
    }

    template <class Run> void Check(difftest::Context &ctx, const char *what, Run run, unsigned seeds = kSeeds)
    {
        sgfx::CheckSides(
            ctx, what, seeds, [this](uint32_t seed, drawfx::Cells &cells) { Build(seed, cells); },
            [&](bool original, std::mt19937 &rng) {
                run(original, rng);
                NoteWorld();
            });
    }
};

} // namespace sgtx
