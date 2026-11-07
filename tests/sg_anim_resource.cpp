// Differential tests for the secondary subobject of nGZSceneDBAnim::cAnimResource (subobject at object +0xc4,
// vtable 0xe65ec4). Its slots are thunks (`sub ecx,[ecx-4]; sub ecx,0x94; jmp body`) into the cSGResource
// secondary-subobject bodies. Each case builds one random state three times: the original thunk runs on the
// first copy, our function directly on the second, and our function reached through the subobject vtable (a
// copy with our function in that slot) on the third. The host object holds the cSGResource part at +0 (primary
// vtable copy, virtual base table at +0x1c, resource at +0x24, flag at +0x28) and the subobject at +0xc4 + d,
// where d is the vtordisp stored at subobject - 4 (random 0, 4 or 8, so the adjustment is exercised).
// Peer calls (QueryInterface, AddRef, Release, slot 21) are fakes that record events by offset from their side.
#include "difftest.h"
#include "game/vtable.h"
#include "sg/cAnimResource.h"

#include <cstring>
#include <random>
#include <utility>
#include <vector>

namespace {

using Sub = nGZSceneDBAnim::cAnimResourceSubobject;
using Events = std::vector<std::pair<uint32_t, uint32_t>>; // (kind, value), in call order
Events gEvents;

enum : uint32_t { kQI = 1, kQIId, kAddRef, kRelease, kSlot21, kKeyWord };

struct Side;
Side *gCur = nullptr; // the side whose objects the fakes and the normalised pointers refer to

// Offset of p from the side under test; the layout of every side is the same, so these compare equal.
uint32_t Off(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(gCur));
}

uint32_t Norm(const void *p)
{
    return p == nullptr ? 0xffffffffu : Off(p);
}

