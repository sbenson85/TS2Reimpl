// Differential tests for the shell folders and the registered product name (foundations F4, lane LY):
// folders::GetSpecialFolderPath (0x1c50d6), folders::EnsureDirectory (0x1c5031) and cTSAppFW::RegisteredProductName
// (0xc27a16).
//
// The Win32 calls go to impfx fakes (tests/impfx.h) on both sides: GetWindowsDirectoryA, SHGetSpecialFolderLocation
// (through the image's thunk 0x859fa0), SHGetPathFromIDListA, SHGetMalloc (a fake allocator whose Free and Release
// log), RegOpenKeyExA, RegQueryValueExA, RegCloseKey. The image's two optional wide entry points (0x10cb628
// GetWindowsDirectoryW, 0x10cb62c SHGetFolderPathW) are set to fakes or null. Our callees and the image's are stood in
// on both sides with one fake each: ConvertBytesToString (0x1afc9d), GetModulePath (0x1b139f), GetModuleDirectory
// (0x1c1c8c), DirectoryExists (0x1c4f94), CreateDirectories (0x1c4caf), the product's registry key (GetProductPaths,
// 0xcb4690). Every fake logs its arguments' texts and draws its answer from one generator seeded alike per side;
// compared: al, the out string's text and the log.
#define NOMINMAX
#include "app/AppServices2.h"
#include "app/ProductInfo.h"
#include "app/ExceptionReporter.h"
#include "app/ReportBlockers.h"
#include "app/SpecialFolders.h"
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "gz/GZCOM.h"
#include "impfx.h"
#include "ts/cTSAppFW.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kSpecialFolder = 0x1c50d6;
constexpr uint32_t kEnsureDirectory = 0x1c5031;
constexpr uint32_t kProductName = 0xc27a16;
constexpr uint32_t kConvertBytesToString = 0x1afc9d;
constexpr uint32_t kGetModulePath = 0x1b139f;
constexpr uint32_t kGetModuleDirectory = 0x1c1c8c;
constexpr uint32_t kDirectoryExists = 0x1c4f94;
constexpr uint32_t kCreateDirectories = 0x1c4caf;
constexpr uint32_t kProductPaths = 0xcb4690;
constexpr uint32_t kWindowsDirectoryWPointer = 0x10cb628;
constexpr uint32_t kFolderPathWPointer = 0x10cb62c;

std::vector<std::string> gLog;
std::mt19937 gR;

void Log(const char *fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    gLog.emplace_back(buf);
}

std::string Joined()
{
    std::string out;
    for (const std::string &s : gLog) out += s + "|";
    return out;
}

// A cRZString on the stack (original vtable), destroyed at the end of scope.
struct Str {
    alignas(cRZString) unsigned char raw[sizeof(cRZString)];
    explicit Str(const char *text = "")
    {
        std::memset(raw, 0xcd, sizeof(raw));
        S().Construct(text);
    }
    ~Str() { S().Destruct(); }
    Str(const Str &) = delete;
    cRZString &S() { return *reinterpret_cast<cRZString *>(raw); }
};

// A folder-like text: empty, short, with a separator at the end or not, long (past the string's own buffer).
std::string FolderText()
{
    switch (gR() % 7) {
    case 0: return "";
    case 1: return "C:";
    case 2: return "C:\\Users\\sim\\Documents";
    case 3: return "C:\\Users\\sim\\Documents\\";
    case 4: return "D:/Data/";
    case 5: return "\\\\server\\share\\some folder with a long name\\deeper";
    default: return std::string("E:\\x") + std::to_string(gR() % 1000);
    }
}

std::wstring Wide(const std::string &text) { return std::wstring(text.begin(), text.end()); }

std::string Narrow(const wchar_t *text, size_t length)
{
    std::string out;
    for (size_t i = 0; i < length; ++i) out.push_back(text[i] < 0x80 ? static_cast<char>(text[i]) : '?');
    return out;
}

// ---- the stand-ins (both sides)

uint32_t __cdecl FakeConvert(const void *src, uint32_t length, int32_t encoding, cIGZString *dst)
{
    std::string text = encoding == 0x10 ? Narrow(static_cast<const wchar_t *>(src), length)
                                        : std::string(static_cast<const char *>(src), length);
    Log("Convert(%s, %u, %d)", text.c_str(), length, encoding);
    reinterpret_cast<cRZString *>(dst)->mString = text;
    return length;
}

uint32_t __cdecl FakeModulePath(cIGZString *out)
{
    Log("GetModulePath(%s)", reinterpret_cast<cRZString *>(out)->mString.c_str());
    reinterpret_cast<cRZString *>(out)->mString = gR() % 2 ? "C:\\Games\\TS2\\TSBin\\Sims2EP9.exe" : "Sims2.exe";
    return 0x5a00 | (gR() & 0xff);
}

