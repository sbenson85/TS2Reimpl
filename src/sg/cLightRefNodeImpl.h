#pragma once
// nGZSceneGraph::cLightRefNodeImpl (lanes TA, TE; scenegraph F1b part 2, N2r part 2): the light-reference node's state
// (the Impl at +0xc of cLightRefNode, sg/cLightRefNode.h): a renderable node's state plus the light it refers to (+0x70,
// counted; SetLight swaps it, announcing the subordinate cILight::kIid around the change and registering the node as
// the light's referrer), the node's own copy of that light (+0x74: a clone, made when an attribute is set and remade
// when the light or the detail changes), a word set to -1 (+0x78), a flag (+0x7c) and the bounds inflation read from
// the configuration service's "lightBoundsInflate" (+0x80). A plain class like its bases; `outer` is the node that
// holds it. The constructor and destructor are hooked at their RVAs (src/hook/tables/SGNodeCopies.cpp, lane TA); the
// other bodies are reached only through the node's slots (lane TE: in effect through the node's callers).
#include "sg/SGLightInterfaces.h"
#include "sg/cRenderableNodeImpl.h"

#include <cstddef>
#include <cstdint>

namespace nGZSceneGraph {

struct cViewerView;

class cLightRefNodeImpl : public cRenderableNodeImpl {
public:
    static constexpr uint32_t kSettingsService = 0x0ac1887e; // the configuration service (framework slot 5)
    static constexpr uint32_t kSettingsIid = 0x4bb0d269;     // its property set (string keys)
    static constexpr uint32_t kIid = 0x053d200a;             // cILightRefNode
    static constexpr uint32_t kVersion = 10;                 // what Write records (before 10: a count, an inverted flag)
    static constexpr uint32_t kTypeWord = 0x253d2018;        // the type record's word 4 (the class id)

    // The events the Impl answers (0xa623a0).
    enum : uint32_t {
        kSubordinateEvent = 0x1d05597b,   // a subordinate added / removed: a light (kinds 2 / 3) remakes the copy
        kDetailChangedEvent = 0x69f75eba, // the detail limit is read again and the copy remade
        kTransformEvent = 0xdd041f30,     // the transform changed
        kLightChangedEvent = 0xe9dd42cf,  // sent to the node's listeners after the first two
    };
    // The light attributes (Set / GetAttribute and the event records): each a standard light setter / getter pair.
    enum : uint32_t {
        kConeFalloffAngle = 0x49de5331, // 48 / 57
        kRange = 0x0a5622e9,            // 45 / 55
        kColor = 0x29de3049,            // 41 / 51 (three floats)
        kConeAngle = 0x29de5328,        // 47 / 56
        kFalloff = 0x49e37821,          // 44 / 54
        kFactor = 0x69de427a,           // 43 / 53
        kIntensity = 0xe9d24d51,        // 42 / 52
    };
    static bool IsLightAttribute(uint32_t id)
    {
        return id == kConeFalloffAngle || id == kRange || id == kColor || id == kConeAngle || id == kFalloff ||
               id == kFactor || id == kIntensity;
    }

    cLightRefNodeImpl();
    ~cLightRefNodeImpl();
    cLightRefNodeImpl(const cLightRefNodeImpl &) = delete;
    cLightRefNodeImpl &operator=(const cLightRefNodeImpl &) = delete;

    cLightRefNodeImpl *Construct(); // 0xa60c20 (thiscall, returns this)
    void Destruct();                // 0xa60ec0 (thiscall)

    // ---- lane TE ----
    // The light the bounds and the draw use: the copy when there is one, else the light.
    cILight *BoundLight() const { return mHeld74 ? mHeld74 : mLight; }
    // Not a function of the original (inlined): the copy let go of (nulled, then released).
    void DropCopy();
    // Not a function of the original (inlined): `light` cloned into the copy (mode 0, as the light interface, no context).
    void CopyFrom(cILight *light);

