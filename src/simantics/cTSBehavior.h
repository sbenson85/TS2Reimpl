#pragma once
#include "gz/cIGZUnknown.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class cIGZString;

// cTSBehavior (lane GU): an object's behaviour context. It holds the three sources of behaviour trees (BHAV resources) an
// object's code can call by id, each with its resource group: global trees (ids 0x100..0xfff), private trees (0x1000..0x1fff,
// the object's own) and semi-global trees (0x2000..0x2710); ids below 0x100 are primitives, answered by the primitive set
// at +0x24. It loads trees and constants (BCON) by id through the resource services, names trees and sources for the
// debugger, and keeps the debugger's breakpoints (a static list, game global 0x10da9f4) applied to the trees' nodes.
// RTTI: cTSBehavior : cITSBehavior : cIGZUnknown, all at +0 (one vtable, 0xdc916c, 51 slots). 0x28 bytes. RVAs 1.18.0.187.
//
// Standalone shape (docs/standalone-shape.md): a real C++ class, the compiler emits its vtable. Creation: only the COM
// factory 0x469e70 (class id 0x1b269450, registered by address in the COM director table, src/rz/COMDirectorData.cpp), which
// builds the object inline (no constructor function); the game's vtable is referenced only there and by the deleting
// destructor 0x469560. The hook on the factory (cTSBehavior::Create) is the class's switch.
//
// Tree kinds (slots 44..47): 0 primitive, 1 global, 2 private, 3 semi-global; base ids 0, 0x100, 0x1000, 0x2000.

// A tree source: the object (an object's resource file view) that holds one kind of trees: slot 11 its resource group,
// slot 14 its name (cIGZString out), slot 16 whether it can name trees, slot 19 finds a resource by type and instance,
// slot 22 looks one up by type and instance (the found flag out), slot 23 names a found resource. Called through its vtable.
using cTSBehaviorSource = cIGZUnknown;

// A breakpoint of the debugger (0x28 bytes): the source's name, the tree id and the node index. The list is a static
// std::vector in the image (0x10da9f4; built by its static initialiser, destroyed by 0xd92a80), shared with the original
// loader 0x46f290 (slot 41). Elements carry the game's cRZString vtable (compared through it, slot 15).
struct cTSBehaviorBreakpoint {
    union { cRZString mName; };   // +0x00
    int32_t mTreeId;              // +0x20 (a tree id sign-extended from 16 bits, or what the loader read)
    int32_t mNode;                // +0x24

    cTSBehaviorBreakpoint() : mTreeId(0), mNode(0) { mName.Construct(); }
    // A copy: the string's text (reference count 0), the id and the node (the original's inline copy at 0x46df8d).
    cTSBehaviorBreakpoint(const cTSBehaviorBreakpoint &other) : mTreeId(other.mTreeId), mNode(other.mNode)
    {
        mName.Construct(other.mName, 0, 0xffffffffu);
    }
    // Assignment as the original's erase loop (0x46d970): the text (std::string::assign of the other's data and size,
    // skipped on self-assignment), the id and the node; the reference count stays.
    cTSBehaviorBreakpoint &operator=(const cTSBehaviorBreakpoint &other)
    {
        if (this != &other) mName.mString.assign(other.mName.mString.data(), other.mName.mString.size());
        mTreeId = other.mTreeId;
        mNode = other.mNode;
        return *this;
    }
    ~cTSBehaviorBreakpoint() { mName.Destruct(); }
};
static_assert(sizeof(cTSBehaviorBreakpoint) == 0x28, "breakpoint layout");
static_assert(offsetof(cTSBehaviorBreakpoint, mTreeId) == 0x20 && offsetof(cTSBehaviorBreakpoint, mNode) == 0x24);
using cTSBehaviorBreakpoints = std::vector<cTSBehaviorBreakpoint>;

