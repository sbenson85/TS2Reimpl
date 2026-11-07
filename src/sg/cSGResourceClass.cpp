// nGZSceneGraph::cSGResource as a whole class (lane OE). Layout, slots and the constructor decision: src/sg/cSGResource.h.
// Not named cSGResource.cpp: src/gz/cSGResource.cpp (the hook-shaped bodies) has that name, and two sources with one
// name in a target get collision object names that matchcheck can't find.
#include "sg/cSGResource.h"

#include "gz/cIGZFrameWork.h"
#include "gz/cIGZPersistResourceManager.h"
#include "gz/cSGResource.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "rz/RZAutoRef.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject: the framework
#include "sg/SGLocalString.h"
#include "sg/SGResourceHelpers.h"
#include "sg/cObjectGraphNodeImpl.h" // NewTypeName

#include <cstdint>

// 0xa3c270's hook target: our constructor's code (OE_cSGResource_Constructor, declared in cSGResource.h). C++ cannot take
// a constructor's address, so a C name stands for it and the linker resolves that name to the constructor's decorated one
// (a wrong decoration fails the link: the C name is then unresolved).
#pragma comment(linker, "/alternatename:_OE_cSGResource_Constructor=??0cSGResource@nGZSceneGraph@@QAE@XZ")

namespace nGZSceneGraph {

namespace {

constexpr uint32_t kResourceManagerService = 0x056b906e; // the framework's system service id
constexpr uint32_t kIidResourceManager = 0x656b8efc;    // cIGZPersistResourceManager

using SubBodies = cSGResourceSubobject;          // the hook-shaped bodies of src/gz/cSGResource.cpp
using HelperBodies = SGResourceHelpers::cSGSecondary; // and of src/sg/SGResourceHelpers.cpp

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

} // namespace

// 0xa3c270: the base (0x9b5fd0), the vtables (the virtual base's through the vbptr, and the vtordisp, both the
// compiler's), the name empty (0x18bf80), no segment, both flags 0. The most-derived flag (ret 4) only says whether the
// vbptr is set here: the game's derived constructors set it and pass 0.
cSGResource::cSGResource() = default;

// 0xa3c2f0: the vtables back to cSGResource's, the segment released (cleared first; the original's second release
// check after that is dead), the name closed, then the base (0x9b6030).
cSGResource::~cSGResource()
{
    if (cIGZUnknown *segment = mSegment) {
        mSegment = nullptr;
        segment->Release();
    }
    mName.Close();
}

// 0xa3c4d0 (ret 8): a null out answers false. cISGResource and the three faces it derives from give the virtual base,
// counted through its own AddRef (slot 1, virtual); everything else is the base's (0x437e30).
bool cSGResource::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    switch (iid) {
    case cISGResource::kIid:
    case kIidReferent:
    case kIidPersistResource2:
    case kIidReferent2: {
        cISGResource *const face = this; // through the vbptr
        *out = face;
        face->AddRef();
        return true;
    }
    }
    return cGZPersistResource::QueryInterface(iid, out);
}

uint32_t cSGResource::AddRef() { return cGZPersistResource::AddRef(); }
uint32_t cSGResource::Release() { return cGZPersistResource::Release(); }

// 0xa3c540 (ret 4): the type-info record, the name's text into the name cell (interned, flag 1), the key flag set; then
// the resource manager (the framework's service) finds the segment holding this resource's key into mSegment, the old
// one released first. The record's name is released last.
void cSGResource::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    {
        nTSSG::SGLocalString text;
        reader->GetGZStr(&text.value);
        ServiceCell temp;
        mName.Assign(&temp.Open(Word(text.Text()), 1)->mValue);
        temp.Close();
        mHasKey = 1;

        RZAutoRef manager{nullptr};
        if (auto *framework = static_cast<cIGZFrameWork *>(GetServiceObject()))
            framework->GetSystemService(kResourceManagerService, kIidResourceManager, &manager.mObject);
        if (cIGZUnknown *old = mSegment) {
            mSegment = nullptr;
            old->Release();
        }
        // The original calls the manager without a null check (the service is there from boot); so does this.
        cGZPersistResourceKey key = GetKey();
        static_cast<cIGZPersistResourceManager *>(manager.mObject)
            ->vf34(Word(&key), reinterpret_cast<void **>(&mSegment));
        manager.ReleaseObject();
    }
    if (info.mName) info.mName->Release();
}

// 0xa3ce10 (hidden out): {"cSGResource" (a new counted cRZString), 0, version 2}.
cPersistTypeInfo cSGResource::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cSGResource");
    info.mWord4 = 0;
    info.mVersion = kVersion;
    return info;
}

