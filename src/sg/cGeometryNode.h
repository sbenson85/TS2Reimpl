#pragma once
// nGZSceneGraph::cGeometryNode (lane QU, scenegraph D3 part 4): the GMND resource, an object-graph node that is also a
// scene-graph resource and a cIGeometryRender (the display list draws it). It holds the geometry builders (parts) and the
// geometry data container (GMDC) they fill, and turns that container into draw elements (the build 0xa3f180, part 5)
// the render face draws (0xa41ee0 / 0xa429b0 / 0xa435d0, part 6). Standalone shape:
//
//   class cGeometryNode : public cObjectGraphNodeT<cIGeometryNode, cObjectGraphNodeImpl, Traits>,
//                         public cSGResource, public cIGeometryRender
//
//   +0x00 the node (vtable 0xe6987c, 51 slots: cIGeometryNode's 29-49, 50 the deleting destructor), +0x04 its count
//   +0x08 the persist face (vtable 0xe6994c, 7 slots), +0x0c the object-graph Impl (8 bytes)
//   +0x14 the cSGResource part (vtable 0xe6996c, 13 slots; vbptr +0x30, name +0x34, segment +0x38, flags +0x3c / +0x3d)
//   +0x40 the render face cIGeometryRender (vtable 0xe699a4, 10 slots)
//   +0x44 .. +0x129 the members below
//   +0x12c the vtordisp, +0x130 the virtual base cISGResource (vtable 0xe699cc, 25 slots; 18 / 19 answer "_gmnd" / "5gn")
//
// MSVC gives that layout from the declaration (static_asserts in the .cpp). The game's STL is MSVC's own, so the members
// are std:: containers. The overrides here are the one final overrider of the slots several faces share, as
// cResourceNode's.
//
// The switch: about sixteen game classes derive from this one (nTSSG's floor, terrain and wall geometry nodes, ...), and
// their constructors call 0xa3dd80 as a base part (most-derived flag 0), so the hook on 0xa3dd80 is our compiler's own
// constructor, as cSGResource's 0xa3c270 (lane OE): same ABI (thiscall, the flag on the stack, ret 4). The destructor
// 0xa3e320 (the derived destructors call it) and the slot bodies are hooked to the adapters below, so a game-derived
// object runs our bodies wherever its vtables still hold the game's. The factory 0xa42590 is hooked to Create.
//
// The geometry build 0xa3f180 is ours (lane QY, part 5); the render slots 3 / 6 / 8 (0xa41ee0, 0xa429b0, 0xa435d0),
// the device-object helper 0xa419e0, the event registry 0xa42f30 and the subset iterators (cGeometryNodeIterators.h)
// are ours (lane QZ, part 6). The render slots are this class's own bodies: every vtable holding them is a class
// derived from this one (the nTSSG geometry nodes), so they are hooked like the node's slots.
#include "game/owned.h"
#include "sg/SGNodeTemplates.h"
#include "sg/cIGeometryRender.h"
#include "sg/cObjectGraphNodeImpl.h"
#include "sg/cSGResource.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <type_traits>
#include <utility>
#include <vector>

