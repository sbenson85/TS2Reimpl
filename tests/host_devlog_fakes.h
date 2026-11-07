#pragma once
// Fake Direct3D 9 objects for tests/host_devlog.cpp: just enough behaviour for the device call log's wrappers
// (host/shared/d3dlog.h) to forward to, lock and name. Buffers and textures keep their bytes in memory; textures
// lock a pitch wider than their rows, with noise in the padding. Not final, and called through interface pointers,
// so calls go through the vtables the log patches.
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace fakes {

struct Counter {
    ULONG refs = 1;
};

#define FAKE_UNKNOWN(Iface)                                                                                            \
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **out) override                                             \
    {                                                                                                                  \
        if (out) *out = nullptr;                                                                                       \
        return E_NOINTERFACE;                                                                                          \
    }                                                                                                                  \
    ULONG STDMETHODCALLTYPE AddRef() override { return ++mRefs.refs; }                                                \
    ULONG STDMETHODCALLTYPE Release() override                                                                         \
    {                                                                                                                  \
        const ULONG n = --mRefs.refs;                                                                                  \
        if (!n) delete this;                                                                                           \
        return n;                                                                                                      \
    }                                                                                                                  \
    Counter mRefs;

#define FAKE_RESOURCE(type)                                                                                            \
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **) override { return E_NOTIMPL; }                           \
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void *, DWORD, DWORD) override { return E_NOTIMPL; }      \
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void *, DWORD *) override { return E_NOTIMPL; }                 \
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override { return E_NOTIMPL; }                                 \
    DWORD STDMETHODCALLTYPE SetPriority(DWORD) override { return 0; }                                                 \
    DWORD STDMETHODCALLTYPE GetPriority() override { return 0; }                                                      \
    void STDMETHODCALLTYPE PreLoad() override {}                                                                       \
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return type; }

