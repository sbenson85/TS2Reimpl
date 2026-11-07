#pragma once
// nGZSceneGraph::cMaterialParser (lane RH, scenegraph M1b): the parser that reads the game's material definition text
// (the "MaterialDefinition", "Material", "DefaultState", "ShaderProgram" blocks and the device-capability functions).
// RVAs are 1.18.0.187.
//
// A cBaseParser (src/gz/cBaseParser.h) with three more virtual faces. Its vtables and vbtables are ours (lane RM): hand-built
// tables in cMaterialParser.cpp (OwnVTables below) that the constructor installs; every slot holds one of our functions (the
// bodies' adapters, or the vtordisp / shifted thunks that reach them). The game's tables (the k...VTable constants) are kept
// for the tests, which compare ours with them. The image's thunks stay hooked (src/hook/tables/cMaterialParser.cpp) for
// code that still reaches them.
//   +0x000 the cBaseParser part (0..0x1ac; its vbptr at +8 is this class's own, 0xe79f4c {-8, 0x2e0, 0x2e8, 0x2f4})
//   +0x1ac .. +0x2e4 the members (below)
//   +0x2e4 the cIGZUnknown face's vtordisp, +0x2e8 its vtable (0xe79e48, 3 slots)
//   +0x2ec the cIBaseParser face's vtordisp, +0x2f0 its vtable (0xe79e58, 51 slots)
//   +0x2f4 a vbptr (0xe79f5c {-4, -0xc}), +0x2f8 the cIMaterialParser face's vtordisp, +0x2fc its vtable (0xe79f28, 9 slots)
//   +0x300 the cIMaterialParser face's vbptr (0xe79f64 {-4, -0x18, -0x10})
// The primary vtable is 0xe79e30 (5 slots; slot 0 the deleting destructor). The faces' slots are thunks
// `sub ecx, [ecx-4]; sub ecx, 0x138; jmp body`: they land on cBaseParser's bodies with `this` where cBaseParser's own faces
// sit (+0x1b0 / +0x1b8), so they are hooked to forwarders to cBaseParserAdapters (ParserShiftedThunk below). The slots with
// bodies of this class's own (8-byte `sub ecx, [ecx-4]; jmp body` thunks) are hooked to VtordispThunk forwarders.
//
// Members: three owned services (a counted-pointer vector, two service cells), COM pointers released by the reset, lists
// and strings the material commands fill, two maps keyed by service cell. Their purposes belong to the bodies of part 2
// (docs/subsystems/scenegraph.md); here they have the shape the constructor, the destructor and the resets give them.
#include "rz/StaticGetters2.h" // ServiceCell
#include "simantics/cTSBehaviorTree.h" // RefPtrVector

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace nGZCommandParser {
class cBaseParser;
}

