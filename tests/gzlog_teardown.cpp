// Differential tests for nGZLog::cService's output thread and destruction (src/rz/nGZLogLifetime.size.cpp, lane LZ):
// the thread loop (0xc9e159) and its entry (0xc9e8ab), ReturnRecord (0xc9d020), the destructor body (0xc9c843), the
// deleting destructor (0xc9ca98) and its thunk from the base at +4 (0xc9c9e7); the std adapter's destructor
// (0xc9c829), deleting destructor (slot 21, 0xc9ca79) and WriteText (slot 14, 0xc9e523); cRZString's Mid / Left /
// Right (0x1ad18d / 0x1ad13b / 0x1ad3ed, src/rz/cRZStringSub.cpp).
//
// The thread loop runs on the test's thread: the signal is a fake whose Wait answers from a script (and ends the run
// by clearing the running flag), the locks, the records and the output back ends are fakes that log their calls; an
// output can queue a record while it is being given one (the producer side), or throw. The destructor tests build the
// same service on both sides from real members (locks, containers, the thread and signal state blocks) and fakes for
// the counted references; CloseHandle (the signal's event) is a logging fake. Frees are compared as the sorted block
// sizes each side freed (difftest::FreeRecorder).
#include "gzlog_support.h" // first: it defines NOMINMAX before windows.h
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "game/vtable.h"
#include "rz/cRZStringSub.h"
#include "rz/cRZThreadSignal.h"
#include "rz/nGZLog.h"
#include "service_shape.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace gzlog_test;
using namespace nGZLog;

namespace {

constexpr uint32_t kThreadLoop = 0xc9e159, kThreadEntry = 0xc9e8ab, kReturnRecord = 0xc9d020;
constexpr uint32_t kDestruct = 0xc9c843, kDeleting = 0xc9ca98, kDeletingThunk = 0xc9c9e7;
constexpr uint32_t kAdapterDestruct = 0xc9c829, kAdapterDeleting = 0xc9ca79, kAdapterWrite = 0xc9e523;
constexpr uint32_t kMid = 0x1ad18d, kLeft = 0x1ad13b, kRight = 0x1ad3ed;
constexpr uint32_t kIatGetCurrentThreadId = 0xd9a194;
constexpr uint32_t kFrameworkGlobal = 0x10c9378;

std::vector<std::string> gLog;
const uint8_t *gBase = nullptr;

void Log(const char *fmt, ...)
{
    char buf[300];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (gLog.capacity() < 8192) gLog.reserve(8192); // no growth (and frees of the old array) while a side runs
    gLog.push_back(buf);
}

std::string Joined()
{
    std::string s;
    for (const std::string &l : gLog) s += l + "; ";
    return s;
}

int32_t Rel(const void *p)
{
    return static_cast<int32_t>(reinterpret_cast<const uint8_t *>(p) - gBase);
}

std::string Sizes(const difftest::FreeRecorder &r)
{
    size_t sizes[256];
    const size_t n = r.SortedSizes(sizes, 256);
    std::string s = r.Overflowed() ? "overflow " : "";
    for (size_t k = 0; k < n; ++k) s += std::to_string(sizes[k]) + " ";
    return s;
}

// A counted object (record, output, filter...): AddRef / Release log by id.
struct Counted {
    const void *vtable;
    int id;
};
uint32_t __fastcall CountedAddRef(Counted *self, void *) { Log("addref(%d)", self->id); return 2; }
uint32_t __fastcall CountedRelease(Counted *self, void *) { Log("release(%d)", self->id); return 1; }

uint32_t gThreadId = 0;
DWORD WINAPI FakeThreadId() { return gThreadId; }

uint32_t __fastcall LockFake(void *self, void *) { Log("lock(%+d)", Rel(self)); return 1; }
uint32_t __fastcall UnlockFake(void *self, void *) { Log("unlock(%+d)", Rel(self)); return 0; }

std::string Ids(const std::vector<void *> &list)
{
    std::string s;
    for (void *p : list) s += p ? std::to_string(static_cast<Counted *>(p)->id) + "," : "null,";
    return s;
}

} // namespace

// ---- the output thread ---------------------------------------------------------------------------------------------

