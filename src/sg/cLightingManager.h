// nTSSG::cLightingManager (lane IR): the scene graph's lighting manager. Interfaces: cILightingManager at +0 (vtable
// 0xdb5e90, 46 slots) and cIGZMessageTarget2 at +4 (vtable 0xdb5f4c). 0x548 bytes (the deleting destructor 0x2cbc40 frees
// that much). Hook shape: Construct (0x2ca6f0) and Destruct (0x2cb800, lane 261110t) build and tear down the members in
// the original's order on raw memory; the class's implicit constructor and destructor are never used.
//
// The light sets (0x214 bytes each) live in mLightSets; mCurrentSet picks one, and an index past the end means the
// built-in default set at +0x2d0.
#pragma once

#include "game/owned.h"
#include "rz/GameAllocator.h"
#include "rz/cRZStopwatch.h"
#include "rz/cRZString.h"
#include "sg/cSunHeightMap.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace nGZSceneGraph {
class cICompositionTreeNode;
}
namespace nGZCommandParser {
class cIFileParser;
}
class cIGZMessage2Standard;
class cIGZString;

// The manager's two vtables as owned statics (game/owned.h): what Construct and Destruct store.
TS2_OWNED_STATIC(const OwnedVTable, db5e90); // nTSSG::cLightingManager (cILightingManager face)
TS2_OWNED_STATIC(const OwnedVTable, db5f4c); // its cIGZMessageTarget2 face

namespace nTSSG {

// 0x3a97d0 (__cdecl, 17 bytes; lane 261109r, tentative name): the layer tag / lerp-set key of a sky sample: the low
// word of `sample` as an int16 when it is not negative, else 0x8000 (so rooms 0x8000-0xffff all give 0x8000). The
// lighting builders tag nodes with it; callers outside the lighting manager too (0x264690, 0x300fc0, ...).
int32_t __cdecl SkySampleKey(int32_t sample);

struct cLightVector {
    float x, y, z;
};

// One of a light set's four directional lights (0x1c bytes; lane 261108a).
struct cSetLight {
    cLightVector mColour;     // 0x00
    float mIntensity;         // 0x0c
    cLightVector mDirection;  // 0x10 unit, towards the light
};
static_assert(sizeof(cSetLight) == 0x1c);

// One light set (0x214 bytes). It starts with its name, a cRZString (FindLightSet compares it through cIGZString slot
// 11). Only the fields the replaced members read are named.
struct cLightSet {
    uint8_t mFields00[0x20];  // 0x000 the name (cRZString, 0x20 bytes)
    uint32_t mEnabled;        // 0x020 which parts the script gave (gz/LightingCommands.h LightingValues::mEnabled; bit
                              // 12 the control switch "edith", which makes slots 14 and 15 keep an object's strength)
    cSetLight mLights[7];     // 0x024 the outdoor (0-3) and indoor (4-6) lights; slot 30 returns mLights[0]'s direction
                              // negated
    const void *mAmbientKey;  // 0x0e8 the ambient map's key (the room settings message loads it with 0xb409f0), or null
    cLightVector mTint;       // 0x0ec scales the ambient-map colour
    // 0x0f8 the script's `modifiers type a c [b [d]]` (gz/LightingCommands.h): the portal builders (0x2d6c10,
    // 0x2e1c30) take their direct factors from type 0 (portals of samples other than 0) or 1, their sky factors from type
    // 2 or 3. a scales the strength, b the value, c the diffusion strength, d its falloff.
    struct Modifier {
        float a, b, c, d;
    } mModifiers[6];
    cLightVector mSkyColour;  // 0x158 added to the sky samples (0x2d7ad0)
    float mSkyReach;          // 0x164 above 0: samples brighter than it get none; else all but the blocked ones
    cLightVector mAmbient;    // 0x168 the hemisphere's base colour
    cLightVector mVector174;  // 0x174 slot 29
    cLightVector mParticles;  // 0x180 the particles light's colour (0xb3f7f0)
    cLightVector mVector18C;  // 0x18c slot 27
    float mShade198;          // 0x198 ApplyLightSet's shade (times the manager's mShadeFactor7C)
    cLightVector mVector19C;  // 0x19c slot 28
    float mSimsScale;         // 0x1a8 the flat lights' (0x2e1560) scale for the Sims' render group
    float mObjectsScale;      // 0x1ac and for the objects'
    float mIndoorScale;       // 0x1b0 the flat indoor lights' extra scale
    float mPortalSaturation;  // 0x1b4 below 1, the portal builders draw the skylight towards the ambient colour
    uint8_t mFields1B8[4];
    cLightVector mFogColour;  // 0x1bc the fog light's colour (0xb3f7f0)
    float mFogNear;           // 0x1c8 and its near and far distances
    float mFogFar;            // 0x1cc
    uint8_t mLitParams1D0[0x30]; // 0x1d0 handed to the manager's mLitObject slot 7 by ApplyLightSet (not read there)
    float mRefreshDelay;      // 0x200 RebuildStep and RebuildAll raise the manager's mField500 to at least 1000 times it
    uint8_t mFields204[0x214 - 0x204];
};
static_assert(sizeof(cLightSet) == 0x214);
static_assert(offsetof(cLightSet, mTint) == 0xec);
static_assert(offsetof(cLightSet, mAmbient) == 0x168);
static_assert(offsetof(cLightSet, mSkyColour) == 0x158);
static_assert(offsetof(cLightSet, mVector19C) == 0x19c);
static_assert(offsetof(cLightSet, mModifiers) == 0xf8 && offsetof(cLightSet, mPortalSaturation) == 0x1b4);
static_assert(offsetof(cLightSet, mAmbientKey) == 0xe8 && offsetof(cLightSet, mParticles) == 0x180);
static_assert(offsetof(cLightSet, mFogColour) == 0x1bc && offsetof(cLightSet, mFogFar) == 0x1cc);

// What slot 21 appends (20 bytes). BuildSunShadowTexture reads the four values as int32: a rectangle (x0, y0, x1, y1)
// in world units whose terrain cells mWeight (times mField2BC) raises over the ground in mSunHeights.mValues.
struct cLightSample {
    float mValues[4];
    float mWeight;
};

class cLerpLightSet;

// A counted reference the builders keep (the light vectors at +0x21c and in each sky sample, the portal records'
// objects): released (slot 2) when dropped. Only clear() and the destructor are used here.
struct cBuiltRef {
    void *mObject = nullptr;
    cBuiltRef() = default;
    cBuiltRef(const cBuiltRef &other);
    cBuiltRef &operator=(const cBuiltRef &other);
    ~cBuiltRef();
};
static_assert(sizeof(cBuiltRef) == 4);

// 0xb3f770 (cdecl; lane 261111n, sg/cLightingManagerSetNodes.cpp): nothing when `lights` is null; else its references
// released in order and the vector emptied (`kinds` kept), then AppendLightSetNodes.
void __cdecl BuildLightSetNodes(const cLightSet *set, std::vector<cBuiltRef> *lights, std::vector<int32_t> *kinds);
// 0xb3f7f0 (cdecl): a light-reference node (0x253d2018) for each enabled part of the set appended to `lights` and its
// kind to `kinds` (when not null). The offset starts at the game's white (0x10fb040). Bit 7: an ambient light
// (0xc9c81b9b) of mAmbient, intensity 1, "LM_Outdoor_Ambient", kind 7; the offset becomes white + mAmbient. Bits 0..6:
// a directional light (0xc9c81ba3) aimed along the light's direction negated, of colour mColour - offset and its
// intensity, "LM_Outdoor_Dir_<i>", kind i. Bit 8: a linear fog light (0xac06a66f) of mFogColour and near / far,
// "LM_Fog", kind 8. Bits 9 and 10: a texture light (0x6a97042f) of colour (1, mVector174) or (1, mParticles),
// "LM_Environment" kind 9 or "LM_Particles" kind 10 (that node's layer bits all removed and layer "Particles" added).
// The skylight and directional lights are made before their node, the rest after; each released after its node or
// before it accordingly. The objects made are not null-checked.
void __cdecl AppendLightSetNodes(const cLightSet *set, std::vector<cBuiltRef> *lights, std::vector<int32_t> *kinds);

// 0xb409f0 (lane 261111n; src/sg/cLightingManagerAmbientMap.cpp): a light set's ambient map. The image resource
// {0x856ddbac, group, instance, 0} (the persist system's resource manager, asked for its cIGZPersistBufferResource face)
// is asked for its cIGZBuffer; the slot's cImageData (made when empty) is set to the buffer's size (1 level, format 2,
// scale 1, 1 entry) and given each pixel's colour, rgb bytes over 255, opaque; 1. Without the resource or its buffer the
// slot's image is dropped (nulled, then released); 0. DoMessage's room settings message loads each set's map with it
// (group 0x8ba01057, the set's mAmbientKey).
uint8_t __cdecl LoadAmbientMapImage(uint32_t group, const void *instance, const void **slot);

// An extension search up a node's tree (8 bytes; lane 261110w, scenegraph LG3h; the game's class not identified): the
// first extension of the node, then of each parent in turn, that answers `iid`. Slot 13 ApplyObjectLighting's helper.
struct cExtensionSearch {
    void *mObject;                               // +0 the interface found (counted, its cIGZUnknown a virtual base), or null
    nGZSceneGraph::cICompositionTreeNode *mNode; // +4 the node searched last (not counted)
    // 0x2ca590 (ret 8): mObject null; then for the node and each parent (slot 34, asked after every node, also after a
    // find) until one answers: mNode = it, its extensions from slot 21 along slot 20, the reference held before each
    // dropped (released through its virtual base) and each asked (slot 0) for `iid` into mObject. Returns this.
    cExtensionSearch *Construct(nGZSceneGraph::cICompositionTreeNode *node, uint32_t iid);
};
static_assert(sizeof(cExtensionSearch) == 8);

// 0x2e59d0 (cdecl): a equal to b (strcmp == 0). The game's helper for the light-set names (0x2d44b0's sets and the
// seasons in 0x2e10a0).
bool StringsEqual(const char *a, const char *b);

// What the portal builder 0x2e0480 appends for each portal (0x6c bytes; lane 261109o): a portal record's own
// cPortalLight (+0x60) with the colour and +0x38 replaced. Plain data (the game's copy 0x2ca640 copies it field by
// field, the padding not); the other fields are not read here.
struct cPortalLight {
    float mColor[3];       // +0x00 the extension's colour times its intensity (FillFromExtension)
    float mPosition[2];    // +0x0c the light's x, y (slot 11)
    uint32_t mLevel;       // +0x14 the level the light is on (MakePortalLight stores its level here and at +0x20)
    int32_t mCell[2];      // +0x18 the truncated x, y (slot 11 asks the world about it)
    uint32_t mLevelAgain;  // +0x20 the level again (the same value as mLevel, as MakePortalLight stores it)
    uint8_t mDirectional;  // +0x24 the extension's directional flag (FillFromExtension: GetSourceIsDirectional)
    uint8_t mPad25[3];
    float mDirection[2];   // +0x28 the light's facing, x, y
    float mAngle;          // +0x30 its angle
    float mDirectionalFalloff;  // +0x34 FillFromExtension: GetSourceDirectionalFalloff
    float mFalloff;        // +0x38 times the modifier's d
    float mWallIntensityScale;  // +0x3c FillFromExtension: GetSourceWallIntensityScale
    float mFloorIntensityScale; // +0x40 FillFromExtension: GetSourceFloorIntensityScale
    float mInterFloorFalloff;   // +0x44 FillFromExtension: GetSourceInterFloorFalloff
    float mSourceMin[2];   // +0x48 the extension's source box
    float mSourceMax[2];   // +0x50
    float mCorners[4];     // +0x58 min x, min y, max x, max y of the rotated box about mPosition
    int16_t mReachedLevel; // +0x68 MakePortalLight: the level (cPartitionManager slot 80's low word) at the point the light reaches
    uint8_t mPad6A[2];

    // 0x3c37b0 (returns this): the colour the game's 0x100e4fc, +0x68 and +0x0c..+0x34 zero, +0x38..+0x40 1, then
    // +0x44..+0x64 the halves .5, -.5, -.5, .5, .5, -.5, -.5, .5, .5. The padding is not touched.
    cPortalLight *Construct();

    // 0x3c3a70: the box (+0x48..+0x54) turned by mAngle - the game's constant 0x...da20ec, its corners' extremes
    // added to mPosition into +0x58..+0x64 (SSE, sin / cos of the widened angle).
    void PlaceCorners();

