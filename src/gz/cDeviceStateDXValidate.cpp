// nGZGraphic4::cDeviceStateDX slots 27-29 (the shader-validation debug drivers) and the validators they call:
//   slot 27, 0x181f00: can this device run ProcessVertices with the engine's declaration (software vertex processing)?
//   slot 28, 0x182220: is the cached state, as it stands, something the device can draw with (caps, textures,
//                      streams, then a one-triangle trial draw)?
//   slot 29, 0x1829d0: would the device's caps (hardware or software set) allow one state command?
// The validators (0x1865f0..0x189e40, static in the original) are in effect through these slots.
//
// Each reports what failed (only when the caller asks) through the framework's report object, with the messages the
// original carries in the image; the messages, categories and interface ids are read from there (rva table below).
#include <cstring>

#include <algorithm>
#include "game/game.h"
#include "game/owned.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cDeviceEnum.h"
#include "gz/cDeviceStateDX.h"
#include "gz/cDeviceStateDXDetail.h"
#include "gz/TextureSupportDX.h"
#include "gz/cIGZCOM.h"
#include "rz/MiscHelpersA.h"
#include "rz/StaticGetters.h"
#include "rz/cRZStd.h"

// The texts the reports use, owned statics (game/owned.h): the image's in hooked builds, the image-free program's own
// (standalone/owned_graphics.cpp) otherwise. The category, the validators' source files, then the messages.
TS2_OWNED_STATIC(const char, d9fad8)[];
TS2_OWNED_STATIC(const char, d9ff68)[];
TS2_OWNED_STATIC(const char, da00f0)[];
TS2_OWNED_STATIC(const char, da0290)[];
TS2_OWNED_STATIC(const char, da0410)[];
TS2_OWNED_STATIC(const char, da0518)[];
TS2_OWNED_STATIC(const char, da0600)[];
TS2_OWNED_STATIC(const char, da0798)[];
TS2_OWNED_STATIC(const char, da0940)[];
TS2_OWNED_STATIC(const char, d9fc68)[];
TS2_OWNED_STATIC(const char, d9fcb4)[];
TS2_OWNED_STATIC(const char, d9fce0)[];
TS2_OWNED_STATIC(const char, d9fd04)[];
TS2_OWNED_STATIC(const char, d9fd28)[];
TS2_OWNED_STATIC(const char, d9fd4c)[];
TS2_OWNED_STATIC(const char, d9fd70)[];
TS2_OWNED_STATIC(const char, d9fd84)[];
TS2_OWNED_STATIC(const char, d9fdb8)[];
TS2_OWNED_STATIC(const char, d9fe0c)[];
TS2_OWNED_STATIC(const char, d9fe34)[];
TS2_OWNED_STATIC(const char, d9fe58)[];
TS2_OWNED_STATIC(const char, d9fe7c)[];
TS2_OWNED_STATIC(const char, d9fea0)[];
TS2_OWNED_STATIC(const char, d9feac)[];
TS2_OWNED_STATIC(const char, d9feb8)[];
TS2_OWNED_STATIC(const char, d9ffcc)[];
TS2_OWNED_STATIC(const char, d9fff4)[];
TS2_OWNED_STATIC(const char, da001c)[];
TS2_OWNED_STATIC(const char, da0040)[];
TS2_OWNED_STATIC(const char, da0074)[];
TS2_OWNED_STATIC(const char, da0150)[];
TS2_OWNED_STATIC(const char, da0174)[];
TS2_OWNED_STATIC(const char, da01a4)[];
TS2_OWNED_STATIC(const char, da01f0)[];
TS2_OWNED_STATIC(const char, da02fc)[];
TS2_OWNED_STATIC(const char, da0340)[];
TS2_OWNED_STATIC(const char, da0370)[];
TS2_OWNED_STATIC(const char, da03a0)[];
TS2_OWNED_STATIC(const char, da0474)[];
TS2_OWNED_STATIC(const char, da04a0)[];
TS2_OWNED_STATIC(const char, da04c4)[];
TS2_OWNED_STATIC(const char, da0580)[];
TS2_OWNED_STATIC(const char, da0664)[];
TS2_OWNED_STATIC(const char, da07fc)[];
TS2_OWNED_STATIC(const char, da0824)[];
TS2_OWNED_STATIC(const char, da0844)[];
TS2_OWNED_STATIC(const char, da0868)[];
TS2_OWNED_STATIC(const char, da08a0)[];
TS2_OWNED_STATIC(const char, da08d8)[];
TS2_OWNED_STATIC(const char, da09a8)[];
TS2_OWNED_STATIC(const char, da09d4)[];
TS2_OWNED_STATIC(const char, da0a1c)[];
TS2_OWNED_STATIC(const char, da0a44)[];
TS2_OWNED_STATIC(const char, da0a84)[];
TS2_OWNED_STATIC(const char, da0ab0)[];
TS2_OWNED_STATIC(const char, da0af0)[];
// The COM interface ids of the two texture interfaces the validators ask the textures for (IID_IDirect3DTexture9,
// IID_IDirect3DCubeTexture9).
TS2_OWNED_STATIC(const IID, d9c8b8);
TS2_OWNED_STATIC(const IID, d9c8c8);

