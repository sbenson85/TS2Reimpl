// Gimex's JPEG codec through the libjpeg in the image (row 2 of the codec table, lane MO): about, is, open, info, read,
// write and close, the row converters, the Photoshop-marker handler and the two error exits. The library's own functions
// (jpeg_std_error, jpeg_read_scanlines, ...) stay the original's: this is the glue the game wrote around them.
//
// The library's structures are handled as raw bytes at the offsets the glue uses (the image's libjpeg is an old 6a-style
// build whose structures differ from a current one's).
#include "gz/GimexFormats.h"

#include "game/game.h"
#include "game/original.h"
#include "ifaces/cIGZFile.h"

#include <xmmintrin.h>

#include <csetjmp>
#include <cstring>

namespace gimex {

namespace jpeg {

namespace {

constexpr uint32_t kFourcc = 0x4a504547; // "GEPJ" in memory
constexpr uint32_t kInfoVersion = 0x159;
constexpr uint32_t kInstanceBytes = 0x18;
constexpr uint32_t kScoreMatch = 99;

// About.
constexpr uint32_t kAboutFlags = 0xb3d7;
constexpr uint32_t kAboutDefaultQuality = 0x50;

// The big-endian values a JPEG stream starts with (the SOI marker and an APP0 / APP1 / APP13 marker).
constexpr uint32_t kMarkerJfif = 0xffd8ffe0;
constexpr uint32_t kMarkerExif = 0xffd8ffe1;
constexpr uint32_t kMarkerPhotoshop = 0xffd8ffed;

// The marker the game writes into (and looks for in) its files: APP13 holding "GIMEXARGB", which says the four channels
// are stored reversed (alpha first).
constexpr int32_t kAlphaMarker = 0xed;
constexpr char kAlphaTag[10] = "GIMEXARGB";
constexpr uint32_t kAlphaTagBytes = 9;
constexpr uint32_t kAlphaCell = 0x10e6944; // set to 1 by the marker handler when it sees the tag

// The library's version and structure sizes the glue creates its objects with.
constexpr int32_t kLibraryVersion = 0x3d;
constexpr uint32_t kDecompressBytes = 0x1d0;
constexpr uint32_t kCompressBytes = 0x168;
constexpr uint32_t kErrorManagerBytes = 0x84;
constexpr int32_t kPoolImage = 1;

// Offsets in the library's structures (jpeg_common_struct, then the decompress and compress structures).
constexpr uint32_t kCommonErr = 0x00;
constexpr uint32_t kCommonMem = 0x04;
constexpr uint32_t kMemAllocSarray = 8;
constexpr uint32_t kErrorExit = 0;
constexpr uint32_t kErrorOutputMessage = 8;
constexpr uint32_t kDecompressSource = 0x14; // the source manager
constexpr uint32_t kDecompressColorSpace = 0x24;
constexpr uint32_t kDecompressOutColorSpace = 0x28;
constexpr uint32_t kDecompressOutWidth = 0x70;
constexpr uint32_t kDecompressOutHeight = 0x74;
constexpr uint32_t kDecompressOutComponents = 0x7c;
constexpr uint32_t kDecompressOutScanline = 0x8c;
constexpr uint32_t kDecompressDensityUnit = 0x11c; // a byte
constexpr uint32_t kDecompressDensityX = 0x11e;    // a word
constexpr uint32_t kCompressWidth = 0x18;
constexpr uint32_t kCompressHeight = 0x1c;
constexpr uint32_t kCompressComponents = 0x20;
constexpr uint32_t kCompressColorSpace = 0x24;
constexpr uint32_t kCompressDensityUnit = 0xcc; // a byte
constexpr uint32_t kCompressDensityX = 0xce;    // a word
constexpr uint32_t kCompressDensityY = 0xd0;    // a word
// The source manager: next byte, bytes left, then the fill and skip callbacks.
constexpr uint32_t kSourceNext = 0;
constexpr uint32_t kSourceLeft = 4;
constexpr uint32_t kSourceFill = 0xc;
constexpr uint32_t kSourceSkip = 0x10;

// The library's colour spaces.
constexpr uint32_t kJcsUnknown = 0;
constexpr uint32_t kJcsGrayscale = 1;
constexpr uint32_t kJcsRgb = 2;
constexpr uint32_t kJcsYcbcr = 3;
constexpr uint32_t kJcsCmyk = 4;
constexpr uint32_t kJcsYcck = 5;

constexpr int32_t kDefaultWriteQuality = 100;
constexpr uint32_t kInfoQuality = 0x46; // what info reports
constexpr uint32_t kInfoBits = 0x20;
constexpr int32_t kWriteOk = 1;

// The offsets of the Info fields the glue reads through the structure's palette (4 bytes an index, from 0x28).
constexpr uint32_t kPaletteEntries = 256;

using StdErrorFn = void *(__cdecl *)(void *err);
using CreateFn = void(__cdecl *)(void *cinfo, int32_t version, uint32_t size);
using SetMarkerProcessorFn = void(__cdecl *)(void *cinfo, int32_t marker, void *handler);
using StdioFn = void(__cdecl *)(void *cinfo, void *stream);
using ReadHeaderFn = int32_t(__cdecl *)(void *cinfo, int32_t requireImage);
using CinfoFn = int32_t(__cdecl *)(void *cinfo);
using ReadScanlinesFn = uint32_t(__cdecl *)(void *cinfo, uint8_t **rows, uint32_t count);
using AllocSarrayFn = uint8_t **(__cdecl *)(void *cinfo, int32_t pool, uint32_t samples, uint32_t rows);
using SetQualityFn = void(__cdecl *)(void *cinfo, int32_t quality, int32_t forceBaseline);
using StartCompressFn = void(__cdecl *)(void *cinfo, int32_t writeAllTables);
using WriteMarkerFn = void(__cdecl *)(void *cinfo, int32_t marker, const void *data, uint32_t length);
using WriteScanlinesFn = uint32_t(__cdecl *)(void *cinfo, uint8_t **rows, uint32_t count);
using FillFn = int32_t(__cdecl *)(void *cinfo);
using SkipFn = void(__cdecl *)(void *cinfo, int32_t bytes);
using OutputMessageFn = void(__cdecl *)(void *cinfo);

// The stream and memory helpers are Gimex's shared ones (src/gz/Gimex.h).

// The library (original calls).
__declspec(noinline) void *StdError(void *err)
{
    return OriginalFunction<StdErrorFn, 0x88d77d>()(err);
}
__declspec(noinline) void CreateDecompress(void *cinfo)
{
    OriginalFunction<CreateFn, 0x88d961>()(cinfo, kLibraryVersion, kDecompressBytes);
}
__declspec(noinline) void CreateCompress(void *cinfo)
{
    OriginalFunction<CreateFn, 0x88df34>()(cinfo, kLibraryVersion, kCompressBytes);
}
__declspec(noinline) void SetMarkerProcessor(void *cinfo, int32_t marker, void *handler)
{
    OriginalFunction<SetMarkerProcessorFn, 0x88dbd2>()(cinfo, marker, handler);
}
__declspec(noinline) void StdioSource(void *cinfo, void *stream)
{
    OriginalFunction<StdioFn, 0x88dca3>()(cinfo, stream);
}
__declspec(noinline) void StdioDestination(void *cinfo, void *stream)
{
    OriginalFunction<StdioFn, 0x88e1c9>()(cinfo, stream);
}
__declspec(noinline) void ReadHeader(void *cinfo)
{
    OriginalFunction<ReadHeaderFn, 0x88db6b>()(cinfo, 1);
}
__declspec(noinline) void StartDecompress(void *cinfo)
{
    OriginalFunction<CinfoFn, 0x88ddc0>()(cinfo);
}
__declspec(noinline) void ReadScanline(void *cinfo, uint8_t **rows)
{
    OriginalFunction<ReadScanlinesFn, 0x88dd36>()(cinfo, rows, 1);
}
__declspec(noinline) void FinishDecompress(void *cinfo)
{
    OriginalFunction<CinfoFn, 0x88dac6>()(cinfo);
}
__declspec(noinline) void Destroy(void *cinfo)
{
    OriginalFunction<void(__cdecl *)(void *), 0x88dac1>()(cinfo);
}
__declspec(noinline) void SetDefaults(void *cinfo)
{
    OriginalFunction<void(__cdecl *)(void *), 0x88e614>()(cinfo);
}
__declspec(noinline) void SetQuality(void *cinfo, int32_t quality)
{
    OriginalFunction<SetQualityFn, 0x88e71d>()(cinfo, quality, 1);
}
__declspec(noinline) void StartCompress(void *cinfo)
{
    OriginalFunction<StartCompressFn, 0x88e793>()(cinfo, 1);
}
__declspec(noinline) void WriteMarker(void *cinfo, int32_t marker, const void *data, uint32_t length)
{
    OriginalFunction<WriteMarkerFn, 0x88e117>()(cinfo, marker, data, length);
}
__declspec(noinline) void WriteScanline(void *cinfo, uint8_t **rows)
{
    OriginalFunction<WriteScanlinesFn, 0x88e7ff>()(cinfo, rows, 1);
}
__declspec(noinline) void FinishCompress(void *cinfo)
{
    OriginalFunction<CinfoFn, 0x88dfde>()(cinfo);
}

template <class T> T &Field(void *base, uint32_t offset)
{
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(base) + offset);
}

// The library's error manager and the jump buffer its error exit lands in, in one block (the exits find the buffer 0x84
// bytes after the manager).
struct ErrorFrame {
    uint8_t manager[kErrorManagerBytes];
    std::jmp_buf env;
};

[[noreturn]] void Jump(void *cinfo)
{
    uint8_t *const manager = Field<uint8_t *>(cinfo, kCommonErr);
    std::longjmp(*reinterpret_cast<std::jmp_buf *>(manager + kErrorManagerBytes), 1);
}

} // namespace

// 0x87f3b5 (__cdecl): a decompress error leaves the call quietly.
void __cdecl DecompressErrorExit(void *cinfo)
{
    Jump(cinfo);
}

// 0x87f3c9 (__cdecl): a compress error is reported through the manager's output_message first.
void __cdecl CompressErrorExit(void *cinfo)
{
    uint8_t *const manager = Field<uint8_t *>(cinfo, kCommonErr);
    Field<OutputMessageFn>(manager, kErrorOutputMessage)(cinfo);
    Jump(cinfo);
}

// 0x87f761 (__cdecl): 1 when every palette entry is gray (red = green = blue), or the count is not positive.
int32_t __cdecl IsGrayPalette(const uint8_t *palette, int32_t count)
{
    int32_t gray = 1;
    if (count > 0) {
        int32_t index = 0;
        do {
            const uint8_t *const entry = palette + index * 4;
            if (entry[2] != entry[1] || entry[2] != entry[0]) {
                gray = 0;
                break;
            }
            ++index;
        } while (index < count);
    }
    return gray;
}

// 0x87f333 (__cdecl): a YCbCr stream is read as RGB, a YCCK one as CMYK.
void __cdecl FixColorSpace(void *cinfo)
{
    if (Field<uint32_t>(cinfo, kDecompressColorSpace) == kJcsYcbcr) {
        Field<uint32_t>(cinfo, kDecompressOutColorSpace) = kJcsRgb;
    } else if (Field<uint32_t>(cinfo, kDecompressColorSpace) == kJcsYcck) {
        Field<uint32_t>(cinfo, kDecompressOutColorSpace) = kJcsCmyk;
    }
}

// 0x87f353 (__cdecl): a CMYK pixel (four bytes) to three: each of the first three channels plus the fourth, clamped,
// inverted.
void __cdecl CmykToRgb(const uint8_t *cmyk, uint8_t *out)
{
    const uint32_t key = cmyk[3];
    for (int i = 0; i < 3; ++i) {
        const uint32_t sum = cmyk[i] + key;
        const uint8_t clamped = sum >= 0xff ? 0xff : static_cast<uint8_t>(sum);
        out[i] = static_cast<uint8_t>(~clamped);
    }
}

// 0x87f255 (__cdecl): a decoded row to the caller's 32-bit pixels (or, for gray, bytes).
void __cdecl ConvertReadRow(const uint8_t *source, uint8_t *dest, int32_t count, int32_t components, uint32_t colorSpace,
                            int32_t alphaFirst)
{
    (void)components;
    if (alphaFirst != 0) {
        for (int32_t i = 0; i < count; ++i) {
            dest[4 * i] = source[4 * i + 3];
            dest[4 * i + 1] = source[4 * i + 2];
            dest[4 * i + 2] = source[4 * i + 1];
            dest[4 * i + 3] = source[4 * i];
        }
        return;
    }
    if (colorSpace == kJcsGrayscale) {
        std::memcpy(dest, source, static_cast<size_t>(count));
    } else if (colorSpace == kJcsCmyk) {
        for (int32_t i = 0; i < count; ++i) {
            uint8_t rgb[3];
            CmykToRgb(source + 4 * i, rgb);
            dest[4 * i + 2] = rgb[0];
            dest[4 * i + 1] = rgb[1];
            dest[4 * i] = rgb[2];
            dest[4 * i + 3] = 0xff;
        }
    } else {
        for (int32_t i = 0; i < count; ++i) {
            dest[4 * i + 3] = 0xff;
            dest[4 * i + 2] = source[3 * i];
            dest[4 * i + 1] = source[3 * i + 1];
            dest[4 * i] = source[3 * i + 2];
        }
    }
}

// 0x87f62d (__cdecl): the caller's pixels (32-bit, or indices into the info's palette) to a row of 1, 3 or 4 channels.
void __cdecl ConvertWriteRow(const uint8_t *source, uint8_t *dest, int32_t count, uint32_t colorSpace, const Info *info)
{
    const bool direct = info->bitsPerPixel == static_cast<int32_t>(kInfoBits);
    const uint8_t *const palette = &info->palette[0][0];
    if (colorSpace == kJcsUnknown) {
        for (int32_t i = 0; i < count; ++i) {
            if (direct) {
                dest[4 * i] = source[4 * i + 3];
                dest[4 * i + 1] = source[4 * i + 2];
                dest[4 * i + 2] = source[4 * i + 1];
                dest[4 * i + 3] = source[4 * i];
            } else {
                const uint8_t *const entry = palette + source[i] * 4;
                dest[4 * i] = entry[3];
                dest[4 * i + 1] = entry[2];
                dest[4 * i + 2] = entry[1];
                dest[4 * i + 3] = entry[0];
            }
        }
    } else if (colorSpace == kJcsGrayscale) {
        for (int32_t i = 0; i < count; ++i) {
            dest[i] = direct ? source[4 * i + 1] : palette[source[i] * 4 + 1];
        }
    } else if (colorSpace == kJcsRgb) {
        for (int32_t i = 0; i < count; ++i) {
            if (direct) {
                dest[3 * i] = source[4 * i + 2];
                dest[3 * i + 1] = source[4 * i + 1];
                dest[3 * i + 2] = source[4 * i];
            } else {
                const uint8_t *const entry = palette + source[i] * 4;
                dest[3 * i] = entry[2];
                dest[3 * i + 1] = entry[1];
                dest[3 * i + 2] = entry[0];
            }
        }
    }
}

// 0x87f177 (__cdecl): the handler for the APP13 marker. Reads the segment through the source manager's buffer, notes
// whether it starts with "GIMEXARGB", skips the rest. 0 when the source has to wait for more bytes.
int32_t __cdecl AlphaMarkerHandler(void *cinfo)
{
    uint8_t *const source = Field<uint8_t *>(cinfo, kDecompressSource);
    const FillFn fill = Field<FillFn>(source, kSourceFill);
    uint32_t left = Field<uint32_t>(source, kSourceLeft);
    if (left == 0) {
        if (fill(cinfo) == 0) return 0;
        left = Field<uint32_t>(source, kSourceLeft);
    }
    const uint8_t *cursor = Field<const uint8_t *>(source, kSourceNext);
    int32_t length = static_cast<int32_t>(*cursor++) << 8;
    --left;
    if (left == 0) {
        if (fill(cinfo) == 0) return 0;
        cursor = Field<const uint8_t *>(source, kSourceNext);
        left = Field<uint32_t>(source, kSourceLeft);
    }
    length = length - 2 + *cursor++;
    --left;
    // ORIGINAL-BUG: a segment shorter than the tag compares an uninitialised buffer with it; ours compares zeros.
    char tag[kAlphaTagBytes] = {};
    if (length >= static_cast<int32_t>(kAlphaTagBytes)) {
        uint32_t index = 0;
        do {
            if (left == 0) {
                if (fill(cinfo) == 0) return 0;
                cursor = Field<const uint8_t *>(source, kSourceNext);
                left = Field<uint32_t>(source, kSourceLeft);
            }
            tag[index++] = static_cast<char>(*cursor++);
            --left;
        } while (index < kAlphaTagBytes);
        length -= static_cast<int32_t>(kAlphaTagBytes);
    }
    if (std::strncmp(tag, kAlphaTag, kAlphaTagBytes) == 0) *GameAddress<uint32_t>(kAlphaCell) = 1;
    Field<const uint8_t *>(source, kSourceNext) = cursor;
    Field<uint32_t>(source, kSourceLeft) = left;
    if (length > 0) Field<SkipFn>(source, kSourceSkip)(cinfo, length);
    return 1;
}

// 0x87ec26: the format's about block.
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
    std::strcpy(about->extensions[0], ".jpg");
    std::strcpy(about->extensions[1], ".jpeg");
    std::strcpy(about->extensions[2], ".jfif");
    std::strcpy(about->author, "FrANK G. Barchard");
    std::strcpy(about->revision, "1.12");
    std::strcpy(about->shortName, "JPG");
    std::strcpy(about->typeName, "JPeg");
    std::strcpy(about->description, "JPeg");
    return about;
}

