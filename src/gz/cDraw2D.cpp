// nGZGraphic4::cDraw2D and cDraw2DSystemService (lane OD): src/gz/cDraw2D.h.
#include "gz/cDraw2D.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <functional>
#include <optional>
#include <utility>

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/GraphicServices.h"
#include "gz/cDeviceDX.h"
#include "gz/cDeviceEnum.h"
#include "gz/cDeviceStateDX.h"
#include "gz/cDraw2DBufferHardwareCache.h"
#include "gz/cGZAny.h"           // AnyBoolean
#include "gz/cGZGraphicSystem.h" // cBackbufferAsGZBufferRaw
#include "gz/cIGZPropertySet.h"  // GZServiceString, cIGZPropertySetString
#include "gz/cImageManipDX.h"
#include "gz/cMemBlock.h"
#include "gz/cSurfaceCopy.h"
#include "gz/cTextureDX.h"
#include "gz/cVertexArrayDX.h"
#include "gz/RenderStates.h"
#include "ifaces/cIGZCOM.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIPixelFormat.h"
#include "ifaces/cIPixelFormatARGB.h"
#include "ifaces/cIPixelFormatsManager.h"
#include "sg/TextureDescription.h" // AdjustTextureSize
#include "rz/RZAutoRef.h"
#include "rz/ServiceGetters.h"   // CachedServiceGetter
#include "rz/Singleton_20ddfb.h" // GetServiceObject
#include "rz/StaticGetters.h"    // GetDirectorFrameworkObject

namespace nGZGraphic4 {

namespace {

// The render state commands the renderer hands the state cache (ApplyStateCommand): built on the stack with the
// state's game vtable, id and size; the bytes the original leaves unwritten are zero here (the state cache reads only
// each state's fields). The vtable is named at each call (GameAddress of that class's constant), so tools/callers.py
// sees which game vtables our code stores.
template <class State> State Command(uint8_t id, const void *vtable)
{
    State state{};
    state.mVTable = vtable;
    state.mId = id;
    state.mSize = static_cast<uint16_t>(sizeof(State));
    return state;
}

constexpr uint8_t kViewportId = 1;
constexpr uint8_t kDepthRangeId = 2;
constexpr uint8_t kClippingId = 3;
constexpr uint8_t kCullModeId = 4;
constexpr uint8_t kAlphaBlendId = 0x11;
constexpr uint8_t kAlphaTestId = 0x12;
constexpr uint8_t kAlphaTestFunctionId = 0x13;
constexpr uint8_t kDepthTestId = 0x14;
constexpr uint8_t kTextureBlendId = 0x19;
constexpr uint8_t kColorScalarId = 0x1a;
constexpr uint8_t kTextureAddressingId = 0x1c;
constexpr uint8_t kTextureFilterId = 0x1e;
constexpr uint8_t kTextureMipFilterId = 0x1f;

uint32_t Word(const void *object) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object)); }

void Apply(cIDeviceState *state, const void *command) { state->ApplyStateCommand(Word(command)); }

// The viewport as fractions of the target (the state's four words carry floats).
void ApplyViewport(cIDeviceState *state, float left, float top, float right, float bottom)
{
    cViewportState viewport = Command<cViewportState>(kViewportId, cViewportState::VTable());
    std::memcpy(&viewport.mX, &left, 4);
    std::memcpy(&viewport.mY, &top, 4);
    std::memcpy(&viewport.mWidth, &right, 4);
    std::memcpy(&viewport.mHeight, &bottom, 4);
    Apply(state, &viewport);
}

void ApplyClippingAndDepthRange(cIDeviceState *state)
{
    cClippingState clipping = Command<cClippingState>(kClippingId, cClippingState::VTable());
    clipping.mEnabled = 1;
    Apply(state, &clipping);
    cDepthRangeState range = Command<cDepthRangeState>(kDepthRangeId, cDepthRangeState::VTable());
    range.mMin = 0.0f;
    range.mMax = 1.0f;
    Apply(state, &range);
}

// The alpha blend state as ApplyState and ApplyDrawState set it: enabled, source factor, destination factor (the op 0).
void ApplyAlphaBlend(cIDeviceState *state, uint8_t enabled, uint8_t source, uint8_t dest)
{
    cAlphaBlendState blend = Command<cAlphaBlendState>(kAlphaBlendId, GameAddress<void>(cAlphaBlendState::kVTable));
    blend.mEnabled = enabled;
    blend.mSource = source;
    blend.mDest = dest;
    blend.mOperation = 0;
    Apply(state, &blend);
}

// The alpha test: always on, against 0x80 (cut the transparent half) or 0.
void ApplyAlphaTest(cIDeviceState *state, uint8_t reference)
{
    cAlphaTestState test = Command<cAlphaTestState>(kAlphaTestId, GameAddress<void>(cAlphaTestState::kVTable));
    test.mEnabled = 1;
    test.mReference = reference;
    Apply(state, &test);
}

// Texture stage 0's colour and alpha operations (four bytes each, the same for both) and its result argument 0.
void ApplyTextureBlend(cIDeviceState *state, uint32_t operations)
{
    cTextureBlendState blend =
        Command<cTextureBlendState>(kTextureBlendId, GameAddress<void>(cTextureBlendState::kVTable));
    blend.mStage = 0;
    std::memcpy(&blend.mColorOp, &operations, 4);
    std::memcpy(&blend.mAlphaOp, &operations, 4);
    blend.mResultArg = 0;
    Apply(state, &blend);
}

// The material's shape drawn under a handler: a fault in it (no material set, say) is swallowed, as the original's
// __try / __except(1) does.
void DrawGuarded(cIDraw2DMaterial *material, cIDraw2D *draw2d, uint32_t kind, uint32_t count, const float *points,
                 const float *values)
{
    __try {
        material->Draw(draw2d, kind, count, points, values);
    } __except (1) {
    }
}

// The draw system, cached at 0x10c95dc (uncounted, as the original's getters leave it).
cIGZDrawSystem *DrawSystem()
{
    return static_cast<cIGZDrawSystem *>(
        CachedServiceGetter<cDraw2D::kDrawSystemCell, cIGZDrawSystem::kServiceId, cIGZDrawSystem::kIid>());
}

ifaces::cIGZFrameWork *Framework() { return static_cast<ifaces::cIGZFrameWork *>(GetServiceObject()); }

// ---- image records: the classes it makes and the game code it calls --------------------------------------------------

constexpr uint32_t kTextureClass = 0xbb892198; // a cTextureDX (asked for cITexture)
constexpr uint32_t kBufferClass = 0xc470d325;  // a cGZBuffer (asked for cIGZBuffer)

// The COM registry (the root director's cIGZCOM) the classes are asked of.
ifaces::cIGZCOM *Com() { return As<ifaces::cIGZCOM>(GetDirectorFrameworkObject()); }

void *PixelFormats() { return GetPixelFormatsManagerCached(); } // the manager, cached
cIImageManip *ImageManip() { return static_cast<cIImageManip *>(GetImageManipCached()); }

// Clamps a texture's size and level count to what the device allows for the format (ours, sg/TextureDescription.h).
void AdjustTextureSize(cIDeviceDX *device, void *format, int32_t *width, int32_t *height, uint32_t *levels)
{
    nGZSceneGraph::AdjustTextureSize(device, static_cast<cIPixelFormat *>(format), width, height, levels, false);
}

// cSurfaceCopy.h's clip (0x18ce70) on cGZRects (four ints, left top right bottom, as RECT).
static_assert(sizeof(cGZRect) == sizeof(RECT), "cGZRect is a RECT's four words");
bool ClipCopyRects(cGZRect *source, const cGZRect *sourceBounds, cGZRect *target, const cGZRect *targetBounds)
{
    return nGZGraphic4::ClipCopyRects(reinterpret_cast<RECT *>(source), reinterpret_cast<const RECT *>(sourceBounds),
                                      reinterpret_cast<RECT *>(target), reinterpret_cast<const RECT *>(targetBounds));
}

// A memory image as a surface (nRZGraphic4::cMemBlockAsSurface, ours: src/gz/cMemBlock.h) the image manipulator copies
// from and to, built and torn down through its hooked entry points (the tests stand in for them). Counted once, as the
// original does.
class MemorySurface {
public:
    MemorySurface(void *bits, void *format, uint32_t width, uint32_t height, uint32_t pitch)
    {
        nRZGraphic4::cMemBlockAsSurface::Construct(mRaw, nullptr, bits, format, width, height, pitch);
        ++mRaw[1]; // AddRef
    }
    ~MemorySurface() { nRZGraphic4::cMemBlockAsSurface::Destruct(reinterpret_cast<nRZGraphic4::cMemBlockAsSurface *>(mRaw), nullptr); }
    MemorySurface(const MemorySurface &) = delete;
    MemorySurface &operator=(const MemorySurface &) = delete;
    void *Get() { return mRaw; }

private:
    uint32_t mRaw[3];
};
static_assert(sizeof(MemorySurface) == 12, "cMemBlockAsSurface: 12 bytes");

// A null appended to a vector of counted pointers (0x14aac0 when it must grow): the elements are copied into the new
// storage (each counted again) and the old ones let go, as a vector of a counted-pointer type does when its element has no
// no-throw move.
void AppendCountedNull(std::vector<cIGZUnknown *> &items)
{
    if (items.size() != items.capacity()) {
        items.push_back(nullptr);
        return;
    }
    size_t capacity = items.capacity() + items.capacity() / 2;
    if (capacity < items.size() + 1) capacity = items.size() + 1;
    std::vector<cIGZUnknown *> grown;
    grown.reserve(capacity);
    for (cIGZUnknown *texture : items) {
        if (texture) texture->AddRef();
        grown.push_back(texture);
    }
    grown.push_back(nullptr);
    for (cIGZUnknown *texture : items)
        if (texture) texture->Release();
    items.swap(grown);
}

// An int product as the 32-bit multiplication wraps it.
int32_t Mul(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b)); }

