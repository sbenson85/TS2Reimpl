// Differential tests for lane HT's owned XC entries (src/app/StaticInitHT.cpp; docs/subsystems/startup.md "Done in lane
// HT") and the members they brought in.
//
// StaticInitHT_Entries: per entry (all but 766, below), the slot holds the image's initialiser our table names; the
//   image's entry against ours from junk (each entry's window with 8-byte junk margins): the window after the entry
//   (heap blocks described: sizes, links, contents), the registrations (exactly one, the image's its thunk and ours not,
//   or none for slot 8). Then a live object is put in place the same way on both sides (elements through our STL on the
//   built object, fake referenced objects whose slots are logged, fake module and process handles), the registered
//   thunk runs under a free recorder, and the frees (sizes, in order; arrays also by element), the calls into fakes and
//   the imports (FreeLibrary, CloseHandle, SleepEx bound to logging fakes), and the window after are compared. Rounds:
//   six per entry with different sizes and nulls; the constructors that are verified on their own (GeneralAllocator
//   0x1c655d / Shutdown 0x1c7b90, cRZCallbackThread 0x9cfa5d / 0x9cfac2) are stood in on both sides (logging object
//   and arguments) so the entry's own work is what is compared.
//   The entries (slot: image initialiser; each checked against the XC table): 3: 0x138323, 8: 0x138385, 11: 0x13e046,
//   12: 0x14094a, 99: 0x138317, 100: 0x13830b, 101: 0x138363, 135: 0x138880, 136: 0x1388b0, 137: 0x1388c0,
//   166: 0x139260, 218: 0x13a970, 220: 0x13aa10, 223: 0x13aa30, 224: 0x13aad0, 233: 0x13b4a0, 502: 0x13e0d0,
//   523: 0x13e386, 525: 0x13e3c6, 527: 0x13e405, 529: 0x13e424, 536: 0x13e475, 564: 0x13e870, 640: 0x13f626,
//   766: 0x1408d0.
// StaticInitHT_MadCow: slot 766 (comdir::InitMadCow, verified by COMDirectors_InitMadCow): ours registers one handler
//   through BoundAtExit (not the image's stub), leaves gRegisterExitHandler as it was, and its handler frees the same
//   blocks as the image's stub.
// StaticInitHT_OnImageTable: the runner over a copy of the image's XC table with only these 25 slots, from the file's
//   state: 25 ours, 0 image / mismatched / unused; the same calls, windows and registrations; the lists run LIFO free
//   the same blocks and make the same calls.
// Members: StaticInitHT_DbgHelpUnload (0x1bd611), StaticInitHT_WorkerShutdown (0x99936e, a scripted clock; deadlines
//   -1, 0..3 and clocks near 2^32 seconds), StaticInitHT_ActionLogFlush (0x526c30), StaticInitHT_CallbackThreadDestruct
//   (0x9cfac2) and StaticInitHT_CallbackThreadDeletingDestruct (0x9cfb3e).
#include "difftest.h"
#include "code_patch.h"
#include "free_recorder.h"
#include "app/ReporterDump.h"
#include "app/StaticInit.h"
#include "app/StaticInitHT.h"
#include "app/WinMainStart.h"
#include "game/game.h"
#include "rz/GeneralAllocator.h"
#include "rz/StaticGetters.h"
#include "rz/cRZCriticalSection.h"
#include "rz/cRZThread.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

using staticinit::HTKind;
using staticinit::HTStatic;

constexpr uint32_t kXcFirst = 0xd9a724, kXcLast = 0xd9b3a8;
constexpr uint32_t kAtExit = 0x824cbe;
constexpr uint32_t kIatFreeLibrary = 0xd9a120, kIatCloseHandle = 0xd9a17c, kIatSleepEx = 0xd9a188;
constexpr uint32_t kIatGetSystemTimeAsFileTime = 0xd9a2a4;
constexpr uint32_t kGeneralAllocatorCtor = 0x1c655d, kGeneralAllocatorShutdown = 0x1c7b90;
constexpr uint32_t kCallbackThreadCtor = 0x9cfa5d, kCallbackThreadDtor = 0x9cfac2, kCallbackThreadDeleting = 0x9cfb3e;
constexpr uint32_t kUnload = 0x1bd611, kShutdown = 0x99936e, kFlush = 0x526c30;
constexpr uint32_t kRZStdGlobal = 0xff246c;
constexpr int kMargin = 8;

using Thunk = void(__cdecl *)();
std::vector<Thunk> gRegistered;
int __cdecl RecordAtExit(Thunk f)
{
    gRegistered.push_back(f);
    return 0;
}

// The calls into fakes and logged imports: (tag, a, b) triples. Reserved, so that logging never frees.
std::vector<uint32_t> gLog;
uintptr_t gBase = 0;
bool gUnexpected = false;
void Log(uint32_t tag, uint32_t a, uint32_t b)
{
    gLog.push_back(tag);
    gLog.push_back(a);
    gLog.push_back(b);
}
uint32_t Rva(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) - gBase); }
uint32_t TextHash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
    return h;
}

// ---- Fakes ----

// A fake object: its vtable (one of the role tables below), an id (logged) and what its slots return.
struct Fake {
    const void *const *vtable;
    uint32_t id;
    uint32_t ret;      // the return of most slots (the locale's _Decref: a pointer)
    int32_t countdown; // workers: IsRunning returns 1 this many more times
};
Fake gFakes[48];

void __fastcall Unexpected(Fake *, void *) { gUnexpected = true; }
template <int Slot> uint32_t __fastcall F0(Fake *self, void *)
{
    Log(100 + Slot, self->id, 0);
    return self->ret;
}
template <int Slot> uint32_t __fastcall F1(Fake *self, void *, uint32_t a)
{
    Log(100 + Slot, self->id, a);
    return self->ret;
}
uint32_t __fastcall FWrite(Fake *self, void *, const char *text)
{
    Log(114, self->id, TextHash(text));
    return 0;
}
uint32_t __fastcall FRunning(Fake *self, void *)
{
    Log(111, self->id, static_cast<uint32_t>(self->countdown));
    if (self->countdown <= 0) return 0;
    --self->countdown;
    return 1;
}

