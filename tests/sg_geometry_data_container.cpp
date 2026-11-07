// Differential tests for nGZSceneGraph::cIGeometryDataContainerSub (src/sg/cGeometryDataContainer.cpp), the
// secondary subobject of cGeometryDataContainer (vtable 0xe73224, at object + 0x2c), and for the subobject thunks of
// the second vtable (0xe73314). Most functions call nothing; the ones that call a virtual slot (SetRecordCDword, and
// the thunks whose bodies use QueryInterface, AddRef/Release, the primary key slot or the resource's slot 21) run
// against small fake objects whose slots log their calls in the arena, so the comparison covers those calls too.
//
// Each case builds one starting state: the 0x40-byte subobject (gObj) and a 16 KB arena that holds the record
// vectors, the uint16 array and the output buffer. The original and the replacement each run from that state; the
// compared outcome is the return value (full eax, or only al for the bool), the subobject bytes and the arena bytes.
#include "difftest.h"
#include "sg/cGeometryDataContainer.h"

#include <climits>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <random>

namespace {

using nGZSceneGraph::cIGeometryDataContainerSub;

constexpr uint32_t kObjSize = 0x40;
constexpr uint32_t kArenaSize = 0x5000;
constexpr uint32_t kRecords = 64;

// Arena offsets: the record vectors (kRecords each), the uint16 array and the output buffer.
constexpr uint32_t kBaseA = 0x0000; // 48-byte records (vector at subobject +0x04)
constexpr uint32_t kBaseB = 0x0c00; // 60-byte records (vector at +0x10)
constexpr uint32_t kBaseC = 0x1c00; // 76-byte records (vector at +0x1c)
constexpr uint32_t kBaseD = 0x3000; // 48-byte records (vector at +0x34)
constexpr uint32_t kWords = 0x3e00; // uint16 array of 64 elements
constexpr uint32_t kOut = 0x3f00;   // output buffer
constexpr uint32_t kBaseE = 0x4000; // 28-byte elements (vector at +0x28), 0x700 bytes
// Fake objects for the thunk tests (the thunk bodies' S is always below 0x3c00, so these never overlap it).
constexpr uint32_t kVbt = 0x4800;      // virtual base table: entry 1 is the displacement
constexpr uint32_t kResA = 0x4900;     // resource objects (vtable, refcount)
constexpr uint32_t kResB = 0x4910;
constexpr uint32_t kResC = 0x4920;
constexpr uint32_t kPeerA = 0x4940;    // QueryInterface peers (vtable, pointer returned, ok flag, release count)
constexpr uint32_t kPeerB = 0x4960;
constexpr uint32_t kProbeLog = 0x4a00; // what the fake slots were called with

struct State {
    uint8_t obj[kObjSize];
    uint8_t arena[kArenaSize];
};

alignas(16) uint8_t gObj[kObjSize];
alignas(16) uint8_t gArena[kArenaSize];
State gA, gB;

void Load(const State &s)
{
    std::memcpy(gObj, s.obj, sizeof(gObj));
    std::memcpy(gArena, s.arena, sizeof(gArena));
}

void Store(State &s)
{
    std::memcpy(s.obj, gObj, sizeof(gObj));
    std::memcpy(s.arena, gArena, sizeof(gArena));
}

uint32_t Addr(uint32_t arenaOff)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(gArena + arenaOff));
}

void Put32(uint8_t *p, uint32_t v)
{
    std::memcpy(p, &v, 4);
}

