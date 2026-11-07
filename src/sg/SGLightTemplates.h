#pragma once
// The light templates (lane OR, scenegraph N2): nGZSceneGraph::cReferentNodeT<I> (an object-graph node on a
// cReferentNodeImpl: the lights and cShape) and cLightT<I> (a light: a cSGResource with a referent node beside it), as
// C++ templates in the standalone shape. A light is
//
//   class cLightT<I> : public cSGResource, public cReferentNodeT<I>
//
//   +0x00 the cSGResource part (primary vtable: QueryInterface, AddRef / Release (the node's), the key slots 3-10,
//         GetRefCount 11 (the node's count), the deleting destructor 12; cStandardLightBase adds slot 13)
//   +0x1c the part's vbptr, +0x20 name, +0x24 segment, +0x28 / +0x29 flags
//   +0x2c the node: +0x2c its face I (vtable, cIStandardLight's 59 slots for the standard lights), +0x30 its count,
//         +0x34 its persist face, +0x38 the cReferentNodeImpl (0x14 bytes)
//   +0x4c the derived class's fields; the vtordisp and the virtual base cISGResource last (+0x68 / +0x6c in cAmbientLight)
//
// Every face's QueryInterface / AddRef / Release is the one final overrider here (the node's count is the light's count:
// the cSGResource part's own count at +4 stays 0); Read / GetTypeInfo / Write / GetOwnerInfo override both the node's
// persist face (+0x34) and the virtual base's slots 3-6, as the game's virtual-base thunks jump to the +0x34 bodies.
// The game folds most of these bodies across the lights (and cShape for the referent template); they are written once
// here and each concrete light derives from the template it uses.
#include "game/original.h"
#include "ifaces/cIGZPersistReader.h"
#include "ifaces/cIGZPersistWriter.h"
#include "sg/SGLightInterfaces.h"
#include "sg/SGNodeTemplates.h"
#include "sg/cReferentNodeImpl.h"
#include "sg/cSGResource.h"

#include <cstdint>

namespace nGZSceneGraph {

// The referent node: the object-graph node with the referrers (slots 29-31), whose notifications and queries reach
// them before the node's own listeners.
template <class Iface, class Traits>
class cReferentNodeT : public cObjectGraphNodeT<Iface, cReferentNodeImpl, Traits> {
    using Base = cObjectGraphNodeT<Iface, cReferentNodeImpl, Traits>;
    using Behavior = nGZEvent::cAbstractBehaviorTypeST<Iface, Traits>;

public:
    cReferentNodeT() = default;
    ~cReferentNodeT() override = default; // (0xa465b0 for cIStandardLight): the object-graph node's

    // 0x3a5b50 (cIStandardLight): cIReferentNode is this node, counted; else the object-graph node's (0x3a5ae0).
    bool QueryInterface(uint32_t iid, void **out) override
    {
        if (!out) return false;
        if (iid == cIReferentNode::kIid) {
            *out = static_cast<Iface *>(this);
            this->AddRef();
            return true;
        }
        return Base::QueryInterface(iid, out);
    }

    // ---- the behaviour ----
    // 0x3c9930: the referrers hear it first (the Impl's Notify, this node the sender), then this node's listeners.
    void NotifyListeners(uint32_t type, uint32_t arg1, void *data) override
    {
        this->mImpl.Notify(this, type, arg1, data);
        Behavior::NotifyListeners(type, arg1, data);
    }
    // 0x3c87b0: the referrers are asked first (the Impl's Ask, which answers false whatever they say), then this node's
    // listeners.
    bool AskListeners(uint32_t type, uint32_t arg1, void *data) override
    {
        if (this->mImpl.Ask(type, arg1, data)) return true;
        return Behavior::AskListeners(type, arg1, data);
    }

