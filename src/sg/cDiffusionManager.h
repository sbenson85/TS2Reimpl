#pragma once
// The light diffusion's manager (lane 261109f, scenegraph row LD1): nTSSG::cDiffusionManager (made by cLightingManager's
// Init 0x2d98c0 through the constructor 0x3c4140) keeps the diffusion's engine: one floor colour grid per level
// (nTSSG::cFloorColorGrid, 0x84 bytes each) and the wall diffusion manager (nTSSG::cWallDiffusionManager). It reads the
// diffusion's settings from the world-loaded message's properties, hands them to the grids and the walls, and runs
// their passes for the lighting. RVAs 1.18.0.187; all /O2.
//
// Layout (0x94, operator new; RTTI order): cIDiffusionManager +0 (vtable 0xdbeb7c, 25 slots), cIGZMessageTarget2 +4
// (0xdbebe4, 4), rz::cRZUnknown +8 (0xdbebf8, 6).
//
// Ours: the constructor, the destructor, slots 3-7 and 12-24, DoMessage, the settings reader 0x3c6220, ApplySettings
// 0x3c60f0 and ResizeGrids 0x3c5eb0. Slots 8-11 (src/sg/cDiffusionManagerPortals.cpp, with the helpers 0x3c5a60, 0x3c5c30,
// 0x3c3870 and 0x3c3a70) are ours (row LD1b). The engine 0x405200-0x412600 (the
// grids' and the walls' methods, row LD2): the grids' storage and leaves are ours (cFloorColorGrid, LD2a); the rest is
// still the original's, called through OriginalFunction.
//
// Shape: the constructor 0x3c4140 is hooked; its only caller is cLightingManager::Init 0x2d98c0 (the original's), so every
// object carries our vtables and the slots, DoMessage and the destructor are in effect through it.
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZUnknown.h"
#include "game/original.h"
#include "game/vtable.h"
#include "rz/RZUnknownBase.h"
#include "sg/cFloorColorGrid.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

namespace nGZSceneGraph {
class cICompositionTreeNode;
class cILightRefNode;
}

namespace nTSSG {

struct cPortalLight;

// A floor colour grid (nTSSG::cFloorColorGrid, vtable 0xdbeb74; src/sg/cFloorColorGrid.h, row LD2a). Built and copied
// by its Construct / CopyConstruct (0x4056d0 / 0x3c42c0), destroyed through its slot 0 (the scalar deleting destructor,
// not freeing). No move: std::vector copies them when it grows, as the original's does.
struct DiffusionGrid {
    void *mVTable;
    uint8_t mBody[0x80];
    DiffusionGrid();                            // cFloorColorGrid::Construct (cDiffusionManager.cpp)
    DiffusionGrid(const DiffusionGrid &other);  // cFloorColorGrid::CopyConstruct
    DiffusionGrid &operator=(const DiffusionGrid &) = delete;
    ~DiffusionGrid() { RawSlot<0, void>(this, 0u); } // receiver: nTSSG::cFloorColorGrid (slot 0, not freeing)
    cFloorColorGrid *Grid() { return reinterpret_cast<cFloorColorGrid *>(this); }
};
static_assert(sizeof(DiffusionGrid) == sizeof(cFloorColorGrid), "DiffusionGrid layout");

// What the wall lists hold (24 bytes; the floor lists' DiffusionTile is in cFloorColorGrid.h).
struct DiffusionWall {
    uint32_t words[6];
};
// The 44-byte records 0x40fa40 gives the grids' 0x4065b0 (a local vector in slots 12 and 13).
struct DiffusionBlob44 {
    uint32_t words[11];
};

// A counted cIGZUnknown reference (the world and its partitions): AddRef slot 1, Release slot 2.
struct DiffusionRef {
    cIGZUnknown *mObject = nullptr;
    DiffusionRef() = default;
    DiffusionRef(const DiffusionRef &) = delete;
    DiffusionRef &operator=(const DiffusionRef &) = delete;
    ~DiffusionRef()
    {
        if (mObject) mObject->Release(); // receiver: cIGZUnknown (slot 2)
    }
    void Assign(cIGZUnknown *object);
    void Clear();
};

// The wall diffusion manager's reference (nTSSG::cWallDiffusionManager, a cRZRefCount: slot 1 Release, slot 2 AddRef).
struct DiffusionWallRef {
    void *mObject = nullptr;
    DiffusionWallRef() = default;
    DiffusionWallRef(const DiffusionWallRef &) = delete;
    DiffusionWallRef &operator=(const DiffusionWallRef &) = delete;
    ~DiffusionWallRef()
    {
        if (mObject) RawSlot<1, void>(mObject); // receiver: nTSSG::cWallDiffusionManager (slot 1, Release)
    }
    void Assign(void *object);
    void Clear();
};

// The interface (0x696d015c and the class id 0x896d0177 are the light map manager's too: the QI 0x2c7db0 is shared).
class cIDiffusionManager : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x696d015c;

