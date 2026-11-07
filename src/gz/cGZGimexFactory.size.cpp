// cGZGimexFactory (lane LX, standalone shape; src/gz/cGZGimexFactory.h). The row converters are in
// cGZGimexFactoryRows.cpp (speed-built in the game).
#include "gz/cGZGimexFactory.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZGimexReader.h"
#include "gz/cGZPersistBufferResource.h"
#include "gz/cGZPersistResourceFactory.h"
#include "gz/cIGZCOM.h"
#include "gz/cIGZPersistResource.h"
#include "gz/cRZCursor.h" // AcquireGraphicSystem, ColorFormatBits
#include "ifaces/cIGZFile.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZGraphicSystem.h"
#include "ifaces/cIGZPersistBufferResource.h"
#include "ifaces/cIGZPersistDBRecord.h"
#include "ifaces/cIGZPersistDBSegment.h"
#include "ifaces/cIGZPersistResourceManager.h"
#include "rz/RZAutoRef.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject, ScopedRef
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "rz/cRZFile.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <new>

namespace {

constexpr uint32_t kFileSize = 0x9c; // cRZFile
// cIGZFile::Open modes: read (access 1, open existing 2, share read 1) and write (access 2, create always 1, share 1).
constexpr uint32_t kReadAccess = 1, kReadCreation = 2, kReadSharing = 1;
constexpr uint32_t kWriteAccess = 2, kWriteCreation = 1, kWriteSharing = 1;
constexpr int32_t kWriteOpenFlag = 1; // the writer's Open flag (the codec's wopen)

// The resource manager service (the getter 0x20db69) and the statics the factory caches in the game's cells.
constexpr uint32_t kResourceManagerId = 0x056b906e, kResourceManagerIid = 0x656b8efc;
constexpr uint32_t kResourceManagerCell = 0x10e6924;
constexpr uint32_t kFactoryGraphicSystemCell = 0x10e6928;
constexpr uint32_t kIjlTriedCell = 0x10e692c;  // a byte: the load was tried
constexpr uint32_t kIjlModuleCell = 0x10e6930; // the module handle
constexpr uint32_t kIjlName = 0xe4a4ec;        // the DLL name (the game's string)
constexpr uint32_t kIatLoadLibraryA = 0xd9a128;
using LoadLibraryFn = HMODULE(WINAPI *)(const char *);

constexpr uint32_t kBufferIid = 0x20732180;  // cIGZBuffer (asked of the framework's buffer class)
constexpr uint32_t kRecordOpenRead = 0, kRecordOpenWrite = 1; // the resource manager's OpenRecord
constexpr uint32_t kSegmentOpenWrite = 2;                     // a segment's OpenRecord
constexpr uint32_t kResourceBufferFace = 0x1c;   // cGZPersistBufferResource: its cIGZPersistBufferResource face
constexpr uint32_t kResourceColorFormat = 0x28;  // cGZPersistBufferResource: the colour format it was made in

// The bits-per-pixel byte count Gimex rows use (a signed division, as the original's cdq / and 7 / sar 3).
int32_t BytesPerPixel(const gimex::Info *info)
{
    return info->bitsPerPixel / 8;
}

cIGZCOM *GZCOM()
{
    return static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
}

// A framework object asked for by class and interface; null (the local stays null) when the framework has none.
template <typename T> T *NewObject(uint32_t classId, uint32_t iid)
{
    T *object = nullptr;
    if (static_cast<uint8_t>(GZCOM()->GetClassObject(classId, iid, reinterpret_cast<void **>(&object))) == 0) return nullptr;
    return object;
}

// A new cRZFile on `path` (the game's constructor 0x1c0c07 on our allocation); null when the allocation failed.
ifaces::cIGZFile *NewFile(const cIGZString &path)
{
    void *const memory = ::operator new(kFileSize, std::nothrow);
    return memory ? As<ifaces::cIGZFile>(static_cast<cRZFile *>(memory)->Construct(path)) : nullptr;
}

cGZPersistResourceFactory *Base(cGZGimexResourceFactory *face)
{
    return reinterpret_cast<cGZPersistResourceFactory *>(face);
}

// A database record opened for `key` (by the segment or the resource manager) handed to `use` as a serial record; the
// record closed and released afterwards. False only when the record did not open: when it opened but is no serial
// record, the open's true stands.
template <typename Use> bool WithSerialRecord(bool opened, ifaces::cIGZPersistDBRecord *record, Use use)
{
    if (!opened) return false;
    bool result = true;
    cIGZUnknown *serial = nullptr;
    if (record->QueryInterface(cGZGimexFactory::kSerialRecordIid, &serial)) {
        result = use(serial);
        serial->Release();
    }
    record->Close();
    record->Release();
    return result;
}

} // namespace