namespace nGZGraphic4 {

using namespace devstate;
using namespace devstate_detail;

namespace {

static_assert(offsetof(D3DCAPS9, PrimitiveMiscCaps) == 0x20 && offsetof(D3DCAPS9, RasterCaps) == 0x24 &&
                  offsetof(D3DCAPS9, ZCmpCaps) == 0x28 && offsetof(D3DCAPS9, SrcBlendCaps) == 0x2c &&
                  offsetof(D3DCAPS9, DestBlendCaps) == 0x30 && offsetof(D3DCAPS9, AlphaCmpCaps) == 0x34 &&
                  offsetof(D3DCAPS9, ShadeCaps) == 0x38 && offsetof(D3DCAPS9, TextureCaps) == 0x3c,
              "caps layout");
static_assert(offsetof(D3DCAPS9, TextureFilterCaps) == 0x40 && offsetof(D3DCAPS9, CubeTextureFilterCaps) == 0x44 &&
                  offsetof(D3DCAPS9, VolumeTextureFilterCaps) == 0x48 && offsetof(D3DCAPS9, TextureAddressCaps) == 0x4c &&
                  offsetof(D3DCAPS9, VolumeTextureAddressCaps) == 0x50 && offsetof(D3DCAPS9, MaxAnisotropy) == 0x6c &&
                  offsetof(D3DCAPS9, StencilCaps) == 0x88 && offsetof(D3DCAPS9, TextureOpCaps) == 0x90,
              "caps layout");
static_assert(offsetof(D3DCAPS9, MaxTextureBlendStages) == 0x94 && offsetof(D3DCAPS9, MaxSimultaneousTextures) == 0x98 &&
                  offsetof(D3DCAPS9, VertexProcessingCaps) == 0x9c && offsetof(D3DCAPS9, MaxStreams) == 0xbc &&
                  offsetof(D3DCAPS9, MaxStreamStride) == 0xc0 && offsetof(D3DCAPS9, DevCaps2) == 0xd4 &&
                  offsetof(D3DCAPS9, NumSimultaneousRTs) == 0xf0,
              "caps layout");

// What the reports use (owned statics, declared above): the category and the source files of the validators' reports.
constexpr const char *kCategory = ts2_rva_d9fad8;
constexpr const char *kFileTargets = ts2_rva_d9ff68, *kFileRaster = ts2_rva_da00f0, *kFileLighting = ts2_rva_da0290,
                     *kFileBlend = ts2_rva_da0410, *kFileDepth = ts2_rva_da0518, *kFileStencil = ts2_rva_da0600,
                     *kFileTexture = ts2_rva_da0798, *kFileSampler = ts2_rva_da0940;

// Held buffers the slots make (class id, interface id).
constexpr uint32_t kVertexHeldClass = 0x3bffec52, kVertexHeldIid = 0xfbffec42;
constexpr uint32_t kIndexHeldClass = 0x9bfff0ea, kIndexHeldIid = 0xdbfff0de;
constexpr uint32_t kVertexBufferId = 0x0998d7e9, kDeclarationId = 0x8b448b4c;

// cRZStd's report slot as a C++ interface (the same view MiscHelpersA.cpp has of it).
struct ReportView {
    virtual void __cdecl Slot0(const char *, ...) = 0;
    virtual void Slot1() = 0;
    virtual void __cdecl Report(const char *category, uint32_t number, const char *format, ...) = 0;
};

inline uint32_t Word(const char *text) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(text)); }

// A report: the object for (file, line), then the message (with its arguments).
template <class... A> void Say(const char *file, uint32_t line, const char *message, A... args)
{
    cRZStd raw;
    cRZStd *log = raw.Construct(8, Word(file), line);
    reinterpret_cast<ReportView *>(log)->Report(kCategory, 4u, message, args...);
}
// The same, the message carrying the text of a failed result (looked up after the report object is made).
void SayResult(const char *file, uint32_t line, const char *message, uint32_t hr)
{
    cRZStd raw;
    cRZStd *log = raw.Construct(8, Word(file), line);
    reinterpret_cast<ReportView *>(log)->Report(kCategory, 4u, message, QueryServiceValue(hr));
}
void LogResult(HRESULT hr, const char *file, uint32_t line)
{
    if (FAILED(hr)) LogD3DError(static_cast<uint32_t>(hr), Word(file), line);
}

inline bool IsPow2(uint32_t x) { return (x & (x - 1)) == 0; }
inline bool Bit(uint32_t caps, uint32_t bit) { return ((caps >> bit) & 1) != 0; }
inline uint32_t Dword(const uint8_t *c, uint32_t offset)
{
    uint32_t v;
    std::memcpy(&v, c + offset, 4);
    return v;
}

// The engine's texture operation (a byte) -> Direct3D's (0x144ba0's neighbours: tables at 0xda06a0, 0xda072c, 0xda0900).
constexpr uint32_t kOpToD3D[24] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0xb, 0xa, 0x10, 0xc, 0xd, 0xe, 0x12, 0x14, 0x13, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a};
constexpr uint32_t D3DOpCapBit(uint32_t op) { return op == 0 ? 7u : (op <= 26 ? 1u << (op - 1) : 0u); }
constexpr uint32_t kArgToD3D[7] = {1, 5, 2, 0, 4, 3, 0};
inline uint32_t OpOf(uint32_t engineOp) { return engineOp < 24 ? kOpToD3D[engineOp] : 0; }
inline uint32_t ArgOf(uint32_t nibble) { return nibble < 7 ? kArgToD3D[nibble] : 0; }

