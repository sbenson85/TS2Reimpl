#pragma once
// The light maps (lane 261108q, scenegraph row LG6): nTSSG::cLightMapManager (the COM director's class 0x896d0177,
// interface 0x696d015c; cTSSGSystem +0xa0) keeps the textures the floors and walls are lit from. Floor tiles are laid
// out on floor images (tiles of floorTileSize texels in images of floorTextureSize), walls get regions of wall atlases
// (nGZGraphic4::cTextureAtlas over images of 0x200 texels); each lit floor tile or wall face is counted while something
// uses it, and the lighting reads the light at a point back from the images. RVAs 1.18.0.187; all /O2.
//
// Layout (0x1a0, operator new; RTTI order): cILightMapManager +0 (vtable 0xdb5ba0, 24 slots), cIGZMessageTarget2 +4
// (0xdb5c04, 4), rz::cRZUnknown +8 (0xdb5c18, 6).
//
// All of it is ours: the life cycle, the slots, DoMessage and the settings reader (lane 261108q), and the routines
// that build, blend, upload and draw the images under slots 7-11 (lane 261108t, row LG6b: 0x2c5340, 0x2c5a60, 0x2c6200,
// 0x2c6630, 0x2c6f20, 0x2c8280, 0x2c85c0, 0x2c7570, the incidence kernel 0x2c6890 and its table 0x2c6950 / 0x2c6d80).
//
// Standalone shape (docs/standalone-shape.md): the director's factory 0x2c73f0 is hooked and is the only constructor
// call, so every object carries our vtables; the slots, the constructor and the destructor are in effect through it.
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZUnknown.h"
#include "gz/cTextureAtlas.h"
#include "rz/RZUnknownBase.h"
#include "rz/cRZStopwatch.h"
#include "sg/cImageData.h"
#include "sg/cTextureManager.h"
#include "simantics/cTSBehaviorTree.h" // RefPtrVector

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <set>
#include <unordered_map>
#include <vector>

namespace nTSSG {

// A floor tile's key (map A's, 12 bytes): the tile (x, y, level).
struct FloorKey {
    int32_t x, y, level;
};
static_assert(sizeof(FloorKey) == 12, "FloorKey layout");

// What slots 14, 15, 18 and 19 are given (16 bytes): the tile, then the user's flag bits (read by 14 and 15 only). The
// layout of worldmath::TileQuadrant, which the lighting passes.
struct FloorUse {
    FloorKey key;
    uint8_t flags;
    uint8_t pad[3];
};
static_assert(sizeof(FloorUse) == 16, "FloorUse layout");

// Where a lit floor tile is (map A's value, and what the lookups return as a word): the tile's column and row in its
// image, the image (-1 for none) and the flags of the users holding it.
struct FloorTile {
    uint8_t x;
    uint8_t y;
    int8_t image;
    uint8_t flags;
};
static_assert(sizeof(FloorTile) == 4, "FloorTile layout");

// A wall face's key (24 bytes): two corners (x, y) and two words; the hash and the equality read every field.
struct WallKey {
    float x0, y0;
    uint32_t word2;
    float x1, y1;
    uint32_t word5;
};
static_assert(sizeof(WallKey) == 0x18, "WallKey layout");

// A wall face's region (map B's value): the atlas node, the atlas (-1 for none), 1 when the region is the border kind,
// and the count of users.
struct WallRegion {
    int32_t node;
    int8_t atlas;
    uint8_t border;
    uint16_t count;
};
static_assert(sizeof(WallRegion) == 8, "WallRegion layout");

// The maps' functors (the game's own: 0x2c5270, 0x2c5290 and 0x2c5100; the equality is called as (searched, stored)).
struct FloorKeyHash {
    size_t operator()(const FloorKey &key) const; // 0x2c5270
};
struct FloorKeyEqual {
    bool operator()(const FloorKey &a, const FloorKey &b) const; // the 12-byte compare of 0x2c43b0
};
struct WallKeyHash {
    size_t operator()(const WallKey &key) const; // 0x2c5290
};
struct WallKeyEqual {
    bool operator()(const WallKey &a, const WallKey &b) const; // 0x2c5100
};

// The interface (0x696d015c).
class cILightMapManager : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x696d015c;

