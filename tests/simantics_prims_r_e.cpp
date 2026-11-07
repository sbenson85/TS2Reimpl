// Differential test for the SimAntics primitive handler of opcode 0x0e (0x58a2c0, PrimRObject::Prim0E).
// Framework and notes: tests/simantics_prims_mid.h, tests/simantics_prims_r.h.
//
// The handler walks a std::list-like container of objects with the original's small iterator helpers (0x5a98e0,
// 0x5a9bd0, 0x57cbb0, 0x57cca0, 0x57cd00, 0x57c330, ...): the test builds a real list in static memory and lets
// them run. The "allowed numbers" hash set is library code on both sides (the original's ctor 0x57c3d0, find 0x55db30,
// destructors 0x38eef0/0x1fcdb0 run for real; ours is a std::unordered_set), and the own interface's slot 150 fills it on
// both sides with the game's own insert (0x420bd0, which grows it through the rehash 0x42d860), as cTSPerson's slot 150
// does in the game. The set's memory comes from the game's allocator service (svcfx::AllocatorScope), so a set of ours
// with any other allocator fails here as it crashed in the game (lane WE: std::allocator's bucket vector freed by the
// game's rehash through the general allocator). The position-vector helpers (0x7dcb60, 0x7dd620) and the two service
// getters (0x212e61, 0x20e0b2) are redirected to logging stand-ins.
#include "simantics_prims_r.h"
#include "simantics/SimGeometry.h"
#include "ts_services_fixture.h" // AllocatorScope

namespace {

// ---- containers in static memory ---------------------------------------------------------------------------------

int32_t gCandidateIds[2];  // the candidates' numbers (slot 131 as the handler reads them)
uint32_t gListObject[4];  // the list: +4 holds the head node
uint32_t gNodes[8][3];    // {next, prev, value}
uint16_t *gStackVec[2];   // the stack object's vector of numbers: begin, end

void *gSvc1 = nullptr, *gSvc2 = nullptr;

// ---- stand-ins -----------------------------------------------------------------------------------------------------

void *__cdecl Stub212e61()
{
    gTrace.push_back(0x212e61u);
    return gSvc1;
}

void *__cdecl Stub20e0b2()
{
    gTrace.push_back(0x20e0b2u);
    return gSvc2;
}

// 0x7dcb60 and 0x7dd620 (cdecl, two pointers): copy a vector through; the stand-ins log and propagate the first word.
void __cdecl Stub7dcb60(uint32_t *out, const uint32_t *in)
{
    gTrace.push_back(0x7dcb60u);
    gTrace.push_back(in[0]);
    out[0] = in[0];
    out[1] = in[0] ^ 0x1234u;
    out[2] = 7;
}

void __cdecl Stub7dd620(uint32_t *out, const uint32_t *in)
{
    gTrace.push_back(0x7dd620u);
    gTrace.insert(gTrace.end(), {in[0], in[1], in[2]});
    out[0] = in[0];
    out[1] = in[1] + 1;
    out[2] = in[2];
}

// ---- fake slots --------------------------------------------------------------------------------------------------

// The stack object's slot 265: the static vector (or none).
uint32_t __fastcall Slot265(void *self, void *, uint32_t arg)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(265u << 8 | 1u);
    gTrace.push_back(arg);
    return gW->bytes[0x3e] ? 0u : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gStackVec));
}

uint32_t __fastcall Registry32(void *self, void *, uint32_t function)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(32u << 8 | 1u);
    gTrace.push_back(function);
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gListObject));
}

// Slot 0 of this: (iid, out) -> the scripted own interface.
uint32_t __fastcall QiPlain2(void *self, void *, uint32_t iid, void **out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(0u << 8 | 2u);
    gTrace.push_back(iid);
    gTrace.push_back(out ? 1u : 0u);
    if (out) *out = reinterpret_cast<void *>(static_cast<uintptr_t>(Scripted(self, 0x500)));
    return Scripted(self, 0x501);
}

// This's data cells: slot 14 stores (cell, value), slot 13 reads (cell) -- cell 0x42 lives in scratch word 3.
uint32_t __fastcall This14(void *self, void *, uint32_t cell, uint32_t value)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(14u << 8 | 2u);
    gTrace.insert(gTrace.end(), {cell, value});
    if (cell == 0x42) gW->scratch[3] = value;
    return 0;
}