// --- Command validators (cdecl(command, caps, log) in the original; the command's bytes from +8 are its fields) ---------

bool RenderTargets(const uint8_t *c, const D3DCAPS9 *caps, bool log) // 0x1865f0
{
    cIGZUnknown *const *surface = reinterpret_cast<cIGZUnknown *const *>(c + 8);
    cIGZUnknown *depth = reinterpret_cast<cIGZUnknown *const *>(c + 0x18)[0];
    bool ok = true;
    if (!surface[0]) {
        if (log) Say(kFileTargets, 0x2c, ts2_rva_d9ffcc);
        ok = false;
    }
    for (uint32_t i = 0; i < 4; ++i) {
        if (!surface[i]) continue;
        if (!VirtualCall<bool(__thiscall *)(void *), 6>(surface[i])) {
            if (log) Say(kFileTargets, 0x35, ts2_rva_d9fff4);
            ok = false;
        }
        if (i >= caps->NumSimultaneousRTs) {
            if (log) Say(kFileTargets, 0x3c, ts2_rva_da001c);
            ok = false;
        }
    }
    if (depth && !VirtualCall<bool(__thiscall *)(void *), 6>(depth)) {
        if (log) Say(kFileTargets, 0x44, ts2_rva_da0040);
        return false;
    }
    return ok;
}

bool Scissor(const uint8_t *c, const D3DCAPS9 *caps, bool log) // 0x1867c0
{
    if (c[0x18] != 0 && !(caps->RasterCaps & 0x1000000)) {
        if (log) Say(kFileTargets, 0x6e, ts2_rva_da0074);
        return false;
    }
    return true;
}

bool ColorWriteEnable(const uint8_t *c, const D3DCAPS9 *caps, bool log) // 0x186980
{
    bool ok = true;
    if (!Bit(caps->PrimitiveMiscCaps, 7)) {
        for (uint32_t i = 0; i < 4; ++i)
            if (c[8 + i] != 0xf) {
                ok = false;
                break;
            }
    }
    for (uint32_t i = caps->NumSimultaneousRTs; i < 4; ++i)
        if (c[8 + i] != 0xf) {
            ok = false;
            break;
        }
    if (!ok && log) Say(kFileRaster, 0xd1, ts2_rva_da01a4);
    return ok;
}

bool CullMode(const uint8_t *c, const D3DCAPS9 *caps, bool log) // 0x186b30
{
    const uint32_t mode = c[8];
    if (mode <= 2 && Bit(caps->PrimitiveMiscCaps, 4 + mode)) return true;
    if (log) Say(kFileRaster, 0x3d, ts2_rva_da0150);
    return false;
}

bool Fog(const uint8_t *c, const D3DCAPS9 *caps, bool log) // 0x186ed0
{
    if (!c[8]) return true;
    bool ok = true;
    const uint32_t r = caps->RasterCaps;
    if (c[9]) ok = (r & 0x10000) != 0;
    if (c[0xa]) {
        if (c[0xb] && !(r & 0x100)) ok = false;
    } else {
        if (c[0xb] && !(r & 0x80)) ok = false;
    }
    if (!ok && log) Say(kFileRaster, 0x11d, ts2_rva_da01f0);
    return ok;
}

bool ShadeMode(const uint8_t *, const D3DCAPS9 *caps, bool log) // 0x186fe0: only warns
{
    if (log && (caps->ShadeCaps & 0x84208) != 0x84208) Say(kFileRaster, 0x80, ts2_rva_da0174);
    return true;
}

bool Lighting(const uint8_t *c, const D3DCAPS9 *caps, bool log) // 0x187890
{
    if (c[9] != 0 && !(caps->VertexProcessingCaps & 0x20)) {
        if (log) Say(kFileLighting, 0x2d, ts2_rva_da02fc);
        return false;
    }
    return true;
}

bool TexCoordsSource(const uint8_t *c, const D3DCAPS9 *caps, bool log) // 0x187d20
{
    const uint32_t v = Dword(c, 0xc);
    const bool ok = (v == 0x100 || v == 0x101 || v == 0x102) ? (caps->VertexProcessingCaps & 1) != 0 : v < 8;
    if (!ok && log) Say(kFileLighting, 0x156, ts2_rva_da0340);
    return ok;
}

bool TexTransformType(const uint8_t *c, const D3DCAPS9 *caps, bool log) // 0x187f40
{
    if (c[0xd] != 1) return true;
    const bool ok = Bit(caps->TextureCaps, 10);
    if (!ok && log) Say(kFileLighting, 0x1ad, ts2_rva_da0370);
    return ok;
}

bool AlphaBlend(const uint8_t *c, const D3DCAPS9 *caps, bool log) // 0x1880e0
{
    if (!c[8]) return true;
    const uint32_t src = c[9], dst = c[0xa];
    const bool srcOk = src <= 10 && Bit(caps->SrcBlendCaps, src);
    const bool dstOk = dst <= 10 && Bit(caps->DestBlendCaps, dst);
    const bool opOk = c[0xb] == 0 || (caps->PrimitiveMiscCaps & 0x800) != 0;
    if (srcOk && dstOk && opOk) return true;
    if (log) Say(kFileBlend, 0x66, ts2_rva_da0474);
    return false;
}

