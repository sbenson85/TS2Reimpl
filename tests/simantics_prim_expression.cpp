// Differential tests for the SimAntics Expression primitive (src/simantics/PrimExpression.*): the handler 0x5890c0
// and its helpers (SimDataRef 0x57c8d0 0x57cc90 0x512030 0x57ed80 0x57d6b0 0x57cdc0, the param accessors 0x57e4c0
// 0x57e950, the identity 0x141120).
//
// The handler talks to the object only through virtual calls, so the world is a set of fake objects whose slots
// are recorders: the object (primary vtable slots 1/2/56, secondary vtable at +4 with slot 7 = the data read and
// slot 20), the context at +0x40 (slots 24, 26), the polled object (slot 56), the table (slot 29), the notified
// object (slots 1, 2, 56), the "newest" object (slot 12) and the stack frame (slot 16). The data read (slot 7)
// fills the SimDataRef the handler passes from the case's description (storage in the world's cell arrays, a
// range, a float) and returns the case's value. Everything the fakes see (owner, index, argument pointers' null-ness,
// the reference as the callee finds it) goes into a trace; the compared outcome is the result (full eax), the
// exception thrown (a `short` code, or the `int` the notify fake throws), the trace, and the cells/float written.
#include "difftest.h"
#include "simantics/PrimExpression.h"

#include <cstring>
#include <random>
#include <vector>

namespace {

constexpr uint32_t kRvaExecute = 0x5890c0;

struct Side {
    bool hasPtr;
    int16_t len, idx, mn, mx;
    bool hasFloat;
    uint32_t ret; // eax of the read (only ax is used by the handler)
};

struct World {
    alignas(8) uint8_t obj[0x80];
    void *vtPrimary[360];
    void *vtSecondary[50];
    alignas(8) uint8_t ctx[16];
    void *vtCtx[40];
    alignas(8) uint8_t pol[16];
    void *vtPol[60];
    alignas(8) uint8_t tab[16];
    void *vtTab[40];
    alignas(8) uint8_t tgt[16];
    void *vtTgt[60];
    alignas(8) uint8_t nw[16];
    void *vtNew[16];
    alignas(8) uint8_t frame[16];
    void *vtFrame[20];
    alignas(4) int16_t lcells[16];
    alignas(4) int16_t rcells[16];
    float fcell;
    alignas(8) uint8_t param[0x18];

