// Differential tests for the thread-state block, cRZThread's constructor and name, the callback
// thread's constructor and the callback system service's constructor. The stopwatch's counter and
// the RaiseException used to announce a thread name are made observable: the original's import
// slots point at recording fakes, ours reads the same script (cRZStopwatch::sPerformanceCounterOverride)
// or raises the real exception, which a vectored handler records and continues.
#include "difftest.h"
#include "rz/cRZCallbackSystemService.h"
#include "rz/cRZThread.h"

#include <windows.h>

#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kStateCtor = 0x1b9aec, kStateAddRef = 0x1b9b9e, kThreadCtor = 0x1b9b19, kSetName = 0x1b9f30, kAnnounce = 0x1b9e26;
constexpr uint32_t kCallbackThreadCtor = 0x9cfa5d, kServiceCtor = 0xc68264;
constexpr uint32_t kIatQpc = 0xd9a24c, kIatRaiseException = 0xd9a180;

// ---- Scripted clock ---------------------------------------------------------------------------------

uint64_t gNow = 0;

BOOL WINAPI FakeQpc(LARGE_INTEGER *out)
{
    out->QuadPart = static_cast<int64_t>(gNow);
    return TRUE;
}

void FakeCounter(uint64_t *out) { *out = gNow; }

struct ScriptedClock {
    difftest::Context &ctx;
    void *saved;
    explicit ScriptedClock(difftest::Context &c) : ctx(c)
    {
        void **slot = ctx.image.At<void *>(kIatQpc);
        saved = *slot;
        *slot = reinterpret_cast<void *>(&FakeQpc);
        cRZStopwatch::sPerformanceCounterOverride = &FakeCounter;
    }
    ~ScriptedClock()
    {
        *ctx.image.At<void *>(kIatQpc) = saved;
        cRZStopwatch::sPerformanceCounterOverride = nullptr;
    }
};

// ---- RaiseException recording ----------------------------------------------------------------------

struct Raised {
    uint32_t code, flags, count;
    uint32_t type; // info[0]
    std::string name;
    uint32_t threadId, info3;
    bool operator==(const Raised &o) const { return code == o.code && flags == o.flags && count == o.count && type == o.type && name == o.name && threadId == o.threadId && info3 == o.info3; }
};

std::vector<Raised> gRaised;

Raised Describe(uint32_t code, uint32_t flags, uint32_t count, const ULONG_PTR *args)
{
    return {code, flags, count, static_cast<uint32_t>(args[0]), reinterpret_cast<const char *>(args[1]), static_cast<uint32_t>(args[2]), static_cast<uint32_t>(args[3])};
}

// The original's import slot.
void WINAPI FakeRaiseException(DWORD code, DWORD flags, DWORD count, const ULONG_PTR *args) { gRaised.push_back(Describe(code, flags, count, args)); }

// Ours: the real exception, seen by a vectored handler that continues.
LONG CALLBACK Handler(EXCEPTION_POINTERS *info)
{
    EXCEPTION_RECORD *r = info->ExceptionRecord;
    if (r->ExceptionCode != cRZThread::kThreadNameException) return EXCEPTION_CONTINUE_SEARCH;
    gRaised.push_back(Describe(r->ExceptionCode, r->ExceptionFlags, r->NumberParameters, r->ExceptionInformation));
    return EXCEPTION_CONTINUE_EXECUTION;
}

struct RaiseScript {
    difftest::Context &ctx;
    void *saved;
    explicit RaiseScript(difftest::Context &c) : ctx(c)
    {
        void **slot = ctx.image.At<void *>(kIatRaiseException);
        saved = *slot;
        *slot = reinterpret_cast<void *>(&FakeRaiseException);
    }
    ~RaiseScript() { *ctx.image.At<void *>(kIatRaiseException) = saved; }
};

// ---- Comparisons ---------------------------------------------------------------------------------------

bool SameState(const cRZThreadState &a, const cRZThreadState &b)
{
    return a.mField0 == b.mField0 && a.mField4 == b.mField4 && a.mThreadId == b.mThreadId && a.mMagic == b.mMagic && a.mName.mVTable == b.mName.mVTable
        && a.mName.mString == b.mName.mString && a.mName.mRefCount == b.mName.mRefCount && a.mRefCount == b.mRefCount && a.mLock.mVTable == b.mLock.mVTable
        && a.mLock.mBlock->lockCount == b.mLock.mBlock->lockCount;
}

void FreeState(cRZThreadState *s)
{
    s->mLock.Destruct();
    s->mName.Destruct();
    ::operator delete(s);
}

