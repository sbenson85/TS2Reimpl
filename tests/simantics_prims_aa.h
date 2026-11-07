#pragma once
// Shared pieces of the lane AA primitive tests (src/simantics/PrimAA.h): Effect (0x5805c0) and MaterialChange
// (0x5827e0). The recording-fake idea of tests/simantics_prims_mid.h, rebuilt with a bigger world: every object the
// handlers reach is a fake with a vtable whose slots record their calls (slot, arguments by kind) and return scripted
// values, so the original (the mapped image) and ours run on the same worlds and are compared on the calls made, in
// order, the thrown code, the return value and the memory the fakes were given.
//
// The thunks are built from a small description of the arguments (one character each): see ArgKind below. Pointers
// into the world are normalised to offsets; stack pointers of the handlers never reach the log (strings and keys are
// logged by content, out pointers by presence).
#include "difftest.h"
#include "game/game.h"
#include "rz/cRZString.h"
#include "simantics/PrimAA.h"
#include "simantics/PrimAACommon.h"
#include "ts_string_fake.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

using std::uint16_t;
using std::uint32_t;
using std::uint8_t;

constexpr int kFakes = 44;
constexpr int kSlots = 320;
constexpr int kSubs = 8;

struct Fake {
    void **vt;
    uint32_t pad; // +4: the vbptr of fakes that stand for objects with a virtual base
    uint8_t rest[0x1f8]; // room for the fields some handlers read at larger offsets (+0x18c)
};
static_assert(sizeof(Fake) == 0x200, "Fake size");

struct World {
    alignas(16) uint8_t obj[0x200]; // the object under test (PrimAAObject is 0x1c4 bytes)
    void *vtThis[kSlots];
    void *vtSec[64];
    Fake fk[kFakes];
    void *vtFk[kFakes][kSlots];
    Fake sub[kSubs]; // virtual-base subobjects of fakes
    void *vtSub[kSubs][64];
    int32_t vbtab[kSubs][2];
    uint32_t vec[48]; // memory the fakes hand out as vectors (first, last, end, then elements)
    uint8_t shorts[64];
    uint32_t scratch[8];
    uint8_t param[0x20];
    uint32_t canary;
};

// ---- scripted returns ----------------------------------------------------------------------------------------------

struct RetEntry {
    uint32_t id;
    int slot;
    bool isObject;
    uint32_t value; // the value, or the offset of a world object (0xffffffff: null)
};

World *gW = nullptr;
uint32_t gSeed = 0;
// Test-only (AA_INJECT_WORLD=1 in the environment): one in eight unscripted fake returns becomes a word inside the
// world (its address plus the random word folded into its size), the case that used to flake (see SharedWorld).
bool gInjectWorld = std::getenv("AA_INJECT_WORLD") != nullptr;
uint32_t gInjected = 0; // words injected (both sides), printed with the histograms
std::vector<RetEntry> gRets;
std::vector<uint32_t> gTrace;
std::vector<uint32_t> gEvents; // (role << 16 | slot) of every fake call, for the coverage report
std::vector<uint32_t> gQueues[kSlots]; // per-slot queues of objects (offsets) for the 'L' kind
size_t gQueuePos[kSlots];

// In some seeds the Nth fake call throws a short so that the handlers' unwinding (references given back) is compared
// too. Both sides see the same N.
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
    if (gW && v >= base && v < base + sizeof(World)) return 0x40000000u + static_cast<uint32_t>(v - base);
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

uint32_t WorldOff(const void *p, World &w)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(&w));
}

void RetValue(const void *objInWorld, int slot, uint32_t value, World &w)
{
    gRets.push_back({WorldOff(objInWorld, w), slot, false, value});
}
void RetObject(const void *objInWorld, int slot, const void *target, World &w)
{
    gRets.push_back({WorldOff(objInWorld, w), slot, true, target ? WorldOff(target, w) : 0xffffffffu});
}