    // The case.
    Side lhs, rhs;
    uint32_t frameRet, newId, polRet, notifyRet;
    bool tgtNull, throwNotify;
};

World *gW = nullptr;
std::vector<uint32_t> gTrace;
int gReads = 0;
uint32_t gHits[64]; // calls per slot kind in the current run (coverage self-check)

uint32_t IdOf(const void *p)
{
    auto a = reinterpret_cast<uintptr_t>(p);
    auto is = [&](const void *base) { return a == reinterpret_cast<uintptr_t>(base); };
    if (!gW) return 0xff;
    if (is(gW->obj)) return 1;
    if (is(gW->obj + 4)) return 11;
    if (is(gW->ctx)) return 2;
    if (is(gW->pol)) return 3;
    if (is(gW->tab)) return 4;
    if (is(gW->tgt)) return 5;
    if (is(gW->nw)) return 6;
    if (is(gW->frame)) return 7;
    return 0xfe;
}

void Note(uint32_t kind, const void *self)
{
    gTrace.push_back(kind);
    gTrace.push_back(IdOf(self));
    if (kind < 64) ++gHits[kind];
    if (kind == 56 && IdOf(self) == 1) ++gHits[63]; // slot 56 on the object itself (owner 3 path)
}

// Slot 7 of the secondary vtable (ret 0x1c).
uint32_t __fastcall S7(void *self, void *, uint32_t owner, uint32_t data, SimDataRef *ref, float **fpOut,
                       uint32_t *out5, uint32_t b6, uint32_t b7)
{
    Note(7, self);
    for (uint32_t v : {owner, data, ref ? 1u : 0u, fpOut ? 1u : 0u, out5 ? 1u : 0u, b6, b7}) gTrace.push_back(v);
    // The reference as the handler hands it over: reset (length, index and the pointer word, then the range).
    gTrace.push_back(static_cast<uint16_t>(ref->mLength));
    gTrace.push_back(static_cast<uint16_t>(ref->mIndex));
    gTrace.push_back(ref->mPtr ? 1u : 0u);
    gTrace.push_back(static_cast<uint16_t>(ref->mMin));
    gTrace.push_back(static_cast<uint16_t>(ref->mMax));
    const bool first = gReads++ == 0; // the handler reads the right operand first
    const Side &s = first ? gW->rhs : gW->lhs;
    int16_t *cells = first ? gW->rcells : gW->lcells;
    ref->mLength = s.len;
    ref->mIndex = s.idx;
    ref->mPtr = s.hasPtr ? cells + s.idx : nullptr;
    ref->mMin = s.mn;
    ref->mMax = s.mx;
    if (fpOut) *fpOut = s.hasFloat ? &gW->fcell : nullptr;
    if (out5) *out5 = 0;
    return s.ret;
}

void *__fastcall S20(void *self, void *)
{
    Note(20, self);
    return gW->nw;
}

uint32_t __fastcall N12(void *self, void *)
{
    Note(12, self);
    return gW->newId;
}

uint32_t __fastcall F16(void *self, void *)
{
    Note(16, self);
    return gW->frameRet;
}

void *__fastcall C24(void *self, void *)
{
    Note(24, self);
    return gW->pol;
}

void *__fastcall C26(void *self, void *)
{
    Note(26, self);
    return gW->tab;
}

void *__fastcall T29(void *self, void *, int32_t id)
{
    Note(29, self);
    gTrace.push_back(static_cast<uint32_t>(id));
    return gW->tgtNull ? nullptr : gW->tgt;
}

uint32_t __fastcall AddRefFake(void *self, void *)
{
    Note(1, self);
    return 5;
}

uint32_t __fastcall ReleaseFake(void *self, void *)
{
    Note(2, self);
    return 4;
}

uint32_t __fastcall P56(void *self, void *)
{
    Note(56, self);
    if (gW->throwNotify) throw 42;
    return gW->notifyRet;
}

uint32_t __fastcall Pol56(void *self, void *)
{
    Note(56, self);
    return gW->polRet;
}

template <typename T> void Put(uint8_t *base, size_t off, T v)
{
    std::memcpy(base + off, &v, sizeof(v));
}

uintptr_t Addr(const void *p)
{
    return reinterpret_cast<uintptr_t>(p);
}

const int16_t kEdges[] = {0, 1, 2, 3, 4, 15, 16, 17, 18, -1, -2, 0x7fff, 0x7ffe, -0x8000, -0x7fff, 0x56, 0x89,
                          100, -100, 255, 256, 0x4000, -0x4000, 31, 32, 33};

int16_t Val(std::mt19937 &r)
{
    if (r() % 3) return kEdges[r() % (sizeof(kEdges) / sizeof(kEdges[0]))];
    return static_cast<int16_t>(r());
}

uint32_t Ret(std::mt19937 &r)
{
    return static_cast<uint16_t>(Val(r)) | (static_cast<uint32_t>(r()) << 16);
}

uint32_t PickRet(std::mt19937 &r)
{
    uint32_t v = r();
    switch (r() % 4) {
    case 0: return 0;
    case 1: return v & 0xffff0000u;
    default: return v;
    }
}

float FloatVal(std::mt19937 &r)
{
    static const float kF[] = {0.0f, -0.0f, 1.0f, -1.0f, 99.5f, 100.0f, -100.0f, 100.5f, -100.5f, 1e30f, -1e30f,
                               32767.5f, -32768.5f, 3.4e38f, 0.5f};
    switch (r() % 6) {
    case 0: return kF[r() % (sizeof(kF) / sizeof(kF[0]))];
    case 1: {
        uint32_t bits = r();
        float f;
        std::memcpy(&f, &bits, 4); // any bit pattern, NaNs and infinities included
        return f;
    }
    default: return static_cast<float>(static_cast<int>(r() % 4001) - 2000) / 7.0f;
    }
}

Side MakeSide(std::mt19937 &r)
{
    Side s;
    s.hasPtr = r() % 10 < 7;
    s.len = static_cast<int16_t>(static_cast<int>(r() % 13) - 3); // -3..9
    s.idx = static_cast<int16_t>(r() % 8);
    switch (r() % 4) {
    case 0: s.mn = INT16_MIN, s.mx = INT16_MAX; break;
    case 1: s.mn = Val(r), s.mx = Val(r); break; // possibly mn > mx
    case 2: s.mn = static_cast<int16_t>(-Val(r) % 200), s.mx = static_cast<int16_t>(100 + (r() % 300)); break;
    default: s.mn = INT16_MIN, s.mx = Val(r); break;
    }
    s.hasFloat = r() % 10 < 6;
    s.ret = Ret(r);
    return s;
}

void Build(World &w, uint32_t seed, int forceOp)
{
    std::mt19937 rng(seed * 2246822519u + 12345u);
    std::memset(w.obj, 0, sizeof(w.obj));
    for (auto &b : w.obj) b = static_cast<uint8_t>(rng());
    for (auto &b : w.param) b = static_cast<uint8_t>(rng());

    w.lhs = MakeSide(rng);
    w.rhs = MakeSide(rng);
    for (auto &c : w.lcells) c = Val(rng);
    for (auto &c : w.rcells) c = Val(rng);
    w.fcell = FloatVal(rng);
    w.frameRet = rng();
    w.newId = rng();
    w.polRet = PickRet(rng);
    w.notifyRet = rng();
    w.tgtNull = rng() % 8 == 0;
    w.throwNotify = rng() % 12 == 0;

    // The operand block: operator mostly in range, owners favouring the ones the epilogue looks at.
    uint8_t op;
    if (forceOp >= 0)
        op = static_cast<uint8_t>(forceOp);
    else if (rng() % 10 < 8)
        op = static_cast<uint8_t>(rng() % 0x16);
    else
        op = static_cast<uint8_t>(0x16 + rng() % 0xea);
    uint8_t lhsOwner = rng() % 4 ? static_cast<uint8_t>(2 + rng() % 17) : static_cast<uint8_t>(rng());
    uint8_t rhsOwner = rng() % 3 ? 7 : static_cast<uint8_t>(rng());
    uint16_t lhsData = rng() % 3 ? static_cast<uint16_t>(Val(rng)) : static_cast<uint16_t>(0x56);
    uint16_t rhsData = static_cast<uint16_t>(Val(rng));
    Put(w.param, 4, lhsData);
    Put(w.param, 6, rhsData);
    w.param[9] = op;
    w.param[0xa] = lhsOwner;
    w.param[0xb] = rhsOwner;
    // Right operand values favour the shift/bit/division edges.
    if (rng() % 3 == 0) w.rhs.ret = static_cast<uint16_t>(rng() % 20 - 1) | (rng() << 16);

    for (auto &s : w.vtPrimary) s = nullptr;
    for (auto &s : w.vtSecondary) s = nullptr;
    for (auto &s : w.vtCtx) s = nullptr;
    for (auto &s : w.vtPol) s = nullptr;
    for (auto &s : w.vtTab) s = nullptr;
    for (auto &s : w.vtTgt) s = nullptr;
    for (auto &s : w.vtNew) s = nullptr;
    for (auto &s : w.vtFrame) s = nullptr;
    w.vtPrimary[1] = reinterpret_cast<void *>(&AddRefFake);
    w.vtPrimary[2] = reinterpret_cast<void *>(&ReleaseFake);
    w.vtPrimary[56] = reinterpret_cast<void *>(&P56);
    w.vtSecondary[7] = reinterpret_cast<void *>(&S7);
    w.vtSecondary[20] = reinterpret_cast<void *>(&S20);
    w.vtCtx[24] = reinterpret_cast<void *>(&C24);
    w.vtCtx[26] = reinterpret_cast<void *>(&C26);
    w.vtPol[56] = reinterpret_cast<void *>(&Pol56);
    w.vtTab[29] = reinterpret_cast<void *>(&T29);
    w.vtTgt[1] = reinterpret_cast<void *>(&AddRefFake);
    w.vtTgt[2] = reinterpret_cast<void *>(&ReleaseFake);
    w.vtTgt[56] = reinterpret_cast<void *>(&P56);
    w.vtNew[12] = reinterpret_cast<void *>(&N12);
    w.vtFrame[16] = reinterpret_cast<void *>(&F16);

    Put(w.obj, 0, Addr(w.vtPrimary));
    Put(w.obj, 4, Addr(w.vtSecondary));
    Put(w.obj, 0x40, Addr(w.ctx));
    Put(w.ctx, 0, Addr(w.vtCtx));
    Put(w.pol, 0, Addr(w.vtPol));
    Put(w.tab, 0, Addr(w.vtTab));
    Put(w.tgt, 0, Addr(w.vtTgt));
    Put(w.nw, 0, Addr(w.vtNew));
    Put(w.frame, 0, Addr(w.vtFrame));
}

struct Outcome {
    uint32_t ret = 0;
    int thrown = 0; // 0 none, 1 short, 2 int, 3 other
    int code = 0;
    std::vector<uint32_t> trace;
    int16_t lcells[16], rcells[16];
    uint32_t fbits = 0;
    uint32_t hits[64];
};

template <typename Call> Outcome Run(World &w, Call call)
{
    gW = &w;
    gTrace.clear();
    gReads = 0;
    std::memset(gHits, 0, sizeof(gHits));
    Outcome o;
    try {
        o.ret = call(w);
    } catch (short c) {
        o.thrown = 1;
        o.code = c;
    } catch (int c) {
        o.thrown = 2;
        o.code = c;
    } catch (...) {
        o.thrown = 3;
    }
    o.trace = gTrace;
    std::memcpy(o.hits, gHits, sizeof(gHits));
    std::memcpy(o.lcells, w.lcells, sizeof(o.lcells));
    std::memcpy(o.rcells, w.rcells, sizeof(o.rcells));
    std::memcpy(&o.fbits, &w.fcell, 4);
    gW = nullptr;
    return o;
}

using ExecuteFn = int32_t(__thiscall *)(void *, void *, void *);

// What the cases reached on the original side: asserted at the end so a generator that stops covering a branch
// fails the test instead of passing silently.
struct Coverage {
    uint32_t hits[64] = {};
    uint32_t thrownShort[0x80] = {};
    uint32_t thrownInt = 0, floatChanged = 0, cellChanged = 0, copyTwoOk = 0, forcedZero = 0;
};

void RunCases(difftest::Context &ctx, const char *name, uint32_t first, uint32_t count, int forceOp, Coverage &cov)
{
    ExecuteFn orig = ctx.Original<ExecuteFn>(kRvaExecute);
    for (uint32_t seed = first; seed < first + count; ++seed) {
        World *a = new World;
        World *b = new World;
        Build(*a, seed, forceOp);
        Build(*b, seed, forceOp);
        Outcome oa = Run(*a, [&](World &w) { return static_cast<uint32_t>(orig(w.obj, w.frame, w.param)); });
        Outcome ob = Run(*b, [&](World &w) {
            return static_cast<uint32_t>(
                reinterpret_cast<ExpressionPrimitive *>(w.obj)->Execute(w.frame, reinterpret_cast<ExpressionParam *>(w.param)));
        });
        ++ctx.cases;
        const uint32_t op = a->param[9];
        for (int k = 0; k < 64; ++k) cov.hits[k] += oa.hits[k];
        if (oa.thrown == 1 && oa.code >= 0 && oa.code < 0x80) ++cov.thrownShort[oa.code];
        if (oa.thrown == 2) ++cov.thrownInt;
        if (oa.thrown == 0 && op == 21) ++cov.copyTwoOk;
        if (oa.thrown != ob.thrown || oa.code != ob.code || (oa.thrown == 0 && oa.ret != ob.ret)) {
            ctx.Fail("%s seed %u op %u: original ret %d thrown %d code %d, ours ret %d thrown %d code %d", name, seed, op,
                     static_cast<int>(oa.ret), oa.thrown, oa.code, static_cast<int>(ob.ret), ob.thrown, ob.code);
        } else if (oa.trace != ob.trace) {
            size_t i = 0;
            while (i < oa.trace.size() && i < ob.trace.size() && oa.trace[i] == ob.trace[i]) ++i;
            ctx.Fail("%s seed %u op %u: traces differ at word %zu (original %zu words, ours %zu)", name, seed, op, i,
                     oa.trace.size(), ob.trace.size());
        } else if (std::memcmp(oa.lcells, ob.lcells, sizeof(oa.lcells)) || std::memcmp(oa.rcells, ob.rcells, sizeof(oa.rcells)) ||
                   oa.fbits != ob.fbits) {
            ctx.Fail("%s seed %u op %u: stored values differ (float bits original 0x%x, ours 0x%x)", name, seed, op,
                     oa.fbits, ob.fbits);
        }
        delete a;
        delete b;
    }
}

void CheckCoverage(difftest::Context &ctx, const Coverage &c)
{
    for (int slot : {1, 2, 7, 12, 16, 20, 24, 29, 56, 63})
        if (!c.hits[slot]) ctx.Fail("coverage: slot %d never called", slot);
    for (int code : {1, 8, 0xd, 0x13, 0x14, 0x26, 0x78})
        if (!c.thrownShort[code]) ctx.Fail("coverage: error 0x%x never thrown", code);
    if (!c.thrownInt) ctx.Fail("coverage: notify fake never threw");
    if (!c.copyTwoOk) ctx.Fail("coverage: operator 21 never succeeded");
}

} // namespace

