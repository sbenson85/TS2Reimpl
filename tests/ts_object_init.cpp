// Differential test of cTSObject::vf198 (0x5342d0), in the person primitives' recording world (tests/simantics_prims_person.h:
// an object with recording vtables for itself and its tree sim subobject at +4). In the vf198 test 0x531320 (ours below) and the
// tree sim's PrepareStack (0x5779d0 and ours, which has its own test) are logging stand-ins.
#include "simantics_prims_person.h"
#include "hook/hooks.h"
#include "simantics/TreeSim.h"
#include "ts/cTSObject.h"
#include "simantics/cTSBehaviorTree.h"

namespace {

void __fastcall ResetStandIn(void *self, void *, uint32_t keep)
{
    const uint32_t a[] = {keep & 0xff}; // a bool argument: only the low byte is defined
    HelperHit(0x531320, self, 1, a);
}

void __fastcall PrepareStackStandIn(void *self, void *, uint32_t reserve, uint32_t defaultArgs)
{
    const uint32_t a[] = {reserve, defaultArgs};
    HelperHit(0x5779d0, self, 2, a);
}

// Slot 197 gets a pointer to two (0, -1) word pairs on the caller's stack: logged by value.
void __fastcall Slot197(void *self, void *, const int16_t *pairs, uint32_t a2, uint32_t a3)
{
    Tick();
    gTrace.push_back(0x197197u);
    gTrace.push_back(Norm(reinterpret_cast<uint32_t>(self)));
    for (int i = 0; i < 4; ++i) gTrace.push_back(static_cast<uint16_t>(pairs[i]));
    gTrace.push_back(a2);
    gTrace.push_back(a3);
}

// Slot 352 lies beyond the harness's 256-slot vtables: the object gets this longer copy of its vtable.
void *gLongVTable[360];

uint32_t __fastcall Slot352(void *self, void *)
{
    const uint32_t value = Hit(self, 352, 0, nullptr);
    return value;
}

void BuildObjectInit(WorldP &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 0x5342d0);
    std::memset(&w, 0, sizeof(w));
    ResetScript(seed, r);
    w.canary = 0xC0DEC0DEu;
    FillRandom(w.obj, sizeof(w.obj), r);
    for (uint8_t &b : w.param) b = static_cast<uint8_t>(r());
    InitPerson(w);

    // The three word answers carry noise in the upper half (only the low word is stored).
    Use(w.vtThis, 66, 0);
    Use(w.vtThis, 67, 0);
    RetValue(w.obj, 66, Edge16(r) | (r() & 0xffff0000u), w);
    RetValue(w.obj, 67, Edge16(r) | (r() & 0xffff0000u), w);
    RetValue(w.obj, 352, Edge16(r) | (r() & 0xffff0000u), w);
    // +0x240 (kF4): slot 4 gives kF5, whose slot 3 answers the reserve.
    void *const owner = &w.fk[kF4];
    std::memcpy(w.obj + 0x240, &owner, sizeof(owner));
    Use(w.vtFk[kF4], 4, 0);
    RetObject(&w.fk[kF4], 4, &w.fk[kF5], w);
    Use(w.vtFk[kF5], 3, 1);
    RetValue(&w.fk[kF5], 3, r(), w);
    w.vtThis[197] = reinterpret_cast<void *>(&Slot197);
    std::memcpy(gLongVTable, w.vtThis, sizeof(w.vtThis));
    gLongVTable[352] = reinterpret_cast<void *>(&Slot352);
    reinterpret_cast<PrimPersonObject *>(w.obj)->mVTable = gLongVTable;
    Use(w.vtTree, 41, 0);
}

bool Always(const Outcome &)
{
    return true; // void
}

} // namespace