struct VTable {
    const void *slots[40];
    VTable()
    {
        for (auto &s : slots) s = reinterpret_cast<const void *>(&Unexpected);
    }
};
VTable gLocaleVt, gOutVt, gRefVt, gWorkerVt;
template <class F> void SetSlot(VTable &vt, int slot, F f) { vt.slots[slot] = reinterpret_cast<const void *>(f); }
struct FakeInit {
    FakeInit()
    {
        SetSlot(gLocaleVt, 0, &F1<0>); // the deleting destructor (flags)
        SetSlot(gLocaleVt, 2, &F0<2>); // _Decref: returns the facet to delete, or null
        SetSlot(gOutVt, 3, &F0<3>);
        SetSlot(gOutVt, 14, &FWrite);
        SetSlot(gOutVt, 21, &F1<21>);
        SetSlot(gOutVt, 23, &F0<23>);
        SetSlot(gRefVt, 2, &F0<2>); // Release
        SetSlot(gWorkerVt, 5, &F0<5>);   // Resume
        SetSlot(gWorkerVt, 11, &FRunning);
        SetSlot(gWorkerVt, 13, &F1<13>); // the deleting destructor
        gLog.reserve(1 << 16);
        gRegistered.reserve(256);
    }
} gFakeInit;

Fake *MakeFake(int i, const VTable &vt, uint32_t ret = 0, int32_t countdown = 0)
{
    gFakes[i] = {vt.slots, static_cast<uint32_t>(i), ret, countdown};
    return &gFakes[i];
}

BOOL WINAPI FakeFreeLibrary(HMODULE module)
{
    Log(1, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(module)), 0);
    return TRUE;
}
BOOL WINAPI FakeCloseHandle(HANDLE handle)
{
    Log(2, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(handle)), 0);
    return TRUE;
}
BOOL WINAPI FakeSymCleanup(HANDLE process)
{
    Log(3, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(process)), 0);
    return TRUE;
}

// The scripted clock: seconds since 1970; each fourth sleep advances it a second (a sleep is a quarter second).
int64_t gSeconds = 0;
uint32_t gSleeps = 0;
void WINAPI FakeGetSystemTimeAsFileTime(FILETIME *out)
{
    const uint64_t ticks = static_cast<uint64_t>(gSeconds) * 10000000ull + 116444736000000000ull;
    out->dwLowDateTime = static_cast<DWORD>(ticks);
    out->dwHighDateTime = static_cast<DWORD>(ticks >> 32);
}
DWORD WINAPI FakeSleepEx(DWORD ms, BOOL alertable)
{
    Log(4, ms, static_cast<uint32_t>(alertable));
    if (++gSleeps % 4 == 0) ++gSeconds;
    return 0;
}

// ---- Patches ----

struct Redirect {
    uint8_t *site;
    uint8_t saved[5];
    Redirect(const difftest::Context &ctx, uint32_t rva, const void *target) : site(ctx.image.At<uint8_t>(rva))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    ~Redirect()
    {
        std::memcpy(site, saved, 5);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
};

struct BindImport {
    void **entry;
    void *saved;
    BindImport(const difftest::Context &ctx, uint32_t rva, const void *target) : entry(ctx.image.At<void *>(rva)), saved(*entry)
    {
        DWORD old;
        VirtualProtect(entry, 4, PAGE_READWRITE, &old);
        *entry = const_cast<void *>(target);
    }
    ~BindImport() { *entry = saved; }
};

// Stand-ins for the verified constructors and destructors the entries call (logged: tag, object, arguments).
void *__fastcall StandAllocCtor(void *self, void *, void *core, uint32_t size, uint32_t canFree, uint32_t canTrim, void *f, void *c)
{
    Log(20, Rva(self), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(core)));
    Log(21, size, canFree);
    Log(22, canTrim, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(f)) ^ static_cast<uint32_t>(reinterpret_cast<uintptr_t>(c)));
    return self;
}
bool __fastcall StandAllocShutdown(void *self, void *)
{
    Log(23, Rva(self), 0);
    return true;
}
void *__fastcall StandThreadCtor(void *self, void *)
{
    Log(24, Rva(self), 0);
    return self;
}
void __fastcall StandThreadDtor(void *self, void *) { Log(25, Rva(self), 0); }

// The worker threads' lock (0x10f4ff8), built for the test (the file holds zero there), and the clock and sleep.
struct WorkerFixture {
    difftest::Context &ctx;
    uint8_t savedLock[8];
    BindImport sleep, clock;
    void *savedOwnTime; // ours reads the clock through our CRT's _time64, which calls our own import
    explicit WorkerFixture(difftest::Context &c)
        : ctx(c), sleep(c, kIatSleepEx, reinterpret_cast<const void *>(&FakeSleepEx)),
          clock(c, kIatGetSystemTimeAsFileTime, reinterpret_cast<const void *>(&FakeGetSystemTimeAsFileTime))
    {
        savedOwnTime = difftest::SwapOwnImport("GetSystemTimeAsFileTime", reinterpret_cast<void *>(&FakeGetSystemTimeAsFileTime));
        std::memcpy(savedLock, c.image.At<void>(staticinit::WorkerThreads::kLock), 8);
        c.image.At<cRZCriticalSection>(staticinit::WorkerThreads::kLock)->Construct();
    }
    ~WorkerFixture()
    {
        difftest::SwapOwnImport("GetSystemTimeAsFileTime", savedOwnTime);
        ctx.image.At<cRZCriticalSection>(staticinit::WorkerThreads::kLock)->Destruct();
        std::memcpy(ctx.image.At<void>(staticinit::WorkerThreads::kLock), savedLock, 8);
    }
};

// Everything the entries' tests need: both sides' atexit captured, the imports bound to logging fakes, the verified
// constructors stood in.
struct Fixture {
    Redirect atExit;
    staticinit::RegisterAtExit savedAtExit;
    BindImport freeLibrary, closeHandle;
    shape::StandIn allocCtor, allocShutdown, threadCtor, threadDtor;
    WorkerFixture workers;
    explicit Fixture(difftest::Context &ctx)
        : atExit(ctx, kAtExit, reinterpret_cast<const void *>(&RecordAtExit)), savedAtExit(staticinit::BoundAtExit()),
          freeLibrary(ctx, kIatFreeLibrary, reinterpret_cast<const void *>(&FakeFreeLibrary)),
          closeHandle(ctx, kIatCloseHandle, reinterpret_cast<const void *>(&FakeCloseHandle)),
          allocCtor(kGeneralAllocatorCtor, &GeneralAllocator::Construct, &StandAllocCtor),
          allocShutdown(kGeneralAllocatorShutdown, &GeneralAllocator::Shutdown, &StandAllocShutdown),
          threadCtor(kCallbackThreadCtor, &cRZCallbackThread::Construct, &StandThreadCtor),
          threadDtor(kCallbackThreadDtor, &cRZCallbackThread::Destruct, &StandThreadDtor), workers(ctx)
    {
        gBase = ctx.image.Base();
        staticinit::BindAtExit(&RecordAtExit);
        gRegistered.clear();
    }
    ~Fixture() { staticinit::BindAtExit(savedAtExit); }
};

