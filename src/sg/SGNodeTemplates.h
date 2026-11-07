#pragma once
// The scene graph's node templates (lane NZ, scenegraph N0 step 2): nGZSceneGraph::cObjectGraphNodeT<I, Impl> and
// cCompositionTreeNodeT<I, Impl>, as C++ templates in the standalone shape. A node is
//
//   +0   the primary face I (vtable) and the behaviour's count (cAbstractBehaviorTypeST<I, Traits>, +4)
//   +8   cIGZPersistSerializableReferent (the persist code's face: Read, GetTypeInfo, Write)
//   +0xc the Impl (cObjectGraphNodeImpl or a class derived from it), whose members do the work
//
// Each instantiation in the game is its own copy of the bodies; most are folded across the node family (the
// forwarders to the Impl, the getters), a few are per instantiation (QueryInterface's chain, the behaviour slots). Here
// they are written once; a concrete node class derives from the template it uses and adds Clone (slot 15, which makes
// its own class) and its factory. `Traits` is the behaviour's (gz/cAbstractBehaviorTypeMT.h).
//
// The deleting destructor is the slot after I's last (cAbstractBehaviorTypeST declares it). The destructors run as the
// original's do: the composition template removes its children (RemoveChild, notifications included), the object-graph
// template its extensions, then the Impl goes.
#include "game/owned.h"
#include "game/original.h"
#include "gz/cAbstractBehaviorTypeST.h"
#include "sg/SGNodeInterfaces.h"
#include "compgeom/CompGeom.h"
#include "sg/cBoundedNodeImpl.h"
#include "sg/cCompositionTreeNodeImpl.h"

#include <cstdint>
#include <cstring>

namespace nGZSceneGraph {

// The node family's event registry (0x263f30, game code: a function-local static map built on first use).
nGZEvent::cEventAttributeMap *NodeEventRegistry();

// The behaviour cells of cAbstractBehaviorTypeT<cICompositionTreeNode, cRefSingleThreaded>.
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f5f98); // the cell below (game/owned.h)
struct cCompositionTreeNodeBehaviorTraits {
    static constexpr uint32_t kEventManagerCell = 0x10f5f98; // 0xa37680's cache
    static nGZEvent::cIConnectionManager *&EventManagerCell() { return ts2_rva_10f5f98; }
    static nGZEvent::cEventAttributeMap *Registry() { return NodeEventRegistry(); }

    // 0x263f30 (cdecl; src/sg/cCompositionTreeNode.cpp): the registry itself. A function-local static map built on first
    // use (its holder 0x255860 runs 0x257030: the object-graph events 0x2570a0 stores, then 0x0a884a15 and 0xcc6c9992
    // through this function; the map a copy of the object-graph registry, 0x25f030), a non-zero id stores `attribute`.
    // Not inlined: tests stand in for it.
    __declspec(noinline) static nGZEvent::cEventAttributeMap *__cdecl RegistryFor(uint32_t id,
                                                                                const nGZEvent::cEventAttribute *attribute);
    // What 0x257030 does (the transform registry's holder runs it): the plain form is for our own callers.
    static void RegisterEvents();
    // 0x257030 (thiscall, `this` unused and returned): RegisterEvents for the original's callers (0x257110).
    static void *__fastcall RegisterEventsAdapter(void *self, void *edx);
    static void ResetRegistryForTest(); // tests only
};

namespace node_detail {
constexpr uint32_t kIidReferent = 0x5b0aaa74;  // QueryInterface answers the +8 face, counted
constexpr uint32_t kIidReferent2 = 0xbb11a53e; // the same
constexpr uint32_t kIidBehavior = 0x3b253dc7;  // the node itself, counted
constexpr uint32_t kIidBehaviorType = 0xfb859324; // the node itself, not counted
constexpr uint32_t kIidRawComposition = 0xdb01d4b2; // a composition node answers itself, not counted
constexpr uint32_t kNoLimit = 0x7fffffff;       // slot 55

inline cRigidTransform IdentityTransform()
{
    cRigidTransform t{};
    t.mValues[6] = 1.0f;
    return t;
}
} // namespace node_detail

