#pragma once
// DynamicLibraryList: the plug-in libraries the application loads at start-up (cTSAppFW::AddDynamicLibrariesHere
// 0xc26c30 builds one on its stack). A plain class (no vtable, 0x1c bytes); every member is __thiscall and hooked at its
// RVA (src/hook/tables/DynamicLibraryList.cpp), and the two section-walk callbacks of ReadConfiguration are in effect
// through it.
//
// The containers are the game's std::vector / std::map (MSVC 14.40, the same layout as ours): a vector of 0x24-byte
// entries and two maps of string pairs (0x50-byte nodes: the tree links, then the key string at +0x10 and the value
// string at +0x30), ordered by the strings' bytes.
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class cIGZString;

namespace dynlib {

// A string held by value in the containers (the game's cRZString vtable, refcount 0), copied with
// Construct(other, 0, npos) and destroyed with Destruct.
struct Text {
    union {
        cRZString s;
    };
    Text() { s.Construct(); }
    explicit Text(const cRZString &other) { s.Construct(other, 0, 0xffffffffu); }
    Text(const Text &other) { s.Construct(other.s, 0, 0xffffffffu); }
    ~Text() { s.Destruct(); }
    Text &operator=(const Text &other)
    {
        s.mString = other.s.mString;
        return *this;
    }
};
static_assert(sizeof(Text) == 0x20, "a string by value");

// The maps' order: the strings' bytes (std::string's operator<), also against a bare std::string (lookups).
struct TextLess {
    using is_transparent = void;
    bool operator()(const Text &a, const Text &b) const { return a.s.mString < b.s.mString; }
    bool operator()(const std::string &a, const Text &b) const { return a < b.s.mString; }
    bool operator()(const Text &a, const std::string &b) const { return a.s.mString < b; }
};

using TextMap = std::map<Text, Text, TextLess>;
static_assert(sizeof(TextMap) == 8, "the game's std::map: head node, size");

// One library (0x24 bytes).
struct Library {
    Text mPath;          // +0x00
    uint8_t mRequired;   // +0x20 a required library fails LoadAll when it doesn't load
    uint8_t mLoaded;     // +0x21
    uint8_t mPad22[2];   // +0x22 never written

    Library(const cRZString &path, uint8_t required) : mPath(path), mRequired(required), mLoaded(0) {}
};
static_assert(sizeof(Library) == 0x24, "Library layout");
static_assert(offsetof(Library, mRequired) == 0x20, "Library layout");

// 0xcb3f90 (__cdecl, a section walk's callback): the "Configurations" line `key=value` as configurations[key] = value.
void __cdecl AddConfiguration(const cRZString *key, const cRZString *value, void *list);
// 0xcb3fd0 (__cdecl, a section walk's callback): one "DLLs" line, `key=name[,configuration]` (see
// DynamicLibraryList::ReadConfiguration).
void __cdecl AddLibraryLine(const cRZString *key, const cRZString *value, void *list);

} // namespace dynlib

class DynamicLibraryList {
public:
    static constexpr uint32_t kSize = 0x1c;

    // 0xcb3640: no libraries, no configurations, no version requirements.
    DynamicLibraryList() = default;
    // 0xcb36f0: the version map, the configuration map, then the libraries.
    ~DynamicLibraryList() = default;
    DynamicLibraryList(const DynamicLibraryList &) = delete;
    DynamicLibraryList &operator=(const DynamicLibraryList &) = delete;

    // 0xcb3740 (ret 8): a library appended (not loaded); only the low byte of `required` counts.
    void Add(const cRZString *path, uint8_t required);
    // 0xcb3870 (ret 8): the ini file `path` (cTSIniFile, `configuration` its first prefix): its "Configurations"
    // section into the configuration map (name = directory), then its "DLLs" section's lines as libraries. Each DLLs
    // line is `required=<file>[,<configuration>]` or `<other>=...` (optional): the file is taken in the directory
    // the configuration names ("." when none or unknown), under the library directory option 0x6bcfce93. Whether
    // the DLLs section was there.
    bool ReadConfiguration(const cIGZString *path, const cRZString *configuration);
    // 0xcb3e10 (ret 0xc): with `required`, the file name of `path` (lower case, without its extension) must have
    // file version `version` to be loaded; without, any requirement for that name is dropped (the version unused).
    void RequireVersion(const cRZString *path, const cRZString *version, uint8_t required);
    // 0xcb3b70: every library not loaded yet that is required or exists, and whose file version matches its
    // requirement (when it has one), handed to the COM director (cIGZCOM::AddLibrary); false when a required one
    // didn't load.
    bool LoadAll();
    // 0xcb3a80: a "Have (NOT) Loaded Required/Optional DLL" line per library, built and dropped (this build has no
    // log for it).
    void Report() const;

    std::vector<dynlib::Library> mLibraries; // +0x00
    dynlib::TextMap mConfigurations;          // +0x0c configuration name -> directory
    dynlib::TextMap mVersions;                // +0x14 lower-case file name -> required file version
};

static_assert(sizeof(DynamicLibraryList) == DynamicLibraryList::kSize, "DynamicLibraryList size");
static_assert(offsetof(DynamicLibraryList, mConfigurations) == 0xc, "DynamicLibraryList layout");
static_assert(offsetof(DynamicLibraryList, mVersions) == 0x14, "DynamicLibraryList layout");

// The hooks' entry points (__fastcall for __thiscall).
struct DynamicLibraryListAdapters {
    static DynamicLibraryList *__fastcall Construct(void *memory, void *);
    // al: 0 (the original leaves what its vector destructor's operator delete left in eax; its caller ignores it).
    static uint8_t __fastcall Destruct(DynamicLibraryList *self, void *);
    static void __fastcall Add(DynamicLibraryList *self, void *, const cRZString *path, uint32_t required);
    static bool __fastcall ReadConfiguration(DynamicLibraryList *self, void *, const cIGZString *path,
                                             const cRZString *configuration);
    static void __fastcall RequireVersion(DynamicLibraryList *self, void *, const cRZString *path,
                                          const cRZString *version, uint32_t required);
    static bool __fastcall LoadAll(DynamicLibraryList *self, void *);
    static void __fastcall Report(const DynamicLibraryList *self, void *);
};
