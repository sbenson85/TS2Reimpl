// Differential tests of nGZSceneGraph::cReferentNodeImpl (lane OR, scenegraph N2): the referent nodes' Impl, an
// object-graph Impl (+0 extension list, +4 tag handle) plus a vector of referrers (+8 first, +0xc last, +0x10 end). Each
// member runs on a 0x14-byte block in a world of sgfx fakes: an outer node, extensions the reader hands out, four
// referrers (behaviour fakes: their slots 3 / 4 log, slot 4 answers per seed), the string pool, a persist reader and
// writer. The block's list starts as our std::vector built it (both CRTs use the process heap). Compared: each side's
// calls, the block's first two words, the referrers by id in order, the size and the capacity, the frees.
#include "difftest.h"
#include "sgfx.h"

#include "sg/cReferentNodeImpl.h"

#include <new>
#include <random>

namespace {

using namespace nGZSceneGraph;
using drawfx::Fake;
using drawfx::P;

constexpr unsigned kReferrers = 4; // ids 600-603
constexpr unsigned kExt = 2;       // extensions the reader hands out, ids 200-201
constexpr uint32_t kBlockTag = 0x7b000000;
constexpr unsigned kSeeds = 64;

const char *const kTexts[] = {"alpha", "", "a tag longer than sixteen"};

struct World {
    Fake outer, data;
    Fake referrer[kReferrers];
    Fake ext[kExt];
    alignas(4) uint32_t block[5];
};
World w;

cReferentNodeImpl *Impl() { return reinterpret_cast<cReferentNodeImpl *>(w.block); }
cIObjectGraphNode *Node(Fake &f) { return reinterpret_cast<cIObjectGraphNode *>(&f); }
nGZEvent::cIAbstractBehavior *Behavior(Fake &f) { return reinterpret_cast<nGZEvent::cIAbstractBehavior *>(&f); }

// The world for `seed`: the block built by our constructor, then 0-6 referrers (repeats allowed) pushed in random order
// (so the capacity is what our vector's growth left: 0, 1, 2, 3, 4, 6, 9).
void Build(uint32_t seed, drawfx::Cells &cells)
{
    std::mt19937 rng(seed);
    sgfx::gPool.Reset(cells);
    sgfx::gReader.Reset();
    sgfx::gWriter.Reset();
    sgfx::MakeNode(w.outer, 100);
    w.data.Reset<sgfx::kUnknown>(101);
    for (unsigned i = 0; i < kReferrers; ++i) {
        w.referrer[i].Reset<sgfx::kBehavior>(600 + i);
        w.referrer[i].Returns(4, uint32_t(rng() % 2));
    }
    for (unsigned i = 0; i < kExt; ++i) sgfx::MakeExtension(w.ext[i], 200 + i, true);
    ::new (w.block) cReferentNodeImpl;
    const unsigned count = rng() % 7;
    for (unsigned i = 0; i < count; ++i) Impl()->mReferrers.push_back(Behavior(w.referrer[rng() % kReferrers]));
    drawfx::AddRange(w.block, sizeof(w.block), kBlockTag);
}

uint32_t IdOf(const void *p)
{
    for (unsigned i = 0; i < kReferrers; ++i)
        if (p == &w.referrer[i]) return w.referrer[i].id;
    return P(p) ? 0xbad : 0;
}

// The block: the first two words, then the list's size, capacity and members (by id), from the raw words.
void NoteBlock()
{
    drawfx::Note("impl", {drawfx::Norm(w.block[0]), w.block[1]});
    const auto **first = reinterpret_cast<const void **>(uintptr_t(w.block[2]));
    const auto **last = reinterpret_cast<const void **>(uintptr_t(w.block[3]));
    const auto **end = reinterpret_cast<const void **>(uintptr_t(w.block[4]));
    drawfx::Note("list", {first != nullptr, uint32_t(last - first), uint32_t(end - first)});
    drawfx::Call c;
    c.note = "referrers";
    for (const void **p = first; p && p != last; ++p) c.words.push_back(IdOf(*p));
    drawfx::gLog.push_back(std::move(c));
}

void NoteWorld()
{
    NoteBlock();
    for (const Fake &r : w.referrer) drawfx::Note("referrer", {r.id, uint32_t(r.refs)});
    for (const Fake &e : w.ext)
        drawfx::Note("extension", {e.id, uint32_t(e.refs), drawfx::Norm(e.value[sgfx::kExtNextWord]),
                                   drawfx::Norm(e.value[sgfx::kExtOwnerWord])});
    drawfx::Note("outer", {uint32_t(w.outer.refs), drawfx::Norm(w.outer.value[sgfx::kFirstExtensionWord])});
    sgfx::gPool.NoteCounts();
    sgfx::gReader.NoteNames();
}

// A writer slot that notes how many blocks the running FreeRecorder has seen freed so far (and answers 1).
difftest::FreeRecorder *gFreed = nullptr;
uint32_t NoteFreesSoFar(Fake &, const uint32_t *)
{
    drawfx::Note("frees so far", gFreed ? uint32_t(gFreed->Count()) : 0xdeadu);
    return 1;
}

// Each seed: the world built twice, the original run on one and ours on the other; the logs compared. `destroyed`: the
// run destroyed the block itself; otherwise our destructor frees what is left (after the notes).
template <class Run> void Check(difftest::Context &ctx, const char *what, Run run, unsigned seeds = kSeeds)
{
    sgfx::CheckSides(ctx, what, seeds, Build, [&](bool original, std::mt19937 &rng) {
        const bool destroyed = run(original, rng);
        NoteWorld();
        if (!destroyed) Impl()->~cReferentNodeImpl();
    });
}

} // namespace

