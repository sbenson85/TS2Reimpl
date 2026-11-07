// nGZSceneGraph::cShapeRefNode (lanes SN, SP: scenegraph H2a / H2b). See the header.
#include "sg/DetailObject.h"
#include "sg/cShapeRefNode.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cVector4fEvent.h" // ts2_rva_db07d4
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "rz/StaticGetters2.h" // Dispatch8
#include "sg/SGEventRecords.h"
#include "sg/SGLocalString.h"
#include "misc/SmallVirtuals.h" // nGZEvent::cNullEvent::kVTable
#include "gz/cEventFaces.h"    // nGZEvent::NullEventVTable (the owned word)
#include "misc/UtilitiesE.h"    // GetCachedQueriedObject
#include "sg/SGTreeCode.h"      // NodeTransformChanged
#include "sg/cObjectGraphNodeImpl.h" // the subordinate notifications
#include "sg/cShape.h"
#include "compgeom/CompGeom.h"
#include "compgeom/Rigid.h" // RigidNearlyEqual (0xae79e0)
#include "sg/cTransformNode.h" // node_family
#include "sg/cShapeRefAttributeNameIterator.h"
#include "sg/cViewerRefNodeBaseImpl.h" // cViewerView

#include <algorithm>
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <map>
#include <cstring>
#include <new>
#include <optional>

namespace nGZSceneGraph {

static_assert(sizeof(cShapeRefNodeImpl) == 0xc4, "cShapeRefNodeImpl size");
static_assert(offsetof(cShapeRefNodeImpl, mShapes) == 0x70, "shapes");
static_assert(offsetof(cShapeRefNodeImpl, mDetailRecords) == 0x7c, "detail records");
static_assert(offsetof(cShapeRefNodeImpl, mHalf) == 0x88 && offsetof(cShapeRefNodeImpl, mCentre) == 0x94, "box");
static_assert(offsetof(cShapeRefNodeImpl, mShapeFlags) == 0xa0, "shape flags");
static_assert(offsetof(cShapeRefNodeImpl, mMorphNames) == 0xa4, "morph names");
static_assert(offsetof(cShapeRefNodeImpl, mMorphWeights) == 0xb0, "morph weights");
static_assert(offsetof(cShapeRefNodeImpl, mBlock) == 0xbc, "block");
static_assert(offsetof(cShapeRefNodeImpl, mColour) == 0xc0, "colour");
static_assert(sizeof(cShapeRefNode) == cShapeRefNode::kSize, "cShapeRefNode size");
static_assert(sizeof(cShapeDetailRecord) == 8, "detail record");

namespace {

// What a shape's slot 52 fills in (0x20 bytes; the shape returns the buffer). Names tentative: the first word is both
// the detail key 0xa59340 registers and the length of the material index list 0xa589a0 walks.
struct cShapeLodInfo {
    uint32_t mDetailKey;               // +0x00 (also the index count)
    const uint32_t *mMaterialIndices;  // +0x04: material manager indices
    uint32_t mWord8;                   // +0x08
    uint32_t mTransformCount;          // +0x0c: transform slots the block reserves
    uint32_t mWord10;                  // +0x10: slot 97's test
    uint32_t mRest[3];                 // +0x14
};
static_assert(sizeof(cShapeLodInfo) == 0x20, "cShapeLodInfo size");

// The detail object (0xaead80): slot 19 registers a detail key (the old handle and key given, the new
// handle returned), slot 20 drops a registration.
void *DetailObject() { return nGZSceneGraph::GetDetailObject(); }
uint32_t RegisterDetail(uint32_t handle, uint32_t oldKey, uint32_t key)
{
    return VirtualCall<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t), 19>(DetailObject(), handle, oldKey, key);
}
void UnregisterDetail(uint32_t handle, uint32_t key)
{
    VirtualCall<void(__thiscall *)(void *, uint32_t, uint32_t), 20>(DetailObject(), handle, key);
}

// ---- the material block (0xa589a0) ----

// A material manager lookup (its slot 33 fills 0x14 bytes and returns them; the material is the third word).
struct cMaterialLookup {
    uint32_t mWord0;
    uint32_t mWord4;
    void *mMaterial; // +0x08
    uint32_t mWordC;
    uint32_t mWord10;
};
static_assert(sizeof(cMaterialLookup) == 0x14, "cMaterialLookup size");

void *LookUpMaterial(void *manager, uint32_t index)
{
    cMaterialLookup lookup;
    return VirtualCall<cMaterialLookup *(__thiscall *)(void *, cMaterialLookup *, uint32_t), 33>(manager, &lookup, index)
        ->mMaterial;
}

// The material slots the block reads (names tentative): 6 whether it is to be skipped, 9 how many parameters of a type
// it has, 11 its parameter data size, 13 a parameter's description (its data offset at +0xc), 14 a parameter's value
// written into an event.
struct cParamInfo {
    uint32_t mWords[3];
    uint32_t mOffset; // +0x0c
};
bool MaterialSkipped(void *material) { return VirtualCall<bool(__thiscall *)(void *), 6>(material); }
int32_t ParamCount(void *material, uint32_t type) { return VirtualCall<int32_t(__thiscall *)(void *, uint32_t), 9>(material, type); }
uint32_t ParamDataSize(void *material) { return VirtualCall<uint32_t(__thiscall *)(void *), 11>(material); }
uint32_t ParamOffset(void *material, int32_t index, uint32_t type)
{
    cParamInfo info;
    return VirtualCall<cParamInfo *(__thiscall *)(void *, cParamInfo *, int32_t, uint32_t), 13>(material, &info, index, type)
        ->mOffset;
}
void GetParam(void *material, void *event, int32_t index)
{
    VirtualCall<void(__thiscall *)(void *, void *, int32_t), 14>(material, event, index);
}

// A shape's binding range (its slot 54 with no subset): three counted cursor objects (first, a middle one, end).
// Cursor slots: 2 Release, 4 at the given end, 5 the current binding, 6 next.
struct cBindingRange {
    void *mCursor;
    void *mMiddle;
    void *mEnd;
};
struct cBinding {
    uint32_t mWord0;
    uint32_t mMaterialIndex; // +0x04
    uint32_t mRest[2];       // (a cSubsetBinding, 0x10 bytes: sg/cSubsetBindingIterator.h)
};
bool CursorAtEnd(void *cursor, void *end) { return VirtualCall<bool(__thiscall *)(void *, void *), 4>(cursor, end); }
void CursorGet(void *cursor, cBinding *out) { VirtualCall<void(__thiscall *)(void *, cBinding *), 5>(cursor, out); }
void CursorNext(void *cursor) { VirtualCall<void(__thiscall *)(void *), 6>(cursor); }
void ReleaseCursor(void *cursor)
{
    if (cursor) VirtualCall<void(__thiscall *)(void *), 2>(cursor);
}

// The four parameter types the block lists (float, float2, float3, float4 by their events), and the events a value is
// read through: the game's cScalarEvent<float> 0xdb07f4 and cVectorEvent<2..4> 0xdb2bc8 / 0xdb5e70 / 0xdb07d4, each
// {vtable, data}; their slot 5 is the data size.
constexpr uint32_t kParamTypes[4] = {0xbb22dd17, 0x9b5b3300, 0x7b5b330c, 0x5b5b331a};
constexpr uint32_t kParamEventVTables[4] = {0xdb07f4, 0xdb2bc8, 0xdb5e70, 0xdb07d4};
struct cParamEvent {
    const void *mVTable;
    float mData[4];
};
uint32_t EventDataSize(cParamEvent *event) { return VirtualCall<uint32_t(__thiscall *)(void *), 5>(event); }

// The persist reader's slot 23: skips n bytes of the stream (the generated interface leaves the slot untyped).
void SkipBytes(ifaces::cIGZPersistReader *reader, uint32_t n)
{
    VirtualCall<void(__thiscall *)(void *, uint32_t), 23>(reader, n);
}

// The block: a 0x24-byte header, then (each present only when non-empty) the transforms (a count, then 0x1c-byte
// rigid transforms), the parameter data, the material table (a count, then a pointer into the data per material index,
// null for none), and the four parameter lists (a count, then data offsets). Zeroed when allocated.
struct cMaterialBlockHeader {
    uint32_t mSize;           // +0x00: the whole block's
    uint32_t *mTransforms;    // +0x04
    uint32_t mDataSize;       // +0x08
    uint8_t *mData;           // +0x0c
    uint32_t *mTable;         // +0x10
    uint32_t *mParamLists[4]; // +0x14..+0x20
};
static_assert(sizeof(cMaterialBlockHeader) == 0x24, "cMaterialBlockHeader size");
constexpr uint32_t kNoIndex = 0xffffffffu;

uint32_t ListSize(uint32_t count) { return count ? count * 4 + 4 : 0; }

void SendBlockChanged(cICompositionTreeNode *outer)
{
    const void *nullEvent = nGZEvent::NullEventVTable();
    outer->NotifyListeners(cShapeRefNodeImpl::kBlockChangedEvent, 0, &nullEvent);
}

} // namespace

