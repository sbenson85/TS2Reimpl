#pragma once
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZUnknownBase.h"
#include "rz/cRZString.h"
#include "simantics/cTSBehaviorTree.h" // RefPtrVector

#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

// nTSSG::cNHoodOccupantManager: the neighbourhood view's scene side of the decorations (flora, roads, props): it builds the
// scene-graph nodes for the occupants cTSNHoodOccupantManager holds, keeps them under one group node in the scene, merges
// flora into batches ("dontMergeNHFlora" turns that off) and fades props ("neighborhoodPropFadeDistance"). Lane GO
// (notes/feedback/GO.md, docs/subsystems/neighbourhood.md). RVAs 1.18.0.187, all /O2.
// RTTI 0x109eef0. Faces: cRZUnknown (+0, vtable 0xdb33d0, 6 slots), nTSSG::cINHoodOccupantManager (+8, 0xdb33ec, 12),
// cIGZMessageTarget2 (+0xc, 0xdb3420, 4). 0xd4 bytes.
// Creation: the creator 0x2a7b70 (cdecl; its address is stored by the nTSSG cDllCOMDirector constructor 0x244e30) calls the
// constructor 0x2a4a00, its only caller; the vtables are referenced only by the constructor and the destructor 0x2a50f0.
// Standalone shape (docs/standalone-shape.md): our class and vtables; the creator and constructor hooks are the switch.
// Every slot is ours (lanes GO, GR), and so are the scene-building helpers (lane GW) and PrepareBuildAll 0x2a81f0 and the
// node builders 0x2a5ef0, 0x2a63a0, 0x2a69b0, 0x2a7130, 0x2a7be0 (lane HB).

