#pragma once
// Harness of the lane Y person primitive tests (src/simantics/PrimPerson.h): recording fakes in the style of
// tests/simantics_prims_w.h, with a person world big enough for the whole object (the handlers touch fields up to +0x8f4)
// and stand-ins for the original helper functions the handlers still call (the mapped image is patched while a test runs,
// so the original and ours reach the same recording stand-in).
//
// Every object the handler reaches is a Fake (a vtable pointer) or one of the person's embedded sub-objects (this, this+4,
// this+0x2f0, this+0x60c, which keep their vtable in the object's bytes): each call is logged (object, slot, arguments) and
// answers from a script (or a hash of the seed). Pointers into the world are logged as offsets; pointers into the handler's
// own stack (out parameters) as presence only. The original and ours run on two worlds built from one seed and are compared
// on the trace, the thrown code, the return value and the memory the handler may write (the object and the scratch area).
#include "difftest.h"
#include "game/game.h"
#include "simantics/PrimPerson.h"

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
constexpr int kSl = 256;
constexpr int kObjSize = 0x900;
constexpr int kScratch = 0x400;

struct WorldP {
    alignas(16) uint8_t obj[kObjSize];
    void *vtThis[kSl];
    void *vtTree[kSl];
    void *vtPerson[kSl];
    void *vt60c[kSl];
    Fake fk[kFk];
    void *vtFk[kFk][kSl];
    alignas(16) uint8_t scratch[kScratch];
    uint8_t param[0x20];
    uint32_t canary;
};

// Fake indexes shared by the tests (each test assigns its own meaning, see its Build function).
enum : int { kF0, kF1, kF2, kF3, kF4, kF5, kF6, kF7, kF8, kF9, kF10, kF11, kF12, kF13, kF14, kF15, kF16, kF17, kF18, kF19 };

// ---- script ---------------------------------------------------------------------------------------------------------

struct RetEntry {
    uint32_t id;
    int slot;
    bool isObject;
    uint32_t value; // the value, or the offset of a world object (0xffffffff: null)
};

WorldP *gW = nullptr;
uint32_t gSeed = 0;
WorldP &SharedWorld();
// Test-only (PERSON_INJECT_WORLD=1 in the environment): one in eight unscripted fake answers and one in eight FillRandom
// dwords become a word inside the world (its address plus the random word folded into its size), the case that used to
// need the high placement and the retries (see SharedWorld).
bool gInjectWorld = std::getenv("PERSON_INJECT_WORLD") != nullptr;
uint32_t gInjected = 0; // words injected (both sides), printed by RunSeeds
uint32_t InjectWorld(uint32_t v)
{
    if (!gInjectWorld || (v >> 29) != 0) return v;
    ++gInjected;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&SharedWorld())) + v % static_cast<uint32_t>(sizeof(WorldP));
}
std::vector<RetEntry> gRets;           // fixed answers
std::vector<std::vector<RetEntry>> gQ; // per-call answers, consumed in call order (one list per object/slot)
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

constexpr uint32_t kOutMarker = 0xfff00001u;

bool OnStack(uint32_t v)
{
    const NT_TIB *tib = reinterpret_cast<const NT_TIB *>(NtCurrentTeb());
    return v >= reinterpret_cast<uintptr_t>(tib->StackLimit) && v < reinterpret_cast<uintptr_t>(tib->StackBase);
}

// Addresses inside the world become offsets (the two worlds sit at different addresses), the handler's own stack becomes a
// marker; everything else is logged as it is.
uint32_t Norm(uint32_t v)
{
    const uintptr_t base = reinterpret_cast<uintptr_t>(gW);
    if (gW && v >= base && v < base + sizeof(WorldP)) return 0x40000000u + static_cast<uint32_t>(v - base);
    if (OnStack(v)) return kOutMarker;
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

uint32_t OffsetIn(const void *p, const WorldP &w)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(&w));
}