// ---- cShapeHandle ----

cShapeHandle::cShapeHandle(cIShape *shape) : mShape(shape)
{
    if (mShape) mShape->AddRef();
}
cShapeHandle::cShapeHandle(const cShapeHandle &other) : cShapeHandle(other.mShape) {}
cShapeHandle &cShapeHandle::operator=(const cShapeHandle &other)
{
    cIShape *const old = mShape;
    if (other.mShape != old) {
        if (other.mShape) other.mShape->AddRef();
        mShape = other.mShape;
        if (old) old->Release();
    }
    return *this;
}
cShapeHandle::~cShapeHandle()
{
    if (mShape) mShape->Release();
}

// ---- cShapeRefNodeImpl ----

// 0xa544f0: the block freed first, then the members in reverse (the vectors at +0xb0, +0xa4, +0x7c; the shapes, each
// released front first), then the renderable Impl (the transform Impl's destructor).
cShapeRefNodeImpl::~cShapeRefNodeImpl()
{
    std::free(mBlock);
    mBlock = nullptr;
}

void cShapeRefNodeImpl::Destruct() { this->cShapeRefNodeImpl::~cShapeRefNodeImpl(); }

bool cShapeRefNodeImpl::IsKindOf(uint32_t iid) { return iid == kIid || cRenderableNodeImpl::IsKindOf(iid); }

void cShapeRefNodeImpl::OnEvent(cICompositionTreeNode *outer, uint32_t type, uint32_t arg1, void *data, uint32_t sender)
{
    cCompositionTreeNodeImpl::OnEvent(outer, type, arg1, data, sender);
    switch (type) {
    case kTransformEvent:
        NodeTransformChanged(outer);
        break;
    case kSubordinateEvent: {
        const auto *const body = static_cast<const cSubordinateListChanged *>(data);
        if (body->mIid != cIShape::kIid) break;
        if (body->mKind == cSubordinateListChanged::kAdded || body->mKind == cSubordinateListChanged::kRemoved)
            NodeTransformChanged(outer);
        RefreshDetailKeys(outer);
        RebuildMaterialBlock(outer);
        break;
    }
    case kMaterialsChangedEvent:
        RebuildMaterialBlock(outer);
        RefreshDetailLimit();
        break;
    case kDetailChangedEvent:
        RefreshDetailKeys(outer);
        break;
    default:
        break;
    }
}

bool cShapeRefNodeImpl::OnQuery(cICompositionTreeNode *outer, uint32_t type, uint32_t, void *)
{
    if (type != kRefreshQuery) return false;
    RefreshDetailKeys(outer);
    RebuildMaterialBlock(outer);
    NodeTransformChanged(outer);
    return true;
}

void cShapeRefNodeImpl::AddShape(cICompositionTreeNode *outer, cIShape *shape)
{
    NotifySubordinateAdding(outer, cIShape::kIid);
    {
        const cShapeHandle held(shape);
        mShapes.push_back(held);
    }
    mDetailRecords.push_back({0, 0});
    shape->AddReferrer(outer);
    NotifySubordinateAdded(outer, cIShape::kIid, shape);
}

void cShapeRefNodeImpl::RemoveShape(cICompositionTreeNode *outer, cIShape *shape)
{
    NotifySubordinateRemoving(outer, cIShape::kIid, shape);
    shape->RemoveReferrer(outer);
    const auto found =
        std::find_if(mShapes.begin(), mShapes.end(), [shape](const cShapeHandle &held) { return held.Get() == shape; });
    const size_t index = static_cast<size_t>(found - mShapes.begin());
    const cShapeDetailRecord record = mDetailRecords[index];
    if (record.mKey) UnregisterDetail(record.mHandle, record.mKey);
    mDetailRecords.erase(mDetailRecords.begin() + index);
    mShapes.erase(found);
    NotifySubordinateRemoved(outer, cIShape::kIid);
}

bool cShapeRefNodeImpl::AnyShapeSlot52()
{
    for (const cShapeHandle &held : mShapes) {
        cShapeLodInfo info;
        if (reinterpret_cast<const cShapeLodInfo *>(held.Get()->GetRenderInfo(&info))->mWord10) return true;
    }
    return false;
}

void cShapeRefNodeImpl::RefreshDetailKeys(cICompositionTreeNode *)
{
    cShapeDetailRecord *record = mDetailRecords.data();
    for (const cShapeHandle &held : mShapes) {
        cShapeLodInfo info;
        const uint32_t key = reinterpret_cast<const cShapeLodInfo *>(held.Get()->GetRenderInfo(&info))->mDetailKey;
        if (record->mKey != key) {
            record->mHandle = RegisterDetail(record->mHandle, record->mKey, key);
            record->mKey = key;
        }
        ++record;
    }
}

