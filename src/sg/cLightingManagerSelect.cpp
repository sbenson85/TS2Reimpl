// nTSSG::cLightingManager's light-set selectors (lane 261110q, scenegraph LG3c): the set for the time of day 0x2e4480,
// the set by name 0x2e10a0 and its two helpers, the hood time 0x2e29f0 and the lot skirt's skybox texture 0x2dfe10.
// See the header for what each does.
#include "sg/cLightingManager.h"

#include "app/AppInit.h"         // HasOptionBits
#include "game/original.h"
#include "game/vtable.h"
#include "misc/ClassesRound4.h"  // BitMask
#include "misc/UtilitiesE.h"     // GetCachedQueriedObject: the material manager
#include "rz/ServiceGetters.h"   // CachedServiceGetter
#include "rz/Singleton_20ddfb.h" // GetCachedSingleton
#include "rz/cRZString.h"
#include "sg/cMaterialManager.h"
#include "sg/cShape.h"           // ShapeMaterialHandle
#include "ts/cTSGlobals.h"
#include "ts/cTSNeighborhoodInfo.h"

#include <cstring>
#include <string>

namespace nTSSG {

namespace {

constexpr uint32_t kPortalSwitch = 0xffde40; // the portal lights built / applied: the sets' "NoPortals" forms when clear
constexpr uint32_t kOptionSeasons = 7;       // the option bit (BitMask) of the weather and seasons

bool Is(const char *name, const char *literal) { return std::strcmp(name, literal) == 0; }

// 0x20df9a (game code): the clock (cell 0x10cc2ac, else the singleton's slot 3); its slot 15 (4) is the time of day.
void *Clock() { return OriginalFunction<void *(__cdecl *)(), 0x20df9a>()(); }

// 0xb39000 (game code): the effects manager (cell 0x10faf84); its slot 31 (index, value) switches an effect.
void *Effects() { return OriginalFunction<void *(__cdecl *)(), 0xb39000>()(); }

// The scene-graph system (cTSSGSystem, cell 0x10cc458).
void *SceneGraphSystem() { return CachedServiceGetter<0x10cc458, 0x9a353ce8, 0xfa353cdb>(); }

cRZString &AsRZString(const void *&vtable) { return *reinterpret_cast<cRZString *>(&vtable); }

} // namespace

void cLightingManager::SetSkyMode(uint32_t kind)
{
    mSkyMode = kind;
    void *const effects = Effects();
    using SetEffect = void(__thiscall *)(void *, uint32_t, uint32_t);
    switch (mSkyMode) {
    case 0:
        VirtualCall<SetEffect, 31>(effects, 0u, 0u);
        VirtualCall<SetEffect, 31>(effects, 1u, 1u);
        break;
    case 1:
        VirtualCall<SetEffect, 31>(effects, 0u, 1u);
        VirtualCall<SetEffect, 31>(effects, 1u, 0u);
        break;
    case 2:
        VirtualCall<SetEffect, 31>(effects, 0u, 1u);
        VirtualCall<SetEffect, 31>(effects, 1u, 1u);
        break;
    case 3:
        VirtualCall<SetEffect, 31>(effects, 0u, 0u);
        VirtualCall<SetEffect, 31>(effects, 1u, 0u);
        break;
    default:
        break;
    }
}

void cLightingManager::SetSkyboxCubeTexture(const char *material, const char *texture)
{
    auto *const materials = static_cast<nGZSceneGraph::cIMaterialManager *>(GetCachedQueriedObject());
    if (!materials->IsLoaded(material)) return;
    // 0xae8840: the named material found or loaded, then counted by the handle
    const nGZSceneGraph::ShapeMaterialHandle handle(static_cast<int32_t>(
        static_cast<nGZSceneGraph::cIMaterialManager *>(GetCachedQueriedObject())->FindOrLoad(material)));
    const nGZSceneGraph::MaterialInfo info = materials->GetInfo(handle.mIndex);
    // the definition's slot 7 object; its slot 13 sets a texture by name (the object is not checked for null there)
    void *const object = VirtualCall<void *(__thiscall *)(void *), 7>(info.material);
    if (object) VirtualCall<uint32_t(__thiscall *)(void *), 1>(object);
    VirtualCall<void(__thiscall *)(void *, const char *, const char *), 13>(object, "skyboxCubeTexture", texture);
    materials->AddMaterial(static_cast<cIGZUnknown *>(object), 0);
    if (object) VirtualCall<uint32_t(__thiscall *)(void *), 2>(object);
}

bool cLightingManager::SelectHoodLightSet(const char *name)
{
    if (Is(name, "day") || Is(name, "neighbourhoodImposter") || Is(name, "neighbourhood") || Is(name, "overcast") ||
        Is(name, "spring") || Is(name, "autumn") || Is(name, "winter"))
        SetSkyMode(1);
    else if (Is(name, "night") || Is(name, "demoNight") || Is(name, "neighborhoodnight") || Is(name, "vacationNight"))
        SetSkyMode(3);
    else if (Is(name, "morning"))
        SetSkyMode(0);
    else if (Is(name, "evening"))
        SetSkyMode(2);

    if (Is(name, "neighborhoodnight") || Is(name, "neighbourhood"))
        VirtualCall<void(__thiscall *)(void *), 31>(SceneGraphSystem()); // the neighbourhood's refresh requested
    else if (Is(name, "day"))
        SetSkyboxCubeTexture("lotSkirtReflectionSkybox", "skymap-generic-day-highres-envcube");
    else if (Is(name, "night"))
        SetSkyboxCubeTexture("lotSkirtReflectionSkybox", "skymap-generic-night-envcube");

    if (*GameAddress<const uint8_t>(kPortalSwitch) == 0) {
        cRZString plain;
        plain.Construct(name);
        plain.mString.append("NoPortals", 9);
        const int32_t index = FindLightSet(*plain.AsInterface());
        if (index != -1) {
            mCurrentSet = static_cast<uint32_t>(index);
            mName.assign(name);
            plain.Destruct();
            return true;
        }
        plain.Destruct();
    }
    cRZString lookup;
    lookup.Construct(name);
    const int32_t index = FindLightSet(*lookup.AsInterface());
    lookup.Destruct();
    if (index == -1) return false;
    mCurrentSet = static_cast<uint32_t>(index);
    mName.assign(name);
    return true;
}

void cLightingManager::SelectTimeOfDayLightSet()
{
    if (!mInitialised || !mUseDefaultName) return;
    const int16_t time = static_cast<int16_t>(VirtualCall<uint16_t(__thiscall *)(void *, uint32_t), 15>(Clock(), 4u));
    cRZString name;
    name.Construct("day");
    bool day = false;
    switch (time) {
    case 0: day = true; break;
    case 1:
        if (!mMorningEvening) day = true;
        else name.mString.assign("evening");
        break;
    case 2: name.mString.assign("night"); break;
    case 3: name.mString.assign(mMorningEvening ? "morning" : "night"); break;
    default: break;
    }

    auto *const globals = static_cast<cITSGlobals *>(GetCachedSingleton());
    void *const neighborhood = globals->GetNeighborhood();
    // the neighbourhood's slot 15: its record
    auto *const info = static_cast<cITSNeighborhoodInfo *>(VirtualCall<void *(__thiscall *)(void *), 15>(neighborhood));
    if (!day) {
        if (info->IsKind567()) name.mString.assign("vacationNight");
    } else if (static_cast<uint8_t>(appinit::HasOptionBits(BitMask(kOptionSeasons)))) {
        // the weather (the singleton's slot 33): slot 35 1 overcast, else slot 17 the season
        if (void *const weather = static_cast<cITSGlobals *>(GetCachedSingleton())->GetField94()) {
            if (VirtualCall<uint32_t(__thiscall *)(void *), 35>(weather) == 1) {
                name.mString.assign("overcast");
            } else {
                switch (VirtualCall<uint32_t(__thiscall *)(void *), 17>(weather)) {
                case 0: name.mString.assign("spring"); break;
                case 2: name.mString.assign("autumn"); break;
                case 3: name.mString.assign("winter"); break;
                default: break;
                }
            }
        }
        if (FindLightSet(*name.AsInterface()) == -1) name.mString.assign("day");
    } else if (!static_cast<uint8_t>(appinit::HasOptionBits(BitMask(kOptionSeasons))) && info->IsKind567()) {
        if (info->GetKind() == 5) name.mString.assign("spring");
        else if (info->GetKind() == 6) name.mString.assign("autumn");
    }

    cRZString old;
    old.Construct(AsRZString(mNameVTable), 0, 0xffffffffu);
    SelectHoodLightSet(name.ToChar());
    if (old.mString != mName) {
        ApplyLightSet();
        mDirty = 1;
        mFlag83 = 1;
    }
    old.Destruct();
    name.Destruct();
}

} // namespace nTSSG