uint32_t Resolve(const RetEntry &e)
{
    if (!e.isObject) return e.value;
    return e.value == 0xffffffffu ? 0 : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gW) + e.value);
}

void RetValue(const void *obj, int slot, uint32_t value, WorldP &w)
{
    gRets.push_back({OffsetIn(obj, w), slot, false, value});
}
void RetObject(const void *obj, int slot, const void *target, WorldP &w)
{
    gRets.push_back({OffsetIn(obj, w), slot, true, target ? OffsetIn(target, w) : 0xffffffffu});
}

void Enqueue(const void *obj, int slot, bool isObject, uint32_t value, WorldP &w)
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
void EnqueueObject(const void *obj, int slot, const void *target, WorldP &w)
{
    Enqueue(obj, slot, true, target ? OffsetIn(target, w) : 0xffffffffu, w);
}
void EnqueueValue(const void *obj, int slot, uint32_t value, WorldP &w)
{
    Enqueue(obj, slot, false, value, w);
}

// The answer for (object, slot): the per-call queue, then the fixed answer, then a hash of the seed.
uint32_t Scripted(uint32_t id, int slot)
{
    for (size_t i = 0; i < gQ.size(); ++i) {
        const std::vector<RetEntry> &q = gQ[i];
        if (q[0].id == id && q[0].slot == slot && gQPos[i] < q.size()) return Resolve(q[gQPos[i]++]);
    }
    for (const RetEntry &e : gRets) {
        if (e.id == id && e.slot == slot) return Resolve(e);
    }
    return InjectWorld(Mix(gSeed, id, static_cast<uint32_t>(slot)));
}

