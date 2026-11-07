// nGZSceneGraph::cPartitionNodeImpl (lane OI). See the header.
#include "sg/cPartitionNodeImpl.h"

#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"

#include <new>

namespace nGZSceneGraph {

cPartitionNodeImpl *cPartitionNodeImpl::Construct() { return ::new (this) cPartitionNodeImpl; }

void cPartitionNodeImpl::Read(cIBoundedNode *outer, ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cBoundedNodeImpl::Read(outer, reader);
    if (info.mName) info.mName->Release();
}

bool cPartitionNodeImpl::IsKindOf(uint32_t iid) { return iid == kIid || cBoundedNodeImpl::IsKindOf(iid); }

cPersistTypeInfo *cPartitionNodeImpl::GetTypeInfo(cPersistTypeInfo *out)
{
    out->mName = NewTypeName("cPartitionNode");
    out->mWord4 = kTypeWord4;
    out->mVersion = kVersion;
    return out;
}

void cPartitionNodeImpl::Write(cIBoundedNode *outer, ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info;
    writer->WriteTypeInfo(GetTypeInfo(&info));
    if (info.mName) info.mName->Release();
    cBoundedNodeImpl::Write(outer, writer);
}

} // namespace nGZSceneGraph
