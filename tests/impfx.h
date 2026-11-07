#pragma once
// impfx: the import-fake table (foundations campaign, docs/subsystems/foundations.md "Hard test boundaries"; how-to in
// docs/lane/test.md, "Faking imports"). Every Win32 call the code under test makes through the game's import address
// table (the original's `call [iat]`, ours through ImportFunction<Fn, IatRva>(), src/game/import.h) goes to one recording
// fake while an impfx::Imports is alive, on both sides alike:
//
//   impfx::Imports imp(ctx);                     // restores every slot it patched when it goes
//   IMPFX_ADD(imp, SetCursor).Returns(0x5c000004); // a plain fake: logs, answers a value (or runs a handler: On)
//   imp.Threads(); imp.Sync(); imp.Clock();      // stateful kinds: fake threads, events / mutexes / semaphores, a clock
//   auto [orig, ours] = ...drawfx::Side([&] { imp.BeginSide(); ... }, [&] { ...call... });   // per side
//
// Built on drawfx (tests/drawfx_core.h): an import is a slot of the "import" interface (one fake, id kId), its thunk a
// `__stdcall` (or `__cdecl`) function of the import's argument count, so calls land in drawfx's log next to the calls on
// any drawfx / d3dfx / winfx fake, are normalised by the same argument kinds (a fake's address becomes its id, a stack
// address a marker, 's' logs text) and are compared with drawfx::Same. The IAT slot is found by name in the mapped
// image's import directory, so a test never writes an IAT RVA; the argument count comes from the SDK prototype
// (IMPFX_ADD: decltype(::Api)), so nothing is hand-counted.
//
// The kinds keep state per side (BeginSide resets it; the test's settings below survive):
// - Threads(): CreateThread / ResumeThread / SuspendThread / Set/GetThreadPriority / GetExitCodeThread / QueueUserAPC /
//   GetCurrentThreadId / Sleep / SleepEx. A created thread is a record (start, parameter, suspend count, priority,
//   exit code); `run` decides when its routine runs: Run::Never (default), Run::Inline (on this thread, when it is
//   created unsuspended or resumed to count 0, or when a wait needs it), or the test calls RunThread. Sleep and SleepEx
//   log, advance the clock (both: the performance counter by ms * perfFrequency / 1000) and return at once; SleepEx(alertable) inside a fake thread runs its queued APCs first
//   (WAIT_IO_COMPLETION).
// - CrtThreads(): the CRT's _beginthreadex / _endthreadex (library code: the image's at 0x83a8bc / 0x83a93f and our
//   CRT's) stood in on both sides by the same thread records; _endthreadex records the exit code and returns.
// - Sync(): CreateEventA / SetEvent / ResetEvent / PulseEvent / CreateMutexA / ReleaseMutex / CreateSemaphoreA /
//   ReleaseSemaphore / WaitForSingleObject(Ex) / WaitForMultipleObjects / CloseHandle on fake handles with state. A
//   wait answers from the script (ScriptWaits) first, else from the object; an INFINITE wait that could never return
//   is an error of the side (the test fails, naming it) and answers WAIT_FAILED.
// - Clock(): timeGetTime / GetTickCount (milliseconds), QueryPerformanceCounter / Frequency, from one clock that the
//   test sets (clockMs, perfCount, perfFrequency) and that each read advances by msStep / perfStep.
// - Libraries(): LoadLibraryA / FreeLibrary / GetProcAddress over fake modules (Module) whose exports are recording
//   fakes (Export), for code that loads a library at run time.
// Handles are words 0x4a000004, 0x4a000008, ... in creation order per side, thread ids 0x7101, 0x7102, ...; the test's
// own thread is kMainThreadId. Out arguments get exactly the bytes the API writes (a DWORD, a LARGE_INTEGER).
#include "code_patch.h"
#include "difftest.h"
#include "drawfx_core.h"
#include "game/game.h"

#include <windows.h>
#include <process.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace impfx {

