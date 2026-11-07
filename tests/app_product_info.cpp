// Differential tests for the product helpers (lane MB, F4e; src/app/ProductInfo.h): GetProductPaths 0xcb4690,
// ListProductExecutables 0xcb4aa0, InstalledProductDirectories 0xcb4bb0, NewestProductExecutable 0xcb4950 and
// FindNewerProduct 0xcb4cc0.
//
// GetModuleFileNameA goes to an impfx fake (both sides; it writes one of a set of paths, seeded alike per side) and the
// CRT's _access (library code: the image's 0x84c4cf and our CRT's) to one logging stand-in on both sides. The list
// helper is tested on its own (its list is a constant); its three callers are tested twice: once over the real list
// helper, once with a stand-in for it on both sides that hands out random lists (empty and stuff-pack names, "SC"
// names, empty lists), so the scoring and the empty-entry paths are reached. Compared: al, every output byte (buffers
// pre-filled with a pattern), the lengths, the lists' texts, the fakes' log, and the sizes of the blocks freed
// (difftest::FreeRecorder). The freeing tests run the original once unrecorded before each case's two sides, so the
// harness's own logs have grown to the case's size and none of their reallocations falls in a recorded side.
#define NOMINMAX
#include "app/ProductInfo.h"
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "impfx.h"

#include <windows.h>

#include <io.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kGetProductPaths = 0xcb4690;
constexpr uint32_t kListProducts = 0xcb4aa0;
constexpr uint32_t kInstalledDirectories = 0xcb4bb0;
constexpr uint32_t kNewestExecutable = 0xcb4950;
constexpr uint32_t kFindNewer = 0xcb4cc0;
constexpr uint32_t kGameAccess = 0x84c4cf;
constexpr uint8_t kPattern = 0xa5;
constexpr size_t kBuffer = 0x210;

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

std::string Hex(const void *p, size_t n)
{
    std::string out;
    char b[4];
    for (size_t i = 0; i < n; ++i) {
        std::snprintf(b, sizeof(b), "%02x", static_cast<const uint8_t *>(p)[i]);
        out += b;
    }
    return out;
}

const char *const kModulePaths[] = {
    "C:\\Program Files\\EA Games\\The Sims 2 Legacy\\Fun with Pets\\SP9\\TSBin\\Sims2EP9.exe",
    "D:/Games/TS2/EP9/TSBin/Sims2EP9.exe",
    "C:\\a\\b\\c.exe",
    "C:\\a\\b",
    "\\x.exe",
    "/root/x.exe",
    "Sims2EP9.exe",
    "",
    "C:\\mixed/separators\\in/the\\path/Sims2.exe",
};

uint32_t OnModuleFileName(drawfx::Fake &, const uint32_t *args)
{
    Log("GetModuleFileNameA(0x%x, %u)", args[0], args[2]);
    char *buffer = reinterpret_cast<char *>(static_cast<uintptr_t>(args[1]));
    const char *path = kModulePaths[gR() % (sizeof(kModulePaths) / sizeof(kModulePaths[0]))];
    std::strcpy(buffer, path);
    return static_cast<uint32_t>(std::strlen(path));
}

int __cdecl FakeAccess(const char *path, int mode)
{
    Log("_access(%s, %d)", path, mode);
    return gR() % 3 ? 0 : -1;
}

// A random product list: names (some empty, some stuff packs, some "SC"), a count of 0..20, the slots as the real one
// (max(count, minimum), calloc'ed 0x104 bytes).
const char *const kNames[] = {"Sims2EP1.exe", "Sims2SP1.exe", "Sims2SC1.exe", "Sims2EP9.exe", "", "Sims2SC.exe",
                              "Sims2Ext.exe", "Sims2SPX.exe", "Sims2xyzw.exe"};

bool __cdecl FakeList(const char *key, char ***names, int32_t *count, int32_t minimum)
{
    Log("List(%s, %d, %d)", key ? key : "(null)", *count, minimum);
    const uint32_t r = gR() % 8;
    *count = r == 0 ? 0 : static_cast<int32_t>(gR() % 20) + 1;
    const int32_t slots = minimum > *count ? minimum : *count;
    *names = slots ? static_cast<char **>(std::malloc(static_cast<size_t>(slots) * sizeof(char *))) : nullptr;
    if (*count == 0) return false;
    for (int32_t i = 0; i < slots; ++i) {
        (*names)[i] = static_cast<char *>(std::calloc(0x104, 1));
        if (i < *count) std::strcpy((*names)[i], kNames[gR() % (sizeof(kNames) / sizeof(kNames[0]))]);
    }
    return true;
}

// The two results from a little before their first difference (the failure text is bounded).
void Differ(difftest::Context &ctx, const std::string &what, const std::string &a, const std::string &b)
{
    size_t at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at]) ++at;
    at = at > 80 ? at - 80 : 0;
    ctx.Fail("%s, from offset %u:\n  orig %.400s\n  ours %.400s", what.c_str(), static_cast<unsigned>(at), a.c_str() + at,
             b.c_str() + at);
}

