// nGZSceneGraph::cObjectGraphNodeImpl, the node notifications and their event bodies (lane NZ). See the header.
#include "sg/SGEventRecords.h"
#include "sg/cObjectGraphNodeImpl.h"

#include "game/game.h"
#include "game/original.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "rz/cRZString.h"
#include "sg/SGLocalString.h"

#include <cstring>
#include <new>
#include <optional>
#include <string>

namespace nGZSceneGraph {

// ---- the behaviour's event registry (0x264010) -------------------------------------------------------------------------

namespace {
std::optional<nGZEvent::cEventAttributeMap> gObjectGraphRegistry;
}

// The holder 0x255910 (its object, kept in 0x10cca20, is read by nothing) runs 0x2570a0 on the first call: the base events
// (0x1464f0), then the connection and subordinates events through this function, whose map the first of them builds.
nGZEvent::cEventAttributeMap *__cdecl cObjectGraphBehaviorTraits::RegistryFor(uint32_t id,
                                                                            const nGZEvent::cEventAttribute *attribute)
{
    if (!gObjectGraphRegistry) {
        nGZEvent::cAbstractBehaviorType::RegisterBaseEvents();
        gObjectGraphRegistry.emplace(*nGZEvent::cAbstractBehaviorType::Registry());
        (*gObjectGraphRegistry)[0x7b54c99c] = sgevents::kGraphConnChanged;
        (*gObjectGraphRegistry)[0x1d05597b] = sgevents::kSubordinateListChanged;
    }
    if (id != 0) (*gObjectGraphRegistry)[id] = *attribute;
    return &*gObjectGraphRegistry;
}

void cObjectGraphBehaviorTraits::ResetRegistryForTest() { gObjectGraphRegistry.reset(); }

void cObjectGraphBehaviorTraits::RegisterEvents()
{
    nGZEvent::cAbstractBehaviorType::RegisterBaseEvents();
    RegistryFor(0x7b54c99c, &sgevents::kGraphConnChanged);
    RegistryFor(0x1d05597b, &sgevents::kSubordinateListChanged);
}

void *__fastcall cObjectGraphBehaviorTraits::RegisterEventsAdapter(void *self, void *)
{
    RegisterEvents();
    return self;
}

// ---- the node state -----------------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kNoTag = 0xffffffff;

// The cell made to hold `handle`, counted in the pool (slot 6; ServiceCell::ConstructFrom, 0xaeb710). Returns the cell.
ServiceCell *ConstructCell(ServiceCell *cell, uint32_t handle) { return cell->ConstructFrom(handle); }

// The tag cell takes the pool's handle of `text` (interned, flag 1).
void AssignText(ServiceCell &tag, const char *text)
{
    ServiceCell temp;
    tag.Assign(&temp.Open(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(text)), 1)->mValue);
    temp.Close();
}

// The extension a tag-extension face stands for (its virtual base), null for null.
cIExtension *ExtensionOf(cITagExtension *tag) { return tag ? static_cast<cIExtension *>(tag) : nullptr; }

} // namespace

// ---- the event bodies ----

uint32_t cSubordinateListChanged::Assign(const cSubordinateListChanged *other)
{
    mKind = other->mKind;
    mIid = other->mIid;
    return mIid;
}

uint32_t cSubordinateListChanged::AssignAlias(const cSubordinateListChanged *other) { return Assign(other); }
uint32_t cSubordinateListChanged::GetEventType() const { return kType; }
uint32_t cSubordinateListChanged::GetSize() const { return sizeof(cSubordinateListChanged); }
uint32_t cSubordinateListChanged::GetBodySize() const { return sizeof(cSubordinateListChanged) - 4; }
void cSubordinateListChanged::CloneInto(void *memory) const { ::new (memory) cSubordinateListChanged(mKind, mIid); }

uint32_t cGraphConnChanged::Assign(const cGraphConnChanged *other)
{
    mDisconnecting = other->mDisconnecting;
    mNode = other->mNode;
    mSubordinate = other->mSubordinate;
    mIid = other->mIid;
    return mIid;
}

uint32_t cGraphConnChanged::AssignAlias(const cGraphConnChanged *other) { return Assign(other); }
uint32_t cGraphConnChanged::GetEventType() const { return kType; }
uint32_t cGraphConnChanged::GetSize() const { return sizeof(cGraphConnChanged); }
uint32_t cGraphConnChanged::GetBodySize() const { return sizeof(cGraphConnChanged) - 4; }
void cGraphConnChanged::CloneInto(void *memory) const
{
    ::new (memory) cGraphConnChanged(mDisconnecting, mNode, mSubordinate, mIid);
}

