#pragma once
// nGZSceneGraph::cIDisplayList (COM class 0x49d02f15, interface 0x49d02f0b): the scene graph's display list, the object
// the viewers hand what they draw to (shapes, geometry, lights) and that sorts and renders it in passes. cDisplayList
// (sg/cDisplayList.h) is the only class on it. The names are ours; what each slot does is from cDisplayList's bodies.
//
//   slots 0-14  cIAbstractBehavior (the event behaviour: the graphics system's device event reaches slot 3)
//   15-20       start / stop (the settings notifications), the frame, the slot allocator
//   21-30       the drawing slots (DLb in docs/subsystems/scenegraph.md): the viewers' passes, shapes, geometry, lights
//   31-47       configuration and getters
//   48          the deleting destructor (the class's; this interface declares none)
#include "gz/cGZEventReflector.h" // nGZEvent::cIAbstractBehavior

#include <cstdint>

struct AffineTransform3;
namespace nGZCompGeom {
struct BoundBox;
struct FramePlacement;
struct RigidTransform3;
} // namespace nGZCompGeom

namespace nGZSceneGraph {

class cICompositionTreeNode;
class cIGeometryRender;
class cILight;
class cIViewerRefNodeBase;
struct DrawItem;
struct Pass;

// The render strategy the display list draws through (slot 32; the default is cDefaultDisplayListRenderStrategy,
// 8 bytes, constructor 0xa9d450; a viewer may name its own, its slot 117). Slot 24 calls 3 and 4 for each view.
class cIDisplayListRenderStrategy : public cIGZUnknown {
public:
    virtual void BeginPass(Pass *pass) = 0;                                // 3
    virtual void Render(Pass *pass, DrawItem *items, uint32_t count) = 0; // 4: the sorted items
};

class cIDisplayList : public nGZEvent::cIAbstractBehavior {
public:
    static constexpr uint32_t kIid = 0x49d02f0b;
    static constexpr uint32_t kClassId = 0x49d02f15;

    // 15: registers for the five performance switches with the settings service (notifications to the +8 face).
    virtual void Init() = 0;
    // 16: drops the settings notifications.
    virtual void Shutdown() = 0;
    // 17: a new frame at `time`: the frame count advances and the slots freed last frame become free.
    virtual void BeginFrame(float time) = 0;
    // 18: `count` consecutive slots (the first one's index): reuses a free run, else grows the table.
    virtual int32_t AllocSlots(uint32_t count) = 0;
    // 19: frees `count` slots at `start` (when count is not 0), then allocates `newCount` (0 when newCount is 0).
    virtual int32_t ReallocSlots(int32_t start, uint32_t count, uint32_t newCount) = 0;
    // 20: frees `count` slots at `start` (they become free at the next frame).
    virtual void FreeSlots(int32_t start, uint32_t count) = 0;

