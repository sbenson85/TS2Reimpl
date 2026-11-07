#pragma once
// nGZSceneGraph::cDisplayList (docs/subsystems/scenegraph.md, rows DLa / DLb): the scene graph's display list. The
// viewers hand it what they draw each frame (geometry as sorted draw items, shapes, lights) in passes; it sorts and
// renders them through its render strategy into the device's dynamic buffers. It also keeps a slot allocator (indices
// freed in one frame become free at the second BeginFrame after it) and five performance switches the settings service
// keeps up to date.
//
// Standalone shape (docs/standalone-shape.md): our class with the game's layout. The factory 0xaa0e00 (the COM
// director's creator for class 0x49d02f15) is hooked and builds ours, so the constructor, the destructor and every slot
// are in effect through it. Every drawing slot is ours, with the light selection (cDisplayListLights.cpp) and the
// item log (cDisplayListLog.cpp): nothing of the class calls the image.
//
// Layout (0x160 bytes; vtables 0xe75404 (49 slots) and 0xe754cc at +8 (the message target, 4 slots)):
//   +0 vtable, +4 reference count and active bit (the behaviour template's), +8 the message target face
//   +0xc  the object 0xae8900 caches (set by Init), handed to the draw item builder
//   +0x10 / +0x14 the dynamic buffers' sizes in bytes (vertex, index)
//   +0x18 / +0x1c dynamic vertex arrays (kinds 1, 2), +0x20 / +0x24 dynamic index arrays (kinds 1, 2)
//   +0x28 the screen rect geometry (a cDynamicGeometryRect made at each device change)
//   +0x2c..+0x30 the five performance switches, +0x31 the buffers are made, +0x32 drawing
//   +0x34 the frame count, +0x38 the frame's time
//   +0x3c the passes, +0x48 the pass stack, +0x54..+0xb0 the render slots' working lists, +0xb4 the draw items
//   (the first +0xb0 of them sorted), +0xcc the slot table with its freed lists' heads at +0xd8 / +0xdc
//   +0xe0 the render strategy, +0xe8 six statistics words, +0x100 a matrix, +0x140 / +0x14c two vectors
//   +0x158 / +0x15c the log (frames, stream)
#include "compgeom/CompGeom.h" // BoundBox
#include "compgeom/Frustum.h"
#include "compgeom/SeparatingAxes.h" // FramePlacement
#include "gz/cAbstractBehaviorTypeST.h"
#include "gz/cIGZMessageTarget2.h"
#include "misc/UtilitiesC.h" // AffineTransform3
#include "sg/SGBehaviorCopies.h" // CopyTraits, DisplayListRegistryFor, the cell 0x10f6628
#include "sg/cFrameCache.h" // cFrameCache::Blob, LightChoices
#include "sg/cIDisplayList.h"
#include "game/owned.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// The game's identity frames (word 0 zero, the identity matrix, no offset): one the passes copy, one whose address the
// builder puts in a draw item's +0x28.
TS2_OWNED_STATIC(const AffineTransform3, 106e3d8);
TS2_OWNED_STATIC(const AffineTransform3, 10728c8);

namespace nGZGraphic4 {
class cDeviceStateDX;
class cIDeviceDX;
class cIDynamicVertexArray;
} // namespace nGZGraphic4