// An int as the original converts it to a float after a multiplication: as the unsigned value (via a double).
float UnsignedFloat(int32_t value) { return static_cast<float>(static_cast<double>(static_cast<uint32_t>(value))); }

// One atlas cell's quad: the corners in target pixels and the texture coordinates.
struct Quad {
    float x0, y0, x1, y1;
    float u0, v0, u1, v1;
};

// A cell of an image record, as the draw code reads it: its texture, the area of its region that is image (the region
// less the border texels) and the atlas texture's size.
struct CellView {
    uint32_t atlas;
    int32_t left, top, right, bottom; // the image part of the region, in texels
    int32_t textureWidth, textureHeight;
};

CellView ViewCell(const cDraw2D::Pool &pool, const cDraw2D::CellRef &cell, const cDraw2D::Record &record)
{
    const cTextureAtlas &atlas = pool.atlases[cell.atlas];
    const cGZRect &whole = atlas.mNodes.data()[0].rect;
    const cGZRect &region = atlas.mNodes.data()[cell.region].rect;
    const int32_t borderX = record.borderX, borderY = record.borderY;
    return {cell.atlas,
            region.left + borderX,
            region.top + borderY,
            region.right - borderX,
            region.bottom - borderY,
            whole.right - whole.left,
            whole.bottom - whole.top};
}

// The quad on the batch the renderer lends: the cell's texture set, four vertices (position, colour 0, texture
// coordinates) and the indices of two triangles, drawn.
// ORIGINAL-BUG: the original also stores a fifth colour (0) one vertex after the four it reserved; the next batch
// overwrites it before anything reads it, so it is not reproduced.
void DrawQuad(cDraw2D *self, cIGZUnknown *texture, const Quad &quad)
{
    self->mState.p->SetTexture(0, texture);
    float *positions = nullptr;
    uint32_t *colours = nullptr;
    float *uvs = nullptr;
    uint16_t *indices = nullptr;
    self->LockBatch(4, 6, &positions, &colours, &uvs, &indices);
    const float xs[4] = {quad.x0, quad.x1, quad.x1, quad.x0};
    const float ys[4] = {quad.y0, quad.y0, quad.y1, quad.y1};
    const float us[4] = {quad.u0, quad.u1, quad.u1, quad.u0};
    const float vs[4] = {quad.v0, quad.v0, quad.v1, quad.v1};
    for (uint32_t i = 0; i < 4; ++i) {
        colours[i * 5] = 0;
        positions[i * 5] = xs[i];
        positions[i * 5 + 1] = ys[i];
        uvs[i * 5] = us[i];
        uvs[i * 5 + 1] = vs[i];
    }
    static const uint16_t kTriangles[6] = {0, 1, 2, 0, 2, 3};
    std::memcpy(indices, kTriangles, sizeof kTriangles);
    self->DrawBatch();
}

} // namespace

// ---- the behaviour template's registry --------------------------------------------------------------------------

namespace {
std::optional<nGZEvent::cEventAttributeMap> gDraw2DRegistry;
}

nGZEvent::cEventAttributeMap *cDraw2DBehaviorTraits::Registry()
{
    nGZEvent::cEventAttribute defaults = nGZEvent::behavior_mt::DefaultAttribute();
    return RegistryFor(0, &defaults);
}

nGZEvent::cEventAttributeMap *__cdecl cDraw2DBehaviorTraits::RegistryFor(uint32_t id,
                                                                         const nGZEvent::cEventAttribute *attribute)
{
    if (!gDraw2DRegistry) {
        // The shared 0x1464f0 registers the base events in the base registry first; the copy then holds them.
        nGZEvent::cAbstractBehaviorType::RegisterBaseEvents();
        gDraw2DRegistry.emplace(*nGZEvent::cAbstractBehaviorType::Registry());
        (*gDraw2DRegistry)[cDraw2D::kDeviceStateEvent] = {cDraw2D::kDeviceStateEvent, "CurrentDeviceEvent", 1, 0x3b6c3e0a,
                                                           2, 4};
    }
    if (id != 0) (*gDraw2DRegistry)[id] = *attribute;
    return &*gDraw2DRegistry;
}

void cDraw2DBehaviorTraits::ResetRegistryForTest() { gDraw2DRegistry.reset(); }

// ---- construction -----------------------------------------------------------------------------------------------

cDraw2D::cDraw2D()
    : mCurrentRecord(-1), mChangeCount(0), mPositionOffset(0), mColourOffset(0), mUvOffset(0), mClipEmpty(false),
      mNeedsRebuild(false)
{
    // Told when the device comes and goes (OnEvent), and given the device objects now when it is there already.
    RZAutoRef graphics{nullptr};
    if (ifaces::cIGZFrameWork *const framework = Framework())
        framework->GetSystemService(kGraphicSystemId, kGraphicSystemIid, &graphics.mObject);
    static_cast<nGZEvent::cIAbstractBehavior *>(graphics.mObject)
        ->Connect(kDeviceStateEvent, static_cast<cIDraw2D *>(this), kDeviceStateEvent);
    cIDeviceDX *const device = GetCurrentDeviceCached();
    if (device && device->IsDeviceReady()) CreateDeviceObjects();
    graphics.ReleaseObject();
}

cDraw2D::~cDraw2D() { ReleaseDeviceObjects(); }

// ---- the device objects -------------------------------------------------------------------------------------------

namespace {

// The vertex the renderer writes (20 bytes): two floats of position, a colour, two floats of texture coordinates. The
// usage ids the vertex format is asked for (and the offsets are read back by).
constexpr uint32_t kPositionUsage = 0x5b830781, kColourUsage = 0xdb830795, kUvUsage = 0xbb8307ab;
constexpr uint32_t kTwoFloats = 1, kColour = 5; // cVertexElement::type

// The vertex shader (vs_1_1), written for this renderer: the position, colour and first texture coordinates come in as
// v0, v1 and v2. The position is scaled and moved into clip space by constant registers c0 (scale) and c1 (offset); the
// colour goes out as diffuse and the texture coordinates as the first set, x and y.
const char kVertexShader[] =
    "vs_1_1\n"
    "; the 2D renderer's vertices: position (v0), colour (v1) and texture coordinates (v2)\n"
    "dcl_position0 v0\n"
    "dcl_color0 v1\n"
    "dcl_texcoord0 v2\n"
    "; clip position = position * c0 + c1\n"
    "mul r1, v0, c0\n"
    "add oPos, r1, c1\n"
    "mov oD0, v1\n"
    "mov oT0.xy, v2\n";

// The compile flags (cIShaderProgramsManager): bit 0 debug information, bit 2 software vertex processing.
constexpr uint32_t kShaderDebug = 1, kShaderSoftware = 4;

// The dynamic arrays' COM classes (and the interfaces asked for) and size.
constexpr uint32_t kVertexArrayClass = 0xbbfffce5, kIndexArrayClass = 0x9c043807;
constexpr uint32_t kArrayBytes = 0x10000;
constexpr uint32_t kStateBlockKind = 3; // CreateStateBlock's first argument

// The cached service getters (gz/GraphicServices.h): the vertex format builder, the shader compiler and the vertex
// declarations manager.
cIVertexFormatsManager *FormatsManager() { return static_cast<cIVertexFormatsManager *>(GetVertexFormatsManagerCached()); }
cIShaderProgramsManager *ShaderManager() { return static_cast<cIShaderProgramsManager *>(GetShaderProgramsManagerCached()); }
cIVertexDeclarationsManagerDX *DeclarationsManager()
{
    return static_cast<cIVertexDeclarationsManagerDX *>(GetVertexDeclarationsManagerCached());
}

// A dynamic array of the class (made when there is none) that is created on the device with `kind`: an array that has
// lost its Direct3D buffer, or has the other kind, lets go of it and is created again.
void EnsureArray(cDraw2D::Ref<cIDynamicVertexArray> &array, cIDeviceDX *device, uint32_t classId, uint32_t iid,
                 uint32_t kind)
{
    if (!array.p) {
        ifaces::cIGZCOM *const com = Com();
        array.Reset();
        com->GetClassObject(classId, iid, reinterpret_cast<void **>(&array.p));
    }
    if (!array.p->IsCreated() || array.p->GetKind() != kind) {
        array.p->ReleaseResource();
        array.p->Create(device, kArrayBytes, kind);
    }
}

// The alpha blend, alpha test, alpha test function and texture blend of one blend mode (the engine's block has the
// first three and a texture filter of its own).
struct BlendMode {
    uint8_t blendEnabled, blendSource, blendDest;
    uint8_t testEnabled, testReference;
    uint8_t testFunction; // 0: none sent
    uint32_t colourOps, alphaOps; // the texture blend's four operation bytes each (operation, argument 1, 2, 0)
};

void SendBlendMode(cIDeviceState *state, const BlendMode &mode)
{
    ApplyAlphaBlend(state, mode.blendEnabled, mode.blendSource, mode.blendDest);
    cAlphaTestState test = Command<cAlphaTestState>(kAlphaTestId, GameAddress<void>(cAlphaTestState::kVTable));
    test.mEnabled = mode.testEnabled;
    test.mReference = mode.testReference;
    Apply(state, &test);
    if (mode.testFunction) {
        cAlphaTestFunction function =
            Command<cAlphaTestFunction>(kAlphaTestFunctionId, GameAddress<void>(cAlphaTestFunction::kVTable));
        function.mFunction = mode.testFunction;
        Apply(state, &function);
    }
    cTextureBlendState blend =
        Command<cTextureBlendState>(kTextureBlendId, GameAddress<void>(cTextureBlendState::kVTable));
    blend.mStage = 0;
    std::memcpy(&blend.mColorOp, &mode.colourOps, 4);
    std::memcpy(&blend.mAlphaOp, &mode.alphaOps, 4);
    blend.mResultArg = 0;
    Apply(state, &blend);
}

// The four blend modes SetBlendMode selects between: opaque, cut out (alpha above half), and the two blended ones.
constexpr uint32_t kOpsKeep = 0x03020100, kOpsModulate = 0x05020300;
const BlendMode kBlendModes[4] = {
    {0, 1, 0, 0, 0, 0, kOpsKeep, kOpsKeep},
    {0, 1, 0, 1, 0x80, 6, kOpsKeep, kOpsKeep},
    {1, 4, 5, 1, 0, 4, kOpsKeep, kOpsModulate},
    {1, 1, 5, 1, 0, 4, kOpsModulate, kOpsModulate},
};

} // namespace