DIFF_TEST(TSObject_vf198, 0x5342d0)
{
    CodePatch reset(ctx, 0x531320, reinterpret_cast<void *>(&ResetStandIn));
    CodePatch resetOurs(MemberAddress(&cTSObject::ReloadReferences), reinterpret_cast<void *>(&ResetStandIn));
    CodePatch prepare(ctx, 0x5779d0, reinterpret_cast<void *>(&PrepareStackStandIn));
    CodePatch prepareOurs(MemberAddress(&cTSTreeSim::PrepareStack), reinterpret_cast<void *>(&PrepareStackStandIn));
    auto original = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x5342d0);
    uint32_t completed = 0, injected = 0;
    RunSeeds(
        ctx, 20000, BuildObjectInit,
        [&](uint8_t *o, void *, const void *p) {
            original(o, *static_cast<const uint32_t *>(p));
            return 0u;
        },
        [&](uint8_t *o, void *, const void *p) {
            reinterpret_cast<cTSObject *>(o)->vf198(*static_cast<const uint32_t *>(p));
            return 0u;
        },
        [&](WorldP &, const Trial &t) {
            if (t.oa.thrown >= 0) ++injected;
            else ++completed;
        },
        &Always);
    if (completed < 10000 || injected < 300) ctx.Fail("coverage: completed %u injected %u", completed, injected);
}

// ---- ReloadReferences (0x531320) and ResyncReferenceSource (0x53ea00) ------------------------------------------------

