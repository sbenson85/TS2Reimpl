// The device call log's wrappers (host/shared/d3dlog.h): IDirect3D9 and IDirect3DDevice9, forwarding every call to
// the real object and logging it. Methods follow d3d9.h's order (the vtable's).
#include "d3dlog_internal.h"

#include <cstdio>
#include <cstring>

namespace d3dlog {

namespace {

using devlog::Kind;

constexpr Kind kStream = Kind::Stream, kAsync = Kind::Async;

template <typename... T> uint64_t ParamHash(T... values)
{
    const uint32_t v[] = {static_cast<uint32_t>(values)...};
    return devlog::Hash(v, sizeof(v));
}

class LoggedDirect3D;

class LoggedDevice final : public IDirect3DDevice9 {
public:
    LoggedDevice(IDirect3DDevice9 *real, LoggedDirect3D *parent, devlog::Recorder &recorder);

    // ---- IUnknown ----
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override
    {
        if (object && (riid == IID_IUnknown || riid == IID_IDirect3DDevice9)) {
            *object = this;
            AddRef();
            return S_OK;
        }
        return mReal->QueryInterface(riid, object);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return mReal->AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override;

    // ---- IDirect3DDevice9 ----
    HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override
    {
        const HRESULT hr = mReal->TestCooperativeLevel();
        Log(Call("TestCooperativeLevel").Hr(hr));
        return hr;
    }
    UINT STDMETHODCALLTYPE GetAvailableTextureMem() override
    {
        Log(Call("GetAvailableTextureMem")); // the answer is the machine's
        return mReal->GetAvailableTextureMem();
    }
    HRESULT STDMETHODCALLTYPE EvictManagedResources() override
    {
        const HRESULT hr = mReal->EvictManagedResources();
        Log(Call("EvictManagedResources").Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9 **d3d) override;
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9 *caps) override
    {
        const HRESULT hr = mReal->GetDeviceCaps(caps);
        Log(Call("GetDeviceCaps").Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetDisplayMode(UINT swapChain, D3DDISPLAYMODE *mode) override
    {
        const HRESULT hr = mReal->GetDisplayMode(swapChain, mode);
        Log(Call("GetDisplayMode").U(swapChain).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *parameters) override
    {
        const HRESULT hr = mReal->GetCreationParameters(parameters);
        Log(Call("GetCreationParameters").Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT x, UINT y, IDirect3DSurface9 *bitmap) override
    {
        const HRESULT hr = mReal->SetCursorProperties(x, y, bitmap);
        Log(Call("SetCursorProperties").U(x).U(y).S(mTracker.Name(bitmap, 'S')).Hr(hr));
        return hr;
    }
    void STDMETHODCALLTYPE SetCursorPosition(int x, int y, DWORD flags) override
    {
        mReal->SetCursorPosition(x, y, flags);
        Log(Call("SetCursorPosition").I(x).I(y).X(flags));
    }
    BOOL STDMETHODCALLTYPE ShowCursor(BOOL show) override
    {
        const BOOL was = mReal->ShowCursor(show);
        Log(Call("ShowCursor").U(show).U(was));
        return was;
    }
    HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *pp, IDirect3DSwapChain9 **swapChain) override
    {
        const HRESULT hr = mReal->CreateAdditionalSwapChain(pp, swapChain);
        Log(Call("CreateAdditionalSwapChain").Present(pp).Hr(hr), kAsync);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetSwapChain(UINT index, IDirect3DSwapChain9 **swapChain) override
    {
        const HRESULT hr = mReal->GetSwapChain(index, swapChain);
        Log(Call("GetSwapChain").U(index).Hr(hr));
        return hr;
    }
    UINT STDMETHODCALLTYPE GetNumberOfSwapChains() override
    {
        const UINT n = mReal->GetNumberOfSwapChains();
        Log(Call("GetNumberOfSwapChains").U(n));
        return n;
    }
    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS *pp) override
    {
        const HRESULT hr = mReal->Reset(pp);
        for (StreamBinding &s : mTracker.streams) s = StreamBinding{};
        mTracker.indices = nullptr;
        Log(Call("Reset").Present(pp).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Present(const RECT *source, const RECT *dest, HWND window, const RGNDATA *dirty) override
    {
        const HRESULT hr = mReal->Present(source, dest, window, dirty);
        Log(Call("Present").Rect(source).Rect(dest).U(window != nullptr).U(dirty != nullptr).Hr(hr), kStream, false, true);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT swapChain, UINT index, D3DBACKBUFFER_TYPE type, IDirect3DSurface9 **surface) override
    {
        const HRESULT hr = mReal->GetBackBuffer(swapChain, index, type, surface);
        if (SUCCEEDED(hr) && surface && *surface) mTracker.RegisterSurface(*surface, 'B', swapChain * 16 + index);
        Log(Call("GetBackBuffer").U(swapChain).U(index).U(type).S(surface ? mTracker.Name(*surface, 'S') : "null").Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetRasterStatus(UINT swapChain, D3DRASTER_STATUS *status) override
    {
        const HRESULT hr = mReal->GetRasterStatus(swapChain, status);
        Log(Call("GetRasterStatus").U(swapChain).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetDialogBoxMode(BOOL enable) override
    {
        const HRESULT hr = mReal->SetDialogBoxMode(enable);
        Log(Call("SetDialogBoxMode").U(enable).Hr(hr));
        return hr;
    }
    void STDMETHODCALLTYPE SetGammaRamp(UINT swapChain, DWORD flags, const D3DGAMMARAMP *ramp) override
    {
        mReal->SetGammaRamp(swapChain, flags, ramp);
        Log(Call("SetGammaRamp").U(swapChain).X(flags).Bytes(ramp, sizeof(D3DGAMMARAMP)));
    }
    void STDMETHODCALLTYPE GetGammaRamp(UINT swapChain, D3DGAMMARAMP *ramp) override
    {
        mReal->GetGammaRamp(swapChain, ramp);
        Log(Call("GetGammaRamp").U(swapChain));
    }
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                                            IDirect3DTexture9 **texture, HANDLE *shared) override
    {
        const HRESULT hr = mReal->CreateTexture(width, height, levels, usage, format, pool, texture, shared);
        if (SUCCEEDED(hr) && texture)
            mTracker.RegisterTexture(*texture, ParamHash('T', width, height, levels, usage, format, pool), (usage & D3DUSAGE_DYNAMIC) != 0);
        Log(Call("CreateTexture").U(width).U(height).U(levels).X(usage).U(format).U(pool).S(Out(texture, 'T')).U(shared != nullptr).Hr(hr),
            kAsync);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT width, UINT height, UINT depth, UINT levels, DWORD usage, D3DFORMAT format,
                                                  D3DPOOL pool, IDirect3DVolumeTexture9 **texture, HANDLE *shared) override
    {
        const HRESULT hr = mReal->CreateVolumeTexture(width, height, depth, levels, usage, format, pool, texture, shared);
        if (SUCCEEDED(hr) && texture) mTracker.Register(*texture, 'U', ParamHash('U', width, height, depth, levels, usage, format, pool));
        Log(Call("CreateVolumeTexture").U(width).U(height).U(depth).U(levels).X(usage).U(format).U(pool).S(Out(texture, 'T'))
                .U(shared != nullptr).Hr(hr),
            kAsync);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT edge, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
                                                IDirect3DCubeTexture9 **texture, HANDLE *shared) override
    {
        const HRESULT hr = mReal->CreateCubeTexture(edge, levels, usage, format, pool, texture, shared);
        if (SUCCEEDED(hr) && texture)
            mTracker.RegisterCube(*texture, ParamHash('C', edge, levels, usage, format, pool), (usage & D3DUSAGE_DYNAMIC) != 0);
        Log(Call("CreateCubeTexture").U(edge).U(levels).X(usage).U(format).U(pool).S(Out(texture, 'T')).U(shared != nullptr).Hr(hr),
            kAsync);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT length, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer9 **buffer,
                                                 HANDLE *shared) override
    {
        const HRESULT hr = mReal->CreateVertexBuffer(length, usage, fvf, pool, buffer, shared);
        if (SUCCEEDED(hr) && buffer)
            mTracker.RegisterBuffer(*buffer, 'V', ParamHash('V', length, usage, fvf, pool), (usage & D3DUSAGE_DYNAMIC) != 0, length, 0);
        Log(Call("CreateVertexBuffer").U(length).X(usage).X(fvf).U(pool).S(Out(buffer, 'V')).U(shared != nullptr).Hr(hr), kAsync);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DIndexBuffer9 **buffer,
                                                HANDLE *shared) override
    {
        const HRESULT hr = mReal->CreateIndexBuffer(length, usage, format, pool, buffer, shared);
        if (SUCCEEDED(hr) && buffer)
            mTracker.RegisterBuffer(*buffer, 'I', ParamHash('I', length, usage, format, pool), (usage & D3DUSAGE_DYNAMIC) != 0, length,
                                    format == D3DFMT_INDEX32 ? 4 : 2);
        Log(Call("CreateIndexBuffer").U(length).X(usage).U(format).U(pool).S(Out(buffer, 'I')).U(shared != nullptr).Hr(hr), kAsync);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multiSample, DWORD quality,
                                                 BOOL lockable, IDirect3DSurface9 **surface, HANDLE *shared) override
    {
        const HRESULT hr = mReal->CreateRenderTarget(width, height, format, multiSample, quality, lockable, surface, shared);
        if (SUCCEEDED(hr) && surface)
            mTracker.RegisterSurface(*surface, 'S', ParamHash('S', width, height, format, multiSample, quality, lockable));
        Log(Call("CreateRenderTarget").U(width).U(height).U(format).U(multiSample).U(quality).U(lockable).S(Out(surface, 'S'))
                .U(shared != nullptr).Hr(hr),
            kAsync);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multiSample,
                                                        DWORD quality, BOOL discard, IDirect3DSurface9 **surface, HANDLE *shared) override
    {
        const HRESULT hr = mReal->CreateDepthStencilSurface(width, height, format, multiSample, quality, discard, surface, shared);
        if (SUCCEEDED(hr) && surface)
            mTracker.RegisterSurface(*surface, 'Z', ParamHash('Z', width, height, format, multiSample, quality, discard));
        Log(Call("CreateDepthStencilSurface").U(width).U(height).U(format).U(multiSample).U(quality).U(discard).S(Out(surface, 'S'))
                .U(shared != nullptr).Hr(hr),
            kAsync);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9 *source, const RECT *sourceRect, IDirect3DSurface9 *dest,
                                            const POINT *destPoint) override
    {
        const HRESULT hr = mReal->UpdateSurface(source, sourceRect, dest, destPoint);
        Call call("UpdateSurface");
        call.S(mTracker.Name(source, 'S')).Rect(sourceRect);
        if (destPoint) call.I(destPoint->x).I(destPoint->y);
        else call.S("null");
        if (SUCCEEDED(hr)) mTracker.Derive(dest, 'S', devlog::Hash(call.Text().data(), call.Text().size()));
        call.S(mTracker.Name(dest, 'S')).Hr(hr);
        Log(call, kAsync);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9 *source, IDirect3DBaseTexture9 *dest) override
    {
        const HRESULT hr = mReal->UpdateTexture(source, dest);
        const std::string from = mTracker.Name(source, 'T');
        if (SUCCEEDED(hr)) mTracker.Derive(dest, 'T', devlog::Hash(from.data(), from.size()));
        Log(Call("UpdateTexture").S(from).S(mTracker.Name(dest, 'T')).Hr(hr), kAsync);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9 *target, IDirect3DSurface9 *dest) override
    {
        const HRESULT hr = mReal->GetRenderTargetData(target, dest);
        Log(Call("GetRenderTargetData").S(mTracker.Name(target, 'S')).S(mTracker.Name(dest, 'S')).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetFrontBufferData(UINT swapChain, IDirect3DSurface9 *dest) override
    {
        const HRESULT hr = mReal->GetFrontBufferData(swapChain, dest);
        Log(Call("GetFrontBufferData").U(swapChain).S(mTracker.Name(dest, 'S')).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9 *source, const RECT *sourceRect, IDirect3DSurface9 *dest, const RECT *destRect,
                                          D3DTEXTUREFILTERTYPE filter) override
    {
        const HRESULT hr = mReal->StretchRect(source, sourceRect, dest, destRect, filter);
        Log(Call("StretchRect").S(mTracker.Name(source, 'S')).Rect(sourceRect).S(mTracker.Name(dest, 'S')).Rect(destRect).U(filter).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9 *surface, const RECT *rect, D3DCOLOR color) override
    {
        const HRESULT hr = mReal->ColorFill(surface, rect, color);
        Log(Call("ColorFill").S(mTracker.Name(surface, 'S')).Rect(rect).X(color).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT width, UINT height, D3DFORMAT format, D3DPOOL pool, IDirect3DSurface9 **surface,
                                                          HANDLE *shared) override
    {
        const HRESULT hr = mReal->CreateOffscreenPlainSurface(width, height, format, pool, surface, shared);
        if (SUCCEEDED(hr) && surface) mTracker.RegisterSurface(*surface, 'O', ParamHash('O', width, height, format, pool));
        Log(Call("CreateOffscreenPlainSurface").U(width).U(height).U(format).U(pool).S(Out(surface, 'S')).U(shared != nullptr).Hr(hr),
            kAsync);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD index, IDirect3DSurface9 *target) override
    {
        const HRESULT hr = mReal->SetRenderTarget(index, target);
        Log(Call("SetRenderTarget").U(index).S(mTracker.Name(target, 'S')).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD index, IDirect3DSurface9 **target) override
    {
        const HRESULT hr = mReal->GetRenderTarget(index, target);
        Log(Call("GetRenderTarget").U(index).S(Out(target, 'S')).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9 *surface) override
    {
        const HRESULT hr = mReal->SetDepthStencilSurface(surface);
        Log(Call("SetDepthStencilSurface").S(mTracker.Name(surface, 'S')).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9 **surface) override
    {
        const HRESULT hr = mReal->GetDepthStencilSurface(surface);
        Log(Call("GetDepthStencilSurface").S(Out(surface, 'S')).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE BeginScene() override
    {
        const HRESULT hr = mReal->BeginScene();
        Log(Call("BeginScene").Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE EndScene() override
    {
        const HRESULT hr = mReal->EndScene();
        Log(Call("EndScene").Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Clear(DWORD count, const D3DRECT *rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil) override
    {
        const HRESULT hr = mReal->Clear(count, rects, flags, color, z, stencil);
        Call call("Clear");
        call.U(count);
        if (rects) call.Ints(reinterpret_cast<const int *>(rects), count * 4);
        else call.S("null");
        Log(call.X(flags).X(color).F(z).U(stencil).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix) override
    {
        const HRESULT hr = mReal->SetTransform(state, matrix);
        Log(Call("SetTransform").U(state).Floats(matrix ? &matrix->_11 : nullptr, 16).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix) override
    {
        const HRESULT hr = mReal->GetTransform(state, matrix);
        Log(Call("GetTransform").U(state).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix) override
    {
        const HRESULT hr = mReal->MultiplyTransform(state, matrix);
        Log(Call("MultiplyTransform").U(state).Floats(matrix ? &matrix->_11 : nullptr, 16).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9 *viewport) override
    {
        const HRESULT hr = mReal->SetViewport(viewport);
        Call call("SetViewport");
        if (viewport) call.U(viewport->X).U(viewport->Y).U(viewport->Width).U(viewport->Height).F(viewport->MinZ).F(viewport->MaxZ);
        else call.S("null");
        Log(call.Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9 *viewport) override
    {
        const HRESULT hr = mReal->GetViewport(viewport);
        Log(Call("GetViewport").Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetMaterial(const D3DMATERIAL9 *material) override
    {
        const HRESULT hr = mReal->SetMaterial(material);
        Log(Call("SetMaterial").Floats(material ? &material->Diffuse.r : nullptr, 17).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL9 *material) override
    {
        const HRESULT hr = mReal->GetMaterial(material);
        Log(Call("GetMaterial").Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetLight(DWORD index, const D3DLIGHT9 *light) override
    {
        const HRESULT hr = mReal->SetLight(index, light);
        Call call("SetLight");
        call.U(index);
        if (light) call.U(light->Type).Floats(&light->Diffuse.r, 25);
        else call.S("null");
        Log(call.Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetLight(DWORD index, D3DLIGHT9 *light) override
    {
        const HRESULT hr = mReal->GetLight(index, light);
        Log(Call("GetLight").U(index).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LightEnable(DWORD index, BOOL enable) override
    {
        const HRESULT hr = mReal->LightEnable(index, enable);
        Log(Call("LightEnable").U(index).U(enable).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD index, BOOL *enable) override
    {
        const HRESULT hr = mReal->GetLightEnable(index, enable);
        Log(Call("GetLightEnable").U(index).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD index, const float *plane) override
    {
        const HRESULT hr = mReal->SetClipPlane(index, plane);
        Log(Call("SetClipPlane").U(index).Floats(plane, 4).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD index, float *plane) override
    {
        const HRESULT hr = mReal->GetClipPlane(index, plane);
        Log(Call("GetClipPlane").U(index).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE state, DWORD value) override
    {
        const HRESULT hr = mReal->SetRenderState(state, value);
        Log(Call("SetRenderState").U(state).X(value).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE state, DWORD *value) override
    {
        const HRESULT hr = mReal->GetRenderState(state, value);
        Log(Call("GetRenderState").U(state).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE type, IDirect3DStateBlock9 **block) override
    {
        const HRESULT hr = mReal->CreateStateBlock(type, block);
        if (SUCCEEDED(hr) && block) mTracker.Register(*block, 'b', static_cast<uint32_t>(type));
        Log(Call("CreateStateBlock").U(type).S(Out(block, 'b')).Hr(hr), kAsync);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE BeginStateBlock() override
    {
        const HRESULT hr = mReal->BeginStateBlock();
        Log(Call("BeginStateBlock").Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE EndStateBlock(IDirect3DStateBlock9 **block) override
    {
        const HRESULT hr = mReal->EndStateBlock(block);
        if (SUCCEEDED(hr) && block) mTracker.Register(*block, 'b', 0);
        Log(Call("EndStateBlock").S(Out(block, 'b')).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetClipStatus(const D3DCLIPSTATUS9 *status) override
    {
        const HRESULT hr = mReal->SetClipStatus(status);
        Call call("SetClipStatus");
        if (status) call.X(status->ClipUnion).X(status->ClipIntersection);
        else call.S("null");
        Log(call.Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS9 *status) override
    {
        const HRESULT hr = mReal->GetClipStatus(status);
        Log(Call("GetClipStatus").Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD stage, IDirect3DBaseTexture9 **texture) override
    {
        const HRESULT hr = mReal->GetTexture(stage, texture);
        Log(Call("GetTexture").U(stage).S(Out(texture, 'T')).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage, IDirect3DBaseTexture9 *texture) override
    {
        const HRESULT hr = mReal->SetTexture(stage, texture);
        Log(Call("SetTexture").U(stage).S(mTracker.Name(texture, 'T')).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD *value) override
    {
        const HRESULT hr = mReal->GetTextureStageState(stage, type, value);
        Log(Call("GetTextureStageState").U(stage).U(type).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD value) override
    {
        const HRESULT hr = mReal->SetTextureStageState(stage, type, value);
        Log(Call("SetTextureStageState").U(stage).U(type).X(value).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD *value) override
    {
        const HRESULT hr = mReal->GetSamplerState(sampler, type, value);
        Log(Call("GetSamplerState").U(sampler).U(type).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value) override
    {
        const HRESULT hr = mReal->SetSamplerState(sampler, type, value);
        Log(Call("SetSamplerState").U(sampler).U(type).X(value).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD *passes) override
    {
        const HRESULT hr = mReal->ValidateDevice(passes);
        Log(Call("ValidateDevice").Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT palette, const PALETTEENTRY *entries) override
    {
        const HRESULT hr = mReal->SetPaletteEntries(palette, entries);
        Log(Call("SetPaletteEntries").U(palette).Bytes(entries, 256 * sizeof(PALETTEENTRY)).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT palette, PALETTEENTRY *entries) override
    {
        const HRESULT hr = mReal->GetPaletteEntries(palette, entries);
        Log(Call("GetPaletteEntries").U(palette).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT palette) override
    {
        const HRESULT hr = mReal->SetCurrentTexturePalette(palette);
        Log(Call("SetCurrentTexturePalette").U(palette).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT *palette) override
    {
        const HRESULT hr = mReal->GetCurrentTexturePalette(palette);
        Log(Call("GetCurrentTexturePalette").Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetScissorRect(const RECT *rect) override
    {
        const HRESULT hr = mReal->SetScissorRect(rect);
        Log(Call("SetScissorRect").Rect(rect).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetScissorRect(RECT *rect) override
    {
        const HRESULT hr = mReal->GetScissorRect(rect);
        Log(Call("GetScissorRect").Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetSoftwareVertexProcessing(BOOL software) override
    {
        const HRESULT hr = mReal->SetSoftwareVertexProcessing(software);
        Log(Call("SetSoftwareVertexProcessing").U(software).Hr(hr));
        return hr;
    }
    BOOL STDMETHODCALLTYPE GetSoftwareVertexProcessing() override
    {
        const BOOL v = mReal->GetSoftwareVertexProcessing();
        Log(Call("GetSoftwareVertexProcessing").U(v));
        return v;
    }
    HRESULT STDMETHODCALLTYPE SetNPatchMode(float segments) override
    {
        const HRESULT hr = mReal->SetNPatchMode(segments);
        Log(Call("SetNPatchMode").F(segments).Hr(hr));
        return hr;
    }
    float STDMETHODCALLTYPE GetNPatchMode() override
    {
        const float v = mReal->GetNPatchMode();
        Log(Call("GetNPatchMode").F(v));
        return v;
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE type, UINT startVertex, UINT primitives) override
    {
        const HRESULT hr = mReal->DrawPrimitive(type, startVertex, primitives);
        // Format 2: "DrawPrimitive <type> <primitives> <streams>", with "@<start vertex>" after the type only when a
        // stream isn't shadowed.
        Call streams("");
        bool positional = false;
        Streams(streams, startVertex, VertexCount(type, primitives), positional);
        Call call("DrawPrimitive");
        call.U(type);
        if (positional) call.S("@" + std::to_string(startVertex));
        call.U(primitives);
        if (!streams.Text().empty()) call.S(streams.Text().substr(1)); // (its tokens, each after a space)
        Log(call.Hr(hr), kStream, true);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE type, INT baseVertex, UINT minIndex, UINT vertices, UINT startIndex,
                                                   UINT primitives) override
    {
        const HRESULT hr = mReal->DrawIndexedPrimitive(type, baseVertex, minIndex, vertices, startIndex, primitives);
        // Format 2, with a shadowed index buffer: "DrawIndexedPrimitive <type> <primitives> n=<vertices used> <streams>
        // ib=<name> #<indices>": the vertices are those the indices reach (base vertex + smallest index on), the
        // indices are hashed relative to the smallest. The base vertex, smallest index, vertex count and start index
        // follow the type only when a buffer isn't shadowed.
        const void *ib = mTracker.indices;
        IndexedRange range;
        const bool known = ib && mTracker.Indices(ib, startIndex, VertexCount(type, primitives), range);
        int64_t first = static_cast<int64_t>(baseVertex) + (known ? range.low : minIndex);
        const uint32_t count = known ? range.high - range.low + 1 : vertices;
        Call streams("");
        bool positional = !known;
        Streams(streams, first > 0 ? static_cast<uint32_t>(first) : 0, count, positional);
        Call call("DrawIndexedPrimitive");
        call.U(type);
        if (positional) call.I(baseVertex).U(minIndex).U(vertices).U(startIndex);
        call.U(primitives);
        if (known) call.S("n=" + std::to_string(count));
        if (!streams.Text().empty()) call.S(streams.Text().substr(1));
        call.S("ib=" + mTracker.Name(ib, 'I'));
        if (known) call.H(range.hash);
        Log(call.Hr(hr), kStream, true);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT primitives, const void *data, UINT stride) override
    {
        const HRESULT hr = mReal->DrawPrimitiveUP(type, primitives, data, stride);
        Log(Call("DrawPrimitiveUP").U(type).U(primitives).U(stride).Bytes(data, static_cast<size_t>(VertexCount(type, primitives)) * stride).Hr(hr),
            kStream, true);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type, UINT minIndex, UINT vertices, UINT primitives, const void *indices,
                                                     D3DFORMAT indexFormat, const void *data, UINT stride) override
    {
        const HRESULT hr = mReal->DrawIndexedPrimitiveUP(type, minIndex, vertices, primitives, indices, indexFormat, data, stride);
        const size_t indexBytes = static_cast<size_t>(VertexCount(type, primitives)) * (indexFormat == D3DFMT_INDEX32 ? 4 : 2);
        Log(Call("DrawIndexedPrimitiveUP").U(type).U(minIndex).U(vertices).U(primitives).U(indexFormat).U(stride)
                .Bytes(indices, indexBytes)
                .Bytes(data ? static_cast<const uint8_t *>(data) + static_cast<size_t>(minIndex) * stride : nullptr,
                       static_cast<size_t>(vertices) * stride)
                .Hr(hr),
            kStream, true);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE ProcessVertices(UINT sourceStart, UINT destIndex, UINT count, IDirect3DVertexBuffer9 *dest,
                                              IDirect3DVertexDeclaration9 *declaration, DWORD flags) override
    {
        const HRESULT hr = mReal->ProcessVertices(sourceStart, destIndex, count, dest, declaration, flags);
        Log(Call("ProcessVertices").U(sourceStart).U(destIndex).U(count).S(mTracker.Name(dest, 'V')).S(mTracker.Name(declaration, 'd'))
                .X(flags).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(const D3DVERTEXELEMENT9 *elements, IDirect3DVertexDeclaration9 **declaration) override
    {
        const HRESULT hr = mReal->CreateVertexDeclaration(elements, declaration);
        const uint32_t n = DeclarationLength(elements);
        if (SUCCEEDED(hr) && declaration) mTracker.Register(*declaration, 'd', devlog::Hash(elements, n * sizeof(D3DVERTEXELEMENT9)));
        Log(Call("CreateVertexDeclaration").U(n).S(Out(declaration, 'd')).Hr(hr), kAsync);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9 *declaration) override
    {
        const HRESULT hr = mReal->SetVertexDeclaration(declaration);
        Log(Call("SetVertexDeclaration").S(mTracker.Name(declaration, 'd')).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9 **declaration) override
    {
        const HRESULT hr = mReal->GetVertexDeclaration(declaration);
        Log(Call("GetVertexDeclaration").S(Out(declaration, 'd')).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetFVF(DWORD fvf) override
    {
        const HRESULT hr = mReal->SetFVF(fvf);
        Log(Call("SetFVF").X(fvf).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetFVF(DWORD *fvf) override
    {
        const HRESULT hr = mReal->GetFVF(fvf);
        Log(Call("GetFVF").Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD *function, IDirect3DVertexShader9 **shader) override
    {
        const HRESULT hr = mReal->CreateVertexShader(function, shader);
        const uint32_t n = ShaderLength(function);
        if (SUCCEEDED(hr) && shader) mTracker.Register(*shader, 'v', devlog::Hash(function, n * sizeof(DWORD)));
        Log(Call("CreateVertexShader").U(n).S(Out(shader, 'v')).Hr(hr), kAsync);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9 *shader) override
    {
        const HRESULT hr = mReal->SetVertexShader(shader);
        Log(Call("SetVertexShader").S(mTracker.Name(shader, 'v')).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9 **shader) override
    {
        const HRESULT hr = mReal->GetVertexShader(shader);
        Log(Call("GetVertexShader").S(Out(shader, 'v')).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT start, const float *data, UINT count) override
    {
        const HRESULT hr = mReal->SetVertexShaderConstantF(start, data, count);
        Log(ConstantsF(Call("SetVertexShaderConstantF"), start, data, count).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT start, float *data, UINT count) override
    {
        const HRESULT hr = mReal->GetVertexShaderConstantF(start, data, count);
        Log(Call("GetVertexShaderConstantF").U(start).U(count).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT start, const int *data, UINT count) override
    {
        const HRESULT hr = mReal->SetVertexShaderConstantI(start, data, count);
        Log(Call("SetVertexShaderConstantI").U(start).U(count).Ints(data, count * 4).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantI(UINT start, int *data, UINT count) override
    {
        const HRESULT hr = mReal->GetVertexShaderConstantI(start, data, count);
        Log(Call("GetVertexShaderConstantI").U(start).U(count).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT start, const BOOL *data, UINT count) override
    {
        const HRESULT hr = mReal->SetVertexShaderConstantB(start, data, count);
        Log(Call("SetVertexShaderConstantB").U(start).U(count).Ints(reinterpret_cast<const int *>(data), count).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantB(UINT start, BOOL *data, UINT count) override
    {
        const HRESULT hr = mReal->GetVertexShaderConstantB(start, data, count);
        Log(Call("GetVertexShaderConstantB").U(start).U(count).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT stream, IDirect3DVertexBuffer9 *buffer, UINT offset, UINT stride) override
    {
        const HRESULT hr = mReal->SetStreamSource(stream, buffer, offset, stride);
        if (SUCCEEDED(hr) && stream < 16) mTracker.streams[stream] = StreamBinding{buffer, offset, stride};
        // Format 2: a shadowed buffer's offset is "*" (a ring's or pool's position; the draws hash what they use).
        Call call("SetStreamSource");
        call.U(stream).S(mTracker.Name(buffer, 'V'));
        if (buffer && mTracker.IsShadowed(buffer)) call.S("*");
        else call.U(offset);
        Log(call.U(stride).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT stream, IDirect3DVertexBuffer9 **buffer, UINT *offset, UINT *stride) override
    {
        const HRESULT hr = mReal->GetStreamSource(stream, buffer, offset, stride);
        Log(Call("GetStreamSource").U(stream).S(Out(buffer, 'V')).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT stream, UINT setting) override
    {
        const HRESULT hr = mReal->SetStreamSourceFreq(stream, setting);
        Log(Call("SetStreamSourceFreq").U(stream).X(setting).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT stream, UINT *setting) override
    {
        const HRESULT hr = mReal->GetStreamSourceFreq(stream, setting);
        Log(Call("GetStreamSourceFreq").U(stream).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9 *buffer) override
    {
        const HRESULT hr = mReal->SetIndices(buffer);
        if (SUCCEEDED(hr)) mTracker.indices = buffer;
        Log(Call("SetIndices").S(mTracker.Name(buffer, 'I')).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9 **buffer) override
    {
        const HRESULT hr = mReal->GetIndices(buffer);
        Log(Call("GetIndices").S(Out(buffer, 'I')).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD *function, IDirect3DPixelShader9 **shader) override
    {
        const HRESULT hr = mReal->CreatePixelShader(function, shader);
        const uint32_t n = ShaderLength(function);
        if (SUCCEEDED(hr) && shader) mTracker.Register(*shader, 'p', devlog::Hash(function, n * sizeof(DWORD)));
        Log(Call("CreatePixelShader").U(n).S(Out(shader, 'p')).Hr(hr), kAsync);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9 *shader) override
    {
        const HRESULT hr = mReal->SetPixelShader(shader);
        Log(Call("SetPixelShader").S(mTracker.Name(shader, 'p')).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9 **shader) override
    {
        const HRESULT hr = mReal->GetPixelShader(shader);
        Log(Call("GetPixelShader").S(Out(shader, 'p')).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT start, const float *data, UINT count) override
    {
        const HRESULT hr = mReal->SetPixelShaderConstantF(start, data, count);
        Log(ConstantsF(Call("SetPixelShaderConstantF"), start, data, count).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT start, float *data, UINT count) override
    {
        const HRESULT hr = mReal->GetPixelShaderConstantF(start, data, count);
        Log(Call("GetPixelShaderConstantF").U(start).U(count).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT start, const int *data, UINT count) override
    {
        const HRESULT hr = mReal->SetPixelShaderConstantI(start, data, count);
        Log(Call("SetPixelShaderConstantI").U(start).U(count).Ints(data, count * 4).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantI(UINT start, int *data, UINT count) override
    {
        const HRESULT hr = mReal->GetPixelShaderConstantI(start, data, count);
        Log(Call("GetPixelShaderConstantI").U(start).U(count).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT start, const BOOL *data, UINT count) override
    {
        const HRESULT hr = mReal->SetPixelShaderConstantB(start, data, count);
        Log(Call("SetPixelShaderConstantB").U(start).U(count).Ints(reinterpret_cast<const int *>(data), count).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantB(UINT start, BOOL *data, UINT count) override
    {
        const HRESULT hr = mReal->GetPixelShaderConstantB(start, data, count);
        Log(Call("GetPixelShaderConstantB").U(start).U(count).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT handle, const float *segments, const D3DRECTPATCH_INFO *info) override
    {
        const HRESULT hr = mReal->DrawRectPatch(handle, segments, info);
        Log(Call("DrawRectPatch").U(handle).Floats(segments, 4).Bytes(info, sizeof(D3DRECTPATCH_INFO)).Hr(hr), kStream, true);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT handle, const float *segments, const D3DTRIPATCH_INFO *info) override
    {
        const HRESULT hr = mReal->DrawTriPatch(handle, segments, info);
        Log(Call("DrawTriPatch").U(handle).Floats(segments, 3).Bytes(info, sizeof(D3DTRIPATCH_INFO)).Hr(hr), kStream, true);
        return hr;
    }
    HRESULT STDMETHODCALLTYPE DeletePatch(UINT handle) override
    {
        const HRESULT hr = mReal->DeletePatch(handle);
        Log(Call("DeletePatch").U(handle).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE type, IDirect3DQuery9 **query) override
    {
        const HRESULT hr = mReal->CreateQuery(type, query);
        if (SUCCEEDED(hr) && query) mTracker.Register(*query, 'q', static_cast<uint32_t>(type));
        Log(Call("CreateQuery").U(type).S(query ? Out(query, 'q') : std::string("null")).Hr(hr), kAsync);
        return hr;
    }

private:
    void Log(const Call &call, Kind kind = kStream, bool draw = false, bool present = false)
    {
        mTracker.Log(call, kind, draw, present);
    }
    // The name of an object a call handed back.
    template <typename T> std::string Out(T **object, char expected)
    {
        if (!object) return "null";
        return mTracker.Name(*object, expected);
    }
    // Shader constants: up to four registers by value, more by hash.
    static Call &ConstantsF(Call &&call, UINT start, const float *data, UINT count)
    {
        call.U(start).U(count);
        if (count <= 4) call.Floats(data, count * 4);
        else call.Bytes(data, static_cast<size_t>(count) * 16);
        return call;
    }
    // Each bound stream: its buffer and stride and the hash of the vertices drawn (a shadowed buffer), or its buffer,
    // offset and stride (one beyond the shadow budget: then `positional` is set, and the draw shows its positions).
    void Streams(Call &call, uint32_t firstVertex, uint32_t count, bool &positional)
    {
        char b[48];
        for (uint32_t i = 0; i < 16; ++i) {
            const StreamBinding &s = mTracker.streams[i];
            if (!s.buffer) continue;
            std::snprintf(b, sizeof(b), "s%u=", i);
            std::string token = b + mTracker.Name(s.buffer, 'V');
            if (mTracker.IsShadowed(s.buffer)) {
                std::snprintf(b, sizeof(b), "/%u", s.stride);
                call.S(token + b);
                call.H(mTracker.ShadowHash(s.buffer, s.offset + static_cast<uint64_t>(firstVertex) * s.stride,
                                           static_cast<uint64_t>(count) * s.stride));
            } else {
                std::snprintf(b, sizeof(b), "+%u/%u", s.offset, s.stride);
                call.S(token + b);
                positional = true;
            }
        }
    }

    IDirect3DDevice9 *mReal;
    LoggedDirect3D *mParent;
    Tracker mTracker;
};

class LoggedDirect3D final : public IDirect3D9 {
public:
    LoggedDirect3D(IDirect3D9 *real, devlog::Recorder &recorder) : mReal(real), mRecorder(recorder) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override
    {
        if (object && (riid == IID_IUnknown || riid == IID_IDirect3D9)) {
            *object = this;
            AddRef();
            return S_OK;
        }
        return mReal->QueryInterface(riid, object);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return mReal->AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG n = mReal->Release();
        if (n == 0) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void *initialize) override
    {
        const HRESULT hr = mReal->RegisterSoftwareDevice(initialize);
        Log(Call("RegisterSoftwareDevice").Hr(hr));
        return hr;
    }
    UINT STDMETHODCALLTYPE GetAdapterCount() override
    {
        const UINT n = mReal->GetAdapterCount();
        Log(Call("GetAdapterCount")); // the answer is the machine's
        return n;
    }
    HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT adapter, DWORD flags, D3DADAPTER_IDENTIFIER9 *identifier) override
    {
        const HRESULT hr = mReal->GetAdapterIdentifier(adapter, flags, identifier);
        Log(Call("GetAdapterIdentifier").U(adapter).X(flags).Hr(hr));
        return hr;
    }
    UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT adapter, D3DFORMAT format) override
    {
        const UINT n = mReal->GetAdapterModeCount(adapter, format);
        Log(Call("GetAdapterModeCount").U(adapter).U(format));
        return n;
    }
    HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT adapter, D3DFORMAT format, UINT mode, D3DDISPLAYMODE *out) override
    {
        const HRESULT hr = mReal->EnumAdapterModes(adapter, format, mode, out);
        Log(Call("EnumAdapterModes").U(adapter).U(format).U(mode).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT adapter, D3DDISPLAYMODE *mode) override
    {
        const HRESULT hr = mReal->GetAdapterDisplayMode(adapter, mode);
        Log(Call("GetAdapterDisplayMode").U(adapter).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT adapter, D3DDEVTYPE type, D3DFORMAT adapterFormat, D3DFORMAT backFormat, BOOL windowed) override
    {
        const HRESULT hr = mReal->CheckDeviceType(adapter, type, adapterFormat, backFormat, windowed);
        Log(Call("CheckDeviceType").U(adapter).U(type).U(adapterFormat).U(backFormat).U(windowed).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT adapter, D3DDEVTYPE type, D3DFORMAT adapterFormat, DWORD usage, D3DRESOURCETYPE resourceType,
                                                D3DFORMAT format) override
    {
        const HRESULT hr = mReal->CheckDeviceFormat(adapter, type, adapterFormat, usage, resourceType, format);
        Log(Call("CheckDeviceFormat").U(adapter).U(type).U(adapterFormat).X(usage).U(resourceType).U(format).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT adapter, D3DDEVTYPE type, D3DFORMAT format, BOOL windowed,
                                                         D3DMULTISAMPLE_TYPE multiSample, DWORD *quality) override
    {
        const HRESULT hr = mReal->CheckDeviceMultiSampleType(adapter, type, format, windowed, multiSample, quality);
        Log(Call("CheckDeviceMultiSampleType").U(adapter).U(type).U(format).U(windowed).U(multiSample).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT adapter, D3DDEVTYPE type, D3DFORMAT adapterFormat, D3DFORMAT targetFormat,
                                                     D3DFORMAT depthFormat) override
    {
        const HRESULT hr = mReal->CheckDepthStencilMatch(adapter, type, adapterFormat, targetFormat, depthFormat);
        Log(Call("CheckDepthStencilMatch").U(adapter).U(type).U(adapterFormat).U(targetFormat).U(depthFormat).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT adapter, D3DDEVTYPE type, D3DFORMAT source, D3DFORMAT target) override
    {
        const HRESULT hr = mReal->CheckDeviceFormatConversion(adapter, type, source, target);
        Log(Call("CheckDeviceFormatConversion").U(adapter).U(type).U(source).U(target).Hr(hr));
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT adapter, D3DDEVTYPE type, D3DCAPS9 *caps) override
    {
        const HRESULT hr = mReal->GetDeviceCaps(adapter, type, caps);
        Log(Call("GetDeviceCaps").U(adapter).U(type).Hr(hr));
        return hr;
    }
    HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT adapter) override
    {
        Log(Call("GetAdapterMonitor").U(adapter));
        return mReal->GetAdapterMonitor(adapter);
    }
    HRESULT STDMETHODCALLTYPE CreateDevice(UINT adapter, D3DDEVTYPE type, HWND focus, DWORD behaviour, D3DPRESENT_PARAMETERS *pp,
                                           IDirect3DDevice9 **device) override
    {
        IDirect3DDevice9 *real = nullptr;
        const HRESULT hr = mReal->CreateDevice(adapter, type, focus, behaviour, pp, device ? &real : nullptr);
        if (device) *device = nullptr;
        if (SUCCEEDED(hr) && real && device) {
            // The device's thread is the main thread: calls from others are recorded as async.
            mRecorder.SetMainThread(GetCurrentThreadId());
            *device = new LoggedDevice(real, this, mRecorder);
        }
        Log(Call("CreateDevice").U(adapter).U(type).U(focus != nullptr).X(behaviour).Present(pp).Hr(hr));
        return hr;
    }

    IDirect3D9 *Real() { return mReal; }

private:
    void Log(const Call &call) { mRecorder.Record(Kind::Stream, call.Text()); }

    IDirect3D9 *mReal;
    devlog::Recorder &mRecorder;
};

LoggedDevice::LoggedDevice(IDirect3DDevice9 *real, LoggedDirect3D *parent, devlog::Recorder &recorder)
    : mReal(real), mParent(parent), mTracker(recorder)
{
    mParent->AddRef(); // GetDirect3D hands it out; it lives as long as the device
    gTracker = &mTracker;
}

ULONG STDMETHODCALLTYPE LoggedDevice::Release()
{
    const ULONG n = mReal->Release();
    if (n == 0) {
        if (gTracker == &mTracker) gTracker = nullptr;
        mTracker.Recorder().Finish();
        LoggedDirect3D *parent = mParent;
        delete this;
        parent->Release();
    }
    return n;
}

HRESULT STDMETHODCALLTYPE LoggedDevice::GetDirect3D(IDirect3D9 **d3d)
{
    if (!d3d) return mReal->GetDirect3D(d3d);
    IDirect3D9 *real = nullptr;
    const HRESULT hr = mReal->GetDirect3D(&real);
    if (SUCCEEDED(hr) && real) {
        real->Release(); // the wrapper's reference stands for it
        mParent->AddRef();
        *d3d = mParent;
    } else {
        *d3d = real;
    }
    Log(Call("GetDirect3D").Hr(hr));
    return hr;
}

} // namespace

IDirect3D9 *Wrap(IDirect3D9 *real, devlog::Recorder &recorder)
{
    if (!real) return nullptr;
    return new LoggedDirect3D(real, recorder);
}

} // namespace d3dlog