    // 0x3c3870 (ret 4): from the node's first extension that answers the diffusion light face (0x8a561e23), else its
    // parent's, and so on: the source box +0x48..+0x54, the colour times the intensity into +0x00, the directional
    // flag +0x24, the falloffs +0x38, +0x34, +0x3c, +0x40, +0x44. Nothing is written when none has one (or the node
    // is null).
    void FillFromExtension(nGZSceneGraph::cICompositionTreeNode *node);
};
static_assert(sizeof(cPortalLight) == 0x6c && offsetof(cPortalLight, mFalloff) == 0x38 &&
              offsetof(cPortalLight, mReachedLevel) == 0x68 &&
              offsetof(cPortalLight, mSourceMin) == 0x48 && offsetof(cPortalLight, mCorners) == 0x58);

// A portal record of a sky sample (0xcc bytes; lane 261109j): two scene objects (+0 refreshed by 0x2d2830, +4 whose
// slot 34 result drops it in 0x2dffd0), the sample it lights (an int16), the portal's 2D normal (+0xc / +0x10; 0x2d88b0
// weights the two sides by their signs), the shade ShadePortals leaves (+0x14), the portal's segment (+0x18) and its
// colour (+0x30). The portal builders (lane 261109o) read +0x3c on: mObject is then a light-reference node
// (nGZSceneGraph::cILightRefNode) and mOther one too, or null.
struct cPortalRecord {
    cBuiltRef mObject;          // +0x00
    cBuiltRef mOther;           // +0x04
    int16_t mSample;            // +0x08 an index into the sky samples; -1 and 0 add nothing
    uint8_t mField0A[2];
    float mNormal[2];           // +0x0c x, y
    float mShade;               // +0x14 1 less what facing portals of the same sample and level take (ShadePortals)
    float mSegment[6];          // +0x18 {x0, y0, level0, x1, y1, level1}, the levels int32 (+0x20 compared raw)
    float mColor[3];            // +0x30
    uint8_t mScaleDirect;       // +0x3c 0x2d6c10 then scales the direct weight by the manager's mPortalDirectScale
    uint8_t mField3D[3];
    float mStrength;            // +0x40 the direct light's strength (0x2e0480: AddExisting / AddPoint's scale)
    float mTint[3];             // +0x44 its colour
    float mValue;               // +0x50 and value
    float mPosition[3];         // +0x54 the sky light's position (AddPoint)
    cPortalLight mLight;        // +0x60

    // 0x2cada0 (returns this): no objects, no sample, shade 1, the segment {-1, -1, 0, -1, -1, 0} (levels 0 as int32),
    // the colour the game's zero vector 0x10d9424, mScaleDirect clear, strength 0, the tint the grey 0xffd994, value 1,
    // position 0, then mLight.Construct(). Raw stores: the references are not released.
    cPortalRecord *Construct();
};
static_assert(sizeof(cPortalRecord) == 0xcc && offsetof(cPortalRecord, mColor) == 0x30 &&
              offsetof(cPortalRecord, mShade) == 0x14 && offsetof(cPortalRecord, mSegment) == 0x18 &&
              offsetof(cPortalRecord, mStrength) == 0x40 && offsetof(cPortalRecord, mPosition) == 0x54 &&
              offsetof(cPortalRecord, mLight) == 0x60);

// A sky sample's lerp light set (a cRZRefCount; may be null): a copy counts it (slot 2), the destructor releases it
// (slot 1), as the game's sky-sample copy (0x2cae70) and destroy (0x2c90d0) do.
struct cLerpRef {
    cLerpLightSet *mSet = nullptr;
    cLerpRef() = default;
    cLerpRef(const cLerpRef &other);
    cLerpRef &operator=(const cLerpRef &) = delete;
    ~cLerpRef();
    cLerpLightSet *operator->() const { return mSet; }
    explicit operator bool() const { return mSet != nullptr; }
};
static_assert(sizeof(cLerpRef) == 4);

// A sky sample (0x8c bytes): six accumulated vectors, their weights, the counts the samplers raise, the lights the
// builders made for it, two lerp light sets (either may be null) and its portal records. The manager keeps them in a
// std::vector (the game's instances, 0x2e5a10 resize among them, are library code): the default constructor (0x2cb050)
// gives the game's zero vector six times and zeros the rest; copies (0x2cae70, the only way the vector moves them) count
// every reference again; the destructor (0x2c90d0's body) lets go of mLerp88, mPortals, mLerp78 and mLights in turn.
struct cSkySample {
    cSkySample();
    cSkySample(const cSkySample &) = default;
    cSkySample &operator=(const cSkySample &) = delete;
    ~cSkySample() = default;

    // 0x2e0210: the six vectors (the game's zero vector 0x10d9424), their weights and the three counts (+0x60..+0x68) zeroed.
    void Clear();

    float mVectors[6][3];               // +0x00
    float mWeights[6];                  // +0x48
    uint32_t mTileCount;                // +0x60 0x2d8410 means to count its tiles (ORIGINAL-BUG: it never grows)
    int32_t mWallCount;                 // +0x64 0x2d8e00 counts its wall portals
    int32_t mPortalCount;               // +0x68 0x2d88b0 counts its portal records
    std::vector<cBuiltRef> mLights;     // +0x6c
    cLerpRef mLerp78;                   // +0x78
    std::vector<cPortalRecord> mPortals; // +0x7c
    cLerpRef mLerp88;                   // +0x88
};
static_assert(sizeof(cSkySample) == 0x8c && offsetof(cSkySample, mLights) == 0x6c &&
              offsetof(cSkySample, mPortals) == 0x7c);

// A sky light configuration (lane 261109m, scenegraph LG4c): the words the config light creators read (the record may
// be longer). Below, v0..v5 are a sky sample's six vectors: 0..3 the four sides, 4 from above (the floors), 5 the other
// vertical one.
struct cLightConfig {
    uint32_t mType;          // +0x00 0 bounce lights, 1 min-side lights, 2..8 ambient lights with type - 2 deltas
    float mBrightness;       // +0x04 every colour's last factor
    float mVerticalScale;    // +0x08 the vertical (v4, v5) colours' scale
    float mSideSum;          // +0x0c the four sides' sum times this is added to v4 and v5
    float mSideScale;        // +0x10 the side (v0..v3) colours' scale
    float mUpToSides;        // +0x14 v4 times this is added to each side
    float mTilt;             // +0x18 the directions less the game's up normal times this
    float mFactor;           // +0x1c the directional lights' SetFactor
    float mLightMapWeight;   // +0x20 not 0 (or NaN): the ambient light is a cAmbientFromLightMapLight of this weight
    float mLightMapScale;    // +0x24 a quarter of it is that light's scale
    float mDeltaIntensity;   // +0x28 the ambient creator's directional lights' intensity
    float mAdjust;           // +0x2c above 0: the first delta light reshapes what is left (CreateAmbientLight)
};
static_assert(sizeof(cLightConfig) == 0x30);

// A room light configuration with a string after it (0x50 bytes; the name cLightSettingsBlock is lane o's, tentative).
// The manager holds three (+0xdc "object", +0x12c "sim", +0x17c "wall"); the lighting parser keeps a vector of them (its
// reset 0xb3d8a0 empties it and appends a new one). Nothing ours reads mText.
struct cLightSettingsBlock {
    cLightConfig mConfig; // +0x00
    cRZString mText;      // +0x30

    // 0xb3d240 (returns this): type 4, brightness 1, vertical scale 3, side sum 0.2, side scale 3, up-to-sides 0.2,
    // tilt 0.5, factor 0, light-map weight 0, light-map scale 1, delta intensity 1, adjust 0; mText empty.
    cLightSettingsBlock *Construct();
    // 0x2cbbb0: mText's destructor (mConfig is plain data).
    void Destruct();
};
static_assert(sizeof(cLightSettingsBlock) == 0x50 && offsetof(cLightSettingsBlock, mText) == 0x30);

// A resource key as the resource manager lists them (16 bytes; the first word is the type, the others are only copied
// to the manager's calls) and the list the manager's slot 19 fills (a vector's three pointers; its block is the
// allocator service's).
struct cLightingKey {
    uint32_t mType;
    uint32_t mWord4;
    uint32_t mWord8;
    uint32_t mWordC;
};
static_assert(sizeof(cLightingKey) == 0x10);
struct cLightingKeyList {
    cLightingKey *mBegin;
    cLightingKey *mEnd;
    cLightingKey *mCapacity;
};

class cLightingManager {
public:
    // 0x2dfda0 slot 0 (ret 8): ids 1 and 0x6b390bfe give the object, 0x452294aa its message-target face (+4); each
    // AddRefs through slot 1. False for other ids and a null `out`.
    bool QueryInterface(uint32_t iid, void **out);
    // 0x2dfee0 slot 2: the count (+8) drops by one; at zero the object deletes itself (slot 38 (1)) and 0 is returned.
    uint32_t Release();
    // 0x2e1010 slot 18 (ret 4): mFlag44 = `value`, mUseDefaultName = 1.
    void SetFlag44(uint8_t value);
    // 0x2d8370 slot 20: 0x2d4010 (0), then, when the global 0xffde64 is set, 0x2dfc80 (1.0); mFlag81 = 1.
    void Refresh();
    // 0x2cbd40 slot 21 (ret 8): appends {values[0..3], weight} to mSamples.
    void AddSample(const float *values, float weight);
    // 0x2ded30 slot 23: the game's default name (0xdaefa0) while mUseDefaultName is set, else mName's text.
    const char *Name();
    // 0x2db060 slot 25: the word at +0x404 of the object at +0x2c.
    uint32_t Value404();
    // 0x2e3690 / 0x2d8310 / 0x2d4e20 slots 27, 28, 29 (by value): the current light set's vectors at +0x18c, +0x19c and
    // +0x174.
    cLightVector Vector18C();
    cLightVector Vector19C();
    cLightVector Vector174();
    // 0x2df7e0 slot 30 (by value): the current light set's direction, negated.
    cLightVector NegatedDirection();
    // 0x2df860 slot 31 (by value): mVector2C4.
    cLightVector Vector2C4();
    // 0x2ded50 slot 34 (ret 4): the colour mRef24's slot 16 gives for `key`, weighted by the game's three luminance
    // factors (0xdadf5c, 0xdb687c, 0xdb6874).
    float Luminance(uint32_t key);
    // 0x2cbc40 slot 38 (ret 4): Destruct, then the memory (0x548 bytes) freed when `flags` bit 0 is set. Returns the
    // object.
    cLightingManager *DeletingDestructor(uint32_t flags);

    // Construction and destruction (lane 261110t, scenegraph LG3f; src/sg/cLightingManagerCreate.cpp).
    // 0x2d83a0 (__cdecl; the COM director's factory for class 0x0b390bf7): operator new (0x548), then Construct; the
    // block is freed when Construct throws.
    static cLightingManager *__cdecl Create();
    // 0x2ca6f0 (returns this): both vtables, no references, the nine message types DoMessage answers in mMessageTypes,
    // the defaults below, mCurrentSet -1 while one light set (the defaults named by the game's text 0xdb6194) is
    // appended to mLightSets, then 0. Allocations in order: mMessageTypes', mSetD4's and mSet214's heads (the last the
    // allocator service's), the nine message-type nodes, mLightSets' block.
    cLightingManager *Construct();
    // 0x2cb800: both vtables stored, then the members torn down from the last to the first (the strings freed, the
    // vectors with their elements' destructors, mAmbientMaps' and mBuiltLights' references released, the sets, the
    // settings blocks). The counted pointers are released when not null and not cleared: mRef23C, mRef238 (slot 2),
    // mLerp230, mLerp22C, mLerp228 (slot 1) between the height map and mBuiltLights; after mPortalLights mAmbientImage,
    // mShadeImage (slot 2), the texture handle (cTextureHandle::Destroy), mFileParser (slot 2), mRef2C's virtual base
    // (slot 2 of the object its vbtable's second entry places), mSceneManager, mRef24, mWorld, mTerrain, mLitObject
    // (slot 2); mMessageTypes last.
    void Destruct();