void cDraw2D::CreateDeviceObjects()
{
    mMaterial.Reset();
    mDevice.Assign(GetCurrentDeviceCached());
    mState.Assign(mDevice.p->GetStateConst());

    // The vertex format and where each element sits in it.
    if (!mVertexFormat.p) {
        FormatsManager()->BeginFormat();
        FormatsManager()->AddElement(0, kTwoFloats, kPositionUsage, 0);
        FormatsManager()->AddElement(0, kColour, kColourUsage, 0);
        FormatsManager()->AddElement(0, kTwoFloats, kUvUsage, 0);
        cIVertexFormatsManager *const formats = FormatsManager();
        mVertexFormat.Reset();
        formats->EndFormat(&mVertexFormat.p);
        mPositionOffset = mVertexFormat.p->GetOffsetAny(kPositionUsage, 0);
        mColourOffset = mVertexFormat.p->GetOffsetAny(kColourUsage, 0);
        mUvOffset = mVertexFormat.p->GetOffsetAny(kUvUsage, 0);
    }

    // The vertex shader; software vertex processing when the device's settings force it.
    if (!mVertexShader.p || !mVertexShader.p->IsCreated()) {
        cIGZPropertySetString *const properties =
            As<cIDeviceEnum>(mDevice.p->GetDeviceEnumConst())->GetProperties();
        gz::AnyBoolean forceSoftware(0);
        const GZServiceString name("draw2dForceSWVP");
        properties->GetPropertyValue(&name, &forceSoftware);
        const uint32_t flags = forceSoftware.mValue ? kShaderDebug | kShaderSoftware : kShaderDebug;
        cIShaderProgramsManager *const shaders = ShaderManager();
        mVertexShader.Reset();
        shaders->AssembleProgram(mDevice.p, kVertexShader, sizeof(kVertexShader), &mVertexShader.p, nullptr, flags);
    }
    if (!mDeclaration.p || !mDeclaration.p->IsCreated()) {
        cIVertexDeclarationsManagerDX *const declarations = DeclarationsManager();
        mDeclaration.Reset();
        declarations->GetDeclaration(mDevice.p, mVertexFormat.p, &mDeclaration.p);
    }

    // The dynamic arrays: with the vertex shader's processing kind (2 when it is not a hardware shader, else 1).
    const uint32_t kind = mVertexShader.p->GetInfo()->hardware == 0 ? 2 : 1;
    EnsureArray(mVertexArray, mDevice.p, kVertexArrayClass, kIidDynamicVertexComponentArray, kind);
    EnsureArray(mIndexArray, mDevice.p, kIndexArrayClass, kIidDynamicVertexIndexArray, kind);

    // The state blocks: each recorded by sending its states to the state cache.
    const auto sendSampler = [this] {
        cTextureAddressingState address =
            Command<cTextureAddressingState>(kTextureAddressingId, GameAddress<void>(cTextureAddressingState::kVTable));
        address.mSampler = 0;
        address.mAddressU = 2;
        address.mAddressV = 2;
        address.mAddressW = 0;
        Apply(mState.p, &address);
        cTextureFilterState filter =
            Command<cTextureFilterState>(kTextureFilterId, GameAddress<void>(cTextureFilterState::kVTable));
        filter.mSampler = 0;
        filter.mMinFilter = 1;
        filter.mMagFilter = 1;
        filter.mMaxAnisotropy = 1;
        Apply(mState.p, &filter);
        cTextureMipFilterState mip =
            Command<cTextureMipFilterState>(kTextureMipFilterId, GameAddress<void>(cTextureMipFilterState::kVTable));
        mip.mSampler = 0;
        mip.mMipFilter = 0;
        mip.mLodBias = 0;
        Apply(mState.p, &mip);
    };
    if (!mDefaultBlock.p || !mDefaultBlock.p->IsCreated()) {
        ApplyDrawState();
        cIDeviceState *const state = mState.p;
        mDefaultBlock.Reset();
        state->CreateStateBlock(kStateBlockKind, Word(&mDefaultBlock.p));
    }
    for (uint32_t mode = 0; mode < 4; ++mode) {
        if (mBlendBlocks[mode].p && mBlendBlocks[mode].p->IsCreated()) continue;
        ApplyDrawState();
        sendSampler();
        SendBlendMode(mState.p, kBlendModes[mode]);
        cIDeviceState *const state = mState.p;
        mBlendBlocks[mode].Reset();
        state->CreateStateBlock(kStateBlockKind, Word(&mBlendBlocks[mode].p));
    }
    // The engine's block (always made again): no blending, the alpha test against 0 with function 4, linear filtering
    // of the texture's minification and magnification off.
    ApplyDrawState();
    ApplyAlphaBlend(mState.p, 0, 1, 0);
    ApplyAlphaTest(mState.p, 0);
    cAlphaTestFunction function =
        Command<cAlphaTestFunction>(kAlphaTestFunctionId, GameAddress<void>(cAlphaTestFunction::kVTable));
    function.mFunction = 4;
    Apply(mState.p, &function);
    cTextureFilterState filter =
        Command<cTextureFilterState>(kTextureFilterId, GameAddress<void>(cTextureFilterState::kVTable));
    filter.mSampler = 0;
    filter.mMinFilter = 0;
    filter.mMagFilter = 0;
    filter.mMaxAnisotropy = 1;
    Apply(mState.p, &filter);
    cIDeviceState *const state = mState.p;
    mEngineBlock.Reset();
    state->CreateStateBlock(kStateBlockKind, Word(&mEngineBlock.p));

    // The clip stack starts again with the whole target.
    mClips.clear();
    const cDeviceTargetSize size = mDevice.p->GetTargetSize();
    cDraw2DClip whole{};
    whole.rect = {0, 0, static_cast<int32_t>(size.mWidth), static_cast<int32_t>(size.mHeight)};
    whole.depth = 0.5f;
    mClips.push_back(whole);
    mClipEmpty = !(whole.rect.left < whole.rect.right && whole.rect.top < whole.rect.bottom);
    RebuildAtlases();
}

void cDraw2D::ReleaseDeviceObjects()
{
    mMaterial.Reset();
    mDefaultBlock.Reset();
    mEngineBlock.Reset();
    for (Ref<cIDeviceResource> &block : mBlendBlocks) block.Reset();
    mVertexShader.Reset();
    mDeclaration.Reset();
    // The arrays let their Direct3D buffers go and are kept.
    if (mVertexArray.p) mVertexArray.p->ReleaseResource();
    if (mIndexArray.p) mIndexArray.p->ReleaseResource();
    for (Pool &pool : mPools) {
        for (cIGZUnknown *&texture : pool.textures.items) {
            if (cIGZUnknown *const old = texture) {
                texture = nullptr;
                old->Release();
            }
        }
    }
    mDevice.Reset();
    mState.Reset();
}

// ---- cIGZUnknown, events ----------------------------------------------------------------------------------------

bool cDraw2D::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    switch (riid) {
    case cGZDrawContextRenderToHardware::kIid:
        *out = static_cast<cGZDrawContextRenderToHardware *>(this);
        AddRef();
        return true;
    case cIGZDrawContextEngineFactory::kIid:
        *out = static_cast<cIGZDrawContextEngineFactory *>(this);
        AddRef();
        return true;
    case cIDraw2D::kIid:
    case 1:
    case kIidBehavior2:
        *out = static_cast<cIDraw2D *>(this);
        AddRef();
        return true;
    case kIidBehaviorType:
        *out = static_cast<cIDraw2D *>(this);
        return true;
    }
    return false;
}

void cDraw2D::OnEvent(uint32_t type, uint32_t, void *data, uint32_t)
{
    if (type == nGZEvent::cConnectionChangedEvent::kType) {
        SyncActive();
    } else if (type == kDeviceStateEvent) {
        // The device event's code: 1 the device is there (again), 3 it is going.
        const uint32_t code = static_cast<const cDeviceEventBody *>(data)->mCode;
        if (code == 1)
            CreateDeviceObjects();
        else if (code == 3)
            ReleaseDeviceObjects();
    }
}

// ---- cIDraw2D: material, clips ----------------------------------------------------------------------------------

void cDraw2D::SetMaterial(cIDraw2DMaterial *material)
{
    mMaterial.Assign(material);
    if (!mMaterial.p) return;
    uint32_t block = mMaterial.p->GetStateBlock(this, mDevice.p);
    if (!block) block = mDefaultBlock.Word();
    mState.p->ApplyStateBlock(block);
    ApplyTopClip();
}

void cDraw2D::PushClipRect(const cGZRect *rect, bool relative, float depth) { PushClip(0, 0, rect, relative, depth); }

void cDraw2D::PushClipOrigin(int32_t x, int32_t y, bool relative, float depth)
{
    const cGZRect unlimited{0, 0, 99999, 99999};
    PushClip(x, y, &unlimited, relative, depth);
}

void cDraw2D::PushClip(int32_t x, int32_t y, const cGZRect *rect, bool relative, float depth)
{
    cDraw2DClip clip;
    cGZRect area = *rect;
    if (relative) {
        // Inside the current clip: from its origin, cut to its rectangle.
        const cDraw2DClip &top = mClips.back();
        clip.originX = x + top.originX;
        clip.originY = y + top.originY;
        area.left += clip.originX;
        area.top += clip.originY;
        area.right += clip.originX;
        area.bottom += clip.originY;
        Intersect(&area, nullptr, &area, &top.rect);
    } else {
        const cDeviceTargetSize size = mDevice.p->GetTargetSize();
        const cGZRect target{0, 0, static_cast<int32_t>(size.mWidth), static_cast<int32_t>(size.mHeight)};
        clip.originX = x;
        clip.originY = y;
        area.left += x;
        area.top += y;
        area.right += x;
        area.bottom += y;
        Intersect(&area, nullptr, &area, &target);
    }
    clip.rect = area;
    clip.depth = depth;
    mClips.push_back(clip);
    mClipEmpty = !(area.right > area.left && area.bottom > area.top);
    ApplyTopClip();
}

