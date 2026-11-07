// The FSH back end's colour record encoder (lane MS; src/gz/GimexFshEncode.h, 0x896e1e..0x897b53).
//
// Uninitialised memory in the original (the nondeterminism lane MP saw in the compressed kinds): 0x896e1e keeps three tables on its
// stack that it does not clear: the pixels' index bytes (16), the palette (16 entries) and the colours chosen for the entries. It
// reads them where it should not:
//  - its second pass (the 3-colour mode) classifies, for each of the first width * height entries, the colour *chosen* for that entry
//    by the first pass instead of the entry's own colour; with a transparent pixel in the block the first pass is skipped, so for
//    the first pair of end points those colours are whatever the stack held, and for every block the entries past the palette's
//    end are unwritten as well (their weight, the count byte of a palette entry that does not exist, is garbage too);
//  - the index bytes of a block narrower than four pixels: each row of the output reads four index slots whatever the width, so
//    the slots past the last pixel are read unwritten (they pick the code of a garbage palette index).
// With a zeroed stack the original behaves as if those tables were zero, and so does this code (every table is zero-initialised).
// Which output depends on it: transparent blocks with three or more colours (about 43 % of random ones) and every block narrower
// or shorter than four pixels. Reproducing the original's garbage is not possible (it differs from run to run); the tests clear the
// stack before each original call. See notes/feedback/MS.md.
#include "gz/GimexFshEncode.h"

#include "gz/GimexFshLeaves.h"

#include <cstring>