// ---- construction ----

cGZGimexResourceFactory::cGZGimexResourceFactory()
{
    mLock.Construct();
}

cGZGimexResourceFactory::~cGZGimexResourceFactory()
{
    mLock.Destruct();
}

cGZGimexFactory::cGZGimexFactory() = default;

cGZGimexFactory::~cGZGimexFactory() = default;

// ---- cIGZUnknown ----

bool cGZGimexFactory::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid == 1 || riid == kIid) {
        *ppv = static_cast<cIGZGimexFactory *>(this);
        AddRef();
        return true;
    }
    return Base(this)->QueryInterface(riid, ppv);
}

uint32_t cGZGimexFactory::AddRef()
{
    return ++mRefCount;
}

uint32_t cGZGimexFactory::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete static_cast<cGZGimexResourceFactory *>(this);
    return 0;
}

// ---- reading ----

bool cGZGimexFactory::ReadStream(ifaces::cIGZFile *stream, uint32_t format, cIGZBuffer *buffer, uint32_t colorFormat)
{
    cIGZGimexReader *const reader = NewObject<cIGZGimexReader>(cGZGimexReader::kClassId, cIGZGimexReader::kIid);
    if (!reader) return false;
    if (!reader->SetStream(stream)) {
        reader->Release();
        return false;
    }
    return ReadImage(reader, format, buffer, colorFormat);
}

bool cGZGimexFactory::NewFromFile(const cIGZString &path, cIGZBuffer **out, uint32_t colorFormat)
{
    if (!CreateBuffer(out)) return false;
    const bool result = ReadFile(path, *out, colorFormat);
    if (!result) {
        (*out)->Release();
        *out = nullptr;
    }
    return result;
}

bool cGZGimexFactory::ReadFile(const cIGZString &path, cIGZBuffer *buffer, uint32_t colorFormat)
{
    ifaces::cIGZFile *const file = NewFile(path);
    if (file) file->AddRef();
    bool result = false;
    // ORIGINAL-BUG: no null test: a failed allocation is opened anyway.
    if (file->Open(kReadAccess, kReadCreation, kReadSharing)) {
        result = ReadStream(file, gimex::kByExtensionOrProbe, buffer, colorFormat);
        if (file->IsOpen()) file->Close();
    }
    if (file) file->Release();
    return result;
}

bool cGZGimexFactory::IsReadable(const cIGZString &path)
{
    uint32_t size[2] = {0, 0};
    return GetImageSize(path, size);
}

bool cGZGimexFactory::GetImageSize(const cIGZString &path, uint32_t *size)
{
    ifaces::cIGZFile *const file = NewFile(path);
    if (file) file->AddRef();
    bool result = false;
    // ORIGINAL-BUG: no null test: a failed allocation is opened anyway.
    if (file->Open(kReadAccess, kReadCreation, kReadSharing)) {
        cIGZGimexReader *reader = nullptr;
        if (static_cast<uint8_t>(GZCOM()->GetClassObject(cGZGimexReader::kClassId, cIGZGimexReader::kIid,
                                                          reinterpret_cast<void **>(&reader))) != 0 &&
            reader->SetStream(file)) {
            LoadIjl();
            int32_t frames = 0;
            if (reader->Open(gimex::kByExtensionOrProbe, &frames) && frames != 0) {
                gimex::Info *const info = reader->GetInfo(0);
                result = info != nullptr;
                if (info) {
                    size[0] = static_cast<uint32_t>(info->width);
                    size[1] = static_cast<uint32_t>(info->height);
                    reader->FreeInfo(info);
                }
                reader->Close();
            }
        }
        if (reader) reader->Release();
    }
    if (file) file->Release();
    return result;
}

