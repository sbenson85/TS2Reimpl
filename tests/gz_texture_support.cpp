// Differential test for the texture support checks 0x18c7f0 / 0x18df30 (lane RJ, src/gz/TextureSupportDX.cpp): whether
// level 0's format of a texture (a cube texture) can be filtered, through its device's IDirect3D9::CheckDeviceFormat.
//
// Fakes (tests/d3dfx.h's drawfx COM fakes): the texture or cube texture, its device and the device's IDirect3D9. Every
// call is logged with its arguments (out-parameters by where they point: the caller's stack). GetDevice, GetLevelDesc,
// GetCreationParameters, GetDisplayMode and GetDirect3D write their out-parameters whatever they answer (the original
// goes on after a failure and uses them; an unwritten one would be the stack's garbage on each side) and answer the
// case's HRESULT: success, S_FALSE, or one of three failures. GetType answers the case's resource type;
// CheckDeviceFormat answers 0, S_FALSE, D3DERR_NOTAVAILABLE, a positive code, or a failure. LogD3DError's reports are
// notes in the same log (d3dfx::ErrorLog). The level description, the creation parameters and the display mode are
// seeded words. Compared: the log, the result, and the reference counts of the three fakes after the call.
#include "d3dfx.h"
#include "difftest.h"

#include "gz/TextureSupportDX.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kTextureId = 0x7e000001, kDeviceId = 0x7e000002, kD3DId = 0x7e000003;
constexpr uint32_t kHResults[] = {0, 1, 0x80004005u, 0x8876086cu, 0x8007000eu};
constexpr uint32_t kChecks[] = {0, 1, 0x8876086au, 0x8876086au, 0x7fff0000u, 0x80004005u, 0x8876086cu};

struct Case {
    bool cube = false;
    uint32_t hr[5]{}; // GetDevice, GetLevelDesc, GetCreationParameters, GetDisplayMode, GetDirect3D
    uint32_t check = 0, type = 0;
    D3DSURFACE_DESC desc{};
    D3DDEVICE_CREATION_PARAMETERS creation{};
    D3DDISPLAYMODE mode{};
};
const Case *gCase = nullptr;

drawfx::Fake gTexture, gDevice, gD3D;

template <class T> T *At(uint32_t address) { return reinterpret_cast<T *>(uintptr_t(address)); }

uint32_t TextureDevice(drawfx::Fake &, const uint32_t *a)
{
    ++gDevice.refs;
    *At<void *>(a[0]) = &gDevice;
    return gCase->hr[0];
}
uint32_t TextureDesc(drawfx::Fake &, const uint32_t *a)
{
    std::memcpy(At<void>(a[1]), &gCase->desc, sizeof(D3DSURFACE_DESC));
    return gCase->hr[1];
}
uint32_t DeviceCreation(drawfx::Fake &, const uint32_t *a)
{
    std::memcpy(At<void>(a[0]), &gCase->creation, sizeof(D3DDEVICE_CREATION_PARAMETERS));
    return gCase->hr[2];
}
uint32_t DeviceMode(drawfx::Fake &, const uint32_t *a)
{
    std::memcpy(At<void>(a[1]), &gCase->mode, sizeof(D3DDISPLAYMODE));
    return gCase->hr[3];
}
uint32_t DeviceD3D(drawfx::Fake &, const uint32_t *a)
{
    ++gD3D.refs;
    *At<void *>(a[0]) = &gD3D;
    return gCase->hr[4];
}

void BuildFakes(const Case &c)
{
    if (c.cube)
        gTexture.Reset<fx_d3d9::IDirect3DCubeTexture9>(kTextureId);
    else
        gTexture.Reset<fx_d3d9::IDirect3DTexture9>(kTextureId);
    gTexture.On(d3dfx::res::GetDevice, &TextureDevice);
    gTexture.On(d3dfx::res::GetLevelDesc, &TextureDesc);
    gTexture.Returns(d3dfx::res::GetType, c.type);
    gDevice.Reset<fx_d3d9::IDirect3DDevice9>(kDeviceId);
    gDevice.On(fx_d3d9::dev::GetCreationParameters, &DeviceCreation);
    gDevice.On(fx_d3d9::dev::GetDisplayMode, &DeviceMode);
    gDevice.On(fx_d3d9::dev::GetDirect3D, &DeviceD3D);
    gD3D.Reset<fx_d3d9::IDirect3D9>(kD3DId);
    gD3D.Returns(10, c.check); // CheckDeviceFormat
}

