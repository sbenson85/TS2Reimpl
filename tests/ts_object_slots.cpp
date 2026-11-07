// Differential tests of cTSObject primary-vtable slots (src/ts/cTSObjectSlots.cpp) in the person primitives' recording world
// (tests/simantics_prims_person.h: an object with recording vtables for itself and its tree sim subobject at +4).
#include "simantics_prims_person.h"
#include "hook/hooks.h"
#include "rz/StaticGetters.h"
#include "rz/cGZAllocatorService.h"
#include "simantics/FindBestAction.h"
#include "simantics/TreeSim.h"
#include "ts/cTSString.h"
#include "ts/cTSObject.h"

namespace {

// The common start of every world: random object bytes (top bit of each dword set), the vtables, a fresh script.
std::mt19937 BaseWorld(WorldP &w, uint32_t seed, uint32_t salt, uint32_t throwWithin = 40)
{
    std::mt19937 r(seed * 2654435761u + salt);
    std::memset(&w, 0, sizeof(w));
    ResetScript(seed, r);
    if (gThrowAt) gThrowAt = 1 + (gThrowAt - 1) % throwWithin; // an injected error should land inside the calls this function makes
    w.canary = 0xC0DEC0DEu;
    FillRandom(w.obj, sizeof(w.obj), r);
    for (uint8_t &b : w.param) b = static_cast<uint8_t>(r());
    InitPerson(w);
    return r;
}

bool Always(const Outcome &)
{
    return true; // void results are not compared
}

template <typename T> T &Field(WorldP &w, size_t offset)
{
    return *reinterpret_cast<T *>(w.obj + offset);
}

} // namespace

// ---- vf276 (0x53d980) ---------------------------------------------------------------------------------------------------

namespace {

void BuildStore(WorldP &w, uint32_t seed)
{
    std::mt19937 r = BaseWorld(w, seed, 0x53d980);
    (void)r;
}

} // namespace

DIFF_TEST(TSObject_vf276, 0x53d980)
{
    auto original = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x53d980);
    RunSeeds(
        ctx, 300, BuildStore,
        [&](uint8_t *o, void *, const void *p) {
            original(o, *reinterpret_cast<const uint32_t *>(p));
            return 0u;
        },
        [&](uint8_t *o, void *, const void *p) {
            reinterpret_cast<cTSObject *>(o)->vf276(*reinterpret_cast<const uint32_t *>(p));
            return 0u;
        },
        [&](WorldP &, const Trial &) {}, &Always);
}

// ---- vf205 (0x53d1a0) ---------------------------------------------------------------------------------------------------

namespace {

uint32_t __fastcall StartTreeStandIn(void *self, void *, uint32_t owner, uint32_t tree, uint32_t value)
{
    const uint32_t a[] = {owner, tree, value};
    return HelperHit(0x577e10, self, 3, a);
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

void BuildRestartTree(WorldP &w, uint32_t seed)
{
    std::mt19937 r = BaseWorld(w, seed, 0x53d1a0, 5);
    Use(w.vtThis, 89, 0);
    RetObject(w.obj, 89, &w.fk[kF4], w);
    Use(w.vtThis, 93, 1);
    RetValue(w.obj, 93, Edge16(r) | (r() & 0xffff0000u), w); // only the low word is the tree id
    w.vtThis[197] = reinterpret_cast<void *>(&Slot197);
}

} // namespace

DIFF_TEST(TSObject_vf205, 0x53d1a0)
{
    CodePatch start(ctx, 0x577e10, reinterpret_cast<void *>(&StartTreeStandIn));
    CodePatch startOurs(MemberAddress(&cTSTreeSim::StartTree), reinterpret_cast<void *>(&StartTreeStandIn));
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x53d1a0);
    uint32_t completed = 0, injected = 0;
    RunSeeds(
        ctx, 20000, BuildRestartTree,
        [&](uint8_t *o, void *, const void *) {
            original(o);
            return 0u;
        },
        [&](uint8_t *o, void *, const void *) {
            reinterpret_cast<cTSObject *>(o)->vf205();
            return 0u;
        },
        [&](WorldP &, const Trial &t) {
            if (t.oa.thrown >= 0) ++injected;
            else ++completed;
        },
        &Always);
    if (completed < 10000 || injected < 300) ctx.Fail("coverage: completed %u injected %u", completed, injected);
}

// ---- vf223 (0x546620) ---------------------------------------------------------------------------------------------------

namespace {

void *gLongVTable[360];

// Fakes: kF2 the +0x40 object, kF3 the object its slot 18 gives (slot 21 gets this object's id).
void BuildRefresh(WorldP &w, uint32_t seed)
{
    std::mt19937 r = BaseWorld(w, seed, 0x546620, 8);
    reinterpret_cast<PrimPersonObject *>(w.obj)->mEnvironment = &w.fk[kF2];
    Field<uint8_t>(w, 0x292) = r() % 3 == 0 ? static_cast<uint8_t>(1 + r() % 255) : 0;
    Field<uint8_t>(w, 0x291) = static_cast<uint8_t>(r());
    Field<uint8_t>(w, 0x2a6) = static_cast<uint8_t>(r());

    Use(w.vtThis, 219, 0);
    RetValue(w.obj, 219, NoiseAl(r, r() % 5 == 0), w);
    Use(w.vtThis, 71, 0);
    const uint32_t pick = r() % 4;
    RetValue(w.obj, 71, pick == 0 ? 0u : pick == 1 ? 0x100u << (r() % 3 * 8) : r(), w); // zero in the low byte only: still non-zero
    Use(w.vtThis, 47, 0);
    RetValue(w.obj, 47, r() % 3 == 0 ? (r() & 0xffff0000u) : r(), w); // the id is the low word
    Use(w.vtFk[kF2], 18, 0);
    RetObject(&w.fk[kF2], 18, r() % 4 == 0 ? nullptr : &w.fk[kF3], w);
    Use(w.vtFk[kF3], 21, 1);
    // Slots 330 and 331 lie beyond the harness's 256-slot vtables: the object gets a longer copy of its vtable.
    std::memcpy(gLongVTable, w.vtThis, sizeof(w.vtThis));
    gLongVTable[330] = reinterpret_cast<void *>(&Th<330, 0>::F);
    gLongVTable[331] = reinterpret_cast<void *>(&Th<331, 0>::F);
    reinterpret_cast<PrimPersonObject *>(w.obj)->mVTable = gLongVTable;
}

} // namespace

DIFF_TEST(TSObject_vf223, 0x546620)
{
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x546620);
    uint32_t guarded = 0, skippedByFlag = 0, told = 0, resetPath = 0, injected = 0;
    RunSeeds(
        ctx, 30000, BuildRefresh,
        [&](uint8_t *o, void *, const void *) {
            original(o);
            return 0u;
        },
        [&](uint8_t *o, void *, const void *) {
            reinterpret_cast<cTSObject *>(o)->vf223();
            return 0u;
        },
        [&](WorldP &w, const Trial &t) {
            if (t.oa.thrown >= 0) ++injected;
            const uint32_t target = OffsetIn(&w.fk[kF3], w);
            bool ranBody = false;
            for (size_t i = 0; i + 1 < t.oa.trace.size(); ++i) {
                if (t.oa.trace[i] == 0 && t.oa.trace[i + 1] == (71u << 8)) ranBody = true;
                if (t.oa.trace[i] == 0 && t.oa.trace[i + 1] == (330u << 8)) ++resetPath;
                if (t.oa.trace[i] == target && t.oa.trace[i + 1] == (21u << 8 | 1u)) ++told;
            }
            if (ranBody) ++guarded;
            else if (t.oa.trace.size() == 2) ++skippedByFlag; // only slot 219 ran
        },
        &Always);
    if (guarded < 5000 || skippedByFlag < 3000 || told < 3000 || resetPath < 1500 || injected < 300)
        ctx.Fail("coverage: body %u skipped %u told %u reset %u injected %u", guarded, skippedByFlag, told, resetPath, injected);
}

// ---- vf182 (0x540560) ---------------------------------------------------------------------------------------------------