bool cGZGimexFactory::NewFromStream(ifaces::cIGZFile *stream, uint32_t format, cIGZBuffer **out, uint32_t colorFormat)
{
    if (!CreateBuffer(out)) return false;
    const bool result = ReadStream(stream, format, *out, colorFormat);
    if (!result) {
        (*out)->Release();
        *out = nullptr;
    }
    return result;
}

bool cGZGimexFactory::ReadKey(const cGZPersistResourceKey *key, uint32_t format, cIGZBuffer *buffer, uint32_t colorFormat)
{
    // ORIGINAL-BUG: no null test on the resource manager (here, in NewFromKey and in WriteKeyRect): none is a null call.
    ifaces::cIGZPersistDBRecord *record;
    const bool opened = static_cast<ifaces::cIGZPersistResourceManager *>(GetResourceManager())
                            ->OpenRecord(key, reinterpret_cast<void **>(&record), kRecordOpenRead) != 0;
    return WithSerialRecord(opened, record, [&](cIGZUnknown *serial) {
        return ReadDBRecord(serial, format, buffer, colorFormat);
    });
}

bool cGZGimexFactory::NewFromKey(const cGZPersistResourceKey *key, uint32_t format, cIGZBuffer **out, uint32_t colorFormat)
{
    ifaces::cIGZPersistDBRecord *record;
    const bool opened = static_cast<ifaces::cIGZPersistResourceManager *>(GetResourceManager())
                            ->OpenRecord(key, reinterpret_cast<void **>(&record), kRecordOpenRead) != 0;
    return WithSerialRecord(opened, record, [&](cIGZUnknown *serial) {
        return NewFromDBRecord(serial, format, out, colorFormat);
    });
}

bool cGZGimexFactory::ReadDBRecord(cIGZUnknown *record, uint32_t format, cIGZBuffer *buffer, uint32_t colorFormat)
{
    cIGZGimexReader *const reader = NewObject<cIGZGimexReader>(cGZGimexReader::kClassId, cIGZGimexReader::kIid);
    if (!reader) return false;
    if (!reader->SetRecord(record)) {
        reader->Release();
        return false;
    }
    return ReadImage(reader, format, buffer, colorFormat);
}

bool cGZGimexFactory::NewFromDBRecord(cIGZUnknown *record, uint32_t format, cIGZBuffer **out, uint32_t colorFormat)
{
    if (!CreateBuffer(out)) return false;
    const bool result = ReadDBRecord(record, format, *out, colorFormat);
    if (!result) {
        (*out)->Release();
        *out = nullptr;
    }
    return result;
}

// ---- writing ----

bool cGZGimexFactory::WriteStream(ifaces::cIGZFile *stream, uint32_t format, cIGZBuffer *buffer, uint32_t quality)
{
    const cGZRect rect = *buffer->GetBoundsPtr();
    return WriteStreamRect(stream, format, buffer, &rect, quality);
}

bool cGZGimexFactory::WriteStreamRect(ifaces::cIGZFile *stream, uint32_t format, cIGZBuffer *buffer, const cGZRect *rect,
                                      uint32_t quality)
{
    cIGZGimexWriter *const writer = NewObject<cIGZGimexWriter>(cGZGimexWriter::kClassId, cIGZGimexWriter::kIid);
    // ORIGINAL-BUG: a writer that does not take the stream is never released.
    if (!writer || !writer->SetStream(stream)) return false;
    return WriteImage(writer, format, buffer, rect, quality);
}

bool cGZGimexFactory::WriteFile(const cIGZString &path, cIGZBuffer *buffer, uint32_t quality)
{
    const cGZRect rect = *buffer->GetBoundsPtr();
    return WriteFileRect(path, buffer, &rect, quality);
}

bool cGZGimexFactory::WriteFileRect(const cIGZString &path, cIGZBuffer *buffer, const cGZRect *rect, uint32_t quality)
{
    ifaces::cIGZFile *const file = NewFile(path);
    // ORIGINAL-BUG: no null test: a failed allocation is counted and opened anyway.
    file->AddRef();
    bool result = false;
    if (file->Open(kWriteAccess, kWriteCreation, kWriteSharing)) {
        result = WriteStreamRect(file, gimex::kByExtension, buffer, rect, quality);
        file->Close();
    }
    file->Release();
    return result;
}

