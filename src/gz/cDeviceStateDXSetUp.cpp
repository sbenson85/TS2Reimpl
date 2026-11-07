// nGZGraphic4::cDeviceStateDX::SetUpDevice (0x1807b0): the cache takes a new or reset Direct3D device. Called by our
// cDeviceDX::CreateDevice after every device creation or Reset, so the cache and the device agree from the first frame:
// the defaults are built once per object (the state tables' default values, then the default commands), and every time
// the back buffer and depth surface of the new device go into the defaults and the whole default state is sent.
#include <cstring>

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cDeviceEnum.h"
#include "gz/cDeviceStateDX.h"
#include "gz/cDeviceStateDXDetail.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGraphicSystemDX.h"
#include "rz/RZAutoRef.h"
#include "rz/Singleton_20ddfb.h" // GetServiceObject

namespace nGZGraphic4 {

using namespace devstate;
using namespace devstate_detail;

namespace {

constexpr uint32_t kGraphicServiceId = 0x85522803, kGraphicServiceIid = 0xc55289ba;
constexpr uint32_t kDefaultCommands = 33;

// The enumeration's limits (cIDeviceEnum::GetLimit) and processing caps SetUpDevice asks for.
constexpr uint32_t kCapHardwareVertexProcessing = 0, kCapPalettes = 8;
constexpr uint32_t kLimitRenderTargets = 1, kLimitStreamFrequency = 4, kLimitStreams = 10, kLimitLights = 0x19;

// The engine's stage state indices the stage defaults treat apart.
constexpr int32_t kStageColorOp = 0, kStageAlphaOp = 4, kStageTexCoordIndex = 10;

// The render states the engine has no index for, sent after the defaults with their source lines.
struct FixedState {
    D3DRENDERSTATETYPE state;
    uint32_t value;
    uint32_t line;
};
constexpr FixedState kFixedStates[] = {
    {D3DRS_DITHERENABLE, 0, 0x4cc},                {D3DRS_VERTEXBLEND, 0, 0x4cd},
    {D3DRS_POINTSIZE, 0x3f800000u, 0x4ce},         {D3DRS_POINTSCALEENABLE, 0, 0x4cf},
    {D3DRS_POINTSCALE_A, 0x3f800000u, 0x4d0},      {D3DRS_POINTSCALE_B, 0, 0x4d1},
    {D3DRS_POINTSCALE_C, 0, 0x4d2},                {D3DRS_INDEXEDVERTEXBLENDENABLE, 0, 0x4d3},
    {D3DRS_TWEENFACTOR, 0, 0x4d4},                 {D3DRS_ANTIALIASEDLINEENABLE, 0, 0x4d5},
};
static_assert(D3DRS_DITHERENABLE == 0x1a && D3DRS_VERTEXBLEND == 0x97 && D3DRS_POINTSIZE == 0x9a, "d3d9 values");
static_assert(D3DRS_POINTSCALEENABLE == 0x9d && D3DRS_POINTSCALE_A == 0x9e && D3DRS_POINTSCALE_C == 0xa0, "d3d9 values");
static_assert(D3DRS_INDEXEDVERTEXBLENDENABLE == 0xa7 && D3DRS_TWEENFACTOR == 0xaa, "d3d9 values");
static_assert(D3DRS_ANTIALIASEDLINEENABLE == 0xb0 && D3DRS_CLIPPING == 0x88 && D3DRS_AMBIENT == 0x8b, "d3d9 values");

template <class T> void ReleaseHeld(T *&object)
{
    if (object) {
        object->Release();
        object = nullptr;
    }
}

} // namespace

void cDeviceStateDX::SetUpDevice(IDirect3DDevice9 *device)
{
    cIDeviceEnum *limits = static_cast<cIDeviceEnum *>(mDeviceEnum);
    mD3D = device;
    mHardwareVertexProcessing = limits->HasProcessingCap(kCapHardwareVertexProcessing);
    mRenderTargetCount = static_cast<uint8_t>(limits->GetLimit(kLimitRenderTargets));

    // The behaviour flags are read even when the call fails (the original's local, uninitialised then).
    D3DDEVICE_CREATION_PARAMETERS parameters;
    Report(mD3D->GetCreationParameters(&parameters), 0x42f);
    const uint32_t software = (parameters.BehaviorFlags & (D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MIXED_VERTEXPROCESSING)) != 0;
    Report(mD3D->SetSoftwareVertexProcessing(software), 0x434);

    if (!mDefaultsBuilt) {
        const StateName *names = RenderStateNames();
        for (uint32_t i = 0; i < 0x59; ++i) mWanted.renderStates[names[i].index] = names[i].defaultValue;
        names = StageStateNames();
        for (uint32_t stage = 0; stage < 8; ++stage)
            for (uint32_t k = 0; k < 0x12; ++k) {
                const int32_t index = names[k].index;
                uint32_t value = names[k].defaultValue;
                if (stage == 0 && index == kStageColorOp) value = D3DTOP_MODULATE;
                else if (stage == 0 && index == kStageAlphaOp) value = D3DTOP_SELECTARG2;
                else if (index == kStageTexCoordIndex) value = stage;
                mWanted.stageStates[stage][index] = value;
            }
        names = SamplerStateNames();
        for (uint32_t sampler = 0; sampler < 16; ++sampler)
            for (uint32_t k = 0; k < 0xd; ++k) mWanted.samplerStates[sampler][names[k].index] = names[k].defaultValue;
        const D3DCAPS9 *caps = limits->GetCaps(software == 0);
        for (uint32_t id = 0; id < kDefaultCommands; ++id) ApplyDefaultCommand(id, caps);
        std::memcpy(&mDefaults, &mWanted, sizeof(mDefaults));
        mDefaultsBuilt = 1;
    }
    mDefaults.softwareVertexProcessing = (parameters.BehaviorFlags & D3DCREATE_SOFTWARE_VERTEXPROCESSING) ? 1 : 0;

    // The device's surfaces. The original reads the present parameters and the description even when the calls fail
    // (and would fault on a null back buffer: the device always has one).
    IDirect3DSwapChain9 *swapChain = nullptr;
    Report(mD3D->GetSwapChain(0, &swapChain), 0x48e);
    D3DPRESENT_PARAMETERS present;
    Report(swapChain->GetPresentParameters(&present), 0x491);
    mDefaults.renderStates[0x2d] = present.EnableAutoDepthStencil; // depth test on when the device has a depth buffer
    IDirect3DSurface9 *backBuffer = nullptr;
    Report(mD3D->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer), 0x495);
    mDefaults.targets[0] = backBuffer;
    D3DSURFACE_DESC desc;
    Report(backBuffer->GetDesc(&desc), 0x49c);
    mDefaults.targetWidth = desc.Width;
    mDefaults.targetHeight = desc.Height;
    IDirect3DSurface9 *depth = nullptr;
    Report(mD3D->GetDepthStencilSurface(&depth), 0x4a1);
    mDefaults.depth = depth;