void cShapeRefNodeImpl::RebuildMaterialBlock(cICompositionTreeNode *outer)
{
    void *const manager = GetCachedQueriedObject();

    // The transform slots: the largest any shape asks for.
    uint32_t transformCount = 0;
    for (const cShapeHandle &held : mShapes) {
        cShapeLodInfo info;
        held.Get()->GetRenderInfo(&info);
        transformCount = std::max(transformCount, info.mTransformCount);
    }
    RebuildMorphs();

    // The distinct materials of the shapes' bindings, in order, each with its data offset; the parameter counts.
    // (The original keeps these three as function-local statics, cleared on each call, as here.)
    static std::vector<void *> sMaterials;
    static std::vector<uint32_t> sDataOffsets;
    static std::vector<uint32_t> sTableOffsets;
    uint32_t dataSize = 0;
    uint32_t paramCounts[4] = {};
    sMaterials.clear();
    sDataOffsets.clear();
    for (const cShapeHandle &held : mShapes) {
        cBindingRange range;
        held.Get()->GetSubsetBindings(&range, nullptr);
        while (!CursorAtEnd(range.mCursor, range.mEnd)) {
            cBinding binding;
            CursorGet(range.mCursor, &binding);
            void *const material = LookUpMaterial(manager, binding.mMaterialIndex);
            if (material && !MaterialSkipped(material) &&
                std::find(sMaterials.begin(), sMaterials.end(), material) == sMaterials.end()) {
                sMaterials.push_back(material);
                sDataOffsets.push_back(dataSize);
                dataSize += ParamDataSize(material);
                for (int type = 0; type < 4; ++type) paramCounts[type] += ParamCount(material, kParamTypes[type]);
            }
            CursorNext(range.mCursor);
        }
        ReleaseCursor(range.mEnd);
        ReleaseCursor(range.mMiddle);
        ReleaseCursor(range.mCursor);
    }

    // Per material index of every shape (in order), the data offset of its material, kNoIndex for none; the list
    // only grows as far as the last index with a material.
    sTableOffsets.clear();
    uint32_t indexCount = 0;
    uint32_t index = 0;
    for (const cShapeHandle &held : mShapes) {
        cShapeLodInfo info;
        held.Get()->GetRenderInfo(&info);
        if (info.mDetailKey == 0 || info.mMaterialIndices == nullptr) continue;
        indexCount += info.mDetailKey;
        for (uint32_t i = 0; i < info.mDetailKey; ++i, ++index) {
            void *const material = LookUpMaterial(manager, info.mMaterialIndices[i]);
            if (material == nullptr || MaterialSkipped(material)) continue;
            sTableOffsets.resize(indexCount, kNoIndex);
            const size_t which = static_cast<size_t>(std::find(sMaterials.begin(), sMaterials.end(), material) -
                                                     sMaterials.begin());
            sTableOffsets[index] = sDataOffsets[which];
        }
    }

    if (transformCount == 0 && sMaterials.empty()) {
        if (mBlock == nullptr) return;
        std::free(mBlock);
        mBlock = nullptr;
        SendBlockChanged(outer);
        return;
    }

    const uint32_t transformBytes = transformCount * sizeof(cRigidTransform);
    uint32_t size = transformCount ? transformBytes + 4 + sizeof(cMaterialBlockHeader) : sizeof(cMaterialBlockHeader);
    size += dataSize ? indexCount * 4 + 4 : 0;
    size += dataSize;
    for (uint32_t count : paramCounts) size += ListSize(count);

    std::free(mBlock);
    mBlock = nullptr;
    mBlock = std::malloc(size);
    std::memset(mBlock, 0, size);
    auto *const header = static_cast<cMaterialBlockHeader *>(mBlock);
    header->mSize = size;
    uint8_t *cursor = static_cast<uint8_t *>(mBlock) + sizeof(cMaterialBlockHeader);

    if (transformCount) {
        header->mTransforms = reinterpret_cast<uint32_t *>(cursor);
        *header->mTransforms = transformCount;
        cursor += 4 + transformBytes;
        auto *const transforms = reinterpret_cast<cRigidTransform *>(header->mTransforms + 1);
        cICompositionTreeNode *const root = FindLimitRoot(outer);
        if (root) {
            cRigidTransform world = root->GetWorldTransform();
            std::fill(transforms, transforms + transformCount, world);
            GatherLimitTransforms(transforms, transformCount, root, &world);
        } else {
            const cRigidTransform identity{{0, 0, 0, 0, 0, 0, 1.0f}};
            std::fill(transforms, transforms + transformCount, identity);
        }
    }

    if (dataSize) {
        header->mData = cursor;
        header->mDataSize = dataSize;
        cursor += dataSize;
        header->mTable = reinterpret_cast<uint32_t *>(cursor);
        *header->mTable = indexCount;
        uint32_t *entry = header->mTable + 1;
        for (uint32_t offset : sTableOffsets)
            *entry++ = offset == kNoIndex ? 0 : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(header->mData + offset));
        cursor = reinterpret_cast<uint8_t *>(header->mTable + 1 + indexCount);

        uint32_t *lists[4] = {};
        for (int type = 0; type < 4; ++type) {
            if (paramCounts[type] == 0) continue;
            header->mParamLists[type] = reinterpret_cast<uint32_t *>(cursor);
            *header->mParamLists[type] = paramCounts[type];
            cursor += paramCounts[type] * 4 + 4;
            lists[type] = header->mParamLists[type] + 1;
        }

        cParamEvent events[4];
        for (int type = 0; type < 4; ++type) {
            events[type].mVTable = type == 3 ? static_cast<const void *>(&ts2_rva_db07d4) // owned (gz/cVector4fEvent.h)
                                             : GameAddress<const void>(kParamEventVTables[type]);
            // The vector events start zeroed; the float event's value is left as it is.
            if (type) std::fill(events[type].mData, events[type].mData + type + 1, 0.0f);
        }
        uint32_t materialOffset = 0;
        for (void *material : sMaterials) {
            for (int type = 0; type < 4; ++type) {
                for (int32_t i = 0; i < ParamCount(material, kParamTypes[type]); ++i) {
                    const uint32_t offset = ParamOffset(material, i, kParamTypes[type]) + materialOffset;
                    GetParam(material, &events[type], i);
                    std::memcpy(header->mData + offset, events[type].mData, EventDataSize(&events[type]));
                    *lists[type]++ = offset;
                }
            }
            materialOffset += ParamDataSize(material);
        }
    }

    RefreshDetailLimit();
    SendBlockChanged(outer);
}

// ---- persistence (lane TB, H2c) ----

// 0xa57e60 (cdecl, returns out): "a::b" interned in a pool cell (Open's flag 1).
OwnedCell *MakeMorphName(OwnedCell *out, const char *shapeName, const char *morphName)
{
    nTSSG::SGLocalString text;
    text.value.Sprintf("%s::%s", shapeName, morphName);
    out->cell.Open(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(text.value.ToChar())), 1);
    return out;
}

// 0xa54930 (cdecl): true when there are no weights (null or none) or every one is under 0.001 in magnitude.
bool AllNearZero(const float *weights, uint32_t count)
{
    if (weights && count) {
        for (uint32_t i = 0; i < count; ++i) {
            if (std::fabs(weights[i]) >= 0.001f) return false;
        }
    }
    return true;
}

namespace {

// What a shape's slot 58 fills in for a morph: its two name parts (the shape's, the morph's).
struct cMorphNameParts {
    const char *mShape;
    const char *mMorph;
};

cShapeLodInfo *LodInfo(cIShape *shape, cShapeLodInfo *buffer)
{
    return reinterpret_cast<cShapeLodInfo *>(static_cast<uintptr_t>(shape->GetRenderInfo(buffer)));
}

} // namespace

void cShapeRefNodeImpl::RebuildMorphs()
{
    // The weights by name (a name listed twice keeps its last weight).
    std::map<OwnedCell, float, CellLess> old;
    const int32_t names = static_cast<int32_t>(mMorphNames.size());
    for (int32_t i = 0; i < names; ++i) old[mMorphNames[i]] = mMorphWeights[i];

    mMorphNames.clear();
    mMorphWeights.clear();
    mMorphCount = 0;
    for (const cShapeHandle &held : mShapes) {
        cShapeLodInfo info;
        held.Get()->GetRenderInfo(&info);
        mMorphCount = static_cast<uint8_t>(mMorphCount + info.mWord8);
        for (uint32_t i = 0; i < info.mWord8; ++i) {
            cMorphNameParts parts;
            held.Get()->QueryPair(&parts, i);
            OwnedCell name;
            MakeMorphName(&name, parts.mShape, parts.mMorph);
            float weight = 0.0f;
            if (old.find(name) != old.end()) {
                weight = old[name];
                old.erase(name);
            }
            mMorphNames.push_back(name);
            mMorphWeights.push_back(weight);
        }
    }
    // The names no shape has now, in handle order.
    for (const auto &entry : old) {
        mMorphNames.push_back(entry.first);
        mMorphWeights.push_back(entry.second);
    }
}

