#pragma once
// The Gimex image reader and writer (lane LX, standalone shape): COM objects of the framework (the cGXCOMDirector's
// classes 0x2a316c02 and 0xca316c55) that read or write one image file through the Gimex codecs (src/gz/Gimex.h).
// cGZGimexFactory (src/gz/cGZGimexFactory.h) is their only user in the game. RVAs 1.18.0.187.
//
// Both are 0x1c bytes: +4 the stream (a cIGZFile, counted) or, failing that, +8 a record file (a
// cGZDBSerialRecordAsIGZFile over a database record: its cIGZFile face is the stream), +0xc open, then the codec's
// instance and format (reader: +0x10 format, +0x14 instance; writer the other way round), +0x18 the reference count.
// Every codec call takes the Gimex lock and makes the object's format current first.
//
// Creation: only the director's creators 0x87b428 / 0x87b43e (registered by address: new 0x1c, then the constructors
// 0x87b4b4 / 0x87b4d1) build them; creators and constructors are hooked to ours. The vtables (0xe4a398, 0xe4a3d0) are
// referenced by nothing else, so every slot is ours through our vtables. Slot 1 (AddRef) is a body folded across
// unrelated classes (0x165e30: ++[ecx + 0x18]); slot 3 (0x87bbd7) is shared by the reader and the writer only.
#include "gz/Gimex.h"
#include "gz/cIGZUnknown.h"

#include <cstddef>
#include <cstdint>

namespace ifaces {
class cIGZDBSerialRecordAsIGZFile;
} // namespace ifaces

// The reader's interface (iid 0xca316beb).
class cIGZGimexReader : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xca316beb;
    // Read from a database record (a serial record): a new record file over it replaces the old one. False when the
    // framework has no record file class.
    virtual bool SetRecord(cIGZUnknown *record) = 0;                         // slot 3
    // Read from `stream` (counted) when it is open for reading, else from nothing; closes an open image first. True
    // when the stream was taken.
    virtual bool SetStream(ifaces::cIGZFile *stream) = 0;                    // slot 4
    // Opens the image as the format whose extensions match `extension`; *frames gets the frame count.
    virtual bool OpenByExtension(const char *extension, int32_t *frames) = 0; // slot 5
    // Opens the image as `format`: a codec row, or gimex::kProbe / kByExtension / kByExtensionOrProbe.
    virtual bool Open(uint32_t format, int32_t *frames) = 0;                 // slot 6
    // Frame `frame`'s description (the codec's, freed with FreeInfo); null on failure.
    virtual gimex::Info *GetInfo(int32_t frame) = 0;                         // slot 7
    // The frame's pixels into `pixels`, `pitch` bytes a row.
    virtual bool Read(gimex::Info *info, void *pixels, int32_t pitch) = 0;  // slot 8
    virtual int32_t FreeInfo(gimex::Info *info) = 0;                         // slot 9
    virtual bool Close() = 0;                                                // slot 10
};

// The writer's interface (iid 0x2a316c43).
class cIGZGimexWriter : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x2a316c43;
    virtual bool SetRecord(cIGZUnknown *record) = 0;                        // slot 3
    // As the reader's, for a stream open for writing.
    virtual bool SetStream(ifaces::cIGZFile *stream) = 0;                   // slot 4
    // `flag` goes to the codec's wopen. Both opens answer the wopen's low byte as it is (not made 0 / 1).
    virtual uint8_t OpenByExtension(const char *extension, int32_t flag) = 0; // slot 5
    // A codec row, or gimex::kByExtension / kByExtensionOrProbe (both by the stream's file name); kProbe fails.
    virtual uint8_t Open(uint32_t format, int32_t flag) = 0;                // slot 6
    virtual bool Write(gimex::Info *info, const void *pixels, int32_t pitch) = 0; // slot 7
    virtual bool Close() = 0;                                               // slot 8
};

// What the reader and the writer hold (+4..+0xc of both).
struct cGZGimexStreamHolder {
    ifaces::cIGZFile *mStream = nullptr;                          // +0x04 counted
    ifaces::cIGZDBSerialRecordAsIGZFile *mRecordFile = nullptr;   // +0x08 counted
    uint8_t mOpen = 0;                                            // +0x0c (the writer: its wopen's low byte)

    // 0x87ba76: the stream, else the record file's cIGZFile face, else null (not counted).
    ifaces::cIGZFile *GetStream();
    // 0x87bbd7 (both classes' slot 3): releases the record file, asks the framework for a new one (class 0x0a32c0da,
    // iid 0x8a32c0ac) and gives it `record` (its answer ignored). True when the framework made one.
    bool AttachRecord(cIGZUnknown *record);
    // Slot 4's body: `mode` is the access bit the stream must be open with (1 read, 2 write).
    bool TakeStream(ifaces::cIGZFile *stream, uint32_t mode);
};

class cGZGimexReader : public cIGZGimexReader, public cGZGimexStreamHolder {
public:
    static constexpr uint32_t kSize = 0x1c;
    static constexpr uint32_t kClassId = 0x2a316c02;

    cGZGimexReader();  // 0x87b4b4

    cGZGimexReader(const cGZGimexReader &) = delete;
    cGZGimexReader &operator=(const cGZGimexReader &) = delete;

