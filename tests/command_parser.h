#pragma once
// Shared fakes for the CommandParser differential tests (tests/command_parser*.cpp).
//
// The commands call through the vtables of the objects they are given (the argument list, the
// parser, the parser state and its output object). A FakeVTable has recording fakes in its slots:
// each call appends an Event (which vtable, which slot, `this`, up to four stack arguments) to gLog
// and returns the value the test configured for that slot (ret[slot]). Any memory whose first word
// is `slots` is an object of that fake class, so the parser state can be a big block of random bytes
// with a fake vtable pointer at +0.
//
// The original and the replacement run on the same objects: Differential() snapshots the memory
// regions the command can touch, runs the original, records the log and the memory, restores the
// snapshot, runs the replacement and compares logs, memory and result. The fakes are __fastcall
// (ecx = this, edx unused, the stack arguments popped by the callee), which is the same register
// and stack layout as __thiscall.
#include "difftest.h"
#include "free_recorder.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace cptest {

struct Event {
    int object; // FakeVTable::id of the object the call was made on (-1: the command itself)
    int slot;
    uint32_t self;
    uint32_t args[4];
    bool operator==(const Event &o) const
    {
        return object == o.object && slot == o.slot && self == o.self && std::memcmp(args, o.args, sizeof(args)) == 0;
    }
};

constexpr int kSlots = 64;

inline std::vector<Event> gLog;
// Bytes a fake took from its arguments while it was called (a buffer the command filled in): the
// original and the replacement must have passed the same ones.
inline std::vector<uint8_t> gExtra;

uint32_t Record(void *self, int slot, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0);

template <int S> uint32_t __fastcall Fn0(void *self, void *) { return Record(self, S); }
template <int S> uint32_t __fastcall Fn1(void *self, void *, uint32_t a) { return Record(self, S, a); }
template <int S> uint32_t __fastcall Fn2(void *self, void *, uint32_t a, uint32_t b) { return Record(self, S, a, b); }
template <int S> uint32_t __fastcall Fn3(void *self, void *, uint32_t a, uint32_t b, uint32_t c) { return Record(self, S, a, b, c); }

struct FakeVTable {
    int id = 0;
    uint32_t ret[kSlots] = {};
    void *slots[kSlots];

    // Declares that slot S is called with `arity` stack arguments (callee-popped).
    template <int S> void Set(int arity)
    {
        switch (arity) {
        case 0: slots[S] = reinterpret_cast<void *>(&Fn0<S>); break;
        case 1: slots[S] = reinterpret_cast<void *>(&Fn1<S>); break;
        case 2: slots[S] = reinterpret_cast<void *>(&Fn2<S>); break;
        default: slots[S] = reinterpret_cast<void *>(&Fn3<S>); break;
        }
    }

    template <int... Ss> void SetAll0(std::integer_sequence<int, Ss...>) { (Set<Ss>(0), ...); }

    explicit FakeVTable(int vtableId)
    {
        id = vtableId;
        SetAll0(std::make_integer_sequence<int, kSlots>());
    }
    FakeVTable(const FakeVTable &) = delete;
    FakeVTable &operator=(const FakeVTable &) = delete;

    void *Slots() { return slots; }
    void ClearRet() { std::memset(ret, 0, sizeof(ret)); }
};

inline uint32_t Record(void *self, int slot, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    void **slots = *static_cast<void ***>(self);
    auto *vt = reinterpret_cast<FakeVTable *>(reinterpret_cast<char *>(slots) - offsetof(FakeVTable, slots));
    difftest::NotCounted bookkeeping; // the log is the test's (difftest::AllocationCounter)
    gLog.push_back({vt->id, slot, reinterpret_cast<uint32_t>(self), {a0, a1, a2, a3}});
    return vt->ret[slot];
}

// The vtable of the object under test itself (calls the command makes on `this`: AddRef, the
// deleting destructor). They log with object id -1 and return gSelfRet.
inline uint32_t gSelfRet = 0;
template <int S> uint32_t __fastcall SelfFn0(void *self, void *)
{
    difftest::NotCounted bookkeeping;
    gLog.push_back({-1, S, reinterpret_cast<uint32_t>(self), {0, 0, 0, 0}});
    return gSelfRet;
}
template <int S> uint32_t __fastcall SelfFn1(void *self, void *, uint32_t a)
{
    difftest::NotCounted bookkeeping;
    gLog.push_back({-1, S, reinterpret_cast<uint32_t>(self), {a, 0, 0, 0}});
    return gSelfRet;
}
struct SelfVTable {
    void *slots[16];
    SelfVTable()
    {
        for (auto &s : slots) s = nullptr;
        slots[1] = reinterpret_cast<void *>(&SelfFn0<1>);
        slots[2] = reinterpret_cast<void *>(&SelfFn0<2>);
        slots[9] = reinterpret_cast<void *>(&SelfFn1<9>);
        slots[11] = reinterpret_cast<void *>(&SelfFn1<11>);
        slots[12] = reinterpret_cast<void *>(&SelfFn1<12>);
    }
};

