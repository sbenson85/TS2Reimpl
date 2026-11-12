#pragma once
// The heights of nTSWorld's helper module (lane RZ): the ground under a point, interpolated from the world DB's corner
// elevations, and the queries next to it (0x7e4000-0x7e5b50). Every function reaches the world DB through the cached
// face (src/world/WorldService.h). Built for speed (/O2).
#include "world/cWorldDB.h"

#include <cstdint>

namespace worldheights {

// 0x7e4680 (__cdecl, the point by pointer, the result in st0). The ground height under {x, y, level}:
// - below the lowest level: inside the world (IsInWorld), the height one level up less the wall height, else 0;
// - above the highest level: inside the world, the height one level down plus the wall height, else 0;
// - on a tile outside the lot (IsInside fails): the tile clamped to [min, max + 1] on both axes, its corner elevation
//   when IsInsideCorners passes, else 0;
// - else the corner elevations of the tile, interpolated bilinearly, or linearly along one axis when the point is within
//   31 ulps of a whole coordinate on the other (both: the corner's elevation as it is).
// The recursive and the one-axis results are x87 sums the original returns unrounded (a double here, rounded at the
// thread's precision by x87::Rounded); the others are floats.
double __cdecl GroundHeightAt(const nTSWorld::WorldPoint *point);

// 0x7ddd60 / 0x7ddd90: out = (in.x, in.y, L + 1) and (in.x, in.y, L), L the level whose ground is the highest at or under
// in.z (0x7e28d0, WU-2's and an original here: from the top level down, the first whose ground height < z + 0.01).
int32_t *__cdecl WithLevelAboveGround(int32_t *out, const float *in);
int32_t *__cdecl WithLevelOfGround(int32_t *out, const float *in);
// 0x7ddc50: the position's tile quadrant is inside the lot and has a floor or ground there (cIWorldDB HasFloorOrGround
// with the quadrant as the mask and checkVisible passed on); false without a world DB.
bool __cdecl HasFloorOrGroundAt(const float *position, bool checkVisible);

// ---- the queries around the heights (src/world/WorldHeightQueries.cpp, 0x7e4250-0x7e5b50) ----
// Callees in WU-2's predicate range (0x7df000-0x7e3800) are still the originals, called through OriginalFunction. The
// object arguments are cTSObject-like faces reached only through their slots (named by offset where unknown).

// 0x7e4250: a room's area in tiles: the partitions' slot 76 fills a vector of 16-byte tile records for (room, level); each
// record's quadrant byte (+0xc) counts 4 when it is 0xf, else one per quadrant bit among 1, 2, 4, 8; the sum / 4. The
// vector's block is freed through the allocator service.
int32_t __cdecl RoomAreaInTiles(int32_t room, int32_t level);
// 0x7e43c0: more than half of the edges of a room's outline (the partitions' slot 40 (7) graph, walked from the room's
// first quadrant centre as 0x7e2de0 snaps it) carry the wall product flag 11 (ProductFlag11 of the edge's partition id).
bool __cdecl RoomMostlyFlagged(int32_t room);
// 0x7e4c00: the ground height under a point, raised by the pool's surface (the pools' slot 17) on a pool deck, or by 0.75
// on a deck foundation (0x7e24f0 / 0x7e2520 on the tile quadrant's GetDeckFoundation).
float __cdecl SurfaceHeightAt(const float *point);
// 0x7e4ce0 (the result in st0): the highest surface at (x, y): a roof (the roofs' slot 24, *onRoof set), else the ground of
// the highest level above 0 with a floor there (0x7e06e0; *level set; unrounded), else level 0's ground: 0.25 lower where
// 0x7e0430 holds (*sunken set), raised to the terrain's height (slot 11) where 0x7e2400 holds; *level 0 then.
double __cdecl HighestSurfaceAt(float x, float y, uint32_t roofArg, bool *onRoof, int32_t *level, bool *sunken);
// 0x7e4f40: an object found by 0x7e3730(key) asked its slot 178 of its slot 42 (out, f); 0 without the object.
uint32_t __cdecl ObjectQueryAt(uint32_t key, float f);
// 0x7e4f90: the object is not a floor-standing one (by its kind, slot 98, its attribute 0x3b, three WU-2 tests, its
// slot 159 count and its catalog entry, and the app's slot 24 manager's slots 43, 49, 51 on its slot 61 face).
bool __cdecl IsRaisedObject(void *object);
// 0x7e5170: some object on the point's tile quadrant answers slot 112 for `other`, or the quadrant's floor pattern has a
// catalog entry with a non-empty name, which is copied into the std::string at other + 4.
bool __cdecl TileClaimsObject(const float *point, void *other);
// 0x7e5330: 0x7dfdc0(a) or 0x7e0600(a) or 0x7e00f0(a, b).
bool __cdecl AnyStructureAt(const void *a, uint32_t b);
// 0x7e5370 (the result in st0, a float): the height an object stands at on a point: the ground elevation (maxed with the
// terrain) for terrain-placed objects on level 0, else the elevation of the first quadrant its slot 153 flags allow
// (0x7e29a0) or the ground height, plus 2.75 on a pool deck or 0.75 on a raised surface, plus its slot 45 offset.
float __cdecl ObjectHeightAt(void *object, const float *point, const uint32_t *context);
// 0x7e55c0: out = (point.x, point.y, ObjectHeightAt(object, point, context)).
float *__cdecl ObjectPositionAt(float *out, void *object, const float *point, const uint32_t *context);
// 0x7e5600: a lot tile has a structure (0x7dfc00, 0x7dfa70 or 0x7e0920 on its whole tile) on some level.
bool __cdecl TileHasStructure(const nTSWorld::WorldTile *tile);
// 0x7e56e0: some object on the tile has bit 0 of its attribute 0x62.
bool __cdecl TileHasFlaggedObject(const nTSWorld::WorldTile *tile);
// 0x7e5810: the six partition bits of a tile (the partitions' slot 43 per bit 1-32), rotated by 0-3 quarter turns (the
// four quadrant bits turn, the two diagonal bits swap on odd turns); 0 for other rotations.
uint32_t __cdecl PartitionMaskRotated(const nTSWorld::WorldTile *tile, uint32_t rotation);
// 0x7e59c0: the height at one end of a segment (first: its start, else its end): on a roof, the roof's less 0.25; else by
// the segment's partition id: the ground plus its partition value, the ground plus 0.75 (id 0x12d), or the next level's
// ground.
float __cdecl SegmentEndHeight(const float *segment, bool first);
// 0x7e5b40: 0x7e37e0(key, 9).
int32_t *__cdecl Lookup9(uint32_t key);

} // namespace worldheights
