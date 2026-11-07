// cRZLock (lane FD; src/rz/cRZLock.h): the constructor (0x1d8af0) and the deleting destructor (0x142248) against ours
// on random memory around a fake lockable that records its lock and unlock calls; the owned cell and our table.
#include "difftest.h"
#include "rz/cRZLock.h"

#include <cstring>
#include <random>

namespace {

// A lockable: slot 1 locks, slot 2 unlocks; every call is recorded (which slot, on which object).
struct Calls {
    void *self[4];
    int slot[4];
    int count;
} gCalls;
void Record(void *self, int slot)
{
    if (gCalls.count < 4) {
        gCalls.self[gCalls.count] = self;
        gCalls.slot[gCalls.count] = slot;
    }
    ++gCalls.count;
}
int32_t __fastcall SlotOther(void *self, void *) { Record(self, 99); return 0; }
int32_t __fastcall SlotLock(void *self, void *) { Record(self, 1); return 7; }
int32_t __fastcall SlotUnlock(void *self, void *) { Record(self, 2); return 5; }
void *const kLockableTable[7] = {reinterpret_cast<void *>(&SlotOther), reinterpret_cast<void *>(&SlotLock),
                                 reinterpret_cast<void *>(&SlotUnlock), reinterpret_cast<void *>(&SlotOther),
                                 reinterpret_cast<void *>(&SlotOther), reinterpret_cast<void *>(&SlotOther),
                                 reinterpret_cast<void *>(&SlotOther)};
struct Lockable {
    void *const *vtable = kLockableTable;
    uint32_t pad[3] = {};
};

template <typename T> const void *Member(T pmf)
{
    const void *p;
    std::memcpy(&p, &pmf, sizeof(p));
    return p;
}

bool SameCalls(const Calls &a, const Calls &b, const void *selfA, const void *selfB)
{
    if (a.count != b.count) return false;
    for (int i = 0; i < a.count && i < 4; ++i)
        if (a.slot[i] != b.slot[i] || (a.self[i] == selfA) != (b.self[i] == selfB)) return false;
    return true;
}

} // namespace

DIFF_TEST(cRZLock_Cells, 0xd9b644)
{
    ++ctx.cases;
    if (&ts2_rva_d9b644 != ctx.image.At(0xd9b644)) ctx.Fail("ts2_rva_d9b644 %p, the image's %p", &ts2_rva_d9b644, ctx.image.At(0xd9b644));
    ++ctx.cases;
    if (*reinterpret_cast<const void *const *>(ctx.image.At(0xd9b644)) != ctx.image.At(0x142248)) ctx.Fail("image slot 0 is not 0x142248");
    ++ctx.cases;
    if (ts2_own_vt_d9b644[0] != Member(&cRZLock::DeletingDestructor)) ctx.Fail("own table slot 0");
}

// 0x1d8af0: random memory (guards on both sides), the lockable locked once, this returned; the eight bytes compared
// (the table word: the image's, which ours stores through &ts2_rva_d9b644 in a hooked build).
DIFF_TEST(cRZLock_Construct, 0x1d8af0)
{
    using Fn = void *(__thiscall *)(void *, void *);
    auto orig = ctx.Original<Fn>(0x1d8af0);
    std::mt19937 gen(0x1d8af0);
    Lockable lockable;
    for (int i = 0; i < 64; ++i) {
        uint32_t a[4], b[4];
        for (int k = 0; k < 4; ++k) a[k] = b[k] = gen();
        gCalls = {};
        void *const ra = orig(&a[1], &lockable);
        const Calls ca = gCalls;
        gCalls = {};
        void *const rb = reinterpret_cast<cRZLock *>(&b[1])->Construct(&lockable);
        const Calls cb = gCalls;
        ++ctx.cases;
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("case %d: objects differ (%08x %08x vs %08x %08x)", i, a[1], a[2], b[1], b[2]);
        if (ra != &a[1] || rb != &b[1]) ctx.Fail("case %d: returns %p / %p", i, ra, rb);
        if (ca.count != 1 || ca.slot[0] != 1 || ca.self[0] != &lockable || !SameCalls(ca, cb, &lockable, &lockable))
            ctx.Fail("case %d: lock calls %d (slot %d) vs %d (slot %d)", i, ca.count, ca.slot[0], cb.count, cb.slot[0]);
    }
}

// 0x142248 with bit 0 clear: the table word put back, the lockable unlocked once, this returned. (With bit 0 set it
// frees the object; its byte match (tools/matchcheck.py) covers that path.)
DIFF_TEST(cRZLock_DeletingDestructor, 0x142248)
{
    using Fn = void *(__thiscall *)(void *, uint32_t);
    auto orig = ctx.Original<Fn>(0x142248);
    std::mt19937 gen(0x142248);
    Lockable lockable;
    for (int i = 0; i < 64; ++i) {
        uint32_t a[4], b[4];
        for (int k = 0; k < 4; ++k) a[k] = b[k] = gen();
        a[2] = b[2] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&lockable));
        const uint32_t flags = gen() & ~1u;
        gCalls = {};
        void *const ra = orig(&a[1], flags);
        const Calls ca = gCalls;
        gCalls = {};
        void *const rb = reinterpret_cast<cRZLock *>(&b[1])->DeletingDestructor(flags);
        const Calls cb = gCalls;
        ++ctx.cases;
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("case %d flags 0x%x: objects differ (%08x vs %08x)", i, flags, a[1], b[1]);
        if (a[1] != static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ctx.image.At(0xd9b644)))) ctx.Fail("case %d: table word %08x", i, a[1]);
        if (ra != &a[1] || rb != &b[1]) ctx.Fail("case %d: returns %p / %p", i, ra, rb);
        if (ca.count != 1 || ca.slot[0] != 2 || !SameCalls(ca, cb, &lockable, &lockable))
            ctx.Fail("case %d: unlock calls %d (slot %d) vs %d (slot %d)", i, ca.count, ca.slot[0], cb.count, cb.slot[0]);
    }
}

// Our scope use (the persist system's and the speculative loader's): lock on entry, unlock on exit, one each, in order.
DIFF_TEST(cRZLock_Scope, 0x1d8af0)
{
    Lockable lockable;
    gCalls = {};
    {
        cRZLock lock(&lockable);
        ++ctx.cases;
        if (gCalls.count != 1 || gCalls.slot[0] != 1) ctx.Fail("entry: %d calls", gCalls.count);
        ++ctx.cases;
        if (static_cast<const void *>(lock.mVTable) != ctx.image.At(0xd9b644) || lock.mLockable != &lockable) ctx.Fail("lock object");
    }
    ++ctx.cases;
    if (gCalls.count != 2 || gCalls.slot[1] != 2 || gCalls.self[1] != &lockable) ctx.Fail("exit: %d calls", gCalls.count);
}
