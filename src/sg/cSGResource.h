#pragma once
// nGZSceneGraph::cSGResource as a whole class (lane OE, scenegraph N1): the scene graph's persist resource, the base of
// every resource class of the scene graph (cShape, cGeometryDataContainer, cMaterialDefinition, cImageData, cResourceNode,
// cSceneNode, cGeometryNode, the lights, ~17 world-lot nodes, three nGZSceneDBAnim resources). Standalone shape with a
// virtual base (docs/standalone-shape.md, "Virtual bases and vtordisp"):
//
//   class cSGResource : public cGZPersistResource, public virtual cISGResource
//
//   +0x00 cGZPersistResource (vtable 0xe695a4, 13 slots: QI 0xa3c4d0, AddRef / Release forwarding to the base, the key
//         slots, the deleting destructor 0xa3c380); +0x04 the count, +0x0c the key
//   +0x1c the vbptr (vbtable {-0x1c, 0x14} in cSGResource alone)
//   +0x20 mName      the resource's name: a string-pool handle (ServiceCell; empty is -1)
//   +0x24 mSegment   the database segment the resource came from (counted; the resource manager's slot 34 finds it)
//   +0x28 mHasKey    set once the name gave a key (Read, the name setters), what slot 21 answers
//   +0x29 mFlag      slots 16 / 23
//   +0x2c the vtordisp, +0x30 the virtual base cISGResource (vtable 0xe695dc, 25 slots) in cSGResource alone; a derived
//         class moves both to its end (cResourceNode +0x5c / +0x60, cGeometryDataContainer +0x7c / +0x80).
//
// The virtual base's slot bodies take `this` = object + 0x30 (the virtual base where cSGResource alone puts it) wherever
// the virtual base really is: the vtordisp thunks (`sub ecx, [ecx-4]; [sub ecx, n;] jmp`) deliver it. Most of those
// bodies were reimplemented hook-shaped before the class existed (src/gz/cSGResource.cpp's cSGResourceSubobject and
// src/sg/SGResourceHelpers.cpp's cSGSecondary, hooked at their RVAs and verified; some byte matches); the overrides here
// call them on Secondary() (object + 0x30), so each body exists once. The rest are here.
//
// Constructors (decision, lane OE): the game's derived constructors call 0xa3c270 with the most-derived flag 0, as a base
// part. The hook on 0xa3c270 is our compiler's own constructor (SGResourceBasePartConstructor below), which has the same
// ABI (thiscall, the flag on the stack, ret 4) and does what the game's does with flag 0: our base part, our vtables
// through the vbptr the derived class set, the vtordisp from the vbtable. tests/sg_resource_class.cpp proves it, alone and
// inside a game-derived constructor (cResourceNode's).
#include "gz/cGZPersistResource.h"
#include "rz/StaticGetters2.h" // ServiceCell
#include "sg/SGNodeInterfaces.h"

#include <cstddef>
#include <cstdint>

namespace ifaces {
class cIGZPersistReader;
class cIGZPersistWriter;
} // namespace ifaces

// cIGZPersistResource2 (global, as its RTTI): a serializable referent that is also a persist resource. Which of the
// virtual base's slots 7-24 it declares and which cISGResource adds is not known (no class implements one without the
// other); the two that hand out the resource face are put here (tentative).
class cIGZPersistResource2 : public cIGZPersistSerializableReferent {
public:
    virtual cIGZPersistResource *GetPersistResourceConst() = 0; // 7
    virtual cIGZPersistResource *GetPersistResource() = 0;      // 8
};

namespace nGZSceneGraph {

// nGZSceneGraph::cISGResource (interface id 0x9b4bbca3): the scene graph's resource face, the virtual base of every
// resource class. Names follow the hook-shaped bodies' (provisional where they say so); slots 18 / 19 are each derived
// class's own (cResourceNode answers two static records).
class cISGResource : public cIGZPersistResource2 {
public:
    static constexpr uint32_t kIid = 0x9b4bbca3;