uint32_t Id(const void *p)
{
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

// Scripted QueryInterface results: call i of a run returns gQIOk[i] and writes gQIOut[i] (0 null, 1 argument,
// 2 virtual base, 3 resource). gQIGarbage sets the bits above al, which the caller must ignore.
bool gQIOk[2];
int gQIOut[2];
uint32_t gQIGarbage = 0;
uint32_t gQICount = 0;
uint32_t gRet21 = 0; // returned by the resource's slot 21 fake

enum PeerKind { kNull = 0, kArg, kBase, kResource };

struct Peer {
    const void *vtable;
    uint8_t pad[0x40];
};

constexpr int kSubOffset = 0xc4; // the subobject's offset in the host object
constexpr int kHostSize = 0x100;

// One side: canaries, the host object, the copied vtables, the virtual base table and the three peers.
struct Side {
    uint8_t front[32];
    uint8_t host[kHostSize];
    uint8_t back[32];
    const void *primaryVT[13];
    const void *subVT[25];
    int vbtable[2];
    int disp; // the vtordisp stored at subobject - 4; the subobject is at host + kSubOffset + disp
    Peer argObj;
    Peer vbase;
    Peer edi;
};

void *PeerFor(Side &s, int kind)
{
    switch (kind) {
    case kArg: return &s.argObj;
    case kBase: return &s.vbase;
    case kResource: return &s.edi;
    default: return nullptr;
    }
}

uint32_t __fastcall FakeQI(void *self, void *, const void *iid, void **out)
{
    gEvents.emplace_back(kQI, Off(self));
    gEvents.emplace_back(kQIId, Id(iid));
    uint32_t i = gQICount++ & 1;
    *out = PeerFor(*gCur, gQIOut[i]);
    return (gQIOk[i] ? 1u : 0u) | gQIGarbage;
}
uint32_t __fastcall FakeAddRef(void *self, void *)
{
    gEvents.emplace_back(kAddRef, Off(self));
    return 0x1234;
}
uint32_t __fastcall FakeRelease(void *self, void *)
{
    gEvents.emplace_back(kRelease, Off(self));
    return 0x5678;
}
uint32_t __fastcall FakeSlot21(void *self, void *, void *key)
{
    gEvents.emplace_back(kSlot21, Off(self));
    const uint32_t *words = static_cast<const uint32_t *>(key);
    for (int i = 0; i < 4; ++i) gEvents.emplace_back(kKeyWord, words[i]);
    return gRet21;
}

// Peer vtable: QueryInterface, AddRef, Release, and slot 21 (the resource's forwarding slot).
void *gPeerVT[25];
void InitPeerVT()
{
    if (gPeerVT[0] != nullptr) return;
    gPeerVT[0] = reinterpret_cast<void *>(&FakeQI);
    gPeerVT[1] = reinterpret_cast<void *>(&FakeAddRef);
    gPeerVT[2] = reinterpret_cast<void *>(&FakeRelease);
    gPeerVT[21] = reinterpret_cast<void *>(&FakeSlot21);
}

// Object state shared by the three sides: the host bytes, the vtordisp, and which peer the resource points at.
struct State {
    uint8_t host[kHostSize];
    int resourceKind; // PeerKind for object +0x24
    int disp;         // vtordisp: 0, 4 or 8
};

State RandomState(std::mt19937 &rng)
{
    State st;
    for (auto &b : st.host) b = static_cast<uint8_t>(rng());
    st.resourceKind = static_cast<int>(rng() % 4);
    static const int kDisps[3] = {0, 4, 8};
    st.disp = kDisps[rng() % 3];
    // Flag byte at object +0x28 (checked by ForwardKey): zero or not, both occur.
    st.host[0x28] = rng() % 2 ? 0 : static_cast<uint8_t>(1 + rng() % 255);
    return st;
}

void StorePtr(uint8_t *at, const void *p)
{
    uintptr_t v = reinterpret_cast<uintptr_t>(p);
    std::memcpy(at, &v, sizeof(v));
}

struct Slots {
    const void *slots[25];
};

struct PrimarySlots {
    const void *slots[13];
};

void Build(Side &s, const State &st, const Slots &sub, const PrimarySlots &prim)
{
    std::memset(&s, 0, sizeof(s));
    std::memset(s.front, 0xA5, sizeof(s.front));
    std::memset(s.back, 0x5A, sizeof(s.back));
    std::memcpy(s.host, st.host, sizeof(s.host));
    std::memcpy(s.primaryVT, prim.slots, sizeof(s.primaryVT));
    std::memcpy(s.subVT, sub.slots, sizeof(s.subVT));
    s.disp = st.disp;
    s.argObj.vtable = s.vbase.vtable = s.edi.vtable = gPeerVT;
    // The virtual base sits at (object +0x1c) + displacement; entry 0 is unused by the functions under test.
    s.vbtable[0] = 0;
    s.vbtable[1] = static_cast<int>(reinterpret_cast<uintptr_t>(&s.vbase) - (reinterpret_cast<uintptr_t>(s.host) + 0x1c));
    StorePtr(s.host + 0x00, s.primaryVT);
    StorePtr(s.host + 0x1c, s.vbtable);
    StorePtr(s.host + 0x24, PeerFor(s, st.resourceKind));
    // The vtordisp (read by the thunk at subobject - 4) and the subobject's vtable pointer.
    const int32_t disp = st.disp;
    std::memcpy(s.host + kSubOffset + disp - 4, &disp, sizeof(disp));
    StorePtr(s.host + kSubOffset + disp, s.subVT);
}

// Object bytes with the pointer fields replaced by their offsets, so sides compare equal. Pointer fields: primary
// vptr (+0), vbptr (+0x1c), resource (+0x24), subobject vptr (+0xc4 + vtordisp).
std::vector<uint8_t> Snapshot(Side &s)
{
    gCur = &s;
    std::vector<uint8_t> out(s.host, s.host + sizeof(s.host));
    uint32_t values[4] = {Norm(s.primaryVT), Norm(s.vbtable), Norm(*reinterpret_cast<void *const *>(s.host + 0x24)),
                          Norm(s.subVT)};
    const int offsets[4] = {0x00, 0x1c, 0x24, kSubOffset + s.disp};
    for (int i = 0; i < 4; ++i) std::memcpy(out.data() + offsets[i], &values[i], sizeof(uint32_t));
    return out;
}

bool CanariesIntact(const Side &s)
{
    for (uint8_t b : s.front)
        if (b != 0xA5) return false;
    for (uint8_t b : s.back)
        if (b != 0x5A) return false;
    return true;
}

// Runs one operation on the three sides and compares the result, the events and the object state.
// run(which, side, subobject) performs the operation (which 0 original, 1 ours, 2 ours through the slot).
template <typename Run>
void Compare(difftest::Context &ctx, const char *name, const State &st, const Slots &plain, const Slots &routed,
             const PrimarySlots &prim, Run run)
{
    static Side a, b, c;
    Build(a, st, plain, prim);
    Build(b, st, plain, prim);
    Build(c, st, routed, prim);

    gEvents.clear();
    gQICount = 0;
    gCur = &a;
    uint64_t ra = run(0, a, a.host + kSubOffset + st.disp);
    Events ea = gEvents;
    gEvents.clear();
    gQICount = 0;
    gCur = &b;
    uint64_t rb = run(1, b, b.host + kSubOffset + st.disp);
    Events eb = gEvents;
    gEvents.clear();
    gQICount = 0;
    gCur = &c;
    uint64_t rc = run(2, c, c.host + kSubOffset + st.disp);
    Events ec = gEvents;

    ++ctx.cases;
    if (ra != rb || ra != rc) {
        ctx.Fail("%s: result original %llu, ours %llu, via slot %llu", name, static_cast<unsigned long long>(ra),
                 static_cast<unsigned long long>(rb), static_cast<unsigned long long>(rc));
    }
    if (ea != eb || ea != ec) {
        ctx.Fail("%s: calls made differ (original %zu, ours %zu, via slot %zu)", name, ea.size(), eb.size(), ec.size());
    }
    if (Snapshot(a) != Snapshot(b) || Snapshot(a) != Snapshot(c)) ctx.Fail("%s: object state differs", name);
    if (!CanariesIntact(a) || !CanariesIntact(b) || !CanariesIntact(c)) ctx.Fail("%s: canaries overwritten", name);
}

// Subobject vtable copies: the original's slots, and a copy with one slot routed to our function.
Slots PlainSlots(const difftest::Context &ctx)
{
    Slots s{};
    std::memcpy(s.slots, ctx.image.At(Sub::kVTable), sizeof(s.slots));
    return s;
}

PrimarySlots PrimarySlotsOf(const difftest::Context &ctx)
{
    PrimarySlots p{};
    std::memcpy(p.slots, ctx.image.At(0xe65e8c), sizeof(p.slots)); // primary vtable of cAnimResource
    return p;
}

template <typename T> const void *Addr(T pmf)
{
    static_assert(sizeof(T) == sizeof(void *), "single-inheritance member pointers only");
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

// VirtualBaseIfObject (slot 6, thunk 0xa10195): no arguments.
void CheckVirtualBaseIfObject(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    InitPeerVT();
    Slots plain = PlainSlots(ctx);
    PrimarySlots prim = PrimarySlotsOf(ctx);
    Slots routed = plain;
    routed.slots[6] = Addr(&Sub::VirtualBaseIfObject);
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xa10195);

    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        Compare(ctx, "VirtualBaseIfObject", st, plain, routed, prim, [&](int which, Side &, void *sub) -> uint64_t {
            if (which == 0) return Norm(orig(sub));
            if (which == 1) return Norm(reinterpret_cast<Sub *>(sub)->VirtualBaseIfObject());
            return Norm(VirtualSlot<void *(__thiscall *)(void *)>(sub, 6)(sub));
        });
    }
}

