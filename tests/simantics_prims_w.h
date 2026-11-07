#pragma once
// Harness of the lane W primitive test (src/simantics/PrimW.h, ManageInventory 0x598ac0): recording fakes in the style of
// tests/simantics_prims_mid.h with a world big enough for the whole object (the handler touches fields up to +0x1c0).
//
// Every object the handler reaches is a Fake (a vtable pointer): each call is logged (object, slot, arguments) and answers
// from a script (or a hash of the seed). Pointers into the world are logged as offsets; pointers to the handler's own stack
// (out parameters, hidden result slots) as presence only. The original and ours run on two worlds built from one seed and
// are compared on the trace, the thrown code, the return value and the memory the handler writes.
#include "difftest.h"
#include "game/game.h"
#include "simantics/PrimW.h"

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <utility>
#include <vector>

#include <windows.h>

namespace {

using std::uint16_t;
using std::uint32_t;
using std::uint8_t;

struct Fake {
    void **vt;
    uint32_t pad;
};
constexpr int kFk = 24;
constexpr int kSl = 96;
constexpr int kMaxElems = 8;

// The memory the list fakes hand out: two lists per inventory, element cells and the begin/end cells the getters return.
struct InvMem {
    uint32_t elems[2][kMaxElems + 1];
    uint32_t beginCell[2];
    uint32_t endCell[2];
    uint32_t count[2];
};

struct WorldW {
    alignas(16) uint8_t obj[0x1d0]; // the PrimWObject
    void *vtThis[kSl];
    void *vtSec[16];
    Fake fk[kFk];
    void *vtFk[kFk][kSl];
    InvMem inv[2];
    uint16_t cursorList[kMaxElems + 1];
    uint32_t cursorBegin, cursorEnd, cursorCount;
    int16_t cells[16];
    uint8_t param[0x20];
    uint32_t canary;
};

// Fake indexes.
enum : int {
    kCH = 0,       // the context holder (object + 0x40)
    kFrame = 1,    // the stack frame
    kO1 = 2,       // the owner list object (context holder slot 6)
    kH = 3,        // inventory
    kP2 = 4,       // second inventory / person
    kCur = 5,      // the cursor (object + 0x1c0)
    kE0 = 6,       // entries 6..8
    kTblA = 9,     // this slot 63 object
    kZ = 10,       // context holder slot 26 object
    kStack = 11,   // the stack object (kZ slot 29)
    kOwner = 12,   // owner entry (kTblA slot 29)
    kOwnerData = 13,
    kDirector = 14,
    kHelper = 15,
    kSingleton = 16,
    kLookup = 17,
    kThing1 = 18,
    kThing2 = 19,
    kTyped = 20,
    kStackData = 21,
    kService = 22,
};

// ---- script ---------------------------------------------------------------------------------------------------------

struct RetEntry {
    uint32_t id;
    int slot;
    bool isObject;
    uint32_t value; // the value, or the offset of a world object (0xffffffff: null)
};

WorldW *gW = nullptr;
uint32_t gSeed = 0;

// Both sides run in this one world, built afresh from the seed before each run, so they see it at the same address (as in
// tests/simantics_prims_aa.h, lane EH). Why: the trace and the captured state hold words that may be pointers into the world
// (Norm turns those into world offsets) next to scripted random words (Mix answers, random parameter bytes). With two worlds
// at different addresses, a random word that happened to fall inside one world's range was normalised on that side only,
// and the seed differed although nothing diverged; the old retry on two more worlds made that rarer but not impossible. At
// one address Norm is the same function on both sides, so equal words stay equal and different words stay different: no
// comparison is weakened (a word the original logs as a world pointer must still be that same pointer in ours), and none
// depends on where the world sits. The outcome copies everything it compares out of the world before it is rebuilt.
WorldW &SharedWorld()
{
    static WorldW *const w = static_cast<WorldW *>(VirtualAlloc(nullptr, sizeof(WorldW), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    return *w;
}
// Test-only (W_INJECT_WORLD=1 in the environment): one in eight unscripted fake answers (and, in Build33, one in eight token
// dwords of the parameter block) becomes a word inside the shared world (its fixed address plus the random word folded into its size: a value, the same on both sides, that happens to look
// like a world pointer), the case that used to need a retry (see SharedWorld).
bool gInjectWorld = std::getenv("W_INJECT_WORLD") != nullptr;
uint32_t gInjected = 0; // words injected (both sides)
uint32_t InjectWorld(uint32_t v)
{
    if (!gInjectWorld || (v >> 29) != 0) return v;
    ++gInjected;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&SharedWorld())) + v % static_cast<uint32_t>(sizeof(WorldW));
}
std::vector<RetEntry> gRets;               // fixed answers
std::vector<std::vector<RetEntry>> gQ;     // per-call answers, consumed in call order (one list per object/slot)
std::vector<size_t> gQPos;
std::vector<uint32_t> gTrace;

// Exception injection: in some seeds the Nth fake call throws a short, so the cleanup (references released when an error
// leaves the frame) is compared too. Both sides see the same N.
uint32_t gThrowAt = 0;
uint32_t gHitCount = 0;
void Tick()
{
    if (++gHitCount == gThrowAt) throw static_cast<short>(0x66);
}

uint32_t Off(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(gW));
}

