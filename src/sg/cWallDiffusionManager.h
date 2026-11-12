#pragma once
// nTSSG::cWallDiffusionManager (lane 261110c, scenegraph LD2d): the walls' half of the diffusion engine. cDiffusionManager
// (sg/cDiffusionManager.h) makes one in Start and holds it counted; it groups the lot's wall pieces into runs (walls in a
// line at one level, 0x411ca0), and each run into segments (one per wall piece: its ends, its tile location, the
// colours diffused along it and the matching segments on the levels above and below). This header is the shape (the manager, its run and segment records) and the leaves of row
// LD2d: the constructor and destructors, the settings, the run's accessors, the Fixed16 helpers and the angle tests.
// Row LD2h's bottom (lane 261110d): the packed-colour writers, the ambient, gradient, test-colour and border passes, the
// neighbour search, the walk over the references and the point along a run. Row LD2i (lane 261110e): the propagation
// DiffuseMarked 0x40f0f0 and everything under it. Rows LD2e, LD2j, LD2k: the entry points, Step's maths, the rebuild.
// Row LD2f (lane 261110i): Step 0x40c2e0 and the blob pass (0x40edb0, 0x40fa40, 0x4104a0, 0x4104e0). Row LD2l (lane
// 261110j): the blends 0x40f6a0 / 0x40f800 / 0x40f1c0 / 0x40f390. Row LD2m (lane 261110k): ApplyLights 0x40dd30, its
// packing 0x4106a0 and the wrappers 0x40ed70 / 0x40ed90.
//
// Standalone shape: a cRZRefCount (the game's vtable 0xdc24f4: slot 0 the scalar deleting destructor 0x40bff0, slots 1-4
// the count's shared bodies), 0x60 bytes. The game's vector, set and allocator instances over these records
// (0x40aed0-0x40bad0, 0x40be30, 0x40bf50, 0x4122a0-0x412510) are library code.
//
// Game code still called (original_calls): the world's cell query 0x7f2260 (__cdecl; not read further) in FrontCellQuery
// and BuildRuns; row LD2k (lane 261110h): the partitions' getter 0x20de97 (cPartitionManager's service, not ours) in
// AddSegment and BuildAll, the world's getter 0x20e0b2 in LinkLevels; row LD2l: the scene graph system's getter 0x20defb
// in BlendLightMap; row LD2m: the partitions' getter 0x20de97 and its slot 50 (the catalog id) in ApplyLights.
#include "rz/RZRefCountBase.h"

#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

namespace nTSWorld {
class cIWallSection;
}

namespace nTSSG {

struct DiffusionWall;   // sg/cDiffusionManager.h: the wall lists' 24-byte records (WallDiffusionChange's layout)
struct DiffusionBlob44; // sg/cDiffusionManager.h: the 44-byte blobs CollectBlobs 0x40fa40 appends (WallBlob's layout)
class cIDiffusionManager; // sg/cDiffusionManager.h: BlendUnlit's floor colours (its slot 23, GetFloorColor)
class cILightMapManager;  // sg/cLightMapManager.h: ApplyLights' wall rects and images

// A 16.16 fixed-point value (4 bytes), made by rounding through the 1.5 * 2^23 magic constant (SSE, to nearest even).
struct Fixed16 {
    int32_t mValue;

    // 0x40bc60 (ret 4, returns this): round(f * 65536).
    Fixed16 *SetFromFloat(float f);
    // 0x40bc00 (ret 8, returns this): round(65536 * (float)(0.0625 / (1 + f * (scaled ? 1.4 : 1)))), the distance
    // falloff of a diffused colour (0.0625 at a distance of 0) in double, then float.
    Fixed16 *SetFalloff(bool scaled, float f);
};
static_assert(sizeof(Fixed16) == 4, "Fixed16");

// One wall piece of a run (0x178 bytes; 0x40c680 builds it from the piece's two ends and its tile location). The default
// constructor leaves the packed colours and both colour blocks as they were.
//
// A reference to another run's segment (12 bytes; {-1, -1, -1} for none, set by the three-word constructor 0x40be30,
// folded with a library _Vector_val): 0x40f960 finds the segment whose location matches and returns {its location's
// level word, the run index, the segment index}.
struct WallSegmentRef {
    int32_t mLevel;
    int32_t mRun;
    int32_t mSegment;

