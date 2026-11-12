// cTSAudioTSObjectUpdateDemon, tMonitorEntry and tPlayingSound (lane LD) in the standalone shape
// (src/audio/cTSAudioTSObjectUpdateDemon.h).
#include "audio/cTSAudioTSObjectUpdateDemon.h"

#include "audio/cTSAudioEvent.h" // cITSAudioEvent
#include "compgeom/Linear3.h"
#include "compgeom/Rigid.h"
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZAny.h" // gz::AnyFloat32
#include "gz/cIGZFrameWork.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZSystemService.h"
#include "misc/UtilitiesC.h"     // KeyMapSlot5Walker, GetCachedServiceObjectC
#include "rz/MiscHelpersB.h"     // KeyMapWalker
#include "rz/Singleton_20ddfb.h" // GetServiceObject, GetCachedSingleton
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "sg/SGUtilities.h"      // FindNodeByName
#include "simantics/SimGeometry.h" // simgeom::RotateByQuaternion

#include <cfloat>
#include <cmath>
#include <cstring>
#include <new>

namespace {

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// The framework service cached at 0x10f5194 (0x211051: filled from GetServiceObject the first time).
void *CachedFramework()
{
    void *&framework = *GameAddress<void *>(0x10f5194);
    if (!framework) framework = GetServiceObject();
    return framework;
}

// Original callees nobody has replaced: the state service and the sound renderer probe (cached getters), the stopwatch's
// restart.
cIGZUnknown *StateService() { return OriginalFunction<cIGZUnknown *(__cdecl *)(), 0x211da3>()(); }
void *SoundProbe() { return OriginalFunction<void *(__cdecl *)(), 0x9c5b7e>()(); }
void Restart(cRZStopwatch *watch) { OriginalFunction<void(__thiscall *)(cRZStopwatch *), 0x1c95ec>()(watch); }

// The scene graph's: the named node under a node (0xae7490, ours; 0xf the search flags), a node's bounding box (0xae7d00,
// ours: min then max, grown; no skip flag and the node as its own frame, as the original passes them), a point through
// a transform (0x19c900, __thiscall, ret 4: rotated by the quaternion, then moved), and the ray's constructor /
// assignment / destructor.
void *FindNode(const char *name, void *node)
{
    return nGZSceneGraph::FindNodeByName(name, static_cast<nGZSceneGraph::cICompositionTreeNode *>(node), 0xf);
}
void GrowBounds(float *box, void *node)
{
    auto *tree = static_cast<nGZSceneGraph::cICompositionTreeNode *>(node);
    nGZSceneGraph::GrowByShapeBounds(box, -1, tree, tree);
}
void TransformPoint(const AudioTransform *transform, AudioPoint *point)
{
    reinterpret_cast<const nGZCompGeom::RigidTransform3 *>(transform)->TransformPoint(reinterpret_cast<nGZCompGeom::Vec3 *>(point));
}
void ConstructRay(AudioRay *ray, const AudioPoint *origin, const AudioPoint *direction)
{
    new (ray) nGZCompGeom::cRay3(*reinterpret_cast<const nGZCompGeom::Vec3 *>(origin),
                                 *reinterpret_cast<const nGZCompGeom::Vec3 *>(direction));
}
void AssignRay(AudioRay *ray, const AudioRay *source)
{
    reinterpret_cast<nGZCompGeom::cRay3 *>(ray)->Assign(*reinterpret_cast<const nGZCompGeom::cRay3 *>(source));
}
void DestroyRay(AudioRay *ray) { reinterpret_cast<nGZCompGeom::cRay3 *>(ray)->nGZCompGeom::cRay3::~cRay3(); }

// A ray held for a scope. The original builds Step's ray inline (the vtable and +0x1c only: the origin and direction are
// whatever the frame held until AimRay fills them); ours is built by the constructor from zero points, the same object
// once AimRay has run.
struct ScopedRay {
    ScopedRay(const AudioPoint &origin, const AudioPoint &direction) { ConstructRay(&ray, &origin, &direction); }
    ~ScopedRay() { DestroyRay(&ray); }
    ScopedRay(const ScopedRay &) = delete;
    ScopedRay &operator=(const ScopedRay &) = delete;
    AudioRay ray;
};

// The game's float comparison of the places: equal when the two values, as integers ordered like the floats (the sign and
// magnitude made two's complement), are within 32 of each other: first - second in [-32, 31].
int32_t OrderedBits(float value)
{
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    const uint32_t sign = bits >> 31;
    return static_cast<int32_t>(((0u - sign) >> 1 ^ bits) + sign);
}
bool NearlyEqual(float first, float second)
{
    const int32_t difference = static_cast<int32_t>(static_cast<uint32_t>(OrderedBits(first)) -
                                                    static_cast<uint32_t>(OrderedBits(second)));
    return (difference >> 5) == (difference >> 31);
}
bool NearlyEqual(const AudioPoint &first, const AudioPoint &second)
{
    return NearlyEqual(first.x, second.x) && NearlyEqual(first.y, second.y) && NearlyEqual(first.z, second.z);
}

// The model's virtual base (its count face): +4 + the virtual base table's entry 1.
cIGZUnknown *ModelBase(void *model)
{
    auto *const base = static_cast<uint8_t *>(model) + 4;
    return reinterpret_cast<cIGZUnknown *>(base + (*reinterpret_cast<const int32_t *const *>(base))[1]);
}

// The facing a node's rotation turns (0xff51b8: up; 0xff51ac, for the model itself when it has no "root_rot" node: y).
// Both cells are only read (by the placement update), statically (0, 0, 1) and (0, 1, 0).
constexpr AudioPoint kNodeFacing = {0.0f, 0.0f, 1.0f};
constexpr AudioPoint kModelFacing = {0.0f, 1.0f, 0.0f};
constexpr float kPi = 3.14159274f; // 0xda77d8

// The sound renderer's events (an audio event from the framework's class 0xcbc79579, posted through the renderer's slot
// 17), their keys, and the key the step's audibility goes to the renderer's slot 54 with.
constexpr uint32_t kEventClass = 0xcbc79579;
constexpr uint32_t kPlaceEvent = 0xec773209, kVisibilityEvent = 0x4a451915;
constexpr uint32_t kPositionKey = 0x6bd63530, kFacingKey = 0x8c3f9e02, kRoomKey = 0x2d7eae6e, kFloorKey = 0xcd7eb01b,
                   kAudibleKey = 0xed7ea743, kGainKey = 0x0bd63084, kFadeKey = 0x8bd63940;

// The message types (DoMessage) and the audio event's keys.
enum : uint32_t {
    kMsgSoundEvent = 0xcc773dd7, // start or stop (OnSoundEvent)
    kMsgSoundStart = 0xaedbce8b, // (re)start a sound
    kMsgUnused = 0x2e04ed78,     // registered, not handled
    kMsgClearB2 = 0xb2,
    kMsgClearB5 = 0xb5,
    kMsgMode = 0xece20be8,
};
constexpr uint32_t kModeIid = 0x65297976;
constexpr uint32_t kObjectIdKey = 0xac5b5b4f, kSoundIdKey = 0x6bd63081, kVolumeKey = 0xec488a56, kStartKey = 0x8bd63096;

} // namespace

