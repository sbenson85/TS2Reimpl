// Differential test for the SimAntics operand reader (cTSObject::vf4_7, 0x531700; src/simantics/OperandReader.h).
// Framework: tests/simantics_prims_u.h.
//
// The world is the object body plus fake objects (see the framework). Every owner case runs on random worlds built
// from the seed; the original and ours are compared on the value (ax), the exception (a short code, or the game's
// cTSTreeStack enum), the calls made with their arguments and the destination/float/zeroed outputs.
//
// Not covered: owner 23 (0x17) ids 1, 3, 4 (last call), 6, 7 call original time/skill code (0x7decd0, 0x7e4250,
// 0x7e24f0, 0x7e43c0) that needs the game's calendar service; ids 0, 2 and 5 and the error path are covered.
// Owners 22 and 50 dereference the person data without a check (ORIGINAL-BUG): the world gives them a person.
#include "simantics_prims_u.h"

namespace {

// ---- special thunks ------------------------------------------------------------------------------------------------

// Slot 0 (QueryInterface-style): (iid, void **out); writes the scripted object (script slot 0), returns al from script slot 1000.
uint32_t __fastcall QiThunk(void *self, void *, uint32_t iid, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(0u << 8 | 2u);
    gTrace.push_back(iid);
    gTrace.push_back(out ? 1u : 0u);
    const uint32_t object = Scripted(self, 0);
    if (out) *out = reinterpret_cast<void *>(static_cast<uintptr_t>(object));
    return Scripted(self, 1000);
}

// Slot 29: (value, iid, void **out)
uint32_t __fastcall Qi29(void *self, void *, uint32_t v, uint32_t iid, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(29u << 8 | 3u);
    gTrace.push_back(Norm(v));
    gTrace.push_back(iid);
    gTrace.push_back(out ? 1u : 0u);
    const uint32_t object = Scripted(self, 1029);
    if (out) *out = reinterpret_cast<void *>(static_cast<uintptr_t>(object));
    return Scripted(self, 29);
}

// The context's slot-10 object, slot 14: (stack object id, id, int16_t **out) -> ax; writes the pointer.
uint32_t __fastcall Slot14(void *self, void *, uint32_t so, uint32_t id, int16_t **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(14u << 8 | 3u);
    gTrace.push_back(so);
    gTrace.push_back(id);
    gTrace.push_back(out ? 1u : 0u);
    if (out) *out = reinterpret_cast<int16_t *>(static_cast<uintptr_t>(Scripted(self, 1014)));
    return Scripted(self, 14);
}

// Slots 11 and 13 of the list-interface holder: (id, uint32_t *a, uint32_t *b) -> al. May write both.
template <int S> struct Slot3Out {
    static uint32_t __fastcall F(void *self, void *, uint32_t id, uint32_t *a, uint32_t *b)
    {
        Tick();
        gTrace.push_back(Off(self));
        gTrace.push_back(static_cast<uint32_t>(S) << 8 | 3u);
        gTrace.push_back(id);
        gTrace.push_back(a ? Norm(*a) : 0xdeadbeefu);
        gTrace.push_back(b ? *b : 0xdeadbeefu);
        if (Scripted(self, 1000 + S)) {
            *a = Scripted(self, 2000 + S);
            *b = Scripted(self, 3000 + S);
        }
        gTrace.push_back(Norm(*a));
        gTrace.push_back(*b);
        return Scripted(self, S);
    }
};

// Slot 4 of the stack-object interface: (id, uint32_t *out) -> al.
uint32_t __fastcall Slot4Out(void *self, void *, uint32_t id, uint32_t *out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(4u << 8 | 2u);
    gTrace.push_back(id);
    gTrace.push_back(out ? Norm(*out) : 0xdeadbeefu);
    return Scripted(self, 4);
}

// Slot 77: (uint8_t *buf, v, two): writes twelve bytes.
uint32_t __fastcall Slot77(void *self, void *, uint8_t *buf, uint32_t v, uint32_t two)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(77u << 8 | 3u);
    gTrace.push_back(buf ? 1u : 0u);
    gTrace.push_back(v);
    gTrace.push_back(two);
    for (int i = 0; i < 3; ++i) {
        const uint32_t d = Scripted(self, 1077 + i);
        std::memcpy(buf + 4 * i, &d, 4);
    }
    return Scripted(self, 77);
}

// Slot 58: (const uint8_t *buf): logs the twelve bytes.
uint32_t __fastcall Slot58(void *self, void *, const uint8_t *buf)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(58u << 8 | 1u);
    for (int i = 0; i < 3; ++i) {
        uint32_t d;
        std::memcpy(&d, buf + 4 * i, 4);
        gTrace.push_back(d);
    }
    return Scripted(self, 58);
}

