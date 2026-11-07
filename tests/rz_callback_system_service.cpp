// Differential tests for cRZCallbackSystemService in the standalone shape (lane KD; the constructor's test is
// cRZCallbackSystemService_Construct in tests/services_b_thread.cpp, next to the thread classes it builds).
//
// The original side is an object from the original constructor 0xc68264 (the game's vtables), ours one from our
// constructor (the hook's adapter: our vtables). Each case copies the side's prototype to fresh memory and seeds the same
// field values (count, id, priorities, running byte, a fake callback thread of the side's own with recording Release /
// delete slots and a chosen ownership byte), then calls the slot through the side's own vtable: the game's vtable on the
// original side, ours on ours. Results, the fake's calls, the frees and every word of the object are compared, the vtable
// words and the fakes' addresses mapped to codes (tests/service_shape.h). (The two prototypes' real threads are leaked.)
//
// RVAs: QueryInterface 0xc68335 and its thunks 0xc68375 (+4) / 0xc6837d (+8); the destructor 0xc682c7, the deleting
// destructor 0xc68316 and its +8 thunk 0xc6830e; the folded slots our vtables now hold: AddRef 0x145240 (+4 / +8 thunks
// 0x145248 / 0x145250), Release 0x1a85b0 (0x1a85b8 / 0x1a85c0), GetCallbackThread 0x15e550, and cRZSystemService's.
#include "difftest.h"
#include "free_scope.h"
#include "service_shape.h"
#include "rz/cRZCallbackSystemService.h"

#include <cstring>
#include <iterator>
#include <random>
#include <vector>