namespace {

// Fakes: kF4 the +0x240 owner, kF6 the definition, kF7 the owner's slot-32 object, kF8 its definition, kF9 our reference
// table, kF10 theirs, kF11..kF15 the reference vectors' entries.
constexpr int kEntryFakes = 5;

// Slot 17 (lookup by id): logged like any slot; the scripted answer is the object stored through the out pointer.
void __fastcall Lookup17(void *self, void *, int32_t id, uint32_t iid, void **out)
{
    const uint32_t a[] = {static_cast<uint32_t>(id), iid, reinterpret_cast<uint32_t>(out)};
    *out = reinterpret_cast<void *>(Hit(self, 17, 3, a));
}

// Our table's slot 8 refills the first vector: logged, then the vector's end moves over the scripted count of the
// pointers still in its storage (the entries before the clear, or the ones Build left beyond them).
void __fastcall Fill8(void *self, void *, uint32_t id, RefPtrVector *a, RefPtrVector *b, RefPtrVector *c)
{
    const uint32_t args[] = {id, reinterpret_cast<uint32_t>(a), reinterpret_cast<uint32_t>(b), reinterpret_cast<uint32_t>(c)};
    const uint32_t count = Hit(self, 8, 4, args) % 7;
    a->mLast = a->mFirst + count;
}

uint32_t __fastcall Slot329(void *self, void *)
{
    return Hit(self, 329, 0, nullptr);
}

void *gRefsVTable[360];

// Places a vector of reference-counted pointers in the scratch area: `count` entries (null or one of the entry fakes),
// storage for 8.
void PlaceRefs(WorldP &w, RefPtrVector *v, size_t scratchOffset, uint32_t count, std::mt19937 &r)
{
    void **storage = reinterpret_cast<void **>(w.scratch + scratchOffset);
    for (int i = 0; i < 8; ++i) storage[i] = r() % 5 == 0 ? nullptr : &w.fk[kF11 + r() % kEntryFakes];
    v->mFirst = storage;
    v->mLast = storage + count;
    v->mEnd = storage + 8;
}

// `resync`: the build for ResyncReferenceSource (vectors mostly empty, so it gets past its checks); otherwise for
// ReloadReferences (vectors mostly filled).
void BuildReferences(WorldP &w, uint32_t seed, bool resync)
{
    std::mt19937 r(seed * 2654435761u + (resync ? 0x53ea00 : 0x531320));
    std::memset(&w, 0, sizeof(w));
    ResetScript(seed, r);
    w.canary = 0xC0DEC0DEu;
    FillRandom(w.obj, sizeof(w.obj), r);
    for (uint8_t &b : w.param) b = static_cast<uint8_t>(r());
    InitPerson(w);
    uint8_t *const obj = w.obj;

    auto *refs = reinterpret_cast<RefPtrVector *>(obj + 0x248);
    for (int k = 0; k < 3; ++k) {
        const uint32_t count = resync ? (r() % 8 == 0 ? 1 + r() % 3 : 0) : r() % 6;
        PlaceRefs(w, &refs[k], 0x100 + k * 0x40, count, r);
    }
    void *const definition = r() % 12 == 0 ? nullptr : &w.fk[kF6];
    // No owner: ResyncReferenceSource stops; ReloadReferences faults in both (kept rare there).
    void *const owner = r() % (resync ? 12 : 80) == 0 ? nullptr : &w.fk[kF4];
    std::memcpy(obj + 0x238, &definition, 4);
    std::memcpy(obj + 0x240, &owner, 4);

    // The definitions' field reads, in call order: the table id (often non-zero), the source field, the table id again.
    if (r() % 3 == 0) { // the same source field on both sides
        const uint32_t id = 1 + r() % 0x7fff, source = r();
        EnqueueValue(&w.fk[kF6], 3, id, w);
        EnqueueValue(&w.fk[kF6], 3, source, w);
        EnqueueValue(&w.fk[kF8], 3, source, w);
        EnqueueValue(&w.fk[kF6], 3, id | (r() & 0xffff0000u), w);
    } else {
        for (int i = 0; i < 4; ++i) {
            const uint32_t pick = r() % 4;
            EnqueueValue(&w.fk[kF6], 3, pick == 0 ? 0u : pick == 1 ? (r() & 0xffff0000u) : r(), w);
        }
        EnqueueValue(&w.fk[kF8], 3, r(), w);
    }
    for (int k = 0; k < kFk; ++k) {
        Use(w.vtFk[k], 1, 0);
        Use(w.vtFk[k], 2, 0);
        Use(w.vtFk[k], 3, 1);
        Use(w.vtFk[k], 4, 0);
        Use(w.vtFk[k], 9, 1);
        Use(w.vtFk[k], 10, 2);
        Use(w.vtFk[k], 11, 0);
        Use(w.vtFk[k], 32, 0);
        w.vtFk[k][8] = reinterpret_cast<void *>(&Fill8);
        w.vtFk[k][17] = reinterpret_cast<void *>(&Lookup17);
    }
    // The entries' slot 8 takes the state word back (one argument).
    for (int k = 0; k < kEntryFakes; ++k) Use(w.vtFk[kF11 + k], 8, 1);

    RetValue(obj, 98, r() % 4 == 0 ? 2u | (r() & 0xffff0000u) : 2u, w);
    RetObject(&w.fk[kF4], 32, r() % 8 == 0 ? nullptr : &w.fk[kF7], w);
    RetObject(&w.fk[kF7], 4, r() % 8 == 0 ? nullptr : &w.fk[kF8], w);
    // Lookups: the owner gives our table (rarely none: ReloadReferences then faults in both), kF7 theirs.
    RetObject(&w.fk[kF4], 17, r() % (resync ? 6 : 40) == 0 ? nullptr : &w.fk[kF9], w);
    RetObject(&w.fk[kF7], 17, r() % 6 == 0 ? nullptr : &w.fk[kF10], w);
    RetValue(&w.fk[kF9], 9, NoiseAl(r, r() % 5 != 0), w);
    RetValue(&w.fk[kF10], 9, NoiseAl(r, r() % 5 != 0), w);
    // The tables' lists (slot 11): vectors in the scratch area of random lengths, often equal.
    auto *ourList = reinterpret_cast<RefPtrVector *>(w.scratch + 0x300);
    auto *theirList = reinterpret_cast<RefPtrVector *>(w.scratch + 0x320);
    PlaceRefs(w, ourList, 0x340, r() % 4, r);
    PlaceRefs(w, theirList, 0x380, r() % 2 ? ourList->Size() : r() % 4, r);
    RetObject(&w.fk[kF9], 11, ourList, w);
    RetObject(&w.fk[kF10], 11, theirList, w);
    RetValue(&w.fk[kF9], 8, r(), w);
    for (int k = 0; k < kEntryFakes; ++k) RetValue(&w.fk[kF11 + k], 4, r(), w);

    Use(w.vtThis, 66, 0);
    Use(w.vtThis, 67, 0);
    Use(w.vtThis, 98, 0);
    std::memcpy(gRefsVTable, w.vtThis, sizeof(w.vtThis));
    gRefsVTable[329] = reinterpret_cast<void *>(&Slot329);
    reinterpret_cast<PrimPersonObject *>(w.obj)->mVTable = gRefsVTable;
}

void BuildResync(WorldP &w, uint32_t seed)
{
    BuildReferences(w, seed, true);
}

void BuildReload(WorldP &w, uint32_t seed)
{
    BuildReferences(w, seed, false);
}

} // namespace