// Every operator, with the random worlds above.
DIFF_TEST(simantics_expression_execute, kRvaExecute)
{
    Coverage cov;
    RunCases(ctx, "Execute", 1, 60000, -1, cov);
    CheckCoverage(ctx, cov);
}

// Each operator alone (including 0x15 and the first out-of-range one), so rarely-hit branches get cases.
DIFF_TEST(simantics_expression_execute_per_operator, kRvaExecute)
{
    Coverage cov;
    for (int op = 0; op <= 0x17; ++op) RunCases(ctx, "Execute(op)", 100000 + op * 5000, 5000, op, cov);
    for (int op : {0x20, 0x7f, 0x80, 0xfe, 0xff}) RunCases(ctx, "Execute(big op)", 300000 + op * 10, 50, op, cov);
    CheckCoverage(ctx, cov);
}

// ---------------------------------------------------------------------------------------------------------
// SimDataRef and the accessors
// ---------------------------------------------------------------------------------------------------------

namespace {

struct RefWorld {
    int16_t cells[16];
    SimDataRef ref;
};

void BuildRef(RefWorld &w, std::mt19937 &rng)
{
    for (auto &c : w.cells) c = Val(rng);
    w.ref.mLength = Val(rng) % 12;
    if (rng() % 4 == 0) w.ref.mLength = Val(rng);
    w.ref.mIndex = static_cast<int16_t>(rng() % 8);
    w.ref.mPtr = rng() % 5 ? w.cells + static_cast<int>(rng() % 8) : nullptr;
    w.ref.mMin = rng() % 3 ? static_cast<int16_t>(INT16_MIN) : Val(rng);
    w.ref.mMax = rng() % 3 ? static_cast<int16_t>(INT16_MAX) : Val(rng);
}

// The pointer as a cell offset, for comparing two worlds.
struct RefState {
    int16_t len, idx, mn, mx;
    int ptrOffset; // -1: null
    int16_t cells[16];
    bool operator==(const RefState &o) const { return std::memcmp(this, &o, sizeof(*this)) == 0; }
};

RefState State(const RefWorld &w)
{
    RefState s;
    std::memset(&s, 0, sizeof(s));
    s.len = w.ref.mLength;
    s.idx = w.ref.mIndex;
    s.mn = w.ref.mMin;
    s.mx = w.ref.mMax;
    s.ptrOffset = w.ref.mPtr ? static_cast<int>(w.ref.mPtr - w.cells) : -1;
    std::memcpy(s.cells, w.cells, sizeof(s.cells));
    return s;
}

} // namespace

