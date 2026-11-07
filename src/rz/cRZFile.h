#pragma once
#include "game/owned.h"
#include "gz/cIGZFile.h"
#include "gz/cIGZString.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// cIGZFile implementation over a Win32 file handle, with optional read and write buffers.
// Original vtable: RVA 0xda42a0 (1.18.0.187), 34 slots.
//
// Two positions are tracked: mPosition is where the caller is (Position()), mFilePosition is where
// the OS file pointer is. They differ while buffered data is pending. The read buffer holds
// mReadBufferLength bytes read from file offset mReadBufferStart; the write buffer holds
// mWriteBufferLength bytes destined for offset mWriteBufferStart. Buffer sizes are 0 (unbuffered)
// or even sizes from 4 to 0x1f4000.
//
// Paths are cIGZString (UTF-8). Each path operation has two branches, as in the original: when the
// game has filled in its table of wide file functions (globals from RVA 0x10cb5e0; always on NT)
// the path goes to UTF-16 and through those pointers; otherwise it's converted to the ANSI code
// page and the ...A import is used. Paths ending in ".lnk" are opened as their shortcut target.
class cRZFile {
public:
    static constexpr uint32_t kVTable = 0xda42a0; // the game's; VTable() is its owned symbol
    // The word our constructors and destructor store: ts2_rva_da42a0 (the game's table in hooked builds, whose slots are
    // hooked to these members; OwnVTable() in an image-free program).
    static const void *VTable();
    // rzface::cRZFile's vtable, our own table (the image-free program's VTable(); tests call it by slot).
    static const void *OwnVTable();
    static constexpr uint32_t kGZIID_cIGZUnknown = 0x00000001;
    static constexpr uint32_t kGZIID_cIGZFile = 0xe0203645;
    static constexpr uint32_t kErrorNone = 0x20000000;          // after construction
    static constexpr uint32_t kErrorBusy = 0x20000001;          // from ERROR_ALREADY_EXISTS (0xb7)
    static constexpr uint32_t kErrorAccessDenied = 0x20000002;  // from ERROR_ACCESS_DENIED (5)
    static constexpr uint32_t kMaxBufferSize = 0x1f4000;

    cRZFile *Construct(const cIGZString &path);  // 0x1c0c07
    cRZFile *Construct(const char *path);        // 0x1c0c8b
    cRZFile *Construct();                        // 0x1c0d1d
    void Destruct();                             // 0x1c0e6d
    cRZFile *DeletingDestructor(uint32_t flags); // slot 33, 0x1c0ef9

