// nGZSceneGraph::cGeometryBuilder (lane QE). Layout and the game's bodies: src/sg/cGeometryBuilder.h.
#include "sg/SGEventRecords.h"
#include "sg/cGeometryBuilder.h"

#include "game/game.h"
#include "game/original.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "sg/cObjectGraphNodeImpl.h" // NewTypeName

#include <cstddef>
#include <optional>

namespace nGZSceneGraph {

static_assert(offsetof(cGeometryBuilder, mRefCount) == 0x08, "cGeometryBuilder count");
static_assert(offsetof(cGeometryBuilder, mListener) == 0x10, "cGeometryBuilder listener");

namespace {
std::optional<nGZEvent::cEventAttributeMap> gBuilderRegistry;
}

// Built on first use. The original runs its holder 0xa44a20 first (on every call, a once byte skipping it after the first):
// the base events, then 0xbbb112f6 through this function, whose map is made by that call as a copy of the base registry
// (0x1485f0). (The holder's 3-byte object, kept in the static 0x10f60fc, is read by nothing.)
nGZEvent::cEventAttributeMap *__cdecl cGeometryBuilderBehaviorTraits::RegistryFor(uint32_t id,
                                                                                const nGZEvent::cEventAttribute *attribute)
{
    if (!gBuilderRegistry) {
        nGZEvent::cAbstractBehaviorType::RegisterBaseEvents();
        const nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
        gBuilderRegistry.emplace(*nGZEvent::cAbstractBehaviorType::RegistryFor(0, &defaults));
        (*gBuilderRegistry)[0xbbb112f6] = sgevents::kBuildGeometry;
    }
    if (id != 0) (*gBuilderRegistry)[id] = *attribute;
    return &*gBuilderRegistry;
}

void cGeometryBuilderBehaviorTraits::ResetRegistryForTest() { gBuilderRegistry.reset(); }

nGZEvent::cEventAttributeMap *cGeometryBuilderBehaviorTraits::Registry()
{
    const nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
    return RegistryFor(0, &defaults);
}

// 0xa45310 (ret 8): a null out answers false. The persist face for its iid, cIGeometryBuilder for its own (each counted
// through the virtual base's AddRef); everything else is the behaviour template's (0xa3bde0).
bool cGeometryBuilder::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    cIGeometryBuilder *const builder = this; // through the vbptr
    if (iid == kIidSerializable) {
        *out = static_cast<cIGZPersistSerializable2 *>(this);
        builder->AddRef();
        return true;
    }
    if (iid == cIGeometryBuilder::kIid) {
        *out = builder;
        builder->AddRef();
        return true;
    }
    return BehaviorQueryInterface(iid, out);
}

bool cGeometryBuilder::BehaviorQueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    cIGeometryBuilder *const builder = this;
    if (iid == 1 || iid == kIidBehavior) {
        *out = builder;
        builder->AddRef();
        return true;
    }
    if (iid == kIidBehaviorUncounted) {
        *out = builder;
        return true;
    }
    return false;
}

uint32_t cGeometryBuilder::AddRef() { return cGeometryBuilderBehavior::AddRef(); }
uint32_t cGeometryBuilder::Release() { return cGeometryBuilderBehavior::Release(); }

// 0x9dc310 (folded, ret 4): the type-info record is read and its name released; nothing else.
void cGeometryBuilder::Read(ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    if (info.mName) info.mName->Release();
}

// {"cGeometryBuilder" (a new counted cRZString), 0, version 3}.
cPersistTypeInfo cGeometryBuilder::GetTypeInfo()
{
    cPersistTypeInfo info;
    info.mName = NewTypeName("cGeometryBuilder");
    info.mWord4 = 0;
    info.mVersion = kVersion;
    return info;
}

void cGeometryBuilder::WriteBuilder(ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info = cGeometryBuilder::GetTypeInfo();
    writer->WriteTypeInfo(&info);
    if (info.mName) info.mName->Release();
}

// 0xa45790 (ret 4).
void cGeometryBuilder::Write(ifaces::cIGZPersistWriter *writer) { WriteBuilder(writer); }

