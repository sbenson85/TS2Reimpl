// nTSSG::cTSSGSystem: playing the cinematic (slot 28, lane JD). See src/sg/cTSSGSystem.h.
#include "sg/cTSSGSystem.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cFileParser.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"
#include "rz/cRZIniFile.h"
#include "rz/cRZStopwatch.h"
#include "rz/cRZString.h"
#include "sg/SGLocalString.h"
#include "sg/SGUtilities.h"

#include <bit>
#include <cstdint>
#include <utility>

namespace {

using nTSSG::cTSSGCinematicActor;
using nTSSG::cTSSGSystem;
using nTSSG::SGLocalString;
using nTSSG::SGVirtualRef;

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

// A reference released through its virtual base (table at +kVbptr, entry 1, slot 2) when it goes.
template <uint32_t kVbptr> struct BaseRef {
    void *object = nullptr;
    BaseRef() = default;
    BaseRef(const BaseRef &) = delete;
    BaseRef &operator=(const BaseRef &) = delete;
    ~BaseRef()
    {
        if (object) Call<2>(VirtualBase(object, kVbptr, 1));
    }
};

// An interface reference released (slot 2) when it goes.
struct Held {
    void *object = nullptr;
    Held() = default;
    Held(const Held &) = delete;
    Held &operator=(const Held &) = delete;
    ~Held()
    {
        if (object) Call<2>(object);
    }
};

// Game text: mRefB4's animated entry, the delimiter of an actor's animation list, the empty text, the camera track's
// name, the tag both kinds of track are recorded with, the hiding property and the three cleanup names.
constexpr uint32_t kAnimatedEntry = 0xdadff8;
constexpr uint32_t kListDelimiter = 0xdaa6c0;
constexpr uint32_t kEmptyText = 0xd9b9d0;
constexpr uint32_t kCameraTrack = 0xdae010;
constexpr uint32_t kTrackTag = 0xdae798;
constexpr uint32_t kHidingProperty = 0xdaf004;
constexpr uint32_t kCleanUpNames[] = {0xdae494, 0xdae4a0, 0xdae4ac};
constexpr uint32_t kCleanUpKind = 4;
constexpr uint32_t kCleanUpFlags = 0xf;
// Interfaces and framework classes.
constexpr uint32_t kAnimatedIid = 0xad374e09;
constexpr uint32_t kChannelsIid = 0x5a69d3d5;
constexpr uint32_t kEventsClass = 0x4d5d50f0;
constexpr uint32_t kEventsIid = 0x2d5d50eb;
constexpr uint32_t kControllerClass = 0x09d0ce69;
constexpr uint32_t kControllerIid = 0x09d0ce60;
// The start message to the sound probe.
constexpr uint32_t kMessageClass = 0xcbc79579;
constexpr uint32_t kMessageIid = 0x0bc7957d;
constexpr uint32_t kMessageId = 0x8dcc55e7;
constexpr uint32_t kMessageKey = 0xadcc5609;
// The actor's channel owner: the kind left alone, the kind it is set to; the channels set and the one non-zero value.
constexpr uint32_t kKeptKind = 0x26b1;
constexpr uint32_t kSetKind = 0x26b2;
constexpr uint32_t kChannels[] = {0x29, 0x2a, 0x2b, 0x2c};
constexpr uint32_t kChannelValue = 0x26ac;
// The blend and attach parameters (float bits: 1, 233.33, 0.001).
constexpr uint32_t kOne = 0x3f800000;
constexpr uint32_t kBlendRate = 0x43695555;
constexpr uint32_t kBlendEpsilon = 0x3a83126f;
constexpr uint32_t kCinematicWatch = 0x10cc848;
constexpr uint32_t kWallLevels = 10;
constexpr uint32_t kHidingMode = 3;
constexpr uint32_t kNormalMode = 1;
// The error codes written to *error.
enum : int32_t { kActorGone = 0x89, kNoCamera = 0x99 };
// The object's own slots: find an animation, end the cinematic.
constexpr int kSelfFindAnimation = 26;
constexpr int kSelfEnd = 29;

// The scene-graph helpers (ours, sg/SGUtilities.h) and the game functions not replaced yet.
nGZSceneGraph::cICompositionTreeNode *TreeNode(void *object)
{
    return static_cast<nGZSceneGraph::cICompositionTreeNode *>(object);
}
void *AnimatedOf(void *object) { return nGZSceneGraph::AnimatedFaceOf(TreeNode(object)); }
void *ChannelOwner(void *animated) { return nGZSceneGraph::ChannelOwnerOf(animated); }
const char *ObjectName(void *object) { return nGZSceneGraph::GetNodeTag(TreeNode(object)); }
void *SoundProbe() { return OriginalFunction<void *(__cdecl *)(), 0x9c5b7e>()(); }
void *TileService() { return OriginalFunction<void *(__cdecl *)(), 0x212e61>()(); }
void CleanUpNamed(void *object, uint32_t name)
{
    // Here render layer `name` is added (slot 29's call, 0x255250, removes it instead).
    nGZSceneGraph::SetLayersByFlag(TreeNode(object), kCleanUpKind, GameAddress<const char>(name), nullptr,
                                   kCleanUpFlags);
}
// 0xae4ef0: the blend of `animation` into *out.
float FloatBits(uint32_t bits) { return std::bit_cast<float>(bits); }
void MakeBlend(void **out, void *animation)
{
    nGZSceneGraph::MakeAnimationBlend(out, animation, FloatBits(kOne), 1, 0.0f, FloatBits(kOne), 1,
                                      FloatBits(kBlendRate), FloatBits(kBlendRate), FloatBits(kBlendEpsilon),
                                      FloatBits(kOne), FloatBits(kBlendEpsilon));
}
// 0xae1f50: `animation` attached to `target` (in `scene` at `placement`, from `start`) into *out.
void Attach(void **out, void *target, void *scene, const void *placement, void *animation, uint32_t now, uint32_t start,
            void *blend, uint8_t tag, void *events)
{
    nGZSceneGraph::AttachAnimation(out, target, scene, placement, animation, int32_t(now), int32_t(start), 0.0f,
                                   FloatBits(kOne), 5, 2, 0.0f, blend, tag, 1, FloatBits(kOne), 0.0f, 0, 0, events, 0,
                                   1);
}
// 0x26cbe0 (30 words, __cdecl): records a track.
constexpr size_t kRecordWords = 30;
template <size_t> using Word = uint32_t;
template <size_t... I> void RecordWith(std::index_sequence<I...>, const uint32_t *words)
{
    OriginalFunction<void(__cdecl *)(Word<I>...), 0x26cbe0>()(words[I]...);
}
void Record(const uint32_t (&words)[kRecordWords]) { RecordWith(std::make_index_sequence<kRecordWords>(), words); }
uint32_t Bits(const void *p) { return uint32_t(reinterpret_cast<uintptr_t>(p)); }

// The flag bits of an actor entry name the cleanups its object gets.
void CleanUp(void *object, uint32_t flags)
{
    for (uint32_t i = 0; i < 3; ++i)
        if (flags & (1u << i)) CleanUpNamed(object, kCleanUpNames[i]);
}

// Floats within 32 units in the last place (the original's integer compare of their ordered bits).
int32_t Ordered(uint32_t bits)
{
    const uint32_t sign = bits >> 31;
    return int32_t((((0u - sign) >> 1) ^ bits) + sign);
}
bool Close(uint32_t a, uint32_t b)
{
    const int32_t d = int32_t(uint32_t(Ordered(a)) - uint32_t(Ordered(b)));
    return (d >> 5) == (d >> 31);
}
// A placement (position, then rotation, or the same rotation negated) that already matches the target.
bool SamePlacement(const uint32_t *current, const uint32_t *target)
{
    for (int i = 0; i < 3; ++i)
        if (!Close(current[i], target[i])) return false;
    bool same = true;
    for (int i = 3; i < 7 && same; ++i) same = Close(current[i], target[i]);
    if (same) return true;
    for (int i = 3; i < 7; ++i)
        if (!Close(current[i], target[i] ^ 0x80000000u)) return false;
    return true;
}

// The cue moves on by the animation's length (st0), rounded through a float and truncated.
int32_t Advance(int32_t cue, double length) { return int32_t(float(length + double(float(cue)))); }

int32_t Level(int16_t level) { return level > 0 ? level - 1 : level; }

// The events object of the framework (a reference at +0 of its virtual-base table) and its face (entry 2).
void *EventsFace(void *events) { return events ? VirtualBase(events, 0, 2) : nullptr; }

// One actor with an animated face: its channels set (unless already of the kept kind), its placement matched to the
// camera holder's when the cinematic asks, then each animation of its list attached and recorded one after another.
void PlayActor(cTSSGSystem *self, void *object, void *animated, uint32_t index, uint32_t now)
{
    if (void *owner = ChannelOwner(animated)) {
        void *channel = Call<8, void *>(owner);
        if (channel && Call<14, uint32_t>(channel) != kKeptKind) {
            Call<2>(owner, channel, kSetKind, now, 0u, 0u);
            Held channels;
            if (Call<0, uint8_t>(Call<120, void *>(object), kChannelsIid, &channels.object)) {
                for (uint32_t c : kChannels) Call<11>(channels.object, c, c == 0x2b ? kChannelValue : 0u, 1u);
            }
        }
    }
    if (!*Call<15, const char *>(self->mCinematic, index)) return;
    if (Call<13, uint8_t>(self->mCinematic, index)) {
        void *holder = Call<120, void *>(self->mCinematicRef128);
        Call<92>(holder);
        uint32_t pose[3];
        Call<42>(holder, pose);
        Call<171>(Call<120, void *>(object), pose, 0u, 1u);
        uint32_t level = Call<43, uint32_t>(holder);
        Call<170>(Call<120, void *>(object), &level, 1u);
        uint32_t target[7], current[7];
        Call<54>(self->mCinematicRef128, target);
        Call<54>(object, current);
        if (!SamePlacement(current, target)) {
            Call<63>(object, target);
            Call<54>(object, current);
        }
        Call<35>(animated);
    }

    int32_t offset = 0;
    SGLocalString list(Call<15, const char *>(self->mCinematic, index));
    const char *delimiter = GameAddress<const char>(kListDelimiter);
    while (!list.value.mString.empty()) {
        SGLocalString token;
        if (list.value.Find(delimiter, 0, true) != -1) {
            cRZString piece;
            reinterpret_cast<cRZIniStringHelpers *>(&list.value)->SplitOff(&piece, delimiter);
            token.value.mString.assign(piece.mString.data(), piece.mString.size());
            piece.Destruct();
        } else {
            token.value.mString.assign(list.value.mString.data(), list.value.mString.size());
            list.value.mString.assign(GameAddress<const char>(kEmptyText));
        }
        reinterpret_cast<cRZIniStringHelpers *>(&token.value)->TrimBoth();
        if (token.value.mString.empty()) continue;
        BaseRef<4> animation;
        if (!Call<kSelfFindAnimation, uint8_t>(self, token.Text(), &animation.object, 0u)) continue;
        BaseRef<4> blend;
        MakeBlend(&blend.object, animation.object);
        BaseRef<0> events;
        Call<3>(GetDirectorFrameworkObject(), kEventsClass, kEventsIid, &events.object);
        BaseRef<4> attached;
        const uint32_t from = uint32_t(offset) + now;
        Attach(&attached.object, animated, nullptr, nullptr, animation.object, now, from, blend.object,
               self->mField163, EventsFace(events.object));
        Call<1>(animated, now, 0u, 1u, 0u, 0u);
        void *nameObject = Call<17, void *>(animation.object);
        const uint32_t nameId = nameObject ? Call<13, uint32_t>(Call<17, void *>(animation.object)) : 0;
        SGLocalString name;
        if (ObjectName(object)) name.value.mString.assign(ObjectName(object));
        const int32_t kind = Call<47, int16_t>(Call<120, void *>(object));
        const uint8_t b2 = Call<55, uint8_t>(animation.object, 2u);
        const uint8_t b1 = Call<55, uint8_t>(animation.object, 1u);
        const uint8_t b0 = Call<55, uint8_t>(animation.object, 0u);
        const uint8_t b53 = Call<53, uint8_t>(animation.object);
        const uint32_t d9 = Call<50, uint32_t>(animation.object, 9u);
        const uint32_t d7 = Call<50, uint32_t>(animation.object, 7u);
        const uint32_t d8 = Call<50, uint32_t>(animation.object, 8u);
        const uint32_t d52 = Call<52, uint32_t>(animation.object);
        void *model = Call<120, void *>(object);
        const char *label = Call<17, const char *>(VirtualBase(animation.object, 4, 1));
        const uint32_t words[kRecordWords] = {Bits(label), Bits(name.Text()), 0, Bits(model), from, 0,
                                              attached.object != nullptr, 1, self->mField163, Bits(GameAddress<const char>(kTrackTag)),
                                              2, 0, 0, 0, 1, d52, d8, d7, d9, b53, b0, b1, b2, nameId, 0, 0, 0xffffffffu, 0,
                                              0, uint32_t(kind)};
        Record(words);
        offset = Advance(offset, Call<36, double>(animation.object));
    }
}

} // namespace

