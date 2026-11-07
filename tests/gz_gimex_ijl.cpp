// Differential tests for src/gz/GimexIjl.size.cpp: Gimex's JPEG codec through Intel's IJL (row 3 of the codec table): the
// DLL load, about, is, open, info and close. (Read and write are the original's still.)
//
// The DLL is faked with impfx Libraries(): LoadLibraryA / GetProcAddress / FreeLibrary over fake modules ijl15.dll,
// ijl11.dll and ijl10.dll, any subset present, each export present or missing; the exports are recording fakes (tests
// answer from a script and fill the properties structure info reads). A case is a script of operations (about, is, open,
// info, close, load, unload) run once with the original's entry points and once with ours; both leave a log of every
// import call and every note (results, the six cells the load keeps in the game's data, a hash of each block returned),
// compared whole. A fake cIGZFile (tests/gz_gimex_stream.h) is the stream.
//
// Inputs kept out: info with the entry points not loaded (the original calls through a null cell), and a stream that
// reads short inside open (the fake cannot).
#define NOMINMAX
#include "code_patch.h"
#include "difftest.h"
#include "game/game.h"
#include "game/original.h"
#include "gz/GimexFormats.h"
#include "gz_gimex_stream.h"
#include "impfx.h"
#include "shape.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using namespace gimexstream;

constexpr uint32_t kUnload = 0x87f790;
constexpr uint32_t kLoad = 0x87f7a8;
constexpr uint32_t kAbout = 0x87f864;
constexpr uint32_t kIs = 0x87fb72;
constexpr uint32_t kOpen = 0x87fbcb;
constexpr uint32_t kInfo = 0x87f9e0;
constexpr uint32_t kClose = 0x87f9b2;
constexpr uint32_t kFreeMemory = 0x87da72;

constexpr uint32_t kCells[] = {0x10e6948, 0x10e694c, 0x10e6950, 0x10e6954, 0x10e6958, 0x10e695c};
constexpr uint32_t kPropertiesBytes = 0x4e68;

using FreeFn = int32_t(__stdcall *)(void *);

// What the faked DLL answers and writes.
struct Script {
    uint32_t initResult = 0, readResult = 0, freeResult = 0;
    int32_t width = 0, height = 0, channels = 0, quality = 0;
    uint32_t exifPresent = 0;
    uint8_t exifFlag = 0;
    uint16_t exifValue = 0;
    uint16_t commentFlag = 0; // the word that says the comment is present
    uint32_t commentPointer = 0;
    uint16_t commentHigh = 0;
    // The read and write tests: the whole-image read's answer, the write's answer, the bytes it produces and the size it
    // reports (the original writes it, the stream gets that many bytes).
    uint32_t wholeResult = 0;
    uint32_t writeResult = 0;
    uint32_t writeBytes = 0;
    uint32_t writeOffset = 0;
    int32_t reportedSize = 0;
    uint32_t fallbackResult = 0;
    int32_t dibChannelsAfter = -1; // the read test: what the whole-image read leaves in the DIB channel count (-1 = untouched)
};
Script gScript;

// The read test's pixel buffer (the whole-image read fills it) and the seed of the pixels it writes.
uint8_t *gPixelBase = nullptr;
size_t gPixelBytes = 0;
std::mt19937 gFill;

uint32_t Hash(const void *bytes, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<const uint8_t *>(bytes)[i]) * 16777619u;
    return h;
}

template <class T> T &At(uint8_t *base, uint32_t offset)
{
    return *reinterpret_cast<T *>(base + offset);
}

uint32_t FakeInit(drawfx::Fake &, const uint32_t *args)
{
    uint8_t *const p = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(args[0]));
    bool zero = true;
    for (uint32_t i = 0; i < kPropertiesBytes; ++i) zero = zero && p[i] == 0;
    drawfx::Note("ijlInit zeroed", zero ? 1 : 0);
    return gScript.initResult;
}

// The properties as the library was given them, with the two pointers (the pixels, the stream's bytes) cleared, hashed.
uint32_t PropertiesHash(const uint8_t *p)
{
    static uint8_t copy[kPropertiesBytes];
    std::memcpy(copy, p, kPropertiesBytes);
    At<uint32_t>(copy, 0x04) = 0;
    At<uint32_t>(copy, 0x24) = 0;
    return Hash(copy, kPropertiesBytes);
}