namespace nGZSceneGraph {

class cIGeometryBuilder;       // sg/cGeometryBuilder.h
class cIGeometryDataContainer; // sg/cGeometryDataContainerClass.h

// A counted pointer member: the destructor releases a non-null pointer without clearing it; a copy adds a reference.
template <class T> struct cGeometryNodeRef {
    T *mPtr = nullptr;

    cGeometryNodeRef() = default;
    explicit cGeometryNodeRef(T *p) : mPtr(p)
    {
        if (mPtr) mPtr->AddRef();
    }
    cGeometryNodeRef(const cGeometryNodeRef &other) : mPtr(other.mPtr)
    {
        if (mPtr) mPtr->AddRef();
    }
    cGeometryNodeRef(cGeometryNodeRef &&other) noexcept : mPtr(other.mPtr) { other.mPtr = nullptr; }
    cGeometryNodeRef &operator=(const cGeometryNodeRef &other)
    {
        if (other.mPtr) other.mPtr->AddRef();
        T *const old = mPtr;
        mPtr = other.mPtr;
        if (old) old->Release();
        return *this;
    }
    cGeometryNodeRef &operator=(cGeometryNodeRef &&other) noexcept
    {
        std::swap(mPtr, other.mPtr);
        return *this;
    }
    ~cGeometryNodeRef()
    {
        if (mPtr) mPtr->Release();
    }
    // Cleared first, then released (the game's member resets).
    void Reset()
    {
        if (T *const p = mPtr) {
            mPtr = nullptr;
            p->Release();
        }
    }
};

// A counted reference without moves, for a vector that copies on growth as the original's does (the part list: its
// insert 0x18c0c0 counts the new part, then each old one into the new block, then drops the old block's; the bounds
// list's resize 0x2b5710 copies the same way).
template <class T> struct cGeometryNodeCopiedRef : cGeometryNodeRef<T> {
    using cGeometryNodeRef<T>::cGeometryNodeRef;
    cGeometryNodeCopiedRef(const cGeometryNodeCopiedRef &other) : cGeometryNodeRef<T>(other) {}
    cGeometryNodeCopiedRef &operator=(const cGeometryNodeCopiedRef &other)
    {
        cGeometryNodeRef<T>::operator=(other);
        return *this;
    }
};
static_assert(!std::is_nothrow_move_constructible_v<cGeometryNodeCopiedRef<cIGZUnknown>>, "copies on growth");

// A string-pool handle the node owns: closed on destruction (0xffffffff is empty).
struct cGeometryNodeCell {
    ServiceCell mCell{0xffffffff};

    cGeometryNodeCell() = default;
    cGeometryNodeCell(cGeometryNodeCell &&other) noexcept : mCell{other.mCell.mValue} { other.mCell.mValue = 0xffffffff; }
    cGeometryNodeCell &operator=(cGeometryNodeCell &&other) noexcept
    {
        std::swap(mCell.mValue, other.mCell.mValue);
        return *this;
    }
    ~cGeometryNodeCell() { mCell.Close(); }
};
static_assert(sizeof(cGeometryNodeCell) == 4, "cGeometryNodeCell size");

// What SetBounds (slot 40) is handed: objects asked through their slot 3 (an interface lookup: id, out) for 0x5ce57549.
// Only that slot is known.
class cIGeometryBoundsSource : public cIGZUnknown {
public:
    virtual bool Find(uint32_t iid, void **out) = 0; // 3
};

// The subset map's order: by handle (lookups by a bare handle too).
struct cGeometryNodeCellLess {
    using is_transparent = void;
    static uint32_t Key(const cGeometryNodeCell &c) { return c.mCell.mValue; }
    static uint32_t Key(uint32_t h) { return h; }
    template <class A, class B> bool operator()(const A &a, const B &b) const { return Key(a) < Key(b); }
};

// A draw element (0x7c bytes), one per primitive group the build makes (part 5 fills them; part 6 draws them). Only what
// part 4 touches is named: the reset word, the device objects slots 3 / 6 / 8 make (released when the device goes), and
// the destruction order (0x2ecb00: the references from +0x68 down, the index list, the name cell).
struct cGeometryNodeElement {
    cGeometryNodeElement() = default;
    // Growth of the element list copies (0xa3db00: the name cell is copied through the pool, unconditionally; the
    // references are counted in order); there is no move, so std::vector reproduces that.
    cGeometryNodeElement(const cGeometryNodeElement &other);
    cGeometryNodeElement &operator=(const cGeometryNodeElement &) = delete;

