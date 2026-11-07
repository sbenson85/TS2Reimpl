// Gimex's PNG codec through the libpng / zlib in the image (row 4 of the codec table, lane MO): about, is, open, info, read,
// write and close, the row converters and the callbacks the game gives the library. The library's own functions
// (png_create_read_struct_2, png_read_image, ...) stay the original's: this is the glue the game wrote around them.
//
// The library's structures are handled as raw bytes at the offsets the glue uses (the image's libpng is 1.0.5 with
// PNG_USER_MEM_SUPPORTED): the png_struct starts with its jump buffer; the png_info holds width, height, rowbytes, the
// palette and the bit depth and colour type bytes.
#include "gz/GimexFormats.h"

#include "game/game.h"
#include "game/original.h"
#include "ifaces/cIGZFile.h"

#include <xmmintrin.h>

#include <csetjmp>
#include <cstdint>
#include <cstring>

namespace gimex {

namespace png {

namespace {

constexpr uint32_t kFourcc = 0x504e47; // "GNP" in memory
constexpr uint32_t kInfoVersion = 0x159;
constexpr uint32_t kInstanceBytes = 0x18;
constexpr uint32_t kLibraryBytes = 8; // the instance's png_ptr and info_ptr
constexpr uint32_t kScoreFull = 0x64;
constexpr uint32_t kScoreShort = 0x32;
constexpr uint32_t kSignatureBytes = 8;
constexpr uint32_t kSignatureShortBytes = 4;

// About.
constexpr uint32_t kAboutFlags = 0xa3d7;
constexpr uint32_t kAboutDefaultQuality = 0x64;

// The libpng build's options and the game's data the glue points the library at.
constexpr uint32_t kVersionString = 0xe4a638;      // "1.0.5"
constexpr uint32_t kReadErrorText = 0xe4a5fc;      // "Read Error"
constexpr uint32_t kWriteErrorText = 0xe4a644;     // "Write Error"
constexpr uint32_t kNoOpCallback = 0x140fe0;       // a function that does nothing: the warning and flush callback
constexpr uint32_t kInchPerMeterBits = 0x3d214285; // 0.0393700786 as the game's float cell holds it

// png_info offsets.
constexpr uint32_t kPngWidth = 0x00;
constexpr uint32_t kPngHeight = 0x04;
constexpr uint32_t kPngValid = 0x08;
constexpr uint32_t kPngRowBytes = 0x0c;
constexpr uint32_t kPngPalette = 0x10;
constexpr uint32_t kPngPaletteCount = 0x14; // a word
constexpr uint32_t kPngBitDepth = 0x18;     // a byte
constexpr uint32_t kPngColorType = 0x19;    // a byte
constexpr uint32_t kPngValidPalette = 8;

// The colour types, and the alpha bit of them.
constexpr uint8_t kColorGray = 0;
constexpr uint8_t kColorRgb = 2;
constexpr uint8_t kColorPalette = 3;
constexpr uint8_t kColorGrayAlpha = 4;
constexpr uint8_t kColorRgba = 6;
constexpr uint8_t kColorAlphaBit = 4;

// Offsets in the Info the glue fills beyond the common head (the palette is 4 bytes an entry from 0x28).
constexpr uint32_t kInfoTextCell = 0x65c; // png_get_text's text pointer lands here
constexpr uint32_t kInfoQuality = 100;
constexpr uint32_t kBitsRgb = 0x18;
constexpr uint32_t kBitsRgba = 0x20;
constexpr uint32_t kInfoBitsDirect = 0x20;
constexpr uint32_t kInfoBitsIndexed = 8;
constexpr int32_t kLibraryByteBits = 8;
constexpr uint32_t kUnitMeter = 1;
constexpr int32_t kWriteOk = 1;

using CreateReadFn = void *(__cdecl *)(const char *version, void *errorPtr, void *errorFn, void *warnFn, void *memPtr, void *mallocFn,
                                       void *freeFn);
using CreateInfoFn = void *(__cdecl *)(void *png);
using SetReadFn = void(__cdecl *)(void *png, void *io, void *readFn);
using SetWriteFn = void(__cdecl *)(void *png, void *io, void *writeFn, void *flushFn);
using PngFn = void(__cdecl *)(void *png);
using PngInfoFn = void(__cdecl *)(void *png, void *info);
using DestroyReadFn = void(__cdecl *)(void *png, void *info, void *end);
using DestroyWriteFn = void(__cdecl *)(void *png, void *info);
using GetTextFn = uint32_t(__cdecl *)(void *png, void *info, void *textPtr, void *textCount);
using GetPixelsFn = uint32_t(__cdecl *)(void *png, void *info);
using GetTrnsFn = uint32_t(__cdecl *)(void *png, void *info, void *trans, void *count, void *values);
using CheckSigFn = int32_t(__cdecl *)(const void *bytes, int32_t count);
using GetIoFn = void *(__cdecl *)(void *png);
using ErrorFn = void(__cdecl *)(void *png, const char *message);
using ImageFn = void(__cdecl *)(void *png, void *rows);
using SetTrnsFn = void(__cdecl *)(void *png, void *info, const void *trans, int32_t count, const void *values);
using SetPhysFn = void(__cdecl *)(void *png, void *info, int32_t x, int32_t y, int32_t unit);

// The stream and memory helpers are Gimex's shared ones (src/gz/Gimex.h).

// The library (original calls).
__declspec(noinline) void *CreateReadStruct(void *mallocFn, void *freeFn)
{
    return OriginalFunction<CreateReadFn, 0x88eaa6>()(GameAddress<const char>(kVersionString), nullptr, nullptr,
                                                      GameAddress<void>(kNoOpCallback), nullptr, mallocFn, freeFn);
}
__declspec(noinline) void *CreateWriteStruct(void *mallocFn, void *freeFn)
{
    return OriginalFunction<CreateReadFn, 0x893bd2>()(GameAddress<const char>(kVersionString), nullptr, nullptr,
                                                      GameAddress<void>(kNoOpCallback), nullptr, mallocFn, freeFn);
}
__declspec(noinline) void *CreateInfoStruct(void *png)
{
    return OriginalFunction<CreateInfoFn, 0x88e903>()(png);
}
__declspec(noinline) void SetReadFunction(void *png, void *io, void *readFn)
{
    OriginalFunction<SetReadFn, 0x893939>()(png, io, readFn);
}
__declspec(noinline) void SetWriteFunction(void *png, void *io, void *writeFn)
{
    OriginalFunction<SetWriteFn, 0x894828>()(png, io, writeFn, GameAddress<void>(kNoOpCallback));
}
__declspec(noinline) void ReadInfo(void *png, void *info)
{
    OriginalFunction<PngInfoFn, 0x88eee2>()(png, info);
}
__declspec(noinline) void DestroyReadStruct(void *pngCell, void *infoCell)
{
    OriginalFunction<DestroyReadFn, 0x88ebd4>()(pngCell, infoCell, nullptr);
}
__declspec(noinline) void DestroyWriteStruct(void *pngCell, void *infoCell)
{
    OriginalFunction<DestroyWriteFn, 0x893c9a>()(pngCell, infoCell);
}
__declspec(noinline) uint32_t GetText(void *png, void *info, void *textPtr, void *textCount)
{
    return OriginalFunction<GetTextFn, 0x893b7a>()(png, info, textPtr, textCount);
}
__declspec(noinline) uint32_t GetXPixelsPerMeter(void *png, void *info)
{
    return OriginalFunction<GetPixelsFn, 0x893bb0>()(png, info);
}
__declspec(noinline) uint32_t GetTransparency(void *png, void *info, void *trans, void *count, void *values)
{
    return OriginalFunction<GetTrnsFn, 0x893b11>()(png, info, trans, count, values);
}
__declspec(noinline) int32_t CheckSignature(const void *bytes, int32_t count)
{
    return OriginalFunction<CheckSigFn, 0x88e8eb>()(bytes, count);
}
__declspec(noinline) void *GetIoPointer(void *png)
{
    return OriginalFunction<GetIoFn, 0x88e92d>()(png);
}
__declspec(noinline) void RaiseError(void *png, const char *message)
{
    OriginalFunction<ErrorFn, 0x893a31>()(png, message);
}
__declspec(noinline) void StripSixteen(void *png)
{
    OriginalFunction<PngFn, 0x8935a8>()(png);
}
__declspec(noinline) void SetPacking(void *png)
{
    OriginalFunction<PngFn, 0x8938c9>()(png);
}
__declspec(noinline) void GrayToRgb(void *png)
{
    OriginalFunction<PngFn, 0x89359c>()(png);
}
__declspec(noinline) void ReadUpdateInfo(void *png, void *info)
{
    OriginalFunction<PngInfoFn, 0x88f4b0>()(png, info);
}
__declspec(noinline) void ReadImage(void *png, void *rows)
{
    OriginalFunction<ImageFn, 0x88ee8b>()(png, rows);
}
__declspec(noinline) void WriteImage(void *png, void *rows)
{
    OriginalFunction<ImageFn, 0x894312>()(png, rows);
}
__declspec(noinline) void SetTransparency(void *png, void *info, const void *trans, int32_t count)
{
    OriginalFunction<SetTrnsFn, 0x894cda>()(png, info, trans, count, nullptr);
}
__declspec(noinline) void SetPhysical(void *png, void *info, int32_t pixelsPerMeter)
{
    OriginalFunction<SetPhysFn, 0x894bce>()(png, info, pixelsPerMeter, pixelsPerMeter, static_cast<int32_t>(kUnitMeter));
}
__declspec(noinline) void WriteInfo(void *png, void *info)
{
    OriginalFunction<PngInfoFn, 0x89435a>()(png, info);
}
__declspec(noinline) void WriteEnd(void *png, void *info)
{
    OriginalFunction<PngInfoFn, 0x89419a>()(png, info);
}

template <class T> T &Field(void *base, uint32_t offset)
{
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(base) + offset);
}

float InchPerMeter()
{
    float value;
    const uint32_t bits = kInchPerMeterBits;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// The instance's library block: the png_ptr and the info_ptr, in that order.
struct Library {
    void *png;
    void *info;
};

// The png_struct opens with its jump buffer: setjmp lands there, and the library's error path jumps back to it.
inline std::jmp_buf &JumpBuffer(void *png)
{
    return *static_cast<std::jmp_buf *>(png);
}

} // namespace

// 0x8800aa (__cdecl): the library's free callback.
void __cdecl FreeCallback(void *, void *memory)
{
    FreeMemory(memory);
}

// 0x8800b4 (__cdecl): the library's allocation callback.
void *__cdecl AllocateCallback(void *, uint32_t size)
{
    return Allocate(size);
}

// 0x88062e (__cdecl): the library's read callback; a short read is an error.
void __cdecl ReadCallback(void *png, void *buffer, int32_t count)
{
    ifaces::cIGZFile *const stream = static_cast<ifaces::cIGZFile *>(GetIoPointer(png));
    if (StreamRead(stream, buffer, count) != count) RaiseError(png, GameAddress<const char>(kReadErrorText));
}

// 0x880a3a (__cdecl): the library's write callback; a short write is an error.
void __cdecl WriteCallback(void *png, const void *buffer, int32_t count)
{
    ifaces::cIGZFile *const stream = static_cast<ifaces::cIGZFile *>(GetIoPointer(png));
    if (StreamWrite(stream, buffer, count) != count) RaiseError(png, GameAddress<const char>(kWriteErrorText));
}

// 0x880c5d (__cdecl): 1 when every palette entry is gray (red = green = blue), or the count is not positive.
int32_t __cdecl IsGrayPalette(const uint8_t *palette, int32_t count)
{
    int32_t gray = 1;
    for (int32_t index = 0; index < count && gray != 0; ++index) {
        const uint8_t *const entry = palette + index * 4;
        if (entry[2] != entry[1] || entry[2] != entry[0]) gray = 0;
    }
    return gray;
}

// 0x8800be: the format's about block.
About *__stdcall NewAbout()
{
    About *about = static_cast<About *>(Allocate(sizeof(About)));
    if (about == nullptr) return nullptr;
    std::memset(about, 0, sizeof(About));
    about->size = sizeof(About);
    about->version = kInfoVersion;
    about->flags = kAboutFlags;
    about->defaultQuality = kAboutDefaultQuality;
    about->unknown1c = kFourcc;
    about->fourcc = kFourcc;
    std::strcpy(about->extensions[0], ".png");
    std::strcpy(about->author, "Kaz Okuda");
    std::strcpy(about->revision, "1.10");
    std::strcpy(about->shortName, "PNG");
    std::strcpy(about->typeName, "PNG");
    std::strcpy(about->description, "Portable Network Graphics");
    return about;
}

// 0x8803e9 (is): the eight signature bytes score 100, the first four alone 50.
int32_t __stdcall Probe(ifaces::cIGZFile *stream)
{
    uint8_t signature[kSignatureBytes] = {};
    StreamSeek(stream, 0, 0);
    StreamRead(stream, signature, kSignatureBytes);
    if (CheckSignature(signature, kSignatureBytes) != 0) return static_cast<int32_t>(kScoreFull);
    if (CheckSignature(signature, kSignatureShortBytes) != 0) return static_cast<int32_t>(kScoreShort);
    return 0;
}

// 0x880435 (open): reads the header into a library state the instance keeps.
int32_t __stdcall Open(Instance **out, ifaces::cIGZFile *stream, const char *, int32_t)
{
    Instance *const instance = static_cast<Instance *>(Allocate(kInstanceBytes));
    if (instance == nullptr) return 0;
    std::memset(instance, 0, kInstanceBytes);
    Library *const library = static_cast<Library *>(Allocate(kLibraryBytes));
    instance->unknown14 = reinterpret_cast<uintptr_t>(library) & 0xffffffffu;
    if (library == nullptr) return 0;
    library->png = CreateReadStruct(reinterpret_cast<void *>(&AllocateCallback), reinterpret_cast<void *>(&FreeCallback));
    if (library->png == nullptr) return 0;
    library->info = CreateInfoStruct(library->png);
    if (library->info == nullptr) {
        DestroyReadStruct(library, nullptr);
        return 0;
    }
    SetReadFunction(library->png, stream, reinterpret_cast<void *>(&ReadCallback));
    if (setjmp(JumpBuffer(library->png)) != 0) {
        DestroyReadStruct(&library->png, &library->info);
        return 0;
    }
    StreamSeek(stream, 0, 0);
    ReadInfo(library->png, library->info);
    instance->frameCount = 1;
    instance->unknownC = 0;
    instance->fourcc = kFourcc;
    instance->size = kInstanceBytes;
    instance->stream = stream;
    *out = instance;
    return static_cast<int32_t>(instance->frameCount);
}

// 0x880168 (close and the PNG reader's wclose slot is the shared one): frees the library state and the instance.
int32_t __stdcall Close(Instance *instance)
{
    if (instance == nullptr) return 0;
    Library *const library = reinterpret_cast<Library *>(static_cast<uintptr_t>(instance->unknown14));
    if (library != nullptr) {
        DestroyReadStruct(&library->png, &library->info);
        FreeMemory(library);
    }
    return FreeMemory(instance);
}

// 0x88019a: describes the picture from the header the open read.
Info *__stdcall GetInfo(Instance *instance, int32_t)
{
    Library *const library = reinterpret_cast<Library *>(static_cast<uintptr_t>(instance->unknown14));
    void *const pngInfo = library->info;
    Info *const info = static_cast<Info *>(Allocate(kWriteInfoSize));
    if (info == nullptr) return nullptr;
    std::memset(info, 0, kWriteInfoSize);
    const uint8_t colorType = Field<uint8_t>(pngInfo, kPngColorType);
    const uint8_t bitDepth = Field<uint8_t>(pngInfo, kPngBitDepth);
    // The bits of a pixel as the library hands them over (a bit depth under 8 is kept; deeper gray and palettes are bytes),
    // and the bits of a channel (a palette's entries are bytes).
    uint32_t bits;
    switch (colorType) {
    case kColorGray:
    case kColorPalette:
        bits = bitDepth < kLibraryByteBits ? bitDepth : static_cast<uint32_t>(kLibraryByteBits);
        break;
    case kColorRgb:
        bits = kBitsRgb;
        break;
    default:
        bits = kBitsRgba;
        break;
    }
    const uint32_t channelBits = colorType == kColorPalette ? static_cast<uint32_t>(kLibraryByteBits) : bitDepth;
    info->signature = kFourcc;
    info->size = kWriteInfoSize;
    info->version = kInfoVersion;
    info->width = Field<int32_t>(pngInfo, kPngWidth);
    info->height = Field<int32_t>(pngInfo, kPngHeight);
    info->alphaBits = (colorType & kColorAlphaBit) != 0 ? channelBits : 0;
    info->quality = kInfoQuality;
    info->bitsPerPixel = bits > static_cast<uint32_t>(kLibraryByteBits) ? kInfoBitsDirect : kInfoBitsIndexed;
    info->redBits = channelBits;
    info->greenBits = channelBits;
    info->blueBits = channelBits;
    info->sourceBits = static_cast<int32_t>(bits);
    info->paletteCount = 0;
    int32_t textCount;
    GetText(library->png, pngInfo, reinterpret_cast<uint8_t *>(info) + kInfoTextCell, &textCount);
    const uint32_t pixelsPerMeter = GetXPixelsPerMeter(library->png, pngInfo);
    info->dotsPerInch = static_cast<float>(static_cast<double>(pixelsPerMeter)) * InchPerMeter();
    if (bits > static_cast<uint32_t>(kLibraryByteBits)) return info;
    if (colorType == kColorGray) {
        const int32_t count = 1 << bits;
        info->paletteCount = static_cast<uint32_t>(count);
        if (count <= 0) return info;
        int32_t accumulated = 0;
        for (int32_t index = 0; index < count; ++index) {
            const uint8_t value = static_cast<uint8_t>(accumulated / (count - 1));
            accumulated += 0xff;
            info->palette[index][0] = value;
            info->palette[index][1] = value;
            info->palette[index][2] = value;
            info->palette[index][3] = 0xff;
        }
        return info;
    }
    const uint16_t paletteCount = Field<uint16_t>(pngInfo, kPngPaletteCount);
    info->paletteCount = paletteCount;
    if (paletteCount == 0) {
        FreeMemory(info);
        return nullptr;
    }
    const uint8_t *transparency = nullptr;
    int32_t transparencyCount = 0;
    void *transparencyValues = nullptr;
    if (GetTransparency(library->png, pngInfo, &transparency, &transparencyCount, &transparencyValues) != 0) {
        info->alphaBits = static_cast<uint32_t>(kLibraryByteBits);
    } else {
        transparency = nullptr;
    }
    const uint8_t *const palette = Field<const uint8_t *>(pngInfo, kPngPalette);
    for (int32_t index = 0; index < static_cast<int32_t>(info->paletteCount); ++index) {
        info->palette[index][2] = palette[3 * index];
        info->palette[index][1] = palette[3 * index + 1];
        info->palette[index][0] = palette[3 * index + 2];
        info->palette[index][3] = transparency != nullptr && index < transparencyCount ? transparency[index] : 0xff;
    }
    return info;
}

// 0x88065c (__cdecl): reads the rows into the caller's buffer (32-bit pixels converted from RGB or RGBA).
int32_t __cdecl ReadRows(const Info *info, void *pngInfo, void *png, uint8_t *pixels, int32_t pitch)
{
    if (setjmp(JumpBuffer(png)) != 0) return 0;
    const int32_t height = info->height;
    void **const rows = static_cast<void **>(Allocate(static_cast<uint32_t>(height) * 4));
    if (rows == nullptr) return kWriteOk;
    if (info->bitsPerPixel == static_cast<int32_t>(kInfoBitsDirect)) {
        const uint32_t rowBytes = Field<uint32_t>(pngInfo, kPngRowBytes);
        uint8_t *const buffer = static_cast<uint8_t *>(Allocate(rowBytes * static_cast<uint32_t>(height)));
        if (buffer != nullptr) {
            for (int32_t row = 0; row < height; ++row) rows[row] = buffer + rowBytes * static_cast<uint32_t>(row);
            ReadImage(png, rows);
            uint8_t *destination = pixels;
            for (int32_t row = 0; row < info->height; ++row) {
                const uint8_t *source = static_cast<const uint8_t *>(rows[row]);
                const uint8_t colorType = Field<uint8_t>(pngInfo, kPngColorType);
                if (colorType == kColorRgb) {
                    for (int32_t x = 0; x < info->width; ++x) {
                        destination[4 * x + 2] = source[3 * x];
                        destination[4 * x + 1] = source[3 * x + 1];
                        destination[4 * x] = source[3 * x + 2];
                        destination[4 * x + 3] = 0xff;
                    }
                } else if (colorType == kColorRgba) {
                    for (int32_t x = 0; x < info->width; ++x) {
                        destination[4 * x + 2] = source[4 * x];
                        destination[4 * x + 1] = source[4 * x + 1];
                        destination[4 * x] = source[4 * x + 2];
                        destination[4 * x + 3] = source[4 * x + 3];
                    }
                }
                destination += pitch;
            }
            FreeMemory(buffer);
        }
    } else {
        uint8_t *destination = pixels;
        for (int32_t row = 0; row < height; ++row) {
            rows[row] = destination;
            destination += pitch;
        }
        ReadImage(png, rows);
    }
    FreeMemory(rows);
    return kWriteOk;
}

// 0x880536 (read): decodes the picture into the caller's pixels with a library state of its own.
int32_t __stdcall ReadFrame(Instance *instance, Info *info, void *pixels, int32_t pitch)
{
    void *volatile png = CreateReadStruct(reinterpret_cast<void *>(&AllocateCallback), reinterpret_cast<void *>(&FreeCallback));
    volatile int32_t result = 0;
    if (png == nullptr) return 0;
    void *volatile pngInfo = CreateInfoStruct(png);
    if (pngInfo == nullptr) return 0;
    SetReadFunction(png, instance->stream, reinterpret_cast<void *>(&ReadCallback));
    if (setjmp(JumpBuffer(png)) == 0) {
        StreamSeek(instance->stream, 0, 0);
        ReadInfo(png, pngInfo);
        if (Field<uint8_t>(pngInfo, kPngBitDepth) == 16) StripSixteen(png);
        if (Field<uint8_t>(pngInfo, kPngBitDepth) < kLibraryByteBits) SetPacking(png);
        if (Field<uint8_t>(pngInfo, kPngColorType) == kColorGrayAlpha) GrayToRgb(png);
        ReadUpdateInfo(png, pngInfo);
        result = ReadRows(info, pngInfo, png, static_cast<uint8_t *>(pixels), pitch);
    }
    void *pngCell = png;
    void *infoCell = pngInfo;
    DestroyReadStruct(&pngCell, &infoCell);
    return result;
}

// 0x880a68 (__cdecl): converts the caller's rows (32-bit pixels, or indices) to the library's and writes them.
int32_t __cdecl WriteRows(const Info *info, void *pngInfo, void *png, const uint8_t *pixels, int32_t pitch)
{
    if (setjmp(JumpBuffer(png)) != 0) return 0;
    const int32_t height = info->height;
    const int32_t width = info->width;
    uint8_t **const rows = static_cast<uint8_t **>(Allocate(static_cast<uint32_t>(height) * 4));
    if (rows == nullptr) return kWriteOk;
    const bool direct = info->bitsPerPixel == static_cast<int32_t>(kInfoBitsDirect);
    const uint8_t colorType = Field<uint8_t>(pngInfo, kPngColorType);
    const bool alpha = (colorType & kColorAlphaBit) != 0;
    int32_t rowBytes;
    if (direct) {
        rowBytes = alpha ? width * 4 : width * 3;
    } else {
        rowBytes = width;
        if (alpha) rowBytes += rowBytes;
    }
    uint8_t *const buffer = static_cast<uint8_t *>(Allocate(static_cast<uint32_t>(rowBytes * height)));
    if (buffer == nullptr) {
        FreeMemory(rows);
        return kWriteOk;
    }
    for (int32_t row = 0; row < height; ++row) rows[row] = buffer + row * rowBytes;
    // The gray rows: a sample is the index scaled to the channel's range, rounded.
    int32_t bits = info->sourceBits;
    if (bits < 1) bits = 1;
    if (static_cast<uint32_t>(bits) > 8) bits = kLibraryByteBits;
    int32_t maxValue = 1;
    int32_t rounding = 0;
    if (bits != 1) {
        const int32_t levels = 1 << bits;
        maxValue = levels - 1;
        rounding = levels >> 1;
    }
    const uint8_t *source = pixels;
    for (int32_t row = 0; row < height; ++row, source += pitch) {
        uint8_t *destination = rows[row];
        if (direct) {
            if (alpha) {
                for (int32_t x = 0; x < width; ++x) {
                    destination[4 * x] = source[4 * x + 2];
                    destination[4 * x + 1] = source[4 * x + 1];
                    destination[4 * x + 2] = source[4 * x];
                    destination[4 * x + 3] = source[4 * x + 3];
                }
            } else {
                for (int32_t x = 0; x < width; ++x) {
                    destination[3 * x] = source[4 * x + 2];
                    destination[3 * x + 1] = source[4 * x + 1];
                    destination[3 * x + 2] = source[4 * x];
                }
            }
        } else if (colorType != kColorGray && colorType != kColorGrayAlpha) {
            for (int32_t x = 0; x < width; ++x) destination[x] = source[x];
        } else {
            for (int32_t x = 0; x < width; ++x) {
                *destination++ = static_cast<uint8_t>((source[x] * 0xff + rounding) / maxValue);
                if (alpha) *destination++ = info->palette[source[x]][3];
            }
        }
    }
    WriteImage(png, rows);
    FreeMemory(buffer);
    FreeMemory(rows);
    return kWriteOk;
}

// 0x8807bb (write): encodes the caller's pixels, once per instance.
int32_t __stdcall WriteFrame(Instance *instance, const Info *info, const void *pixels, int32_t pitch)
{
    if (instance->unknownC != 0) return 0;
    volatile int32_t result = 0; // read back from memory after a jump
    void *volatile png = CreateWriteStruct(reinterpret_cast<void *>(&AllocateCallback), reinterpret_cast<void *>(&FreeCallback));
    if (png != nullptr) {
        void *volatile pngInfo = CreateInfoStruct(png);
        if (pngInfo != nullptr) {
            SetWriteFunction(png, instance->stream, reinterpret_cast<void *>(&WriteCallback));
            if (setjmp(JumpBuffer(png)) == 0) {
                uint8_t depth;
                uint8_t colorType;
                uint8_t *palette24 = nullptr;
                uint8_t *transparency = nullptr;
                if (info->bitsPerPixel == static_cast<int32_t>(kInfoBitsDirect)) {
                    depth = kLibraryByteBits;
                    colorType = info->alphaBits == 0 ? kColorRgb : kColorRgba;
                } else {
                    if (info->sourceBits == 1) {
                        depth = 1;
                    } else if (info->sourceBits == 2) {
                        depth = 2;
                    } else {
                        depth = info->sourceBits <= 4 ? 4 : kLibraryByteBits;
                    }
                    if (IsGrayPalette(&info->palette[0][0], static_cast<int32_t>(info->paletteCount)) != 0) {
                        if (info->alphaBits != 0) {
                            depth = kLibraryByteBits;
                            colorType = kColorGrayAlpha;
                        } else {
                            colorType = kColorGray;
                        }
                    } else {
                        colorType = kColorPalette;
                        palette24 = static_cast<uint8_t *>(Allocate(info->paletteCount * 3));
                        for (int32_t index = 0; index < static_cast<int32_t>(info->paletteCount); ++index) {
                            palette24[3 * index] = info->palette[index][2];
                            palette24[3 * index + 1] = info->palette[index][1];
                            palette24[3 * index + 2] = info->palette[index][0];
                        }
                    }
                }
                Field<int32_t>(pngInfo, kPngWidth) = info->width;
                Field<int32_t>(pngInfo, kPngHeight) = info->height;
                Field<uint8_t>(pngInfo, kPngBitDepth) = depth;
                Field<uint8_t>(pngInfo, kPngColorType) = colorType;
                if (colorType == kColorPalette) {
                    Field<uint32_t>(pngInfo, kPngValid) |= kPngValidPalette;
                    Field<uint8_t *>(pngInfo, kPngPalette) = palette24;
                    Field<uint16_t>(pngInfo, kPngPaletteCount) = static_cast<uint16_t>(info->paletteCount);
                    if (static_cast<int32_t>(info->alphaBits) > 0) {
                        const uint32_t count = Field<uint16_t>(pngInfo, kPngPaletteCount);
                        transparency = static_cast<uint8_t *>(Allocate(count * 4));
                        if (transparency != nullptr) {
                            for (uint32_t index = 0; index < count; ++index) transparency[index] = info->palette[index][3];
                            SetTransparency(png, pngInfo, transparency, static_cast<int32_t>(count));
                        }
                    }
                }
                if (info->dotsPerInch != 0.0f) {
                    SetPhysical(png, pngInfo, _mm_cvttss_si32(_mm_set_ss(info->dotsPerInch / InchPerMeter())));
                }
                WriteInfo(png, pngInfo);
                if (Field<uint8_t>(pngInfo, kPngBitDepth) < kLibraryByteBits) SetPacking(png);
                result = WriteRows(info, pngInfo, png, static_cast<const uint8_t *>(pixels), pitch);
                WriteEnd(png, pngInfo);
                if (palette24 != nullptr) FreeMemory(palette24);
                if (transparency != nullptr) FreeMemory(transparency);
            }
            void *pngCell = png;
            void *infoCell = pngInfo;
            DestroyWriteStruct(&pngCell, &infoCell);
        }
    }
    ++instance->unknownC;
    return result;
}

} // namespace png

} // namespace gimex
