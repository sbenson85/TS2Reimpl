// cTSTreeTableEntry (lane GN): differential tests in the standalone shape (docs/standalone-shape.md, "Tests").
//
// Three routes per case, each on its own entry seeded alike:
//   route 0: the entry built by the original constructor 0x430f60 (game vtable), the original function called at its RVA;
//   route 1: the entry built by our constructor, the function called through our vtable, in the slot where the game's vtable
//            holds that RVA (shape::SlotOf: a slot-order mistake calls the wrong function);
//   route 2: the entry built by the original constructor carrying a copy of the game's vtable with an arity-matched trap in the
//            case's own slot, the RVA hook's adapter called (an adapter that dispatched virtually lands in the trap).
// Compared: the result, every fake call in order, and the whole entry (Capture: every field, the name, every advertisement
// with its vector sizes and capacities, the use records). The only bytes normalised are vtable words: the entry's own (the
// route's expected vtable becomes one code) and the advertisements' (game or ours, per kind, one code each: entries are
// seeded through our std::vector code, so both routes start with our element vtables, and new elements carry the creating
// side's). The two padding bytes of an advertisement (+0xa) are not compared: the original's element copy (0x430940) copies
// the dword at +4 and the word at +8 only, so they hold whatever the heap had.
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game_allocator.h"
#include "game/game.h"
#include "gz/cIGZString.h"
#include "shape.h"
#include "simantics/cTSTreeTableEntry.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

