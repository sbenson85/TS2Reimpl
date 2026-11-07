// nGZSceneGraph::cSceneGraphFactory (lane RC). Layout, slots and what the game does: sg/cSceneGraphFactory.h.
#include "sg/cSceneGraphFactory.h"

#include "game/vtable.h"
#include "gz/cGZPersistResourceFactory.h"
#include "gz/cIGZCOM.h"
#include "gz/cIGZFrameWork.h"
#include "gz/cIStreamStringBehaviourAdaptor.h"
#include "gz/cOStreamStringBehaviourAdaptor.h"
#include "rz/RZAutoRef.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject: the framework
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject: the COM

#include <new>

namespace nGZSceneGraph {

namespace {

using LockFn = int32_t(__thiscall *)(void *);
using NoArgFn = uint32_t(__thiscall *)(void *);
using QueryFn = bool(__thiscall *)(void *, uint32_t, void **);
using RegisterFn = bool(__thiscall *)(void *, uint32_t, uint32_t, void *);

constexpr uint32_t kResourcesService = 0x056b906e; // the framework's persist resource manager service
constexpr uint32_t kResourcesIid = 0x656b8efc;
constexpr int kResourceManagerRegisterSlot = 20;
constexpr int kRecordContextSlot = 15; // the record's slot (offset 0x3c): the reader's context, by id
constexpr int kSerialGroupSlot = 20;   // the serial's slot (offset 0x50): the writer's context word

cGZPersistResourceFactory *Base(cSceneGraphFactory *self)
{
    return reinterpret_cast<cGZPersistResourceFactory *>(static_cast<cSceneGraphFactoryBase *>(self));
}

void ReleaseIfAny(cIGZUnknown *object)
{
    if (object) object->Release();
}

cIGZCOM *COM() { return static_cast<cIGZCOM *>(GetDirectorFrameworkObject()); }

} // namespace

// 0x9b6010 (count 0, the lock) and 0x9b6040 (the lock), the base part of every persist resource factory.
cSceneGraphFactoryBase::cSceneGraphFactoryBase() : mRefCount(0)
{
    mLock.Construct();
}

cSceneGraphFactoryBase::~cSceneGraphFactoryBase()
{
    mLock.Destruct();
}

cSceneGraphFactory *cSceneGraphFactory::Get()
{
    if (ts2_rva_10f61b0) return ts2_rva_10f61b0;
    ts2_rva_10f61b0 = new (std::nothrow) cSceneGraphFactory;
    return ts2_rva_10f61b0;
}

cIGZUnknown *cSceneGraphFactory::Create()
{
    cSceneGraphFactory *const factory = Get();
    if (!factory) return nullptr;
    cISceneGraphFactory *const face = factory;
    return face ? static_cast<cIGZUnknown *>(face) : nullptr;
}

// 0xa4d310: the base part, then the tables (the compiler's), then the counted fields zero.
cSceneGraphFactory::cSceneGraphFactory()
    : mOption14(0), mOption18(0), mUnused(nullptr), mWriterSource(nullptr), mReader(nullptr), mWriter(nullptr)
{
}

// Slot 7 (0xa4d570) runs 0xa4d3e0: the global cleared, then +0x28, +0x24, +0x20, +0x1c released, then the base part.
cSceneGraphFactory::~cSceneGraphFactory()
{
    ts2_rva_10f61b0 = nullptr;
    ReleaseIfAny(mWriter);
    ReleaseIfAny(mReader);
    ReleaseIfAny(mWriterSource);
    ReleaseIfAny(mUnused);
}

bool cSceneGraphFactory::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid == kFaceId) {
        cISceneGraphFactory *const face = this;
        *out = face;
        static_cast<cIGZUnknown *>(face)->AddRef();
        return true;
    }
    return Base(this)->QueryInterface(riid, out);
}

uint32_t cSceneGraphFactory::AddRef() { return Base(this)->AddRef(); }
uint32_t cSceneGraphFactory::Release() { return Base(this)->Release(); }