    // 0x410250 (18 bytes; al only): a run and a segment (neither negative). The level word is not looked at.
    bool IsValid() const;
};
static_assert(sizeof(WallSegmentRef) == 12, "WallSegmentRef");

// The light records Step 0x40c2e0 walks (0x6c: its count is the vector's byte length / 0x6c, the stride 0x6c; the same
// records as cFloorColorGrid.h's FloorLight, cDiffusionManager's step argument). The fields Step and Light 0x40c040 read
// are named; the rest is neither read nor written by the walls.
struct WallLight {
    float mColor[3];        // +0x00
    uint32_t mPositionBits[2]; // +0x0c the light's x, y (cPortalLight::mPosition, the same record; not read by the walls)
    int32_t mLevel;         // +0x14 the light's level (compared signed with a run's level word)
    uint32_t mCellAndLevelBits[3];  // +0x18 cPortalLight's mCell[2] and mLevelAgain (the same record; not read by the walls)
    uint8_t mDirectional;   // +0x24 nonzero: the facing below weighs the light
    uint8_t mPad25[0x0b];   // +0x25
    float mAngle;           // +0x30 its direction in radians (cos, sin)
    float mFocus;           // +0x34 clamped to 0..1: how much the facing matters
    float mSpread;          // +0x38 times the manager's mThreshold: Light's spread (FloorLight's mKey)
    float mIntensity;       // +0x3c Light's starting intensity
    float mScale;           // +0x40 (the floor's colour multiplier; not read by the walls)
    float mFalloff;         // +0x44 the share lost per level down
    uint32_t mSourceBoxBits[4];   // +0x48 cPortalLight's mSourceMin / mSourceMax (the same record; not read by the walls)
    float mMin[2];          // +0x58 the box it lights (x, y): Step lights from its centre
    float mMax[2];          // +0x60
    int16_t mLevelId;       // +0x68 compared with the low 16 bits of a run's FrontCellQuery
    uint8_t mPad6A[2];         // +0x6a
};
static_assert(sizeof(WallLight) == 0x6c, "WallLight size");
static_assert(offsetof(WallLight, mLevel) == 0x14 && offsetof(WallLight, mDirectional) == 0x24 &&
                  offsetof(WallLight, mAngle) == 0x30 && offsetof(WallLight, mFocus) == 0x34,
              "WallLight fields");
static_assert(offsetof(WallLight, mSpread) == 0x38 && offsetof(WallLight, mIntensity) == 0x3c &&
                  offsetof(WallLight, mFalloff) == 0x44 && offsetof(WallLight, mMin) == 0x58 &&
                  offsetof(WallLight, mMax) == 0x60 && offsetof(WallLight, mLevelId) == 0x68,
              "WallLight Step fields");

struct WallDiffusionSegment {
    float mStart[3];          // +0x000 the piece's ends (0 at first)
    float mEnd[3];            // +0x00c
    uint32_t mLocation[5];    // +0x018 the tile location 0x7ef3b0 makes from the ends ({-1, -1, 0, 0, 0} at first);
                              //        word 2 its level (0x40d850 asks about word 2 + 1 and - 1)
    uint32_t mPacked[12];     // +0x02c two sides of six packed ARGB colours (left unset)
    uint8_t mCurrent[0x80];   // +0x05c this pass's colour block (left unset)
    uint8_t mPrevious[0x80];  // +0x0dc the last pass's (Reset 0x40eff0 copies mCurrent here; left unset)
    WallSegmentRef mAbove;    // +0x15c the matching segment one level up (0x40d850, through the run), or none
    WallSegmentRef mBelow;    // +0x168 one level down
    uint8_t mFlag;            // +0x174 1 at first
    uint8_t mPad175[3];       //        never written, never copied

    // 0x40bd90.
    WallDiffusionSegment();
    // 0x40bcd0 (ret 4): every field; the padding left as it was.
    WallDiffusionSegment(const WallDiffusionSegment &other);
    WallDiffusionSegment &operator=(const WallDiffusionSegment &other);

    // 0x4114a0 / 0x4114d0 (7 bytes each): the two references' addresses.
    WallSegmentRef *Above() { return &mAbove; }
    WallSegmentRef *Below() { return &mBelow; }
    // 0x411bc0 / 0x411c00 (ret 4): the two references set.
    void SetAbove(const WallSegmentRef *ref);
    void SetBelow(const WallSegmentRef *ref);

    // ---- the packed colours (row LD2h; PackWallColor below) ----
    // 0x40cf30 (ret 4): the colour packed into all twelve words of mPacked.
    void SetPacked(const float *rgb);
    // 0x40d180 (ret 8): into side `side`'s six words (mPacked[6 * side ...], not checked).
    void SetSidePacked(const float *rgb, int32_t side);
    // 0x40d3c0 (ret 8): into the eight words of mCurrent's row `row` (row * 0x20, not checked).
    void SetRow(const float *rgb, int32_t row);
    // 0x40d260 (ret 8; al only, the rest of eax is MMX junk): side `side`'s six words raised to the colour byte by byte
    // (each channel, alpha too, the greater of the two); whether any byte changed.
    bool MergeMax(const float *rgb, int32_t side);
    // 0x40da80 (ret 8): mCurrent as a 4 x 8 block of packed words: rows 1 and 2, words 1-6, the two sides of mPacked; the
    // border (row 0, row 3, words 0 and 7) from the eight neighbours 0x40fe70 finds (n[0] the segment before, n[1-3]
    // above it before / at / after, n[4] the one after, n[5-7] below it after / at / before), or from the block's own
    // edge where a neighbour is missing. The second argument (the pass's change list) is not used.
    void FillBlock(WallDiffusionSegment *const *n, void *unused);