    virtual bool Init() = 0;                                          // 3
    virtual bool Shutdown() = 0;                                      // 4
    virtual void Start() = 0;                                         // 5: the world's size read, the first images made
    virtual void Stop() = 0;                                          // 6: everything dropped
    virtual void Update() = 0;                                        // 7: each frame (cTSSGSystem)
    virtual void SetColor(const float *rgb) = 0;                      // 8
    virtual void SetTransition(bool on) = 0;                          // 9
    virtual void UpdateIncidence() = 0;                               // 10
    virtual void Advance(float step) = 0;                             // 11
    virtual int32_t AcquireWall(const WallKey *key) = 0;              // 12: the face's atlas, counted
    virtual void ReleaseWall(const WallKey *key) = 0;                 // 13
    virtual int32_t AcquireFloor(const FloorUse *use) = 0;            // 14: the tile's image, its users' flags added
    virtual void ReleaseFloor(const FloorUse *use) = 0;               // 15
    virtual float *GetFloorLight(float *out, const float *position) = 0; // 16: by value (the hidden pointer back in eax)
    virtual float *GetWallLight(float *out, const WallKey *key) = 0;  // 17: by value
    virtual bool GetFloorUV(const FloorUse *use, float *uv0, float *uv1) = 0;                          // 18
    virtual bool GetFloorRect(const FloorUse *use, int32_t *left, int32_t *top, int32_t *right, int32_t *bottom,
                              uint32_t *image) = 0;                                                    // 19
    virtual bool GetWallUV(const WallKey *key, float *uv0, float *uv1, uint32_t *atlas) = 0;          // 20
    virtual bool GetWallRect(const WallKey *key, int32_t *left, int32_t *top, int32_t *right, int32_t *bottom,
                             uint32_t *atlas) = 0;                                                     // 21
    virtual nGZSceneGraph::cIImageData *GetImage(uint32_t group, uint32_t index) = 0;                  // 22: not counted
    virtual float GetScale(int32_t which) = 0;                                                         // 23
};

// One of the image groups' vectors (counted image pointers; the original's 0x14c060 destroys it, 0x176f00 clears it).
struct LightMapImages : RefPtrVector {
    LightMapImages() : RefPtrVector{} {}
    LightMapImages(const LightMapImages &) = delete;
    LightMapImages &operator=(const LightMapImages &) = delete;
    ~LightMapImages() { Destroy(); }
    nGZSceneGraph::cIImageData *At(uint32_t i) const { return static_cast<nGZSceneGraph::cIImageData *>(mFirst[i]); }
};
static_assert(sizeof(LightMapImages) == 12, "LightMapImages layout");

// The stopwatch member, built as the constructor builds it (0x1c9030 with mode 5; its destructor 0x140fe0 does nothing).
struct LightMapStopwatch {
    union {
        cRZStopwatch value;
    };
    LightMapStopwatch() { value.Construct(5); }
    LightMapStopwatch(const LightMapStopwatch &) = delete;
    LightMapStopwatch &operator=(const LightMapStopwatch &) = delete;
    ~LightMapStopwatch() {}
};

class cLightMapManager : public cILightMapManager, public cIGZMessageTarget2, public rz::cRZUnknown {
public:
    static constexpr uint32_t kClass = 0x896d0177;
    static constexpr uint32_t kWorldLoaded = 0x4b3bc675;    // the message the manager reads its settings on
    static constexpr uint32_t kBorderSetting = 0xae050748;  // lmReserveBorderTiles' change message
    static constexpr uint32_t kChanged = 0xeb425393;        // posted when a setting changed after Start

    cLightMapManager();           // 0x2c4950: +0x184, +0x188 and +0x19c left unset
    ~cLightMapManager() override; // 0x2c4f50; deleting 0x2c5310, +8 thunk 0x2c5305
    static cLightMapManager *__cdecl Create(); // 0x2c73f0: null on failure
    // 0x2c7450: the class id the nTSSG COM director registers (its constructor 0x244e30, ours, holds it as data).
    static uint32_t __cdecl ClassId();
    static cLightMapManager *__fastcall Construct(void *memory, void *); // the tests' way to the constructor

    bool QueryInterface(uint32_t riid, void **out) override; // 0: 0x2c7db0 (shared_vf; thunks 0x2c7ded, 0x2c7df5)
    uint32_t AddRef() override;
    uint32_t Release() override;

    bool DoMessage(void *message) override; // face +4, slot 3: 0x2c6d90

