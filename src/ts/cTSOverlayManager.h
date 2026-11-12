#pragma once
// The ground overlays (lane RX, rendering row E2): cTSOverlayManager (the COM director's class 0xac22fa8d; interface
// 0xac22fa89) keeps the textured quads laid on the ground or the water, cTSOverlay one such quad, cTSDecal one placed by a
// position, a size and a turn; each frame Update hands what changed to a renderer's cTSOverlayTable. cTSTestMap (the
// director's class 0x2c2a7110) is the wavy test height map. RVAs 1.18.0.187; all /O2.
//
// Layouts (RTTI order). cTSOverlay 0xd4: cITSOverlay +0 (vtable 0xe8e5e0, 16 slots), cRZUnknown +4 (0xe8e624, 6).
// cTSDecal 0x108: cITSDecal +0 (0xe8e640, 7), cTSOverlay +4 (0xe8e660, 16; 0xe8e6a4, 6). cTSOverlayManager 0x78:
// cITSOverlayManager +0 (0xe8e6c0, 15), cIGZMessageTarget2 +4 (0xe8e700, 4), cRZUnknown +8 (0xe8e714, 6).
// cTSTestMap 0xc: cITSEffectMap +0 (0xe8e4e0, 8), cRZUnknown +4 (0xe8e504, 6).
//
// Heap: overlays and decals come from the fixed pools 0x107d9a8 (0xd4 bytes) and 0x107d9b8 (0x108); the manager, its list
// and vectors through operator new; the table's vector and list through the allocator service.
//
// Standalone shape (docs/standalone-shape.md): the two director factories 0xbd9700, 0xbdaf50 are hooked, the manager's
// constructor is reached from them and cTSEffectsManager::Init (ours), and overlays and decals are only made by the
// manager's slots, so every object carries our vtables and the slots and destructors need no adapters.
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZUnknown.h"
#include "rz/GameAllocator.h"
#include "rz/RZUnknownBase.h"
#include "sg/cTextureManager.h"
#include "ts/CountedRef.h"
#include "ts/EffectsTransform.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <vector>

class cIGZString;

// A decal's numbers, read and written whole (0x2c bytes, cITSDecal's slots 4 and 5).
struct cTSDecalParams {
    fx::Vector3 mPosition; // +0x00
    float mSize;           // +0x0c
    float mRotation;       // +0x10 in turns
    float mAspect;         // +0x14
    float mRepeatU;        // +0x18
    float mRepeatV;        // +0x1c
    float mOffsetU;        // +0x20
    float mOffsetV;        // +0x24
    uint32_t mSpare;       // +0x28 nothing found reads it; GetParams and SetParams copy it (0xbdad90, 0xbdbea0)
};
static_assert(sizeof(cTSDecalParams) == 0x2c, "cTSDecalParams layout");

class cITSOverlay : public cIGZUnknown {
public:
    virtual void SetVisible(bool value) = 0;              // 3
    virtual void SetAlpha(float alpha) = 0;               // 4
    virtual void SetColor(const fx::Vector3 *color) = 0;  // 5
    virtual void SetDraw(uint32_t draw) = 0;              // 6
    virtual void SetTexture(const char *name) = 0;        // 7
    virtual void SetLayer(uint16_t layer) = 0;            // 8
    // 9: the rectangle (min x, max x, min y, max y), the projection, the texture window's corners, the record's flag bit.
    virtual void SetWindowedPlacement(const float *rect, const float *matrix, const float *windowMin, const float *windowMax,
                                      bool flagged) = 0;
    virtual void SetPlacement(const float *rect, const float *matrix, bool flagged) = 0; // 10: the whole texture
    virtual bool IsVisible() = 0;                         // 11
    virtual float GetAlpha() = 0;                         // 12
    virtual fx::Vector3 *GetColor(fx::Vector3 *color) = 0; // 13: a by-value return, so the hidden pointer comes back in eax
    virtual uint32_t GetDraw() = 0;                       // 14
    virtual uint16_t GetLayer() = 0;                      // 15
};

class cITSDecal : public cIGZUnknown {
public:
    virtual void SetPosition(const fx::Vector3 *position) = 0; // 3: nothing when each axis is within 32 float steps
    virtual void SetParams(const cTSDecalParams *p) = 0;       // 4
    virtual void GetParams(cTSDecalParams *p) = 0;             // 5
    virtual cITSOverlay *GetOverlay() = 0;                     // 6: 0x1c04e0, folded across classes
};