uint32_t Get32(const uint8_t *p)
{
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

void Randomize(std::mt19937 &gen, State &s)
{
    for (auto &x : s.obj) x = static_cast<uint8_t>(gen());
    for (auto &x : s.arena) x = static_cast<uint8_t>(gen());
}

// The vector at subobject +beginOff holds kRecords records of `stride` bytes starting at arena offset `base`.
void SetVector(State &s, uint32_t beginOff, uint32_t base, uint32_t stride)
{
    Put32(s.obj + beginOff, Addr(base));
    Put32(s.obj + beginOff + 4, Addr(base) + kRecords * stride);
}

// Capacity values for the std::string branch of c_str(): inline (<= 15) and heap (> 15) cases, edges included.
uint32_t PickCap(std::mt19937 &gen)
{
    const uint32_t caps[] = {0u, 7u, 15u, 16u, 31u, 0x7fffffffu, 0x80000000u, 0xffffffffu};
    return gen() % 2 ? caps[gen() % std::size(caps)] : gen();
}

// Sets the capacity dword at record offset `capOff` of every record (base, stride), so both c_str branches occur.
void SetCaps(std::mt19937 &gen, State &s, uint32_t base, uint32_t stride, uint32_t capOff)
{
    for (uint32_t r = 0; r < kRecords; ++r) Put32(s.arena + base + r * stride + capOff, PickCap(gen));
}

// The uint16 vectors of record B: (begin, end) pairs at +0x18, +0x24 and +0x30. Both equal and unequal pairs occur,
// with odd and negative differences too.
uint32_t PickEnd(std::mt19937 &gen, uint32_t begin)
{
    switch (gen() % 7) {
    case 0: return begin;
    case 1: return begin + 2;
    case 2: return begin + 1;
    case 3: return begin - 2;
    case 4: return begin + 2 * (gen() % 64);
    case 5: return begin - 4 * (gen() % 8);
    default: return gen();
    }
}

using Run = std::function<uint64_t(bool original)>;

// Runs the original and the replacement from `start` and compares the outcome. `run` returns the result widened to
// 64 bits, with only the defined bits kept (eax, or al for a bool).
void Check(difftest::Context &ctx, const char *name, const State &start, const Run &run)
{
    Load(start);
    uint64_t want = run(true);
    Store(gA);
    Load(start);
    uint64_t got = run(false);
    Store(gB);
    if (want != got) {
        ctx.Fail("%s: result original %llx ours %llx", name, static_cast<unsigned long long>(want),
                 static_cast<unsigned long long>(got));
    }
    if (std::memcmp(gA.obj, gB.obj, sizeof(gA.obj)) != 0) ctx.Fail("%s: subobject bytes differ", name);
    if (std::memcmp(gA.arena, gB.arena, sizeof(gA.arena)) != 0) ctx.Fail("%s: arena bytes differ", name);
    ++ctx.cases;
}

// Counting functions (signed (end - begin) / divisor of the vector at beginOff/endOff) on random and edge values.
void TestCount(difftest::Context &ctx, const char *name, uint32_t rva, uint32_t beginOff, uint32_t endOff,
               int32_t (cIGeometryDataContainerSub::*ours)())
{
    auto orig = ctx.Original<int32_t(__thiscall *)(void *)>(rva);
    std::mt19937 gen(rva);
    const uint32_t edges[][2] = {
        {0u, 0u},          {0u, 48u},         {0u, 47u},          {0u, 49u},        {0x1000u, 0x1000u + 60 * 3},
        {0x80000000u, 0x7fffffffu}, {0x7fffffffu, 0x80000000u}, {0xffffffffu, 0u}, {0u, 0xffffffffu}, {5u, 4u},
        {0u, 0x7fffffffu}, {0x80000000u, 0u}, {0xfffffffcu, 0x0c * 4u}};
    for (uint32_t i = 0; i < 20000; ++i) {
        State s;
        Randomize(gen, s);
        uint32_t begin = gen(), end = gen();
        if (i < std::size(edges)) {
            begin = edges[i][0];
            end = edges[i][1];
        } else if (i % 3 == 0) {
            end = begin + 48u * (gen() % 1000) - (gen() % 2 ? 0u : 1u);
        }
        Put32(s.obj + beginOff, begin);
        Put32(s.obj + endOff, end);
        Check(ctx, name, s, [&](bool useOrig) -> uint64_t {
            if (useOrig) return static_cast<uint32_t>(orig(gObj));
            return static_cast<uint32_t>((reinterpret_cast<cIGeometryDataContainerSub *>(gObj)->*ours)());
        });
    }
}

uint32_t Vt(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

uint32_t &ArenaDword(uint32_t off)
{
    return *reinterpret_cast<uint32_t *>(gArena + off);
}

// Fake vtables. Each slot logs what it was called with in the arena (or counts in the object), so the original and the
// replacement leave the same bytes behind when they make the same calls.
void *gSubVtable[60];      // the subobject's own vtable: slot 21 is the vf44_21 stand-in
void *gPrimaryVtable[8];   // the cSGResource object's primary vtable: slot 3 copies the object's 16-byte key
void *gResourceVtable[24]; // a resource: 1 AddRef, 2 Release, 21 the forwarded key call
void *gPeerVtable[4];      // a peer: 0 QueryInterface, 2 Release

void __fastcall FakeProbe(void *, void *, uint32_t a, uint32_t b, uint32_t c)
{
    ArenaDword(kProbeLog) += 1;
    ArenaDword(kProbeLog + 4) = a;
    ArenaDword(kProbeLog + 8) = b;
    ArenaDword(kProbeLog + 12) = c;
}

void *__fastcall FakePrimaryKey(void *self, void *, void *out)
{
    std::memcpy(out, static_cast<uint8_t *>(self) + 0x0c, 16);
    return out;
}

uint32_t __fastcall FakeAddRef(void *self, void *)
{
    return ++*reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(self) + 4);
}

uint32_t __fastcall FakeRelease(void *self, void *)
{
    return --*reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(self) + 4);
}