// True when the two logs are the same call sequence. An address inside the object under test (its
// first 0x100 bytes; `baseA` for the first run, `baseB` for the second) is compared as an offset.
inline bool SameLogs(const std::vector<Event> &a, const void *baseA, const std::vector<Event> &b, const void *baseB)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const Event &x = a[i], &y = b[i];
        if (x.object != y.object || x.slot != y.slot) return false;
        auto norm = [](uint32_t v, const void *base) {
            uint32_t b = reinterpret_cast<uint32_t>(base);
            return v - b < 0x100 ? 0xBA5E0000u + (v - b) : v;
        };
        if (norm(x.self, baseA) != norm(y.self, baseB)) return false;
        for (int k = 0; k < 4; ++k)
            if (norm(x.args[k], baseA) != norm(y.args[k], baseB)) return false;
    }
    return true;
}

struct Region {
    void *p;
    size_t n;
};

// What a run produced besides memory and calls: the value in eax and whether it threw.
struct Outcome {
    uint32_t ret = 0;
    bool threw = false;
    std::string detail; // what was thrown (see Thrown), when it threw
    bool operator==(const Outcome &o) const { return ret == o.ret && threw == o.threw && detail == o.detail; }
};

// ---- C++ exceptions seen through SEH --------------------------------------------------------------
//
// The original throws with the game's own type information and ours with the compiler's. Both are
// caught as the SEH exception 0xE06D7363, and what was thrown is read from the exception record:
// the object (for a cError: the vtable word and the message) and the names and sizes of the types
// it can be caught as.
struct Thrown {
    uint32_t vtable = 0;
    uint32_t size = 0;
    char text[2300] = {};
    char types[300] = {};
};