uint8_t __cdecl FakeModuleDirectory(cIGZString *path, cIGZString *out)
{
    std::string text = reinterpret_cast<cRZString *>(path)->mString;
    Log("GetModuleDirectory(%s)", text.c_str());
    const size_t cut = text.find_last_of("\\/");
    reinterpret_cast<cRZString *>(out)->mString = cut == std::string::npos ? "" : text.substr(0, cut + (gR() % 2));
    return static_cast<uint8_t>(gR());
}

uint32_t __cdecl FakeDirectoryExists(cRZString *path)
{
    Log("DirectoryExists(%s)", path->mString.c_str());
    const uint32_t r = gR() % 4;
    return r == 0 ? 1u : r == 1 ? 0x7701u : r == 2 ? 0x7700u : 0u;
}

uint32_t __cdecl FakeCreateDirectories(cRZString *path)
{
    Log("CreateDirectories(%s)", path->mString.c_str());
    const uint32_t r = gR() % 4;
    return r == 0 ? 1u : r == 1 ? 0x6601u : r == 2 ? 0x6600u : 0u;
}

// ---- the optional wide entry points

UINT __stdcall FakeWindowsDirectoryW(wchar_t *buffer, UINT size)
{
    Log("GetWindowsDirectoryW(%u)", size);
    const std::wstring text = Wide(FolderText());
    switch (gR() % 5) {
    case 0: return size;              // too small: nothing written
    case 1: return size + 0x40;
    case 2: buffer[0] = 0; return 0;  // failed
    default:
        std::memcpy(buffer, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
        return static_cast<UINT>(text.size());
    }
}

HRESULT __stdcall FakeFolderPathW(HWND window, int folder, HANDLE token, DWORD flags, wchar_t *buffer)
{
    Log("SHGetFolderPathW(%p, 0x%x, %p, %u)", window, folder, token, flags);
    const std::wstring text = Wide(FolderText());
    std::memcpy(buffer, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    return gR() % 3 ? S_OK : E_FAIL;
}

// ---- the imports (impfx handlers: raw argument words)

std::string Text(uint32_t word) { return word ? reinterpret_cast<const char *>(static_cast<uintptr_t>(word)) : "(null)"; }

uint32_t OnWindowsDirectoryA(drawfx::Fake &, const uint32_t *args)
{
    char *buffer = reinterpret_cast<char *>(static_cast<uintptr_t>(args[0]));
    Log("GetWindowsDirectoryA(%u)", args[1]);
    const std::string text = FolderText();
    switch (gR() % 5) {
    case 0: return args[1];
    case 1: return args[1] + 7;
    case 2: buffer[0] = 0; return 0;
    default:
        std::memcpy(buffer, text.c_str(), text.size() + 1);
        return static_cast<uint32_t>(text.size());
    }
}

uint32_t OnSpecialFolderLocation(drawfx::Fake &, const uint32_t *args)
{
    auto *list = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(args[2]));
    Log("SHGetSpecialFolderLocation(%u, 0x%x, list %u)", args[0], args[1], *list);
    if (gR() % 3 == 0) {
        *list = 0;
        return static_cast<uint32_t>(E_FAIL);
    }
    *list = 0x51d00000u | args[1];
    return S_OK;
}

uint32_t OnPathFromIDList(drawfx::Fake &, const uint32_t *args)
{
    char *buffer = reinterpret_cast<char *>(static_cast<uintptr_t>(args[1]));
    Log("SHGetPathFromIDListA(0x%x)", args[0]);
    const std::string text = FolderText();
    std::memcpy(buffer, text.c_str(), text.size() + 1);
    return gR() % 4 ? 1u + (gR() % 2) * 0x100u : 0u;
}

// The shell's allocator: Free (slot 5) and Release (slot 2) log.
uint32_t __stdcall MallocTrap(void *) { Log("IMalloc.TRAP"); return 0; }
ULONG __stdcall MallocRelease(void *) { Log("IMalloc.Release"); return 0; }
void __stdcall MallocFree(void *, void *block) { Log("IMalloc.Free(0x%x)", static_cast<uint32_t>(reinterpret_cast<uintptr_t>(block))); }
void *gMallocVt[] = {reinterpret_cast<void *>(&MallocTrap), reinterpret_cast<void *>(&MallocTrap),
                     reinterpret_cast<void *>(&MallocRelease), reinterpret_cast<void *>(&MallocTrap),
                     reinterpret_cast<void *>(&MallocTrap), reinterpret_cast<void *>(&MallocFree)};
struct {
    void **vt;
} gMalloc{gMallocVt};

uint32_t OnGetMalloc(drawfx::Fake &, const uint32_t *args)
{
    auto *out = reinterpret_cast<void **>(static_cast<uintptr_t>(args[0]));
    Log("SHGetMalloc(%p)", *out);
    if (gR() % 4 == 0) {
        *out = nullptr;
        return static_cast<uint32_t>(E_FAIL);
    }
    *out = &gMalloc;
    return S_OK;
}

constexpr uint32_t kKeyHandle = 0x4a0000f0;

uint32_t OnRegOpenKey(drawfx::Fake &, const uint32_t *args)
{
    Log("RegOpenKeyExA(0x%x, %s, %u, 0x%x)", args[0], Text(args[1]).c_str(), args[2], args[3]);
    if (gR() % 3 == 0) return ERROR_FILE_NOT_FOUND;
    *reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(args[4])) = kKeyHandle;
    return ERROR_SUCCESS;
}