namespace {

constexpr int kAllocatorFake = 20;
constexpr int kEntries = 5;      // kF11..kF15: the objects the +0x74 subobject lists
constexpr int kInterfaces = 4;   // kF16..kF19: the interfaces the entries give

struct ListScript {
    uint32_t count = 0;   // entries listed
    bool storage = true;  // false: the vector stays empty with no storage (nothing to free)
    uint32_t pick[8] = {};
} gList;

// The +0x74 subobject's slot 10 fills the vector it is given: `count` entries in the scratch area (storage for 8).
void __fastcall Fill10(void *self, void *, RefPtrVector *v)
{
    const uint32_t a[] = {reinterpret_cast<uint32_t>(v)};
    Hit(self, 10, 1, a);
    if (!gList.storage) {
        v->mFirst = v->mLast = v->mEnd = nullptr;
        return;
    }
    void **storage = reinterpret_cast<void **>(gW->scratch + 0x100);
    for (uint32_t i = 0; i < gList.count; ++i) storage[i] = &gW->fk[kF11 + gList.pick[i] % kEntries];
    v->mFirst = storage;
    v->mLast = storage + gList.count;
    v->mEnd = storage + 8;
}

// An entry's QueryInterface (slot 0): logged and scripted; a non-zero al stores the interface fake for the entry.
uint32_t __fastcall EntryQuery(void *self, void *, uint32_t iid, void **out)
{
    const uint32_t a[] = {iid, reinterpret_cast<uint32_t>(out)};
    const uint32_t answer = Hit(self, 0, 2, a);
    if (static_cast<uint8_t>(answer) != 0) {
        const int entry = static_cast<int>((static_cast<Fake *>(self) - &gW->fk[kF11]));
        *out = &gW->fk[kF16 + entry % kInterfaces];
    }
    return answer;
}

// The allocator service's free (slot 4): logged without ticking (it runs while an error unwinds, where nothing may throw).
void __fastcall ServiceFree(void *self, void *, void *mem)
{
    gTrace.push_back(Off(self));
    gTrace.push_back(4u << 8 | 1u);
    gTrace.push_back(Norm(reinterpret_cast<uint32_t>(mem)));
}

void BuildOffer(WorldP &w, uint32_t seed)
{
    std::mt19937 r = BaseWorld(w, seed, 0x540560, 14);
    gList.storage = r() % 8 != 0;
    gList.count = r() % 8 == 0 ? 0 : 1 + r() % 5;
    for (uint32_t &p : gList.pick) p = r();
    // The subobject at +0x74 keeps its vtable pointer in the object's bytes: the person vtable array stands in for it.
    void **const vt = w.vtPerson;
    std::memcpy(w.obj + 0x74, &vt, sizeof(vt));
    vt[10] = reinterpret_cast<void *>(&Fill10);
    for (int k = 0; k < kEntries; ++k) {
        void **const evt = w.vtFk[kF11 + k];
        evt[0] = reinterpret_cast<void *>(&EntryQuery);
        Use(evt, 2, 0);
        // Whether each question gets an answer: asked at most once per list position, so a queue per entry.
        for (int n = 0; n < 6; ++n) EnqueueValue(&w.fk[kF11 + k], 0, NoiseAl(r, r() % 3 == 0), w);
    }
    for (int k = 0; k < kInterfaces; ++k) {
        Use(w.vtFk[kF16 + k], 6, 6);
        Use(w.vtFk[kF16 + k], 2, 0);
        RetValue(&w.fk[kF16 + k], 6, NoiseAl(r, r() % 2 == 0), w);
    }
    w.vtFk[kAllocatorFake][4] = reinterpret_cast<void *>(&ServiceFree);
}

} // namespace

DIFF_TEST(TSObject_vf182, 0x540560)
{
    void **const cache = ctx.image.At<void *>(0x10c95d4);
    int *const guard = ctx.image.At<int>(0x10c95d8);
    const struct Saved {
        void **cache;
        int *guard;
        void *cacheValue;
        int guardValue;
        ~Saved()
        {
            *cache = cacheValue;
            *guard = guardValue;
            gBeforeRun = nullptr;
        }
    } saved{cache, guard, *cache, *guard};
    // Every run finds the world's allocator fake in the (initialised) function-local cache both sides share.
    gBeforeRun = [&](WorldP &w) {
        *cache = &w.fk[kAllocatorFake];
        *guard = INT32_MIN;
    };
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x540560);
    uint32_t answered = 0, none = 0, freed = 0, injected = 0, released = 0;
    RunSeeds(
        ctx, 30000, BuildOffer,
        [&](uint8_t *o, void *, const void *p) {
            const uint32_t *a = static_cast<const uint32_t *>(p);
            return static_cast<uint32_t>(original(o, a[0], a[1], a[2], a[3], a[4], a[5]));
        },
        [&](uint8_t *o, void *, const void *p) {
            const uint32_t *a = static_cast<const uint32_t *>(p);
            return static_cast<uint32_t>(reinterpret_cast<cTSObject *>(o)->vf182(a[0], a[1], a[2], a[3], a[4], a[5]));
        },
        [&](WorldP &w, const Trial &t) {
            if (t.oa.thrown >= 0) ++injected;
            bool sixCalled = false;
            for (size_t i = 0; i + 1 < t.oa.trace.size(); ++i) {
                if (t.oa.trace[i + 1] == (6u << 8 | 6u)) sixCalled = true;
                if (t.oa.trace[i] == OffsetIn(&w.fk[kAllocatorFake], w) && t.oa.trace[i + 1] == (4u << 8 | 1u)) ++freed;
                if ((t.oa.trace[i + 1] & 0xffffff00u) == 2u << 8 && t.oa.trace[i] != 0) ++released;
            }
            if (sixCalled) ++answered;
            else if (t.oa.thrown < 0) ++none;
        },
        nullptr);
    if (answered < 5000 || none < 3000 || freed < 10000 || injected < 300 || released < 10000)
        ctx.Fail("coverage: answered %u none %u freed %u injected %u released %u", answered, none, freed, injected, released);
}

// ---- vf259 (0x52ec10) ---------------------------------------------------------------------------------------------------

namespace {

void *gLongDecay[360];

// The object's slot 0 (QueryInterface): logged and scripted; the interface it stores is the scripted object for pseudo slot 256.
uint32_t __fastcall DecayQuery(void *self, void *, uint32_t iid, void **out)
{
    const uint32_t a[] = {iid, reinterpret_cast<uint32_t>(out)};
    const uint32_t answer = Hit(self, 0, 2, a);
    *out = reinterpret_cast<void *>(Scripted(Off(self), 256));
    return answer;
}

// Fakes: kF2 the interface the query gives, kF3 another object, kF4 the +0x40 object, kF5 what its slot 24 gives, kF6 what that
// gives for slot 22, kF7 the limits it leads to, kF8 the definition at +0x238, kF9 the service singleton, kF10 its slot 23 object.
void BuildDecay(WorldP &w, uint32_t seed)
{
    std::mt19937 r = BaseWorld(w, seed, 0x52ec10, 24);
    auto *self = reinterpret_cast<PrimPersonObject *>(w.obj);
    self->mEnvironment = &w.fk[kF4];
    const void *definition = r() % 6 == 0 ? nullptr : &w.fk[kF8];
    std::memcpy(w.obj + 0x238, &definition, 4);
    Field<uint16_t>(w, 0x14c) = r() % 8 == 0 ? 0 : static_cast<uint16_t>(r());

    Use(w.vtThis, 104, 0);
    RetValue(w.obj, 104, NoiseAl(r, r() % 2 == 0), w);
    gLongDecay[0] = nullptr;
    // The chain to the limits: each step usually leads on.
    RetObject(&w.fk[kF2], 6, r() % 6 == 0 ? static_cast<const void *>(&w.fk[kF3]) : &w.fk[kF2], w);
    RetObject(w.obj, 256, r() % 6 == 0 ? nullptr : &w.fk[kF2], w);
    RetObject(&w.fk[kF4], 24, &w.fk[kF5], w);
    RetObject(&w.fk[kF5], 22, r() % 6 == 0 ? nullptr : &w.fk[kF6], w);
    RetObject(&w.fk[kF6], 4, r() % 6 == 0 ? nullptr : &w.fk[kF7], w);
    Use(w.vtFk[kF2], 6, 0);
    Use(w.vtFk[kF2], 2, 0);
    Use(w.vtFk[kF3], 6, 0);
    Use(w.vtFk[kF3], 2, 0);
    Use(w.vtFk[kF4], 24, 0);
    Use(w.vtFk[kF5], 22, 1);
    Use(w.vtFk[kF6], 4, 0);

    // The limits objects (the same shape for both): fields (slot 3) in call order 0x25, 0x23, 0x26, steps (slot 5) 1 or 2 calls.
    for (int k : {kF7, kF8}) {
        Use(w.vtFk[k], 1, 0);
        Use(w.vtFk[k], 2, 0);
        Use(w.vtFk[k], 3, 1);
        Use(w.vtFk[k], 5, 1);
        const uint32_t pick = r() % 4;
        EnqueueValue(&w.fk[k], 3, pick == 0 ? 0u : pick == 1 ? (0x10000u << (r() % 3 * 8)) : r(), w);                       // field 0x25
        EnqueueValue(&w.fk[k], 3, r() % 3 == 0 ? 0u : r(), w);                                                              // field 0x23
        EnqueueValue(&w.fk[k], 3, r() % 2 ? static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(r()))) : r(), w); // field 0x26
        EnqueueValue(&w.fk[k], 5, r(), w);
        EnqueueValue(&w.fk[k], 5, r(), w);
    }
    // The service singleton, set per run in the test body; its slot 23 gives the timer (rarely none), whose slot 21 answers in al.
    Use(w.vtFk[kF9], 23, 0);
    RetObject(&w.fk[kF9], 23, r() % 8 == 0 ? nullptr : &w.fk[kF10], w);
    Use(w.vtFk[kF10], 21, 0);
    RetValue(&w.fk[kF10], 21, NoiseAl(r, r() % 3 == 0), w);

    std::memcpy(gLongDecay, w.vtThis, sizeof(w.vtThis));
    gLongDecay[0] = reinterpret_cast<void *>(&DecayQuery);
    gLongDecay[258] = reinterpret_cast<void *>(&Th<258, 0>::F);
    reinterpret_cast<PrimPersonObject *>(w.obj)->mVTable = gLongDecay;
}

} // namespace