namespace {

struct ThreadScript {
    std::vector<uint32_t> waits; // what Wait answers, call by call (then 1)
    size_t stopAt = 0;           // the Wait call that clears the running flag
    std::vector<uint8_t> synchronous, accept;
    int produceAt = -1;          // the output that queues one more record (once) while given one
    int throwAt = -1;            // the output whose Accept throws (once)
    size_t waitCalls = 0;
    bool produced = false;
} gThread;
cLogService *gService = nullptr;
std::deque<Counted> *gExtra = nullptr;

uint32_t __fastcall SignalWait(void *self, void *, uint32_t us)
{
    const size_t k = gThread.waitCalls++;
    const uint32_t answer = k < gThread.waits.size() ? gThread.waits[k] : 1;
    Log("wait(%+d, %x) -> %u", Rel(self), us, answer);
    if (k >= gThread.stopAt) gService->mOutputRunning = 0;
    if (gThread.waitCalls > 200) gService->mOutputRunning = 0, gThread.waits.clear();
    return answer;
}
uint32_t __fastcall SignalReset(void *self, void *) { Log("reset(%+d)", Rel(self)); return 1; }

uint32_t __fastcall OutCount(Counted *, void *) { return 1; }
uint8_t __fastcall OutSynchronous(Counted *self, void *)
{
    Log("out%d.synch?", self->id);
    return gThread.synchronous[self->id];
}
uint8_t __fastcall OutAccept(Counted *self, void *, Counted *record)
{
    Log("out%d.accept(%d)", self->id, record ? record->id : -1);
    if (gThread.throwAt == self->id) {
        gThread.throwAt = -1;
        throw 6;
    }
    return gThread.accept[self->id];
}
void __fastcall OutOutput(Counted *self, void *, Counted *record)
{
    Log("out%d.output(%d)", self->id, record ? record->id : -1);
    if (gThread.produceAt == self->id && !gThread.produced) {
        gThread.produced = true;
        gExtra->push_back(gExtra->front());
        gExtra->back().id = 900;
        gService->mQueue.push_back(&gExtra->back());
        ++gService->mQueueCount;
    }
}

void *gRecordVTable[4], *gOutVTable[24], *gLockVTable[4], *gSignalVTable[8];
struct ThreadSetup {
    ThreadSetup()
    {
        gRecordVTable[1] = reinterpret_cast<void *>(&CountedAddRef);
        gRecordVTable[2] = reinterpret_cast<void *>(&CountedRelease);
        gOutVTable[1] = reinterpret_cast<void *>(&OutCount); // the list's holders count their outputs
        gOutVTable[2] = reinterpret_cast<void *>(&OutCount);
        gOutVTable[4] = reinterpret_cast<void *>(&OutAccept);
        gOutVTable[5] = reinterpret_cast<void *>(&OutOutput);
        gOutVTable[15] = reinterpret_cast<void *>(&OutSynchronous);
        gLockVTable[1] = reinterpret_cast<void *>(&LockFake);
        gLockVTable[2] = reinterpret_cast<void *>(&UnlockFake);
        gSignalVTable[3] = reinterpret_cast<void *>(&SignalWait);
        gSignalVTable[6] = reinterpret_cast<void *>(&SignalReset);
    }
} gThreadSetup;

struct ThreadSide {
    std::vector<uint8_t> store;
    uint8_t *block;
    std::deque<Counted> records, outs, extra;
    cLogService *S() { return reinterpret_cast<cLogService *>(block); }
    ThreadSide(const std::vector<uint8_t> &bytes) : store(bytes.size() + 16)
    {
        block = store.data() + 8 - reinterpret_cast<uintptr_t>(store.data()) % 8;
        std::memcpy(block, bytes.data(), bytes.size());
        for (size_t off : {offsetof(cLogService, mLockA0), offsetof(cLogService, mLock90), offsetof(cLogService, mLockE0)})
            *reinterpret_cast<void **>(block + off) = gLockVTable;
        *reinterpret_cast<void **>(block + offsetof(cLogService, mSignal)) = gSignalVTable;
        new (&S()->mQueue) std::vector<void *>();
        new (&S()->mRecords) std::vector<void *>();
        new (&S()->mOutputs) std::vector<RefHolder>();
    }
    ~ThreadSide()
    {
        S()->mQueue.~vector();
        S()->mRecords.~vector();
        S()->mOutputs.~vector();
    }
};

} // namespace

