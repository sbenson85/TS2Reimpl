#pragma once
#include <cstddef>
#include <cstdint>

#include "game/owned.h"

// cGZDBSerialRecordAsIGZFile: presents a cIGZPersistDBSerialRecord (the wrapped record) as a cIGZFile.
// Primary vtable 0xe5fbb0 (cIGZDBSerialRecordAsIGZFile and cIGZUnknown at +0), cIGZFile vtable 0xe5fbd0
// at +4. RVAs are 1.18.0.187. Size 0x10.
//
// Layout: +0 primary vtable, +4 cIGZFile vtable, +8 reference count (uint32), +0xc the wrapped record
// (an owned reference; null when none). The record is reached through its own vtable:
//   slot 1 AddRef, 2 Release, 3 AsIGZPersistDBRecord (returns an object whose slot 6 is Close),
//   27 GetFieldVoid(dst, n), 51 SetFieldVoid(src, n), 52 GetSize, 53 SetSize(n), 54 GetPosition,
//   55 SeekAbsolute(pos), 56 SeekRelative(offset).
//
// Slots of the cIGZFile subobject are called with `this` = object+4, so they live in the nested File
// struct and reach the object through Owner(). Calls the original makes through `this`'s vtable (the
// file vtable for Seek, the primary one for AddRef and Release) are kept as virtual calls.
// Both vtables are owned statics (OwnVTable and OwnFileVTable are our rzface tables, below the class). The tables'
// stubs (AddRef, GetRecord, AsFile, the file face's constant answers) are the faces' own bodies.
class cGZDBSerialRecordAsIGZFile {
public:
    static constexpr uint32_t kVTable = 0xe5fbb0;     // the game's; VTable() is its owned symbol
    static constexpr uint32_t kFileVTable = 0xe5fbd0;
    static const void *VTable();                      // the word our code stores at +0
    static const void *FileVTable();                  // the word our code stores at +4
    static const void *OwnVTable();                   // rzface::cGZDBSerialRecordAsIGZFile's: the image-free VTable()
    static const void *OwnFileVTable();               // rzface::cGZDBSerialRecordAsIGZFileFile's
    static constexpr uint32_t kFileOffset = 4;       // cIGZFile subobject
    static constexpr uint32_t kGZIID_cIGZUnknown = 0x00000001;
    static constexpr uint32_t kGZIID_cIGZFile = 0xe0203645;
    static constexpr uint32_t kGZIID_Self = 0x8a32c0ac;  // cIGZDBSerialRecordAsIGZFile (not named in db/)
    static constexpr int kSetRecordSlot = 3;        // primary slot 3 (0x9afcd0), called by Release with null
    static constexpr int kDeleteSlot = 6;           // primary slot 6 (deleting destructor), flags & 1 frees

    // Record interface slots (cIGZPersistDBSerialRecord).
    static constexpr int kRecAddRef = 1;
    static constexpr int kRecRelease = 2;
    static constexpr int kRecAsPersistRecord = 3;
    static constexpr int kRecGetFieldVoid = 27;
    static constexpr int kRecSetFieldVoid = 51;
    static constexpr int kRecGetSize = 52;
    static constexpr int kRecSetSize = 53;
    static constexpr int kRecGetPosition = 54;
    static constexpr int kRecSeekAbsolute = 55;
    static constexpr int kRecSeekRelative = 56;
    static constexpr int kPersistClose = 6;         // cIGZPersistDBRecord::Close

    // Primary vtable (this = object).
    bool QueryInterface(uint32_t riid, void **ppvObj); // slot 0, 0x9adad0
    uint32_t Release();                                // slot 2, 0x9af990
    bool SetRecord(void *record);                      // slot 3, 0x9afcd0: takes a reference, releases the old one
    cGZDBSerialRecordAsIGZFile *DeletingDestructor(uint32_t flags); // slot 6, 0x9a7b30: closes and releases the record; flags & 1 frees

    // cIGZFile subobject (this = object+4).
    struct File {
        cGZDBSerialRecordAsIGZFile &Owner();