    bool QueryInterface(uint32_t riid, void **ppv) override;        // 0x87baea
    uint32_t AddRef() override;                                     // folded 0x165e30
    // 0x87bb46: above 1, the count less one; else FinalRelease and 0 (the count is not decremented).
    uint32_t Release() override;
    bool SetRecord(cIGZUnknown *record) override;                   // 0x87bbd7 (shared with the writer)
    bool SetStream(ifaces::cIGZFile *stream) override;              // 0x87bb70
    bool OpenByExtension(const char *extension, int32_t *frames) override; // 0x87b6ea
    // 0x87b552. kByExtension / kByExtensionOrProbe: the format of the stream's path's extension (cIGZFile slot 25). kProbe
    // / kByExtensionOrProbe (the extension found none): asks every row from 1 how well the stream looks like it and
    // takes the one with the highest score above 0 (the first of equals).
    bool Open(uint32_t format, int32_t *frames) override;
    gimex::Info *GetInfo(int32_t frame) override;                   // 0x87b9fa (also when not open)
    bool Read(gimex::Info *info, void *pixels, int32_t pitch) override; // 0x87ba2e
    int32_t FreeInfo(gimex::Info *info) override;                   // 0x87b9f5: gimex::FreeMemory
    bool Close() override;                                          // 0x87b8f7
    virtual ~cGZGimexReader();                                      // 0x87b4ee; slot 11 deleting 0x87b514
    // Slot 12, 0x87b997: closes the stream when it is open and lets it go (SetStream(null)), then deletes this.
    virtual void FinalRelease();

    // 0x87b890: the codec's open on the stream (no path, flag 1) into the instance; *frames its answer. Always true.
    bool OpenCurrent(int32_t *frames);

    int32_t mFormat = 0;                   // +0x10
    gimex::Instance *mInstance = nullptr;  // +0x14
    uint32_t mRefCount = 0;                // +0x18
};

static_assert(sizeof(cGZGimexReader) == cGZGimexReader::kSize, "cGZGimexReader size");
static_assert(offsetof(cGZGimexReader, mStream) == 0x4 && offsetof(cGZGimexReader, mRecordFile) == 0x8 &&
                  offsetof(cGZGimexReader, mOpen) == 0xc && offsetof(cGZGimexReader, mFormat) == 0x10 &&
                  offsetof(cGZGimexReader, mInstance) == 0x14 && offsetof(cGZGimexReader, mRefCount) == 0x18,
              "cGZGimexReader layout");

class cGZGimexWriter : public cIGZGimexWriter, public cGZGimexStreamHolder {
public:
    static constexpr uint32_t kSize = 0x1c;
    static constexpr uint32_t kClassId = 0xca316c55;

    cGZGimexWriter();  // 0x87b4d1

    cGZGimexWriter(const cGZGimexWriter &) = delete;
    cGZGimexWriter &operator=(const cGZGimexWriter &) = delete;

    bool QueryInterface(uint32_t riid, void **ppv) override;        // 0x87bb22
    uint32_t AddRef() override;                                     // folded 0x165e30
    uint32_t Release() override;                                    // 0x87bb5b
    bool SetRecord(cIGZUnknown *record) override;                   // 0x87bbd7 (shared with the reader)
    bool SetStream(ifaces::cIGZFile *stream) override;              // 0x87bc1f
    // 0x87b83e. ORIGINAL-BUG: when no format matches, it returns without releasing the Gimex lock.
    uint8_t OpenByExtension(const char *extension, int32_t flag) override;
    uint8_t Open(uint32_t format, int32_t flag) override;           // 0x87b73d
    // 0x87ba8c: for format 2 (libjpeg) info's jpegFlag is set to 1 first.
    bool Write(gimex::Info *info, const void *pixels, int32_t pitch) override;
    bool Close() override;                                          // 0x87b947
    virtual ~cGZGimexWriter();                                      // 0x87b501; slot 9 deleting 0x87b533
    virtual void FinalRelease();                                    // slot 10, 0x87b9c6

    // 0x87b8b6: takes the lock, makes the format current and the codec's wopen (no path, `flag`) into the instance;
    // open is its answer's low byte.
    uint8_t OpenCurrent(int32_t flag);

    gimex::Instance *mInstance = nullptr;  // +0x10
    int32_t mFormat = 0;                   // +0x14
    uint32_t mRefCount = 0;                // +0x18
};

static_assert(sizeof(cGZGimexWriter) == cGZGimexWriter::kSize, "cGZGimexWriter size");
static_assert(offsetof(cGZGimexWriter, mStream) == 0x4 && offsetof(cGZGimexWriter, mRecordFile) == 0x8 &&
                  offsetof(cGZGimexWriter, mOpen) == 0xc && offsetof(cGZGimexWriter, mInstance) == 0x10 &&
                  offsetof(cGZGimexWriter, mFormat) == 0x14 && offsetof(cGZGimexWriter, mRefCount) == 0x18,
              "cGZGimexWriter layout");

// The hooks' entry points (docs/standalone-shape.md).
struct cGZGimexReaderAdapters {
    // 0x87b428 / 0x87b43e (__cdecl): the director's creators, `new` of each class (null when the allocation fails).
    static cGZGimexReader *__cdecl CreateReader();
    static cGZGimexWriter *__cdecl CreateWriter();
    // 0x87b4b4 / 0x87b4d1: the constructors on `memory` (only the creators call them).
    static cGZGimexReader *__fastcall ConstructReader(void *memory, void *edx);
    static cGZGimexWriter *__fastcall ConstructWriter(void *memory, void *edx);
};
