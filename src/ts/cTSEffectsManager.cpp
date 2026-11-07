#include "rz/RZMessageServer.h"
#include "ts/cTSEffectsManager.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZMessage2Standard.h"
#include "gz/cIGZPropertySet.h"
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersB.h"
#include "rz/StaticGetters.h"
#include "rz/RZAutoRef.h"
#include "rz/RZMemory.h"
#include "rz/RZWildcard.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/cRZCallbackTimer.h"
#include "rz/cRZFile.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZString.h"
#include "simantics/ServiceRefVector.h"
#include "ts/cTSEffectsMaps.h"
#include "ts/cTSEffectsResourceFactory.h"
#include "ts/cTSVisualEffect.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <new>

// cTSEffectsManager (lane IT). Container operations follow the originals' order (the containers are the game's own
// standard-library types). Calls into code nobody has replaced yet go through OriginalFunction (the status entries list
// them).

namespace {

// The original's float floor (an inlined helper): round (x - 0.5) to nearest through the 1.5 * 2^23 bias, then take the
// next integer when it equals x exactly.
int32_t FloorToInt(float x)
{
    const float biased = (x - 0.5f) + 12582912.0f;
    int32_t bits;
    std::memcpy(&bits, &biased, 4);
    int32_t result = bits - 0x4b400000;
    const int32_t next = result + 1;
    if (static_cast<float>(next) == x) result = next;
    return result;
}

// An unsigned count as a float the original's way: through a double (the signed conversion, 2^32 added when negative).
float UnsignedToFloat(uint32_t value)
{
    return static_cast<float>(static_cast<double>(value));
}

void LowerInPlace(std::string &text)
{
    for (char &c : text) c = static_cast<char>(tolower(c));
}

// The cached message server (0x9c5a89, cdecl; not ours yet). Its slot 5 registers a target for a message id, slot 6
// unregisters it.
cIGZUnknown *MessageServer()
{
    return static_cast<cIGZUnknown *>(GetCachedMessageServer());
}

// 0xbcb4c0 (thiscall on the manager, ret 8; not ours yet): applies the level of detail and debug bits to one effect.
void ApplyDetail(cTSEffectsManager *manager, cITSVisualEffect *effect, uint32_t activate, uint32_t changedBits)
{
    OriginalFunction<void(__thiscall *)(void *, void *, uint32_t, uint32_t), 0xbcb4c0>()(
        static_cast<cITSEffectsManager *>(manager), effect, activate, changedBits);
}

} // namespace

// ---- helpers of the members ----

fx::PathString::PathString() : mVTable(GameAddress<void>(cRZString::kVTable)), mRefCount(0) {}

// 0xbc4640: the name and the renderer's name empty, the index -1, the variant 1, no effect.
fx::EffectListEntry::EffectListEntry() : mIndex(-1), mVariant(1), mObject(nullptr) {}

// 0xbcc640: the copy takes the strings by data and size, the index and variant, and counts the effect (slot 1).
fx::EffectListEntry::EffectListEntry(const EffectListEntry &other)
    : mName(other.mName), mIndex(other.mIndex), mVariant(other.mVariant), mResolved(other.mResolved),
      mObject(other.mObject)
{
    if (mObject) mObject->AddRef();
}

// 0xbc5420: the effect released (slot 2), then the strings.
fx::EffectListEntry::~EffectListEntry()
{
    if (mObject) mObject->Release();
}

fx::PathString::PathString(const char *text) : mVTable(GameAddress<void>(cRZString::kVTable)), mText(text), mRefCount(0) {}

// 0xbcc740's node construction: the cRZString vtable, the text by data and size, count 0.
fx::PathString::PathString(const PathString &other)
    : mVTable(GameAddress<void>(cRZString::kVTable)), mText(other.mText.c_str(), other.mText.size()), mRefCount(0)
{
}

fx::CallbackTimerMember::CallbackTimerMember()
{
    reinterpret_cast<cRZCallbackTimer *>(mRaw)->Construct();
}

fx::CallbackTimerMember::~CallbackTimerMember()
{
    reinterpret_cast<cRZCallbackTimer *>(mRaw)->Destruct();
}

// ---- construction ----

// 0xbc4870: the collection, every member in order (the maps with 8 buckets, the random generator seeded 0x1105), the six
// message ids DoMessage handles, then the cheats (0xbc5aa0, 0xbdb700: not ours yet) registered into mCheats.
cTSEffectsManager::cTSEffectsManager()
    : mInitialized(false), mRefCount(0), mState(0), mFlags(0), mByte8c(0), mMaxDetail(true), mNoFlush(false),
      mByteA58(false), mReloading(false), mWordA5C(3), mTimeMode(1), mWordA64(0), mMaxElapsed(0.0f), mDetail(-1),
      mPreviousDetail(-1), mDetailLevel(-1.0f), mPreviousDetailLevel(-1.0f), mDetailIsMax(0), mDetailBias(0.0f),
      mDebugBits(0), mPreviousDebugBits(0), mWantedDebugBits(0), mWordAF8(2), mWordAFC(0), mWordB00(0), mCounters{},
      mParams{1.0f, 1.0f, 1000.0f}, mDetailFraction(0.0f), mWordsB44{}, mHalfB50(0), mByteB52(0), mWordB54(0),
      mDebugValue{-1, -1}
{
    mRandom.Construct(0x1105);
    mMessageIds.insert(0x122ced69);
    mMessageIds.insert(0x322ced70);
    mMessageIds.insert(0xd23b7d7c);
    mMessageIds.insert(0xb23b7d66);
    mMessageIds.insert(0x522ced95);
    mMessageIds.insert(0xb23b7d6f);
    mDebugValue[0] = -1;
    OriginalFunction<void(__cdecl *)(void *, void *), 0xbc5aa0>()(&mCheats, static_cast<cITSEffectsManager *>(this));
    OriginalFunction<void(__cdecl *)(void *, void *), 0xbdb700>()(&mCheats, static_cast<cITSEffectsManager *>(this));
}

// 0xbc5500: members in reverse order, then the collection.
cTSEffectsManager::~cTSEffectsManager() = default;

// 0xbc86f0 (cdecl): operator new(0xc2c) and the constructor; null when the allocation fails.
cTSEffectsManager *cTSEffectsManager::Create()
{
    return new (std::nothrow) cTSEffectsManager;
}

// ---- cIGZUnknown ----

// 0xbc9ea0 (ret 8): 1 and the manager's id give +0, the message target's id +4, the collection's id +8; counted through
// slot 1. The out pointer is not checked.
bool cTSEffectsManager::QueryInterface(uint32_t riid, void **out)
{
    if (riid == kInterfaceId || riid == 1) {
        *out = static_cast<cITSEffectsManager *>(this);
        AddRef();
        return true;
    }
    if (riid == kMessageTargetId) {
        *out = static_cast<cIGZMessageTarget2 *>(this);
        AddRef();
        return true;
    }
    if (riid == cTSEffectsCollection::kInterfaceId) {
        *out = static_cast<cTSEffectsCollection *>(this);
        AddRef();
        return true;
    }
    return false;
}

// Folded 0x207d70: ++count.
uint32_t cTSEffectsManager::AddRef()
{
    return ++mRefCount;
}

// 0xbca210: above 1 the count drops; otherwise the manager is deleted (slot 12 of the collection face) and 0 returned, the
// count left as it was.
uint32_t cTSEffectsManager::Release()
{
    if (mRefCount > 1) return --mRefCount;
    delete this;
    return 0;
}

// ---- start and stop ----

namespace {

// The settings the manager follows (Init registers for each, Shutdown withdraws): the name, the message the settings service
// sends on a change, and the cell of the option global (0x107d500 and on: a vtable word, then the value).
struct SettingsOption {
    const char *name;
    uint32_t message;
    uint32_t cell;
};
const SettingsOption kSettingsOptions[] = {
    {"particleSizeThreshold", 0x6bf17c99, 0x107d500}, {"particleDensity", 0x6bf17c9a, 0x107d50c},
    {"particleScale", 0x6bf17c9b, 0x107d518},         {"maxParticlesTarget", 0x6bf17c9c, 0x107d524},
    {"particleDamping", 0x6bf17c9d, 0x107d530},       {"particleLODOffset", 0x6bf17c9e, 0x107d53c},
    {"effectPriorityLevel", 0x6bf17c9f, 0x107d548},   {"effectLODSoftChange", 0x6bf17ca0, 0x107d554},
    {"effectMaxTimeDelta", 0x6bf17ca1, 0x107d560},    {"useEffects", 0x6bf17ca2, 0x107d56c},
    {"enableSnapshot", 0x6bf17ca3, 0x107d578},
};

// A service of the framework (GetServiceObject's slot 5): the object, counted, or null.
void *QueryService(uint32_t classId, uint32_t interfaceId)
{
    void *object = nullptr;
    if (void *framework = GetServiceObject())
        VirtualSlot<bool(__thiscall *)(void *, uint32_t, uint32_t, void **)>(framework, 5)(framework, classId, interfaceId,
                                                                                         &object);
    return object;
}

// The framework's CreateInstance (the director's framework object, slot 3): an object of a class into `out`.
void CreateInstance(void *framework, uint32_t classId, uint32_t interfaceId, void **out)
{
    VirtualSlot<bool(__thiscall *)(void *, uint32_t, uint32_t, void **)>(framework, 3)(framework, classId, interfaceId, out);
}

void ReleaseObject(void *object)
{
    if (object) VirtualSlot<uint32_t(__thiscall *)(void *)>(object, 2)(object);
}

} // namespace