// 0x87ef8c (is)
int32_t __stdcall Probe(ifaces::cIGZFile *stream)
{
    uint32_t score = 0;
    StreamSeek(stream, 0, 0);
    uint8_t marker[4];
    if (StreamRead(stream, marker, 4) != 0) {
        const uint32_t value = ReadBigEndian(marker, 4);
        if (value == kMarkerJfif || value == kMarkerExif || value == kMarkerPhotoshop) score = kScoreMatch;
    }
    return static_cast<int32_t>(score);
}

// 0x87efdb
int32_t __stdcall Open(Instance **out, ifaces::cIGZFile *stream, const char *, int32_t)
{
    Instance *instance = static_cast<Instance *>(Allocate(kInstanceBytes));
    if (instance == nullptr) return 0;
    instance->unknownC = 0;
    instance->unknown14 = 0;
    instance->stream = stream;
    instance->frameCount = 1;
    instance->fourcc = kFourcc;
    instance->size = kInstanceBytes;
    *out = instance;
    return 1;
}

// 0x87ed09 (close and wclose of row 2, wclose of row 4): frees the instance block; 0 for none.
int32_t __stdcall Close(Instance *instance)
{
    if (instance == nullptr) return 0;
    return FreeMemory(instance);
}

// 0x87ed18: decodes the whole stream (the game reads every row) and describes it.
Info *__stdcall GetInfo(Instance *instance, int32_t)
{
    ErrorFrame frame;
    alignas(8) uint8_t cinfo[kDecompressBytes];
    Field<void *>(cinfo, kCommonErr) = StdError(frame.manager);
    Field<void *>(frame.manager, kErrorExit) = reinterpret_cast<void *>(&DecompressErrorExit);
    // What a failed call answers is read back from memory: a register the compiler holds it in would not survive the jump.
    Info *volatile answer = nullptr;
    if (setjmp(frame.env) != 0) {
        Destroy(cinfo);
        return answer;
    }
    CreateDecompress(cinfo);
    SetMarkerProcessor(cinfo, kAlphaMarker, reinterpret_cast<void *>(&AlphaMarkerHandler));
    StreamSeek(instance->stream, 0, 0);
    StdioSource(cinfo, instance->stream);
    *GameAddress<uint32_t>(kAlphaCell) = 0;
    ReadHeader(cinfo);
    const uint32_t alphaFirst = *GameAddress<uint32_t>(kAlphaCell);
    StartDecompress(cinfo);
    const uint32_t components = Field<uint32_t>(cinfo, kDecompressOutComponents);
    const int32_t width = Field<int32_t>(cinfo, kDecompressOutWidth);
    const int32_t height = Field<int32_t>(cinfo, kDecompressOutHeight);
    uint32_t dotsPerInch = 0;
    if (Field<uint8_t>(cinfo, kDecompressDensityUnit) != 0) dotsPerInch = Field<uint16_t>(cinfo, kDecompressDensityX);
    uint8_t **const rows = Field<AllocSarrayFn>(Field<uint8_t *>(cinfo, kCommonMem), kMemAllocSarray)(
        cinfo, kPoolImage, components * static_cast<uint32_t>(width), 1);
    while (Field<uint32_t>(cinfo, kDecompressOutScanline) < Field<uint32_t>(cinfo, kDecompressOutHeight)) {
        ReadScanline(cinfo, rows);
    }
    FinishDecompress(cinfo);
    Destroy(cinfo);
    const int32_t bits = components == 1 ? 8 : 0x18;
    Info *const info = static_cast<Info *>(Allocate(kWriteInfoSize));
    if (info == nullptr) return nullptr;
    std::memset(info, 0, kWriteInfoSize);
    info->signature = kFourcc;
    info->size = kWriteInfoSize;
    info->version = kInfoVersion;
    info->width = width;
    info->height = height;
    info->variant = alphaFirst != 0 ? 1 : 0;
    info->jpegFlag = 1;
    info->quality = kInfoQuality;
    info->alphaBits = 0;
    info->redBits = 8;
    info->greenBits = 8;
    info->blueBits = 8;
    info->dotsPerInch = static_cast<float>(static_cast<int32_t>(dotsPerInch));
    if (bits >= 0xf) {
        info->bitsPerPixel = static_cast<int32_t>(kInfoBits);
        info->sourceBits = bits;
        info->paletteCount = 0;
    } else {
        info->bitsPerPixel = 8;
        info->sourceBits = 8;
        info->paletteCount = kPaletteEntries;
        for (uint32_t i = 0; i < kPaletteEntries; ++i) {
            info->palette[i][0] = static_cast<uint8_t>(i);
            info->palette[i][1] = static_cast<uint8_t>(i);
            info->palette[i][2] = static_cast<uint8_t>(i);
            info->palette[i][3] = 0xff;
        }
    }
    return info;
}