DIFF_TEST(simantics_dataref_get, 0x512030)
{
    using Fn = int32_t(__thiscall *)(SimDataRef *);
    Fn orig = ctx.Original<Fn>(0x512030);
    std::mt19937 rng(11);
    for (int i = 0; i < 20000; ++i) {
        RefWorld a, b;
        BuildRef(a, rng);
        b = a;
        b.ref.mPtr = a.ref.mPtr ? b.cells + (a.ref.mPtr - a.cells) : nullptr;
        int32_t x = orig(&a.ref), y = b.ref.Get();
        ++ctx.cases;
        if (x != y || !(State(a) == State(b))) ctx.Fail("Get case %d: original %d, ours %d", i, x, y);
    }
}

DIFF_TEST(simantics_dataref_set, 0x57ed80)
{
    using Fn = uint8_t(__thiscall *)(SimDataRef *, int32_t);
    Fn orig = ctx.Original<Fn>(0x57ed80);
    std::mt19937 rng(12);
    for (int i = 0; i < 20000; ++i) {
        RefWorld a, b;
        BuildRef(a, rng);
        b = a;
        b.ref.mPtr = a.ref.mPtr ? b.cells + (a.ref.mPtr - a.cells) : nullptr;
        // The low 16 bits are what counts; the upper half is random to prove it.
        int32_t v = static_cast<uint16_t>(Val(rng)) | (static_cast<int32_t>(rng()) << 16);
        uint8_t x = orig(&a.ref, v);
        uint8_t y = b.ref.Set(v) ? 1 : 0;
        ++ctx.cases;
        // al only: the original returns 0/1 (xor al,al / mov al,1).
        if (x != y || !(State(a) == State(b))) ctx.Fail("Set case %d: original %u, ours %u", i, x, y);
    }
}

