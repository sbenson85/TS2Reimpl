#pragma once
// nGZGraphic4::cNullDevice (RTTI 0x1097924; vftable 0xd9e64c, 120 slots; 4 bytes; lane NQ): a Direct3D 9 device that does
// nothing, handed out by cGraphicSystemDX's slot 29 (GetNullDevice). Every call fails with D3DERR_INVALIDCALL except
// QueryInterface (E_NOINTERFACE), AddRef / Release (1: the object is a static, never counted), GetAvailableTextureMem,
// ShowCursor and GetNumberOfSwapChains (0), GetSoftwareVertexProcessing (TRUE), GetNPatchMode (0.0f) and the three that
// return nothing (SetCursorPosition, SetGammaRamp, GetGammaRamp). Slot 119 is the deleting destructor (0x16a840).
//
// The game's object is a function-local static (0x10c9760, guard 0x10c9764, constructor 0x16a820, atexit 0xd8e950),
// made only by slot 29 (0x15a0d0); ours is our own constant-initialised static (cGraphicSystemDX::GetNullDevice), so
// the game's constructor, vtable and bodies are dead once the graphic system is ours. The declarations follow the Windows
// SDK's IDirect3DDevice9 (slot order and signatures); each body is the answer above for its slot.
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>

namespace nGZGraphic4 {

class cNullDevice : public IDirect3DDevice9 {
public:
    static constexpr HRESULT kInvalidCall = static_cast<HRESULT>(0x8876086c); // D3DERR_INVALIDCALL
    static constexpr HRESULT kNoInterface = static_cast<HRESULT>(0x80004002); // E_NOINTERFACE