// GetOwner (slot 7, thunk 0xa0f3ae): no arguments.
void CheckOwner(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    InitPeerVT();
    Slots plain = PlainSlots(ctx);
    PrimarySlots prim = PrimarySlotsOf(ctx);
    Slots routed = plain;
    routed.slots[7] = Addr(&Sub::GetOwner);
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0xa0f3ae);

    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        Compare(ctx, "GetOwner", st, plain, routed, prim, [&](int which, Side &, void *sub) -> uint64_t {
            if (which == 0) return Norm(orig(sub));
            if (which == 1) return Norm(reinterpret_cast<Sub *>(sub)->GetOwner());
            return Norm(VirtualSlot<void *(__thiscall *)(void *)>(sub, 7)(sub));
        });
    }
}

// GetFlag (slot 23, thunk 0xa0f57b): no arguments.
void CheckFlag(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    InitPeerVT();
    Slots plain = PlainSlots(ctx);
    PrimarySlots prim = PrimarySlotsOf(ctx);
    Slots routed = plain;
    routed.slots[23] = Addr(&Sub::GetFlag);
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *)>(0xa0f57b);

    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        Compare(ctx, "GetFlag", st, plain, routed, prim, [&](int which, Side &, void *sub) -> uint64_t {
            // Only al is the result: the thunk and the body never write the rest of eax.
            if (which == 0) return orig(sub) & 0xffu;
            if (which == 1) return reinterpret_cast<Sub *>(sub)->GetFlag() & 0xffu;
            return VirtualSlot<uint8_t(__thiscall *)(void *)>(sub, 23)(sub) & 0xffu;
        });
    }
}

