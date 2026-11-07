// Differential tests of cTSObject primary-vtable slots (src/ts/cTSObjectSlots2.cpp) in the person primitives' recording world
// (tests/simantics_prims_person.h: an object with recording vtables for itself and its tree sim subobject at +4).
#include "simantics_prims_person.h"
#include "hook/hooks.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"
#include "rz/cGZAllocatorService.h"
#include "ts/cTSObject.h"
#include "ts/cTSObjectFootprint.h"

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

constexpr int kAllocatorFake = 20;

// The allocator service's free (slot 4): logged without ticking (it runs while an error unwinds, where nothing may throw).
void __fastcall ServiceFree(void *self, void *, void *mem)
{
    gTrace.push_back(Off(self));
    gTrace.push_back(4u << 8 | 1u);
    gTrace.push_back(Norm(reinterpret_cast<uint32_t>(mem)));
}

} // namespace

// ---- vf325 (0x540fc0) ---------------------------------------------------------------------------------------------------

namespace {

constexpr int kEntries = 5;    // kF11..kF15: the objects the +0x74 subobject lists
constexpr int kInterfaces = 4; // kF16..kF19: the interfaces the entries give

struct ListScript {
    uint32_t count = 0;
    bool storage = true;
    uint32_t pick[8] = {};
} gList;

struct HeldVector {
    void **mFirst, **mLast, **mEnd;
};

// The +0x74 subobject's slot 10 fills the vector it is given: `count` entries in the scratch area (storage for 8).
void __fastcall Fill10(void *self, void *, HeldVector *v)
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

void BuildHelperAsk(WorldP &w, uint32_t seed)
{
    std::mt19937 r = BaseWorld(w, seed, 0x540fc0, 14);
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
        for (int n = 0; n < 6; ++n) EnqueueValue(&w.fk[kF11 + k], 0, NoiseAl(r, r() % 3 == 0), w);
    }
    for (int k = 0; k < kInterfaces; ++k) {
        Use(w.vtFk[kF16 + k], 5, 1);
        Use(w.vtFk[kF16 + k], 2, 0);
        RetValue(&w.fk[kF16 + k], 5, r(), w); // the whole eax is the result
    }
    w.vtFk[kAllocatorFake][4] = reinterpret_cast<void *>(&ServiceFree);
}

} // namespace

DIFF_TEST(TSObject_vf325, 0x540fc0)
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
    gBeforeRun = [&](WorldP &w) {
        *cache = &w.fk[kAllocatorFake];
        *guard = INT32_MIN;
    };
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, uint32_t)>(0x540fc0);
    uint32_t answered = 0, none = 0, freed = 0, injected = 0;
    RunSeeds(
        ctx, 30000, BuildHelperAsk,
        [&](uint8_t *o, void *, const void *p) { return original(o, *static_cast<const uint32_t *>(p)); },
        [&](uint8_t *o, void *, const void *p) { return reinterpret_cast<cTSObject *>(o)->vf325(*static_cast<const uint32_t *>(p)); },
        [&](WorldP &w, const Trial &t) {
            if (t.oa.thrown >= 0) {
                ++injected;
                return;
            }
            bool helperCalled = false;
            for (size_t i = 0; i + 1 < t.oa.trace.size(); ++i) {
                if (t.oa.trace[i + 1] == (5u << 8 | 1u)) helperCalled = true;
                if (t.oa.trace[i] == OffsetIn(&w.fk[kAllocatorFake], w) && t.oa.trace[i + 1] == (4u << 8 | 1u)) ++freed;
            }
            if (helperCalled) ++answered;
            else ++none;
        });
    if (answered < 5000 || none < 3000 || freed < 10000 || injected < 300)
        ctx.Fail("coverage: answered %u none %u freed %u injected %u", answered, none, freed, injected);
}

// ---- vf195 (0x543ae0) ---------------------------------------------------------------------------------------------------

namespace {

// Fakes: kF2 the director framework, kF3 the service it gives (slot 11: kind -> mask), kF4 the cached service C (stand-in for
// GetCachedSingletonC), kF5 the container it makes, kF6..kF8 the objects its lookups give, kF9 the target's object.
uint32_t gMasks[16];

void *__cdecl DirectorStandIn()
{
    HelperHit(0x1ae18d, nullptr, 0, nullptr);
    return &gW->fk[kF2];
}

void *__cdecl CachedServiceStandIn()
{
    HelperHit(0x20df6f, nullptr, 0, nullptr);
    return &gW->fk[kF4];
}

uint32_t __fastcall FrameworkSlot3(void *self, void *, uint32_t a1, uint32_t a2, void **out)
{
    const uint32_t a[] = {a1, a2, reinterpret_cast<uint32_t>(out)};
    Hit(self, 3, 3, a);
    *out = &gW->fk[kF3];
    return 0;
}

uint32_t __fastcall MapperMask(void *self, void *, uint32_t kind)
{
    const uint32_t a[] = {kind};
    Hit(self, 11, 1, a);
    return gMasks[kind & 15];
}

uint32_t __fastcall MakeContainer(void *self, void *, void **out)
{
    const uint32_t a[] = {reinterpret_cast<uint32_t>(out)};
    const uint32_t h = Hit(self, 34, 1, a);
    if (static_cast<uint8_t>(h) != 0 && (h & 0x100) != 0) *out = &gW->fk[kF5]; // sometimes the answer is true without a container
    return h;
}

template <int S> uint32_t __fastcall Lookup(void *self, void *, uint32_t id, void **out)
{
    const uint32_t a[] = {id, reinterpret_cast<uint32_t>(out)};
    const uint32_t h = Hit(self, S, 2, a);
    if ((h & 0x100) != 0) *out = &gW->fk[kF6 + (h >> 9) % 3]; // written even when the answer is false
    return h;
}

void BuildFillTarget(WorldP &w, uint32_t seed)
{
    std::mt19937 r = BaseWorld(w, seed, 0x543ae0, 60);
    // The two attributes (slot 13): noise in the upper half, which the low word hides.
    const uint32_t density = 1 + r() % 4;
    auto sparse = [&]() {
        uint32_t v = r();
        for (uint32_t i = 1; i < density; ++i) v &= r();
        return v & 0xffff;
    };
    uint32_t a = r() % 7 == 0 ? 0u : sparse();
    if (r() % 9 == 0) a |= 0x8000; // sign extension reaches the masks' upper half
    static const uint32_t kBits[] = {0x1, 0x2, 0x4000, 0x4, 0x800, 0x2000};
    uint32_t b = r() % 3 == 0 ? (r() & 0x7ff8 & ~0x4000u & ~0x800u) : 0u; // bits that are not counted
    for (const uint32_t bit : kBits)
        if (r() % (2 + density) == 0) b |= bit;
    const bool few = r() % 2 == 0; // few selections: at most one counted bit, most kinds unmapped (the single-selection paths)
    if (few) {
        const uint32_t counted = b & 0x6807u;
        b = (b & ~0x6807u) | (counted & (0u - counted));
    }
    if (r() % 11 == 0) b |= 0x8000;
    Use(w.vtThis, 13, 1);
    EnqueueValue(w.obj, 13, a | (r() & 0xffff0000u), w);
    EnqueueValue(w.obj, 13, b | (r() & 0xffff0000u), w);

    w.vtFk[kF2][3] = reinterpret_cast<void *>(&FrameworkSlot3);
    w.vtFk[kF3][11] = reinterpret_cast<void *>(&MapperMask);
    Use(w.vtFk[kF3], 2, 0);
    for (uint32_t &m : gMasks) {
        const uint32_t pick = r() % 8;
        m = pick < 3 ? 0u : pick < 7 ? 1u << (r() % 16) : r();
        if (pick < 5 && a != 0 && r() % 3 == 0) m = a & (0u - a); // the lowest bit a has: a sure match
    }
    if (few)
        for (uint32_t &m : gMasks)
            if (r() % 3 != 0) m = 0;
    // Kind 1 is the gate: often matching.
    if (r() % 4 != 0) gMasks[1] = a ? (a & (0u - a)) : 1u;

    Use(w.vtFk[kF4], 33, 1);
    RetValue(&w.fk[kF4], 33, NoiseAl(r, r() % 8 != 0), w);
    w.vtFk[kF4][34] = reinterpret_cast<void *>(&MakeContainer);
    RetValue(&w.fk[kF4], 34, NoiseAl(r, r() % 8 != 0) | (r() & 0x100), w);
    w.vtFk[kF4][35] = reinterpret_cast<void *>(&Lookup<35>);
    w.vtFk[kF4][36] = reinterpret_cast<void *>(&Lookup<36>);
    for (int n = 0; n < 16; ++n) {
        EnqueueValue(&w.fk[kF4], 35, NoiseAl(r, r() % 2 == 0) | (r() & 0xffffff00u), w);
        EnqueueValue(&w.fk[kF4], 36, NoiseAl(r, r() % 2 == 0) | (r() & 0xffffff00u), w);
    }
    Use(w.vtFk[kF4], 37, 2);
    Use(w.vtFk[kF4], 38, 2);
    RetValue(&w.fk[kF4], 37, NoiseAl(r, r() % 2 == 0), w);
    RetValue(&w.fk[kF4], 38, NoiseAl(r, r() % 2 == 0), w);
    for (int k = kF5; k <= kF8; ++k) Use(w.vtFk[k], 2, 0);

    // The target: a pointer to a pointer to the target's object (slot 19 takes an object).
    *reinterpret_cast<Fake **>(w.scratch + 0x180) = &w.fk[kF9];
    Use(w.vtFk[kF9], 19, 1);
    Use(w.vtFk[kF9], 2, 0);
}

} // namespace