DIFF_TEST(TSObject_vf259, 0x52ec10)
{
    void **const singleton = ctx.image.At<void *>(0x10cc288);
    const struct Saved {
        void **slot;
        void *value;
        ~Saved()
        {
            *slot = value;
            gBeforeRun = nullptr;
        }
    } saved{singleton, *singleton};
    gBeforeRun = [&](WorldP &w) { *singleton = &w.fk[kF9]; };
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x52ec10);
    uint32_t viaInterface = 0, viaDefinition = 0, earlyEnd = 0, changed = 0, floored = 0, injected = 0;
    RunSeeds(
        ctx, 30000, BuildDecay,
        [&](uint8_t *o, void *, const void *) {
            original(o);
            return 0u;
        },
        [&](uint8_t *o, void *, const void *) {
            reinterpret_cast<cTSObject *>(o)->vf259();
            return 0u;
        },
        [&](WorldP &w, const Trial &t) {
            if (t.oa.thrown >= 0) {
                ++injected;
                return;
            }
            const uint32_t limits = OffsetIn(&w.fk[kF7], w), definition = OffsetIn(&w.fk[kF8], w);
            bool askedField = false, reachedEnd = false;
            for (size_t i = 0; i + 1 < t.oa.trace.size(); ++i) {
                if (t.oa.trace[i] == OffsetIn(&w.fk[kF9], w) && t.oa.trace[i + 1] == (23u << 8)) reachedEnd = true; // the singleton asked
                if (t.oa.trace[i] == limits && t.oa.trace[i + 1] == (3u << 8 | 1u)) askedField = true;
                if (t.oa.trace[i] == limits && t.oa.trace[i + 1] == (1u << 8)) ++viaInterface;
                if (t.oa.trace[i] == definition && t.oa.trace[i + 1] == (1u << 8)) ++viaDefinition;
                if ((t.oa.trace[i] == limits || t.oa.trace[i] == definition) && t.oa.trace[i + 1] == (5u << 8 | 1u) &&
                    i + 2 < t.oa.trace.size() && t.oa.trace[i + 2] == 0x26)
                    ++floored;
            }
            if (!reachedEnd) ++earlyEnd;
            changed += askedField ? 1 : 0;
        },
        &Always);
    if (viaInterface < 1000 || viaDefinition < 1000 || earlyEnd < 1000 || changed < 3000 || floored < 500 || injected < 500)
        ctx.Fail("coverage: interface %u definition %u early %u fields %u floor %u injected %u", viaInterface, viaDefinition, earlyEnd, changed,
                 floored, injected);
}

// ---- vf284 (0x53a290) ---------------------------------------------------------------------------------------------------

namespace {

constexpr int kTargets = 4; // kF16..kF19: the objects the list gives for the contained ones

void *gLongTargets[kTargets][300];
std::vector<uint16_t> gIds;                              // the vector both sides append to (the same address on both)
const char *const gNames[] = {nullptr, "", "a", "TreeName", "a name with spaces and a longer text that is past the small string"};

// The tree owner's slot 31: the game string is logged by content (it lives on the caller's stack).
uint32_t __fastcall NameToTree(void *self, void *, const cRZString *text, uint32_t a2, uint32_t a3)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(31u << 8 | 3u);
    gTrace.push_back(0x57a70000u | text->mString.size());
    for (char c : text->mString) gTrace.push_back(static_cast<uint8_t>(c));
    gTrace.push_back(a2);
    gTrace.push_back(a3);
    return Scripted(Off(self), 31);
}

uint32_t __fastcall CallTreeAnswer(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7,
                                   uint32_t a8)
{
    const uint32_t a[] = {a1, a2, a3, a4, a5, a6, a7, a8};
    return Hit(self, 15, 8, a);
}

// A target's slot 284 (the recursion): logged with all six arguments, answers from the script.
uint32_t __fastcall Collect284(void *self, void *, void *ids, uint32_t a2, const char *name, uint32_t d, uint32_t e, uint32_t f)
{
    const uint32_t a[] = {reinterpret_cast<uint32_t>(ids), a2, reinterpret_cast<uint32_t>(name), d, e, f};
    return Hit(self, 284, 6, a);
}

// Fakes: kF4 the tree owner, kF2 the +0x40 context, kF3 its object list, kF11..kF14 contained objects, kF16..kF19 the targets.
void BuildCollect(WorldP &w, uint32_t seed)
{
    std::mt19937 r = BaseWorld(w, seed, 0x53a290, 24);
    reinterpret_cast<PrimPersonObject *>(w.obj)->mEnvironment = &w.fk[kF2];
    Use(w.vtThis, 98, 0);
    RetValue(w.obj, 98, r() % 8 == 0 ? 2u : r() % 3 == 0 ? (r() & 0xffffff00u) | 2u : r() % 5, w); // 2 in the low dword only: kind 2
    Use(w.vtThis, 89, 0);
    RetObject(w.obj, 89, &w.fk[kF4], w);
    w.vtFk[kF4][31] = reinterpret_cast<void *>(&NameToTree);
    RetValue(&w.fk[kF4], 31, r() % 4 == 0 ? (r() & 0xffff0000u) : r() % 4 == 0 ? 0u : r(), w); // the tree id is the low word
    w.vtTree[15] = reinterpret_cast<void *>(&CallTreeAnswer);
    RetValue(w.obj + 4, 15, NoiseAl(r, r() % 4 != 0), w);

    Use(w.vtThis, 66, 0);
    Use(w.vtThis, 68, 1);
    RetValue(w.obj, 66, r() % 8 == 0 ? 0xfffffffeu : r() % 5, w);
    for (int n = 0; n < 6; ++n) {
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
    for (int k = 0; k < kTargets; ++k) {
        std::memset(gLongTargets[k], 0, sizeof(gLongTargets[k]));
        gLongTargets[k][284] = reinterpret_cast<void *>(&Collect284);
        w.fk[kF16 + k].vt = gLongTargets[k];
        RetValue(&w.fk[kF16 + k], 284, NoiseAl(r, r() % 3 != 0), w);
    }
}

struct Snapshot {
    std::vector<uint16_t> values;
    size_t capacity = 0;
};
Snapshot gSnapOriginal, gSnapOurs;

// The arguments of one trial, from the parameter bytes: [0] flags (skip own, skip children, e), [1] name, [2] initial size, [3] spare capacity,
// [4..7] d. The flag bytes carry noise above the low byte where the code only looks at the low byte.
struct CollectArgs {
    uint32_t skipOwn, skipChildren, e, d;
    const char *name;
    size_t size, spare;
};

CollectArgs ArgsOf(const void *p)
{
    const uint8_t *b = static_cast<const uint8_t *>(p);
    CollectArgs a;
    a.skipOwn = (b[0] & 1u ? 0x80u : 0u) | (static_cast<uint32_t>(b[8]) << 8);
    a.skipChildren = (b[0] & 2u ? 0x01u : 0u) | (static_cast<uint32_t>(b[9]) << 8);
    a.e = (b[0] & 4u ? 0x40u : 0u) | (static_cast<uint32_t>(b[10]) << 8);
    std::memcpy(&a.d, b + 4, 4);
    a.name = gNames[b[1] % 5];
    a.size = b[2] % 4;
    a.spare = b[3] % 3;
    return a;
}

void ResetIds(const CollectArgs &a)
{
    std::vector<uint16_t>().swap(gIds);
    gIds.reserve(a.size + a.spare);
    for (size_t i = 0; i < a.size; ++i) gIds.push_back(static_cast<uint16_t>(0x1000 + i));
}

void Take(Snapshot &s)
{
    s.values = gIds;
    s.capacity = gIds.capacity();
}

} // namespace

