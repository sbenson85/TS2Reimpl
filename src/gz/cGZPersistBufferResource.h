#pragma once
#include "gz/cGZPersistResource.h"
#include "gz/cIGZBuffer.h"
#include "gz/cIGZPersistBufferResource.h"
#include "gz/cIGZString.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ifaces {
class cIGZPersistDBSerialRecord;
} // namespace ifaces

// A BMP info header and the colour table after it, as the readers keep them: the 0x28-byte BITMAPINFOHEADER, then the
// palette entries (4 bytes each: blue, green, red, unused). The readers keep 0x428 bytes: 256 entries.
struct BmpInfo {
    uint32_t size;        // +0x00
    int32_t width;        // +0x04
    int32_t height;       // +0x08 (positive: rows bottom-up)
    uint16_t planes;      // +0x0c
    uint16_t bitCount;    // +0x0e
    uint32_t compression; // +0x10 (0 raw, 1 RLE8; the support check lets only these two through)
    uint32_t sizeImage;   // +0x14
    uint32_t xPelsPerMeter;
    uint32_t yPelsPerMeter;
    uint32_t colorsUsed;      // +0x20
    uint32_t colorsImportant; // +0x24
    uint8_t palette[0x400];   // +0x28
};
static_assert(offsetof(BmpInfo, bitCount) == 0xe);
static_assert(offsetof(BmpInfo, compression) == 0x10);
static_assert(offsetof(BmpInfo, colorsUsed) == 0x20);
static_assert(offsetof(BmpInfo, palette) == 0x28);
static_assert(sizeof(BmpInfo) == 0x428);

// The persist resource of an image (record type 0x856ddbac), 0x3c bytes, /O2 (module 0x9b5fd0-0x9b8e00). Lane NU (standalone
// shape, docs/standalone-shape.md). RVAs 1.18.0.187.
//
// Faces (RTTI order): +0 cGZPersistResource (vtable 0xe60434, 16 slots: 0 QI 0x9b7c80, 1 AddRef 0x5522b0 and 2 Release
// 0x556090 (folded, jumps to cGZPersistResource's), 3-11 the base's folded accessors, 12 the deleting destructor 0x9b6630,
// 13 / 15 the folded "return false" 0x6ea850 (one argument), 14 0x9b67b0), +0x1c cIGZPersistBufferResource (vtable
// 0xe60478: 0 QI thunk 0x9b7cf8, 1 / 2 thunks 0x5522b5 / 0x556095 (folded), 3 GetBuffer 0x9b6910, 4 QueryBuffer 0x9b68f0,
// 5 SetBuffer 0x9b80f0). The vtables are referenced by the constructor 0x9b64a0 and the destructor 0x9b6590 alone.
//
// The resource holds a cIGZBuffer (+0x20, a GZCOM object of class 0xc470d326, else the plain 0xc470d325) aggregated into
// itself: the buffer's cIGZAggregate face (+0x24, iid 0x12f) is given the resource's +0x1c face as its outer unknown, so
// the buffer's reference counting and QueryInterface answer for the resource, and the resource's QueryInterface falls
// through to the aggregate's, else the buffer's. The constructor's (format, bits) colour type (+0x28 / +0x2c) is the one
// the readers build the buffer in. +0x30 is a vector of uint32 the dithering readers keep their error rows in.
//
// The readers (non-virtual; all `this` in ecx): ReadBmpRecord 0x9b6930 and ReadTgaRecord 0x9b77b0 parse an image record
// (the factory's ReadRecord tries the BMP, rewinds, then the TGA); ReadBmpFile 0x9b6c30 a BMP file. Each makes the buffer
// (Init) in the colour type, locks it, fills it and unlocks: the BMP readers through ConvertBitmap 0x9b8140, the TGA
// reader through the pixel converter (0x9cd6a0 / 0x9cd830 -> 0x9cea90 / 0x9cd890, another class's code: original calls).
// The 24 bit BMP and the 32 bit TGA into the 16 bit format 7 (ARGB4444) buffer of a colour type 1 resource are dithered
// (0x9b88e0 / 0x9b8b40: Floyd-Steinberg with a serpentine scan over the +0x30 rows).
class cGZPersistBufferResource : public cGZPersistResource, public cIGZPersistBufferResource {
public:
    static constexpr uint32_t kBufferClsidAggregated = 0xc470d326; // tried first
    static constexpr uint32_t kBufferClsid = 0xc470d325;           // the fallback: not aggregated
    static constexpr uint32_t kBufferIid = 0x20732180;             // cIGZBuffer
    static constexpr uint32_t kLabelRva = 0xe604ec;                // the 13 characters slot 14 hands out (read at run time)
    static constexpr uint32_t kLabelLength = 13;
    static constexpr uint32_t kLockBmp = 0x8040;                   // the cIGZBuffer lock flags the readers pass
    static constexpr uint32_t kLockTga = 0x8080;

    cGZPersistBufferResource(uint32_t format, uint32_t bits); // 0x9b64a0
    // 0x9b6590 (slot 12 is the compiler's scalar deleting destructor around it, 0x9b6630: delete 0x3c on bit 0): the
    // aggregate's outer is cleared and the aggregate released, the buffer released, the vector freed, then the base's.
    ~cGZPersistBufferResource() override;
    cGZPersistBufferResource(const cGZPersistBufferResource &) = delete;
    cGZPersistBufferResource &operator=(const cGZPersistBufferResource &) = delete;