namespace nGZSceneGraph {

// A service cell that closes itself (ServiceCell::Close, 0xaeb760) when it goes: the element type of the destructor's
// containers. Ordered by the handle, unsigned.
struct OwnedCell {
    ServiceCell cell;
    OwnedCell() { cell.mValue = 0xffffffffu; }
    explicit OwnedCell(uint32_t handle) { cell.mValue = handle; }
    OwnedCell(const OwnedCell &other);            // 0xaeb6f0: the same handle, counted again
    OwnedCell &operator=(const OwnedCell &other); // ServiceCell::Assign: the new handle counted, the old one dropped
    ~OwnedCell() { cell.Close(); }
};
static_assert(sizeof(OwnedCell) == 4, "OwnedCell layout");

// The maps' order: the handle, unsigned. Transparent, so a lookup by handle makes no cell (a cell closes when it goes).
struct CellLess {
    using is_transparent = void;
    bool operator()(const OwnedCell &a, const OwnedCell &b) const { return a.cell.mValue < b.cell.mValue; }
    bool operator()(const OwnedCell &a, uint32_t b) const { return a.cell.mValue < b; }
    bool operator()(uint32_t a, const OwnedCell &b) const { return a < b.cell.mValue; }
};

struct CellPair {
    OwnedCell first, second;
};
static_assert(sizeof(CellPair) == 8, "CellPair layout");

struct Blob12 {
    uint32_t words[3];
};

// The elements of the list at +0x1ec: 0x34 bytes (slot 4 counts them so); their fields are not known yet.
struct Record34 {
    uint32_t words[13];
};
static_assert(sizeof(Record34) == 0x34, "Record34 layout");

// What material slot 4 fills in (0x18 bytes; the last three bytes are left as they were).
struct MaterialDefinitionInfo {
    void *definition;          // +0x00 the object at +0x1dc (not counted)
    uint32_t count1e0;         // +0x04 the list at +0x1e0
    const uint32_t *list1e0;   // +0x08 null when empty
    uint32_t count1ec;         // +0x0c the list at +0x1ec
    const Record34 *list1ec;   // +0x10 null when empty
    uint8_t flag1f8;           // +0x14
};
static_assert(sizeof(MaterialDefinitionInfo) == 0x18, "MaterialDefinitionInfo layout");

class cMaterialParser {
public:
    static constexpr uint32_t kSize = 0x304;
    static constexpr uint32_t kVTable = 0xe79e30;            // primary (slot 0 the deleting destructor)
    static constexpr uint32_t kUnknownVTable = 0xe79e48;     // the cIGZUnknown face
    static constexpr uint32_t kParserVTable = 0xe79e58;      // the cIBaseParser face
    static constexpr uint32_t kMaterialVTable = 0xe79f28;    // the cIMaterialParser face
    static constexpr uint32_t kVbTable = 0xe79f4c;           // {-8, 0x2e0, 0x2e8, 0x2f4}
    static constexpr uint32_t kFace2VbTable = 0xe79f5c;      // {-4, -0xc}
    static constexpr uint32_t kMaterialFaceVbTable = 0xe79f64; // {-4, -0x18, -0x10}
    // What the virtual bases' own constructors store (most derived only) besides cBaseParser's.
    static constexpr uint32_t kMaterialBaseVTable = 0xe79d10;     // cIMaterialParser's own (at +0x2fc)
    static constexpr uint32_t kMaterialBaseUnknownVTable = 0xe79d38; // its view of the cIGZUnknown (at +0x2e8)
    static constexpr uint32_t kMaterialBaseParserVTable = 0xe79d48;  // its view of the cIBaseParser (at +0x2f0)
    static constexpr uint32_t kMaterialIID = 0x4959875c;
    // Our tables (cMaterialParser.cpp), the same shape as the game's above: slot counts, vbtable words.
    static const void *const kOwnVTable[5];
    static const void *const kOwnUnknownVTable[3];
    static const void *const kOwnParserVTable[51];
    static const void *const kOwnMaterialVTable[9];
    static const int32_t kOwnVbTable[4];
    static const int32_t kOwnFace2VbTable[2];
    static const int32_t kOwnMaterialFaceVbTable[3];

    // 0xacec90 (__thiscall, ret 4, returns this): the virtual bases' vtables when `mostDerived`, cBaseParser's constructor
    // (with 0), this class's vtables, the three vtordisps zero, the members (the two maps' head nodes allocated).
    cMaterialParser *Construct(uint32_t mostDerived);
    // 0xacf040 (__thiscall, plain ret): the vtables again, the members destroyed in reverse order, cBaseParser's destructor.
    void Destruct();
    // 0xacfd90, primary slot 0 (ret 4): Destruct, then the memory (0x304 bytes) when bit 0 of `flags` is set.
    cMaterialParser *DeletingDestructor(uint32_t flags);

