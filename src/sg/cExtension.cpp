// nGZSceneGraph::cExtension as a whole class (lane OY). Layout, bodies and the constructor decision: src/sg/cExtension.h.
#include "sg/cExtension.h"

#include "game/game.h"
#include "gz/cIGZString.h"
#include "rz/cRZString.h" // the empty text 0xd9b9d0
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "sg/cObjectGraphNodeImpl.h" // NewTypeName

#include <cstddef>
#include <cstdint>

// 0xa3b7d0's / 0xa3b750's hook targets: our constructors' code (declared in cExtension.h). C++ cannot take a constructor's
// address, so C names stand for them and the linker resolves those names to the constructors' decorated ones (a wrong
// decoration fails the link: the C name is then unresolved).
#pragma comment(linker, "/alternatename:_OY_cExtension_Constructor=??0cExtension@nGZSceneGraph@@QAE@XZ")
#pragma comment(linker, "/alternatename:_OY_cExtension_CopyConstructor=??0cExtension@nGZSceneGraph@@QAE@ABV01@@Z")

namespace nGZSceneGraph {

static_assert(sizeof(cExtension) == 0x20, "cExtension size (the deleting destructor frees 0x20)");
static_assert(offsetof(cExtension, mRefCount) == 0x08, "cExtension count");
static_assert(offsetof(cExtension, mOwner) == 0x10, "cExtension owner");
static_assert(offsetof(cExtension, mNext) == 0x14, "cExtension next");

namespace {
uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
} // namespace

// 0xa3b7d0: the behaviour template's part (0xa3b710: its vtables, the count 0), then cExtension's vtables (the virtual
// base's through the vbptr the derived class set) and the vtordisp, both the compiler's; no owner, no next. The
// most-derived flag (ret 4) only says whether the vbptr is set here: the game's derived constructors set it and pass 0
// (the original's code has no flag test left; nothing builds a cExtension alone).
cExtension::cExtension() = default;

// 0xa3b750: the same; nothing of `other` is read (a clone starts unowned, unlinked and uncounted).
cExtension::cExtension(const cExtension &) : cExtension() {}

// 0xa3b880: the vtables back to cExtension's, the next extension released (not cleared), then the template's part
// (0xa3b850: its vtables).
cExtension::~cExtension()
{
    if (mNext) mNext->Release();
}

// 0xa3be60 (ret 8): a null out answers false. The referent face for its two iids, cIExtension for its own (each counted
// through the virtual base's AddRef, slot 1); everything else is the behaviour template's (0xa3bde0).
bool cExtension::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    cIExtension *const extension = this; // through the vbptr
    switch (iid) {
    case kIidReferent:
    case kIidSerializable2:
        *out = static_cast<cIGZPersistSerializableReferent *>(this);
        extension->AddRef();
        return true;
    case cIExtension::kIid:
        *out = extension;
        extension->AddRef();
        return true;
    }
    return BehaviorQueryInterface(iid, out);
}

// 0xa3bde0 (ret 8): 1 and the behaviour type answer the virtual base, counted; 0xfb859324 answers it uncounted.
bool cExtension::BehaviorQueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    cIExtension *const extension = this;
    if (iid == 1 || iid == kIidBehavior) {
        *out = extension;
        extension->AddRef();
        return true;
    }
    if (iid == kIidBehaviorUncounted) {
        *out = extension;
        return true;
    }
    return false;
}

uint32_t cExtension::AddRef() { return cExtensionBehavior::AddRef(); }
uint32_t cExtension::Release() { return cExtensionBehavior::Release(); }

// 0x9dc310 (folded, ret 4): the type-info record is read and its name released; nothing else.
void cExtension::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    if (info.mName) info.mName->Release();
}

// 0xa3c160 (hidden out): {"cExtension" (a new counted cRZString), 0, version 3}.
cPersistTypeInfo cExtension::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cExtension");
    info.mWord4 = 0;
    info.mVersion = kVersion;
    return info;
}

uint32_t cExtension::WriteExtension(ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info = cExtension::GetTypeInfo();
    const uint32_t written = writer->WriteTypeInfo(&info);
    if (info.mName) return info.mName->Release();
    return written;
}

void cExtension::Write(ifaces::cIGZPersistWriter *writer) { WriteExtension(writer); }

// 0xa3bf60: the owner's own answer (through its persist face, node slot 17), or 0 with no owner. The owner is asked
// twice, as the original does (virtual slot 18 both times).
uint32_t cExtension::GetOwnerInfo()
{
    if (!GetOwner()) return 0;
    return GetOwner()->GetReferent()->GetOwnerInfo();
}

// 0xa3b950: the persist face (null for a null object).
cIGZPersistSerializableReferent *cExtension::GetPersistReferent() { return this; }

cIObjectGraphNode *cExtension::GetOwnerConst() { return mOwner; }
cIObjectGraphNode *cExtension::GetOwner() { return mOwner; }
cIExtension *cExtension::GetNextConst() { return mNext; }
cIExtension *cExtension::GetNext() { return mNext; }

// 0xa3c0d0 (ret 4): uncounted; eax is the owner (the stored argument).
cIObjectGraphNode *cExtension::SetOwner(cIObjectGraphNode *owner)
{
    mOwner = owner;
    return owner;
}