DIFF_TEST(SG_ReferentNodeImpl_Construct, 0xa446b0)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xa446b0);
    Check(ctx, "constructor", [&](bool original, std::mt19937 &rng) {
        Impl()->~cReferentNodeImpl();
        for (uint32_t &x : w.block) x = rng();
        void *r = original ? orig(w.block) : Impl()->Construct();
        drawfx::Note("returns the block", r == w.block);
        drawfx::Note("words", {w.block[0], w.block[1], w.block[2], w.block[3], w.block[4]});
        return false;
    });
}

DIFF_TEST(SG_ReferentNodeImpl_Destruct, 0xa44710)
{
    auto orig = ctx.Original<void(__thiscall *)(void *)>(0xa44710);
    Check(ctx, "destructor", [&](bool original, std::mt19937 &) {
        difftest::FreeRecorder freed;
        if (original)
            orig(w.block);
        else
            Impl()->Destruct();
        freed.Stop();
        drawfx::Note("frees", uint32_t(freed.Count()));
        drawfx::Note("words", {w.block[2], w.block[3], w.block[4]});
        w.block[2] = w.block[3] = w.block[4] = 0;
        return true;
    });
}

DIFF_TEST(SG_ReferentNodeImpl_AddReferrer, 0xa44770)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *)>(0xa44770);
    Check(ctx, "AddReferrer", [&](bool original, std::mt19937 &rng) {
        const unsigned adds = 1 + rng() % 5;
        difftest::FreeRecorder freed;
        for (unsigned i = 0; i < adds; ++i) {
            Fake &r = w.referrer[rng() % kReferrers];
            if (original)
                orig(w.block, &r);
            else
                Impl()->AddReferrer(Behavior(r));
            NoteBlock();
        }
        freed.Stop();
        drawfx::Note("frees", uint32_t(freed.Count()));
        return false;
    });
}

DIFF_TEST(SG_ReferentNodeImpl_RemoveReferrer, 0xa44850)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *)>(0xa44850);
    Check(ctx, "RemoveReferrer", [&](bool original, std::mt19937 &rng) {
        // Only referrers in the list (one not in it crashes both: the original moves (size_t)-4 bytes).
        while (w.block[3] != w.block[2]) {
            const auto **first = reinterpret_cast<const void **>(uintptr_t(w.block[2]));
            const unsigned size = (w.block[3] - w.block[2]) / 4;
            Fake &r = *const_cast<Fake *>(static_cast<const Fake *>(first[rng() % size]));
            if (original)
                orig(w.block, &r);
            else
                Impl()->RemoveReferrer(Behavior(r));
            NoteBlock();
            if (rng() % 4 == 0) break;
        }
        return false;
    });
}

DIFF_TEST(SG_ReferentNodeImpl_Ask, 0xa447a0)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t, uint32_t, void *)>(0xa447a0);
    Check(ctx, "Ask", [&](bool original, std::mt19937 &rng) {
        const uint32_t type = rng(), arg1 = rng() % 3;
        void *data = rng() % 2 ? &w.data : nullptr;
        const bool r = original ? orig(w.block, type, arg1, data) : Impl()->Ask(type, arg1, data);
        drawfx::Note("result", uint8_t(r));
        return false;
    });
}