// 0x87f013: decodes the stream into the caller's pixels, one converted row at a time.
int32_t __stdcall ReadFrame(Instance *instance, Info *info, void *pixels, int32_t pitch)
{
    ErrorFrame frame;
    alignas(8) uint8_t cinfo[kDecompressBytes];
    Field<void *>(cinfo, kCommonErr) = StdError(frame.manager);
    Field<void *>(frame.manager, kErrorExit) = reinterpret_cast<void *>(&DecompressErrorExit);
    volatile int32_t failed = 0; // what a failed call answers, read back from memory (a register would not survive the jump)
    if (setjmp(frame.env) != 0) {
        Destroy(cinfo);
        return failed;
    }
    CreateDecompress(cinfo);
    SetMarkerProcessor(cinfo, kAlphaMarker, reinterpret_cast<void *>(&AlphaMarkerHandler));
    StreamSeek(instance->stream, 0, 0);
    StdioSource(cinfo, instance->stream);
    *GameAddress<uint32_t>(kAlphaCell) = 0;
    ReadHeader(cinfo);
    const uint32_t alphaFirst = *GameAddress<uint32_t>(kAlphaCell);
    FixColorSpace(cinfo);
    StartDecompress(cinfo);
    const int32_t components = Field<int32_t>(cinfo, kDecompressOutComponents);
    const int32_t width = Field<int32_t>(cinfo, kDecompressOutWidth);
    uint8_t **const rows = Field<AllocSarrayFn>(Field<uint8_t *>(cinfo, kCommonMem), kMemAllocSarray)(
        cinfo, kPoolImage, static_cast<uint32_t>(components * width), 1);
    uint8_t *destination = static_cast<uint8_t *>(pixels);
    const int32_t height = Field<int32_t>(cinfo, kDecompressOutHeight);
    for (int32_t row = 0; row < height; ++row) {
        ReadScanline(cinfo, rows);
        if (row < info->height) {
            ConvertReadRow(rows[0], destination, width, components, Field<uint32_t>(cinfo, kDecompressOutColorSpace),
                           static_cast<int32_t>(alphaFirst));
            destination += pitch;
        }
    }
    FinishDecompress(cinfo);
    Destroy(cinfo);
    return kWriteOk;
}