    constexpr cNullDevice() = default;
    virtual ~cNullDevice() = default; // 119 (0x16a840)

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return kNoInterface; } // 0
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; } // 1
    ULONG STDMETHODCALLTYPE Release() override { return 1; } // 2
    HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override { return kInvalidCall; } // 3
    UINT STDMETHODCALLTYPE GetAvailableTextureMem() override { return 0; } // 4
    HRESULT STDMETHODCALLTYPE EvictManagedResources() override { return kInvalidCall; } // 5
    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9**) override { return kInvalidCall; } // 6
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9*) override { return kInvalidCall; } // 7
    HRESULT STDMETHODCALLTYPE GetDisplayMode(UINT, D3DDISPLAYMODE*) override { return kInvalidCall; } // 8
    HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS*) override { return kInvalidCall; } // 9
    HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT, UINT, IDirect3DSurface9*) override { return kInvalidCall; } // 10
    void STDMETHODCALLTYPE SetCursorPosition(int, int, DWORD) override {} // 11
    BOOL STDMETHODCALLTYPE ShowCursor(BOOL) override { return 0; } // 12
    HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS*, IDirect3DSwapChain9**) override { return kInvalidCall; } // 13
    HRESULT STDMETHODCALLTYPE GetSwapChain(UINT, IDirect3DSwapChain9**) override { return kInvalidCall; } // 14
    UINT STDMETHODCALLTYPE GetNumberOfSwapChains() override { return 0; } // 15
    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS*) override { return kInvalidCall; } // 16
    HRESULT STDMETHODCALLTYPE Present(CONST RECT*, CONST RECT*, HWND, CONST RGNDATA*) override { return kInvalidCall; } // 17
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9**) override { return kInvalidCall; } // 18
    HRESULT STDMETHODCALLTYPE GetRasterStatus(UINT, D3DRASTER_STATUS*) override { return kInvalidCall; } // 19
    HRESULT STDMETHODCALLTYPE SetDialogBoxMode(BOOL) override { return kInvalidCall; } // 20
    void STDMETHODCALLTYPE SetGammaRamp(UINT, DWORD, CONST D3DGAMMARAMP*) override {} // 21
    void STDMETHODCALLTYPE GetGammaRamp(UINT, D3DGAMMARAMP*) override {} // 22
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*) override { return kInvalidCall; } // 23
    HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DVolumeTexture9**, HANDLE*) override { return kInvalidCall; } // 24
    HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DCubeTexture9**, HANDLE*) override { return kInvalidCall; } // 25
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT, DWORD, DWORD, D3DPOOL, IDirect3DVertexBuffer9**, HANDLE*) override { return kInvalidCall; } // 26
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DIndexBuffer9**, HANDLE*) override { return kInvalidCall; } // 27
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL, IDirect3DSurface9**, HANDLE*) override { return kInvalidCall; } // 28
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL, IDirect3DSurface9**, HANDLE*) override { return kInvalidCall; } // 29
    HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9*, CONST RECT*, IDirect3DSurface9*, CONST POINT*) override { return kInvalidCall; } // 30
    HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9*, IDirect3DBaseTexture9*) override { return kInvalidCall; } // 31
    HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9*, IDirect3DSurface9*) override { return kInvalidCall; } // 32
    HRESULT STDMETHODCALLTYPE GetFrontBufferData(UINT, IDirect3DSurface9*) override { return kInvalidCall; } // 33
    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9*, CONST RECT*, IDirect3DSurface9*, CONST RECT*, D3DTEXTUREFILTERTYPE) override { return kInvalidCall; } // 34
    HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9*, CONST RECT*, D3DCOLOR) override { return kInvalidCall; } // 35
    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT, UINT, D3DFORMAT, D3DPOOL, IDirect3DSurface9**, HANDLE*) override { return kInvalidCall; } // 36
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD, IDirect3DSurface9*) override { return kInvalidCall; } // 37
    HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD, IDirect3DSurface9**) override { return kInvalidCall; } // 38
    HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9*) override { return kInvalidCall; } // 39
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9**) override { return kInvalidCall; } // 40
    HRESULT STDMETHODCALLTYPE BeginScene() override { return kInvalidCall; } // 41
    HRESULT STDMETHODCALLTYPE EndScene() override { return kInvalidCall; } // 42
    HRESULT STDMETHODCALLTYPE Clear(DWORD, CONST D3DRECT*, DWORD, D3DCOLOR, float, DWORD) override { return kInvalidCall; } // 43
    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE, CONST D3DMATRIX*) override { return kInvalidCall; } // 44
    HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE, D3DMATRIX*) override { return kInvalidCall; } // 45
    HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE, CONST D3DMATRIX*) override { return kInvalidCall; } // 46
    HRESULT STDMETHODCALLTYPE SetViewport(CONST D3DVIEWPORT9*) override { return kInvalidCall; } // 47
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9*) override { return kInvalidCall; } // 48
    HRESULT STDMETHODCALLTYPE SetMaterial(CONST D3DMATERIAL9*) override { return kInvalidCall; } // 49
    HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL9*) override { return kInvalidCall; } // 50
    HRESULT STDMETHODCALLTYPE SetLight(DWORD, CONST D3DLIGHT9*) override { return kInvalidCall; } // 51
    HRESULT STDMETHODCALLTYPE GetLight(DWORD, D3DLIGHT9*) override { return kInvalidCall; } // 52
    HRESULT STDMETHODCALLTYPE LightEnable(DWORD, BOOL) override { return kInvalidCall; } // 53
    HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD, BOOL*) override { return kInvalidCall; } // 54
    HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD, CONST float*) override { return kInvalidCall; } // 55
    HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD, float*) override { return kInvalidCall; } // 56
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE, DWORD) override { return kInvalidCall; } // 57
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE, DWORD*) override { return kInvalidCall; } // 58
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE, IDirect3DStateBlock9**) override { return kInvalidCall; } // 59
    HRESULT STDMETHODCALLTYPE BeginStateBlock() override { return kInvalidCall; } // 60
    HRESULT STDMETHODCALLTYPE EndStateBlock(IDirect3DStateBlock9**) override { return kInvalidCall; } // 61
    HRESULT STDMETHODCALLTYPE SetClipStatus(CONST D3DCLIPSTATUS9*) override { return kInvalidCall; } // 62
    HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS9*) override { return kInvalidCall; } // 63
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD, IDirect3DBaseTexture9**) override { return kInvalidCall; } // 64
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD, IDirect3DBaseTexture9*) override { return kInvalidCall; } // 65
    HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD, D3DTEXTURESTAGESTATETYPE, DWORD*) override { return kInvalidCall; } // 66
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD, D3DTEXTURESTAGESTATETYPE, DWORD) override { return kInvalidCall; } // 67
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD, D3DSAMPLERSTATETYPE, DWORD*) override { return kInvalidCall; } // 68
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD, D3DSAMPLERSTATETYPE, DWORD) override { return kInvalidCall; } // 69
    HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD*) override { return kInvalidCall; } // 70
    HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT, CONST PALETTEENTRY*) override { return kInvalidCall; } // 71
    HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT, PALETTEENTRY*) override { return kInvalidCall; } // 72
    HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT) override { return kInvalidCall; } // 73
    HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT*) override { return kInvalidCall; } // 74
    HRESULT STDMETHODCALLTYPE SetScissorRect(CONST RECT*) override { return kInvalidCall; } // 75
    HRESULT STDMETHODCALLTYPE GetScissorRect(RECT*) override { return kInvalidCall; } // 76
    HRESULT STDMETHODCALLTYPE SetSoftwareVertexProcessing(BOOL) override { return kInvalidCall; } // 77
    BOOL STDMETHODCALLTYPE GetSoftwareVertexProcessing() override { return 1; } // 78
    HRESULT STDMETHODCALLTYPE SetNPatchMode(float) override { return kInvalidCall; } // 79
    float STDMETHODCALLTYPE GetNPatchMode() override { return 0.0f; } // 80
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE, UINT, UINT) override { return kInvalidCall; } // 81
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT) override { return kInvalidCall; } // 82
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE, UINT, CONST void*, UINT) override { return kInvalidCall; } // 83
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE, UINT, UINT, UINT, CONST void*, D3DFORMAT, CONST void*, UINT) override { return kInvalidCall; } // 84
    HRESULT STDMETHODCALLTYPE ProcessVertices(UINT, UINT, UINT, IDirect3DVertexBuffer9*, IDirect3DVertexDeclaration9*, DWORD) override { return kInvalidCall; } // 85
    HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(CONST D3DVERTEXELEMENT9*, IDirect3DVertexDeclaration9**) override { return kInvalidCall; } // 86
    HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9*) override { return kInvalidCall; } // 87
    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9**) override { return kInvalidCall; } // 88
    HRESULT STDMETHODCALLTYPE SetFVF(DWORD) override { return kInvalidCall; } // 89
    HRESULT STDMETHODCALLTYPE GetFVF(DWORD*) override { return kInvalidCall; } // 90
    HRESULT STDMETHODCALLTYPE CreateVertexShader(CONST DWORD*, IDirect3DVertexShader9**) override { return kInvalidCall; } // 91
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9*) override { return kInvalidCall; } // 92
    HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9**) override { return kInvalidCall; } // 93
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT, CONST float*, UINT) override { return kInvalidCall; } // 94
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT, float*, UINT) override { return kInvalidCall; } // 95
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT, CONST int*, UINT) override { return kInvalidCall; } // 96
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantI(UINT, int*, UINT) override { return kInvalidCall; } // 97
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT, CONST BOOL*, UINT) override { return kInvalidCall; } // 98
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantB(UINT, BOOL*, UINT) override { return kInvalidCall; } // 99
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT, IDirect3DVertexBuffer9*, UINT, UINT) override { return kInvalidCall; } // 100
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT, IDirect3DVertexBuffer9**, UINT*, UINT*) override { return kInvalidCall; } // 101
    HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT, UINT) override { return kInvalidCall; } // 102
    HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT, UINT*) override { return kInvalidCall; } // 103
    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9*) override { return kInvalidCall; } // 104
    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9**) override { return kInvalidCall; } // 105
    HRESULT STDMETHODCALLTYPE CreatePixelShader(CONST DWORD*, IDirect3DPixelShader9**) override { return kInvalidCall; } // 106
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9*) override { return kInvalidCall; } // 107
    HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9**) override { return kInvalidCall; } // 108
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT, CONST float*, UINT) override { return kInvalidCall; } // 109
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT, float*, UINT) override { return kInvalidCall; } // 110
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT, CONST int*, UINT) override { return kInvalidCall; } // 111
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantI(UINT, int*, UINT) override { return kInvalidCall; } // 112
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT, CONST BOOL*, UINT) override { return kInvalidCall; } // 113
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantB(UINT, BOOL*, UINT) override { return kInvalidCall; } // 114
    HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT, CONST float*, CONST D3DRECTPATCH_INFO*) override { return kInvalidCall; } // 115
    HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT, CONST float*, CONST D3DTRIPATCH_INFO*) override { return kInvalidCall; } // 116
    HRESULT STDMETHODCALLTYPE DeletePatch(UINT) override { return kInvalidCall; } // 117
    HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE, IDirect3DQuery9**) override { return kInvalidCall; } // 118
};
static_assert(sizeof(cNullDevice) == 4, "cNullDevice size");

} // namespace nGZGraphic4