// ijlRead's whole-image mode: records the pixel description it was given and fills the caller's buffer, a row of
// width x channels bytes at a time, `width x channels + pad` bytes apart.
uint32_t WholeImageRead(uint8_t *p)
{
    const uint32_t bytes = At<uint32_t>(p, 0x04);
    const int32_t width = At<int32_t>(p, 0x08);
    const int32_t height = At<int32_t>(p, 0x0c);
    const int32_t pad = At<int32_t>(p, 0x10);
    const int32_t channels = At<int32_t>(p, 0x14);
    drawfx::Note("ijlRead whole", {static_cast<uint32_t>(width), static_cast<uint32_t>(height), static_cast<uint32_t>(pad),
                                   static_cast<uint32_t>(channels)});
    drawfx::Note("ijlRead whole 2", {At<uint32_t>(p, 0x18), At<uint32_t>(p, 0x38), bytes == reinterpret_cast<uintptr_t>(gPixelBase) ? 1u : 0u,
                                     PropertiesHash(p)});
    const int64_t stride = static_cast<int64_t>(width) * channels + pad;
    const int64_t rowBytes = std::min<int64_t>(static_cast<int64_t>(width) * channels, stride);
    if (bytes == reinterpret_cast<uintptr_t>(gPixelBase) && rowBytes > 0) {
        for (int64_t y = 0; y < height; ++y) {
            const int64_t at = y * stride;
            if (at < 0 || at + rowBytes > static_cast<int64_t>(gPixelBytes)) break;
            for (int64_t i = 0; i < rowBytes; ++i) gPixelBase[at + i] = static_cast<uint8_t>(gFill());
        }
    }
    if (gScript.dibChannelsAfter >= 0) At<int32_t>(p, 0x14) = gScript.dibChannelsAfter;
    return gScript.wholeResult;
}

uint32_t FakeRead(drawfx::Fake &, const uint32_t *args)
{
    uint8_t *const p = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(args[0]));
    if (args[1] == 3) return WholeImageRead(p);
    const uint32_t bytes = At<uint32_t>(p, 0x24);
    const uint32_t size = At<uint32_t>(p, 0x28);
    const uint32_t seen = size < 64 ? size : 64;
    drawfx::Note("ijlRead", {args[1], size, bytes != 0 ? Hash(reinterpret_cast<const void *>(static_cast<uintptr_t>(bytes)), seen) : 0});
    // The fields are written whatever the answer, so an info that reads them after a failure is seen.
    At<int32_t>(p, 0x2c) = gScript.width;
    At<int32_t>(p, 0x30) = gScript.height;
    At<int32_t>(p, 0x34) = gScript.channels;
    At<int32_t>(p, 0x50) = gScript.quality;
    At<uint32_t>(p, 0x4e50) = gScript.exifPresent;
    At<uint8_t>(p, 0x4e56) = gScript.exifFlag;
    At<uint16_t>(p, 0x4e58) = gScript.exifValue;
    At<uint16_t>(p, 0x4e60) = gScript.commentFlag;
    At<uint16_t>(p, 0x4e62) = gScript.commentHigh; // the flag is a word: the bytes above it are not part of it
    At<uint32_t>(p, 0x4e5c) = gScript.commentPointer;
    return gScript.readResult;
}

uint32_t FakeFree(drawfx::Fake &, const uint32_t *args)
{
    drawfx::Note("ijlFree", args[0] != 0 ? 1 : 0);
    return gScript.freeResult;
}

// ijlWrite: records what it was given (the pixel description, a hash of the 3-byte pixels, the label the output buffer
// starts with, the capacity) and fills the output with scripted bytes, from a scripted offset (so the label survives or
// not), reporting a scripted size.
uint32_t FakeWrite(drawfx::Fake &, const uint32_t *args)
{
    uint8_t *const p = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(args[0]));
    const uint8_t *const rgb = reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(At<uint32_t>(p, 0x04)));
    uint8_t *const output = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(At<uint32_t>(p, 0x24)));
    const int32_t width = At<int32_t>(p, 0x08);
    const int32_t height = At<int32_t>(p, 0x0c);
    const int64_t rgbBytes = static_cast<int64_t>(width) * height * 3;
    const int32_t capacity = At<int32_t>(p, 0x28);
    drawfx::Note("ijlWrite", {args[1], static_cast<uint32_t>(width), static_cast<uint32_t>(height), static_cast<uint32_t>(capacity)});
    drawfx::Note("ijlWrite pixels", {rgb != nullptr && rgbBytes >= 0 && rgbBytes < (1 << 24) ? Hash(rgb, static_cast<size_t>(rgbBytes)) : 0,
                                     output != nullptr ? Hash(output, 12) : 0, PropertiesHash(p)});
    if (output != nullptr && capacity > 0) {
        const uint32_t n = std::min<uint32_t>(gScript.writeBytes, static_cast<uint32_t>(capacity));
        for (uint32_t i = std::min(gScript.writeOffset, n); i < n; ++i) output[i] = static_cast<uint8_t>(gFill());
    }
    At<int32_t>(p, 0x28) = gScript.reportedSize < 0 ? gScript.reportedSize : std::min(gScript.reportedSize, capacity);
    return gScript.writeResult;
}

