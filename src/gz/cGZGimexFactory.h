#pragma once
// cGZGimexFactory (lane LX, standalone shape): the image loader and saver over the Gimex reader and writer
// (src/gz/cGZGimexReader.h). It reads an image (a file, a stream, a database record or a resource key) into a cIGZBuffer
// in a given colour format, and writes a buffer (or a rectangle of it) out as an image file of a given Gimex format; as
// the persist resource factory of image resources (type 0x856ddbac) it makes cGZPersistBufferResources and has them
// read (any format, probed) and written (PNG) through the same paths. RVAs 1.18.0.187.
//
// 0x38 bytes. Faces (RTTI order): +0 cIGZGimexFactory (vtable 0xe4a464, 21 slots), +4 cIGZPersistResourceFactory
// (vtable 0xe4a4bc, 12 slots; with the base cGZPersistResourceFactory's reference count +8 and lock +0xc, both left to
// the base's own slots). +0x14 the reference count, +0x18 the colour format resources are made in (1: the display's),
// +0x1c dither (on), +0x20 the pixel scratch buffer, +0x2c the dither's error rows.
//
// Creation: only the cGXCOMDirector's creator 0x87b412 (new 0x38, then the constructor 0x87bcdc) builds it; creator and
// constructor are hooked to ours, and the vtables are referenced by nothing else, so every slot is ours through our
// vtables. Folded bodies: slot 1 (AddRef, 0x192140), the +4 face's AddRef thunk (0x4805dd) and slot 8 (0x1f62f0) are
// shared across unrelated classes: implemented here, never hooked.
//
// Statics it keeps in the game's cells (no one else reads them): the resource manager (0x10e6924) and the graphics
// system (0x10e6928), both cached uncounted on first use, and the Intel JPEG library handle (0x10e6930, loaded once on
// the first image read or written; the flag 0x10e692c says it was tried). The game's exit handler 0x87d6e2 frees it.
#include "gz/Gimex.h"
#include "gz/cIGZBuffer.h"
#include "gz/cIGZPersistResourceFactory.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZCriticalSection.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class cIGZGimexReader;
class cIGZGimexWriter;
class cIGZPersistResource;
class cIGZString;
struct cGZPersistResourceKey;
namespace ifaces {
class cIGZFile;
class cIGZPersistDBSegment;
} // namespace ifaces

// The factory's interface (iid 0x2ffbb60). `format` is a Gimex format (a codec row, or gimex::kProbe /
// kByExtension / kByExtensionOrProbe); `colorFormat` the buffer's (cGZColorType); `quality` the writer's (JPEG).
// `record` is a cIGZPersistDBSerialRecord. Reads into `buffer` (re)initialise it to the image's size; the New* slots
// first make a buffer (released and nulled again on failure).
class cIGZGimexFactory : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x2ffbb60;
    virtual bool ReadStream(ifaces::cIGZFile *stream, uint32_t format, cIGZBuffer *buffer, uint32_t colorFormat) = 0; // 3
    virtual bool NewFromFile(const cIGZString &path, cIGZBuffer **out, uint32_t colorFormat) = 0;                  // 4
    virtual bool ReadFile(const cIGZString &path, cIGZBuffer *buffer, uint32_t colorFormat) = 0;                    // 5
    virtual bool WriteStream(ifaces::cIGZFile *stream, uint32_t format, cIGZBuffer *buffer, uint32_t quality) = 0;  // 6
    virtual bool WriteStreamRect(ifaces::cIGZFile *stream, uint32_t format, cIGZBuffer *buffer, const cGZRect *rect,
                                 uint32_t quality) = 0;                                                             // 7
    virtual bool WriteFile(const cIGZString &path, cIGZBuffer *buffer, uint32_t quality) = 0;                       // 8
    virtual bool WriteFileRect(const cIGZString &path, cIGZBuffer *buffer, const cGZRect *rect, uint32_t quality) = 0; // 9
    virtual bool IsReadable(const cIGZString &path) = 0;                                                            // 10
    // size[0] / size[1]: the first frame's width and height (untouched on failure).
    virtual bool GetImageSize(const cIGZString &path, uint32_t *size) = 0;                                          // 11
    virtual bool NewFromStream(ifaces::cIGZFile *stream, uint32_t format, cIGZBuffer **out, uint32_t colorFormat) = 0; // 12
    virtual bool ReadKey(const cGZPersistResourceKey *key, uint32_t format, cIGZBuffer *buffer, uint32_t colorFormat) = 0; // 13
    virtual bool NewFromKey(const cGZPersistResourceKey *key, uint32_t format, cIGZBuffer **out, uint32_t colorFormat) = 0; // 14
    virtual bool ReadDBRecord(cIGZUnknown *record, uint32_t format, cIGZBuffer *buffer, uint32_t colorFormat) = 0;  // 15
    virtual bool NewFromDBRecord(cIGZUnknown *record, uint32_t format, cIGZBuffer **out, uint32_t colorFormat) = 0; // 16
    virtual bool WriteKey(const cGZPersistResourceKey *key, ifaces::cIGZPersistDBSegment *segment, uint32_t format,
                          cIGZBuffer *buffer, uint32_t quality) = 0;                                                // 17
    // To `segment` when given (its OpenRecord, mode 2), else through the resource manager (OpenRecord for writing).
    virtual bool WriteKeyRect(const cGZPersistResourceKey *key, ifaces::cIGZPersistDBSegment *segment, uint32_t format,
                              cIGZBuffer *buffer, const cGZRect *rect, uint32_t quality) = 0;                       // 18
    virtual bool WriteDBRecord(cIGZUnknown *record, uint32_t format, cIGZBuffer *buffer, uint32_t quality) = 0;    // 19
    virtual bool WriteDBRecordRect(cIGZUnknown *record, uint32_t format, cIGZBuffer *buffer, const cGZRect *rect,
                                   uint32_t quality) = 0;                                                           // 20
};

