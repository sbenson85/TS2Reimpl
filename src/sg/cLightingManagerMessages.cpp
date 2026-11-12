// nTSSG::cLightingManager's message target (the +4 face, cIGZMessageTarget2) slot 3, DoMessage 0x2d44b0 (lane 261110s,
// scenegraph LG3e). See sg/cLightingManager.h.
//
// Still the game's, called through OriginalFunction: the state service getter 0x211da3 (cdecl, no arguments). The
// ambient map loader 0xb409f0 is ours (LoadAmbientMapImage, lane 261111n).
#include "sg/cLightingManager.h"
#include "sg/cLightingManagerDetail.h"

#include "game/original.h"
#include "gz/cIGZMessage2Standard.h"
#include "misc/UtilitiesE.h"
#include "rz/RZMessageServer.h"
#include "rz/Singleton_20ddfb.h"
#include "simantics/ServiceRefVector.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace nTSSG {

namespace {

// The message ids.
constexpr uint32_t kTimeOfDay = 0x8d;             // and kTimeOfDayB: the state service's slot 36 object told (slot 8)
constexpr uint32_t kTimeOfDayB = 0xb23b7d66;
constexpr uint32_t kTimeOfDayOnly = 0xd23b7d7c;   // the time-of-day set chosen
constexpr uint32_t kSwapHoodSet = 0xfb96149;      // the neighbourhood set swapped with its night one (own slot 19)
constexpr uint32_t kSunShadowsOff = 0xfb96150;    // mSunShadows cleared, mDirty set
constexpr uint32_t kToggleCamera = 0xfe3cf23;     // the app's view object (class not identified) toggled; then kSwapHoodSet
constexpr uint32_t kFadeOption = 0x2bf18d56;      // the option byte 0xffde34
constexpr uint32_t kFirstOption = 0x2bf18d58;     // the ten option rows 0xffde40 + 0xc * (id - kFirstOption)
constexpr uint32_t kOptionCount = 10;
constexpr uint32_t kLightingResources = 0x2df90c05; // the message's object's resources of type 0xadee8d84 read
constexpr uint32_t kRoomSettings = 0x4b3bc675;    // the lighting script ran (Update posts it): the room settings read
constexpr uint32_t kRefresh = 0xeb425393;         // a world change of the kind in data word 1

// The option rows (12 bytes each: an .rdata pointer, then the value byte; app/StaticInitAny.cpp rows 174-184). Only the
// value bytes are named here.
constexpr uint32_t kFadeSwitch = 0xffde34;        // the lit object's fade
constexpr uint32_t kOptionBase = 0xffde40;        // row 0's value (0xffde40 the portal switch SelectHoodLightSet reads)
constexpr uint32_t kShadeOption = 0xffde4c;       // copied to mShadeTiles
constexpr uint32_t kMaterialOption = 0xffde58;    // the two material switches set (slot 0) or cleared (slot 1)
constexpr uint32_t kIncrementalOption = 0xffde7c; // copied to mField87
constexpr uint32_t kSceneStrengthOption = 0xffdea0; // set: the scene's objects' strengths set to 1; mFlag4D its inverse
constexpr uint32_t kShadowMaskOption = 0xffdeac;  // the third material switch's value

constexpr uint32_t kObjectsFilterVTable = 0xdb608c; // nTSSG::cTSSGObjectsFilter
constexpr uint32_t kSceneObjectFlag = 0xfa1c39ed;   // what the scene's objects are asked (their slot 25)
constexpr uint32_t kViewKind = 0xabea28c7;          // the kind (slot 5) of the view object the camera toggle handles
constexpr uint32_t kResourcesIid = 0x656b8f0c;      // asked of kLightingResources' object: its slot 19 lists keys
constexpr uint32_t kRoomSettingsIid = 0xab7cc174;   // asked of kRoomSettings' object (the file parser's base parser)
constexpr uint32_t kScriptType = 0xadee8d84;        // the resource type of the lighting scripts
constexpr uint32_t kTypeFilterVTable = 0xda5cf0;    // cGZPersistResourceKeyFilterByType (the persist code's)
constexpr uint32_t kAmbientMapType = 0x8ba01057;    // what 0xb409f0 loads a light set's ambient map as
constexpr uint32_t kSecondsToMs = 0xda4568;         // the game's 1000.0f

// The material parser's three switch names and the two values the third takes (game strings).
constexpr uint32_t kMaterialName1 = 0xdb619c;
constexpr uint32_t kMaterialName2 = 0xdb61b8;
constexpr uint32_t kMaterialName3 = 0xdb61d4;
constexpr uint32_t kValueOn = 0xda4fac;
constexpr uint32_t kValueOff = 0xda4af4;
constexpr uint32_t kHoodName = 0xdae884;            // "neighbourhood"
constexpr uint32_t kHoodNightName = 0xdae894;       // "neighborhoodnight"

constexpr int kServerSend = 4; // the message server's slot 4 (message, 0)

uint8_t &Byte(uint32_t rva) { return *GameAddress<uint8_t>(rva); }

// 0x211da3 (still the game's): the state service.
void *StateService() { return OriginalFunction<void *(__cdecl *)(), 0x211da3>()(); }

// A light set's ambient map loaded into its counted slot (LoadAmbientMapImage 0xb409f0; the result unused).
void LoadAmbientMap(const void *key, const void **slot) { LoadAmbientMapImage(kAmbientMapType, key, slot); }

// A counted pointer whose cIGZUnknown is a virtual base, through the vbptr at +8 (the base's offset in the table's
// second entry); the table's third entry finds the face the room settings are read from.
uint8_t *BaseOf(uint8_t *object, int entry)
{
    uint8_t *const vbptr = object + 8;
    return vbptr + (*reinterpret_cast<int32_t *const *>(vbptr))[entry];
}

// kRoomSettings' object's interface, released through its virtual base when it goes (on an exception too).
struct HeldSettings {
    uint8_t *mObject = nullptr;
    HeldSettings() = default;
    HeldSettings(const HeldSettings &) = delete;
    HeldSettings &operator=(const HeldSettings &) = delete;
    ~HeldSettings()
    {
        if (mObject) RawSlot<2, uint32_t>(BaseOf(mObject, 1)); // receiver: cIGZUnknown (the object's virtual base; Release)
    }
};

// A counted interface pointer released (slot 2) when it goes (on an exception too).
struct HeldUnknown {
    void *mObject = nullptr;
    HeldUnknown() = default;
    HeldUnknown(const HeldUnknown &) = delete;
    HeldUnknown &operator=(const HeldUnknown &) = delete;
    ~HeldUnknown()
    {
        if (mObject) RawSlot<2, uint32_t>(mObject); // receiver: cIGZUnknown (Release)
    }
};

// The key list kLightingResources fills, its block freed through the allocator service (0x195720) when it goes.
struct HeldKeys {
    cLightingKeyList mKeys{nullptr, nullptr, nullptr};
    HeldKeys() = default;
    HeldKeys(const HeldKeys &) = delete;
    HeldKeys &operator=(const HeldKeys &) = delete;
    ~HeldKeys() { reinterpret_cast<ServiceBlockVector *>(&mKeys)->FreeBlock(); }
};

// cGZPersistResourceKeyFilterByType as the original builds it (12 bytes, not ours: the game's vtable, its count, the type).
struct KeyFilterByType {
    const void *mVTable;
    uint32_t mRefCount;
    uint32_t mType;
};
static_assert(sizeof(KeyFilterByType) == 0xc, "KeyFilterByType layout");

// The ambient maps resized to `count` as the original's std::vector of counted pointers: a shrink releases the dropped
// maps; a growth within the capacity adds nulls; past it (the library's 0x2941e0) a block of the larger of `count` and
// one and a half times the capacity, the maps copied into it (each counted, slot 1), then the old ones released.
void ResizeMaps(std::vector<const void *> &maps, uint32_t count)
{
    const uint32_t size = static_cast<uint32_t>(maps.size());
    if (count < size) {
        for (uint32_t i = count; i < size; ++i)
            if (maps.data()[i]) RawSlot<2, uint32_t>(maps.data()[i]); // receiver: the map (cIGZUnknown Release)
        maps.resize(count);
    } else if (count > size) {
        const uint32_t capacity = static_cast<uint32_t>(maps.capacity());
        if (count <= capacity) {
            maps.resize(count);
            return;
        }
        std::vector<const void *> grown;
        grown.reserve(std::max(count, capacity + capacity / 2));
        for (const void *map : maps) {
            if (map) RawSlot<1, uint32_t>(map); // receiver: the map (cIGZUnknown AddRef)
            grown.push_back(map);
        }
        grown.resize(count);
        for (const void *map : maps)
            if (map) RawSlot<2, uint32_t>(map); // receiver: the map (cIGZUnknown Release)
        maps.swap(grown);
    }
}

} // namespace

