// Differential tests for nTSSG::cLightingManager's light evaluation (lane 261108a, scenegraph LG1; src/sg/cLightingManager.cpp).
// Hook shape: the original runs on the same object image as ours, each case at the x87's 53-bit and 24-bit precision.
//
// The world: the object is 0x548 random bytes with the fields the members read set: 0..3 light sets (each word a float
// from Nice(); the name's vtable a fake whose slot 11 logs and answers a drawn bit), the current index in and out of range
// (the built-in set at +0x2d0), 0..3 ambient maps (null or fakes whose slots 26 / 27 give a drawn width / height and 38
// logs the texel asked for and writes a colour hashed from it), a fake terrain at +0x1c whose slot 62 logs the vertex and
// returns a height hashed from it (floats and doubles with low bits set, so the x87 subtractions of a returned height
// are told apart from float ones), the sun height map's grid (+0x248) and the sky samples (+0x1f4). Floats are drawn
// from Nice(): zeros of both signs, small integers, values near the cases' scales, wide exponents, rare infinities;
// NaNs: one planted input, or two with different payloads (and infinities and texels of any bits making more), so the
// operand order of each SSE and x87 operation decides which NaN survives (docs/lane/test.md "NaN operand order").
//
// Compared: results (float bits), out buffers, the fakes' event log, the object's bytes and the light sets' bytes.
#include "sg_lighting_world.h"