bool cGZGimexFactory::WriteKey(const cGZPersistResourceKey *key, ifaces::cIGZPersistDBSegment *segment, uint32_t format,
                               cIGZBuffer *buffer, uint32_t quality)
{
    const cGZRect rect = *buffer->GetBoundsPtr();
    return WriteKeyRect(key, segment, format, buffer, &rect, quality);
}

bool cGZGimexFactory::WriteKeyRect(const cGZPersistResourceKey *key, ifaces::cIGZPersistDBSegment *segment,
                                   uint32_t format, cIGZBuffer *buffer, const cGZRect *rect, uint32_t quality)
{
    ifaces::cIGZPersistDBRecord *record = nullptr;
    bool opened;
    if (segment) {
        opened = segment->OpenRecord(key, &record, kSegmentOpenWrite) != 0;
    } else {
        opened = static_cast<ifaces::cIGZPersistResourceManager *>(GetResourceManager())
                     ->OpenRecord(key, reinterpret_cast<void **>(&record), kRecordOpenWrite) != 0;
    }
    return WithSerialRecord(opened, record, [&](cIGZUnknown *serial) {
        return WriteDBRecordRect(serial, format, buffer, rect, quality);
    });
}

bool cGZGimexFactory::WriteDBRecord(cIGZUnknown *record, uint32_t format, cIGZBuffer *buffer, uint32_t quality)
{
    const cGZRect rect = *buffer->GetBoundsPtr();
    return WriteDBRecordRect(record, format, buffer, &rect, quality);
}

bool cGZGimexFactory::WriteDBRecordRect(cIGZUnknown *record, uint32_t format, cIGZBuffer *buffer, const cGZRect *rect,
                                        uint32_t quality)
{
    cIGZGimexWriter *const writer = NewObject<cIGZGimexWriter>(cGZGimexWriter::kClassId, cIGZGimexWriter::kIid);
    // ORIGINAL-BUG: a writer that does not take the record is never released.
    if (!writer || !writer->SetRecord(record)) return false;
    return WriteImage(writer, format, buffer, rect, quality);
}

// ---- cIGZPersistResourceFactory ----

bool cGZGimexFactory::CreateInstance(ifaces::cIGZPersistDBRecord *record, uint32_t riid, void **out, uint32_t a4,
                                     uint32_t a5)
{
    return Base(this)->CreateInstance(record, riid, out, a4, a5);
}

bool cGZGimexFactory::CreateInstanceByType(uint32_t type, uint32_t riid, void **out, uint32_t a4, uint32_t)
{
    if (type != kResourceType) return false;
    const uint32_t format = a4 & 0xffff;
    const uint32_t bits = ColorFormatBits(format);
    void *const memory = ::operator new(kResourceSize, std::nothrow);
    // The cGZPersistBufferResource (ours, lane NU).
    cIGZUnknown *const resource =
        memory ? static_cast<cGZPersistResource *>(::new (memory) cGZPersistBufferResource(format, bits)) : nullptr;
    // ORIGINAL-BUG: no null test: a failed allocation is asked for the interface anyway.
    return resource->QueryInterface(riid, out);
}

bool cGZGimexFactory::Read(cIGZUnknown *resource, cIGZUnknown *record)
{
    return Base(this)->Read(resource, record);
}

bool cGZGimexFactory::Write(cIGZUnknown *resource, cIGZUnknown *record)
{
    return Base(this)->Write(resource, record);
}

bool cGZGimexFactory::CreateInstance3(ifaces::cIGZPersistDBRecord *record, uint32_t riid, void **out)
{
    return CreateInstance(record, riid, out, 0, 0);
}

bool cGZGimexFactory::CreateResource(uint32_t type, uint32_t riid, void **out)
{
    if (type != kResourceType) return false;
    return CreateInstanceByType(kResourceType, riid, out, mDefaultColorFormat, 0);
}

bool cGZGimexFactory::ReadRecord(cIGZPersistResource *resource, cIGZUnknown *record)
{
    if (resource->GetType() != kResourceType) return false;
    auto *const bytes = reinterpret_cast<uint8_t *>(resource);
    auto *const buffer =
        static_cast<cIGZBuffer *>(As<ifaces::cIGZPersistBufferResource>(bytes + kResourceBufferFace)->GetBuffer());
    if (!buffer) return false;
    const uint32_t colorFormat = *reinterpret_cast<const uint32_t *>(bytes + kResourceColorFormat);
    const bool result = ReadDBRecord(record, gimex::kProbe, buffer, colorFormat);
    buffer->Release();
    return result;
}