    // Initialisation and shutdown (lanes 261110v and 261110u, scenegraph LG3g; src/sg/cLightingManagerInit.cpp).
    // 0x2d98c0 slot 3: always true; nothing when mInitialised is set, else it is set and: the message types registered
    // (this object's message face); with the byte 0xffde34 a new cDiffusionManager (0x94 bytes) into mLitObject
    // (AddRef 1 / Release 2, nothing when equal) and its slot 3 Init (the member re-read); the world database (0x20e0b2,
    // cached) into mTerrain likewise; a new nTSSG::cLightingParserState (0x43c bytes; sg/cLightingParserState.h) into
    // mRef2C (counted through its virtual base); the director (fetched first) asked for a cFileParser into mFileParser
    // (the old one nulled and released first), its slot 3 Init and slot 5 SetParser(mRef2C's cIBaseParser face, 0), both
    // unchecked; the director's shade texture image (class 0x2a956dad) into a local, filed with the texture manager
    // (slot 20, name 0xdb60ac, 1) and its id handed to mShadeTexture through a temporary handle; mRef238 (a
    // cLightRefNode) and mRef23C (a cLinearFogLight) remade as mFileParser and joined (mRef238's slot 93 SetLight,
    // unchecked); the settings service told to watch the 11 option rows (slot 4: target, id, key, the row's value cell
    // 0xffde30 + 12 * i); the six group masks zeroed, then or'd with the registry's bits (each member re-read after the
    // call; the environment group also or'd with the first five as they then are, before the shade group is stored);
    // new mLerp22C and mLerp230 (AddRef 2 / Release 1) keyed (SkySampleKey(0), 0) and (…, 1); the settings service and
    // the image released.
    bool Init();
    // 0x2e2b60 slot 4: always true. With mInitialised set: it and mSunShadows cleared; the settings service (framework
    // service 0xac1887e / iid 0x6bb22f38) when there is one told (slot 5: this object's message face, the id, a
    // temporary key made from the name cell) to stop watching the 11 option rows (ids 0x2bf18d56, then 0x2bf18d58 to
    // 0x2bf18d61; the name cells 0xffde38 + 12 * i); mShadeTexture reset; mFileParser's slot 4 (not null-checked) and its
    // release (nulled first); mRef2C (nulled first, released through its virtual base), mTerrain, mWorld, mRef24
    // (nulled first, slot 2); mLitObject's slot 4, then, when still set, nulled and released; the message types
    // unregistered; with a scene manager RemoveSampleLights, RemoveBuiltLights, then (when still set) it nulled and
    // released; mRef238 and mRef23C released; the sky samples destroyed (the vector emptied); mLightingPath emptied;
    // mLerp22C and mLerp230 nulled and released (slot 1); the settings service released.
    bool Shutdown();

    // Light evaluation (lane 261108a, scenegraph LG1).
    // 0x2d6740 (ret 8): the ambient light for a unit `normal`. With an ambient map for the current set (mAmbientMaps,
    // indexed like mLightSets), the map's texel at ((n.x + 1) / 2 * width, (n.y + 1) / 2 * height), floored and kept
    // inside the map, times the set's tint. Otherwise the hemisphere: each of the set's four lights weighs
    // max(0, n . direction) * intensity, and the colour is the set's ambient colour times (1 - the weights) plus each
    // light's colour times its weight.
    void EvaluateAmbient(const float *normal, float *out);
    // 0x2d74b0 (ret 0xc): EvaluateAmbient with the sun's light 1 / light 0 shaded by `shade` (lane 261108b): an ambient
    // map is read at the normal bent towards light 1's direction (mSunHeights.Blend with t = shade), times the tint
    // (texel first); the hemisphere's light 0 weighs max(0, n . direction) * intensity * max(0, 1 - shade *
    // mSunFalloff).
    void EvaluateAmbientShaded(const float *normal, float shade, float *out);
    // 0x2d66f0 (ret 8): EvaluateAmbient times mAmbientScale1EC.
    void EvaluateAmbientScaled(const float *normal, float *out);
    // 0x2d7930 slot 17 (ret 0xc): EvaluateAmbient times mAmbientScale1F0; the first argument is not read.
    void AmbientForNormal(uint32_t unused, const float *normal, float *out);
    // 0x2d7980 slot 16 (ret 0xc): the ambient light on the terrain at world (x, y): EvaluateAmbient of TerrainNormal,
    // times mAmbientScale1F0.
    void AmbientAt(float x, float y, float *out);
    // 0x2e3280 (ret 0xc; returns out): the terrain's unit normal at world (x, y), from the twelve heights around the
    // cell (x, y) * mTerrainScale falls in: central differences at the cell's corners, interpolated by the fractions.
    float *TerrainNormal(float *out, float x, float y);
    // 0x2e3640 (ret 8; the value on the x87 stack): the height mTerrain's slot 62 gives for vertex (i, j).
    double TerrainHeight(int32_t i, int32_t j);
    // 0x2e31d0 slot 32 (ret 4): how much sun reaches p: 1 unless mSunShadows is set and p lies inside the sun height
    // map below its stored height, then 1 - depth * mSunFalloff, at least 0.
    float SunVisibility(const float *p);
    // 0x2e0320 slot 35 (ret 4): 0: a quarter of the current set's first light (colour times intensity) weighted by the
    // luminance factors; 1 and up: the luminance of the average of entry `index`'s six vectors in mSkySamples; 0 when
    // negative or past the end.
    float SampleLuminance(int16_t index);
    // 0x2d6b80 (ret 4): the index of the first light set whose name equals `name` (case-insensitive), or -1.
    int32_t FindLightSet(const cIGZString &name);
    // 0x2d37c0: mLightSets[mCurrentSet], or mDefaultSet when the index is past the end.
    const cLightSet &CurrentSet() const;

    // The lifecycle's small slots (lane 261108h, scenegraph LG3). Objects are lit through their "ObjectLighting"
    // extension (0xaeb5b0 finds it by name; class not identified): its slot 3 (name, float) sets a value, slot 15 (name,
    // &float) reads one.
    // 0x2d39f0 slot 12 (ret 4): when `object` has the extension, slot 14 (object, 0, 0, 0), then the extension's
    // virtual base (through its vbptr at +4) handed to that base's slot 18 result's slot 23.
    void ResetObjectLighting(void *object);
    // 0x2d2610 slot 14 (ret 0x10): with a world and the extension, "SimAnticsStrength" = strength; unless byte +0x4d
    // is set and the current set's word +0x20 has bit 12, the strength becomes 1. Nothing more when "Strength" reads
    // within 0.001 of it; else "Strength" = it, slot 13 (object, 0), the dirty byte +0x80 set and +0x500 raised to at
    // least +0x508 * 1000. The second and fourth arguments are not read.
    void SetObjectStrength(void *object, uint32_t unused, float strength, uint32_t unused2);
    // 0x2d2720 slot 15 (ret 0x10): as slot 14 without the comparison: "Strength" always set, then slot 13 (object,
    // `source`'s slot 5); the dirty byte and +0x500 only when the object's slot 134 answers true.
    void UpdateObjectStrength(void *object, void *source, float strength, uint32_t unused);
    // The objects' lights (lane 261110w, scenegraph LG3h; src/sg/cLightingManagerObjects.cpp).
    // 0x2d14b0 slot 11 (ret 4): an object whose owner (slot 120) is of kind 8 or 9 (the owner's slot 98, a short)
    // has every light-reference node below it (slot 44, filter mask bit 0xf) switched off (kAttrOne = 0); any other
    // kind gets its "ObjectLighting" extension (0xaeb3f0, made if missing) when 0xaead10 (object, 2) says so.
    void PrepareObjectLighting(void *object);
    // 0x2e4c40 slot 13 (ret 8): with the object's "ObjectLighting" extension, its "Strength" (1 when unread) times a
    // jitter in [1 - mObjectLightJitter, mObjectLightJitter + 1] (0 while g_DiffusionLighting 0xffde34 is clear) lights
    // every light-reference node below the object that `name` picks: the light's tag equal to `name`
    // (case-insensitive); with no name, any untagged light, any tag not ending "_designmodelight" (lowercased), and a
    // design-mode light whose stem (lowercased) is one of the object's two design-mode names (0x3aab80, 0x3ab990).
    // A picked light gets the current set's modifier 5 (its layer word equal to sample 0's key) or 4 applied to its
    // diffusion extension (intensity * strength * c * mField50, falloff * d) and attributes (kAttrOne * a * strength
    // * mField50, kAttrValue * b); a light not picked is switched off (kAttrOne 0, the extension's intensity 0).
    void ApplyObjectLighting(void *object, const char *name);
    // 0x2df290 slot 33 (ret 8): the ambient image at +0x3c (null: false) read at world xy * mTerrainScale, floored
    // and held inside [0, mTerrainWidth - 1] x [0, mTerrainHeight - 1] (unsigned compares: a size of 0 gives
    // 0xffffffff): its slot 38 texel copied to `out`; true.
    bool AmbientImageAt(const float *xy, float *out);
    // 0x2e1020 slot 19 (ret 4): `name` equal (strcmp) to the game's default name: mUseDefaultName = 1 and the time
    // of day picks the set (0x2e4480); else mUseDefaultName = 0 and, when 0x2e10a0 (name) picks a set, 0x2d0730
    // and bytes +0x80 and +0x83 set.
    void SelectLightSet(const char *name);
    // The selectors (lane 261110q, scenegraph LG3c; src/sg/cLightingManagerSelect.cpp).
    // 0x2e4480 (ret 0): with mInitialised and mUseDefaultName set, the set for the time of day: the clock's (0x20df9a)
    // slot 15 (4) kind as a short (0 day; 1 day, or "evening" with mMorningEvening; 2 "night"; 3 "night", or "morning"
    // with mMorningEvening; else day); not day: "vacationNight" when the sim world (singleton slot 6's slot 15) slot
    // 31 says so; day: with option bit 7 the weather (singleton slot 33; slot 35 == 1 "overcast", else its slot 17
    // season 0 / 2 / 3 "spring" / "autumn" / "winter", and then a name FindLightSet does not know becomes "day"),
    // else (the option asked again, clear) a vacation world's slot 18 5 / 6 "spring" / "autumn". Then
    // SelectHoodLightSet (name), and when mName's text then differs from before, ApplyLightSet and bytes +0x80 and
    // +0x83 set.
    void SelectTimeOfDayLightSet();
    // 0x2e10a0 (ret 4): the hood time (SetSkyMode) for the names of day (1), night (3), morning (0) and evening (2);
    // the sg system's slot 31 for "neighborhoodnight" / "neighbourhood", else the lot skirt's skybox texture for
    // "day" / "night"; then with the portal switch (0xffde40) clear the set `name` + "NoPortals", else (or not found)
    // the set `name`: mCurrentSet and mName set; false when neither is found.
    bool SelectHoodLightSet(const char *name);
    // 0x2e29f0 (ret 4): mSkyMode = kind; the effects manager's (0xb39000) slot 31 (index, value) for indices 0 and 1:
    // kind 0: (0, 0) (1, 1); 1: (0, 1) (1, 0); 2: (0, 1) (1, 1); 3: (0, 0) (1, 0); other kinds nothing.
    void SetSkyMode(uint32_t kind);
    // 0x2dfe10 (__cdecl): when the material manager has `material` loaded, its definition's slot 7 object (AddRef'd
    // when not null) is given ("skyboxCubeTexture", texture) by its slot 13 and handed back to the manager's
    // AddMaterial (obj, 0), then released.
    static void SetSkyboxCubeTexture(const char *material, const char *texture);
    // 0x2d9480 slot 36 (ret 4): the current lot's (GetCachedSlot23's slot 9) slot 55 value for `id`.
    uint32_t LotValue(uint32_t id);
    // 0x2e29b0 slot 37 (ret 8): the current lot's (slot 10) slot 56 (id, value).
    uint32_t SetLotValue(uint32_t id, uint32_t value);

    // 0x2e3120 (__cdecl; returns out): copies the segment `in` ({x, y, level} twice; the levels are integers) to out,
    // its ends swapped unless they are in order: by level, then by x, then by y.
    static float *__cdecl OrderSegment(float *out, const float *in);
    // 0x2e56b0 (__cdecl; returns out): the 2D normal (y1 - y0, -(x1 - x0)) of the segment `in` (as OrderSegment's),
    // divided by its length when that is above 0.
    static float *__cdecl EdgeNormal(float *out, const float *in);
    // 0x2d79f0 (__cdecl): a face kind's sign, offset and flag: 3 (-1, 0, set), 6 (1, 1, set), 12 (-1, 0, clear), any
    // other (1, 1, clear); the offset then less 0.01.
    static void __cdecl FaceKind(char kind, float *sign, float *offset, bool *flag);
    // 0x2daee0 (__cdecl; returns out): the sum of six vectors, added in order to 0.
    static float *__cdecl SumOfSix(float *out, const float *vectors);
    // 0x2dae80 (__cdecl; returns out): SumOfSix divided by 6.
    static float *__cdecl AverageOfSix(float *out, const float *vectors);

