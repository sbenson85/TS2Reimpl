#pragma once
// The lighting script's parser (lanes 261110v, 261110y; scenegraph LG3g, LG7f): cTSLightingParserState, the cBaseParser the
// lighting commands (gz/LightingCommands.h) run on, and nTSSG::cLightingParserState, the lighting manager's (its +0x2c, made
// by cLightingManager::Init). RVAs are 1.18.0.187.
//
// Layout (MSVC, virtual inheritance as cBaseParser's): the cBaseParser part at +0 (its vbptr at +8), then the parser's
// own fields; the virtual bases follow the most derived class's fields, each after its vtordisp word: in a whole
// cTSLightingParserState cIGZUnknown at +0x408 and cIBaseParser at +0x410 (its own vbptr at +0x414; 0x418 bytes); in a
// nTSSG::cLightingParserState cIGZUnknown at +0x42c and cIBaseParser at +0x434 (its vbptr at +0x438; 0x43c bytes).
//
// The standalone shape, as cLightRigAndCameraParserState's (sg/cLightRigAndCameraParserState.h): the vtables and vbtables
// are ours (the .cpp), installed by the constructors; the game's are kept as constants for the tests. The faces' slots
// in the game are thunks: `sub ecx, [ecx-4]; sub ecx, 0x258; jmp body` (cTSLightingParserState; 0x27c in the derived
// class) into cBaseParser's bodies, `sub ecx, [ecx-4]; jmp body` into the class's own bodies, and in the derived class
// `sub ecx, [ecx-4]; sub ecx, 0x24; jmp body` into cTSLightingParserState's bodies (its unknown face's QueryInterface, its
// parser face's ResetBlocks). Every thunk and body is hooked to ours (src/hook/tables/cLightingParserState.cpp).
#include "game/owned.h"
#include "gz/LightingCommands.h" // LightingState
#include "gz/cBaseParser.h"      // the d9b428 / db574c / db581c virtual-base tables
#include "rz/cRZString.h"
#include "sg/cLightingManager.h" // cLightSettingsBlock

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nTSSG {
class cILightSettings;
}

// One `timeOfDay name time [value]` entry of the lighting script (0x20 bytes): the two numbers, then the name.
struct LightingTimeOfDay {
    float mTime;       // +0
    float mValue;      // +4 0 when the command has no third word
    std::string mName; // +8
};
static_assert(sizeof(LightingTimeOfDay) == 0x20 && offsetof(LightingTimeOfDay, mName) == 8, "LightingTimeOfDay layout");

// A room settings block as the parser's vector holds it (0x50 bytes): the block's defaults (0xb3d240), its copy (0xb3d190)
// and its destructor (0x2cbbb0).
struct LightingSettingsEntry {
    nTSSG::cLightSettingsBlock mBlock;
    LightingSettingsEntry() { mBlock.Construct(); }
    LightingSettingsEntry(const LightingSettingsEntry &other) { CopyConstruct(other); }
    LightingSettingsEntry &operator=(const LightingSettingsEntry &) = delete;
    ~LightingSettingsEntry() { mBlock.Destruct(); }
    // 0xb3d190 (__thiscall, ret 4, returns this): the twelve config words, then the text copied (its characters; a new
    // vtable word and a count of 0).
    LightingSettingsEntry *CopyConstruct(const LightingSettingsEntry &other);
};
static_assert(sizeof(LightingSettingsEntry) == 0x50, "LightingSettingsEntry layout");

