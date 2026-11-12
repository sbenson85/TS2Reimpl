#pragma once
// nTSSG::cShadow (lane 261109e, scenegraph row LG7d): one object's shadow (framework class 0x6aa7ec20, interface
// 0x4aa7ec1a). It holds the object's node (+0x20), picks a mode from the shadow manager's option bits and the object
// (SelectMode), binds the object's shadow subsets ("groundshadow", the wall shadows) to their own materials or to
// "GUOBOff" / "VisGUOBMaterial", and each frame (slot 12) asks the manager for a shadow of the object's owner, places
// it under the object's root bone and hands the projection to its two projectors (+0x48, +0x4c). RVAs 1.18.0.187;
// all /O2.
//
// Layout (0x150, operator new): cIShadow +0 (vtable 0xdba2f4, 24 slots). Factory 0x355700 (the director's class
// 0x6aa7ec20), constructor 0x354320, destructor 0x354480, deleting destructor 0x3545b0 (slot 23).
//
// Standalone shape (docs/standalone-shape.md): the factory is hooked and is the only constructor call, so every object
// carries our vtable; the slots, the constructor and the destructor are in effect through it. The helpers other code
// calls (0x3aced0, 0x3acd30) are hooked; ObjectVisible 0x355bd0, UpdateLevel 0x357750 and row LG7d3's (lane 261109h)
// CreateResources 0x354790 with its helpers 0x354630, 0x3546b0 and cObjectShadowFilter are in effect through their
// callers (all ours).
#include "gz/cIGZUnknown.h"
#include "rz/RZUnknownBase.h"
#include "rz/nGZLog.h"
#include "sg/SGNodeInterfaces.h" // cIVisibilityQueryFilter
#include "sg/cShadowManager.h"   // ShadowRef

#include <cstddef>
#include <cstdint>

namespace nTSSG {

// The interface (0x4aa7ec1a). Names are ours.
class cIShadow : public cIGZUnknown {
public:
    virtual bool Init() = 0;                         // 3
    virtual bool Shutdown() = 0;                     // 4
    virtual void SetObject(cIGZUnknown *object) = 0; // 5
    virtual void SetMode(int32_t mode) = 0;          // 6: the requested mode
    virtual void SetEnabled(uint8_t on) = 0;         // 7
    virtual void Suspend() = 0;                      // 8: hidden, the materials let go
    virtual void Resume() = 0;                       // 9: the materials bound, shown
    virtual void Show() = 0;                         // 10
    virtual void Hide() = 0;                         // 11
    virtual void Update() = 0;                       // 12: each frame
    virtual void Refresh() = 0;                      // 13
    virtual void Slot14() = 0;                       // 14: nothing (shared 0x140fe0)
    virtual void ApplyMaterials() = 0;               // 15
    virtual void RestoreMaterials() = 0;             // 16: every subset its own material
    virtual void ApplyMaterialsAgain() = 0;          // 17: a jump to 15's body
    virtual uint8_t IsShown() = 0;                   // 18 (shared 0x355bc0)
    virtual uint8_t IsEnabled() = 0;                 // 19 (shared 0x1b7090)
    virtual int32_t RequestedMode() = 0;             // 20 (shared 0x148ba0)
    virtual int32_t Mode() = 0;                      // 21 (shared 0x147d00)
    virtual bool HasMaterial(uint32_t subset) = 0;   // 22
    virtual ~cIShadow() = default;                   // 23: the deleting destructor
};

// The object (+0x20): counted through its cIGZUnknown virtual base (the vbtable at +4), released at destruction.
struct ShadowObjectRef {
    cIGZUnknown *p = nullptr;
    ShadowObjectRef() = default;
    ShadowObjectRef(const ShadowObjectRef &) = delete;
    ShadowObjectRef &operator=(const ShadowObjectRef &) = delete;
    ~ShadowObjectRef();
};

class cShadow final : public cIShadow {
public:
    static constexpr uint32_t kClass = 0x6aa7ec20;
    static constexpr uint32_t kIid = 0x4aa7ec1a;
    static constexpr uint32_t kSubsets = 6;

    cShadow();           // 0x354320
    ~cShadow() override; // 0x354480; deleting 0x3545b0
    static cShadow *__cdecl Create(); // 0x355700: null on failure
    static cShadow *__fastcall Construct(void *memory, void *); // the tests' way to the constructor

    bool QueryInterface(uint32_t iid, void **out) override; // 0: 0x356880
    uint32_t AddRef() override;                             // 1: shared 0x148fb0 (++mRefCount)
    uint32_t Release() override;                            // 2: shared 0x356910 (slot 23 with 1 at zero)