// The interface (the original's name, global as its RTTI): slots 3..50 after cIGZUnknown's.
class cITSBehavior : public cIGZUnknown {
public:
    virtual bool Init(uint32_t owner, cIGZUnknown *primitives, cTSBehaviorSource *global, cTSBehaviorSource *privateSource,
                      cTSBehaviorSource *semiGlobal) = 0;                             // 3
    virtual bool ApplyBreakpoints(uint32_t treeId) = 0;                               // 4: true when it dropped a stale one
    virtual void ApplyAllBreakpoints() = 0;                                           // 5
    virtual void SetGlobalSource(uint32_t group, cTSBehaviorSource *source) = 0;      // 6
    virtual void SetSemiGlobalSource(uint32_t group, cTSBehaviorSource *source) = 0;  // 7
    virtual void SetPrivateSource(uint32_t group, cTSBehaviorSource *source) = 0;     // 8
    virtual uint32_t GetGlobalGroup() = 0;                                            // 9 (folded getter)
    virtual uint32_t GetSemiGlobalGroup() = 0;                                        // 10 (folded getter)
    virtual uint32_t GetPrivateGroup() = 0;                                           // 11 (folded getter)
    virtual uint32_t GetGroupForId(int16_t treeId) = 0;                              // 12: 0 outside 0x100..0x2710
    virtual uint32_t GetGroupForKind(uint32_t kind) = 0;                              // 13
    virtual cTSBehaviorSource *GetGlobalSource() = 0;                                 // 14 (folded getter)
    virtual cTSBehaviorSource *GetSemiGlobalSource() = 0;                             // 15 (folded getter)
    virtual cTSBehaviorSource *GetPrivateSource() = 0;                                // 16 (folded getter)
    virtual cTSBehaviorSource *GetSourceForId(int16_t treeId) = 0;                   // 17
    virtual cTSBehaviorSource *GetSourceForKind(uint32_t kind) = 0;                   // 18
    virtual bool GetGlobalSourceName(cIGZString *out) = 0;                            // 19
    virtual bool GetSemiGlobalSourceName(cIGZString *out) = 0;                        // 20
    virtual bool GetPrivateSourceName(cIGZString *out) = 0;                           // 21
    virtual bool GetSourceName(uint32_t treeId, cIGZString *out) = 0;                 // 22
    virtual cIGZUnknown *GetTree(uint32_t treeId) = 0;                                // 23: the cached tree, not AddRef'd
    virtual bool CreateTree(uint32_t treeId, uint32_t iid, void **out, uint32_t name) = 0; // 24
    virtual bool UncacheTree(uint32_t treeId) = 0;                                    // 25: always true
    virtual bool SaveTree(uint32_t treeId) = 0;                                       // 26
    virtual bool ReloadTree(uint32_t treeId) = 0;                                     // 27
    virtual void Nothing28() = 0;                                                     // 28 (folded `ret`)
    virtual uint32_t GetConstants(uint32_t id, uint32_t iid, void **out) = 0;         // 29: the BCON resource
    virtual uint32_t GetTreeSize(uint32_t treeId) = 0;                                // 30: over the call graph
    virtual uint32_t FindTree(uint32_t treeId, bool skipGlobal, bool skipSemiGlobal) = 0; // 31: the found word
    virtual void DescribeNode(uint32_t a, cIGZUnknown *node, cIGZString *out) = 0;   // 32 (debug text)
    virtual void DescribePrimitive(uint32_t a, uint32_t b, cIGZString *out) = 0;     // 33
    virtual void GetTreeName(uint32_t treeId, cIGZString *out) = 0;                   // 34
    virtual void GetConstantsName(uint32_t id, cIGZString *out) = 0;                  // 35
    virtual uint16_t GetStringCount() = 0;                                            // 36
    virtual cIGZUnknown *GetPrimitives() = 0;                                         // 37 (folded getter)
    virtual uint32_t GetOwner() = 0;                                                  // 38 (folded getter)
    virtual bool CheckFramework(uint32_t a, uint32_t b, uint32_t c) = 0;              // 39
    virtual void SetBreakpoint(uint32_t treeId, uint8_t node, bool on) = 0;           // 40
    virtual void LoadBreakpoints(void *file) = 0;                                     // 41 (original 0x46f290)
    virtual void Nothing42() = 0;                                                     // 42 (folded `ret`)
    virtual void DumpTrees(cIGZUnknown *target) = 0;                                  // 43 (original 0x46c610 writes)
    virtual uint32_t GetBaseIdForKind(uint32_t kind) = 0;                             // 44
    virtual uint32_t GetMaxIdForKind(uint32_t kind) = 0;                              // 45
    virtual void GetKindName(uint32_t kind, cIGZString *out) = 0;                     // 46
    virtual uint32_t GetKindForId(uint32_t treeId) = 0;                               // 47
    virtual cITSBehavior *AsBehavior() = 0;                                           // 48 (folded: this)
    virtual cITSBehavior *AsBehavior2() = 0;                                          // 49 (folded: this)
    virtual ~cITSBehavior() = default;                                                // 50: deleting destructor
};

