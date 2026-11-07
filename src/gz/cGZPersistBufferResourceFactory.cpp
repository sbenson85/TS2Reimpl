#include "gz/cGZPersistBufferResourceFactory.h"

#include "game/game.h"
#include "game/import.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZPersistBufferResource.h"
#include "gz/cGZPersistResource.h"
#include "gz/cIGZBuffer.h"
#include "gz/cIGZCOM.h"
#include "gz/cIGZPersistResource.h"
#include "gz/cIGZPersistResourceFactory.h"
#include "gz/cIGZString.h"
#include "gz/cRZCursor.h" // GetGraphicSystem
#include "ifaces/cIGZGraphicSystem.h"
#include "ifaces/cIGZPersistBufferResource.h"
#include "ifaces/cIGZPersistDBSerialRecord.h"
#include "rz/StaticGetters.h" // GetDirectorFrameworkObject
#include "rz/cRZString.h"

#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

namespace {

using Self = cGZPersistBufferResourceFactory;
using Record = ifaces::cIGZPersistDBSerialRecord;

// cIGZBuffer lock flags as these bodies pass them.
constexpr uint32_t kLockCopy = 0x8010;   // ReadRecord's blit
constexpr uint32_t kLockPixels = 0x8040; // the raw rows
constexpr uint32_t kLockSetPixel = 0x8080; // the old kind's pixel-by-pixel fill

// Buffer record kinds (the first uint32).
constexpr uint32_t kKindRaw = 1;
constexpr uint32_t kKindCompressed = 2;

// The pixel codec (try/catch wrappers; a caught exception gives false and a zero size).
bool Compress(const void *src, uint32_t size, void *dst, uint32_t *outSize)
{
    return OriginalFunction<bool(__stdcall *)(const void *, uint32_t, void *, uint32_t *), 0x9b6670>()(src, size, dst, outSize);
}
bool Decompress(const void *src, uint32_t size, void *dst, uint32_t dstSize, uint32_t *outSize)
{
    return OriginalFunction<bool(__stdcall *)(const void *, uint32_t, void *, uint32_t, uint32_t *), 0x9b6860>()(
        src, size, dst, dstSize, outSize);
}
// 0x9cdec0: the buffer's pixels brought to its colour format after a raw fill.
bool FinishPixels(cIGZBuffer *buffer, uint32_t format)
{
    return OriginalFunction<bool(__cdecl *)(cIGZBuffer *, uint32_t), 0x9cdec0>()(buffer, format);
}

// Bytes and bits per pixel of a buffer colour format as ReadBuffer sizes them.
uint32_t BytesPerPixel(uint32_t format)
{
    switch (format) {
    case 2: return 1;
    case 3: case 4: case 5: case 6: case 7: return 2;
    case 8: case 9: return 4;
    default: return 0;
    }
}
uint32_t BitsPerPixel(uint32_t format)
{
    switch (format) {
    case 2: return 8;
    case 3: case 4: case 5: case 6: case 7: return 16;
    case 8: case 9: return 32;
    default: return 0;
    }
}

uint32_t Word(const void *object, size_t offset)
{
    uint32_t value;
    std::memcpy(&value, static_cast<const uint8_t *>(object) + offset, 4);
    return value;
}


// A cRZString on the stack around a copy of the path, as NewBufferFromFile hands it to the BMP reader.
struct PathString {
    union {
        cRZString s;
    };
    PathString(const char *text, uint32_t len)
    {
        new (&s.mString) std::string(text, len);
        s.mVTable = GameAddress(cRZString::kVTable);
        s.mRefCount = 0;
    }
    ~PathString() { s.mString.~basic_string(); }
    PathString(const PathString &) = delete;
    PathString &operator=(const PathString &) = delete;
};

using LoadImageFn = HANDLE(WINAPI *)(HINSTANCE, LPCSTR, UINT, int, int, UINT);
using GetModuleHandleFn = HMODULE(WINAPI *)(LPCSTR);
using GetObjectFn = int(WINAPI *)(HANDLE, int, LPVOID);
using DeleteObjectFn = BOOL(WINAPI *)(HGDIOBJ);
void DeleteGdiObject(HANDLE object) { ImportFunction<DeleteObjectFn, 0xd9a0a8>()(object); }

} // namespace

// ---- primary face ----