inline constexpr uint32_t kId = 0x1a7f0001;   // the import fake's id in the log
inline constexpr unsigned kFirstSlot = 3;     // drawfx gives slots 0-2 their COM meaning (QueryInterface, AddRef, Release)
inline constexpr unsigned kMaxImports = 48;   // imports (and exports of fake modules) one Imports can fake at once
inline constexpr unsigned kMaxWords = 12;     // stack words of the widest import (CreateWindowExA)
inline constexpr uint32_t kMainThreadId = 0x7000;
inline constexpr uint32_t kFirstHandle = 0x4a000004;
inline constexpr uint32_t kStillActive = 0x103;

enum class Conv : uint8_t { Std, Cdecl };
enum class Run : uint8_t { Never, Inline };

inline drawfx::Slot gSlots[kFirstSlot + kMaxImports];
inline const drawfx::Iface gIface{"import", kFirstSlot + kMaxImports, gSlots};
inline drawfx::Fake gFake;

// ---- thunks: one per (slot, word count, convention), all dispatching to gFake ----
namespace detail {
template <size_t> using Word = uint32_t;
template <unsigned Slot, class Seq> struct Gen;
template <unsigned Slot, size_t... I> struct Gen<Slot, std::index_sequence<I...>> {
    static uint32_t __stdcall Std(Word<I>... a)
    {
        const uint32_t args[] = {a..., 0u};
        return drawfx::detail::Dispatch(gFake, Slot, args, unsigned(sizeof...(I)));
    }
    static uint32_t __cdecl Cdecl(Word<I>... a)
    {
        const uint32_t args[] = {a..., 0u};
        return drawfx::detail::Dispatch(gFake, Slot, args, unsigned(sizeof...(I)));
    }
};
using Row = std::array<void *, kMaxImports>;
template <unsigned W, size_t... S> Row StdRow(std::index_sequence<S...>)
{
    return {reinterpret_cast<void *>(&Gen<kFirstSlot + unsigned(S), std::make_index_sequence<W>>::Std)...};
}
template <unsigned W, size_t... S> Row CdeclRow(std::index_sequence<S...>)
{
    return {reinterpret_cast<void *>(&Gen<kFirstSlot + unsigned(S), std::make_index_sequence<W>>::Cdecl)...};
}
template <size_t... W> void *Pick(unsigned words, unsigned slot, Conv conv, std::index_sequence<W...>)
{
    static const Row stdRows[] = {StdRow<unsigned(W)>(std::make_index_sequence<kMaxImports>())...};
    static const Row cdeclRows[] = {CdeclRow<unsigned(W)>(std::make_index_sequence<kMaxImports>())...};
    return (conv == Conv::Std ? stdRows : cdeclRows)[words][slot - kFirstSlot];
}
inline void *Thunk(unsigned words, unsigned slot, Conv conv)
{
    return Pick(words, slot, conv, std::make_index_sequence<kMaxWords + 1>());
}

template <class F> struct Arity;
template <class R, class... A> struct Arity<R __stdcall(A...)> {
    static constexpr unsigned words = (0u + ... + unsigned((sizeof(A) + 3) / 4));
    static constexpr Conv conv = Conv::Std;
};
template <class R, class... A> struct Arity<R __cdecl(A...)> {
    static constexpr unsigned words = (0u + ... + unsigned((sizeof(A) + 3) / 4));
    static constexpr Conv conv = Conv::Cdecl;
};
} // namespace detail

// The IAT RVA of an import of the mapped image, by function name (and DLL name, case-insensitive, when given); 0 if the
// image does not import it.
inline uint32_t FindImport(const char *function, const char *dll = nullptr)
{
    auto *base = GameAddress<uint8_t>(0);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS32 *>(base + dos->e_lfanew);
    const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return 0;
    for (auto *d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress); d->Name; ++d) {
        if (dll && _stricmp(reinterpret_cast<const char *>(base + d->Name), dll) != 0) continue;
        if (!d->OriginalFirstThunk) continue; // the names are only in the lookup table (the IAT holds the traps)
        const auto *names = reinterpret_cast<const IMAGE_THUNK_DATA32 *>(base + d->OriginalFirstThunk);
        for (uint32_t i = 0; names[i].u1.AddressOfData; ++i) {
            if (IMAGE_SNAP_BY_ORDINAL32(names[i].u1.Ordinal)) continue;
            const auto *byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME *>(base + names[i].u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char *>(byName->Name), function) == 0) return d->FirstThunk + i * 4;
        }
    }
    return 0;
}