// ---- tMonitorEntry ----

// 0x2291ac (124 B): the demon counted, the id; -1 in +0x30 / +0x34; the points, +0x2e / +0x2f and the padding not written.
tMonitorEntry::tMonitorEntry(cTSAudioTSObjectUpdateDemon *demon, uint16_t objectId) : mDemon(demon)
{
    mRegistered = 0;
    mUpdatedAt = 0;
    mStepAt = 0;
    mInterval = 0;
    mObjectId = objectId;
    mRoom = -1;
    mFloor = -1;
    mAudibility = 0;
    mSoundCount = 0;
    mDistance = 0;
    mEnabled = 0;
    mHidden = 0;
}

// 0x2292e9: the references in reverse order (+0x18, the model through its virtual base, +0x10, +0xc, the demon).
tMonitorEntry::~tMonitorEntry() noexcept = default;

// 0x229dc8 (109 B).
bool tMonitorEntry::Init()
{
    if (!RawSlot<46, cIGZUnknown *>(StateService())) return false;
    mLot.Assign(RawSlot<4, cIGZUnknown *>(RawSlot<46, cIGZUnknown *>(StateService())));
    if (!mLot || !SoundProbe()) return false;
    mStepAt = mDemon->Seconds();
    mUpdatedAt = mDemon->Seconds();
    mEnabled = 1;
    return true;
}