    bool QueryInterface(uint32_t riid, void **ppvObj);  // slot 0, 0x1c2aa0
    uint32_t AddRef();                                  // slot 1, 0x1c0f1b
    uint32_t Release();                                 // slot 2, 0x1c2e0b
    bool IsOpen() const { return mOpen; }               // slot 4 (4-byte function shared with other classes)
    bool Close();                                       // slot 5, 0x1c0faa
    int32_t Position() const;                           // slot 6, 0x1c2a93
    uint32_t Length();                                  // slot 7, 0x1c230b
    bool SetLength(uint32_t length);                    // slot 8, 0x1c3717
    int32_t SeekToBegin();                              // slot 9, 0x1c34d5
    int32_t SeekToEnd();                                // slot 10, 0x1c34df
    int32_t SeekToRelativePosition(int32_t offset);     // slot 11, 0x1c34f7
    int32_t SeekToPosition(int32_t position);           // slot 12, 0x1c34e9
    int32_t Seek(int32_t offset, uint32_t origin);      // slot 13, 0x1c3450 (0 begin, 1 current, 2 end)
    bool Open(uint32_t access, uint32_t creation, uint32_t sharing); // slot 3, 0x1c285d
    bool Read(void *dst, uint32_t length);              // slot 14, 0x1c2c6e
    bool ReadWithCount(void *dst, uint32_t &length);    // slot 15, 0x1c2c97
    bool Write(const void *src, uint32_t length);       // slot 16, 0x1c3bec
    bool WriteWithCount(const void *src, uint32_t &length); // slot 17, 0x1c3c15
    bool Flush();                                       // slot 18, 0x1c1413
    bool SysFlush();                                    // slot 19, 0x1c3bdd
    bool Remove();                                      // slot 20, 0x1c2ea2
    bool Rename(const cIGZString &newPath);             // slot 21, 0x1c3015 (the path changes even on failure)
    bool Exists();                                      // slot 22, 0x1c114e
    // Copy the other file onto this one (overwrite: replace an existing file).
    bool Copy(const void *sourceFile, uint8_t overwrite);       // slot 23, 0x1c10d3 (a cIGZFile*)
    bool Copy(const cIGZString &sourcePath, uint8_t overwrite); // slot 24, 0x1c1122
    const cIGZString *GetPath() const { return mPath.AsInterface(); } // slot 25 (shared 4-byte function)
    void GetPath(cIGZString &out) const;                // slot 26 (shared with other classes)
    const char *GetPathCStr() const { return mPath.mString.c_str(); } // slot 27 (shared)
    void SetPath(const char *path);                     // slot 28, 0x1c379f
    void SetPath(const cIGZString &path);               // slot 29, 0x1c377a
    bool GetOpenModes(uint32_t *access, uint32_t *creation, uint32_t *sharing) const; // slot 30, 0x1c1c37
    // Returns eax as the original leaves it: the `write` pointer (callers only test al).
    uintptr_t GetBuffering(int32_t *read, int32_t *write) const; // slot 31, 0x1c1a4d
    bool SetBuffering(int32_t readSize, int32_t writeSize);      // slot 32, 0x1c3673

    // Helpers (non-virtual in the original).
    void ClearReadBuffer();                         // 0x1c0f8f
    void ClearWriteBuffer();                        // 0x1c0f9b
    bool FlushWriteBuffer();                        // 0x1c142b
    int32_t RawRead(void *dst, uint32_t length);    // 0x1c2bc9: ReadFile, -1 on failure
    int32_t RawSeek(int32_t offset, uint32_t origin); // 0x1c2c0a: SetFilePointer
    bool FillReadBuffer();                          // 0x1c12c1
    bool BufferedWrite(const void *src, int32_t length); // 0x1c1312
    void SetErrorFromLastError();                   // 0x1c3429

    // Static path helpers (cdecl in the original).
    static uint32_t LengthOfPath(const cIGZString &path);   // 0x1c1b8b: -1 if not found
    static bool PathExists(const cIGZString &path);         // 0x1c1242
    static bool MovePath(const cIGZString &from, const cIGZString &to); // 0x1c2f48
    static bool CopyPath(const cIGZString &from, const cIGZString &to, uint8_t overwrite); // 0x1c0ff8
    // Shortcut (.lnk) targets via the shell's IShellLink. W: true and *target set if resolved.
    static bool ResolveShortcutW(const std::u16string &path, std::u16string *target); // 0x1c3235
    // A: 0 if not a .lnk path, -1 on failure, else the target's length + 1 (copied to target).
    static int32_t ResolveShortcutA(const char *path, char *target, int32_t targetSize); // 0x1c3059

    const void *mVTable;          // 0x00
    cRZString mPath;              // 0x04
    bool mOpen;                   // 0x24
    bool mLengthCached;           // 0x25
    void *mHandle;                // 0x28 (INVALID_HANDLE_VALUE when closed)
    uint32_t mAccess;             // 0x2c open modes as passed to Open (bit 1: write)
    uint32_t mCreation;           // 0x30
    uint32_t mSharing;            // 0x34
    uint32_t mRefCount;           // 0x38
    uint32_t mError;              // 0x3c
    cRZString mUnknownString;     // 0x40 (not used by any slot)
    uint32_t mCachedLength;       // 0x60
    int32_t mPosition;            // 0x64
    int32_t mFilePosition;        // 0x68
    uint32_t mReadBufferSize;     // 0x6c
    std::vector<char> mReadBuffer;  // 0x70
    int32_t mReadBufferStart;     // 0x7c
    int32_t mReadBufferLength;    // 0x80
    uint32_t mWriteBufferSize;    // 0x84
    std::vector<char> mWriteBuffer; // 0x88
    int32_t mWriteBufferStart;    // 0x94
    int32_t mWriteBufferLength;   // 0x98
};