void cLightingManager::OptionsChanged(uint8_t oldFade)
{
    mShadeTiles = Byte(kShadeOption);
    mField87 = Byte(kIncrementalOption);
    if (mSceneManager && oldFade != Byte(kFadeSwitch)) {
        if (oldFade) {
            mFlag4D = 0;
            if (mSunMode == 1) Call<6, void>(mLitObject); // receiver: the lit object, slot 6
        } else {
            mFlag4D = 1;
            if (mSunMode == 1) Call<5, void>(mLitObject); // receiver: the lit object, slot 5
        }
    }
    mDirty = 1;
    mFlag81 = 0;
    mFlag83 = 1;
}

void cLightingManager::MaterialOptionsChanged()
{
    mDirty = 1;
    mFlag83 = 1;
    void *const parser = GetMaterialParserCached();
    if (!parser) return;
    const char *const name1 = GameAddress<const char>(kMaterialName1);
    const char *const name2 = GameAddress<const char>(kMaterialName2);
    if (Byte(kMaterialOption)) {
        const char *const on = GameAddress<const char>(kValueOn);
        Call<0, void>(parser, 0u, name1, on); // receiver: the material parser (the registry), slot 0 sets a switch
        Call<0, void>(parser, 0u, name2, on);
    } else {
        Call<1, void>(parser, 0u, name1); // receiver: the material parser, slot 1 clears a switch
        Call<1, void>(parser, 0u, name2);
    }
    const char *const value = GameAddress<const char>(Byte(kShadowMaskOption) ? kValueOn : kValueOff);
    Call<0, void>(parser, 0u, GameAddress<const char>(kMaterialName3), value); // receiver: the material parser, slot 0
    // ORIGINAL-BUG: the material manager is not null-checked.
    Call<24, void>(GetCachedQueriedObject(), 0u, 0u); // receiver: the material manager (0xae8900), slot 24 (0, 0)
}