DIFF_TEST(TSObject_vf284, 0x53a290)
{
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, std::vector<uint16_t> *, uint32_t, const char *, uint32_t, uint32_t, uint32_t)>(0x53a290);
    uint32_t kind2 = 0, treeCalled = 0, treeFailed = 0, returned1 = 0, recursed = 0, childFailed = 0, reallocated = 0, injected = 0, sawName = 0;
    uint32_t trial = 0;
    RunSeeds(
        ctx, 30000, BuildCollect,
        [&](uint8_t *o, void *, const void *p) {
            const CollectArgs a = ArgsOf(p);
            ResetIds(a);
            const size_t before = gIds.capacity();
            const uint32_t r = original(o, &gIds, a.skipOwn, a.name, a.d, a.e, a.skipChildren);
            Take(gSnapOriginal);
            if (gSnapOriginal.capacity != before) ++reallocated;
            return r;
        },
        [&](uint8_t *o, void *, const void *p) {
            const CollectArgs a = ArgsOf(p);
            ResetIds(a);
            const uint32_t r = reinterpret_cast<cTSObject *>(o)->vf284(&gIds, a.skipOwn, a.name, a.d, a.e, a.skipChildren);
            Take(gSnapOurs);
            return r;
        },
        [&](WorldP &w, const Trial &t) {
            ++trial;
            if (gSnapOriginal.values != gSnapOurs.values || gSnapOriginal.capacity != gSnapOurs.capacity)
                ctx.Fail("trial %u: the id list differs (%zu/%zu entries, capacity %zu/%zu)", trial, gSnapOriginal.values.size(), gSnapOurs.values.size(),
                         gSnapOriginal.capacity, gSnapOurs.capacity);
            if (t.oa.thrown >= 0) {
                ++injected;
                return;
            }
            size_t lastTree = 0;
            for (size_t i = 0; i + 1 < t.oa.trace.size(); ++i) {
                if (t.oa.trace[i + 1] == (15u << 8 | 8u)) {
                    ++treeCalled;
                    lastTree = i + 2 + 8;
                }
                if (t.oa.trace[i + 1] == (31u << 8 | 3u)) ++sawName;
                if (t.oa.trace[i + 1] == (284u << 8 | 6u)) ++recursed;
            }
            const bool ok = (t.oa.ret & 0xff) != 0;
            if (t.oa.trace.size() == 2 && !ok) ++kind2;
            else if (!ok && lastTree == t.oa.trace.size()) ++treeFailed;
            else if (!ok) ++childFailed;
            if (ok) ++returned1;
            (void)w;
        });
    if (kind2 < 1000 || treeCalled < 3000 || sawName < 3000 || returned1 < 8000 || recursed < 3000 || treeFailed < 300 || childFailed < 300 ||
        reallocated < 500 || injected < 500)
        ctx.Fail("coverage: kind2 %u tree %u name %u returned1 %u recursed %u treefail %u failed %u reallocated %u injected %u", kind2, treeCalled,
                 sawName, returned1, recursed, treeFailed, childFailed, reallocated, injected);
}

// ---- vf358 (0x5ae3a0) ---------------------------------------------------------------------------------------------------

namespace {

// Fakes: kF2 the +0x40 context, kF3 its manager (slot 29 gives members by id, slot 83 the group), kF4 the group, kF11..kF14 the members.
void BuildOther(WorldP &w, uint32_t seed)
{
    std::mt19937 r = BaseWorld(w, seed, 0x5ae3a0, 30);
    reinterpret_cast<PrimPersonObject *>(w.obj)->mEnvironment = &w.fk[kF2];
    Use(w.vtThis, 98, 0);
    RetValue(w.obj, 98, r() % 6 == 0 ? (r() & 0xffffff00u) | 0xc : r() % 10 == 0 ? 0xbu : 0xc, w); // kind 0xc (a high byte set: still not 0xc)
    Use(w.vtThis, 57, 0);
    RetValue(w.obj, 57, r() % 8 == 0 ? static_cast<uint32_t>(-1 - static_cast<int>(r() % 3)) : r() % 6, w);
    Use(w.vtThis, 47, 0);
    const uint32_t ownId = r();
    RetValue(w.obj, 47, ownId, w);

    Use(w.vtFk[kF2], 26, 0);
    RetObject(&w.fk[kF2], 26, &w.fk[kF3], w);
    Use(w.vtFk[kF3], 83, 1);
    RetObject(&w.fk[kF3], 83, r() % 8 == 0 ? nullptr : &w.fk[kF4], w);
    Use(w.vtFk[kF3], 29, 1);
    for (int n = 0; n < 8; ++n) {
        const int pick = static_cast<int>(r() % 5);
        EnqueueObject(&w.fk[kF3], 29, pick == 4 ? nullptr : &w.fk[kF11 + pick], w);
    }
    Use(w.vtFk[kF4], 8, 0);
    Use(w.vtFk[kF4], 7, 1);
    // The group's size is asked before the walk, to start it, and after each member; mostly steady, sometimes changing.
    const uint32_t size = r() % 7 == 0 ? 0u : 1 + r() % 4;
    RetValue(&w.fk[kF4], 8, size | (r() % 4 == 0 ? 0u : 0u), w);
    if (r() % 3 == 0)
        for (int n = 0; n < 6; ++n) EnqueueValue(&w.fk[kF4], 8, r() % 6 == 0 ? 0xffffffffu : r() % 5, w);
    for (int n = 0; n < 8; ++n) {
        // The member's id: our own (low word, noise above) now and then, which skips it.
        EnqueueValue(&w.fk[kF4], 7, r() % 4 == 0 ? (ownId & 0xffff) | (r() & 0xffff0000u) : r(), w);
    }
    for (int k = 0; k < 4; ++k) {
        Use(w.vtFk[kF11 + k], 1, 0);
        Use(w.vtFk[kF11 + k], 2, 0);
        Use(w.vtFk[kF11 + k], 98, 0);
        for (int n = 0; n < 6; ++n) {
            const uint32_t pick = r() % 5;
            EnqueueValue(&w.fk[kF11 + k], 98, pick == 0 ? 0xbu : pick == 1 ? 0xcu : pick == 2 ? ((r() & 0xffffff00u) | 0xb) : r() % 16, w);
        }
    }
}

} // namespace

DIFF_TEST(TSObject_vf358, 0x5ae3a0)
{
    auto original = ctx.Original<void *(__thiscall *)(void *)>(0x5ae3a0);
    uint32_t found = 0, none = 0, replaced = 0, injected = 0;
    RunSeeds(
        ctx, 30000, BuildOther, [&](uint8_t *o, void *, const void *) { return Norm(reinterpret_cast<uint32_t>(original(o))); },
        [&](uint8_t *o, void *, const void *) { return Norm(reinterpret_cast<uint32_t>(reinterpret_cast<cTSObject *>(o)->vf358())); },
        [&](WorldP &w, const Trial &t) {
            if (t.oa.thrown >= 0) {
                ++injected;
                return;
            }
            if (t.oa.ret) ++found;
            else ++none;
            uint32_t addrefs = 0;
            for (size_t i = 0; i + 1 < t.oa.trace.size(); ++i) {
                const uint32_t who = t.oa.trace[i];
                if (who >= OffsetIn(&w.fk[kF11], w) && who <= OffsetIn(&w.fk[kF14], w)) {
                    if (t.oa.trace[i + 1] == (1u << 8)) ++addrefs;
                }
            }
            if (addrefs >= 4) ++replaced;                // more references taken than members seen: a replacement happened
        },
        &Always);
    if (found < 3000 || none < 3000 || replaced < 300 || injected < 500)
        ctx.Fail("coverage: found %u none %u replaced %u injected %u", found, none, replaced, injected);
}

// ---- vf201 (0x53ccb0) ---------------------------------------------------------------------------------------------------

namespace {

void *gLongReset[400];

// Slot 187 (five out parameters): logged by presence, then the script fills them in (pairs, tick, id object, word, flag byte).
void __fastcall Slot187(void *self, void *, int16_t *pairs, uint32_t *tick, void **idObject, uint16_t *word, uint8_t *flag)
{
    const uint32_t a[] = {reinterpret_cast<uint32_t>(pairs), reinterpret_cast<uint32_t>(tick), reinterpret_cast<uint32_t>(idObject),
                          reinterpret_cast<uint32_t>(word), reinterpret_cast<uint32_t>(flag)};
    const uint32_t h = Hit(self, 187, 5, a);
    pairs[0] = static_cast<int16_t>(h);
    pairs[1] = static_cast<int16_t>(h >> 16);
    pairs[2] = static_cast<int16_t>(Mix(h, 1, 2));
    pairs[3] = static_cast<int16_t>(Mix(h, 3, 4) >> 8);
    *tick = Mix(h, 5, 6);
    *idObject = reinterpret_cast<void *>(Scripted(Off(self), 1187));
    *word = static_cast<uint16_t>(Mix(h, 7, 8));
    *flag = static_cast<uint8_t>(Mix(h, 9, 10));
}

// Slot 197 gets the pairs the first slot filled: logged by value.
void __fastcall Slot197Pairs(void *self, void *, const int16_t *pairs, uint32_t tick, uint32_t a3)
{
    Tick();
    gTrace.push_back(0x197197u);
    gTrace.push_back(Norm(reinterpret_cast<uint32_t>(self)));
    for (int i = 0; i < 4; ++i) gTrace.push_back(static_cast<uint16_t>(pairs[i]));
    gTrace.push_back(tick);
    gTrace.push_back(a3);
}

// Slot 238 (notify an event) takes a pointer to an argument block of eight words: logged by content.
void __fastcall Slot238Words(void *self, void *, uint32_t a1, uint32_t a2, const uint16_t *words, uint32_t a4, uint32_t a5)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(238u << 8 | 5u);
    gTrace.push_back(a1);
    gTrace.push_back(a2);
    for (int i = 0; i < 8; ++i) gTrace.push_back(words[i]);
    gTrace.push_back(a4);
    gTrace.push_back(a5);
}

// Slot 9 hands out the service (or none): the scripted object for pseudo slot 1009.
uint32_t __fastcall Slot9Service(void *self, void *, uint32_t a1, uint32_t a2, void **out)
{
    const uint32_t a[] = {a1, a2, reinterpret_cast<uint32_t>(out)};
    Hit(self, 9, 3, a);
    *out = reinterpret_cast<void *>(Scripted(Off(self), 1009));
    return 0;
}