uint32_t __fastcall This13(void *self, void *, uint32_t cell)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(13u << 8 | 1u);
    gTrace.push_back(cell);
    return cell == 0x42 ? gW->scratch[3] : Mix(gSeed, 13, cell);
}

uint32_t __fastcall This19c(void *self, void *, uint32_t arg)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(19u << 8 | 1u);
    gTrace.push_back(arg);
    return Mix(gSeed, 19, arg);
}

// A candidate's slot 13 (data cell read): biased so the filters pass often enough.
uint32_t __fastcall Cand13(void *self, void *, uint32_t cell)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(13u << 8 | 1u);
    gTrace.push_back(cell);
    const uint32_t h = Mix(gSeed, Off(self), cell);
    switch (cell) {
    case 0x38: return h % 5 == 0 ? h : (h & ~5u);
    case 0x6b: return h % 5 == 0 ? h : (h & 0xffff0000u);
    case 0x19: return h % 3 == 0 ? (h & 0xffff) : 0u;
    default: return h;
    }
}

uint32_t __fastcall Cand25(void *self, void *, uint32_t flags)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(25u << 8 | 1u);
    gTrace.push_back(flags);
    return Mix(gSeed, Off(self), flags) % 7 == 0 ? 1u : (Mix(gSeed, Off(self), flags) & ~0xffu);
}

uint32_t __fastcall Cand190(void *self, void *, uint32_t arg)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(190u << 8 | 1u);
    gTrace.push_back(arg);
    return Mix(gSeed, Off(self), 190) % 7 == 0 ? 1u : (Mix(gSeed, Off(self), 190) & ~0xffu);
}

// Slots 101 (one pointer, filled with a number) and 42 (one pointer, filled with three floats and returned).
uint32_t __fastcall Cand101(void *self, void *, uint32_t *out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(101u << 8 | 1u);
    *out = Mix(gSeed, Off(self), 101);
    return 0;
}

float *__fastcall Pos42(void *self, void *, float *out)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(42u << 8 | 1u);
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t h = Mix(gSeed, Off(self), 420 + i);
        out[i] = static_cast<float>(static_cast<int32_t>(h % 2001) - 1000) / 8.0f;
    }
    return out;
}

// The own interface: slot 150 (&set, number), slots 79 and 78 returning floats.
uint32_t __fastcall Own150(void *self, void *, void *set, int32_t number)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(150u << 8 | 2u);
    gTrace.push_back(set ? 1u : 0u);
    gTrace.push_back(static_cast<uint32_t>(number));
    if (set) {
        // The game's insert (0x420bd0, __thiscall, ret 8: out {node, inserted}, &key), as cTSPerson's slot 150 fills it.
        // Each candidate is allowed or not by the seed; up to 12 other numbers (outside the int16 range the candidates'
        // come from) make the set rehash past its first 8 buckets, so its first bucket vector is freed by the game's code.
        using Insert = void(__thiscall *)(void *, uint32_t *, const int32_t *);
        const auto insert = reinterpret_cast<Insert>(GameAddress<void>(0x420bd0));
        uint32_t result[2];
        for (const int32_t id : gCandidateIds)
            if (Mix(gSeed, static_cast<uint32_t>(id), 55) % 2) insert(set, result, &id);
        const uint32_t others = Mix(gSeed, 7, 150) % 13;
        for (uint32_t i = 0; i < others; ++i) {
            const int32_t id = static_cast<int32_t>(0x10000u + i * 0x101u);
            insert(set, result, &id);
        }
    }
    return Scripted(self, 150);
}

float __fastcall Own79(void *self, void *, uint32_t object)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(79u << 8 | 1u);
    gTrace.push_back(Norm(object));
    float f = static_cast<float>(static_cast<int32_t>(Mix(gSeed, Off(self), 79) % 41) - 10) / 4.0f;
    return f;
}

float __fastcall Own78(void *self, void *, uint32_t object, uint32_t zero)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(78u << 8 | 2u);
    gTrace.push_back(Norm(object));
    gTrace.push_back(zero);
    float f = static_cast<float>(static_cast<int32_t>(Mix(gSeed, Off(self), 78 + (Norm(object) & 0xff)) % 21) - 4) / 2.0f;
    return f;
}