// Float-returning slots (st0): one argument.
template <int S> struct FloatSlot {
    static float __fastcall F(void *self, void *, uint32_t id)
    {
        Tick();
        gTrace.push_back(Off(self));
        gTrace.push_back(static_cast<uint32_t>(S) << 8 | 1u);
        gTrace.push_back(id);
        const uint32_t bits = Scripted(self, S);
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    }
};

// ---- the world -------------------------------------------------------------------------------------------------------

enum Role {
    CTX, CTX26, O1, A, X34, OBJ38, X38, Y38, FR0, FR1, EL0, EL1, X47, Y47, LIST, SOBJ0, SOBJ1, P0, P1, Q16, Q91, A61, Q21, B27, IFACE,
    SECENT, SVC, Y77, LE0, LE1, LE2, LE3, kRoles
};
static_assert(kRoles <= kF, "fakes");

struct Scenario {
    bool pValid = false; // the stack lookup reaches a person (owners 22 and 50 need one)
};

float RandomFloat(std::mt19937 &r)
{
    switch (r() % 9) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return static_cast<float>(static_cast<int>(r() % 2001) - 1000) / 8.0f;
    case 3: return static_cast<float>(static_cast<int>(r() % 200001) - 100000) / 100.0f;
    case 4: return (r() % 2) ? 3.0e9f : -3.0e9f;
    case 5: return (r() % 2) ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
    case 6: return static_cast<float>(static_cast<int>(r() % 400) - 100) / 400.0f; // around 0.25 .. 1.25 (owner 23 id 0)
    case 7: return 0.25f + static_cast<float>(r() % 300) / 400.0f;
    default: {
        uint32_t b = r();
        float f;
        std::memcpy(&f, &b, 4);
        return f;
    }
    }
}

