// nTSCASCore::cTSCASBodyPartSystem: the constructor 0xcddf40 and the creator 0xcebc20 (lane KB), the destructor 0xcdfb50 and
// the class in the standalone shape (lane KC): our vtables, the forwarding overrides of the cITSCASBodyPartSystem face and the
// hooks' adapters. See cTSCASBodyPartSystem.h.
#include "cas/cTSCASBodyPartSystem.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"

#include <cstring>
#include <new>

namespace nTSCASCore {

namespace {

// The node sizes the image's head allocators ask the allocator service for (the typed maps' own: ResourceMap 0x18 from
// 0x2040a0, AgeNameMap 0x38 from 0xcfceb0, SurfaceMap 0x50 from 0xcfcf50).
static_assert(sizeof(std::_Tree_node<ResourceMap::value_type, void *>) == 0x18 &&
                  sizeof(std::_Tree_node<AgeNameMap::value_type, void *>) == 0x38 &&
                  sizeof(std::_Tree_node<SurfaceMap::value_type, void *>) == 0x50,
              "the embedded maps' node sizes");
constexpr size_t kNodeResources = 0x18;    // +0x4c
constexpr size_t kNodeAgeNames = 0x38;     // +0x54 (four), +0x74; 0xcfceb0
constexpr size_t kNodeOffsetLimits = 0x30; // +0x20, 0xcfce10
constexpr size_t kNode24 = 0x24;           // +0x28, 0xcfca50
constexpr size_t kNode1C = 0x1c;           // +0x2c, +0x30, +0x3c, +0x48, 0x3cff20
constexpr size_t kNodeSnapshot = 0x78;     // +0x44, 0xcfcaf0
constexpr size_t kNodeFloatNames = 0x34;   // the float name map's

// nTSCASCore::cFloatNameMap (0xc bytes): its vtable and a map.
constexpr uint32_t kFloatNameMapVTable = 0xeba0dc;
struct FloatNameMap {
    const void *mVTable;
    TreeHeader mNames;
};

// The skin filters (nTSCASCore::cTSSkinBaseFilter, cTSSkinFilter, cTSOverlayFilter; the image's classes): the base
// part's constructor 0xb64d00, the vtables the derived constructors store inline (+0 primary, +0x54 the second face).
constexpr size_t kSkinBaseFilterSize = 0x58, kSkinFilterSize = 0x7c, kOverlayFilterSize = 0x78;
constexpr uint32_t kCommonFaceVTable = 0xdde514;                                  // the base's second face
constexpr uint32_t kSkinBaseFilterVTable = 0xdde904, kSkinBaseFilterFaceVTable = 0xdde928;
constexpr uint32_t kOverlayFilterVTable = 0xdde940, kOverlayFilterFaceVTable = 0xdde964;

void ConstructFilterBase(uint8_t *filter)
{
    OriginalFunction<void *(__thiscall *)(void *), 0xb64d00>()(filter);
}
void SetWord(uint8_t *p, size_t offset, const void *value) { std::memcpy(p + offset, &value, 4); }

void *NewSkinBaseFilter()
{
    auto *const f = static_cast<uint8_t *>(::operator new(kSkinBaseFilterSize));
    std::memset(f, 0, kSkinBaseFilterSize);
    ConstructFilterBase(f);
    SetWord(f, 0x54, GameAddress(kCommonFaceVTable));
    SetWord(f, 0, GameAddress(kSkinBaseFilterVTable));
    SetWord(f, 0x54, GameAddress(kSkinBaseFilterFaceVTable));
    return f;
}
void *NewSkinFilter()
{
    void *const f = ::operator new(kSkinFilterSize);
    return OriginalFunction<void *(__thiscall *)(void *, uint32_t), 0xb64e00>()(f, 0);
}
void *NewOverlayFilter()
{
    auto *const f = static_cast<uint8_t *>(::operator new(kOverlayFilterSize));
    std::memset(f, 0, kOverlayFilterSize);
    ConstructFilterBase(f);
    SetWord(f, 0x54, GameAddress(kCommonFaceVTable));
    SetWord(f, 0x54, GameAddress(kSkinBaseFilterFaceVTable));
    SetWord(f, 0, GameAddress(kOverlayFilterVTable));
    SetWord(f, 0x54, GameAddress(kOverlayFilterFaceVTable));
    std::memset(f + 0x58, 0, 0x20); // two empty vectors' worth of words
    return f;
}

// A counted pointer member assigned: the new object AddRef'd, stored, the old one released.
void Assign(void *&member, void *object)
{
    void *const old = member;
    if (object == old) return;
    if (object) VirtualCall<uint32_t(__thiscall *)(void *), 1>(object);
    member = object;
    if (old) VirtualCall<uint32_t(__thiscall *)(void *), 2>(old);
}

// A counted pointer member released (slot 2) when set; the pointer is left as it was.
void ReleaseMember(void *object)
{
    if (object) VirtualCall<uint32_t(__thiscall *)(void *), 2>(object);
}

BodyPartEntryVector *NewVector() { return new BodyPartEntryVector{nullptr, nullptr, nullptr}; }

// A heap map deleted as the destructor does each one: the image's erase from the root, the head freed, then the header
// (nothing reset; nothing when the pointer is null).
// Each map's erase from its root (the image's: values destroyed, nodes freed to the allocator service).
using Erase = void(__thiscall *)(TreeHeader *, TreeHeader *, TreeHeader::Node *);
using EraseAllFn = void (*)(TreeHeader *);
void EraseOffsetLimits(TreeHeader *t) { OriginalFunction<Erase, 0xcdbf10>()(t, t, t->mHead->mParent); } // +0x20
void EraseTree28(TreeHeader *t) { OriginalFunction<Erase, 0xcdb5e0>()(t, t, t->mHead->mParent); }       // +0x28
void EraseTree2C(TreeHeader *t) { OriginalFunction<Erase, 0xcdb4a0>()(t, t, t->mHead->mParent); }       // +0x2c
void EraseTree30(TreeHeader *t) { OriginalFunction<Erase, 0xcdb400>()(t, t, t->mHead->mParent); }       // +0x30
void EraseTree3C(TreeHeader *t) { OriginalFunction<Erase, 0xcdb970>()(t, t, t->mHead->mParent); }       // +0x3c
void EraseSnapshots(TreeHeader *t) { OriginalFunction<Erase, 0xcdb710>()(t, t, t->mHead->mParent); }    // +0x44
void EraseMaterials(TreeHeader *t) { OriginalFunction<Erase, 0xcdb540>()(t, t, t->mHead->mParent); }    // +0x48
void EraseTree158(TreeHeader *t) { OriginalFunction<Erase, 0xd0a8e0>()(t, t, t->mHead->mParent); }      // +0x158 (0xd0a980's)
void EraseResources(TreeHeader *t) { OriginalFunction<Erase, 0xcdbc10>()(t, t, t->mHead->mParent); }    // +0x4c
void EraseAgeNames(TreeHeader *t) { OriginalFunction<Erase, 0xcdc280>()(t, t, t->mHead->mParent); }     // +0x54 (four)
void EraseTree74(TreeHeader *t) { OriginalFunction<Erase, 0xcdc300>()(t, t, t->mHead->mParent); }       // +0x74

void DeleteHeapTree(TreeHeader *tree, EraseAllFn eraseAll)
{
    if (tree == nullptr) return;
    eraseAll(tree);
    GZServiceFree(tree->mHead);
    ::operator delete(tree, sizeof(TreeHeader));
}

// A stack deleted: the vector's destructor (the image's 0xcdf230: each record's destructor 0xcdf820, the buffer freed), then
// the header.
void DeleteStack(BodyPartEntryVector *stack)
{
    if (stack == nullptr) return;
    OriginalFunction<void(__thiscall *)(BodyPartEntryVector *), 0xcdf230>()(stack);
    ::operator delete(stack, sizeof(BodyPartEntryVector));
}

// An embedded map destroyed: the erase, then the head freed (0xcdea90 for the per-age maps).
void DestroyEmbedded(TreeHeader *tree, EraseAllFn eraseAll)
{
    eraseAll(tree);
    GZServiceFree(tree->mHead);
}

} // namespace

ResourceTree::ResourceTree() { InitTree(this, kNodeResources); }
ResourceTree::~ResourceTree() { DestroyEmbedded(this, EraseResources); }
AgeNameTree::AgeNameTree() { InitTree(this, kNodeAgeNames); }
AgeNameTree::~AgeNameTree() { DestroyEmbedded(this, EraseAgeNames); }
Tree74::Tree74() { InitTree(this, kNodeAgeNames); }
Tree74::~Tree74() { DestroyEmbedded(this, EraseTree74); }

SystemName::SystemName()
{
    mText.mVTable = GameAddress<void>(cRZString::kVTable);
    std::memset(&mText.mString, 0, sizeof(mText.mString));
    ::new (&mText.mString) std::string();
    mText.mRefCount = 0;
}
SystemName::~SystemName()
{
    mText.mVTable = GameAddress<void>(cRZString::kVTable);
    mText.mString.~basic_string();
}

cTSCASBodyPartSystem::cTSCASBodyPartSystem() : rz::cRZSystemService(kServiceId, kPriority)
{
    // The embedded containers are the members' constructors (+0x4c .. +0xb4, in that order).
    std::memset(mGuids, 0, sizeof(mGuids));
    mByte128 = 0;
    mDword12C = 6;
    mDword130 = 2;
    mAge = 0;
    mDword138 = 0x3f;
    mByte13C = 1;
    mDword140 = 0;
    mByte144 = 0;
    mByte145 = 1;
    mByte146 = 0;
    mDirectorCookie = 0;
    OriginalFunction<void *(__thiscall *)(void *), 0xcfdf50>()(&mLockedObjects);
    mTree158 = nullptr; // 0xd0a970
    mSkinBaseFilter = nullptr;
    mSkinFilter = nullptr;
    mOverlayFilter = nullptr;

    // The containers on the heap.
    mTree20 = NewHeapTree(kNodeOffsetLimits);
    mRecords = new std::vector<SurfaceRecord, CasAlloc<SurfaceRecord>>();
    mTree28 = NewHeapTree(kNode24);
    mTree2C = NewHeapTree(kNode1C);
    mTree30 = NewHeapTree(kNode1C);
    mRedoStack = NewVector();
    mStack = NewVector();
    mTree3C = NewHeapTree(kNode1C);
    auto *const floatNames = static_cast<FloatNameMap *>(::operator new(sizeof(FloatNameMap)));
    floatNames->mVTable = GameAddress(kFloatNameMapVTable);
    InitTree(&floatNames->mNames, kNodeFloatNames);
    mFloatNameMap = floatNames;
    mTree44 = NewHeapTree(kNodeSnapshot);
    mTree48 = NewHeapTree(kNode1C);

    Assign(mSkinBaseFilter, NewSkinBaseFilter());
    Assign(mSkinFilter, NewSkinFilter());
    Assign(mOverlayFilter, NewOverlayFilter());

    InitCategoryLists();
    InitTargetSurfaceNames();
    Iface()->ClearTreeAt14();
    Iface()->ResetState();
    ResetKeys();
}

cTSCASBodyPartSystem::~cTSCASBodyPartSystem()
{
    Iface()->ClearTreeAt14();
    Iface()->ResetState();
    DeleteHeapTree(mTree20, EraseOffsetLimits);
    delete mRecords; // the vector's destructor (the image's 0xcdf1a0: each record's strings, the buffer to the service)
    DeleteHeapTree(mTree28, EraseTree28);
    if (mFloatNameMap) VirtualCall<void *(__thiscall *)(void *, uint32_t), 0>(mFloatNameMap, 1); // its deleting destructor
    DeleteHeapTree(mTree2C, EraseTree2C);
    DeleteHeapTree(mTree30, EraseTree30);
    DeleteStack(mRedoStack);
    DeleteStack(mStack);
    DeleteHeapTree(mTree3C, EraseTree3C);
    DeleteHeapTree(mTree44, EraseSnapshots);
    DeleteHeapTree(mTree48, EraseMaterials);
    ReleaseMember(mOverlayFilter);
    ReleaseMember(mSkinFilter);
    ReleaseMember(mSkinBaseFilter);
    DeleteHeapTree(mTree158, EraseTree158);
    OriginalFunction<void(__thiscall *)(void *), 0xcfe450>()(&mLockedObjects);
    // Then the members (the name, the surface sets and map, the three id lists, +0x74, the per-age maps from the last,
    // +0x4c) and the service.
}

cTSCASBodyPartSystem *__cdecl cTSCASBodyPartSystem::Create() { return new cTSCASBodyPartSystem; }

// ---- adapters --------------------------------------------------------------------------------------------------------

cTSCASBodyPartSystem *__fastcall cTSCASBodyPartSystemAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cTSCASBodyPartSystem;
}
bool __fastcall cTSCASBodyPartSystemAdapters::QueryInterface(cTSCASBodyPartSystem *self, void *, uint32_t riid, void **out)
{
    [[msvc::forceinline_calls]] return self->cTSCASBodyPartSystem::QueryInterface(riid, out);
}
bool __fastcall cTSCASBodyPartSystemAdapters::Init(cTSCASBodyPartSystem *self, void *)
{
    [[msvc::forceinline_calls]] return self->cTSCASBodyPartSystem::Init();
}
bool __fastcall cTSCASBodyPartSystemAdapters::Shutdown(cTSCASBodyPartSystem *self, void *)
{
    [[msvc::forceinline_calls]] return self->cTSCASBodyPartSystem::Shutdown();
}
void __fastcall cTSCASBodyPartSystemAdapters::RemoveMorphRecords(cTSCASBodyPartSystem *self, void *)
{
    [[msvc::forceinline_calls]] self->cTSCASBodyPartSystem::RemoveMorphRecords();
}
void *__fastcall cTSCASBodyPartSystemAdapters::GetPartResource(cTSCASBodyPartSystem *self, void *, uint32_t kind, uint32_t key,
                                                               uint32_t flag)
{
    [[msvc::forceinline_calls]] return self->cTSCASBodyPartSystem::GetPartResource(kind, key, flag);
}