// The service's slot 24 hands out a stream (or none): the scripted object for pseudo slot 1024.
void __fastcall Slot24Stream(void *self, void *, uint32_t a1, void **out, uint32_t a3)
{
    const uint32_t a[] = {a1, reinterpret_cast<uint32_t>(out), a3};
    Hit(self, 24, 3, a);
    *out = reinterpret_cast<void *>(Scripted(Off(self), 1024));
}

// An object with a virtual base: the fake's second dword holds the vbtable (in the scratch area), whose entry 1 leads to the base fake.
void LinkVirtualBase(WorldP &w, int object, int base, size_t scratchOffset)
{
    int32_t *const vbtable = reinterpret_cast<int32_t *>(w.scratch + scratchOffset);
    vbtable[0] = 0;
    vbtable[1] = static_cast<int32_t>(reinterpret_cast<uint8_t *>(&w.fk[base]) - (reinterpret_cast<uint8_t *>(&w.fk[object]) + 4));
    w.fk[object].pad = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(vbtable));
}

uint32_t __fastcall StartTreeLogged(void *self, void *, uint32_t owner, uint32_t tree, uint32_t value)
{
    const uint32_t a[] = {owner, tree, value};
    return HelperHit(0x577e10, self, 3, a);
}

// Fakes: kF2 the +0x40 context, kF3 its slot 26 manager, kF4 the object slot 187 names, kF5 the service slot 9 gives, kF6 the stream
// its slot 24 gives, kF8 the definition (+0x238), kF9 the tree owner (slot 89), kF10 the service's virtual base, kF11 the stream's.
void BuildReset(WorldP &w, uint32_t seed)
{
    std::mt19937 r = BaseWorld(w, seed, 0x53ccb0, 40);
    auto *self = reinterpret_cast<PrimPersonObject *>(w.obj);
    self->mEnvironment = &w.fk[kF2];
    void *const definition = &w.fk[kF8];
    std::memcpy(w.obj + 0x238, &definition, 4);
    // The two owner tables (+0x88, +0x90) and their lengths (signed words): lists of words in the scratch area.
    for (int k = 0; k < 2; ++k) {
        uint16_t *table = reinterpret_cast<uint16_t *>(w.scratch + 0x200 + k * 0x40);
        for (int i = 0; i < 24; ++i) table[i] = static_cast<uint16_t>(1 + r() % 0xfffe);
        const uint8_t *tablePtr = reinterpret_cast<uint8_t *>(table);
        std::memcpy(w.obj + 0x88 + k * 8, &tablePtr, 4);
        const uint32_t pick = r() % 8;
        const int16_t length = pick == 0 ? 0 : pick == 1 ? -3 : static_cast<int16_t>(1 + r() % 20);
        std::memcpy(w.obj + 0x8c + k * 8, &length, 2);
    }
    const void *edithVTable = w.vtPerson;
    std::memcpy(w.obj + 0x74, &edithVTable, 4);
    Use(w.vtPerson, 12, 0);
    Use(w.vtPerson, 13, 0);

    Use(w.vtTree, 43, 1);
    RetValue(w.obj + 4, 43, NoiseAl(r, r() % 6 == 0), w);
    Use(w.vtThis, 93, 1);
    RetValue(w.obj, 93, Edge16(r) | (r() & 0xffff0000u), w);
    Use(w.vtThis, 89, 0);
    RetObject(w.obj, 89, &w.fk[kF9], w);
    Use(w.vtThis, 104, 0);
    RetValue(w.obj, 104, NoiseAl(r, r() % 3 == 0), w);
    w.vtThis[238] = reinterpret_cast<void *>(&Slot238Words);
    Use(w.vtThis, 236, 3);
    Use(w.vtThis, 233, 0);
    w.vtThis[187] = reinterpret_cast<void *>(&Slot187);
    w.vtThis[9] = reinterpret_cast<void *>(&Slot9Service);
    w.vtThis[197] = reinterpret_cast<void *>(&Slot197Pairs);
    RetObject(w.obj, 1187, r() % 4 == 0 ? nullptr : &w.fk[kF4], w);
    RetObject(w.obj, 1009, r() % 4 == 0 ? nullptr : &w.fk[kF5], w);
    RetObject(&w.fk[kF5], 1024, r() % 4 == 0 ? nullptr : &w.fk[kF6], w);
    std::memcpy(gLongReset, w.vtThis, sizeof(w.vtThis));
    gLongReset[258] = reinterpret_cast<void *>(&Th<258, 0>::F);
    gLongReset[282] = reinterpret_cast<void *>(&Th<282, 0>::F);
    gLongReset[337] = reinterpret_cast<void *>(&Th<337, 1>::F);
    gLongReset[352] = reinterpret_cast<void *>(&Th<352, 0>::F);
    self->mVTable = gLongReset;
    RetValue(w.obj, 337, NoiseAl(r, r() % 6 == 0), w);
    RetValue(w.obj, 352, r() % 3 == 0 ? (r() & 0xffff0000u) : r(), w);

    w.vtFk[kF5][24] = reinterpret_cast<void *>(&Slot24Stream);
    LinkVirtualBase(w, kF5, kF10, 0x300);
    LinkVirtualBase(w, kF6, kF11, 0x310);
    Use(w.vtFk[kF10], 2, 0);
    Use(w.vtFk[kF11], 2, 0);
    Use(w.vtFk[kF6], 8, 1);
    Use(w.vtFk[kF4], 47, 0);
    RetValue(&w.fk[kF4], 47, r() % 3 == 0 ? (r() & 0xffff0000u) : r(), w);
    Use(w.vtFk[kF2], 26, 0);
    RetObject(&w.fk[kF2], 26, &w.fk[kF3], w);
    Use(w.vtFk[kF3], 71, 2);
    // The definition: field 5 once (0x31), then field reads (slot 3) in call order, many of them zero in the low word or all over.
    Use(w.vtFk[kF8], 5, 1);
    RetValue(&w.fk[kF8], 5, r(), w);
    Use(w.vtFk[kF8], 3, 1);
    for (int n = 0; n < 12; ++n) {
        const uint32_t pick = r() % 6;
        EnqueueValue(&w.fk[kF8], 3, pick < 2 ? 0u : pick == 2 ? (r() & 0xffff0000u) : r(), w);
    }
}

} // namespace

DIFF_TEST(TSObject_vf201, 0x53ccb0)
{
    CodePatch start(ctx, 0x577e10, reinterpret_cast<void *>(&StartTreeLogged));
    CodePatch startOurs(MemberAddress(&cTSTreeSim::StartTree), reinterpret_cast<void *>(&StartTreeLogged));
    auto original = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t)>(0x53ccb0);
    auto argsOf = [](const void *p, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
        const uint8_t *x = static_cast<const uint8_t *>(p);
        // a: a word (often zero) with noise above it; b, c, d: bytes with noise above the low byte (the callee only looks at the low byte).
        *a = x[0] % 3 == 0 ? 0u : x[0] % 6 == 1 ? 0x12340000u : static_cast<uint32_t>(x[1]) | static_cast<uint32_t>(x[2]) << 8 | 0x12340000u;
        *b = (x[3] % 2 ? 0x01u : 0u) | static_cast<uint32_t>(x[4]) << 8 | static_cast<uint32_t>(x[5]) << 16 | static_cast<uint32_t>(x[6]) << 24;
        *c = (x[7] % 2 ? 0x80u : 0u) | static_cast<uint32_t>(x[8]) << 8 | 0x5a000000u;
        *d = x[9] | static_cast<uint32_t>(x[10]) << 8;
    };
    uint32_t full = 0, notified = 0, serviceSeen = 0, streamSeen = 0, noNotify = 0, injected = 0;
    RunSeeds(
        ctx, 30000, BuildReset,
        [&](uint8_t *o, void *, const void *p) {
            uint32_t a, b, c, d;
            argsOf(p, &a, &b, &c, &d);
            original(o, a, b, c, d);
            return 0u;
        },
        [&](uint8_t *o, void *, const void *p) {
            uint32_t a, b, c, d;
            argsOf(p, &a, &b, &c, &d);
            reinterpret_cast<cTSObject *>(o)->vf201(a, b, c, d);
            return 0u;
        },
        [&](WorldP &w, const Trial &t) {
            if (t.oa.thrown >= 0) {
                ++injected;
                return;
            }
            const uint32_t service = OffsetIn(&w.fk[kF5], w), stream = OffsetIn(&w.fk[kF6], w);
            bool sawService = false, sawStream = false, sawNotify = false;
            for (size_t i = 0; i + 1 < t.oa.trace.size(); ++i) {
                if (t.oa.trace[i] == 0 && t.oa.trace[i + 1] == (187u << 8 | 5u)) ++full;
                if (t.oa.trace[i] == service && t.oa.trace[i + 1] == (24u << 8 | 3u)) sawService = true;
                if (t.oa.trace[i] == stream && t.oa.trace[i + 1] == (8u << 8 | 1u)) sawStream = true;
                if (t.oa.trace[i] == 0 && t.oa.trace[i + 1] == (238u << 8 | 5u)) sawNotify = true;
            }
            if (sawService) ++serviceSeen;
            if (sawStream) ++streamSeen;
            if (sawNotify) ++notified;
            else ++noNotify;
        },
        &Always);
    if (full < 10000 || serviceSeen < 3000 || streamSeen < 1500 || notified < 3000 || noNotify < 3000 || injected < 1000)
        ctx.Fail("coverage: full %u service %u stream %u notified %u not %u injected %u", full, serviceSeen, streamSeen, notified, noNotify, injected);
}