namespace nTSSG {

// A counted pointer member: null at first, Release (slot 2) when it goes, without clearing (the original's inline
// `test ecx, ecx; je; call [eax + 8]`, and the RZAutoRef arrays' 0x14c5b0 / 0x145d50).
struct SGRef {
    cIGZUnknown *mObject = nullptr;
    SGRef() = default;
    SGRef(const SGRef &) = delete;
    SGRef &operator=(const SGRef &) = delete;
    ~SGRef()
    {
        if (mObject) mObject->Release();
    }
    // Null first, then the old object released (the original's `mov [x], 0; call [eax + 8]`).
    void Reset()
    {
        if (cIGZUnknown *old = mObject) {
            mObject = nullptr;
            old->Release();
        }
    }
};
static_assert(sizeof(SGRef) == 4);

// A cRZString member (hook-shaped: Construct / Destruct by hand), built and destroyed in member order.
struct SGString {
    union {
        cRZString mString;
    };
    SGString() { mString.Construct(); }
    SGString(const SGString &) = delete;
    SGString &operator=(const SGString &) = delete;
    ~SGString() { mString.Destruct(); }
};
static_assert(sizeof(SGString) == 0x20);

// An element of the node list at +0x88 (the props that fade, filled by game code): a counted scene node and a byte saying the
// node is faded (its shape-colour flag set). 0x2a4e40 (its vector's tidy) and DetachScene release the node of each.
struct SGNodeEntry {
    SGRef mNode;
    uint8_t mFaded;
    uint8_t mPad[3];
};
static_assert(sizeof(SGNodeEntry) == 8);

// An element of the list at +0x7c (filled by game code): two cRZStrings (hook-shaped: built and destroyed by hand). Its
// destructor is 0x257860; the vector's tidy is the library's (0x2a5010, std::vector<Blob4<64>>::_Tidy).
struct SGTextPair {
    SGString mFirst;  // +0
    SGString mSecond; // +0x20
};
static_assert(sizeof(SGTextPair) == 0x40);

// The scene face (+8, game vtable 0xdb33ec, 12 slots). Names after what the bodies do (tentative where they say so).
class cINHoodOccupantManager : public cIGZUnknown {
public:
    // slot 3, 0x2a8100 (ret 0, EH): listens for the message ids, watches the property "neighborhoodPropFadeDistance" through
    // the property-notification service, records game option bit 7 at +0xd1; marks initialised. Always true.
    virtual bool Initialize() = 0;
    // slot 4, 0x2a92c0 (ret 0, EH): if initialised: stops listening, stops watching, DetachScene, drops +0x78 and +0x30.
    // Always true.
    virtual bool Shutdown() = 0;
    // slot 5, 0x2a8780 (ret 4, EH, 1462 B): (re)builds the scene nodes of every occupant (a byte: all of them, or only those
    // around the current lot: the lot must be one the editor accepts, else nothing happens past DetachScene). A new root and
    // four layer nodes (framework slot 3), the neighbourhood name, the sources at +0x28 / +0x2c / +0x30, the flora (added,
    // merged, or both), the dirty layers rebuilt, the pending nodes handed on, layer 0 shown unless merged, the other
    // occupants, the root into the scene, +0x60 set.
    virtual void BuildScene(uint32_t all) = 0;
    // slot 6, 0x2a7af0 (ret 8): layer `index` (0-3) shown or hidden: its node is added to (slot 45) or removed from (slot 46)
    // the root when it exists and the flag changes.
    virtual void SetLayerVisible(uint32_t index, uint8_t visible) = 0;
    virtual void MarkDirty() = 0;  // slot 7, 0x2a7b30: +0x60 = 1
    virtual void ClearDirty() = 0; // slot 8, 0x2a7660: +0x60 = 0
    // slot 9, 0x2a93c0 (ret 4, EH, 592 B): flora merged into one node or kept in layer 0 (a byte; never merged with the property
    // "dontMergeNHFlora"). On a change: unmerged, the merged node leaves the root and layer 0's joins it (shown); merged,
    // layer 0's leaves, the dirty layers' merge nodes (+0x98, slot 5) are reset, app slot 12's occupants in a dirty layer
    // re-added to the merge (0x2a5340), the dirty layers' +0xa8 objects rebuilt (slot 34), the pending nodes handed on
    // (0x2a9100) and dropped, the merged node joins the root (layer 0 marked hidden).
    virtual void SetMergeFlora(uint32_t merge) = 0;
    // slot 10, 0x2a86a0 (ret 0, EH, 217 B): when building everything with a flora merge pending, the merge is redone (0x2a5860,
    // 0x2a5d70) and the flag cleared; then slot 11 (through the vtable) with the scene's slot 18 object (counted for the call).
    virtual void Update() = 0;
    // slot 11, 0x2a9610 (ret 4, EH, 1417 B): props fade by the view's distance (only when built for one lot, with a fade
    // distance, props and a view). The view's position (its slot 140) against the camera bounds (inside, or the distance
    // squared to the bounds' clamp); each prop node's shape (QI 0xa516cd10) whose detail level (QI 0x1c585e30, slot 89) is
    // below the registry's limit and whose box top is not below 0 fades (alpha (d / fade)^4, shape-colour flag set) when the
    // view is nearer to its box than to the bounds, else goes back to opaque.
    virtual void UpdateFade(cIGZUnknown *view) = 0;
};

class cNHoodOccupantManager : public rz::cRZUnknown, public cINHoodOccupantManager, public cIGZMessageTarget2 {
public:
    // The game's vtables (the tests relate ours to them; nothing in src/ installs them).
    static constexpr uint32_t kVTable = 0xdb33d0;
    static constexpr uint32_t kSceneVTable = 0xdb33ec;
    static constexpr uint32_t kMessageVTable = 0xdb3420;
    static constexpr uint32_t kSceneIid = 0xebd4927b;         // QI -> +8
    static constexpr uint32_t kMessageTargetIid = 0x452294aa; // QI -> +0xc
    // Services through the framework's slot 5 (GetServiceObject, game global 0x10c9378): the property set and the property
    // notifier (one class id, two interfaces).
    static constexpr uint32_t kPropertyClass = 0x0ac1887e;
    static constexpr uint32_t kPropertySetIid = 0x4bb0d269;
    static constexpr uint32_t kPropertyNotifierIid = 0x6bb22f38;
    static constexpr uint32_t kFadeDistanceWatchId = 0x6fbda451;
    // The scene-graph system, cached in game global 0x10cc458 (shared with other code).
    static constexpr uint32_t kSgSystemCache = 0x10cc458;
    static constexpr uint32_t kSgSystemIdA = 0x9a353ce8;
    static constexpr uint32_t kSgSystemIdB = 0xfa353cdb;
    static constexpr uint32_t kOptionBit = 7; // game option bit read by Initialize (0xb39370 is 1 << n)

    // 0x2a4a00 (ret 0, EH): the vtables, 2500 at +0x10, the members, the ten message ids, layers shown, flora merged unless
    // the property "dontMergeNHFlora" is true. +0x14-0x20, +0xb8-0xbb, +0xca-0xcb, +0xd2-0xd3 stay as the allocator left them.
    cNHoodOccupantManager();
    // Primary slot 3, 0x2a5310 (ret 4; the destructor 0x2a50f0): Shutdown (non-virtual), then the members in reverse.
    ~cNHoodOccupantManager() override;
    // 0x2a7b70 (cdecl, EH): new 0xd4 (null tested), the constructor, the +8 face.
    static cINHoodOccupantManager *__cdecl Create();