// What the manager lays decals on (+0x3c, set through slot 14; the objects are the game's).
class cITSOverlayGround : public cIGZUnknown {
public:
    virtual void SnapPosition(fx::Vector3 *position) = 0;                                 // 3
    virtual bool GetNormal(const fx::Vector3 *position, fx::Vector3 *normal) = 0;         // 4: false for none
};

// One overlay as the renderer gets it (0xb8 bytes). The original leaves a new record's bytes before +0xa0 unset.
struct cTSOverlayRecord {
    int32_t mCells[4];     // +0x00
    float mRect[4];        // +0x10
    float mMatrix[16];     // +0x20
    float mTextureMatrix[16]; // +0x60
    uint32_t mColor = 0;   // +0xa0: alpha, red, green, blue from the top byte
    uint32_t mTexture = 0xffffffffu; // +0xa4
    uint8_t mDraw = 0;     // +0xa8
    uint16_t mFlags = 0;   // +0xaa: 1 the overlay's flag, 2 changed since the last pass
    uint16_t mLayer;       // +0xac
    uint32_t mKey = 0;     // +0xb0: the overlay's draw id
    uint8_t mActive = 0;   // +0xb4
};
static_assert(sizeof(cTSOverlayRecord) == 0xb8, "cTSOverlayRecord layout");

// A renderer's records by overlay. The object is the renderer's; 0x3afe70, which marks a record active or not, is shared
// with nTSSG::cLotSkirtOverlayGeometry and stays the original's.
struct cTSOverlayTable {
    std::vector<cTSOverlayRecord, GZServiceAllocator<cTSOverlayRecord>> mRecords; // +0x0
    std::list<uint32_t, GZServiceAllocator<uint32_t>> mFree;                      // +0xc: the unused records' indices

    cTSOverlayRecord *Acquire(uint32_t key); // 0xbdafc0: the key's record, active; a free or a new one when it has none
};
static_assert(sizeof(cTSOverlayTable) == 0x14, "cTSOverlayTable layout");

class cTSOverlay : public cITSOverlay, public rz::cRZUnknown {
public:
    enum : uint32_t {
        kVisible = 1,      // set by the constructor
        kChanged = 2,
        kRepeating = 4,    // a decal's repeat is not 1
        kIsDecal = 0x20,
        kInManager = 0x80,
        kRecordFlag = 0x100,
    };

    cTSOverlay();           // 0xbd9e70: the rectangle, the cells and the layer stay unset
    ~cTSOverlay() override; // 0xbda3d0 (deleting); +4 thunk 0xbda33e

    static void *operator new(size_t);       // 0xbda2f0: from the pool
    static void operator delete(void *memory); // 0xbda320: back to the pool

    bool QueryInterface(uint32_t riid, void **out) override; // 0: 0x1e2e00, +4 0x1b945c: cRZUnknown's
    uint32_t AddRef() override;
    uint32_t Release() override;

    void SetVisible(bool value) override;              // 0xbdb5a0
    void SetAlpha(float alpha) override;               // 0xbdb420: small steps away from 0 and 1 are dropped
    void SetColor(const fx::Vector3 *color) override;  // 0xbdb490
    void SetDraw(uint32_t draw) override;              // 0xbdb570
    void SetTexture(const char *name) override;        // 0xbdb650
    void SetLayer(uint16_t layer) override;            // 0xbdb6c0
    void SetWindowedPlacement(const float *rect, const float *matrix, const float *windowMin, const float *windowMax,
                              bool flagged) override;  // 0xbdbf60
    void SetPlacement(const float *rect, const float *matrix, bool flagged) override; // 0xbdc010
    bool IsVisible() override;                         // 0xbdb1d0
    float GetAlpha() override;                         // 0xbdabc0
    fx::Vector3 *GetColor(fx::Vector3 *color) override; // 0xbdad70
    uint32_t GetDraw() override;                       // 0xbdadd0
    uint16_t GetLayer() override;                      // 0xbdc470

    // The rectangle and both matrices, as the slots and the manager's makers do: the matrix is mapped onto the window.
    void PlaceWindowed(const float *rect, const float *matrix, const float *windowMin, const float *windowMax);
    void Place(const float *rect, const float *matrix);

    void MarkChanged() { mFlags = (mFlags & (kVisible | kChanged)) ? mFlags | kChanged : mFlags & ~kChanged; }

