// nTSSG::cTSSGSystem: ending the cinematic (lane IR). See src/sg/cTSSGSystem.h.
#include "sg/cTSSGSystem.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "rz/Singleton_20ddfb.h"
#include "rz/StaticGetters.h"
#include "rz/StaticGetters2.h"
#include "rz/StopwatchMore.h"

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

// Clears a reference held through its virtual base, then releases the old object there.
void DropVirtual(void *&ref)
{
    if (void *old = ref) {
        ref = nullptr;
        Call<2>(VirtualBase(old, 4, 1));
    }
}

// The cinematic stopwatch (a game global, mode 4).
constexpr uint32_t kCinematicWatch = 0x10cc848;
// The message the sound probe gets when a cinematic ends.
constexpr uint32_t kMessageClass = 0xcbc79579;
constexpr uint32_t kMessageIid = 0x0bc7957d;
constexpr uint32_t kMessageId = 0x8dcc55e7;
constexpr uint32_t kMessageKey = 0xadcc5609;
// Game text: the property the hiding value goes back to, and the three cinematic names.
constexpr uint32_t kObjectHiding = 0xdaf004;
constexpr uint32_t kCinematicNames[] = {0xdae494, 0xdae4a0, 0xdae4ac};
constexpr uint32_t kCinematicNameFlags = 0xf;

// The cached sound probe (0x9c5b7e) and the per-object cleanup helper (0xaeb0e0), not replaced yet.
void *SoundProbe() { return OriginalFunction<void *(__cdecl *)(), 0x9c5b7e>()(); }
void CleanUpNamed(void *object, uint32_t name)
{
    OriginalFunction<void(__cdecl *)(void *, uint32_t, uint32_t, const char *, uint32_t), 0xaeb0e0>()(
        object, 4, 0, GameAddress<const char>(name), kCinematicNameFlags);
}

// Slot 97 (0) and the three named cleanups.
void CleanUpActor(void *object)
{
    Call<97>(object, 0u);
    for (uint32_t name : kCinematicNames) CleanUpNamed(object, name);
}

// An interface reference released (slot 2) when it goes or is dropped.
struct Held {
    void *object = nullptr;
    Held() = default;
    Held(const Held &) = delete;
    Held &operator=(const Held &) = delete;
    ~Held() { Drop(); }
    void Drop()
    {
        if (void *old = object) {
            object = nullptr;
            Call<2>(old);
        }
    }
};

} // namespace

namespace nTSSG {

SGVirtualRef::~SGVirtualRef()
{
    if (mObject) Call<2>(VirtualBase(mObject, 4, 1));
}

void cTSSGSystem::EndCinematic(uint8_t abort)
{
    if (!mCinematic) return;
    if (mRef124.mObject) {
        if (!abort) {
            Call<2>(mRefB0.mObject, 0u);
            Call<1>(mRefB0.mObject, mCinematicRestore);
        }
        void *camera = Call<33, void *>(mRef124.mObject);
        // An unfinished camera without abort: the controller stays, so the cinematic goes on.
        if (camera && !Call<60, uint8_t>(camera) && !abort) return;
        auto *watch = GameAddress<cRZStopwatch>(kCinematicWatch);
        stopwatch_more::Stop(watch, nullptr);
        watch->Elapsed();
        Call<2>(mRefB0.mObject, 1u);
        if (void *probe = SoundProbe()) {
            Held message;
            void *framework = GetDirectorFrameworkObject();
            Call<3>(framework, kMessageClass, kMessageIid, &message.object);
            if (message.object) {
                Call<5>(message.object, kMessageId);
                Call<12>(message.object, kMessageKey, 0u);
                Call<12>(probe, message.object);
            }
        }
        if (void *settings = GetCachedSingletonB()) {
            Call<27>(settings, mCinematicSaved[0]);
            Call<29>(settings, mCinematicSaved[1]);
            Call<25>(settings, mCinematicSaved[2], 1u);
        }
        if (mObjectHiding) {
            if (void *properties = Call<21, void *>(GetCachedSingleton()))
                Call<13>(properties, GameAddress<const char>(kObjectHiding), mObjectHidingValue);
        }
        if (mWallManager.mObject) Call<21>(mWallManager.mObject, 0u);
        if (mWallManager.mObject) Call<16>(mWallManager.mObject);
        if (camera && !Call<60, uint8_t>(camera)) {
            void *view = Call<11, void *>(VirtualBase(mRefB0.mObject, 4, 2), 3u, 0u);
            Call<7>(camera, view);
            Call<6>(camera);
        }
        Call<7>(mRef124.mObject, 0u);
        DropVirtual(mRef124.mObject);
    }
    if (!abort && (mCinematicHeld || mRef124.mObject)) return;

    for (cTSSGCinematicActor *it = mCinematicActors.data(), *end = it + mCinematicActors.size(); it != end; ++it) {
        void *object = it->mObject.mObject;
        if (!object) continue;
        CleanUpActor(object);
        if (it->mRestore && Call<120, void *>(object)) {
            void *app = Call<26, void *>(GetCachedSingleton());
            const uint16_t value = Call<47, uint16_t>(Call<120, void *>(object), 1u);
            Call<18>(app, uint32_t(value));
        }
    }
    for (cTSSGCinematicActor *it = mCinematicExtras.data(), *end = it + mCinematicExtras.size(); it != end; ++it)
        CleanUpActor(it->mObject.mObject);
    mCinematicActors.clear();
    mCinematicExtras.clear();
    mCinematicRefs.clear();
    mCinematicHeld = 0;
    DropVirtual(mCinematicRef128);
    DropVirtual(mCinematic);
}

} // namespace nTSSG