void cShapeRefNodeImpl::Read(cICompositionTreeNode *outer, ifaces::cIGZPersistReader *reader)
{
    cPersistTypeInfo info;
    reader->ReadTypeInfo(&info);
    cRenderableNodeImpl::Read(static_cast<cIBoundedNode *>(outer), reader);

    for (uint32_t n = reader->GetUint32(); n != 0; --n) {
        cIShape *shape = nullptr;
        reader->GetObjectRef(cIShape::kIid, &shape);
        if (shape) static_cast<cIShapeRefNode *>(outer)->AddShape(shape);
        if (shape) shape->Release();
    }
    mShapeFlags = static_cast<uint16_t>(reader->GetUint32());
    const uint32_t weightCount = reader->GetUint32();

    // The names from the shapes as they are now (no weights kept).
    mMorphNames.clear();
    mMorphWeights.clear();
    mMorphCount = 0;
    for (const cShapeHandle &held : mShapes) {
        cShapeLodInfo buffer;
        const int32_t count = static_cast<int32_t>(LodInfo(held.Get(), &buffer)->mWord8);
        mMorphCount = static_cast<uint8_t>(mMorphCount + count);
        for (int32_t i = 0; i < count; ++i) {
            cMorphNameParts parts;
            held.Get()->QueryPair(&parts, static_cast<uint32_t>(i));
            OwnedCell name;
            mMorphNames.push_back(*MakeMorphName(&name, parts.mShape, parts.mMorph));
        }
    }
    mMorphWeights.resize(mMorphCount, 0.0f);

    if (weightCount) {
        // The weights only when the shapes have that many morphs, else skipped.
        if (mMorphWeights.size() == weightCount) {
            reader->GetFloat32Array(mMorphWeights.data(), weightCount);
        } else {
            SkipBytes(reader, weightCount * 4);
        }
        if (info.mVersion >= 0x15) {
            // A name per weight, read and dropped.
            for (uint32_t i = 0; i < weightCount; ++i) {
                nTSSG::SGLocalString name;
                reader->GetGZStr(&name.value);
            }
        }
    }

    // The block's data, read in place when the block has data of that size.
    const uint32_t dataSize = reader->GetUint32();
    if (dataSize) {
        auto *const block = static_cast<cMaterialBlockHeader *>(mBlock);
        if (block && block->mData && block->mDataSize == dataSize) {
            reader->GetVoid(block->mData, dataSize);
        } else {
            SkipBytes(reader, dataSize);
        }
    }

    if (mMorphCount == 0 || AllNearZero(mMorphWeights.data(), mMorphCount)) {
        mShapeFlags &= static_cast<uint16_t>(~kWeighted);
    } else {
        mShapeFlags |= kWeighted;
    }
    if (info.mVersion > 0x13) mColour = reader->GetUint32();
    if (info.mName) info.mName->Release();
}

cPersistTypeInfo *cShapeRefNodeImpl::GetTypeInfo(cPersistTypeInfo *out)
{
    out->mName = NewTypeName("cShapeRefNode");
    out->mWord4 = kTypeWord;
    out->mVersion = kVersion;
    return out;
}

void cShapeRefNodeImpl::Write(cICompositionTreeNode *outer, ifaces::cIGZPersistWriter *writer)
{
    cPersistTypeInfo info;
    writer->WriteTypeInfo(GetTypeInfo(&info));
    if (info.mName) info.mName->Release();
    cRenderableNodeImpl::Write(static_cast<cIBoundedNode *>(outer), writer);

    writer->SetUint32(static_cast<uint32_t>(mShapes.size()));
    for (const cShapeHandle &held : mShapes) writer->WriteObjectRef(held.Get());
    writer->SetUint32(mShapeFlags);
    if (!mMorphWeights.empty()) {
        writer->SetUint32(mMorphCount);
        writer->SetFloat32Array(mMorphWeights.data(), mMorphCount);
    } else {
        writer->SetUint32(0);
    }
    for (uint32_t i = 0; i < mMorphCount; ++i) {
        writer->WriteRZCharStr(reinterpret_cast<char *>(static_cast<uintptr_t>(Dispatch8(&mMorphNames[i].cell))));
    }
    auto *const block = static_cast<cMaterialBlockHeader *>(mBlock);
    if (block && block->mDataSize) {
        writer->SetUint32(block->mDataSize);
        writer->SetVoid(block->mData, block->mDataSize);
    } else {
        writer->SetUint32(0);
    }
    writer->SetUint32(mColour);
}

void cShapeRefNodeImpl::CloneInto(cICompositionTreeNode *source, cICompositionTreeNode *clone, cShapeRefNodeImpl *cloneImpl,
                                  uint32_t mode, cINodeCloneContext *context)
{
    cRenderableNodeImpl::CloneInto(source, clone, cloneImpl, mode, context);
    cloneImpl->mShapeFlags = mShapeFlags;
    for (const cShapeHandle &held : mShapes) static_cast<cIShapeRefNode *>(clone)->AddShape(held.Get());
    if (mBlock) {
        if (cloneImpl != this) {
            cloneImpl->mMorphNames.assign(mMorphNames.begin(), mMorphNames.end());
            cloneImpl->mMorphWeights.assign(mMorphWeights.begin(), mMorphWeights.end());
        }
        cloneImpl->mMorphCount = mMorphCount;
        const auto *const block = static_cast<const cMaterialBlockHeader *>(mBlock);
        if (block->mDataSize) {
            // ORIGINAL-BUG: the clone's block is not checked (AddShape rebuilt it, so it is there when a shape is).
            std::memmove(static_cast<cMaterialBlockHeader *>(cloneImpl->mBlock)->mData, block->mData, block->mDataSize);
        }
    }
    cloneImpl->mColour = mColour;
}

// ---- lane TB: bounds and hits (H2d) ----

namespace {

// 0x19cd80 (thiscall, returns this): a rigid transform inverted in place; ours.
cRigidTransform *RigidInvert(cRigidTransform *rigid)
{
    return reinterpret_cast<cRigidTransform *>(reinterpret_cast<nGZCompGeom::RigidTransform3 *>(rigid)->Invert()); // ours
}
const nGZCompGeom::RigidTransform3 &AsRigid(const cRigidTransform &rigid)
{
    return *reinterpret_cast<const nGZCompGeom::RigidTransform3 *>(&rigid);
}
const nGZCompGeom::Cuboid *AsCuboid(const float *half) { return reinterpret_cast<const nGZCompGeom::Cuboid *>(half); }
cBoundBox *Store(cBoundBox *out, const nGZCompGeom::BoundBox &box)
{
    *reinterpret_cast<nGZCompGeom::BoundBox *>(out) = box;
    return out;
}
constexpr uint32_t kRecordWords = 8; // OnContentChanged's records: the block transform's index, then the transform

} // namespace

void cShapeRefNodeImpl::BoxFrame(AffineTransform3 *frame)
{
    nGZCompGeom::Vec3 centre{mCentre[0], mCentre[1], mCentre[2]};
    AsRigid(mWorld).TransformPoint(&centre);
    frame->mUnknown0 = 1;
    frame->mOffset[0] = centre.x;
    frame->mOffset[1] = centre.y;
    frame->mOffset[2] = centre.z;
    game_code::SetFrameRotation(frame, mWorld.Rotation());
}

template <class Visit> bool cShapeRefNodeImpl::VisitParts(cICompositionTreeNode *outer, Visit &&visit)
{
    for (const cShapeHandle &held : mShapes) {
        cShapeLodInfo info;
        held.Get()->GetRenderInfo(&info);
        nGZCompGeom::cIBound *const *parts = reinterpret_cast<nGZCompGeom::cIBound *const *>(uintptr_t(info.mRest[2]));
        if (!parts) continue;
        const auto *block = static_cast<const cMaterialBlockHeader *>(mBlock);
        if (block && block->mTransforms) {
            const uint32_t n = std::min(info.mTransformCount, block->mTransforms[0]);
            const auto *placed = reinterpret_cast<const cRigidTransform *>(block->mTransforms + 1);
            for (uint32_t i = 0; i < n; ++i) {
                if (!parts[i]) continue;
                AffineTransform3 frame;
                frame.mUnknown0 = 1;
                std::memcpy(frame.mOffset, placed[i].Translation(), sizeof frame.mOffset);
                game_code::SetFrameRotation(&frame, placed[i].Rotation());
                if (visit(parts[i], &frame)) return true;
            }
        } else if (parts[0]) {
            RefreshIfStale(outer);
            AffineTransform3 frame;
            CachedFrame(&frame);
            if (visit(parts[0], &frame)) return true;
        }
    }
    return false;
}