    // The bodies of the cIBaseParser face's slots 0 and 10 (on the object), of the cIMaterialParser face's slots 3, 5 and 8
    // and of the cIGZUnknown face's slot 0.
    bool Initialise(uint8_t caseSensitive); // 0xad8ba0: the parser's, versions 1..4, 9 commands and 14 functions registered
    void ResetBlocks();                     // 0xada9d0: the parser's, then the per-run state emptied
    void ResetState();                      // 0xad13c0 (material slot 5): the state flag cleared, the two lists emptied
    bool IsStateOne() const { return mState == 1; } // 0xad8b80 (material slot 8)
    void EraseCell(uint32_t handle);        // 0xadc8a0 (material slot 3): the entry for the service named by `handle` removed
    // The cell lists: the sorted vectors of (name, value) cells, the list of group `group` (a name; the group 0 is the
    // unnamed list at +0x2d0), the groups in the map at +0x2c8 (a missing group is made).
    void SetPair(uint32_t group, uint32_t name, uint32_t value); // 0xadcb90 (material slot 0, ret 0xc): the value of `name` set or added
    void RemovePair(uint32_t group, uint32_t name);              // 0xadc930 (material slot 1, ret 8): `name` removed (0: the list emptied)
    void SetAlias(uint32_t from, uint32_t to);                   // 0xadca70 (material slot 2, ret 8): the second map's entry for `from` set to `to`
    std::vector<CellPair> &GroupCells(uint32_t group);           // the list of a group (inlined in both slots above)
    bool QueryInterface(uint32_t iid, void **out); // 0xadc800: the material face for kMaterialIID, else the parser's

    // The material builders. `definition` is a material definition object: its slot 19 the name, 20 the text (the name of
    // the material script, the "create" call's target), 21 the count of its (name, value) pairs, 22 (pair out, index) a pair,
    // 8 an object whose slot 22 (out) gives two dwords (the second kept at +0x1d4). The script runs in the parser with the
    // definition's name pushed as a namespace, its pairs, the unnamed cell list and the list of the material's group set as
    // variables, and "create <text>()" run.
    // 0xacfe00 (material slot 4, ret 8): the definition's script run in state 1 (the materialDefinition block) and what it
    // built handed back (`out`, returned).
    MaterialDefinitionInfo *ParseDefinition(MaterialDefinitionInfo *out, void *definition);
    // 0xad03e0 (material slot 6, ret 0x10): the material's shader built for the render shader context (`context`, `data`):
    // 0 built (counted, in `*out`), 1 the script failed the shader's validation (state 6), 2 no text, 3 nothing built.
    // Without a usable device a stand-in shader is made through the framework.
    uint32_t BuildShader(void **out, void *definition, uint32_t context, const Blob12 *data);
    // 0xad0d90 (material slot 7, ret 0x14): the error visualisation shader for `reason` (1..4) built in its place; the
    // framework's stand-in when that builds nothing. `*out` is counted.
    void BuildErrorShader(void **out, void *definition, uint32_t context, const Blob12 *data, int32_t reason);

    nGZCommandParser::cBaseParser *Parser() { return reinterpret_cast<nGZCommandParser::cBaseParser *>(this); }
    const int32_t *VbTable() const { return *reinterpret_cast<const int32_t *const *>(mBase + 8); }
    void *UnknownFace() { return mBase + 8 + VbTable()[1]; }
    void *ParserFace() { return mBase + 8 + VbTable()[2]; }
    void *MaterialFace() { return mBase + 8 + VbTable()[3]; }
    static cMaterialParser *FromUnknownFace(void *face) { return reinterpret_cast<cMaterialParser *>(static_cast<char *>(face) - 0x2e8); }
    static cMaterialParser *FromParserFace(void *face) { return reinterpret_cast<cMaterialParser *>(static_cast<char *>(face) - 0x2f0); }
    static cMaterialParser *FromMaterialFace(void *face) { return reinterpret_cast<cMaterialParser *>(static_cast<char *>(face) - 0x2fc); }