// 0x22a00f (53 B).
bool tMonitorEntry::Detach()
{
    mEnabled = 0;
    mDemon.Assign(nullptr);
    mLot.Assign(nullptr);
    mObject.Assign(nullptr);
    mModel.Assign(nullptr);
    return true;
}

void *tMonitorEntry::View() { return RawSlot<18, void *>(RawSlot<33, void *>(StateService())); }
void *tMonitorEntry::LotScene() { return RawSlot<17, void *>(RawSlot<33, void *>(StateService())); }

// 0x22a5ad (1420 B). Without the app's world nothing changes. The world's object of this id, when it has one with a room
// (slot 131) not negative, is taken (a new one drops the model), and its model fetched (slot 12) when none is held. Enabled
// is having both; without them the audibility, the object, the model and the hidden flag are cleared and the sound
// renderer told (slot 29). Else the place: the "root_rot" node's transform (or the model's, slot 54) and the facing its
// rotation turns; when either moved (beyond 32 ulps in a coordinate) the centre is first the old place, then, when the
// model answers 0x7d14eb66, its bounding box centre through the model's transform. With no 0x5a69d3d5 face the facing is
// the object's direction (slot 43) in eighths of a turn. Then the floor (slot 36, less one), the room, the distance to
// the view's eye (slot 53) and the hidden flag (slot 13 of 0x22, bit 1).
void tMonitorEntry::UpdatePlacement()
{
    if (!RawSlot<26, void *>(GetCachedSingleton())) return;
    cIGZUnknown *const object =
        RawSlot<29, cIGZUnknown *>(RawSlot<26, void *>(GetCachedSingleton()), static_cast<uint32_t>(mObjectId));
    if (object && RawSlot<131, int16_t>(object) >= 0) {
        if (mObject.Get() != object) {
            mObject.Assign(object);
            mModel.Assign(nullptr);
        }
        if (!mModel.Get()) RawSlot<12>(mObject.Get(), 0xfa1c39f7u, 0xfa1c39edu, mModel.Reset());
    }
    mEnabled = mObject && mModel.Get();
    if (!mEnabled) {
        mAudibility = 0.0f;
        mObject.Assign(nullptr);
        mModel.Assign(nullptr);
        mHidden = 0;
        RawSlot<29>(GetCachedServiceObjectC(), static_cast<uint32_t>(mObjectId), 0u);
        return;
    }

    AudioPoint facing = kNodeFacing;
    void *node = FindNode("root_rot", mModel.Get());
    if (!node) {
        node = mModel.Get();
        facing = kModelFacing;
    }
    AudioTransform placed;
    RawSlot<54>(node, &placed);
    simgeom::RotateByQuaternion(&facing.x, placed.rotation);
    if (!NearlyEqual(placed.position, mPosition) || !NearlyEqual(facing, mFacing)) {
        mCentre = mPosition;
        AudioRef bounds;
        ModelBase(mModel.Get())->QueryInterface(0x7d14eb66, bounds.AsPPVoid());
        if (bounds) {
            float box[6] = {FLT_MAX, FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX}; // min, max
            GrowBounds(box, mModel.Get());
            AudioPoint centre = {(box[3] - box[0]) * 0.5f + box[0], (box[4] - box[1]) * 0.5f + box[1],
                                 (box[5] - box[2]) * 0.5f + box[2]};
            AudioTransform model;
            TransformPoint(RawSlot<54, const AudioTransform *>(mModel.Get(), &model), &centre);
            mCentre = centre;
        }
    }
    mPosition = placed.position;
    mFacing = facing;

    AudioRef face;
    if (!mObject->QueryInterface(0x5a69d3d5, face.AsPPVoid())) {
        const float angle = static_cast<float>(RawSlot<43, int32_t>(mObject.Get())) * kPi * 0.25f;
        mFacing.x = -static_cast<float>(std::sin(static_cast<double>(angle)));
        mFacing.y = static_cast<float>(std::cos(static_cast<double>(angle)));
        mFacing.z = 0.0f;
    }
    mFloor = RawSlot<36, int16_t>(mObject.Get()) - 1;
    mRoom = RawSlot<131, int16_t>(mObject.Get());
    AudioTransform view;
    const AudioPoint eye = RawSlot<53, const AudioTransform *>(View(), &view)->position;
    const float dx = eye.x - mPosition.x, dy = eye.y - mPosition.y, dz = eye.z - mPosition.z;
    mDistance = static_cast<float>(std::sqrt(static_cast<double>(dx * dx + dy * dy + dz * dz)));
    mHidden = (RawSlot<13, uint16_t>(mObject.Get(), 0x22u) >> 1) & 1;
}