// ---- the kinds' state, one side at a time ----
enum class Kind : uint8_t { Event, Mutex, Semaphore, Thread, Module };
struct Object {
    uint32_t handle = 0;
    Kind kind = Kind::Event;
    bool open = true;
    bool signaled = false; // events; a thread that has run
    bool manual = false;   // manual-reset event
    int32_t count = 0;     // mutex recursion / semaphore count / module references
    int32_t max = 0;       // semaphore maximum
    uint32_t owner = 0;    // mutex owner thread id
    // threads
    uint32_t id = 0, start = 0, param = 0, suspend = 0, exitCode = kStillActive;
    int32_t priority = 0;
    bool running = false, exited = false;
    std::vector<std::pair<uint32_t, uint32_t>> apcs; // (routine, data)
    // modules
    std::string name;
};
struct ExportDef {
    std::string module, name;
    unsigned slot;
    void *thunk;
};
struct State {
    std::deque<Object> objects; // stable references: threads run while others are created
    uint32_t nextHandle = kFirstHandle, nextThreadId = 0x7101, current = kMainThreadId;
    std::deque<uint32_t> waits;
    uint64_t ms = 0, perf = 0;
};
inline State gState;

// Settings (survive BeginSide).
struct Settings {
    Run run = Run::Never;
    std::vector<uint32_t> waits; // scripted wait results, consumed in order before the objects answer
    uint64_t clockMs = 0x10000, msStep = 0;
    uint64_t perfCount = 0x123456789ull, perfFrequency = 3579545, perfStep = 0;
    std::vector<std::string> modules; // LoadLibraryA succeeds for these names (case-insensitive)
    std::vector<ExportDef> exports;
};
inline Settings gSettings;

inline Object *Find(uint32_t handle)
{
    for (Object &o : gState.objects)
        if (o.handle == handle && o.open) return &o;
    return nullptr;
}
inline Object &NewObject(Kind kind)
{
    Object &o = gState.objects.emplace_back();
    o.kind = kind;
    o.handle = gState.nextHandle;
    gState.nextHandle += 4;
    return o;
}
inline void Error(const char *what, uint32_t value)
{
    char buf[160];
    std::snprintf(buf, sizeof(buf), "impfx: %s (%#x)", what, value);
    drawfx::gErrors.emplace_back(buf);
}
inline uint32_t &Arg(const uint32_t *args, unsigned i) { return const_cast<uint32_t *>(args)[i]; }
template <class T> T *Ptr(uint32_t word) { return reinterpret_cast<T *>(uintptr_t(word)); }

// Runs a fake thread's routine on this thread (once), its exit code from the return value unless _endthreadex set it.
inline void RunThread(Object &t)
{
    if (t.kind != Kind::Thread || t.signaled || t.running) return;
    t.running = true;
    drawfx::Note("impfx thread runs", t.id);
    const uint32_t saved = gState.current;
    gState.current = t.id;
    const uint32_t code = reinterpret_cast<uint32_t(__stdcall *)(uint32_t)>(uintptr_t(t.start))(t.param);
    gState.current = saved;
    if (!t.exited) t.exitCode = code;
    t.running = false;
    t.signaled = true;
    drawfx::Note("impfx thread ends", {t.id, t.exitCode});
}
inline void RunThread(uint32_t handle)
{
    if (Object *t = Find(handle)) RunThread(*t);
}
inline Object *CurrentThread()
{
    for (Object &o : gState.objects)
        if (o.kind == Kind::Thread && o.id == gState.current) return &o;
    return nullptr;
}
inline bool RunApcs(Object &t)
{
    if (t.apcs.empty()) return false;
    auto apcs = std::move(t.apcs);
    t.apcs.clear();
    for (auto [routine, data] : apcs) {
        drawfx::Note("impfx apc runs", {t.id, data});
        reinterpret_cast<void(__stdcall *)(uint32_t)>(uintptr_t(routine))(data);
    }
    return true;
}