    // Lightmap lighting (lanes 261108b and 261108e, scenegraph LG2): slots 39 to 44 light lightmap textures. Each
    // lighter adds a colour to the texels [x0, x1) x [y0, y1) (unsigned bounds) of a texture whose row y starts at
    // pixels + y * stride words: per component, clamped to [0, 1] and scaled to a byte (TexelByte), added to the
    // texel's red (x), green (y) and blue (z) byte, each sum held at 255; alpha kept. Across a tile u runs from 0 at
    // x0 by 1 / (x1 - x0), v from 1 - 1 / (y1 - y0) at y0 down by the same (float steps of a double quotient).
    // 0x2d6500 (__stdcall, ecx not read): adds `colour` to every texel.
    static void __stdcall AddFlatQuad(const float *colour, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                                      uint32_t *pixels, int32_t stride);
    // 0x2d6230 (__stdcall): adds `colour` to the texels of the half tile face kind `kind` picks (FaceKind's sign,
    // offset and flag): those where (offset > u * sign + v) differs from the flag.
    static void __stdcall AddFlatTriangle(char kind, const float *colour, uint32_t x0, uint32_t y0, uint32_t x1,
                                          uint32_t y1, uint32_t *pixels, int32_t stride);
    // 0x2dcc40 (ret 0x24): lights the tile at `tile` (x, y) from its corners: `normals` slot 22 gives the normals of
    // vertices (x, y), (x + 1, y), (x, y + 1), (x + 1, y + 1), EvaluateAmbient their colours, blended per texel with
    // weights u (1 - v), (1 - v)(1 - u), v (1 - u) (in doubles, stored as floats) and u v, times
    // scale * mAmbientScale1F0.
    void LightQuad(const void *normals, const int32_t *tile, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                   uint32_t *pixels, int32_t stride, float scale);
    // 0x2db070 (ret 0x24): LightQuad over the half tile the tile's face kind (byte +0xc) picks, as AddFlatTriangle.
    void LightTriangle(const void *normals, const int32_t *tile, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                       uint32_t *pixels, int32_t stride, float scale);
    // The shadowed lighters: slots 39 and 41 pick them over LightQuad / LightTriangle. The tile record is {x, y, level,
    // kind byte}; the terrain's slot 61 (tile, kind) says whether the tile is lit per vertex (level 0 only). Texel
    // (col, row) reads cell (y * 8 - 1 + y1 - row, x * 8 + col - x0) of mSunHeights' grids (mWidth per row): the depth
    // is mHeights there minus, per vertex, mGround there, or else the terrain's slot 62 height of {x, y, level}. Per
    // vertex the corner normals (slot 22, as LightQuad) are blended per texel with LightQuad's weights and divided by
    // their length (a float of the double square root); otherwise the normal is the global up vector (0xffd918). The
    // colour is scaled by scale * mAmbientScale1F0 and added as in LightQuad.
    // 0x2dd1b0 (ret 0x24): the alpha byte is cleared, then where the depth is above 0 set to depth * mSunFalloff (at
    // most 1) as a byte; the colour is EvaluateAmbient's.
    void LightQuadShadowMask(const void *normals, const int32_t *tile, uint32_t x0, uint32_t y0, uint32_t x1,
                             uint32_t y1, uint32_t *pixels, int32_t stride, float scale);
    // 0x2db640 (ret 0x24): LightQuadShadowMask over the half tile, as LightTriangle.
    void LightTriangleShadowMask(const void *normals, const int32_t *tile, uint32_t x0, uint32_t y0, uint32_t x1,
                                 uint32_t y1, uint32_t *pixels, int32_t stride, float scale);
    // 0x2ddb80 (ret 0x24): where the depth is above 0 the alpha byte becomes sqrt(1 - depth * mSunFalloff) (at least
    // 0) as a byte and the colour EvaluateAmbientShaded's for the depth; elsewhere alpha is kept and the colour is
    // EvaluateAmbient's.
    void LightQuadSunShaded(const void *normals, const int32_t *tile, uint32_t x0, uint32_t y0, uint32_t x1,
                            uint32_t y1, uint32_t *pixels, int32_t stride, float scale);
    // 0x2dc0e0 (ret 0x24): LightQuadSunShaded over the half tile, as LightTriangle.
    void LightTriangleSunShaded(const void *normals, const int32_t *tile, uint32_t x0, uint32_t y0, uint32_t x1,
                                uint32_t y1, uint32_t *pixels, int32_t stride, float scale);
    // 0x2d7ad0 (ret 0xc): the current set's sky colour weighted into the sky samples. Nothing (false) without sky
    // samples, with mFlag44 clear, or when the colour is within 32 ulps of the game's zero vector (0x10d9424) in
    // every component. `weights` is resized to the sample count; with mSkyReach above 0 each sample's weight is
    // 1 - m / mSkyReach where m (the largest component of its six vectors' average, magnitude) is below it, else
    // 0; otherwise 1, and 0 for the samples the `lights` records (0x6c bytes, the sample index an int16 at +0x68)
    // name. Samples whose index has low 16 bits 0 (sample 0) get no light. Each sample of positive weight gets
    // colour * weight added to its first five vectors; true when one did.
    bool AddSkyLight(const uint8_t *lights, int32_t count, std::vector<float> *weights);
    // 0x2d2420 slot 45 (ret 8): AddSkyLight into a local weights vector; when it lit a sample, each sample of positive
    // weight (16-bit index not 0) has mLitObject's slot 22 called with the index and the current set's sky colour *
    // weight * (this object's slot 36 for the index, an int16) * 0.01.
    void SendSkyLight(const uint8_t *lights, int32_t count);
    // 0x2cc370 slot 41: lights the tiles mLitObject's slot 16 lists (16-byte records {x, y, level, kind byte}) into
    // their lightmap textures. mRef24's slot 23 (1) gives the scale, its slot 19 a tile's texel rectangle and texture
    // index (tiles it fails skipped), its slot 22 (1, index) the texture when the index changes (slot 9 (0) the texels,
    // 28 the pitch in bytes, 27 the height; rows bottom-up); the normals source is slot 39 of the service the global
    // getter 0x20defb returns. Kind 15 tiles light as quads, others as triangles: LightQuadSunShaded /
    // LightTriangleSunShaded when mShadeTiles is set, else LightQuad / LightTriangle at level 0 where the terrain's
    // slot 61 (tile, kind) says so, else the up normal's ambient colour times scale * mAmbientScale1F0 (AddFlatQuad /
    // AddFlatTriangle).
    void LightTiles();
    // 0x257cf0 (thiscall, this unread, ret 8): the strict order of the wall-segment sets (and of world-lot code's):
    // segments {x0, y0, level, x1, y1, level} (levels as int32) order by the first level; when both start points are
    // within 32 ulps per coordinate, by the second level, then x1 (y1 when x1 is equal); else by x0 (y0 when equal).
    static bool __stdcall SegmentLess(const float *a, const float *b);
    // 0x2de610 (ret 0x24): a wall side's texels lit with sun shading. Quad corners come from 0x2d8120 (wall,
    // float[12]); texel (u, v) (u across from 0, v down, steps 1 / (w - 1) and 1 / (h - 1), or fixed half-texel steps
    // for 4 x 8 rectangles) is interpolated between them, pushed out by normal / 8, and its depth below mSunHeights'
    // mHeights (cell (y + 0.001) * mScale, (x + 0.001) * mScale) picks EvaluateAmbientShaded over the normal's plain
    // ambient colour. The colour, scaled by scale * mAmbientScale1F0, is stored back (the next texel starts from it)
    // and added.
    void LightWallShaded(const float *wall, const float *normal, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                         uint32_t *pixels, int32_t stride, float scale);
    // 0x2cbd90 slot 39: LightTiles over the outdoor tiles (mWorld's slot 76 for room 0) that the terrain shows (slot
    // 53 or 55 (tile, kind, 1)); shaded tiles use LightQuadShadowMask / LightTriangleShadowMask when the global byte
    // 0xffdeac is set. With mDrawSunHeights set, texture 0 of level 1 is overwritten with mSunHeights' mValues (a
    // debug view: value / 3 split into red, green and blue ramps).
    void LightOutdoorTiles();
    // 0x2cc660 slot 40: every wall side of mWorld (slots 55 count, 56 (&wall, i), 50 kind) once (a set of segments in
    // SegmentLess order) lit by LightWallSide at level 0, the far side with the segment's ends swapped.
    void LightWalls();
    // 0x2ccb80 slot 42: LightWallSide for the near side of each wall mLitObject's slot 17 lists (no set, no far side).
    void LightListedWalls();
    // 0x2d1720 slot 43 (ret 8): AddSkyLight, then the sky colour times mRef24's slot 23 (1) and each room's weight
    // added flat to the room's shown tiles (mWorld's slot 76 per room of positive weight, room 0 skipped), and times
    // slot 23 (0) and the weight to each wall side (once per segment, as LightWalls) whose room (0x7f2340) is not 0.
    void LightRoomsBySky(const uint8_t *lights, int32_t count);
    // 0x2d1ec0 slot 44 (ret 8): LightRoomsBySky over the tiles (slot 18, room from mWorld's slot 79) and walls (slot
    // 19, near sides only) mLitObject lists; tiles are not checked for being shown nor for room 0.
    void LightListedBySky(const uint8_t *lights, int32_t count);

    // The light builders' leaves (lane 261109j, scenegraph LG4a; src/sg/cLightingManagerBuild.cpp).
    // A vector's size is the largest of its components' magnitudes (std::max of |x| and std::max(|y|, |z|), so a NaN
    // y or z gives way to the other and a NaN x is taken only when the rest are not above it).
    // 0x2dedb0 (__cdecl): the index of the largest of six vectors (the first on ties; NaN sizes never win).
    static int32_t __cdecl LargestOfSix(const float *vectors);
    // 0x2def70 (__cdecl): as LargestOfSix over four vectors.
    static int32_t __cdecl LargestOfFour(const float *vectors);
    // 0x2df0a0 (__cdecl): the index of the smallest of six vectors, as LargestOfSix.
    static int32_t __cdecl SmallestOfSix(const float *vectors);
    // 0x2dff00: every light in mBuiltLights removed from the scene manager (slot 9), then released and dropped; then,
    // unless both g_RefreshSwitch and the byte 0xffde34 are set, mLerp228 cleared (cLerpLightSet::Clear).
    void RemoveBuiltLights();
    // 0x2dffd0: per sky sample: its lights removed from the scene manager; its lerp sets (when not null) marked inactive
    // and faded out (byte 0xffde34 clear) or, with the byte set and g_RefreshSwitch clear, cleared; each portal record's
    // +4 object (when not null) handed to the slot 46 of its own slot 34 result; the lights released and dropped; the
    // portal records dropped.
    void RemoveSampleLights();
    // 0x2dfc80 (ret 4): mLerp228, mLerp22C and mLerp230 evaluated at `t` (cLerpLightSet::Evaluate), then each sky
    // sample's two lerp sets when not null; then mRef24's slot 11 (t).
    void EvaluateLerpLights(float t);
    // 0x2d2830: unless g_RefreshSwitch is set, each portal record's two objects (when not null) asked for interface
    // 0x8a561e23 through their slot 24 (iid, &out, 0); each one found gets its slot 3 (0). One reference is kept
    // across records (released through its virtual base before the next query, and at the end).
    void RefreshPortalObjects();
    // 0x2e4b50: every scene object (the scene manager's slot 17 scene, its slot 44 iterator with the
    // cTSSGObjectsFilter strategy) whose slot 25 answers true for 0xfa1c39ed handed to this object's slot 13 (object, 0).
    void RelightSceneObjects();
    // 0x2e4300: with mShadeTiles or mDrawSunHeights set: when mSunMapDirty or mFlag82 is set, mSunHeights rebuilt (its
    // blend scale from mBlendScale240, BuildGround over mTerrain at 8 cells per tile, with mShadeWalls also AddWalls
    // towards the current set's sun direction (+0x34), AddRoofs and AddAwnings towards its negation) and mSunMapDirty
    // cleared; then BuildShadow towards the sun direction. A stopwatch (mode 5) is restarted around both parts and
    // stopped after each. Then mSunShadows = mShadeTiles.
    void RebuildSunHeights();
    // 0x2d8b40: every sky sample cleared, then filled from the rooms' tiles (0x2d8410), the wall portals (0x2d8e00) and
    // the portal records (AddPortalRecords), a stopwatch (mode 5) restarted before each; then each vector divided by its
    // weight where the weight is not 0.
    void SamplePortals();
    // 0x2d88b0: each portal record whose sample (int16, sign-extended) is neither -1 nor 0 and below the sample count
    // adds its colour times mPortalScale to that sample: times the front weight to vector 0 (front above 0; the weight
    // to weight 0) or, negated, to vector 1 (weight 1 less it); likewise the back weight with vectors 2 and 3. The
    // sample's mPortalCount is raised.
    void AddPortalRecords();

