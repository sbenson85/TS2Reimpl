// Differential tests for cGZBufferFontRenderer's code outside its slots (src/gz/cGZBufferFontBlit.h, lane LS): the
// constructor 0x90a7b5, the blend table builder 0x90e1aa, the glyph blitter 0x90bbba and the 16-bit blends 0x90c7ba
// (565) / 0x90c82b (4444).
//
// The blitter draws into a fake cIGZBuffer: a vtable whose slots 9, 10, 14, 16, 34 and 35 (width, height, colour type,
// bits per pixel, pixel memory, stride) answer from a script and log the call; the others trap. Each case runs both
// sides on copies of the same random pixel memory (with margins around it) and compares the memory, the log and the
// result. The blend table is the game's (built by the original builder first; the builder is compared separately).
#include "difftest.h"

#include "game/game.h"
#include "gz/cGZBufferFontBlit.h"
#include "gz/cIGZBuffer.h"

#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using Code = cGZBufferFontRendererCode;

constexpr uint32_t kConstruct = 0x90a7b5;
constexpr uint32_t kBuild = 0x90e1aa;
constexpr uint32_t kBlit = 0x90bbba;
constexpr uint32_t kBlend565 = 0x90c7ba;
constexpr uint32_t kBlend4444 = 0x90c82b;
constexpr uint32_t kTableBytes = 0x8000;

template <class Fn> Fn Game(uint32_t rva) { return reinterpret_cast<Fn>(GameAddress<void>(rva)); }

// ---- the fake buffer -----------------------------------------------------------------------------------------------

struct Script {
    uint32_t width, height, bits, format, stride;
    uint8_t *memory;
} gScript;

std::string gLog;

struct FakeBuffer {
    void **vtable;
};

uint32_t __fastcall Width(FakeBuffer *, void *)
{
    gLog += "width,";
    return gScript.width;
}
uint32_t __fastcall Height(FakeBuffer *, void *)
{
    gLog += "height,";
    return gScript.height;
}
cGZColorType *__fastcall ColorType(FakeBuffer *, void *, cGZColorType *out)
{
    gLog += "colortype,";
    out->format = gScript.format;
    out->bits = 0x77;
    return out;
}
uint32_t __fastcall Bits(FakeBuffer *, void *)
{
    gLog += "bits,";
    return gScript.bits;
}
void *__fastcall Memory(FakeBuffer *, void *)
{
    gLog += "memory,";
    return gScript.memory;
}
uint32_t __fastcall Stride(FakeBuffer *, void *)
{
    gLog += "stride,";
    return gScript.stride;
}
uint32_t __fastcall Unexpected(FakeBuffer *, void *)
{
    gLog += "UNEXPECTED,";
    return 0;
}

void *gVTable[40];
FakeBuffer gBuffer{gVTable};

void InitBuffer()
{
    for (void *&slot : gVTable) slot = reinterpret_cast<void *>(&Unexpected);
    gVTable[9] = reinterpret_cast<void *>(&Width);
    gVTable[10] = reinterpret_cast<void *>(&Height);
    gVTable[14] = reinterpret_cast<void *>(&ColorType);
    gVTable[16] = reinterpret_cast<void *>(&Bits);
    gVTable[34] = reinterpret_cast<void *>(&Memory);
    gVTable[35] = reinterpret_cast<void *>(&Stride);
}

// The game's table, built by the original (the blitter's input on both sides).
void BuildTableWithOriginal()
{
    Game<bool(__cdecl *)()>(kBuild)();
    *GameAddress<uint8_t>(Code::kTableBuilt) = 1;
}

std::string Hex(uint32_t v)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%x", v);
    return buf;
}

} // namespace

// The builder: the table filled from garbage by each side, the 0x8000 bytes and the result compared.
DIFF_TEST(BufferFontBlit_BuildBlendTable, kBuild)
{
    uint8_t *table = GameAddress<uint8_t>(Code::kBlendTable);
    std::vector<uint8_t> runs[2];
    bool results[2];
    for (int side = 0; side < 2; ++side) {
        std::memset(table, 0xa5 + side, kTableBytes);
        results[side] = side == 0 ? Game<bool(__cdecl *)()>(kBuild)() : Code::BuildBlendTable();
        runs[side].assign(table, table + kTableBytes);
    }
    ++ctx.cases;
    if (results[0] != results[1] || runs[0] != runs[1]) {
        size_t at = 0;
        while (at < kTableBytes && runs[0][at] == runs[1][at]) ++at;
        ctx.Fail("table: results %d / %d, first difference at %zx", results[0], results[1], at);
    }
}