const char *const kModules[] = {"ijl15.dll", "ijl11.dll", "ijl10.dll"};
const char *const kComments[] = {"", "a comment", "Made with a fake encoder; 0123456789 abcdefghij", "x"};

struct Fixture {
    impfx::Imports imp;
    std::vector<impfx::ExportDef> all;
    explicit Fixture(difftest::Context &ctx) : imp(ctx)
    {
        imp.Libraries();
        for (const char *m : kModules) {
            imp.Export(m, "ijlInit", 1, impfx::Conv::Std, "i").On(&FakeInit);
            imp.Export(m, "ijlRead", 2, impfx::Conv::Std, "ii").On(&FakeRead);
            imp.Export(m, "ijlWrite", 2, impfx::Conv::Std, "ii").On(&FakeWrite);
            imp.Export(m, "ijlFree", 1, impfx::Conv::Std, "i").On(&FakeFree);
        }
        all = imp.settings().exports;
    }
    // A case's libraries: the modules that exist and their exports (a missing one is not handed out).
    void Program(std::mt19937 &r)
    {
        impfx::Settings &s = imp.settings();
        s.modules.clear();
        s.exports.clear();
        const uint32_t present = Roll(r, 4) == 0 ? 0 : 1 + Roll(r, 7); // a mask of the three, or none
        for (int m = 0; m < 3; ++m) {
            if (!(present & (1u << m))) continue;
            s.modules.emplace_back(kModules[m]);
            const uint32_t flaw = Roll(r, 3) == 0 ? 1 + Roll(r, 5) : 0; // 1 no init, 2 no read, 3 no free, 4 no write, 5 none
            for (const impfx::ExportDef &e : all) {
                if (e.module != kModules[m]) continue;
                if (flaw == 1 && e.name == "ijlInit") continue;
                if (flaw == 2 && e.name == "ijlRead") continue;
                if (flaw == 3 && e.name == "ijlFree") continue;
                if (flaw == 4 && e.name == "ijlWrite") continue;
                if (flaw == 5) continue;
                s.exports.push_back(e);
            }
        }
    }
};

void ResetCells()
{
    for (uint32_t cell : kCells) *GameAddress<uint32_t>(cell) = 0;
}

void NoteCells()
{
    for (uint32_t cell : kCells) drawfx::Note("cell", *GameAddress<uint32_t>(cell));
}

void ProgramScript(std::mt19937 &r)
{
    gScript = Script{};
    gScript.initResult = Roll(r, 6) == 0 ? 1 + Roll(r, 3) : 0;
    gScript.readResult = Roll(r, 6) == 0 ? 1 + Roll(r, 3) : 0;
    gScript.freeResult = Roll(r, 8) == 0 ? 1 : 0;
    gScript.width = static_cast<int32_t>(Roll(r, 4) == 0 ? r() : Roll(r, 3000));
    gScript.height = static_cast<int32_t>(Roll(r, 4) == 0 ? r() : Roll(r, 3000));
    gScript.channels = static_cast<int32_t>(Roll(r, 6));
    gScript.quality = static_cast<int32_t>(Roll(r, 4) == 0 ? r() : Roll(r, 101));
    gScript.exifPresent = Roll(r, 2) ? r() | 1 : 0;
    gScript.exifFlag = Roll(r, 2) ? static_cast<uint8_t>(1 + Roll(r, 255)) : 0;
    gScript.exifValue = static_cast<uint16_t>(Roll(r, 3) == 0 ? 0x8000 + Roll(r, 0x8000) : Roll(r, 600));
    const uint32_t comment = Roll(r, 4);
    // A nonzero flag always comes with a valid string (the original reads it); a zero flag may carry any pointer.
    gScript.commentFlag = 0;
    gScript.commentPointer = 0;
    if (comment == 1) {
        gScript.commentFlag = static_cast<uint16_t>(1 + Roll(r, 0xffff));
        gScript.commentPointer = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(kComments[Roll(r, 4)]));
    }
    if (comment == 2) gScript.commentPointer = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(kComments[Roll(r, 4)]));
    if (comment == 3) gScript.commentFlag = 0;
    gScript.commentHigh = static_cast<uint16_t>(Roll(r, 2) ? 1 + Roll(r, 0xffff) : 0);
}