template <class Iface, class Impl, class Traits>
class cObjectGraphNodeT : public nGZEvent::cAbstractBehaviorTypeST<Iface, Traits>,
                          public cIGZPersistSerializableReferent {
    using Behavior = nGZEvent::cAbstractBehaviorTypeST<Iface, Traits>;

public:
    cObjectGraphNodeT() = default;
    // (0xa36b10 in the composition instantiation): every extension removed through RemoveExtension, then the Impl.
    ~cObjectGraphNodeT() override
    {
        while (mImpl.mFirst) cObjectGraphNodeT::RemoveExtension(mImpl.mFirst);
    }

    // ---- cIGZUnknown (both faces; the +8 face's are adjustor thunks) ----
    // 0x3a5ae0, then the behaviour's 0x148310.
    bool QueryInterface(uint32_t iid, void **out) override
    {
        if (!out) return false;
        if (iid == cIObjectGraphNode::kIid) {
            *out = static_cast<Iface *>(this);
            this->AddRef();
            return true;
        }
        if (iid == node_detail::kIidReferent || iid == node_detail::kIidReferent2) {
            *out = static_cast<cIGZPersistSerializableReferent *>(this);
            this->AddRef();
            return true;
        }
        if (iid == 1 || iid == node_detail::kIidBehavior) {
            *out = static_cast<Iface *>(this);
            this->AddRef();
            return true;
        }
        if (iid == node_detail::kIidBehaviorType) {
            *out = static_cast<Iface *>(this); // not counted (the original returns 0xfb859301: al is 1)
            return true;
        }
        return false;
    }
    uint32_t AddRef() override { return Behavior::AddRef(); }
    uint32_t Release() override { return Behavior::Release(); }

    // ---- cIObjectGraphNode (the forwarders, folded across the family) ----
    cIGZPersistSerializableReferent *GetReferentConst() override { return this; } // shared 0x3a4f40
    cIGZPersistSerializableReferent *GetReferent() override { return this; }      // shared 0x3a4f40
    void SetTagHandle(uint32_t handle) override { mImpl.SetTagHandle(this, handle); }    // shared 0x3a6340
    void SetTag(const char *tag) override { mImpl.SetTag(this, tag); }                   // shared 0x3a6350
    cIExtension *GetFirstExtensionConst() override { return mImpl.mFirst; }              // shared 0x145770
    cIExtension *GetFirstExtension() override { return mImpl.mFirst; }                   // shared 0x145770
    void AddExtension(cIExtension *extension) override { mImpl.AddExtension(this, extension); } // shared 0x3a4680
    void RemoveExtension(cIExtension *extension) override { mImpl.RemoveExtension(extension); } // shared 0x3a5f20
    bool FindExtension(uint32_t iid, void **out, cIExtension *start) override             // shared 0x3a5a30
    {
        return mImpl.FindExtension(iid, out, start);
    }
    bool IsKindOf(uint32_t iid) override { return mImpl.IsKindOf(iid); } // the Impl's (0xa38fc0 for composition)
    const char *GetTag() override { return mImpl.GetTag(); }              // shared 0x3a64b0
    uint32_t GetTagHandle() override { return mImpl.GetTagHandle(); }     // shared 0x3a64a0
    bool HasExtension(cIExtension *extension) override { return mImpl.HasExtension(extension); } // shared 0x3a5740

    // ---- cIGZPersistSerializableReferent (+8) ----
    void Read(ifaces::cIGZPersistReader *reader) override { mImpl.Read(this, reader); }
    cPersistTypeInfo GetTypeInfo() override
    {
        cPersistTypeInfo info;
        mImpl.GetTypeInfo(&info);
        return info;
    }
    void Write(ifaces::cIGZPersistWriter *writer) override { mImpl.Write(this, writer); }

    Impl mImpl; // +0xc
};