// 0xbc7350 (cdecl): the once-a-second message to the message server, when there is one (the message handed over with no
// reference of ours).
void __cdecl cTSEffectsManager::OnTimer(void *)
{
    void *server = GetCachedMessageServer();
    if (!server) return;
    cRZMessage2Standard *message = nullptr;
    if (void *memory = RZMemory::Malloc(0x2c)) message = static_cast<cRZMessage2Standard *>(memory)->Construct();
    VirtualSlot<void(__thiscall *)(void *, uint32_t)>(message, 5)(message, 0x697d01ed);
    VirtualSlot<void(__thiscall *)(void *, void *, uint32_t)>(server, 4)(server, message, 0);
}

// 0xbc8fa0 (slot 3): once. The message ids registered, the counters cleared, the maps object and two overlay managers
// (0xbd9fd0, not ours yet) made and started, the parser and renderer created and joined, the resource factory registered
// with the resource service, the settings service told to notify this of each option, the settings read, the plane collider
// made, the one-second timer started, the cheats registered and the debug object kept. Always true.
bool cTSEffectsManager::Init()
{
    if (mInitialized) return true;
    mInitialized = true;
    cIGZMessageTarget2 *const target = this;
    reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)
        ->CallSlot5ForEachKey(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target)), nullptr);
    std::memset(mCounters, 0, sizeof(mCounters));

    mRefBCC.Assign(static_cast<cITSEffectsMaps *>(new (std::nothrow) cTSEffectsMaps));
    VirtualSlot<bool(__thiscall *)(void *)>(mRefBCC.Get(), 3)(mRefBCC.Get());
    for (int i = 0; i < 2; ++i) {
        void *memory = ::operator new(0x78, std::nothrow);
        cIGZUnknown *overlay =
            memory ? static_cast<cIGZUnknown *>(OriginalFunction<void *(__thiscall *)(void *), 0xbd9fd0>()(memory)) : nullptr;
        mSlotObjects[i].Assign(overlay);
        VirtualSlot<bool(__thiscall *)(void *)>(mSlotObjects[i].Get(), 3)(mSlotObjects[i].Get());
    }

    void *framework = GetDirectorFrameworkObject();
    mParser.Reset();
    CreateInstance(framework, 0x5cf9459a, 0x1cf94583, reinterpret_cast<void **>(mParser.Address()));
    framework = GetDirectorFrameworkObject();
    mRenderer.Reset();
    CreateInstance(framework, 0xa956ad14, 0x6956babc, &mRenderer.mObject);
    {
        void *parser = mParser.Get();
        VirtualSlot<void(__thiscall *)(void *)>(parser, 3)(parser);
    }
    {
        void *renderer = mRenderer.mObject;
        VirtualSlot<void(__thiscall *)(void *, void *)>(renderer, 0)(renderer, static_cast<cITSEffectsManager *>(this));
        cTSEffectsCollection *const collection = this;
        VirtualSlot<void(__thiscall *)(void *, void *)>(mRenderer.mObject, 1)(mRenderer.mObject, collection);
        void *parser = mParser.Get();
        void *base = mRenderer.mObject ? mRenderer.Base(2) : nullptr;
        VirtualSlot<void(__thiscall *)(void *, void *, uint32_t)>(parser, 5)(parser, base, 1);
    }

    RZAutoRef resourceService{QueryService(0x056b906e, 0x656b8efc)};
    if (resourceService.mObject) {
        cTSEffectsResourceFactory *factory = new (std::nothrow) cTSEffectsResourceFactory;
        if (factory) factory->AddRef();
        VirtualSlot<void(__thiscall *)(void *, uint32_t, uint32_t, void *)>(resourceService.mObject, 20)(
            resourceService.mObject, 0xea5118b5, 0xea5118b0, factory);
        if (factory) factory->Release();
    }

    ScopedRef settings{QueryService(0x0ac1887e, 0x6bb22f38)};
    if (settings.mObject) {
        for (const SettingsOption &option : kSettingsOptions) {
            const GZServiceString key(option.name);
            VirtualSlot<void(__thiscall *)(void *, void *, uint32_t, const GZServiceString *, void *)>(
                settings.mObject, 4)(settings.mObject, target, option.message, &key, GameAddress<void>(option.cell));
        }
    }

    ApplySettings();
    if (void *server = GetCachedMessageServer())
        VirtualSlot<void(__thiscall *)(void *, void *, uint32_t)>(server, 5)(server, target, 0x697d01ed);

    // The plane collider (cTSPlaneCollider: 12 bytes, its two vtables, count 0), kept under "plane".
    {
        auto *collider = static_cast<uint32_t *>(::operator new(0xc, std::nothrow));
        if (collider) {
            collider[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GameAddress<void>(0xe8ce50)));
            collider[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GameAddress<void>(0xe8ce70)));
            collider[2] = 0;
        }
        SetNamedA("plane", reinterpret_cast<cIGZUnknown *>(collider));
    }

    {
        void *timer = mTimer.mRaw;
        VirtualSlot<bool(__thiscall *)(void *, void *, void *)>(timer, 9)(timer, reinterpret_cast<void *>(&OnTimer), nullptr);
        VirtualSlot<bool(__thiscall *)(void *, uint32_t, void *)>(timer, 5)(timer, 1000, static_cast<cITSEffectsManager *>(this));
    }

    ScopedRef cheats{QueryService(0x2a4915ea, 0x4a490e00)};
    if (cheats.mObject) {
        for (size_t i = 0; i < mCheats.size(); ++i)
            VirtualSlot<void(__thiscall *)(void *, void *)>(cheats.mObject, 4)(cheats.mObject, mCheats[i].Get());
    }
    cheats.ReleaseObject();

    ScopedRef debug{QueryService(0xa417445e, 0x5a4)};
    mRefC18.Assign(static_cast<cIGZUnknown *>(debug.mObject));
    debug.ReleaseObject();
    settings.ReleaseObject();
    resourceService.ReleaseObject();
    return true;
}

// 0xbcaa00 (slot 4): once. The reverse: the cheats withdrawn from their service, the scene, camera and debug value dropped,
// the option notifications withdrawn, the timer stopped, the message registrations withdrawn, the aliases, triggers, maps
// object, named objects, renderer, parser and pooled effects released, then the collection's descriptions, effects and name
// index emptied (its message aliases and triggers stay). Always true.
bool cTSEffectsManager::Shutdown()
{
    if (!mInitialized) return true;
    mInitialized = false;
    mByte8c = 0;
    mRefC18.Reset();
    {
        ScopedRef cheats{QueryService(0x2a4915ea, 0x4a490e00)};
        if (cheats.mObject) {
            for (size_t i = 0; i < mCheats.size(); ++i)
                VirtualSlot<void(__thiscall *)(void *, void *)>(cheats.mObject, 6)(cheats.mObject, mCheats[i].Get());
        }
        cheats.ReleaseObject();
    }
    mScene.Reset();
    mCamera.Reset();
    mDebugValue[0] = -1;

    cIGZMessageTarget2 *const target = this;
    ScopedRef settings{QueryService(0x0ac1887e, 0x6bb22f38)};
    if (settings.mObject) {
        for (const SettingsOption &option : kSettingsOptions) {
            const GZServiceString key(option.name);
            VirtualSlot<void(__thiscall *)(void *, void *, uint32_t, const GZServiceString *)>(settings.mObject, 5)(
                settings.mObject, target, option.message, &key);
        }
    }

    {
        void *timer = mTimer.mRaw;
        VirtualSlot<bool(__thiscall *)(void *)>(timer, 7)(timer);
    }
    if (void *server = GetCachedMessageServer()) {
        VirtualSlot<void(__thiscall *)(void *, void *, uint32_t)>(server, 6)(server, target, 0x697d01ed);
        for (size_t i = 0; i < mMessageTriggers.size(); ++i)
            VirtualSlot<void(__thiscall *)(void *, void *, uint32_t)>(server, 6)(server, target,
                                                                              mMessageTriggers[i].mMessage);
    }
    mAliases.clear();
    mTriggers.clear();
    if (mRefBCC.Get()) {
        VirtualSlot<bool(__thiscall *)(void *)>(mRefBCC.Get(), 4)(mRefBCC.Get());
        mRefBCC.Reset();
    }
    mNamedA.clear();
    mRenderer.Reset();
    {
        void *parser = mParser.Get();
        VirtualSlot<bool(__thiscall *)(void *)>(parser, 4)(parser);
        mParser.Reset();
    }
    for (auto it = mPool.begin(); it != mPool.end(); ++it) {
        void *face = reinterpret_cast<uint8_t *>(it->Get()) + 4;
        VirtualSlot<void(__thiscall *)(void *)>(face, 1)(face);
        it->Reset();
    }
    mPool.clear();
    mTables.clear();
    mEffects.clear();
    mEffectsByName.clear();
    reinterpret_cast<KeyMapWalker *>(&mMessageIds)
        ->CallSlot6ForEachKey(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target)), nullptr);
    settings.ReleaseObject();
    return true;
}