uint32_t Scripted(void *self, int slot)
{
    const uint32_t id = Off(self);
    for (const RetEntry &e : gRets) {
        if (e.id == id && e.slot == slot) {
            if (!e.isObject) return e.value;
            return e.value == 0xffffffffu ? 0 : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gW) + e.value);
        }
    }
    const uint32_t v = Mix(gSeed, id, static_cast<uint32_t>(slot));
    if (gInjectWorld && (v >> 29) == 0) {
        ++gInjected;
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gW)) + v % static_cast<uint32_t>(sizeof(*gW));
    }
    return v;
}

// Scripts for the per-argument outputs (out objects, filled texts, keys): key = base + slot * 8 + argument index.
constexpr int kOutObject = 0x1000;
constexpr int kOutText = 0x2000;
constexpr int kOutByte = 0x3000;
constexpr int kOutKey = 0x4000;
int OutKey(int base, int slot, int arg)
{
    return base + slot * 8 + arg;
}

void QueueObject(int slot, const void *target, World &w)
{
    gQueues[slot].push_back(target ? WorldOff(target, w) : 0xffffffffu);
}
void QueueValue(int slot, uint32_t value)
{
    gQueues[slot].push_back(value);
}
// The value slot `slot` (kind X) returns for first argument `index`.
void RetIndexed(const void *objInWorld, int slot, uint32_t index, uint32_t value, World &w)
{
    gRets.push_back({WorldOff(objInWorld, w), 0x5000 + slot * 64 + static_cast<int>(index & 63), false, value});
}

// ---- logging helpers -------------------------------------------------------------------------------------------------

void LogString(const char *s)
{
    if (!s) {
        gTrace.push_back(0xdead5001u);
        return;
    }
    const size_t n = std::strlen(s);
    gTrace.push_back(static_cast<uint32_t>(n));
    for (size_t i = 0; i < n; ++i) gTrace.push_back(static_cast<uint8_t>(s[i]));
}

void LogBytes(const void *p, size_t n)
{
    if (!p) {
        gTrace.push_back(0xdead5002u);
        return;
    }
    for (size_t i = 0; i < n; ++i) gTrace.push_back(static_cast<const uint8_t *>(p)[i]);
}

// Texts the fakes write into out strings.
const char *const kTexts[] = {"", "fx_spark", "Bella Goth", "x", "tvstation_none", "a longer text that does not fit a small buffer 12345", "##key", "wall%d"};
constexpr int kTextCount = sizeof(kTexts) / sizeof(kTexts[0]);

// ---- the argument description ------------------------------------------------------------------------------------------
//   V  a value (pointers into the world are normalised)
//   S  const char *: logged by content
//   s  std::string *: logged by content
//   C  cRZString *: logged by content
//   c  cRZString * to fill: logged as present, a scripted text is written into it
//   P  void ** out: logged as present (and whether it was empty), a scripted object (or null) is written
//   B  a holder whose byte at +4 is logged and then set from the script
//   p  an opaque pointer to a stack object: logged as present
//   K  a 16-byte key: logged by content
//   k  a 16-byte key to fill from the script; the slot returns the pointer to it
//   F  float *: logged by value
//   b  uint8_t * out: logged as present, a scripted byte written
//   q  a vector passed by value (three words): logged by size and 0x14-byte elements
//   M  a cRZString * when the previous argument is 0, else a value
//   L  (no argument) the slot returns the next object of its queue
//   R  (no argument) the slot returns the next value of its queue
//   X  (no argument) the slot returns a value scripted per first argument (RetIndexed)
constexpr int WordsOf(char k)
{
    return k == 'q' ? 3 : ((k == 'L' || k == 'R' || k == 'X') ? 0 : 1);
}

bool gLive = false;

