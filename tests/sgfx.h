#pragma once
// sgfx: the scene-graph node world for difftests (lane NZ, scenegraph N0). Later scenegraph lanes (N1 the other nodes,
// D1 the drawables, M1 the material and resource nodes) build their tests on it; extend it here rather than copying it.
// fakes: cICompositionTreeNode, cIObjectGraphNode, cIExtension, cITagExtension, cIGZPersistReader, cIGZPersistWriter, cICloneContext
//
// What it gives (everything is a drawfx fake, tests/drawfx_core.h: each call is logged, pointers to fakes log as their
// ids, and the two sides' logs are compared with drawfx::Same):
//
//   Slot tables   kNode (cICompositionTreeNode, 62 slots with the deleting destructor; a cIObjectGraphNode is its first
//                 29), kExtension (cIExtension, 27), kTagFace (cITagExtension's own 3), kReferent (the +8 persist face,
//                 7), kPool (the string pool, 9), kReader / kWriter (cIGZPersistReader / Writer), kCloneContext,
//                 kBehavior (a plain listener, 15), kUnknown (3). Arities from the implementing bodies' `ret N`
//                 (build/nz_dis.py, tools/slotsig.py); a slot nobody has needed yet is unsettled and fails the test
//                 when called (settle it here once you know it).
//   Node fakes    MakeNode: a state-backed fake node. Its links (parent, previous and next sibling, first child; slot 47
//                 SetLink writes them, the getters 33-40 read them), its referent face (16/17) and first extension
//                 (20/21) live in value[] (kParentWord...). QueryInterface answers the tree and graph interfaces with
//                 itself, counted. Events (slot 3) and notifications (slot 5) note their body (NoteBody).
//                 FindExtension (24) walks the fake's extension list (kFirstExtensionWord) as the Impl does.
//                 GetChildren (41/42) writes {first child, first child, 0} into its hidden out and GetLocal /
//                 GetWorldTransform (53/54) write a transform distinct per id (not the identity), so code that
//                 climbs to a fake parent shows it. A hidden out the fake leaves unwritten is stack garbage to the
//                 original.
//   Extensions    MakeExtension: next (19/20, 22), owner (17/18, 21), persistent (24), clone (15) in value[].
//                 MakeTagExtension adds the cITagExtension face (interface 0x9a80964e): a struct with its own vtable
//                 (an embedded fake) and a vbptr to the extension fake, as MSVC lays out `public virtual cIExtension`;
//                 QueryInterface hands it out counted, its slot 1 returns the tag text.
//   String pool   gPool, installed in the cache cell 0x10f9070 (ServiceCell's pool): slot 5 interns (equal texts share
//                 a handle, from kFirstHandle up), 6 / 7 count a handle up and down, 8 returns its text ("" for an unknown
//                 one). NoteCounts logs each handle's count; Handle(text) interns for the test without logging.
//   Reader/writer gReader answers GetUint32 (10), GetGZStr (22, through the string's FromChar), GetObjectRef (25, counted
//                 through the object's AddRef) and ReadTypeInfo (26: a counted name fake, 0, the next version) from
//                 queues the test fills; gWriter notes the type-info records (28: name text, word 4, version), the rest
//                 is logged by the slot tables (4 'u', 10 'u', 23 's', 26 / 27 the object).
//   Events        NoteBody notes an nGZEvent body (slot 3 its type, 4 its size, 5 its body size, then its words,
//                 pointers normalised). Manager::Use installs our real cConnectionManager (verified) in a behaviour's
//                 event-manager cell (the cell is restored by `cells`; Drop the other side's manager first);
//                 Manager::Listen connects a listener fake (MakeListener) to a node. Every test that makes real nodes
//                 needs one: with a null cell the original asks the framework service for the manager.
//   Registry      RegistryScope(kNodeRegistry): the node family's event registry (0x263f30, a function-local static
//                 std::map) placed at its static with kRegistryIds' records for the test's scope. Make one at the top
//                 of every test whose nodes send events; without it the runner dies of heap corruption.
//   Resources     gResources (lane OE): the framework in g_Framework (0x10c9378) handing out a resource manager fake
//                 whose FindSegmentWithKey (34) answers three segment fakes in turn (cSGResource's Read, its submit
//                 bodies' slots 9 / 10 / 14, a segment's slot 21), and (lane OK) the scene graph system fake (462: a
//                 scene node's constructor and destructor call its slots 13 / 14) while systemUp.
//   Others        MakeCloneContext (430): FindClone answers one node, counted. MakeTraversal: Visit returns a value.
//   Real nodes    Nodes: n nodes from a factory (either side's: ctx.Original<Factory>(rva) or our Create), counted,
//                 each registered as a range (kNodeTag | index << 8 | offset), linked by a parent list through AddChild;
//                 NoteShape logs each node's links and flags through its slots, NoteWords its words (vtable words
//                 skipped); Release releases the test's references.
//
// Use: CheckSides(ctx, what, seeds, build, run) runs build(seed, cells) then run on each side and compares the logs;
// build resets the fakes, the pool and the manager. MarkStep(ctx, "step k, op n") adds the step to the side's mark, so
// a runner that dies mid-seed reports where (a hung runner reports only when killed: the child with --monitor-state).
// Ids: nodes 100+, extensions 200+, tag faces 300+, the pool 400, reader 410, writer 420, clone context 430, type names
// 440+, traversals 450, the framework 460, the resource manager 461, segments 470+, listeners 500+, referents 600+.
//
// Pitfalls: a node fake does not count its own next sibling as a real node does (SetLink stores the word only), so
// compare refcounts of fakes only between sides, never against a real node's rules. NoteBody calls the body's own
// slots: only pass it nGZEvent bodies (the event ids below say which events carry one). drawfx counts slots 1 / 2 as
// AddRef / Release unless the slot has a handler: a face whose slots 1 / 2 are something else (the tag face's text)
// needs one. A clone context that answers a node for every source under a deep clone (mode 1) makes a node its own
// child: answer one source, or none.
#include "code_patch.h"
#include "difftest.h"
#include "drawfx_core.h"
#include "gz_registry_fx.h"

#include "gz/cConnectionManager.h"
#include "gz/cGZEventReflector.h"
#include "sg/SGNodeInterfaces.h"
#include "sg/SGNodeTemplates.h"
#include "sg/cGeometryBuilder.h"
#include "sg/cShapeRefNode.h"
#include "sg/cStandardLight.h"
#include "sg/cViewerRefNode.h"

#include <array>
#include <climits>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace sgfx {

using drawfx::Fake;
using drawfx::Ret;
using drawfx::Slot;

constexpr uint32_t kIidTree = 0x4512dd13;  // cICompositionTreeNode
constexpr uint32_t kIidGraph = 0x5aef607b; // cIObjectGraphNode
constexpr uint32_t kIidTag = 0x9a80964e;   // cITagExtension
constexpr uint32_t kNoHandle = 0xffffffff;

// ---- slot tables ----
#define SGFX_UNKNOWN_SLOTS                                                                                             \
    {2, Ret::Int, true, "QueryInterface", "ui"}, {0, Ret::Int, true, "AddRef", ""}, {0, Ret::Int, true, "Release", ""}
#define SGFX_BEHAVIOR_SLOTS                                                                                            \
    SGFX_UNKNOWN_SLOTS, {4, Ret::Int, true, "OnEvent", "uuii"},              /* 3 */                                   \
        {3, Ret::Int, true, "OnQuery", "uui"},                               /* 4 */                                   \
        {3, Ret::Int, true, "NotifyListeners", "uui"},                       /* 5 */                                   \
        {3, Ret::Int, true, "AskListeners", "uui"},                          /* 6 */                                   \
        {3, Ret::Int, true, "Slot7", "uuu"},                                 /* 7 */                                   \
        {3, Ret::Int, true, "Connect", "uiu"},                               /* 8 */                                   \
        {3, Ret::Int, true, "Disconnect", "uiu"},                            /* 9 */                                   \
        {3, Ret::Int, true, "Slot10", "uuu"},                                /* 10 */                                  \
        {0, Ret::Int, true, "IsActive", ""},                                 /* 11 */                                  \
        {0, Ret::Int, true, "GetEventCount", ""},                            /* 12 */                                  \
        {1, Ret::Int, true, "GetEventIds", "i"},                             /* 13 (hidden out) */                     \
        {2, Ret::Int, true, "GetEventAttribute", "iu"}                       /* 14 (hidden out) */

