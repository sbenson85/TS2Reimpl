#pragma once
// nGZSceneGraph::cShape (lane RY, scenegraph H1a): the SHPE resource, class 0xfc6eb1f7. A shape is a scene-graph resource
// whose node is a referent node (the light template's, sg/SGLightTemplates.h), holding the geometry parts of each level of
// detail and the material bound to each subset name.
//
//   class cShape : public cSGResource, public cReferentNodeT<cIShape, cShapeBehaviorTraits>
//
//   +0x00 the cSGResource part (vtable 0xe6f514, 13 slots), +0x1c its vbptr (vbtable 0xe6f6cc), +0x20 name, +0x24 segment
//   +0x2c the node face cIShape (0xe6f54c, 62 slots: 61 the deleting destructor), +0x30 its count, +0x34 the persist face
//         (0xe6f648, 7 slots), +0x38 the cReferentNodeImpl
//   +0x4c mParts     std::vector<ShapePart>: one per part (slot 41 adds, 42 removes), 0x20 bytes each
//   +0x58 mBindings  std::map<subset name cell, ShapeBinding>: what slots 34-40 set per subset
//   +0x60 .. +0x70   five words copied from the first part's slot 29 -> slot 4 record (0xa5fbb0; zero without parts)
//   +0x74 mLods      std::deque<int32_t>: the levels of detail the stream lists (15 alone when it lists none)
//   +0x88 mPartNames std::vector<ShapePartName>: each part's name and level, sorted by level after Read
//   +0x94 the vtordisp, +0x98 the virtual base cISGResource (0xe6f668, 25 slots); 0x9c bytes
//
// The switch: the director's factory 0xa5d3b0 is hooked to Create. The constructor 0xa5bd40 has no other caller than the
// factory and the game's Clone 0xa5d070 (reached only through the game's vtable), and nothing derives from the class, so
// once the factory is ours every shape has our vtables. The slots and helpers not ported yet run the game's bodies on
// our object (OriginalFunction, `this` the face the game's vtable would pass): the layout is the game's to the byte (the
// members are MSVC 14.40 STL types, as the game's).
#include "game/owned.h"
#include "sg/SGLightTemplates.h"
#include "sg/cMaterialParser.h" // OwnedCell, CellLess

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <utility>
#include <vector>

struct ServiceCell;

namespace nGZSceneGraph {

// nGZSceneGraph::cIShape (interface id 0x3c6eb1f0): slots 34-60 after the referent node's. Names are tentative where
// only the arguments are known; `this` in each body is the node face (+0x2c). The returns are the bodies' eax (words).
class cIShape : public cIReferentNode {
public:
    static constexpr uint32_t kIid = 0x3c6eb1f0;

