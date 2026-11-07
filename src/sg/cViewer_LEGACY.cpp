// nGZSceneGraph::cViewer_LEGACY (lane VD). Layout, faces and the switch: see the header.
#include "sg/cViewer_LEGACY.h"

#include "ifaces/cIGZPersistReader.h"
#include "sg/SGBehaviorCopies.h" // the connection-manager cell 0x10f63dc

#include <new>

namespace nGZSceneGraph {

static_assert(sizeof(cViewer_LEGACY) == cViewer_LEGACY::kSize, "cViewer_LEGACY size");
static_assert(offsetof(cViewer_LEGACY, mImpl) == 0x38, "cViewer_LEGACY node Impl");
static_assert(offsetof(cViewer_LEGACY, mView) == 0x40, "cViewer_LEGACY view");

nGZEvent::cIConnectionManager *&cViewerLegacyBehaviorTraits::EventManagerCell() { return ts2_rva_10f63dc; }

nGZEvent::cEventAttributeMap *cViewerLegacyBehaviorTraits::Registry()
{
    const nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
    return cObjectGraphBehaviorTraits::RegistryFor(0, &defaults);
}

// (The original's operator new throws on failure, so its null branch is dead.)
cIObjectGraphNode *__cdecl cViewer_LEGACY::Create() { return new cViewer_LEGACY; }

cViewer_LEGACY *__fastcall cViewer_LEGACY::Construct(cViewer_LEGACY *self, void *, uint32_t)
{
    return ::new (self) cViewer_LEGACY;
}
void __fastcall cViewer_LEGACY::Destruct(cViewer_LEGACY *self, void *) { self->cViewer_LEGACY::~cViewer_LEGACY(); }
cViewer_LEGACY *__fastcall cViewer_LEGACY::DeletingDestructor(cViewer_LEGACY *self, void *, uint32_t flags)
{
    self->cViewer_LEGACY::~cViewer_LEGACY();
    if (flags & 1) ::operator delete(self, kSize);
    return self;
}

bool cViewer_LEGACY::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == kIid) {
        *out = static_cast<cSGResource *>(this);
        AddRef();
        return true;
    }
    if (Node::QueryInterface(iid, out)) return true;
    return cSGResource::QueryInterface(iid, out);
}

void cViewer_LEGACY::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cSGResource::Read(reader);
    mImpl.Read(this, reader);
    if (info.mVersion < kVersion) {
        // What older records kept here (a word, five floats, a word) is not used any more.
        reader->GetUint32();
        for (int i = 0; i < 5; ++i) reader->GetFloat32();
        reader->GetUint32();
    }
    ReadViewMaterials(mView, reader);
    for (float &f : mView.mViewport) f = reader->GetFloat32();
    for (float &f : mView.mSubrect) f = reader->GetFloat32();
    mView.mNear = reader->GetFloat32();
    mView.mFar = reader->GetFloat32();
    mView.mFov = reader->GetFloat32();
    mView.mPixelAspect = reader->GetFloat32();
    mView.mDepthMin = reader->GetFloat32();
    mView.mDepthMax = reader->GetFloat32();
    for (float &f : mView.mOffset) f = reader->GetFloat32();
    if (reader->GetBool()) mView.mType = cViewerRefEntry::kOrtho;
    for (float &f : mView.mOrtho) f = reader->GetFloat32();
    if (info.mName) info.mName->Release();
}

cPersistTypeInfo cViewer_LEGACY::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cViewer");
    info.mWord4 = kClassId;
    info.mVersion = kVersion;
    return info;
}

} // namespace nGZSceneGraph
