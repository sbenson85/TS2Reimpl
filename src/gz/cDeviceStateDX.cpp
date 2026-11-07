#include "gz/cDeviceStateDX.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <new>

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cDeviceStateDXDetail.h"
#include "gz/cStateBlockDX.h"
#include "ifaces/cIDevice.h"
#include "ifaces/cIShaderProgram.h"
#include "ifaces/cISurface.h"
#include "rz/MiscHelpersA.h"

namespace nGZGraphic4 {

using namespace devstate;
using namespace devstate_detail;

namespace {

// QueryInterface ids under which the engine's resource objects hand out their Direct3D object (no reference taken).
constexpr uint32_t kTextureId = 0x0998dbd2;      // cTextureDX, cCubeTextureDX: IDirect3DBaseTexture9
constexpr uint32_t kVertexShaderId = 0x09b1b574; // cVertexShaderDX
constexpr uint32_t kPixelShaderId = 0xcc433307;  // cPixelShaderDX
constexpr uint32_t kDeclarationId = 0x8b448b4c;  // cVertexDeclarationDX
constexpr uint32_t kVertexBufferId = 0x0998d7e9; // vertex component arrays
constexpr uint32_t kIndexBufferId = 0x4998d917;  // vertex index arrays
constexpr uint32_t kSurfaceId = 0x4a5a7223;      // render targets and depth surfaces: IDirect3DSurface9

// The engine's texture-stage indices the texture-coordinate helpers use.
constexpr uint32_t kStageTexCoordIndex = 10;     // sent as D3DTSS_TEXCOORDINDEX
constexpr uint32_t kStageTransformFlags = 11;    // sent as D3DTSS_TEXTURETRANSFORMFLAGS

void BuildStateMaps()
{
    uint16_t *renderStates = RenderStateMap();
    uint8_t *stageStates = StageStateMap();
    uint8_t *samplerStates = SamplerStateMap();
    std::fill_n(renderStates, 0x59, uint16_t(0xffff));
    std::fill_n(stageStates, 0x12, uint8_t(0xff));
    std::fill_n(samplerStates, 0xd, uint8_t(0xff));
    const StateName *names = RenderStateNames();
    for (uint32_t i = 0; i < 0x59; ++i) renderStates[names[i].index] = static_cast<uint16_t>(names[i].d3d);
    names = StageStateNames();
    for (uint32_t i = 0; i < 0x12; ++i) stageStates[names[i].index] = static_cast<uint8_t>(names[i].d3d);
    names = SamplerStateNames();
    for (uint32_t i = 0; i < 0xd; ++i) samplerStates[names[i].index] = static_cast<uint8_t>(names[i].d3d);
}

// A target dimension (unsigned) as a float, through double as the original converts it.
float Dimension(uint32_t size) { return static_cast<float>(static_cast<double>(size)); }

// A fraction of a target dimension in pixels: round(size * fraction - 0.5 + 0.5) by the float bias trick (1.5 * 2^23
// added, the integer read from the low mantissa bits), then one less where that equals the unrounded value exactly.
int32_t ToPixels(float size, float fraction)
{
    const float scaled = size * fraction - 0.5f;
    const float biased = (scaled + 0.5f) + 12582912.0f;
    int32_t pixels = std::bit_cast<int32_t>(biased) - 0x4b400000;
    if (static_cast<float>(pixels - 1) == scaled) pixels -= 1;
    return pixels;
}

} // namespace

// --- DeviceState and StateDelta --------------------------------------------------------------------------------------

void DeviceState::SetDefaults()
{
    D3DMATRIX identity{};
    identity._11 = identity._22 = identity._33 = identity._44 = 1.0f;
    std::fill(std::begin(textures), std::end(textures), nullptr);
    softwareVertexProcessing = 0;
    vertexShader = nullptr;
    pixelShader = nullptr;
    std::fill(std::begin(renderStates), std::end(renderStates), 0u);
    std::fill(&stageStates[0][0], &stageStates[0][0] + 8 * 0x12, 0u);
    std::fill(&samplerStates[0][0], &samplerStates[0][0] + 16 * 0xd, 0u);
    std::fill(std::begin(textureTransforms), std::end(textureTransforms), identity);
    std::memset(&material, 0, sizeof(material));
    declaration = nullptr;
    std::memset(streams, 0, sizeof(streams));
    indices = nullptr;
    std::fill(std::begin(targets), std::end(targets), nullptr);
    depth = nullptr;
    targetWidth = 0;
    targetHeight = 0;
    scissor[0] = 0.0f;
    scissor[1] = 0.0f;
    scissor[2] = 1.0f;
    scissor[3] = 1.0f;
    scissorEnable = 0;
    viewport[0] = 0.0f;
    viewport[1] = 0.0f;
    viewport[2] = 1.0f;
    viewport[3] = 1.0f;
    minZ = 0.0f;
    maxZ = 1.0f;
    clipping = 1;
    projection = identity;
    world = identity;
    ambient = 0;
    lightCount = 0;
    std::memset(lights, 0, sizeof(lights));
    for (D3DLIGHT9 &light : lights) {
        light.Type = D3DLIGHT_DIRECTIONAL;
        light.Direction.z = 1.0f;
    }
}

void StateDelta::Clear()
{
    flags = 0;
    changeCount = 0;
    textureCount = 0;
    textureMask = 0;
    std::memset(textures, 0, sizeof(textures));
    softwareVertexProcessing = 0;
    vertexShaderChanged = 0;
    vertexShader = 0;
    pixelShaderChanged = 0;
    pixelShader = 0;
    renderStateCount = 0;
    std::fill(std::begin(renderStateMask), std::end(renderStateMask), 0u);
    std::memset(renderStates, 0, sizeof(renderStates));
    stageMask = 0;
    std::fill(std::begin(stageCounts), std::end(stageCounts), 0u);
    std::fill(std::begin(stageBits), std::end(stageBits), 0u);
    std::memset(stageStates, 0, sizeof(stageStates));
    samplerMask = 0;
    std::fill(std::begin(samplerCounts), std::end(samplerCounts), 0u);
    std::fill(std::begin(samplerBits), std::end(samplerBits), 0u);
    std::memset(samplerStates, 0, sizeof(samplerStates));
    transformCount = 0;
    transformMask = 0;
    std::memset(transforms, 0, sizeof(transforms));
    materialChanged = 0;
    std::memset(&material, 0, sizeof(material));
    declarationChanged = 0;
    declaration = 0;
    streamCount = 0;
    streamMask = 0;
    std::memset(streams, 0, sizeof(streams));
    indicesChanged = 0;
    indices = 0;
}

// --- Construction ----------------------------------------------------------------------------------------------------

// The constructor leaves only +0x17 and the change set's bytes after its change flags unwritten (as the original).
cDeviceStateDX::cDeviceStateDX(cDeviceDX *device, void *deviceEnum)
    : mRefCount(0), mDevice(device), mDeviceEnum(deviceEnum), mD3D(nullptr), mDefaultsBuilt(0), mHardwareVertexProcessing(0),
      mRenderTargetCount(0), mChangeCount(0), mChanges{}, mStreamChanges(0), mDeclarationChanged(0), mIndicesChanged(0),
      mPending{}, mVertexFloatStart(kNoStart), mVertexIntStart(kNoStart), mVertexBoolStart(kNoStart),
      mPixelFloatStart(kNoStart), mPixelIntStart(kNoStart), mPixelBoolStart(kNoStart), mStateBlock98(nullptr),
      mStateBlock9c(nullptr), mSentTargets{}, mSentDepth(nullptr), mSentViewport{}, mSentScissor{}, mSentScissorEnable(0),
      mHeld9a14(nullptr), mHeld9a18(nullptr)
{
    mWanted.SetDefaults();
    std::memset(mVertexFloats, 0, sizeof(mVertexFloats));
    std::memset(mVertexInts, 0, sizeof(mVertexInts));
    std::memset(mVertexBools, 0, sizeof(mVertexBools));
    std::memset(mPixelFloats, 0, sizeof(mPixelFloats));
    std::memset(mPixelInts, 0, sizeof(mPixelInts));
    std::memset(mPixelBools, 0, sizeof(mPixelBools));
    mDefaults.SetDefaults();
    mDelta.Clear();
    BuildStateMaps();
}

cDeviceStateDX::~cDeviceStateDX() { ReleaseDeviceObjects(); }

void cDeviceStateDX::ReleaseDeviceObjects()
{
    if (cIGZUnknown *held = mHeld9a14) {
        mHeld9a14 = nullptr;
        held->Release();
    }
    if (cIGZUnknown *held = mHeld9a18) {
        mHeld9a18 = nullptr;
        held->Release();
    }
    if (mStateBlock98) {
        mStateBlock98->Release();
        mStateBlock98 = nullptr;
    }
    if (mStateBlock9c) {
        mStateBlock9c->Release();
        mStateBlock9c = nullptr;
    }
    mD3D = nullptr;
}

// --- cIGZUnknown -----------------------------------------------------------------------------------------------------

bool cDeviceStateDX::QueryInterface(uint32_t riid, void **out)
{
    if (!out) return false;
    switch (riid) {
    case 1:
    case kInterfaceId:
        *out = static_cast<cIDeviceState *>(this);
        AddRef();
        return true;
    }
    return false;
}

uint32_t cDeviceStateDX::AddRef() { return ++mRefCount; }

uint32_t cDeviceStateDX::Release()
{
    if (--mRefCount == 0) {
        delete this;
        return 0;
    }
    return mRefCount;
}

// --- Slots -----------------------------------------------------------------------------------------------------------

void *cDeviceStateDX::GetResourceManager() { return As<ifaces::cIDevice>(mDevice)->GetResourceManager(); }
void *cDeviceStateDX::GetResourceManagerConst() { return As<ifaces::cIDevice>(mDevice)->GetResourceManager(); }

void cDeviceStateDX::SetTexture(uint32_t stage, cIGZUnknown *texture)
{
    IDirect3DBaseTexture9 *native = nullptr;
    if (texture) texture->QueryInterface(kTextureId, reinterpret_cast<void **>(&native));
    if (mWanted.textures[stage] == native) return;
    mChanges.textures |= static_cast<uint16_t>(1u << (stage & 31));
    mWanted.textures[stage] = native;
    Report(mD3D->SetTexture(stage, native), 0xa21);
    ++mChangeCount;
}

void cDeviceStateDX::SetVertexShader(cIGZUnknown *shader)
{
    uint8_t hardware = mHardwareVertexProcessing;
    IDirect3DVertexShader9 *native = nullptr;
    if (shader) {
        hardware = As<ifaces::cIShaderProgram>(shader)->GetDescription()[7];
        shader->QueryInterface(kVertexShaderId, reinterpret_cast<void **>(&native));
    }
    const uint32_t software = hardware == 0;
    if (mWanted.softwareVertexProcessing != software) {
        // Leaving software vertex processing: what is pending goes to the device first (constants, as Flush(0x47) sends).
        if (!software && mPending.Any()) Flush(0x47);
        mChanges.misc |= ChangeRecord::kSoftwareVertexProcessing;
        mWanted.softwareVertexProcessing = software;
        Report(mD3D->SetSoftwareVertexProcessing(software), 0xa44);
        ++mChangeCount;
    }
    if (mWanted.vertexShader != native) {
        mChanges.misc |= ChangeRecord::kVertexShader;
        mWanted.vertexShader = native;
        Report(mD3D->SetVertexShader(native), 0xa4e);
        ++mChangeCount;
    }
}

void cDeviceStateDX::SetPixelShader(cIGZUnknown *shader)
{
    IDirect3DPixelShader9 *native = nullptr;
    if (shader) shader->QueryInterface(kPixelShaderId, reinterpret_cast<void **>(&native));
    if (mWanted.pixelShader == native) return;
    mChanges.misc |= ChangeRecord::kPixelShader;
    mWanted.pixelShader = native;
    Report(mD3D->SetPixelShader(native), 0xa66);
    ++mChangeCount;
}

void cDeviceStateDX::SetVertexDeclaration(cIGZUnknown *declaration)
{
    IDirect3DVertexDeclaration9 *native = nullptr;
    if (declaration) declaration->QueryInterface(kDeclarationId, reinterpret_cast<void **>(&native));
    if (mWanted.declaration == native) return;
    mDeclarationChanged = 1;
    mWanted.declaration = native;
    Report(mD3D->SetVertexDeclaration(native), 0xa7d);
    ++mChangeCount;
}

void cDeviceStateDX::SetStreamSource(uint32_t stream, cIGZUnknown *buffer, uint32_t stride, uint32_t offset)
{
    IDirect3DVertexBuffer9 *native = nullptr;
    if (buffer) buffer->QueryInterface(kVertexBufferId, reinterpret_cast<void **>(&native));
    StreamSource &source = mWanted.streams[stream];
    if (source.buffer == native && source.stride == stride && source.offset == offset) return;
    mStreamChanges |= static_cast<uint16_t>(1u << (stream & 31));
    source.buffer = native;
    source.stride = stride;
    source.offset = offset;
    Report(mD3D->SetStreamSource(stream, native, offset, stride), 0xa9c);
    ++mChangeCount;
}

void cDeviceStateDX::SetIndices(cIGZUnknown *indices)
{
    IDirect3DIndexBuffer9 *native = nullptr;
    if (indices) indices->QueryInterface(kIndexBufferId, reinterpret_cast<void **>(&native));
    if (mWanted.indices == native) return;
    mIndicesChanged = 1;
    mWanted.indices = native;
    Report(mD3D->SetIndices(native), 0xab3);
    ++mChangeCount;
}

// The constant locks widen the group's pending range to [start, start + count) and hand out the registers to fill;
// Flush sends the range. Unsigned compares; the end is kept in 16 bits (vertex) or 8 bits (pixel).
float *cDeviceStateDX::LockVertexFloatConstants(uint32_t start, uint32_t count)
{
    mVertexFloatStart = std::min(start, mVertexFloatStart);
    const uint32_t end = start + count;
    if (mPending.vertexFloatEnd < end) mPending.vertexFloatEnd = static_cast<uint16_t>(end);
    ++mChangeCount;
    return mVertexFloats[start];
}
int32_t *cDeviceStateDX::LockVertexIntConstants(uint32_t start, uint32_t count)
{
    mVertexIntStart = std::min(start, mVertexIntStart);
    const uint32_t end = start + count;
    if (mPending.vertexIntEnd < end) mPending.vertexIntEnd = static_cast<uint16_t>(end);
    ++mChangeCount;
    return mVertexInts[start];
}
BOOL *cDeviceStateDX::LockVertexBoolConstants(uint32_t start, uint32_t count)
{
    mVertexBoolStart = std::min(start, mVertexBoolStart);
    const uint32_t end = start + count;
    if (mPending.vertexBoolEnd < end) mPending.vertexBoolEnd = static_cast<uint16_t>(end);
    ++mChangeCount;
    return &mVertexBools[start];
}
float *cDeviceStateDX::LockPixelFloatConstants(uint32_t start, uint32_t count)
{
    mPixelFloatStart = std::min(start, mPixelFloatStart);
    const uint32_t end = start + count;
    if (mPending.pixelFloatEnd < end) mPending.pixelFloatEnd = static_cast<uint8_t>(end);
    ++mChangeCount;
    return mPixelFloats[start];
}
int32_t *cDeviceStateDX::LockPixelIntConstants(uint32_t start, uint32_t count)
{
    mPixelIntStart = std::min(start, mPixelIntStart);
    const uint32_t end = start + count;
    if (mPending.pixelIntEnd < end) mPending.pixelIntEnd = static_cast<uint8_t>(end);
    ++mChangeCount;
    return mPixelInts[start];
}
BOOL *cDeviceStateDX::LockPixelBoolConstants(uint32_t start, uint32_t count)
{
    mPixelBoolStart = std::min(start, mPixelBoolStart);
    const uint32_t end = start + count;
    if (mPending.pixelBoolEnd < end) mPending.pixelBoolEnd = static_cast<uint8_t>(end);
    ++mChangeCount;
    return &mPixelBools[start];
}

// The projection is sent every time; only a changed one counts.
void cDeviceStateDX::SetProjection(const D3DMATRIX *matrix)
{
    const bool changed = CopyMatrixTransposed(&mWanted.projection, matrix);
    Report(mD3D->SetTransform(D3DTS_PROJECTION, &mWanted.projection), 0xb37);
    if (changed) ++mChangeCount;
}

// `transform` holds a 3x3 rotation at +4 (row by row) and a translation at +0x28: the rotation goes in transposed and
// the translation in the fourth row; the matrix's fourth column is left as it is. Compared as words.
void cDeviceStateDX::SetWorld(const void *transform)
{
    const uint32_t *words = static_cast<const uint32_t *>(transform);
    uint32_t *world = reinterpret_cast<uint32_t *>(&mWanted.world);
    uint32_t differ = 0;
    for (uint32_t row = 0; row < 3; ++row) {
        for (uint32_t column = 0; column < 3; ++column) {
            const uint32_t word = words[1 + 3 * row + column];
            differ |= world[4 * column + row] ^ word;
            world[4 * column + row] = word;
        }
        const uint32_t word = words[10 + row];
        differ |= world[12 + row] ^ word;
        world[12 + row] = word;
    }
    if (differ == 0) return;
    Report(mD3D->SetTransform(D3DTS_WORLD, &mWanted.world), 0xb45);
    ++mChangeCount;
}

// --- Non-virtual members ---------------------------------------------------------------------------------------------

bool cDeviceStateDX::CopyMatrixTransposed(D3DMATRIX *dst, const D3DMATRIX *src)
{
    uint32_t *to = reinterpret_cast<uint32_t *>(dst);
    const uint32_t *from = reinterpret_cast<const uint32_t *>(src);
    uint32_t differ = 0;
    for (uint32_t i = 0; i < 16; ++i) {
        const uint32_t at = 4 * (i % 4) + i / 4;
        const uint32_t word = from[i];
        differ |= to[at] ^ word;
        to[at] = word;
    }
    return differ != 0;
}

void cDeviceStateDX::Flush(uint32_t groups)
{
    if (mPending.targets && (groups & kFlushTargets)) {
        mPending.targets = 0;
        for (uint32_t i = 0; i < mRenderTargetCount; ++i) {
            IDirect3DSurface9 *target = mWanted.targets[i];
            if (mSentTargets[i] == target) continue;
            mSentTargets[i] = target;
            Report(mD3D->SetRenderTarget(i, target), 0x727);
            // A new target resets the device's viewport and scissor to the whole target: record that, resend ours.
            mSentViewport.X = 0;
            mSentViewport.Y = 0;
            mSentViewport.Width = mWanted.targetWidth;
            mSentViewport.Height = mWanted.targetHeight;
            mPending.viewport = 1;
            mSentScissor.left = 0;
            mSentScissor.top = 0;
            mSentScissor.right = static_cast<LONG>(mWanted.targetWidth);
            mSentScissor.bottom = static_cast<LONG>(mWanted.targetHeight);
            mPending.scissor = 1;
        }
        IDirect3DSurface9 *depth = mWanted.depth;
        if (mSentDepth != depth) {
            mSentDepth = depth;
            Report(mD3D->SetDepthStencilSurface(depth), 0x741);
        }
    }
    if (mPending.scissor && (groups & kFlushTargets)) {
        mPending.scissor = 0;
        const float width = Dimension(mWanted.targetWidth), height = Dimension(mWanted.targetHeight);
        RECT rect;
        rect.left = ToPixels(width, mWanted.scissor[0]);
        rect.top = ToPixels(height, mWanted.scissor[1]);
        rect.right = ToPixels(width, mWanted.scissor[2]);
        rect.bottom = ToPixels(height, mWanted.scissor[3]);
        const bool changed = std::memcmp(&rect, &mSentScissor, sizeof(rect)) != 0;
        mSentScissor = rect;
        if (changed) Report(mD3D->SetScissorRect(&mSentScissor), 0x75e);
        const uint32_t enable = mWanted.scissorEnable;
        if (mSentScissorEnable != enable) {
            mSentScissorEnable = enable;
            Report(mD3D->SetRenderState(D3DRS_SCISSORTESTENABLE, enable), 0x765);
        }
    }
    if (mPending.viewport && (groups & kFlushViewport)) {
        mPending.viewport = 0;
        const float width = Dimension(mWanted.targetWidth), height = Dimension(mWanted.targetHeight);
        int32_t left = ToPixels(width, mWanted.viewport[0]);
        int32_t top = ToPixels(height, mWanted.viewport[1]);
        int32_t right = ToPixels(width, mWanted.viewport[2]);
        int32_t bottom = ToPixels(height, mWanted.viewport[3]);
        if (left == right || top == bottom) { // empty: a one-pixel viewport at the origin
            left = 0;
            top = 0;
            right = 1;
            bottom = 1;
        }
        D3DVIEWPORT9 viewport;
        viewport.X = static_cast<DWORD>(left);
        viewport.Y = static_cast<DWORD>(top);
        viewport.Width = static_cast<DWORD>(right - left);
        viewport.Height = static_cast<DWORD>(bottom - top);
        std::memcpy(&viewport.MinZ, &mWanted.minZ, sizeof(float)); // depth range by its bits
        std::memcpy(&viewport.MaxZ, &mWanted.maxZ, sizeof(float));
        const bool changed = std::memcmp(&viewport, &mSentViewport, sizeof(viewport)) != 0;
        mSentViewport = viewport;
        if (changed) Report(mD3D->SetViewport(&mSentViewport), 0x791);
    }
    if (groups & kFlushConstants) {
        if (const uint32_t end = mPending.vertexFloatEnd) {
            const uint32_t start = mVertexFloatStart;
            mPending.vertexFloatEnd = 0;
            mVertexFloatStart = kNoStart;
            Report(mD3D->SetVertexShaderConstantF(start, mVertexFloats[start], end - start), 0x79f);
        }
        if (const uint32_t end = mPending.vertexIntEnd) {
            const uint32_t start = mVertexIntStart;
            mPending.vertexIntEnd = 0;
            mVertexIntStart = kNoStart;
            Report(mD3D->SetVertexShaderConstantI(start, mVertexInts[start], end - start), 0x7aa);
        }
        if (const uint32_t end = mPending.vertexBoolEnd) {
            const uint32_t start = mVertexBoolStart;
            mPending.vertexBoolEnd = 0;
            mVertexBoolStart = kNoStart;
            Report(mD3D->SetVertexShaderConstantB(start, &mVertexBools[start], end - start), 0x7b5);
        }
        if (const uint32_t end = mPending.pixelFloatEnd) {
            const uint32_t start = mPixelFloatStart;
            mPixelFloatStart = kNoStart;
            mPending.pixelFloatEnd = 0;
            Report(mD3D->SetPixelShaderConstantF(start, mPixelFloats[start], end - start), 0x7c0);
        }
        if (const uint32_t end = mPending.pixelIntEnd) {
            const uint32_t start = mPixelIntStart;
            mPixelIntStart = kNoStart;
            mPending.pixelIntEnd = 0;
            Report(mD3D->SetPixelShaderConstantI(start, mPixelInts[start], end - start), 0x7cb);
        }
        if (const uint32_t end = mPending.pixelBoolEnd) {
            const uint32_t start = mPixelBoolStart;
            mPixelBoolStart = kNoStart;
            mPending.pixelBoolEnd = 0;
            Report(mD3D->SetPixelShaderConstantB(start, &mPixelBools[start], end - start), 0x7d6);
        }
    }
}

void cDeviceStateDX::MarkTargetsDirty()
{
    mPending.targets = 1;
    mPending.viewport = 1;
    mPending.scissor = 1;
}

// The device's own back buffer (and depth) becomes the target: the device then has the whole surface as viewport and
// scissor, which is recorded; a target the device already had needs the two sent explicitly. Extra targets are unset.
void cDeviceStateDX::SetBackBufferTarget(IDirect3DSurface9 *surface, IDirect3DSurface9 *depth, uint32_t width,
                                         uint32_t height)
{
    mSentScissor.left = 0;
    mSentViewport.X = 0;
    mSentViewport.Width = width;
    mSentScissor.right = static_cast<LONG>(width);
    mSentViewport.Y = 0;
    mSentViewport.Height = height;
    mSentScissor.top = 0;
    mSentScissor.bottom = static_cast<LONG>(height);
    if (mSentTargets[0] == surface) {
        Report(mD3D->SetViewport(&mSentViewport), 0x6c6);
        Report(mD3D->SetScissorRect(&mSentScissor), 0x6c7);
    } else {
        mSentTargets[0] = surface;
        Report(mD3D->SetRenderTarget(0, surface), 0x6be);
    }
    for (uint32_t i = 1; i < mRenderTargetCount; ++i) {
        if (!mSentTargets[i]) continue;
        mSentTargets[i] = nullptr;
        Report(mD3D->SetRenderTarget(i, nullptr), 0x6cf);
    }
    if (mSentDepth != depth) {
        mSentDepth = depth;
        Report(mD3D->SetDepthStencilSurface(depth), 0x6d6);
    }
}

// Sends stage i's texture coordinates from set i, untransformed, and clipping off, without touching the wanted state
// (pending constants first); RestoreTexCoordStates sends the wanted values back.
void cDeviceStateDX::ResetTexCoordStates()
{
    if (mPending.Any()) Flush(7);
    for (uint32_t stage = 0; stage < 8; ++stage) {
        if (mWanted.stageStates[stage][kStageTexCoordIndex] != stage)
            Report(mD3D->SetTextureStageState(stage, D3DTSS_TEXCOORDINDEX, stage), 0x6f2);
        if (mWanted.stageStates[stage][kStageTransformFlags] != 0)
            Report(mD3D->SetTextureStageState(stage, D3DTSS_TEXTURETRANSFORMFLAGS, 0), 0x6f7);
    }
    if (mWanted.clipping != 0) Report(mD3D->SetRenderState(D3DRS_CLIPPING, 0), 0x6fd);
}

void cDeviceStateDX::RestoreTexCoordStates()
{
    for (uint32_t stage = 0; stage < 8; ++stage) {
        const uint32_t texCoordIndex = mWanted.stageStates[stage][kStageTexCoordIndex];
        if (texCoordIndex != stage)
            Report(mD3D->SetTextureStageState(stage, D3DTSS_TEXCOORDINDEX, texCoordIndex), 0x708);
        const uint32_t transformFlags = mWanted.stageStates[stage][kStageTransformFlags];
        if (transformFlags != 0)
            Report(mD3D->SetTextureStageState(stage, D3DTSS_TEXTURETRANSFORMFLAGS, transformFlags), 0x70d);
    }
    if (mWanted.clipping != 0) Report(mD3D->SetRenderState(D3DRS_CLIPPING, mWanted.clipping), 0x713);
}

void cDeviceStateDX::SetRenderState(uint32_t index, uint32_t value)
{
    if (mWanted.renderStates[index] == value) return;
    mWanted.renderStates[index] = value;
    Report(mD3D->SetRenderState(static_cast<D3DRENDERSTATETYPE>(RenderStateMap()[index]), value), 0x55c);
    mChanges.renderStates[static_cast<int32_t>(index) >> 5] |= 1u << (index & 31);
    ++mChangeCount;
}

// The three list setters run their body at least once: a count of 0 wraps (as the original).
void cDeviceStateDX::SetRenderStates(uint32_t count, const uint32_t *indices, const uint32_t *values)
{
    do {
        const uint32_t index = *indices++;
        const uint32_t value = *values++;
        if (mWanted.renderStates[index] != value) {
            mWanted.renderStates[index] = value;
            Report(mD3D->SetRenderState(static_cast<D3DRENDERSTATETYPE>(RenderStateMap()[index]), value), 0x573);
            mChanges.renderStates[static_cast<int32_t>(index) >> 5] |= 1u << (index & 31);
            ++mChangeCount;
        }
    } while (--count != 0);
}

void cDeviceStateDX::SetTextureStageState(uint32_t stage, uint32_t index, uint32_t value)
{
    uint32_t &wanted = (&mWanted.stageStates[0][0])[stage * 0x12 + index];
    if (wanted == value) return;
    wanted = value;
    Report(mD3D->SetTextureStageState(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(StageStateMap()[index]), value), 0x589);
    mChanges.stages |= static_cast<uint16_t>(1u << (stage & 31));
    mChanges.stageStates[stage] |= 1u << (index & 31);
    ++mChangeCount;
}

void cDeviceStateDX::SetTextureStageStates(uint32_t stage, uint32_t count, const uint32_t *indices, const uint32_t *values)
{
    do {
        const uint32_t index = *indices++;
        const uint32_t value = *values++;
        uint32_t &wanted = (&mWanted.stageStates[0][0])[stage * 0x12 + index];
        if (wanted != value) {
            wanted = value;
            Report(mD3D->SetTextureStageState(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(StageStateMap()[index]), value),
                   0x5a1);
            mChanges.stages |= static_cast<uint16_t>(1u << (stage & 31));
            mChanges.stageStates[stage] |= 1u << (index & 31);
            ++mChangeCount;
        }
    } while (--count != 0);
}

void cDeviceStateDX::SetSamplerStates(uint32_t sampler, uint32_t count, const uint32_t *indices, const uint32_t *values)
{
    do {
        const uint32_t index = *indices++;
        const uint32_t value = *values++;
        uint32_t &wanted = (&mWanted.samplerStates[0][0])[sampler * 0xd + index];
        if (wanted != value) {
            wanted = value;
            Report(mD3D->SetSamplerState(sampler, static_cast<D3DSAMPLERSTATETYPE>(SamplerStateMap()[index]), value), 0x5d0);
            mChanges.samplers |= static_cast<uint16_t>(1u << (sampler & 31));
            mChanges.samplerStates[sampler] |= static_cast<uint16_t>(1u << (index & 31));
            ++mChangeCount;
        }
    } while (--count != 0);
}

void cDeviceStateDX::SetTextureTransform(uint32_t index, const D3DMATRIX *matrix)
{
    D3DMATRIX *wanted = &mWanted.textureTransforms[index];
    if (!CopyMatrixTransposed(wanted, matrix)) return;
    Report(mD3D->SetTransform(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_TEXTURE0 + index), wanted), 0x5e8);
    ++mChangeCount;
    mChanges.transforms |= static_cast<uint8_t>(1u << (index & 31));
}