uint32_t FloatBits(float f)
{
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

int16_t Edge16(std::mt19937 &r)
{
    const int16_t e[] = {0, 1, -1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 16, 0x19, 0x3b, 0x3c, 0x71, 0x72, 100, 0xe6, 0xe7, 32767, -32768, 12345, -12345};
    return (r() % 3 == 0) ? static_cast<int16_t>(r()) : e[r() % (sizeof(e) / sizeof(e[0]))];
}

uint32_t SmallCount(std::mt19937 &r)
{
    return r() % 10;
}

void Build(UWorld &w, uint32_t seed, Scenario &sc)
{
    EnsureThunks();
    std::mt19937 r(seed * 2654435761u + 12345);
    std::memset(&w, 0, sizeof(UWorld));
    auto fill = [&](void *p, size_t n) {
        uint8_t *b = static_cast<uint8_t *>(p);
        size_t i = 0;
        for (; i + 4 <= n; i += 4) {
            const uint32_t v = InjectWorld(r());
            std::memcpy(b + i, &v, 4);
        }
        for (; i < n; ++i) b[i] = static_cast<uint8_t>(r());
    };
    fill(w.obj, sizeof(w.obj));
    for (int i = 0; i < kF; ++i) fill(w.fk[i].body, sizeof(w.fk[i].body));
    fill(w.table, 0x40);
    fill(w.cells, sizeof(w.cells));
    fill(w.ranges, sizeof(w.ranges));
    fill(w.shorts, sizeof(w.shorts));
    fill(w.buf, sizeof(w.buf));
    // Small cell values make comparisons interesting: a third of the cells are small numbers.
    for (int16_t &c : w.cells)
        if (r() % 3 == 0) c = static_cast<int16_t>(static_cast<int>(r() % 40) - 8);
    gRets.clear();
    gSeed = seed;
    gThrowAt = (r() % 9 == 0) ? 1 + r() % 8 : 0;
    w.canary = 0xC0DEC0DEu;

    for (int i = 0; i < kF; ++i) w.fk[i].vt = w.vtFk[i];
    *reinterpret_cast<void ***>(w.obj) = w.vtObj;
    uint8_t *s = SubOf(w);
    *reinterpret_cast<void ***>(s) = w.vtSec;
    *reinterpret_cast<void ***>(w.table) = w.vtTable;
    *reinterpret_cast<void ***>(s + 0x18) = w.vtStack;

    auto put32 = [&](uint8_t *base, size_t off, const void *v) { std::memcpy(base + off, &v, 4); };
    auto put16 = [&](uint8_t *base, size_t off, int16_t v) { std::memcpy(base + off, &v, 2); };
    auto cellPtr = [&](uint32_t maxOffset) { return &w.cells[r() % (maxOffset + 1)]; };
    auto maybeNull = [&](const void *p, uint32_t oneIn) -> const void * { return (r() % oneIn == 0) ? nullptr : p; };
    UBig *fk = w.fk;

    // ---- the stack: counter at S + 0x18, frames at +0x1c/+0x20 ------------------------------------------------------
    // Half of the worlds are friendly: a valid stack, objects and persons, so the deeper cases get reached.
    const bool friendly = r() % 2 == 0;
    const int frames = friendly ? 2 : static_cast<int>(r() % 3); // 0..2
    w.frameVec[0] = &fk[FR0];
    w.frameVec[1] = &fk[FR1];
    put32(s, 0x1c, w.frameVec);
    put32(s, 0x20, w.frameVec + frames);
    Use(w.vtStack, 10, 0);
    const int depth = friendly ? static_cast<int>(Pick(r, {1, 2})) : static_cast<int>(Pick(r, {0, 1, 1, 1, 2, 2, 2, 3, 3}));
    RetValue(s + 0x18, 10, static_cast<uint32_t>(depth), w);
    const bool frameOk = depth >= 1 && depth - 1 < frames;
    uint8_t depthElem = static_cast<uint8_t>(depth == 2 ? EL1 : EL0);
    for (int i = 0; i < 2; ++i) {
        Use(fk[FR0 + i].vt, 41, 0);
        RetObject(&fk[FR0 + i], 41, &fk[EL0 + i], w);
    }

    // ---- elements -------------------------------------------------------------------------------------------------
    for (int i = 0; i < 2; ++i) {
        UBig &e = fk[EL0 + i];
        put16(e.body, 0xa - 4, static_cast<int16_t>(friendly ? Pick(r, {1, 2, 3}) : Pick(r, {0, 1, 1, 1, 2, 2, 3, 0xffff, 4})));
        Use(e.vt, 26, 0);
        Use(e.vt, 27, 0);
        Use(e.vt, 30, 1);
        Use(e.vt, 20, 0);
        Use(e.vt, 35, 1);
        Use(e.vt, 36, 1);
        RetValue(&e, 26, SmallCount(r), w);
        RetValue(&e, 27, SmallCount(r), w);
        RetValue(&e, 30, (r() % 4 == 0) ? (r() & 0xffff) : (r() % 8), w);
        RetObject(&e, 20, &fk[X47], w);
        RetObject(&e, 35, maybeNull(cellPtr(100), 6), w);
        RetObject(&e, 36, maybeNull(cellPtr(100), 6), w);
    }

    // ---- stack objects and persons ------------------------------------------------------------------------------------
    UBig *pOf[2];
    for (int i = 0; i < 2; ++i) pOf[i] = (!friendly && r() % 5 == 0) ? nullptr : &fk[P0 + i];
    w.objVec[0] = (!friendly && r() % 6 == 0) ? nullptr : &fk[SOBJ0];
    w.objVec[1] = (!friendly && r() % 6 == 0) ? nullptr : &fk[SOBJ1];
    w.objVec[2] = &fk[SOBJ0];
    // The slot before the vector holds a live object, so a lookup that wrongly accepts id 0 (reading entry -1) shows.
    w.vtTable[kSlots - 1] = &fk[SOBJ0];
    const int count = friendly ? 3 : static_cast<int>(Pick(r, {0, 1, 2, 2, 2, 3, 3}));
    put32(w.table, 0x1a24, w.objVec);
    put32(w.table, 0x1a28, w.objVec + count);
    put32(s, 0x1c0, w.table);
    for (int i = 0; i < 2; ++i) {
        Use(fk[SOBJ0 + i].vt, 280, 0);
        RetObject(&fk[SOBJ0 + i], 280, pOf[i], w);
    }
    sc.pValid = false;
    if (frameOk) {
        int16_t id;
        std::memcpy(&id, fk[depthElem].body + 0xa - 4, 2);
        if (id > 0 && id <= count && w.objVec[id - 1]) {
            const int which = static_cast<UBig *>(w.objVec[id - 1]) == &fk[SOBJ1] ? 1 : 0;
            sc.pValid = pOf[which] != nullptr;
        }
    }

    for (int i = 0; i < 2; ++i) {
        UBig &p = fk[P0 + i];
        // body fields (offsets are relative to P; the body starts at +4)
        put32(p.body, 0x88 - 4, cellPtr(60));
        put16(p.body, 0x8c - 4, static_cast<int16_t>(SmallCount(r)));
        put32(p.body, 0x90 - 4, cellPtr(60));
        put16(p.body, 0x94 - 4, static_cast<int16_t>(SmallCount(r)));
        Use(p.vt, 98, 0);
        Use(p.vt, 68, 1);
        Use(p.vt, 91, 0);
        Use(p.vt, 61, 0);
        Use(p.vt, 64, 0);
        Use(p.vt, 104, 0);
        Use(p.vt, 47, 0);
        Use(p.vt, 23, 0);
        Use(p.vt, 266, 0);
        Use(p.vt, 267, 2);
        Use(p.vt, 269, 1);
        Use(p.vt, 271, 1);
        Use(p.vt, 273, 1);
        Use(p.vt, 274, 1);
        p.vt[0] = reinterpret_cast<void *>(&QiThunk);
        RetValue(&p, 98, Pick(r, {2, 2, 2, 2, 1, 0}), w);
        RetObject(&p, 68, maybeNull(&fk[Q16], 5), w);
        RetObject(&p, 91, &fk[Q91], w);
        RetObject(&p, 61, &fk[A61], w);
        RetObject(&p, 64, &fk[Q21], w);
        RetValue(&p, 104, r() % 2, w);
        RetValue(&p, 47, r() & 0xffff, w);
        RetValue(&p, 23, SmallCount(r), w);
        RetValue(&p, 266, (r() % 6 == 0) ? (r() & 0xffff) : SmallCount(r), w);
        RetObject(&p, 267, maybeNull(cellPtr(100), 6), w);
        RetObject(&p, 269, maybeNull(cellPtr(100), 6), w);
        RetObject(&p, 271, maybeNull(cellPtr(100), 6), w);
        RetValue(&p, 273, (r() % 6 == 0) ? (r() & 0xffff) : SmallCount(r), w);
        RetValue(&p, 274, (r() % 6 == 0) ? (r() & 0xffff) : (r() % 6), w);
        // A QueryInterface that says yes must give an object (the callers use it unchecked).
        const bool have = r() % 4 != 0;
        RetObject(&p, 0, have ? &fk[IFACE] : nullptr, w);
        RetValue(&p, 1000, have && r() % 5 != 0 ? (r() | 1u) : (r() & 0xffffff00u), w); // al of the QI
    }

    // ---- the object itself (S - 4) ------------------------------------------------------------------------------------
    Use(w.vtObj, 98, 0);
    Use(w.vtObj, 68, 1);
    Use(w.vtObj, 61, 0);
    w.vtObj[0] = reinterpret_cast<void *>(&QiThunk);
    w.vtObj[185] = reinterpret_cast<void *>(&FloatSlot<185>::F);
    RetValue(w.obj, 98, Pick(r, {2, 2, 2, 1, 0}), w);
    RetObject(w.obj, 68, maybeNull(&fk[Q16], 5), w);
    RetObject(w.obj, 61, &fk[A61], w);
    {
        const bool have = r() % 4 != 0;
        RetObject(w.obj, 0, have ? &fk[IFACE] : nullptr, w);
        RetValue(w.obj, 1000, have && r() % 5 != 0 ? (r() | 1u) : (r() & 0xffffff00u), w);
    }
    RetValue(w.obj, 185, FloatBits(RandomFloat(r)), w);

    // ---- shared leaf objects ------------------------------------------------------------------------------------------
    Use(fk[Q16].vt, 4, 0);
    RetValue(&fk[Q16], 4, r() & 0xffff, w);
    Use(fk[Q91].vt, 8, 1);
    RetValue(&fk[Q91], 8, (r() % 3 == 0) ? 0 : (r() & 0xffff), w);
    Use(fk[A61].vt, 46, 0);
    Use(fk[A61].vt, 47, 0);
    Use(fk[A61].vt, 27, 0);
    RetValue(&fk[A61], 46, (r() % 8 == 0) ? static_cast<uint32_t>(-1 - static_cast<int>(r() % 3)) : SmallCount(r), w);
    RetObject(&fk[A61], 47, maybeNull(cellPtr(100), 6), w);
    RetObject(&fk[A61], 27, &fk[B27], w);
    Use(fk[B27].vt, 4, 0);
    RetObject(&fk[B27], 4, &fk[Q21], w);
    fk[Q21].vt[4] = reinterpret_cast<void *>(&Slot4Out);
    Use(fk[Q21].vt, 5, 1);
    Use(fk[Q21].vt, 7, 1);
    RetValue(&fk[Q21], 4, (r() % 4 == 0) ? 0 : (r() | 1u), w);
    RetValue(&fk[Q21], 5, r() & 0xffff, w);
    RetObject(&fk[Q21], 7, maybeNull(cellPtr(100), 5), w);
    Use(fk[IFACE].vt, 12, 0);
    Use(fk[IFACE].vt, 2, 0);
    fk[IFACE].vt[13] = reinterpret_cast<void *>(&FloatSlot<13>::F);
    Use(fk[IFACE].vt, 15, 1);
    RetObject(&fk[IFACE], 12, maybeNull(cellPtr(100), 6), w);
    RetValue(&fk[IFACE], 13, FloatBits(RandomFloat(r)), w);
    RetValue(&fk[IFACE], 15, r(), w);

    // ---- the context ---------------------------------------------------------------------------------------------------
    put32(s, 0x3c, &fk[CTX]);
    Use(fk[CTX].vt, 26, 0);
    Use(fk[CTX].vt, 3, 0);
    Use(fk[CTX].vt, 10, 0);
    Use(fk[CTX].vt, 6, 0);
    RetObject(&fk[CTX], 26, &fk[CTX26], w);
    RetObject(&fk[CTX], 3, &fk[O1], w);
    RetObject(&fk[CTX], 10, &fk[A], w);
    RetObject(&fk[CTX], 6, &fk[X34], w);
    Use(fk[CTX26].vt, 72, 1);
    Use(fk[O1].vt, 17, 1);
    RetObject(&fk[O1], 17, maybeNull(cellPtr(100), 5), w);
    Use(fk[A].vt, 16, 2);
    fk[A].vt[14] = reinterpret_cast<void *>(&Slot14);
    RetObject(&fk[A], 16, maybeNull(&fk[OBJ38], 5), w);
    RetObject(&fk[A], 1014, maybeNull(cellPtr(100), 5), w);
    RetValue(&fk[A], 14, r() & 0xffff, w);
    Use(fk[X34].vt, 24, 1);
    RetValue(&fk[X34], 24, r() & 0xffff, w);
    Use(fk[OBJ38].vt, 10, 0);
    Use(fk[OBJ38].vt, 18, 1);
    RetObject(&fk[OBJ38], 10, maybeNull(&fk[X38], 4), w);
    RetObject(&fk[OBJ38], 18, maybeNull(cellPtr(100), 5), w);
    Use(fk[X38].vt, 4, 0);
    RetObject(&fk[X38], 4, maybeNull(&fk[Y38], 4), w);
    fk[Y38].vt[4] = reinterpret_cast<void *>(&Slot4Out);
    Use(fk[Y38].vt, 5, 1);
    RetValue(&fk[Y38], 4, (r() % 4 == 0) ? 0 : (r() | 1u), w);
    RetValue(&fk[Y38], 5, r() & 0xffff, w);

    // ---- the list-interface chain of owners 26 and 47 ------------------------------------------------------------------
    Use(fk[X47].vt, 37, 0);
    fk[X47].vt[29] = reinterpret_cast<void *>(&Qi29);
    RetObject(&fk[X47], 37, &fk[Y47], w);
    RetObject(&fk[X47], 1029, maybeNull(&fk[LIST], 4), w);
    fk[Y47].vt[11] = reinterpret_cast<void *>(&Slot3Out<11>::F);
    fk[Y47].vt[13] = reinterpret_cast<void *>(&Slot3Out<13>::F);
    for (int slot : {11, 13}) {
        RetValue(&fk[Y47], slot, (r() % 5 == 0) ? 0 : (r() | 1u), w);
        RetValue(&fk[Y47], 1000 + slot, r() % 3 != 0, w);
        RetValue(&fk[Y47], 2000 + slot, static_cast<uint32_t>(r()) | 0xf0000000u, w); // outside user address space: Norm() must not map it
        // Slot 11's output is a signed list index; slot 13's is an index into the temps (owner 47), which must not run
        // below them: the 16 bytes before are the table pointer, whose bits depend on the world's address.
        RetValue(&fk[Y47], 3000 + slot, static_cast<uint32_t>(slot == 11 ? static_cast<int>(r() % 14) - 3 : static_cast<int>(r() % 10)), w);
    }
    Use(fk[LIST].vt, 9, 0);
    Use(fk[LIST].vt, 11, 1);
    Use(fk[LIST].vt, 2, 0);
    RetValue(&fk[LIST], 9, (r() % 8 == 0) ? static_cast<uint32_t>(-1) : SmallCount(r), w);
    RetValue(&fk[LIST], 11, r() & 0xffff, w);

    // ---- the secondary vtable (depth, stack objects) --------------------------------------------------------------------------
    Use(w.vtSec, 22, 1);
    Use(w.vtSec, 23, 0);
    RetObject(s, 22, &fk[SECENT], w);
    RetValue(s, 23, (r() % 5 == 0) ? (r() & 0xffff) : (1 + r() % 4), w);
    Use(fk[SECENT].vt, 11, 0);
    RetObject(&fk[SECENT], 11, maybeNull(cellPtr(100), 5), w);

    // ---- the cached service of owner 23 --------------------------------------------------------------------------------------
    Use(fk[SVC].vt, 39, 0);
    fk[SVC].vt[58] = reinterpret_cast<void *>(&Slot58);
    RetObject(&fk[SVC], 39, &fk[Y77], w);
    RetValue(&fk[SVC], 58, r(), w);
    fk[Y77].vt[77] = reinterpret_cast<void *>(&Slot77);
    for (int i = 0; i < 3; ++i) RetValue(&fk[Y77], 1077 + i, (r() % 3 == 0) ? (r() & 0xffff) : static_cast<uint32_t>(r()), w);

    // ---- the object table's list (owner 23 id 5) -------------------------------------------------------------------------
    Use(w.vtTable, 31, 0);
    {
        // list object = listMem[0] -> sentinel; sentinel = {next, prev, value}; nodes = {next, prev, value}
        uint32_t *m = w.listMem;
        const int nodes = static_cast<int>(r() % 5);
        uint32_t **sentinel = reinterpret_cast<uint32_t **>(&m[4]);
        m[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(sentinel));
        uint32_t *prev = reinterpret_cast<uint32_t *>(sentinel);
        for (int i = 0; i < nodes; ++i) {
            uint32_t *node = &m[8 + 4 * i];
            node[2] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&fk[LE0 + (i % 4)]));
            prev[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node));
            node[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(prev));
            prev = node;
        }
        prev[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(sentinel));
        RetObject(w.table, 31, m, w);
    }
    for (int i = 0; i < 4; ++i) {
        UBig &e = fk[LE0 + i];
        Use(e.vt, 25, 1);
        Use(e.vt, 13, 1);
        Use(e.vt, 131, 0);
        RetValue(&e, 25, r() % 3 == 0 ? 1 : 0, w);
        RetValue(&e, 13, (r() % 2) ? 0x16 : (r() & 0xffff), w);
        RetValue(&e, 131, (r() % 2) ? (r() % 4) : (r() & 0xffff), w);
    }

    // ---- fields of the subobject ---------------------------------------------------------------------------------------------
    put32(s, 0x84, cellPtr(60));
    put16(s, 0x88, static_cast<int16_t>(SmallCount(r)));
    // Owner 2 adds id * 2 to this pointer and reads through the result: a null table with a non-zero length would
    // fault in the original (and ours), so the world always gives a table.
    put32(s, 0x8c, cellPtr(60));
    put16(s, 0x90, static_cast<int16_t>(SmallCount(r)));
    for (int i = 0; i < 8; ++i) put16(s, 0x92 + 2 * i, static_cast<int16_t>(Pick(r, {0, 1, 2, 3, 4, 5, 6, 7, 7, 8, 9, 15, 16, 0xe6, 0xe7, 0xffff, 0x8000, 0x7fff})));
    // owners 43/45: ranges and the 16-bit value vector
    const int nr = static_cast<int>(Pick(r, {0, 1, 2, 3, 4}));
    for (int i = 0; i < nr; ++i) {
        const uint32_t n = r() % 12;
        int16_t *begin = cellPtr(60);
        std::memcpy(w.ranges + 12 * i, &begin, 4);
        int16_t *end = begin + n;
        std::memcpy(w.ranges + 12 * i + 4, &end, 4);
        std::memcpy(w.ranges + 12 * i + 8, &end, 4);
    }
    put32(s, 0x194, w.ranges);
    put32(s, 0x198, w.ranges + nr * 12 + ((r() % 8 == 0) ? 4 : 0));
    const int ns = static_cast<int>(r() % 12);
    for (int i = 0; i < 32; ++i) w.shorts[i] = static_cast<int16_t>(Pick(r, {0, 1, 2, 3, 4, 5, 6, 11, 0xffff, 0x8000, 0x7fff}));
    put32(s, 0x1a0, w.shorts);
    put32(s, 0x1a4, w.shorts + ns);
}

} // namespace