    // The config light creators (lane 261109m, scenegraph LG4c; src/sg/cLightingManagerConfigLights.cpp). Each makes
    // light-reference nodes (the director's class 0x253d2018) holding directional (0xc9c81ba3) or ambient (0xc9c81b9b)
    // standard lights; a node gets the layer tag, all layer bits removed then `bits` added, its light, a direction
    // (0xaeee30) for directional lights, joins sample.mLights and the scene manager (slot 7) and is named prefix + a
    // suffix (0xae78f0). Colours are worked out from cLightConfig (sg/cLightingManager.h) and the sample's vectors.
    // 0x2ccec0 (ret 0x14): by config->mType, CreateBounceLight (0), CreateMinSideLight (1) or CreateAmbientLight with
    // mType - 2 deltas (2..8); any other type makes nothing.
    __declspec(noinline) void CreateConfigLight(cSkySample *sample, const cLightConfig *config, uint32_t tag,
                                                uint32_t bits, const cRZString *prefix);
    // 0x2ccf10 (ret 0x18): six colours, each side's (v_i + v4 * mUpToSides) * mSideScale and the vertical ones'
    // (v_i + side sum * mSideSum) * mVerticalScale, times mBrightness, less their average. The average becomes
    // an ambient light ("Ambient"), or with mLightMapWeight not 0 a cAmbientFromLightMapLight
    // ("AmbientFromLightMap"); then `deltas` directional lights ("Delta%d") take the largest colour left (of six,
    // of four with 2 deltas or fewer) each, from the game's direction for its vector (tilted for the sides), and
    // zero it. With mAdjust above 0 the first one also reshapes the pair the tables 0xdb5d70 / 0xdb5d88 name. With g_RefreshSwitch set the lights go to
    // sample.mLerp78 (cLerpLightSet::AddLight / AddDirectional) instead, when it is not null.
    __declspec(noinline) void CreateAmbientLight(cSkySample *sample, const cLightConfig *config, uint32_t tag,
                                                 uint32_t bits, const cRZString *prefix, int32_t deltas);
    // 0x2ce130 (ret 0x14): five directional lights: "Bounce" down the game's up normal, "PosX" / "NegX" along +-
    // the vector 0xffd900 and "PosY" / "NegY" along +- 0xffd90c (each less up * mTilt). Coloured from the sample
    // (the vertical and the four sides, by mBrightness with mVerticalScale or mSideScale, SetFactor mFactor) or,
    // with mFixedBounce set, the game's fixed colours at intensity 1. g_RefreshSwitch is not read.
    __declspec(noinline) void CreateBounceLight(cSkySample *sample, const cLightConfig *config, uint32_t tag,
                                                uint32_t bits, const cRZString *prefix);
    // 0x2ceee0 (ret 0x14): a "Bounce" light down the up normal of vector 4's colour, then two lights ("MaxSide%d",
    // 0 and 1) taking the largest colour left each (as CreateAmbientLight), then a "MinSide" light between the two
    // smallest vectors left when they are not a pair (0/1, 2/3, 4/5) and either is above 0: their directions and
    // colours weighed by their sizes. g_RefreshSwitch is not read.
    __declspec(noinline) void CreateMinSideLight(cSkySample *sample, const cLightConfig *config, uint32_t tag,
                                                 uint32_t bits, const cRZString *prefix);

    // 0x2d8410: with mWorld set, the rooms mWorld's slot 81 lists (a std::set<short>; the sky samples grown to the
    // largest + 1 first) each gather their floor tiles (slot 76, one list kept across rooms) into their sample: each
    // quadrant a tile's kind byte has a bit for that the terrain's slot 43 gives an object for adds that object's colour
    // (the 0x20e006 service's slot 3 (1, object)), drawn towards the game's grey 0xffd994 by mField1D4 and times
    // mLitObject's slot 23 at the quadrant's centre / 4, to vector 4 (weight 4 + 1), and mField1CC times that light to
    // vector 5 (weight 5 + 1). Rooms below 0 or past the samples are skipped.
    __declspec(noinline) void SampleRoomTiles();
    // 0x2d8e00: with mWorld set, each wall of mWorld (slots 55, 56; once per segment, as LightWalls) lights the sample of
    // the room behind each side (0x7f2340; rooms 0 and 0xffff skipped; the samples grown to reach it): the colour the
    // 0x20e006 service's slot 3 (2, name) gives the wall's name (mWorld's slot 51, or 52 for the last slot-22 count of
    // walls), drawn towards 0xffd994 by mField1D4 and times mLitObject's slot 24 for the wall / 2, goes by the side's
    // unit normal's x to vector 0 (x above 0, weight 0 + x) or 1 (less, weight 1 less x), by its y to 2 or 3; the
    // sample's mWallCount is raised. A zero-length (or NaN) side only grows the samples.
    __declspec(noinline) void SampleWallPortals();
    // 0x2d8120 (__cdecl): a wall side {x0, y0, level0, x1, y1, level1} (levels int32) as a quad of four points: the two
    // ends at the lower of their ground heights (GroundHeightAt), then the two ends again that plus the whole number of
    // units (rounded half up, unless the difference is a whole number) from there up to the higher top (the roof's height
    // less 0.25 where a roof covers the end, else the ground height a level up).
    __declspec(noinline) static void __cdecl WallCorners(const float *wall, float *quad);

    // LG4e, the edge builders (lane 261109n, src/sg/cLightingManagerEdges.cpp).
    // 0x2d94a0 (__stdcall, ret 0x14): a curved pool's edge (class not identified: its tile at +4, its direction at
    // +0x10) as four wall-like segments {x0, y0, -1, x1, y1, -1} (the levels int32 -1), each one unit long: c and d the
    // two sides of the unit square at the tile that meet at its corner in that direction (direction 0: (x + 1, y + 1)),
    // a the side in line with c that ends at c's start, b the one in line with d that starts at d's end. Directions 0,
    // 2, 4 and 6; any other leaves the four untouched.
    __declspec(noinline) static void __stdcall CurvedPoolEdges(const void *edge, float *a, float *c, float *b, float *d);
    // 0x2da3b0: for each curved pool edge of the world DB (0x10cc45c; slot 38's pools, slot 11 the ids, slot 10 one
    // edge): the texel column at the inner side of a's rectangle (mRef24's slot 21) and the one at the inner side of
    // b's, then c's and d's rectangles filled row by row with the blend from a's texel to b's (c the first half, d the
    // second, (x - x0) / (max(2, 2 * width) - 1) of the way). The texture (slot 22) is fetched again only when the
    // index changes. Stops at the first edge with no texels in a or b.
    __declspec(noinline) void BlendLotEdges();
    // 0x2d71f0 (ret 4): every record's shade set to 1; then for each record of a sample (not 0), each other record of
    // the same sample and level (+0x20) that it and the other face (the dots with the line between their midpoints not
    // below -0.001, the other's negated), within mPortalReach and further than 0.1, takes
    // ((aligned - crossed) * dot(normals)^2 + crossed) * (1 - distance / reach) from the shade, which stops at 0.
    __declspec(noinline) void ShadePortals(std::vector<cPortalRecord> *records);
    // 0x2d6c10 (ret 0x20; lane 261109o): a portal record's two lights for sample `sample` (its low 16 bits). The direct
    // factors are the current set's modifiers 1 (record of sample 0) or 0, the sky factors 3 or 2, each `a` times the
    // record's shade; the colours start black with weights 1 and 0. With mPortalSky: for sample 0 and a record of
    // another sample, the record's inward normal (-nx, -ny, 0) gives the direct colour (the set's light 0 times its
    // intensity, weighted by its facing, the sky colour the set's ambient at weight 1, when the set has a skylight
    // (bit 7), both drawn towards that normal's EvaluateAmbientScaled colour by mPortalSaturation below 1; without
    // one the direct colour is that ambient), plus mLitObject's slot 24 colour for the segment, and the sky colour
    // half of it; otherwise the direct colour is half that slot 24 colour (and the sky colour too, at weight 1, with a
    // skylight). The record's colour is direct * weight + sky * weight (times its shade with mShadePortalColors); with
    // the record's mScaleDirect the direct weight is then scaled by mPortalDirectScale.
    __declspec(noinline) void PortalColors(int32_t sample, cPortalRecord *record, float *directFactors,
                                           float *directColor, float *directWeight, float *skyFactors,
                                           float *skyColor, float *skyWeight);
    // 0x2e0480 (ret 4): the sky samples' portal lights through their lerp sets (mLerp88, made and keyed by the sample
    // index at first): each set's entries marked inactive; per sample, its records shaded (ShadePortals) when
    // mPortalShadeAligned is above 0, and for each record PortalColors, a random strength factor (1 -/+
    // mPortalJitter), then, for a record of an existing sample, its node's light added as it is (AddExisting) and a
    // point light at mPosition facing the normal (AddPoint), each followed by the record's cPortalLight appended to
    // `lights` (colour and falloff scaled) unless its colour is within 32 ulps of black; then the sets' unused entries
    // faded out.
    __declspec(noinline) void BuildPortalLights(std::vector<cPortalLight> *lights);
    // 0x2e1c30: the same colours, set on the records' nodes directly: the direct node's light attributes (e9d24d51,
    // 29de3049, 49e37821) scaled and its diffusion extension's source set; the other node's standard light given the
    // sky colour times the direct node's original attributes, and its extension's source. With mDebugPortals or the
    // set's bit 11, each sample's portals take the colour of its index + 1's low three bits instead.
    __declspec(noinline) void ApplyPortalLights();

    // LG4d, the top builders (lane 261109p, src/sg/cLightingManagerBuilders.cpp). Nodes are the director's light
    // reference nodes (class 0x253d2018); g_RefreshSwitch (0xffde64) sends the lights to lerp sets instead.
    // 0x2cfd60: the current set's lights. Without g_RefreshSwitch: BuildLightSetNodes (0xb3f770) rebuilds mBuiltLights from
    // the set and gives each light's kind; each node is tagged 0x3a97d0 (0), its light flag 2 with mSunMode 2 (else
    // 0), given layer bits by kind (8: all; 9: mEnvironmentGroup; 4-6: bit 0 removed, the Sims' group; else every
    // group but mEnvironmentGroup and mShadeGroup) and added to the scene manager; a kind 0 light with mSunMode 1,
    // mShadeTiles and mSunHMLight is replaced by a cHeightMapShadowLight ("LM_sunHM") of the old light's standard
    // colour and intensity. With mSunMode 2 a cTerrainShadeLight node joins (mShadeGroup). With the switch:
    // mLerp228's BuildRenderGroupLights (kind 3 under the height-map conditions, else 2) and, with mSunMode 2, its
    // "LM_OutdoorShade" (kind 7, the game's grey).
    __declspec(noinline) void BuildSunHeightMapLights();
    // 0x2d0250: for each sky sample but 0 whose room value (slot 36) is 100: the three room configs (+0xdc, +0x12c,
    // +0x17c; groups mPortalGroups 1, 0, 2) through CreateConfigLight, tagged 0x3a97d0 (index) and named
    // "LM_Room%d"; then an environment light of the samples' average: a node of class 0x6a97042f ("...Env",
    // mEnvironmentGroup), or with g_RefreshSwitch the sample's mLerp78 (made and keyed when null; entries marked
    // inactive first, unused ones faded after) gets "...Environment" (kind 8).
    __declspec(noinline) void BuildRoomLights();
    // 0x2e1560 (ret 4): mLerp22C and mLerp230 marked inactive. Unless g_RefreshSwitch and `lerp`: an ambient node of
    // the current set's mVector19C (into mBuiltLights and the scene), unless the byte 0xffde34 is set. Else (byte
    // clear) the flat outdoor lights into mLerp22C and the indoor ones (times mIndoorScale) into mLerp230: ambient
    // ones for the Sims, the objects and the rest at 1 - mFlatSunFraction, and with that fraction above 0
    // directional ones down the set's light 0 (outdoor) or light 1 (indoor). Then both sets' unused entries faded.
    __declspec(noinline) void BuildFlatOutdoorLights(uint8_t lerp);