// The secondary slot 15 (eight stack arguments).
uint32_t __fastcall Sec15(void *self, void *, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6, uint32_t a7,
                          uint32_t a8)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(15u << 8 | 8u);
    gTrace.insert(gTrace.end(), {a1, a2, a3, a4, a5, a6, a7, a8});
    return Mix(gSeed, 15, a2) % 4 != 0 ? 1u : (Mix(gSeed, 15, a3) & ~0xffu);
}

// The position service's slot 19: (best position, candidate position, 0, 0, 0): both pointers are logged by value.
uint32_t __fastcall Svc19(void *self, void *, const float *a, const float *b, uint32_t z1, uint32_t z2, uint32_t z3)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(19u << 8 | 5u);
    for (int i = 0; i < 3; ++i) {
        uint32_t bits;
        std::memcpy(&bits, &a[i], 4);
        gTrace.push_back(bits);
    }
    for (int i = 0; i < 3; ++i) {
        uint32_t bits;
        std::memcpy(&bits, &b[i], 4);
        gTrace.push_back(bits);
    }
    gTrace.insert(gTrace.end(), {z1, z2, z3});
    return Mix(gSeed, 19, 5) % 3 == 0 ? 1u : 0u;
}

// The service object's slot 80: (dest vector, 0, 1) -> a number; the vector is logged by value.
uint32_t __fastcall Svc80(void *self, void *, const uint32_t *dest, uint32_t a, uint32_t b)
{
    Tick();
    gTrace.push_back(Off(self));
    gTrace.push_back(80u << 8 | 3u);
    gTrace.insert(gTrace.end(), {dest[0], dest[1], dest[2], a, b});
    return Mix(gSeed, 80, dest[0]) % 3 == 0 ? 0u : (Mix(gSeed, 80, dest[1]) | 1u);
}

const uint32_t kKinds[] = {0, 1, 2, 3, 6, 7, 8, 10, 11, 12, 13, 14, 15, 23, 25, 27};

