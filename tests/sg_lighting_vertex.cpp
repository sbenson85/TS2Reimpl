// Differential tests for nTSSG::cLightingManager's vertex lighting (lane 261108b, scenegraph LG2; src/sg/cLightingManager.cpp).
// Hook shape: the original runs on the same object image as ours, each case at the x87's 53-bit and 24-bit precision.
//
// The world is tests/sg_lighting_world.h's (the light sets, ambient maps and terrain fake the light evaluation reads),
// plus a texture: a buffer of random texels the lighters add to, rows `stride` words apart, the stride positive or
// negative (bottom-up, as slot 39 passes it), wider than the tile or not. Tiles: spans 0 (nothing to do), 1, small, 8
// (slot 39's) and up to 13, x0 > x1 and y0 > y1 now and then (unsigned bounds: nothing drawn), corners anywhere in the
// texture. Colours from Nice() around [-1.5, 1.5] (bytes clamped at both ends, sums past 255), NaNs planted: one, or two
// with different payloads in different inputs (colours, normals, the scale, mAmbientScale1F0, light-set words), so the
// operand order of each SSE operation decides which NaN survives and which byte its bits give.
//
// Compared: the whole texture buffer, the fakes' event log (normal requests, the evaluation's map and name calls), the
// object's bytes and the light sets' bytes. The texture, normals, grids and slot 41 fakes are tests/sg_lighting_tiles.h's.
#include "sg_lighting_tiles.h"

// 0x2d6500 AddFlatQuad and 0x2d6230 AddFlatTriangle (face kinds 3, 6, 12 and others): a colour added over a tile.
DIFF_TEST(sg_lighting_vertex_flat, 0x2d6500)
{
    using QuadFn = void(__stdcall *)(const float *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *, int32_t);
    using TriangleFn = void(__stdcall *)(char, const float *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *, int32_t);
    auto quad = ctx.Original<QuadFn>(0x2d6500);
    auto triangle = ctx.Original<TriangleFn>(0x2d6230);
    AtBothPrecisions(ctx, "flat", 3000, [&](uint32_t seed, const char *label) {
        std::mt19937 gen(seed * 7919u + 0x2d6500u);
        const Placement p = Place(gen);
        float colour[3];
        for (auto &c : colour) c = Component(gen);
        const unsigned roll = gen() % 8;
        if (roll == 0) colour[gen() % 3] = NaN(gen);
        else if (roll == 1) colour[gen() % 3] = NaN(gen), colour[gen() % 3] = NaN(gen);
        else if (roll == 2) colour[gen() % 3] = gen() % 2 ? -0.0f : Fl(0x7f800000u | (gen() & 0x80000000u));
        static const char kKinds[] = {3, 6, 12, 0, 9, 15, 5, char(0x83)};
        const char kind = gen() % 4 ? kKinds[gen() % 8] : char(gen());
        const bool half = seed % 2 != 0;
        const uint32_t texture = gen();
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            FillTexture(texture);
            gLog.clear();
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) {
                if (half) triangle(kind, colour, p.x0, p.y0, p.x1, p.y1, p.pixels, p.stride);
                else quad(colour, p.x0, p.y0, p.x1, p.y1, p.pixels, p.stride);
            } else {
                if (half) cLightingManager::AddFlatTriangle(kind, colour, p.x0, p.y0, p.x1, p.y1, p.pixels, p.stride);
                else cLightingManager::AddFlatQuad(colour, p.x0, p.y0, p.x1, p.y1, p.pixels, p.stride);
            }
            outcome[side].assign(gTexture, gTexture + kTexWords);
        }
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
}