uint32_t __fastcall FakeForwardKey(void *, void *, void *key)
{
    const uint32_t *k = static_cast<const uint32_t *>(key);
    ArenaDword(kProbeLog) += 1;
    ArenaDword(kProbeLog + 4) = k[0];
    ArenaDword(kProbeLog + 8) = k[1];
    ArenaDword(kProbeLog + 12) = k[2];
    ArenaDword(kProbeLog + 16) = k[3];
    return k[0] ^ k[3];
}

// QueryInterface on a peer: *out is the peer's dword +8 (a null or peer address), the result is its dword +12 bit 0.
uint8_t __fastcall FakeQueryInterface(void *self, void *, const void *, void **out)
{
    const uint8_t *p = static_cast<const uint8_t *>(self);
    *out = reinterpret_cast<void *>(static_cast<uintptr_t>(Get32(p + 8)));
    return static_cast<uint8_t>(Get32(p + 12) & 1);
}

uint32_t __fastcall FakePeerRelease(void *self, void *)
{
    return ++*reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(self) + 16);
}

// Installs the fake slots once, before any test runs.
struct FakeVtables {
    FakeVtables()
    {
        gSubVtable[21] = reinterpret_cast<void *>(&FakeProbe);
        gPrimaryVtable[3] = reinterpret_cast<void *>(&FakePrimaryKey);
        gResourceVtable[1] = reinterpret_cast<void *>(&FakeAddRef);
        gResourceVtable[2] = reinterpret_cast<void *>(&FakeRelease);
        gResourceVtable[21] = reinterpret_cast<void *>(&FakeForwardKey);
        gPeerVtable[0] = reinterpret_cast<void *>(&FakeQueryInterface);
        gPeerVtable[2] = reinterpret_cast<void *>(&FakePeerRelease);
    }
} gFakeVtables;

// The thunks' `this` is T = gObj + 0x30, whose vtordisp (at T-4 = obj +0x2c) makes the adjusted pointer S = gArena +
// sOff: `sub ecx,[ecx-4]; sub ecx,0x50` gives T - (T - 0x50 - S) - 0x50 = S.
void *ThunkThis()
{
    return gObj + 0x30;
}

uint32_t PickS(std::mt19937 &gen)
{
    return 0x80 + gen() % (0x3c00 - 0x80);
}

void SetThunkBase(State &s, uint32_t sOff)
{
    Put32(s.obj + 0x2c, Vt(gObj + 0x30) - 0x50 - Addr(sOff));
}

// Peers A and B: a valid vtable, a random pointer-or-null at +8 (only peer addresses, so Release stays valid), a random
// ok bit at +12 and a release count at +16.
void SetPeers(std::mt19937 &gen, State &s)
{
    for (uint32_t off : {kPeerA, kPeerB}) {
        Put32(s.arena + off, Vt(gPeerVtable));
        uint32_t pick = gen() % 3;
        Put32(s.arena + off + 8, pick == 0 ? 0u : pick == 1 ? Addr(kPeerA) : Addr(kPeerB));
        Put32(s.arena + off + 12, gen());
        Put32(s.arena + off + 16, gen());
    }
}

// Resource objects A, B and C, each with the fake vtable and a random refcount.
void SetResources(std::mt19937 &gen, State &s)
{
    for (uint32_t off : {kResA, kResB, kResC}) {
        Put32(s.arena + off, Vt(gResourceVtable));
        Put32(s.arena + off + 4, gen() % 4);
    }
}

// 0 or one of the resource objects.
uint32_t PickResource(std::mt19937 &gen)
{
    switch (gen() % 4) {
    case 0: return 0;
    case 1: return Addr(kResA);
    case 2: return Addr(kResB);
    default: return Addr(kResC);
    }
}

} // namespace