namespace {
namespace gn {
using Entry = cTSTreeTableEntry;
using A = cTSTreeTableEntryAdapters;
using Blob = std::vector<uint32_t>;

constexpr uint32_t kCtor = 0x430f60, kCopyCtor = 0x430d20, kDtor = 0x4310e0, kDeleting = 0x4312e0;
constexpr uint32_t kGameAdVT = 0xdc4430, kGameTraitVT = 0xdc4438, kGameTripleVT = 0xdc4428;
constexpr uint32_t kTuningCache = 0x10cc290; // GetCachedSingletonC's cache
constexpr uint32_t kEntryCode = 0xE0E0E0E0;

uint32_t P(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
uint32_t Bits(float f) { uint32_t b; std::memcpy(&b, &f, 4); return b; }
float Float(uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; }

template <class R, class... Args> R CallSlot(const void *object, unsigned slot, Args... args)
{
    using Fn = R(__thiscall *)(const void *, Args...);
    return (*static_cast<Fn *const *>(object))[slot](object, args...);
}

unsigned Slot(uint32_t rva)
{
    const unsigned slot = shape::SlotOf(Entry::kOriginalVTable, Entry::kVTableSlots, rva);
    if (slot == ~0u) {
        std::printf("  no slot holds 0x%x\n", rva);
        std::abort();
    }
    return slot;
}

// Our vtables (entry, advertisement, trait value, base triple).
struct OurVTables {
    const void *entry, *ad, *trait, *triple;
};
const OurVTables &Ours()
{
    static OurVTables tables{};
    if (!tables.entry) {
        Entry *e = new Entry;
        tables.entry = *reinterpret_cast<void **>(e);
        delete e;
        cTSTreeTableAd ad;
        cTSTraitAdValue trait;
        cTSTriple<int16_t> triple;
        tables.ad = *reinterpret_cast<void **>(&ad);
        tables.trait = *reinterpret_cast<void **>(&trait);
        tables.triple = *reinterpret_cast<void **>(&triple);
    }
    return tables;
}

uint32_t ElementCode(const void *object)
{
    const void *vt = *static_cast<const void *const *>(object);
    if (vt == GameAddress<void>(kGameAdVT) || vt == Ours().ad) return 0xA1;
    if (vt == GameAddress<void>(kGameTraitVT) || vt == Ours().trait) return 0xA2;
    if (vt == GameAddress<void>(kGameTripleVT) || vt == Ours().triple) return 0xA3;
    return P(vt);
}

// ---- the route's entry ----

void *gTrapVT[Entry::kVTableSlots]; // route 2's vtable copy
uint32_t gTrapHits;
uint32_t TrapHit(void *, unsigned) { return ++gTrapHits; }

const void *ExpectedVT(int route)
{
    return route == 1 ? Ours().entry : route == 2 ? static_cast<const void *>(gTrapVT) : GameAddress<void>(Entry::kOriginalVTable);
}

void Capture(Blob &out, int route, const Entry *e)
{
    const void *vt = *reinterpret_cast<const void *const *>(e);
    out.push_back(vt == ExpectedVT(route) ? kEntryCode : P(vt));
    out.insert(out.end(), {e->mRefCount, e->mCheckTreeId, e->mActionTreeId, uint32_t(uint16_t(e->mUIDisplayType)), e->mAttenuation,
        Bits(e->mCustomAttenuation), e->mFlags, e->mIndex, e->mAutonomyThreshold, e->mJoinIndex, e->mFacialAnimType,
        Bits(e->mMemoryIterativeMultiplier), e->mIconTableIndex, e->mIconGuid});
    const cRZString &name = e->mName.mString;
    out.push_back(*reinterpret_cast<const uint32_t *>(&name) == P(GameAddress<void>(0xd9b470)) ? 0x5A : 0x5B);
    out.push_back(name.mRefCount);
    out.push_back(uint32_t(name.mString.size()));
    for (char c : name.mString) out.push_back(uint8_t(c));
    out.push_back(uint32_t(e->mMotiveAds.size()));
    out.push_back(uint32_t(e->mMotiveAds.capacity()));
    for (const auto &ads : e->mMotiveAds) {
        out.push_back(uint32_t(ads.size()));
        out.push_back(uint32_t(ads.capacity()));
        for (const auto &ad : ads) out.insert(out.end(), {ElementCode(&ad), uint16_t(ad.mFirst), uint16_t(ad.mSecond), uint16_t(ad.mThird)});
    }
    out.push_back(uint32_t(e->mTraitAds.size()));
    out.push_back(uint32_t(e->mTraitAds.capacity()));
    for (const auto &list : e->mTraitAds) {
        out.push_back(uint32_t(list.size()));
        out.push_back(uint32_t(list.capacity()));
        for (const auto &values : list) {
            out.push_back(uint32_t(values.size()));
            out.push_back(uint32_t(values.capacity()));
            for (const auto &v : values) out.insert(out.end(), {ElementCode(&v), uint16_t(v.mFirst), uint16_t(v.mSecond), uint16_t(v.mThird)});
        }
    }
    out.push_back(uint32_t(e->mUses.size()));
    out.push_back(uint32_t(e->mUses.capacity()));
    for (const auto &use : e->mUses) out.insert(out.end(), {uint32_t(use.mTick), uint16_t(use.mActor), uint16_t(use.mZero)});
}

int16_t SmallValue(std::mt19937 &r) { return r() % 3 == 0 ? 0 : int16_t(int32_t(r() % 600) - 300); }

// Fields, name, advertisements and uses from the seed (through our std::vector code on every route).
void Seed(Entry *e, uint32_t seed)
{
    std::mt19937 r(seed);
    if (seed % 5 != 0) {
        e->mCheckTreeId = uint16_t(r());
        e->mActionTreeId = uint16_t(r());
        e->mUIDisplayType = int16_t(r());
        e->mAttenuation = r() % 7;
        e->mCustomAttenuation = float(int32_t(r() % 4000) - 2000) / 7.0f;
        e->mFlags = r();
        e->mIndex = r();
        const uint32_t thresholds[] = {0x32, 99, 100, 101, 0xffffffffu, r()};
        e->mAutonomyThreshold = thresholds[r() % 6];
        e->mJoinIndex = r();
        e->mFacialAnimType = r() % 9;
        e->mMemoryIterativeMultiplier = float(r() % 1000) / 333.0f;
        e->mIconTableIndex = r();
        e->mIconGuid = r();
        e->mName.mString.mString.assign(r() % 40, char('a' + r() % 26));
    }
    if (seed % 7 != 1) {
        e->mMotiveAds.resize(r() % 7);
        if (r() % 2) e->mMotiveAds.reserve(e->mMotiveAds.size() + r() % 4);
        for (auto &ads : e->mMotiveAds) {
            ads.resize(r() % 18);
            if (r() % 3 == 0) ads.reserve(ads.size() + r() % 5);
            for (auto &ad : ads) {
                ad.mFirst = r() % 4 == 0 ? int16_t(r() % 12) : 0;
                ad.mSecond = SmallValue(r);
                ad.mThird = SmallValue(r);
                if (r() % 4 == 0) ad.mSecond = ad.mThird = 0;
            }
        }
        e->mTraitAds.resize(r() % 10);
        for (auto &list : e->mTraitAds) {
            list.resize(r() % 18);
            for (auto &values : list) {
                values.resize(r() % 7);
                for (auto &v : values) {
                    if (r() % 3 == 0) continue; // all zero
                    v.mFirst = SmallValue(r);
                    v.mSecond = SmallValue(r);
                    v.mThird = SmallValue(r);
                }
            }
        }
    }
    const uint32_t uses = r() % 6;
    for (uint32_t i = 0; i < uses; ++i) e->mUses.push_back(cTSTreeTableUse{int32_t(r() % 4), int16_t(r() % 3), int16_t(r())});
    e->mRefCount = 1 + r() % 3;
}

Entry *Make(difftest::Context &ctx, int route, uint32_t seed)
{
    void *memory = ::operator new(Entry::kSize);
    std::memset(memory, 0xcd, Entry::kSize);
    Entry *e;
    if (route == 1) {
        e = new (memory) Entry;
    } else {
        e = static_cast<Entry *>(ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kCtor)(memory, 0x5eed0000 + seed)); // (a word it never reads)
    }
    Seed(e, seed);
    return e;
}

// Frees an entry with our destructor (not under test here; the destructors have their own test).
void Destroy(Entry *e)
{
    e->Entry::~Entry();
    ::operator delete(e);
}

// ---- fakes ----

Blob gLog;
void Note(uint32_t a, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0) { gLog.insert(gLog.end(), {0xCA11, a, b, c, d}); }

// A person (slots 10, 19, 20, 38).
void *gPersonVT[40];
void *gPerson[1] = {gPersonVT};
uint32_t gUsesMotiveAds, gStageA, gStageB, gPersonData;
uint32_t __fastcall PersonSlot38(void *, void *) { Note(38); return gUsesMotiveAds; }
uint32_t __fastcall PersonSlot19(void *, void *) { Note(19); return gStageA; }
uint32_t __fastcall PersonSlot20(void *, void *) { Note(20); return gStageB; }
uint32_t __fastcall PersonSlot10(void *, void *, uint32_t index, uint32_t flag)
{
    Note(10, index, flag);
    return gPersonData;
}

// The tuning service (slot 4: id, out float).
void *gTuningVT[8];
void *gTuning[1] = {gTuningVT};
uint32_t __fastcall TuningGet(void *, void *, uint32_t id, float *out)
{
    Note(4, id);
    *out = float(id) * 0.25f + 0.125f;
    return 0x77;
}
bool gTuningNull;
void *__cdecl StandTuning() { return gTuningNull ? nullptr : gTuning; }

// Streams: in (slots 6, 8, 9, 12, 18, 20 -> owner, whose slot 78 gives the version) and out (6, 8, 9, 12, 18).
void *gInVT[24], *gOutVT[24], *gOwnerVT[80];
void *gIn[1] = {gInVT}, *gOut[1] = {gOutVT}, *gOwner[1] = {gOwnerVT};
uint32_t gVersion, gStreamError;
// Two scripts: the 16-bit reads (any value) and the 32-bit ones (counts among them: small, so no vector grows past what the
// heap holds). A script that runs out reads zeros. gCursor counts the reads.
Blob gScript, gScript16;
size_t gCursor, gCursor32, gCursor16;
uint32_t Next(bool narrow)
{
    ++gCursor;
    if (narrow) return gCursor16 < gScript16.size() ? gScript16[gCursor16++] : 0;
    return gCursor32 < gScript.size() ? gScript[gCursor32++] : 0;
}
template <int S, class T> uint8_t __fastcall InGet(void *, void *, T *out)
{
    const uint32_t v = Next(sizeof(T) == 2);
    Note(S, v);
    std::memcpy(out, &v, sizeof(T));
    return 0x41;
}
uint8_t __fastcall InError(void *, void *) { Note(18); return uint8_t(gStreamError); }
void *__fastcall InOwner(void *, void *) { Note(20); return gOwner; }
uint32_t *__fastcall OwnerVersion(void *, void *) { Note(78); return &gVersion; }
template <int S, class T> uint8_t __fastcall OutSet(void *, void *, T value)
{
    uint32_t v = 0;
    std::memcpy(&v, &value, sizeof(T));
    Note(S, v);
    return 0x42;
}

void SetupFakes()
{
    gPersonVT[10] = reinterpret_cast<void *>(&PersonSlot10);
    gPersonVT[19] = reinterpret_cast<void *>(&PersonSlot19);
    gPersonVT[20] = reinterpret_cast<void *>(&PersonSlot20);
    gPersonVT[38] = reinterpret_cast<void *>(&PersonSlot38);
    gTuningVT[4] = reinterpret_cast<void *>(&TuningGet);
    gInVT[6] = reinterpret_cast<void *>(&InGet<6, int16_t>);
    gInVT[8] = reinterpret_cast<void *>(&InGet<8, int32_t>);
    gInVT[9] = reinterpret_cast<void *>(&InGet<9, uint32_t>);
    gInVT[12] = reinterpret_cast<void *>(&InGet<12, float>);
    gInVT[18] = reinterpret_cast<void *>(&InError);
    gInVT[20] = reinterpret_cast<void *>(&InOwner);
    gOwnerVT[78] = reinterpret_cast<void *>(&OwnerVersion);
    gOutVT[6] = reinterpret_cast<void *>(&OutSet<6, int16_t>);
    gOutVT[8] = reinterpret_cast<void *>(&OutSet<8, int32_t>);
    gOutVT[9] = reinterpret_cast<void *>(&OutSet<9, uint32_t>);
    gOutVT[12] = reinterpret_cast<void *>(&OutSet<12, float>);
    gOutVT[18] = reinterpret_cast<void *>(&InError);
}

// ---- running a case on the three routes ----

// One call on an entry of a route: `rva` the function, `slot` its slot (or ~0u for a non-virtual one).
struct Ctx {
    difftest::Context &ctx;
    int route;
    Entry *e;
    std::mt19937 &r;
};
using Case = uint32_t (*)(Ctx &c);

// Route 0 the original at `rva`, route 1 our vtable slot, route 2 the adapter.
template <class R, class... Args> R Invoke(Ctx &c, uint32_t rva, R(__fastcall *adapter)(Entry *, void *, Args...), std::type_identity_t<Args>... args)
{
    if (c.route == 0) return c.ctx.Original<R(__thiscall *)(void *, Args...)>(rva)(c.e, args...);
    if (c.route == 1) return CallSlot<R, Args...>(c.e, Slot(rva), args...);
    return adapter(c.e, nullptr, args...);
}

template <class R> uint32_t Wide(R value)
{
    if constexpr (std::is_same_v<R, float>) return Bits(value);
    else if constexpr (std::is_same_v<R, bool>) return uint8_t(value);
    else if constexpr (std::is_pointer_v<R>) return uint32_t(reinterpret_cast<uintptr_t>(value));
    else return uint32_t(value);
}

Blob RunRoute(difftest::Context &ctx, int route, uint32_t seed, uint32_t rva, Case call, uint32_t caseSeed)
{
    Entry *e = Make(ctx, route, seed);
    if (route == 2) {
        std::memcpy(gTrapVT, GameAddress<void>(Entry::kOriginalVTable), sizeof(gTrapVT));
        const unsigned slot = shape::SlotOf(Entry::kOriginalVTable, Entry::kVTableSlots, rva);
        if (slot != ~0u) gTrapVT[slot] = shape::TrapFor<&TrapHit>(8); // (popping more words than any slot takes: a hit fails anyway)
        *reinterpret_cast<void **>(e) = gTrapVT;
    }
    gLog.clear();
    gTrapHits = 0;
    gCursor = gCursor16 = gCursor32 = 0;
    std::mt19937 r(caseSeed);
    Ctx c{ctx, route, e, r};
    uint32_t result;
    try {
        result = call(c);
    } catch (const std::exception &x) {
        std::printf("GNDBG route %d seed %u: %s\n", route, seed, x.what());
        throw;
    }
    Blob blob{result, gTrapHits, uint32_t(gCursor)};
    blob.insert(blob.end(), gLog.begin(), gLog.end());
    Capture(blob, route, e);
    if (route == 2) *reinterpret_cast<const void **>(e) = GameAddress<void>(Entry::kOriginalVTable);
    Destroy(e);
    return blob;
}

// Every seed: route 0 against routes 1 and 2 (`adapterRoute` false: route 1 only).
void Sweep(difftest::Context &ctx, const char *name, uint32_t rva, Case call, uint32_t seeds, bool adapterRoute = true)
{
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        const uint32_t caseSeed = seed * 2654435761u + rva;
        const Blob original = RunRoute(ctx, 0, seed, rva, call, caseSeed);
        for (int route = 1; route <= (adapterRoute ? 2 : 1); ++route) {
            const Blob ours = RunRoute(ctx, route, seed, rva, call, caseSeed);
            if (original != ours) {
                size_t at = 0;
                while (at < original.size() && at < ours.size() && original[at] == ours[at]) ++at;
                ctx.Fail("%s seed %u route %d: differs at word %zu (%zu / %zu words): %08x / %08x", name, seed, route, at,
                    original.size(), ours.size(), at < original.size() ? original[at] : 0, at < ours.size() ? ours[at] : 0);
                if (std::getenv("GN_DUMP")) { // the words around the first difference
                    for (const Blob *b : {&original, &ours}) {
                        for (size_t i = at > 8 ? at - 8 : 0; i < at + 24 && i < b->size(); ++i) std::printf(" %x", (*b)[i]);
                        std::printf("\n");
                    }
                }
                return;
            }
            ++ctx.cases;
        }
    }
}

uint32_t AnyStage(std::mt19937 &r)
{
    const uint32_t stages[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 0xffffffffu};
    return stages[r() % 17];
}
// A stage for GetMotiveAd with `create` set: never negative. Excluded because the original's result is not defined there:
// 0x43315e (cmp esi, 5; jge) lets a negative stage through, 0x433187 resizes the stages to stage + 1 = 0 entries and
// 0x433192 reads the vector at index -1, before the storage (heap bookkeeping); ours reproduces the same reads.
uint32_t CreatingStage(std::mt19937 &r)
{
    const uint32_t stage = AnyStage(r);
    return stage == 0xffffffffu ? 15 : stage;
}
uint32_t AnyMotive(std::mt19937 &r) { return r() % 8 == 0 ? 16 + r() % 4 : r() % 16; }

// While alive: the game's general allocator (0x10cb098) is built and the allocator service cache (0x10c95d4, guard
// 0x10c95d8) holds an object with the game's allocator-service vtable (0xda3cd4): the use records (+0x70) are allocated
// through the service, by the original and by ours.
struct ServiceEnv {
    GeneralAllocator *allocator = GameAddress<GeneralAllocator>(0x10cb098);
    bool built = false;
    void **cache = GameAddress<void *>(0x10c95d4);
    int *guard = GameAddress<int>(0x10c95d8);
    void *savedCache;
    int savedGuard;
    alignas(8) uint8_t object[16] = {};
    ServiceEnv()
    {
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(allocator);
        bool zero = true;
        for (size_t i = 0; i < sizeof(GeneralAllocator); ++i) zero = zero && bytes[i] == 0;
        if (zero) {
            allocator->Construct(nullptr, 0, 0, 0, nullptr, nullptr);
            built = true;
        }
        savedCache = *cache;
        savedGuard = *guard;
        const void *vtable = GameAddress<void>(0xda3cd4);
        std::memcpy(object, &vtable, sizeof(vtable));
        *cache = object;
        *guard = INT32_MIN;
        SetupFakes();
    }
    ServiceEnv(const ServiceEnv &) = delete;
    ~ServiceEnv()
    {
        *cache = savedCache;
        *guard = savedGuard;
        if (built) difftest_alloc::ShutdownAndRelease(allocator);
    }
};

} // namespace gn
} // namespace