template <class Iface, class Impl, class Traits>
class cCompositionTreeNodeT : public cObjectGraphNodeT<Iface, Impl, Traits> {
    using Base = cObjectGraphNodeT<Iface, Impl, Traits>;
    using Behavior = nGZEvent::cAbstractBehaviorTypeST<Iface, Traits>;
    using Node = cICompositionTreeNode;

public:
    cCompositionTreeNodeT() = default;
    // (0xa36a10 in the composition instantiation): every child removed through the Impl (with its notifications).
    ~cCompositionTreeNodeT() override
    {
        while (this->mImpl.mFirstChild) this->mImpl.RemoveChild(this, this->mImpl.mFirstChild);
    }

    // 0xa38c90 (composition): the tree interface counted, 0xdb01d4b2 not counted, else the object-graph node's.
    bool QueryInterface(uint32_t iid, void **out) override
    {
        if (!out) return false;
        if (iid == Node::kIid) {
            *out = static_cast<Iface *>(this);
            this->AddRef();
            return true;
        }
        if (iid == node_detail::kIidRawComposition) {
            *out = static_cast<Iface *>(this);
            return true;
        }
        return Base::QueryInterface(iid, out);
    }

    // ---- the behaviour ----
    // 0xa37cb0: the behaviour's (the active bit), then the Impl's (graph connection changes).
    void OnEvent(uint32_t type, uint32_t arg1, void *data, uint32_t sender) override
    {
        Behavior::OnEvent(type, arg1, data, sender);
        this->mImpl.OnEvent(this, type, arg1, data, sender);
    }
    // 0xa38ed0: the parent hears it first (as an event from this node); then this node's listeners. (The original also
    // calls the Impl's notify hook between the two: shared 0x16a930, which does nothing.)
    void NotifyListeners(uint32_t type, uint32_t arg1, void *data) override
    {
        if (Node *const parent = this->mImpl.mParent)
            parent->OnEvent(type, arg1, data, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(static_cast<Iface *>(this))));
        Behavior::NotifyListeners(type, arg1, data);
    }
    // 0xa378d0: the parent answers first; this node's listeners otherwise.
    bool AskListeners(uint32_t type, uint32_t arg1, void *data) override
    {
        Node *const parent = this->mImpl.mParent;
        if (parent && parent->OnQuery(type, arg1, data)) return true;
        return Behavior::AskListeners(type, arg1, data);
    }

    // ---- cICompositionTreeNode ----
    void SetFlag(uint32_t bit, bool value) override { this->mImpl.SetFlag(bit, value); } // shared 0xa38f50
    uint32_t Accept(cICompositionTreeTraversal *traversal) override { return traversal->Visit(this); } // 0xa38ff0
    uint32_t GetFromParent31() override // shared 0xa38dd0
    {
        Node *const parent = this->mImpl.mParent;
        return parent ? parent->GetFromParent31() : 0;
    }
    uint32_t GetFromParent32() override // shared 0xa38db0
    {
        Node *const parent = this->mImpl.mParent;
        return parent ? parent->GetFromParent32() : 0;
    }
    Node *GetParentConst() override { return this->mImpl.mParent; }           // shared 0x149d00
    Node *GetParent() override { return this->mImpl.mParent; }
    Node *GetPrevSiblingConst() override { return this->mImpl.mPrevSibling; } // shared 0x148ba0
    Node *GetPrevSibling() override { return this->mImpl.mPrevSibling; }
    Node *GetNextSiblingConst() override { return this->mImpl.mNextSibling; } // shared 0x147d00
    Node *GetNextSibling() override { return this->mImpl.mNextSibling; }
    Node *GetFirstChildConst() override { return this->mImpl.mFirstChild; }   // shared 0x15e550
    Node *GetFirstChild() override { return this->mImpl.mFirstChild; }
    cChildRange GetChildrenConst() override { return {this->mImpl.mFirstChild, this->mImpl.mFirstChild, nullptr}; }
    cChildRange GetChildren() override { return {this->mImpl.mFirstChild, this->mImpl.mFirstChild, nullptr}; } // 0xa373a0
    // Slots 43 / 44 / 51 (the iterator builders) are the instantiation's: each concrete class names its Impl's.
    void AddChild(Node *child) override { this->mImpl.AddChild(this, child); }       // shared 0xa37250
    void RemoveChild(Node *child) override { this->mImpl.RemoveChild(this, child); } // shared 0xa38da0
    // shared 0xa38f60: the parent and the previous sibling are raw, the next sibling counted.
    void SetLink(uint32_t kind, Node *node) override
    {
        switch (kind) {
        case Node::kParentLink: this->mImpl.mParent = node; break;
        case Node::kPrevLink: this->mImpl.mPrevSibling = node; break;
        case Node::kNextLink: {
            Node *const old = this->mImpl.mNextSibling;
            if (node == old) break;
            if (node) node->AddRef();
            this->mImpl.mNextSibling = node;
            if (old) old->Release();
            break;
        }
        default: break;
        }
    }
    bool TestFlag(uint32_t bit) override { return (this->mImpl.mFlags & (1u << (bit & 31))) != 0; } // shared 0xa38fd0
    uint32_t GetFlags() override { return this->mImpl.mFlags; }                                       // shared 0x15e970
    uint32_t AcceptReadOnly(cICompositionTreeTraversal *traversal) override { return traversal->Visit(this); }
    bool HasChildren() override { return this->mImpl.mFirstChild != nullptr; } // shared 0xa37cf0
    cRigidTransform GetLocalTransform() override { return node_detail::IdentityTransform(); } // shared 0x2676f0
    cRigidTransform GetWorldTransform() override // shared 0xa37b80
    {
        Node *const parent = this->mImpl.mParent;
        return parent ? parent->GetWorldTransform() : node_detail::IdentityTransform();
    }
    uint32_t GetLimit55() override { return node_detail::kNoLimit; } // shared 0x141210
    void ToParent34(cTransform34 *out, const cTransform34 *in) override { *out = *in; }         // shared 0x263060
    void ToParent1c(cRigidTransform *out, const cRigidTransform *in) override { *out = *in; }   // shared 0x263090
    void FromParent34(cTransform34 *out, const cTransform34 *in) override { *out = *in; }       // shared 0x263060
    void FromParent1c(cRigidTransform *out, const cRigidTransform *in) override { *out = *in; } // shared 0x263090
    void OnContentChanged(uint32_t, uint32_t, uint32_t) override // shared 0xa39000
    {
        this->mImpl.SetFlag(Node::kReadFlag, false);
    }

    // ---- cIGZPersistSerializableReferent (+8) ----
    // shared 0x2648f0: the parent's referent answers (the root's class decides); 0 without a parent.
    uint32_t GetOwnerInfo() override
    {
        Node *const parent = this->mImpl.mParent;
        return parent ? parent->GetReferent()->GetOwnerInfo() : 0;
    }
};