// ---- the notifications ----

namespace {
void TellNode(cIObjectGraphNode *node, uint32_t kind, uint32_t iid)
{
    cSubordinateListChanged body(kind, iid);
    node->OnEvent(cSubordinateListChanged::kEventId, 0, &body, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node)));
}

void TellSubordinate(cIObjectGraphNode *node, uint32_t iid, cIObjectGraphNode *subordinate, uint32_t disconnecting)
{
    cGraphConnChanged body(disconnecting, node, subordinate, iid);
    subordinate->NotifyListeners(cGraphConnChanged::kEventId, 0, &body);
}
} // namespace

void __stdcall NotifySubordinateAdding(cIObjectGraphNode *node, uint32_t iid)
{
    TellNode(node, cSubordinateListChanged::kAdding, iid);
}

void __stdcall NotifySubordinateRemoving(cIObjectGraphNode *node, uint32_t iid, cIObjectGraphNode *subordinate)
{
    TellSubordinate(node, iid, subordinate, 1);
    TellNode(node, cSubordinateListChanged::kRemoving, iid);
}

void __stdcall NotifySubordinateAdded(cIObjectGraphNode *node, uint32_t iid, cIObjectGraphNode *subordinate)
{
    TellSubordinate(node, iid, subordinate, 0);
    TellNode(node, cSubordinateListChanged::kAdded, iid);
}

void __stdcall NotifySubordinateRemoved(cIObjectGraphNode *node, uint32_t iid)
{
    TellNode(node, cSubordinateListChanged::kRemoved, iid);
}

// ---- the type name ----

cIGZUnknown *NewTypeName(const char *name)
{
    // A heap cRZString as the original builds it inline: a cRZString vtable (ours), the text, count 0; then counted once.
    // (Its operator new throws on failure, so the original's null branch is dead.)
    cRZString *const string = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
    string->mVTable = cRZString::VTable();
    ::new (&string->mString) std::string(name);
    string->mRefCount = 0;
    cIGZUnknown *const counted = reinterpret_cast<cIGZUnknown *>(string);
    counted->AddRef();
    return counted;
}

// ---- cObjectGraphNodeImpl ----

cObjectGraphNodeImpl::~cObjectGraphNodeImpl()
{
    mTag.Close();
    if (mFirst) mFirst->Release();
}

void cObjectGraphNodeImpl::Destruct() { this->~cObjectGraphNodeImpl(); }

void cObjectGraphNodeImpl::AddExtension(cIObjectGraphNode *outer, cIExtension *extension)
{
    extension->SetOwner(outer);
    extension->SetNext(mFirst);
    cIExtension *const old = mFirst;
    if (extension != old) {
        extension->AddRef();
        mFirst = extension;
        if (old) old->Release();
    }
}

bool cObjectGraphNodeImpl::HasExtension(cIExtension *extension)
{
    for (cIExtension *e = mFirst; e; e = e->GetNextConst())
        if (e == extension) return true;
    return false;
}

bool cObjectGraphNodeImpl::FindExtension(uint32_t iid, void **out, cIExtension *start)
{
    for (cIExtension *e = start ? start : mFirst; e; e = e->GetNext())
        if (e->QueryInterface(iid, out)) return true;
    return false;
}

void cObjectGraphNodeImpl::Read(cIObjectGraphNode *outer, ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    const uint32_t count = reader->GetUint32();
    for (uint32_t i = 0; i < count; ++i) {
        // (The original also builds an empty cRZString here and reads its text: nothing comes of it.)
        cIExtension *extension = nullptr;
        reader->GetObjectRef(kExtensionIid, &extension);
        if (extension) outer->AddExtension(extension);
        if (extension) extension->Release();
    }
    if (info.mVersion < kVersion) {
        // The legacy tag: its text becomes the handle, and the extension goes once the handle holds something.
        cITagExtension *tag = nullptr;
        if (outer->FindExtension(cITagExtension::kIid, reinterpret_cast<void **>(&tag), nullptr)) {
            AssignText(mTag, tag->GetTagText());
            if (mTag.mValue != kNoTag) outer->RemoveExtension(ExtensionOf(tag));
        }
        if (tag) ExtensionOf(tag)->Release();
    } else {
        nTSSG::SGLocalString text;
        reader->GetGZStr(&text.value);
        AssignText(mTag, text.Text());
    }
    if (info.mName) info.mName->Release();
}