// 0xa3c0f0 (ret 4): the new one counted, then the old one released; nothing when they are the same. eax is the last
// count answer (the old one's Release, else the new one's AddRef); with nothing to do the original leaves the caller's
// eax, ours 0 (nothing can rely on it).
uint32_t cExtension::SetNext(cIExtension *next)
{
    cIExtension *const old = mNext;
    if (next == old) return 0;
    uint32_t answer = 0;
    if (next) answer = next->AddRef();
    mNext = next;
    if (old) answer = old->Release();
    return answer;
}

// 0xa3c130 (ret 4): the interfaces QueryInterface knows, but the uncounted behaviour iid 0xfb859324. Only al is set
// (the rest of eax is the iid's upper bytes).
bool cExtension::IsKindOf(uint32_t iid)
{
    return iid == cIExtension::kIid || iid == kIidBehavior || iid == kIidSerializable2 || iid == kIidReferent || iid == 1;
}

bool cExtension::IsPersistent() { return true; }

// 0xa3bb90 (ret 4): the text's FromChar (slot 4) with the image's empty string, tail-called.
void cExtension::GetDescription(cIGZString *text) { text->FromChar(ts2_rva_d9b9d0); }

// ---- the RVA hooks' entry points ----

void __fastcall cExtensionAdapters::Destruct(Self *self, void *)
{
    [[msvc::forceinline_calls]] self->cExtension::~cExtension();
}

bool __fastcall cExtensionAdapters::QueryInterface(void *face, void *, uint32_t iid, void **out)
{
    [[msvc::forceinline_calls]] return FromReferent(face)->cExtension::QueryInterface(iid, out);
}

uint32_t __fastcall cExtensionAdapters::Release(void *face, void *)
{
    [[msvc::forceinline_calls]] return FromReferent(face)->cExtension::Release();
}

cPersistTypeInfo *__fastcall cExtensionAdapters::GetTypeInfo(void *face, void *, cPersistTypeInfo *out)
{
    [[msvc::forceinline_calls]] *out = FromReferent(face)->cExtension::GetTypeInfo();
    return out;
}

uint32_t __fastcall cExtensionAdapters::Write(void *face, void *, ifaces::cIGZPersistWriter *writer)
{
    [[msvc::forceinline_calls]] return FromReferent(face)->cExtension::WriteExtension(writer);
}

uint32_t __fastcall cExtensionAdapters::GetOwnerInfo(void *face, void *)
{
    [[msvc::forceinline_calls]] return FromReferent(face)->cExtension::GetOwnerInfo();
}

void __fastcall cExtensionAdapters::OnEvent(void *face, void *, uint32_t type, uint32_t arg1, void *data, uint32_t arg3)
{
    [[msvc::forceinline_calls]] FromBehavior(face)->cExtensionBehavior::OnEvent(type, arg1, data, arg3);
}

void __fastcall cExtensionAdapters::NotifyListeners(void *face, void *, uint32_t type, uint32_t arg1, void *data)
{
    [[msvc::forceinline_calls]] FromBehavior(face)->cExtensionBehavior::NotifyListeners(type, arg1, data);
}

bool __fastcall cExtensionAdapters::AskListeners(void *face, void *, uint32_t type, uint32_t arg1, void *data)
{
    [[msvc::forceinline_calls]] return FromBehavior(face)->cExtensionBehavior::AskListeners(type, arg1, data);
}

void __fastcall cExtensionAdapters::Connect(void *face, void *, uint32_t event, cIGZUnknown *target, uint32_t targetEvent)
{
    [[msvc::forceinline_calls]] FromBehavior(face)->cExtensionBehavior::Connect(event, target, targetEvent);
}

void __fastcall cExtensionAdapters::Disconnect(void *face, void *, uint32_t event, cIGZUnknown *target, uint32_t targetEvent)
{
    [[msvc::forceinline_calls]] FromBehavior(face)->cExtensionBehavior::Disconnect(event, target, targetEvent);
}

nGZEvent::cEventAttribute *__fastcall cExtensionAdapters::GetEventAttribute(void *face, void *, nGZEvent::cEventAttribute *out,
                                                                            uint32_t event)
{
    [[msvc::forceinline_calls]] *out = FromBehavior(face)->cExtensionBehavior::GetEventAttribute(event);
    return out;
}

cIGZPersistSerializableReferent *__fastcall cExtensionAdapters::GetPersistReferent(void *face, void *)
{
    [[msvc::forceinline_calls]] return FromExtension(face)->cExtension::GetPersistReferent();
}

cIObjectGraphNode *__fastcall cExtensionAdapters::SetOwner(void *face, void *, cIObjectGraphNode *owner)
{
    [[msvc::forceinline_calls]] return FromExtension(face)->cExtension::SetOwner(owner);
}

uint32_t __fastcall cExtensionAdapters::SetNext(void *face, void *, cIExtension *next)
{
    [[msvc::forceinline_calls]] return FromExtension(face)->cExtension::SetNext(next);
}

bool __fastcall cExtensionAdapters::IsKindOf(void *face, void *, uint32_t iid)
{
    [[msvc::forceinline_calls]] return FromExtension(face)->cExtension::IsKindOf(iid);
}

void __fastcall cExtensionAdapters::GetDescription(void *face, void *, cIGZString *text)
{
    [[msvc::forceinline_calls]] FromExtension(face)->cExtension::GetDescription(text);
}

} // namespace nGZSceneGraph
