// nGZSceneGraph::cDrawStateLight (lane OT). Layout, faces and the switch: see the header.
#include "sg/cDrawStateLight.h"

#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "sg/cObjectGraphNodeImpl.h" // NewTypeName

namespace nGZSceneGraph {

static_assert(sizeof(cDrawStateLight) == cDrawStateLight::kSize, "cDrawStateLight size");
static_assert(offsetof(cDrawStateLight, mFirstFlag) == 0x4c, "cDrawStateLight first flag");
static_assert(offsetof(cDrawStateLight, mSecondFlag) == 0x4d, "cDrawStateLight second flag");

// (The original's operator new throws on failure, so its null branch is dead.)
cIDrawStateLight *__cdecl cDrawStateLight::Create() { return new cDrawStateLight; }

bool cDrawStateLight::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == cIDrawStateLight::kIid) {
        *out = static_cast<cIDrawStateLight *>(this);
        AddRef();
        return true;
    }
    return Light::QueryInterface(iid, out);
}

bool cDrawStateLight::IsKindOf(uint32_t iid) { return iid == cIDrawStateLight::kIid || Light::IsKindOf(iid); }

void cDrawStateLight::Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *)
{
    cDrawStateLight *const clone = new cDrawStateLight;
    CopyContentTo(clone, mode);
    clone->QueryInterface(iid, out);
}

void cDrawStateLight::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    Light::Read(reader);
    if (info.mName) info.mName->Release();
}

cPersistTypeInfo cDrawStateLight::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cDrawStateLight");
    info.mWord4 = kClassId;
    info.mVersion = 1;
    return info;
}

void cDrawStateLight::Write(ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info = cDrawStateLight::GetTypeInfo();
    writer->WriteTypeInfo(&info);
    if (info.mName) info.mName->Release();
    Light::Write(writer);
}

void cDrawStateLight::CopyContentTo(cDrawStateLight *target, uint32_t mode)
{
    mImpl.CopyTo(this, target, mode, nullptr);
    target->mFirstFlag = mFirstFlag;
    target->mSecondFlag = mSecondFlag;
}

} // namespace nGZSceneGraph
