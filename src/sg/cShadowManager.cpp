// nTSSG::cShadowManager (lanes IR, 261108u and 261109a, scenegraph LG6c and LG6d; src/sg/cShadowManager.h).
//
// Still the original's here (OriginalFunction): the service getters 0x20dfb5 (textures, cell 0x10cc2d0), 0x20e0b2
// (placement), 0x20de97 (wall partitions), 0x20defb (the camera's owner) and 0xb38f60 (the common system); the drawing's
// callees 0x3ad4a0 (a material bound to a node's named subset), 0x3ca190 and 0x3ca030 (the brush drawn into a texture,
// into the renderer's target) and 0x3a40d0 (the static shadow's light, constructed). 0xae1ed0 (a node's animated face),
// 0xae7490 (a named node under a node) and 0xaeb2c0 (a node's transform relative to another) are ours (sg/SGUtilities.h).
#include "sg/cShadowManager.h"

#include "compgeom/CompGeom.h"      // BoundBox, RigidTransform3
#include "compgeom/ConvexShapes.h"  // ConvexBox
#include "compgeom/Frames.h"        // FrameMath
#include "compgeom/Matrix4.h"       // Matrix4Multiply
#include "compgeom/Rigid.h"         // Dot87
#include "compgeom/SeparatingAxes.h" // FramePlacement
#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/GraphicServices.h" // GetCurrentDeviceCached
#include "gz/cDeviceDX.h"
#include "gz/cDeviceStateDX.h"
#include "gz/cGZAny.h"
#include "gz/cIGZCOM.h"
#include "gz/cIGZMessage2Standard.h"
#include "gz/cIGZPropertySet.h" // GZServiceString
#include "ifaces/cIGZFrameWork.h"
#include "misc/Quaternion.h"
#include "misc/SmallVirtuals.h" // cBranchNodesMaskBitAndIIDFilter
#include "misc/UtilitiesC.h" // KeyMapSlot5Walker
#include "rz/MiscHelpersB.h" // KeyMapWalker
#include "rz/Singleton_20ddfb.h" // GetServiceObject, ScopedRef, GetCachedSingleton
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject
#include "sg/SGLightInterfaces.h" // cILight
#include "sg/SGNodeInterfaces.h"
#include "sg/SGUtilities.h"
#include "sg/cDataListExtension.h"
#include "sg/cDataListItem.h"
#include "sg/cIViewerRefNode.h"
#include "sg/cLightRefNode.h"
#include "sg/cSceneGraphSystem.h" // cISceneNodeIterator, cSceneNodeRange
#include "sg/cShape.h"
#include "sg/cShapeRefNode.h"
#include "sg/cViewerRefNodeImpl.h" // cViewerRefEntry
#include "simantics/ServiceRefVector.h" // ServiceBlockVector
#include "simantics/SimGeometry.h"      // NotchQuaternion
#include "ts/cTSCommonSystem.h"
#include "ts/cTSObjectFootprint.h" // FacingPointerToNotch
#include "world/WorldTileMath.h"
#include "world/WorldTileRects.h"

#include <cfloat>
#include <cmath>
#include <cstring>
#include <new>