    // LG4g, the rooms' portals (lane 261109r, src/sg/cLightingManagerRoomPortals.cpp). A wall side as mWorld's slot 56
    // gives it: {x0, y0, level0, x1, y1, level1}, the levels int32.
    struct cWallSide {
        float v[6];
    };
    // 0x2d50f0: with mWorld set, for each wall (slots 55, 56) whose two sides' rooms (slot 79 of each side's
    // SegmentToQuadrants; the side drawn the other way for the second) both have the room value (slot 36) 100: the
    // wall-side object 0x7e37b0 gives for the wall (else, failing that or its slot 9 interface, for the reversed
    // side), asked by its slot 9 (0xfa1c39f7, 0xfa1c39ed) for the portal object, which AddRoomPortal gets with the
    // side it came from first. The object and interface are kept from one wall to the next until another replaces them.
    __declspec(noinline) void BuildRoomPortals();
    // 0x2d5430 (ret 0x34): each light-reference node under `portal` (mask bit 0xf) placed where its world transform
    // puts it: dropped (layer tag 0x3ffffffe, attribute e9d24d51 set to 0) unless mTerrain's slot 72 has the point, it
    // is not below the ground at mTerrain's lowest level (slot 88), the side is longer than 1e-3 and the levels above
    // and below it reach ground heights above 0 (the side moved along itself by the whole number of lengths its
    // projection falls outside it, the middle too); else a portal record for the sample of the room the node faces
    // (0x7f2340 on the side at that level, by its down vector against side's normal), appended to that sample (the
    // samples grown to reach it). Without g_RefreshSwitch the node gets a "LM_PortalSpill" half-space point light child
    // (mOther) at mPortalLightOffset off the middle along the normal, between the two ground heights; with it the
    // node's light attributes are read into the record and mLitObject's slot 11 fills its cPortalLight.
    __declspec(noinline) void AddRoomPortal(void *portal, cWallSide side, cWallSide other);

    // LG4h, the sun shadow texture and the rebuilds (lane 261110a, src/sg/cLightingManagerShadow.cpp and
    // src/sg/cLightingManagerRebuild.cpp).
    // 0x2d2a70: with the scene graph system's terrain (0x20defb's slot 40) and an ambient map for the current set: the
    // terrain's grid size (slot 7) into mGridWidth / mGridHeight (mTerrainWidth / Height one less, mTerrainScale 0.1);
    // mShadeImage and mAmbientImage made (class 0x1c4a276c, the next powers of two at or above the cells, when
    // mShadeImage is null) and filled with the game's colour 0xffd964; mSunHeights resized to the grid (cell 10, fill
    // -1000), its mGround the terrain's heights (slot 8), mValues that raised by each of mSamples over its rectangle
    // (widened by mSampleMargin, at mTerrainScale), and its shadow built towards the current set's sun. Each cell's
    // normal (slot 9), bent towards the sun by how far the shadow is above mGround (mShadeImage) or mValues
    // (mAmbientImage), picks the ambient map's texel, times the set's tint, as an opaque pixel. Then mShadeTexture's
    // texture is given mShadeImage (its cITextureImage's slot 10 when it already has it, else slot 9 and the cached
    // queried object's slot 24).
    void BuildSunShadowTexture();
    // 0x2d3a50: up to 100 phases of the rebuild mRebuildStep names, until 10 ms (two stopwatches of mode 4) have passed:
    // the lights removed and the sun lights built (with mSunMode 2 only those and the shadow texture), the portal
    // lights gathered into mPortalLights, the room and flat lights, the sun height map and lot edges; the last phase
    // (23) settles the scene flag and the refresh clock and sets mRebuildStep to 0. Each round first reads the budget
    // watch (Elapsed of 10 or more ends the call) and restarts the phase watch; step 0 returns at once, steps above 23
    // do nothing. When the hundredth round has run (or the budget runs out on it), mRebuildStep is set to 0; otherwise
    // the budget watch is read once more.
    void RebuildStep();
    // 0x2d4010 (ret 4): the same phases at once, with a local portal light vector; `lerp` (its low byte) with
    // g_RefreshSwitch set sends the lights to the lerp sets and keeps mRef24's slots 9 and 10 from running. Nothing
    // without a scene manager; with mSunMode 2 only the light removal, the sun lights and the shadow texture (mDirty
    // cleared, the scene flag left alone).
    void RebuildAll(uint32_t lerp);

    // LG3b part 1, the lifecycle's leaves (lane 261110o, src/sg/cLightingManagerLife.cpp; names tentative).
    // 0x2d3820 slot 7 (ret 4; the argument is not read): the world let go of. mLitObject's slot 6 first when the byte
    // 0xffde34 is set (not null-checked); mSunShadows and mDirty cleared, mSamples emptied, mWorld then mRef24 nulled
    // and released (slot 2) when set, mLightingPath emptied, mFileParser marked dirty (slot 8 (1)) when set. With a
    // scene manager: RemoveSampleLights, RemoveBuiltLights, the sky samples destroyed, the scene manager nulled and
    // released. Then mLerp228 nulled and released (its slot 1) when set, and mSunMode 0.
    void ReleaseWorld(uint32_t unused);
    // 0x2d1300 slot 8 (ret 4): the neighbourhood view's set-up. From mTerrain (re-read before each call): mTerrainScale
    // 1 / slot 87 (an x87 float), mGridWidth / mGridHeight slots 77 / 78 (x87, truncated through int64, low words),
    // mTerrainWidth / mTerrainHeight slots 83 / 84. mRef24 becomes the scene graph system's (0x20defb) slot 48 result
    // and mSceneManager `sceneManager` (each: the new one AddRef'd when not null, stored, the old released when not
    // null; nothing when equal); mSunMode 2 and mSunShadows 0 between the two. A new cLerpLightSet becomes mLerp228
    // (its slot 2 the AddRef, slot 1 the old one's Release), keyed SkySampleKey(0) with light flag 2; then
    // BuildSunHeightMapLights, mFlag81 cleared, BuildSunShadowTexture.
    void SetupNeighborhood(void *sceneManager);
    // 0x2d10b0 slot 6 (ret 4; lane 261110r, src/sg/cLightingManagerLife.cpp): the lot view's set-up. From mTerrain
    // (re-read before each call): mTerrainScale 1 / slot 87 (an x87 float), mTerrainWidth / mTerrainHeight slots 83 /
    // 84 and mGridWidth / mGridHeight those plus one (the width read back from the member). mWorld becomes slot 39's
    // result and mRef24 the scene graph system's (0x20defb) slot 48 result (each: the new one's slot 1 AddRef when not
    // null, stored, the old one's slot 2 Release when not null; nothing when equal); mLitObject's slot 5 when the byte
    // 0xffde34 is set; mSunMode 1, mSunShadows 0, mSunMapDirty 1; mSceneManager `sceneManager` (as the others). A new
    // cLerpLightSet becomes mLerp228 (its slot 2 the AddRef, slot 1 the old one's Release), keyed SkySampleKey(0) with
    // light flag 0; then BuildSunHeightMapLights, mDirty 1, mFlag81 0, mFlag83 1, SelectTimeOfDayLightSet; the terrain's
    // slot 99 fills a string and answers whether it did: then this object's slot 22 (FindLightingFile) is called with
    // it.
    void SetupLot(void *sceneManager);
    // 0x2d3910 slot 9 (ret 4): the lot let go of: as slot 7 without mLitObject, the path or the parser, with
    // mShadeImage and mAmbientImage released first (after the two flags) and RemoveBuiltLights alone.
    void ReleaseLot(uint32_t unused);
    // 0x2d0db0 slot 22 (ret 4): looks for the lighting file `name` beside each path of the app's option 0x6c9f7a1f
    // (cTSAppCommonImpl slot 5), last path first: the path cut after its last '/' (0xda52b4, RFind from the last
    // character; paths without one skipped), `name` appended; each that exists (cRZFile::PathExists) becomes
    // mLightingPath with mFileParser marked dirty (not null-checked), and the search goes on. None: mLightingPath
    // emptied and the parser marked dirty. True when one was found.
    bool FindLightingFile(const char *name);
    // 0x2d0730: the current light set applied. mLitObject's slot 7 (&set.mLitParams1D0) when mFlag44 is set and it is
    // not null; then mVector2C4, per component, f(shaded) / f(plain) where f(plain) is above the epsilon 0xdaaa28 (else
    // 1), with f(v) = 1 - g(0.5 - g(v) * 0.25), g(v) = |v| + v: plain the up normal's EvaluateAmbient colour, shaded its
    // EvaluateAmbientShaded colour for set.mShade198 * mShadeFactor7C while mSunFalloff is above the epsilon, else
    // the plain colour. mFlag82 set; then two messages posted (0xcbc20880, then 0x8a with data 1 = mCurrentSet is 1).
    void ApplyLightSet();
    // 0x2e3710 (ret 4; lane 261110p, src/sg/cLightingManagerSettings.cpp): the room lighting settings read from
    // `parser` (the property parser) into the members, by name in the original's order: 3 prelude floats, the "sim",
    // "object" and "wall" sections (parser slots 20 / 21) of 12 each (the room* names = cLightConfig mType .. mAdjust),
    // 24 more, then the setting lightRangeCutoff (default 0.25) copied, when found, to the framework's settings
    // object as lightRadianceRangeCutoff (a float Any). The readers (0xaf7190 float, 0xaf7210 int, 0xaf7110 bool) are
    // the game's; each is told whether any earlier read found a change.
    void ReadRoomSettings(void *parser);

    // LG3c rest (lane 261110r, src/sg/cLightingManagerLoad.cpp; names tentative).
    // 0x2d0ae0 (ret 4): the lighting scripts in the resources `keys` list, run through the file parser's parser (mFileParser's
    // slot 9, counted through its virtual base for the call; nothing happens when there is none). For each key whose type
    // is 0xadee8d84, with the resource manager (the cached service's slot 3, not counted): its slot 39 opens the resource
    // (key, &resource, 0); when that answers true the resource's slot 10 gives the size n, the buffer (a vector of bytes,
    // emptied after every key, so it is resized from nothing: n + 1 zeroed bytes) is read into by the resource's slot 8
    // (buffer, n); when that answers true the manager's slot 34 is asked (key, &name) with the name word zeroed first (the
    // answer is ignored), and when the word is set a cRZString (empty) is filled by its slot 11 and the parser's slot 14
    // (on the primary pointer) gets (buffer, n, the name's text); the name's object is released (slot 2). The resource's
    // slot 6 (close) runs whenever the open answered true, then it is released (slot 2) when set. The buffer is freed
    // (the sized delete) at the end.
    void ReadLightingResources(cLightingKeyList *keys);
    // 0x2df8a0 slot 5: the lighting files loaded. Every path of the app's option 0x6c9f7a1f (cTSAppCommonImpl slot 5),
    // listed first with the mask 0x20000 inverted and then with the mask itself (the second call appends to the same list),
    // that exists (cRZFile::PathExists) goes to mFileParser's slot 6 with 5, last path first. Then slot 7 runs the files;
    // the resource manager's slot 19 lists the keys of type 0xadee8d84, ReadLightingResources runs on them, and the key
    // vector's block, then the path list's nodes and head are freed through the allocator service.
    void LoadLightingFiles();

    // LG3e (lane 261110s, src/sg/cLightingManagerUpdate.cpp; names tentative).
    // 0x2df430 slot 10, the per-frame update. When mFileParser answers dirty (its slot 10): its files run again (slot 7);
    // when the lighting file found (mPathVTable's cRZString: its slot 7 length) exists, the base parser (slot 9) runs it
    // (its slot 12 with the text and 0; an exception there is caught and skips the message) and the message server gets
    // the message 0x4b3bc675 with the base parser's cIGZUnknown; the scripts in the resources run (the resource manager's
    // slot 19, ReadLightingResources); with a scene manager, own slot 25's object gets the scene (its slot 9); mDirty is
    // set. With mDirty: mRefreshWatch stopped, then slot 20 when mFlag81 is clear, else with mField87 an incremental
    // rebuild (mRebuildStep 1, mDirty cleared), else RebuildAll (1) and mFlag81 set. RebuildStep while mRebuildStep is
    // set. While mRefreshWatch runs: its elapsed time t at or past mField500 ends the fade (the lerp lights at 1, the
    // watch stopped, mLitObject's slot 21 under g_Switch88 and 0xffde34, mField500 0, the scene flags 0xc when
    // mSceneFlagWanted); before it (or NaN), the lerp lights at (t - mField504) / (mField500 - mField504) and mField504 =
    // t. Then mLitObject's slot 20 when mFlag84 and 0xffde34 are set, else the cached singleton's (0x20ddfb) slot 22's
    // slot 20's slot 6.
    void Update();