DIFF_TEST(TSObject_ResyncReferenceSource, 0x53ea00)
{
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x53ea00);
    uint32_t taken = 0, injected = 0;
    RunSeeds(
        ctx, 30000, BuildResync,
        [&](uint8_t *o, void *, const void *) {
            original(o);
            return 0u;
        },
        [&](uint8_t *o, void *, const void *) {
            reinterpret_cast<cTSObject *>(o)->ResyncReferenceSource();
            return 0u;
        },
        [&](WorldP &w, const Trial &t) {
            if (t.oa.thrown >= 0) ++injected;
            const uint32_t table = OffsetIn(&w.fk[kF9], w);
            for (size_t i = 0; i + 1 < t.oa.trace.size(); ++i)
                if (t.oa.trace[i] == table && t.oa.trace[i + 1] == (10u << 8 | 2u)) ++taken;
        },
        &Always);
    if (taken < 300 || injected < 300) ctx.Fail("coverage: list taken %u, injected %u", taken, injected);
}

DIFF_TEST(TSObject_ReloadReferences, 0x531320)
{
    // ResyncReferenceSource runs for real on both sides (original and ours; it has its own test above).
    auto original = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x531320);
    uint32_t restored = 0, injected = 0, crashed = 0;
    RunSeeds(
        ctx, 30000, BuildReload,
        [&](uint8_t *o, void *, const void *p) {
            // A bool argument: the upper bytes are noise the callee must ignore.
            original(o, (*static_cast<const uint8_t *>(p) & 1) | 0xabcdef00u);
            return 0u;
        },
        [&](uint8_t *o, void *, const void *p) {
            reinterpret_cast<cTSObject *>(o)->ReloadReferences((*static_cast<const uint8_t *>(p) & 1) != 0);
            return 0u;
        },
        [&](WorldP &w, const Trial &t) {
            if (t.oa.thrown >= 0x7000) ++crashed;
            else if (t.oa.thrown >= 0) ++injected;
            for (int k = 0; k < kEntryFakes; ++k) {
                const uint32_t entry = OffsetIn(&w.fk[kF11 + k], w);
                for (size_t i = 0; i + 1 < t.oa.trace.size(); ++i)
                    if (t.oa.trace[i] == entry && t.oa.trace[i + 1] == (8u << 8 | 1u)) ++restored;
            }
        },
        &Always);
    if (restored < 3000 || injected < 1000 || crashed > 1500) ctx.Fail("coverage: restored %u injected %u crashed %u", restored, injected, crashed);
}

// ---- vf244 (0x52df60) --------------------------------------------------------------------------------------------------