bool SlotHolds(difftest::Context &ctx, uint32_t slot, uint32_t rva)
{
    uint32_t held = *ctx.image.At<uint32_t>(kXcFirst + 4 * slot) - static_cast<uint32_t>(ctx.image.Base());
    if (held == rva) return true;
    ctx.Fail("slot %u holds %08x, our table says %08x", slot, held, rva);
    return false;
}

// ---- Windows ----

struct Area {
    uint32_t first, size;
};
Area AreaOf(const HTStatic &e)
{
    if (e.kind == HTKind::WorkerShutdown) // the threads, their flags, the shutdown flag and the busy bytes (the list of slot 527 follows)
        return {staticinit::WorkerThreads::kThreads - kMargin, staticinit::WorkerThreads::kBusy + 4 - staticinit::WorkerThreads::kThreads + kMargin};
    return {e.storage - kMargin, static_cast<uint32_t>(staticinit::HTObjectSize(e)) + 2 * kMargin};
}
std::vector<uint8_t> Take(const difftest::Context &ctx, const Area &a)
{
    const uint8_t *p = ctx.image.At<uint8_t>(a.first);
    return std::vector<uint8_t>(p, p + a.size);
}
void Put(const difftest::Context &ctx, const Area &a, const std::vector<uint8_t> &w) { std::memcpy(ctx.image.At<uint8_t>(a.first), w.data(), a.size); }

uint32_t Word(const std::vector<uint8_t> &w, size_t off)
{
    uint32_t v;
    std::memcpy(&v, w.data() + off, 4);
    return v;
}
size_t BlockSize(uint32_t p) { return HeapSize(GetProcessHeap(), 0, reinterpret_cast<void *>(static_cast<uintptr_t>(p))); }

// A window with its heap pointers replaced by what they point at.
struct Shot {
    std::vector<uint8_t> bytes;
    std::vector<uint32_t> heap;
    bool operator==(const Shot &o) const { return bytes == o.bytes && heap == o.heap; }
    bool operator!=(const Shot &o) const { return !(*this == o); }
};
void Mask(Shot &s, size_t objectOffset, size_t n = 4) { std::memset(s.bytes.data() + kMargin + objectOffset, 0, n); }

// MSVC's hash: {max load, list {head, size}, buckets {first, last, end}, mask, max index}; nodes' links at +0, +4.
void DescribeHash(Shot &s, const std::vector<uint8_t> &w, size_t off)
{
    const uint32_t head = Word(w, kMargin + off + 4);
    Mask(s, off + 4);
    const uint32_t *n = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(head));
    s.heap.push_back(static_cast<uint32_t>(BlockSize(head)));
    s.heap.push_back(n[0] == head);
    s.heap.push_back(n[1] == head);
    const uint32_t first = Word(w, kMargin + off + 0xc), last = Word(w, kMargin + off + 0x10), end = Word(w, kMargin + off + 0x14);
    for (size_t k = 0xc; k <= 0x14; k += 4) Mask(s, off + k);
    s.heap.push_back(last - first);
    s.heap.push_back(end - first);
    s.heap.push_back(static_cast<uint32_t>(BlockSize(first)));
    for (uint32_t p = first; p != last; p += 4) s.heap.push_back(*reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(p)) == head);
}
// A list: {head, size}; the head's links point at itself.
void DescribeList(Shot &s, const std::vector<uint8_t> &w, size_t off)
{
    const uint32_t head = Word(w, kMargin + off);
    Mask(s, off);
    const uint32_t *n = reinterpret_cast<const uint32_t *>(static_cast<uintptr_t>(head));
    s.heap.push_back(static_cast<uint32_t>(BlockSize(head)));
    s.heap.push_back(n[0] == head);
    s.heap.push_back(n[1] == head);
}
// A vector: {first, last, end}; its contents compared bytewise.
void DescribeVector(Shot &s, const std::vector<uint8_t> &w, size_t off)
{
    const uint32_t first = Word(w, kMargin + off), last = Word(w, kMargin + off + 4), end = Word(w, kMargin + off + 8);
    for (size_t k = 0; k < 12; k += 4) Mask(s, off + k);
    s.heap.push_back(first != 0);
    s.heap.push_back(last - first);
    s.heap.push_back(end - first);
    if (!first) return;
    s.heap.push_back(static_cast<uint32_t>(BlockSize(first)));
    const uint8_t *p = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(first));
    for (uint32_t i = 0; i < last - first; ++i) s.heap.push_back(p[i]);
}

// A destroyed std::string (inside a cRZString at `off`, the string at +4) keeps bytes 1-3 of its old pointer: the
// teardown zeroes only the first character (_Tidy_deallocate), and the pointers differ between the sides.
void MaskString(Shot &s, size_t rzStringOffset) { Mask(s, rzStringOffset + 5, 3); }

Shot Built(const difftest::Context &ctx, const HTStatic &e)
{
    Shot s;
    const std::vector<uint8_t> w = Take(ctx, AreaOf(e));
    s.bytes = w;
    switch (e.kind) {
    case HTKind::RecordMap: DescribeHash(s, w, 0); break;
    case HTKind::ActionLog: DescribeHash(s, w, 0x10); break;
    case HTKind::NamedList: DescribeList(s, w, 0); break;
    case HTKind::MotiveCurveSets:
        for (uint32_t i = 0; i < e.count; ++i) DescribeVector(s, w, 0x14 * i + 4);
        break;
    default: break;
    }
    return s;
}

Shot Destroyed(const difftest::Context &ctx, const HTStatic &e)
{
    Shot s;
    s.bytes = Take(ctx, AreaOf(e));
    // Dangling words (a freed head the destructor leaves in place) differ between the sides.
    switch (e.kind) {
    case HTKind::RecordMap: Mask(s, 4); break;
    case HTKind::NamedList: Mask(s, 0); break;
    case HTKind::ActionLog:
        Mask(s, 0x14);
        MaskString(s, 0x3c);
        break;
    case HTKind::RecordArray:
        for (uint32_t i = 0; i < e.count; ++i) {
            MaskString(s, 0x7c * i);
            MaskString(s, 0x7c * i + 0x20);
        }
        break;
    default: break;
    }
    return s;
}

// ---- Live objects: the same on both sides, through our STL on the built object ----

template <typename T> T *At(const difftest::Context &ctx, uint32_t rva) { return ctx.image.At<T>(rva); }
std::string *Text(void *rzString) { return reinterpret_cast<std::string *>(static_cast<uint8_t *>(rzString) + 4); }
const void *Block(const std::string &s) { return s.capacity() > 15 ? s.data() : nullptr; }