template <typename Iface, typename Desc, D3DRESOURCETYPE kType> class FakeBuffer : public Iface {
public:
    FakeBuffer(UINT length, DWORD usage, D3DFORMAT format) : mBytes(length, 0)
    {
        mDesc = Desc{};
        mDesc.Format = format;
        mDesc.Type = kType;
        mDesc.Usage = usage;
        mDesc.Size = length;
    }
    FAKE_UNKNOWN(Iface)
    FAKE_RESOURCE(kType)
    HRESULT STDMETHODCALLTYPE Lock(UINT offset, UINT, void **data, DWORD) override
    {
        if (offset > mBytes.size()) return D3DERR_INVALIDCALL;
        *data = mBytes.data() + offset;
        ++locks;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE Unlock() override
    {
        ++unlocks;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDesc(Desc *desc) override
    {
        *desc = mDesc;
        return D3D_OK;
    }
    std::vector<uint8_t> mBytes;
    Desc mDesc;
    int locks = 0, unlocks = 0;
};
using FakeVertexBuffer = FakeBuffer<IDirect3DVertexBuffer9, D3DVERTEXBUFFER_DESC, D3DRTYPE_VERTEXBUFFER>;
using FakeIndexBuffer = FakeBuffer<IDirect3DIndexBuffer9, D3DINDEXBUFFER_DESC, D3DRTYPE_INDEXBUFFER>;

class FakeTexture;

// A texture's level 0, or a stand-alone surface.
class FakeSurface : public IDirect3DSurface9 {
public:
    FakeSurface(UINT width, UINT height, D3DFORMAT format, IDirect3DBaseTexture9 *container, uint8_t *bits, UINT pitch)
        : mContainer(container), mBits(bits), mPitch(pitch)
    {
        mDesc = D3DSURFACE_DESC{};
        mDesc.Format = format;
        mDesc.Type = D3DRTYPE_SURFACE;
        mDesc.Width = width;
        mDesc.Height = height;
        if (!bits) {
            mOwn.assign(static_cast<size_t>(pitch) * height, 0);
            mBits = mOwn.data();
        }
    }
    FAKE_UNKNOWN(IDirect3DSurface9)
    FAKE_RESOURCE(D3DRTYPE_SURFACE)
    HRESULT STDMETHODCALLTYPE GetContainer(REFIID riid, void **out) override
    {
        if (mContainer && riid == IID_IDirect3DBaseTexture9) {
            mContainer->AddRef();
            *out = mContainer;
            return D3D_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC *desc) override
    {
        *desc = mDesc;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT *locked, const RECT *rect, DWORD) override
    {
        const UINT x = rect ? rect->left : 0, y = rect ? rect->top : 0;
        locked->Pitch = static_cast<INT>(mPitch);
        locked->pBits = mBits + static_cast<size_t>(y) * mPitch + x * 4;
        ++locks;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE UnlockRect() override
    {
        ++unlocks;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDC(HDC *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE ReleaseDC(HDC) override { return E_NOTIMPL; }

    IDirect3DBaseTexture9 *mContainer; // not counted: the texture owns its level
    uint8_t *mBits;
    UINT mPitch;
    std::vector<uint8_t> mOwn;
    D3DSURFACE_DESC mDesc;
    int locks = 0, unlocks = 0;
};

// One-level 32-bit texture. Its LockRect locks its level's surface through the surface's vtable, as a runtime may.
class FakeTexture : public IDirect3DTexture9 {
public:
    static constexpr UINT kPadding = 8;
    FakeTexture(UINT width, UINT height, D3DFORMAT format, DWORD usage, uint32_t noise)
        : mPitch(width * 4 + kPadding), mBits(static_cast<size_t>(width * 4 + kPadding) * height)
    {
        for (size_t i = 0; i < mBits.size(); ++i) mBits[i] = static_cast<uint8_t>(noise * 2654435761u >> (i % 24));
        auto *level = new FakeSurface(width, height, format, this, mBits.data(), mPitch);
        level->mDesc.Usage = usage;
        mLevel = level;
        mUsage = usage;
    }
    ~FakeTexture() { mLevel->Release(); }
    FAKE_UNKNOWN(IDirect3DTexture9)
    FAKE_RESOURCE(D3DRTYPE_TEXTURE)
    DWORD STDMETHODCALLTYPE SetLOD(DWORD) override { return 0; }
    DWORD STDMETHODCALLTYPE GetLOD() override { return 0; }
    DWORD STDMETHODCALLTYPE GetLevelCount() override { return 1; }
    HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE) override { return D3D_OK; }
    D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override { return D3DTEXF_NONE; }
    void STDMETHODCALLTYPE GenerateMipSubLevels() override {}
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level, D3DSURFACE_DESC *desc) override
    {
        if (level) return D3DERR_INVALIDCALL;
        *desc = static_cast<FakeSurface *>(mLevel)->mDesc;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT level, IDirect3DSurface9 **surface) override
    {
        if (level) return D3DERR_INVALIDCALL;
        mLevel->AddRef();
        *surface = mLevel;
        return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE LockRect(UINT level, D3DLOCKED_RECT *locked, const RECT *rect, DWORD flags) override
    {
        if (level) return D3DERR_INVALIDCALL;
        return mLevel->LockRect(locked, rect, flags);
    }
    HRESULT STDMETHODCALLTYPE UnlockRect(UINT level) override
    {
        if (level) return D3DERR_INVALIDCALL;
        return mLevel->UnlockRect();
    }
    HRESULT STDMETHODCALLTYPE AddDirtyRect(const RECT *) override { return D3D_OK; }

    UINT mPitch;
    std::vector<uint8_t> mBits;
    IDirect3DSurface9 *mLevel; // through the interface: calls go through its vtable
    DWORD mUsage;
};

// The device: creates the fakes above and remembers what reached it.
class FakeDevice : public IDirect3DDevice9 {
public:
    explicit FakeDevice(uint32_t noise) : mNoise(noise) {}
    FAKE_UNKNOWN(IDirect3DDevice9)
    std::vector<std::string> calls; // names of the calls that reached the device, in order
    DWORD renderState[256] = {};
    IDirect3DBaseTexture9 *texture0 = nullptr;
    UINT drawStart = 0, drawCount = 0;
    uint32_t mNoise;

    HRESULT Note(const char *name)
    {
        calls.push_back(name);
        return D3D_OK;
    }

    HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override { return Note("TestCooperativeLevel"); }
    UINT STDMETHODCALLTYPE GetAvailableTextureMem() override { return 64u << 20; }
    HRESULT STDMETHODCALLTYPE EvictManagedResources() override { return Note("EvictManagedResources"); }
    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9 **d3d) override
    {
        *d3d = nullptr;
        return D3DERR_INVALIDCALL;
    }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9 *) override { return Note("GetDeviceCaps"); }
    HRESULT STDMETHODCALLTYPE GetDisplayMode(UINT, D3DDISPLAYMODE *) override { return Note("GetDisplayMode"); }
    HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *) override { return Note("GetCreationParameters"); }
    HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT, UINT, IDirect3DSurface9 *) override { return Note("SetCursorProperties"); }
    void STDMETHODCALLTYPE SetCursorPosition(int, int, DWORD) override { Note("SetCursorPosition"); }
    BOOL STDMETHODCALLTYPE ShowCursor(BOOL) override { return FALSE; }
    HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *, IDirect3DSwapChain9 **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetSwapChain(UINT, IDirect3DSwapChain9 **) override { return E_NOTIMPL; }
    UINT STDMETHODCALLTYPE GetNumberOfSwapChains() override { return 1; }
    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS *) override { return Note("Reset"); }
    HRESULT STDMETHODCALLTYPE Present(const RECT *, const RECT *, HWND, const RGNDATA *) override { return Note("Present"); }
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9 **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetRasterStatus(UINT, D3DRASTER_STATUS *) override { return Note("GetRasterStatus"); }
    HRESULT STDMETHODCALLTYPE SetDialogBoxMode(BOOL) override { return Note("SetDialogBoxMode"); }
    void STDMETHODCALLTYPE SetGammaRamp(UINT, DWORD, const D3DGAMMARAMP *) override { Note("SetGammaRamp"); }
    void STDMETHODCALLTYPE GetGammaRamp(UINT, D3DGAMMARAMP *) override { Note("GetGammaRamp"); }
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT width, UINT height, UINT, DWORD usage, D3DFORMAT format, D3DPOOL, IDirect3DTexture9 **texture,
                                            HANDLE *) override
    {
        *texture = new FakeTexture(width, height, format, usage, ++mNoise);
        return Note("CreateTexture");
    }
    HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DVolumeTexture9 **, HANDLE *) override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DCubeTexture9 **, HANDLE *) override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT length, DWORD usage, DWORD, D3DPOOL, IDirect3DVertexBuffer9 **buffer, HANDLE *) override
    {
        *buffer = new FakeVertexBuffer(length, usage, D3DFMT_VERTEXDATA);
        return Note("CreateVertexBuffer");
    }
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT length, DWORD usage, D3DFORMAT format, D3DPOOL, IDirect3DIndexBuffer9 **buffer, HANDLE *) override
    {
        *buffer = new FakeIndexBuffer(length, usage, format);
        return Note("CreateIndexBuffer");
    }
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE, DWORD, BOOL,
                                                 IDirect3DSurface9 **surface, HANDLE *) override
    {
        *surface = new FakeSurface(width, height, format, nullptr, nullptr, width * 4);
        return Note("CreateRenderTarget");
    }
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL, IDirect3DSurface9 **, HANDLE *) override
    {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9 *, const RECT *, IDirect3DSurface9 *, const POINT *) override { return Note("UpdateSurface"); }
    HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9 *, IDirect3DBaseTexture9 *) override { return Note("UpdateTexture"); }
    HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9 *, IDirect3DSurface9 *) override { return Note("GetRenderTargetData"); }
    HRESULT STDMETHODCALLTYPE GetFrontBufferData(UINT, IDirect3DSurface9 *) override { return Note("GetFrontBufferData"); }
    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9 *, const RECT *, IDirect3DSurface9 *, const RECT *, D3DTEXTUREFILTERTYPE) override
    {
        return Note("StretchRect");
    }
    HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9 *, const RECT *, D3DCOLOR) override { return Note("ColorFill"); }
    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT width, UINT height, D3DFORMAT format, D3DPOOL, IDirect3DSurface9 **surface, HANDLE *) override
    {
        *surface = new FakeSurface(width, height, format, nullptr, nullptr, width * 4);
        return Note("CreateOffscreenPlainSurface");
    }
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD, IDirect3DSurface9 *) override { return Note("SetRenderTarget"); }
    HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD, IDirect3DSurface9 **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9 *) override { return Note("SetDepthStencilSurface"); }
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9 **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE BeginScene() override { return Note("BeginScene"); }
    HRESULT STDMETHODCALLTYPE EndScene() override { return Note("EndScene"); }
    HRESULT STDMETHODCALLTYPE Clear(DWORD, const D3DRECT *, DWORD, D3DCOLOR, float, DWORD) override { return Note("Clear"); }
    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE, const D3DMATRIX *) override { return Note("SetTransform"); }
    HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE, D3DMATRIX *) override { return Note("GetTransform"); }
    HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE, const D3DMATRIX *) override { return Note("MultiplyTransform"); }
    HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9 *) override { return Note("SetViewport"); }
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9 *) override { return Note("GetViewport"); }
    HRESULT STDMETHODCALLTYPE SetMaterial(const D3DMATERIAL9 *) override { return Note("SetMaterial"); }
    HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL9 *) override { return Note("GetMaterial"); }
    HRESULT STDMETHODCALLTYPE SetLight(DWORD, const D3DLIGHT9 *) override { return Note("SetLight"); }
    HRESULT STDMETHODCALLTYPE GetLight(DWORD, D3DLIGHT9 *) override { return Note("GetLight"); }
    HRESULT STDMETHODCALLTYPE LightEnable(DWORD, BOOL) override { return Note("LightEnable"); }
    HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD, BOOL *) override { return Note("GetLightEnable"); }
    HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD, const float *) override { return Note("SetClipPlane"); }
    HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD, float *) override { return Note("GetClipPlane"); }
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE state, DWORD value) override
    {
        if (state < 256) renderState[state] = value;
        return Note("SetRenderState");
    }
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE, DWORD *) override { return Note("GetRenderState"); }
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE, IDirect3DStateBlock9 **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE BeginStateBlock() override { return Note("BeginStateBlock"); }
    HRESULT STDMETHODCALLTYPE EndStateBlock(IDirect3DStateBlock9 **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetClipStatus(const D3DCLIPSTATUS9 *) override { return Note("SetClipStatus"); }
    HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS9 *) override { return Note("GetClipStatus"); }
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD, IDirect3DBaseTexture9 **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage, IDirect3DBaseTexture9 *texture) override
    {
        if (stage == 0) texture0 = texture;
        return Note("SetTexture");
    }
    HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD, D3DTEXTURESTAGESTATETYPE, DWORD *) override { return Note("GetTextureStageState"); }
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD, D3DTEXTURESTAGESTATETYPE, DWORD) override { return Note("SetTextureStageState"); }
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD, D3DSAMPLERSTATETYPE, DWORD *) override { return Note("GetSamplerState"); }
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD, D3DSAMPLERSTATETYPE, DWORD) override { return Note("SetSamplerState"); }
    HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD *) override { return Note("ValidateDevice"); }
    HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT, const PALETTEENTRY *) override { return Note("SetPaletteEntries"); }
    HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT, PALETTEENTRY *) override { return Note("GetPaletteEntries"); }
    HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT) override { return Note("SetCurrentTexturePalette"); }
    HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT *) override { return Note("GetCurrentTexturePalette"); }
    HRESULT STDMETHODCALLTYPE SetScissorRect(const RECT *) override { return Note("SetScissorRect"); }
    HRESULT STDMETHODCALLTYPE GetScissorRect(RECT *) override { return Note("GetScissorRect"); }
    HRESULT STDMETHODCALLTYPE SetSoftwareVertexProcessing(BOOL) override { return Note("SetSoftwareVertexProcessing"); }
    BOOL STDMETHODCALLTYPE GetSoftwareVertexProcessing() override { return FALSE; }
    HRESULT STDMETHODCALLTYPE SetNPatchMode(float) override { return Note("SetNPatchMode"); }
    float STDMETHODCALLTYPE GetNPatchMode() override { return 0.0f; }
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE, UINT start, UINT count) override
    {
        drawStart = start;
        drawCount = count;
        return Note("DrawPrimitive");
    }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT) override { return Note("DrawIndexedPrimitive"); }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE, UINT, const void *, UINT) override { return Note("DrawPrimitiveUP"); }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE, UINT, UINT, UINT, const void *, D3DFORMAT, const void *, UINT) override
    {
        return Note("DrawIndexedPrimitiveUP");
    }
    HRESULT STDMETHODCALLTYPE ProcessVertices(UINT, UINT, UINT, IDirect3DVertexBuffer9 *, IDirect3DVertexDeclaration9 *, DWORD) override
    {
        return Note("ProcessVertices");
    }
    HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(const D3DVERTEXELEMENT9 *, IDirect3DVertexDeclaration9 **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9 *) override { return Note("SetVertexDeclaration"); }
    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9 **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetFVF(DWORD) override { return Note("SetFVF"); }
    HRESULT STDMETHODCALLTYPE GetFVF(DWORD *) override { return Note("GetFVF"); }
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD *, IDirect3DVertexShader9 **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9 *) override { return Note("SetVertexShader"); }
    HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9 **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT, const float *, UINT) override { return Note("SetVertexShaderConstantF"); }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT, float *, UINT) override { return Note("GetVertexShaderConstantF"); }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT, const int *, UINT) override { return Note("SetVertexShaderConstantI"); }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantI(UINT, int *, UINT) override { return Note("GetVertexShaderConstantI"); }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT, const BOOL *, UINT) override { return Note("SetVertexShaderConstantB"); }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantB(UINT, BOOL *, UINT) override { return Note("GetVertexShaderConstantB"); }
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT, IDirect3DVertexBuffer9 *, UINT, UINT) override { return Note("SetStreamSource"); }
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT, IDirect3DVertexBuffer9 **, UINT *, UINT *) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT, UINT) override { return Note("SetStreamSourceFreq"); }
    HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT, UINT *) override { return Note("GetStreamSourceFreq"); }
    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9 *) override { return Note("SetIndices"); }
    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9 **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD *, IDirect3DPixelShader9 **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9 *) override { return Note("SetPixelShader"); }
    HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9 **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT, const float *, UINT) override { return Note("SetPixelShaderConstantF"); }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT, float *, UINT) override { return Note("GetPixelShaderConstantF"); }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT, const int *, UINT) override { return Note("SetPixelShaderConstantI"); }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantI(UINT, int *, UINT) override { return Note("GetPixelShaderConstantI"); }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT, const BOOL *, UINT) override { return Note("SetPixelShaderConstantB"); }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantB(UINT, BOOL *, UINT) override { return Note("GetPixelShaderConstantB"); }
    HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT, const float *, const D3DRECTPATCH_INFO *) override { return Note("DrawRectPatch"); }
    HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT, const float *, const D3DTRIPATCH_INFO *) override { return Note("DrawTriPatch"); }
    HRESULT STDMETHODCALLTYPE DeletePatch(UINT) override { return Note("DeletePatch"); }
    HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE, IDirect3DQuery9 **) override { return E_NOTIMPL; }
};