    virtual bool Init() = 0;                                        // 3
    virtual bool Shutdown() = 0;                                    // 4
    virtual void Start() = 0;                                       // 5: the walls made, the grids sized
    virtual void Stop() = 0;                                        // 6
    virtual void SetSettings(const float *values) = 0;              // 7: from the lighting's 12-float block
    virtual void PortalLightsOfScene(std::vector<cPortalLight> *out) = 0;             // 8: 0x3c4f20: every portal node's lights
    virtual void PortalLightsOfScenePublicLots(std::vector<cPortalLight> *out) = 0;             // 9: 0x3c5050: those of lit lots
    virtual uint32_t PortalLightsOfNode(nGZSceneGraph::cICompositionTreeNode *node, std::vector<cPortalLight> *out) = 0; // 10: 0x3c48a0
    // 11: 0x3c4b30 (ret 0xc, al): one light reference node's light, under the portal node
    virtual bool MakePortalLight(nGZSceneGraph::cICompositionTreeNode *node, nGZSceneGraph::cILightRefNode *ref, cPortalLight *light) = 0;
    virtual void Diffuse(uint32_t arg) = 0;                         // 12: the full pass, the light maps read
    virtual void DiffuseUnlit(uint32_t arg) = 0;                    // 13: the pass without the light maps
    virtual void ReadLightMaps() = 0;                               // 14
    virtual void ReadLightMapsAndCollect() = 0;                     // 15: and the changed tiles and walls collected
    virtual void CollectTiles(std::vector<DiffusionTile> *out) = 0; // 16
    virtual void CollectWalls(std::vector<DiffusionWall> *out) = 0; // 17
    virtual void CollectTiles2(std::vector<DiffusionTile> *out) = 0; // 18
    virtual void CollectWalls2(std::vector<DiffusionWall> *out) = 0; // 19
    virtual void PassChanges() = 0;                                 // 20: the collected lists given to floors and walls
    virtual void Reset() = 0;                                       // 21
    virtual void Mark(uint32_t a, uint32_t b) = 0;                  // 22
    virtual float *GetFloorColor(float *out, const float *position) = 0; // 23: by value (the hidden pointer back)
    virtual float *GetWallColor(float *out, uint32_t wall) = 0;     // 24: by value
};

class cDiffusionManager : public cIDiffusionManager, public cIGZMessageTarget2, public rz::cRZUnknown {
public:
    static constexpr uint32_t kClass = 0x896d0177;
    static constexpr uint32_t kWorldLoaded = 0x4b3bc675; // the message the settings are read on
    static constexpr uint32_t kChanged = 0xeb425393;     // posted when a setting changed after Start; data1 1 or 4 dirties
    static constexpr uint32_t kLevels = 0x2b4d1900;      // the world's levels changed: the grids resized

    cDiffusionManager();           // 0x3c4140: +0x1c, +0x1e and +0x1f left unset
    ~cDiffusionManager() override; // 0x3c4730; deleting 0x3c4840, +8 thunk 0x3c482c
    static cDiffusionManager *__fastcall Construct(void *memory, void *); // the hook's and the tests' way in

    bool QueryInterface(uint32_t riid, void **out) override; // 0: 0x2c7db0 (shared_vf with cLightMapManager)
    uint32_t AddRef() override;
    uint32_t Release() override;

    bool DoMessage(void *message) override; // face +4, slot 3: 0x3c5920

    bool Init() override;                                    // 0x3c5c90
    bool Shutdown() override;                                // 0x3c6050
    void Start() override;                                   // 0x3c5dc0
    void Stop() override;                                    // 0x3c5e60
    void SetSettings(const float *values) override;          // 0x3c6010
    void PortalLightsOfScene(std::vector<cPortalLight> *out) override;     // 0x3c4f20
    void PortalLightsOfScenePublicLots(std::vector<cPortalLight> *out) override;     // 0x3c5050
    uint32_t PortalLightsOfNode(nGZSceneGraph::cICompositionTreeNode *node, std::vector<cPortalLight> *out) override; // 0x3c48a0
    bool MakePortalLight(nGZSceneGraph::cICompositionTreeNode *node, nGZSceneGraph::cILightRefNode *ref, cPortalLight *light) override; // 0x3c4b30
    void Diffuse(uint32_t arg) override;                     // 0x3c5470
    void DiffuseUnlit(uint32_t arg) override;                // 0x3c5710
    void ReadLightMaps() override;                           // 0x3c51b0
    void ReadLightMapsAndCollect() override;                 // 0x3c52a0
    void CollectTiles(std::vector<DiffusionTile> *out) override;  // 0x3c5ba0
    void CollectWalls(std::vector<DiffusionWall> *out) override;  // 0x3c5c10
    void CollectTiles2(std::vector<DiffusionTile> *out) override; // 0x3c5b10
    void CollectWalls2(std::vector<DiffusionWall> *out) override; // 0x3c5b80
    void PassChanges() override;                             // 0x3c5d20
    void Reset() override;                                   // 0x3c5410
    void Mark(uint32_t a, uint32_t b) override;              // 0x3c4ab0
    float *GetFloorColor(float *out, const float *position) override; // 0x3c5d60
    float *GetWallColor(float *out, uint32_t wall) override; // 0x3c5da0