// n cRZStrings of `stride` bytes (the string at +0, the rest junk) in a new buffer of `capacity` elements, every third
// one long; returns the buffer and adds the long ones' blocks to `blocks`.
uint8_t *StringArray(uint32_t n, uint32_t capacity, uint32_t stride, std::vector<const void *> &blocks)
{
    // Through the STL's allocator: blocks of 4 KB and more carry its alignment header, which the teardown checks.
    uint8_t *buffer = std::allocator<uint8_t>().allocate(capacity * stride);
    std::memset(buffer, 0x5a, capacity * stride);
    for (uint32_t i = 0; i < n; ++i) {
        auto *s = reinterpret_cast<cRZString *>(buffer + i * stride);
        s->Construct();
        if (i % 3 == 0) Text(s)->assign(20 + i, static_cast<char>('a' + i % 26));
        else Text(s)->assign(1 + i % 5, 'b');
        blocks.push_back(Block(*Text(s)));
    }
    return buffer;
}
void SetSpan(void *at, void *first, uint32_t n, uint32_t capacity, uint32_t stride)
{
    auto *w = static_cast<uint8_t **>(at);
    w[0] = static_cast<uint8_t *>(first);
    w[1] = first ? w[0] + n * stride : nullptr;
    w[2] = first ? w[0] + capacity * stride : nullptr;
}

uint8_t gRefBuffer[16 * 40]; // slot 523's entries: a static buffer, the same on both sides (the thunk frees nothing)

// Puts round `round`'s live object in place (after the entry ran); returns the heap blocks to follow (arrays).
std::vector<const void *> Live(const difftest::Context &ctx, const HTStatic &e, int round)
{
    std::vector<const void *> blocks;
    const uint32_t n = round == 0 ? 0 : round == 1 ? 1 : round == 2 ? 3 : round == 3 ? 7 : round == 4 ? 16 : 33;
    uint8_t *object = At<uint8_t>(ctx, e.storage);
    switch (e.kind) {
    case HTKind::ClassicLocale: {
        // _Ptr at +4: none (even rounds 0), a facet whose _Decref keeps it (null) or gives it back to delete.
        const bool none = round == 0 || round == 3;
        Fake *facet = MakeFake(1, gLocaleVt);
        facet->ret = round % 2 ? reinterpret_cast<uint32_t>(facet) : 0;
        *reinterpret_cast<Fake **>(object + 4) = none ? nullptr : facet;
        break;
    }
    case HTKind::PointerVector: {
        void *first = n ? HeapAlloc(GetProcessHeap(), 0, 4 * (n + round)) : nullptr;
        if (first) std::memset(first, 0x33, 4 * n);
        SetSpan(object, first, n, n + round, 4);
        break;
    }
    case HTKind::DbgHelpUnload: {
        auto *t = reinterpret_cast<appbase::DbgHelpTable *>(object);
        uint8_t *flags = At<uint8_t>(ctx, appbase::DbgHelpTable::kDoneFlag);
        flags[0] = round == 0 || round == 3 ? 0 : round == 4 ? 0x80 : 1;
        t->process = reinterpret_cast<void *>(static_cast<uintptr_t>(0x5550 + round));
        void **modules[] = {&t->dbghelp, &t->ntdll, &t->kernel32, &t->tlhelp32, &t->psapi};
        for (int k = 0; k < 5; ++k) *modules[k] = (round + k) % 3 ? reinterpret_cast<void *>(static_cast<uintptr_t>(0x6000 + 16 * k + round)) : nullptr;
        t->symCleanup = round % 2 ? reinterpret_cast<void *>(&FakeSymCleanup) : nullptr;
        break;
    }
    case HTKind::ModuleHandle:
        *reinterpret_cast<uint32_t *>(object) = round % 2 ? 0x7000 + static_cast<uint32_t>(round) : 0;
        break;
    case HTKind::RecordArray: {
        auto *records = reinterpret_cast<staticinit::Record7c *>(object);
        for (uint32_t i = 0; i < e.count; ++i) {
            if (n && i % (34 - n) == 0) Text(&records[i].first)->assign(16 + i % 40, 'f');
            if (n > 3 && i % 5 == 1) Text(&records[i].second)->assign(30, 's');
            blocks.push_back(Block(*Text(&records[i].first)));
            blocks.push_back(Block(*Text(&records[i].second)));
        }
        break;
    }
    case HTKind::RZStringVector: {
        uint8_t *buffer = n ? StringArray(n, n + 2, 0x20, blocks) : nullptr;
        SetSpan(object, buffer, n, n + 2, 0x20);
        blocks.push_back(buffer);
        break;
    }
    case HTKind::PairVectors: {
        auto *v = ::new (object) std::vector<std::vector<uint64_t>>(n);
        for (uint32_t i = 0; i < n; ++i) (*v)[i].resize(i % 4 == 3 ? 0 : 1 + i);
        break;
    }
    case HTKind::MotiveCurveSets: break; // the built sets as they are
    case HTKind::Strings40:
    case HTKind::Strings828: {
        const uint32_t stride = e.kind == HTKind::Strings40 ? 40 : 0x828;
        const uint32_t count = e.kind == HTKind::Strings40 ? n : n / 4;
        uint8_t *buffer = count ? StringArray(count, count + 1, stride, blocks) : nullptr;
        SetSpan(object, buffer, count, count + 1, stride);
        blocks.push_back(buffer);
        break;
    }
    case HTKind::RecordMap: {
        auto *map = reinterpret_cast<staticinit::RecordMap *>(object);
        for (uint32_t i = 0; i < n; ++i) {
            staticinit::MapRecord &r = map->try_emplace(i * 2654435761u).first->second;
            if (i % 2) Text(r.At<void>(staticinit::MapRecord::kName))->assign(20 + i, 'n');
            if (i % 3 == 0) Text(r.At<void>(staticinit::MapRecord::kText))->assign(40 + i, 't');
            r.At<std::vector<staticinit::MapRecord::Row>>(staticinit::MapRecord::kRows)->resize(i % 4);
            r.At<std::vector<uint8_t>>(staticinit::MapRecord::kBufferBytes)->resize(i % 3 * 50);
        }
        break;
    }
    case HTKind::ActionLog: {
        auto *log = reinterpret_cast<staticinit::ActionLog *>(object);
        for (uint32_t i = 0; i < n; ++i) log->counts[i * 7 + 1] = i;
        uint8_t *buffer = n ? StringArray(n, n + 1, 0x20, blocks) : nullptr;
        SetSpan(&log->lines, buffer, n, n + 1, 0x20);
        if (round % 2) Text(&log->name)->assign(25, 'l');
        log->out = round == 0 || round == 4 ? nullptr : MakeFake(2, gOutVt);
        break;
    }
    case HTKind::ReleaseRefs: {
        std::memset(gRefBuffer, 0x77, sizeof gRefBuffer);
        for (uint32_t i = 0; i < n && i < 40; ++i)
            *reinterpret_cast<Fake **>(gRefBuffer + 16 * i + 0xc) = MakeFake(4 + static_cast<int>(i), gRefVt, i);
        SetSpan(object, round == 0 ? nullptr : gRefBuffer, std::min<uint32_t>(n, 40), 40, 16);
        break;
    }
    case HTKind::NamedList: {
        auto *list = reinterpret_cast<std::list<staticinit::ListRecord> *>(object);
        for (uint32_t i = 0; i < n; ++i) {
            staticinit::ListRecord &r = list->emplace_back();
            Text(r.bytes + staticinit::ListRecord::kName)->assign(i % 2 ? 3 : 18 + i, 'r');
        }
        break;
    }
    case HTKind::WorkerShutdown: {
        void **threads = At<void *>(ctx, staticinit::WorkerThreads::kThreads);
        uint8_t *suspended = At<uint8_t>(ctx, staticinit::WorkerThreads::kSuspended);
        for (int k = 0; k < 3; ++k) {
            threads[k] = (round + k) % 4 == 0 ? nullptr : MakeFake(44 + k, gWorkerVt, 0, (round * 3 + k) % 5);
            suspended[k] = static_cast<uint8_t>((round + k) % 3 == 1 ? 0 : k + 1);
        }
        gSeconds = round == 5 ? 0xfffffffeLL : 1700000000LL + round;
        gSleeps = 0;
        break;
    }
    default: break;
    }
    return blocks;
}

