#pragma once
// nGZSceneGraph::cShapeRefNode (lane SN, scenegraph H2a): the shape-reference node, the renderable node that draws a list
// of shapes (nGZSceneGraph::cIShape, interface 0x3c6eb1f0; the scene's models). Standalone shape:
// cRenderableNodeT<cIShapeRefNode, cShapeRefNodeImpl, cShapeRefNodeBehaviorTraits> (sg/SGRenderableTemplates.h) plus
// the shape-reference level (the game's cShapeRefNodeT) and Clone; 0xd0 bytes like the game's (vtables 0xe6ef70, 101
// slots, and 0xe6f108 at +8).
//
// The switch: the factory 0xa55270 (the director's) is hooked to Create, and the constructor 0xa541e0 to Construct, so
// every shape-reference node is built by us. cTSScreenEffect's slot 0 (0xc09820) builds a cScreenShapeRef with that
// constructor, a class derived from this one that lays its own vtables (0xe91708 / 0xe918a0) over ours; they hold the
// game's bodies at the same RVAs as cShapeRefNode's (all but slot 70), so those bodies are hooked to ours too (the
// Hook* members below), and the destructors 0xa543d0 / 0xa544f0 / 0xa54880 likewise (lane SP).
//
// What is ours (lanes SN, SP, TB, TD): the layout, the constructors and destructors of every level, the factory,
// QueryInterface, IsKindOf, Clone (15), Accept (30 / 50), the template slots, the shape-reference slots (70, 87-99), the
// behaviour (3, 4), adding and removing shapes, the material block (0xa589a0), persistence, bounds and hits (TB), the
// attributes, their names, Draw and slot 83 (TD). Still the game's: the detail object 0xaead80 (OriginalFunction) and the
// display list's slot 25 Draw calls (a virtual call). See
// docs/subsystems/scenegraph.md, H2.
#include "game/owned.h"
#include "sg/SGRenderableTemplates.h"
#include "sg/cMaterialParser.h" // OwnedCell
#include "sg/cRenderableNodeImpl.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nGZSceneGraph {

class cIShape;
struct cViewerView;

// The range slots 92 / 93 hand out: {first, first, end} over the shape pointers, all null when there are none.
struct cShapeRange {
    cIShape **mFirst;
    cIShape **mCurrent;
    cIShape **mEnd;
};

// nGZSceneGraph::cIShapeRefNode (interface id 0xa516cd10): a renderable node holding a list of shapes, each counted.
// Names are ours and tentative.
class cIShapeRefNode : public cIRenderableNode {
public:
    static constexpr uint32_t kIid = 0xa516cd10;

    virtual uint8_t GetRefKind() = 0;                          // 87 (shared 0x559c60: 2)
    virtual uint32_t GetDetailLimit() = 0;                     // 88 (shared 0x251bf0: the Impl's +0x68)
    virtual uint32_t GetWord6c() = 0;                          // 89 (shared 0x24bdc0: the Impl's +0x6c)
    virtual void SetWord6c(uint32_t value) = 0;                // 90 (shared 0x4daa20)
    virtual void SetShapeFlags(uint16_t flags) = 0;            // 91 (0xa588d0: the Impl's +0xa0 half-word)
    virtual cShapeRange GetShapesConst() = 0;                  // 92 (0xa588e0)
    virtual cShapeRange GetShapes() = 0;                       // 93 (0xa588e0)
    virtual void AddShape(cIShape *shape) = 0;                 // 94 (0xa548f0 -> 0xa55750)
    virtual void RemoveShape(cIShape *shape) = 0;              // 95 (0xa58710 -> 0xa570f0)
    virtual uint16_t GetShapeFlags() = 0;                      // 96 (0xa552e0)
    virtual bool AnyShapeSlot52() = 0;                         // 97 (0xa553f0 -> 0xa561f0)
    virtual uint32_t ShapeCount() = 0;                         // 98 (shared 0xa58000)
    virtual bool HasShape(cIShape *shape) = 0;                 // 99 (0xa55400)
};