namespace gimex {

namespace fsh {

namespace {

constexpr int32_t kMaxPixels = 16;          // the largest block: 4 x 4
constexpr int32_t kBlockSide = 4;
constexpr int32_t kEndpointBytes = 4;       // two 16-bit colours
constexpr uint32_t kTransparentBelow = 0x40000000; // alpha below 0x40
constexpr uint32_t kNoError = 0xffffffff;

constexpr uint32_t kMask5 = 0x1f;
constexpr uint32_t kMask6 = 0x3f;
constexpr uint32_t kRedShift = 19;          // the top five bits of the red byte
constexpr uint32_t kGreenShift = 10;
constexpr uint32_t kBlueShift = 3;

constexpr uint32_t kThirdsMultiplier = 0xaaaa;  // x * 0xaaaa + 0x5555 >> 17: about x / 3
constexpr uint32_t kThirdsRound = 0x5555;
constexpr uint32_t kThirdsShift = 17;
constexpr uint32_t kMiddleMultiplier = 0x5555;  // (a + b) * 0x5555 >> 16: about (a + b) / 3 (the original's 3-colour middle)
constexpr uint32_t kMiddleShift = 16;

constexpr uint32_t kRedScale = 3;           // the error scales the parts by 8, 4, 8 (as shifts)
constexpr uint32_t kGreenScale = 2;
constexpr uint32_t kBlueScale = 3;

constexpr uint32_t kFlagExplicitAlpha = 0x100;
constexpr uint32_t kFlagInterpolatedAlpha = 0x200;
constexpr uint32_t kFlagPremultiplied = 4;
constexpr uint32_t kFlagBigEndian = 8;
constexpr uint8_t kType18 = 0x18;
constexpr uint8_t kType1a = 0x1a;
constexpr uint8_t kType1c = 0x1c;
constexpr uint8_t kType28 = 0x28;
constexpr uint8_t kWideBit = 0x80;
constexpr uint8_t kSecondHeaderByte = 0xfb;
constexpr int32_t kWideAbove = 16000000;
constexpr int32_t kHeaderBytes = 8;
constexpr int32_t kSizeOffset = 2;
constexpr int32_t kNarrowFlagsOffset = 5;
constexpr int32_t kWideFlagsOffset = 6;
constexpr int32_t kSizeBytes = 4;
constexpr int32_t kBlockBytes = 8;
constexpr int32_t kPixelsPerRowOfFour = 4;
constexpr uint8_t kFlagBitPremultiplied = 4;

struct Colour {
    uint8_t r;  // 5 bits
    uint8_t g;  // 6 bits
    uint8_t b;  // 5 bits
};

struct Entry {
    Colour colour;
    uint8_t count;
};

Colour FromBytes(const uint8_t *bytes)
{
    return Colour{static_cast<uint8_t>(bytes[0] & kMask5), static_cast<uint8_t>(bytes[1] & kMask6), static_cast<uint8_t>(bytes[2] & kMask5)};
}

Colour FromSlot(uint32_t slot)
{
    return Colour{static_cast<uint8_t>(slot & kMask5), static_cast<uint8_t>((slot >> 8) & kMask6), static_cast<uint8_t>((slot >> 16) & kMask5)};
}

uint32_t ToSlot(Colour colour)
{
    return static_cast<uint32_t>(colour.r) | static_cast<uint32_t>(colour.g) << 8 | static_cast<uint32_t>(colour.b) << 16;
}

uint16_t Pack(Colour colour)
{
    return static_cast<uint16_t>(((colour.r << 6 | colour.g) << 5) | colour.b);
}

bool SameColour(Colour a, Colour b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

int32_t Absolute(int32_t value)
{
    return value < 0 ? -value : value;
}

int32_t Distance(Colour a, Colour b)
{
    return Absolute(a.r - b.r) + Absolute(a.g - b.g) + Absolute(a.b - b.b);
}

uint8_t Thirds(uint32_t sum)
{
    return static_cast<uint8_t>((sum * kThirdsMultiplier + kThirdsRound) >> kThirdsShift);
}

uint8_t MiddleThird(uint32_t sum)
{
    return static_cast<uint8_t>((sum * kMiddleMultiplier) >> kMiddleShift);
}

} // namespace

// 0x89779a
uint8_t __cdecl NearestOfFour(uint32_t end0, uint32_t end1, uint32_t twoThirds, uint32_t oneThird, uint32_t entry)
{
    const Colour pixel = FromSlot(entry);
    const int32_t toTwoThirds = Distance(pixel, FromSlot(twoThirds));
    const int32_t toEnd0 = Distance(pixel, FromSlot(end0));
    if (toTwoThirds >= toEnd0) return 0;
    const int32_t toOneThird = Distance(pixel, FromSlot(oneThird));
    if (toOneThird > toTwoThirds) return 2;
    const int32_t toEnd1 = Distance(pixel, FromSlot(end1));
    return toEnd1 > toOneThird ? 3 : 1;
}

// 0x8978a7
uint32_t __cdecl NearestOfThree(uint32_t end0, uint32_t end1, uint32_t middle, uint32_t entry)
{
    const Colour pixel = FromSlot(entry);
    const int32_t toEnd0 = Distance(pixel, FromSlot(end0));
    const int32_t toEnd1 = Distance(pixel, FromSlot(end1));
    const int32_t toMiddle = Distance(pixel, FromSlot(middle));
    const uint32_t nearerEnd = toEnd1 < toEnd0 ? 1 : 0;
    const int32_t nearestEnd = toEnd1 >= toEnd0 ? toEnd0 : toEnd1;
    return toMiddle < nearestEnd ? 2 : nearerEnd;
}

// 0x897987
uint32_t __cdecl ColourError(const uint8_t *palette, const uint8_t *chosen, uint8_t count)
{
    uint32_t total = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const Colour entry = FromBytes(palette + i * 4);
        const Colour pick = FromBytes(chosen + i * 3);
        const int32_t red = (entry.r - pick.r) * (1 << kRedScale);
        const int32_t green = (entry.g - pick.g) * (1 << kGreenScale);
        const int32_t blue = (entry.b - pick.b) * (1 << kBlueScale);
        const int32_t squares = blue * blue + green * green + red * red;
        total += static_cast<uint32_t>(squares) * palette[i * 4 + 3];
    }
    return total;
}

// 0x896e1e
void __cdecl EncodeColourBlock(const uint32_t *pixels, uint16_t *out, int32_t stride, int32_t width, int32_t height)
{
    // Every table the original leaves on its stack, zeroed (see the top of the file).
    uint8_t slots[kMaxPixels] = {};              // the palette index of each pixel
    Entry palette[kMaxPixels] = {};
    Colour chosen[kMaxPixels] = {};
    uint8_t codes[kMaxPixels] = {};              // the code of each entry in the candidate being tried
    uint8_t codeOfEntry[kMaxPixels] = {};        // the best candidate's codes
    const uint32_t pixelCount = static_cast<uint32_t>(width * height);

    // The distinct colours in order of first appearance, with how often each occurs.
    uint32_t entries = 0;
    bool transparent = false;
    uint32_t pixelIndex = 0;
    if (height != 0) {
        for (int32_t row = 0; row < height; ++row) {
            const uint32_t *p = pixels + row * stride;
            for (int32_t column = 0; column < width; ++column, ++p, ++pixelIndex) {
                const uint32_t pixel = *p;
                const Colour colour{static_cast<uint8_t>((pixel >> kRedShift) & kMask5), static_cast<uint8_t>((pixel >> kGreenShift) & kMask6),
                                    static_cast<uint8_t>((pixel >> kBlueShift) & kMask5)};
                if (pixel < kTransparentBelow) transparent = true;
                uint32_t found = 0;
                while (found < entries && !SameColour(palette[found].colour, colour)) ++found;
                if (found < entries) {
                    ++palette[found].count;
                } else {
                    palette[entries].colour = colour;
                    palette[entries].count = 1;
                    ++entries;
                }
                slots[pixelIndex & 0xffff] = static_cast<uint8_t>(found);
            }
        }
    }

    Colour end0{};
    Colour end1{};
    if (entries == 1) {
        end0 = palette[0].colour;
        codeOfEntry[0] = 0;
    } else if (entries == 2) {
        const Colour first = palette[0].colour;
        const Colour second = palette[1].colour;
        const uint16_t firstPacked = Pack(first);
        const uint16_t secondPacked = Pack(second);
        end0 = first;
        end1 = second;
        // The 4-colour mode wants the larger colour first, the 3-colour mode (any transparent pixel) the smaller.
        const bool swap = transparent ? secondPacked <= firstPacked : firstPacked < secondPacked;
        if (swap) {
            end0 = second;
            end1 = first;
        }
        if (SameColour(end0, first)) {
            codeOfEntry[0] = 0;
            codeOfEntry[1] = 1;
        } else {
            codeOfEntry[0] = 1;
            codeOfEntry[1] = 0;
        }
    } else if (entries > 1) {
        uint32_t bestError = kNoError;
        for (uint32_t i = 1; i < entries; ++i) {
            for (uint32_t j = i; j < entries; ++j) {
                Colour high = palette[i - 1].colour;
                Colour low = palette[j].colour;
                if (Pack(high) < Pack(low)) {
                    const Colour held = high;
                    high = low;
                    low = held;
                }
                const Colour twoThirds{Thirds(low.r + high.r * 2u), Thirds(low.g + high.g * 2u), Thirds(low.b + high.b * 2u)};
                const Colour oneThird{Thirds(low.r * 2u + high.r), Thirds(low.g * 2u + high.g), Thirds(low.b * 2u + high.b)};
                // ORIGINAL-BUG: the 3-colour mode's middle is a third of the sum (it should be half): (a + b) * 0x5555 >> 16.
                const Colour middle{MiddleThird(low.r + high.r), MiddleThird(low.g + high.g), MiddleThird(low.b + high.b)};
                if (!transparent) {
                    const Colour ladder[4] = {high, low, twoThirds, oneThird};
                    for (uint32_t k = 0; k < entries; ++k) {
                        const uint8_t pick = NearestOfFour(ToSlot(high), ToSlot(low), ToSlot(twoThirds), ToSlot(oneThird), ToSlot(palette[k].colour));
                        chosen[k] = ladder[pick];
                        codes[k] = pick;
                    }
                    uint8_t table[kMaxPixels * 4];
                    uint8_t picked[kMaxPixels * 3];
                    for (uint32_t k = 0; k < entries; ++k) {
                        table[k * 4 + 0] = palette[k].colour.r;
                        table[k * 4 + 1] = palette[k].colour.g;
                        table[k * 4 + 2] = palette[k].colour.b;
                        table[k * 4 + 3] = palette[k].count;
                        picked[k * 3 + 0] = chosen[k].r;
                        picked[k * 3 + 1] = chosen[k].g;
                        picked[k * 3 + 2] = chosen[k].b;
                    }
                    const uint32_t error = ColourError(table, picked, static_cast<uint8_t>(entries));
                    if (error < bestError) {
                        bestError = error;
                        end0 = high;
                        end1 = low;
                        std::memcpy(codeOfEntry, codes, entries);
                    }
                }
                // The 3-colour mode over the first width * height entries. ORIGINAL-BUG: each entry's colour is taken from
                // `chosen`, the colour the 4-colour pass picked for it (or whatever the previous pair left), not from the palette.
                for (uint32_t k = 0; k < pixelCount; ++k) {
                    const uint32_t pick = NearestOfThree(ToSlot(low), ToSlot(high), ToSlot(middle), ToSlot(chosen[k]));
                    if (pick == 0) {
                        chosen[k] = low;
                        codes[k] = 0;
                    } else if (pick == 1) {
                        chosen[k] = high;
                        codes[k] = 1;
                    } else if (pick == 2) {
                        chosen[k] = middle;
                        codes[k] = 2;
                    }
                }
                uint8_t table[kMaxPixels * 4];
                uint8_t picked[kMaxPixels * 3];
                for (uint32_t k = 0; k < kMaxPixels; ++k) {
                    table[k * 4 + 0] = palette[k].colour.r;
                    table[k * 4 + 1] = palette[k].colour.g;
                    table[k * 4 + 2] = palette[k].colour.b;
                    table[k * 4 + 3] = palette[k].count;
                    picked[k * 3 + 0] = chosen[k].r;
                    picked[k * 3 + 1] = chosen[k].g;
                    picked[k * 3 + 2] = chosen[k].b;
                }
                const uint32_t error = ColourError(table, picked, static_cast<uint8_t>(pixelCount));
                if (error < bestError) {
                    bestError = error;
                    end0 = low;
                    end1 = high;
                    std::memcpy(codeOfEntry, codes, pixelCount);
                }
            }
        }
    }

    out[0] = Pack(end0);
    out[1] = Pack(end1);
    if (pixelCount != 0) {
        uint8_t *index = reinterpret_cast<uint8_t *>(out + 2);
        const uint8_t *slot = slots;
        uint32_t covered = 0;
        do {
            covered += static_cast<uint32_t>(width);
            *index++ = static_cast<uint8_t>(((codeOfEntry[slot[3]] << 2 | codeOfEntry[slot[2]]) << 2 | codeOfEntry[slot[1]]) << 2 |
                                            codeOfEntry[slot[0]]);
            slot += kPixelsPerRowOfFour;
        } while (covered < pixelCount);
    }
}

// 0x8976f6
void __cdecl EncodeColourBlocks(const uint32_t *pixels, uint8_t *out, uint32_t width, uint32_t rows)
{
    for (uint32_t y = 0; y < rows; y += kBlockSide) {
        const uint32_t blockRows = rows - y > kBlockSide ? kBlockSide : rows - y;
        const uint32_t *row = pixels;
        for (uint32_t x = 0; x < width; x += kBlockSide) {
            const uint32_t blockWidth = width - x > kBlockSide ? kBlockSide : width - x;
            EncodeColourBlock(row, reinterpret_cast<uint16_t *>(out), static_cast<int32_t>(width), static_cast<int32_t>(blockWidth),
                              static_cast<int32_t>(blockRows));
            out += kBlockBytes;
            row += kBlockSide;
        }
        pixels += width * kBlockSide;
    }
}

// 0x897a29
int32_t __stdcall EncodeColourRecord(uint8_t *dest, const uint32_t *source, int32_t size, const uint32_t *flags)
{
    const uint32_t mode = flags != nullptr ? *flags : 0;
    uint8_t flagByte = 0;
    if ((mode & kFlagExplicitAlpha) != 0) flagByte = 1;
    if ((mode & kFlagPremultiplied) != 0) flagByte |= kFlagBitPremultiplied;
    if ((mode & kFlagBigEndian) != 0) flagByte = 0;
    const int32_t rows = (size + ((size >> 31) & 15)) >> 4;
    uint8_t type;
    if ((mode & kFlagExplicitAlpha) != 0) {
        type = size > kWideAbove ? static_cast<uint8_t>(kType1a | kWideBit) : kType1a;
    } else if ((mode & kFlagInterpolatedAlpha) != 0) {
        type = size > kWideAbove ? static_cast<uint8_t>(kType1c | kWideBit) : kType1c;
    } else if ((mode & kFlagBigEndian) != 0) {
        type = size > kWideAbove ? static_cast<uint8_t>(kType28 | kWideBit) : kType28;
    } else {
        type = size > kWideAbove ? static_cast<uint8_t>(kType18 | kWideBit) : kType18;
    }
    dest[0] = type;
    dest[1] = kSecondHeaderByte;
    if (size > kWideAbove) {
        PutBigEndian(dest + kSizeOffset, static_cast<uint32_t>(size), kSizeBytes);
        dest[kWideFlagsOffset] = flagByte;
    } else {
        dest[4] = static_cast<uint8_t>(size);
        dest[2] = static_cast<uint8_t>(static_cast<uint32_t>(size) >> 16);
        dest[3] = static_cast<uint8_t>(static_cast<uint32_t>(size) >> 8);
        dest[kNarrowFlagsOffset] = flagByte;
    }
    EncodeColourBlocks(source, dest + kHeaderBytes, 4, static_cast<uint32_t>(rows));
    const int32_t padded = rows + 3;
    return ((padded + ((padded >> 31) & 3)) >> 2) * kBlockBytes + kHeaderBytes;
}

} // namespace fsh

} // namespace gimex