// ---- the transform family (lane OB, scenegraph N0 part 3) ----
// cTransformNodeT, cBoundedNodeT and cExplicitBoundNodeT each add one level to the composition template, as the game's
// instantiations do (each its own copy of the bodies; the forwarders to the Impl are folded across the family). `Impl` is
// the matching Impl class or one derived from it (cTransformNodeImpl, cBoundedNodeImpl, cExplicitBoundNodeImpl).

// The event registry the transform family shares (0x2640f0, game code: a function-local static map built on first use).
nGZEvent::cEventAttributeMap *TransformNodeEventRegistry();

// The behaviour cells of cAbstractBehaviorTypeT<cITransformNode, cRefSingleThreaded> and of the explicit-bound node's.
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f5ffc); // the cell below (game/owned.h)
struct cTransformNodeBehaviorTraits {
    static constexpr uint32_t kEventManagerCell = 0x10f5ffc; // 0xa3a4c0's cache
    static nGZEvent::cIConnectionManager *&EventManagerCell() { return ts2_rva_10f5ffc; }
    static nGZEvent::cEventAttributeMap *Registry() { return TransformNodeEventRegistry(); }

    // 0x2640f0 (cdecl; src/sg/cTransformNode.cpp): the registry itself, shared by the transform family. Built on first use
    // (its holder 0x2559c0 runs 0x257110: 0x257030's events, then 0xc9de8b96, 0x9b59fb00, 0xfb5b3383 and 0x9b5b3506 through
    // this function; the map a copy of the node registry, 0x25efe0); a non-zero id stores `attribute`. Not inlined.
    __declspec(noinline) static nGZEvent::cEventAttributeMap *__cdecl RegistryFor(uint32_t id,
                                                                                const nGZEvent::cEventAttribute *attribute);
    // What 0x257110 does (the holder 0x2559c0 runs it, as does the bounded registry's holder 0xa606d0): the node
    // registry's events, then the four bone events stored through RegistryFor. The plain form is for our own callers.
    static void RegisterEvents();
    // 0x257110 (thiscall, `this` unused and returned): RegisterEvents for the original's callers (0xa606d0).
    static void *__fastcall RegisterEventsAdapter(void *self, void *edx);
    static void ResetRegistryForTest(); // tests only
};
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f616c); // the cell below (game/owned.h)
struct cExplicitBoundNodeBehaviorTraits {
    static constexpr uint32_t kEventManagerCell = 0x10f616c; // 0xa4ba30's cache
    static nGZEvent::cIConnectionManager *&EventManagerCell() { return ts2_rva_10f616c; }
    static nGZEvent::cEventAttributeMap *Registry() { return TransformNodeEventRegistry(); }
};
TS2_OWNED_STATIC(nGZEvent::cIConnectionManager *, 10f624c); // the cell below (game/owned.h)
struct cLooseOctreePartitionNodeBehaviorTraits {
    static constexpr uint32_t kEventManagerCell = 0x10f624c; // 0xa516b0's cache
    static nGZEvent::cIConnectionManager *&EventManagerCell() { return ts2_rva_10f624c; }
    static nGZEvent::cEventAttributeMap *Registry() { return TransformNodeEventRegistry(); }
};