    bool Init() override;     // 0x2c7460
    bool Shutdown() override; // 0x2c8140
    void Start() override;    // 0x2c7bb0
    void Stop() override;     // 0x2c7bf0
    void Update() override;                    // 0x2c7cd0
    void SetColor(const float *rgb) override;  // 0x2c6460
    void SetTransition(bool on) override;      // 0x2c6eb0
    void UpdateIncidence() override;           // 0x2c6ad0
    void Advance(float step) override;         // 0x2c7540
    int32_t AcquireWall(const WallKey *key) override;   // 0x2c6080
    void ReleaseWall(const WallKey *key) override;      // 0x2c7fa0
    int32_t AcquireFloor(const FloorUse *use) override; // 0x2c5990
    void ReleaseFloor(const FloorUse *use) override;    // 0x2c7ed0
    float *GetFloorLight(float *out, const float *position) override; // 0x2c76a0
    float *GetWallLight(float *out, const WallKey *key) override;     // 0x2c78e0
    bool GetFloorUV(const FloorUse *use, float *uv0, float *uv1) override; // 0x2c71a0
    bool GetFloorRect(const FloorUse *use, int32_t *left, int32_t *top, int32_t *right, int32_t *bottom,
                      uint32_t *image) override;                           // 0x2c72d0
    bool GetWallUV(const WallKey *key, float *uv0, float *uv1, uint32_t *atlas) override; // 0x2c88f0
    bool GetWallRect(const WallKey *key, int32_t *left, int32_t *top, int32_t *right, int32_t *bottom,
                     uint32_t *atlas) override;                            // 0x2c8aa0
    nGZSceneGraph::cIImageData *GetImage(uint32_t group, uint32_t index) override; // 0x2c7660
    float GetScale(int32_t which) override;                                // 0x2c7640

    // Helpers (thiscall in the original; ours are reached only from the slots above).
    FloorTile FindFloor(const FloorKey *key) const;      // 0x2c7110: level 0 the tile itself, else map A's, else none
    WallRegion FindWall(const WallKey *key) const;       // 0x2c8820: map B's, else none
    bool FindWallRegion(const WallKey *key, WallRegion **region); // 0x2c8890: the stored region; false (untouched) when none
    FloorTile TakeFreeTile();                            // 0x2c7a20
    WallRegion AllocateRegion(int32_t kind, bool border); // 0x2c7aa0: kind wall tiles high (the border kind 1 x 1)
    void ReturnTile(FloorTile tile);                     // 0x2c7e00
    void FreeRegion(int32_t node, int8_t atlas);         // 0x2c7e40
    static void ClearImages(LightMapImages *group);      // 0x2c7ea0: the four vectors of a group
    void ReadWorldSize();                                // 0x2c8070
    void ComputeInsets();                                // 0x2c80f0
    void ReadSettings(void *properties);                 // 0x2c83e0
    void MakeFloorImage();                               // 0x2c5340: a floor image pair, its incidence pair, free tiles
    void MakeWallAtlas();                                // 0x2c5a60: a wall image pair, its incidence pair, an atlas
    void DrawFloorTile(const FloorUse *use);             // 0x2c6200: the tile filled with the light's colour
    void DrawWallRegion(WallRegion region);              // 0x2c6630 (ret 8): the region filled with the light's colour
    void Upload(LightMapImages *group);                  // 0x2c85c0 (ret 4): the group's images given to the textures
    // 0x2c8280 (thiscall on dst, ret 8): dst grown to src's sizes, then each image blended from src by t.
    static void Blend(LightMapImages *dst, LightMapImages *src, float t);
    // 0x2c7570 (cdecl): dst = src blended by t (made src's size when empty); nothing for a pitch that isn't 4 * width.
    static void __cdecl BlendImage(nGZSceneGraph::cIImageData *src, nGZSceneGraph::cIImageData *dst, float t);
    // 0x2c6f20 (cdecl, st0): a wall face's height in levels, the higher top less the lower ground of its two ends.
    static double __cdecl WallFaceHeight(const WallKey *key);
    // 0x2c6950 (cdecl): the incidence table (8-aligned, the allocation's pointer in the dword before it); 0x2c6d80 frees it.
    static uint8_t *__cdecl MakeIncidenceTable(float inverse, float clamp);
    static void __cdecl FreeIncidenceTable(uint8_t *table);
    // 0x2c6860 (cdecl): the kernel over a half-size image, pitches from widths.
    static void __cdecl Incidence(uint8_t *dst, int32_t dstWidth, const uint8_t *src, int32_t srcWidth, int32_t width,
                                  int32_t height, const uint8_t *table);
    // 0x2c6890 (stdcall, MMX in the original): each dst texel from a 2 x 2 block of src texels through the table.
    static void __stdcall IncidenceKernel(uint8_t *dst, int32_t dstPitch, const uint8_t *src, int32_t srcPitch,
                                          int32_t width, int32_t height, const uint8_t *table);