// ---- handlers ----
namespace on {
using drawfx::Fake;
// CreateThread / _beginthreadex (security, stack size, start, parameter, flags, id out).
inline uint32_t CreateThread(Fake &, const uint32_t *a)
{
    Object &t = NewObject(Kind::Thread);
    t.id = gState.nextThreadId++;
    t.start = a[2];
    t.param = a[3];
    t.suspend = (a[4] & CREATE_SUSPENDED) ? 1 : 0;
    if (a[5]) *Ptr<uint32_t>(a[5]) = t.id;
    const uint32_t handle = t.handle;
    if (gSettings.run == Run::Inline && !t.suspend) RunThread(t);
    return handle;
}
inline uint32_t EndThread(Fake &, const uint32_t *a)
{
    if (Object *t = CurrentThread()) {
        t->exitCode = a[0];
        t->exited = true;
    } else {
        Error("_endthreadex outside a fake thread", a[0]);
    }
    return 0;
}
inline uint32_t ResumeThread(Fake &, const uint32_t *a)
{
    Object *t = Find(a[0]);
    if (!t || t->kind != Kind::Thread) return 0xffffffff;
    const uint32_t previous = t->suspend;
    if (t->suspend) --t->suspend;
    if (previous == 1 && gSettings.run == Run::Inline) RunThread(*t);
    return previous;
}
inline uint32_t SuspendThread(Fake &, const uint32_t *a)
{
    Object *t = Find(a[0]);
    if (!t || t->kind != Kind::Thread) return 0xffffffff;
    return t->suspend++;
}
inline uint32_t SetThreadPriority(Fake &, const uint32_t *a)
{
    Object *t = Find(a[0]);
    if (!t || t->kind != Kind::Thread) return FALSE;
    t->priority = int32_t(a[1]);
    return TRUE;
}
inline uint32_t GetThreadPriority(Fake &, const uint32_t *a)
{
    Object *t = Find(a[0]);
    return t && t->kind == Kind::Thread ? uint32_t(t->priority) : uint32_t(THREAD_PRIORITY_ERROR_RETURN);
}
inline uint32_t GetExitCodeThread(Fake &, const uint32_t *a)
{
    Object *t = Find(a[0]);
    if (!t || t->kind != Kind::Thread) return FALSE;
    *Ptr<uint32_t>(a[1]) = t->signaled ? t->exitCode : kStillActive;
    return TRUE;
}
inline uint32_t QueueUserAPC(Fake &, const uint32_t *a)
{
    Object *t = Find(a[1]);
    if (!t || t->kind != Kind::Thread || t->signaled) return 0;
    t->apcs.push_back({a[0], a[2]});
    return 1;
}
inline uint32_t GetCurrentThreadId(Fake &, const uint32_t *) { return gState.current; }
// A sleep advances both clocks: the milliseconds and the performance counter by as many ticks.
inline void Advance(uint32_t milliseconds)
{
    gState.ms += milliseconds;
    gState.perf += uint64_t(milliseconds) * gSettings.perfFrequency / 1000;
}
inline uint32_t Sleep(Fake &, const uint32_t *a)
{
    Advance(a[0]);
    return 0;
}
inline uint32_t SleepEx(Fake &, const uint32_t *a)
{
    if (a[1])
        if (Object *t = CurrentThread(); t && RunApcs(*t)) return WAIT_IO_COMPLETION;
    Advance(a[0]);
    return 0;
}
// Sync objects.
inline uint32_t CreateEventA(Fake &, const uint32_t *a)
{
    Object &e = NewObject(Kind::Event);
    e.manual = a[1] != 0;
    e.signaled = a[2] != 0;
    return e.handle;
}
inline uint32_t SetEvent(Fake &, const uint32_t *a)
{
    Object *e = Find(a[0]);
    if (!e || e->kind != Kind::Event) return FALSE;
    e->signaled = true;
    return TRUE;
}
inline uint32_t ResetEvent(Fake &, const uint32_t *a)
{
    Object *e = Find(a[0]);
    if (!e || e->kind != Kind::Event) return FALSE;
    e->signaled = false;
    return TRUE;
}
// No waiter is ever blocked in a fake world: a pulse releases nobody and leaves the event unsignalled.
inline uint32_t PulseEvent(Fake &f, const uint32_t *a) { return ResetEvent(f, a); }
inline uint32_t CreateMutexA(Fake &, const uint32_t *a)
{
    Object &m = NewObject(Kind::Mutex);
    if (a[1]) {
        m.owner = gState.current;
        m.count = 1;
    }
    return m.handle;
}
inline uint32_t ReleaseMutex(Fake &, const uint32_t *a)
{
    Object *m = Find(a[0]);
    if (!m || m->kind != Kind::Mutex || !m->count || m->owner != gState.current) return FALSE;
    if (--m->count == 0) m->owner = 0;
    return TRUE;
}
inline uint32_t CreateSemaphoreA(Fake &, const uint32_t *a)
{
    Object &s = NewObject(Kind::Semaphore);
    s.count = int32_t(a[1]);
    s.max = int32_t(a[2]);
    return s.handle;
}
inline uint32_t ReleaseSemaphore(Fake &, const uint32_t *a)
{
    Object *s = Find(a[0]);
    if (!s || s->kind != Kind::Semaphore || int64_t(s->count) + int32_t(a[1]) > s->max) return FALSE;
    if (a[2]) *Ptr<int32_t>(a[2]) = s->count;
    s->count += int32_t(a[1]);
    return TRUE;
}
inline uint32_t CloseHandle(Fake &, const uint32_t *a)
{
    Object *o = Find(a[0]);
    if (!o) return FALSE;
    o->open = false;
    return TRUE;
}
// Would a wait on o succeed now? (Runs an inline thread that has not run yet.) Consumes the object when it does.
inline bool Acquire(Object &o)
{
    switch (o.kind) {
    case Kind::Thread:
        if (!o.signaled && !o.suspend && gSettings.run == Run::Inline) RunThread(o);
        return o.signaled;
    case Kind::Event:
        if (!o.signaled) return false;
        if (!o.manual) o.signaled = false;
        return true;
    case Kind::Mutex:
        if (o.count && o.owner != gState.current) return false;
        o.owner = gState.current;
        ++o.count;
        return true;
    case Kind::Semaphore:
        if (o.count <= 0) return false;
        --o.count;
        return true;
    default: return false;
    }
}
inline uint32_t WaitResult(uint32_t timeout, uint32_t handle)
{
    if (timeout == INFINITE) {
        Error("INFINITE wait that never returns", handle);
        return WAIT_FAILED;
    }
    return WAIT_TIMEOUT;
}
inline bool Scripted(uint32_t &result)
{
    if (gState.waits.empty()) return false;
    result = gState.waits.front();
    gState.waits.pop_front();
    return true;
}
inline uint32_t WaitForSingleObject(Fake &, const uint32_t *a)
{
    uint32_t r;
    if (Scripted(r)) return r;
    Object *o = Find(a[0]);
    if (!o) return WAIT_FAILED;
    return Acquire(*o) ? WAIT_OBJECT_0 : WaitResult(a[1], a[0]);
}
inline uint32_t WaitForSingleObjectEx(Fake &f, const uint32_t *a)
{
    if (a[2])
        if (Object *t = CurrentThread(); t && RunApcs(*t)) return WAIT_IO_COMPLETION;
    return WaitForSingleObject(f, a);
}
inline uint32_t WaitForMultipleObjects(Fake &, const uint32_t *a)
{
    uint32_t r;
    if (Scripted(r)) return r;
    const uint32_t n = a[0];
    const uint32_t *handles = Ptr<const uint32_t>(a[1]);
    std::vector<Object *> objects;
    for (uint32_t i = 0; i < n; ++i) {
        Object *o = Find(handles[i]);
        if (!o) return WAIT_FAILED;
        objects.push_back(o);
    }
    if (a[2]) {
        for (Object *o : objects)
            if (!(o->kind == Kind::Thread ? (Acquire(*o), o->signaled) : (o->kind == Kind::Event ? o->signaled : true)))
                return WaitResult(a[3], n);
        for (Object *o : objects) Acquire(*o);
        return WAIT_OBJECT_0;
    }
    for (uint32_t i = 0; i < n; ++i)
        if (Acquire(*objects[i])) return WAIT_OBJECT_0 + i;
    return WaitResult(a[3], n);
}
// Clock.
inline uint32_t Milliseconds(Fake &, const uint32_t *)
{
    const uint32_t now = uint32_t(gState.ms);
    gState.ms += gSettings.msStep;
    return now;
}
inline uint32_t QueryPerformanceCounter(Fake &, const uint32_t *a)
{
    std::memcpy(Ptr<void>(a[0]), &gState.perf, 8);
    gState.perf += gSettings.perfStep;
    return TRUE;
}
inline uint32_t QueryPerformanceFrequency(Fake &, const uint32_t *a)
{
    std::memcpy(Ptr<void>(a[0]), &gSettings.perfFrequency, 8);
    return TRUE;
}
// Libraries.
inline uint32_t LoadLibraryA(Fake &, const uint32_t *a)
{
    const char *name = Ptr<const char>(a[0]);
    if (!name) return 0;
    for (Object &o : gState.objects)
        if (o.kind == Kind::Module && o.open && _stricmp(o.name.c_str(), name) == 0) {
            ++o.count;
            return o.handle;
        }
    for (const std::string &m : gSettings.modules)
        if (_stricmp(m.c_str(), name) == 0) {
            Object &o = NewObject(Kind::Module);
            o.name = m;
            o.count = 1;
            return o.handle;
        }
    return 0;
}
inline uint32_t FreeLibrary(Fake &, const uint32_t *a)
{
    Object *o = Find(a[0]);
    if (!o || o->kind != Kind::Module) return FALSE;
    if (--o->count == 0) o->open = false;
    return TRUE;
}
inline uint32_t GetProcAddress(Fake &, const uint32_t *a)
{
    Object *o = Find(a[0]);
    if (!o || o->kind != Kind::Module || a[1] < 0x10000) return 0;
    for (const ExportDef &e : gSettings.exports)
        if (_stricmp(e.module.c_str(), o->name.c_str()) == 0 && e.name == Ptr<const char>(a[1]))
            return uint32_t(uintptr_t(e.thunk));
    return 0;
}
} // namespace on