using namespace gn;

#define GN_CALL(R, Name, rva, ...) Wide<R>(Invoke<R>(c, rva, &A::Name, __VA_ARGS__))
#define GN_CALL0(R, Name, rva) Wide<R>(Invoke<R>(c, rva, &A::Name))

// The flag getters and setters, the masked flag functions, ResetToDefaults and IsAutonomyDisabled, on every route.
DIFF_TEST(TreeTableEntry_Flags, 0x435220)
{
    ServiceEnv env;
    struct FlagPair { const char *name; uint32_t get, set; bool (__fastcall *getter)(Entry *, void *); void (__fastcall *setter)(Entry *, void *, bool); };
    static const FlagPair pairs[] = {
        {"CanJoin", 0x432e20, 0x435190, &A::GetCanJoin, &A::SetCanJoin},
        {"RunImmediately", 0x433090, 0x435270, &A::GetRunImmediately, &A::SetRunImmediately},
        {"AllowSubQueue", 0x432b90, 0x435130, &A::GetAllowSubQueue, &A::SetAllowSubQueue},
        {"AllowOnSelf", 0x432b80, 0x435110, &A::GetAllowOnSelf, &A::SetAllowOnSelf},
        {"OverrideDisable", 0x433130, 0x435390, &A::GetOverrideDisable, &A::SetOverrideDisable},
        {"PreferredByFurious", 0x433460, 0x4354a0, &A::GetPreferredByFurious, &A::SetPreferredByFurious},
        {"TypeSubQueue", 0x433650, 0x435600, &A::GetTypeSubQueue, &A::SetTypeSubQueue},
        {"ResetMotiveDecay", 0x433470, 0x4354c0, &A::GetResetMotiveDecay, &A::SetResetMotiveDecay},
        {"ParentalControl", 0x433640, 0x4355e0, &A::GetParentalControl, &A::SetParentalControl},
        {"Flag5", 0x432e10, 0x435170, &A::GetFlag5, &A::SetFlag5},
        {"DebugOnly", 0x432e50, 0x4351e0, &A::GetDebugOnly, &A::SetDebugOnly},
        {"AutoFirstSelect", 0x432df0, 0x435150, &A::GetAutoFirstSelect, &A::SetAutoFirstSelect},
        {"Flag22", 0x433670, 0x435630, &A::GetFlag22, &A::SetFlag22},
        {"AffectsOwnership", 0x432b70, 0x4350f0, &A::GetAffectsOwnership, &A::SetAffectsOwnership},
    };
    static const FlagPair *current;
    // The routes' entries agree before any call (both constructors, the seeding, the capture).
    Sweep(ctx, "Seed", 0x432e20, [](Ctx &) -> uint32_t { return 0; }, 12);
    for (const FlagPair &pair : pairs) {
        current = &pair;
        Sweep(ctx, pair.name, pair.get, [](Ctx &c) -> uint32_t { return Wide<bool>(Invoke<bool>(c, current->get, current->getter)); }, 12);
        Sweep(ctx, pair.name, pair.set, [](Ctx &c) -> uint32_t {
            Invoke<void>(c, current->set, current->setter, bool(c.r() % 2));
            return 0;
        }, 12);
    }
    // A setter's bool is a byte: the high bytes of the argument word are not part of it.
    Sweep(ctx, "SetCanJoin(raw)", 0x435190, [](Ctx &c) -> uint32_t {
        const uint32_t word = c.r() % 2 ? 0x100 : 0x101;
        if (c.route == 0) c.ctx.Original<void(__thiscall *)(void *, uint32_t)>(0x435190)(c.e, word);
        else if (c.route == 1) CallSlot<void>(c.e, Slot(0x435190), word);
        else reinterpret_cast<void(__fastcall *)(Entry *, void *, uint32_t)>(&A::SetCanJoin)(c.e, nullptr, word);
        return 0;
    }, 8);
    Sweep(ctx, "HasFlags", 0x433700, [](Ctx &c) -> uint32_t {
        const uint32_t masks[] = {1, 0x800, 0x400, 0x10, 0x40, 0x10000, c.e->mFlags, c.e->mFlags | 1, c.r(), 0};
        return GN_CALL(bool, HasFlags, 0x433700, masks[c.r() % 10]);
    }, 30);
    Sweep(ctx, "SetMaskedFlags", 0x435200, [](Ctx &c) -> uint32_t {
        const uint32_t values = c.r(), mask = c.r() & c.r();
        Invoke<void>(c, 0x435200, &A::SetMaskedFlags, values, mask);
        return 0;
    }, 20);
    Sweep(ctx, "SetFlags", 0x435220, [](Ctx &c) -> uint32_t {
        const uint32_t bits = c.r() & c.r();
        Invoke<void>(c, 0x435220, &A::SetFlags, bits, bool(c.r() % 2));
        return 0;
    }, 20);
    Sweep(ctx, "GetAgeFlags", 0x432e00, [](Ctx &c) -> uint32_t { return GN_CALL0(uint32_t, GetAgeFlags, 0x432e00); }, 12);
    Sweep(ctx, "IsAutonomyDisabled", 0x4330b0, [](Ctx &c) -> uint32_t { return GN_CALL0(bool, IsAutonomyDisabled, 0x4330b0); }, 30);
    Sweep(ctx, "ResetToDefaults", 0x4336c0, [](Ctx &c) -> uint32_t {
        return GN_CALL(bool, ResetToDefaults, 0x4336c0, uint32_t(c.r()));
    }, 12);
}

