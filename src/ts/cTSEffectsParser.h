#pragma once
// cTSEffectsParser (lane WU, rendering E12): the parser that reads the effects text (the effectID/textureID... lookups, the
// `effect`, `resource`, `timeline` and `effectGroup` blocks and the particle, camera, water and screen blocks the other command
// files register). RVAs are 1.18.0.187.
//
// The same shape as nGZSceneGraph::cMaterialParser (src/sg/cMaterialParser.h): a cBaseParser with three more virtual faces, the
// vtables and vbtables ours (hand-built in cTSEffectsParser.cpp, installed by the constructor; the game's tables are kept as
// constants for the tests to compare against), the image's thunks hooked for code that still reaches them.
//   +0x000 the cBaseParser part (0..0x1ac; its vbptr at +8 is this class's own, 0xe8dca0 {-8, 0x29c, 0x2a4, 0x2b0})
//   +0x1ac .. +0x2a0 the members (below)
//   +0x2a0 the cIGZUnknown face's vtordisp, +0x2a4 its vtable (0xe8dba4, 3 slots)
//   +0x2a8 the cIBaseParser face's vtordisp, +0x2ac its vtable (0xe8dbb4, 51 slots)
//   +0x2b0 a vbptr (0xe8dcb0 {-4, -0xc}), +0x2b4 the cITSEffectsParser face's vtordisp, +0x2b8 its vtable (0xe8dc84, 7 slots)
//   +0x2bc the cITSEffectsParser face's vbptr (0xe8dcb8 {-4, -0x18, -0x10})
// The primary vtable is 0xe8db8c (5 slots; slot 0 the deleting destructor). The faces' slots are thunks
// `sub ecx, [ecx-4]; sub ecx, 0xf4; jmp body` that land on cBaseParser's bodies with `this` where cBaseParser's own faces sit
// (+0x1b0 / +0x1b8), hooked to forwarders (EffectsShiftedThunk below); the slots with bodies of this class's own are
// `sub ecx, [ecx-4]; jmp body` thunks hooked to VtordispThunk forwarders.
//
// The members: five counted pointers the cITSEffectsParser face sets (the descriptions manager at +0x1b0), four name->number
// maps, two strings, a list of 0x88-byte records, a set of 56-byte keys, a lower-cased name -> text map, and two vectors of
// counted pointers with the values they are handed.
#include "rz/cRZString.h"
#include "simantics/cTSBehaviorTree.h" // RefPtrVector

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace nGZCommandParser {
class cBaseParser;
}

// The elements of the list at +0x230: 0x88 bytes, a string first (the rest is not known yet).
struct EffectsRecord88 {
    std::string text;
    uint8_t rest[0x88 - sizeof(std::string)];
};
static_assert(sizeof(EffectsRecord88) == 0x88, "EffectsRecord88 layout");

// The keys of the set at +0x278: 56 bytes, two strings (at +0 and +0x1c; the node destructor 0xbcd250 empties both). The order
// is never used by the parts of the class done so far, so this one (the strings in turn) is a stand-in.
struct EffectsBlob56 {
    std::string first;
    uint32_t middle;
    std::string second;
    uint32_t last;
};
static_assert(sizeof(EffectsBlob56) == 56 && offsetof(EffectsBlob56, second) == 0x1c, "EffectsBlob56 layout");
struct EffectsBlob56Less {
    bool operator()(const EffectsBlob56 &a, const EffectsBlob56 &b) const
    {
        if (a.first != b.first) return a.first < b.first;
        return a.second < b.second;
    }
};

class cTSEffectsParser {
public:
    static constexpr uint32_t kSize = 0x2c0;
    static constexpr uint32_t kVTable = 0xe8db8c;            // primary (slot 0 the deleting destructor)
    static constexpr uint32_t kUnknownVTable = 0xe8dba4;     // the cIGZUnknown face
    static constexpr uint32_t kParserVTable = 0xe8dbb4;      // the cIBaseParser face
    static constexpr uint32_t kEffectsVTable = 0xe8dc84;     // the cITSEffectsParser face
    static constexpr uint32_t kVbTable = 0xe8dca0;           // {-8, 0x29c, 0x2a4, 0x2b0}
    static constexpr uint32_t kFace2VbTable = 0xe8dcb0;      // {-4, -0xc}
    static constexpr uint32_t kEffectsFaceVbTable = 0xe8dcb8; // {-4, -0x18, -0x10}
    static constexpr uint32_t kEffectsIID = 0x6956babc;
    static constexpr uint32_t kLookupFormat = 0xe8e0c8;      // the lookup key's format ("%s:%d")
    static constexpr uint32_t kCommandNames = 0x107d618;     // the 14 command names: pointers to the literals, in order
    // Our tables (cTSEffectsParser.cpp), the same shape as the game's above: slot counts, vbtable words.
    static const void *const kOwnVTable[5];
    static const void *const kOwnUnknownVTable[3];
    static const void *const kOwnParserVTable[51];
    static const void *const kOwnEffectsVTable[7];
    static const int32_t kOwnVbTable[4];
    static const int32_t kOwnFace2VbTable[2];
    static const int32_t kOwnEffectsFaceVbTable[3];