namespace {

using svc::P;

constexpr uint32_t kCtor = 0xc68264, kDtor = 0xc682c7, kDeleting = 0xc68316, kDeletingThunk = 0xc6830e;
constexpr uint32_t kFaceOffset[3] = {0, 4, 8};
constexpr uint32_t kGameVt[3] = {cRZCallbackSystemService::kVTable, cRZCallbackSystemService::kSystemServiceVTable,
                                 cRZCallbackSystemService::kUnknownVTable};
constexpr unsigned kSlotCount[3] = {4, 13, 6};
constexpr uint32_t kSize = cRZCallbackSystemService::kSize;

// ---- the fake callback thread ----

std::vector<uint32_t> gLog;
const void *gThread = nullptr; // the side's own fake: logged as 1, anything else raw

uint32_t Who(const void *self) { return self == gThread ? 1u : P(self); }

uint32_t __fastcall FakeRelease(void *self, void *)
{
    gLog.push_back(2);
    gLog.push_back(Who(self));
    return 7;
}

void *__fastcall FakeDelete(void *self, void *, uint32_t flags)
{
    gLog.push_back(8);
    gLog.push_back(Who(self));
    gLog.push_back(flags);
    return self;
}

void *gThreadVTable[10] = {};

// A fake thread: the vtable word, the ownership byte at +9.
struct alignas(4) FakeThread {
    uint8_t bytes[16];
    explicit FakeThread(uint8_t owned)
    {
        std::memset(bytes, 0, sizeof(bytes));
        gThreadVTable[2] = reinterpret_cast<void *>(&FakeRelease);
        gThreadVTable[8] = reinterpret_cast<void *>(&FakeDelete);
        void **vt = gThreadVTable;
        std::memcpy(bytes, &vt, 4);
        bytes[9] = owned;
    }
};

// ---- the two sides ----

struct Side {
    bool ours;
    uint8_t *prototype;
    svc::CodeMap map;
};

Side MakeSide(difftest::Context &ctx, bool ours)
{
    Side side{ours, static_cast<uint8_t *>(::operator new(kSize)), {}};
    std::memset(side.prototype, 0xCD, kSize);
    if (ours) {
        cRZCallbackSystemServiceAdapters::Construct(side.prototype, nullptr);
    } else {
        ctx.Original<void *(__thiscall *)(void *)>(kCtor)(side.prototype);
    }
    for (uint32_t f = 0; f < 3; ++f) {
        side.map.AddRva(kGameVt[f], f);
        side.map.Add(svc::VTableAt(side.prototype, kFaceOffset[f]), f);
    }
    return side;
}

struct Seeds {
    uint32_t count, id, priority, tickPriority, runningWord;
    uint8_t owned;
};

Seeds RandomSeeds(std::mt19937 &rng, int run)
{
    static const uint32_t kCounts[] = {0, 1, 2, 3, 0x7fffffff, 0xffffffff};
    Seeds s{};
    s.count = run % 8 < 6 ? kCounts[run % 6] : rng();
    s.id = rng();
    s.priority = rng();
    s.tickPriority = rng();
    s.runningWord = rng();
    if (run % 4 == 0) s.runningWord &= 0xffffff00; // running byte 0
    if (run % 4 == 1) s.runningWord = (s.runningWord & 0xffffff00) | 0x80;
    s.owned = run % 3 == 0 ? 0 : static_cast<uint8_t>(1 + rng() % 255);
    return s;
}

// A fresh object of the side: the prototype's bytes (its vtables), then the seeded fields.
uint8_t *Instance(const Side &side, const Seeds &s, FakeThread &thread)
{
    auto *object = static_cast<uint8_t *>(::operator new(kSize));
    std::memcpy(object, side.prototype, kSize);
    auto *w = reinterpret_cast<uint32_t *>(object);
    w[3] = s.count;
    w[4] = s.id;
    w[5] = s.priority;
    w[6] = s.tickPriority;
    w[7] = s.runningWord;
    w[8] = P(&thread);
    return object;
}

svc::CodeMap MapFor(const Side &side, const FakeThread &thread)
{
    svc::CodeMap map = side.map;
    map.Add(&thread, 0x100);
    return map;
}

struct Pair {
    Side a, b;
    explicit Pair(difftest::Context &ctx) : a(MakeSide(ctx, false)), b(MakeSide(ctx, true))
    {
        svc::AddServiceBaseCodes(a.map, b.map, 0x10);
    }
};

struct Slot {
    unsigned face, slot, words;
    svc::Ret ret;
    const char *name;
};

const Slot kSlotTable[] = {
    {0, 0, 2, svc::kByte, "QueryInterface"},       {0, 1, 0, svc::kWord, "AddRef"},
    {0, 2, 0, svc::kWord, "Release"},              {0, 3, 0, svc::kPtr, "GetCallbackThread"},
    {1, 0, 2, svc::kByte, "+4 QueryInterface"},    {1, 1, 0, svc::kWord, "+4 AddRef"},
    {1, 2, 0, svc::kWord, "+4 Release"},           {1, 3, 0, svc::kWord, "GetServiceID"},
    {1, 4, 1, svc::kVoid, "SetServiceID"},         {1, 5, 0, svc::kWord, "GetServicePriority"},
    {1, 6, 0, svc::kByte, "IsServiceRunning"},     {1, 7, 1, svc::kVoid, "SetServiceRunning"},
    {1, 8, 0, svc::kByte, "Init"},                 {1, 9, 0, svc::kByte, "Shutdown"},
    {1, 10, 1, svc::kByte, "OnTick"},              {1, 11, 1, svc::kByte, "OnIdle"},
    {1, 12, 0, svc::kWord, "GetServiceTickPriority"},
    {2, 0, 2, svc::kByte, "+8 QueryInterface"},    {2, 1, 0, svc::kWord, "+8 AddRef"},
    {2, 2, 0, svc::kWord, "+8 Release"},           {2, 4, 0, svc::kWord, "RemoveRef"},
    {2, 5, 0, svc::kWord, "RefCount"},
};

constexpr uint32_t kIds[] = {1, cRZCallbackSystemService::kInterfaceId, cRZCallbackSystemService::kSystemServiceId, 0, 2,
                             0x276c784a, 0x276c784c, 0x287fb696, 0x287fb698, 0xffffffff, 0x089b7dc8, 0x3ae4beaa};

} // namespace