// A transform node: a rigid transform and a limit; the composition node's slots 53-59 answer from them.
template <class Iface, class Impl, class Traits>
class cTransformNodeT : public cCompositionTreeNodeT<Iface, Impl, Traits> {
    using Base = cCompositionTreeNodeT<Iface, Impl, Traits>;

public:
    // (0xa39fa0): flag 0 set.
    cTransformNodeT() { this->mImpl.SetFlag(0, true); }

    // 0xa3aee0: the transform interface counted, else the composition node's.
    bool QueryInterface(uint32_t iid, void **out) override
    {
        if (out && iid == cITransformNode::kIid) {
            *out = static_cast<Iface *>(this);
            this->AddRef();
            return true;
        }
        return Base::QueryInterface(iid, out);
    }

    // 7 / 10 (0xa3b250 / 0xa3af20): the attributes.
    void SetAttribute(uint32_t id, uint32_t extra, uint32_t record) override
    {
        this->mImpl.SetAttribute(this, id, extra, reinterpret_cast<const cAttributeValue *>(static_cast<uintptr_t>(record)));
    }
    void GetAttribute(uint32_t record, uint32_t id, uint32_t extra) override
    {
        this->mImpl.GetAttribute(reinterpret_cast<cAttributeValue *>(static_cast<uintptr_t>(record)), id, extra);
    }

    // 30 / 50 (shared 0xa3b240): the traversal's slot 5.
    uint32_t Accept(cICompositionTreeTraversal *traversal) override { return traversal->VisitTransform(this); }
    uint32_t AcceptReadOnly(cICompositionTreeTraversal *traversal) override { return traversal->VisitTransform(this); }