Case Seed(std::mt19937 &gen)
{
    Case c;
    c.cube = gen() % 2 != 0;
    for (uint32_t &hr : c.hr) hr = gen() % 4 != 0 ? 0 : kHResults[gen() % 5];
    c.check = kChecks[gen() % (sizeof kChecks / sizeof kChecks[0])];
    c.type = gen() % 3 == 0 ? gen() : (gen() % 2 ? D3DRTYPE_TEXTURE : D3DRTYPE_CUBETEXTURE);
    c.desc.Format = static_cast<D3DFORMAT>(gen() % 3 == 0 ? gen() : 20 + gen() % 60);
    c.desc.Type = static_cast<D3DRESOURCETYPE>(gen() % 8);
    c.desc.Usage = gen() % 3 == 0 ? 0 : (gen() % 2 ? gen() : (1u << (gen() % 32)));
    c.desc.Pool = static_cast<D3DPOOL>(gen() % 4);
    c.desc.MultiSampleType = static_cast<D3DMULTISAMPLE_TYPE>(gen() % 4);
    c.desc.MultiSampleQuality = gen() % 4;
    c.desc.Width = gen() % 4096;
    c.desc.Height = gen() % 4096;
    c.creation.AdapterOrdinal = gen() % 3 == 0 ? gen() : gen() % 4;
    c.creation.DeviceType = static_cast<D3DDEVTYPE>(gen() % 5);
    c.creation.hFocusWindow = reinterpret_cast<HWND>(uintptr_t(gen() & 0xfff0));
    c.creation.BehaviorFlags = gen();
    c.mode.Width = gen() % 4096;
    c.mode.Height = gen() % 4096;
    c.mode.RefreshRate = gen() % 200;
    c.mode.Format = static_cast<D3DFORMAT>(gen() % 3 == 0 ? gen() : 20 + gen() % 60);
    return c;
}

std::string Describe(const drawfx::Log &log)
{
    std::string out;
    char word[16];
    for (const drawfx::Call &call : log) {
        if (call.iface) {
            out += call.iface->name;
            out += '.';
            out += call.slot < call.iface->count ? call.iface->slots[call.slot].name : "?";
        } else {
            out += call.note;
        }
        for (uint32_t w : call.words) {
            std::snprintf(word, sizeof word, " %08x", w);
            out += word;
        }
        out += '|';
    }
    return out;
}

struct Result {
    drawfx::Log log;
    bool supported = false;
    int32_t refs[3]{};
};

Result Side(difftest::Context &ctx, const Case &c, bool original)
{
    drawfx::ResetAll();
    BuildFakes(c);
    gCase = &c;
    Result r;
    if (original) {
        const uint32_t rva = c.cube ? 0x18df30 : 0x18c7f0;
        r.supported = ctx.Original<bool(__cdecl *)(void *)>(rva)(&gTexture);
    } else if (c.cube) {
        r.supported = nGZGraphic4::CubeTextureFilterSupported(reinterpret_cast<IDirect3DCubeTexture9 *>(&gTexture));
    } else {
        r.supported = nGZGraphic4::TextureFilterSupported(reinterpret_cast<IDirect3DTexture9 *>(&gTexture));
    }
    r.log = drawfx::gLog;
    r.refs[0] = gTexture.refs;
    r.refs[1] = gDevice.refs;
    r.refs[2] = gD3D.refs;
    return r;
}

void Run(difftest::Context &ctx, bool cube)
{
    d3dfx::ErrorLog errors(ctx);
    std::mt19937 gen(cube ? 0x18df30 : 0x18c7f0);
    unsigned reports = 0, unsupported = 0;
    for (unsigned i = 0; i < 1500; ++i) {
        Case c = Seed(gen);
        c.cube = cube;
        const Result a = Side(ctx, c, true);
        const Result b = Side(ctx, c, false);
        ++ctx.cases;
        reports += d3dfx::Notes(a.log, "LogD3DError") != 0;
        unsupported += !a.supported;
        if (a.log != b.log || a.supported != b.supported || std::memcmp(a.refs, b.refs, sizeof a.refs) != 0) {
            ctx.Fail("%s case %u: result %d / %d, refs %d %d %d / %d %d %d\n  original %s\n  ours     %s", cube ? "cube" : "texture",
                     i, a.supported, b.supported, a.refs[0], a.refs[1], a.refs[2], b.refs[0], b.refs[1], b.refs[2],
                     Describe(a.log).c_str(), Describe(b.log).c_str());
            return;
        }
    }
    if (reports == 0 || unsupported == 0) ctx.Fail("%s: a path never ran (reports %u, unsupported %u)", cube ? "cube" : "texture", reports, unsupported);
}

} // namespace

DIFF_TEST(TextureSupport_Texture, 0x18c7f0) { Run(ctx, false); }
DIFF_TEST(TextureSupport_Cube, 0x18df30) { Run(ctx, true); }
