#pragma once
// DirectoryScan: the entries of one directory matching a pattern (FindFirstFile / FindNextFile through
// FindFirst 0x1c54dd, FindNext 0x1c5678, CloseFind 0x1c54b2), kept in a list with a cursor, plus listings of names or
// paths into a caller's list. A plain class (no vtable) the game builds inline in its callers' frames or objects
// (0x5c bytes): every member is __thiscall and hooked at its RVA (src/hook/tables/DirectoryScan.cpp).
//
// The lists are the game's std::list (MSVC 14.40, the same layout as ours): the scan's own list of Entry (0x30-byte
// nodes) and the callers' lists of path strings (0x28-byte nodes) whose elements the game destroys through the
// string's vtable (slot 33, flags 0).
#include "app/AppServices2.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <list>

// The special names' cells (ff2478, "*", is AppServices2.h's) and the FindNextFileA import slot, owned statics
// (game/owned.h): standalone/owned_statics.cpp defines them for image-free programs.
TS2_OWNED_STATIC(const char *, ff2470); // "."
TS2_OWNED_STATIC(const char *, ff2474); // ".."
TS2_OWNED_STATIC(void *, d9a238);       // FindNextFileA

class DirectoryScan;

namespace dscan {

// Pointer cells holding the special names (read at run time, as the original does; owned statics below).
constexpr uint32_t kDotPointer = 0xff2470;      // "."
constexpr uint32_t kDotDotPointer = 0xff2474;   // ".."
constexpr uint32_t kWildcardPointer = 0xff2478; // "*"
// The import slot of FindNextFileA and the FindNextFileW entry of the wide directory function table (+8 of
// ts2_rva_10cb614, rz/WinApiTables.h; null when the table is not wide).
constexpr uint32_t kIatFindNextFileA = 0xd9a238;
constexpr uint32_t kFindNextFileWPointer = 0x10cb61c;

// What an entry is (the word at +0x24).
enum Kind : uint32_t {
    kFolder = 4,
    kFile = 8,
};

// What ListMatching keeps: bit 0 ".", bit 1 "..", bit 2 folders, bit 3 files.
enum Select : uint32_t {
    kSelectDot = 1,
    kSelectDotDot = 2,
    kSelectFolders = 4,
    kSelectFiles = 8,
};

// An element of a caller's path list: a cRZString (the game's vtable), destroyed through its vtable's slot 33 with
// flags 0 as the game's own list code does; copied with Construct(other, 0, -1).
struct Path {
    static constexpr int kSlotDeletingDestructor = 33;
    union {
        cRZString s;
    };
    explicit Path(const cRZString &other) { s.Construct(other, 0, 0xffffffffu); }
    ~Path();
    Path(const Path &) = delete;
    Path &operator=(const Path &) = delete;
};
static_assert(sizeof(Path) == 0x20, "a path node holds the 0x20-byte string");

using PathList = std::list<Path>;
static_assert(sizeof(PathList) == 8, "the game's std::list: head node, size");

// One entry of the scan's list (0x28 bytes; copied by 0x1c4939, built empty by 0x1c4962, destroyed by 0x1c4a13).
struct Entry {
    DirectoryScan *mScan; // +0x00 the scan that found it (its directory prefixes the path)
    union {
        cRZString mName;  // +0x04 the name found (UTF-8)
    };
    uint32_t mKind;       // +0x24 kFolder / kFile

    explicit Entry(DirectoryScan *scan) : mScan(scan), mKind(0) { mName.Construct(); }
    Entry(const Entry &other) : mScan(other.mScan), mKind(other.mKind) { mName.Construct(other.mName, 0, 0xffffffffu); }
    ~Entry() { mName.Destruct(); }
    Entry &operator=(const Entry &) = delete;

    // 0x1c509f (ret 4): `out` = the scan's directory (empty without a scan) followed by the name.
    void FullPath(cRZString *out) const;
};
static_assert(sizeof(Entry) == 0x28, "Entry layout");
static_assert(offsetof(Entry, mName) == 4, "Entry layout");
static_assert(offsetof(Entry, mKind) == 0x24, "Entry layout");

// 0x1c5678 (__cdecl): the next match of the search into the node (name, directory bit); false at the end or without a
// node. Through the game's FindNextFileW pointer as UTF-16 when it is set, else FindNextFileA (import slot) with the
// name converted from the system code page. Only al is defined.
bool __cdecl FindNext(as2::FindNode *node);

} // namespace dscan

class DirectoryScan {
public:
    static constexpr uint32_t kSize = 0x5c;

    // 0x1c48da (ret 0x10): the directory (a separator appended when missing), the pattern "*", which of ".." / "."
    // Scan keeps, then Scan when `scanNow`.
    DirectoryScan(const cRZString *directory, bool scanNow, bool keepDotDot, bool keepDot);
    // 0x1c49eb: the strings, then the list.
    ~DirectoryScan();
    DirectoryScan(const DirectoryScan &) = delete;
    DirectoryScan &operator=(const DirectoryScan &) = delete;