inline constexpr Slot kUnknownSlots[] = {SGFX_UNKNOWN_SLOTS};
inline constexpr drawfx::Iface kUnknown{"Unknown", 3, kUnknownSlots};

inline constexpr Slot kBehaviorSlots[] = {SGFX_BEHAVIOR_SLOTS};
inline constexpr drawfx::Iface kBehavior{"Behavior", 15, kBehaviorSlots};

inline constexpr Slot kNodeSlots[] = {
    SGFX_BEHAVIOR_SLOTS,
    {4, Ret::Int, true, "Clone", "uuii"},              // 15
    {0, Ret::Int, true, "GetReferentConst", ""},       // 16
    {0, Ret::Int, true, "GetReferent", ""},            // 17
    {1, Ret::Int, true, "SetTagHandle", "u"},          // 18
    {1, Ret::Int, true, "SetTag", "s"},                // 19
    {0, Ret::Int, true, "GetFirstExtensionConst", ""}, // 20
    {0, Ret::Int, true, "GetFirstExtension", ""},      // 21
    {1, Ret::Int, true, "AddExtension", "i"},          // 22
    {1, Ret::Int, true, "RemoveExtension", "i"},       // 23
    {3, Ret::Int, true, "FindExtension", "uii"},       // 24
    {1, Ret::Int, true, "IsKindOf", "u"},              // 25
    {0, Ret::Int, true, "GetTag", ""},                 // 26
    {0, Ret::Int, true, "GetTagHandle", ""},           // 27
    {1, Ret::Int, true, "HasExtension", "i"},          // 28
    {2, Ret::Int, true, "SetFlag", "ub"},              // 29
    {1, Ret::Int, true, "Accept", "i"},                // 30
    {0, Ret::Int, true, "GetFromParent31", ""},        // 31
    {0, Ret::Int, true, "GetFromParent32", ""},        // 32
    {0, Ret::Int, true, "GetParentConst", ""},         // 33
    {0, Ret::Int, true, "GetParent", ""},              // 34
    {0, Ret::Int, true, "GetPrevSiblingConst", ""},    // 35
    {0, Ret::Int, true, "GetPrevSibling", ""},         // 36
    {0, Ret::Int, true, "GetNextSiblingConst", ""},    // 37
    {0, Ret::Int, true, "GetNextSibling", ""},         // 38
    {0, Ret::Int, true, "GetFirstChildConst", ""},     // 39
    {0, Ret::Int, true, "GetFirstChild", ""},          // 40
    {1, Ret::Int, true, "GetChildrenConst", "i"},      // 41 (hidden out)
    {1, Ret::Int, true, "GetChildren", "i"},           // 42 (hidden out)
    {3, Ret::Int, true, "MakeIterator43", "iuu"},      // 43
    {3, Ret::Int, true, "MakeIterator44", "iuu"},      // 44
    {1, Ret::Int, true, "AddChild", "i"},              // 45
    {1, Ret::Int, true, "RemoveChild", "i"},           // 46
    {2, Ret::Int, true, "SetLink", "ui"},              // 47
    {1, Ret::Int, true, "TestFlag", "u"},              // 48
    {0, Ret::Int, true, "GetFlags", ""},               // 49
    {1, Ret::Int, true, "AcceptReadOnly", "i"},        // 50
    {2, Ret::Int, true, "MakeIterator51", "iu"},       // 51
    {0, Ret::Int, true, "HasChildren", ""},            // 52
    {1, Ret::Int, true, "GetLocalTransform", "i"},     // 53 (hidden out)
    {1, Ret::Int, true, "GetWorldTransform", "i"},     // 54 (hidden out)
    {0, Ret::Int, true, "GetLimit55", ""},             // 55
    {2, Ret::Int, true, "ToParent34", "iF13"},         // 56
    {2, Ret::Int, true, "ToParent1c", "iF7"},          // 57
    {2, Ret::Int, true, "FromParent34", "iF13"},       // 58
    {2, Ret::Int, true, "FromParent1c", "iF7"},        // 59
    {3, Ret::Int, true, "OnContentChanged", "uuu"},    // 60
    {1, Ret::Int, true, "DeletingDestructor", "u"},    // 61
};
inline constexpr drawfx::Iface kNode{"Node", 62, kNodeSlots};

// The transform family's tables (lane OB): kNode's slots 0-60, then each level's own (the tail replaces kNode's slot 61,
// the deleting destructor, which moves to the end of the last level's interface).
namespace detail {
template <size_t Total, size_t Tail>
constexpr std::array<Slot, Total> ExtendNode(const Slot (&tail)[Tail])
{
    static_assert(Total == 61 + Tail);
    std::array<Slot, Total> r{};
    for (size_t i = 0; i < 61; ++i) r[i] = kNodeSlots[i];
    for (size_t i = 0; i < Tail; ++i) r[61 + i] = tail[i];
    return r;
}
#define SGFX_TRANSFORM_PART                                                                                                {1, Ret::Int, true, "SetLimit", "u"},               /* 61 */                                                              {2, Ret::Int, true, "SetTransformParts", "F3F4"}, /* 62 (translation, rotation) */                                    {1, Ret::Int, true, "SetTransform", "F7"},      /* 63 */                                                              {1, Ret::Int, true, "SetTranslation", "F3"},    /* 64 */                                                              {1, Ret::Int, true, "SetRotation", "F4"}        /* 65 */
#define SGFX_BOUNDED_PART                                                                                                  {1, Ret::Int, true, "SetBoundFlag", "b"},           /* 66 */                                                              {2, Ret::Int, true, "SetHolder", "uu"},         /* 67 */                                                              {0, Ret::Int, true, "GetHolderConst", ""},      /* 68 */                                                              {0, Ret::Int, true, "GetHolder", ""},           /* 69 */                                                              {0, Ret::Int, true, "Slot70", ""},              /* 70 */                                                              {2, Ret::Int, true, "GetBoundBoxIn", "iF13"},   /* 71 (hidden out) */                                                 {1, Ret::Int, true, "GetBoundBox", "i"},        /* 72 (hidden out) */                                                 {2, Ret::Int, true, "Slot73", "uu"},            /* 73 */                                                              {1, Ret::Int, true, "IntersectsLinear", "i"},   /* 74 */                                                              {2, Ret::Int, true, "FirstHitLinear", "ii"},    /* 75 */                                                              {0, Ret::Int, true, "GetHolderData", ""}        /* 76 */
#define SGFX_DELETING {1, Ret::Int, true, "DeletingDestructor", "u"}
inline constexpr Slot kTransformTail[] = {SGFX_TRANSFORM_PART, SGFX_DELETING};                             // 61-66
inline constexpr Slot kBoundedTail[] = {SGFX_TRANSFORM_PART, SGFX_BOUNDED_PART, SGFX_DELETING};            // 61-77
inline constexpr Slot kExplicitTail[] = {
    SGFX_TRANSFORM_PART, SGFX_BOUNDED_PART,
    {1, Ret::Int, true, "SetShape", "i"},            // 77
    {0, Ret::Int, true, "GetShape", ""},             // 78
    SGFX_DELETING,                                   // 79
};
#undef SGFX_TRANSFORM_PART
#undef SGFX_BOUNDED_PART
#undef SGFX_DELETING
inline constexpr auto kTransformNodeSlots = ExtendNode<67>(kTransformTail);
inline constexpr auto kBoundedNodeSlots = ExtendNode<78>(kBoundedTail);
inline constexpr auto kExplicitNodeSlots = ExtendNode<80>(kExplicitTail);
} // namespace detail
inline constexpr drawfx::Iface kTransformNode{"TransformNode", 67, detail::kTransformNodeSlots.data()};
inline constexpr drawfx::Iface kBoundedNode{"BoundedNode", 78, detail::kBoundedNodeSlots.data()};
inline constexpr drawfx::Iface kExplicitBoundNode{"ExplicitBoundNode", 80, detail::kExplicitNodeSlots.data()};