// ---- pool and lookups ----

// 0xbca590 (stdcall, ret 4): a message 0x4ba66b69 carrying the text (an empty string for none) posted to the message server, when
// there is one. The message and the string are handed over with no reference of ours (the server counts the message).
void __stdcall cTSEffectsManager::SendText(const char *text)
{
    void *server = GetCachedMessageServer();
    if (!server) return;
    cRZMessage2Standard *message = nullptr;
    if (void *memory = RZMemory::Malloc(0x2c)) message = static_cast<cRZMessage2Standard *>(memory)->Construct();
    VirtualSlot<void(__thiscall *)(void *, uint32_t)>(message, 5)(message, 0x4ba66b69);
    cRZString *string = static_cast<cRZString *>(::operator new(sizeof(cRZString), std::nothrow));
    if (string) {
        if (text)
            string->Construct(text);
        else
            string->Construct();
    }
    VirtualSlot<void(__thiscall *)(void *, void *)>(message, 33)(message, string);
    VirtualSlot<void(__thiscall *)(void *, void *, uint32_t)>(server, 4)(server, message, 0);
}

// The pool's new effect: operator new(400), then the constructor (0xbe86a0). Out of line so a test can stand in for it.
__declspec(noinline) cITSVisualEffect *cTSEffectsManager::NewVisualEffect()
{
    return new (std::nothrow) cTSVisualEffect;
}

// 0xbc9ab0: a free effect is taken back (its index moves from the free list to the pending list); otherwise a new one is made,
// its index (the pool's size) queued as pending and the effect put into the pool, counted.
cITSVisualEffect *cTSEffectsManager::AcquireEffect()
{
    if (!mFree.empty()) {
        mPending.push_back(mFree.back());
        mFree.pop_back();
        return mPool[mPending.back()].Get();
    }
    cITSVisualEffect *effect = NewVisualEffect();
    mPending.push_back(static_cast<uint32_t>(mPool.size()));
    const EffectRef held(effect);
    mPool.push_back(held);
    return effect;
}

// 0xbc8590: of the effects under the name (the key is lower case), the one with the highest variant the detail level allows
// (1 to mWordA5C); -1 when none.
int32_t cTSEffectsManager::FindEffectIndex(const std::string &key)
{
    const auto range = mEffectsByName.equal_range(key);
    uint32_t best = 0;
    int32_t index = -1;
    for (auto it = range.first; it != range.second; ++it) {
        const uint32_t variant = mEffects[it->second].mVariant;
        if (variant <= static_cast<uint32_t>(mWordA5C) && best < variant) {
            index = static_cast<int32_t>(it->second);
            best = variant;
        }
    }
    return index;
}

// 0xbc8620: the same for the effect a message-alias pair maps to (one effect per pair); -1 when the pair has none.
int32_t cTSEffectsManager::FindAliasIndex(uint32_t first, uint32_t second)
{
    const auto found = mAliases.find(fx::AliasKey{first, second});
    if (found == mAliases.end()) return -1;
    const uint32_t variant = mEffects[found->second].mVariant;
    if (variant <= static_cast<uint32_t>(mWordA5C) && 0 < variant) return static_cast<int32_t>(found->second);
    return -1;
}

// 0xbc9e20: the renderer told whether the flag bit 1 is set (its slot 4), then the parser's message sent: its text when the
// parser (slot 7) has no changes, else none.
__declspec(noinline) void cTSEffectsManager::ApplyParserState()
{
    void *renderer = mRenderer.mObject;
    VirtualSlot<void(__thiscall *)(void *, uint32_t)>(renderer, 4)(renderer, (mFlags >> 1) & 1);
    void *parser = mParser.Get();
    if (!VirtualSlot<bool(__thiscall *)(void *)>(parser, 7)(parser)) {
        void *object = VirtualSlot<void *(__thiscall *)(void *)>(parser, 9)(parser);
        SendText(VirtualSlot<const char *(__thiscall *)(void *)>(object, 0x2c)(object));
        return;
    }
    SendText(nullptr);
}

// ---- creation ----

// 0xbc61e0 (ret 0xc): an effect for a message-alias pair (not in a bad state). A pair mapping to no usable effect (-1) that
// has no entry at all is reported to the effects service (slot 0x22 with -1) and fails; otherwise an effect is taken from the
// pool, attached, given the pair and the index, and for a real index described (0xbe9670), the pending ones activated and
// the service told (slot 0x23). The effect is handed out counted.
bool cTSEffectsManager::CreateEffect(uint32_t first, uint32_t second, void **out)
{
    if (!out || mState == 1) return 0;
    const int32_t index = FindAliasIndex(first, second);
    if (index < 0 && mAliases.find(fx::AliasKey{first, second}) == mAliases.end()) {
        ScopedRef service{nullptr};
        if (void *framework = GetServiceObject())
            VirtualSlot<bool(__thiscall *)(void *, uint32_t, uint32_t, void **)>(framework, 5)(framework, 0x8d127822, 0x6d1922a7,
                                                                                             &service.mObject);
        if (service.mObject) VirtualSlot<void(__thiscall *)(void *, int32_t)>(service.mObject, 0x22)(service.mObject, index);
        service.ReleaseObject();
        return 0;
    }
    cITSVisualEffect *effect = AcquireEffect();
    void *face = reinterpret_cast<uint8_t *>(effect) + 4;
    cITSEffectsManager *owner = this;
    VirtualSlot<void(__thiscall *)(void *, void *)>(face, 0)(face, owner);
    auto *fields = reinterpret_cast<uint8_t *>(effect);
    *reinterpret_cast<uint32_t *>(fields + 0x2c) = first;
    *reinterpret_cast<uint32_t *>(fields + 0x30) = second;
    *reinterpret_cast<int32_t *>(fields + 0x34) = index;
    if (index >= 0) {
        OriginalFunction<void(__thiscall *)(void *, void *), 0xbe9670>()(effect, &mEffects[index]);
        FlushPending();
        ScopedRef service{nullptr};
        if (void *framework = GetServiceObject())
            VirtualSlot<bool(__thiscall *)(void *, uint32_t, uint32_t, void **)>(framework, 5)(framework, 0x8d127822, 0x6d1922a7,
                                                                                             &service.mObject);
        if (service.mObject) VirtualSlot<void(__thiscall *)(void *, int32_t)>(service.mObject, 0x23)(service.mObject, index);
        service.ReleaseObject();
    }
    *out = effect;
    effect->AddRef();
    return 1;
}

// 0xbc6360 (ret 8): an effect by name (not in a bad state, the name given and the output wanted). The name goes lower case
// into the scratch key; with no usable variant the creation fails when the name is not known at all (the effects service told,
// slot 0x20, with the name) and goes on with index -1 when it is. Otherwise as CreateEffect: a pool effect, attached, named,
// indexed, and for a real index described, the pending ones activated and the service told (slot 0x21, the name).
bool cTSEffectsManager::CreateVisualEffect(const char *name, void **out)
{
    if (mState == 1 || !name || *name == 0 || !out) return 0;
    SetScratchLower(name);
    const int32_t index = FindEffectIndex(mScratch);
    bool failed = false;
    if (index < 0) {
        const std::string copy(mScratch.c_str());
        failed = !HasEffectName(copy);
    }
    if (failed) {
        ScopedRef service{nullptr};
        if (void *framework = GetServiceObject())
            VirtualSlot<bool(__thiscall *)(void *, uint32_t, uint32_t, void **)>(framework, 5)(framework, 0x8d127822, 0x6d1922a7,
                                                                                             &service.mObject);
        if (service.mObject) {
            cRZString text;
            text.Construct(name);
            VirtualSlot<void(__thiscall *)(void *, void *)>(service.mObject, 0x20)(service.mObject, &text);
        }
        service.ReleaseObject();
        return 0;
    }
    cITSVisualEffect *effect = AcquireEffect();
    void *face = reinterpret_cast<uint8_t *>(effect) + 4;
    cITSEffectsManager *owner = this;
    VirtualSlot<void(__thiscall *)(void *, void *)>(face, 0)(face, owner);
    auto *fields = reinterpret_cast<uint8_t *>(effect);
    std::string &effectName = *reinterpret_cast<std::string *>(fields + 0x14);
    effectName.assign(mScratch.c_str(), mScratch.size());
    *reinterpret_cast<int32_t *>(fields + 0x34) = index;
    if (index >= 0) {
        OriginalFunction<void(__thiscall *)(void *, void *), 0xbe9670>()(effect, &mEffects[index]);
        FlushPending();
        ScopedRef service{nullptr};
        if (void *framework = GetServiceObject())
            VirtualSlot<bool(__thiscall *)(void *, uint32_t, uint32_t, void **)>(framework, 5)(framework, 0x8d127822, 0x6d1922a7,
                                                                                             &service.mObject);
        if (service.mObject) {
            cRZString text;
            text.Construct(name);
            VirtualSlot<void(__thiscall *)(void *, void *)>(service.mObject, 0x21)(service.mObject, &text);
        }
        service.ReleaseObject();
    }
    *out = effect;
    effect->AddRef();
    return 1;
}

