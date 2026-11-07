#pragma once
// nGZSceneGraph's level-of-detail table (lane QS, scenegraph D3 part 3): which LOD level a model of a given on-screen size
// gets. RVAs are 1.18.0.187.
//
// cLODLevelsTable (0x1cc bytes) is a cBaseParser (src/gz/cBaseParser.h) with one command, "LOD <level> <size>", that
// collects {size, level} pairs. It is hook-shaped like its base: the game's vtables (primary 0xe767b8, cIGZUnknown face
// 0xe767d0 at +0x1bc, cIBaseParser face 0xe767e0 at +0x1c4) are installed by the constructor, and their slots are hooked:
//   +0x000 the cBaseParser part (0..0x1ac; its vbptr at +8 is the table's own, 0xe768ac {-8, 0x1b4, 0x1bc})
//   +0x1ac mLevels   std::vector<LODLevel> (our toolchain's; nothing else touches it)
//   +0x1b8 the cIGZUnknown face's vtordisp, +0x1bc its vtable
//   +0x1c0 the cIBaseParser face's vtordisp, +0x1c4 its vtable, +0x1c8 its vbptr (0xe768b8)
// The faces' slots are thunks `sub ecx, [ecx-4]; sub ecx, 0xc; jmp body`: they land on cBaseParser's bodies with `this`
// where cBaseParser's own faces sit (+0x1b0 / +0x1b8), so they are hooked to forwarders to cBaseParserAdapters
// (TableShiftedThunk below). Three cIBaseParser slots have bodies of the table's own (Initialise 0, Shutdown 1, Reset 9;
// reached through `sub ecx, [ecx-4]` thunks).
//
// cLODLevelsStore (0x1c bytes; COM class 0x2c674b2c, interface 0x2c674b25, made by the director's factory 0xab6f40)
// owns a table and the cFileParser that runs the LOD file through it, and answers LOD queries; it watches two settings
// ("useLODs", "lodOverride") as a cIGZMessageTarget2. Standalone shape (docs/standalone-shape.md): the factory is hooked
// to Create, so our vtables are on every store.
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nGZCommandParser {
class cBaseParser;
class cIFileParser;
class cCommandT;
class cIArguments;
} // namespace nGZCommandParser

namespace nGZSceneGraph {

// One row of the table: models whose size key is at most `size` get `level`.
struct LODLevel {
    int32_t size;  // +0 the sort key
    int32_t level; // +4 at most 30
};
static_assert(sizeof(LODLevel) == 8, "LODLevel layout");

class cLODLevelsTable {
public:
    static constexpr uint32_t kSize = 0x1cc;
    static constexpr uint32_t kVTable = 0xe767b8;           // primary (slot 0 the deleting destructor)
    static constexpr uint32_t kUnknownVTable = 0xe767d0;    // the cIGZUnknown face
    static constexpr uint32_t kParserVTable = 0xe767e0;     // the cIBaseParser face
    static constexpr uint32_t kVbTable = 0xe768ac;          // {-8, 0x1b4, 0x1bc}
    static constexpr uint32_t kParserFaceVbTable = 0xe768b8;
    static constexpr int32_t kLevelLimit = 30;              // the LOD command's largest level

    // Our copies of those tables (sg/cLODLevelsTables.cpp): the owned statics ts2_rva_e767b8 ... are aliased to them in an
    // image-free build (standalone/owned_statics.cpp); the hooked game keeps the image's.
    static const void *const kOwnVTable[5];         // kVTable
    static const void *const kOwnUnknownVTable[3];  // kUnknownVTable (TableShiftedThunk forwarders)
    static const void *const kOwnParserVTable[51];  // kParserVTable (the hook table's forwarders, slot order)
    static const int32_t kOwnVbTable[3];            // kVbTable
    static const int32_t kOwnParserFaceVbTable[2];  // kParserFaceVbTable