uint32_t cSGResource::WriteResource(ifaces::cIGZPersistWriter *writer)
{
    // The record through 0xa3ce10 directly (not slot 4), then the name through slot 17 (virtual).
    cPersistTypeInfo info = cSGResource::GetTypeInfo();
    writer->WriteTypeInfo(&info);
    if (info.mName) info.mName->Release();
    return writer->WriteRZCharStr(const_cast<char *>(GetName()));
}

void cSGResource::Write(ifaces::cIGZPersistWriter *writer) { WriteResource(writer); }

// The slots whose bodies are the hook-shaped ones (each on this + 0x30, as the game's thunks call them).
uint32_t cSGResource::GetOwnerInfo() { return Word(static_cast<HelperBodies *>(Secondary())->VirtualBaseIfObject()); }
cIGZPersistResource *cSGResource::GetPersistResourceConst()
{
    return static_cast<cGZPersistResource *>(static_cast<SubBodies *>(Secondary())->GetOwner());
}
cIGZPersistResource *cSGResource::GetPersistResource()
{
    return static_cast<cGZPersistResource *>(static_cast<SubBodies *>(Secondary())->GetOwner());
}
cISGResource *cSGResource::GetSubResource(uint32_t index)
{
    return static_cast<cISGResource *>(
        static_cast<SubBodies *>(Secondary())->GetVirtualBase(reinterpret_cast<void *>(static_cast<uintptr_t>(index))));
}
uint8_t cSGResource::SubmitKeyToService() { return static_cast<HelperBodies *>(Secondary())->SubmitKeyToService(); }
int32_t cSGResource::CompareIdentity(cIGZUnknown *other) { return static_cast<SubBodies *>(Secondary())->CompareIdentity(other); }
uint8_t cSGResource::AssignAndSubmit(const char *name, cIGZUnknown *segment)
{
    return static_cast<HelperBodies *>(Secondary())->AssignAndSubmit(Word(name), segment);
}
uint8_t cSGResource::ResetAndSubmit(uint32_t group, uint32_t instance)
{
    return static_cast<HelperBodies *>(Secondary())
        ->ResetAndSubmit(reinterpret_cast<void *>(uintptr_t(group)), reinterpret_cast<void *>(uintptr_t(instance)));
}
uint8_t cSGResource::OpenAssignAndSubmit(const char *name, uint32_t group, cIGZUnknown *segment)
{
    return static_cast<HelperBodies *>(Secondary())
        ->OpenAssignAndSubmit(Word(name), reinterpret_cast<void *>(uintptr_t(group)), segment);
}
bool cSGResource::SetSegment(cIGZUnknown *segment) { return static_cast<SubBodies *>(Secondary())->SetResource(segment); }
void cSGResource::SetFlag(uint8_t value) { static_cast<SubBodies *>(Secondary())->SetFlag(value); }
cGZPersistResourceKey *cSGResource::CopyKeyTo(cGZPersistResourceKey *out)
{
    return static_cast<cGZPersistResourceKey *>(static_cast<SubBodies *>(Secondary())->CopyKeyTo(out));
}
uint8_t cSGResource::GetFlag() { return static_cast<SubBodies *>(Secondary())->GetFlag(); }
uint32_t cSGResource::ForwardKey() { return static_cast<SubBodies *>(Secondary())->ForwardKey(); }

// Slot 17 (0xa3c4b0: the name cell's text from the string pool); 20 / 21 are folded with other classes' (0x9e8850 the
// dword at this - 12, 0x9d9570 the byte at this - 8).
const char *cSGResource::GetName() { return reinterpret_cast<const char *>(static_cast<uintptr_t>(Dispatch8(&mName))); }
cIGZUnknown *cSGResource::GetSegment() { return mSegment; }
uint8_t cSGResource::HasKey() { return mHasKey; }

// ---- the RVA hooks' entry points ----

void __fastcall cSGResourceAdapters::Destruct(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->cSGResource::~cSGResource();
}

bool __fastcall cSGResourceAdapters::QueryInterface(Self *self, void *, uint32_t iid, void **out)
{
    [[msvc::forceinline_calls]] return self->cSGResource::QueryInterface(iid, out);
}

void __fastcall cSGResourceAdapters::Read(void *face, void *, ifaces::cIGZPersistReader *reader)
{
    [[msvc::forceinline_calls]] Owner(face)->cSGResource::Read(reader);
}

uint32_t __fastcall cSGResourceAdapters::Write(void *face, void *, ifaces::cIGZPersistWriter *writer)
{
    [[msvc::forceinline_calls]] return Owner(face)->cSGResource::WriteResource(writer);
}

} // namespace nGZSceneGraph