namespace nTSSG {

uint8_t cTSSGSystem::PlayCinematic(int32_t start, int32_t *error)
{
    *error = -1;
    for (cTSSGCinematicActor *it = mCinematicActors.data(), *end = it + mCinematicActors.size(); it != end; ++it) {
        if (it->mObject.mObject && !Call<120, void *>(it->mObject.mObject)) {
            Call<kSelfEnd>(this, 1u);
            *error = kActorGone;
            return 0;
        }
    }

    // The camera: the scene manager's animated face, when the cinematic is held and mRefB4's animated entry answers.
    BaseRef<4> camera;
    const uint32_t now = Call<11, uint32_t>(VirtualBase(mRefB0.mObject, 4, 2));
    uint32_t placement[7];
    Call<54>(mCinematicRef128, placement);
    if (mCinematicHeld && mRefB4.mObject) {
        const int32_t index = Call<35, int32_t>(mRefB4.mObject, GameAddress<const char>(kAnimatedEntry));
        if (index != -1) {
            void *entry = Call<36, void *>(mRefB4.mObject, index);
            void *animated = nullptr;
            if (Call<0, uint8_t>(VirtualBase(entry, 4, 1), kAnimatedIid, &animated) &&
                Call<18, void *>(mSceneManager.mObject)) {
                if (void *face = AnimatedOf(Call<18, void *>(mSceneManager.mObject))) {
                    Call<1>(VirtualBase(face, 4, 1));
                    camera.object = face;
                }
            }
            if (animated) Call<2>(VirtualBase(animated, 0, 1));
        }
    }
    mCinematicHeld = 0;
    if (!camera.object) {
        Call<kSelfEnd>(this, 1u);
        if (*error == -1) *error = kNoCamera;
        return 0;
    }

    // The actors (an index per entry, empty ones too) and the extras.
    uint32_t index = 0;
    for (cTSSGCinematicActor *it = mCinematicActors.data(), *end = it + mCinematicActors.size(); it != end;
         ++it, ++index) {
        void *object = it->mObject.mObject;
        if (!object) continue;
        if (*Call<14, const char *>(mCinematic, index)) Call<78>(object, Call<14, const char *>(mCinematic, index), 1u);
        if (void *animated = AnimatedOf(object)) PlayActor(this, object, animated, index, now);
        CleanUp(object, it->mValue);
    }
    for (cTSSGCinematicActor *it = mCinematicExtras.data(), *end = it + mCinematicExtras.size(); it != end; ++it) {
        void *object = it->mObject.mObject;
        Call<97>(object, 1u);
        CleanUp(object, it->mValue);
    }

    mCinematicRestore = Call<7, uint32_t>(mRefB0.mObject);
    Call<1>(mRefB0.mObject, 1u);
    auto *watch = GameAddress<cRZStopwatch>(kCinematicWatch);
    nGZCommandParser::ResetStopwatch(watch);
    watch->Start();
    if (void *probe = SoundProbe()) {
        Held message;
        Call<3>(GetDirectorFrameworkObject(), kMessageClass, kMessageIid, &message.object);
        if (message.object) {
            Call<5>(message.object, kMessageId);
            Call<12>(message.object, kMessageKey, 1u);
            Call<12>(probe, message.object);
        }
    }
    mField2C4 = 0;

    // The view settings saved and set for the cinematic (render option 8 keeps them), the hiding property, the walls.
    if (void *settings = GetCachedSingletonB()) {
        mCinematicSaved[0] = Call<26, uint32_t>(settings);
        uint32_t level = uint32_t(Level(Call<36, int16_t>(Call<120, void *>(mCinematicRef128))));
        if (mObjectHiding) level = Call<89, uint32_t>(TileService());
        if (!mRenderOptions[8]) Call<27>(settings, level);
        mCinematicSaved[1] = Call<28, uint32_t>(settings);
        if (!mRenderOptions[8]) Call<29>(settings, mObjectHiding ? kHidingMode : kNormalMode);
        if (mObjectHiding) {
            if (void *properties = Call<21, void *>(GetCachedSingleton())) {
                Call<14>(properties, GameAddress<const char>(kHidingProperty), &mObjectHidingValue);
                Call<13>(properties, GameAddress<const char>(kHidingProperty), 1u);
            }
        }
        mCinematicSaved[2] = Call<23, uint32_t>(settings);
        if (!mRenderOptions[8]) {
            Call<25>(settings, kWallLevels, 1u);
            if (mWallManager.mObject) {
                Call<16>(mWallManager.mObject);
                for (cTSSGCinematicActor *it = mCinematicActors.data(), *end = it + mCinematicActors.size(); it != end;
                     ++it) {
                    void *object = it->mObject.mObject;
                    if (!object) continue;
                    uint32_t where[7];
                    Call<54>(object, where);
                    const int32_t floor = Level(Call<36, int16_t>(Call<120, void *>(object)));
                    uint32_t spot[3] = {where[0], where[1], uint32_t(floor)};
                    Call<15>(mWallManager.mObject, spot);
                }
                Call<21>(mWallManager.mObject, 1u);
            }
        }
    }

    // The camera tracks: each part in turn from `start` (the original does not bound the parts by the vector); the
    // last one's track gets the new controller.
    const uint32_t parts = Call<6, uint32_t>(mCinematic);
    const SGVirtualRef *part = mCinematicRefs.data();
    int32_t cue = start;
    for (uint32_t k = 1; k <= parts; ++k, ++part) {
        BaseRef<4> blend;
        MakeBlend(&blend.object, part->mObject);
        BaseRef<0> events;
        Call<3>(GetDirectorFrameworkObject(), kEventsClass, kEventsIid, &events.object);
        BaseRef<4> attached;
        void *eventsFace = EventsFace(events.object);
        const uint32_t from = uint32_t(cue) + now;
        Attach(&attached.object, camera.object, Call<17, void *>(mSceneManager.mObject), placement, part->mObject, now,
               from, blend.object, mField163, eventsFace);
        void *nameObject = Call<17, void *>(part->mObject);
        const uint32_t nameId = nameObject ? Call<13, uint32_t>(Call<17, void *>(part->mObject)) : 0;
        const uint8_t b53 = Call<53, uint8_t>(part->mObject);
        const uint32_t d52 = Call<52, uint32_t>(part->mObject);
        const char *label = Call<17, const char *>(VirtualBase(part->mObject, 4, 1));
        const uint32_t words[kRecordWords] = {Bits(label), Bits(GameAddress<const char>(kCameraTrack)), 0, 0, from, 0,
                                              attached.object != nullptr, 1, mField163, Bits(GameAddress<const char>(kTrackTag)),
                                              2, 0, 0, 0, 1, d52, 0, 0, 0, b53, 0, 0, 0, nameId, 0, 0, 0xffffffffu, 0, 0, 0};
        Record(words);
        cue = Advance(cue, Call<36, double>(part->mObject));
        if (k == parts) {
            void *framework = GetDirectorFrameworkObject();
            if (void *old = mRef124.mObject) {
                mRef124.mObject = nullptr;
                Call<2>(VirtualBase(old, 4, 1));
            }
            Call<3>(framework, kControllerClass, kControllerIid, &mRef124.mObject);
            void *controller = mRef124.mObject;
            Call<3>(controller, Call<17, const char *>(VirtualBase(part->mObject, 4, 1)));
            Call<4>(mRef124.mObject, GameAddress<const char>(kTrackTag));
            Call<7>(mRef124.mObject, attached.object ? VirtualBase(attached.object, 4, 2) : nullptr);
            Call<8>(mRef124.mObject, 0u);
            Call<9>(mRef124.mObject, 0u);
            Call<10>(mRef124.mObject, 0u);
            Call<12>(mRef124.mObject, 0u);
        }
    }
    Call<1>(camera.object, now, 0u, 1u, 0u, 0u);
    return 1;
}

} // namespace nTSSG
