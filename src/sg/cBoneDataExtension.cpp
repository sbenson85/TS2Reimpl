// nGZSceneGraph::cBoneDataExtension (lane QP). Layout and the hook decision: src/sg/cBoneDataExtension.h.
#include "sg/cBoneDataExtension.h"

#include "gz/cIGZString.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "rz/cRZString.h"
#include "sg/cObjectGraphNodeImpl.h" // NewTypeName

#include <cstring>

namespace nGZSceneGraph {

static_assert(sizeof(cBoneDataExtension) == 0x4c, "cBoneDataExtension size (the factory and Clone allocate 0x4c)");

// 0xa98470: new and the constructor (most-derived), answered as the cIExtension.
cIGZUnknown *__cdecl cBoneDataExtension::Create()
{
    cBoneDataExtension *const extension = new cBoneDataExtension;
    cIExtension *const face = static_cast<cIBoneDataExtension *>(extension);
    return face;
}

// 0xa980a0: cExtension's part (0xa3b7d0), then the values (the member initialisers).
cBoneDataExtension::cBoneDataExtension() = default;

// 0xa97ff0: cExtension's copy (0xa3b750: nothing copied), then `other`'s values.
cBoneDataExtension::cBoneDataExtension(const cBoneDataExtension &other)
    : cExtension(other), mAxisH(other.mAxisH), mAxisM(other.mAxisM), mAxisS(other.mAxisS), mScale(other.mScale)
{
    std::memcpy(mQuaternion, other.mQuaternion, sizeof mQuaternion);
}

// 0xa98160: nothing of its own; cExtension's destructor.
cBoneDataExtension::~cBoneDataExtension() = default;

// ---- the referent face ----

// 0xa98510 (ret 8): a null out answers false; kIid the cIBoneDataExtension face, counted; anything else cExtension's.
bool cBoneDataExtension::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid != cIBoneDataExtension::kIid) return cExtension::QueryInterface(iid, out);
    *out = static_cast<cIBoneDataExtension *>(this);
    cExtension::AddRef();
    return true;
}

// 0xa98560 (ret 4): the class's record read (its name released last), cExtension's (0x9dc310), the three axis codes and
// the scale; the quaternion only from records of version 4 on (older files keep the one the extension has).
void cBoneDataExtension::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cExtension::Read(reader);
    mAxisH = reader->GetSint32();
    mAxisM = reader->GetSint32();
    mAxisS = reader->GetSint32();
    mScale = reader->GetFloat32();
    if (info.mVersion >= kQuaternionVersion) {
        float value[4];
        reader->GetFloat32Array(value, 4);
        std::memcpy(mQuaternion, value, sizeof mQuaternion);
    }
    if (info.mName) info.mName->Release();
}

// 0xa98670 (hidden out): {"cBoneDataExtension" (a new counted cRZString), the class id, version 5}.
cPersistTypeInfo cBoneDataExtension::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cBoneDataExtension");
    info.mWord4 = kClassId;
    info.mVersion = kVersion;
    return info;
}

// 0xa98740 (ret 4): the class's record (0xa98670 directly) through the writer's slot 28, its name released; cExtension's
// Write (0xa3c210); then the values through the cIBoneDataExtension face's getters.
void cBoneDataExtension::Write(ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info = cBoneDataExtension::GetTypeInfo();
    writer->WriteTypeInfo(&info);
    if (info.mName) info.mName->Release();
    cExtension::WriteExtension(writer);
    writer->SetSint32(GetAxisH());
    writer->SetSint32(GetAxisM());
    writer->SetSint32(GetAxisS());
    writer->SetFloat32(GetScale());
    writer->SetFloat32Array(const_cast<float *>(GetQuaternion()), 4);
}

// ---- cIExtension ----

// 0xa981f0: a copy (the copy constructor 0xa97ff0), uncounted, as its cIExtension.
cIExtension *cBoneDataExtension::Clone()
{
    cBoneDataExtension *const copy = new cBoneDataExtension(*this);
    return static_cast<cIBoneDataExtension *>(copy);
}

// 0xa98650 (ret 4): kIid, else cExtension's 0xa3c130.
bool cBoneDataExtension::IsKindOf(uint32_t iid)
{
    if (iid == cIBoneDataExtension::kIid) return true;
    return cExtension::IsKindOf(iid);
}

// 0xa98460: the extension's type name.
const char *cBoneDataExtension::GetTypeName() { return "bone_data"; }

// 0xa98270 (ret 4): the axis codes as letters, then the text's FromChar (slot 4) of "(H%c M%c S%c)", or with the scale
// "(H%c M%c S%c:%3.2f)" unless it is exactly 1 (a NaN takes the long form). The scale is asked again for the long form.
void cBoneDataExtension::GetDescription(cIGZString *text)
{
    char h = 'N';
    switch (GetAxisH()) {
    case 0: h = 'X'; break;
    case 1: h = 'Y'; break;
    case 2: h = 'Z'; break;
    default: break;
    }
    // M has no "none": any code but 1 and 2 reads as X.
    char m = 'X';
    switch (GetAxisM()) {
    case 1: m = 'Y'; break;
    case 2: m = 'Z'; break;
    default: break;
    }
    char s = 'N';
    switch (uint32_t(GetAxisS())) {
    case 0: s = 'X'; break;
    case 1: s = 'Y'; break;
    case 2: s = 'Z'; break;
    case 4: s = 'A'; break;
    default: break;
    }
    cRZString description;
    description.Construct();
    const float scale = GetScale();
    if (scale == 1.0f)
        description.Sprintf("(H%c M%c S%c)", h, m, s);
    else
        description.Sprintf("(H%c M%c S%c%c%3.2f)", h, m, s, ':', double(GetScale()));
    text->FromChar(description.ToChar());
    description.Destruct();
}

// ---- cIBoneDataExtension (the game's bodies are folded accessors, except SetQuaternion's) ----

void cBoneDataExtension::SetAxisH(int32_t value) { mAxisH = value; }
void cBoneDataExtension::SetAxisM(int32_t value) { mAxisM = value; }
void cBoneDataExtension::SetAxisS(int32_t value) { mAxisS = value; }
void cBoneDataExtension::SetScale(float value) { mScale = value; }

// 0xa98630 (ret 4): the four floats copied in.
void cBoneDataExtension::SetQuaternion(const float *value) { std::memcpy(mQuaternion, value, sizeof mQuaternion); }

int32_t cBoneDataExtension::GetAxisH() { return mAxisH; }
int32_t cBoneDataExtension::GetAxisM() { return mAxisM; }
int32_t cBoneDataExtension::GetAxisS() { return mAxisS; }
float cBoneDataExtension::GetScale() { return mScale; }
const float *cBoneDataExtension::GetQuaternion() { return mQuaternion; }

} // namespace nGZSceneGraph