struct Run {
    Shot built, destroyed;
    std::vector<Thunk> registered;
    std::vector<uint32_t> entryLog, thunkLog;
    std::vector<size_t> freed;
    std::vector<int> order;
    std::vector<uint8_t> refs; // slot 523: the entries after the thunk
};

std::vector<int> FreeOrder(const difftest::FreeRecorder &rec, const std::vector<const void *> &blocks)
{
    std::vector<int> order;
    for (size_t i = 0; i < rec.Count(); ++i) {
        auto it = std::find(blocks.begin(), blocks.end(), rec.Pointer(i));
        order.push_back(it == blocks.end() ? -1 : static_cast<int>(it - blocks.begin()));
    }
    return order;
}

Run RunSide(difftest::Context &ctx, const HTStatic &e, bool image, const std::vector<uint8_t> &start, int round)
{
    Run r;
    const Area area = AreaOf(e);
    Put(ctx, area, start);
    gRegistered.clear();
    gLog.clear();
    if (image) ctx.Original<void(__cdecl *)()>(e.imageRva)();
    else staticinit::InitHT(&e);
    r.registered = gRegistered;
    r.entryLog = gLog;
    r.built = Built(ctx, e);
    if (r.registered.size() != 1) return r;
    const std::vector<const void *> blocks = Live(ctx, e, round);
    gLog.clear();
    {
        difftest::FreeRecorder rec;
        r.registered[0]();
        rec.Stop();
        for (size_t k = 0; k < rec.Count(); ++k) r.freed.push_back(rec.Size(k));
        r.order = FreeOrder(rec, blocks);
    }
    r.thunkLog = gLog;
    r.destroyed = Destroyed(ctx, e);
    if (e.kind == HTKind::ReleaseRefs) r.refs.assign(gRefBuffer, gRefBuffer + sizeof gRefBuffer);
    return r;
}

const HTStatic *Find(uint32_t slot)
{
    size_t n = 0;
    const HTStatic *all = staticinit::HTStatics(&n);
    for (size_t i = 0; i < n; ++i)
        if (all[i].slot == slot) return &all[i];
    return nullptr;
}

} // namespace

DIFF_TEST(StaticInitHT_Entries, 0x138323)
{
    Fixture fixture(ctx);
    size_t n = 0;
    const HTStatic *entries = staticinit::HTStatics(&n);
    ++ctx.cases;
    if (n != 25) ctx.Fail("%u entries (25)", static_cast<unsigned>(n));
    std::mt19937 rng(0x4854);
    uint8_t *rzStdGlobal = ctx.image.At<uint8_t>(kRZStdGlobal);
    const uint8_t savedRZStdGlobal = *rzStdGlobal;
    const std::vector<uint8_t> savedFlags = Take(ctx, {appbase::DbgHelpTable::kDoneFlag, 4});
    for (size_t i = 0; i < n; ++i) {
        const HTStatic &e = entries[i];
        ++ctx.cases;
        if (!SlotHolds(ctx, e.slot, e.imageRva)) continue;
        if (e.kind == HTKind::MadCowDirector) continue; // StaticInitHT_MadCow
        const Area area = AreaOf(e);
        const std::vector<uint8_t> saved = Take(ctx, area);
        const uint32_t imageThunk = e.thunk ? static_cast<uint32_t>(ctx.image.Base()) + e.thunk : 0;
        for (int round = 0; round < 6; ++round) {
            ++ctx.cases;
            std::vector<uint8_t> start(area.size);
            for (auto &b : start) b = static_cast<uint8_t>(rng());
            // The worker threads' words: never junk (the thunk calls through them); Live fills them.
            if (e.kind == HTKind::WorkerShutdown) std::memset(start.data() + kMargin, 0, 12);
            // The containers' size words the entries leave (the file's zero): junk in round 0 only, where nothing is
            // inserted (the teardown walks the links, not the size).
            if (round > 0) {
                const size_t sizeWord = e.kind == HTKind::RecordMap ? 8 : e.kind == HTKind::ActionLog ? 0x18 : e.kind == HTKind::NamedList ? 4 : 0;
                if (sizeWord) std::memset(start.data() + kMargin + sizeWord, 0, 4);
            }
            if (e.kind == HTKind::RZStd) *rzStdGlobal = static_cast<uint8_t>(round % 2 ? rng() | 1 : 0);
            gUnexpected = false;
            Run a = RunSide(ctx, e, true, start, round);
            Run b = RunSide(ctx, e, false, start, round);
            const size_t expected = e.thunk ? 1 : 0;
            if (a.registered.size() != expected || b.registered.size() != expected) {
                ctx.Fail("slot %u: %u registrations by the image's entry, %u by ours (%u)", e.slot,
                         static_cast<unsigned>(a.registered.size()), static_cast<unsigned>(b.registered.size()), static_cast<unsigned>(expected));
                continue;
            }
            if (expected && (reinterpret_cast<uint32_t>(a.registered[0]) != imageThunk || b.registered[0] == a.registered[0]))
                ctx.Fail("slot %u: the image's entry did not register %08x, or ours registered the image's", e.slot, e.thunk);
            if (a.entryLog != b.entryLog) ctx.Fail("slot %u round %d: the entries' calls differ (%u, %u)", e.slot, round,
                                                   static_cast<unsigned>(a.entryLog.size()), static_cast<unsigned>(b.entryLog.size()));
            if (a.built != b.built) ctx.Fail("slot %u round %d: the window after the entry differs", e.slot, round);
            if (a.thunkLog != b.thunkLog) ctx.Fail("slot %u round %d: the thunks' calls differ (%u, %u)", e.slot, round,
                                                   static_cast<unsigned>(a.thunkLog.size()), static_cast<unsigned>(b.thunkLog.size()));
            if (a.freed != b.freed) ctx.Fail("slot %u round %d: the thunks free %u / %u blocks", e.slot, round,
                                             static_cast<unsigned>(a.freed.size()), static_cast<unsigned>(b.freed.size()));
            if (a.order != b.order) ctx.Fail("slot %u round %d: the elements are freed in another order", e.slot, round);
            if (a.destroyed != b.destroyed) ctx.Fail("slot %u round %d: the window after the thunk differs", e.slot, round);
            if (a.refs != b.refs) ctx.Fail("slot %u round %d: the released entries differ", e.slot, round);
            if (gUnexpected) ctx.Fail("slot %u round %d: a fake's unexpected slot was called", e.slot, round);
        }
        Put(ctx, area, saved);
    }
    *rzStdGlobal = savedRZStdGlobal;
    Put(ctx, {appbase::DbgHelpTable::kDoneFlag, 4}, savedFlags);
}

