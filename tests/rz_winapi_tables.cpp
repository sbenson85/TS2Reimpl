// Differential tests for src/rz/WinApiTables.h (lane HQ): the start-up lookups of Windows entry points.
//
// The image's import table entries the code calls (GetVersion, GetModuleHandleA, LoadLibraryA, GetProcAddress,
// FreeLibrary) are bound to logging fakes for both sides (ours calls through the same entries). Per case: GetVersion's
// answer (NT, Windows 95, 98 / Me, edge values of the major and minor bytes), modules found or not, entry points found or
// not; the object or the loader's globals start as junk with 8-byte margins. Compared: the window after the call, the
// return value (the object), and the call log (each call's name, module and text, in order).
#include "difftest.h"
#include "game/game.h"
#include "rz/WinApiTables.h"

#include <windows.h>

#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace rz;

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

std::vector<std::string> gLog;
DWORD gVersion = 0;
uint32_t gCase = 0;
bool gNoModule = false;    // GetModuleHandleA / LoadLibraryA fail
uint32_t gMissingProc = 0; // GetProcAddress fails for names whose hash has this low bit pattern (0 = never)

uint32_t Hash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; ++s) h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
    return h;
}

DWORD WINAPI FakeGetVersion()
{
    gLog.push_back("GetVersion");
    return gVersion;
}
HMODULE WINAPI FakeGetModuleHandleA(const char *name)
{
    gLog.push_back(std::string("GetModuleHandleA ") + name);
    return gNoModule ? nullptr : reinterpret_cast<HMODULE>(static_cast<uintptr_t>((Hash(name) ^ gCase) | 1));
}
HMODULE WINAPI FakeLoadLibraryA(const char *name)
{
    gLog.push_back(std::string("LoadLibraryA ") + name);
    return gNoModule ? nullptr : reinterpret_cast<HMODULE>(static_cast<uintptr_t>((Hash(name) + gCase) | 2));
}
FARPROC WINAPI FakeGetProcAddress(HMODULE module, const char *name)
{
    gLog.push_back(std::string("GetProcAddress ") + std::to_string(reinterpret_cast<uintptr_t>(module)) + " " + name);
    uint32_t h = Hash(name);
    if (gMissingProc && (h & 3) == (gMissingProc & 3)) return nullptr;
    return reinterpret_cast<FARPROC>(static_cast<uintptr_t>(h ^ reinterpret_cast<uintptr_t>(module)));
}
BOOL WINAPI FakeFreeLibrary(HMODULE module)
{
    gLog.push_back("FreeLibrary " + std::to_string(reinterpret_cast<uintptr_t>(module)));
    return TRUE;
}

struct Imports {
    BindImport version, handle, load, proc, freeLib;
    explicit Imports(const difftest::Context &ctx)
        : version(ctx, winapi::kIatGetVersion, reinterpret_cast<const void *>(&FakeGetVersion)),
          handle(ctx, winapi::kIatGetModuleHandleA, reinterpret_cast<const void *>(&FakeGetModuleHandleA)),
          load(ctx, winapi::kIatLoadLibraryA, reinterpret_cast<const void *>(&FakeLoadLibraryA)),
          proc(ctx, winapi::kIatGetProcAddress, reinterpret_cast<const void *>(&FakeGetProcAddress)),
          freeLib(ctx, winapi::kIatFreeLibrary, reinterpret_cast<const void *>(&FakeFreeLibrary))
    {
    }
};

const DWORD kVersions[] = {
    0x23f00206, // NT 6.2 build
    0x00000000, // the sign edge: NT
    0x00000005, 0x0a280105, 0x7fffffff, 0x80000004, // 95 (4.0): no TryEnter
    0x80000a04,                                     // 98 (4.10)
    0x80005a04,                                     // Me
    0x80000003, 0x80000005, 0x80000104, 0x800000ff, 0xffffffff, 0x80000000, 0xc0000004,
};

struct Window {
    uint32_t first, size;
    std::vector<uint8_t> Read(const difftest::Context &ctx) const
    {
        const uint8_t *p = ctx.image.At<uint8_t>(first);
        return {p, p + size};
    }
    void Write(const difftest::Context &ctx, const std::vector<uint8_t> &w) const
    {
        std::memcpy(ctx.image.At<uint8_t>(first), w.data(), w.size());
    }
};