    // The message-target face (+4, vtable 0xdb5f4c; lane 261110s, src/sg/cLightingManagerMessages.cpp).
    struct MessageFace {
        const void *mVTable;
        // 0x2d44b0 slot 3 (ret 4): the message's type (slot 4) picks the work. The option messages (0x2bf18d56 the
        // fade byte 0xffde34; 0x2bf18d58 + i the option row 0xffde40 + 0xc * i, i below 10) need the message's data 2
        // (slot 13, else false) and its pointer (slot 15, else false), whose byte +4 becomes the option's value; then
        // OptionsChanged, except 0xffde58 (false when unchanged, else MaterialOptionsChanged) and 0xffdeac
        // (MaterialOptionsChanged); 0xffdea0 set first walks the scene (SetSceneStrengths), and mFlag4D becomes its
        // inverse. 0xfb96149: with mInitialised, own slot 19 with "neighborhoodnight" when mName is "neighbourhood",
        // else "neighbourhood". 0xfb96150: mSunShadows 0, mDirty 1. 0xfe3cf23: ToggleCamera with mInitialised. 0x8d and
        // 0xb23b7d66: the state service's (0x211da3) slot 36 object, when there is one, gets its slot 8; then (and for
        // 0xd23b7d7c) SelectTimeOfDayLightSet. 0x2df90c05: ReadResourcesOf. 0x4b3bc675: ReadRoomSettingsOf's answer.
        // 0xeb425393: WorldChanged with data word 1 (slot 9). True for these, false for any other type.
        bool DoMessage(void *message);
        cLightingManager *Owner() { return reinterpret_cast<cLightingManager *>(reinterpret_cast<uint8_t *>(this) - 4); }
    };

    uint32_t SkySampleCount() const { return static_cast<uint32_t>(mSkySamples.size()); }
    cSkySample &SkySample(uint32_t i) { return mSkySamples.data()[i]; }

private:
    // DoMessage's pieces (inline in the original; src/sg/cLightingManagerMessages.cpp).
    // The options' common end: mShadeTiles = 0xffde4c, mField87 = 0xffde7c; with a scene manager and the fade byte
    // 0xffde34 changed from `oldFade`: mFlag4D 0 and (mSunMode 1) mLitObject's slot 6 when it was set, else mFlag4D 1
    // and (mSunMode 1) its slot 5. Then mDirty 1, mFlag81 0, mFlag83 1.
    void OptionsChanged(uint8_t oldFade);
    // mDirty and mFlag83 set; the material parser (0xae8980), when there is one: two switches set (slot 0 (0, name,
    // "1"-like value 0xda4fac)) with 0xffde58, else cleared (slot 1 (0, name)); a third set to 0xda4fac or 0xda4af4 by
    // 0xffdeac; then the material manager's (0xae8900) slot 24 (0, 0).
    void MaterialOptionsChanged();
    // Every scene object (mSceneManager's scene, slot 44 with an nTSSG::cTSSGObjectsFilter) whose slot 25 answers
    // 0xfa1c39ed gets own slot 14 (object, 0, 1.0, 0).
    void SetSceneStrengths();
    // The singleton's (0x20ddfb) slot 23 view object, when its kind (slot 5) is 0xabea28c7: its slot 6 object's slot 19
    // gets whether its slot 20 answered 0; the singleton's slot 8 object gets its slot 5; then the message, its type
    // set to 0xfb96149, goes to the message server's slot 4 (message, 0).
    void ToggleCamera(cIGZMessage2Standard *message);
    // The message's object (slot 32), when there is one, asked for 0x656b8f0c: its slot 19 lists the keys a new
    // cGZPersistResourceKeyFilterByType (0xadee8d84) passes; ReadLightingResources reads them.
    void ReadResourcesOf(cIGZMessage2Standard *message);
    // The message's object (slot 32, not null-checked) asked for 0xab7cc174 (false when it answers no). With light
    // sets in its +0x1ac vector: those swapped into mLightSets, mAmbientMaps resized to match (a vector of counted
    // pointers), each set's map loaded by its key (0xb409f0) or dropped, mCurrentSet 0, SelectHoodLightSet (mName).
    // Then ReadRoomSettings on the object's base parser face (vbtable entry 2); with mFlag81 ApplyLightSet, mDirty and
    // mFlag83 set. True.
    bool ReadRoomSettingsOf(cIGZMessage2Standard *message);
    // Kind 2: nothing without mShadeTiles. Kind 2 with it: mDirty, mSunMapDirty and mFlag83 set. Other kinds: mDirty set; 1, 3 to 6
    // and 8 also set mSunMapDirty and mFlag83. Then
    // mField500 is raised to 1000 times mField508 (kind 6) or the current set's mRefreshDelay.
    void WorldChanged(uint32_t kind);

    // The rebuilds' shared pieces (inline in both originals; src/sg/cLightingManagerRebuild.cpp): the bounce colour to
    // mRef24's slot 8, the last step's mFlag84 / scene flag settling, and the refresh clock restart.
    void PassBounceColour();
    void SettleScene();
    void RestartRefresh();
    // mRef24's slot 22 (level, index) texture: its texels (slot 9), bottom row first (slot 27 the height, slot 28
    // the pitch in bytes), as the lighters' pixels and stride (in texels, negative).
    void LoadTexture(uint32_t level, uint32_t index, uint32_t *&pixels, int32_t &stride);
    // Terrain slots 53 or 55 (tile, kind, 1).
    bool TileShown(const int32_t *tile);
    // One wall side (slots 40 and 42): skipped when its room (0x7f2340) is not 0 unless kind is 3 (then only the
    // rows below 3 * width are lit), or when mRef24's slot 21 gives no rectangle. LightWallShaded when mShadeTiles and
    // mShadeWalls are set and the side faces the sun; else its normal's EvaluateAmbientScaled colour times scale.
    void LightWallSide(float *wall, int32_t kind, const float *sun, float scale, int32_t &texture, uint32_t *&pixels,
                       int32_t &stride);

public:
    const void *mVTable;         // 0x000 0xdb5e90
    const void *mVTable4;         // 0x004 0xdb5f4c (cIGZMessageTarget2)
    uint32_t mRefCount;           // 0x008
    std::set<uint32_t> mMessageTypes; // 0x00c the nine message types DoMessage answers (the constructor's inserts)
    uint8_t mInitialised;         // 0x014 SelectTimeOfDayLightSet does nothing while it is clear
    uint8_t mPad15[3];            // 0x015 never stored
    void *mLitObject;             // 0x018 slot 16 lists its lightmap tiles (slot 41), 17 its walls (slot 42), 18 and
                                  // 19 its tiles and walls (slot 44); slot 22 takes a sky-lit sample's colour (45)
    void *mTerrain;               // 0x01c slot 62 (&{i, j, 0}) gives a vertex's height
    void *mWorld;                 // 0x020 the world (class not identified): slot 55 counts its walls, 56 (&wall, i)
                                  // gives one, 50 (&wall) its kind; 76 (room, &tiles, 0) lists a room's floor tiles,
                                  // 79 (tile, 0, 1) gives a tile's room
    void *mRef24;                 // 0x024 the lightmaps: slot 34 asks its slot 16 for a colour; 19 / 21 give a tile's /
                                  // wall's texel rectangle and texture, 22 (level, index) a texture, 23 (level) a scale
    void *mSceneManager;          // 0x028 nTSSG::cSceneManager: slot 9 removes a light node, slot 17 gives the scene
    uint8_t *mRef2C;              // 0x02c slot 25 reads its +0x404
    nGZCommandParser::cIFileParser *mFileParser; // 0x030 the lighting script's parser: slots 7 and 22 mark it dirty
    uint32_t mShadeTexture;      // 0x034 a cTextureHandle's id: BuildSunShadowTexture gives its texture mShadeImage
    void *mShadeImage;            // 0x038 a cImageData (class 0x1c4a276c): the ambient map's colour per terrain cell,
                                  // looked up by the normal bent towards the sun where the shadow is above the ground
    const void *mAmbientImage;    // 0x03c a cImageData likewise, the shadow measured from mSunHeights.mValues (the
                                  // ground raised by mSamples); slot 33 reads its slot 38 texels
    uint32_t mSunMode;            // 0x040 1: the sun shadowed by the height map (with mShadeTiles and mSunHMLight),
                                  // 2: the terrain shade light (0x2cfd60)
    uint8_t mFlag44;              // 0x044 slot 18
    uint8_t mPortalSky;           // 0x045 the portal builders add the sky (skylight or ambient) and the lit object's
                                  // slot 24 colour; else the portals take no colour but the debug one
    uint8_t mFixedBounce;         // 0x046 CreateBounceLight gives its lights the game's fixed colours at intensity 1
    uint8_t mDebugPortals;        // 0x047 0x2e1c30 colours each sample's portals by its index (also the set's bit 11)
    uint32_t mSkyMode;           // 0x048 SetSkyMode's kind (0 morning, 1 day, 2 evening, 3 night)
    uint8_t mUseDefaultName;      // 0x04c 1 at construction; slot 18 sets it
    uint8_t mFlag4D;              // 0x04d with the current set's flag bit 12, slots 14 and 15 keep the strength
    uint8_t mPad4E[2];            // 0x04e never stored
    float mField50;               // 0x050 1 at construction; slot 13 scales the objects' lights by it
    float mPortalDirectScale;     // 0x054 scales the direct weight of records with mScaleDirect (0x2d6c10)
    float mFlatSunFraction;       // 0x058 the flat lights' (0x2e1560) directional share; the rest is ambient
    uint8_t mShadeTiles;          // 0x05c slots 39 and 41 light tiles sun-shaded when set
    uint8_t mShadeWalls;          // 0x05d with mShadeTiles, walls facing the sun light shadowed (LightWallShaded)
    uint8_t mSunHMLight;          // 0x05e with mSunMode 1 and mShadeTiles, the sun is a cHeightMapShadowLight
    uint8_t mPad5F;               // 0x05f never stored
    float mSunLightValue;         // 0x060 that light's value (BuildRenderGroupLights' a7)
    uint32_t mField64;            // 0x064 passed to BuildRenderGroupLights as a fifth argument it does not read