// ---- vf202 (0x53aa80) ---------------------------------------------------------------------------------------------------

namespace {

void *gLongPhase[400];
void *gLongEntry[300]; // the registry entry fake: slot 280 lies beyond the harness's vtables
ObjectRegistry gRegistry;
// A registry of 65536 entries, only these populated (so a negative number read as unsigned would find an entry): the first four and last four.
constexpr uint32_t kRegistryCount = 0x10000;
constexpr uint32_t kRegistryUsed[8] = {0, 1, 2, 3, 0xfffc, 0xfffd, 0xfffe, 0xffff};
void *gRegistryTable[kRegistryCount];

uint32_t __fastcall ReloadStandIn(void *self, void *, uint32_t keep)
{
    const uint32_t a[] = {keep & 0xff}; // a bool argument: only the low byte is defined
    return HelperHit(0x531320, self, 1, a);
}

void *__cdecl DirectorStandIn()
{
    HelperHit(0x1ae18d, nullptr, 0, nullptr);
    return &gW->fk[kF8];
}

const char *const kTexts[] = {"", "reaction", "a longer reaction text that does not fit into the small string buffer", "x"};

// The loading constructor of cTSString (0x1d0df0, ours): stands in on both sides, builds the string from a scripted text.
cTSString *__fastcall TextStandIn(cTSString *self, void *, uint32_t table, uint32_t entry, uint32_t group, uint32_t flags, uint32_t type)
{
    const uint32_t a[] = {table, entry, group, flags, type};
    const uint32_t pick = HelperHit(0x1d0df0, self, 5, a);
    self->ConstructEmpty();
    self->mString.FromChar(kTexts[pick % 4]);
    return self;
}

// The notification (slot 238): the argument block is either the caller's or two zero dwords on the stack, logged by content then.
void __fastcall Notify238(void *self, void *, uint32_t a1, uint32_t a2, const uint32_t *block, uint32_t a4, uint32_t a5)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(238u << 8 | 5u);
    gTrace.push_back(a1);
    gTrace.push_back(a2);
    if (OnStack(reinterpret_cast<uint32_t>(block))) {
        gTrace.push_back(0x57ac0000u);
        gTrace.push_back(block[0]);
        gTrace.push_back(block[1]);
    } else {
        gTrace.push_back(Norm(reinterpret_cast<uint32_t>(block)));
    }
    gTrace.push_back(a4);
    gTrace.push_back(a5);
}

// Slot 201 (vf201): its third argument carries stack leftovers above the low byte, which vf201 ignores: only the low byte is logged.
void __fastcall Slot201Log(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    const uint32_t args[] = {a, b, c & 0xff, d};
    Hit(self, 201, 4, args);
}

uint32_t __fastcall Slot9Text(void *self, void *, uint32_t a1, uint32_t a2, void **out)
{
    const uint32_t a[] = {a1, a2, reinterpret_cast<uint32_t>(out)};
    const uint32_t answer = Hit(self, 9, 3, a);
    *out = reinterpret_cast<void *>(Scripted(Off(self), 1009));
    if (static_cast<uint8_t>(answer) != 0 && !*out) *out = &gW->fk[kF12]; // (the original dereferences it: never null when it answers)
    return answer;
}

uint32_t __fastcall Slot3Framework(void *self, void *, uint32_t a1, uint32_t a2, void **out)
{
    const uint32_t a[] = {a1, a2, reinterpret_cast<uint32_t>(out)};
    Hit(self, 3, 3, a);
    *out = &gW->fk[kF9];
    return 0;
}

// The text service's slot 78: the text is logged by content.
uint32_t __fastcall TextOffered(void *self, void *, const char *text, uint32_t one)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(78u << 8 | 2u);
    gTrace.push_back(0x57e70000u | static_cast<uint32_t>(std::strlen(text)));
    for (const char *c = text; *c; ++c) gTrace.push_back(static_cast<uint8_t>(*c));
    gTrace.push_back(one);
    return Scripted(Off(self), 78);
}

// The query on the object: logged, the interface it stores is the scripted object for pseudo slot 256.
uint32_t __fastcall PhaseQuery(void *self, void *, uint32_t iid, void **out)
{
    const uint32_t a[] = {iid, reinterpret_cast<uint32_t>(out)};
    const uint32_t answer = Hit(self, 0, 2, a);
    *out = reinterpret_cast<void *>(Scripted(Off(self), 256));
    return answer;
}

// Fakes: kF2 the interface the query gives, kF3 what its slot 6 gives, kF4 another object, kF5 the object info (slot 61), kF6 its
// slot 27 object, kF7 the limits, kF8 the director framework, kF9 its service, kF10 the +0x240 object, kF11 the +0x48 object, kF12
// the text service (slot 9) with the virtual base kF13, kF14 its slot 120 object, kF15 the registry's entry, kF16 its slot 280
// target, kF17 the target's slot 61 object, kF18 the tree owner (slot 89).
void BuildPhase(WorldP &w, uint32_t seed)
{
    std::mt19937 r = BaseWorld(w, seed, 0x53aa80, 60);
    auto *self = reinterpret_cast<PrimPersonObject *>(w.obj);
    self->mHolder48 = &w.fk[kF11];
    const void *owner240 = &w.fk[kF10];
    std::memcpy(w.obj + 0x240, &owner240, 4);
    const void *registry = &gRegistry;
    std::memcpy(w.obj + 0x1c4, &registry, 4);
    // The number word (+0x2a4): usually a valid one (1..4), sometimes zero, negative or past the end.
    const uint32_t numberPick = r() % 8;
    const int16_t number = numberPick == 0 ? 0 : numberPick == 1 ? static_cast<int16_t>(-1 - r() % 3) : numberPick == 2 ? static_cast<int16_t>(5 + r() % 3) : static_cast<int16_t>(1 + r() % 4);
    std::memcpy(w.obj + 0x2a4, &number, 2);
    if (r() % 3 == 0) {
        const uint32_t flags = r() | 4; // bit 2 of +0x190 set
        std::memcpy(w.obj + 0x190, &flags, 4);
    } else if (r() % 2 == 0) {
        const uint32_t flags = r() & ~4u;
        std::memcpy(w.obj + 0x190, &flags, 4);
    }
    const void *edithVTable = w.vtPerson;
    std::memcpy(w.obj + 0x74, &edithVTable, 4);

    Use(w.vtTree, 46, 1);
    Use(w.vtThis, 215, 0);
    Use(w.vtThis, 33, 0);
    RetObject(w.obj, 33, w.obj + 0x1d0, w);
    w.vtThis[197] = gThunk[197][3];
    Use(w.vtThis, 104, 0);
    RetValue(w.obj, 104, NoiseAl(r, r() % 2 == 0), w);
    w.vtThis[0] = reinterpret_cast<void *>(&PhaseQuery);
    RetObject(w.obj, 256, &w.fk[kF2], w); // (never null: the original dereferences it)
    Use(w.vtThis, 61, 0);
    RetObject(w.obj, 61, &w.fk[kF5], w);
    w.vtThis[238] = reinterpret_cast<void *>(&Notify238);
    Use(w.vtThis, 204, 2);
    w.vtThis[201] = reinterpret_cast<void *>(&Slot201Log);
    Use(w.vtThis, 188, 0);
    RetValue(w.obj, 188, NoiseAl(r, r() % 2 == 0), w);
    Use(w.vtThis, 193, 1);
    Use(w.vtThis, 89, 0);
    RetObject(w.obj, 89, &w.fk[kF18], w);
    w.vtThis[9] = reinterpret_cast<void *>(&Slot9Text);
    RetValue(w.obj, 9, NoiseAl(r, r() % 2 == 0), w);
    RetObject(w.obj, 1009, r() % 4 == 0 ? nullptr : &w.fk[kF12], w);
    Use(w.vtThis, 221, 0);
    Use(w.vtThis, 233, 0);
    Use(w.vtThis, 216, 0);
    std::memcpy(gLongPhase, w.vtThis, sizeof(w.vtThis));
    gLongPhase[340] = reinterpret_cast<void *>(&Th<340, 1>::F);
    gLongPhase[291] = reinterpret_cast<void *>(&Th<291, 0>::F);
    gLongPhase[293] = reinterpret_cast<void *>(&Th<293, 1>::F);
    gLongPhase[290] = reinterpret_cast<void *>(&Th<290, 1>::F);
    gLongPhase[296] = reinterpret_cast<void *>(&Th<296, 1>::F);
    self->mVTable = gLongPhase;
    RetValue(w.obj, 291, r() % 3 == 0 ? 0u : r() % 4, w);

    // The interface chain of the limits step.
    Use(w.vtFk[kF2], 6, 0);
    Use(w.vtFk[kF2], 2, 0);
    RetObject(&w.fk[kF2], 6, &w.fk[kF3], w);
    Use(w.vtFk[kF3], 11, 0);
    RetObject(&w.fk[kF3], 11, r() % 2 == 0 ? static_cast<const void *>(w.obj) : &w.fk[kF4], w);
    Use(w.vtFk[kF5], 27, 0);
    RetObject(&w.fk[kF5], 27, &w.fk[kF6], w);
    Use(w.vtFk[kF6], 4, 0);
    RetObject(&w.fk[kF6], 4, &w.fk[kF7], w);
    Use(w.vtFk[kF7], 5, 1);
    EnqueueValue(&w.fk[kF7], 5, r(), w);
    EnqueueValue(&w.fk[kF7], 5, r() % 2 ? static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(r()))) : r(), w);
    Use(w.vtFk[kF5], 34, 0);
    RetValue(&w.fk[kF5], 34, r() % 4 == 0 ? 0xffffu : r(), w);
    // The service step.
    w.vtFk[kF8][3] = reinterpret_cast<void *>(&Slot3Framework);
    Use(w.vtFk[kF9], 11, 1);
    Use(w.vtFk[kF9], 2, 0);
    // The registry: a table of entries (null now and then), each leading through slot 280 to a target.
    gRegistry.mObjectsBegin = gRegistryTable;
    gRegistry.mObjectsEnd = gRegistryTable + kRegistryCount;
    for (const uint32_t i : kRegistryUsed) gRegistryTable[i] = r() % 6 == 0 ? nullptr : &w.fk[kF15];
    std::memset(gLongEntry, 0, sizeof(gLongEntry));
    gLongEntry[280] = reinterpret_cast<void *>(&Th<280, 0>::F);
    w.fk[kF15].vt = gLongEntry;
    RetObject(&w.fk[kF15], 280, r() % 6 == 0 ? nullptr : &w.fk[kF16], w);
    Use(w.vtFk[kF16], 1, 0);
    Use(w.vtFk[kF16], 2, 0);
    Use(w.vtFk[kF16], 25, 1);
    EnqueueValue(&w.fk[kF16], 25, NoiseAl(r, r() % 5 == 0), w);
    EnqueueValue(&w.fk[kF16], 25, NoiseAl(r, r() % 5 == 0), w);
    Use(w.vtFk[kF16], 61, 0);
    RetObject(&w.fk[kF16], 61, r() % 5 == 0 ? nullptr : &w.fk[kF17], w);
    Use(w.vtFk[kF17], 28, 0);
    RetValue(&w.fk[kF17], 28, r() % 5 == 0 ? 0u : r() % 4 == 0 ? (r() | 1u) : 0xec987278u, w);
    // The run step.
    Use(w.vtFk[kF10], 44, 0);
    RetValue(&w.fk[kF10], 44, NoiseAl(r, r() % 2 == 0), w);
    Use(w.vtFk[kF11], 16, 0);
    RetValue(&w.fk[kF11], 16, NoiseAl(r, r() % 2 == 0), w);
    // The text step.
    Use(w.vtFk[kF18], 11, 0);
    RetValue(&w.fk[kF18], 11, r(), w);
    LinkVirtualBase(w, kF12, kF13, 0x300);
    Use(w.vtFk[kF13], 2, 0);
    w.vtFk[kF12][78] = reinterpret_cast<void *>(&TextOffered);
    RetValue(&w.fk[kF12], 78, NoiseAl(r, r() % 2 == 0), w);
    Use(w.vtFk[kF12], 120, 0);
    RetObject(&w.fk[kF12], 120, &w.fk[kF14], w);
    Use(w.vtFk[kF14], 71, 0);
    RetValue(&w.fk[kF14], 71, r() % 3 == 0 ? 0u : r() % 3 == 0 ? 0x100u : r(), w);
    // The text the loading constructor's stand-in makes (HelperHit answers from the script for (rva | 0x80000000, 0)).
    gRets.push_back({0x80000000u | 0x1d0df0u, 0, false, r()});
}

} // namespace

