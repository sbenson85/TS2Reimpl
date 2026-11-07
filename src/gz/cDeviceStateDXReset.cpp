// nGZGraphic4::cDeviceStateDX::Reset (slot 9, 0x17e070): puts the cache and the device back to the defaults for the
// groups asked for, after a device reset or mode change (cDeviceDX::Reset / ResetMode, cDraw2D). The groups:
//   1 states (textures, shaders, render / stage / sampler states, texture transforms, material, the held group-1 block)
//   2 streams (declaration, streams, indices, the held group-2 block)   4 constant registers (cleared, all pending)
//   8 render targets and scissor (copied from the defaults, pending)    0x10 viewport and depth range, clipping
//   0x20 projection and world transforms                                0x40 ambient, lights
// Groups 2, 4 and 0x40 first make the device's vertex processing mode that the creation parameters name (a mixed or
// software device) and the limits asked from the device enumerator are those of that mode; at the end the mode goes
// back to what the wanted state (group 1) or the device had before.
#include <cstring>

#include "game/game.h"
#include "gz/cDeviceEnum.h"
#include "gz/cDeviceStateDX.h"
#include "gz/cDeviceStateDXDetail.h"
#include "gz/cStateBlockDX.h"

namespace nGZGraphic4 {

using namespace devstate;
using namespace devstate_detail;

namespace {

// GZFillDwords (0x1b2d0a): `count` zero dwords.
void ZeroDwords(void *p, uint32_t count) { std::memset(p, 0, size_t(count) * 4); }

} // namespace

void cDeviceStateDX::Reset(uint32_t flags)
{
    const bool softwareBefore = mD3D->GetSoftwareVertexProcessing() != 0;
    bool software = softwareBefore;
    if (flags & 0x46) {
        D3DDEVICE_CREATION_PARAMETERS parameters = {};
        Report(mD3D->GetCreationParameters(&parameters), 0x844);
        software = (parameters.BehaviorFlags & 0xa0) != 0; // software or mixed vertex processing
        Report(mD3D->SetSoftwareVertexProcessing(software), 0x847);
    }
    // The limits of the mode (hardware and software ones follow each other).
    cIDeviceEnum *const limits = static_cast<cIDeviceEnum *>(mDeviceEnum);
    const uint32_t mode = software ? 1 : 0;

    if (flags & 1) {
        for (uint32_t i = 0; i < 16; ++i) {
            if (mWanted.textures[i] == nullptr) continue;
            mWanted.textures[i] = nullptr;
            Report(mD3D->SetTexture(i, nullptr), 0x853);
        }
        mWanted.softwareVertexProcessing = mDefaults.softwareVertexProcessing;
        if (mWanted.vertexShader != nullptr) {
            mWanted.vertexShader = nullptr;
            Report(mD3D->SetVertexShader(nullptr), 0x860);
        }
        if (mWanted.pixelShader != nullptr) {
            mWanted.pixelShader = nullptr;
            Report(mD3D->SetPixelShader(nullptr), 0x867);
        }
        for (uint32_t i = 0; i < 0x59; ++i) {
            if (mWanted.renderStates[i] == mDefaults.renderStates[i]) continue;
            mWanted.renderStates[i] = mDefaults.renderStates[i];
            Report(mD3D->SetRenderState(static_cast<D3DRENDERSTATETYPE>(RenderStateMap()[i]), mWanted.renderStates[i]), 0x872);
        }
        for (uint32_t stage = 0; stage < 8; ++stage) {
            for (uint32_t k = 0; k < 0x12; ++k) {
                if (mWanted.stageStates[stage][k] == mDefaults.stageStates[stage][k]) continue;
                mWanted.stageStates[stage][k] = mDefaults.stageStates[stage][k];
                Report(mD3D->SetTextureStageState(stage, static_cast<D3DTEXTURESTAGESTATETYPE>(StageStateMap()[k]),
                                                  mWanted.stageStates[stage][k]),
                       0x880);
            }
        }
        for (uint32_t sampler = 0; sampler < 16; ++sampler) {
            for (uint32_t k = 0; k < 0xd; ++k) {
                if (mWanted.samplerStates[sampler][k] == mDefaults.samplerStates[sampler][k]) continue;
                mWanted.samplerStates[sampler][k] = mDefaults.samplerStates[sampler][k];
                Report(mD3D->SetSamplerState(sampler, static_cast<D3DSAMPLERSTATETYPE>(SamplerStateMap()[k]),
                                             mWanted.samplerStates[sampler][k]),
                       0x88f);
            }
        }
        for (uint32_t t = 0; t < 8; ++t) {
            if (CopyWords(&mWanted.textureTransforms[t], &mDefaults.textureTransforms[t], 16))
                Report(mD3D->SetTransform(static_cast<D3DTRANSFORMSTATETYPE>(D3DTS_TEXTURE0 + t), &mWanted.textureTransforms[t]),
                       0x899);
        }
        if (CopyWords(&mWanted.material, &mDefaults.material, sizeof(D3DMATERIAL9) / 4))
            Report(mD3D->SetMaterial(&mWanted.material), 0x8a0);
        if (mStateBlock98 != nullptr) {
            mStateBlock98->Release();
            mStateBlock98 = nullptr;
        }
        std::memset(&mChanges, 0, sizeof(mChanges));
    }

    if (flags & 2) {
        if (mWanted.declaration != nullptr) {
            mWanted.declaration = nullptr;
            Report(mD3D->SetVertexDeclaration(nullptr), 0x8b4);
        }
        const int32_t streams = static_cast<int32_t>(limits->GetLimit(10 + mode));
        for (int32_t i = 0; i < streams; ++i) {
            StreamSource &stream = mWanted.streams[i];
            if (stream.buffer == nullptr && stream.offset == 0 && stream.stride == 0) continue;
            stream.buffer = nullptr;
            stream.stride = 0;
            stream.offset = 0;
            Report(mD3D->SetStreamSource(static_cast<UINT>(i), nullptr, 0, 0), 0x8c4);
        }
        if (mWanted.indices != nullptr) {
            mWanted.indices = nullptr;
            Report(mD3D->SetIndices(nullptr), 0x8cd);
        }
        if (mStateBlock9c != nullptr) {
            mStateBlock9c->Release();
            mStateBlock9c = nullptr;
        }
        const uint32_t none = 0;
        std::memcpy(reinterpret_cast<uint8_t *>(this) + 0x70, &none, 4);
    }

    if (flags & 4) {
        const uint32_t vertexFloats = limits->GetLimit(0x10 + mode);
        const uint32_t vertexInts = limits->GetLimit(0x12 + mode);
        const uint32_t vertexBools = limits->GetLimit(0x14 + mode);
        ZeroDwords(mVertexFloats, vertexFloats * 4);
        ZeroDwords(mVertexInts, vertexInts * 4);
        ZeroDwords(mVertexBools, vertexBools);
        mVertexFloatStart = 0;
        mPending.vertexFloatEnd = static_cast<uint16_t>(vertexFloats);
        mVertexIntStart = 0;
        // ORIGINAL-BUG: the pending ends of the integer and the boolean registers are both the boolean count (the integer
        // count is asked for and used for the clearing, not for the end).
        mPending.vertexIntEnd = static_cast<uint16_t>(vertexBools);
        mVertexBoolStart = 0;
        mPending.vertexBoolEnd = static_cast<uint16_t>(vertexBools);
        const uint32_t pixelFloats = limits->GetLimit(0x16);
        const uint32_t pixelInts = limits->GetLimit(0x17);
        const uint32_t pixelBools = limits->GetLimit(0x18);
        ZeroDwords(mPixelFloats, pixelFloats * 4);
        ZeroDwords(mPixelInts, pixelInts * 4);
        ZeroDwords(mPixelBools, pixelBools);
        // ORIGINAL-BUG: as above, the pixel integer end is the boolean count.
        mPending.pixelIntEnd = static_cast<uint8_t>(pixelBools);
        mPending.pixelBoolEnd = static_cast<uint8_t>(pixelBools);
        mPixelFloatStart = 0;
        mPending.pixelFloatEnd = static_cast<uint8_t>(pixelFloats);
        mPixelIntStart = 0;
        mPixelBoolStart = 0;
    }

    if (flags & 8) {
        // Targets, depth, the target's size, the scissor rectangle and its enable.
        std::memcpy(&mWanted.targets, &mDefaults.targets, offsetof(DeviceState, viewport) - offsetof(DeviceState, targets));
        mPending.targets = 1;
        mPending.scissor = 1;
    }

    if (flags & 0x10) {
        // Viewport and depth range.
        std::memcpy(&mWanted.viewport, &mDefaults.viewport, offsetof(DeviceState, clipping) - offsetof(DeviceState, viewport));
        mPending.viewport = 1;
        if (mWanted.clipping != mDefaults.clipping) {
            mWanted.clipping = mDefaults.clipping;
            Report(mD3D->SetRenderState(D3DRS_CLIPPING, mWanted.clipping), 0x923);
        }
    }

    if (flags & 0x20) {
        std::memcpy(&mWanted.projection, &mDefaults.projection, sizeof(D3DMATRIX));
        Report(mD3D->SetTransform(D3DTS_PROJECTION, &mWanted.projection), 0x92f);
        if (CopyWords(&mWanted.world, &mDefaults.world, 16)) Report(mD3D->SetTransform(D3DTS_WORLD, &mWanted.world), 0x933);
    }

    if (flags & 0x40) {
        mWanted.lightCount = 0;
        if (mWanted.ambient != mDefaults.ambient) {
            mWanted.ambient = mDefaults.ambient;
            Report(mD3D->SetRenderState(D3DRS_AMBIENT, mWanted.ambient), 0x940);
        }
        const uint32_t lights = limits->GetLimit(0x19 + mode);
        for (uint32_t i = 0; i < lights; ++i) {
            if (CopyWords(&mWanted.lights[i], &mDefaults.lights[i], sizeof(D3DLIGHT9) / 4))
                Report(mD3D->SetLight(i, &mWanted.lights[i]), 0x948);
            Report(mD3D->LightEnable(i, FALSE), 0x94b);
        }
    }

    // Back to the mode the wanted state names (or the device had), flushing what is pending before leaving software.
    const bool softwareAfter = (flags & 1) ? mWanted.softwareVertexProcessing != 0 : softwareBefore;
    if (softwareAfter != software) {
        if (software && mPending.Any()) Flush(0x47);
        Report(mD3D->SetSoftwareVertexProcessing(softwareAfter), 0x95c);
    }
    ++mChangeCount;
}

} // namespace nGZGraphic4
