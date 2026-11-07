#pragma once
// cHitDebugInfo (lane LH): the HIT (sound script) debug information service (system service id 0xfbc9cd04, priority 0
// at construction, GetServicePriority 499998; cIHitDebugInfo iid 0x7bba0cbd). 0x58 bytes, /O1 (cHitDebugInfo.size.cpp).
// RVAs 1.18.0.187. It keeps a list of modules (a compiled HIT script: its source file name, the base address and size of
// its code) each with a table of {source line, code offset} pairs, loaded from a ".HDB" text file next to the script
// ("SOURCEFILE <name>" starts a module, "{line,offset}" adds a pair); the slots map a line to an address and an address
// back to its file and line. Two slots ask the hit manager service (0x1b2ce671) for an address.
//
// Faces (RTTI order): +0 cIHitDebugInfo (vtable 0xe82080, 12 slots), +4 cRZSystemService / cIGZSystemService (0xe820b4,
// 13: slot 9 Shutdown's adjustor thunk 0xb31c78), +8 cRZUnknownMT (0xe820ec, 6: slot 3 the deleting destructor's thunk
// 0xb31637). The count the object lives by is its own, at +0x20 (AddRef 0x85c074 is a folded non-interlocked increment;
// cRZUnknownMT's count at +0xc is never used).
//
// Standalone shape (docs/standalone-shape.md): the factory 0xb2a30c (hooked) is registered by address with the hit
// manager's COM director (class 0x3bc9e6e0, cHitManCOMDirector 0xb29d78) and is the only caller of the constructor
// 0xb31526; the vtables are referenced only by the constructor and the destructor 0xb315a6, so every object carries ours
// (db/status/cHitDebugInfo.jsonl). The modules (HitDebugModule) are made only by AddModule.
#include "gz/cIGZString.h"
#include "gz/cIGZSystemService.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZSystemServiceBase.h"
#include "ts/cTSCustomContentSupport.h" // ContentString

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <list>

// cIHitDebugInfo (iid 0x7bba0cbd): slots 3-11.
class cIHitDebugInfo : public cIGZUnknown {
public:
    // 3 (shared 0x144910): true. The hit manager director's PreAppShutdown calls it; it also overrides
    // cIGZSystemService::Shutdown (the +4 face's slot 9 is its thunk 0xb31c78).
    virtual bool Shutdown() = 0;
    // 4 (0xb31681, ret 0xc): a new module {name, base, size} put first in the list (0x1c4609 inserts before the head's next node) and made the current one.
    virtual void AddModule(const cIGZString &name, uint32_t base, uint32_t size) = 0;
    // 5 (0xb3176a, ret 0xc): the address of `line`: the first module (in list order) that has the line gives its base plus
    // the line's offset; false when none has it (or the offset is 0). `file` is not compared (see the body).
    virtual bool FindLineAddress(const cIGZString &file, int32_t line, uint32_t *address) = 0;
    // 6 (0xb317d5, ret 0xc): the first module whose code holds `address` and has a line for it: its file name copied into
    // `file`, the line into `line`.
    virtual bool FindLine(uint32_t address, cIGZString *file, int32_t *line) = 0;
    // 7 (0xb31711, ret 8) / 8 (0xb31817, ret 8): the hit manager's slot 14 asked for `key` (an object and an offset); 7: the
    // object's slot 3 (a base) plus the offset into `address`, unless the base is 0; 8: the object's slot 6 given `value`.
    virtual bool HitManAddress(uint32_t key, uint32_t *address) = 0;
    virtual bool HitManSet(uint32_t key, uint32_t value) = 0;
    // 9 (0xb318de, ret 0xc): the ".HDB" file beside `path` (its drive and directory, its name, ".HDB") read line by line;
    // modules (named by the directory plus the SOURCEFILE name) get `base` and `size`. False when the file does not open.
    virtual bool LoadLineInfo(const cIGZString &path, uint32_t base, uint32_t size) = 0;
    // 10 (0xb31b28, ret 4): the file `path` read line by line and scanned as "%s %ld (" into nothing. False when it does
    // not open; the file is never closed (ORIGINAL-BUG).
    virtual bool ReadSymbolFile(const cIGZString &path) = 0;
    // 11 (0xb316ea, ret 8): {line, offset} appended to the current module's lines.
    virtual void AddLine(int32_t line, int32_t offset) = 0;
};

// One {line, offset} pair (std::list node 0x10: links, the line at +8, the offset at +0xc).
struct HitDebugLine {
    int32_t line;
    int32_t offset;
};
using HitDebugLines = std::list<HitDebugLine>;
static_assert(sizeof(HitDebugLines) == 8, "MSVC 14.40 list");

// A module (0x30 bytes, operator new): its source file name, the code's base address and size, its lines.
struct HitDebugModule {
    // 0xb31577 (ret 0xc): the name assigned through the string's own slot 16 (operator=).
    HitDebugModule(const cIGZString &name, uint32_t base, uint32_t size);
    // 0xb3160d: the lines, then the name.
    ~HitDebugModule() = default;
    HitDebugModule(const HitDebugModule &) = delete;
    HitDebugModule &operator=(const HitDebugModule &) = delete;

