#pragma once
// The lot imposter's wall-slice record (lane 261111t, scenegraph row LT2a2a): one straight run of wall pieces as a
// textured quad, its top and bottom edges at both ends, built by walking a wall's neighbours that share its angle and
// split into slices of at most one tile row each. The lot imposter manager (sg/cLotImposterManager.h) keeps them in
// +0x140 (0x365990, 0x365d40) and places their tiles on its texture atlases (PlaceSlice 0x3632b0).
// The functions sit in LT2a's file (0x3f6610-0x3f6e60); the record has no vtable and its users build it inline (the
// constructor 0x3f6610 is called by 0x365990 / 0x365d40), so every member is hooked at its RVA. RVAs 1.18.0.187; the
// names are ours.
#include "world/cWorldDB.h" // nTSWorld::WorldPoint

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nTSSG {

// A wall piece of the world's wall graph, as the record walks it (slots by number; the class is the world's).
struct cImposterWallVertex; // slot 5: its position (x, y, level)
struct cImposterWall;       // slots 5 SetMarked(bool), 21 Previous, 23 Next, 30 Angle (st0), 34 IsSkipped,
                            // 35 StartVertex, 36 EndVertex

// A wall piece's two ends ({x, y, level} each): what WorldUtils::SegmentEndHeight (0x7e59c0) takes.
struct cImposterWallEnds {
    nTSWorld::WorldPoint start; // +0x00
    nTSWorld::WorldPoint end;   // +0x0c

    // 0x3f66a0 (ret 4, returns this): both ends (-1, -1, level 0); with a wall whose two vertices are both set, their
    // positions.
    __declspec(noinline) cImposterWallEnds *Init(cImposterWall *wall);
};
static_assert(sizeof(cImposterWallEnds) == 0x18);

struct cImposterSliceRecord {
    struct Edge {
        float x, y, height;
    };
    Edge mTopStart;      // +0x00
    Edge mTopEnd;        // +0x0c
    Edge mBottomStart;   // +0x18
    Edge mBottomEnd;     // +0x24
    int32_t mColumns;    // +0x30 the wall pieces in the run (one tile column each)
    float mAngle;        // +0x34 the run's angle (the first piece's slot 30)
    int32_t mTexture;    // +0x38 the atlas / texture index, -1 while unplaced
    float mU0, mV0;      // +0x3c the inner rectangle in the texture, 0..1
    float mU1, mV1;      // +0x44
    int32_t mLevel;      // +0x4c the first piece's start vertex level

    // 0x3f6610: every field 0, the texture -1.
    cImposterSliceRecord();

    // 0x3f69c0: the lower of the two bottom heights (st0).
    __declspec(noinline) float MinBottom() const;
    // 0x3f6b10: the higher of the two top heights (st0).
    __declspec(noinline) float MaxTop() const;
    // 0x3f6a80: MaxTop - MinBottom, unrounded (x87, st0).
    __declspec(noinline) double Height() const;
    // 0x3f6a30: the tile rows the run spans: Height rounded up to a whole number (the 1.5 * 2^23 rounding trick).
    __declspec(noinline) int32_t RowCount() const;
    // 0x3f6e30 (ret 4): the run's angle within 0.0001 of `angle`.
    __declspec(noinline) bool SameAngle(float angle) const;
    // 0x3f69e0 (ret 4, returns out): (sin angle, -cos angle, 0).
    __declspec(noinline) float *Normal(float *out) const;
    // 0x3f6aa0: the run's length in the plane (its bottom edge), st0.
    __declspec(noinline) float Length() const;
    // 0x3f6b30 (ret 8): starts the run at `wall` (not skipped): both ends, the bottom at the lower ground height, the
    // top at the higher end height, the angle; the wall marked.
    __declspec(noinline) bool Begin(cImposterWall *wall, void *walls);
    // 0x3f6800 (ret 0xc): `wall` joins the run at its start (side 0: its start vertex) or end (side 1: its end vertex)
    // when it is not skipped and has the run's angle; the bottom and top widen, the wall is marked.
    __declspec(noinline) bool Extend(int32_t side, cImposterWall *wall, void *walls);
    // 0x3f6730 (ret 0xc): an empty record becomes the run through `wall`: Begin, then the Next pieces at the start and
    // the Previous pieces at the end while they join and the run is under `maxColumns` (negative: no limit).
    __declspec(noinline) bool Build(cImposterWall *wall, void *walls, int32_t maxColumns);
    // 0x3f6d00 (ret 8): `slices` becomes copies of this record, one per `rowHeight` of the height (rounded as
    // RowCount); with two or more, each gets its own bottom and top. Returns nothing (eax is left over; callers
    // ignore it).
    __declspec(noinline) void Split(float rowHeight, std::vector<cImposterSliceRecord> *slices) const;
};
static_assert(sizeof(cImposterSliceRecord) == 0x50);
static_assert(offsetof(cImposterSliceRecord, mBottomStart) == 0x18 && offsetof(cImposterSliceRecord, mColumns) == 0x30);
static_assert(offsetof(cImposterSliceRecord, mTexture) == 0x38 && offsetof(cImposterSliceRecord, mU0) == 0x3c);
static_assert(offsetof(cImposterSliceRecord, mLevel) == 0x4c);

// The constructor's hook (0x3f6610, thiscall: placement new on the caller's memory, returns it).
cImposterSliceRecord *__fastcall ConstructImposterSliceRecord(cImposterSliceRecord *self, void *edx);

} // namespace nTSSG
