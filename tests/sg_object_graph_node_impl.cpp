// Differential tests of nGZSceneGraph::cObjectGraphNodeImpl, the node notifications and cSubordinateListChanged (lane
// NZ, scenegraph N0). Each member runs on an 8-byte Impl block (the list head, the tag handle) in a world of sgfx fakes:
// an outer node whose extension list is the block's (its FindExtension walks it), five extensions and a tag extension
// (cITagExtension face), the string pool in its cell, a persist reader and writer. The original runs at its RVA, ours
// as the member; each side's calls, the block, every fake's links and counts and the pool's handle counts are compared.
#include "difftest.h"
#include "sgfx.h"

#include "sg/cObjectGraphNodeImpl.h"

#include <algorithm>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;

constexpr unsigned kExt = 5;             // plain extensions, ids 200-204
constexpr unsigned kTagIndex = kExt;     // the tag extension (id 205), as an index into the list
constexpr uint32_t kOtherFace = 0x7777;  // an interface some extensions answer (themselves)
constexpr uint32_t kBlockTag = 0x7b000000;
constexpr unsigned kSeeds = 96;

const char *const kTexts[] = {"alpha", "beta", "", "a tag longer than sixteen"};

struct World {
    Fake outer, target, subordinate;
    Fake ext[kExt];
    Fake tagExt, tagFaceFake;
    sgfx::TagFace tagFace;
    Fake context;
    alignas(8) uint32_t block[2];
    uint32_t list[kExt + 1];
    unsigned length;
};
World w;

Fake &Ext(unsigned i) { return i == kTagIndex ? w.tagExt : w.ext[i]; }
cObjectGraphNodeImpl *Impl() { return reinterpret_cast<cObjectGraphNodeImpl *>(w.block); }
cIObjectGraphNode *Node(Fake &f) { return reinterpret_cast<cIObjectGraphNode *>(&f); }
cIExtension *AsExtension(Fake &f) { return reinterpret_cast<cIExtension *>(&f); }

// The world for `seed`: a random list (the tag extension in it or not), a tag handle or none, persistence, faces and
// clones at random.
void Build(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 rng(seed);
    sgfx::gPool.Reset(cells);
    sgfx::gReader.Reset();
    sgfx::gWriter.Reset();
    sgfx::MakeNode(w.outer, 100);
    sgfx::MakeNode(w.target, 101);
    sgfx::MakeNode(w.subordinate, 102);
    for (unsigned i = 0; i < kExt; ++i) {
        sgfx::MakeExtension(w.ext[i], 200 + i, rng() % 3 != 0);
        if (rng() % 2) w.ext[i].AddFace(kOtherFace, w.ext[i]);
    }
    sgfx::MakeTagExtension(w.tagExt, 200 + kExt, w.tagFaceFake, 300, w.tagFace, kTexts[rng() % 4]);
    w.tagExt.value[sgfx::kExtPersistentWord] = rng() % 2;
    // Clones: another extension (uncounted, as Clone returns) or none.
    for (unsigned i = 0; i <= kExt; ++i) {
        const unsigned c = rng() % (kExt + 2);
        Ext(i).value[sgfx::kExtCloneWord] = c < kExt ? P(&w.ext[c]) : 0;
    }
    // The list: a random selection in random order.
    unsigned order[kExt + 1];
    for (unsigned i = 0; i <= kExt; ++i) order[i] = i;
    std::shuffle(order, order + kExt + 1, rng);
    w.length = rng() % (kExt + 2);
    for (unsigned i = 0; i < w.length; ++i) w.list[i] = order[i];
    for (unsigned i = 0; i < w.length; ++i) {
        Fake &e = Ext(w.list[i]);
        e.value[sgfx::kExtNextWord] = i + 1 < w.length ? P(&Ext(w.list[i + 1])) : 0;
        e.value[sgfx::kExtOwnerWord] = P(&w.outer);
    }
    w.block[0] = w.length ? P(&Ext(w.list[0])) : 0;
    w.outer.value[sgfx::kFirstExtensionWord] = w.block[0];
    // The tag: the pool's "alpha" held once by the block, or none.
    const uint32_t alpha = sgfx::gPool.Handle("alpha");
    sgfx::gPool.Handle("beta");
    if (rng() % 2) {
        w.block[1] = alpha;
        ++sgfx::gPool.counts[0];
    } else {
        w.block[1] = sgfx::kNoHandle;
    }
    drawfx::AddRange(w.block, sizeof(w.block), kBlockTag);
}

