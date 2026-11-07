// Self-test of the shared fake device's resource helpers (tests/d3dfx.h): Creates, surfaces, textures, queries.
// Lane NC added them for cDeviceDX; the resource lanes (R3) reuse them, so they are checked on their own here.
#include "d3dfx.h"
#include "difftest.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr uint32_t kDeviceId = 0x3101, kTextureId = 0x3102, kQueryId = 0x3103, kSurfaceId = 0x3104, kLevelId = 0x3105,
                   kOtherTextureId = 0x3106;

drawfx::Fake gDevice, gTexture, gOtherTexture, gQuery, gSurface, gLevel;

} // namespace

DIFF_TEST(D3DFx_Resources, 0)
{
    drawfx::ResetAll();
    gDevice.Reset<fx_d3d9::IDirect3DDevice9>(kDeviceId);
    d3dfx::SurfaceData texData, surfData;
    d3dfx::QueryData queryData;
    d3dfx::MakeTexture(gTexture, kTextureId, texData);
    gOtherTexture.Reset<fx_d3d9::IDirect3DTexture9>(kOtherTextureId);
    d3dfx::MakeQuery(gQuery, kQueryId, queryData);
    d3dfx::MakeSurface(gSurface, kSurfaceId, surfData);
    gLevel.Reset<fx_d3d9::IDirect3DSurface9>(kLevelId);
    IDirect3DDevice9 *const device = reinterpret_cast<IDirect3DDevice9 *>(&gDevice);

    // Creates: queued fakes handed out in order per slot (AddRef'd), then null and E_OUTOFMEMORY; a programmed
    // failure wins over the queue and writes null.
    d3dfx::Creates creates(gDevice);
    creates.Queue(fx_d3d9::dev::CreateTexture, gTexture);
    creates.Queue(fx_d3d9::dev::CreateTexture, gOtherTexture);
    creates.Queue(fx_d3d9::dev::CreateQuery, gQuery);
    IDirect3DTexture9 *tex = reinterpret_cast<IDirect3DTexture9 *>(uintptr_t(1));
    const int32_t texRefs = gTexture.refs;
    HRESULT hr = device->CreateTexture(16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr);
    if (hr != S_OK || tex != reinterpret_cast<IDirect3DTexture9 *>(&gTexture) || gTexture.refs != texRefs + 1)
        ctx.Fail("CreateTexture: hr %08lx, got %p (want the first queued), refs %d", hr, static_cast<void *>(tex),
                 gTexture.refs - texRefs);
    gDevice.Returns(fx_d3d9::dev::CreateQuery, uint32_t(E_FAIL));
    IDirect3DQuery9 *q = reinterpret_cast<IDirect3DQuery9 *>(uintptr_t(1));
    hr = device->CreateQuery(D3DQUERYTYPE_EVENT, &q);
    if (hr != E_FAIL || q != nullptr || creates.Pending() != 2)
        ctx.Fail("CreateQuery with a programmed failure: hr %08lx, got %p, pending %u", hr, static_cast<void *>(q),
                 creates.Pending());
    gDevice.Returns(fx_d3d9::dev::CreateQuery, 0u);
    hr = device->CreateQuery(D3DQUERYTYPE_EVENT, &q);
    if (hr != S_OK || q != reinterpret_cast<IDirect3DQuery9 *>(&gQuery)) ctx.Fail("CreateQuery: hr %08lx", hr);
    hr = device->CreateTexture(16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr);
    if (hr != S_OK || tex != reinterpret_cast<IDirect3DTexture9 *>(&gOtherTexture))
        ctx.Fail("CreateTexture: the second queued fake not handed out (hr %08lx)", hr);
    tex = reinterpret_cast<IDirect3DTexture9 *>(uintptr_t(1));
    hr = device->CreateTexture(16, 16, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr);
    if (hr != E_OUTOFMEMORY || tex != nullptr || creates.Pending() != 0)
        ctx.Fail("CreateTexture with nothing queued: hr %08lx, got %p", hr, static_cast<void *>(tex));
    // The calls are logged as any slot's.
    unsigned created = 0;
    for (const drawfx::Call &call : drawfx::gLog)
        if (call.object == kDeviceId && (call.slot == fx_d3d9::dev::CreateTexture || call.slot == fx_d3d9::dev::CreateQuery))
            ++created;
    if (created != 5) ctx.Fail("%u Create calls logged, want 5", created);

    // Surfaces: GetDesc and LockRect answer from the data; a programmed failure writes nothing.
    surfData.desc.Width = 33;
    surfData.desc.Format = D3DFMT_D24S8;
    surfData.pitch = 128;
    IDirect3DSurface9 *const surface = reinterpret_cast<IDirect3DSurface9 *>(&gSurface);
    D3DSURFACE_DESC desc{};
    hr = surface->GetDesc(&desc);
    if (hr != S_OK || desc.Width != 33 || desc.Format != D3DFMT_D24S8) ctx.Fail("surface GetDesc: hr %08lx", hr);
    D3DLOCKED_RECT locked{};
    hr = surface->LockRect(&locked, nullptr, 0);
    if (hr != S_OK || locked.pBits != surfData.bits || locked.Pitch != 128) ctx.Fail("surface LockRect: hr %08lx", hr);
    gSurface.Returns(d3dfx::surf::LockRect, uint32_t(D3DERR_INVALIDCALL));
    locked = {};
    hr = surface->LockRect(&locked, nullptr, 0);
    if (hr != D3DERR_INVALIDCALL || locked.pBits != nullptr) ctx.Fail("failed LockRect wrote its out-parameter");

    // Textures: every level describes and locks from the data; GetSurfaceLevel hands out data.level, AddRef'd.
    texData.desc.Height = 77;
    texData.level = &gLevel;
    IDirect3DTexture9 *const texture = reinterpret_cast<IDirect3DTexture9 *>(&gTexture);
    desc = {};
    hr = texture->GetLevelDesc(2, &desc);
    if (hr != S_OK || desc.Height != 77) ctx.Fail("texture GetLevelDesc: hr %08lx", hr);
    locked = {};
    hr = texture->LockRect(1, &locked, nullptr, 0);
    if (hr != S_OK || locked.pBits != texData.bits) ctx.Fail("texture LockRect: hr %08lx", hr);
    const int32_t levelRefs = gLevel.refs;
    IDirect3DSurface9 *level = nullptr;
    hr = texture->GetSurfaceLevel(0, &level);
    if (hr != S_OK || level != reinterpret_cast<IDirect3DSurface9 *>(&gLevel) || gLevel.refs != levelRefs + 1)
        ctx.Fail("texture GetSurfaceLevel: hr %08lx", hr);

    // Queries: S_FALSE while busy, then the result (up to the size asked) and S_OK.
    queryData.busy = 2;
    std::memset(queryData.result, 0x5a, sizeof(queryData.result));
    IDirect3DQuery9 *const query = reinterpret_cast<IDirect3DQuery9 *>(&gQuery);
    uint8_t out[8]{};
    const HRESULT first = query->GetData(out, 4, D3DGETDATA_FLUSH), second = query->GetData(out, 4, D3DGETDATA_FLUSH),
                  third = query->GetData(out, 4, D3DGETDATA_FLUSH);
    if (first != S_FALSE || second != S_FALSE || third != S_OK || out[3] != 0x5a || out[4] != 0)
        ctx.Fail("query GetData: %08lx %08lx %08lx, bytes %02x %02x", first, second, third, out[3], out[4]);
    drawfx::ResetAll();
}
