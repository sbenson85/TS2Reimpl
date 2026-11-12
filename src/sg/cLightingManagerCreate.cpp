// nTSSG::cLightingManager's construction and destruction (lane 261110t, scenegraph LG3f): Create 0x2d83a0, Construct
// 0x2ca6f0, Destruct 0x2cb800, and the small pieces they build: the room settings blocks (0xb3d240 / 0x2cbbb0) and the
// sun height map's destructor (0x2cb6c0). See sg/cLightingManager.h. The lighting states' defaults (0xb3cfb0) are in
// gz/LightingCommands.cpp.
//
// Library code the members' types bring (not ours, never hooked; the test checks their shapes): the std::set
// constructors, inserts and destructors (0x1c02fb, 0x1c0366, 0x15d790, 0x2c9bd0), the vectors' destructors and growth
// (0x177770, 0x2cb550, 0x2cb5d0, 0x2cb650, 0x2c9590) and std::string::assign (0x144402).
#include "sg/cLightingManager.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/LightingCommands.h"
#include "sg/cTextureManager.h"
#include "simantics/cTSBehaviorTree.h" // RefPtrVector

#include <cstring>
#include <new>

namespace nTSSG {

namespace {

constexpr uint32_t kDefaultSetName = 0xdb6194; // the name of the light set the constructor appends (4 characters)

// The manager's in-place strings (+0xb0, +0x524): a cRZString's three parts as members.
cRZString *AsRZString(const void *&vtableWord) { return reinterpret_cast<cRZString *>(&vtableWord); }

// The light sets are lighting states (gz/LightingCommands.h): the same 0x214 bytes, the name a cRZString.
std::vector<LightingState> &AsStates(std::vector<cLightSet> &sets)
{
    static_assert(sizeof(LightingState) == sizeof(cLightSet));
    return reinterpret_cast<std::vector<LightingState> &>(sets);
}

template <class T> RefPtrVector &AsRefs(std::vector<T> &v)
{
    static_assert(sizeof(T) == 4 && sizeof(std::vector<T>) == sizeof(RefPtrVector));
    return reinterpret_cast<RefPtrVector &>(v);
}

float Bits(uint32_t bits)
{
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// A counted pointer the destructor lets go of: slot `kSlot` when it is not null (the member is left as it is).
template <int kSlot, class T> void LetGo(T *object)
{
    if (object) RawSlot<kSlot, void>(object); // receiver: the member's object (its release slot)
}

} // namespace

// ---- the room settings blocks ----------------------------------------------------------------------------------------

cLightSettingsBlock *cLightSettingsBlock::Construct()
{
    mConfig.mType = 4;
    mConfig.mBrightness = 1.0f;
    mConfig.mVerticalScale = 3.0f;
    mConfig.mSideSum = 0.2f;
    mConfig.mSideScale = 3.0f;
    mConfig.mUpToSides = 0.2f;
    mConfig.mTilt = 0.5f;
    mConfig.mFactor = 0.0f;
    mConfig.mLightMapWeight = 0.0f;
    mConfig.mLightMapScale = 1.0f;
    mConfig.mDeltaIntensity = 1.0f;
    mConfig.mAdjust = 0.0f;
    mText.Construct();
    return this;
}

void cLightSettingsBlock::Destruct() { mText.Destruct(); }

// ---- the sun height map ----------------------------------------------------------------------------------------------

void cSunHeightMap::Destruct() { this->~cSunHeightMap(); }

// ---- the manager -----------------------------------------------------------------------------------------------------

cLightingManager *__cdecl cLightingManager::Create()
{
    void *const memory = ::operator new(sizeof(cLightingManager));
    try {
        return static_cast<cLightingManager *>(memory)->Construct();
    } catch (...) {
        ::operator delete(memory, sizeof(cLightingManager));
        throw;
    }
}

cLightingManager *cLightingManager::Construct()
{
    mVTable = &ts2_rva_db5e90;
    mVTable4 = &ts2_rva_db5f4c;
    mRefCount = 0;
    new (&mMessageTypes) std::set<uint32_t>();
    mInitialised = 0;
    mLitObject = nullptr;
    mTerrain = nullptr;
    mWorld = nullptr;
    mRef24 = nullptr;
    mSceneManager = nullptr;
    mRef2C = nullptr;
    mFileParser = nullptr;
    mShadeTexture = 0xffffffff; // a cTextureHandle without a texture
    mShadeImage = nullptr;
    mAmbientImage = nullptr;
    mSunMode = 0;
    mFlag44 = 0;
    mPortalSky = 1;
    mFixedBounce = 0;
    mDebugPortals = 0;
    mSkyMode = 3;
    mUseDefaultName = 1;
    mFlag4D = 1;
    mField50 = 1.0f;
    mPortalDirectScale = 0.0f;
    mFlatSunFraction = 0.0f;
    mShadeTiles = 0;
    mShadeWalls = 1;
    mSunHMLight = 1;
    mSunLightValue = Bits(0x3df5c28f); // 0.12
    mField64 = 0x3f666666;             // 0.9 as a float
    mDrawSunHeights = 0;
    mSunShadows = 0;
    mSunMapDirty = 1;
    mWallDrivenPortals = 0;
    mMorningEvening = 1;
    mObjectLightJitter = 0.0f;
    mPortalJitter = 0.0f;
    mPortalLightOffset = Bits(0x3e99999a); // 0.3
    mShadeFactor7C = 1.0f;
    mDirty = 0;
    mFlag81 = 0;
    mFlag82 = 0;
    mFlag83 = 1;
    mFlag84 = 0;
    mSceneFlagWanted = 0;
    mSceneFlagSet = 0;
    mField87 = 0;
    mRebuildStep = 0;
    new (&mPortalLights) std::vector<cPortalLight>();
    new (&mLightSets) std::vector<cLightSet>();
    new (&mAmbientMaps) std::vector<const void *>();
    AsRZString(mNameVTable)->Construct();
    mCurrentSet = 0xffffffff;
    new (&mSetD4) std::set<uint32_t>();
    mObjectSettings.Construct();
    mSimSettings.Construct();
    mWallSettings.Construct();
    mField1CC = 0.25f;
    mPortalScale = 1.0f;
    mField1D4 = 1.0f;
    mThresholdLightsByArea = 0;
    mPortalReach = 1.0f;
    mPortalShadeAligned = 0.0f;
    mPortalShadeCrossed = 0.0f;
    mShadePortalColors = 1;
    mAmbientScale1EC = 1.0f;
    mAmbientScale1F0 = 1.0f;
    new (&mSkySamples) std::vector<cSkySample>();
    mTerrainScale = 1.0f;
    mGridWidth = 0x41;
    mGridHeight = 0x41;
    mTerrainWidth = 0x40;
    mTerrainHeight = 0x40;
    new (&mSet214) decltype(mSet214)();
    new (&mBuiltLights) std::vector<cBuiltRef>();
    mLerp228 = nullptr;
    mLerp22C = nullptr;
    mLerp230 = nullptr;
    mField234 = 0;
    mRef238 = nullptr;
    mRef23C = nullptr;
    mBlendScale240 = 1.0f;
    mShadowBlendScale = 1.0f;
    mSunHeights.Construct();
    mSunFalloff = 1.0f;
    new (&mSamples) std::vector<cLightSample>();
    mSampleScale = 1.0f;
    mSampleMargin = 1.0f;
    mVector2C4 = cLightVector{1.0f, 1.0f, 1.0f};
    reinterpret_cast<LightingState *>(&mDefaultSet)->Construct();
    mRefreshWatch.Construct(4);
    mField500 = 0.0f;
    mField504 = 0.0f;
    mField508 = 0.5f;
    mPortalGroups[0] = 0;
    mPortalGroups[1] = 0;
    mPortalGroups[2] = 0;
    mShadeGroup = 0;
    mEnvironmentGroup = 0;
    AsRZString(mPathVTable)->Construct();

    // The message types DoMessage answers.
    static constexpr uint32_t kMessageTypes[] = {0x4b3bc675, 0xeb425393, 0x8d,       0x2df90c05, 0xfb96149,
                                                 0xfb96150,  0xfe3cf23,  0xb23b7d66, 0xd23b7d7c};
    for (uint32_t type : kMessageTypes) mMessageTypes.insert(type);

    // One light set, the defaults under the game's name; the current set is -1 while it is appended.
    alignas(4) uint8_t raw[sizeof(LightingState)];
    LightingState *const defaults = reinterpret_cast<LightingState *>(raw)->Construct();
    defaults->mName.mString.assign(GameAddress<const char>(kDefaultSetName), 4);
    AsStates(mLightSets).push_back(*defaults);
    mCurrentSet = 0;
    defaults->~LightingState();
    return this;
}

void cLightingManager::Destruct()
{
    mVTable = &ts2_rva_db5e90;
    mVTable4 = &ts2_rva_db5f4c;
    AsRZString(mPathVTable)->Destruct();
    // mRefreshWatch: its destructor (0x140fe0) does nothing.
    reinterpret_cast<LightingState *>(&mDefaultSet)->~LightingState();
    mSamples.~vector();
    mSunHeights.Destruct();
    LetGo<2>(mRef23C);
    LetGo<2>(mRef238);
    LetGo<1>(mLerp230); // cRZRefCount's release
    LetGo<1>(mLerp22C);
    LetGo<1>(mLerp228);
    AsRefs(mBuiltLights).Destroy();
    mSet214.~set();
    mSkySamples.~vector();
    mWallSettings.Destruct();
    mSimSettings.Destruct();
    mObjectSettings.Destruct();
    mSetD4.~set();
    AsRZString(mNameVTable)->Destruct();
    AsRefs(mAmbientMaps).Destroy();
    AsStates(mLightSets).~vector();
    mPortalLights.~vector();
    LetGo<2>(mAmbientImage);
    LetGo<2>(mShadeImage);
    nGZSceneGraph::cTextureHandle::At(&mShadeTexture)->Destroy();
    LetGo<2>(mFileParser);
    if (mRef2C) {
        // Released through its virtual base: the vbptr at +8, the base's offset in its second entry.
        uint8_t *const vbptr = mRef2C + 8;
        const int32_t offset = (*reinterpret_cast<int32_t *const *>(vbptr))[1];
        RawSlot<2, void>(vbptr + offset); // receiver: cIGZUnknown (the object's virtual base; Release)
    }
    LetGo<2>(mSceneManager);
    LetGo<2>(mRef24);
    LetGo<2>(mWorld);
    LetGo<2>(mTerrain);
    LetGo<2>(mLitObject);
    mMessageTypes.~set();
}

} // namespace nTSSG