// Everything but the heap pointers (the state block, the lock's block) and bytes never written
// (both sides start from the same fill).
bool SameCallbackThread(const cRZCallbackThread &a, const cRZCallbackThread &b)
{
    if (a.mVTable != b.mVTable || a.mStartableVTable != b.mStartableVTable || a.mRunning != b.mRunning || a.mOwnedByCaller != b.mOwnedByCaller) return false;
    if (a.mField0c != b.mField0c || a.mField10 != b.mField10 || a.mField14 != b.mField14 || a.mField18 != b.mField18) return false;
    if (a.mStopwatch.mMode != b.mStopwatch.mMode || a.mStopwatch.mStart != b.mStopwatch.mStart || a.mStopwatch.mAccumulated != b.mStopwatch.mAccumulated) return false;
    if (a.mThread.mVTable != b.mThread.mVTable || a.mField40 != b.mField40 || a.mLock.mVTable != b.mLock.mVTable) return false;
    if (a.mLock.mBlock->lockCount != b.mLock.mBlock->lockCount) return false;
    return SameState(*a.mThread.mState, *b.mThread.mState);
}

void FreeCallbackThread(cRZCallbackThread *t)
{
    FreeState(t->mThread.mState);
    t->mLock.Destruct();
    ::operator delete(t);
}

} // namespace

DIFF_TEST(cRZThreadState_Construct, 0x1b9aec)
{
    auto orig = ctx.Original<cRZThreadState *(__thiscall *)(void *)>(kStateCtor);
    for (int fill : {0xCD, 0x00, 0xFF}) {
        auto *a = static_cast<cRZThreadState *>(::operator new(sizeof(cRZThreadState)));
        auto *b = static_cast<cRZThreadState *>(::operator new(sizeof(cRZThreadState)));
        std::memset(a, fill, sizeof(*a));
        std::memset(b, fill, sizeof(*b));
        cRZThreadState *ra = orig(a), *rb = b->Construct();
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("fill %02x: return value is not this", fill);
        if (!SameState(*a, *b) || a->mMagic != 0x12345678 || a->mRefCount != 0) ctx.Fail("fill %02x: state differs", fill);
        FreeState(a);
        FreeState(b);
    }
}

DIFF_TEST(cRZThreadState_AddRef, 0x1b9b9e)
{
    auto ctor = ctx.Original<cRZThreadState *(__thiscall *)(void *)>(kStateCtor);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *)>(kStateAddRef);
    for (uint32_t start : {0u, 1u, 7u, 0xffffffffu}) {
        auto *a = static_cast<cRZThreadState *>(::operator new(sizeof(cRZThreadState)));
        auto *b = static_cast<cRZThreadState *>(::operator new(sizeof(cRZThreadState)));
        ctor(a);
        ctor(b);
        a->mRefCount = b->mRefCount = start;
        uint32_t ra = orig(a), rb = b->AddRef();
        ++ctx.cases;
        if (ra != rb || ra != start + 1 || !SameState(*a, *b)) ctx.Fail("from %u: %u/%u", start, ra, rb);
        FreeState(a);
        FreeState(b);
    }
}

DIFF_TEST(cRZThread_Construct, 0x1b9b19)
{
    auto orig = ctx.Original<cRZThread *(__thiscall *)(void *)>(kThreadCtor);
    for (int fill : {0xCD, 0x00}) {
        cRZThread a, b;
        std::memset(&a, fill, sizeof(a));
        std::memset(&b, fill, sizeof(b));
        cRZThread *ra = orig(&a), *rb = b.Construct();
        ++ctx.cases;
        if (ra != &a || rb != &b || a.mVTable != b.mVTable || a.mVTable != ctx.image.At(cRZThread::kVTable)) ctx.Fail("fill %02x: return or vtable", fill);
        if (!SameState(*a.mState, *b.mState) || a.mState->mRefCount != 1) ctx.Fail("fill %02x: state differs (count %u/%u)", fill, a.mState->mRefCount, b.mState->mRefCount);
        FreeState(a.mState);
        FreeState(b.mState);
    }
}

DIFF_TEST(cRZThread_AnnounceThreadName, 0x1b9e26)
{
    RaiseScript script(ctx);
    auto orig = ctx.Original<void(__cdecl *)(const char *, uint32_t)>(kAnnounce);
    void *handler = AddVectoredExceptionHandler(1, &Handler);
    const char *names[] = {"", "a", "CallbackThread", "a name longer than the small string buffer holds"};
    for (const char *name : names) {
        for (uint32_t id : {0u, 1u, 0x1234u, 0xffffffffu}) {
            gRaised.clear();
            orig(name, id);
            std::vector<Raised> raisedA = gRaised;
            gRaised.clear();
            cRZThread::AnnounceThreadName(name, id);
            ++ctx.cases;
            if (raisedA.size() != 1 || raisedA != gRaised) ctx.Fail("name '%s' id %u: %zu/%zu exceptions or arguments differ", name, id, raisedA.size(), gRaised.size());
            if (!raisedA.empty() && (raisedA[0].code != 0x406d1388 || raisedA[0].count != 4 || raisedA[0].type != 0x1000)) ctx.Fail("original's exception unexpected");
        }
    }
    RemoveVectoredExceptionHandler(handler);
}

