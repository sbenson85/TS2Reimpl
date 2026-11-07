// The FSH back end's block encoder driver (lane MS; src/gz/GimexFshEncode.h, 0x8988f3): cuts a BGRA image into 4 x 4 blocks and writes
// each as an optional alpha block (4-bit, or fitted 3-bit with two end points) followed by a colour block (two 16-bit colours and a
// 32-bit word of 2-bit indices), fitting the colours with FitColours and the alpha with FitAlpha.
#include "gz/GimexFshEncode.h"

#include <cstring>

namespace gimex {

namespace fsh {

namespace {

constexpr int32_t kBlockSide = 4;
constexpr int32_t kBlockPixels = 16;
constexpr uint32_t kKindMask = 0xff00;
constexpr uint32_t kModeMask = 0xff;
constexpr uint32_t kKindAlpha4 = 0x100;
constexpr uint32_t kKindAlphaFitted = 0x200;
constexpr uint32_t kFlagAllowZero = 0x10000;
constexpr uint32_t kFlagAllowOne = 0x20000;
constexpr int32_t kModeOpaqueMax = 1;           // modes 0 and 1: every pixel; 2: only pixels with alpha above kAlphaFloor
constexpr int32_t kModeAlphaFloor = 2;
constexpr uint8_t kAlphaFloor = 0x40;
constexpr uint8_t kAlphaOpaque = 0xff;
constexpr double kByteToUnit = 1.0 / 255.0;
constexpr float kUnitOne = 1.0f;
constexpr uint32_t kAlphaRound = 0x80;
constexpr uint32_t kAlpha4Scale = 15;
constexpr uint32_t kAlpha4Divisor = 0xff;
constexpr int32_t kAlphaLevels6 = 6;
constexpr int32_t kAlphaLevels8 = 8;
constexpr int32_t kLevelsThree = 3;
constexpr int32_t kLevelsFour = 4;
constexpr int32_t kNoSlot = -1;
constexpr uint32_t kRedBits = 6;
constexpr uint32_t kGreenBits = 5;
constexpr uint32_t kCodeShift = 21;             // a 3-bit alpha code enters at the top of a 24-bit group
constexpr uint32_t kCodeBits = 3;
constexpr uint32_t kBlue4444Fill = 0xffe0;
constexpr uint32_t kGreen4444Mask = 0xfffe;
constexpr uint32_t kBlue4444Shift = 6;
constexpr uint32_t kRed4444Shift = 4;

void PutWord(uint8_t *out, uint16_t value, bool bigEndian)
{
    if (bigEndian) {
        out[0] = static_cast<uint8_t>(value >> 8);
        out[1] = static_cast<uint8_t>(value);
    } else {
        std::memcpy(out, &value, sizeof value);
    }
}

// The alpha code for a swapped pair of end points: code i stands for the same value on the other side.
uint32_t SwapCode(uint32_t code, int32_t levels)
{
    if (levels == kAlphaLevels8) {
        uint32_t swapped = code == 0 ? 1u : 0u;
        if (static_cast<int32_t>(code) > 1) swapped = 9 - code;
        return swapped;
    }
    if (levels == kAlphaLevels6) {
        uint32_t swapped = code == 0 ? 1u : 0u;
        if (static_cast<int32_t>(code) > 1) swapped = 7 - code;
        if (static_cast<int32_t>(code) > 5) swapped = code;
        return swapped;
    }
    return code;
}

} // namespace

// 0x8988f3
int32_t __cdecl EncodeBlocks(int32_t width, int32_t rows, uint32_t pitch, const uint8_t *source, uint8_t *dest, uint32_t flags,
                             const float *weights, int32_t form4444, int32_t bigEndian)
{
    uint8_t *out = dest;
    // The slot of each block pixel in the colour list (or kNoSlot) lives on the original's stack across blocks: modes the colour
    // pass ignores leave the previous block's slots in place.
    int32_t slots[kBlockPixels] = {};
    if (rows > 0) {
        const int32_t extra = static_cast<int32_t>((pitch >> 2) - kBlockSide) * 4; // the bytes from a block row's end to the next row
        const uint32_t blockRowBytes = pitch << 2;
        const uint32_t kind = flags & kKindMask;
        const uint32_t mode = flags & kModeMask;
        for (int32_t y = 0; y < rows; y += kBlockSide) {
            const int32_t height = rows - y < kBlockSide ? rows - y : kBlockSide;
            const uint8_t *block = source;
            if (width > 0) {
                for (int32_t x = 0; x < width; x += kBlockSide) {
                    const int32_t blockWidth = width - x < kBlockSide ? width - x : kBlockSide;
                    ColourFit fit = {};
                    for (int32_t i = 0; i < 3; ++i) fit.weights[i] = static_cast<double>(weights[i]);

                    if (kind == kKindAlpha4) {
                        const uint8_t *p = block;
                        for (int32_t r = 0; r < kBlockSide; ++r) {
                            uint16_t value = 0;
                            if (r < height) {
                                for (int32_t c = 0; c < kBlockSide; ++c) {
                                    const uint16_t shifted = static_cast<uint16_t>(value >> 4);
                                    if (c < blockWidth)
                                        value = static_cast<uint16_t>(((p[3] * kAlpha4Scale + kAlphaRound) / kAlpha4Divisor) << 12 | shifted);
                                    else
                                        value = shifted;
                                    p += 4;
                                }
                            }
                            PutWord(out, value, false);
                            out += 2;
                            p += extra;
                        }
                    } else if (kind == kKindAlphaFitted) {
                        AlphaFit alpha = {};
                        int32_t count = 0;
                        const uint8_t *p = block;
                        for (int32_t r = 0; r < kBlockSide; ++r) {
                            for (int32_t c = 0; c < kBlockSide; ++c) {
                                if (c < blockWidth && r < height) {
                                    const uint8_t a = p[3];
                                    slots[r * kBlockSide + c] = count;
                                    alpha.values[count] = a == kAlphaOpaque ? kUnitOne : static_cast<float>(a * kByteToUnit);
                                    ++count;
                                } else {
                                    slots[r * kBlockSide + c] = kNoSlot;
                                }
                                p += 4;
                            }
                            p += extra;
                        }
                        alpha.count = count;
                        alpha.allowZero = static_cast<int32_t>(flags & kFlagAllowZero);
                        alpha.allowOne = static_cast<int32_t>(flags & kFlagAllowOne);
                        bool forced = false;
                        if (count == 0) {
                            alpha.endpoints[0] = 0;
                            alpha.endpoints[1] = 0;
                            forced = true;
                        } else {
                            FitAlpha(&alpha);
                            forced = alpha.endpoints[0] == alpha.endpoints[1];
                        }
                        if (forced) {
                            alpha.endpoints[1] = alpha.endpoints[1] + 1;
                            for (int32_t i = 0; i < count; ++i) alpha.codes[i] = 0;
                            alpha.levels = kAlphaLevels6;
                        }
                        const bool swapped = (alpha.endpoints[0] > alpha.endpoints[1]) == (alpha.levels == kAlphaLevels6);
                        if (swapped) {
                            const int32_t held = alpha.endpoints[0];
                            alpha.endpoints[0] = alpha.endpoints[1];
                            alpha.endpoints[1] = held;
                        }
                        out[0] = static_cast<uint8_t>(alpha.endpoints[0]);
                        out[1] = static_cast<uint8_t>(alpha.endpoints[1]);
                        out += 2;
                        uint32_t group = 0;
                        int32_t slot = 0;
                        for (int32_t j = 0; j < kBlockSide; ++j) {
                            for (int32_t k = 0; k < kBlockSide; ++k) {
                                group >>= kCodeBits;
                                if (slots[slot] >= 0) {
                                    uint32_t code = static_cast<uint32_t>(alpha.codes[slots[slot]]);
                                    if (swapped) code = SwapCode(code, alpha.levels);
                                    group |= code << kCodeShift;
                                }
                                ++slot;
                            }
                            if ((j & 1) != 0) {
                                out[0] = static_cast<uint8_t>(group);
                                out[1] = static_cast<uint8_t>(group >> 8);
                                out[2] = static_cast<uint8_t>(group >> 16);
                                out += 3;
                                group = 0;
                            }
                        }
                    }

                    // The colours of the block's pixels (every pixel in modes 0 and 1, those with alpha above 0x40 in mode 2).
                    int32_t colourCount = 0;
                    if (static_cast<int32_t>(mode) <= kModeOpaqueMax || static_cast<int32_t>(mode) == kModeAlphaFloor) {
                        const uint8_t *p = block;
                        for (int32_t r = 0; r < kBlockSide; ++r) {
                            for (int32_t c = 0; c < kBlockSide; ++c) {
                                const bool taken = c < blockWidth && r < height && (static_cast<int32_t>(mode) <= kModeOpaqueMax || p[3] > kAlphaFloor);
                                if (taken) {
                                    slots[r * kBlockSide + c] = colourCount;
                                    fit.colours[colourCount][0] = static_cast<double>(p[2]) * kByteToUnit;
                                    fit.colours[colourCount][1] = static_cast<double>(p[1]) * kByteToUnit;
                                    fit.colours[colourCount][2] = static_cast<double>(p[0]) * kByteToUnit;
                                    ++colourCount;
                                    fit.count = colourCount;
                                } else {
                                    slots[r * kBlockSide + c] = kNoSlot;
                                }
                                p += 4;
                            }
                            p += extra;
                        }
                    }
                    fit.maxLevels = (colourCount >= blockWidth * height ? 1 : 0) + kLevelsThree;
                    if (colourCount == 0) {
                        std::memset(fit.endpoints, 0, sizeof fit.endpoints);
                    } else {
                        FitColours(&fit);
                    }

                    uint16_t ends[2];
                    if (form4444 == 0) {
                        ends[0] = static_cast<uint16_t>(((static_cast<uint32_t>(fit.endpoints[0][0]) << kRedBits | static_cast<uint32_t>(fit.endpoints[0][1]))
                                                         << kGreenBits) |
                                                        static_cast<uint32_t>(fit.endpoints[0][2]));
                        ends[1] = static_cast<uint16_t>(((static_cast<uint32_t>(fit.endpoints[1][0]) << kRedBits | static_cast<uint32_t>(fit.endpoints[1][1]))
                                                         << kGreenBits) |
                                                        static_cast<uint32_t>(fit.endpoints[1][2]));
                    } else {
                        for (int32_t e = 0; e < 2; ++e) {
                            uint16_t high = static_cast<uint16_t>(kBlue4444Fill | static_cast<uint16_t>(fit.endpoints[e][2]));
                            high = static_cast<uint16_t>(high << kBlue4444Shift);
                            const uint16_t green = static_cast<uint16_t>(static_cast<uint16_t>(fit.endpoints[e][1]) & kGreen4444Mask);
                            high = static_cast<uint16_t>(high | green);
                            high = static_cast<uint16_t>(high << kRed4444Shift);
                            ends[e] = static_cast<uint16_t>(high | static_cast<uint16_t>(fit.endpoints[e][0]));
                        }
                    }

                    int32_t fitLevels = fit.levels;
                    if (ends[0] == ends[1]) {
                        ends[1] = static_cast<uint16_t>(ends[1] + 1);
                        std::memset(fit.indices, 0, static_cast<size_t>(fit.count) * sizeof(int32_t));
                        fitLevels = kLevelsThree;
                        fit.levels = kLevelsThree;
                    }
                    bool swapped = false;
                    if ((ends[1] < ends[0]) == (fitLevels == kLevelsThree)) {
                        const uint16_t held = ends[0];
                        ends[0] = ends[1];
                        ends[1] = held;
                        swapped = true;
                    }
                    for (int32_t e = 0; e < 2; ++e) {
                        PutWord(out, ends[e], bigEndian != 0);
                        out += 2;
                    }

                    uint32_t word = 0;
                    auto addIndex = [&](int32_t slot) {
                        word <<= 2;
                        if (slot < 0) {
                            word |= 3;
                        } else {
                            word |= static_cast<uint32_t>(fit.indices[slot]);
                            if (swapped) {
                                if (fitLevels == kLevelsFour)
                                    word ^= 1;
                                else
                                    word ^= ~(word >> 1) & 1;
                            }
                        }
                    };
                    if (bigEndian == 0) {
                        for (int32_t k = kBlockPixels - 1; k >= 0; --k) addIndex(slots[k]);
                    } else {
                        for (int32_t row = kBlockSide - 1; row >= 0; --row)
                            for (int32_t k = 0; k < kBlockSide; ++k) addIndex(slots[row * kBlockSide + k]);
                    }
                    std::memcpy(out, &word, sizeof word);
                    out += sizeof word;
                    block += 4 * kBlockSide;
                }
            }
            source += blockRowBytes;
        }
    }
    return static_cast<int32_t>(out - dest);
}

} // namespace fsh

} // namespace gimex