bool AlphaTestFunction(const uint8_t *c, const D3DCAPS9 *caps, bool log) // 0x188310
{
    const uint32_t v = c[8];
    const bool ok = v <= 7 && Bit(caps->AlphaCmpCaps, v);
    if (!ok && log) Say(kFileBlend, 0xca, ts2_rva_da04c4);
    return ok;
}

bool AlphaTestState(const uint8_t *c, const D3DCAPS9 *caps, bool log) // 0x188460
{
    if (c[8] != 0 && (caps->AlphaCmpCaps & 0xffffff7e) == 0) {
        if (log) Say(kFileBlend, 0x8f, ts2_rva_da04a0);
        return false;
    }
    return true;
}

bool DepthTestFunction(const uint8_t *c, const D3DCAPS9 *caps, bool log) // 0x188570
{
    const uint32_t v = c[8];
    const bool ok = v <= 7 && Bit(caps->ZCmpCaps, v);
    if (!ok && log) Say(kFileDepth, 0x5c, ts2_rva_da0580);
    return ok;
}

bool StencilOperation(const uint8_t *c, const D3DCAPS9 *caps, bool log) // 0x1888e0
{
    bool ok = true;
    for (uint32_t i = 8; i <= 0xa; ++i)
        if (!(c[i] <= 7 && Bit(caps->StencilCaps, c[i]))) ok = false;
    if (!ok) {
        if (log) Say(kFileStencil, 0xa6, ts2_rva_da0664);
        return false;
    }
    return true;
}

bool SpecularAdd(const uint8_t *c, const D3DCAPS9 *caps, bool log) // 0x188c80
{
    if (c[8] != 0 && !(caps->ShadeCaps & 0x200)) {
        if (log) Say(kFileTexture, 0x13a, ts2_rva_da08d8);
        return false;
    }
    return true;
}

bool TextureBlend(const uint8_t *c, const D3DCAPS9 *caps, bool log) // 0x188ec0
{
    const uint32_t colorOp = OpOf(c[0xd]), alphaOp = OpOf(c[0x11]);
    bool ok;
    if (colorOp == 1 && alphaOp == 1) return true;
    if (colorOp == 1 || alphaOp == 1) {
        ok = false;
    } else {
        ok = true;
        const uint32_t a = c[0xc];
        if (((a & 0xf0) | ArgOf(a & 0xf)) == 5) ok = ok && (caps->PrimitiveMiscCaps & 0x400) != 0;
        ok = ok && (D3DOpCapBit(colorOp) & caps->TextureOpCaps) != 0;
        ok = ok && (D3DOpCapBit(alphaOp) & caps->TextureOpCaps) != 0;
        if (alphaOp - 0x12 <= 3) ok = false;
        for (const uint32_t offset : {0x14u, 0x12u, 0x13u}) {
            const uint32_t arg = c[offset];
            if (((ArgOf(arg & 0xf) | arg) & 0x20) != 0) ok = false;
        }
        if (ok) return true;
    }
    if (log) Say(kFileTexture, 0xc4, ts2_rva_da07fc);
    return false;
}

bool TextureAddressing(const uint8_t *c, const D3DCAPS9 *caps, bool log) // 0x189470
{
    const uint32_t m = caps->TextureAddressCaps | caps->VolumeTextureAddressCaps;
    const uint32_t u = c[0xc], v = c[0xd], w = c[0xe];
    const bool pair = u == v ? true : (m & 0x10) != 0;
    const bool ok = pair && (u <= 3 && Bit(m, u)) && (v <= 3 && Bit(m, v)) && (w <= 3 && Bit(m, w));
    if (!ok && log) Say(kFileSampler, 0x65, ts2_rva_da09a8);
    return ok;
}

bool TextureFilter(const uint8_t *c, const D3DCAPS9 *caps, bool log) // 0x189990
{
    const uint32_t f = caps->VolumeTextureFilterCaps | caps->CubeTextureFilterCaps | caps->TextureFilterCaps;
    const uint32_t minF = c[0xc], magF = c[0xd], aniso = Dword(c, 0x10);
    bool ok = true;
    if (minF == 2 || magF == 2) ok = aniso <= caps->MaxAnisotropy;
    const bool minBit = minF <= 2 && Bit(f, 0x18 + minF);
    const bool magBit = magF <= 2 && Bit(f, 8 + magF);
    ok = ok && minBit && magBit;
    if (!ok && log) Say(kFileSampler, 0x12c, ts2_rva_da0a1c);
    return ok;
}

bool TextureMipFilter(const uint8_t *c, const D3DCAPS9 *caps, bool log) // 0x189d70
{
    const uint32_t bits = Dword(c, 0x10);
    float bias;
    std::memcpy(&bias, &bits, 4);
    bool ok = true;
    if (bias != 0.0f) ok = (caps->RasterCaps & 0x2000) != 0;
    const bool mips = (caps->TextureCaps & 0x1c000) != 0;
    const uint32_t f = caps->VolumeTextureFilterCaps | caps->CubeTextureFilterCaps | caps->TextureFilterCaps;
    switch (c[0xc]) {
    case 0: break;
    case 1: ok = ok && mips && (f & 0x10000) != 0; break;
    case 2: ok = ok && mips && (f & 0x20000) != 0; break;
    default: ok = false; break;
    }
    if (!ok && log) Say(kFileSampler, 0x1e6, ts2_rva_da0a84);
    return ok;
}

