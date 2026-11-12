// The lot imposter's wall-slice record (lane 261111t): see sg/cImposterSliceRecord.h.
#include "sg/cImposterSliceRecord.h"

#include "game/vtable.h"
#include "misc/X87.h"
#include "world/WorldHeights.h"

#include <bit>
#include <cmath>
#include <new>
#include <xmmintrin.h>

namespace nTSSG {

namespace {

// The wall piece's and its vertices' slots the record calls (the world's wall graph classes).
enum WallSlot : int {
    kSetMarked = 5,    // (bool): the piece is taken by a run
    kPrevious = 21,    // the neighbour at its end
    kNext = 23,        // the neighbour at its start
    kAngle = 30,       // float (st0)
    kIsSkipped = 34,   // bool: marked already, or not a piece a run takes
    kStartVertex = 35, // cImposterWallVertex*
    kEndVertex = 36,   // cImposterWallVertex*
};
enum VertexSlot : int {
    kPosition = 5, // const WorldPoint*: x, y, level
};

bool IsSkipped(cImposterWall *wall) { return VirtualCall<bool(__thiscall *)(void *), kIsSkipped>(wall); }
float Angle(cImposterWall *wall) { return VirtualCall<float(__thiscall *)(void *), kAngle>(wall); }
void SetMarked(cImposterWall *wall, bool marked) { VirtualCall<void(__thiscall *)(void *, bool), kSetMarked>(wall, marked); }
cImposterWall *Next(cImposterWall *wall) { return VirtualCall<cImposterWall *(__thiscall *)(void *), kNext>(wall); }
cImposterWall *Previous(cImposterWall *wall) { return VirtualCall<cImposterWall *(__thiscall *)(void *), kPrevious>(wall); }
cImposterWallVertex *StartVertex(cImposterWall *wall)
{
    return VirtualCall<cImposterWallVertex *(__thiscall *)(void *), kStartVertex>(wall);
}
cImposterWallVertex *EndVertex(cImposterWall *wall)
{
    return VirtualCall<cImposterWallVertex *(__thiscall *)(void *), kEndVertex>(wall);
}
const nTSWorld::WorldPoint *Position(cImposterWallVertex *vertex)
{
    return VirtualCall<const nTSWorld::WorldPoint *(__thiscall *)(void *), kPosition>(vertex);
}

// The nearest whole number above or at q, by the 1.5 * 2^23 trick: q + 0.5 rounded to an integer (ties to even), less
// one when that one less is q itself.
uint32_t RoundUp(float q)
{
    const float shifted = (q + 0.5f) + 12582912.0f;
    const uint32_t r = std::bit_cast<uint32_t>(shifted) - 0x4b400000u;
    const uint32_t less = r - 1;
    return static_cast<float>(static_cast<int32_t>(less)) == q ? less : r;
}

} // namespace

cImposterWallEnds *cImposterWallEnds::Init(cImposterWall *wall)
{
    start = {-1.0f, -1.0f, 0};
    end = {-1.0f, -1.0f, 0};
    if (wall) {
        cImposterWallVertex *const s = StartVertex(wall);
        cImposterWallVertex *const e = EndVertex(wall);
        if (s && e) {
            start = *Position(s);
            end = *Position(e);
        }
    }
    return this;
}

cImposterSliceRecord::cImposterSliceRecord()
    : mTopStart{}, mTopEnd{}, mBottomStart{}, mBottomEnd{}, mColumns(0), mAngle(0.0f), mTexture(-1), mU0(0.0f), mV0(0.0f),
      mU1(0.0f), mV1(0.0f), mLevel(0)
{
}

cImposterSliceRecord *__fastcall ConstructImposterSliceRecord(cImposterSliceRecord *self, void *)
{
    return new (self) cImposterSliceRecord();
}

float cImposterSliceRecord::MinBottom() const
{
    return mBottomEnd.height < mBottomStart.height ? mBottomEnd.height : mBottomStart.height;
}

float cImposterSliceRecord::MaxTop() const { return mTopEnd.height > mTopStart.height ? mTopEnd.height : mTopStart.height; }

double cImposterSliceRecord::Height() const
{
    return x87::Rounded(static_cast<double>(MaxTop()) - static_cast<double>(MinBottom()));
}

int32_t cImposterSliceRecord::RowCount() const { return static_cast<int32_t>(RoundUp(static_cast<float>(Height()))); }

bool cImposterSliceRecord::SameAngle(float angle) const
{
    return static_cast<double>(std::fabs(mAngle - angle)) < 0.0001;
}

float *cImposterSliceRecord::Normal(float *out) const
{
    out[0] = static_cast<float>(std::sin(static_cast<double>(mAngle)));
    const float c = static_cast<float>(std::cos(static_cast<double>(mAngle)));
    out[2] = 0.0f;
    out[1] = -c;
    return out;
}

float cImposterSliceRecord::Length() const
{
    const float dy = mBottomEnd.y - mBottomStart.y;
    const float dx = mBottomEnd.x - mBottomStart.x;
    // dy^2 first: with two NaNs the sum is the first operand's (the compiler may swap a plain `+`).
    const float squared = _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(dy * dy), _mm_set_ss(dx * dx)));
    return static_cast<float>(std::sqrt(static_cast<double>(squared)));
}