// A pointer the functions return, as something both routes agree on: a field of the entry (0xE0000000 | offset), a field
// of a motive advertisement (0xAD000000 | stage << 16 | index << 4 | byte offset), null, or the raw value.
static uint32_t Rel(const Entry *e, const void *p)
{
    const uint8_t *b = static_cast<const uint8_t *>(p), *base = reinterpret_cast<const uint8_t *>(e);
    if (p == nullptr) return 0;
    if (b >= base && b < base + Entry::kSize) return 0xE0000000 | uint32_t(b - base);
    for (size_t s = 0; s < e->mMotiveAds.size(); ++s) {
        const auto &ads = e->mMotiveAds[s];
        if (ads.empty()) continue;
        const uint8_t *first = reinterpret_cast<const uint8_t *>(ads.data());
        if (b >= first && b < first + ads.size() * 12) {
            const uint32_t at = uint32_t(b - first);
            return 0xAD000000 | uint32_t(s) << 16 | (at / 12) << 4 | at % 12;
        }
    }
    return P(p) | 1; // (never equal across routes: a pointer outside both)
}

template <class V> static void CaptureAds(Blob &out, const V &ads)
{
    out.push_back(uint32_t(ads.size()));
    out.push_back(uint32_t(ads.capacity()));
    for (const auto &ad : ads) out.insert(out.end(), {ElementCode(&ad), uint16_t(ad.mFirst), uint16_t(ad.mSecond), uint16_t(ad.mThird)});
}

static Blob gExtra; // more of a case's results, appended to the blob