// GetVirtualBase (slot 9, thunk 0xa10187): ret 4.
void CheckVirtualBase(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    InitPeerVT();
    Slots plain = PlainSlots(ctx);
    PrimarySlots prim = PrimarySlotsOf(ctx);
    Slots routed = plain;
    routed.slots[9] = Addr(&Sub::GetVirtualBase);
    auto orig = ctx.Original<void *(__thiscall *)(void *, void *)>(0xa10187);

    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        void *arg = rng() % 2 ? nullptr : reinterpret_cast<void *>(static_cast<uintptr_t>(rng() | 1));
        Compare(ctx, "GetVirtualBase", st, plain, routed, prim, [&](int which, Side &, void *sub) -> uint64_t {
            if (which == 0) return Norm(orig(sub, arg));
            if (which == 1) return Norm(reinterpret_cast<Sub *>(sub)->GetVirtualBase(arg));
            return Norm(VirtualSlot<void *(__thiscall *)(void *, void *)>(sub, 9)(sub, arg));
        });
    }
}

// CompareIdentity (slot 11, thunk 0xa0f554): ret 4. The argument is a peer, never the host object.
void CheckIdentity(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    InitPeerVT();
    Slots plain = PlainSlots(ctx);
    PrimarySlots prim = PrimarySlotsOf(ctx);
    Slots routed = plain;
    routed.slots[11] = Addr(&Sub::CompareIdentity);
    auto orig = ctx.Original<int32_t(__thiscall *)(void *, void *)>(0xa0f554);

    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        int argKind = 1 + static_cast<int>(rng() % 3);
        for (int k = 0; k < 2; ++k) {
            gQIOk[k] = rng() % 4 != 0;
            gQIOut[k] = static_cast<int>(rng() % 4);
        }
        gQIGarbage = rng() % 2 ? 0xffffff00u : 0u;
        Compare(ctx, "CompareIdentity", st, plain, routed, prim, [&](int which, Side &s, void *sub) -> uint64_t {
            void *other = PeerFor(s, argKind);
            if (which == 0) return static_cast<uint32_t>(orig(sub, other));
            if (which == 1) return static_cast<uint32_t>(reinterpret_cast<Sub *>(sub)->CompareIdentity(other));
            return static_cast<uint32_t>(VirtualSlot<int32_t(__thiscall *)(void *, void *)>(sub, 11)(sub, other));
        });
    }
}

// SetResource (slot 15, thunk 0xa103ce): ret 4; AddRef on the new resource, the store, Release on the old one.
void CheckSetResource(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    InitPeerVT();
    Slots plain = PlainSlots(ctx);
    PrimarySlots prim = PrimarySlotsOf(ctx);
    Slots routed = plain;
    routed.slots[15] = Addr(&Sub::SetResource);
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, void *)>(0xa103ce);

    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        int newKind = static_cast<int>(rng() % 4);
        Compare(ctx, "SetResource", st, plain, routed, prim, [&](int which, Side &s, void *sub) -> uint64_t {
            void *resource = PeerFor(s, newKind);
            if (which == 0) return orig(sub, resource) & 0xffu;
            if (which == 1) return static_cast<uint8_t>(reinterpret_cast<Sub *>(sub)->SetResource(resource));
            return VirtualSlot<uint8_t(__thiscall *)(void *, void *)>(sub, 15)(sub, resource) & 0xffu;
        });
    }
}

// SetFlag (slot 16, thunk 0xa101e0): ret 4; stores only the low byte of its argument.
void CheckSetFlag(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    InitPeerVT();
    Slots plain = PlainSlots(ctx);
    PrimarySlots prim = PrimarySlotsOf(ctx);
    Slots routed = plain;
    routed.slots[16] = Addr(&Sub::SetFlag);
    auto orig = ctx.Original<void(__thiscall *)(void *, uint32_t)>(0xa101e0);

    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        uint32_t value = rng() % 3 == 0 ? static_cast<uint32_t>(rng() & 0xff) : static_cast<uint32_t>(rng());
        Compare(ctx, "SetFlag", st, plain, routed, prim, [&](int which, Side &, void *sub) -> uint64_t {
            if (which == 0) {
                orig(sub, value);
            } else if (which == 1) {
                reinterpret_cast<Sub *>(sub)->SetFlag(value);
            } else {
                VirtualSlot<void(__thiscall *)(void *, uint32_t)>(sub, 16)(sub, value);
            }
            return 0;
        });
    }
}