    // ---- the propagation (row LD2i) ----
    // mPacked read as a 2 x 6 grid, [a][h] = mPacked[6 * a + h]: a along the run (the segment's two halves: half 1 of a
    // segment meets half 0 of the next), h up the wall (an upward exchange meets this one's h 0 with the segment above's
    // h 5). Each spread is integer MMX on the four bytes of a word (alpha too): s = (from << 4) * w >> 16 (pmulhw by the
    // falloff's low 16 bits, signed), then to = max(s, to) saturated to 0..255 (pcmpgtw, packuswb); a word "rises" when s
    // is above to (also when to was 255 and s above it: a falloff above 1/16 never settles).
    // 0x411380 (ret 0x10; al only, eax's upper bytes MMX junk; ecx not read, and the MMX state left set: its caller
    // 0x410c10 ends with emms): `count` words of `from` spread into `to` by falloff's low word; whether any rose.
    static bool __stdcall SpreadWords(const Fixed16 *falloff, const uint32_t *from, uint32_t *to, int32_t count);
    // 0x410c10 (ret 0xc; al only): from the run's neighbouring segments (either may be null): prev's half 1 into this
    // one's half 0 (straight across by SetFalloff(false, distance), and one step up or down the wall by SetFalloff(true,
    // distance)), then next's half 0 into this one's half 1 the same way; whether any word rose.
    bool SpreadAlong(const WallDiffusionSegment *prev, const WallDiffusionSegment *next, float distance);
    // 0x410d50 (ret 0xc; al only): an exchange with the segment above (up: this h 0 <-> its h 5) or below (this h 5 <->
    // its h 0) in both halves, each half first into this one, then this one's (new) word back; SetFalloff(false,
    // distance) inlined. Whether any word rose.
    bool SpreadAcross(WallDiffusionSegment *other, float distance, uint8_t up);
    // 0x411160 (ret 4; al only): the grid's own spread, in place: for each half a, then each of the eight neighbours
    // (0xdc2508's {dh, da} pairs, even ones straight by SetFalloff(false), odd ones diagonal by SetFalloff(true)) whose
    // half lies in the grid, every h whose h + dh does, ascending: [a][h] from [a + da][h + dh]. Whether any word rose.
    bool SpreadWithin(float distance);

    // ---- the blob pass (row LD2f) ----
    // 0x4104e0 (ret 0xc; nothing in eax, and ends with emms): mCurrent's row `row` (row * 0x20, not checked) blended
    // towards the colour: P = PackWallColor of rgb times scale (MulSs per channel, alpha 0xff), w = the low 16 bits of
    // bits(((scale - 1) * 0.5) * 65536 + 1.5 * 2^23) (round((scale - 1) * 32768) inside the magic window), then each of
    // the eight words' four bytes b (alpha too) becomes P's byte less (w * 2b) >> 16 (pmulhw, signed), saturated to 0..255
    // (packuswb). So about P + (1 - scale) b.
    void BlendRow(const float *rgb, float scale, int32_t row);
};
static_assert(sizeof(WallDiffusionSegment) == 0x178, "WallDiffusionSegment size");
static_assert(offsetof(WallDiffusionSegment, mLocation) == 0x18 && offsetof(WallDiffusionSegment, mPacked) == 0x2c &&
                  offsetof(WallDiffusionSegment, mCurrent) == 0x5c && offsetof(WallDiffusionSegment, mPrevious) == 0xdc,
              "WallDiffusionSegment blocks");
static_assert(offsetof(WallDiffusionSegment, mAbove) == 0x15c && offsetof(WallDiffusionSegment, mBelow) == 0x168 &&
                  offsetof(WallDiffusionSegment, mFlag) == 0x174,
              "WallDiffusionSegment references");

// A run of wall pieces in one line (0x1c bytes): the segments, the neighbouring runs' indices and the line's angle.
struct WallDiffusionRun {
    std::vector<WallDiffusionSegment> mSegments; // +0x00
    int32_t mPrev;                               // +0x0c the run before it in a chain, or -1 (0x411ca0 links them)
    int32_t mNext;                               // +0x10 the run after it, or -1
    float mAngle;                                // +0x14 the line's angle in radians (0 at first; folded getter 0x410160)
    uint8_t mDiagonal;                           // +0x18 SetAngle's: the angle within pi/8 of a diagonal
    uint8_t mFlag19;                             // +0x19 getter 0x410220, setter 0x4119e0 (the passes' 0x40d010 / 0x4119f0)
    uint8_t mFlag1A;                             // +0x1a getter 0x410230 (folded), setter 0x411b50 (0x40fa40's)
    uint8_t mFlag1B;                             // +0x1b getter 0x410240, setter 0x411b90 (folded; 0x40fa40's)

    // 0x40bc90.
    WallDiffusionRun();
    // Inlined in the vector instance 0x40bb20 (library): the segments copied, then every field.
    WallDiffusionRun(const WallDiffusionRun &other) = default;
    WallDiffusionRun &operator=(const WallDiffusionRun &other) = default;
    // The vector instance's growth (0x40b100's 0x40b940) moves the old runs: their segment vectors' blocks change hands
    // (capacities kept), the fields copied, the source's three vector words cleared. Ours moves likewise (noexcept, so
    // the library's growth takes it).
    WallDiffusionRun(WallDiffusionRun &&other) noexcept = default;
    WallDiffusionRun &operator=(WallDiffusionRun &&other) noexcept = default;

    // 0x410680: the number of segments.
    int32_t SegmentCount() const;
    // 0x410660: twice that (a segment's two sides).
    int32_t SideCount() const;
    // 0x410270: the first segment's location (not checked for an empty run).
    uint32_t *FrontLocation();
    // 0x410310: the first segment's location word 2 (its level).
    uint32_t FrontLocationWord2() const;
    // 0x411420: the last segment's location (not checked).
    uint32_t *BackLocation();
    // 0x411460: the world's 0x7f2260 about the first segment's location (flag 0); its answer as it is.
    uint32_t FrontCellQuery();
    // 0x4114e0 (ret 4): segment i's location.
    uint32_t *LocationAt(uint32_t i);
    // 0x411480 / 0x4114b0 (ret 4): segment i's references (through the segment's 0x4114a0 / 0x4114d0).
    WallSegmentRef *AboveAt(uint32_t i);
    WallSegmentRef *BelowAt(uint32_t i);
    // 0x411ba0 / 0x411be0 (ret 8): segment i's references set (through the segment's 0x411bc0 / 0x411c00).
    void SetAboveAt(uint32_t i, const WallSegmentRef *ref);
    void SetBelowAt(uint32_t i, const WallSegmentRef *ref);
    // 0x40d570: every segment dropped, the angle 0.
    void Clear();
    // 0x411960 (ret 4): the angle set; diagonal (and returned) when it lies within [pi/8, 3pi/8] or [5pi/8, 7pi/8]
    // either side of 0 (bounds the float values of those, compared in double).
    bool SetAngle(float angle);
    // 0x410280: the run's length in tiles: the segment count times sqrt(2) when diagonal (the root rounded to float),
    // in double, returned as float.
    float Length() const;
    // 0x4119e0 / 0x411b50 (ret 4): the argument's low byte as it is.
    void SetFlag19(uint8_t value) { mFlag19 = value; }
    void SetFlag1A(uint8_t value) { mFlag1A = value; }
    // Folded or under 5 bytes (never hooked): 0x410160 mAngle, 0x410220 / 0x410230 / 0x410240 the flags,
    // 0x411b90 mFlag1B's setter.
    void SetFlag1B(uint8_t value) { mFlag1B = value; }

