// Differential tests for cRZMutex. The original calls CreateMutexA, WaitForSingleObject,
// ReleaseMutex and CloseHandle through its import table, where the harness has traps; for these
// tests the four slots are pointed at the real functions (and restored afterwards). Each side gets
// its own named mutex, with a name unique to this run, so the two never touch each other's objects;
// the names are process-local ("Local\" namespace) and the mutexes are closed by the destructors.
#include "difftest.h"
#include "free_scope.h"
#include "rz/cRZMutex.h"
#include "rz/cRZString.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr uint32_t kIatWait = 0xd9a184, kIatRelease = 0xd9a3f0, kIatClose = 0xd9a17c, kIatCreate = 0xd9a3ec;

// Points the original's mutex imports at the real functions for the life of the object.
struct RealMutexImports {
    difftest::Context &ctx;
    void *saved[4];
    explicit RealMutexImports(difftest::Context &c) : ctx(c)
    {
        const uint32_t rvas[4] = {kIatWait, kIatRelease, kIatClose, kIatCreate};
        const char *names[4] = {"WaitForSingleObject", "ReleaseMutex", "CloseHandle", "CreateMutexA"};
        HMODULE k32 = GetModuleHandleA("kernel32.dll");
        for (int i = 0; i < 4; ++i) {
            void **slot = ctx.image.At<void *>(rvas[i]);
            saved[i] = *slot;
            *slot = reinterpret_cast<void *>(GetProcAddress(k32, names[i]));
        }
    }
    ~RealMutexImports()
    {
        const uint32_t rvas[4] = {kIatWait, kIatRelease, kIatClose, kIatCreate};
        for (int i = 0; i < 4; ++i) *ctx.image.At<void *>(rvas[i]) = saved[i];
    }
};

using CtorFn = cRZMutex *(__thiscall *)(void *, const void *);
using SlotFn = uint32_t(__thiscall *)(void *);
using GetHandleFn = uint32_t(__thiscall *)(void *, void **);
using DeletingFn = void *(__thiscall *)(void *, uint32_t);
using InnerDeletingFn = void *(__thiscall *)(void *, uint32_t);

constexpr uint32_t kCtor = 0xc6c85e, kDtor = 0xc6c8c3, kDeleting = 0xc6c8e0;
constexpr uint32_t kDelete = 0x9ccb85, kLock = 0xc6c906, kUnlock = 0xc6c93e, kTryLock = 0xc6c91e, kIsValid = 0xc6c8ff, kGetHandle = 0x9ccb69;
constexpr uint32_t kInnerDeleting = 0x218f06;

bool Live(const void *p)
{
    return !difftest::FreedSinceBegin(p); // the call under test ran after difftest::BeginFrees()
}

bool HandleOpen(void *h)
{
    DWORD flags = 0;
    return h != nullptr && GetHandleInformation(h, &flags) != FALSE;
}

std::string UniqueName(uint32_t run, char side, size_t pad = 0)
{
    // The process id at a fixed width, so a name's length (and its string's capacity) never depends on it.
    char pid[16];
    std::snprintf(pid, sizeof pid, "%010lu", static_cast<unsigned long>(GetCurrentProcessId()));
    return "Local\\TS2DiffTestMutex_" + std::string(pid) + "_" + std::to_string(run) + "_" + side + std::string(pad, 'x');
}

cRZMutex *RawMutex(uint8_t fill = 0xCD)
{
    auto *p = static_cast<cRZMutex *>(::operator new(sizeof(cRZMutex)));
    std::memset(p, fill, sizeof(cRZMutex));
    return p;
}

// Marks the same operation on both sides.
struct Side {
    cRZMutex *m;
    bool original;
};

} // namespace