DIFF_TEST(TSObject_vf195, 0x543ae0)
{
    CodePatch director(ctx, 0x1ae18d, reinterpret_cast<void *>(&DirectorStandIn));
    CodePatch directorOurs(reinterpret_cast<void *>(&GetDirectorFrameworkObject), reinterpret_cast<void *>(&DirectorStandIn));
    CodePatch cached(ctx, 0x20df6f, reinterpret_cast<void *>(&CachedServiceStandIn));
    CodePatch cachedOurs(reinterpret_cast<void *>(&GetCachedSingletonC), reinterpret_cast<void *>(&CachedServiceStandIn));
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0x543ae0);
    uint32_t nullTarget = 0, none = 0, oneBit = 0, oneKind = 0, many = 0, refused = 0, containerFailed = 0, added = 0, injected = 0;
    uint32_t manyBits = 0;
    // The target is null in some seeds: the pointer is taken from the trial's parameter block (its first dword stays zero then).
    RunSeeds(
        ctx, 40000, BuildFillTarget,
        [&](uint8_t *o, void *, const void *p) {
            const bool isNull = (static_cast<const uint8_t *>(p)[0] % 13) == 0;
            return static_cast<uint32_t>(original(o, isNull ? nullptr : gW->scratch + 0x180));
        },
        [&](uint8_t *o, void *, const void *p) {
            const bool isNull = (static_cast<const uint8_t *>(p)[0] % 13) == 0;
            return static_cast<uint32_t>(reinterpret_cast<cTSObject *>(o)->vf195(isNull ? nullptr : reinterpret_cast<void **>(gW->scratch + 0x180)));
        },
        [&](WorldP &w, const Trial &t) {
            if (t.oa.thrown >= 0) ++injected;
            const uint32_t cachedId = OffsetIn(&w.fk[kF4], w);
            bool saw37 = false, saw38 = false, saw33 = false, saw34 = false, saw35 = false;
            for (size_t i = 0; i + 1 < t.oa.trace.size(); ++i) {
                if (t.oa.trace[i] == cachedId) {
                    const uint32_t tag = t.oa.trace[i + 1];
                    saw37 |= tag == (37u << 8 | 2u);
                    saw38 |= tag == (38u << 8 | 2u);
                    saw33 |= tag == (33u << 8 | 1u);
                    saw34 |= tag == (34u << 8 | 1u);
                    saw35 |= tag == (35u << 8 | 2u);
                }
                if (t.oa.trace[i] == OffsetIn(&w.fk[kF9], w) && t.oa.trace[i + 1] == (19u << 8 | 1u)) ++added;
            }
            if (t.oa.trace.empty() && t.oa.thrown < 0) ++nullTarget;
            if (saw37) ++oneBit;
            if (saw38) ++oneKind;
            if (saw33 && !saw34) ++refused;
            if (saw34) ++many;
            if (saw35) ++manyBits;
            if (saw34 && t.oa.thrown < 0 && t.oa.ret == 0) ++containerFailed;
            if (t.oa.thrown < 0 && !saw33 && !saw37 && !saw38 && !t.oa.trace.empty()) ++none;
        });
    if (nullTarget < 1000 || none < 1500 || oneBit < 1500 || oneKind < 1500 || many < 5000 || manyBits < 2000 || refused < 300 ||
        containerFailed < 300 || added < 20000 || injected < 500)
        ctx.Fail("coverage: null %u none %u bit %u kind %u many %u (bits %u) refused %u containerFailed %u added %u injected %u", nullTarget, none,
                 oneBit, oneKind, many, manyBits, refused, containerFailed, added, injected);
}

// ---- vf261 (0x5290e0) ---------------------------------------------------------------------------------------------------

namespace {

// Fakes: kF4 the cached service C (stand-in for GetCachedSingletonC; slot 27 gives kF5), kF5 the string it hands out (slots 5, 6, 7),
// kF6 the output string (slot 16 takes the message, slot 31 is a cdecl Sprintf), kF7 the routing service (stand-in for 0x20dee0).
const char *const kLocations[] = {"", "tree 12 node 5", "a rather longer description of where the tree sim stopped, past the small buffer"};
const char *const kRouteTexts[] = {"", " (slot 3)", "a long routing slot description that does not fit into the small string buffer of the message"};

void PushText(const char *text, size_t length)
{
    uint32_t h = 0x57e70000u | static_cast<uint32_t>(length);
    for (size_t i = 0; i < length; ++i) h = Mix(h, static_cast<uint8_t>(text[i]), static_cast<uint32_t>(i));
    gTrace.push_back(h);
}

uint32_t gLocationLength = 0;
uint32_t gLocationPick = 0;
uint32_t gRoutePick = 0;
bool gServicePresent = true;

void *__cdecl RoutingServiceStandIn()
{
    HelperHit(0x20dee0, nullptr, 0, nullptr);
    return gServicePresent ? &gW->fk[kF7] : nullptr;
}

uint32_t __fastcall LocationText(void *self, void *)
{
    // Slots 5 and 6 (the same text).
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(0x5ee0u);
    return reinterpret_cast<uint32_t>(kLocations[gLocationPick]);
}

uint32_t __fastcall LocationLength(void *self, void *)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(7u << 8);
    return gLocationLength;
}

