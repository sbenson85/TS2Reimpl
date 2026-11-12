// nTSSG::cTSSGSystem: setting up the cinematic (slot 27, lane JD). See src/sg/cTSSGSystem.h.
#include "sg/cTSSGSystem.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StringKeyParser.h"
#include "rz/cRZIniFile.h"
#include "sg/SGLocalString.h"
#include "sg/SGUtilities.h"

#include <cstring>

namespace {

template <int kSlot, typename R = uint32_t, typename... A> __forceinline R Call(const void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), kSlot>(object, args...);
}

void *VirtualBase(void *object, uint32_t vbptr, int entry)
{
    auto *at = static_cast<uint8_t *>(object) + vbptr;
    const int32_t *table = *reinterpret_cast<int32_t *const *>(at);
    return at + table[entry];
}

// Game text: the cinematic resource suffix (five characters), the part-name format, mRefB4's animated entry and the
// cinematic name that also sets the hiding flags.
constexpr uint32_t kCinematicSuffix = 0xdadff0;
constexpr uint32_t kSuffixLength = 5;
constexpr uint32_t kPartFormat = 0xdaeff0;
constexpr uint32_t kAnimatedEntry = 0xdadff8;
constexpr uint32_t kHidingName = 0xdaeff8;
// The app's feature query that must answer, the key preset, the service and the resource type of the cinematic.
constexpr uint32_t kFeature = 0x2d;
constexpr uint32_t kCinematicKey = 0x4d51f042;
constexpr uint32_t kServiceClass = 0x056b906e;
constexpr uint32_t kServiceIid = 0x9af5e924;
constexpr uint32_t kCinematicType = 0x2d51f03a;
// Interfaces: an object's owner, the actor interface (class and iid), the lot's selected-object query, mRefB4's entry.
constexpr uint32_t kOwnerIid = 0x5a6216f6;
constexpr uint32_t kActorClass = 0xfa1c39f7;
constexpr uint32_t kActorIid = 0xfa1c39ed;
constexpr uint32_t kSelectedQuery = 0xcd87f642;
constexpr uint32_t kAnimatedIid = 0xad374e09;
// The error codes written to *error.
enum : int32_t {
    kNoCinematic = 0x87,
    kNoPart = 0x88,
    kNoActorId = 0x89,
    kNoObject = 0x8a,
    kNoActor = 0x8b,
    kBadSelection = 0x8c,
    kNoSelected = 0x8d,
    kIdAndRestore = 0x8e,
    kActorRejected = 0x8f,
    kNotHeld = 0x97,
};
// The object's own slots: find an animation, play the cinematic, end it.
constexpr int kSelfFindAnimation = 26;
constexpr int kSelfPlay = 28;
constexpr int kSelfEnd = 29;

// An object's animated face, or null (0xae1ed0, ours): the actor needs one when the cinematic names a requirement for
// it, and the scene manager's camera needs one for the cinematic to be held.
void *AnimatedOf(void *object)
{
    return nGZSceneGraph::AnimatedFaceOf(static_cast<nGZSceneGraph::cICompositionTreeNode *>(object));
}

} // namespace

