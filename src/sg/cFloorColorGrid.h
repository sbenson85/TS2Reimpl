#pragma once
// The diffusion's floor colour grid (lane 261109i, scenegraph row LD2a): nTSSG::cFloorColorGrid, one per level, held by
// value in cDiffusionManager's grid vector (src/sg/cDiffusionManager.h, DiffusionGrid is its shell there). It keeps a
// colour cell per tile corner, a link per tile naming its four cells, and a light cell per lit tile; the diffusion's
// passes (rows LD2b and LD2c, ours) fill and step them, and the rebuild 0x40ac20 / 0x409a20 (row LD2g, ours) builds
// them. RVAs 1.18.0.187; all /O2.
//
// Layout (0x84; vtable 0xdbeb74, one slot: the scalar deleting destructor 0x3c4870, lane HX's). The vtable is the
// image's (the class has no other virtual), stored by the constructors and the destructor.
//
// Ours (LD2a): the constructor 0x4056d0, the copy 0x3c42c0, the destructor 0x4058e0, SetSize 0x4098d0, the accessors
// 0x409880 / 0x40abd0 / 0x40abe0, the colour setters 0x4098b0 / 0x409a00, the collectors 0x405910 / 0x405a00 / 0x405ae0,
// Reset 0x406f40 and Mark 0x405bc0. The trivial setters of +0x10, +0x14, +0x3c and +0x80 (0x4098a0, 0x4099f0, 0x409890,
// 0x239840) are folded with other classes' (never hooked); cDiffusionManager calls them as the original's.
// Ours (LD2c, lane 261109q): Step 0x405d70 and its passes (ResetColors 0x408860, LightReaches 0x406ff0, ApplyLight
// 0x4089a0, Propagate 0x409170, Spread 0x409080), GetColor 0x408e30, and the rebuild's leaves LevelAt 0x408790,
// MarkFloorTiles 0x40a9e0, LinkSamples 0x40a610 and PruneCorner 0x40a790.
// Ours (LD2g, lane 261110b): Rebuild 0x40ac20 and BuildCells 0x409a20; through them the element constructors 0x405810,
// 0x405850 and 0x4058b0 (FloorColorSample, FloorLightHalf, FloorColorLink).
#include "game/owned.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

