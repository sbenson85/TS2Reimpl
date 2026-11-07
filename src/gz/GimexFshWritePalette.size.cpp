// The FSH back end's encode side, the attachment builders (lane MY; src/gz/GimexFshWrite.h, 0x88b405..0x88bd45): the palette block and the
// text, user-data, 0x7c and metal-bin blocks that follow a shape in a directory entry, and the copy of the shape's name.
#include "gz/GimexFshWrite.h"

#include <cstring>

#include "game/game.h"
#include "gz/GimexFshLeaves.h"

namespace gimex {

namespace fsh {

namespace {

// The text the original keeps in its data section: the user-data block's filler and the two metal-bin attachment notes (RVAs in the image).
constexpr uint32_t kUserDataText = 0xe4a6fc;
constexpr uint32_t kMetalBin240Text = 0xe4a714;
constexpr uint32_t kMetalBin64Text = 0xe4a750;
constexpr uint32_t kMetalBin240Bytes = 0x3c;
constexpr uint32_t kMetalBin64Bytes = 0x3b;

constexpr uint32_t kInfoVariant = 0x428;
constexpr uint32_t kInfoAlphaBits = 0x438;
constexpr uint32_t kInfoRedBits = 0x43c;
constexpr uint32_t kInfoGreenBits = 0x440;
constexpr uint32_t kInfoBlueBits = 0x444;
constexpr uint32_t kInfoField448 = 0x448;
constexpr uint32_t kInfoField44c = 0x44c;
constexpr uint32_t kInfoNameText = 0x45c;
constexpr uint32_t kInfoField458 = 0x458;
constexpr uint32_t kInfoText = 0x65c;
constexpr uint32_t kInfoPairs = 0xa5c;
constexpr uint32_t kInfoUserDataBytes = 0x2a78;
constexpr uint32_t kInfoUserDataSize = 0x2a7b;
constexpr uint32_t kVariantAlphaFirst = 0x400000;
constexpr uint32_t kVariantMetalBin = 0x200000;
constexpr uint32_t kVariantNoPageOrder = 0x4000;

constexpr int32_t kPlatformPs2Native = 5;
constexpr int32_t kPlatformPaged = 6;

uint32_t InfoWord(const Info *info, uint32_t offset)
{
    uint32_t value;
    std::memcpy(&value, reinterpret_cast<const uint8_t *>(info) + offset, sizeof value);
    return value;
}

const char *InfoString(const Info *info, uint32_t offset)
{
    return reinterpret_cast<const char *>(info) + offset;
}

// 0x885849 answers 0x2a and nothing else (a four-byte function; no hook possible).
constexpr int32_t kPaletteTypeConsole = 0x2a;

uint32_t Align16(uint32_t v)
{
    return v & ~0xfu;
}

void Header(uint8_t *block, uint8_t type, int32_t platform)
{
    block[0] = type;
    PutWord(block + 1, 1, 3, platform);
}

void Store16(uint8_t *at, uint32_t value)
{
    at[0] = static_cast<uint8_t>(value);
    at[1] = static_cast<uint8_t>(value >> 8);
}

void Store16Swapped(uint8_t *at, uint32_t value)
{
    at[1] = static_cast<uint8_t>(value);
    at[0] = static_cast<uint8_t>(value >> 8);
}

} // namespace

// 0x88b405
void __cdecl BuildPalette(const Info *info, uint32_t count, const uint8_t *entries, uint8_t **out, uint32_t *outBytes, int32_t skip, int32_t platform)
{
    uint8_t *block = nullptr;
    uint32_t blockBytes = 0;
    bool reorder = false;
    if (static_cast<int32_t>(count) >= 1 && skip == 0) {
        const int32_t alpha = static_cast<int32_t>(InfoWord(info, kInfoAlphaBits));
        const int32_t red = static_cast<int32_t>(InfoWord(info, kInfoRedBits));
        const int32_t green = static_cast<int32_t>(InfoWord(info, kInfoGreenBits));
        const int32_t blue = static_cast<int32_t>(InfoWord(info, kInfoBlueBits));
        const bool alphaFirst = (InfoWord(info, kInfoVariant) & kVariantAlphaFirst) != 0;
        int32_t type = static_cast<int32_t>(count);
        uint32_t bytes = count;
        bool build = true;
        switch (platform) {
        case 1:
            type = 0x23;
            bytes = count * 2;
            break;
        case 2: {
            type = alphaFirst ? PaletteTypeForBitsA(alpha, red, green, blue) : PaletteTypeForBitsB(alpha, red, green, blue);
            const int32_t width = (type == 0x29 || type == 0x2d) ? 2 : (type == 0x22 || type == 0x24) ? 3 : 4;
            bytes = count * static_cast<uint32_t>(width);
            break;
        }
        case 3:
            type = 0x22;
            bytes = count * 3;
            break;
        case 4:
            type = 0x27;
            if (IsGreyRamp(entries, static_cast<int32_t>(count)) != 0) build = false;
            bytes = count * 2;
            break;
        case 5:
            type = alphaFirst ? PaletteTypeForBitsF(alpha, red, green, blue) : PaletteTypeForBitsC(alpha, red, green, blue);
            bytes = count * 2;
            if (type == 0x33) bytes = ((bytes + 0x1f) & ~0x1fu) + bytes;
            break;
        case 6: {
            type = PaletteTypeForBitsE(alpha, red, green, blue);
            bytes = count;
            if (static_cast<int32_t>(InfoWord(info, 0x1c)) > 4) {
                bytes = PaletteOrder(count);
                reorder = (~(InfoWord(info, kInfoVariant) >> 14) & 1) != 0;
            }
            bytes *= (type == 0x20) ? 2 : 4;
            break;
        }
        case 7:
            type = PaletteTypeForBitsD(alpha, red, green, blue);
            bytes = ((type == 0x39) ? 2u : 4u) * count;
            break;
        case 8:
            type = kPaletteTypeConsole;
            bytes = ((type == 0x29 || type == 0x2d) ? 2u : 4u) * count;
            break;
        case 9:
            type = 0x20;
            bytes = count * 2;
            break;
        default: break;
        }
        const bool sized = platform < 1 || platform > 9 || bytes != 0;
        if (build && sized) {
            blockBytes = (bytes + 0x2f) & ~0xfu;
            block = static_cast<uint8_t *>(Allocate(blockBytes));
            if (block != nullptr) {
                std::memset(block, 0, blockBytes);
                PutWord(block, static_cast<uint32_t>(type), 1, platform);
                PutWord(block + 1, (reorder && platform == kPlatformPaged) ? 0x4001u : 1u, 3, platform);
                PutWord(block + 8, 0x20, 4, platform);
                PutWord(block + 0xc, blockBytes - 0x20, 4, platform);
                PutWord(block + 0x18, count, 4, platform);
                PutWord(block + 0x1c, 1, 4, platform);
                for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(count); ++i) {
                    uint32_t slot = i;
                    if (reorder && (i & 0x1f) - 8 < 0x10) slot = i ^ 0x18;
                    const uint8_t *pixel = entries + i * 4;
                    switch (platform) {
                    case 1: Store16(block + slot * 2 + 0x20, static_cast<uint16_t>(Pack1555Alt(pixel))); break;
                    case 3: {
                        uint8_t *at = block + slot * 3 + 0x20;
                        at[0] = static_cast<uint8_t>(pixel[2] >> 2);
                        at[1] = static_cast<uint8_t>(pixel[1] >> 2);
                        at[2] = static_cast<uint8_t>(pixel[0] >> 2);
                        break;
                    }
                    case 4: Store16Swapped(block + slot * 2 + 0x20, Pack556(pixel)); break;
                    case 2:
                    case 5:
                    case 6:
                    case 7:
                    case 8:
                    case 9: {
                        uint8_t *quad = block + slot * 4 + 0x20;
                        uint8_t *pair = block + slot * 2 + 0x20;
                        switch (type) {
                        case 0x20: Store16(pair, Pack1555Swapped(pixel)); break;
                        case 0x21:
                            quad[0] = pixel[2];
                            quad[1] = pixel[1];
                            quad[2] = pixel[0];
                            quad[3] = pixel[3];
                            break;
                        case 0x22: {
                            uint8_t *at = block + slot * 3 + 0x20;
                            at[0] = static_cast<uint8_t>(pixel[2] >> 2);
                            at[1] = static_cast<uint8_t>(pixel[1] >> 2);
                            at[2] = static_cast<uint8_t>(pixel[0] >> 2);
                            break;
                        }
                        case 0x24: {
                            uint8_t *at = block + slot * 3 + 0x20;
                            at[0] = pixel[2];
                            at[1] = pixel[1];
                            at[2] = pixel[0];
                            break;
                        }
                        case 0x29: Store16(pair, Pack565(pixel)); break;
                        case 0x2a:
                            quad[0] = pixel[0];
                            quad[1] = pixel[1];
                            quad[2] = pixel[2];
                            quad[3] = pixel[3];
                            break;
                        case 0x2c:
                            Store16(quad, Pack565(pixel));
                            quad[2] = 0;
                            quad[3] = pixel[3];
                            break;
                        case 0x2d: Store16(pair, Pack1555(pixel)); break;
                        case 0x2e:
                            Store16(quad, Pack1555(pixel));
                            quad[2] = 0;
                            quad[3] = pixel[3];
                            break;
                        case 0x30: Store16Swapped(pair, PackGrey88(pixel)); break;
                        case 0x31: Store16Swapped(pair, Pack565(pixel)); break;
                        case 0x32: Store16Swapped(pair, Pack3555(pixel)); break;
                        case 0x33: {
                            uint8_t *first = block + slot * 2 + 0x20;
                            first[0] = pixel[3];
                            first[1] = pixel[2];
                            uint8_t *second = block + (((count * 2 + 0x1f) & ~0x1fu) + slot * 2) + 0x20;
                            second[0] = pixel[1];
                            second[1] = pixel[0];
                            break;
                        }
                        case 0x38:
                            quad[0] = pixel[3];
                            quad[1] = pixel[2];
                            quad[2] = pixel[1];
                            quad[3] = pixel[0];
                            break;
                        case 0x39: Store16Swapped(pair, Pack1555(pixel)); break;
                        default: break;
                        }
                        break;
                    }
                    default: break;
                    }
                }
            }
        }
    }
    *outBytes = blockBytes;
    *out = block;
}

// 0x88b9e1
void __cdecl BuildTextBlock(const Info *info, uint8_t **out, uint32_t *outBytes, int32_t skip, int32_t platform)
{
    uint8_t *block = nullptr;
    uint32_t blockBytes = 0;
    const char *text = InfoString(info, kInfoText);
    if (*text != '\0' && skip == 0) {
        const uint32_t length = static_cast<uint32_t>(std::strlen(text));
        blockBytes = (length + 0x20) & ~0xfu;
        block = static_cast<uint8_t *>(Allocate(blockBytes));
        if (block != nullptr) {
            std::memset(block, 0, blockBytes);
            Header(block, 0x6f, platform);
            PutWord(block + 8, 0x10, 4, platform);
            PutWord(block + 0xc, length + 1, 4, platform);
            std::memcpy(block + 0x10, text, length + 1);
        }
    }
    *outBytes = blockBytes;
    *out = block;
}

// 0x88ba81
void __cdecl BuildUserDataBlock(const Info *info, uint8_t **out, uint32_t *outBytes, int32_t platform)
{
    uint8_t *block = nullptr;
    uint32_t blockBytes = 0;
    const int32_t userBytes = static_cast<int32_t>(InfoWord(info, kInfoUserDataBytes));
    if (info->size > kInfoUserDataSize && userBytes > 0) {
        blockBytes = (static_cast<uint32_t>(userBytes) + 0x1f) & ~0xfu;
        block = static_cast<uint8_t *>(Allocate(blockBytes));
        if (block != nullptr) {
            std::memset(block, 0, blockBytes);
            block[0] = 0x6b;
            std::strncpy(reinterpret_cast<char *>(block) + 0x10, GameAddress<const char>(kUserDataText), static_cast<size_t>(userBytes));
            PutWord(block + 1, 1, 3, platform);
            PutWord(block + 8, 0x10, 4, platform);
            PutWord(block + 0xc, static_cast<uint32_t>(userBytes), 4, platform);
        }
    }
    *outBytes = blockBytes;
    *out = block;
}

// 0x88bb19
void __cdecl BuildPairsBlock(const Info *info, uint8_t **out, uint32_t *outBytes, int32_t skip, int32_t platform)
{
    uint8_t *block = nullptr;
    uint32_t blockBytes = 0;
    const int32_t pairs = static_cast<int32_t>(InfoWord(info, kInfoField458));
    if ((pairs > 0 || InfoWord(info, kInfoField448) != 0 || InfoWord(info, kInfoField44c) != 0) && skip == 0) {
        blockBytes = (static_cast<uint32_t>(pairs) * 8 + 0x2f) & ~0xfu;
        block = static_cast<uint8_t *>(Allocate(blockBytes));
        if (block != nullptr) {
            std::memset(block, 0, blockBytes);
            block[0] = 0x7c;
            PutWord(block + 1, 1, 3, platform);
            PutWord(block + 0x1c, static_cast<uint32_t>(pairs), 4, platform);
            PutWord(block + 8, 0x20, 4, platform);
            PutWord(block + 0xc, blockBytes - 0x20, 4, platform);
            PutWord(block + 0x10, InfoWord(info, kInfoField448), 4, platform);
            PutWord(block + 0x14, InfoWord(info, kInfoField44c), 4, platform);
            for (int32_t i = 0; i < pairs; ++i) {
                PutWord(block + 0x20 + i * 8, InfoWord(info, kInfoPairs + static_cast<uint32_t>(i) * 8), 4, platform);
                PutWord(block + 0x24 + i * 8, InfoWord(info, kInfoPairs + 4 + static_cast<uint32_t>(i) * 8), 4, platform);
            }
        }
    }
    *outBytes = blockBytes;
    *out = block;
}

// 0x88bc6b
void __cdecl BuildMetalBinBlock(const Info *info, uint8_t **out, uint32_t *outBytes, int32_t platform)
{
    uint8_t *block = nullptr;
    uint32_t blockBytes = 0;
    const uint32_t value = platform == kPlatformPaged ? 0xf0u : 0x40u;
    if ((InfoWord(info, kInfoVariant) & kVariantMetalBin) != 0) {
        blockBytes = value + 0x1f;
        if (platform == kPlatformPs2Native) blockBytes = 0x1f;
        blockBytes = Align16(blockBytes);
        block = static_cast<uint8_t *>(Allocate(blockBytes));
        if (block != nullptr) {
            std::memset(block, 0, blockBytes);
            block[0] = 0x69;
            if (platform != kPlatformPs2Native) {
                if (platform == kPlatformPaged) std::memcpy(block + 0x10, GameAddress<const char>(kMetalBin240Text), kMetalBin240Bytes);
                else std::memcpy(block + 0x10, GameAddress<const char>(kMetalBin64Text), kMetalBin64Bytes);
            }
            PutWord(block + 1, 1, 3, platform);
            if (platform != kPlatformPs2Native) {
                PutWord(block + 8, 0x10, 4, platform);
                PutWord(block + 0xc, value, 4, platform);
            }
        }
    }
    *outBytes = blockBytes;
    *out = block;
}

// 0x88bd45
void __cdecl CopyShapeName(const Info *info, char **out)
{
    const char *name = InfoString(info, kInfoNameText);
    const uint32_t length = static_cast<uint32_t>(std::strlen(name));
    char *copy = static_cast<char *>(Allocate(length + 1));
    if (copy != nullptr) std::memcpy(copy, name, length + 1);
    *out = copy;
}

} // namespace fsh

} // namespace gimex
