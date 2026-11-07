// nGZSceneGraph::cEnvironmentCubeLight (lane TC). Layout, faces and the switch: see the header.
#include "sg/cEnvironmentCubeLight.h"

#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "sg/SGLocalString.h"
#include "sg/cObjectGraphNodeImpl.h" // NewTypeName

#include <cstring>

namespace nGZSceneGraph {

static_assert(sizeof(cEnvironmentCubeLight) == cEnvironmentCubeLight::kSize, "cEnvironmentCubeLight size");
static_assert(offsetof(cEnvironmentCubeLight, mTexture) == 0x4c, "cEnvironmentCubeLight texture");
static_assert(offsetof(cEnvironmentCubeLight, mColors) == 0x50, "cEnvironmentCubeLight colours");

// (The original's operator new throws on failure, so its null branch is dead.)
cITextureLight *__cdecl cEnvironmentCubeLight::Create() { return new cEnvironmentCubeLight; }

bool cEnvironmentCubeLight::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == cITextureLight::kIid) {
        *out = static_cast<cITextureLight *>(this);
        AddRef();
        return true;
    }
    return Light::QueryInterface(iid, out);
}

bool cEnvironmentCubeLight::IsKindOf(uint32_t iid) { return iid == cITextureLight::kIid || Light::IsKindOf(iid); }

void cEnvironmentCubeLight::Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *)
{
    cEnvironmentCubeLight *const clone = new cEnvironmentCubeLight;
    CopyContentTo(clone, mode);
    clone->QueryInterface(iid, out);
}

void cEnvironmentCubeLight::SetTexture(const char *name)
{
    const cTextureHandle named(name);
    mTexture = named;
}

void cEnvironmentCubeLight::SetColor(uint32_t index, const float *rgba) { std::memcpy(mColors[index], rgba, 16); }

float *cEnvironmentCubeLight::GetColor(float *rgba, uint32_t index)
{
    std::memcpy(rgba, mColors[index], 16);
    return rgba;
}

void cEnvironmentCubeLight::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    Light::Read(reader);
    {
        nTSSG::SGLocalString name;
        reader->GetGZStr(&name.value);
        // Through the node face's slot 40, as the original calls it.
        if (name.value.Strlen() != 0) static_cast<cITextureLight *>(this)->SetTexture(name.value.ToChar());
        if (info.mVersion > 1)
            for (auto &color : mColors)
                for (float &channel : color) channel = reader->GetFloat32();
    }
    if (info.mName) info.mName->Release();
}

cPersistTypeInfo cEnvironmentCubeLight::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cEnvironmentCubeLight");
    info.mWord4 = kClassId;
    info.mVersion = 2;
    return info;
}

void cEnvironmentCubeLight::Write(ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info = cEnvironmentCubeLight::GetTypeInfo();
    writer->WriteTypeInfo(&info);
    if (info.mName) info.mName->Release();
    Light::Write(writer);
    const TextureInfo texture = TextureManager()->GetInfo(mTexture.mId);
    const char *const name = texture.name ? reinterpret_cast<const char *>(uintptr_t(texture.name)) : "";
    writer->WriteRZCharStr(const_cast<char *>(name));
    for (const auto &color : mColors)
        for (const float channel : color) writer->SetFloat32(channel);
}

void cEnvironmentCubeLight::CopyContentTo(cEnvironmentCubeLight *target, uint32_t mode)
{
    mImpl.CopyTo(this, target, mode, nullptr);
    target->mTexture = mTexture;
}

} // namespace nGZSceneGraph