enum Op { kOpAbout = 1, kOpIs = 2, kOpOpen = 4, kOpInfo = 8, kOpClose = 16, kOpLoad = 32, kOpUnload = 64 };

struct Held {
    Side *side;
};

// One script of operations through the original's entry points (ours = false) or ours.
void RunScript(difftest::Context &ctx, uint32_t seed, uint32_t mask, bool ours)
{
    std::mt19937 r(seed);
    std::vector<Side *> held;
    std::vector<uint32_t> ops;
    for (int i = 0; i < 10; ++i) {
        const uint32_t op = 1u << Roll(r, 7);
        if (mask & op) ops.push_back(op);
    }
    for (uint32_t op : ops) {
        switch (op) {
        case kOpAbout: {
            gimex::About *a = ours ? gimex::ijl::NewAbout() : ctx.Original<gimex::AboutFn>(kAbout)();
            drawfx::Note("about", a != nullptr ? Hash(a, sizeof(gimex::About)) : 0);
            if (a) ctx.Original<FreeFn>(kFreeMemory)(a);
            break;
        }
        case kOpIs: {
            Bytes data;
            const uint32_t kind = Roll(r, 8);
            static const uint8_t kMarks[3][4] = {{0xff, 0xd8, 0xff, 0xe0}, {0xff, 0xd8, 0xff, 0xe1}, {0xff, 0xd8, 0xff, 0xed}};
            if (kind < 3) {
                data.assign(kMarks[kind], kMarks[kind] + 4);
            } else if (kind == 3) {
                { const uint32_t k = Roll(r, 3); data.assign(kMarks[k], kMarks[k] + 4); }
                data[Roll(r, 4)] ^= static_cast<uint8_t>(1u << Roll(r, 8)); // one bit off
            } else if (kind == 4) {
                data = {0xe0, 0xff, 0xd8, 0xff}; // little-endian order
            } else if (kind == 5) {
                data = {0xff, 0xd8, 0xff, static_cast<uint8_t>(0xe2 + Roll(r, 10))};
            }
            RandomBytes(r, data, Roll(r, 3) == 0 ? Roll(r, 6) : 0);
            if (kind == 6) data.resize(Roll(r, 4)); // short
            Stream s(data);
            s.file.pos = Roll(r, 4) == 0 ? Roll(r, 3) : 0;
            const int32_t result = ours ? gimex::ijl::Probe(s.Get()) : ctx.Original<gimex::IsFn>(kIs)(s.Get());
            drawfx::Note("is", {static_cast<uint32_t>(result), s.file.pos, static_cast<uint32_t>(s.file.stray)});
            break;
        }
        case kOpOpen: {
            Bytes data;
            RandomBytes(r, data, Roll(r, 4) == 0 ? 0 : Roll(r, 41));
            Side *side = new Side(data);
            side->stream.file.pos = Roll(r, 3) == 0 ? Roll(r, static_cast<uint32_t>(data.size()) + 1) : 0; // open seeks to the start
            if (Roll(r, 2) == 0) { // dirty the heap, so a field the open leaves unwritten shows
                void *block = ctx.Original<void *(__stdcall *)(uint32_t)>(0x87da65)(0x18);
                if (block) std::memset(block, 0xa5, 0x18);
                if (block) ctx.Original<FreeFn>(kFreeMemory)(block);
            }
            gimex::Instance *sentinel = reinterpret_cast<gimex::Instance *>(0x5e5e5e5e);
            side->instance = sentinel;
            const int32_t result = ours ? gimex::ijl::Open(&side->instance, side->stream.Get(), "x.jpg", 0)
                                        : ctx.Original<gimex::OpenFn>(kOpen)(&side->instance, side->stream.Get(), "x.jpg", 0);
            const bool set = side->instance != sentinel;
            uint32_t words[6] = {};
            if (set) {
                std::memcpy(words, side->instance, sizeof(words));
                words[4] = 0;                      // the stream pointer
                words[5] = words[5] != 0 ? 1 : 0;  // the buffer pointer
            }
            drawfx::Note("open", {static_cast<uint32_t>(result), set ? 1u : 0u, words[0], words[1], words[2], words[3]});
            drawfx::Note("open buffer", {words[5], static_cast<uint32_t>(side->stream.file.pos), static_cast<uint32_t>(side->stream.file.stray)});
            if (set && held.size() < 4) {
                held.push_back(side);
            } else {
                delete side;
            }
            break;
        }
        case kOpInfo: {
            if (held.empty()) break;
            // The original calls through the cells: without all three it would call null.
            if (*GameAddress<uint32_t>(kCells[1]) == 0 || *GameAddress<uint32_t>(kCells[2]) == 0 ||
                *GameAddress<uint32_t>(kCells[4]) == 0)
                break;
            Side *side = held[Roll(r, static_cast<uint32_t>(held.size()))];
            if (side->instance->unknown14 == 0) break;
            gimex::Info *info = ours ? gimex::ijl::GetInfo(side->instance, static_cast<int32_t>(Roll(r, 3)))
                                     : ctx.Original<gimex::InfoFn>(kInfo)(side->instance, static_cast<int32_t>(Roll(r, 3)));
            drawfx::Note("info", info != nullptr ? Hash(info, sizeof(gimex::Info)) : 0);
            if (info != nullptr) {
                drawfx::Note("info fields", {static_cast<uint32_t>(info->width), static_cast<uint32_t>(info->height), info->variant,
                                             info->quality, std::bit_cast<uint32_t>(info->dotsPerInch)});
                ctx.Original<FreeFn>(kFreeMemory)(info);
            }
            break;
        }
        case kOpClose: {
            gimex::Instance *instance = nullptr;
            Side *side = nullptr;
            if (!held.empty() && Roll(r, 6) != 0) {
                const size_t k = Roll(r, static_cast<uint32_t>(held.size()));
                side = held[k];
                instance = side->instance;
                held.erase(held.begin() + static_cast<std::ptrdiff_t>(k));
            }
            const int32_t result = ours ? gimex::ijl::Close(instance) : ctx.Original<gimex::CloseFn>(kClose)(instance);
            drawfx::Note("close", static_cast<uint32_t>(result));
            delete side;
            break;
        }
        case kOpLoad: {
            const int32_t result = ours ? gimex::ijl::Load() : ctx.Original<int32_t(__cdecl *)()>(kLoad)();
            drawfx::Note("load", static_cast<uint32_t>(result));
            break;
        }
        case kOpUnload: {
            const int32_t result = ours ? gimex::ijl::Unload() : ctx.Original<int32_t(__cdecl *)()>(kUnload)();
            drawfx::Note("unload", static_cast<uint32_t>(result));
            break;
        }
        }
        NoteCells();
    }
    for (Side *side : held) delete side;
}