std::string Frees(difftest::FreeRecorder &rec)
{
    rec.Stop();
    size_t sizes[256];
    const size_t n = rec.SortedSizes(sizes, 256);
    std::string out = "frees " + std::to_string(n) + ":";
    for (size_t i = 0; i < n && i < 256; ++i) out += " " + std::to_string(sizes[i]);
    return out;
}

struct World {
    impfx::Imports imp;
    shape::StandIn access{kGameAccess, &::_access, &FakeAccess};
    explicit World(difftest::Context &c) : imp(c) { IMPFX_ADD(imp, GetModuleFileNameA).On(&OnModuleFileName); }
    void BeginSide(uint32_t seed)
    {
        imp.BeginSide();
        drawfx::ResetAll();
        gLog.clear();
        gR.seed(seed);
    }
};

using PathsFn = uint32_t(__cdecl *)(const char *, char *, uint32_t *, char *, uint32_t *, char *, uint32_t *, uint32_t *);
using ListFn = uint32_t(__cdecl *)(const char *, char ***, int32_t *, int32_t);
using InstalledFn = uint32_t(__cdecl *)(const char *, char ***, int32_t *);
using NewestFn = uint32_t(__cdecl *)(char *);
using NewerFn = uint32_t(__cdecl *)(char *, int32_t *);

} // namespace

// ---- 0xcb4690: every executable name kind (the base game's, an expansion's, a long name, one whose folder is cut to
// nothing), each output given or not (buffer and length separately), the exists flag given or not, nine module paths.

DIFF_TEST(ProductInfo_GetProductPaths, 0xcb4690)
{
    World world(ctx);
    auto original = ctx.Original<PathsFn>(kGetProductPaths);
    const char *const executables[] = {"Sims2.exe", "Sims2EP9.exe", "Sims2SP1.exe", "Sims2Long Name.exe", "Sims2ABCD",
                                       "xxxxxyz.exe", "Sims2EP10.exe"};
    for (const char *executable : executables) {
        for (uint32_t mask = 0; mask < 128; ++mask) {
            for (uint32_t script = 0; script < 3; ++script) {
                const uint32_t seed = mask * 131u + script * 7u + static_cast<uint32_t>(std::strlen(executable));
                std::string results[2];
                for (int side = 0; side < 2; ++side) {
                    world.BeginSide(seed);
                    char key[kBuffer], directory[kBuffer], module[kBuffer];
                    std::memset(key, kPattern, sizeof(key));
                    std::memset(directory, kPattern, sizeof(directory));
                    std::memset(module, kPattern, sizeof(module));
                    uint32_t keyLength = 0x104, directoryLength = 0x55, moduleLength = 0x77, exists = 0xcccccccc;
                    char *const k = mask & 1 ? key : nullptr;
                    uint32_t *const kl = mask & 2 ? &keyLength : nullptr;
                    char *const d = mask & 4 ? directory : nullptr;
                    uint32_t *const dl = mask & 8 ? &directoryLength : nullptr;
                    char *const m = mask & 16 ? module : nullptr;
                    uint32_t *const ml = mask & 32 ? &moduleLength : nullptr;
                    uint32_t *const e = mask & 64 ? &exists : nullptr;
                    const uint32_t al = side == 0 ? original(executable, k, kl, d, dl, m, ml, e) & 0xff
                                                  : product::GetProductPaths(executable, k, kl, d, dl, m, ml, e);
                    results[side] = std::to_string(al) + " " + std::to_string(keyLength) + " " +
                                    std::to_string(directoryLength) + " " + std::to_string(moduleLength) + " " +
                                    std::to_string(exists) + "\n key " + Hex(key, sizeof(key)) + "\n dir " +
                                    Hex(directory, sizeof(directory)) + "\n mod " + Hex(module, sizeof(module)) + "\n " +
                                    Joined();
                }
                ++ctx.cases;
                if (results[0] != results[1])
                    Differ(ctx, std::string(executable) + " mask " + std::to_string(mask) + " script " + std::to_string(script),
                           results[0], results[1]);
            }
        }
    }
}

// ---- 0xcb4aa0: minimum below, at and above the list's 16; the key is not read (null or text).

DIFF_TEST(ProductInfo_ListProductExecutables, 0xcb4aa0)
{
    auto original = ctx.Original<ListFn>(kListProducts);
    for (int32_t minimum : {-5, 0, 1, 15, 16, 17, 40}) {
        for (const char *key : {static_cast<const char *>(nullptr), "Software\\x\\"}) {
            std::string results[2];
            for (int side = 0; side < 2; ++side) {
                char **names = reinterpret_cast<char **>(static_cast<uintptr_t>(0x0bad0000));
                int32_t count = 0x3c3c;
                const uint32_t al = side == 0 ? original(key, &names, &count, minimum) & 0xff
                                              : product::ListProductExecutables(key, &names, &count, minimum);
                const int32_t slots = minimum > count ? minimum : count;
                std::string text = std::to_string(al) + " count " + std::to_string(count) + " names " +
                                   std::to_string(names != nullptr) + ":";
                for (int32_t i = 0; names && i < slots; ++i) {
                    text += " [" + std::string(names[i]) + "]" + (names[i][0] ? "" : Hex(names[i], 0x104));
                    std::free(names[i]);
                }
                if (names) std::free(names);
                results[side] = text;
            }
            ++ctx.cases;
            if (results[0] != results[1])
                Differ(ctx, "minimum " + std::to_string(minimum), results[0], results[1]);
        }
    }
}

