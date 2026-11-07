// Differential tests for gz::cGZApp in the standalone shape (src/gz/cGZApp.*, lane EV): the application base. Our side:
// objects built by our constructor (the constructor hook's adapter), every slot called through the object's own
// vtables. Original side: objects built by the original constructor 0xc59f5a, slots called through the game's vtables
// 0xe9d118 / 0xe9d150 / 0xe9d16c. The objects are compared whole: every byte except the three vtable words, which are
// compared as codes ("the side's own cGZApp vtable", "the side's own rz base vtable", other), and the name's buffer
// pointer (a heap address: its text, size and capacity are compared instead).
//
// The compiler's adjustor thunks are exercised by GZApp_Slots through the +4 and +0x1c vtables: QueryInterface 0xc5a01e
// (+4) and 0xc5a026 (+0x1c), Release 0xc5a02e (+4) and 0xc5a036 (+0x1c), the deleting destructor 0xc59fbf (+4 slot 3,
// into 0xc59fc7), and the folded AddRef thunks 0x1cc325 / 0x1cc32d.
#define NOMINMAX
#include "app_debugger_fakes.h"
#include "difftest.h"
#include "game/game.h"
#include "gz/cGZApp.h"
#include "rz/RZSystemServiceBase.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <string>

namespace {

using gz::cGZApp;
using Adapters = gz::cGZAppAdapters;

constexpr uint32_t kCtor = 0xc59f5a, kDtor = 0xc59f91, kDeleting = 0xc59fc7, kDeletingThunk = 0xc59fbf;
constexpr uint32_t kQueryInterface = 0xc59ff7, kAddApplicationService = 0xc59fe6, kFramework = 0xc59ff2;
constexpr uint32_t kAsSystemService = 0xa28af0;
constexpr uint32_t kThunks[] = {0xc5a01e, 0xc5a026, 0xc5a02e, 0xc5a036, kDeletingThunk, 0x1cc325, 0x1cc32d};
constexpr uint32_t kGameVt0 = 0xe9d118, kGameVt4 = 0xe9d150, kGameVt1c = 0xe9d16c;
constexpr uint32_t kGameBaseVt0 = 0xda3058, kGameBaseVt4 = 0xda3090, kGameUnknownVt = 0xda2fec;
constexpr uint32_t kGameSizedDelete = 0x824624, kFrameworkGlobal = 0x10c9378;
constexpr uint32_t kIdApp = 0x66;

std::string Fmt(const char *fmt, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    return buffer;
}

template <typename R, typename... Args> R CallSlot(void *face, int slot, Args... args)
{
    auto fn = reinterpret_cast<R(__thiscall *)(void *, Args...)>((*reinterpret_cast<void *const *const *>(face))[slot]);
    return fn(face, args...);
}

// Our vtables: cGZApp's three, rz::cRZSystemService's two, rz::cRZUnknownMT's one (what our destructors store back).
struct OurVTables {
    const void *app[3], *base[2], *unknown;
};
const OurVTables &Ours()
{
    static OurVTables v = [] {
        OurVTables r{};
        alignas(8) static uint8_t memory[0x40];
        auto *app = Adapters::Construct(memory, nullptr, 1, "x");
        r.app[0] = *reinterpret_cast<void **>(memory);
        r.app[1] = *reinterpret_cast<void **>(memory + 4);
        r.app[2] = *reinterpret_cast<void **>(memory + 0x1c);
        Adapters::Destruct(app, nullptr);
        auto *base = rz::cRZSystemServiceAdapters::Construct(memory, nullptr, 1, 2);
        r.base[0] = *reinterpret_cast<void **>(memory);
        r.base[1] = *reinterpret_cast<void **>(memory + 4);
        rz::cRZSystemServiceAdapters::Destruct(base, nullptr);
        auto *unknown = rz::cRZUnknownMTAdapters::Construct(memory, nullptr);
        r.unknown = *reinterpret_cast<void **>(memory);
        rz::cRZUnknownMTAdapters::Destruct(unknown, nullptr);
        return r;
    }();
    return v;
}

// A vtable word as a code: the side's own vtable of that kind, or "other" (raw).
std::string VtCode(difftest::Context &ctx, bool original, const void *word)
{
    const OurVTables &o = Ours();
    struct { uint32_t game; const void *ours; const char *name; } kinds[] = {
        {kGameVt0, o.app[0], "app0"}, {kGameVt4, o.app[1], "app4"}, {kGameVt1c, o.app[2], "app1c"},
        {kGameBaseVt0, o.base[0], "base0"}, {kGameBaseVt4, o.base[1], "base4"}, {kGameUnknownVt, o.unknown, "unknown"},
    };
    for (const auto &k : kinds)
        if (word == (original ? ctx.image.At(k.game) : k.ours)) return k.name;
    return Fmt("other(%p)", word);
}

// The object: vtable words as codes, the service fields raw, the name as its cRZString vtable word, text, size and
// capacity (the buffer pointer only as inline/heap).
std::string Dump(difftest::Context &ctx, bool original, const uint8_t *m)
{
    auto u32 = [&](int off) { return *reinterpret_cast<const uint32_t *>(m + off); };
    std::string s = VtCode(ctx, original, *reinterpret_cast<void *const *>(m)) + " " + VtCode(ctx, original, *reinterpret_cast<void *const *>(m + 4)) +
                    " " + VtCode(ctx, original, *reinterpret_cast<void *const *>(m + 0x1c));
    s += Fmt(" count %x id %x prio %x tick %x running %02x pad %02x%02x%02x", u32(8), u32(0xc), u32(0x10), u32(0x14), m[0x18], m[0x19], m[0x1a], m[0x1b]);
    uint32_t size = u32(0x34), cap = u32(0x38);
    const char *text = cap > 15 ? *reinterpret_cast<const char *const *>(m + 0x24) : reinterpret_cast<const char *>(m + 0x24);
    s += Fmt(" name vt %s size %u cap %u %s '%s'", *reinterpret_cast<void *const *>(m + 0x20) == ctx.image.At(cRZString::kVTable) ? "rzstring" : "other",
             size, cap, cap > 15 ? "heap" : "inline", std::string(text, size < 200 ? size : 200).c_str());
    if (cap <= 15) s += Fmt(" sso %s", std::string(reinterpret_cast<const char *>(m + 0x24), 16).c_str() == std::string(text, 16) ? "" : "?");
    s += Fmt(" w3c %x", u32(0x3c));
    return s;
}

std::string RandomName(std::mt19937 &rng)
{
    static const char *fixed[] = {"", "The Sims 2", "Sims2", "123456789012345", "1234567890123456", "A much longer application name for the heap"};
    if (rng() % 3) return fixed[rng() % 6];
    std::string s(rng() % 40, 'a');
    for (char &c : s) c = char(0x20 + rng() % 95);
    return s;
}

// ---- frees through either sized delete, recorded

// The code stays writable after the restore: putting back the old protection VirtualProtect reported (the first page's)
// narrows both pages when the five bytes straddle a page boundary, and an earlier redirect on the second page then
// faults when it restores its bytes (lane MW, tests/app_fixture.h).
struct Redirect {
    uint8_t *site; uint8_t saved[5];
    Redirect(void *entry, void *target) : site(static_cast<uint8_t *>(entry))
    {
        std::memcpy(saved, site, 5);
        DWORD old; VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t relative = int32_t(reinterpret_cast<uintptr_t>(target) - reinterpret_cast<uintptr_t>(site) - 5);
        site[0] = 0xe9; std::memcpy(site + 1, &relative, 4);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    ~Redirect()
    {
        DWORD old; VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        std::memcpy(site, saved, 5);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    Redirect(const Redirect &) = delete;
};

char gFreeLog[2048];
size_t gFreeLength = 0;
const void *gWatched = nullptr;
void __cdecl RecordingFree(void *p, size_t size)
{
    if (gFreeLength < sizeof(gFreeLog) - 32)
        gFreeLength += size_t(std::snprintf(gFreeLog + gFreeLength, sizeof(gFreeLog) - gFreeLength, "%s:%x ", p == gWatched ? "object" : "block", unsigned(size)));
    HeapFree(GetProcessHeap(), 0, p);
}
std::string Frees() { return std::string(gFreeLog, gFreeLength); }

struct FreeWatch {
    Redirect game, ours;
    explicit FreeWatch(difftest::Context &ctx)
        : game(ctx.image.At(kGameSizedDelete), reinterpret_cast<void *>(&RecordingFree)),
          ours(reinterpret_cast<void *>(static_cast<void(__cdecl *)(void *, size_t)>(&::operator delete)), reinterpret_cast<void *>(&RecordingFree))
    {
        gFreeLength = 0;
    }
};

// ---- a fake framework for g_Framework (slot 3 AddSystemService records the service, answers a word with junk above al)

std::string gLog;
uint32_t gAnswer = 0;
const uint8_t *gObject = nullptr;
uint32_t __fastcall FwAddSystemService(void *, void *, void *service)
{
    gLog += Fmt("fw.add(%d) ", int(static_cast<const uint8_t *>(service) - gObject));
    return gAnswer;
}
bool gTrapped = false;
uint32_t __fastcall Trap0(void *, void *) { gTrapped = true; return 0; }
uint32_t __fastcall Trap1(void *, void *, uint32_t) { gTrapped = true; return 0; }
uint32_t __fastcall Trap2(void *, void *, uint32_t, uint32_t) { gTrapped = true; return 0; }

struct FakeFramework {
    const void *vt[64];
    const void *const *self;
    FakeFramework()
    {
        for (auto &s : vt) s = reinterpret_cast<const void *>(&Trap0);
        vt[3] = reinterpret_cast<const void *>(&FwAddSystemService);
        self = vt;
    }
};

uint8_t *Build(difftest::Context &ctx, bool original, uint32_t id, const char *name)
{
    auto *mem = static_cast<uint8_t *>(::operator new(0x40));
    std::memset(mem, 0xa5, 0x40);
    void *r = original ? ctx.Original<void *(__thiscall *)(void *, uint32_t, const char *)>(kCtor)(mem, id, name)
                       : static_cast<void *>(Adapters::Construct(mem, nullptr, id, name));
    if (r != mem) ctx.Fail("constructor returned %p, not %p", r, static_cast<void *>(mem));
    return mem;
}

void Free(difftest::Context &ctx, bool original, uint8_t *mem)
{
    if (original) ctx.Original<void(__thiscall *)(void *)>(kDtor)(mem);
    else CallSlot<void *>(mem + 4, 3, 0u);
    ::operator delete(mem);
}

} // namespace

// ---- 0xc59f5a (constructor), 0xc59f91 (destructor body), 0xc59fc7 (deleting destructor, through the +4 face's slot 3
// thunk 0xc59fbf on each side, and our adapter on game-built objects)

DIFF_TEST(GZApp_ConstructDestruct, kCtor)
{
    std::mt19937 rng(kCtor);
    for (unsigned i = 0; i < 300; ++i) {
        uint32_t id = rng() % 3 ? static_cast<uint32_t>(rng()) : 0x10c9380;
        std::string name = RandomName(rng);
        uint32_t flags = i % 4 == 3 ? 1u : (i % 4 == 2 ? 0xfffffffeu : (i % 2 ? 0x80000001u : 0u));
        int how = i % 3; // 0: destructor body, 1: deleting destructor through the +4 slot 3, 2: the adapters on a game-built object
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            bool original = side == 0;
            bool viaAdapter = !original && how == 2;
            uint8_t *mem = Build(ctx, original || viaAdapter, id, name.c_str());
            results[side] = viaAdapter ? Dump(ctx, true, mem) : Dump(ctx, original, mem);
            gWatched = mem;
            void *ret = mem;
            {
                FreeWatch watch(ctx);
                if (how == 0) {
                    if (original) ctx.Original<void(__thiscall *)(void *)>(kDtor)(mem);
                    else Adapters::Destruct(reinterpret_cast<cGZApp *>(mem), nullptr);
                } else if (how == 1 || original) {
                    void *r = CallSlot<void *>(mem + 4, 3, flags);
                    ret = r;
                } else {
                    ret = Adapters::DeletingDestructor(reinterpret_cast<cGZApp *>(mem), nullptr, flags);
                }
            }
            bool freed = how != 0 && (flags & 1);
            results[side] += Fmt(" | ret %d frees %s", ret == mem, Frees().c_str());
            if (!freed) {
                // After the destructor: the vtable words the base destructors stored (ours: our rz bases'; an adapter
                // on a game-built object stores ours too, so it is compared as ours).
                results[side] += " | after: " + Dump(ctx, original, mem).substr(0, 60);
                ::operator delete(mem);
            }
        }
        ++ctx.cases;
        if (results[0] != results[1]) ctx.Fail("case %u (how %d, flags %x):\n  original: %s\n  ours:     %s", i, how, flags, results[0].c_str(), results[1].c_str());
    }
    (void)kDeleting;
}

// ---- every slot of the three faces, through each side's own vtables (the thunks above included)

DIFF_TEST(GZApp_Slots, kQueryInterface)
{
    FakeFramework framework;
    adt::GlobalOverride<uint32_t> global(ctx, kFrameworkGlobal, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&framework.self)));
    std::mt19937 rng(kQueryInterface);
    static const uint32_t iids[] = {kIdApp, 1, 0x287fb697, 0, 0x67, 0x452294aa};
    struct SlotCase { int face; int slot; int args; const char *name; };
    static const SlotCase cases[] = {
        {0, 0, 2, "QI"}, {0, 1, 0, "AddRef"}, {0, 2, 0, "Release"}, {0, 3, 0, "GetServiceID"}, {0, 4, 1, "SetServiceID"},
        {0, 5, 0, "GetServicePriority"}, {0, 6, 0, "IsServiceRunning"}, {0, 7, 1, "SetServiceRunning"}, {0, 8, 0, "Init"},
        {0, 9, 0, "Shutdown"}, {0, 10, 1, "OnTick"}, {0, 11, 1, "OnIdle"}, {0, 12, 0, "GetServiceTickPriority"},
        {4, 0, 2, "QI+4"}, {4, 1, 0, "AddRef+4"}, {4, 2, 0, "Release+4"}, {4, 4, 0, "RemoveRef"}, {4, 5, 0, "RefCount"},
        {0x1c, 0, 2, "QI+1c"}, {0x1c, 1, 0, "AddRef+1c"}, {0x1c, 2, 0, "Release+1c"}, {0x1c, 3, 0, "AsIGZSystemService"},
        {0x1c, 4, 1, "AddApplicationService"}, {0x1c, 5, 0, "ModuleName"}, {0x1c, 6, 0, "PreFrameworkInit"},
        {0x1c, 7, 0, "PostFrameworkInit"}, {0x1c, 8, 0, "GZRun"}, {0x1c, 9, 0, "PreFrameworkShutdown"}, {0x1c, 10, 0, "Framework"},
        {0x1c, 11, 0, "AddDynamicLibrariesHere"}, {0x1c, 12, 0, "AddCOMDirectorsHere"}, {0x1c, 13, 0, "AddApplicationServicesHere"},
        {0x1c, 14, 0, "LoadRegistry"},
    };
    for (unsigned i = 0; i < 1200; ++i) {
        const SlotCase &c = cases[i % (sizeof(cases) / sizeof(cases[0]))];
        uint32_t id = static_cast<uint32_t>(rng());
        std::string name = RandomName(rng);
        uint32_t count = rng() % 4, arg = rng() % 3 ? static_cast<uint32_t>(rng()) : (rng() % 2 ? 0x80u : 1u);
        uint32_t iid = rng() % 4 ? iids[rng() % 6] : static_cast<uint32_t>(rng());
        bool nullOut = c.args == 2 && i % 7 == 0 && iid != kIdApp && iid != 1 && iid != 0x287fb697;
        gAnswer = rng() % 2 ? (static_cast<uint32_t>(rng()) & ~0xffu) | 1 : (static_cast<uint32_t>(rng()) & ~0xffu);
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            bool original = side == 0;
            uint8_t *mem = Build(ctx, original, id, name.c_str());
            for (uint32_t k = 0; k < count; ++k) CallSlot<uint32_t>(mem, 1);
            uint8_t *face = mem + c.face;
            gObject = mem;
            gLog.clear();
            gTrapped = false;
            void *out = reinterpret_cast<void *>(0x5eed);
            std::string extra;
            uint32_t r = 0;
            if (c.args == 2) {
                r = CallSlot<uint32_t>(face, c.slot, iid, nullOut ? static_cast<void **>(nullptr) : &out) & 0xff;
                extra = out == reinterpret_cast<void *>(0x5eed) ? "untouched" : Fmt("+%d", int(static_cast<uint8_t *>(out) - mem));
            } else if (c.args == 1) {
                r = CallSlot<uint32_t>(face, c.slot, c.name[0] == 'A' ? reinterpret_cast<uintptr_t>(mem + 0x10) : arg);
            } else {
                r = CallSlot<uint32_t>(face, c.slot);
            }
            // Results: the bool slots compare al only (bool returns); pointer results as offsets or names; slots 11 and 12
            // of the cIGZApp face leave al undefined in the game (0x140fe0 is a bare `ret`): not compared.
            if (c.face == 0x1c && (c.slot == 11 || c.slot == 12)) r = 0;
            else if (c.face == 0x1c && c.slot == 3) r = uint32_t(reinterpret_cast<uint8_t *>(uintptr_t(r)) - mem);
            else if (c.face == 0x1c && c.slot == 5) {
                const char *p = reinterpret_cast<const char *>(uintptr_t(r));
                extra = Fmt("%s '%s'", p == reinterpret_cast<const char *>(mem + 0x24) ? "inline" : "heap", p);
                r = 0;
            } else if (c.face == 0x1c && c.slot == 10) r = r == uint32_t(reinterpret_cast<uintptr_t>(&framework.self)) ? 1 : 2;
            else if (c.face == 0x1c && c.slot == 4) {} // the framework's whole eax passes through
            else if (c.face == 0 && (c.slot == 3 || c.slot == 5 || c.slot == 12)) {}
            else if (c.face == 4 && (c.slot == 4 || c.slot == 5)) {}
            else if (c.slot == 1 || c.slot == 2) {}
            else r &= 0xff;
            results[side] = Fmt("%x %s %s%s | ", r, extra.c_str(), gLog.c_str(), gTrapped ? "TRAP" : "") + Dump(ctx, original, mem);
            Free(ctx, original, mem);
        }
        ++ctx.cases;
        if (results[0] != results[1]) ctx.Fail("case %u (%s):\n  original: %s\n  ours:     %s", i, c.name, results[0].c_str(), results[1].c_str());
    }
    (void)kFramework; (void)kAsSystemService; (void)kThunks;
}