    virtual uint32_t BindMaterial(const char *material, const char *subset) = 0;  // 34 (0xa5f5b0, ret 8)
    virtual uint32_t BindMaterialToAllSubsets(uint32_t a) = 0;                                       // 35 (0xa5f620, ret 4)
    virtual uint32_t AddRenderGroup(const char *group, const char *subset) = 0;   // 36 (0xa5cad0, ret 8)
    virtual uint32_t SetRenderGroupMask(uint32_t mask, const char *subset) = 0;   // 37 (0xa5cb10, ret 8): -1 is all
    virtual uint32_t RemoveRenderGroup(const char *group, const char *subset) = 0; // 38 (0xa5f180, ret 8): slot 39 of the group's bit
    virtual uint32_t ClearRenderGroupMask(uint32_t mask, const char *subset) = 0;  // 39 (0xa5f1c0, ret 8): the mask's bits off
    virtual uint32_t SetSubsetWord(uint32_t word, const char *subset) = 0;        // 40 (0xa5f7d0, ret 8): binding +0x1c
    virtual uint32_t AddPart(cIGZUnknown *object, int32_t lod) = 0;               // 41 (0xa5c520, ret 8)
    virtual uint32_t RemovePart(int32_t lod) = 0;                                  // 42 (0xa5f230, ret 4)
    virtual uint32_t LoadNamedParts() = 0;                                                 // 43 (0xa5dd50)
    virtual uint32_t UpdateLods() = 0;                                             // 44 (0xa5efe0): 0xa5e280 -> 0xa5de40 -> 0xa5fa10
    virtual uint32_t PrepareSubsetVariants(const char *subset) = 0;                // 45 (0xa5d9c0, ret 4)
    // 46 (0xa5c6f0, ret 0x18): hands the part to show to the draw sink (`sink` points at the pointer to the sink object); `tablePtr` and
    // `weightsPtr` point at pointers that are advanced past the part's own records.
    virtual uint32_t QueuePartDraw(uint32_t flags, uint32_t tablePtr, uint32_t weightsPtr, uint32_t transforms, uint32_t sink,
                                   uint32_t detailHandle) = 0;
    virtual uint32_t PartObjectAtLod(uint32_t a) = 0;                                       // 47 (0xa5d430, ret 4)
    virtual uint32_t PartObjectAtLod2(uint32_t a) = 0;                                       // 48 (0xa5d430, the same body)
    virtual uint32_t FirstPartObject() = 0;                                        // 49 (0xa5d420): the first part's object
    virtual uint32_t FirstPartObject2() = 0;                                       // 50 (0xa5d420, the same body)
    virtual uint32_t SetLods(uint32_t a, uint32_t b) = 0;                           // 51 (0xa5f540, ret 8: the levels)
    virtual uint32_t GetRenderInfo(void *out) = 0;                                        // 52 (0xa5d7f0, ret 4: returns out)
    virtual uint32_t MaterialNameOf(const char *subset) = 0;                       // 53 (0xa5cfd0, ret 4): null without one
    virtual uint32_t GetSubsetBindings(void *out, const char *subset) = 0;         // 54 (0xa5f840, ret 8: returns out)
    virtual uint32_t GetPartNameAtLod(uint32_t a, uint32_t b) = 0;                           // 55 (0xa5e1d0, ret 8)
    virtual uint32_t GetPartLods(uint32_t a) = 0;                                       // 56 (0xa5dca0, ret 4)
    virtual uint32_t GetShownLod() = 0;                                                 // 57 (0xa5dc60)
    virtual uint32_t QueryPair(void *out, uint32_t a) = 0;                        // 58 (0xa5e170, ret 8: returns out)
    virtual uint32_t GrowBoxByPartsMoved(uint32_t a, uint32_t b, uint32_t c, uint32_t d) = 0;   // 59 (0xa5d590, ret 0x10)
    virtual uint32_t GrowBoxByParts(uint32_t a) = 0;                                       // 60 (0xa5d4f0, ret 4)
};

// The preferred level's fallback (0xa5e280's function statics): the object of the service 0xe5074b6b / 0x25074b4d slot 12
// returns, cached under a thread-safe guard.
TS2_OWNED_STATIC(void *, 10f6310);
TS2_OWNED_STATIC(int, 10f6314);

// The registry of the shape's behaviour (0xa5f010 builds it on first use, the shared map shape of 0x3a5d00).
nGZEvent::cEventAttributeMap *ShapeEventRegistry();

TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f6324); // the cell below (game/owned.h)
struct cShapeBehaviorTraits {
    static constexpr uint32_t kEventManagerCell = 0x10f6324; // 0xa5d140's cache
    static nGZEvent::cIConnectionManager *&EventManagerCell() { return ts2_rva_10f6324; }
    static nGZEvent::cEventAttributeMap *Registry() { return ShapeEventRegistry(); }
    // 0xa5f010 (cdecl): the registry, built on first use (holder 0xa59480, parent map 0xa480c0 folded in); a non-zero
    // id stores `attribute` under it. Our own map (the original's is the game global 0x10f6318). Not inlined: tests stand in.
    __declspec(noinline) static nGZEvent::cEventAttributeMap *__cdecl RegistryFor(uint32_t id,
                                                                                  const nGZEvent::cEventAttribute *attribute);
    static void ResetRegistryForTest(); // tests only: the next use builds the registry again
};