    // ---- row LD2h ----
    // 0x40cec0 (ret 4): every segment's SetPacked.
    void SetPacked(const float *rgb);
    // 0x40d090: a debug gradient: side k of the run's 2n sides (k = 2i + side) gets {k / 2n, 0, 1 - k / 2n}, the
    // reciprocal of 2n in float from the unsigned count.
    void PaintGradient();
    // 0x40d4b0: a debug pattern: each segment's mCurrent rows 0, 1 and 3 red, blue and green (row 2 left).
    void PaintTestColors();
    // 0x410760 (ret 0xc, returns out): the point a fraction t (clamped to [0, 1]; NaN kept) of the run's length along
    // the first segment's direction from its start, moved `offset` along the direction's normal (normalised when its
    // length is above 0). Only x and y are written.
    float *PointAlong(float *out, float t, float offset);
    // 0x40fe70 (ret 0xc): segment i's eight neighbours (FillBlock's n[]) through its references into `runs`; where the
    // segment has no segment above (or below), the corner ones from the neighbouring segments' references.
    void Neighbours(std::vector<WallDiffusionRun> *runs, uint32_t i, WallDiffusionSegment **out);
    // 0x4119f0 (ret 0xc; the low bytes of the two words): mFlag19 = flag, then the runs the segments reference upwards
    // (up) or downwards: their mFlag1A cleared, then each one still clear walked the same way and marked.
    void Walk(std::vector<WallDiffusionRun> *runs, uint8_t flag, uint8_t up);

    // ---- row LD2l (lane 261110j): the floor's light onto the run ----
    // 0x40f390 (ret 4; bool in al, the rest of eax junk): the scene graph system's (0x20defb) light map manager (its slot
    // 48; AddRef'd, Released at the end, both when not null). For each segment (count re-read, unsigned) whose Below
    // reference is not valid, its two halves k = 0, 1: the point PointAlong(t, 0.125) at t = (2i + k + 0.5) * step in
    // double, step = 1.0f / (float)(2n, unsigned) widened; the light map's GetFloorLight (slot 16) at {x, y, the first
    // segment's level word}, each channel times weight * 0.5 (MulSs), MergeMax'd into half k. Whether any merge
    // changed a byte. FrontLocationWord2 is read before the count (an empty run's first segment unchecked).
    bool BlendLightMap(float weight);
    // 0x40f1c0 (ret 8; the same): as BlendLightMap with `floors`' GetFloorColor (slot 23) for the light map's slot 16
    // (no AddRef / Release).
    bool BlendFloors(cIDiffusionManager *floors, float weight);

    // ---- the propagation (row LD2i; distance is the manager's mThreshold, each bool al only) ----
    // Both recursive walks first clear mFlag1A of every run the segments reference (up: above, else below), then visit
    // each referenced run still clear and mark it; nothing stops a cycle (the references climb or descend a level, so the
    // game's graph has none), and a run two paths reach is visited once per path.
    // 0x410fe0 (ret 0x10): with `apply`, each segment's SpreadWithin; then the referenced runs the same way (apply 1).
    // Whether any word rose.
    bool DiffuseWithin(std::vector<WallDiffusionRun> *runs, float distance, uint8_t up, uint8_t apply);
    // 0x4108d0 (ret 0x10): with `apply` and two or more segments, SpreadAlong of the first, the last, then the middle
    // ones in order; then each segment's SpreadAcross with the segment it references (whatever `apply`); then the
    // referenced runs the same way (apply 1). Whether any word rose.
    bool DiffuseBetween(std::vector<WallDiffusionRun> *runs, float distance, uint8_t up, uint8_t apply);
    // 0x411300 (ret 8): one step: DiffuseWithin up (apply 1), down (apply 0), DiffuseBetween up (1), down (0), all four
    // run; whether any rose.
    bool DiffuseStep(std::vector<WallDiffusionRun> *runs, float distance);
    // 0x410890 (ret 0xc): DiffuseStep up to `steps` times (unsigned), stopping at the first that changes nothing;
    // whether any did.
    bool Diffuse(std::vector<WallDiffusionRun> *runs, uint32_t steps, float distance);