void cShapeRefNodeImpl::OnContentChanged(const cRigidTransform *transform, const uint32_t *records, uint32_t count)
{
    cRenderableNodeImpl::SetWorldTransform(transform, uint32_t(uintptr_t(records)), count);
    mShapeFlags |= 0x10;
    mByteA2 = 10;
    uint32_t placedCount = 0;
    cRigidTransform *placed = nullptr;
    auto *block = static_cast<cMaterialBlockHeader *>(mBlock);
    if (block && block->mTransforms) {
        placedCount = block->mTransforms[0];
        placed = reinterpret_cast<cRigidTransform *>(block->mTransforms + 1);
        if (count != 0) {
            for (uint32_t i = 0; i < placedCount; ++i) std::memcpy(&placed[i], records + 1, sizeof(cRigidTransform));
            for (uint32_t r = 0; r < count; ++r) {
                const uint32_t *record = records + r * kRecordWords;
                if (record[0] < placedCount) std::memcpy(&placed[record[0]], record + 1, sizeof(cRigidTransform));
            }
        }
    }
    const float *t = transform->Translation();
    if (mShapes.empty()) {
        mHalf[0] = mHalf[1] = mHalf[2] = 0.0f;
        std::memcpy(mCentre, t, sizeof mCentre);
        return;
    }
    nGZCompGeom::BoundBox box;
    box.min = {FLT_MAX, FLT_MAX, FLT_MAX};
    box.max = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    if (!placed) {
        for (const cShapeHandle &held : mShapes) held.Get()->GrowBoxByParts(uint32_t(uintptr_t(&box)));
    } else {
        cRigidTransform inverse = *transform;
        inverse = *RigidInvert(&inverse);
        for (const cShapeHandle &held : mShapes)
            held.Get()->GrowBoxByPartsMoved(uint32_t(uintptr_t(&box)), uint32_t(uintptr_t(&inverse)), placedCount,
                               uint32_t(uintptr_t(placed)));
    }
    if (box.min.x <= box.max.x && box.min.y <= box.max.y && box.min.z <= box.max.z) {
        mHalf[0] = (box.max.x - box.min.x) * 0.5f;
        mHalf[1] = (box.max.y - box.min.y) * 0.5f;
        mHalf[2] = (box.max.z - box.min.z) * 0.5f;
        mCentre[0] = mHalf[0] + box.min.x;
        mCentre[1] = mHalf[1] + box.min.y;
        mCentre[2] = mHalf[2] + box.min.z;
    } else {
        mHalf[0] = mHalf[1] = mHalf[2] = 0.0f;
        std::memcpy(mCentre, t, sizeof mCentre);
    }
}

cBoundBox *cShapeRefNodeImpl::GetBoundBoxIn(cBoundBox *out, cICompositionTreeNode *outer, const cTransform34 *frame)
{
    RefreshIfStale(outer);
    AffineTransform3 mine;
    CachedFrame(&mine);
    AffineTransform3 composed = *game_code::ComposeFrames(&mine, reinterpret_cast<const AffineTransform3 *>(frame));
    const nGZCompGeom::Vec3 centre =
        nGZCompGeom::FramePoint(composed, nGZCompGeom::Vec3{mCentre[0], mCentre[1], mCentre[2]});
    composed.mUnknown0 |= 1;
    composed.mOffset[0] = centre.x;
    composed.mOffset[1] = centre.y;
    composed.mOffset[2] = centre.z;
    return Store(out, AsCuboid(mHalf)->BoxInFrame(composed));
}

cBoundBox *cShapeRefNodeImpl::GetBoundBox(cBoundBox *out, cICompositionTreeNode *outer)
{
    RefreshIfStale(outer);
    nGZCompGeom::RigidTransform3 placed = AsRigid(mWorld);
    placed.translation = {mCentre[0], mCentre[1], mCentre[2]};
    AsRigid(mWorld).TransformPoint(&placed.translation);
    return Store(out, AsCuboid(mHalf)->BoxInRigid(placed));
}

bool cShapeRefNodeImpl::IntersectsBound(cICompositionTreeNode *outer, nGZCompGeom::cIBound *bound,
                                        const AffineTransform3 *frame)
{
    RefreshIfStale(outer);
    AffineTransform3 box;
    BoxFrame(&box);
    if (!bound->IntersectsCuboidInAt(frame, AsCuboid(mHalf), &box)) return false;
    return VisitParts(outer, [&](nGZCompGeom::cIBound *part, const AffineTransform3 *partFrame) {
        return part->IntersectsBoundInAt(partFrame, bound, frame);
    });
}

bool cShapeRefNodeImpl::IntersectsLinear(cICompositionTreeNode *outer, nGZCompGeom::cILinear3 *linear)
{
    RefreshIfStale(outer);
    AffineTransform3 box;
    BoxFrame(&box);
    if (!linear->IntersectsCuboidIn(AsCuboid(mHalf), &box)) return false;
    return VisitParts(outer, [&](nGZCompGeom::cIBound *part, const AffineTransform3 *partFrame) {
        return linear->IntersectsBoundIn(part, partFrame);
    });
}

bool cShapeRefNodeImpl::FirstHitLinear(cICompositionTreeNode *outer, float *t, nGZCompGeom::cILinear3 *linear)
{
    bool found = false;
    VisitParts(outer, [&](nGZCompGeom::cIBound *part, const AffineTransform3 *partFrame) {
        float hit;
        if (linear->FirstHitBoundIn(&hit, part, partFrame) && *t > hit) {
            *t = hit;
            found = true;
        }
        return false;
    });
    return found;
}

// ---- lane TD: attributes, attribute names, drawing (H2e) ----