// One faked import (or export of a fake module): its slot in the "import" interface.
struct Import {
    unsigned slot = 0; // 0: not faked (the image does not import it)
    uint32_t rva = 0;  // its IAT slot; 0 for exports and CRT stand-ins
    void *thunk = nullptr;
    Import &Returns(uint32_t value)
    {
        if (slot) gFake.ret[slot] = value;
        return *this;
    }
    Import &On(drawfx::Handler handler)
    {
        if (slot) gFake.handler[slot] = handler;
        return *this;
    }
    explicit operator bool() const { return slot != 0; }
};

// The patches of one test (or case): every IAT slot it fakes points at its thunk until it goes.
class Imports {
public:
    explicit Imports(difftest::Context &ctx) : ctx_(ctx)
    {
        gFake.vt = nullptr;
        gFake.id = kId;
        gFake.iface = &gIface;
        gFake.refs = 0;
        gFake.owner = nullptr;
        gFake.faceCount = 0;
        for (unsigned i = 0; i < drawfx::kMaxSlots; ++i) {
            gFake.ret[i] = 0;
            gFake.handler[i] = nullptr;
            gFake.settledOverride[i] = true;
            gFake.table[i] = nullptr;
        }
        for (drawfx::Slot &s : gSlots) s = {0, drawfx::Ret::Int, true, "", ""};
        gSettings = Settings{};
        gState = State{};
    }
    ~Imports()
    {
        crt_.clear();
        for (auto it = patched_.rbegin(); it != patched_.rend(); ++it) *GameAddress<void *>(it->first) = it->second;
        gSettings = Settings{};
        gState = State{};
    }
    Imports(const Imports &) = delete;
    Imports &operator=(const Imports &) = delete;