bool cImposterSliceRecord::Begin(cImposterWall *wall, void *walls)
{
    if (!wall || !walls || IsSkipped(wall)) return false;
    const nTSWorld::WorldPoint a = *Position(StartVertex(wall));
    const nTSWorld::WorldPoint b = *Position(EndVertex(wall));
    mLevel = a.level;
    cImposterWallEnds ends;
    ends.Init(wall);
    const float groundB = static_cast<float>(worldheights::GroundHeightAt(&b));
    const float groundA = static_cast<float>(worldheights::GroundHeightAt(&a));
    const float bottom = groundB < groundA ? groundB : groundA;
    const float endStart = worldheights::SegmentEndHeight(&ends.start.x, false);
    const float endEnd = worldheights::SegmentEndHeight(&ends.start.x, true);
    const float top = endStart > endEnd ? endStart : endEnd;
    mBottomStart.x = a.x;
    mBottomStart.y = a.y;
    mTopStart.x = a.x;
    mTopStart.y = a.y;
    mBottomStart.height = bottom;
    mTopStart.height = top;
    mBottomEnd.x = b.x;
    mBottomEnd.y = b.y;
    mBottomEnd.height = bottom;
    mTopEnd.x = b.x;
    mTopEnd.y = b.y;
    mTopEnd.height = top;
    mAngle = Angle(wall);
    mColumns = 1;
    SetMarked(wall, true);
    return true;
}

bool cImposterSliceRecord::Extend(int32_t side, cImposterWall *wall, void *walls)
{
    if (!wall || !walls || IsSkipped(wall)) return false;
    if (!SameAngle(Angle(wall))) return false;
    const nTSWorld::WorldPoint p = *Position(side ? EndVertex(wall) : StartVertex(wall));
    cImposterWallEnds ends;
    ends.Init(wall);
    const float ground = static_cast<float>(worldheights::GroundHeightAt(&p));
    const float minBottom = MinBottom();
    const float bottom = ground < minBottom ? ground : minBottom;
    const float end = worldheights::SegmentEndHeight(&ends.start.x, side == 0);
    const float maxTop = MaxTop();
    const float top = end > maxTop ? end : maxTop;
    if (side == 0) {
        mBottomStart.x = p.x;
        mBottomStart.y = p.y;
        mBottomStart.height = bottom;
        mTopStart.x = p.x;
        mTopStart.y = p.y;
        mTopStart.height = top;
        mBottomEnd.height = bottom;
        mTopEnd.height = top;
    } else {
        mBottomEnd.x = p.x;
        mBottomEnd.y = p.y;
        mBottomEnd.height = bottom;
        mTopEnd.x = p.x;
        mTopEnd.y = p.y;
        mTopEnd.height = top;
        mBottomStart.height = bottom;
        mTopStart.height = top;
    }
    ++mColumns;
    SetMarked(wall, true);
    return true;
}

bool cImposterSliceRecord::Build(cImposterWall *wall, void *walls, int32_t maxColumns)
{
    if (mColumns != 0 || !wall || !walls || maxColumns == 0 || IsSkipped(wall) || !Begin(wall, walls)) return false;
    for (cImposterWall *w = Next(wall); w && (maxColumns < 0 || mColumns < maxColumns) && Extend(0, w, walls); w = Next(w)) {
    }
    for (cImposterWall *w = Previous(wall); w && (maxColumns < 0 || mColumns < maxColumns) && Extend(1, w, walls);
         w = Previous(w)) {
    }
    return true;
}

void cImposterSliceRecord::Split(float rowHeight, std::vector<cImposterSliceRecord> *slices) const
{
    const float rows = static_cast<float>(x87::Rounded(Height() / static_cast<double>(rowHeight)));
    const uint32_t count = RoundUp(rows);
    slices->resize(count, *this);
    if (count == 1 || static_cast<int32_t>(count) <= 0) return;
    for (int32_t i = 0; i < static_cast<int32_t>(count); ++i) {
        const float bottom = static_cast<float>(
            x87::Rounded(static_cast<double>(MinBottom()) + static_cast<double>(static_cast<float>(i) * rowHeight)));
        const float maxTop = MaxTop();
        const float rowTop = bottom + rowHeight;
        const float top = maxTop < rowTop ? maxTop : rowTop;
        cImposterSliceRecord &slice = (*slices)[i];
        slice.mTopStart.height = top;
        slice.mTopEnd.height = top;
        slice.mBottomStart.height = bottom;
        slice.mBottomEnd.height = bottom;
    }
}

} // namespace nTSSG