// The +4 face: cIGZPersistResourceFactory as cGZPersistResourceFactory lays it out (its reference count and lock
// follow the vtable), with the factory slots 7-11.
class cGZGimexResourceFactory : public cIGZPersistResourceFactory {
public:
    // Slot 7 (the class's deleting destructor). The base part's teardown: the lock destroyed (0x9b6040's work).
    virtual ~cGZGimexResourceFactory();
    virtual bool CreateInstance3(ifaces::cIGZPersistDBRecord *record, uint32_t riid, void **out) = 0; // slot 8
    virtual bool CreateResource(uint32_t type, uint32_t riid, void **out) = 0;                     // slot 9
    virtual bool ReadRecord(cIGZPersistResource *resource, cIGZUnknown *record) = 0;               // slot 10
    virtual bool WriteRecord(cIGZPersistResource *resource, cIGZUnknown *record) = 0;              // slot 11

    uint32_t mBaseRefCount = 0;  // +0x04 of the face (the base's; the factory counts in its own)
    cRZCriticalSection mLock;    // +0x08 of the face (the base's)

protected:
    // The base part as 0x9b6010 builds it: count 0, the lock constructed.
    cGZGimexResourceFactory();

    cGZGimexResourceFactory(const cGZGimexResourceFactory &) = delete;
    cGZGimexResourceFactory &operator=(const cGZGimexResourceFactory &) = delete;
};

class cGZGimexFactory : public cIGZGimexFactory, public cGZGimexResourceFactory {
public:
    static constexpr uint32_t kSize = 0x38;
    static constexpr uint32_t kClassId = 0x2ffbb60;        // the director registers it under its iid
    static constexpr uint32_t kResourceType = 0x856ddbac;  // image resources (cGZPersistBufferResource)
    static constexpr uint32_t kResourceSize = 0x3c;        // a cGZPersistBufferResource
    static constexpr uint32_t kSerialRecordIid = 0xc56b8f08; // cIGZPersistDBSerialRecord
    static constexpr uint32_t kBufferClass = 0xc470d325;   // cGZBuffer (framework class)
    static constexpr uint32_t kResourceWriteFormat = gimex::kFormatPng; // WriteRecord's format
    static constexpr uint32_t kResourceWriteQuality = 100;
    // cIGZBuffer::Lock / Unlock flags: around the whole copy of a read, and around each pixel access.
    static constexpr uint32_t kLockForInit = 0x8080;
    static constexpr uint32_t kLockForPixels = 0x8040;

    cGZGimexFactory();   // 0x87bcdc
    ~cGZGimexFactory() override;  // 0x87bd22; +4 slot 7 the deleting destructor (0x87bd55, thunk 0x87bd4d)

    cGZGimexFactory(const cGZGimexFactory &) = delete;
    cGZGimexFactory &operator=(const cGZGimexFactory &) = delete;