    // Fakes the import `name` (of `dll` when given) with `words` stack words: every call logs, then answers the
    // programmed value (Returns, default 0) or runs the handler (On). Fails the test when the image does not import it.
    Import &Add(const char *name, unsigned words, const char *kinds = "", Conv conv = Conv::Std, const char *dll = nullptr)
    {
        const uint32_t rva = FindImport(name, dll);
        if (!rva) {
            ctx_.Fail("impfx: the image does not import %s", name);
            return none_;
        }
        for (Import &i : imports_)
            if (i.rva == rva) return i;
        Import &i = NewSlot(name, words, kinds, conv);
        if (!i) return i;
        i.rva = rva;
        void **slot = GameAddress<void *>(rva);
        patched_.push_back({rva, *slot});
        *slot = i.thunk;
        return i;
    }
    template <class Fn> Import &Add(const char *name, const char *kinds = "", const char *dll = nullptr)
    {
        return Add(name, detail::Arity<Fn>::words, kinds, detail::Arity<Fn>::conv, dll);
    }
    // The import added under this name (a null Import if none).
    Import &Get(const char *name)
    {
        for (Import &i : imports_)
            if (std::strcmp(gSlots[i.slot].name, name) == 0) return i;
        return none_;
    }
    // Only when the image imports it (the kinds use this: a missing import is simply not faked).
    Import &AddIfImported(const char *name, unsigned words, const char *kinds = "")
    {
        return FindImport(name) ? Add(name, words, kinds) : none_;
    }