bool cGZGimexFactory::WriteRecord(cIGZPersistResource *resource, cIGZUnknown *record)
{
    if (resource->GetType() != kResourceType) return false;
    auto *const bytes = reinterpret_cast<uint8_t *>(resource);
    auto *const buffer =
        static_cast<cIGZBuffer *>(As<ifaces::cIGZPersistBufferResource>(bytes + kResourceBufferFace)->GetBuffer());
    if (!buffer) return false;
    const bool result = WriteDBRecord(record, kResourceWriteFormat, buffer, kResourceWriteQuality);
    buffer->Release();
    return result;
}

// ---- helpers ----

bool cGZGimexFactory::CreateBuffer(cIGZBuffer **out)
{
    if (static_cast<uint8_t>(GZCOM()->GetClassObject(kBufferClass, kBufferIid, reinterpret_cast<void **>(out))) != 0)
        return true;
    // ORIGINAL-BUG: no null test on the graphics system.
    return static_cast<ifaces::cIGZGraphicSystem *>(GetGraphicSystem())->CreateBuffer(out);
}

bool cGZGimexFactory::ReadImage(cIGZGimexReader *reader, uint32_t format, cIGZBuffer *buffer, uint32_t colorFormat)
{
    if (!reader || !buffer) return false;
    LoadIjl();
    int32_t frames;
    bool result = reader->Open(format, &frames);
    if (result && frames != 0) {
        gimex::Info *const info = reader->GetInfo(0);
        if (info) {
            mPixels.resize(static_cast<size_t>(BytesPerPixel(info) * info->height * info->width));
            result = reader->Read(info, mPixels.data(), BytesPerPixel(info) * info->width);
            if (result) {
                if (buffer->IsReady()) buffer->Shutdown();
                result = buffer->Init(static_cast<uint32_t>(info->width), static_cast<uint32_t>(info->height),
                                      colorFormat, ColorFormatBits(colorFormat));
                if (result && buffer->Lock(kLockForInit)) {
                    result = CopyToBuffer(info, mPixels.data(), buffer);
                    buffer->Unlock(kLockForInit);
                }
            }
            reader->FreeInfo(info);
        }
        reader->Close();
    }
    reader->Release();
    return result;
}

bool cGZGimexFactory::CopyToBuffer(const gimex::Info *info, const uint8_t *pixels, cIGZBuffer *buffer)
{
    if (!buffer || !info || !pixels) return false;
    if (info->bitsPerPixel == 32) {
        const uint32_t width = static_cast<uint32_t>(info->width);
        uint32_t rows = static_cast<uint32_t>(info->height);
        const int32_t sourceRow = BytesPerPixel(info) * info->width;
        const uint32_t stride = buffer->GetColorSurfaceStride();
        const uint32_t format = buffer->GetColorType().format;
        if (!buffer->Lock(kLockForPixels)) return false;
        bool result = true;
        uint8_t *destination = static_cast<uint8_t *>(buffer->GetColorSurfaceBits());
        if (mDither && mDefaultColorFormat == 1 && format == 7) {
            Dither(info, pixels, buffer); // its answer is not looked at
        } else {
            auto rowsOf = [&](auto convert, auto destinationType) {
                using Pixel = decltype(destinationType);
                for (; rows != 0; --rows) {
                    convert(reinterpret_cast<const uint32_t *>(pixels), reinterpret_cast<Pixel *>(destination), width);
                    destination += stride;
                    pixels += sourceRow;
                }
            };
            switch (format) {
            case 4: rowsOf(gimexrows::To1555Opaque, uint16_t{}); break;
            case 5: rowsOf(gimexrows::To565, uint16_t{}); break;
            case 6: rowsOf(gimexrows::To1555, uint16_t{}); break;
            case 7: rowsOf(gimexrows::To4444, uint16_t{}); break;
            case 8:
            case 9: rowsOf(gimexrows::Copy32, uint32_t{}); break;
            default: result = false; break;
            }
        }
        buffer->Unlock(kLockForPixels);
        return result;
    }
    if (info->bitsPerPixel == 8) {
        switch (buffer->GetBitsPerPixel()) {
        case 8: return CopyIndices8(info, pixels, buffer);
        case 16: return CopyIndices16(info, pixels, buffer);
        case 32: return CopyIndices32(info, pixels, buffer);
        default: return false;
        }
    }
    return false;
}