    // 0xab6b00 (__thiscall, ret 4, returns this): the virtual bases' own vtables when `mostDerived`, cBaseParser's
    // constructor (with 0), the table's vtables, both vtordisps zero, an empty vector.
    cLODLevelsTable *Construct(uint32_t mostDerived);
    // 0xab6fa0 (__cdecl): a new table's cIGZUnknown face (null when the allocation fails).
    static void *__cdecl Create();
    // 0xab6c50, primary slot 0: the vector freed, cBaseParser's destructor, then the memory (0x1cc bytes) when bit 0
    // of `flags` is set.
    cLODLevelsTable *DeletingDestructor(uint32_t flags);

    // The bodies of the cIBaseParser face's slots 0, 1 and 9 (on the table).
    bool Initialise(uint8_t caseSensitive); // 0xab7210: the parser's, then the LOD command registered (slot 4)
    bool Shutdown();                        // 0xab7960: the parser's, then the levels emptied
    void Reset();                           // 0xab6d10: the parser's, then the levels emptied

    nGZCommandParser::cBaseParser *Parser() { return reinterpret_cast<nGZCommandParser::cBaseParser *>(this); }
    const int32_t *VbTable() const { return *reinterpret_cast<const int32_t *const *>(mBase + 8); }
    void *UnknownFace() { return mBase + 8 + VbTable()[1]; }
    void *ParserFace() { return mBase + 8 + VbTable()[2]; }
    static cLODLevelsTable *FromParserFace(void *face) { return reinterpret_cast<cLODLevelsTable *>(static_cast<char *>(face) - 0x1c4); }

    alignas(4) uint8_t mBase[0x1ac];      // +0x000 the cBaseParser part
    std::vector<LODLevel> mLevels;        // +0x1ac
    int32_t mUnknownVtorDisp;             // +0x1b8
    const void *mUnknownFaceVTable;       // +0x1bc
    int32_t mParserVtorDisp;              // +0x1c0
    const void *mParserFaceVTable;        // +0x1c4
    const int32_t *mParserFaceVbTable;    // +0x1c8
};
static_assert(offsetof(cLODLevelsTable, mLevels) == 0x1ac && offsetof(cLODLevelsTable, mUnknownFaceVTable) == 0x1bc &&
                  offsetof(cLODLevelsTable, mParserFaceVTable) == 0x1c4 && sizeof(cLODLevelsTable) == 0x1cc,
              "cLODLevelsTable layout");

// The LOD command (a cCommandT, 16 bytes, vtable 0xe76914; its other slots are cCommandT's shared bodies). Its state
// is the table (SetContextAdjusted takes 4 off the parser's cState at +4).
class cLODCommand {
public:
    static constexpr uint32_t kVTable = 0xe76914;
    static constexpr uint32_t kName = 0xe6f73c; // "LOD"

    // Slot 4, 0xab6e90 (ret 4): "LOD <level> <size>": both evaluated as integers (size first), the level capped at
    // 30, the pair appended to the table.
    void Execute(nGZCommandParser::cIArguments *args);
    // Slot 5, 0xab7320: the name, from the game's string data.
    const char *GetName() const;

    // Our copy of the vtable (sg/cLODLevelsTables.cpp; ts2_rva_e76914 in an image-free build): cCommandT's slots around
    // the two above.
    static const void *const kOwnVTable[10];

    const void *mVTable;     // +0
    void *mParser;           // +4
    int32_t mRefCount;       // +8
    cLODLevelsTable *mTable; // +0xc the state
};
static_assert(sizeof(cLODCommand) == 0x10, "cLODCommand layout");

// The hooks' entries (ecx is the face the slot was called on: __fastcall with an unused edx).
struct cLODLevelsTableAdapters {
    static cLODLevelsTable *__fastcall Construct(cLODLevelsTable *self, void *, uint32_t mostDerived);
    static cLODLevelsTable *__fastcall DeletingDestructor(cLODLevelsTable *self, void *, uint32_t flags);
    static bool __fastcall Initialise(void *face, void *, uint8_t caseSensitive); // the body: face +0x1c4
    static bool __fastcall Shutdown(void *face, void *);
    static void __fastcall Reset(void *face, void *);
    static void __fastcall NoOp(void *face, void *);         // slot 11's body: the folded bare `ret` 0x140fe0
    static int32_t __fastcall GetVersion(void *face, void *); // slot 45's body: the folded 0x9d6630 (cBaseParser +0xf0)
    static void __fastcall Execute(cLODCommand *self, void *, nGZCommandParser::cIArguments *args);
    static const char *__fastcall GetName(cLODCommand *self, void *);
};

// The face's thunk: the face less its vtordisp, less 0xc more (where cBaseParser's own face of that kind sits), handed to
// the body's adapter.
inline void *TableBodyThis(void *face)
{
    char *at = static_cast<char *>(face);
    return at - *reinterpret_cast<const int32_t *>(at - 4) - 0xc;
}
template <auto Adapter> struct TableShiftedThunk;
template <class R, class... A, R(__fastcall *Adapter)(void *, void *, A...)> struct TableShiftedThunk<Adapter> {
    static R __fastcall Call(void *face, void *, A... args) { return Adapter(TableBodyThis(face), nullptr, args...); }
};

// cILODLevelsStore (interface 0x2c674b25; the game's vtable 0xe768c4 order).
class cILODLevelsStore : public cIGZUnknown {
public:
    static constexpr uint32_t kIID = 0x2c674b25;

