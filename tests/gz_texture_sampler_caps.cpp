// Differential tests for the sampler state fitting 0x18cc40 (2D) and 0x18e320 (cube) (lane VU,
// src/gz/TextureSamplerCapsDX.cpp).
//
// Stood in on both sides: the filter support checks 0x18c7f0 / 0x18df30 (note their argument, answer the case's
// support). Fakes: the texture (slot 0 QueryInterface, 4 GetDevice, 17 / 18 level width and height), its device
// (slot 15 the enumeration), the enumeration's cIGZUnknown face (slot 0 QueryInterface, answering the cIDeviceEnum) and
// the cIDeviceEnum (slot 24 the caps, 2 Release). Every other slot of the five notes its index (and would fail the
// comparison). The caps are seeded raw words (MaxAnisotropy from a small set), so each filter, mip and raster bit is
// on or off; the states' filters span 0-3 and odd bytes, the anisotropy 0, 1, small and raw words, the bias 0, -0,
// numbers, quiet and signalling NaNs; the level sizes powers of two, 0 and others. Compared: the call log with its
// arguments, and the two states' 0x14 bytes plus a word of slack.
#include "code_patch.h"
#include "difftest.h"

#include "gz/TextureSamplerCapsDX.h"
#include "gz/TextureSupportDX.h"

#include <d3d9.h>

#include <cstdint>
#include <cstring>
#include <random>
#include <utility>
#include <vector>

