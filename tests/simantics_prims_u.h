#pragma once
// Shared pieces of the lane U tests (src/simantics/OperandReader.h): a recording-fake world for the SimAntics operand
// reader 0x531700. The reader reaches everything through offsets of the object (this = the secondary subobject) and
// through vtable slots of the objects those fields lead to, so the world is the object body (a block of memory with
// the fields the reader reads) plus fake objects whose slots are recorders: every call logs (object, slot,
// arguments) and returns a scripted value; original and ours are compared on the calls made, in order, the value,
// the exception thrown and the destination written. World pointers in logged values are normalised to offsets so the
// two worlds (one per implementation) compare equal.
#include "difftest.h"
#include "game/game.h"
#include "rz/RZAutoRef.h"
#include "simantics/OperandReader.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <random>
#include <utility>
#include <vector>

namespace {

using std::uint16_t;
using std::uint32_t;
using std::uint8_t;

constexpr int kF = 40;       // fake objects
constexpr int kSlots = 288;  // vtable slots (the reader calls up to slot 280)
constexpr int kBody = 0x1fc;

struct UBig {
    void **vt;
    uint8_t body[kBody]; // fields the reader reads from the person data (P) at fixed offsets
};

struct UWorld {
    alignas(16) uint8_t obj[0x200]; // [0] primary vtable; the subobject S = obj + 4 (fields at S + offset)
    void *vtObj[kSlots];
    void *vtSec[64];
    void *vtStack[16]; // the embedded stack counter at S + 0x18
    UBig fk[kF];
    void *vtFk[kF][kSlots];
    uint8_t table[0x1a40]; // the stack object table owner at S + 0x1c0 (first dword: vtable)
    void *vtTable[kSlots];
    void *objVec[8];
    void *frameVec[4];
    int16_t cells[640];
    uint8_t ranges[16 * 12];
    int16_t shorts[32];
    uint32_t listMem[32];
    uint8_t buf[64];
    uint32_t canary;
};

uint8_t *SubOf(UWorld &w)
{
    return w.obj + 4;
}

// ---- scripted returns ----------------------------------------------------------------------------------------------

struct RetEntry {
    uint32_t id;
    int slot;
    bool isObject;
    uint32_t value; // the value, or the offset of a world object (0xffffffff: null)
};

UWorld *gW = nullptr;
uint32_t gSeed = 0;

// Both sides run in this one world, built afresh from the seed before each run, so they see it at the same address (as in
// tests/simantics_prims_aa.h, lane EH). Why: the trace and the captured state hold words that may be pointers into the world
// (Norm turns those into world offsets) next to random words (the world's random fill, the arguments). With two worlds at
// different addresses, a random word that happened to fall inside one world's range was normalised on that side only, and
// the seed differed although nothing diverged; the old retry on two more worlds made that rarer but not impossible. At one
// address Norm is the same function on both sides, so equal words stay equal and different words stay different: no
// comparison is weakened (a word the original logs as a world pointer must still be that same pointer in ours), and none
// depends on where the world sits. The outcome copies everything it compares out of the world before it is rebuilt.
UWorld &SharedWorld()
{
    static UWorld *const w = static_cast<UWorld *>(::operator new(sizeof(UWorld))); // (raw memory, kept for the process; Build zeroes it)
    return *w;
}
// Test-only (U_INJECT_WORLD=1 in the environment): one in eight dwords of the world's random fill, and some of the arguments
// passed on to the fakes (MakeArgs in simantics_prims_u_reader.cpp), become a word inside the shared world (its fixed address plus the random word folded into its size: a value, the same on both sides, that happens
// to look like a world pointer), the case that used to need a retry (see SharedWorld).
bool gInjectWorld = std::getenv("U_INJECT_WORLD") != nullptr;
uint32_t gInjected = 0; // words injected (both sides)
uint32_t InjectWorld(uint32_t v)
{
    if (!gInjectWorld || (v >> 29) != 0) return v;
    ++gInjected;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&SharedWorld())) + v % static_cast<uint32_t>(sizeof(UWorld));
}
std::vector<RetEntry> gRets;
std::vector<uint32_t> gTrace;

uint32_t gThrowAt = 0;
uint32_t gHitCount = 0;
void Tick()
{
    if (++gHitCount == gThrowAt) throw static_cast<short>(0x66);
}

uint32_t OffOf(const void *p, const UWorld &w)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(&w));
}

uint32_t Off(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(gW));
}

uint32_t Norm(uint32_t v)
{
    const uintptr_t base = reinterpret_cast<uintptr_t>(gW);
    if (gW && v >= base && v < base + sizeof(UWorld)) return 0x40000000u + static_cast<uint32_t>(v - base);
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

void RetValue(const void *objInWorld, int slot, uint32_t value, const UWorld &w)
{
    gRets.push_back({OffOf(objInWorld, w), slot, false, value});
}
void RetObject(const void *objInWorld, int slot, const void *target, const UWorld &w)
{
    gRets.push_back({OffOf(objInWorld, w), slot, true, target ? OffOf(target, w) : 0xffffffffu});
}

// The scripted result for a slot; unscripted slots answer 0 (null / false / zero).
uint32_t Scripted(void *self, int slot)
{
    const uint32_t id = Off(self);
    for (const RetEntry &e : gRets) {
        if (e.id == id && e.slot == slot) {
            if (!e.isObject) return e.value;
            return e.value == 0xffffffffu ? 0 : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gW) + e.value);
        }
    }
    return 0;
}

uint32_t Hit(void *self, int slot, int n, const uint32_t *a)
{
    if (slot != 2) Tick(); // Release (slot 2) runs while errors unwind and must not throw
    gTrace.push_back(Off(self));
    gTrace.push_back(static_cast<uint32_t>(slot) << 8 | static_cast<uint32_t>(n));
    for (int i = 0; i < n; ++i) gTrace.push_back(Norm(a[i]));
    return Scripted(self, slot);
}

// ---- generic thunks: slot S taking N stack arguments (__thiscall) -----------------------------------------------------

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

void *gThunk[kSlots][4];

template <int... S> void FillThunks(std::integer_sequence<int, S...>)
{
    ((gThunk[S][0] = reinterpret_cast<void *>(&Th<S, 0>::F), gThunk[S][1] = reinterpret_cast<void *>(&Th<S, 1>::F),
      gThunk[S][2] = reinterpret_cast<void *>(&Th<S, 2>::F), gThunk[S][3] = reinterpret_cast<void *>(&Th<S, 3>::F)),
     ...);
}

void EnsureThunks()
{
    static bool done = false;
    if (!done) {
        FillThunks(std::make_integer_sequence<int, kSlots>());
        done = true;
    }
}

void Use(void **vtable, int slot, int args)
{
    vtable[slot] = gThunk[slot][args];
}

uint32_t Pick(std::mt19937 &r, std::initializer_list<uint32_t> values)
{
    return *(values.begin() + r() % values.size());
}

} // namespace
