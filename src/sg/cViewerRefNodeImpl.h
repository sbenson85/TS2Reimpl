#pragma once
// nGZSceneGraph::cViewerRefNodeImpl: the viewer-reference node's state (the Impl of the game's cViewerRefNode, 0x104
// bytes at node+0xc): the viewer base's (cViewerRefNodeBaseImpl) plus the view's entries (+0xe8) and the target's
// aspect and size as last seen (+0xf4..+0x100). Constructor and destructor: lane TA; the rest: lane TZ (scenegraph N3r;
// the node is sg/cViewerRefNode.h).
//
// An entry is one view of the node's subtree: a viewport and a sub-rectangle of the target (fractions), a projection
// (perspective by field of view, orthographic by box, or a given matrix), the placement of the eye relative to the node
// and a list of materials the display list draws the view with (their display-list slots at +0xc0). Setters mark the
// entry dirty; GetEntry brings the projection up to date first (UpdateProjection).
//
// Persistence (Read 0xa66eb0, Write 0xa68060: lane VA): records of version 12 on hold each entry's fields; older ones
// refer to a legacy viewer (sg/cViewer_LEGACY.h) whose view is copied.
#include "sg/cShape.h" // ShapeMaterialHandle
#include "sg/cViewerRefNodeBaseImpl.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nGZSceneGraph {

class cIDisplayList;

// A material handle as the entries hold them: ShapeMaterialHandle with the game's assignment (0xae88a0), so the
// vector's own assignment and growth do what the game's instantiations (0xa639e0, 0x299470) do.
struct cViewerMaterialHandle : ShapeMaterialHandle {
    cViewerMaterialHandle() = default; // -1, no reference (0x18bf80)
    cViewerMaterialHandle(const cViewerMaterialHandle &other) = default; // counted (0xae8800)
    cViewerMaterialHandle &operator=(const cViewerMaterialHandle &other)
    {
        Assign(other);
        return *this;
    }
};
static_assert(sizeof(cViewerMaterialHandle) == 4, "cViewerMaterialHandle size");

// One view (0xcc bytes; constructor 0xa64270, copy 0xa63e50's element, assignment 0xa64970).
struct cViewerRefEntry {
    enum : uint32_t { kPerspective = 0, kOrtho = 1, kExplicit = 2 };

    uint32_t mType = kPerspective;          // +0x00
    uint8_t mFixedAspect = 0;               // +0x04: the target's aspect is ignored
    float mViewport[4] = {0, 0, 1, 1};      // +0x08: x, y, w, h (fractions of the target)
    float mSubrect[4] = {0, 0, 1, 1};       // +0x18: x, y, w, h (the part of the projection drawn)
    float mNear = 1.0f;                     // +0x28
    float mFar = 100.0f;                    // +0x2c
    float mPixelAspect = 1.0f;              // +0x30
    float mDepthMin = 0.0f;                 // +0x34
    float mDepthMax = 1.0f;                 // +0x38
    float mFov = 1.04719758f;               // +0x3c (bits 0x3f860a92: 60 degrees)
    float mOffset[2] = {0, 0};              // +0x40: the projection centre's shift
    float mOrtho[4] = {0, 0, 0, 0};         // +0x48: left, right, bottom, top
    cRigidTransform mPlacement = {{0, 0, 0, 0, 0, 0, 1}}; // +0x58: the eye relative to the node
    float mProjection[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // +0x74
    std::vector<cViewerMaterialHandle> mMaterials; // +0xb4
    int32_t mDetailSlot = -1;               // +0xc0: the display list's slots for the materials (-1: none)
    float mAspect = 0.0f;                   // +0xc4: the node's aspect the projection was made for
    uint8_t mDirty = 0;                     // +0xc8

    cViewerRefEntry() = default;
    // The game's copy and assignment: the first 0xb4 bytes as they are, the materials (counted), the tail. The move is
    // the vector's growth (0xa63c00): the same, but the materials' array taken over (the source's left empty), nothing
    // counted.
    cViewerRefEntry(const cViewerRefEntry &other);
    cViewerRefEntry(cViewerRefEntry &&other) noexcept;
    cViewerRefEntry &operator=(const cViewerRefEntry &other);
    ~cViewerRefEntry() = default;
};
static_assert(sizeof(cViewerRefEntry) == 0xcc, "cViewerRefEntry size");
static_assert(offsetof(cViewerRefEntry, mPlacement) == 0x58 && offsetof(cViewerRefEntry, mProjection) == 0x74 &&
                  offsetof(cViewerRefEntry, mMaterials) == 0xb4 && offsetof(cViewerRefEntry, mDirty) == 0xc8,
              "cViewerRefEntry layout");

// A view's materials as records hold them (the viewer entries', cViewer_LEGACY's and cViewerRefNodeRecursive's): the
// count, then for each a flag and, when set, the name, found or loaded in the stream's group (the reader's slot 28; group 0
// the name as it is).
void ReadViewMaterials(std::vector<cViewerMaterialHandle> &materials, ifaces::cIGZPersistReader *reader);
inline void ReadViewMaterials(cViewerRefEntry &entry, ifaces::cIGZPersistReader *reader)
{
    ReadViewMaterials(entry.mMaterials, reader);
}
// The same written: the count, then for each a flag (the material has a name) and the name (what follows the "!" of a
// "*...!name" reference).
void WriteViewMaterials(const std::vector<cViewerMaterialHandle> &materials, ifaces::cIGZPersistWriter *writer);

class cViewerRefNodeImpl : public cViewerRefNodeBaseImpl {
public:
    static constexpr uint32_t kIid = 0x5ca76db6;       // cIViewerRefNode
    static constexpr uint32_t kTypeWord = 0xdca76dbb;  // the type record's word 4
    static constexpr uint32_t kVersion = 0xe;
    static constexpr uint32_t kSettingsService = 0x0ac1887e; // the configuration service (framework slot 5)
    static constexpr uint32_t kSettingsIid = 0x4bb0d269;     // its property set (string keys)
    // The attributes (Set / GetAttribute, the event records, slot 51's names): the entry `extra`'s.
    enum : uint32_t {
        kFovAttribute = 0x4d40c00f,     // "fov": one float
        kNearFarAttribute = 0x4d40c014, // "nearFarClip": two floats
    };