bool cSceneGraphFactory::CreateInstance(ifaces::cIGZPersistDBRecord *record, uint32_t riid, void **out, uint32_t a4,
                                        uint32_t a5)
{
    return Base(this)->CreateInstance(record, riid, out, a4, a5);
}

bool cSceneGraphFactory::CreateInstanceByType(uint32_t type, uint32_t riid, void **out, uint32_t a4, uint32_t a5)
{
    return Base(this)->CreateInstance2(type, riid, out, a4, a5);
}

bool cSceneGraphFactory::CreateInstance3(ifaces::cIGZPersistDBRecord *record, uint32_t riid, void **out)
{
    return CreateInstance(record, riid, out, 0, 0);
}

uint32_t cSceneGraphFactory::CreateResource(uint32_t type, uint32_t riid, void **out)
{
    if (!out) return 0;
    *out = nullptr;
    return COM()->GetClassObject(type, riid, out); // the callers read the low byte of the COM's word, as with the original
}

bool cSceneGraphFactory::ReadRecord(cIGZUnknown *, cIGZUnknown *) { return false; }
bool cSceneGraphFactory::WriteRecord(cIGZUnknown *, cIGZUnknown *) { return false; }

bool cSceneGraphFactory::Read(cIGZUnknown *resource, cIGZUnknown *record)
{
    VirtualCall<LockFn, 1>(&mLock);
    bool result = false;
    cIGZUnknown *stream = nullptr;
    if (record->QueryInterface(kIStreamId, reinterpret_cast<void **>(&stream))) {
        cIGZUnknown *serial = nullptr;
        resource->QueryInterface(kSerialIdRead, reinterpret_cast<void **>(&serial)); // the answer is not looked at
        auto *const adapter = new (std::nothrow) cIStreamStringBehaviourAdaptor;
        if (adapter) {
            adapter->Construct(stream);
            VirtualCall<NoArgFn, 1>(adapter);
        }
        cIGZUnknown *context = nullptr;
        VirtualCall<QueryFn, kRecordContextSlot>(record, kContextId, reinterpret_cast<void **>(&context));
        GetBinaryReader()->ReadRoot(serial, adapter, context);
        result = true;
        ReleaseIfAny(context);
        if (adapter) VirtualCall<NoArgFn, 2>(adapter);
        ReleaseIfAny(serial);
    }
    ReleaseIfAny(stream);
    VirtualCall<LockFn, 2>(&mLock);
    return result;
}

bool cSceneGraphFactory::Write(cIGZUnknown *resource, cIGZUnknown *record)
{
    VirtualCall<LockFn, 1>(&mLock);
    bool result = false;
    cIGZUnknown *stream = nullptr;
    if (record->QueryInterface(kOStreamId, reinterpret_cast<void **>(&stream))) {
        cIGZUnknown *serial = nullptr;
        resource->QueryInterface(kSerialIdWrite, reinterpret_cast<void **>(&serial)); // the answer is not looked at
        auto *const adapter = new (std::nothrow) cOStreamStringBehaviourAdaptor;
        if (adapter) {
            adapter->Construct(stream);
            VirtualCall<NoArgFn, 1>(adapter);
        }
        const uint32_t context = VirtualCall<NoArgFn, kSerialGroupSlot>(serial); // no null check on the serial
        GetBinaryWriter()->WriteRoot(serial, adapter, reinterpret_cast<void *>(context));
        result = true;
        if (adapter) VirtualCall<NoArgFn, 2>(adapter);
        ReleaseIfAny(serial);
    }
    ReleaseIfAny(stream);
    VirtualCall<LockFn, 2>(&mLock);
    return result;
}

// 0xa4d900: the resource manager (the framework's service 0x056b906e) takes the class id under the factory id 0xe5259c1b.
// The original never tests the service it got.
void cSceneGraphFactory::RegisterClass(uint32_t classId)
{
    RZAutoRef resources{};
    if (auto *const framework = static_cast<cIGZFrameWork *>(GetServiceObject()))
        framework->GetSystemService(kResourcesService, kResourcesIid, &resources.mObject);
    VirtualCall<RegisterFn, kResourceManagerRegisterSlot>(resources.mObject, kClassId, classId, nullptr);
    resources.ReleaseObject();
}