bool Self::QueryInterface(uint32_t riid, void **out)
{
    if (riid == kIid) {
        *out = &mFace; // null-safe in the original (`this` is never null here)
        As<cIGZUnknown>(this)->AddRef();
        return true;
    }
    return cGZPersistResourceFactory::QueryInterface(riid, out);
}

bool Self::CreateInstanceByType(uint32_t type, uint32_t riid, void **out, uint32_t format, uint32_t)
{
    if (type != kResourceType) return false;
    const cGZColorType color(format & 0xffff);
    void *const memory = ::operator new(kResourceSize, std::nothrow);
    cIGZUnknown *const resource =
        memory ? static_cast<cGZPersistResource *>(::new (memory) cGZPersistBufferResource(color.format, color.bits)) : nullptr;
    // ORIGINAL-BUG: no null test: a failed allocation is asked for the interface anyway.
    return resource->QueryInterface(riid, out);
}

Self *Self::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, sizeof(*this));
    return this;
}

bool Self::Create(uint32_t type, uint32_t riid, void **out)
{
    if (type != kResourceType) return false;
    return As<cIGZPersistResourceFactory>(this)->CreateInstanceByType(kResourceType, riid, out, mFace.mFormat, 0);
}

bool Self::ReadRecord(cIGZUnknown *resource, Record *record)
{
    if (As<cIGZPersistResource>(resource)->GetType() != kResourceType) return false;
    auto *const bytes = reinterpret_cast<uint8_t *>(resource);
    auto *const target = static_cast<cIGZBuffer *>(As<ifaces::cIGZPersistBufferResource>(bytes + 0x1c)->GetBuffer());
    if (target == nullptr) return false;

    // A resource of the same colour type, on the stack, reads the file; its buffer is copied into the target's.
    cGZPersistBufferResource temp(Word(bytes, 0x28), Word(bytes, 0x2c));
    temp.cGZPersistResource::AddRef();
    bool ok = false;
    if (temp.ReadBmpRecord(record, 0) || (record->SeekAbsolute(0) && temp.ReadTgaRecord(record, 0))) {
        if (temp.mBuffer != nullptr) {
            temp.mBuffer->AddRef();
            if (cIGZBuffer *const source = temp.mBuffer) {
                const cGZColorType color = source->GetNativeColorType();
                const uint32_t height = source->GetHeight();
                const uint32_t width = source->GetWidth();
                if (target->Init(width, height, color.format, color.bits) && target->Lock(kLockCopy)) {
                    const cGZRect *const to = target->GetBoundsPtr();
                    const cGZRect *const from = source->GetBoundsPtr();
                    target->Blt(source, from, to, nullptr);
                    target->Unlock(kLockCopy);
                }
                ok = true; // whether or not the copy happened
                source->Release();
            }
        }
    }
    target->Release();
    return ok; // the stack resource's destructor runs here
}

// ---- cIGZPersistBufferResourceFactory face ----

bool Self::Face::NewBufferFromFile(cIGZString *path, void **out, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6)
{
    void *const memory = ::operator new(kResourceSize, std::nothrow);
    auto *const resource = memory ? ::new (memory) cGZPersistBufferResource(1, 0) : nullptr;
    bool ok;
    {
        const uint32_t len = path->Strlen();
        const char *const text = path->Data();
        PathString name(text, len);
        ok = resource->ReadBmpFile(name.s.AsInterface(),a3, a4, a5, a6); // ORIGINAL-BUG: a failed allocation is used anyway
    }
    if (!ok) return false; // ORIGINAL-BUG: the resource is never released (leaked, either way)
    return resource->QueryInterface(kBufferIid, out);
}