class cTSBehavior : public cITSBehavior {
public:
    static constexpr uint32_t kOriginalVTable = 0xdc916c;
    static constexpr uint32_t kVTableSlots = 51;
    static constexpr uint32_t kInterfaceId = 0x7b269499; // cITSBehavior
    static constexpr uint32_t kClassId = 0x1b269450;     // the COM class id of the factory 0x469e70
    static constexpr uint32_t kTreeType = 0x42484156;    // "BHAV"
    static constexpr uint32_t kConstantsType = 0x42434f4e; // "BCON"
    static constexpr uint32_t kTreeIid = 0x5b267c7c;     // a tree resource (cTSBehaviorTree::kIidTree)
    static constexpr uint32_t kBreakpoints = 0x10da9f4;  // the static breakpoint list (std::vector)
    static constexpr size_t kSize = 0x28;

    // The factory 0x469e70 (__cdecl, no arguments): `new` and the fields; +0x0c and +0x20 left as the heap had them.
    static cTSBehavior *Create();
    cTSBehavior();
    cTSBehavior(const cTSBehavior &) = delete;
    cTSBehavior &operator=(const cTSBehavior &) = delete;
    ~cTSBehavior() override; // slot 50: 0x469560

    // cIGZUnknown
    bool QueryInterface(uint32_t riid, void **out) override; // 0x46d020
    uint32_t AddRef() override;                              // folded 0x148fb0
    uint32_t Release() override;                             // folded 0x46dba0

    bool Init(uint32_t owner, cIGZUnknown *primitives, cTSBehaviorSource *global, cTSBehaviorSource *privateSource,
              cTSBehaviorSource *semiGlobal) override;
    bool ApplyBreakpoints(uint32_t treeId) override;
    void ApplyAllBreakpoints() override;
    void SetGlobalSource(uint32_t group, cTSBehaviorSource *source) override;
    void SetSemiGlobalSource(uint32_t group, cTSBehaviorSource *source) override;
    void SetPrivateSource(uint32_t group, cTSBehaviorSource *source) override;
    uint32_t GetGlobalGroup() override;
    uint32_t GetSemiGlobalGroup() override;
    uint32_t GetPrivateGroup() override;
    uint32_t GetGroupForId(int16_t treeId) override;
    uint32_t GetGroupForKind(uint32_t kind) override;
    cTSBehaviorSource *GetGlobalSource() override;
    cTSBehaviorSource *GetSemiGlobalSource() override;
    cTSBehaviorSource *GetPrivateSource() override;
    cTSBehaviorSource *GetSourceForId(int16_t treeId) override;
    cTSBehaviorSource *GetSourceForKind(uint32_t kind) override;
    bool GetGlobalSourceName(cIGZString *out) override;
    bool GetSemiGlobalSourceName(cIGZString *out) override;
    bool GetPrivateSourceName(cIGZString *out) override;
    bool GetSourceName(uint32_t treeId, cIGZString *out) override;
    cIGZUnknown *GetTree(uint32_t treeId) override;
    bool CreateTree(uint32_t treeId, uint32_t iid, void **out, uint32_t name) override;
    bool UncacheTree(uint32_t treeId) override;
    bool SaveTree(uint32_t treeId) override;
    bool ReloadTree(uint32_t treeId) override;
    void Nothing28() override;
    uint32_t GetConstants(uint32_t id, uint32_t iid, void **out) override;
    uint32_t GetTreeSize(uint32_t treeId) override;
    uint32_t FindTree(uint32_t treeId, bool skipGlobal, bool skipSemiGlobal) override;
    void DescribeNode(uint32_t a, cIGZUnknown *node, cIGZString *out) override;
    void DescribePrimitive(uint32_t a, uint32_t b, cIGZString *out) override;
    void GetTreeName(uint32_t treeId, cIGZString *out) override;
    void GetConstantsName(uint32_t id, cIGZString *out) override;
    uint16_t GetStringCount() override;
    cIGZUnknown *GetPrimitives() override;
    uint32_t GetOwner() override;
    bool CheckFramework(uint32_t a, uint32_t b, uint32_t c) override;
    void SetBreakpoint(uint32_t treeId, uint8_t node, bool on) override;
    void LoadBreakpoints(void *file) override;
    void Nothing42() override;
    void DumpTrees(cIGZUnknown *target) override;
    uint32_t GetBaseIdForKind(uint32_t kind) override;
    uint32_t GetMaxIdForKind(uint32_t kind) override;
    void GetKindName(uint32_t kind, cIGZString *out) override;
    uint32_t GetKindForId(uint32_t treeId) override;
    cITSBehavior *AsBehavior() override;
    cITSBehavior *AsBehavior2() override;