        bool Close();                                   // slot 5, 0x9a8090
        int32_t Position();                             // slot 6, 0x9ada80
        uint32_t Length();                              // slot 7, 0x9ad730
        bool SetLength(uint32_t length);                // slot 8, 0x9b01e0
        int32_t SeekToBegin();                          // slot 9, 0x9afbe0
        int32_t SeekToEnd();                            // slot 10, 0x9afc10
        int32_t SeekToRelativePosition(int32_t offset); // slot 11, 0x9afc80
        int32_t SeekToPosition(int32_t position);       // slot 12, 0x9afc50
        int32_t Seek(int32_t offset, uint32_t origin);  // slot 13, 0x9af9c0 (0 begin, 1 current, 2 end)
        bool Read(void *dst, uint32_t length);          // slot 14, 0x9ae4e0
        bool ReadWithCount(void *dst, uint32_t &length); // slot 15, 0x9af930
        bool Write(const void *src, uint32_t length);   // slot 16, 0x9b1180
        bool WriteWithCount(const void *src, uint32_t &length); // slot 17, 0x9b26b0 (length becomes 0 when the write fails)
        void *GetPath();                                // slot 25, 0x9ac870: the shared empty path string
        bool GetOpenModes(uint32_t *access, uint32_t *creation, uint32_t *sharing); // slot 30, 0x9ac800
    };

    void *mVTable;        // +0x00
    void *mFileVTable;    // +0x04
    uint32_t mRefCount;   // +0x08
    void *mRecord;        // +0x0c
};

static_assert(offsetof(cGZDBSerialRecordAsIGZFile, mVTable) == 0x00, "cGZDBSerialRecordAsIGZFile: primary vtable at +0");
static_assert(offsetof(cGZDBSerialRecordAsIGZFile, mFileVTable) == 0x04, "cGZDBSerialRecordAsIGZFile: file vtable at +4");
static_assert(offsetof(cGZDBSerialRecordAsIGZFile, mRefCount) == 0x08, "cGZDBSerialRecordAsIGZFile: count at +8");
static_assert(offsetof(cGZDBSerialRecordAsIGZFile, mRecord) == 0x0c, "cGZDBSerialRecordAsIGZFile: record at +0xc");
static_assert(sizeof(cGZDBSerialRecordAsIGZFile) == 0x10, "cGZDBSerialRecordAsIGZFile: size");

// The vtables and GetPath's static as owned statics (owned.h): the image's objects in the hooked game, standalone/
// owned_statics.cpp's otherwise. The path is a function-local static cRZString (0x20 bytes) and its init guard.
TS2_OWNED_STATIC(const OwnedVTable, e5fbb0);
TS2_OWNED_STATIC(const OwnedVTable, e5fbd0);
TS2_OWNED_STATIC(uint8_t, 10f500c)[0x20];
TS2_OWNED_STATIC(int, 10f502c);

inline const void *cGZDBSerialRecordAsIGZFile::VTable() { return &ts2_rva_e5fbb0; }
inline const void *cGZDBSerialRecordAsIGZFile::FileVTable() { return &ts2_rva_e5fbd0; }