uint32_t __fastcall OutAssign(void *self, void *, const cRZString *message)
{
    const uint32_t a[] = {reinterpret_cast<uint32_t>(message)};
    Hit(self, 16, 1, a);
    PushText(message->mString.c_str(), message->mString.size());
    return reinterpret_cast<uint32_t>(self);
}

// out->Sprintf(fmt, message, location): a cdecl varargs member with `this` pushed first.
void __cdecl OutSprintf(void *self, const char *fmt, const char *first, const char *second)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(31u << 8);
    PushText(fmt, std::strlen(fmt));
    PushText(first, std::strlen(first));
    PushText(second, std::strlen(second));
}

// The routing service's slot 37 (kind, four out words): written with noise above the low word, sometimes with a zero low word or not at all.
uint32_t __fastcall RouteData(void *self, void *, uint32_t which, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d)
{
    const uint32_t args[] = {which, reinterpret_cast<uint32_t>(a), reinterpret_cast<uint32_t>(b), reinterpret_cast<uint32_t>(c),
                             reinterpret_cast<uint32_t>(d)};
    const uint32_t h = Hit(self, 37, 5, args);
    if (((h >> 8) & 7) == 0) return 0;
    uint32_t *const out[] = {a, b, c, d};
    for (int i = 0; i < 4; ++i) *out[i] = Mix(h, which, static_cast<uint32_t>(i));
    if (((h >> (which * 2)) & 3) == 0) *a &= 0xffff0000u;
    return 0;
}

uint32_t __fastcall RouteText(void *self, void *)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(39u << 8);
    return reinterpret_cast<uint32_t>(kRouteTexts[gRoutePick]);
}

const int32_t kErrorCodes[] = {-1, -0x8000, 0x7fff, 0xa2, 0xa3, 0x3e7, 0x3e8, 0x3e9, 0x3ea, 0x44b, 0x44c, 0x44d, 0x44e, 0x44f,
                               0x450, 0x451, 0x452, 0x453, 0x454, 0x4ff, 0x1000};

void BuildDescribe(WorldP &w, uint32_t seed)
{
    std::mt19937 r = BaseWorld(w, seed, 0x5290e0, 30);
    // The code: every value 0..0xa1 several times, the other interesting ones in turn.
    const uint32_t n = seed % 400;
    static const int32_t kBranching[] = {0x25, 0x80, 0x81, 0x9b, 0xa1, 0x3f, 0x40, 0x6b}; // the codes with their own handling or no text
    const int32_t code = seed % 3 == 0 ? kBranching[(seed / 3) % 8]
                         : n < 0xa4 * 2 ? static_cast<int32_t>(n % 0xa4)
                                        : kErrorCodes[n % (sizeof(kErrorCodes) / sizeof(kErrorCodes[0]))];
    RetValue(w.obj + 4, 17, (static_cast<uint32_t>(code) & 0xffff) | (r() & 0xffff0000u), w);
    Use(w.vtTree, 17, 0);

    gLocationPick = r() % 3;
    gLocationLength = r() % 5 == 0 ? r() % 4 : static_cast<uint32_t>(std::strlen(kLocations[gLocationPick]));
    if (gLocationLength > std::strlen(kLocations[gLocationPick])) gLocationLength = static_cast<uint32_t>(std::strlen(kLocations[gLocationPick]));
    gRoutePick = r() % 3;
    gServicePresent = r() % 5 != 0;

    Use(w.vtFk[kF4], 27, 0);
    RetObject(&w.fk[kF4], 27, &w.fk[kF5], w);
    w.vtFk[kF5][5] = reinterpret_cast<void *>(&LocationText);
    w.vtFk[kF5][6] = reinterpret_cast<void *>(&LocationText);
    w.vtFk[kF5][7] = reinterpret_cast<void *>(&LocationLength);
    w.vtFk[kF6][16] = reinterpret_cast<void *>(&OutAssign);
    w.vtFk[kF6][31] = reinterpret_cast<void *>(&OutSprintf);
    w.vtFk[kF7][37] = reinterpret_cast<void *>(&RouteData);
    w.vtFk[kF7][39] = reinterpret_cast<void *>(&RouteText);
}

} // namespace

DIFF_TEST(TSObject_vf261, 0x5290e0)
{
    CodePatch cached(ctx, 0x20df6f, reinterpret_cast<void *>(&CachedServiceStandIn));
    CodePatch cachedOurs(reinterpret_cast<void *>(&GetCachedSingletonC), reinterpret_cast<void *>(&CachedServiceStandIn));
    CodePatch routing(ctx, 0x20dee0, reinterpret_cast<void *>(&RoutingServiceStandIn));
    auto original = ctx.Original<void(__thiscall *)(void *, void *)>(0x5290e0);
    uint32_t formatted = 0, serviceText = 0, appended = 0, located = 0, injected = 0;
    RunSeeds(
        ctx, 20000, BuildDescribe,
        [&](uint8_t *o, void *, const void *) {
            original(o, &gW->fk[kF6]);
            return 0u;
        },
        [&](uint8_t *o, void *, const void *) {
            reinterpret_cast<cTSObject *>(o)->vf261(reinterpret_cast<cIGZString *>(&gW->fk[kF6]));
            return 0u;
        },
        [&](WorldP &w, const Trial &t) {
            if (t.oa.thrown >= 0) ++injected;
            const uint32_t out = OffsetIn(&w.fk[kF6], w);
            for (size_t i = 0; i + 1 < t.oa.trace.size(); ++i) {
                if (t.oa.trace[i] == out && t.oa.trace[i + 1] == (31u << 8)) ++located;
                if (t.oa.trace[i] == OffsetIn(&w.fk[kF7], w) && t.oa.trace[i + 1] == (39u << 8)) ++appended;
                if (t.oa.trace[i] == OffsetIn(&w.fk[kF7], w) && t.oa.trace[i + 1] == (37u << 8 | 5u)) ++formatted;
                if (t.oa.trace[i] == OffsetIn(&w.fk[kF5], w) && t.oa.trace[i + 1] == (27u << 8)) ++serviceText;
            }
        },
        nullptr);
    if (located < 5000 || appended < 100 || formatted < 200 || injected < 300)
        ctx.Fail("coverage: located %u appended %u formatted %u injected %u", located, appended, formatted, injected);
}

