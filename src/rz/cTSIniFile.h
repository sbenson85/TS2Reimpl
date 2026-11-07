#pragma once
#include "gz/cIGZString.h"
#include "rz/cRZCmdLine.h"
#include "rz/cRZIniFile.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>

// cTSIniFile: the game's .ini reader with section prefixes. Original vtable: RVA 0xeb7220 (1.18.0.187),
// 21 slots: cRZIniFile's 20 with 0 (deleting destructor), 6 (GetString) and 13 (the section walk)
// overridden, and a new slot 20 (HasSection with prefixes).
//
// Layout: cRZIniFile's 0x40 bytes, then at
// 0x40 a vector<cRZString> of prefixes (the type the command line also uses). The constructor fills the
// prefixes from the extra argument and from the command-line switch "iniContext" (a ';'-separated list).
// Every lookup tries "<prefix>-<section>" for each prefix, in order, before the plain section name.
//
// The base slots are cRZIniFile's (ours), called through this object's vtable where the original does; slot 13
// calls the base walk (0x9c5ef2, cRZIniFile::ForEachKey) directly, like the original. The base class's slot 15
// (0x9c6a0e, HasSection) is reimplemented here, as a member, because slot 20 needs it.
class cTSIniFile : public cRZIniFile {
public:
    static constexpr uint32_t kVTable = 0xeb7220;
    static constexpr uint32_t kSize = 0x4c;
    static constexpr int kSlotHasSectionBase = 15;

    // 0xcb2a20 (ret 8): path as for cRZIniFile; `extra`, when not null, is the first prefix.
    cTSIniFile *Construct(const cIGZString &path, const cRZString *extra);
    void Destruct();                                  // 0xcb2cc0: prefixes freed, then cRZIniFile::Destruct
    cTSIniFile *DeletingDestructor(uint32_t flags);   // slot 0, 0xcb2ce0 (ret 4); flags & 1 frees the 0x4c bytes
    bool GetString(const cIGZString &section, const cIGZString &key, cIGZString *out);  // slot 6, 0xcb2e90 (ret 0xc)
    // Slot 13, 0xcb2d00 (ret 0xc): the first prefix that has the section decides which name the base walk gets.
    // callback and context are the base slot's (callback(key, value, context), cdecl).
    bool SectionWalk(const cIGZString &section, void *callback, void *context);
    bool HasSectionPrefixed(const cIGZString &section);  // slot 20, 0xcb30f0 (ret 4)
    bool HasSection(const cIGZString &section);          // cRZIniFile slot 15, 0x9c6a0e (ret 4)

    cRZStringVector mPrefixes;   // 0x40
};

static_assert(sizeof(cTSIniFile) == cTSIniFile::kSize, "cTSIniFile size");
static_assert(offsetof(cTSIniFile, mPrefixes) == 0x40, "cTSIniFile layout");

// 0x1ad676 (__thiscall, ret 8; `this` is a cRZString): takes the next ';'-style token off the front of the
// string. Leading delimiters are dropped; the text up to the next delimiter is assigned to out->mString, and
// the token and the delimiter run after it are erased from the string. With no delimiter left, the rest of the
// text is moved into out (out is cleared first, and the string ends up with out's old buffer). False when
// nothing is left.
struct cRZStringTokenizer {
    bool NextToken(char delimiter, cRZString *out);
};