    // ---- Step's maths (row LD2j) ----
    // 0x40d5a0 (ret 0x14): the point (x, y) against the first segment's line: u its unit direction, d the point less its
    // start. *t (when given) = (u . d) / Length(). t < 0: *distance (when given) = |d| signed by the cross u x d (NaN:
    // negative), *nearest (when given) the start, returns 0. t < 1: *distance = the cross (never checked for null),
    // *nearest = start + u t Length(), returns floor(2n t) (the magic-number floor) clamped to 0..2n - 1. Else (and for a
    // NaN t): *distance as for t < 0, *nearest = start + u Length(), returns 2n - 1. Reads the first segment unchecked.
    int32_t Project(float x, float y, float *distance, float *t, float *nearest);
    // 0x411500 (ret 0xc; bool in al): the packed colour of rgb painted from side `side` back to side 0: each side's words
    // 1..4 take the per-byte maximum (bytes as words, compared signed); while that raised a byte, the colour is scaled by
    // the falloff word (pmulhw of the bytes << 4; Fixed16 of 0.0625 / (1 + distance, times 1.4 on a diagonal run)), and
    // that side's words 0 and 5 set to it (saturated). Whether it moved off `side`.
    bool PaintBack(const float *rgb, float distance, int32_t side);
    // 0x411700 (ret 0xc; bool in al): the same forwards from side + 1 up to SideCount() - 1, with the colour scaled once
    // before the first side. Whether it moved past side + 1.
    bool PaintAhead(const float *rgb, float distance, int32_t side);
    // 0x40c040 (ret 0x10; bool in al): one light on the run (Step 0x40c2e0, per light record): Project the point; false
    // when the distance is not above 0. A directional light (+0x24) farther than 1e-4 weighs the facing f = (unit to the
    // nearest point) . (cos, sin of its angle) (the x87 dot 0x19d250): intensity *= ((f + 1) / 2) (1 - e) + e with
    // e = 1 - clamp(+0x34, 0, 1), and the exponent's factor (6 - 2f) / (2f + 6). The colour is the light's times
    // intensity * pow(1 / (spread + 1), max(1, 2 distance factor)), painted both ways from the projected side over
    // max(0, spread) (directional: ((2 spread / exponent - spread) max(f, 0) + spread)).
    bool Light(float intensity, float spread, const float *point, const WallLight *light);

    // ---- row LD2k (the rebuild) ----
    // 0x40c680 (ret 0xc): a new segment (the default constructor 0x40bd90: the colour blocks left as they were) with the
    // two ends and the location copied in, appended (push_back: the image's realloc insert 0x40b2c0 grows as our
    // toolchain's, cap + cap / 2 or size + 1). The partitions' getter 0x20de97 is called on the way, its answer unused.
    void AddSegment(const float *start, const float *end, const uint32_t *location);

    // ---- row LD2f ----
    // 0x4104a0 (ret 0xc): row k of the run's 4n rows (segment k / 4, unsigned and not checked; its row k & 3) blended by
    // the segment's BlendRow.
    void BlendRowAt(const float *rgb, float scale, uint32_t k);

    // ---- hook adapters ----
    void SetFlag19Hook(uint8_t value);
    void SetFlag1AHook(uint8_t value);
};
static_assert(sizeof(WallDiffusionRun) == 0x1c, "WallDiffusionRun size");
static_assert(offsetof(WallDiffusionRun, mPrev) == 0xc && offsetof(WallDiffusionRun, mAngle) == 0x14 &&
                  offsetof(WallDiffusionRun, mDiagonal) == 0x18,
              "WallDiffusionRun fields");

// The 24-byte records of the manager's two change lists (+0x48, +0x54; the layout of cDiffusionManager.h's DiffusionWall,
// which the collectors 0x40c620 / 0x40c650 append them to). Value-initialised as below by the vector instance 0x40bad0
// (library).
struct WallDiffusionChange {
    float mA[2] = {-1.0f, -1.0f};
    uint32_t mA2 = 0;
    float mB[2] = {-1.0f, -1.0f};
    uint32_t mB2 = 0;
};
static_assert(sizeof(WallDiffusionChange) == 0x18, "WallDiffusionChange");

// The 44-byte records CollectBlobs 0x40fa40 appends (cDiffusionManager.h's DiffusionBlob44, cFloorColorGrid.h's FloorBlob):
// where two runs meet a quarter turn apart.
struct WallBlob {
    uint32_t mBack[5];  // +0x00 the first run's last segment's location
    uint32_t mFront[5]; // +0x14 the second run's first segment's location
    uint8_t mOctant;    // +0x28 WallOctant of the corner's mean direction
    uint8_t mSingle;    // +0x29 either run one segment long
    uint8_t mPad2a[2];  // +0x2a not written by the original's in-place append; not read
};
static_assert(sizeof(WallBlob) == 0x2c && offsetof(WallBlob, mFront) == 0x14 && offsetof(WallBlob, mOctant) == 0x28 &&
                  offsetof(WallBlob, mSingle) == 0x29,
              "WallBlob");

// The angle tests of the passes (__cdecl). 0.09424778f is 0.03 pi.
// 0x4101f0: |b - a| < 0.03 pi (float).
bool __cdecl WallAnglesNear(float a, float b);
// 0x411430: |b - a + pi/2| < 0.03 pi (float): a a quarter turn past b.
bool __cdecl WallAnglesQuarterApart(float a, float b);
// 0x410170: the octant (0-7) of a direction: floor(fmod(pi/8 - angle, 2pi) / (pi/4)), less 2, modulo 8 (the floor by the
// magic constant with a correction for exact integers, as the original's).
uint8_t __cdecl WallOctant(float angle);

// The packed colour every LD2h writer inlines: 0xff000000 | R << 16 | G << 8 | B from rgb[0..2], each channel
// round(c * 256) (SSE, to nearest even, through the 1.5 * 2^15 magic constant 0xd9c6a0) clamped to 0..255 by sign masks
// in 32-bit integers: below 0 gives 0, above 255 gives 255. Outside the magic window (|c| >= 16384, infinities, NaN)
// the sum's bits less the magic's are clamped as they come: positive (a sum above 2^16, a negative sum above -49152, +inf,
// a NaN with the sign clear) gives 255; negative gives 0, except 0x80000000-0x800000ff (c just below -98304), where 255
// less them overflows and gives 255 (the original's, as it is).
uint32_t PackWallColor(const float *rgb);
// 0x4106a0 (__cdecl): ApplyLights' copy of the packing (its own body, the same as PackWallColor's).
uint32_t __cdecl PackLightColor(const float *rgb);

class cWallDiffusionManager : public rz::cRZRefCount {
public:
    static constexpr uint32_t kVTable = 0xdc24f4;