// A material index of the material manager (the cached queried object 0xae8900, cIMaterialManager): -1 is none. A copy
// takes a reference (slot 20, 0xae8800: unconditionally), the destructor drops it (slot 21 unless -1, 0xae8a00).
struct ShapeMaterialHandle {
    int32_t mIndex = -1;
    ShapeMaterialHandle() = default;
    explicit ShapeMaterialHandle(int32_t index); // 0xae8820: takes a reference (unconditionally, -1 too)
    ShapeMaterialHandle(const ShapeMaterialHandle &other);
    ShapeMaterialHandle &operator=(const ShapeMaterialHandle &) = delete;
    // 0xae88a0: the source counted, the old index dropped (unconditionally, -1 too), the index stored.
    void Assign(const ShapeMaterialHandle &source);
    ~ShapeMaterialHandle();
};
static_assert(sizeof(ShapeMaterialHandle) == 4, "ShapeMaterialHandle size");

// What a part's subsets were bound to (0xa5fbb0): a render-group mask and the binding's word, per subset.
struct ShapeSubsetGroups {
    uint32_t mMask;
    uint32_t mWord;
};

// A part (slot 41's): its level of detail, the object (asked for 0x5ba38381 when read; slots 47 / 48 / 29 / 46 are asked
// of it), and per subset of the object the material and groups 0xa5fbb0 found for its name. Only the game's code (slots
// 41 / 42, 0xa5fbb0) fills and empties these; the destructor removes every part through slot 42 first.
struct ShapePart {
    int32_t mLod = 0;                              // +0
    cIGZUnknown *mObject = nullptr;                // +4 (counted: slot 1 takes, slot 2 drops)
    std::vector<ShapeMaterialHandle> mMaterials;   // +8
    std::vector<ShapeSubsetGroups> mGroups;        // +0x14

    ShapePart() = default;
    ShapePart(int32_t lod, cIGZUnknown *object) : mLod(lod), mObject(object) { Take(); }
    // 0xa5bbb0: the object counted again, the vectors copied at their size.
    ShapePart(const ShapePart &other)
        : mLod(other.mLod), mObject(Taken(other.mObject)), mMaterials(other.mMaterials), mGroups(other.mGroups)
    {
    }
    // The sort's and the erase's moves (0xa59f00, 0xa5ad50): the object counted again, the vectors taken. Not noexcept:
    // growth copies, as the game's does.
    ShapePart(ShapePart &&other) noexcept(false)
        : mLod(other.mLod), mObject(Taken(other.mObject)), mMaterials(std::move(other.mMaterials)), mGroups(std::move(other.mGroups))
    {
    }
    ShapePart &operator=(ShapePart &&other) noexcept(false)
    {
        if (this == &other) return *this;
        mLod = other.mLod;
        if (mObject != other.mObject) {
            cIGZUnknown *const old = mObject;
            mObject = other.mObject;
            Take();
            if (old) old->Release();
        }
        mMaterials = std::move(other.mMaterials);
        mGroups = std::move(other.mGroups);
        return *this;
    }
    ShapePart &operator=(const ShapePart &other)
    {
        ShapePart copy(other);
        return *this = std::move(copy);
    }
    ~ShapePart()
    {
        // 0xa5c090: the groups, then the materials, then the object.
        std::vector<ShapeSubsetGroups>().swap(mGroups);
        std::vector<ShapeMaterialHandle>().swap(mMaterials);
        if (mObject) mObject->Release();
    }

private:
    void Take()
    {
        if (mObject) mObject->AddRef();
    }
    static cIGZUnknown *Taken(cIGZUnknown *object)
    {
        if (object) object->AddRef();
        return object;
    }
};
static_assert(sizeof(ShapePart) == 0x20, "ShapePart size");

