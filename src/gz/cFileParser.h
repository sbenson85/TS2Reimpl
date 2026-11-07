#pragma once
// nGZCommandParser::cFileParser (lane IM): runs a list of command files through a parser, and watches their directories so
// that a change marks the list dirty. The game makes it only through its COM class factory 0xb0dc30 (class 0x5cf9459a,
// registered by address in the COM director data, src/rz/COMDirectorData.cpp), which is hooked, so the class is in the
// standalone shape (docs/standalone-shape.md): our vtables on every object.
//
// RTTI: cFileParser derives from cIFileParser (+0; the game's vtable 0xe7f9d0, 11 slots), cIGZMessageTarget2 (+4; 0xe7fa00)
// and cRZUnknown (+8, the count at +0xc; 0xe7fa14). AddRef and Release of the first two faces forward to cRZUnknown's
// (the folded 0x1c04c0 `add ecx, 8; jmp 0x148fb0` and 0x2c7e80 `add ecx, 8; jmp 0x1b94e9`, and their `sub ecx, 4` thunks);
// QueryInterface 0xb0def0 answers 0x1cf94583 itself and leaves the rest to cRZUnknown's (thunks 0xb0df17, 0xb0df1f); the
// deleting destructor 0xb0d5a0 is cRZUnknown's slot 3 through the thunk 0xb0d58e. RVAs are 1.18.0.187.
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZUnknownBase.h"
#include "rz/cRZStopwatch.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class cRZFileName;

// The directory watcher the file parser asks the COM framework for (class 0xe953da68, interface 0xe953da64; the game's
// cGZPersistDirectoryWatcher, factory 0x9c15c0, vtable 0xe61184). Only the slots the file parser calls; named by that use.
class cIGZPersistDirectoryWatcher : public cIGZUnknown {
public:
    static constexpr uint32_t kClassId = 0xe953da68;
    static constexpr uint32_t kIID = 0xe953da64;

    virtual void Slot3() = 0;                       // 3 (unused here)
    virtual bool Init() = 0;                        // 4
    virtual bool Shutdown() = 0;                    // 5 (0x9c1930)
    virtual bool Start() = 0;                       // 6 (0x9c1970)
    virtual void Slot7() = 0;                       // 7 (unused here)
    virtual void SetTarget(cIGZMessageTarget2 *target) = 0; // 8 (0x9c1900, ret 4): a counted target
    virtual void Slot9(uint32_t) = 0;               // 9 (0x3bdc80, ret 4; unused here)
    // 10 (0x9c18b0, ret 0xc): the directory to watch, the change filter (here 0x18) and whether subdirectories count.
    virtual void SetDirectory(cIGZString *directory, uint32_t filter, bool subtree) = 0;
};

namespace nGZCommandParser {

class cIBaseParser; // the parser's cIBaseParser face: reached through its vtable, its cIGZUnknown through its vbtable (+4)

// The file parser's interface (the game's 0xe7f9d0 order).
class cIFileParser : public cIGZUnknown {
public:
    virtual bool Init() = 0;     // 3 (0xb0dd30)
    virtual bool Shutdown() = 0; // 4 (0xb0df90)
    // 5 (0xb0df30, ret 8): the parser to run the files through; `caseSensitive` goes to its slot 0 as the whole dword.
    virtual void SetParser(cIBaseParser *parser, uint32_t caseSensitive) = 0;
    // 6 (0xb0d5c0, ret 8): adds a file to the list (flags bit 0: it must run without errors; bit 1: watch its directory).
    virtual void AddFile(const char *path, uint32_t flags) = 0;
    virtual bool RunFiles() = 0;                // 7 (0xb0dd40)
    virtual void SetDirty(bool dirty) = 0;      // 8 (folded 0x46e0c0)
    virtual cIBaseParser *GetParser() = 0;      // 9 (folded 0x149d00)
    virtual bool IsDirty() = 0;                 // 10 (folded 0x1bcdd0)
};

// A cRZString held by value (the game's vtable, the text, a zero count), copied as the game's copy constructor copies one
// (the text, a zero count). The file list's entries and the watch key are these.
struct FileString {
    union {
        cRZString value;
    };
    FileString();
    explicit FileString(const std::string &text);
    FileString(const FileString &other);
    FileString &operator=(const FileString &) = delete;
    ~FileString();
    cRZString *RZ() { return &value; }
    cIGZString *Face() { return value.AsInterface(); }
};
static_assert(sizeof(FileString) == 0x20, "FileString: a cRZString");

// One file of the list (0x44 bytes).
struct FileEntry {
    FileString path;     // +0x00 the path as given, made absolute unless it starts with letters and ":/"
    FileString resolved; // +0x20 the source's name for it (its slot 7)
    bool required;       // +0x40 flags bit 0
};
static_assert(sizeof(FileEntry) == 0x44 && offsetof(FileEntry, required) == 0x40, "FileEntry layout");

// The parser pointer: counted through the parser's cIGZUnknown (its vbtable's second entry from the face's +4).
struct ParserRef {
    cIBaseParser *p = nullptr;
    ParserRef() = default;
    ParserRef(const ParserRef &) = delete;
    ParserRef &operator=(const ParserRef &) = delete;
    ~ParserRef();
};

// A watcher pointer, counted (Release when it goes).
struct WatcherRef {
    cIGZPersistDirectoryWatcher *p = nullptr;
    WatcherRef() = default;
    WatcherRef(const WatcherRef &) = delete;
    WatcherRef &operator=(const WatcherRef &) = delete;
    ~WatcherRef();
    void Assign(cIGZPersistDirectoryWatcher *other); // AddRef the new one, Release the old one (nothing when the same)
};

// The stopwatch member, built as the game's constructor builds it (0x1c9030 with mode 5; the dword at +4 is left alone).
struct ParserStopwatch {
    union {
        cRZStopwatch value;
    };
    ParserStopwatch() { value.Construct(5); }
    ParserStopwatch(const ParserStopwatch &) = delete;
    ParserStopwatch &operator=(const ParserStopwatch &) = delete;
    ~ParserStopwatch() {}
};

class cFileParser : public cIFileParser, public cIGZMessageTarget2, public rz::cRZUnknown {
public:
    static constexpr uint32_t kVTable = 0xe7f9d0;        // the game's (11 slots)
    static constexpr uint32_t kTargetVTable = 0xe7fa00;  // cIGZMessageTarget2 face (4 slots)
    static constexpr uint32_t kUnknownVTable = 0xe7fa14; // cRZUnknown face (6 slots)
    static constexpr uint32_t kTargetFace = 4, kUnknownFace = 8;
    static constexpr uint32_t kClassId = 0x5cf9459a;     // the COM class the factory makes
    static constexpr uint32_t kInterfaceId = 0x1cf94583; // what QueryInterface answers itself
    static constexpr uint32_t kChangedMessage = 0x6ae76406; // a watcher's message: the files changed
    static constexpr uint32_t kRanMessage = 0x4b3bc675;     // posted after RunFiles