namespace {

// The tree sim's slot 15 (CallTree) takes eight arguments, more than the harness's thunks.
void __fastcall CallTree15(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7,
                           uint32_t a8)
{
    const uint32_t a[] = {a1, a2, a3, a4, a5, a6, a7, a8};
    Hit(self, 15, 8, a);
}

// Fakes: kF4 the tree owner, kF2 the context (+0x40), kF3 its object list, kF11..kF14 contained objects, kF16..kF19 the
// objects the list gives.
void BuildEvent(WorldP &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 0x52df60);
    std::memset(&w, 0, sizeof(w));
    ResetScript(seed, r);
    w.canary = 0xC0DEC0DEu;
    FillRandom(w.obj, sizeof(w.obj), r);
    for (uint8_t &b : w.param) b = static_cast<uint8_t>(r());
    InitPerson(w);
    auto *self = reinterpret_cast<PrimPersonObject *>(w.obj);
    self->mEnvironment = &w.fk[kF2];

    Use(w.vtThis, 89, 0);
    RetObject(w.obj, 89, &w.fk[kF4], w);
    Use(w.vtFk[kF4], 31, 3);
    RetValue(&w.fk[kF4], 31, r() % 3 == 0 ? (r() & 0xffff0000u) : r(), w); // the tree id is the low word
    w.vtTree[15] = reinterpret_cast<void *>(&CallTree15);

    // Up to five contained objects (a negative count now and then); each is null or a fake with an id (asked twice).
    Use(w.vtThis, 66, 0);
    Use(w.vtThis, 68, 1);
    const uint32_t count = r() % 8 == 0 ? 0xfffffffeu : r() % 6;
    RetValue(w.obj, 66, count, w);
    for (uint32_t i = 0; i < 6; ++i) {
        const int pick = static_cast<int>(r() % 5);
        EnqueueObject(w.obj, 68, pick == 4 ? nullptr : &w.fk[kF11 + pick], w);
    }
    for (int k = 0; k < 4; ++k) {
        Use(w.vtFk[kF11 + k], 4, 0);
        for (int n = 0; n < 4; ++n) EnqueueValue(&w.fk[kF11 + k], 4, r() % 4 == 0 ? (r() & 0xffff0000u) : r(), w);
    }
    Use(w.vtFk[kF2], 26, 0);
    RetObject(&w.fk[kF2], 26, &w.fk[kF3], w);
    Use(w.vtFk[kF3], 29, 1);
    for (int n = 0; n < 6; ++n) {
        const int pick = static_cast<int>(r() % 5);
        EnqueueObject(&w.fk[kF3], 29, pick == 4 ? nullptr : &w.fk[kF16 + pick], w);
    }
    for (int k = 0; k < 4; ++k) Use(w.vtFk[kF16 + k], 244, 4);
}

} // namespace

DIFF_TEST(TSObject_vf244, 0x52df60)
{
    auto original = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t)>(0x52df60);
    uint32_t calledTree = 0, sent = 0, injected = 0;
    RunSeeds(
        ctx, 30000, BuildEvent,
        [&](uint8_t *o, void *, const void *p) {
            const uint8_t *b = static_cast<const uint8_t *>(p);
            uint32_t event, argument;
            std::memcpy(&event, b, 4);
            std::memcpy(&argument, b + 8, 4);
            // The two flags are bytes: the upper bytes are noise the callee must ignore.
            original(o, event, (b[4] & 1) | 0x5a5a5a00u, (b[5] & 1) | 0xa5a5a500u, argument);
            return 0u;
        },
        [&](uint8_t *o, void *, const void *p) {
            const uint8_t *b = static_cast<const uint8_t *>(p);
            uint32_t event, argument;
            std::memcpy(&event, b, 4);
            std::memcpy(&argument, b + 8, 4);
            reinterpret_cast<cTSObject *>(o)->vf244(event, (b[4] & 1) != 0, (b[5] & 1) != 0, argument);
            return 0u;
        },
        [&](WorldP &w, const Trial &t) {
            if (t.oa.thrown >= 0) ++injected;
            for (size_t i = 0; i + 1 < t.oa.trace.size(); ++i) {
                if (t.oa.trace[i + 1] == (15u << 8 | 8u)) ++calledTree;
                if (t.oa.trace[i + 1] == (244u << 8 | 4u)) ++sent;
            }
            (void)w;
        },
        &Always);
    if (calledTree < 3000 || sent < 5000 || injected < 500) ctx.Fail("coverage: tree %u sent %u injected %u", calledTree, sent, injected);
}