// ---- cIGZUnknown ------------------------------------------------------------------------------------------------------

uint32_t cTSCASBodyPartSystem::AddRef() { return rz::cRZSystemService::AddRef(); }
uint32_t cTSCASBodyPartSystem::Release() { return rz::cRZSystemService::Release(); }

// ---- cITSCASBodyPartSystem: the slots forward to the hook-shaped bodies (`this` the +0x1c face) -----------------------

void cTSCASBodyPartSystem::ClearData(uint32_t flags) { Iface()->ClearData(flags); }
void cTSCASBodyPartSystem::LoadFaceData() { Iface()->LoadFaceData(); }
void cTSCASBodyPartSystem::ImportPartData(void *source, const char *name) { Iface()->ImportPartData(source, name); }
void cTSCASBodyPartSystem::EraseAgeName(const char *name) { Iface()->EraseAgeName(name); }
bool cTSCASBodyPartSystem::HasAgeName(const char *name) { return Iface()->HasAgeName(name); }
bool cTSCASBodyPartSystem::HasActiveMorph(const char *name) { return Iface()->HasActiveMorph(name); }
void cTSCASBodyPartSystem::ResetState() { Iface()->ResetState(); }
void cTSCASBodyPartSystem::ClearTreeAt14() { Iface()->ClearTreeAt14(); }
const char *cTSCASBodyPartSystem::GetText() { return Iface()->GetText(); }
void cTSCASBodyPartSystem::SaveSnapshot(int32_t id) { Iface()->SaveSnapshot(id); }
bool cTSCASBodyPartSystem::RestoreSnapshot(int32_t id) { return Iface()->RestoreSnapshot(id); }
void cTSCASBodyPartSystem::PopEntry(uint32_t moveToRedo) { Iface()->PopEntry(moveToRedo); }
void cTSCASBodyPartSystem::RedoEntry() { Iface()->RedoEntry(); }
void cTSCASBodyPartSystem::ClearStacks() { Iface()->ClearStacks(); }
bool cTSCASBodyPartSystem::ClearPartWeight(int32_t value, int32_t category) { return Iface()->ClearPartWeight(value, category); }
int32_t cTSCASBodyPartSystem::StackCount() { return Iface()->StackCount(); }
int32_t cTSCASBodyPartSystem::RedoStackCount() { return Iface()->RedoStackCount(); }
void cTSCASBodyPartSystem::RefreshPart(uint32_t a, uint32_t b, uint32_t id) { Iface()->RefreshPart(a, b, id); }
void cTSCASBodyPartSystem::VisitIds(uint32_t a, uint32_t b) { Iface()->VisitIds(a, b); }
void cTSCASBodyPartSystem::ApplyAll(uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4, uint32_t p5, uint32_t p6)
{
    Iface()->ApplyAll(p1, p2, p3, p4, p5, p6);
}
void cTSCASBodyPartSystem::ApplyAllPasses(uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4) { Iface()->ApplyAllPasses(p1, p2, p3, p4); }
void cTSCASBodyPartSystem::ApplyPart(uint32_t kind, uint32_t key, uint32_t id, uint8_t allKinds, uint8_t allKeys, uint8_t allIds,
                                     uint8_t rebuild, uint8_t refresh, uint8_t firstPass, uint8_t resetIndex)
{
    Iface()->ApplyPart(kind, key, id, allKinds, allKeys, allIds, rebuild, refresh, firstPass, resetIndex);
}
bool cTSCASBodyPartSystem::RebuildPartMaterials(uint32_t kind, uint32_t key, uint32_t materialArg, uint8_t applyToParts)
{
    return Iface()->RebuildPartMaterials(kind, key, materialArg, applyToParts);
}
uint32_t cTSCASBodyPartSystem::CreateSkinMaterial(uint32_t id, uint32_t resourceArg, const char *name, uint32_t keyArg, uint8_t eyeGizmo)
{
    return Iface()->CreateSkinMaterial(id, resourceArg, name, keyArg, eyeGizmo);
}
uint32_t cTSCASBodyPartSystem::CreateHairMaterial(uint32_t id, uint32_t resourceArg, const char *name, uint32_t keyArg, uint32_t a,
                                                  uint32_t b, uint32_t species, uint8_t hair)
{
    return Iface()->CreateHairMaterial(id, resourceArg, name, keyArg, a, b, species, hair);
}
void cTSCASBodyPartSystem::ApplyOutfitProperties(uint32_t id, uint8_t reset) { Iface()->ApplyOutfitProperties(id, reset); }
void cTSCASBodyPartSystem::CopyFrom(void *source, uint8_t resetBlocks, uint8_t copyWord, uint8_t copyKeys, uint8_t copyNamed,
                                    uint8_t copyItems, uint8_t copyNames5)
{
    Iface()->CopyFrom(source, resetBlocks, copyWord, copyKeys, copyNamed, copyItems, copyNames5);
}
uint8_t cTSCASBodyPartSystem::ApplyToAllParts(void *target) { return Iface()->ApplyToAllParts(target); }
void cTSCASBodyPartSystem::RefreshGeometry(uint32_t kind, uint32_t key, uint32_t id, uint32_t partId)
{
    Iface()->RefreshGeometry(kind, key, id, partId);
}
void cTSCASBodyPartSystem::PushMarker() { Iface()->PushMarker(); }
void cTSCASBodyPartSystem::PushKind5(const void *items) { Iface()->PushKind5(items); }
bool cTSCASBodyPartSystem::ItemsMatchNewestKind5(const BodyPartItemVector *items) { return Iface()->ItemsMatchNewestKind5(items); }
void cTSCASBodyPartSystem::PushKind6(const NamedWeight *source) { Iface()->PushKind6(source); }
bool cTSCASBodyPartSystem::MatchesNewestKind6(const NamedWeight *named) { return Iface()->MatchesNewestKind6(named); }
void cTSCASBodyPartSystem::ApplyPartGuid(const uint32_t *guid, uint8_t on, uint8_t asNew) { Iface()->ApplyPartGuid(guid, on, asNew); }
void cTSCASBodyPartSystem::ApplyGuidText(const char *text, uint32_t a, uint32_t b) { Iface()->ApplyGuidText(text, a, b); }
void cTSCASBodyPartSystem::ClearGuidPartWeights(const uint32_t *guid) { Iface()->ClearGuidPartWeights(guid); }
uint32_t cTSCASBodyPartSystem::IsGuidApplied(uint32_t a, uint32_t b, const char *guidText) { return Iface()->IsGuidApplied(a, b, guidText); }
void cTSCASBodyPartSystem::ApplyModifierTexts(const StringVector *texts) { Iface()->ApplyModifierTexts(texts); }
void cTSCASBodyPartSystem::ExportModifierTexts(StringVector *out) { Iface()->ExportModifierTexts(out); }
void cTSCASBodyPartSystem::ApplyNamesValue0B(const StringVector *names) { Iface()->ApplyNamesValue0B(names); }
void cTSCASBodyPartSystem::CollectNamesValue0B(StringVector *out) { Iface()->CollectNamesValue0B(out); }
void cTSCASBodyPartSystem::ApplyNamesValue5(const StringVector *names) { Iface()->ApplyNamesValue5(names); }
void cTSCASBodyPartSystem::CollectNamesValue5(StringVector *out) { Iface()->CollectNamesValue5(out); }
void cTSCASBodyPartSystem::PushKind0(const char *name) { Iface()->PushKind0(name); }
void cTSCASBodyPartSystem::AddMorph(const char *name, float weight, uint8_t merge) { Iface()->AddMorph(name, weight, merge); }
void cTSCASBodyPartSystem::AddNamedWeight(const char *name, float weight) { Iface()->AddNamedWeight(name, weight); }
float cTSCASBodyPartSystem::SumNamedWeights(const char *name, uint32_t unused) { return Iface()->SumNamedWeights(name, unused); }
float cTSCASBodyPartSystem::TopMorphWeight(const char *name) { return Iface()->TopMorphWeight(name); }
void *cTSCASBodyPartSystem::GetPart(uint32_t a, uint32_t b, uint32_t id) { return Iface()->GetPart(a, b, id); }
void cTSCASBodyPartSystem::CollectLinkedParts(uint32_t a, uint32_t b, uint32_t id, ::ServiceRefVector *objects, IdVector *values,
                                              GuidVector *guids)
{
    Iface()->CollectLinkedParts(a, b, id, objects, values, guids);
}
void cTSCASBodyPartSystem::StoreBlockB8(const void *src) { Iface()->StoreBlockB8(src); }
void cTSCASBodyPartSystem::StoreBlockD8(const void *src) { Iface()->StoreBlockD8(src); }
void cTSCASBodyPartSystem::StoreKey(uint32_t index, const void *key) { Iface()->StoreKey(index, key); }
const void *cTSCASBodyPartSystem::GetKey(uint32_t index) { return Iface()->GetKey(index); }
void cTSCASBodyPartSystem::StoreDword108(uint32_t value) { Iface()->StoreDword108(value); }
void cTSCASBodyPartSystem::StoreByte10C(uint8_t value) { Iface()->StoreByte10C(value); }
void cTSCASBodyPartSystem::CheckRegionPairs(void *outfitA, void *outfitB, const ServiceStringVector *flags, ServiceStringVector *out)
{
    Iface()->CheckRegionPairs(outfitA, outfitB, flags, out);
}
void cTSCASBodyPartSystem::VisitIdsF7(uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4, uint32_t p5, uint32_t p6)
{
    Iface()->VisitIdsF7(p1, p2, p3, p4, p5, p6);
}
void cTSCASBodyPartSystem::VisitIdsEA(uint32_t p1, uint32_t p2, uint32_t p3, uint32_t p4, uint32_t p5, uint32_t p6)
{
    Iface()->VisitIdsEA(p1, p2, p3, p4, p5, p6);
}
void cTSCASBodyPartSystem::CollectRecordTexts(ServiceStringVector *out) { Iface()->CollectRecordTexts(out); }
void cTSCASBodyPartSystem::StoreByte129(uint8_t value) { Iface()->StoreByte129(value); }
uint32_t cTSCASBodyPartSystem::ByteGate120(int32_t mode) { return Iface()->ByteGate120(mode); }
void cTSCASBodyPartSystem::SetAge(uint32_t code) { Iface()->SetAge(code); }
uint32_t cTSCASBodyPartSystem::AgeMask() { return Iface()->AgeMask(); }
void cTSCASBodyPartSystem::StoreBlockC8(const void *src) { Iface()->StoreBlockC8(src); }
void cTSCASBodyPartSystem::ExportPointDeltas(void *source, void *target) { Iface()->ExportPointDeltas(source, target); }
void cTSCASBodyPartSystem::ExportPointPositions(void *source, void *target) { Iface()->ExportPointPositions(source, target); }
void cTSCASBodyPartSystem::ExportPositionsByUv(void *source, void *target, uint32_t mode) { Iface()->ExportPositionsByUv(source, target, mode); }
bool cTSCASBodyPartSystem::AverageWeightedOffset(uint32_t kind, uint32_t key, uint32_t id, uint32_t morph, float *out)
{
    return Iface()->AverageWeightedOffset(kind, key, id, morph, out);
}
uint8_t cTSCASBodyPartSystem::ApplyToChildren(uint32_t a, uint32_t b, void *target) { return Iface()->ApplyToChildren(a, b, target); }
bool cTSCASBodyPartSystem::ApplyBoneAdjusts(uint32_t kind, uint32_t key, uint32_t id, void *target)
{
    return Iface()->ApplyBoneAdjusts(kind, key, id, target);
}
bool cTSCASBodyPartSystem::GetNodeTransform(uint32_t a, uint32_t b, const char *name, void *position, void *rotation)
{
    return Iface()->GetNodeTransform(a, b, name, position, rotation);
}
bool cTSCASBodyPartSystem::ExportFaceObj(const char *path, uint32_t kind, uint32_t key) { return Iface()->ExportFaceObj(path, kind, key); }
void cTSCASBodyPartSystem::ReportVertexIds(const char *name, void *out) { Iface()->ReportVertexIds(name, out); }
void cTSCASBodyPartSystem::ReportGroupVertexIds(const char *name, uint32_t group, void *out) { Iface()->ReportGroupVertexIds(name, group, out); }
void cTSCASBodyPartSystem::ReportRegionMasks(const char *regions, void *out) { Iface()->ReportRegionMasks(regions, out); }
void cTSCASBodyPartSystem::ReportBones(void *out) { Iface()->ReportBones(out); }

} // namespace nTSCASCore