    cRigidTransform GetLocalTransform() override { return this->mImpl.mTransform; } // shared 0xa3b220
    cRigidTransform GetWorldTransform() override // shared 0xa3a730
    {
        cRigidTransform out;
        this->mImpl.GetWorldTransform(&out);
        return out;
    }
    uint32_t GetLimit55() override { return this->mImpl.mLimit; } // shared 0x15f390
    void ToParent34(cTransform34 *out, const cTransform34 *in) override { this->mImpl.ToParent34(out, in); }     // 0xa3aec0
    void ToParent1c(cRigidTransform *out, const cRigidTransform *in) override { this->mImpl.ToParent1c(out, in); } // 0xa3aed0
    void FromParent34(cTransform34 *out, const cTransform34 *in) override { this->mImpl.FromParent34(out, in); }   // 0xa3aea0
    void FromParent1c(cRigidTransform *out, const cRigidTransform *in) override { this->mImpl.FromParent1c(out, in); } // 0xa3aeb0

    // 61-65 (shared 0xa3b130, 0xa3b190, 0xa3b180, 0xa3b1d0, 0xa3b140): all end in the Impl's SetLimit / SetTransform.
    void SetLimit(uint32_t limit) override { this->mImpl.SetLimit(this, limit); }
    void SetTransformParts(const float *translation, const float *rotation) override
    {
        cRigidTransform transform;
        std::memcpy(transform.Translation(), translation, 3 * sizeof(float));
        std::memcpy(transform.Rotation(), rotation, 4 * sizeof(float));
        this->mImpl.SetTransform(this, &transform);
    }
    void SetTransform(const cRigidTransform *transform) override { this->mImpl.SetTransform(this, transform); }
    void SetTranslation(const float *translation) override
    {
        cRigidTransform transform;
        std::memcpy(transform.Translation(), translation, 3 * sizeof(float));
        std::memcpy(transform.Rotation(), this->mImpl.mTransform.Rotation(), 4 * sizeof(float));
        this->mImpl.SetTransform(this, &transform);
    }
    void SetRotation(const float *rotation) override
    {
        cRigidTransform transform;
        std::memcpy(transform.Translation(), this->mImpl.mTransform.Translation(), 3 * sizeof(float));
        std::memcpy(transform.Rotation(), rotation, 4 * sizeof(float));
        this->mImpl.SetTransform(this, &transform);
    }
};

// A bounded node: the cached world transform (the Impl's, kept current by the tree code) answers slot 54, and the
// bound queries (66-76) work from it. Without a shape the box is the cached position and nothing intersects.
template <class Iface, class Impl, class Traits>
class cBoundedNodeT : public cTransformNodeT<Iface, Impl, Traits> {
    using Base = cTransformNodeT<Iface, Impl, Traits>;

public:
    // (0xa4b350): flags 1 (bounded) and 11 (the bound flag) set, after the transform node's.
    cBoundedNodeT()
    {
        this->mImpl.SetFlag(cIBoundedNode::kBoundedFlag, true);
        this->mImpl.SetFlag(cIBoundedNode::kBoundFlag, true);
    }

    // 0xa4c310: the bounded interface counted, else the transform node's.
    bool QueryInterface(uint32_t iid, void **out) override
    {
        if (out && iid == cIBoundedNode::kIid) {
            *out = static_cast<Iface *>(this);
            this->AddRef();
            return true;
        }
        return Base::QueryInterface(iid, out);
    }

    // 54 (shared 0xa4bca0): the cache, brought up to date first when flag 12 says it is stale.
    cRigidTransform GetWorldTransform() override
    {
        this->mImpl.RefreshIfStale(this);
        return this->mImpl.mWorld;
    }
    // 60 (shared 0xa4c680): the read flag cleared (as the composition node's), then the cache set from `transform`.
    void OnContentChanged(uint32_t transform, uint32_t, uint32_t) override
    {
        this->mImpl.SetFlag(cICompositionTreeNode::kReadFlag, false);
        this->mImpl.mWorld = *reinterpret_cast<const cRigidTransform *>(static_cast<uintptr_t>(transform));
    }