void cDeviceStateDX::SetMaterial(const D3DMATERIAL9 *material)
{
    if (!CopyWords(&mWanted.material, material, sizeof(D3DMATERIAL9) / 4)) return;
    Report(mD3D->SetMaterial(&mWanted.material), 0x5f7);
    mChanges.misc |= ChangeRecord::kMaterial;
    ++mChangeCount;
}

void cDeviceStateDX::SetLight(uint32_t index, const D3DLIGHT9 *light)
{
    D3DLIGHT9 *wanted = &mWanted.lights[index];
    if (!CopyWords(wanted, light, sizeof(D3DLIGHT9) / 4)) return;
    Report(mD3D->SetLight(index, wanted), 0x6a7);
    ++mChangeCount;
}

// Lights 0..count-1 are enabled: the difference from the current count is switched; then the ambient colour.
void cDeviceStateDX::SetLightCount(uint32_t count, uint32_t ambient)
{
    const uint32_t enabled = mWanted.lightCount;
    if (enabled != count) {
        if (enabled < count) {
            for (uint32_t light = enabled; light < count; ++light) Report(mD3D->LightEnable(light, TRUE), 0x686);
        } else {
            for (uint32_t light = count; light < mWanted.lightCount; ++light) Report(mD3D->LightEnable(light, FALSE), 0x68d);
        }
        ++mChangeCount;
        mWanted.lightCount = count;
    }
    if (mWanted.ambient != ambient) {
        mWanted.ambient = ambient;
        Report(mD3D->SetRenderState(D3DRS_AMBIENT, ambient), 0x699);
        ++mChangeCount;
    }
}