void cLightingManager::SetSceneStrengths()
{
    struct Filter {
        const void *mVTable;
    } filter = {GameAddress<const void>(kObjectsFilterVTable)};
    // ORIGINAL-BUG: mSceneManager is not null-checked here.
    nGZSceneGraph::cICompositionTreeNode *const scene = SceneOf(mSceneManager);
    NodeRange range;
    scene->MakeIterator44(&range, 0, reinterpret_cast<uint32_t>(&filter)); // receiver: the scene (cICompositionTreeNode)
    while (!range.AtEnd()) {
        void *const object = range.mCursor->Get();                         // receiver: cISceneNodeIterator
        if (RawSlot<25, bool>(object, kSceneObjectFlag))                   // receiver: the scene object (not identified)
            RawSlot<14, void>(this, range.mCursor->Get(), 0u, 1.0f, 0u);   // receiver: own slot 14 (UpdateObjectStrength)
        range.mCursor->Next();                                             // receiver: cISceneNodeIterator
    }
}

void cLightingManager::ToggleCamera(cIGZMessage2Standard *message)
{
    void *const app = GetCachedSingleton();
    void *const view = Call<23, void *>(app); // receiver: Singleton_20ddfb's object, slot 23
    if (Call<5, uint32_t>(view) != kViewKind) return; // receiver: the view object (not identified), slot 5 its kind
    void *const camera = Call<6, void *>(view);   // receiver: the view object, slot 6
    const uint32_t off = Call<20, uint32_t>(camera) == 0 ? 1u : 0u; // receiver: the camera (not identified), slot 20
    Call<19, void>(camera, off);                  // receiver: the camera, slot 19
    Call<5, void>(Call<8, void *>(app));          // receiver: Singleton_20ddfb's object slot 8's object, slot 5
    message->SetType(kSwapHoodSet);               // receiver: cIGZMessage2Standard
    // ORIGINAL-BUG: the message server is not null-checked.
    Call<kServerSend, uint32_t>(GetCachedMessageServer(), message, 0u); // receiver: the message server, slot 4
}

void cLightingManager::ReadResourcesOf(cIGZMessage2Standard *message)
{
    cIGZUnknown *const object = message->GetIGZUnknown(); // receiver: cIGZMessage2Standard
    if (!object) return;
    HeldUnknown resources;
    if (!object->QueryInterface(kResourcesIid, &resources.mObject)) return; // receiver: the message's object
    HeldKeys keys;
    auto *filter = static_cast<KeyFilterByType *>(::operator new(sizeof(KeyFilterByType)));
    if (filter) {
        filter->mRefCount = 0;
        filter->mVTable = GameAddress<void>(kTypeFilterVTable);
        filter->mType = kScriptType;
        RawSlot<1, uint32_t>(filter); // receiver: the key filter (the game's vtable, AddRef)
    }
    HeldUnknown heldFilter;
    heldFilter.mObject = filter;
    Call<19, uint32_t>(resources.mObject, &keys.mKeys, filter); // receiver: the resources interface, slot 19 lists keys
    ReadLightingResources(&keys.mKeys);
}