// The constructor on 0xcd-filled memory with the table flag clear (the table built) and set (the table untouched).
DIFF_TEST(BufferFontBlit_Construct, kConstruct)
{
    uint8_t *table = GameAddress<uint8_t>(Code::kBlendTable);
    uint8_t *flag = GameAddress<uint8_t>(Code::kTableBuilt);
    for (const uint8_t initialFlag : {uint8_t{0}, uint8_t{1}, uint8_t{2}}) {
        std::string runs[2];
        for (int side = 0; side < 2; ++side) {
            std::memset(table, 0x5a, kTableBytes);
            *flag = initialFlag;
            alignas(4) uint8_t object[12];
            std::memset(object, 0xcd, sizeof(object));
            auto *self = reinterpret_cast<cGZBufferFontRenderer *>(object);
            void *result = side == 0 ? Game<void *(__thiscall *)(void *)>(kConstruct)(object)
                                     : static_cast<void *>(Code::Construct(self, nullptr));
            std::string &r = runs[side];
            r = result == object ? "this" : "other";
            r += " vt " + std::string(self->mVTable == GameAddress<void>(Code::kVTable) ? "game" : "other");
            for (int at = 4; at < 12; at += 4) r += " " + Hex(*reinterpret_cast<uint32_t *>(object + at));
            r += " flag " + std::to_string(*flag);
            uint32_t sum = 0;
            for (uint32_t i = 0; i < kTableBytes; ++i) sum = sum * 31 + table[i];
            r += " table " + Hex(sum);
        }
        ++ctx.cases;
        if (runs[0] != runs[1]) ctx.Fail("flag %u\n  orig: %s\n  ours: %s", initialFlag, runs[0].c_str(), runs[1].c_str());
    }
    BuildTableWithOriginal();
}

// The 16-bit blends: every alpha 0..31 with edge and random colours.
DIFF_TEST(BufferFontBlit_Blend16, kBlend565)
{
    BuildTableWithOriginal();
    std::mt19937 rng(0x90c7ba);
    const uint32_t edges[] = {0, 1, 0x1f, 0x20, 0x7e0, 0x7ff, 0x800, 0xf800, 0xffff, 0xf, 0xf0, 0xf00, 0xf000, 0x7fff};
    for (int which = 0; which < 2; ++which) {
        auto original = Game<uint16_t(__cdecl *)(uint32_t, uint32_t, uint32_t)>(which ? kBlend4444 : kBlend565);
        auto ours = which ? &Code::Blend4444 : &Code::Blend565;
        uint32_t failures = 0;
        for (int i = 0; i < 20000; ++i) {
            const uint32_t colour = i < 196 ? edges[i % 14] : rng() & 0xffff;
            const uint32_t destination = i < 196 ? edges[i / 14] : rng() & 0xffff;
            for (uint32_t alpha = 0; alpha < 32; ++alpha) {
                const uint16_t a = original(colour, destination, alpha);
                const uint16_t b = ours(colour, destination, alpha);
                ++ctx.cases;
                if (a != b && failures++ < 5)
                    ctx.Fail("%s %x over %x alpha %u: %x / %x", which ? "4444" : "565", colour, destination, alpha, a, b);
            }
        }
    }
}