// 0xa70e10 (24 bytes): (end - begin) / 48 for vector A (+0x04/+0x08).
DIFF_TEST(sg_geometry_count_records_a, 0xa70e10)
{
    TestCount(ctx, "sg_geometry_count_records_a", 0xa70e10, 0x04, 0x08, &cIGeometryDataContainerSub::CountRecordsA);
}

// 0xa70e30 (28 bytes): (end - begin) / 60 for vector B (+0x10/+0x14).
DIFF_TEST(sg_geometry_count_records_b, 0xa70e30)
{
    TestCount(ctx, "sg_geometry_count_records_b", 0xa70e30, 0x10, 0x14, &cIGeometryDataContainerSub::CountRecordsB);
}

// 0xa70e50 (24 bytes): (end - begin) / 48 for vector D (+0x34/+0x38).
DIFF_TEST(sg_geometry_count_records_d, 0xa70e50)
{
    TestCount(ctx, "sg_geometry_count_records_d", 0xa70e50, 0x34, 0x38, &cIGeometryDataContainerSub::CountRecordsD);
}

// 0xa70e70 (28 bytes): (end - begin) / 28 for the vector at +0x28/+0x2c.
DIFF_TEST(sg_geometry_count_elements_28, 0xa70e70)
{
    TestCount(ctx, "sg_geometry_count_elements_28", 0xa70e70, 0x28, 0x2c,
              &cIGeometryDataContainerSub::CountElements28);
}

// 0xa70e90 (24 bytes): (end - begin) / 76 for vector C (+0x1c/+0x20).
DIFF_TEST(sg_geometry_count_records_c, 0xa70e90)
{
    TestCount(ctx, "sg_geometry_count_records_c", 0xa70e90, 0x1c, 0x20, &cIGeometryDataContainerSub::CountRecordsC);
}

// 0xa70b30 (108 bytes, `ret 8`): record index of vector B, query `which` (0..3, anything else gives 0). Only al is
// compared. Every record gets random uint16 vectors and dwords, so all four branches occur.
DIFF_TEST(sg_geometry_record_has_words, 0xa70b30)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, uint32_t)>(0xa70b30);
    std::mt19937 gen(0xa70b30);
    for (uint32_t i = 0; i < 6000; ++i) {
        State s;
        Randomize(gen, s);
        SetVector(s, 0x10, kBaseB, 60);
        for (uint32_t r = 0; r < kRecords; ++r) {
            uint8_t *rec = s.arena + kBaseB + r * 60;
            for (uint32_t off : {0x18u, 0x24u, 0x30u}) {
                uint32_t begin = gen();
                Put32(rec + off, begin);
                Put32(rec + off + 4, PickEnd(gen, begin));
            }
            Put32(rec + 0x08, gen() % 2 ? uint32_t{gen() % 3} : (gen() % 2 ? 0x80000000u : gen()));
        }
        uint32_t index = i < 2 ? (i ? kRecords - 1 : 0u) : gen() % kRecords;
        uint32_t which = i % 9 == 0 ? 0xffffffffu : gen() % 6;
        Check(ctx, "sg_geometry_record_has_words", s, [&](bool useOrig) -> uint64_t {
            if (useOrig) return static_cast<uint8_t>(orig(gObj, index, which));
            return static_cast<uint8_t>(
                reinterpret_cast<cIGeometryDataContainerSub *>(gObj)->RecordHasWords(index, which));
        });
    }
}

// 0xa70d80 (29 bytes, `ret 4`): c_str() of the std::string at record +0x18 of vector D.
DIFF_TEST(sg_geometry_record_string_at_18, 0xa70d80)
{
    auto orig = ctx.Original<const char *(__thiscall *)(void *, uint32_t)>(0xa70d80);
    std::mt19937 gen(0xa70d80);
    for (uint32_t i = 0; i < 6000; ++i) {
        State s;
        Randomize(gen, s);
        SetVector(s, 0x34, kBaseD, 48);
        SetCaps(gen, s, kBaseD, 48, 0x18 + 0x14);
        uint32_t index = i < 2 ? (i ? kRecords - 1 : 0u) : gen() % kRecords;
        Check(ctx, "sg_geometry_record_string_at_18", s, [&](bool useOrig) -> uint64_t {
            const char *p = useOrig ? orig(gObj, index)
                                    : reinterpret_cast<cIGeometryDataContainerSub *>(gObj)->RecordStringAt18(index);
            return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p));
        });
    }
}