void cDraw2D::PopClip()
{
    // The stack is not checked: popping the last clip leaves the one below the vector's start as the top.
    mClips.pop_back();
    const cGZRect &area = mClips.data()[mClips.size() - 1].rect;
    mClipEmpty = !(area.right > area.left && area.bottom > area.top);
    ApplyTopClip();
}

bool __fastcall cDraw2D::Intersect(cGZRect *out, void *, const cGZRect *a, const cGZRect *b)
{
    if (a->right >= b->left && a->left <= b->right && a->bottom >= b->top && a->top <= b->bottom) {
        out->left = a->left > b->left ? a->left : b->left;
        out->top = a->top > b->top ? a->top : b->top;
        out->right = a->right < b->right ? a->right : b->right;
        out->bottom = a->bottom < b->bottom ? a->bottom : b->bottom;
        return true;
    }
    out->bottom = 0;
    out->top = 0;
    out->right = 0;
    out->left = 0;
    return false;
}

void cDraw2D::ApplyTopClip()
{
    if (mClipEmpty) return;
    const cDeviceTargetSize size = mDevice.p->GetTargetSize();
    const int32_t width = static_cast<int32_t>(size.mWidth);
    const int32_t height = static_cast<int32_t>(size.mHeight);
    const cDraw2DClip &top = mClips.back();
    const int32_t left = top.rect.left < 0 ? 0 : top.rect.left;
    const int32_t up = top.rect.top < 0 ? 0 : top.rect.top;
    const int32_t right = width < top.rect.right ? width : top.rect.right;
    const int32_t bottom = height < top.rect.bottom ? height : top.rect.bottom;
    if (!(left < right) || !(up < bottom)) return;

    cIDeviceState *const state = mState.p;
    ApplyClippingAndDepthRange(state);
    const float fw = static_cast<float>(width);
    const float fh = static_cast<float>(height);
    ApplyViewport(state, static_cast<float>(left) / fw, static_cast<float>(up) / fh, static_cast<float>(right) / fw,
                  static_cast<float>(bottom) / fh);

    // An orthographic projection of the clip's pixels (half-pixel centred) from the clip's origin, at its depth.
    const float scaleX = 2.0f / static_cast<float>(right - left);
    const float scaleY = 2.0f / static_cast<float>(up - bottom);
    const float offsetX =
        (-(static_cast<float>(right - left) * 0.5f + static_cast<float>(left)) - 0.5f + static_cast<float>(top.originX)) *
        scaleX;
    const float offsetY =
        (-(static_cast<float>(bottom - up) * 0.5f + static_cast<float>(up)) - 0.5f + static_cast<float>(top.originY)) *
        scaleY;
    const float depth = top.depth;
    float *const constants = state->LockVertexFloatConstants(0, 2);
    constants[0] = scaleX;
    constants[1] = scaleY;
    constants[2] = 0.0f;
    constants[3] = 0.0f;
    constants[4] = offsetX;
    constants[5] = offsetY;
    constants[6] = depth;
    constants[7] = 1.0f;
}

// ---- cIDraw2D: shapes -------------------------------------------------------------------------------------------

void cDraw2D::DrawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, float width)
{
    if (mClipEmpty) return;
    // A quad along the line, half the width either side, through the pixel centres.
    const float dx = static_cast<float>(x1 - x0);
    const float dy = static_cast<float>(y1 - y0);
    const float length = static_cast<float>(std::sqrt(static_cast<double>(dy * dy + dx * dx)));
    const float inverse = 1.0f / length;
    const float nx = dx * inverse * 0.5f;
    const float ny = static_cast<float>(y1 - y0) * inverse * 0.5f;
    const float acrossX = ny * width;
    const float acrossY = -nx * width;
    const float startX = static_cast<float>(x0) + 0.5f - nx;
    const float startY = static_cast<float>(y0) + 0.5f - ny;
    const float endX = nx + (static_cast<float>(x1) + 0.5f);
    const float endY = (static_cast<float>(y1) + 0.5f) + ny;
    float points[8] = {startX + acrossX, acrossY + startY, endX + acrossX, acrossY + endY,
                       endX - acrossX,   endY - acrossY,   startX - acrossX, startY - acrossY};
    if (x0 == x1 && y0 == y1) {
        // A zero-length line has no direction (0 * 1/0): every point is NaN, whatever the width. The original's SSE
        // arithmetic gives the default NaN (sign set) in all but the first two points' y, where the negated direction
        // is the addition's first operand (+NaN). Which NaN an addition of two keeps depends on that operand order, which
        // our compiler is free to swap, so the points are set.
        const float negative = std::bit_cast<float>(0xffc00000u), positive = std::bit_cast<float>(0x7fc00000u);
        for (float &p : points) p = negative;
        points[1] = positive;
        points[3] = positive;
    }
    const float values[4] = {0.0f, 1.0f, 1.0f, 0.0f};
    DrawGuarded(mMaterial.p, this, 1, 4, points, values);
}

void cDraw2D::FillRect(int32_t x, int32_t y, int32_t width, int32_t height)
{
    if (mClipEmpty) return;
    const float left = static_cast<float>(x);
    const float top = static_cast<float>(y);
    const float right = static_cast<float>(width + x);
    const float bottom = static_cast<float>(height + y);
    const float points[8] = {left, top, right, top, right, bottom, left, bottom};
    const float values[4] = {0.0f, 1.0f, 2.0f, 3.0f};
    DrawGuarded(mMaterial.p, this, 0, 4, points, values);
}

void cDraw2D::FillTriangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    if (mClipEmpty) return;
    const float points[6] = {static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x1),
                             static_cast<float>(y1), static_cast<float>(x2), static_cast<float>(y2)};
    const float values[3] = {0.0f, 1.0f, 2.0f};
    DrawGuarded(mMaterial.p, this, 0, 3, points, values);
}

void cDraw2D::FillQuad(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3)
{
    if (mClipEmpty) return;
    const float points[8] = {static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x1),
                             static_cast<float>(y1), static_cast<float>(x2), static_cast<float>(y2),
                             static_cast<float>(x3), static_cast<float>(y3)};
    const float values[4] = {0.0f, 1.0f, 2.0f, 3.0f};
    DrawGuarded(mMaterial.p, this, 0, 4, points, values);
}

void cDraw2D::SetBlendMode(uint32_t mode, float alpha)
{
    mMaterial.Reset();
    mState.p->ApplyStateBlock(mBlendBlocks[mode].Word());
    // The alpha as a byte (0..255, clamped) through the float's bits: alpha + 49152 puts alpha * 256 in the low
    // mantissa. The clamp is the original's branch-free one, its wrap included (a sum below -2^31 reads as 255).
    const float biased = alpha + 49152.0f;
    uint32_t bits;
    std::memcpy(&bits, &biased, 4);
    const int32_t value = static_cast<int32_t>(bits + 0xb8c00000u);
    const int32_t over = static_cast<int32_t>(0xffu - static_cast<uint32_t>(value)) >> 31;
    const uint8_t level = static_cast<uint8_t>((~(value >> 31) & value) | over);
    cColorScalarState scalar =
        Command<cColorScalarState>(kColorScalarId, GameAddress<void>(cColorScalarState::kVTable));
    scalar.mColor = level * 0x01010101u;
    Apply(mState.p, &scalar);
    ApplyTopClip();
}

// ---- cIDraw2D: image records -------------------------------------------------------------------------------------

