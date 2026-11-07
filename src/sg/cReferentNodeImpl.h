#pragma once
// nGZSceneGraph::cReferentNodeImpl (lane OR, scenegraph N2): the Impl of the referent nodes (cReferentNodeT: the lights
// and cShape), an object-graph Impl plus the list of referrers (a std::vector of behaviours, +8..+0x10). A referrer hears
// what the node notifies (its slot 3, the node as the sender) and is asked what the node asks (its slot 4).
//
// A plain class, its members thiscall on the Impl, hooked at their RVAs as cObjectGraphNodeImpl's are: the referent
// template's forwarders (folded across the lights and cShape, never hooked) reach them there, and our light classes
// call them directly. Both CRTs use the process heap, so a vector one side grew the other may free.
#include "sg/cObjectGraphNodeImpl.h"

#include <cstdint>
#include <vector>

namespace nGZSceneGraph {

class cReferentNodeImpl : public cObjectGraphNodeImpl {
public:
    static constexpr uint32_t kIid = 0x9c6ab984; // cIReferentNode: IsKindOf's own answer
    static constexpr uint32_t kVersion = 1;      // what Write records

    cReferentNodeImpl() = default; // 0xa446b0: the object-graph Impl (no extensions, no tag), no referrers
    ~cReferentNodeImpl();          // 0xa44710: the list freed, then the object-graph Impl's destructor
    cReferentNodeImpl(const cReferentNodeImpl &) = delete;
    cReferentNodeImpl &operator=(const cReferentNodeImpl &) = delete;
    // The constructor and destructor as hookable members (0xa446b0 returns this; 0xa44710).
    cReferentNodeImpl *Construct();
    void Destruct();

    void AddReferrer(nGZEvent::cIAbstractBehavior *referrer);    // 0xa44770: at the end
    // 0xa44850: the first entry equal to `referrer` removed. The original assumes it is there: a referrer not in the
    // list makes it move (size_t)-4 bytes (a crash), and ours does what std::vector::erase(end()) does, the same.
    void RemoveReferrer(nGZEvent::cIAbstractBehavior *referrer);
    uint32_t GetReferrerCount() const { return static_cast<uint32_t>(mReferrers.size()); } // (slot 31, shared 0x3a5a20)
    // 0xa447a0 (ret 0xc): each referrer's slot 4 (OnQuery) asked in turn; returns false whatever they answer.
    bool Ask(uint32_t type, uint32_t arg1, void *data);
    // 0xa44880 (ret 0x10): each referrer's slot 3 (OnEvent) with `outer` as the sender. (The original then calls the
    // Impl's notify hook, shared 0x16a930, which does nothing.)
    void Notify(cIObjectGraphNode *outer, uint32_t type, uint32_t arg1, void *data);
    // 0xa447e0: the type-info record (read and dropped), then the object-graph Impl's Read.
    void Read(cIObjectGraphNode *outer, ifaces::cIGZPersistReader *reader);
    // 0xa449a0: the type-info record ("cReferentNode", 0, 1), then the object-graph Impl's Write.
    void Write(cIObjectGraphNode *outer, ifaces::cIGZPersistWriter *writer);
    bool IsKindOf(uint32_t iid);                           // 0xa448d0: cIReferentNode, else the object-graph Impl's
    cPersistTypeInfo *GetTypeInfo(cPersistTypeInfo *out); // 0xa448f0

    std::vector<nGZEvent::cIAbstractBehavior *> mReferrers; // +8 first, +0xc last, +0x10 end
};
static_assert(sizeof(cReferentNodeImpl) == 0x14, "cReferentNodeImpl size");

} // namespace nGZSceneGraph