// ---- loading ----

// 0xbc9840: the collection emptied, then every effects resource the resource manager lists (its slot 19 for the keys of type
// 0xea5118b0, sorted by instance) loaded into it (an instance 1 sets the byte at +0x8c), the parser given the files of the
// first path list (type 5; the byte set) and the existing ones of the second (type 4), the parser's state applied, its message
// sent, the renderer told (slot 6) and the collection linked.
void cTSEffectsManager::LoadEffects()
{
    Clear();
    RZAutoRef manager{nullptr};
    if (void *framework = GetServiceObject())
        VirtualSlot<bool(__thiscall *)(void *, uint32_t, uint32_t, void **)>(framework, 5)(framework, 0x56b906e, 0x656b8efc,
                                                                                         &manager.mObject);
    if (manager.mObject) {
        struct Key {
            uint32_t word[4];
        };
        ServiceBlockVector keys{};
        void *resource = nullptr;
        if (VirtualSlot<int(__thiscall *)(void *, void *, uint32_t)>(manager.mObject, 19)(manager.mObject, &keys, 0xea5118b0)) {
            auto *first = static_cast<Key *>(keys.mFirst);
            auto *last = static_cast<Key *>(keys.mLast);
            std::sort(first, last, [](const Key &a, const Key &b) { return a.word[2] < b.word[2]; });
            for (uint32_t i = 0; i < static_cast<uint32_t>(last - first); ++i) {
                const Key *key = first + i;
                if (resource) {
                    void *old = resource;
                    resource = nullptr;
                    static_cast<cIGZUnknown *>(old)->Release();
                }
                if (VirtualSlot<bool(__thiscall *)(void *, const void *, uint32_t, void **, uint32_t, uint32_t)>(
                        manager.mObject, 5)(manager.mObject, key, 0xca51189b, &resource, 0, 0)) {
                    cTSEffectsCollection *collection = this;
                    VirtualSlot<void(__thiscall *)(void *, void *)>(resource, 5)(resource, collection);
                    if (key->word[2] == 1) mByte8c = 1;
                }
            }
        }
        keys.FreeBlock();
        if (resource) static_cast<cIGZUnknown *>(resource)->Release();
    }
    void *parser = mParser.Get();
    if (!mPathsA.empty()) {
        for (const fx::PathString &path : mPathsA)
            VirtualSlot<void(__thiscall *)(void *, const char *, uint32_t)>(parser, 6)(parser, path.mText.c_str(), 5);
        mByte8c = 1;
    }
    for (const fx::PathString &path : mPathsB)
        if (cRZFile::PathExists(reinterpret_cast<const cIGZString &>(path)))
            VirtualSlot<void(__thiscall *)(void *, const char *, uint32_t)>(parser, 6)(parser, path.mText.c_str(), 4);
    ApplyParserState();
    void *object = VirtualSlot<void *(__thiscall *)(void *)>(parser, 9)(parser);
    SendText(VirtualSlot<const char *(__thiscall *)(void *)>(object, 0x2c)(object));
    VirtualSlot<void(__thiscall *)(void *)>(mRenderer.mObject, 6)(mRenderer.mObject);
    Link();
    manager.ReleaseObject();
}

// ---- lists ----

// 0xbc8760 (ret 8): the list emptied, then a row for each live, shown active effect that has an effect description (the byte
// at +0x38 clear, the one at +0x3b set, an index) and matches the filter (a wildcard on its name; none: all): its name, its
// description's variant and the effect, counted.
void cTSEffectsManager::ListActive(void *out, const char *filter)
{
    auto &list = *static_cast<std::vector<fx::EffectListEntry> *>(out);
    list.clear();
    for (uint32_t i = 0; i < mActive.size(); ++i) {
        cITSVisualEffect *effect = mPool[mActive[i]].Get();
        if (VisualEffectFields::Byte(effect, 0x38) != 0 || VisualEffectFields::Byte(effect, 0x3b) == 0 ||
            VisualEffectFields::EffectIndex(effect) < 0)
            continue;
        const std::string &name = *reinterpret_cast<const std::string *>(reinterpret_cast<const uint8_t *>(effect) + 0x14);
        if (filter && !RZWildcard::MatchCase(name.c_str(), filter, 0)) continue;
        fx::EffectListEntry entry;
        entry.mName.mText.assign(name.c_str(), name.size());
        entry.mVariant = mEffects[VisualEffectFields::EffectIndex(effect)].mVariant;
        if (entry.mObject != effect) {
            effect->AddRef();
            cIGZUnknown *old = entry.mObject;
            entry.mObject = effect;
            if (old) old->Release();
        }
        list.push_back(entry);
    }
}

// 0xbc8900 (ret 8): the list emptied, then a row for each named effect description (in the name map's order) matching the
// filter: the name, the description's index and variant, and with the flag bit 1 the name the renderer (its slot 5) gives it.
void cTSEffectsManager::ListEffects(void *out, const char *filter)
{
    auto &list = *static_cast<std::vector<fx::EffectListEntry> *>(out);
    list.clear();
    for (const auto &named : mEffectsByName) {
        if (filter && !RZWildcard::MatchCase(named.first.c_str(), filter, 0)) continue;
        fx::EffectListEntry entry;
        entry.mName.mText.assign(named.first.c_str(), named.first.size());
        entry.mIndex = static_cast<int32_t>(named.second);
        entry.mVariant = mEffects[named.second].mVariant;
        if ((mFlags >> 1) & 1) {
            void *renderer = mRenderer.mObject;
            const char *resolved = VirtualSlot<const char *(__thiscall *)(void *, const char *, uint32_t)>(renderer, 5)(
                renderer, entry.mName.mText.c_str(), entry.mVariant);
            if (resolved) entry.mResolved.mText.assign(resolved, std::strlen(resolved));
        }
        list.push_back(entry);
    }
}

// ---- messages ----

namespace {

// The option messages' target: the word (or byte) the options' value is stored to, and whether the message carried one.
template <class T> bool StoreOption(cIGZMessage2Standard *message, uint32_t rva, bool &changed)
{
    if (message->HasData2()) {
        if (const uint8_t *data = static_cast<const uint8_t *>(message->GetVoid2())) {
            *GameAddress<T>(rva) = *reinterpret_cast<const T *>(data + 4);
            changed = true;
        }
    }
    return false;
}

} // namespace

// 0xbc66d0 (this = the message target face): a message with a trigger starts its effect (a message trigger names an effect: made
// with CreateVisualEffect, placed at the centre of the first view's rectangle on its ground height (translation only) and
// started) and is then handled by id: 0x697d01ed reloads; the debug messages set their group of debug bits from the data word
// (one bit for each value); the option messages (0x6bf17c99 to 0x6bf17ca3) store the value they carry in the option's global
// and flag the options changed (for Update). 1 when handled as reload or debug bits, else 0.
bool cTSEffectsManager::DoMessage(void *messagePointer)
{
    auto *message = static_cast<cIGZMessage2Standard *>(messagePointer);
    const uint32_t id = message->GetType();
    if (!mTriggers.empty()) {
        const auto trigger = mTriggers.find(id);
        if (trigger != mTriggers.end()) {
            void *effect = nullptr;
            if (CreateVisualEffect(mMessageTriggers[trigger->second].mEffect.c_str(), &effect)) {
                void *view = GetBCC();
                void *object = VirtualSlot<void *(__thiscall *)(void *, int)>(view, 7)(view, 0);
                float x = 0.0f, y = 0.0f, z = 0.0f;
                if (object) {
                    float first = 0.0f, second = 0.0f, third = 0.0f, fourth = 0.0f;
                    VirtualSlot<void(__thiscall *)(void *, float *, float *, float *, float *)>(object, 3)(object, &first, &second,
                                                                                                      &third, &fourth);
                    y = (second + fourth) * 0.5f;
                    x = (first + third) * 0.5f;
                    z = VirtualSlot<float(__thiscall *)(void *, float, float)>(object, 5)(object, x, y);
                }
                fx::EffectsTransform placement{};
                placement.Reset();
                placement.flags |= fx::EffectsTransform::kTranslated;
                ++placement.Version();
                placement.translation.x = x;
                placement.translation.y = y;
                placement.translation.z = z;
                VirtualSlot<void(__thiscall *)(void *, const void *)>(effect, 10)(effect, &placement);
                VirtualSlot<void(__thiscall *)(void *, uint32_t)>(effect, 3)(effect, 0);
            }
            if (effect) static_cast<cIGZUnknown *>(effect)->Release();
        }
    }
    switch (id) {
    case 0x697d01ed:
        Reload();
        return true;
    case 0x122ced69:
        SetDebugBit(4, message->GetData1() == 1);
        SetDebugBit(5, message->GetData1() == 2);
        SetDebugBit(6, message->GetData1() == 3);
        return true;
    case 0x322ced70:
        SetDebugBit(7, message->GetData1() == 1);
        SetDebugBit(8, message->GetData1() == 2);
        SetDebugBit(9, message->GetData1() == 3);
        return true;
    case 0x522ced95:
        SetDebugBit(0x10, message->GetData1() == 1);
        SetDebugBit(0x11, message->GetData1() == 2);
        return true;
    case 0xb23b7d6f:
        SetDebugBit(0x12, message->GetData1() == 1);
        return true;
    case 0xd23b7d7c:
        SetDebugBit(10, message->GetData1() == 1);
        return true;
    case 0xb23b7d66:
        SetDebugBit(0xc, message->GetData1() == 0);
        SetDebugBit(0xd, message->GetData1() == 1);
        SetDebugBit(0xe, message->GetData1() == 2);
        SetDebugBit(0xf, message->GetData1() == 3);
        return true;
    case 0x6bf17c99: return StoreOption<uint32_t>(message, 0x107d504, mByteA58);
    case 0x6bf17c9a: return StoreOption<uint32_t>(message, 0x107d510, mByteA58);
    case 0x6bf17c9b: return StoreOption<uint32_t>(message, 0x107d51c, mByteA58);
    case 0x6bf17c9c: return StoreOption<uint32_t>(message, 0x107d528, mByteA58);
    case 0x6bf17c9d: return StoreOption<uint32_t>(message, 0x107d534, mByteA58);
    case 0x6bf17c9e: return StoreOption<uint32_t>(message, 0x107d540, mByteA58);
    case 0x6bf17c9f: return StoreOption<uint32_t>(message, 0x107d54c, mByteA58);
    case 0x6bf17ca0: return StoreOption<uint8_t>(message, 0x107d558, mByteA58);
    case 0x6bf17ca1: return StoreOption<uint32_t>(message, 0x107d564, mByteA58);
    case 0x6bf17ca2: return StoreOption<uint8_t>(message, 0x107d570, mByteA58);
    case 0x6bf17ca3: return StoreOption<uint8_t>(message, 0x107d57c, mByteA58);
    default: return false;
    }
}