void cDeviceStateDX::SetClipping(uint32_t clipping)
{
    if (mWanted.clipping == clipping) return;
    mWanted.clipping = clipping;
    Report(mD3D->SetRenderState(D3DRS_CLIPPING, clipping), 0x675);
    ++mChangeCount;
}

// Depth range, viewport and scissor are float compares (NaN never equal) and wait for Flush.
void cDeviceStateDX::SetDepthRange(float minZ, float maxZ)
{
    if (mWanted.minZ == minZ && mWanted.maxZ == maxZ) return;
    ++mChangeCount;
    mWanted.minZ = minZ;
    mWanted.maxZ = maxZ;
    mPending.viewport = 1;
}

void cDeviceStateDX::SetViewportFractions(float left, float top, float right, float bottom)
{
    float *viewport = mWanted.viewport;
    if (viewport[0] == left && viewport[1] == top && viewport[2] == right && viewport[3] == bottom) return;
    viewport[0] = left;
    viewport[1] = top;
    viewport[2] = right;
    viewport[3] = bottom;
    ++mChangeCount;
    mPending.viewport = 1;
}

void cDeviceStateDX::SetScissor(float left, float top, float right, float bottom, uint8_t enable)
{
    float *scissor = mWanted.scissor;
    if (static_cast<uint32_t>(mWanted.scissorEnable != 0) == enable && scissor[0] == left && scissor[1] == top &&
        scissor[2] == right && scissor[3] == bottom)
        return;
    scissor[0] = left;
    scissor[1] = top;
    scissor[2] = right;
    scissor[3] = bottom;
    ++mChangeCount;
    mWanted.scissorEnable = enable;
    mPending.scissor = 1;
}