// 0x2dcc40 LightQuad and 0x2db070 LightTriangle: corner normals from the source's slot 22, their ambient colours
// (the hemisphere or an ambient map, as the world draws), blended across the tile.
DIFF_TEST(sg_lighting_vertex_smooth, 0x2dcc40)
{
    FillFakes();
    gNormalsVT[22] = reinterpret_cast<void *>(&NormalAt);
    using TileFn = void(__thiscall *)(void *, const void *, const int32_t *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *, int32_t, float);
    auto quad = ctx.Original<TileFn>(0x2dcc40);
    auto triangle = ctx.Original<TileFn>(0x2db070);
    AtBothPrecisions(ctx, "smooth", 2000, [&](uint32_t seed, const char *label) {
        std::mt19937 gen(seed * 7919u + 0x2dcc40u);
        const uint32_t world = gen();
        const Placement p = Place(gen);
        static const int32_t kCoords[] = {0, 1, 5, 17, -1, 0x7fffffff, int32_t(0x80000000u)};
        int32_t tile[4] = {gen() % 4 ? int32_t(gen() % 64) : kCoords[gen() % 7], gen() % 4 ? int32_t(gen() % 64) : kCoords[gen() % 7],
                           int32_t(gen() % 4), int32_t(gen())};
        static const uint8_t kKinds[] = {3, 6, 12, 0, 9, 15};
        if (gen() % 4) tile[3] = int32_t((uint32_t(tile[3]) & 0xffffff00u) | kKinds[gen() % 6]);
        float scale = gen() % 4 ? float(1 + gen() % 64) / 32.0f : Nice(gen, 2.0f);
        const uint32_t salt = gen();
        const bool wide = gen() % 4 == 0;
        const unsigned mode = gen() % 10; // 0 a NaN normal, 1 two, 2 scale and a normal, 3 scale and 0x1f0, 4.. MaybeNaN
        const uint32_t nanPick = gen(), texture = gen();
        const bool half = seed % 2 != 0;
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            Build(world);
            gLog.clear();
            gTexelNaN = ~0u;
            FillTexture(texture);
            float s = scale;
            gNormals = {gNormalsVT, salt, {~0u, ~0u}, wide};
            {
                std::mt19937 pick(nanPick);
                const auto corner = [&](int c) { return Mix(Mix(salt, uint32_t(tile[0]) + uint32_t(c & 1)), uint32_t(tile[1]) + uint32_t(c >> 1)); };
                auto &set = const_cast<cLightSet &>(Current());
                float *f1f0 = reinterpret_cast<float *>(gObject + 0x1f0);
                switch (mode) {
                case 0: gNormals.nanAt[0] = corner(int(pick() % 4)); break;
                case 1: gNormals.nanAt[0] = corner(int(pick() % 4)), gNormals.nanAt[1] = corner(int(pick() % 4)); break;
                case 2: gNormals.nanAt[0] = corner(int(pick() % 4)), s = NaN(pick); break;
                case 3: s = NaN(pick), *f1f0 = NaN(pick); break;
                default: {
                    float *places[] = {&set.mLights[0].mColour.x, &set.mLights[1].mIntensity, &set.mLights[2].mColour.y,
                                       &set.mLights[3].mColour.z, &set.mAmbient.y, &set.mAmbient.x, &set.mTint.x, f1f0, &s};
                    MaybeNaN(pick, places, 9);
                }
                }
            }
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) (half ? triangle : quad)(gObject, &gNormals, tile, p.x0, p.y0, p.x1, p.y1, p.pixels, p.stride, s);
            else if (half) Ours()->LightTriangle(&gNormals, tile, p.x0, p.y0, p.x1, p.y1, p.pixels, p.stride, s);
            else Ours()->LightQuad(&gNormals, tile, p.x0, p.y0, p.x1, p.y1, p.pixels, p.stride, s);
            outcome[side] = TextureEnd();
        }
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
}

