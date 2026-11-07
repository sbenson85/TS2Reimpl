#pragma once
#include "rz/cRZFile.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <string>

// cRZFile with text support: a byte-order mark is looked for when the file is opened, and two
// line buffers (narrow and wide) and a cRZString sit after the base class.
// Original vtable: RVA 0xda43c4 (1.18.0.187), 34 slots, the same as cRZFile's apart from slots 3
// (Open) and 33 (deleting destructor). Its instance is 0xf4 bytes.
class cRZTextFile : public cRZFile {
public:
    static constexpr uint32_t kVTable = 0xda43c4;
    static constexpr uint32_t kSize = 0xf4;
    static constexpr int32_t kBufferSize = 0x1000; // the buffer sizes the constructor sets

    cRZTextFile *Construct(const char *path);        // 0x1c3ec4
    cRZTextFile *Construct(const cIGZString &path);  // 0x1c3e4a
    void InitText();                                 // what both constructors do after cRZFile's
    void Destruct();                                 // 0x1bab55 (also tail-calls cRZFile::Destruct)
    cRZTextFile *DeletingDestructor(uint32_t flags); // slot 33, 0x1c3ff8
    bool Open(uint32_t access, uint32_t creation, uint32_t sharing); // slot 3, 0x1c401a

    // The constructor clears both. Open only sets them when the file starts with a UTF-16
    // byte-order mark (the two bytes read as 0xfffe or 0xfeff); it never clears them.
    int32_t mCharSize;      // 0x9c: 0 from the constructor, 2 after a byte-order mark
    bool mByteOrderMark;    // 0xa0: true after a byte-order mark
    std::string mLine;      // 0xa4: narrow line buffer
    std::u16string mWide;   // 0xbc: wide line buffer
    cRZString mText;        // 0xd4: cRZString (vtable, std::string at +4, refcount at +0x1c)
};

static_assert(sizeof(cRZTextFile) == cRZTextFile::kSize, "cRZTextFile size");
static_assert(offsetof(cRZTextFile, mCharSize) == 0x9c, "cRZTextFile layout");
static_assert(offsetof(cRZTextFile, mByteOrderMark) == 0xa0, "cRZTextFile layout");
static_assert(offsetof(cRZTextFile, mLine) == 0xa4, "cRZTextFile layout");
static_assert(offsetof(cRZTextFile, mWide) == 0xbc, "cRZTextFile layout");
static_assert(offsetof(cRZTextFile, mText) == 0xd4, "cRZTextFile layout");
