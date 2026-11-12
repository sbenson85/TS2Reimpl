// nTSSG::cLightingManager slot 10, the per-frame update 0x2df430 (lane 261110s, scenegraph LG3e). See
// sg/cLightingManager.h.
//
// Still the game's, called through OriginalFunction: the stopwatch's running test 0x1c95cf (14 bytes: its start time
// is not zero). The scene flag setter 0xaeeea0 is ours (nGZSceneGraph::RebuildLightBounds).
#include "sg/cLightingManager.h"
#include "sg/cLightingManagerDetail.h"
#include "sg/SGUtilities.h"

#include "game/original.h"
#include "gz/cFileParser.h"
#include "gz/cIGZString.h"
#include "rz/RZMemory.h"
#include "rz/RZMessageServer.h"
#include "rz/ServiceGetters.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StopwatchMore.h"
#include "rz/cGZAllocatorService.h"
#include "rz/cRZFile.h"
#include "rz/cRZMessage2Standard.h"

#include <cstdint>

namespace nTSSG {

namespace {

constexpr uint32_t kScriptType = 0xadee8d84; // the resource type of the lighting scripts
constexpr uint32_t kSwitch88 = 0xffde88;     // g_Switch88
constexpr uint32_t kFadeSwitch = 0xffde34;   // the byte the lit object's fade calls wait on
constexpr uint32_t kSceneFlags = 0xc;        // what the scene gets with mSceneFlagWanted set

// The message's slots (cRZMessage2Standard, the game's vtable 0xda4064) and the message server's post.
constexpr int kMessageAddRef = 1;
constexpr int kMessageRelease = 2;
constexpr int kMessageSetId = 5;
constexpr int kMessageSetObject = 33; // a counted object pointer (mPtr24)
constexpr int kServerPost = 3;

// The resource manager (the cached service 0xb390c0's slot 3; class not identified).
void *ResourceManager() { return Call<3, void *>(CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>()); }

// The cRZStopwatch running test (0x1c95cf, still the game's).
bool IsRunning(cRZStopwatch *watch) { return OriginalFunction<bool(__thiscall *)(cRZStopwatch *), 0x1c95cf>()(watch); }

// 0xaeeea0 (ours): the scene's light bound shapes rebuilt for the flags.
void SetSceneFlags(void *scene, uint32_t flags)
{
    nGZSceneGraph::RebuildLightBounds(static_cast<nGZSceneGraph::cICompositionTreeNode *>(scene), flags);
}

// The base parser's cIGZUnknown: its virtual base, through the vbptr at +4 (the offset in the table's second entry).
void *UnknownOf(nGZCommandParser::cIBaseParser *parser)
{
    uint8_t *const vbptr = reinterpret_cast<uint8_t *>(parser) + 4;
    return vbptr + (*reinterpret_cast<int32_t *const *>(vbptr))[1];
}

// The key list the resource manager fills, its block freed through the allocator service when it goes (on an exception
// too, as the original's vector).
struct HeldKeys {
    cLightingKeyList mKeys{nullptr, nullptr, nullptr};
    HeldKeys() = default;
    HeldKeys(const HeldKeys &) = delete;
    HeldKeys &operator=(const HeldKeys &) = delete;
    ~HeldKeys()
    {
        if (mKeys.mBegin) FreeThroughAllocatorService(mKeys.mBegin, 0);
    }
};

// The posted message's reference, released (its slot 2) when it goes (on an exception too).
struct HeldMessage {
    cRZMessage2Standard *mMessage;
    explicit HeldMessage(cRZMessage2Standard *message) : mMessage(message) {}
    HeldMessage(const HeldMessage &) = delete;
    HeldMessage &operator=(const HeldMessage &) = delete;
    ~HeldMessage()
    {
        if (mMessage) Call<kMessageRelease, uint32_t>(mMessage); // receiver: cRZMessage2Standard (Release)
    }
};

// The message server (when there is one) gets the message 0x4b3bc675 carrying the file parser's base parser.
void PostRanMessage(nGZCommandParser::cIFileParser *const *parserSlot)
{
    void *const server = GetCachedMessageServer();
    if (!server) return;
    auto *const block = static_cast<cRZMessage2Standard *>(RZMemory::Malloc(cRZMessage2Standard::kSize));
    cRZMessage2Standard *const message = block ? block->Construct() : nullptr;
    if (message) Call<kMessageAddRef, uint32_t>(message); // receiver: cRZMessage2Standard (AddRef)
    HeldMessage held(message);
    // ORIGINAL-BUG: a failed allocation is not checked here: the null message is called.
    Call<kMessageSetId, void>(message, nGZCommandParser::cFileParser::kRanMessage); // receiver: cRZMessage2Standard
    nGZCommandParser::cIBaseParser *const parser = (*parserSlot)->GetParser(); // receiver: nGZCommandParser::cIFileParser
    Call<kMessageSetObject, void *>(message, parser ? UnknownOf(parser) : nullptr); // receiver: cRZMessage2Standard
    Call<kServerPost, uint32_t>(server, message); // receiver: the message server (cIGZMessageServer2), post
}

} // namespace

void cLightingManager::Update()
{
    if (mFileParser && mFileParser->IsDirty()) { // receiver: nGZCommandParser::cIFileParser
        mFileParser->RunFiles();                 // receiver: nGZCommandParser::cIFileParser
        cIGZString *const path = reinterpret_cast<cIGZString *>(&mPathVTable);
        if (path->Strlen() && cRZFile::PathExists(*path)) { // receiver: cIGZString (a cRZString)
            bool ran = true;
            try {
                nGZCommandParser::cIBaseParser *const parser = mFileParser->GetParser(); // receiver: cIFileParser
                // receiver: nGZCommandParser::cIBaseParser, slot 12 runs a file (path, flags); a null parser is not
                // checked (as the original).
                Call<12, uint32_t>(parser, path->ToChar(), 0u);
            } catch (...) {
                ran = false;
            }
            if (ran) PostRanMessage(&mFileParser);
        }
        HeldKeys keys;
        Call<19, uint32_t>(ResourceManager(), &keys.mKeys, kScriptType); // receiver: the resource manager; lists keys
        ReadLightingResources(&keys.mKeys);
        if (void *const sceneManager = mSceneManager) {
            void *const target = reinterpret_cast<void *>(static_cast<uintptr_t>(Call<25, uint32_t>(this))); // own slot 25
            Call<9, uint32_t>(target, SceneOf(sceneManager)); // receiver: slot 25's object (not identified), slot 9
        }
        mDirty = 1;
    }
    if (mDirty) {
        stopwatch_more::Stop(&mRefreshWatch, nullptr);
        if (!mFlag81) {
            Call<20, void>(this); // own slot 20 (Refresh)
        } else if (mField87) {
            mRebuildStep = 1;
            mDirty = 0;
        } else {
            RebuildAll(1);
            mFlag81 = 1;
        }
    }
    if (mRebuildStep) RebuildStep();
    if (!IsRunning(&mRefreshWatch)) return;
    float elapsed;
    stopwatch_more::ElapsedAsFloat(&mRefreshWatch, nullptr, &elapsed);
    const float duration = mField500;
    if (elapsed >= duration) { // comiss: an unordered pair takes the other branch
        EvaluateLerpLights(1.0f);
        stopwatch_more::Stop(&mRefreshWatch, nullptr);
        if (Switch(kSwitch88) && Switch(kFadeSwitch)) Call<21, void>(mLitObject); // receiver: the lit object, slot 21
        mField500 = 0.0f;
        if (mSceneFlagWanted) {
            SetSceneFlags(SceneOf(mSceneManager), kSceneFlags); // receiver: nTSSG::cSceneManager, slot 17 (the scene)
            mSceneFlagSet = 1;
        }
    } else {
        const float last = mField504;
        EvaluateLerpLights((elapsed - last) / (duration - last));
        mField504 = elapsed;
    }
    if (mFlag84 && Switch(kFadeSwitch)) {
        Call<20, void>(mLitObject); // receiver: the lit object, slot 20
        return;
    }
    // receiver: the cached singleton (0x20ddfb; class not identified): slot 22's object's slot 20's object's slot 6.
    void *const first = Call<22, void *>(GetCachedSingleton());
    void *const second = Call<20, void *>(first);
    Call<6, uint32_t>(second);
}

} // namespace nTSSG