// 0xa45660 (ret 0xc): the extra listener first, whether or not the builder is active (its OnEvent, slot 3, with the
// builder's cIGeometryBuilder as the source), then the template's notification of the connected listeners.
void cGeometryBuilder::NotifyListeners(uint32_t type, uint32_t arg1, void *data)
{
    if (mListener) {
        cIGeometryBuilder *const source = this;
        mListener->OnEvent(type, arg1, data, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(source)));
    }
    cGeometryBuilderBehavior::NotifyListeners(type, arg1, data);
}

// 0xa456c0 (ret 4).
nGZEvent::cIAbstractBehaviorType *cGeometryBuilder::SetListener(nGZEvent::cIAbstractBehaviorType *listener)
{
    mListener = listener;
    return listener;
}

// ---- the hook adapters ----

bool __fastcall cGeometryBuilderAdapters::QueryInterface(void *face, void *, uint32_t iid, void **out)
{
    [[msvc::forceinline_calls]] return FromFace(face, kPersistOffset)->cGeometryBuilder::QueryInterface(iid, out);
}

uint32_t __fastcall cGeometryBuilderAdapters::Release(void *face, void *)
{
    [[msvc::forceinline_calls]] return FromFace(face, kPersistOffset)->cGeometryBuilder::Release();
}

void __fastcall cGeometryBuilderAdapters::Write(void *face, void *, ifaces::cIGZPersistWriter *writer)
{
    [[msvc::forceinline_calls]] FromFace(face, kPersistOffset)->cGeometryBuilder::Write(writer);
}

void __fastcall cGeometryBuilderAdapters::OnEvent(void *face, void *, uint32_t type, uint32_t arg1, void *data, uint32_t arg3)
{
    [[msvc::forceinline_calls]] FromFace(face, kBehaviorOffset)->cGeometryBuilderBehavior::OnEvent(type, arg1, data, arg3);
}

bool __fastcall cGeometryBuilderAdapters::AskListeners(void *face, void *, uint32_t type, uint32_t arg1, void *data)
{
    [[msvc::forceinline_calls]] return FromFace(face, kBehaviorOffset)->cGeometryBuilderBehavior::AskListeners(type, arg1, data);
}

void __fastcall cGeometryBuilderAdapters::Connect(void *face, void *, uint32_t event, cIGZUnknown *target, uint32_t targetEvent)
{
    [[msvc::forceinline_calls]] FromFace(face, kBehaviorOffset)->cGeometryBuilderBehavior::Connect(event, target, targetEvent);
}

void __fastcall cGeometryBuilderAdapters::Disconnect(void *face, void *, uint32_t event, cIGZUnknown *target,
                                                     uint32_t targetEvent)
{
    [[msvc::forceinline_calls]] FromFace(face, kBehaviorOffset)->cGeometryBuilderBehavior::Disconnect(event, target, targetEvent);
}

uint32_t __fastcall cGeometryBuilderAdapters::GetEventCount(void *face, void *)
{
    [[msvc::forceinline_calls]] return FromFace(face, kBehaviorOffset)->cGeometryBuilderBehavior::GetEventCount();
}

nGZEvent::cAttributeIDRange *__fastcall cGeometryBuilderAdapters::GetEventIds(void *face, void *,
                                                                              nGZEvent::cAttributeIDRange *out)
{
    [[msvc::forceinline_calls]] *out = FromFace(face, kBehaviorOffset)->cGeometryBuilderBehavior::GetEventIds();
    return out;
}

nGZEvent::cEventAttribute *__fastcall cGeometryBuilderAdapters::GetEventAttribute(void *face, void *,
                                                                                  nGZEvent::cEventAttribute *out, uint32_t event)
{
    [[msvc::forceinline_calls]] *out = FromFace(face, kBehaviorOffset)->cGeometryBuilderBehavior::GetEventAttribute(event);
    return out;
}

void __fastcall cGeometryBuilderAdapters::NotifyListeners(void *face, void *, uint32_t type, uint32_t arg1, void *data)
{
    [[msvc::forceinline_calls]] FromFace(face, kListenerOffset)->cGeometryBuilder::NotifyListeners(type, arg1, data);
}

nGZEvent::cIAbstractBehaviorType *__fastcall cGeometryBuilderAdapters::SetListener(void *face, void *,
                                                                                   nGZEvent::cIAbstractBehaviorType *listener)
{
    [[msvc::forceinline_calls]] return FromFace(face, kListenerOffset)->cGeometryBuilder::SetListener(listener);
}

} // namespace nGZSceneGraph