void NoteWorld()
{
    drawfx::Note("impl", {drawfx::Norm(w.block[0]), w.block[1]});
    for (unsigned i = 0; i <= kExt; ++i) {
        const Fake &e = Ext(i);
        drawfx::Note("extension", {e.id, uint32_t(e.refs), drawfx::Norm(e.value[sgfx::kExtNextWord]),
                                   drawfx::Norm(e.value[sgfx::kExtOwnerWord])});
    }
    drawfx::Note("nodes", {uint32_t(w.outer.refs), uint32_t(w.target.refs), uint32_t(w.subordinate.refs),
                           uint32_t(w.tagFaceFake.refs)});
    sgfx::gPool.NoteCounts();
    sgfx::gReader.NoteNames();
}

using sgfx::NoteText;

// Each seed: the world built twice, the original run on one and ours on the other; the logs compared.
template <class Run> void Check(difftest::Context &ctx, const char *what, Run run, unsigned seeds = kSeeds)
{
    sgfx::CheckSides(ctx, what, seeds, Build, [&](bool original, std::mt19937 &rng) {
        run(original, rng);
        NoteWorld();
    });
}

// An extension at random: in the list, outside it, or (for AddExtension) the first.
Fake &AnyExtension(std::mt19937 &rng) { return Ext(rng() % (kExt + 1)); }

} // namespace


DIFF_TEST(SG_ObjectGraphNodeImpl_Destruct, 0xa342a0)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0xa342a0);
    Check(ctx, "destructor", [&](bool original, std::mt19937 &) {
        if (original)
            orig(w.block);
        else
            Impl()->Destruct();
    });
}

DIFF_TEST(SG_ObjectGraphNodeImpl_AddExtension, 0xa34530)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0xa34530);
    Check(ctx, "AddExtension", [&](bool original, std::mt19937 &rng) {
        Fake &e = rng() % 4 == 0 && w.length ? Ext(w.list[0]) : AnyExtension(rng);
        if (original)
            orig(w.block, &w.outer, &e);
        else
            Impl()->AddExtension(Node(w.outer), AsExtension(e));
    });
}

DIFF_TEST(SG_ObjectGraphNodeImpl_HasExtension, 0xa34570)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *, void *)>(0xa34570);
    Check(ctx, "HasExtension", [&](bool original, std::mt19937 &rng) {
        Fake &e = AnyExtension(rng);
        const bool r = original ? orig(w.block, &e) : Impl()->HasExtension(AsExtension(e));
        drawfx::Note("result", uint8_t(r));
    });
}

DIFF_TEST(SG_ObjectGraphNodeImpl_FindExtension, 0xa345a0)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t, void **, void *)>(0xa345a0);
    Check(ctx, "FindExtension", [&](bool original, std::mt19937 &rng) {
        const uint32_t iids[] = {sgfx::kIidTag, kOtherFace, 0x12345678};
        const uint32_t iid = iids[rng() % 3];
        Fake *start = rng() % 3 == 0 && w.length ? &Ext(w.list[rng() % w.length]) : nullptr;
        void *out = nullptr;
        const bool r = original ? orig(w.block, iid, &out, start)
                                : Impl()->FindExtension(iid, &out, start ? AsExtension(*start) : nullptr);
        drawfx::Note("result", {uint8_t(r), drawfx::Norm(P(out))});
    });
}

DIFF_TEST(SG_ObjectGraphNodeImpl_Read, 0xa345f0)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0xa345f0);
    Check(ctx, "Read", [&](bool original, std::mt19937 &rng) {
        const uint32_t versions[] = {0, 3, 4, 5};
        sgfx::Reader &r = sgfx::gReader;
        r.Version(versions[rng() % 4]);
        const uint32_t count = rng() % 4;
        r.Word(count);
        for (uint32_t i = 0; i < count; ++i) r.Ref(rng() % 5 ? static_cast<void *>(&AnyExtension(rng)) : nullptr);
        r.Text(kTexts[rng() % 4]);
        if (original)
            orig(w.block, &w.outer, &r.fake);
        else
            Impl()->Read(Node(w.outer), reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
    });
}

