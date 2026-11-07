// nGZSceneGraph::cExplicitBoundNodeImpl (lane OB). See the header.
#include "sg/cExplicitBoundNodeImpl.h"

#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"

namespace nGZSceneGraph {

cExplicitBoundNodeImpl::~cExplicitBoundNodeImpl()
{
    if (mShape) mShape->Release();
}

void cExplicitBoundNodeImpl::Destruct() { this->~cExplicitBoundNodeImpl(); }

void cExplicitBoundNodeImpl::Read(cIBoundedNode *outer, ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cBoundedNodeImpl::Read(outer, reader);
    if (info.mName) info.mName->Release();
}

bool cExplicitBoundNodeImpl::IsKindOf(uint32_t iid) { return iid == kIid || cBoundedNodeImpl::IsKindOf(iid); }

cPersistTypeInfo *cExplicitBoundNodeImpl::GetTypeInfo(cPersistTypeInfo *out)
{
    out->mName = NewTypeName("cExplicitBoundNode");
    out->mWord4 = kTypeWord4;
    out->mVersion = kVersion;
    return out;
}

void cExplicitBoundNodeImpl::Write(cIBoundedNode *outer, ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info;
    writer->WriteTypeInfo(GetTypeInfo(&info));
    if (info.mName) info.mName->Release();
    cBoundedNodeImpl::Write(outer, writer);
}

} // namespace nGZSceneGraph