    // Static helpers (__cdecl in the original).
    static void ApplySourceBreakpoints(cTSBehavior *self, cTSBehaviorSource *source); // 0x46da40
    static void KindName(uint32_t kind, cIGZString *out);                            // 0x46f1c0
    static uint32_t KindForId(uint16_t treeId);                                      // 0x46f230
    static cTSBehaviorBreakpoints &Breakpoints();                                    // the game's list at 0x10da9f4

    // +0x00 vtable
    uint32_t mRefCount;                   // +0x04
    cTSBehaviorSource *mGlobalSource;     // +0x08 (held: AddRef'd)
    uint32_t mGlobalGroup;                // +0x0c (not set by the factory)
    cTSBehaviorSource *mSemiGlobalSource; // +0x10 (held)
    uint32_t mSemiGlobalGroup;            // +0x14
    cTSBehaviorSource *mPrivateSource;    // +0x18 (held)
    uint32_t mPrivateGroup;               // +0x1c
    uint32_t mOwner;                      // +0x20 (Init's first argument; not set by the factory)
    cIGZUnknown *mPrimitives;             // +0x24 (held)
};
static_assert(sizeof(cTSBehavior) == cTSBehavior::kSize, "cTSBehavior layout");
static_assert(offsetof(cTSBehavior, mRefCount) == 0x04);
static_assert(offsetof(cTSBehavior, mGlobalSource) == 0x08);
static_assert(offsetof(cTSBehavior, mSemiGlobalSource) == 0x10);
static_assert(offsetof(cTSBehavior, mPrivateSource) == 0x18);
static_assert(offsetof(cTSBehavior, mOwner) == 0x20);
static_assert(offsetof(cTSBehavior, mPrimitives) == 0x24);

// The RVA hooks' entry points kept for byte matches (docs/standalone-shape.md, "Dropping adapters"): original calling
// convention (this in ecx), our members called without virtual dispatch, bodies inlined.
struct cTSBehaviorAdapters {
    using Self = cTSBehavior;
    static bool __fastcall QueryInterface(Self *self, void *, uint32_t riid, void **out);
    static void __fastcall ApplyAllBreakpoints(Self *self, void *);
    static void __fastcall SetGlobalSource(Self *self, void *, uint32_t group, cTSBehaviorSource *source);
    static void __fastcall SetSemiGlobalSource(Self *self, void *, uint32_t group, cTSBehaviorSource *source);
    static void __fastcall SetPrivateSource(Self *self, void *, uint32_t group, cTSBehaviorSource *source);
    static uint32_t __fastcall GetGroupForId(Self *self, void *, int16_t treeId);
    static cTSBehaviorSource *__fastcall GetSourceForId(Self *self, void *, int16_t treeId);
    static bool __fastcall GetSourceName(Self *self, void *, uint32_t treeId, cIGZString *out);
    static void __fastcall DescribePrimitive(Self *self, void *, uint32_t a, uint32_t b, cIGZString *out);
    static void __fastcall LoadBreakpoints(Self *self, void *, void *file);
    static uint32_t __fastcall GetKindForId(Self *self, void *, uint32_t treeId);
};