DIFF_TEST(SG_ObjectGraphNodeImpl_RemoveExtension, 0xa34870)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *)>(0xa34870);
    Check(ctx, "RemoveExtension", [&](bool original, std::mt19937 &rng) {
        if (!w.length) return; // (an extension not in the list ends in a null call in both)
        Fake &e = Ext(w.list[rng() % w.length]);
        if (original)
            orig(w.block, &e);
        else
            Impl()->RemoveExtension(AsExtension(e));
    });
}

DIFF_TEST(SG_ObjectGraphNodeImpl_SetTagHandle, 0xa34900)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, uint32_t)>(0xa34900);
    Check(ctx, "SetTagHandle", [&](bool original, std::mt19937 &rng) {
        const uint32_t handles[] = {sgfx::kNoHandle, sgfx::Pool::kFirstHandle, sgfx::Pool::kFirstHandle + 1};
        const uint32_t h = handles[rng() % 3];
        if (original)
            orig(w.block, &w.outer, h);
        else
            Impl()->SetTagHandle(Node(w.outer), h);
    });
}

DIFF_TEST(SG_ObjectGraphNodeImpl_SetTag, 0xa349d0)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, const char *)>(0xa349d0);
    Check(ctx, "SetTag", [&](bool original, std::mt19937 &rng) {
        const unsigned pick = rng() % 5;
        const char *text = pick < 4 ? kTexts[pick] : nullptr;
        if (original)
            orig(w.block, &w.outer, text);
        else
            Impl()->SetTag(Node(w.outer), text);
    });
}

DIFF_TEST(SG_ObjectGraphNodeImpl_IsKindOf, 0xa34ac0)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t)>(0xa34ac0);
    Check(
        ctx, "IsKindOf",
        [&](bool original, std::mt19937 &rng) {
            const uint32_t iids[] = {0x5aef607b, 0x3b253dc7, 0xbb11a53e, 0x5b0aaa74, 1,         0x4512dd13,
                                     0,          0xfb859324, 0xfb06dba0, 2,          uint32_t(rng())};
            for (uint32_t iid : iids)
                drawfx::Note("kind", {iid, uint8_t(original ? orig(w.block, iid) : Impl()->IsKindOf(iid))});
        },
        4);
}

DIFF_TEST(SG_ObjectGraphNodeImpl_GetTagHandle, 0xa34af0)
{
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0xa34af0);
    Check(ctx, "GetTagHandle", [&](bool original, std::mt19937 &) {
        drawfx::Note("handle", original ? orig(w.block) : Impl()->GetTagHandle());
    });
}

DIFF_TEST(SG_ObjectGraphNodeImpl_GetTag, 0xa34bb0)
{
    auto orig = ctx.Original<const char *(__thiscall *)(void *)>(0xa34bb0);
    Check(ctx, "GetTag", [&](bool original, std::mt19937 &) {
        NoteText("tag", original ? orig(w.block) : Impl()->GetTag());
    });
}

DIFF_TEST(SG_ObjectGraphNodeImpl_GetTypeInfo, 0xa34cb0)
{
    auto orig = ctx.Original<cPersistTypeInfo *(__thiscall *)(void *, cPersistTypeInfo *)>(0xa34cb0);
    Check(
        ctx, "GetTypeInfo",
        [&](bool original, std::mt19937 &) {
            cPersistTypeInfo info{};
            cPersistTypeInfo *r = original ? orig(w.block, &info) : Impl()->GetTypeInfo(&info);
            drawfx::Note("returns the record", r == &info);
            sgfx::NoteTypeInfo("type", &info);
            // The name is counted once: one Release frees it (the free is compared by its size).
            difftest::FreeRecorder freed;
            // Released into a local: a Note inside the recorder window can grow the log and count its free (VH, shuffle seed 11).
            const uint32_t released = info.mName ? info.mName->Release() : 0xdead;
            freed.Stop();
            drawfx::Note("name released", released);
            drawfx::Note("frees", uint32_t(freed.Count()));
        },
        4);
}

