#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "hook/hooks.h"
#include "rz/cRZCriticalSection.h"

#include <windows.h>

#include <random>
#include <thread>

namespace {

using Thiscall0 = int32_t(__thiscall *)(void *);

// The game's vtable (0xda2708) in the mapped image, set by each test before comparing.
const void *gGameVTable;

// Compares the parts of the two locks that don't depend on which object they live in. `a` is the original's and
// carries the game's vtable; `b` is ours and carries `ours` (VTable(), the game's in this harness; or OwnVTable()).
bool SameState(const cRZCriticalSection &a, const cRZCriticalSection &b, const void *ours = cRZCriticalSection::VTable())
{
    auto *ca = reinterpret_cast<const CRITICAL_SECTION *>(a.mBlock);
    auto *cb = reinterpret_cast<const CRITICAL_SECTION *>(b.mBlock);
    return a.mVTable == gGameVTable && b.mVTable == ours
        && a.mBlock->lockCount == b.mBlock->lockCount
        && ca->RecursionCount == cb->RecursionCount && ca->OwningThread == cb->OwningThread
        && ca->LockCount == cb->LockCount;
}

// Holds a lock on another thread until released, so TryLock sees it busy.
struct Holder {
    HANDLE locked = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread t;
    explicit Holder(CRITICAL_SECTION *cs)
        : t([this, cs] { EnterCriticalSection(cs); SetEvent(locked); WaitForSingleObject(release, INFINITE); LeaveCriticalSection(cs); })
    {
        WaitForSingleObject(locked, INFINITE);
    }
    ~Holder() { SetEvent(release); t.join(); CloseHandle(locked); CloseHandle(release); }
};

} // namespace

DIFF_TEST(cRZCriticalSection_All, 0x1af64e)
{
    auto construct = ctx.Original<void *(__thiscall *)(void *)>(0x1af64e);
    auto lock = ctx.Original<Thiscall0>(0x1af6ce);
    auto unlock = ctx.Original<Thiscall0>(0x1af715);
    auto tryLock = ctx.Original<Thiscall0>(0x1af6f0);
    auto isLocked = ctx.Original<Thiscall0>(0x1af6c7);
    auto release = ctx.Original<Thiscall0>(0x1af6e2);
    auto *tryEnterPtr = ctx.image.At<void *>(cRZCriticalSection::kTryEnterCriticalSection);
    void *realTryEnter = reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "TryEnterCriticalSection"));
    gGameVTable = ctx.image.At(cRZCriticalSection::kVTable);

    std::mt19937 rng(9);
    for (int round = 0; round < 400; ++round) {
        // Both TryLock branches: the game's pointer unset (blocking fallback) and set.
        *tryEnterPtr = round % 2 ? realTryEnter : nullptr;

        auto *a = static_cast<cRZCriticalSection *>(::operator new(sizeof(cRZCriticalSection)));
        auto *b = static_cast<cRZCriticalSection *>(::operator new(sizeof(cRZCriticalSection)));
        void *ra = construct(a);
        cRZCriticalSection *rb = b->Construct();
        ++ctx.cases;
        if (ra != a || rb != b || !SameState(*a, *b)) ctx.Fail("Construct differs");

        int depth = 0;
        for (int step = 0; step < 50; ++step) {
            int op = static_cast<int>(rng() % 4);
            if (op == 1 && depth == 0) op = 0; // never unlock below zero
            int32_t wa = 0, wb = 0;
            switch (op) {
            case 0: wa = lock(a); wb = b->Lock(); ++depth; break;
            case 1: wa = unlock(a); wb = b->Unlock(); --depth; break;
            case 2: wa = tryLock(a); wb = b->TryLock(); ++depth; break;
            default: wa = isLocked(a); wb = b->IsLocked(); break;
            }
            ++ctx.cases;
            if (wa != wb || !SameState(*a, *b)) ctx.Fail("round %d step %d op %d: original %d, ours %d", round, step, op, wa, wb);
        }
        while (depth-- > 0) { unlock(a); b->Unlock(); }

        if (*tryEnterPtr) {
            // Busy: held by another thread, TryLock must fail without changing the count.
            Holder ha(reinterpret_cast<CRITICAL_SECTION *>(a->mBlock)), hb(reinterpret_cast<CRITICAL_SECTION *>(b->mBlock));
            int32_t wa = tryLock(a), wb = b->TryLock();
            ++ctx.cases;
            if (wa != wb || wa != 0 || a->mBlock->lockCount != b->mBlock->lockCount) ctx.Fail("busy TryLock: original %d, ours %d", wa, wb);
        }

        // Release deletes the object through slot 6 of its (original) vtable.
        int32_t wa = release(a), wb = b->Release();
        ++ctx.cases;
        if (wa != wb) ctx.Fail("Release: original %d, ours %d", wa, wb);
    }
    // Release on a null object returns 0 without touching anything.
    auto ours = reinterpret_cast<Thiscall0>(MemberAddress(&cRZCriticalSection::Release));
    ++ctx.cases;
    if (release(nullptr) != ours(nullptr)) ctx.Fail("Release(null) differs");
    *tryEnterPtr = nullptr;
    if (!HeapValidate(GetProcessHeap(), 0, nullptr)) ctx.Fail("process heap corrupted");
}

