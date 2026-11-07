// Differential tests of nGZSceneGraph::cRenderGroupNameIterator (its constructors 0xadffc0 / 0xadff90 and slots 0xae0050,
// 0xae0080, 0xae0680, 0xae0030, all reached through the builder) and the builder of the names' range (0xae02f0; lane PX,
// scenegraph N3). The builder runs on a 0x70-byte renderable Impl block (sg_renderable_fx.h: the registry as a
// stand-in); the iterators it returns are real objects on both sides (the game's with its vtable 0xe7c8dc, ours), walked
// through their vtables: Clone (0xae0050), Get (0xae0080), Next (0xae0680), and the shared slots QueryInterface,
// AddRef, Release (which deletes through the deleting destructor 0xae0030) and IsEqual.
#include "sg_renderable_fx.h"

namespace {

using namespace nGZSceneGraph;
using namespace sgtx;

World<28> w;
cRenderableNodeImpl *Impl() { return w.Impl<cRenderableNodeImpl>(); }

void NoteName(const char *label, const char *text)
{
    drawfx::Call c;
    c.note = label;
    c.words = {text != nullptr};
    if (text) c.text = text;
    drawfx::gLog.push_back(std::move(c));
}

// One iterator walk from `it` to `end`: the names and the positions (a step budget keeps a broken side from hanging).
void Walk(cIRenderGroupNameIterator *it, cIRenderGroupNameIterator *end)
{
    for (unsigned steps = 0; steps < 40 && !it->IsEqual(end); ++steps) {
        NoteName("name", it->Get());
        it->Next();
    }
    drawfx::Note("at end", uint32_t(it->IsEqual(end)));
}

} // namespace

DIFF_TEST(SG_RenderGroupNameIterator_Build, 0xae02f0)
{
    sgrx::Services services;
    auto orig = ctx.Original<cRenderGroupNameRange *(__thiscall *)(void *, cRenderGroupNameRange *)>(0xae02f0);
    w.Check(ctx, "builder and walks", [&](bool original, std::mt19937 &rng) {
        sgrx::Setup(rng);
        sgrx::RandomLayerWords(w.block, rng);
        cRenderGroupNameRange range{};
        cRenderGroupNameRange *r = original ? orig(w.block, &range) : Impl()->MakeLayerNames(&range);
        drawfx::Note("returns out", r == &range);
        drawfx::Note("all made", {range.mCurrent != nullptr, range.mBegin != nullptr, range.mEnd != nullptr});
        // The counts: a clone and two ends, each counted once.
        drawfx::Note("counts", {range.mCurrent->AddRef(), range.mBegin->AddRef(), range.mEnd->AddRef()});
        drawfx::Note("counts back", {range.mCurrent->Release(), range.mBegin->Release(), range.mEnd->Release()});
        // A copy walks the whole range; the cursor and begin stay.
        cIRenderGroupNameIterator *copy = range.mBegin->Clone();
        drawfx::Note("clone starts counted", copy->AddRef());
        drawfx::Note("clone equals begin", uint32_t(copy->IsEqual(range.mBegin)));
        Walk(copy, range.mEnd);
        copy->Release();
        // The cursor moves part of the way, is cloned in the middle, and the clone finishes.
        for (unsigned k = rng() % 4; k > 0 && !range.mCurrent->IsEqual(range.mEnd); --k) range.mCurrent->Next();
        cIRenderGroupNameIterator *middle = range.mCurrent->Clone();
        middle->AddRef();
        Walk(middle, range.mEnd);
        drawfx::Note("middle released", middle->Release());
        Walk(range.mCurrent, range.mEnd);
        drawfx::Note("begin still first", uint32_t(range.mBegin->IsEqual(range.mCurrent)));
        // QueryInterface: iid 1 answers itself, counted; anything else does not.
        void *out = nullptr;
        drawfx::Note("qi 1", {uint32_t(range.mBegin->QueryInterface(1, &out)), out == range.mBegin});
        drawfx::Note("qi 1 count", range.mBegin->Release());
        out = nullptr;
        drawfx::Note("qi 0", {uint32_t(range.mBegin->QueryInterface(0, &out)), out == nullptr});
        // Equality is of the position, not an order.
        drawfx::Note("begin vs end", {uint32_t(range.mBegin->IsEqual(range.mEnd)), uint32_t(range.mEnd->IsEqual(range.mBegin))});
        out = nullptr;
        drawfx::Note("qi other", {uint32_t(range.mBegin->QueryInterface(uint32_t(rng()) | 2, &out)), out == nullptr});
        drawfx::Note("release end", range.mEnd->Release());
        drawfx::Note("release begin", range.mBegin->Release());
        drawfx::Note("release current", range.mCurrent->Release());
    });
}
