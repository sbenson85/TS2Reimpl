// Differential test for opcode 3 (0x521860, PrimPersonObject::Prim03), FindBestAction. The whole search is real on both sides over the
// search world (tests/simantics_autonomy_searchworld.h, on the gather, score and check worlds of the autonomy tests): BeginSearch,
// GatherInteractions, ScorePending, ScoreInteraction, ChooseAction and StartChosenAction, the check-tree runner and the interaction test
// sim under them, the vector helpers; the objects behind the vtables are recording fakes (the loop's inputs come from the input object,
// which also leaves interactions in the two reference vectors the way the tree's primitives do). Earlier versions of this test stood the
// helpers in with code patches in the mapped image; that cannot redirect a direct call of ours, which is how the helpers are called
// now. Framework: tests/simantics_prims_person.h.
#include "simantics_autonomy_searchworld.h"
#include "code_patch.h"
#include "rz/RZFileStreams.h"

namespace {

struct SearchCover {
    uint32_t refused = 0, ran = 0, started = 0, loops = 0, pairs = 0, gathered = 0, thrown = 0, scoreRecording = 0, chose = 0, nodeObject = 0;
};

uint32_t CountOf(const std::vector<uint32_t> &trace, uint32_t id, uint32_t slotAndCount)
{
    uint32_t n = 0;
    for (size_t i = 0; i + 1 < trace.size(); ++i) n += trace[i] == id && trace[i + 1] == slotAndCount;
    return n;
}

} // namespace

DIFF_TEST(PrimPerson_Prim03, 0x521860)
{
    GatherGlobals globals(ctx);
    const SavedGlobals saved(ctx);
    GameAllocatorFixture allocator;
    // The file class is ours (rz/RZFileStreams.h): the stand-in covers the image's constructor and ours.
    shape::StandIn fileClass(0x1b4dd2, static_cast<cRZOFStreamFromString>(&cRZOFStream::Construct), &SearchFileClassStandIn);
    auto original = ctx.Original<OrigFn>(0x521860);
    SearchCover cover;
    RunSeeds(
        ctx, 60000, BuildSearchWorld,
        [&](uint8_t *o, void *f, const void *p) {
            Queues guard(o);
            uint32_t result = 0xbad;
            try {
                result = original(o, f, p);
            } catch (...) {
                FinishSearchWorld(*gW);
                throw;
            }
            FinishSearchWorld(*gW);
            return result;
        },
        [&](uint8_t *o, void *f, const void *p) {
            Queues guard(o);
            uint32_t result = 0xbad;
            try {
                result = reinterpret_cast<PrimPersonObject *>(o)->Prim03(f, static_cast<const PrimParamView *>(p));
            } catch (...) {
                FinishSearchWorld(*gW);
                throw;
            }
            FinishSearchWorld(*gW);
            return result;
        },
        [&](WorldP &w, const Trial &t) {
            const Outcome &o = t.oa;
            if (o.thrown >= 0) {
                ++cover.thrown;
                return;
            }
            const uint32_t input = OffsetIn(w.obj + 0x60c, w);
            const uint32_t reads = CountOf(o.trace, input, 8u << 8 | 3u);
            (o.ret == 0 ? cover.refused : cover.ran) += 1;
            cover.started += o.ret == 1;
            cover.loops += reads > 1;
            cover.gathered += CountOf(o.trace, OffsetIn(&w.fk[kModule], w), 48u << 8 | 3u) != 0;
            cover.chose += CountOf(o.trace, OffsetIn(&w.fk[kHolder], w), 55u << 8) != 0;
            cover.nodeObject += CountOf(o.trace, OffsetIn(&w.fk[kSettings], w), 29u << 8 | 1u) != 0;
            for (int i = 0; i < kEntries; ++i) cover.pairs += CountOf(o.trace, OffsetIn(&w.fk[kT0 + i], w), 1u << 8) != 0;
        });
    gBeforeRun = nullptr;
    saved.Restore(ctx);
    if (cover.refused < 500 || cover.ran < 2000 || cover.started < 2000 || cover.loops < 5000 || cover.gathered < 5000 || cover.thrown < 2000 || cover.chose < 1000 ||
        cover.nodeObject < 5000 || std::getenv("GATHER_COVER"))
        ctx.Fail("coverage: refused %u ran %u started %u loops %u gathered %u thrown %u chose %u node object %u pairs-ish %u", cover.refused, cover.ran, cover.started,
                 cover.loops, cover.gathered, cover.thrown, cover.chose, cover.nodeObject, cover.pairs);
}