    // ---- kinds ----
    void Threads()
    {
        AddIfImported("CreateThread", 6).On(on::CreateThread);
        AddIfImported("ResumeThread", 1).On(on::ResumeThread);
        AddIfImported("SuspendThread", 1).On(on::SuspendThread);
        AddIfImported("SetThreadPriority", 2).On(on::SetThreadPriority);
        AddIfImported("GetThreadPriority", 1).On(on::GetThreadPriority);
        AddIfImported("GetExitCodeThread", 2).On(on::GetExitCodeThread);
        AddIfImported("QueueUserAPC", 3).On(on::QueueUserAPC);
        AddIfImported("GetCurrentThreadId", 0).On(on::GetCurrentThreadId);
        AddIfImported("Sleep", 1).On(on::Sleep);
        AddIfImported("SleepEx", 2).On(on::SleepEx);
    }
    // The CRT's thread start and end, stood in on both sides (the image's library code and ours).
    void CrtThreads()
    {
        Import &begin = NewSlot("_beginthreadex", 6, "", Conv::Cdecl);
        Import &end = NewSlot("_endthreadex", 1, "", Conv::Cdecl);
        if (!begin || !end) return;
        begin.On(on::CreateThread);
        end.On(on::EndThread);
        crt_.push_back(std::make_unique<shape::StandIn>(kBeginThreadEx, &::_beginthreadex, begin.thunk));
        crt_.push_back(std::make_unique<shape::StandIn>(kEndThreadEx, &::_endthreadex, end.thunk));
    }
    void Sync()
    {
        AddIfImported("CreateEventA", 4, "iiis").On(on::CreateEventA);
        AddIfImported("SetEvent", 1).On(on::SetEvent);
        AddIfImported("ResetEvent", 1).On(on::ResetEvent);
        AddIfImported("PulseEvent", 1).On(on::PulseEvent);
        AddIfImported("CreateMutexA", 3, "iis").On(on::CreateMutexA);
        AddIfImported("ReleaseMutex", 1).On(on::ReleaseMutex);
        AddIfImported("CreateSemaphoreA", 4, "iiis").On(on::CreateSemaphoreA);
        AddIfImported("ReleaseSemaphore", 3).On(on::ReleaseSemaphore);
        AddIfImported("WaitForSingleObject", 2).On(on::WaitForSingleObject);
        AddIfImported("WaitForSingleObjectEx", 3).On(on::WaitForSingleObjectEx);
        AddIfImported("WaitForMultipleObjects", 4, "i@0x1").On(on::WaitForMultipleObjects);
        AddIfImported("CloseHandle", 1).On(on::CloseHandle);
    }
    void Clock()
    {
        AddIfImported("timeGetTime", 0).On(on::Milliseconds);
        AddIfImported("GetTickCount", 0).On(on::Milliseconds);
        AddIfImported("QueryPerformanceCounter", 1).On(on::QueryPerformanceCounter);
        AddIfImported("QueryPerformanceFrequency", 1).On(on::QueryPerformanceFrequency);
    }
    void Libraries()
    {
        AddIfImported("LoadLibraryA", 1, "s").On(on::LoadLibraryA);
        AddIfImported("FreeLibrary", 1).On(on::FreeLibrary);
        AddIfImported("GetProcAddress", 2, "is").On(on::GetProcAddress);
    }
    // A fake module LoadLibraryA finds, and one of its exports (a recording fake GetProcAddress hands out).
    void Module(const char *name) { gSettings.modules.emplace_back(name); }
    Import &Export(const char *module, const char *name, unsigned words, Conv conv = Conv::Std, const char *kinds = "")
    {
        Import &i = NewSlot(name, words, kinds, conv);
        if (i) gSettings.exports.push_back({module, name, i.slot, i.thunk});
        return i;
    }

