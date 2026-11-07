// nTSCASCore::cTSCASBodyPartSystem (lane JZ): slots 26 and 27, which build a skin / hair material from the property set
// of a queried object and register its texture-material resource. The face's `this` is the subobject at +0x1c; see
// cTSCASBodyPartSystem.h.
#include "sg/cTextureManager.h"
#include "cas/cTSCASBodyPartSystem.h"

#include "game/original.h"
#include "game/vtable.h"
#include "misc/UtilitiesE.h" // GetCachedQueriedObject
#include "rz/ServiceGetters.h"

#include <cstdlib>
#include <cstring>

namespace nTSCASCore {

namespace {

// The image's texts (read where they are).
constexpr uint32_t kRvaParamAge = 0xeba564;          // "paramAge"
constexpr uint32_t kRvaParamGender = 0xeba570;       // "paramGender"
constexpr uint32_t kRvaParamSpecies = 0xeba57c;      // "paramSpecies"
constexpr uint32_t kRvaNameFormat = 0xeba58c;        // a generated material name: serial and two numbers
constexpr uint32_t kRvaNumberFormat = 0xd9b648;      // one number
constexpr uint32_t kRvaHairFlag = 0xe8bbfc;          // "simHairFlag"
constexpr uint32_t kRvaAlphaBlendMode = 0xe8bc08;    // "stdMatAlphaBlendMode"
constexpr uint32_t kRvaBlend = 0xdbcb18;             // "blend"
constexpr uint32_t kRvaEyeGizmo = 0xe83af8;          // "eyeGizmoEnable"
constexpr uint32_t kRvaTrue = 0xda2d44;              // the value both flags are set to
constexpr uint32_t kRvaOverlayCount = 0xdbc964;      // "cafNumOverlays"
constexpr uint32_t kRvaNoOverlays = 0xda4af4;        // the count set when the properties have none
constexpr uint32_t kRvaOverlayFormat = 0xda6db8;     // an overlay property's name: a prefix and an index
constexpr uint32_t kRvaOverlayPrefix = 0xdbc974;
constexpr uint32_t kRvaTxmtSuffix = 0xdb0810;        // "_txmt"

const char *Text(uint32_t rva)
{
    return GameAddress<const char>(rva);
}

// The queried object and the property set it holds for `id`. Slot 33 writes the whole 0x14-byte entry: an 8-byte out
// here let it overwrite the caller's saved `this`, which crashed the game at lot load (see MaterialEntryRef).
void *LookUp(void *queried, uint32_t id)
{
    nGZSceneGraph::TextureManager(); // makes sure its cached service exists (result unused)
    const uint32_t key = VirtualCall<uint32_t(__thiscall *)(void *, uint32_t), 19>(queried, id);
    MaterialEntryRef found;
    VirtualCall<void(__thiscall *)(void *, MaterialEntryRef *, uint32_t), 33>(queried, &found, key);
    return found.mProperties;
}

const char *Property(void *props, const char *name)
{
    return VirtualCall<const char *(__thiscall *)(void *, const char *), 24>(props, name);
}

void SetParameter(void *material, const char *name, const char *value)
{
    VirtualCall<void(__thiscall *)(void *, const char *, const char *), 13>(material, name, value);
}

// The property set's material (slot 7), held while the slot runs.
struct HeldMaterial {
    void *mObject;
    explicit HeldMaterial(void *props) : mObject(VirtualCall<void *(__thiscall *)(void *), 7>(props))
    {
        if (mObject != nullptr) VirtualCall<uint32_t(__thiscall *)(void *), 1>(mObject);
    }
    HeldMaterial(const HeldMaterial &) = delete;
    ~HeldMaterial()
    {
        if (mObject != nullptr) VirtualCall<uint32_t(__thiscall *)(void *), 2>(mObject);
    }
};

// The material's name: `given`, or the next serial number whose name the queried object does not know yet.
void NameMaterial(BodyPartSystemIface *self, void *queried, LocalRZString &name, const char *given, uint32_t a, uint32_t b)
{
    if (given != nullptr) {
        name.mText.mString.assign(given, std::strlen(given));
        return;
    }
    do {
        ++self->mMaterialSerial;
        name.mText.Sprintf(Text(kRvaNameFormat), self->mMaterialSerial, a, b);
    } while (VirtualCall<bool(__thiscall *)(void *, const char *), 31>(queried, name.mText.mString.c_str()));
}

// What both slots end with: the material handed to the queried object, its "_txmt" resource keyed and registered.
uint32_t FinishMaterial(void *queried, void *material, LocalRZString &name, uint32_t resourceArg, uint32_t keyArg)
{
    VirtualCall<void(__thiscall *)(void *, void *, uint32_t), 23>(queried, material, 2);
    void *const resource = VirtualCall<void *(__thiscall *)(void *), 9>(material);
    name.mText.mString.append(Text(kRvaTxmtSuffix), 5);
    LocalRZString key;
    nGZSceneGraph::ResolveResourceName(key.mText.AsInterface(), name.mText.mString.c_str(), keyArg, nullptr);
    VirtualCall<void(__thiscall *)(void *, const char *, uint32_t), 12>(resource, key.mText.mString.c_str(), resourceArg);
    VirtualCall<void(__thiscall *)(void *, uint32_t), 16>(resource, 1);
    VirtualCall<void(__thiscall *)(void *), 10>(resource);
    void *const persist = CachedServiceGetter<0x10faf6c, 0x056b906e, 0x9af5e924>();
    void *const registry = VirtualCall<void *(__thiscall *)(void *), 3>(persist);
    void *const owner = VirtualCall<void *(__thiscall *)(void *), 8>(resource);
    uint8_t scratch[16];
    void *const resourceKey = VirtualCall<void *(__thiscall *)(void *, void *), 3>(owner, scratch);
    VirtualCall<void(__thiscall *)(void *, void *), 10>(registry, resourceKey);
    return VirtualCall<uint32_t(__thiscall *)(void *), 19>(material);
}

} // namespace

uint32_t BodyPartSystemIface::CreateSkinMaterial(uint32_t id, uint32_t resourceArg, const char *name, uint32_t keyArg, uint8_t eyeGizmo)
{
    void *const queried = GetCachedQueriedObject();
    void *const props = LookUp(queried, id); // not checked for null, as in the original
    if (VirtualCall<uint32_t(__thiscall *)(void *), 20>(props) == 0) return 0;
    const char *const ageText = Property(props, Text(kRvaParamAge));
    const char *const genderText = Property(props, Text(kRvaParamGender));
    if (ageText == nullptr || genderText == nullptr) return 0;
    const int age = std::atoi(ageText);
    const int gender = std::atoi(genderText);
    HeldMaterial material(props);
    LocalRZString materialName;
    NameMaterial(this, queried, materialName, name, static_cast<uint32_t>(age), static_cast<uint32_t>(gender));
    VirtualCall<void(__thiscall *)(void *, const char *), 10>(material.mObject, materialName.mText.mString.c_str());
    if (eyeGizmo) SetParameter(material.mObject, Text(kRvaEyeGizmo), Text(kRvaTrue));
    const char *const overlays = Property(props, Text(kRvaOverlayCount));
    if (overlays == nullptr) {
        SetParameter(material.mObject, Text(kRvaOverlayCount), Text(kRvaNoOverlays));
    } else {
        SetParameter(material.mObject, Text(kRvaOverlayCount), overlays);
        const int count = std::atoi(overlays);
        for (int i = 0; i < count; ++i) {
            LocalRZString overlay;
            overlay.mText.Sprintf(Text(kRvaOverlayFormat), Text(kRvaOverlayPrefix), i);
            const char *const value = Property(props, overlay.mText.mString.c_str());
            SetParameter(material.mObject, overlay.mText.mString.c_str(), value);
        }
    }
    return FinishMaterial(queried, material.mObject, materialName, resourceArg, keyArg);
}

uint32_t BodyPartSystemIface::CreateHairMaterial(uint32_t id, uint32_t resourceArg, const char *name, uint32_t keyArg, uint32_t a, uint32_t b,
                                                 uint32_t species, uint8_t hair)
{
    void *const queried = GetCachedQueriedObject();
    void *const props = LookUp(queried, id);
    if (props == nullptr || VirtualCall<uint32_t(__thiscall *)(void *), 20>(props) == 0) return 0;
    HeldMaterial material(props);
    LocalRZString materialName;
    NameMaterial(this, queried, materialName, name, a, b);
    VirtualCall<void(__thiscall *)(void *, const char *), 10>(material.mObject, materialName.mText.mString.c_str());
    LocalRZString speciesText;
    speciesText.mText.Sprintf(Text(kRvaNumberFormat), species);
    SetParameter(material.mObject, Text(kRvaParamSpecies), speciesText.mText.mString.c_str());
    if (hair) {
        SetParameter(material.mObject, Text(kRvaHairFlag), Text(kRvaTrue));
        SetParameter(material.mObject, Text(kRvaAlphaBlendMode), Text(kRvaBlend));
    }
    return FinishMaterial(queried, material.mObject, materialName, resourceArg, keyArg);
}

} // namespace nTSCASCore