void Cases(difftest::Context &ctx, uint32_t seed, uint32_t mask, int cases)
{
    Fixture fx(ctx);
    std::mt19937 rng(seed);
    for (int c = 0; c < cases; ++c) {
        const uint32_t caseSeed = rng();
        std::mt19937 programRng(caseSeed ^ 0x9e3779b9u);
        fx.Program(programRng);
        ProgramScript(programRng);
        const Script script = gScript;
        auto side = [&](bool ours) {
            return drawfx::Side(
                [&] {
                    fx.imp.BeginSide();
                    ResetCells();
                    gScript = script;
                },
                [&] { RunScript(ctx, caseSeed, mask, ours); });
        };
        const auto original = side(false);
        const auto mine = side(true);
        ResetCells();
        char what[48];
        std::snprintf(what, sizeof(what), "ijl case %d", c);
        drawfx::Same(ctx, original.first, mine.first, what, caseSeed, original.second);
        if (ctx.failures.size() >= 10) return;
    }
}

// ---- Read (0x87fc52) and write (0x87fe74) ----

constexpr uint32_t kRead = 0x87fc52;
constexpr uint32_t kWrite = 0x87fe74;
constexpr uint32_t kAllocate = 0x87da65;
constexpr uint32_t kLibJpegWrite = 0x87f413;

// The libraries of a read / write case: 0 ijl15 whole, 1 ijl10 whole, 2 none, 3 ijl15 without ijlInit, 4 without
// ijlRead, 5 without ijlFree (the loads that fail; the write then falls back to the libjpeg writer).
void Config(Fixture &fx, uint32_t kind)
{
    impfx::Settings &s = fx.imp.settings();
    s.modules.clear();
    s.exports.clear();
    if (kind == 2) return;
    const char *const module = kind == 1 ? "ijl10.dll" : "ijl15.dll";
    s.modules.emplace_back(module);
    for (const impfx::ExportDef &e : fx.all) {
        if (e.module != module) continue;
        if (kind == 3 && e.name == "ijlInit") continue;
        if (kind == 4 && e.name == "ijlRead") continue;
        if (kind == 5 && e.name == "ijlFree") continue;
        s.exports.push_back(e);
    }
}