// --- Validators of the cached state (cdecl(state, caps, log)) -----------------------------------------------------------

IDirect3DTexture9 *AsTexture(IDirect3DBaseTexture9 *texture)
{
    IDirect3DTexture9 *out = nullptr;
    texture->QueryInterface(ts2_rva_d9c8b8, reinterpret_cast<void **>(&out));
    return out;
}
IDirect3DCubeTexture9 *AsCube(IDirect3DBaseTexture9 *texture)
{
    IDirect3DCubeTexture9 *out = nullptr;
    texture->QueryInterface(ts2_rva_d9c8c8, reinterpret_cast<void **>(&out));
    return out;
}
// The CheckDeviceFormat-based support checks (0x18c7f0, 0x18df30): gz/TextureSupportDX.h.
using nGZGraphic4::CubeTextureFilterSupported;
using nGZGraphic4::TextureFilterSupported;

bool VertexShaderDependent(cDeviceStateDX *state, const D3DCAPS9 *, bool log) // 0x187fa0
{
    if (!state->mWanted.vertexShader) return true;
    const uint32_t bad = state->mWanted.renderStates[11] & 0xfffffeff;
    if (bad != 0 && log) Say(kFileLighting, 0x1c0, ts2_rva_da03a0);
    return bad == 0;
}

bool StageChain(cDeviceStateDX *state, const D3DCAPS9 *caps, bool log) // 0x189010
{
    const DeviceState &w = state->mWanted;
    if (w.pixelShader) return true;
    bool ok = true;
    uint32_t used = 8;
    for (uint32_t i = 0; i < 8; ++i)
        if (w.stageStates[i][0] == 1 || w.stageStates[i][4] == 1) {
            used = i;
            break;
        }
    if (used > caps->MaxTextureBlendStages) {
        if (log) Say(kFileTexture, 0xe3, ts2_rva_da0824);
        ok = false;
    }
    if (used != 0 && w.stageStates[used - 1][8] == 5) {
        if (log) Say(kFileTexture, 0xeb, ts2_rva_da0844);
        ok = false;
    }
    for (uint32_t i = used; i < 8; ++i) {
        if (w.stageStates[i][0] == 1 && w.stageStates[i][4] == 1) continue;
        if (log) Say(kFileTexture, 0xf8, ts2_rva_da0868);
        if (log) Say(kFileTexture, 0x100, ts2_rva_da08a0);
        return false;
    }
    if (!ok && log) Say(kFileTexture, 0x100, ts2_rva_da08a0);
    return ok;
}

bool NonPow2Textures(cDeviceStateDX *state, const D3DCAPS9 *caps, bool log) // 0x189240
{
    const DeviceState &w = state->mWanted;
    for (uint32_t i = 0; i < 16; ++i) {
        IDirect3DBaseTexture9 *texture = w.textures[i];
        if (!texture) continue;
        if (texture->GetType() != D3DRTYPE_TEXTURE || !(caps->TextureCaps & 2)) continue;
        IDirect3DTexture9 *level = AsTexture(texture);
        D3DSURFACE_DESC desc = {};
        if (level) LogResult(level->GetLevelDesc(0, &desc), kFileSampler, 0x28b);
        const bool pow2 = IsPow2(desc.Width) && IsPow2(desc.Height);
        const bool fine = pow2 || !(caps->TextureCaps & 0x100) || w.renderStates[1 + i] == 0;
        if (level) level->Release();
        if (fine) continue;
        if (log) Say(kFileSampler, 0x29e, ts2_rva_da0af0);
        return false;
    }
    return true;
}

bool SamplerAddressing(cDeviceStateDX *state, const D3DCAPS9 *caps, bool log) // 0x1895c0
{
    const DeviceState &w = state->mWanted;
    bool flag = true;
    for (uint32_t i = 0; i < 16; ++i) {
        IDirect3DBaseTexture9 *texture = w.textures[i];
        if (!texture) continue;
        const uint32_t u = w.samplerStates[i][0], v = w.samplerStates[i][1], x = w.samplerStates[i][2];
        if (texture->GetType() == D3DRTYPE_CUBETEXTURE) {
            if (u == 3 && v == 3 && x == 3) continue;
            flag = false;
            break;
        }
        if (texture->GetType() == D3DRTYPE_TEXTURE && (caps->TextureCaps & 2)) {
            IDirect3DTexture9 *level = AsTexture(texture);
            D3DSURFACE_DESC desc = {};
            if (level) LogResult(level->GetLevelDesc(0, &desc), kFileSampler, 0x93);
            const bool pow2 = IsPow2(desc.Width) && IsPow2(desc.Height);
            if (level) level->Release();
            if (!pow2) {
                if (!(caps->TextureCaps & 0x100)) continue;
                if (u == 3 && v == 3) continue;
                flag = false;
                break;
            }
        }
        uint32_t m = 0;
        if (texture->GetType() == D3DRTYPE_TEXTURE)
            m = caps->TextureAddressCaps;
        // ORIGINAL-BUG: the volume test compares the type with 0x2000 (a cap bit), not 4 (D3DRTYPE_VOLUMETEXTURE), so a
        // volume texture gets no address caps and fails.
        else if (texture->GetType() == 0x2000)
            m = caps->VolumeTextureAddressCaps;
        bool now = flag;
        if (u != v) now = now && (m & 0x10) != 0;
        const auto allowed = [m](uint32_t mode) { return (mode - 1) <= 3 && Bit(m, mode - 1); };
        flag = now && allowed(u) && allowed(v) && allowed(x);
    }
    if (!flag && log) Say(kFileSampler, 0xdf, ts2_rva_da09d4);
    return flag;
}