    // 0x355ab0: once: shown, enabled and the resources cleared, the camera's negated position and the manager's change
    // count taken. False when already started.
    bool Init() override;
    // 0x3569e0: the resources and the materials let go, the object released; false when not started.
    bool Shutdown() override;
    // 0x356950 (ret 4): the object held (counted through its virtual base), then the materials bound and applied, the
    // ground level, the visibility and the settings brought up to date.
    void SetObject(cIGZUnknown *object) override;
    void SetMode(int32_t mode) override;  // 0x3569c0 (ret 4): on a change, SelectMode(1)
    void SetEnabled(uint8_t on) override; // 0x356930 (ret 4): on a change, the visibility and the materials
    void Suspend() override;              // 0x356b10: hidden; the resources let go in mode 2; the materials unbound
    void Resume() override;               // 0x3568b0: the materials bound and applied, shown
    void Show() override;                 // 0x3545e0: the node put under the scene's root, the visibility
    void Hide() override;                 // 0x3553b0: the node taken out, both projectors off
    // 0x355d20: the frame update: the mode, the resources, the shadow of the owner in mode 2 placed under the root
    // bone and handed to the projectors; the camera followed.
    void Update() override;
    // 0x355be0: the mode forced, everything brought up to date; the grid followed to the ground level.
    void Refresh() override;
    void Slot14() override;               // shared 0x140fe0: nothing
    // 0x356cc0: each subset's material for the mode: in mode 3 (the option bits 7 and 9, the enabled flag, the
    // ground test of the wall subsets) "VisGUOBMaterial", its own, or "GUOBOff"; otherwise "GUOBOff".
    void ApplyMaterials() override;
    void RestoreMaterials() override;     // 0x355cf0: every bound subset its own material
    void ApplyMaterialsAgain() override;  // 0x355ce0: jump to ApplyMaterials
    uint8_t IsShown() override;           // shared 0x355bc0: mShown
    uint8_t IsEnabled() override;         // shared 0x1b7090: mEnabled
    int32_t RequestedMode() override;     // shared 0x148ba0: mRequestedMode
    int32_t Mode() override;              // shared 0x147d00: mMode
    bool HasMaterial(uint32_t subset) override; // 0x355a90 (ret 4): subset's material text non-empty

    // Helpers (thiscall unless noted).
    // 0x355290: the node out of its parent; the target, drawable, viewer and node let go; the shadow index given back;
    // the projectors out of their grids and let go; the grid let go.
    void ReleaseResources();
    void UnbindMaterials(); // 0x356a20: each bound subset its own material again, the texts cleared
    // 0x355b50: once, each subset's material text read from the object; then the wall subsets "GUOBOff".
    void BindMaterials();
    // 0x355410 (ret 0xc): subset `index` bound to "GUOBOff" (off), "VisGUOBMaterial" (on, visible) or its own text.
    void BindSubset(uint32_t index, uint8_t on, uint8_t visible);
    void UpdateLevel();        // 0x357750: the ground level under the object's box centre (0 without an object)
    // 0x354790: in modes 4 and 1 the render target ("shadowTexture<this>", the settings' resolution), the viewer that
    // draws the object's render group into it (an orthographic view, a cObjectShadowFilter for the object's owner) under
    // the node, and in mode 1 the drawable (a quad shape with its own material); then in modes 2, 4 and 1 the grid for
    // the ground level and the projectors on it (the floor one in mode 2, or with the option bit 6). Sets mSettingsDone
    // outside modes 4 and 1; false (nothing made) when the target got no texture.
    bool CreateResources();
    void UpdateVisibility();   // 0x357d70: the drawable, the projectors on or off
    bool SelectMode(uint8_t force); // 0x356b40 (ret 4): the mode from the options and the object; true on a change
    void ModeChanged();        // 0x3568d0: the resources made again (when made), then the materials applied
    bool FollowsCamera();      // 0x355a70: mode 4 or 1
    bool ObjectVisible();      // 0x355bd0: the object's slot 112
    // 0x356e40: the viewer turned and its orthographic view set for the manager's settings: when the object is
    // visible (outdoors), the view along the camera's negated position (the outdoor blur radius); otherwise straight
    // down (the indoor one). In mode 1 the view's box comes from the W / H settings and the extents (+0x54) follow;
    // otherwise (visible only) the node is put at the root bone's position and the camera fitted to the object. Sets
    // mSettingsDone (mode 1, or the bone's position in view). Nothing without a viewer.
    void UpdateSettings();
    // 0x357810: the node (when there is one) put at the root bone's position: unrotated when the object is visible;
    // otherwise turned by the bone's yaw and lifted 0.005 above the object's height. Then, in modes 1 and 4 with a
    // viewer and the object visible, the projectors told the viewer's view-projection and the rectangle (mode 1: the
    // object's box centre plus the extents; mode 4: the footprint), the projector its tile quadrant. Mode 2 clears the
    // matrix stamp.
    void PlaceProjectors();

