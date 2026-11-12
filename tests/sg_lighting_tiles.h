// The lighting manager's lightmap fixture (lanes 261108b and 261108e, scenegraph LG2): what the tile and wall lighters
// write into and ask, over tests/sg_lighting_world.h's world. Included by tests/sg_lighting_vertex.cpp and
// tests/sg_lighting_walls.cpp (its definitions are in an anonymous namespace, one copy per test file).
//
// - gTexture: a 48 x 48 buffer of random texels (FillTexture), Place() a tile's rectangle, pixels and stride in it;
// - the normals source (FakeNormals, slot 22 NormalAt) the per-corner lighters ask;
// - the sun height map's grids (gHeights, gGround) and the terrain's slots 61 and 62;
// - the lit object at +0x18 (slot 16), the lightmap manager at +0x24 (slots 19, 22, 23), three textures (slots 9,
//   27, 28), the service the cached global 0x10cc29c holds (slot 39). TextureEnd() is the comparison: the log, the
//   object and sets (sg_lighting_world.h's End) and the whole texture buffer.
#pragma once

#include "sg_lighting_world.h"

#include <vector>

namespace {


constexpr uint32_t kTexWords = 48 * 48;
alignas(16) uint32_t gTexture[kTexWords];

// A texture placement for a tile: pixels and stride such that every texel [x0, x1) x [y0, y1) lies in gTexture.
struct Placement {
    uint32_t x0, y0, x1, y1;
    uint32_t *pixels;
    int32_t stride;
};

Placement Place(std::mt19937 &gen)
{
    Placement p;
    static const uint32_t kSpans[] = {0, 1, 1, 2, 3, 5, 8, 8, 8, 8, 13};
    const uint32_t sx = kSpans[gen() % 11], sy = kSpans[gen() % 11];
    p.x0 = gen() % 12, p.y0 = gen() % 12;
    p.x1 = p.x0 + sx, p.y1 = p.y0 + sy;
    if (gen() % 16 == 0) std::swap(p.x0, p.x1), p.x0 += 1; // x0 > x1: no columns
    if (gen() % 16 == 0) std::swap(p.y0, p.y1), p.y0 += 1;
    const uint32_t width = (p.x0 > p.x1 ? p.x0 : p.x1) + gen() % 3; // at least x1
    const uint32_t rows = 26 + gen() % 4;
    const uint32_t stride = width + (gen() % 2 ? 0 : 1 + gen() % 5);
    if (gen() % 2) {
        p.pixels = gTexture, p.stride = int32_t(stride);
    } else { // bottom-up: row y at the last row less y rows
        p.pixels = gTexture + (rows - 1) * stride, p.stride = -int32_t(stride);
    }
    return p;
}

void FillTexture(uint32_t seed)
{
    std::mt19937 gen(seed);
    for (auto &t : gTexture) {
        const uint32_t r = gen();
        t = gen() % 4 ? r : (r & 0xff000000u) | (gen() % 2 ? 0x00ffffffu : 0u); // now and then bytes at 255 or 0
    }
}

float Component(std::mt19937 &gen)
{
    switch (gen() % 10) {
    case 0: return Nice(gen, 1.5f);
    case 1: return float(int32_t(gen() % 513) - 128) / 256.0f; // exact multiples of 1/256 (the rounding's ties)
    case 2: return float(int32_t(gen() % 1025) - 256) / 512.0f; // halfway between them
    default: return float(int32_t(gen() % 30000) - 7500) / 20000.0f; // mostly in [0, 1]
    }
}

// The normals source slot 39 hands the per-corner lighters: slot 22 (out, x, y) logs the vertex and writes a normal
// hashed from it; vertices whose hash is in nanAt answer a NaN component (a payload from the hash).
struct FakeNormals {
    void **vtable;
    uint32_t salt, nanAt[2];
    bool wide; // components anywhere (Nice), else in about [-1.3, 1.3]
};
FakeNormals gNormals;
float *__fastcall NormalAt(FakeNormals *self, void *, float *out, int32_t x, int32_t y)
{
    gLog.push_back(0x22), gLog.push_back(uint32_t(x)), gLog.push_back(uint32_t(y));
    const uint32_t h = Mix(Mix(self->salt, uint32_t(x)), uint32_t(y));
    std::mt19937 gen(h);
    for (auto c = 0; c < 3; ++c) out[c] = self->wide && gen() % 3 == 0 ? Nice(gen, 2.0f) : float(int32_t(gen() % 2600) - 1300) / 1000.0f;
    if (h == self->nanAt[0] || h == self->nanAt[1]) out[gen() % 3] = Fl(0x7f800001u | (h & 0x807fffffu));
    return out;
}
void *gNormalsVT[23];

Blob TextureEnd()
{
    const uint32_t mark = 0xeeee;
    Blob o = End(&mark, 1);
    o.insert(o.end(), gTexture, gTexture + kTexWords);
    return o;
}



// The shadowed lighters' world: the sun height map's two grids (its heights at +0x18, the ground under them at +0x0c),
// rows `width` cells apart, big enough for tiles at x, y in [0, 3] with spans up to 13; the terrain's slot 61 (does
// this tile light per vertex?) and slot 62 (the tile's height) answering per case; the global up normal (0xffd918).
constexpr uint32_t kGridCells = 1536;
alignas(16) float gHeights[kGridCells];
alignas(16) float gGround[kGridCells];
bool gPerVertex;
double gTileHeight;
bool __fastcall PerVertexSlot(FakeTerrain *, void *, const int32_t *tile, uint32_t kind)
{
    gLog.push_back(0x61), gLog.push_back(uint32_t(tile[0])), gLog.push_back(uint32_t(tile[1])), gLog.push_back(uint32_t(tile[2]));
    gLog.push_back(kind);
    return gPerVertex;
}
double __fastcall TileHeightSlot(FakeTerrain *, void *, const int32_t *vertex)
{
    gLog.push_back(0x62), gLog.push_back(uint32_t(vertex[0])), gLog.push_back(uint32_t(vertex[1])), gLog.push_back(uint32_t(vertex[2]));
    return gTileHeight;
}

float GridValue(std::mt19937 &gen)
{
    switch (gen() % 8) {
    case 0: return Nice(gen, 8.0f);
    case 1: return float(int32_t(gen() % 33) - 8) * 0.25f; // exact quarters: depths exactly 0 and alphas at ties
    default: return float(int32_t(gen() % 4000) - 1000) / 1000.0f;
    }
}



// Slot 41's world: the shadowed lighters' (sun grids, slots 61 and 62, the up normal) plus the lit object at +0x18
// (slot 16 lists the tiles: 16-byte records {x, y, level, kind byte}), the lightmap manager at +0x24 (slot 23: the
// scale; slot 19: a tile's texel rectangle and texture index; slot 22: the texture), three textures (slot 9: the
// texels, 28: the pitch in bytes, 27: the height; rows bottom-up), and the service the cached global 0x10cc29c holds
// (slot 39: the normals source). The terrain's slot 61 here logs the kind's low byte only: it takes a char, and slot
// 41's triangle path pushes it from a register whose upper bytes hold whatever was there (the texture index or a
// vtable pointer).
constexpr uint32_t kServiceGlobal = 0x10cc29c;
struct LitTile {
    int32_t x, y, level;
    uint32_t kind; // the low byte; the rest noise
};
struct FakeLitObject {
    void **vtable;
    std::vector<LitTile> tiles;
} gLitObject;
void *gLitObjectVT[23];
void __fastcall TilesSlot(FakeLitObject *self, void *, std::vector<LitTile> *out)
{
    gLog.push_back(0x16), gLog.push_back(uint32_t(out->size()));
    *out = self->tiles;
}

struct FakeTexture {
    void **vtable;
    uint32_t *texels, pitchBytes, height;
} gTextures[3];
void *gTextureVT[29];
uint32_t *__fastcall TexelsSlot(FakeTexture *self, void *, uint32_t level)
{
    gLog.push_back(0x09), gLog.push_back(level);
    return self->texels;
}
uint32_t __fastcall PitchSlot(FakeTexture *self, void *, uint32_t level)
{
    gLog.push_back(0x28), gLog.push_back(level);
    return self->pitchBytes;
}
uint32_t __fastcall HeightSlot(FakeTexture *self, void *, uint32_t level)
{
    gLog.push_back(0x27), gLog.push_back(level);
    return self->height;
}

struct Rect {
    uint32_t x0, y0, x1, y1;
    int32_t texture;
    bool found;
};
struct FakeLightmaps {
    void **vtable;
    float scale;
    Rect rects[8];
    uint32_t next;
} gLightmaps;
void *gLightmapsVT[24];
float __fastcall ScaleSlot(FakeLightmaps *self, void *, uint32_t which)
{
    gLog.push_back(0x23), gLog.push_back(which);
    return self->scale;
}
bool __fastcall RectSlot(FakeLightmaps *self, void *, const LitTile *tile, uint32_t *x0, uint32_t *y0, uint32_t *x1, uint32_t *y1,
                         int32_t *texture)
{
    gLog.push_back(0x19), gLog.push_back(uint32_t(tile->x)), gLog.push_back(uint32_t(tile->y));
    const Rect &r = self->rects[self->next++ % 8];
    *x0 = r.x0, *y0 = r.y0, *x1 = r.x1, *y1 = r.y1, *texture = r.texture;
    return r.found;
}
FakeTexture *__fastcall TextureSlot(FakeLightmaps *, void *, uint32_t kind, int32_t index)
{
    gLog.push_back(0x1022), gLog.push_back(kind), gLog.push_back(uint32_t(index));
    return &gTextures[uint32_t(index) % 3];
}

struct FakeService {
    void **vtable;
} gService;
void *gServiceVT[40];
const void *__fastcall NormalsSourceSlot(FakeService *, void *)
{
    gLog.push_back(0x39);
    return &gNormals;
}

bool __fastcall TileKindSlot(FakeTerrain *, void *, const int32_t *tile, uint32_t kind)
{
    gLog.push_back(0x61), gLog.push_back(uint32_t(tile[0])), gLog.push_back(uint32_t(tile[1])), gLog.push_back(uint32_t(tile[2]));
    gLog.push_back(kind & 0xffu);
    return gPerVertex;
}

} // namespace
