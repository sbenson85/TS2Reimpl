// Gimex's FSH codec (row 1 of the codec table, lane MP; src/gz/Gimex.h, src/gz/GimexFormats.h): EA's shape files, a
// directory of shapes (frames), each a header, its pixels and attached blocks (palette, text, name). The nine entry
// points are ours, and so are the back end's leaves (lane MQ, 0x88374d..0x885849: src/gz/GimexFshLeaves.h) and its decode
// side (lane MX, 0x88584d..0x8888e7: the directory reader 0x887a06, the shape decoder 0x886e83 and their helpers,
// src/gz/GimexFshDecode.h), and its encode side (lanes MV..MZ: the shape encoder 0x88905f, the directory writer 0x88c719 and
// their helpers, src/gz/GimexFshWrite.h): nothing of the back end is the original's any more.
#include "gz/GimexFormats.h"
#include "gz/GimexFshDecode.h"
#include "gz/GimexFshLeaves.h"
#include "gz/GimexFshWrite.h"

#include <cstdio>
#include <cstring>

namespace gimex {

namespace fsh {

namespace {

constexpr uint32_t kFourcc = 0x53687046;      // "FphS" in memory; the files' "ShpF" chunks read big-endian
constexpr uint32_t kFontFourcc = 0x466e7446;  // "FntF" chunks (fonts)
constexpr uint32_t kInfoVersion = 0x159;
constexpr uint32_t kInfoBytes = 0x2a7c;       // FSH's info block: Info and 0xc bytes more (0x2a78 one of them)
constexpr uint32_t kInfoClearFrom = 0x18;     // the block is zeroed from here to its end
constexpr uint32_t kInstanceBytes = 0x18;
constexpr uint32_t kProbeBytes = 0x20;
constexpr int32_t kScoreShape = 100;
constexpr int32_t kScoreFont = 0x62;
constexpr int32_t kPlatform = 2;              // the back end's platform argument (tentative: 2 the PC)
constexpr int32_t kNoFrame = -1;
constexpr int32_t kQuality = 100;
constexpr uint32_t kChannelBits = 8;

// About's words (immediates in the original's code).
constexpr uint32_t kAboutFlagsMask = 0xffffb7ff;
constexpr uint32_t kAboutFlags = 0x400b7ff;
constexpr uint32_t kAboutWord14 = 0x1f8;
constexpr uint32_t kAboutQuality = 0x4b;
constexpr uint32_t kAboutTag = 0x2e667368;    // "hsf." in memory
constexpr float kRevision = 4.14f;            // the original formats a float constant widened to double

// The directory reader's arguments (0x887a06): parse a stream that starts with neither signature as chunk files too.
constexpr int32_t kScanChunks = 1;
// The directory writer's arguments (0x88c719, tentative): the layout (0 the directory with its header), the header
// written, the alignment masks (16 bytes) and an option left 0.
constexpr int32_t kWriteLayout = 0;
constexpr int32_t kWriteHeader = 1;
constexpr uint32_t kWriteAlignMask = 0xf;
constexpr int32_t kWriteOption = 0;

// The shape header's attribute word (3 bytes little-endian at +1): its bits, and the entry flags each sets.
constexpr uint32_t kAttribute8000 = 0x8000;
constexpr uint32_t kAttribute4000 = 0x4000;
constexpr uint32_t kAttribute2000 = 0x2000;
constexpr uint32_t kAttribute1000 = 0x1000;
constexpr uint32_t kAttribute0800 = 0x800;
constexpr uint32_t kAttribute0400 = 0x400;
constexpr uint32_t kAttribute0200 = 0x200;
constexpr uint32_t kAttribute0100 = 0x100;
constexpr uint32_t kFlag8000 = 0x1000;
constexpr uint32_t kFlag4000 = 0x2000;
constexpr uint32_t kFlag2000 = 0x1000000;
constexpr uint32_t kFlag1000 = 0x2000000;
constexpr uint32_t kFlag0800 = 0x4000000;
constexpr uint32_t kFlag0400 = 0x20000;
constexpr uint32_t kFlag0200 = 0x10000;
constexpr uint32_t kFlag0100 = 0x40000;
constexpr uint32_t kFlagIndexed = 1;          // cleared for a shape of more than 8 bits
// The palette's attribute bit: entries 8..23 of each 32 swapped with 8 more (the PS2's CLUT order).
constexpr uint32_t kPaletteSwizzled = 0x4000;
constexpr uint32_t kSwizzleMask = 0x1f;
constexpr uint32_t kSwizzleLow = 8;
constexpr uint32_t kSwizzleHigh = 0x18;
constexpr uint32_t kSwizzle = 0x18;

// Shape header offsets.
constexpr uint32_t kShapeAttributes = 1;
constexpr uint32_t kShapeOriginX = 0x10;
constexpr uint32_t kShapeOriginY = 0x14;
constexpr uint32_t kShapeWidth = 0x18;
constexpr uint32_t kShapeHeight = 0x1c;
constexpr uint32_t kShapeMarkHigh = 0x20;     // two bytes after the header some 32-bit shapes carry (0x1a, 0xfb)
constexpr uint32_t kShapeMarkLow = 0x21;
constexpr uint32_t kShapeMark = 0x1afb;
constexpr uint8_t kTypeMask = 0x7f;
constexpr uint32_t kPaletteData = 0x20;       // a palette's entries follow its 0x20-byte header
constexpr uint32_t kPaletteCheck = 0x31d;     // a 256-entry palette's 3-byte word there (0: entry 255 transparent)
constexpr int32_t kFullPalette = 0x100;
constexpr int32_t kLastEntry = 0xff;
constexpr uint32_t kSplitAlign = 0x1f;        // type '3': its second half starts on a 32-byte boundary

// Shape types (the header's first byte, the top bit masked off) read for the channel bits.
constexpr uint8_t kType4444 = 0x6d;
constexpr uint8_t kType0655 = 0x67;
constexpr uint8_t kType8800 = 0x65;
constexpr uint8_t kType0484 = 0x68;
constexpr uint8_t kType6666 = 0x66;
constexpr uint8_t kType2101010 = 0x6a;
constexpr uint8_t kTypeDxt3 = 0x61;           // tentative names: their alpha and colour bits
constexpr uint8_t kTypeDxt = 0x62;
constexpr uint8_t kType1e = 0x1e;
constexpr uint8_t kType60 = 0x60;
constexpr uint8_t kTypeGrey4 = 0x10;
constexpr uint8_t kTypeGrey8 = 0x11;
constexpr uint8_t kTypeGreyAlpha = 0x12;
constexpr uint8_t kTypeAlpha8 = 0x64;
constexpr uint32_t kKindDxt = 3;              // the entry's kind (info +0x42c) of a compressed shape (tentative)
constexpr int32_t kBits12 = 12;
constexpr int32_t kBits16 = 16;
constexpr int32_t kBits32 = 32;
constexpr int32_t kBitsIndexed = 8;
constexpr int32_t kBitsDirect = 0x20;
constexpr uint8_t kGreyStep4 = 0x11;
constexpr uint8_t kOpaque = 0xff;
constexpr int32_t kFullScale = 0xff;
constexpr int32_t kSixBitMax = 0x3f;
constexpr int32_t kSixBitHalf = 0x20;
constexpr uint32_t kNibble = 0xf;
constexpr uint32_t kNibbleShift = 4;
constexpr int32_t kMaxChannel = 8;

// The palettes' types (the palette header's first byte, signed).
constexpr char kPalette1555Swizzled = ' ';
constexpr char kPalette8888Swizzled = '!';
constexpr char kPalette666 = '"';
constexpr char kPalette1555Alt = '#';
constexpr char kPalette888 = '$';
constexpr char kPalette565 = ')';
constexpr char kPalette8888 = '*';
constexpr char kPalette8565 = ',';
constexpr char kPalette1555 = '-';
constexpr char kPalette8555 = '.';
constexpr char kPalette88 = '0';
constexpr char kPalette565Alt = '1';
constexpr char kPalette3555 = '2';
constexpr char kPaletteSplit = '3';
constexpr char kPaletteArgb = '8';
constexpr char kPalette1555Other = '9';

// Entry `index` (no bounds: a negative frame reads before the directory, as the original's does).
Entry &EntryAt(Directory *directory, int32_t index)
{
    return reinterpret_cast<Entry *>(reinterpret_cast<uint8_t *>(directory) + kDirectoryHead)[index];
}

template <class T> T Load(const void *at)
{
    T value;
    std::memcpy(&value, at, sizeof(T));
    return value;
}

Directory *DirectoryOf(const Instance *instance)
{
    return reinterpret_cast<Directory *>(static_cast<uintptr_t>(instance->unknown14));
}

// A 12-bit signed origin (the low 12 bits of the header's 16-bit word).
int32_t Origin(const uint8_t *at)
{
    return static_cast<int16_t>(static_cast<uint16_t>(Load<uint16_t>(at) << 4)) >> 4;
}

uint8_t *PaletteEntry(Info *info, int32_t index)
{
    return reinterpret_cast<uint8_t *>(info->palette) + 4 * index;
}

void SetBits(Info *info, uint32_t alpha, uint32_t red, uint32_t green, uint32_t blue)
{
    info->alphaBits = alpha;
    info->redBits = red;
    info->greenBits = green;
    info->blueBits = blue;
}

// The channel bits of a shape of more than 8 bits (direct colour).
void DescribeDirect(Info *info, const uint8_t *shape, int32_t bits)
{
    const uint8_t type = shape[0] & kTypeMask;
    info->bitsPerPixel = kBitsDirect;
    info->sourceBits = bits;
    info->paletteCount = 0;
    if (bits < kBits16) SetBits(info, 1, 5, 5, 5);
    if (bits == kBits12) {
        SetBits(info, 0, 2, 8, 2);
        info->sourceBits = kBits12;
    } else if (bits == kBits16) {
        if (type == kType4444) {
            SetBits(info, 4, 4, 4, 4);
        } else if (type == kType0655) {
            SetBits(info, 0, 6, 5, 5);
        } else if (type == kType8800) {
            SetBits(info, 8, 8, 0, 0);
        } else if (type == kType0484) {
            SetBits(info, 0, 4, 8, 4);
        } else {
            SetBits(info, 0, 5, 6, 5);
        }
    } else if (bits > kBits16) {
        if (type == kType6666) {
            SetBits(info, 6, 6, 6, 6);
        } else if (type == kType2101010) {
            SetBits(info, 2, 10, 10, 10);
        } else {
            int32_t channel = bits / 3;
            if (channel > kMaxChannel) channel = kMaxChannel;
            const uint32_t c = static_cast<uint32_t>(channel);
            SetBits(info, static_cast<uint32_t>(bits - 3 * channel), c, c, c);
        }
    }
    if ((bits == kBits32 && info->jpegFlag == kKindDxt) || type == kTypeDxt3 || type == kTypeDxt) {
        const uint32_t mark = static_cast<uint32_t>(shape[kShapeMarkHigh]) << 8 | shape[kShapeMarkLow];
        if (mark == kShapeMark || type == kTypeDxt3) {
            SetBits(info, 4, 5, 6, 5);
        } else {
            SetBits(info, 3, 5, 6, 5);
        }
    }
    if ((bits == kBits16 && info->jpegFlag == kKindDxt) || type == kType1e || type == kType60) SetBits(info, 1, 5, 6, 5);
}

// The made-up palette of an indexed shape with none attached: by the shape's type, else a grey ramp.
void DescribeImplicitPalette(Info *info, const uint8_t *shape, int32_t count)
{
    const uint8_t type = shape[0] & kTypeMask;
    info->paletteCount = static_cast<uint32_t>(count);
    if (type == kTypeGrey4) {
        SetBits(info, 0, 4, 0, 0);
        uint8_t grey = 0;
        for (int32_t i = 0; i < count; ++i, grey = static_cast<uint8_t>(grey + kGreyStep4)) {
            uint8_t *entry = PaletteEntry(info, i);
            entry[0] = entry[1] = entry[2] = grey;
            entry[3] = kOpaque;
        }
    } else if (type == kTypeGrey8) {
        SetBits(info, 0, 8, 0, 0);
        for (int32_t i = 0; i < count; ++i) {
            uint8_t *entry = PaletteEntry(info, i);
            entry[0] = entry[1] = entry[2] = static_cast<uint8_t>(i);
            entry[3] = kOpaque;
        }
    } else if (type == kTypeGreyAlpha) {
        SetBits(info, 4, 4, 0, 0);
        for (int32_t i = 0; i < count; ++i) {
            uint8_t *entry = PaletteEntry(info, i);
            entry[3] = static_cast<uint8_t>((i >> kNibbleShift) * kGreyStep4);
            entry[0] = entry[1] = entry[2] = static_cast<uint8_t>((i & kNibble) * kGreyStep4);
        }
    } else if (type == kTypeAlpha8) {
        SetBits(info, 8, 0, 0, 0);
        for (int32_t i = 0; i < count; ++i) {
            uint8_t *entry = PaletteEntry(info, i);
            entry[0] = entry[1] = entry[2] = kOpaque;
            entry[3] = static_cast<uint8_t>(i);
        }
    } else {
        info->alphaBits = 0;
        if (count < 2) return;
        for (int32_t i = 0; i < count; ++i) {
            uint8_t *entry = PaletteEntry(info, i);
            entry[3] = kOpaque;
            entry[0] = entry[1] = entry[2] = static_cast<uint8_t>(kFullScale * i / (count - 1));
        }
    }
}

// Entry j of 32 (8..23 swapped with the next or previous 8) of a swizzled palette.
int32_t Unswizzle(int32_t index, bool swizzled)
{
    const uint32_t low = static_cast<uint32_t>(index) & kSwizzleMask;
    if (swizzled && low >= kSwizzleLow && low < kSwizzleHigh) return index ^ static_cast<int32_t>(kSwizzle);
    return index;
}

// The attached palette, converted to B, G, R, A by its type.
void DescribePalette(Info *info, const uint8_t *palette, int32_t bits)
{
    int32_t start;
    const int32_t n = PaletteEntries(palette, &start, bits, kPlatform);
    const bool swizzled = (ReadLittleEndian(palette + kShapeAttributes, 3) & kPaletteSwizzled) != 0;
    info->paletteCount = static_cast<uint32_t>(start + n);
    const uint8_t *p = palette + kPaletteData;
    auto word = [p](int32_t at) { return static_cast<uint32_t>(Load<uint16_t>(p + at)); };
    auto put = [info, start](int32_t i, uint32_t colour) {
        std::memcpy(PaletteEntry(info, start + i), &colour, sizeof(colour));
    };
    switch (static_cast<char>(palette[0])) {
    case kPalette1555Swizzled:
        SetBits(info, 1, 5, 5, 5);
        for (int32_t i = 0; i < n; ++i) put(i, Colour1555Swizzled(word(2 * Unswizzle(i, swizzled))));
        break;
    case kPalette8888Swizzled:
        SetBits(info, 8, 8, 8, 8);
        for (int32_t i = 0; i < n; ++i) {
            const uint8_t *s = p + 4 * Unswizzle(i, swizzled);
            uint8_t *d = PaletteEntry(info, start + i);
            d[2] = s[0];
            d[1] = s[1];
            d[0] = s[2];
            d[3] = s[3];
        }
        break;
    case kPalette666:
    case kPalette888: {
        const bool sixBit = palette[0] == kPalette666;
        if (sixBit) {
            SetBits(info, 0, 6, 6, 6);
        } else {
            SetBits(info, 0, 8, 8, 8);
        }
        if (n == kFullPalette && ReadLittleEndian(palette + kPaletteCheck, 3) == 0) info->alphaBits = 1;
        for (int32_t i = 0; i < n; ++i) {
            const uint8_t *s = p + 3 * i;
            uint8_t *d = PaletteEntry(info, start + i);
            d[3] = i == kLastEntry && info->alphaBits != 0 ? 0 : kOpaque;
            for (int32_t k = 0; k < 3; ++k) {
                const int32_t value = static_cast<int8_t>(s[k]);
                d[2 - k] = sixBit ? static_cast<uint8_t>((value * kFullScale + kSixBitHalf) / kSixBitMax) : s[k];
            }
        }
        break;
    }
    case kPalette1555Alt:
        SetBits(info, 1, 5, 5, 5);
        for (int32_t i = 0; i < n; ++i) put(i, Colour1555Alt(word(2 * i)));
        break;
    case kPalette565:
    case kPalette565Alt:
        SetBits(info, 0, 5, 6, 5);
        for (int32_t i = 0; i < n; ++i) put(i, Colour565(word(2 * i)));
        break;
    case kPalette8888:
        SetBits(info, 8, 8, 8, 8);
        for (int32_t i = 0; i < n; ++i) std::memcpy(PaletteEntry(info, start + i), p + 4 * i, 4);
        break;
    case kPalette8565:
        SetBits(info, 8, 5, 6, 5);
        for (int32_t i = 0; i < n; ++i) {
            put(i, Colour565(word(4 * i)));
            PaletteEntry(info, start + i)[3] = p[4 * i + 3];
        }
        break;
    case kPalette1555:
    case kPalette1555Other:
        SetBits(info, 1, 5, 5, 5);
        for (int32_t i = 0; i < n; ++i) put(i, Colour1555(word(2 * i)));
        break;
    case kPalette8555:
        SetBits(info, 8, 5, 5, 5);
        for (int32_t i = 0; i < n; ++i) {
            put(i, Colour1555(word(4 * i)));
            PaletteEntry(info, start + i)[3] = p[4 * i + 3];
        }
        break;
    case kPalette88:
        SetBits(info, 8, 8, 0, 0);
        for (int32_t i = 0; i < n; ++i) put(i, Colour88(word(2 * i)));
        break;
    case kPalette3555:
        SetBits(info, 3, 5, 5, 5);
        for (int32_t i = 0; i < n; ++i) put(i, Colour3555(word(2 * i)));
        break;
    case kPaletteSplit: {
        SetBits(info, 8, 8, 8, 8);
        const uint8_t *q = p + ((static_cast<uint32_t>(2 * n) + kSplitAlign) & ~kSplitAlign);
        for (int32_t i = 0; i < n; ++i) {
            uint8_t *d = PaletteEntry(info, start + i);
            d[3] = p[2 * i];
            d[2] = p[2 * i + 1];
            d[1] = q[2 * i];
            d[0] = q[2 * i + 1];
        }
        break;
    }
    case kPaletteArgb:
        SetBits(info, 8, 8, 8, 8);
        for (int32_t i = 0; i < n; ++i) {
            const uint8_t *s = p + 4 * i;
            uint8_t *d = PaletteEntry(info, start + i);
            d[3] = s[0];
            d[2] = s[1];
            d[1] = s[2];
            d[0] = s[3];
        }
        break;
    default:
        break;
    }
}

} // namespace

About *__stdcall NewAbout()
{
    About *about = static_cast<About *>(Allocate(sizeof(About)));
    if (about != nullptr) {
        std::memset(about, 0, sizeof(About));
        about->size = sizeof(About);
        about->flags = (about->flags & kAboutFlagsMask) | kAboutFlags;
        about->fourcc = kFourcc;
        about->version = kInfoVersion;
        about->unknown14 = kAboutWord14;
        about->defaultQuality = kAboutQuality;
        about->unknown1c = kAboutTag;
        std::strcpy(about->extensions[0], ".fsh");
        std::strcpy(about->extensions[1], ".newfsh");
        std::strcpy(about->author, "F Barchard");
        std::sprintf(about->revision, "%.2f", static_cast<double>(kRevision));
        std::strcpy(about->shortName, "FSH");
        std::strcpy(about->typeName, "FSH");
        std::strcpy(about->description, "PC Shape File");
    }
    return about;
}

int32_t __stdcall Probe(ifaces::cIGZFile *stream)
{
    int32_t score = 0;
    StreamSeek(stream, 0, 0);
    uint8_t head[kProbeBytes];
    if (StreamRead(stream, head, kProbeBytes) == static_cast<int32_t>(kProbeBytes)) {
        const uint32_t signature = ReadBigEndian(head, 4);
        if (signature == kFourcc) {
            score = kScoreShape;
        } else if (signature == kFontFourcc) {
            score = kScoreFont;
        }
    }
    return score;
}

int32_t __stdcall Open(Instance **out, ifaces::cIGZFile *stream, const char *, int32_t)
{
    Directory *directory = nullptr;
    if (ReadDirectoryStream(stream, &directory, kScanChunks, kPlatform, kFourcc, kFontFourcc, 0) == 0) return 0;
    Instance *instance = static_cast<Instance *>(Allocate(kInstanceBytes));
    int32_t frames = 0;
    if (instance != nullptr) {
        // ORIGINAL-BUG: a reader answering non-zero with no directory makes this read null (both crash).
        frames = directory->count;
        instance->fourcc = kFourcc;
        instance->size = kInstanceBytes;
        instance->frameCount = static_cast<uint32_t>(frames);
        instance->unknownC = 0;
        instance->stream = stream;
        instance->unknown14 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(directory));
    }
    *out = instance;
    return frames;
}

Info *__stdcall GetInfo(Instance *instance, int32_t frame)
{
    Directory *directory = DirectoryOf(instance);
    if (frame >= directory->count) return nullptr;
    Entry &entry = EntryAt(directory, frame);
    const uint8_t *shape = entry.shape;
    if (shape == nullptr) return nullptr;
    const int32_t width = Load<int32_t>(shape + kShapeWidth);
    const int32_t height = Load<int32_t>(shape + kShapeHeight);
    const int32_t originX = Origin(shape + kShapeOriginX);
    const int32_t originY = Origin(shape + kShapeOriginY);
    const uint32_t attributes = ReadLittleEndian(shape + kShapeAttributes, 3);
    if (attributes & kAttribute8000) entry.flags |= kFlag8000;
    if (attributes & kAttribute4000) entry.flags |= kFlag4000;
    if (attributes & kAttribute2000) entry.flags |= kFlag2000;
    if (attributes & kAttribute1000) entry.flags |= kFlag1000;
    if (attributes & kAttribute0800) entry.flags |= kFlag0800;
    if (attributes & kAttribute0400) entry.flags |= kFlag0400;
    if (attributes & kAttribute0200) entry.flags |= kFlag0200;
    if (attributes & kAttribute0100) entry.flags |= kFlag0100;
    int32_t bits = ShapeBits(shape, kPlatform);
    if (bits > kBitsIndexed) entry.flags &= ~kFlagIndexed;
    if (frame >= static_cast<int32_t>(instance->frameCount)) return nullptr;
    Info *info = static_cast<Info *>(Allocate(kInfoBytes));
    if (info == nullptr) return nullptr;
    std::memset(reinterpret_cast<uint8_t *>(info) + kInfoClearFrom, 0, kInfoBytes - kInfoClearFrom);
    info->signature = kFourcc;
    info->size = kInfoBytes;
    info->version = kInfoVersion;
    info->unknownC = static_cast<uint32_t>(frame);
    info->width = width;
    info->height = height;
    info->variant = entry.flags;
    info->jpegFlag = entry.kind;
    info->imageBytes = entry.imageBytes;
    info->redBits = kChannelBits;
    info->greenBits = kChannelBits;
    info->blueBits = kChannelBits;
    info->originX = originX;
    info->originY = originY;
    info->quality = kQuality;
    info->alphaBits = 1;
    info->unknown458 = 0;
    char *name = reinterpret_cast<char *>(info->unknown45c);
    // ORIGINAL-BUG: the name is copied unbounded (one longer than the block's 0x2620 bytes left runs past it).
    if (entry.name != nullptr) std::strcpy(name, entry.name);
    for (int32_t i = 3; i >= 0 && name[i] == ' '; --i) name[i] = 0;
    if (ReadShapePairs(directory, frame, reinterpret_cast<uint8_t *>(info), kPlatform) == 0) {
        FreeMemory(info);
        return nullptr;
    }
    ReadShapeText(directory, frame, reinterpret_cast<uint8_t *>(info));
    ReadShapeMarker(directory, frame, reinterpret_cast<uint8_t *>(info));
    ReadShapeExtra(directory, frame, reinterpret_cast<uint8_t *>(info), kPlatform);
    if (bits > kBitsIndexed) {
        DescribeDirect(info, shape, bits);
        return info;
    }
    info->bitsPerPixel = kBitsIndexed;
    if (bits == 0) bits = kBitsIndexed;
    info->sourceBits = bits;
    const int32_t count = static_cast<int32_t>(1u << (static_cast<uint32_t>(bits) & 0x1f));
    if (entry.palette == nullptr) {
        DescribeImplicitPalette(info, shape, count);
    } else {
        DescribePalette(info, entry.palette, bits);
    }
    return info;
}

int32_t __stdcall ReadFrame(Instance *instance, Info *info, void *pixels, int32_t pitch)
{
    const int32_t frame = static_cast<int32_t>(info->unknownC);
    if (frame == kNoFrame) return 0;
    uint8_t *shape = EntryAt(DirectoryOf(instance), frame).shape;
    if (shape == nullptr) return 0;
    return DecodeShape(shape, static_cast<uint8_t *>(pixels), pitch, kPlatform);
}

int32_t __stdcall Close(Instance *instance)
{
    if (instance != nullptr) {
        FreeDirectory(DirectoryOf(instance));
        FreeMemory(instance);
    }
    return 1;
}

int32_t __stdcall OpenWriter(Instance **out, ifaces::cIGZFile *stream, const char *, int32_t frames)
{
    const uint32_t bytes = static_cast<uint32_t>(frames) * sizeof(Entry) + kDirectoryHead;
    Directory *directory = static_cast<Directory *>(Allocate(bytes));
    if (directory == nullptr) return 0;
    std::memset(directory, 0, bytes);
    directory->count = frames;
    // ORIGINAL-BUG: when the instance can't be allocated the directory leaks.
    Instance *instance = static_cast<Instance *>(Allocate(kInstanceBytes));
    if (instance == nullptr) return 0;
    instance->stream = stream;
    instance->unknownC = 0;
    instance->frameCount = static_cast<uint32_t>(frames);
    instance->fourcc = kFourcc;
    instance->size = kInstanceBytes;
    instance->unknown14 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(directory));
    *out = instance;
    return 1;
}

int32_t __stdcall WriteFrame(Instance *instance, const Info *info, const void *pixels, int32_t pitch)
{
    const int32_t index = static_cast<int32_t>(instance->unknownC);
    if (index >= static_cast<int32_t>(instance->frameCount)) return 0;
    const int32_t result = EncodeShape(DirectoryOf(instance), info, static_cast<const uint8_t *>(pixels), pitch, index, kPlatform);
    ++instance->unknownC;
    return result;
}

int32_t __stdcall CloseWriter(Instance *instance)
{
    if (instance == nullptr) return 0;
    Directory *directory = DirectoryOf(instance);
    directory->count = static_cast<int32_t>(instance->unknownC);
    const int32_t result = static_cast<int32_t>(WriteDirectory(instance->stream, directory, kPlatform, kWriteLayout, kWriteHeader, kFourcc,
                                                               kWriteAlignMask, kWriteAlignMask, kWriteAlignMask, kWriteOption, kWriteAlignMask));
    FreeWrittenDirectory(directory);
    FreeMemory(instance);
    return result;
}

} // namespace fsh

} // namespace gimex