// 0x87f413: encodes the caller's pixels and writes the stream through the library.
int32_t __stdcall WriteFrame(Instance *instance, const Info *info, const void *pixels, int32_t pitch)
{
    const int32_t quality = info->jpegFlag != 0 ? static_cast<int32_t>(info->quality) : kDefaultWriteQuality;
    const int32_t width = info->width;
    const int32_t height = info->height;
    uint32_t components = 3;
    uint32_t colorSpace = kJcsRgb;
    if ((info->variant & 1) != 0) {
        components = 4;
        colorSpace = kJcsUnknown;
    } else if (info->bitsPerPixel == 8 && IsGrayPalette(&info->palette[0][0], static_cast<int32_t>(info->paletteCount)) != 0) {
        components = 1;
        colorSpace = kJcsGrayscale;
    }
    ErrorFrame frame;
    alignas(8) uint8_t cinfo[kCompressBytes];
    uint8_t *volatile buffer = nullptr;
    volatile int32_t result = 0; // set once the whole stream is written; read back from memory after a jump
    Field<void *>(cinfo, kCommonErr) = StdError(frame.manager);
    Field<void *>(frame.manager, kErrorExit) = reinterpret_cast<void *>(&CompressErrorExit);
    if (setjmp(frame.env) != 0) {
        if (buffer != nullptr) FreeMemory(buffer);
        Destroy(cinfo);
        return result;
    }
    CreateCompress(cinfo);
    StdioDestination(cinfo, instance->stream);
    Field<int32_t>(cinfo, kCompressWidth) = width;
    Field<int32_t>(cinfo, kCompressHeight) = height;
    Field<uint32_t>(cinfo, kCompressColorSpace) = colorSpace;
    Field<uint32_t>(cinfo, kCompressComponents) = components;
    SetDefaults(cinfo);
    SetQuality(cinfo, quality);
    const float dotsPerInch = info->dotsPerInch;
    uint16_t density;
    if (dotsPerInch == 0.0f) {
        Field<uint8_t>(cinfo, kCompressDensityUnit) = 0;
        density = 1;
    } else {
        Field<uint8_t>(cinfo, kCompressDensityUnit) = 1;
        density = static_cast<uint16_t>(_mm_cvttss_si32(_mm_set_ss(dotsPerInch)));
    }
    Field<uint16_t>(cinfo, kCompressDensityX) = density;
    Field<uint16_t>(cinfo, kCompressDensityY) = density;
    StartCompress(cinfo);
    if ((info->variant & 1) != 0) WriteMarker(cinfo, kAlphaMarker, kAlphaTag, kAlphaTagBytes);
    buffer = static_cast<uint8_t *>(Allocate(static_cast<uint32_t>(static_cast<int32_t>(components) * width)));
    if (buffer == nullptr) {
        Destroy(cinfo);
        return 0;
    }
    const uint8_t *source = static_cast<const uint8_t *>(pixels);
    uint8_t *rowPointer = buffer;
    for (int32_t row = 0; row < Field<int32_t>(cinfo, kCompressHeight); ++row) {
        ConvertWriteRow(source, buffer, width, colorSpace, info);
        rowPointer = buffer;
        WriteScanline(cinfo, &rowPointer);
        source += pitch;
    }
    FinishCompress(cinfo);
    result = kWriteOk;
    FreeMemory(buffer);
    Destroy(cinfo);
    return result;
}

} // namespace jpeg

} // namespace gimex
