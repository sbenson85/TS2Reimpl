#pragma once
// nGZCommandParser::cFileSource, where the command parser reads its files from (lane IM): it answers whether a path is
// one it serves, whether it exists, reads a file's text, and resolves a path against the file that names it. Every parser
// makes one (cBaseParser's constructor 0xafd800 and SetSource 0xb06140 with a null source, both ours), so the class is in
// the standalone shape (docs/standalone-shape.md): our vtables on every object.
//
// RTTI: cFileSource derives from cIParserSource (+0, itself a cIGZUnknown; the game's vtable 0xe7dc30, 8 slots) and
// cRZRefCount (+4, the count at +8; vtable 0xe7dc54, 5 slots). AddRef and Release of the first face forward to the
// count's (0x1dab20 `add ecx, 4; jmp 0x148fb0`, 0x5b7980 `add ecx, 4; jmp 0x1b94af`); the count's face reaches the
// class's overrides through the compiler's thunks (0x683fab, 0x1dab28) and its destructor through 0xaff047. RVAs are
// 1.18.0.187.
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "gz/cGZDBSegmentPackedFile.h" // GZServiceAllocator
#include "rz/RZRefCountBase.h"

#include <cstdint>
#include <vector>

class cRZFileName;
class cRZString;

namespace nGZCommandParser {

// A file's bytes as the parser's RunFile hands them over (its memory from the allocator service), one zero past the
// text.
using FileData = std::vector<char, GZServiceAllocator<char>>;

// The parser's view of where files come from (the game's own interface vtable 0xe7dae8 is all _purecall).
class cIParserSource : public cIGZUnknown {
public:
    // 3 (ret 4): whether this source serves `path` (RunFile refuses the others).
    virtual bool AcceptsPath(const char *path) = 0;
    // 4 (ret 4): whether `path` names an existing file.
    virtual bool Exists(const char *path) = 0;
    // 5 (ret 8): the file's bytes into `out` (resized to its length plus a zero); false when it cannot be opened.
    virtual bool ReadFile(const char *path, FileData *out) = 0;
    // 6 (ret 0xc): `out` = `path` resolved against the directory of the file `relativeTo`.
    virtual void ResolvePath(const char *path, const char *relativeTo, cIGZString *out) = 0;
    // 7 (ret 8): `out` = `path` as this source names it (here unchanged); true.
    virtual bool CanonicalPath(const char *path, cIGZString *out) = 0;
};

class cFileSource : public cIParserSource, public rz::cRZRefCount {
public:
    static constexpr uint32_t kVTable = 0xe7dc30;         // the game's (8 slots)
    static constexpr uint32_t kRefCountVTable = 0xe7dc54; // the game's cRZRefCount face (5 slots)
    static constexpr uint32_t kRefCountFace = 4;
    static constexpr uint32_t kInterfaceId = 0x4d8241bd;  // what QueryInterface answers besides 1

    cFileSource() = default; // inline in 0xafd800 and 0xb06140: the vtables and a zero count
    // 0xaff0a0 (slot 0 of the count's face through the thunk 0xaff047): the deleting destructor.
    ~cFileSource() override;

    // 0xb059b0 (folded with another class's slot 0, not hooked): this for ids 1 and 0x4d8241bd, counted through slot 1.
    bool QueryInterface(uint32_t riid, void **out) override;
    uint32_t AddRef() override;  // 0x1dab20 (folded): the count's AddRef
    uint32_t Release() override; // 0x5b7980 (folded): the count's Release
    bool AcceptsPath(const char *path) override;                                      // 0xb05490
    bool Exists(const char *path) override;                                           // 0xb02370
    bool ReadFile(const char *path, FileData *out) override;                          // 0xb035e0
    void ResolvePath(const char *path, const char *relativeTo, cIGZString *out) override; // 0xb05ec0
    bool CanonicalPath(const char *path, cIGZString *out) override;                   // 0xb02720
};
static_assert(sizeof(cFileSource) == 12, "cFileSource: two vtables and the count");

// 0x1c8789 (__thiscall on a cRZFileName, ret 4; its only caller is ResolvePath): the full path (+0x80) = `path` resolved
// to an absolute one (ResolvePathString with no base), then split into the parts (0x1c8beb).
void SetFileNamePath(cRZFileName *name, const cRZString *path);
// 0x1c8beb (__thiscall on a cRZFileName): splits the full path into drive, directory, name and extension unless it is
// empty.
void SplitFileNamePath(cRZFileName *name);

// The RVA hooks' entries: `this` in ecx is the face of the slot (__fastcall with the dummy edx = __thiscall), forwarded with
// qualified calls (objects carrying the game's vtables: none once our constructor makes every source).
struct cFileSourceAdapters {
    static cFileSource *__fastcall DeletingDestructor(cFileSource *self, void *, uint32_t flags);
    static bool __fastcall AcceptsPath(cFileSource *self, void *, const char *path);
    static bool __fastcall Exists(cFileSource *self, void *, const char *path);
    static bool __fastcall ReadFile(cFileSource *self, void *, const char *path, FileData *out);
    static void __fastcall ResolvePath(cFileSource *self, void *, const char *path, const char *relativeTo, cIGZString *out);
    static bool __fastcall CanonicalPath(cFileSource *self, void *, const char *path, cIGZString *out);
    static void __fastcall SetFileNamePath(cRZFileName *name, void *, const cRZString *path);
    static void __fastcall SplitFileNamePath(cRZFileName *name, void *);
};

} // namespace nGZCommandParser