    // cIGZUnknown (every face; the secondary faces' entries are the compiler's adjustor thunks 0x2a8deb / 0x2a8df3)
    bool QueryInterface(uint32_t riid, void **out) override; // slot 0, 0x2a8da0 (ret 8)
    uint32_t AddRef() override;                              // slot 1, folded 0x146a50: cRZUnknown's
    uint32_t Release() override;                             // slot 2, folded 0x1f9aa0: cRZUnknown's
    // primary slots 4 / 5 stay cRZUnknown's (0x1b9538 / 0x147a30).

    // cINHoodOccupantManager (+8; these run with this = the face)
    bool Initialize() override;
    bool Shutdown() override;
    void BuildScene(uint32_t all) override;
    void SetLayerVisible(uint32_t index, uint8_t visible) override;
    void MarkDirty() override;
    void ClearDirty() override;
    void SetMergeFlora(uint32_t merge) override;
    void Update() override;
    void UpdateFade(cIGZUnknown *view) override;

    // cIGZMessageTarget2 (+0xc): 0x2a7670 (ret 4, EH, 1150 B): the standard message (QI 0x65297976) by type: occupants added,
    // changed, removed, highlighted (with the scene built), the label texts, the +0x78 setting, the fade distance property, and
    // three that rebuild the scene when it was built for one lot only. True for the ten ids, false otherwise.
    bool DoMessage(void *message) override;

    // Non-virtual members (__thiscall on the object).
    // 0x2a56d0 (EH): with a root node: the root removed from the scene (scene-graph system slot 33's slot 9), +0x30's slot 21
    // and +0x30 dropped, the node list released (kept allocated), the four layer nodes removed from the root (where shown) and
    // dropped, the flora node at +0x94 removed (where merging), the root dropped.
    void DetachScene();
    // 0x2a5340 (ret 4, EH): an occupant added to its layer's flora merge node and queued at +0xbc.
    __declspec(noinline) void MergeOccupant(cIGZUnknown *occupant);
    // 0x2a7500 (ret 4, EH): an occupant without a node built by its kind (flora, two other faces, the others).
    __declspec(noinline) void AddOccupant(cIGZUnknown *occupant);
    // 0x2a8e70 (ret 4, EH): an occupant's node taken out of its layer; the flora layer marked dirty.
    __declspec(noinline) void RemoveOccupant(cIGZUnknown *occupant);
    // 0x2a9100 (ret 4, EH): a queued occupant's props handed to its layer's target in the merged flora node.
    __declspec(noinline) void HandOnPending(void *occupant);
    // 0x2a5a40 (EH): the merged flora node (+0x94) and the per-layer merge nodes (+0x98) and builders (+0xa8) built.
    __declspec(noinline) void BuildFloraNode();
    // 0x2a5d70 (EH): when building everything, layers 1 and 2 rebuilt from app slot 12's lists and merged.
    __declspec(noinline) void RebuildMergedLayers();
    // 0x2a5860: layers 1 and 2 emptied of their children.
    __declspec(noinline) void ClearLayers1And2();
    // 0x2a5560 (cdecl, recursive): on a node and its descendants (where slot 48(4) says so) `clear` cleared, `set` set.
    static __declspec(noinline) void __cdecl ChangeNodeFlags(void *node, uint32_t set, uint32_t clear);
    // 0x2a55e0 (cdecl, recursive, EH; also called by 0x2b0d30): the colour event to a node and its descendants.
    static __declspec(noinline) void __cdecl SetNodeColor(void *node, uint32_t arg, const float *rgba);
    // 0x2a7f20 (cdecl): an occupant's layer (0-3) from its position, -1 for none.
    static __declspec(noinline) int32_t __cdecl LayerOf(void *occupant);
    // 0x2a8570 (cdecl): a footprint touching the rectangle (`whole`: lying wholly inside it).
    static __declspec(noinline) bool __cdecl FootprintIn(void *footprint, const int32_t *rect, uint8_t whole);
    // 0x2a5ef0 (ret 4, EH): a flora occupant's node built (its model's shape placed, turned, attached; queued to fade for one
    // lot); its layer marked dirty.
    __declspec(noinline) void AddFloraOccupant(cIGZUnknown *occupant);
    // 0x2a63a0 (ret 4, EH): an occupant from app slot 12's slot 17 list built (its model's shape placed, turned, attached;
    // queued to fade for one lot when tall or in the view). False without a name, model or shape.
    __declspec(noinline) bool AddOtherOccupant(cIGZUnknown *occupant);
    // 0x2a81f0 (EH): without +0x78: the model "neighborhood_fxCube" looked up; its shape's first part becomes +0x78 (when
    // it has parts); then the text pairs at +0x7c refilled from +0x78's entries and +0x78's setting applied.
    __declspec(noinline) void PrepareBuildAll();
    // 0x2a7be0 (ret 8, EH; does not use this): a layer node's shape children merged into one group node (its shape face in
    // `out`; the QI's answer is the result).
    __declspec(noinline) bool MergeLayerChildren(cIGZUnknown *layer, cIGZUnknown **out);
    // 0x2a7130 (ret 4, EH): an occupant of face 0x6bd4d6ce built (its model looked up by name, a node placed and turned).
    __declspec(noinline) bool AddFaceCOccupant(cIGZUnknown *occupant);
    // 0x2a69b0 (ret 4, EH): a road occupant (face 0xabd4c3f4) built: its quad and its pieces made into meshes
    // ("neighborhood-roads-..."), grouped under one node attached to its placement. False when a part can't be created.
    __declspec(noinline) bool AddRoadOccupant(cIGZUnknown *occupant);
    // 0x2a9ba0 (ret 4): an occupant with a node, or of type 1 or 2, is removed (0x2a8e70) and added again (0x2a7500).
    void RefreshOccupant(cIGZUnknown *occupant);
    // 0x2a8040 (ret 8; does not use this): the occupant's node (slot 11) coloured from an ARGB word (each byte / 255, A first)
    // and its shape-colour flag set.
    void HighlightOccupant(cIGZUnknown *occupant, uint32_t argb);
    // 0x2a8e30 (ret 4; does not use this): the occupant's node's shape-colour flag cleared.
    void ClearOccupantHighlight(cIGZUnknown *occupant);
    // 0x2a9260: with +0x78, its slot 34 for each text pair (second, first as C strings).
    void ApplyTexts();
    // 0x2a8020: with +0x78, its slot 35 with the pointer in game data 0xffac34.
    void ApplyRef78Setting();

