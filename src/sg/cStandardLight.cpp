// The standard lights (lane OR). Layout, faces and the switch: see the header.
#include "sg/SGEventRecords.h"
#include "sg/cStandardLight.h"

#include "compgeom/CompGeom.h" // NearlyEqual
#include "game/game.h"
#include "game/original.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "misc/SmallVirtuals.h" // nGZEvent::cNullEvent::kVTable
#include "gz/cEventFaces.h"    // nGZEvent::NullEventVTable (the owned word)
#include "sg/SGLocalString.h"
#include "sg/cLinearFogLight.h"      // cSimpleLightBehaviorTraits
#include "sg/cObjectGraphNodeImpl.h" // NewTypeName

#include <cmath>
#include <optional>

namespace nGZSceneGraph {

static_assert(sizeof(cAmbientLight) == cAmbientLight::kSize, "cAmbientLight size");
static_assert(sizeof(cDirectionalLight) == cDirectionalLight::kSize, "cDirectionalLight size");
static_assert(offsetof(cStandardLightBase, mIntensity) == 0x4c, "cStandardLightBase intensity");
static_assert(offsetof(cStandardLightBase, mFactor) == 0x50, "cStandardLightBase factor");
static_assert(offsetof(cStandardLightBase, mColor) == 0x54, "cStandardLightBase colour");
static_assert(offsetof(cStandardLightBase, mLightName) == 0x60, "cStandardLightBase name");
static_assert(offsetof(cAmbientLight, mStrength) == 0x64, "cAmbientLight strength");
static_assert(offsetof(cDirectionalLight, mStrength) == 0x64, "cDirectionalLight strength");

namespace {
uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// |colour|: the squares summed in float, the root taken in double and rounded back (0xa48230 / 0xa482b0).
float ColorLength(const float *c)
{
    const float sum = c[0] * c[0] + c[1] * c[1] + c[2] * c[2];
    return static_cast<float>(std::sqrt(static_cast<double>(sum)));
}
} // namespace

namespace {
std::optional<nGZEvent::cEventAttributeMap> gStandardLightRegistry;
}

// Built on first use. The original runs its holder 0xa457f0 first (on every call, a once byte skipping it after the first):
// the object-graph events (0x2570a0), the shared simple-light map (0x3a5d00) given the record 0x106abb0 under 0xdd041f30 (the
// shape's holder stores its own record there when it runs), then 0x69f75eba through this function, whose map is made by
// that call as a copy of the shared map (0xa480c0). (The holder's 6-byte object, kept in 0x10f6154, is read by nothing.)
nGZEvent::cEventAttributeMap *__cdecl cStandardLightBehaviorTraits::RegistryFor(uint32_t id,
                                                                              const nGZEvent::cEventAttribute *attribute)
{
    if (!gStandardLightRegistry) {
        cObjectGraphBehaviorTraits::RegisterEvents();
        cSimpleLightBehaviorTraits::RegistryFor(0xdd041f30, &sgevents::kBoundsUpdated);
        gStandardLightRegistry.emplace(*SimpleLightEventRegistry());
        (*gStandardLightRegistry)[0x69f75eba] = sgevents::kLightDependentAttributesChanged;
    }
    if (id != 0) (*gStandardLightRegistry)[id] = *attribute;
    return &*gStandardLightRegistry;
}

void cStandardLightBehaviorTraits::ResetRegistryForTest() { gStandardLightRegistry.reset(); }

nGZEvent::cEventAttributeMap *StandardLightEventRegistry()
{
    const nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
    return cStandardLightBehaviorTraits::RegistryFor(0, &defaults);
}

// ---- cStandardLightBase ----

cStandardLightBase::cStandardLightBase() = default;
cStandardLightBase::~cStandardLightBase() { mLightName.Close(); }

void cStandardLightBase::OnLightChanged()
{
    const void *nullEvent = nGZEvent::NullEventVTable();
    NotifyListeners(kChangedEvent, 0, &nullEvent);
}

bool cStandardLightBase::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == cIStandardLight::kIid) {
        *out = static_cast<cIStandardLight *>(this);
        AddRef();
        return true;
    }
    return Light::QueryInterface(iid, out);
}