// The generic hit: logs the call, ticks (a seed may throw here), answers from the script. Release (slot 2) runs while errors
// unwind and must not throw.
uint32_t Hit(void *self, int slot, int n, const uint32_t *a)
{
    if (slot != 2) Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(static_cast<uint32_t>(slot) << 8 | static_cast<uint32_t>(n));
    for (int i = 0; i < n; ++i) gTrace.push_back(Norm(a[i]));
    return Scripted(Off(self), slot);
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
template <int S> struct Th<S, 5> {
    static uint32_t __fastcall F(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
    {
        const uint32_t a[] = {a1, a2, a3, a4, a5};
        return Hit(self, S, 5, a);
    }
};
template <int S> struct Th<S, 6> {
    static uint32_t __fastcall F(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
    {
        const uint32_t a[] = {a1, a2, a3, a4, a5, a6};
        return Hit(self, S, 6, a);
    }
};
template <int S> struct Th<S, 7> {
    static uint32_t __fastcall F(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6,
                                 uint32_t a7)
    {
        const uint32_t a[] = {a1, a2, a3, a4, a5, a6, a7};
        return Hit(self, S, 7, a);
    }
};

void *gThunk[kSl][8];

template <int... S> void FillThunks(std::integer_sequence<int, S...>)
{
    ((gThunk[S][0] = reinterpret_cast<void *>(&Th<S, 0>::F), gThunk[S][1] = reinterpret_cast<void *>(&Th<S, 1>::F),
      gThunk[S][2] = reinterpret_cast<void *>(&Th<S, 2>::F), gThunk[S][3] = reinterpret_cast<void *>(&Th<S, 3>::F),
      gThunk[S][4] = reinterpret_cast<void *>(&Th<S, 4>::F), gThunk[S][5] = reinterpret_cast<void *>(&Th<S, 5>::F),
      gThunk[S][6] = reinterpret_cast<void *>(&Th<S, 6>::F), gThunk[S][7] = reinterpret_cast<void *>(&Th<S, 7>::F)),
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

// ---- helper stand-ins (the original functions the handlers still call) ----------------------------------------------

// A helper's id in the trace: its RVA with the top bit set. Arguments are logged normalised; the answer is scripted by
// (id, 0).
uint32_t HelperHit(uint32_t rva, void *self, int n, const uint32_t *a)
{
    Tick();
    gTrace.push_back(0x80000000u | rva);
    gTrace.push_back(self ? Norm(reinterpret_cast<uint32_t>(self)) : 0u);
    for (int i = 0; i < n; ++i) gTrace.push_back(Norm(a[i]));
    return Scripted(0x80000000u | rva, 0);
}

template <uint32_t RVA> struct HelperThis0 {
    static uint32_t __fastcall F(void *self, void *) { return HelperHit(RVA, self, 0, nullptr); }
};
template <uint32_t RVA> struct HelperThis1 {
    static uint32_t __fastcall F(void *self, void *, uint32_t a1)
    {
        const uint32_t a[] = {a1};
        return HelperHit(RVA, self, 1, a);
    }
};
template <uint32_t RVA> struct HelperThis5 {
    static uint32_t __fastcall F(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
    {
        const uint32_t a[] = {a1, a2, a3, a4, a5};
        return HelperHit(RVA, self, 5, a);
    }
};

// Redirects a function of the mapped image to a stand-in (a jmp over its first five bytes) while it lives.
struct CodePatch {
    uint8_t *site;
    uint8_t saved[5];
    CodePatch(difftest::Context &ctx, uint32_t rva, void *stand) : CodePatch(ctx.image.At<uint8_t>(rva), stand) {}
    // The same on our own code (a replacement the code under test reaches directly, not through the image), so a
    // callee's stand-in serves both sides of the caller's test.
    CodePatch(void *ours, void *stand) : site(static_cast<uint8_t *>(ours))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stand) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    CodePatch(const CodePatch &) = delete;
    ~CodePatch() { std::memcpy(site, saved, 5); }
};

// ---- values ---------------------------------------------------------------------------------------------------------

uint32_t NoiseAl(std::mt19937 &r, bool nonzero)
{
    const uint32_t high = r() & ~0xffu;
    return nonzero ? (high | (1 + r() % 255)) : high;
}

uint32_t Edge16(std::mt19937 &r)
{
    const int16_t e[] = {0, 1, -1, 2, 3, 7, 100, 32767, -32768, 12345, -12345};
    const int16_t v = (r() % 3 == 0) ? static_cast<int16_t>(r()) : e[r() % 11];
    return static_cast<uint32_t>(static_cast<uint16_t>(v)) | (r() % 4 == 0 ? (r() & 0xffff0000u) : 0u);
}

// Random bytes for a block whose dwords are compared: the top bit of every dword is set, so no random word equals an address
// of the (user-space) world (which once mattered: see SharedWorld). Under PERSON_INJECT_WORLD some dwords are world words.
void FillRandom(uint8_t *p, size_t n, std::mt19937 &r)
{
    for (size_t i = 0; i < n; ++i) p[i] = static_cast<uint8_t>(r());
    for (size_t i = 3; i < n; i += 4) p[i] |= 0x80;
    if (gInjectWorld) {
        for (size_t i = 0; i + 4 <= n; i += 4) {
            uint32_t v;
            std::memcpy(&v, p + i, 4);
            if (((v >> 28) & 7) != 0) continue; // one in eight
            v = InjectWorld(v & 0x0fffffffu);
            std::memcpy(p + i, &v, 4);
        }
    }
}

// ---- run ------------------------------------------------------------------------------------------------------------

struct Outcome {
    uint32_t ret = 0;
    int thrown = -1;
    std::vector<uint32_t> trace;
    std::vector<uint32_t> state;
};

std::function<void(WorldP &)> gBeforeRun;

// The object's and the scratch area's dwords, with world addresses normalised.
void Capture(WorldP &w, Outcome &o)
{
    const uint32_t *words = reinterpret_cast<const uint32_t *>(w.obj);
    for (size_t i = 0; i < kObjSize / 4; ++i) o.state.push_back(Norm(words[i]));
    const uint32_t *s = reinterpret_cast<const uint32_t *>(w.scratch);
    for (size_t i = 0; i < kScratch / 4; ++i) o.state.push_back(Norm(s[i]));
    o.state.push_back(w.canary);
}

// A crash inside a run is reported (not swallowed): SEH needs a function without unwindable objects.
DWORD gCrashCode = 0;
int GuardedInvoke(void (*fn)(void *), void *arg)
{
    __try {
        fn(arg);
        return 0;
    } __except (gCrashCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER) {
        return 1;
    }
}

template <typename Call> Outcome Run(WorldP &w, Call call)
{
    gW = &w;
    for (size_t &p : gQPos) p = 0;
    gHitCount = 0;
    gTrace.clear();
    if (gBeforeRun) gBeforeRun(w);
    Outcome o;
    struct Frame {
        Call *call;
        WorldP *w;
        Outcome *o;
    } frame{&call, &w, &o};
    const int crashed = GuardedInvoke(
        [](void *p) {
            Frame *f = static_cast<Frame *>(p);
            try {
                f->o->ret = (*f->call)(*f->w);
            } catch (short code) {
                f->o->thrown = code;
            }
        },
        &frame);
    if (crashed) {
        o.thrown = 0x7000 + static_cast<int>(gCrashCode & 0xfff);
        std::fprintf(stderr, "crash 0x%lx after %zu trace words\n", gCrashCode, gTrace.size());
        for (size_t i = 0; i < gTrace.size() && i < 400; ++i) std::fprintf(stderr, "%x ", gTrace[i]);
        std::fprintf(stderr, "\n");
    }
    o.trace = gTrace;
    Capture(w, o);
    gW = nullptr;
    return o;
}

struct Trial {
    bool bad = false;
    Outcome oa, ob;
};

// Builds the one world (see SharedWorld) from the seed and runs ours on it, then builds it again from the seed and runs the
// original, so the world afterwards is the original's (what RunSeeds' `stat` reads, as when the original had a world of its
// own). `skipRet`: true on the paths where the original leaves the caller's eax untouched (undefined result).
template <typename Build, typename Orig, typename Ours>
Trial Try(WorldP &w, uint32_t seed, Build build, Orig original, Ours ours, bool (*skipRet)(const Outcome &) = nullptr)
{
    Trial t;
    std::memset(&w, 0, sizeof(WorldP)); // (each build starts from zeroed memory, as a freshly committed world did)
    build(w, seed);
    t.ob = Run(w, [&](WorldP &x) { return ours(x.obj, &x.fk[0], x.param); });
    std::memset(&w, 0, sizeof(WorldP));
    build(w, seed);
    t.oa = Run(w, [&](WorldP &x) { return original(x.obj, &x.fk[0], x.param); });
    const bool ignoreRet = skipRet && skipRet(t.oa);
    t.bad = t.oa.thrown != t.ob.thrown || t.oa.trace != t.ob.trace || t.oa.state != t.ob.state ||
            (t.oa.thrown < 0 && !ignoreRet && t.oa.ret != t.ob.ret);
    return t;
}

// Fills the person object's vtables and the fakes' vtable pointers.
void InitPerson(WorldP &w)
{
    auto *self = reinterpret_cast<PrimPersonObject *>(w.obj);
    self->mVTable = w.vtThis;
    self->mTreeSimVTable = w.vtTree;
    self->mPersonVTable = w.vtPerson;
    self->mObject60cVTable = w.vt60c;
    for (int i = 0; i < kFk; ++i) w.fk[i].vt = w.vtFk[i];
}

void ResetScript(uint32_t seed, std::mt19937 &r)
{
    EnsureThunks();
    gRets.clear();
    gQ.clear();
    gQPos.clear();
    gSeed = seed;
    gThrowAt = (r() % 6 == 0) ? 1 + r() % 40 : 0;
}

// Where the world lives. Both sides run in this one world, rebuilt from the seed before each run (see Try), so they see it at
// the same address (as in tests/simantics_prims_aa.h, lane EH). Norm() turns every word that falls inside the world into an
// offset; with two worlds at different addresses, a word whose bits equal an address inside one world but not the other (same
// bits, different Norm) made the two states differ although nothing diverged. Random words could do that (FillRandom sets their
// top bit for that reason, and the fakes' Mix answers are raw), and so could floats: ordinary values (0.01 .. 1000, the grid
// steps, sums of them) have bit patterns 0x3c000000..0x447a0000, where the process heap sits once earlier tests have grown it,
// and ~2% of the seeds of a heap world differed by chance. The old fix (worlds placed high, plus up to six retries on fresh
// worlds) made it rare, not impossible. At one address Norm is the same function on both sides, so equal words stay equal and
// different words stay different: no comparison is weakened (a word the original logs as a world pointer must still be that
// same pointer in ours), and none depends on where the world sits. The world is still placed high (below 2 GB), which keeps
// the dumps readable (fewer ordinary floats print as world offsets) but no longer matters for the verdict.
WorldP &SharedWorld()
{
    static WorldP *const w = [] {
        void *p = nullptr;
        for (uintptr_t at = 0x7a000000u; at >= 0x50000000u && !p; at -= 0x10000)
            p = VirtualAlloc(reinterpret_cast<void *>(at), sizeof(WorldP), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!p) p = VirtualAlloc(nullptr, sizeof(WorldP), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!p) {
            std::fprintf(stderr, "no address space for the test world\n");
            std::abort();
        }
        return static_cast<WorldP *>(p);
    }();
    return *w;
}

// Runs `seeds` seeds, prints failures through ctx and returns the outcomes' statistics via `stat` (given the world as the
// original left it). A mismatch is reported as it is, without a retry.
template <typename Build, typename Orig, typename Ours, typename Stat>
void RunSeeds(difftest::Context &ctx, uint32_t seeds, Build build, Orig original, Ours ours, Stat stat, bool (*skipRet)(const Outcome &) = nullptr)
{
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        WorldP &w = SharedWorld();
        Trial t = Try(w, seed, build, original, ours, skipRet);
        ++ctx.cases;
        stat(w, t);
        if (t.bad) {
            const Outcome &oa = t.oa, &ob = t.ob;
            size_t k = 0;
            while (k < oa.trace.size() && k < ob.trace.size() && oa.trace[k] == ob.trace[k]) ++k;
            if (std::getenv("PERSON_TRACE") && ctx.failures.size() < 2) {
                for (const Outcome *o : {&oa, &ob}) {
                    std::fprintf(stderr, "trace:");
                    for (size_t i = k > 24 ? k - 24 : 0; i < o->trace.size() && i < k + 6; ++i) std::fprintf(stderr, " %x", o->trace[i]);
                    std::fprintf(stderr, "\n");
                }
            }
            size_t s = 0;
            while (s < oa.state.size() && s < ob.state.size() && oa.state[s] == ob.state[s]) ++s;
            ctx.Fail("seed %u: (original/ours) thrown %d/%d ret 0x%x/0x%x trace %zu/%zu (first diff at %zu: 0x%x/0x%x) state %s (first diff %zu: 0x%x/0x%x)", seed,
                     oa.thrown, ob.thrown, oa.ret, ob.ret, oa.trace.size(), ob.trace.size(), k, k < oa.trace.size() ? oa.trace[k] : 0xdeadu,
                     k < ob.trace.size() ? ob.trace[k] : 0xdeadu, oa.state == ob.state ? "same" : "DIFFERENT", s,
                     s < oa.state.size() ? oa.state[s] : 0xdeadu, s < ob.state.size() ? ob.state[s] : 0xdeadu);
        }
    }
    if (gInjectWorld) std::printf("      PERSON_INJECT_WORLD: %u world-range words injected\n", gInjected);
    gInjected = 0;
}

using OrigFn = uint32_t(__thiscall *)(void *, void *, const void *);

} // namespace
