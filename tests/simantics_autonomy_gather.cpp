// Differential tests for the gather step of the person's FindBestAction: GatherInteractions 0x5155b0 (src/simantics/FindBestActionGather.cpp)
// and the interaction test sim's gather members (src/simantics/EdithObjectTestSimGather.cpp): AppendInteractions 0x44fb90,
// AppendAutonomousInteractions 0x44fbc0, AppendInteractionsForMenu 0x44ff80, AppendIfPasses 0x4519c0 and the cheat interaction 0x44f760.
// Everything below them is real on both sides over the gather world (tests/simantics_autonomy_gatherworld.h): the check-tree runner, the
// allocator service, the vector helper 0x1d57e0; the objects behind the vtables are recording fakes. The cases the original crashes on (a
// table without the entry a group names, a Sim without a current interaction, a Sim stack object that gives no person interface) are not
// generated. Set GATHER_COVER=1 to print what each test reached.
#include "simantics_autonomy_gatherworld.h"

namespace {

struct GatherCover {
    uint32_t runs = 0, thrown = 0, listed = 0, created = 0, setUps = 0, between = 0, names = 0, asked = 0, branchFlags = 0, tableAsked = 0;
};

// How often the fakes `first` .. `first + count - 1` answered `slot` in a run's trace.
uint32_t CountCalls(const std::vector<uint32_t> &trace, const WorldP &w, int first, int count, int slot)
{
    uint32_t calls = 0;
    for (int fake = first; fake < first + count; ++fake) {
        const uint32_t id = OffsetIn(&w.fk[fake], w);
        for (size_t i = 0; i + 1 < trace.size(); ++i) {
            if (trace[i] == id && (trace[i + 1] >> 8) == static_cast<uint32_t>(slot)) ++calls;
        }
    }
    return calls;
}

void Observe(GatherCover &cover, const WorldP &w, const Trial &t)
{
    const Outcome &o = t.oa;
    ++cover.runs;
    cover.thrown += o.thrown >= 0;
    cover.created += CountCalls(o.trace, w, kFramework, 1, 3);
    cover.setUps += CountCalls(o.trace, w, kI0, kInteractions, 8);
    cover.between += CountCalls(o.trace, w, kI0, kInteractions, 7);
    cover.names += CountCalls(o.trace, w, kI0, kInteractions, 26);
    cover.branchFlags += CountCalls(o.trace, w, kI0, kInteractions, 44);
    cover.asked += CountCalls(o.trace, w, kModule, 1, 48);
    cover.tableAsked += CountCalls(o.trace, w, kTable, 1, 9);
}

// A run of one gather function: `call(world, arena)` on either side, then the world is described and torn down.
template <typename Original, typename Ours>
void RunGather(difftest::Context &ctx, uint32_t seeds, uint32_t salt, const GatherParams &params, Original original, Ours ours, GatherCover &cover)
{
    GatherGlobals globals(ctx);
    const SavedGlobals saved(ctx);
    GameAllocatorFixture allocator;
    RunSeeds(
        ctx, seeds, [&](WorldP &w, uint32_t seed) { BuildGatherWorld(w, seed, salt, params); },
        [&](uint8_t *o, void *, const void *) {
            Queues guard(o);
            GatherArena &arena = ArenaOf(*gW);
            const size_t before = arena.list.mLast - arena.list.mFirst + (Person(*gW)->mPending.mLast - Person(*gW)->mPending.mFirst);
            try {
                original(*gW, arena);
            } catch (...) {
                FinishGatherWorld(*gW, true);
                throw;
            }
            cover.listed += static_cast<size_t>(arena.list.mLast - arena.list.mFirst + (Person(*gW)->mPending.mLast - Person(*gW)->mPending.mFirst)) > before;
            FinishGatherWorld(*gW, true);
            return 0u;
        },
        [&](uint8_t *o, void *, const void *) {
            Queues guard(o);
            GatherArena &arena = ArenaOf(*gW);
            try {
                ours(*gW, arena);
            } catch (...) {
                FinishGatherWorld(*gW, true);
                throw;
            }
            FinishGatherWorld(*gW, true);
            return 0u;
        },
        [&](WorldP &w, const Trial &t) { Observe(cover, w, t); });
    gBeforeRun = nullptr;
    saved.Restore(ctx);
}

// Fails when `ok` is not true; with GATHER_COVER set, fails with the numbers either way (to read them).
void CheckCover(difftest::Context &ctx, const char *name, const GatherCover &c, bool ok)
{
    if (!ok || std::getenv("GATHER_COVER"))
        ctx.Fail("coverage of %s: runs %u thrown %u listed %u created %u set up %u between %u names %u branch flags %u groups asked %u table asked %u", name, c.runs,
                 c.thrown, c.listed, c.created, c.setUps, c.between, c.names, c.branchFlags, c.asked, c.tableAsked);
}

} // namespace