// A detail registration the Impl keeps beside each shape (8 bytes): the detail object's slot 19 hands out the handle for
// a shape's detail key (0xa59340), and slot 20 is given both on removal. {0, 0} until the first refresh.
struct cShapeDetailRecord {
    uint32_t mHandle; // +0: the detail object's slot 19 answer
    uint32_t mKey;    // +4: the shape's detail key it was registered for (0: none)
};

// A counted shape pointer, the element of the Impl's shape list (the game's cRZAutoRefCount<cIShape>, its copy and
// assignment inlined in the game's vector code): copying counts, assigning counts the new shape before releasing the
// old one and does nothing for the same pointer, destroying releases. No move: std::vector copies on growth (each
// shape counted again, then the old copies released front first) and copy-assigns on erase, as the game's vector does.
class cShapeHandle {
public:
    explicit cShapeHandle(cIShape *shape);
    cShapeHandle(const cShapeHandle &other);
    cShapeHandle &operator=(const cShapeHandle &other);
    ~cShapeHandle();
    cIShape *Get() const { return mShape; }

private:
    cIShape *mShape;
};
static_assert(sizeof(cShapeHandle) == 4, "cShapeHandle size");

// nGZSceneGraph::cShapeRefNodeImpl (0xc4 bytes, at +0xc of the node): the renderable state plus the shapes. Constructor
// 0xa54200 and destructor 0xa544f0 are ours (written out below: every member zero but the half-word's flags byte and
// the colour); the bodies that are still the game's are forwarders to their RVAs (thiscall on this Impl, `outer` the
// node). The vectors are std::vector with the default allocator, as the game's (its CRT and ours share the process
// heap), so the game's bodies grow them and ours free them.
class cShapeRefNodeImpl : public cRenderableNodeImpl {
public:
    static constexpr uint32_t kIid = cIShapeRefNode::kIid;
    static constexpr uint32_t kNoColour = 0xffffffffu; // the constructor's colour (DAT 0x106b99c, read by nothing else)
    enum : uint16_t { kFlag70 = 0x100 };               // slot 70's bit (the byte +0xa1, bit 0)
    enum : uint16_t { kWeighted = 0x20 };              // Read's: some morph weight is not near zero
    static constexpr uint32_t kVersion = 21;           // what Write records (Read: 20 on the colour, 21 on the names)
    static constexpr uint32_t kTypeWord = 0x65245517;  // the type record's word 4

    // The events and queries the Impl answers (0xa560f0 / 0xa561a0).
    enum : uint32_t {
        kDetailChangedEvent = 0xedf79966,    // the detail keys are refreshed
        kMaterialsChangedEvent = 0x3d44b140, // the material block is rebuilt and the detail limit read again
        kSubordinateEvent = 0x1d05597b,      // a subordinate added / removed (cObjectGraphNodeImpl's notifications)
        kTransformEvent = 0xdd041f30,        // the transform changed
        kRefreshQuery = 0x2a7e3a48,          // everything refreshed; answered
        kBlockChangedEvent = 0xe9dd42cf,     // sent to the node's listeners when the material block changes
    };

    cShapeRefNodeImpl() = default;
    ~cShapeRefNodeImpl();
    cShapeRefNodeImpl(const cShapeRefNodeImpl &) = delete;
    cShapeRefNodeImpl &operator=(const cShapeRefNodeImpl &) = delete;

    // 0xa544f0 as a hookable member (thiscall, no arguments): the destructor, qualified.
    void Destruct();

    // 0xa57220: the shape-reference interface, else the renderable Impl's answer.
    bool IsKindOf(uint32_t iid);

