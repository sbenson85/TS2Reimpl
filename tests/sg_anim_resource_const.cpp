// Differential tests for the cIAnimResourceConst subobject of nGZSceneDBAnim::cAnimResourceConst (subobject at
// object +0x2c, vtable 0xe66470). Each trial builds one random state twice: the original runs on the first copy
// and our function on the second. A side is an object (pointer fields set to that side's own record and entry
// array), the record and the entry array, with canaries around the object. Results that are pointers are
// compared as offsets from the side's own record or entry array, so the two sides compare equal.
#include "difftest.h"
#include "game/vtable.h"
#include "sg/cAnimResourceConst.h"

#include <cstring>
#include <random>
#include <utility>
#include <vector>

namespace {

using Sub = nGZSceneDBAnim::cAnimResourceConstSubobject;

constexpr int kEntries = 8;
constexpr int kObjectSize = 0x60;

// The state both sides start from.
struct State {
    uint8_t object[kObjectSize];
    uint8_t record[0x40];
    uint8_t entries[kEntries * 28];
};

// One side: canaries around the object, then the object, the record and the entry array.
struct Side {
    uint8_t front[32];
    uint8_t object[kObjectSize];
    uint8_t back[32];
    uint8_t record[0x40];
    uint8_t entries[kEntries * 28];
};

State RandomState(std::mt19937 &rng)
{
    State st;
    for (auto &b : st.object) b = static_cast<uint8_t>(rng());
    for (auto &b : st.record) b = static_cast<uint8_t>(rng());
    for (auto &b : st.entries) b = static_cast<uint8_t>(rng());
    return st;
}

void StorePtr(uint8_t *at, const void *p)
{
    uintptr_t v = reinterpret_cast<uintptr_t>(p);
    std::memcpy(at, &v, sizeof(v));
}

void Build(Side &s, const State &st)
{
    std::memset(s.front, 0xA5, sizeof(s.front));
    std::memset(s.back, 0x5A, sizeof(s.back));
    std::memcpy(s.object, st.object, sizeof(s.object));
    std::memcpy(s.record, st.record, sizeof(s.record));
    std::memcpy(s.entries, st.entries, sizeof(s.entries));
    // The record pointer at subobject +0x18 (object +0x44) and the entry array pointer at +0x1c (object +0x48).
    StorePtr(s.object + 0x44, s.record);
    StorePtr(s.object + 0x48, s.entries);
}

// The subobject: `this` for every call (object + 0x2c).
Sub *SubOf(Side &s)
{
    return reinterpret_cast<Sub *>(s.object + 0x2c);
}

// Object bytes with the two pointer fields zeroed (they are checked separately), then the record and entries.
std::vector<uint8_t> Snapshot(const Side &s)
{
    std::vector<uint8_t> out(s.object, s.object + sizeof(s.object));
    std::memset(out.data() + 0x44, 0, 8);
    out.insert(out.end(), s.record, s.record + sizeof(s.record));
    out.insert(out.end(), s.entries, s.entries + sizeof(s.entries));
    return out;
}

bool PointersIntact(const Side &s)
{
    uintptr_t rec = 0, ent = 0;
    std::memcpy(&rec, s.object + 0x44, sizeof(rec));
    std::memcpy(&ent, s.object + 0x48, sizeof(ent));
    return rec == reinterpret_cast<uintptr_t>(s.record) && ent == reinterpret_cast<uintptr_t>(s.entries);
}

bool CanariesIntact(const Side &s)
{
    for (uint8_t b : s.front)
        if (b != 0xA5) return false;
    for (uint8_t b : s.back)
        if (b != 0x5A) return false;
    return true;
}

// Compares the object, record and entry bytes after the call, the pointer fields and the canaries.
void CheckState(difftest::Context &ctx, const char *name, const Side &a, const Side &b)
{
    if (Snapshot(a) != Snapshot(b)) ctx.Fail("%s: object or record state differs", name);
    if (!PointersIntact(a) || !PointersIntact(b)) ctx.Fail("%s: pointer fields changed", name);
    if (!CanariesIntact(a) || !CanariesIntact(b)) ctx.Fail("%s: canaries overwritten", name);
}

// Member function pointers of this class are plain code addresses (no virtual or multiple inheritance).
template <typename T> const void *Addr(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance member pointers only");
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

template <typename Fn, typename Pmf> Fn AsFn(Pmf pmf)
{
    return reinterpret_cast<Fn>(const_cast<void *>(Addr(pmf)));
}

uint64_t FloatBits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

// Pointer results as offsets from the side's own record.
uint64_t RecordOffset(const Side &s, const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(s.record));
}

// Pointer results as offsets from the side's own entry array (wrapping in 32 bits, as the original does).
uint64_t EntryOffset(const Side &s, const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(s.entries));
}

