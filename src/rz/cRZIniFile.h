#pragma once
#include "gz/cIGZString.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <string>

// The game's .ini reader. Original vtable: RVA 0xe617a4 (1.18.0.187), 20 slots.
//
// Layout: mPath is the file's path (a cRZString, so its std::string is at 0x08); mEnabled (0x24)
// is set by SetEnabled; mLoaded (0x25) is cleared by the game's slot 5; mEntries (0x28) is the
// MSVC std::map header of the section/key table (head pointer at +0, size at +4); mFile (0x38) is
// the cIGZFile the entries come from, which the object holds a reference to.
//
// The tables are rzini::SectionTable and rzini::SecondTable (rz/cRZIniFileTables.h). mOpenForWrite (0x3c) is
// the mode slot 19 last opened the file in.
//
// Reimplemented: every slot but 1 and 3 (AddRef / Release, shared with other classes) and 15 (HasSection, in
// cTSIniFile.cpp). Slots are called through the vtable where the original does, so
// subclasses (cTSIniFile, cHitIniFile) see their own overrides.
class cRZIniFile {
public:
    static constexpr uint32_t kVTable = 0xe617a4;

    cRZIniFile *Construct(const cIGZString &path);         // 0x9c5d11 (ret 4): copies the path, makes the two empty tables and a cRZFile on `path`
    void Destruct();                                       // 0x9c5e5d: closes and releases the file, destroys the tables and the path
    cRZIniFile *DeletingDestructor(uint32_t flags);        // slot 0, 0x9c5eac (ret 4); flags & 1 frees the 0x40 bytes
    // slot 6, 0x9c679f (ret 0xc): the value of (section, key), lowercased for the lookup, loaded on first use
    // through the game's slot 17; the lines come from the game's slot 18. `out` is written (a cRZString).
    bool GetString(const cIGZString &section, const cIGZString &key, cIGZString *out);
    void SetPath(const cIGZString &path);                  // slot 2, 0x9c6b06: copies the path (clears first if loaded)
    bool SetFile(void *file);                              // slot 4, 0x9c6a73: a cIGZFile*; null makes a cRZFile on mPath
    bool GetLong(const cIGZString &section, const cIGZString &key, int32_t *value); // slot 7, 0x9c69ab
    bool SetLong(const cIGZString &section, const cIGZString &key, int32_t value);  // slot 11, 0x9c75f5
    bool SetEnabled(bool enabled);                         // slot 16, 0x9c6aec: clears when disabled

    // Slot 5, 0x9c5ecb: closes the file when open, empties the section table, marks it unloaded; returns 1.
    bool Clear();
    // Slot 8, 0x9c66b2 (ret 0x10): up to `size` bytes hex-decoded from the value into `out`; the byte count, or 0
    // when there is no value or it is not hex.
    uint32_t GetBinary(const cIGZString &section, const cIGZString &key, void *out, uint32_t size);
    // Slot 9, 0x9c6722 (ret 0xc): the whole value hex-decoded into a new[] block put in *out; its size, or 0 (and
    // *out null) when it is not hex.
    uint32_t GetBinaryAlloc(const cIGZString &section, const cIGZString &key, void **out);
    // Slot 10, 0x9c6bbc (ret 0xc): writes "key = value" into the file (cRZIniFileWrite.size.cpp): a new file gets
    // the section and the line; an unknown section is appended with the line; in a known section the key's line is
    // replaced, or the line goes before the next section or at the end of the file. False for an empty section or
    // key, or when the file cannot be loaded or opened for writing.
    bool SetString(const cIGZString &section, const cIGZString &key, const cIGZString &value);
    // Slot 12, 0x9c6b28 (ret 0x10): `size` bytes as upper-case hex through slot 10 (SetString).
    bool SetBinary(const cIGZString &section, const cIGZString &key, const void *data, uint32_t size);
    // Slot 13, 0x9c5ef2 (ret 0xc): each key=value of the section, in file order, to callback(key, value, context).
    using KeyCallback = void(__cdecl *)(const cIGZString *key, const cIGZString *value, void *context);
    bool ForEachKey(const cIGZString &section, KeyCallback callback, void *context);
    // Slot 14, 0x9c615f (ret 8): each section's name as written, in table order, to callback(name, empty, context).
    using SectionCallback = void(__cdecl *)(const cIGZString *name, const cIGZString *unused, void *context);
    bool ForEachSection(SectionCallback callback, void *context);
    // Slot 17, 0x9c63da (ret 4): opens the file when closed (slot 19), then indexes its [section] lines.
    bool Load(bool write);
    // Slot 18, 0x9c623d (ret 4): the next line into `out` (without its line break; following blank lines
    // skipped); false at the end of the file.
    bool ReadLine(cIGZString *out);
    // Slot 19, 0x9c65f1 (ret 4): opens the file for reading, or reading and writing; retries for about two
    // seconds while it exists but cannot be opened.
    bool Open(bool write);

    const void *mVTable;                         // 0x00
    cRZString mPath;                             // 0x04
    bool mEnabled;                               // 0x24
    bool mLoaded;                                // 0x25
    uint8_t mUnknown26[2];                       // 0x26
    uint8_t mEntries[8];                         // 0x28: MSVC std::map header (not interpreted here)
    uint8_t mSecondTable[8];                     // 0x30: a second MSVC tree header (the original's slots 13-17 use it)
    void *mFile;                                 // 0x38 (cIGZFile*)
    bool mOpenForWrite;                          // 0x3c: set by slot 19
    uint8_t mUnknown3d[3];                       // 0x3d
};

// The hex text of the binary slots (__cdecl). 0x1afb5e: two characters per byte, high nibble first, '0'-'9',
// 'A'-'F', 'a'-'f'; anything else counts as 0 and makes the result false; `length` odd reads the character after.
bool __cdecl IniHexDecode(const char *text, uint32_t length, uint8_t *out);
// 0x1afbd7: two upper-case hex digits per byte, then a '0' (not a terminator) at out[2 * length].
void __cdecl IniHexEncode(const uint8_t *data, uint32_t length, char *out);

// The string helpers GetString uses (cRZString methods in the original, not vtable slots). `this` is the
// cRZString; the struct holds no data. All __thiscall.
struct cRZIniStringHelpers {
    void TrimLeading();                                  // 0x1ad11d: erases leading spaces and tabs
    void TrimTrailing();                                 // 0x1ad307: erases trailing spaces and tabs
    void TrimBoth();                                     // 0x1ad83c: TrimLeading then TrimTrailing
    // 0x1ad555 (ret 8): the text before the first `delimiter` goes to *out and is erased from this together with
    // the delimiter; no delimiter found: *out is empty; a null or empty delimiter: *out is a copy of the whole.
    cRZString *SplitOff(cRZString *out, const char *delimiter);
};

static_assert(offsetof(cRZIniFile, mPath) == 0x04, "cRZIniFile layout");
static_assert(offsetof(cRZIniFile, mEnabled) == 0x24 && offsetof(cRZIniFile, mLoaded) == 0x25, "cRZIniFile layout");
static_assert(offsetof(cRZIniFile, mEntries) == 0x28, "cRZIniFile layout");
static_assert(offsetof(cRZIniFile, mFile) == 0x38, "cRZIniFile layout");
static_assert(sizeof(cRZIniFile) == 0x40, "cRZIniFile layout");