// 0x2d6740 EvaluateAmbient (map path: widths and heights 0, 1, small, large, 2^31 + 1, normals in and outside [-1, 1];
// hemisphere path: no map, a null map, the index past the maps), 0x2d66f0 its 0x1ec-scaled form and 0x2d7930 slot 17
// (0x1f0).
DIFF_TEST(sg_lighting_eval_ambient, 0x2d6740)
{
    FillFakes();
    using AmbientFn = void(__thiscall *)(void *, const float *, float *);
    auto evaluate = ctx.Original<AmbientFn>(0x2d6740);
    auto scaled = ctx.Original<AmbientFn>(0x2d66f0);
    auto slot17 = ctx.Original<void(__thiscall *)(void *, uint32_t, const float *, float *)>(0x2d7930);
    AtBothPrecisions(ctx, "ambient", 3000, [&](uint32_t seed, const char *label) {
        std::mt19937 gen(seed * 7919u + 0x2d6740u);
        const uint32_t world = gen();
        const unsigned which = seed % 3;
        float normal[3];
        for (auto &f : normal) f = gen() % 8 ? (float(int32_t(gen() % 2600) - 1300) / 1000.0f) : Nice(gen);
        const uint32_t nanPick = gen();
        const uint32_t unused = gen();
        const unsigned mode = gen() % 12; // 0..3 a planted NaN pair, 4 signed zeros, else MaybeNaN
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            Build(world);
            gLog.clear();
            gTexelNaN = ~0u;
            float n[3] = {normal[0], normal[1], normal[2]};
            {
                std::mt19937 pick(nanPick);
                auto &set = const_cast<cLightSet &>(Current());
                const unsigned c = pick() % 3, i = pick() % 4;
                float *colour = &set.mLights[i].mColour.x, *tint = &set.mTint.x, *ambient = &set.mAmbient.x;
                switch (mode) {
                case 0: gTexelNaN = c, tint[c] = NaN(pick); break;                     // tint * texel
                case 1: colour[c] = NaN(pick), set.mLights[i].mIntensity = NaN(pick); break; // colour * weight
                case 2: colour[c] = NaN(pick), n[pick() % 3] = NaN(pick); break;       // the weight from the normal
                case 3: ambient[c] = NaN(pick), set.mLights[i].mIntensity = NaN(pick); break; // ambient * rest
                case 4: // zeros of both signs: the -0 a dot product and the weights keep
                    for (auto &f : n) f = pick() % 2 ? -0.0f : 0.0f;
                    for (int k = 0; k < 3; ++k) {
                        ambient[k] = pick() % 2 ? -0.0f : 0.0f;
                        for (auto &l : set.mLights) (&l.mColour.x)[k] = pick() % 2 ? -0.0f : 0.0f, (&l.mDirection.x)[k] = Nice(pick);
                    }
                    if (pick() % 2) { // every dot product -0 (a -0 normal, positive directions): the weights stay -0
                        for (auto &f : n) f = -0.0f;
                        for (auto &l : set.mLights) {
                            for (int k = 0; k < 3; ++k) (&l.mDirection.x)[k] = float(1 + pick() % 100) / 64.0f;
                            l.mIntensity = float(1 + pick() % 100) / 32.0f;
                            (&l.mColour.x)[c] = 0.0f; // +0 * -0: the sum keeps -0 only while each weight does
                        }
                        ambient[c] = -0.0f;
                    }
                    break;
                default: {
                    float *places[] = {&n[0], &n[1], &n[2], &set.mLights[0].mColour.x, &set.mLights[1].mIntensity,
                                       &set.mLights[2].mDirection.y, &set.mLights[3].mColour.z, &set.mAmbient.y,
                                       &set.mTint.x, &set.mTint.z, reinterpret_cast<float *>(gObject + 0x1ec),
                                       reinterpret_cast<float *>(gObject + 0x1f0)};
                    MaybeNaN(pick, places, 12);
                }
                }
            }
            float out[3] = {Fl(0xabababab), Fl(0xabababab), Fl(0xabababab)};
            ctx.Mark(side ? "ours" : "original");
            if (side == 0) {
                if (which == 0) evaluate(gObject, n, out);
                else if (which == 1) scaled(gObject, n, out);
                else slot17(gObject, unused, n, out);
            } else {
                if (which == 0) Ours()->EvaluateAmbient(n, out);
                else if (which == 1) Ours()->EvaluateAmbientScaled(n, out);
                else Ours()->AmbientForNormal(unused, n, out);
            }
            const uint32_t values[3] = {U(out[0]), U(out[1]), U(out[2])};
            outcome[side] = End(values, 3);
        }
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
}

// 0x2e3280 TerrainNormal (terrain widths -2..39, coordinates around and outside the grid, negative ones included),
// 0x2e3640 TerrainHeight (any vertex) and 0x2d7980 slot 16 (TerrainNormal into EvaluateAmbient, scaled by 0x1f0).
DIFF_TEST(sg_lighting_eval_terrain, 0x2e3280)
{
    FillFakes();
    auto normalAt = ctx.Original<float *(__thiscall *)(void *, float *, float, float)>(0x2e3280);
    auto height = ctx.Original<double(__thiscall *)(void *, int32_t, int32_t)>(0x2e3640);
    auto slot16 = ctx.Original<void(__thiscall *)(void *, float, float, float *)>(0x2d7980);
    AtBothPrecisions(ctx, "terrain", 4000, [&](uint32_t seed, const char *label) {
        std::mt19937 gen(seed * 7919u + 0x2e3280u);
        const uint32_t world = gen();
        const unsigned which = seed % 4 == 3 ? 1 + gen() % 2 : 0; // 0 normal, 1 height, 2 slot 16
        float xy[2];
        for (auto &f : xy) f = gen() % 10 ? float(int32_t(gen() % 1200) - 100) / float(1 + gen() % 16) : Nice(gen, 50.0f);
        const int32_t vi = gen() % 4 ? int32_t(gen() % 50) - 5 : int32_t(gen());
        const int32_t vj = gen() % 4 ? int32_t(gen() % 50) - 5 : int32_t(gen());
        const bool nanHeight = gen() % 8 == 0;
        const uint32_t nanPick = gen();
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            Build(world);
            gLog.clear();
            float x = xy[0], y = xy[1];
            {
                std::mt19937 pick(nanPick);
                if (nanHeight) {
                    // one vertex the normal reads answers NaN, or the two of one x87 subtraction (b1 or b2 and the
                    // height at x0 on its row) with different payloads
                    const float s = *reinterpret_cast<float *>(gObject + 0x200);
                    const int32_t ix = int32_t(std::floor(s * x)), iy = int32_t(std::floor(s * y));
                    int32_t w, hgt;
                    std::memcpy(&w, gObject + 0x20c, 4), std::memcpy(&hgt, gObject + 0x210, 4);
                    auto vertex = [](int32_t i, int32_t j) { return Mix(Mix(gTerrain.salt, uint32_t(i)), uint32_t(j)); };
                    if (pick() % 2) {
                        gTerrain.nanAt[0] = vertex(ix + int32_t(pick() % 3) - 1, iy + int32_t(pick() % 3) - 1);
                    } else {
                        const int32_t x0 = ix - 1 > 0 ? ix - 1 : 0, x2 = ix + 1 < w ? ix + 1 : w;
                        const int32_t row = pick() % 2 ? iy : (iy + 1 < hgt ? iy + 1 : hgt);
                        gTerrain.nanAt[0] = vertex(x2, row), gTerrain.nanAt[1] = vertex(x0, row);
                    }
                } else {
                    float *places[] = {&x, &y, reinterpret_cast<float *>(gObject + 0x200)};
                    MaybeNaN(pick, places, 3);
                }
            }
            float out[3] = {Fl(0xabababab), Fl(0xabababab), Fl(0xabababab)};
            uint32_t values[4] = {0, 0, 0, 0};
            ctx.Mark(side ? "ours" : "original");
            if (which == 0) {
                float *r = side == 0 ? normalAt(gObject, out, x, y) : Ours()->TerrainNormal(out, x, y);
                values[3] = r == out;
            } else if (which == 1) {
                const double h = side == 0 ? height(gObject, vi, vj) : Ours()->TerrainHeight(vi, vj);
                uint64_t bits;
                std::memcpy(&bits, &h, 8);
                values[3] = uint32_t(bits) ^ uint32_t(bits >> 32) * 31;
            } else {
                if (side == 0) slot16(gObject, x, y, out);
                else Ours()->AmbientAt(x, y, out);
            }
            values[0] = U(out[0]), values[1] = U(out[1]), values[2] = U(out[2]);
            outcome[side] = End(values, 4);
        }
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
}

// 0x2e31d0 slot 32 SunVisibility (flag clear or set, points inside, on the edges of and outside the grid), 0x2e2aa0
// HeightAbove (points over the grid) and 0x2d1600 Blend (a + b t s, normalised; zero results included).
DIFF_TEST(sg_lighting_eval_sun, 0x2e31d0)
{
    FillFakes();
    auto visibility = ctx.Original<float(__thiscall *)(void *, const float *)>(0x2e31d0);
    auto above = ctx.Original<double(__thiscall *)(void *, const float *)>(0x2e2aa0);
    auto blend = ctx.Original<float *(__thiscall *)(void *, float *, const float *, const float *, float)>(0x2d1600);
    AtBothPrecisions(ctx, "sun", 4000, [&](uint32_t seed, const char *label) {
        std::mt19937 gen(seed * 7919u + 0x2e31d0u);
        const uint32_t world = gen();
        const unsigned which = seed % 3;
        float p[3], a[3], b[3];
        const float t = gen() % 6 ? float(int32_t(gen() % 2000) - 1000) / 500.0f : Nice(gen);
        for (auto &f : a) f = gen() % 4 ? float(int32_t(gen() % 9) - 4) * 0.5f : Nice(gen, 3.0f);
        for (auto &f : b) f = gen() % 4 ? float(int32_t(gen() % 9) - 4) * 0.25f : Nice(gen, 3.0f);
        const uint32_t pDraw = gen(), nanPick = gen();
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            Build(world);
            gLog.clear();
            const cSunHeightMap &map = *OurSun();
            {
                std::mt19937 pg(pDraw);
                const float span = float(map.mWidth + 1) / map.mScale, spanY = float(map.mHeight + 1) / map.mScale;
                p[0] = pg() % 8 ? float(pg() % 4096) / 4096.0f * span - (pg() % 4 == 0 ? 0.5f : 0.0f) : Nice(pg, span);
                p[1] = pg() % 8 ? float(pg() % 4096) / 4096.0f * spanY - (pg() % 4 == 0 ? 0.5f : 0.0f) : Nice(pg, spanY);
                p[2] = pg() % 3 ? gGrid[pg() % (sizeof(gGrid) / 4)] + float(int32_t(pg() % 9) - 4) * 0.125f : Nice(pg, 8.0f);
                if (which == 1) { // HeightAbove reads any cell: keep the point over the grid (rows of W, H + 2 of them)
                    p[0] = float(pg() % 4096) / 4096.0f * (float(map.mWidth) - 0.5f) / map.mScale;
                    p[1] = float(pg() % 4096) / 4096.0f * (float(map.mHeight) - 0.5f) / map.mScale;
                }
            }
            float pp[3] = {p[0], p[1], p[2]}, aa[3] = {a[0], a[1], a[2]}, bb[3] = {b[0], b[1], b[2]}, tt = t;
            {
                std::mt19937 pick(nanPick);
                float *places[] = {&pp[0], &pp[1], &pp[2], reinterpret_cast<float *>(gObject + 0x2ac),
                                   reinterpret_cast<float *>(gObject + kSunMap + 0x30), &gGrid[pick() % (sizeof(gGrid) / 4)]};
                // HeightAbove indexes the grid with no check: only the height and the cells may be NaN there
                float *abovePlaces[] = {&pp[2], places[5]};
                float *blendPlaces[] = {&aa[0], &aa[1], &aa[2], &bb[0], &bb[1], &bb[2], &tt,
                                        reinterpret_cast<float *>(gObject + kSunMap + 0x4c)};
                if (which == 2 && pick() % 6 == 0) { // a zero sum: no normalisation
                    tt = pick() % 2 ? 0.0f : -0.0f;
                    for (auto &f : aa) f = pick() % 2 ? 0.0f : -0.0f;
                } else if (which != 0 && pick() % 5 == 0) {
                    // the cell read and the height both NaN (the height signalling or quiet, either significand larger)
                    const int32_t ix = int32_t(std::floor((pp[0] + 0.001f) * map.mScale));
                    const int32_t iy = int32_t(std::floor((pp[1] + 0.001f) * map.mScale));
                    const uint32_t cell = uint32_t(map.mWidth) * uint32_t(iy) + uint32_t(ix);
                    if (which == 1 && cell < sizeof(gGrid) / 4) gGrid[cell] = NaN(pick), pp[2] = NaN(pick);
                }
                if (which == 2) MaybeNaN(pick, blendPlaces, 8);
                else if (which == 1) MaybeNaN(pick, abovePlaces, 2);
                else MaybeNaN(pick, places, 6);
            }
            float out[3] = {Fl(0xabababab), Fl(0xabababab), Fl(0xabababab)};
            uint32_t values[4] = {0, 0, 0, 0};
            ctx.Mark(side ? "ours" : "original");
            if (which == 0) {
                values[3] = U(side == 0 ? visibility(gObject, pp) : Ours()->SunVisibility(pp));
            } else if (which == 1) {
                const double d = side == 0 ? above(gObject + kSunMap, pp) : OurSun()->HeightAbove(pp);
                uint64_t bits;
                std::memcpy(&bits, &d, 8);
                values[2] = uint32_t(bits >> 32), values[3] = uint32_t(bits);
            } else {
                float *r = side == 0 ? blend(gObject + kSunMap, out, aa, bb, tt) : OurSun()->Blend(out, aa, bb, tt);
                values[3] = r == out;
            }
            values[0] = U(out[0]), values[1] = U(out[1]);
            if (which == 2) values[2] = U(out[2]);
            outcome[side] = End(values, 4);
        }
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
}

// The free helpers 0x2e3120 OrderSegment (levels equal or not, x equal or not, NaNs), 0x2e56b0 EdgeNormal (zero-length
// segments), 0x2d79f0 FaceKind (every kind), 0x2daee0 SumOfSix, 0x2dae80 AverageOfSix, and the members 0x2e0320 slot 35
// SampleLuminance (indexes -32768, -1, 0..5, 32767; 0..4 samples) and 0x2d6b80 FindLightSet (0..3 sets, any match bits).
DIFF_TEST(sg_lighting_eval_helpers, 0x2e3120)
{
    FillFakes();
    using SegmentFn = float *(__cdecl *)(float *, const float *);
    auto order = ctx.Original<SegmentFn>(0x2e3120);
    auto edge = ctx.Original<SegmentFn>(0x2e56b0);
    auto kind = ctx.Original<void(__cdecl *)(char, float *, float *, bool *)>(0x2d79f0);
    auto sum = ctx.Original<SegmentFn>(0x2daee0);
    auto average = ctx.Original<SegmentFn>(0x2dae80);
    auto luminance = ctx.Original<float(__thiscall *)(void *, int16_t)>(0x2e0320);
    auto find = ctx.Original<int32_t(__thiscall *)(void *, const void *)>(0x2d6b80);
    AtBothPrecisions(ctx, "helpers", 6000, [&](uint32_t seed, const char *label) {
        std::mt19937 gen(seed * 7919u + 0x2e3120u);
        const uint32_t world = gen();
        const unsigned which = seed % 7;
        float in[18];
        for (auto &f : in) f = gen() % 3 ? float(int32_t(gen() % 9) - 4) * 0.5f : Nice(gen, 4.0f);
        uint32_t levels[2] = {gen() % 4 ? gen() % 3 : gen(), gen() % 4 ? gen() % 3 : gen()};
        if (gen() % 3 == 0) levels[1] = levels[0];
        if (gen() % 3 == 0) in[3] = in[0];
        if (gen() % 4 == 0) in[4] = in[1];
        if (gen() % 6 == 0) in[gen() % 18] = NaN(gen);
        if (gen() % 6 == 0) in[gen() % 18] = NaN(gen);
        static const int16_t kIndexes[] = {0, 0, 1, 2, 3, 4, 5, -1, -32768, 32767};
        const int16_t index = kIndexes[gen() % 10];
        const uint32_t match = gen() % 3 ? gen() : 0;
        const uint32_t nanPick = gen();
        Blob outcome[2];
        for (int side = 0; side < 2; ++side) {
            Build(world);
            gLog.clear();
            gNameMatch = match;
            float input[18];
            std::memcpy(input, in, sizeof(in));
            if (which <= 1) std::memcpy(&input[2], &levels[0], 4), std::memcpy(&input[5], &levels[1], 4);
            if (which == 1 && (seed & 3) == 0) { // both components NaN, different payloads
                std::mt19937 pick(nanPick);
                input[pick() % 2 ? 1 : 4] = NaN(pick), input[pick() % 2 ? 0 : 3] = NaN(pick);
            }
            if (which == 5) {
                std::mt19937 pick(nanPick);
                auto &light = const_cast<cLightSet &>(Current()).mLights[0];
                float *places[] = {&light.mColour.x, &light.mColour.y, &light.mColour.z, &light.mIntensity,
                                   &gSky[pick() % 4][pick() % 18]};
                MaybeNaN(pick, places, 5);
            }
            float out[18];
            for (auto &f : out) f = Fl(0xabababab);
            uint32_t values[2] = {0, 0};
            ctx.Mark(side ? "ours" : "original");
            switch (which) {
            case 0: values[0] = (side == 0 ? order(out, input) : cLightingManager::OrderSegment(out, input)) == out; break;
            case 1: values[0] = (side == 0 ? edge(out, input) : cLightingManager::EdgeNormal(out, input)) == out; break;
            case 2:
                for (int k = -128; k < 128; ++k) {
                    float sign = Fl(0xcdcdcdcd), offset = in[k & 15];
                    bool flag = (k & 1) != 0;
                    if (side == 0) kind(char(k), &sign, &offset, &flag);
                    else cLightingManager::FaceKind(char(k), &sign, &offset, &flag);
                    gLog.push_back(U(sign)), gLog.push_back(U(offset)), gLog.push_back(flag);
                }
                break;
            case 3: values[0] = (side == 0 ? sum(out, input) : cLightingManager::SumOfSix(out, input)) == out; break;
            case 4: values[0] = (side == 0 ? average(out, input) : cLightingManager::AverageOfSix(out, input)) == out; break;
            case 5: values[0] = U(side == 0 ? luminance(gObject, index) : Ours()->SampleLuminance(index)); break;
            default:
                values[0] = uint32_t(side == 0 ? find(gObject, gQueryName)
                                               : Ours()->FindLightSet(*reinterpret_cast<const cIGZString *>(gQueryName)));
            }
            values[1] = index;
            outcome[side] = End(values, 2);
            for (auto f : out) outcome[side].push_back(U(f));
        }
        svcfx::Same(ctx, outcome[0], outcome[1], label, seed);
    });
}