bool SamplerFilter(cDeviceStateDX *state, const D3DCAPS9 *caps, bool log) // 0x189a70
{
    const DeviceState &w = state->mWanted;
    bool flag = true;
    for (uint32_t i = 0; i < 16; ++i) {
        IDirect3DBaseTexture9 *texture = w.textures[i];
        if (!texture) continue;
        const uint32_t minF = w.samplerStates[i][4], magF = w.samplerStates[i][5], aniso = w.samplerStates[i][9];
        uint32_t f = 0x1000100;
        switch (texture->GetType()) {
        case D3DRTYPE_TEXTURE: {
            IDirect3DTexture9 *level = AsTexture(texture);
            if (TextureFilterSupported(level)) {
                f = caps->TextureFilterCaps;
                D3DSURFACE_DESC desc = {};
                LogResult(level->GetLevelDesc(0, &desc), kFileSampler, 0x153);
            }
            if (level) level->Release();
            break;
        }
        case D3DRTYPE_VOLUMETEXTURE: f = caps->VolumeTextureFilterCaps; break;
        case D3DRTYPE_CUBETEXTURE: {
            IDirect3DCubeTexture9 *cube = AsCube(texture);
            if (CubeTextureFilterSupported(cube)) f = caps->CubeTextureFilterCaps;
            if (cube) cube->Release();
            break;
        }
        default: return false;
        }
        if (minF == 3 || magF == 3) flag = flag && !(aniso > caps->MaxAnisotropy);
        const bool minBit = (minF - 1) <= 2 && Bit(f, 0x18 + minF - 1);
        const bool magBit = (magF - 1) <= 2 && Bit(f, 8 + magF - 1);
        flag = flag && minBit && magBit;
    }
    if (!flag && log) Say(kFileSampler, 0x19b, ts2_rva_da0a44);
    return flag;
}

bool SamplerMipFilter(cDeviceStateDX *state, const D3DCAPS9 *caps, bool log) // 0x189e40
{
    const DeviceState &w = state->mWanted;
    bool flag = true;
    for (uint32_t i = 0; i < 16; ++i) {
        IDirect3DBaseTexture9 *texture = w.textures[i];
        if (!texture) continue;
        bool mips = false;
        uint32_t f = 0x10000;
        switch (texture->GetType()) {
        case D3DRTYPE_TEXTURE: {
            IDirect3DTexture9 *level = AsTexture(texture);
            D3DSURFACE_DESC desc = {};
            if (level) LogResult(level->GetLevelDesc(0, &desc), kFileSampler, 0x206);
            if ((IsPow2(desc.Width) && IsPow2(desc.Height)) || !(caps->TextureCaps & 0x100)) {
                mips = Bit(caps->TextureCaps, 14);
                if (TextureFilterSupported(level)) f = caps->TextureFilterCaps;
            }
            if (level) level->Release();
            break;
        }
        case D3DRTYPE_VOLUMETEXTURE:
            mips = Bit(caps->TextureCaps, 15);
            f = caps->VolumeTextureFilterCaps;
            break;
        case D3DRTYPE_CUBETEXTURE: {
            mips = Bit(caps->TextureCaps, 16);
            IDirect3DCubeTexture9 *cube = AsCube(texture);
            if (CubeTextureFilterSupported(cube)) f = caps->CubeTextureFilterCaps;
            if (cube) cube->Release();
            break;
        }
        default: return false;
        }
        bool ok;
        switch (w.samplerStates[i][6]) {
        case 0: ok = true; break;
        case 1: ok = mips && (f & 0x10000) != 0; break;
        case 2: ok = mips && (f & 0x20000) != 0; break;
        default: ok = false; break;
        }
        flag = flag && ok;
    }
    if (!flag && log) Say(kFileSampler, 0x248, ts2_rva_da0ab0);
    return flag;
}

// The command validators by command type (slot 29), null for the types that always pass.
using CommandValidator = bool (*)(const uint8_t *, const D3DCAPS9 *, bool);
CommandValidator CommandValidatorFor(uint32_t type)
{
    switch (type) {
    case 0: return RenderTargets;
    case 4: return CullMode;
    case 6: return ShadeMode;
    case 7: return ColorWriteEnable;
    case 8: return Fog;
    case 9: return Lighting;
    case 0xe: return TexCoordsSource;
    case 0x10: return TexTransformType;
    case 0x11: return AlphaBlend;
    case 0x12: return AlphaTestState;
    case 0x13: return AlphaTestFunction;
    case 0x15: return DepthTestFunction;
    case 0x18: return StencilOperation;
    case 0x19: return TextureBlend;
    case 0x1b: return SpecularAdd;
    case 0x1c: return TextureAddressing;
    case 0x1e: return TextureFilter;
    case 0x1f: return TextureMipFilter;
    case 0x20: return Scissor;
    default: return nullptr;
    }
}