// Runs a no-argument getter: the original and ours on identical states, comparing the normalised result.
// Only the bytes the caller reads are compared (al for uint8_t, eax for uint32_t, st(0) bits for float).
template <typename R, typename Norm>
void CheckGetter(difftest::Context &ctx, const char *name, uint32_t rva, R (Sub::*pmf)(), std::mt19937 &rng, int trials,
                 Norm norm)
{
    using Fn = R(__thiscall *)(void *);
    auto orig = ctx.Original<Fn>(rva);
    auto ours = AsFn<Fn>(pmf);
    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        Side a, b;
        Build(a, st);
        Build(b, st);
        R ra = orig(SubOf(a));
        R rb = ours(SubOf(b));
        ++ctx.cases;
        if (norm(a, ra) != norm(b, rb)) {
            ctx.Fail("%s: result original %llx, ours %llx", name, static_cast<unsigned long long>(norm(a, ra)),
                     static_cast<unsigned long long>(norm(b, rb)));
            return;
        }
        CheckState(ctx, name, a, b);
    }
}

// Runs a one-argument getter (the argument is a 32-bit stack slot; ret 4).
template <typename R, typename Norm>
void CheckIndexed(difftest::Context &ctx, const char *name, uint32_t rva, R (Sub::*pmf)(uint32_t), std::mt19937 &rng,
                  int trials, Norm norm)
{
    using Fn = R(__thiscall *)(void *, uint32_t);
    auto orig = ctx.Original<Fn>(rva);
    auto ours = AsFn<Fn>(pmf);
    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        // Small indices (inside the array) and any 32-bit value, which must wrap as the original's lea does.
        uint32_t index = (i % 2) ? static_cast<uint32_t>(rng() % kEntries) : static_cast<uint32_t>(rng());
        Side a, b;
        Build(a, st);
        Build(b, st);
        R ra = orig(SubOf(a), index);
        R rb = ours(SubOf(b), index);
        ++ctx.cases;
        if (norm(a, ra) != norm(b, rb)) {
            ctx.Fail("%s: index %u result original %llx, ours %llx", name, index,
                     static_cast<unsigned long long>(norm(a, ra)), static_cast<unsigned long long>(norm(b, rb)));
            return;
        }
        CheckState(ctx, name, a, b);
    }
}

// SetFlagBits: ret 8. The arguments are 32-bit stack slots of which only the low byte is read; the values are
// chosen so that low bytes are zero while the high bits are not.
void CheckSetFlagBits(difftest::Context &ctx, uint32_t rva, std::mt19937 &rng, int trials)
{
    using Fn = void(__thiscall *)(void *, uint32_t, uint32_t);
    auto orig = ctx.Original<Fn>(rva);
    auto ours = AsFn<Fn>(&Sub::SetFlagBits);
    auto pick = [&rng]() -> uint32_t {
        switch (rng() % 5) {
        case 0: return 0;
        case 1: return 1;
        case 2: return rng() & ~0xffu; // low byte zero, high bits set
        case 3: return 0x100u * (1 + rng() % 255);
        default: return rng();
        }
    };
    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        uint32_t a0 = pick(), a1 = pick();
        Side a, b;
        Build(a, st);
        Build(b, st);
        orig(SubOf(a), a0, a1);
        ours(SubOf(b), a0, a1);
        ++ctx.cases;
        CheckState(ctx, "SetFlagBits", a, b);
    }
}

} // namespace

// 0x57e910 (6 bytes). Only al is defined; compare al.
DIFF_TEST(cAnimResourceConst_FlagBit0, 0x57e910)
{
    std::mt19937 rng(0x1001);
    CheckGetter<uint8_t>(ctx, "FlagBit0", 0x57e910, &Sub::FlagBit0, rng, 2000,
                         [](const Side &, uint8_t v) -> uint64_t { return v; });
}

// 0xa111b0 (7 bytes): record + 0xc.
DIFF_TEST(cAnimResourceConst_RecordFieldC, 0xa111b0)
{
    std::mt19937 rng(0x1002);
    CheckGetter<void *>(ctx, "RecordFieldC", 0xa111b0, &Sub::RecordFieldC, rng, 1000,
                        [](const Side &s, void *p) -> uint64_t { return RecordOffset(s, p); });
}

// 0xa10da0 (22 bytes, ret 4): entries + 28 * index, wrapping in 32 bits.
DIFF_TEST(cAnimResourceConst_EntryAt, 0xa10da0)
{
    std::mt19937 rng(0x1003);
    CheckIndexed<void *>(ctx, "EntryAt", 0xa10da0, &Sub::EntryAt, rng, 4000,
                         [](const Side &s, void *p) -> uint64_t { return EntryOffset(s, p); });
}

// 0xa15e90 (8 bytes): setne al of (flags & 3). Only al is defined.
DIFF_TEST(cAnimResourceConst_HasFlagBits0To1, 0xa15e90)
{
    std::mt19937 rng(0x1004);
    CheckGetter<bool>(ctx, "HasFlagBits0To1", 0xa15e90, &Sub::HasFlagBits0To1, rng, 2000,
                      [](const Side &, bool v) -> uint64_t { return v ? 1 : 0; });
}