void Build0E(World &w, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 505);
    Base(w, seed, r);
    SetParam(w, r);
    ClearQueues();
    Fake &ctx = w.fk[0], &frame = w.fk[1], &table = w.fk[2], &registry = w.fk[3], &own = w.fk[4], &operand = w.fk[5], &posSvc = w.fk[6],
         &svc1 = w.fk[7], &svc1inner = w.fk[8], &svc2 = w.fk[9], &type = w.fk[10], &entry = w.fk[11], &candA = w.fk[12], &candB = w.fk[13];
    gSvc1 = &svc1;
    gSvc2 = &svc2;

    const uint32_t kind = (r() % 12 == 0) ? static_cast<uint32_t>(r() % 40) - 3 : kKinds[r() % (sizeof(kKinds) / sizeof(kKinds[0]))];
    PutWord(w, 4, static_cast<uint16_t>(kind));
    w.param[6] = static_cast<uint8_t>(r());
    if (r() % 2) w.param[6] &= static_cast<uint8_t>(r());
    w.param[0xa] = static_cast<uint8_t>(r());
    const uint8_t f6 = w.param[6];
    // The own interface's answer: never missing for the kinds that dereference it unchecked.
    const bool ownMissing = (r() % 5 == 0) && kind != 23 && kind != 27;
    (void)f6;

    Use(ctx.vt, 26, 0);
    RetObject(&ctx, 26, &table, w);
    table.vt[29] = reinterpret_cast<void *>(&Q1<29>);
    QObj(29, (r() % 6 == 0) ? nullptr : &operand, w);
    QObj(29, (r() % 6 == 0) ? nullptr : &operand, w);

    // This.
    w.vtThis[19] = reinterpret_cast<void *>(&This19c);
    Use(w.vtThis, 47, 0);
    RetValue(&w.obj, 47, Wide16(r), w);
    Use(w.vtThis, 131, 0);
    RetValue(&w.obj, 131, Wide16(r), w);
    w.vtThis[0] = reinterpret_cast<void *>(&QiPlain2);
    RetObject(&w.obj, 0x500, ownMissing ? nullptr : &own, w);
    RetValue(&w.obj, 0x501, ownMissing ? 0u : 1u, w);
    w.vtThis[14] = reinterpret_cast<void *>(&This14);
    w.vtThis[13] = reinterpret_cast<void *>(&This13);
    w.vtThis[128] = reinterpret_cast<void *>(&Dist128);
    RetValue(&w.obj, 128, FloatBits(RandomFloat(r)), w);
    w.vtThis[42] = reinterpret_cast<void *>(&Pos42);
    w.vtSec[15] = reinterpret_cast<void *>(&Sec15);
    w.vtSec[7] = reinterpret_cast<void *>(&ReadQ);
    gReadQ = {Wide16(r), Wide16(r), Wide16(r)};
    {
        const size_t vtOffset = reinterpret_cast<uint8_t *>(w.vtThis) - reinterpret_cast<uint8_t *>(&w.obj);
        const size_t index = (0x1c4 - vtOffset) / 4;
        for (int s : {0, 13, 14, 19, 42, 47, 128, 131}) {
            if (index == static_cast<size_t>(s)) w.canary = 0;
        }
        *reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(&w.obj) + 0x1c4) = &registry;
    }
    registry.vt[32] = reinterpret_cast<void *>(&Registry32);

    // The operand object (also the stack object): numbers, position, distance and the number vector.
    Use(operand.vt, 47, 0);
    RetValue(&operand, 47, Wide16(r), w);
    Use(operand.vt, 131, 0);
    RetValue(&operand, 131, Wide16(r), w);
    operand.vt[42] = reinterpret_cast<void *>(&Pos42);
    operand.vt[128] = reinterpret_cast<void *>(&Dist128);
    RetValue(&operand, 128, FloatBits(RandomFloat(r)), w);
    operand.vt[265] = reinterpret_cast<void *>(&Slot265);
    w.bytes[0x3e] = (r() % 8 == 0) ? 1 : 0; // the stack object has no vector (error 0x6c)

    // The own interface.
    own.vt[150] = reinterpret_cast<void *>(&Own150);
    RetValue(&own, 150, (r() % 3 == 0) ? Pick(r, {0, 0x100}) : Pick(r, {1, 0xff}), w);
    Use(own.vt, 59, 1);
    Use(own.vt, 46, 0);
    RetValue(&own, 46, (r() % 2) ? Pick(r, {0, 0x100}) : Pick(r, {1, 0xff}), w);
    own.vt[79] = reinterpret_cast<void *>(&Own79);
    own.vt[78] = reinterpret_cast<void *>(&Own78);
    Use(own.vt, 2, 0);

    // Services.
    Use(svc1.vt, 39, 0);
    RetObject(&svc1, 39, &svc1inner, w);
    Use(svc1inner.vt, 40, 1);
    RetObject(&svc1inner, 40, (r() % 4 == 0) ? nullptr : &posSvc, w);
    posSvc.vt[19] = reinterpret_cast<void *>(&Svc19);
    Use(svc2.vt, 39, 0);
    RetObject(&svc2, 39, &svc2, w);
    svc2.vt[80] = reinterpret_cast<void *>(&Svc80);

    // The candidates.
    Use(type.vt, 11, 1);
    RetObject(&type, 11, (r() % 6 == 0) ? nullptr : &entry, w);
    Use(entry.vt, 6, 0);
    RetValue(&entry, 6, (r() % 3 == 0) ? 0u : Wide16(r), w);
    Use(entry.vt, 8, 0);
    RetValue(&entry, 8, (r() % 3 == 0) ? 0u : (Wide16(r) | 1u), w);
    for (Fake *c : {&candA, &candB}) {
        Use(c->vt, 1, 0);
        Use(c->vt, 2, 0);
        c->vt[25] = reinterpret_cast<void *>(&Cand25);
        c->vt[13] = reinterpret_cast<void *>(&Cand13);
        Use(c->vt, 131, 0);
        RetValue(c, 131, Wide16(r), w);
        Use(c->vt, 129, 0);
        RetValue(c, 129, (r() % 8 == 0) ? 1u : 0u, w);
        c->vt[190] = reinterpret_cast<void *>(&Cand190);
        Use(c->vt, 135, 0);
        RetValue(c, 135, (r() % 8 == 0) ? 1u : 0u, w);
        Use(c->vt, 91, 0);
        RetObject(c, 91, &type, w);
        c->vt[101] = reinterpret_cast<void *>(&Cand101);
        Use(c->vt, 47, 0);
        RetValue(c, 47, Wide16(r), w);
        Use(c->vt, 89, 0);
        RetValue(c, 89, static_cast<uint32_t>(r()), w);
        c->vt[42] = reinterpret_cast<void *>(&Pos42);
    }

    // The list: up to five elements (candidates and nulls); the stack vector.
    const uint32_t count = r() % 6;
    for (uint32_t i = 0; i < 8; ++i) w.bytes[i] = 0xff;
    for (uint32_t i = 0; i < count; ++i) w.bytes[i] = static_cast<uint8_t>(r() % 6 == 0 ? 0xff : r() % 2);
    w.bytes[0x3c] = static_cast<uint8_t>(count);
    // Stack vector numbers: the candidates' own numbers sometimes, others at random.
    const uint32_t vecCount = r() % 5;
    uint16_t *vec = reinterpret_cast<uint16_t *>(w.counters);
    for (uint32_t i = 0; i < 16; ++i) vec[i] = static_cast<uint16_t>(r());
    w.bytes[0x3d] = static_cast<uint8_t>(vecCount);
    // (a candidate's number can be put in the vector: slot 131 values are scripted, read back through Scripted)
    // done in beforeRun, where the world is final

    // Frame.
    Use(frame.vt, 13, 1);
}