// ---- cITSEffectsManager ----

// 0xbca680 (ret 8): both path lists emptied, then each non-null path put into its list.
void cTSEffectsManager::SetPaths(const char *first, const char *second)
{
    mPathsA.clear();
    mPathsB.clear();
    cTSEffectsManager::AddPaths(first, second);
}

// 0xbc5c10 (ret 8): each non-null path put at the front of its list (a cRZString of it copied in, 0xbcc740).
void cTSEffectsManager::AddPaths(const char *first, const char *second)
{
    if (first) {
        const fx::PathString path(first);
        mPathsA.push_front(path);
    }
    if (second) {
        const fx::PathString path(second);
        mPathsB.push_front(path);
    }
}

// 0xbca7c0 (ret 8): bits 0-2 of the flags.
void cTSEffectsManager::SetFlag(uint32_t bit, bool on)
{
    if (bit >= 3) return;
    const uint32_t mask = 1u << (bit & 31);
    if (on)
        mFlags |= mask;
    else
        mFlags &= ~mask;
}

// 0xbc86c0 (ret 4)
bool cTSEffectsManager::GetFlag(uint32_t bit)
{
    if (bit >= 3) return false;
    return (mFlags & (1u << (bit & 31))) != 0;
}

// 0xbc5b20 (ret 4): every resource key's resource (the resource manager's slot 5, interface 0xca51189b) parses itself into
// the collection face (its slot 5).
void cTSEffectsManager::ParseResources(const void *keys)
{
    struct KeyVector {
        const uint8_t *first;
        const uint8_t *last;
    };
    const auto *vector = static_cast<const KeyVector *>(keys);
    uint32_t count = static_cast<uint32_t>((vector->last - vector->first) >> 4);
    if (count == 0) return;
    RZAutoRef manager{nullptr};
    if (void *framework = GetServiceObject())
        VirtualSlot<bool(__thiscall *)(void *, uint32_t, uint32_t, void **)>(framework, 5)(framework, 0x56b906e, 0x656b8efc,
                                                                                         &manager.mObject);
    uint32_t offset = 0;
    do {
        void *resource = nullptr;
        if (VirtualSlot<bool(__thiscall *)(void *, const void *, uint32_t, void **, uint32_t, uint32_t)>(manager.mObject, 5)(
                manager.mObject, vector->first + offset, 0xca51189b, &resource, 0, 0)) {
            cTSEffectsCollection *collection = this;
            VirtualSlot<void(__thiscall *)(void *, void *)>(resource, 5)(resource, collection);
        }
        if (resource) static_cast<cIGZUnknown *>(resource)->Release();
        offset += 0x10;
    } while (--count != 0);
    manager.ReleaseObject();
}

// 0xbc8e20 (ret 4): whether an effect of the name (lower-cased into the scratch key) has the 16-bit variant.
bool cTSEffectsManager::HasEffectVariant(const void *nameAndVariant)
{
    const auto *name = static_cast<const cRZString *>(nameAndVariant);
    if (&mScratch != &name->mString) mScratch.assign(name->mString.data(), name->mString.size());
    LowerInPlace(mScratch);
    const int16_t variant = *reinterpret_cast<const int16_t *>(static_cast<const uint8_t *>(nameAndVariant) + 0x20);
    const auto range = mEffectsByName.equal_range(mScratch);
    int32_t index = -1;
    for (auto it = range.first; it != range.second; ++it) {
        if (static_cast<int16_t>(mEffects[it->second].mVariant) == variant) {
            index = static_cast<int32_t>(it->second);
            break;
        }
    }
    return index != -1;
}

// 0xbc8f00 (ret 4): false for a null or empty name; else whether an effect has the name (lower-cased).
bool cTSEffectsManager::HasEffect(const char *name)
{
    if (!name || *name == 0) return false;
    SetScratchLower(name);
    return HasEffectName(mScratch);
}

// 0xbc8d90 (this = the collection face): whether the effects map has the key.
bool cTSEffectsManager::HasEffectName(const std::string &name)
{
    return mEffectsByName.find(name) != mEffectsByName.end();
}

// 0xbca860 (ret 4): state 1 drops every effect; entering or leaving 3 hides and suspends (slots 8, 7) every pool effect,
// entering or leaving 2 suspends them.
void cTSEffectsManager::SetState(int32_t state)
{
    if (state == mState) return;
    if (state == 1) {
        ReleaseAllEffects();
        mState = 1;
        return;
    }
    if (state == 3 || mState == 3) {
        for (auto it = mPool.begin(); it != mPool.end(); ++it) {
            it->Get()->SetHidden(state == 3);
            it->Get()->SetSuspended(state == 3);
        }
    }
    if (state == 2 || mState == 2) {
        for (auto it = mPool.begin(); it != mPool.end(); ++it) it->Get()->SetSuspended(state == 2);
    }
    mState = state;
}

// 0xbca380 (ret 4): the detail from a maximum: the first call after SetDetail (or construction) jumps to 5 when the level
// was at the top; the level becomes the maximum less the bias (at least 1), truncated.
void cTSEffectsManager::SetMaxDetail(uint32_t detail)
{
    bool forced = false;
    if (!mMaxDetail && static_cast<double>(mDetailLevel) + 1e-05 >= static_cast<double>(4.9999f)) {
        mDetail = 5;
        forced = true;
        mDetailLevel = 5.0f;
    }
    mMaxDetail = true;
    mDetailIsMax = 1;
    float level = static_cast<float>(detail) - mDetailBias;
    level = level > 1.0f ? level : 1.0f;
    const int32_t value = static_cast<int32_t>(static_cast<int64_t>(level));
    const int32_t previous = mDetail;
    if (value != previous || forced) {
        mPreviousDetailLevel = mDetailLevel;
        mPreviousDetail = previous;
        mDetail = value;
        mDetailFraction = 0.0f;
        mDetailLevel = static_cast<float>(static_cast<uint32_t>(value));
        if (value != previous) RefreshDetail();
    }
}

// 0xbca470 (ret 4): the detail level (less the bias) clamped to [1, 4.9999]; its floor is the detail, the rest the fraction.
void cTSEffectsManager::SetDetail(float detail)
{
    const float kTop = 4.9999f;
    bool forced = false;
    if (mMaxDetail && mDetailLevel > kTop) {
        mDetailLevel = kTop;
        mDetail = 4;
        forced = true;
    }
    const float value = detail - mDetailBias;
    mMaxDetail = false;
    mDetailIsMax = 0;
    float level = 1.0f;
    if (!(1.0f > value)) level = kTop < value ? kTop : value;
    if (level != mDetailLevel || forced) {
        mPreviousDetailLevel = mDetailLevel;
        const int32_t previous = mDetail;
        mDetailLevel = level;
        mPreviousDetail = previous;
        const int32_t floor = FloorToInt(level);
        mDetail = floor;
        mDetailFraction = level - static_cast<float>(floor);
        if (floor != previous) RefreshDetail();
    }
}