    void SetBoundFlag(bool value) override { this->mImpl.SetBoundFlag(this, value); }          // shared 0xa4c610
    void SetHolder(void *holder, uint32_t data) override                                         // shared 0x332750
    {
        this->mImpl.mHolder = holder;
        this->mImpl.mHolderData = data;
    }
    void *GetHolderConst() override { return this->mImpl.mHolder; } // shared 0x145520
    void *GetHolder() override { return this->mImpl.mHolder; }
    bool IsEverywhere() override { return false; } // shared 0x147480
    cBoundBox GetBoundBoxIn(const cTransform34 *frame) override
    {
        cBoundBox out;
        this->mImpl.GetBoundBoxIn(&out, this, frame);
        return out;
    }
    cBoundBox GetBoundBox() override
    {
        cBoundBox out;
        this->mImpl.GetBoundBox(&out, this);
        return out;
    }
    bool IntersectsBound(uint32_t, uint32_t) override { return false; }
    bool IntersectsLinear(nGZCompGeom::cILinear3 *) override { return false; }
    bool FirstHitLinear(float *, nGZCompGeom::cILinear3 *) override { return false; }
    uint32_t GetHolderData() override { return this->mImpl.mHolderData; } // shared 0x169f60
};

// A partition node (lane OI, scenegraph N1 step 1): a bounded node that holds other bounded nodes for spatial queries
// (cIPartitionNode, slots 77-87; the concrete class does the holding). The level adds flag 6 (set by the constructor; the
// bounded Impl's Read then leaves the bound flag alone), the partition interface and the traversal's slot 13. cSceneNode
// and cLooseOctreePartitionNode instantiate it; the bodies below are shared by the two (folded in the game).
template <class Iface, class Impl, class Traits>
class cPartitionNodeT : public cBoundedNodeT<Iface, Impl, Traits> {
    using Base = cBoundedNodeT<Iface, Impl, Traits>;

public:
    // (0xa50dc0 in the octree's instantiation): flag 6 set, after the bounded node's flags.
    cPartitionNodeT() { this->mImpl.SetFlag(cBoundedNodeImpl::kReadSkipsBoundFlag, true); }

    // shared 0xa4ef20: the partition interface counted, else the bounded node's.
    bool QueryInterface(uint32_t iid, void **out) override
    {
        if (!out) return false;
        if (iid == cIPartitionNode::kIid) {
            *out = static_cast<Iface *>(this);
            this->AddRef();
            return true;
        }
        return Base::QueryInterface(iid, out);
    }

    // 30 / 50 (shared 0xa4f700): the traversal's slot 13.
    uint32_t Accept(cICompositionTreeTraversal *traversal) override { return traversal->VisitPartition(this); }
    uint32_t AcceptReadOnly(cICompositionTreeTraversal *traversal) override { return traversal->VisitPartition(this); }

    // 80 (shared 0xa4ee60): RemovePartition (through the vtable), then false.
    bool RemovePartitionFalse(cIPartitionNode *partition) override
    {
        this->RemovePartition(partition);
        return false;
    }
};

// An explicit-bound node: the bound is a shape (an nGZCompGeom bound) the node keeps a copy of, placed by the cached
// world transform. Slots 71-75 hand the shape the frame of that transform; slot 77 replaces the shape.
template <class Iface, class Impl, class Traits>
class cExplicitBoundNodeT : public cBoundedNodeT<Iface, Impl, Traits> {
    using Base = cBoundedNodeT<Iface, Impl, Traits>;

    nGZCompGeom::cIBound *Shape() { return static_cast<nGZCompGeom::cIBound *>(this->mImpl.mShape); }

public:
    cExplicitBoundNodeT() { this->mImpl.SetFlag(9, true); } // (0xa4b3d0): flag 9 set

    // 0xa4c350: the explicit-bound interface counted, else the bounded node's.
    bool QueryInterface(uint32_t iid, void **out) override
    {
        if (out && iid == cIExplicitBoundNode::kIid) {
            *out = static_cast<Iface *>(this);
            this->AddRef();
            return true;
        }
        return Base::QueryInterface(iid, out);
    }

    // 30 / 50 (0xa4c650): the traversal's slot 15.
    uint32_t Accept(cICompositionTreeTraversal *traversal) override { return traversal->VisitExplicitBound(this); }
    uint32_t AcceptReadOnly(cICompositionTreeTraversal *traversal) override { return traversal->VisitExplicitBound(this); }