// Every slot of the three faces but the deleting destructor (next test), through each side's own vtable.
DIFF_TEST(cRZCallbackSystemService_Slots, 0xc68335)
{
    Pair pair(ctx);
    std::mt19937 rng(0xCB55);
    void *const kUntouched = reinterpret_cast<void *>(0x5a5a5a5a);
    for (const Slot &slot : kSlotTable) {
        if (slot.slot >= kSlotCount[slot.face]) ctx.Fail("%s: slot out of range", slot.name);
        const int runs = slot.slot == 0 ? 2 * static_cast<int>(std::size(kIds)) : 24;
        for (int run = 0; run < runs; ++run) {
            const Seeds seeds = RandomSeeds(rng, run);
            uint32_t args[2] = {rng(), rng()};
            if (slot.slot == 0) args[0] = kIds[run % std::size(kIds)];
            if (slot.face == 1 && (slot.slot == 7) && run % 3 == 0) args[0] &= 0xffffff00;
            FakeThread threadA(seeds.owned), threadB(seeds.owned);
            uint8_t *a = Instance(pair.a, seeds, threadA);
            uint8_t *b = Instance(pair.b, seeds, threadB);
            const svc::CodeMap mapA = MapFor(pair.a, threadA), mapB = MapFor(pair.b, threadB);
            void *outA = kUntouched, *outB = kUntouched;
            uint32_t argsA[2] = {args[0], slot.slot == 0 ? P(&outA) : args[1]};
            uint32_t argsB[2] = {args[0], slot.slot == 0 ? P(&outB) : args[1]};

            const uint32_t off = kFaceOffset[slot.face];
            const void *fnA = GameAddress<void *>(kGameVt[slot.face])[slot.slot];
            const void *fnB = svc::VTableAt(b, off)[slot.slot];
            gLog.clear();
            gThread = &threadA;
            difftest::BeginFrees();
            const uint32_t ra = svc::CallWords(fnA, a + off, slot.words, argsA);
            const bool freedA = difftest::FreedSinceBegin(a);
            const std::vector<uint32_t> logA = gLog;
            gLog.clear();
            gThread = &threadB;
            difftest::BeginFrees();
            const uint32_t rb = svc::CallWords(fnB, b + off, slot.words, argsB);
            const bool freedB = difftest::FreedSinceBegin(b);
            ++ctx.cases;

            uint32_t va = svc::Masked(slot.ret, ra), vb = svc::Masked(slot.ret, rb);
            if (slot.ret == svc::kPtr) {
                va = mapA.Code(va);
                vb = mapB.Code(vb);
            }
            if (va != vb) ctx.Fail("%s run %d: result 0x%x / 0x%x", slot.name, run, va, vb);
            if (logA != gLog) ctx.Fail("%s run %d: thread calls %zu / %zu", slot.name, run, logA.size(), gLog.size());
            if (freedA != freedB) ctx.Fail("%s run %d: freed %d / %d", slot.name, run, freedA, freedB);
            const int32_t offA = outA == kUntouched ? -1 : static_cast<int32_t>(P(outA) - P(a));
            const int32_t offB = outB == kUntouched ? -1 : static_cast<int32_t>(P(outB) - P(b));
            if (offA != offB) ctx.Fail("%s run %d id 0x%x: out offset %d / %d", slot.name, run, args[0], offA, offB);
            if (!freedA && !freedB) {
                const std::string diff = svc::Diff(svc::Words(a, kSize, mapA), svc::Words(b, kSize, mapB));
                if (!diff.empty()) ctx.Fail("%s run %d: object differs at %s", slot.name, run, diff.c_str());
            }
            // Spot checks of the original itself, so a fake that answered alike on both sides is noticed.
            if (slot.slot == 0) {
                const uint32_t id = args[0];
                const bool known = id == 1 || id == cRZCallbackSystemService::kInterfaceId || id == cRZCallbackSystemService::kSystemServiceId;
                if ((ra & 0xff) != (known ? 1u : 0u) || (known && offA != (id == cRZCallbackSystemService::kSystemServiceId ? 4 : 0)))
                    ctx.Fail("%s id 0x%x: original answered %u at %d", slot.name, id, ra & 0xff, offA);
            }
            if (!freedA) ::operator delete(a);
            if (!freedB) ::operator delete(b);
        }
    }
}

