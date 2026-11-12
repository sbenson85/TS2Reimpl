#pragma once
// nTSSG::cShadowManager (lanes IR and 261108u, scenegraph rows LG6c / LG6d): the scene graph's shadow manager (framework
// class 0x0bf9c04f, interface 0xcbf9c006; cTSSGSystem keeps it at +0xa8). It hands out the shadows the objects cast:
// each node asks for one (slot 8) by its name and owner and gets an index into the entries, each with a texture of its
// own (or, in the render-target mode, the renderer's), and lets go of it (slot 9). Changes to the world (walls, lots,
// levels, the camera) queue the shadows to be drawn again; the frame update (slot 5) draws up to four of them. RVAs
// 1.18.0.187; all /O2.
//
// Layout (0xd4, operator new): cIShadowManager +0 (vtable 0xdb68ac, 18 slots), cIGZMessageTarget2 +4 (0xdb68f8).
//
// What is ours: the life cycle, every slot, DoMessage and their helpers, and the drawing (row LG6d, lane 261109a):
// slot 10's body 0x2e7380 (a static shadow made at a point), 0x2e9920 (one queued shadow drawn), their passes
// (0x2e8650, 0x2e87c0), the node preparation 0x2e7020 and the free helpers below (the bounds of a node 0x3ad230, the
// light direction 0x3ad6b0, the shadow camera's fit 0x3ad080 and a node's root bone 0x3ace40).
//
// Standalone shape (docs/standalone-shape.md): the director's factory 0x2e7ba0 is hooked and is the only constructor
// call (constructor 0x2e63a0, destructor 0x2e6c70, deleting destructor 0x2e6e10), so every object carries our vtables;
// the slots, the constructor and the destructor are in effect through it.
#include "gz/cIGZMessageTarget2.h"
#include "gz/cIGZUnknown.h"
#include "rz/nGZLog.h"
#include "sg/cBlurBrush.h"
#include "sg/cTextureManager.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <set>
#include <unordered_map>
#include <vector>

namespace nTSSG {

// What slot 16 returns (by value): an entry's mBox, or 0, 0, 0, 0, 0, 0, 1 for a bad index.
struct cShadowBox {
    float mValues[7];
};

// A shadow's key text ("<node name>_<owner>") and an entry's node name: a cRZString. The copy is the string's copy
// constructor; the assignment (the reuse path of slot 8) assigns the text only.
struct ShadowName : nGZLog::LogString {
    ShadowName() = default;
    ShadowName(const ShadowName &other) : LogString(other) {}
    ShadowName &operator=(const ShadowName &other)
    {
        mString = other.mString;
        return *this;
    }
};

// The map's hash: nGZLog::HashChars over the key's text (read directly, not through the string's slot 5).
struct ShadowKeyHash {
    size_t operator()(const nGZLog::LogString &key) const { return nGZLog::HashChars(key.mString.c_str()); }
};

// One shadow (0x98 bytes; the vector at +0x54). The default constructor is the original's 0x2e6940, the copy 0x2e6830,
// the destructor 0x2e6db0; the padding after mQueued is neither set nor copied.
struct cShadowEntry {
    int32_t mUsers = 0;                          // 0x00 in use while above zero
    ShadowName mName;                            // 0x04 the node's name
    int32_t mOwner = 0;                          // 0x24 the owner slot 8 was given
    uint8_t mQueued = 0;                         // 0x28 in the draw queue
    uint32_t mMatrixStamp = 0;                   // 0x2c slot 14 hands it out with the matrix
    nGZSceneGraph::cTextureHandle mTexture;      // 0x30 slot 13 turns it into the texture
    int32_t mScaleKeys[2] = {-1, -1};            // 0x34 the renderer's keys (render-target mode); slot 15 hands them on
    float mMatrix[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                         0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}; // 0x3c slot 14
    float mBox[7] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};         // 0x7c slot 16
};
static_assert(sizeof(cShadowEntry) == 0x98);
static_assert(offsetof(cShadowEntry, mName) == 0x04 && offsetof(cShadowEntry, mOwner) == 0x24 &&
              offsetof(cShadowEntry, mQueued) == 0x28 && offsetof(cShadowEntry, mMatrixStamp) == 0x2c &&
              offsetof(cShadowEntry, mTexture) == 0x30 && offsetof(cShadowEntry, mScaleKeys) == 0x34 &&
              offsetof(cShadowEntry, mMatrix) == 0x3c && offsetof(cShadowEntry, mBox) == 0x7c);