// 0xa70da0 (24 bytes, `ret 4`): c_str() of the std::string at record +0x00 of vector D.
DIFF_TEST(sg_geometry_record_string_at_00, 0xa70da0)
{
    auto orig = ctx.Original<const char *(__thiscall *)(void *, uint32_t)>(0xa70da0);
    std::mt19937 gen(0xa70da0);
    for (uint32_t i = 0; i < 6000; ++i) {
        State s;
        Randomize(gen, s);
        SetVector(s, 0x34, kBaseD, 48);
        SetCaps(gen, s, kBaseD, 48, 0x14);
        uint32_t index = i < 2 ? (i ? kRecords - 1 : 0u) : gen() % kRecords;
        Check(ctx, "sg_geometry_record_string_at_00", s, [&](bool useOrig) -> uint64_t {
            const char *p = useOrig ? orig(gObj, index)
                                    : reinterpret_cast<cIGeometryDataContainerSub *>(gObj)->RecordStringAt00(index);
            return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p));
        });
    }
}

// 0xa70d50 (39 bytes, `ret 8`): copies the 12-byte header of vector B's record to `out`; returns `out`.
DIFF_TEST(sg_geometry_copy_header_b, 0xa70d50)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, void *, uint32_t)>(0xa70d50);
    std::mt19937 gen(0xa70d50);
    for (uint32_t i = 0; i < 6000; ++i) {
        State s;
        Randomize(gen, s);
        SetVector(s, 0x10, kBaseB, 60);
        uint32_t index = i < 2 ? (i ? kRecords - 1 : 0u) : gen() % kRecords;
        uint32_t out = Addr(kOut + (gen() % 2 ? 0 : 0x10));
        Check(ctx, "sg_geometry_copy_header_b", s, [&](bool useOrig) -> uint64_t {
            void *p = useOrig ? orig(gObj, reinterpret_cast<void *>(static_cast<uintptr_t>(out)), index)
                              : reinterpret_cast<cIGeometryDataContainerSub *>(gObj)->CopyHeaderB(
                                    reinterpret_cast<void *>(static_cast<uintptr_t>(out)), index);
            return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p));
        });
    }
}

// 0xa71000 (69 bytes, `ret 8`): copies vector C's record to `out` and stores the c_str() of its string at out+0x14.
DIFF_TEST(sg_geometry_copy_record_c, 0xa71000)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, void *, uint32_t)>(0xa71000);
    std::mt19937 gen(0xa71000);
    for (uint32_t i = 0; i < 6000; ++i) {
        State s;
        Randomize(gen, s);
        SetVector(s, 0x1c, kBaseC, 76);
        SetCaps(gen, s, kBaseC, 76, 0x30);
        uint32_t index = i < 2 ? (i ? kRecords - 1 : 0u) : gen() % kRecords;
        uint32_t out = Addr(kOut);
        Check(ctx, "sg_geometry_copy_record_c", s, [&](bool useOrig) -> uint64_t {
            void *o = reinterpret_cast<void *>(static_cast<uintptr_t>(out));
            void *p = useOrig ? orig(gObj, o, index)
                              : reinterpret_cast<cIGeometryDataContainerSub *>(gObj)->CopyRecordC(o, index);
            return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p));
        });
    }
}

// 0xa708d0 (36 bytes, `ret 8`): copies the first 24 bytes of vector A's record to `out`; returns `out`.
DIFF_TEST(sg_geometry_copy_record_a24, 0xa708d0)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, void *, uint32_t)>(0xa708d0);
    std::mt19937 gen(0xa708d0);
    for (uint32_t i = 0; i < 6000; ++i) {
        State s;
        Randomize(gen, s);
        SetVector(s, 0x04, kBaseA, 48);
        uint32_t index = i < 2 ? (i ? kRecords - 1 : 0u) : gen() % kRecords;
        uint32_t out = Addr(kOut);
        Check(ctx, "sg_geometry_copy_record_a24", s, [&](bool useOrig) -> uint64_t {
            void *o = reinterpret_cast<void *>(static_cast<uintptr_t>(out));
            void *p = useOrig ? orig(gObj, o, index)
                              : reinterpret_cast<cIGeometryDataContainerSub *>(gObj)->CopyRecordA24(o, index);
            return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p));
        });
    }
}