// ---- the adapters on objects built by the original constructor (game vtables), the adapter's own slot trapped in a copy
// of the game's vtable: an adapter that dispatched virtually would reach the trap (in the game: its own hook).

DIFF_TEST(GZApp_Adapters, kAddApplicationService)
{
    FakeFramework framework;
    adt::GlobalOverride<uint32_t> global(ctx, kFrameworkGlobal, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&framework.self)));
    std::mt19937 rng(kAddApplicationService);
    static const uint32_t iids[] = {kIdApp, 1, 0x287fb697, 0x67};
    for (unsigned i = 0; i < 400; ++i) {
        int kind = i % 2; // 0: QueryInterface (+0 slot 0), 1: AddApplicationService (+0x1c slot 4)
        std::string name = RandomName(rng);
        uint32_t iid = rng() % 4 ? iids[rng() % 4] : static_cast<uint32_t>(rng());
        gAnswer = static_cast<uint32_t>(rng());
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            uint8_t *mem = Build(ctx, true, 0x1234, name.c_str());
            gObject = mem;
            gLog.clear();
            gTrapped = false;
            const void *vt0[13], *vt1c[15];
            const void *own0 = *reinterpret_cast<const void **>(mem), *own1c = *reinterpret_cast<const void **>(mem + 0x1c);
            std::memcpy(vt0, own0, sizeof(vt0));
            std::memcpy(vt1c, own1c, sizeof(vt1c));
            if (side == 1) {
                vt0[0] = reinterpret_cast<const void *>(&Trap2);
                vt1c[4] = reinterpret_cast<const void *>(&Trap1);
            }
            *reinterpret_cast<const void **>(mem) = vt0;
            *reinterpret_cast<const void **>(mem + 0x1c) = vt1c;
            uint32_t r;
            std::string extra;
            if (kind == 0) {
                void *out = reinterpret_cast<void *>(0x5eed);
                r = (side ? Adapters::QueryInterface(reinterpret_cast<cGZApp *>(mem), nullptr, iid, &out)
                          : ctx.Original<uint8_t(__thiscall *)(void *, uint32_t, void **)>(kQueryInterface)(mem, iid, &out)) & 0xff;
                extra = out == reinterpret_cast<void *>(0x5eed) ? "untouched" : Fmt("+%d", int(static_cast<uint8_t *>(out) - mem));
            } else {
                auto *face = reinterpret_cast<cIGZApp *>(mem + 0x1c);
                r = side ? uint32_t(Adapters::AddApplicationService(face, nullptr, reinterpret_cast<cIGZSystemService *>(mem + 8))) & 0xff
                         : ctx.Original<uint32_t(__thiscall *)(void *, void *)>(kAddApplicationService)(face, mem + 8);
                if (side == 0) r &= 0xff; // our adapter's bool: al compared (the game's callers test al)
            }
            *reinterpret_cast<const void **>(mem) = own0;
            *reinterpret_cast<const void **>(mem + 0x1c) = own1c;
            results[side] = Fmt("%x %s %s%s | ", r, extra.c_str(), gLog.c_str(), gTrapped ? "TRAP" : "") + Dump(ctx, true, mem);
            Free(ctx, true, mem);
        }
        ++ctx.cases;
        if (results[0] != results[1]) ctx.Fail("case %u (kind %d):\n  original: %s\n  ours:     %s", i, kind, results[0].c_str(), results[1].c_str());
    }
}