// ---- running -----------------------------------------------------------------------------------------------------------

namespace {

struct Args {
    int32_t owner = 0;
    uint32_t id = 0;
    bool wantDest = true, wantFloat = true, wantZero = true;
    uint32_t clear = 0;
    uint32_t arg7 = 0;
};

struct Outcome {
    uint32_t ret = 0;
    int thrown = -1; // -1 none; the short code; 0x10000 + value for the stack enum
    std::vector<uint32_t> trace;
    std::vector<uint32_t> state;
};

using OrigFn = uint32_t(__thiscall *)(void *self, int32_t owner, uint32_t id, SimDataRef *dest, uint32_t *floatDest, uint32_t *zeroed,
                                      uint32_t clear, uint32_t arg7);

template <typename Call> Outcome Run(UWorld &w, const Args &a, Call call)
{
    gW = &w;
    gHitCount = 0;
    gTrace.clear();
    *GameAddress<void *>(0x10cc45c) = &w.fk[SVC];
    SimDataRef dest;
    std::memset(&dest, 0x5a, sizeof(dest));
    uint32_t fl = 0xa5a5a5a5u, zr = 0x3c3c3c3cu;
    Outcome o;
    try {
        o.ret = call(SubOf(w), a, a.wantDest ? &dest : nullptr, a.wantFloat ? &fl : nullptr, a.wantZero ? &zr : nullptr) & 0xffff;
    } catch (short code) {
        o.thrown = static_cast<uint16_t>(code);
    } catch (decltype(cTSTreeStack::kStackErrorNoFrame) e) {
        o.thrown = 0x10000 + static_cast<int>(e);
    }
    o.trace = gTrace;
    if (a.wantDest) {
        o.state.push_back(static_cast<uint16_t>(dest.mLength));
        o.state.push_back(static_cast<uint16_t>(dest.mIndex));
        o.state.push_back(Norm(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(dest.mPtr))));
        o.state.push_back(static_cast<uint16_t>(dest.mMin));
        o.state.push_back(static_cast<uint16_t>(dest.mMax));
    }
    o.state.push_back(a.wantFloat ? fl : 0);
    o.state.push_back(a.wantZero ? zr : 0);
    o.state.push_back(w.canary);
    uint32_t h = 17;
    for (int16_t c : w.cells) h = h * 31 + static_cast<uint16_t>(c);
    o.state.push_back(h);
    gW = nullptr;
    return o;
}