// 0xa15eb0 (9 bytes): byte at record + 8, bit 0. Only al is defined.
DIFF_TEST(cAnimResourceConst_RecordFlagBit0, 0xa15eb0)
{
    std::mt19937 rng(0x1005);
    CheckGetter<uint8_t>(ctx, "RecordFlagBit0", 0xa15eb0, &Sub::RecordFlagBit0, rng, 2000,
                         [](const Side &, uint8_t v) -> uint64_t { return v; });
}

// 0xa16380 (11 bytes): bit 1 of the dword at record + 8. Only al is defined.
DIFF_TEST(cAnimResourceConst_RecordFlagBit1, 0xa16380)
{
    std::mt19937 rng(0x1006);
    CheckGetter<uint8_t>(ctx, "RecordFlagBit1", 0xa16380, &Sub::RecordFlagBit1, rng, 2000,
                         [](const Side &, uint8_t v) -> uint64_t { return v; });
}

// 0xa16490 (15 bytes): (dword record + 8 >> 12) & 0xfff.
DIFF_TEST(cAnimResourceConst_RecordField8Bits12To23, 0xa16490)
{
    std::mt19937 rng(0x1007);
    CheckGetter<uint32_t>(ctx, "RecordField8Bits12To23", 0xa16490, &Sub::RecordField8Bits12To23, rng, 2000,
                          [](const Side &, uint32_t v) -> uint64_t { return v; });
}

// 0xa16530 (8 bytes): movzx eax, word [record + 2]. The whole eax is compared.
DIFF_TEST(cAnimResourceConst_RecordWord2, 0xa16530)
{
    std::mt19937 rng(0x1008);
    CheckGetter<uint32_t>(ctx, "RecordWord2", 0xa16530, &Sub::RecordWord2, rng, 2000,
                          [](const Side &, uint32_t v) -> uint64_t { return v; });
}

// 0xa16590 (13 bytes): (dword record + 8 >> 8) & 0xf.
DIFF_TEST(cAnimResourceConst_RecordField8Bits8To11, 0xa16590)
{
    std::mt19937 rng(0x1009);
    CheckGetter<uint32_t>(ctx, "RecordField8Bits8To11", 0xa16590, &Sub::RecordField8Bits8To11, rng, 2000,
                          [](const Side &, uint32_t v) -> uint64_t { return v; });
}

// 0xa17770 (7 bytes): dword record + 0x14.
DIFF_TEST(cAnimResourceConst_RecordDword14, 0xa17770)
{
    std::mt19937 rng(0x100a);
    CheckGetter<uint32_t>(ctx, "RecordDword14", 0xa17770, &Sub::RecordDword14, rng, 2000,
                          [](const Side &, uint32_t v) -> uint64_t { return v; });
}

// 0xa17780 (7 bytes): dword record + 0x18.
DIFF_TEST(cAnimResourceConst_RecordDword18, 0xa17780)
{
    std::mt19937 rng(0x100b);
    CheckGetter<uint32_t>(ctx, "RecordDword18", 0xa17780, &Sub::RecordDword18, rng, 2000,
                          [](const Side &, uint32_t v) -> uint64_t { return v; });
}

// 0xa175c0 (41 bytes, ret 8): writes the flag word at subobject +8.
DIFF_TEST(cAnimResourceConst_SetFlagBits, 0xa175c0)
{
    std::mt19937 rng(0x100c);
    CheckSetFlagBits(ctx, 0xa175c0, rng, 4000);
}

// 0xa125f0 (7 bytes): float at record + 0x1c, returned in st(0). Compared bit for bit.
DIFF_TEST(cAnimResourceConst_RecordFloat1C, 0xa125f0)
{
    std::mt19937 rng(0x100d);
    CheckGetter<float>(ctx, "RecordFloat1C", 0xa125f0, &Sub::RecordFloat1C, rng, 2000,
                       [](const Side &, float v) -> uint64_t { return FloatBits(v); });
}

// 0xa163a0 (7 bytes): float at record + 0x20.
DIFF_TEST(cAnimResourceConst_RecordFloat20, 0xa163a0)
{
    std::mt19937 rng(0x100e);
    CheckGetter<float>(ctx, "RecordFloat20", 0xa163a0, &Sub::RecordFloat20, rng, 2000,
                       [](const Side &, float v) -> uint64_t { return FloatBits(v); });
}

// 0xa16390 (7 bytes): float at record + 0x24.
DIFF_TEST(cAnimResourceConst_RecordFloat24, 0xa16390)
{
    std::mt19937 rng(0x100f);
    CheckGetter<float>(ctx, "RecordFloat24", 0xa16390, &Sub::RecordFloat24, rng, 2000,
                       [](const Side &, float v) -> uint64_t { return FloatBits(v); });
}