uint32_t Run(void *self, int slot, const char *kinds, const uint32_t *a, int words)
{
    if (slot != 2) Tick(); // Release (slot 2) runs while errors unwind and must not throw
    if (gLive) {
        std::printf("      call fk%d slot %d\n", static_cast<int>((Off(self) - offsetof(World, fk)) / sizeof(Fake)), slot);
        std::fflush(stdout);
    }
    {
        const uint32_t off = Off(self);
        const uint32_t fkOff = static_cast<uint32_t>(offsetof(World, fk));
        const uint32_t role = (off >= fkOff && off < fkOff + sizeof(Fake) * kFakes) ? (off - fkOff) / static_cast<uint32_t>(sizeof(Fake)) : 0xfffu;
        gEvents.push_back(role << 16 | static_cast<uint32_t>(slot));
    }
    gTrace.push_back(Off(self));
    gTrace.push_back(static_cast<uint32_t>(slot) << 8 | static_cast<uint32_t>(words));
    int i = 0;
    uint32_t forced = 0;
    bool hasForced = false;
    bool fromQueue = false;
    bool fromValueQueue = false;
    bool keyedByIndex = false;
    for (const char *k = kinds; *k; ++k) {
        const int argIndex = i;
        switch (*k) {
        case 'V':
            gTrace.push_back(Norm(a[i++]));
            break;
        case 'S':
            LogString(reinterpret_cast<const char *>(static_cast<uintptr_t>(a[i++])));
            break;
        case 's': {
            const std::string *s = reinterpret_cast<const std::string *>(static_cast<uintptr_t>(a[i++]));
            LogString(s ? s->c_str() : nullptr);
            break;
        }
        case 'C': {
            const cRZString *s = reinterpret_cast<const cRZString *>(static_cast<uintptr_t>(a[i++]));
            LogString(s ? s->mString.c_str() : nullptr);
            break;
        }
        case 'c': {
            cRZString *s = reinterpret_cast<cRZString *>(static_cast<uintptr_t>(a[i++]));
            gTrace.push_back(s ? 1u : 0u);
            if (s) {
                gTrace.push_back(static_cast<uint32_t>(s->mString.size()));
                s->mString = kTexts[Scripted(self, OutKey(kOutText, slot, argIndex)) % kTextCount];
            }
            break;
        }
        case 'P': {
            void **out = reinterpret_cast<void **>(static_cast<uintptr_t>(a[i++]));
            gTrace.push_back(out ? 1u : 0u);
            if (out) {
                gTrace.push_back(*out ? 0xbad0u : 0u); // a holder is reset before the call
                *out = reinterpret_cast<void *>(static_cast<uintptr_t>(Scripted(self, OutKey(kOutObject, slot, argIndex))));
            }
            break;
        }
        case 'B': {
            uint8_t *holder = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(a[i++]));
            gTrace.push_back(holder ? 1u : 0u);
            if (holder) {
                gTrace.push_back(holder[4]);
                holder[4] = static_cast<uint8_t>(Scripted(self, OutKey(kOutByte, slot, argIndex)));
            }
            break;
        }
        case 'p':
            gTrace.push_back(a[i++] ? 1u : 0u);
            break;
        case 'u': {
            // uint32_t * out: logged as present, a scripted word written
            uint32_t *out = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(a[i++]));
            gTrace.push_back(out ? 1u : 0u);
            if (out) *out = Scripted(self, OutKey(kOutByte, slot, argIndex));
            break;
        }
        case 'y': {
            // a reply buffer: a pointer to a scripted object (or null) is written at +4
            uint8_t *buffer = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(a[i++]));
            gTrace.push_back(buffer ? 1u : 0u);
            if (buffer) {
                const uint32_t object = Scripted(self, OutKey(kOutObject, slot, argIndex));
                std::memcpy(buffer + 4, &object, 4);
            }
            break;
        }
        case 'v': {
            // A ShortVec the handler built on its stack: the fake fills it with scripted shorts through our push_back.
            primAA::ShortVec *shorts = reinterpret_cast<primAA::ShortVec *>(static_cast<uintptr_t>(a[i++]));
            gTrace.push_back(shorts ? 1u : 0u);
            if (shorts) {
                const uint32_t n = Scripted(self, OutKey(kOutByte, slot, argIndex)) % 6;
                gTrace.push_back(n);
                for (uint32_t e = 0; e < n; ++e) {
                    const int16_t v = static_cast<int16_t>(Mix(Scripted(self, OutKey(kOutByte, slot, argIndex)), e, 0x77));
                    shorts->PushBack(v);
                }
            }
            break;
        }
        case 'K':
            LogBytes(reinterpret_cast<const void *>(static_cast<uintptr_t>(a[i++])), 16);
            break;
        case 'k': {
            uint8_t *key = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(a[i++]));
            gTrace.push_back(key ? 1u : 0u);
            if (key) {
                for (int n = 0; n < 16; n += 4) {
                    const uint32_t v = Mix(Scripted(self, OutKey(kOutKey, slot, argIndex)), static_cast<uint32_t>(n), 0x6b);
                    std::memcpy(key + n, &v, 4);
                }
                forced = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(key));
                hasForced = true;
            }
            break;
        }
        case 'F': {
            const float *f = reinterpret_cast<const float *>(static_cast<uintptr_t>(a[i++]));
            uint32_t bits = 0xdead5003u;
            if (f) std::memcpy(&bits, f, 4);
            gTrace.push_back(bits);
            break;
        }
        case 'b': {
            uint8_t *out = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(a[i++]));
            gTrace.push_back(out ? 1u : 0u);
            if (out) *out = static_cast<uint8_t>(Scripted(self, OutKey(kOutByte, slot, argIndex)));
            break;
        }
        case 'q': {
            const uint8_t *first = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(a[i]));
            const uint8_t *last = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(a[i + 1]));
            const uint8_t *end = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(a[i + 2]));
            i += 3;
            const uint32_t count = first ? static_cast<uint32_t>((last - first) / 0x14) : 0;
            gTrace.push_back(count);
            gTrace.push_back(first ? static_cast<uint32_t>((end - first) / 0x14) : 0);
            for (uint32_t e = 0; e < count; ++e) {
                uint32_t object;
                std::memcpy(&object, first + e * 0x14, 4);
                gTrace.push_back(Norm(object));
                gTrace.push_back(first[e * 0x14 + 4]);
                LogBytes(first + e * 0x14 + 8, 12);
            }
            break;
        }
        case 'L':
            fromQueue = true;
            break;
        case 'R':
            fromValueQueue = true;
            break;
        case 'X':
            keyedByIndex = true;
            break;
        case 'M': {
            // The argument is a cRZString * when the previous argument is 0, else a value.
            const uint32_t prev = i > 0 ? a[i - 1] : 0;
            const uint32_t v = a[i++];
            if (prev == 0) {
                const cRZString *str = reinterpret_cast<const cRZString *>(static_cast<uintptr_t>(v));
                LogString(str ? str->mString.c_str() : nullptr);
            } else {
                gTrace.push_back(Norm(v));
            }
            break;
        }
        default:
            break;
        }
    }
    if (fromQueue) {
        const uint32_t e = gQueuePos[slot] < gQueues[slot].size() ? gQueues[slot][gQueuePos[slot]++] : 0xffffffffu;
        return e == 0xffffffffu ? 0 : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gW) + e);
    }
    if (fromValueQueue) return gQueuePos[slot] < gQueues[slot].size() ? gQueues[slot][gQueuePos[slot]++] : 0u;
    if (keyedByIndex) return Scripted(self, 0x5000 + slot * 64 + static_cast<int>(a[0] & 63));
    return hasForced ? forced : Scripted(self, slot);
}

