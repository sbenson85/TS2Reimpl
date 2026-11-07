#pragma once
// A fake scene of shape instances under a renderable, for code that drives them each frame (lane XB, for
// cTSParticlesRenderable::UpdateParticleChildren 0xbc1d10 and cTSMetaParticlesEffect's draw tick 0xc03ea0). Sizes and behaviour
// come from the real child, cShapeRefNode (src/sg/cShapeRefNode.cpp): slot 14 answers a parameter id with the 24-byte record
// whose +8 is the material's list length, and slot 7 reads only the floats that id's list holds (one to four, after the event
// object's first word). Every call goes to the shared log `gEv`, floats as bits, so that comparing logs compares them bit for bit.
// fakes: cICompositionTreeNode, cITransformNode, nGZEvent::cIAbstractBehavior
//
// Use: `ChildNodes children;` for the test (it copies gFakeNodeVT, so make it after the test's overrides of that table),
// `children.Reset()` for each run, `children.Make(id)` for a node (one reference, as the factory hands it out), `Link(...)`
// after any change to the sibling order. Nodes are never freed: the pool's slots stay valid for the run.
#include "ts_effects_collection.h"

#include "gz/cGZEventReflector.h"
#include "sg/SGNodeInterfaces.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

namespace childnodes {

using fxtest::Ev;
using fxtest::FakeNode;

// The four parameter ids, in the order of the material's float lists: a list of index k holds k + 1 floats per entry.
inline constexpr uint32_t kParameterIds[4] = {0x09f5fdb0, 0x09f5fdb6, 0x09f5fdbc, 0x09f5fdc0};
inline int ParameterList(uint32_t id)
{
    for (int k = 0; k < 4; ++k)
        if (id == kParameterIds[k]) return k;
    return -1;
}

enum : uint32_t {
    evChildSet = 0xb1000000,       // id, parameter, the event's first word, then the list's floats
    evChildAttribute = 0xb2000000, // id
    evChildNext = 0xb3000000,
    evChildChildren = 0xb4000000,
    evChildFlags = 0xb5000000,
    evChildWorld = 0xb6000000,
    evChildTransform = 0xb7000000, // the seven floats
};

// What the runs reached, by list (slot 7), and slot 63's calls: for a test's floors.
inline uint32_t gParameterSets[4];
inline uint32_t gTransformSets;

struct ChildNode : FakeNode {
    ChildNode *next;       // slot 38's answer
    uint32_t lists[4];     // slot 14's answer: the material's list lengths
    float world[7];        // slot 54's answer
    float transform[7];    // the last slot 63
    uint32_t transformsSet;
};

inline uint32_t __fastcall ChildGetFlags(ChildNode *n, void *)
{
    Ev({evChildFlags, n->id});
    return 0;
}
inline ChildNode *__fastcall ChildGetNext(ChildNode *n, void *)
{
    Ev({evChildNext, n->id});
    return n->next;
}
// No children of its own. The first word of the range is what the walkers test.
inline uint32_t *__fastcall ChildGetChildren(ChildNode *n, void *, uint32_t *out)
{
    Ev({evChildChildren, n->id});
    out[0] = out[1] = out[2] = 0;
    return out;
}
inline float *__fastcall ChildGetWorld(ChildNode *n, void *, float *out)
{
    Ev({evChildWorld, n->id});
    std::memcpy(out, n->world, sizeof(n->world));
    return out;
}
inline void __fastcall ChildSetTransform(ChildNode *n, void *, const float *transform)
{
    Ev({evChildTransform, n->id});
    for (int i = 0; i < 7; ++i) Ev({std::bit_cast<uint32_t>(transform[i])});
    std::memcpy(n->transform, transform, sizeof(n->transform));
    ++n->transformsSet;
    ++gTransformSets;
}
// The real slot 14 hands back the registry's record with the list's length at +8; an id it does not know reads as zeros.
inline nGZEvent::cEventAttribute *__fastcall ChildGetAttribute(ChildNode *n, void *, nGZEvent::cEventAttribute *out, uint32_t id)
{
    Ev({evChildAttribute, n->id, id});
    std::memset(out, 0, sizeof(*out));
    const int list = ParameterList(id);
    if (list >= 0) {
        out->mId = id;
        out->mWord08 = n->lists[list];
    }
    return out;
}
inline void __fastcall ChildSetAttribute(ChildNode *n, void *, uint32_t id, uint32_t parameter, const uint32_t *event)
{
    Ev({evChildSet, n->id, id, parameter, event[0]});
    const int list = ParameterList(id);
    for (int i = 0; i <= list; ++i) Ev({event[1 + i]});
    if (list >= 0) ++gParameterSets[list];
}

class ChildNodes {
public:
    ChildNodes()
    {
        std::memcpy(mTable, fxtest::gFakeNodeVT, sizeof(mTable));
        Set(7, &ChildSetAttribute);
        Set(14, &ChildGetAttribute);
        Set(38, &ChildGetNext);
        Set(42, &ChildGetChildren);
        Set(49, &ChildGetFlags);
        Set(54, &ChildGetWorld);
        Set(63, &ChildSetTransform);
    }
    void Reset() { mUsed = 0; }
    ChildNode *Make(uint32_t id, const uint32_t (&lists)[4] = kDefaultLists)
    {
        if (mUsed == kCapacity) mUsed = 0; // a run that long has let go of the oldest
        ChildNode *const n = &mPool[mUsed++];
        std::memset(n, 0, sizeof(*n));
        n->vt = mTable;
        n->id = id;
        n->refs = 1;
        std::memcpy(n->lists, lists, sizeof(n->lists));
        return n;
    }
    bool Owns(const void *node) const { return node >= mPool && node < mPool + kCapacity; }
    // The sibling order; the last one's next is `end` (null: the list simply stops).
    template <class T> static void Link(const std::vector<T *> &order, ChildNode *end = nullptr)
    {
        for (size_t i = 0; i < order.size(); ++i)
            static_cast<ChildNode *>(static_cast<FakeNode *>(order[i]))->next =
                i + 1 < order.size() ? static_cast<ChildNode *>(static_cast<FakeNode *>(order[i + 1])) : end;
    }

    static constexpr uint32_t kDefaultLists[4] = {8, 8, 8, 8};

private:
    template <class F> void Set(unsigned slot, F f) { mTable[slot] = reinterpret_cast<void *>(f); }
    static constexpr uint32_t kCapacity = 8192;
    void *mTable[fxtest::kFakeNodeSlots];
    ChildNode mPool[kCapacity];
    uint32_t mUsed = 0;
};

} // namespace childnodes