bool cGZGimexFactory::CopyIndices8(const gimex::Info *info, const uint8_t *pixels, cIGZBuffer *buffer)
{
    const int32_t width = info->width;
    int32_t rows = info->height;
    const uint32_t stride = buffer->GetColorSurfaceStride();
    uint8_t *destination = static_cast<uint8_t *>(buffer->GetColorSurfaceBits());
    for (; rows > 0; --rows) {
        std::memcpy(destination, pixels, static_cast<size_t>(width));
        destination += stride;
        pixels += width;
    }
    return true;
}

bool cGZGimexFactory::CopyIndices16(const gimex::Info *info, const uint8_t *pixels, cIGZBuffer *buffer)
{
    int32_t cache[256];
    std::fill_n(cache, 256, -1);
    const int32_t width = info->width;
    int32_t rows = info->height;
    const uint32_t pixelStride = buffer->GetColorSurfaceStride() >> 1;
    const uint32_t format = buffer->GetNativeColorType().format;
    uint16_t *row = static_cast<uint16_t *>(buffer->GetColorSurfaceBits());
    for (; rows > 0; --rows) {
        uint16_t *destination = row;
        for (int32_t x = width; x > 0; --x) {
            const uint8_t index = *pixels++;
            if (cache[index] < 0) {
                const uint8_t *const colour = info->palette[index];
                switch (format) {
                case 4:
                case 6: cache[index] = gimexrows::Pack1555(colour[2], colour[1], colour[0]); break;
                case 5: cache[index] = gimexrows::Pack565(colour[2], colour[1], colour[0]); break;
                case 7: cache[index] = gimexrows::Pack4444(colour[2], colour[1], colour[0]); break;
                default: break; // stays -1: the pixel is 0xffff
                }
            }
            *destination++ = static_cast<uint16_t>(cache[index]);
        }
        row += pixelStride;
    }
    return true;
}

bool cGZGimexFactory::CopyIndices32(const gimex::Info *info, const uint8_t *pixels, cIGZBuffer *buffer)
{
    const int32_t width = info->width;
    int32_t rows = info->height;
    const uint32_t pixelStride = buffer->GetColorSurfaceStride() >> 2;
    buffer->GetNativeColorType(); // asked, not used
    uint32_t *row = static_cast<uint32_t *>(buffer->GetColorSurfaceBits());
    for (; rows > 0; --rows) {
        uint32_t *destination = row;
        for (int32_t x = width; x > 0; --x) {
            const uint8_t *const colour = info->palette[*pixels++];
            *destination++ = 0xff000000u | static_cast<uint32_t>(colour[2]) << 16 | static_cast<uint32_t>(colour[1]) << 8 |
                             colour[0];
        }
        row += pixelStride;
    }
    return true;
}