bool cStandardLightBase::IsKindOf(uint32_t iid) { return iid == cIStandardLight::kIid || Light::IsKindOf(iid); }

void cStandardLightBase::SetLightName(const char *name)
{
    ServiceCell temp;
    mLightName.Assign(&temp.Open(Word(name), 0)->mValue);
    temp.Close();
    const void *nullEvent = nGZEvent::NullEventVTable();
    NotifyListeners(kChangedEvent, 0, &nullEvent);
}

void cStandardLightBase::SetColor(const float *rgb)
{
    if (nGZCompGeom::NearlyEqual(mColor[0], rgb[0]) && nGZCompGeom::NearlyEqual(mColor[1], rgb[1]) &&
        nGZCompGeom::NearlyEqual(mColor[2], rgb[2]))
        return;
    mColor[0] = rgb[0];
    mColor[1] = rgb[1];
    mColor[2] = rgb[2];
    OnLightChanged();
}

void cStandardLightBase::SetIntensity(float value)
{
    if (mIntensity == value) return;
    mIntensity = value;
    OnLightChanged();
}

void cStandardLightBase::SetFactor(float value)
{
    if (mFactor == value) return;
    mFactor = value;
    OnLightChanged();
}

const char *cStandardLightBase::GetLightName()
{
    if (mLightName.mValue == kNoName) return nullptr;
    return reinterpret_cast<const char *>(static_cast<uintptr_t>(Dispatch8(&mLightName)));
}

float *cStandardLightBase::GetColor(float *rgb)
{
    rgb[0] = mColor[0];
    rgb[1] = mColor[1];
    rgb[2] = mColor[2];
    return rgb; // 0xa47400 leaves eax = the out pointer; the lighting manager (0x3c2e60) reads through it
}

void cStandardLightBase::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cSGResource::Read(reader);
    Light::Read(reader);
    {
        nTSSG::SGLocalString text;
        reader->GetGZStr(&text.value);
        ServiceCell temp;
        mLightName.Assign(&temp.Open(Word(text.Text()), 0)->mValue);
        temp.Close();
        mIntensity = reader->GetFloat32();
        mFactor = reader->GetFloat32();
        mColor[0] = reader->GetFloat32();
        mColor[1] = reader->GetFloat32();
        mColor[2] = reader->GetFloat32();
        OnLightChanged();
    }
    if (info.mName) info.mName->Release();
}

cPersistTypeInfo cStandardLightBase::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cStandardLightBase");
    info.mWord4 = 0;
    info.mVersion = kVersion;
    return info;
}

void cStandardLightBase::Write(ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info = cStandardLightBase::GetTypeInfo();
    writer->WriteTypeInfo(&info);
    if (info.mName) info.mName->Release();
    cSGResource::WriteResource(writer);
    Light::Write(writer);
    writer->WriteRZCharStr(reinterpret_cast<char *>(static_cast<uintptr_t>(Dispatch8(&mLightName))));
    writer->SetFloat32(mIntensity);
    writer->SetFloat32(mFactor);
    writer->SetFloat32(mColor[0]);
    writer->SetFloat32(mColor[1]);
    writer->SetFloat32(mColor[2]);
}

void cStandardLightBase::CopyTo(cStandardLightBase *target, uint32_t mode)
{
    mImpl.CopyTo(this, target, mode, nullptr);
    target->mIntensity = mIntensity;
    target->mFactor = mFactor;
    target->mColor[0] = mColor[0];
    target->mColor[1] = mColor[1];
    target->mColor[2] = mColor[2];
    target->mLightName.Assign(&mLightName.mValue);
}

// ---- cAmbientLight ----