template <size_t> using Word = uint32_t;

// Slots nobody installed: log and throw, so a world that misses one makes both sides stop at the same call.
template <int S> uint32_t __fastcall TrapThunk(void *self, void *)
{
    gTrace.push_back(Off(self));
    gTrace.push_back(0x7a70000u | static_cast<uint32_t>(S));
    throw static_cast<short>(0x5555);
}
void *gTrapThunks[kSlots];
template <int... S> void FillTraps(std::integer_sequence<int, S...>)
{
    ((gTrapThunks[S] = reinterpret_cast<void *>(&TrapThunk<S>)), ...);
}

template <int S, typename Seq, char... K> struct Thunk;
template <int S, size_t... I, char... K> struct Thunk<S, std::index_sequence<I...>, K...> {
    static uint32_t __fastcall F(void *self, void *, Word<I>... a)
    {
        const uint32_t args[] = {a..., 0};
        static const char kinds[] = {K..., 0};
        return Run(self, S, kinds, args, static_cast<int>(sizeof...(I)));
    }
};

template <char... K> constexpr size_t WordCount()
{
    return (static_cast<size_t>(WordsOf(K)) + ... + 0);
}

// Installs a recording slot S taking the arguments described by K... in `vt`.
template <int S, char... K> void Th(void **vt)
{
    vt[S] = reinterpret_cast<void *>(&Thunk<S, std::make_index_sequence<WordCount<K...>()>, K...>::F);
}