inline constexpr Slot kExtensionSlots[] = {
    SGFX_BEHAVIOR_SLOTS,
    {0, Ret::Int, true, "Clone", ""},            // 15
    {0, Ret::Int, false, "", ""},                // 16
    {0, Ret::Int, true, "GetOwnerConst", ""},    // 17
    {0, Ret::Int, true, "GetOwner", ""},         // 18
    {0, Ret::Int, true, "GetNextConst", ""},     // 19
    {0, Ret::Int, true, "GetNext", ""},          // 20
    {1, Ret::Int, true, "SetOwner", "i"},        // 21
    {1, Ret::Int, true, "SetNext", "i"},         // 22
    {1, Ret::Int, true, "ExtensionSlot23", "u"}, // 23
    {0, Ret::Int, true, "IsPersistent", ""},     // 24
    {0, Ret::Int, false, "", ""},                // 25
    {1, Ret::Int, true, "Accept", "i"},          // 26
};
inline constexpr drawfx::Iface kExtension{"Extension", 27, kExtensionSlots};

inline constexpr Slot kTagFaceSlots[] = {
    {0, Ret::Int, false, "", ""},           // 0
    {0, Ret::Int, true, "GetTagText", ""},  // 1
    {0, Ret::Int, false, "", ""},           // 2
};
inline constexpr drawfx::Iface kTagFace{"TagFace", 3, kTagFaceSlots};

inline constexpr Slot kReferentSlots[] = {
    SGFX_UNKNOWN_SLOTS,
    {1, Ret::Int, true, "Read", "i"},        // 3
    {1, Ret::Int, true, "GetTypeInfo", "i"}, // 4 (hidden out)
    {1, Ret::Int, true, "Write", "i"},       // 5
    {0, Ret::Int, true, "GetOwnerInfo", ""}, // 6
};
inline constexpr drawfx::Iface kReferent{"Referent", 7, kReferentSlots};

inline constexpr Slot kPoolSlots[] = {
    SGFX_UNKNOWN_SLOTS,
    {0, Ret::Int, false, "", ""},            // 3
    {0, Ret::Int, false, "", ""},            // 4
    {2, Ret::Int, true, "Intern", "su"},     // 5
    {1, Ret::Int, true, "AddRefHandle", "u"},  // 6
    {1, Ret::Int, true, "ReleaseHandle", "u"}, // 7
    {1, Ret::Int, true, "GetText", "u"},     // 8
};
inline constexpr drawfx::Iface kPool{"StringPool", 9, kPoolSlots};

#define SGFX_UNSETTLED {0, Ret::Int, false, "", ""}
inline constexpr Slot kReaderSlots[] = {
    SGFX_UNKNOWN_SLOTS, SGFX_UNSETTLED,
    {0, Ret::Int, true, "GetBool", ""}, // 4
    SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED,
    {0, Ret::Int, true, "GetUint32", ""}, // 10
    {0, Ret::Float, true, "GetFloat32", ""}, // 11
    SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED,
    SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED,
    {1, Ret::Int, true, "GetGZStr", "i"},        // 22
    SGFX_UNSETTLED, SGFX_UNSETTLED,
    {2, Ret::Int, true, "GetObjectRef", "ui"},   // 25
    {1, Ret::Int, true, "ReadTypeInfo", "i"},    // 26
};
inline constexpr drawfx::Iface kReader{"PersistReader", 27, kReaderSlots};

inline constexpr Slot kWriterSlots[] = {
    SGFX_UNKNOWN_SLOTS, SGFX_UNSETTLED,
    {1, Ret::Int, true, "SetUint8", "b"}, // 4 (a byte: the caller may leave the rest of the word as it was, e.g. after sete)
    SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED,
    {1, Ret::Int, true, "SetUint32", "u"}, // 10
    {1, Ret::Int, true, "SetFloat32", "f"}, // 11
    SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED,
    SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED,
    {1, Ret::Int, true, "WriteRZCharStr", "s"}, // 23
    SGFX_UNSETTLED, SGFX_UNSETTLED,
    {1, Ret::Int, true, "WriteObjectRef", "i"}, // 26
    {1, Ret::Int, true, "WriteFlaggedObject", "i"}, // 27 (lane OG: cResourceNode's Write, its source's resource or null)
    {1, Ret::Int, true, "WriteTypeInfo", "i"},  // 28
};
inline constexpr drawfx::Iface kWriter{"PersistWriter", 29, kWriterSlots};

inline constexpr Slot kCloneContextSlots[] = {
    SGFX_UNKNOWN_SLOTS,
    {3, Ret::Int, true, "FindClone", "iui"}, // 3
};
inline constexpr drawfx::Iface kCloneContext{"CloneContext", 4, kCloneContextSlots};
#undef SGFX_UNSETTLED

// The tables' shapes: each as long as its interface, the named slots where their comments say.
namespace detail {
constexpr bool SlotIs(const Slot *slots, unsigned i, const char *name)
{
    const char *a = slots[i].name;
    while (*a && *a == *name) ++a, ++name;
    return *a == *name;
}
} // namespace detail
static_assert(sizeof(kNodeSlots) / sizeof(Slot) == 62 && detail::SlotIs(kNodeSlots, 15, "Clone") &&
              detail::SlotIs(kNodeSlots, 29, "SetFlag") && detail::SlotIs(kNodeSlots, 47, "SetLink") &&
              detail::SlotIs(kNodeSlots, 60, "OnContentChanged"));
static_assert(sizeof(kExtensionSlots) / sizeof(Slot) == 27 && detail::SlotIs(kExtensionSlots, 22, "SetNext") &&
              detail::SlotIs(kExtensionSlots, 26, "Accept"));
static_assert(sizeof(kReaderSlots) / sizeof(Slot) == 27 && detail::SlotIs(kReaderSlots, 10, "GetUint32") &&
              detail::SlotIs(kReaderSlots, 22, "GetGZStr") && detail::SlotIs(kReaderSlots, 25, "GetObjectRef") &&
              detail::SlotIs(kReaderSlots, 26, "ReadTypeInfo"));
static_assert(sizeof(kWriterSlots) / sizeof(Slot) == 29 && detail::SlotIs(kWriterSlots, 10, "SetUint32") &&
              detail::SlotIs(kWriterSlots, 23, "WriteRZCharStr") && detail::SlotIs(kWriterSlots, 26, "WriteObjectRef") &&
              detail::SlotIs(kWriterSlots, 28, "WriteTypeInfo"));
static_assert(sizeof(kPoolSlots) / sizeof(Slot) == 9 && detail::SlotIs(kPoolSlots, 5, "Intern") &&
              detail::SlotIs(kPoolSlots, 8, "GetText"));
static_assert(sizeof(kReferentSlots) / sizeof(Slot) == 7 && sizeof(kBehaviorSlots) / sizeof(Slot) == 15 &&
              detail::SlotIs(kBehaviorSlots, 14, "GetEventAttribute"));

