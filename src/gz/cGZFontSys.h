#pragma once
// cGZFontSys (lane OA, docs/subsystems/rendering.md R5b): the font system service (class 0x20673960 / iid 0x673960, priority
// 900000). 0x5c bytes, /O1 (the module is built for size).
//
// It keeps every font file it has been told about (AddFontFile, AddFontDirectory, AddFont, LoadFont) as a record: the font
// objects loaded from it (each with the style they were asked for), the family name, style flags, glyph group (1 or 2) and a
// kind. Names are matched lowercased. FindFont turns a comma separated list of family names (aliases resolved) into the
// best record, a score of how far its name, group and style are from the request, and loads or shares a font of that
// size; LoadFont loads a file (a bare name is looked for in the Windows fonts folder) through a new cGZFont (COM class
// 0x1aa404ac) set up with the size, style, encoding and font renderer, and the anti-aliasing mode its size asks.
//
// Faces (RTTI order): +0 cIGZFontSys (vtable 0xe50968, 25 slots), +4 cIGZSystemService (0xe509d0, 13 slots).
//
// Standalone shape (docs/standalone-shape.md): the COM director's creator 0x909fd2 (new, 0x5c bytes, then the constructor
// 0x90f626 by a jump) and the constructor and destructor 0x90f85c are hooked; nothing else references the vtables, so every
// system carries ours and the slots are in effect through their callers.
#include "gz/cIGZFont.h"
#include "gz/cIGZFontSys.h"
#include "gz/cIGZSystemService.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <set>
#include <utility>
#include <vector>

// A cRZString member or local (the game's cRZString, built and destroyed by hand as the original's inlined code does).
struct FontSysString {
    union {
        cRZString s;
    };
    FontSysString() { s.Construct(); }
    explicit FontSysString(const char *text) { s.Construct(text); }
    explicit FontSysString(const cIGZString &other) { s.Construct(other); }
    // The containers' copies: cRZString(const cRZString &, 0, npos) (0x141eb3).
    FontSysString(const FontSysString &other) { s.Construct(other.s, 0, 0xffffffffu); }
    FontSysString &operator=(const FontSysString &) = delete;
    ~FontSysString() { s.Destruct(); }
};
static_assert(sizeof(FontSysString) == 0x20, "cRZString");

// The names' order: the std::strings' operator<.
struct FontSysNameLess {
    using is_transparent = void;
    bool operator()(const FontSysString &a, const FontSysString &b) const { return a.s.mString < b.s.mString; }
    bool operator()(const FontSysString &a, const cRZString &b) const { return a.s.mString < b.mString; }
    bool operator()(const cRZString &a, const FontSysString &b) const { return a.mString < b.s.mString; }
};

// A font of a record and the style it was asked for (the key that finds it again).
struct FontSysElement {
    cIGZFont *font;
    uint32_t style;
};

// One font file the system knows (0x34 bytes; the key of the files map is the file path).
struct FontSysRecord {
    std::list<FontSysElement> fonts; // +0 the loaded fonts (counted)
    FontSysString name;              // +8 the family name
    uint32_t style;                  // +0x28 bit 0 bold, bit 1 italic (more bits from the file)
    uint32_t group;                  // +0x2c 1 or 2: whether the face has a charmap, plus one
    uint32_t kind;                   // +0x30 what the file's header says (the detector 0x910b35)
    FontSysRecord(std::list<FontSysElement> &&list, const FontSysString &family, uint32_t s, uint32_t g, uint32_t k)
        : fonts(std::move(list)), name(family), style(s), group(g), kind(k)
    {
    }
};
static_assert(sizeof(FontSysRecord) == 0x34, "FontSysRecord: 0x34 bytes");

struct FontSysRange {
    uint32_t low, high;
};

class cGZFontSys : public cIGZFontSys, public cIGZSystemService {
public:
    static constexpr uint32_t kSize = 0x5c;
    static constexpr int32_t kPriority = 900000;
    static constexpr uint32_t kFontClass = cIGZFont::kClassId;

    // MSVC 14.40 containers as in the game: nodes of 0x10 bytes and the value (the sizes 0x20, 0x24, 0x54, 0x2c, 0x28).
    using NameSet = std::set<FontSysString, FontSysNameLess>;                     // +0x28 lowercased family names
    using KindMap = std::map<int, FontSysString>;                                 // +0x30 the default face of a kind
    using FileMap = std::map<FontSysString, FontSysRecord, FontSysNameLess>;     // +0x38 by font file path
    using AliasMap = std::map<FontSysString, std::vector<FontSysString>, FontSysNameLess>; // +0x40
    using RangeMap = std::map<FontSysString, FontSysRange, FontSysNameLess>;      // +0x54 size ranges by family

    cGZFontSys();                    // 0x90f626
    virtual ~cGZFontSys();           // 0x90f85c; deleting: slot 24
    cGZFontSys(const cGZFontSys &) = delete;
    cGZFontSys &operator=(const cGZFontSys &) = delete;

    static cGZFontSys *__cdecl Create(); // 0x909fd2: new 0x5c, the constructor