bool Self::Face::NewBufferFromBitmap(uint32_t id, cIGZBuffer **out, uint32_t, uint32_t format, uint32_t bits, uint32_t)
{
    HMODULE const module = ImportFunction<GetModuleHandleFn, 0xd9a114>()(nullptr);
    HANDLE const bitmap = ImportFunction<LoadImageFn, 0xd9a4c4>()(module, MAKEINTRESOURCEA(id & 0xffff), IMAGE_BITMAP, 0, 0,
                                                                    LR_CREATEDIBSECTION | LR_DEFAULTSIZE);
    if (bitmap == nullptr) return false;
    DIBSECTION section;
    if (ImportFunction<GetObjectFn, 0xd9a098>()(bitmap, sizeof(section), &section) == 0) {
        DeleteGdiObject(bitmap);
        return true; // ORIGINAL-BUG: a bitmap it cannot describe counts as loaded (no buffer made)
    }
    const int32_t width = section.dsBm.bmWidth;
    const int32_t height = section.dsBm.bmHeight;
    if (section.dsBm.bmBitsPixel != 24 || bits != 16 ||
        static_cast<uint8_t>(static_cast<cIGZCOM *>(GetDirectorFrameworkObject())
                                 ->GetClassObject(kBufferClsid, kBufferIid, reinterpret_cast<void **>(out))) == 0) {
        DeleteGdiObject(bitmap);
        return false;
    }
    (*out)->AddRef(); // the caller gets two references
    if (!(*out)->Init(width, height, format, bits)) {
        (*out)->Release();
        DeleteGdiObject(bitmap);
        return false;
    }
    cIGZBuffer *const buffer = *out;
    const uint8_t *src = static_cast<const uint8_t *>(section.dsBm.bmBits);
    const int32_t pad = width % 4; // 24-bit rows are padded to 4 bytes
    if (!buffer->Lock(kLockPixels)) {
        (*out)->Release();
        DeleteGdiObject(bitmap);
        return false;
    }
    // The DIB is bottom-up: its first row is the buffer's last.
    const uint32_t half = buffer->GetColorSurfaceStride() >> 1;
    uint8_t *row = static_cast<uint8_t *>(buffer->GetColorSurfaceBits());
    if (height > 0) {
        row += static_cast<uint32_t>(2 * height - 2) * half;
        for (int32_t y = height; y != 0; --y) {
            if (width > 0) {
                auto *dst = reinterpret_cast<uint16_t *>(row);
                for (int32_t x = width; x != 0; --x) {
                    const uint8_t blue = src[0], green = src[1], red = src[2];
                    src += 3;
                    *dst++ = static_cast<uint16_t>(buffer->ConvertRGBValueToNative(red, green, blue));
                }
            }
            row -= 2 * half;
            src += pad;
        }
    }
    buffer->Unlock(kLockPixels);
    DeleteGdiObject(bitmap);
    return true;
}

bool Self::Face::ReadBuffer(cIGZBuffer **out, Record *record, uint32_t)
{
    if (out == nullptr || record == nullptr) return false;
    uint32_t kind = 0, dataSize = 0, width = 0, height = 0, format = 0;
    cIGZBuffer *buffer = nullptr;
    bool ok = record->GetFieldUint32(&kind);
    if (!ok) return false;

    if (kind == kResourceType) {
        // The old kind: width * height uint32 pixels, set one at a time into a 16-bit buffer.
        if (!record->GetFieldUint32(&width) || !record->GetFieldUint32(&height)) return false;
        ok = GetGraphicSystem()->CreateBuffer(&buffer);
        void *const pixels = std::malloc(height * width * 4);
        if (ok) {
            ok = record->GetFieldUint32Array(pixels, height * width);
            if (ok) {
                ok = buffer->Init(width, height, 3, 16);
                if (ok && buffer->Lock(kLockSetPixel)) {
                    const auto *const values = static_cast<const uint32_t *>(pixels);
                    for (uint32_t y = 0; y < height; ++y)
                        for (uint32_t x = 0; x < width; ++x) buffer->SetPixel(x, y, values[y * width + x]);
                    buffer->Unlock(kLockSetPixel);
                }
            }
        }
        if (pixels != nullptr) std::free(pixels);
    } else if (kind == kKindRaw || kind == kKindCompressed) {
        uint32_t bpp = 0;
        ok = record->GetFieldUint32(&dataSize) && record->GetFieldUint32(&width) && record->GetFieldUint32(&height);
        if (ok) {
            ok = record->GetFieldSint32(&format);
            bpp = BytesPerPixel(format);
            if (ok) {
                ok = GetGraphicSystem()->CreateBuffer(&buffer);
                if (ok) ok = buffer->Init(width, height, format, BitsPerPixel(format));
            }
        }
        // ORIGINAL-BUG: a read that fails before the buffer exists reaches here with none and calls its slot 35
        // through null.
        if (buffer == nullptr || !buffer->Lock(kLockPixels)) {
            buffer->GetColorSurfaceStride();
            ok = false;
        } else {
            const uint32_t stride = buffer->GetColorSurfaceStride();
            const uint32_t rowBytes = bpp * width;
            const uint32_t total = bpp * height * width;
            if (ok) {
                void *packed = nullptr; // the record's bytes when they cannot go straight into the surface
                void *rows = nullptr;   // decompressed rows to repack
                if (kind == kKindCompressed) {
                    packed = std::malloc(dataSize);
                    ok = record->GetFieldVoid(packed, dataSize);
                    if (ok) {
                        uint8_t *bits = static_cast<uint8_t *>(buffer->GetColorSurfaceBits());
                        if (stride != rowBytes) rows = std::malloc(total);
                        uint32_t produced = total;
                        ok = Decompress(packed, dataSize, stride != rowBytes ? rows : bits, total, &produced);
                        if (produced != total) {
                            ok = false;
                        } else if (ok) {
                            if (stride != rowBytes) {
                                const uint8_t *from = static_cast<const uint8_t *>(rows);
                                for (uint32_t y = 0; y < height; ++y) {
                                    std::memcpy(bits, from, rowBytes);
                                    bits += stride;
                                    from += rowBytes;
                                }
                            }
                            buffer->Unlock(kLockPixels);
                            ok = FinishPixels(buffer, format);
                        }
                    }
                } else {
                    uint8_t *bits = static_cast<uint8_t *>(buffer->GetColorSurfaceBits());
                    if (stride != rowBytes) packed = std::malloc(dataSize);
                    ok = record->GetFieldVoid(stride != rowBytes ? packed : bits, dataSize);
                    if (ok) {
                        if (stride != rowBytes) {
                            // ORIGINAL-BUG: the strides are swapped: the packed source advances by the surface's
                            // stride and the surface by the packed row size.
                            const uint8_t *from = static_cast<const uint8_t *>(packed);
                            for (uint32_t y = 0; y < height; ++y) {
                                std::memcpy(bits, from, rowBytes);
                                from += stride;
                                bits += rowBytes;
                            }
                        }
                        buffer->Unlock(kLockPixels);
                        ok = FinishPixels(buffer, format);
                    }
                }
                if (rows != nullptr) std::free(rows);
                if (packed != nullptr) std::free(packed);
            }
        }
    }
    // Any other kind: true with no buffer.
    if (ok) {
        *out = buffer;
        return true;
    }
    if (buffer != nullptr) buffer->Release();
    return false;
}