// ---- world construction --------------------------------------------------------------------------------------------

void Base(World &w, uint32_t seed, std::mt19937 &r)
{
    static bool trapsReady = false;
    if (!trapsReady) {
        FillTraps(std::make_integer_sequence<int, kSlots>());
        trapsReady = true;
    }
    std::memset(&w, 0, sizeof(World));
    for (int s = 0; s < kSlots; ++s) {
        w.vtThis[s] = gTrapThunks[s];
        for (int i = 0; i < kFakes; ++i) w.vtFk[i][s] = gTrapThunks[s];
    }
    for (int s = 0; s < 64; ++s) {
        w.vtSec[s] = gTrapThunks[s];
        for (int i = 0; i < kSubs; ++i) w.vtSub[i][s] = gTrapThunks[s];
    }
    auto fill = [&](void *p, size_t n) {
        for (size_t i = 0; i < n; ++i) static_cast<uint8_t *>(p)[i] = static_cast<uint8_t>(r());
    };
    fill(w.param, sizeof(w.param));
    for (int i = 0; i < kSubs; ++i) w.sub[i].vt = w.vtSub[i];
    Th<1>(w.vtThis);
    Th<2>(w.vtThis);
    *reinterpret_cast<void ***>(w.obj) = w.vtThis;
    *reinterpret_cast<void ***>(w.obj + 4) = w.vtSec;
    for (int i = 0; i < kFakes; ++i) {
        w.fk[i].vt = w.vtFk[i];
        Th<1>(w.vtFk[i]);
        Th<2>(w.vtFk[i]);
    }
    for (int i = 0; i < kSubs; ++i) {
        Th<1>(w.vtSub[i]);
        Th<2>(w.vtSub[i]);
    }
    *reinterpret_cast<void **>(w.obj + 0x40) = &w.fk[0];
    gRets.clear();
    for (auto &q : gQueues) q.clear();
    gSeed = seed;
    gThrowAt = (r() % 8 == 0) ? 1 + r() % 12 : 0;
    w.canary = 0xC0DEC0DEu;
}

// Makes fk[index] an object with a virtual base (vbptr at +4, offset table entry 1) at sub[k].
void MakeVirtualBase(World &w, int index, int k)
{
    w.fk[index].pad = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(w.vbtab[k]));
    w.vbtab[k][0] = 0;
    w.vbtab[k][1] = static_cast<int32_t>(reinterpret_cast<uint8_t *>(&w.sub[k]) - (reinterpret_cast<uint8_t *>(&w.fk[index]) + 4));
}

// Set by tests that need global game state to point into the world being run (called before each run).
std::function<void(World &)> gBeforeRun;

struct Outcome {
    uint32_t ret = 0;
    int thrown = -1;
    std::vector<uint32_t> trace;
    std::vector<uint32_t> state;
    std::vector<uint32_t> events;
};

void Capture(World &w, Outcome &o)
{
    // vec[0..2] is a ShortVec (first, last, end): its size, capacity and contents, not the heap addresses.
    {
        const uint8_t *first = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(w.vec[0]));
        const uint8_t *last = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(w.vec[1]));
        const uint8_t *end = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(w.vec[2]));
        o.state.push_back(first ? static_cast<uint32_t>(last - first) : 0u);
        o.state.push_back(first ? static_cast<uint32_t>(end - first) : 0u);
        if (first && last >= first && last - first < 256)
            for (const uint8_t *q = first; q < last; ++q) o.state.push_back(*q);
    }
    for (int k = 3; k < 48; ++k) o.state.push_back(Norm(w.vec[k]));
    for (uint8_t b : w.shorts) o.state.push_back(b);
    for (uint32_t s : w.scratch) o.state.push_back(Norm(s));
    o.state.push_back(w.canary);
}