// 0xbca580 (ret 4)
void cTSEffectsManager::SetTimeMode(int32_t mode)
{
    mTimeMode = mode;
}

// 0xbc6f80 (ret 8): Update (slot 19) with the milliseconds in seconds.
void cTSEffectsManager::UpdateMs(uint32_t elapsedMs, uint32_t simElapsedMs)
{
    Update(static_cast<float>(elapsedMs) * 0.001f, static_cast<float>(simElapsedMs) * 0.001f);
}

// 0xbc8d80 (slots 21 and 22)
void *cTSEffectsManager::GetParams()
{
    return mParams;
}

void *cTSEffectsManager::GetParams2()
{
    return mParams;
}

// Folded 0x238f10: +0x84.
int32_t cTSEffectsManager::GetState()
{
    return mState;
}

// 0xbc7340
void *cTSEffectsManager::GetBCC()
{
    return mRefBCC.Get();
}

// 0xbca1d0 (ret 8)
void cTSEffectsManager::SetSlotObject(int32_t index, cIGZUnknown *object)
{
    mSlotObjects[index].Assign(object);
}

// 0xbc9e10 (ret 4)
cIGZUnknown *cTSEffectsManager::GetSlotObject(int32_t index)
{
    return mSlotObjects[index].Get();
}

// 0xbc9f10 (ret 8): the object under the lower-cased name (an entry made when missing), counted.
void cTSEffectsManager::SetNamedA(const char *name, cIGZUnknown *object)
{
    SetScratchLower(name);
    std::string key(mScratch.c_str());
    mNamedA[std::move(key)].Assign(object);
}

// 0xbc5df0 (ret 4): the object under the lower-cased name, null when there is none (not counted).
cIGZUnknown *cTSEffectsManager::GetNamedA(const char *name)
{
    SetScratchLower(name);
    const std::string key(mScratch.c_str());
    const auto found = mNamedA.find(key);
    if (found != mNamedA.end()) return found->second.Get();
    return nullptr;
}

// 0xbca070 (ret 8): as SetNamedA, the second map.
void cTSEffectsManager::SetNamedB(const char *name, cIGZUnknown *object)
{
    SetScratchLower(name);
    std::string key(mScratch.c_str());
    mNamedB[std::move(key)].Assign(object);
}

// 0xbc5f20 (ret 4): as GetNamedA, the second map.
cIGZUnknown *cTSEffectsManager::GetNamedB(const char *name)
{
    SetScratchLower(name);
    const std::string key(mScratch.c_str());
    const auto found = mNamedB.find(key);
    if (found != mNamedB.end()) return found->second.Get();
    return nullptr;
}

// 0xbca340 (ret 8): the wanted debug bits (Update applies them).
void cTSEffectsManager::SetDebugBit(uint32_t bit, bool on)
{
    if (bit >= 32) return;
    const uint32_t mask = 1u << bit;
    if (on)
        mWantedDebugBits |= mask;
    else
        mWantedDebugBits &= ~mask;
}

// 0xbc5d20 (ret 4): the applied debug bits.
bool cTSEffectsManager::GetDebugBit(uint32_t bit)
{
    if (bit >= 32) return false;
    return (mDebugBits & (1u << bit)) != 0;
}

// 0xbc9e80: the renderer's second virtual base, null without a renderer.
void *cTSEffectsManager::GetRendererBase()
{
    if (!mRenderer.mObject) return nullptr;
    return mRenderer.Base(2);
}

// Folded 0x148ba0: +0x18, the effects map's size.
uint32_t cTSEffectsManager::GetEffectNameCount()
{
    return static_cast<uint32_t>(mEffectsByName.size());
}

// 0xbc8cb0 (ret 4): one line of statistics through the output's slot 31 (a printf).
void cTSEffectsManager::PrintStats(void *out)
{
    const int32_t freeCount = static_cast<int32_t>(mFree.size());
    const int32_t activeCount = static_cast<int32_t>(mActive.size());
    const int32_t detail = FloorToInt(static_cast<float>(mDetail) + mDetailFraction + 0.5f);
    VirtualSlot<void(__cdecl *)(void *, const char *, ...)>(out, 31)(
        out,
        "Effects: LOD: %d Active:%d Live:%d Free:%d, tex pts: %d/%d, model pts: %d/%d, meta pts: %d, density: %f, decals: %d\n",
        detail, mWordB00, activeCount, freeCount, mCounters[3], mCounters[2], mCounters[5], mCounters[4], mCounters[6],
        static_cast<double>(mParams[0]), mCounters[7]);
}

// 0xbc8c20 (ret 0x10): up to `count` effect names (map order; those matching `filter` when given, RZWildcard::MatchCase) copied into
// consecutive `size`-byte slots of *buffer, each cut to size - 1 and terminated. Returns how many.
int32_t cTSEffectsManager::CopyEffectNames(char **buffer, int32_t count, int32_t size, const char *filter)
{
    int32_t copied = 0;
    char *out = *buffer;
    for (auto it = mEffectsByName.begin(); it != mEffectsByName.end(); ++it) {
        if (count <= copied) return copied;
        if (filter &&
            !RZWildcard::MatchCase(it->first.c_str(), filter, 0))
            continue;
        strncpy(out, it->first.c_str(), static_cast<size_t>(size - 1));
        out += size;
        ++copied;
        out[-1] = 0;
    }
    return copied;
}

// 0xbca810 (ret 4): a different scene drops every effect first.
void cTSEffectsManager::SetScene(cIGZUnknown *scene)
{
    if (mScene.Get() && mScene.Get() != scene) ReleaseAllEffects();
    mScene.Assign(scene);
}

// 0xbca920 (ret 4)
void cTSEffectsManager::SetCamera(cIGZUnknown *camera)
{
    mCamera.Assign(camera);
}

// 0xbca960 (ret 4): the camera, then (with one) every live, shown active effect re-attached (cTSVisualEffect::Follow).
void cTSEffectsManager::SetCameraAndRefresh(cIGZUnknown *camera)
{
    mCamera.Assign(camera);
    if (!mCamera.Get()) return;
    for (uint32_t i = 0; i < mActive.size(); ++i) {
        cITSVisualEffect *effect = mPool[mActive[i]].Get();
        if (VisualEffectFields::Byte(effect, 0x38) == 0 && VisualEffectFields::Byte(effect, 0x3b) != 0 &&
            (VisualEffectFields::Flags44(effect) & 1) == 0 && ((VisualEffectFields::Flags44(effect) >> 1) & 1) == 0)
            static_cast<cTSVisualEffect *>(effect)->Follow();
    }
}

// 0xbca330
cIGZUnknown *cTSEffectsManager::GetScene()
{
    return mScene.Get();
}

// 0xbcbde0
cIGZUnknown *cTSEffectsManager::GetCamera()
{
    return mCamera.Get();
}

// 0xbca250: with the debug setting at 0x107d57c on, a value from the service 0x8af76dc8 (its object's slot 7 for the pair
// its slot 5 gave once); else -1.
int32_t cTSEffectsManager::GetDebugValue()
{
    ScopedRef service{nullptr};
    if (void *framework = GetServiceObject())
        VirtualSlot<bool(__thiscall *)(void *, uint32_t, uint32_t, void **)>(framework, 5)(framework, 0x8af76dc8, 0x4af76d8c,
                                                                                         &service.mObject);
    void *object = VirtualSlot<void *(__thiscall *)(void *)>(service.mObject, 5)(service.mObject);
    service.ReleaseObject();
    if (*GameAddress<uint8_t>(0x107d57c) != 0 && object) {
        if (mDebugValue[0] == -1) {
            int32_t pair[2];
            const int32_t *got =
                VirtualSlot<const int32_t *(__thiscall *)(void *, int32_t *, int32_t, int32_t, int32_t)>(object, 5)(object, pair,
                                                                                                                 1, 1, 4);
            const int32_t second = got[1];
            mDebugValue[0] = got[0];
            mDebugValue[1] = second;
        }
        return VirtualSlot<int32_t(__thiscall *)(void *, int32_t, int32_t)>(object, 7)(object, mDebugValue[0], mDebugValue[1]);
    }
    return -1;
}

// ---- the collection face ----

// 0xbc5d50: the message triggers unregistered (the server's slot 6), the alias and trigger maps emptied, then the
// collection's Clear.
void cTSEffectsManager::Clear()
{
    cIGZUnknown *server = MessageServer();
    if (server && !mMessageTriggers.empty()) {
        cIGZMessageTarget2 *target = this;
        for (uint32_t i = 0; i < mMessageTriggers.size(); ++i)
            VirtualSlot<bool(__thiscall *)(void *, void *, uint32_t)>(server, 6)(server, target, mMessageTriggers[i].mMessage);
    }
    mAliases.clear();
    mTriggers.clear();
    cTSEffectsCollection::Clear();
}