namespace nGZSceneGraph {

class cDisplayList;
class cIGeometryRender;
class cILightRefNode;
class cIMaterialManager;
class cIRenderableNode;
class cIViewerRefNodeBase;

// One thing to draw (0x44 bytes): what the builder 0xaeae00 fills for a geometry in a pass. The list keeps them in
// DrawItemOrder; the render slots sort them again (DrawItemOrder, DrawItemLayerOrder).
struct DrawItem {
    uint32_t mWord00;    // +0x00 (the builder leaves it)
    void *mMaterial;     // +0x04 the material's variant for the item's state (the material manager's slot 22)
    int32_t mSortKey;    // +0x08 the variant's sort key; odd keys are sorted by depth
    void *mGeometry;     // +0x0c
    uint32_t mUserData;  // +0x10 (slot 29's last argument; slot 24's index plus its base)
    uint32_t mSubKey;    // +0x14 the low 16 bits order items of one geometry
    int32_t mLayer;      // +0x18
    float mDepth;        // +0x1c
    uint32_t mWords20[2];
    const void *mPlacement; // +0x28 (the builder: the game's 0x10728c8)
    void *mRecord;       // +0x2c the pass's current node record (Pass::mRecord)
    uint32_t mState[3];  // +0x30 packed state bits (the variant's key)
    uint32_t mWord3c;
    int32_t mMaterialIndex; // +0x40 the material manager's index
};
static_assert(sizeof(DrawItem) == 0x44, "the original's draw item");
static_assert(offsetof(DrawItem, mDepth) == 0x1c && offsetof(DrawItem, mRecord) == 0x2c &&
                  offsetof(DrawItem, mMaterialIndex) == 0x40,
              "draw item offsets");

// The draw items' order (0xaa1290's lower bound, the sort 0xa9cf70): by sort key; even keys by material, geometry and
// sub key; odd keys back to front (the larger depth first; a NaN depth is never before anything).
struct DrawItemOrder {
    bool operator()(const DrawItem &a, const DrawItem &b) const
    {
        if (a.mSortKey != b.mSortKey) return a.mSortKey < b.mSortKey;
        if ((a.mSortKey & 1) == 0) {
            if (a.mMaterial != b.mMaterial)
                return reinterpret_cast<uintptr_t>(a.mMaterial) < reinterpret_cast<uintptr_t>(b.mMaterial);
            if (a.mGeometry != b.mGeometry)
                return reinterpret_cast<uintptr_t>(a.mGeometry) < reinterpret_cast<uintptr_t>(b.mGeometry);
            return static_cast<uint16_t>(a.mSubKey) < static_cast<uint16_t>(b.mSubKey);
        }
        return a.mDepth > b.mDepth;
    }
};

// The second order (the sort 0xa9d100, slot 28): by sort key; even keys by material and sub key (not the geometry);
// odd keys by layer (the higher first), then front to back by depth.
struct DrawItemLayerOrder {
    bool operator()(const DrawItem &a, const DrawItem &b) const
    {
        if (a.mSortKey != b.mSortKey) return a.mSortKey < b.mSortKey;
        if ((a.mSortKey & 1) == 0) {
            if (a.mMaterial != b.mMaterial)
                return reinterpret_cast<uintptr_t>(a.mMaterial) < reinterpret_cast<uintptr_t>(b.mMaterial);
            return static_cast<uint16_t>(a.mSubKey) < static_cast<uint16_t>(b.mSubKey);
        }
        if (a.mLayer != b.mLayer) return a.mLayer > b.mLayer;
        return a.mDepth < b.mDepth;
    }
};

// An object (a light) with a flag in one of a pass's entry lists (slots 26 / 27).
struct PassEntry {
    void *mObject;
    uint8_t mFlag;
    uint8_t mPad[3];
};
static_assert(sizeof(PassEntry) == 8, "the original's pass entry");

// A node a pass draws (0x5c bytes): what DrawNode (slot 25) records for a shape node, and the pass's own record for
// its viewer (Pass::mViewerRecord, which DrawViewer fills). A new pass's viewer record has 0x1f in the placement's
// first word.
struct NodeRecord {
    cIRenderableNode *mNode;          // +0x00
    uint32_t mLayerState[2];          // +0x04 the node's slot 86 (its layer mask and word)
    uint8_t mCurrent;                 // +0x0c the node's slot 88 equals the display list's frame
    uint8_t mPlaneMask;               // +0x0d the frustum planes the node's box crosses
    uint8_t mPad0e[2];
    AffineTransform3 mPlacement;      // +0x10
    nGZCompGeom::BoundBox mBounds;    // +0x44
};
static_assert(sizeof(NodeRecord) == 0x5c && offsetof(NodeRecord, mPlacement) == 0x10 &&
                  offsetof(NodeRecord, mBounds) == 0x44,
              "the original's node record");

// One pass (0x350 bytes): a view as DrawViewer (slot 23) sets it up from a viewer node, the context the render strategy
// and the geometry draw with (cGeometryRenderContext, cViewerView and ShaderRenderCtx are other headers' partial views
// of it). Value-initialised (0xa9d350 per element): zero, the two frames' matrices and the five 4x4 matrices the
// identity (0xa9d950), the viewer record's placement word 0x1f. Moving one (0xa9c680) copies it and takes its lists.
struct Pass {
    Pass();