// 0x2296d2 (221 B, ret 4): false without a model. Else the ray from the view's eye (slot 140 with 0; plus zero, so a
// negative zero becomes positive) towards the bounding box centre, built (0x18e720) and assigned to `ray` (0x18e880).
bool tMonitorEntry::AimRay(AudioRay *ray)
{
    if (!mModel.Get()) return false;
    AudioTransform view;
    const AudioPoint eye = RawSlot<140, const AudioTransform *>(View(), &view, 0u)->position;
    const AudioPoint centre = mCentre;
    const AudioPoint direction = {centre.x - eye.x, centre.y - eye.y, centre.z - eye.z};
    const AudioPoint origin = {eye.x + 0.0f, eye.y + 0.0f, eye.z + 0.0f};
    ScopedRay aimed(origin, direction);
    AssignRay(ray, &aimed.ray);
    return true;
}

// 0x2299b7 (328 B): when enabled, with the view and the lot scene: the ray aimed (its result unused); audible (1) on the
// view's floor (the demon's +0x5c) or in room 0 (the low word), else below that floor when the lot scene's ray cast
// (slot 92: the range 1, the lot, the ray) hits nothing, else not (0). A change goes to the sound renderer (slot 54: the
// object id, key 0xed7ea743, an AnyFloat32). The step time is the demon's seconds.
void tMonitorEntry::Step()
{
    if (!mEnabled || !View() || !LotScene()) return;
    const AudioPoint zero = {};
    ScopedRay ray(zero, zero);
    AimRay(&ray.ray);
    float audibility;
    if (mFloor == mDemon->mModeWord5c || static_cast<int16_t>(mRoom) == 0) {
        audibility = 1.0f;
    } else if (mFloor < mDemon->mModeWord5c) {
        float range = 1.0f;
        AudioRef hit;
        hit.Assign(RawSlot<92, cIGZUnknown *>(LotScene(), &range, mLot.Get(), &ray.ray));
        audibility = static_cast<float>(hit.Get() == nullptr);
    } else {
        audibility = 0.0f;
    }
    if (audibility != mAudibility) {
        mAudibility = audibility;
        gz::AnyFloat32 value(mAudibility);
        RawSlot<54>(GetCachedServiceObjectC(), static_cast<uint32_t>(mObjectId), kAudibleKey, static_cast<cIGZAny *>(&value));
    }
    mUpdatedAt = mDemon->Seconds();
}