    // slot 0 (0x9b7c80; the face's thunk 0x9b7cf8): the face (0x06f1568e) counted through slot 1; then cGZPersistResource's
    // answers (1 and 0x456b8f1d: this); then the aggregate's slot 0, else the buffer's slot 0 (false with neither).
    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t AddRef() override;  // slot 1 (folded 0x5522b0; the face's thunk 0x5522b5 folded): cGZPersistResource's
    uint32_t Release() override; // slot 2 (folded 0x556090; the face's thunk 0x556095 folded): cGZPersistResource's

    // slots 13 to 15 (the class's own virtuals; the base has 13 slots).
    virtual bool Slot13(void *);                  // 0x6ea850 (folded, a jump to 0x18a7a0 `xor al, al; ret 4`): false
    virtual bool GetDescription(cIGZString *out); // 0x9b67b0 (ret 4): out->Copy of the 13 character label; true
    virtual bool Slot15(void *);                  // 0x6ea850 (folded): false

    // The face's slots (the compiler's thunks put `this` back).
    cIGZBuffer *GetBuffer() override;                    // 0x9b6910: AddRef'd (the buffer's, through the outer), or null
    bool QueryBuffer(uint32_t iid, void **out) override; // 0x9b68f0: the buffer's QueryInterface (a tail call)
    void SetBuffer(cIGZBuffer *buffer) override;         // 0x9b80f0

    // ---- the readers (non-virtual) ----
    // 0x9b6930 (ret 8): a BMP file inside the record, from its current position. The second argument is never read.
    bool ReadBmpRecord(ifaces::cIGZPersistDBSerialRecord *record, uint32_t unused);
    // 0x9b77b0 (ret 8): a TGA file inside the record (seeks to its start first). Uncompressed (2) or RLE (10) true colour
    // only; the second argument is never read.
    bool ReadTgaRecord(ifaces::cIGZPersistDBSerialRecord *record, uint32_t unused);
    // 0x9b6c30 (ret 0x14): the BMP file at `path`, the buffer made in {format, bits} (the 3rd and 4th arguments, which are
    // the colour type of the caller; the resource's own +0x28 / +0x2c are not used). The 2nd and 5th are never read.
    bool ReadBmpFile(cIGZString *path, uint32_t unused1, uint32_t format, uint32_t bits, uint32_t unused2);

    // 0x9b8db0 (ret 4): compression 0 or 1 and 4, 8 or 24 bits per pixel.
    bool IsSupportedBitmap(const BmpInfo *info);
    // 0x9b8140 (ret 8): the pixel rows of a bottom-up BMP (padded to 4 bytes) into the locked buffer, converted to its
    // bits per pixel (4 bit to 16; 8 bit to 8, 16 or 32; 24 bit to 16, 24 or 32; anything else false).
    bool ConvertBitmap(const BmpInfo *info, const uint8_t *pixels);
    // 0x9b88e0 (ret 8): 24 bit rows (BMP padding) to the 16 bit ARGB4444 buffer, error-diffused.
    bool DitherRgb24(const BmpInfo *info, const uint8_t *pixels);
    // 0x9b8b40 (ret 0x10): `height` rows of 32 bit B, G, R, A pixels, `stride` bytes apart (negative: upwards), the same.
    bool DitherArgb32(int32_t width, int32_t height, const uint8_t *pixels, int32_t stride);

    // +0x00 cGZPersistResource (0x1c bytes), +0x1c the face's vtable
    cIGZBuffer *mBuffer = nullptr;       // +0x20
    cIGZAggregate *mAggregate = nullptr; // +0x24 the buffer's aggregate face, outer = the +0x1c face (null: not aggregated)
    uint32_t mFormat;                    // +0x28 the colour type the record readers build the buffer in
    uint32_t mBits;                      // +0x2c
    std::vector<uint32_t> mErrorRows;    // +0x30 the dithering readers' error rows (2 * width + 4 words)
};
static_assert(sizeof(cGZPersistBufferResource) == 0x3c);
static_assert(offsetof(cGZPersistBufferResource, mBuffer) == 0x20);
static_assert(offsetof(cGZPersistBufferResource, mAggregate) == 0x24);
static_assert(offsetof(cGZPersistBufferResource, mFormat) == 0x28);
static_assert(offsetof(cGZPersistBufferResource, mBits) == 0x2c);
static_assert(offsetof(cGZPersistBufferResource, mErrorRows) == 0x30);

// The RVA hooks' entry points (src/hook/tables/cGZPersistBufferResource.cpp): the constructor for the game's creation site
// (the Gimex factory's, 0x87bfc7) and the destructor for direct calls. Original calling convention (this in ecx), our
// members called without virtual dispatch, bodies inlined.
struct cGZPersistBufferResourceAdapters {
    using Self = cGZPersistBufferResource;
    static Self *__fastcall Construct(void *memory, void *, uint32_t format, uint32_t bits); // 0x9b64a0 (ret 8)
    static void __fastcall Destruct(Self *self, void *);                                      // 0x9b6590
};