// Makes a new held buffer for slot 27 or 28 (the caller found the old one missing or unusable).
cIGZUnknown *CreateHeld(cIGZUnknown *&slot, uint32_t classId, uint32_t iid)
{
    cIGZCOM *com = static_cast<cIGZCOM *>(GetDirectorFrameworkObject());
    if (slot) {
        cIGZUnknown *old = slot;
        slot = nullptr;
        old->Release();
    }
    com->GetClassObject(classId, iid, reinterpret_cast<void **>(&slot));
    return slot;
}

} // namespace

// Slot 27 (0x181f00): ProcessVertices into a buffer the device must be able to write (software vertex processing).
bool cDeviceStateDX::CheckVertexProcessing(cIGZUnknown *declaration, bool log)
{
    D3DDEVICE_CREATION_PARAMETERS params = {};
    Report(mD3D->GetCreationParameters(&params), 0xb5d);
    if ((params.BehaviorFlags & 0xa0) == 0) {
        if (log) Say(ts2_rva_d9fba8, 0xb62, ts2_rva_d9fc68);
        return false;
    }
    if (mWanted.softwareVertexProcessing == 0) {
        if (log) Say(ts2_rva_d9fba8, 0xb69, ts2_rva_d9fcb4);
        return false;
    }
    cIGZUnknown *held = mHeld9a14;
    if (!(held && VirtualCall<bool(__thiscall *)(void *), 6>(held))) {
        held = CreateHeld(mHeld9a14, kVertexHeldClass, kVertexHeldIid);
        const bool made = VirtualCall<bool(__thiscall *)(void *, void *, uint32_t, uint32_t), 20>(held, mDevice, 0x400u, 2u);
        if (!made) {
            if (log) Say(ts2_rva_d9fba8, 0xb74, ts2_rva_d9fce0);
            return false;
        }
    }
    if (mPending.Any()) Flush(7);
    for (uint32_t s = 0; s < 8; ++s) {
        if (mWanted.stageStates[s][10] != s) Report(mD3D->SetTextureStageState(s, D3DTSS_TEXCOORDINDEX, s), 0x6f2);
        if (mWanted.stageStates[s][11] != 0) Report(mD3D->SetTextureStageState(s, D3DTSS_TEXTURETRANSFORMFLAGS, 0), 0x6f7);
    }
    if (mWanted.clipping != 0) Report(mD3D->SetRenderState(D3DRS_CLIPPING, 0), 0x6fd);
    // ORIGINAL-BUG: the two interfaces the queries return are never released.
    IDirect3DVertexBuffer9 *buffer = nullptr;
    IDirect3DVertexDeclaration9 *decl = nullptr;
    mHeld9a14->QueryInterface(kVertexBufferId, reinterpret_cast<void **>(&buffer));
    declaration->QueryInterface(kDeclarationId, reinterpret_cast<void **>(&decl));
    const HRESULT hr = mD3D->ProcessVertices(0, 0, 1, buffer, decl, 0x800);
    if (FAILED(hr) && log) SayResult(ts2_rva_d9fba8, 0xb84, ts2_rva_d9fd04, static_cast<uint32_t>(hr));
    for (uint32_t s = 0; s < 8; ++s) {
        if (mWanted.stageStates[s][10] != s)
            Report(mD3D->SetTextureStageState(s, D3DTSS_TEXCOORDINDEX, mWanted.stageStates[s][10]), 0x708);
        if (mWanted.stageStates[s][11] != 0)
            Report(mD3D->SetTextureStageState(s, D3DTSS_TEXTURETRANSFORMFLAGS, mWanted.stageStates[s][11]), 0x70d);
    }
    if (mWanted.clipping != 0) Report(mD3D->SetRenderState(D3DRS_CLIPPING, mWanted.clipping), 0x713);
    return !FAILED(hr);
}