// Slot 766: comdir::InitMadCow, its handler bound to ours for the call.
DIFF_TEST(StaticInitHT_MadCow, 0x1408d0)
{
    Fixture fixture(ctx);
    const HTStatic *e = Find(766);
    ++ctx.cases;
    if (!e || !SlotHolds(ctx, e->slot, e->imageRva)) return;
    const Area area = AreaOf(*e);
    const std::vector<uint8_t> saved = Take(ctx, area);
    std::vector<size_t> freed[2];
    std::vector<Thunk> registered[2];
    for (int side = 0; side < 2; ++side) {
        Put(ctx, area, saved);
        gRegistered.clear();
        int (*const handler)(void (*)()) = gRegisterExitHandler;
        if (side == 0) ctx.Original<void(__cdecl *)()>(e->imageRva)();
        else staticinit::InitHT(e);
        if (gRegisterExitHandler != handler) ctx.Fail("gRegisterExitHandler was left changed");
        registered[side] = gRegistered;
        if (gRegistered.size() != 1) continue;
        difftest::FreeRecorder rec;
        gRegistered[0]();
        rec.Stop();
        for (size_t k = 0; k < rec.Count(); ++k) freed[side].push_back(rec.Size(k));
        std::sort(freed[side].begin(), freed[side].end());
    }
    Put(ctx, area, saved);
    if (registered[0].size() != 1 || registered[1].size() != 1)
        ctx.Fail("%u / %u registrations (1)", static_cast<unsigned>(registered[0].size()), static_cast<unsigned>(registered[1].size()));
    else if (reinterpret_cast<uint32_t>(registered[0][0]) - ctx.image.Base() != e->thunk || registered[1][0] == registered[0][0])
        ctx.Fail("the registered handlers");
    if (freed[0] != freed[1] || freed[0].empty())
        ctx.Fail("the handlers free %u / %u blocks", static_cast<unsigned>(freed[0].size()), static_cast<unsigned>(freed[1].size()));
}

DIFF_TEST(StaticInitHT_OnImageTable, 0x13e475)
{
    Fixture fixture(ctx);
    size_t ownedCount = 0, n = 0;
    const winmainstart::OwnedInitialiser *owned = staticinit::OwnedHT(&ownedCount);
    const HTStatic *entries = staticinit::HTStatics(&n);
    const uint32_t slots = (kXcLast - kXcFirst) / 4;
    std::vector<winmainstart::Initialiser> table(slots, nullptr);
    for (size_t i = 0; i < ownedCount; ++i)
        if (owned[i].slot < slots) table[owned[i].slot] = ctx.image.At<winmainstart::Initialiser>(kXcFirst)[owned[i].slot];
    std::vector<std::vector<uint8_t>> saved;
    for (size_t i = 0; i < n; ++i) saved.push_back(Take(ctx, AreaOf(entries[i])));
    auto restore = [&] {
        for (size_t i = 0; i < n; ++i) Put(ctx, AreaOf(entries[i]), saved[i]);
    };
    std::vector<uint32_t> log[2], exitLog[2];
    std::vector<Shot> built[2], destroyed[2];
    std::vector<size_t> freed[2];
    size_t registrations[2];
    winmainstart::XcRunCounts c{};
    for (int side = 0; side < 2; ++side) {
        restore();
        gLog.clear();
        gRegistered.clear();
        gSeconds = 1700000000;
        if (side == 0) {
            for (uint32_t s = 0; s < slots; ++s)
                if (table[s]) table[s]();
        } else {
            c = winmainstart::RunInitialisersOwned(table.data(), table.data() + slots, ctx.image.Base(), owned, ownedCount);
        }
        log[side] = gLog;
        for (size_t i = 0; i < n; ++i)
            if (entries[i].kind != HTKind::MadCowDirector) built[side].push_back(Built(ctx, entries[i]));
        registrations[side] = gRegistered.size();
        gLog.clear();
        {
            difftest::FreeRecorder rec;
            for (auto it = gRegistered.rbegin(); it != gRegistered.rend(); ++it) (*it)();
            rec.Stop();
            for (size_t k = 0; k < rec.Count(); ++k) freed[side].push_back(rec.Size(k));
        }
        exitLog[side] = gLog;
        for (size_t i = 0; i < n; ++i)
            if (entries[i].kind != HTKind::MadCowDirector) destroyed[side].push_back(Destroyed(ctx, entries[i]));
    }
    restore();
    ++ctx.cases;
    if (c.owned != 25 || ownedCount != 25 || c.image || c.mismatched || c.unused)
        ctx.Fail("counts: %d ours, %d image, %d mismatched, %d unused", c.owned, c.image, c.mismatched, c.unused);
    if (log[0] != log[1]) ctx.Fail("the entries' calls differ (%u, %u)", static_cast<unsigned>(log[0].size()), static_cast<unsigned>(log[1].size()));
    std::vector<uint32_t> windowSlots;
    for (size_t i = 0; i < n; ++i)
        if (entries[i].kind != HTKind::MadCowDirector) windowSlots.push_back(entries[i].slot);
    for (size_t i = 0; i < built[0].size() && i < built[1].size(); ++i) {
        if (built[0][i] != built[1][i]) ctx.Fail("slot %u: the window differs after the run", windowSlots[i]);
        if (destroyed[0][i] != destroyed[1][i]) ctx.Fail("slot %u: the window differs after the exit", windowSlots[i]);
    }
    // Every entry but slot 8's registers one.
    if (registrations[0] != registrations[1] || registrations[0] != 24)
        ctx.Fail("registrations %u / %u (24)", static_cast<unsigned>(registrations[0]), static_cast<unsigned>(registrations[1]));
    if (freed[0] != freed[1]) ctx.Fail("exit: %u / %u blocks freed", static_cast<unsigned>(freed[0].size()), static_cast<unsigned>(freed[1].size()));
    if (exitLog[0] != exitLog[1]) ctx.Fail("exit calls differ (%u, %u)", static_cast<unsigned>(exitLog[0].size()), static_cast<unsigned>(exitLog[1].size()));
}