// 0xa72d20 (23 bytes, `ret 8`): stores a dword at +0x14 of vector A's record.
DIFF_TEST(sg_geometry_set_record_a_dword, 0xa72d20)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t)>(0xa72d20);
    std::mt19937 gen(0xa72d20);
    for (uint32_t i = 0; i < 6000; ++i) {
        State s;
        Randomize(gen, s);
        SetVector(s, 0x04, kBaseA, 48);
        uint32_t index = i < 2 ? (i ? kRecords - 1 : 0u) : gen() % kRecords;
        uint32_t value = gen();
        Check(ctx, "sg_geometry_set_record_a_dword", s, [&](bool useOrig) -> uint64_t {
            if (useOrig) {
                orig(gObj, index, value);
            } else {
                reinterpret_cast<cIGeometryDataContainerSub *>(gObj)->SetRecordADword(index, value);
            }
            return 0;
        });
    }
}

// 0xa72fb0 (34 bytes, `ret 0xc`): stores a uint16 in the array at +0x0c of vector B's record.
DIFF_TEST(sg_geometry_set_record_b_word, 0xa72fb0)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t, uint16_t)>(0xa72fb0);
    std::mt19937 gen(0xa72fb0);
    for (uint32_t i = 0; i < 6000; ++i) {
        State s;
        Randomize(gen, s);
        SetVector(s, 0x10, kBaseB, 60);
        for (uint32_t r = 0; r < kRecords; ++r) Put32(s.arena + kBaseB + r * 60 + 0x0c, Addr(kWords));
        uint32_t index = i < 2 ? (i ? kRecords - 1 : 0u) : gen() % kRecords;
        uint32_t element = i < 2 ? (i ? 63u : 0u) : gen() % 64;
        uint16_t value = static_cast<uint16_t>(gen());
        Check(ctx, "sg_geometry_set_record_b_word", s, [&](bool useOrig) -> uint64_t {
            if (useOrig) {
                orig(gObj, index, element, value);
            } else {
                reinterpret_cast<cIGeometryDataContainerSub *>(gObj)->SetRecordBWord(index, element, value);
            }
            return 0;
        });
    }
}

// 0xa72dd0 (60 bytes, `ret 8`): stores a dword at +0x08 of vector B's record, then sets each uint16 vector's end to
// its begin when they differ.
DIFF_TEST(sg_geometry_set_record_b_and_trim, 0xa72dd0)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t)>(0xa72dd0);
    std::mt19937 gen(0xa72dd0);
    for (uint32_t i = 0; i < 6000; ++i) {
        State s;
        Randomize(gen, s);
        SetVector(s, 0x10, kBaseB, 60);
        for (uint32_t r = 0; r < kRecords; ++r) {
            uint8_t *rec = s.arena + kBaseB + r * 60;
            for (uint32_t off : {0x18u, 0x24u, 0x30u}) {
                uint32_t begin = gen();
                Put32(rec + off, begin);
                Put32(rec + off + 4, PickEnd(gen, begin));
            }
        }
        uint32_t index = i < 2 ? (i ? kRecords - 1 : 0u) : gen() % kRecords;
        uint32_t value = gen();
        Check(ctx, "sg_geometry_set_record_b_and_trim", s, [&](bool useOrig) -> uint64_t {
            if (useOrig) {
                orig(gObj, index, value);
            } else {
                reinterpret_cast<cIGeometryDataContainerSub *>(gObj)->SetRecordBAndTrim(index, value);
            }
            return 0;
        });
    }
}

// 0xa70eb0 (49 bytes, `ret 8`): copies the 28-byte element `index` (vector at +0x28) to `out`; returns `out`.
DIFF_TEST(sg_geometry_copy_element_28, 0xa70eb0)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, void *, uint32_t)>(0xa70eb0);
    std::mt19937 gen(0xa70eb0);
    for (uint32_t i = 0; i < 6000; ++i) {
        State s;
        Randomize(gen, s);
        SetVector(s, 0x28, kBaseE, 28);
        uint32_t index = i < 2 ? (i ? kRecords - 1 : 0u) : gen() % kRecords;
        uint32_t out = Addr(kOut + (gen() % 2 ? 0 : 0x10));
        Check(ctx, "sg_geometry_copy_element_28", s, [&](bool useOrig) -> uint64_t {
            void *o = reinterpret_cast<void *>(static_cast<uintptr_t>(out));
            void *p = useOrig ? orig(gObj, o, index)
                              : reinterpret_cast<cIGeometryDataContainerSub *>(gObj)->CopyElement28(o, index);
            return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p));
        });
    }
}

