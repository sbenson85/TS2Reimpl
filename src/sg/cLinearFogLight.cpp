// nGZSceneGraph::cLinearFogLight (lane OT). Layout, faces and the switch: see the header.
#include "sg/SGEventRecords.h"
#include "sg/cLinearFogLight.h"

#include "game/original.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "game/game.h"
#include "sg/cObjectGraphNodeImpl.h" // NewTypeName, cObjectGraphBehaviorTraits

#include <optional>

namespace nGZSceneGraph {

static_assert(sizeof(cLinearFogLight) == cLinearFogLight::kSize, "cLinearFogLight size");
static_assert(offsetof(cLinearFogLight, mNear) == 0x4c, "cLinearFogLight near");
static_assert(offsetof(cLinearFogLight, mFar) == 0x50, "cLinearFogLight far");
static_assert(offsetof(cLinearFogLight, mColor) == 0x54, "cLinearFogLight colour");

namespace {
std::optional<nGZEvent::cEventAttributeMap> gSimpleLightRegistry;
}

// The holder 0x3a3e60 (its object, kept in 0x10da040, is read by nothing) runs 0x3a4090 on the first call: 0x2570a0's work
// (the base events and the object-graph registry's two), then 0x3a5d00 itself with the event 0xdd041f30 (the record at
// 0x100b6e8), whose map is built by that call: a copy of the object-graph registry (0x25f030 is 0x264010 with id 0).
nGZEvent::cEventAttributeMap *__cdecl cSimpleLightBehaviorTraits::RegistryFor(uint32_t id,
                                                                            const nGZEvent::cEventAttribute *attribute)
{
    if (!gSimpleLightRegistry) {
        const nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
        gSimpleLightRegistry.emplace(*cObjectGraphBehaviorTraits::RegistryFor(0, &defaults));
        (*gSimpleLightRegistry)[0xdd041f30] = sgevents::kBoundsUpdated;
    }
    if (id != 0) (*gSimpleLightRegistry)[id] = *attribute;
    return &*gSimpleLightRegistry;
}

void cSimpleLightBehaviorTraits::ResetRegistryForTest() { gSimpleLightRegistry.reset(); }

nGZEvent::cEventAttributeMap *SimpleLightEventRegistry()
{
    const nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
    return cSimpleLightBehaviorTraits::RegistryFor(0, &defaults);
}

// (The original's operator new throws on failure, so its null branch is dead.)
cILinearFogLight *__cdecl cLinearFogLight::Create() { return new cLinearFogLight; }

bool cLinearFogLight::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == cILinearFogLight::kIid) {
        *out = static_cast<cILinearFogLight *>(this);
        AddRef();
        return true;
    }
    return Light::QueryInterface(iid, out);
}

bool cLinearFogLight::IsKindOf(uint32_t iid) { return iid == cILinearFogLight::kIid || Light::IsKindOf(iid); }

void cLinearFogLight::Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *)
{
    cLinearFogLight *const clone = new cLinearFogLight;
    CopyContentTo(clone, mode);
    clone->QueryInterface(iid, out);
}

void cLinearFogLight::SetColor(const float *rgb)
{
    mColor[0] = rgb[0];
    mColor[1] = rgb[1];
    mColor[2] = rgb[2];
}

void cLinearFogLight::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    Light::Read(reader);
    if (info.mName) info.mName->Release();
}

cPersistTypeInfo cLinearFogLight::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cLinearFogLight");
    info.mWord4 = kClassId;
    info.mVersion = 1;
    return info;
}

void cLinearFogLight::Write(ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info = cLinearFogLight::GetTypeInfo();
    writer->WriteTypeInfo(&info);
    if (info.mName) info.mName->Release();
    Light::Write(writer);
}

void cLinearFogLight::CopyContentTo(cLinearFogLight *target, uint32_t mode)
{
    mImpl.CopyTo(this, target, mode, nullptr);
    target->mNear = mNear;
    target->mFar = mFar;
    target->mColor[0] = mColor[0];
    target->mColor[1] = mColor[1];
    target->mColor[2] = mColor[2];
}

} // namespace nGZSceneGraph