DIFF_TEST(cRZThread_SetName, 0x1b9f30)
{
    RaiseScript script(ctx);
    auto ctor = ctx.Original<cRZThread *(__thiscall *)(void *)>(kThreadCtor);
    auto orig = ctx.Original<uint8_t(__thiscall *)(void *, const char *)>(kSetName);
    void *handler = AddVectoredExceptionHandler(1, &Handler);
    std::mt19937 rng(0x7AD);
    const char *names[] = {"", "a", "CallbackThread", "a name longer than the small string buffer holds", nullptr};
    for (int run = 0; run < 40; ++run) {
        cRZThread a, b;
        ctor(&a);
        ctor(&b);
        // Start from a previous name (short or long) so clearing and reassigning both show.
        const char *previous = names[rng() % 4];
        a.mState->mName.mString = previous;
        b.mState->mName.mString = previous;
        uint32_t id = rng() % 3 == 0 ? 0 : 1 + rng() % 5000;
        a.mState->mThreadId = b.mState->mThreadId = id;
        const char *name = names[rng() % 5];
        gRaised.clear();
        uint8_t ra = orig(&a, name);
        std::vector<Raised> raisedA = gRaised;
        gRaised.clear();
        bool rb = b.SetName(name);
        ++ctx.cases;
        if (ra != static_cast<uint8_t>(rb) || ra != (id != 0)) ctx.Fail("run %d: result %u/%u for id %u", run, ra, rb, id);
        if (raisedA != gRaised || raisedA.size() != (id != 0 ? 1u : 0u)) ctx.Fail("run %d: announced %zu/%zu times or with different arguments", run, raisedA.size(), gRaised.size());
        if (!SameState(*a.mState, *b.mState)) ctx.Fail("run %d: state differs ('%s' / '%s')", run, a.mState->mName.mString.c_str(), b.mState->mName.mString.c_str());
        FreeState(a.mState);
        FreeState(b.mState);
    }
    RemoveVectoredExceptionHandler(handler);
}

DIFF_TEST(cRZCallbackThread_Construct, 0x9cfa5d)
{
    ScriptedClock clock(ctx);
    auto orig = ctx.Original<cRZCallbackThread *(__thiscall *)(void *)>(kCallbackThreadCtor);
    std::mt19937 rng(0xCB7);
    for (int run = 0; run < 8; ++run) {
        gNow = (static_cast<uint64_t>(rng()) << 20) ^ rng();
        int fill = run % 3 == 0 ? 0x00 : run % 3 == 1 ? 0xCD : 0xFF;
        auto *a = static_cast<cRZCallbackThread *>(::operator new(sizeof(cRZCallbackThread)));
        auto *b = static_cast<cRZCallbackThread *>(::operator new(sizeof(cRZCallbackThread)));
        std::memset(a, fill, sizeof(*a));
        std::memset(b, fill, sizeof(*b));
        cRZCallbackThread *ra = orig(a), *rb = b->Construct();
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("run %d: return value is not this", run);
        if (!SameCallbackThread(*a, *b)) ctx.Fail("run %d: object differs", run);
        // Bytes the constructor never writes keep the fill, on both sides.
        for (size_t offset : {size_t(0xa), size_t(0xb), size_t(0x1c), size_t(0x1d), size_t(0x1e), size_t(0x1f), size_t(0x24), size_t(0x4c), size_t(0x4d), size_t(0x4e), size_t(0x4f)}) {
            if (reinterpret_cast<unsigned char *>(a)[offset] != fill || reinterpret_cast<unsigned char *>(b)[offset] != fill) ctx.Fail("run %d: byte %zx was written", run, offset);
        }
        if (a->mStopwatch.mMode != 4 || a->mStopwatch.mStart != gNow || a->mRunning != 1 || a->mField40 != 1 || a->mThread.mState->mRefCount != 1) ctx.Fail("run %d: original's fields unexpected", run);
        if (b->mThread.mState->mName.mString.empty()) ctx.Fail("run %d: the thread was not named", run);
        FreeCallbackThread(a);
        FreeCallbackThread(b);
    }
}

namespace {

// The callback thread's slot 1 (AddRef) body isn't isolated in the harness (it is original code
// either way); a vtable with a recording AddRef keeps the service constructor's call observable.
std::vector<void *> gAddRefs;

uint32_t __fastcall RecAddRef(void *self, void *)
{
    gAddRefs.push_back(self);
    return 1;
}

} // namespace