// 0xa700b0 (114 bytes, `ret 8`): `which` 0, 1, 2 trims one uint16 vector pair of record `index` of vector B; any other
// `which` stores nothing. The function returns void here (the original's eax on the default path is not compared).
DIFF_TEST(sg_geometry_trim_word_vector, 0xa700b0)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t)>(0xa700b0);
    std::mt19937 gen(0xa700b0);
    for (uint32_t i = 0; i < 6000; ++i) {
        State s;
        Randomize(gen, s);
        SetVector(s, 0x10, kBaseB, 60);
        for (uint32_t r = 0; r < kRecords; ++r) {
            uint8_t *rec = s.arena + kBaseB + r * 60;
            for (uint32_t off : {0x18u, 0x24u, 0x30u}) {
                uint32_t begin = gen();
                Put32(rec + off, begin);
                Put32(rec + off + 4, PickEnd(gen, begin));
            }
        }
        uint32_t index = i < 2 ? (i ? kRecords - 1 : 0u) : gen() % kRecords;
        uint32_t which = i % 7 == 0 ? 0xffffffffu : gen() % 5;
        Check(ctx, "sg_geometry_trim_word_vector", s, [&](bool useOrig) -> uint64_t {
            if (useOrig) {
                orig(gObj, index, which);
            } else {
                reinterpret_cast<cIGeometryDataContainerSub *>(gObj)->TrimWordVector(index, which);
            }
            return 0;
        });
    }
}

// 0xa72e70 (41 bytes, `ret 0x10`): calls slot 21 of [this] with (index, a, b), then stores `value` at the dword +0x10 of
// vector C's record `index`. The subobject's vtable is the fake one, so the call is logged in the arena.
DIFF_TEST(sg_geometry_set_record_c_dword, 0xa72e70)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t)>(0xa72e70);
    std::mt19937 gen(0xa72e70);
    for (uint32_t i = 0; i < 6000; ++i) {
        State s;
        Randomize(gen, s);
        Put32(s.obj, Vt(gSubVtable));
        SetVector(s, 0x1c, kBaseC, 76);
        uint32_t index = i < 2 ? (i ? kRecords - 1 : 0u) : gen() % kRecords;
        uint32_t a = gen(), b = gen(), value = gen();
        Check(ctx, "sg_geometry_set_record_c_dword", s, [&](bool useOrig) -> uint64_t {
            if (useOrig) {
                orig(gObj, index, a, b, value);
            } else {
                reinterpret_cast<cIGeometryDataContainerSub *>(gObj)->SetRecordCDword(index, a, b, value);
            }
            return 0;
        });
    }
}

// Thunk tests. The thunk's `this` is ThunkThis() (gObj + 0x30) with the vtordisp set by SetThunkBase, so the body gets
// the random object state at S = gArena + sOff.

// 0xa72d0f (vf128_16, body SetFlag, ret 4): stores the low byte of the argument at S - 7.
DIFF_TEST(sg_geometry_vb_thunk_set_flag, 0xa72d0f)
{
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0xa72d0f);
    std::mt19937 gen(0xa72d0f);
    for (uint32_t i = 0; i < 4000; ++i) {
        State s;
        Randomize(gen, s);
        SetThunkBase(s, PickS(gen));
        uint32_t value = gen();
        Check(ctx, "sg_geometry_vb_thunk_set_flag", s, [&](bool useOrig) -> uint64_t {
            if (useOrig) {
                orig(ThunkThis(), value);
            } else {
                nGZSceneGraph::VbThunkSetFlag(ThunkThis(), nullptr, static_cast<uint8_t>(value));
            }
            return 0;
        });
    }
}