DIFF_TEST(SG_ObjectGraphNodeImpl_Write, 0xa34d70)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0xa34d70);
    Check(ctx, "Write", [&](bool original, std::mt19937 &) {
        if (original)
            orig(w.block, &w.outer, &sgfx::gWriter.fake);
        else
            Impl()->Write(Node(w.outer), reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
    });
}

DIFF_TEST(SG_ObjectGraphNodeImpl_CopyTo, 0xa34f20)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *, uint32_t, void *)>(0xa34f20);
    Check(ctx, "CopyTo", [&](bool original, std::mt19937 &rng) {
        const uint32_t handles[] = {sgfx::kNoHandle, sgfx::Pool::kFirstHandle, sgfx::Pool::kFirstHandle + 1};
        w.outer.Returns(27, handles[rng() % 3]);
        const uint32_t mode = rng() % 3;
        sgfx::MakeCloneContext(w.context, nullptr);
        void *context = rng() % 2 ? &w.context : nullptr;
        if (original)
            orig(w.block, &w.outer, &w.target, mode, context);
        else
            Impl()->CopyTo(Node(w.outer), Node(w.target), mode, static_cast<cINodeCloneContext *>(context));
    });
}

// ---- the notifications (__stdcall) ----

namespace {
template <class Fn2, class Fn3> void CheckNotify(difftest::Context &ctx, const char *what, Fn2 orig2, Fn3 orig3,
                                                 void(__stdcall *ours2)(cIObjectGraphNode *, uint32_t),
                                                 void(__stdcall *ours3)(cIObjectGraphNode *, uint32_t, cIObjectGraphNode *))
{
    Check(ctx, what, [&](bool original, std::mt19937 &rng) {
        const uint32_t iids[] = {sgfx::kIidTree, sgfx::kIidGraph, 0xabcdef01};
        const uint32_t iid = iids[rng() % 3];
        if (ours2) {
            if (original)
                orig2(&w.outer, iid);
            else
                ours2(Node(w.outer), iid);
        } else {
            if (original)
                orig3(&w.outer, iid, &w.subordinate);
            else
                ours3(Node(w.outer), iid, Node(w.subordinate));
        }
    }, 12);
}
using Notify2 = void(__stdcall *)(void *, uint32_t);
using Notify3 = void(__stdcall *)(void *, uint32_t, void *);
} // namespace

DIFF_TEST(SG_NotifySubordinateAdding, 0xa34310)
{
    CheckNotify(ctx, "adding", ctx.Original<Notify2>(0xa34310), Notify3(nullptr), &NotifySubordinateAdding, nullptr);
}

DIFF_TEST(SG_NotifySubordinateRemoving, 0xa34370)
{
    CheckNotify(ctx, "removing", Notify2(nullptr), ctx.Original<Notify3>(0xa34370), nullptr, &NotifySubordinateRemoving);
}

DIFF_TEST(SG_NotifySubordinateAdded, 0xa34430)
{
    CheckNotify(ctx, "added", Notify2(nullptr), ctx.Original<Notify3>(0xa34430), nullptr, &NotifySubordinateAdded);
}

DIFF_TEST(SG_NotifySubordinateRemoved, 0xa344d0)
{
    CheckNotify(ctx, "removed", ctx.Original<Notify2>(0xa344d0), Notify3(nullptr), &NotifySubordinateRemoved, nullptr);
}

// ---- the event bodies ----