    cWallDiffusionManager();
    // 0x40bff0's body: the lists, the set and the runs, then the count's own.
    ~cWallDiffusionManager() override;

    // ---- hook adapters ----
    static cWallDiffusionManager *__fastcall Construct(void *memory, void *); // 0x40be50 (returns this)
    cWallDiffusionManager *DeletingDestructor(uint32_t flags);                // 0x40bff0 (slot 0, ret 4): 0x60 freed on bit 0

    // ---- the settings (cDiffusionManager::ApplySettings) ----
    // 0x411b30 (ret 4).
    void SetInsideAmbientColor(const float *color);
    // 0x411b60 (ret 4).
    void SetOutsideWallAmbient(float value);
    // The game's are folded with other classes' setters (never hooked): 0x20c6f0, 0x229f60, 0x4099f0, 0x411b70. Out of
    // line, so a test can stand them in.
    void SetPropagationSteps(int32_t steps);
    void SetInsideWallAmbient(float value);
    void SetThreshold(float value);
    void SetOutsideAmbientColor(const float *color);

    // ---- row LD2h: the simple passes ----
    // 0x40c720 (ret 8, returns out): the run's ambient colour: inside (colour times wall ambient) when the low 16 bits
    // of its FrontCellQuery are not 0, else outside. (The original first copies a static vector, 0x10da730, into out; it
    // is overwritten whole, so that is not done.)
    float *AmbientFor(float *out, WallDiffusionRun *run);
    // 0x40d010: every run's segments set to its ambient colour, and its mFlag19 cleared.
    void AmbientPass();
    // 0x411c60: every run's PaintGradient (cDiffusionManager's short pass, the debug switch 0x10da3dc / 0x10da3de).
    void GradientPass();
    // 0x411c20: every run's PaintTestColors (the debug switches 0x10da3dd / 0x10da3df).
    void TestColorPass();
    // 0x40f5a0: every segment's FillBlock from its Neighbours (the list argument mChanges for an inside run, by
    // AmbientFor's test, else mChanges2).
    void BorderPass();

    // ---- row LD2e: the entry points cDiffusionManager calls ----
    // 0x40c620 (ret 4): mChanges appended at out's end (the library insert 0x40b590, from mChanges' first element and
    // its count; mChanges as it was).
    void CollectWalls2(std::vector<DiffusionWall> *out);
    // 0x40c650 (ret 4): mChanges2 the same way.
    void CollectWalls(std::vector<DiffusionWall> *out);
    // 0x40eff0: every segment's mCurrent copied to mPrevious, then mChanges2 and mChanges emptied (the library resize
    // 0x412510 to 0: the end set back, the capacity kept).
    void Reset();
    // 0x40c800 (ret 8; nothing in eax): the colour added into all 32 words of mCurrent of every segment of each run whose
    // FrontCellQuery's low 16 bits equal level's (every run asked, an empty one too: it reads the first segment unchecked).
    // Per channel (rgb[0] into each word's byte 2, rgb[1] byte 1, rgb[2] byte 0; byte 3 kept): v = bits(c + 49152.0f) -
    // 0x47400000 (round(c * 256), to nearest even), a = (v - (v >> 8, logical)) & 0xff, then the byte becomes
    // min(a + byte, 255) (unsigned). So c = 1 adds 255, but 257/256 adds 0 and -2/256 adds 255 (the original's).
    void Mark(int32_t level, const float *rgb);
    // 0x40f960 (ret 0xc, returns out): the first segment (runs in order, then segments) of a run whose
    // FrontLocationWord2 is location[2], whose five location words are location's: out = {that level word, the run
    // index, the segment index}, else {-1, -1, -1} (0x40be30). Copied to `copy` too when it is not null. Every run's
    // FrontLocationWord2 is read (an empty run's first segment unchecked).
    WallSegmentRef *FindSegment(WallSegmentRef *out, const uint32_t *location, WallSegmentRef *copy);
    // 0x410320 (ret 8, returns out): the colour of a wall piece (`wall` two float triples, the cell by
    // worldrect::CellOfRect 0x7ef3b0): FindSegment's segment, its mPacked[9] (side 1, word 3) as {R, G, B} bytes / 255;
    // with mOutsideAmbientEnabled set and the run outside (its FrontCellQuery's low 16 bits 0) less mOutsideAmbientColor times
    // mOutsideWallAmbient. No segment: {0, 0, 0} (the static vector 0x10da730, zero and never written).
    float *GetColor(float *out, const float *wall);

    // ---- row LD2i ----
    // 0x40f0f0: each marked run (in key order, unchecked indices) Diffuse'd with mPropagationSteps and mThreshold, then
    // the marks cleared (the set's clear inlined: the library _Erase_tree 0x15abe0 and sized delete 0x824624).
    void DiffuseMarked();

