// Differential test for the snapshot assignment of the person's FindBestAction (SearchSnapshot::Assign, 0x4365d0): the data
// words are copied, the reference is replaced with AddRef / Release in the original's order, and nothing happens to the
// reference when both snapshots hold the same object (or are the same snapshot).
#include "simantics_autonomy_records.h"

namespace {

constexpr uint32_t kSnapshotDestination = 0xa0;
constexpr uint32_t kSnapshotSource = 0x130;

// AddRef (slot 1) and Release (slot 2) of a held object: they also log what the destination's reference field holds at that
// moment, so the order of the store relative to the two calls is compared.
uint32_t ReferenceFieldNow()
{
    return Norm(reinterpret_cast<uint32_t>(reinterpret_cast<SearchSnapshot *>(gW->scratch + kSnapshotDestination)->mReference));
}
uint32_t __fastcall AddRefProbe(void *self, void *)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(1u << 8);
    gTrace.push_back(ReferenceFieldNow());
    return 0;
}
uint32_t __fastcall ReleaseProbe(void *self, void *)
{
    gTrace.push_back(Off(self));
    gTrace.push_back(2u << 8);
    gTrace.push_back(ReferenceFieldNow());
    return 0;
}

void BuildAssign(WorldP &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 29);
    InitPerson(w);
    ResetScript(seed, r);
    w.canary = 0xc0ffee00u;
    FillRandom(w.scratch, kScratch, r);
    for (int i = 0; i < kHeldCount; ++i) {
        w.vtFk[kHeld + i][1] = reinterpret_cast<void *>(&AddRefProbe);
        w.vtFk[kHeld + i][2] = reinterpret_cast<void *>(&ReleaseProbe);
    }
    SearchSnapshot *dst = reinterpret_cast<SearchSnapshot *>(w.scratch + kSnapshotDestination);
    SearchSnapshot *src = reinterpret_cast<SearchSnapshot *>(w.scratch + kSnapshotSource);
    auto pick = [&]() -> void * { return (r() % 4 == 0) ? nullptr : static_cast<void *>(&w.fk[kHeld + r() % kHeldCount]); };
    dst->mReference = pick();
    src->mReference = (r() % 3 == 0) ? dst->mReference : pick();
}

} // namespace

DIFF_TEST(Autonomy_SearchSnapshotAssign, 0x4365d0)
{
    auto original = ctx.Original<void *(__thiscall *)(void *, const void *)>(0x4365d0);
    uint32_t quiet = 0, counted = 0, aliased = 0;
    RunSeeds(
        ctx, 8000, BuildAssign,
        [&](void *, void *, void *) {
            SearchSnapshot *dst = reinterpret_cast<SearchSnapshot *>(gW->scratch + kSnapshotDestination);
            const SearchSnapshot *src = (gSeed % 5 == 0) ? dst : reinterpret_cast<SearchSnapshot *>(gW->scratch + kSnapshotSource);
            return Norm(reinterpret_cast<uint32_t>(original(dst, src)));
        },
        [&](void *, void *, void *) {
            SearchSnapshot *dst = reinterpret_cast<SearchSnapshot *>(gW->scratch + kSnapshotDestination);
            const SearchSnapshot *src = (gSeed % 5 == 0) ? dst : reinterpret_cast<SearchSnapshot *>(gW->scratch + kSnapshotSource);
            return Norm(reinterpret_cast<uint32_t>(dst->Assign(src)));
        },
        [&](WorldP &, const Trial &t) {
            if (gSeed % 5 == 0) ++aliased;
            (t.oa.trace.empty() ? quiet : counted)++;
        });
    if (!quiet || !counted || !aliased) ctx.Fail("coverage: no-op %u reference changes %u aliased %u", quiet, counted, aliased);
}