inline int CaptureThrown(EXCEPTION_POINTERS *p, Thrown *t)
{
    const EXCEPTION_RECORD *r = p->ExceptionRecord;
    if (r->ExceptionCode != 0xE06D7363 || r->NumberParameters < 3) return EXCEPTION_CONTINUE_SEARCH;
    const uint32_t *obj = reinterpret_cast<const uint32_t *>(r->ExceptionInformation[1]);
    t->vtable = obj[0];
    uint32_t size = obj[5], cap = obj[6];
    const char *s = cap > 15 ? reinterpret_cast<const char *>(obj[1]) : reinterpret_cast<const char *>(obj + 1);
    t->size = size;
    for (uint32_t i = 0; i < size && i < sizeof(t->text) - 1; ++i) t->text[i] = s[i];
    const uint32_t *throwInfo = reinterpret_cast<const uint32_t *>(r->ExceptionInformation[2]);
    const uint32_t *types = reinterpret_cast<const uint32_t *>(throwInfo[3]);
    size_t used = 0;
    for (uint32_t i = 0; i < types[0]; ++i) {
        const uint32_t *ct = reinterpret_cast<const uint32_t *>(types[1 + i]);
        const char *name = reinterpret_cast<const char *>(ct[1]) + 8;
        used += std::snprintf(t->types + used, sizeof(t->types) - used, "%s:%x;", name, ct[5]);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// Calls fn(ctx); true when it threw a C++ exception, whose description goes to *t.
inline bool CallGuarded(void (*fn)(void *), void *ctx, Thrown *t)
{
    __try {
        fn(ctx);
        return false;
    } __except (CaptureThrown(GetExceptionInformation(), t)) {
        return true;
    }
}

// Runs f() (returning the value for eax, or void) and reports the result and any exception.
template <class F> Outcome RunGuarded(F f)
{
    struct Ctx {
        F *f;
        uint32_t ret;
    } c{&f, 0};
    Thrown t;
    bool threw = CallGuarded(
        [](void *p) {
            auto *cc = static_cast<Ctx *>(p);
            if constexpr (std::is_void_v<decltype((*cc->f)())>) (*cc->f)();
            else cc->ret = static_cast<uint32_t>((*cc->f)());
        },
        &c, &t);
    Outcome o;
    o.ret = c.ret;
    o.threw = threw;
    if (threw) {
        char buf[2800];
        std::snprintf(buf, sizeof(buf), "vtable %08x size %u text '%s' types %s", t.vtable, t.size, t.text, t.types);
        o.detail = buf;
    }
    return o;
}

inline std::string Describe(const Event &e)
{
    char buf[160];
    std::snprintf(buf, sizeof(buf), "obj%d.slot%d(this=%08x; %08x %08x %08x %08x)", e.object, e.slot, e.self, e.args[0], e.args[1],
                  e.args[2], e.args[3]);
    return buf;
}

// Text for a failure message with bytes outside printable ASCII as \xNN (the report is read as text).
inline std::string Printable(const std::string &s)
{
    std::string out;
    char buf[8];
    for (unsigned char c : s) {
        if (c >= 0x20 && c < 0x7f) {
            out += static_cast<char>(c);
        } else {
            std::snprintf(buf, sizeof(buf), "\\x%02x", c);
            out += buf;
        }
    }
    return out;
}

// Runs `runA` (the original) and `runB` (the replacement) from the same memory state and compares
// the call logs, the regions afterwards and the outcomes. Returns true when they agree.
// `after` (optional) runs right after each side's call and before the memory is captured: it may
// normalise memory that legitimately differs between the sides (a heap pointer inside a string).
// `before` (optional) runs before each side's call, after the regions are restored: it can rebuild
// memory the call is allowed to free (a vector's block).
template <class RunA, class RunB, class After = void (*)(), class Before = void (*)()>
bool Differential(difftest::Context &ctx, const char *what, const std::vector<Region> &regions, RunA runA, RunB runB,
                  After after = [] {}, Before before = [] {})
{
    std::vector<std::vector<uint8_t>> snap, afterA, afterB;
    for (const Region &r : regions) snap.emplace_back(static_cast<uint8_t *>(r.p), static_cast<uint8_t *>(r.p) + r.n);
    auto restore = [&] {
        for (size_t i = 0; i < regions.size(); ++i) std::memcpy(regions[i].p, snap[i].data(), regions[i].n);
    };
    auto capture = [&](std::vector<std::vector<uint8_t>> &into) {
        for (const Region &r : regions) into.emplace_back(static_cast<uint8_t *>(r.p), static_cast<uint8_t *>(r.p) + r.n);
    };
    before();
    gLog.clear();
    gExtra.clear();
    Outcome oa = runA();
    std::vector<Event> logA = gLog;
    after();
    std::vector<uint8_t> extraA = gExtra;
    capture(afterA);
    restore();
    before();
    gLog.clear();
    gExtra.clear();
    Outcome ob = runB();
    std::vector<Event> logB = gLog;
    after();
    std::vector<uint8_t> extraB = gExtra;
    capture(afterB);
    ++ctx.cases;
    bool ok = true;
    if (!(oa == ob)) {
        ctx.Fail("%s: result %08x%s vs %08x%s", what, oa.ret, oa.threw ? " (threw)" : "", ob.ret, ob.threw ? " (threw)" : "");
        if (oa.detail != ob.detail) {
            ctx.Fail("   original threw: %.200s", Printable(oa.detail).c_str());
            ctx.Fail("   ours threw:     %.200s", Printable(ob.detail).c_str());
        }
        ok = false;
    }
    size_t n = std::min(logA.size(), logB.size());
    for (size_t i = 0; i < n && ok; ++i) {
        if (!(logA[i] == logB[i])) {
            ctx.Fail("%s: call %zu differs: %s vs %s", what, i, Describe(logA[i]).c_str(), Describe(logB[i]).c_str());
            ok = false;
        }
    }
    if (ok && logA.size() != logB.size()) {
        ctx.Fail("%s: %zu calls vs %zu", what, logA.size(), logB.size());
        ok = false;
    }
    if (ok && extraA != extraB) {
        ctx.Fail("%s: data seen by the fakes differs (%zu vs %zu bytes)", what, extraA.size(), extraB.size());
        ok = false;
    }
    for (size_t i = 0; i < regions.size() && ok; ++i) {
        if (afterA[i] != afterB[i]) {
            size_t k = 0;
            while (afterA[i][k] == afterB[i][k]) ++k;
            ctx.Fail("%s: region %zu differs at +%zx (%02x vs %02x)", what, i, k, afterA[i][k], afterB[i][k]);
            ok = false;
        }
    }
    restore();
    return ok;
}

} // namespace cptest