bool cGZGimexFactory::Dither(const gimex::Info *info, const uint8_t *pixels, cIGZBuffer *buffer)
{
    const int32_t width = info->width;
    int32_t rows = info->height;
    const int32_t sourceRow = BytesPerPixel(info) * width;
    uint8_t *destination = static_cast<uint8_t *>(buffer->GetColorSurfaceBits());
    const int32_t pixelStride = static_cast<int32_t>(buffer->GetColorSurfaceStride()) / 2;
    // Two error rows of width + 2 words with a guard word in front of the first: each pixel's error (four nibbles, one
    // per channel) spreads 7/16 ahead on its row and 3, 5 and 1 sixteenths onto the next row (Floyd-Steinberg, the
    // weights' sum left in each nibble's carry). The current row starts at the bias 8 in every channel.
    mDitherErrors.resize(static_cast<size_t>(2 * width + 4));
    uint32_t *current = mDitherErrors.data() + 1;
    uint32_t *next = current + (width + 2);
    std::fill_n(current, width, 0x48484848u);
    std::fill_n(next, width, 0u);
    int32_t start = 0;
    int32_t end = width;
    int32_t direction = 1;
    for (; rows > 0; --rows) {
        uint32_t error = pixels[start * 4] & 0xfu;
        current[start] += error * 7;
        next[start] += error;
        for (int32_t x = start; x != end; x += direction) {
            const uint32_t carried = current[x];
            const uint8_t *const source = pixels + x * 4;
            int32_t b = source[0] + static_cast<int32_t>(carried >> 4 & 0xf) - 8;
            int32_t g = source[1] + static_cast<int32_t>(carried >> 12 & 0xf) - 8;
            int32_t r = source[2] + static_cast<int32_t>(carried >> 20 & 0xf) - 8;
            int32_t a = static_cast<int32_t>(carried >> 28) + source[3] - 8;
            // Out of range: 0 below, all ones above (blue only its low byte).
            if (static_cast<uint32_t>(b) > 0xff) b = static_cast<uint8_t>(~(b >> 31));
            if (static_cast<uint32_t>(g) > 0xff) g = ~(g >> 31);
            if (static_cast<uint32_t>(r) > 0xff) r = ~(r >> 31);
            if (static_cast<uint32_t>(a) > 0xff) a = ~(a >> 31);
            reinterpret_cast<uint16_t *>(destination)[x] =
                static_cast<uint16_t>((a & 0xfff0) << 8 | (r & 0xf0) << 4 | (g & 0xf0) | (b >> 4));
            error = static_cast<uint32_t>((a & 0xf) << 24 | (r & 0xf) << 16 | (g & 0xf) << 8 | (b & 0xf));
            current[x + direction] += error * 7;
            next[x + direction] = error;
            next[x] += error * 5;
            next[x - direction] += error * 3;
        }
        next[end - direction] += error * 3;
        pixels += sourceRow;
        destination += 2 * pixelStride;
        std::swap(current, next);
        direction = -direction;
        const int32_t oldStart = start;
        start = end + direction;
        end = oldStart + direction;
    }
    return true;
}

bool cGZGimexFactory::WriteImage(cIGZGimexWriter *writer, uint32_t format, cIGZBuffer *buffer, const cGZRect *rect,
                                 uint32_t quality)
{
    LoadIjl();
    bool result = false;
    if (writer->Open(format, kWriteOpenFlag)) {
        // The codecs' full GINFO is kWriteInfoSize bytes; only its head is named.
        union {
            gimex::Info info;
            uint8_t bytes[gimex::kWriteInfoSize];
        } frame;
        std::memset(frame.bytes, 0, sizeof(frame.bytes));
        gimex::Info &info = frame.info;
        const int32_t width = rect->right - rect->left;
        const int32_t height = rect->bottom - rect->top;
        info.size = gimex::kWriteInfoSize;
        info.version = gimex::kWriteInfoVersion;
        info.width = width;
        info.height = height;
        info.bitsPerPixel = 32;
        info.sourceBits = 32;
        info.alphaBits = HasAlpha(buffer, false) ? 8 : 0;
        info.redBits = 8;
        info.greenBits = 8;
        info.blueBits = 8;
        info.quality = quality;
        info.imageBytes = static_cast<uint32_t>(height * width * 4);
        mPixels.resize(static_cast<size_t>(BytesPerPixel(&info) * height * width));
        std::fill(mPixels.begin(), mPixels.end(), uint8_t{0});
        ConvertFromBuffer(buffer, rect, &info, mPixels.data()); // its answer is not looked at
        result = writer->Write(&info, mPixels.data(), info.bitsPerPixel * info.width / 8);
    }
    writer->Close();
    writer->Release();
    return result;
}