namespace {

struct Case {
    bool supported;
    uint32_t width, height;
    D3DCAPS9 caps;
};
const Case *gCase = nullptr;
std::vector<uint32_t> gLog;

struct Fake {
    const void *const *vtable;
};
Fake gTex, gDev, gEnumUnk, gEnum, gD3D;

// Who: 1 the texture, 2 the device, 3 the enumeration's face, 4 the cIDeviceEnum, 5 the Direct3D texture token.
uint32_t Who(const void *self)
{
    return self == &gTex ? 1 : self == &gDev ? 2 : self == &gEnumUnk ? 3 : self == &gEnum ? 4 : self == &gD3D ? 5 : 0;
}

template <unsigned N> uint32_t __fastcall Unexpected(void *self, void *)
{
    gLog.insert(gLog.end(), {0xdead0000u | N, Who(self)});
    return 0;
}

bool __fastcall TexQuery(void *self, void *, uint32_t iid, void **out)
{
    gLog.insert(gLog.end(), {0x100, Who(self), iid, out != nullptr});
    *out = &gD3D;
    return true;
}
void *__fastcall TexDevice(void *self, void *)
{
    gLog.insert(gLog.end(), {0x104, Who(self)});
    return &gDev;
}
uint32_t __fastcall TexWidth(void *self, void *, uint32_t level)
{
    gLog.insert(gLog.end(), {0x117, Who(self), level});
    return gCase->width;
}
uint32_t __fastcall TexHeight(void *self, void *, uint32_t level)
{
    gLog.insert(gLog.end(), {0x118, Who(self), level});
    return gCase->height;
}
void *__fastcall DevEnum(void *self, void *)
{
    gLog.insert(gLog.end(), {0x215, Who(self)});
    return &gEnumUnk;
}
bool __fastcall EnumQuery(void *self, void *, uint32_t iid, void **out)
{
    gLog.insert(gLog.end(), {0x300, Who(self), iid, out != nullptr, out && *out == nullptr});
    *out = &gEnum;
    return true;
}
const D3DCAPS9 *__fastcall EnumCaps(void *self, void *)
{
    gLog.insert(gLog.end(), {0x424, Who(self)});
    return &gCase->caps;
}
uint32_t __fastcall EnumRelease(void *self, void *)
{
    gLog.insert(gLog.end(), {0x402, Who(self)});
    return 1;
}

bool __cdecl StandSupported(void *d3d)
{
    gLog.insert(gLog.end(), {0x18c7f0, Who(d3d)});
    return gCase->supported;
}
bool __cdecl StandCubeSupported(void *d3d)
{
    gLog.insert(gLog.end(), {0x18df30, Who(d3d)});
    return gCase->supported;
}

template <size_t... I> std::vector<const void *> Unexpecteds(std::index_sequence<I...>)
{
    return {reinterpret_cast<const void *>(&Unexpected<I>)...};
}

std::vector<const void *> gTables[5];

void BuildTables()
{
    for (auto &t : gTables) t = Unexpecteds(std::make_index_sequence<32>());
    gTables[0][0] = reinterpret_cast<const void *>(&TexQuery);
    gTables[0][4] = reinterpret_cast<const void *>(&TexDevice);
    gTables[0][17] = reinterpret_cast<const void *>(&TexWidth);
    gTables[0][18] = reinterpret_cast<const void *>(&TexHeight);
    gTables[1][15] = reinterpret_cast<const void *>(&DevEnum);
    gTables[2][0] = reinterpret_cast<const void *>(&EnumQuery);
    gTables[3][24] = reinterpret_cast<const void *>(&EnumCaps);
    gTables[3][2] = reinterpret_cast<const void *>(&EnumRelease);
    gTex.vtable = gTables[0].data();
    gDev.vtable = gTables[1].data();
    gEnumUnk.vtable = gTables[2].data();
    gEnum.vtable = gTables[3].data();
    gD3D.vtable = gTables[4].data();
}

uint32_t Size(std::mt19937 &rng)
{
    static constexpr uint32_t kSizes[] = {0, 1, 2, 3, 64, 100, 256, 1024, 0x80000000u, 0xffffffffu};
    return rng() % 4 == 0 ? rng() : kSizes[rng() % 10];
}

uint8_t Filter(std::mt19937 &rng) { return rng() % 6 == 0 ? static_cast<uint8_t>(rng()) : static_cast<uint8_t>(rng() % 4); }

Case Seed(std::mt19937 &rng)
{
    static constexpr uint32_t kAniso[] = {0, 1, 2, 4, 16};
    Case c;
    c.supported = rng() % 5 != 0;
    c.width = Size(rng);
    c.height = rng() % 3 == 0 ? c.width : Size(rng);
    auto *words = reinterpret_cast<uint32_t *>(&c.caps);
    for (size_t i = 0; i < sizeof c.caps / 4; ++i) words[i] = rng();
    c.caps.MaxAnisotropy = rng() % 4 == 0 ? rng() : kAniso[rng() % 5];
    return c;
}

void SeedStates(std::mt19937 &rng, uint8_t (&filter)[0x18], uint8_t (&mip)[0x18])
{
    static constexpr uint32_t kAniso[] = {0, 1, 2, 3, 8, 16, 17, 0xffffffffu};
    static constexpr uint32_t kBias[] = {0, 0x80000000u, 0x3fc00000u, 0xbf800000u, 0x7fc00001u, 0x7f800001u, 0xff800000u};
    for (auto &b : filter) b = static_cast<uint8_t>(rng());
    for (auto &b : mip) b = static_cast<uint8_t>(rng());
    filter[0xc] = Filter(rng);
    filter[0xd] = Filter(rng);
    const uint32_t aniso = rng() % 4 == 0 ? rng() : kAniso[rng() % 8];
    std::memcpy(filter + 0x10, &aniso, 4);
    mip[0xc] = Filter(rng);
    const uint32_t bias = rng() % 4 == 0 ? rng() : kBias[rng() % 7];
    std::memcpy(mip + 0x10, &bias, 4);
}

struct Result {
    std::vector<uint32_t> log;
    uint8_t filter[0x18], mip[0x18];
};

using Fn = void(__cdecl *)(nGZGraphic4::cTextureFilterState *, nGZGraphic4::cTextureMipFilterState *,
                           nGZGraphic4::cITextureBase *);

void Run(difftest::Context &ctx, Fn orig, Fn ours, uint32_t seed, bool pow2)
{
    BuildTables();
    std::mt19937 rng(seed);
    unsigned unsupported = 0, supported = 0, npot = 0;
    for (int i = 0; i < 4000; ++i) {
        const Case c = Seed(rng);
        gCase = &c;
        uint8_t filter[0x18], mip[0x18];
        SeedStates(rng, filter, mip);
        Result r[2];
        for (int side = 0; side < 2; ++side) {
            gLog.clear();
            std::memcpy(r[side].filter, filter, sizeof filter);
            std::memcpy(r[side].mip, mip, sizeof mip);
            (side == 0 ? orig : ours)(reinterpret_cast<nGZGraphic4::cTextureFilterState *>(r[side].filter),
                                      reinterpret_cast<nGZGraphic4::cTextureMipFilterState *>(r[side].mip),
                                      reinterpret_cast<nGZGraphic4::cITextureBase *>(&gTex));
            r[side].log = gLog;
        }
        ++ctx.cases;
        (c.supported ? supported : unsupported)++;
        if (c.supported && pow2 && (c.caps.TextureCaps & D3DPTEXTURECAPS_POW2) &&
            ((c.width & (c.width - 1)) || (c.height & (c.height - 1))))
            ++npot;
        if (r[0].log != r[1].log || std::memcmp(r[0].filter, r[1].filter, sizeof filter) != 0 ||
            std::memcmp(r[0].mip, r[1].mip, sizeof mip) != 0) {
            ctx.Fail("case %d (supported %d, size %u x %u): log %zu / %zu words; filter %02x %02x %08x / %02x %02x %08x; "
                     "mip %02x %08x / %02x %08x",
                     i, c.supported, c.width, c.height, r[0].log.size(), r[1].log.size(), r[0].filter[0xc],
                     r[0].filter[0xd], *reinterpret_cast<uint32_t *>(r[0].filter + 0x10), r[1].filter[0xc],
                     r[1].filter[0xd], *reinterpret_cast<uint32_t *>(r[1].filter + 0x10), r[0].mip[0xc],
                     *reinterpret_cast<uint32_t *>(r[0].mip + 0x10), r[1].mip[0xc],
                     *reinterpret_cast<uint32_t *>(r[1].mip + 0x10));
            return;
        }
    }
    if (!supported || !unsupported || (pow2 && !npot))
        ctx.Fail("a path never ran (supported %u, unsupported %u, not a power of two %u)", supported, unsupported, npot);
}

} // namespace

DIFF_TEST(TextureSamplerCaps_Texture, 0x18cc40)
{
    shape::StandIn support{0x18c7f0, &nGZGraphic4::TextureFilterSupported, &StandSupported};
    Run(ctx, ctx.Original<Fn>(0x18cc40), &nGZGraphic4::FitTextureSamplerStates, 0x18cc40, true);
}

DIFF_TEST(TextureSamplerCaps_Cube, 0x18e320)
{
    shape::StandIn support{0x18df30, &nGZGraphic4::CubeTextureFilterSupported, &StandCubeSupported};
    Run(ctx, ctx.Original<Fn>(0x18e320), &nGZGraphic4::FitCubeTextureSamplerStates, 0x18e320, false);
}