// The interface (0xcbf9c006).
class cIShadowManager : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                  // 3
    virtual bool Shutdown() = 0;                                              // 4
    virtual void Update() = 0;                                                // 5: each frame
    virtual void SetOption(uint32_t index, uint8_t on) = 0;                   // 6
    virtual void ResetResources() = 0;                                        // 7
    virtual int32_t AcquireShadow(cIGZUnknown *node, int32_t owner) = 0;      // 8
    virtual void ReleaseShadow(int32_t index) = 0;                            // 9
    virtual uint8_t MakeStaticShadow(const float *position, cIGZUnknown *node, uint32_t *outHandle) = 0; // 10 (LG6d)
    virtual void GetOptionBits(uint32_t *out) = 0;                            // 11
    virtual int32_t *ChangeCount() = 0;                                       // 12
    virtual uint32_t ShadowTexture(int32_t index) = 0;                        // 13
    virtual bool GetShadowMatrix(int32_t index, float *out, uint32_t *stamp) = 0; // 14
    virtual void GetShadowScale(int32_t index, float *offset, float *scale) = 0;  // 15
    virtual cShadowBox GetShadowBox(int32_t index) = 0;                       // 16
    virtual ~cIShadowManager() = default;                                     // 17: the deleting destructor
};

// A counted pointer member: released (slot 2) at destruction when set.
template <class T> struct ShadowRef {
    T *p = nullptr;
    ShadowRef() = default;
    ShadowRef(const ShadowRef &) = delete;
    ShadowRef &operator=(const ShadowRef &) = delete;
    ~ShadowRef()
    {
        if (p) p->Release(); // receiver: the held object (cIGZUnknown, slot 2)
    }
};

class cShadowManager final : public cIShadowManager, public cIGZMessageTarget2 {
public:
    static constexpr uint32_t kClass = 0x0bf9c04f;
    static constexpr uint32_t kIid = 0xcbf9c006;
    static constexpr uint32_t kOptionCount = 10;

    cShadowManager();           // 0x2e63a0
    ~cShadowManager() override; // 0x2e6c70; deleting 0x2e6e10
    static cShadowManager *__cdecl Create(); // 0x2e7ba0: null on failure
    static cShadowManager *__fastcall Construct(void *memory, void *); // the tests' way to the constructor

    bool QueryInterface(uint32_t iid, void **out) override; // 0: 0x2e89a0 (thunk on the +4 face)
    uint32_t AddRef() override;                             // 1: library-matched 0x15e520 (++mRefCount)
    uint32_t Release() override;                            // 2: 0x2e8a10

    bool DoMessage(void *message) override; // face +4, slot 3: 0x2e77c0

    // 0x2e7c10: once: the option bits reset (4, 6, 0-3 set, 5 clear, each counted), the messages registered, the
    // render-target manager taken from the common system, the resources made. False when already started.
    bool Init() override;
    // 0x2e8de0: the messages unregistered, the resources and the renderer let go; false when not started.
    bool Shutdown() override;
    // 0x2e80d0: the changed segments' shadows invalidated, the camera followed, up to four queued shadows drawn, the
    // settings read (each change counted).
    void Update() override;
    // 0x2e8d80 (ret 8): option bit `index` (0..9; others ignored) set to `on`, counted in mChangeCount when it changes.
    // `on` is compared as a byte with the bit (0 or 1), so any other value counts as a change that sets it.
    void SetOption(uint32_t index, uint8_t on) override;
    void ResetResources() override; // 0x2e8a00: the resources let go and made again
    // 0x2e8e90 (ret 8): the shadow of `node` (its slot 124 name) for `owner`, counted; -1 when there is none.
    int32_t AcquireShadow(cIGZUnknown *node, int32_t owner) override;
    void ReleaseShadow(int32_t index) override; // 0x2e8a30
    // 0x2e7380 (ret 0xc): `node` lit from `position` and drawn into a new image, which a new light projects (the light
    // held by a new light-reference node, handed out counted in *outHandle; 1). The node is moved under mRefC4 for the
    // pass and back to its old parent afterwards, except when the image cannot be made (then it stays there and the
    // result is 0); 0 as well when the shadow camera finds nothing to fit.
    uint8_t MakeStaticShadow(const float *position, cIGZUnknown *node, uint32_t *outHandle) override;
    void GetOptionBits(uint32_t *out) override; // 0x2e7b90 (ret 4)
    int32_t *ChangeCount() override;            // shared_vf_1758b0 (4 bytes): &mChangeCount
    // 0x2e9620 (ret 4): the texture of shadow `index` (the service 0x20dfb5's slot 24 on its handle); 0 for a bad index
    // or an unused entry.
    uint32_t ShadowTexture(int32_t index) override;
    // 0x2e9480 (ret 0xc): copies shadow `index`'s matrix to `out` and true, unless the index is bad or the entry unused,
    // or `stamp` is given and already equals the entry's stamp (else it gets it).
    bool GetShadowMatrix(int32_t index, float *out, uint32_t *stamp) override;
    // 0x2e95a0 (ret 0xc): the renderer's (+0x50) slot 8 with shadow `index`'s two keys, `offset` and `scale`; for a bad
    // index offset = (0, 0) and scale = (1, 1).
    void GetShadowScale(int32_t index, float *offset, float *scale) override;
    cShadowBox GetShadowBox(int32_t index) override; // 0x2e9500 (ret 8)