    // The drawing slots (DLb).
    // 21 0xa9fae0: the device's scene begins; the pass stack emptied; the log (if open) gets the frame's header.
    virtual void BeginScene() = 0;
    // 22 0xaa08b0: the pass stack emptied, the device state reset (8), the scene ends; the log closes after its
    // last frame.
    virtual void EndScene() = 0;
    // 23 0xa9fb40 (ret 0x1c): a new pass pushed and set up for the viewer's view: the viewport (min, max and depth
    // range as fractions of the target), the view frame and two 4x4 matrices (the projection and one more); returns
    // the pass (the whole eax).
    virtual Pass *DrawViewer(cIViewerRefNodeBase *viewer, const float *viewportMin, const float *viewportMax,
                             const float *depthRange, const nGZCompGeom::FramePlacement *view, const float *projection,
                             const float *matrix) = 0;
    // 24 0xaa0920 (ret 0xc): the current pass drawn and popped: its entries' and nodes' draws, then (with the buffers
    // made) the screen rect added once per material in `materials` (user data base + i), the items sorted and
    // rendered through the strategy.
    virtual void DrawPass(uint32_t count, const int32_t *materials, uint32_t userDataBase) = 0;
    // 25 0xa9f4c0: a node placed by a frame, in a box, with the frustum plane mask (cShapeRefNode's draw).
    virtual void DrawNode(cICompositionTreeNode *node, const AffineTransform3 *placement, const nGZCompGeom::BoundBox *bounds,
                          uint32_t planeMask) = 0;
    // 26 0xa9f3c0: a light node in view (cLightRefNodeImpl::Draw).
    virtual void AddLightNode(cICompositionTreeNode *node, uint32_t planeMask) = 0;
    // 27: an object and a flag appended to the current pass's first entry list.
    virtual void AddPassEntry(void *object, uint8_t flag) = 0;
    // 28 0xa9e700 (ret 0x20): a draw item for each subset of `geometry` (its slot 4 GeometryInfo, slot 5 per subset)
    // whose material (materials[i], -1 for none) has a variant the geometry supports and whose layer state (the current
    // node's, or layerStates[i]) the pass shows; `flags` pick frustum culling (0x80), the bone frames from `bonePoses`
    // (0x10), the blend count (0x20), the light state (1 none, 2 its own lights, else the item at +0xe4's), the depth
    // (0x40 the centre's, else the nearest corner's) and sub key bits (2, 0x100). Items with odd sort keys are sorted
    // and get depths in their order.
    virtual void AddGeometrySubsets(cIGeometryRender *geometry, uint32_t flags, const int32_t *materials,
                                    const uint32_t *const *subsetWords, const uint32_t *word24,
                                    const uint32_t (*layerStates)[2], const nGZCompGeom::RigidTransform3 *bonePoses,
                                    uint32_t userDataBase) = 0;
    // 29 0xa9f460: a draw item for `geometry` with the material manager's `material` in `pass` (built by 0xaeae00),
    // inserted in sorted order.
    virtual void AddGeometry(cIGeometryRender *geometry, int32_t material, Pass *pass, uint32_t userData) = 0;
    // 30 0xa9f140: a bound light with its frame (cLightRefNodeImpl).
    virtual void AddLight(cILight *light, const AffineTransform3 *frame) = 0;

    // 31: the number of passes; each pass is linked to the one before it.
    virtual void SetPassCount(uint32_t count) = 0;
    // 32: the render strategy (null: a new default one).
    virtual void SetRenderStrategy(cIDisplayListRenderStrategy *strategy) = 0;
    // 33: the dynamic buffers' sizes in KB (vertex, index); the buffers are made again.
    virtual void SetBufferSizes(uint32_t vertexKB, uint32_t indexKB) = 0;
    // 34: a log file at `path` for `frames` frames.
    virtual void OpenLog(const char *path, uint32_t frames) = 0;
    // 35: the six statistics words copied out, cleared when `reset`.
    // (eax is left holding `out`, so ours returns it.)
    virtual uint32_t *TakeStats(uint32_t *out, uint8_t reset) = 0;
    virtual void SetMatrix(const float *matrix) = 0;  // 36: 16 floats
    virtual void SetVector140(const float *v) = 0;    // 37: 3 floats
    virtual void SetVector14c(const float *v) = 0;    // 38: 3 floats
    virtual bool IsDrawing() = 0;                     // 39: the byte at +0x32
    virtual bool HasPass() = 0;                       // 40: the pass stack is not empty
    virtual uint32_t GetSlotCount() = 0;              // 41
    virtual cIDisplayListRenderStrategy *GetRenderStrategy() = 0; // 42
    virtual uint32_t GetFrame() = 0;                  // 43
    // 44: in KB (eax is left holding `indexKB`, so ours returns it).
    virtual uint32_t *GetBufferSizes(uint32_t *vertexKB, uint32_t *indexKB) = 0;
    virtual const float *GetMatrix() = 0;             // 45
    virtual const float *GetVector140() = 0;          // 46
    virtual const float *GetVector14c() = 0;          // 47
};

} // namespace nGZSceneGraph
