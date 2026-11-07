#include "gz/TextureSamplerCapsDX.h"

#include <d3d9.h>

#include <bit>
#include <cstdint>

#include "gz/TextureSupportDX.h"
#include "gz/cDeviceDX.h"
#include "gz/cDeviceEnum.h"
#include "gz/cTextureDX.h"

namespace nGZGraphic4 {

namespace {

constexpr uint32_t kIidDeviceEnum = 0x6c02c311; // cIDeviceEnum, as cDeviceResourceManagerDX asks for it

// One filter (0 point, 1 linear, 2 anisotropic) stepped down to what `caps` has; point missing makes it linear.
void FitFilter(uint8_t &filter, uint32_t caps, uint32_t anisotropic, uint32_t linear, uint32_t point)
{
    if (filter == 2 && !(caps & anisotropic)) filter = 1;
    if (filter == 1 && !(caps & linear)) filter = 0;
    if (filter == 0 && !(caps & point)) filter = 1;
}

bool PowerOfTwo(uint32_t n) { return (n & (n - 1)) == 0; } // 0 counts

// The shared body. `filterCaps` picks TextureFilterCaps or CubeTextureFilterCaps, `mipmap` the TextureCaps bit that
// allows mipmapping; `pow2` runs the 2D texture's power-of-two check.
template <class D3DTexture, bool (*Supported)(D3DTexture *)>
void Fit(cTextureFilterState *filter, cTextureMipFilterState *mip, cITextureBase *texture, uint32_t iid,
         DWORD D3DCAPS9::*filterCaps, uint32_t mipmap, bool pow2)
{
    D3DTexture *d3d = nullptr;
    texture->QueryInterface(iid, reinterpret_cast<void **>(&d3d));
    if (!Supported(d3d)) {
        filter->mMinFilter = 0;
        filter->mMagFilter = 0;
        filter->mMaxAnisotropy = 1;
        mip->mMipFilter = 0;
        mip->mLodBias = 0;
        return;
    }
    cIGZUnknown *const enumeration = texture->GetDevice()->GetDeviceEnum();
    cIDeviceEnum *devEnum = nullptr;
    enumeration->QueryInterface(kIidDeviceEnum, reinterpret_cast<void **>(&devEnum));
    const D3DCAPS9 *const caps = devEnum->GetCurrentCaps();

    uint32_t anisotropy = filter->mMaxAnisotropy;
    if (caps->MaxAnisotropy < anisotropy) anisotropy = caps->MaxAnisotropy;
    filter->mMaxAnisotropy = anisotropy < 1 ? 1 : anisotropy;

    const uint32_t fc = caps->*filterCaps;
    FitFilter(filter->mMinFilter, fc, D3DPTFILTERCAPS_MAGFANISOTROPIC, D3DPTFILTERCAPS_MAGFLINEAR,
              D3DPTFILTERCAPS_MAGFPOINT);
    FitFilter(filter->mMagFilter, fc, D3DPTFILTERCAPS_MINFANISOTROPIC, D3DPTFILTERCAPS_MINFLINEAR,
              D3DPTFILTERCAPS_MINFPOINT);

    if (std::bit_cast<float>(mip->mLodBias) != 0.0f && !(caps->RasterCaps & D3DPRASTERCAPS_MIPMAPLODBIAS))
        mip->mLodBias = 0;

    if (!(caps->TextureCaps & mipmap)) mip->mMipFilter = 0;
    if (mip->mMipFilter == 2 && !(fc & D3DPTFILTERCAPS_MIPFLINEAR)) mip->mMipFilter = 1;
    if (mip->mMipFilter == 1 && !(fc & D3DPTFILTERCAPS_MIPFPOINT)) mip->mMipFilter = 0;

    if (pow2 && (caps->TextureCaps & D3DPTEXTURECAPS_POW2) &&
        (!PowerOfTwo(texture->GetWidth(0)) || !PowerOfTwo(texture->GetHeight(0)))) {
        filter->mMaxAnisotropy = 1;
        mip->mLodBias = 0;
        mip->mMipFilter = 0;
    }
    if (devEnum) devEnum->Release();
}

} // namespace

void __cdecl FitTextureSamplerStates(cTextureFilterState *filter, cTextureMipFilterState *mip, cITextureBase *texture)
{
    Fit<IDirect3DTexture9, &TextureFilterSupported>(filter, mip, texture, kIidD3DTexture,
                                                    &D3DCAPS9::TextureFilterCaps, D3DPTEXTURECAPS_MIPMAP, true);
}

void __cdecl FitCubeTextureSamplerStates(cTextureFilterState *filter, cTextureMipFilterState *mip,
                                         cITextureBase *texture)
{
    Fit<IDirect3DCubeTexture9, &CubeTextureFilterSupported>(filter, mip, texture, kIidD3DCubeTexture,
                                                            &D3DCAPS9::CubeTextureFilterCaps,
                                                            D3DPTEXTURECAPS_MIPCUBEMAP, false);
}

} // namespace nGZGraphic4