namespace nTSSG {

SGVirtualRef::SGVirtualRef(const SGVirtualRef &other) : mObject(other.mObject)
{
    if (mObject) Call<1>(VirtualBase(mObject, 4, 1));
}

uint8_t cTSSGSystem::SetUpCinematic(const char *name, const std::vector<int16_t> *ids, uint8_t field163,
                                    uint8_t playNow, uint8_t objectHiding, uint8_t field161, int32_t *error)
{
    *error = -1;
    void *app = GetCachedSingleton();
    if (!app || !Call<3, void *>(app)) return 0;
    if (!Call<15, uint16_t>(Call<3, void *>(app), kFeature)) return 0;
    if (mCinematic) return 0;
    mCinematicHeld = 0;

    // Every error from here on ends the half-built cinematic (slot 29, abort).
    auto fail = [&](int32_t code) -> uint8_t {
        Call<kSelfEnd>(this, 1u);
        *error = code;
        return 0;
    };

    SGLocalString text(name);
    reinterpret_cast<cRZIniStringHelpers *>(&text.value)->TrimBoth();
    if (!text.value.mString.empty()) {
        const char *suffix = GameAddress<const char>(kCinematicSuffix);
        if (!text.Helpers()->EndsWith(suffix, kSuffixLength, 1)) text.value.mString.append(suffix);
        StringKeyParser::Result key{};
        StringKeyParser::ParseKey(&key, Call<5, const char *>(&text.value), kCinematicKey, nullptr);
        void *resources = nullptr;
        if (void *service = GetServiceObject()) Call<5>(service, kServiceClass, kServiceIid, &resources);
        if (void *old = mCinematic) {
            mCinematic = nullptr;
            Call<2>(VirtualBase(old, 4, 1));
        }
        // The original asks without testing the service's answer.
        Call<5>(resources, &key, kCinematicType, &mCinematic, 0u, 0u);
        if (resources) Call<2>(resources);
    }
    if (!mCinematic) {
        *error = kNoCinematic;
        return 0;
    }

    // The parts: the cinematic's name, numbered from 1 when there are several, each an animation.
    const uint32_t parts = Call<6, uint32_t>(mCinematic);
    for (uint32_t i = 1; i <= parts; ++i) {
        SGLocalString part(Call<7, const char *>(mCinematic));
        if (parts > 1) part.value.Sprintf(GameAddress<const char>(kPartFormat), Call<7, const char *>(mCinematic), i);
        SGVirtualRef animation;
        if (!Call<kSelfFindAnimation, uint8_t>(this, part.Text(), &animation.mObject, 0u)) return fail(kNoPart);
        mCinematicRefs.push_back(animation);
    }

    // The actors: an object of the lot each, by the caller's id or the selected one, or an empty entry.
    void *lot = Call<26, void *>(app);
    const uint32_t actors = Call<16, uint32_t>(mCinematic);
    const int16_t *next = ids->data();
    const int16_t *const last = next + ids->size();
    for (uint32_t i = 0; i < actors; ++i, next = next != last ? next + 1 : next) {
        const uint8_t restore = Call<12, uint8_t>(mCinematic, i);
        const uint8_t required = Call<11, uint8_t>(mCinematic, i);
        int16_t id = next != last ? *next : 0;
        if (id > 0) {
            if (restore) return fail(kIdAndRestore);
        } else if (restore) {
            void *selection = Call<24, void *>(GetCachedSingleton());
            if (void *selected = Call<14, void *>(selection, kSelectedQuery))
                id = int16_t(Call<16, uint16_t>(lot, selected, 0u, 0u, 0u));
        } else {
            if (required) return fail(kNoActorId);
            mCinematicActors.push_back(cTSSGCinematicActor{});
            continue;
        }

        void *object = Call<29, void *>(lot, int32_t(id));
        if (!object) {
            if (required || restore) return fail(kNoObject);
            mCinematicActors.push_back(cTSSGCinematicActor{});
            continue;
        }
        if (Call<104, uint8_t>(object)) {
            void *owner = nullptr;
            Call<0>(object, kOwnerIid, &owner);
            object = Call<11, void *>(Call<6, void *>(owner));
            if (owner) Call<2>(owner);
        }
        SGVirtualRef actor;
        if (!Call<9, uint8_t>(object, kActorClass, kActorIid, &actor.mObject)) {
            if (required) return fail(kNoActor);
            cTSSGCinematicActor entry{};
            entry.mRestore = restore;
            mCinematicActors.push_back(entry);
            continue;
        }
        const char *requirement = Call<15, const char *>(mCinematic, i);
        if (*requirement && !AnimatedOf(actor.mObject)) return fail(kActorRejected);
        const uint32_t value = Call<10, uint32_t>(mCinematic, i);
        {
            const cTSSGCinematicActor entry{actor, value, restore};
            mCinematicActors.push_back(entry);
        }
        const uint32_t pieces = Call<66, uint32_t>(object);
        for (uint32_t j = 0; j < pieces; ++j) {
            void *piece = Call<69, void *>(object, j);
            if (!piece) continue;
            SGVirtualRef extra;
            if (Call<9, uint8_t>(piece, kActorClass, kActorIid, &extra.mObject)) {
                const cTSSGCinematicActor entry{extra, value, 0};
                mCinematicExtras.push_back(entry);
            }
        }
    }

    // The selected actor holds the camera.
    const uint32_t selected = Call<17, uint32_t>(mCinematic);
    if (selected >= mCinematicActors.size()) return fail(kBadSelection);
    void *chosen = mCinematicActors[selected].mObject.mObject;
    if (chosen != mCinematicRef128) {
        if (chosen) Call<1>(VirtualBase(chosen, 4, 1));
        void *old = mCinematicRef128;
        mCinematicRef128 = chosen;
        if (old) Call<2>(VirtualBase(old, 4, 1));
    }
    if (!mCinematicRef128) return fail(kNoSelected);

    // Held when the animated entry answers and the camera passes the check.
    if (mRefB4.mObject) {
        const int32_t index = Call<35, int32_t>(mRefB4.mObject, GameAddress<const char>(kAnimatedEntry));
        if (index != -1) {
            void *entry = Call<36, void *>(mRefB4.mObject, index);
            void *animated = nullptr;
            if (Call<0, uint8_t>(VirtualBase(entry, 4, 1), kAnimatedIid, &animated) &&
                Call<18, void *>(mSceneManager.mObject) && AnimatedOf(Call<18, void *>(mSceneManager.mObject))) {
                mField163 = field163;
                mObjectHiding = objectHiding;
                mField161 = field161;
                mCinematicHeld = 1;
                const char *hiding = GameAddress<const char>(kHidingName);
                mObjectHiding |= _stricmp(Call<17, const char *>(VirtualBase(mCinematic, 4, 1)), hiding) == 0;
                mField161 |= _stricmp(Call<17, const char *>(VirtualBase(mCinematic, 4, 1)), hiding) == 0;
                for (cTSSGCinematicActor &it : mCinematicActors)
                    if (it.mObject.mObject) Call<97>(it.mObject.mObject, 1u);
            }
            if (animated) Call<2>(VirtualBase(animated, 0, 1));
        }
    }

    if (mCinematicHeld && playNow) return Call<kSelfPlay, uint8_t>(this, 0u, error);
    if (!mCinematicHeld) Call<kSelfEnd>(this, 1u);
    if (!mCinematicHeld && *error == -1) *error = kNotHeld;
    return mCinematicHeld;
}

} // namespace nTSSG