    // 0xb0dc30 (__cdecl, no arguments): a new file parser's cRZUnknown face (null when the allocation fails).
    static rz::cRZUnknown *__cdecl Create();

    cFileParser() = default; // inline in 0xb0dc30
    // 0xb0d390: Shutdown's work when still initialised, then the members (the parser released last).
    ~cFileParser() override;

    bool QueryInterface(uint32_t riid, void **out) override; // 0xb0def0
    uint32_t AddRef() override;                              // folded 0x1c04c0: cRZUnknown's
    uint32_t Release() override;                             // folded 0x2c7e80: cRZUnknown's

    bool Init() override;                                            // 0xb0dd30
    bool Shutdown() override;                                        // 0xb0df90
    void SetParser(cIBaseParser *parser, uint32_t caseSensitive) override; // 0xb0df30
    void AddFile(const char *path, uint32_t flags) override;         // 0xb0d5c0
    bool RunFiles() override;                                        // 0xb0dd40
    void SetDirty(bool dirty) override;                              // 0x46e0c0 (folded, not hooked)
    cIBaseParser *GetParser() override;                              // 0x149d00 (folded, not hooked)
    bool IsDirty() override;                                         // 0x1bcdd0 (folded, not hooked)

    bool DoMessage(void *message) override;                          // 0xb0dc00

    bool mInitialised = false;                     // +0x10
    ParserRef mParser;                             // +0x14
    std::vector<FileEntry> mFiles;                 // +0x18
    bool mDirty = false;                           // +0x24
    std::map<std::string, WatcherRef> mWatchers;   // +0x28 directory ("X:\dir\") -> its watcher
    ParserStopwatch mStopwatch;                    // +0x30
};

// 0x1c8554 (__thiscall on a cRZFileName): the directory part, split out of the full path first when it is empty and the
// full path is not (the directory alone), then normalised (0x1c86a1); returns it.
cRZString *SplitDirectory(cRZFileName *name);
// 0x1c85c2: the same for the drive (0x1c86fe).
cRZString *SplitDrive(cRZFileName *name);
// 0x1c95dd (__thiscall on a cRZStopwatch): the start and the accumulated count zero.
void ResetStopwatch(cRZStopwatch *stopwatch);

// The RVA hooks' entries: `this` in ecx is the face of the slot (__fastcall with the dummy edx = __thiscall), forwarded with
// qualified calls.
struct cFileParserAdapters {
    static cFileParser *__fastcall DeletingDestructor(cFileParser *self, void *, uint32_t flags);
    static void __fastcall Destructor(cFileParser *self, void *);
    static bool __fastcall QueryInterface(cFileParser *self, void *, uint32_t riid, void **out);
    static bool __fastcall Init(cFileParser *self, void *);
    static bool __fastcall Shutdown(cFileParser *self, void *);
    static void __fastcall SetParser(cFileParser *self, void *, cIBaseParser *parser, uint32_t caseSensitive);
    static void __fastcall AddFile(cFileParser *self, void *, const char *path, uint32_t flags);
    static bool __fastcall RunFiles(cFileParser *self, void *);
    static bool __fastcall DoMessage(cIGZMessageTarget2 *face, void *, void *message);
    static cRZString *__fastcall SplitDirectory(cRZFileName *name, void *);
    static cRZString *__fastcall SplitDrive(cRZFileName *name, void *);
    static void __fastcall ResetStopwatch(cRZStopwatch *stopwatch, void *);
};

} // namespace nGZCommandParser