// ---- vf249 (0x530e80) --------------------------------------------------------------------------------------------------

namespace {

uint32_t __fastcall ResumeStandIn(void *self, void *, uint32_t frame, uint32_t node)
{
    const uint32_t a[] = {frame, node};
    return HelperHit(0x577810, self, 2, a);
}

// Fakes: kF0 the +0x48 services object, kF1 its helper, kF5 the frame, kF6 the node.
void BuildResume(WorldP &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 0x530e80);
    std::memset(&w, 0, sizeof(w));
    ResetScript(seed, r);
    w.canary = 0xC0DEC0DEu;
    FillRandom(w.obj, sizeof(w.obj), r);
    for (uint8_t &b : w.param) b = static_cast<uint8_t>(r());
    InitPerson(w);
    auto *self = reinterpret_cast<PrimPersonObject *>(w.obj);
    self->mHolder48 = &w.fk[kF0];
    // The flags at +0x230: bits 6 and 7 in all four combinations.
    uint32_t flags;
    std::memcpy(&flags, w.obj + 0x230, 4);
    flags = (flags & ~0xc0u) | ((r() % 4) << 6);
    std::memcpy(w.obj + 0x230, &flags, 4);

    Use(w.vtFk[kF0], 6, 0);
    RetObject(&w.fk[kF0], 6, r() % 4 == 0 ? nullptr : &w.fk[kF1], w);
    Use(w.vtFk[kF1], 3, 3);
    Use(w.vtFk[kF5], 22, 0);
    Use(w.vtFk[kF5], 23, 1);
    RetValue(&w.fk[kF5], 22, NoiseAl(r, r() % 3 == 0), w);
    Use(w.vtFk[kF6], 18, 0);
    Use(w.vtFk[kF6], 19, 1);
    RetValue(&w.fk[kF6], 18, NoiseAl(r, r() % 2 == 0), w);
    Use(w.vtTree, 13, 2);
}

} // namespace

DIFF_TEST(TSObject_vf249, 0x530e80)
{
    CodePatch resume(ctx, 0x577810, reinterpret_cast<void *>(&ResumeStandIn));
    CodePatch resumeOurs(MemberAddress(&cTSTreeSim::ResumeFrame), reinterpret_cast<void *>(&ResumeStandIn));
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, void *, void *)>(0x530e80);
    uint32_t paths[5] = {}, injected = 0;
    RunSeeds(
        ctx, 30000, BuildResume, [&](uint8_t *o, void *, const void *) { return original(o, &gW->fk[kF5], &gW->fk[kF6]); },
        [&](uint8_t *o, void *, const void *) { return reinterpret_cast<cTSObject *>(o)->vf249(&gW->fk[kF5], &gW->fk[kF6]); },
        [&](WorldP &w, const Trial &t) {
            if (t.oa.thrown >= 0) {
                ++injected;
                return;
            }
            const uint32_t helper = OffsetIn(&w.fk[kF1], w), frame = OffsetIn(&w.fk[kF5], w), node = OffsetIn(&w.fk[kF6], w);
            int path = 4; // none ran
            for (size_t i = 0; i + 1 < t.oa.trace.size(); ++i) {
                if (t.oa.trace[i] == 0x80577810u) path = 0;
                if (t.oa.trace[i] == helper && t.oa.trace[i + 1] == (3u << 8 | 3u)) path = 1;
                if (t.oa.trace[i] == frame && t.oa.trace[i + 1] == (23u << 8 | 1u)) path = 2;
                if (t.oa.trace[i] == node && t.oa.trace[i + 1] == (19u << 8 | 1u)) path = 3;
            }
            ++paths[path];
        });
    for (uint32_t p : paths)
        if (p < 1000) ctx.Fail("coverage: paths %u %u %u %u %u injected %u", paths[0], paths[1], paths[2], paths[3], paths[4], injected);
}