Args MakeArgs(std::mt19937 &r, int owner)
{
    Args a;
    a.owner = static_cast<int32_t>(static_cast<int16_t>(owner));
    if (r() % 3 == 2) a.owner = static_cast<int32_t>(((r() | 0xf0000000u) & 0xffff0000u) | static_cast<uint16_t>(owner));
    const int16_t low = Edge16(r);
    switch (r() % 8) {
    case 0: a.id = static_cast<uint16_t>(low); break;
    case 1: a.id = static_cast<uint32_t>(static_cast<int32_t>(low)); break;
    case 2: a.id = ((r() | 0xf0000000u) & 0xffff0000u) | static_cast<uint16_t>(low); break;
    case 3: a.id = ((r() | 0xf0000000u) & 0xffff0000u) | static_cast<uint16_t>(r() % 6); break;
    default: a.id = static_cast<uint16_t>(static_cast<int16_t>(r() % 8)); break;
    }
    if (owner == 23) {
        // Ids 1, 3, 4, 6 and 7 call original time/skill code that needs the game's calendar service (see the top of the file).
        const int16_t id16 = static_cast<int16_t>(a.id);
        const uint32_t hi = a.id & 0xffff0000u;
        if (id16 == 1 || id16 == 3 || id16 == 4 || id16 == 6 || id16 == 7) a.id = hi | static_cast<uint16_t>(Pick(r, {0, 2, 5, 8}));
    }
    a.wantDest = r() % 6 != 0;
    a.wantFloat = r() % 4 != 0;
    a.wantZero = r() % 4 != 0;
    a.clear = Pick(r, {0, 0, 0, 1, 0x100, 0xff, 0x7f00, r()});
    a.arg7 = r() | 0xf0000000u;
    if (gInjectWorld) {
        // U_INJECT_WORLD: the words passed on to the fakes as they are (arg7, and an id's noise high half) sometimes become
        // words inside the shared world; the id keeps its low half (the id the reader uses).
        const uintptr_t base = reinterpret_cast<uintptr_t>(&SharedWorld());
        const uint32_t inWorld = static_cast<uint32_t>(base + ((static_cast<uint16_t>(a.id) - base) & 0xffff));
        if ((a.id >> 16) != 0 && (a.id >> 16) != 0xffff && inWorld - base < sizeof(UWorld) && r() % 2) {
            a.id = inWorld;
            ++gInjected;
        }
        if (r() % 4 == 0) {
            a.arg7 = InjectWorld(a.arg7 & 0x1fffffffu);
        }
    }
    return a;
}