DIFF_TEST(simantics_dataref_predicates, 0x57cc90)
{
    using Fn = uint8_t(__thiscall *)(SimDataRef *);
    Fn hasData = ctx.Original<Fn>(0x57cc90);
    Fn hasNext = ctx.Original<Fn>(0x57d6b0);
    std::mt19937 rng(13);
    for (int i = 0; i < 20000; ++i) {
        RefWorld a, b;
        BuildRef(a, rng);
        b = a;
        b.ref.mPtr = a.ref.mPtr ? b.cells + (a.ref.mPtr - a.cells) : nullptr;
        ++ctx.cases;
        if (hasData(&a.ref) != (b.ref.HasData() ? 1 : 0)) ctx.Fail("HasData case %d", i);
        if (hasNext(&a.ref) != (b.ref.HasNext() ? 1 : 0)) ctx.Fail("HasNext case %d", i);
        if (!(State(a) == State(b))) ctx.Fail("predicates changed the object, case %d", i);
    }
}

DIFF_TEST(simantics_dataref_reset_advance, 0x57c8d0)
{
    using Fn = SimDataRef *(__thiscall *)(SimDataRef *);
    Fn reset = ctx.Original<Fn>(0x57c8d0);
    Fn advance = ctx.Original<Fn>(0x57cdc0);
    std::mt19937 rng(14);
    for (int i = 0; i < 20000; ++i) {
        RefWorld a, b;
        BuildRef(a, rng);
        b = a;
        b.ref.mPtr = a.ref.mPtr ? b.cells + (a.ref.mPtr - a.cells) : nullptr;
        ++ctx.cases;
        SimDataRef *ra = advance(&a.ref);
        SimDataRef *rb = b.ref.Advance();
        if (ra != &a.ref || rb != &b.ref || !(State(a) == State(b))) ctx.Fail("Advance case %d", i);
        // Reset over the whole thing, garbage included, 12 bytes compared raw.
        uint8_t ga[12], gb[12];
        for (int k = 0; k < 12; ++k) ga[k] = gb[k] = static_cast<uint8_t>(rng());
        SimDataRef *xa = reset(reinterpret_cast<SimDataRef *>(ga));
        SimDataRef *xb = reinterpret_cast<SimDataRef *>(gb)->Reset();
        if (xa != reinterpret_cast<SimDataRef *>(ga) || xb != reinterpret_cast<SimDataRef *>(gb) || std::memcmp(ga, gb, 12))
            ctx.Fail("Reset case %d", i);
    }
}

DIFF_TEST(simantics_param_accessors, 0x57e4c0)
{
    using Fn = uint8_t *(__thiscall *)(void *);
    Fn op = ctx.Original<Fn>(0x57e4c0);
    Fn owner = ctx.Original<Fn>(0x57e950);
    using IdFn = void *(__cdecl *)(void *);
    IdFn id = ctx.Original<IdFn>(0x141120);
    alignas(8) uint8_t buf[0x18];
    for (int i = 0; i < 100; ++i) {
        for (auto &b : buf) b = static_cast<uint8_t>(i * 7 + 3);
        ExpressionParam *p = reinterpret_cast<ExpressionParam *>(buf);
        ++ctx.cases;
        if (op(buf) != p->OperatorPtr() || owner(buf) != p->RhsOwnerPtr()) ctx.Fail("param accessor addresses");
        void *q = reinterpret_cast<void *>(static_cast<uintptr_t>(0x1000u * i + 0xdead0000u));
        if (id(q) != ParamIdentity(q)) ctx.Fail("identity");
    }
}
