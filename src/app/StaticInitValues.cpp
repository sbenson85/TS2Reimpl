// Owned XC entries of the "value" kind (lane GQ; docs/subsystems/startup.md "Stage 3: the static initialisers"):
// a global set from constants or from other globals, no calls. Hosted, the sources are read where the image's
// entry reads them (the image's .data, which earlier entries may have written: slots 364 and 799 read the global
// the entry before them wrote); the constants the image's entries take from .rdata (the sign mask 0xd9c6f0, 0.5f
// at 0xd9c694, 1.4142f at 0xdcc3b8) are literals here. Standalone, each becomes a constexpr / constinit static.
#include "app/StaticInit.h"

#include "game/game.h"

#include <array>
#include <cstring>
#include <iterator>

namespace staticinit {

namespace {

constexpr uint32_t kSignBit = 0x80000000;   // 0xd9c6f0 (xorps: negation)
constexpr float kHalf = 0.5f;               // 0xd9c694
constexpr uint32_t kSqrt2Bits = 0x3fb4fdf4; // 0xdcc3b8, 1.41399...

uint32_t Load(uint32_t rva)
{
    uint32_t v;
    std::memcpy(&v, GameAddress<uint8_t>(rva), 4);
    return v;
}

void Store(uint32_t rva, uint32_t v)
{
    std::memcpy(GameAddress<uint8_t>(rva), &v, 4);
}

float LoadFloat(uint32_t rva)
{
    float f;
    std::memcpy(&f, GameAddress<uint8_t>(rva), 4);
    return f;
}

void StoreFloat(uint32_t rva, float f)
{
    std::memcpy(GameAddress<uint8_t>(rva), &f, 4);
}

// Slot order; image entry, target and source checked against each entry's code (and by the difftest).
constexpr ValueStatic kValues[] = {
    {4, 0x138358, 0x10cb5b0, 0x10cb098, ValueOp::StoreAddress, 0},
    {173, 0x1393a0, 0x10d93a8, 0xffd900, ValueOp::CopyAndNegateTriples, 3},
    {320, 0x13ca83, 0x10dc100, 0, ValueOp::StoreConstant, 0xffdee8e3},
    {321, 0x13ca59, 0x10dc104, 0, ValueOp::StoreConstant, 0xffdee8e3},
    {322, 0x13ca6e, 0x10dc108, 0, ValueOp::StoreConstant, 0xffdee8e3},
    {325, 0x13cabc, 0x10dc334, 0x101bf68, ValueOp::Length, 0},
    {345, 0x13cd08, 0x10dc94c, 0x1011cdc, ValueOp::Copy, 1},
    {346, 0x13cd13, 0x10dc950, 0x1011ce0, ValueOp::Copy, 1},
    {364, 0x13cff8, 0x10dcdc8, 0x10dcdc4, ValueOp::ScaleBySqrt2, 0},
    {368, 0x13d0cc, 0x10dd02c, 0x1011cdc, ValueOp::Copy, 1},
    {375, 0x13d1df, 0x10dd28c, 0x1011cdc, ValueOp::Copy, 1},
    {376, 0x13d1ea, 0x10dd3a4, 0x1011cd8, ValueOp::Copy, 1},
    {484, 0x13de21, 0x10ded58, 0x1011cd8, ValueOp::Copy, 1},
    {503, 0x13e0e0, 0x10ecc88, 0x105f808, ValueOp::Copy, 1},
    {530, 0x13e430, 0x10f5030, 0, ValueOp::StoreConstant, 0x46504244}, // the image byte-swaps 'FPBD' at run time
    {531, 0x13e460, 0x10f5110, 0x1062434, ValueOp::Copy, 1},
    {532, 0x13e450, 0x10f5114, 0x1062438, ValueOp::Copy, 1},
    {551, 0x13e6a0, 0x10f5c68, 0x10654b4, ValueOp::Box, 0},
    {552, 0x13e5a0, 0x10f5c8c, 0x10654c0, ValueOp::Box, 0},
    {553, 0x13e720, 0x10f5cb0, 0x10654e4, ValueOp::Box, 0},
    {554, 0x13e620, 0x10f5cd4, 0x10654f0, ValueOp::Box, 0},
    {677, 0x13ffb0, 0x10fb36c, 0x1079b60, ValueOp::Copy, 3},
    {679, 0x13fff0, 0x10fc484, 0x107b620, ValueOp::Length, 0},
    {797, 0x140e70, 0x1107d1c, 0x108ab98, ValueOp::Negate, 3},
    {799, 0x140ee0, 0x1107e20, 0x1107e1c, ValueOp::ScaleBySqrt2, 0},
};

constexpr auto kOwned = [] {
    std::array<winmainstart::OwnedInitialiser, std::size(kValues)> out{};
    for (size_t i = 0; i < std::size(kValues); ++i) out[i] = {kValues[i].slot, kValues[i].imageRva, &InitValue, &kValues[i]};
    return out;
}();

} // namespace

void __cdecl InitValue(const void *valueStatic)
{
    const auto &e = *static_cast<const ValueStatic *>(valueStatic);
    switch (e.op) {
    case ValueOp::StoreConstant:
        Store(e.target, e.constant);
        break;
    case ValueOp::StoreAddress:
        Store(e.target, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GameAddress<void>(e.source))));
        break;
    case ValueOp::Copy:
        for (uint32_t i = 0; i < e.constant; ++i) Store(e.target + 4 * i, Load(e.source + 4 * i));
        break;
    case ValueOp::Negate:
        for (uint32_t i = 0; i < e.constant; ++i) Store(e.target + 4 * i, Load(e.source + 4 * i) ^ kSignBit);
        break;
    case ValueOp::CopyAndNegateTriples:
        // Per source triple: the triple, then its negation (three pairs of bounds: +v, -v).
        for (uint32_t t = 0; t < e.constant; ++t) {
            for (uint32_t i = 0; i < 3; ++i) {
                uint32_t v = Load(e.source + 12 * t + 4 * i);
                Store(e.target + 24 * t + 4 * i, v);
                Store(e.target + 24 * t + 12 + 4 * i, v ^ kSignBit);
            }
        }
        break;
    case ValueOp::ScaleBySqrt2: {
        float k;
        std::memcpy(&k, &kSqrt2Bits, 4);
        StoreFloat(e.target, LoadFloat(e.source) * k);
        break;
    }
    case ValueOp::Length:
        Store(e.target, static_cast<uint32_t>(std::strlen(*GameAddress<const char *>(e.source))));
        break;
    case ValueOp::Box:
        // From a source of two vectors (a at +0, b at +0x18; 0x30 bytes): b, a, then b * 0.5.
        for (uint32_t i = 0; i < 3; ++i) {
            Store(e.target + 4 * i, Load(e.source + 0x18 + 4 * i));
            Store(e.target + 0xc + 4 * i, Load(e.source + 4 * i));
            StoreFloat(e.target + 0x18 + 4 * i, LoadFloat(e.source + 0x18 + 4 * i) * kHalf);
        }
        break;
    }
}

const ValueStatic *ValueStatics(size_t *count)
{
    *count = std::size(kValues);
    return kValues;
}

const winmainstart::OwnedInitialiser *OwnedValues(size_t *count)
{
    *count = kOwned.size();
    return kOwned.data();
}

} // namespace staticinit
