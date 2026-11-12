#pragma once
// The effects cheats: `effect` (cTSEffectCheat), `effectsManager` (cTSEffectsManagerCheat) and `overlay` (cOverlayCheat, in
// an anonymous namespace of the overlay translation unit in the game, `.?AUcOverlayCheat@?A0xfa94be60@@`; global here),
// cheat commands (ts/cCheatCommand.h) that the effects manager's constructor makes through two functions, 0xbc5aa0 and
// 0xbdb700. Each parses its arguments with an option parser (CheatOptionSpec, the game's, not ours) into its fields and acts on
// the bits the parser sets. RVAs are 1.18.0.187.
//
// ORIGINAL-BUG: the two make functions never register what they make (their vector argument is unused) and keep no
// pointer, so the three cheats leak and no parser ever runs them in game. Our constructors are hooked, so the objects are
// ours; their slots are reached only through our vtables (tests drive them directly).
#include "ts/cCheatCommand.h"
#include "ts/CountedRef.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class cITSEffectsManager;
class cITSVisualEffect;
class cTSEffectsManager;

namespace nGZCommandParser {
class cIArguments;
}

// The cheats' option parser (0x8c bytes; the class at 0xaf81e0-0xafc200, not ours: every member is the original's). Define
// takes the description and the option tuples (a pattern, the bit the option sets when present (absent for plain
// arguments), the fields its values go to, its help text; an optional value's bit after its fields; 0 ends the list), Parse
// fills the fields and the bit word (+0x70) from the arguments, Help gives an option's help text.
class CheatOptionSpec {
public:
    explicit CheatOptionSpec(uint32_t mode); // 0xaf88a0 (ret 4)
    ~CheatOptionSpec();                      // 0x2b3ef0
    CheatOptionSpec(const CheatOptionSpec &) = delete;
    CheatOptionSpec &operator=(const CheatOptionSpec &) = delete;

    void Parse(nGZCommandParser::cIArguments *args, void *parser); // 0xafb500 (ret 8)
    const char *Help(const char *name, uint32_t index);          // 0xafa670 (ret 8)
    uint32_t Bits() const { return mBits; }
    bool Has(uint32_t bit) const { return (mBits >> bit) & 1; }

    uint8_t mHead[0x70];
    uint32_t mBits; // +0x70 the options seen by the last Parse
    uint8_t mTail[0x18];
};
static_assert(sizeof(CheatOptionSpec) == 0x8c && offsetof(CheatOptionSpec, mBits) == 0x70, "CheatOptionSpec layout");

// `effect` (0xdc bytes, vtable 0xe8cf38): starts, moves and controls visual effects held in slots (`-id`).
struct cTSEffectCheat : public cCheatCommand {
    // The option bits.
    enum : uint32_t {
        kPos = 0, kPosZ = 1, kStart = 2, kStop = 3, kSoft = 4, kHard = 5, kClear = 6, kEdit = 7, kTexture = 8, kFrame = 9,
        kState = 10, kAttract = 11, kAttractPosition = 12, kPaused = 13, kHidden = 14,
    };

    explicit cTSEffectCheat(cITSEffectsManager *manager); // 0xbc46d0 (ret 4)
    ~cTSEffectCheat() override;                           // deleting 0xbc5930

    void Execute(nGZCommandParser::cIArguments *args) override; // 4: 0xbc7590
    const char *GetName() override;                             // 5: 0xbc9a90 "effect"
    const char *GetHelp(uint32_t index) override;               // 6: 0xbc6630

    cTSEffectsManager *mManager;                           // +0xc
    std::vector<fx::CountedRef<cITSVisualEffect>> mEffects; // +0x10 the effect slots
    CheatOptionSpec mSpec;                                 // +0x1c (bits +0x8c)
    const char *mName;                                     // +0xa8 [<effectName>]
    const char *mTexture;                                  // +0xac -texture
    float mPosition[3];                                    // +0xb0 -pos
    uint32_t mId;                                          // +0xbc -id
    int32_t mFrame;                                        // +0xc0 -frame
    int32_t mState;                                        // +0xc4 -state
    bool mHidden;                                          // +0xc8 -hidden
    bool mPaused;                                          // +0xc9 -paused
    float mAttractStrength;                                // +0xcc -attract
    float mAttractPosition[3];                             // +0xd0 -attract's position
};
static_assert(sizeof(cTSEffectCheat) == 0xdc && offsetof(cTSEffectCheat, mSpec) == 0x1c &&
                  offsetof(cTSEffectCheat, mName) == 0xa8 && offsetof(cTSEffectCheat, mId) == 0xbc &&
                  offsetof(cTSEffectCheat, mHidden) == 0xc8 && offsetof(cTSEffectCheat, mAttractPosition) == 0xd0,
              "cTSEffectCheat layout");