    // 0xb316f2 (ret 4): the base plus the offset of `line`, 0 when the line has none (or its offset is 0).
    uint32_t LineAddress(int32_t line) const;
    // 0xb31bc4 (ret 4): the offset of the first pair with `line`, 0 for none.
    int32_t LineOffset(int32_t line) const;
    // 0xb3186b (ret 4): the line at `address` (LineAtOffset of its offset), 0 outside [base, base + size) (unsigned, the
    // end wrapping at 32 bits).
    int32_t LineAt(uint32_t address) const;
    // 0xb3188e (ret 4): the line of the first pair, in list order, by which some pair so far had an offset <= `offset` and
    // some pair so far an offset >= it (signed); 0 for none.
    int32_t LineAtOffset(int32_t offset) const;

    ContentString mName; // +0x00
    uint32_t mBase;      // +0x20
    uint32_t mSize;      // +0x24
    HitDebugLines mLines; // +0x28 (pushed at the back by 0x90f1db)
};
static_assert(sizeof(HitDebugModule) == 0x30, "new 0x30 in AddModule");
static_assert(offsetof(HitDebugModule, mBase) == 0x20 && offsetof(HitDebugModule, mSize) == 0x24 &&
                  offsetof(HitDebugModule, mLines) == 0x28,
              "HitDebugModule layout");

using HitDebugModules = std::list<HitDebugModule *>; // newest first (AddModule)
static_assert(sizeof(HitDebugModules) == 8, "MSVC 14.40 list");

class cHitDebugInfo : public cIHitDebugInfo, public rz::cRZSystemService {
public:
    static constexpr uint32_t kVTable = 0xe82080;
    static constexpr uint32_t kServiceVTable = 0xe820b4;
    static constexpr uint32_t kUnknownVTable = 0xe820ec;
    static constexpr uint32_t kSize = 0x58;
    static constexpr uint32_t kServiceId = 0xfbc9cd04;
    static constexpr int32_t kPriority = 0;
    static constexpr int32_t kServicePriority = 499998; // GetServicePriority 0xb31865
    static constexpr uint32_t kIid = 0x7bba0cbd;        // cIHitDebugInfo
    static constexpr uint32_t kHitManId = 0x1b2ce671;   // the hit manager service
    static constexpr uint32_t kHitManIid = 0xbb1bd9a8;

    cHitDebugInfo();                    // 0xb31526
    ~cHitDebugInfo() noexcept override; // 0xb315a6 (deleting 0xb3163f; +8 thunk 0xb31637)
    cHitDebugInfo(const cHitDebugInfo &) = delete;
    cHitDebugInfo &operator=(const cHitDebugInfo &) = delete;

    // 0xb2a30c (__cdecl): the COM director's factory: a new service's cIGZSystemService face (+4), null when new failed.
    static cIGZSystemService *__cdecl Create();

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0xb31bed; thunks 0xb31c3e (+4), 0xb31c46 (+8)
    uint32_t AddRef() override;                              // folded 0x85c074 (+0x20); thunks 0x85c07c, 0x96cdf1
    uint32_t Release() override;                             // 0xb31c4e; thunks 0xb31c68 (+4), 0xb31c70 (+8)

    // ---- cIGZSystemService ----
    int32_t GetServicePriority() override; // 0xb31865

    // ---- cIHitDebugInfo (and cIGZSystemService::Shutdown) ----
    bool Shutdown() override; // shared 0x144910; +4 thunk 0xb31c78
    void AddModule(const cIGZString &name, uint32_t base, uint32_t size) override;
    bool FindLineAddress(const cIGZString &file, int32_t line, uint32_t *address) override;
    bool FindLine(uint32_t address, cIGZString *file, int32_t *line) override;
    bool HitManAddress(uint32_t key, uint32_t *address) override;
    bool HitManSet(uint32_t key, uint32_t value) override;
    bool LoadLineInfo(const cIGZString &path, uint32_t base, uint32_t size) override;
    bool ReadSymbolFile(const cIGZString &path) override;
    void AddLine(int32_t line, int32_t offset) override;

    // The C runtime calls of the two file readers (the original's fopen 0x83aa0f, fgets 0xc4b542, sscanf 0x242ec5, fclose
    // 0x838ff4): the CRT's, replaceable by a test.
    static FILE *(__cdecl *sOpen)(const char *path, const char *mode);
    static char *(__cdecl *sGets)(char *buffer, int size, FILE *file);
    static int(__cdecl *sScan)(const char *text, const char *format, ...);
    static int(__cdecl *sClose)(FILE *file);

    // +0x00 cIHitDebugInfo vtable; +0x04 .. +0x1f cRZSystemService
    uint32_t mCount;           // +0x20 the reference count
    HitDebugModules mModules;  // +0x24
    HitDebugModule *mCurrent;  // +0x2c the module AddLine adds to (the last added)
    ContentString mString30;   // +0x30 constructed and destroyed only
    uint32_t mWord50;          // +0x50 0, never read
    uint32_t mWord54;          // +0x54 0, never read
};
static_assert(sizeof(cHitDebugInfo) == cHitDebugInfo::kSize, "new 0x58 in the factory");
static_assert(offsetof(cHitDebugInfo, mCount) == 0x20 && offsetof(cHitDebugInfo, mModules) == 0x24 &&
                  offsetof(cHitDebugInfo, mCurrent) == 0x2c && offsetof(cHitDebugInfo, mString30) == 0x30 &&
                  offsetof(cHitDebugInfo, mWord50) == 0x50 && offsetof(cHitDebugInfo, mWord54) == 0x54,
              "cHitDebugInfo layout");