    // ---- cIReferentNode ----
    void AddReferrer(nGZEvent::cIAbstractBehavior *referrer) override { this->mImpl.AddReferrer(referrer); } // 0x3a46e0
    void RemoveReferrer(nGZEvent::cIAbstractBehavior *referrer) override                                      // 0x3a5f30
    {
        this->mImpl.RemoveReferrer(referrer);
    }
    uint32_t GetReferrerCount() override { return this->mImpl.GetReferrerCount(); } // shared 0x3a5a20
};

// The light: a scene-graph resource whose node is a referent node. Slots 32 / 33 hand out the resource face; the
// virtual base's record 18 is "_lght".
template <class Iface, class Traits>
class cLightT : public cSGResource, public cReferentNodeT<Iface, Traits> {
    using Node = cReferentNodeT<Iface, Traits>;

public:
    static constexpr uint32_t kVersion = 1; // cLightT's type-info record

    // (0xa45a40): the cSGResource part, then the node. User-provided, not defaulted, so that the explicit instantiations
    // in cLightLevels.cpp emit it (a defaulted constructor is only defined where it is used) for the hook's C name.
    cLightT() {}
    ~cLightT() override = default; // (0xa463d0): the node (its extensions removed), then the cSGResource part

    // ---- cIGZUnknown (every face) ----
    // 0x3a5a60: cILight and cIReferentNode are the node face, counted; then the referent node's answers; then the
    // cSGResource part's (0xa3c4d0).
    bool QueryInterface(uint32_t iid, void **out) override
    {
        if (!out) return false;
        if (iid == cILight::kIid || iid == cIReferentNode::kIid) {
            *out = static_cast<Iface *>(this);
            this->AddRef();
            return true;
        }
        if (Node::QueryInterface(iid, out)) return true;
        return cSGResource::QueryInterface(iid, out);
    }
    uint32_t AddRef() override { return Node::AddRef(); }   // (the primary's 0x3a46c0: the node's)
    uint32_t Release() override { return Node::Release(); } // 0x3c9030: the node's; the last one deletes

    // ---- cIObjectGraphNode ----
    // 0x3a6460: cILight, else the Impl's (0x3a6490 -> 0xa448d0).
    bool IsKindOf(uint32_t iid) override { return iid == cILight::kIid || this->mImpl.IsKindOf(iid); }

    // ---- cIReferentNode ----
    cISGResource *GetResourceConst() override { return static_cast<cISGResource *>(this); } // shared 0x3a4f50
    cISGResource *GetResource() override { return static_cast<cISGResource *>(this); }

    // ---- cIGZPersistSerializableReferent (+0x34, and the virtual base's 3-6) ----
    // 0x3a5c30 (ret 4): the record (read and dropped last), the cSGResource part (0xa3c540), the Impl (0x3a5cc0 ->
    // 0xa447e0).
    void Read(ifaces::cIGZPersistReader *reader) override
    {
        cPersistTypeInfo info;
        reader->ReadTypeInfo(&info);
        cSGResource::Read(reader);
        this->mImpl.Read(this, reader);
        if (info.mName) info.mName->Release();
    }
    // 0xa4a610: {"cLightT", 0, 1}.
    cPersistTypeInfo GetTypeInfo() override
    {
        cPersistTypeInfo info;
        info.mName = NewTypeName("cLightT");
        info.mWord4 = 0;
        info.mVersion = kVersion;
        return info;
    }
    // 0xa4ad50 (ret 4): the record (0xa4a610 directly; its name released after it is written), the cSGResource part
    // (0xa3ced0), the Impl (0x3a6680 -> 0xa449a0).
    void Write(ifaces::cIGZPersistWriter *writer) override
    {
        cPersistTypeInfo info = cLightT::GetTypeInfo();
        writer->WriteTypeInfo(&info);
        if (info.mName) info.mName->Release();
        cSGResource::WriteResource(writer);
        this->mImpl.Write(this, writer);
    }
    // shared 0x3a5fb0 -> 0xa3c750: the cSGResource part's (the virtual base).
    uint32_t GetOwnerInfo() override { return cSGResource::GetOwnerInfo(); }

    // ---- cIGZPersistResource (the primary) ----
    // shared 0x3a56b0 -> 0x3a56a0: the node's count.
    uint32_t GetRefCount() override { return this->Node::mRefCount & nGZEvent::behavior_mt::kCountMask; }

    // ---- cISGResource (the virtual base) ----
    const void *GetRecord18() override { return "_lght"; } // shared 0x3a59c0
};

} // namespace nGZSceneGraph
