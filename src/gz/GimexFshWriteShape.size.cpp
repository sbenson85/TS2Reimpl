// The FSH back end's encode side, the shape encoder and compressor (lane MZ; src/gz/GimexFshWrite.h, 0x88905f and 0x88aa6e): EncodeShape
// turns one frame of the writer's info into a directory entry (the shape record, the palette and the attached blocks), CompressShape builds
// the shape record itself. Both pick their numbers (bits per pixel of the record, the record type byte) from the platform and the info's
// channel bits: long chains of conditions that the original reaches by jumps; here they are functions of the platform.
#include "gz/GimexFshWrite.h"

#include <cstring>

#include "gz/GimexFshBlocks.h"
#include "gz/GimexFshConvert.h"
#include "gz/GimexFshDecode.h"
#include "gz/GimexFshLeaves.h"

namespace gimex {

namespace fsh {

namespace {

// The info's words beyond the named ones, and the FSH row's info block (0x2a7c bytes) the encoder tags entries by.
constexpr uint32_t kInfoNameText = 0x45c;
constexpr uint32_t kInfoFrameDepth = 0x2a70;
constexpr uint32_t kInfoSignatureMask = 0xffffff00;
constexpr uint32_t kInfoSignatureShp = 0x53687000; // "\0pSh"
constexpr uint32_t kInfoFshBytes = 0x2a7c;

// The variant word (info + 0x428): its bits.
constexpr uint32_t kVariantNoPalette = 1;
constexpr uint32_t kVariantNoShape = 2;
constexpr uint32_t kVariantNoPairs = 4;
constexpr uint32_t kVariantNoText = 8;
constexpr uint32_t kVariantPageOrder = 0x2000; // pixels in the console's order
constexpr uint32_t kVariantUnpackedAlpha = 0x100000; // header kept on a block record
constexpr uint32_t kVariantConsoleSurface = 0x20000;
constexpr uint32_t kVariantAlphaFirst = 0x400000;

// Compression kinds (info + 0x42c).
constexpr int32_t kKindRefPack = 2;
constexpr int32_t kKindBlocks = 3;
constexpr int32_t kKindGm = 4;
constexpr int32_t kKindBlocksPs2 = 5;

constexpr int32_t kPlatformPc1 = 1;
constexpr int32_t kPlatformPc = 2;
constexpr int32_t kPlatformPs2 = 6;
constexpr int32_t kPlatformGc = 5;
constexpr int32_t kPlatformXbox = 7;
constexpr int32_t kPlatformDefault = 8;
constexpr int32_t kPlatformPs2Alt = 9;

constexpr uint32_t kShapeHeaderBytes = 0x20;
constexpr uint32_t kRecordCodedBit = 2; // the attribute word's bit for a coded (compressed) record

uint32_t InfoWord(const Info *info, uint32_t offset)
{
    uint32_t value;
    std::memcpy(&value, reinterpret_cast<const uint8_t *>(info) + offset, sizeof value);
    return value;
}

// The channel bit counts of the info (alpha, red, green, blue).
struct Channels {
    int32_t alpha, red, green, blue;
    explicit Channels(const Info *info)
        : alpha(static_cast<int32_t>(info->alphaBits)), red(static_cast<int32_t>(info->redBits)), green(static_cast<int32_t>(info->greenBits)),
          blue(static_cast<int32_t>(info->blueBits))
    {
    }
    bool Is(int32_t a, int32_t r, int32_t g, int32_t b) const { return alpha == a && red == r && green == g && blue == b; }
};

// Sets the coded bit in a record's attribute word.
void MarkCoded(uint8_t *record, int32_t platform)
{
    PutWord(record + 1, ReadWord(record + 1, 3, platform) | kRecordCodedBit, 3, platform);
}

// The rest of the 15 / 16-bit record types of the platforms 8 and the others: from the channel bits, with `alphaMode` the block-compressed
// alpha kind (0 none).
int32_t Type16Fallback(const Channels &c, int32_t alphaMode)
{
    if (c.green < 6 && alphaMode == 0) return c.red >= 6 ? 0x67 : 0x7e;
    return 0x78;
}

int32_t Type16(const Channels &c, int32_t alphaMode)
{
    if (c.alpha >= 4) return alphaMode == 0 ? 0x6d : Type16Fallback(c, alphaMode);
    if (c.alpha != 0) return Type16Fallback(c, alphaMode);
    if (c.red == 2) {
        if (c.green != 8) return Type16Fallback(c, alphaMode);
        return c.blue == 2 ? 0x6c : 0x78;
    }
    if (c.red == 4) {
        if (c.green != 8) return Type16Fallback(c, alphaMode);
        return c.blue == 4 ? 0x68 : 0x78;
    }
    return Type16Fallback(c, alphaMode);
}

// The record type byte for `bits` per pixel (the original's switch on the platform).
int32_t RecordType(const Info *info, int32_t platform, int32_t bits, int32_t alphaMode, bool console, int32_t aux8, int32_t aux9)
{
    const Channels c(info);
    const uint32_t variant = info->variant;
    switch (platform) {
    case kPlatformPc1:
        switch (bits) {
        case 8: return 0x41;
        case 0xf:
        case 0x10: return 0x42;
        case 0x18: return 0x43;
        case 0x20: return 0x42;
        default: return 0x40;
        }
    case kPlatformPs2:
        switch (bits) {
        case 8: return 2;
        case 0xf:
        case 0x10:
            if (c.alpha > 2) return 5;
            return c.Is(0, 2, 8, 2) ? 0x6c : 3;
        case 0x18: return 4;
        case 0x20: return 5;
        default: return 1;
        }
    case kPlatformPs2Alt:
        switch (bits) {
        case 8: return 2;
        case 0xf:
        case 0x10: return 3;
        default: return 1;
        }
    case kPlatformGc:
        if (console || aux9 == 8) return 0x13;
        if (aux9 == 4) return 0x12;
        if (aux8 == 8) {
            if (variant & kVariantAlphaFirst) return 0x11;
        } else if (aux8 == 4 && (variant & kVariantAlphaFirst)) {
            return 0x10;
        }
        switch (bits) {
        case 8: return 0x19;
        case 0xf:
        case 0x10:
            if (c.alpha >= 1) return alphaMode != 0 ? 0x14 : 0x15;
            return c.Is(0, 2, 8, 2) ? 0x1c : 0x14;
        case 0x18:
        case 0x20: return 0x16;
        default: return 0x18;
        }
    case kPlatformXbox:
        switch (bits) {
        case 8: return 0x59;
        case 0xf:
        case 0x10:
            if (c.alpha >= 4 && alphaMode == 0) return 0x5c;
            if (c.green < 6 && alphaMode == 0) return 0x5a;
            return 0x5b;
        case 0x18: return 0x5d;
        case 0x20: return 0x5e;
        default: return 0x58;
        }
    default: break;
    }
    // Platform 8 and every other (the PC's 2 among them): the 0x6x / 0x7x types; the grey alpha planes override.
    int32_t type;
    if (platform == kPlatformDefault) {
        switch (bits) {
        case 0xf:
        case 0x10: type = Type16(c, alphaMode); break;
        case 0x18: type = 0x7f; break;
        case 0x20: type = 0x7d; break;
        default: type = 0x7b; break;
        }
    } else {
        switch (bits) {
        case 8: type = 0x7b; break;
        case 0xf:
        case 0x10: type = Type16(c, alphaMode); break;
        case 0x18: type = (c.alpha >= 6 && (variant & kVariantAlphaFirst)) ? 0x66 : 0x7f; break;
        case 0x20: type = (c.alpha <= 2 && (variant & kVariantAlphaFirst)) ? 0x6a : 0x7d; break;
        default: type = 0x79; break;
        }
    }
    if (aux9 == 8) return 0x65;
    if (aux8 == 8) return 0x64;
    return type;
}

} // namespace

// 0x88aa6e
uint32_t __cdecl CompressShape(uint8_t *out, const Info *info, int32_t bits, const uint8_t *pixels, uint32_t stride, uint32_t /*noPack*/,
                               int32_t platform, int32_t aux8, int32_t aux9)
{
    uint32_t variant = info->variant;
    uint32_t dropHeader = ~(variant >> 20) & 1;
    int32_t alphaMode = 0; // the block-compressed alpha kind: 0 none, 1 / 3 / 5 by the alpha bits
    bool gm = false;
    // The console surface's size: the original passes the flag where the width will be, and the height left uninitialised (read only by
    // the platforms that ignore it or after PageSize / PaddedSize set it).
    uint32_t reorderWidth = (platform == kPlatformGc && (variant & kVariantConsoleSurface)) ? 1 : 0;
    uint32_t reorderHeight = 0;
    std::memset(out, 0, kShapeHeaderBytes);

    const int32_t kind = static_cast<int32_t>(info->jpegFlag);
    const Channels c(info);
    int32_t effective = bits;
    if (kind == kKindBlocks || kind == kKindBlocksPs2) {
        if (bits > 8 && info->bitsPerPixel > 8 && aux9 == 0 && aux8 == 0) {
            alphaMode = c.alpha >= 4 ? 3 : (c.alpha > 1 ? 5 : 1);
            effective = 16;
        }
    }
    if (kind == kKindGm && effective > 8) {
        effective = (c.red > 5 || c.green > 5) ? 0x18 : 0x10;
        if (c.alpha > 1) effective = 0x20;
        gm = true;
    }

    int32_t type = RecordType(info, platform, effective, alphaMode, reorderWidth != 0, aux8, aux9);
    if (alphaMode != 0 && dropHeader != 0) {
        if (platform == kPlatformGc) {
            type = 0x1e;
        } else if (platform == kPlatformDefault || platform == kPlatformPc || platform == kPlatformPs2 || platform == kPlatformPs2Alt) {
            type = alphaMode == 1 ? 0x60 : (alphaMode == 3 ? 0x61 : (alphaMode == 5 ? 0x62 : 0x60));
        } else {
            dropHeader = 0;
        }
    }

    const uint32_t originX = static_cast<uint32_t>(info->originX);
    const uint32_t originY = static_cast<uint32_t>(info->originY);
    const uint32_t width = static_cast<uint32_t>(info->width);
    const uint32_t height = static_cast<uint32_t>(info->height);
    // Whether the pixels go out in the console's page order: only the shapes of a power-of-two size on the PC / console platforms.
    if (variant & kVariantPageOrder) {
        if (platform == kPlatformDefault || platform == kPlatformPc) {
            if (!(width == 1u << HighestBit(width) && height == 1u << HighestBit(height))) variant &= ~kVariantPageOrder;
        } else if (platform == kPlatformPs2) {
            if (width != 1u << HighestBit(width) || height != 1u << HighestBit(height) || effective >= 0x18) variant &= ~kVariantPageOrder;
            PageSize(&reorderWidth, &reorderHeight, width, height, effective);
        } else if (platform == kPlatformGc && alphaMode == 0) {
            PaddedSize(&reorderWidth, &reorderHeight, static_cast<int32_t>(width), static_cast<int32_t>(height), effective);
            goto sizesDone;
        } else {
            variant &= ~kVariantPageOrder;
        }
    }
    if (alphaMode != 0) {
        if (platform == kPlatformGc)
            variant |= kVariantPageOrder;
        else
            variant &= ~kVariantPageOrder;
    }
sizesDone:
    if (effective == 0x18 || ((effective == 4 || type == 0x68) && (platform == kPlatformDefault || platform == kPlatformPc))) variant &= ~kVariantPageOrder;

    // The attribute word: bit 0 always, then the info's flags mapped to the record's.
    const bool pageOrder = (variant & kVariantPageOrder) != 0;
    uint32_t attributes = ((variant & 0x1000) << 3) | 1;
    if (pageOrder) attributes |= 0x4000;
    if (variant & 0x1000000) attributes |= 0x2000;
    if (variant & 0x2000000) attributes |= 0x1000;
    if (variant & 0x4000000) attributes |= 0x800;
    if (variant & 0x20000) attributes |= 0x400;
    if (variant & 0x10000) attributes |= 0x200;
    if (variant & 0x40000) attributes |= 0x100;
    PutWord(out, static_cast<uint32_t>(type), 1, platform);
    PutWord(out + 1, attributes, 3, platform);
    PutWord(out + 0x18, width, 4, platform);
    PutWord(out + 0x1c, height, 4, platform);
    PutWord(out + 0x10, originX, 4, platform);
    PutWord(out + 0x14, originY, 4, platform);

    uint32_t length;
    if (alphaMode != 0) {
        length = BlockCompress(info, static_cast<int32_t>(stride), pixels, out + kShapeHeaderBytes, platform, static_cast<int32_t>(dropHeader));
        if (dropHeader == 0) MarkCoded(out, platform);
    } else if (type == 0x1c || type == 0x6c) {
        length = static_cast<uint32_t>(RgbToYuv420(info, static_cast<int32_t>(stride), pixels, out + kShapeHeaderBytes, 1, 1, 1, variant, platform, type,
                                                    static_cast<int32_t>(stride), static_cast<int32_t>(reorderWidth), static_cast<int32_t>(reorderHeight)));
    } else if (type == 0x68) {
        length = static_cast<uint32_t>(RgbToYuyv(info, static_cast<int32_t>(stride), pixels, out + kShapeHeaderBytes));
    } else if (gm) {
        length = GmCompress(info, stride, reinterpret_cast<const uint32_t *>(pixels), reinterpret_cast<uint16_t *>(out + kShapeHeaderBytes));
        MarkCoded(out, platform);
    } else {
        PackShapePixels(out, out + kShapeHeaderBytes, info, pixels, static_cast<int32_t>(stride), platform, variant, static_cast<int32_t>(reorderWidth),
                        static_cast<int32_t>(reorderHeight));
        uint32_t planeWidth = width, planeHeight = height;
        if ((platform == kPlatformGc || platform == kPlatformPs2) && pageOrder) {
            planeWidth = reorderWidth;
            planeHeight = reorderHeight;
        }
        length = static_cast<uint32_t>(PlaneBytes(planeWidth, planeHeight, effective, platform, 0));
    }

    if (kind == kKindRefPack) {
        uint8_t *packed = static_cast<uint8_t *>(Allocate(static_cast<uint32_t>(static_cast<int32_t>(static_cast<double>(static_cast<int32_t>(length)) * 1.2 + 1024.0))));
        if (packed != nullptr) {
            const int32_t packedBytes = RefPackEncodeWithHeader(packed, out + kShapeHeaderBytes, static_cast<int32_t>(length), 0);
            const uint32_t rounded = (static_cast<uint32_t>(packedBytes) + 0xf) & ~0xfu;
            if (static_cast<int32_t>(rounded) < static_cast<int32_t>(length)) {
                std::memcpy(out + kShapeHeaderBytes, packed, rounded);
                MarkCoded(out, platform);
                length = rounded;
            }
            FreeMemory(packed);
        }
    }
    PutWord(out + 8, kShapeHeaderBytes, 4, platform);
    PutWord(out + 0xc, length, 4, platform);
    return (length + 0x2f) & ~0xfu;
}

namespace {

// What the encoder decides about a frame before it builds anything.
struct Plan {
    int32_t bits = 0;   // the record's bits per pixel
    int32_t aux8 = 0;   // CompressShape's two plane selectors
    int32_t aux9 = 0;
    bool palette = false; // the shape carries its palette (its separate block is then not built)
    bool channelFix = false; // the channel bits alpha 0 / red 2 / green 8 / blue 2 make the record 16 bits
};

Plan Fixed(int32_t bits, int32_t aux8, int32_t aux9)
{
    Plan plan;
    plan.bits = bits;
    plan.aux8 = aux8;
    plan.aux9 = aux9;
    plan.palette = true;
    return plan;
}

Plan Fallback(int32_t bits)
{
    Plan plan;
    plan.bits = bits;
    plan.channelFix = true;
    return plan;
}

// Platform 1: the source bits to 4 / 8 / 16 / 24.
Plan PlanPc1(int32_t source)
{
    return Fallback(source <= 4 ? 4 : (source <= 8 ? 8 : (source <= 0x10 ? 0x10 : 0x18)));
}

// Platforms 2 and 8: the source bits (2: 4 / 8 / 15 / 16 / 24 / 32, 8: 8 / 15 / 16 / 32), and a frame with a wide alpha but colour channels
// that are all but one zero is a plane of that colour.
Plan PlanPc(const Info *info, int32_t platform)
{
    const int32_t source = info->sourceBits;
    int32_t bits;
    if (platform == kPlatformPc) {
        bits = source <= 4 ? 4 : (source <= 8 ? 8 : (source <= 0xf ? 0xf : (source <= 0x10 ? 0x10 : (source <= 0x18 ? 0x18 : 0x20))));
    } else {
        bits = source <= 8 ? 8 : (source <= 0xf ? 0xf : (source <= 0x10 ? 0x10 : 0x20));
    }
    const Channels c(info);
    const int32_t kind = static_cast<int32_t>(info->jpegFlag);
    if (c.alpha > 4 && kind < kKindBlocks) {
        const bool onlyRed = c.red > 4 && c.green == 0 && c.blue == 0;
        const bool onlyGreen = c.red == 0 && c.green > 4 && c.blue == 0;
        const bool onlyBlue = c.red == 0 && c.green == 0 && c.blue > 4;
        if (onlyRed || onlyGreen || onlyBlue) return Fixed(0x10, 0, 8);
        if (c.red == 0 && c.green == 0 && c.blue == 0) return Fixed(8, 8, 0);
    }
    return Fallback(bits);
}

// Platform 5 (the GameCube-like console): the source bits to 4 / 8 / 15 / 16 / 32, then the alpha and plane cases.
Plan PlanGc(const Info *info)
{
    const int32_t source = info->sourceBits;
    const int32_t bits = source <= 4 ? 4 : (source <= 8 ? 8 : (source <= 0xf ? 0xf : (source <= 0x13 ? 0x10 : 0x20)));
    const Channels c(info);
    const int32_t kind = static_cast<int32_t>(info->jpegFlag);
    const uint32_t variant = info->variant;
    const bool noColour = c.red == 0 && c.green == 0 && c.blue == 0;
    const Plan none = Fallback(bits);
    // alpha 8, no colour: an 8-bit alpha plane (only in the alpha-first variant and below the block kinds).
    auto alphaPlane = [&]() -> Plan {
        if (c.alpha == 8 && noColour && kind < kKindBlocks) return Fixed(8, 8, 0);
        return none;
    };
    auto planeCase = [&]() -> Plan {
        if (variant & kVariantAlphaFirst) return alphaPlane();
        return none;
    };
    auto singleColour = [&](int32_t low, int32_t high) {
        return (c.red >= low && c.red <= high && c.green == 0 && c.blue == 0) || (c.red == 0 && c.green >= low && c.green <= high && c.blue == 0) ||
               (c.red == 0 && c.green == 0 && c.blue >= low && c.blue <= high);
    };
    auto grey = [&](uint32_t low) { return noColour && static_cast<uint32_t>(info->bitsPerPixel) - low <= 3; };
    if (c.alpha > 4) {
        if (kind >= kKindBlocks) return planeCase();
        const bool onlyRed = c.red > 4 && c.green == 0 && c.blue == 0;
        const bool onlyGreen = c.red == 0 && c.green > 4 && c.blue == 0;
        const bool onlyBlue = c.red == 0 && c.green == 0 && c.blue > 4;
        bool wide = onlyRed || onlyGreen || onlyBlue;
        if (!wide) {
            if (variant & kVariantAlphaFirst) return alphaPlane();
            wide = grey(5);
        }
        if (wide) {
            Plan plan;
            plan.bits = info->bitsPerPixel != 8 ? 0x10 : bits;
            plan.aux9 = (info->bitsPerPixel == 8) + 8;
            plan.palette = info->bitsPerPixel != 8;
            return plan;
        }
        return none;
    }
    if (c.alpha <= 2 || kind >= kKindBlocks) return planeCase();
    // alpha 3 or 4: a plane of 3 or 4 bits of one colour.
    if (singleColour(3, 4)) return Fixed(8, 0, 4);
    if (variant & kVariantAlphaFirst) return alphaPlane();
    if (noColour && static_cast<uint32_t>(info->bitsPerPixel) - 3 <= 1) return Fixed(8, 0, 4);
    return none;
}

// Platform 4: the source bits to 1 / 4 / 8 / 15 / 16 / 32; alpha 8 with red 8 only is a 16-bit record.
Plan PlanP4(const Info *info)
{
    const int32_t source = info->sourceBits;
    Plan plan;
    plan.bits = source <= 1 ? 1 : (source <= 4 ? 4 : (source <= 8 ? 8 : (source <= 0xf ? 0xf : (source <= 0x10 ? 0x10 : 0x20))));
    const Channels c(info);
    if (c.alpha != 8) {
        plan.channelFix = true;
        return plan;
    }
    if (c.red == 8 && c.green == 0 && c.blue == 0) plan.bits = 0x10;
    return plan;
}

// Platform 6 (the PS2-like console).
Plan PlanPs2(const Info *info)
{
    const int32_t source = info->sourceBits;
    const Channels c(info);
    int32_t bits = source <= 4 ? 4 : (source <= 8 ? 8 : (source <= 0x10 ? 0xf : 0x18));
    Plan plan;
    if (source > 0x18) {
        plan.bits = 0x20;
        plan.channelFix = true;
    } else if (bits == 0xf && c.alpha > 2) {
        plan.bits = 0x20;
    } else {
        plan.bits = bits;
        plan.channelFix = true;
    }
    return plan;
}

// Platforms 7 and the others: the source bits to a record size.
Plan PlanSimple(int32_t platform, int32_t source)
{
    int32_t bits;
    if (platform == kPlatformXbox) {
        bits = source <= 4 ? 4 : (source <= 8 ? 8 : (source <= 0xf ? 0xf : (source <= 0x10 ? 0x10 : (source <= 0x18 ? 0x18 : 0x20))));
    } else if (platform == kPlatformPs2Alt) {
        bits = source <= 4 ? 4 : (source <= 8 ? 8 : 0xf);
    } else {
        bits = 0;
    }
    return Fallback(bits);
}

Plan MakePlan(const Info *info, int32_t platform)
{
    switch (platform) {
    case 1: return PlanPc1(info->sourceBits);
    case 2:
    case 8: return PlanPc(info, platform);
    case 3: {
        Plan plan; // platform 3: 8 bits, whatever the source (and no channel check)
        plan.bits = 8;
        return plan;
    }
    case 4: return PlanP4(info);
    case 5: return PlanGc(info);
    case 6: return PlanPs2(info);
    default: return PlanSimple(platform, info->sourceBits);
    }
}

} // namespace

// 0x88905f
int32_t __cdecl EncodeShape(Directory *directory, const Info *info, const uint8_t *pixels, int32_t pitch, int32_t index, int32_t platform)
{
    Plan plan = MakePlan(info, platform);
    const Channels c(info);
    const int32_t kind = static_cast<int32_t>(info->jpegFlag);
    if (plan.channelFix && c.Is(0, 2, 8, 2)) plan.bits = 0x10;
    // Block-compressed kinds ask for 16 bits when the frame is true colour, and pad the surface.
    int32_t pad = 0;
    if (kind == kKindBlocks || kind == kKindBlocksPs2) {
        pad = 1;
        if (info->bitsPerPixel > 8) plan.bits = 0x10;
    }

    int32_t result = 1;
    uint8_t *shape = nullptr;
    uint32_t shapeBytes = 0;
    const uint32_t variant = info->variant;
    if ((variant & kVariantNoShape) == 0) {
        uint32_t first = static_cast<uint32_t>(info->width);
        uint32_t second = static_cast<uint32_t>(info->height);
        int32_t planeBytes;
        const bool consoleSize = (platform == kPlatformGc || platform == kPlatformPs2) && (variant & kVariantPageOrder) != 0 && pad == 0;
        if (consoleSize) {
            if (platform == kPlatformGc)
                PaddedSize(&first, &second, info->width, info->height, plan.bits);
            else
                PageSize(&first, &second, static_cast<uint32_t>(info->width), static_cast<uint32_t>(info->height), plan.bits);
            planeBytes = PlaneBytes(second, first, plan.bits, platform, pad);
        } else if ((platform == kPlatformDefault || platform == kPlatformPc) && platform != kPlatformPs2 && c.Is(0, 4, 8, 4)) {
            planeBytes = PlaneBytes(static_cast<uint32_t>(info->height), (static_cast<uint32_t>(info->width) + 1) & ~1u, plan.bits, platform, pad);
        } else {
            planeBytes = PlaneBytes(static_cast<uint32_t>(info->width), static_cast<uint32_t>(info->height), plan.bits, platform, pad);
        }
        if (kind == 4)
            planeBytes = planeBytes * 4 + 0x400;
        else if (kind >= 2)
            planeBytes = static_cast<int32_t>(static_cast<float>(planeBytes) * 1.2f + 1024.0f);
        const uint32_t size = (static_cast<uint32_t>(planeBytes) + 0x2f) & ~0xfu;
        shapeBytes = size;
        shape = static_cast<uint8_t *>(Allocate(size));
        if (shape == nullptr) {
            result = 0;
        } else {
            std::memset(shape, 0, size);
            shapeBytes = (CompressShape(shape, info, plan.bits, pixels, static_cast<uint32_t>(pitch), variant & kVariantNoShape, platform, plan.aux8,
                                        plan.aux9) + 0xf) & ~0xfu;
        }
    }

    uint8_t *palette = nullptr, *metalBin = nullptr, *userData = nullptr, *pairs = nullptr, *text = nullptr;
    uint32_t paletteBytes = 0, metalBinBytes = 0, userDataBytes = 0, pairsBytes = 0, textBytes = 0;
    char *name = nullptr;
    const int32_t skipPalette = ((variant & kVariantNoPalette) == 0 && !plan.palette) ? 0 : 1;
    BuildPalette(info, info->paletteCount, info->palette[0], &palette, &paletteBytes, skipPalette, platform);
    BuildMetalBinBlock(info, &metalBin, &metalBinBytes, platform);
    BuildUserDataBlock(info, &userData, &userDataBytes, platform);
    BuildPairsBlock(info, &pairs, &pairsBytes, static_cast<int32_t>(variant & kVariantNoPairs), platform);
    BuildTextBlock(info, &text, &textBytes, static_cast<int32_t>(variant & kVariantNoText), platform);
    CopyShapeName(info, &name);

    // The entry's tag: four characters, from the fifth of the info's name field on, when the name has at least four; else 0x21 and three zeros.
    const char *nameText = reinterpret_cast<const char *>(info) + kInfoNameText;
    char tag[4] = {0x21, 0, 0, 0};
    int32_t length = 0;
    while (nameText[length] != '\0' && ++length < 4) {
    }
    if (nameText[length] != '\0') {
        int32_t i = 0;
        for (; i < 4; ++i) {
            const char ch = nameText[4 + i];
            if (ch == '\0') {
                std::memset(tag + i, 0x20, static_cast<size_t>(4 - i));
                break;
            }
            tag[i] = ch;
        }
    }

    Entry &entry = EntriesOf(directory)[index];
    PutBigEndian(&entry.tag, ReadBigEndian(tag, 4), 4);
    entry.shape = shape;
    entry.shapeBytes = shapeBytes;
    entry.palette = palette;
    entry.paletteBytes = paletteBytes;
    entry.marker = metalBin;
    entry.markerBytes = metalBinBytes;
    entry.userData = userData;
    entry.userDataBytes = userDataBytes;
    entry.pairs = pairs;
    entry.pairsBytes = pairsBytes;
    entry.text = text;
    entry.textBytes = textBytes;
    entry.name = name;
    entry.flags = variant;
    uint32_t frameDepth = 0;
    if ((info->signature & kInfoSignatureMask) == kInfoSignatureShp && info->size == kInfoFshBytes && InfoWord(info, kInfoFrameDepth) != 0)
        frameDepth = InfoWord(info, kInfoFrameDepth) - 1;
    entry.frameDepth = frameDepth;
    return result;
}

} // namespace fsh

} // namespace gimex