    // 0xbcce40 (__thiscall, ret 4, returns this): the virtual bases' vbptrs when `mostDerived`, cBaseParser's constructor
    // (with 0), this class's vtables, the three vtordisps zero, the members (the maps' head nodes allocated).
    cTSEffectsParser *Construct(uint32_t mostDerived);
    // 0xbcd2e0 (__thiscall, plain ret): the vtables again, the members destroyed in reverse order, cBaseParser's destructor.
    void Destruct();
    // 0xbcd7e0, primary slot 0 (ret 4): Destruct, then the memory (0x2c0 bytes) when bit 0 of `flags` is set.
    cTSEffectsParser *DeletingDestructor(uint32_t flags);

    // The bodies of the cIBaseParser face's slots 0 and 9 (`this` the face at +0x2ac), the cIGZUnknown face's slot 0 (+0x2a4)
    // and the cITSEffectsParser face's slots 0..6 (+0x2b8).
    bool Initialise(uint8_t caseSensitive); // 0xbcf820: the parser's, versions 1..0x16, 14 commands and the 8 blocks' registrars
    void Reset();                           // 0xbcdb10: the parser's, then the per-run state emptied
    bool QueryInterface(uint32_t iid, void **out); // 0xbd19b0: the effects face for kEffectsIID, else the parser's
    // Slots 0..3: the counted pointers at +0x1ac, +0x1b0, +0x1b4, +0x1bc replaced (the new one counted, the old released).
    // Slot 0 also clears the byte at +0x1d0.
    void SetPointer1ac(void *object); // 0xbd1db0
    void SetPointer1b0(void *object); // 0xbd1d50 (the description manager)
    void SetPointer1b4(void *object); // 0xbd1e10
    void SetPointer1bc(void *object); // 0xbd1e50
    void SetFlag1d0(uint8_t value);   // 0xbd1d85 (slot 4, thunk and body in one)
    // Slot 5, 0xbcdc70 (ret 8): the text registered for the lower-cased "name:number" key, null when there is none.
    const char *Lookup(const char *name, int32_t number);
    // Slot 6, 0xbd1f10: for each counted object of the list at +0x288, its slot 3 called with the value at the same index of
    // the list at +0x294.
    void Apply();

    nGZCommandParser::cBaseParser *Parser() { return reinterpret_cast<nGZCommandParser::cBaseParser *>(this); }
    const int32_t *VbTable() const { return *reinterpret_cast<const int32_t *const *>(mBase + 8); }
    void *UnknownFace() { return mBase + 8 + VbTable()[1]; }
    void *ParserFace() { return mBase + 8 + VbTable()[2]; }
    void *EffectsFace() { return mBase + 8 + VbTable()[3]; }
    static cTSEffectsParser *FromUnknownFace(void *face) { return reinterpret_cast<cTSEffectsParser *>(static_cast<char *>(face) - 0x2a4); }
    static cTSEffectsParser *FromParserFace(void *face) { return reinterpret_cast<cTSEffectsParser *>(static_cast<char *>(face) - 0x2ac); }
    static cTSEffectsParser *FromEffectsFace(void *face) { return reinterpret_cast<cTSEffectsParser *>(static_cast<char *>(face) - 0x2b8); }

    alignas(4) uint8_t mBase[0x1ac];          // +0x000 the cBaseParser part
    void *mObject1ac;                         // +0x1ac counted
    void *mDescriptions;                      // +0x1b0 counted (the description manager)
    void *mObject1b4;                         // +0x1b4 counted
    void *mObject1b8;                         // +0x1b8 counted (the reset releases and clears it)
    void *mObject1bc;                         // +0x1bc counted
    uint32_t mZero1c0[4];                     // +0x1c0
    uint8_t mFlag1d0;                         // +0x1d0 (the constructor never writes it)
    uint8_t mPad1d1[3];
    std::map<std::string, uint32_t> mMap1d4;  // +0x1d4
    std::map<std::string, uint32_t> mMap1dc;  // +0x1dc
    std::map<std::string, uint32_t> mMap1e4;  // +0x1e4
    std::map<std::string, uint32_t> mMap1ec;  // +0x1ec
    cRZString mText1f4;                       // +0x1f4 (the reset empties its string)
    uint32_t mZero214;                        // +0x214
    uint16_t mWord218;                        // +0x218 one
    uint8_t mPad21a[2];
    uint32_t mZero21c[5];                     // +0x21c
    std::vector<EffectsRecord88> mList230;    // +0x230
    cRZString mText23c;                       // +0x23c
    uint8_t mUntouched25c[0x1a];              // +0x25c nothing here is written by the constructor
    uint16_t mWord276;                        // +0x276 (only the reset writes it: zero)
    std::set<EffectsBlob56, EffectsBlob56Less> mSet278; // +0x278
    std::map<std::string, std::string> mMap280; // +0x280 lower-cased "name:number" -> text (the lookup's map)
    RefPtrVector mRefs288;                    // +0x288 counted objects (Apply calls their slot 3)
    std::vector<void *> mValues294;           // +0x294 the values Apply hands them
    int32_t mUnknownVtorDisp;                 // +0x2a0
    const void *mUnknownFaceVTable;           // +0x2a4
    int32_t mParserVtorDisp;                  // +0x2a8
    const void *mParserFaceVTable;            // +0x2ac
    const int32_t *mFace2VbTable;             // +0x2b0
    int32_t mEffectsVtorDisp;                 // +0x2b4
    const void *mEffectsFaceVTable;           // +0x2b8
    const int32_t *mEffectsFaceVbTable;       // +0x2bc
};
static_assert(sizeof(std::map<std::string, uint32_t>) == 8 && sizeof(std::set<EffectsBlob56, EffectsBlob56Less>) == 8 &&
                  sizeof(std::string) == 24 && sizeof(std::vector<void *>) == 12,
              "the toolchain's containers do not have the game's layout");