class cTSLightingParserState {
public:
    static constexpr uint32_t kSize = 0x418;
    static constexpr uint32_t kVTable = 0xe84200;           // primary (6 slots; 0 the deleting destructor 0xb3d860)
    static constexpr uint32_t kUnknownVTable = 0xe8421c;    // the cIGZUnknown face (3 slots)
    static constexpr uint32_t kParserVTable = 0xe8422c;     // the cIBaseParser face (51 slots)
    static constexpr uint32_t kVbTable = 0xe842f8;          // {-8, 0x400, 0x408} for the whole object
    static constexpr uint32_t kParserFaceVbTable = 0xe84304; // cIBaseParser's own
    // The two virtual bases' offsets from the vbptr at +8 when this is the most derived class.
    static constexpr int32_t kUnknownOffset = 0x400, kParserOffset = 0x408;
    // The interface id QueryInterface answers with the object itself.
    static constexpr uint32_t kIID = 0xab7cc174;
    // Our tables (the .cpp), the same shape as the game's above.
    static const void *const kOwnVTable[6];
    static const void *const kOwnUnknownVTable[3];
    static const void *const kOwnParserVTable[51];
    static const int32_t kOwnVbTable[3];
    static const int32_t kOwnParserFaceVbTable[2];

    // 0xb3ce70 (__thiscall, ret 4, returns this). mostDerived != 0: the whole object, so the vbptrs are stored first (the
    // virtual bases' own vtable stores are overwritten before anything reads them, so they are not made). Then the
    // cBaseParser part (cBaseParser::Construct(0): the vbptrs are this class's), this class's three vtables, the vtordisps
    // (the virtual base's displacement beyond the one it has in a whole cTSLightingParserState) and the fields.
    cTSLightingParserState *Construct(uint32_t mostDerived);
    // 0xb3d3e0 (__thiscall, plain ret): the vtables and vtordisps again, the members in reverse, cBaseParser's destructor.
    void Destruct();
    // 0xb3d860, primary slot 0 (ret 4): Destruct, then the memory (0x418 bytes) when bit 0 of `flags` is set.
    cTSLightingParserState *DeletingDestructor(uint32_t flags);
    // 0xb3eda0, primary slot 5 (ret 0xc): the first state named `name` (same length, same bytes): copied to *out (its
    // assignment, 0xb3d510), its index to *index; true. False (nothing written) when there is none.
    bool FindState(const char *name, LightingState *out, uint32_t *index);

    // The bodies of the faces' own slots (`this` the face at +0x408 / +0x410 of the cTSLightingParserState part; the
    // derived class's thunks move its faces there).
    // 0xb3f640 (unknown face slot 0, ret 8): kIID gives the object itself (AddRef through the unknown face); other ids
    // cBaseParser's.
    bool QueryInterface(uint32_t iid, void **out);
    // 0xb3eea0 (parser face slot 0, ret 4): the parser's (its result ignored), versions 4..10, the lightingState and
    // timeOfDay commands registered; true.
    bool Initialise(uint8_t caseSensitive);
    // 0xb3f720 (parser face slot 1): the parser's.
    bool Shutdown();
    // 0xb3d8a0 (parser face slot 9): the parser's, then the states, room settings and times emptied; one room settings
    // block of the defaults appended.
    void Reset();
    // 0xb3f140 (parser face slot 10): the parser's.
    void ResetBlocks();

    nGZCommandParser::cBaseParser *Base() { return reinterpret_cast<nGZCommandParser::cBaseParser *>(this); }
    const int32_t *VbTable() const { return *reinterpret_cast<const int32_t *const *>(mBaseParser + 8); }
    void *UnknownFace() { return mBaseParser + 8 + VbTable()[1]; }
    void *ParserFace() { return mBaseParser + 8 + VbTable()[2]; }
    static cTSLightingParserState *FromUnknownFace(void *face)
    {
        return reinterpret_cast<cTSLightingParserState *>(static_cast<char *>(face) - 0x408);
    }
    static cTSLightingParserState *FromParserFace(void *face)
    {
        return reinterpret_cast<cTSLightingParserState *>(static_cast<char *>(face) - 0x410);
    }

