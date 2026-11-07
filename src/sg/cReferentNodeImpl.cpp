// nGZSceneGraph::cReferentNodeImpl (lane OR). See the header.
#include "sg/cReferentNodeImpl.h"

#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"

#include <algorithm>
#include <new>

namespace nGZSceneGraph {

static_assert(offsetof(cReferentNodeImpl, mReferrers) == 8, "cReferentNodeImpl referrers");

cReferentNodeImpl::~cReferentNodeImpl() = default; // the vector's (freed, its words cleared), then the base's

cReferentNodeImpl *cReferentNodeImpl::Construct() { return ::new (this) cReferentNodeImpl; }

void cReferentNodeImpl::Destruct() { this->~cReferentNodeImpl(); }

void cReferentNodeImpl::AddReferrer(nGZEvent::cIAbstractBehavior *referrer) { mReferrers.push_back(referrer); }

void cReferentNodeImpl::RemoveReferrer(nGZEvent::cIAbstractBehavior *referrer)
{
    mReferrers.erase(std::find(mReferrers.begin(), mReferrers.end(), referrer));
}

bool cReferentNodeImpl::Ask(uint32_t type, uint32_t arg1, void *data)
{
    // ORIGINAL-BUG: the answers are dropped, so the referent template's slot 6 (which returns true when this does) always
    // goes on to the node's own listeners.
    for (auto it = mReferrers.begin(); it != mReferrers.end(); ++it) (*it)->OnQuery(type, arg1, data);
    return false;
}

void cReferentNodeImpl::Notify(cIObjectGraphNode *outer, uint32_t type, uint32_t arg1, void *data)
{
    const uint32_t sender = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(outer));
    for (auto it = mReferrers.begin(); it != mReferrers.end(); ++it) (*it)->OnEvent(type, arg1, data, sender);
}

void cReferentNodeImpl::Read(cIObjectGraphNode *outer, ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cObjectGraphNodeImpl::Read(outer, reader);
    if (info.mName) info.mName->Release();
}

void cReferentNodeImpl::Write(cIObjectGraphNode *outer, ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info;
    writer->WriteTypeInfo(GetTypeInfo(&info));
    if (info.mName) info.mName->Release();
    cObjectGraphNodeImpl::Write(outer, writer);
}

bool cReferentNodeImpl::IsKindOf(uint32_t iid) { return iid == kIid || cObjectGraphNodeImpl::IsKindOf(iid); }

cPersistTypeInfo *cReferentNodeImpl::GetTypeInfo(cPersistTypeInfo *out)
{
    out->mName = NewTypeName("cReferentNode");
    out->mWord4 = 0;
    out->mVersion = kVersion;
    return out;
}

} // namespace nGZSceneGraph