void cObjectGraphNodeImpl::RemoveExtension(cIExtension *extension)
{
    extension->AddRef();
    extension->SetOwner(nullptr);
    cIExtension *const first = mFirst;
    if (extension == first) {
        cIExtension *const next = extension->GetNext();
        cIExtension *const old = mFirst;
        if (next != old) {
            if (next) next->AddRef();
            mFirst = next;
            if (old) old->Release();
        }
    } else {
        // The one before it (asked twice per step, as the original does). A list without it ends in a null call.
        cIExtension *previous = first;
        while (previous && previous->GetNext() != extension) previous = previous->GetNext();
        previous->SetNext(extension->GetNext());
    }
    extension->SetNext(nullptr);
    extension->Release();
}

void cObjectGraphNodeImpl::DropLegacyTag(cIObjectGraphNode *outer)
{
    if (mTag.mValue != kNoTag) return;
    cITagExtension *tag = nullptr;
    if (outer->FindExtension(cITagExtension::kIid, reinterpret_cast<void **>(&tag), nullptr))
        outer->RemoveExtension(ExtensionOf(tag));
    if (tag) ExtensionOf(tag)->Release();
}

void cObjectGraphNodeImpl::AdoptLegacyTag()
{
    if (mTag.mValue != kNoTag) return;
    cITagExtension *tag = nullptr;
    if (FindExtension(cITagExtension::kIid, reinterpret_cast<void **>(&tag), nullptr)) {
        AssignText(mTag, tag->GetTagText());
        RemoveExtension(ExtensionOf(tag));
    }
    if (tag) ExtensionOf(tag)->Release();
}

void cObjectGraphNodeImpl::SetTagHandle(cIObjectGraphNode *outer, uint32_t handle)
{
    DropLegacyTag(outer);
    ServiceCell temp;
    mTag.Assign(&ConstructCell(&temp, handle)->mValue);
    temp.Close();
}

void cObjectGraphNodeImpl::SetTag(cIObjectGraphNode *outer, const char *tag)
{
    DropLegacyTag(outer);
    ServiceCell temp;
    if (tag)
        temp.Open(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tag)), 1);
    else
        ConstructCell(&temp, kNoTag);
    mTag.Assign(&temp.mValue);
    temp.Close();
}

bool cObjectGraphNodeImpl::IsKindOf(uint32_t iid)
{
    return iid == cIObjectGraphNode::kIid || iid == 0x3b253dc7 || iid == 0xbb11a53e || iid == 0x5b0aaa74 || iid == 1;
}

uint32_t cObjectGraphNodeImpl::GetTagHandle()
{
    AdoptLegacyTag();
    return mTag.mValue;
}

const char *cObjectGraphNodeImpl::GetTag()
{
    AdoptLegacyTag();
    const char *const text = reinterpret_cast<const char *>(static_cast<uintptr_t>(Dispatch8(&mTag)));
    return text && *text ? text : nullptr;
}

cPersistTypeInfo *cObjectGraphNodeImpl::GetTypeInfo(cPersistTypeInfo *out)
{
    out->mName = NewTypeName("cObjectGraphNode");
    out->mWord4 = 0;
    out->mVersion = kVersion;
    return out;
}

void cObjectGraphNodeImpl::Write(cIObjectGraphNode *outer, ifaces::cIGZPersistWriter *writer)
{
    (void)outer;
    cPersistTypeInfo info;
    writer->WriteTypeInfo(GetTypeInfo(&info));
    if (info.mName) info.mName->Release();
    uint32_t count = 0;
    for (cIExtension *e = mFirst; e; e = e->GetNextConst())
        if (e->IsPersistent()) ++count;
    writer->SetUint32(count);
    for (cIExtension *e = mFirst; e; e = e->GetNextConst()) {
        // (The original also prints "Extension(<index>)" into a local string it never uses; below index 10000 that
        // string stays in its inline buffer, so nothing of it is seen.)
        if (e->IsPersistent()) writer->WriteObjectRef(e);
    }
    const char *const tag = GetTag();
    writer->WriteRZCharStr(const_cast<char *>(tag ? tag : ts2_rva_d9b9d0));
}

void cObjectGraphNodeImpl::CopyTo(cIObjectGraphNode *source, cIObjectGraphNode *target, uint32_t, cINodeCloneContext *)
{
    for (cIExtension *e = mFirst; e; e = e->GetNextConst()) {
        cIExtension *const copy = e->Clone();
        if (copy) copy->AddRef();
        if (copy) target->AddExtension(copy);
        if (copy) copy->Release();
    }
    target->SetTagHandle(source->GetTagHandle());
}

} // namespace nGZSceneGraph
