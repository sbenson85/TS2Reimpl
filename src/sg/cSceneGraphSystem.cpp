// nGZSceneGraph::cSceneGraphSystem and its scene node iterator (lane OM, scenegraph N1 step 4). Layout, the switch and
// what stays game code: sg/cSceneGraphSystem.h.
#include "sg/cSceneGraphSystem.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cFileParser.h"
#include "gz/cIGZCOM.h"
#include "gz/cIGZFrameWork.h"
#include "rz/RZAutoRef.h"
#include "rz/RZFastFixedAllocators.h"
#include "rz/RZFixedAllocator.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject: the framework
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject: the COM
#include "sg/cGeometryDataContainerClass.h"
#include "sg/cMaterialManager.h"
#include "sg/cMaterialParser.h"
#include "sg/cResourceNode.h"
#include "sg/cSceneNode.h"
#include "sg/cStringPool.h"
#include "sg/cTextureManager.h"

#include <new>

namespace nGZSceneGraph {

namespace {

// The faces these classes reach through a virtual base table: the vbptr at `vbptrOffset` in `object` points at a table of
// 32-bit offsets (relative to the vbptr) to the virtual bases, `entry` picks one.
template <class Face> Face *ThroughVbptr(const void *object, uint32_t vbptrOffset, uint32_t entry)
{
    auto *const vbptr = static_cast<const uint8_t *>(object) + vbptrOffset;
    const int32_t *const table = *reinterpret_cast<const int32_t *const *>(vbptr);
    return reinterpret_cast<Face *>(const_cast<uint8_t *>(vbptr) + table[entry]);
}

// The cIGZUnknown of the factory's cISceneGraphFactory face and of the parser's cIMaterialParser face: entry 1 of the
// face's vbtable at +4.
cIGZUnknown *UnknownOf(const void *face) { return ThroughVbptr<cIGZUnknown>(face, 4, 1); }

// The scene graph factory (ours since lane RC: the static 0xa4d660 builds it on first use); its cISceneGraphFactory face,
// none for none, as the constructor checks.
cISceneGraphFactory *SceneGraphFactory() { return cSceneGraphFactory::Get(); }

cMaterialManager *MaterialManager() { return cMaterialManager::Get(); } // ours since lane RD (0xacc040)
cStringPool *StringPool() { return cStringPool::Instance(); } // ours since lane OX (0xadf8b0)

// The classes Init registers with the factory, in Init's order: each class's static GetClassId (cSceneNode's, cResourceNode's
// and cGeometryDataContainer's are their classes'; the others are cSceneGraphClassIds', lane RC).
uint32_t RegisteredClassId(uint32_t index)
{
    switch (index) {
    case 0: return cSceneNode::GetClassId();
    case 1: return cResourceNode::GetClassId();
    case 2: return cSceneGraphClassIds::Shape();
    case 3: return cSceneGraphClassIds::AmbientLight();
    case 4: return cSceneGraphClassIds::DirectionalLight();
    case 5: return cSceneGraphClassIds::PointLight();
    case 6: return cSceneGraphClassIds::SpotLight();
    case 7: return cSceneGraphClassIds::View();
    case 8: return cSceneGraphClassIds::GeometryNode();
    case 9: return cGeometryDataContainer::GetClassId();
    case 10: return cSceneGraphClassIds::IndexedMeshBuilder();
    case 11: return cSceneGraphClassIds::CompactorBuilder();
    case 12: return cSceneGraphClassIds::BoundingVolumeBuilder();
    case 13: return cSceneGraphClassIds::TangentSpaceBuilder();
    case 14: return cSceneGraphClassIds::OldPart();
    case 15: return cSceneGraphClassIds::Unknown925d0();
    case 16: return cSceneGraphClassIds::MaterialDefinition();
    case 17: return cSceneGraphClassIds::Texture();
    default: return cSceneGraphClassIds::LevelOfDetail();
    }
}
constexpr uint32_t kRegisteredClasses = 19;

// The material parser Init builds: 0x304 bytes, the game's constructor (1: build the virtual bases), its cIMaterialParser
// face through the vbptr at +8 (entry 3); a failed allocation gives no parser.
constexpr uint32_t kMaterialParserSize = 0x304;
cIMaterialParser *NewMaterialParser()
{
    void *const memory = ::operator new(kMaterialParserSize);
    if (!memory) return nullptr;
    void *const parser = static_cast<cMaterialParser *>(memory)->Construct(1);
    return parser ? ThroughVbptr<cIMaterialParser>(parser, 8, 3) : nullptr;
}

// The parser's cIBaseParser face, what the file parser parses with: entry 2 of the face's vbtable at +4.
nGZCommandParser::cIBaseParser *BaseParserOf(cIMaterialParser *parser)
{
    return parser ? ThroughVbptr<nGZCommandParser::cIBaseParser>(parser, 4, 2) : nullptr;
}

// A member the COM fills: released (and cleared) first, then asked for.
template <class T> void GetClassObject(cIGZCOM *com, uint32_t classId, uint32_t iid, T *&slot)
{
    if (T *const old = slot) {
        slot = nullptr;
        old->Release();
    }
    com->GetClassObject(classId, iid, reinterpret_cast<void **>(&slot));
}

// A face counted through its virtual cIGZUnknown, cleared then released.
template <class T> void ReleaseFace(T *&slot)
{
    if (T *const old = slot) {
        slot = nullptr;
        UnknownOf(old)->Release();
    }
}

template <class T> void ReleasePlain(T *&slot)
{
    if (T *const old = slot) {
        slot = nullptr;
        old->Release();
    }
}

} // namespace

// ---- cSceneNodeIterator ----

void *cSceneNodeIterator::operator new(size_t) noexcept { return reinterpret_cast<RZFixedAllocatorBase *>(&ts2_rva_10f617c)->Alloc(); }

void cSceneNodeIterator::operator delete(void *block) noexcept { reinterpret_cast<RZFastFixed0xc *>(&ts2_rva_10f617c)->Free(block); }

bool cSceneNodeIterator::QueryInterface(uint32_t riid, void **out)
{
    if (!out || riid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cSceneNodeIterator::AddRef() { return ++mRefCount; }

uint32_t cSceneNodeIterator::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

cISceneNodeIterator *cSceneNodeIterator::Clone() { return new cSceneNodeIterator(*this); }

bool cSceneNodeIterator::IsEqual(const cISceneNodeIterator *other)
{
    return mAt == static_cast<const cSceneNodeIterator *>(other)->mAt;
}

cISceneNode *cSceneNodeIterator::Get() { return *mAt; }

void cSceneNodeIterator::Next() { ++mAt; }

// ---- cSceneGraphSystem ----

cSceneGraphSystem::cSceneGraphSystem()
    : rz::cRZSystemService(kServiceId, kPriority), mFactory(nullptr), mRenderGroups(nullptr), mDisplayList(nullptr),
      mFileParser(nullptr), mMaterialParser(nullptr), mLODLevels(nullptr)
{
    cISceneGraphFactory *const factory = SceneGraphFactory();
    if (factory != mFactory) {
        if (factory) UnknownOf(factory)->AddRef();
        mFactory = factory;
    }
}

cSceneGraphSystem::~cSceneGraphSystem()
{
    if (mLODLevels) mLODLevels->Release();
    if (mMaterialParser) UnknownOf(mMaterialParser)->Release();
    if (mFileParser) mFileParser->Release();
    if (mDisplayList) mDisplayList->Release();
    if (mRenderGroups) mRenderGroups->Release();
    if (mFactory) UnknownOf(mFactory)->Release();
}

cSceneGraphSystem *cSceneGraphSystem::Create() { return new cSceneGraphSystem; }

bool cSceneGraphSystem::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid != cISceneGraphSystem::kIid && riid != cISceneGraphSystemPrivate::kIid) {
        return rz::cRZSystemService::QueryInterface(riid, out);
    }
    *out = static_cast<cISceneGraphSystemPrivate *>(this);
    AddRef();
    return true;
}

uint32_t cSceneGraphSystem::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cSceneGraphSystem::Release() { return rz::cRZSystemService::Release(); }

cSceneNodeRange cSceneGraphSystem::GetSceneNodes()
{
    cSceneNodeIterator *const end = new cSceneNodeIterator(mSceneNodes.cend());
    cSceneNodeIterator *const begin = new cSceneNodeIterator(mSceneNodes.cbegin());
    cSceneNodeRange range;
    range.mCurrent = begin->Clone(); // the original calls the clone through the vtable without a null check on begin
    if (range.mCurrent) range.mCurrent->AddRef();
    range.mBegin = begin;
    begin->AddRef(); // the original's AddRef here has no null check either
    range.mEnd = end;
    if (end) end->AddRef();
    return range;
}

cISceneGraphFactory *cSceneGraphSystem::GetFactory() { return mFactory; }
cMaterialManager *cSceneGraphSystem::GetMaterialManager() { return MaterialManager(); }
cTextureManager *cSceneGraphSystem::GetTextureManager() { return cTextureManager::Get(); } // ours since lane SE (0xade2e0)
cIRenderGroupManager *cSceneGraphSystem::GetRenderGroups() { return mRenderGroups; }
cStringPool *cSceneGraphSystem::GetStringPool() { return StringPool(); }
cIDisplayList *cSceneGraphSystem::GetDisplayList() { return mDisplayList; }
nGZCommandParser::cIFileParser *cSceneGraphSystem::GetFileParser() { return mFileParser; }
cIMaterialParser *cSceneGraphSystem::GetMaterialParser() { return mMaterialParser; }
cILODLevelsStore *cSceneGraphSystem::GetLODLevels() { return mLODLevels; }

void cSceneGraphSystem::AddSceneNode(cISceneNode *node) { mSceneNodes.insert(node); }

void cSceneGraphSystem::RemoveSceneNode(cISceneNode *node) { mSceneNodes.erase(node); }

bool cSceneGraphSystem::Init()
{
    for (uint32_t index = 0; index < kRegisteredClasses; ++index) {
        cISceneGraphFactory *const factory = SceneGraphFactory();
        factory->RegisterClass(RegisteredClassId(index));
    }
    RawSlot<3>(StringPool());

    GetClassObject(static_cast<cIGZCOM *>(GetDirectorFrameworkObject()), 0x4973819d, 0xc9737f2c, mRenderGroups);

    cIMaterialParser *const parser = NewMaterialParser();
    if (parser != mMaterialParser) {
        cIMaterialParser *const old = mMaterialParser;
        if (parser) UnknownOf(parser)->AddRef();
        mMaterialParser = parser;
        if (old) UnknownOf(old)->Release();
    }

    GetClassObject(static_cast<cIGZCOM *>(GetDirectorFrameworkObject()), nGZCommandParser::cFileParser::kClassId,
                   nGZCommandParser::cFileParser::kInterfaceId, mFileParser);
    RawSlot<15>(cTextureManager::Get());
    MaterialManager()->Start();
    GetClassObject(static_cast<cIGZCOM *>(GetDirectorFrameworkObject()), 0x49d02f15, 0x49d02f0b, mDisplayList);
    mDisplayList->Init();      // no null check in the original
    mFileParser->Init();       // nor here
    mFileParser->SetParser(BaseParserOf(mMaterialParser), 1);
    GetClassObject(static_cast<cIGZCOM *>(GetDirectorFrameworkObject()), 0x2c674b2c, 0x2c674b25, mLODLevels);
    RawSlot<5>(mLODLevels);

    static_cast<cIGZFrameWork *>(GetServiceObject())->AddToTick(this);
    return true;
}

bool cSceneGraphSystem::Shutdown()
{
    // The framework's checked only before the service lookup, not before RemoveFromTick (it outlives its services).
    static_cast<cIGZFrameWork *>(GetServiceObject())->RemoveFromTick(this);
    RZAutoRef resources{};
    if (auto *const framework = static_cast<cIGZFrameWork *>(GetServiceObject())) {
        framework->GetSystemService(0x056b906e, 0x656b8efc, &resources.mObject);
    }
    RawSlot<45>(resources.mObject); // AdvanceCacheGenerations, without a null check

    if (mDisplayList) {
        mDisplayList->Shutdown();
        ReleasePlain(mDisplayList);
    }
    MaterialManager()->Shutdown();
    RawSlot<16>(cTextureManager::Get());
    ReleaseFace(mFactory);
    ReleasePlain(mRenderGroups);
    if (mFileParser) {
        mFileParser->Shutdown();
        ReleasePlain(mFileParser);
    }
    ReleaseFace(mMaterialParser);
    if (mLODLevels) {
        RawSlot<6>(mLODLevels);
        ReleasePlain(mLODLevels);
    }
    RawSlot<4>(StringPool());
    resources.ReleaseObject();
    return true;
}

bool cSceneGraphSystem::OnTick(uint32_t)
{
    GetMaterialManager()->OnTick();
    return true;
}

} // namespace nGZSceneGraph