DIFF_TEST(TSObject_vf202, 0x53aa80)
{
    CodePatch reload(ctx, 0x531320, reinterpret_cast<void *>(&ReloadStandIn));
    CodePatch reloadOurs(MemberAddress(&cTSObject::ReloadReferences), reinterpret_cast<void *>(&ReloadStandIn));
    CodePatch director(ctx, 0x1ae18d, reinterpret_cast<void *>(&DirectorStandIn));
    CodePatch directorOurs(reinterpret_cast<void *>(&GetDirectorFrameworkObject), reinterpret_cast<void *>(&DirectorStandIn));
    using LoadConstruct = cTSString *(cTSString::*)(uint16_t, uint16_t, uint32_t, uint8_t, uint32_t);
    CodePatch text(ctx, 0x1d0df0, reinterpret_cast<void *>(&TextStandIn));
    CodePatch textOurs(MemberAddress(static_cast<LoadConstruct>(&cTSString::Construct)), reinterpret_cast<void *>(&TextStandIn));
    gBeforeRun = [&](WorldP &w) {
        for (const uint32_t i : kRegistryUsed)
            if (gRegistryTable[i]) gRegistryTable[i] = &w.fk[kF15];
    };
    struct Reset {
        ~Reset() { gBeforeRun = nullptr; }
    } reset;
    static const int32_t kPhases[] = {-1, 0, 7, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 100, INT32_MIN, INT32_MAX};
    auto original = ctx.Original<void(__thiscall *)(void *, int32_t, const void *)>(0x53aa80);
    uint32_t phases[4] = {}, kept = 0, cleared = 0, offered = 0, reacted = 0, ran = 0, looped = 0, forwarded = 0, injected = 0;
    RunSeeds(
        ctx, 40000, BuildPhase,
        [&](uint8_t *o, void *, const void *p) {
            const uint8_t *b = static_cast<const uint8_t *>(p);
            original(o, kPhases[b[0] % 12], b[1] % 3 == 0 ? nullptr : gW->scratch + 0x180);
            return 0u;
        },
        [&](uint8_t *o, void *, const void *p) {
            const uint8_t *b = static_cast<const uint8_t *>(p);
            reinterpret_cast<cTSObject *>(o)->vf202(kPhases[b[0] % 12], b[1] % 3 == 0 ? nullptr : gW->scratch + 0x180);
            return 0u;
        },
        [&](WorldP &w, const Trial &t) {
            if (t.oa.thrown >= 0) {
                ++injected;
                return;
            }
            const int32_t phase = kPhases[w.param[0] % 12];
            ++phases[phase < 0x20 ? 0 : phase == 0x20 ? 1 : phase < 0x23 ? 2 : 3];
            const uint32_t target = OffsetIn(&w.fk[kF16], w), service = OffsetIn(&w.fk[kF12], w);
            for (size_t i = 0; i + 1 < t.oa.trace.size(); ++i) {
                if (t.oa.trace[i] == OffsetIn(&w.fk[kF17], w) && t.oa.trace[i + 1] == (28u << 8)) ++kept;
                if (t.oa.trace[i] == 0 && t.oa.trace[i + 1] == (201u << 8 | 4u)) ++ran;
                if (t.oa.trace[i] == service && t.oa.trace[i + 1] == (78u << 8 | 2u)) ++offered;
                if (t.oa.trace[i] == 0 && t.oa.trace[i + 1] == (221u << 8)) ++reacted;
                if (t.oa.trace[i] == 0 && t.oa.trace[i + 1] == (293u << 8 | 1u)) ++looped;
                if (t.oa.trace[i] == 0 && t.oa.trace[i + 1] == (0u << 8 | 2u)) ++forwarded;
                if (t.oa.trace[i] == target && t.oa.trace[i + 1] == (1u << 8)) ++cleared;
            }
        },
        &Always);
    if (phases[0] < 3000 || phases[1] < 1000 || phases[2] < 2000 || phases[3] < 5000 || kept < 500 || cleared < 1000 || offered < 1000 || reacted < 100 ||
        ran < 5000 || looped < 1000 || forwarded < 1000 || injected < 1000)
        ctx.Fail("coverage: phases %u %u %u %u kept %u target %u offered %u reacted %u ran %u looped %u forwarded %u injected %u", phases[0], phases[1], phases[2],
                 phases[3], kept, cleared, offered, reacted, ran, looped, forwarded, injected);
}

// ---- vf157 (0x544f60) and vf158 (0x544060) ------------------------------------------------------------------------------