// The blitter: random buffers (8, 16, 24 and 32 bits, colour types 3..8, strides with padding), destination
// rectangles in and around the buffer, source origins, clips (none, inside, crossing), modes 0..4, glyph coverage with
// zeros, edges (0x7f / 0x80) and random values, and palettes.
DIFF_TEST(BufferFontBlit_Blit, kBlit)
{
    InitBuffer();
    BuildTableWithOriginal();
    std::mt19937 rng(0x90bbba);
    using BlitFn = bool(__thiscall *)(void *, uint32_t, void *, const int32_t *, uint32_t, const uint8_t *,
                                      const int32_t *, int32_t, uint32_t, const uint32_t *, const int32_t *);
    auto original = Game<BlitFn>(kBlit);
    const uint32_t bitsChoices[] = {16, 16, 32, 32, 24, 8};
    constexpr int32_t kGlyph = 48;
    constexpr uint32_t kMargin = 64;
    uint32_t failures = 0;
    for (int trial = 0; trial < 4000; ++trial) {
        Script s{};
        s.width = 1 + rng() % 40;
        s.height = 1 + rng() % 30;
        s.bits = bitsChoices[rng() % 6];
        s.format = 3 + rng() % 6;
        const uint32_t bytes = s.bits == 16 ? 2 : 4;
        s.stride = s.width * bytes + bytes * (rng() % 3);
        const uint32_t mode = rng() % 5;

        std::vector<uint8_t> glyph(kGlyph * 128); // rows enough for any source origin the clips make
        for (uint8_t &c : glyph) {
            const uint32_t pick = rng() % 6;
            c = pick == 0 ? 0 : pick == 1 ? 0x7f : pick == 2 ? 0x80 : pick == 3 ? 0xff : static_cast<uint8_t>(rng());
        }
        const int32_t pitch = kGlyph;
        const int32_t src[4] = {static_cast<int32_t>(rng() % 8), static_cast<int32_t>(rng() % 8), 0x11111, 0x22222};
        int32_t dest[4];
        dest[0] = static_cast<int32_t>(rng() % 60) - 15;
        dest[1] = static_cast<int32_t>(rng() % 45) - 10;
        dest[2] = dest[0] + static_cast<int32_t>(rng() % 33) - (rng() % 8 == 0 ? 3 : 0);
        dest[3] = dest[1] + static_cast<int32_t>(rng() % 33) - (rng() % 8 == 0 ? 3 : 0);
        int32_t clipRect[4] = {static_cast<int32_t>(rng() % 50) - 10, static_cast<int32_t>(rng() % 40) - 10, 0, 0};
        clipRect[2] = clipRect[0] + static_cast<int32_t>(rng() % 45);
        clipRect[3] = clipRect[1] + static_cast<int32_t>(rng() % 35);
        const int32_t *clip = rng() % 3 == 0 ? nullptr : clipRect;
        uint32_t palette[256];
        for (uint32_t &p : palette) p = rng();

        const size_t memoryBytes = s.stride * s.height + 2 * kMargin;
        std::vector<uint8_t> initial(memoryBytes);
        for (uint8_t &c : initial) c = static_cast<uint8_t>(rng());
        std::vector<uint8_t> memory[2] = {initial, initial};
        std::string logs[2];
        bool results[2];
        for (int side = 0; side < 2; ++side) {
            gScript = s;
            gScript.memory = memory[side].data() + kMargin;
            gLog.clear();
            uint32_t self[2] = {0, 0};
            results[side] = side == 0 ? original(self, 0x1234, &gBuffer, dest, 0x41, glyph.data(), src, pitch, mode,
                                                 palette, clip)
                                      : reinterpret_cast<Code *>(self)->Blit(0x1234, reinterpret_cast<cIGZBuffer *>(&gBuffer),
                                                                             dest, 0x41, glyph.data(), src, pitch, mode,
                                                                             palette, clip);
            logs[side] = gLog;
        }
        ++ctx.cases;
        if (results[0] != results[1] || logs[0] != logs[1] || memory[0] != memory[1]) {
            size_t at = 0;
            while (at < memoryBytes && memory[0][at] == memory[1][at]) ++at;
            if (failures++ < 8)
                ctx.Fail("trial %d: %ux%u bits %u format %u stride %u mode %u dest %d,%d,%d,%d src %d,%d clip %s "
                         "results %d/%d first byte difference %zd (margin %u)\n  orig log: %s\n  ours log: %s",
                         trial, s.width, s.height, s.bits, s.format, s.stride, mode, dest[0], dest[1], dest[2], dest[3],
                         src[0], src[1], clip ? "yes" : "no", results[0], results[1],
                         at == memoryBytes ? -1 : static_cast<ptrdiff_t>(at), kMargin, logs[0].c_str(), logs[1].c_str());
        }
    }
}
