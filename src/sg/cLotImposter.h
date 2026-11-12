#pragma once
// nTSSG::cLotImposterDesc and nTSSG::cLotImposter (lane 261108j, scenegraph row LT2a): a neighbourhood lot's imposter, the
// low-detail model the neighbourhood view draws for a lot (its "imposter_cres" scene graph, loaded from the lot's own
// package group), and the small persist resource that says whether the lot has one. The lot imposter manager
// (nTSSG::cLotImposterManager, row LT2b) creates one per lot through the COM director (class 0x4d892b0b) and loads it.
// RVAs 1.18.0.187. Names of slots and fields are ours (tentative unless the status entry says otherwise).
//
// cLotImposterDesc (0x28 bytes, class and resource type 0xcd8b6498; factory 0x36d260): faces cILotImposterDesc +0
// (vtable 0xdbb7d8), cIGZSerializable +4 (0xdbb7f0), cGZPersistResource +8 (0xdbb80c, the base's slots). One word, at
// +0x24, read and written by slots 3 / 4 of the face and serialised after a version word (1).
//
// cLotImposter (0x14 bytes, class 0x4d892b0b; factory 0x36d0a0): rz::cRZUnknown +0 (vtable 0xdbb778), cILotImposter +8
// (0xdbb794). Holds the loaded scene node (+0xc) and the lot's desc (+0x10), each counted.
//
// Standalone shape (docs/standalone-shape.md): the factories are the only callers of the constructors and the only code
// that stores the vtables besides the destructors, so hooking the two factories (the COM director's class table,
// src/rz/COMDirectorData.cpp) makes every desc and imposter ours; every other member is reached through our vtables (in
// effect through callers, trapped; db/status/cLotImposter.jsonl).
#include "gz/cGZPersistResource.h"
#include "gz/cIGZSerializable.h"
#include "gz/cIGZUnknown.h"
#include "rz/RZUnknownBase.h"

#include <cstddef>
#include <cstdint>

namespace nGZSceneGraph {
class cIResourceNode;
}

namespace nTSSG {

// cILotImposterDesc (iid 0xed8b648b): the desc's one value (non-zero: the lot has an imposter to show).
class cILotImposterDesc : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xed8b648b;
    virtual uint32_t GetValue() = 0;               // 3 (shared 0x15e970: +0x24)
    virtual uint32_t SetValue(uint32_t value) = 0; // 4 (shared 0x2109b0: stores +0x24, returns the value)
};

class cLotImposterDesc final : public cILotImposterDesc, public cIGZSerializable, public cGZPersistResource {
public:
    static constexpr uint32_t kType = 0xcd8b6498;            // the persist key's type, GetGZCLSID and the COM class
    static constexpr uint32_t kSerializableIid = 0xe4fda3d4; // cIGZSerializable
    static constexpr uint16_t kVersion = 1;                  // Write's version word (Read skips it)
    static constexpr size_t kSize = 0x28;

    cLotImposterDesc();           // 0x36d160: the base, the value 0, then the key (type 0xcd8b6498, group 0, instance 0)
    ~cLotImposterDesc() override; // 0x36d1f0; the deleting destructor 0x36d220 (thunk 0x36d20b) frees 0x28
    cLotImposterDesc(const cLotImposterDesc &) = delete;
    cLotImposterDesc &operator=(const cLotImposterDesc &) = delete;

    // 0x36d260 (__cdecl): a new desc as its cILotImposterDesc face (null when the allocation fails).
    static cILotImposterDesc *__cdecl Create();

    // ---- cIGZUnknown (every face) ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0x36d2d0; thunks 0x36d326 (+4), 0x36d32e (+8)
    uint32_t AddRef() override;                              // 0x36d240: the base's; thunks 0x36d248, 0x36d250
    uint32_t Release() override;                             // 0x36d370: the base's; thunks 0x36d378, 0x36d380

    // ---- cILotImposterDesc ----
    uint32_t GetValue() override;
    uint32_t SetValue(uint32_t value) override;

    // ---- cIGZSerializable (+4) ----
    bool Write(ifaces::cIGZOStream *stream) override; // 0x36d390
    bool Read(ifaces::cIGZIStream *stream) override;  // 0x36d340
    uint32_t GetGZCLSID() override;                   // 0x36d2c0

    uint32_t mValue; // +0x24
};
static_assert(sizeof(cLotImposterDesc) == cLotImposterDesc::kSize, "new 0x28 in 0x36d260");
static_assert(offsetof(cLotImposterDesc, mValue) == 0x24, "cLotImposterDesc layout");

// cILotImposter (iid 0xad892b02): slots 3-9.
class cILotImposter : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xad892b02;
    virtual bool Load(uint32_t lot) = 0;                            // 3 (0x36cb20, ret 4)
    virtual bool Unload() = 0;                                      // 4 (0x36d090)
    virtual nGZSceneGraph::cIResourceNode *GetSceneNode() = 0;      // 5 (shared 0x147a30 on the face: +0xc)
    virtual cILotImposterDesc *GetDesc() = 0;                       // 6 (shared 0x46390 on the face: +0x10)
    virtual cILotImposterDesc *GetDescAgain() = 0;                  // 7 (the same body)
    virtual bool HasImposter() = 0;                                 // 8 (0x36cac0)
    virtual void AttachToTerrain(void *terrain) = 0;                // 9 (0x36cae0, ret 4)
};

