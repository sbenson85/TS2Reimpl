// nTSSG::cDisplayListCensorRenderStrategy (lane 261108c). See src/sg/cDisplayListCensorRenderStrategy.h.
#include "sg/cDisplayListCensorRenderStrategy.h"

#include "game/game.h"
#include "game/original.h"
#include "gz/cDeviceDX.h"
#include "gz/cDeviceStateDX.h"
#include "gz/cIGZCOM.h"
#include "rz/StaticGetters.h" // GetDirectorFrameworkObject
#include "sg/DetailObject.h"
#include "sg/SGUtilities.h" // ProjectBox
#include "sg/cDisplayList.h"
#include "sg/cDisplayListDynamicRenderStrategy.h" // DynamicWords16, DynamicStrategyState, DynamicRectRecord
#include "sg/cDynamicGeometryRect.h"
#include "sg/cIViewerRefNode.h"
#include "sg/cMaterialManager.h"
#include "sg/cTSRenderSurface.h"
#include "misc/UtilitiesE.h"

#include <bit>
#include <cstring>
#include <new>

namespace nTSSG {

namespace {

using nGZSceneGraph::cDynamicGeometryRect;
using nGZSceneGraph::cIDisplayList;
using nGZSceneGraph::DrawItem;
using nGZSceneGraph::Pass;

constexpr uint32_t kStrategyIid = 0x0bf077fd; // nGZSceneGraph::cIDisplayListRenderStrategy

// The frame state's class (COM, asked with the unknown interface) when the viewer has none yet.
constexpr uint32_t kStateClass = 0x6d548978, kUnknownIid = 1;

// The texture pass: the screen reflection's pass type (its stencil is cleared to 1), the device's clear flags (all
// sixteen bits), the depth it clears to and the clear colour (a game global only this reads).
constexpr uint32_t kTypeScreenOwn = 3;
constexpr uint32_t kClearAll = 0xffff;
constexpr float kClearDepth = 1.0f;
constexpr uint32_t kClearColorCell = 0x100933c;
constexpr uint8_t kViewportId = 1, kDepthRangeId = 2;

// Sort keys DrawItems passes over: an item without one, and on the screen reflection's pass the two the reflection
// keeps for itself (-9998 and 9998).
constexpr int32_t kNoSortKey = -1;
constexpr int32_t kReflectionKey = 9998;

// An item's material variant (the material manager's GetVariant): slot 11 draws the item in the pass.
class cIDrawItemVariant : public cIGZUnknown {
public:
    virtual void Slot3() = 0;
    virtual void Slot4() = 0;
    virtual void Slot5() = 0;
    virtual void Slot6() = 0;
    virtual void Slot7() = 0;
    virtual void Slot8() = 0;
    virtual void Slot9() = 0;
    virtual void Slot10() = 0;
    virtual void Draw(Pass *pass, DrawItem *item) = 0; // 11
};

template <class State> State Command(uint8_t id, const void *vtable)
{
    State state{};
    state.mVTable = vtable;
    state.mId = id;
    state.mSize = static_cast<uint16_t>(sizeof(State));
    return state;
}

// The frame state, counted while held.
struct HeldFrameState {
    DynamicStrategyState *mState = nullptr;
    HeldFrameState() = default;
    HeldFrameState(const HeldFrameState &) = delete;
    HeldFrameState &operator=(const HeldFrameState &) = delete;
    ~HeldFrameState()
    {
        if (mState) mState->Unknown()->Release();
    }
};
constexpr const char kMaterialName[] = "CensorMaterial";
constexpr float kCellCount = 8.0f;   // the geometry's grid, each way
constexpr float kMinExtent = 0.01f;  // a clipped rectangle narrower or lower than this is dropped

cIDisplayList *DetailList() { return static_cast<cIDisplayList *>(nGZSceneGraph::GetDetailObject()); }
nGZSceneGraph::cIMaterialManager *Materials()
{
    return static_cast<nGZSceneGraph::cIMaterialManager *>(GetCachedQueriedObject());
}

// a - b as the game's 32-bit integer subtraction (wrapping), converted to float.
float Difference(int32_t a, int32_t b) { return static_cast<float>(static_cast<int32_t>(uint32_t(a) - uint32_t(b))); }

// The game's float-to-integer rounding: x moved half a unit down (floor) or up (ceil) and added to 1.5 * 2^23, whose
// low mantissa bits are then the rounded integer, corrected by one when that integer's neighbour equals x (an exact
// integer that the round-to-even step took one too far). Single precision throughout; outside about +-2^22 (and for NaN)
// the result is the same meaningless word as the game's.
constexpr float kRoundHalf = 0.5f;
constexpr float kRoundMagic = 12582912.0f; // 1.5 * 2^23
constexpr uint32_t kRoundMagicBits = 0x4b400000u;

int32_t RoundedBits(float t) { return static_cast<int32_t>(std::bit_cast<uint32_t>(t) - kRoundMagicBits); }

int32_t Floor(float x)
{
    const float t = (x - kRoundHalf) + kRoundMagic;
    const int32_t r = RoundedBits(t);
    const int32_t up = static_cast<int32_t>(uint32_t(r) + 1);
    return static_cast<float>(up) == x ? up : r;
}

int32_t Ceil(float x)
{
    const float t = (x + kRoundHalf) + kRoundMagic;
    const int32_t r = RoundedBits(t);
    const int32_t down = static_cast<int32_t>(uint32_t(r) - 1);
    return static_cast<float>(down) == x ? down : r;
}

// The unsigned size as a float, through double (the game converts it as a signed int and adds 2^32 when negative).
float SizeAsFloat(uint32_t size) { return static_cast<float>(static_cast<double>(size)); }

// A ratio in [0, 1] of the viewport mapped to [-1, 1] (or [1, -1] for the flipped y), in double as the game does.
float ToClip(float ratio) { return static_cast<float>((static_cast<double>(ratio) - 0.5) * 2.0); }
float ToClipFlipped(float ratio) { return static_cast<float>((0.5 - static_cast<double>(ratio)) * 2.0); }

} // namespace

CensorBox::CensorBox(const CensorBox &other)
    : mBox(other.mBox), mRect{other.mRect[0], other.mRect[1], other.mRect[2], other.mRect[3]},
      mGeometry(other.mGeometry), mVisible(other.mVisible)
{
    if (mGeometry) mGeometry->AddRef();
}

CensorBox::~CensorBox()
{
    if (mGeometry) mGeometry->Release();
}

cDisplayListCensorRenderStrategy::cDisplayListCensorRenderStrategy() : mTexture(nullptr), mSlotCount(0)
{
    // mSlotStart is left as it was: the game's constructor does not write it (the first BeginPass with boxes hands it
    // to ReallocSlots with a count of 0, which ignores it).
}

cDisplayListCensorRenderStrategy::~cDisplayListCensorRenderStrategy()
{
    Clear();
    if (mSlotCount != 0) DetailList()->FreeSlots(mSlotStart, mSlotCount);
}

bool cDisplayListCensorRenderStrategy::QueryInterface(uint32_t iid, void **out)
{
    if (out == nullptr) return false;
    if (iid == kStrategyIid || iid == cITSDisplayListRenderStrategy::kIid) {
        cITSDisplayListRenderStrategy *const face = this;
        *out = face;
        AddRef();
        return true;
    }
    return rz::cRZUnknown::QueryInterface(iid, out);
}

void cDisplayListCensorRenderStrategy::Clear()
{
    mBoxes.clear();
    mVisible.clear();
}

void cDisplayListCensorRenderStrategy::AddBox(const nGZCompGeom::BoundBox *box) { mBoxes.push_back(CensorBox(*box)); }

// 0x377920. The last pass's drawn list emptied; with boxes, the material found or loaded once ("CensorMaterial"; with
// none to be had, nothing more), each box given a new geometry, the display-list slots grown to one per box, then per
// box: its projection by the pass's matrix 4 (0xaf22d0, nGZSceneGraph::ProjectBox, giving the near corner `lo` and the far one
// `hi`) rounded out to pixels (lo down, hi up), clipped to the pass's pixel viewport; a rectangle at least kMinExtent
// wide and high is drawn: the geometry spans it in clip space (x and y in [-1, 1], y up) and texture space (the pixel
// position over the pass's size), at depth max(0, lo.z), in 8 x 8 cells, handed to the pass's display list (AddGeometry
// with the material and the box's slot), and the box (copied, the geometry counted again) joins the drawn list; any
// other box keeps a zero rectangle and is not drawn. The game returns whatever eax holds (no caller reads it).
void cDisplayListCensorRenderStrategy::BeginPass(nGZSceneGraph::Pass *pass)
{
    mVisible.clear();
    if (mBoxes.empty()) return;
    if (mMaterial.mIndex == -1) {
        const nGZSceneGraph::ShapeMaterialHandle found(static_cast<int32_t>(Materials()->FindOrLoad(kMaterialName)));
        mMaterial.Assign(found);
        if (mMaterial.mIndex == -1) return;
    }

    for (uint32_t i = 0; i < mBoxes.size(); ++i) {
        CensorBox &box = mBoxes[i];
        cDynamicGeometryRect *const made = new cDynamicGeometryRect;
        cDynamicGeometryRect *const old = box.mGeometry;
        if (made != old) {
            if (made) made->AddRef();
            box.mGeometry = made;
            if (old) old->Release();
        }
    }

    const uint32_t count = static_cast<uint32_t>(mBoxes.size());
    const int32_t left = pass->mPixelRect[0];
    const int32_t top = pass->mPixelRect[1];
    const int32_t right = pass->mPixelRect[2];
    const int32_t bottom = pass->mPixelRect[3];
    if (mSlotCount < count) {
        mSlotStart = DetailList()->ReallocSlots(mSlotStart, mSlotCount, count);
        mSlotCount = count;
    }

    for (int32_t i = 0; i < static_cast<int32_t>(count); ++i) {
        CensorBox &box = mBoxes[i];
        cDynamicGeometryRect *const geometry = box.mGeometry;
        float lo[3], hi[3];
        nGZSceneGraph::ProjectBox(lo, hi, &box.mBox, pass->mMatrices[4]);
        int32_t *const rect = box.mRect;
        rect[0] = Floor(lo[0]);
        rect[1] = Floor(lo[1]);
        rect[2] = Ceil(hi[0]);
        rect[3] = Ceil(hi[1]);
        bool drawn = false;
        if (left < rect[2] && rect[0] < right && top < rect[3] && rect[1] < bottom) {
            if (rect[0] <= left) rect[0] = left;
            if (rect[1] <= top) rect[1] = top;
            if (rect[2] >= right) rect[2] = right;
            if (rect[3] >= bottom) rect[3] = bottom;
            drawn = !(kMinExtent > Difference(rect[2], rect[0])) && !(kMinExtent > Difference(rect[3], rect[1]));
        }
        if (!drawn) {
            rect[0] = rect[1] = rect[2] = rect[3] = 0;
            box.mVisible = 0;
            continue;
        }
        box.mVisible = 1;
        const float z = lo[2];
        geometry->mZ = 0.0f > z ? 0.0f : z;
        const float width = Difference(right, left);
        const float height = Difference(bottom, top);
        geometry->mX0 = ToClip(Difference(rect[0], left) / width);
        geometry->mX1 = ToClip(Difference(rect[2], left) / width);
        geometry->mY0 = ToClipFlipped(Difference(rect[3], top) / height);
        geometry->mY1 = ToClipFlipped(Difference(rect[1], top) / height);
        const float passWidth = SizeAsFloat(pass->mWidth);
        geometry->mU0 = static_cast<float>(rect[0]) / passWidth;
        geometry->mU1 = static_cast<float>(rect[2]) / passWidth;
        const float passHeight = SizeAsFloat(pass->mHeight);
        geometry->mV0 = static_cast<float>(rect[1]) / passHeight;
        geometry->mV1 = static_cast<float>(rect[3]) / passHeight;
        geometry->mCountX = kCellCount;
        geometry->mCountY = kCellCount;
        geometry->mUvFlag = 1;
        cIDisplayList *const list = pass->mOwner;
        list->AddGeometry(geometry, mMaterial.mIndex, pass, static_cast<uint32_t>(mSlotStart + i));
        mVisible.push_back(box);
    }
}

// ---- the dynamic strategy's dirty-rect part (row TS1h) ---------------------------------------------------------------

void GetViewerFrameState(nGZSceneGraph::cIViewerRefNodeBase *viewer, DynamicStrategyState **out)
{
    cIGZUnknown *made = viewer->Slot116(); // receiver: cIViewerRefNodeBase (0xa68fa0 / 0xa6c380, ret 0)
    if (made) {
        made->AddRef(); // receiver: cIGZUnknown (the frame state class)
    } else {
        cIGZCOM *const com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
        com->GetClassObject(kStateClass, kUnknownIid, reinterpret_cast<void **>(&made)); // receiver: cIGZCOM (the director)
        viewer->Slot99(made); // receiver: cIViewerRefNodeBase (0xa69530 / 0xa6c9d0, ret 4)
    }
    *out = reinterpret_cast<DynamicStrategyState *>(made);
    // ORIGINAL-BUG: not checked; a state the director could not make is a null call here.
    made->AddRef(); // receiver: cIGZUnknown (the frame state class 0x6d548978)
    if (made) made->Release(); // receiver: cIGZUnknown (the frame state class)
}

// 0x378140. `out` resized to the drawn boxes' count, then each drawn box's rectangle copied over every element; the
// count is returned. (The game's resize leaves the elements it adds unset, or past the capacity filled from its stack,
// 0x377470; each is overwritten before anything reads it.)
int32_t cDisplayListCensorRenderStrategy::CopyBoxRects(std::vector<DynamicWords16> *out)
{
    const int32_t count = static_cast<int32_t>(mVisible.size());
    out->resize(static_cast<uint32_t>(count));
    for (int32_t i = 0; i < count; ++i) std::memcpy((*out)[i].w, mVisible[i].mRect, sizeof(DynamicWords16));
    return count;
}

// 0x377f50. CopyBoxRects, then the frame state's rectangles (the ones RenderTexture kept, the last frame's) appended;
// the two counts summed. The state is the viewer's, counted for the call.
int32_t cDisplayListCensorRenderStrategy::CollectDirtyRects(Pass *pass, std::vector<DynamicWords16> *out)
{
    const int32_t boxes = CopyBoxRects(out);
    HeldFrameState held;
    GetViewerFrameState(pass->mViewer, &held.mState);
    const std::vector<DynamicWords16> &kept = held.mState->mRects54;
    const int32_t count = static_cast<int32_t>(kept.size());
    for (int32_t i = 0; i < count; ++i) out->push_back(kept[i]);
    return boxes + count;
}

// 0x378020. `out` emptied, then each drawn box's rectangle appended; the count is returned.
int32_t cDisplayListCensorRenderStrategy::ListBoxRects(std::list<DynamicWords16> *out)
{
    const int32_t count = static_cast<int32_t>(mVisible.size());
    out->clear();
    for (int32_t i = 0; i < count; ++i) {
        DynamicWords16 rect;
        std::memcpy(rect.w, mVisible[i].mRect, sizeof rect);
        out->push_back(rect);
    }
    return count;
}

// 0x378370. Nothing without the texture. With drawn boxes, the texture is set up and the items drawn into it; either
// way the drawn boxes' rectangles are kept in the viewer's frame state (CollectDirtyRects hands them back next frame).
void cDisplayListCensorRenderStrategy::RenderTexture(Pass *pass, const std::vector<DrawItem *> *items,
                                                     const std::vector<DynamicRectRecord *> *records)
{
    if (mTexture == nullptr) return;
    if (!mVisible.empty()) {
        SetUpTexture(pass);
        DrawItems(pass, items, records);
    }
    HeldFrameState held;
    GetViewerFrameState(pass->mViewer, &held.mState);
    CopyBoxRects(&held.mState->mRects54);
}

// 0x378400. The texture applied (forced), the pass's projection, viewport and depth range set (the viewport's MinZ and
// MaxZ, which the device state never reads, are left as the stack had them in the original; ours zero), then the whole
// target cleared: every flag, the game's clear colour, depth 1, the stencil to 1 on the screen reflection's pass and 0
// on any other.
void cDisplayListCensorRenderStrategy::SetUpTexture(Pass *pass)
{
    mTexture->Apply(pass, true); // receiver: cTSRenderTextureSurface (0x3751f0, ret 8)
    nGZGraphic4::cIDeviceState *const state = pass->mDeviceState;
    state->SetProjection(reinterpret_cast<const D3DMATRIX *>(pass->mMatrices[0])); // receiver: nGZGraphic4::cDeviceStateDX
    nGZGraphic4::cViewportState viewport =
        Command<nGZGraphic4::cViewportState>(kViewportId, nGZGraphic4::cViewportState::VTable());
    std::memcpy(&viewport.mX, pass->mViewportMin, sizeof(pass->mViewportMin));
    std::memcpy(&viewport.mWidth, pass->mViewportMax, sizeof(pass->mViewportMax));
    state->ApplyStateCommand(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&viewport))); // receiver: nGZGraphic4::cDeviceStateDX
    nGZGraphic4::cDepthRangeState depth =
        Command<nGZGraphic4::cDepthRangeState>(kDepthRangeId, nGZGraphic4::cDepthRangeState::VTable());
    depth.mMin = pass->mDepthRange[0];
    depth.mMax = pass->mDepthRange[1];
    state->ApplyStateCommand(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&depth))); // receiver: nGZGraphic4::cDeviceStateDX
    // receiver: nGZGraphic4::cDeviceDX
    pass->mDevice->Clear(kClearAll, GameAddress<const float>(kClearColorCell), kClearDepth,
                         pass->mRenderType == kTypeScreenOwn ? 1u : 0u);
}