namespace nTSSG {

// What the floor collectors push (16 bytes): a light cell's tile, the grid's level and a flag byte at +0xc. Bytes
// +0xd..+0xf are padding: the original writes the fields alone, into a free slot or a local its reallocation moves
// field by field, so they hold whatever the memory did (ours too) and carry nothing.
struct DiffusionTile {
    uint32_t words[4];
};

// A colour sample (0x24; the constructor 0x405810, two per FloorColorCell). Mark adds to mColor where mLit is set and
// mLevel matches. The constructor leaves +0x1d and +0x20..+0x23 alone (value-initialised cells have them zero).
struct FloorColorSample {
    float mColor[3];   // +0x00
    float mOther[3];   // +0x0c
    uint8_t m18;       // +0x18 0xff
    uint8_t m19;       // +0x19 0
    uint8_t m1a;       // +0x1a 1
    uint8_t mLit;      // +0x1b 0
    uint8_t m1c;       // +0x1c 0
    uint8_t m1d;       // +0x1d not set
    int16_t m1e;       // +0x1e 0
    int16_t mLevel;    // +0x20 not set
    uint8_t mPad22[2]; // +0x22 not set
    FloorColorSample();
};
static_assert(sizeof(FloorColorSample) == 0x24 && offsetof(FloorColorSample, mLevel) == 0x20, "FloorColorSample");

// A colour cell (0x48): two samples. No constructor of its own, so std::vector value-initialises it (zeroed, then the
// samples' constructor), as the original's 0x405640 does.
struct FloorColorCell {
    FloorColorSample mSamples[2];
};
static_assert(sizeof(FloorColorCell) == 0x48, "FloorColorCell");

// A tile's link (0x18; the constructor 0x4058b0, not zeroed first): the four colour cells of its corners.
struct FloorColorLink {
    int16_t m0;          // +0x00 0; its low byte the lit flag, its high byte (+0x01) the tile's kind (Kind)
    // +0x02 / +0x03 (not set by the constructor): BuildCells' quadrant masks of a lit tile, the quadrants (1, 2, 4, 8)
    // whose level id is 0 and those whose is not; Rebuild copies them into the tile's light cell (mFlagA, mFlagB).
    uint8_t mGroundQuads;
    uint8_t mUpperQuads;
    uint8_t m4;          // +0x04 0; BuildCells' wall sides (1, 2, 4, 8, 0x10, 0x20: a cut with no 0x7defd0 answer)
    uint8_t mPad5[3];    // not set
    uint32_t mCells[4];  // +0x08 indices into the colour cells
    FloorColorLink();
    // The byte at +0x01: 0 a whole tile (one half, a quad), 1 and 2 a tile split along one diagonal or the other (two
    // halves, triangles), anything else not drawn (LD2b's readers test the byte alone).
    uint8_t Kind() const { return static_cast<uint8_t>(static_cast<uint16_t>(m0) >> 8); }
};
static_assert(sizeof(FloorColorLink) == 0x18 && offsetof(FloorColorLink, mCells) == 8, "FloorColorLink");

struct FloorColorVec {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// Half a light cell (0x184; the constructor 0x405850): Reset copies mCurrent over mSaved where mSave is set.
// mCurrent is a 4x4 grid of colours (row-major, entry = column + row * 4) over the tile and its border: the centre
// 2x2 (entries 5, 6, 9, 10) from the tile's four corner cells, the ring from the neighbours (PrepareLight). The shaders
// sample it bilinearly into the light map's pixels.
struct FloorLightHalf {
    FloorColorVec mCurrent[16]; // +0x000
    FloorColorVec mSaved[16];   // +0x0c0
    uint8_t mSave;              // +0x180 not set by the constructor; PrepareLight sets it when a corner sample is lit
    uint8_t mPad181[3];

    // 0x4080c0 (ret 0x20): the half's colours over the rectangle [left, right) x [top, bottom) of `pixels` (rows
    // `pitch` pixels apart), each pixel 0xff000000 | r << 16 | g << 8 | b with the channels scaled by 2 * scale.
    // `levelZero` samples pixel centres (ground level), otherwise the edges map to the grid's ends.
    void ShadeQuad(uint32_t left, uint32_t top, uint32_t right, uint32_t bottom, uint32_t *pixels, int32_t pitch,
                   float scale, bool levelZero);
    // 0x4072a0 (ret 0x2c): as ShadeQuad over one triangle: a pixel at (u, v) in [0, 1] is drawn unless
    // (threshold > u * slope + v) equals `side` (the byte compared whole).
    void ShadeTriangle(float slope, float threshold, uint8_t side, uint32_t left, uint32_t top, uint32_t right,
                       uint32_t bottom, uint32_t *pixels, int32_t pitch, float scale, bool levelZero);
};
static_assert(sizeof(FloorLightHalf) == 0x184 && offsetof(FloorLightHalf, mSave) == 0x180, "FloorLightHalf");

// A light cell (0x318), value-initialised (0x405680: zeroed, then both halves' constructor).
struct FloorLightCell {
    FloorLightHalf mHalves[2]; // +0x000 the second used by split tiles (link kind non-zero)
    uint8_t m308;              // +0x308 PrepareLight: the tile's link kind non-zero
    uint8_t mPad309[3];
    int32_t mX;                // +0x30c the tile
    int32_t mY;                // +0x310
    uint8_t mActive;           // +0x314
    uint8_t mFlagA;            // +0x315
    uint8_t mFlagB;            // +0x316
    uint8_t mPad317;