    // ---- lane SP ----
    // 0xa560f0: the composition Impl's (0xa38160) first; then a transform change propagates, a shape subordinate
    // added / removed (kinds 2 / 3 also propagate the transform) refreshes the detail keys and the material block,
    // the materials event rebuilds the block and rereads the detail limit, and the detail event refreshes the keys.
    void OnEvent(cICompositionTreeNode *outer, uint32_t type, uint32_t arg1, void *data, uint32_t sender);
    // 0xa561a0: kRefreshQuery refreshes the detail keys and the block, propagates the transform, and answers true;
    // anything else false (the folded 0x147110).
    bool OnQuery(cICompositionTreeNode *outer, uint32_t type, uint32_t arg1, void *data);
    // 0xa55750: the subordinate notifications around it; the shape counted at the end of the list, a {0, 0} detail
    // record beside it, and the node registered as the shape's referrer.
    void AddShape(cICompositionTreeNode *outer, cIShape *shape);
    // 0xa570f0: the reverse. The shape must be in the list (the original then reads past both vectors); its detail
    // handle, if it has a key, is given back to the detail object (slot 20).
    void RemoveShape(cICompositionTreeNode *outer, cIShape *shape);
    // 0xa561f0: whether some shape's slot 52 info has its word +0x10 set.
    bool AnyShapeSlot52();
    // 0xa59340 (thiscall, `outer` unused, ret 4): each shape's detail key (its slot 52 info's first word) registered
    // with the detail object (slot 19) where it changed.
    void RefreshDetailKeys(cICompositionTreeNode *outer);
    // 0xa589a0 (thiscall, ret 4): the material block (mBlock) rebuilt from the shapes' materials (the material manager,
    // GetCachedQueriedObject) and transforms; the listeners told (kBlockChangedEvent) unless there was none before and
    // is none now. Layout in the .cpp.
    void RebuildMaterialBlock(cICompositionTreeNode *outer);

    // ---- lane TB: persistence (H2c) ----
    // 0xa580f0 (thiscall, no arguments): the morph lists rebuilt from the shapes (each shape's slot 52 info's word +8
    // morphs, named by its slot 58 pairs through MakeMorphName), each keeping the weight its name had; the names no
    // shape has any more are kept after them, in handle order, with their weights.
    void RebuildMorphs();
    // 0xa56c90: the type record, the renderable Impl's part, the shapes (each added through the node's slot 94), the
    // flags, the morph names rebuilt from the shapes and the weights read when their count matches (skipped otherwise;
    // version 21 on, a name per weight follows, read and dropped), the block's data read in place when its size matches
    // (skipped otherwise), kWeighted set when some weight is not near zero, and (version 20 on) the colour.
    void Read(cICompositionTreeNode *outer, ifaces::cIGZPersistReader *reader);
    // 0xa57240: {"cShapeRefNode", 0x65245517, 21}.
    cPersistTypeInfo *GetTypeInfo(cPersistTypeInfo *out);
    // 0xa57b80: the reverse of Read (the weights only when there are some; the names, never their weights' names).
    void Write(cICompositionTreeNode *outer, ifaces::cIGZPersistWriter *writer);
    // 0xa57d00: the renderable part; the flags; each shape added to the clone (slot 94); with a block, the morph lists,
    // the count and the block's data (into the clone's block, unchecked) copied; the colour.
    void CloneInto(cICompositionTreeNode *source, cICompositionTreeNode *clone, cShapeRefNodeImpl *cloneImpl,
                   uint32_t mode, cINodeCloneContext *context);