DIFF_TEST(GZLog_ServiceOutputThread, kThreadLoop)
{
    Slots slots(ctx);
    slots.Set(kIatGetCurrentThreadId, reinterpret_cast<void *>(&FakeThreadId));
    auto loop = ctx.Original<void(__thiscall *)(void *)>(kThreadLoop);
    auto entry = ctx.Original<uint32_t(__stdcall *)(void *)>(kThreadEntry);
    std::mt19937 rng(kThreadLoop);
    int delivered = 0, produced = 0, thrown = 0, partial = 0;
    for (int i = 0; i < 1500; ++i) {
        std::vector<uint8_t> bytes(cLogService::kSize);
        for (uint8_t &x : bytes) x = static_cast<uint8_t>(rng());
        ThreadSide a(bytes), b(bytes);
        const int queued = static_cast<int>(rng() % 6), pooled = static_cast<int>(rng() % 3), outputs = static_cast<int>(rng() % 4);
        std::vector<int> queueIds;
        for (int k = 0; k < queued; ++k) queueIds.push_back(rng() % 7 == 0 ? -1 : 100 + k);
        // The count of queued records: the list's size, or (sometimes) fewer.
        const uint32_t count = queued && rng() % 5 == 0 ? static_cast<uint32_t>(rng() % queued) : static_cast<uint32_t>(queued);
        const uint8_t running = static_cast<uint8_t>(rng() % 4 ? 1 + rng() % 255 : 0);
        for (ThreadSide *s : {&a, &b}) {
            for (int k = 0; k < queued + pooled + 1; ++k) s->records.push_back({gRecordVTable, k < queued ? queueIds[k] : 200 + k});
            for (int k = 0; k < queued; ++k) s->S()->mQueue.push_back(queueIds[k] < 0 ? nullptr : &s->records[k]);
            for (int k = 0; k < pooled; ++k) s->S()->mRecords.push_back(&s->records[queued + k]);
            for (int k = 0; k < outputs; ++k) s->outs.push_back({gOutVTable, k});
            for (Counted &o : s->outs) {
                void *object = &o;
                s->S()->mOutputs.push_back(reinterpret_cast<const RefHolder &>(object));
            }
            s->extra.push_back({gRecordVTable, 900});
            s->S()->mQueueCount = count;
            s->S()->mOutputRunning = running;
        }
        ThreadScript script;
        for (int k = 0, n = static_cast<int>(rng() % 4); k < n; ++k) script.waits.push_back(rng() % 3 == 0 ? 1 : static_cast<uint32_t>(rng() % 4));
        script.stopAt = rng() % 4;
        for (int k = 0; k < outputs; ++k) {
            script.synchronous.push_back(static_cast<uint8_t>(rng() % 3 == 0 ? 1 + rng() % 255 : 0));
            script.accept.push_back(static_cast<uint8_t>(rng() % 3 ? 1 + rng() % 255 : 0));
        }
        script.produceAt = outputs && rng() % 3 == 0 ? static_cast<int>(rng() % outputs) : -1;
        script.throwAt = outputs && rng() % 5 == 0 ? static_cast<int>(rng() % outputs) : -1;
        const bool viaEntry = rng() % 2 == 0;
        gThreadId = 0x7000 + rng() % 16;

        std::string logs[2];
        uint32_t results[2] = {};
        ThreadSide *sides[2] = {&a, &b};
        for (int side = 0; side < 2; ++side) {
            ThreadSide &s = *sides[side];
            gThread = script;
            gService = s.S();
            gExtra = &s.extra;
            gBase = s.block;
            gLog.clear();
            if (side == 0)
                viaEntry ? (void)(results[0] = entry(s.block)) : loop(s.block);
            else
                viaEntry ? (void)(results[1] = cLogService::ThreadEntry(s.block)) : s.S()->OutputThread();
            logs[side] = Joined();
        }
        ++ctx.cases;
        if (logs[0] != logs[1]) {
            size_t at = 0;
            while (at < logs[0].size() && at < logs[1].size() && logs[0][at] == logs[1][at]) ++at;
            const size_t from = at > 60 ? at - 60 : 0;
            ctx.Fail("case %d: calls differ at %zu: ...%.150s / ...%.150s", i, at, logs[0].c_str() + from, logs[1].c_str() + from);
        }
        if (results[0] != results[1] || results[0] != 0) ctx.Fail("case %d: entry returned %u / %u", i, results[0], results[1]);
        if (Ids(a.S()->mQueue) != Ids(b.S()->mQueue)) ctx.Fail("case %d: queue %s / %s", i, Ids(a.S()->mQueue).c_str(), Ids(b.S()->mQueue).c_str());
        if (Ids(a.S()->mRecords) != Ids(b.S()->mRecords)) ctx.Fail("case %d: pool %s / %s", i, Ids(a.S()->mRecords).c_str(), Ids(b.S()->mRecords).c_str());
        const std::vector<std::pair<size_t, size_t>> skip = {{0x54, 0xc}, {0x60, 0xc}, {0xe8, 0xc}};
        int at = FirstDifference(a.block, b.block, cLogService::kSize, skip);
        if (at >= 0) ctx.Fail("case %d: service bytes differ at 0x%x", i, at);
        if (b.S()->mOutputThreadId != gThreadId) ctx.Fail("case %d: thread id not stored", i);
        if (logs[0].find(".output(") != std::string::npos) ++delivered;
        if (logs[0].find("(900)") != std::string::npos) ++produced;
        if (script.throwAt >= 0 && logs[0].find(".accept(") != std::string::npos) ++thrown;
        if (count < static_cast<uint32_t>(queued)) ++partial;
    }
    if (delivered < 300 || produced < 40 || thrown < 40 || partial < 40) ctx.Fail("coverage: %d delivered, %d produced, %d thrown, %d partial", delivered, produced, thrown, partial);
}