// 0xa16470 (7 bytes): float at record + 0x28.
DIFF_TEST(cAnimResourceConst_RecordFloat28, 0xa16470)
{
    std::mt19937 rng(0x1010);
    CheckGetter<float>(ctx, "RecordFloat28", 0xa16470, &Sub::RecordFloat28, rng, 2000,
                       [](const Side &, float v) -> uint64_t { return FloatBits(v); });
}

// 0xa16480 (7 bytes): float at record + 0x2c.
DIFF_TEST(cAnimResourceConst_RecordFloat2C, 0xa16480)
{
    std::mt19937 rng(0x1011);
    CheckGetter<float>(ctx, "RecordFloat2C", 0xa16480, &Sub::RecordFloat2C, rng, 2000,
                       [](const Side &, float v) -> uint64_t { return FloatBits(v); });
}

// 0xa17870 (7 bytes): float at record + 0x30.
DIFF_TEST(cAnimResourceConst_RecordFloat30, 0xa17870)
{
    std::mt19937 rng(0x1012);
    CheckGetter<float>(ctx, "RecordFloat30", 0xa17870, &Sub::RecordFloat30, rng, 2000,
                       [](const Side &, float v) -> uint64_t { return FloatBits(v); });
}

// 0xa17790 (7 bytes): float at record + 0x34.
DIFF_TEST(cAnimResourceConst_RecordFloat34, 0xa17790)
{
    std::mt19937 rng(0x1013);
    CheckGetter<float>(ctx, "RecordFloat34", 0xa17790, &Sub::RecordFloat34, rng, 2000,
                       [](const Side &, float v) -> uint64_t { return FloatBits(v); });
}

// 0xa16580 (7 bytes): float at record + 0x38.
DIFF_TEST(cAnimResourceConst_RecordFloat38, 0xa16580)
{
    std::mt19937 rng(0x1014);
    CheckGetter<float>(ctx, "RecordFloat38", 0xa16580, &Sub::RecordFloat38, rng, 2000,
                       [](const Side &, float v) -> uint64_t { return FloatBits(v); });
}

// 0xa17100 (7 bytes): float at record + 0x3c.
DIFF_TEST(cAnimResourceConst_RecordFloat3C, 0xa17100)
{
    std::mt19937 rng(0x1015);
    CheckGetter<float>(ctx, "RecordFloat3C", 0xa17100, &Sub::RecordFloat3C, rng, 2000,
                       [](const Side &, float v) -> uint64_t { return FloatBits(v); });
}

// ---- The subobject at object + 0x58 (cAnimResourceConstSecondary): thunks into the cSGResource bodies ----
//
// Each case builds one random state three times, as tests/sg_anim_resource.cpp does: the original thunk runs on
// the first copy, our function directly on the second, and our function reached through the subobject vtable
// (a copy with our function in that slot) on the third. The host holds the cSGResource part at +0 (primary vtable
// copy from 0xe66434, virtual base table at +0x1c, resource at +0x24, flag at +0x28) and the subobject at
// +0x58 + d, where d is the vtordisp stored at subobject - 4 (0, 4 or 8). The thunk's `sub ecx, 0x28` then gives
// S = host + 0x30 for every d, which is the cSGResource secondary subobject the bodies expect.
// Peer calls (QueryInterface, AddRef, Release, slot 21) are fakes that log their calls by offset from their side.
namespace {

using Sec = nGZSceneDBAnim::cAnimResourceConstSecondary;
using SecEvents = std::vector<std::pair<uint32_t, uint32_t>>; // (kind, value), in call order
SecEvents gSecEvents;

enum : uint32_t { kSecQI = 1, kSecQIId, kSecAddRef, kSecRelease, kSecSlot21, kSecKeyWord };

struct SecSide;
SecSide *gSecCur = nullptr; // the side whose objects the fakes and the normalised pointers refer to

uint32_t SecOff(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(gSecCur));
}

uint32_t SecNorm(const void *p)
{
    return p == nullptr ? 0xffffffffu : SecOff(p);
}