// 0x2d74b0 EvaluateAmbientShaded: the map path at the normal bent towards light 1 (Blend, the sun map's +0x4c), the
// hemisphere with light 0 shaded by 1 - shade * mSunFalloff (above 1, between, exactly 0, negative, NaN).
DIFF_TEST(sg_lighting_vertex_shaded, 0x2d74b0)
{
    FillFakes();
    using ShadedFn = void(__thiscall *)(void *, const float *, float, float *);
    auto shaded = ctx.Original<ShadedFn>(0x2d74b0);
    AtBothPrecisions(ctx, "shaded", 3000, [&](uint32_t seed, const char *label) {
        std::mt19937 gen(seed * 7919u + 0x2d74b0u);
        const uint32_t world = gen();
        float normal[3];
        for (auto &f : normal) f = gen() % 8 ? (float(int32_t(gen() % 2600) - 1300) / 1000.0f) : Nice(gen);
        const unsigned shadeKind = gen() % 6;
        float shade = shadeKind == 0 ? 0.0f : shadeKind == 1 ? -0.0f : shadeKind == 2 ? Nice(gen, 4.0f) : float(gen() % 300) / 100.0f;
        const bool exact = gen() % 6 == 0; // shade * falloff exactly 1: lit +0 or -0
        const uint32_t nanPick = gen();
        const unsigned mode = gen() % 12; // 0..3 a planted NaN pair, else MaybeNaN
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            Build(world);
            gLog.clear();
            gTexelNaN = ~0u;
            float n[3] = {normal[0], normal[1], normal[2]};
            float s = shade;
            if (exact) PutF(0x2ac, 0.5f), s = 2.0f;
            {
                std::mt19937 pick(nanPick);
                auto &set = const_cast<cLightSet &>(Current());
                const unsigned c = pick() % 3, i = pick() % 4;
                float *falloff = reinterpret_cast<float *>(gObject + 0x2ac), *blend = reinterpret_cast<float *>(gObject + kSunMap + 0x4c);
                switch (mode) {
                case 0: s = NaN(pick), *falloff = NaN(pick); break;                       // shade * falloff
                case 1: s = NaN(pick), set.mLights[0].mIntensity = NaN(pick); break;      // weight0 * lit
                case 2: n[c] = NaN(pick), (&set.mLights[1].mDirection.x)[c] = NaN(pick); break; // the bend, light 1's dot
                case 3: s = NaN(pick), *blend = NaN(pick), gTexelNaN = pick() % 2 ? c : ~0u; break; // Blend's t, the texel
                default: {
                    float *places[] = {&n[0], &n[1], &n[2], &s, &set.mLights[0].mColour.x, &set.mLights[i].mIntensity,
                                       &set.mLights[1].mDirection.y, &set.mAmbient.z, &set.mTint.x, &set.mTint.y, falloff, blend};
                    MaybeNaN(pick, places, 12);
                }
                }
            }
            float out[3] = {Fl(0xabababab), Fl(0xabababab), Fl(0xabababab)};
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) shaded(gObject, n, s, out);
            else Ours()->EvaluateAmbientShaded(n, s, out);
            const uint32_t values[3] = {U(out[0]), U(out[1]), U(out[2])};
            outcome[side] = End(values, 3);
        }
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
}