DIFF_TEST(SG_ReferentNodeImpl_Notify, 0xa44880)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, uint32_t, uint32_t, void *)>(0xa44880);
    Check(ctx, "Notify", [&](bool original, std::mt19937 &rng) {
        const uint32_t type = rng(), arg1 = rng() % 3;
        void *data = rng() % 2 ? &w.data : nullptr;
        if (original)
            orig(w.block, &w.outer, type, arg1, data);
        else
            Impl()->Notify(Node(w.outer), type, arg1, data);
        return false;
    });
}

DIFF_TEST(SG_ReferentNodeImpl_Read, 0xa447e0)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0xa447e0);
    Check(ctx, "Read", [&](bool original, std::mt19937 &rng) {
        const uint32_t versions[] = {0, 1, 4, 5};
        sgfx::Reader &r = sgfx::gReader;
        r.Version(versions[rng() % 4]); // the referent record
        r.Version(versions[rng() % 4]); // the object-graph record
        const uint32_t count = rng() % 3;
        r.Word(count);
        for (uint32_t i = 0; i < count; ++i) r.Ref(rng() % 4 ? static_cast<void *>(&w.ext[i]) : nullptr);
        r.Text(kTexts[rng() % 3]);
        if (original)
            orig(w.block, &w.outer, &r.fake);
        else
            Impl()->Read(Node(w.outer), reinterpret_cast<ifaces::cIGZPersistReader *>(&r.fake));
        return false;
    });
}

DIFF_TEST(SG_ReferentNodeImpl_Write, 0xa449a0)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, void *, void *)>(0xa449a0);
    Check(ctx, "Write", [&](bool original, std::mt19937 &) {
        difftest::FreeRecorder freed; // the type name, released after it is written
        // The object-graph part's count (slot 10) and tag (slot 23) note the frees so far: the referent record's name is
        // released before that part is written.
        gFreed = &freed;
        sgfx::gWriter.fake.On(10, &NoteFreesSoFar);
        sgfx::gWriter.fake.On(23, &NoteFreesSoFar);
        if (original)
            orig(w.block, &w.outer, &sgfx::gWriter.fake);
        else
            Impl()->Write(Node(w.outer), reinterpret_cast<ifaces::cIGZPersistWriter *>(&sgfx::gWriter.fake));
        freed.Stop();
        gFreed = nullptr;
        drawfx::Note("frees", uint32_t(freed.Count()));
        return false;
    });
}

DIFF_TEST(SG_ReferentNodeImpl_IsKindOf, 0xa448d0)
{
    auto orig = ctx.Original<bool(__thiscall *)(void *, uint32_t)>(0xa448d0);
    Check(
        ctx, "IsKindOf",
        [&](bool original, std::mt19937 &rng) {
            const uint32_t iids[] = {0x9c6ab984, 0x5aef607b, 0x3b253dc7, 0xbb11a53e, 0x5b0aaa74, 0x9c5ed164,
                                     0xea932e72, 0,          1,          0x9c6ab985, uint32_t(rng())};
            for (uint32_t iid : iids)
                drawfx::Note("kind", {iid, uint8_t(original ? orig(w.block, iid) : Impl()->IsKindOf(iid))});
            return false;
        },
        4);
}

DIFF_TEST(SG_ReferentNodeImpl_GetTypeInfo, 0xa448f0)
{
    auto orig = ctx.Original<cPersistTypeInfo *(__thiscall *)(void *, cPersistTypeInfo *)>(0xa448f0);
    Check(
        ctx, "GetTypeInfo",
        [&](bool original, std::mt19937 &) {
            cPersistTypeInfo info{};
            cPersistTypeInfo *r = original ? orig(w.block, &info) : Impl()->GetTypeInfo(&info);
            drawfx::Note("returns the record", r == &info);
            sgfx::NoteTypeInfo("type", &info);
            difftest::FreeRecorder freed;
            // Released into a local: a Note inside the recorder window can grow the log and count its free (VH, shuffle seed 11).
            const uint32_t released = info.mName ? info.mName->Release() : 0xdead;
            freed.Stop();
            drawfx::Note("name released", released);
            drawfx::Note("frees", uint32_t(freed.Count()));
            return false;
        },
        4);
}