namespace {

// The float bits at a signed distance in the sortable order (negative floats have their low 31 bits flipped), so that distances
// of 0, +-1, +-31..33 and others are generated exactly, also across zero.
uint32_t Shifted(uint32_t bits, uint32_t pick, std::mt19937 &r)
{
    static const int32_t deltas[] = {0, 1, -1, 31, -31, 32, -32, 33, -33, 16, -16, 40, -40};
    const int32_t delta = pick < 13 ? deltas[pick] : static_cast<int32_t>(r() % 81) - 40;
    const uint32_t key = (bits >> 31) ? bits ^ 0x7fffffffu : bits;
    const uint32_t moved = key + static_cast<uint32_t>(delta);
    return (moved >> 31) ? moved ^ 0x7fffffffu : moved;
}

// The record slot (163 or 165): six dwords written from the script. The mode picks how the record relates to the wanted segment
// (the six dwords in the world's parameter bytes).
template <int S> void __fastcall FillSegment(void *self, void *, uint32_t *record, int32_t index)
{
    const uint32_t a[] = {reinterpret_cast<uint32_t>(record), static_cast<uint32_t>(index)};
    const uint32_t h = Hit(self, S, 2, a);
    std::mt19937 r(Mix(h, static_cast<uint32_t>(index), 77));
    uint32_t p[6];
    std::memcpy(p, gW->param, sizeof(p));
    const uint32_t mode = r() % 10;
    auto closeTo = [&](uint32_t v) { return Shifted(v, r() % 20, r); };
    if (mode <= 2) { // the same direction
        const uint32_t v[6] = {closeTo(p[0]), closeTo(p[1]), p[2], closeTo(p[3]), closeTo(p[4]), p[5]};
        std::memcpy(record, v, sizeof(v));
    } else if (mode <= 5) { // the reverse direction
        const uint32_t v[6] = {closeTo(p[3]), closeTo(p[4]), p[5], closeTo(p[0]), closeTo(p[1]), p[2]};
        std::memcpy(record, v, sizeof(v));
    } else if (mode == 6) { // right ends, wrong levels
        const uint32_t v[6] = {closeTo(p[0]), closeTo(p[1]), p[2] + 1, closeTo(p[3]), closeTo(p[4]), p[5]};
        std::memcpy(record, v, sizeof(v));
    } else if (mode == 7) { // reversed, one corner far off
        const uint32_t v[6] = {closeTo(p[3]), closeTo(p[4]), p[5], closeTo(p[0]) ^ 0x00400000u, closeTo(p[1]), p[2]};
        std::memcpy(record, v, sizeof(v));
    } else {
        for (uint32_t &w : p) w = r();
        std::memcpy(record, p, sizeof(p));
    }
}

template <int Count, int Record> void BuildSegments(WorldP &w, uint32_t seed)
{
    std::mt19937 r = BaseWorld(w, seed, 0x544f60 + Count, 16);
    // The wanted segment: corners with signs and magnitudes around zero as well, small level numbers.
    uint32_t p[6];
    for (int k : {0, 1, 3, 4}) {
        const uint32_t pick = r() % 6;
        p[k] = pick == 0 ? 0x80000000u + r() % 40 : pick == 1 ? r() % 40 : pick == 2 ? 0x3f800000u + r() % 64 : pick == 3 ? 0xbf800000u + r() % 64 : r();
    }
    p[2] = r() % 4;
    p[5] = r() % 4;
    std::memcpy(w.param, p, sizeof(p));
    Use(w.vtThis, 153, 0);
    Use(w.vtThis, Count, 0);
    const uint32_t countPick = r() % 8;
    RetValue(w.obj, Count, countPick == 0 ? 0u : countPick == 1 ? 0xfffffffeu : 1 + r() % 4, w);
    w.vtThis[Record] = reinterpret_cast<void *>(&FillSegment<Record>);
}

template <int Count, int Record> void RunSegments(difftest::Context &ctx, uint32_t rva, uint8_t (cTSObject::*ours)(const uint32_t *))
{
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, const uint32_t *)>(rva);
    uint32_t matched = 0, firstMatch = 0, none = 0, empty = 0, injected = 0;
    RunSeeds(
        ctx, 40000, BuildSegments<Count, Record>,
        [&](uint8_t *o, void *, const void *p) { return static_cast<uint32_t>(original(o, static_cast<const uint32_t *>(p))); },
        [&](uint8_t *o, void *, const void *p) { return static_cast<uint32_t>((reinterpret_cast<cTSObject *>(o)->*ours)(static_cast<const uint32_t *>(p))); },
        [&](WorldP &, const Trial &t) {
            if (t.oa.thrown >= 0) {
                ++injected;
                return;
            }
            uint32_t records = 0;
            for (size_t i = 0; i + 1 < t.oa.trace.size(); ++i)
                if (t.oa.trace[i + 1] == (static_cast<uint32_t>(Record) << 8 | 2u)) ++records;
            if (t.oa.ret & 0xff) {
                ++matched;
                if (records == 1) ++firstMatch;
            } else if (records == 0) {
                ++empty;
            } else {
                ++none;
            }
        });
    if (matched < 5000 || firstMatch < 2000 || none < 3000 || empty < 2000 || injected < 500)
        ctx.Fail("coverage: matched %u first %u none %u empty %u injected %u", matched, firstMatch, none, empty, injected);
}

} // namespace

DIFF_TEST(TSObject_vf157, 0x544f60)
{
    RunSegments<159, 163>(ctx, 0x544f60, &cTSObject::vf157);
}

DIFF_TEST(TSObject_vf158, 0x544060)
{
    RunSegments<160, 165>(ctx, 0x544060, &cTSObject::vf158);
}

// ---- vf324 (0x541140) ---------------------------------------------------------------------------------------------------

namespace {

// The helper's slot 4 (out, a2, a float on the stack): logged, and the 12 bytes of out are written from the script.
void __fastcall HelperValue(void *self, void *, uint32_t *out, uint32_t a2, uint32_t floatBits)
{
    const uint32_t a[] = {reinterpret_cast<uint32_t>(out), a2, floatBits};
    const uint32_t h = Hit(self, 4, 3, a);
    out[0] = h;
    out[1] = Mix(h, 1, 2);
    out[2] = Mix(h, 3, 4);
}

void BuildHelperValue(WorldP &w, uint32_t seed)
{
    BuildOffer(w, seed);
    // BuildOffer's interface fakes take slot 6; this function asks them for slot 4.
    for (int k = 0; k < kInterfaces; ++k) w.vtFk[kF16 + k][4] = reinterpret_cast<void *>(&HelperValue);
}

} // namespace

DIFF_TEST(TSObject_vf324, 0x541140)
{
    void **const cache = ctx.image.At<void *>(0x10c95d4);
    int *const guard = ctx.image.At<int>(0x10c95d8);
    uint8_t *const defaults = ctx.image.At<uint8_t>(0x10daf34);
    uint8_t *const defaults2 = ctx.image.At<uint8_t>(0x10daf3c);
    struct Restore {
        void **cache;
        int *guard;
        void *cacheValue;
        int guardValue;
        uint8_t *at1, *at2;
        uint8_t saved1[8], saved2[4];
        Restore(void **c, int *g, uint8_t *a1, uint8_t *a2) : cache(c), guard(g), cacheValue(*c), guardValue(*g), at1(a1), at2(a2)
        {
            std::memcpy(saved1, a1, 8);
            std::memcpy(saved2, a2, 4);
        }
        ~Restore()
        {
            *cache = cacheValue;
            *guard = guardValue;
            std::memcpy(at1, saved1, 8);
            std::memcpy(at2, saved2, 4);
            gBeforeRun = nullptr;
        }
    } restore(cache, guard, defaults, defaults2);
    // Known default bytes for the run (the globals the no-helper path copies), and the allocator fake as in the vf182 test.
    for (int i = 0; i < 8; ++i) defaults[i] = static_cast<uint8_t>(0x11 * (i + 1));
    for (int i = 0; i < 4; ++i) defaults2[i] = static_cast<uint8_t>(0xa0 + i);
    gBeforeRun = [&](WorldP &w) {
        *cache = &w.fk[kAllocatorFake];
        *guard = INT32_MIN;
    };
    auto original = ctx.Original<void *(__thiscall *)(void *, void *, uint32_t, float)>(0x541140);
    uint32_t answered = 0, none = 0, injected = 0, freed = 0;
    RunSeeds(
        ctx, 30000, BuildHelperValue,
        [&](uint8_t *o, void *, const void *p) {
            const uint32_t *a = static_cast<const uint32_t *>(p);
            float f;
            std::memcpy(&f, &a[1], 4);
            return Norm(reinterpret_cast<uint32_t>(original(o, gW->scratch + 0x180, a[0], f)));
        },
        [&](uint8_t *o, void *, const void *p) {
            const uint32_t *a = static_cast<const uint32_t *>(p);
            float f;
            std::memcpy(&f, &a[1], 4);
            return Norm(reinterpret_cast<uint32_t>(reinterpret_cast<cTSObject *>(o)->vf324(gW->scratch + 0x180, a[0], f)));
        },
        [&](WorldP &w, const Trial &t) {
            if (t.oa.thrown >= 0) {
                ++injected;
                return;
            }
            bool helperCalled = false;
            for (size_t i = 0; i + 1 < t.oa.trace.size(); ++i) {
                if (t.oa.trace[i + 1] == (4u << 8 | 3u)) helperCalled = true;
                if (t.oa.trace[i] == OffsetIn(&w.fk[kAllocatorFake], w) && t.oa.trace[i + 1] == (4u << 8 | 1u)) ++freed;
            }
            if (helperCalled) ++answered;
            else ++none;
        });
    if (answered < 5000 || none < 3000 || freed < 10000 || injected < 300)
        ctx.Fail("coverage: answered %u none %u freed %u injected %u", answered, none, freed, injected);
}