// The motive and trait advertisement functions (52..63, 68..72, 93) and the static helpers.
DIFF_TEST(TreeTableEntry_Ads, 0x433140)
{
    ServiceEnv env;
    Sweep(ctx, "GetMotiveAdCount", 0x4333d0, [](Ctx &c) -> uint32_t {
        return GN_CALL(uint32_t, GetMotiveAdCount, 0x4333d0, int32_t(AnyStage(c.r)));
    }, 40);
    Sweep(ctx, "GetMotiveAdDelta", 0x4333b0, [](Ctx &c) -> uint32_t {
        return Rel(c.e, Invoke<int16_t *>(c, 0x4333b0, &A::GetMotiveAdDelta, AnyMotive(c.r), CreatingStage(c.r)));
    }, 40);
    Sweep(ctx, "GetMotiveAdMin", 0x433390, [](Ctx &c) -> uint32_t {
        return Rel(c.e, Invoke<int16_t *>(c, 0x433390, &A::GetMotiveAdMin, AnyMotive(c.r), CreatingStage(c.r)));
    }, 40);
    Sweep(ctx, "GetMotiveAdType", 0x433440, [](Ctx &c) -> uint32_t {
        return Rel(c.e, Invoke<int16_t *>(c, 0x433440, &A::GetMotiveAdType, AnyMotive(c.r), CreatingStage(c.r)));
    }, 40);
    Sweep(ctx, "HasAdvertisement", 0x431300, [](Ctx &c) -> uint32_t {
        return GN_CALL(bool, HasAdvertisement, 0x431300, AnyMotive(c.r), AnyStage(c.r));
    }, 60);
    Sweep(ctx, "GetMotiveAd", 0x433140, [](Ctx &c) -> uint32_t {
        const uint32_t motive = AnyMotive(c.r), anyStage = AnyStage(c.r);
        const bool baby = c.r() % 2, create = c.r() % 2;
        const uint32_t stage = create && anyStage == 0xffffffffu ? 15 : anyStage; // (see CreatingStage)
        return Rel(c.e, Invoke<cTSTreeTableAd *>(c, 0x433140, &A::GetMotiveAd, motive, stage, baby, create));
    }, 60);
    Sweep(ctx, "FindMotiveAd", 0x4331e0, [](Ctx &c) -> uint32_t {
        const int32_t motive = int32_t(AnyMotive(c.r)) - (c.r() % 9 == 0 ? 30 : 0), stage = int32_t(AnyStage(c.r));
        return Rel(c.e, Invoke<cTSTreeTableAd *>(c, 0x4331e0, &A::FindMotiveAd, motive, stage, bool(c.r() % 2)));
    }, 60);
    Sweep(ctx, "SetMotiveAd", 0x4353b0, [](Ctx &c) -> uint32_t {
        cTSTreeTableAd ad;
        ad.mFirst = int16_t(c.r() % 12);
        ad.mSecond = c.r() % 3 ? 0 : SmallValue(c.r);
        ad.mThird = c.r() % 3 ? 0 : SmallValue(c.r);
        const int32_t motive = int32_t(AnyMotive(c.r)), stage = int32_t(AnyStage(c.r));
        Invoke<void>(c, 0x4353b0, &A::SetMotiveAd, motive, stage, static_cast<const cTSTreeTableAd *>(&ad), bool(c.r() % 2));
        return uint16_t(ad.mFirst) | uint32_t(uint16_t(ad.mSecond)) << 16;
    }, 80);
    Sweep(ctx, "GetMotiveAds", 0x433260, [](Ctx &c) -> uint32_t {
        alignas(4) uint8_t raw[sizeof(cTSTreeTableAdVector)];
        std::memset(raw, 0xcd, sizeof(raw));
        auto *out = reinterpret_cast<cTSTreeTableAdVector *>(raw);
        auto *result = Invoke<cTSTreeTableAdVector *>(c, 0x433260, &A::GetMotiveAds, out, int32_t(AnyStage(c.r)));
        gExtra.clear();
        CaptureAds(gExtra, *out);
        gLog.insert(gLog.end(), gExtra.begin(), gExtra.end());
        out->~vector();
        return result == out;
    }, 40);
    Sweep(ctx, "GetTraitAds", 0x433480, [](Ctx &c) -> uint32_t {
        alignas(4) uint8_t raw[sizeof(cTSTraitAdList)];
        std::memset(raw, 0xcd, sizeof(raw));
        auto *out = reinterpret_cast<cTSTraitAdList *>(raw);
        auto *result = Invoke<cTSTraitAdList *>(c, 0x433480, &A::GetTraitAds, out, AnyStage(c.r));
        gLog.push_back(uint32_t(out->size()));
        gLog.push_back(uint32_t(out->capacity()));
        for (const auto &values : *out) CaptureAds(gLog, values);
        out->~vector();
        return result == out;
    }, 40);
    Sweep(ctx, "GetTraitAdValue", 0x4335b0, [](Ctx &c) -> uint32_t {
        const int32_t trait = int32_t(c.r() % 20) - 2;
        const uint32_t stage = AnyStage(c.r), element = c.r() % 9;
        const int32_t which = int32_t(c.r() % 5) - 1;
        return uint16_t(Invoke<int16_t>(c, 0x4335b0, &A::GetTraitAdValue, trait, stage, element, which));
    }, 80);
    Sweep(ctx, "SetTraitAdValue", 0x4354e0, [](Ctx &c) -> uint32_t {
        const int32_t trait = int32_t(c.r() % 20);
        const uint32_t stage = c.r() % 5 == 0 ? AnyStage(c.r) : 5 + c.r() % 8, element = c.r() % 9;
        const int32_t which = int32_t(c.r() % 4);
        const int16_t value = c.r() % 3 ? 0 : SmallValue(c.r);
        Invoke<void>(c, 0x4354e0, &A::SetTraitAdValue, trait, stage, element, which, value, bool(c.r() % 2));
        return 0;
    }, 80);
    Sweep(ctx, "CopyStageToAll", 0x431940, [](Ctx &c) -> uint32_t {
        Invoke<void>(c, 0x431940, &A::CopyStageToAll, AnyStage(c.r), bool(c.r() % 2));
        return 0;
    }, 60);
    Sweep(ctx, "ClearAds", 0x4318c0, [](Ctx &c) -> uint32_t { Invoke<void>(c, 0x4318c0, &A::ClearAds); return 0; }, 12);
    Sweep(ctx, "ClearStage", 0x431800, [](Ctx &c) -> uint32_t {
        Invoke<void>(c, 0x431800, &A::ClearStage, c.r() % 2 ? 5 + c.r() % 9 : AnyStage(c.r), bool(c.r() % 2));
        return 0;
    }, 60);
    Sweep(ctx, "ClearMotiveAds", 0x4318e0, [](Ctx &c) -> uint32_t { Invoke<void>(c, 0x4318e0, &A::ClearMotiveAds); return 0; }, 12);
    Sweep(ctx, "ClearTraitAds", 0x431910, [](Ctx &c) -> uint32_t { Invoke<void>(c, 0x431910, &A::ClearTraitAds); return 0; }, 12);
    Sweep(ctx, "Trim", 0x4339d0, [](Ctx &c) -> uint32_t { Invoke<void>(c, 0x4339d0, &A::Trim); return 0; }, 40);
    // The static helpers (cdecl), on route 0 and 1.
    Sweep(ctx, "TrimMotiveAds", 0x42f210, [](Ctx &c) -> uint32_t {
        if (c.route == 0) c.ctx.Original<void(__cdecl *)(cTSTreeTableAdStages *)>(0x42f210)(&c.e->mMotiveAds);
        else Entry::TrimMotiveAds(&c.e->mMotiveAds);
        return 0;
    }, 40, false);
    Sweep(ctx, "TrimTraitAds", 0x42ef80, [](Ctx &c) -> uint32_t {
        if (c.route == 0) c.ctx.Original<void(__cdecl *)(cTSTraitAdStages *)>(0x42ef80)(&c.e->mTraitAds);
        else Entry::TrimTraitAds(&c.e->mTraitAds);
        return 0;
    }, 40, false);
    Sweep(ctx, "IsAllZero", 0x42ef30, [](Ctx &c) -> uint32_t {
        uint32_t bits = 0, n = 0;
        for (const auto &list : c.e->mTraitAds) {
            for (const auto &values : list) {
                const bool zero = c.route == 0 ? c.ctx.Original<bool(__cdecl *)(const cTSTraitAdVector *)>(0x42ef30)(&values)
                                               : Entry::IsAllZero(&values);
                bits = bits * 3 + zero;
                ++n;
            }
        }
        return bits ^ n << 24;
    }, 40, false);
}