    // 0xa623a0: the composition Impl's first; then a light subordinate added or removed (kinds 2 / 3) propagates the
    // transform and remakes the copy (when there are both), the detail event rereads the detail limit and remakes the
    // copy; both then tell the listeners (kLightChangedEvent, a cNullEvent). The transform event propagates only.
    void OnEvent(cICompositionTreeNode *outer, uint32_t type, uint32_t arg1, void *data, uint32_t sender);
    // 0xa62d80: nothing for the same light; else the old one let go of (removing notification, the node no longer its
    // referrer, released, removed notification) and the new one taken (adding notification, counted, the node its
    // referrer, added notification).
    void SetLight(cICompositionTreeNode *outer, cILight *light);
    // 0xa631f0: the renderable part, then the clone's flag (slot 91) and light (slot 93); with a copy, the clone gets a
    // copy of this one's copy.
    void CloneInto(cICompositionTreeNode *source, cICompositionTreeNode *clone, cLightRefNodeImpl *cloneImpl,
                   uint32_t mode, cINodeCloneContext *context);
    bool IsKindOf(uint32_t iid); // 0xa62e20: kIid, else the renderable Impl's
    // 0xa62ef0: a light attribute (with a light that has a standard face) is set on the copy (made first when there is
    // none) and the transform propagated; other ids go to the transform Impl's.
    void SetAttribute(cICompositionTreeNode *outer, uint32_t id, uint32_t extra, const cAttributeValue *value);
    // 0xa62a80: a light attribute read from the copy (else the light), when it has a standard face.
    void GetAttribute(cAttributeValue *out, uint32_t id, uint32_t extra);
    // 0xa62c00 / 0xa62e40 / 0xa63120: the type record, the renderable part, the light (a reference), the flag, and the
    // copy (inline, after a "none" byte).
    void Read(cICompositionTreeNode *outer, ifaces::cIGZPersistReader *reader);
    cPersistTypeInfo *GetTypeInfo(cPersistTypeInfo *out); // {"cLightRefNode", 0x253d2018, 10}
    void Write(cICompositionTreeNode *outer, ifaces::cIGZPersistWriter *writer);

    // Bounds and hits: the bound light's shape (cILight slot 37) placed by the cached world frame. A light without a
    // shape is everywhere: its boxes are everything, and the hit tests say yes (the first hit at 0).
    cBoundBox *GetBoundBoxIn(cBoundBox *out, cICompositionTreeNode *outer, const cTransform34 *frame); // 0xa628f0
    cBoundBox *GetBoundBox(cBoundBox *out, cICompositionTreeNode *outer);                             // 0xa62730
    bool IntersectsBound(cICompositionTreeNode *outer, nGZCompGeom::cIBound *bound, const AffineTransform3 *frame); // 0xa624e0
    bool IntersectsLinear(cICompositionTreeNode *outer, nGZCompGeom::cILinear3 *linear);                // 0xa625a0
    bool FirstHitLinear(cICompositionTreeNode *outer, float *t, nGZCompGeom::cILinear3 *linear);        // 0xa62650

    // 0xa61d70 (slot 82): the view's display list told of the node (its slot 26).
    void Draw(cICompositionTreeNode *outer, cViewerView *view, uint32_t planeMask);
    // 0xa61bb0 (slot 83): with a light, its shape's box (inflated) culled against the view's frustum unless `mask` says
    // every plane is passed; the bound light handed to the display list (its slot 30) with the frame in the view.
    void Cull(cICompositionTreeNode *outer, cViewerView *view, uint8_t mask);

    // 0xa61d90 (slot 51, hooked): a light attribute's name range (sg/cLightRefAttributeNameIterator.h), else the empty one.
    void *MakeIterator51(void *out, uint32_t id);

    cILight *mLight = nullptr;  // +0x70: the light referred to (counted)
    cILight *mHeld74 = nullptr; // +0x74: the node's copy of it (counted; released before the light)
    int32_t mWord78 = -1;       // +0x78
    uint8_t mFlag7c = 0;        // +0x7c (stored as given)
    float mBoundsInflate;       // +0x80: "lightBoundsInflate" (a uint32 setting, 0 without one), as a float
};
static_assert(sizeof(cLightRefNodeImpl) == 0x84, "cLightRefNodeImpl size");
static_assert(offsetof(cLightRefNodeImpl, mLight) == 0x70 && offsetof(cLightRefNodeImpl, mBoundsInflate) == 0x80,
              "cLightRefNodeImpl layout");

} // namespace nGZSceneGraph
