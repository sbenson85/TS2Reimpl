// nTSSG::cDiffusionLightExtension (lane 261109c, scenegraph LG7e). Layout and the hook decision:
// src/sg/cDiffusionLightExtension.h. The face's accessors are in cDiffusionLightExtension.size.cpp.
#include "sg/cDiffusionLightExtension.h"

#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "sg/cObjectGraphNodeImpl.h" // NewTypeName

#include <cstddef>

namespace nTSSG {

static_assert(sizeof(cDiffusionLightExtension) == 0xbc, "cDiffusionLightExtension size (the factory and Clone allocate 0xbc)");

// The game's offsets (the face's bodies address them from the +0xb4 face; Read and Write from the +0xc one).
struct cDiffusionLightExtensionLayout {
    using X = cDiffusionLightExtension;
    static_assert(offsetof(X, mDefaultMin) == 0x18);
    static_assert(offsetof(X, mDefaultMax) == 0x20);
    static_assert(offsetof(X, mDefaultIntensity) == 0x28);
    static_assert(offsetof(X, mDefaultColor) == 0x2c);
    static_assert(offsetof(X, mDefaultIsDirectional) == 0x38);
    static_assert(offsetof(X, mDefaultFalloff) == 0x3c);
    static_assert(offsetof(X, mDefaultInterFloorFalloff) == 0x4c);
    static_assert(offsetof(X, mMin) == 0x50 && offsetof(X, mHasMin) == 0x58);
    static_assert(offsetof(X, mMax) == 0x5c && offsetof(X, mHasMax) == 0x64);
    static_assert(offsetof(X, mIntensity) == 0x68 && offsetof(X, mHasIntensity) == 0x6c);
    static_assert(offsetof(X, mColor) == 0x70 && offsetof(X, mHasColor) == 0x7c);
    static_assert(offsetof(X, mIsDirectional) == 0x7d && offsetof(X, mHasIsDirectional) == 0x7e);
    static_assert(offsetof(X, mFalloff) == 0x80 && offsetof(X, mHasFalloff) == 0x84);
    static_assert(offsetof(X, mDirectionalFalloff) == 0x88 && offsetof(X, mHasDirectionalFalloff) == 0x8c);
    static_assert(offsetof(X, mWallIntensityScale) == 0x90 && offsetof(X, mHasWallIntensityScale) == 0x94);
    static_assert(offsetof(X, mFloorIntensityScale) == 0x98 && offsetof(X, mHasFloorIntensityScale) == 0x9c);
    static_assert(offsetof(X, mInterFloorFalloff) == 0xa0 && offsetof(X, mHasInterFloorFalloff) == 0xa4);
};

// 0x2ea3b0: new and the constructor (most-derived), answered as the cIExtension.
cIGZUnknown *__cdecl cDiffusionLightExtension::Create()
{
    cDiffusionLightExtension *const extension = new cDiffusionLightExtension;
    cIExtension *const face = static_cast<cIDiffusionLightExtension *>(extension);
    return face;
}

// 0x2ea080: cExtension's part (0xa3b7d0), then the defaults and the cleared flags (the member initialisers).
cDiffusionLightExtension::cDiffusionLightExtension() = default;

// 0x2e9eb0: cExtension's copy (0xa3b750: nothing copied), then every default, override value and flag of `other`.
cDiffusionLightExtension::cDiffusionLightExtension(const cDiffusionLightExtension &other)
    : cExtension(other),
      mDefaultMin{other.mDefaultMin[0], other.mDefaultMin[1]},
      mDefaultMax{other.mDefaultMax[0], other.mDefaultMax[1]},
      mDefaultIntensity(other.mDefaultIntensity),
      mDefaultColor{other.mDefaultColor[0], other.mDefaultColor[1], other.mDefaultColor[2]},
      mDefaultIsDirectional(other.mDefaultIsDirectional),
      mDefaultFalloff(other.mDefaultFalloff),
      mDefaultDirectionalFalloff(other.mDefaultDirectionalFalloff),
      mDefaultWallIntensityScale(other.mDefaultWallIntensityScale),
      mDefaultFloorIntensityScale(other.mDefaultFloorIntensityScale),
      mDefaultInterFloorFalloff(other.mDefaultInterFloorFalloff),
      mMin{other.mMin[0], other.mMin[1]},
      mHasMin(other.mHasMin),
      mMax{other.mMax[0], other.mMax[1]},
      mHasMax(other.mHasMax),
      mIntensity(other.mIntensity),
      mHasIntensity(other.mHasIntensity),
      mColor{other.mColor[0], other.mColor[1], other.mColor[2]},
      mHasColor(other.mHasColor),
      mIsDirectional(other.mIsDirectional),
      mHasIsDirectional(other.mHasIsDirectional),
      mFalloff(other.mFalloff),
      mHasFalloff(other.mHasFalloff),
      mDirectionalFalloff(other.mDirectionalFalloff),
      mHasDirectionalFalloff(other.mHasDirectionalFalloff),
      mWallIntensityScale(other.mWallIntensityScale),
      mHasWallIntensityScale(other.mHasWallIntensityScale),
      mFloorIntensityScale(other.mFloorIntensityScale),
      mHasFloorIntensityScale(other.mHasFloorIntensityScale),
      mInterFloorFalloff(other.mInterFloorFalloff),
      mHasInterFloorFalloff(other.mHasInterFloorFalloff)
{
}

// 0x2ea1d0: nothing of its own; cExtension's destructor (0xa3b880).
cDiffusionLightExtension::~cDiffusionLightExtension() = default;

// ---- the referent face ----

// 0x2ea5d0 (ret 8): a null out answers false; kIid the cIDiffusionLightExtension face, counted (AddRef through the
// referent face's slot 1); anything else cExtension's (0xa3be60).
bool cDiffusionLightExtension::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid != cIDiffusionLightExtension::kIid) return cExtension::QueryInterface(iid, out);
    *out = static_cast<cIDiffusionLightExtension *>(this);
    AddRef();
    return true;
}