// `effectsManager` (0xb4 bytes, vtable 0xe8d3e0): queries and controls the effects manager.
struct cTSEffectsManagerCheat : public cCheatCommand {
    enum : uint32_t {
        kStats = 0, kKill = 1, kListActive = 2, kList = 3, kWhere = 4, kState = 5, kAppFlag = 6, kMemStats = 7, kVersion = 8,
        kSave = 9, kLoad = 10, kNoAlpha = 11, kShowBounds = 12, kBoundsSchema = 13, kShowColliders = 14, kLocationInfo = 15,
    };

    explicit cTSEffectsManagerCheat(cITSEffectsManager *manager); // 0xbc4fb0 (ret 4)
    ~cTSEffectsManagerCheat() override;                           // deleting 0xbc59a0

    void Execute(nGZCommandParser::cIArguments *args) override; // 4: 0xbc7e30
    const char *GetName() override;                             // 5: 0xbc9aa0 "effectsManager"
    const char *GetHelp(uint32_t index) override;               // 6: 0xbc6650 (shared_vf_bc6650: the same body as another class's)

    cTSEffectsManager *mManager; // +0xc
    CheatOptionSpec mSpec;       // +0x10 (bits +0x80)
    const char *mPattern;        // +0x9c -listActive, -list, -where, -kill
    bool mShowBounds;            // +0xa0
    bool mShowColliders;         // +0xa1
    bool mNoAlpha;               // +0xa2
    int32_t mAppFlag;            // +0xa4 -appFlag's flag
    bool mAppFlagSetting;        // +0xa8 -appFlag's setting
    int32_t mBoundsSchema;       // +0xac -1
    int32_t mManagerState;       // +0xb0 -state
};
static_assert(sizeof(cTSEffectsManagerCheat) == 0xb4 && offsetof(cTSEffectsManagerCheat, mSpec) == 0x10 &&
                  offsetof(cTSEffectsManagerCheat, mPattern) == 0x9c && offsetof(cTSEffectsManagerCheat, mAppFlag) == 0xa4 &&
                  offsetof(cTSEffectsManagerCheat, mBoundsSchema) == 0xac,
              "cTSEffectsManagerCheat layout");

// `overlay` (0xbc bytes, vtable 0xe8e748): the overlay manager of a type (terrain or water).
struct cOverlayCheat : public cCheatCommand {
    enum : uint32_t { kStatus = 0, kDecal = 1 };

    explicit cOverlayCheat(cITSEffectsManager *manager); // 0xbd9da0 (ret 4)
    ~cOverlayCheat() override;                           // deleting 0xbda350

    void Execute(nGZCommandParser::cIArguments *args) override; // 4: 0xbdade0
    const char *GetName() override;                             // 5: 0xbdb320 "overlay"
    const char *GetHelp(uint32_t index) override;               // 6: 0x76ed60 (shared_vf_76ed60)

    CheatOptionSpec mSpec;        // +0xc (bits +0x7c)
    cITSEffectsManager *mManager; // +0x98 (unused)
    int32_t mType;                // +0x9c <type>
    const char *mDecalName;       // +0xa0 -decal
    float mDecalLocation[3];      // +0xa4
    float mDecalSize;             // +0xb0 32
    float mDecalTurns;            // +0xb4 0
    uint32_t mWordB8;             // +0xb8 0x20 (nothing reads it)
};
static_assert(sizeof(cOverlayCheat) == 0xbc && offsetof(cOverlayCheat, mManager) == 0x98 &&
                  offsetof(cOverlayCheat, mDecalLocation) == 0xa4 && offsetof(cOverlayCheat, mWordB8) == 0xb8,
              "cOverlayCheat layout");

// 0xbc5aa0 (cdecl): a new effect cheat and a new effects-manager cheat for `manager`. ORIGINAL-BUG: `cheats` is unused, so
// they are never registered, and leak.
void __cdecl MakeEffectCheats(void *cheats, cITSEffectsManager *manager);
// 0xbdb700 (cdecl): a new overlay cheat. ORIGINAL-BUG: likewise never registered.
void __cdecl MakeOverlayCheat(void *cheats, cITSEffectsManager *manager);
