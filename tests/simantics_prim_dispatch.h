#pragma once
// Shared pieces of the lane V tests (src/simantics/PrimDispatch.h): recording fake objects, a scripted world and the
// image patcher that redirects the original's callees (handlers, base dispatch, base reader) to stubs shared with
// our side. Both implementations run on the same world; every virtual call and every handler call is logged, and the
// two logs, results and exceptions are compared.
#include "difftest.h"
#include "simantics/PrimDispatch.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <utility>
#include <vector>

namespace pd {

using std::uint16_t;
using std::uint32_t;
using std::uint8_t;

constexpr int kSlots = 400;

// Object ids of the fake world (the trace records ids, not addresses).
enum : uint32_t {
    kSelf = 0,
    kNode = 1,
    kFrame = 2,
    kIface = 3,   // the interface the QueryInterface returns
    kStep = 4,    // what the interface's slot 6 returns
    kTarget = 5,  // what the step's slot 11 returns
    kOther = 6,   // spare
    kObjectCount = 8,
    kPersonObject = 16, // the person-read world: the object at S - 4
    kHolder2 = 17,      // S + 0x2ec
    kPersonIface = 18,  // the pointer at S + 0x44
    kRunHolder = 19,    // what the person interface's slot 63 returns
};

// The argument count of each slot as the code under test calls it (the callee pops them).
constexpr int Arity(int slot)
{
    switch (slot) {
    case 0: return 2;   // QueryInterface(iid, &out)
    case 19: return 1;  // (id)
    case 53: case 54: case 55: return 2; // (id, extra)
    case 246: return 2; // (frame, node)
    case 361: case 362: return 2; // (frame, param)
    default: return 0;
    }
}

// ---- the world ------------------------------------------------------------------------------------------------------

struct Config {
    uint32_t seed = 0;
    uint32_t opcodeWord = 0;      // what node slot 3 returns (eax; only ax is meaningful)
    void *param = nullptr;        // what node slot 16 returns
    uint32_t slot104 = 0;         // self slot 104
    void *iface = nullptr;        // the QueryInterface result written to the out pointer
    void *step = nullptr;         // iface slot 6
    void *target = nullptr;       // step slot 11
    uint32_t slot64 = 0;          // target slot 64
    bool throwIn246 = false;      // target slot 246 throws
    uint32_t tagThrow = 0;        // 246 throw code
    uint32_t frameWord = 0;       // frame slot 12 (person dispatch)
    int nodeThrows = 0;           // 3 or 16: that slot of the node throws 0x333
    // person-read world
    uint32_t slot98 = 0;
    uint32_t slot19 = 0;
    void *tableMem = nullptr;     // holder2 slot 12
    uint32_t slot20 = 0;
    void *runs[3] = {nullptr, nullptr, nullptr}; // run holder slots 53, 54, 55
};

inline Config gCfg;
inline std::vector<uint32_t> gTrace;
inline void *gObjects[32]; // id -> address (object ids above)

inline uint32_t Mix(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u) * 0x85ebca6bu ^ (c + 0x165667b1u) * 0xc2b2ae35u;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    h *= 0x297a2d39u;
    h ^= h >> 15;
    return h;
}

inline uint32_t IdOf(const void *p)
{
    for (uint32_t i = 0; i < 32; ++i)
        if (gObjects[i] == p) return i;
    return 0xffffu;
}

// The value a slot answers with. Unscripted slots give a hash of (seed, object, slot).
inline uint32_t Answer(uint32_t id, int slot, const uint32_t *args)
{
    const Config &c = gCfg;
    const uint32_t dflt = Mix(c.seed, id, static_cast<uint32_t>(slot));
    switch (id) {
    case kSelf:
        if (slot == 104) return c.slot104;
        if (slot == 0) {
            *reinterpret_cast<void **>(args[1]) = c.iface;
            return dflt;
        }
        return dflt;
    case kNode:
        if (slot == c.nodeThrows) throw static_cast<short>(0x333);
        if (slot == 3) return c.opcodeWord;
        if (slot == 16) return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(c.param));
        return dflt;
    case kFrame:
        if (slot == 12) return c.frameWord;
        return dflt;
    case kIface:
        if (slot == 6) return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(c.step));
        return dflt;
    case kStep:
        if (slot == 11) return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(c.target));
        return dflt;
    case kTarget:
        if (slot == 64) return c.slot64;
        if (slot == 246 && c.throwIn246) throw static_cast<short>(c.tagThrow);
        return dflt;
    case kPersonObject:
        if (slot == 98) return c.slot98;
        if (slot == 19) return c.slot19;
        return dflt;
    case kHolder2:
        if (slot == 12) return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(c.tableMem));
        if (slot == 20) return c.slot20;
        return dflt;
    case kPersonIface:
        if (slot == 63) return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gObjects[kRunHolder]));
        return dflt;
    case kRunHolder:
        if (slot == 53) return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(c.runs[0]));
        if (slot == 54) return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(c.runs[1]));
        if (slot == 55) return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(c.runs[2]));
        return dflt;
    default:
        return dflt;
    }
}