struct Stats {
    uint32_t cases = 0, returns = 0, throws = 0, skipped = 0, bad = 0;
};

bool Differ(const Outcome &a, const Outcome &b)
{
    return a.thrown != b.thrown || a.trace != b.trace || a.state != b.state || (a.thrown < 0 && a.ret != b.ret);
}

// One seed for one owner, both sides in the one shared world (see SharedWorld in tests/simantics_prims_u.h), rebuilt from
// the seed before each run. A mismatch counts as it is, without a retry.
bool Trial(OrigFn original, int owner, uint32_t seed, Outcome &oa, Outcome &ob, Args &args, bool &skipped)
{
    std::mt19937 r(seed * 7919u + static_cast<uint32_t>(owner) * 104729u + 5);
    args = MakeArgs(r, owner);
    UWorld &w = SharedWorld();
    Scenario sa, sb;
    Build(w, seed, sa);
    skipped = false;
    if ((owner == 22 || owner == 50) && !sa.pValid) {
        skipped = true;
        return false;
    }
    oa = Run(w, args, [&](void *s, const Args &x, SimDataRef *d, uint32_t *f, uint32_t *z) {
        return original(s, x.owner, x.id, d, f, z, x.clear, x.arg7);
    });
    Build(w, seed, sb);
    ob = Run(w, args, [&](void *s, const Args &x, SimDataRef *d, uint32_t *f, uint32_t *z) {
        return static_cast<SimOperandSource *>(s)->Read(x.owner, x.id, d, f, z, x.clear, x.arg7);
    });
    return Differ(oa, ob);
}

