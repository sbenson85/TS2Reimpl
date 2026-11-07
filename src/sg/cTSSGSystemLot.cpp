// nTSSG::cTSSGSystem: clearing the lot (lane IR). See src/sg/cTSSGSystem.h.
#include "sg/cTSSGSystem.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "rz/StaticGetters2.h"

namespace {

using nTSSG::cTSSGSystem;
using nTSSG::SGRef;

template <int kSlot, typename R = uint32_t, typename... A> __forceinline R Call(const void *object, A... args)
{
    return VirtualCall<R(__thiscall *)(void *, A...), kSlot>(object, args...);
}

// Cached getters and helpers nobody has replaced yet.
void *ResourceManager() { return OriginalFunction<void *(__cdecl *)(), 0x20deb5>()(); }
void *SceneCache() { return OriginalFunction<void *(__cdecl *)(), 0x20de7c>()(); }
void *CarManager() { return OriginalFunction<void *(__cdecl *)(), 0xb39000>()(); }
void ReleaseSceneObject(void *object) { OriginalFunction<void(__cdecl *)(void *), 0xae3c00>()(object); }
void ReleaseCars(void *object) { OriginalFunction<void(__cdecl *)(void *), 0xb3c2b0>()(object); }

// Slot 4 (with `args`) on the object when there is one, then the reference cleared and released.
template <typename... A> void ShutDownAndDrop(SGRef &ref, A... args)
{
    if (!ref.mObject) return;
    Call<4>(ref.mObject, args...);
    if (void *old = ref.mObject) {
        ref.mObject = nullptr;
        Call<2>(old);
    }
}

// The part ClearLot and ClearLotForLoad share (up to the manager drops).
void ClearShared(cTSSGSystem &self, bool forLoad)
{
    if (void *rig = Call<12, void *>(self.mLightRigManager.mObject))
        Call<11>(self.mLightRigManager.mObject, rig, self.mSceneManager.mObject);
    if (forLoad) Call<7>(self.mLightingManager.mObject, self.mSceneManager.mObject);
    else Call<9>(self.mLightingManager.mObject, self.mSceneManager.mObject);
    if (Call<18, void *>(self.mSceneManager.mObject))
        ReleaseSceneObject(Call<18, void *>(self.mSceneManager.mObject));
    if (self.mRefAC.mObject) {
        Call<12>(self.mRefAC.mObject);
        const uint32_t count = Call<16>(self.mRefAC.mObject);
        for (uint32_t i = 0; i < count; ++i) {
            void *items = self.mRefAC.mObject;
            Call<7>(items, Call<5>(items, i));
        }
        Call<12>(self.mRefAC.mObject);
    }
    ShutDownAndDrop(self.mRef114, 1u);
    ShutDownAndDrop(self.mRef118, 1u);
    if (forLoad) ShutDownAndDrop(self.mRef110, 1u);
}

void ResetCars()
{
    void *cars = CarManager();
    ReleaseCars(Call<43, void *>(cars));
    Call<39>(cars, 0u);
    Call<40>(cars, 0u);
}

void FinishClear()
{
    Call<45>(ResourceManager());
    Call<25>(SceneCache(), 0u, 1u);
}

} // namespace

namespace nTSSG {

void cTSSGSystem::ClearLot()
{
    mFlag16B = 0;
    GetCachedSlot23();
    ClearShared(*this, false);
    ResetCars();
    ShutDownAndDrop(mRef84);
    ShutDownAndDrop(mNHoodOccupants);
    ShutDownAndDrop(mRef94);
    ShutDownAndDrop(mRef90);
    FinishClear();
}

void cTSSGSystem::ClearLotForLoad()
{
    mFlag16B = 0;
    ClearShared(*this, true);
    ShutDownAndDrop(mRef90);
    ShutDownAndDrop(mNHoodOccupants);
    ResetCars();
    Call<6>(mLotSkirt.mObject);
    Call<5>(mFenceManager.mObject);
    Call<12>(mFloorManager.mObject);
    Call<11>(mWallManager.mObject);
    Call<6>(mTerrain.mObject);
    Call<5>(mRoofManager.mObject);
    Call<6>(mPoolManager.mObject);
    Call<5>(mRugManager.mObject);
    Call<5>(mCurvedPoolManager.mObject);
    Call<7>(mTerrainSnowManager.mObject);
    Call<5>(mTerrainCutoutManager.mObject);
    Call<5>(mAwningManager.mObject);
    FinishClear();
    Call<6>(mLightMapManager.mObject);
}

} // namespace nTSSG