    // ---- cIGZUnknown (both faces; +4 thunks 0x87c8cd, folded 0x4805dd, 0x87c8ef) ----
    // 0x87c8a2: 1 and the factory's iid answer the +0 face; anything else goes to the base's QueryInterface on +4.
    bool QueryInterface(uint32_t riid, void **ppv) override;
    uint32_t AddRef() override;   // folded 0x192140
    // 0x87c8d5: above 1, the count less one; else the +4 face's deleting destructor and 0.
    uint32_t Release() override;

    // ---- cIGZGimexFactory ----
    // 0x87c26d: a reader (framework class 0x2a316c02) on the stream, then ReadImage. False when the reader cannot take
    // the stream (released) or none was made.
    bool ReadStream(ifaces::cIGZFile *stream, uint32_t format, cIGZBuffer *buffer, uint32_t colorFormat) override;
    bool NewFromFile(const cIGZString &path, cIGZBuffer **out, uint32_t colorFormat) override;     // 0x87c1b3
    // 0x87c1f1: a new cRZFile on `path` opened for reading, ReadStream with kByExtensionOrProbe, closed if still open.
    // ORIGINAL-BUG: a failed allocation of the file is used anyway (Open on null).
    bool ReadFile(const cIGZString &path, cIGZBuffer *buffer, uint32_t colorFormat) override;
    bool WriteStream(ifaces::cIGZFile *stream, uint32_t format, cIGZBuffer *buffer, uint32_t quality) override; // 0x87c9f9
    // 0x87c9a2: a writer (framework class 0xca316c55) on the stream, then WriteImage. ORIGINAL-BUG: a writer that
    // cannot take the stream is never released.
    bool WriteStreamRect(ifaces::cIGZFile *stream, uint32_t format, cIGZBuffer *buffer, const cGZRect *rect,
                         uint32_t quality) override;
    bool WriteFile(const cIGZString &path, cIGZBuffer *buffer, uint32_t quality) override;         // 0x87c964
    // 0x87c8f7: a new cRZFile opened for writing (creating it), WriteStreamRect with kByExtension, closed.
    // ORIGINAL-BUG: a failed allocation of the file is used anyway (AddRef on null).
    bool WriteFileRect(const cIGZString &path, cIGZBuffer *buffer, const cGZRect *rect, uint32_t quality) override;
    bool IsReadable(const cIGZString &path) override;                                              // 0x87c16c
    // 0x87c045: opens the file, a reader on it, the IJL library loaded, opens it (kByExtensionOrProbe) and reads frame
    // 0's description. True when it has one. ORIGINAL-BUG: as ReadFile, a failed allocation of the file is used.
    bool GetImageSize(const cIGZString &path, uint32_t *size) override;
    bool NewFromStream(ifaces::cIGZFile *stream, uint32_t format, cIGZBuffer **out, uint32_t colorFormat) override; // 0x87beab
    // 0x87c2c9: the resource manager's record for `key` (read), as a serial record, ReadDBRecord; the record closed and
    // released.
    bool ReadKey(const cGZPersistResourceKey *key, uint32_t format, cIGZBuffer *buffer, uint32_t colorFormat) override;
    bool NewFromKey(const cGZPersistResourceKey *key, uint32_t format, cIGZBuffer **out, uint32_t colorFormat) override; // 0x87beed
    // 0x87c33c: a reader on the record (its SetRecord), then ReadImage; a reader that cannot take it is released.
    bool ReadDBRecord(cIGZUnknown *record, uint32_t format, cIGZBuffer *buffer, uint32_t colorFormat) override;
    bool NewFromDBRecord(cIGZUnknown *record, uint32_t format, cIGZBuffer **out, uint32_t colorFormat) override; // 0x87bf60
    bool WriteKey(const cGZPersistResourceKey *key, ifaces::cIGZPersistDBSegment *segment, uint32_t format,
                  cIGZBuffer *buffer, uint32_t quality) override;                                  // 0x87cace
    bool WriteKeyRect(const cGZPersistResourceKey *key, ifaces::cIGZPersistDBSegment *segment, uint32_t format,
                      cIGZBuffer *buffer, const cGZRect *rect, uint32_t quality) override;         // 0x87ca3b
    bool WriteDBRecord(cIGZUnknown *record, uint32_t format, cIGZBuffer *buffer, uint32_t quality) override; // 0x87cb6b
    // 0x87cb14. ORIGINAL-BUG: as WriteStreamRect, a writer that cannot take the record is never released.
    bool WriteDBRecordRect(cIGZUnknown *record, uint32_t format, cIGZBuffer *buffer, const cGZRect *rect,
                           uint32_t quality) override;