DIFF_TEST(Autonomy_GatherInteractions, 0x5155b0)
{
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x5155b0);
    GatherCover cover;
    RunGather(
        ctx, 40000, 0x5155b0, GatherParams(),
        [&](WorldP &w, GatherArena &arena) {
            arena.autonomy = true;
            original(w.obj);
        },
        [&](WorldP &w, GatherArena &arena) {
            arena.autonomy = true;
            Person(w)->GatherInteractions();
        },
        cover);
    CheckCover(ctx, "GatherInteractions", cover, cover.thrown > 500 && cover.listed > 3000 && cover.created > 8000 && cover.asked > 30000 && cover.between > 500);
}

DIFF_TEST(Autonomy_AppendIfPasses, 0x4519c0)
{
    auto original = ctx.Original<void(__thiscall *)(void *, void *, void *, void *, uint32_t)>(0x4519c0);
    GatherCover cover;
    auto args = [](WorldP &w, GatherArena &arena, void *&interaction, void *&recorder, uint32_t &extra) {
        std::mt19937 r(gSeed * 31 + 7);
        interaction = &Interaction(w, r());
        recorder = r() % 5 ? static_cast<void *>(&Fk(w, kRecorder)) : nullptr;
        extra = r() % 3 ? r() % 4 : r();
        arena.Sim()->mFlagA = static_cast<uint8_t>(r() % 2);
        arena.autonomy = arena.Sim()->mFlagA != 0;
    };
    RunGather(
        ctx, 30000, 0x4519c0, GatherParams(),
        [&](WorldP &w, GatherArena &arena) {
            void *interaction, *recorder;
            uint32_t extra;
            args(w, arena, interaction, recorder, extra);
            original(arena.Sim(), &arena.list, interaction, recorder, extra);
        },
        [&](WorldP &w, GatherArena &arena) {
            void *interaction, *recorder;
            uint32_t extra;
            args(w, arena, interaction, recorder, extra);
            arena.Sim()->AppendIfPasses(&arena.list, interaction, recorder, extra);
        },
        cover);
    CheckCover(ctx, "AppendIfPasses", cover, cover.thrown > 100 && cover.listed > 5000 && cover.listed < 25000);
}

DIFF_TEST(Autonomy_AddCheatInteraction, 0x44f760)
{
    auto original = ctx.Original<void(__cdecl *)(void *, void *, void *, int32_t, uint32_t, const char *, uint32_t)>(0x44f760);
    GatherCover cover;
    static const char *const names[] = {"Dump Happy Log", "Force Error", "", "A name that is rather longer than the sixteen characters a small string holds", "x"};
    auto args = [](WorldP &w, uint32_t &entry, uint32_t &kind, const char *&name, uint32_t &text, void *&person, void *&stackObject) {
        std::mt19937 r(gSeed * 17 + 3);
        const int16_t entries[] = {-2, 0, 5, -1, 0x7fff, -32768};
        entry = static_cast<uint32_t>(static_cast<int32_t>(entries[r() % 6]));
        kind = r() % 3 ? 0x32 : r();
        name = names[r() % 5];
        text = r() % 4 ? 0x2000 + r() % 0x200 : r();
        person = w.obj + 0x2f0;
        stackObject = &Fk(w, kS0 + r() % kObjects);
    };
    RunGather(
        ctx, 20000, 0x44f760, GatherParams(),
        [&](WorldP &w, GatherArena &arena) {
            uint32_t entry, kind, text;
            const char *name;
            void *person, *stackObject;
            args(w, entry, kind, name, text, person, stackObject);
            original(&arena.list, person, stackObject, static_cast<int32_t>(entry), kind, name, text);
        },
        [&](WorldP &w, GatherArena &arena) {
            uint32_t entry, kind, text;
            const char *name;
            void *person, *stackObject;
            args(w, entry, kind, name, text, person, stackObject);
            AddCheatInteraction(&arena.list, person, stackObject, static_cast<int16_t>(entry), kind, name, text);
        },
        cover);
    CheckCover(ctx, "AddCheatInteraction", cover, cover.thrown > 100 && cover.listed > 5000 && cover.names > 15000);
}