uint32_t OnRegQueryValue(drawfx::Fake &, const uint32_t *args)
{
    auto *size = reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(args[5]));
    Log("RegQueryValueExA(0x%x, %s, %u, type %d, data %d, size %u)", args[0], Text(args[1]).c_str(), args[2],
        args[3] != 0, args[4] != 0, *size);
    switch (gR() % 4) {
    case 0: return ERROR_FILE_NOT_FOUND;
    case 1: return ERROR_MORE_DATA;
    default: {
        std::string text = gR() % 2 ? "The Sims 2 Ultimate Collection" : std::string(gR() % 200, 'n');
        *reinterpret_cast<uint32_t *>(static_cast<uintptr_t>(args[3])) = REG_SZ;
        std::memcpy(reinterpret_cast<char *>(static_cast<uintptr_t>(args[4])), text.c_str(), text.size() + 1);
        *size = static_cast<uint32_t>(text.size() + 1);
        return ERROR_SUCCESS;
    }
    }
}

uint32_t OnRegCloseKey(drawfx::Fake &, const uint32_t *args)
{
    Log("RegCloseKey(0x%x)", args[0]);
    return ERROR_SUCCESS;
}

uint8_t __cdecl FakeProductPaths(const char *executable, char *key, uint32_t *keyLength, char *directory,
                                 uint32_t *directoryLength, void *a, void *b, void *c)
{
    Log("ProductPaths(%s, key %u, dir %d %d, %p %p %p)", executable, *keyLength, directory != nullptr,
        directoryLength != nullptr, a, b, c);
    const std::string text = gR() % 2 ? "Software\\Electronic Arts\\The Sims 2 Ultimate Collection" : "";
    std::memcpy(key, text.c_str(), text.size() + 1);
    *keyLength = static_cast<uint32_t>(text.size());
    return static_cast<uint8_t>(gR());
}

// The world of every test: the imports, the wide pointers (set per case), the stand-ins.
struct World {
    difftest::Context &ctx;
    impfx::Imports imp;
    shape::StandIn convert{kConvertBytesToString, &ConvertBytesToString, &FakeConvert};
    shape::StandIn modulePath{kGetModulePath, &GetModulePath, &FakeModulePath};
    shape::StandIn moduleDirectory{kGetModuleDirectory, &appbase::GetModuleDirectory, &FakeModuleDirectory};
    shape::StandIn directoryExists{kDirectoryExists, &as2::DirectoryExists, &FakeDirectoryExists};
    shape::StandIn createDirectories{kCreateDirectories, &appbase::CreateDirectories, &FakeCreateDirectories};
    shape::StandIn productPaths{kProductPaths, &product::GetProductPaths, &FakeProductPaths};
    void *savedWindowsW, *savedFolderW;
    explicit World(difftest::Context &c) : ctx(c), imp(c)
    {
        IMPFX_ADD(imp, GetWindowsDirectoryA).On(&OnWindowsDirectoryA);
        IMPFX_ADD(imp, SHGetSpecialFolderLocation).On(&OnSpecialFolderLocation);
        IMPFX_ADD(imp, SHGetPathFromIDListA).On(&OnPathFromIDList);
        IMPFX_ADD(imp, SHGetMalloc).On(&OnGetMalloc);
        IMPFX_ADD(imp, RegOpenKeyExA).On(&OnRegOpenKey);
        IMPFX_ADD(imp, RegQueryValueExA).On(&OnRegQueryValue);
        IMPFX_ADD(imp, RegCloseKey).On(&OnRegCloseKey);
        savedWindowsW = *c.image.At<void *>(kWindowsDirectoryWPointer);
        savedFolderW = *c.image.At<void *>(kFolderPathWPointer);
    }
    ~World()
    {
        *ctx.image.At<void *>(kWindowsDirectoryWPointer) = savedWindowsW;
        *ctx.image.At<void *>(kFolderPathWPointer) = savedFolderW;
    }
    void SetWide(bool windows, bool folder)
    {
        *ctx.image.At<void *>(kWindowsDirectoryWPointer) = windows ? reinterpret_cast<void *>(&FakeWindowsDirectoryW) : nullptr;
        *ctx.image.At<void *>(kFolderPathWPointer) = folder ? reinterpret_cast<void *>(&FakeFolderPathW) : nullptr;
    }
    void BeginSide(uint32_t seed)
    {
        imp.BeginSide();
        drawfx::ResetAll();
        gLog.clear();
        gR.seed(seed);
    }
};