// 0xbc73d0: each alias's message pair maps to every effect of its name (the first mapping kept); each trigger's message to
// its index (the last kept); then every trigger's message registered with the server (slot 5).
void cTSEffectsManager::Link()
{
    cTSEffectsCollection::Link();
    for (uint32_t i = 0; i < mMessageAliases.size(); ++i) {
        const EffectMessageAlias &alias = mMessageAliases[i];
        const auto range = mEffectsByName.equal_range(alias.mEffect);
        for (auto it = range.first; it != range.second; ++it)
            mAliases.emplace(fx::AliasKey{alias.mFirst, alias.mSecond}, it->second);
    }
    for (uint32_t i = 0; i < mMessageTriggers.size(); ++i) mTriggers[mMessageTriggers[i].mMessage] = i;
    cIGZUnknown *server = MessageServer();
    if (server && !mMessageTriggers.empty()) {
        cIGZMessageTarget2 *target = this;
        for (uint32_t i = 0; i < mMessageTriggers.size(); ++i)
            VirtualSlot<bool(__thiscall *)(void *, void *, uint32_t)>(server, 5)(server, target, mMessageTriggers[i].mMessage);
    }
}

// ---- non-virtual members ----

// 0xbcb1a0: every pool effect told (its +4 face's slot 1) and released, the active and free lists and the pool emptied.
void cTSEffectsManager::ReleaseAllEffects()
{
    for (auto it = mPool.begin(); it != mPool.end(); ++it) {
        void *face = reinterpret_cast<uint8_t *>(it->Get()) + 4;
        VirtualSlot<void(__thiscall *)(void *)>(face, 1)(face);
        it->Reset();
    }
    mActive.clear();
    mFree.clear();
    mPool.clear();
}

// 0xbcb210: with a scene, the detail applied to every active effect (0xbcb4c0), then the pending effects activated.
void cTSEffectsManager::RefreshDetail()
{
    if (!mScene.Get()) return;
    for (auto it = mActive.begin(); it != mActive.end(); ++it) ApplyDetail(this, mPool[*it].Get(), 0, 0);
    FlushPending();
}

// 0xbc59d0: unless held, the pending effects move to the active list (at most 10 rounds), each activated (0xbcb4c0).
void cTSEffectsManager::FlushPending()
{
    if (mNoFlush) return;
    int32_t rounds = 10;
    while (!mPending.empty()) {
        if (rounds < 1) return;
        const size_t start = mActive.size();
        mActive.insert(mActive.end(), mPending.begin(), mPending.end());
        mPending.clear();
        for (auto it = mActive.begin() + start; it != mActive.end(); ++it) ApplyDetail(this, mPool[*it].Get(), 1, 0);
        --rounds;
    }
}

// 0xbc6670: when the parser has changes (its slot 10), the effects are cleared, reloaded (ApplyParserState) and
// linked, the detail refreshed, and the renderer told (its slot 6).
void cTSEffectsManager::Reload()
{
    if (!mParser.Get()) return;
    if (!VirtualSlot<bool(__thiscall *)(void *)>(mParser.Get(), 10)(mParser.Get())) return;
    Clear();
    ApplyParserState();
    Link();
    mReloading = true;
    RefreshDetail();
    mReloading = false;
    VirtualSlot<void(__thiscall *)(void *)>(mRenderer.mObject, 6)(mRenderer.mObject);
}

// 0xbc6e10: the settings read again from the game's option globals (the parameters, the detail bias, the mode word, the
// elapsed-time cap), the state followed from the 0x107d570 option, the detail refreshed when the mode word changed, and
// a pending debug value handed back to its service (slot 6) when the 0x107d57c option is off.
void cTSEffectsManager::ApplySettings()
{
    mParams[0] = *GameAddress<float>(0x107d510);
    mParams[1] = *GameAddress<float>(0x107d51c);
    mParams[2] = 10000.0f;
    const int32_t previous = mWordA5C;
    mDetailBias = UnsignedToFloat(*GameAddress<uint32_t>(0x107d540));
    mWordA5C = *GameAddress<int32_t>(0x107d54c);
    mWordA64 = *GameAddress<uint8_t>(0x107d558) == 0 ? 1 : 0;
    mMaxElapsed = *GameAddress<float>(0x107d564);
    if (*GameAddress<uint8_t>(0x107d570) == 0)
        SetState(1);
    else if (mState == 1)
        SetState(0);
    if (previous != mWordA5C) {
        mReloading = true;
        RefreshDetail();
        mReloading = false;
    }
    if (mDebugValue[0] != -1 && *GameAddress<uint8_t>(0x107d57c) == 0) {
        ScopedRef service{nullptr};
        if (void *framework = GetServiceObject())
            VirtualSlot<bool(__thiscall *)(void *, uint32_t, uint32_t, void **)>(framework, 5)(framework, 0x8af76dc8,
                                                                                             0x4af76d8c, &service.mObject);
        void *object = VirtualSlot<void *(__thiscall *)(void *)>(service.mObject, 5)(service.mObject);
        service.ReleaseObject();
        if (object)
            VirtualSlot<void(__thiscall *)(void *, int32_t, int32_t)>(object, 6)(object, mDebugValue[0], mDebugValue[1]);
        mDebugValue[0] = -1;
    }
    mByteA58 = false;
}

// 0xbcb3f0: the first parameter follows the ratio of a target (0x107d528) to the measured count (mWordAFC): above the
// target it shrinks by the blend (0x107d534); below, it grows toward the cap (0x107d510) by the same blend.
void cTSEffectsManager::CounterBlend()
{
    const float target = UnsignedToFloat(*GameAddress<uint32_t>(0x107d528));
    const float measured = UnsignedToFloat(mWordAFC);
    const float blend = *GameAddress<float>(0x107d534);
    const float cap = *GameAddress<float>(0x107d510);
    if (target < measured) {
        mParams[0] = (((1.0f - blend) * target) / measured + blend) * mParams[0];
        return;
    }
    if (mParams[0] < cap) {
        const float next = ((1.0f - blend) * target + measured * blend) * mParams[0];
        if (next < measured * cap)
            mParams[0] = next / measured;
        else
            mParams[0] = cap;
    }
}

// 0xbc6560: with flushing held, the active effects that are not done (0xbe8de0, not ours yet) are kept and counted in
// mWordB00; a done effect held by the pool alone (count 1) is told (its +4 face's slot 1) and its index goes to the free
// list; one held elsewhere stays active, uncounted. Then the pending effects are activated.
void cTSEffectsManager::ActivityWalk()
{
    mWordB00 = 0;
    mNoFlush = true;
    size_t kept = 0;
    for (size_t i = 0; i < mActive.size(); ++i) {
        cITSVisualEffect *effect = mPool[mActive[i]].Get();
        if (!OriginalFunction<bool(__thiscall *)(void *), 0xbe8de0>()(effect)) {
            ++mWordB00;
            mActive[kept++] = mActive[i];
            continue;
        }
        effect = mPool[mActive[i]].Get();
        if (VisualEffectFields::RefCount(effect) != 1) {
            mActive[kept++] = mActive[i];
            continue;
        }
        void *face = reinterpret_cast<uint8_t *>(effect) + 4;
        VirtualSlot<void(__thiscall *)(void *)>(face, 1)(face);
        mFree.push_back(mActive[i]);
    }
    if (kept != mActive.size()) mActive.erase(mActive.begin() + kept, mActive.end());
    mNoFlush = false;
    FlushPending();
}

