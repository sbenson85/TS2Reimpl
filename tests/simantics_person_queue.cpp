// Differential tests for the person's interaction bookkeeping (src/simantics/PersonInteractions.cpp): ClearActionQueue
// 0x512c30, RejectInteraction 0x5120c0, PublishActionQueue 0x520e40, PublishPendingQueue 0x5208a0, SelectInteraction 0x5205c0,
// StartInteraction 0x520360 and CountInteractionUse 0x523da0. The person and the fakes are the shared person world
// (tests/simantics_person_world.h); functions that call other functions of the group (Select, Start) run the whole cluster on
// both sides.
#include "simantics_person_world.h"

DIFF_TEST(cTSPerson_ClearActionQueue, 0x512c30)
{
    auto original = ctx.Original<VoidThis>(0x512c30);
    uint32_t emptyQueue = 0, big = 0, injected = 0, removed = 0;
    Params p;
    p.bigQueue = true;
    p.selfRemoving = true;
    RunSeeds(
        ctx, 20000, [&](WorldP &w, uint32_t seed) { BuildPersonWorld(w, seed, 0x512c30, p); },
        [&](uint8_t *o, void *, const void *) {
            Queues guard(o);
            original(o);
            return 0u;
        },
        [&](uint8_t *o, void *, const void *) {
            Queues guard(o);
            reinterpret_cast<PrimPersonObject *>(o)->ClearActionQueue();
            return 0u;
        },
        [&](WorldP &, const Trial &t) {
            const std::vector<uint32_t> &tr = t.oa.trace;
            emptyQueue += tr.size() < 40;
            big += tr.size() > 80;
            for (uint32_t word : tr) removed += word == 0x5e1f0000u;
            injected += t.oa.thrown == 0x66;
        });
    if (emptyQueue < 100 || big < 500 || injected < 100 || removed < 1000)
        ctx.Fail("coverage: empty %u big %u injected %u removed %u", emptyQueue, big, injected, removed);
}

DIFF_TEST(cTSPerson_RejectInteraction, 0x5120c0)
{
    auto original = ctx.Original<VoidThisOne>(0x5120c0);
    Params p;
    p.nullStackObjects = true;
    uint32_t noObject = 0, notified = 0;
    RunSeeds(
        ctx, 10000, [&](WorldP &w, uint32_t seed) { BuildPersonWorld(w, seed, 0x5120c0, p); },
        [&](uint8_t *o, void *, const void *) {
            Queues guard(o);
            original(o, &Interaction(WorldOf(o), gSeed));
            return 0u;
        },
        [&](uint8_t *o, void *, const void *) {
            Queues guard(o);
            reinterpret_cast<PrimPersonObject *>(o)->RejectInteraction(&Interaction(WorldOf(o), gSeed));
            return 0u;
        },
        [&](WorldP &, const Trial &t) {
            bool has238 = false;
            for (size_t i = 0; i + 1 < t.oa.trace.size(); ++i) has238 |= t.oa.trace[i + 1] == (238u << 8 | 5u);
            notified += has238;
            noObject += !has238 && t.oa.thrown < 0;
        });
    if (noObject < 300 || notified < 3000) ctx.Fail("coverage: no stack object %u notified %u", noObject, notified);
}

DIFF_TEST(cTSPerson_PublishActionQueue, 0x520e40)
{
    auto original = ctx.Original<VoidThis>(0x520e40);
    Params p;
    p.currentNullable = true;
    uint32_t emptyQueue = 0, completed = 0, injected = 0;
    RunSeeds(
        ctx, 20000, [&](WorldP &w, uint32_t seed) { BuildPersonWorld(w, seed, 0x520e40, p); },
        [&](uint8_t *o, void *, const void *) {
            Queues guard(o);
            original(o);
            return 0u;
        },
        [&](uint8_t *o, void *, const void *) {
            Queues guard(o);
            reinterpret_cast<PrimPersonObject *>(o)->PublishActionQueue();
            return 0u;
        },
        [&](WorldP &, const Trial &t) {
            injected += t.oa.thrown == 0x66;
            // (The first call writes the queue size: trace word 3.)
            emptyQueue += t.oa.thrown < 0 && t.oa.trace.size() > 4 && t.oa.trace[3] == 0u;
            completed += t.oa.thrown < 0 && t.oa.trace.size() > 20;
        });
    if (injected < 200 || completed < 300 || emptyQueue < 100) ctx.Fail("coverage: injected %u completed %u empty %u", injected, completed, emptyQueue);
}

DIFF_TEST(cTSPerson_PublishPendingQueue, 0x5208a0)
{
    auto original = ctx.Original<VoidThis>(0x5208a0);
    Params p;
    p.nullStackObjects = true;
    uint32_t injected = 0, empty = 0;
    RunSeeds(
        ctx, 20000, [&](WorldP &w, uint32_t seed) { BuildPersonWorld(w, seed, 0x5208a0, p); },
        [&](uint8_t *o, void *, const void *) {
            Queues guard(o);
            original(o);
            return 0u;
        },
        [&](uint8_t *o, void *, const void *) {
            Queues guard(o);
            reinterpret_cast<PrimPersonObject *>(o)->PublishPendingQueue();
            return 0u;
        },
        [&](WorldP &, const Trial &t) {
            bool headRead = false;
            for (size_t i = 0; i + 1 < t.oa.trace.size(); ++i) headRead |= t.oa.trace[i + 1] == (33u << 8 | 0u);
            injected += t.oa.thrown == 0x66;
            empty += t.oa.thrown < 0 && !headRead;
        });
    if (injected < 200 || empty < 200) ctx.Fail("coverage: injected %u empty %u", injected, empty);
}