// Prints where an access violation happened (module-relative) so a crash in ours or in a stand-in can be located.
long CrashFilter(EXCEPTION_POINTERS *info)
{
    if (info->ExceptionRecord->ExceptionCode == 0xE06D7363u) return EXCEPTION_CONTINUE_SEARCH; // C++ exceptions
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    std::printf("      CRASH %08lx at +0x%x accessing 0x%x\n", info->ExceptionRecord->ExceptionCode,
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress) - base),
                info->ExceptionRecord->NumberParameters > 1 ? static_cast<unsigned>(info->ExceptionRecord->ExceptionInformation[1]) : 0u);
    return EXCEPTION_CONTINUE_SEARCH;
}

template <typename F> uint32_t GuardedCall(F &f, World &w)
{
    __try {
        return f(w);
    } __except (CrashFilter(GetExceptionInformation())) {
        return 0;
    }
}

template <typename Call> Outcome RunWorld(World &w, Call call)
{
    gW = &w;
    gHitCount = 0;
    for (auto &p : gQueuePos) p = 0;
    gTrace.clear();
    gEvents.clear();
    if (gBeforeRun) gBeforeRun(w);
    Outcome o;
    try {
        o.ret = GuardedCall(call, w);
    } catch (short code) {
        o.thrown = code;
    }
    o.trace = gTrace;
    o.events = gEvents;
    Capture(w, o);
    gW = nullptr;
    return o;
}

std::map<uint32_t, uint32_t> gTrapHist;  // trap slot -> count
std::map<int, uint32_t> gThrownHist;     // thrown code -> count
std::map<uint32_t, uint32_t> gEventHist; // (role << 16 | slot) -> number of runs that made the call

struct Stats {
    uint32_t throws = 0, returns = 0;
    uint32_t byRet[8] = {};
    uint32_t calls = 0;
};

struct Trial {
    bool bad = false;
    Outcome oa, ob;
};