void BeforeRun0E(World &w)
{
    ResetQueues();
    gSvc1 = &w.fk[7];
    gSvc2 = &w.fk[9];
    // Rebuild the list for this world.
    const uint32_t count = w.bytes[0x3c];
    gListObject[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gNodes[0]));
    for (uint32_t i = 0; i <= count; ++i) {
        gNodes[i][0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gNodes[i == count ? 0 : i + 1]));
        gNodes[i][1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&gNodes[i == 0 ? count : i - 1]));
        gNodes[i][2] = 0;
    }
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t which = w.bytes[i];
        gNodes[i + 1][2] = which == 0xff ? 0 : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&w.fk[12 + (which & 1)]));
    }
    for (uint32_t i = 0; i < 2; ++i) gCandidateIds[i] = static_cast<int16_t>(Scripted(&w.fk[12 + i], 131));
    // The stack vector.
    gStackVec[0] = reinterpret_cast<uint16_t *>(w.counters);
    gStackVec[1] = reinterpret_cast<uint16_t *>(w.counters) + w.bytes[0x3d];
    // Put candidate numbers into the vector now and then (so the vector search finds something).
    if (w.bytes[0x3d] > 0 && (Mix(gSeed, 3, 3) % 2)) {
        const uint32_t v = Scripted(&w.fk[12], 131);
        reinterpret_cast<uint16_t *>(w.counters)[Mix(gSeed, 4, 4) % w.bytes[0x3d]] = static_cast<uint16_t>(v);
    }
}

} // namespace

DIFF_TEST(PrimR_Prim0E, 0x58a2c0)
{
    svcfx::AllocatorScope allocator; // the visited set's memory: the game's allocator service on both sides
    CodePatch p5(ctx, 0x212e61, reinterpret_cast<void *>(&Stub212e61));
    CodePatch p6(ctx, 0x20e0b2, reinterpret_cast<void *>(&Stub20e0b2));
    CodePatch p7(ctx, 0x7dcb60, reinterpret_cast<void *>(&Stub7dcb60));
    CodePatch p8(ctx, 0x7dd620, reinterpret_cast<void *>(&Stub7dd620));
    // Both are ours now (simgeom::TileFromBytes / TileCentre), reached directly: the same stand-ins sit over them.
    CodePatch p9(reinterpret_cast<void *>(&simgeom::TileFromBytes), reinterpret_cast<void *>(&Stub7dcb60));
    CodePatch p10(reinterpret_cast<void *>(&simgeom::TileCentre), reinterpret_cast<void *>(&Stub7dd620));

    gBeforeRun = BeforeRun0E;
    Stats st = RunHandler(ctx, "Prim0E", 0x58a2c0, 60000, Build0E,
                          [](PrimMidObject *o, void *f, const PrimParamView *p) {
                              return reinterpret_cast<PrimRObject *>(o)->Prim0E(f, p);
                          });
    gBeforeRun = nullptr;
    if (st.throws < 100 || st.byRet[0] < 3000 || st.byRet[1] < 1000)
        ctx.Fail("coverage: %u throws, returns 0/1: %u/%u", st.throws, st.byRet[0], st.byRet[1]);
}
