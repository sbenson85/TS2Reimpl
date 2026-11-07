#pragma once
// nGZSceneGraph::cObjectGraphNodeImpl (lane NZ, scenegraph N0 step 1): the state every scene-graph node keeps at +0xc
// (the node templates' member) and the work behind the cIObjectGraphNode slots: a list of extensions (the first held
// counted; each extension holds the next) and a tag (a string-pool handle, 0x10f9070's pool; -1 when there is none).
// A plain class, its members thiscall on the Impl; `outer` is the node that holds it, whose own slots the members call
// (so a subclass's overrides take part). Hooked at its RVAs: the node templates' forwarders (folded across the node
// family, never hooked) reach it there.
//
// Nodes written before version 4 kept their tag in a cITagExtension (interface 0x9a80964e). Reading one, asking for the
// tag or setting it moves the tag into the handle and drops that extension ("the legacy tag" below).
//
// Also here: the node notifications (free __stdcall helpers the tree code calls around a change to a node's list of
// subordinates) and their two event bodies.
#include "rz/StaticGetters2.h" // ServiceCell
#include "gz/cGZEventReflector.h" // cEventAttributeMap
#include "sg/SGNodeInterfaces.h"

#include <cstdint>

namespace nGZSceneGraph {

// The object-graph node behaviour's event registry (cAbstractBehaviorTypeT<cIObjectGraphNode>; the parent of the node,
// geometry and shape registries): the base registry plus the connection-changed (0x7b54c99c) and subordinates-changed
// (0x1d05597b) events.
struct cObjectGraphBehaviorTraits {
    // 0x264010 (cdecl): built on first use (its holder 0x255910 runs 0x2570a0: the base events, then the two events stored
    // here; the map is a copy of the base registry 0x1480d0); a non-zero id stores `attribute` under it. 0x25f030 is this
    // with id 0. Our own map (the original's is the game global 0x10cc9f4). Not inlined: tests stand in for it.
    __declspec(noinline) static nGZEvent::cEventAttributeMap *__cdecl RegistryFor(uint32_t id,
                                                                                const nGZEvent::cEventAttribute *attribute);
    static void ResetRegistryForTest(); // tests only: the next use builds the registry again
    // What 0x2570a0 does (the first step of every derived registry's holder): the base events, then the connection and
    // subordinates events stored through RegistryFor. The plain form is for our own callers.
    static void RegisterEvents();
    // 0x2570a0 (thiscall, `this` unused and returned): RegisterEvents for the original's callers (0x257030).
    static void *__fastcall RegisterEventsAdapter(void *self, void *edx);
};

// cSubordinateListChanged (game vtable 0xe68768, 0xc bytes): what a node tells itself (its slot 3, event 0x1d05597b)
// around a change to one of its lists. Kinds: 0 adding, 1 removing, 2 added, 3 removed; the list is named by the
// interface id of what it holds (0x4512dd13: the children).
class cSubordinateListChanged {
public:
    static constexpr uint32_t kEventId = 0x1d05597b;
    static constexpr uint32_t kType = 0x9d054e5e; // slot 3 (0xa34fc0)
    enum Kind : uint32_t { kAdding = 0, kRemoving = 1, kAdded = 2, kRemoved = 3 };
    cSubordinateListChanged(uint32_t kind, uint32_t iid) : mKind(kind), mIid(iid) {}
    virtual ~cSubordinateListChanged() = default;                     // 0 (shared 0x291930)
    virtual uint32_t Assign(const cSubordinateListChanged *other);    // 1 (0xa342f0): returns the copied iid
    virtual uint32_t AssignAlias(const cSubordinateListChanged *other); // 2 (the same body)
    virtual uint32_t GetEventType() const;                            // 3 (0xa34fc0)
    virtual uint32_t GetSize() const;                                 // 4 (shared 0x2919e0: 0xc)
    virtual uint32_t GetBodySize() const;                             // 5 (shared 0x1559c0: 8)
    virtual void CloneInto(void *memory) const;                       // 6 (0xa34410)

    uint32_t mKind; // +4
    uint32_t mIid;  // +8
};
static_assert(sizeof(cSubordinateListChanged) == 0xc, "cSubordinateListChanged size");

// cGraphConnChanged (game vtable 0xdb1f98, 0x14 bytes): what a node tells a subordinate's listeners (event 0x7b54c99c,
// through the subordinate's slot 5) when it is connected (mDisconnecting 0) or about to be disconnected (1).
class cGraphConnChanged {
public:
    static constexpr uint32_t kEventId = 0x7b54c99c;
    static constexpr uint32_t kType = 0x3b5cb427; // slot 3 (0x290350)
    cGraphConnChanged(uint32_t disconnecting, cIObjectGraphNode *node, cIObjectGraphNode *subordinate, uint32_t iid)
        : mDisconnecting(disconnecting), mNode(node), mSubordinate(subordinate), mIid(iid)
    {
    }
    virtual ~cGraphConnChanged() = default;                   // 0 (shared 0x154eb0)
    virtual uint32_t Assign(const cGraphConnChanged *other);  // 1 (shared 0x1550b0): returns the copied iid
    virtual uint32_t AssignAlias(const cGraphConnChanged *other); // 2 (the same body)
    virtual uint32_t GetEventType() const;                    // 3 (0x290350)
    virtual uint32_t GetSize() const;                         // 4 (shared 0x1559e0: 0x14)
    virtual uint32_t GetBodySize() const;                     // 5 (shared 0x1561d0: 0x10)
    virtual void CloneInto(void *memory) const;               // 6 (0x28bda0)