// ---- Our vtables -------------------------------------------------------------------------------------------------------
//
// One class per face, in namespace rzface so that the RTTI name is the game's. Slots forward to the members above; the
// stubs of the game's tables are written out here. The file face's `this` is the object + 4 (the adjusting slots 0..2
// subtract it), so its slots reach the object through File::Owner().
namespace rzface {

class cGZDBSerialRecordAsIGZFile final {
    ::cGZDBSerialRecordAsIGZFile *Self() { return reinterpret_cast<::cGZDBSerialRecordAsIGZFile *>(this); }

public:
    virtual bool QueryInterface(uint32_t riid, void **ppvObj) { return Self()->QueryInterface(riid, ppvObj); } // 0
    virtual uint32_t AddRef() { return ++Self()->mRefCount; }                                                  // 1, 0x145260
    virtual uint32_t Release() { return Self()->Release(); }                                                   // 2
    virtual bool SetRecord(void *record) { return Self()->SetRecord(record); }                                 // 3
    virtual void *GetRecord() { return Self()->mRecord; }                                                      // 4, 0x145770
    virtual void *AsFile()                                                                                     // 5, 0x1c04e0
    {
        return this != nullptr ? reinterpret_cast<uint8_t *>(this) + 4 : nullptr;
    }
    virtual void *DeletingDestructor(uint32_t flags) { return Self()->DeletingDestructor(flags); }                   // 6
};

class cGZDBSerialRecordAsIGZFileFile final {
    using F = ::cGZDBSerialRecordAsIGZFile::File;
    F *Self() { return reinterpret_cast<F *>(this); }

public:
    virtual bool QueryInterface(uint32_t riid, void **ppvObj) { return Self()->Owner().QueryInterface(riid, ppvObj); } // 0, 0x9adb17
    virtual uint32_t AddRef() { return ++Self()->Owner().mRefCount; }                                                  // 1, 0x1448e9
    virtual uint32_t Release() { return Self()->Owner().Release(); }                                                   // 2, 0x9af9b5
    virtual bool Open(uint32_t, uint32_t, uint32_t) { return false; }                                                  // 3, 0x1474a0
    virtual bool IsOpen() { return Self()->Owner().mRecord != nullptr; }                                               // 4, 0x17da80
    virtual bool Close() { return Self()->Close(); }                                                                   // 5
    virtual int32_t Position() { return Self()->Position(); }                                                          // 6
    virtual uint32_t Length() { return Self()->Length(); }                                                             // 7
    virtual bool SetLength(uint32_t length) { return Self()->SetLength(length); }                                      // 8
    virtual int32_t SeekToBegin() { return Self()->SeekToBegin(); }                                                    // 9
    virtual int32_t SeekToEnd() { return Self()->SeekToEnd(); }                                                        // 10
    virtual int32_t SeekToRelativePosition(int32_t offset) { return Self()->SeekToRelativePosition(offset); }          // 11
    virtual int32_t SeekToPosition(int32_t position) { return Self()->SeekToPosition(position); }                      // 12
    virtual int32_t Seek(int32_t offset, uint32_t origin) { return Self()->Seek(offset, origin); }                     // 13
    virtual bool Read(void *dst, uint32_t length) { return Self()->Read(dst, length); }                                // 14
    virtual bool ReadWithCount(void *dst, uint32_t &length) { return Self()->ReadWithCount(dst, length); }             // 15
    virtual bool Write(const void *src, uint32_t length) { return Self()->Write(src, length); }                       // 16
    virtual bool WriteWithCount(const void *src, uint32_t &length) { return Self()->WriteWithCount(src, length); }     // 17
    virtual bool Flush() { return true; }                                                                              // 18, 0x144910
    virtual bool SysFlush() { return true; }                                                                           // 19, 0x144910
    virtual bool Remove() { return false; }                                                                            // 20, 0x147480
    virtual bool Rename(const void *) { return false; }                                                                // 21, 0x18a7a0
    virtual bool Exists() { return Self()->Owner().mRecord != nullptr; }                                               // 22, 0x17da80
    virtual bool CopyFile(const void *, uint8_t) { return false; }                                                     // 23, 0x1474b0
    virtual bool CopyPath(const void *, uint8_t) { return false; }                                                     // 24, 0x1474b0
    virtual void *GetPath() { return Self()->GetPath(); }                                                              // 25
    virtual void GetPathInto(void *) {}                                                                                // 26, 0x300a0
    virtual const char *GetPathCStr() { return nullptr; }                                                              // 27, 0x6c6b0
    virtual void SetPathCStr(const char *) {}                                                                          // 28, 0x300a0
    virtual void SetPath(const void *) {}                                                                              // 29, 0x300a0
    virtual bool GetOpenModes(uint32_t *access, uint32_t *creation, uint32_t *sharing)                                 // 30
    {
        return Self()->GetOpenModes(access, creation, sharing);
    }
    virtual void GetBuffering(int32_t *, int32_t *) {}                                                                 // 31, 0x1b9a50
    virtual bool SetBuffering(int32_t, int32_t) { return true; }                                                       // 32, 0x1477b0
};

} // namespace rzface

namespace serialrecord_detail {
template <class Face> inline const void *VTableOf()
{
    Face face;
    return *reinterpret_cast<const void *const *>(&face);
}
} // namespace serialrecord_detail

inline const void *cGZDBSerialRecordAsIGZFile::OwnVTable() { return serialrecord_detail::VTableOf<rzface::cGZDBSerialRecordAsIGZFile>(); }
inline const void *cGZDBSerialRecordAsIGZFile::OwnFileVTable() { return serialrecord_detail::VTableOf<rzface::cGZDBSerialRecordAsIGZFileFile>(); }
