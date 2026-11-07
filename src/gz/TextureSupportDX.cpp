// The texture support checks 0x18c7f0 / 0x18df30 (lane RJ, O-c2; gz/TextureSupportDX.h).
#include "gz/TextureSupportDX.h"

#include "rz/MiscHelpersA.h" // LogD3DError

#include <cstdint>

namespace nGZGraphic4 {

namespace {

// The report lines of one check: GetDevice, GetLevelDesc, GetCreationParameters, GetDisplayMode, GetDirect3D and
// CheckDeviceFormat.
struct Lines {
    uint32_t device, desc, creation, mode, d3d, check;
};

// Releases what it holds when the check ends, a report's exception included (the original's unwind does the same).
template <class T> struct Held {
    T *p = nullptr;
    ~Held()
    {
        if (p) p->Release();
    }
};

void Report(HRESULT hr, const char *file, uint32_t line)
{
    LogD3DError(static_cast<uint32_t>(hr), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(file)), line);
}

template <class Texture> bool FilterSupported(Texture *texture, const char *file, const Lines &lines)
{
    Held<IDirect3DDevice9> device;
    HRESULT hr = texture->GetDevice(&device.p);
    if (FAILED(hr)) Report(hr, file, lines.device);
    D3DSURFACE_DESC desc;
    hr = texture->GetLevelDesc(0, &desc);
    if (FAILED(hr)) Report(hr, file, lines.desc);
    D3DDEVICE_CREATION_PARAMETERS creation;
    hr = device.p->GetCreationParameters(&creation);
    if (FAILED(hr)) Report(hr, file, lines.creation);
    D3DDISPLAYMODE mode;
    hr = device.p->GetDisplayMode(0, &mode);
    if (FAILED(hr)) Report(hr, file, lines.mode);
    Held<IDirect3D9> d3d;
    hr = device.p->GetDirect3D(&d3d.p);
    if (FAILED(hr)) Report(hr, file, lines.d3d);
    const D3DRESOURCETYPE type = texture->GetType();
    hr = d3d.p->CheckDeviceFormat(creation.AdapterOrdinal, creation.DeviceType, mode.Format,
                                  desc.Usage | D3DUSAGE_QUERY_FILTER, type, desc.Format);
    if (hr != D3DERR_NOTAVAILABLE && FAILED(hr)) Report(hr, file, lines.check);
    return hr != D3DERR_NOTAVAILABLE;
}

} // namespace

bool __cdecl TextureFilterSupported(IDirect3DTexture9 *texture)
{
    return FilterSupported(texture, ts2_rva_da0e78, {0xf3, 0xf6, 0xf9, 0xfc, 0xff, 0x10b});
}

bool __cdecl CubeTextureFilterSupported(IDirect3DCubeTexture9 *texture)
{
    return FilterSupported(texture, ts2_rva_da10d8, {0xd8, 0xdb, 0xde, 0xe1, 0xe4, 0xf0});
}

} // namespace nGZGraphic4
