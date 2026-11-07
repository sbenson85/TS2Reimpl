#pragma once
// nGZSceneGraph::cIViewerRefNode: the viewer reference node as the display list's DrawViewer (slot 23 0xa9fb40) and
// view draw (slot 24 0xaa0920) ask it for a view. cViewerRefNode's vtable 0xe71b10 (147 slots, from the class info);
// the getters' bodies are cViewerRefNodeBaseImpl's (sg/cViewerRefNodeBaseImpl.h). Slots the display list never calls
// keep placeholders with the original's argument words (their rets); their bodies are setters and accessors the
// base class documents.
#include "sg/SGNodeInterfaces.h"

#include <cstdint>

namespace nGZSceneGraph {

struct cViewerTargetSize;
struct cViewerRefEntry;
class cIDisplayListRenderStrategy;

// How the view's target is cleared (slot 104 fills it; the default render strategy hands it to the device's Clear):
// bit 0 of the flags clears the colour, bit 1 depth and stencil.
struct cViewerClearInfo {
    uint32_t mFlags;
    float mColor[4];
    float mDepth;
    uint32_t mStencil;
};
static_assert(sizeof(cViewerClearInfo) == 0x1c, "the default render strategy's frame");

// The viewer base's level (slots 87-122), cViewerRefNodeBaseT's: both viewer classes have it (cViewerRefNode and
// cViewerRefNodeRecursive), and the display list's DrawViewer takes it.
class cIViewerRefNodeBase : public cIRenderableNode {
public:
    // 87: how the viewer's passes match a node's layer tag (cIRenderableNode::SetLayerTag) to the viewer's own: 0 draws
    // nodes whose tag equals it, 1 those whose tag differs, 2 ignores tags (Pass::Shows; a new pass's copy is 2).
    virtual uint8_t GetLayerTagMatch() = 0;
    virtual uint32_t Slot88() = 0;                // 88: the detail limit (the renderable Impl's +0x68)
    virtual uint32_t Slot89() = 0;                // 89: the Impl's +0x6c
    virtual void Slot90(uint32_t) = 0;
    virtual void Slot91(const cViewerClearInfo *info) = 0; // 91: SetClearInfo (slot 104 reads it back)
    virtual void Slot92(cIGZUnknown *texture, cIGZUnknown *ref94, float scale) = 0; // 92: cViewerRefNodeBaseImpl::SetExplicit
    virtual void Slot93(uint32_t count, cIGZUnknown *const *textures, cIGZUnknown *ref94, float scale) = 0; // 93: SetTextures
    virtual void Slot94(uint32_t) = 0;
    virtual void Slot95(const char *name, uint32_t group) = 0; // 95: SetTextureByName (94: SetTexture by id)
    virtual void Slot96(cIGZUnknown *color, cIGZUnknown *depth) = 0; // 96: SetSurfaces
    virtual void Slot97(uint32_t) = 0;
    virtual void SetLayerTagMatch(uint32_t) = 0; // 98: the byte GetLayerTagMatch reads (stored as a byte)
    virtual void Slot99(cIGZUnknown *ref) = 0;    // 99: SetRefA8
    virtual void Slot100(cIGZUnknown *ref) = 0;   // 100: SetRefA4
    virtual void Slot101(cIBoundedNode *bound, const float *plane, uint32_t flags) = 0; // 101: SetBoundAndPlane
    virtual void Slot102(cIBoundedNode *bound) = 0; // 102: SetBound
    virtual void Slot103(cIGZUnknown *ref) = 0;   // 103: the +0x70 reference (slot 122), counted
    // 104: how to clear the view's target (the default render strategy presets the colour to 1, 0, 0, 0).
    virtual void GetClearInfo(cViewerClearInfo *out) = 0;
    // 105 0xae0e00: how many textures the view renders into (cViewerRefNodeBaseImpl::CallTextureSlot7).
    virtual uint32_t GetTextureCount() = 0;
    virtual cIGZUnknown *GetTexture(int32_t index) = 0;                     // 106 0xae1200
    virtual cIGZUnknown *GetTextureRef94() = 0;                             // 107 0xae10f0
    virtual uint32_t Slot108() = 0;               // 108: the texture-manager handle (+0x98)
    virtual cIGZUnknown *GetHandleTexture() = 0;                            // 109 0xae1340
    virtual cViewerTargetSize *GetTargetSize(cViewerTargetSize *out) = 0;   // 110 0xae1000
    virtual cIGZUnknown *GetColorSurface() = 0;                             // 111 0xae0bd0
    virtual cIGZUnknown *GetDepthSurface() = 0;                             // 112 0xae0b90
    virtual bool Slot113() = 0;                   // 113: IsDeviceViewMode
    virtual bool Slot114() = 0;                   // 114: UsesDeviceTargets
    // 115: the view's render type (the display list's log prints it).
    virtual uint32_t GetRenderType() = 0;
    virtual cIGZUnknown *Slot116() = 0;           // 116: the +0xa8 reference
    // 117: the view's own render strategy, or null (the display list's is used).
    virtual cIDisplayListRenderStrategy *GetRenderStrategy() = 0;
    virtual cIBoundedNode *Slot118() = 0;         // 118: the held bound node
    virtual float *Slot119() = 0;                 // 119: the plane (four floats)
    virtual uint32_t Slot120() = 0;               // 120: the base's flags (+0xc0)
    // 121: the holder of the view's frame cache (asked for cFrameCache::kIid). Not counted for the caller.
    virtual cIGZUnknown *GetFrameCacheHolder() = 0;
    // 122: the texture mode word.
    virtual uint32_t GetTextureMode() = 0;
};

class cIViewerRefNode : public cIViewerRefNodeBase {
public:
    // ---- cViewerRefNode's own level (slots 123-146; lane TZ, sg/cViewerRefNode.h) ----
    // A view's entries: each one a viewport, a projection and the view's placement relative to the node
    // (cViewerRefEntry, sg/cViewerRefNodeImpl.h). `index` is never range-checked.
    virtual void Render() = 0;                                                       // 123: every entry drawn
    virtual void ResizeEntries(uint32_t count) = 0;                                  // 124
    virtual void SetEntryPlacement(uint32_t index, const cRigidTransform *placement) = 0; // 125
    virtual void SetMaterialCount(uint32_t index, uint32_t count) = 0;               // 126
    virtual void SetMaterial(uint32_t index, uint32_t slot, const char *name) = 0;   // 127
    virtual void SetViewport(uint32_t index, float x, float y, float w, float h) = 0; // 128
    virtual void SetSubrect(uint32_t index, float x, float y, float w, float h) = 0; // 129
    virtual void SetFixedAspect(uint32_t index, uint8_t fixed) = 0;                  // 130
    virtual void SetPixelAspect(uint32_t index, float aspect) = 0;                   // 131
    virtual void SetPerspective(uint32_t index, float fov) = 0;                      // 132
    virtual void SetDepthRange(uint32_t index, float min, float max) = 0;            // 133
    virtual void SetNearFar(uint32_t index, float nearClip, float farClip) = 0;      // 134
    virtual void SetOffsets(uint32_t index, float x, float y) = 0;                   // 135
    virtual void SetOrtho(uint32_t index, float left, float right, float bottom, float top, float nearClip,
                          float farClip) = 0;                                         // 136
    virtual void SetProjection(uint32_t index, const float *matrix) = 0;             // 137: sixteen floats
    virtual uint32_t GetEntryCount() = 0;                                            // 138
    virtual cViewerRefEntry *GetEntry(uint32_t index) = 0;                           // 139: brought up to date first
    virtual cRigidTransform *GetView(cRigidTransform *out, uint32_t index) = 0;      // 140
    virtual float *GetViewport(float *out, uint32_t index) = 0;                      // 141: a 4x4 matrix
    virtual float *WorldToScreen(float *out, const float *point, uint32_t index) = 0; // 142
    virtual float *WorldToClip(float *out, const float *point, uint32_t index) = 0;  // 143
    virtual float *ScreenToWorld(float *out, const float *point, uint32_t index) = 0; // 144
    virtual float *ClipToWorld(float *out, const float *point, uint32_t index) = 0;  // 145
};

// cViewerRefNodeRecursive's (vtable 0xe72eac, 126 slots; lane VF, sg/cViewerRefNodeRecursive.h): the viewer base's
// level, then the materials the node's view is drawn with.
class cIViewerRefNodeRecursive : public cIViewerRefNodeBase {
public:
    virtual void SetMaterialCount(uint32_t count) = 0;               // 123
    virtual void SetMaterial(uint32_t index, const char *name) = 0;  // 124
};

} // namespace nGZSceneGraph