DIFF_TEST(cRZMutex_Construct, 0xc6c85e)
{
    RealMutexImports imports(ctx);
    auto ctor = ctx.Original<CtorFn>(kCtor);
    auto dtor = ctx.Original<DeletingFn>(kDeleting);
    std::mt19937 rng(0x70701);
    for (uint32_t run = 0; run < 120; ++run) {
        // Names: plain, long (a heap string), empty, with a backslash in the middle (invalid for
        // CreateMutexA), too long for a mutex name.
        std::string a, b;
        switch (run % 6) {
        case 0: a = UniqueName(run, 'A'); b = UniqueName(run, 'B'); break;
        case 1: a = UniqueName(run, 'A', 20); b = UniqueName(run, 'B', 20); break;
        case 2: a = UniqueName(run, 'A', 300); b = UniqueName(run, 'B', 300); break; // over MAX_PATH: fails
        case 3: a = b = "bad\\name\\" + std::to_string(run); break;                  // not under Local\: fails or not, same on both
        case 4: a = "Local\\TS2DiffTest\\" + std::to_string(run); b = "Local\\TS2DiffTest\\" + std::to_string(run); break;
        default: {
            // One pad for both sides: the check below compares the two stored names' capacities, so the names must
            // have the same length (with separate pads, lengths 31 and 32 got capacities 31 and 47 whenever the
            // process id had three digits: the old full-suite flake on runs 47/53/71/77/83).
            const size_t pad = rng() % 5;
            a = UniqueName(run, 'A', pad);
            b = UniqueName(run, 'B', pad);
            break;
        }
        }
        cRZString na{}, nb{};
        na.Construct(a.c_str());
        nb.Construct(b.c_str());
        cRZMutex *x = RawMutex(), *y = RawMutex();
        cRZMutex *rx = ctor(x, na.AsInterface());
        cRZMutex *ry = y->Construct(*nb.AsInterface());
        ++ctx.cases;
        if (rx != x || ry != y) ctx.Fail("run %u: return value is not this", run);
        if (x->mVTable != y->mVTable) ctx.Fail("run %u: vtables differ", run);
        cRZMutexData *dx = x->mData, *dy = y->mData;
        if (!dx || !dy) {
            ctx.Fail("run %u: no data block", run);
            continue;
        }
        if (dx->mValid != dy->mValid || (dx->mHandle == nullptr) != (dy->mHandle == nullptr) || dx->mValid != (dx->mHandle != nullptr ? 1u : 0u)) {
            ctx.Fail("run %u '%.40s': valid %u/%u handle %p/%p", run, a.c_str(), dx->mValid, dy->mValid, dx->mHandle, dy->mHandle);
        }
        if (dx->mName.mString != a || dy->mName.mString != b || dx->mName.mVTable != dy->mName.mVTable || dx->mName.mRefCount != dy->mName.mRefCount
            || dx->mName.mString.capacity() != dy->mName.mString.capacity()) {
            ctx.Fail("run %u: stored name differs", run);
        }
        // The mutex is not owned after creation (bInitialOwner false): releasing it fails.
        if (dx->mHandle && dy->mHandle) {
            bool relX = ReleaseMutex(dx->mHandle) != FALSE, relY = ReleaseMutex(dy->mHandle) != FALSE;
            if (relX || relY) ctx.Fail("run %u: mutex was created owned (%d/%d)", run, relX, relY);
            // The names really exist.
            if (run % 6 != 3 && run % 6 != 4) {
                HANDLE ox = OpenMutexA(SYNCHRONIZE, FALSE, a.c_str()), oy = OpenMutexA(SYNCHRONIZE, FALSE, b.c_str());
                if (!ox || !oy) ctx.Fail("run %u: named mutex not found (%p/%p)", run, ox, oy);
                if (ox) CloseHandle(ox);
                if (oy) CloseHandle(oy);
            }
        }
        dtor(x, 1);
        y->DeletingDestructor(1);
    }
}

namespace {

// A mutex pair for the slot tests: original-built and ours, plain Local\ names.
struct MutexPair {
    cRZMutex *x;
    cRZMutex *y;
    std::string nameX, nameY;
};

MutexPair MakeMutexes(difftest::Context &ctx, uint32_t run)
{
    MutexPair p;
    p.nameX = UniqueName(run, 'A');
    p.nameY = UniqueName(run, 'B');
    cRZString na{}, nb{};
    na.Construct(p.nameX.c_str());
    nb.Construct(p.nameY.c_str());
    p.x = RawMutex();
    p.y = RawMutex();
    ctx.Original<CtorFn>(kCtor)(p.x, na.AsInterface());
    p.y->Construct(*nb.AsInterface());
    return p;
}

void FreeMutexes(difftest::Context &ctx, MutexPair &p)
{
    ctx.Original<DeletingFn>(kDeleting)(p.x, 1);
    p.y->DeletingDestructor(1);
}

// A thread that takes a named mutex and holds it until told, or exits holding it (abandoning it).
struct Holder {
    std::thread thread;
    std::atomic<int> state{0}; // 1 = holding, 2 = release requested
    explicit Holder(const std::string &name, bool abandon)
    {
        thread = std::thread([this, name, abandon] {
            HANDLE h = OpenMutexA(SYNCHRONIZE, FALSE, name.c_str());
            if (!h) {
                state = -1;
                return;
            }
            WaitForSingleObject(h, INFINITE);
            state = 1;
            if (!abandon) {
                while (state != 2) Sleep(1);
                ReleaseMutex(h);
            }
            CloseHandle(h);
        });
        while (state == 0) Sleep(1);
    }
    void Release()
    {
        state = 2;
        thread.join();
    }
};

} // namespace

