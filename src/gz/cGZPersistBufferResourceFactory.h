#pragma once
#include "gz/cGZPersistResourceFactory.h"

#include <cstddef>
#include <cstdint>

class cIGZBuffer;
class cIGZString;
class cIGZUnknown;
namespace ifaces {
class cIGZPersistDBSerialRecord;
} // namespace ifaces

// cGZPersistBufferResourceFactory: the persist factory of image resources (record type 0x856ddbac, cGZPersistBufferResource,
// 0x3c bytes), 0x1c bytes. Base cGZPersistResourceFactory (0x10 bytes, ours), then the cIGZPersistBufferResourceFactory face
// at +0x10 (iid 0x656ddc26, vtable 0xe604c8) with a default colour type after it. Primary vtable 0xe60494. Hook-shaped like
// its base: non-virtual members hooked at the game's slot bodies, `this` as the game passes it (the face's members take the
// face). RVAs 1.18.0.187. Lane NP.
//
// The resource (cGZPersistBufferResource, still the game's): +0 cGZPersistResource (slot 5 the record type), +0x1c its
// cIGZPersistBufferResource face (slot 3 GetBuffer, AddRef'd), +0x20 its cIGZBuffer, +0x28 / +0x2c the colour type it was
// built with. Built by the game's constructor 0x9b64a0 (this, format, bits) and destroyed by 0x9b6590.
//
// Records: an image record is a Windows BMP or a TGA file (ReadRecord tries the BMP reader 0x9b6930, rewinds, then the TGA
// reader 0x9b77b0). A buffer written by WriteBuffer: uint32 kind (1 raw, 2 compressed), uint32 data size, uint32 width,
// uint32 height, sint32 colour format, then the pixel rows packed (compressed: through 0x9b6670, read back by 0x9b6860).
// ReadBuffer also reads the older kind 0x856ddbac: uint32 width, height, then width * height uint32 pixels set one by one.
class cGZPersistBufferResourceFactory : public cGZPersistResourceFactory {
public:
    static constexpr uint32_t kVTable = 0xe60494;
    static constexpr uint32_t kFaceVTable = 0xe604c8;
    static constexpr uint32_t kIid = 0x656ddc26;          // cIGZPersistBufferResourceFactory
    static constexpr uint32_t kResourceType = 0x856ddbac; // image records
    static constexpr size_t kResourceSize = 0x3c;         // cGZPersistBufferResource
    static constexpr uint32_t kBufferIid = 0x20732180;    // cIGZBuffer
    static constexpr uint32_t kBufferClsid = 0xc470d325;  // the GZCOM class of a plain cGZBuffer

    // The cIGZPersistBufferResourceFactory face (+0x10). `this` is the face.
    struct Face {
        // slot 3, 0x9b6f60 (ret 0x18): a new resource in colour type {1, 0} filled from the BMP file at `path`
        // (0x9b6c30, given a3..a6), then asked for its cIGZBuffer into `out`.
        bool NewBufferFromFile(cIGZString *path, void **out, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6);
        // slot 5, 0x9b7060 (ret 0x18): a 24-bit bitmap resource of the exe (LoadImageA, id = low word of `id`) into a new
        // cGZBuffer of colour type {format, bits}; only bits 16 is supported (each pixel through slot 30).
        bool NewBufferFromBitmap(uint32_t id, cIGZBuffer **out, uint32_t a3, uint32_t format, uint32_t bits, uint32_t a6);
        // slot 6, 0x9b7280 (ret 0xc): a buffer from `record` (the format above) into a new cGZBuffer.
        bool ReadBuffer(cIGZBuffer **out, ifaces::cIGZPersistDBSerialRecord *record, uint32_t a3);
        // slot 7, 0x9b7d40 (ret 0xc): `buffer` to `record`, compressed when `compress`.
        bool WriteBuffer(cIGZBuffer *buffer, ifaces::cIGZPersistDBSerialRecord *record, bool compress);
        // slot 8, 0x9b8120 (ret 8): the colour type slot 9 creates resources in.
        void SetColorType(uint32_t format, uint32_t bits);

        const void *mVTable; // +0x10
        uint32_t mFormat;    // +0x14 (CreateBuffer's format for slot 9)
        uint32_t mBits;      // +0x18 (stored, never read in the class)
    };

    bool QueryInterface(uint32_t riid, void **out);                                                      // slot 0, 0x9b7d00
    bool CreateInstanceByType(uint32_t type, uint32_t riid, void **out, uint32_t format, uint32_t a5);   // slot 4, 0x9b6740
    cGZPersistBufferResourceFactory *DeletingDestructor(uint32_t flags);                                 // slot 7, 0x9b6650
    bool Create(uint32_t type, uint32_t riid, void **out);                                               // slot 9, 0x9b6710
    bool ReadRecord(cIGZUnknown *resource, ifaces::cIGZPersistDBSerialRecord *record);                   // slot 10, 0x9b7fa0

    Face mFace; // +0x10
};

static_assert(sizeof(cGZPersistBufferResourceFactory) == 0x1c, "cGZPersistBufferResourceFactory layout");
static_assert(offsetof(cGZPersistBufferResourceFactory, mFace) == 0x10, "cGZPersistBufferResourceFactory layout");