Bytes DataBytes(std::mt19937 &r)
{
    Bytes data;
    RandomBytes(r, data, 1 + Roll(r, 70));
    return data;
}

// Read: a stream of bytes, an info block with a size, a pixel buffer of random bytes; the library answers from a script.
void RunRead(difftest::Context &ctx, uint32_t seed, bool ours)
{
    std::mt19937 r(seed);
    gScript = Script{};
    gScript.initResult = Roll(r, 8) == 0 ? 1 + Roll(r, 3) : 0;
    gScript.readResult = Roll(r, 8) == 0 ? 1 + Roll(r, 3) : 0;
    gScript.wholeResult = Roll(r, 4) == 0 ? 1 + Roll(r, 3) : 0;
    gScript.freeResult = Roll(r, 8) == 0 ? 1 : 0;
    if (Roll(r, 6) == 0) gScript.dibChannelsAfter = Roll(r, 2) ? 4 : 3; // the library changing the count: the original reads it back
    static const int32_t kChannels[] = {1, 3, 4, 4, 3, 1, 0, 2, 5, 4, 3};
    gScript.channels = kChannels[Roll(r, 11)];
    int32_t width = static_cast<int32_t>(Roll(r, 12));
    int32_t height = static_cast<int32_t>(Roll(r, 10));
    if (Roll(r, 12) == 0) width = -static_cast<int32_t>(Roll(r, 4));
    if (Roll(r, 12) == 0) height = -static_cast<int32_t>(Roll(r, 4));
    const int32_t rowNeed = 4 * std::max(width, 0);
    int32_t pitch = rowNeed + static_cast<int32_t>(Roll(r, 9));
    if (Roll(r, 10) == 0) pitch = static_cast<int32_t>(Roll(r, 60));
    gFill.seed(r());

    Bytes data = DataBytes(r);
    Stream stream(data);
    stream.file.pos = Roll(r, 3) == 0 ? Roll(r, static_cast<uint32_t>(data.size()) + 1) : 0;
    gimex::Instance instance{};
    instance.stream = stream.Get();
    instance.unknown14 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data.data()));
    std::vector<uint8_t> info(sizeof(gimex::Info));
    gimex::Info *const header = reinterpret_cast<gimex::Info *>(info.data());
    header->width = width;
    header->height = height;

    const size_t bytes = static_cast<size_t>(std::max(pitch, 0)) * static_cast<size_t>(std::max(height, 0)) +
                         static_cast<size_t>(rowNeed) + 64;
    Bytes pixels;
    RandomBytes(r, pixels, bytes);
    gPixelBase = pixels.data();
    gPixelBytes = pixels.size();

    if ((ours ? gimex::ijl::Load() : ctx.Original<int32_t(__cdecl *)()>(kLoad)()) == 0) drawfx::Note("load failed", 1);
    const int32_t result = ours ? gimex::ijl::ReadFrame(&instance, header, pixels.data(), pitch)
                                : ctx.Original<gimex::ReadFn>(kRead)(&instance, header, pixels.data(), pitch);
    drawfx::Note("read", {static_cast<uint32_t>(result), Hash(pixels.data(), pixels.size()), Hash(info.data(), info.size()),
                          stream.file.pos, static_cast<uint32_t>(stream.file.stray)});
    NoteCells();
    gPixelBase = nullptr;
    gPixelBytes = 0;
}

// Write's allocator: the original's 0x87da65 / 0x87da72 are replaced (CodePatch) on both sides by a fake that notes each
// request and answers from malloc, failing a scripted call, so the sizes asked for and the order of the frees are seen.
struct Heap {
    std::vector<void *> blocks;
    int calls = 0;
    int failAt = 0;
};
Heap gHeap;

void *__stdcall FakeAllocate(uint32_t size)
{
    const int index = ++gHeap.calls;
    drawfx::Note("allocate", size);
    if (index == gHeap.failAt || size > 0x4000000) return nullptr;
    void *const block = std::malloc(size != 0 ? size : 1);
    std::memset(block, 0xa5, size != 0 ? size : 1);
    gHeap.blocks.push_back(block);
    return block;
}