    uint32_t mDrawId = 0;              // +0x0c: the common system's, taken when the manager first sees the overlay
    uint32_t mFlags = 0;               // +0x10
    uint8_t mDraw = 0;                 // +0x14
    uint8_t mPad15;                    // +0x15
    uint16_t mPad16 = 0;               // +0x16
    float mRect[4];                    // +0x18: min x, max x, min y, max y
    float mMatrix[16];                 // +0x28
    float mTextureMatrix[16];          // +0x68
    float mAlpha = 1.0f;               // +0xa8
    fx::Vector3 mColor;                // +0xac
    uint16_t mLayer;                   // +0xb8
    int32_t mCells[4];                 // +0xbc: the rectangle on the manager's grid, clamped; zeros off the grid
    nGZSceneGraph::cTextureHandle mTexture; // +0xcc
    // +0xd0: this overlay's node in the manager's list, valid while kInManager.
    std::list<fx::CountedRef<cTSOverlay>>::iterator mNode;
};
static_assert(sizeof(cTSOverlay) == 0xd4, "cTSOverlay layout");

class cTSDecal : public cITSDecal, public cTSOverlay {
public:
    cTSDecal();           // inside CreateDecal 0xbda440
    ~cTSDecal() override; // 0xbda380 (deleting); thunk 0xbda336

    static void *operator new(size_t);         // 0xbda2e0: from the pool
    static void operator delete(void *memory); // 0xbda300: back to the pool

    bool QueryInterface(uint32_t riid, void **out) override; // 0: 0xbdb330; thunks 0xbdb338, 0xbdb340
    uint32_t AddRef() override;
    uint32_t Release() override;

    void SetPosition(const fx::Vector3 *position) override; // 0xbdb1e0
    void SetParams(const cTSDecalParams *p) override;       // 0xbdbea0
    void GetParams(cTSDecalParams *p) override;             // 0xbdad90
    cITSOverlay *GetOverlay() override;                     // 0x1c04e0

    cTSDecalParams mParams; // +0xd8
    float mTextureAspect;   // +0x104: the texture's height over its width
};
static_assert(sizeof(cTSDecal) == 0x108, "cTSDecal layout");

class cITSOverlayManager : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xac22fa89;

    virtual bool Init() = 0;     // 3: false when already up
    virtual bool Shutdown() = 0; // 4: every overlay dropped
    virtual cITSDecal *CreateDecal(const char *texture, const fx::Vector3 *position, float size, float rotation) = 0; // 5
    virtual void AddOverlay(cITSOverlay *overlay) = 0; // 6
    virtual cITSOverlay *CreateWindowedOverlay(const char *texture, const float *rect, const float *matrix,
                                               const float *windowMin, const float *windowMax) = 0; // 7
    virtual cITSOverlay *CreateOverlay(const char *texture, const float *rect, const float *matrix) = 0; // 8
    virtual void RemoveOverlay(cITSOverlay *overlay) = 0; // 9
    virtual void GetStatus(cIGZString *out) = 0;          // 10
    virtual bool Update(cTSOverlayTable *table) = 0;      // 11: whether anything changed
    virtual void SetGrid(float originX, float originY, float cellSize, int32_t width, int32_t height) = 0; // 12
    virtual void SetGridSize(float cellSize, int32_t width, int32_t height) = 0;                            // 13
    virtual void SetGround(cITSOverlayGround *ground) = 0; // 14: 0x46e120, a body folded across classes
};

class cTSOverlayManager : public cITSOverlayManager, public cIGZMessageTarget2, public rz::cRZUnknown {
public:
    static constexpr uint32_t kClass = 0xac22fa8d;

    cTSOverlayManager();           // 0xbd9fd0
    ~cTSOverlayManager() override; // 0xbda200; deleting 0xbda420, +8 thunk 0xbda346
    static cTSOverlayManager *__cdecl Create(); // 0xbdaf50: null on failure
    static cTSOverlayManager *__fastcall Construct(void *memory, void *); // cTSEffectsManager::Init's way to the constructor

    bool QueryInterface(uint32_t riid, void **out) override; // 0: 0xbdb350; thunks 0xbdb377, 0xbdb37f
    uint32_t AddRef() override;
    uint32_t Release() override;

    bool DoMessage(void *message) override; // face +4, slot 3 (0x18a7a0, folded): false