// (The original's operator new throws on failure, so its null branch is dead.)
cAmbientLight::cAmbientLight() = default;
cAmbientLight::~cAmbientLight() = default;

cIStandardLight *__cdecl cAmbientLight::Create() { return new cAmbientLight; }

void cAmbientLight::OnLightChanged()
{
    mStrength = ColorLength(mColor) * mIntensity;
    cStandardLightBase::OnLightChanged();
}

void cAmbientLight::Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *)
{
    cAmbientLight *const clone = new cAmbientLight;
    CopyContentTo(clone, mode);
    clone->QueryInterface(iid, out);
}

void cAmbientLight::GetParams(float *out, uint32_t)
{
    out[0] = mColor[0] * mIntensity;
    out[1] = mColor[1] * mIntensity;
    out[2] = mColor[2] * mIntensity;
    out[3] = 0.0f;
    out[4] = 0.0f;
    out[5] = 0.0f;
    out[6] = 0.0f;
    out[7] = 0.0f;
}

void cAmbientLight::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cStandardLightBase::Read(reader);
    if (info.mName) info.mName->Release();
}

cPersistTypeInfo cAmbientLight::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cAmbientLight");
    info.mWord4 = kClassId;
    info.mVersion = 1;
    return info;
}

void cAmbientLight::Write(ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info = cAmbientLight::GetTypeInfo();
    writer->WriteTypeInfo(&info);
    if (info.mName) info.mName->Release();
    cStandardLightBase::Write(writer);
}

void cAmbientLight::CopyContentTo(cAmbientLight *target, uint32_t mode)
{
    CopyTo(target, mode);
    target->mStrength = mStrength;
}

// ---- cDirectionalLight ----

cDirectionalLight::cDirectionalLight() = default;
cDirectionalLight::~cDirectionalLight() = default;

cIStandardLight *__cdecl cDirectionalLight::Create() { return new cDirectionalLight; }

void cDirectionalLight::OnLightChanged()
{
    mStrength = ColorLength(mColor) * mIntensity * kStrengthScale;
    cStandardLightBase::OnLightChanged();
}

void cDirectionalLight::Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *)
{
    cDirectionalLight *const clone = new cDirectionalLight;
    CopyContentTo(clone, mode);
    clone->QueryInterface(iid, out);
}

void cDirectionalLight::GetParams(float *out, uint32_t)
{
    out[0] = mColor[0] * mIntensity;
    out[1] = mColor[1] * mIntensity;
    out[2] = mColor[2] * mIntensity;
    out[3] = mFactor;
    out[4] = 0.0f;
    out[5] = 0.0f;
    out[6] = 0.0f;
    out[7] = 0.0f;
}

void cDirectionalLight::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cStandardLightBase::Read(reader);
    if (info.mName) info.mName->Release();
}

cPersistTypeInfo cDirectionalLight::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cDirectionalLight");
    info.mWord4 = kClassId;
    info.mVersion = 1;
    return info;
}

void cDirectionalLight::Write(ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info = cDirectionalLight::GetTypeInfo();
    writer->WriteTypeInfo(&info);
    if (info.mName) info.mName->Release();
    cStandardLightBase::Write(writer);
}

void cDirectionalLight::CopyContentTo(cDirectionalLight *target, uint32_t mode)
{
    CopyTo(target, mode);
    target->mStrength = mStrength;
}

// ---- the RVA hooks' entry points (lane VW) ----

using Adapters = cStandardLightAdapters;

bool __fastcall Adapters::QueryInterface(void *face, void *, uint32_t iid, void **out)
{
    [[msvc::forceinline_calls]] return FromPart(face)->cStandardLightBase::QueryInterface(iid, out);
}

uint32_t __fastcall Adapters::Release(void *face, void *)
{
    [[msvc::forceinline_calls]] return FromPart(face)->cStandardLightBase::Release();
}

void __fastcall Adapters::OnLightChanged(void *face, void *)
{
    [[msvc::forceinline_calls]] FromPart(face)->cStandardLightBase::OnLightChanged();
}