static_assert(sizeof(cRZFile) == 0x9c, "cRZFile size");
static_assert(offsetof(cRZFile, mOpen) == 0x24 && offsetof(cRZFile, mHandle) == 0x28, "cRZFile layout");
static_assert(offsetof(cRZFile, mRefCount) == 0x38 && offsetof(cRZFile, mUnknownString) == 0x40, "cRZFile layout");
static_assert(offsetof(cRZFile, mReadBuffer) == 0x70 && offsetof(cRZFile, mWriteBuffer) == 0x88, "cRZFile layout");
static_assert(offsetof(cRZFile, mWriteBufferLength) == 0x98, "cRZFile layout");

// The vtable (owned.h, OwnedVTable): `offset ts2_rva_da42a0`, the same one-instruction store as GameAddress(kVTable).
TS2_OWNED_STATIC(const OwnedVTable, da42a0);

namespace rzface {

// cRZFile's own vtable (the image-free program's): a cIGZFile face over the hook-shaped class (no data; `this` is the
// file). Each slot runs the cRZFile member for it (src/rz/cRZFile.size.cpp), slot 33 the deleting destructor. Namespaced
// so that its RTTI name is still cRZFile (tools/matchcheck.py ties our vtable to the game's 0xda42a0).
class cRZFile final : public cIGZFile {
public:
    bool QueryInterface(uint32_t riid, void **ppvObj) override;
    uint32_t AddRef() override;
    uint32_t Release() override;
    bool Open(uint32_t access, uint32_t creation, uint32_t sharing) override;
    bool IsOpen() const override;
    bool Close() override;
    int32_t Position() const override;
    uint32_t Length() override;
    bool SetLength(uint32_t length) override;
    int32_t SeekToBegin() override;
    int32_t SeekToEnd() override;
    int32_t SeekToRelativePosition(int32_t offset) override;
    int32_t SeekToPosition(int32_t position) override;
    int32_t Seek(int32_t offset, uint32_t origin) override;
    bool Read(void *dst, uint32_t length) override;
    bool ReadWithCount(void *dst, uint32_t &length) override;
    bool Write(const void *src, uint32_t length) override;
    bool WriteWithCount(const void *src, uint32_t &length) override;
    bool Flush() override;
    bool SysFlush() override;
    bool Remove() override;
    bool Rename(const cIGZString &newPath) override;
    bool Exists() override;
    bool Copy(const cIGZString &sourcePath, uint8_t overwrite) override;
    bool Copy(const cIGZFile *sourceFile, uint8_t overwrite) override;
    void GetPath(cIGZString &out) const override;
    const cIGZString *GetPath() const override;
    const char *GetPathCStr() const override;
    void SetPath(const cIGZString &path) override;
    void SetPath(const char *path) override;
    bool GetOpenModes(uint32_t *access, uint32_t *creation, uint32_t *sharing) const override;
    uintptr_t GetBuffering(int32_t *read, int32_t *write) const override;
    bool SetBuffering(int32_t readSize, int32_t writeSize) override;
    virtual void *DeletingDestructor(uint32_t flags); // slot 33
};

} // namespace rzface

inline const void *cRZFile::VTable() { return &ts2_rva_da42a0; }

// The address of rzface::cRZFile's vtable as a link-time constant: the compiler folds the local face's vptr into
// `offset ??_7cRZFile@rzface@@6B@` (no object is built).
inline const void *cRZFile::OwnVTable()
{
    rzface::cRZFile face;
    return *reinterpret_cast<const void *const *>(&face);
}