DIFF_TEST(cRZMutex_LockUnlockTryLock, 0xc6c906)
{
    RealMutexImports imports(ctx);
    auto lockX = ctx.Original<SlotFn>(kLock);
    auto unlockX = ctx.Original<SlotFn>(kUnlock);
    auto tryX = ctx.Original<SlotFn>(kTryLock);
    std::mt19937 rng(0x70702);
    for (uint32_t run = 0; run < 60; ++run) {
        MutexPair p = MakeMutexes(ctx, run);
        // Unowned: unlock fails, a try lock and a lock succeed (the mutex is recursive), and each
        // lock needs an unlock.
        uint32_t steps = 1 + rng() % 6;
        for (uint32_t s = 0; s < steps; ++s) {
            uint32_t op = rng() % 3;
            uint32_t rx, ry;
            const char *name;
            if (op == 0) { rx = lockX(p.x); ry = p.y->Lock(); name = "Lock"; }
            else if (op == 1) { rx = tryX(p.x); ry = p.y->TryLock(); name = "TryLock"; }
            else { rx = unlockX(p.x); ry = p.y->Unlock(); name = "Unlock"; }
            ++ctx.cases;
            if (rx != ry || rx > 1) ctx.Fail("run %u step %u %s: %u/%u", run, s, name, rx, ry);
        }
        // Drain the ownership counts so another thread can take the mutex.
        while (unlockX(p.x)) {}
        while (p.y->Unlock()) {}
        // Another thread holds it: a try lock times out (false); once it lets go, true again.
        {
            Holder hx(p.nameX, false), hy(p.nameY, false);
            uint32_t rx = tryX(p.x), ry = p.y->TryLock();
            ++ctx.cases;
            if (rx != 0 || ry != 0) ctx.Fail("run %u: try lock while held by another thread: %u/%u", run, rx, ry);
            uint32_t ux = unlockX(p.x), uy = p.y->Unlock();
            if (ux != 0 || uy != 0) ctx.Fail("run %u: unlock of a mutex another thread owns: %u/%u", run, ux, uy);
            hx.Release();
            hy.Release();
            rx = tryX(p.x);
            ry = p.y->TryLock();
            if (rx != 1 || ry != 1) ctx.Fail("run %u: try lock after release: %u/%u", run, rx, ry);
            while (unlockX(p.x)) {}
            while (p.y->Unlock()) {}
        }
        // Abandoned (the owner thread exited holding it): the wait reports it, the call counts it as acquired.
        if (run % 3 == 0) {
            {
                Holder hx(p.nameX, true), hy(p.nameY, true);
                hx.thread.join();
                hy.thread.join();
            }
            bool useTry = run % 2 == 0; // the first wait on an abandoned mutex reports WAIT_ABANDONED_0
            uint32_t rx = useTry ? tryX(p.x) : lockX(p.x), ry = useTry ? p.y->TryLock() : p.y->Lock();
            ++ctx.cases;
            if (rx != 1 || ry != 1) ctx.Fail("run %u: %s on an abandoned mutex: %u/%u", run, useTry ? "try lock" : "lock", rx, ry);
        }
        FreeMutexes(ctx, p);
    }
}