DIFF_TEST(cRZCallbackSystemService_Construct, 0xc68264)
{
    ScriptedClock clock(ctx);
    auto orig = ctx.Original<cRZCallbackSystemService *(__thiscall *)(void *)>(kServiceCtor);
    // Both sides create the thread with the game's vtable; slot 1 is replaced in the image's table
    // by a recorder for the duration of the test (restored afterwards).
    void **vtable = ctx.image.At<void *>(cRZCallbackThread::kVTable);
    DWORD oldProtect = 0;
    VirtualProtect(vtable, 0x24, PAGE_READWRITE, &oldProtect);
    void *savedSlot = vtable[1];
    vtable[1] = reinterpret_cast<void *>(&RecAddRef);
    std::mt19937 rng(0xC5E);
    for (int run = 0; run < 8; ++run) {
        gNow = (static_cast<uint64_t>(rng()) << 20) ^ rng();
        int fill = run % 3 == 0 ? 0x00 : run % 3 == 1 ? 0xCD : 0xFF;
        auto *a = static_cast<cRZCallbackSystemService *>(::operator new(sizeof(cRZCallbackSystemService)));
        auto *b = static_cast<cRZCallbackSystemService *>(::operator new(sizeof(cRZCallbackSystemService)));
        std::memset(a, fill, sizeof(*a));
        std::memset(b, fill, sizeof(*b));
        gAddRefs.clear();
        cRZCallbackSystemService *ra = orig(a);
        auto *threadA = static_cast<cRZCallbackThread *>(a->mCallbackThread);
        std::vector<void *> addRefsA = gAddRefs;
        gAddRefs.clear();
        // Ours: the constructor hook's adapter (lane KD: the standalone shape), our vtables on all three faces.
        cRZCallbackSystemService *rb = cRZCallbackSystemServiceAdapters::Construct(b, nullptr);
        auto *threadB = static_cast<cRZCallbackThread *>(b->mCallbackThread);
        ++ctx.cases;
        if (ra != a || rb != b) ctx.Fail("run %d: return value is not this", run);
        // The service itself: id, priority, count, running flag (the thread pointer aside). The vtable words are the only
        // bytes that differ by design: the original's are the game's three, ours are our class's (the same on every run,
        // none of them the game's).
        const uint32_t gameVt[3] = {cRZCallbackSystemService::kVTable, cRZCallbackSystemService::kSystemServiceVTable, cRZCallbackSystemService::kUnknownVTable};
        static const void *ourVt[3] = {};
        for (int f = 0; f < 3; ++f) {
            const void *wa = reinterpret_cast<void *const *>(a)[f], *wb = reinterpret_cast<void *const *>(b)[f];
            if (!ourVt[f]) ourVt[f] = wb;
            if (wa != ctx.image.At(gameVt[f])) ctx.Fail("run %d: original's face %d vtable unexpected", run, f);
            if (wb != ourVt[f] || wb == ctx.image.At(gameVt[0]) || wb == ctx.image.At(gameVt[1]) || wb == ctx.image.At(gameVt[2])) ctx.Fail("run %d: our face %d vtable unexpected", run, f);
        }
        if (std::memcmp(reinterpret_cast<char *>(a) + 0xc, reinterpret_cast<char *>(b) + 0xc, offsetof(cRZCallbackSystemService, mCallbackThread) - 0xc) != 0) ctx.Fail("run %d: service differs", run);
        if (a->mServiceId != cRZCallbackSystemService::kServiceId || a->mPriority != cRZCallbackSystemService::kPriority) ctx.Fail("run %d: original's service id or priority unexpected", run);
        if (!threadA || !threadB) {
            ctx.Fail("run %d: no thread", run);
        } else {
            if (!SameCallbackThread(*threadA, *threadB)) ctx.Fail("run %d: thread differs", run);
            if (threadA->mOwnedByCaller != 1 || threadB->mOwnedByCaller != 1) ctx.Fail("run %d: ownership flag %u/%u", run, threadA->mOwnedByCaller, threadB->mOwnedByCaller);
        }
        // Each side called slot 1 once, on its own thread.
        if (addRefsA.size() != 1 || addRefsA[0] != threadA || gAddRefs.size() != 1 || gAddRefs[0] != threadB) ctx.Fail("run %d: AddRef calls on the thread: original %zu, ours %zu", run, addRefsA.size(), gAddRefs.size());
        if (threadA) FreeCallbackThread(threadA);
        if (threadB) FreeCallbackThread(threadB);
        // The service part holds nothing to free (its destructor only stores vtables).
        ::operator delete(a);
        ::operator delete(b);
    }
    vtable[1] = savedSlot;
    VirtualProtect(vtable, 0x24, oldProtect, &oldProtect);
}