    // 0x4085e0 (ret 0x24): the cell into the light map by the tile's link kind: 0 the first half as a quad, 1 and 2
    // each lit half as a triangle (slope 1 or -1); any other kind draws nothing.
    void Shade(uint32_t kind, uint32_t left, uint32_t top, uint32_t right, uint32_t bottom, uint32_t *pixels,
               int32_t pitch, float scale, bool levelZero);
};
static_assert(sizeof(FloorLightCell) == 0x318 && offsetof(FloorLightCell, mX) == 0x30c &&
                  offsetof(FloorLightCell, mActive) == 0x314,
              "FloorLightCell");

struct FloorKey; // sg/cLightMapManager.h
class cILightMapManager;

// A light as the floor step reads it (0x6c; the diffusion's light records, cDiffusionManager's step argument). Only the
// fields named here are read.
struct FloorLight {
    float mColor[3];   // +0x00
    uint32_t m0c[2];   // +0x0c
    int32_t mLevel;    // +0x14 the light's level
    uint32_t m18[8];   // +0x18
    float mKey;        // +0x38 the sort key (brightest first); also scales the spread rate
    uint32_t m3c;      // +0x3c
    float mScale;      // +0x40 the colour's multiplier
    float mFalloff;    // +0x44 the share lost per level down (1 or more: none reaches below)
    uint32_t m48[4];   // +0x48
    float mMin[2];     // +0x58 the box it lights (tiles; x, y)
    float mMax[2];     // +0x60
    int16_t mLevelId;  // +0x68 the samples' level id it lights
    uint8_t m6a[2];
};
static_assert(sizeof(FloorLight) == 0x6c && offsetof(FloorLight, mKey) == 0x38 && offsetof(FloorLight, mMin) == 0x58 &&
                  offsetof(FloorLight, mLevelId) == 0x68,
              "FloorLight");

// A light blob (0x2c) TakeBlobs reads: cDiffusionManager's DiffusionBlob44 (src/sg/cDiffusionManager.h), the records
// the wall grids push. Only the key, the direction and the mode are read here.
struct FloorBlob {
    int32_t mX;          // +0x00 the tile
    int32_t mY;          // +0x04
    int32_t mLevel;      // +0x08
    uint32_t m0c[7];     // +0x0c not read here
    uint8_t mDir;        // +0x28 a corner direction (odd) or a side (even, skipped)
    uint8_t mMode;       // +0x29 non-zero: the 2x2 blend; zero: the 4x4
    uint8_t m2a[2];      // +0x2a not read here
    FloorKey *Key(FloorKey *out) const; // 0x3c3a50 (ret 4): the first three words copied out; returns out
};
static_assert(sizeof(FloorBlob) == 0x2c && offsetof(FloorBlob, mDir) == 0x28, "FloorBlob");

class cFloorColorGrid {
public:
    cFloorColorGrid();                              // 0x4056d0: +0x68 left unset
    cFloorColorGrid(const cFloorColorGrid &other);  // 0x3c42c0: memberwise, the vtable stored
    cFloorColorGrid &operator=(const cFloorColorGrid &) = delete;
    ~cFloorColorGrid();                             // 0x4058e0 (not virtual: the image's vtable's slot 0 calls it)
    cFloorColorGrid *DeletingDestructor(uint32_t flags); // 0x3c4870, the vtable's slot 0 (lane HX's, moved here)

    // The hooks' and the tests' ways in (thiscall in the original).
    static cFloorColorGrid *__fastcall Construct(void *memory, void *);
    static cFloorColorGrid *__fastcall CopyConstruct(void *memory, void *, const cFloorColorGrid *other);
    static void __fastcall Destroy(cFloorColorGrid *self, void *);

    void SetSize(int32_t level, uint32_t width, uint32_t height); // 0x4098d0 (ret 0xc; eax left as is)
    FloorColorCell *Cell(uint32_t index);                          // 0x409880
    FloorColorLink *Link(uint32_t index);                          // 0x40abd0
    uint32_t CellIndex(int32_t x, int32_t y);                      // 0x40abe0: the tile clamped into the grid
    void SetInsideAmbientColor(const float *color);                // 0x4098b0
    void SetOutsideAmbientColor(const float *color);               // 0x409a00
    void CollectTiles(std::vector<DiffusionTile> *out);            // 0x405910: active cells, flag A | B
    void CollectTilesB(std::vector<DiffusionTile> *out);           // 0x405a00: active cells with flag B
    void CollectTilesA(std::vector<DiffusionTile> *out);           // 0x405ae0: active cells with flag A
    void Reset();                                                  // 0x406f40
    void Mark(uint32_t level, const float *amount);                // 0x405bc0: half the amount added at the level