// Both sides run in this one world, built afresh from the seed before each run, so they see it at the same address.
// Why: the trace and the captured state hold words that may be pointers into the world (Norm turns those into world
// offsets, for readable dumps) next to scripted random words. With two worlds at different heap addresses, a random
// word that happened to fall inside one world's range was normalised on that side only, and the test failed for about
// one full run in three (lane DX: Prim70 seed 7531, 0x1ee77d85 inside world A at 0x1ee6c010). At one address Norm is the
// same function on both sides, so equal words stay equal and different words stay different: no comparison is weakened
// (a word the original logs as a world pointer must still be that same pointer in ours), and none depends on where the
// heap put the world. The earlier retry with a second pair of worlds (which could hide nothing, but also fixed nothing
// when the retry landed badly) is gone. The outcome copies everything it compares out of the world before it is rebuilt.
World &SharedWorld()
{
    static World *const w = static_cast<World *>(VirtualAlloc(nullptr, sizeof(World), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    return *w;
}

template <typename Build, typename Orig, typename Ours>
Trial Try(World &w, uint32_t seed, Build build, Orig original, Ours ours, bool compareRet)
{
    Trial t;
    build(w, seed);
    t.oa = RunWorld(w, [&](World &x) { return original(x.obj, &x.fk[1], x.param); });
    build(w, seed);
    t.ob = RunWorld(w, [&](World &x) { return ours(x.obj, &x.fk[1], x.param); });
    t.bad = t.oa.thrown != t.ob.thrown || t.oa.trace != t.ob.trace || t.oa.state != t.ob.state ||
            (compareRet && t.oa.thrown < 0 && t.oa.ret != t.ob.ret);
    return t;
}

uint32_t gDumpSeed = 0xffffffffu;
uint32_t gCurSeed = 0;

void TerminateReport()
{
    std::printf("      TERMINATE during seed %u (trace so far %zu entries)\n", gCurSeed, gTrace.size());
    std::fflush(stdout);
    std::abort();
}

void DumpTrace(const char *label, const std::vector<uint32_t> &t)
{
    std::printf("      %s (%zu):", label, t.size());
    const uint32_t fkOff = static_cast<uint32_t>(offsetof(World, fk));
    for (uint32_t v : t) {
        const uint32_t stride = static_cast<uint32_t>(sizeof(Fake));
        if (v >= fkOff && v < fkOff + stride * kFakes && (v - fkOff) % stride == 0) std::printf(" fk%u", (v - fkOff) / stride);
        else if (v >= 0x40000000u + fkOff && v < 0x40000000u + fkOff + stride * kFakes && (v - 0x40000000u - fkOff) % stride == 0)
            std::printf(" *fk%u", (v - 0x40000000u - fkOff) / stride);
        else std::printf(" %x", v);
    }
    std::printf("\n");
}

template <typename Build, typename Orig, typename Ours>
void Compare(difftest::Context &ctx, const char *name, uint32_t seeds, Build build, Orig original, Ours ours, Stats &st, bool compareRet = true)
{
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        gCurSeed = seed;
        Trial t = Try(SharedWorld(), seed, build, original, ours, compareRet);
        ++ctx.cases;
        const Outcome &oa = t.oa, &ob = t.ob;
        if (seed == gDumpSeed) {
            DumpTrace("original", oa.trace);
            DumpTrace("ours", ob.trace);
        }
        st.calls += static_cast<uint32_t>(oa.trace.size());
        {
            std::vector<uint32_t> distinct = oa.events;
            std::sort(distinct.begin(), distinct.end());
            distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
            for (uint32_t e : distinct) ++gEventHist[e];
        }
        if (oa.thrown >= 0) ++gThrownHist[oa.thrown];
        if (oa.thrown == 0x5555) {
            for (uint32_t v : oa.trace)
                if ((v & 0xffff0000u) == 0x07a70000u) ++gTrapHist[v & 0xffff];
        }
        if (oa.thrown >= 0) ++st.throws;
        else {
            ++st.returns;
            if (oa.ret < 8) ++st.byRet[oa.ret];
        }
        if (t.bad) {
            size_t k = 0;
            while (k < oa.trace.size() && k < ob.trace.size() && oa.trace[k] == ob.trace[k]) ++k;
            ctx.Fail("%s seed %u: (original/ours) thrown %d/%d ret 0x%x/0x%x trace %zu/%zu (first diff at %zu: 0x%x/0x%x) state %s", name, seed, oa.thrown,
                     ob.thrown, oa.ret, ob.ret, oa.trace.size(), ob.trace.size(), k, k < oa.trace.size() ? oa.trace[k] : 0xdeadu,
                     k < ob.trace.size() ? ob.trace[k] : 0xdeadu, oa.state == ob.state ? "same" : "DIFFERENT");
        }
    }
}

void PrintHists()
{
    std::printf("      thrown:");
    for (auto &kv : gThrownHist) std::printf(" %x:%u", kv.first, kv.second);
    std::printf("\n      traps (slot:count):");
    for (auto &kv : gTrapHist) std::printf(" %u:%u", kv.first, kv.second);
    std::printf("\n");
    std::printf("      events (role.slot:runs):");
    for (auto &kv : gEventHist) std::printf(" %u.%u:%u", kv.first >> 16, kv.first & 0xffff, kv.second);
    std::printf("\n");
    if (gInjectWorld) std::printf("      AA_INJECT_WORLD: %u world-range words returned by fakes\n", gInjected);
    gInjected = 0;
    gThrownHist.clear();
    gTrapHist.clear();
    gEventHist.clear();
}

using OrigFn = uint32_t(__thiscall *)(void *, void *, const void *);

template <typename Build, typename Ours>
Stats RunHandler(difftest::Context &ctx, const char *name, uint32_t rva, uint32_t seeds, Build build, Ours ours, bool compareRet = true)
{
    auto original = ctx.Original<OrigFn>(rva);
    Stats st;
    Compare(ctx, name, seeds, build, [&](uint8_t *o, void *f, const void *p) { return original(o, f, p); },
            [&](uint8_t *o, void *f, const void *p) { return ours(o, f, static_cast<const PrimParamView *>(p)); }, st, compareRet);
    return st;
}

uint32_t Pick(std::mt19937 &r, std::initializer_list<uint32_t> values)
{
    return *(values.begin() + r() % values.size());
}

void PutWord(World &w, size_t off, uint16_t v)
{
    std::memcpy(w.param + off, &v, 2);
}

// ---- redirecting original functions (the callee runs the same stand-in on both sides) --------------------------------

struct Redirect {
    uint8_t *site;
    uint8_t saved[5];
    Redirect(difftest::Context &ctx, uint32_t rva, void *stand_in) : site(ctx.image.At<uint8_t>(rva))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stand_in) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    Redirect(const Redirect &) = delete;
    Redirect &operator=(const Redirect &) = delete;
    ~Redirect() { std::memcpy(site, saved, 5); }
};