    uint32_t mRefCount = 0;                  // +0x04
    uint8_t mStarted = 0;                    // +0x08
    uint8_t mMaterialsBound = 0;             // +0x09
    uint8_t mResourcesMade = 0;              // +0x0a
    uint8_t mShown = 0;                      // +0x0b
    uint8_t mEnabled = 0;                    // +0x0c
    int32_t mChangeId = 0;                   // +0x10 the manager's change count last seen
    uint32_t mMatrixStamp = 0;               // +0x14 the manager's matrix stamp
    int32_t mRequestedMode = 0;              // +0x18
    int32_t mMode = 0;                       // +0x1c 0 none, 1, 2 the owner's shadow, 3 the materials, 4
    ShadowObjectRef mObject;                 // +0x20
    ShadowRef<cIGZUnknown> mNode;            // +0x24 a tree node of ours under the scene's root
    ShadowRef<cIGZUnknown> mDrawable;        // +0x28
    ShadowRef<cIGZUnknown> mViewer;          // +0x2c
    ShadowRef<cIGZUnknown> mTarget;          // +0x30
    float mCamera[3];                        // +0x34 the camera's negated position (not set by the constructor)
    int32_t mShadowIndex = -1;               // +0x40 the manager's shadow (mode 2)
    ShadowRef<cIGZUnknown> mGrid;            // +0x44
    ShadowRef<cIGZUnknown> mProjector;       // +0x48 the ground projector
    ShadowRef<cIGZUnknown> mFloorProjector;  // +0x4c the second projector (only on level 0 inside the lot)
    int32_t mLevel = 0;                      // +0x50 the level mGrid was picked for
    float mExtents[4] = {-1.0f, 1.0f, -1.0f, 1.0f}; // +0x54
    nGZLog::LogString mMaterials[kSubsets];  // +0x64 each subset's own material
    int32_t mOwner = 8;                      // +0x124
    int32_t mLastOwner = 8;                  // +0x128
    float mBone[7] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}; // +0x12c the root bone's transform last used
    int32_t mGroundLevel = 0;                // +0x148
    uint8_t mBoneValid = 0;                  // +0x14c
    uint8_t mSettingsDone = 0;               // +0x14d (0x356e40 sets it)
};
static_assert(offsetof(cShadow, mRefCount) == 0x04 && offsetof(cShadow, mStarted) == 0x08 &&
              offsetof(cShadow, mEnabled) == 0x0c && offsetof(cShadow, mChangeId) == 0x10 &&
              offsetof(cShadow, mMode) == 0x1c && offsetof(cShadow, mObject) == 0x20 &&
              offsetof(cShadow, mTarget) == 0x30 && offsetof(cShadow, mCamera) == 0x34 &&
              offsetof(cShadow, mShadowIndex) == 0x40 && offsetof(cShadow, mGrid) == 0x44 &&
              offsetof(cShadow, mFloorProjector) == 0x4c && offsetof(cShadow, mLevel) == 0x50 &&
              offsetof(cShadow, mExtents) == 0x54 && offsetof(cShadow, mMaterials) == 0x64 &&
              offsetof(cShadow, mOwner) == 0x124 && offsetof(cShadow, mBone) == 0x12c &&
              offsetof(cShadow, mGroundLevel) == 0x148 && offsetof(cShadow, mBoneValid) == 0x14c &&
              offsetof(cShadow, mSettingsDone) == 0x14d,
              "cShadow layout");
static_assert(sizeof(cShadow) == 0x150, "cShadow size");