    // ---- cIGZUnknown (both faces) ----
    bool QueryInterface(uint32_t riid, void **out) override; // ids 1 and 0x673960 the primary face, 0x287fb697 the service
    uint32_t AddRef() override;
    uint32_t Release() override;

    // ---- cIGZFontSys (+0) ----
    bool Init() override;       // 0x910db5 (service slot 8: thunk)
    bool Shutdown() override;   // 0x9111e3 (service slot 9: thunk)
    bool SetEncoding(uint32_t encoding) override;
    void SetFontRenderer(cIGZFontRenderer *renderer) override;
    void SetQuality(uint32_t mode) override;
    void SetSizeRange(uint32_t low, uint32_t high, const cIGZString *family) override;
    cIGZFont *GetFontByGroup(uint32_t group, uint32_t size, uint32_t style, uint32_t colour, bool shared) override;
    cIGZFont *LoadFont(const cIGZString *name, uint32_t size, uint32_t style, bool shared) override;
    cIGZFont *FindFont(const cIGZString *names, uint32_t group, uint32_t size, uint32_t style, bool shared) override;
    uint32_t ReleaseFont(cIGZFont *font) override;
    bool AddFont(cIGZFont *font) override;
    bool AddFontFile(const cIGZString *path) override;
    uint32_t AddFontDirectory(const cIGZString *directory) override;
    bool vf16(uint32_t) override { return false; }
    bool RemoveFamily(const cIGZString *name) override;
    uint32_t ListFamilies(cGZFontFamilyInfo *out, uint32_t count) override;
    uint32_t ListFonts(cIGZFont **out, uint32_t capacity) override;
    bool AddAlias(const cIGZString *name, const cIGZString *alias) override;
    uint32_t GetAliases(const cIGZString *name, cIGZString **out, uint32_t count) override;
    bool HasFamily(const cIGZString *name, uint32_t unused, uint32_t style) override;
    bool ResolveAlias(const cIGZString *name, cIGZString *out) override;

    // ---- cIGZSystemService (+4) ----
    uint32_t GetServiceID() override { return mServiceId; }
    void SetServiceID(uint32_t id) override { mServiceId = id; }
    int32_t GetServicePriority() override { return kPriority; }
    bool IsServiceRunning() override { return true; }
    void SetServiceRunning(bool) override {}
    bool OnTick(uint32_t) override { return true; }
    bool OnIdle(uint32_t) override { return false; }
    int32_t GetServiceTickPriority() override { return kPriority; }

    // ---- non-virtual ----
    // 0x910a9d: the default face of a kind (an existing one is replaced).
    bool SetDefaultFace(int kind, const cIGZString &name); // true
    // 0x910b35: the file kind, from the extension, else the font file's header.
    uint32_t DetectKind(const cIGZString &path);

    // +0 and +4: the vtables
    bool mInitialised = false;           // 8
    int32_t mRefCount = 0;               // 0xc
    uint32_t mServiceId = kServiceId;    // 0x10
    uint32_t mQuality = 0;               // 0x14 the anti-aliasing mode of the fonts made (SetQuality)
    uint32_t mSizeLow = 0;               // 0x18 the sizes drawn in mode 1 (SetSizeRange)
    uint32_t mSizeHigh = 0;              // 0x1c
    uint32_t mEncoding;                  // 0x20
    cIGZFontRenderer *mRenderer = nullptr; // 0x24 (counted)
    NameSet mFamilies;                   // 0x28
    KindMap mDefaults;                   // 0x30
    FileMap mFiles;                      // 0x38
    AliasMap mAliases;                   // 0x40
    std::vector<FontSysString> mPatterns; // 0x48 the file patterns AddFontDirectory scans for
    RangeMap mRanges;                    // 0x54
};

static_assert(sizeof(cGZFontSys) == cGZFontSys::kSize, "cGZFontSys size");
static_assert(offsetof(cGZFontSys, mInitialised) == 8 && offsetof(cGZFontSys, mRefCount) == 0xc &&
                  offsetof(cGZFontSys, mServiceId) == 0x10 && offsetof(cGZFontSys, mQuality) == 0x14 &&
                  offsetof(cGZFontSys, mSizeLow) == 0x18 && offsetof(cGZFontSys, mSizeHigh) == 0x1c &&
                  offsetof(cGZFontSys, mEncoding) == 0x20 && offsetof(cGZFontSys, mRenderer) == 0x24,
              "cGZFontSys layout");
static_assert(offsetof(cGZFontSys, mFamilies) == 0x28 && offsetof(cGZFontSys, mDefaults) == 0x30 &&
                  offsetof(cGZFontSys, mFiles) == 0x38 && offsetof(cGZFontSys, mAliases) == 0x40 &&
                  offsetof(cGZFontSys, mPatterns) == 0x48 && offsetof(cGZFontSys, mRanges) == 0x54,
              "cGZFontSys containers");

// The hooks' adapters: the constructor 0x90f626 (ecx = the memory) and the destructor 0x90f85c (ecx = the object).
struct cGZFontSysAdapters {
    static cGZFontSys *__fastcall Construct(void *memory, void *);
    static void __fastcall Destruct(cGZFontSys *self, void *);
};