namespace {

// DrawImage's work, apart from the handler: the part `source` of the image drawn into `destination`. The rectangles
// are copied, cut to the record's image and the first clip together, and each cell that meets the cut part is a quad.
void DrawImageBody(cDraw2D *self, const cGZRect *destination, int32_t index, const cGZRect *source)
{
    if (self->mClipEmpty || index == -1) return;
    const cDraw2D::Record &record = self->mRecords[index];
    cGZRect src = *source;
    cGZRect dst = *destination;
    cGZRect visible = {0, 0, static_cast<int32_t>(record.width), static_cast<int32_t>(record.height)};
    if (!ClipCopyRects(&src, &visible, &dst, &self->mClips.data()[0].rect)) return;
    const cDraw2D::Pool &pool = self->mPools[record.pool];
    const cDraw2D::CellRef *cell = record.cells.data();
    int32_t y = 0;
    int32_t lastHeight = 0;
    for (uint32_t row = 0; row < record.rows; ++row) {
        int32_t x = 0;
        for (uint32_t column = 0; column < record.columns; ++column, ++cell) {
            const CellView view = ViewCell(pool, *cell, record);
            const int32_t width = view.right - view.left;
            lastHeight = view.bottom - view.top;
            // The cell in image space.
            const cGZRect cellRect = {x, y, width + x, lastHeight + y};
            const int32_t cellX = x;
            x += width;
            if (!cDraw2D::Intersect(&visible, nullptr, &src, &cellRect)) continue;
            const int32_t destWidth = dst.right - dst.left;
            const int32_t destHeight = dst.bottom - dst.top;
            const float sourceWidth = static_cast<float>(src.right - src.left);
            const float sourceHeight = static_cast<float>(src.bottom - src.top);
            Quad quad;
            quad.x0 = static_cast<float>(Mul(visible.left - src.left, destWidth)) / sourceWidth + static_cast<float>(dst.left);
            quad.x1 = static_cast<float>(Mul(visible.right - src.left, destWidth)) / sourceWidth + static_cast<float>(dst.left);
            quad.y0 = static_cast<float>(Mul(visible.top - src.top, destHeight)) / sourceHeight + static_cast<float>(dst.top);
            quad.y1 = static_cast<float>(Mul(visible.bottom - src.top, destHeight)) / sourceHeight + static_cast<float>(dst.top);
            const float textureWidth = static_cast<float>(view.textureWidth);
            const float textureHeight = static_cast<float>(view.textureHeight);
            quad.u0 = static_cast<float>(view.left - cellX + visible.left) / textureWidth;
            quad.v0 = static_cast<float>(view.top - y + visible.top) / textureHeight;
            quad.u1 = static_cast<float>(view.right - (cellX + width) + visible.right) / textureWidth;
            quad.v1 = static_cast<float>(view.bottom - (y + lastHeight) + visible.bottom) / textureHeight;
            DrawQuad(self, pool.textures.items[view.atlas], quad);
        }
        y += lastHeight;
    }
}

// DrawRecord's work, apart from the handler: the whole image stretched to `destination`.
void DrawRecordBody(cDraw2D *self, const cGZRect *destination, int32_t index)
{
    if (self->mClipEmpty || index == -1) return;
    const cDraw2D::Record &record = self->mRecords[index];
    const cDraw2D::Pool &pool = self->mPools[record.pool];
    const cDraw2D::CellRef *cell = record.cells.data();
    const int32_t destWidth = destination->right - destination->left;
    const int32_t destHeight = destination->bottom - destination->top;
    const float imageWidth = UnsignedFloat(static_cast<int32_t>(record.width));
    const float imageHeight = UnsignedFloat(static_cast<int32_t>(record.height));
    int32_t y = 0;
    int32_t lastHeight = 0;
    for (uint32_t row = 0; row < record.rows; ++row) {
        int32_t x = 0;
        for (uint32_t column = 0; column < record.columns; ++column, ++cell) {
            const CellView view = ViewCell(pool, *cell, record);
            Quad quad;
            quad.x0 = UnsignedFloat(Mul(destWidth, x)) / imageWidth + static_cast<float>(destination->left);
            x += view.right - view.left;
            quad.x1 = UnsignedFloat(Mul(x, destWidth)) / imageWidth + static_cast<float>(destination->left);
            lastHeight = view.bottom - view.top;
            quad.y0 = UnsignedFloat(Mul(destHeight, y)) / imageHeight + static_cast<float>(destination->top);
            quad.y1 = UnsignedFloat(Mul(lastHeight + y, destHeight)) / imageHeight + static_cast<float>(destination->top);
            const float textureWidth = static_cast<float>(view.textureWidth);
            const float textureHeight = static_cast<float>(view.textureHeight);
            quad.u0 = static_cast<float>(view.left) / textureWidth;
            quad.v0 = static_cast<float>(view.top) / textureHeight;
            quad.u1 = static_cast<float>(view.right) / textureWidth;
            quad.v1 = static_cast<float>(view.bottom) / textureHeight;
            DrawQuad(self, pool.textures.items[view.atlas], quad);
        }
        y += lastHeight;
    }
}

} // namespace

// The drawing runs under the original's __try / __except(1): a fault in it (an unplaced record's pool, a missing device
// object) is swallowed and the call ends.
void cDraw2D::DrawImage(const cGZRect *destination, int32_t record, const cGZRect *source)
{
    __try {
        DrawImageBody(this, destination, record, source);
    } __except (1) {
    }
}

void cDraw2D::DrawRecord(const cGZRect *destination, int32_t record)
{
    __try {
        DrawRecordBody(this, destination, record);
    } __except (1) {
    }
}

void cDraw2D::DrawImageAt(int32_t x, int32_t y, int32_t record, const cGZRect *source)
{
    const cGZRect destination{x, y, source->right - source->left + x, source->bottom - source->top + y};
    DrawImage(&destination, record, source);
}

void cDraw2D::DrawRecordAt(int32_t x, int32_t y, int32_t record)
{
    if (mClipEmpty || record == -1) return;
    const Record &entry = mRecords[record];
    const cGZRect destination{x, y, static_cast<int32_t>(entry.width) + x, static_cast<int32_t>(entry.height) + y};
    DrawRecord(&destination, record);
}

int32_t cDraw2D::AllocateRecord()
{
    if (!mFreeRecords.empty()) {
        const int32_t index = mFreeRecords.back();
        mFreeRecords.pop_back();
        return index;
    }
    const int32_t index = static_cast<int32_t>(mRecords.size());
    mRecords.emplace_back();
    return index;
}

namespace {

// A locked rectangle of a surface (UploadKeyed32 and UploadRecord's record): destroyed it unlocks, lets the surface go
// when the unlock left it set, and zeroes the pointers, as the original's lock record does.
struct RectLock {
    cSurfaceLock lock{};

    RectLock() = default;
    RectLock(const RectLock &) = delete;
    RectLock &operator=(const RectLock &) = delete;
    ~RectLock()
    {
        if (!lock.surface) return;
        As<cILockableSurface>(lock.surface)->Unlock(&lock);
        if (void *const held = lock.surface) {
            lock.surface = nullptr;
            static_cast<cIGZUnknown *>(held)->Release();
        }
        lock.bits = nullptr;
        lock.rowBytes = 0;
        lock.pitch = 0;
        lock.rows = 0;
    }
    uint8_t *Bits() const { return static_cast<uint8_t *>(lock.bits); }
};

} // namespace

void __cdecl cDraw2D::UploadKeyed32(const uint8_t *src, uint32_t width, uint32_t height, uint32_t stride, void *surface,
                                    const cGZRect *rect, uint32_t key, uint32_t mask)
{
    if (width == 0 || height == 0) return;
    RectLock locked;
    As<cILockableSurface>(surface)->LockRect(&locked.lock, rect->left, rect->top, rect->right - rect->left,
                                             rect->bottom - rect->top, false);
    uint8_t *destination = locked.Bits();
    for (uint32_t row = height; row != 0; --row) {
        for (uint32_t x = 0; x != width; ++x) {
            uint32_t pixel, out;
            std::memcpy(&pixel, src + x * 4, 4);
            out = ((pixel & mask) != key) ? pixel : 0;
            std::memcpy(destination + x * 4, &out, 4);
        }
        src += stride;
        destination += locked.lock.pitch;
    }
}

void cDraw2D::UploadRecord(Record &record)
{
    using ifaces::cIPixelFormat;
    using ifaces::cIPixelFormatARGB;
    cIGZBuffer *const buffer = record.buffer;
    if (!buffer->Lock(0x40)) return;
    const uint8_t *const bits = static_cast<const uint8_t *>(buffer->GetColorSurfaceBits());
    void *const targetFormat = GetPixelFormatForColorType(buffer->GetNativeColorType().format);
    const int32_t stride = static_cast<int32_t>(buffer->GetColorSurfaceStride());
    const int32_t bytesPerPixel = static_cast<int32_t>(buffer->GetBytesPerPixel());
    Pool &pool = mPools[record.pool];
    const int32_t borderX = record.borderX, borderY = record.borderY;
    int32_t sourceY = 0;
    const CellRef *cell = record.cells.data();
    for (uint32_t row = 0; row < record.rows; ++row) {
        int32_t sourceX = 0;
        int32_t lastBottom = 0, lastTop = 0;
        for (uint32_t column = 0; column < record.columns; ++column, ++cell) {
            const cGZRect region = pool.atlases[cell->atlas].mNodes[cell->region].rect;
            cILockableSurface *const lockable = static_cast<cILockableSurface *>(
                static_cast<cITexture *>(pool.textures.items[cell->atlas])->GetTargetLockableSurface(0));
            const bool firstColumn = borderX && column == 0;
            const bool firstRow = borderY && row == 0;
            const bool lastColumn = borderX && column == record.columns - 1;
            const bool lastRow = borderY && row == record.rows - 1;
            const int32_t x = sourceX - (borderX ? !firstColumn : 0);
            const int32_t y = sourceY - (borderY ? !firstRow : 0);
            const uint8_t *const source = bits + y * stride + x * bytesPerPixel;
            const cGZRect rect{region.left + firstColumn, region.top + firstRow, region.right - lastColumn,
                               region.bottom - lastRow};
            const int32_t width = rect.right - rect.left, height = rect.bottom - rect.top;

            const uint32_t surfaceFormat = As<cIPixelFormat>(lockable->GetPixelFormat())->GetFormatId();
            const uint32_t sourceFormat = As<cIPixelFormat>(targetFormat)->GetFormatId();
            if (sourceFormat == surfaceFormat && record.word1c != 0 && (bytesPerPixel == 2 || bytesPerPixel == 4)) {
                void *argbFace = nullptr;
                static_cast<cIGZUnknown *>(targetFormat)->QueryInterface(0x9ac38991, &argbFace);
                if (argbFace) static_cast<cIGZUnknown *>(argbFace)->Release();
                cIPixelFormatARGB *const argb = As<cIPixelFormatARGB>(argbFace);
                const uint32_t blue = argb->GetBlueBits();
                const uint32_t green = argb->GetGreenBits();
                const uint32_t red = argb->GetRedBits();
                const uint32_t colourBits = red + green + blue;
                if (colourBits != 0) {
                    argb->GetBitsPerPixel();
                    const uint32_t mask = 0xffffffffu >> ((32 - colourBits) & 31);
                    const uint32_t key = buffer->ConvertRGBValueToNative(uint8_t(record.word1c >> 16),
                                                                         uint8_t(record.word1c >> 8),
                                                                         uint8_t(record.word1c)) &
                                         mask;
                    if (bytesPerPixel == 2) {
                        if (width != 0 && height != 0) {
                            RectLock locked;
                            lockable->LockRect(&locked.lock, rect.left, rect.top, width, height, false);
                            const uint8_t *in = source;
                            uint8_t *out = locked.Bits();
                            for (int32_t r = 0; r < height; ++r) {
                                for (int32_t c = 0; c < width; ++c) {
                                    uint16_t pixel;
                                    std::memcpy(&pixel, in + c * 2, 2);
                                    const uint16_t value = ((uint32_t(pixel) & mask) != key) ? pixel : uint16_t(0);
                                    std::memcpy(out + c * 2, &value, 2);
                                }
                                in += stride;
                                out += locked.lock.pitch;
                            }
                        }
                    } else {
                        UploadKeyed32(source, uint32_t(width), uint32_t(height), uint32_t(stride), lockable, &rect, key,
                                      mask);
                    }
                }
            } else {
                As<cIImageManip>(GetImageManipCached())
                    ->LoadSurfaceFromMemoryRect(source, targetFormat, uint32_t(width), uint32_t(height),
                                                uint32_t(stride), lockable, reinterpret_cast<const RECT *>(&rect),
                                                &record.word1c);
            }

            // The border texels: the first and last columns and rows of the region copy their neighbours.
            {
                const int32_t regionWidth = region.right - region.left, regionHeight = region.bottom - region.top;
                RectLock locked;
                lockable->LockRect(&locked.lock, region.left, region.top, regionWidth, regionHeight, false);
                const int32_t pixelBytes = int32_t(As<cIPixelFormat>(lockable->GetPixelFormat())->GetBitsPerPixel() >> 3);
                uint8_t *const base = locked.Bits();
                const int32_t pitch = locked.lock.pitch;
                if (firstColumn || lastColumn) {
                    for (uint32_t r = 0; r < locked.lock.rows; ++r) {
                        uint8_t *const line = base + int32_t(r) * pitch;
                        if (firstColumn) std::memcpy(line, line + pixelBytes, size_t(pixelBytes));
                        if (lastColumn)
                            std::memcpy(line + (regionWidth - 1) * pixelBytes, line + (regionWidth - 2) * pixelBytes,
                                        size_t(pixelBytes));
                    }
                }
                if (firstRow) std::memcpy(base, base + pitch, size_t(regionWidth * pixelBytes));
                if (lastRow)
                    std::memcpy(base + (regionHeight - 1) * pitch, base + (regionHeight - 2) * pitch,
                                size_t(regionWidth * pixelBytes));
            }
            sourceX += (region.right - 2 * borderX) - region.left;
            lastBottom = region.bottom;
            lastTop = region.top;
        }
        sourceY += (lastBottom - 2 * borderY) - lastTop;
    }
    buffer->Unlock(0x40);
}