// Free helpers (cdecl).
// 0x3ad380: where two of a projection's clip planes meet in x, y (p[0], p[1], p[3] of each); false when the
// determinant is not above 1e-6.
uint8_t __cdecl ClipPlanesMeet(const float *a, const float *b, float *out);
// 0x3aced0: the x, y rectangle a projection matrix covers (its rows 0 +- 3 and 1 +- 3 met pairwise); false when two
// planes don't meet (the outputs untouched).
uint8_t __cdecl ShadowFootprint(const float *matrix, float *outMin, float *outMax);
// 0x3558d0: `box` grown by the boxes of `node`'s shapes and its children's, recursively (a child is skipped when
// mask >= 0 and its flag `mask` is set); a node counts as ShadowBounds' (0x3ad230) does, in its own frame.
void __cdecl ObjectBounds(float *box, int32_t mask, cIGZUnknown *node);
// 0x3acd30: the material a shape of `node` (or of its children, depth first) names for `subset`; null when none.
const char *__cdecl FindSubsetMaterial(cIGZUnknown *node, const char *subset);
// 0x355470: FindSubsetMaterial, but null for "GUOBOff" (the node's slot 124 asked first, its answer unused).
const char *__cdecl SubsetMaterial(cIGZUnknown *node, const char *subset);
// 0xae7f00: the box (min, max) of one subset's elements over `node`'s shapes, in the shapes' own frame: each shape's
// shown part's mesh walked for `subset`, each element's box (its floats +0x1c..+0x33) grown in; empty (FLT_MAX,
// -FLT_MAX) without any.
void __cdecl SubsetBounds(cIGZUnknown *node, const char *subset, float *out);
// 0x355990: `box` grown by SubsetBounds of `node` and of its children, recursively, each carried by its node's world
// transform; a node counts when its flag 4 is set, it doesn't reach everywhere and a shape answers slot 52.
void __cdecl SubsetWorldBounds(float *box, const char *subset, cIGZUnknown *node);
// 0x355770: the whole-tile span a box covers in x (box[0], box[3]) and y (box[1], box[4]), each rounded to nearest;
// a box that rounds to one point is widened by a tile towards the far side of its thin axis (under 0.01 wide).
void __cdecl TileSpan(const float *box, int32_t *x0, int32_t *x1, int32_t *y0, int32_t *y1);
// 0x3554b0: whether a wall subset's shadow lies on a floor: the subset's world box spans tiles, and placement's
// object (slot 39) answers slot 44 for each tile edge along it at the object's height; false when that height's level
// isn't shown (0x20df51 slot 22) or the game's mode (singleton B slot 28) is neither 2 nor 3.
uint8_t __cdecl FloorTest(cIGZUnknown *node, uint32_t subset);
// 0x354630: `remove` then `add` taken from / given to the node's render-group bits (when its flag 5 is set), then the
// same for its children, recursively, with the two masks swapped at each level (see the definition).
void __cdecl SwapLayerBitsDeep(cIGZUnknown *node, uint32_t remove, uint32_t add);
// 0x3546b0: a new material definition (the director's class 0x49596978) named `name` of type `shader`, its base texture
// `texture` and "debugMode" "1" or "0", added to the material manager (flags 2); the manager's index.
int32_t __cdecl AddShadowMaterial(const char *name, const char *texture, const char *shader, uint8_t debug);
// 0x3ad430: whether `point` falls in the viewer's view: its slot 143 (view 0) puts the point in clip space, and x, y
// must lie in [-1, 1] and z in [0, 1] (a NaN fails).
uint8_t __cdecl PointInView(cIGZUnknown *viewer, const float *point);

// The shadow viewer's visibility filter (the original's cObjectShadowFilter in an anonymous namespace; 0x14 bytes,
// vtables 0xdba368 / 0xdba37c; made only by CreateResources, which hands it to the viewer's slot 103). A node is drawn
// when its flag 8 is clear and, walking from it up its parents, one with flag 15 has an owner whose slot 47 is
// mOwnerId before the node's own render-group bits (when its flag 5 is set) are found to miss mMask.
class cObjectShadowFilter final : public nGZSceneGraph::cIVisibilityQueryFilter, public rz::cRZUnknown {
public:
    explicit cObjectShadowFilter(uint32_t mask) : mMask(mask) {}
    ~cObjectShadowFilter() override; // deleting 0x354580 (thunk 0x3545a3): only the cRZUnknown's destructor

    bool QueryInterface(uint32_t riid, void **out) override; // shared 0x1e2e00 (+4 thunk 0x356874)
    uint32_t AddRef() override;                              // shared 0x1dab20 (+4 thunk 0x3545d3)
    uint32_t Release() override;                             // shared 0x1e3970 (+4 thunk 0x356902)
    bool Accept(nGZSceneGraph::cIBoundedNode *node, void *viewer) override; // 0x356a70 (ret 8)

    uint16_t mOwnerId; // +0x0c set by the maker after construction (the object's owner's slot 47)
    uint32_t mMask;    // +0x10 the render group's bit
};
static_assert(offsetof(cObjectShadowFilter, mOwnerId) == 0xc && offsetof(cObjectShadowFilter, mMask) == 0x10 &&
                  sizeof(cObjectShadowFilter) == 0x14,
              "cObjectShadowFilter layout");

} // namespace nTSSG