    // What the device has now: nothing pending, the back buffer, the whole target.
    std::memset(&mPending, 0, sizeof(mPending));
    mVertexFloatStart = mVertexIntStart = mVertexBoolStart = kNoStart;
    mPixelFloatStart = mPixelIntStart = mPixelBoolStart = kNoStart;
    for (IDirect3DSurface9 *&target : mSentTargets) target = nullptr;
    mSentTargets[0] = backBuffer;
    mSentDepth = depth;
    mSentViewport = {0, 0, desc.Width, desc.Height, 0.0f, 1.0f};
    mSentScissor = {0, 0, static_cast<LONG>(desc.Width), static_cast<LONG>(desc.Height)};
    mSentScissorEnable = 0;

    // The whole default state.
    const uint16_t *renderMap = RenderStateMap();
    for (uint32_t i = 0; i < 0x59; ++i)
        Report(mD3D->SetRenderState(static_cast<D3DRENDERSTATETYPE>(renderMap[i]), mDefaults.renderStates[i]), 0x4c9);
    for (const FixedState &fixed : kFixedStates) Report(mD3D->SetRenderState(fixed.state, fixed.value), fixed.line);
    Report(mD3D->SetRenderState(D3DRS_CLIPPING, mDefaults.clipping), 0x4d6);
    Report(mD3D->SetRenderState(D3DRS_AMBIENT, mDefaults.ambient), 0x4d7);
    const uint8_t *stageMap = StageStateMap();
    for (uint32_t stage = 0; stage < 8; ++stage)
        for (uint32_t k = 0; k < 0x12; ++k)
            Report(mD3D->SetTextureStageState(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(stageMap[k]),
                                              mDefaults.stageStates[stage][k]),
                   0x4de);
    const uint8_t *samplerMap = SamplerStateMap();
    for (uint32_t sampler = 0; sampler < 16; ++sampler)
        for (uint32_t k = 0; k < 0xd; ++k)
            Report(mD3D->SetSamplerState(sampler, static_cast<D3DSAMPLERSTATETYPE>(samplerMap[k]),
                                         mDefaults.samplerStates[sampler][k]),
                   0x4e7);
    for (uint32_t i = 0; i < 16; ++i) Report(mD3D->SetTexture(i, mDefaults.textures[i]), 0x4ed);
    for (uint32_t i = 0; i < 5; ++i) Report(mD3D->SetTexture(D3DDMAPSAMPLER + i, nullptr), 0x4f0 + i);
    Report(mD3D->SetNPatchMode(0.0f), 0x4f6);
    Report(mD3D->SetVertexShader(mDefaults.vertexShader), 0x4f8);
    Report(mD3D->SetPixelShader(mDefaults.pixelShader), 0x4f9);
    Report(mD3D->SetMaterial(&mDefaults.material), 0x4fa);
    Report(mD3D->SetVertexDeclaration(mDefaults.declaration), 0x4fc);
    const int32_t streams = static_cast<int32_t>(limits->GetLimit(kLimitStreams + software));
    for (int32_t i = 0; i < streams; ++i) {
        const StreamSource &stream = mDefaults.streams[i];
        Report(mD3D->SetStreamSource(i, stream.buffer, stream.offset, stream.stride), 0x503);
        if (limits->GetLimit(kLimitStreamFrequency) >= 3) Report(mD3D->SetStreamSourceFreq(i, 1), 0x50c);
    }
    Report(mD3D->SetIndices(mDefaults.indices), 0x510);
    for (uint32_t i = 0; i < 8; ++i)
        Report(mD3D->SetTransform(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_TEXTURE0 + i), &mDefaults.textureTransforms[i]),
               0x514);
    const D3DMATRIX identity = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
    D3DMATRIX sent;
    CopyMatrixTransposed(&sent, &identity);
    Report(mD3D->SetTransform(D3DTS_VIEW, &sent), 0x519);
    Report(mD3D->SetTransform(D3DTS_PROJECTION, &sent), 0x51a);
    Report(mD3D->SetTransform(D3DTS_WORLD, &sent), 0x51b);
    const uint32_t lights = limits->GetLimit(kLimitLights + software);
    for (uint32_t i = 0; i < lights; ++i) {
        Report(mD3D->SetLight(i, &mDefaults.lights[i]), 0x520);
        Report(mD3D->LightEnable(i, FALSE), 0x521);
    }
    Reset(kFlushAll);
    Flush(kFlushAll);

    if (limits->HasProcessingCap(kCapPalettes)) {
        RZAutoRef graphics{nullptr};
        if (void *const framework = GetServiceObject())
            As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kGraphicServiceId, kGraphicServiceIid, &graphics.mObject);
        // ORIGINAL-BUG: the graphic service is used without a check; it always exists while a device does.
        const auto *palette = static_cast<const PALETTEENTRY *>(As<ifaces::cIGraphicSystemDX>(graphics.mObject)->GetPalette());
        const HRESULT hr = mD3D->SetPaletteEntries(0, palette);
        graphics.ReleaseObject();
        Report(hr, 0x530);
        Report(mD3D->SetCurrentTexturePalette(0), 0x531);
    }
    ReleaseHeld(depth);
    ReleaseHeld(backBuffer);
    ReleaseHeld(swapChain);
}

} // namespace nGZGraphic4