bool Self::Face::WriteBuffer(cIGZBuffer *buffer, Record *record, bool compress)
{
    if (buffer == nullptr || record == nullptr || !buffer->Lock(kLockPixels)) return false;
    const uint32_t width = buffer->GetWidth();
    const uint32_t height = buffer->GetHeight();
    const uint32_t bpp = buffer->GetBytesPerPixel();
    const uint32_t stride = buffer->GetColorSurfaceStride();
    const uint8_t *bits = static_cast<const uint8_t *>(buffer->GetColorSurfaceBits());
    const uint32_t total = bpp * height * width;
    const uint32_t format = buffer->GetNativeColorType().format;
    const uint32_t rowBytes = bpp * width;

    // The rows packed (the surface itself when it has no padding).
    void *packed = nullptr;
    const void *data = bits;
    if (stride != rowBytes) {
        packed = std::malloc(total);
        data = packed;
        auto *to = static_cast<uint8_t *>(packed);
        for (uint32_t y = height; y != 0; --y) {
            std::memcpy(to, bits, rowBytes);
            bits += stride;
            to += rowBytes;
        }
    }
    void *compressed = nullptr;
    uint32_t size = total;
    bool ok;
    if (compress) {
        compressed = std::malloc(total * 15u / 10u + 0x400u);
        const bool done = Compress(data, total, compressed, &size);
        if (done) data = compressed;
        // A failed compression writes the raw kind with the size it left (zero).
        ok = record->SetFieldUint32(done ? kKindCompressed : kKindRaw);
    } else {
        ok = record->SetFieldUint32(kKindRaw);
    }
    ok = ok && record->SetFieldUint32(size) && record->SetFieldUint32(width) && record->SetFieldUint32(height) &&
         record->SetFieldSint32(static_cast<int32_t>(format)) && record->SetFieldVoid(const_cast<void *>(data), size);
    buffer->Unlock(kLockPixels);
    if (packed != nullptr) std::free(packed);
    if (compressed != nullptr) std::free(compressed);
    return ok;
}

void Self::Face::SetColorType(uint32_t format, uint32_t bits)
{
    mFormat = format;
    mBits = bits;
}