inline uint32_t Hit(void *self, int slot, int n, const uint32_t *a)
{
    const uint32_t id = IdOf(self);
    gTrace.push_back(id);
    gTrace.push_back(static_cast<uint32_t>(slot) << 8 | static_cast<uint32_t>(n));
    // The QueryInterface out pointer is a stack address of the caller: only the interface id is recorded.
    const int recorded = (slot == 0 && id == kSelf) ? 1 : n;
    for (int i = 0; i < recorded; ++i) gTrace.push_back(a[i]);
    return Answer(id, slot, a);
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

inline void *gVtable[kSlots];

template <int... S> void FillVtable(std::integer_sequence<int, S...>)
{
    ((gVtable[S] = reinterpret_cast<void *>(&Th<S, Arity(S)>::F)), ...);
}

inline void EnsureVtable()
{
    static bool done = false;
    if (!done) {
        FillVtable(std::make_integer_sequence<int, kSlots>());
        done = true;
    }
}

// ---- stubs for the original's callees --------------------------------------------------------------------------------

// A handler stand-in: logs (0x1000 + tag, this, frame, param) and answers with a hash, or throws.
struct HandlerMode {
    bool throws = false;
};
inline HandlerMode gHandlerMode;

inline uint32_t HandlerBody(int tag, void *self, void *frame, void *param)
{
    gTrace.push_back(0x1000u + static_cast<uint32_t>(tag));
    gTrace.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self)));
    gTrace.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(frame)));
    gTrace.push_back(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(param)));
    if (gHandlerMode.throws) throw static_cast<short>(0x700 + tag);
    return Mix(gCfg.seed, static_cast<uint32_t>(tag), 77);
}

template <int I> uint32_t __fastcall HandlerStub(void *self, void *, void *frame, void *param)
{
    return HandlerBody(I, self, frame, param);
}
template <int I> uint32_t HandlerFake(void *self, void *frame, void *param)
{
    return HandlerBody(I, self, frame, param);
}

constexpr int kStubCount = 0x7f;
using StubFn = uint32_t(__fastcall *)(void *, void *, void *, void *);
inline StubFn gStubs[kStubCount];
inline PrimHandlerFn gFakes[kStubCount];

template <int... I> void FillStubs(std::integer_sequence<int, I...>)
{
    ((gStubs[I] = &HandlerStub<I>, gFakes[I] = &HandlerFake<I>), ...);
}

inline void EnsureStubs()
{
    static bool done = false;
    if (!done) {
        FillStubs(std::make_integer_sequence<int, kStubCount>());
        done = true;
    }
}

// Overwrites the first five bytes of an image function with a jump to `target`; restores them on destruction.
struct ImagePatch {
    uint8_t *site;
    uint8_t saved[5];
    ImagePatch(difftest::Context &ctx, uint32_t rva, void *target) : site(ctx.image.At<uint8_t>(rva))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    ~ImagePatch() { std::memcpy(site, saved, 5); }
    ImagePatch(const ImagePatch &) = delete;
    ImagePatch &operator=(const ImagePatch &) = delete;
};

// ---- outcomes ------------------------------------------------------------------------------------------------------------

struct Outcome {
    uint32_t ret = 0;
    int32_t result = 0;
    int exception = -1; // the thrown 16-bit code, or -1; 0x10000 + n for another exception
    std::vector<uint32_t> trace;
    std::vector<uint8_t> extra; // anything else the case wants compared
};

inline bool Same(const Outcome &a, const Outcome &b)
{
    return a.ret == b.ret && a.result == b.result && a.exception == b.exception && a.trace == b.trace && a.extra == b.extra;
}

inline void Describe(const Outcome &a, const Outcome &b, char *out, size_t n)
{
    size_t i = 0;
    while (i < a.trace.size() && i < b.trace.size() && a.trace[i] == b.trace[i]) ++i;
    std::snprintf(out, n, "ret %08x/%08x result %d/%d exc %d/%d trace %zu/%zu first diff at %zu (%08x/%08x) extra %s", a.ret, b.ret,
                  a.result, b.result, a.exception, b.exception, a.trace.size(), b.trace.size(), i, i < a.trace.size() ? a.trace[i] : 0,
                  i < b.trace.size() ? b.trace[i] : 0, a.extra == b.extra ? "same" : "differ");
}

template <typename F> Outcome Run(F &&body)
{
    Outcome o;
    gTrace.clear();
    try {
        body(o);
    } catch (short c) {
        o.exception = static_cast<uint16_t>(c);
    } catch (...) {
        o.exception = 0x10000;
    }
    o.trace = gTrace;
    return o;
}

} // namespace pd