namespace {
// A body object of either side: the game's vtable (0xe68768 / 0xdb1f98) on the original's, ours on ours.
struct RawBody {
    void **vt;
    uint32_t words[4];
};
using U32Fn = uint32_t(__thiscall *)(void *);
using AssignFn = uint32_t(__thiscall *)(void *, const void *);
using CloneFn = void(__thiscall *)(void *, void *);

// Slots 1-6 of a body through its vtable: Assign / AssignAlias from `from`, the type and sizes, CloneInto a block.
// cSubordinateListChanged (vtable 0xe68768): Assign 0xa342f0 (slots 1 and 2), GetEventType 0xa34fc0, CloneInto 0xa34410;
// slots 4 / 5 are bodies folded with other events (0x2919e0, 0x1559c0). cGraphConnChanged (0xdb1f98): compared the
// same way, its slots (0x290350, 0x28bda0, ...) not claimed: other code (0x28caf0) builds that vtable too.
void NoteBodySlots(void *body, const void *from, unsigned words)
{
    void **vt = *static_cast<void ***>(body);
    const uint32_t a = reinterpret_cast<AssignFn>(vt[1])(body, from);
    drawfx::Note("assigned", {a});
    drawfx::Note("words", {static_cast<RawBody *>(body)->words[0], static_cast<RawBody *>(body)->words[1],
                           static_cast<RawBody *>(body)->words[2], static_cast<RawBody *>(body)->words[3]});
    const uint32_t b = reinterpret_cast<AssignFn>(vt[2])(body, from);
    drawfx::Note("aliased", {b});
    drawfx::Note("type", {reinterpret_cast<U32Fn>(vt[3])(body), reinterpret_cast<U32Fn>(vt[4])(body),
                          reinterpret_cast<U32Fn>(vt[5])(body)});
    RawBody copy{};
    for (uint32_t &x : copy.words) x = 0xcdcdcdcd;
    reinterpret_cast<CloneFn>(vt[6])(body, &copy);
    drawfx::Note("clone's vtable is the body's", copy.vt == vt);
    drawfx::Note("clone", {copy.words[0], copy.words[1], copy.words[2], copy.words[3], words});
}
} // namespace

DIFF_TEST(SG_SubordinateListChanged_Slots, 0xa34410)
{
    for (uint32_t seed = 0; seed < 8; ++seed) {
        std::mt19937 rng(seed);
        const uint32_t kind = rng() % 4, iid = rng();
        const uint32_t kind2 = rng() % 4, iid2 = rng();
        auto a = drawfx::Side([] {}, [&] {
            RawBody body{ctx.image.At<void *>(0xe68768), {kind, iid, 0xcdcdcdcd, 0xcdcdcdcd}};
            RawBody from{ctx.image.At<void *>(0xe68768), {kind2, iid2, 0xcdcdcdcd, 0xcdcdcdcd}};
            NoteBodySlots(&body, &from, 2);
        });
        auto b = drawfx::Side([] {}, [&] {
            // Our object is 0xc bytes, in a block padded as the original's above.
            alignas(4) uint32_t block[5] = {0, 0, 0, 0xcdcdcdcd, 0xcdcdcdcd};
            cSubordinateListChanged *body = ::new (block) cSubordinateListChanged(kind, iid);
            cSubordinateListChanged from(kind2, iid2);
            NoteBodySlots(body, &from, 2);
        });
        drawfx::Same(ctx, a.first, b.first, "cSubordinateListChanged", seed, a.second);
    }
}

DIFF_TEST(SG_GraphConnChanged_Slots, 0x28bda0)
{
    for (uint32_t seed = 0; seed < 8; ++seed) {
        std::mt19937 rng(seed);
        uint32_t v[8];
        for (uint32_t &x : v) x = rng();
        auto a = drawfx::Side([] {}, [&] {
            RawBody body{ctx.image.At<void *>(0xdb1f98), {v[0], v[1], v[2], v[3]}};
            RawBody from{ctx.image.At<void *>(0xdb1f98), {v[4], v[5], v[6], v[7]}};
            NoteBodySlots(&body, &from, 4);
        });
        auto b = drawfx::Side([] {}, [&] {
            cGraphConnChanged body(v[0], reinterpret_cast<cIObjectGraphNode *>(uintptr_t(v[1])),
                                   reinterpret_cast<cIObjectGraphNode *>(uintptr_t(v[2])), v[3]);
            cGraphConnChanged from(v[4], reinterpret_cast<cIObjectGraphNode *>(uintptr_t(v[5])),
                                   reinterpret_cast<cIObjectGraphNode *>(uintptr_t(v[6])), v[7]);
            NoteBodySlots(&body, &from, 4);
        });
        drawfx::Same(ctx, a.first, b.first, "cGraphConnChanged", seed, a.second);
    }
}