    // Helpers (thiscall in the original; ours are reached only from the slots above).
    void ApplySettings();               // 0x3c60f0: the settings handed to the walls and each grid
    void ResizeGrids();                 // 0x3c5eb0: one grid per level, each sized from its floor
    void ReadSettings(void *properties); // 0x3c6220 (ret 4)
    // 0x3c5a60 (__cdecl): the node's portal face (interface 0xfa1c39ed, uncounted): the node's own, else its nearest
    // ancestor's; null when none has one. Asks each node while any QueryInterface wrote a reference, releasing it.
    static void *__cdecl PortalOf(nGZSceneGraph::cICompositionTreeNode *node);
    // 0x3c5c30 (ret 4): the lighting manager's lot value (slot 36) for the partitions' level at the object's position
    // (its slot 42 (out, 0, 1)) is below 100 as an int16.
    bool LotValueBelow100(void *object);

    // Fields (the constructor's values).
    uint8_t mInitialized = 0;                     // +0x10
    std::set<uint32_t> mMessageIds;               // +0x14 {kWorldLoaded, kChanged, kLevels}
    uint8_t mStarted;                             // +0x1c not set by the constructor
    uint8_t mDirty = 1;                           // +0x1d the grids' and walls' sources to be rebuilt
    uint8_t mPad1e[2];                            // +0x1e never written
    DiffusionRef mWorld;                          // +0x20 the placement service (0x20e0b2) while initialised
    DiffusionRef mPartitions;                     // +0x24 its slot 39
    float mInsideFloorAmbient = 1.0f;             // +0x28
    float mOutsideFloorAmbient = 1.0f;            // +0x2c
    float mInsideWallAmbient = 1.0f;              // +0x30
    float mOutsideWallAmbient = 1.0f;             // +0x34
    float mFloorDiffusionThreshold = 0.137f;      // +0x38 (0x3e0c49ba)
    float mWallDiffusionThreshold = 0.137f;       // +0x3c
    float mBrightWallFloorContribution = 0.5f;    // +0x40
    float mDarkWallFloorContribution = 1.0f;      // +0x44
    float mInsideAmbientColor[3] = {0.07f, 0.08f, 0.1f}; // +0x48
    float mOutsideAmbientColor[3] = {0.5f, 0.5f, 0.5f}; // +0x54
    int32_t mWallPropagationSteps = 10;           // +0x60
    int32_t mFloorPropagationSteps = 10;          // +0x64
    DiffusionWallRef mWalls;                      // +0x68
    std::vector<DiffusionGrid> mGrids;            // +0x6c one per level from mBaseLevel
    int32_t mBaseLevel = 0;                       // +0x78 the world's lowest level
    std::vector<DiffusionTile> mTiles;            // +0x7c
    std::vector<DiffusionWall> mWallList;         // +0x88
};
static_assert(offsetof(cDiffusionManager, mInitialized) == 0x10 && offsetof(cDiffusionManager, mMessageIds) == 0x14 &&
                  offsetof(cDiffusionManager, mStarted) == 0x1c && offsetof(cDiffusionManager, mDirty) == 0x1d &&
                  offsetof(cDiffusionManager, mWorld) == 0x20 && offsetof(cDiffusionManager, mPartitions) == 0x24 &&
                  offsetof(cDiffusionManager, mInsideFloorAmbient) == 0x28 &&
                  offsetof(cDiffusionManager, mFloorDiffusionThreshold) == 0x38 &&
                  offsetof(cDiffusionManager, mBrightWallFloorContribution) == 0x40 &&
                  offsetof(cDiffusionManager, mInsideAmbientColor) == 0x48 &&
                  offsetof(cDiffusionManager, mOutsideAmbientColor) == 0x54 &&
                  offsetof(cDiffusionManager, mWallPropagationSteps) == 0x60 &&
                  offsetof(cDiffusionManager, mWalls) == 0x68 && offsetof(cDiffusionManager, mGrids) == 0x6c &&
                  offsetof(cDiffusionManager, mBaseLevel) == 0x78 && offsetof(cDiffusionManager, mTiles) == 0x7c &&
                  offsetof(cDiffusionManager, mWallList) == 0x88,
              "cDiffusionManager layout");
static_assert(sizeof(cDiffusionManager) == 0x94, "cDiffusionManager size");

} // namespace nTSSG