    cViewerRefNodeImpl() = default;
    cViewerRefNodeImpl(const cViewerRefNodeImpl &) = delete;
    cViewerRefNodeImpl &operator=(const cViewerRefNodeImpl &) = delete;
    ~cViewerRefNodeImpl() = default; // the entries, then the viewer base's

    // 0xa644c0 (thiscall, returns this): the viewer base's, then no entries, the words zero, the scale 1.
    cViewerRefNodeImpl *Construct();
    void Destruct(); // 0xa64840 (thiscall)

    // ---- the entries (the node's slots 124-139 are these) ----
    // 0xa67950: the display-list slots of the entries past `count` freed (the display list's FreeSlots), then the
    // vector resized (new entries default).
    void ResizeEntries(uint32_t count);
    void SetEntryPlacement(uint32_t index, const cRigidTransform *placement); // 0xa67b60
    // 0xa678f0: the entry's display-list slots reallocated for `count` materials, then its vector resized.
    void SetMaterialCount(uint32_t index, uint32_t count);
    // 0xa67a70: material `slot` (the vector grown to it first, its display-list slots reallocated) set to the named
    // one (found or loaded), or none for a null name.
    void SetMaterial(uint32_t index, uint32_t slot, const char *name);
    void SetViewport(uint32_t index, float x, float y, float w, float h);
    void SetSubrect(uint32_t index, float x, float y, float w, float h);
    void SetFixedAspect(uint32_t index, uint8_t fixed);
    void SetPixelAspect(uint32_t index, float aspect);
    void SetPerspective(uint32_t index, float fov);                       // 0xa67840: the type perspective
    void SetDepthRange(uint32_t index, float min, float max);             // (not dirty)
    void SetNearFar(uint32_t index, float nearClip, float farClip);       // 0xa678c0
    void SetOffsets(uint32_t index, float x, float y);
    void SetOrtho(uint32_t index, float left, float right, float bottom, float top, float nearClip, float farClip); // 0xa679e0
    void SetProjection(uint32_t index, const float *matrix);              // 0xa67770: explicit (not dirty)
    uint32_t GetEntryCount() const { return static_cast<uint32_t>(mEntries.size()); }

    // 0xa69840: the target's size and scale noted; the aspect (width / height, the height scaled when the
    // "usePixelAspectRatio" setting is on) and, when it changed, every entry marked dirty.
    void RefreshAspect();
    // 0xa699b0: the entry, the aspect refreshed when the target changed, the projection remade when it is dirty.
    cViewerRefEntry *GetEntry(uint32_t index);
    // 0xa656f0: the entry's projection from its type, viewport, sub-rectangle and the node's aspect (explicit: kept).
    void UpdateProjection(cViewerRefEntry *entry);

