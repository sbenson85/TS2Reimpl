#pragma once
// cRZIniFile's two tables (the object's +0x28 and +0x30) and the key type they share. Used by the read
// (cRZIniFileRead.size.cpp) and the load / enumeration slots (cRZIniFileLoad.size.cpp).
#include "rz/cRZString.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>

namespace rzini {

// A cRZString held by value in the tables. Copies are built like the game's cRZString(const cRZString&, 0, npos)
// (0x141eb3); moves take the other's text. The destructor is the cRZString element teardown (0x1d8450: vtable
// reset, string released). The comparison is the string's operator< (0x144a19).
struct IniKey {
    cRZString key;

    IniKey() { key.Construct(); }
    explicit IniKey(const cRZString &text) { key.Construct(text, 0, 0xffffffff); }
    IniKey(const IniKey &other) { key.Construct(other.key, 0, 0xffffffff); }
    IniKey(IniKey &&other) noexcept
    {
        key.Construct();
        key.mString = std::move(other.key.mString);
    }
    IniKey &operator=(const IniKey &) = delete;
    ~IniKey() { key.Destruct(); }
    bool operator<(const IniKey &other) const { return key.mString < other.key.mString; }
};
static_assert(sizeof(IniKey) == 0x20, "IniKey is a cRZString");

// +0x28: a lowercased section name to the file offset of its header line (0x34-byte nodes).
using SectionTable = std::map<IniKey, uint32_t>;
// +0x30: a lowercased section name to the name as written (0x50-byte nodes). Load adds to it and nothing but
// the destructor empties it.
using SecondTable = std::map<IniKey, IniKey>;
static_assert(sizeof(SectionTable) == 8 && sizeof(SecondTable) == 8, "MSVC tree header: head node pointer, size");

} // namespace rzini