    // ---- lane TB: bounds and hits (H2d) ----
    // The node's box is a cuboid (mHalf) about mCentre, both in the node's own frame; the shapes' parts (each shape's
    // slot 52 info, word +0x1c: a cIBound per transform slot, or one) are tested in the block's transforms when the
    // block has some (the first min(info's count, block's count) of them), else the first part in the world frame.
    // 0xa572f0 (thiscall: the transform, the records (an index and a rigid transform each, 8 words), their count; ret
    // 0xc): the renderable Impl's part, flags |= 0x10 and the byte +0xa2 = 10; the block's transforms all set to the
    // first record's and then each in-range record's own; the box grown from the shapes (slot 60, or with block
    // transforms slot 59 with the inverted transform and the block's transforms); an empty box (or no shapes) leaves
    // half extents 0 about the transform's translation.
    void OnContentChanged(const cRigidTransform *transform, const uint32_t *records, uint32_t count);
    // 0xa569e0: the cuboid placed by the cached frame composed with `frame`, its offset the centre through that.
    cBoundBox *GetBoundBoxIn(cBoundBox *out, cICompositionTreeNode *outer, const cTransform34 *frame);
    // 0xa56930: the cuboid placed by the world rotation about the centre carried into the world.
    cBoundBox *GetBoundBox(cBoundBox *out, cICompositionTreeNode *outer);
    // 0xa56230: whether `bound` (placed by `frame`) meets the box and then some part (true on the first).
    bool IntersectsBound(cICompositionTreeNode *outer, nGZCompGeom::cIBound *bound, const AffineTransform3 *frame);
    // 0xa564b0: whether the linear meets the box and then some part (true on the first).
    bool IntersectsLinear(cICompositionTreeNode *outer, nGZCompGeom::cILinear3 *linear);
    // 0xa56720: the nearest part hit nearer than *t (no box test; every part tried), *t lowered to it.
    bool FirstHitLinear(cICompositionTreeNode *outer, float *t, nGZCompGeom::cILinear3 *linear);
    // Not functions of the original (inlined in each): the box's frame (flags 1, the centre carried into the world,
    // the world rotation), and the shapes' parts each with its frame, `visit` returning true to stop.
    void BoxFrame(AffineTransform3 *frame);
    template <class Visit> bool VisitParts(cICompositionTreeNode *outer, Visit &&visit);

    // ---- lane TD: attributes and drawing (H2e) ----
    // The node's own attributes: the block's parameters (float .. float4, by list index `extra`), a morph weight
    // (`extra` the morph) and the colour (four channels 0..1, packed as bytes; set only for `extra` 0). The rest are
    // the transform Impl's.
    // 0xa575a0: a parameter that moves past ~1.2e-10 (or a morph weight, or a colour byte that changes) rereads the
    // detail limit; a morph weight also sets kWeighted unless every weight is near zero.
    void SetAttribute(cICompositionTreeNode *outer, uint32_t id, uint32_t extra, const cAttributeValue *value);
    // 0xa56b00.
    void GetAttribute(cAttributeValue *out, uint32_t id, uint32_t extra);
    // 0xa55830 (returns out, a cArrayAttributeEntryRange): the names of an attribute's entries
    // (sg/cShapeRefAttributeNameIterator.h): per distinct material with parameters of the type, {material name, each
    // parameter's name word}; per shape morph its slot 58 pair; the colour's {"shapeRefGroup", "shapeColor"}; any
    // other attribute the empty range.
    void *MakeIterator51(void *out, uint32_t id);
    // 0xa55f00 (thiscall (out, event), returns out): slot 14's record (cpp).
    nGZEvent::cEventAttribute GetEventAttribute(uint32_t event) const;
    // 0xa555d0: with shapes, the cached frame composed with the viewer's; unless flagged kFlag70 or inside every plane
    // already, the box is classified against the frustum (outside: nothing drawn; the mask narrowed); then the viewer's
    // drawer (slot 25) is given the node, the frame, the box and the mask.
    void Draw(cICompositionTreeNode *outer, cViewerView *viewer, uint32_t planeMask);
    // 0xa55430 (the second argument unused; the node's slot 83, which each class gives its own meaning): every 11th call after a content change the shapes' rest transforms are checked against the
    // block's (nGZCompGeom::RigidNearlyEqual, 0xae79e0) and flag 0x10 set on a mismatch; then each shape's slot 46 is given the flags, the
    // block's material table and the weights (by address), the block's transforms, `sink` and its detail handle.
    void QueueShapeDraws(cICompositionTreeNode *outer, void *sink, void *unused);