uint32_t SecId(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

// Scripted QueryInterface results (0 null, 1 argument, 2 virtual base, 3 resource) and garbage above al.
bool gSecQIOk[2];
int gSecQIOut[2];
uint32_t gSecQIGarbage = 0;
uint32_t gSecQICount = 0;
uint32_t gSecRet21 = 0; // returned by the resource's slot 21 fake

enum SecPeerKind { kSecNull = 0, kSecArg, kSecBase, kSecResource };

struct SecPeer {
    const void *vtable;
    uint8_t pad[0x40];
};

constexpr int kSecSubOffset = 0x58;
constexpr int kSecHostSize = 0x100;

struct SecSide {
    uint8_t front[32];
    uint8_t host[kSecHostSize];
    uint8_t back[32];
    const void *primaryVT[14];
    const void *subVT[25];
    int vbtable[2];
    int disp; // the vtordisp stored at subobject - 4; the subobject is at host + kSecSubOffset + disp
    SecPeer argObj;
    SecPeer vbase;
    SecPeer edi;
};

void *SecPeerFor(SecSide &s, int kind)
{
    switch (kind) {
    case kSecArg: return &s.argObj;
    case kSecBase: return &s.vbase;
    case kSecResource: return &s.edi;
    default: return nullptr;
    }
}

uint32_t __fastcall SecFakeQI(void *self, void *, const void *iid, void **out)
{
    gSecEvents.emplace_back(kSecQI, SecOff(self));
    gSecEvents.emplace_back(kSecQIId, SecId(iid));
    uint32_t i = gSecQICount++ & 1;
    *out = SecPeerFor(*gSecCur, gSecQIOut[i]);
    return (gSecQIOk[i] ? 1u : 0u) | gSecQIGarbage;
}

uint32_t __fastcall SecFakeAddRef(void *self, void *)
{
    gSecEvents.emplace_back(kSecAddRef, SecOff(self));
    return 0x1234;
}

uint32_t __fastcall SecFakeRelease(void *self, void *)
{
    gSecEvents.emplace_back(kSecRelease, SecOff(self));
    return 0x5678;
}

uint32_t __fastcall SecFakeSlot21(void *self, void *, void *key)
{
    gSecEvents.emplace_back(kSecSlot21, SecOff(self));
    const uint32_t *words = static_cast<const uint32_t *>(key);
    for (int i = 0; i < 4; ++i) gSecEvents.emplace_back(kSecKeyWord, words[i]);
    return gSecRet21;
}

void *gSecPeerVT[25];

void SecInitPeerVT()
{
    if (gSecPeerVT[0] != nullptr) return;
    gSecPeerVT[0] = reinterpret_cast<void *>(&SecFakeQI);
    gSecPeerVT[1] = reinterpret_cast<void *>(&SecFakeAddRef);
    gSecPeerVT[2] = reinterpret_cast<void *>(&SecFakeRelease);
    gSecPeerVT[21] = reinterpret_cast<void *>(&SecFakeSlot21);
}

// Object state shared by the three sides: the host bytes, the vtordisp, and which peer the resource points at.
struct SecState {
    uint8_t host[kSecHostSize];
    int resourceKind;
    int disp; // 0, 4 or 8
};

SecState SecRandomState(std::mt19937 &rng)
{
    SecState st;
    for (auto &b : st.host) b = static_cast<uint8_t>(rng());
    st.resourceKind = static_cast<int>(rng() % 4);
    static const int kDisps[3] = {0, 4, 8};
    st.disp = kDisps[rng() % 3];
    // Flag byte at S - 8 = object +0x28 (checked by ForwardKey): zero or not, both occur.
    st.host[0x28] = rng() % 2 ? 0 : static_cast<uint8_t>(1 + rng() % 255);
    return st;
}

void SecStorePtr(uint8_t *at, const void *p)
{
    uintptr_t v = reinterpret_cast<uintptr_t>(p);
    std::memcpy(at, &v, sizeof(v));
}

struct SecSlots {
    const void *slots[25];
};

struct SecPrimary {
    const void *slots[14];
};

// The plain subobject vtable, the same with one slot routed to our function, and the primary vtable copy.
struct SecVtables {
    SecSlots plain;
    SecSlots routed;
    SecPrimary prim;
};

SecVtables SecSetup(const difftest::Context &ctx, int slot, const void *ours)
{
    SecVtables v{};
    std::memcpy(v.plain.slots, ctx.image.At(Sec::kVTable), sizeof(v.plain.slots));
    std::memcpy(v.prim.slots, ctx.image.At(0xe66434), sizeof(v.prim.slots));
    v.routed = v.plain;
    v.routed.slots[slot] = ours;
    return v;
}

void SecBuild(SecSide &s, const SecState &st, const SecSlots &sub, const SecPrimary &prim)
{
    std::memset(&s, 0, sizeof(s));
    std::memset(s.front, 0xA5, sizeof(s.front));
    std::memset(s.back, 0x5A, sizeof(s.back));
    std::memcpy(s.host, st.host, sizeof(s.host));
    std::memcpy(s.primaryVT, prim.slots, sizeof(s.primaryVT));
    std::memcpy(s.subVT, sub.slots, sizeof(s.subVT));
    s.disp = st.disp;
    s.argObj.vtable = s.vbase.vtable = s.edi.vtable = gSecPeerVT;
    // The virtual base sits at (object +0x1c) + displacement; entry 0 is unused by the bodies under test.
    s.vbtable[0] = 0;
    s.vbtable[1] = static_cast<int>(reinterpret_cast<uintptr_t>(&s.vbase) - (reinterpret_cast<uintptr_t>(s.host) + 0x1c));
    SecStorePtr(s.host + 0x00, s.primaryVT);
    SecStorePtr(s.host + 0x1c, s.vbtable);
    SecStorePtr(s.host + 0x24, SecPeerFor(s, st.resourceKind));
    // The vtordisp (read by the thunk at subobject - 4) and the subobject's vtable pointer.
    const int32_t disp = st.disp;
    std::memcpy(s.host + kSecSubOffset + disp - 4, &disp, sizeof(disp));
    SecStorePtr(s.host + kSecSubOffset + disp, s.subVT);
}

// Object bytes with the pointer fields replaced by their offsets, so sides compare equal.
std::vector<uint8_t> SecSnapshot(SecSide &s)
{
    gSecCur = &s;
    std::vector<uint8_t> out(s.host, s.host + sizeof(s.host));
    uint32_t values[4] = {SecNorm(s.primaryVT), SecNorm(s.vbtable), SecNorm(*reinterpret_cast<void *const *>(s.host + 0x24)),
                          SecNorm(s.subVT)};
    const int offsets[4] = {0x00, 0x1c, 0x24, kSecSubOffset + s.disp};
    for (int i = 0; i < 4; ++i) std::memcpy(out.data() + offsets[i], &values[i], sizeof(uint32_t));
    return out;
}

bool SecCanariesIntact(const SecSide &s)
{
    for (uint8_t b : s.front)
        if (b != 0xA5) return false;
    for (uint8_t b : s.back)
        if (b != 0x5A) return false;
    return true;
}

// Runs one operation on the three sides and compares the result, the peer calls and the object state.
// run(which, side, subobject) performs the operation (which 0 original, 1 ours, 2 ours through the slot).
template <typename Run>
void SecCompare(difftest::Context &ctx, const char *name, const SecState &st, const SecVtables &v, Run run)
{
    static SecSide a, b, c;
    SecBuild(a, st, v.plain, v.prim);
    SecBuild(b, st, v.plain, v.prim);
    SecBuild(c, st, v.routed, v.prim);

    gSecEvents.clear();
    gSecQICount = 0;
    gSecCur = &a;
    uint64_t ra = run(0, a, a.host + kSecSubOffset + st.disp);
    SecEvents ea = gSecEvents;
    gSecEvents.clear();
    gSecQICount = 0;
    gSecCur = &b;
    uint64_t rb = run(1, b, b.host + kSecSubOffset + st.disp);
    SecEvents eb = gSecEvents;
    gSecEvents.clear();
    gSecQICount = 0;
    gSecCur = &c;
    uint64_t rc = run(2, c, c.host + kSecSubOffset + st.disp);
    SecEvents ec = gSecEvents;

    ++ctx.cases;
    if (ra != rb || ra != rc) {
        ctx.Fail("%s: result original %llu, ours %llu, via slot %llu", name, static_cast<unsigned long long>(ra),
                 static_cast<unsigned long long>(rb), static_cast<unsigned long long>(rc));
    }
    if (ea != eb || ea != ec) {
        ctx.Fail("%s: calls made differ (original %zu, ours %zu, via slot %zu)", name, ea.size(), eb.size(), ec.size());
    }
    if (SecSnapshot(a) != SecSnapshot(b) || SecSnapshot(a) != SecSnapshot(c)) ctx.Fail("%s: object state differs", name);
    if (!SecCanariesIntact(a) || !SecCanariesIntact(b) || !SecCanariesIntact(c)) ctx.Fail("%s: canaries overwritten", name);
}

// VirtualBaseIfObject (slot 6, thunk 0xa170e0): no arguments.
void SecCheckVirtualBaseIfObject(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    SecInitPeerVT();
    SecVtables v = SecSetup(ctx, 6, Addr(&Sec::VirtualBaseIfObject));
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xa170e0);
    for (int i = 0; i < trials; ++i) {
        SecState st = SecRandomState(rng);
        SecCompare(ctx, "Secondary VirtualBaseIfObject", st, v, [&](int which, SecSide &, void *sub) -> uint64_t {
            if (which == 0) return SecNorm(orig(sub));
            if (which == 1) return SecNorm(reinterpret_cast<Sec *>(sub)->VirtualBaseIfObject());
            return SecNorm(VirtualSlot<void *(__thiscall *)(void *)>(sub, 6)(sub));
        });
    }
}