    // 0x1c5896: the entries of directory + pattern, in the order found, "." and ".." (the first of each that is a
    // folder) kept only as the flags ask; when the pattern is "*" and the flags ask for one the search didn't find, it
    // is put first ("." before ".."). The cursor is reset. False (and an empty list) when nothing matches.
    bool Scan();
    // 0x1c4a45 (ret 8): a new directory (separator appended), Scan when `scanNow`; true.
    bool SetDirectory(const cRZString *directory, bool scanNow);
    // 0x1c63d4 (ret 8): a new pattern ("*" when empty), Scan when `scanNow`.
    void SetPattern(const cRZString *pattern, bool scanNow);
    // 0x1c539a: a backslash appended to the directory unless it ends in one or a slash (an empty directory gets one).
    void EndDirectory();
    // 0x1c5047 (ret 4): entry `index` (null past the count), stepping the cursor from the last one asked for.
    dscan::Entry *EntryAt(uint32_t index);
    // 0x1c5000 (ret 4): whether an entry has exactly this name.
    bool HasEntry(const cRZString *name) const;
    // 0x1c5db7 (ret 8): a fresh search of directory + pattern (the list untouched), appending to `out` the names
    // `select` asks for (dscan::Select); "." / ".." put first as Scan does. False when nothing matches.
    bool ListMatching(dscan::PathList *out, uint32_t select);
    // 0x1c5bbd (ret 4): ListMatching of the files.
    bool ListFiles(dscan::PathList *out);
    // 0x1c587f (ret 8): ListMatching of the folders, "." / ".." as bits 0 / 1 of `dots` ask.
    bool ListFolders(dscan::PathList *out, uint32_t dots);
    // 0x1c5bcb (ret 0xc): the full paths of the files matching the pattern in `directory` and every folder below it
    // (depth first, after the directory's own files); the top directory's own files only when `includeTop`. Leaves
    // the scan on the last directory visited. True.
    bool ListFilesBelow(const cRZString *directory, dscan::PathList *out, bool includeTop);

    std::list<dscan::Entry> mEntries; // +0x00
    uint32_t mCount;                    // +0x08 entries listed by Scan
    union {
        cRZString mDirectory;           // +0x0c with a trailing separator
    };
    union {
        cRZString mPattern;             // +0x2c
    };
    bool mKeepDotDot;                   // +0x4c
    bool mKeepDot;                      // +0x4d
    uint8_t mPad4e[2];                  // +0x4e never written
    int32_t mDepth;                     // +0x50 ListFilesBelow's recursion depth
    int32_t mCursorIndex;               // +0x54 -1: no cursor
    std::list<dscan::Entry>::iterator mCursor; // +0x58 the node of mCursorIndex (null before the first)
};

static_assert(sizeof(DirectoryScan) == DirectoryScan::kSize, "DirectoryScan size");
static_assert(offsetof(DirectoryScan, mCount) == 8, "DirectoryScan layout");
static_assert(offsetof(DirectoryScan, mDirectory) == 0xc, "DirectoryScan layout");
static_assert(offsetof(DirectoryScan, mPattern) == 0x2c, "DirectoryScan layout");
static_assert(offsetof(DirectoryScan, mKeepDotDot) == 0x4c, "DirectoryScan layout");
static_assert(offsetof(DirectoryScan, mDepth) == 0x50, "DirectoryScan layout");
static_assert(offsetof(DirectoryScan, mCursorIndex) == 0x54, "DirectoryScan layout");
static_assert(offsetof(DirectoryScan, mCursor) == 0x58, "DirectoryScan layout");

// The hooks' entry points (__fastcall for __thiscall).
struct DirectoryScanAdapters {
    static DirectoryScan *__fastcall Construct(void *memory, void *, const cRZString *directory, bool scanNow,
                                               bool keepDotDot, bool keepDot);
    static void __fastcall Destruct(DirectoryScan *self, void *);
    static bool __fastcall Scan(DirectoryScan *self, void *);
    static bool __fastcall SetDirectory(DirectoryScan *self, void *, const cRZString *directory, bool scanNow);
    static void __fastcall SetPattern(DirectoryScan *self, void *, const cRZString *pattern, bool scanNow);
    static void __fastcall EndDirectory(DirectoryScan *self, void *);
    static dscan::Entry *__fastcall EntryAt(DirectoryScan *self, void *, uint32_t index);
    static bool __fastcall HasEntry(DirectoryScan *self, void *, const cRZString *name);
    static void __fastcall FullPath(const dscan::Entry *self, void *, cRZString *out);
    static bool __fastcall ListMatching(DirectoryScan *self, void *, dscan::PathList *out, uint32_t select);
    static bool __fastcall ListFiles(DirectoryScan *self, void *, dscan::PathList *out);
    static bool __fastcall ListFolders(DirectoryScan *self, void *, dscan::PathList *out, uint32_t dots);
    static bool __fastcall ListFilesBelow(DirectoryScan *self, void *, const cRZString *directory, dscan::PathList *out,
                                          bool includeTop);
};