// The person-dependent functions (64, 65, 67 and the two scaling members) and the score (66).
DIFF_TEST(TreeTableEntry_Person, 0x433720)
{
    ServiceEnv env;
    static auto setPerson = [](std::mt19937 &r) {
        gUsesMotiveAds = r() % 2 ? 1 : 0x100 * (r() % 3); // (only the low byte counts)
        const uint32_t stages[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 0xffffffffu};
        gStageA = stages[r() % 11];
        gStageB = stages[r() % 11];
        gPersonData = uint32_t(int32_t(SmallValue(r)) * 3);
    };
    Sweep(ctx, "ScaleAds", 0x433720, [](Ctx &c) -> uint32_t {
        setPerson(c.r);
        const float factor = float(int32_t(c.r() % 400) - 100) / 64.0f;
        Invoke<void>(c, 0x433720, &A::ScaleAds, static_cast<void *>(gPerson), factor, AnyMotive(c.r));
        return 0;
    }, 80);
    Sweep(ctx, "GetAdValue", 0x4328f0, [](Ctx &c) -> uint32_t {
        setPerson(c.r);
        return Bits(Invoke<float>(c, 0x4328f0, &A::GetAdValue, static_cast<void *>(gPerson), AnyMotive(c.r)));
    }, 160);
    Sweep(ctx, "GetTraitAdFor", 0x432ba0, [](Ctx &c) -> uint32_t {
        const uint32_t motive = AnyMotive(c.r), stage = c.r() % 4 ? 5 + c.r() % 8 : AnyStage(c.r), mask = c.r();
        return Bits(Invoke<float>(c, 0x432ba0, &A::GetTraitAdFor, motive, stage, mask));
    }, 160);
    Sweep(ctx, "ScaleMotiveMin", 0x433850, [](Ctx &c) -> uint32_t {
        setPerson(c.r);
        const float factor = float(int32_t(c.r() % 400) - 100) / 64.0f;
        const uint32_t motive = AnyMotive(c.r);
        if (c.route == 0) c.ctx.Original<void(__thiscall *)(void *, void *, float, uint32_t)>(0x433850)(c.e, gPerson, factor, motive);
        else c.e->ScaleMotiveMin(gPerson, factor, motive);
        return 0;
    }, 80, false);
    Sweep(ctx, "ScaleTraitAds", 0x4338a0, [](Ctx &c) -> uint32_t {
        setPerson(c.r);
        const float factor = float(int32_t(c.r() % 400) - 100) / 64.0f;
        const int32_t trait = int32_t(c.r() % 20);
        if (c.route == 0) c.ctx.Original<void(__thiscall *)(void *, void *, float, int32_t)>(0x4338a0)(c.e, gPerson, factor, trait);
        else c.e->ScaleTraitAds(gPerson, factor, trait);
        return 0;
    }, 80, false);
    Sweep(ctx, "GetMotiveScore", 0x432f00, [](Ctx &c) -> uint32_t {
        std::map<int32_t, int32_t> personality;
        const uint32_t keys = c.r() % 12;
        for (uint32_t i = 0; i < keys; ++i) personality[c.r() % 3 ? GameAddress<const int32_t>(Entry::kMotiveTable)[3 * (c.r() % 12)] : int32_t(c.r() % 16)] = int32_t(c.r() % 2001) - 1000;
        uint32_t motive = AnyMotive(c.r), stage = AnyStage(c.r);
        // Mostly an ad that exists, with a type (the personality path): the seeds alone rarely reach it.
        if (c.r() % 4 != 0 && !c.e->mMotiveAds.empty()) {
            stage = c.r() % c.e->mMotiveAds.size();
            auto &ads = c.e->mMotiveAds[stage];
            if (!ads.empty()) {
                motive = c.r() % ads.size();
                ads[motive].mFirst = int16_t(c.r() % 12);
                if (c.r() % 2) ads[motive].mThird = SmallValue(c.r);
            }
        }
        return Bits(Invoke<float>(c, 0x432f00, &A::GetMotiveScore, motive, stage, static_cast<void *>(&personality)));
    }, 160);
}

// Use records (73..75), attenuation (84, 85) and the name (89, 91).
DIFF_TEST(TreeTableEntry_Uses, 0x4352b0)
{
    ServiceEnv env;
    Sweep(ctx, "RecordUse", 0x4352b0, [](Ctx &c) -> uint32_t {
        const uint32_t n = 1 + c.r() % 3;
        for (uint32_t i = 0; i < n; ++i)
            Invoke<void>(c, 0x4352b0, &A::RecordUse, int32_t(c.r() % 5), uint16_t(c.r() % 4 == 0 ? c.r() : c.r() % 3), uint32_t(c.r() % 12));
        return 0;
    }, 40);
    Sweep(ctx, "WasRecentlyUsed", 0x434c10, [](Ctx &c) -> uint32_t {
        const int32_t actor = c.r() % 5 == 0 ? int32_t(0x10000 + c.r() % 3) : int32_t(c.r() % 3);
        return GN_CALL(bool, WasRecentlyUsed, 0x434c10, int32_t(c.r() % 5), actor, uint32_t(c.r() % 8));
    }, 80);
    Sweep(ctx, "ClearUses", 0x434510, [](Ctx &c) -> uint32_t { Invoke<void>(c, 0x434510, &A::ClearUses); return 0; }, 12);
    // The tuning service from its cache (0x10cc290); the null service (the getter then acquires one) is not reachable here.
    void **tuningCache = GameAddress<void *>(kTuningCache);
    void *savedTuning = *tuningCache;
    *tuningCache = gTuning;
    Sweep(ctx, "GetAttenuationValue", 0x432cd0, [](Ctx &c) -> uint32_t {
        c.e->mAttenuation = c.r() % 4 == 0 ? c.r() : c.r() % 6;
        return Bits(Invoke<float>(c, 0x432cd0, &A::GetAttenuationValue, bool(c.r() % 2)));
    }, 80);
    *tuningCache = savedTuning;
    Sweep(ctx, "SetCustomAttenuation", 0x4351c0, [](Ctx &c) -> uint32_t {
        Invoke<void>(c, 0x4351c0, &A::SetCustomAttenuation, float(int32_t(c.r() % 1000)) / 3.0f);
        return 0;
    }, 12);
    Sweep(ctx, "GetName", 0x4330e0, [](Ctx &c) -> uint32_t {
        cRZString out;
        out.Construct();
        out.mString.assign(c.r() % 30, 'x');
        const bool result = Invoke<bool>(c, 0x4330e0, &A::GetName, reinterpret_cast<cIGZString *>(&out));
        gLog.push_back(uint32_t(out.mString.size()));
        for (char ch : out.mString) gLog.push_back(uint8_t(ch));
        out.Destruct();
        return result;
    }, 20);
    Sweep(ctx, "SetName", 0x435380, [](Ctx &c) -> uint32_t {
        cRZString name;
        name.Construct();
        name.mString.assign(c.r() % 40, char('A' + c.r() % 26));
        const uint32_t result = Rel(c.e, Invoke<cIGZString *>(c, 0x435380, &A::SetName, *reinterpret_cast<const cIGZString *>(&name)));
        name.Destruct();
        return result;
    }, 20);
}

