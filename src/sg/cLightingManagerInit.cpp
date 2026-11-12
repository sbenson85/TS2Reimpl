// nTSSG::cLightingManager's Init (lane 261110v; slot 3) and Shutdown (lane 261110u; slot 4), scenegraph LG3g. See
// sg/cLightingManager.h.
//
// Library code the members' types bring (not ours, never hooked): the sky samples' vector destroy (0x2c90d0, std::vector's)
// and the strings' freeing (0x152b80, the temporary keys').
//
// Still the game's, called through OriginalFunction: the world database's cached getter 0x20e0b2.
#include "sg/cLightingManager.h"
#include "sg/cLightingManagerDetail.h" // Switch, SampleKey, kLightRefNodeClass

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZPropertyManager.h"
#include "gz/cIGZCOM.h"
#include "gz/cIGZPropertySet.h" // GZServiceString
#include "ifaces/cIGZFrameWork.h"
#include "gz/cFileParser.h"
#include "misc/UtilitiesC.h"     // KeyMapSlot5Walker
#include "rz/Singleton_20ddfb.h" // GetServiceObject, ScopedRef
#include "rz/MiscHelpersB.h"     // KeyMapWalker
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "sg/cDiffusionManager.h"
#include "sg/cLerpLightSet.h"
#include "sg/cLightRefNode.h"
#include "sg/cLightingParserState.h"
#include "sg/cRenderGroupNameIterator.h"
#include "sg/cTextureManager.h"

#include <cstdint>
#include <new>