DIFF_TEST(GZLog_ServiceReturnRecord, kReturnRecord)
{
    auto original = ctx.Original<void(__thiscall *)(void *, void *)>(kReturnRecord);
    std::mt19937 rng(kReturnRecord);
    for (int i = 0; i < 1000; ++i) {
        std::vector<uint8_t> bytes(cLogService::kSize);
        for (uint8_t &x : bytes) x = static_cast<uint8_t>(rng());
        ThreadSide a(bytes), b(bytes);
        const int pooled = static_cast<int>(rng() % 9);
        const size_t reserve = rng() % 2 ? 0 : rng() % 12;
        for (ThreadSide *s : {&a, &b}) {
            for (int k = 0; k <= pooled; ++k) s->records.push_back({gRecordVTable, k});
            s->S()->mRecords.reserve(reserve);
            for (int k = 0; k < pooled; ++k) s->S()->mRecords.push_back(&s->records[k]);
        }
        const bool null = rng() % 6 == 0;
        gBase = a.block;
        gLog.clear();
        original(a.block, null ? nullptr : &a.records[pooled]);
        std::string logA = Joined();
        gBase = b.block;
        gLog.clear();
        b.S()->ReturnRecord(null ? nullptr : &b.records[pooled]);
        std::string logB = Joined();
        ++ctx.cases;
        if (logA != logB || gLog.size() != 2) ctx.Fail("calls %s / %s", logA.c_str(), logB.c_str());
        if (Ids(a.S()->mRecords) != Ids(b.S()->mRecords) || a.S()->mRecords.capacity() != b.S()->mRecords.capacity())
            ctx.Fail("pool %s (%zu) / %s (%zu)", Ids(a.S()->mRecords).c_str(), a.S()->mRecords.capacity(), Ids(b.S()->mRecords).c_str(),
                     b.S()->mRecords.capacity());
        if (FirstDifference(a.block, b.block, cLogService::kSize, {{0x54, 0xc}, {0x60, 0xc}, {0xe8, 0xc}}) >= 0) ctx.Fail("service bytes differ");
    }
}

// ---- destruction ---------------------------------------------------------------------------------------------------