bool cDraw2D::ReleaseRecordCells(Record &record)
{
    bool rebuild = false;
    for (size_t i = 0; i != record.cells.size(); ++i) {
        const CellRef cell = record.cells[i];
        Pool &pool = mPools[record.pool];
        cTextureAtlas &atlas = pool.atlases[cell.atlas];
        atlas.Free(static_cast<int32_t>(cell.region));
        if (atlas.mRegions == 0) {
            // Nothing left in the atlas: its tree forgotten and its texture let go.
            atlas.Clear();
            cIGZUnknown *&texture = pool.textures.items[cell.atlas];
            if (cIGZUnknown *const old = texture) {
                texture = nullptr;
                old->Release();
            }
            rebuild = true;
        }
    }
    record.columns = 0;
    record.rows = 0;
    record.pool = kUnplaced;
    record.cells.clear();
    return rebuild;
}

bool cDraw2D::PlaceRecord(Record &record, bool create)
{
    cIDeviceEnum *const devices = As<cIDeviceEnum>(mDevice.p->GetDeviceEnumConst());
    // The formats a cell can be stored in: 16 bits (4444) and 32 bits (8888) when the display takes them for textures
    // (the 8-bit alpha format is asked too, and unused).
    cIPixelFormat *const format8 =
        static_cast<cIPixelFormat *>(As<ifaces::cIPixelFormatsManager>(PixelFormats())->GetARGB(8, 8, 0, 0, 0));
    cIPixelFormat *const format16 =
        static_cast<cIPixelFormat *>(As<ifaces::cIPixelFormatsManager>(PixelFormats())->GetARGB(16, 4, 4, 4, 4));
    cIPixelFormat *const format32 =
        static_cast<cIPixelFormat *>(As<ifaces::cIPixelFormatsManager>(PixelFormats())->GetARGB(32, 8, 8, 8, 8));
    devices->IsColorFormatSupportedForDisplay(format8, 4);
    cIPixelFormat *const usable16 = devices->IsColorFormatSupportedForDisplay(format16, 4) ? format16 : nullptr;
    cIPixelFormat *const usable32 = devices->IsColorFormatSupportedForDisplay(format32, 4) ? format32 : nullptr;
    if (!usable16 && !usable32) return false;

    // Pool 1 (16 bits) for an image under 24 bits when the display takes it, else pool 2 (32 bits), else pool 1.
    uint32_t poolIndex;
    cIPixelFormat *format;
    if (record.c < 0x18 && usable16) {
        poolIndex = 1;
        format = usable16;
    } else {
        poolIndex = 2;
        format = usable32;
        if (!usable32) {
            poolIndex = 1;
            format = usable16;
        }
    }
    record.pool = poolIndex;
    record.columns = 0;
    record.rows = 0;
    Pool &pool = mPools[poolIndex];
    int32_t textureWidth = 0x100, textureHeight = 0x100;
    uint32_t levels = 1;
    AdjustTextureSize(mDevice.p, format, &textureWidth, &textureHeight, &levels);
    // A cell has a border of 2 texels each side when the image is not as wide (high) as the texture, so that filtering
    // does not pick up its neighbour.
    const bool borderedX = static_cast<int32_t>(record.width) != textureWidth;
    const bool borderedY = static_cast<int32_t>(record.height) != textureHeight;
    record.borderX = borderedX;
    record.borderY = borderedY;
    const int32_t borderX = borderedX ? 2 : 0;
    const int32_t borderY = borderedY ? 2 : 0;

    bool placed = true;
    int32_t remainingHeight = static_cast<int32_t>(record.height);
    for (;;) {
        int32_t remainingWidth = static_cast<int32_t>(record.width);
        record.columns = 0;
        const int32_t cellHeight = (std::min)(remainingHeight, textureHeight - borderY);
        remainingHeight -= cellHeight;
        do {
            const int32_t cellWidth = (std::min)(remainingWidth, textureWidth - borderX);
            remainingWidth -= cellWidth;
            // The first atlas of the pool that has a cell for it; one with no texture (yet) is the place for a new one.
            size_t firstFree = pool.atlases.size();
            size_t index = 0;
            for (;;) {
                if (index == pool.atlases.size()) {
                    if (create) {
                        if (firstFree == pool.atlases.size()) {
                            AppendCountedNull(pool.textures.items);
                            pool.atlases.emplace_back();
                        }
                        ifaces::cIGZCOM *const com = Com();
                        cIGZUnknown *&slot = pool.textures.items[firstFree];
                        if (cIGZUnknown *const old = slot) {
                            slot = nullptr;
                            old->Release();
                        }
                        com->GetClassObject(kTextureClass, kIidTexture, reinterpret_cast<void **>(&slot));
                        if (static_cast<cITexture *>(slot)->Create(mDevice.p, static_cast<uint32_t>(textureWidth),
                                                                   static_cast<uint32_t>(textureHeight), format, levels,
                                                                   1.0f, 2)) {
                            pool.atlases[firstFree].Init(textureWidth, textureHeight);
                            index = firstFree;
                        } else {
                            if (cIGZUnknown *const failed = slot) {
                                slot = nullptr;
                                failed->Release();
                            }
                            placed = false;
                            break;
                        }
                    } else {
                        placed = false;
                        break;
                    }
                }
                cIGZUnknown *const texture = pool.textures.items[index];
                if (!texture || !static_cast<cITexture *>(texture)->IsCreated()) {
                    firstFree = (std::min)(firstFree, index);
                    ++index;
                    continue;
                }
                const int32_t region = pool.atlases[index].Allocate(cellWidth + borderX, cellHeight + borderY);
                if (region == -1) {
                    ++index;
                    continue;
                }
                record.cells.push_back({static_cast<uint32_t>(index), static_cast<uint32_t>(region)});
                ++record.columns;
                break;
            }
        } while (placed && remainingWidth != 0);
        ++record.rows;
        if (remainingHeight == 0 && placed) return true;
        if (remainingHeight == 0 || !placed) break;
    }
    ReleaseRecordCells(record);
    return false;
}

int32_t cDraw2D::CreateRecord(uint32_t width, uint32_t height, uint32_t c, uint32_t d)
{
    const int32_t index = AllocateRecord();
    Record &record = mRecords[index];
    record.refCount = 0;
    record.c = c;
    record.width = width;
    record.height = height;
    record.d = d;
    record.bufferUncounted = 0;
    record.buffer = nullptr;
    record.word1c = 0;
    // The buffer's pixel format: alpha only, 16 bits or 32 bits (the same classes as the pools').
    uint32_t format = 9, bits = 0x20;
    if (c == 0) {
        format = 2;
        bits = 8;
    } else if (c < 0x18) {
        format = 7;
        bits = 0x10;
    }
    cIGZBuffer *buffer = nullptr;
    Com()->GetClassObject(kBufferClass, cIGZBuffer::kIid, reinterpret_cast<void **>(&buffer));
    int32_t result = index;
    if (buffer->Init(record.width, record.height, format, bits)) {
        record.buffer = buffer;
        buffer->AddRef();
        if (!PlaceRecord(record, false)) RebuildAtlases();
        if (record.cells.empty()) {
            record.buffer->Release();
            record.buffer = nullptr;
            mFreeRecords.push_back(index);
            result = -1;
        }
    } else {
        mFreeRecords.push_back(index);
        result = -1;
    }
    if (buffer) buffer->Release();
    return result;
}