// ---- Members ----

// 0x1bd611 on the global table: the done flag clear, set and junk; modules, process and SymCleanup set and null.
DIFF_TEST(StaticInitHT_DbgHelpUnload, 0x1bd611)
{
    gBase = ctx.image.Base();
    BindImport freeLibrary(ctx, kIatFreeLibrary, reinterpret_cast<const void *>(&FakeFreeLibrary));
    BindImport closeHandle(ctx, kIatCloseHandle, reinterpret_cast<const void *>(&FakeCloseHandle));
    const Area area{appbase::DbgHelpTable::kDoneFlag - 4, 8 + sizeof(appbase::DbgHelpTable) + kMargin};
    const std::vector<uint8_t> saved = Take(ctx, area);
    auto *table = ctx.image.At<appbase::DbgHelpTable>(appbase::DbgHelpTable::kGlobal);
    uint8_t *flags = ctx.image.At<uint8_t>(appbase::DbgHelpTable::kDoneFlag);
    std::mt19937 rng(0x1bd611);
    for (int round = 0; round < 64; ++round) {
        std::vector<uint8_t> start(area.size);
        for (auto &b : start) b = static_cast<uint8_t>(rng());
        std::vector<uint8_t> after[2];
        std::vector<uint32_t> log[2];
        uint8_t result[2];
        for (int side = 0; side < 2; ++side) {
            Put(ctx, area, start);
            flags[0] = round % 4 == 0 ? 0 : static_cast<uint8_t>(round % 4 == 1 ? 1 : rng() | 1);
            void **modules[] = {&table->dbghelp, &table->ntdll, &table->kernel32, &table->tlhelp32, &table->psapi};
            for (int k = 0; k < 5; ++k)
                if ((round >> k) & 1) *modules[k] = nullptr;
            table->symCleanup = round & 2 ? reinterpret_cast<void *>(&FakeSymCleanup) : nullptr;
            gLog.clear();
            result[side] = side == 0 ? ctx.Original<uint8_t(__thiscall *)(void *)>(kUnload)(table) : table->Unload();
            after[side] = Take(ctx, area);
            log[side] = gLog;
        }
        ++ctx.cases;
        if (result[0] != result[1] || after[0] != after[1] || log[0] != log[1])
            ctx.Fail("round %d: result %u / %u, %s, %u / %u calls", round, result[0], result[1], after[0] == after[1] ? "same memory" : "memory differs",
                     static_cast<unsigned>(log[0].size()), static_cast<unsigned>(log[1].size()));
    }
    Put(ctx, area, saved);
}

// 0x99936e: three fake workers (some null, some suspended, each running a few more passes), deadlines -1 and 0..3
// seconds, clocks of 1.7e9 s and just below 2^32 s (the deadline's low dword wraps), and from above 2^32 s.
DIFF_TEST(StaticInitHT_WorkerShutdown, 0x99936e)
{
    gBase = ctx.image.Base();
    WorkerFixture workers(ctx);
    const Area area{staticinit::WorkerThreads::kThreads - kMargin, staticinit::WorkerThreads::kBusy + 4 - staticinit::WorkerThreads::kThreads + kMargin};
    const std::vector<uint8_t> saved = Take(ctx, area);
    void **threads = ctx.image.At<void *>(staticinit::WorkerThreads::kThreads);
    uint8_t *suspended = ctx.image.At<uint8_t>(staticinit::WorkerThreads::kSuspended);
    std::mt19937 rng(0x99936e);
    const int64_t clocks[] = {1700000000LL, 0xfffffffdLL, 0x100000003LL};
    for (int round = 0; round < 90; ++round) {
        const int32_t seconds = round % 5 == 4 ? -1 : round % 5;
        const int64_t clock = clocks[round / 5 % 3];
        std::vector<uint8_t> start(area.size);
        for (auto &b : start) b = static_cast<uint8_t>(rng());
        int32_t countdowns[3];
        bool present[3];
        uint8_t flags[3];
        for (int k = 0; k < 3; ++k) {
            countdowns[k] = static_cast<int32_t>(rng() % (round % 3 == 0 ? 3 : 14));
            present[k] = rng() % 4 != 0;
            flags[k] = static_cast<uint8_t>(rng() % 3 == 0 ? 0 : rng() | 1);
        }
        std::vector<uint8_t> after[2];
        std::vector<uint32_t> log[2];
        uint32_t result[2];
        for (int side = 0; side < 2; ++side) {
            Put(ctx, area, start);
            for (int k = 0; k < 3; ++k) {
                threads[k] = present[k] ? MakeFake(44 + k, gWorkerVt, 0, countdowns[k]) : nullptr;
                suspended[k] = flags[k];
            }
            gSeconds = clock;
            gSleeps = 0;
            gLog.clear();
            result[side] = side == 0 ? ctx.Original<uint32_t(__cdecl *)(int32_t)>(kShutdown)(seconds) : staticinit::WorkerThreads::Shutdown(seconds);
            after[side] = Take(ctx, area);
            log[side] = gLog;
        }
        ++ctx.cases;
        if (result[0] != result[1] || after[0] != after[1] || log[0] != log[1])
            ctx.Fail("round %d (%d s): %u / %u deleted, %s, %u / %u calls", round, seconds, result[0], result[1],
                     after[0] == after[1] ? "same memory" : "memory differs", static_cast<unsigned>(log[0].size()), static_cast<unsigned>(log[1].size()));
    }
    Put(ctx, area, saved);
}