    alignas(4) uint8_t mBase[0x1ac];          // +0x000 the cBaseParser part
    RefPtrVector mRefs;                       // +0x1ac counted pointers (destroyed by 0x14c060)
    OwnedCell mCellA;                         // +0x1b8
    OwnedCell mCellB;                         // +0x1bc
    uint32_t mZero1c0[4];                     // +0x1c0 the render shader context (slots 6, 7), +0x1c4 its 12 bytes of data
    int32_t mState;                           // +0x1d0 the open block: 1 materialDefinition (slot 4), 2 shader (slots 6, 7), 0 none
    uint32_t mZero1d4;                        // +0x1d4
    void *mObject1d8;                         // +0x1d8 counted (slot 2 releases)
    void *mObject1dc;                         // +0x1dc counted
    std::vector<uint32_t> mList1e0;           // +0x1e0
    std::vector<Record34> mList1ec;           // +0x1ec
    uint8_t mFlag1f8;                         // +0x1f8
    uint8_t mPad1f9[3];
    void *mObject1fc;                         // +0x1fc counted
    void *mObject200;                         // +0x200 counted
    void *mObject204;                         // +0x204 counted
    std::string mText208;                     // +0x208
    std::vector<uint32_t> mList220;           // +0x220
    std::string mText22c;                     // +0x22c
    std::string mText244;                     // +0x244
    std::vector<Blob12> mList25c;             // +0x25c
    uint8_t mFlags268[8];                     // +0x268 (the constructor zeroes them; the reset leaves the first)
    uint8_t mFlag270;                         // +0x270
    uint8_t mFlag271;                         // +0x271 one in both
    uint8_t mFlag272;                         // +0x272
    uint8_t mPad273;
    int32_t mField274;                        // +0x274 4
    int32_t mField278;                        // +0x278 4
    int32_t mField27c;                        // +0x27c -1
    int32_t mField280;                        // +0x280 -1
    int32_t mField284;                        // +0x284 0
    uint8_t mUntouched288[0x40];              // +0x288 nothing here is written by the constructor
    std::map<OwnedCell, std::vector<CellPair>, CellLess> mMap2c8; // +0x2c8
    std::vector<CellPair> mCells2d0;          // +0x2d0
    std::map<OwnedCell, OwnedCell, CellLess> mMap2dc; // +0x2dc
    int32_t mUnknownVtorDisp;                 // +0x2e4
    const void *mUnknownFaceVTable;           // +0x2e8
    int32_t mParserVtorDisp;                  // +0x2ec
    const void *mParserFaceVTable;            // +0x2f0
    const int32_t *mFace2VbTable;             // +0x2f4
    int32_t mMaterialVtorDisp;                // +0x2f8
    const void *mMaterialFaceVTable;          // +0x2fc
    const int32_t *mMaterialFaceVbTable;      // +0x300
};
static_assert(sizeof(std::map<OwnedCell, OwnedCell, CellLess>) == 8&& sizeof(std::string) == 24 && sizeof(std::vector<uint32_t>) == 12,
              "the toolchain's containers do not have the game's layout");
static_assert(offsetof(cMaterialParser, mRefs) == 0x1ac && offsetof(cMaterialParser, mCellA) == 0x1b8 &&
                  offsetof(cMaterialParser, mState) == 0x1d0 && offsetof(cMaterialParser, mObject1d8) == 0x1d8 &&
                  offsetof(cMaterialParser, mList1e0) == 0x1e0 && offsetof(cMaterialParser, mList1ec) == 0x1ec &&
                  offsetof(cMaterialParser, mFlag1f8) == 0x1f8 && offsetof(cMaterialParser, mObject1fc) == 0x1fc &&
                  offsetof(cMaterialParser, mText208) == 0x208 && offsetof(cMaterialParser, mList220) == 0x220 &&
                  offsetof(cMaterialParser, mText22c) == 0x22c && offsetof(cMaterialParser, mText244) == 0x244 &&
                  offsetof(cMaterialParser, mList25c) == 0x25c && offsetof(cMaterialParser, mFlags268) == 0x268 &&
                  offsetof(cMaterialParser, mField274) == 0x274 && offsetof(cMaterialParser, mField284) == 0x284 &&
                  offsetof(cMaterialParser, mMap2c8) == 0x2c8 && offsetof(cMaterialParser, mCells2d0) == 0x2d0 &&
                  offsetof(cMaterialParser, mMap2dc) == 0x2dc && offsetof(cMaterialParser, mUnknownFaceVTable) == 0x2e8 &&
                  offsetof(cMaterialParser, mParserFaceVTable) == 0x2f0 && offsetof(cMaterialParser, mMaterialFaceVTable) == 0x2fc &&
                  offsetof(cMaterialParser, mMaterialFaceVbTable) == 0x300 && sizeof(cMaterialParser) == 0x304,
              "cMaterialParser layout");

// The hooks' entries (ecx is the face the slot was called on: __fastcall with an unused edx).
struct cMaterialParserAdapters {
    static cMaterialParser *__fastcall Construct(cMaterialParser *self, void *, uint32_t mostDerived);
    static void __fastcall Destruct(cMaterialParser *self, void *);
    static cMaterialParser *__fastcall DeletingDestructor(cMaterialParser *self, void *, uint32_t flags);
    static void __fastcall SetField158(cMaterialParser *self, void *, uint32_t value); // primary slot 2 (the folded 0x960960): cBaseParser +0x158 = value
    static uint32_t __fastcall GetField158(cMaterialParser *self, void *);            // primary slot 3 (the folded 0x5b1920)
    static bool __fastcall Initialise(void *face, void *, uint8_t caseSensitive); // the body: face +0x2f0
    static void __fastcall ResetBlocks(void *face, void *);                       // the body: face +0x2f0
    static void __fastcall ResetState(void *face, void *);                        // the body: face +0x2fc
    static bool __fastcall IsStateOne(void *face, void *);                        // the body: face +0x2fc
    static void __fastcall EraseCell(void *face, void *, uint32_t handle);        // the body: face +0x2fc
    static void __fastcall SetPair(void *face, void *, uint32_t group, uint32_t name, uint32_t value); // the body: face +0x2fc
    static void __fastcall RemovePair(void *face, void *, uint32_t group, uint32_t name);              // the body: face +0x2fc
    static void __fastcall SetAlias(void *face, void *, uint32_t from, uint32_t to);                   // the body: face +0x2fc
    static bool __fastcall QueryInterface(void *face, void *, uint32_t iid, void **out); // the body: face +0x2e8
    static MaterialDefinitionInfo *__fastcall ParseDefinition(void *face, void *, MaterialDefinitionInfo *out, void *definition); // face +0x2fc
    static uint32_t __fastcall BuildShader(void *face, void *, void **out, void *definition, uint32_t context, const Blob12 *data); // face +0x2fc
    static void __fastcall BuildErrorShader(void *face, void *, void **out, void *definition, uint32_t context, const Blob12 *data,
                                            int32_t reason); // face +0x2fc
};

// A cBaseParser-shaped face's thunk: the face less its vtordisp, less 0x138 more (where cBaseParser's own face of that kind
// sits: +0x1b0 against +0x2e8, +0x1b8 against +0x2f0), handed to the body's adapter.
inline void *ParserBodyThis(void *face)
{
    char *at = static_cast<char *>(face);
    return at - *reinterpret_cast<const int32_t *>(at - 4) - 0x138;
}
template <auto Adapter> struct ParserShiftedThunk;
template <class R, class... A, R(__fastcall *Adapter)(void *, void *, A...)> struct ParserShiftedThunk<Adapter> {
    static R __fastcall Call(void *face, void *, A... args) { return Adapter(ParserBodyThis(face), nullptr, args...); }
};

} // namespace nGZSceneGraph