    alignas(4) uint8_t mBaseParser[0x1ac];           // +0x000 the cBaseParser's own part (vtable +0, vbptr +8)
    std::vector<LightingState> mStates;              // +0x1ac the named states (FindState)
    LightingState mCurrent;                          // +0x1b8 the state the open lightingState block fills
    std::vector<LightingSettingsEntry> mRoomSettings; // +0x3cc (Reset leaves one block of the defaults)
    cRZString m3D8;                                  // +0x3d8 empty (only built and destroyed here)
    std::vector<LightingTimeOfDay> mTimesOfDay;      // +0x3f8 the timeOfDay commands' entries
    uint32_t m404;                                   // +0x404 (whole object: cIGZUnknown's vtordisp; derived: its first field)
};
static_assert(offsetof(cTSLightingParserState, mStates) == 0x1ac && offsetof(cTSLightingParserState, mCurrent) == 0x1b8 &&
                  offsetof(cTSLightingParserState, mRoomSettings) == 0x3cc && offsetof(cTSLightingParserState, m3D8) == 0x3d8 &&
                  offsetof(cTSLightingParserState, mTimesOfDay) == 0x3f8 && sizeof(cTSLightingParserState) == 0x408,
              "cTSLightingParserState layout");

// The vtables' entries (ecx the object or the face the slot was called on: __fastcall with an unused edx).
struct cTSLightingParserStateAdapters {
    static cTSLightingParserState *__fastcall Construct(cTSLightingParserState *self, void *, uint32_t mostDerived);
    static void __fastcall Destruct(cTSLightingParserState *self, void *);
    static cTSLightingParserState *__fastcall DeletingDestructor(cTSLightingParserState *self, void *, uint32_t flags);
    static bool __fastcall FindState(cTSLightingParserState *self, void *, const char *name, LightingState *out, uint32_t *index);
    static bool __fastcall QueryInterface(void *unknownFace, void *, uint32_t iid, void **out); // the body: face +0x408
    static bool __fastcall Initialise(void *face, void *, uint8_t caseSensitive);               // the bodies: face +0x410
    static bool __fastcall Shutdown(void *face, void *);
    static void __fastcall Reset(void *face, void *);
    static void __fastcall ResetBlocks(void *face, void *);
    static LightingSettingsEntry *__fastcall CopySettings(LightingSettingsEntry *self, void *, const LightingSettingsEntry *other);
    static LightingState *__fastcall AssignState(LightingState *self, void *, const LightingState *other); // 0xb3d510
};

// A face's thunk into a body compiled for faces `Shift` bytes lower: the face less its vtordisp, less `Shift`, handed to
// the body's adapter (0x258: cTSLightingParserState's faces to cBaseParser's own; 0x27c: nTSSG::cLightingParserState's to
// cBaseParser's; 0x24: nTSSG::cLightingParserState's to cTSLightingParserState's).
template <int32_t Shift, auto Adapter> struct LightingShiftedThunk;
template <int32_t Shift, class R, class... A, R(__fastcall *Adapter)(void *, void *, A...)>
struct LightingShiftedThunk<Shift, Adapter> {
    static R __fastcall Call(void *face, void *, A... args)
    {
        char *const at = static_cast<char *>(face);
        return Adapter(at - *reinterpret_cast<const int32_t *>(at - 4) - Shift, nullptr, args...);
    }
};

namespace nTSSG {

// `timeOfDay name time [value]` (vtable 0xe849c8, cBlockCommandT, 0x44 bytes; registered by cTSLightingParserState's
// Initialise): appends {time, value (0 without a third word), name} to the parser's times (the parser face's slot 31
// evaluates the numbers). Slots other than 4..6 are cBlockCommand's.
class cTimeOfDayCommand : public nGZCommandParser::cBlockCommandT {
public:
    static constexpr uint32_t kVTable = 0xe849c8;
    cTSLightingParserState *State() const { return static_cast<cTSLightingParserState *>(mState); }
    void Execute(nGZCommandParser::cIArguments *args); // slot 4, 0xb3ea80
    const char *GetName() const;                       // slot 5, 0xb3f080: "timeOfDay"
    const char *GetHelp(uint32_t index) const;         // slot 6, 0xb3da50: one text, whatever the index
};

// The lighting manager's parser (0x43c bytes). Its constructor is inline in cLightingManager::Init (no RVA of its own).
class cLightingParserState {
public:
    static constexpr uint32_t kSize = 0x43c;
    static constexpr uint32_t kVTable = 0xdb5f60;        // primary (6 slots; 0 the deleting destructor 0x2cbc70)
    static constexpr uint32_t kUnknownVTable = 0xdb5f7c; // the cIGZUnknown face
    static constexpr uint32_t kParserVTable = 0xdb5f8c;  // the cIBaseParser face
    static constexpr uint32_t kVbTable = 0xdb6058;       // {-8, 0x424, 0x42c}
    static constexpr uint32_t kParserFaceVbTable = 0xdb6064;
    static const void *const kOwnVTable[6];
    static const void *const kOwnUnknownVTable[3];
    static const void *const kOwnParserVTable[51];
    static const int32_t kOwnVbTable[3];
    static const int32_t kOwnParserFaceVbTable[2];