    // Helpers (thiscall in the original; ours are reached only from the slots above).
    void CreateResources();                  // 0x2e7d10
    void ReleaseResources();                 // 0x2e8e30
    void Queue(int32_t index);               // 0x2e6f40: a used, unqueued entry queued
    void QueueAll();                         // 0x2e6e40: every used entry pushed (the flags not read or set)
    void Unqueue(int32_t index);             // 0x2e8bf0: the first match taken out of the queue
    void DrainQueue();                       // 0x2e8920: up to four drawn (DrawShadow)
    // 0x2e9920 (ret 4): shadow `index` drawn (a used entry only): its node, found by name through the camera's owner,
    // prepared, placed under mRefC4 turned by the owner's facing, the camera fitted, and the pass drawn into the
    // entry's matrix (and box, from the node's root bone), the stamp counted.
    // ORIGINAL-BUG: the entry's mQueued is never cleared, so Queue never queues a drawn shadow again.
    void DrawShadow(int32_t index);
    // 0x2e8650 (ret 8): the static pass: the viewer drawn, then the brush into `texture` (when it is ready); `out` the
    // light's view-projection matrix (sixteen floats). Nothing without a ready texture.
    void RenderStatic(cIGZUnknown *texture, float *out);
    // 0x2e87c0 (ret 0xc): the same pass into the renderer's target (key0, key1); nothing for key0 < 0.
    void RenderTarget(int32_t key0, int32_t key1, float *out);
    // 0x2e7020 (cdecl): the node's shadow materials: "GUOBOff" bound to the ground and wall shadow subsets (0x3ad4a0),
    // then every shape part with a "tsNoShadow" data list has each listed subset (an int item, non-zero) bound to
    // "GUOBOff"; a shape with more than one referrer is cloned first and the clone takes its place.
    static void __cdecl PrepareNode(cIGZUnknown *node);
    void TrackCamera();                      // 0x2e9700
    void SegmentChanged(int32_t id);         // 0x2e9690
    void Invalidate(const uint32_t *segment); // 0x2e97d0: the objects on the segment's two quadrants told
    static uint8_t Lookup(uint32_t object, void **out);  // 0x2e8d00 (cdecl): the face's slot 12 (iids 0xfa1c39f7, 0xfa1c39ed)