// Slot 28 (0x182220): the whole cached state against the device's current caps, then a one-triangle trial draw.
bool cDeviceStateDX::Validate(bool log)
{
    const char *const kFile = ts2_rva_d9fba8;
    Flush(kFlushAll);
    cIDeviceEnum *enumeration = static_cast<cIDeviceEnum *>(mDeviceEnum);
    const D3DCAPS9 *caps = enumeration->GetCurrentCaps();
    bool flag = true;
    const auto step = [&](bool (*validator)(cDeviceStateDX *, const D3DCAPS9 *, bool)) {
        const bool result = validator(this, caps, log);
        flag = flag && result;
    };
    step(VertexShaderDependent);
    step(StageChain);
    step(SamplerAddressing);
    step(NonPow2Textures);
    step(SamplerFilter);
    step(SamplerMipFilter);

    uint32_t textures = 0;
    for (uint32_t i = 0; i < 16; ++i) {
        IDirect3DBaseTexture9 *texture = mWanted.textures[i];
        if (!texture) continue;
        ++textures;
        const D3DRESOURCETYPE type = texture->GetType();
        if (type == D3DRTYPE_TEXTURE) continue;
        if (type == D3DRTYPE_VOLUMETEXTURE) {
            if (!(caps->TextureCaps & 0x2000)) {
                if (log) Say(kFile, 0xbc7, ts2_rva_d9fd4c);
                flag = false;
            }
        } else if (type == D3DRTYPE_CUBETEXTURE) {
            if (!(caps->TextureCaps & 0x800)) {
                if (log) Say(kFile, 0xbbe, ts2_rva_d9fd28);
                flag = false;
            }
        } else {
            return false;
        }
    }
    if (!mWanted.pixelShader && textures > caps->MaxSimultaneousTextures) {
        if (log) Say(kFile, 0xbd6, ts2_rva_d9fd70);
        flag = false;
    }

    uint32_t streams = 0, maxStride = 0, anyOffset = 0;
    for (uint32_t i = 0; i < 16; ++i) {
        const StreamSource &stream = mWanted.streams[i];
        if (stream.buffer) ++streams;
        if (stream.stride > maxStride) maxStride = stream.stride;
        anyOffset |= stream.offset;
        if (stream.buffer && mWanted.softwareVertexProcessing != 0) {
            D3DVERTEXBUFFER_DESC desc = {};
            const HRESULT hr = stream.buffer->GetDesc(&desc);
            if (FAILED(hr)) LogResult(hr, kFile, 0xbef);
            if (!(desc.Usage & D3DUSAGE_SOFTWAREPROCESSING)) {
                if (log) Say(kFile, 0xbf3, ts2_rva_d9fd84, i);
                flag = false;
            }
        }
    }
    const uint32_t maxStreams = caps->MaxStreams == 0 ? 1 : caps->MaxStreams;
    if (streams > maxStreams || maxStride > caps->MaxStreamStride || (anyOffset != 0 && !(caps->DevCaps2 & 1))) {
        if (log) Say(kFile, 0xbff, ts2_rva_d9fdb8);
        flag = false;
    }

    if (mWanted.indices && mWanted.softwareVertexProcessing != 0) {
        D3DINDEXBUFFER_DESC desc = {};
        const HRESULT hr = mWanted.indices->GetDesc(&desc);
        if (FAILED(hr)) LogResult(hr, kFile, 0xc07);
        if (!(desc.Usage & D3DUSAGE_SOFTWAREPROCESSING)) {
            if (log) Say(kFile, 0xc0b, ts2_rva_d9fe0c);
            return false;
        }
    }
    if (!flag) return false;

    if (!enumeration->HasFeature(1)) {
        DWORD passes = 0;
        const HRESULT hr = mD3D->ValidateDevice(&passes);
        if (hr != 0) {
            if (log) SayResult(kFile, 0xc2a, ts2_rva_d9fe34, static_cast<uint32_t>(hr));
            return false;
        }
    }
    if (mWanted.vertexShader) return true;

    VirtualCall<void(__thiscall *)(void *), 29>(mDevice); // BeginScene
    HRESULT hr;
    if (mWanted.indices) {
        cIGZUnknown *held = mHeld9a18;
        if (!(held && VirtualCall<bool(__thiscall *)(void *), 6>(held))) {
            const uint8_t hardware = enumeration->HasProcessingCap(0);
            held = CreateHeld(mHeld9a18, kIndexHeldClass, kIndexHeldIid);
            const bool made =
                VirtualCall<bool(__thiscall *)(void *, void *, uint32_t, uint32_t), 20>(held, mDevice, 0x400u, hardware ? 1u : 2u);
            if (!made) {
                if (log) Say(kFile, 0xc4b, ts2_rva_d9fe58);
                VirtualCall<void(__thiscall *)(void *), 30>(mDevice); // EndScene
                return false;
            }
            uint32_t *data = VirtualCall<uint32_t *(__thiscall *)(void *, uint32_t, uint32_t, uint32_t), 15>(held, 1u, 0u, 0u);
            std::fill_n(data, 0x100, 0u);
            VirtualCall<void(__thiscall *)(void *), 16>(held);
        }
        SetIndices(held);
        Flush(kFlushAll);
        hr = mD3D->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 1, 0, 1);
    } else {
        Flush(kFlushAll);
        hr = mD3D->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 1);
    }
    if (hr != 0 && log) SayResult(kFile, 0xc64, ts2_rva_d9fe7c, static_cast<uint32_t>(hr));
    VirtualCall<void(__thiscall *)(void *), 30>(mDevice); // EndScene
    return hr == 0;
}

// Slot 29 (0x1829d0): one state command (the engine's, as the command slot takes it) against the caps of the hardware
// or software processing set.
bool cDeviceStateDX::ValidateCommand(const void *command, bool hardware, bool log)
{
    const D3DCAPS9 *caps = static_cast<cIDeviceEnum *>(mDeviceEnum)->GetCaps(hardware);
    if (!caps) {
        if (log) Say(ts2_rva_d9fba8, 0xc89, ts2_rva_d9feb8, hardware ? ts2_rva_d9fea0 : ts2_rva_d9feac);
        return false;
    }
    const uint8_t *bytes = static_cast<const uint8_t *>(command);
    const CommandValidator validator = CommandValidatorFor(bytes[4]);
    return validator ? validator(bytes, caps, log) : true;
}

} // namespace nGZGraphic4
