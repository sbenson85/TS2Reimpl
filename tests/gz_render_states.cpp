// Differential tests for the nGZGraphic4 render states' two slots (src/gz/RenderStates.h): CloneInto (slot 1), the
// out-of-line copy constructors of the three large states, and the deleting destructors (slot 0, one body per size).
#include "difftest.h"
#include "free_recorder.h"
#include "gz/RenderStates.h"

#include <cstring>
#include <new>
#include <random>

using namespace nGZGraphic4;

namespace {

constexpr size_t kSlack = 16; // bytes past the object that must stay untouched

void Fill(std::mt19937 &rng, uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; ++i) p[i] = static_cast<uint8_t>(rng());
}

// CloneInto(dest) on a random source (every byte random, the padding and the vtable word included) into a random
// destination with slack after it: the whole destination and the source are compared afterwards.
template <class State> void CheckCloneInto(difftest::Context &ctx, uint32_t rva, uint32_t seed)
{
    std::mt19937 rng(seed);
    auto orig = ctx.Original<void(__thiscall *)(const void *, void *)>(rva);
    constexpr size_t n = sizeof(State);
    for (int i = 0; i < 20000; ++i) {
        alignas(16) uint8_t src[2][n], dst[2][n + kSlack];
        Fill(rng, src[0], n);
        Fill(rng, dst[0], n + kSlack);
        std::memcpy(src[1], src[0], n);
        std::memcpy(dst[1], dst[0], n + kSlack);
        orig(src[0], dst[0]);
        reinterpret_cast<const State *>(src[1])->CloneInto(dst[1]);
        ++ctx.cases;
        if (std::memcmp(dst[0], dst[1], n + kSlack) != 0 || std::memcmp(src[0], src[1], n) != 0) {
            size_t at = 0;
            while (at < n + kSlack && dst[0][at] == dst[1][at]) ++at;
            ctx.Fail("0x%x case %d: destinations differ at +0x%zx (original %02x, ours %02x)", rva, i, at,
                     at < n + kSlack ? dst[0][at] : 0, at < n + kSlack ? dst[1][at] : 0);
            return;
        }
    }
}

// The out-of-line copy constructor: this = destination, the source as its argument, returns this.
template <class State> void CheckCopyFrom(difftest::Context &ctx, uint32_t rva, uint32_t seed)
{
    std::mt19937 rng(seed);
    auto orig = ctx.Original<void *(__thiscall *)(void *, const void *)>(rva);
    constexpr size_t n = sizeof(State);
    for (int i = 0; i < 20000; ++i) {
        alignas(16) uint8_t src[2][n], dst[2][n + kSlack];
        Fill(rng, src[0], n);
        Fill(rng, dst[0], n + kSlack);
        std::memcpy(src[1], src[0], n);
        std::memcpy(dst[1], dst[0], n + kSlack);
        void *r0 = orig(dst[0], src[0]);
        void *r1 = reinterpret_cast<State *>(dst[1])->CopyFrom(*reinterpret_cast<const State *>(src[1]));
        ++ctx.cases;
        if (r0 != dst[0] || r1 != dst[1] || std::memcmp(dst[0], dst[1], n + kSlack) != 0 ||
            std::memcmp(src[0], src[1], n) != 0) {
            ctx.Fail("0x%x case %d: results or destinations differ", rva, i);
            return;
        }
    }
}

// The deleting destructor on a heap block of the state's size per side, flags with bit 0 clear, set, or random: the
// result, the frees (FreeRecorder: whether the block was freed, how many frees) and a surviving block's bytes (the
// vtable word the destructor stores included) are compared. A freed block's bytes are not observable (the heap reuses
// them). Not observable either: the size passed to the sized delete (our CRT frees the block whatever the size).
template <class State> void CheckDeletingDestructor(difftest::Context &ctx, uint32_t rva, uint32_t seed)
{
    std::mt19937 rng(seed);
    auto orig = ctx.Original<void *(__thiscall *)(void *, uint32_t)>(rva);
    constexpr size_t n = sizeof(State);
    for (int i = 0; i < 4000; ++i) {
        uint32_t flags = i % 4 == 0 ? 0 : i % 4 == 1 ? 1 : static_cast<uint32_t>(rng());
        uint8_t bytes[n];
        Fill(rng, bytes, n);
        uint8_t *blocks[2];
        void *results[2];
        bool freed[2];
        size_t frees[2];
        for (int side = 0; side < 2; ++side) {
            blocks[side] = static_cast<uint8_t *>(::operator new(n));
            std::memcpy(blocks[side], bytes, n);
            difftest::FreeRecorder rec;
            results[side] = side == 0 ? orig(blocks[side], flags)
                                      : reinterpret_cast<cStateHeader *>(blocks[side])->DeletingDestructor<State>(flags);
            rec.Stop();
            freed[side] = rec.Freed(blocks[side]);
            frees[side] = rec.Count();
        }
        ++ctx.cases;
        bool same = results[0] == blocks[0] && results[1] == blocks[1] && freed[0] == freed[1] && frees[0] == frees[1];
        if (same && !freed[0]) same = std::memcmp(blocks[0], blocks[1], n) == 0;
        if (!same) {
            ctx.Fail("0x%x flags 0x%x: original freed %d (%zu frees), ours freed %d (%zu frees)", rva, flags, freed[0],
                     frees[0], freed[1], frees[1]);
            return;
        }
        for (int side = 0; side < 2; ++side)
            if (!freed[side]) ::operator delete(blocks[side]);
    }
}

} // namespace