    // LD2b (lane 261109l): the light cells.
    // 0x4087e0 (ret 0x10): the sample next to sub-cell (x, y) in direction `dir` (0..7, kDirX/kDirY), or null when
    // the sub-cell's chosen sample (the first or second) has the direction's bit clear in m18; m19's bit picks the
    // neighbour's second sample.
    FloorColorSample *NeighbourSample(int32_t x, int32_t y, uint32_t dir, bool first);
    // 0x4077a0 (ret 0xc): the light cell's 4x4 colours for tile (x, y): the centre from the tile's corner cells (lit
    // samples' colours, else the inside or outside ambient), the ring from lit neighbours (or the centre).
    void PrepareLight(int32_t x, int32_t y, FloorLightCell *cell);
    // 0x4060d0 (ret 8): every light cell prepared, then shaded into its light map rectangle.
    void ShadeLights(cILightMapManager *lights, float scale);
    // 0x406300 (ret 8): every light cell prepared, mActive set where a lit half's colours changed since Reset, and
    // the active cells shaded.
    void ShadeChangedLights(cILightMapManager *lights, float scale);
    // 0x4065b0 (ret 4): the blobs at this grid's level and a corner direction blended into the colour cells there,
    // toward the ambient colours.
    void TakeBlobs(const std::vector<FloorBlob> *blobs);

    // LD2c (lane 261109q): the step and the rebuild's leaves. Sub-cells are (x, y) in 0..2w-1 x 0..2h-1 (two per
    // tile each way), the cell x + y * 2w.
    // 0x409080 (static stdcall, ret 0x10): `to` pulled toward `from` (both lit) per channel where from - to exceeds
    // to * rate (rate * 1.4 for a corner direction): to = from / (rate + 1) + 0.01. Returns the channels changed.
    static int32_t __stdcall Spread(FloorColorSample *to, const FloorColorSample *from, uint8_t dir, float rate);
    // 0x408790 (static cdecl): the partitions' level id at (x, y, level) (the world DB's slot 39, its slot 80).
    static uint32_t __cdecl LevelAt(void *world, float x, float y, int32_t level);
    // 0x408860: every lit sample's colour reset to the inside or outside ambient (m1a), and m1c cleared.
    void ResetColors();
    // 0x406ff0 (ret 4): whether a sub-cell in the light's box has a lit sample at its level id whose way down from the
    // light's level is open (no floor in between, the world DB's slot 52).
    bool LightReaches(const FloorLight *light);
    // 0x4089a0 (ret 0xc): the light into the lit samples of its box (each channel raised to colour * share), share
    // falling by mFalloff a level; the changed sub-cells grow the box min..max (empty while max.x < min.x).
    bool ApplyLight(const FloorLight *light, int32_t *min, int32_t *max);
    // 0x409170 (ret 0x18): up to `passes` sweeps spreading the changed samples' colour into their lit neighbours
    // around the box, until a sweep changes nothing.
    void Propagate(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY, uint32_t passes, float rate);
    // 0x405d70 (ret 4): the lights that reach this grid, brightest first, applied and spread in groups of near keys.
    void Step(const std::vector<FloorLight> *lights);
    // 0x408e30 (ret 8): the floor colour at a position (x, y in tiles): twice the sample under it, less the outside
    // ambient where m40 and the sample's m1a; the image's 0x10da6fc (zero) off the lit floor. Returns out.
    float *GetColor(float *out, const float *position);
    // 0x40a9e0: each tile's link lit where the world DB has the tile inside with a floor (slots 71, 53 / 55), its four
    // cells named and their samples tagged.
    void MarkFloorTiles();
    // 0x40a610 (static stdcall, ret 0xc): the two cells' samples linked across direction `dir` (b is a's neighbour
    // there): the m18 bit cleared where no lit partner, m19's bit naming the partner's sample.
    static void __stdcall LinkSamples(FloorColorCell *a, FloorColorCell *b, uint8_t dir);
    // 0x40a790 (ret 0xc): a corner direction's link at sub-cell (x, y) dropped (both ways) unless one of the two
    // sides next to it links consistently around the corner.
    void PruneCorner(int32_t x, int32_t y, uint8_t dir);