void __fastcall Adapters::AmbientOnLightChanged(void *face, void *)
{
    [[msvc::forceinline_calls]] AmbientFrom(FromPart(face))->cAmbientLight::OnLightChanged();
}

void __fastcall Adapters::DirectionalOnLightChanged(void *face, void *)
{
    [[msvc::forceinline_calls]] DirectionalFrom(FromPart(face))->cDirectionalLight::OnLightChanged();
}

bool __fastcall Adapters::NodeQueryInterface(void *face, void *, uint32_t iid, void **out)
{
    [[msvc::forceinline_calls]] return FromNode(face)->cStandardLightBase::QueryInterface(iid, out);
}

uint32_t __fastcall Adapters::NodeRelease(void *face, void *)
{
    [[msvc::forceinline_calls]] return FromNode(face)->cStandardLightBase::Release();
}

void __fastcall Adapters::NotifyListeners(void *face, void *, uint32_t type, uint32_t arg1, void *data)
{
    [[msvc::forceinline_calls]] FromNode(face)->cStandardLightBase::NotifyListeners(type, arg1, data);
}

bool __fastcall Adapters::AskListeners(void *face, void *, uint32_t type, uint32_t arg1, void *data)
{
    [[msvc::forceinline_calls]] return FromNode(face)->cStandardLightBase::AskListeners(type, arg1, data);
}

uint32_t __fastcall Adapters::GetEventCount(void *face, void *)
{
    [[msvc::forceinline_calls]] return FromNode(face)->cStandardLightBase::GetEventCount();
}

nGZEvent::cAttributeIDRange *__fastcall Adapters::GetEventIds(void *face, void *, nGZEvent::cAttributeIDRange *out)
{
    [[msvc::forceinline_calls]] *out = FromNode(face)->cStandardLightBase::GetEventIds();
    return out;
}

bool __fastcall Adapters::IsKindOf(void *face, void *, uint32_t iid)
{
    [[msvc::forceinline_calls]] return FromNode(face)->cStandardLightBase::IsKindOf(iid);
}

cIStandardLight *__fastcall Adapters::GetStandardLight(void *face, void *)
{
    [[msvc::forceinline_calls]] return FromNode(face)->cStandardLightBase::GetStandardLight();
}

void __fastcall Adapters::SetLightName(void *face, void *, const char *name)
{
    [[msvc::forceinline_calls]] FromNode(face)->cStandardLightBase::SetLightName(name);
}

void __fastcall Adapters::SetColor(void *face, void *, const float *rgb)
{
    [[msvc::forceinline_calls]] FromNode(face)->cStandardLightBase::SetColor(rgb);
}

void __fastcall Adapters::SetIntensity(void *face, void *, float value)
{
    [[msvc::forceinline_calls]] FromNode(face)->cStandardLightBase::SetIntensity(value);
}

void __fastcall Adapters::SetFactor(void *face, void *, float value)
{
    [[msvc::forceinline_calls]] FromNode(face)->cStandardLightBase::SetFactor(value);
}

const char *__fastcall Adapters::GetLightName(void *face, void *)
{
    [[msvc::forceinline_calls]] return FromNode(face)->cStandardLightBase::GetLightName();
}

float *__fastcall Adapters::GetColor(void *face, void *, float *rgb)
{
    [[msvc::forceinline_calls]] return FromNode(face)->cStandardLightBase::GetColor(rgb);
}

float __fastcall Adapters::GetRange(void *face, void *)
{
    [[msvc::forceinline_calls]] return FromNode(face)->cStandardLightBase::GetRange();
}

void __fastcall Adapters::AmbientClone(void *face, void *, uint32_t mode, uint32_t iid, void **out,
                                       cINodeCloneContext *context)
{
    [[msvc::forceinline_calls]] AmbientFrom(FromNode(face))->cAmbientLight::Clone(mode, iid, out, context);
}