DIFF_TEST(cTSPerson_SelectInteraction, 0x5205c0)
{
    auto original = ctx.Original<VoidThisOne>(0x5205c0);
    Params p;
    uint32_t cleared = 0, completed = 0, injected = 0;
    // The argument: mostly one of the interactions, sometimes the person's selected one (assigning it again does nothing).
    auto argument = [](uint8_t *o) {
        WorldP &w = WorldOf(o);
        auto *self = reinterpret_cast<PrimPersonObject *>(o);
        std::mt19937 r(gSeed + 1);
        void *arg = r() % 5 == 0 ? self->mSelectedInteraction : static_cast<void *>(&Interaction(w, r()));
        return arg ? arg : static_cast<void *>(&Interaction(w, 0));
    };
    RunSeeds(
        ctx, 30000, [&](WorldP &w, uint32_t seed) { BuildPersonWorld(w, seed, 0x5205c0, p); },
        [&](uint8_t *o, void *, const void *) {
            Queues guard(o);
            original(o, argument(o));
            return 0u;
        },
        [&](uint8_t *o, void *, const void *) {
            Queues guard(o);
            reinterpret_cast<PrimPersonObject *>(o)->SelectInteraction(argument(o));
            return 0u;
        },
        [&](WorldP &w, const Trial &t) {
            auto *self = reinterpret_cast<PrimPersonObject *>(w.obj);
            cleared += self->mMotiveBegin == self->mMotiveEnd;
            completed += t.oa.thrown < 0;
            injected += t.oa.thrown == 0x66;
        });
    if (cleared < 3000 || injected < 400 || completed < 5000) ctx.Fail("coverage: cleared %u completed %u injected %u", cleared, completed, injected);
}

DIFF_TEST(cTSPerson_StartInteraction, 0x520360)
{
    auto original = ctx.Original<VoidThisOne>(0x520360);
    Params p;
    p.nullStackObjects = true;
    p.nullTableEntries = true;
    p.bigQueue = true;
    uint32_t completed = 0, injected = 0;
    RunSeeds(
        ctx, 30000, [&](WorldP &w, uint32_t seed) { BuildPersonWorld(w, seed, 0x520360, p); },
        [&](uint8_t *o, void *, const void *) {
            Queues guard(o);
            std::mt19937 r(gSeed + 1);
            original(o, &Interaction(WorldOf(o), r()));
            return 0u;
        },
        [&](uint8_t *o, void *, const void *) {
            Queues guard(o);
            std::mt19937 r(gSeed + 1);
            reinterpret_cast<PrimPersonObject *>(o)->StartInteraction(&Interaction(WorldOf(o), r()));
            return 0u;
        },
        [&](WorldP &, const Trial &t) {
            completed += t.oa.thrown < 0;
            injected += t.oa.thrown == 0x66;
        });
    if (completed < 5000 || injected < 400) ctx.Fail("coverage: completed %u injected %u", completed, injected);
}

DIFF_TEST(cTSPerson_CountInteractionUse, 0x523da0)
{
    auto original = ctx.Original<VoidThisOne>(0x523da0);
    Params p;
    p.nullStackObjects = true;
    p.nullTableEntries = true;
    uint32_t viaTracker = 0, created = 0, bumped = 0, nullArg = 0, injected = 0;
    // The argument: one of the interactions, now and then none.
    auto argument = [](uint8_t *o) -> void * {
        std::mt19937 r(gSeed + 1);
        return r() % 25 == 0 ? nullptr : static_cast<void *>(&Interaction(WorldOf(o), r()));
    };
    RunSeeds(
        ctx, 40000, [&](WorldP &w, uint32_t seed) { BuildPersonWorld(w, seed, 0x523da0, p); },
        [&](uint8_t *o, void *, const void *) {
            Queues guard(o);
            original(o, argument(o));
            return 0u;
        },
        [&](uint8_t *o, void *, const void *) {
            Queues guard(o);
            reinterpret_cast<PrimPersonObject *>(o)->CountInteractionUse(argument(o));
            return 0u;
        },
        [&](WorldP &, const Trial &t) {
            auto has = [&](uint32_t word) {
                for (size_t i = 0; i + 1 < t.oa.trace.size(); ++i)
                    if (t.oa.trace[i + 1] == word) return true;
                return false;
            };
            viaTracker += has(0u << 8 | 2u);
            created += has(6u << 8 | 3u);
            bumped += has(10u << 8 | 3u);
            std::mt19937 r(gSeed + 1);
            nullArg += r() % 25 == 0;
            injected += t.oa.thrown == 0x66;
        });
    if (viaTracker < 1000 || created < 500 || bumped < 2000 || nullArg < 200 || injected < 400)
        ctx.Fail("coverage: tracker %u created %u bumped %u null argument %u injected %u", viaTracker, created, bumped, nullArg, injected);
}