bool cLightingManager::ReadRoomSettingsOf(cIGZMessage2Standard *message)
{
    HeldSettings settings;
    // ORIGINAL-BUG: a message without its object is read through a null pointer.
    cIGZUnknown *const object = message->GetIGZUnknown(); // receiver: cIGZMessage2Standard
    if (!object->QueryInterface(kRoomSettingsIid, reinterpret_cast<void **>(&settings.mObject))) // receiver: the object
        return false;
    uint8_t *const source = settings.mObject;
    auto *const sets = reinterpret_cast<uint32_t *>(source + 0x1ac); // the source's light sets (a vector's three words)
    if (sets[0] != sets[1]) {
        auto *const ours = reinterpret_cast<uint32_t *>(&mLightSets);
        if (ours != sets) std::swap_ranges(ours, ours + 3, sets);
        const int32_t count = static_cast<int32_t>(mLightSets.size());
        ResizeMaps(mAmbientMaps, static_cast<uint32_t>(count));
        for (int32_t i = 0; i < count; ++i) {
            const void *const key = mLightSets.data()[i].mAmbientKey;
            const void **const slot = &mAmbientMaps.data()[i];
            if (key) {
                LoadAmbientMap(key, slot);
            } else if (const void *const old = *slot) {
                *slot = nullptr;
                RawSlot<2, uint32_t>(old); // receiver: the map (cIGZUnknown Release)
            }
        }
        mCurrentSet = 0;
        SelectHoodLightSet(mName.c_str());
    }
    ReadRoomSettings(source ? BaseOf(source, 2) : nullptr);
    if (mFlag81) {
        ApplyLightSet();
        mDirty = 1;
        mFlag83 = 1;
    }
    return true;
}

void cLightingManager::WorldChanged(uint32_t kind)
{
    if (kind == 2) {
        if (!mShadeTiles) return;
        mDirty = 1;
        mSunMapDirty = 1;
        mFlag83 = 1;
    } else {
        mDirty = 1;
        if (kind == 1 || kind == 4 || kind == 3 || kind == 5 || kind == 6 || kind == 8) {
            mSunMapDirty = 1;
            mFlag83 = 1;
        }
    }
    const float delay = (kind == 6 ? mField508 : CurrentSet().mRefreshDelay) * *GameAddress<const float>(kSecondsToMs);
    mField500 = delay > mField500 ? delay : mField500;
}

bool cLightingManager::MessageFace::DoMessage(void *data)
{
    cLightingManager *const self = Owner();
    auto *const message = static_cast<cIGZMessage2Standard *>(data);
    const uint8_t oldFade = Byte(kFadeSwitch);
    const uint8_t oldMaterial = Byte(kMaterialOption);
    const uint32_t type = message->GetType(); // receiver: cIGZMessage2Standard (the server's messages)
    const uint32_t option = type - kFirstOption;
    if (type == kFadeOption || option < kOptionCount) {
        if (!message->HasData2()) return false; // receiver: cIGZMessage2Standard
        const auto *const value = static_cast<const uint8_t *>(message->GetVoid2()); // receiver: cIGZMessage2Standard
        if (!value) return false;
        const uint32_t row = type == kFadeOption ? kFadeSwitch : kOptionBase + 0xc * option;
        Byte(row) = value[4];
        if (row == kMaterialOption) {
            if (oldMaterial == Byte(kMaterialOption)) return false;
            self->MaterialOptionsChanged();
        } else if (row == kShadowMaskOption) {
            self->MaterialOptionsChanged();
        } else {
            if (row == kSceneStrengthOption) {
                if (Byte(kSceneStrengthOption)) self->SetSceneStrengths();
                self->mFlag4D = Byte(kSceneStrengthOption) == 0;
            }
            self->OptionsChanged(oldFade);
        }
        return true;
    }
    switch (type) {
    case kSwapHoodSet:
        if (self->mInitialised) {
            const char *const name = self->mName.c_str();
            const char *const swapped = GameAddress<const char>(std::strcmp(name, GameAddress<const char>(kHoodName)) == 0
                                                                    ? kHoodNightName
                                                                    : kHoodName);
            RawSlot<19, void>(self, swapped); // receiver: own slot 19 (SelectLightSet)
        }
        return true;
    case kSunShadowsOff:
        self->mSunShadows = 0;
        self->mDirty = 1;
        return true;
    case kToggleCamera:
        if (self->mInitialised) self->ToggleCamera(message);
        return true;
    case kTimeOfDay:
    case kTimeOfDayB:
        if (void *const object = Call<36, void *>(StateService())) // receiver: the state service, slot 36
            Call<8, void>(object);                                // receiver: its object (not identified), slot 8
        self->SelectTimeOfDayLightSet();
        return true;
    case kTimeOfDayOnly:
        self->SelectTimeOfDayLightSet();
        return true;
    case kLightingResources:
        self->ReadResourcesOf(message);
        return true;
    case kRoomSettings:
        return self->ReadRoomSettingsOf(message);
    case kRefresh:
        self->WorldChanged(message->GetData1()); // receiver: cIGZMessage2Standard
        return true;
    default:
        return false;
    }
}

} // namespace nTSSG