// A subset's binding (the map's value): the material (slot 34), the render-group mask (36 / 37; -1 is all), a word (40).
struct ShapeBinding {
    ShapeMaterialHandle mMaterial; // node +0x14
    uint32_t mMask = 0;            // node +0x18
    uint32_t mWord = 0;            // node +0x1c
};

// A part's name and level (the vector at +0x88).
struct ShapePartName {
    OwnedCell mName;
    int32_t mLod;
};
static_assert(sizeof(ShapePartName) == 8, "ShapePartName size");

class cShape : public cSGResource, public cReferentNodeT<cIShape, cShapeBehaviorTraits> {
    using Node = cReferentNodeT<cIShape, cShapeBehaviorTraits>;

public:
    static constexpr size_t kSize = 0x9c;
    static constexpr uint32_t kClassId = 0xfc6eb1f7; // the type-info record's word
    static constexpr uint32_t kVersion = 8;          // what Write records
    static constexpr uint32_t kPartIid = 0x5ba38381; // what Read asks the stream's object references for
    static constexpr uint32_t kKeyType = 0x7ba3838c; // the type Write keys a named part's resource with
    static constexpr int32_t kDefaultLod = 15;       // the level Read lists when the stream lists none (0xe6f1d8)

    cShape();           // 0xa5bd40 (flag 1)
    ~cShape() override; // 0xa5c140; deleting 0xa5c4b0 (primary 12), 0xa5c442 (node face 61)

    static cIShape *__cdecl Create(); // 0xa5d3b0 (hooked)

    // ---- cIGZUnknown (every face) ----
    // 0xa5e4f0 (ret 8): cIShape and cIReferentNode are the node face, counted; then the referent node's answers
    // (0x3a5b50); then the cSGResource part's (0xa3c4d0).
    bool QueryInterface(uint32_t iid, void **out) override;
    uint32_t AddRef() override { return Node::AddRef(); }   // (0x3a46c0: the node's)
    uint32_t Release() override { return Node::Release(); } // 0xa5f160: the node's; the last one deletes

    // ---- the behaviour ----
    // 0xa5d710 (ret 0x10): the behaviour's, then on 0x1d05597b three notifications (one a
    // cMaterialBindingsUpdated) and on 0x9bb24282 0xa5fbb0.
    void OnEvent(uint32_t type, uint32_t arg1, void *data, uint32_t sender) override;