static_assert(offsetof(cTSEffectsParser, mObject1ac) == 0x1ac && offsetof(cTSEffectsParser, mDescriptions) == 0x1b0 &&
                  offsetof(cTSEffectsParser, mObject1bc) == 0x1bc && offsetof(cTSEffectsParser, mZero1c0) == 0x1c0 &&
                  offsetof(cTSEffectsParser, mFlag1d0) == 0x1d0 && offsetof(cTSEffectsParser, mMap1d4) == 0x1d4 &&
                  offsetof(cTSEffectsParser, mMap1ec) == 0x1ec && offsetof(cTSEffectsParser, mText1f4) == 0x1f4 &&
                  offsetof(cTSEffectsParser, mZero214) == 0x214 && offsetof(cTSEffectsParser, mWord218) == 0x218 &&
                  offsetof(cTSEffectsParser, mZero21c) == 0x21c && offsetof(cTSEffectsParser, mList230) == 0x230 &&
                  offsetof(cTSEffectsParser, mText23c) == 0x23c && offsetof(cTSEffectsParser, mUntouched25c) == 0x25c &&
                  offsetof(cTSEffectsParser, mWord276) == 0x276 && offsetof(cTSEffectsParser, mSet278) == 0x278 &&
                  offsetof(cTSEffectsParser, mMap280) == 0x280 && offsetof(cTSEffectsParser, mRefs288) == 0x288 &&
                  offsetof(cTSEffectsParser, mValues294) == 0x294 && offsetof(cTSEffectsParser, mUnknownFaceVTable) == 0x2a4 &&
                  offsetof(cTSEffectsParser, mParserFaceVTable) == 0x2ac && offsetof(cTSEffectsParser, mEffectsFaceVTable) == 0x2b8 &&
                  offsetof(cTSEffectsParser, mEffectsFaceVbTable) == 0x2bc && sizeof(cTSEffectsParser) == 0x2c0,
              "cTSEffectsParser layout");

// The hooks' entries (ecx is the face the slot was called on: __fastcall with an unused edx).
struct cTSEffectsParserAdapters {
    static cTSEffectsParser *__fastcall Construct(cTSEffectsParser *self, void *, uint32_t mostDerived);
    static void __fastcall Destruct(cTSEffectsParser *self, void *);
    static cTSEffectsParser *__fastcall DeletingDestructor(cTSEffectsParser *self, void *, uint32_t flags);
    static void __fastcall SetField158(cTSEffectsParser *self, void *, uint32_t value); // primary slot 2 (the folded 0x960960)
    static uint32_t __fastcall GetField158(cTSEffectsParser *self, void *);            // primary slot 3 (the folded 0x5b1920)
    static bool __fastcall Initialise(void *face, void *, uint8_t caseSensitive);   // the body: face +0x2ac
    static void __fastcall Reset(void *face, void *);                               // the body: face +0x2ac
    static bool __fastcall QueryInterface(void *face, void *, uint32_t iid, void **out); // the body: face +0x2a4
    static void __fastcall SetPointer1ac(void *face, void *, void *object);         // the bodies: face +0x2b8
    static void __fastcall SetPointer1b0(void *face, void *, void *object);
    static void __fastcall SetPointer1b4(void *face, void *, void *object);
    static void __fastcall SetPointer1bc(void *face, void *, void *object);
    static void __fastcall SetFlag1d0(void *face, void *, uint8_t value);
    static const char *__fastcall Lookup(void *face, void *, const char *name, int32_t number);
    static void __fastcall Apply(void *face, void *);
};

// A cBaseParser-shaped face's thunk: the face less its vtordisp, less 0xf4 more (where cBaseParser's own face of that kind
// sits: +0x1b0 against +0x2a4, +0x1b8 against +0x2ac), handed to the body's adapter.
inline void *EffectsParserBodyThis(void *face)
{
    char *at = static_cast<char *>(face);
    return at - *reinterpret_cast<const int32_t *>(at - 4) - 0xf4;
}
template <auto Adapter> struct EffectsShiftedThunk;
template <class R, class... A, R(__fastcall *Adapter)(void *, void *, A...)> struct EffectsShiftedThunk<Adapter> {
    static R __fastcall Call(void *face, void *, A... args) { return Adapter(EffectsParserBodyThis(face), nullptr, args...); }
};