// 0x22a091 (136 B): the placement, then (when enabled) the update interval: the seconds since Init over the distance
// rounded up (ceil of the float distance + 1e-6, the compiler's way: the rounding constant 1.5 * 2^23 added to the value
// + 0.5, the result's bits less the constant's, one less when that equals the value). The step time follows the demon's
// clock while sounds play.
void tMonitorEntry::Update()
{
    UpdatePlacement();
    mInterval = 0;
    if (!mEnabled) return;
    const uint32_t now = mDemon->Seconds();
    const float distance = static_cast<float>(static_cast<double>(mDistance) + 1e-6);
    const float rounded = (distance + 0.5f) + 12582912.0f;
    uint32_t bits;
    std::memcpy(&bits, &rounded, 4);
    uint32_t steps = bits - 0x4b400000u;
    if (static_cast<float>(static_cast<int32_t>(steps - 1)) == distance) steps -= 1;
    mInterval = (now - mUpdatedAt) / steps;
    if (mSoundCount) mStepAt = now;
}

// ---- tPlayingSound ----

// 0x229228 (120 B): the demon counted, both points from 0x10cc5a0; +0x12 / +0x13 and the padding not written.
tPlayingSound::tPlayingSound(cTSAudioTSObjectUpdateDemon *demon) : mDemon(demon)
{
    mObjectId = 0;
    mSoundId = 0;
    mVolume = 0;
    mPosition = *GameAddress<AudioPoint>(0x10cc5a0);
    mFacing = *GameAddress<AudioPoint>(0x10cc5a0);
    mAudibility = 0;
    mRoom = 0;
    mFloor = 0;
    mHidden = 0;
}

// 0x229374: the monitor, then the demon.
tPlayingSound::~tPlayingSound() noexcept = default;

// 0x229f6e (61 B): a monitor taken only when none is held (its sound count up), dropped only for null (its count down);
// anything else leaves the held one.
void tPlayingSound::SetMonitor(tMonitorEntry *monitor)
{
    if (!mMonitor) {
        if (!monitor) return;
        mMonitor.Assign(monitor);
        ++mMonitor->mSoundCount;
    } else if (!monitor) {
        --mMonitor->mSoundCount;
        mMonitor.Assign(nullptr);
    }
}

// 0x22a044 (26 B).
bool tPlayingSound::Detach()
{
    mDemon.Assign(nullptr);
    SetMonitor(nullptr);
    return true;
}

// 0x22a119 (1146 B). The events come from the director's framework (slot 3: class 0xcbc79579, the event interface) and
// go to the sound renderer's slot 17; an event the framework doesn't make is skipped.
void tPlayingSound::Update()
{
    tMonitorEntry *const monitor = mMonitor.Get();
    if (!monitor) return;
    if (!monitor->mEnabled) {
        SetMonitor(nullptr);
        return;
    }
    if (!NearlyEqual(mPosition, monitor->Position()) || !NearlyEqual(mFacing, monitor->Facing()) ||
        mRoom != monitor->mRoom || mFloor != monitor->mFloor || mAudibility != monitor->mAudibility) {
        mPosition = monitor->Position();
        mFacing = mMonitor->Facing();
        mRoom = mMonitor->mRoom;
        mFloor = mMonitor->mFloor;
        mAudibility = mMonitor->mAudibility;
        cRZAutoRefCount<cITSAudioEvent> event;
        void *const framework = GetDirectorFrameworkObject();
        RawSlot<3, bool>(framework, kEventClass, cITSAudioEvent::kIid, event.AsPPVoid());
        if (event) {
            event->SetEventId(kPlaceEvent);
            event->SetUint32(kSoundIdKey, mSoundId);
            event->SetVector3(kPositionKey, &mPosition.x);
            event->SetVector3(kFacingKey, &mFacing.x);
            event->SetUint32(kRoomKey, static_cast<uint32_t>(mRoom));
            event->SetSint32(kFloorKey, mFloor);
            event->SetUint32(kAudibleKey, mAudibility != 0.0f);
            RawSlot<17>(GetCachedServiceObjectC(), event.Get());
        }
    }
    if (mHidden ? !mMonitor->Hidden() : mMonitor->Hidden()) {
        mHidden = mMonitor->Hidden();
        cRZAutoRefCount<cITSAudioEvent> event;
        void *const framework = GetDirectorFrameworkObject();
        RawSlot<3, bool>(framework, kEventClass, cITSAudioEvent::kIid, event.AsPPVoid());
        if (event) {
            event->SetEventId(kVisibilityEvent);
            event->SetUint32(kSoundIdKey, mSoundId);
            event->SetFloat(kGainKey, mHidden ? 0.0f : 1.0f);
            event->SetFloat(kFadeKey, 0.0f);
            RawSlot<17>(GetCachedServiceObjectC(), event.Get());
        }
    }
}