// The slots our vtable holds for folded original functions (not hooked: shared_vf_*), and AddRef: route 0 calls the game's
// vtable slot on the original's entry, route 1 ours.
DIFF_TEST(TreeTableEntry_Slots, 0x433a50)
{
    ServiceEnv env;
    static unsigned slot;
    for (unsigned s : {1u, 5u, 8u, 10u, 37u, 44u, 46u, 48u, 80u, 87u, 90u, 96u, 97u}) {
        slot = s;
        Sweep(ctx, "Get", 0, [](Ctx &c) -> uint32_t { return Rel(c.e, reinterpret_cast<void *>(uintptr_t(CallSlot<uint32_t>(c.e, slot)))); }, 6, false);
    }
    for (unsigned s : {31u, 76u, 78u}) {
        slot = s;
        Sweep(ctx, "Get16", 0, [](Ctx &c) -> uint32_t { return CallSlot<uint16_t>(c.e, slot); }, 6, false);
    }
    for (unsigned s : {82u, 86u}) {
        slot = s;
        Sweep(ctx, "GetFloat", 0, [](Ctx &c) -> uint32_t { return Bits(CallSlot<float>(c.e, slot)); }, 6, false);
    }
    for (unsigned s : {9u, 38u, 45u, 47u, 49u, 81u, 88u}) {
        slot = s;
        Sweep(ctx, "Set", 0, [](Ctx &c) -> uint32_t { CallSlot<void>(c.e, slot, uint32_t(c.r())); return 0; }, 6, false);
    }
    for (unsigned s : {32u, 77u, 79u}) {
        slot = s;
        Sweep(ctx, "Set16", 0, [](Ctx &c) -> uint32_t { CallSlot<void>(c.e, slot, uint32_t(c.r())); return 0; }, 6, false);
    }
    Sweep(ctx, "SetMultiplier", 0, [](Ctx &c) -> uint32_t { CallSlot<void>(c.e, 83, float(c.r() % 1000) / 7.0f); return 0; }, 6, false);
    // Every slot of our vtable that holds a function hooked at an RVA is the one the game's vtable holds there (the cases
    // above and in the other tests call through SlotOf; this checks the count).
    if (shape::SlotOf(Entry::kOriginalVTable, Entry::kVTableSlots, 0x433a50) != 0) ctx.Fail("QueryInterface is not slot 0");
    Sweep(ctx, "QueryInterface", 0x433a50, [](Ctx &c) -> uint32_t {
        const uint32_t ids[] = {Entry::kSerializableId, 1, Entry::kClassId, Entry::kInterfaceId, 0, 0x1b5a2045, c.r()};
        void *out = reinterpret_cast<void *>(0x1234);
        const bool result = Invoke<bool>(c, 0x433a50, &A::QueryInterface, ids[c.r() % 7], &out);
        gLog.push_back(Rel(c.e, out));
        return result;
    }, 40);
    Sweep(ctx, "Release", 0x434400, [](Ctx &c) -> uint32_t {
        c.e->mRefCount = 2 + c.r() % 4;
        return GN_CALL0(uint32_t, Release, 0x434400);
    }, 12);
}

// The stream functions: Read (every version branch), Write and WriteBody.
DIFF_TEST(TreeTableEntry_Stream, 0x433e60)
{
    ServiceEnv env;
    Sweep(ctx, "Read", 0x433e60, [](Ctx &c) -> uint32_t {
        const uint32_t versions[] = {0x40, 0x43, 0x44, 0x45, 0x46, 0x49, 0x4a, 0x4b, 0x4c, 0x51, 0x52, 0x53, 0x54, 0x55};
        gVersion = versions[c.r() % 14];
        gStreamError = c.r() % 5 == 0;
        gScript.clear();
        gScript16.clear();
        const uint32_t n = 20 + c.r() % 120;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t kind = c.r() % 10;
            gScript.push_back(kind < 7 ? c.r() % 6 : kind < 9 ? c.r() % 0x40 : c.r() % 0x400);
            gScript16.push_back(kind < 3 ? 0 : uint16_t(SmallValue(c.r)));
        }
        return GN_CALL(bool, Read, 0x433e60, reinterpret_cast<ifaces::cIGZIStream *>(gIn));
    }, 300);
    Sweep(ctx, "Write", 0x435a80, [](Ctx &c) -> uint32_t {
        gStreamError = c.r() % 4 == 0;
        return GN_CALL(bool, Write, 0x435a80, reinterpret_cast<ifaces::cIGZOStream *>(gOut));
    }, 40);
    Sweep(ctx, "WriteBody", 0x4357c0, [](Ctx &c) -> uint32_t {
        gStreamError = c.r() % 4 == 0;
        const bool trim = c.r() % 2;
        auto *out = reinterpret_cast<ifaces::cIGZOStream *>(gOut);
        if (c.route == 0) return c.ctx.Original<bool(__thiscall *)(void *, void *, bool)>(0x4357c0)(c.e, out, trim);
        return c.e->WriteBody(out, trim);
    }, 40, false);
}

// CopyFrom (7): the fast path (the source answers the class id) and the slow path (it does not: every value through its
// slots). The source is a second entry of the route's kind; for the slow path one of ours with a vtable copy whose
// QueryInterface says no.
static void *gNoClassVT[Entry::kVTableSlots];
static bool __fastcall NoClassQI(Entry *self, void *, uint32_t riid, void **out)
{
    Note(0xC1A55, riid);
    if (riid == Entry::kClassId) return false;
    return self->Entry::QueryInterface(riid, out);
}
DIFF_TEST(TreeTableEntry_CopyFrom, 0x431b30)
{
    ServiceEnv env;
    std::memcpy(gNoClassVT, Ours().entry, sizeof(gNoClassVT));
    gNoClassVT[0] = reinterpret_cast<void *>(&NoClassQI);
    static bool slow;
    for (bool s : {false, true}) {
        slow = s;
        Sweep(ctx, s ? "CopyFrom(slow)" : "CopyFrom", 0x431b30, [](Ctx &c) -> uint32_t {
            const uint32_t sourceSeed = c.r() % 40;
            Entry *source;
            if (slow) {
                source = new Entry;
                Seed(source, sourceSeed);
                *reinterpret_cast<void **>(source) = gNoClassVT;
            } else {
                source = Make(c.ctx, c.route == 1 ? 1 : 0, sourceSeed);
            }
            source->mRefCount += 1; // (the copy releases what it acquired; this keeps the source alive either way)
            Invoke<void>(c, 0x431b30, &A::CopyFrom, static_cast<cITSTreeTableEntry *>(source));
            Blob seen;
            Capture(seen, slow ? 1 : c.route == 1 ? 1 : 0, source);
            seen[0] = seen[0] == kEntryCode || *reinterpret_cast<void **>(source) == gNoClassVT ? kEntryCode : seen[0];
            gLog.insert(gLog.end(), seen.begin(), seen.end());
            if (slow) *reinterpret_cast<const void **>(source) = Ours().entry;
            Destroy(source);
            return 0;
        }, 60);
    }
}