void cSceneGraphFactory::SetBinaryReader(cIBinaryReader *reader)
{
    cIBinaryReader *const old = mReader;
    if (reader == old) return;
    if (reader) reader->AddRef();
    mReader = reader;
    if (old) old->Release();
}

void cSceneGraphFactory::SetBinaryWriter(cIBinaryWriter *writer)
{
    cIBinaryWriter *const old = mWriter;
    if (writer == old) return;
    if (writer) writer->AddRef();
    mWriter = writer;
    if (old) old->Release();
}

void cSceneGraphFactory::SetOption14(uint32_t value)
{
    if (mOption14 != value) mOption14 = value;
}

void cSceneGraphFactory::SetOption18(uint32_t value)
{
    if (mOption18 == value) return;
    cIBinaryWriter *const writer = mWriter;
    mOption18 = value;
    if (!writer) {
        GetBinaryWriter();
        return;
    }
    if (value == 0) {
        cIBinaryWriter *const source = mWriterSource;
        if (source != writer) {
            if (source) source->AddRef();
            mWriter = source;
            writer->Release();
        }
    }
}

cIGZPersistResourceFactory *cSceneGraphFactory::GetResourceFactory()
{
    return this;
}

cIBinaryReader *cSceneGraphFactory::GetBinaryReader()
{
    if (!mReader) COM()->GetClassObject(kReaderClass, kReaderIid, reinterpret_cast<void **>(&mReader));
    return mReader;
}

cIBinaryWriter *cSceneGraphFactory::GetBinaryWriter()
{
    if (!mWriter) {
        COM()->GetClassObject(kWriterClass, kWriterIid, reinterpret_cast<void **>(&mWriterSource));
        if (mOption18 == 0) {
            mWriterSource->AddRef(); // no null check
            mWriter = mWriterSource;
        }
    }
    return mWriter;
}

uint32_t __cdecl cSceneGraphClassIds::Factory() { return 0xe5259c1b; }
uint32_t __cdecl cSceneGraphClassIds::Shape() { return 0xfc6eb1f7; }
uint32_t __cdecl cSceneGraphClassIds::AmbientLight() { return 0xc9c81b9b; }
uint32_t __cdecl cSceneGraphClassIds::DirectionalLight() { return 0xc9c81ba3; }
uint32_t __cdecl cSceneGraphClassIds::PointLight() { return 0xc9c81ba9; }
uint32_t __cdecl cSceneGraphClassIds::SpotLight() { return 0xc9c81bad; }
uint32_t __cdecl cSceneGraphClassIds::View() { return 0xbca76ddc; }
uint32_t __cdecl cSceneGraphClassIds::GeometryNode() { return 0x7ba3838c; }
uint32_t __cdecl cSceneGraphClassIds::IndexedMeshBuilder() { return 0x9bffc10d; }
uint32_t __cdecl cSceneGraphClassIds::CompactorBuilder() { return 0xdcdda078; }
uint32_t __cdecl cSceneGraphClassIds::BoundingVolumeBuilder() { return 0x1cfeceb8; }
uint32_t __cdecl cSceneGraphClassIds::TangentSpaceBuilder() { return 0x5d054225; }
uint32_t __cdecl cSceneGraphClassIds::OldPart() { return 0x5ce7e026; }
uint32_t __cdecl cSceneGraphClassIds::Unknown925d0() { return 0xac689a41; }
uint32_t __cdecl cSceneGraphClassIds::MaterialDefinition() { return 0x49596978; }
uint32_t __cdecl cSceneGraphClassIds::Texture() { return 0x1c4a276c; }
uint32_t __cdecl cSceneGraphClassIds::LevelOfDetail() { return 0xed534136; }

} // namespace nGZSceneGraph