class cLotImposter final : public rz::cRZUnknown, public cILotImposter {
public:
    static constexpr uint32_t kClassId = 0x4d892b0b;  // the COM director's class (factory 0x36d0a0)
    static constexpr size_t kSize = 0x14;
    // Load's resources.
    static constexpr uint32_t kLotGroupType = 0xac506764;   // the key of the lot's imposter resource list
    static constexpr uint32_t kLotGroupInstance = 0x486d;
    static constexpr uint32_t kKeyListIid = 0x6cd04176;     // what the resource manager's slot 5 hands out for it
    static constexpr uint32_t kSceneTypePreset = 0xe519c933; // a "_cres" resource's key type
    static constexpr uint32_t kResourceNodeIid = 0xe519c92c; // nGZSceneGraph::cIResourceNode
    static constexpr uint32_t kShapeRefNodeIid = 0xa516cd10; // nGZSceneGraph::cIShapeRefNode
    static constexpr uint32_t kSeasonsBit = 7;               // the option bit Load asks before applying the season

    cLotImposter();           // 0x36c9d0: the base, both references null
    ~cLotImposter() override; // 0x36ca30: releases the desc, then the node; the deleting destructor 0x36caa0 frees 0x14
    cLotImposter(const cLotImposter &) = delete;
    cLotImposter &operator=(const cLotImposter &) = delete;

    // 0x36d0a0 (__cdecl): a new imposter as its cILotImposter face (null when the allocation fails).
    static cILotImposter *__cdecl Create();

    // ---- cIGZUnknown ----
    bool QueryInterface(uint32_t riid, void **out) override; // 0x36d110; thunk 0x36d14f (+8)
    uint32_t AddRef() override { return rz::cRZUnknown::AddRef(); }   // shared 0x146a50; thunk 0x146a55
    uint32_t Release() override { return rz::cRZUnknown::Release(); } // shared 0x1f9aa0; thunk 0x1f9aa5

    // ---- cILotImposter (+8) ----
    bool Load(uint32_t lot) override;
    bool Unload() override;
    nGZSceneGraph::cIResourceNode *GetSceneNode() override;
    cILotImposterDesc *GetDesc() override;
    cILotImposterDesc *GetDescAgain() override;
    bool HasImposter() override;
    void AttachToTerrain(void *terrain) override;

    nGZSceneGraph::cIResourceNode *mNode; // +0xc the imposter's scene graph (its resource node)
    cILotImposterDesc *mDesc;             // +0x10 the lot's desc resource (null when the lot's list has none)

private:
    void ReleaseResources(); // folded 0x2a2ea0 (shared with cNHoodTerrain): the node, then the desc, released and null
};
static_assert(sizeof(cLotImposter) == cLotImposter::kSize, "new 0x14 in 0x36d0a0");
static_assert(offsetof(cLotImposter, mNode) == 0xc && offsetof(cLotImposter, mDesc) == 0x10, "cLotImposter layout");

// ---- Load's helpers (src/sg/cLotImposterResources.cpp; their own TU so tests can stand in for them) ----

// One resource key of a lot's imposter list (the resource manager's 16-byte key: type, group, instance, high instance).
struct ImposterKey {
    uint32_t mType;
    uint32_t mGroup;
    uint32_t mInstance;
    uint32_t mInstanceHigh;
};
static_assert(sizeof(ImposterKey) == 0x10, "the resource manager's key");
// The list's keys as the key-list resource's slot 7 hands them out: {begin, end} (a std::vector's first two words).
struct ImposterKeyRange {
    ImposterKey *mBegin;
    ImposterKey *mEnd;
};

// 0x401790 (__cdecl): the package group of lot `lot`'s resources: the lot (the current neighbourhood's slot 57 for the id
// when there is one, else the game state's current lot, slot 9), its name (slot 8), and the persist service's group for
// that name (slot 11). False, `*group` untouched, when there is no lot.
bool __cdecl GetLotGroup(uint32_t lot, uint32_t *group);

// 0xb40560 (__cdecl): loads every resource of an imposter list: each material definition (type 0x49596978) gets its name
// resolved in its group, each texture (0x1c4a276c) is registered with the texture manager (or its image handed to the
// one already there), and the scene graph (0xe519c933) is loaded into `*node` as a resource node. A material or texture
// not in its group is looked for again in the global group 0x1c0532fa. True when every key loaded (keys of other types
// count as loaded); every key is tried either way.
bool __cdecl LoadImposterResources(const ImposterKeyRange *keys, nGZSceneGraph::cIResourceNode **node);

} // namespace nTSSG