    // Whether the pass shows something with this layer state (a node's slot 86): a mask bit in common, and with a viewer
    // tag match (the viewer's slot 87) other than 2 the layer tags equal (0) or different (1); 2 ignores the tags.
    bool Shows(const uint32_t layerState[2]) const
    {
        if (!(mLayerState[0] & layerState[0])) return false;
        return mLayerTagMatch == 2 || ((layerState[1] == mLayerState[1]) != (mLayerTagMatch == 1));
    }

    cDisplayList *mOwner;              // +0x000
    uint32_t mFrame;                   // +0x004 the display list's frame and time
    float mTime;                       // +0x008
    cIViewerRefNodeBase *mViewer;          // +0x00c
    uint32_t mRenderType;              // +0x010 the viewer's slot 115
    uint32_t mLayerState[2];           // +0x014 the viewer's slot 86 (-1, 0 until DrawViewer)
    uint8_t mLayerTagMatch;            // +0x01c the viewer's slot 87 (2 until DrawViewer)
    uint8_t mPad1d[3];
    uint32_t mTextureMode;             // +0x020 the viewer's slot 122
    nGZCompGeom::Frustum mFrustum;     // +0x024 of mMatrices[0]
    float mViewportMin[2];             // +0x084 DrawViewer's fractions of the target (0, 0 / 1, 1 / 0, 1 at setup)
    float mViewportMax[2];             // +0x08c
    float mDepthRange[2];              // +0x094
    cIGZUnknown *mColorSurface;        // +0x09c the viewer's slots 111, 112, 105, 106 (each texture), 107, 109
    cIGZUnknown *mDepthSurface;        // +0x0a0
    uint32_t mTextureCount;            // +0x0a4
    cIGZUnknown *mTextures[4];         // +0x0a8
    cIGZUnknown *mTextureRef94;        // +0x0b8
    cIGZUnknown *mHandleTexture;       // +0x0bc
    uint32_t mWidth;                   // +0x0c0 the viewer's slot 110: the target's size and scale
    uint32_t mHeight;                  // +0x0c4
    float mScale;                      // +0x0c8
    int32_t mPixelRect[4];             // +0x0cc the viewport in pixels (min x, min y, max x, max y), rounded
    nGZCompGeom::FramePlacement mView; // +0x0dc DrawViewer's frame (its offset the eye)
    nGZCompGeom::FramePlacement mViewInverse; // +0x110
    // +0x144: [0] the projection, [1] the inverse view times it, [2] DrawViewer's last matrix, [3] [0] times [2],
    // [4] [1] times [2].
    float mMatrices[5][16];
    Pass *mPrevious;                   // +0x284 the pass before it (SetPassCount)
    NodeRecord *mRecord;               // +0x288 the current node record: &mViewerRecord (SetPassCount)
    void *mLastShader;                 // +0x28c the shader the strategy last applied (ShaderRenderCtx)
    uint32_t mWords290[3];             // +0x290
    uint32_t mDrawStats[3];            // +0x29c added to the display list's stats 0, 4 and 5 after the strategy
    nGZGraphic4::cIDeviceDX *mDevice;  // +0x2a8
    nGZGraphic4::cDeviceStateDX *mDeviceState;           // +0x2ac
    nGZGraphic4::cIDynamicVertexArray *mVertices1;       // +0x2b0 the display list's four arrays
    nGZGraphic4::cIDynamicVertexArray *mVertices2;       // +0x2b4
    nGZGraphic4::cIDynamicVertexArray *mIndices1;        // +0x2b8
    nGZGraphic4::cIDynamicVertexArray *mIndices2;        // +0x2bc
    cFrameCache *mFrameCache;          // +0x2c0 the viewer's (DrawViewer; not counted)
    uint32_t mEntryCountA;             // +0x2c4 entries in use (the list only grows)
    std::vector<PassEntry> mEntriesA;  // +0x2c8 (slot 27)
    uint32_t mEntryCountB;             // +0x2d4
    std::vector<PassEntry> mEntriesB;  // +0x2d8
    uint32_t mNodeCount;               // +0x2e4 records in use (the list only grows)
    std::vector<NodeRecord> mNodes;    // +0x2e8 (slot 25)
    NodeRecord mViewerRecord;          // +0x2f4
};
static_assert(sizeof(Pass) == 0x350, "the original's pass");
static_assert(offsetof(Pass, mFrustum) == 0x24 && offsetof(Pass, mViewportMin) == 0x84 &&
                  offsetof(Pass, mTextures) == 0xa8 && offsetof(Pass, mPixelRect) == 0xcc && offsetof(Pass, mView) == 0xdc &&
                  offsetof(Pass, mViewInverse) == 0x110 && offsetof(Pass, mLastShader) == 0x28c &&
                  offsetof(Pass, mDrawStats) == 0x29c && offsetof(Pass, mDevice) == 0x2a8 &&
                  offsetof(Pass, mFrameCache) == 0x2c0,
              "pass offsets");
static_assert(offsetof(Pass, mMatrices) == 0x144 && offsetof(Pass, mPrevious) == 0x284 &&
                  offsetof(Pass, mEntryCountA) == 0x2c4 && offsetof(Pass, mEntriesB) == 0x2d8 &&
                  offsetof(Pass, mNodes) == 0x2e8 && offsetof(Pass, mViewerRecord) == 0x2f4,
              "pass offsets");

// A light the current scene holds (slot 30, any type but ambient; 0x80 bytes). New records (0xaa2890's growth) have the
// two frames' first words 0x1f (all the original's constructor 0xa9d320 sets; ours are zero elsewhere), and slot 30
// writes everything but the pad.
struct LightRecord {
    cILight *mLight;           // +0x00
    cILightRefNode *mNode;     // +0x04 the light node (its SetWord78 has the record's index; -1 at the pass reset)
    uint32_t mType;            // +0x08 the light's type (cILight 36)
    uint32_t mLayerState[2];   // +0x0c the node's (cIRenderableNode 86)
    uint8_t mFlag;             // +0x14 the node's light flag (87)
    uint8_t mCurrent;          // +0x15 the node's slot 88 is the display list's frame
    uint8_t mPad16[2];
    AffineTransform3 mFrame;   // +0x18 the light's frame
    AffineTransform3 mInverse; // +0x4c its inverse
};
static_assert(sizeof(LightRecord) == 0x80 && offsetof(LightRecord, mFrame) == 0x18 &&
                  offsetof(LightRecord, mInverse) == 0x4c,
              "the original's light record");

// An ambient light the current scene holds (slot 30; 0x10 bytes).
struct AmbientRecord {
    cILight *mLight;         // +0x00
    uint32_t mLayerState[2]; // +0x04 the node's
    uint8_t mFlag;           // +0x0c the node's light flag
    uint8_t mCurrent;        // +0x0d the node's slot 88 is the display list's frame
    uint8_t mPad0e[2];
};
static_assert(sizeof(AmbientRecord) == 0x10, "the original's ambient record");

using DisplayListBehaviorTraits =
    behavior_copies::CopyTraits<ts2_rva_10f6628, &behavior_copies::DisplayListRegistryFor>;

class cDisplayList : public nGZEvent::cAbstractBehaviorTypeST<cIDisplayList, DisplayListBehaviorTraits>,
                     public cIGZMessageTarget2 {
    using Behavior = nGZEvent::cAbstractBehaviorTypeST<cIDisplayList, DisplayListBehaviorTraits>;

public:
    static constexpr size_t kSize = 0x160;
    static constexpr uint32_t kIidBehavior2 = 0x3b253dc7;
    static constexpr uint32_t kIidBehaviorType = 0xfb859324;
    static constexpr uint32_t kGraphicSystemId = 0x85522803;
    static constexpr uint32_t kGraphicSystemIid = 0xa55227bb;
    static constexpr uint32_t kDeviceEvent = 0xca7ccbbc; // the graphics system's CurrentDeviceEvent
    static constexpr uint32_t kSettingsId = 0x0ac1887e;
    static constexpr uint32_t kSettingsIid = 0x6bb22f38;
    static constexpr uint32_t kMessageIid = 0x65297976; // cIGZMessage2Standard
    static constexpr int32_t kSlotEnd = -1;             // the free lists' end
    static constexpr int32_t kSlotFree = -2;            // a free slot
    static constexpr int32_t kSlotUsed = -3;            // an allocated slot
    static constexpr uint32_t kStats = 6;

    // The five switches: their settings messages (DoMessage), in the order of mSwitches.
    static constexpr uint32_t kSwitchMessages[5] = {0xadc5b46b, 0xcdc5b472, 0xcdc5b477, 0xcdc5b47b, 0xcdc5b480};

    // A counted pointer: the new one counted, stored, the old one released (nothing when they are the same).
    template <class T> struct Ref {
        T *p = nullptr;
        Ref() = default;
        Ref(const Ref &) = delete;
        Ref &operator=(const Ref &) = delete;
        ~Ref()
        {
            if (p) p->Release();
        }
        void Assign(T *object)
        {
            T *const old = p;
            if (object == old) return;
            if (object) object->AddRef();
            p = object;
            if (old) old->Release();
        }
        // Cleared, then the old one released.
        void Reset()
        {
            if (T *const old = p) {
                p = nullptr;
                old->Release();
            }
        }
    };

    cDisplayList();           // 0xa9d460
    ~cDisplayList() override; // 0xa9e090 (members); 0xa9e580 the deleting destructor

    // cIGZUnknown (both faces)
    bool QueryInterface(uint32_t riid, void **out) override; // 0xaa2160 (the +8 face's thunk 0xaa2192)
    uint32_t AddRef() override;
    uint32_t Release() override;

    // cIAbstractBehavior
    void OnEvent(uint32_t type, uint32_t arg1, void *data, uint32_t arg3) override; // 0xaa0f80

    // cIGZMessageTarget2 (+8): the settings' switch messages.
    bool DoMessage(void *message) override; // 0xaa06c0

    // cIDisplayList
    void Init() override;                                                            // 0xaa0fd0
    void Shutdown() override;                                                        // 0xaa2d10
    void BeginFrame(float time) override;                                            // 0xaa1570
    int32_t AllocSlots(uint32_t count) override;                                     // 0xa9f6a0
    int32_t ReallocSlots(int32_t start, uint32_t count, uint32_t newCount) override; // 0xaa21d0
    void FreeSlots(int32_t start, uint32_t count) override;                          // 0xaa0db0
    void BeginScene() override; // 0xa9fae0
    void EndScene() override;   // 0xaa08b0
    // 0xa9fb40 (cDisplayListDraw.cpp).
    Pass *DrawViewer(cIViewerRefNodeBase *viewer, const float *viewportMin, const float *viewportMax, const float *depthRange,
                     const nGZCompGeom::FramePlacement *view, const float *projection, const float *matrix) override;
    void DrawPass(uint32_t count, const int32_t *materials, uint32_t userDataBase) override; // 0xaa0920
    // 0xa9f4c0 (ret 0x10): a node record appended to the current pass (the node's slots 86, 88 and 90 asked, 90 told the
    // frame).
    void DrawNode(cICompositionTreeNode *node, const AffineTransform3 *placement, const nGZCompGeom::BoundBox *bounds,
                  uint32_t planeMask) override;
    // 0xa9f3c0 (ret 8): with the first switch on, the node and the plane mask's low byte appended to the current pass's
    // second entry list.
    void AddLightNode(cICompositionTreeNode *node, uint32_t planeMask) override;
    void AddPassEntry(void *object, uint8_t flag) override;                          // 0xa9f600
    void AddGeometrySubsets(cIGeometryRender *geometry, uint32_t flags, const int32_t *materials,
                            const uint32_t *const *subsetWords, const uint32_t *word24, const uint32_t (*layerStates)[2],
                            const nGZCompGeom::RigidTransform3 *bonePoses, uint32_t userDataBase) override; // 0xa9e700
    void AddGeometry(cIGeometryRender *geometry, int32_t material, Pass *pass, uint32_t userData) override; // 0xa9f460
    // 0xa9f140 (ret 8): the light of the pass entry mWords84[1] names recorded if the current pass shows its node: an
    // ambient one in mRecords78, any other with its frame and inverse in mRecords58 (the node told its index, slot 96),
    // and a current one (the node's slot 88 is the frame) counted (mWords84[0]) or listed (mWords68).
    void AddLight(cILight *light, const AffineTransform3 *frame) override;
    void SetPassCount(uint32_t count) override;                                      // 0xaa2b90
    void SetRenderStrategy(cIDisplayListRenderStrategy *strategy) override;          // 0xaa2c80
    void SetBufferSizes(uint32_t vertexKB, uint32_t indexKB) override;               // 0xaa2b30
    void OpenLog(const char *path, uint32_t frames) override;                        // 0xaa07f0
    uint32_t *TakeStats(uint32_t *out, uint8_t reset) override;                      // 0xaa2d90
    void SetMatrix(const float *matrix) override;                                    // 0xaa2b50
    void SetVector140(const float *v) override;                                      // 0xaa2cf0
    void SetVector14c(const float *v) override;                                      // 0xaa2b10
    bool IsDrawing() override;                                                       // shared_vf_6175c0
    bool HasPass() override;                                                         // 0xaa0fc0
    uint32_t GetSlotCount() override;                                                // 0xaa1620
    cIDisplayListRenderStrategy *GetRenderStrategy() override;                       // shared 0x142870
    uint32_t GetFrame() override;                                                    // shared 0x147800
    uint32_t *GetBufferSizes(uint32_t *vertexKB, uint32_t *indexKB) override;        // 0xaa0890
    const float *GetMatrix() override;                                               // 0xaa0ea0
    const float *GetVector140() override;                                            // 0xaa2e00
    const float *GetVector14c() override;                                            // 0xaa0690

    // 0xaa1290 (ret 4): the item inserted after the sorted ones it does not come before (an upper half lower bound in
    // DrawItemOrder over the first mSortedCount items); the sorted count grows by one.
    void InsertDrawItem(const DrawItem *item);
    // 0xaa11f0: the four dynamic buffers released and created again on a ready device (mBuffersReady when all were).
    void RecreateBuffers();
    // 0xaa0eb0 (ret 4): the device event's data; on a new device (its word 1 is 1) the buffers and the screen rect.
    void DeviceChanged(const uint32_t *event);

    // 0xaa2890 (ret 4): mRecords58 grown to `count` records; when they move, the current list's pointers (mWords68, the
    // first mCount64) follow to the new block.
    void GrowLights(uint32_t count);

    // Slot 28's helpers (cDisplayListSubsets.cpp).
    // 0xaa2920 (ret 4): mBoneFrames resized to `count` (new frames the game's identity); when the block moves, the items'
    // placements into the frames in use (the first mWords84[3]) follow, rounded down to a whole frame.
    void GrowBoneFrames(uint32_t count);
    // The light selection (cDisplayListLights.cpp).
    // 0xaa0130 (ret 0x14, bool in al; `material` unread): the item's lights for its box, placement and layer state, from
    // last frame's choice for its slot (mUserData) when that still holds, else chosen again and stored in the frame
    // cache; the item's state words 0-1 are the blob's light counts and mWord3c its light block in mBytesA4 (the ambient
    // colour, then the chosen records), or 0 without lights and ambient. With colour lights (type 7) the record's node is
    // asked for the colour event, and an alpha away from 1 sets state bit 3. True unless last frame's choice was reused.
    bool SelectLights(DrawItem *item, int32_t material, const nGZCompGeom::BoundBox *box,
                      const AffineTransform3 *placement, const uint32_t *layerState);
    // 0xaa2f40 (ret 0x18): the frame cache's blob for `slot` from last frame's (null when it has none from the last
    // flip): the ambient colour summed again when an ambient light is current; last frame's lights reused when none is
    // current and all still are lights, else chosen again from them (the gone and current ones dropped) and the current
    // lights. With a blob, `*stored` says which (false: last frame's reused); without, it is left.
    cFrameCache::Blob *FindCachedLights(uint32_t slot, DrawItem *item, const nGZCompGeom::BoundBox *box,
                                        const AffineTransform3 *placement, const uint32_t *layerState, bool *stored);
    // 0xaa1430 (__cdecl, bool in al): `choice` put in strongest-first order into `list` (strengths within 0.01 ordered
    // by the record's node pointer, greater first); false for no strength (not above 0) or when the list is full and its
    // last is greater. A full list loses its last.
    static bool __cdecl InsertLight(LightChoices *list, const LightChoice *choice);
    // 0xaa27e0 (ret 4): mBytesA4 resized to `bytes`; when its block moves, the sorted items' light blocks (mWord3c, when
    // set) follow (from no block at all when mWordA0 is 0).
    void GrowLightBytes(uint32_t bytes);
    // 0xaf2200 (__cdecl; returns out): the depth of the box's centre through the 4x4 `matrix` (its third row over its
    // fourth; 0 when the fourth is not above 1e-5).
    static float *__cdecl CentreDepth(float *out, const nGZCompGeom::BoundBox *box, const float *matrix);
    // 0xaf2340 (__cdecl): the least such depth of the box's eight corners (at most 1e6).
    // 0xaf2340 eax-ignored: eax is the last corner's address (a stack local); slot 28 does not read it.
    static void __cdecl NearestCornerDepth(float *out, const nGZCompGeom::BoundBox *box, const float *matrix);

    // The drawing slots' helpers (cDisplayListDraw.cpp).
    // 0xaa2540: the top pass reset for a view (the viewer fields, the viewport 0..1, the targets, the stats, the device
    // and its state, the four arrays, the lists' counts, the viewer record) and the render slots' counts cleared.
    void SetupTopPass();
    // 0xaa24c0 (ret 8): the strategy's slots 3 and 4 on the pass and the sorted items; any exception is swallowed.
    void RenderWithStrategy(cIDisplayListRenderStrategy *strategy, Pass *pass);
    // 0xaeae00 (__cdecl, bool in al): fills `item` for `geometry` with `material` in `pass`: the variant for the item's
    // state, its sort key; false for no material (-1) or pass, a variant without passes, or one the geometry refuses.
    static bool BuildDrawItem(DrawItem *item, cIGeometryRender *geometry, cIMaterialManager *materialManager,
                              int32_t material, Pass *pass);

    // The members, in the original's order (the destructor releases them in reverse, as 0xa9e090 does).
    cIMaterialManager *mMaterialManager = nullptr;            // +0x0c
    uint32_t mVertexBytes = 0;                                // +0x10
    uint32_t mIndexBytes = 0;                                 // +0x14
    Ref<nGZGraphic4::cIDynamicVertexArray> mVertices1;        // +0x18
    Ref<nGZGraphic4::cIDynamicVertexArray> mVertices2;        // +0x1c
    Ref<nGZGraphic4::cIDynamicVertexArray> mIndices1;         // +0x20
    Ref<nGZGraphic4::cIDynamicVertexArray> mIndices2;         // +0x24
    Ref<cIGeometryRender> mScreenRect;                        // +0x28
    uint8_t mSwitches[5] = {1, 1, 1, 1, 1};                   // +0x2c
    uint8_t mBuffersReady = 0;                                // +0x31
    uint8_t mDrawing = 0;                                     // +0x32
    uint8_t mPad33 = 0;
    uint32_t mFrame = 0;                                      // +0x34
    float mTime = 0.0f;                                       // +0x38
    std::vector<Pass> mPasses;                                // +0x3c
    std::vector<Pass *> mPassStack;                           // +0x48
    uint32_t mCount54 = 0;                                    // +0x54 lights in use
    std::vector<LightRecord> mRecords58;                      // +0x58 (the list only grows)
    uint32_t mCount64 = 0;                                    // +0x64
    std::vector<LightRecord *> mWords68;                      // +0x68 the current lights (mCurrent set)
    uint32_t mCount74 = 0;                                    // +0x74 ambient lights in use
    std::vector<AmbientRecord> mRecords78;                    // +0x78
    uint32_t mWords84[4] = {};                                // +0x84
    std::vector<nGZCompGeom::FramePlacement> mBoneFrames;     // +0x94 slot 28's bone frames (mWords84[3] in use)
    uint32_t mWordA0 = 0;                                     // +0xa0
    std::vector<char> mBytesA4;                               // +0xa4
    uint32_t mSortedCount = 0;                                // +0xb0
    std::vector<DrawItem> mItems;                             // +0xb4
    std::vector<float> mDepths;                               // +0xc0 slot 28's depth sort
    std::vector<int32_t> mSlots;                              // +0xcc
    int32_t mFreedThisFrame = kSlotEnd;                       // +0xd8 FreeSlots' list (through mSlots)
    int32_t mFreedLastFrame = kSlotEnd;                       // +0xdc last frame's; free at the next BeginFrame
    Ref<cIDisplayListRenderStrategy> mStrategy;               // +0xe0
    uint32_t mWordE4 = 0;                                     // +0xe4 (the constructor leaves it)
    uint32_t mStats[kStats] = {};                             // +0xe8
    float mMatrix[16] = {};                                   // +0x100 the identity at construction
    float mVector140[3] = {};                                 // +0x140 (the constructor leaves it)
    float mVector14c[3] = {};                                 // +0x14c (likewise)
    uint32_t mLogFrames = 0;                                  // +0x158
    Ref<cIGZUnknown> mLog;                                    // +0x15c a cRZOFStream
};

// The hooks' entry points (src/hook/tables/cDisplayList.cpp).
struct cDisplayListAdapters {
    // 0xaa0e00 (cdecl): the COM creator, a new display list (null when the allocation fails).
    static cIDisplayList *__cdecl Create();
};

} // namespace nGZSceneGraph