    // The constructor as Init has it inline: the vbptrs, the base (cTSLightingParserState::Construct(0)), this class's
    // vtables, the vtordisps 0 (the whole object), the light settings pointer 0 and an empty cRZString at +0x408.
    cLightingParserState *Construct();
    // 0x2cbc70, primary slot 0 (ret 4): the text's destructor, the light settings released, the base's destructor; the
    // memory (0x43c bytes) freed when bit 0 of `flags` is set.
    cLightingParserState *DeletingDestructor(uint32_t flags);

    // The bodies of the parser face's own slots (`this` the face at +0x434).
    // 0x2da270 (slot 0, ret 4): the base's (false: false); a new cLightSettings held at +0x404 (counted), its Init; the
    // lightAttribOverride and lightAttributes commands registered; true.
    bool Initialise(uint8_t caseSensitive);
    // 0x2e30e0 (slot 1): the light settings' Shutdown and the reference dropped; the parser's.
    bool Shutdown();
    // 0x2e02e0 (slot 8): the parser's, then 0xb41a80 on the parser face (not ours: runs the app's lighting text through
    // the face's slot 19 when the app has one).
    void RestartViaReset();
    // 0x2d2810 (slot 9): the base's, then the light settings' Clear.
    void Reset();

    // The faces, through the vbtable at +8 as the original's code finds them.
    void *UnknownFace() { return reinterpret_cast<uint8_t *>(this) + 8 + mBase.VbTable()[1]; }
    void *ParserFace() { return reinterpret_cast<uint8_t *>(this) + 8 + mBase.VbTable()[2]; }
    cILightSettings *&Settings() { return *reinterpret_cast<cILightSettings **>(&mBase.m404); }
    static cLightingParserState *FromParserFace(void *face)
    {
        return reinterpret_cast<cLightingParserState *>(static_cast<char *>(face) - 0x434);
    }

    cTSLightingParserState mBase;      // +0x000..+0x404 (m404, the light settings, is this class's first word)
    cRZString mText;                   // +0x408
    int32_t mUnknownVtorDisp;          // +0x428
    const void *mUnknownVTableWord;    // +0x42c cIGZUnknown
    int32_t mParserVtorDisp;           // +0x430
    const void *mParserFaceVTable;     // +0x434 cIBaseParser
    const int32_t *mParserFaceVbTable; // +0x438
};
static_assert(offsetof(cLightingParserState, mText) == 0x408 && offsetof(cLightingParserState, mUnknownVTableWord) == 0x42c &&
                  offsetof(cLightingParserState, mParserFaceVbTable) == 0x438 && sizeof(cLightingParserState) == 0x43c,
              "nTSSG::cLightingParserState layout");

struct cLightingParserStateAdapters {
    static cLightingParserState *__fastcall DeletingDestructor(cLightingParserState *self, void *, uint32_t flags);
    static bool __fastcall Initialise(void *face, void *, uint8_t caseSensitive); // the bodies: face +0x434
    static bool __fastcall Shutdown(void *face, void *);
    static void __fastcall RestartViaReset(void *face, void *);
    static void __fastcall Reset(void *face, void *);
};

} // namespace nTSSG