// targets[0] must be set (asked without a null test); the size comes from it. Only the wanted state changes here.
void cDeviceStateDX::SetRenderTargets(cIGZUnknown *const *targets, cIGZUnknown *depth)
{
    IDirect3DSurface9 *first = nullptr;
    targets[0]->QueryInterface(kSurfaceId, reinterpret_cast<void **>(&first));
    if (mWanted.targets[0] != first) {
        mWanted.targets[0] = first;
        ifaces::cISurface *surface = As<ifaces::cISurface>(targets[0]);
        mWanted.targetWidth = surface->GetWidth();
        mWanted.targetHeight = surface->GetHeight();
        ++mChangeCount;
        mPending.targets = 1;
    }
    for (uint32_t i = 1; i < 4; ++i) {
        IDirect3DSurface9 *native = nullptr;
        if (targets[i]) targets[i]->QueryInterface(kSurfaceId, reinterpret_cast<void **>(&native));
        if (mWanted.targets[i] == native) continue;
        mWanted.targets[i] = native;
        ++mChangeCount;
        mPending.targets = 1;
    }
    IDirect3DSurface9 *nativeDepth = nullptr;
    if (depth) depth->QueryInterface(kSurfaceId, reinterpret_cast<void **>(&nativeDepth));
    if (mWanted.depth != nativeDepth) {
        ++mChangeCount;
        mWanted.depth = nativeDepth;
        mPending.targets = 1;
    }
}

// --- Adapters --------------------------------------------------------------------------------------------------------

cDeviceStateDX *__fastcall cDeviceStateDXAdapters::Construct(void *memory, void *, cDeviceDX *device, void *deviceEnum)
{
    return ::new (memory) cDeviceStateDX(device, deviceEnum);
}

} // namespace nGZGraphic4