    // ---- the view's transforms (thiscall, `outer` the node; each returns out) ----
    // 0xa66760: the eye in the world (the entry's placement then the node's world transform).
    cRigidTransform *GetView(cRigidTransform *out, cICompositionTreeNode *outer, uint32_t index);
    // 0xa66800: the viewport matrix (clip space to target pixels and depth) of the entry, as it is (not refreshed).
    float *GetViewport(float *out, uint32_t index);
    float *WorldToScreen(float *out, cICompositionTreeNode *outer, const float *point, uint32_t index); // 0xa66bc0
    float *WorldToClip(float *out, cICompositionTreeNode *outer, const float *point, uint32_t index);   // 0xa66a30
    float *ScreenToWorld(float *out, cICompositionTreeNode *outer, const float *point, uint32_t index); // 0xa67e40
    float *ClipToWorld(float *out, cICompositionTreeNode *outer, const float *point, uint32_t index);   // 0xa67cb0

    // ---- the draw ----
    // 0xa674d0 (slot 123): every entry drawn through the detail object's display list, between its BeginScene and
    // EndScene.
    void Render(cIBoundedNode *outer);
    // 0xa661a0 (slot 83): once per frame of the view's display list, every entry drawn through it.
    void Cull(cIBoundedNode *outer, cViewerView *view, uint32_t mask);

    // ---- the node's other slots ----
    void SetAttribute(cICompositionTreeNode *outer, uint32_t id, uint32_t extra, const cAttributeValue *value); // 0xa68000
    void GetAttribute(cAttributeValue *out, uint32_t id, uint32_t extra);                                    // 0xa66e50
    bool IsKindOf(uint32_t iid);                                        // 0xa67be0
    cPersistTypeInfo *GetTypeInfo(cPersistTypeInfo *out);               // 0xa67c00 {"cViewerRefNode", 0xdca76dbb, 0xe}
    // 0xa66eb0: the viewer base's record, then the entries (the vector resized to the count read, each entry read in
    // place: its fields, or from version 12 back the legacy view's copy).
    void Read(cIBoundedNode *outer, ifaces::cIGZPersistReader *reader);
    // 0xa68060: the viewer base's record, then each entry as GetEntry gives it (refreshed), its materials by name.
    void Write(cIBoundedNode *outer, ifaces::cIGZPersistWriter *writer);
    // 0xa68660: the viewer base's, then the entries copied (vector assignment).
    void CloneInto(cICompositionTreeNode *source, cICompositionTreeNode *clone, cViewerRefNodeImpl *cloneImpl,
                   uint32_t mode, cINodeCloneContext *context);
    // 0xa66440 (slot 51, hooked): an attribute's name range (sg/cViewerRefAttributeNameIterator.h), else the empty one.
    void *MakeIterator51(void *out, uint32_t id);

    std::vector<cViewerRefEntry> mEntries; // +0xe8
    float mAspect = 0.0f;                  // +0xf4: the target's aspect as last seen
    uint32_t mTargetWidth = 0;             // +0xf8
    uint32_t mTargetHeight = 0;            // +0xfc
    float mTargetScale = 1.0f;             // +0x100

private:
    // The draw loop Cull and Render share (inlined twice in the game): each entry with at least a pixel of the target
    // handed to `*list` (read for each call; DrawViewer, then its pass drawn with the entry's materials).
    void DrawEntries(cIBoundedNode *outer, cIDisplayList *const *list, uint32_t parentWord);
    // Read's two kinds of entry record (inlined in the game's).
    static void ReadEntry(cViewerRefEntry &entry, uint32_t version, ifaces::cIGZPersistReader *reader);
    static void ReadLegacyEntry(cViewerRefEntry &entry, uint32_t version, ifaces::cIGZPersistReader *reader);
};
static_assert(sizeof(cViewerRefNodeImpl) == 0x104, "cViewerRefNodeImpl size");
static_assert(offsetof(cViewerRefNodeImpl, mEntries) == 0xe8 && offsetof(cViewerRefNodeImpl, mTargetScale) == 0x100,
              "cViewerRefNodeImpl layout");

} // namespace nGZSceneGraph