// Runs `orig` and `ours` (each returning the object) from the same junk window and fake answers; compares.
template <class Orig, class Ours>
void Compare(difftest::Context &ctx, const char *what, const Window &w, uint32_t object, Orig orig, Ours ours)
{
    Imports imports(ctx);
    const std::vector<uint8_t> saved = w.Read(ctx);
    std::mt19937 rng(Hash(what));
    for (uint32_t c = 0; c < 3 * std::size(kVersions) * 3; ++c) {
        ++ctx.cases;
        gCase = c << 4;
        gVersion = kVersions[c % std::size(kVersions)];
        gNoModule = (c / std::size(kVersions)) % 3 == 1;
        gMissingProc = (c / std::size(kVersions)) % 3 == 2 ? 1 + c % 3 : 0;
        std::vector<uint8_t> start(w.size);
        for (auto &b : start) b = static_cast<uint8_t>(rng());
        w.Write(ctx, start);
        gLog.clear();
        void *a = orig();
        std::vector<uint8_t> afterA = w.Read(ctx);
        std::vector<std::string> logA = gLog;
        w.Write(ctx, start);
        gLog.clear();
        void *b = ours();
        std::vector<uint8_t> afterB = w.Read(ctx);
        if (a != b || a != ctx.image.At<void>(object)) ctx.Fail("%s case %u: returned %p vs ours %p", what, c, a, b);
        if (afterA != afterB) {
            for (uint32_t i = 0; i < w.size; ++i)
                if (afterA[i] != afterB[i]) {
                    ctx.Fail("%s case %u (version %08x): byte %08x is %02x, ours %02x", what, c, gVersion, w.first + i,
                             afterA[i], afterB[i]);
                    break;
                }
        }
        if (logA != gLog) {
            ctx.Fail("%s case %u (version %08x): calls differ (%zu vs %zu)", what, c, gVersion, logA.size(), gLog.size());
            for (size_t i = 0; i < logA.size() || i < gLog.size(); ++i)
                ctx.Fail("  %s | %s", i < logA.size() ? logA[i].c_str() : "-", i < gLog.size() ? gLog[i].c_str() : "-");
            break;
        }
    }
    w.Write(ctx, saved);
}

using Ctor = void *(__thiscall *)(void *);

} // namespace

DIFF_TEST(WinApiTables_WideFileFunctions, 0x1c0d9b)
{
    auto orig = ctx.Original<Ctor>(0x1c0d9b);
    void *object = ctx.image.At<void>(WideFileFunctions::kRva);
    Compare(ctx, "wide file", {WideFileFunctions::kRva - 8, 0x34 + 16}, WideFileFunctions::kRva, [&] { return orig(object); },
            [&] { return static_cast<void *>(GameAddress<WideFileFunctions>(WideFileFunctions::kRva)->Construct()); });
}

DIFF_TEST(WinApiTables_WideDirectoryFunctions, 0x1c4830)
{
    auto orig = ctx.Original<Ctor>(0x1c4830);
    void *object = ctx.image.At<void>(WideDirectoryFunctions::kRva);
    Compare(ctx, "wide directory", {WideDirectoryFunctions::kRva - 8, 0x20 + 16}, WideDirectoryFunctions::kRva,
            [&] { return orig(object); },
            [&] { return static_cast<void *>(GameAddress<WideDirectoryFunctions>(WideDirectoryFunctions::kRva)->Construct()); });
}

// The loaders: the window covers the proc and module globals and the (empty) object after them; then the exit thunk.
DIFF_TEST(WinApiTables_TryEnterLoader, 0x1af60a)
{
    auto orig = ctx.Original<Ctor>(0x1af60a);
    void *object = ctx.image.At<void>(TryEnterCriticalSectionLoader::kRva);
    const Window w{TryEnterCriticalSectionLoader::kProcRva - 8, 0x10 + 16};
    Compare(ctx, "try-enter loader", w, TryEnterCriticalSectionLoader::kRva, [&] { return orig(object); },
            [&] {
                return static_cast<void *>(
                    GameAddress<TryEnterCriticalSectionLoader>(TryEnterCriticalSectionLoader::kRva)->Construct());
            });
    auto thunk = ctx.Original<void(__cdecl *)()>(0xd90658);
    Compare(ctx, "try-enter unload", w, TryEnterCriticalSectionLoader::kRva,
            [&] { thunk(); return object; },
            [&] { TryEnterCriticalSectionLoader::Unload(); return object; });
}

DIFF_TEST(WinApiTables_ShellExecuteLoader, 0x1afb32)
{
    auto orig = ctx.Original<Ctor>(0x1afb32);
    void *object = ctx.image.At<void>(ShellExecuteLoader::kRva);
    const Window w{ShellExecuteLoader::kProcRva - 8, 0xc + 16};
    Compare(ctx, "shell loader", w, ShellExecuteLoader::kRva, [&] { return orig(object); },
            [&] { return static_cast<void *>(GameAddress<ShellExecuteLoader>(ShellExecuteLoader::kRva)->Construct()); });
    auto thunk = ctx.Original<void(__cdecl *)()>(0xd906b9);
    Compare(ctx, "shell unload", w, ShellExecuteLoader::kRva, [&] { thunk(); return object; },
            [&] { ShellExecuteLoader::Unload(); return object; });
}