// Our own vtable (lane QB; the image-free program's): every slot called by number through each object's own vtable,
// the original's object built by the original constructor (the game's vtable 0xda2708, original bodies; slot 4 is the
// folded 4-byte IsValid 0x886f4) and ours by our constructor, then given OwnVTable() (rzface::cRZCriticalSection's
// slots, our IsValid). Random slot 1..5 calls,
// the busy TryLock, then the end of life by slot 0 (Release) or slot 6 (deleting destructor, flags 0 or 1).
DIFF_TEST(cRZCriticalSection_OurVTable, 0x886f4)
{
    using Slot = int32_t(__thiscall *)(void *);
    using Deleting = void *(__thiscall *)(void *, uint32_t);
    auto construct = ctx.Original<void *(__thiscall *)(void *)>(0x1af64e);
    auto *tryEnterPtr = ctx.image.At<void *>(cRZCriticalSection::kTryEnterCriticalSection);
    void *realTryEnter = reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "TryEnterCriticalSection"));
    gGameVTable = ctx.image.At(cRZCriticalSection::kVTable);
    auto slot = [](void *object, int index) { return (*static_cast<void *const *const *>(object))[index]; };

    // Ours is our own table: none of its seven slots is an image address.
    const void *const *ours = static_cast<const void *const *>(cRZCriticalSection::OwnVTable());
    for (int i = 0; i < 7; ++i) {
        ++ctx.cases;
        const uintptr_t at = reinterpret_cast<uintptr_t>(ours[i]);
        if (at - ctx.image.Base() < ctx.image.Size()) ctx.Fail("our vtable slot %d points into the image", i);
    }

    std::mt19937 rng(31);
    for (int round = 0; round < 400; ++round) {
        *tryEnterPtr = round % 2 ? realTryEnter : nullptr;
        auto *a = static_cast<cRZCriticalSection *>(::operator new(sizeof(cRZCriticalSection)));
        auto *b = static_cast<cRZCriticalSection *>(::operator new(sizeof(cRZCriticalSection)));
        construct(a);
        b->Construct();
        ++ctx.cases;
        if (!SameState(*a, *b)) ctx.Fail("round %d: Construct differs", round);
        b->mVTable = ours;

        int depth = 0;
        for (int step = 0; step < 40; ++step) {
            int index = 1 + static_cast<int>(rng() % 5);
            if (index == 2 && depth == 0) index = 1; // never unlock below zero
            int32_t wa = reinterpret_cast<Slot>(slot(a, index))(a);
            int32_t wb = reinterpret_cast<Slot>(slot(b, index))(b);
            if (index == 1 || index == 3) ++depth;
            if (index == 2) --depth;
            ++ctx.cases;
            if (wa != wb || !SameState(*a, *b, ours)) ctx.Fail("round %d step %d slot %d: original %d, ours %d", round, step, index, wa, wb);
        }
        while (depth-- > 0) {
            reinterpret_cast<Slot>(slot(a, 2))(a);
            reinterpret_cast<Slot>(slot(b, 2))(b);
        }
        if (*tryEnterPtr) {
            // Unlocked here first: the holder thread must be able to take both locks.
            Holder ha(reinterpret_cast<CRITICAL_SECTION *>(a->mBlock)), hb(reinterpret_cast<CRITICAL_SECTION *>(b->mBlock));
            int32_t wa = reinterpret_cast<Slot>(slot(a, 3))(a), wb = reinterpret_cast<Slot>(slot(b, 3))(b);
            ++ctx.cases;
            if (wa != wb || wa != 0 || a->mBlock->lockCount != b->mBlock->lockCount) ctx.Fail("busy TryLock: original %d, ours %d", wa, wb);
        }

        const int end = static_cast<int>(rng() % 3);
        ++ctx.cases;
        if (end == 0) {
            int32_t wa = reinterpret_cast<Slot>(slot(a, 0))(a), wb = reinterpret_cast<Slot>(slot(b, 0))(b);
            if (wa != wb) ctx.Fail("round %d: Release original %d, ours %d", round, wa, wb);
        } else {
            const uint32_t flags = end == 1 ? 0u : 1u;
            void *ra, *rb;
            bool freedA, freedB;
            {
                difftest::FreeRecorder rec;
                ra = reinterpret_cast<Deleting>(slot(a, 6))(a, flags);
                freedA = rec.Freed(a);
            }
            {
                difftest::FreeRecorder rec;
                rb = reinterpret_cast<Deleting>(slot(b, 6))(b, flags);
                freedB = rec.Freed(b);
            }
            if (ra != a || rb != b) ctx.Fail("round %d: deleting destructor (%u) returned %p/%p", round, flags, ra, rb);
            ++ctx.cases;
            if (freedA != freedB || freedA != (flags == 1)) ctx.Fail("round %d: deleting destructor (%u) freed the object: original %d, ours %d", round, flags, freedA, freedB);
            if (flags == 0) {
                // Destroyed, not freed: each destructor stores its class's vtable word (ours VTable()).
                ++ctx.cases;
                if (a->mVTable != gGameVTable || b->mVTable != cRZCriticalSection::VTable()) ctx.Fail("round %d: vtable after destruction", round);
                ::operator delete(a);
                ::operator delete(b);
            }
        }
    }
    *tryEnterPtr = nullptr;
    if (!HeapValidate(GetProcessHeap(), 0, nullptr)) ctx.Fail("process heap corrupted");
}