namespace {

// The block's parameter values (each a list of offsets into the block's data, by index) and the node's own attributes.
const float kChangeEpsilon = std::bit_cast<float>(0x2f03126fu); // a parameter "changes" past this (about 1.2e-10)
constexpr float kWeightEpsilon = 0.001f;
constexpr float kColourScale = 255.0f;
constexpr float kColourBias = 49152.0f; // bits 0x47400000: v + bias holds v x 256 in the low mantissa bits
constexpr int32_t kColourBiasBits = 0x47400000;

// The attributes the Impl answers itself (the rest are the transform Impl's).
constexpr uint32_t kFloatAttribute = 0x09f5fdb0;  // float parameters (the block's list 0)
constexpr uint32_t kFloat2Attribute = 0x09f5fdb6; // float2 (list 1)
constexpr uint32_t kFloat3Attribute = 0x09f5fdbc; // float3 (list 2)
constexpr uint32_t kFloat4Attribute = 0x09f5fdc0; // float4 (list 3)
constexpr uint32_t kMorphWeightAttribute = 0x69c7eb40;
constexpr uint32_t kColourAttribute = 0x8c798726;

constexpr uint8_t kAllPlanes = 0x3f; // a plane mask inside all six planes: no frustum test
constexpr int kDrawSlot = 25;        // the viewer's drawer: draw a node placed by a frame, in a box, with a plane mask

int ParamList(uint32_t id)
{
    switch (id) {
    case kFloatAttribute: return 0;
    case kFloat2Attribute: return 1;
    case kFloat3Attribute: return 2;
    case kFloat4Attribute: return 3;
    default: return -1;
    }
}

float *ParamData(void *block, int list, uint32_t index)
{
    auto *const header = static_cast<cMaterialBlockHeader *>(block);
    return reinterpret_cast<float *>(header->mData + header->mParamLists[list][1 + index]);
}

// A colour channel as a byte: the bits of v + 49152 less those of 49152 (v x 256, rounded), clamped to 0..255.
uint8_t ColourByte(float v)
{
    const int32_t i = std::bit_cast<int32_t>(v + kColourBias) - kColourBiasBits;
    return uint8_t((~(i >> 31) & i & 0xff) | (((0xff - i) >> 31) & 0xff));
}
float ChannelValue(uint8_t byte) { return float(int32_t(byte)) / kColourScale; }

// A material's name (slot 5) and its parameter info's first word (slot 13), the name list's two words.
const char *MaterialName(void *material) { return VirtualCall<const char *(__thiscall *)(void *), 5>(material); }
uint32_t ParamNameWord(void *material, int32_t index, uint32_t type)
{
    cParamInfo info;
    return VirtualCall<cParamInfo *(__thiscall *)(void *, cParamInfo *, int32_t, uint32_t), 13>(material, &info, index, type)
        ->mWords[0];
}

// A shape's slot 58: the morph's {shape name, morph name}.
cArrayAttributeEntryName MorphPair(cIShape *shape, uint32_t index)
{
    cArrayAttributeEntryName pair;
    shape->QueryPair(&pair, index);
    return pair;
}

// The game's colour names (its "shapeRefGroup" / "shapeColor").
constexpr char kColourGroup[] = "shapeRefGroup";
constexpr char kColourName[] = "shapeColor";

} // namespace

void cShapeRefNodeImpl::GetAttribute(cAttributeValue *out, uint32_t id, uint32_t extra)
{
    const int list = ParamList(id);
    if (list >= 0) {
        std::memcpy(out->mValues, ParamData(mBlock, list, extra), (list + 1) * sizeof(float));
        return;
    }
    if (id == kMorphWeightAttribute) {
        out->mValues[0] = mMorphWeights.data()[extra];
        return;
    }
    if (id == kColourAttribute) {
        out->mValues[0] = ChannelValue(uint8_t(mColour >> 24));
        out->mValues[1] = ChannelValue(uint8_t(mColour >> 16));
        out->mValues[2] = ChannelValue(uint8_t(mColour >> 8));
        out->mValues[3] = ChannelValue(uint8_t(mColour));
        return;
    }
    cTransformNodeImpl::GetAttribute(out, id, extra);
}

void cShapeRefNodeImpl::SetAttribute(cICompositionTreeNode *outer, uint32_t id, uint32_t extra, const cAttributeValue *value)
{
    const int list = ParamList(id);
    if (list >= 0) {
        float *const data = ParamData(mBlock, list, extra);
        // A component that moved (or is NaN on either side) rereads the detail limit, once.
        for (int i = 0; i <= list; ++i) {
            if (!(kChangeEpsilon >= std::fabs(data[i] - value->mValues[i]))) {
                RefreshDetailLimit();
                break;
            }
        }
        std::memcpy(data, value->mValues, (list + 1) * sizeof(float));
        return;
    }
    if (id == kMorphWeightAttribute) {
        RefreshDetailLimit();
        float *const weights = mMorphWeights.data();
        const float weight = value->mValues[0];
        weights[extra] = weight;
        if (!(std::fabs(weight) > kWeightEpsilon) && AllNearZero(weights, mMorphCount))
            mShapeFlags &= uint16_t(~kWeighted);
        else
            mShapeFlags |= kWeighted;
        return;
    }
    if (id == kColourAttribute) {
        if (extra != 0) return;
        const uint8_t a = ColourByte(value->mValues[0]), r = ColourByte(value->mValues[1]),
                      g = ColourByte(value->mValues[2]), b = ColourByte(value->mValues[3]);
        const uint32_t packed = uint32_t(a) << 24 | uint32_t(r) << 16 | uint32_t(g) << 8 | b;
        if (!nGZCompGeom::NearlyEqual(ChannelValue(a), ChannelValue(uint8_t(mColour >> 24))) ||
            !nGZCompGeom::NearlyEqual(ChannelValue(r), ChannelValue(uint8_t(mColour >> 16))) ||
            !nGZCompGeom::NearlyEqual(ChannelValue(g), ChannelValue(uint8_t(mColour >> 8))) ||
            !nGZCompGeom::NearlyEqual(ChannelValue(b), ChannelValue(uint8_t(mColour))))
            RefreshDetailLimit();
        mColour = packed;
        return;
    }
    cTransformNodeImpl::SetAttribute(outer, id, extra, value);
}

void *cShapeRefNodeImpl::MakeIterator51(void *outRange, uint32_t id)
{
    auto *const out = static_cast<cArrayAttributeEntryRange *>(outRange);
    const int list = ParamList(id);
    if (list < 0 && id != kMorphWeightAttribute && id != kColourAttribute)
        return MakeEmptyAttributeEntryRange(this, nullptr, out, id);

    auto *const names = new cShapeRefAttributeNameList;
    names->AddRef();
    if (list >= 0) {
        // Each distinct material of the shapes' bindings with parameters of the type: {its name, each parameter's}.
        const uint32_t type = kParamTypes[list];
        void *const manager = GetCachedQueriedObject();
        // The materials seen: a function-local static like the original's, cleared on each call (its storage kept).
        static std::vector<void *> seen;
        seen.clear();
        for (const cShapeHandle &held : mShapes) {
            cBindingRange range;
            held.Get()->GetSubsetBindings(&range, nullptr);
            while (!CursorAtEnd(range.mCursor, range.mEnd)) {
                cBinding binding;
                CursorGet(range.mCursor, &binding);
                void *const material = LookUpMaterial(manager, binding.mMaterialIndex);
                if (material && ParamCount(material, type) != 0 &&
                    std::find(seen.begin(), seen.end(), material) == seen.end()) {
                    seen.push_back(material);
                    for (int32_t i = 0; i < ParamCount(material, type); ++i) {
                        const char *const name = MaterialName(material);
                        names->mNames.push_back({uint32_t(uintptr_t(name)), ParamNameWord(material, i, type)});
                    }
                }
                CursorNext(range.mCursor);
            }
            ReleaseCursor(range.mEnd);
            ReleaseCursor(range.mMiddle);
            ReleaseCursor(range.mCursor);
        }
    } else if (id == kMorphWeightAttribute) {
        for (const cShapeHandle &held : mShapes) {
            cShapeLodInfo info;
            held.Get()->GetRenderInfo(&info);
            for (uint32_t i = 0; i < info.mWord8; ++i) names->mNames.push_back(MorphPair(held.Get(), i));
        }
    } else {
        names->mNames.push_back({uint32_t(uintptr_t(kColourGroup)), uint32_t(uintptr_t(kColourName))});
    }
    MakeAttributeNameRange(out, names);
    names->Release();
    return out;
}