// GetOwner (slot 7, thunk 0xa10e60): no arguments.
void SecCheckOwner(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    SecInitPeerVT();
    SecVtables v = SecSetup(ctx, 7, Addr(&Sec::GetOwner));
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xa10e60);
    for (int i = 0; i < trials; ++i) {
        SecState st = SecRandomState(rng);
        SecCompare(ctx, "Secondary GetOwner", st, v, [&](int which, SecSide &, void *sub) -> uint64_t {
            if (which == 0) return SecNorm(orig(sub));
            if (which == 1) return SecNorm(reinterpret_cast<Sec *>(sub)->GetOwner());
            return SecNorm(VirtualSlot<void *(__thiscall *)(void *)>(sub, 7)(sub));
        });
    }
}

// GetVirtualBase (slot 9, thunk 0xa170d5): ret 4.
void SecCheckVirtualBase(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    SecInitPeerVT();
    SecVtables v = SecSetup(ctx, 9, Addr(&Sec::GetVirtualBase));
    auto orig = ctx.Original<void *(__thiscall *)(void *, void *)>(0xa170d5);
    for (int i = 0; i < trials; ++i) {
        SecState st = SecRandomState(rng);
        void *arg = rng() % 2 ? nullptr : reinterpret_cast<void *>(static_cast<uintptr_t>(rng() | 1));
        SecCompare(ctx, "Secondary GetVirtualBase", st, v, [&](int which, SecSide &, void *sub) -> uint64_t {
            if (which == 0) return SecNorm(orig(sub, arg));
            if (which == 1) return SecNorm(reinterpret_cast<Sec *>(sub)->GetVirtualBase(arg));
            return SecNorm(VirtualSlot<void *(__thiscall *)(void *, void *)>(sub, 9)(sub, arg));
        });
    }
}

