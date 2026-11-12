// nTSSG::cLightingManager, the lifecycle's leaves (lane 261110o, scenegraph LG3b part 1): slots 7, 8, 9 and 22 and
// ApplyLightSet 0x2d0730. See sg/cLightingManager.h.
//
// Still the game's, called through OriginalFunction: the scene graph system's getter 0x20defb.
#include "sg/cLightingManager.h"
#include "sg/cLightingManagerDetail.h"

#include "app/AppDebugger.h"
#include "app/AppInit.h"
#include "game/original.h"
#include "gz/cFileParser.h"
#include "gz/cGZAny.h"
#include "gz/cGZMessageServer2.h"
#include "gz/cIGZMessage2Standard.h"
#include "misc/X87.h"
#include "rz/RZMemory.h" // 0x1c03b7, the messages' allocation, is a tail call to RZMemory::Malloc 0x1be3b0
#include "rz/RZMessageServer.h"
#include "rz/cGZAllocatorService.h"
#include "rz/cRZFile.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZString.h"
#include "rz/cRZStringSub.h"
#include "sg/cLerpLightSet.h"

#include <cmath>
#include <cstring>
#include <new>
#include <string>

namespace nTSSG {

namespace {

constexpr uint32_t kFadeSwitch = 0xffde34;      // slot 7 calls mLitObject's slot 6 when it is set
constexpr uint32_t kLightingPaths = 0x6c9f7a1f; // the app option slot 22 lists the lighting directories of
constexpr uint32_t kPathSeparator = 0xda52b4;   // "/" (slot 22 cuts each path after its last one)
constexpr uint32_t kEpsilon = 0xdaaa28;         // a double: ApplyLightSet's "above zero"
constexpr uint32_t kLightSetChanged = 0xcbc20880; // ApplyLightSet's first message
constexpr uint32_t kLightSetIndoor = 0x8a;        // and its second (data 1: the current set is set 1)

// A node of the list of strings the app's option lists fill (0x20 bytes; AllocateListNodes' nodes, the texts' buffers
// the allocator service's: DestroyStringListNodes frees them through 0x152b80).
struct PathNode {
    PathNode *mNext;
    PathNode *mPrev;
    GZString mText;
};
static_assert(sizeof(PathNode) == 0x20, "PathNode: a std::list<std::string> node");

// A reference member set to `value`: the new one counted (its slot `kAddRef`) when not null, stored, the old one
// released (`kRelease`) when not null; nothing when they are the same.
template <int kAddRef, int kRelease, class T> void Assign(T *&member, T *value)
{
    T *const old = member;
    if (value == old) return;
    if (value) RawSlot<kAddRef, void>(value);
    member = value;
    if (old) RawSlot<kRelease, void>(old);
}

// A member nulled, then released through slot `kRelease`, when it is set.
template <int kRelease, class T> void Drop(T *&member)
{
    T *const old = member;
    if (!old) return;
    member = nullptr;
    RawSlot<kRelease, void>(old);
}

// The x87's fistp qword (under a truncating control word) of `v`, the low word kept: the integer part, or the
// indefinite 0x8000000000000000 (low word 0) for NaNs and values out of the int64 range.
uint32_t TruncatedLow(float v)
{
    if (!(std::fabs(v) < 9223372036854775808.0f)) return 0;
    return static_cast<uint32_t>(static_cast<int64_t>(v));
}

uint32_t Bits(float v)
{
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    return bits;
}
float FromBits(uint32_t bits)
{
    float v;
    std::memcpy(&v, &bits, sizeof v);
    return v;
}

// SSE's mulss / addss of two floats where the first operand's NaN wins (quieted): for one NaN, either way gives it.
float MulFirst(float a, float b) { return std::isnan(a) ? FromBits(Bits(a) | 0x400000u) : a * b; }

// g(v) = |v| + v as the original adds it (andps then addss onto |v|): a NaN gives |v|, quieted.
float AbsPlus(float v) { return std::isnan(v) ? FromBits((Bits(v) & 0x7fffffffu) | 0x400000u) : std::fabs(v) + v; }

// ApplyLightSet's f(v) = 1 - g(0.5 - g(v) * 0.25) (the constants 0xda03e0 0.25, 0xd9c694 0.5, 0xd9c698 1.0).
float Shape(float v) { return 1.0f - AbsPlus(0.5f - AbsPlus(v) * 0.25f); }

double Epsilon() { return *GameAddress<const double>(kEpsilon); }

void *SceneGraphSystem() { return OriginalFunction<void *(__cdecl *)(), 0x20defb>()(); }

// A new cRZMessage2Standard counted once (null when the allocation failed).
cIGZMessage2Standard *NewMessage()
{
    void *const memory = RZMemory::Malloc(cRZMessage2Standard::kSize);
    auto *const message = reinterpret_cast<cIGZMessage2Standard *>(
        memory ? static_cast<cRZMessage2Standard *>(memory)->Construct() : nullptr);
    if (message) message->AddRef(); // receiver: cRZMessage2Standard
    return message;
}

void Post(cIGZMessage2Standard *message)
{
    auto *const server = static_cast<cIGZMessageServer2 *>(GetCachedMessageServer());
    server->MessagePost(message, false); // receiver: cIGZMessageServer2
}

} // namespace

void cLightingManager::ReleaseWorld(uint32_t /*unused*/)
{
    if (Switch(kFadeSwitch)) Call<6, void>(mLitObject); // receiver: the lit object (class not identified)
    mSunShadows = 0;
    mDirty = 0;
    mSamples.clear();
    Drop<2>(mWorld);  // receiver: the world (class not identified; Release)
    Drop<2>(mRef24);  // receiver: the lightmaps (class not identified; Release)
    mLightingPath.clear();
    if (mFileParser) mFileParser->SetDirty(true); // receiver: nGZCommandParser::cIFileParser
    if (mSceneManager) {
        RemoveSampleLights();
        RemoveBuiltLights();
        mSkySamples.clear();
        Drop<2>(mSceneManager); // receiver: nTSSG::cSceneManager (Release)
    }
    Drop<1>(mLerp228); // receiver: nTSSG::cLerpLightSet (cRZRefCount Release)
    mSunMode = 0;
}

void cLightingManager::ReleaseLot(uint32_t /*unused*/)
{
    mSunShadows = 0;
    mDirty = 0;
    Drop<2>(mShadeImage);   // receiver: cImageData (class 0x1c4a276c; Release)
    Drop<2>(mAmbientImage); // receiver: cImageData (Release)
    mSamples.clear();
    Drop<2>(mWorld); // receiver: the world (Release)
    Drop<2>(mRef24); // receiver: the lightmaps (Release)
    if (mSceneManager) {
        RemoveBuiltLights();
        mSkySamples.clear();
        Drop<2>(mSceneManager); // receiver: nTSSG::cSceneManager (Release)
    }
    Drop<1>(mLerp228); // receiver: nTSSG::cLerpLightSet (cRZRefCount Release)
    mSunMode = 0;
}

void cLightingManager::SetupNeighborhood(void *sceneManager)
{
    // receiver (all five): the terrain (class not identified); slots 87, 77 and 78 answer on the x87 stack.
    mTerrainScale = static_cast<float>(x87::Rounded(1.0 / static_cast<double>(Call<87, float>(mTerrain))));
    mGridWidth = TruncatedLow(Call<77, float>(mTerrain));
    mGridHeight = TruncatedLow(Call<78, float>(mTerrain));
    mTerrainWidth = Call<83, int32_t>(mTerrain);
    mTerrainHeight = Call<84, int32_t>(mTerrain);
    // receiver: the scene graph system (0x20defb; slot 48 its lightmaps); the lightmaps AddRef 1, Release 2.
    Assign<1, 2>(mRef24, Call<48, void *>(SceneGraphSystem()));
    mSunMode = 2;
    mSunShadows = 0;
    Assign<1, 2>(mSceneManager, sceneManager); // receiver: nTSSG::cSceneManager
    void *const memory = ::operator new(0x1c);
    cLerpLightSet *const made = memory ? static_cast<cLerpLightSet *>(memory)->Construct() : nullptr;
    Assign<2, 1>(mLerp228, made); // receiver: nTSSG::cLerpLightSet (cRZRefCount: slot 2 AddRef, slot 1 Release)
    mLerp228->SetKeys(SampleKey(0), 2);
    BuildSunHeightMapLights();
    mFlag81 = 0;
    BuildSunShadowTexture();
}

void cLightingManager::SetupLot(void *sceneManager)
{
    // receiver (all the terrain's): the terrain (class not identified), re-read before each call; slot 87 answers on
    // the x87 stack.
    mTerrainScale = static_cast<float>(x87::Rounded(1.0 / static_cast<double>(Call<87, float>(mTerrain))));
    mTerrainWidth = Call<83, int32_t>(mTerrain);
    mTerrainHeight = Call<84, int32_t>(mTerrain);
    mGridWidth = static_cast<uint32_t>(mTerrainWidth) + 1;
    mGridHeight = static_cast<uint32_t>(mTerrainHeight) + 1;
    Assign<1, 2>(mWorld, Call<39, void *>(mTerrain)); // the world: AddRef 1, Release 2
    // receiver: the scene graph system (0x20defb; slot 48 its lightmaps).
    Assign<1, 2>(mRef24, Call<48, void *>(SceneGraphSystem()));
    if (Switch(kFadeSwitch)) Call<5, void>(mLitObject); // receiver: the lit object (class not identified)
    mSunMode = 1;
    mSunShadows = 0;
    mSunMapDirty = 1;
    Assign<1, 2>(mSceneManager, sceneManager); // receiver: nTSSG::cSceneManager
    void *const memory = ::operator new(0x1c);
    cLerpLightSet *const made = memory ? static_cast<cLerpLightSet *>(memory)->Construct() : nullptr;
    Assign<2, 1>(mLerp228, made); // receiver: nTSSG::cLerpLightSet (cRZRefCount: slot 2 AddRef, slot 1 Release)
    mLerp228->SetKeys(SampleKey(0), 0);
    BuildSunHeightMapLights();
    mDirty = 1;
    mFlag81 = 0;
    mFlag83 = 1;
    SelectTimeOfDayLightSet();
    cRZString text;
    text.Construct();
    // receiver: the terrain; slot 99 fills the text and answers whether there is one
    if (Call<99, uint8_t>(mTerrain, text.AsInterface())) Call<22, uint32_t>(this, text.mString.c_str()); // slot 22: FindLightingFile
    text.Destruct();
}

bool cLightingManager::FindLightingFile(const char *name)
{
    bool found = false;
    appdebugger::ListHeader list{nullptr, 0};
    auto *const sentinel = static_cast<PathNode *>(appdebugger::AllocateListNodes(1, 0));
    sentinel->mNext = sentinel;
    sentinel->mPrev = sentinel;
    list.mHead = sentinel;
    appinit::CommonImplSlot5(kLightingPaths, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&list)), 1, 0,
                             0xffffffffu);
    if (sentinel->mNext != sentinel) {
        // From the last path to the first (the loop stops once the first node has been read).
        PathNode *cursor = sentinel;
        do {
            PathNode *const node = cursor->mPrev;
            cRZString path;
            path.Construct(node->mText.c_str());
            const int32_t cut =
                path.RFind(GameAddress<const char>(kPathSeparator), path.Strlen() - 1, true);
            if (cut != -1) {
                cRZString directory;
                Sub(path).Mid(&directory, 0, static_cast<uint32_t>(cut + 1));
                path.mString.assign(directory.mString.c_str(), directory.mString.size());
                directory.Destruct();
                path.mString.append(name, std::strlen(name));
                if (cRZFile::PathExists(*path.AsInterface())) {
                    mLightingPath.assign(path.mString.c_str(), path.mString.size());
                    mFileParser->SetDirty(true); // receiver: nGZCommandParser::cIFileParser
                    found = true;
                }
            }
            path.Destruct();
            cursor = node;
        } while (cursor != sentinel->mNext);
    }
    if (!found) {
        mLightingPath.clear();
        mFileParser->SetDirty(true); // receiver: nGZCommandParser::cIFileParser
    }
    appdebugger::DestroyStringListNodes(&list, list.mHead);
    FreeThroughAllocatorService(list.mHead, 1);
    return found;
}