#define CLONE_TEST(State, rva)                                                                                         \
    DIFF_TEST(RenderStates_CloneInto_##State, rva) { CheckCloneInto<State>(ctx, rva, rva); }

CLONE_TEST(cAlphaBlendState, 0x14e880)
CLONE_TEST(cAlphaTestFunction, 0x14e8c0)
CLONE_TEST(cAlphaTestState, 0x14e8f0)
CLONE_TEST(cClippingState, 0x14e920)
CLONE_TEST(cColorScalarState, 0x14e950)
CLONE_TEST(cColorWriteEnableState, 0x186a10)
CLONE_TEST(cCullModeState, 0x14e980)
CLONE_TEST(cDepthTestFunction, 0x1884c0)
CLONE_TEST(cDepthTestState, 0x14e9e0)
CLONE_TEST(cFFLightingState, 0x187180)
CLONE_TEST(cFillModeState, 0x186a40)
CLONE_TEST(cShadingState, 0x186a80)
CLONE_TEST(cSpecularAddState, 0x188bd0)
CLONE_TEST(cStencilOperationState, 0x188700)
CLONE_TEST(cTexCoordInterpolationState, 0x189170)
CLONE_TEST(cDepthRangeState, 0x14e9b0)
CLONE_TEST(cFFPerVertexColorState, 0x187210)
CLONE_TEST(cFFTextureCoordsSourceState, 0x187260)
CLONE_TEST(cFFTextureTransformTypeState, 0x1872f0)
CLONE_TEST(cStencilState, 0x188740)
CLONE_TEST(cTextureAddressingState, 0x14e7a0)
CLONE_TEST(cStencilFunctionState, 0x1886c0)
CLONE_TEST(cTextureFilterState, 0x14e7f0)
CLONE_TEST(cTextureMipFilterState, 0x14e840)
CLONE_TEST(cFFEnabledLights, 0x187130)
CLONE_TEST(cTextureBlendState, 0x14ea60)
CLONE_TEST(cRenderTargetsState, 0x1864b0)
CLONE_TEST(cScissorTestState, 0x1864e0)
CLONE_TEST(cViewportState, 0x14ea10)
CLONE_TEST(cFogState, 0x186a70)
CLONE_TEST(cFFMaterial, 0x1871c0)
CLONE_TEST(cFFTextureMatrixState, 0x1872a0)
CLONE_TEST(cFFLight, 0x187170)

DIFF_TEST(RenderStates_CopyFrom_cTextureBlendState, 0x14bd50) { CheckCopyFrom<cTextureBlendState>(ctx, 0x14bd50, 1); }
DIFF_TEST(RenderStates_CopyFrom_cFogState, 0x186820) { CheckCopyFrom<cFogState>(ctx, 0x186820, 2); }
DIFF_TEST(RenderStates_CopyFrom_cFFLight, 0x187030) { CheckCopyFrom<cFFLight>(ctx, 0x187030, 3); }

#define DELETE_TEST(name, State, rva)                                                                                  \
    DIFF_TEST(RenderStates_DeletingDestructor_##name, rva) { CheckDeletingDestructor<State>(ctx, rva, rva); }

DELETE_TEST(Size08, cStateHeader, 0x14c870)
DELETE_TEST(Size0c, cAlphaBlendState, 0x14c620)
DELETE_TEST(Size10, cDepthRangeState, 0x14c650)
DELETE_TEST(Size14, cStencilFunctionState, 0x14c8d0)
DELETE_TEST(Size18, cTextureBlendState, 0x14c8a0)
DELETE_TEST(Size1c, cRenderTargetsState, 0x186480)
DELETE_TEST(Size20, cViewportState, 0x14c900)
DELETE_TEST(Size24, cFogState, 0x186890)
DELETE_TEST(Size4c, cFFMaterial, 0x187100)
DELETE_TEST(Size58, cFFLight, 0x1870d0)