// ---- the demon: construction ----

// 0x22910e (158 B): the service base (id 0x4d58135d, priority -0x2dc6c0), the members in order (+0x64 not written).
cTSAudioTSObjectUpdateDemon::cTSAudioTSObjectUpdateDemon() : rz::cRZSystemService(kServiceId, kPriority)
{
    mWatch.Construct(3);
}

// 0x2292a0: the members in reverse order (the monitors, the sounds, the message ids), then the base.
cTSAudioTSObjectUpdateDemon::~cTSAudioTSObjectUpdateDemon() noexcept = default;

// 0x229c42: new 0x68 and the constructor; the +8 face (null when new failed).
cIGZSystemService *__cdecl cTSAudioTSObjectUpdateDemon::Create()
{
    cTSAudioTSObjectUpdateDemon *const demon = new (std::nothrow) cTSAudioTSObjectUpdateDemon;
    return demon ? demon->Service() : nullptr;
}

cTSAudioTSObjectUpdateDemon *__fastcall cTSAudioTSObjectUpdateDemonAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cTSAudioTSObjectUpdateDemon;
}

// ---- cIGZUnknown ----

// 0x229e98: the interface -> this, counted through the primary face's AddRef; else the service's QueryInterface.
bool cTSAudioTSObjectUpdateDemon::QueryInterface(uint32_t riid, void **out)
{
    if (riid != kIid) return rz::cRZSystemService::QueryInterface(riid, out);
    *out = static_cast<cITSAudioTSObjectUpdateDemon *>(this);
    static_cast<cITSAudioTSObjectUpdateDemon *>(this)->AddRef();
    return true;
}

uint32_t cTSAudioTSObjectUpdateDemon::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cTSAudioTSObjectUpdateDemon::Release() { return rz::cRZSystemService::Release(); }

// ---- cITSAudioTSObjectUpdateDemon / cIGZSystemService ----

// 0x229d1e (162 B): false without the framework; the message ids, registered with the message server (0x1c0458 with the
// message target face), the stopwatch restarted, the service on the tick list and running.
bool cTSAudioTSObjectUpdateDemon::Init()
{
    void *const framework = CachedFramework();
    if (!framework) return false;
    for (uint32_t id : {uint32_t(kMsgSoundEvent), uint32_t(kMsgSoundStart), uint32_t(kMsgUnused), uint32_t(kMsgClearB2),
                        uint32_t(kMsgClearB5), uint32_t(kMsgMode)})
        mMessageIds.insert(id);
    reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)->CallSlot5ForEachKey(Word(MessageTarget()), nullptr);
    Restart(&mWatch);
    cIGZSystemService *const service = Service();
    As<ifaces::cIGZFrameWork>(framework)->AddToTick(As<ifaces::cIGZSystemService>(service));
    service->SetServiceRunning(true);
    return true;
}

// 0x229fbc (75 B): the message ids unregistered (0x1c081e), then, with the framework, the service stopped and off the tick
// list. The monitors and sounds stay.
bool cTSAudioTSObjectUpdateDemon::Shutdown()
{
    reinterpret_cast<KeyMapWalker *>(&mMessageIds)->CallSlot6ForEachKey(Word(MessageTarget()), nullptr);
    void *const framework = CachedFramework();
    if (!framework) return false;
    cIGZSystemService *const service = Service();
    service->SetServiceRunning(false);
    As<ifaces::cIGZFrameWork>(framework)->RemoveFromTick(As<ifaces::cIGZSystemService>(service));
    return true;
}