void RunOwner(difftest::Context &ctx, OrigFn original, int owner, uint32_t seeds, Stats &st)
{
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        if (std::getenv("U_TRACE")) {
            std::printf("owner %d seed %u\n", owner, seed);
            std::fflush(stdout);
        }
        Outcome oa, ob;
        Args args;
        bool skipped = false;
        const bool bad = Trial(original, owner, seed, oa, ob, args, skipped);
        if (skipped) {
            ++st.skipped;
            continue;
        }
        ++ctx.cases;
        ++st.cases;
        if (oa.thrown >= 0) ++st.throws;
        else ++st.returns;
        if (bad) {
            ++st.bad;
            if (std::getenv("U_DUMP")) {
                std::printf("original trace:");
                for (uint32_t v : oa.trace) std::printf(" %x", v);
                std::printf("\nours trace:");
                for (uint32_t v : ob.trace) std::printf(" %x", v);
                std::printf("\nargs owner %x id %x dest %d float %d zero %d clear %x arg7 %x\n", args.owner, args.id, args.wantDest, args.wantFloat,
                            args.wantZero, args.clear, args.arg7);
            }
            size_t k = 0;
            while (k < oa.trace.size() && k < ob.trace.size() && oa.trace[k] == ob.trace[k]) ++k;
            size_t j = 0;
            while (j < oa.state.size() && j < ob.state.size() && oa.state[j] == ob.state[j]) ++j;
            ctx.Fail("owner %d seed %u id 0x%x: (original/ours) thrown %d/%d ret 0x%x/0x%x trace %zu/%zu (first diff at %zu: 0x%x/0x%x) state diff at %zu (0x%x/0x%x)",
                     owner, seed, args.id, oa.thrown, ob.thrown, oa.ret, ob.ret, oa.trace.size(), ob.trace.size(), k,
                     k < oa.trace.size() ? oa.trace[k] : 0xdeadu, k < ob.trace.size() ? ob.trace[k] : 0xdeadu, j,
                     j < oa.state.size() ? oa.state[j] : 0xdeadu, j < ob.state.size() ? ob.state[j] : 0xdeadu);
        }
    }
}

} // namespace

DIFF_TEST(OperandReader_Read, 0x531700)
{
    OrigFn original = ctx.Original<OrigFn>(0x531700);
    void *savedService = *GameAddress<void *>(0x10cc45c);
    std::vector<int> owners;
    for (int o = -3; o <= 54; ++o) owners.push_back(o);
    owners.insert(owners.end(), {0x7fff, -32768, 100, 255, 256});
    const uint32_t seeds = std::getenv("U_SEEDS") ? static_cast<uint32_t>(std::atoi(std::getenv("U_SEEDS"))) : 6000;
    if (std::getenv("U_OWNER")) {
        owners.clear();
        owners.push_back(std::atoi(std::getenv("U_OWNER")));
    }
    for (int owner : owners) {
        Stats st;
        RunOwner(ctx, original, owner, seeds, st);
        if (std::getenv("U_VERBOSE"))
            std::printf("owner %3d: %u cases, %u returns, %u throws, %u skipped, %u bad\n", owner, st.cases, st.returns, st.throws,
                        st.skipped, st.bad);
    }
    if (gInjectWorld) std::printf("      U_INJECT_WORLD: %u world-range words in the random fill and the arguments\n", gInjected);
    *GameAddress<void *>(0x10cc45c) = savedService;
}