// 0x2dd1b0 LightQuadShadowMask, 0x2db640 LightTriangleShadowMask, 0x2ddb80 LightQuadSunShaded and 0x2dc0e0
// LightTriangleSunShaded: per-vertex tiles (level 0 and the terrain's slot 61 says so) blend the corner normals and
// read the depth (heights less ground) per texel; the others light the up normal once and read heights less the tile's
// height. Alpha from the depth: min(depth * falloff, 1) for the masks, sqrt(max(1 - depth * falloff, 0)) shaded
// (negatives under the root: the original's libm path). The colour scaled and added as in LightQuad.
DIFF_TEST(sg_lighting_vertex_shadowed, 0x2dd1b0)
{
    FillFakes();
    gNormalsVT[22] = reinterpret_cast<void *>(&NormalAt);
    gTerrainVT[61] = reinterpret_cast<void *>(&PerVertexSlot);
    gTerrainVT[62] = reinterpret_cast<void *>(&TileHeightSlot);
    float *const up = GameAddress<float>(0xffd918);
    const float savedUp[3] = {up[0], up[1], up[2]};
    using TileFn = void(__thiscall *)(void *, const void *, const int32_t *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *, int32_t, float);
    using OursFn = void (cLightingManager::*)(const void *, const int32_t *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t *, int32_t, float);
    const TileFn originals[4] = {ctx.Original<TileFn>(0x2dd1b0), ctx.Original<TileFn>(0x2db640), ctx.Original<TileFn>(0x2ddb80),
                                 ctx.Original<TileFn>(0x2dc0e0)};
    const OursFn ours[4] = {&cLightingManager::LightQuadShadowMask, &cLightingManager::LightTriangleShadowMask,
                            &cLightingManager::LightQuadSunShaded, &cLightingManager::LightTriangleSunShaded};
    AtBothPrecisions(ctx, "shadowed", 4000, [&](uint32_t seed, const char *label) {
        std::mt19937 gen(seed * 7919u + 0x2dd1b0u);
        const unsigned which = seed % 4;
        const uint32_t world = gen(), grids = gen();
        const Placement p = Place(gen);
        int32_t tile[4] = {int32_t(gen() % 4), int32_t(gen() % 4), gen() % 3 ? 0 : int32_t(1 + gen() % 3), int32_t(gen())};
        static const uint8_t kKinds[] = {3, 6, 12, 0, 9, 15};
        if (gen() % 4) tile[3] = int32_t((uint32_t(tile[3]) & 0xffffff00u) | kKinds[gen() % 6]);
        const bool perVertex = gen() % 4 != 0;
        static const uint32_t kWidths[] = {1, 5, 8, 12, 13, 16, 24, 40};
        const uint32_t width = gen() % 3 ? 13 + gen() % 28 : kWidths[gen() % 8];
        static const float kFalloffs[] = {0.5f, 1.0f, 2.0f, 0.25f, 0.0f, -0.5f, 4.0f, 0.1f};
        const float falloff = gen() % 3 ? kFalloffs[gen() % 8] : Nice(gen, 2.0f);
        double height;
        switch (gen() % 5) {
        case 0: height = 0.0; break;
        case 1: height = double(int32_t(gen() % 9) - 4) * 0.25; break;
        case 2: height = double(Nice(gen, 2.0f)) + double(gen() % 256) * 0x1p-40; break; // rounds to float
        default: height = double(int32_t(gen() % 3000) - 1000) / 1000.0;
        }
        float upNormal[3];
        for (auto &f : upNormal) f = gen() % 4 ? float(int32_t(gen() % 2600) - 1300) / 1000.0f : 0.0f;
        if (gen() % 3 == 0) upNormal[0] = 0.0f, upNormal[1] = 0.0f, upNormal[2] = 1.0f;
        const float scale = gen() % 4 ? float(1 + gen() % 64) / 32.0f : Nice(gen, 2.0f);
        const uint32_t salt = gen();
        const bool wide = gen() % 4 == 0;
        const unsigned mode = gen() % 14; // 0..6 a planted NaN or pair, else MaybeNaN
        const uint32_t nanPick = gen(), texture = gen();
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            Build(world);
            gLog.clear();
            gTexelNaN = ~0u;
            FillTexture(texture);
            {
                std::mt19937 g(grids);
                for (uint32_t i = 0; i < kGridCells; ++i) {
                    gHeights[i] = GridValue(g);
                    gGround[i] = g() % 6 ? GridValue(g) : gHeights[i]; // depth exactly 0 now and then
                }
            }
            Put(kSunMap + 0x0c, P(gGround)), Put(kSunMap + 0x18, P(gHeights));
            Put(kSunMap + 0x24, width), Put(kSunMap + 0x28, kGridCells / width);
            PutF(0x2ac, falloff);
            gPerVertex = perVertex;
            gTileHeight = height;
            up[0] = upNormal[0], up[1] = upNormal[1], up[2] = upNormal[2];
            float s = scale;
            gNormals = {gNormalsVT, salt, {~0u, ~0u}, wide};
            {
                std::mt19937 pick(nanPick);
                const auto corner = [&](int c) { return Mix(Mix(salt, uint32_t(tile[0]) + uint32_t(c & 1)), uint32_t(tile[1]) + uint32_t(c >> 1)); };
                const auto cell = [&]() {
                    const uint32_t row = pick() % 13, col = pick() % 13;
                    return width * (uint32_t(tile[1]) * 8u + row) + uint32_t(tile[0]) * 8u + col;
                };
                auto &set = const_cast<cLightSet &>(Current());
                float *f1f0 = reinterpret_cast<float *>(gObject + 0x1f0), *fall = reinterpret_cast<float *>(gObject + 0x2ac);
                switch (mode) {
                case 0: gNormals.nanAt[0] = corner(int(pick() % 4)); break;
                case 1: gNormals.nanAt[0] = corner(int(pick() % 4)), gNormals.nanAt[1] = corner(int(pick() % 4)); break;
                case 2: {
                    const uint32_t c = cell();
                    gHeights[c] = NaN(pick), gGround[c] = NaN(pick);
                    break;
                }
                case 3: gHeights[cell()] = NaN(pick), *fall = NaN(pick); break;
                case 4: s = NaN(pick), *f1f0 = NaN(pick); break;
                case 5: gTileHeight = double(NaN(pick)), *fall = NaN(pick); break;
                case 6: up[pick() % 3] = NaN(pick), gGround[cell()] = NaN(pick); break;
                default: {
                    float *places[] = {&set.mLights[0].mColour.x, &set.mLights[1].mIntensity, &set.mLights[2].mColour.y,
                                       &set.mAmbient.y, &set.mTint.x, f1f0, fall, &s, &up[0], &up[2], &gGround[cell()],
                                       &gHeights[cell()]};
                    MaybeNaN(pick, places, 12);
                }
                }
            }
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) originals[which](gObject, &gNormals, tile, p.x0, p.y0, p.x1, p.y1, p.pixels, p.stride, s);
            else (Ours()->*ours[which])(&gNormals, tile, p.x0, p.y0, p.x1, p.y1, p.pixels, p.stride, s);
            outcome[side] = TextureEnd();
            outcome[side].insert(outcome[side].end(), {U(up[0]), U(up[1]), U(up[2])});
        }
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
    up[0] = savedUp[0], up[1] = savedUp[1], up[2] = savedUp[2];
    gTerrainVT[62] = reinterpret_cast<void *>(&TerrainHeightSlot);
}