    // LD2g (lane 261110b): the rebuild.
    // 0x409a20: for each tile of the world's box at this level whose link is lit, the cuts the partitions report
    // (slot 43 per side bit 1..0x20, the cut's catalog flag or id 0x1d) clear the cells' direction links and set the
    // link's wall sides and kind (0x10 and 0x20 split the tile, the second samples copied from the first); then each
    // half's level id (LevelAt at the tile's centre or the halves' quarter points) fills the samples' mLevel / m1a, the
    // link's quadrant masks and mLevels. Last, every sub-cell's links to its neighbours (LinkSamples, off-grid
    // directions cleared) and the corners pruned (PruneCorner).
    void BuildCells();
    // 0x40ac20: the cells and links reset to blank, MarkFloorTiles, BuildCells, and a fresh light cell per lit link
    // (its tile, the link's quadrant masks; the saved colours zeroed). Restarts a stopwatch it never reads.
    void Rebuild();

    const OwnedVTable *mVTable;                    // +0x00
    float m04;                                     // +0x04 0x3f7f7cee
    float m08 = 1.0f;                              // +0x08
    float m0c;                                     // +0x0c 0x3f4ed917
    float mInsideAmbient = 1.0f;                   // +0x10 (0x4098a0)
    float mOutsideAmbient = 1.0f;                  // +0x14 (0x4099f0)
    float m18[3] = {1.0f, 1.0f, 1.0f};             // +0x18 (the image's 0x10114f4..fc, read only by the constructor)
    float mInsideAmbientColor[3] = {0.1f, 0.1f, 0.1f}; // +0x24
    float mOutsideAmbientColor[3] = {0.5f, 0.5f, 0.5f}; // +0x30
    float mThreshold;                              // +0x3c 0x3e0c49ba (0x409890)
    uint8_t m40 = 1;                               // +0x40
    int32_t mLevel = 0;                            // +0x44
    uint32_t mWidth = 0;                           // +0x48
    uint32_t mHeight = 0;                          // +0x4c
    std::vector<FloorColorCell> mCells;            // +0x50 four per tile
    std::vector<FloorColorLink> mLinks;            // +0x5c one per tile
    int32_t m68;                                   // +0x68 not set by the constructor
    std::vector<FloorLightCell> mLights;           // +0x6c
    std::set<int16_t> mLevels;                     // +0x78 the levels Mark adds at
    int32_t mSteps = 1;                            // +0x80 (0x239840)
};
static_assert(offsetof(cFloorColorGrid, m04) == 4 && offsetof(cFloorColorGrid, m18) == 0x18 &&
                  offsetof(cFloorColorGrid, mInsideAmbientColor) == 0x24 &&
                  offsetof(cFloorColorGrid, mOutsideAmbientColor) == 0x30 &&
                  offsetof(cFloorColorGrid, mThreshold) == 0x3c && offsetof(cFloorColorGrid, m40) == 0x40 &&
                  offsetof(cFloorColorGrid, mLevel) == 0x44 && offsetof(cFloorColorGrid, mCells) == 0x50 &&
                  offsetof(cFloorColorGrid, mLinks) == 0x5c && offsetof(cFloorColorGrid, m68) == 0x68 &&
                  offsetof(cFloorColorGrid, mLights) == 0x6c && offsetof(cFloorColorGrid, mLevels) == 0x78 &&
                  offsetof(cFloorColorGrid, mSteps) == 0x80,
              "cFloorColorGrid layout");
static_assert(sizeof(cFloorColorGrid) == 0x84, "cFloorColorGrid size");

} // namespace nTSSG