uint32_t Norm(uint32_t v)
{
    const uintptr_t base = reinterpret_cast<uintptr_t>(gW);
    if (gW && v >= base && v < base + sizeof(WorldW)) return 0x40000000u + static_cast<uint32_t>(v - base);
    return v;
}

uint32_t Mix(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u) * 0x85ebca6bu ^ (c + 0x165667b1u) * 0xc2b2ae35u;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return h;
}

uint32_t OffsetIn(const void *p, const WorldW &w)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(&w));
}

uint32_t Resolve(const RetEntry &e)
{
    if (!e.isObject) return e.value;
    return e.value == 0xffffffffu ? 0 : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gW) + e.value);
}

void RetValue(const void *obj, int slot, uint32_t value, WorldW &w)
{
    gRets.push_back({OffsetIn(obj, w), slot, false, value});
}
void RetObject(const void *obj, int slot, const void *target, WorldW &w)
{
    gRets.push_back({OffsetIn(obj, w), slot, true, target ? OffsetIn(target, w) : 0xffffffffu});
}

void Enqueue(const void *obj, int slot, bool isObject, uint32_t value, WorldW &w)
{
    const uint32_t id = OffsetIn(obj, w);
    for (std::vector<RetEntry> &q : gQ) {
        if (q[0].id == id && q[0].slot == slot) {
            q.push_back({id, slot, isObject, value});
            return;
        }
    }
    gQ.push_back({{id, slot, isObject, value}});
    gQPos.push_back(0);
}
void EnqueueObject(const void *obj, int slot, const void *target, WorldW &w)
{
    Enqueue(obj, slot, true, target ? OffsetIn(target, w) : 0xffffffffu, w);
}
void EnqueueValue(const void *obj, int slot, uint32_t value, WorldW &w)
{
    Enqueue(obj, slot, false, value, w);
}

uint32_t Scripted(void *self, int slot)
{
    const uint32_t id = Off(self);
    for (size_t i = 0; i < gQ.size(); ++i) {
        const std::vector<RetEntry> &q = gQ[i];
        if (q[0].id == id && q[0].slot == slot && gQPos[i] < q.size()) return Resolve(q[gQPos[i]++]);
    }
    for (const RetEntry &e : gRets) {
        if (e.id == id && e.slot == slot) return Resolve(e);
    }
    return InjectWorld(Mix(gSeed, id, static_cast<uint32_t>(slot)));
}

// The generic hit: logs the call, ticks (a seed may throw here), answers from the script.
uint32_t Hit(void *self, int slot, int n, const uint32_t *a)
{
    if (slot != 2) Tick(); // Release (slot 2) runs while errors unwind and must not throw
    gTrace.push_back(Off(self));
    gTrace.push_back(static_cast<uint32_t>(slot) << 8 | static_cast<uint32_t>(n));
    for (int i = 0; i < n; ++i) gTrace.push_back(Norm(a[i]));
    return Scripted(self, slot);
}

template <int S, int N> struct Th;
template <int S> struct Th<S, 0> {
    static uint32_t __fastcall F(void *self, void *) { return Hit(self, S, 0, nullptr); }
};
template <int S> struct Th<S, 1> {
    static uint32_t __fastcall F(void *self, void *, uint32_t a1)
    {
        const uint32_t a[] = {a1};
        return Hit(self, S, 1, a);
    }
};
template <int S> struct Th<S, 2> {
    static uint32_t __fastcall F(void *self, void *, uint32_t a1, uint32_t a2)
    {
        const uint32_t a[] = {a1, a2};
        return Hit(self, S, 2, a);
    }
};
template <int S> struct Th<S, 3> {
    static uint32_t __fastcall F(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3)
    {
        const uint32_t a[] = {a1, a2, a3};
        return Hit(self, S, 3, a);
    }
};
template <int S> struct Th<S, 4> {
    static uint32_t __fastcall F(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
    {
        const uint32_t a[] = {a1, a2, a3, a4};
        return Hit(self, S, 4, a);
    }
};

void *gThunk[kSl][5];

template <int... S> void FillThunks(std::integer_sequence<int, S...>)
{
    ((gThunk[S][0] = reinterpret_cast<void *>(&Th<S, 0>::F), gThunk[S][1] = reinterpret_cast<void *>(&Th<S, 1>::F),
      gThunk[S][2] = reinterpret_cast<void *>(&Th<S, 2>::F), gThunk[S][3] = reinterpret_cast<void *>(&Th<S, 3>::F),
      gThunk[S][4] = reinterpret_cast<void *>(&Th<S, 4>::F)),
     ...);
}

void EnsureThunks()
{
    static bool done = false;
    if (!done) {
        FillThunks(std::make_integer_sequence<int, kSl>());
        done = true;
    }
}

void Use(void **vtable, int slot, int args)
{
    vtable[slot] = gThunk[slot][args];
}

} // namespace