// 0xa72c76 (vf128_9, body GetVirtualBase, ret 4): the virtual base (vbptr at S - 0x14, entry 1) when the argument is
// null and S - 0x30 is not null; otherwise null.
DIFF_TEST(sg_geometry_vb_thunk_get_virtual_base, 0xa72c76)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, void *)>(0xa72c76);
    std::mt19937 gen(0xa72c76);
    for (uint32_t i = 0; i < 4000; ++i) {
        State s;
        Randomize(gen, s);
        uint32_t sOff = PickS(gen);
        SetThunkBase(s, sOff);
        Put32(s.arena + sOff - 0x14, Addr(kVbt));
        Put32(s.arena + kVbt + 4, gen());
        uint32_t arg = gen() % 2 ? 0u : Addr(kPeerA);
        Check(ctx, "sg_geometry_vb_thunk_get_virtual_base", s, [&](bool useOrig) -> uint64_t {
            void *a = reinterpret_cast<void *>(static_cast<uintptr_t>(arg));
            void *p = useOrig ? orig(ThunkThis(), a) : nGZSceneGraph::VbThunkGetVirtualBase(ThunkThis(), nullptr, a);
            return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p));
        });
    }
}

// 0xa70bac (vf128_11, body CompareIdentity, ret 4): QueryInterface on `other` and on the virtual base (both fake peers).
DIFF_TEST(sg_geometry_vb_thunk_compare_identity, 0xa70bac)
{
    auto orig = ctx.Original<int32_t(__thiscall *)(void *, void *)>(0xa70bac);
    std::mt19937 gen(0xa70bac);
    for (uint32_t i = 0; i < 4000; ++i) {
        State s;
        Randomize(gen, s);
        uint32_t sOff = PickS(gen);
        SetThunkBase(s, sOff);
        SetPeers(gen, s);
        Put32(s.arena + sOff - 0x14, Addr(kVbt));
        uint32_t base = gen() % 2 ? Addr(kPeerA) : Addr(kPeerB);
        Put32(s.arena + kVbt + 4, base - Addr(sOff) + 0x14);
        uint32_t other = gen() % 2 ? Addr(kPeerA) : Addr(kPeerB);
        Check(ctx, "sg_geometry_vb_thunk_compare_identity", s, [&](bool useOrig) -> uint64_t {
            void *o = reinterpret_cast<void *>(static_cast<uintptr_t>(other));
            int32_t r = useOrig ? orig(ThunkThis(), o) : nGZSceneGraph::VbThunkCompareIdentity(ThunkThis(), nullptr, o);
            return static_cast<uint32_t>(r);
        });
    }
}

// 0xa72c98 (vf128_22, body CopyKeyTo, ret 4): the primary slot 3 (fake: copies the key at object + 0x0c) fills `out`.
DIFF_TEST(sg_geometry_vb_thunk_copy_key_to, 0xa72c98)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *, void *)>(0xa72c98);
    std::mt19937 gen(0xa72c98);
    for (uint32_t i = 0; i < 4000; ++i) {
        State s;
        Randomize(gen, s);
        uint32_t sOff = PickS(gen);
        SetThunkBase(s, sOff);
        Put32(s.arena + sOff - 0x30, Vt(gPrimaryVtable));
        uint32_t out = Addr(kOut + (gen() % 2 ? 0 : 0x10));
        Check(ctx, "sg_geometry_vb_thunk_copy_key_to", s, [&](bool useOrig) -> uint64_t {
            void *o = reinterpret_cast<void *>(static_cast<uintptr_t>(out));
            void *p = useOrig ? orig(ThunkThis(), o) : nGZSceneGraph::VbThunkCopyKeyTo(ThunkThis(), nullptr, o);
            return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p));
        });
    }
}

// 0xa72eee (vf128_15, body SetResource, ret 4): stores the new resource at S - 0x0c, AddRef on it (slot 1) and Release on
// the old one (slot 2); nothing when the pointers are equal. Returns true (al only).
DIFF_TEST(sg_geometry_vb_thunk_set_resource, 0xa72eee)
{
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0xa72eee);
    std::mt19937 gen(0xa72eee);
    for (uint32_t i = 0; i < 4000; ++i) {
        State s;
        Randomize(gen, s);
        uint32_t sOff = PickS(gen);
        SetThunkBase(s, sOff);
        SetResources(gen, s);
        uint32_t old = PickResource(gen);
        Put32(s.arena + sOff - 0x0c, old);
        uint32_t fresh = gen() % 3 == 0 ? old : PickResource(gen);
        Check(ctx, "sg_geometry_vb_thunk_set_resource", s, [&](bool useOrig) -> uint64_t {
            void *r = reinterpret_cast<void *>(static_cast<uintptr_t>(fresh));
            uint8_t v = useOrig ? orig(ThunkThis(), r) : nGZSceneGraph::VbThunkSetResource(ThunkThis(), nullptr, r);
            return v;
        });
    }
}