// 0x229e45 (64 B): every monitor's update, then the monitors stepped in turn until 500 units of the stopwatch passed (at
// least one step), then every sound's update.
bool cTSAudioTSObjectUpdateDemon::OnTick(uint32_t)
{
    UpdateMonitors();
    const uint32_t start = mWatch.Elapsed();
    do {
        StepNextMonitor();
    } while (mWatch.Elapsed() - start < 500);
    UpdateSounds();
    return true;
}

// ---- cIGZMessageTarget2 ----

// 0x2297c2 (408 B). The sound start and the mode messages answer false even when handled.
bool cTSAudioTSObjectUpdateDemon::DoMessage(void *message)
{
    auto *const m = static_cast<cIGZUnknown *>(message);
    if (!m) return false;
    switch (RawSlot<4>(m)) {
    case kMsgClearB2:
    case kMsgClearB5: return ClearAll();
    case kMsgSoundStart: {
        cRZAutoRefCount<cITSAudioEvent> event;
        if (m->QueryInterface(cITSAudioEvent::kIid, event.AsPPVoid())) {
            AddMonitor(event->GetUint32(kObjectIdKey));
            RemoveSound(event->GetUint32(kSoundIdKey));
            const float volume = event->GetFloat(kVolumeKey);
            const uint32_t soundId = event->GetUint32(kSoundIdKey);
            AddSound(event->GetUint32(kObjectIdKey), soundId, volume);
        }
        return false;
    }
    case kMsgSoundEvent: {
        cRZAutoRefCount<cITSAudioEvent> event;
        if (!m->QueryInterface(cITSAudioEvent::kIid, event.AsPPVoid())) return false;
        return OnSoundEvent(event.Get());
    }
    case kMsgMode: {
        // ORIGINAL-BUG: the face is used without checking the QueryInterface (a null call when the message lacks it).
        AudioRef mode;
        m->QueryInterface(kModeIid, mode.AsPPVoid());
        mModeWord5c = RawSlot<9, int32_t>(mode.Get());
        mModeWord60 = RawSlot<19, int32_t>(mode.Get());
        return false;
    }
    default: return false;
    }
}

// ---- non-virtual members ----

// 0x229436 (293 B): for a non-zero id with the app's world, the world's object of that id; when it has none yet, a new
// monitor, kept (counted; put first, then the round-robin turned once: so it ends last) when its Init succeeds. True only when one was kept.
bool cTSAudioTSObjectUpdateDemon::AddMonitor(uint32_t objectId)
{
    const uint16_t id = static_cast<uint16_t>(objectId);
    if (!id || !RawSlot<26, void *>(GetCachedSingleton())) return false;
    AudioRef object(RawSlot<29, cIGZUnknown *>(RawSlot<26, void *>(GetCachedSingleton()), uint32_t(id)));
    if (!object || FindMonitor(objectId)) return false;
    // ORIGINAL-BUG: a failed new is used unchecked (Init on null).
    cRZAutoRefCount<tMonitorEntry> monitor(new tMonitorEntry(this, id));
    if (!monitor->Init()) return false;
    monitor->mRegistered = 1;
    monitor->Update();
    ++mMonitorsAdded;
    mMonitors.push_front(monitor);
    RotateMonitors();
    return true;
}

// 0x22955b (319 B): with the app's world and its object of that id, no sound of that id yet and a monitor for the object, a
// new sound (id, volume) updated once (before it has its monitor: the update returns at once), then tied to the monitor.
bool cTSAudioTSObjectUpdateDemon::AddSound(uint32_t objectId, uint32_t soundId, float volume)
{
    if (!RawSlot<26, void *>(GetCachedSingleton())) return false;
    AudioRef object(
        RawSlot<29, cIGZUnknown *>(RawSlot<26, void *>(GetCachedSingleton()), uint32_t(static_cast<uint16_t>(objectId))));
    if (!object || FindSound(soundId)) return false;
    tMonitorEntry *const monitor = FindMonitor(objectId);
    if (!monitor) return false;
    // ORIGINAL-BUG: a failed new is used unchecked.
    cRZAutoRefCount<tPlayingSound> sound(new tPlayingSound(this));
    sound->mObjectId = static_cast<uint16_t>(objectId);
    sound->mSoundId = soundId;
    sound->mVolume = volume;
    sound->Update();
    sound->SetMonitor(monitor);
    mSounds.push_back(sound);
    return true;
}