void __fastcall Adapters::DirectionalClone(void *face, void *, uint32_t mode, uint32_t iid, void **out,
                                           cINodeCloneContext *context)
{
    [[msvc::forceinline_calls]] DirectionalFrom(FromNode(face))->cDirectionalLight::Clone(mode, iid, out, context);
}

void __fastcall Adapters::DirectionalGetParams(void *face, void *, float *out, uint32_t unused)
{
    [[msvc::forceinline_calls]] DirectionalFrom(FromNode(face))->cDirectionalLight::GetParams(out, unused);
}

bool __fastcall Adapters::PersistQueryInterface(void *face, void *, uint32_t iid, void **out)
{
    [[msvc::forceinline_calls]] return FromPersist(face)->cStandardLightBase::QueryInterface(iid, out);
}

uint32_t __fastcall Adapters::PersistRelease(void *face, void *)
{
    [[msvc::forceinline_calls]] return FromPersist(face)->cStandardLightBase::Release();
}

void __fastcall Adapters::Read(void *face, void *, ifaces::cIGZPersistReader *reader)
{
    [[msvc::forceinline_calls]] FromPersist(face)->cStandardLightBase::Read(reader);
}

void __fastcall Adapters::Write(void *face, void *, ifaces::cIGZPersistWriter *writer)
{
    [[msvc::forceinline_calls]] FromPersist(face)->cStandardLightBase::Write(writer);
}

cPersistTypeInfo *__fastcall Adapters::AmbientGetTypeInfo(void *face, void *, cPersistTypeInfo *out)
{
    [[msvc::forceinline_calls]] *out = AmbientFrom(FromPersist(face))->cAmbientLight::GetTypeInfo();
    return out;
}

void __fastcall Adapters::AmbientWrite(void *face, void *, ifaces::cIGZPersistWriter *writer)
{
    [[msvc::forceinline_calls]] AmbientFrom(FromPersist(face))->cAmbientLight::Write(writer);
}

cPersistTypeInfo *__fastcall Adapters::DirectionalGetTypeInfo(void *face, void *, cPersistTypeInfo *out)
{
    [[msvc::forceinline_calls]] *out = DirectionalFrom(FromPersist(face))->cDirectionalLight::GetTypeInfo();
    return out;
}

void __fastcall Adapters::DirectionalWrite(void *face, void *, ifaces::cIGZPersistWriter *writer)
{
    [[msvc::forceinline_calls]] DirectionalFrom(FromPersist(face))->cDirectionalLight::Write(writer);
}

const void *__fastcall Adapters::AmbientRecord19(void *, void *) { return cAmbientLight::kRecord19; }

const void *__fastcall Adapters::DirectionalRecord19(void *, void *) { return cDirectionalLight::kRecord19; }

void __fastcall Adapters::CopyTo(Self *self, void *, Self *target, uint32_t mode)
{
    [[msvc::forceinline_calls]] self->cStandardLightBase::CopyTo(target, mode);
}

} // namespace nGZSceneGraph

// The decorated names of the constructors (`??0<class>@QAE@XZ`) and destructors (`??1<class>@UAE@XZ`): the linker fails
// if one is wrong.
#pragma comment(linker, "/alternatename:_cStandardLightBase_Constructor=??0cStandardLightBase@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_cAmbientLight_Constructor=??0cAmbientLight@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_cDirectionalLight_Constructor=??0cDirectionalLight@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_cStandardLightBase_Destructor=??1cStandardLightBase@nGZSceneGraph@@UAE@XZ")
#pragma comment(linker, "/alternatename:_cAmbientLight_Destructor=??1cAmbientLight@nGZSceneGraph@@UAE@XZ")
#pragma comment(linker, "/alternatename:_cDirectionalLight_Destructor=??1cDirectionalLight@nGZSceneGraph@@UAE@XZ")

extern "C" const int cStandardLight_Anchor = 1;