// ---- vf330 (0x53fbb0) and the placement callback (0x545f90) -------------------------------------------------------------

namespace {

void *gLongPlace[400];
uint32_t gAttr10 = 0, gAttr46 = 0;
uint32_t gFacing = 0;
void *gCallbackAddress = nullptr; // the original callback in the mapped image
uint32_t gTileBytes = 0;

constexpr uint32_t kCallbackMarker = 0xca11bac4u;

// Slot 13: the value depends on the attribute asked for (10 and 0x46 are the ones vf330 reads).
uint32_t __fastcall PlaceAttribute(void *self, void *, uint32_t index)
{
    const uint32_t a[] = {index};
    Hit(self, 13, 1, a);
    return index == 0x46 ? gAttr46 : gAttr10;
}

// Slot 311 (interface id, out): logged; a non-zero al gives the helper fake.
uint32_t __fastcall PlaceQuery(void *self, void *, uint32_t iid, void **out)
{
    const uint32_t a[] = {iid, reinterpret_cast<uint32_t>(out)};
    const uint32_t answer = Hit(self, 311, 2, a);
    if (static_cast<uint8_t>(answer) != 0) *out = &gW->fk[kF3];
    return answer;
}

// Slot 0 (interface id 1, out): the group interface.
uint32_t __fastcall PlaceGroup(void *self, void *, uint32_t iid, void **out)
{
    const uint32_t a[] = {iid, reinterpret_cast<uint32_t>(out)};
    const uint32_t answer = Hit(self, 0, 2, a);
    *out = &gW->fk[kF5];
    return answer;
}

// Slot 42 (out): writes three dwords and returns out.
uint32_t __fastcall PlacePosition(void *self, void *, uint32_t *out)
{
    const uint32_t a[] = {reinterpret_cast<uint32_t>(out)};
    const uint32_t h = Hit(self, 42, 1, a);
    out[0] = h;
    out[1] = Mix(h, 1, 1);
    out[2] = Mix(h, 2, 2);
    return reinterpret_cast<uint32_t>(out);
}

// Slot 65 (position, address of the facing notch): the position and the notch are logged by value; the answer is the rectangle list.
uint32_t __fastcall PlaceRects(void *self, void *, const uint32_t *position, const uint32_t *notch)
{
    const uint32_t a[] = {position[0], position[1], position[2], *notch};
    const uint32_t h = Hit(self, 65, 4, a);
    (void)h;
    return reinterpret_cast<uint32_t>(gW->scratch + 0x200);
}

// Slot 101 (out): three bytes.
uint32_t __fastcall PlaceTileBytes(void *self, void *, uint8_t *out)
{
    const uint32_t a[] = {reinterpret_cast<uint32_t>(out)};
    Hit(self, 101, 1, a);
    out[0] = static_cast<uint8_t>(gTileBytes);
    out[1] = static_cast<uint8_t>(gTileBytes >> 8);
    out[2] = static_cast<uint8_t>(gTileBytes >> 16);
    return reinterpret_cast<uint32_t>(out);
}

// The placer's slot 23: eleven stack arguments (the callee pops them), all logged; the callback address as a marker.
void __fastcall PlaceRect(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7,
                          uint32_t a8, uint32_t a9, uint32_t a10, uint32_t a11)
{
    const bool isCallback = a9 == reinterpret_cast<uint32_t>(gCallbackAddress) ||
                            a9 == reinterpret_cast<uint32_t>(reinterpret_cast<void *>(&PlacementEventCheck));
    const uint32_t a[] = {a1, a2, a3, a4, a5, a6, a7, a8, isCallback ? kCallbackMarker : a9, a10, a11};
    Hit(self, 23, 11, a);
}

float RandomFloat(std::mt19937 &r)
{
    static const uint32_t kBits[] = {0x00000000u, 0x3f800000u, 0xbf800000u, 0x40200000u, 0x7fc00000u, 0xffc00000u, 0x7f800000u, 0xff800000u,
                                     0x80000000u, 0x3f800001u, 0x41200000u};
    uint32_t bits = kBits[r() % 11];
    if (r() % 4 == 0) bits = r();
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

void BuildPlace(WorldP &w, uint32_t seed)
{
    std::mt19937 r = BaseWorld(w, seed, 0x53fbb0, 40);
    *reinterpret_cast<void **>(w.obj + 0x40) = &w.fk[kF6];
    w.obj[0x16a] = static_cast<uint8_t>(r() % 2 ? r() | 1 : r() & ~1);

    // Flags: true (nonzero low byte, noise above) in `percent` of the worlds.
    auto openAl = [&](int percent) { return NoiseAl(r, static_cast<int>(r() % 100) < percent); };
    auto gate = [&](int slot, int percent) {
        Use(w.vtThis, slot, 0);
        RetValue(w.obj, slot, openAl(percent), w);
    };
    gate(219, 10);
    Use(w.vtThis, 71, 0);
    RetValue(w.obj, 71, r() % 6 == 0 ? (r() | 1u) : 0u, w);
    Use(w.vtThis, 98, 0);
    static const uint32_t kKinds[] = {0x12, 0xd, 9, 0xc, 8, 2, 3, 0x12, 9, 0xb};
    for (int n = 0; n < 5; ++n) EnqueueValue(w.obj, 98, r() % 10 == 0 ? r() : kKinds[r() % 10], w);
    gate(104, 50);
    Use(w.vtThis, 13, 1);
    w.vtThis[13] = reinterpret_cast<void *>(&PlaceAttribute);
    gAttr10 = (r() % 3 == 0 ? Edge16(r) : (r() % 5) - 1) | (r() & 0xffff0000u);
    gAttr46 = r() % 3 == 0 ? (r() | 0x10000u) & 0xffff0000u : (r() | 1u);
    gate(145, 15);
    gate(225, 50);
    for (size_t i = sizeof(w.vtThis) / sizeof(void *); i < 400; ++i) gLongPlace[i] = nullptr;
    gLongPlace[356] = reinterpret_cast<void *>(&Th<356, 0>::F);
    gLongPlace[355] = reinterpret_cast<void *>(&Th<355, 0>::F);
    gLongPlace[311] = reinterpret_cast<void *>(&PlaceQuery);
    RetValue(w.obj, 311, openAl(90), w);
    gate(188, 90);
    gate(119, 60);
    gate(100, 50);
    Use(w.vtThis, 93, 1);
    for (int n = 0; n < 6; ++n) EnqueueValue(w.obj, 93, r() % 2 ? 0u : (r() | 1u) , w);
    // Two scripted answers have a zero low word with noise above it.
    for (int n = 0; n < 3; ++n) EnqueueValue(w.obj, 93, r() % 2 ? (r() & 0xffff0000u) : (r() | 1u), w);
    Use(w.vtThis, 64, 0);
    RetObject(w.obj, 64, &w.fk[kF2], w);
    Use(w.vtFk[kF2], 3, 1);
    RetValue(&w.fk[kF2], 3, r() % 2 ? 0x93u : r() % 2 ? r() : 0x92u, w);
    Use(w.vtThis, 61, 0);
    RetObject(w.obj, 61, &w.fk[kF4], w);
    Use(w.vtFk[kF4], 8, 0);
    w.vtThis[0] = reinterpret_cast<void *>(&PlaceGroup);
    Use(w.vtFk[kF3], 2, 0);
    Use(w.vtFk[kF5], 2, 0);

    // The context and the placer.
    Use(w.vtFk[kF6], 18, 0);
    RetObject(&w.fk[kF6], 18, &w.fk[kF7], w);
    w.vtFk[kF7][23] = reinterpret_cast<void *>(&PlaceRect);

    Use(w.vtThis, 43, 0);
    RetValue(w.obj, 43, r(), w);
    w.vtThis[42] = reinterpret_cast<void *>(&PlacePosition);
    w.vtThis[65] = reinterpret_cast<void *>(&PlaceRects);
    gTileBytes = r();
    w.vtThis[101] = reinterpret_cast<void *>(&PlaceTileBytes);
    Use(w.vtThis, 36, 0);
    RetValue(w.obj, 36, r() % 3 == 0 ? (r() & 0xffff0000u) : r(), w);
    Use(w.vtThis, 47, 0);
    RetValue(w.obj, 47, r() % 3 == 0 ? (r() | 0x8000u) : r(), w);

    // The rectangle list: up to six rectangles, then the (begin, end) pair.
    const uint32_t count = r() % 8 == 0 ? 0 : 1 + r() % 6;
    float *rects = reinterpret_cast<float *>(w.scratch + 0x100);
    for (uint32_t i = 0; i < count; ++i) {
        const bool good = r() % 2 == 0;
        const float x1 = RandomFloat(r), y1 = RandomFloat(r);
        rects[i * 4 + 0] = x1;
        rects[i * 4 + 1] = y1;
        rects[i * 4 + 2] = good ? x1 + 1.0f : RandomFloat(r);
        rects[i * 4 + 3] = good ? y1 + 1.0f : RandomFloat(r);
        if (r() % 5 == 0) rects[i * 4 + 3] = RandomFloat(r);
    }
    float **pair = reinterpret_cast<float **>(w.scratch + 0x200);
    pair[0] = rects;
    pair[1] = rects + count * 4;

    // The object gets a long copy of its vtable (the harness vtables stop at slot 255): the slots set above, then the extra ones.
    std::memcpy(gLongPlace, w.vtThis, sizeof(w.vtThis));
    reinterpret_cast<PrimPersonObject *>(w.obj)->mVTable = gLongPlace;
}

} // namespace

DIFF_TEST(TSObject_vf330, 0x53fbb0)
{
    gCallbackAddress = ctx.image.At<void>(0x545f90);
    auto original = ctx.Original<void(__thiscall *)(void *)>(0x53fbb0);
    uint32_t placements = 0, tile = 0, callbacks = 0, early = 0, injected = 0, noWeight = 0;
    RunSeeds(
        ctx, 40000, BuildPlace,
        [&](uint8_t *o, void *, const void *) {
            original(o);
            return 0u;
        },
        [&](uint8_t *o, void *, const void *) {
            reinterpret_cast<cTSObject *>(o)->vf330();
            return 0u;
        },
        [&](WorldP &w, const Trial &t) {
            if (t.oa.thrown >= 0) ++injected;
            const uint32_t placer = OffsetIn(&w.fk[kF7], w);
            uint32_t mine = 0;
            for (size_t i = 0; i + 1 < t.oa.trace.size(); ++i) {
                if (t.oa.trace[i] == placer && t.oa.trace[i + 1] == (23u << 8 | 11u)) {
                    ++mine;
                    // The trace holds the object, the tag and the eleven arguments: weight at +8, mode +9, callback +10, last +12.
                    if (i + 12 < t.oa.trace.size()) {
                        if (t.oa.trace[i + 10] == kCallbackMarker) ++callbacks;
                        if (t.oa.trace[i + 8] == 0x7f7fffffu) ++noWeight;
                        if (t.oa.trace[i + 12] == 1) ++tile;
                    }
                }
            }
            placements += mine;
            if (mine == 0 && t.oa.thrown < 0) ++early;
        },
        nullptr);
    if (placements < 20000 || tile < 3000 || callbacks < 3000 || noWeight < 3000 || early < 3000 || injected < 500)
        ctx.Fail("coverage: placements %u tile %u callbacks %u noWeight %u early %u injected %u", placements, tile, callbacks, noWeight, early,
                 injected);
}

namespace {

void BuildCallback(WorldP &w, uint32_t seed)
{
    std::mt19937 r = BaseWorld(w, seed, 0x545f90, 8);
    // The owner (kF2) hands out the event interface (kF3) through its slot 0; the interface is always written.
    w.vtFk[kF2][0] = reinterpret_cast<void *>(&PlaceGroup);
    Use(w.vtFk[kF5], 238, 5);
    RetValue(&w.fk[kF5], 238, NoiseAl(r, r() % 2 == 0), w);
    w.vtFk[kF5][13] = reinterpret_cast<void *>(&PlaceAttribute);
    Use(w.vtFk[kF3], 2, 0);
    Use(w.vtFk[kF5], 2, 0);
    gAttr10 = (r() % 3 == 0 ? Edge16(r) : (r() % 5) - 1) | (r() & 0xffff0000u);
}

} // namespace

DIFF_TEST(TSObject_PlacementEventCheck, 0x545f90)
{
    auto original = ctx.Original<uint8_t(__cdecl *)(int32_t *, float *, void *, const uint16_t *)>(0x545f90);
    uint32_t accepted = 0, refused = 0, noId = 0, injected = 0;
    RunSeeds(
        ctx, 20000, BuildCallback,
        [&](uint8_t *o, void *, const void *p) {
            (void)o;
            const uint8_t *b = static_cast<const uint8_t *>(p);
            const uint16_t id = static_cast<uint16_t>(b[0] | b[1] << 8);
            return static_cast<uint32_t>(original(reinterpret_cast<int32_t *>(gW->scratch + 0x10), reinterpret_cast<float *>(gW->scratch + 0x14),
                                                  &gW->fk[kF2], b[2] % 7 == 0 ? nullptr : &id));
        },
        [&](uint8_t *o, void *, const void *p) {
            (void)o;
            const uint8_t *b = static_cast<const uint8_t *>(p);
            const uint16_t id = static_cast<uint16_t>(b[0] | b[1] << 8);
            return static_cast<uint32_t>(PlacementEventCheck(reinterpret_cast<int32_t *>(gW->scratch + 0x10),
                                                            reinterpret_cast<float *>(gW->scratch + 0x14), &gW->fk[kF2],
                                                            b[2] % 7 == 0 ? nullptr : &id));
        },
        [&](WorldP &, const Trial &t) {
            if (t.oa.thrown >= 0) ++injected;
            else if (t.oa.trace.empty()) ++noId;
            else if (t.oa.ret & 0xff) ++accepted;
            else ++refused;
        },
        [](const Outcome &) { return false; });
    if (accepted < 3000 || refused < 3000 || noId < 1000 || injected < 300)
        ctx.Fail("coverage: accepted %u refused %u noId %u injected %u", accepted, refused, noId, injected);
}

// ---- vf65 (0x5426a0) ----------------------------------------------------------------------------------------------------

namespace {

// Scratch layout: the position (2 floats) at 0x20, the notch at 0x30, the base rectangles at 0x100 (up to 8), the cache's first
// storage at 0x180 (room for 2 rectangles), the grown storage at 0x200 (room for 16).
constexpr uint32_t kBaseRects = 0x100, kCacheFirst = 0x180, kCacheGrown = 0x200;
uint32_t gBaseCount = 0;
struct CacheLayout {
    float *mBegin, *mEnd, *mCapacity;
};

// 0x7eaf50 (cdecl: object, 12-byte out, 4-byte out, the list): builds the base list (pointers to the scratch rectangles).
void __cdecl BuilderStandIn(void *self, uint32_t *extent, uint32_t *count, uint32_t *list)
{
    const uint32_t a[] = {reinterpret_cast<uint32_t>(extent), reinterpret_cast<uint32_t>(count), reinterpret_cast<uint32_t>(list),
                          extent[0], extent[1], extent[2], *count};
    HelperHit(0x7eaf50, self, 7, a);
    list[0] = reinterpret_cast<uint32_t>(gW->scratch + kBaseRects);
    list[1] = reinterpret_cast<uint32_t>(gW->scratch + kBaseRects + gBaseCount * 16);
}

// 0x255f70 (thiscall: vector, where, value): the vector's growing insert; the stand-in moves the rectangles to the grown storage.
void __fastcall GrowStandIn(CacheLayout *vec, void *, float *where, const float *value)
{
    uint32_t bits[4];
    std::memcpy(bits, value, 16);
    const uint32_t a[] = {reinterpret_cast<uint32_t>(where), bits[0], bits[1], bits[2], bits[3],
                          static_cast<uint32_t>(vec->mEnd - vec->mBegin), static_cast<uint32_t>(vec->mCapacity - vec->mBegin)};
    HelperHit(0x255f70, vec, 7, a);
    float *const grown = reinterpret_cast<float *>(gW->scratch + kCacheGrown);
    const size_t used = vec->mBegin ? static_cast<size_t>(vec->mEnd - vec->mBegin) : 0;
    if (vec->mBegin && vec->mBegin != grown) std::memmove(grown, vec->mBegin, used * sizeof(float));
    std::memcpy(grown + used, bits, 16);
    vec->mBegin = grown;
    vec->mEnd = grown + used + 4;
    vec->mCapacity = grown + 64;
}

float PoolFloat(std::mt19937 &r)
{
    switch (r() % 12) {
    case 0: return RandomFloat(r); // specials: NaN, infinities, signed zeros, raw bits
    case 1: return static_cast<float>(static_cast<int32_t>(r() % 33) - 16) * 0.0625f;
    case 2: return static_cast<float>(static_cast<int32_t>(r() % 17) - 8) * 0.125f;
    case 3: return static_cast<float>(static_cast<int32_t>(r() % 17) - 8) * 0.125f + 0.01f;
    case 4: return static_cast<float>(static_cast<int32_t>(r() % 17) - 8) * 0.125f - 0.01f;
    default: return (static_cast<float>(r() % 2001) - 1000.0f) * 0.004f;
    }
}

void BuildFootprintWorld(WorldP &w, uint32_t seed)
{
    std::mt19937 r = BaseWorld(w, seed, 0x5426a0, 12);
    float *position = reinterpret_cast<float *>(w.scratch + 0x20);
    position[0] = PoolFloat(r);
    position[1] = PoolFloat(r);
    *reinterpret_cast<uint32_t *>(w.scratch + 0x30) = r() % 4 == 0 ? r() : r() % 8;

    gBaseCount = r() % 10 == 0 ? 0 : 1 + r() % 8;
    float *rects = reinterpret_cast<float *>(w.scratch + kBaseRects);
    for (uint32_t i = 0; i < 8; ++i) {
        const float x1 = PoolFloat(r), y1 = PoolFloat(r);
        static const float kWidths[] = {0.0f, 0.03f, 0.0625f, 0.07f, 0.1f, 0.125f, 0.3f, 0.5f, 1.0f, 2.0f, -0.5f, 5.0f};
        rects[i * 4 + 0] = x1;
        rects[i * 4 + 1] = y1;
        rects[i * 4 + 2] = r() % 8 == 0 ? PoolFloat(r) : x1 + kWidths[r() % 12];
        rects[i * 4 + 3] = r() % 8 == 0 ? PoolFloat(r) : y1 + kWidths[r() % 12];
    }
    const bool built = r() % 2 == 0;
    w.obj[0x293] = built ? static_cast<uint8_t>(1 + r() % 255) : 0;
    if (built) {
        uint32_t *list = reinterpret_cast<uint32_t *>(w.obj + 0x1e4);
        list[0] = reinterpret_cast<uint32_t>(w.scratch + kBaseRects);
        list[1] = reinterpret_cast<uint32_t>(w.scratch + kBaseRects + gBaseCount * 16);
    }
    // The cache vector: empty without storage, full, or part full; stale extent values either way.
    CacheLayout *cache = reinterpret_cast<CacheLayout *>(w.obj + 0x200);
    float *first = reinterpret_cast<float *>(w.scratch + kCacheFirst);
    switch (r() % 5) {
    case 0: cache->mBegin = cache->mEnd = cache->mCapacity = nullptr; break;
    case 1: cache->mBegin = cache->mEnd = cache->mCapacity = first; break;
    case 2: cache->mBegin = first; cache->mEnd = first + 4; cache->mCapacity = first + 8; break;
    case 3: cache->mBegin = first; cache->mEnd = first + 8; cache->mCapacity = first + 8; break;
    default: cache->mBegin = first; cache->mEnd = first; cache->mCapacity = first + 8; break;
    }
}

} // namespace

DIFF_TEST(TSObject_vf65, 0x5426a0)
{
    CodePatch builder(ctx, 0x7eaf50, reinterpret_cast<void *>(&BuilderStandIn));
    CodePatch builderOurs(reinterpret_cast<void *>(&BuildFootprint), reinterpret_cast<void *>(&BuilderStandIn));
    CodePatch grow(ctx, 0x255f70, reinterpret_cast<void *>(&GrowStandIn));
    auto original = ctx.Original<void *(__thiscall *)(void *, const float *, const uint32_t *)>(0x5426a0);
    uint32_t built = 0, grown = 0, notches[8] = {}, injected = 0, empty = 0;
    RunSeeds(
        ctx, 40000, BuildFootprintWorld,
        [&](uint8_t *o, void *, const void *) {
            return Norm(reinterpret_cast<uint32_t>(original(o, reinterpret_cast<float *>(gW->scratch + 0x20),
                                                            reinterpret_cast<uint32_t *>(gW->scratch + 0x30))));
        },
        [&](uint8_t *o, void *, const void *) {
            return Norm(reinterpret_cast<uint32_t>(reinterpret_cast<cTSObject *>(o)->vf65(
                reinterpret_cast<float *>(gW->scratch + 0x20), reinterpret_cast<uint32_t *>(gW->scratch + 0x30))));
        },
        [&](WorldP &w, const Trial &t) {
            if (t.oa.thrown >= 0) ++injected;
            for (size_t i = 0; i < t.oa.trace.size(); ++i) {
                if (t.oa.trace[i] == (0x80000000u | 0x7eaf50)) ++built;
                if (t.oa.trace[i] == (0x80000000u | 0x255f70)) ++grown;
            }
            ++notches[*reinterpret_cast<uint32_t *>(w.scratch + 0x30) & 7];
            if (gBaseCount == 0) ++empty;
        });
    uint32_t fewest = notches[0];
    for (const uint32_t n : notches) fewest = n < fewest ? n : fewest;
    if (built < 10000 || grown < 10000 || fewest < 2000 || empty < 1000)
        ctx.Fail("coverage: built %u grown %u fewest notch %u empty %u", built, grown, fewest, empty);
}

// ---- 0x7dd070 (the notch of a facing word) ------------------------------------------------------------------------------

DIFF_TEST(TSObject_FacingPointerToNotch, 0x7dd070)
{
    auto original = ctx.Original<int32_t(__cdecl *)(const int32_t *)>(0x7dd070);
    std::mt19937 r(0x7dd070);
    static const int32_t kEdges[] = {0, 1, 2, 9, 10, 11, 17, 18, -1, -2, -7, -8, -9, 0x7fffffff, INT32_MIN, INT32_MIN + 1, 0xffff, 0x10000, -0xffff};
    for (int i = 0; i < 4000; ++i) {
        const int32_t value = i < 19 ? kEdges[i] : (i % 3 == 0 ? static_cast<int32_t>(r()) : static_cast<int32_t>(r() % 40) - 20);
        ++ctx.cases;
        const int32_t a = original(&value), b = FacingPointerToNotch(&value);
        if (a != b) ctx.Fail("facing %d: original %d, ours %d", value, a, b);
    }
}

// ---- 0x7e86e0 (the footprint size) --------------------------------------------------------------------------------------

namespace {

void BuildSize(WorldP &w, uint32_t seed)
{
    std::mt19937 r = BaseWorld(w, seed, 0x7e86e0, 10);
    Use(w.vtThis, 98, 0);
    EnqueueValue(w.obj, 98, r() % 3 == 0 ? 2u : r() % 4 == 0 ? r() : r() % 12, w);
    Use(w.vtThis, 64, 0);
    RetObject(w.obj, 64, &w.fk[kF2], w);
    Use(w.vtFk[kF2], 1, 0);
    Use(w.vtFk[kF2], 2, 0);
    Use(w.vtFk[kF2], 3, 1);
    const uint32_t pick = r() % 5;
    RetValue(&w.fk[kF2], 3, pick == 0 ? 0u : pick == 1 ? 0xffffffffu : pick == 2 ? (r() % 20) : r(), w);
}

} // namespace

DIFF_TEST(TSObject_FootprintSize, 0x7e86e0)
{
    auto original = ctx.Original<float(__cdecl *)(void *)>(0x7e86e0);
    uint32_t person = 0, zero = 0, scaled = 0, injected = 0;
    RunSeeds(
        ctx, 10000, BuildSize,
        [&](uint8_t *o, void *, const void *) {
            const float f = original(o);
            uint32_t bits;
            std::memcpy(&bits, &f, 4);
            return bits;
        },
        [&](uint8_t *o, void *, const void *) {
            const float f = FootprintSize(reinterpret_cast<cTSObject *>(o));
            uint32_t bits;
            std::memcpy(&bits, &f, 4);
            return bits;
        },
        [&](WorldP &, const Trial &t) {
            if (t.oa.thrown >= 0) ++injected;
            else if (t.oa.ret == 0x3e4ccccdu) ++person;
            else if (t.oa.ret == 0x3f800000u) ++zero;
            else ++scaled;
        });
    if (person < 1000 || zero < 1000 || scaled < 3000 || injected < 200)
        ctx.Fail("coverage: person %u zero %u scaled %u injected %u", person, zero, scaled, injected);
}

// ---- 0x7eaf50 (build a footprint) ---------------------------------------------------------------------------------------

namespace {

uint32_t gFoundCount = 0;     // rectangles slot 183 reports (they sit at scratch + 0x100)
uint32_t gFoundCapacity = 0;  // room for them in the buffer
bool gFoundBuffer = true;     // false: the buffer stays empty without storage
uint32_t gAttr8 = 0;

uint32_t __fastcall BuildAttribute(void *self, void *, uint32_t index)
{
    const uint32_t a[] = {index};
    Hit(self, 13, 1, a);
    return index == 8 ? gAttr8 : 0;
}

// Slot 85 (out): three floats from the script.
uint32_t __fastcall BuildOrigin(void *self, void *, float *out)
{
    const uint32_t a[] = {reinterpret_cast<uint32_t>(out)};
    const uint32_t h = Hit(self, 85, 1, a);
    std::mt19937 r(h);
    out[0] = PoolFloat(r);
    out[1] = PoolFloat(r);
    out[2] = PoolFloat(r);
    return 0;
}

// Slot 183 (tile x, tile y, the rectangle buffer, 0, the level word): logged by value, with the buffer's extent as it comes in
// (the buffer is a whole footprint cache, 0x1c bytes: the vector, then the four bounds); the buffer is filled from the script,
// and the extent rewritten as the game's callee does.
uint32_t __fastcall BuildRects(void *self, void *, int32_t x, int32_t y, uint32_t *buffer, uint32_t zero, uint32_t levelWord)
{
    const uint32_t a[] = {static_cast<uint32_t>(x), static_cast<uint32_t>(y), reinterpret_cast<uint32_t>(buffer), zero, levelWord,
                          buffer[3], buffer[4], buffer[5], buffer[6]};
    const uint32_t h = Hit(self, 183, 9, a);
    std::mt19937 r(h);
    for (int i = 3; i < 7; ++i) buffer[i] = r();
    if (gFoundBuffer) {
        buffer[0] = reinterpret_cast<uint32_t>(gW->scratch + 0x100);
        buffer[1] = reinterpret_cast<uint32_t>(gW->scratch + 0x100 + gFoundCount * 16);
        buffer[2] = reinterpret_cast<uint32_t>(gW->scratch + 0x100 + gFoundCapacity * 16);
    }
    return h;
}

// The person helper's slot 10 (entry, 1).
uint32_t __fastcall BuildEntry(void *self, void *, uint32_t entry, uint32_t one)
{
    const uint32_t a[] = {entry, one};
    return Hit(self, 10, 2, a);
}

void BuildBuildFootprint(WorldP &w, uint32_t seed)
{
    std::mt19937 r = BaseWorld(w, seed, 0x7eaf50, 40);
    float *position = reinterpret_cast<float *>(w.scratch + 0x20);
    position[0] = PoolFloat(r);
    position[1] = PoolFloat(r);
    *reinterpret_cast<uint32_t *>(w.scratch + 0x30) = r() % 4 == 0 ? r() : r() % 8;

    // Kind 2 (person) in a third of the worlds, the others random kinds.
    Use(w.vtThis, 98, 0);
    for (int n = 0; n < 3; ++n) EnqueueValue(w.obj, 98, r() % 3 == 0 ? 2u : r() % 5 == 0 ? r() : r() % 12, w);
    // The attribute table: slot 5 (entry 0x4d) the packed margins, slot 3 (0x43) the size steps; counted.
    Use(w.vtThis, 64, 0);
    RetObject(w.obj, 64, &w.fk[kF2], w);
    Use(w.vtFk[kF2], 1, 0);
    Use(w.vtFk[kF2], 2, 0);
    Use(w.vtFk[kF2], 3, 1);
    Use(w.vtFk[kF2], 5, 1);
    const uint32_t sizePick = r() % 4;
    RetValue(&w.fk[kF2], 3, sizePick == 0 ? 0u : sizePick == 1 ? 1u : r() % 40, w);
    const uint32_t marginPick = r() % 5;
    RetValue(&w.fk[kF2], 5, marginPick == 0 ? 0u : marginPick == 1 ? (r() & 0xffff0000u) : marginPick == 2 ? (r() & 0xf) : r(), w);
    // The person helper: QueryInterface through slot 0 always writes kF3.
    w.vtThis[0] = reinterpret_cast<void *>(&PlaceGroup);
    w.vtFk[kF5][10] = reinterpret_cast<void *>(&BuildEntry);
    Use(w.vtFk[kF5], 2, 0);
    RetValue(&w.fk[kF5], 10, r() % 3 == 0 ? (r() & 0xffff0000u) : r(), w);
    RetValue(w.obj, 0, NoiseAl(r, r() % 4 != 0), w);
    gAttr8 = r() % 4 == 0 ? 0x80u | (r() & 0xff00u) : (r() & 0xff7f) | (r() & 0xffff0000u);
    w.vtThis[13] = reinterpret_cast<void *>(&BuildAttribute);
    w.vtThis[85] = reinterpret_cast<void *>(&BuildOrigin);
    Use(w.vtThis, 107, 0);
    RetValue(w.obj, 107, r(), w);
    w.vtThis[183] = reinterpret_cast<void *>(&BuildRects);
    RetValue(w.obj, 183, NoiseAl(r, r() % 4 != 0), w);
    gFoundBuffer = r() % 6 != 0;
    gFoundCount = r() % 5 == 0 ? 0 : 1 + r() % 6;
    gFoundCapacity = gFoundCount + r() % 3;
    float *rects = reinterpret_cast<float *>(w.scratch + 0x100);
    for (uint32_t i = 0; i < 8; ++i) {
        const float x1 = PoolFloat(r), y1 = PoolFloat(r);
        static const float kWidths[] = {0.0f, 0.03f, 0.0625f, 0.07f, 0.1f, 0.125f, 0.3f, 0.5f, 1.0f, 2.0f, -0.5f, 5.0f};
        rects[i * 4 + 0] = x1;
        rects[i * 4 + 1] = y1;
        rects[i * 4 + 2] = r() % 8 == 0 ? PoolFloat(r) : x1 + kWidths[r() % 12];
        rects[i * 4 + 3] = r() % 8 == 0 ? PoolFloat(r) : y1 + kWidths[r() % 12];
    }
    // The cache to fill.
    CacheLayout *cache = reinterpret_cast<CacheLayout *>(w.scratch + 0x380);
    float *first = reinterpret_cast<float *>(w.scratch + 0x3a0);
    switch (r() % 5) {
    case 0: cache->mBegin = cache->mEnd = cache->mCapacity = nullptr; break;
    case 1: cache->mBegin = cache->mEnd = cache->mCapacity = first; break;
    case 2: cache->mBegin = first; cache->mEnd = first + 4; cache->mCapacity = first + 8; break;
    case 3: cache->mBegin = first; cache->mEnd = first + 8; cache->mCapacity = first + 8; break;
    default: cache->mBegin = first; cache->mEnd = first; cache->mCapacity = first + 8; break;
    }
    uint32_t *extent = reinterpret_cast<uint32_t *>(w.scratch + 0x38c);
    for (int i = 0; i < 4; ++i) extent[i] = r();
    w.vtFk[kAllocatorFake][4] = reinterpret_cast<void *>(&ServiceFree);
}

} // namespace

DIFF_TEST(TSObject_BuildFootprint, 0x7eaf50)
{
    void **const cacheSlot = ctx.image.At<void *>(0x10c95d4);
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
    } saved{cacheSlot, guard, *cacheSlot, *guard};
    gBeforeRun = [&](WorldP &w) {
        *cacheSlot = &w.fk[kAllocatorFake];
        *guard = INT32_MIN;
    };
    CodePatch grow(ctx, 0x255f70, reinterpret_cast<void *>(&GrowStandIn));
    auto original = ctx.Original<void(__cdecl *)(void *, const float *, const uint32_t *, void *)>(0x7eaf50);
    uint32_t viaRects = 0, square = 0, withMargins = 0, personHelper = 0, freed = 0, grown = 0, injected = 0, noRects = 0;
    RunSeeds(
        ctx, 40000, BuildBuildFootprint,
        [&](uint8_t *o, void *, const void *) {
            original(o, reinterpret_cast<float *>(gW->scratch + 0x20), reinterpret_cast<uint32_t *>(gW->scratch + 0x30), gW->scratch + 0x380);
            return 0u;
        },
        [&](uint8_t *o, void *, const void *) {
            BuildFootprint(reinterpret_cast<cTSObject *>(o), reinterpret_cast<float *>(gW->scratch + 0x20),
                           reinterpret_cast<uint32_t *>(gW->scratch + 0x30), reinterpret_cast<FootprintCache *>(gW->scratch + 0x380));
            return 0u;
        },
        [&](WorldP &w, const Trial &t) {
            if (t.oa.thrown >= 0) ++injected;
            bool asked = false, entry = false;
            for (size_t i = 0; i + 1 < t.oa.trace.size(); ++i) {
                if (t.oa.trace[i + 1] == (183u << 8 | 9u)) asked = true;
                if (t.oa.trace[i + 1] == (10u << 8 | 2u)) entry = true;
                if (t.oa.trace[i] == OffsetIn(&w.fk[kAllocatorFake], w) && t.oa.trace[i + 1] == (4u << 8 | 1u)) ++freed;
                if (t.oa.trace[i] == (0x80000000u | 0x255f70)) ++grown;
            }
            if (asked) ++viaRects;
            if (entry) ++personHelper;
            if (!asked && t.oa.thrown < 0) ++square;
            if (asked && gFoundBuffer && gFoundCount == 0) ++noRects;
            (void)withMargins;
        });
    if (viaRects < 5000 || square < 5000 || personHelper < 1500 || freed < 3000 || grown < 3000 || injected < 800 || noRects < 100)
        ctx.Fail("coverage: rects %u square %u person %u freed %u grown %u injected %u noRects %u", viaRects, square, personHelper, freed, grown,
                 injected, noRects);
}