    // Fields (the constructor's values).
    uint8_t mStarted = 0;                    // +0x08
    uint32_t mRefCount = 0;                  // +0x0c
    std::set<int32_t> mChangedSegments;      // +0x10 DoMessage 0xcb46c21d adds; the update empties it
    std::set<uint32_t> mMessageIds;          // +0x18 the six message ids
    int32_t mChangeCount = 1;                // +0x20 every option and setting change adds one
    float mShadowSize = 1.0f;                // +0x24 shadowSize
    float mCameraDistance = 10.0f;           // +0x28 shadowCameraDistance
    int32_t mBlurRadius = 3;                 // +0x2c shadowBlurRadius (or indoorShadowBlurRadius)
    int32_t mOutdoorBlurRadius = 2;          // +0x30 outdoorShadowBlurRadius: the brush's radius
    uint32_t mResolution = 0x80;             // +0x34 shadowResolution: a shadow's texels before its size scales them
    uint32_t mBaseResolution = 0x80;         // +0x38 baseShadowResolution: the cap, and the pass-1 image's size
    float mW0 = 0.7f;                        // +0x3c shadowW0
    float mH0 = -1.0f;                       // +0x40 shadowH0
    float mW1 = 0.7f;                        // +0x44 shadowW1
    float mH1 = 2.0f;                        // +0x48 shadowH1
    uint32_t mOptionBits = 0;                // +0x4c bits 0..9 (slot 6)
    ShadowRef<cIGZUnknown> mRenderer;        // +0x50 the common system's render-target manager (Init)
    std::vector<cShadowEntry> mShadows;      // +0x54
    std::vector<int32_t> mFreeIndices;       // +0x60
    std::unordered_map<ShadowName, int32_t, ShadowKeyHash, nGZLog::LogStringEq> mIndexByName; // +0x6c
    std::deque<uint32_t> mQueue;             // +0x8c the shadows to draw
    float mCamera[3] = {};                   // +0xa0 the camera's negated position when the shadows were last queued
    float mLightDirection[4] = {0.0f, 0.0f, 0.0f, 1.0f}; // +0xac (0x3ad6b0 of the position)
    ShadowRef<cIGZUnknown> mRefBC;           // +0xbc the director's class 0xdca76dbb
    ShadowRef<cIGZUnknown> mRefC0;           // +0xc0 class 0x25232b11
    ShadowRef<cIGZUnknown> mRefC4;           // +0xc4 class 0x65246462 (ReleaseResources leaves it)
    nGZSceneGraph::cTextureHandle mPass1;    // +0xc8 "staticShadowPass1"
    ShadowRef<cBlurBrush> mBrush;            // +0xcc
    uint8_t mRenderTargets = 0;              // +0xd0 shadows drawn by the renderer
    uint8_t mEnabled = 1;                    // +0xd1 slot 8 hands out nothing without it

private:
    cShadowEntry *Entry(int32_t index);
};
static_assert(offsetof(cShadowManager, mStarted) == 0x08 && offsetof(cShadowManager, mRefCount) == 0x0c &&
              offsetof(cShadowManager, mChangedSegments) == 0x10 && offsetof(cShadowManager, mMessageIds) == 0x18 &&
              offsetof(cShadowManager, mChangeCount) == 0x20 && offsetof(cShadowManager, mBaseResolution) == 0x38 &&
              offsetof(cShadowManager, mOptionBits) == 0x4c && offsetof(cShadowManager, mRenderer) == 0x50 &&
              offsetof(cShadowManager, mShadows) == 0x54 && offsetof(cShadowManager, mFreeIndices) == 0x60 &&
              offsetof(cShadowManager, mIndexByName) == 0x6c && offsetof(cShadowManager, mQueue) == 0x8c &&
              offsetof(cShadowManager, mCamera) == 0xa0 && offsetof(cShadowManager, mLightDirection) == 0xac &&
              offsetof(cShadowManager, mRefBC) == 0xbc && offsetof(cShadowManager, mPass1) == 0xc8 &&
              offsetof(cShadowManager, mBrush) == 0xcc && offsetof(cShadowManager, mRenderTargets) == 0xd0 &&
              offsetof(cShadowManager, mEnabled) == 0xd1,
              "cShadowManager layout");
static_assert(sizeof(cShadowManager) == 0xd4, "cShadowManager size");

// The shadow camera's helpers (cdecl free functions).
// 0x3ad230: `box` (min x, y, z, max x, y, z) grown by the box of `node`'s shapes and those of its children, recursively
// (a child is skipped when mask >= 0 and its flag `mask` is set), each brought into `reference`'s frame. A node counts
// when its flag 4 is set, it is not everywhere and a shape answers (slot 97); its min z is raised to 0 first.
void __cdecl ShadowBounds(float *box, int32_t mask, cIGZUnknown *reference, cIGZUnknown *node);
// 0x3ad6b0: the light rotation for a position (a quaternion: the yaw about z towards it, then the pitch about x); out.
float *__cdecl ShadowDirection(float *out, const float *position);
// 0x3ad080: the viewer's orthographic camera fitted to the bounds of `node` seen from the viewer (half a unit of margin
// across, the depth as the clip range, the aspect to match); false when the box is empty or degenerate.
uint8_t __cdecl FitShadowCamera(cIGZUnknown *viewer, cIGZUnknown *node);
// 0x3ace40: the node's root bone: its animated face's bone 0x6c918d88 or 0xe6e97a71, else the named node "root_rot" or
// "root_trans"; failing those "chairdining_rot" or "chairdining_trans". Null when none.
void *__cdecl FindRootBone(cIGZUnknown *node);

} // namespace nTSSG
