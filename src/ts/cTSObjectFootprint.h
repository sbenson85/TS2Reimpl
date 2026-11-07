#pragma once
#include <cstdint>

class cTSObject;

// The footprint of a cTSObject (src/ts/cTSObjectFootprint.cpp): a cache of 16-byte rectangles (x1, y1, x2, y2 floats) snapped
// inwards to the 1/8 grid, with the extent of what it holds. The object keeps one at +0x1e4 (the base footprint, at position 0 and
// notch 0, built on first use) and one at +0x200 (the footprint at the position and facing the last vf65 call asked for).
struct FootprintCache {
    float *mBegin;
    float *mEnd;
    float *mCapacity;
    float mMinX;
    float mMinY;
    float mMaxX;
    float mMaxY;
};
static_assert(sizeof(FootprintCache) == 0x1c, "the cache is the vector (12 bytes) and four floats");

// 0x7e86e0 (cdecl; tentative name): the object's footprint size: 0.2 for kind 2 (a person), otherwise the attribute table's entry
// 0x43 (zero: 1.0) times 1/16. Returns the float in st0.
float __cdecl FootprintSize(cTSObject *object);

// 0x7dd070 (cdecl; tentative name): (10 - *facing) % 8 with C's sign (-7..7), the notch a facing word gives.
int32_t __cdecl FacingPointerToNotch(const int32_t *facing);

// 0x7eaf50 (cdecl; tentative name): fills `cache` with the object's footprint rectangles at `position` (two floats) turned by
// `*notch`: the rectangles slot 183 reports for the tile the object stands on (not for a kind 2 object, nor when the sign bit of
// the low byte of attribute 8 is set), else one square of the footprint size around the position shrunk by the margins of
// attribute 0x4d (and, for kind 2 with the helper interface 0x5a69d3d5, its entry 0x49).
void __cdecl BuildFootprint(cTSObject *object, const float *position, const uint32_t *notch, FootprintCache *cache);