namespace nTSSG {

namespace {

constexpr uint32_t kSettingsId = 0x0ac1887e, kSettingsIid = 0x6bb22f38;
constexpr uint32_t kFirstOption = 0x2bf18d56;    // the fade byte 0xffde34's message
constexpr uint32_t kOptionNames = 0xffde38;      // the 11 name cells (12 bytes apart: each is a settings row's name)
constexpr uint32_t kOptionDefaults = 0xffde30;   // each row's value cell (the row's first word), handed as the default
constexpr uint32_t kOptionStride = 12;
constexpr uint32_t kFadeSwitch = 0xffde34;       // the cDiffusionManager is made only when it is set

// The director's classes Init makes.
constexpr uint32_t kFileParserClass = 0x5cf9459a, kFileParserIid = 0x1cf94583;     // nGZCommandParser::cFileParser
constexpr uint32_t kShadeTextureClass = 0x2a956dad, kShadeTextureIid = 0x2a956da8; // the shade texture's image
constexpr uint32_t kFogLightClass = 0xac06a66f, kFogLightIid = 0x0c069ed0;         // cLinearFogLight, cILinearFogLight
constexpr uint32_t kShadeTextureName = 0xdb60ac; // the game's text the texture manager files the shade texture under

// The render groups' names (the game's texts, in the order Init asks the registry for them).
constexpr uint32_t kPortalGroupNames[3] = {0xda63d4, 0xdb076c, 0xdb0764};
constexpr uint32_t kOtherGroupNames[2] = {0xdb6074, 0xdb607c};
constexpr uint32_t kShadeGroupName = 0xdb34ec;
constexpr uint32_t kEnvironmentGroupNames[3] = {0xdb606c, 0xdb2b1c, 0xdb2b24};

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// A counted pointer let go of when it is not null: nulled first, then slot `kSlot` called.
template <int kSlot, class T> void Drop(T *&member)
{
    if (T *const object = member) {
        member = nullptr;
        RawSlot<kSlot, void>(object); // receiver: the member's object (its release slot)
    }
}

// A reference member set to `value`: the new one counted (its slot `kAddRef`) when not null, stored, the old one
// released (`kRelease`) when not null; nothing when they are the same.
template <int kAddRef, int kRelease, class T> void Assign(T *&member, T *value)
{
    T *const old = member;
    if (value == old) return;
    if (value) RawSlot<kAddRef, void>(value); // receiver: the member's object (its AddRef slot)
    member = value;
    if (old) RawSlot<kRelease, void>(old); // receiver: the member's object (its Release slot)
}

// The cIGZUnknown of an object whose vbptr is at +8 (a cLightingParserState): its vbtable's second entry places it.
void *UnknownOf(uint8_t *object)
{
    uint8_t *const vbptr = object + 8;
    return vbptr + (*reinterpret_cast<int32_t *const *>(vbptr))[1];
}

// Its cIBaseParser face: the vbtable's third entry; null for null.
nGZCommandParser::cIBaseParser *ParserOf(uint8_t *object)
{
    if (!object) return nullptr;
    uint8_t *const vbptr = object + 8;
    return reinterpret_cast<nGZCommandParser::cIBaseParser *>(vbptr + (*reinterpret_cast<int32_t *const *>(vbptr))[2]);
}

// A director's class made into a member: the director asked for first, the old one let go of (nulled, slot 2), then
// the director's slot 3 asked for the class into the member (its answer not read).
template <class T> void Remake(T *&member, uint32_t classId, uint32_t iid)
{
    cIGZCOM *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    Drop<2>(member);
    com->GetClassObject(classId, iid, reinterpret_cast<void **>(&member)); // receiver: cIGZCOM
}

// A render group's bit, as the registry's slot 3 answers it (bts: the low five bits).
uint32_t GroupBit(nGZSceneGraph::cIRenderGroupRegistry *registry, uint32_t name)
{
    return 1u << (registry->BitOfName(GameAddress<const char>(name)) & 31); // receiver: nGZSceneGraph::cIRenderGroupRegistry
}

cLerpLightSet *NewLerpSet()
{
    void *const memory = ::operator new(0x1c);
    return memory ? static_cast<cLerpLightSet *>(memory)->Construct() : nullptr;
}

} // namespace

bool cLightingManager::Init()
{
    if (mInitialised) return true;
    mInitialised = 1;
    cIGZMessageTarget2 *const target = reinterpret_cast<cIGZMessageTarget2 *>(&mVTable4);
    reinterpret_cast<KeyMapSlot5Walker *>(&mMessageTypes)->CallSlot5ForEachKey(Word(target), nullptr);

    if (Switch(kFadeSwitch)) {
        void *const memory = ::operator new(0x94);
        void *const made = memory ? static_cast<void *>(cDiffusionManager::Construct(memory, nullptr)) : nullptr;
        Assign<1, 2>(mLitObject, made); // receiver: nTSSG::cDiffusionManager (AddRef 1, Release 2)
        static_cast<cIDiffusionManager *>(mLitObject)->Init(); // receiver: nTSSG::cDiffusionManager (re-read, not checked)
    }
    Assign<1, 2>(mTerrain, OriginalFunction<void *(__cdecl *)(), 0x20e0b2>()()); // receiver: the world database

    {
        void *const memory = ::operator new(cLightingParserState::kSize);
        uint8_t *const made =
            memory ? reinterpret_cast<uint8_t *>(static_cast<cLightingParserState *>(memory)->Construct()) : nullptr;
        uint8_t *const old = mRef2C;
        if (made != old) {
            if (made) RawSlot<1, void>(UnknownOf(made)); // receiver: cIGZUnknown (the parser state's virtual base; AddRef)
            mRef2C = made;
            if (old) RawSlot<2, void>(UnknownOf(old)); // receiver: cIGZUnknown (Release)
        }
    }

    Remake(mFileParser, kFileParserClass, kFileParserIid);
    mFileParser->Init();                          // receiver: nGZCommandParser::cIFileParser (not null-checked)
    nGZCommandParser::cIBaseParser *const parser = ParserOf(mRef2C);
    mFileParser->SetParser(parser, 0);            // receiver: nGZCommandParser::cIFileParser

    cIGZUnknown *image = nullptr;
    static_cast<cIGZCOM *>(GetDirectorFrameworkObject())
        ->GetClassObject(kShadeTextureClass, kShadeTextureIid, reinterpret_cast<void **>(&image)); // receiver: cIGZCOM
    {
        // receiver: nGZSceneGraph::cITextureManager
        const nGZSceneGraph::cTextureHandle made(
            nGZSceneGraph::TextureManager()->Add(image, GameAddress<const char>(kShadeTextureName), 1));
        *nGZSceneGraph::cTextureHandle::At(&mShadeTexture) = made;
    }

    Remake(mRef238, kLightRefNodeClass, nGZSceneGraph::cILightRefNode::kIid);
    Remake(mRef23C, kFogLightClass, kFogLightIid);
    // receiver: nGZSceneGraph::cILightRefNode (not null-checked)
    static_cast<nGZSceneGraph::cILightRefNode *>(mRef238)->SetLight(static_cast<nGZSceneGraph::cILight *>(mRef23C));

    ScopedRef settings{nullptr};
    if (auto *const framework = static_cast<ifaces::cIGZFrameWork *>(GetServiceObject()))
        framework->GetSystemService(kSettingsId, kSettingsIid, &settings.mObject); // receiver: ifaces::cIGZFrameWork
    if (auto *const manager = static_cast<cIGZPropertyManagerString *>(settings.mObject)) {
        for (uint32_t i = 0; i < 11; ++i) {
            const uint32_t id = i == 0 ? kFirstOption : kFirstOption + 1 + i;
            GZServiceString name(*GameAddress<const char *>(kOptionNames + kOptionStride * i));
            // receiver: cIGZPropertyManagerString
            manager->AddNotification(target, id, &name, GameAddress<void>(kOptionDefaults + kOptionStride * i));
        }
    }

    mPortalGroups[0] = mPortalGroups[1] = mPortalGroups[2] = 0;
    mOtherGroup = 0;
    mShadeGroup = 0;
    mEnvironmentGroup = 0;
    nGZSceneGraph::cIRenderGroupRegistry *const registry = nGZSceneGraph::RenderGroupRegistry();
    // Each member is read after its call (a registry that wrote it would be seen).
    for (int i = 0; i < 3; ++i) {
        const uint32_t bit = GroupBit(registry, kPortalGroupNames[i]);
        mPortalGroups[i] |= bit;
    }
    for (const uint32_t name : kOtherGroupNames) {
        const uint32_t bit = GroupBit(registry, name);
        mOtherGroup |= bit;
    }
    {
        const uint32_t shade = GroupBit(registry, kShadeGroupName);
        const uint32_t shadeGroup = mShadeGroup | shade;
        // The groups so far, as they are in memory then, go to the environment's too.
        mEnvironmentGroup |= mPortalGroups[2] | mOtherGroup | mPortalGroups[1] | mPortalGroups[0];
        mShadeGroup = shadeGroup;
    }
    for (const uint32_t name : kEnvironmentGroupNames) {
        const uint32_t bit = GroupBit(registry, name);
        mEnvironmentGroup |= bit;
    }

    Assign<2, 1>(mLerp22C, NewLerpSet()); // receiver: nTSSG::cLerpLightSet (cRZRefCount: slot 2 AddRef, slot 1 Release)
    mLerp22C->SetKeys(SampleKey(0), 0);  // the member re-read
    Assign<2, 1>(mLerp230, NewLerpSet()); // receiver: nTSSG::cLerpLightSet
    mLerp230->SetKeys(SampleKey(0), 1);

    settings.ReleaseObject();
    if (image) image->Release(); // receiver: cIGZUnknown (the shade texture's image; not nulled)
    return true;
}

bool cLightingManager::Shutdown()
{
    if (!mInitialised) return true;
    mInitialised = 0;
    mSunShadows = 0;

    ScopedRef settings{nullptr};
    if (auto *const framework = static_cast<ifaces::cIGZFrameWork *>(GetServiceObject()))
        framework->GetSystemService(kSettingsId, kSettingsIid, &settings.mObject);
    if (auto *const manager = static_cast<cIGZPropertyManagerString *>(settings.mObject)) {
        cIGZMessageTarget2 *const target = reinterpret_cast<cIGZMessageTarget2 *>(&mVTable4);
        for (uint32_t i = 0; i < 11; ++i) {
            // The message ids run on from 0x2bf18d58 after the first: 0x2bf18d57 is not one of them.
            const uint32_t id = i == 0 ? kFirstOption : kFirstOption + 1 + i;
            GZServiceString name(*GameAddress<const char *>(kOptionNames + kOptionStride * i));
            manager->RemoveNotification(target, id, &name);
        }
    }
    nGZSceneGraph::cTextureHandle::At(&mShadeTexture)->Reset();
    mFileParser->Shutdown(); // receiver: nGZCommandParser::cIFileParser (not null-checked)
    Drop<2>(mFileParser);
    if (mRef2C) {
        // Released through its virtual base: the vbptr at +8, the base's offset in its second entry.
        uint8_t *const object = mRef2C;
        mRef2C = nullptr;
        uint8_t *const vbptr = object + 8;
        const int32_t offset = (*reinterpret_cast<int32_t *const *>(vbptr))[1];
        RawSlot<2, void>(vbptr + offset); // receiver: cIGZUnknown (the object's virtual base; Release)
    }
    Drop<2>(mTerrain);
    Drop<2>(mWorld);
    Drop<2>(mRef24);
    if (mLitObject) {
        RawSlot<4, void>(mLitObject); // receiver: nTSSG::cDiffusionManager (cIDiffusionManager slot 4, Shutdown 0x3c6050)
        Drop<2>(mLitObject);          // the member is read again after the call
    }
    reinterpret_cast<KeyMapWalker *>(&mMessageTypes)
        ->CallSlot6ForEachKey(Word(reinterpret_cast<cIGZMessageTarget2 *>(&mVTable4)), nullptr);
    if (mSceneManager) {
        RemoveSampleLights();
        RemoveBuiltLights();
        Drop<2>(mSceneManager); // receiver: nTSSG::cSceneManager (Release; the member is read again)
    }
    Drop<2>(mRef238);
    Drop<2>(mRef23C);
    mSkySamples.clear();
    mLightingPath.clear();
    Drop<1>(mLerp22C); // receiver: nTSSG::cLerpLightSet (cRZRefCount Release)
    Drop<1>(mLerp230);
    settings.ReleaseObject();
    return true;
}

} // namespace nTSSG