    // Fields (the constructor's values).
    uint8_t mInitialized = 0;                // +0x10
    std::set<uint32_t> mMessageIds;          // +0x14 {kWorldLoaded}, inserted last
    uint32_t mPad1c;                         // +0x1c never written (the original aligns the stopwatch to 8)
    LightMapStopwatch mStopwatch;            // +0x20
    uint8_t mStarted = 0;                    // +0x38
    uint8_t mDebug = 0;                      // +0x39 lightMapDebug
    int32_t mWorldWidth = 0x40;              // +0x3c the world DB's slot 85
    int32_t mWorldHeight = 0x40;             // +0x40 slot 86
    int32_t mFloorTextureSize = 0x200;       // +0x44
    int32_t mFloorTileSize = 8;              // +0x48
    int32_t mWallTextureSize = 0x200;        // +0x4c
    int32_t mWallTileSize = 8;               // +0x50
    int32_t mBorderWidth = 4;                // +0x54
    int32_t mBorderHeight = 8;               // +0x58
    int32_t mInset = 1;                      // +0x5c texels
    float mFloorScale = 0.5f;                // +0x60 GetScale(1)
    float mWallScale = 0.5f;                 // +0x64 GetScale(0)
    float mFloorLightScale = 2.0f;           // +0x68
    float mWallLightScale = 2.0f;            // +0x6c
    float mIncidenceMapScale = 20.0f;        // +0x70
    float mIncidenceMapHeightClamp = 1.0f;   // +0x74
    LightMapImages mImages[4];               // +0x78 [0] walls, [1] floors
    LightMapImages mImages2[4];              // +0xa8 [0] walls, [1] floors (the lights read back)
    std::set<int32_t> mSetD8;                // +0xd8 the wall atlases drawn since the last upload (insert 0x2c4870)
    std::set<int32_t> mSetE0;                // +0xe0 the floor images drawn since the last upload
    std::vector<nGZSceneGraph::cTextureHandle> mTextures[4]; // +0xe8
    std::unordered_map<FloorKey, FloorTile, FloorKeyHash, FloorKeyEqual> mFloors; // +0x118
    std::vector<FloorTile> mFreeTiles;       // +0x138
    int32_t mTilesPerRow = 8;                // +0x144
    float mTileUV = 0.125f;                  // +0x148
    float mHalfTexelUV = 1.0f / 1024.0f;     // +0x14c
    std::unordered_map<WallKey, WallRegion, WallKeyHash, WallKeyEqual> mWalls; // +0x150
    std::vector<nGZGraphic4::cTextureAtlas> mAtlases; // +0x170
    float mInsetU = 0.0f;                    // +0x17c
    float mInsetV = 0.0f;                    // +0x180
    float mBorderInsetU;                     // +0x184 unset until Start
    float mBorderInsetV;                     // +0x188
    cIGZUnknown *mRef18c = nullptr;          // +0x18c counted; nothing in the image sets it (only cleared)
    int32_t mWord190 = 0;                    // +0x190 the transition's steps left
    int32_t mTransitionSteps = 60;           // +0x194
    float mTransitionStepSize = 0.1f;        // +0x198
    uint32_t mUnused19c;                     // +0x19c never written
};
static_assert(offsetof(cLightMapManager, mInitialized) == 0x10 && offsetof(cLightMapManager, mMessageIds) == 0x14 &&
                  offsetof(cLightMapManager, mStopwatch) == 0x20 && offsetof(cLightMapManager, mStarted) == 0x38 &&
                  offsetof(cLightMapManager, mWorldWidth) == 0x3c && offsetof(cLightMapManager, mInset) == 0x5c &&
                  offsetof(cLightMapManager, mIncidenceMapHeightClamp) == 0x74 &&
                  offsetof(cLightMapManager, mImages) == 0x78 && offsetof(cLightMapManager, mImages2) == 0xa8 &&
                  offsetof(cLightMapManager, mSetD8) == 0xd8 && offsetof(cLightMapManager, mTextures) == 0xe8 &&
                  offsetof(cLightMapManager, mFloors) == 0x118 && offsetof(cLightMapManager, mFreeTiles) == 0x138 &&
                  offsetof(cLightMapManager, mTilesPerRow) == 0x144 && offsetof(cLightMapManager, mWalls) == 0x150 &&
                  offsetof(cLightMapManager, mAtlases) == 0x170 && offsetof(cLightMapManager, mInsetU) == 0x17c &&
                  offsetof(cLightMapManager, mRef18c) == 0x18c && offsetof(cLightMapManager, mTransitionSteps) == 0x194 &&
                  offsetof(cLightMapManager, mUnused19c) == 0x19c,
              "cLightMapManager layout");
static_assert(sizeof(cLightMapManager) == 0x1a0, "cLightMapManager size");

} // namespace nTSSG
