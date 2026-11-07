// cGZPersistBufferResource: the persist resource of an image (module 0x9b5fd0-0x9b8e00, /O2). Lane NU. The class's header
// holds the layout and the faces; this file the slots, the three readers (BMP record 0x9b6930, TGA record 0x9b77b0, BMP file
// 0x9b6c30) and what they share: the bitmap converter 0x9b8140, its support check 0x9b8db0 and the two dithers 0x9b88e0 /
// 0x9b8b40. The TGA reader's pixel converter (0x9cd6a0 / 0x9cd830 -> 0x9cea90 / 0x9cd890) is another class's code: original
// calls.
#include "gz/cGZPersistBufferResource.h"

#include "game/game.h"
#include "game/original.h"
#include "gz/cIGZCOM.h"
#include "ifaces/cIGZPersistDBSerialRecord.h"
#include "rz/StaticGetters.h" // GetDirectorFrameworkObject
#include "rz/cRZString.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <new>
#include <string>
#include <utility>

namespace {

using Self = cGZPersistBufferResource;
using Record = ifaces::cIGZPersistDBSerialRecord;

constexpr uint16_t kBmpMagic = 0x4d42; // "BM"

// The pixel converter the TGA reader drives (another class's functions, not yet ours). A pixel format record is five words:
// bits per pixel, then the alpha, red, green and blue masks.
struct PixelFormatWords {
    uint32_t bits;
    uint32_t alpha;
    uint32_t red;
    uint32_t green;
    uint32_t blue;
};
static_assert(sizeof(PixelFormatWords) == 0x14);
constexpr size_t kConverterSize = 0x1010; // four 256-entry tables, then the two byte counts (+0x1000 / +0x1004)

// 0x9cd6a0 (ret 4): `out` the pixel format record of a buffer colour type.
void MakePixelFormat(PixelFormatWords *out, const cGZColorType *type)
{
    OriginalFunction<void *(__thiscall *)(void *, const void *), 0x9cd6a0>()(out, type);
}
// 0x9cd830 is a 22-byte forwarder (this, a, b) to 0x9cea90 that db/library-matches.jsonl takes for a std::map constructor
// (the same bytes); its target is what this calls: the tables of `converter` that map pixel format `first` to `second`.
void BuildConverter(void *converter, const PixelFormatWords *first, const PixelFormatWords *second)
{
    OriginalFunction<void(__thiscall *)(void *, const void *, const void *), 0x9cea90>()(converter, first, second);
}
// 0x9cd890 (ret 0x18): `rows` rows of `width` pixels from `pixels` (`step` bytes apart) through the tables into `bits`.
void ConvertRows(void *converter, void *bits, uint32_t stride, const uint8_t *pixels, int32_t step, uint32_t width, uint32_t rows)
{
    OriginalFunction<void(__thiscall *)(void *, void *, uint32_t, const void *, int32_t, uint32_t, uint32_t), 0x9cd890>()(
        converter, bits, stride, pixels, step, width, rows);
}

// A cRZString on the stack around a copy of some text, as slot 14 hands its label to the caller's string.
struct LabelString {
    union {
        cRZString s;
    };
    LabelString(const char *text, uint32_t len)
    {
        new (&s.mString) std::string(text, len);
        s.mVTable = GameAddress(cRZString::kVTable);
        s.mRefCount = 0;
    }
    ~LabelString() { s.mString.~basic_string(); }
    LabelString(const LabelString &) = delete;
    LabelString &operator=(const LabelString &) = delete;
};

uint32_t Word(const uint8_t *bytes, size_t offset)
{
    uint32_t value;
    std::memcpy(&value, bytes + offset, 4);
    return value;
}

// The RLE8 decoder the BMP readers share (the record reader also stops once an end-of-line escape leaves the row count above
// the height; the row's start is then at or past the end of the image, so the loop ends there anyway).
// ORIGINAL-BUG: the delta escape (0, 2, dx, dy) consumes only its two escape bytes, so
// the dx and dy bytes are read as the next run (count dx, value dy).
// ORIGINAL-BUG: nothing is bounds-checked: a run past the end of `out`, or a stream with no end escape that stops filling the
// image, writes or reads past the buffers.
void DecodeRle8(const uint8_t *in, uint8_t *out, uint32_t outSize, uint32_t pitch)
{
    uint32_t row = 0, column = 0, written = 0, read = 0;
    while (written < outSize) {
        const uint8_t count = in[read], value = in[read + 1];
        if (count != 0) { // an encoded run
            std::memset(out + written, value, count);
            written += count;
            column += count;
            read += 2;
            continue;
        }
        if (value == 0) { // end of line
            ++row;
            written = row * pitch;
            column = 0;
            read += 2;
        } else if (value == 1) { // end of bitmap
            break;
        } else if (value == 2) { // delta
            row += in[read + 3];
            column += in[read + 2];
            written = row * pitch + column;
            read += 2;
        } else { // an absolute run of `value` bytes, padded to an even length
            for (uint32_t i = 0; i < value; ++i) out[written + i] = in[read + 2 + i];
            written += value;
            read += value;
            if ((value & 1) != 0) ++read;
            read += 2;
        }
    }
}

// The error-diffusion pass of both dithers (Floyd-Steinberg, serpentine: the scan direction flips every row). `errorRows`
// holds two rows of per-pixel packed errors (one nibble per channel in bits 4-7, 12-15, 20-23 and, with alpha, 28-31), the
// current row from word 1 and the next `width + 2` words after it. The 4444 result goes to `dst` (a row of 16 bit pixels,
// `dstStep` bytes between rows); the pixel at column i is `src + i * 3` (4 with alpha), `srcStep` bytes between rows.
//
// The vector is sized to 2 * width + 4 words and every one cleared (the original resizes, then fills the words); each row then
// resets the three cells at its starting edge (cur[start], next[start - dir], next[start]).
void DiffuseRows(std::vector<uint32_t> &errorRows, int32_t width, int32_t rows, bool alpha, const uint8_t *src, int32_t srcStep,
                 uint8_t *dst, int32_t dstStep)
{
    const uint32_t count = static_cast<uint32_t>(width) * 2 + 4;
    errorRows.resize(count, 0);
    std::fill(errorRows.begin(), errorRows.end(), 0u);
    if (rows == 0) return;

    uint32_t *current = errorRows.data() + 1;
    uint32_t *next = current + (width + 2);
    int32_t direction = 1, start = 0, end = width;
    const uint32_t bytesPerPixel = alpha ? 4 : 3;
    do {
        current[start] = 0;
        next[start - direction] = 0;
        next[start] = 0;
        for (int32_t i = start; i != end; i += direction) {
            const uint32_t error = current[i];
            const uint8_t *pixel = src + static_cast<ptrdiff_t>(i) * bytesPerPixel;
            uint32_t c0 = ((error >> 4) & 0xf) + pixel[0];
            uint32_t c1 = ((error >> 12) & 0xf) + pixel[1];
            uint32_t c2 = ((error >> 20) & 0xf) + pixel[2];
            uint32_t c3 = alpha ? (error >> 28) + pixel[3] : 0;
            if (c0 > 0xff) c0 = 0xff;
            if (c1 > 0xff) c1 = 0xff;
            if (c2 > 0xff) c2 = 0xff;
            if (c3 > 0xff) c3 = 0xff;
            uint32_t out, carried;
            if (alpha) {
                out = ((((c3 & 0xfff0) << 4) | (c2 & 0xf0)) << 4) | (c0 >> 4) | (c1 & 0xf0);
                carried = ((((((c3 & 0xf) << 8) | (c2 & 0xf)) << 8) | (c1 & 0xf)) << 8) | (c0 & 0xf);
            } else {
                out = (((c2 & 0xfff0) | 0xff00) << 4) | (c0 >> 4) | (c1 & 0xf0);
                carried = ((((c2 & 0xf) << 8) | (c1 & 0xf)) << 8) | (c0 & 0xf);
            }
            *reinterpret_cast<uint16_t *>(dst + static_cast<ptrdiff_t>(i) * 2) = static_cast<uint16_t>(out);
            current[i + direction] += carried * 7;
            next[i + direction] = carried;
            next[i] += carried * 5;
            next[i - direction] += carried * 3;
        }
        direction = -direction;
        src += srcStep;
        dst += dstStep;
        const int32_t newStart = end + direction, newEnd = start + direction;
        start = newStart;
        end = newEnd;
        std::swap(current, next);
    } while (--rows != 0);
}

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Construction

// 0x9b64a0: the base, the vtables, the colour type, then the buffer: a GZCOM object of class 0xc470d326 asked for cIGZBuffer;
// when there is one its aggregate face (0x12f) takes this resource's +0x1c face as the outer unknown. Without that class the
// plain buffer class 0xc470d325 is asked for (not aggregated).
cGZPersistBufferResource::cGZPersistBufferResource(uint32_t format, uint32_t bits) : mFormat(format), mBits(bits)
{
    static_cast<cIGZCOM *>(GetDirectorFrameworkObject())
        ->GetClassObject(kBufferClsidAggregated, kBufferIid, reinterpret_cast<void **>(&mBuffer));
    if (mBuffer != nullptr) {
        mBuffer->QueryInterface(cIGZAggregate::kIid, reinterpret_cast<void **>(&mAggregate));
        mAggregate->SetOuter(static_cast<cIGZPersistBufferResource *>(this));
        return;
    }
    static_cast<cIGZCOM *>(GetDirectorFrameworkObject())
        ->GetClassObject(kBufferClsid, kBufferIid, reinterpret_cast<void **>(&mBuffer));
}

// 0x9b6590 (the deleting destructor around it is the compiler's: 0x9b6630 and thunks): the aggregate lets go of this resource
// and is released, then the buffer; the vector frees its words and the base follows.
cGZPersistBufferResource::~cGZPersistBufferResource()
{
    if (mAggregate != nullptr) {
        mAggregate->SetOuter(nullptr);
        mAggregate->Release();
        mAggregate = nullptr;
    }
    if (mBuffer != nullptr) {
        mBuffer->Release();
        mBuffer = nullptr;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Slots

// 0x9b7c80 (and the face's thunk 0x9b7cf8)
bool cGZPersistBufferResource::QueryInterface(uint32_t riid, void **out)
{
    if (riid == cIGZPersistBufferResource::kIid) {
        *out = static_cast<cIGZPersistBufferResource *>(this);
        AddRef();
        return true;
    }
    if (cGZPersistResource::QueryInterface(riid, out)) return true; // the original jumps to its folded body 0x437e30
    if (mAggregate != nullptr) return mAggregate->NonDelegatingQueryInterface(riid, out);
    if (mBuffer != nullptr) return mBuffer->QueryInterface(riid, out);
    return false;
}

// slot 1 (folded 0x5522b0: jmp 0x99ba50)
uint32_t cGZPersistBufferResource::AddRef()
{
    return cGZPersistResource::AddRef();
}

// slot 2 (folded 0x556090: jmp 0x9b6260)
uint32_t cGZPersistBufferResource::Release()
{
    return cGZPersistResource::Release();
}

// slots 13 and 15 (folded 0x6ea850 -> 0x18a7a0)
bool cGZPersistBufferResource::Slot13(void *)
{
    return false;
}
bool cGZPersistBufferResource::Slot15(void *)
{
    return false;
}

// 0x9b67b0: the 13 character label stored in the image, put into the caller's string through its slot 17 (Copy).
bool cGZPersistBufferResource::GetDescription(cIGZString *out)
{
    LabelString label(GameAddress<const char>(kLabelRva), kLabelLength);
    out->Copy(*label.s.AsInterface());
    return true;
}

// 0x9b6910
cIGZBuffer *cGZPersistBufferResource::GetBuffer()
{
    if (mBuffer == nullptr) return nullptr;
    mBuffer->AddRef();
    return mBuffer;
}

// 0x9b68f0 (the original tail-calls the buffer's slot 0 with the same arguments)
bool cGZPersistBufferResource::QueryBuffer(uint32_t iid, void **out)
{
    if (mBuffer == nullptr) return false;
    return mBuffer->QueryInterface(iid, out);
}

// 0x9b80f0: the new buffer is counted before the old one is released; the aggregate is left as it is.
void cGZPersistBufferResource::SetBuffer(cIGZBuffer *buffer)
{
    if (buffer != nullptr) buffer->AddRef();
    if (mBuffer != nullptr) mBuffer->Release();
    mBuffer = buffer;
}

// ---------------------------------------------------------------------------------------------------------------
// The bitmap converter

// 0x9b8db0
bool cGZPersistBufferResource::IsSupportedBitmap(const BmpInfo *info)
{
    if (info->compression != 0 && info->compression != 1) return false;
    return info->bitCount == 4 || info->bitCount == 8 || info->bitCount == 0x18;
}

// 0x9b8140: `pixels` are the rows of a bottom-up bitmap, each padded to a multiple of 4 bytes; they land in the locked
// buffer top-down (the first source row in the last buffer row). The buffer's bits per pixel decide the conversion; the
// palette colours go through slot 30 (and an 8 bit palette index is converted once: the cache holds the results, -1 for none
// yet, a 32 bit result with its top bit set is taken for none).
bool cGZPersistBufferResource::ConvertBitmap(const BmpInfo *info, const uint8_t *pixels)
{
    const int32_t bufferBits = static_cast<int32_t>(mBuffer->GetBitsPerPixel());
    const uint8_t *src = pixels;
    switch (info->bitCount) {
    case 4: {
        if (bufferBits != 0x10) return false;
        uint32_t colors[16];
        for (int i = 0; i < 16; ++i) {
            const uint8_t *entry = info->palette + i * 4;
            colors[i] = mBuffer->ConvertRGBValueToNative(entry[2], entry[1], entry[0]);
        }
        const uint32_t width = static_cast<uint32_t>(info->width);
        int32_t rows = info->height;
        const int32_t stride = static_cast<int32_t>(mBuffer->GetColorSurfaceStride());
        uint8_t *bits = static_cast<uint8_t *>(mBuffer->GetColorSurfaceBits());
        const int32_t rowBytes = static_cast<int32_t>((width & 1) + width) / 2;
        if (rows < 1) return true;
        uint8_t *row = bits + (rows * 2 - 2) * (stride / 2);
        do {
            auto *out = reinterpret_cast<uint16_t *>(row);
            for (int32_t x = 0; x < static_cast<int32_t>(width); x += 2) {
                const uint8_t byte = *src++;
                out[0] = static_cast<uint16_t>(colors[byte >> 4]);
                out[1] = static_cast<uint16_t>(colors[byte & 0xf]);
                out += 2;
            }
            src += ((static_cast<uint32_t>(rowBytes) - 1) | 3) - static_cast<uint32_t>(rowBytes) + 1;
            row -= (stride / 2) * 2;
        } while (--rows != 0);
        return true;
    }
    case 8: {
        const int32_t width = info->width;
        int32_t rows = info->height;
        const uint32_t pad = ((static_cast<uint32_t>(width) - 1) | 3) - static_cast<uint32_t>(width) + 1;
        switch (bufferBits) {
        case 8: {
            const int32_t stride = static_cast<int32_t>(mBuffer->GetColorSurfaceStride());
            uint8_t *bits = static_cast<uint8_t *>(mBuffer->GetColorSurfaceBits());
            if (rows > 0) {
                uint8_t *row = bits + (rows - 1) * stride;
                do {
                    uint8_t *out = row;
                    for (int32_t x = width; x > 0; --x) *out++ = *src++;
                    src += pad;
                    row -= stride;
                } while (--rows != 0);
            }
            return true;
        }
        case 0x10: {
            int32_t cache[256];
            std::memset(cache, 0xff, sizeof(cache));
            const int32_t stride = static_cast<int32_t>(mBuffer->GetColorSurfaceStride());
            uint8_t *bits = static_cast<uint8_t *>(mBuffer->GetColorSurfaceBits());
            if (rows > 0) {
                uint8_t *row = bits + (rows * 2 - 2) * (stride / 2);
                do {
                    auto *out = reinterpret_cast<uint16_t *>(row);
                    for (int32_t x = width; x > 0; --x) {
                        const uint32_t index = *src++;
                        int32_t color = cache[index];
                        if (color < 0) {
                            const uint8_t *entry = info->palette + index * 4;
                            color = static_cast<int32_t>(mBuffer->ConvertRGBValueToNative(entry[2], entry[1], entry[0]));
                            cache[index] = color;
                        }
                        *out++ = static_cast<uint16_t>(color);
                    }
                    row -= (stride / 2) * 2;
                    src += pad;
                } while (--rows != 0);
            }
            return true;
        }
        case 0x20: {
            int32_t cache[256];
            std::memset(cache, 0xff, sizeof(cache));
            const int32_t stride = static_cast<int32_t>(mBuffer->GetColorSurfaceStride());
            const int32_t quarter = stride >> 2;
            uint8_t *bits = static_cast<uint8_t *>(mBuffer->GetColorSurfaceBits());
            if (rows > 0) {
                uint8_t *row = bits + (rows * 4 - 4) * quarter;
                do {
                    auto *out = reinterpret_cast<int32_t *>(row);
                    for (int32_t x = width; x > 0; --x) {
                        const uint32_t index = *src++;
                        int32_t color = cache[index];
                        if (color < 0) {
                            const uint8_t *entry = info->palette + index * 4;
                            color = static_cast<int32_t>(mBuffer->ConvertRGBValueToNative(entry[2], entry[1], entry[0]));
                            cache[index] = color;
                        }
                        *out++ = color;
                    }
                    row -= quarter * 4;
                    src += pad;
                } while (--rows != 0);
            }
            return true;
        }
        default: return false;
        }
    }
    case 0x18:
        switch (bufferBits) {
        case 0x10: {
            if (mFormat == 1 && mBuffer->GetNativeColorType().format == 7) return DitherRgb24(info, src);
            const uint32_t width = static_cast<uint32_t>(info->width);
            int32_t rows = info->height;
            const int32_t half = static_cast<int32_t>(mBuffer->GetColorSurfaceStride()) / 2;
            uint8_t *bits = static_cast<uint8_t *>(mBuffer->GetColorSurfaceBits());
            if (rows > 0) {
                uint8_t *row = bits + (rows * 2 - 2) * half;
                do {
                    auto *out = reinterpret_cast<uint16_t *>(row);
                    for (uint32_t x = width; x != 0; --x) {
                        *out++ = static_cast<uint16_t>(mBuffer->ConvertRGBValueToNative(src[2], src[1], src[0]));
                        src += 3;
                    }
                    row -= half * 2;
                    src += width & 3;
                } while (--rows != 0);
            }
            return true;
        }
        case 0x18: {
            const uint32_t width = static_cast<uint32_t>(info->width);
            const uint32_t pad = width & 3;
            int32_t rows = info->height;
            const int32_t stride = static_cast<int32_t>(mBuffer->GetColorSurfaceStride());
            uint8_t *bits = static_cast<uint8_t *>(mBuffer->GetColorSurfaceBits());
            if (rows > 0) {
                uint8_t *row = bits + stride * (rows - 1);
                do {
                    uint8_t *out = row;
                    for (uint32_t x = width; x != 0; --x) {
                        out[0] = src[2];
                        out[1] = src[1];
                        out[2] = src[0];
                        src += 3;
                        out += 3;
                    }
                    src += pad;
                    row -= stride;
                } while (--rows != 0);
            }
            return true;
        }
        case 0x20: {
            uint8_t *bits = static_cast<uint8_t *>(mBuffer->GetColorSurfaceBits());
            const int32_t width = info->width;
            int32_t rows = info->height;
            const uint32_t pitch = ((static_cast<uint32_t>(width) + 1) * 3) & ~3u;
            const int32_t stride = static_cast<int32_t>(mBuffer->GetColorSurfaceStride());
            if (rows != 0) {
                const uint8_t *row = src + static_cast<uint32_t>(rows - 1) * pitch;
                uint8_t *out = bits;
                do {
                    const uint8_t *in = row;
                    for (int32_t x = width; x != 0; --x) {
                        const uint32_t value = 0xff000000u + (static_cast<uint32_t>(in[2]) << 16) + in[0] + (static_cast<uint32_t>(in[1]) << 8);
                        std::memcpy(out, &value, 4);
                        out += 4;
                        in += 3;
                    }
                    out += stride - width * 4;
                    row -= pitch;
                } while (--rows != 0);
            }
            return true;
        }
        default: return false;
        }
    default: return false;
    }
}

// 0x9b88e0: a 24 bit bitmap into the 16 bit ARGB4444 buffer. The source rows are bottom-up and padded to (width + 1) * 3
// rounded down to a multiple of 4 (the bitmap's own padding is the same size or larger for the widths that matter).
bool cGZPersistBufferResource::DitherRgb24(const BmpInfo *info, const uint8_t *pixels)
{
    const int32_t width = info->width;
    const int32_t rows = info->height;
    const uint32_t pitch = ((static_cast<uint32_t>(width) + 1) * 3) & ~3u;
    const int32_t stride = static_cast<int32_t>(mBuffer->GetColorSurfaceStride());
    uint8_t *bits = static_cast<uint8_t *>(mBuffer->GetColorSurfaceBits());
    const uint8_t *last = pixels;
    if (rows != 0) last = pixels + static_cast<uint32_t>(rows - 1) * pitch;
    DiffuseRows(mErrorRows, width, rows, false, last, -static_cast<int32_t>(pitch), bits, (stride / 2) * 2);
    return true;
}

// 0x9b8b40
bool cGZPersistBufferResource::DitherArgb32(int32_t width, int32_t height, const uint8_t *pixels, int32_t stride)
{
    const int32_t half = static_cast<int32_t>(mBuffer->GetColorSurfaceStride()) / 2;
    uint8_t *bits = static_cast<uint8_t *>(mBuffer->GetColorSurfaceBits());
    DiffuseRows(mErrorRows, width, height > 0 ? height : 0, true, pixels, stride, bits, half * 2);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// The readers

// 0x9b6930
bool cGZPersistBufferResource::ReadBmpRecord(Record *record, uint32_t)
{
    uint8_t header[14]; // "BM", the file size, 4 reserved bytes, the offset of the pixels
    if (!record->GetFieldVoid(header, 14)) return false;
    uint16_t magic;
    std::memcpy(&magic, header, 2);
    if (magic != kBmpMagic) return false;
    BmpInfo info;
    if (!record->GetFieldVoid(&info, 0x28)) return false;
    if (!IsSupportedBitmap(&info)) return false;

    // ORIGINAL-BUG: a 4 bit bitmap's row size comes out 0 here ((bits >> 3) * width = 0), so no pixels are read for it and
    // the converter reads from an empty vector; the file reader has the 4 bit case (width / 2).
    const uint32_t pitch = ((static_cast<uint32_t>(info.bitCount >> 3) * static_cast<uint32_t>(info.width) - 1) | 3) + 1;
    const uint32_t size = static_cast<uint32_t>(info.height) * pitch;
    std::vector<uint8_t> pixels(size);
    if (info.colorsUsed != 0 || info.compression == 1 || info.bitCount == 8) {
        const uint32_t entries = info.colorsUsed != 0 ? info.colorsUsed : 0x100;
        // ORIGINAL-BUG: the palette is read straight into the 256 entry area; more than 256 entries overrun the stack. Ours
        // reads them all and keeps the first 256. Without a palette (a 4 bit bitmap with no colours used, no RLE) the
        // palette stays uninitialised.
        if (entries <= 0x100) {
            record->GetFieldVoid(info.palette, entries * 4);
        } else {
            std::vector<uint8_t> all(static_cast<size_t>(entries) * 4);
            record->GetFieldVoid(all.data(), entries * 4);
            std::memcpy(info.palette, all.data(), sizeof(info.palette));
        }
    }
    const uint32_t fileSize = Word(header, 2), offBits = Word(header, 10);
    if (offBits + 14 < 0x36) return false;

    if (info.compression == 1) {
        uint32_t packedSize = info.sizeImage;
        if (packedSize == 0) packedSize = fileSize - offBits;
        if (packedSize >= fileSize) return false;
        std::vector<uint8_t> packed(packedSize);
        record->GetFieldVoid(packed.data(), packedSize); // the result is not looked at
        DecodeRle8(packed.data(), pixels.data(), size, pitch);
    } else if (!record->GetFieldVoid(pixels.data(), size)) {
        return false;
    }

    if (!mBuffer->Init(static_cast<uint32_t>(info.width), static_cast<uint32_t>(info.height), mFormat, mBits)) return false;
    if (!mBuffer->Lock(kLockBmp)) return false;
    const bool ok = ConvertBitmap(&info, pixels.data());
    mBuffer->Unlock(kLockBmp);
    return ok;
}

// 0x9b6c30
bool cGZPersistBufferResource::ReadBmpFile(cIGZString *path, uint32_t, uint32_t format, uint32_t bits, uint32_t)
{
    if (path->Strlen() == 0) return false;
    std::FILE *file = std::fopen(path->ToChar(), "rb");
    if (file == nullptr) return false;

    bool result = false;
    BmpInfo *info = nullptr;
    uint8_t *pixels = nullptr;
    uint8_t header[14];
    if (std::fread(header, 14, 1, file) == 1 && header[0] == 'B' && header[1] == 'M') {
        info = static_cast<BmpInfo *>(std::malloc(sizeof(BmpInfo)));
        std::fread(info, sizeof(BmpInfo), 1, file); // a short file leaves the rest as it was
        if (IsSupportedBitmap(info)) {
            const uint32_t width = static_cast<uint32_t>(info->width);
            uint32_t rowBytes;
            if (info->bitCount == 4)
                rowBytes = static_cast<uint32_t>(static_cast<int32_t>((width & 1) + width) / 2);
            else
                rowBytes = static_cast<uint32_t>(info->bitCount >> 3) * width;
            const uint32_t pitch = ((rowBytes - 1) | 3) + 1;
            const uint32_t size = static_cast<uint32_t>(info->height) * pitch;
            pixels = static_cast<uint8_t *>(std::malloc(size));
            const long offBits = static_cast<long>(Word(header, 10));
            bool haveRows = true;
            if (info->compression == 1) {
                uint8_t *packed = static_cast<uint8_t *>(std::malloc(size * 2));
                if (std::fseek(file, offBits, SEEK_SET) != 0) {
                    // ORIGINAL-BUG (not reproduced): the original leaks the packed rows here.
                    std::free(packed);
                    haveRows = false;
                } else {
                    if (std::fread(packed, 1, size * 2, file) != 0) {
                        // ORIGINAL-BUG: with nothing read the rows stay as malloc left them.
                        if (size != 0) std::memset(pixels, 0, size);
                        DecodeRle8(packed, pixels, size, pitch);
                    }
                    std::free(packed);
                }
            } else if (std::fseek(file, offBits, SEEK_SET) != 0 || std::fread(pixels, 1, size, file) != size) {
                haveRows = false;
            }
            if (haveRows && mBuffer->Init(static_cast<uint32_t>(info->width), static_cast<uint32_t>(info->height), format, bits) &&
                mBuffer->Lock(kLockBmp)) {
                result = ConvertBitmap(info, pixels);
                mBuffer->Unlock(kLockBmp);
            }
        }
    }
    std::fclose(file);
    std::free(pixels);
    std::free(info);
    return result;
}

// 0x9b77b0
bool cGZPersistBufferResource::ReadTgaRecord(Record *record, uint32_t)
{
    uint8_t idLength, colorMapType, imageType, mapEntryBits, depth, descriptor;
    uint16_t mapFirst, mapLength, xOrigin, yOrigin, width, height;
    if (!record->SeekAbsolute(0)) return false;
    if (!record->GetFieldUint8(&idLength)) return false;
    if (!record->GetFieldUint8(&colorMapType)) return false;
    if (!record->GetFieldUint8(&imageType)) return false;
    if (!record->GetFieldUint16(&mapFirst)) return false;
    if (!record->GetFieldUint16(&mapLength)) return false;
    if (!record->GetFieldUint8(&mapEntryBits)) return false;
    if (!record->GetFieldUint16(&xOrigin)) return false;
    if (!record->GetFieldUint16(&yOrigin)) return false;
    if (!record->GetFieldUint16(&width)) return false;
    if (!record->GetFieldUint16(&height)) return false;
    if (!record->GetFieldUint8(&depth)) return false;
    if (!record->GetFieldUint8(&descriptor)) return false;
    // past the image id and the colour map
    if (!record->SeekRelative(static_cast<int32_t>(static_cast<uint32_t>(mapLength) * ((mapEntryBits + 7u) >> 3) + idLength)))
        return false;
    const bool compressed = imageType == 10; // (type 2 or 10 are the only ones that pass the next test)
    if ((imageType & 0xf7) != 2) return false; // true colour only (type 2, or 10 with run lengths)

    const uint32_t bytesPerPixel = (depth + 7u) >> 3;
    const uint32_t total = static_cast<uint32_t>(height) * width * bytesPerPixel;
    std::vector<uint8_t> pixels(total);
    if (!compressed) {
        if (!record->GetFieldVoid(pixels.data(), total)) return false;
    } else {
        // The rest of the record from here is the packet stream. (The original pushes the allocator argument of the vector
        // below before these two calls; the slots take none, and the vector's constructor pops it, so the stack balances.)
        const uint32_t position = record->GetPosition();
        const uint32_t end = record->GetSize();
        std::vector<uint8_t> packed(end - position);
        if (!record->GetFieldVoid(packed.data(), static_cast<uint32_t>(packed.size()))) return false;
        // Packets: a header byte (bit 7: repeat the one pixel that follows, else the pixels that follow, the count in
        // bits 0-6, plus one). ORIGINAL-BUG: nothing is bounds-checked against the buffers.
        int32_t remaining = static_cast<int32_t>(total);
        const uint8_t *src = packed.data();
        uint8_t *dst = pixels.data();
        while (remaining > 0) {
            const uint8_t head = *src++;
            for (uint32_t i = 0; i < bytesPerPixel; ++i) *dst++ = *src++;
            const uint8_t *afterFirst = src;
            if (head < 0x80) src += static_cast<uint32_t>(head) * bytesPerPixel;
            const uint32_t extra = (head & 0x7f) * bytesPerPixel;
            remaining -= static_cast<int32_t>(extra + bytesPerPixel);
            if (extra != 0) {
                const uint8_t *from = head < 0x80 ? afterFirst : dst - bytesPerPixel;
                for (uint32_t i = 0; i < extra; ++i) *dst++ = *from++;
            }
        }
    }

    if (!mBuffer->Init(width, height, mFormat, mBits)) return false;
    if (!mBuffer->Lock(kLockTga)) return false;
    const uint32_t alphaBits = descriptor & 0xf;
    void *const bits = mBuffer->GetColorSurfaceBits();
    const uint32_t stride = mBuffer->GetColorSurfaceStride();
    int32_t step = static_cast<int32_t>(static_cast<uint32_t>(width) * bytesPerPixel);
    const uint8_t *source = pixels.data();
    if ((descriptor & 0x20) == 0) { // rows bottom-up
        source += static_cast<uint32_t>(height - 1) * static_cast<uint32_t>(step);
        step = -step;
    }

    PixelFormatWords from = {};
    bool convert = true;
    if (depth == 0x10) {
        from = {0x10, alphaBits != 0 ? 0x8000u : 0u, 0x7c00, 0x3e0, 0x1f};
    } else if (depth == 0x18) {
        from = {0x18, 0, 0xff0000, 0xff00, 0xff};
    } else if (depth == 0x20) {
        if (mFormat == 1 && mBuffer->GetNativeColorType().format == 7) {
            DitherArgb32(width, height, source, step);
            mBuffer->Unlock(kLockTga);
            return true;
        }
        from = {0x20, ~(0xffffffffu >> (alphaBits & 0x1f)), 0xff0000, 0xff00, 0xff};
    } else {
        convert = false; // other depths leave the buffer as Init made it
    }
    if (convert) {
        const cGZColorType native = mBuffer->GetNativeColorType();
        PixelFormatWords to;
        MakePixelFormat(&to, &native);
        alignas(4) uint8_t converter[kConverterSize];
        BuildConverter(converter, &to, &from);
        ConvertRows(converter, bits, stride, source, step, width, height);
    }
    mBuffer->Unlock(kLockTga);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// The RVA hooks' adapters

using Adapters = cGZPersistBufferResourceAdapters;

// 0x9b64a0
Adapters::Self *__fastcall Adapters::Construct(void *memory, void *, uint32_t format, uint32_t bits)
{
    return ::new (memory) Self(format, bits);
}

// 0x9b6590
void __fastcall Adapters::Destruct(Self *self, void *)
{
    self->Self::~Self();
}