DIFF_TEST(cRZMutex_FailedWaits, 0xc6c91e)
{
    // With no handle every Win32 call fails: WAIT_FAILED for the waits, FALSE for the release.
    RealMutexImports imports(ctx);
    auto lockX = ctx.Original<SlotFn>(kLock);
    auto unlockX = ctx.Original<SlotFn>(kUnlock);
    auto tryX = ctx.Original<SlotFn>(kTryLock);
    cRZMutexData dx{}, dy{};
    dx.mHandle = nullptr;
    dy.mHandle = nullptr;
    cRZMutex x{}, y{};
    x.mData = &dx;
    y.mData = &dy;
    uint32_t a = lockX(&x), b = y.Lock();
    uint32_t c = tryX(&x), d = y.TryLock();
    uint32_t e = unlockX(&x), f = y.Unlock();
    ++ctx.cases;
    if (a != b || c != d || e != f || a != 0 || c != 0 || e != 0) ctx.Fail("failed waits: lock %u/%u try %u/%u unlock %u/%u", a, b, c, d, e, f);
    // An invalid (closed) handle value fails the same way.
    HANDLE h = CreateMutexA(nullptr, FALSE, nullptr);
    CloseHandle(h);
    dx.mHandle = dy.mHandle = h;
    a = lockX(&x);
    b = y.Lock();
    c = tryX(&x);
    d = y.TryLock();
    e = unlockX(&x);
    f = y.Unlock();
    ++ctx.cases;
    if (a != b || c != d || e != f) ctx.Fail("closed handle: lock %u/%u try %u/%u unlock %u/%u", a, b, c, d, e, f);
}

DIFF_TEST(cRZMutex_IsValidGetHandle, 0xc6c8ff)
{
    auto isValid = ctx.Original<SlotFn>(kIsValid);
    auto getHandle = ctx.Original<GetHandleFn>(kGetHandle);
    std::mt19937 rng(0x70703);
    for (int run = 0; run < 200; ++run) {
        cRZMutexData dx{}, dy{};
        uint32_t valid = rng();
        void *handle = reinterpret_cast<void *>(static_cast<uintptr_t>(rng()));
        dx.mValid = dy.mValid = valid;
        dx.mHandle = dy.mHandle = handle;
        cRZMutex x{}, y{};
        x.mData = &dx;
        y.mData = &dy;
        void *ox = reinterpret_cast<void *>(0x1111), *oy = ox;
        uint32_t gx = getHandle(&x, &ox), gy = y.GetHandle(&oy);
        ++ctx.cases;
        if (isValid(&x) != y.IsValid() || y.IsValid() != valid) ctx.Fail("IsValid differs");
        if (gx != gy || gx != 1 || ox != oy || ox != handle) ctx.Fail("GetHandle: %u/%u out %p/%p", gx, gy, ox, oy);
    }
}

namespace {

std::vector<int> gDeleteLog;

void *__fastcall RecordingDeleting(void *self, void * /*edx*/, uint32_t flags)
{
    gDeleteLog.push_back(static_cast<int>(flags));
    return self;
}

} // namespace

DIFF_TEST(cRZMutex_Delete, 0x9ccb85)
{
    auto del = ctx.Original<SlotFn>(kDelete);
    void *vtable[7];
    std::memcpy(vtable, ctx.image.At(cRZMutex::kVTable), sizeof(vtable));
    vtable[5] = reinterpret_cast<void *>(&RecordingDeleting);
    cRZMutex x{}, y{};
    x.mVTable = y.mVTable = vtable;
    gDeleteLog.clear();
    uint32_t a = del(&x);
    std::vector<int> logA = gDeleteLog;
    gDeleteLog.clear();
    uint32_t b = y.Delete();
    ++ctx.cases;
    if (a != b || a != 0 || logA != gDeleteLog || logA.size() != 1 || logA[0] != 1) ctx.Fail("Delete: result %u/%u, deleting-destructor calls differ", a, b);
    // A null object does nothing.
    gDeleteLog.clear();
    cRZMutex *none = nullptr;
    uint32_t c = del(none);
    size_t callsA = gDeleteLog.size();
    uint32_t d = none->Delete();
    ++ctx.cases;
    if (c != 0 || d != 0 || callsA != 0 || !gDeleteLog.empty()) ctx.Fail("Delete on null: %u/%u, calls %zu/%zu", c, d, callsA, gDeleteLog.size());
}