const char *const kStartTexts[] = {"", "old", "C:\\left\\over\\text\\longer than the buffer\\"};

} // namespace

// ---- 0x1c50d6 GetSpecialFolderPath: every kind (and three outside the switch), with and without each wide entry
// point, ensure off / on (also a non-1 byte), 24 answer scripts each.

DIFF_TEST(SpecialFolders_GetSpecialFolderPath, 0x1c50d6)
{
    World world(ctx);
    auto original = ctx.Original<uint32_t(__cdecl *)(int32_t, cRZString *, uint32_t)>(kSpecialFolder);
    const int32_t kinds[] = {-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 0x7fffffff};
    for (int32_t kind : kinds) {
        for (int wide = 0; wide < 4; ++wide) {
            for (uint32_t ensure : {0x0u, 0x1u, 0x80u, 0x100u}) {
                for (uint32_t script = 0; script < 24; ++script) {
                    world.SetWide((wide & 1) != 0, (wide & 2) != 0);
                    const uint32_t seed = static_cast<uint32_t>(kind) * 7919u + wide * 131u + ensure * 17u + script;
                    std::string results[2];
                    for (int side = 0; side < 2; ++side) {
                        world.BeginSide(seed);
                        Str out(kStartTexts[script % 3]);
                        const uint32_t al = side == 0 ? original(kind, &out.S(), ensure) & 0xff
                                                      : folders::GetSpecialFolderPath(kind, &out.S(), static_cast<uint8_t>(ensure));
                        results[side] = std::to_string(al) + " [" + out.S().mString + "] " + Joined();
                    }
                    ++ctx.cases;
                    if (results[0] != results[1])
                        ctx.Fail("kind %d wide %d ensure 0x%x script %u:\n  orig %s\n  ours %s", kind, wide, ensure, script,
                                 results[0].c_str(), results[1].c_str());
                }
            }
        }
    }
}

// ---- 0x1c5031 EnsureDirectory: al (the upper bytes are DirectoryExists' leftovers when it exists).

DIFF_TEST(SpecialFolders_EnsureDirectory, 0x1c5031)
{
    World world(ctx);
    auto original = ctx.Original<uint32_t(__cdecl *)(cRZString *)>(kEnsureDirectory);
    for (uint32_t script = 0; script < 64; ++script) {
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            world.BeginSide(script * 31u + 5u);
            Str path(FolderText().c_str());
            const uint32_t al = (side == 0 ? original(&path.S()) : folders::EnsureDirectory(&path.S())) & 0xff;
            results[side] = std::to_string(al) + " [" + path.S().mString + "] " + Joined();
        }
        ++ctx.cases;
        if (results[0] != results[1]) ctx.Fail("script %u: %s / %s", script, results[0].c_str(), results[1].c_str());
    }
}

// ---- 0xc27a16 RegisteredProductName (`this` not read: both are called with a junk one).

DIFF_TEST(cTSAppFW_RegisteredProductName, 0xc27a16)
{
    World world(ctx);
    using Fn = void(__thiscall *)(void *, cIGZString *);
    auto original = ctx.Original<Fn>(kProductName);
    auto ours = reinterpret_cast<Fn>(shape::CodeOf(&ts::cTSAppFW::RegisteredProductName));
    for (uint32_t script = 0; script < 256; ++script) {
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            world.BeginSide(script * 977u + 3u);
            Str out(kStartTexts[script % 3]);
            (side == 0 ? original : ours)(reinterpret_cast<void *>(0x0bad0044), out.S().AsInterface());
            results[side] = "[" + out.S().mString + "] " + Joined();
        }
        ++ctx.cases;
        if (results[0] != results[1]) ctx.Fail("script %u:\n  orig %s\n  ours %s", script, results[0].c_str(), results[1].c_str());
    }
}