// 0x2ea630 (ret 4): the class's record read (its name released last; no version test), cExtension's (0x9dc310), then the
// defaults straight into the object: min and max (two floats each), intensity, colour (three), the directional flag (the
// reader's GetBool), falloff, directional falloff, wall and floor scales, inter-floor falloff.
void cDiffusionLightExtension::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cExtension::Read(reader);
    reader->GetFloat32Array(mDefaultMin, 2);
    reader->GetFloat32Array(mDefaultMax, 2);
    mDefaultIntensity = reader->GetFloat32();
    reader->GetFloat32Array(mDefaultColor, 3);
    mDefaultIsDirectional = reader->GetBool();
    mDefaultFalloff = reader->GetFloat32();
    mDefaultDirectionalFalloff = reader->GetFloat32();
    mDefaultWallIntensityScale = reader->GetFloat32();
    mDefaultFloorIntensityScale = reader->GetFloat32();
    mDefaultInterFloorFalloff = reader->GetFloat32();
    if (info.mName) info.mName->Release();
}

// 0x2ea9e0 (hidden out): {"cDiffusionLightExtension" (a new counted cRZString), the class id, version 1}.
cPersistTypeInfo cDiffusionLightExtension::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = nGZSceneGraph::NewTypeName("cDiffusionLightExtension");
    info.mWord4 = kClassId;
    info.mVersion = kVersion;
    return info;
}

// 0x2eaab0 (ret 4): the class's record (0x2ea9e0 directly) through the writer's slot 28, its name released; cExtension's
// Write (0xa3c210); then the defaults read straight from the object (not through the getters), in Read's order; the
// directional flag through the writer's slot 4 (SetUint8, zero-extended).
void cDiffusionLightExtension::Write(ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info = cDiffusionLightExtension::GetTypeInfo();
    writer->WriteTypeInfo(&info);
    if (info.mName) info.mName->Release();
    cExtension::WriteExtension(writer);
    writer->SetFloat32Array(mDefaultMin, 2);
    writer->SetFloat32Array(mDefaultMax, 2);
    writer->SetFloat32(mDefaultIntensity);
    writer->SetFloat32Array(mDefaultColor, 3);
    writer->SetUint8(mDefaultIsDirectional);
    writer->SetFloat32(mDefaultFalloff);
    writer->SetFloat32(mDefaultDirectionalFalloff);
    writer->SetFloat32(mDefaultWallIntensityScale);
    writer->SetFloat32(mDefaultFloorIntensityScale);
    writer->SetFloat32(mDefaultInterFloorFalloff);
}

// ---- cIExtension ----

// 0x2ea2d0 (through the vtordisp thunk 0x2ea2b9): a copy (the copy constructor 0x2e9eb0), uncounted, as its cIExtension.
nGZSceneGraph::cIExtension *cDiffusionLightExtension::Clone()
{
    cDiffusionLightExtension *const copy = new cDiffusionLightExtension(*this);
    return static_cast<cIDiffusionLightExtension *>(copy);
}

// 0x2ea9b0 (ret 4; thunk 0x2ea99e): kIid, else cExtension's 0xa3c130.
bool cDiffusionLightExtension::IsKindOf(uint32_t iid)
{
    if (iid == cIDiffusionLightExtension::kIid) return true;
    return cExtension::IsKindOf(iid);
}

// 0x2ea380 (thunk 0x2ea36e; the body folded with an anonymous cLightCommand's slot 5): the extension's type name.
const char *cDiffusionLightExtension::GetTypeName() { return "light"; }

} // namespace nTSSG