    // The shapes, each counted (released front to back when the Impl goes; the node removes them all before that).
    std::vector<cShapeHandle> mShapes;               // +0x70
    std::vector<cShapeDetailRecord> mDetailRecords;  // +0x7c: one per shape
    float mHalf[3] = {};                             // +0x88: the box's half extents (0xa572f0)
    float mCentre[3] = {};                           // +0x94: its centre, in the node's frame
    uint16_t mShapeFlags = 0;                        // +0xa0 (slots 91 / 96; bit 8 is slot 70's, kWeighted Read's)
    uint8_t mByteA2 = 0x0a;                          // +0xa2
    uint8_t mMorphCount = 0;                         // +0xa3: the shapes' morphs (a byte: the sum wraps)
    std::vector<OwnedCell> mMorphNames;              // +0xa4: "shape::morph" pool cells, one per morph
    std::vector<float> mMorphWeights;                // +0xb0: one per morph (Read sizes it to mMorphCount)
    void *mBlock = nullptr;                          // +0xbc: the material block (0xa589a0), malloc'd; free()d first by the destructor
    uint32_t mColour = kNoColour;                    // +0xc0
};

// 0xa57e60 (cdecl, returns out): the morph name "shapeName::morphName" opened in `out` (a pool cell, flag 1).
OwnedCell *MakeMorphName(OwnedCell *out, const char *shapeName, const char *morphName);
// 0xa54930 (cdecl): whether every weight is under 0.001 in magnitude (true for a null list or none).
bool AllNearZero(const float *weights, uint32_t count);

// The behaviour cells of cAbstractBehaviorTypeT<cIShapeRefNode, cRefSingleThreaded>.
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f62dc); // the cell below (game/owned.h)
struct cShapeRefNodeBehaviorTraits {
    static constexpr uint32_t kEventManagerCell = 0x10f62dc; // 0xa55030's cache
    static nGZEvent::cIConnectionManager *&EventManagerCell() { return ts2_rva_10f62dc; }
    // 0xa585a0 (cdecl (id, attribute), hooked; its holder 0xa53800 folded in): the map built on first use, a copy of
    // the transform family's map holding the shape-reference node's ten events; a non-zero id stores `attribute`.
    __declspec(noinline) static nGZEvent::cEventAttributeMap *__cdecl RegistryFor(uint32_t id,
                                                                                  const nGZEvent::cEventAttribute *attribute);
    // The map the slots use (RegistryFor with id 0).
    static nGZEvent::cEventAttributeMap *Registry();
    static void ResetRegistryForTest();
};

class cShapeRefNode : public cRenderableNodeT<cIShapeRefNode, cShapeRefNodeImpl, cShapeRefNodeBehaviorTraits> {
    using Base = cRenderableNodeT<cIShapeRefNode, cShapeRefNodeImpl, cShapeRefNodeBehaviorTraits>;
    using Behavior = nGZEvent::cAbstractBehaviorTypeST<cIShapeRefNode, cShapeRefNodeBehaviorTraits>;

public:
    static constexpr size_t kSize = 0xd0;
    enum : uint32_t { kShapeRefFlag = 4 };

    // (0xa541e0 / 0xa54070): flag 4 set, after the renderable node's.
    cShapeRefNode() { mImpl.SetFlag(kShapeRefFlag, true); }
    // (0xa543d0): every shape removed through RemoveShape (its notifications included), front first, before the
    // renderable level's destructor (the children, then the extensions, then the Impl).
    ~cShapeRefNode() override;

    // 0xa55270 (cdecl; the director's factory, hooked): a new node, uncounted.
    static cShapeRefNode *__cdecl Create();