namespace {

BOOL WINAPI FakeCloseHandle(HANDLE handle)
{
    Log("CloseHandle(%p)", handle);
    return TRUE;
}

void *gCountedVTable[4];
struct CountedSetup {
    CountedSetup()
    {
        gCountedVTable[1] = reinterpret_cast<void *>(&CountedAddRef);
        gCountedVTable[2] = reinterpret_cast<void *>(&CountedRelease);
    }
} gCountedSetup;

struct DestroyPlan {
    std::vector<std::string> names;
    std::vector<std::pair<std::string, std::string>> aliases;
    std::vector<std::pair<uint32_t, uint32_t>> nesting;
    int queued = 0, pooled = 0, outputs = 0, filters = 0, formatters = 0;
    bool withAliases = true, withTimestamp = true, withProcessInfo = true, defaultFormatter = true, defaultFilter = true, field84 = true;
    uint32_t event = 0;
};

// One side's service, built from real members as the constructor leaves them (plus contents from the plan).
struct DestroySide {
    uint8_t *block;
    std::deque<Counted> objects;
    cLogService *S() { return reinterpret_cast<cLogService *>(block); }
    void *NewObject(int id)
    {
        objects.push_back({gCountedVTable, id});
        return &objects.back();
    }
    DestroySide(const DestroyPlan &plan, std::mt19937 &rng, uint32_t seed)
    {
        block = static_cast<uint8_t *>(::operator new(cLogService::kSize));
        std::mt19937 fill(seed);
        for (size_t k = 0; k < cLogService::kSize; ++k) block[k] = static_cast<uint8_t>(fill());
        (void)rng;
        cLogService *s = S();
        // The rz::cRZSystemService part as its constructor leaves it (ours, on both sides: each destructor writes its own
        // vtables over it).
        ::new (static_cast<rz::cRZSystemService *>(s)) rz::cRZSystemService(cLogService::kServiceId, cLogService::kPriority);
        s->mLock28.Construct();
        new (&s->mNames) NameSet();
        for (const std::string &n : plan.names) s->mNames.insert(LogString(n.c_str()));
        new (&s->mQueue) std::vector<void *>();
        for (int k = 0; k < plan.queued; ++k) s->mQueue.push_back(NewObject(100 + k));
        for (std::vector<RefHolder> *list : {&s->mOutputs, &s->mFilters, &s->mFormatters}) new (list) std::vector<RefHolder>();
        auto fill3 = [&](std::vector<RefHolder> &list, int n, int base) {
            for (int k = 0; k < n; ++k) {
                void *o = NewObject(base + k);
                list.push_back(reinterpret_cast<const RefHolder &>(o));
            }
        };
        fill3(s->mOutputs, plan.outputs, 300);
        fill3(s->mFilters, plan.filters, 400);
        fill3(s->mFormatters, plan.formatters, 500);
        gLog.clear(); // the AddRefs of filling the lists
        s->mField84 = plan.field84 ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(NewObject(84))) : 0;
        s->mFormatter.mObject = plan.defaultFormatter ? NewObject(88) : nullptr;
        s->mFilter.mObject = plan.defaultFilter ? NewObject(89) : nullptr;
        s->mLock90.Construct();
        s->mLockA0.Construct();
        s->mSignal.mVTable = GameAddress(cRZThreadSignal::kVTable);
        s->mSignal.mState = static_cast<cRZThreadSignalState *>(::operator new(cRZThreadSignalState::kSize))->Construct();
        s->mSignal.mState->mEvent = reinterpret_cast<void *>(static_cast<uintptr_t>(plan.event));
        s->mThread.Construct();
        new (&s->mNesting) std::map<uint32_t, uint32_t>();
        for (auto &e : plan.nesting) s->mNesting[e.first] = e.second;
        s->mLockCC.Construct();
        s->mTimestamp = plan.withTimestamp ? static_cast<cLogTimestamp *>(::operator new(cLogTimestamp::kSize)) : nullptr;
        s->mProcessInfo = plan.withProcessInfo ? static_cast<cLogProcessInfo *>(::operator new(cLogProcessInfo::kSize)) : nullptr;
        s->mLockE0.Construct();
        new (&s->mRecords) std::vector<void *>();
        for (int k = 0; k < plan.pooled; ++k) s->mRecords.push_back(NewObject(200 + k));
        s->mAliases = nullptr;
        if (plan.withAliases) {
            s->mAliases = new AliasMap();
            for (auto &e : plan.aliases) AddAliasText(s->mAliases, nullptr, e.first.c_str(), e.second.c_str());
        }
    }
};

DestroyPlan MakePlan(std::mt19937 &rng)
{
    DestroyPlan p;
    for (int k = 0, n = static_cast<int>(rng() % 12); k < n; ++k) p.names.push_back(std::string(rng() % 20, 'n') + std::to_string(k));
    for (int k = 0, n = static_cast<int>(rng() % 6); k < n; ++k) p.aliases.push_back({"key" + std::to_string(k), std::string(rng() % 40, 'v')});
    for (int k = 0, n = static_cast<int>(rng() % 5); k < n; ++k) p.nesting.push_back({static_cast<uint32_t>(rng()), static_cast<uint32_t>(rng() % 4)});
    p.queued = static_cast<int>(rng() % 4);
    p.pooled = static_cast<int>(rng() % 5);
    p.outputs = static_cast<int>(rng() % 3);
    p.filters = static_cast<int>(rng() % 3);
    p.formatters = static_cast<int>(rng() % 3);
    p.withAliases = rng() % 4 != 0;
    p.withTimestamp = rng() % 4 != 0;
    p.withProcessInfo = rng() % 6 != 0;
    p.defaultFormatter = rng() % 3 != 0;
    p.defaultFilter = rng() % 3 != 0;
    p.field84 = rng() % 3 == 0;
    p.event = rng() % 4 == 0 ? 0 : 0x4a000000u + (rng() % 64) * 4;
    return p;
}

} // namespace

