#pragma once
// nTSSG::cLightRigAndCameraParserState (lane 261109d, scenegraph row LG7g): the light rig file's parser state, which the
// light rig manager makes in Init (0x2c2d30) and hands to its file parser (cLightRigManager.h), with the five commands it
// registers (an anonymous namespace of the manager's translation unit, `.?AUcLightRigCommand@?A0x5a038c03@@` and so on;
// here in nTSSG). A rig file reads
//   lightrig <name>
//     resource <resource name>
//     root <tag of the rig's node in that resource>
//     anim <key> <animation resource name>      (any number)
//     autoRig [<auto-rig name>]
//   end
// and the block's end makes or finds the manager's record for <name> and fills it (AddRig, 0x2c21a0). RVAs are 1.18.0.187.
//
// The same shape as cTSEffectsParser (src/ts/cTSEffectsParser.h): a cBaseParser with its two virtual faces moved to the end,
// the vtables and vbtables ours (hand-built in the .cpp, installed by the constructor; the game's kept as constants for the
// tests to compare against).
//   +0x000 the cBaseParser part (0..0x1ac; its vbptr at +8 is this class's own, 0xdb5974 {-8, 0x230, 0x238})
//   +0x1ac .. +0x234 the members (below)
//   +0x234 the cIGZUnknown face's vtordisp, +0x238 its vtable (0xdb5898, 3 slots)
//   +0x23c the cIBaseParser face's vtordisp, +0x240 its vtable (0xdb58a8, 51 slots)
//   +0x244 a vbptr (0xdb5980 {-4, -0xc})
// The primary vtable is 0xdb5880 (5 slots; slot 0 the deleting destructor). The faces' slots are thunks
// `sub ecx, [ecx-4]; sub ecx, 0x88; jmp body` that land on cBaseParser's bodies with `this` where cBaseParser's own faces
// sit (+0x1b0 / +0x1b8), except the cIBaseParser face's slots 0 (Initialise) and 10 (ResetBlocks), `sub ecx, [ecx-4]; jmp
// body` thunks to this class's own bodies. Every thunk, body, the constructor and destructors are hooked to ours
// (src/hook/tables/cLightRigAndCameraParserState.cpp), as cTSEffectsParser's are; the manager's Init calls ours directly.
//
// The commands are made by Initialise with the game's vtables (the other slots are cCommand's / cBlockCommand's shared,
// verified ones), so their own slots are hooked at their RVAs.
#include "gz/CommandParser.h"
#include "sg/cLightRigManager.h" // AnimationMap

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace nGZCommandParser {
class cBaseParser;
class cIArguments;
} // namespace nGZCommandParser

namespace nTSSG {

class cLightRigManager;

class cLightRigAndCameraParserState {
public:
    static constexpr uint32_t kSize = 0x248;
    static constexpr uint32_t kVTable = 0xdb5880;        // primary (slot 0 the deleting destructor)
    static constexpr uint32_t kUnknownVTable = 0xdb5898; // the cIGZUnknown face
    static constexpr uint32_t kParserVTable = 0xdb58a8;  // the cIBaseParser face
    static constexpr uint32_t kVbTable = 0xdb5974;       // {-8, 0x230, 0x238}
    static constexpr uint32_t kFace2VbTable = 0xdb5980;  // {-4, -0xc}
    // Our tables (the .cpp), the same shape as the game's above.
    static const void *const kOwnVTable[5];
    static const void *const kOwnUnknownVTable[3];
    static const void *const kOwnParserVTable[51];
    static const int32_t kOwnVbTable[3];
    static const int32_t kOwnFace2VbTable[2];

    // 0x2c1800 (__thiscall, ret 8, returns this): the vbptrs when `mostDerived`, cBaseParser's constructor (with 0), this
    // class's vtables and vtordisps, the manager, the five strings empty, the animations map (its head node allocated).
    __declspec(noinline) cLightRigAndCameraParserState *Construct(cLightRigManager *manager, uint32_t mostDerived);
    // 0x2c1cb0 (__thiscall, plain ret): the vtables and vtordisps again, the members in reverse, cBaseParser's destructor.
    void Destruct();
    // 0x2c2130, primary slot 0 (ret 4): Destruct, then the memory (0x248 bytes) when bit 0 of `flags` is set.
    cLightRigAndCameraParserState *DeletingDestructor(uint32_t flags);