DIFF_TEST(cRZMutex_Destruct, 0xc6c8c3)
{
    RealMutexImports imports(ctx);
    auto dtor = ctx.Original<DeletingFn>(kDeleting);
    auto plain = ctx.Original<SlotFn>(kDtor);
    for (uint32_t run = 0; run < 80; ++run) {
        uint32_t flags = run % 4;
        bool nonDeleting = run % 8 == 7;
        // Short names stay inside the string object; long names own a heap block.
        std::string suffix(run % 2 ? 30 : 0, 'n');
        MutexPair p;
        p.nameX = UniqueName(run, 'A') + suffix;
        p.nameY = UniqueName(run, 'B') + suffix;
        cRZString na{}, nb{};
        na.Construct(p.nameX.c_str());
        nb.Construct(p.nameY.c_str());
        p.x = RawMutex();
        p.y = RawMutex();
        ctx.Original<CtorFn>(kCtor)(p.x, na.AsInterface());
        p.y->Construct(*nb.AsInterface());
        void *hx = p.x->mData->mHandle, *hy = p.y->mData->mHandle;
        cRZMutexData *dx = p.x->mData, *dy = p.y->mData;
        const void *nameBlockX = dx->mName.mString.capacity() > 15 ? dx->mName.mString.data() : nullptr;
        const void *nameBlockY = dy->mName.mString.capacity() > 15 ? dy->mName.mString.data() : nullptr;
        difftest::BeginFrees();
        if (nonDeleting) plain(p.x);
        else dtor(p.x, flags);
        bool closedX = !HandleOpen(hx), dataFreedX = !Live(dx), nameFreedX = nameBlockX && !Live(nameBlockX), objFreedX = !Live(p.x);
        difftest::BeginFrees();
        if (nonDeleting) p.y->Destruct();
        else p.y->DeletingDestructor(flags);
        bool closedY = !HandleOpen(hy), dataFreedY = !Live(dy), nameFreedY = nameBlockY && !Live(nameBlockY), objFreedY = !Live(p.y);
        ++ctx.cases;
        if (closedX != closedY || dataFreedX != dataFreedY || nameFreedX != nameFreedY || objFreedX != objFreedY) {
            ctx.Fail("run %u flags %u%s: handle closed %d/%d, data freed %d/%d, name block freed %d/%d, object freed %d/%d", run, flags, nonDeleting ? " (plain)" : "",
                     closedX, closedY, dataFreedX, dataFreedY, nameFreedX, nameFreedY, objFreedX, objFreedY);
        }
        if (!closedX || !dataFreedX || (nameBlockX && !nameFreedX)) ctx.Fail("run %u: original left something open or allocated", run);
        if (objFreedX != (!nonDeleting && (flags & 1))) ctx.Fail("run %u flags %u: object freed %d", run, flags, objFreedX);
        if (!objFreedX) ::operator delete(p.x);
        if (!objFreedY) ::operator delete(p.y);
    }
}

DIFF_TEST(cRZMutexData_DeletingDestructor, 0x218f06)
{
    auto orig = ctx.Original<InnerDeletingFn>(kInnerDeleting);
    std::mt19937 rng(0x70704);
    for (uint32_t run = 0; run < 100; ++run) {
        uint32_t flags = run % 4;
        auto *a = static_cast<cRZMutexData *>(::operator new(sizeof(cRZMutexData)));
        auto *b = static_cast<cRZMutexData *>(::operator new(sizeof(cRZMutexData)));
        std::memset(a, 0xCD, sizeof(*a));
        std::memset(b, 0xCD, sizeof(*b));
        std::string name(rng() % 2 ? 5 : 40, 'q');
        a->mName.Construct(name.c_str());
        b->mName.Construct(name.c_str());
        a->mHandle = b->mHandle = nullptr;
        a->mValid = b->mValid = 0;
        const void *blockA = a->mName.mString.capacity() > 15 ? a->mName.mString.data() : nullptr;
        const void *blockB = b->mName.mString.capacity() > 15 ? b->mName.mString.data() : nullptr;
        difftest::BeginFrees();
        void *ra = orig(a, flags);
        bool objA = !Live(a), blkA = blockA && !Live(blockA);
        difftest::BeginFrees();
        void *rb = b->DeletingDestructor(flags);
        bool objB = !Live(b), blkB = blockB && !Live(blockB);
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("run %u: return value is not this", run);
        if (objA != objB || blkA != blkB || objA != ((flags & 1) != 0) || (blockA && !blkA)) ctx.Fail("run %u flags %u: object freed %d/%d, name block freed %d/%d", run, flags, objA, objB, blkA, blkB);
        if (!objA) {
            // Both strings are left reset: string vtable, small empty string.
            if (a->mName.mVTable != b->mName.mVTable || a->mName.mString.size() != b->mName.mString.size() || a->mName.mString.capacity() != b->mName.mString.capacity()) {
                ctx.Fail("run %u: name state after destruction differs", run);
            }
            ::operator delete(a);
            ::operator delete(b);
        }
    }
}
