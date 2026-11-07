#pragma once
// Recording fakes for the AppDebugger tests (tests/app_debugger_*.cpp).
//
// A fake object is a FakeObject: a vtable pointer and a kind. Every slot a kind defines is a trampoline that
// logs "<kind>.<slot>(args)" into gLog and runs the kind's handler for the slot (default: return 0). The same
// trampolines can be patched over the slots of the game's own vtables (the console's, the adapters'), so the
// calls the setup code makes on objects it creates itself are logged on both sides alike; the kind of such an
// object is found from its vtable.
//
// Pointer arguments are logged by what they stand for, not their value: a c string or cRZString as its text,
// a fake as its kind, any other object as "new:<vtable>#<n>" (n counts the distinct objects seen so far, so
// both sides number them alike).
#include "difftest.h"
#include "rz/cRZString.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace adt {

enum Kind {
    kFramework,
    kCommandLine,
    kLogService,
    kCommonImpl,
    kAllocator,
    kConsole,     // the game's cTSWinCheatConsole vtable, patched
    kAdapter,     // the game's cRZStdAdapter vtable, patched
    kConsoleFace, // an object the console's QueryInterface hands out
    kConsoleService,
    kStreamBase,  // the base of the stream-like cheat file object
    kLock,        // a fake cRZCriticalSection vtable
    kOther,
    kKindCount
};
constexpr const char *kKindNames[kKindCount] = {"fw", "cmd", "log", "impl", "alloc", "console", "adapter", "face", "svc", "stream", "lock", "other"};
constexpr int kMaxSlots = 48;      // a Fake's vtable, and BuildVTable's default size
constexpr int kSlotCapacity = 128; // slots Def and the trampolines cover: BuildVTable<N> for vtables longer than 48

struct FakeObject {
    const void *vtable;
    int kind;
    uint32_t pad[2];
};

using Handler = std::function<uint32_t(void *self, const uint32_t *args)>;
struct SlotDef {
    int arity = -1;           // stack arguments (the callee pops them); -1: slot not defined
    const char *kinds = "";   // one letter per argument: i int, s c string, S cIGZString *, p object, o out pointer
    Handler fn;               // optional; the result is the slot's eax
};

inline SlotDef gSlots[kKindCount][kSlotCapacity];
inline std::vector<std::string> gLog;
inline std::map<const void *, int> gKnownVTables; // game vtable address -> kind
inline std::map<const void *, int> gFakes;        // fake object -> kind
inline std::map<const void *, std::string> gLabels; // fake object -> its own name (Fake(kind, label)), instead of the kind's
inline std::vector<const void *> gSeen;           // other objects, in order of first appearance

inline void ResetSide()
{
    gLog.clear();
    gSeen.clear();
}

inline int KindOf(const void *self)
{
    auto it = gFakes.find(self);
    if (it != gFakes.end()) return it->second;
    auto vt = gKnownVTables.find(*static_cast<void *const *>(self));
    return vt == gKnownVTables.end() ? kOther : vt->second;
}

inline std::string Hex(uint32_t v)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%x", v);
    return buf;
}

inline std::string Tag(const void *p)
{
    if (!p) return "null";
    auto it = gFakes.find(p);
    if (it != gFakes.end()) {
        auto label = gLabels.find(p);
        return "<" + (label != gLabels.end() ? label->second : std::string(kKindNames[it->second])) + ">";
    }
    size_t i = 0;
    while (i < gSeen.size() && gSeen[i] != p) ++i;
    if (i == gSeen.size()) gSeen.push_back(p);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "new:%x#%zu", *static_cast<const uint32_t *>(p), i);
    return buf;
}

inline std::string TagIndex(const void *p)
{
    if (!p) return "null";
    size_t i = 0;
    while (i < gSeen.size() && gSeen[i] != p) ++i;
    if (i == gSeen.size()) gSeen.push_back(p);
    return "blk#" + std::to_string(i);
}

inline std::string Describe(const cRZString *s)
{
    std::string seen = s->mVTable ? "vptr:" : "novptr:";
    seen += s->mString;
    seen += s->mString.capacity() == 15 ? "|sso" : "|big";
    return seen;
}

inline std::string FormatArgs(const SlotDef &def, const uint32_t *args)
{
    std::string out;
    for (int i = 0; i < def.arity; ++i) {
        if (i) out += ", ";
        switch (def.kinds[i]) {
        case 's': out += args[i] ? std::string("\"") + reinterpret_cast<const char *>(args[i]) + "\"" : "null"; break;
        case 'S': out += args[i] ? "S:" + Describe(reinterpret_cast<const cRZString *>(args[i])) : "null"; break;
        case 'p': out += Tag(reinterpret_cast<const void *>(args[i])); break;
        case 'q': out += TagIndex(reinterpret_cast<const void *>(args[i])); break; // a block: its number only
        case 'o': out += "out"; break;
        default: out += Hex(args[i]); break;
        }
    }
    return out;
}