    uint8_t mDrawSunHeights;      // 0x068 slot 39 then draws mSunHeights.mValues over lightmap 0
    uint8_t mSunShadows;          // 0x069 slot 32 reads the sun height map only when set
    uint8_t mSunMapDirty;         // 0x06a 0x2e4300 rebuilds mSunHeights' grids when set, and clears it
    uint8_t mWallDrivenPortals;   // 0x06b the setting wallDrivenPortals (a bool)
    uint8_t mMorningEvening;      // 0x06c the setting morningEvening (a bool)
    uint8_t mPad6D[3];            // 0x06d never stored
    float mObjectLightJitter;     // 0x070 the setting objectLightJitter
    float mPortalJitter;         // 0x074 the portal builders scale each portal's strength by a random 1 -/+ this
    float mPortalLightOffset;     // 0x078 how far a room portal's light sits off the portal's middle, along its normal
    float mShadeFactor7C;         // 0x07c ApplyLightSet's shade: the current set's mShade198 times this
    uint8_t mDirty;              // 0x080 slots 14, 15 and 19 set it
    uint8_t mFlag81;              // 0x081 slot 20 sets it
    uint8_t mFlag82;              // 0x082
    uint8_t mFlag83;              // 0x083 slot 19 sets it when a set is picked by name
    uint8_t mFlag84;              // 0x084 the rebuilds' last step: g_Switch88 (0xffde88) set and mFlag83 clear
    uint8_t mSceneFlagWanted;     // 0x085 the rebuilds give the scene (mSceneManager's slot 17) flags 0xc (0xaeeea0)
    uint8_t mSceneFlagSet;        // 0x086 and remember it; with mSceneFlagWanted clear they give it 0 again
    uint8_t mField87;             // 0x087 the setting incrementalUpdates (a bool)
    uint32_t mRebuildStep;        // 0x088 RebuildStep's next phase (0: none)
    std::vector<cPortalLight> mPortalLights; // 0x08c RebuildStep's portal lights (mLitObject's slots 8, 9, 13)
    std::vector<cLightSet> mLightSets; // 0x098 (one at construction)
    std::vector<const void *> mAmbientMaps; // 0x0a4 an image per light set, or null (slots 26, 27: width, height; 38: a texel)
    const void *mNameVTable;      // 0x0b0 a cRZString: vtable, text, reference count
    std::string mName;            // 0x0b4
    uint32_t mNameRefCount;       // 0x0cc
    uint32_t mCurrentSet;         // 0x0d0 an index into mLightSets; out of range: mDefaultSet
    // 0x0d4 a std::allocator tree of 4-byte values (0x14-byte nodes; the destructor's 0x15d790 is the library's
    // _Tree<Str *> instance); empty at construction and nothing ours fills or reads it.
    std::set<uint32_t> mSetD4;
    // The three room configs ReadRoomSettings reads (the script's sections "object", "sim", "wall"; the setting names
    // roomLightingType, roomOverallStrength, roomBounceStrength, roomBounceCrossover, roomSideStrength,
    // roomSideCrossover, roomSideSlope, roomSpecular, roomFromLightMapMix, roomFromLightMapStrength,
    // roomDirectionalBoost, roomCrossBias are cLightConfig's mType, mBrightness ... mAdjust in order).
    cLightSettingsBlock mObjectSettings; // 0x0dc "object"
    cLightSettingsBlock mSimSettings;    // 0x12c "sim"
    cLightSettingsBlock mWallSettings;   // 0x17c "wall"
    float mField1CC;             // 0x1cc scales the room-tile light into a sky sample's vector 5 (0x2d8410)
    float mPortalScale;           // 0x1d0 the portal records' colour scale (0x2d88b0)
    float mField1D4;              // 0x1d4 how far the sky samplers' colours move from the grey 0xffd994 (1: all)
    uint8_t mThresholdLightsByArea; // 0x1d8 the setting thresholdLightsByArea (a bool)
    uint8_t mPad1D9[3];           // 0x1d9 never stored
    float mPortalReach;          // 0x1dc ShadePortals: portals further apart (or 0.1 and less) do not shade each other
    float mPortalShadeAligned;    // 0x1e0 ShadePortals' shade for parallel normals (0x2e0480 shades only when above 0)
    float mPortalShadeCrossed;    // 0x1e4 and for perpendicular ones
    uint8_t mShadePortalColors;   // 0x1e8 the portal builders scale each record's colour by its shade
    uint8_t mPad1E9[3];           // 0x1e9 never stored
    float mAmbientScale1EC;       // 0x1ec
    float mAmbientScale1F0;       // 0x1f0 slots 16 and 17
    std::vector<cSkySample> mSkySamples; // 0x1f4 one per room (slot 35 reads the six vectors)
    float mTerrainScale;          // 0x200 terrain vertices per world unit
    uint32_t mGridWidth;          // 0x204 the terrain's vertex columns (its slot 7), BuildSunShadowTexture's grid
    uint32_t mGridHeight;         // 0x208 and rows
    int32_t mTerrainWidth;        // 0x20c the largest vertex index TerrainNormal reads, x
    int32_t mTerrainHeight;       // 0x210 and y
    // 0x214 a tree of 2-byte values on the allocator service (16-byte nodes; its _Erase 0x2c9bd0 is the one
    // SampleRoomTiles' std::set<short> uses); empty at construction and nothing ours fills or reads it.
    std::set<int16_t, std::less<int16_t>, GZServiceAllocator<int16_t>> mSet214;
    std::vector<cBuiltRef> mBuiltLights; // 0x21c the lights the builders added to the scene manager
    cLerpLightSet *mLerp228;      // 0x228
    cLerpLightSet *mLerp22C;      // 0x22c
    cLerpLightSet *mLerp230;      // 0x230
    uint8_t mField234;            // 0x234 0 at construction; nothing ours reads it
    uint8_t mPad235[3];           // 0x235 never stored
    void *mRef238;                // 0x238 null at construction; the destructor releases it (slot 2) after mRef23C
    void *mRef23C;                // 0x23c likewise
    float mBlendScale240;         // 0x240 copied to mSunHeights.mBlendScale when it is rebuilt
    float mShadowBlendScale;      // 0x244 copied to mSunHeights.mBlendScale by BuildSunShadowTexture
    cSunHeightMap mSunHeights;    // 0x248
    float mSunFalloff;            // 0x2ac
    std::vector<cLightSample> mSamples; // 0x2b0 slot 21 appends
    float mSampleScale;           // 0x2bc BuildSunShadowTexture: a sample raises its cells by its weight times this
    float mSampleMargin;          // 0x2c0 and widens its rectangle by this (world units) each way
    cLightVector mVector2C4;      // 0x2c4 (1, 1, 1) at construction; slot 31
    cLightSet mDefaultSet;        // 0x2d0
    uint8_t mPad4E4[4];           // 0x4e4 never stored (the stopwatch's 8-byte alignment)
    cRZStopwatch mRefreshWatch;   // 0x4e8 restarted by the rebuilds' last step under g_RefreshSwitch
    float mField500;              // 0x500 slots 14 and 15 raise it to at least mField508 * 1000; the rebuilds, when it
                                  // is 0, to the current set's mRefreshDelay * 1000
    float mField504;              // 0x504 the fade's last elapsed time (slot 10); cleared with mRefreshWatch's restart
    float mField508;              // 0x508 0.5 at construction
    uint32_t mPortalGroups[3];    // 0x50c or'd: the render groups 0x2e0480's lerp lights go to
    uint32_t mOtherGroup;         // 0x518 the render groups lit by neither the Sims' nor the objects' lights
    uint32_t mShadeGroup;         // 0x51c the terrain shade light's ("LM_OutdoorShade")
    uint32_t mEnvironmentGroup;   // 0x520 the rooms' environment lights'
    const void *mPathVTable;      // 0x524 a cRZString: vtable, text, reference count
    std::string mLightingPath;    // 0x528 the lighting file slot 22 found (cleared by slot 7 and a failed search)
    uint32_t mPathRefCount;       // 0x540
    uint8_t mPad544[4];           // 0x544 never stored

private:
    // LightQuad, or LightTriangle when `half`.
    void LightTile(const void *normals, const int32_t *tile, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1,
                   uint32_t *pixels, int32_t stride, float scale, bool half);
    // The four shadowed lighters: the half tile when `half`, LightQuadSunShaded's alpha and colour when `shaded`.
    void LightTileShadowed(const void *normals, const int32_t *tile, uint32_t x0, uint32_t y0, uint32_t x1,
                           uint32_t y1, uint32_t *pixels, int32_t stride, float scale, bool half, bool shaded);
};
static_assert(offsetof(cLightingManager, mRef24) == 0x24);
static_assert(offsetof(cLightingManager, mUseDefaultName) == 0x4c);
static_assert(offsetof(cLightingManager, mFixedBounce) == 0x46);
static_assert(offsetof(cLightingManager, mLightSets) == 0x98);
static_assert(offsetof(cLightingManager, mTerrain) == 0x1c);
static_assert(offsetof(cLightingManager, mSunShadows) == 0x69);
static_assert(offsetof(cLightingManager, mAmbientMaps) == 0xa4);
static_assert(offsetof(cLightingManager, mName) == 0xb4);
static_assert(offsetof(cLightingManager, mAmbientScale1EC) == 0x1ec && offsetof(cLightingManager, mPortalReach) == 0x1dc &&
              offsetof(cLightingManager, mPortalShadeCrossed) == 0x1e4);
static_assert(offsetof(cLightingManager, mSkySamples) == 0x1f4 &&
              offsetof(cLightingManager, mField1CC) == 0x1cc && offsetof(cLightingManager, mField1D4) == 0x1d4);
static_assert(offsetof(cLightingManager, mTerrainScale) == 0x200);
static_assert(offsetof(cLightingManager, mTerrainWidth) == 0x20c);
static_assert(offsetof(cLightingManager, mSunHeights) == 0x248);
static_assert(offsetof(cLightingManager, mSunFalloff) == 0x2ac);
static_assert(offsetof(cLightingManager, mCurrentSet) == 0xd0);
static_assert(offsetof(cLightingManager, mSamples) == 0x2b0);
static_assert(offsetof(cLightingManager, mDefaultSet) == 0x2d0);
static_assert(offsetof(cLightingManager, mAmbientImage) == 0x3c);
static_assert(offsetof(cLightingManager, mFlag4D) == 0x4d);
static_assert(offsetof(cLightingManager, mDirty) == 0x80 && offsetof(cLightingManager, mFlag83) == 0x83);
static_assert(offsetof(cLightingManager, mField500) == 0x500 && offsetof(cLightingManager, mField508) == 0x508);
static_assert(offsetof(cLightingManager, mSunMapDirty) == 0x6a && offsetof(cLightingManager, mPortalScale) == 0x1d0);
static_assert(offsetof(cLightingManager, mBuiltLights) == 0x21c && offsetof(cLightingManager, mLerp230) == 0x230 &&
              offsetof(cLightingManager, mBlendScale240) == 0x240);
static_assert(offsetof(cLightingManager, mSunMode) == 0x40 && offsetof(cLightingManager, mFlatSunFraction) == 0x58 &&
              offsetof(cLightingManager, mSunHMLight) == 0x5e && offsetof(cLightingManager, mField64) == 0x64);
static_assert(offsetof(cLightingManager, mOtherGroup) == 0x518 &&
              offsetof(cLightingManager, mEnvironmentGroup) == 0x520);
static_assert(offsetof(cLightSet, mIndoorScale) == 0x1b0 && offsetof(cLightSet, mPortalSaturation) == 0x1b4);
static_assert(offsetof(cLightingManager, mShadeTexture) == 0x34 && offsetof(cLightingManager, mShadeImage) == 0x38);
static_assert(offsetof(cLightingManager, mSceneFlagSet) == 0x86 && offsetof(cLightingManager, mRebuildStep) == 0x88 &&
              offsetof(cLightingManager, mPortalLights) == 0x8c);
static_assert(offsetof(cLightingManager, mObjectSettings) == 0xdc && offsetof(cLightingManager, mSimSettings) == 0x12c &&
              offsetof(cLightingManager, mWallSettings) == 0x17c && offsetof(cLightingManager, mThresholdLightsByArea) == 0x1d8);
static_assert(offsetof(cLightingManager, mMessageTypes) == 0xc && sizeof(std::set<uint32_t>) == 8 &&
              offsetof(cLightingManager, mField50) == 0x50 && offsetof(cLightingManager, mSetD4) == 0xd4 &&
              offsetof(cLightingManager, mSet214) == 0x214 && offsetof(cLightingManager, mField234) == 0x234 &&
              offsetof(cLightingManager, mRef238) == 0x238 && offsetof(cLightingManager, mRef23C) == 0x23c);
static_assert(offsetof(cLightingManager, mWallDrivenPortals) == 0x6b && offsetof(cLightingManager, mMorningEvening) == 0x6c &&
              offsetof(cLightingManager, mObjectLightJitter) == 0x70 && offsetof(cLightingManager, mPortalJitter) == 0x74 &&
              offsetof(cLightingManager, mField87) == 0x87);
static_assert(offsetof(cLightingManager, mGridWidth) == 0x204 && offsetof(cLightingManager, mShadowBlendScale) == 0x244 &&
              offsetof(cLightingManager, mSampleMargin) == 0x2c0);
static_assert(offsetof(cLightingManager, mRefreshWatch) == 0x4e8 && offsetof(cLightingManager, mField504) == 0x504);
static_assert(offsetof(cLightSet, mRefreshDelay) == 0x200);
static_assert(offsetof(cLightSet, mShade198) == 0x198 && offsetof(cLightSet, mLitParams1D0) == 0x1d0);
static_assert(offsetof(cLightingManager, mFileParser) == 0x30 && offsetof(cLightingManager, mShadeFactor7C) == 0x7c &&
              offsetof(cLightingManager, mPathVTable) == 0x524 && offsetof(cLightingManager, mLightingPath) == 0x528 &&
              offsetof(cLightingManager, mPathRefCount) == 0x540);
static_assert(sizeof(cLightingManager) == 0x548);

} // namespace nTSSG