namespace nTSSG {

namespace {

constexpr uint32_t kIidMessageTarget2 = 0x452294aa;
constexpr uint32_t kIidMessage2 = 0x65297976;
constexpr uint32_t kSettingsId = 0x0ac1887e, kSettingsIid = 0x4bb0d269;

// The messages the manager listens to (the constructor's set, in its order).
constexpr uint32_t kWallChanged = 0x0b4cf313;    // a wall segment's object (interface 0x2ab8190c)
constexpr uint32_t kFenceChanged = 0x2b4cf319;   // a segment's object (interface 0x4ab8192e)
constexpr uint32_t kLevelChanged = 0x2b4d18fb;   // data 1: the level whose lot rectangles are told
constexpr uint32_t kSegmentChanged = 0xcb46c21d; // data 1: a partition id, invalidated on the next update
constexpr uint32_t kRequeueA = 0x522ced95;       // every shadow queued
constexpr uint32_t kRequeueB = 0xb23b7d6f;
constexpr uint32_t kMessageIds[] = {kWallChanged, kFenceChanged, kLevelChanged, kSegmentChanged, kRequeueA, kRequeueB};

// The format texts (the game's): "%s_%d" (a shadow's key) and "%s_StaticShadow" (its texture's name).
constexpr uint32_t kKeyFormat = 0xdb6910;
constexpr uint32_t kTextureFormat = 0xdb69fc;

// The image's fill values: what the pass-1 image (the original's 0x10d9448, zeroes) and a shadow's image (0xffdfcc)
// are cleared to.
const float kPass1Fill[4] = {0.0f, 0.0f, 0.0f, 0.0f};
const float kShadowFill[4] = {0.0f, 1.0f, 1.0f, 1.0f};

// The director's classes.
constexpr uint32_t kImageClass = 0x2b96cc2b, kImageIid = 0x0b96cc22;
constexpr uint32_t kClassBC = 0xdca76dbb, kIidBC = 0x5ca76db6;
constexpr uint32_t kClassC0 = 0x25232b11, kIidC0 = 0x0516c708;
constexpr uint32_t kClassC4 = 0x65246462, kIidC4 = 0xe51c10f5;

constexpr int32_t kInitialShadows = 64;

uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

nGZSceneGraph::cITextureManager *TextureService()
{
    return OriginalFunction<nGZSceneGraph::cITextureManager *(__cdecl *)(), 0x20dfb5>()();
}
void *Placement() { return OriginalFunction<void *(__cdecl *)(), 0x20e0b2>()(); }
void *Partitions() { return OriginalFunction<void *(__cdecl *)(), 0x20de97>()(); }
void *CameraOwner() { return OriginalFunction<void *(__cdecl *)(), 0x20defb>()(); }
cITSCommonSystem *CommonSystem() { return OriginalFunction<cITSCommonSystem *(__cdecl *)(), 0xb38f60>()(); }

cIGZCOM *Director() { return static_cast<cIGZCOM *>(GetDirectorFrameworkObject()); }

// An object's cIGZUnknown virtual base: the vbtable at `vbptr`, entry 1, is the base's offset from it.
cIGZUnknown *VirtualBase(uint8_t *object, size_t vbptr)
{
    return reinterpret_cast<cIGZUnknown *>(object + vbptr + reinterpret_cast<const int32_t *const *>(object + vbptr)[0][1]);
}

// A counted reference held through a virtual base (the vbtable at +4).
void ReleaseThroughVirtualBase(uint8_t *object)
{
    VirtualBase(object, 4)->Release(); // receiver: the object's cIGZUnknown virtual base
}

// Release a pointer member: zeroed first, then released (the original's order).
template <class T> void Drop(ShadowRef<T> &ref)
{
    if (T *const old = ref.p) {
        ref.p = nullptr;
        old->Release(); // receiver: the held object (cIGZUnknown, slot 2)
    }
}

// 0x142080's order: the new one counted, stored, then the old one released; nothing for the same pointer.
template <class T> void Assign(ShadowRef<T> &ref, T *object)
{
    T *const old = ref.p;
    if (object == old) return;
    if (object) object->AddRef(); // receiver: the new object (cIGZUnknown, slot 1)
    ref.p = object;
    if (old) old->Release(); // receiver: the old object (cIGZUnknown, slot 2)
}

// What 0x20e0b2's slots 65, 67 and 71 take: the first 12 bytes of a tile quadrant (x, y, level).
struct Tile12 {
    int32_t words[3];
};

// ---- the drawing's constants and callees ----

// What the node is turned and lifted by under mRefC4: the identity first, then 0.2 up on snow.
const nGZSceneGraph::cRigidTransform kIdentityTransform = {{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}};
const float kSnowLift[3] = {0.0f, 0.0f, 0.2f};

// The camera's owner's slot 23 finds a node by name as this interface.
constexpr uint32_t kNamedNodeIid = 0xe519c92c;

// The static shadow's light: a holder (the director's class 0x253d2018, a light reference node) and the light itself
// (0xe4 bytes, constructed by 0x3a40d0: its vbtable at +0x1c, its cILight face at +0x2c).
constexpr uint32_t kLightHolderClass = 0x253d2018;
constexpr size_t kLightSize = 0xe4, kLightVbptr = 0x1c, kLightFace = 0x2c;

// The material PrepareNode binds, the subsets it binds it to, and the data list naming more of them.
constexpr const char *kShadowOff = "GUOBOff";
constexpr const char *kShadowSubsets[] = {"groundshadow", "northwallshadow", "southwallshadow", "eastwallshadow",
                                          "westwallshadow"};
constexpr const char *kNoShadowList = "tsNoShadow";
constexpr uint32_t kDataListIid = 0x6a836d51; // cIDataListExtension
constexpr uint32_t kItemSint32 = 2;           // a data-list item's type: a signed int

// The root bones FindRootBone tries: an animated face's bones, then nodes by name.
constexpr uint32_t kRootBoneA = 0x6c918d88, kRootBoneB = 0xe6e97a71;
constexpr const char *kRootNames[] = {"root_rot", "root_trans", "chairdining_rot", "chairdining_trans"};

void BindSubsetMaterial(cIGZUnknown *node, const char *material, const char *subset)
{
    OriginalFunction<void(__cdecl *)(cIGZUnknown *, const char *, const char *), 0x3ad4a0>()(node, material, subset);
}
void DrawBrushToTexture(cBlurBrush *brush, cIGZUnknown *texture)
{
    OriginalFunction<void(__cdecl *)(cBlurBrush *, cIGZUnknown *), 0x3ca190>()(brush, texture);
}
void DrawBrushToTarget(cBlurBrush *brush, cIGZUnknown *renderer, int32_t key0, int32_t key1)
{
    OriginalFunction<void(__cdecl *)(cBlurBrush *, cIGZUnknown *, int32_t, int32_t), 0x3ca030>()(brush, renderer, key0, key1);
}
// The scene-graph helpers (ours, sg/SGUtilities.h), the node pointers as the game passes them.
nGZSceneGraph::cICompositionTreeNode *TreeNode(void *node)
{
    return reinterpret_cast<nGZSceneGraph::cICompositionTreeNode *>(node);
}
void *AnimatedFace(cIGZUnknown *node) { return nGZSceneGraph::AnimatedFaceOf(TreeNode(node)); }
void *NamedNode(const char *name, cIGZUnknown *node) { return nGZSceneGraph::FindNodeByName(name, TreeNode(node), 0xf); }
const float *RelativeTransform(float *out, void *bone, cIGZUnknown *node)
{
    return nGZSceneGraph::RelativeTransform(out, TreeNode(bone), TreeNode(node));
}
uint8_t *ConstructLight(void *memory)
{
    return OriginalFunction<uint8_t *(__thiscall *)(void *, uint32_t), 0x3a40d0>()(memory, 1u);
}

nGZSceneGraph::cIViewerRefNode *Viewer(cIGZUnknown *object) { return reinterpret_cast<nGZSceneGraph::cIViewerRefNode *>(object); }
nGZSceneGraph::cITransformNode *TransformNode(cIGZUnknown *object) { return reinterpret_cast<nGZSceneGraph::cITransformNode *>(object); }
nGZSceneGraph::cICompositionTreeNode *TreeNode(cIGZUnknown *object) { return reinterpret_cast<nGZSceneGraph::cICompositionTreeNode *>(object); }

// The settings service (into `settings`, which the caller releases) and its "useSnowForShadows" (false when unset).
bool UseSnowForShadows(ScopedRef &settings)
{
    gz::AnyBoolean snow(0);
    if (auto *const framework = static_cast<ifaces::cIGZFrameWork *>(GetServiceObject()))
        framework->GetSystemService(kSettingsId, kSettingsIid, &settings.mObject);
    // ORIGINAL-BUG: the settings service is used without a null test.
    auto *const properties = static_cast<cIGZPropertySetString *>(settings.mObject);
    {
        GZServiceString key("useSnowForShadows");
        properties->GetPropertyValue(&key, &snow); // receiver: the settings (cIGZPropertySetString)
    }
    return snow.mValue != 0;
}

// The light's view-projection matrix after a pass: the viewer's projection (entry 0) times its inverted view.
void LightViewProjection(nGZSceneGraph::cIViewerRefNode *viewer, float *out)
{
    float projection[16];
    std::memcpy(projection, viewer->GetEntry(0)->mProjection, sizeof(projection)); // receiver: cIViewerRefNode
    nGZSceneGraph::cRigidTransform view;
    const nGZSceneGraph::cRigidTransform *const placed = viewer->GetView(&view, 0); // receiver: cIViewerRefNode
    nGZCompGeom::FrameMath frame;
    frame.mUnknown0 = 1;
    std::memcpy(frame.mOffset, placed->Translation(), sizeof(frame.mOffset));
    frame.SetRotation(placed->Rotation());
    float inverse[16];
    reinterpret_cast<nGZCompGeom::FramePlacement *>(static_cast<AffineTransform3 *>(&frame))->Invert();
    frame.ToMatrix4(inverse);
    float product[16];
    std::memcpy(out, nGZCompGeom::Matrix4Multiply(product, inverse, projection), sizeof(product));
}

// The three counted iterators a node's range is made of (the cursor, the begin, the end), released end first.
struct NodeRange {
    nGZSceneGraph::cISceneNodeIterator *mCursor = nullptr;
    nGZSceneGraph::cISceneNodeIterator *mBegin = nullptr;
    nGZSceneGraph::cISceneNodeIterator *mEnd = nullptr;
    NodeRange() = default;
    NodeRange(const NodeRange &) = delete;
    NodeRange &operator=(const NodeRange &) = delete;
    ~NodeRange()
    {
        if (mEnd) mEnd->Release();       // receiver: cISceneNodeIterator
        if (mBegin) mBegin->Release();   // receiver: cISceneNodeIterator
        if (mCursor) mCursor->Release(); // receiver: cISceneNodeIterator
    }
    bool AtEnd() { return mCursor->IsEqual(mEnd); } // receiver: cISceneNodeIterator
};
static_assert(sizeof(NodeRange) == sizeof(nGZSceneGraph::cSceneNodeRange), "NodeRange: a cSceneNodeRange");

} // namespace

// ---- life ------------------------------------------------------------------------------------------------------------

cShadowManager::cShadowManager()
{
    for (const uint32_t id : kMessageIds) mMessageIds.insert(id);
    SetOption(4, 1);
    SetOption(5, 0);
    SetOption(6, 1);
}

cShadowManager::~cShadowManager() = default;

cShadowManager *__cdecl cShadowManager::Create() { return new (std::nothrow) cShadowManager; }

__declspec(noinline) cShadowManager *__fastcall cShadowManager::Construct(void *memory, void *)
{
    return ::new (memory) cShadowManager;
}

bool cShadowManager::QueryInterface(uint32_t iid, void **out)
{
    if (!out) return false;
    if (iid == 1 || iid == kIid) {
        *out = static_cast<cIShadowManager *>(this);
    } else if (iid == kIidMessageTarget2) {
        *out = static_cast<cIGZMessageTarget2 *>(this);
    } else {
        return false;
    }
    // Through the object's own table, as the original (the class is final: a plain call would bind directly).
    using AddRefSlot = uint32_t(__thiscall *)(void *);
    (*reinterpret_cast<AddRefSlot *const *>(this))[1](this); // receiver: this (slot 1, AddRef)
    return true;
}

uint32_t cShadowManager::AddRef() { return ++mRefCount; }

uint32_t cShadowManager::Release()
{
    if (--mRefCount) return mRefCount;
    // Through the object's own table (slot 17, flags 1), as the original: the class is final, so a delete expression
    // would bind the destructor directly.
    using DeletingDestructor = void *(__thiscall *)(void *, uint32_t);
    (*reinterpret_cast<DeletingDestructor *const *>(this))[17](this, 1u); // receiver: this (slot 17, the deleting destructor)
    return 0;
}

// ---- the director's slots -------------------------------------------------------------------------------------------

bool cShadowManager::Init()
{
    if (mStarted) return false;
    mStarted = 1;
    mOptionBits = 0;
    ++mChangeCount;
    SetOption(4, 1);
    SetOption(5, 0);
    SetOption(6, 1);
    SetOption(0, 1);
    SetOption(1, 1);
    SetOption(2, 1);
    SetOption(3, 1);
    cIGZMessageTarget2 *const target = this;
    reinterpret_cast<KeyMapSlot5Walker *>(&mMessageIds)->CallSlot5ForEachKey(Word(target), nullptr);
    // receiver: the common system (cITSCommonSystem)
    Assign(mRenderer, CommonSystem()->GetRenderTargetTextureManager());
    CreateResources();
    return true;
}

bool cShadowManager::Shutdown()
{
    if (!mStarted) return false;
    mStarted = 0;
    cIGZMessageTarget2 *const target = this;
    reinterpret_cast<KeyMapWalker *>(&mMessageIds)->CallSlot6ForEachKey(Word(target), nullptr);
    ReleaseResources();
    Drop(mRenderer);
    return true;
}

void cShadowManager::Update()
{
    if (!mChangedSegments.empty()) {
        for (const int32_t id : mChangedSegments) SegmentChanged(id);
        mChangedSegments.clear();
    }
    TrackCamera();
    DrainQueue();

    ScopedRef settings{nullptr};
    if (auto *const framework = static_cast<ifaces::cIGZFrameWork *>(GetServiceObject()))
        framework->GetSystemService(kSettingsId, kSettingsIid, &settings.mObject);
    // ORIGINAL-BUG: the settings service is used without a null test.
    auto *const properties = static_cast<cIGZPropertySetString *>(settings.mObject);
    gz::AnyFloat32 f(0.0f);
    gz::AnyUint32 u(0);
    const auto readFloat = [&](const char *name, float &field) {
        GZServiceString key(name);
        const bool ok = properties->GetPropertyValue(&key, &f); // receiver: the settings (cIGZPropertySetString)
        if (ok && f.mValue != field) {
            ++mChangeCount;
            field = f.mValue;
        }
    };
    const auto changeWord = [&](bool ok, uint32_t &field) {
        if (ok && u.mValue != field) {
            ++mChangeCount;
            field = u.mValue;
        }
    };
    const auto readWord = [&](const char *name, uint32_t &field) {
        GZServiceString key(name);
        const bool ok = properties->GetPropertyValue(&key, &u); // receiver: the settings (cIGZPropertySetString)
        changeWord(ok, field);
    };
    readFloat("shadowSize", mShadowSize);
    readFloat("shadowCameraDistance", mCameraDistance);
    {
        bool ok;
        {
            GZServiceString key("shadowBlurRadius");
            ok = properties->GetPropertyValue(&key, &u); // receiver: the settings (cIGZPropertySetString)
            if (!ok) {
                GZServiceString indoor("indoorShadowBlurRadius");
                ok = properties->GetPropertyValue(&indoor, &u); // receiver: the settings (cIGZPropertySetString)
            }
        }
        changeWord(ok, reinterpret_cast<uint32_t &>(mBlurRadius));
    }
    readWord("outdoorShadowBlurRadius", reinterpret_cast<uint32_t &>(mOutdoorBlurRadius));
    readWord("shadowResolution", mResolution);
    readWord("baseShadowResolution", mBaseResolution);
    readFloat("shadowH0", mH0);
    readFloat("shadowW0", mW0);
    readFloat("shadowH1", mH1);
    readFloat("shadowW1", mW1);
    settings.ReleaseObject();
}

void cShadowManager::SetOption(uint32_t index, uint8_t on)
{
    if (index >= kOptionCount) return;
    uint32_t &word = (&mOptionBits)[index >> 5];
    const uint32_t bit = 1u << (index & 31);
    if (uint8_t((word & bit) != 0) == on) return;
    if (on) word |= bit;
    else word &= ~bit;
    ++mChangeCount;
}

void cShadowManager::ResetResources()
{
    ReleaseResources();
    CreateResources();
}

int32_t cShadowManager::AcquireShadow(cIGZUnknown *node, int32_t owner)
{
    if (!mEnabled) return -1;
    const char *const name = reinterpret_cast<const char *>(RawSlot<124>(node)); // receiver: the scene node (its name)
    if (!name) return -1;

    ShadowName key;
    key.Sprintf(GameAddress<const char>(kKeyFormat), name, owner);
    const auto found = mIndexByName.find(key);
    if (found != mIndexByName.end()) {
        const int32_t index = found->second;
        if (index >= 0 && uint32_t(index) < mShadows.size() && mShadows[uint32_t(index)].mUsers > 0) {
            ++mShadows[uint32_t(index)].mUsers;
            return index;
        }
    }

    cShadowEntry entry;
    entry.mUsers = 1;
    entry.mMatrixStamp = 0;
    entry.mName.mString.assign(name);
    entry.mOwner = owner;
    nGZSceneGraph::cITextureManager *const textures = TextureService();
    nGZLog::LogString textureName;
    textureName.Sprintf(GameAddress<const char>(kTextureFormat), key.mString.c_str());
    if (textures->Has(textureName.mString.c_str())) return -1; // receiver: the texture service (cITextureManager)

    uint32_t resolution = mResolution;
    float bounds[6] = {FLT_MAX, FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX};
    ShadowBounds(bounds, 0xf, node, node);
    // The comparisons are written so that a NaN goes the original's way: it passes every test (the box is kept) and
    // leaves the resolution as it is; the larger extent takes the later operand when the two are unordered.
    const float dy = bounds[4] - bounds[1];
    const float dz = bounds[5] - bounds[2];
    const float longer = dz > dy ? dz : dy;
    const float dx = bounds[3] - bounds[0];
    const float size = longer > dx ? longer : dx;
    if (bounds[0] > bounds[3] || bounds[1] > bounds[4] || bounds[2] > bounds[5] || 0.15 > double(size)) return -1;
    if (0.5f > size) resolution >>= 2;
    else if (1.5f > size) resolution >>= 1;
    else if (size > 3.0f) resolution *= 2;
    if (mBaseResolution < resolution) resolution = mBaseResolution;

    if (!mRenderTargets) {
        cIGZUnknown *image = nullptr;
        Director()->GetClassObject(kImageClass, kImageIid, reinterpret_cast<void **>(&image)); // receiver: the director
        // receiver: the image (the director's class 0x2b96cc2b): made with its size, format and fill
        RawSlot<9>(image, resolution, resolution, 0x18u, 8u, 0u, 1u, kShadowFill, 0u);
        if (!RawSlot<6, uint8_t>(image)) { // receiver: the image (ready)
            image->Release();               // receiver: the image (cIGZUnknown)
            return -1;
        }
        // receiver: the texture service (cITextureManager)
        entry.mTexture = nGZSceneGraph::cTextureHandle(textures->Add(image, textureName.mString.c_str(), 5));
        image->Release(); // receiver: the image (cIGZUnknown)
    } else {
        uint32_t keys[2];
        // receiver: the render-target manager (+0x50): a target of the size, its two keys
        const int32_t *const made = reinterpret_cast<const int32_t *>(RawSlot<5>(mRenderer.p, keys, resolution, resolution, 0u));
        entry.mScaleKeys[0] = made[0];
        entry.mScaleKeys[1] = made[1];
        if (entry.mScaleKeys[0] < 0) return -1;
        // receiver: the render-target manager (+0x50): the target's texture id
        entry.mTexture = nGZSceneGraph::cTextureHandle(RawSlot<7>(mRenderer.p, entry.mScaleKeys[0], entry.mScaleKeys[1]));
    }

    int32_t index;
    if (mFreeIndices.empty()) {
        index = int32_t(mShadows.size());
        mShadows.push_back(entry);
    } else {
        index = mFreeIndices.back();
        mFreeIndices.pop_back();
        mShadows[uint32_t(index)] = entry;
    }
    mIndexByName.insert(std::pair<const ShadowName, int32_t>(key, index));
    Queue(index);
    return index;
}

void cShadowManager::ReleaseShadow(int32_t index)
{
    if (index < 0 || uint32_t(index) >= mShadows.size()) return;
    cShadowEntry &entry = mShadows[uint32_t(index)];
    if (entry.mUsers <= 0 || --entry.mUsers != 0) return;
    if (entry.mQueued) Unqueue(index);
    entry.mTexture.Reset();
    ShadowName key;
    key.Sprintf(GameAddress<const char>(kKeyFormat), entry.mName.mString.c_str(), entry.mOwner);
    mIndexByName.erase(key);
    if (entry.mScaleKeys[0] >= 0) {
        // receiver: the render-target manager (+0x50): the target let go
        RawSlot<6>(mRenderer.p, entry.mScaleKeys[0], entry.mScaleKeys[1]);
        entry.mScaleKeys[0] = -1;
        entry.mScaleKeys[1] = -1;
    }
    mFreeIndices.push_back(index);
}

uint8_t cShadowManager::MakeStaticShadow(const float *position, cIGZUnknown *node, uint32_t *outHandle)
{
    using namespace nGZSceneGraph;
    cICompositionTreeNode *const tree = TreeNode(node);
    cITransformNode *const holder4 = TransformNode(mRefC4.p);
    cIViewerRefNode *const viewer = Viewer(mRefBC.p);

    // The node is taken from its parent (kept counted, to be given it back) and put under mRefC4, unturned.
    cICompositionTreeNode *previous = nullptr;
    if (tree->GetParent()) { // receiver: cICompositionTreeNode
        previous = tree->GetParent(); // receiver: cICompositionTreeNode
        if (previous) previous->AddRef(); // receiver: the parent (cIGZUnknown)
        // ORIGINAL-BUG: a parent gone between the two asks is used without a null test.
        previous->RemoveChild(tree); // receiver: the parent (cICompositionTreeNode)
    }
    holder4->AddChild(tree);                  // receiver: mRefC4 (cITransformNode)
    holder4->SetTransform(&kIdentityTransform); // receiver: mRefC4 (cITransformNode)
    ScopedRef settings{nullptr};
    if (UseSnowForShadows(settings)) holder4->SetTranslation(kSnowLift); // receiver: mRefC4 (cITransformNode)
    float direction[4];
    viewer->SetRotation(ShadowDirection(direction, position)); // receiver: cIViewerRefNode

    uint8_t made = 0;
    bool restore = true;
    if (FitShadowCamera(mRefBC.p, mRefC4.p)) {
        cILightRefNode *lightHolder = nullptr;
        // receiver: the director (cIGZCOM)
        Director()->GetClassObject(kLightHolderClass, cILightRefNode::kIid, reinterpret_cast<void **>(&lightHolder));
        // The game's operator new (0x824632) is the library's, and it throws rather than return null, so the original's
        // null test of the memory is dead.
        uint8_t *const light = ConstructLight(::operator new(kLightSize));
        if (light) VirtualBase(light, kLightVbptr)->AddRef(); // receiver: the light's cIGZUnknown virtual base
        // receiver: the light holder (cILightRefNode)
        lightHolder->SetLight(light ? reinterpret_cast<cILight *>(light + kLightFace) : nullptr);

        cIGZUnknown *image = nullptr;
        Director()->GetClassObject(kImageClass, kImageIid, reinterpret_cast<void **>(&image)); // receiver: the director
        // receiver: the image (the director's class 0x2b96cc2b): made with its size, format and fill
        RawSlot<9>(image, mResolution, mResolution, 0x18u, 8u, 0u, 1u, kShadowFill, 1u);
        if (!RawSlot<6, uint8_t>(image)) { // receiver: the image (ready)
            if (image) image->Release(); // receiver: the image (cIGZUnknown)
            if (light) VirtualBase(light, kLightVbptr)->Release(); // receiver: the light's cIGZUnknown virtual base
            if (lightHolder) lightHolder->Release(); // receiver: the light holder (cIGZUnknown)
            // ORIGINAL-BUG: this way out skips the restore: the node stays under mRefC4 and its old parent loses it.
            restore = false;
        } else {
            {
                nGZLog::LogString name;
                name.Sprintf("shadowLight_%x", light);
                // receiver: the texture service (cITextureManager)
                const cTextureHandle texture(TextureService()->Add(image, name.mString.c_str(), 5));
                // ORIGINAL-BUG: with no ready texture RenderStatic draws nothing, and the light is handed the matrix
                // unset (the stack's leftovers).
                float matrix[16];
                RenderStatic(texture.Get(), matrix);
                RawSlot<14>(light, texture.mId); // receiver: the light (its texture)
                RawSlot<13>(light, matrix, 0u);  // receiver: the light (its matrix)
                *outHandle = Word(lightHolder);
                lightHolder->AddRef(); // receiver: the light holder (cIGZUnknown)
                made = 1;
            }
            if (image) image->Release(); // receiver: the image (cIGZUnknown)
            if (light) VirtualBase(light, kLightVbptr)->Release(); // receiver: the light's cIGZUnknown virtual base
            if (lightHolder) lightHolder->Release(); // receiver: the light holder (cIGZUnknown)
        }
    }
    if (restore) {
        tree->GetParent()->RemoveChild(tree); // receiver: the node's parent (cICompositionTreeNode)
        if (previous) previous->AddChild(tree); // receiver: the old parent (cICompositionTreeNode)
    }
    settings.ReleaseObject();
    if (previous) previous->Release(); // receiver: the old parent (cIGZUnknown)
    return made;
}

void cShadowManager::GetOptionBits(uint32_t *out) { *out = mOptionBits; }

int32_t *cShadowManager::ChangeCount() { return &mChangeCount; }

cShadowEntry *cShadowManager::Entry(int32_t index)
{
    if (index < 0 || uint32_t(index) >= mShadows.size()) return nullptr;
    cShadowEntry *const entry = &mShadows[uint32_t(index)];
    return entry->mUsers > 0 ? entry : nullptr;
}

uint32_t cShadowManager::ShadowTexture(int32_t index)
{
    const cShadowEntry *const entry = Entry(index);
    if (!entry) return 0;
    uint32_t result[2];
    // receiver: the texture service (cITextureManager, slot 24 GetInfo: the name word of the id's info)
    return *RawSlot<24, uint32_t *>(TextureService(), result, entry->mTexture.mId);
}

bool cShadowManager::GetShadowMatrix(int32_t index, float *out, uint32_t *stamp)
{
    const cShadowEntry *const entry = Entry(index);
    if (!entry) return false;
    if (stamp) {
        if (*stamp == entry->mMatrixStamp) return false;
        *stamp = entry->mMatrixStamp;
    }
    std::memcpy(out, entry->mMatrix, sizeof(entry->mMatrix)); // bit copies (random bits may be NaNs)
    return true;
}

void cShadowManager::GetShadowScale(int32_t index, float *offset, float *scale)
{
    if (const cShadowEntry *const entry = Entry(index)) {
        // receiver: the render-target manager (+0x50): the target's offset and scale
        RawSlot<8>(mRenderer.p, entry->mScaleKeys[0], entry->mScaleKeys[1], offset, scale);
        return;
    }
    offset[0] = 0.0f, offset[1] = 0.0f;
    scale[0] = 1.0f, scale[1] = 1.0f;
}

cShadowBox cShadowManager::GetShadowBox(int32_t index)
{
    if (const cShadowEntry *const entry = Entry(index)) {
        cShadowBox box;
        std::memcpy(box.mValues, entry->mBox, sizeof(box.mValues));
        return box;
    }
    return cShadowBox{{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}};
}

// ---- DoMessage --------------------------------------------------------------------------------------------------------

bool cShadowManager::DoMessage(void *data)
{
    if (!data) return false;
    auto *const message = static_cast<cIGZMessage2Standard *>(data);
    cIGZMessage2Standard *standard = nullptr;
    message->QueryInterface(kIidMessage2, reinterpret_cast<void **>(&standard)); // receiver: the message
    const uint32_t type = message->GetType();                                     // receiver: the message
    bool handled = false;
    if (type == kRequeueA || type == kRequeueB) {
        QueueAll();
    } else if (type == kWallChanged || type == kFenceChanged) {
        const bool wall = type == kWallChanged;
        uint32_t *segment = nullptr;
        // ORIGINAL-BUG: the message's standard face, its object and the object's segment face are used unchecked.
        cIGZUnknown *const object = standard->GetIGZUnknown(); // receiver: cIGZMessage2Standard
        // receiver: the message's object: its segment face (0x2ab8190c for walls, 0x4ab8192e otherwise)
        object->QueryInterface(wall ? 0x2ab8190cu : 0x4ab8192eu, reinterpret_cast<void **>(&segment));
        uint32_t words[6];
        std::memcpy(words, segment + (wall ? 5 : 3), sizeof(words));
        Invalidate(words);
        if (segment) reinterpret_cast<cIGZUnknown *>(segment)->Release(); // receiver: the segment face (cIGZUnknown)
        handled = true;
    } else if (type == kLevelChanged) {
        const uint32_t level = static_cast<uint16_t>(standard->GetData1()); // receiver: cIGZMessage2Standard
        void *const placement = Placement();
        void *const lot = reinterpret_cast<void *>(RawSlot<39>(placement)); // receiver: the placement service (its lot)
        ServiceBlockVector rects{};
        RawSlot<76>(lot, level, &rects, 0u); // receiver: the lot (the level's rectangles, 16 bytes each)
        const int32_t count = int32_t(Word(rects.mLast) - Word(rects.mFirst)) >> 4;
        for (int32_t r = 0; r < count; ++r) {
            Tile12 tile;
            std::memcpy(&tile, static_cast<const uint8_t *>(rects.mFirst) + r * 16, sizeof(tile));
            const int32_t objects = int32_t(RawSlot<65>(placement, &tile)); // receiver: the placement service
            for (int32_t i = 0; i < objects; ++i) {
                const uint32_t object = RawSlot<67>(placement, i, &tile, 1u); // receiver: the placement service
                uint8_t *ref = nullptr;
                Lookup(object, reinterpret_cast<void **>(&ref));
                if (ref) {
                    if (void *const shadow = reinterpret_cast<void *>(RawSlot<129>(ref))) // receiver: the object's sim face
                        RawSlot<13>(shadow);                                               // receiver: its shadow user
                }
                if (ref) ReleaseThroughVirtualBase(ref);
            }
        }
        rects.FreeBlock();
        handled = true;
    } else if (type == kSegmentChanged) {
        mChangedSegments.insert(int32_t(standard->GetData1())); // receiver: cIGZMessage2Standard
    }
    if (standard) standard->Release(); // receiver: cIGZMessage2Standard
    return handled;
}

// ---- helpers ----------------------------------------------------------------------------------------------------------

void cShadowManager::CreateResources()
{
    cIGZUnknown *image = nullptr;
    Director()->GetClassObject(kImageClass, kImageIid, reinterpret_cast<void **>(&image)); // receiver: the director
    // receiver: the image (the director's class 0x2b96cc2b): made with its size, format and fill
    RawSlot<9>(image, mBaseResolution, mBaseResolution, 0x18u, 8u, 0u, 1u, kPass1Fill, 1u);
    // receiver: the texture service (cITextureManager)
    mPass1 = nGZSceneGraph::cTextureHandle(TextureService()->Add(image, "staticShadowPass1", 5));
    Assign(mBrush, new (std::nothrow) cBlurBrush);
    mBrush.p->SetRadius(mOutdoorBlurRadius); // receiver: cBlurBrush
    mBrush.p->SetTexture(mPass1.mId);        // receiver: cBlurBrush
    mBrush.p->SetScale(1.0f, 1.0f);          // receiver: cBlurBrush

    cIGZCOM *director = Director();
    Drop(mRefBC);
    director->GetClassObject(kClassBC, kIidBC, reinterpret_cast<void **>(&mRefBC.p)); // receiver: the director
    RawSlot<124>(mRefBC.p, 1u); // receiver: the director's class 0xdca76dbb
    const uint32_t setup[7] = {3, 0, 0x3f800000u, 0x3f800000u, 0x3f800000u, 0x3f800000u, 0};
    RawSlot<91>(mRefBC.p, setup);                                                        // receiver: class 0xdca76dbb
    RawSlot<136>(mRefBC.p, 0.0f, -2.0f, 2.0f, -2.0f, 2.0f, -6.0f, 1.0f);                 // receiver: class 0xdca76dbb
    RawSlot<66>(mRefBC.p, 1u);                                                           // receiver: class 0xdca76dbb
    RawSlot<97>(mRefBC.p, 1u);                                                           // receiver: class 0xdca76dbb

    director = Director();
    Drop(mRefC0);
    director->GetClassObject(kClassC0, kIidC0, reinterpret_cast<void **>(&mRefC0.p)); // receiver: the director
    RawSlot<45>(mRefC0.p, mRefBC.p); // receiver: the director's class 0x25232b11
    director = Director();
    Drop(mRefC4);
    director->GetClassObject(kClassC4, kIidC4, reinterpret_cast<void **>(&mRefC4.p)); // receiver: the director
    RawSlot<45>(mRefC0.p, mRefC4.p); // receiver: the director's class 0x25232b11

    if (mShadows.empty()) {
        mShadows.resize(kInitialShadows);
        for (int32_t i = 0; i < kInitialShadows; ++i) mFreeIndices.push_back(i);
    }
    mCamera[0] = 0.0f;
    mCamera[1] = 0.0f;
    mCamera[2] = 0.0f;
    if (image) image->Release(); // receiver: the image (cIGZUnknown)
}

void cShadowManager::ReleaseResources()
{
    Drop(mRefBC);
    Drop(mRefC0);
    mPass1.Reset();
    Drop(mBrush);
}

void cShadowManager::Queue(int32_t index)
{
    if (index < 0 || uint32_t(index) >= mShadows.size()) return;
    cShadowEntry &entry = mShadows[uint32_t(index)];
    if (entry.mUsers <= 0 || entry.mQueued) return;
    entry.mQueued = 1;
    mQueue.push_back(uint32_t(index));
}

void cShadowManager::QueueAll()
{
    for (uint32_t i = 0; i < mShadows.size(); ++i)
        if (mShadows[i].mUsers > 0) mQueue.push_back(i);
}

void cShadowManager::Unqueue(int32_t index)
{
    if (index < 0 || uint32_t(index) >= mShadows.size()) return;
    cShadowEntry &entry = mShadows[uint32_t(index)];
    if (!entry.mQueued) return;
    entry.mQueued = 0;
    for (auto it = mQueue.begin(); it != mQueue.end(); ++it) {
        if (*it == uint32_t(index)) {
            mQueue.erase(it);
            return;
        }
    }
}

void cShadowManager::DrainQueue()
{
    for (int32_t drawn = 0; !mQueue.empty() && drawn < 4; ++drawn) {
        const uint32_t index = mQueue.front();
        mQueue.pop_front();
        DrawShadow(int32_t(index));
    }
}

void cShadowManager::TrackCamera()
{
    void *const camera = reinterpret_cast<void *>(RawSlot<49>(CameraOwner())); // receiver: 0x20defb's object (its camera)
    float buffer[4];
    const float *const position = reinterpret_cast<const float *>(RawSlot<30>(camera, buffer)); // receiver: the camera
    const float negated[3] = {-position[0], -position[1], -position[2]};
    const float dx = negated[0] - mCamera[0];
    const float dy = negated[1] - mCamera[1];
    const float dz = negated[2] - mCamera[2];
    const float distance = (dy * dy + dx * dx) + dz * dz;
    if (!(double(distance) > 1e-6)) return;
    mCamera[0] = negated[0];
    mCamera[1] = negated[1];
    mCamera[2] = negated[2];
    const float *const direction = ShadowDirection(buffer, negated);
    std::memcpy(mLightDirection, direction, sizeof(mLightDirection));
    QueueAll();
}

void cShadowManager::SegmentChanged(int32_t id)
{
    void *const partitions = Partitions();
    const int32_t count = int32_t(RawSlot<55>(partitions)); // receiver: 0x20de97's service (the segment count)
    for (int32_t i = 0; i < count; ++i) {
        uint32_t segment[6];
        RawSlot<56>(partitions, segment, i); // receiver: 0x20de97's service (segment i)
        if (int32_t(segment[2]) == id || int32_t(segment[5]) == id) Invalidate(segment);
    }
}

void cShadowManager::Invalidate(const uint32_t *segment)
{
    void *const placement = Placement();
    const float *const rect = reinterpret_cast<const float *>(segment);
    Tile12 tiles[2];
    worldmath::TileQuadrant quadrant;
    std::memcpy(&tiles[0], worldrect::SegmentToQuadrants(&quadrant, rect), sizeof(Tile12));
    std::memcpy(&tiles[1], worldrect::ReversedSegmentToQuadrants(&quadrant, rect), sizeof(Tile12));
    for (Tile12 &tile : tiles) {
        if (!RawSlot<71, uint8_t>(placement, &tile)) continue; // receiver: the placement service (the tile is valid)
        const int32_t objects = int32_t(RawSlot<65>(placement, &tile)); // receiver: the placement service
        for (int32_t i = 0; i < objects; ++i) {
            const uint32_t object = RawSlot<67>(placement, i, &tile, 1u); // receiver: the placement service
            uint8_t *ref = nullptr;
            Lookup(object, reinterpret_cast<void **>(&ref));
            if (ref) {
                if (void *const shadow = reinterpret_cast<void *>(RawSlot<129>(ref))) // receiver: the object's sim face
                    RawSlot<15>(shadow);                                               // receiver: its shadow user
            }
            if (ref) ReleaseThroughVirtualBase(ref);
        }
    }
}

uint8_t cShadowManager::Lookup(uint32_t object, void **out)
{
    void *const app = reinterpret_cast<void *>(RawSlot<26>(GetCachedSingleton())); // receiver: the cached singleton
    if (!app) return 0;
    void *const found = reinterpret_cast<void *>(RawSlot<29>(app, object)); // receiver: the singleton's object table
    if (!found) return 0;
    return RawSlot<12, uint8_t>(found, 0xfa1c39f7u, 0xfa1c39edu, out); // receiver: the found object (its face)
}

// ---- drawing (lane 261109a) ---------------------------------------------------------------------------------------------

void __cdecl ShadowBounds(float *box, int32_t mask, cIGZUnknown *reference, cIGZUnknown *node)
{
    using namespace nGZSceneGraph;
    auto *const shapes = reinterpret_cast<cIShapeRefNode *>(node);
    // receiver: the node (cIShapeRefNode: TestFlag 48, IsEverywhere 70, AnyShapeSlot52 97)
    if (shapes->TestFlag(4) && !shapes->IsEverywhere() && shapes->AnyShapeSlot52()) {
        const cBoundBox own = shapes->GetBoundBox(); // receiver: cIShapeRefNode
        nGZCompGeom::ConvexBox placed;
        std::memcpy(&placed, &own, sizeof(placed));
        if (0.0f > placed.min.z) placed.min.z = 0.0f; // nothing below the ground
        // receiver: the reference node (cICompositionTreeNode)
        const cRigidTransform world = reinterpret_cast<cICompositionTreeNode *>(reference)->GetWorldTransform();
        nGZCompGeom::RigidTransform3 toReference;
        std::memcpy(&toReference, &world, sizeof(toReference));
        toReference.Invert();
        placed.TransformRigid(&toReference);
        reinterpret_cast<nGZCompGeom::BoundBox *>(box)->Grow(placed);
    }
    const cChildRange children = shapes->GetChildren(); // receiver: cICompositionTreeNode
    for (cICompositionTreeNode *child = children.mCurrent; child != children.mEnd;
         child = child->GetNextSibling()) { // receiver: the child (cICompositionTreeNode)
        if (mask < 0 || !child->TestFlag(uint32_t(mask))) // receiver: the child (cICompositionTreeNode)
            ShadowBounds(box, mask, reference, reinterpret_cast<cIGZUnknown *>(child));
    }
}

float *__cdecl ShadowDirection(float *out, const float *position)
{
    static constexpr float kYawAxis[3] = {0.0f, 0.0f, 1.0f};
    static constexpr float kPitchAxis[3] = {1.0f, 0.0f, 0.0f};
    const float length =
        float(std::sqrt(double(position[0] * position[0] + position[1] * position[1] + position[2] * position[2])));
    const float inverse = 1.0f / length;
    const float yaw = float(std::atan2(double(position[0] * inverse), double(-(position[1] * inverse))));
    const float pitch = float(std::asin(double(-(position[2] * inverse))));
    const math::Quaternion turn = math::Quaternion::FromAxisHalfAngle(kYawAxis, double(yaw * 0.5f));
    math::Quaternion tilt = math::Quaternion::FromAxisHalfAngle(kPitchAxis, double(pitch * 0.5f));
    tilt.PreMultiplyNormalised(&turn);
    std::memcpy(out, &tilt, sizeof(tilt));
    return out;
}

uint8_t __cdecl FitShadowCamera(cIGZUnknown *viewer, cIGZUnknown *node)
{
    float box[6] = {FLT_MAX, FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX};
    ShadowBounds(box, -1, viewer, node);
    if (box[0] > box[3] || box[1] > box[4] || box[2] > box[5]) return 0;
    const float extent[3] = {box[0] - box[3], box[1] - box[4], box[2] - box[5]};
    if (0.0001 > nGZCompGeom::Dot87(extent, extent)) return 0;
    constexpr float kMargin = 0.5f;
    nGZSceneGraph::cIViewerRefNode *const camera = Viewer(viewer);
    // x across, z up, y the depth: the box seen along y with half a unit to spare on each side.
    // receiver: cIViewerRefNode
    camera->SetOrtho(0, box[0] - kMargin, box[3] + kMargin, box[2] - kMargin, box[5] + kMargin, box[1], box[4]);
    camera->SetPixelAspect(0, (box[4] - box[1]) / (box[3] - box[0])); // receiver: cIViewerRefNode
    return 1;
}

void *__cdecl FindRootBone(cIGZUnknown *node)
{
    void *bone;
    if (void *const face = AnimatedFace(node)) {
        bone = reinterpret_cast<void *>(RawSlot<52>(face, kRootBoneA)); // receiver: the animated face (a bone by id)
        if (bone) return bone;
        bone = reinterpret_cast<void *>(RawSlot<52>(face, kRootBoneB)); // receiver: the animated face (a bone by id)
    } else {
        bone = NamedNode(kRootNames[0], node);
        if (bone) return bone;
        bone = NamedNode(kRootNames[1], node);
    }
    if (!bone) bone = NamedNode(kRootNames[2], node);
    if (!bone) bone = NamedNode(kRootNames[3], node);
    return bone;
}

void __cdecl cShadowManager::PrepareNode(cIGZUnknown *node)
{
    using namespace nGZSceneGraph;
    for (const char *const subset : kShadowSubsets) BindSubsetMaterial(node, kShadowOff, subset);

    // Every shape node under it (mask bit 0xf, interface cIShapeRefNode).
    nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter filter{GameAddress<const void>(nRZSceneGraph::cBranchNodesMaskBitAndIIDFilter::kVTable),
                                                         0xf, cIShapeRefNode::kIid};
    {
        NodeRange range;
        TreeNode(node)->MakeIterator44(&range, 0, Word(&filter)); // receiver: cICompositionTreeNode
        ServiceBlockVector lods{};
        while (!range.AtEnd()) {
            auto *const holder = reinterpret_cast<cIShapeRefNode *>(range.mCursor->Get()); // receiver: cISceneNodeIterator
            const cShapeRange shapes = holder->GetShapes(); // receiver: cIShapeRefNode
            for (cIShape **shape = shapes.mFirst; shape != shapes.mEnd; ++shape) {
                (*shape)->GetPartLods(Word(&lods)); // receiver: cIShape
                const auto lodCount = [&lods] {
                    return uint32_t(int32_t(static_cast<uint8_t *>(lods.mLast) - static_cast<uint8_t *>(lods.mFirst)) >> 2);
                };
                for (uint32_t lod = 0; lod < lodCount(); ++lod) {
                    // receiver: cIShape
                    auto *const part = reinterpret_cast<cIObjectGraphNode *>((*shape)->PartObjectAtLod2(lod));
                    if (!part) continue;
                    // receiver: the part (cIObjectGraphNode), then each extension (cIExtension)
                    for (cIExtension *extension = part->GetFirstExtensionConst(); extension; extension = extension->GetNext()) {
                        cIDataListExtension *list = nullptr;
                        // receiver: the extension (cIExtension), then the list (cIDataListExtension)
                        if (extension->QueryInterface(kDataListIid, reinterpret_cast<void **>(&list)) &&
                            !_stricmp(list->GetListName(), kNoShadowList)) {
                            cIDataListItem *item = nullptr;
                            for (uint32_t i = 0; i < list->GetCount(); ++i) { // receiver: cIDataListExtension
                                if (cIDataListItem *const old = item) {
                                    item = nullptr;
                                    old->Release(); // receiver: the item (cIGZUnknown)
                                }
                                list->GetItem(i, &item); // receiver: cIDataListExtension
                                // ORIGINAL-BUG: an item the list does not hand out is used without a null test.
                                // receiver: the item (cIDataListItem)
                                if (item->GetType() != kItemSint32 || !item->GetSint32()) continue;
                                if (int32_t((*shape)->GetReferrerCount()) > 1) { // receiver: cIShape
                                    // Shared with others: this holder gets a copy of its own to change.
                                    cIShape *copy = nullptr;
                                    // receiver: cIShape
                                    (*shape)->Clone(1, cIShape::kIid, reinterpret_cast<void **>(&copy), nullptr);
                                    holder->RemoveShape(*shape); // receiver: cIShapeRefNode
                                    holder->AddShape(copy);      // receiver: cIShapeRefNode
                                    // ORIGINAL-BUG: a failed copy is used without a null test.
                                    copy->BindMaterial(kShadowOff, item->GetName()); // receiver: the copy (cIShape)
                                    if (copy) copy->Release(); // receiver: the copy (cIGZUnknown)
                                } else {
                                    cIShape *const own = *shape;
                                    own->BindMaterial(kShadowOff, item->GetName()); // receiver: cIShape
                                }
                            }
                            if (item) item->Release(); // receiver: the item (cIGZUnknown)
                        }
                        if (list) static_cast<cIExtension *>(list)->Release(); // receiver: the list's cIExtension base
                    }
                }
            }
            range.mCursor->Next(); // receiver: cISceneNodeIterator
        }
        lods.FreeBlock();
    }
    filter.Destruct();
}

void cShadowManager::RenderStatic(cIGZUnknown *texture, float *out)
{
    if (!texture || !RawSlot<6, uint8_t>(texture)) return; // receiver: the texture (ready)
    nGZSceneGraph::cIViewerRefNode *const viewer = Viewer(mRefBC.p);
    viewer->Slot94(mPass1.mId); // receiver: cIViewerRefNode (the pass-1 texture)
    viewer->Render();           // receiver: cIViewerRefNode
    nGZGraphic4::cIDeviceDX *const device = nGZGraphic4::GetCurrentDeviceCached();
    device->BeginScene(); // receiver: cIDeviceDX
    DrawBrushToTexture(mBrush.p, texture);
    device->GetStateConst()->Reset(8); // receiver: cIDeviceDX, then its state (cIDeviceState)
    device->EndScene();                // receiver: cIDeviceDX
    LightViewProjection(viewer, out);
}

void cShadowManager::RenderTarget(int32_t key0, int32_t key1, float *out)
{
    if (key0 < 0) return;
    nGZSceneGraph::cIViewerRefNode *const viewer = Viewer(mRefBC.p);
    viewer->Slot94(mPass1.mId); // receiver: cIViewerRefNode (the pass-1 texture)
    viewer->Render();           // receiver: cIViewerRefNode
    nGZGraphic4::cIDeviceDX *const device = nGZGraphic4::GetCurrentDeviceCached();
    device->BeginScene(); // receiver: cIDeviceDX
    DrawBrushToTarget(mBrush.p, mRenderer.p, key0, key1);
    device->GetStateConst()->Reset(8); // receiver: cIDeviceDX, then its state (cIDeviceState)
    device->EndScene();                // receiver: cIDeviceDX
    LightViewProjection(viewer, out);
}

void cShadowManager::DrawShadow(int32_t index)
{
    using namespace nGZSceneGraph;
    if (!mRefC0.p || !mRefBC.p || index < 0 || uint32_t(index) >= mShadows.size()) return;
    cShadowEntry &entry = mShadows[uint32_t(index)];
    if (entry.mUsers <= 0) return;

    cICompositionTreeNode *node = nullptr;
    // receiver: 0x20defb's object (the node of that name)
    RawSlot<23>(CameraOwner(), entry.mName.mString.c_str(), 0u, 0u, kNamedNodeIid, &node, 0u);
    if (!node) return;

    PrepareNode(reinterpret_cast<cIGZUnknown *>(node));
    cITransformNode *const holder4 = TransformNode(mRefC4.p);
    holder4->AddChild(node);                    // receiver: mRefC4 (cITransformNode)
    holder4->SetTransform(&kIdentityTransform); // receiver: mRefC4 (cITransformNode)
    ScopedRef settings{nullptr};
    if (UseSnowForShadows(settings)) holder4->SetTranslation(kSnowLift); // receiver: mRefC4 (cITransformNode)
    float facing[4];
    holder4->SetRotation(simgeom::NotchQuaternion(facing, int32_t(uint16_t(FacingPointerToNotch(&entry.mOwner))))); // receiver: mRefC4
    Viewer(mRefBC.p)->SetRotation(mLightDirection); // receiver: cIViewerRefNode

    if (FitShadowCamera(mRefBC.p, mRefC4.p)) {
        if (entry.mScaleKeys[0] >= 0) RenderTarget(entry.mScaleKeys[0], entry.mScaleKeys[1], entry.mMatrix);
        else RenderStatic(entry.mTexture.Get(), entry.mMatrix);
        if (void *const bone = FindRootBone(reinterpret_cast<cIGZUnknown *>(node))) {
            float relative[7];
            std::memcpy(entry.mBox, RelativeTransform(relative, bone, reinterpret_cast<cIGZUnknown *>(node)), sizeof(entry.mBox));
        } else {
            std::memcpy(entry.mBox, kIdentityTransform.mValues, sizeof(entry.mBox));
        }
        ++entry.mMatrixStamp;
    }
    node->GetParent()->RemoveChild(node); // receiver: the node's parent (cICompositionTreeNode)
    if (cICompositionTreeNode *const held = node) {
        node = nullptr;
        held->Release(); // receiver: the node (cIGZUnknown)
    }
    settings.ReleaseObject();
    // ORIGINAL-BUG: entry.mQueued stays set (see the declaration).
}

} // namespace nTSSG