void cLightingManager::ApplyLightSet()
{
    const cLightSet &set = CurrentSet();
    if (mFlag44 && mLitObject) {
        // receiver: the lit object (class not identified)
        Call<7, void>(mLitObject, static_cast<const void *>(set.mLitParams1D0));
    }
    const float shade = MulFirst(set.mShade198, mShadeFactor7C);
    float plain[3] = {0, 0, 0};
    float shaded[3] = {0, 0, 0};
    const float *const up = GameAddress<const float>(kUpNormal);
    EvaluateAmbient(up, plain);
    if (static_cast<double>(mSunFalloff) > Epsilon()) {
        EvaluateAmbientShaded(up, shade, shaded);
    } else {
        std::memcpy(shaded, plain, sizeof shaded);
    }
    mVector2C4 = {1.0f, 1.0f, 1.0f};
    float *const out = &mVector2C4.x;
    for (int c = 0; c < 3; ++c) {
        const float below = Shape(plain[c]);
        const float above = Shape(shaded[c]);
        if (static_cast<double>(below) > Epsilon()) out[c] = above / below;
    }
    mFlag82 = 1;

    // ORIGINAL-BUG: a failed allocation is used anyway (SetType, SetData1 and the post are not null-checked).
    cIGZMessage2Standard *const first = NewMessage();
    cIGZMessage2Standard *volatile firstUsed = first;
    firstUsed->SetType(kLightSetChanged); // receiver: cRZMessage2Standard
    Post(first);
    cIGZMessage2Standard *const second = NewMessage();
    cIGZMessage2Standard *volatile secondUsed = second;
    secondUsed->SetType(kLightSetIndoor);  // receiver: cRZMessage2Standard
    secondUsed->SetData1(mCurrentSet == 1); // receiver: cRZMessage2Standard
    Post(second);
    if (second) second->Release(); // receiver: cRZMessage2Standard
    if (first) first->Release();   // receiver: cRZMessage2Standard
}

} // namespace nTSSG