void cShapeRefNodeImpl::Draw(cICompositionTreeNode *outer, cViewerView *viewer, uint32_t planeMask)
{
    if (mShapes.empty()) return;
    RefreshIfStale(outer);
    AffineTransform3 mine;
    CachedFrame(&mine);
    const AffineTransform3 placed =
        *game_code::ComposeFrames(&mine, reinterpret_cast<const AffineTransform3 *>(&viewer->mFrame));
    const nGZCompGeom::Vec3 centre{mCentre[0], mCentre[1], mCentre[2]};
    uint8_t mask = uint8_t(planeMask);
    if (!(mShapeFlags & kFlag70) && mask != kAllPlanes) {
        AffineTransform3 box = placed;
        const nGZCompGeom::Vec3 at = nGZCompGeom::FramePoint(placed, centre);
        box.mUnknown0 |= 1;
        box.mOffset[0] = at.x;
        box.mOffset[1] = at.y;
        box.mOffset[2] = at.z;
        if (viewer->mFrustum.ClassifyBoxInFrame(&mask, reinterpret_cast<const nGZCompGeom::Vec3 *>(mHalf), &box) > 0)
            return;
    }
    // The classifier updates the mask's low byte (the planes the box is wholly inside); the rest of the word is kept.
    planeMask = (planeMask & ~0xffu) | mask;
    const nGZCompGeom::BoundBox bounds = AsCuboid(mHalf)->BoxAt(centre);
    VirtualCall<void(__thiscall *)(void *, cICompositionTreeNode *, const AffineTransform3 *, const nGZCompGeom::BoundBox *,
                                   uint32_t),
                kDrawSlot>(viewer->mDrawer, outer, &placed, &bounds, planeMask);
}

void cShapeRefNodeImpl::QueueShapeDraws(cICompositionTreeNode *, void *sink, void *)
{
    const float *weights = mMorphWeights.empty() ? nullptr : mMorphWeights.data();
    const uint32_t *table = nullptr;
    const uint32_t *transforms = nullptr;
    if (auto *const block = static_cast<cMaterialBlockHeader *>(mBlock)) {
        table = block->mTable ? block->mTable + 1 : nullptr;
        if (block->mTransforms) {
            transforms = block->mTransforms + 1;
            // Every eleventh call after a content change, the shapes' rest transforms are checked against the block's
            // (each composed with the block's first): one that differs sets the stale flag 0x10.
            if (mByteA2 <= 10) --mByteA2;
            if (mByteA2 == 0) {
                mShapeFlags &= uint16_t(~0x10u);
                const auto *const placed = reinterpret_cast<const nGZCompGeom::RigidTransform3 *>(transforms);
                for (const cShapeHandle &held : mShapes) {
                    if (mShapeFlags & 0x10) break;
                    cShapeLodInfo info;
                    held.Get()->GetRenderInfo(&info);
                    const auto *const rest =
                        reinterpret_cast<const nGZCompGeom::RigidTransform3 *>(uintptr_t(info.mRest[1]));
                    if (!rest) continue;
                    for (uint32_t i = 0; i < info.mTransformCount; ++i) {
                        nGZCompGeom::RigidTransform3 composed = rest[i];
                        composed.Compose(&placed[0]);
                        if (!nGZCompGeom::RigidNearlyEqual(&composed, &placed[i])) {
                            mShapeFlags |= 0x10;
                            break;
                        }
                    }
                }
                mByteA2 = 11;
            }
        }
    }
    const cShapeDetailRecord *record = mDetailRecords.data();
    for (const cShapeHandle &held : mShapes) {
        held.Get()->QueuePartDraw(mShapeFlags, uint32_t(uintptr_t(&table)), uint32_t(uintptr_t(&weights)),
                           uint32_t(uintptr_t(transforms)), uint32_t(uintptr_t(sink)), record->mHandle);
        ++record;
    }
}

// ---- the behaviour's registry ----

namespace {

std::optional<nGZEvent::cEventAttributeMap> gShapeRefRegistry;

// The shape-reference node's own events (the game's data 0x106b7fc-0x106b874): the morph weights, the four float lists
// and the colour, each with three words and 0xc.
const nGZEvent::cEventAttribute kMorphWeightRecord{kMorphWeightAttribute, "MorphWeight", 0, 0xbb22dd17, 3, 0xc};
const nGZEvent::cEventAttribute kFloatRecords[4] = {
    {kFloatAttribute, "MaterialFloat1", 0, 0xbb22dd17, 3, 0xc},
    {kFloat2Attribute, "MaterialFloat2", 0, 0x9b5b3300, 3, 0xc},
    {kFloat3Attribute, "MaterialFloat3", 0, 0x7b5b330c, 3, 0xc},
    {kFloat4Attribute, "MaterialFloat4", 0, 0x5b5b331a, 3, 0xc},
};
const nGZEvent::cEventAttribute kShapeColorRecord{kColourAttribute, "ShapeColor", 1, 0x5b5b331a, 3, 0xc};

// The node events the holder stores with 4 in the last word.
nGZEvent::cEventAttribute WithLastWord4(const nGZEvent::cEventAttribute &record)
{
    nGZEvent::cEventAttribute attribute = record;
    attribute.mWord14 = 4;
    return attribute;
}

} // namespace

// Built on first use. The original runs its holder 0xa53800 first (on every call, a once byte skipping it after the
// first): the transform family's events (0x257110: the node registry's, 0x257030, then the four bone events through the
// transform registry), then ten events through this function, whose map is made by the first of them as a copy of the
// transform family's map. (The holder's 0x14-byte object, kept in the static 0x10f62e4, is read by nothing.)
nGZEvent::cEventAttributeMap *__cdecl cShapeRefNodeBehaviorTraits::RegistryFor(uint32_t id,
                                                                             const nGZEvent::cEventAttribute *attribute)
{
    if (!gShapeRefRegistry) {
        cCompositionTreeNodeBehaviorTraits::RegisterEvents();
        cTransformNodeBehaviorTraits::RegistryFor(0xc9de8b96, &sgevents::kBoneID);
        cTransformNodeBehaviorTraits::RegistryFor(0x9b59fb00, &sgevents::kBoneTransform);
        cTransformNodeBehaviorTraits::RegistryFor(0xfb5b3383, &sgevents::kBoneTranslation);
        cTransformNodeBehaviorTraits::RegistryFor(0x9b5b3506, &sgevents::kBoneOrientation);
        gShapeRefRegistry.emplace(*TransformNodeEventRegistry());
        nGZEvent::cEventAttributeMap &map = *gShapeRefRegistry;
        map[0xdd041f30] = WithLastWord4(sgevents::kBoundsUpdated);
        map[0x2a7e3a48] = WithLastWord4(sgevents::kHandleGeometryNodeUpdated);
        map[0x3d44b140] = WithLastWord4(sgevents::kMaterialBindingsUpdated);
        map[0xedf79966] = WithLastWord4(sgevents::kPartCountUpdated);
        map[kMorphWeightAttribute] = kMorphWeightRecord;
        for (const nGZEvent::cEventAttribute &record : kFloatRecords) map[record.mId] = record;
        map[kColourAttribute] = kShapeColorRecord;
    }
    if (id != 0) (*gShapeRefRegistry)[id] = *attribute;
    return &*gShapeRefRegistry;
}

nGZEvent::cEventAttributeMap *cShapeRefNodeBehaviorTraits::Registry()
{
    const nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
    return RegistryFor(0, &defaults);
}

void cShapeRefNodeBehaviorTraits::ResetRegistryForTest() { gShapeRefRegistry.reset(); }

// (0xa55f00) The event's record (the registry's, else the missing record). With a material block, the float lists'
// events carry their list's count in word 2 (0 for a list it lacks); the morph weights event carries the morph count
// whether there is a block or not.
nGZEvent::cEventAttribute cShapeRefNodeImpl::GetEventAttribute(uint32_t event) const
{
    nGZEvent::cEventAttributeMap *const registry = cShapeRefNodeBehaviorTraits::Registry();
    const auto found = registry->find(event);
    if (found == registry->end()) return nGZEvent::behavior_mt::gMissingRecord;
    nGZEvent::cEventAttribute record = found->second;
    if (event == kMorphWeightAttribute) {
        record.mWord08 = mMorphCount;
        return record;
    }
    if (!mBlock) return record;
    const auto *const block = static_cast<const cMaterialBlockHeader *>(mBlock);
    for (uint32_t k = 0; k < 4; ++k)
        if (event == kFloatRecords[k].mId) record.mWord08 = block->mParamLists[k] ? *block->mParamLists[k] : 0;
    return record;
}