    // 71 (0xa4c160): the shape's box in the frame of the cached transform composed with `frame` (slot 37 of the shape);
    // without a shape, the composed frame's position twice.
    cBoundBox GetBoundBoxIn(const cTransform34 *frame) override
    {
        cBoundBox out;
        this->mImpl.RefreshIfStale(this);
        AffineTransform3 mine;
        this->mImpl.CachedFrame(&mine);
        const AffineTransform3 composed = *game_code::ComposeFrames(&mine, reinterpret_cast<const AffineTransform3 *>(frame));
        if (nGZCompGeom::cIBound *const shape = Shape()) {
            shape->Slot37(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&out)),
                          static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&composed)));
            return out;
        }
        std::memcpy(out.mMin, composed.mOffset, sizeof out.mMin);
        std::memcpy(out.mMax, composed.mOffset, sizeof out.mMax);
        return out;
    }
    // 72 (0xa4c270): the shape's box (slot 38 of the shape, given a copy of the cached transform); without a shape, the
    // cached position twice.
    cBoundBox GetBoundBox() override
    {
        cBoundBox out;
        this->mImpl.RefreshIfStale(this);
        cRigidTransform cached = this->mImpl.mWorld;
        if (nGZCompGeom::cIBound *const shape = Shape()) {
            shape->Slot38(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&out)),
                          static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&cached)));
            return out;
        }
        std::memcpy(out.mMin, cached.Translation(), sizeof out.mMin);
        std::memcpy(out.mMax, cached.Translation(), sizeof out.mMax);
        return out;
    }
    // 73 (0xa4bf50): slot 20 of the shape, given the frame of the cached transform; false without a shape.
    bool IntersectsBound(uint32_t a, uint32_t b) override
    {
        nGZCompGeom::cIBound *const shape = Shape();
        if (!shape) return false;
        this->mImpl.RefreshIfStale(this);
        AffineTransform3 frame;
        this->mImpl.CachedFrame(&frame);
        return static_cast<uint8_t>(shape->Slot20(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&frame)), a, b)) != 0;
    }
    // 74 (0xa4bfe0): the line, ray or segment meets the shape placed by the cached transform; false without a shape.
    bool IntersectsLinear(nGZCompGeom::cILinear3 *linear) override
    {
        nGZCompGeom::cIBound *const shape = Shape();
        if (!shape) return false;
        this->mImpl.RefreshIfStale(this);
        AffineTransform3 frame;
        this->mImpl.CachedFrame(&frame);
        return linear->IntersectsBoundIn(shape, &frame);
    }
    // 75 (0xa4c070): the same, with the parameter of the first hit; *t only goes down.
    bool FirstHitLinear(float *t, nGZCompGeom::cILinear3 *linear) override
    {
        nGZCompGeom::cIBound *const shape = Shape();
        if (!shape) return false;
        this->mImpl.RefreshIfStale(this);
        AffineTransform3 frame;
        this->mImpl.CachedFrame(&frame);
        float hit;
        if (linear->FirstHitBoundIn(&hit, shape, &frame) && hit < *t) {
            *t = hit;
            return true;
        }
        return false;
    }

    // 77 (0xa4c5b0): the old shape released, a copy of `shape` (as an nGZCompGeom bound) kept; the tree code is told.
    void SetShape(nGZCompGeom::cIShape *shape) override
    {
        if (cIGZUnknown *const old = this->mImpl.mShape) {
            this->mImpl.mShape = nullptr;
            old->Release();
        }
        if (shape) shape->Clone(kBoundIid, reinterpret_cast<void **>(&this->mImpl.mShape));
        NodeTransformChanged(this);
    }
    nGZCompGeom::cIBound *GetShape() override { return Shape(); } // shared 0x255730

private:
    static constexpr uint32_t kBoundIid = 0x5ce57549; // nGZCompGeom::cIBound
};

} // namespace nGZSceneGraph