    // The bodies of the cIBaseParser face's slots 0 and 10 (`this` the face at +0x240).
    // 0x2c2b60 (ret 4): the parser's (its result ignored), versions 1..1, the five commands; true.
    bool Initialise(uint8_t caseSensitive);
    // 0x2c3ba0: the parser's, then the rig being read emptied (name, resource, tag, animations, auto-rig flag and name).
    void ResetBlocks();
    // 0x2c21a0 (__thiscall, returns true): the manager's record for mRigName (made when new) given what the block read;
    // the record's loaded and root-missing flags cleared. Called only by cLightRigCommand's end (slot 9).
    bool AddRig();

    nGZCommandParser::cBaseParser *Parser() { return reinterpret_cast<nGZCommandParser::cBaseParser *>(this); }
    const int32_t *VbTable() const { return *reinterpret_cast<const int32_t *const *>(mBase + 8); }
    void *UnknownFace() { return mBase + 8 + VbTable()[1]; }
    void *ParserFace() { return mBase + 8 + VbTable()[2]; }
    static cLightRigAndCameraParserState *FromParserFace(void *face)
    {
        return reinterpret_cast<cLightRigAndCameraParserState *>(static_cast<char *>(face) - 0x240);
    }

    alignas(4) uint8_t mBase[0x1ac];             // +0x000 the cBaseParser part
    cLightRigManager *mManager;                  // +0x1ac
    std::string mRigName;                        // +0x1b0 the open block's rig (empty outside a block)
    std::string mResourceName;                   // +0x1c8
    std::string mRootTag;                        // +0x1e0
    std::string mText1f8;                        // +0x1f8 only constructed and destroyed
    std::string mAutoRigName;                    // +0x210
    bool mAutoRig;                               // +0x228 (the constructor never writes it)
    uint8_t mPad229[3];
    AnimationMap mAnimations;                   // +0x22c key -> animation resource name (case-insensitive)
    int32_t mUnknownVtorDisp;                    // +0x234
    const void *mUnknownFaceVTable;              // +0x238
    int32_t mParserVtorDisp;                     // +0x23c
    const void *mParserFaceVTable;               // +0x240
    const int32_t *mFace2VbTable;                // +0x244
};
static_assert(sizeof(AnimationMap) == 8 && sizeof(std::string) == 24,
              "the toolchain's containers do not have the game's layout");
static_assert(offsetof(cLightRigAndCameraParserState, mManager) == 0x1ac &&
                  offsetof(cLightRigAndCameraParserState, mRigName) == 0x1b0 &&
                  offsetof(cLightRigAndCameraParserState, mResourceName) == 0x1c8 &&
                  offsetof(cLightRigAndCameraParserState, mRootTag) == 0x1e0 &&
                  offsetof(cLightRigAndCameraParserState, mText1f8) == 0x1f8 &&
                  offsetof(cLightRigAndCameraParserState, mAutoRigName) == 0x210 &&
                  offsetof(cLightRigAndCameraParserState, mAutoRig) == 0x228 &&
                  offsetof(cLightRigAndCameraParserState, mAnimations) == 0x22c &&
                  offsetof(cLightRigAndCameraParserState, mUnknownFaceVTable) == 0x238 &&
                  offsetof(cLightRigAndCameraParserState, mParserFaceVTable) == 0x240 &&
                  offsetof(cLightRigAndCameraParserState, mFace2VbTable) == 0x244 &&
                  sizeof(cLightRigAndCameraParserState) == 0x248,
              "cLightRigAndCameraParserState layout");

// The vtables' entries (ecx the object or the face the slot was called on: __fastcall with an unused edx).
struct cLightRigAndCameraParserStateAdapters {
    static cLightRigAndCameraParserState *__fastcall Construct(cLightRigAndCameraParserState *self, void *, cLightRigManager *manager,
                                                               uint32_t mostDerived);
    static void __fastcall Destruct(cLightRigAndCameraParserState *self, void *);
    static cLightRigAndCameraParserState *__fastcall DeletingDestructor(cLightRigAndCameraParserState *self, void *, uint32_t flags);
    static void __fastcall SetField158(cLightRigAndCameraParserState *self, void *, uint32_t value); // primary slot 2 (folded 0x960960)
    static uint32_t __fastcall GetField158(cLightRigAndCameraParserState *self, void *);            // primary slot 3 (folded 0x5b1920)
    static bool __fastcall Initialise(void *face, void *, uint8_t caseSensitive); // the body: face +0x240
    static void __fastcall ResetBlocks(void *face, void *);                       // the body: face +0x240
};

// A cBaseParser-shaped face's thunk: the face less its vtordisp, less 0x88 more (where cBaseParser's own face of that kind
// sits: +0x1b0 against +0x238, +0x1b8 against +0x240), handed to the body's adapter.
inline void *LightRigParserBodyThis(void *face)
{
    char *at = static_cast<char *>(face);
    return at - *reinterpret_cast<const int32_t *>(at - 4) - 0x88;
}
template <auto Adapter> struct LightRigShiftedThunk;
template <class R, class... A, R(__fastcall *Adapter)(void *, void *, A...)> struct LightRigShiftedThunk<Adapter> {
    static R __fastcall Call(void *face, void *, A... args) { return Adapter(LightRigParserBodyThis(face), nullptr, args...); }
};

// ---- the commands (the state at +0x40 / +0xc is the parser state above) -------------------------------------------------
// The texts: "Wrong number of arguments" (0xdb59cc) for a count other than the command's, "No lightrig name" (0xdb5a54) for
// a rig command outside a lightrig block.

// `lightrig <name>` (vtable 0xdb598c, cBlockCommandT, 0x44 bytes): opens a rig block ("Lightrig name already set" inside
// one) and a namespace of its name; its end adds the rig ("Failed to add light rig" when it did not).
class cLightRigCommand : public nGZCommandParser::cBlockCommandT {
public:
    static constexpr uint32_t kVTable = 0xdb598c;
    cLightRigAndCameraParserState *State() const { return static_cast<cLightRigAndCameraParserState *>(mState); }
    void Execute(nGZCommandParser::cIArguments *args); // slot 4, 0x2c2830
    const char *GetName() const;                       // slot 5, 0x2c3030: "lightrig"
    void EndBlock();                                   // slot 9, 0x2c3b20
};

// `resource <name>` (vtable 0xdb5a20, cCommandT, 16 bytes): the rig's resource name.
class cLightRigResNameCommand : public nGZCommandParser::cCommandT {
public:
    static constexpr uint32_t kVTable = 0xdb5a20;
    cLightRigAndCameraParserState *State() const { return static_cast<cLightRigAndCameraParserState *>(mState); }
    void Execute(nGZCommandParser::cIArguments *args); // slot 4, 0x2c2990
    const char *GetName() const;                       // slot 5, 0x2c3040: "resource"
};

// `root <tag>` (vtable 0xdb5a6c, cCommandT, 16 bytes): the tag of the rig's node in the resource.
class cRootTagCommand : public nGZCommandParser::cCommandT {
public:
    static constexpr uint32_t kVTable = 0xdb5a6c;
    cLightRigAndCameraParserState *State() const { return static_cast<cLightRigAndCameraParserState *>(mState); }
    void Execute(nGZCommandParser::cIArguments *args); // slot 4, 0x2c2a30
    const char *GetName() const;                       // slot 5, 0x2c3050: "root"
};

// `anim <key> <name>` (vtable 0xdb5ad4, cCommandT, 16 bytes): an animation of the rig by key.
class cAnimNameCommand : public nGZCommandParser::cCommandT {
public:
    static constexpr uint32_t kVTable = 0xdb5ad4;
    cLightRigAndCameraParserState *State() const { return static_cast<cLightRigAndCameraParserState *>(mState); }
    void Execute(nGZCommandParser::cIArguments *args); // slot 4, 0x2c26b0
    const char *GetName() const;                       // slot 5, 0x2c3010: "anim"
};

// `autoRig [<name>]` (vtable 0xdb5aa0, cCommandT, 16 bytes): the rig is an auto rig (with a name when one is given). The
// rig name is checked first; there is no count check (the positional words are asked for 0..1).
class cAutoRigCommand : public nGZCommandParser::cCommandT {
public:
    static constexpr uint32_t kVTable = 0xdb5aa0;
    cLightRigAndCameraParserState *State() const { return static_cast<cLightRigAndCameraParserState *>(mState); }
    void Execute(nGZCommandParser::cIArguments *args); // slot 4, 0x2c27a0
    const char *GetName() const;                       // slot 5, 0x2c3020: "autoRig"
};

} // namespace nTSSG