DIFF_TEST(GZLog_ServiceDestruct, kDestruct)
{
    Slots slots(ctx);
    slots.Set(cRZThreadSignal::kIatCloseHandle, reinterpret_cast<void *>(&FakeCloseHandle));
    auto destruct = ctx.Original<void(__thiscall *)(void *)>(kDestruct);
    auto deleting = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeleting);
    auto thunk = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kDeletingThunk);
    std::mt19937 rng(kDestruct);
    int freedBlocks = 0;
    svc::CodeMap baseCodes[2];
    svc::AddServiceBaseCodes(baseCodes[0], baseCodes[1], 1);
    uint32_t ourFaceVTable = 0;
    for (int i = 0; i < 600; ++i) {
        const DestroyPlan plan = MakePlan(rng);
        const uint32_t seed = rng();
        const int route = static_cast<int>(rng() % 3); // the body, the deleting destructor, the thunk
        const uint32_t flags = route == 0 ? 0 : (rng() % 3 == 0 ? static_cast<uint32_t>(rng()) & ~1u : 1u | static_cast<uint32_t>(rng() % 4) << 1);
        std::string logs[2], frees[2], words[2];
        bool blockFreed[2] = {};
        void *results[2] = {};
        for (int side = 0; side < 2; ++side) {
            DestroySide s(plan, rng, seed);
            gBase = s.block;
            gLog.clear();
            difftest::FreeRecorder recorder;
            if (side == 0) {
                if (route == 0) destruct(s.block);
                else if (route == 1) results[0] = deleting(s.block, flags);
                else results[0] = thunk(s.block + 4, flags);
            } else {
                if (route == 0) cLogServiceAdapters::Destruct(s.S(), nullptr);
                else if (route == 1) results[1] = cLogServiceAdapters::DeletingDestructor(s.S(), nullptr, flags);
                else results[1] = thunks::ServiceDeletingFromUnknown(s.block + 4, nullptr, flags);
            }
            recorder.Stop();
            logs[side] = Joined();
            frees[side] = Sizes(recorder);
            blockFreed[side] = recorder.Freed(s.block);
            if (results[side]) results[side] = reinterpret_cast<void *>(static_cast<uint8_t *>(results[side]) - s.block);
            if (!blockFreed[side]) {
                const uint32_t *w = reinterpret_cast<const uint32_t *>(s.block);
                char buf[96];
                // The vtables the destroyed bases leave, as codes (the game's on side 0, ours on side 1).
                const svc::CodeMap &map = side == 0 ? baseCodes[0] : baseCodes[1];
                // The cIService face keeps its class's vtable (the game's 0xeb4674; ours, the same word in every case and
                // outside the image).
                uint32_t face = w[7];
                if (side == 0 && face == svc::P(ctx.image.At(cLogService::kServiceVTable))) face = 0xc0de0010;
                if (side == 1) {
                    if (!ourFaceVTable) ourFaceVTable = face;
                    if (face == ourFaceVTable && (face < ctx.image.Base() || face >= ctx.image.Base() + ctx.image.Size())) face = 0xc0de0010;
                }
                std::snprintf(buf, sizeof(buf), "%x %x %x formatter %x", map.Code(w[0]), map.Code(w[1]), face, w[0x88 / 4]);
                words[side] = buf;
                ::operator delete(s.block);
            }
        }
        ++ctx.cases;
        if (logs[0] != logs[1]) ctx.Fail("case %d route %d: calls %.150s / %.150s", i, route, logs[0].c_str(), logs[1].c_str());
        if (frees[0] != frees[1]) ctx.Fail("case %d route %d: frees %.120s / %.120s", i, route, frees[0].c_str(), frees[1].c_str());
        if (blockFreed[0] != blockFreed[1] || blockFreed[0] != (route != 0 && (flags & 1) != 0)) ctx.Fail("case %d route %d flags %x: block freed %d / %d", i, route, flags, blockFreed[0], blockFreed[1]);
        if (words[0] != words[1]) ctx.Fail("case %d route %d: words %s / %s", i, route, words[0].c_str(), words[1].c_str());
        if (results[0] != results[1]) ctx.Fail("case %d route %d: result differs", i, route);
        if (blockFreed[0]) ++freedBlocks;
    }
    if (freedBlocks < 100) ctx.Fail("only %d blocks freed", freedBlocks);
}