// 0x3782c0. Each item (the strategy's list, with its record at the same index) whose record is on screen and marked
// (kind 4), unless its sort key is none or, on the screen reflection's pass, one of the reflection's own.
void cDisplayListCensorRenderStrategy::DrawItems(Pass *pass, const std::vector<DrawItem *> *items,
                                                 const std::vector<DynamicRectRecord *> *records)
{
    const uint32_t type = pass->mRenderType;
    const int32_t count = static_cast<int32_t>(items->size());
    for (int32_t i = 0; i < count; ++i) {
        DrawItem *const item = (*items)[i];
        const DynamicRectRecord *const record = (*records)[i];
        if (!record->mDrawn) continue;
        const int32_t key = item->mSortKey;
        if (key == kNoSortKey) continue;
        if (type == kTypeScreenOwn && (key == -kReflectionKey || key == kReflectionKey)) continue;
        if (record->mFlag1d) DrawOne(pass, item);
    }
}

void cDisplayListCensorRenderStrategy::DrawOne(Pass *pass, nGZSceneGraph::DrawItem *item)
{
    static_cast<cIDrawItemVariant *>(item->mMaterial)->Draw(pass, item); // receiver: nGZSceneGraph::cShader (0xaa69e0, ret 8)
    ++pass->mDrawStats[0];
}

// ---- the hooks' entry points ----------------------------------------------------------------------------------------

cDisplayListCensorRenderStrategy *__fastcall cDisplayListCensorRenderStrategyAdapters::Construct(Self *memory, void *)
{
    return new (memory) cDisplayListCensorRenderStrategy;
}

cITSDisplayListRenderStrategy *__cdecl cDisplayListCensorRenderStrategyAdapters::Create()
{
    cDisplayListCensorRenderStrategy *const made = new cDisplayListCensorRenderStrategy;
    return made;
}

void __fastcall cDisplayListCensorRenderStrategyAdapters::DestroyBoxes(std::vector<CensorBox> *boxes, void *)
{
    boxes->~vector();
}

} // namespace nTSSG