    // ---- row LD2k: the rebuild (cDiffusionManager's Diffuse / DiffuseUnlit when dirty) ----
    // 0x411ca0 (ret 4): the runs of a wall section's two sides (edges 0 and 2), each side not flagged yet: from the
    // edge, back along RotOnextInvRotC (slot 25) while the angle stays near the edge's (WallAnglesNear, no cycle guard);
    // then along SymOnextC (slot 27) until back at that start, each edge flagged (SetFlag(1)) and made a segment
    // (AddSegment of its two ends, each WithGroundHeight'd, the far one first; the cell CellOfRect of {origin, dest}).
    // The run so far is closed (appended, linked mNext / mPrev to the one before it in this side's chain) where the angle
    // leaves the run's or the cell's level (the low 16 bits of the world's 0x7f2260, flag 0) changes; a run of the
    // remaining segments is appended at the end, and the side's chain is closed into a ring (the last run's mNext the
    // first, the first's mPrev the last) when it holds two runs or more. A run closed before its first segment (the
    // first edge's angle not near itself: a NaN) is appended empty.
    void BuildRuns(nTSWorld::cIWallSection *section);
    // 0x4120d0: the partitions 0x20de97's layers of kinds 3, 8, 10 and 11 (the table 0xdc25a0; slot 40, counted): each
    // layer's slot 14 called, then BuildRuns of each of its slot 15 count of sections (slot 11). Then layer 0's sections
    // for the partitions' segments (slot 55 count, re-read each time, unsigned; slot 56 fills a 6-word segment) whose
    // catalog id (slot 50, the whole word) is 0x1d and for which the layer's slot 12 answers a section. A null layer is
    // not checked before slot 14 (the original's).
    void BuildAll();
    // 0x40d850: every segment's references to the levels above and below: the location's level word + 1 (when not above
    // the world's GetMaxLevel, signed) is looked up by FindSegment when the world has no floor or ground (slot 52, mask
    // the quadrant byte, checkVisible 1) at the segment's location one level up (CellToQuadrants); the level - 1 (when not
    // below GetMinLevel) when there is none at the segment's own location. The runs' and segments' counts re-read each
    // time; empty runs skipped.
    void LinkLevels();
    // 0x412240: the runs dropped (each run's segments freed, then the end set back: the capacity kept), BuildAll, then
    // LinkLevels (a tail jump).
    void Rebuild();

    // ---- row LD2f (lane 261110i): Step and the blob pass ----
    // 0x40c2e0 (ret 4; nothing in eax): each light (count re-read), then each run (count re-read) whose level word is not
    // above the light's level (signed) and whose FrontCellQuery's low 16 bits are its mLevelId: AmbientFor (its colour
    // unused, the original's), the point the box's centre ((min + max) * 0.5, MulSs / AddSs), intensity mIntensity. When
    // the run lies below the light, from the light's level down while above the run's: the world's slot 52
    // (HasFloorOrGround, the quadrant byte, checkVisible 1) at PositionToTileQuadrant of {point, level as bits} stops it,
    // else level - 1 and intensity = (float)((1.0 - (double)mFalloff) * intensity) (in double). Then Light(intensity,
    // mSpread * mThreshold, point, light); when that paints, Walk(&mRuns, 1, 1) and the run's index into mMarkedRuns.
    // The world is the cached world database (worldsvc::CachedWorldDB, read once).
    void Step(const std::vector<WallLight> *lights);
    // 0x40edb0: every run with mFlag19 set (nonzero) whose neighbour run (mPrev, then mNext, when not negative) has a
    // different mFlag19 byte and meets it a quarter turn apart (WallAnglesQuarterApart(prev angle, this), or (this,
    // next)): the run's end rows blended towards its AmbientFor colour by BlendRowAt: at the start rows 0, 1 (0.8) and
    // 2 (0.4), and 4 (0.4) for two or more segments, else 3; at the far end rows 4n - 1, 4n - 2 (0.8), 4n - 3 (0.4), and
    // 4n - 5 or, for one segment, 4n - 4 (0.4).
    void BlobPass();
    // 0x40fa40 (ret 4; nothing in eax): `out` emptied (the capacity kept), every run's mFlag1A and mFlag1B cleared, then
    // for each run (count re-read): with a next run (mNext not negative), mFlag1B clear, not both mFlag19 set and
    // WallAnglesQuarterApart(this, next): this one's mFlag1B and next's mFlag1A set, a blob of the two appended. Then the
    // same with the previous run (mPrev): this one's mFlag1A (re-read) clear, not both set, QuarterApart(prev, this): prev's
    // mFlag1B and this one's mFlag1A set, a blob of prev and this. A blob of `from` and `to` (WallBlob): from's
    // BackLocation, to's FrontLocation, WallOctant((float)(((from angle - pi) + to angle) * 0.5), in double for the
    // halving), and whether either has exactly one segment. The original writes the 42 bytes in place when there is room
    // (its padding left as the vector's storage held it) or grows through the library 0x40aed0 from a stack record (the
    // padding the stack's); ours appends the record with its padding zero (nothing reads it).
    void CollectBlobs(std::vector<DiffusionBlob44> *out);

    // ---- row LD2l (lane 261110j): the blends cDiffusionManager's Diffuse / DiffuseUnlit call after Step ----
    // 0x40f6a0 (ret 8; nothing in eax): each run (count re-read, unsigned) with its weight, `bright` when its mFlag19
    // byte is set, else `dark`; when the weight is not 0 (a NaN is not) and BlendLightMap(weight) changed a byte, the
    // run's index into mMarkedRuns.
    void Blend(float bright, float dark);
    // 0x40f800 (ret 0xc): the same with BlendFloors(floors, weight).
    void BlendUnlit(float bright, float dark, cIDiffusionManager *floors);