// ---- the std adapter -----------------------------------------------------------------------------------------------

namespace {

int32_t gServiceAnswer = 0; // 0: no service; else the fake log service is handed out
struct FakeLogService {
    const void *vtable;
} gLogService;
uint32_t __fastcall FrameworkQuery(void *, void *, uint32_t serviceId, uint32_t iid, void **out)
{
    Log("framework.query(%x, %x)", serviceId, iid);
    *out = gServiceAnswer ? &gLogService : nullptr;
    return gServiceAnswer ? 1 : 0;
}
uint32_t __fastcall ServiceRelease(void *, void *) { Log("service.release"); return 0; }
void __fastcall ServiceWriteFake(void *, void *, const char *file, uint32_t line, const char *group, int32_t level, const char *format, va_list args)
{
    Log("service.write(%s, %u, '%s', %d, '%s', %p)", file ? file : "(null)", line, group, level, format, static_cast<void *>(args));
}
void *gFrameworkVTable[8], *gLogServiceVTable[32];
struct FrameworkFake {
    const void *vtable = gFrameworkVTable;
} gFramework;
struct AdapterSetup {
    AdapterSetup()
    {
        gFrameworkVTable[5] = reinterpret_cast<void *>(&FrameworkQuery);
        gLogServiceVTable[2] = reinterpret_cast<void *>(&ServiceRelease);
        gLogServiceVTable[28] = reinterpret_cast<void *>(&ServiceWriteFake);
        gLogService.vtable = gLogServiceVTable;
    }
} gAdapterSetup;

class PointerGlobal {
public:
    PointerGlobal(difftest::Context &c, uint32_t rva, const void *value) : slot(c.image.At<uint32_t>(rva)), saved(*slot)
    {
        *slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value));
    }
    ~PointerGlobal() { *slot = saved; }

private:
    uint32_t *slot;
    uint32_t saved;
};

} // namespace

DIFF_TEST(GZLog_StdAdapterDestruct, kAdapterDestruct)
{
    auto destruct = ctx.Original<void(__thiscall *)(void *)>(kAdapterDestruct);
    auto deleting = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kAdapterDeleting);
    std::mt19937 rng(kAdapterDestruct);
    for (int i = 0; i < 1000; ++i) {
        const std::string name(rng() % 3 == 0 ? 16 + rng() % 30 : rng() % 16, static_cast<char>('a' + rng() % 26));
        const uint32_t target = rng();
        const bool viaDeleting = rng() % 2 == 0;
        const uint32_t flags = rng() % 3 == 0 ? 0 : 1;
        std::vector<uint8_t> after[2];
        std::string frees[2];
        bool freed[2] = {};
        for (int side = 0; side < 2; ++side) {
            auto *a = static_cast<cLogStdAdapter *>(::operator new(cLogStdAdapter::kSize));
            std::memset(a, 0x5c, cLogStdAdapter::kSize); // the same padding on both sides
            a->Construct(target, name.c_str());
            difftest::FreeRecorder recorder;
            if (side == 0) {
                if (viaDeleting) deleting(a, flags);
                else destruct(a);
            } else {
                if (viaDeleting) a->DeletingDestructor(flags);
                else a->Destruct();
            }
            recorder.Stop();
            frees[side] = Sizes(recorder);
            freed[side] = recorder.Freed(a);
            if (!freed[side]) {
                after[side].assign(reinterpret_cast<uint8_t *>(a), reinterpret_cast<uint8_t *>(a) + cLogStdAdapter::kSize);
                ::operator delete(a);
            }
        }
        ++ctx.cases;
        if (frees[0] != frees[1]) ctx.Fail("'%s': frees %s / %s", name.c_str(), frees[0].c_str(), frees[1].c_str());
        if (freed[0] != freed[1] || freed[0] != (viaDeleting && flags)) ctx.Fail("'%s': block freed %d / %d", name.c_str(), freed[0], freed[1]);
        // A long name's buffer pointer stays in the string's bytes after the tidy (+0x18, beyond the cleared first byte).
        const int differs = after[0].size() ? FirstDifference(after[0].data(), after[1].data(), cLogStdAdapter::kSize, name.size() > 15 ? std::vector<std::pair<size_t, size_t>>{{0x19, 3}} : std::vector<std::pair<size_t, size_t>>{}) : -1;
        if (differs >= 0) ctx.Fail("'%s' via %d: bytes after differ at 0x%x: %08x / %08x", name.c_str(), viaDeleting, differs, *reinterpret_cast<uint32_t *>(&after[0][differs & ~3]), *reinterpret_cast<uint32_t *>(&after[1][differs & ~3]));
    }
}

