#pragma once
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"

#include <cstdint>

// The game's file interface (RTTI cIGZFile; db/interface-slots.jsonl, checked against cRZFile's vtable 0xda42a0 and the
// member list in rz/cRZFile.h). Implemented by rzface::cRZFile (cRZFile's own vtable, src/rz/cRZFile.h), which adds the
// deleting destructor at slot 33.
// MSVC places a group of overloads together at the first one's position, in REVERSE declaration order: each pair below
// is declared higher slot first.
class cIGZFile : public cIGZUnknown {
public:
    virtual bool Open(uint32_t access, uint32_t creation, uint32_t sharing) = 0; // slot 3
    virtual bool IsOpen() const = 0;                                             // slot 4
    virtual bool Close() = 0;                                                    // slot 5
    virtual int32_t Position() const = 0;                                        // slot 6
    virtual uint32_t Length() = 0;                                               // slot 7
    virtual bool SetLength(uint32_t length) = 0;                                 // slot 8
    virtual int32_t SeekToBegin() = 0;                                           // slot 9
    virtual int32_t SeekToEnd() = 0;                                             // slot 10
    virtual int32_t SeekToRelativePosition(int32_t offset) = 0;                  // slot 11
    virtual int32_t SeekToPosition(int32_t position) = 0;                        // slot 12
    virtual int32_t Seek(int32_t offset, uint32_t origin) = 0;                   // slot 13
    virtual bool Read(void *dst, uint32_t length) = 0;                           // slot 14
    virtual bool ReadWithCount(void *dst, uint32_t &length) = 0;                 // slot 15
    virtual bool Write(const void *src, uint32_t length) = 0;                    // slot 16
    virtual bool WriteWithCount(const void *src, uint32_t &length) = 0;          // slot 17
    virtual bool Flush() = 0;                                                    // slot 18
    virtual bool SysFlush() = 0;                                                 // slot 19
    virtual bool Remove() = 0;                                                   // slot 20
    virtual bool Rename(const cIGZString &newPath) = 0;                          // slot 21
    virtual bool Exists() = 0;                                                   // slot 22
    virtual bool Copy(const cIGZString &sourcePath, uint8_t overwrite) = 0;      // slot 24
    virtual bool Copy(const cIGZFile *sourceFile, uint8_t overwrite) = 0;        // slot 23
    virtual void GetPath(cIGZString &out) const = 0;                             // slot 26
    virtual const cIGZString *GetPath() const = 0;                               // slot 25
    virtual const char *GetPathCStr() const = 0;                                 // slot 27
    virtual void SetPath(const cIGZString &path) = 0;                            // slot 29
    virtual void SetPath(const char *path) = 0;                                  // slot 28
    virtual bool GetOpenModes(uint32_t *access, uint32_t *creation, uint32_t *sharing) const = 0; // slot 30
    // The original leaves the write-size pointer in eax and callers test only al: not a clean bool.
    virtual uintptr_t GetBuffering(int32_t *read, int32_t *write) const = 0;     // slot 31
    virtual bool SetBuffering(int32_t readSize, int32_t writeSize) = 0;          // slot 32
};