// ---- cShapeRefNode ----

// (The original's operator new throws on failure, so its null branch is dead.)
cShapeRefNode *__cdecl cShapeRefNode::Create() { return new cShapeRefNode; }

cShapeRefNode *cShapeRefNode::Construct() { return ::new (this) cShapeRefNode; }
void cShapeRefNode::Destruct() { this->cShapeRefNode::~cShapeRefNode(); }
cShapeRefNode *cShapeRefNode::DeletingDestructor(uint32_t flags)
{
    this->cShapeRefNode::~cShapeRefNode();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

bool cShapeRefNode::HookQueryInterface(uint32_t iid, void **out) { return cShapeRefNode::QueryInterface(iid, out); }
void cShapeRefNode::HookClone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context)
{
    cShapeRefNode::Clone(mode, iid, out, context);
}
uint32_t cShapeRefNode::HookAccept(cICompositionTreeTraversal *traversal) { return cShapeRefNode::Accept(traversal); }
void cShapeRefNode::HookOnEvent(uint32_t type, uint32_t arg1, void *data, uint32_t sender)
{
    cShapeRefNode::OnEvent(type, arg1, data, sender);
}
bool cShapeRefNode::HookOnQuery(uint32_t type, uint32_t arg1, void *data) { return cShapeRefNode::OnQuery(type, arg1, data); }
cShapeRange cShapeRefNode::HookGetShapes() { return Shapes(); }
void cShapeRefNode::HookAddShape(cIShape *shape) { mImpl.AddShape(this, shape); }
void cShapeRefNode::HookRemoveShape(cIShape *shape) { mImpl.RemoveShape(this, shape); }
bool cShapeRefNode::HookAnyShapeSlot52() { return mImpl.AnyShapeSlot52(); }
bool cShapeRefNode::HookHasShape(cIShape *shape) { return cShapeRefNode::HasShape(shape); }
void cShapeRefNode::HookOnContentChanged(uint32_t transform, uint32_t records, uint32_t count)
{
    cShapeRefNode::OnContentChanged(transform, records, count);
}
cBoundBox cShapeRefNode::HookGetBoundBoxIn(const cTransform34 *frame) { return cShapeRefNode::GetBoundBoxIn(frame); }
cBoundBox cShapeRefNode::HookGetBoundBox() { return cShapeRefNode::GetBoundBox(); }
bool cShapeRefNode::HookIntersectsBound(uint32_t bound, uint32_t frame) { return cShapeRefNode::IntersectsBound(bound, frame); }
bool cShapeRefNode::HookIntersectsLinear(nGZCompGeom::cILinear3 *linear) { return cShapeRefNode::IntersectsLinear(linear); }
bool cShapeRefNode::HookFirstHitLinear(float *t, nGZCompGeom::cILinear3 *linear)
{
    return cShapeRefNode::FirstHitLinear(t, linear);
}
void cShapeRefNode::HookSetAttribute(uint32_t id, uint32_t extra, uint32_t record) { cShapeRefNode::SetAttribute(id, extra, record); }
void cShapeRefNode::HookGetAttribute(uint32_t record, uint32_t id, uint32_t extra) { cShapeRefNode::GetAttribute(record, id, extra); }
bool cShapeRefNode::HookIsKindOf(uint32_t iid) { return cShapeRefNode::IsKindOf(iid); }
void *cShapeRefNode::HookMakeIterator51(void *out, uint32_t id) { return cShapeRefNode::MakeIterator51(out, id); }
void cShapeRefNode::HookDraw(void *viewer, uint32_t planeMask) { cShapeRefNode::Draw(viewer, planeMask); }
void cShapeRefNode::HookSlot83(void *a, void *b) { cShapeRefNode::Slot83(a, b); }
void cShapeRefNode::HookNotifyListeners(uint32_t type, uint32_t arg1, void *data)
{
    cShapeRefNode::NotifyListeners(type, arg1, data);
}
bool cShapeRefNode::HookAskListeners(uint32_t type, uint32_t arg1, void *data)
{
    return cShapeRefNode::AskListeners(type, arg1, data);
}
uint32_t cShapeRefNode::HookGetEventCount() { return cShapeRefNode::GetEventCount(); }
nGZEvent::cAttributeIDRange cShapeRefNode::HookGetEventIds() { return cShapeRefNode::GetEventIds(); }
nGZEvent::cEventAttribute cShapeRefNode::HookGetEventAttribute(uint32_t event) { return cShapeRefNode::GetEventAttribute(event); }

uint32_t __fastcall cShapeRefNode::HookFaceRelease(void *face, void *)
{
    return reinterpret_cast<cShapeRefNode *>(static_cast<uint8_t *>(face) - kFace8)->cShapeRefNode::Release();
}
bool __fastcall cShapeRefNode::HookFaceQueryInterface(void *face, void *, uint32_t iid, void **out)
{
    return reinterpret_cast<cShapeRefNode *>(static_cast<uint8_t *>(face) - kFace8)->cShapeRefNode::QueryInterface(iid, out);
}

// (0xa543d0) Every shape removed through RemoveShape, front first.
cShapeRefNode::~cShapeRefNode()
{
    while (!mImpl.mShapes.empty()) mImpl.RemoveShape(this, mImpl.mShapes.front().Get());
}

void cShapeRefNode::OnEvent(uint32_t type, uint32_t arg1, void *data, uint32_t sender)
{
    Behavior::OnEvent(type, arg1, data, sender);
    mImpl.OnEvent(this, type, arg1, data, sender);
}

bool cShapeRefNode::OnQuery(uint32_t type, uint32_t arg1, void *data)
{
    if (mImpl.OnQuery(this, type, arg1, data)) return true;
    return Behavior::OnQuery(type, arg1, data);
}

bool cShapeRefNode::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == cIShapeRefNode::kIid) {
        *out = static_cast<cIShapeRefNode *>(this);
        AddRef();
        return true;
    }
    return Base::QueryInterface(iid, out);
}

void cShapeRefNode::Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context)
{
    cICompositionTreeNode *clone = nullptr;
    if (context) {
        context->FindClone(this, cIShapeRefNode::kIid, &clone);
    } else {
        clone = new cShapeRefNode;
        clone->AddRef();
    }
    if (mode != 2) {
        auto *const cloneImpl = &static_cast<cShapeRefNode *>(clone)->mImpl;
        mImpl.CloneInto(this, clone, cloneImpl, mode, context);
    }
    clone->QueryInterface(iid, out);
    if (clone) clone->Release();
}

void *cShapeRefNode::MakeIterator43(void *out, uint32_t a, uint32_t b)
{
    return node_family::BuildIterator43(&mImpl, out, this, a, b);
}
void *cShapeRefNode::MakeIterator44(void *out, uint32_t a, uint32_t b)
{
    return node_family::BuildIterator44(&mImpl, out, this, a, b);
}

// 92 / 93 (0xa588e0): {first, first, end}, or three nulls when the list is empty.
cShapeRange cShapeRefNode::Shapes()
{
    std::vector<cShapeHandle> &items = mImpl.mShapes;
    if (items.empty()) return {nullptr, nullptr, nullptr};
    // A cShapeHandle is the shape pointer (4 bytes, nothing else), so the list reads as the pointers.
    cIShape **const first = reinterpret_cast<cIShape **>(items.data());
    return {first, first, first + items.size()};
}

// 99 (0xa55400): a linear search of the list.
bool cShapeRefNode::HasShape(cIShape *shape)
{
    for (const cShapeHandle &item : mImpl.mShapes)
        if (item.Get() == shape) return true;
    return false;
}

} // namespace nGZSceneGraph