// A game global (a pointer cell) set for the duration of a test.
struct GlobalCell {
    uint32_t *cell;
    uint32_t saved;
    GlobalCell(difftest::Context &ctx, uint32_t rva) : cell(ctx.image.At<uint32_t>(rva)), saved(*cell) {}
    GlobalCell(const GlobalCell &) = delete;
    GlobalCell &operator=(const GlobalCell &) = delete;
    void Set(const void *p) { *cell = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
    ~GlobalCell() { *cell = saved; }
};

// ---- the allocator service ------------------------------------------------------------------------------------------------
// Ours reaches the game's allocator service through its function-local static (service 0x10c95d4, guard 0x10c95d8);
// the original's vector helpers read it through the game's thread-local epoch, which the harness doesn't have, so they
// are redirected to stand-ins with the same effect (the CRT's malloc and free).

struct AllocObject {
    void **vt;
};
void *gAllocVt[8];
AllocObject gAllocObject{gAllocVt};

void *__fastcall AllocSlot3(void *, void *, uint32_t bytes)
{
    return std::malloc(bytes ? bytes : 1);
}
uint32_t __fastcall AllocSlot4(void *, void *, void *p)
{
    std::free(p);
    return 0;
}
void *__fastcall Alloc5a9830(void *, void *, uint32_t count, uint32_t)
{
    return std::malloc(count ? count * 0x14 : 1);
}
void __fastcall Free5a9ab0(void *, void *, void *p, uint32_t)
{
    std::free(p);
}
void *__cdecl Alloc194be0(void *, const uint32_t *count)
{
    return std::malloc(*count ? *count * 2 : 1);
}
void __stdcall Free197100(void *p, uint32_t)
{
    std::free(p);
}

struct AllocatorFixture {
    GlobalCell service, guard;
    Redirect a, b, c, d;
    explicit AllocatorFixture(difftest::Context &ctx)
        : service(ctx, 0x10c95d4), guard(ctx, 0x10c95d8), a(ctx, 0x5a9830, reinterpret_cast<void *>(&Alloc5a9830)),
          b(ctx, 0x5a9ab0, reinterpret_cast<void *>(&Free5a9ab0)), c(ctx, 0x194be0, reinterpret_cast<void *>(&Alloc194be0)),
          d(ctx, 0x197100, reinterpret_cast<void *>(&Free197100))
    {
        gAllocVt[3] = reinterpret_cast<void *>(&AllocSlot3);
        gAllocVt[4] = reinterpret_cast<void *>(&AllocSlot4);
        service.Set(&gAllocObject);
        *guard.cell = 0x80000000u; // "initialised": not above any thread's epoch
    }
};

// ---- the string-table loader's resource manager (tests/ts_string_fake.h) -------------------------------------------------------
//
// The loader (cTSString, 0x1d0df0) runs for real on both sides; the fake resource manager logs the key it is asked for and answers
// with the next scripted text, so the log and the texts are the same on both sides.

std::vector<std::string> gLoaderTexts;
size_t gLoaderPos = 0;

struct LoaderFixture {
    tsfake::ManagerFixture manager;
    explicit LoaderFixture(difftest::Context &ctx) : manager(ctx)
    {
        manager.InstallManagerOnly();
        tsfake::gAnswerer = [](const tsfake::Ask &ask, std::string &text) {
            gTrace.push_back(0x10adu);
            gTrace.insert(gTrace.end(), {ask.instance >> 16, ask.instance & 0xffff, Norm(ask.group), static_cast<uint32_t>(ask.loadFlags) & 0xff, ask.type});
            text = gLoaderPos < gLoaderTexts.size() ? gLoaderTexts[gLoaderPos++] : std::string();
            return true;
        };
    }
};

} // namespace