// ---- event bodies ----
namespace detail {
// The body's type, size and body size through its slots 3-5 and its first words; false when it cannot be read.
inline bool ReadBody(const void *body, uint32_t *head, uint32_t *words, unsigned *count)
{
    __try {
        using U32Fn = uint32_t(__thiscall *)(const void *);
        void *const *vt = *static_cast<void *const *const *>(body);
        head[0] = reinterpret_cast<U32Fn>(vt[3])(body);
        head[1] = reinterpret_cast<U32Fn>(vt[4])(body);
        head[2] = reinterpret_cast<U32Fn>(vt[5])(body);
        unsigned n = head[1] / 4;
        if (n > 16) n = 16;
        const uint32_t *w = static_cast<const uint32_t *>(body);
        for (unsigned i = 1; i < n; ++i) words[i - 1] = w[i];
        *count = n ? n - 1 : 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
} // namespace detail

// Notes an nGZEvent body: its type, size and body size (its slots 3-5), then its words after the vtable, normalised
// (a node in it logs as its id or range).
inline void NoteBody(const char *label, const void *body)
{
    drawfx::Call c;
    c.note = label;
    uint32_t head[3] = {};
    uint32_t words[16] = {};
    unsigned count = 0;
    if (!body) {
        c.words.push_back(0);
    } else if (!detail::ReadBody(body, head, words, &count)) {
        c.words.push_back(drawfx::kUnreadable);
    } else {
        c.words = {1, head[0], head[1], head[2]};
        for (unsigned i = 0; i < count; ++i) c.words.push_back(drawfx::Norm(words[i]));
    }
    drawfx::gLog.push_back(std::move(c));
}

// ---- node fakes ----
enum NodeWord : unsigned {
    kParentWord = 0,    // SetLink kind 0 (cICompositionTreeNode::kParentLink)
    kPrevWord = 1,      // kind 1
    kNextWord = 2,      // kind 2
    kFirstChildWord = 3,
    kReferentWord = 4,
    kFirstExtensionWord = 5,
    kFlagsWord = 6, // MakeFlagNode: the node's flags (SetFlag 29, TestFlag 48, GetFlags 49)
    kLimitWord = 7, // MakeFlagNode: slot 55's answer
};

namespace detail {
template <unsigned Word> uint32_t GetWord(Fake &self, const uint32_t *) { return self.value[Word]; }
// FindExtension(iid, out, start): from start (or the first extension), each asked through its QueryInterface, the next
// through its GetNext (slot 20), as the node's Impl walks its list.
inline uint32_t NodeFind(Fake &self, const uint32_t *a)
{
    using NextFn = uint32_t(__thiscall *)(void *);
    using QueryFn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
    for (uint32_t e = a[2] ? a[2] : self.value[kFirstExtensionWord]; e;) {
        void *p = reinterpret_cast<void *>(uintptr_t(e));
        void **vt = *static_cast<void ***>(p);
        if (uint8_t(reinterpret_cast<QueryFn>(vt[0])(p, a[0], a[1]))) return 1;
        e = reinterpret_cast<NextFn>(vt[20])(p);
    }
    return 0;
}
inline uint32_t SetLink(Fake &self, const uint32_t *a)
{
    if (a[0] <= kNextWord) self.value[a[0]] = a[1];
    return 0;
}
// GetChildren(Const) (slots 41 / 42) return a cChildRange through a hidden out: {first child, first child, 0}, as the
// templates' 0xa373a0 writes it.
inline uint32_t NodeChildren(Fake &self, const uint32_t *a)
{
    uint32_t *out = reinterpret_cast<uint32_t *>(uintptr_t(a[0]));
    out[0] = out[1] = self.value[kFirstChildWord];
    out[2] = 0;
    return a[0];
}
// GetLocalTransform / GetWorldTransform (slots 53 / 54) return a cRigidTransform (0x1c bytes) through a hidden out:
// every float written, distinct per fake id (word i = id + i / 4, the last 1 + id / 64), so a node that climbs to a
// fake parent shows it.
inline uint32_t NodeTransform(Fake &self, const uint32_t *a)
{
    float *out = reinterpret_cast<float *>(uintptr_t(a[0]));
    for (unsigned i = 0; i < 6; ++i) out[i] = float(self.id) + float(i) / 4.0f;
    out[6] = 1.0f + float(self.id) / 64.0f;
    return a[0];
}
inline uint32_t NodeEvent(Fake &, const uint32_t *a)
{
    NoteBody("event body", reinterpret_cast<const void *>(uintptr_t(a[2])));
    return 0;
}
inline uint32_t NodeNotify(Fake &, const uint32_t *a)
{
    NoteBody("notify body", reinterpret_cast<const void *>(uintptr_t(a[2])));
    return 0;
}
} // namespace detail

// A fake node (kNode): links in value[] (NodeWord), the tree and graph interfaces answered with itself, events noted.
template <const drawfx::Iface &I = kNode> inline void MakeNode(Fake &f, uint32_t id)
{
    f.Reset<I>(id);
    f.AddFace(kIidTree, f);
    f.AddFace(kIidGraph, f);
    f.On(3, &detail::NodeEvent);
    f.On(5, &detail::NodeNotify);
    f.On(16, &detail::GetWord<kReferentWord>);
    f.On(17, &detail::GetWord<kReferentWord>);
    f.On(20, &detail::GetWord<kFirstExtensionWord>);
    f.On(21, &detail::GetWord<kFirstExtensionWord>);
    f.On(24, &detail::NodeFind);
    f.On(33, &detail::GetWord<kParentWord>);
    f.On(34, &detail::GetWord<kParentWord>);
    f.On(35, &detail::GetWord<kPrevWord>);
    f.On(36, &detail::GetWord<kPrevWord>);
    f.On(37, &detail::GetWord<kNextWord>);
    f.On(38, &detail::GetWord<kNextWord>);
    f.On(39, &detail::GetWord<kFirstChildWord>);
    f.On(40, &detail::GetWord<kFirstChildWord>);
    f.On(41, &detail::NodeChildren);
    f.On(42, &detail::NodeChildren);
    f.On(47, &detail::SetLink);
    f.On(53, &detail::NodeTransform);
    f.On(54, &detail::NodeTransform);
}
inline void SetWord(Fake &f, unsigned word, const void *p) { f.value[word] = drawfx::P(p); }

namespace detail {
inline uint32_t NodeSetFlag(Fake &self, const uint32_t *a)
{
    if (a[0] < 32) self.value[kFlagsWord] = (self.value[kFlagsWord] & ~(1u << a[0])) | (uint32_t(a[1] & 0xff ? 1 : 0) << a[0]);
    return 0;
}
inline uint32_t NodeTestFlag(Fake &self, const uint32_t *a) { return (self.value[kFlagsWord] >> (a[0] & 31)) & 1; }
inline uint32_t NodeFlags(Fake &self, const uint32_t *) { return self.value[kFlagsWord]; }
inline uint32_t NodeLimit(Fake &self, const uint32_t *) { return self.value[kLimitWord]; }
} // namespace detail
// MakeNode, and the flags the tree code reads and writes (a state-backed word) and the limit it asks (slot 55).
template <const drawfx::Iface &I = kNode> inline void MakeFlagNode(Fake &f, uint32_t id)
{
    MakeNode<I>(f, id);
    f.On(29, &detail::NodeSetFlag);
    f.On(48, &detail::NodeTestFlag);
    f.On(49, &detail::NodeFlags);
    f.On(55, &detail::NodeLimit);
}

// ---- extensions ----
enum ExtensionWord : unsigned {
    kExtNextWord = 0,
    kExtOwnerWord = 1,
    kExtPersistentWord = 2,
    kExtCloneWord = 3,
    kExtTagFaceWord = 4, // the cITagExtension face, or 0
};

// The cITagExtension face: its own vtable (an embedded fake's thunks), a vbptr whose second entry leads to the
// extension (the virtual base cIExtension).
struct TagFace {
    void *vt;
    const int32_t *vbptr;
    int32_t vbtable[2];
};

namespace detail {
inline uint32_t ExtSet(Fake &self, unsigned word, uint32_t v) { return self.value[word] = v, 0; }
inline uint32_t ExtSetNext(Fake &self, const uint32_t *a) { return ExtSet(self, kExtNextWord, a[0]); }
inline uint32_t ExtSetOwner(Fake &self, const uint32_t *a) { return ExtSet(self, kExtOwnerWord, a[0]); }
template <unsigned Word> uint32_t ExtGet(Fake &self, const uint32_t *) { return self.value[Word]; }
// QueryInterface: the tag face (counted) when there is one, else the added faces.
inline uint32_t ExtQuery(Fake &self, const uint32_t *a)
{
    void **const out = reinterpret_cast<void **>(uintptr_t(a[1]));
    if (a[0] == kIidTag && self.value[kExtTagFaceWord]) {
        ++self.refs;
        if (out) *out = reinterpret_cast<void *>(uintptr_t(self.value[kExtTagFaceWord]));
        return 1;
    }
    for (unsigned i = 0; i < self.faceCount; ++i)
        if (self.faces[i].iid == a[0]) {
            ++self.faces[i].fake->refs;
            if (out) *out = self.faces[i].fake;
            return 1;
        }
    return 0;
}
} // namespace detail

// A fake extension (kExtension): its list and owner links, persistence and clone in value[] (ExtensionWord).
inline void MakeExtension(Fake &f, uint32_t id, bool persistent = true)
{
    f.Reset<kExtension>(id);
    f.value[kExtPersistentWord] = persistent;
    f.On(0, &detail::ExtQuery);
    f.On(15, &detail::ExtGet<kExtCloneWord>);
    f.On(17, &detail::ExtGet<kExtOwnerWord>);
    f.On(18, &detail::ExtGet<kExtOwnerWord>);
    f.On(19, &detail::ExtGet<kExtNextWord>);
    f.On(20, &detail::ExtGet<kExtNextWord>);
    f.On(21, &detail::ExtSetOwner);
    f.On(22, &detail::ExtSetNext);
    f.On(24, &detail::ExtGet<kExtPersistentWord>);
}

// A fake extension that is also a cITagExtension with `text` (static storage): `face` is the face object, `faceFake`
// answers its slots.
inline void MakeTagExtension(Fake &ext, uint32_t extId, Fake &faceFake, uint32_t faceId, TagFace &face, const char *text)
{
    MakeExtension(ext, extId);
    faceFake.Reset<kTagFace>(faceId);
    face.vbptr = face.vbtable;
    face.vbtable[0] = -4;
    face.vbtable[1] = int32_t(drawfx::P(&ext) - drawfx::P(&face.vbptr));
    faceFake.Embed(&face);
    // (A handler: drawfx counts on slots 1 and 2 as AddRef / Release, and this face's slot 1 is not AddRef.)
    faceFake.Returns(1, text);
    faceFake.On(1, [](Fake &self, const uint32_t *) { return self.ret[1]; });
    ext.value[kExtTagFaceWord] = drawfx::P(&face);
    drawfx::AddRange(&face, sizeof(face), 0x7d000000 | (faceId << 8));
}

// ---- the string pool ----
struct Pool {
    static constexpr uint32_t kCell = 0x10f9070; // ServiceCell's pool (GetCachedObjectB's cache)
    static constexpr uint32_t kFirstHandle = 0x5100;
    static constexpr unsigned kMax = 64;

    Fake fake;
    std::string texts[kMax];
    int32_t counts[kMax];
    unsigned size = 0;

    // A fresh pool (no handles) in the cell, restored when `cells` ends.
    void Reset(drawfx::Cells &cells);
    // The handle of `text`, interned without logging (for a test's setup).
    uint32_t Handle(const char *text);
    int Index(uint32_t handle) const
    {
        const uint32_t i = handle - kFirstHandle;
        return handle >= kFirstHandle && i < size ? int(i) : -1;
    }
    // Each handle's count and text (the test's references to the pool, compared between sides).
    void NoteCounts() const
    {
        for (unsigned i = 0; i < size; ++i) {
            drawfx::Call c;
            c.note = "pool handle";
            c.words = {kFirstHandle + i, uint32_t(counts[i])};
            c.text = texts[i];
            drawfx::gLog.push_back(std::move(c));
        }
    }
};
inline Pool gPool;

namespace detail {
inline uint32_t PoolIntern(Fake &, const uint32_t *a)
{
    const char *text = reinterpret_cast<const char *>(uintptr_t(a[0]));
    return text ? gPool.Handle(text) : kNoHandle;
}
inline uint32_t PoolAddRef(Fake &, const uint32_t *a)
{
    const int i = gPool.Index(a[0]);
    if (i >= 0) ++gPool.counts[i];
    return 0;
}
inline uint32_t PoolRelease(Fake &, const uint32_t *a)
{
    const int i = gPool.Index(a[0]);
    if (i >= 0) --gPool.counts[i];
    return 0;
}
inline uint32_t PoolText(Fake &, const uint32_t *a)
{
    static const char kEmpty[] = "";
    const int i = gPool.Index(a[0]);
    return drawfx::P(i >= 0 ? gPool.texts[i].c_str() : kEmpty);
}
} // namespace detail

inline void Pool::Reset(drawfx::Cells &cells)
{
    fake.Reset<kPool>(400);
    fake.On(5, &detail::PoolIntern);
    fake.On(6, &detail::PoolAddRef);
    fake.On(7, &detail::PoolRelease);
    fake.On(8, &detail::PoolText);
    for (unsigned i = 0; i < size; ++i) texts[i].clear();
    size = 0;
    for (int32_t &c : counts) c = 0;
    cells.Set(kCell, &fake);
}

inline uint32_t Pool::Handle(const char *text)
{
    for (unsigned i = 0; i < size; ++i)
        if (texts[i] == text) return kFirstHandle + i;
    if (size == kMax) return kNoHandle;
    // The pool's own storage, not the code under test's (difftest::AllocationCounter): Reset keeps each text's buffer,
    // so a text past the small-string buffer would count only on the first side that interns it.
    difftest::NotCounted bookkeeping;
    texts[size] = text;
    counts[size] = 0;
    return kFirstHandle + size++;
}

// ---- the persist reader and writer ----
struct Reader {
    static constexpr unsigned kMax = 32;
    Fake fake;
    Fake typeNames[4]; // what ReadTypeInfo hands out (counted), in turn
    // (Was 4 until lane OK: a partition node's read takes 6 records, 7 with cSceneNode's cSGResource part, and the
    // 5th and later overwrote versionCount / versionAt. The type names stay 4: later records get none.)
    uint32_t versions[kMax];
    unsigned versionCount = 0, versionAt = 0;
    uint32_t words[kMax];
    unsigned wordCount = 0, wordAt = 0;
    void *refs[kMax];
    unsigned refCount = 0, refAt = 0;
    const char *texts[8];
    unsigned textCount = 0, textAt = 0;

    void Reset();
    Reader &Version(uint32_t v) { return versions[versionCount++] = v, *this; }
    Reader &Word(uint32_t w) { return words[wordCount++] = w, *this; }
    Reader &Float(float f) { return Word(std::bit_cast<uint32_t>(f)); }
    Reader &Ref(void *p) { return refs[refCount++] = p, *this; }
    Reader &Text(const char *t) { return texts[textCount++] = t, *this; }
    void NoteNames() const
    {
        for (const Fake &n : typeNames) drawfx::Note("type name refs", {n.id, uint32_t(n.refs)});
    }
};
inline Reader gReader;

namespace detail {
inline uint32_t ReaderUint32(Fake &, const uint32_t *)
{
    Reader &r = gReader;
    return r.wordAt < r.wordCount ? r.words[r.wordAt++] : 0;
}
// (GetBool and GetFloat32 read the same queue: a float is its bits.)
inline uint32_t ReaderFloat(Fake &f, const uint32_t *a) { return ReaderUint32(f, a); }
// GetGZStr(string): the next text through the string's FromChar(text, length) (cIGZString slot 3).
inline uint32_t ReaderString(Fake &, const uint32_t *a)
{
    Reader &r = gReader;
    const char *text = r.textAt < r.textCount ? r.texts[r.textAt++] : "";
    void *s = reinterpret_cast<void *>(uintptr_t(a[0]));
    using FromCharFn = void(__thiscall *)(void *, const char *, uint32_t);
    reinterpret_cast<FromCharFn>((*static_cast<void ***>(s))[3])(s, text, uint32_t(std::strlen(text)));
    return 1;
}
// GetObjectRef(iid, out): the next object, counted through its AddRef; true when there is one.
inline uint32_t ReaderRef(Fake &, const uint32_t *a)
{
    Reader &r = gReader;
    void *p = r.refAt < r.refCount ? r.refs[r.refAt++] : nullptr;
    if (p) static_cast<cIGZUnknown *>(p)->AddRef();
    *reinterpret_cast<void **>(uintptr_t(a[1])) = p;
    return p != nullptr;
}
// ReadTypeInfo(record): a counted name, 0, the next version.
inline uint32_t ReaderTypeInfo(Fake &, const uint32_t *a)
{
    Reader &r = gReader;
    auto *record = reinterpret_cast<::cPersistTypeInfo *>(uintptr_t(a[0]));
    const unsigned i = r.versionAt < r.versionCount ? r.versionAt++ : r.versionCount;
    Fake *name = i < 4 ? &r.typeNames[i] : nullptr;
    if (name) ++name->refs;
    record->mName = reinterpret_cast<cIGZUnknown *>(name);
    record->mWord4 = 0;
    record->mVersion = i < r.versionCount ? r.versions[i] : 0;
    return a[0];
}
} // namespace detail

inline void Reader::Reset()
{
    fake.Reset<kReader>(410);
    fake.On(4, &detail::ReaderUint32);
    fake.On(10, &detail::ReaderUint32);
    fake.On(11, &detail::ReaderFloat);
    fake.On(22, &detail::ReaderString);
    fake.On(25, &detail::ReaderRef);
    fake.On(26, &detail::ReaderTypeInfo);
    for (unsigned i = 0; i < 4; ++i) typeNames[i].Reset<kUnknown>(440 + i);
    versionCount = versionAt = wordCount = wordAt = refCount = refAt = textCount = textAt = 0;
}

// Notes a type-info record: its name's text (cIGZString slots 6 / 7), word 4 and version.
inline void NoteTypeInfo(const char *label, const ::cPersistTypeInfo *info)
{
    drawfx::Call c;
    c.note = label;
    if (!info) {
        c.words.push_back(0);
    } else {
        c.words = {1, info->mWord4, info->mVersion, info->mName != nullptr};
        if (void *s = info->mName) {
            using DataFn = const char *(__thiscall *)(void *);
            using LenFn = uint32_t(__thiscall *)(void *);
            void **vt = *static_cast<void ***>(s);
            const char *data = reinterpret_cast<DataFn>(vt[6])(s);
            c.text.assign(data ? data : "", data ? reinterpret_cast<LenFn>(vt[7])(s) : 0);
        }
    }
    drawfx::gLog.push_back(std::move(c));
}

struct Writer {
    Fake fake;
    void Reset();
};
inline Writer gWriter;

namespace detail {
inline uint32_t WriterTypeInfo(Fake &, const uint32_t *a)
{
    NoteTypeInfo("written type", reinterpret_cast<const ::cPersistTypeInfo *>(uintptr_t(a[0])));
    return 1;
}
} // namespace detail

inline void Writer::Reset()
{
    fake.Reset<kWriter>(420);
    fake.On(28, &detail::WriterTypeInfo);
    fake.Returns(4, 1u);
    fake.Returns(10, 1u);
    fake.Returns(11, 1u);
    fake.Returns(23, 1u);
    fake.Returns(26, 1u);
    fake.Returns(27, 1u);
}

// ---- the resource manager and its segments (lane OE) ----
// gResources.framework sits in the framework cell (g_Framework 0x10c9378); its AddToTick / RemoveFromTick (9 / 10) log
// the service (lane OM); its GetSystemService (5) answers the resource
// manager service (0x056b906e, cIGZPersistResourceManager 0x656b8efc) with gResources.manager, counted, while `up`. The
// manager's FindSegmentWithKey (34) answers the segments in turn (one round in four none: null written, false); its
// slots 9, 10 and 14 (what cSGResource's submit bodies call) return their programmed values. A segment's slot 21 (the
// key forwarded by cSGResource slot 24) returns 0x2100 + its id.
inline constexpr Slot kFrameworkSlots[] = {
    SGFX_UNKNOWN_SLOTS,
    {0, Ret::Int, false, "", ""},                          // 3
    {0, Ret::Int, false, "", ""},                          // 4
    {3, Ret::Int, true, "GetSystemService", "uui"},        // 5
    {0, Ret::Int, false, "", ""},                          // 6
    {0, Ret::Int, false, "", ""},                          // 7
    {0, Ret::Int, false, "", ""},                          // 8
    {1, Ret::Int, true, "AddToTick", "i"},                 // 9 (lane OM: the scene graph system's Init)
    {1, Ret::Int, true, "RemoveFromTick", "i"},            // 10 (its Shutdown)
};
inline constexpr drawfx::Iface kFramework{"FrameWork", 11, kFrameworkSlots};

#define SGFX_UNSETTLED {0, Ret::Int, false, "", ""}
inline constexpr Slot kResourceManagerSlots[] = {
    SGFX_UNKNOWN_SLOTS, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, // 3-8
    {2, Ret::Int, true, "Slot9", "W4i"},                   // 9 (key, resource)
    {1, Ret::Int, true, "ForgetResource", "W4"},           // 10 (key)
    SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED,        // 11-13
    {3, Ret::Int, true, "Slot14", "iW4i"},                 // 14 (resource, key, segment)
    SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, // 15-21
    SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, // 22-28
    SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED,                                 // 29-33
    {2, Ret::Int, true, "FindSegmentWithKey", "W4i"},      // 34 (key, out)
    SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, // 35-41
    SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED,                                                                 // 42-44
    {0, Ret::Int, true, "AdvanceCacheGenerations", ""},    // 45 (lane OM: the scene graph system's Shutdown)
};
inline constexpr drawfx::Iface kResourceManager{"ResourceManager", 46, kResourceManagerSlots};

inline constexpr Slot kSegmentSlots[] = {
    SGFX_UNKNOWN_SLOTS, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, // 3-8
    SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, // 9-15
    SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED,                                 // 16-20
    {1, Ret::Int, true, "Slot21", "W4"},                   // 21 (key)
};
inline constexpr drawfx::Iface kSegment{"Segment", 22, kSegmentSlots};

// The scene graph system (lane OK; cSceneGraphSystem's +0 face, cISceneGraphSystemPrivate tentative): a scene node's
// constructor adds itself (13), its destructor removes itself (14). The constructor's node is not registered yet (each
// side's heap puts it elsewhere), so 13 logs only that it has one; Resources::sceneNodes keeps the set, and a test notes
// a node's place in it.
inline constexpr Slot kSceneGraphSystemSlots[] = {
    SGFX_UNKNOWN_SLOTS, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, // 3-8
    SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED, SGFX_UNSETTLED,                                                 // 9-12
    {1, Ret::Int, true, "AddSceneNode", "W0"},            // 13 (node)
    {1, Ret::Int, true, "RemoveSceneNode", "i"},          // 14 (node)
};
inline constexpr drawfx::Iface kSceneGraphSystem{"SceneGraphSystem", 15, kSceneGraphSystemSlots};
#undef SGFX_UNSETTLED

struct Resources {
    static constexpr uint32_t kFrameworkCell = 0x10c9378;
    static constexpr uint32_t kService = 0x056b906e, kIid = 0x656b8efc;
    static constexpr uint32_t kSystemService = 0xe5074b6b, kSystemIid = 0x894978cd; // the scene graph system (lane OK)
    Fake framework, manager, segments[3], system;
    bool up = true;
    bool systemUp = true; // the scene graph system answered (counted) while set
    unsigned found = 0;
    std::vector<uint32_t> sceneNodes; // the system's set: the nodes added (13) and not removed (14), in order
    // The index of a node in sceneNodes, or 0xffffffff.
    uint32_t SceneNodeIndex(const void *node) const
    {
        for (size_t i = 0; i < sceneNodes.size(); ++i)
            if (sceneNodes[i] == uint32_t(uintptr_t(node))) return uint32_t(i);
        return 0xffffffffu;
    }
    // Fresh fakes (ids 460, 461, 462, 470-472), the framework in its cell (restored when `cells` ends).
    void Reset(drawfx::Cells &cells);
    void NoteRefs() const
    {
        drawfx::Note("manager refs", uint32_t(manager.refs));
        for (const Fake &s : segments) drawfx::Note("segment refs", {s.id, uint32_t(s.refs)});
    }
};
inline Resources gResources;

namespace detail {
inline uint32_t GetSystemService(Fake &, const uint32_t *a)
{
    Resources &r = gResources;
    if (a[0] == Resources::kSystemService && a[1] == Resources::kSystemIid && r.systemUp) {
        ++r.system.refs;
        *reinterpret_cast<void **>(uintptr_t(a[2])) = &r.system;
        return 1;
    }
    if (a[0] != Resources::kService || a[1] != Resources::kIid || !r.up) return 0;
    ++r.manager.refs;
    *reinterpret_cast<void **>(uintptr_t(a[2])) = &r.manager;
    return 1;
}
inline uint32_t FindSegment(Fake &, const uint32_t *a)
{
    Resources &r = gResources;
    const unsigned k = r.found++ % 4;
    Fake *segment = k < 3 ? &r.segments[k] : nullptr;
    if (segment) ++segment->refs;
    *reinterpret_cast<void **>(uintptr_t(a[1])) = segment;
    return segment != nullptr;
}
inline uint32_t SegmentKey(Fake &self, const uint32_t *) { return 0x2100 + self.id; }
inline uint32_t AddSceneNode(Fake &, const uint32_t *a)
{
    gResources.sceneNodes.push_back(a[0]);
    return 0;
}
inline uint32_t RemoveSceneNode(Fake &, const uint32_t *a)
{
    std::vector<uint32_t> &set = gResources.sceneNodes;
    for (size_t i = 0; i < set.size(); ++i)
        if (set[i] == a[0]) {
            set.erase(set.begin() + ptrdiff_t(i));
            break;
        }
    return 0;
}
} // namespace detail

inline void Resources::Reset(drawfx::Cells &cells)
{
    framework.Reset<kFramework>(460);
    framework.On(5, &detail::GetSystemService);
    manager.Reset<kResourceManager>(461);
    manager.On(34, &detail::FindSegment);
    manager.Returns(9, 1u);
    manager.Returns(14, 1u);
    for (unsigned i = 0; i < 3; ++i) {
        segments[i].Reset<kSegment>(470 + i);
        segments[i].On(21, &detail::SegmentKey);
    }
    system.Reset<kSceneGraphSystem>(462);
    system.On(13, &detail::AddSceneNode);
    system.On(14, &detail::RemoveSceneNode);
    up = true;
    systemUp = true;
    found = 0;
    sceneNodes.clear();
    cells.Set(kFrameworkCell, &framework);
}

// ---- the clone context and a traversal ----
namespace detail {
inline uint32_t FindClone(Fake &self, const uint32_t *a)
{
    void *clone = reinterpret_cast<void *>(uintptr_t(self.value[0]));
    if (clone) static_cast<cIGZUnknown *>(clone)->AddRef();
    *reinterpret_cast<void **>(uintptr_t(a[2])) = clone;
    return 0;
}
} // namespace detail
// A clone context whose FindClone answers `clone` (counted through its AddRef, as the node's Clone releases it; null:
// none).
inline void MakeCloneContext(Fake &f, const void *clone)
{
    f.Reset<kCloneContext>(430);
    f.value[0] = drawfx::P(clone);
    f.On(3, &detail::FindClone);
}

inline constexpr Slot kTraversalSlots[] = {
    SGFX_UNKNOWN_SLOTS,
    {0, Ret::Int, false, "", ""},   // 3
    {1, Ret::Int, true, "Visit", "i"}, // 4
    {1, Ret::Int, true, "VisitTransform", "i"}, // 5
    {0, Ret::Int, false, "", ""}, // 6
    {1, Ret::Int, true, "VisitParticles", "i"}, // 7 (lane WQ: cTSParticlesRenderable's Accept)
    {1, Ret::Int, true, "VisitShapeRef", "i"}, // 8 (lane SN: the shape-reference node's Accept 0xa58950)
    {0, Ret::Int, false, "", ""}, // 9
    {1, Ret::Int, true, "VisitViewerRefRecursive", "i"}, // 10 (lane VF: cViewerRefNodeRecursive's Accept)
    {0, Ret::Int, false, "", ""}, // 11
    {1, Ret::Int, true, "VisitResource", "i"}, // 12 (lane OG: the resource nodes' Accept)
    {1, Ret::Int, true, "VisitPartition", "i"}, // 13 (lane OI: the partition nodes' Accept, shared 0xa4f700)
    {1, Ret::Int, true, "VisitScene", "i"},     // 14 (lane OK: cSceneNode's Accept 0xa4f710)
    {1, Ret::Int, true, "VisitExplicitBound", "i"}, // 15
};
inline constexpr drawfx::Iface kTraversal{"Traversal", 16, kTraversalSlots};
// A cICompositionTreeTraversal whose Visit (slot 4) returns `result`.
inline void MakeTraversal(Fake &f, uint32_t id, uint32_t result)
{
    f.Reset<kTraversal>(id);
    f.Returns(4, result);
    f.Returns(5, result + 1);
    f.Returns(15, result + 2);
    f.Returns(12, result + 3);
    f.Returns(13, result + 4);
    f.Returns(14, result + 5);
    f.Returns(8, result + 6);
    f.Returns(10, result + 7);
    f.Returns(7, result + 8);
}

// ---- events through a real connection manager ----
namespace detail {
inline uint32_t ListenerEvent(Fake &, const uint32_t *a)
{
    NoteBody("heard body", reinterpret_cast<const void *>(uintptr_t(a[2])));
    return 0;
}
} // namespace detail
// A listener fake (kBehavior): a behaviour to the connection manager (it answers cIAbstractBehaviorType 0xfb859324
// with itself), noting the bodies it hears (slot 3) and answering queries (slot 4) with `answer`.
inline void MakeListener(Fake &f, uint32_t id, bool answer = true)
{
    f.Reset<kBehavior>(id);
    f.AddFace(nGZEvent::cIAbstractBehaviorType::kIid, f);
    f.On(3, &detail::ListenerEvent);
    f.Returns(4, uint32_t(answer));
}

// Our connection manager in a behaviour's event-manager cell (restored by `cells`); Drop destroys it.
struct Manager {
    nGZEvent::cConnectionManager *manager = nullptr;
    void Use(drawfx::Cells &cells, uint32_t cell)
    {
        manager = nGZEvent::cConnectionManagerAdapters::Construct(::operator new(nGZEvent::cConnectionManager::kSize),
                                                                  nullptr);
        drawfx::AddRange(manager, nGZEvent::cConnectionManager::kSize, 0x7e000000);
        cells.Set(cell, manager);
    }
    nGZEvent::cIConnectionManager *operator->() const { return static_cast<nGZEvent::cIConnectionManager *>(manager); }
    // `source`'s `event` heard by `listener` as `heardAs`.
    void Listen(void *source, uint32_t event, Fake &listener, uint32_t heardAs) const
    {
        (*this)->Connect(static_cast<cIGZUnknown *>(source), event, reinterpret_cast<cIGZUnknown *>(&listener), heardAs);
    }
    void Drop()
    {
        if (!manager) return;
        nGZEvent::cConnectionManagerAdapters::Destruct(manager, nullptr);
        ::operator delete(manager);
        manager = nullptr;
    }
};

// A behaviour's event registry (a function-local static std::map, game code reached through OriginalFunction on both
// sides) placed at its static with a few records, its guards marked done, for the scope (everything restored at its
// end). Without it the registry builds itself on first use through the original's thread-safe-static machinery and
// allocator, which the harness has not initialised (the runner dies of heap corruption).
struct RegistryStatics {
    uint32_t map;         // the std::map (8 bytes)
    uint32_t mapGuard;    // its _Init_thread_header epoch
    uint32_t once;        // the holder's once byte
    uint32_t holderGuard; // the holder's epoch
};
static_assert(sizeof(nGZEvent::cEventAttributeMap) == 8, "the game's std::map: head, size");
// The node family's (0x263f30, through 0x255860).
inline constexpr RegistryStatics kNodeRegistry{0x10cca00, 0x10cca08, 0x10cca28, 0x10cca30};
// The transform family's (0x2640f0): the same shape, its own statics.
inline constexpr RegistryStatics kTransformRegistry{0x10cca0c, 0x10cca14, 0x10cca34, 0x10cca3c};
// Records: the connection-change event, the subordinate-list event and one other.
inline constexpr uint32_t kRegistryIds[] = {0x7b54c99c, 0x1d05597b, 0x1234};

class RegistryScope {
public:
    explicit RegistryScope(const RegistryStatics &at) : at_(at)
    {
        std::memcpy(saved_, GameAddress<void>(at.map), sizeof(saved_));
        savedMapGuard_ = *GameAddress<int32_t>(at.mapGuard);
        savedOnce_ = *GameAddress<uint8_t>(at.once);
        savedHolderGuard_ = *GameAddress<int32_t>(at.holderGuard);
        map_ = ::new (GameAddress<void>(at.map)) nGZEvent::cEventAttributeMap;
        *GameAddress<int32_t>(at.mapGuard) = INT_MIN;
        *GameAddress<uint8_t>(at.once) = 1;
        *GameAddress<int32_t>(at.holderGuard) = INT_MIN;
        static const char *const kNames[] = {"conn", "subordinates", "other"};
        for (uint32_t i = 0; i < 3; ++i)
            (*map_)[kRegistryIds[i]] = {kRegistryIds[i], kNames[i], i, 0x100u + i, 0x200u + i, 0x300u + i};
        // The registries are ours now (lane SM): the stand-in keeps both sides, the image's function and ours, on the map
        // planted above, as the original alone read it before.
        if (at.map == kNodeRegistry.map)
            stand_ = std::make_unique<shape::StandIn>(0x263f30, &nGZSceneGraph::cCompositionTreeNodeBehaviorTraits::RegistryFor,
                                                      &pdfx::StandRegistry<kNodeRegistry.map>);
        else if (at.map == 0x10f6138) // the standard lights' (0xa49210)
            stand_ = std::make_unique<shape::StandIn>(0xa49210, &nGZSceneGraph::cStandardLightBehaviorTraits::RegistryFor,
                                                      &pdfx::StandRegistry<0x10f6138>);
        else if (at.map == 0x10f60e8) // the geometry builders' (0xa453a0)
            stand_ = std::make_unique<shape::StandIn>(0xa453a0, &nGZSceneGraph::cGeometryBuilderBehaviorTraits::RegistryFor,
                                                      &pdfx::StandRegistry<0x10f60e8>);
        else if (at.map == kTransformRegistry.map)
            stand_ = std::make_unique<shape::StandIn>(0x2640f0, &nGZSceneGraph::cTransformNodeBehaviorTraits::RegistryFor,
                                                      &pdfx::StandRegistry<kTransformRegistry.map>);
        else if (at.map == 0x10f62d0) // the shape-reference nodes' (0xa585a0, lane TF)
            stand_ = std::make_unique<shape::StandIn>(0xa585a0, &nGZSceneGraph::cShapeRefNodeBehaviorTraits::RegistryFor,
                                                      &pdfx::StandRegistry<0x10f62d0>);
        else if (at.map == 0x10f63d0) // the viewer-reference nodes' (0xa68db0)
            stand_ = std::make_unique<shape::StandIn>(0xa68db0, &nGZSceneGraph::cViewerRefNodeBehaviorTraits::RegistryFor,
                                                      &pdfx::StandRegistry<0x10f63d0>);
    }
    ~RegistryScope()
    {
        stand_.reset();
        std::destroy_at(map_);
        std::memcpy(GameAddress<void>(at_.map), saved_, sizeof(saved_));
        *GameAddress<int32_t>(at_.mapGuard) = savedMapGuard_;
        *GameAddress<uint8_t>(at_.once) = savedOnce_;
        *GameAddress<int32_t>(at_.holderGuard) = savedHolderGuard_;
    }
    RegistryScope(const RegistryScope &) = delete;
    RegistryScope &operator=(const RegistryScope &) = delete;

private:
    RegistryStatics at_;
    uint8_t saved_[sizeof(nGZEvent::cEventAttributeMap)];
    int32_t savedMapGuard_;
    uint8_t savedOnce_;
    int32_t savedHolderGuard_;
    nGZEvent::cEventAttributeMap *map_;
    std::unique_ptr<shape::StandIn> stand_;
};

// ---- real nodes ----
using Factory = nGZSceneGraph::cICompositionTreeNode *(__cdecl *)();
constexpr uint32_t kNodeTag = 0x7c000000; // kNodeTag | index << 8 | offset

struct Nodes {
    static constexpr unsigned kMax = 16;
    nGZSceneGraph::cICompositionTreeNode *at[kMax] = {};
    unsigned count = 0;
    uint32_t size = 0;

    // n nodes of `size` bytes from `make`, each counted once by the test and registered as a range.
    void Make(Factory make, unsigned n, uint32_t nodeSize)
    {
        count = n;
        size = nodeSize;
        for (unsigned i = 0; i < n; ++i) {
            at[i] = make();
            at[i]->AddRef();
            drawfx::AddRange(at[i], nodeSize, kNodeTag | (i << 8));
        }
    }
    // parents[i] >= 0: node i added to that node (in index order; children go to the front).
    void Link(const int *parents)
    {
        for (unsigned i = 0; i < count; ++i)
            if (parents[i] >= 0) at[parents[i]]->AddChild(at[i]);
    }
    nGZSceneGraph::cICompositionTreeNode *operator[](unsigned i) const { return at[i]; }
    // Each node's links, flags, extension and tag handle through its slots.
    void NoteShape(const char *label) const
    {
        for (unsigned i = 0; i < count; ++i) {
            nGZSceneGraph::cICompositionTreeNode *n = at[i];
            drawfx::Note(label, {i, drawfx::Norm(drawfx::P(n->GetParent())), drawfx::Norm(drawfx::P(n->GetPrevSibling())),
                                 drawfx::Norm(drawfx::P(n->GetNextSibling())), drawfx::Norm(drawfx::P(n->GetFirstChild())),
                                 n->GetFlags(), n->HasChildren(), drawfx::Norm(drawfx::P(n->GetFirstExtension())),
                                 n->GetTagHandle()});
        }
    }
    // Each node's words, normalised, the words in `skip` (bit per word: vtables) left out. Nodes past 64 words keep
    // their words from the 64th on.
    void NoteWords(const char *label, uint64_t skip) const
    {
        for (unsigned i = 0; i < count; ++i) {
            drawfx::Call c;
            c.note = label;
            const uint32_t *w = reinterpret_cast<const uint32_t *>(at[i]);
            for (unsigned k = 0; k < size / 4; ++k)
                if (k >= 64 || !((skip >> k) & 1)) c.words.push_back(drawfx::Norm(w[k]));
            drawfx::gLog.push_back(std::move(c));
        }
    }
    // The test's references released, last node first.
    void Release()
    {
        for (unsigned i = count; i-- > 0;)
            if (at[i]) at[i]->Release();
        count = 0;
    }
};

// ---- the comparison ----
// The side and seed CheckSides is running ("what: original, seed 3"); MarkStep adds a step to it, so a runner that dies
// mid-seed reports the step (the harness keeps the last mark).
inline char gSideMark[96];
inline void MarkStep(difftest::Context &ctx, const char *step)
{
    char mark[160];
    std::snprintf(mark, sizeof(mark), "%s, %s", gSideMark, step);
    ctx.Mark(mark);
}

// For each seed: build(seed, cells) then run(original, rng) on each side (rng seeded alike), the logs compared.
// `build` resets the fakes (and may Set cells, restored when the check ends); `run` calls the original or ours and
// notes what it wants compared.
template <class Build, class Run>
void CheckSides(difftest::Context &ctx, const char *what, unsigned seeds, Build &&build, Run &&run)
{
    drawfx::Cells cells;
    for (uint32_t seed = 0; seed < seeds; ++seed) {
        auto side = [&](bool original) {
            std::snprintf(gSideMark, sizeof(gSideMark), "%s: %s, seed %u", what, original ? "original" : "ours", seed);
            ctx.Mark(gSideMark);
            return drawfx::Side([&] { build(seed, cells); },
                                [&] {
                                    std::mt19937 rng(seed * 7919u + 13u);
                                    run(original, rng);
                                });
        };
        auto a = side(true);
        auto b = side(false);
        std::vector<std::string> errors = a.second;
        errors.insert(errors.end(), b.second.begin(), b.second.end());
        drawfx::Same(ctx, a.first, b.first, what, seed, errors);
    }
}

// A string (or null) as a note.
inline void NoteText(const char *label, const char *text)
{
    drawfx::Call c;
    c.note = label;
    c.words.push_back(text != nullptr);
    if (text) c.text = text;
    drawfx::gLog.push_back(std::move(c));
}

#undef SGFX_BEHAVIOR_SLOTS
#undef SGFX_UNKNOWN_SLOTS

} // namespace sgfx