void cDraw2D::SetRecordPixels(int32_t index, void *bits, void *format, uint32_t width, uint32_t height, uint32_t pitch,
                              const uint32_t *key)
{
    if (index == -1) return;
    Record &record = mRecords[index];
    // The caller's memory and the record's buffer as surfaces, the first copied into the second.
    MemorySurface from(bits, format, width, height, pitch);
    const cGZRect fromRect = {0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height)};
    void *const targetBits = record.buffer->GetColorSurfaceBits();
    void *const targetFormat = GetPixelFormatForColorType(record.buffer->GetNativeColorType().format);
    const uint32_t targetPitch = record.buffer->GetColorSurfaceStride();
    MemorySurface to(targetBits, targetFormat, record.width, record.height, targetPitch);
    const cGZRect toRect = {0, 0, static_cast<int32_t>(record.width), static_cast<int32_t>(record.height)};
    const uint32_t noKey = 0;
    ImageManip()->CopySurfaceRect(from.Get(), reinterpret_cast<const RECT *>(&fromRect), to.Get(),
                                  reinterpret_cast<const RECT *>(&toRect), &noKey);
    record.word1c = *key;
    UploadRecord(record);
}

void cDraw2D::RebuildAtlases()
{
    mNeedsRebuild = false;
    // Every atlas starts again: a live texture keeps its size, a lost one is dropped.
    for (Pool &pool : mPools) {
        for (size_t i = 0; i < pool.atlases.size(); ++i) {
            cTextureAtlas &atlas = pool.atlases[i];
            cIGZUnknown *&texture = pool.textures.items[i];
            if (texture && static_cast<cITexture *>(texture)->IsCreated()) {
                const cGZRect whole = atlas.mNodes.data()[0].rect;
                atlas.Clear();
                atlas.Init(whole.right - whole.left, whole.bottom - whole.top);
            } else {
                if (cIGZUnknown *const old = texture) {
                    texture = nullptr;
                    old->Release();
                }
                atlas.Clear();
            }
        }
    }
    // Every record with a buffer is placed again, the largest (by area, then index) first.
    std::vector<std::pair<uint32_t, uint32_t>> order;
    order.reserve(mRecords.size());
    for (size_t i = 0; i < mRecords.size(); ++i) {
        Record &record = mRecords[i];
        record.columns = 0;
        record.rows = 0;
        record.pool = kUnplaced;
        record.cells.clear();
        if (record.buffer) order.emplace_back(record.height * record.width, static_cast<uint32_t>(i));
    }
    std::sort(order.begin(), order.end(), std::greater<>());
    for (const auto &entry : order) {
        Record &record = mRecords[entry.second];
        if (!PlaceRecord(record, true)) continue;
        if (!record.bufferUncounted) {
            UploadRecord(record);
        } else if (cIGZUnknown *const cache = record.buffer->GetHardwareCache()) {
            // The buffer's cache copies the pixels again when it next loads.
            cIGZUnknown *held = nullptr;
            if (cache->QueryInterface(cDraw2DBufferHardwareCache::kIidHardwareCache, reinterpret_cast<void **>(&held)))
                static_cast<cDraw2DBufferHardwareCache *>(cache)->Invalidate();
            if (held) held->Release();
        }
    }
    // An atlas with nothing in it gives its texture back; the others are asked their size (and it is not used).
    for (Pool &pool : mPools) {
        for (size_t i = 0; i < pool.atlases.size(); ++i) {
            cTextureAtlas &atlas = pool.atlases[i];
            cIGZUnknown *&texture = pool.textures.items[i];
            if (atlas.mNodes.empty() || atlas.mRegions == 0) {
                atlas.Clear();
                if (cIGZUnknown *const old = texture) {
                    texture = nullptr;
                    old->Release();
                }
            } else if (texture) {
                static_cast<cITexture *>(texture)->GetVideoMemorySize();
            }
        }
    }
}

void cDraw2D::Tick()
{
    if (mNeedsRebuild && mDevice.p && mDevice.p->IsDeviceReady()) RebuildAtlases();
}

void cDraw2D::AddRefRecord(int32_t record)
{
    if (record != -1) ++mRecords[record].refCount;
}

void cDraw2D::ReleaseRecord(int32_t record)
{
    if (record == -1) return;
    Record &entry = mRecords[record];
    if (--entry.refCount != 0) return;
    // The record's cells handed back to its pool; true when the atlases want rebuilding.
    const bool rebuild = ReleaseRecordCells(entry);
    if (!entry.bufferUncounted) entry.buffer->Release();
    entry.buffer = nullptr;
    mFreeRecords.push_back(record);
    if (rebuild) mNeedsRebuild = true;
}

cDraw2DRecordInfo cDraw2D::GetRecordInfo(int32_t record)
{
    if (record == -1) return {0, 0, nullptr};
    const Record &entry = mRecords[record];
    cDraw2DRecordInfo info;
    info.width = entry.width;
    info.height = entry.height;
    info.pixelFormat = GetPixelFormatForColorType(entry.buffer->GetNativeColorType().format);
    return info;
}

// ---- cIDraw2D: batches ------------------------------------------------------------------------------------------

void cDraw2D::ApplyDrawState()
{
    cIDeviceState *const state = mState.p;
    state->Reset(3);
    cDepthTestState depth = Command<cDepthTestState>(kDepthTestId, GameAddress<void>(cDepthTestState::kVTable));
    depth.mTestEnabled = 0;
    depth.mWriteEnabled = 0;
    Apply(state, &depth);
    cCullModeState cull = Command<cCullModeState>(kCullModeId, GameAddress<void>(cCullModeState::kVTable));
    cull.mMode = 0;
    Apply(state, &cull);
    ApplyAlphaBlend(state, 1, 4, 5);
    state->SetVertexShader(mVertexShader.p);
    state->SetVertexDeclaration(mDeclaration.p);
    state->SetStreamSource(0, mVertexArray.p, 0x14, 0);
    state->SetIndices(mIndexArray.p);
}

bool cDraw2D::LockBatch(uint32_t vertices, uint32_t indices, float **positions, uint32_t **colours, float **uvs,
                        uint16_t **indexOut)
{
    if (vertices > kBatchVertices || indices > kBatchIndices) return false;
    mVertexArray.p->Reserve(0x14, vertices);
    mIndexArray.p->Reserve(2, indices);
    uint8_t *const base = static_cast<uint8_t *>(mVertexArray.p->Lock(1, 0, 0));
    *positions = reinterpret_cast<float *>(base + mPositionOffset);
    *colours = reinterpret_cast<uint32_t *>(base + mColourOffset);
    *uvs = reinterpret_cast<float *>(base + mUvOffset);
    *indexOut = static_cast<uint16_t *>(mIndexArray.p->Lock(1, 0, 0));
    return true;
}

void cDraw2D::DrawBatch()
{
    mVertexArray.p->Unlock();
    mIndexArray.p->Unlock();
    const uint32_t vertexCount = mVertexArray.p->GetSize() / 0x14;
    const uint32_t triangles = mIndexArray.p->GetSize() / 6;
    const uint32_t baseVertex = mVertexArray.p->GetOffset() / 0x14;
    const uint32_t startIndex = mIndexArray.p->GetOffset() >> 1;
    mDevice.p->DrawIndexedPrimitive(3, triangles, static_cast<int32_t>(baseVertex), 0, vertexCount, startIndex);
}

int32_t cDraw2D::GetClipDepth() { return static_cast<int32_t>(mClips.end() - mClips.begin()); }

cDraw2DPoint cDraw2D::GetClipOrigin()
{
    const cDraw2DClip &top = mClips.data()[mClips.size() - 1];
    return {top.originX, top.originY};
}

cGZRect cDraw2D::GetClipRect() { return mClips.data()[mClips.size() - 1].rect; }

// ---- the engine (+8) --------------------------------------------------------------------------------------------

void cDraw2D::vf19()
{
    mDrawSystemFlag = DrawSystem()->GetFlag();
    mChangeCount = mState.p->GetChangeCount();
}

bool cDraw2D::OnTargetSet()
{
    mStateDirty = true;
    mClipDirty = true;
    return true;
}

void cDraw2D::ApplyState()
{
    cIDeviceState *const state = mState.p;
    state->ApplyStateBlock(mEngineBlock.Word());
    ApplyClippingAndDepthRange(state);
    if (mTexture) {
        if (mScaled) {
            cTextureFilterState filter =
                Command<cTextureFilterState>(kTextureFilterId, GameAddress<void>(cTextureFilterState::kVTable));
            filter.mSampler = 0;
            filter.mMinFilter = 1;
            filter.mMagFilter = 1;
            filter.mMaxAnisotropy = 1;
            Apply(state, &filter);
        }
        ApplyTextureBlend(state, mModulate ? 0x03020300u : 0x03020100u);
    } else {
        ApplyTextureBlend(state, 0x03030100u);
    }
    // Blending by the engine's blend kind (vf8, vf9) and the draw system's flag: kind 2 and a plain blend alpha-test at
    // 0x80 without blending; with mFlag49 the blend factors are (1, 5), above 2 or with the flag (4, 5); no blend
    // neither blends nor cuts.
    if (mBlendIs2) {
        ApplyAlphaBlend(state, 0, 1, 0);
        ApplyAlphaTest(state, 0x80);
    } else if (mBlend) {
        if (mFlag49) {
            ApplyAlphaBlend(state, 1, 1, 5);
            ApplyAlphaTest(state, 0);
        } else if (mBlendAbove2 || mDrawSystemFlag) {
            ApplyAlphaBlend(state, 1, 4, 5);
            ApplyAlphaTest(state, 0);
        } else {
            ApplyAlphaBlend(state, 0, 1, 0);
            ApplyAlphaTest(state, 0x80);
        }
    } else {
        ApplyAlphaBlend(state, 0, 1, 0);
        ApplyAlphaTest(state, 0);
    }
    mChangeCount = state->GetChangeCount();
}

void cDraw2D::SetViewport(const cGZRect *viewport)
{
    const cDeviceTargetSize size = mDevice.p->GetTargetSize();
    const float fw = static_cast<float>(static_cast<int32_t>(size.mWidth));
    const float fh = static_cast<float>(static_cast<int32_t>(size.mHeight));
    ApplyViewport(mState.p, static_cast<float>(viewport->left) / fw, static_cast<float>(viewport->top) / fh,
                  static_cast<float>(viewport->right) / fw, static_cast<float>(viewport->bottom) / fh);
    mChangeCount = mState.p->GetChangeCount();
}