// 0x229ed2 (67 B): the first sound of that id detached and dropped.
bool cTSAudioTSObjectUpdateDemon::RemoveSound(uint32_t soundId)
{
    for (auto it = mSounds.begin(); it != mSounds.end(); ++it) {
        if ((*it)->mSoundId != soundId) continue;
        (*it)->Detach();
        mSounds.erase(it);
        return true;
    }
    return false;
}

// 0x229c85 (125 B): a start (key 0x8bd63096 non-zero) adds the monitor and the sound, a stop removes the sound.
bool cTSAudioTSObjectUpdateDemon::OnSoundEvent(cITSAudioEvent *event)
{
    if (!event) return false;
    if (event->GetUint32(kStartKey)) {
        AddMonitor(event->GetUint32(kObjectIdKey));
        const float volume = event->GetFloat(kVolumeKey);
        const uint32_t soundId = event->GetUint32(kSoundIdKey);
        AddSound(event->GetUint32(kObjectIdKey), soundId, volume);
    } else {
        RemoveSound(event->GetUint32(kSoundIdKey));
    }
    return true;
}

// 0x229aff (45 B): the first monitor whose id is the low word.
tMonitorEntry *cTSAudioTSObjectUpdateDemon::FindMonitor(uint32_t objectId)
{
    for (const cRZAutoRefCount<tMonitorEntry> &monitor : mMonitors)
        if (monitor->mObjectId == static_cast<uint16_t>(objectId)) return monitor.Get();
    return nullptr;
}

// 0x229b2c (44 B).
tPlayingSound *cTSAudioTSObjectUpdateDemon::FindSound(uint32_t soundId)
{
    for (const cRZAutoRefCount<tPlayingSound> &sound : mSounds)
        if (sound->mSoundId == soundId) return sound.Get();
    return nullptr;
}

// 0x229b64 (111 B): each first monitor held, dropped from the list, detached.
void cTSAudioTSObjectUpdateDemon::ClearMonitors()
{
    while (!mMonitors.empty()) {
        cRZAutoRefCount<tMonitorEntry> monitor(mMonitors.front());
        mMonitors.pop_front();
        monitor->Detach();
    }
}

// 0x229bd3 (111 B): as ClearMonitors for the sounds.
void cTSAudioTSObjectUpdateDemon::ClearSounds()
{
    while (!mSounds.empty()) {
        cRZAutoRefCount<tPlayingSound> sound(mSounds.front());
        mSounds.pop_front();
        sound->Detach();
    }
}

// 0x229d02 (19 B).
bool cTSAudioTSObjectUpdateDemon::ClearAll()
{
    ClearMonitors();
    ClearSounds();
    return true;
}

// 0x22a593 (26 B).
void cTSAudioTSObjectUpdateDemon::UpdateMonitors()
{
    for (const cRZAutoRefCount<tMonitorEntry> &monitor : mMonitors) monitor->Update();
}

// 0x22995a (93 B): the first monitor (held) stepped, then moved to the back.
void cTSAudioTSObjectUpdateDemon::StepNextMonitor()
{
    if (mMonitors.empty()) return;
    cRZAutoRefCount<tMonitorEntry> monitor(mMonitors.front());
    monitor->Step();
    RotateMonitors();
}

// 0x22a05e (33 B): a copy of the first at the back, then the first erased.
void cTSAudioTSObjectUpdateDemon::RotateMonitors()
{
    mMonitors.push_back(mMonitors.front());
    mMonitors.pop_front();
}

// 0x22ab39 (26 B).
void cTSAudioTSObjectUpdateDemon::UpdateSounds()
{
    for (const cRZAutoRefCount<tPlayingSound> &sound : mSounds) sound->Update();
}

// 0x22a07f (18 B).
uint32_t cTSAudioTSObjectUpdateDemon::Seconds() { return mWatch.Elapsed() / 1000; }