// CompareIdentity (slot 11, thunk 0xa15e76): ret 4. The argument is a peer, never the host object.
void SecCheckIdentity(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    SecInitPeerVT();
    SecVtables v = SecSetup(ctx, 11, Addr(&Sec::CompareIdentity));
    auto orig = ctx.Original<int32_t(__thiscall *)(void *, void *)>(0xa15e76);
    for (int i = 0; i < trials; ++i) {
        SecState st = SecRandomState(rng);
        int argKind = 1 + static_cast<int>(rng() % 3);
        for (int k = 0; k < 2; ++k) {
            gSecQIOk[k] = rng() % 4 != 0;
            gSecQIOut[k] = static_cast<int>(rng() % 4);
        }
        gSecQIGarbage = rng() % 2 ? 0xffffff00u : 0u;
        SecCompare(ctx, "Secondary CompareIdentity", st, v, [&](int which, SecSide &s, void *sub) -> uint64_t {
            void *other = SecPeerFor(s, argKind);
            if (which == 0) return static_cast<uint32_t>(orig(sub, other));
            if (which == 1) return static_cast<uint32_t>(reinterpret_cast<Sec *>(sub)->CompareIdentity(other));
            return static_cast<uint32_t>(VirtualSlot<int32_t(__thiscall *)(void *, void *)>(sub, 11)(sub, other));
        });
    }
}

// SetResource (slot 15, thunk 0xa175ff): ret 4; AddRef on the new resource, the store, Release on the old one.
void SecCheckSetResource(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    SecInitPeerVT();
    SecVtables v = SecSetup(ctx, 15, Addr(&Sec::SetResource));
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0xa175ff);
    for (int i = 0; i < trials; ++i) {
        SecState st = SecRandomState(rng);
        int newKind = static_cast<int>(rng() % 4);
        SecCompare(ctx, "Secondary SetResource", st, v, [&](int which, SecSide &s, void *sub) -> uint64_t {
            void *resource = SecPeerFor(s, newKind);
            if (which == 0) return orig(sub, resource) & 0xffu;
            if (which == 1) return static_cast<uint8_t>(reinterpret_cast<Sec *>(sub)->SetResource(resource));
            return VirtualSlot<uint8_t(__thiscall *)(void *, void *)>(sub, 15)(sub, resource) & 0xffu;
        });
    }
}

// SetFlag (slot 16, thunk 0xa1759c): ret 4; stores only the low byte of its argument.
void SecCheckSetFlag(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    SecInitPeerVT();
    SecVtables v = SecSetup(ctx, 16, Addr(&Sec::SetFlag));
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0xa1759c);
    for (int i = 0; i < trials; ++i) {
        SecState st = SecRandomState(rng);
        uint32_t value = rng() % 3 == 0 ? static_cast<uint32_t>(rng() & 0xff) : static_cast<uint32_t>(rng());
        SecCompare(ctx, "Secondary SetFlag", st, v, [&](int which, SecSide &, void *sub) -> uint64_t {
            if (which == 0) {
                orig(sub, value);
            } else if (which == 1) {
                reinterpret_cast<Sec *>(sub)->SetFlag(value);
            } else {
                VirtualSlot<void(__thiscall *)(void *, uint32_t)>(sub, 16)(sub, value);
            }
            return 0;
        });
    }
}

// CopyKeyTo (slot 22, thunk 0xa170eb): ret 4; primary slot 3 copies the key to the argument and returns it.
void SecCheckCopyKey(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    SecInitPeerVT();
    SecVtables v = SecSetup(ctx, 22, Addr(&Sec::CopyKeyTo));
    auto orig = ctx.Original<void *(__thiscall *)(void *, void *)>(0xa170eb);
    for (int i = 0; i < trials; ++i) {
        SecState st = SecRandomState(rng);
        uint32_t outs[3][4];
        std::memset(outs, 0xA5, sizeof(outs));
        SecCompare(ctx, "Secondary CopyKeyTo", st, v, [&](int which, SecSide &, void *sub) -> uint64_t {
            uint32_t *out = outs[which];
            void *ret = nullptr;
            if (which == 0) {
                ret = orig(sub, out);
            } else if (which == 1) {
                ret = reinterpret_cast<Sec *>(sub)->CopyKeyTo(out);
            } else {
                ret = VirtualSlot<void *(__thiscall *)(void *, void *)>(sub, 22)(sub, out);
            }
            return ret == out ? 1 : 0;
        });
        if (std::memcmp(outs[0], outs[1], sizeof(outs[0])) != 0 || std::memcmp(outs[0], outs[2], sizeof(outs[0])) != 0) {
            ctx.Fail("Secondary CopyKeyTo: key bytes differ");
        }
    }
}