DIFF_TEST(GZLog_StdAdapterWriteText, kAdapterWrite)
{
    PointerGlobal framework(ctx, kFrameworkGlobal, &gFramework);
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, const char *)>(kAdapterWrite);
    std::mt19937 rng(kAdapterWrite);
    for (int i = 0; i < 400; ++i) {
        const std::string name(rng() % 30, static_cast<char>('a' + rng() % 26));
        const uint32_t target = rng() % 3 == 0 ? 0 : rng();
        gServiceAnswer = rng() % 4 != 0;
        const std::string text = "line " + std::to_string(i);
        alignas(4) uint8_t sa[cLogStdAdapter::kSize], sb[cLogStdAdapter::kSize];
        auto *a = reinterpret_cast<cLogStdAdapter *>(sa)->Construct(target, name.c_str());
        auto *b = reinterpret_cast<cLogStdAdapter *>(sb)->Construct(target, name.c_str());
        gLog.clear();
        uint8_t ra = original(a, text.c_str());
        std::string logA = Joined();
        gLog.clear();
        uint8_t rb = b->WriteText(text.c_str());
        std::string logB = Joined();
        ++ctx.cases;
        if (ra != rb || ra != (gServiceAnswer ? 1 : 0)) ctx.Fail("result %u / %u", ra, rb);
        if (logA != logB) ctx.Fail("calls %s / %s", logA.c_str(), logB.c_str());
        a->Destruct();
        b->Destruct();
    }
}

// ---- cRZString's substrings ------------------------------------------------------------------------------------------

DIFF_TEST(cRZString_Substrings, kMid)
{
    auto mid = ctx.Original<cRZString *(__thiscall *)(const void *, cRZString *, uint32_t, uint32_t)>(kMid);
    auto left = ctx.Original<cRZString *(__thiscall *)(const void *, cRZString *, uint32_t)>(kLeft);
    auto right = ctx.Original<cRZString *(__thiscall *)(const void *, cRZString *, uint32_t)>(kRight);
    std::mt19937 rng(kMid);
    for (int i = 0; i < 20000; ++i) {
        const size_t length = rng() % 4 == 0 ? rng() % 60 : rng() % 18;
        std::string text;
        for (size_t k = 0; k < length; ++k) text += static_cast<char>('a' + rng() % 26);
        cRZString source;
        source.Construct(text.c_str());
        auto pick = [&]() -> uint32_t {
            switch (rng() % 5) {
            case 0: return static_cast<uint32_t>(length);
            case 1: return static_cast<uint32_t>(length + 1);
            case 2: return static_cast<uint32_t>(rng());
            case 3: return 0xffffffffu - rng() % 3;
            default: return static_cast<uint32_t>(rng() % (length + 2));
            }
        };
        const uint32_t x = pick(), y = pick();
        const int which = i % 3;
        alignas(4) uint8_t sa[sizeof(cRZString)], sb[sizeof(cRZString)];
        std::memset(sa, 0xcd, sizeof(sa));
        std::memset(sb, 0xcd, sizeof(sb));
        auto *oa = reinterpret_cast<cRZString *>(sa);
        auto *ob = reinterpret_cast<cRZString *>(sb);
        cRZString *ra, *rb;
        if (which == 0) {
            ra = mid(&source, oa, x, y);
            rb = Sub(source).Mid(ob, x, y);
        } else if (which == 1) {
            ra = left(&source, oa, x);
            rb = Sub(source).Left(ob, x);
        } else {
            ra = right(&source, oa, x);
            rb = Sub(source).Right(ob, x);
        }
        ++ctx.cases;
        if (ra != oa || rb != ob) ctx.Fail("result is not the out argument");
        if (oa->mString != ob->mString || oa->mString.capacity() != ob->mString.capacity() || oa->mVTable != ob->mVTable || oa->mRefCount != ob->mRefCount)
            ctx.Fail("op %d on '%s' (%u, %u): '%s' / '%s'", which, text.c_str(), x, y, oa->mString.c_str(), ob->mString.c_str());
        if (source.mString != text) ctx.Fail("source changed");
        oa->Destruct();
        ob->Destruct();
        source.Destruct();
    }
}