    uint32_t mWords[13] = {};       // +0x00 (+0x00 / +0x04 / +0x4c / +0x50 are what slot 7 forwards)
    uint16_t mPrepared = 0;         // +0x34 cleared by ReleaseElementResources
    uint16_t mPad36 = 0;
    cGeometryNodeCell mName;        // +0x38 the element's name (slot 45's text, slot 46's handle)
    uint32_t mWord3c = 0;           // +0x3c
    std::vector<uint32_t> mIndices; // +0x40
    uint32_t mWord4c = 0;           // +0x4c
    uint32_t mWord50 = 0;           // +0x50
    cGeometryNodeRef<cIGZUnknown> mRefs[6]; // +0x54 .. +0x68: [2] .. [5] the device objects (+0x5c .. +0x68)
    uint32_t mTail[4] = {};         // +0x6c .. +0x78
};
static_assert(sizeof(cGeometryNodeElement) == 0x7c, "cGeometryNodeElement size");

// The 16-byte records at +0x84 (a counted reference, then three words).
struct cGeometryNodeRefRecord {
    cGeometryNodeCopiedRef<cIGZUnknown> mRef; // copied on growth (0x2eb430), as the part list
    uint32_t mWords[3] = {};
};
static_assert(sizeof(cGeometryNodeRefRecord) == 0x10, "cGeometryNodeRefRecord size");

struct cGeometryNodeRecord24 {
    uint32_t mWords[6];
};
struct cGeometryNodeRecord28 {
    uint32_t mWords[7];
};

// What slots 42 / 44 hand out: three counted iterators (the cursor, the begin, the end).
struct cGeometrySubsetRange {
    cIGZUnknown *mCursor;
    cIGZUnknown *mBegin;
    cIGZUnknown *mEnd;
};

// nGZSceneGraph::cIGeometryNode (interface id 0x5ba38381): the object-graph node's slots, then 29-49. Names are ours.
class cIGeometryNode : public cIObjectGraphNode {
public:
    static constexpr uint32_t kIid = 0x5ba38381;

    virtual cIGeometryRender *GetRenderConst() = 0;                                  // 29 (0xa3e880)
    virtual cIGeometryRender *GetRender() = 0;                                       // 30 (0xa3e880)
    virtual void SetBuildFlags(uint32_t flags) = 0;                                  // 31 (0xa43510): a change rebuilds
    virtual void AddPart(cIGeometryBuilder *part) = 0;                               // 32 (0xa3e790)
    virtual void ClearParts() = 0;                                                   // 33 (0xa430c0)
    virtual void Rebuild() = 0;                                                      // 34 (0xa3ebe0)
    virtual uint8_t FindPart(uint32_t iid, void **out) = 0;                          // 35 (0xa42ac0)
    virtual void AddListener(nGZEvent::cIAbstractBehavior *listener) = 0;            // 36 (0xa3e830)
    virtual void RemoveListener(nGZEvent::cIAbstractBehavior *listener) = 0;         // 37 (0xa43160)
    virtual void SetContainer(cIGeometryDataContainer *container) = 0;               // 38 (0xa43530)
    virtual bool GetContainer(cIGeometryDataContainer **out) = 0;                    // 39 (0xa42600)
    virtual void SetBounds(uint32_t count, cIGZUnknown *const *objects) = 0;         // 40 (0xa43470)
    virtual uint32_t GetBuildFlags() = 0;                                            // 41 (0xa43090: +0x124)
    virtual cGeometrySubsetRange *GetSubsetNames(cGeometrySubsetRange *out) = 0;     // 42 (0xa436a0)
    virtual uint32_t CountSubset(const char *name) = 0;                              // 43 (0xa428d0)
    virtual cGeometrySubsetRange *GetSubsetParts(cGeometrySubsetRange *out, const char *name) = 0; // 44 (0xa43770)
    virtual uint32_t GetElementName(uint32_t index) = 0;                             // 45 (0xa42980): the text
    virtual uint32_t GetElementNameHandle(uint32_t index) = 0;                       // 46 (0xa429a0)
    virtual uint32_t GetNameA(uint32_t index) = 0;                                   // 47 (0xa42800): +0x10c's text
    virtual uint32_t GetNameB(uint32_t index) = 0;                                   // 48 (0xa427e0): +0x118's text
    virtual void *GetBoundsInfo() = 0;                                               // 49 (0xa430b0: this + 0x90)
};

// The behaviour cells of cAbstractBehaviorTypeT<cIGeometryNode, cRefSingleThreaded>, and its registry 0xa42f30.
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f6080); // the cell below (game/owned.h)
struct cGeometryNodeBehaviorTraits {
    static constexpr uint32_t kEventManagerCell = 0x10f6080; // 0xa3f100's cache
    static nGZEvent::cIConnectionManager *&EventManagerCell() { return ts2_rva_10f6080; }
    static nGZEvent::cEventAttributeMap *Registry();
    // 0xa42f30 (cdecl): built on first use (the holder 0xa3cf50: the base events, the object-graph node's two events in
    // the base registry 0x264010, then a copy of that and this class's four events); a non-zero `id` stores *attribute.
    __declspec(noinline) static nGZEvent::cEventAttributeMap *__cdecl RegistryFor(uint32_t id,
                                                                                const nGZEvent::cEventAttribute *attribute);
    static void ResetRegistryForTest(); // the next use builds it afresh
};