DIFF_TEST(Autonomy_AppendInteractions, 0x44fb90)
{
    auto original = ctx.Original<void(__thiscall *)(void *, void *, uint32_t, uint32_t)>(0x44fb90);
    GatherCover cover;
    auto args = [](GatherArena &arena, uint32_t &mask, uint32_t &stopAtFirst) {
        std::mt19937 r(gSeed * 13 + 5);
        mask = r() % 3 ? (r() % 3 ? r() & 0xffff : 0u) : r();
        stopAtFirst = NoiseAl(r, r() % 2);
        arena.autonomy = arena.Sim()->mFlagA != 0;
    };
    RunGather(
        ctx, 20000, 0x44fb90, GatherParams(),
        [&](WorldP &, GatherArena &arena) {
            uint32_t mask, stop;
            args(arena, mask, stop);
            original(arena.Sim(), &arena.list, mask, stop);
        },
        [&](WorldP &, GatherArena &arena) {
            uint32_t mask, stop;
            args(arena, mask, stop);
            arena.Sim()->AppendInteractions(&arena.list, mask, stop);
        },
        cover);
    CheckCover(ctx, "AppendInteractions", cover, cover.thrown > 50 && cover.listed > 3000 && cover.setUps > 5000 && cover.between > 100 && cover.names > 100);
}

DIFF_TEST(Autonomy_AppendAutonomousInteractions, 0x44fbc0)
{
    auto original = ctx.Original<void(__thiscall *)(void *, void *, uint32_t)>(0x44fbc0);
    GatherCover cover;
    auto args = [](GatherArena &arena, uint32_t &mask) {
        std::mt19937 r(gSeed * 19 + 1);
        mask = r() % 3 ? (r() % 3 ? r() & 0xffff : 0u) : r();
        arena.Sim()->mFlagA = 1;
        arena.autonomy = true;
    };
    RunGather(
        ctx, 30000, 0x44fbc0, GatherParams(),
        [&](WorldP &, GatherArena &arena) {
            uint32_t mask;
            args(arena, mask);
            original(arena.Sim(), &arena.list, mask);
        },
        [&](WorldP &, GatherArena &arena) {
            uint32_t mask;
            args(arena, mask);
            arena.Sim()->AppendAutonomousInteractions(&arena.list, mask);
        },
        cover);
    CheckCover(ctx, "AppendAutonomousInteractions", cover, cover.thrown > 100 && cover.listed > 5000 && cover.setUps > 5000 && cover.between > 500);
}

DIFF_TEST(Autonomy_AppendInteractionsForMenu, 0x44ff80)
{
    auto original = ctx.Original<void(__thiscall *)(void *, void *, uint32_t)>(0x44ff80);
    GatherCover cover;
    auto args = [](GatherArena &arena, uint32_t &stop) {
        std::mt19937 r(gSeed * 23 + 9);
        stop = NoiseAl(r, r() % 2);
        arena.Sim()->mFlagA = 0;
        arena.autonomy = false;
    };
    RunGather(
        ctx, 30000, 0x44ff80, GatherParams(),
        [&](WorldP &, GatherArena &arena) {
            uint32_t stop;
            args(arena, stop);
            original(arena.Sim(), &arena.list, stop);
        },
        [&](WorldP &, GatherArena &arena) {
            uint32_t stop;
            args(arena, stop);
            arena.Sim()->AppendInteractionsForMenu(&arena.list, stop);
        },
        cover);
    CheckCover(ctx, "AppendInteractionsForMenu", cover, cover.thrown > 100 && cover.listed > 5000 && cover.setUps > 5000 && cover.between > 500 && cover.names > 500);
}