// 0x2cc370 slot 41 LightTiles: each tile the lit object lists lit into its lightmap texture: sun-shaded (mShadeTiles
// set), per vertex (level 0 and slot 61 says so) or the ambient colour of the up normal, as quads (kind 15) or
// triangles. Tiles 0 to 8, rectangles found or not, texture indexes repeating (the texture fetched
// once per run; never -1, the "none fetched yet" value: the first tile would then light through a null pointer).
DIFF_TEST(sg_lighting_vertex_tiles, 0x2cc370)
{
    FillFakes();
    gNormalsVT[22] = reinterpret_cast<void *>(&NormalAt);
    gTerrainVT[61] = reinterpret_cast<void *>(&TileKindSlot);
    gTerrainVT[62] = reinterpret_cast<void *>(&TileHeightSlot);
    gLitObjectVT[16] = reinterpret_cast<void *>(&TilesSlot);
    gTextureVT[9] = reinterpret_cast<void *>(&TexelsSlot);
    gTextureVT[28] = reinterpret_cast<void *>(&PitchSlot);
    gTextureVT[27] = reinterpret_cast<void *>(&HeightSlot);
    gLightmapsVT[23] = reinterpret_cast<void *>(&ScaleSlot);
    gLightmapsVT[19] = reinterpret_cast<void *>(&RectSlot);
    gLightmapsVT[22] = reinterpret_cast<void *>(&TextureSlot);
    gServiceVT[39] = reinterpret_cast<void *>(&NormalsSourceSlot);
    float *const up = GameAddress<float>(0xffd918);
    const float savedUp[3] = {up[0], up[1], up[2]};
    void **const service = GameAddress<void *>(kServiceGlobal);
    void *const savedService = *service;
    using TilesFn = void(__thiscall *)(void *);
    auto tiles = ctx.Original<TilesFn>(0x2cc370);
    AtBothPrecisions(ctx, "tiles", 3000, [&](uint32_t seed, const char *label) {
        std::mt19937 gen(seed * 7919u + 0x2cc370u);
        const uint32_t world = gen(), grids = gen();
        std::vector<LitTile> list(gen() % 6 ? 1 + gen() % 8 : 0);
        static const uint8_t kKinds[] = {15, 15, 15, 3, 6, 12, 0, 9};
        for (auto &t : list) {
            t.x = int32_t(gen() % 4), t.y = int32_t(gen() % 4), t.level = gen() % 3 ? 0 : int32_t(1 + gen() % 3);
            t.kind = (gen() & 0xffffff00u) | kKinds[gen() % 8];
        }
        FakeTexture textures[3];
        for (unsigned i = 0; i < 3; ++i) {
            static const uint32_t kPitches[] = {16, 24, 40};
            const uint32_t pitch = kPitches[gen() % 3];
            textures[i] = {gTextureVT, gTexture + 16 * i, pitch * 4 + (gen() % 4 ? 0 : gen() % 4), 14 + gen() % 35};
        }
        Rect rects[8];
        for (auto &r : rects) {
            static const uint32_t kSpans[] = {0, 1, 2, 5, 8, 8, 8, 13};
            const uint32_t sx = kSpans[gen() % 8], sy = kSpans[gen() % 8];
            r.x0 = gen() % 3, r.y0 = gen() % 2; // inside every texture: x1 <= 16 words, y1 <= 14 rows
            r.x1 = r.x0 + sx, r.y1 = r.y0 + sy;
            if (gen() % 16 == 0) std::swap(r.x0, r.x1), r.x0 += 1;
            r.texture = gen() % 3 ? int32_t(gen() % 2) : int32_t(gen() % 5);
            r.found = gen() % 6 != 0;
        }
        const uint8_t shade = uint8_t(gen() % 3 ? 0 : 1 + gen() % 255);
        const bool perVertex = gen() % 4 != 0;
        static const uint32_t kWidths[] = {13, 16, 24, 40};
        const uint32_t width = kWidths[gen() % 4];
        static const float kFalloffs[] = {0.5f, 1.0f, 2.0f, 0.25f, 0.0f, -0.5f, 4.0f, 0.1f};
        const float falloff = kFalloffs[gen() % 8];
        const double height = gen() % 2 ? 0.0 : double(int32_t(gen() % 3000) - 1000) / 1000.0;
        float upNormal[3];
        for (auto &f : upNormal) f = gen() % 4 ? float(int32_t(gen() % 2600) - 1300) / 1000.0f : 0.0f;
        if (gen() % 3 == 0) upNormal[0] = 0.0f, upNormal[1] = 0.0f, upNormal[2] = 1.0f;
        const float scale = gen() % 4 ? float(1 + gen() % 64) / 32.0f : Nice(gen, 2.0f);
        const uint32_t salt = gen();
        const unsigned mode = gen() % 10; // 0..3 a planted NaN or pair, else MaybeNaN
        const uint32_t nanPick = gen(), texture = gen();
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            Build(world);
            gLog.clear();
            gTexelNaN = ~0u;
            FillTexture(texture);
            {
                std::mt19937 g(grids);
                for (uint32_t i = 0; i < kGridCells; ++i) {
                    gHeights[i] = GridValue(g);
                    gGround[i] = g() % 6 ? GridValue(g) : gHeights[i];
                }
            }
            Put(kSunMap + 0x0c, P(gGround)), Put(kSunMap + 0x18, P(gHeights));
            Put(kSunMap + 0x24, width), Put(kSunMap + 0x28, kGridCells / width);
            PutF(0x2ac, falloff);
            gPerVertex = perVertex;
            gTileHeight = height;
            up[0] = upNormal[0], up[1] = upNormal[1], up[2] = upNormal[2];
            gNormals = {gNormalsVT, salt, {~0u, ~0u}, false};
            gLitObject.vtable = gLitObjectVT, gLitObject.tiles = list;
            for (unsigned i = 0; i < 3; ++i) gTextures[i] = textures[i];
            gLightmaps.vtable = gLightmapsVT, gLightmaps.scale = scale, gLightmaps.next = 0;
            std::memcpy(gLightmaps.rects, rects, sizeof(rects));
            gService.vtable = gServiceVT;
            *service = &gService;
            Put(0x18, P(&gLitObject)), Put(0x24, P(&gLightmaps));
            gObject[0x5c] = shade;
            {
                std::mt19937 pick(nanPick);
                auto &set = const_cast<cLightSet &>(Current());
                float *f1f0 = reinterpret_cast<float *>(gObject + 0x1f0);
                switch (mode) {
                case 0: gLightmaps.scale = NaN(pick), *f1f0 = NaN(pick); break; // the scale's product
                case 1: up[pick() % 3] = NaN(pick), set.mAmbient.x = NaN(pick); break;
                case 2: gLightmaps.scale = NaN(pick), set.mLights[0].mIntensity = NaN(pick); break; // colour * scale
                case 3: gNormals.nanAt[0] = Mix(Mix(salt, uint32_t(pick() % 5)), uint32_t(pick() % 5)); break;
                default: {
                    float *places[] = {&set.mLights[0].mColour.x, &set.mLights[1].mIntensity, &set.mAmbient.y, &set.mTint.x,
                                       f1f0, &gLightmaps.scale, &up[0], &up[2], &gHeights[pick() % kGridCells]};
                    MaybeNaN(pick, places, 9);
                }
                }
            }
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) tiles(gObject);
            else Ours()->LightTiles();
            outcome[side] = TextureEnd();
            outcome[side].insert(outcome[side].end(), {U(up[0]), U(up[1]), U(up[2])});
        }
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
    *service = savedService;
    up[0] = savedUp[0], up[1] = savedUp[1], up[2] = savedUp[2];
    gTerrainVT[62] = reinterpret_cast<void *>(&TerrainHeightSlot);
}