    // ---- cIObjectGraphNode ----
    // 15 (0xa5d070, ret 0x10): a new shape, CopyContentTo, viewed as `iid` in *out (counted when answered).
    void Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context) override;
    bool IsKindOf(uint32_t iid) override { return iid == cIShape::kIid || this->mImpl.IsKindOf(iid); } // 0xa5f9f0

    // ---- cIReferentNode ----
    cISGResource *GetResourceConst() override { return static_cast<cISGResource *>(this); } // shared 0x3a4f50
    cISGResource *GetResource() override { return static_cast<cISGResource *>(this); }

    // ---- cIShape (34-60; ours: 49 / 50 / 58, the bodies sg/SGShapeGeometry.h hooked before the class) ----
    uint32_t BindMaterial(const char *material, const char *subset) override;
    uint32_t BindMaterialToAllSubsets(uint32_t a) override;
    uint32_t AddRenderGroup(const char *group, const char *subset) override;
    uint32_t SetRenderGroupMask(uint32_t mask, const char *subset) override;
    uint32_t RemoveRenderGroup(const char *group, const char *subset) override;
    uint32_t ClearRenderGroupMask(uint32_t mask, const char *subset) override;
    uint32_t SetSubsetWord(uint32_t word, const char *subset) override;
    uint32_t AddPart(cIGZUnknown *object, int32_t lod) override;
    uint32_t RemovePart(int32_t lod) override;
    uint32_t LoadNamedParts() override;
    uint32_t UpdateLods() override;
    uint32_t PrepareSubsetVariants(const char *subset) override;
    uint32_t QueuePartDraw(uint32_t flags, uint32_t tablePtr, uint32_t weightsPtr, uint32_t transforms, uint32_t sink,
                           uint32_t detailHandle) override;
    uint32_t PartObjectAtLod(uint32_t a) override;
    uint32_t PartObjectAtLod2(uint32_t a) override;
    uint32_t FirstPartObject() override;
    uint32_t FirstPartObject2() override;
    uint32_t SetLods(uint32_t a, uint32_t b) override;
    uint32_t GetRenderInfo(void *out) override;
    uint32_t MaterialNameOf(const char *subset) override;
    uint32_t GetSubsetBindings(void *out, const char *subset) override;
    uint32_t GetPartNameAtLod(uint32_t a, uint32_t b) override;
    uint32_t GetPartLods(uint32_t a) override;
    uint32_t GetShownLod() override;
    uint32_t QueryPair(void *out, uint32_t a) override;
    uint32_t GrowBoxByPartsMoved(uint32_t a, uint32_t b, uint32_t c, uint32_t d) override;
    uint32_t GrowBoxByParts(uint32_t a) override;

    // ---- cIGZPersistSerializableReferent (+0x34, and the virtual base's 3-6) ----
    // 0xa5e580 (ret 4): the record, the cSGResource part, the node (version 5 on: the referent Impl's record first), the
    // levels (version 7 on), the parts (by object reference, or by name from version 8 on), the bindings.
    void Read(ifaces::cIGZPersistReader *reader) override;
    cPersistTypeInfo GetTypeInfo() override; // 0xa5fb00: {"cShape", 0xfc6eb1f7, 8}
    // 0xa5fef0 (ret 4): the mirror of Read at version 8 (every part by name when it has one).
    void Write(ifaces::cIGZPersistWriter *writer) override;
    uint32_t GetOwnerInfo() override { return cSGResource::GetOwnerInfo(); } // shared 0x3a5fb0 -> 0xa3c750

    // ---- cIGZPersistResource (the primary) ----
    uint32_t GetRefCount() override { return this->Node::mRefCount & nGZEvent::behavior_mt::kCountMask; }

    // ---- cISGResource (the virtual base) ----
    const void *GetRecord18() override { return "_shpe"; } // 0xe6f194
    const void *GetRecord19() override { return "5sh"; }   // 0xe6f19c

    void CopyContentTo(cShape *target, uint32_t mode); // 0xa5d8a0 (ret 8, this the source)
    std::vector<ShapePart>::iterator FindPartRange(int32_t lod); // 0xa5db60 (the part of the level or end)

    // The node face, what the game's bodies take as `this`.
    cIShape *Face() { return static_cast<cIShape *>(this); }

    std::vector<ShapePart> mParts;                                // +0x4c
    std::map<OwnedCell, ShapeBinding, CellLess> mBindings;        // +0x58
    uint32_t mFirstPartInfo[5] = {};                              // +0x60 (0xa5fbb0: record words 1, 2, 0, 3, 4)
    std::deque<int32_t> mLods;                                    // +0x74
    std::vector<ShapePartName> mPartNames;                        // +0x88
};

// The name of a resource object (0xa5d470, __cdecl): its cISGResource's name (slot 17), null for none or an empty one.
const char *ShapeObjectName(cIGZUnknown *object);

// 0xaed300 (__cdecl, returns out; hooked: the game's other callers get it too): a name's string-pool cell opened into
// `out`, the empty cell (0xffffffff) for a null or empty text.
ServiceCell *__cdecl MakeNameCell(ServiceCell *out, const char *name);

class cIGeometryRender;
// 0xaeaf30 (__cdecl; only slot 45 calls it): for a part's material `index` bound to `material` (none for -1), the
// material manager asked for the variant of the element's format key (slot 22), then told the render to support the
// format of every variant the material has (slot 3 with the variant's format byte).
void __cdecl RequestDrawVariants(cIGeometryRender *render, uint32_t index, int32_t material);

} // namespace nGZSceneGraph