static_assert(offsetof(nGZSceneGraph::cDisplayList, mMaterialManager) == 0xc, "cDisplayList +0xc");
static_assert(offsetof(nGZSceneGraph::cDisplayList, mScreenRect) == 0x28, "cDisplayList +0x28");
static_assert(offsetof(nGZSceneGraph::cDisplayList, mSwitches) == 0x2c, "cDisplayList +0x2c");
static_assert(offsetof(nGZSceneGraph::cDisplayList, mPasses) == 0x3c, "cDisplayList +0x3c");
static_assert(offsetof(nGZSceneGraph::cDisplayList, mRecords58) == 0x58, "cDisplayList +0x58");
static_assert(offsetof(nGZSceneGraph::cDisplayList, mBoneFrames) == 0x94, "cDisplayList +0x94");
static_assert(offsetof(nGZSceneGraph::cDisplayList, mSortedCount) == 0xb0, "cDisplayList +0xb0");
static_assert(offsetof(nGZSceneGraph::cDisplayList, mSlots) == 0xcc, "cDisplayList +0xcc");
static_assert(offsetof(nGZSceneGraph::cDisplayList, mStrategy) == 0xe0, "cDisplayList +0xe0");
static_assert(offsetof(nGZSceneGraph::cDisplayList, mStats) == 0xe8, "cDisplayList +0xe8");
static_assert(offsetof(nGZSceneGraph::cDisplayList, mMatrix) == 0x100, "cDisplayList +0x100");
static_assert(offsetof(nGZSceneGraph::cDisplayList, mVector14c) == 0x14c, "cDisplayList +0x14c");
static_assert(offsetof(nGZSceneGraph::cDisplayList, mLog) == 0x15c, "cDisplayList +0x15c");
static_assert(sizeof(nGZSceneGraph::cDisplayList) == nGZSceneGraph::cDisplayList::kSize, "cDisplayList size");