    // ---- cIGZPersistResourceFactory (+4) ----
    bool CreateInstance(ifaces::cIGZPersistDBRecord *record, uint32_t riid, void **out, uint32_t a4,
                        uint32_t a5) override;                                 // slot 3, the base's 0x9b60e0
    // Slot 4, 0x87bfc7: for the image type, a new cGZPersistBufferResource (the game's constructor 0x9b64a0) in colour
    // format a4 (its low word), asked for `riid`. ORIGINAL-BUG: a failed allocation is asked anyway (null call).
    bool CreateInstanceByType(uint32_t type, uint32_t riid, void **out, uint32_t a4, uint32_t a5) override;
    bool Read(cIGZUnknown *resource, cIGZUnknown *record) override;            // slot 5, the base's 0x9b6200
    bool Write(cIGZUnknown *resource, cIGZUnknown *record) override;           // slot 6, the base's 0x9b62d0
    bool CreateInstance3(ifaces::cIGZPersistDBRecord *record, uint32_t riid, void **out) override; // slot 8 (folded 0x1f62f0)
    // Slot 9, 0x87bfa2: the image type through slot 4 in mDefaultColorFormat; false (out untouched) for others.
    bool CreateResource(uint32_t type, uint32_t riid, void **out) override;
    // Slot 10, 0x87cd33: an image resource's buffer read from the record (probing the format) in the resource's
    // colour format.
    bool ReadRecord(cIGZPersistResource *resource, cIGZUnknown *record) override;
    // Slot 11, 0x87cd83: an image resource's buffer written to the record as PNG, quality 100.
    bool WriteRecord(cIGZPersistResource *resource, cIGZUnknown *record) override;

    // ---- helpers (thiscall in the original) ----
    // 0x87be75: a new buffer (framework class 0xc470d325), else the graphics system's CreateBuffer (slot 3).
    // ORIGINAL-BUG: no graphics system: a null call.
    bool CreateBuffer(cIGZBuffer **out);
    // 0x87c3a0: opens the reader as `format`, reads frame 0 into the scratch buffer and copies it into `buffer`
    // (re)initialised to the frame's size in `colorFormat`; the reader closed (when it opened frames) and released.
    // True also when the reader opened no frames or frame 0 has no description; false when it did not open.
    bool ReadImage(cIGZGimexReader *reader, uint32_t format, cIGZBuffer *buffer, uint32_t colorFormat);
    // 0x87d494: the frame's pixels (32-bit B, G, R, A or 8-bit palette indices) into the locked buffer's colour format.
    bool CopyToBuffer(const gimex::Info *info, const uint8_t *pixels, cIGZBuffer *buffer);
    // 0x87d441 / 0x87d204 / 0x87d37e: 8-bit frames into 8-, 16- and 32-bit buffers (the palette's colours).
    bool CopyIndices8(const gimex::Info *info, const uint8_t *pixels, cIGZBuffer *buffer);
    bool CopyIndices16(const gimex::Info *info, const uint8_t *pixels, cIGZBuffer *buffer);
    bool CopyIndices32(const gimex::Info *info, const uint8_t *pixels, cIGZBuffer *buffer);
    // 0x87cfbf: a 32-bit frame into a 4444 buffer with serpentine error diffusion. Always true.
    bool Dither(const gimex::Info *info, const uint8_t *pixels, cIGZBuffer *buffer);
    // 0x87cbad: `rect` of the buffer as a 32-bit frame (alpha bits 8 when the buffer's format has alpha) to the writer
    // opened as `format`; the writer closed and released whatever happened.
    bool WriteImage(cIGZGimexWriter *writer, uint32_t format, cIGZBuffer *buffer, const cGZRect *rect, uint32_t quality);
    // 0x87cdcc: the buffer's pixels from rect's top left, info's width x height, as 32-bit B, G, R, A into `pixels`.
    bool ConvertFromBuffer(cIGZBuffer *buffer, const cGZRect *rect, const gimex::Info *info, uint8_t *pixels);
    // 0x87bd74 (__cdecl): the buffer's format has alpha (1555 format 6, 4444, 8888 format 9); with `scan`, only when
    // a pixel is not opaque.
    static bool HasAlpha(cIGZBuffer *buffer, bool scan);
    // 0x87c189 (__cdecl): loads the Intel JPEG library once.
    static void LoadIjl();
    // 0x87c13c / 0x87c015: the cached resource manager and graphics system.
    static void *GetResourceManager();
    static void *GetGraphicSystem();