    // ---- row LD2m (lane 261110k): the segments' colours into the light map's wall images ----
    // 0x40dd30 (ret 0xc; nothing in eax): the change lists emptied (mChanges2, then mChanges: the capacity kept), the
    // partitions fetched (0x20de97, once). Then each run (count re-read, unsigned), its colour {0, 0, 0} (the static
    // 0x10da730, read), or with mOutsideAmbientEnabled set and its FrontCellQuery's low 16 bits 0, mOutsideAmbientColor times
    // mOutsideWallAmbient (MulSs, made once before the runs); its list mChanges when a second FrontCellQuery's low 16 bits
    // are not 0, else mChanges2. Each segment (count re-read, unsigned): with changedOnly, skipped when mPrevious equals
    // mCurrent (all 0x80 bytes), else its CellToSegment appended to the run's list. Then skipped when its CellToSegment is
    // in a local set (cLightingManager::SegmentLess order, nodes from the allocator service; not mMarkedRuns), else added,
    // and skipped when the light map's GetWallRect (slot 21) answers false. On a new atlas (the cache -1 at first):
    // GetImage(0, atlas) (not checked), its level 0's LevelData, LevelPitch / 4 (unsigned) and LevelHeight (in that order),
    // rows from the last up (row r at data + (height - 1 - r) * pitch words). With w = right - left, h = bottom - top:
    // - w 4, h 8: the 4 x 8 block (rect column x, row y from mCurrent word (x - left) * 8 + 7 - (y - top)), each byte b of
    //   it less the run colour's packed byte (PackWallColor), shifted left 4 (16 bits), times the low word of
    //   SetFromFloat(scale * 0.0625) (pmulhw), saturated to 0..255, alpha 0xff. Rows top..bottom and columns left..right
    //   compared unsigned.
    // - h 3w: BilinearFill of h rows.
    // - else h a multiple of w (signed idiv): BilinearFill of min(h, 3w) rows (v's step 6 / (3w - 1)), then rows 3w..h
    //   each the row before it, each byte b (b << 4) times the low word of SetFromFloat((float)(0.0625 / (mThreshold *
    //   0.4 + 1))) (doubles), saturated, alpha 0xff, and masked: 0 for a run whose first FrontCellQuery's low 16 bits are
    //   not 0 and whose segment's partitions catalog id (slot 50) is 3; for changedOnly that segment is skipped before.
    // - else nothing.
    // The fill: u from 0.5 by 2 / (w - 1) along a row, v from 6.5 down by 6 / (rows - 1) (6 / (h - 1) for h 3w) per row;
    // each pixel the bilinear of mCurrent's words around (u, v) (column u * 8, row v), less the run colour, times scale,
    // packed (PackLightColor). The bytes 0..255 / 255 per channel (byte 2 red, 1 green, 0 blue).
    void ApplyLights(cILightMapManager *lights, float scale, uint8_t changedOnly);
    // 0x40ed70 (ret 8): ApplyLights(lights, scale, 0).
    void ApplyAllLights(cILightMapManager *lights, float scale);
    // 0x40ed90 (ret 8): ApplyLights(lights, scale, 1).
    void ApplyChangedLights(cILightMapManager *lights, float scale);

    std::vector<WallDiffusionRun> mRuns;                     // +0x08
    float mThreshold = 0.137f;                                   // +0x14 wallDiffusionThreshold
    float mInsideWallAmbient = 1.0f;                             // +0x18
    float mOutsideWallAmbient = 1.0f;                            // +0x1c
    float mInsideAmbientColor[3] = {0.07f, 0.08f, 0.1f};         // +0x20
    float mOutsideAmbientColor[3] = {0.07f, 0.08f, 0.1f};        // +0x2c
    uint8_t mOutsideAmbientEnabled = 1;                          // +0x38 the only readers (GetColor and the pixel path) take mOutsideAmbientColor off outside runs when it is set (rows LD2e / LD2f)
    int32_t mPropagationSteps = 10;                              // +0x3c wallPropagationSteps
    std::set<int32_t> mMarkedRuns;                               // +0x40 run indices DiffuseMarked 0x40f0f0 visits (signed keys: 0x1d5d80)
    std::vector<WallDiffusionChange> mChanges;                   // +0x48 appended to a list by 0x40c620
    std::vector<WallDiffusionChange> mChanges2;                  // +0x54 by 0x40c650; both emptied by Reset 0x40eff0
};
static_assert(sizeof(cWallDiffusionManager) == 0x60, "cWallDiffusionManager size");
static_assert(offsetof(cWallDiffusionManager, mRuns) == 8 && offsetof(cWallDiffusionManager, mThreshold) == 0x14 &&
                  offsetof(cWallDiffusionManager, mInsideAmbientColor) == 0x20 &&
                  offsetof(cWallDiffusionManager, mOutsideAmbientEnabled) == 0x38 &&
                  offsetof(cWallDiffusionManager, mPropagationSteps) == 0x3c,
              "cWallDiffusionManager settings");
static_assert(offsetof(cWallDiffusionManager, mMarkedRuns) == 0x40 && offsetof(cWallDiffusionManager, mChanges) == 0x48 &&
                  offsetof(cWallDiffusionManager, mChanges2) == 0x54,
              "cWallDiffusionManager lists");

} // namespace nTSSG