inline uint32_t Dispatch(void *self, int slot, const uint32_t *args)
{
    int kind = KindOf(self);
    const SlotDef &def = gSlots[kind][slot];
    std::string line = std::string(kind == kOther ? "other" : kKindNames[kind]) + "." + std::to_string(slot) + "(";
    if (kind == kConsole || kind == kAdapter || kind == kOther) line += Tag(self) + (def.arity > 0 ? ", " : "");
    line += FormatArgs(def, args) + ")";
    gLog.push_back(line);
    return def.fn ? def.fn(self, args) : 0;
}

template <int A, int S> struct Tramp;
template <int S> struct Tramp<0, S> {
    static uint32_t __fastcall Fn(void *self, void *) { return Dispatch(self, S, nullptr); }
};
template <int S> struct Tramp<1, S> {
    static uint32_t __fastcall Fn(void *self, void *, uint32_t a) { uint32_t v[1] = {a}; return Dispatch(self, S, v); }
};
template <int S> struct Tramp<2, S> {
    static uint32_t __fastcall Fn(void *self, void *, uint32_t a, uint32_t b) { uint32_t v[2] = {a, b}; return Dispatch(self, S, v); }
};
template <int S> struct Tramp<3, S> {
    static uint32_t __fastcall Fn(void *self, void *, uint32_t a, uint32_t b, uint32_t c) { uint32_t v[3] = {a, b, c}; return Dispatch(self, S, v); }
};
template <int S> struct Tramp<4, S> {
    static uint32_t __fastcall Fn(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d) { uint32_t v[4] = {a, b, c, d}; return Dispatch(self, S, v); }
};
template <int S> struct Tramp<5, S> {
    static uint32_t __fastcall Fn(void *self, void *, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) { uint32_t v[5] = {a, b, c, d, e}; return Dispatch(self, S, v); }
};

template <int A, size_t... S> constexpr std::array<void *, sizeof...(S)> MakeRow(std::index_sequence<S...>)
{
    return {reinterpret_cast<void *>(&Tramp<A, static_cast<int>(S)>::Fn)...};
}

inline void *Trampoline(int arity, int slot)
{
    static const auto rows = std::array<std::array<void *, kSlotCapacity>, 6>{
        MakeRow<0>(std::make_index_sequence<kSlotCapacity>()), MakeRow<1>(std::make_index_sequence<kSlotCapacity>()), MakeRow<2>(std::make_index_sequence<kSlotCapacity>()),
        MakeRow<3>(std::make_index_sequence<kSlotCapacity>()), MakeRow<4>(std::make_index_sequence<kSlotCapacity>()), MakeRow<5>(std::make_index_sequence<kSlotCapacity>())};
    return rows[static_cast<size_t>(arity)][static_cast<size_t>(slot)];
}

// Defines a slot of a kind.
inline void Def(int kind, int slot, int arity, const char *kinds, Handler fn = nullptr)
{
    SlotDef &d = gSlots[kind][slot];
    d.arity = arity;
    d.kinds = kinds;
    d.fn = std::move(fn);
}

// A vtable for a kind with all its defined slots (kept alive by the caller's static). N > 48 (up to kSlotCapacity)
// for a longer vtable: BuildVTable<64>(kConsole) reaches slot 63.
template <size_t N = kMaxSlots> inline std::array<void *, N> BuildVTable(int kind)
{
    static_assert(N <= kSlotCapacity, "raise kSlotCapacity");
    std::array<void *, N> vt{};
    for (int s = 0; s < static_cast<int>(N); ++s)
        if (gSlots[kind][s].arity >= 0) vt[static_cast<size_t>(s)] = Trampoline(gSlots[kind][s].arity, s);
    return vt;
}

// A fake object of a kind with its own vtable. A label names this object in the log instead of its kind, so several
// fakes of one kind can be told apart: Fake(kOther, "seg1") logs as <seg1> (calls on a kOther fake as "other.N(<seg1>)").
struct Fake {
    FakeObject object;
    std::array<void *, kMaxSlots> vtable;
    explicit Fake(int kind, const char *label = nullptr)
    {
        vtable = BuildVTable(kind);
        object.vtable = vtable.data();
        object.kind = kind;
        object.pad[0] = object.pad[1] = 0;
        gFakes[&object] = kind;
        if (label) gLabels[&object] = label;
    }
    ~Fake()
    {
        gFakes.erase(&object);
        gLabels.erase(&object);
    }
    Fake(const Fake &) = delete;
    void *get() { return &object; }
};

// Patches slots of a vtable in the mapped image for the lifetime of the object, and registers the vtable's
// kind. `slots` lists the slots to patch.
class PatchedVTable {
public:
    PatchedVTable(difftest::Context &ctx, uint32_t rva, int kind, std::initializer_list<int> slots) : mCtx(ctx), mRva(rva), mKind(kind)
    {
        void **vt = ctx.image.At<void *>(rva);
        for (int s : slots) {
            mSaved.emplace_back(s, vt[s]);
            vt[s] = Trampoline(gSlots[kind][s].arity, s);
        }
        gKnownVTables[vt] = kind;
    }
    ~PatchedVTable()
    {
        void **vt = mCtx.image.At<void *>(mRva);
        for (auto &p : mSaved) vt[p.first] = p.second;
        gKnownVTables.erase(vt);
    }
    PatchedVTable(const PatchedVTable &) = delete;

private:
    difftest::Context &mCtx;
    uint32_t mRva;
    int mKind;
    std::vector<std::pair<int, void *>> mSaved;
};