    virtual cISGResource *GetSubResource(uint32_t index) = 0;                       // 9: this resource for 0, else null
    virtual uint8_t SubmitKeyToService() = 0;                                         // 10 (provisional)
    virtual int32_t CompareIdentity(cIGZUnknown *other) = 0;                         // 11: 0 when the same object
    virtual uint8_t AssignAndSubmit(const char *name, cIGZUnknown *segment) = 0;      // 12 (provisional)
    virtual uint8_t ResetAndSubmit(uint32_t group, uint32_t instance) = 0;             // 13 (provisional)
    virtual uint8_t OpenAssignAndSubmit(const char *name, uint32_t group, cIGZUnknown *segment) = 0; // 14 (provisional)
    virtual bool SetSegment(cIGZUnknown *segment) = 0;                               // 15: counted
    virtual void SetFlag(uint8_t value) = 0;                                         // 16
    virtual const char *GetName() = 0;                                               // 17: the name's text
    virtual const void *GetRecord18() = 0;                                           // 18: the derived class's
    virtual const void *GetRecord19() = 0;                                           // 19: the derived class's
    virtual cIGZUnknown *GetSegment() = 0;                                           // 20
    virtual uint8_t HasKey() = 0;                                                     // 21
    virtual cGZPersistResourceKey *CopyKeyTo(cGZPersistResourceKey *out) = 0;        // 22: returns out
    virtual uint8_t GetFlag() = 0;                                                   // 23
    virtual uint32_t ForwardKey() = 0;                                               // 24: the segment's slot 21, or -1
};

class cSGResource : public cGZPersistResource, public virtual cISGResource {
public:
    static constexpr uint32_t kVersion = 2;                 // the type-info record's version (0xa3ce10)
    static constexpr uint32_t kIidReferent = 0x5b0aaa74;    // cIGZPersistSerializableReferent: the virtual base, counted
    static constexpr uint32_t kIidReferent2 = 0xbb11a53e;   // the same
    static constexpr uint32_t kIidPersistResource2 = 0x9b11bf70; // cIGZPersistResource2 (tentative): the same
    static constexpr uint32_t kNoName = 0xffffffff;
    static constexpr int kSecondaryOffset = 0x30; // where the slot bodies' `this` is, from the cSGResource part

    cSGResource();                       // 0xa3c270 (only ever a base part: the class is abstract)
    ~cSGResource() override;             // 0xa3c2f0; primary slot 12 the deleting destructor 0xa3c380 (delete 0x34)
    cSGResource(const cSGResource &) = delete;
    cSGResource &operator=(const cSGResource &) = delete;

    // ---- cIGZUnknown (both faces; the virtual base's are vtordisp thunks 0xa3c4b8 / 0xa3c3a0 / 0xa3c706) ----
    bool QueryInterface(uint32_t iid, void **out) override; // 0xa3c4d0
    uint32_t AddRef() override;                             // 0x5522b0 (folded: jmp to the base's)
    uint32_t Release() override;                            // 0x556090 (folded: jmp to the base's)

    // ---- cIGZPersistSerializableReferent (the virtual base's 3-6) ----
    void Read(ifaces::cIGZPersistReader *reader) override;   // 0xa3c540
    cPersistTypeInfo GetTypeInfo() override;                  // 0xa3ce10
    void Write(ifaces::cIGZPersistWriter *writer) override;  // 0xa3ced0 (WriteResource; see there)
    uint32_t GetOwnerInfo() override;                         // 0xa3c750: the virtual base

