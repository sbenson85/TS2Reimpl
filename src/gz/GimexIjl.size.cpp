// Gimex's JPEG codec through Intel's IJL (row 3 of the codec table, lane MN): the DLL load (ijl15.dll, else ijl11.dll,
// else ijl10.dll), about, is, open, info, read, write and close. Read and write use the entry points the load leaves
// in the game's cells below.
#include "gz/GimexFormats.h"

#include "game/game.h"
#include "game/original.h"
#include "ifaces/cIGZFile.h"

#include <windows.h>

#include <cstring>

namespace gimex {

namespace ijl {

namespace {

// The load's state, in the game's cells (the original read and write use them).
constexpr uint32_t kModuleCell = 0x10e6948;
constexpr uint32_t kInitCell = 0x10e694c;  // ijlInit
constexpr uint32_t kReadCell = 0x10e6950;  // ijlRead
constexpr uint32_t kWriteCell = 0x10e6954; // ijlWrite
constexpr uint32_t kFreeCell = 0x10e6958;  // ijlFree
constexpr uint32_t kStateCell = 0x10e695c; // About's: 0 untried, 1 loaded, -1 failed

constexpr uint32_t kIatFreeLibrary = 0xd9a120;
constexpr uint32_t kIatGetProcAddress = 0xd9a124;
constexpr uint32_t kIatLoadLibraryA = 0xd9a128;
using FreeLibraryFn = BOOL(WINAPI *)(HMODULE);
using GetProcAddressFn = FARPROC(WINAPI *)(HMODULE, const char *);
using LoadLibraryFn = HMODULE(WINAPI *)(const char *);

using InitFn = int32_t(__stdcall *)(void *properties);
using ReadFn = int32_t(__stdcall *)(void *properties, int32_t mode);
using WritePropertiesFn = int32_t(__stdcall *)(void *properties, int32_t mode);
using FreeFn = int32_t(__stdcall *)(void *properties);

constexpr uint32_t kFourcc = 0x494a5047; // "GPJI" in memory
constexpr uint32_t kInfoVersion = 0x159;
constexpr uint32_t kInstanceBytes = 0x18;
constexpr uint32_t kScoreMatch = 100;

// About: the descriptive words.
constexpr uint32_t kAboutFlags = 0xb317;     // the bits About sets; the load failing clears 0x317 and keeps the rest
constexpr uint32_t kAboutFlagsNoDll = 0xb000;
constexpr uint32_t kAboutDefaultQuality = 0x50;
constexpr uint32_t kAboutTag = 0x2e6a7067; // ".jpg" in memory (the original's immediate)

// The big-endian values a JPEG stream starts with (the SOI marker and an APP0 / APP1 / APP13 marker).
constexpr uint32_t kMarkerJfif = 0xffd8ffe0;
constexpr uint32_t kMarkerExif = 0xffd8ffe1;
constexpr uint32_t kMarkerPhotoshop = 0xffd8ffed;

// IJL's JPEG_CORE_PROPERTIES, the fields info reads (offsets in the structure the original zeroes: 0x4e68 bytes).
constexpr uint32_t kPropertiesBytes = 0x4e68;
constexpr uint32_t kPropJpgBytes = 0x24;     // JPGBytes: the file's bytes
constexpr uint32_t kPropJpgSizeBytes = 0x28; // JPGSizeBytes
constexpr uint32_t kPropJpgWidth = 0x2c;
constexpr uint32_t kPropJpgHeight = 0x30;
constexpr uint32_t kPropJpgChannels = 0x34;
constexpr uint32_t kPropQuality = 0x50;
constexpr uint32_t kPropExifPresent = 0x4e50;     // a word of the trailing JPEG properties
constexpr uint32_t kPropExifFlag = 0x4e56;        // a byte
constexpr uint32_t kPropExifValue = 0x4e58;       // a word
constexpr uint32_t kPropCommentFlag = 0x4e60;     // a word: the comment is present
constexpr uint32_t kPropComment = 0x4e5c;         // the comment string (a pointer; the load is after a push, so it reads 4 below the flag)
constexpr uint32_t kReadParameters = 1;           // ijlRead: IJL_JBUFF_READPARAMS
constexpr uint32_t kReadWholeImage = 3;           // ijlRead: IJL_JBUFF_READWHOLEIMAGE
constexpr uint32_t kWriteWholeImage = 9;          // ijlWrite: IJL_JBUFF_WRITEWHOLEIMAGE

// More of the properties structure, the fields read and write fill (the "DIB" fields describe the pixels, the "JPG"
// ones the compressed stream).
constexpr uint32_t kPropDibBytes = 0x04;
constexpr uint32_t kPropDibWidth = 0x08;
constexpr uint32_t kPropDibHeight = 0x0c;
constexpr uint32_t kPropDibPadBytes = 0x10;
constexpr uint32_t kPropDibChannels = 0x14;
constexpr uint32_t kPropDibColor = 0x18;
constexpr uint32_t kPropJpgColor = 0x38;
constexpr uint32_t kPropJpgSubsampling = 0x3c;
constexpr uint32_t kPropColorConversion = 0x48;

// IJL's color codes.
constexpr uint32_t kColorRgb = 2;
constexpr uint32_t kColorYcbcr = 3;
constexpr uint32_t kColorGray = 4;
constexpr uint32_t kColorRgbaFpx = 5;
constexpr uint32_t kColorYcbcrAFpx = 6;
constexpr uint32_t kColorOther = 0xff;
constexpr uint32_t kSubsampling411 = 1;
constexpr uint32_t kSubsampling422 = 2;
constexpr int32_t kQualityForSharperChroma = 0x4b; // from this quality up the stream is 4:2:2, below it 4:1:1
constexpr int32_t kDefaultWriteQuality = 100;
constexpr int32_t kWriteOk = 100;
constexpr uint32_t kAlphaOpaque = 0xff000000;

// The write's output buffer: the pixels' size times 1.1 plus 1024, and a 12-byte label ("jpeg buffer") at its start.
constexpr float kOutputRatio = 1.1f;
constexpr float kOutputSlack = 1024.0f;
constexpr char kBufferLabel[12] = {'j', 'p', 'e', 'g', ' ', 'b', 'u', 'f', 'f', 'e', 'r', '\0'};

// The info block's offsets that info fills beyond Info's named fields.
constexpr uint32_t kInfoStringOffset = 0x65c; // the copied string

constexpr const char *kLibraryNames[] = {"ijl15.dll", "ijl11.dll", "ijl10.dll"};
constexpr const char kEmptyString[] = "";

// The stream and memory helpers are Gimex's shared ones (src/gz/Gimex.h).

// Row 2's write (libjpeg), what this write falls back to when the DLL is not there.
int32_t LibJpegWrite(Instance *instance, const Info *info, const void *pixels, int32_t pitch)
{
    return jpeg::WriteFrame(instance, info, pixels, pitch);
}

uint32_t &Cell(uint32_t rva)
{
    return *GameAddress<uint32_t>(rva);
}

template <class T> T &Field(void *base, uint32_t offset)
{
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(base) + offset);
}

} // namespace

// 0x87f790: unloads the DLL.
int32_t __cdecl Unload()
{
    int32_t result = 0;
    if (Cell(kModuleCell) != 0) {
        result = (*GameAddress<FreeLibraryFn>(kIatFreeLibrary))(reinterpret_cast<HMODULE>(Cell(kModuleCell)));
        Cell(kModuleCell) = 0;
    }
    return result;
}

// 0x87f7a8: loads the DLL and the entry points unless it is loaded; 1 when it is (or already was), else 0.
int32_t __cdecl Load()
{
    if (Cell(kModuleCell) != 0) return 1;
    HMODULE module = nullptr;
    for (const char *name : kLibraryNames) {
        module = (*GameAddress<LoadLibraryFn>(kIatLoadLibraryA))(name);
        Cell(kModuleCell) = reinterpret_cast<uint32_t>(module);
        if (module != nullptr) break;
    }
    if (module == nullptr) return 0;
    const GetProcAddressFn lookup = *GameAddress<GetProcAddressFn>(kIatGetProcAddress);
    Cell(kInitCell) = reinterpret_cast<uint32_t>(lookup(module, "ijlInit"));
    Cell(kReadCell) = reinterpret_cast<uint32_t>(lookup(module, "ijlRead"));
    Cell(kWriteCell) = reinterpret_cast<uint32_t>(lookup(module, "ijlWrite"));
    Cell(kFreeCell) = reinterpret_cast<uint32_t>(lookup(module, "ijlFree"));
    // The write entry is optional.
    if (Cell(kInitCell) != 0 && Cell(kReadCell) != 0 && Cell(kFreeCell) != 0) return 1;
    Unload();
    return 0;
}

About *__stdcall NewAbout()
{
    About *about = static_cast<About *>(Allocate(sizeof(About)));
    if (about == nullptr) return nullptr;
    std::memset(about, 0, sizeof(About));
    about->fourcc = kFourcc;
    about->size = sizeof(About);
    about->version = kInfoVersion;
    about->flags = kAboutFlags;
    about->defaultQuality = kAboutDefaultQuality;
    about->unknown1c = kAboutTag;
    std::strcpy(about->extensions[0], ".jpg");
    std::strcpy(about->extensions[1], ".ijpg");
    std::strcpy(about->extensions[2], ".ijl");
    std::strcpy(about->author, "FrANK G. Barchard");
    std::strcpy(about->revision, "1.19");
    std::strcpy(about->shortName, "ijl");
    std::strcpy(about->typeName, "iJPeg");
    std::strcpy(about->description, "Intel JPeg");
    int32_t &state = reinterpret_cast<int32_t &>(Cell(kStateCell));
    if (state == 0) {
        state = 1;
        if (Load() == 0) state = -1;
    }
    if (state < 0) about->flags = kAboutFlagsNoDll;
    return about;
}

int32_t __stdcall Probe(ifaces::cIGZFile *stream)
{
    uint32_t score = 0;
    StreamSeek(stream, 0, 0);
    uint8_t marker[4];
    if (StreamRead(stream, marker, 4) != 0) {
        const uint32_t value = ReadBigEndian(marker, 4);
        if (value == kMarkerJfif || value == kMarkerExif || value == kMarkerPhotoshop) {
            if (Load() != 0) score = kScoreMatch;
        }
    }
    return static_cast<int32_t>(score);
}

int32_t __stdcall Open(Instance **out, ifaces::cIGZFile *stream, const char *, int32_t)
{
    Instance *instance = static_cast<Instance *>(Allocate(kInstanceBytes));
    if (instance == nullptr) return 0;
    const uint32_t length = StreamLength(stream);
    StreamSeek(stream, 0, 0);
    instance->unknownC = 0;
    instance->unknown14 = 0;
    instance->frameCount = 1;
    instance->fourcc = kFourcc;
    instance->size = kInstanceBytes;
    instance->stream = stream;
    // ORIGINAL-BUG: a failed allocation or read leaves the instance allocated (nothing frees it); ours does the same.
    void *const bytes = Allocate(length);
    instance->unknown14 = reinterpret_cast<uint32_t>(bytes);
    if (bytes == nullptr) return 0;
    if (StreamRead(stream, bytes, static_cast<int32_t>(length)) == 0) {
        FreeMemory(bytes);
        return 0;
    }
    *out = instance;
    if (Load() != 0) return 1;
    Unload();
    return 0;
}

Info *__stdcall GetInfo(Instance *instance, int32_t)
{
    // IJL's properties structure, on the stack.
    alignas(8) uint8_t properties[kPropertiesBytes];
    std::memset(properties, 0, sizeof(properties));
    int32_t width = 0;
    int32_t height = 0;
    int32_t channels = 0;
    int32_t quality = 0;
    int32_t exifValue = 0;
    const char *comment = kEmptyString;
    bool read = false;
    Info *info = nullptr;
    if ((*GameAddress<InitFn>(kInitCell))(properties) != 0) return nullptr;
    Field<uint32_t>(properties, kPropJpgBytes) = instance->unknown14;
    Field<uint32_t>(properties, kPropJpgSizeBytes) = StreamLength(instance->stream);
    if ((*GameAddress<ReadFn>(kReadCell))(properties, kReadParameters) == 0) {
        width = Field<int32_t>(properties, kPropJpgWidth);
        height = Field<int32_t>(properties, kPropJpgHeight);
        quality = Field<int32_t>(properties, kPropQuality);
        channels = Field<int32_t>(properties, kPropJpgChannels);
        if (Field<uint32_t>(properties, kPropExifPresent) != 0 && Field<uint8_t>(properties, kPropExifFlag) != 0)
            exifValue = Field<uint16_t>(properties, kPropExifValue);
        if (Field<uint16_t>(properties, kPropCommentFlag) != 0)
            comment = reinterpret_cast<const char *>(Field<uintptr_t>(properties, kPropComment));
        read = true;
    }
    if ((*GameAddress<FreeFn>(kFreeCell))(properties) != 0) return nullptr;
    if (!read) return nullptr;
    info = static_cast<Info *>(Allocate(kWriteInfoSize));
    if (info == nullptr) return nullptr;
    std::memset(info, 0, kWriteInfoSize);
    info->signature = kFourcc;
    info->size = kWriteInfoSize;
    info->version = kInfoVersion;
    info->width = width;
    info->height = height;
    info->bitsPerPixel = 0x20;
    info->sourceBits = 0x18;
    info->variant = static_cast<uint32_t>(channels);
    info->jpegFlag = 1;
    info->quality = static_cast<uint32_t>(quality);
    info->redBits = 8;
    info->greenBits = 8;
    info->blueBits = 8;
    info->dotsPerInch = static_cast<float>(exifValue);
    char *const text = reinterpret_cast<char *>(info) + kInfoStringOffset;
    std::strcpy(text, comment);
    return info;
}

int32_t __stdcall ReadFrame(Instance *instance, Info *info, void *pixels, int32_t pitch)
{
    alignas(8) uint8_t properties[kPropertiesBytes];
    std::memset(properties, 0, sizeof(properties));
    if ((*GameAddress<InitFn>(kInitCell))(properties) != 0) return 0;
    Field<uint32_t>(properties, kPropJpgBytes) = instance->unknown14;
    Field<uint32_t>(properties, kPropJpgSizeBytes) = StreamLength(instance->stream);
    int32_t result = 0;
    if ((*GameAddress<ReadFn>(kReadCell))(properties, kReadParameters) == 0) {
        // The pixels are read into the caller's buffer as 3 channels (the stream's gray, YCbCr), 4 or the stream's own.
        uint32_t channels = Field<uint32_t>(properties, kPropJpgChannels);
        uint32_t jpgColor = kColorOther;
        uint32_t dibColor = kColorOther;
        if (channels == 1) {
            jpgColor = kColorGray;
            dibColor = kColorRgb;
            channels = 3;
        } else if (channels == 3) {
            jpgColor = kColorYcbcr;
            dibColor = kColorRgb;
        } else if (channels == 4) {
            jpgColor = kColorYcbcrAFpx;
            dibColor = kColorRgbaFpx;
        }
        Field<uint32_t>(properties, kPropDibBytes) = reinterpret_cast<uint32_t>(pixels);
        Field<int32_t>(properties, kPropDibWidth) = info->width;
        Field<int32_t>(properties, kPropDibHeight) = info->height;
        Field<uint32_t>(properties, kPropDibChannels) = channels;
        Field<uint32_t>(properties, kPropDibColor) = dibColor;
        Field<uint32_t>(properties, kPropJpgColor) = jpgColor;
        Field<uint32_t>(properties, kPropDibPadBytes) = static_cast<uint32_t>(pitch) - static_cast<uint32_t>(info->width) * channels;
        (*GameAddress<ReadFn>(kReadCell))(properties, kReadWholeImage);
        uint8_t *row = static_cast<uint8_t *>(pixels);
        if (Field<uint32_t>(properties, kPropDibChannels) == 4) {
            // RGBA from the library: premultiplied, the red and blue bytes swapped, opaque alpha.
            for (int32_t y = 0; y < info->height; ++y) {
                uint8_t *pixel = row;
                for (int32_t x = 0; x < info->width; ++x) {
                    const uint32_t alpha = pixel[3];
                    const uint8_t red = pixel[2];
                    pixel[3] = 0xff;
                    pixel[2] = static_cast<uint8_t>((pixel[0] * alpha + 0x80) >> 8);
                    pixel[1] = static_cast<uint8_t>((pixel[1] * alpha + 0x80) >> 8);
                    pixel[0] = static_cast<uint8_t>((red * alpha + 0x80) >> 8);
                    pixel += 4;
                }
                row += pitch;
            }
        } else {
            // 3 bytes per pixel to 4, from the row's end so the expansion is in place.
            for (int32_t y = 0; y < info->height; ++y) {
                const uint8_t *source = row + info->width * 3 - 3;
                uint8_t *target = row + (info->width - 1) * 4;
                for (int32_t x = 0; x < info->width; ++x) {
                    uint32_t value;
                    std::memcpy(&value, source, sizeof(value));
                    value |= kAlphaOpaque;
                    std::memcpy(target, &value, sizeof(value));
                    source -= 3;
                    target -= 4;
                }
                row += pitch;
            }
        }
        result = kScoreMatch;
    }
    if ((*GameAddress<FreeFn>(kFreeCell))(properties) != 0) result = 0;
    return result;
}

int32_t __stdcall WriteFrame(Instance *instance, const Info *info, const void *pixels, int32_t pitch)
{
    const int32_t quality = info->jpegFlag != 0 ? static_cast<int32_t>(info->quality) : kDefaultWriteQuality;
    if (info->bitsPerPixel <= 8) return 0;
    if (Load() == 0) return LibJpegWrite(instance, info, pixels, pitch);
    int32_t result = 0;
    // The pixels as 3 bytes each (the fourth dropped), for the library.
    const int32_t pixelBytes = static_cast<int32_t>(static_cast<uint32_t>(info->width) * static_cast<uint32_t>(info->height) * 3);
    uint8_t *const rgb = static_cast<uint8_t *>(Allocate(static_cast<uint32_t>(pixelBytes)));
    if (rgb != nullptr) {
        uint8_t *cursor = rgb;
        const uint8_t *row = static_cast<const uint8_t *>(pixels);
        for (int32_t y = 0; y < info->height; ++y) {
            const uint8_t *source = row;
            for (int32_t x = 0; x < info->width; ++x) {
                cursor[0] = source[0];
                cursor[1] = source[1];
                cursor[2] = source[2];
                source += 4;
                cursor += 3;
            }
            row += pitch;
        }
        const int32_t outputBytes = static_cast<int32_t>(static_cast<float>(pixelBytes) * kOutputRatio + kOutputSlack);
        uint8_t *const output = static_cast<uint8_t *>(Allocate(static_cast<uint32_t>(outputBytes)));
        if (output != nullptr) {
            std::memcpy(output, kBufferLabel, sizeof(kBufferLabel));
            alignas(8) uint8_t properties[kPropertiesBytes];
            std::memset(properties, 0, sizeof(properties));
            if ((*GameAddress<InitFn>(kInitCell))(properties) == 0) {
                Field<uint32_t>(properties, kPropDibBytes) = reinterpret_cast<uint32_t>(rgb);
                Field<int32_t>(properties, kPropDibWidth) = info->width;
                Field<int32_t>(properties, kPropDibHeight) = info->height;
                Field<uint32_t>(properties, kPropDibPadBytes) = 0;
                Field<uint32_t>(properties, kPropDibChannels) = 3;
                Field<uint32_t>(properties, kPropDibColor) = kColorRgb;
                Field<uint32_t>(properties, kPropJpgBytes) = reinterpret_cast<uint32_t>(output);
                Field<int32_t>(properties, kPropJpgSizeBytes) = outputBytes;
                Field<int32_t>(properties, kPropJpgWidth) = info->width;
                Field<int32_t>(properties, kPropJpgHeight) = info->height;
                Field<uint32_t>(properties, kPropJpgChannels) = 3;
                Field<uint32_t>(properties, kPropJpgColor) = kColorYcbcr;
                Field<uint32_t>(properties, kPropJpgSubsampling) =
                    static_cast<int32_t>(info->quality) >= kQualityForSharperChroma ? kSubsampling422 : kSubsampling411;
                Field<uint32_t>(properties, kPropColorConversion) = 1;
                Field<int32_t>(properties, kPropQuality) = quality;
                if ((*GameAddress<WritePropertiesFn>(kWriteCell))(properties, kWriteWholeImage) == 0) {
                    const int32_t size = Field<int32_t>(properties, kPropJpgSizeBytes);
                    if (StreamWrite(instance->stream, output, size) != 0) result = kWriteOk;
                }
                if ((*GameAddress<FreeFn>(kFreeCell))(properties) != 0) result = 0;
            }
        }
        FreeMemory(output);
    }
    FreeMemory(rgb);
    return result;
}

int32_t __stdcall Close(Instance *instance)
{
    uint32_t result = 0;
    if (instance != nullptr) {
        if (instance->unknown14 != 0) FreeMemory(reinterpret_cast<void *>(instance->unknown14));
        result = static_cast<uint8_t>(FreeMemory(instance));
    }
    Unload();
    return static_cast<int32_t>(result);
}

} // namespace ijl

} // namespace gimex