// CopyKeyTo (slot 22, thunk 0xa101a3): ret 4; primary slot 3 copies the key to the argument and returns it.
void CheckCopyKey(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    InitPeerVT();
    Slots plain = PlainSlots(ctx);
    PrimarySlots prim = PrimarySlotsOf(ctx);
    Slots routed = plain;
    routed.slots[22] = Addr(&Sub::CopyKeyTo);
    auto orig = ctx.Original<void *(__thiscall *)(void *, void *)>(0xa101a3);

    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        uint32_t outs[3][4];
        std::memset(outs, 0xA5, sizeof(outs));
        Compare(ctx, "CopyKeyTo", st, plain, routed, prim, [&](int which, Side &, void *sub) -> uint64_t {
            uint32_t *out = outs[which];
            void *ret = nullptr;
            if (which == 0) {
                ret = orig(sub, out);
            } else if (which == 1) {
                ret = reinterpret_cast<Sub *>(sub)->CopyKeyTo(out);
            } else {
                ret = VirtualSlot<void *(__thiscall *)(void *, void *)>(sub, 22)(sub, out);
            }
            return ret == out ? 1 : 0;
        });
        if (std::memcmp(outs[0], outs[1], sizeof(outs[0])) != 0 || std::memcmp(outs[0], outs[2], sizeof(outs[0])) != 0) {
            ctx.Fail("CopyKeyTo: key bytes differ");
        }
    }
}

// ForwardKey (slot 24, thunk 0xa0fb19): -1 without a resource or with a zero flag byte; else the resource's slot 21.
void CheckForwardKey(difftest::Context &ctx, std::mt19937 &rng, int trials)
{
    InitPeerVT();
    Slots plain = PlainSlots(ctx);
    PrimarySlots prim = PrimarySlotsOf(ctx);
    Slots routed = plain;
    routed.slots[24] = Addr(&Sub::ForwardKey);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(0xa0fb19);

    for (int i = 0; i < trials; ++i) {
        State st = RandomState(rng);
        gRet21 = static_cast<uint32_t>(rng());
        Compare(ctx, "ForwardKey", st, plain, routed, prim, [&](int which, Side &, void *sub) -> uint64_t {
            if (which == 0) return orig(sub);
            if (which == 1) return reinterpret_cast<Sub *>(sub)->ForwardKey();
            return VirtualSlot<uint32_t(__thiscall *)(void *)>(sub, 24)(sub);
        });
    }
}

} // namespace

DIFF_TEST(cAnimResource_VirtualBaseIfObject, 0xa10195)
{
    std::mt19937 rng(0x6a01);
    CheckVirtualBaseIfObject(ctx, rng, 20000);
}

DIFF_TEST(cAnimResource_Owner, 0xa0f3ae)
{
    std::mt19937 rng(0x6a02);
    CheckOwner(ctx, rng, 20000);
}

DIFF_TEST(cAnimResource_Flag, 0xa0f57b)
{
    std::mt19937 rng(0x6a09);
    CheckFlag(ctx, rng, 20000);
}

DIFF_TEST(cAnimResource_VirtualBase, 0xa10187)
{
    std::mt19937 rng(0x6a03);
    CheckVirtualBase(ctx, rng, 20000);
}

DIFF_TEST(cAnimResource_CompareIdentity, 0xa0f554)
{
    std::mt19937 rng(0x6a04);
    CheckIdentity(ctx, rng, 20000);
}

DIFF_TEST(cAnimResource_SetResource, 0xa103ce)
{
    std::mt19937 rng(0x6a05);
    CheckSetResource(ctx, rng, 20000);
}

DIFF_TEST(cAnimResource_SetFlag, 0xa101e0)
{
    std::mt19937 rng(0x6a06);
    CheckSetFlag(ctx, rng, 20000);
}

DIFF_TEST(cAnimResource_CopyKey, 0xa101a3)
{
    std::mt19937 rng(0x6a07);
    CheckCopyKey(ctx, rng, 20000);
}

DIFF_TEST(cAnimResource_ForwardKey, 0xa0fb19)
{
    std::mt19937 rng(0x6a08);
    CheckForwardKey(ctx, rng, 20000);
}