    // +0x00 cRZUnknown (vtable, mRefCount at +4); +0x08 and +0x0c the two interface vtables.
    uint32_t mFadeDistanceSq;         // +0x10 2500 (50 squared); DoMessage: the fade distance property squared
    int32_t mViewRect[4];             // +0x14 x0, y0, x1, y1: the lot's rect grown by 3 (BuildScene); not initialised
    uint8_t mInitialized;             // +0x24 Initialize / Shutdown
    uint8_t mPad25[3];
    SGRef mAppSlot12;                 // +0x28 app slot 12 (BuildScene); DetachScene calls its slot 21 and drops it
    SGRef mAppSlot11;                 // +0x2c app slot 11 (BuildScene)
    SGRef mSgSystem;                  // +0x30 the scene-graph system (BuildScene); Shutdown drops it
    std::set<uint32_t> mMessageIds;   // +0x34 the ten ids listened to
    SGString mNeighborhoodName;       // +0x3c app slot 6's slot 65 text (BuildScene)
    uint8_t mLayerVisible[4];         // +0x5c all 1
    uint8_t mDirty;                   // +0x60 slots 7 / 8; BuildScene sets it (scene built), DoMessage's occupant messages need it
    uint8_t mPad61[3];
    SGRef mLayerNodes[4];             // +0x64
    SGRef mRoot;                      // +0x74 the group node under the scene
    SGRef mRef78;                     // +0x78 Shutdown drops it
    std::vector<SGTextPair> mTexts;   // +0x7c
    std::vector<SGNodeEntry> mNodes;  // +0x88
    SGRef mFloraNode;                 // +0x94 the merged flora node
    SGRef mMergeNodes[4];             // +0x98 per layer, the flora merge (SetMergeFlora resets them, slot 5)
    SGRef mLayerBuilders[4];          // +0xa8 per layer (slot 34 rebuilds once the layer's merge is redone)
    uint8_t mLayerDirty[4];           // +0xb8 per layer: its merge needs redoing; not initialised
    RefPtrVector mPending;            // +0xbc counted pointers (destroyed first)
    uint8_t mMergeFlora;              // +0xc8 1, 0 with "dontMergeNHFlora"
    uint8_t mBuildAll;                // +0xc9 1; BuildScene's argument
    uint8_t mPadCA[2];
    uint32_t mLotByte;                // +0xcc 0; BuildScene: the lot's slot 23 byte (0 when building everything)
    uint8_t mFloraPending;            // +0xd0 0
    uint8_t mOptionBit7;              // +0xd1 0; Initialize
    uint8_t mPadD2[2];
};

static_assert(sizeof(cNHoodOccupantManager) == 0xd4, "new 0xd4 in 0x2a7b70, delete 0xd4 in 0x2a5310");
static_assert(offsetof(cNHoodOccupantManager, mFadeDistanceSq) == 0x10);
static_assert(offsetof(cNHoodOccupantManager, mInitialized) == 0x24);
static_assert(offsetof(cNHoodOccupantManager, mAppSlot12) == 0x28);
static_assert(offsetof(cNHoodOccupantManager, mMessageIds) == 0x34);
static_assert(offsetof(cNHoodOccupantManager, mNeighborhoodName) == 0x3c);
static_assert(offsetof(cNHoodOccupantManager, mLayerVisible) == 0x5c);
static_assert(offsetof(cNHoodOccupantManager, mDirty) == 0x60);
static_assert(offsetof(cNHoodOccupantManager, mLayerNodes) == 0x64);
static_assert(offsetof(cNHoodOccupantManager, mRoot) == 0x74);
static_assert(offsetof(cNHoodOccupantManager, mTexts) == 0x7c);
static_assert(offsetof(cNHoodOccupantManager, mNodes) == 0x88);
static_assert(offsetof(cNHoodOccupantManager, mFloraNode) == 0x94);
static_assert(offsetof(cNHoodOccupantManager, mMergeNodes) == 0x98);
static_assert(offsetof(cNHoodOccupantManager, mLayerBuilders) == 0xa8);
static_assert(offsetof(cNHoodOccupantManager, mPending) == 0xbc);
static_assert(offsetof(cNHoodOccupantManager, mMergeFlora) == 0xc8);
static_assert(offsetof(cNHoodOccupantManager, mLotByte) == 0xcc);
static_assert(offsetof(cNHoodOccupantManager, mOptionBit7) == 0xd1);

// The faces' offsets (base offsets are not constant expressions: the tests check these).
constexpr uint32_t kSceneFace = 8;
constexpr uint32_t kMessageFace = 0xc;

// The RVA hooks' entry points (src/hook/tables/nTSSGNHoodOccupantManager.cpp): every manager the game creates carries our
// vtables (the constructor and creator hooks), so these run only where the game calls an old entry point directly or holds a
// manager built with its own vtables. Original calling convention (this in ecx = the face of the slot), our functions called
// without virtual dispatch, bodies inlined.
struct cNHoodOccupantManagerAdapters {
    using Self = cNHoodOccupantManager;
    static Self *__fastcall Construct(Self *memory, void *);
    static void __fastcall Destruct(Self *self, void *);
    static Self *__fastcall DeletingDestructor(Self *self, void *, uint32_t flags);
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t riid, void **out);
    // the face thunks 0x2a8deb (+8) and 0x2a8df3 (+0xc)
    static bool __fastcall SceneQueryInterface(cINHoodOccupantManager *face, void *, uint32_t riid, void **out);
    static bool __fastcall MessageQueryInterface(cIGZMessageTarget2 *face, void *, uint32_t riid, void **out);
    static bool __fastcall Initialize(cINHoodOccupantManager *face, void *);
    static bool __fastcall Shutdown(cINHoodOccupantManager *face, void *);
    static void __fastcall SetLayerVisible(cINHoodOccupantManager *face, void *, uint32_t index, uint8_t visible);
    static void __fastcall MarkDirty(cINHoodOccupantManager *face, void *);
    static void __fastcall ClearDirty(cINHoodOccupantManager *face, void *);
    static void __fastcall Update(cINHoodOccupantManager *face, void *);
    static bool __fastcall DoMessage(cIGZMessageTarget2 *face, void *, void *message);
    static void __fastcall SetMergeFlora(cINHoodOccupantManager *face, void *, uint32_t merge);
    static void __fastcall UpdateFade(cINHoodOccupantManager *face, void *, cIGZUnknown *view);
    static void __fastcall BuildScene(cINHoodOccupantManager *face, void *, uint32_t all);
};

} // namespace nTSSG