void cDraw2D::SetProjection(const float *matrix)
{
    // The engine's matrix as the shader's scale and offset (half-pixel centred), at the top clip's depth.
    const float scaleX = matrix[0];
    const float scaleY = matrix[5];
    const float offsetX = matrix[12] - scaleX * 0.5f;
    const float offsetY = matrix[13] - scaleY * 0.5f;
    const float depth = mClips.data()[mClips.size() - 1].depth;
    float *const constants = mState.p->LockVertexFloatConstants(0, 2);
    constants[0] = scaleX;
    constants[1] = scaleY;
    constants[2] = 0.0f;
    constants[3] = 0.0f;
    constants[4] = offsetX;
    constants[5] = offsetY;
    constants[6] = depth;
    constants[7] = 1.0f;
    mChangeCount = mState.p->GetChangeCount();
}

bool cDraw2D::SetTextureSource(cIGZBuffer *texture)
{
    mCurrentRecord = -1;
    if (!texture) return false;
    // The buffer's hardware cache (made and given to the buffer when it has none) holds the buffer in a record of ours.
    Ref<cDraw2DBufferHardwareCache> cache;
    bool found;
    if (cIGZUnknown *const held = texture->GetHardwareCache()) {
        found = held->QueryInterface(cDraw2DBufferHardwareCache::kIidHardwareCache, reinterpret_cast<void **>(&cache.p));
    } else {
        cache.Assign(new cDraw2DBufferHardwareCache(this));
        found = texture->SetHardwareCache(cache.p);
    }
    if (found && cache.p->Load(DrawSystem()->GetTick())) mCurrentRecord = cache.p->mRecord.mIndex;
    return mCurrentRecord != -1;
}

void cDraw2D::GetTextureTiles(std::vector<Tile> *tiles, std::vector<TileRect> *rects)
{
    Record &record = mRecords[mCurrentRecord];
    record.buffer->GetWidth();
    record.buffer->GetHeight();
    const Pool &pool = mPools[record.pool];
    const uint32_t count = record.columns * record.rows;
    tiles->resize(count);
    rects->resize(count);
    const int32_t borderX = record.borderX;
    const int32_t borderY = record.borderY;
    // The cells row by row: each a tile (its atlas's whole texture) and one piece, the cell's region less its border,
    // laid out in the buffer left to right, top to bottom.
    Tile *tile = tiles->data();
    TileRect *rect = rects->data();
    const CellRef *cell = record.cells.data();
    int32_t y = 0;
    for (uint32_t row = 0; row < record.rows; ++row) {
        int32_t x = 0;
        cGZRect region{};
        for (uint32_t column = 0; column < record.columns; ++column, ++cell, ++tile, ++rect) {
            const cTextureAtlas &atlas = pool.atlases[cell->atlas];
            region = atlas.mNodes[cell->region].rect;
            const cGZRect &whole = atlas.mNodes[0].rect;
            tile->index = static_cast<int32_t>(cell->atlas);
            tile->count = 1;
            tile->width = whole.right - whole.left;
            tile->height = whole.bottom - whole.top;
            const int32_t width = region.right - 2 * borderX - region.left;
            rect->source.left = x;
            rect->source.top = y;
            x += width;
            rect->source.right = x;
            rect->source.bottom = region.bottom - 2 * borderY - region.top + y;
            rect->texels.left = region.left + borderX;
            rect->texels.top = region.top + borderY;
            rect->texels.right = region.right - borderX;
            rect->texels.bottom = region.bottom - borderY;
        }
        // The row's height is its last cell's (with no columns the original adds what its registers hold; nothing is
        // written then).
        y += region.bottom - 2 * borderY - region.top;
    }
}

void cDraw2D::SelectTile(int32_t index, uint32_t, uint32_t)
{
    const Record &record = mRecords[mCurrentRecord];
    mState.p->SetTexture(0, mPools[record.pool].textures.items[index]);
    mChangeCount = mState.p->GetChangeCount();
}

namespace {

// DrawTriangles / DrawQuads' loop, under the original's handler (a fault ends the drawing quietly). Batches of up to
// kBatchVertices vertices; `quads` writes two triangles per four vertices, else one index per vertex.
void DrawVertexBatches(cDraw2D *self, const cGZDrawContextRenderToHardware::Vertex *vertices, int32_t count,
                       bool quads)
{
    __try {
        while (count > 0) {
            const int32_t batch = count > static_cast<int32_t>(cDraw2D::kBatchVertices)
                                      ? static_cast<int32_t>(cDraw2D::kBatchVertices)
                                      : count;
            count -= batch;
            float *positions;
            uint32_t *colours;
            float *uvs;
            uint16_t *indices;
            const uint32_t indexCount = quads ? static_cast<uint32_t>((batch >> 2) * 6) : static_cast<uint32_t>(batch);
            if (!static_cast<cIDraw2D *>(self)->LockBatch(static_cast<uint32_t>(batch), indexCount, &positions,
                                                          &colours, &uvs, &indices))
                break;
            for (uint32_t i = 0; i < static_cast<uint32_t>(batch); ++i, ++vertices) {
                *colours = vertices->colour;
                colours = reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(colours) + 0x14);
                uvs[0] = vertices->u;
                uvs[1] = vertices->v;
                uvs = reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(uvs) + 0x14);
                positions[0] = vertices->x;
                positions[1] = vertices->y;
                positions = reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(positions) + 0x14);
            }
            if (quads) {
                // ORIGINAL-BUG: six indices per started quad, so a count that is not a multiple of 4 writes past the
                // indices locked ((count >> 2) * 6). The base class only hands whole quads.
                for (uint32_t k = 0; k < static_cast<uint32_t>(batch); k += 4) {
                    *indices++ = static_cast<uint16_t>(k);
                    *indices++ = static_cast<uint16_t>(k + 1);
                    *indices++ = static_cast<uint16_t>(k + 2);
                    *indices++ = static_cast<uint16_t>(k);
                    *indices++ = static_cast<uint16_t>(k + 2);
                    *indices++ = static_cast<uint16_t>(k + 3);
                }
            } else {
                for (uint32_t i = 0; i < static_cast<uint32_t>(batch); ++i) indices[i] = static_cast<uint16_t>(i);
            }
            static_cast<cIDraw2D *>(self)->DrawBatch();
        }
    } __except (1) {
    }
}

} // namespace

void cDraw2D::DrawTriangles(const Vertex *vertices, uint32_t count)
{
    if (mChangeCount != mState.p->GetChangeCount()) ApplyStateAndClip();
    DrawVertexBatches(this, vertices, static_cast<int32_t>(count), false);
}

void cDraw2D::DrawQuads(const Vertex *vertices, uint32_t count)
{
    if (mChangeCount != mState.p->GetChangeCount()) ApplyStateAndClip();
    DrawVertexBatches(this, vertices, static_cast<int32_t>(count), true);
}

// ---- the factory (+0x100) ---------------------------------------------------------------------------------------

bool cDraw2D::CreateEngine(cIGZBuffer *target, cIGZDrawContextEngine **engine)
{
    // Only for the back buffer (its raw face answers true).
    cBackbufferAsGZBufferRaw *raw = nullptr;
    bool made = false;
    if (target->QueryInterface(cBackbufferAsGZBufferRaw::kIid, reinterpret_cast<void **>(&raw)) && raw->Slot4()) {
        cGZDrawContextRenderToHardware *const self = this;
        *engine = self;
        self->AddRef();
        made = true;
    }
    if (raw) raw->Release();
    return made;
}

// ---- cDraw2DSystemService ---------------------------------------------------------------------------------------

cDraw2DSystemService *__cdecl cDraw2DSystemService::Create() { return new cDraw2DSystemService; }

cDraw2DSystemService::~cDraw2DSystemService()
{
    if (mDraw2D) static_cast<cIDraw2D *>(mDraw2D)->Release();
}

bool cDraw2DSystemService::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    if (riid == 1 || riid == cIGZSystemService::kIID || riid == kIid) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

uint32_t cDraw2DSystemService::Release()
{
    if (--mRefCount != 0) return mRefCount;
    delete this;
    return 0;
}

bool cDraw2DSystemService::Init()
{
    if (!mDraw2D) {
        cDraw2D *const draw2d = new cDraw2D;
        cDraw2D *const old = mDraw2D;
        if (draw2d != old) {
            if (draw2d) static_cast<cIDraw2D *>(draw2d)->AddRef();
            mDraw2D = draw2d;
            if (old) static_cast<cIDraw2D *>(old)->Release();
        }
        // The renderer's factory goes to the draw system.
        RZAutoRef drawSystem{nullptr};
        if (ifaces::cIGZFrameWork *const framework = Framework())
            framework->GetSystemService(cIGZDrawSystem::kServiceId, cIGZDrawSystem::kIid, &drawSystem.mObject);
        cIGZDrawContextEngineFactory *factory = nullptr;
        static_cast<cIDraw2D *>(mDraw2D)->QueryInterface(cIGZDrawContextEngineFactory::kIid,
                                                         reinterpret_cast<void **>(&factory));
        static_cast<cIGZDrawSystem *>(drawSystem.mObject)->AddFactory(factory);
        if (factory) factory->Release();
        drawSystem.ReleaseObject();
    }
    Framework()->AddToTick(reinterpret_cast<ifaces::cIGZSystemService *>(static_cast<cIGZSystemService *>(this)));
    return true;
}

bool cDraw2DSystemService::Shutdown()
{
    if (cDraw2D *const old = mDraw2D) {
        mDraw2D = nullptr;
        static_cast<cIDraw2D *>(old)->Release();
    }
    return true;
}

bool cDraw2DSystemService::OnTick(uint32_t)
{
    if (mDraw2D) static_cast<cIDraw2D *>(mDraw2D)->Tick();
    return true;
}

} // namespace nGZGraphic4