// ---- 0xcb4bb0: over the real list (16 products, each directory existing or not), then over random lists.

DIFF_TEST(ProductInfo_InstalledProductDirectories, 0xcb4bb0)
{
    World world(ctx);
    auto original = ctx.Original<InstalledFn>(kInstalledDirectories);
    for (int fake = 0; fake < 2; ++fake) {
        for (uint32_t script = 0; script < 48; ++script) {
            std::string results[3];
            for (int side = -1; side < 2; ++side) { // side -1: the original, unrecorded (grows the harness's logs first)
                world.BeginSide(script * 977u + fake * 13u + 1u);
                std::unique_ptr<shape::StandIn> list;
                if (fake) list = std::make_unique<shape::StandIn>(kListProducts, &product::ListProductExecutables, &FakeList);
                char **directories = reinterpret_cast<char **>(static_cast<uintptr_t>(0x0bad0000));
                int32_t count = 0x3c3c;
                difftest::FreeRecorder rec;
                const uint32_t al = side <= 0 ? original("Software\\key\\", &directories, &count) & 0xff
                                              : product::InstalledProductDirectories("Software\\key\\", &directories, &count);
                std::string text = Frees(rec) + " " + std::to_string(al) + " count " + std::to_string(count) + " dirs " +
                                   std::to_string(directories != nullptr) + ":";
                for (int32_t i = 0; directories && al && i < count; ++i) {
                    text += directories[i] ? " [" + std::string(directories[i]) + "]" : " null";
                    std::free(directories[i]);
                }
                if (directories && al) std::free(directories);
                results[side + 1] = text + "\n " + Joined();
            }
            ++ctx.cases;
            if (results[1] != results[2])
                Differ(ctx, "fake " + std::to_string(fake) + " script " + std::to_string(script), results[1], results[2]);
        }
    }
}

// ---- 0xcb4950 and 0xcb4cc0: over the real list, then over random lists; the outputs given or not.

DIFF_TEST(ProductInfo_NewestProductExecutable, 0xcb4950)
{
    World world(ctx);
    auto original = ctx.Original<NewestFn>(kNewestExecutable);
    for (int fake = 0; fake < 2; ++fake) {
        for (uint32_t script = 0; script < 64; ++script) {
            std::string results[3];
            for (int side = -1; side < 2; ++side) { // side -1: the original, unrecorded (grows the harness's logs first)
                world.BeginSide(script * 31u + fake * 7u + 3u);
                std::unique_ptr<shape::StandIn> list;
                if (fake) list = std::make_unique<shape::StandIn>(kListProducts, &product::ListProductExecutables, &FakeList);
                char out[0x110];
                std::memset(out, kPattern, sizeof(out));
                difftest::FreeRecorder rec;
                const uint32_t al = side <= 0 ? original(out) & 0xff : product::NewestProductExecutable(out);
                results[side + 1] = Frees(rec) + " " + std::to_string(al) + " " + Hex(out, sizeof(out)) + "\n " + Joined();
            }
            ++ctx.cases;
            if (results[1] != results[2])
                Differ(ctx, "fake " + std::to_string(fake) + " script " + std::to_string(script), results[1], results[2]);
        }
    }
}

DIFF_TEST(ProductInfo_FindNewerProduct, 0xcb4cc0)
{
    World world(ctx);
    auto original = ctx.Original<NewerFn>(kFindNewer);
    for (int fake = 0; fake < 2; ++fake) {
        for (uint32_t script = 0; script < 256; ++script) {
            if (!fake && script >= 4) break;
            std::string results[3];
            for (int side = -1; side < 2; ++side) { // side -1: the original, unrecorded (grows the harness's logs first)
                world.BeginSide(script * 61u + fake * 5u + 11u);
                std::unique_ptr<shape::StandIn> list;
                if (fake) list = std::make_unique<shape::StandIn>(kListProducts, &product::ListProductExecutables, &FakeList);
                char out[0x110];
                std::memset(out, kPattern, sizeof(out));
                int32_t score = 0x5a5a5a5a;
                char *const o = script & 1 ? nullptr : out;
                int32_t *const s = script & 2 ? nullptr : &score;
                difftest::FreeRecorder rec;
                const uint32_t al = side <= 0 ? original(o, s) & 0xff : product::FindNewerProduct(o, s);
                results[side + 1] = Frees(rec) + " " + std::to_string(al) + " score " + std::to_string(score) + " " +
                                Hex(out, sizeof(out)) + "\n " + Joined();
            }
            ++ctx.cases;
            if (results[1] != results[2])
                Differ(ctx, "fake " + std::to_string(fake) + " script " + std::to_string(script), results[1], results[2]);
        }
    }
}