// The destructor 0xc682c7 (called directly; ours: our destructor, no dispatch), the deleting destructor 0xc68316 (on the
// primary pointer) and its +8 thunk 0xc6830e (ours: our +8 vtable's slot 3 for both), flags 0..3, ownership byte 0 or not.
DIFF_TEST(cRZCallbackSystemService_Destruct, 0xc682c7)
{
    Pair pair(ctx);
    auto dtor = ctx.Original<void(__thiscall *)(void *)>(kDtor);
    auto deleting = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeleting);
    auto thunk = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeletingThunk);
    std::mt19937 rng(0xCB5D);
    for (int run = 0; run < 72; ++run) {
        const int route = run % 3;
        const uint32_t flags = route == 0 ? 0 : (run / 3) % 4;
        const Seeds seeds = RandomSeeds(rng, run / 3);
        FakeThread threadA(seeds.owned), threadB(seeds.owned);
        uint8_t *a = Instance(pair.a, seeds, threadA);
        uint8_t *b = Instance(pair.b, seeds, threadB);
        const svc::CodeMap mapA = MapFor(pair.a, threadA), mapB = MapFor(pair.b, threadB);

        gLog.clear();
        gThread = &threadA;
        difftest::BeginFrees();
        void *ra = route == 0 ? (dtor(a), a) : route == 1 ? deleting(a, flags) : thunk(a + 8, flags);
        const bool freedA = difftest::FreedSinceBegin(a);
        const std::vector<uint32_t> logA = gLog;
        gLog.clear();
        gThread = &threadB;
        difftest::BeginFrees();
        void *rb = b;
        auto *ours = reinterpret_cast<cRZCallbackSystemService *>(b);
        if (route == 0) {
            ours->cRZCallbackSystemService::~cRZCallbackSystemService();
        } else {
            using Deleting = void *(__thiscall *)(void *, uint32_t);
            rb = reinterpret_cast<Deleting>(svc::VTableAt(b, 8)[3])(b + 8, flags);
        }
        const bool freedB = difftest::FreedSinceBegin(b);
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("route %d flags %u: return value is not the object", route, flags);
        if (freedA != freedB || freedA != ((flags & 1) != 0)) ctx.Fail("route %d flags %u: freed %d / %d", route, flags, freedA, freedB);
        if (logA != gLog) ctx.Fail("route %d owned %u: thread calls %zu / %zu", route, seeds.owned, logA.size(), gLog.size());
        const size_t expected = seeds.owned ? 2 : 3;
        if (logA.size() != expected || logA[0] != (seeds.owned ? 2u : 8u) || logA[1] != 1) ctx.Fail("route %d owned %u: original's thread calls unexpected", route, seeds.owned);
        if (!freedA && !freedB) {
            if (reinterpret_cast<uint32_t *>(a)[8] != 0) ctx.Fail("route %d: original did not clear the thread", route);
            const std::string diff = svc::Diff(svc::Words(a, kSize, mapA), svc::Words(b, kSize, mapB));
            if (!diff.empty()) ctx.Fail("route %d flags %u: object differs at %s", route, flags, diff.c_str());
        }
        if (!freedA) ::operator delete(a);
        if (!freedB) ::operator delete(b);
    }
}