int32_t __stdcall FakeRelease(void *block)
{
    uint32_t ordinal = 0;
    for (size_t i = 0; i < gHeap.blocks.size(); ++i) {
        if (gHeap.blocks[i] == block && block != nullptr) ordinal = static_cast<uint32_t>(i) + 1;
    }
    drawfx::Note("release", ordinal);
    if (ordinal != 0) {
        std::free(block);
        gHeap.blocks[ordinal - 1] = nullptr;
    }
    return 1;
}

struct WriteArguments {
    gimex::Instance *instance;
    const gimex::Info *info;
    const void *pixels;
};
WriteArguments gWriteArguments;

// The libjpeg writer the DLL-less path falls back to.
int32_t __stdcall FakeLibJpegWrite(gimex::Instance *instance, const gimex::Info *info, const void *pixels, int32_t pitch)
{
    drawfx::Note("libjpeg write", {instance == gWriteArguments.instance ? 1u : 0u, info == gWriteArguments.info ? 1u : 0u,
                                   pixels == gWriteArguments.pixels ? 1u : 0u, static_cast<uint32_t>(pitch)});
    return static_cast<int32_t>(gScript.fallbackResult);
}

void RunWrite(difftest::Context &ctx, uint32_t seed, bool ours)
{
    std::mt19937 r(seed);
    gScript = Script{};
    gScript.initResult = Roll(r, 8) == 0 ? 1 + Roll(r, 3) : 0;
    gScript.freeResult = Roll(r, 8) == 0 ? 1 : 0;
    gScript.writeResult = Roll(r, 6) == 0 ? 1 + Roll(r, 3) : 0;
    gScript.writeBytes = Roll(r, 4) == 0 ? Roll(r, 20) : Roll(r, 3000);
    gScript.writeOffset = Roll(r, 2) ? 0 : 12;
    gScript.reportedSize = static_cast<int32_t>(Roll(r, 5) == 0 ? Roll(r, 30) : Roll(r, 3000));
    if (Roll(r, 12) == 0) gScript.reportedSize = -1 - static_cast<int32_t>(Roll(r, 5));
    gScript.fallbackResult = Roll(r, 3) == 0 ? 0 : 1 + Roll(r, 200);
    gFill.seed(r());
    gHeap = Heap{};
    const uint32_t failure = Roll(r, 6);
    gHeap.failAt = failure < 2 ? static_cast<int>(failure) + 1 : 0;

    int32_t width = static_cast<int32_t>(Roll(r, 14));
    int32_t height = static_cast<int32_t>(Roll(r, 12));
    if (Roll(r, 20) == 0) {
        width = static_cast<int32_t>(60 + Roll(r, 60));
        height = static_cast<int32_t>(20 + Roll(r, 30));
    }
    if (Roll(r, 14) == 0) width = -static_cast<int32_t>(Roll(r, 4));
    if (Roll(r, 14) == 0) height = -static_cast<int32_t>(Roll(r, 4));
    static const int32_t kBits[] = {32, 32, 32, 24, 9, 8, 7, 0, -4, 16};
    const int32_t bits = kBits[Roll(r, 10)];
    const int32_t rowNeed = 4 * std::max(width, 0);
    int32_t pitch = rowNeed + static_cast<int32_t>(Roll(r, 9));
    if (Roll(r, 10) == 0) pitch = static_cast<int32_t>(Roll(r, 60));
    static const int32_t kQualities[] = {0, 1, 74, 75, 76, 100, 101, 50, 90, -5, 0x7fffffff};
    std::vector<uint8_t> info(sizeof(gimex::Info));
    gimex::Info *const header = reinterpret_cast<gimex::Info *>(info.data());
    header->width = width;
    header->height = height;
    header->bitsPerPixel = bits;
    header->jpegFlag = Roll(r, 3) == 0 ? 0 : 1 + Roll(r, 4);
    header->quality = static_cast<uint32_t>(kQualities[Roll(r, 11)]);
    if (Roll(r, 6) == 0) header->quality = r();
    const size_t bytes = static_cast<size_t>(std::max(pitch, 0)) * static_cast<size_t>(std::max(height, 0)) +
                         static_cast<size_t>(rowNeed) + 64;
    Bytes pixels;
    RandomBytes(r, pixels, bytes);
    Bytes data = DataBytes(r);
    Stream stream(data);
    if (Roll(r, 5) == 0) stream.file.writeLimit = Roll(r, 700);
    gimex::Instance instance{};
    instance.stream = stream.Get();
    gWriteArguments = {&instance, header, pixels.data()};

    const int32_t result = ours ? gimex::ijl::WriteFrame(&instance, header, pixels.data(), pitch)
                                : ctx.Original<gimex::WriteFn>(kWrite)(&instance, header, pixels.data(), pitch);
    drawfx::Note("write", {static_cast<uint32_t>(result), Hash(stream.file.written.data(), stream.file.written.size()),
                           static_cast<uint32_t>(stream.file.written.size()), static_cast<uint32_t>(stream.file.stray),
                           Hash(info.data(), info.size())});
    NoteCells();
    for (void *block : gHeap.blocks) std::free(block);
    gHeap = Heap{};
}