// 0x526c30 on a scratch log: lines (some long) written and destroyed in place, the vector emptied; none, one, many.
DIFF_TEST(StaticInitHT_ActionLogFlush, 0x526c30)
{
    gBase = ctx.image.Base();
    for (uint32_t n : {0u, 1u, 2u, 5u, 17u}) {
        std::vector<uint32_t> log[2];
        std::vector<size_t> freed[2];
        std::vector<int> order[2];
        uint32_t spans[2][3];
        for (int side = 0; side < 2; ++side) {
            alignas(4) uint8_t bytes[sizeof(staticinit::ActionLog)];
            std::memset(bytes, 0x3c, sizeof bytes);
            auto *object = reinterpret_cast<staticinit::ActionLog *>(bytes);
            std::vector<const void *> blocks;
            uint8_t *buffer = n ? StringArray(n, n + 3, 0x20, blocks) : nullptr;
            SetSpan(&object->lines, buffer, n, n + 3, 0x20);
            object->out = MakeFake(2, gOutVt);
            gLog.clear();
            {
                difftest::FreeRecorder rec;
                if (side == 0) ctx.Original<void(__thiscall *)(void *)>(kFlush)(object);
                else object->Flush();
                rec.Stop();
                for (size_t k = 0; k < rec.Count(); ++k) freed[side].push_back(rec.Size(k));
                order[side] = FreeOrder(rec, blocks);
            }
            log[side] = gLog;
            spans[side][0] = object->lines.mFirst == reinterpret_cast<cRZString *>(buffer);
            spans[side][1] = static_cast<uint32_t>(object->lines.mLast - object->lines.mFirst);
            spans[side][2] = static_cast<uint32_t>(object->lines.mEnd - object->lines.mFirst);
            if (buffer) std::allocator<uint8_t>().deallocate(buffer, (n + 3) * 0x20);
        }
        ++ctx.cases;
        if (log[0] != log[1] || freed[0] != freed[1] || order[0] != order[1] || std::memcmp(spans[0], spans[1], sizeof spans[0]) != 0)
            ctx.Fail("%u lines: %u / %u calls, %u / %u frees", n, static_cast<unsigned>(log[0].size()), static_cast<unsigned>(log[1].size()),
                     static_cast<unsigned>(freed[0].size()), static_cast<unsigned>(freed[1].size()));
        if (n && log[0].size() != 3 * n) ctx.Fail("%u lines: %u calls logged", n, static_cast<unsigned>(log[0].size() / 3));
    }
}

namespace {

// cRZCallbackThread's thread slots, logged with the bytes queued at the time (what a woken thread would still run);
// slot 11 (IsRunning) returns gThreadRunning.
uint32_t gThreadRunning = 0;
uint32_t QueuedBytes(void *thread)
{
    const uint32_t *queue = reinterpret_cast<const uint32_t *>(static_cast<uint8_t *>(thread) - 0x38 + 0x10);
    return queue[1] - queue[0];
}
template <int Slot> uint32_t __fastcall T0(void *self, void *)
{
    Log(200 + Slot, QueuedBytes(self), 0);
    return Slot == 11 ? gThreadRunning : 0;
}
uint32_t __fastcall T8(void *self, void *, int32_t timeout)
{
    Log(208, static_cast<uint32_t>(timeout), QueuedBytes(self));
    return 0;
}
VTable gThreadVt;
struct ThreadVtInit {
    ThreadVtInit()
    {
        SetSlot(gThreadVt, 5, &T0<5>);
        SetSlot(gThreadVt, 7, &T0<7>);
        SetSlot(gThreadVt, 8, &T8);
        SetSlot(gThreadVt, 11, &T0<11>);
    }
} gThreadVtInit;

// One callback thread destroyed by each side (`deleting`: through slot 8 with `flags`): built by our verified
// constructor, the thread's vtable swapped for the logging one, the queue holding `queued` entries.
void CompareDestruct(difftest::Context &ctx, bool deleting)
{
    gBase = ctx.image.Base();
    for (int round = 0; round < 8; ++round) {
        const uint32_t queued = round / 2 * 3;
        gThreadRunning = round % 2;
        const uint32_t flags = deleting ? (round % 4 < 2 ? 1u : 0u) | (round == 6 ? 0x10u : 0u) : 0;
        std::vector<uint32_t> log[2];
        std::vector<size_t> freed[2];
        std::vector<uint8_t> after[2];
        void *returned[2];
        for (int side = 0; side < 2; ++side) {
            auto *t = static_cast<cRZCallbackThread *>(::operator new(cRZCallbackThread::kSize));
            std::memset(t, 0x6b, cRZCallbackThread::kSize);
            t->Construct();
            t->mThread.mVTable = gThreadVt.slots;
            // Both vtables junk: the destructor stores its own.
            t->mVTable = reinterpret_cast<const void *>(static_cast<uintptr_t>(0x5a5a0000 + round));
            t->mStartableVTable = reinterpret_cast<const void *>(static_cast<uintptr_t>(0xa5a50000 + round));
            if (queued) {
                auto *buffer = static_cast<uint8_t *>(::operator new(28 * (queued + 1)));
                std::memset(buffer, 0x11, 28 * (queued + 1));
                SetSpan(&t->mField10, buffer, queued, queued + 1, 28);
            }
            gLog.clear();
            {
                difftest::FreeRecorder rec;
                if (deleting) {
                    returned[side] = side == 0 ? ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kCallbackThreadDeleting)(t, flags)
                                               : t->DeletingDestruct(flags);
                } else {
                    returned[side] = t;
                    if (side == 0) ctx.Original<void(__thiscall *)(void *)>(kCallbackThreadDtor)(t);
                    else t->Destruct();
                }
                rec.Stop();
                for (size_t k = 0; k < rec.Count(); ++k) freed[side].push_back(rec.Size(k));
            }
            log[side] = gLog;
            returned[side] = returned[side] == t ? nullptr : returned[side];
            if (!(flags & 1)) {
                const uint8_t *p = reinterpret_cast<const uint8_t *>(t);
                after[side].assign(p, p + cRZCallbackThread::kSize);
                // The stopwatch's start (the clock), the thread's state and the lock's block (freed, left dangling).
                std::memset(after[side].data() + 0x20, 0, 0x18);
                std::memset(after[side].data() + 0x3c, 0, 4);
                std::memset(after[side].data() + 0x48, 0, 4);
                ::operator delete(t);
            }
        }
        ++ctx.cases;
        if (log[0] != log[1] || freed[0] != freed[1] || after[0] != after[1] || returned[0] || returned[1])
            ctx.Fail("round %d: %u / %u calls, %u / %u frees, %s", round, static_cast<unsigned>(log[0].size()), static_cast<unsigned>(log[1].size()),
                     static_cast<unsigned>(freed[0].size()), static_cast<unsigned>(freed[1].size()), after[0] == after[1] ? "same object" : "object differs");
    }
}

} // namespace

DIFF_TEST(StaticInitHT_CallbackThreadDestruct, 0x9cfac2) { CompareDestruct(ctx, false); }
DIFF_TEST(StaticInitHT_CallbackThreadDeletingDestruct, 0x9cfb3e) { CompareDestruct(ctx, true); }