    bool Init() override;     // 0xbdb190
    bool Shutdown() override; // 0xbdb6d0
    cITSDecal *CreateDecal(const char *texture, const fx::Vector3 *position, float size, float rotation) override; // 0xbda440
    void AddOverlay(cITSOverlay *overlay) override; // 0xbda820, 0xbda830
    cITSOverlay *CreateWindowedOverlay(const char *texture, const float *rect, const float *matrix, const float *windowMin,
                                       const float *windowMax) override; // 0xbda710
    cITSOverlay *CreateOverlay(const char *texture, const float *rect, const float *matrix) override; // 0xbda610
    void RemoveOverlay(cITSOverlay *overlay) override; // 0xbdb390
    void GetStatus(cIGZString *out) override;          // 0xbdb170
    bool Update(cTSOverlayTable *table) override;      // 0xbdc0b0
    void SetGrid(float originX, float originY, float cellSize, int32_t width, int32_t height) override; // 0xbdb500
    void SetGridSize(float cellSize, int32_t width, int32_t height) override;                            // 0xbdb4d0
    void SetGround(cITSOverlayGround *ground) override;                                                  // 0x46e120

    void ComputeCells(const float *rect, int32_t *cells) const; // 0xbdabd0 (with 0xbdbef0, which does the same)
    void UpdateProjection(cTSDecal *decal);                     // 0xbdb750

    bool mInitialised = false;  // +0x10
    bool mChanged = true;       // +0x11
    uint8_t mPad12 = 0;         // +0x12 read by 0xbdbef0 only, never set
    uint8_t mPad13 = 1;         // +0x13
    float mOriginX = 0, mOriginY = 0; // +0x14
    int32_t mWidth = 0, mHeight = 0;  // +0x1c
    float mCellSize = 0;        // +0x24
    float mInverseCellSize = 0; // +0x28
    int32_t mCellMinX, mCellMinY, mCellMaxX, mCellMaxY; // +0x2c: unset until SetGrid
    fx::CountedRef<cITSOverlayGround> mGround;          // +0x3c
    std::list<fx::CountedRef<cTSOverlay>> mOverlays;    // +0x40
    int32_t mOverlayCount = 0;  // +0x48: overlays brought up to date by the last Update
    int32_t mDecalCount = 0;    // +0x4c: of those, decals
    std::vector<cTSOverlay *> mVisible; // +0x50: the last Update's
    uint32_t mUnused5c[4];      // +0x5c
    std::vector<std::list<void *>> mUnused6c; // +0x6c: 0xbda150 destroys it; nothing fills it
};
static_assert(sizeof(cTSOverlayManager) == 0x78, "cTSOverlayManager layout");

// The height maps (the same interface as the other effects' maps; only this test one is the effects' own).
class cITSEffectMap : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x2c2a7106;

    // 3. Nothing returned: ret 0x10, four outs, and the slot has no fifth, hidden one.
    virtual void GetBounds(float *minX, float *minY, float *maxX, float *maxY) = 0; // eax-ignored: last out pointer, reloaded
    virtual bool Contains(float x, float y) = 0;                                    // 4
    virtual float GetHeight(float x, float y) = 0;                                  // 5
    virtual fx::Vector3 *GetNormal(fx::Vector3 *normal, float x, float y) = 0;      // 6: by value, the hidden pointer back in eax
    virtual float GetScale() = 0;                                                   // 7
};

class cTSTestMap : public cITSEffectMap, public rz::cRZUnknown {
public:
    static constexpr uint32_t kClass = 0x2c2a7110;

    cTSTestMap() = default;
    ~cTSTestMap() override = default;           // deleting: 0x3a2ad0 through thunk 0xb87779, folded
    static cTSTestMap *__cdecl Create();        // 0xbd9700: null on failure

    bool QueryInterface(uint32_t riid, void **out) override; // 0: 0xbd99d0; +4 thunk 0xbd99f7
    uint32_t AddRef() override;
    uint32_t Release() override;

    void GetBounds(float *minX, float *minY, float *maxX, float *maxY) override; // 0xbd97a0 eax-ignored: the last out pointer, a scratch reload.
    bool Contains(float x, float y) override;                                    // 0xbd9960
    float GetHeight(float x, float y) override;                                  // 0xbd97d0
    fx::Vector3 *GetNormal(fx::Vector3 *normal, float x, float y) override;      // 0xbd9860
    float GetScale() override;                                                   // 0xbd9b70
};
static_assert(sizeof(cTSTestMap) == 0xc, "cTSTestMap layout");