    // ---- settings (kept across sides) ----
    void RunThreads(Run run) { gSettings.run = run; }
    void ScriptWaits(std::vector<uint32_t> results) { gSettings.waits = std::move(results); }
    Settings &settings() { return gSettings; }

    // Resets the kinds' state for one side: no objects, handle and id counters back to their start, the clock and
    // the wait script as set. Call it first in each side's build.
    void BeginSide()
    {
        gState = State{};
        gState.ms = gSettings.clockMs;
        gState.perf = gSettings.perfCount;
        gState.waits.assign(gSettings.waits.begin(), gSettings.waits.end());
    }
    State &state() { return gState; }

    static constexpr uint32_t kBeginThreadEx = 0x83a8bc, kEndThreadEx = 0x83a93f;

private:
    Import &NewSlot(const char *name, unsigned words, const char *kinds, Conv conv)
    {
        if (imports_.size() == kMaxImports || words > kMaxWords) {
            ctx_.Fail("impfx: no slot for %s (%u imports, %u words)", name, unsigned(imports_.size()), words);
            return none_;
        }
        const unsigned slot = kFirstSlot + unsigned(imports_.size());
        names_.emplace_back(name);
        kinds_.emplace_back(kinds);
        gSlots[slot] = {uint8_t(words), drawfx::Ret::Int, true, names_.back().c_str(), kinds_.back().c_str()};
        Import &i = imports_.emplace_back();
        i.slot = slot;
        i.thunk = detail::Thunk(words, slot, conv);
        return i;
    }

    difftest::Context &ctx_;
    std::deque<Import> imports_;
    std::deque<std::string> names_, kinds_;
    std::vector<std::pair<uint32_t, void *>> patched_;
    std::vector<std::unique_ptr<shape::StandIn>> crt_;
    Import none_;
};

} // namespace impfx

// Fakes an import by its SDK name; the argument count comes from the prototype: IMPFX_ADD(imp, SetCursor).Returns(0);
#define IMPFX_ADD(imports, api, ...) (imports).Add<decltype(::api)>(#api, ##__VA_ARGS__)