void ReadWriteCases(difftest::Context &ctx, uint32_t seed, bool write, int cases)
{
    Fixture fx(ctx);
    shape::CodePatch allocate{kAllocate, reinterpret_cast<const void *>(&FakeAllocate)};
    shape::CodePatch release{kFreeMemory, reinterpret_cast<const void *>(&FakeRelease)};
    // Ours call our allocator (lane MP), so it gets the same fake.
    shape::CodePatch ourAllocate{reinterpret_cast<void *>(&gimex::Allocate), reinterpret_cast<const void *>(&FakeAllocate)};
    shape::CodePatch ourRelease{reinterpret_cast<void *>(&gimex::FreeMemory), reinterpret_cast<const void *>(&FakeRelease)};
    shape::StandIn fallback{kLibJpegWrite, &gimex::jpeg::WriteFrame, &FakeLibJpegWrite};
    std::mt19937 rng(seed);
    for (int c = 0; c < cases; ++c) {
        const uint32_t caseSeed = rng();
        std::mt19937 kindRng(caseSeed ^ 0x9e3779b9u);
        // Reads need the entry points loaded (the original calls through the cells); writes mostly so.
        const uint32_t kind = write ? (Roll(kindRng, 3) == 0 ? 2 + Roll(kindRng, 4) : Roll(kindRng, 2)) : Roll(kindRng, 2);
        Config(fx, kind);
        auto side = [&](bool ours) {
            return drawfx::Side(
                [&] {
                    fx.imp.BeginSide();
                    ResetCells();
                },
                [&] {
                    if (write) {
                        RunWrite(ctx, caseSeed, ours);
                    } else {
                        RunRead(ctx, caseSeed, ours);
                    }
                });
        };
        const auto original = side(false);
        const auto mine = side(true);
        ResetCells();
        char what[48];
        std::snprintf(what, sizeof(what), "ijl %s case %d", write ? "write" : "read", c);
        drawfx::Same(ctx, original.first, mine.first, what, caseSeed, original.second);
        if (ctx.failures.size() >= 10) return;
    }
}

} // namespace

// The read 0x87fc52: 1, 3, 4 and odd channel counts, the premultiplying 4-channel path and the in-place 3 to 4 expansion,
// widths and heights including zero and negative, pitches at and below the row size, the library failing at each call.
DIFF_TEST(Gimex_IjlRead, 0x87fc52)
{
    ReadWriteCases(ctx, 0x87fc52, false, 1500);
}

// The write 0x87fe74: the 4 to 3 byte copy, the buffer's size and label, the library's properties, the stream write, the
// frees, an allocation failing at each of the two buffers, the DLL missing (the libjpeg fallback). Inputs kept out: a
// loaded DLL without ijlWrite (the original calls through a null cell: ORIGINAL-BUG, not run).
DIFF_TEST(Gimex_IjlWrite, 0x87fe74)
{
    ReadWriteCases(ctx, 0x87fe74, true, 2500);
}

// The load 0x87f7a8 and the unload 0x87f790, through about and is and directly.
DIFF_TEST(Gimex_IjlLoad, 0x87f7a8)
{
    Cases(ctx, 0x87f7a8, kOpAbout | kOpIs | kOpLoad | kOpUnload, 600);
}

DIFF_TEST(Gimex_IjlAbout, 0x87f864)
{
    Cases(ctx, 0x87f864, kOpAbout | kOpLoad | kOpUnload, 400);
}

DIFF_TEST(Gimex_IjlIs, 0x87fb72)
{
    Cases(ctx, 0x87fb72, kOpIs | kOpUnload | kOpLoad, 500);
}

// Open 0x87fbcb and close 0x87f9b2.
DIFF_TEST(Gimex_IjlOpenClose, 0x87fbcb)
{
    Cases(ctx, 0x87fbcb, kOpOpen | kOpClose | kOpLoad | kOpUnload, 600);
}

DIFF_TEST(Gimex_IjlInfo, 0x87f9e0)
{
    Cases(ctx, 0x87f9e0, kOpOpen | kOpInfo | kOpClose | kOpLoad, 800);
}