// The constructors, destructors, the deleting destructor and Release's last reference, on the three routes: the original
// at its RVA, ours (new, ~, delete, our vtable), the adapter. The frees are compared by their sizes.
DIFF_TEST(TreeTableEntry_Lifetime, 0x430f60)
{
    ServiceEnv env;
    for (uint32_t seed = 0; seed < 30; ++seed) {
        Blob results[3];
        for (int route = 0; route < 3; ++route) {
            Blob &out = results[route];
            const int kind = route == 0 ? 0 : 1; // the expected entry vtable of a route (the adapter builds ours)
            const void *expected = route == 0 ? GameAddress<void>(Entry::kOriginalVTable) : Ours().entry;
            // Construct.
            void *memory = ::operator new(Entry::kSize);
            std::memset(memory, 0xcd, Entry::kSize);
            Entry *e = route == 0 ? static_cast<Entry *>(ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kCtor)(memory, seed))
                     : route == 1 ? new (memory) Entry
                                  : A::Construct(static_cast<Entry *>(memory), nullptr, seed);
            out.push_back(e == memory);
            out.push_back(*reinterpret_cast<void **>(e) == expected);
            Capture(out, kind, e);
            Seed(e, seed);
            // Copy construct.
            void *copyMemory = ::operator new(Entry::kSize);
            std::memset(copyMemory, 0xcd, Entry::kSize);
            Entry *copy = route == 0 ? static_cast<Entry *>(ctx.Original<void *(__thiscall *)(void *, const void *)>(kCopyCtor)(copyMemory, e))
                        : route == 1 ? new (copyMemory) Entry(*e)
                                     : A::CopyConstruct(static_cast<Entry *>(copyMemory), nullptr, e);
            out.push_back(copy == copyMemory);
            out.push_back(*reinterpret_cast<void **>(copy) == expected);
            Capture(out, kind, copy);
            // Destruct the copy (no free of the object).
            {
                difftest::FreeRecorder frees;
                if (route == 0) ctx.Original<void(__thiscall *)(void *)>(kDtor)(copy);
                else if (route == 1) copy->~Entry();
                else A::Destruct(copy, nullptr);
                frees.Stop();
                size_t sizes[64];
                const size_t n = frees.SortedSizes(sizes, 64);
                out.push_back(uint32_t(n));
                out.insert(out.end(), sizes, sizes + n);
                out.push_back(frees.Freed(copyMemory));
            }
            ::operator delete(copyMemory);
            // The deleting destructor without the delete flag, then Release of the last reference (with it).
            void *again = ::operator new(Entry::kSize);
            Entry *e2 = route == 0 ? static_cast<Entry *>(ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kCtor)(again, 0))
                                   : new (again) Entry;
            Seed(e2, seed + 100);
            {
                difftest::FreeRecorder frees;
                void *result = route == 0 ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeleting)(e2, 0)
                             : route == 1 ? CallSlot<void *>(e2, 98, uint32_t(0))
                                          : A::DeletingDestructor(e2, nullptr, 0);
                frees.Stop();
                out.push_back(result == e2);
                out.push_back(uint32_t(frees.Count()));
                out.push_back(frees.Freed(again));
            }
            ::operator delete(again);
            e->mRefCount = 1;
            {
                difftest::FreeRecorder frees;
                const uint32_t result = route == 0 ? ctx.Original<uint32_t(__thiscall *)(void *)>(0x434400)(e)
                                      : route == 1 ? CallSlot<uint32_t>(e, 2)
                                                   : A::Release(e, nullptr);
                frees.Stop();
                out.push_back(result);
                out.push_back(frees.Freed(memory));
                size_t sizes[64];
                const size_t n = frees.SortedSizes(sizes, 64);
                out.push_back(uint32_t(n));
                out.insert(out.end(), sizes, sizes + n);
            }
            // The deleting destructor with the flag, on a fresh entry.
            void *third = ::operator new(Entry::kSize);
            Entry *e3 = route == 0 ? static_cast<Entry *>(ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kCtor)(third, 0))
                                   : new (third) Entry;
            Seed(e3, seed + 200);
            {
                difftest::FreeRecorder frees;
                void *result = route == 0 ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeleting)(e3, 1)
                             : route == 1 ? CallSlot<void *>(e3, 98, uint32_t(1))
                                          : A::DeletingDestructor(e3, nullptr, 1);
                frees.Stop();
                out.push_back(result == e3);
                out.push_back(frees.Freed(third));
                size_t sizes[64];
                const size_t n = frees.SortedSizes(sizes, 64);
                out.push_back(uint32_t(n));
                out.insert(out.end(), sizes, sizes + n);
            }
        }
        for (int route = 1; route < 3; ++route) {
            if (results[route] != results[0]) {
                size_t at = 0;
                while (at < results[0].size() && at < results[route].size() && results[0][at] == results[route][at]) ++at;
                ctx.Fail("lifetime seed %u route %d: differs at word %zu: %08x / %08x", seed, route, at,
                    at < results[0].size() ? results[0][at] : 0, at < results[route].size() ? results[route][at] : 0);
                return;
            }
            ++ctx.cases;
        }
    }
}

// DumpText (92) is the original's code (forwarded, 0x432060 not hooked): our slot reaches it with the entry, the stream
// and the second dword the original pops (ret 8) without reading.
static void *gDumpThis, *gDumpStream;
static uint32_t gDumpHits, gDumpExtra;
static void __fastcall DumpStand(void *self, void *, void *stream, uint32_t extra)
{
    ++gDumpHits;
    gDumpThis = self;
    gDumpStream = stream;
    gDumpExtra = extra;
}
DIFF_TEST(TreeTableEntry_DumpText, 0x432060)
{
    ServiceEnv env;
    shape::CodePatch patch(0x432060, reinterpret_cast<const void *>(&DumpStand));
    Entry *e = new Entry;
    void *stream = reinterpret_cast<void *>(0x5714);
    CallSlot<void>(e, shape::SlotOf(Entry::kOriginalVTable, Entry::kVTableSlots, 0x432060), stream, uint32_t(0x6d2e));
    if (gDumpHits != 1 || gDumpThis != e || gDumpStream != stream || gDumpExtra != 0x6d2e)
        ctx.Fail("DumpText: %u calls, this %p, stream %p, extra %#x", gDumpHits, gDumpThis, gDumpStream, gDumpExtra);
    ++ctx.cases;
    delete e;
}