class cGeometryNode
    : public cObjectGraphNodeT<cIGeometryNode, cObjectGraphNodeImpl, cGeometryNodeBehaviorTraits>,
      public cSGResource,
      public cIGeometryRender {
public:
    using Node = cObjectGraphNodeT<cIGeometryNode, cObjectGraphNodeImpl, cGeometryNodeBehaviorTraits>;
    using Behavior = nGZEvent::cAbstractBehaviorTypeST<cIGeometryNode, cGeometryNodeBehaviorTraits>;

    static constexpr size_t kSize = 0x134;
    static constexpr uint32_t kTypeId = 0x7ba3838c; // the type-info record's word
    static constexpr uint32_t kVersion = 12;        // the type-info record's version
    static constexpr uint32_t kIidRender = 0x89d39f12;    // the render face, counted
    static constexpr uint32_t kIidUncounted = 0x9ba38357; // this node, not counted
    static constexpr uint32_t kRebuildEvent = 0xbbb112f6; // OnEvent: rebuild
    static constexpr uint32_t kDeviceEvent = 0xca7ccbbc;  // the graphic system's device event (connected in the constructor)
    static constexpr uint32_t kBuiltEvent = 0x9bb24282;   // Rebuild tells the listeners
    static constexpr uint32_t kBuiltQuery = 0x2a7e3a48;   // and asks them
    static constexpr uint32_t kFlagCheckIntegrity = 2;    // flags bit 1: read and written as a byte

    // 0xa3dd80: the node and the cSGResource part, every member empty, Reset and ReleaseElementResources, then connected
    // to the graphic system's device event.
    cGeometryNode();
    // 0xa3e320: the parts cleared (ClearParts), then the members, the cSGResource part and the node.
    ~cGeometryNode() override;
    cGeometryNode(const cGeometryNode &) = delete;
    cGeometryNode &operator=(const cGeometryNode &) = delete;

    // 0xa42590 (cdecl; the director's factory, hooked): a new node, uncounted.
    static cGeometryNode *__cdecl Create();

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t iid, void **out) override; // 0xa42b20
    uint32_t AddRef() override;                             // shared 0x3aecf0: the node's count
    uint32_t Release() override;                            // 0xa43040

    // ---- the behaviour ----
    void OnEvent(uint32_t type, uint32_t arg1, void *data, uint32_t arg3) override; // 0xa42760
    void NotifyListeners(uint32_t type, uint32_t arg1, void *data) override;        // 0xa43410
    bool AskListeners(uint32_t type, uint32_t arg1, void *data) override;           // 0xa42510

    // ---- cIObjectGraphNode ----
    void Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context) override; // 15: does nothing
    bool IsKindOf(uint32_t iid) override;                                                     // 25 (0xa43860)

    // ---- cIGeometryNode ----
    cIGeometryRender *GetRenderConst() override;
    cIGeometryRender *GetRender() override;
    void SetBuildFlags(uint32_t flags) override;
    void AddPart(cIGeometryBuilder *part) override;
    void ClearParts() override;
    void Rebuild() override;
    uint8_t FindPart(uint32_t iid, void **out) override;
    void AddListener(nGZEvent::cIAbstractBehavior *listener) override;
    void RemoveListener(nGZEvent::cIAbstractBehavior *listener) override;
    void SetContainer(cIGeometryDataContainer *container) override;
    bool GetContainer(cIGeometryDataContainer **out) override;
    void SetBounds(uint32_t count, cIGZUnknown *const *objects) override;
    uint32_t GetBuildFlags() override;
    cGeometrySubsetRange *GetSubsetNames(cGeometrySubsetRange *out) override;
    uint32_t CountSubset(const char *name) override;
    cGeometrySubsetRange *GetSubsetParts(cGeometrySubsetRange *out, const char *name) override;
    uint32_t GetElementName(uint32_t index) override;
    uint32_t GetElementNameHandle(uint32_t index) override;
    uint32_t GetNameA(uint32_t index) override;
    uint32_t GetNameB(uint32_t index) override;
    void *GetBoundsInfo() override;

    // ---- cIGZPersistSerializableReferent (+8, and the virtual base's 3-6) ----
    void Read(ifaces::cIGZPersistReader *reader) override;  // 0xa42bb0
    cPersistTypeInfo GetTypeInfo() override;                 // 0xa43890: {"cGeometryNode", 0x7ba3838c, 12}
    void Write(ifaces::cIGZPersistWriter *writer) override; // 0xa43950
    uint32_t GetOwnerInfo() override;                        // 0xa431c0: the cSGResource part's

    // ---- cIGZPersistResource (+0x14) ----
    uint32_t GetRefCount() override; // 0xa42640: the node's count

    // ---- cISGResource (the virtual base) ----
    const void *GetRecord18() override; // 0xa42840: "_gmnd"
    const void *GetRecord19() override; // 0xa42830: "5gn"

    // ---- cIGeometryRender (+0x40) ----
    // 3 (0xa41ee0): the device objects made once (`what` low byte 0, or made already: true); false once they failed.
    bool IsSupported(uint32_t what) override;
    uint32_t *GetFlags() override;                                                // 4: &mRenderInfo[0]
    uint32_t *GetDrawInfo(uint32_t index) override;                               // 5 (0xa42970): the element
    // 6 (0xa429b0): element `index`'s vertices processed (software vertex processing) into the context's dynamic array
    // (`flag` low byte: mVertices, else mVerticesB) with `declaration` (a cIVertexDeclarationDX); the first vertex, or
    // 0xffffffff when the array is too small.
    uint32_t Prepare(uint32_t index, uint32_t context, uint32_t declaration, uint32_t flag) override;
    void Begin(uint32_t index, cGeometryRenderContext *context, uint32_t first) override; // 7 (0xa423c0)
    // 8 (0xa435d0): element `index` drawn, by its state block (`hardware`) or its declarations; the base vertex.
    uint32_t Render(uint32_t index, cGeometryRenderContext *context, uint8_t hardware, uint32_t flag) override;

    // ---- the helpers (no slots) ----
    void Reset();                   // 0xa3ee00: every built thing emptied
    void ReleaseElementResources(); // 0xa3ed70: the device objects the render slots made
    void OnDeviceEvent(void *data); // 0xa42650: the device is back (code 1): the device objects dropped, rebuilt
    void EnsureContainer();         // 0xa3ec70: the container, loaded by key or built from the parts
    void ReleaseContainerIfKeyed(); // 0xa42360: a container that can be made again is dropped after use
    void BuildParts(cIGeometryDataContainer *container); // 0xa431e0: every part's Build over the container
    // 0xa3f180 (lane QY, cGeometryNodeBuild.cpp): the container turned into draw elements, the vertex and index buffers
    // and the bounds; throws a const char * when the device refuses a buffer.
    void BuildGeometry(cIGeometryDataContainer *container);
    // 0xa419e0 (thiscall, ret 8): each element's device objects made for the current device's vertex processing: the
    // software declarations (mRefs[0] / [1]) when it can process in software, else dropped; with hardware processing,
    // the hardware declarations (mRefs[2] / [3], when `declarations`) and the state blocks (mRefs[4] / [5], when
    // `stateBlocks`), else dropped. False when the node failed already or a declaration cannot be had. Not inlined: the
    // node event and build tests stand in for it.
    __declspec(noinline) bool ResetDeviceState(bool declarations, bool stateBlocks);
    static bool ReadBooleanSetting(const char *name); // a boolean setting (false without the service's answer)
    static bool CheckIntegritySetting(); // the "geomCheckGeomDataIntegrity" setting (inlined in 0xa431e0 and Read)

    // +0x44 the subset names (string-pool cells, closed with their nodes: 0x2eb770) to their parts
    std::multimap<cGeometryNodeCell, uint32_t, cGeometryNodeCellLess> mSubsets;
    uint32_t mRenderInfo[11] = {};                   // +0x4c slot 4's flags, then what part 6 keeps
    std::vector<cGeometryNodeElement> mElements;     // +0x78
    std::vector<cGeometryNodeRefRecord> mRecords;    // +0x84
    uint32_t mBoundsInfo = 0;                        // +0x90 slot 49 hands out its address
    uint32_t mWord94 = 0;                            // +0x94
    uint32_t mBoundsCount = 0;                       // +0x98 SetBounds: the count
    uint32_t mBoundsData = 0;                        // +0x9c SetBounds: the array
    std::vector<cGeometryNodeRecord24> mRecords24;   // +0xa0
    std::vector<cGeometryNodeCopiedRef<cIGZUnknown>> mBounds; // +0xac SetBounds' objects (each asked for 0x5ce57549)
    cGeometryNodeRef<cIGZUnknown> mRefB8;            // +0xb8
    cGeometryNodeRef<cIGZUnknown> mRefBC;            // +0xbc a device object (the device event drops it)
    cGeometryNodeRef<cIGZUnknown> mRefC0;            // +0xc0
    cGeometryNodeRef<cIGZUnknown> mRefC4;            // +0xc4 a device object
    std::vector<nGZEvent::cIAbstractBehavior *> mListeners; // +0xc8 uncounted
    std::vector<cGeometryNodeCopiedRef<cIGeometryBuilder>> mParts; // +0xd4
    cGeometryNodeRef<cIGeometryDataContainer> mContainer;    // +0xe0
    cGZPersistResourceKey mKey = {};                 // +0xe4 the container's key (type 0: built from the parts)
    std::vector<cGeometryNodeRecord28> mRecordsF4;   // +0xf4
    std::vector<cGeometryNodeRecord28> mRecords100;  // +0x100
    std::vector<cGeometryNodeCell> mNamesA;          // +0x10c slot 47's
    std::vector<cGeometryNodeCell> mNamesB;          // +0x118 slot 48's
    uint32_t mFlags = 0;                             // +0x124 slots 31 / 41
    uint8_t mDeviceReady = 0;                        // +0x128 "failed": set by IsSupported, cleared by the device event
    uint8_t mResourcesReady = 0;                     // +0x129 cleared by ReleaseElementResources
};