// GetFlag (slot 23, thunk 0xa15e98): al only.
void SecCheckFlag(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    SecInitPeerVT();
    SecVtables v = SecSetup(ctx, 23, Addr(&Sec::GetFlag));
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *)>(0xa15e98);
    for (int i = 0; i < trials; ++i) {
        SecState st = SecRandomState(rng);
        SecCompare(ctx, "Secondary GetFlag", st, v, [&](int which, SecSide &, void *sub) -> uint64_t {
            if (which == 0) return orig(sub) & 0xffu;
            if (which == 1) return reinterpret_cast<Sec *>(sub)->GetFlag() & 0xffu;
            return VirtualSlot<uint8_t(__thiscall *)(void *)>(sub, 23)(sub) & 0xffu;
        });
    }
}

// ForwardKey (slot 24, thunk 0xa170af): -1 without a resource or with a zero flag byte; else the resource's slot 21.
void SecCheckForwardKey(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    SecInitPeerVT();
    SecVtables v = SecSetup(ctx, 24, Addr(&Sec::ForwardKey));
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0xa170af);
    for (int i = 0; i < trials; ++i) {
        SecState st = SecRandomState(rng);
        gSecRet21 = static_cast<uint32_t>(rng());
        SecCompare(ctx, "Secondary ForwardKey", st, v, [&](int which, SecSide &, void *sub) -> uint64_t {
            if (which == 0) return orig(sub);
            if (which == 1) return reinterpret_cast<Sec *>(sub)->ForwardKey();
            return VirtualSlot<uint32_t(__thiscall *)(void *)>(sub, 24)(sub);
        });
    }
}

} // namespace

// Thunk 0xa170e0 (slot 6): the cSGResource secondary's VirtualBaseIfObject body (0xa3c750).
DIFF_TEST(cAnimResourceConst_Secondary_VirtualBaseIfObject, 0xa170e0)
{
    std::mt19937 rng(0x7a01);
    SecCheckVirtualBaseIfObject(ctx, rng, 10000);
}

// Thunk 0xa10e60 (slot 7): GetOwner (body 0xa3c3c0).
DIFF_TEST(cAnimResourceConst_Secondary_GetOwner, 0xa10e60)
{
    std::mt19937 rng(0x7a02);
    SecCheckOwner(ctx, rng, 10000);
}

// Thunk 0xa170d5 (slot 9): GetVirtualBase (body 0xa3c720), ret 4.
DIFF_TEST(cAnimResourceConst_Secondary_GetVirtualBase, 0xa170d5)
{
    std::mt19937 rng(0x7a03);
    SecCheckVirtualBase(ctx, rng, 10000);
}

// Thunk 0xa15e76 (slot 11): CompareIdentity (body 0xa3c3d0), ret 4.
DIFF_TEST(cAnimResourceConst_Secondary_CompareIdentity, 0xa15e76)
{
    std::mt19937 rng(0x7a04);
    SecCheckIdentity(ctx, rng, 10000);
}

// Thunk 0xa175ff (slot 15): SetResource (body 0xa3cdd0), ret 4.
DIFF_TEST(cAnimResourceConst_Secondary_SetResource, 0xa175ff)
{
    std::mt19937 rng(0x7a05);
    SecCheckSetResource(ctx, rng, 10000);
}

// Thunk 0xa1759c (slot 16): SetFlag (body 0xa3c870), ret 4.
DIFF_TEST(cAnimResourceConst_Secondary_SetFlag, 0xa1759c)
{
    std::mt19937 rng(0x7a06);
    SecCheckSetFlag(ctx, rng, 10000);
}

// Thunk 0xa170eb (slot 22): CopyKeyTo (body 0xa3c770), ret 4.
DIFF_TEST(cAnimResourceConst_Secondary_CopyKey, 0xa170eb)
{
    std::mt19937 rng(0x7a07);
    SecCheckCopyKey(ctx, rng, 10000);
}

// Thunk 0xa15e98 (slot 23): GetFlag (body 0xa3c4a0).
DIFF_TEST(cAnimResourceConst_Secondary_GetFlag, 0xa15e98)
{
    std::mt19937 rng(0x7a08);
    SecCheckFlag(ctx, rng, 10000);
}

// Thunk 0xa170af (slot 24): ForwardKey (body 0xa3c6d0).
DIFF_TEST(cAnimResourceConst_Secondary_ForwardKey, 0xa170af)
{
    std::mt19937 rng(0x7a09);
    SecCheckForwardKey(ctx, rng, 10000);
}