// Saves a game global (by RVA) and restores it on destruction.
template <typename T> class GlobalOverride {
public:
    GlobalOverride(difftest::Context &ctx, uint32_t rva) : mAt(ctx.image.At<T>(rva)), mSaved(*mAt) {}
    GlobalOverride(difftest::Context &ctx, uint32_t rva, T value) : GlobalOverride(ctx, rva) { *mAt = value; }
    ~GlobalOverride() { *mAt = mSaved; }
    GlobalOverride(const GlobalOverride &) = delete;
    T &operator*() { return *mAt; }
    void Set(T value) { *mAt = value; }

private:
    T *mAt;
    T mSaved;
};

// The allocator service the list code reaches through its function-local static (cache 0x10c95d4, guard
// 0x10c95d8). Slot 3 allocates (the size is logged), slot 4 frees: the free is only recorded, so the test can
// look at the block afterwards; everything is released when the fake is destroyed.
class AllocatorFake {
public:
    static constexpr uint32_t kCacheRva = 0x10c95d4, kGuardRva = 0x10c95d8;

    explicit AllocatorFake(difftest::Context &ctx) : mCache(ctx, kCacheRva), mGuard(ctx, kGuardRva)
    {
        Def(kAllocator, 3, 1, "i", [this](void *, const uint32_t *args) {
            void *p = ::operator new(args[0]);
            std::memset(p, 0xcd, args[0]);
            mAll.insert(p);
            return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
        });
        // AddRef and Release, which GetAllocatorService's holder uses on the first use.
        Def(kAllocator, 1, 0, "");
        Def(kAllocator, 2, 0, "");
        Def(kAllocator, 4, 1, "q", [this](void *, const uint32_t *args) {
            mFreed.push_back(reinterpret_cast<void *>(args[0]));
            mAll.insert(reinterpret_cast<void *>(args[0]));
            return 0u;
        });
        mFake = std::make_unique<Fake>(kAllocator);
        Arm();
    }
    ~AllocatorFake()
    {
        for (int s = 1; s <= 4; ++s) gSlots[kAllocator][s] = SlotDef();
        for (void *p : mAll) ::operator delete(p);
    }
    // The cached pointer is the fake and the guard says initialised (as after the first use).
    void Arm()
    {
        mCache.Set(mFake->get());
        mGuard.Set(INT32_MIN);
    }
    // Nothing cached yet: the next use asks the framework (GetAllocatorService).
    void Disarm()
    {
        mCache.Set(nullptr);
        mGuard.Set(0);
    }
    void *get() { return mFake->get(); }
    // Forgets the blocks seen so far (after a side's run has been checked).
    void Release()
    {
        for (void *p : mAll) ::operator delete(p);
        mAll.clear();
        mFreed.clear();
    }
    void Track(void *p) { mAll.insert(p); }
    std::vector<void *> mFreed; // in order

private:
    GlobalOverride<void *> mCache;
    GlobalOverride<int32_t> mGuard;
    std::unique_ptr<Fake> mFake;
    std::set<void *> mAll;
};

// The game keeps its wide file functions in globals filled at start-up; the sandbox provides the same
// functions, so file code (PathExists) runs against the sandbox.
inline void InstallWideFunctions(difftest::Context &ctx)
{
    static const uint32_t kWideGlobals[] = {0x10cb5e0, 0x10cb5e4, 0x10cb5e8, 0x10cb5ec, 0x10cb5f0, 0x10cb5f4};
    static const char *const kWideNames[] = {"CreateFileW", "CopyFileW", "MoveFileExW", "FindFirstFileW", "DeleteFileW", "GetFileAttributesW"};
    for (int i = 0; i < 6; ++i) *ctx.image.At<void *>(kWideGlobals[i]) = difftest::SandboxFunction(kWideNames[i]);
}

inline std::string Joined(const std::vector<std::string> &log)
{
    std::string s;
    for (const std::string &l : log) s += l + "\n";
    return s;
}

// First differing line of two logs, for failure messages.
inline std::string FirstDifference(const std::vector<std::string> &a, const std::vector<std::string> &b)
{
    size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i)
        if (a[i] != b[i]) return "line " + std::to_string(i) + ": orig `" + a[i] + "` ours `" + b[i] + "`";
    return "lengths " + std::to_string(a.size()) + "/" + std::to_string(b.size()) + (a.size() > b.size() ? " (orig extra: `" + a[n] + "`)" : b.size() > a.size() ? " (ours extra: `" + b[n] + "`)" : "");
}

} // namespace adt