// 0xbc6fe0 (ret 8): the settings re-read when flagged; the elapsed time capped (a positive mMaxElapsed); changed debug bits
// applied to the active effects whose description uses them (then the pending ones activated); with a camera and the
// renderer object, the camera's view vector (from the renderer's size) given to the effects that want it; each shown,
// live active effect updated with the time its flags choose (the capped one, the simulation's, or the mode's), the counters
// collected; then the counter blend and the activity walk.
// The original also normalises the view vector (unless its squared length is tiny) and never uses the result: effects get
// the raw one. The dead normalisation is not reproduced (it has no effect).
void cTSEffectsManager::Update(float elapsed, float simElapsed)
{
    if (mByteA58) ApplySettings();
    float capped;
    if (mMaxElapsed > 0.0f)
        capped = mMaxElapsed < elapsed ? mMaxElapsed : elapsed;
    else
        capped = elapsed;
    const uint32_t changed = mDebugBits ^ mWantedDebugBits;
    if (changed != 0) {
        const uint32_t previousBits = mDebugBits;
        mDebugBits = mWantedDebugBits;
        mPreviousDebugBits = previousBits;
        const size_t count = mActive.size();
        for (size_t i = 0; i < count; ++i) {
            cITSVisualEffect *effect = mPool[mActive[i]].Get();
            const int32_t index = VisualEffectFields::EffectIndex(effect);
            if (index != -1 && (mEffects[index].mParams[0] & changed) != 0) ApplyDetail(this, effect, 0, changed);
        }
        mPreviousDebugBits = mDebugBits;
        if (!mPending.empty()) FlushPending();
    }
    bool haveView = false;
    float view[3] = {0.0f, 0.0f, 0.0f};
    if (void *sizeSource = mRefC18.Get()) {
        int32_t width, height;
        VirtualSlot<void(__thiscall *)(void *, int32_t *, int32_t *)>(sizeSource, 34)(sizeSource, &width, &height);
        if (void *camera = mCamera.Get()) {
            const float point[3] = {static_cast<float>(width), static_cast<float>(height), 0.0f};
            float result[3];
            const float *got = VirtualSlot<const float *(__thiscall *)(void *, float *, const float *, uint32_t)>(camera, 144)(
                camera, result, point, 0);
            view[0] = got[0];
            view[1] = got[1];
            view[2] = got[2];
            haveView = true;
        }
    }
    const float timeValue = mTimeMode == 1 ? capped : simElapsed;
    std::memset(mCounters, 0, sizeof(mCounters));
    for (size_t i = 0; i < mActive.size(); ++i) {
        const uint32_t poolIndex = mActive[i];
        cITSVisualEffect *effect = mPool[poolIndex].Get();
        if (VisualEffectFields::Byte(effect, 0x38) != 0 || VisualEffectFields::Byte(effect, 0x3b) == 0) continue;
        if ((VisualEffectFields::Flags44(effect) & 1) == 0) {
            if (haveView && ((VisualEffectFields::DescriptionFlags(effect) >> 7) & 1) != 0)
                VirtualSlot<bool(__thiscall *)(void *, uint32_t, const float *, uint32_t)>(effect, 12)(effect, 9, view, 2);
            effect = mPool[poolIndex].Get();
            void *face = reinterpret_cast<uint8_t *>(effect) + 4;
            const uint32_t descriptionFlags = VisualEffectFields::DescriptionFlags(effect);
            float used;
            if (((descriptionFlags >> 11) & 1) != 0)
                used = capped;
            else if (((descriptionFlags >> 12) & 1) != 0)
                used = simElapsed;
            else
                used = timeValue;
            VirtualSlot<void(__thiscall *)(void *, float, void *)>(face, 5)(face, used, mCounters);
        } else if (VisualEffectFields::Byte(effect, 0x3a) != 0) {
            void *face = reinterpret_cast<uint8_t *>(effect) + 4;
            VirtualSlot<void(__thiscall *)(void *, float, void *)>(face, 5)(face, 0.0f, mCounters);
        }
    }
    mWordAFC = mCounters[4] + mCounters[2];
    CounterBlend();
    ActivityWalk();
}

// ---- adapters (RVA hooks) ----

using Adapters = cTSEffectsManagerAdapters;
using Mgr = cTSEffectsManager;

Mgr *__fastcall Adapters::Construct(Self *memory, void *)
{
    return ::new (memory) cTSEffectsManager;
}

void __fastcall Adapters::Destruct(Self *self, void *)
{
    self->Mgr::~Mgr();
}

Mgr *__fastcall Adapters::DeletingDestructor(Self *self, void *, uint32_t flags)
{
    self->Mgr::~Mgr();
    if (flags & 1) ::operator delete(self, sizeof(Mgr));
    return self;
}

#define IT_ADAPT(ret, name, params, args)                                                                                   \
    ret __fastcall Adapters::name params { [[msvc::forceinline_calls]] return self->Mgr::name args; }

bool __fastcall Adapters::QueryInterface(Self *self, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return self->Mgr::QueryInterface(riid, out);
}
uint32_t __fastcall Adapters::Release(Self *self, void *)
{
    [[msvc::forceinline_calls]] return self->Mgr::Release();
}
IT_ADAPT(bool, Init, (Self * self, void *), ())
IT_ADAPT(bool, Shutdown, (Self * self, void *), ())
IT_ADAPT(void, SetPaths, (Self * self, void *, const char *first, const char *second), (first, second))
IT_ADAPT(void, AddPaths, (Self * self, void *, const char *first, const char *second), (first, second))
IT_ADAPT(void, SetFlag, (Self * self, void *, uint32_t bit, bool on), (bit, on))
IT_ADAPT(bool, GetFlag, (Self * self, void *, uint32_t bit), (bit))
IT_ADAPT(void, ParseResources, (Self * self, void *, const void *keys), (keys))
IT_ADAPT(bool, HasEffectVariant, (Self * self, void *, const void *nameAndVariant), (nameAndVariant))
IT_ADAPT(bool, HasEffect, (Self * self, void *, const char *name), (name))
IT_ADAPT(void, SetState, (Self * self, void *, int32_t state), (state))
IT_ADAPT(void, SetMaxDetail, (Self * self, void *, uint32_t detail), (detail))
IT_ADAPT(void, SetDetail, (Self * self, void *, float detail), (detail))
IT_ADAPT(void, SetTimeMode, (Self * self, void *, int32_t mode), (mode))
IT_ADAPT(void, UpdateMs, (Self * self, void *, uint32_t elapsedMs, uint32_t simElapsedMs), (elapsedMs, simElapsedMs))
IT_ADAPT(void *, GetParams, (Self * self, void *), ())
IT_ADAPT(void *, GetBCC, (Self * self, void *), ())
IT_ADAPT(void, SetSlotObject, (Self * self, void *, int32_t index, cIGZUnknown *object), (index, object))
IT_ADAPT(cIGZUnknown *, GetSlotObject, (Self * self, void *, int32_t index), (index))
IT_ADAPT(void, SetNamedA, (Self * self, void *, const char *name, cIGZUnknown *object), (name, object))
IT_ADAPT(cIGZUnknown *, GetNamedA, (Self * self, void *, const char *name), (name))
IT_ADAPT(void, SetNamedB, (Self * self, void *, const char *name, cIGZUnknown *object), (name, object))
IT_ADAPT(cIGZUnknown *, GetNamedB, (Self * self, void *, const char *name), (name))
IT_ADAPT(void, SetDebugBit, (Self * self, void *, uint32_t bit, bool on), (bit, on))
IT_ADAPT(bool, GetDebugBit, (Self * self, void *, uint32_t bit), (bit))
IT_ADAPT(void *, GetRendererBase, (Self * self, void *), ())
IT_ADAPT(void, PrintStats, (Self * self, void *, void *out), (out))
IT_ADAPT(int32_t, CopyEffectNames, (Self * self, void *, char **buffer, int32_t count, int32_t size, const char *filter),
         (buffer, count, size, filter))
IT_ADAPT(void, SetScene, (Self * self, void *, cIGZUnknown *scene), (scene))
IT_ADAPT(void, SetCamera, (Self * self, void *, cIGZUnknown *camera), (camera))
IT_ADAPT(void, SetCameraAndRefresh, (Self * self, void *, cIGZUnknown *camera), (camera))
IT_ADAPT(cIGZUnknown *, GetScene, (Self * self, void *), ())
IT_ADAPT(cIGZUnknown *, GetCamera, (Self * self, void *), ())
IT_ADAPT(int32_t, GetDebugValue, (Self * self, void *), ())
IT_ADAPT(void, ReleaseAllEffects, (Self * self, void *), ())
IT_ADAPT(void, RefreshDetail, (Self * self, void *), ())
IT_ADAPT(void, FlushPending, (Self * self, void *), ())
IT_ADAPT(void, Reload, (Self * self, void *), ())
IT_ADAPT(void, ApplySettings, (Self * self, void *), ())
IT_ADAPT(void, CounterBlend, (Self * self, void *), ())
IT_ADAPT(void, ActivityWalk, (Self * self, void *), ())
IT_ADAPT(void, Update, (Self * self, void *, float elapsed, float simElapsed), (elapsed, simElapsed))
IT_ADAPT(void, LoadEffects, (Self * self, void *), ())
IT_ADAPT(bool, CreateEffect, (Self * self, void *, uint32_t first, uint32_t second, void **out), (first, second, out))
IT_ADAPT(bool, CreateVisualEffect, (Self * self, void *, const char *name, void **out), (name, out))
IT_ADAPT(void, ListActive, (Self * self, void *, void *out, const char *filter), (out, filter))
IT_ADAPT(void, ListEffects, (Self * self, void *, void *out, const char *filter), (out, filter))
IT_ADAPT(cITSVisualEffect *, AcquireEffect, (Self * self, void *), ())
IT_ADAPT(int32_t, FindEffectIndex, (Self * self, void *, const std::string &key), (key))
IT_ADAPT(int32_t, FindAliasIndex, (Self * self, void *, uint32_t first, uint32_t second), (first, second))
IT_ADAPT(void, ApplyParserState, (Self * self, void *), ())
#undef IT_ADAPT

bool __fastcall Adapters::DoMessage(Face4 *face, void *, void *message)
{
    [[msvc::forceinline_calls]] return static_cast<Mgr *>(face)->Mgr::DoMessage(message);
}

void __fastcall Adapters::Clear(Face8 *face, void *)
{
    [[msvc::forceinline_calls]] static_cast<Mgr *>(face)->Mgr::Clear();
}

void __fastcall Adapters::Link(Face8 *face, void *)
{
    [[msvc::forceinline_calls]] static_cast<Mgr *>(face)->Mgr::Link();
}

bool __fastcall Adapters::HasEffectName(Face8 *face, void *, const std::string &name)
{
    [[msvc::forceinline_calls]] return static_cast<Mgr *>(face)->Mgr::HasEffectName(name);
}