    uint32_t mRefCount = 0;                // +0x14
    uint32_t mDefaultColorFormat = 1;      // +0x18 the colour format CreateResource makes resources in
    bool mDither = true;                   // +0x1c dither 32-bit frames into 4444 buffers (when the format above is 1)
    std::vector<uint8_t> mPixels;          // +0x20 the frame being read or written
    std::vector<uint32_t> mDitherErrors;   // +0x2c the dither's two error rows
};

static_assert(sizeof(cGZGimexFactory) == cGZGimexFactory::kSize, "cGZGimexFactory size");
static_assert(offsetof(cGZGimexFactory, mBaseRefCount) == 0x8 && offsetof(cGZGimexFactory, mLock) == 0xc &&
                  offsetof(cGZGimexFactory, mRefCount) == 0x14 && offsetof(cGZGimexFactory, mDefaultColorFormat) == 0x18 &&
                  offsetof(cGZGimexFactory, mDither) == 0x1c && offsetof(cGZGimexFactory, mPixels) == 0x20 &&
                  offsetof(cGZGimexFactory, mDitherErrors) == 0x2c,
              "cGZGimexFactory layout");

// The row converters (/O2 in the game: src/gz/cGZGimexFactoryRows.cpp). __fastcall (source, destination, pixel count).
namespace gimexrows {
// 32-bit B, G, R, A into 16-bit pixels.
void __fastcall To1555Opaque(const uint32_t *source, uint16_t *destination, uint32_t count);  // 0x87c790 (format 4)
// 0x87c7e0 (format 5). ORIGINAL-BUG: green is packed as 1555's (its top 5 bits in bits 5..9, bit 10 always 0), so in
// the 565 pixel it reads at half intensity.
void __fastcall To565(const uint32_t *source, uint16_t *destination, uint32_t count);
void __fastcall To1555(const uint32_t *source, uint16_t *destination, uint32_t count);        // 0x87c730 (format 6)
void __fastcall To4444(const uint32_t *source, uint16_t *destination, uint32_t count);        // 0x87c826 (format 7)
void __fastcall Copy32(const uint32_t *source, uint32_t *destination, uint32_t count);        // 0x87c880 (8 and 9)
// 16-bit pixels into 32-bit B, G, R, A (5 and 6 bit fields scaled by 255 / 31 or 255 / 63, rounding down).
void __fastcall From1555Opaque(const uint16_t *source, uint32_t *destination, uint32_t count); // 0x87c5b0 (format 4)
void __fastcall From565(const uint16_t *source, uint32_t *destination, uint32_t count);        // 0x87c660 (format 5)
void __fastcall From1555(const uint16_t *source, uint32_t *destination, uint32_t count);       // 0x87c4f0 (format 6)
// 0x87c6f8 (format 7). ORIGINAL-BUG: not a conversion: (pixel >> 4 & 0xf00) | (pixel & 0xf0), red's nibble in the
// green byte's high half and green's in the blue byte's, no alpha.
void __fastcall From4444(const uint16_t *source, uint32_t *destination, uint32_t count);
// A palette colour as a 16-bit pixel (__cdecl, in the size-built file): 0x87d6fa 1555 (alpha bit clear), 0x87d728 565,
// 0x87d75b 4444 (alpha 0xf).
uint16_t __cdecl Pack1555(uint8_t r, uint8_t g, uint8_t b);
uint16_t __cdecl Pack565(uint8_t r, uint8_t g, uint8_t b);
uint16_t __cdecl Pack4444(uint8_t r, uint8_t g, uint8_t b);
} // namespace gimexrows

// The hooks' entry points (docs/standalone-shape.md).
struct cGZGimexFactoryAdapters {
    // 0x87b412 (__cdecl): the director's creator, `new cGZGimexFactory` (null when the allocation fails).
    static cGZGimexFactory *__cdecl Create();
    // 0x87bcdc: the constructor on `memory` (only the creator calls it).
    static cGZGimexFactory *__fastcall Construct(void *memory, void *edx);
};