bool cGZGimexFactory::ConvertFromBuffer(cIGZBuffer *buffer, const cGZRect *rect, const gimex::Info *info, uint8_t *pixels)
{
    if (!info || !pixels || !buffer) return false;
    const uint32_t width = static_cast<uint32_t>(info->width);
    const uint32_t stride = buffer->GetColorSurfaceStride();
    uint32_t rows = static_cast<uint32_t>(info->height);
    const int32_t destinationRow = BytesPerPixel(info) * info->width;
    const cGZColorType type = buffer->GetColorType();
    if (!buffer->Lock(kLockForPixels)) return false;
    bool result = true;
    const uint8_t *source = static_cast<const uint8_t *>(buffer->GetColorSurfaceBits()) +
                            (type.bits >> 3) * static_cast<uint32_t>(rect->left) +
                            static_cast<uint32_t>(rect->top) * stride;
    auto rowsOf = [&](auto convert, auto sourceType) {
        using Pixel = decltype(sourceType);
        for (; rows != 0; --rows) {
            convert(reinterpret_cast<const Pixel *>(source), reinterpret_cast<uint32_t *>(pixels), width);
            source += stride;
            pixels += destinationRow;
        }
    };
    switch (type.format) {
    case 4: rowsOf(gimexrows::From1555Opaque, uint16_t{}); break;
    case 5: rowsOf(gimexrows::From565, uint16_t{}); break;
    case 6: rowsOf(gimexrows::From1555, uint16_t{}); break;
    case 7: rowsOf(gimexrows::From4444, uint16_t{}); break;
    case 8:
    case 9: rowsOf(gimexrows::Copy32, uint32_t{}); break;
    default: result = false; break;
    }
    buffer->Unlock(kLockForPixels);
    return result;
}

bool cGZGimexFactory::HasAlpha(cIGZBuffer *buffer, bool scan)
{
    const uint32_t format = buffer->GetColorType().format;
    if (format != 6 && format != 7 && format != 9) return false;
    if (!scan) return true;
    if (!buffer->Lock(kLockForPixels)) return false;
    bool result = false;
    const uint32_t width = buffer->GetWidth();
    const uint32_t height = buffer->GetHeight();
    for (uint32_t y = 0; y < height && !result; ++y) {
        for (uint32_t x = 0; x < width && !result; ++x) {
            const uint32_t pixel = buffer->GetPixel(x, y);
            bool opaque;
            switch (format) {
            case 6: opaque = (pixel & 0x8000) != 0; break;
            case 7: opaque = (pixel & 0xf000) == 0xf000; break;
            default: opaque = (pixel >> 24 & 0xff) == 0xff; break;
            }
            if (!opaque) result = true;
        }
    }
    buffer->Unlock(kLockForPixels);
    return result;
}

void cGZGimexFactory::LoadIjl()
{
    auto *const module = GameAddress<HMODULE>(kIjlModuleCell);
    auto *const tried = GameAddress<uint8_t>(kIjlTriedCell);
    if (*module || *tried) return;
    *module = (*GameAddress<LoadLibraryFn>(kIatLoadLibraryA))(GameAddress<const char>(kIjlName));
    *tried = 1;
}

void *cGZGimexFactory::GetResourceManager()
{
    auto *const cell = GameAddress<void *>(kResourceManagerCell);
    if (!*cell) {
        // The getter 0x20db69 into a local reference, released by 0x1466e0 (try / catch(...)): the cell keeps no count.
        RZAutoRef ref{nullptr};
        if (void *const framework = GetServiceObject())
            As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kResourceManagerId, kResourceManagerIid, &ref.mObject);
        if (ref.mObject) *cell = ref.mObject;
        ref.ReleaseObject();
    }
    return *cell;
}

void *cGZGimexFactory::GetGraphicSystem()
{
    auto *const cell = GameAddress<void *>(kFactoryGraphicSystemCell);
    if (!*cell) {
        ScopedRef ref;
        AcquireGraphicSystem(&ref);
        if (ref.mObject) *cell = ref.mObject;
        ref.ReleaseObject();
    }
    return *cell;
}

// ---- the palette packers ----

namespace gimexrows {

uint16_t __cdecl Pack1555(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>((r & 0xf8) << 7 | (g & 0xf8) << 2 | b >> 3);
}

uint16_t __cdecl Pack565(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>((r & 0xf8) << 8 | (g & 0xfc) << 3 | b >> 3);
}

uint16_t __cdecl Pack4444(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>((r & 0xf0) << 4 | (g & 0xf0) | b >> 4 | 0xf000);
}

} // namespace gimexrows

// ---- adapters ----

cGZGimexFactory *__cdecl cGZGimexFactoryAdapters::Create()
{
    return new (std::nothrow) cGZGimexFactory;
}

cGZGimexFactory *__fastcall cGZGimexFactoryAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cGZGimexFactory;
}