    uint32_t mDisconnecting;          // +4
    cIObjectGraphNode *mNode;         // +8 the node whose list changes
    cIObjectGraphNode *mSubordinate;  // +0xc
    uint32_t mIid;                    // +0x10 the list (what it holds)
};
static_assert(sizeof(cGraphConnChanged) == 0x14, "cGraphConnChanged size");

// The notifications (each __stdcall; the events are on the stack and live for the call only).
// 0xa34310: `node` is about to get a subordinate in its `iid` list.
void __stdcall NotifySubordinateAdding(cIObjectGraphNode *node, uint32_t iid);
// 0xa34370: `subordinate`'s listeners hear it is being disconnected, then `node` that it is losing it.
void __stdcall NotifySubordinateRemoving(cIObjectGraphNode *node, uint32_t iid, cIObjectGraphNode *subordinate);
// 0xa34430: `subordinate`'s listeners hear it was connected, then `node` that it was added.
void __stdcall NotifySubordinateAdded(cIObjectGraphNode *node, uint32_t iid, cIObjectGraphNode *subordinate);
// 0xa344d0: `node` lost a subordinate of its `iid` list.
void __stdcall NotifySubordinateRemoved(cIObjectGraphNode *node, uint32_t iid);

class cObjectGraphNodeImpl {
public:
    static constexpr uint32_t kExtensionIid = 0xfb06dba0; // what Read asks the reader for (each extension)
    static constexpr uint32_t kVersion = 4;               // what Write records; older streams have the legacy tag

    cObjectGraphNodeImpl() : mFirst(nullptr), mTag{0xffffffffu} {}
    ~cObjectGraphNodeImpl(); // 0xa342a0
    cObjectGraphNodeImpl(const cObjectGraphNodeImpl &) = delete;
    cObjectGraphNodeImpl &operator=(const cObjectGraphNodeImpl &) = delete;
    void Destruct(); // the destructor as a hookable member (0xa342a0)

    void AddExtension(cIObjectGraphNode *outer, cIExtension *extension);      // 0xa34530
    bool HasExtension(cIExtension *extension);                                // 0xa34570
    bool FindExtension(uint32_t iid, void **out, cIExtension *start);         // 0xa345a0
    void Read(cIObjectGraphNode *outer, ifaces::cIGZPersistReader *reader);   // 0xa345f0
    void RemoveExtension(cIExtension *extension);                             // 0xa34870
    void SetTagHandle(cIObjectGraphNode *outer, uint32_t handle);             // 0xa34900
    void SetTag(cIObjectGraphNode *outer, const char *tag);                   // 0xa349d0
    bool IsKindOf(uint32_t iid);                                              // 0xa34ac0
    uint32_t GetTagHandle();                                                  // 0xa34af0
    const char *GetTag();                                                     // 0xa34bb0: null when empty
    cPersistTypeInfo *GetTypeInfo(cPersistTypeInfo *out);                     // 0xa34cb0 ("cObjectGraphNode", 4)
    void Write(cIObjectGraphNode *outer, ifaces::cIGZPersistWriter *writer);  // 0xa34d70
    // 0xa34f20: a copy of each extension added to `target`, then the tag handle (mode and context are not used).
    void CopyTo(cIObjectGraphNode *source, cIObjectGraphNode *target, uint32_t mode, cINodeCloneContext *context);

    cIExtension *mFirst; // +0 counted
    ServiceCell mTag;    // +4 the tag's string-pool handle

private:
    // The legacy tag through `outer`'s slots (SetTagHandle, SetTag): an outer tag extension is removed.
    void DropLegacyTag(cIObjectGraphNode *outer);
    // The legacy tag through this Impl's own members (GetTagHandle, GetTag): its text becomes the handle.
    void AdoptLegacyTag();
};
static_assert(sizeof(cObjectGraphNodeImpl) == 8, "cObjectGraphNodeImpl size");

// The heap cRZString a type-info record names its class with (what the persist code releases), counted once.
cIGZUnknown *NewTypeName(const char *name);

} // namespace nGZSceneGraph