    // ---- the game's bodies as hookable members (thiscall on the node; lane SP) ----
    // The constructor 0xa541e0 (returns this), the destructor 0xa543d0 (qualified, so non-virtual), the deleting
    // destructor 0xa54880 (slot 100: the sized delete of 0xd0 bytes on bit 0, returns this).
    cShapeRefNode *Construct();
    void Destruct();
    cShapeRefNode *DeletingDestructor(uint32_t flags);
    // The slot bodies cScreenShapeRef's vtables still hold, each the qualified (non-virtual) member.
    bool HookQueryInterface(uint32_t iid, void **out);                                         // 0xa58060
    void HookClone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context);      // 0xa54ec0
    uint32_t HookAccept(cICompositionTreeTraversal *traversal);                                // 0xa58950
    void HookOnEvent(uint32_t type, uint32_t arg1, void *data, uint32_t sender);               // 0xa55370
    bool HookOnQuery(uint32_t type, uint32_t arg1, void *data);                                // 0xa553b0
    cShapeRange HookGetShapes();                                                               // 0xa588e0
    void HookAddShape(cIShape *shape);                                                         // 0xa548f0
    void HookRemoveShape(cIShape *shape);                                                      // 0xa58710
    bool HookAnyShapeSlot52();                                                                 // 0xa553f0
    bool HookHasShape(cIShape *shape);                                                         // 0xa55400
    void HookOnContentChanged(uint32_t transform, uint32_t records, uint32_t count);           // 0xa59330
    cBoundBox HookGetBoundBoxIn(const cTransform34 *frame);                                    // 0xa57f60
    cBoundBox HookGetBoundBox();                                                               // 0xa57f80
    bool HookIntersectsBound(uint32_t bound, uint32_t frame);                                  // 0xa57de0
    bool HookIntersectsLinear(nGZCompGeom::cILinear3 *linear);                                 // 0xa57e00
    bool HookFirstHitLinear(float *t, nGZCompGeom::cILinear3 *linear);                         // 0xa57f30
    // Lane TD (H2e).
    void HookSetAttribute(uint32_t id, uint32_t extra, uint32_t record);                       // 0xa593a0 (7)
    void HookGetAttribute(uint32_t record, uint32_t id, uint32_t extra);                       // 0xa580a0 (10)
    bool HookIsKindOf(uint32_t iid);                                                           // 0xa58930 (25)
    void *HookMakeIterator51(void *out, uint32_t id);                                          // 0xa54970 (51)
    void HookDraw(void *viewer, uint32_t planeMask);                                           // 0xa548d0 (82)
    void HookSlot83(void *a, void *b);                                                         // 0xa548b0 (83)
    // Lane TF (H2f): the behaviour slots.
    void HookNotifyListeners(uint32_t type, uint32_t arg1, void *data);                        // 0xa58850 (5)
    bool HookAskListeners(uint32_t type, uint32_t arg1, void *data);                           // 0xa55230 (6)
    uint32_t HookGetEventCount();                                                              // 0xa57fa0 (12)
    nGZEvent::cAttributeIDRange HookGetEventIds();                                             // 0xa54990 (13)
    nGZEvent::cEventAttribute HookGetEventAttribute(uint32_t event);                           // 0xa54ea0 (14)
    // The face at +8's thunks (`sub ecx, 8`, then the node's Release / QueryInterface), held by cScreenShapeRef's
    // vtable 0xe918a0 too.
    static constexpr int kFace8 = 8;
    static uint32_t __fastcall HookFaceRelease(void *face, void *);                            // 0xa586e5
    static bool __fastcall HookFaceQueryInterface(void *face, void *, uint32_t iid, void **out); // 0xa58092

    // ---- the behaviour ----
    // 3 (0xa55370): the behaviour's (the active bit), then the Impl's.
    void OnEvent(uint32_t type, uint32_t arg1, void *data, uint32_t sender) override;
    // 4 (0xa553b0): the Impl answers first, the listeners otherwise.
    bool OnQuery(uint32_t type, uint32_t arg1, void *data) override;
    // 14 (0xa54ea0): the Impl's (0xa55f00).
    nGZEvent::cEventAttribute GetEventAttribute(uint32_t event) override { return mImpl.GetEventAttribute(event); }

    // 0 (0xa58060): the shape-reference interface counted, else the renderable node's.
    bool QueryInterface(uint32_t iid, void **out) override;
    // 15 (0xa54ec0): as cExplicitBoundNode's, asking the context for the shape-reference interface; the Impl's
    // CloneInto (0xa57d00) does the copying.
    void Clone(uint32_t mode, uint32_t iid, void **out, cINodeCloneContext *context) override;
    // 30 / 50 (0xa58950): the traversal's slot 8.
    uint32_t Accept(cICompositionTreeTraversal *traversal) override { return traversal->TraversalSlot8(this); }
    uint32_t AcceptReadOnly(cICompositionTreeTraversal *traversal) override { return traversal->TraversalSlot8(this); }
    // 43 / 44 (shared 0xa37380 / 0xa37360) and 51 (0xa54970).
    void *MakeIterator43(void *out, uint32_t a, uint32_t b) override;
    void *MakeIterator44(void *out, uint32_t a, uint32_t b) override;
    void *MakeIterator51(void *out, uint32_t a) override { return mImpl.MakeIterator51(out, a); }

    // 60 (0xa59330), 70 (0xa57cf0).
    void OnContentChanged(uint32_t transform, uint32_t records, uint32_t count) override
    {
        mImpl.OnContentChanged(reinterpret_cast<const cRigidTransform *>(uintptr_t(transform)),
                               reinterpret_cast<const uint32_t *>(uintptr_t(records)), count);
    }
    bool IsEverywhere() override { return (mImpl.mShapeFlags & cShapeRefNodeImpl::kFlag70) != 0; }
    // 71-75 go to the Impl through the bounded template (its GetBoundBoxIn / GetBoundBox hide the bounded Impl's).
    bool IntersectsBound(uint32_t bound, uint32_t frame) override // 0xa57de0
    {
        return mImpl.IntersectsBound(this, reinterpret_cast<nGZCompGeom::cIBound *>(uintptr_t(bound)),
                                     reinterpret_cast<const AffineTransform3 *>(uintptr_t(frame)));
    }
    bool IntersectsLinear(nGZCompGeom::cILinear3 *linear) override { return mImpl.IntersectsLinear(this, linear); } // 0xa57e00
    bool FirstHitLinear(float *t, nGZCompGeom::cILinear3 *linear) override { return mImpl.FirstHitLinear(this, t, linear); } // 0xa57f30

    // 82 / 83 (0xa548d0 / 0xa548b0).
    void Draw(void *viewer, uint32_t planeMask) override { mImpl.Draw(this, static_cast<cViewerView *>(viewer), planeMask); }
    void Slot83(void *a, void *b) override { mImpl.QueueShapeDraws(this, a, b); }

    // ---- cIShapeRefNode ----
    uint8_t GetRefKind() override { return 2; }
    uint32_t GetDetailLimit() override { return mImpl.mDetailLimit; }
    uint32_t GetWord6c() override { return mImpl.mZero6c; }
    void SetWord6c(uint32_t value) override { mImpl.mZero6c = value; }
    void SetShapeFlags(uint16_t flags) override { mImpl.mShapeFlags = flags; }
    cShapeRange GetShapesConst() override { return Shapes(); }
    cShapeRange GetShapes() override { return Shapes(); }
    void AddShape(cIShape *shape) override { mImpl.AddShape(this, shape); }
    void RemoveShape(cIShape *shape) override { mImpl.RemoveShape(this, shape); }
    uint16_t GetShapeFlags() override { return mImpl.mShapeFlags; }
    bool AnyShapeSlot52() override { return mImpl.AnyShapeSlot52(); }
    uint32_t ShapeCount() override { return static_cast<uint32_t>(mImpl.mShapes.size()); }
    bool HasShape(cIShape *shape) override;

private:
    cShapeRange Shapes();
};

} // namespace nGZSceneGraph