    // ---- cIGZPersistResource2 / cISGResource ----
    cIGZPersistResource *GetPersistResourceConst() override; // 0xa3c3c0
    cIGZPersistResource *GetPersistResource() override;      // 0xa3c3c0
    cISGResource *GetSubResource(uint32_t index) override;   // 0xa3c720
    uint8_t SubmitKeyToService() override;                   // 0xa3c790
    int32_t CompareIdentity(cIGZUnknown *other) override;    // 0xa3c3d0
    uint8_t AssignAndSubmit(const char *name, cIGZUnknown *segment) override;            // 0xa3cbf0
    uint8_t ResetAndSubmit(uint32_t group, uint32_t instance) override;                                 // 0xa3c890
    uint8_t OpenAssignAndSubmit(const char *name, uint32_t group, cIGZUnknown *segment) override; // 0xa3ca00
    bool SetSegment(cIGZUnknown *segment) override;          // 0xa3cdd0
    void SetFlag(uint8_t value) override;                    // 0xa3c870
    const char *GetName() override;                          // 0xa3c4b0 (Dispatch8 on the name cell)
    cIGZUnknown *GetSegment() override;                      // 0x9e8850 (folded)
    uint8_t HasKey() override;                               // 0x9d9570 (folded)
    cGZPersistResourceKey *CopyKeyTo(cGZPersistResourceKey *out) override; // 0xa3c770
    uint8_t GetFlag() override;                              // 0xa3c4a0
    uint32_t ForwardKey() override;                          // 0xa3c6d0
    // 18 / 19 stay pure: each derived class answers its own.

    // 0xa3ced0's body: the type-info record, then the name's text; returns the writer's answer to the text (slot 23).
    // Write (the slot) returns nothing because the shared referent interface declares it so (sg/SGNodeInterfaces.h);
    // the game's derived classes call 0xa3ced0 directly and get this result through the adapter.
    uint32_t WriteResource(ifaces::cIGZPersistWriter *writer);

    // The slot bodies' `this` (object + 0x30), what the hook-shaped bodies take.
    void *Secondary() { return reinterpret_cast<uint8_t *>(this) + kSecondaryOffset; }

    ServiceCell mName{kNoName};          // +0x20
    cIGZUnknown *mSegment = nullptr;     // +0x24 counted
    uint8_t mHasKey = 0;                 // +0x28
    uint8_t mFlag = 0;                   // +0x29
};

// The RVA hooks' entry points (src/hook/tables/cSGResource.cpp). The game's derived classes call the destructor and
// QueryInterface with this = their cSGResource part, and Read / Write with this = that part + 0x30 (the static distance,
// never through the vbptr: C++ has no static_cast from a virtual base). Qualified calls, bodies inlined. None for the
// deleting destructor 0xa3c380 (only 0xe695a4 holds it: with the constructor and destructor hooked no object carries that
// vtable) nor GetTypeInfo 0xa3ce10 (called by 0xa3ced0 and 0xe695dc's thunk alone): in effect through their callers.
struct cSGResourceAdapters {
    using Self = cSGResource;
    static Self *Owner(void *face) { return reinterpret_cast<Self *>(static_cast<uint8_t *>(face) - Self::kSecondaryOffset); }

    static void __fastcall Destruct(Self *self, void *);                                         // 0xa3c2f0
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t iid, void **out);         // 0xa3c4d0
    static void __fastcall Read(void *face, void *, ifaces::cIGZPersistReader *reader);          // 0xa3c540
    static uint32_t __fastcall Write(void *face, void *, ifaces::cIGZPersistWriter *writer);     // 0xa3ced0
};

} // namespace nGZSceneGraph

// 0xa3c270's hook: the address of our constructor (`??0cSGResource@nGZSceneGraph@@QAE@XZ`, thiscall with the most-derived
// flag on the stack, ret 4), which C++ cannot name. A C name the linker resolves to the decorated one (/alternatename in
// cSGResourceClass.cpp; a wrong decoration fails the link). A code address, never data. The hook row takes its address
// directly, so the row stays a static initializer (a function call there would make the whole kHooks array dynamically
// initialized, and matchcheck then sees no row of the table).
extern "C" const uint8_t OE_cSGResource_Constructor;

namespace nGZSceneGraph {
inline const void *SGResourceBasePartConstructor() { return &OE_cSGResource_Constructor; }
} // namespace nGZSceneGraph