// The RVA hooks' entry points (src/hook/tables/cGeometryNode.cpp): the game's bodies take `this` = the face their thunks
// deliver (the object for the node's slots, +8 for Read / GetTypeInfo / Write, +0x14 for GetRefCount). Each calls the
// class's own body qualified (no virtual dispatch to a game-derived override), as the game's do; the bodies inlined.
struct cGeometryNodeAdapters {
    using Self = cGeometryNode;
    static constexpr int kPersistOffset = 0x08;
    static constexpr int kResourceOffset = 0x14;
    static Self *FromFace(void *face, int offset) { return reinterpret_cast<Self *>(static_cast<uint8_t *>(face) - offset); }

    static void __fastcall Destruct(Self *self, void *);                                       // 0xa3e320
    static void __fastcall Reset(Self *self, void *);                                          // 0xa3ee00
    static void __fastcall ReleaseElementResources(Self *self, void *);                        // 0xa3ed70
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t iid, void **out);       // 0xa42b20
    static uint32_t __fastcall Release(Self *self, void *);                                    // 0xa43040
    static void __fastcall OnEvent(Self *self, void *, uint32_t type, uint32_t arg1, void *data, uint32_t arg3); // 0xa42760
    static void __fastcall NotifyListeners(Self *self, void *, uint32_t type, uint32_t arg1, void *data);       // 0xa43410
    static bool __fastcall AskListeners(Self *self, void *, uint32_t type, uint32_t arg1, void *data);          // 0xa42510
    static void __fastcall Connect(Self *self, void *, uint32_t event, cIGZUnknown *target, uint32_t targetEvent);    // 0xa3f0e0
    static void __fastcall Disconnect(Self *self, void *, uint32_t event, cIGZUnknown *target, uint32_t targetEvent); // 0xa423a0
    static uint32_t __fastcall GetEventCount(Self *self, void *);                                                // 0xa42880
    static nGZEvent::cAttributeIDRange *__fastcall GetEventIds(Self *self, void *, nGZEvent::cAttributeIDRange *out); // 0xa3e890
    static nGZEvent::cEventAttribute *__fastcall GetEventAttribute(Self *self, void *, nGZEvent::cEventAttribute *out,
                                                                   uint32_t event);                             // 0xa3eb00
    static bool __fastcall IsKindOf(Self *self, void *, uint32_t iid);                         // 0xa43860
    static cIGeometryRender *__fastcall GetRender(Self *self, void *);                         // 0xa3e880
    static void __fastcall AddPart(Self *self, void *, cIGeometryBuilder *part);               // 0xa3e790
    static void __fastcall ClearParts(Self *self, void *);                                     // 0xa430c0
    static void __fastcall Rebuild(Self *self, void *);                                        // 0xa3ebe0
    static void __fastcall BuildGeometry(Self *self, void *, cIGeometryDataContainer *container); // 0xa3f180
    static void __fastcall AddListener(Self *self, void *, nGZEvent::cIAbstractBehavior *listener);    // 0xa3e830
    static void __fastcall RemoveListener(Self *self, void *, nGZEvent::cIAbstractBehavior *listener); // 0xa43160
    static bool __fastcall GetContainer(Self *self, void *, cIGeometryDataContainer **out);    // 0xa42600
    static void __fastcall SetBounds(Self *self, void *, uint32_t count, cIGZUnknown *const *objects); // 0xa43470
    static cGeometrySubsetRange *__fastcall GetSubsetNames(Self *self, void *, cGeometrySubsetRange *out); // 0xa436a0
    static uint32_t __fastcall CountSubset(Self *self, void *, const char *name);              // 0xa428d0
    static cGeometrySubsetRange *__fastcall GetSubsetParts(Self *self, void *, cGeometrySubsetRange *out,
                                                           const char *name);                  // 0xa43770
    static void __fastcall Read(void *face, void *, ifaces::cIGZPersistReader *reader);        // 0xa42bb0
    static cPersistTypeInfo *__fastcall GetTypeInfo(void *face, void *, cPersistTypeInfo *out); // 0xa43890
    static void __fastcall Write(void *face, void *, ifaces::cIGZPersistWriter *writer);       // 0xa43950
    static uint32_t __fastcall GetRefCount(void *face, void *);                                // 0xa42640
    // The render face's (`this` = +0x40) and the device-object helper.
    static constexpr int kRenderOffset = 0x40;
    static bool __fastcall ResetDeviceState(Self *self, void *, uint32_t declarations, uint32_t stateBlocks); // 0xa419e0
    static bool __fastcall IsSupported(void *face, void *, uint32_t what);                                     // 0xa41ee0
    static uint32_t __fastcall Prepare(void *face, void *, uint32_t index, uint32_t context, uint32_t declaration,
                                       uint32_t flag);                                                         // 0xa429b0
    static uint32_t __fastcall Render(void *face, void *, uint32_t index, cGeometryRenderContext *context,
                                      uint32_t hardware, uint32_t flag);                                       // 0xa435d0
};

} // namespace nGZSceneGraph

// 0xa3dd80's hook: the address of our constructor (`??0cGeometryNode@nGZSceneGraph@@QAE@XZ`, thiscall with the
// most-derived flag on the stack, ret 4), as cSGResource's (sg/cSGResource.h): /alternatename in cGeometryNodeClass.cpp.
extern "C" const uint8_t QU_cGeometryNode_Constructor;