    virtual cIGZMessageTarget2 *GetMessageTarget() = 0;  // 3 (folded 0x1c04e0: this + 4, null stays null)
    virtual cIGZMessageTarget2 *GetMessageTarget2() = 0; // 4 (the same body)
    virtual bool Init() = 0;                             // 5
    virtual bool Shutdown() = 0;                         // 6
    virtual void LoadFile(cIGZString *path) = 0;         // 7: runs the file into the table, sorted by size
    virtual void Update() = 0;                           // 8: reruns the files when the file parser is dirty
    virtual int32_t GetLevel(float width, float height) = 0; // 9: the level for a size (the larger, truncated)
    virtual bool IsEnabled() = 0;                        // 10: LODs on and the table not empty
    virtual int32_t GetOverride() = 0;                   // 11 (folded 0x149cf0: +0x10)
    virtual ~cILODLevelsStore() = default;               // 12: the deleting destructor
    virtual void Refresh() = 0;                          // 13: every scene node told the LOD settings changed
};

class cLODLevelsStore : public cILODLevelsStore, public cIGZMessageTarget2 {
public:
    static constexpr uint32_t kClassId = 0x2c674b2c;
    static constexpr uint32_t kUseLODsSetting = 0xcc6887ab;
    static constexpr uint32_t kOverrideSetting = 0x8c7df62a;

    // 0xab6f40 (__cdecl): a new store (uncounted; null when the allocation fails).
    static cILODLevelsStore *__cdecl Create();

    cLODLevelsStore() = default;     // 0xab6aa0
    ~cLODLevelsStore() override;     // 0xab6bc0 (the deleting destructor 0xab6c30)

    bool QueryInterface(uint32_t riid, void **out) override; // 0xab7460
    uint32_t AddRef() override;                              // folded 0x145260
    uint32_t Release() override;                             // folded 0x313300

    cIGZMessageTarget2 *GetMessageTarget() override;
    cIGZMessageTarget2 *GetMessageTarget2() override;
    bool Init() override;                          // 0xab7030
    bool Shutdown() override;                      // 0xab7810
    void LoadFile(cIGZString *path) override;      // 0xab7780
    void Update() override;                        // 0xab7340
    int32_t GetLevel(float width, float height) override; // 0xab72b0
    bool IsEnabled() override;                     // 0xab6ce0
    int32_t GetOverride() override;                // folded 0x149cf0
    void Refresh() override;                       // 0xab74a0

    bool DoMessage(void *message) override;        // 0xab6d60 (the cIGZMessageTarget2 face)

    uint32_t mRefCount = 0;                                // +0x08
    uint8_t mUseLODs = 1;                                  // +0x0c the setting's byte as it came (0 off)
    int32_t mOverride = -1;                                // +0x10
    nGZCommandParser::cIFileParser *mFileParser = nullptr; // +0x14 counted
    cLODLevelsTable *mTable = nullptr;                     // +0x18 counted (through its cIGZUnknown face)

private:
    void SortLevels();
};
static_assert(sizeof(cLODLevelsStore) == 0x1c, "cLODLevelsStore layout");

} // namespace nGZSceneGraph