class FakeDirect3D : public IDirect3D9 {
public:
    explicit FakeDirect3D(uint32_t noise) : mNoise(noise) {}
    FAKE_UNKNOWN(IDirect3D9)
    uint32_t mNoise;
    FakeDevice *device = nullptr; // the last device created (not counted)

    HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void *) override { return E_NOTIMPL; }
    UINT STDMETHODCALLTYPE GetAdapterCount() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT, DWORD, D3DADAPTER_IDENTIFIER9 *) override { return D3D_OK; }
    UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT, D3DFORMAT) override { return 0; }
    HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT, D3DFORMAT, UINT, D3DDISPLAYMODE *) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT, D3DDISPLAYMODE *) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, BOOL) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT, D3DDEVTYPE, D3DFORMAT, DWORD, D3DRESOURCETYPE, D3DFORMAT format) override
    {
        return format == D3DFMT_DXT1 ? D3D_OK : D3DERR_NOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT, D3DDEVTYPE, D3DFORMAT, BOOL, D3DMULTISAMPLE_TYPE, DWORD *) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, D3DFORMAT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT, D3DDEVTYPE, D3DCAPS9 *) override { return D3D_OK; }
    HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT) override { return nullptr; }
    HRESULT STDMETHODCALLTYPE CreateDevice(UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **out) override
    {
        device = new FakeDevice(mNoise);
        *out = device;
        return D3D_OK;
    }
};

#undef FAKE_UNKNOWN
#undef FAKE_RESOURCE

} // namespace fakes
