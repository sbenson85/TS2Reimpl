// nTSSG::cWallDiffusionManager (lane 261110c, scenegraph LD2d; src/sg/cWallDiffusionManager.h): the shape, the
// constructor and destructors, the settings, the run's and segment's accessors, the Fixed16 helpers and the angle tests.
// Row LD2h (lane 261110d): packed colours, neighbours, the simple passes. Row LD2i (lane 261110e): the propagation under
// DiffuseMarked 0x40f0f0 (the MMX word spread a byte at a time, the segment's three spreads, the run's recursive walks).
#include "sg/cWallDiffusionManager.h"

#include "game/original.h"
#include "game/vtable.h"
#include "gz/cIGZUnknown.h"
#include "rz/GameAllocator.h"
#include "sg/cDiffusionManager.h"
#include "sg/cImageData.h"
#include "sg/cLightMapManager.h"
#include "sg/cLightingManager.h"
#include "world/WorldTileMath.h"
#include "world/WorldService.h"
#include "world/WorldTileRects.h"
#include "world/cWallEdge.h"
#include "world/cWallSection.h"
#include "world/cWallVertex.h"
#include "world/cWorldDB.h"

#include <cmath>
#include <cstring>
#include <new>
#include <set>
#include <emmintrin.h>
#include <xmmintrin.h>

namespace nTSSG {

namespace {

// The float constants the originals read (as floats, or as the doubles of floats).
constexpr float kAngleTolerance = 0x1.8209f6p-4f; // 0.03 pi
constexpr float kHalfPi = 0x1.921fb6p+0f;
constexpr float kPiOver8 = 0x1.921fb6p-2f;
constexpr float kPiOver4 = 0x1.921fb6p-1f;
constexpr double kTwoPi = 0x1.921fb6p+2;           // the double of 2pi's float
constexpr float kFixedOne = 65536.0f;
constexpr float kRoundMagic = 12582912.0f;     // 1.5 * 2^23: adding it rounds to an integer in the low mantissa bits
constexpr uint32_t kRoundMagicBits = 0x4b400000;

// The integer the magic sum holds (its bits less the magic's).
int32_t MagicToInt(float sum)
{
    uint32_t bits;
    std::memcpy(&bits, &sum, sizeof(bits));
    return static_cast<int32_t>(bits - kRoundMagicBits);
}

// The diagonal ranges SetAngle tests: the doubles of the float bounds (0xdc2558-0xdc2598).
constexpr double kEighth1 = 0x1.921fb6p-2;  // pi/8
constexpr double kEighth3 = 0x1.2d97c88p+0; // 3pi/8
constexpr double kEighth5 = 0x1.f6a7a38p+0; // 5pi/8
constexpr double kEighth7 = 0x1.5fdbbf4p+1; // 7pi/8

// The packed colours' magic (0xd9c6a0, 1.5 * 2^15): adding it leaves round(c * 256) in the low mantissa bits.
constexpr float kColorMagic = 49152.0f;
constexpr uint32_t kColorMagicBits = 0x47400000;

// mulss / addss a, b with the operands in the original's order: a NaN pair gives the first operand's NaN (quieted). The
// compiler may swap the operands of a plain * or +, and of the intrinsics too (lane 261110f found Light's
// power * intensity compiled as mulss intensity, power), so a NaN a is returned (quieted) before the operation.
float QuietSs(float a)
{
    uint32_t bits;
    std::memcpy(&bits, &a, sizeof(bits));
    bits |= 0x400000u;
    std::memcpy(&a, &bits, sizeof(bits));
    return a;
}
float MulSs(float a, float b) { return a != a ? QuietSs(a) : _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(a), _mm_set_ss(b))); }
float AddSs(float a, float b) { return a != a ? QuietSs(a) : _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(a), _mm_set_ss(b))); }
float SubSs(float a, float b) { return a != a ? QuietSs(a) : _mm_cvtss_f32(_mm_sub_ss(_mm_set_ss(a), _mm_set_ss(b))); }
float DivSs(float a, float b) { return _mm_cvtss_f32(_mm_div_ss(_mm_set_ss(a), _mm_set_ss(b))); }
// mulsd / subsd likewise (a NaN first operand kept, quieted), and the conversions cvtps2pd / cvtpd2ps.
double QuietSd(double a)
{
    uint64_t bits;
    std::memcpy(&bits, &a, sizeof(bits));
    bits |= 0x0008000000000000ull;
    std::memcpy(&a, &bits, sizeof(bits));
    return a;
}
double MulSd(double a, double b) { return a != a ? QuietSd(a) : _mm_cvtsd_f64(_mm_mul_sd(_mm_set_sd(a), _mm_set_sd(b))); }
double SubSd(double a, double b) { return a != a ? QuietSd(a) : _mm_cvtsd_f64(_mm_sub_sd(_mm_set_sd(a), _mm_set_sd(b))); }
double WidenSs(float a) { return _mm_cvtsd_f64(_mm_cvtss_sd(_mm_setzero_pd(), _mm_set_ss(a))); }
float NarrowSd(double a) { return _mm_cvtss_f32(_mm_cvtsd_ss(_mm_setzero_ps(), _mm_set_sd(a))); }
// The sign flip (xorps with the sign mask), kept apart so -(a / b) is never folded into (-a) / b or back.
float NegSs(float a) { return _mm_cvtss_f32(_mm_xor_ps(_mm_set_ss(a), _mm_set_ss(-0.0f))); }

// One channel: the rounded value clamped to 0..255 by two sign masks, in 32-bit integers.
uint32_t PackChannel(float c)
{
    const float sum = c + kColorMagic;
    uint32_t bits;
    std::memcpy(&bits, &sum, sizeof(bits));
    const uint32_t v = bits - kColorMagicBits;
    const uint32_t negative = static_cast<uint32_t>(static_cast<int32_t>(v) >> 31);
    const uint32_t over = static_cast<uint32_t>(static_cast<int32_t>(0xffu - v) >> 31);
    return ((~negative & v) | over) & 0xffu;
}

// Element k of a vector by its first-element pointer, as the originals index (no check, no use of the end).
WallDiffusionSegment *SegmentAt(WallDiffusionRun &run, int32_t k)
{
    return reinterpret_cast<WallDiffusionSegment *>(reinterpret_cast<uintptr_t>(run.mSegments.data()) +
                                                    static_cast<uint32_t>(k) * sizeof(WallDiffusionSegment));
}
WallDiffusionRun *RunAt(std::vector<WallDiffusionRun> *runs, int32_t k)
{
    return reinterpret_cast<WallDiffusionRun *>(reinterpret_cast<uintptr_t>(runs->data()) +
                                                static_cast<uint32_t>(k) * sizeof(WallDiffusionRun));
}
// The segment a valid reference names.
WallDiffusionSegment *Referenced(std::vector<WallDiffusionRun> *runs, const WallSegmentRef *ref)
{
    return SegmentAt(*RunAt(runs, ref->mRun), ref->mSegment);
}

// The propagation's spread of one word (row LD2i; the MMX of 0x411380 / 0x410d50 / 0x411160 a byte at a time): each byte
// b of `from` as (b << 4) times the falloff word, high half kept (pmulhw, signed), against the byte under it in `to`
// (pcmpgtw: the greater kept), saturated to 0..255 (packuswb). Whether any byte's scaled value was above.
bool SpreadWord(int16_t falloff, uint32_t from, uint32_t &to)
{
    uint32_t out = 0;
    bool rose = false;
    for (uint32_t shift = 0; shift < 32; shift += 8) {
        const int32_t below = static_cast<int32_t>((to >> shift) & 0xffu);
        const int32_t scaled = (static_cast<int32_t>(((from >> shift) & 0xffu) << 4) * falloff) >> 16;
        int32_t kept = below;
        if (scaled > below) {
            kept = scaled;
            rose = true;
        }
        if (kept > 255) kept = 255; // never below 0: `below` is a byte
        out |= static_cast<uint32_t>(kept) << shift;
    }
    to = out;
    return rose;
}

// The falloff's low word, as the MMX multiplies by it (punpcklwd of the Fixed16's low half).
int16_t FalloffWord(bool scaled, float distance)
{
    Fixed16 f;
    f.SetFalloff(scaled, distance);
    return static_cast<int16_t>(f.mValue & 0xffff);
}

// SpreadWithin's eight neighbours (0xdc2508): {dh, da}, even entries straight, odd ones diagonal.
constexpr int32_t kWithin[8][2] = {{0, -1}, {-1, -1}, {-1, 0}, {-1, 1}, {0, 1}, {1, 1}, {1, 0}, {1, -1}};

// The references the propagation follows from segment i.
WallSegmentRef *RefAt(WallDiffusionRun &run, uint32_t i, uint8_t up) { return up ? run.AboveAt(i) : run.BelowAt(i); }

} // namespace

uint32_t PackWallColor(const float *rgb)
{
    return 0xff000000u | PackChannel(rgb[0]) << 16 | PackChannel(rgb[1]) << 8 | PackChannel(rgb[2]);
}

// ---- the segment reference --------------------------------------------------------------------------------------------

bool WallSegmentRef::IsValid() const { return mRun >= 0 && mSegment >= 0; }

// ---- Fixed16 ----------------------------------------------------------------------------------------------------------

Fixed16 *Fixed16::SetFromFloat(float f)
{
    mValue = MagicToInt(f * kFixedOne + kRoundMagic);
    return this;
}

Fixed16 *Fixed16::SetFalloff(bool scaled, float f)
{
    double d = f;
    if (scaled) d *= 1.4;
    d += 1.0;
    const float falloff = static_cast<float>(0.0625 / d);
    mValue = MagicToInt(falloff * kFixedOne + kRoundMagic);
    return this;
}

// ---- the angle tests --------------------------------------------------------------------------------------------------

bool __cdecl WallAnglesNear(float a, float b) { return kAngleTolerance > std::fabs(b - a); }

bool __cdecl WallAnglesQuarterApart(float a, float b) { return kAngleTolerance > std::fabs(b - a + kHalfPi); }

uint8_t __cdecl WallOctant(float angle)
{
    const double turned = std::fmod(static_cast<double>(kPiOver8 - angle), kTwoPi);
    const float eighths = static_cast<float>(turned) / kPiOver4;
    // The floor: round(eighths - 0.5), then one more when that one more is eighths exactly.
    uint32_t n = static_cast<uint32_t>(MagicToInt(eighths - 0.5f + kRoundMagic));
    const uint32_t up = n + 1;
    if (static_cast<float>(static_cast<int32_t>(up)) == eighths) n = up;
    return static_cast<uint8_t>((n - 2) & 7);
}

// ---- the segment ------------------------------------------------------------------------------------------------------

WallDiffusionSegment::WallDiffusionSegment()
    : mStart{0.0f, 0.0f, 0.0f}, mEnd{0.0f, 0.0f, 0.0f}, mLocation{0xffffffffu, 0xffffffffu, 0, 0, 0},
      mAbove{-1, -1, -1}, mBelow{-1, -1, -1}, mFlag(1)
{
}

WallDiffusionSegment::WallDiffusionSegment(const WallDiffusionSegment &other) { *this = other; }

WallDiffusionSegment &WallDiffusionSegment::operator=(const WallDiffusionSegment &other)
{
    std::memcpy(mStart, other.mStart, sizeof(mStart));
    std::memcpy(mEnd, other.mEnd, sizeof(mEnd));
    std::memcpy(mLocation, other.mLocation, sizeof(mLocation));
    std::memcpy(mPacked, other.mPacked, sizeof(mPacked));
    std::memcpy(mCurrent, other.mCurrent, sizeof(mCurrent));
    std::memcpy(mPrevious, other.mPrevious, sizeof(mPrevious));
    mAbove = other.mAbove;
    mBelow = other.mBelow;
    mFlag = other.mFlag;
    return *this;
}

void WallDiffusionSegment::SetAbove(const WallSegmentRef *ref) { mAbove = *ref; }

void WallDiffusionSegment::SetBelow(const WallSegmentRef *ref) { mBelow = *ref; }

void WallDiffusionSegment::SetPacked(const float *rgb)
{
    const uint32_t color = PackWallColor(rgb);
    for (uint32_t &word : mPacked) word = color;
}

void WallDiffusionSegment::SetSidePacked(const float *rgb, int32_t side)
{
    const uint32_t color = PackWallColor(rgb);
    uint32_t *words = mPacked + 6 * side;
    for (int k = 0; k < 6; ++k) words[k] = color;
}

void WallDiffusionSegment::SetRow(const float *rgb, int32_t row)
{
    const uint32_t color = PackWallColor(rgb);
    uint32_t *words = reinterpret_cast<uint32_t *>(mCurrent) + 8 * row;
    for (int k = 0; k < 8; ++k) words[k] = color;
}

bool WallDiffusionSegment::MergeMax(const float *rgb, int32_t side)
{
    const uint32_t color = PackWallColor(rgb);
    uint32_t *words = mPacked + 6 * side;
    uint32_t changed = 0;
    for (int k = 0; k < 6; ++k) {
        const uint32_t old = words[k];
        uint32_t merged = 0;
        for (int shift = 0; shift < 32; shift += 8) {
            const uint32_t a = color >> shift & 0xffu;
            const uint32_t b = old >> shift & 0xffu;
            merged |= (a > b ? a : b) << shift;
        }
        changed |= merged ^ old;
        words[k] = merged;
    }
    return changed != 0;
}

void WallDiffusionSegment::FillBlock(WallDiffusionSegment *const *n, void *)
{
    // b[r][c]: mCurrent as four rows of eight packed words. Side s's word k is mPacked[6 * s + k].
    uint32_t(*b)[8] = reinterpret_cast<uint32_t(*)[8]>(mCurrent);
    for (int k = 0; k < 6; ++k) {
        b[1][1 + k] = mPacked[k];
        b[2][1 + k] = mPacked[6 + k];
    }
    // The top row from the segment before (its second side), or the block's own row 1.
    for (int k = 0; k < 6; ++k) b[0][1 + k] = n[0] ? n[0]->mPacked[6 + k] : b[1][1 + k];
    if (n[1])
        b[0][0] = n[1]->mPacked[11];
    else if (n[0]) {
        b[0][0] = n[0]->mPacked[6];
        if (n[2]) b[0][0] = n[2]->mPacked[5];
    } else
        b[0][0] = n[2] ? n[2]->mPacked[5] : b[1][1];
    // The left column from the segment above.
    if (n[2]) {
        b[1][0] = n[2]->mPacked[5];
        b[2][0] = n[2]->mPacked[11];
    } else {
        b[1][0] = b[1][1];
        b[2][0] = b[2][1];
    }
    if (n[3])
        b[3][0] = n[3]->mPacked[5];
    else if (n[2]) {
        b[3][0] = n[2]->mPacked[11];
        if (n[4]) b[3][0] = n[4]->mPacked[0];
    } else
        b[3][0] = n[4] ? n[4]->mPacked[0] : b[2][1];
    // The bottom row from the segment after (its first side).
    for (int k = 0; k < 6; ++k) b[3][1 + k] = n[4] ? n[4]->mPacked[k] : b[2][1 + k];
    if (n[5])
        b[3][7] = n[5]->mPacked[0];
    else if (n[4]) {
        b[3][7] = n[4]->mPacked[5];
        if (n[6]) b[3][7] = n[6]->mPacked[6];
    } else
        b[3][7] = n[6] ? n[6]->mPacked[6] : b[2][6];
    // The right column from the segment below.
    if (n[6]) {
        b[1][7] = n[6]->mPacked[0];
        b[2][7] = n[6]->mPacked[6];
    } else {
        b[1][7] = b[1][6];
        b[2][7] = b[2][6];
    }
    if (n[7])
        b[0][7] = n[7]->mPacked[6];
    else if (n[6]) {
        b[0][7] = n[6]->mPacked[0];
        if (n[0]) b[0][7] = n[0]->mPacked[11];
    } else
        b[0][7] = n[0] ? n[0]->mPacked[11] : b[1][6];
}

// ---- row LD2i: the segment's spreads ------------------------------------------------------------------------------------

bool __stdcall WallDiffusionSegment::SpreadWords(const Fixed16 *falloff, const uint32_t *from, uint32_t *to, int32_t count)
{
    const int16_t w = static_cast<int16_t>(falloff->mValue & 0xffff);
    bool rose = false;
    for (int32_t i = 0; i < count; ++i) rose |= SpreadWord(w, from[i], to[i]);
    return rose;
}

bool WallDiffusionSegment::SpreadAlong(const WallDiffusionSegment *prev, const WallDiffusionSegment *next, float distance)
{
    Fixed16 straight;
    Fixed16 diagonal;
    straight.SetFalloff(false, distance);
    diagonal.SetFalloff(true, distance);
    bool rose = false;
    if (prev) {
        rose |= SpreadWords(&diagonal, prev->mPacked + 6, mPacked + 1, 5);
        rose |= SpreadWords(&straight, prev->mPacked + 6, mPacked, 6);
        rose |= SpreadWords(&diagonal, prev->mPacked + 7, mPacked, 5);
    }
    if (next) {
        rose |= SpreadWords(&diagonal, next->mPacked, mPacked + 7, 5);
        rose |= SpreadWords(&straight, next->mPacked, mPacked + 6, 6);
        rose |= SpreadWords(&diagonal, next->mPacked + 1, mPacked + 6, 5);
    }
    return rose;
}

bool WallDiffusionSegment::SpreadAcross(WallDiffusionSegment *other, float distance, uint8_t up)
{
    const int16_t w = FalloffWord(false, distance);
    // up: this h 0 with the other's h 5; down: this h 5 with its h 0.
    const uint32_t mine = up ? 0 : 5;
    const uint32_t theirs = up ? 5 : 0;
    bool rose = false;
    for (uint32_t a = 0; a < 2; ++a) {
        rose |= SpreadWord(w, other->mPacked[6 * a + theirs], mPacked[6 * a + mine]);
        rose |= SpreadWord(w, mPacked[6 * a + mine], other->mPacked[6 * a + theirs]);
    }
    return rose;
}

bool WallDiffusionSegment::SpreadWithin(float distance)
{
    const int16_t w[2] = {FalloffWord(false, distance), FalloffWord(true, distance)};
    bool rose = false;
    for (int32_t a = 0; a < 2; ++a) {
        for (uint32_t k = 0; k < 8; ++k) {
            const int32_t dh = kWithin[k][0];
            const int32_t from = a + kWithin[k][1];
            if (static_cast<uint32_t>(from) > 1) continue;
            // h + dh within 0..5: h from max(-dh, 0) to min(-dh, 0) + 6.
            const int32_t first = -dh > 0 ? -dh : 0;
            const int32_t end = (-dh < 0 ? -dh : 0) + 6;
            for (int32_t h = first; h < end; ++h)
                rose |= SpreadWord(w[k & 1], mPacked[6 * from + dh + h], mPacked[6 * a + h]);
        }
    }
    return rose;
}

// ---- the run ----------------------------------------------------------------------------------------------------------

WallDiffusionRun::WallDiffusionRun() : mPrev(-1), mNext(-1), mAngle(0.0f), mDiagonal(0), mFlag19(0), mFlag1A(0), mFlag1B(0)
{
}

int32_t WallDiffusionRun::SegmentCount() const { return static_cast<int32_t>(mSegments.size()); }

int32_t WallDiffusionRun::SideCount() const { return SegmentCount() * 2; }

uint32_t *WallDiffusionRun::FrontLocation() { return mSegments.data()->mLocation; }

uint32_t WallDiffusionRun::FrontLocationWord2() const { return mSegments.data()->mLocation[2]; }

uint32_t *WallDiffusionRun::BackLocation() { return (mSegments.data() + mSegments.size() - 1)->mLocation; }

uint32_t WallDiffusionRun::FrontCellQuery()
{
    // The world's cell query (__cdecl: the location, 0).
    return OriginalFunction<uint32_t(__cdecl *)(const uint32_t *, uint32_t), 0x7f2260>()(mSegments.data()->mLocation, 0);
}

uint32_t *WallDiffusionRun::LocationAt(uint32_t i) { return mSegments.data()[i].mLocation; }

WallSegmentRef *WallDiffusionRun::AboveAt(uint32_t i) { return mSegments.data()[i].Above(); }

WallSegmentRef *WallDiffusionRun::BelowAt(uint32_t i) { return mSegments.data()[i].Below(); }

void WallDiffusionRun::SetAboveAt(uint32_t i, const WallSegmentRef *ref) { mSegments.data()[i].SetAbove(ref); }

void WallDiffusionRun::SetBelowAt(uint32_t i, const WallSegmentRef *ref) { mSegments.data()[i].SetBelow(ref); }

void WallDiffusionRun::Clear()
{
    mSegments.clear();
    mAngle = 0.0f;
}

bool WallDiffusionRun::SetAngle(float angle)
{
    mAngle = angle;
    const double a = angle;
    const bool diagonal = (a >= -kEighth7 && a <= -kEighth5) || (a >= -kEighth3 && a <= -kEighth1) ||
                          (a >= kEighth1 && a <= kEighth3) || (a >= kEighth5 && a <= kEighth7);
    mDiagonal = diagonal;
    return diagonal;
}

float WallDiffusionRun::Length() const
{
    const double step = mDiagonal ? static_cast<double>(static_cast<float>(std::sqrt(2.0))) : 1.0;
    return static_cast<float>(static_cast<double>(mSegments.size()) * step);
}

void WallDiffusionRun::SetPacked(const float *rgb)
{
    for (uint32_t i = 0; i < mSegments.size(); ++i) mSegments.data()[i].SetPacked(rgb);
}

void WallDiffusionRun::PaintGradient()
{
    const uint32_t count = static_cast<uint32_t>(SegmentCount());
    // The side count as unsigned (cvtdq2pd plus the 2^32 table 0xd9b740 by the sign bit), to float, then 1 / it.
    const float step = 1.0f / static_cast<float>(static_cast<double>(2 * count));
    for (uint32_t i = 0; i < static_cast<uint32_t>(SegmentCount()); ++i)
        for (int32_t side = 0; side < 2; ++side) {
            const float f = static_cast<float>(static_cast<int32_t>(2 * i) + side) * step;
            const float rgb[3] = {f, 0.0f, 1.0f - f};
            mSegments.data()[i].SetSidePacked(rgb, side);
        }
}

void WallDiffusionRun::PaintTestColors()
{
    static const float kRed[3] = {1.0f, 0.0f, 0.0f};
    static const float kBlue[3] = {0.0f, 0.0f, 1.0f};
    static const float kGreen[3] = {0.0f, 1.0f, 0.0f};
    for (uint32_t i = 0; i < static_cast<uint32_t>(SegmentCount()); ++i) {
        mSegments.data()[i].SetRow(kRed, 0);
        mSegments.data()[i].SetRow(kBlue, 1);
        mSegments.data()[i].SetRow(kGreen, 3);
    }
}

float *WallDiffusionRun::PointAlong(float *out, float t, float offset)
{
    // [0, 1]: 0 below 0, else minss(1, t) (t when either is NaN).
    const float along = 0.0f > t ? 0.0f : (1.0f < t ? 1.0f : t);
    const WallDiffusionSegment &first = *mSegments.data();
    // NaN pairs: every product and sum below in the original's operand order (MulSs / AddSs).
    const float distance = MulSs(static_cast<float>(static_cast<double>(static_cast<uint32_t>(SegmentCount()))), along);
    const float dx = first.mEnd[0] - first.mStart[0];
    const float dy = first.mEnd[1] - first.mStart[1];
    const float ndx = NegSs(dx);
    const float length = static_cast<float>(std::sqrt(static_cast<double>(AddSs(MulSs(dy, dy), MulSs(ndx, ndx)))));
    float nx = dy;
    float ny = ndx;
    if (length > 0.0f) {
        nx = DivSs(dy, length);
        ny = DivSs(ndx, length);
    }
    out[0] = AddSs(MulSs(nx, offset), AddSs(first.mStart[0], MulSs(dx, distance)));
    out[1] = AddSs(MulSs(ny, offset), AddSs(MulSs(dy, distance), first.mStart[1]));
    return out;
}

void WallDiffusionRun::Neighbours(std::vector<WallDiffusionRun> *runs, uint32_t i, WallDiffusionSegment **out)
{
    const WallSegmentRef *above = AboveAt(i);
    const WallSegmentRef *below = BelowAt(i);
    out[0] = i != 0 ? SegmentAt(*this, static_cast<int32_t>(i - 1)) : nullptr;
    if (above->IsValid()) {
        WallDiffusionRun &run = *RunAt(runs, above->mRun);
        const int32_t s = above->mSegment;
        out[1] = s > 0 ? SegmentAt(run, s - 1) : nullptr;
        out[2] = SegmentAt(run, s);
        out[3] = static_cast<uint32_t>(s) < static_cast<uint32_t>(run.SegmentCount() - 1) ? SegmentAt(run, s + 1) : nullptr;
    } else {
        out[1] = nullptr;
        out[2] = nullptr;
        out[3] = nullptr;
    }
    out[4] = i == static_cast<uint32_t>(SegmentCount() - 1) ? nullptr : SegmentAt(*this, static_cast<int32_t>(i + 1));
    if (below->IsValid()) {
        WallDiffusionRun &run = *RunAt(runs, below->mRun);
        const int32_t s = below->mSegment;
        out[7] = s > 0 ? SegmentAt(run, s - 1) : nullptr;
        out[6] = SegmentAt(run, s);
        out[5] = static_cast<uint32_t>(s) < static_cast<uint32_t>(run.SegmentCount() - 1) ? SegmentAt(run, s + 1) : nullptr;
    } else {
        out[7] = nullptr;
        out[6] = nullptr;
        out[5] = nullptr;
    }
    // No segment above (below): the corners from the references of the segments either side.
    if (out[0]) {
        if (!out[2]) {
            const WallSegmentRef *ref = AboveAt(i - 1);
            if (ref->IsValid()) out[1] = Referenced(runs, ref);
        }
        if (!out[6]) {
            const WallSegmentRef *ref = BelowAt(i - 1);
            if (ref->IsValid()) out[7] = Referenced(runs, ref);
        }
    }
    if (out[4]) {
        if (!out[2]) {
            const WallSegmentRef *ref = AboveAt(i + 1);
            if (ref->IsValid()) out[3] = Referenced(runs, ref);
        }
        if (!out[6]) {
            const WallSegmentRef *ref = BelowAt(i + 1);
            if (ref->IsValid()) out[5] = Referenced(runs, ref);
        }
    }
}

void WallDiffusionRun::Walk(std::vector<WallDiffusionRun> *runs, uint8_t flag, uint8_t up)
{
    SetFlag19(flag);
    for (uint32_t i = 0; i < static_cast<uint32_t>(SegmentCount()); ++i) {
        const WallSegmentRef *ref = up ? AboveAt(i) : BelowAt(i);
        if (ref->IsValid()) RunAt(runs, ref->mRun)->SetFlag1A(0);
    }
    // Nothing here stops a cycle; the references above (below) name segments a level up (down), so there is none.
    for (uint32_t i = 0; i < static_cast<uint32_t>(SegmentCount()); ++i) {
        const WallSegmentRef *ref = up ? AboveAt(i) : BelowAt(i);
        if (!ref->IsValid()) continue;
        WallDiffusionRun *run = RunAt(runs, ref->mRun);
        if (run->mFlag1A == 0) {
            run->Walk(runs, flag, up);
            run->SetFlag1A(1);
        }
    }
}

// ---- row LD2i: the propagation over the references --------------------------------------------------------------------

namespace {
// Both walks' first loop: every referenced run's mFlag1A cleared.
void ClearReferenced(WallDiffusionRun &run, std::vector<WallDiffusionRun> *runs, uint8_t up)
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(run.SegmentCount()); ++i) {
        const WallSegmentRef *ref = RefAt(run, i, up);
        if (ref->IsValid()) RunAt(runs, ref->mRun)->SetFlag1A(0);
    }
}
} // namespace

bool WallDiffusionRun::DiffuseWithin(std::vector<WallDiffusionRun> *runs, float distance, uint8_t up, uint8_t apply)
{
    ClearReferenced(*this, runs, up);
    bool rose = false;
    // Nothing here stops a cycle (see the header).
    for (uint32_t i = 0; i < static_cast<uint32_t>(SegmentCount()); ++i) {
        if (apply) rose |= SegmentAt(*this, static_cast<int32_t>(i))->SpreadWithin(distance);
        const WallSegmentRef *ref = RefAt(*this, i, up);
        if (!ref->IsValid()) continue;
        WallDiffusionRun *run = RunAt(runs, ref->mRun);
        if (run->mFlag1A == 0) {
            rose |= run->DiffuseWithin(runs, distance, up, 1);
            run->SetFlag1A(1);
        }
    }
    return rose;
}

bool WallDiffusionRun::DiffuseBetween(std::vector<WallDiffusionRun> *runs, float distance, uint8_t up, uint8_t apply)
{
    ClearReferenced(*this, runs, up);
    bool rose = false;
    const uint32_t count = static_cast<uint32_t>(SegmentCount());
    if (apply && count >= 2) {
        // The first, the last, then the middle ones.
        const bool first = SegmentAt(*this, 0)->SpreadAlong(nullptr, SegmentAt(*this, 1), distance);
        const bool last = SegmentAt(*this, static_cast<int32_t>(count - 1))
                              ->SpreadAlong(SegmentAt(*this, static_cast<int32_t>(count - 2)), nullptr, distance);
        rose = first | last;
        for (uint32_t i = 1; i < count - 1; ++i) {
            WallDiffusionSegment *s = SegmentAt(*this, static_cast<int32_t>(i));
            rose |= s->SpreadAlong(s - 1, s + 1, distance);
        }
    }
    for (uint32_t i = 0; i < static_cast<uint32_t>(SegmentCount()); ++i) {
        const WallSegmentRef *ref = RefAt(*this, i, up);
        if (ref->IsValid())
            rose |= SegmentAt(*this, static_cast<int32_t>(i))->SpreadAcross(Referenced(runs, ref), distance, up);
    }
    for (uint32_t i = 0; i < static_cast<uint32_t>(SegmentCount()); ++i) {
        const WallSegmentRef *ref = RefAt(*this, i, up);
        if (!ref->IsValid()) continue;
        WallDiffusionRun *run = RunAt(runs, ref->mRun);
        if (run->mFlag1A == 0) {
            rose |= run->DiffuseBetween(runs, distance, up, 1);
            run->SetFlag1A(1);
        }
    }
    return rose;
}

bool WallDiffusionRun::DiffuseStep(std::vector<WallDiffusionRun> *runs, float distance)
{
    bool rose = DiffuseWithin(runs, distance, 1, 1);
    rose |= DiffuseWithin(runs, distance, 0, 0);
    rose |= DiffuseBetween(runs, distance, 1, 1);
    rose |= DiffuseBetween(runs, distance, 0, 0);
    return rose;
}

bool WallDiffusionRun::Diffuse(std::vector<WallDiffusionRun> *runs, uint32_t steps, float distance)
{
    bool rose = false;
    for (uint32_t i = 0; i < steps; ++i) {
        if (!DiffuseStep(runs, distance)) break;
        rose = true;
    }
    return rose;
}

void WallDiffusionRun::SetFlag19Hook(uint8_t value) { SetFlag19(value); }

void WallDiffusionRun::SetFlag1AHook(uint8_t value) { SetFlag1A(value); }

// ---- the manager ------------------------------------------------------------------------------------------------------

cWallDiffusionManager::cWallDiffusionManager() = default;

cWallDiffusionManager::~cWallDiffusionManager() = default;

__declspec(noinline) cWallDiffusionManager *__fastcall cWallDiffusionManager::Construct(void *memory, void *)
{
    return ::new (memory) cWallDiffusionManager;
}

cWallDiffusionManager *cWallDiffusionManager::DeletingDestructor(uint32_t flags)
{
    this->cWallDiffusionManager::~cWallDiffusionManager();
    if (flags & 1) ::operator delete(this, sizeof(cWallDiffusionManager));
    return this;
}

__declspec(noinline) void cWallDiffusionManager::SetInsideAmbientColor(const float *color)
{
    mInsideAmbientColor[0] = color[0];
    mInsideAmbientColor[1] = color[1];
    mInsideAmbientColor[2] = color[2];
}

__declspec(noinline) void cWallDiffusionManager::SetOutsideWallAmbient(float value) { mOutsideWallAmbient = value; }

__declspec(noinline) void cWallDiffusionManager::SetPropagationSteps(int32_t steps) { mPropagationSteps = steps; }

__declspec(noinline) void cWallDiffusionManager::SetInsideWallAmbient(float value) { mInsideWallAmbient = value; }

__declspec(noinline) void cWallDiffusionManager::SetThreshold(float value) { mThreshold = value; }

__declspec(noinline) void cWallDiffusionManager::SetOutsideAmbientColor(const float *color)
{
    mOutsideAmbientColor[0] = color[0];
    mOutsideAmbientColor[1] = color[1];
    mOutsideAmbientColor[2] = color[2];
}

// ---- row LD2h: the simple passes ------------------------------------------------------------------------------------

float *cWallDiffusionManager::AmbientFor(float *out, WallDiffusionRun *run)
{
    // The answer's low 16 bits (test ax, ax at 0x40c748).
    const bool inside = static_cast<uint16_t>(run->FrontCellQuery()) != 0;
    const float *color = inside ? mInsideAmbientColor : mOutsideAmbientColor;
    const float scale = inside ? mInsideWallAmbient : mOutsideWallAmbient;
    out[0] = MulSs(color[0], scale); // a NaN pair: the colour's NaN
    out[1] = MulSs(color[1], scale);
    out[2] = MulSs(color[2], scale);
    return out;
}

__declspec(noinline) void cWallDiffusionManager::AmbientPass()
{
    for (uint32_t i = 0; i < mRuns.size(); ++i) {
        float ambient[3];
        WallDiffusionRun *run = mRuns.data() + i;
        run->SetPacked(AmbientFor(ambient, run));
        mRuns.data()[i].SetFlag19(0);
    }
}

__declspec(noinline) void cWallDiffusionManager::GradientPass()
{
    const int32_t count = static_cast<int32_t>(mRuns.size());
    for (int32_t i = 0; i < count; ++i) mRuns.data()[i].PaintGradient();
}

__declspec(noinline) void cWallDiffusionManager::TestColorPass()
{
    const int32_t count = static_cast<int32_t>(mRuns.size());
    for (int32_t i = 0; i < count; ++i) mRuns.data()[i].PaintTestColors();
}

__declspec(noinline) void cWallDiffusionManager::BorderPass()
{
    const int32_t count = static_cast<int32_t>(mRuns.size());
    for (int32_t r = 0; r < count; ++r) {
        WallDiffusionRun &run = mRuns.data()[r];
        void *list = static_cast<uint16_t>(run.FrontCellQuery()) != 0 ? static_cast<void *>(&mChanges)
                                                                       : static_cast<void *>(&mChanges2);
        WallDiffusionSegment *neighbours[8];
        for (uint32_t i = 0; i < static_cast<uint32_t>(run.SegmentCount()); ++i) {
            run.Neighbours(&mRuns, i, neighbours);
            run.mSegments.data()[i].FillBlock(neighbours, list);
        }
    }
}

// ---- row LD2e: the entry points ---------------------------------------------------------------------------------------

static_assert(sizeof(DiffusionWall) == sizeof(WallDiffusionChange), "the wall lists hold the change records");

__declspec(noinline) void cWallDiffusionManager::CollectWalls2(std::vector<DiffusionWall> *out)
{
    const DiffusionWall *first = reinterpret_cast<const DiffusionWall *>(mChanges.data());
    out->insert(out->end(), first, first + mChanges.size());
}

__declspec(noinline) void cWallDiffusionManager::CollectWalls(std::vector<DiffusionWall> *out)
{
    const DiffusionWall *first = reinterpret_cast<const DiffusionWall *>(mChanges2.data());
    out->insert(out->end(), first, first + mChanges2.size());
}

__declspec(noinline) void cWallDiffusionManager::Reset()
{
    for (uint32_t r = 0; r < mRuns.size(); ++r) {
        WallDiffusionRun &run = mRuns.data()[r];
        for (uint32_t i = 0; i < run.mSegments.size(); ++i) {
            WallDiffusionSegment &segment = run.mSegments.data()[i];
            std::memcpy(segment.mPrevious, segment.mCurrent, sizeof(segment.mPrevious));
        }
    }
    mChanges2.clear();
    mChanges.clear();
}

namespace {

// One channel of Mark: round(c * 256) through the magic add (addss, to nearest even), less itself shifted right by 8
// (logical), low byte; added to the stored byte, at most 255 (unsigned).
// ORIGINAL-BUG: the input isn't clamped first, so a channel outside 0..1 wraps in the low byte: -2/256 adds 255
// (saturates), 2.0 adds 254.
uint32_t MarkChannel(float c, uint32_t stored)
{
    const float magic = AddSs(c, kColorMagic);
    uint32_t bits;
    std::memcpy(&bits, &magic, sizeof(bits));
    const uint32_t v = bits - kColorMagicBits;
    const uint32_t sum = ((v - (v >> 8)) & 0xffu) + stored;
    return sum >= 0xffu ? 0xffu : sum;
}

} // namespace

__declspec(noinline) void cWallDiffusionManager::Mark(int32_t level, const float *rgb)
{
    for (uint32_t r = 0; r < mRuns.size(); ++r) {
        // cmp ax, word [level] (0x40c848): the low 16 bits only.
        if (static_cast<uint16_t>(mRuns.data()[r].FrontCellQuery()) != static_cast<uint16_t>(level)) continue;
        for (uint32_t i = 0; i < mRuns.data()[r].mSegments.size(); ++i) {
            uint8_t *block = mRuns.data()[r].mSegments.data()[i].mCurrent;
            for (uint32_t w = 0; w < 0x80; w += 4) {
                uint32_t word;
                std::memcpy(&word, block + w, 4);
                const uint32_t red = MarkChannel(rgb[0], (word >> 16) & 0xffu);
                const uint32_t green = MarkChannel(rgb[1], (word >> 8) & 0xffu);
                const uint32_t blue = MarkChannel(rgb[2], word & 0xffu);
                word = (word & 0xff000000u) | (red << 16) | (green << 8) | blue;
                std::memcpy(block + w, &word, 4);
            }
        }
    }
}

__declspec(noinline) WallSegmentRef *cWallDiffusionManager::FindSegment(WallSegmentRef *out, const uint32_t *location,
                                                                        WallSegmentRef *copy)
{
    const uint32_t runs = static_cast<uint32_t>(mRuns.size());
    WallSegmentRef found{-1, -1, -1};
    for (uint32_t r = 0; r < runs; ++r) {
        WallDiffusionRun &run = mRuns.data()[r];
        if (run.FrontLocationWord2() != location[2]) continue;
        const uint32_t segments = static_cast<uint32_t>(run.SegmentCount());
        uint32_t i = 0;
        for (; i < segments; ++i) {
            const uint32_t *at = run.LocationAt(i);
            if (at[0] == location[0] && at[1] == location[1] && at[2] == location[2] && at[3] == location[3] &&
                at[4] == location[4])
                break;
        }
        if (i < segments) {
            found = {static_cast<int32_t>(run.FrontLocationWord2()), static_cast<int32_t>(r), static_cast<int32_t>(i)};
            break;
        }
    }
    *out = found;
    if (copy) *copy = *out;
    return out;
}

__declspec(noinline) float *cWallDiffusionManager::GetColor(float *out, const float *wall)
{
    int32_t cell[5];
    ::worldrect::CellOfRect(cell, wall);
    WallSegmentRef ref;
    FindSegment(&ref, reinterpret_cast<const uint32_t *>(cell), nullptr);
    if (!ref.IsValid()) {
        out[0] = 0.0f; // the static vector 0x10da730: zero in .data, never written
        out[1] = 0.0f;
        out[2] = 0.0f;
        return out;
    }
    WallDiffusionRun &run = *RunAt(&mRuns, ref.mRun);
    const uint32_t word = Referenced(&mRuns, &ref)->mPacked[9];
    float color[3] = {DivSs(static_cast<float>(static_cast<int32_t>((word >> 16) & 0xffu)), 255.0f),
                      DivSs(static_cast<float>(static_cast<int32_t>((word >> 8) & 0xffu)), 255.0f),
                      DivSs(static_cast<float>(static_cast<int32_t>(word & 0xffu)), 255.0f)};
    // cmp byte [mOutsideAmbientEnabled], 0, then test ax, ax on the run's answer (0x4103f7): outside less the outside ambient.
    if (mOutsideAmbientEnabled != 0 && static_cast<uint16_t>(run.FrontCellQuery()) == 0) {
        const float scale = mOutsideWallAmbient;
        color[0] = color[0] - MulSs(mOutsideAmbientColor[0], scale);
        color[1] = color[1] - MulSs(mOutsideAmbientColor[1], scale);
        color[2] = color[2] - MulSs(mOutsideAmbientColor[2], scale);
    }
    out[0] = color[0];
    out[1] = color[1];
    out[2] = color[2];
    return out;
}

// ---- row LD2i ---------------------------------------------------------------------------------------------------------

__declspec(noinline) void cWallDiffusionManager::DiffuseMarked()
{
    for (const uint32_t run : mMarkedRuns)
        RunAt(&mRuns, static_cast<int32_t>(run))->Diffuse(&mRuns, static_cast<uint32_t>(mPropagationSteps), mThreshold);
    mMarkedRuns.clear();
}

// ---- row LD2j: Step's maths -------------------------------------------------------------------------------------------

namespace {

// A packed colour's four bytes as 16-bit words (punpcklbw with zero), the painters' MMX register a word at a time.
struct ColorWords {
    int16_t w[4];
};

ColorWords Unpacked(uint32_t packed)
{
    ColorWords c;
    for (int k = 0; k < 4; ++k) c.w[k] = static_cast<int16_t>((packed >> (8 * k)) & 0xffu);
    return c;
}

// packuswb: each word saturated to 0..255.
uint32_t Saturated(const ColorWords &c)
{
    uint32_t out = 0;
    for (int k = 0; k < 4; ++k) {
        const int32_t v = c.w[k] < 0 ? 0 : (c.w[k] > 255 ? 255 : c.w[k]);
        out |= static_cast<uint32_t>(v) << (8 * k);
    }
    return out;
}

// psllw 4 then pmulhw by the falloff word: (word << 4, wrapping in 16 bits) times the word, high half, signed.
ColorWords Scaled(const ColorWords &c, int16_t falloff)
{
    ColorWords out;
    for (int k = 0; k < 4; ++k) {
        const int16_t shifted = static_cast<int16_t>(static_cast<uint16_t>(static_cast<uint16_t>(c.w[k]) << 4));
        out.w[k] = static_cast<int16_t>((static_cast<int32_t>(shifted) * falloff) >> 16);
    }
    return out;
}

// The painters' inner loop over a side's words 1..4: per word, the greater of the colour and the stored byte (pcmpgtw,
// signed) stored saturated; whether any word's colour was above (the xor of new and old, or-ed, packsswb, nonzero).
bool PaintWords(const ColorWords &c, uint32_t *side)
{
    bool rose = false;
    for (int i = 1; i < 5; ++i) {
        const ColorWords old = Unpacked(side[i]);
        ColorWords kept;
        for (int k = 0; k < 4; ++k) {
            kept.w[k] = c.w[k] > old.w[k] ? c.w[k] : old.w[k];
            if (kept.w[k] != old.w[k]) rose = true;
        }
        side[i] = Saturated(kept);
    }
    return rose;
}

// The painters' falloff word: Fixed16::SetFromFloat of 0.0625 / (1 + distance (times 1.4 on a diagonal run)) in
// doubles, its low half (punpcklwd).
int16_t PaintFalloff(const WallDiffusionRun &run, float distance)
{
    double d = distance;
    if (run.mDiagonal) d *= 1.4;
    d += 1.0;
    Fixed16 f;
    f.SetFromFloat(static_cast<float>(0.0625 / d));
    return static_cast<int16_t>(f.mValue & 0xffff);
}

// Side k's six words: segment k / 2's half k % 2 (the originals index from the first element, unchecked).
uint32_t *SideWords(WallDiffusionRun &run, uint32_t segment, uint32_t half)
{
    return SegmentAt(run, static_cast<int32_t>(segment))->mPacked + half * 6;
}

// 0x19d250 (__cdecl, still the game's; also cLightingManagerEdges.cpp): a.y * b.y + a.x * b.x on the x87, at its
// precision, in st0.
double Dot2D87(const float *a, const float *b)
{
    return OriginalFunction<double(__cdecl *)(const float *, const float *), 0x19d250>()(a, b);
}

constexpr float kLightNear = 0x1.a36e2ep-14f; // 1e-4 (0xda74e0)

} // namespace

__declspec(noinline) int32_t WallDiffusionRun::Project(float x, float y, float *distance, float *t, float *nearest)
{
    const WallDiffusionSegment *first = mSegments.data();
    const float ey = first->mEnd[1] - first->mStart[1];
    const float dx = x - first->mStart[0];
    const float dy = y - first->mStart[1];
    const float ex = first->mEnd[0] - first->mStart[0];
    // The roots: sqrtsd, or the CRT's for a negative square (never: a sum of squares).
    const float reach = static_cast<float>(std::sqrt(static_cast<double>(AddSs(MulSs(dy, dy), MulSs(dx, dx)))));
    const float base = static_cast<float>(std::sqrt(static_cast<double>(AddSs(MulSs(ey, ey), MulSs(ex, ex)))));
    const float uy = DivSs(ey, base);
    const float ux = DivSs(ex, base);
    const float across = MulSs(uy, dx) - MulSs(ux, dy);
    const float length = Length();
    const float along = DivSs(AddSs(MulSs(uy, dy), MulSs(ux, dx)), length);
    if (t) *t = along;
    if (0.0f > along) {
        if (distance) *distance = across >= 0.0f ? reach : NegSs(reach);
        if (nearest) std::memcpy(nearest, first->mStart, 2 * sizeof(float)); // two dwords moved as integers
        return 0;
    }
    if (1.0f > along) {
        *distance = across;
        if (nearest) {
            nearest[0] = AddSs(MulSs(MulSs(ux, along), length), first->mStart[0]);
            nearest[1] = AddSs(MulSs(MulSs(uy, along), length), first->mStart[1]);
        }
        const uint32_t sides = static_cast<uint32_t>(SegmentCount() * 2);
        const float scaled = MulSs(static_cast<float>(static_cast<double>(sides)), along);
        // floor by the magic sum: round(scaled - 0.5) to nearest even, one up when that lands on scaled exactly.
        int32_t side = MagicToInt(AddSs(scaled - 0.5f, kRoundMagic));
        const int32_t up = static_cast<int32_t>(static_cast<uint32_t>(side) + 1u);
        if (static_cast<float>(up) == scaled) side = up;
        if (side < 0) return 0;
        if (static_cast<uint32_t>(side) >= sides) side = static_cast<int32_t>(sides - 1u);
        return side;
    }
    if (distance) *distance = across >= 0.0f ? reach : NegSs(reach);
    if (nearest) {
        nearest[0] = AddSs(MulSs(ux, length), first->mStart[0]);
        nearest[1] = AddSs(MulSs(uy, length), first->mStart[1]);
    }
    return static_cast<int32_t>(static_cast<uint32_t>(SegmentCount()) * 2u - 1u);
}

__declspec(noinline) bool WallDiffusionRun::PaintBack(const float *rgb, float distance, int32_t side)
{
    ColorWords color = Unpacked(PackWallColor(rgb));
    const int16_t falloff = PaintFalloff(*this, distance);
    int32_t k = side;
    while (k >= 0) {
        uint32_t *words = SideWords(*this, static_cast<uint32_t>(k) >> 1, static_cast<uint32_t>(k) & 1u);
        if (!PaintWords(color, words)) break;
        --k;
        color = Scaled(color, falloff);
        words[0] = words[5] = Saturated(color);
    }
    return k < side;
}

__declspec(noinline) bool WallDiffusionRun::PaintAhead(const float *rgb, float distance, int32_t side)
{
    const int32_t count = SideCount();
    const int32_t from = static_cast<int32_t>(static_cast<uint32_t>(side) + 1u);
    const uint32_t packed = PackWallColor(rgb);
    const int16_t falloff = PaintFalloff(*this, distance);
    ColorWords color = Scaled(Unpacked(packed), falloff);
    int32_t k = from;
    while (k < count) {
        // idiv by 2: signed quotient and remainder (only side >= -1 reaches here from Light).
        uint32_t *words = SideWords(*this, static_cast<uint32_t>(k / 2), static_cast<uint32_t>(k % 2));
        if (!PaintWords(color, words)) break;
        ++k;
        color = Scaled(color, falloff);
        words[0] = words[5] = Saturated(color);
    }
    return k > from;
}

__declspec(noinline) bool WallDiffusionRun::Light(float intensity, float spread, const float *point, const WallLight *light)
{
    float nearest[2];
    float along;
    float distance = 0.0f;
    const int32_t side = Project(point[0], point[1], &distance, &along, nearest);
    if (distance <= 0.0f) return false; // a NaN distance goes on
    float facing = 0.0f;
    float factor = 1.0f;
    const uint8_t directional = light->mDirectional;
    if (directional && distance > kLightNear) {
        float toward[2] = {DivSs(nearest[0] - point[0], distance), DivSs(nearest[1] - point[1], distance)};
        float direction[2];
        direction[0] = static_cast<float>(std::cos(static_cast<double>(light->mAngle)));
        direction[1] = static_cast<float>(std::sin(static_cast<double>(light->mAngle)));
        const float f = static_cast<float>(Dot2D87(toward, direction));
        const double twice = static_cast<double>(f) + static_cast<double>(f);
        factor = static_cast<float>((6.0 - twice) / (twice + 6.0));
        if (f > 0.0f) facing = f;
        const float lo = 0.0f > light->mFocus ? 0.0f : light->mFocus; // maxss / minss: a NaN focus kept
        const float focus = 1.0f < lo ? 1.0f : lo;
        const float rest = 1.0f - focus;
        const float half = static_cast<float>((static_cast<double>(f) + 1.0) * 0.5);
        intensity = MulSs(AddSs(MulSs(half, 1.0f - rest), rest), intensity);
    }
    const float base = DivSs(1.0f, AddSs(spread, 1.0f));
    const float scaled = MulSs(MulSs(distance, 2.0f), factor);
    const float exponent = 1.0f > scaled ? 1.0f : scaled; // maxss: a NaN kept
    const float power = static_cast<float>(std::pow(static_cast<double>(base), static_cast<double>(exponent)));
    const float k = MulSs(power, intensity);
    float color[3];
    for (int c = 0; c < 3; ++c) color[c] = MulSs(light->mColor[c], k);
    float reach = spread;
    if (directional) reach = AddSs(MulSs(DivSs(AddSs(spread, spread), exponent) - spread, facing), spread);
    reach = 0.0f > reach ? 0.0f : reach;
    PaintBack(color, reach, side);
    PaintAhead(color, reach, side);
    return true;
}

// ---- row LD2k: the rebuild (lane 261110h) -----------------------------------------------------------------------------

namespace {

// The partitions' service (0x20de97: cached in 0x10cc2b0, else the world's slot 39; cPartitionManager, not ours).
void *Partitions() { return OriginalFunction<void *(__cdecl *)(), 0x20de97>()(); }

// The world's cell query (__cdecl: the location, a flag).
uint32_t CellQuery(const uint32_t *location, uint32_t flag)
{
    return OriginalFunction<uint32_t(__cdecl *)(const uint32_t *, uint32_t), 0x7f2260>()(location, flag);
}

nTSWorld::cIWorldDB *World() { return static_cast<nTSWorld::cIWorldDB *>(OriginalFunction<void *(__cdecl *)(), 0x20e0b2>()()); }

// The partitions' catalog id of the wall segments BuildAll looks for.
constexpr uint32_t kWallSegmentId = 0x1d;
// The layer kinds BuildAll walks first (0xdc25a0).
constexpr uint32_t kLayerKinds[] = {3, 8, 0xa, 0xb};

// The last run appended (the library push_back: in place by 0x40bb20, else the realloc insert 0x40b100, growing as ours)
// linked to the one before it when both belong to the chain begun at `first` (runs[n - 2].mNext = n - 1,
// runs[n - 1].mPrev = n - 2; the folded setters 0x1eeb80 / 0x149c60).
void LinkLast(std::vector<WallDiffusionRun> &runs, uint32_t first)
{
    const uint32_t n = static_cast<uint32_t>(runs.size());
    if (first < n - 1) {
        runs.data()[n - 2].mNext = static_cast<int32_t>(n - 1);
        runs.data()[n - 1].mPrev = static_cast<int32_t>(n - 2);
    }
}

} // namespace

__declspec(noinline) void WallDiffusionRun::AddSegment(const float *start, const float *end, const uint32_t *location)
{
    WallDiffusionSegment segment;
    std::memcpy(segment.mStart, start, sizeof(segment.mStart));
    std::memcpy(segment.mEnd, end, sizeof(segment.mEnd));
    std::memcpy(segment.mLocation, location, sizeof(segment.mLocation));
    Partitions(); // the answer unused (the original's)
    mSegments.push_back(segment);
}

__declspec(noinline) void cWallDiffusionManager::BuildRuns(nTSWorld::cIWallSection *section)
{
    WallDiffusionRun run;
    for (uint32_t side = 0; side < 4; side += 2) {
        nTSWorld::cIWallEdge *edge = section->EdgeAtC(side);
        if (edge->Flag()) continue;
        const float angle = edge->Angle();
        nTSWorld::cIWallEdge *start = edge;
        for (nTSWorld::cIWallEdge *back = edge->RotOnextInvRotC(); back;) {
            const float backAngle = back->Angle();
            if (!WallAnglesNear(backAngle, angle)) break;
            start = back;
            back = back->RotOnextInvRotC();
        }
        const uint32_t first = static_cast<uint32_t>(mRuns.size());
        run.Clear();
        run.SetAngle(angle);
        bool firstEdge = true;
        uint32_t lastLevel = 0xffffffffu;
        nTSWorld::cIWallEdge *e = start;
        do {
            nTSWorld::cIWallVertex *origin = e->Vertex();
            nTSWorld::cIWallVertex *dest = e->DestVertex();
            const float *to = dest->Position();
            const float *from = origin->Position();
            float rect[6];
            std::memcpy(rect, from, 3 * sizeof(float));
            std::memcpy(rect + 3, to, 3 * sizeof(float));
            uint32_t cell[5];
            ::worldrect::CellOfRect(reinterpret_cast<int32_t *>(cell), rect);
            const uint32_t level = CellQuery(cell, 0) & 0xffffu;
            const uint32_t runLevel = firstEdge ? level : lastLevel;
            firstEdge = false;
            const float runAngle = run.mAngle;
            const float edgeAngle = e->Angle();
            if (!WallAnglesNear(edgeAngle, runAngle) || static_cast<uint16_t>(level) != static_cast<uint16_t>(runLevel)) {
                mRuns.push_back(run);
                LinkLast(mRuns, first);
                run.Clear();
                run.SetAngle(e->Angle());
            }
            float toGround[3];
            float fromGround[3];
            ::worldmath::WithGroundHeight(toGround, dest->Position());
            ::worldmath::WithGroundHeight(fromGround, origin->Position());
            run.AddSegment(fromGround, toGround, cell);
            lastLevel = level;
            e->SetFlag(1);
            e = e->SymOnextC();
        } while (e != start);
        if (run.SegmentCount() != 0) {
            mRuns.push_back(run);
            run.Clear();
            LinkLast(mRuns, first);
        }
        // The side's chain closed into a ring.
        const uint32_t n = static_cast<uint32_t>(mRuns.size());
        if (first < n - 1) {
            mRuns.data()[n - 1].mNext = static_cast<int32_t>(first);
            mRuns.data()[first].mPrev = static_cast<int32_t>(n - 1);
        }
    }
}

__declspec(noinline) void cWallDiffusionManager::BuildAll()
{
    void *const partitions = Partitions();
    for (const uint32_t kind : kLayerKinds) {
        // receiver: the partitions (the layer of a kind, counted)
        cIGZUnknown *const layer = RawSlot<40, cIGZUnknown *>(partitions, kind);
        if (layer) layer->AddRef();
        RawSlot<14, uint32_t>(layer); // receiver: the layer (null not checked: the original's)
        const int32_t count = static_cast<int32_t>(RawSlot<15, uint32_t>(layer)); // receiver: the layer (its sections)
        for (int32_t i = 0; i < count; ++i)
            BuildRuns(RawSlot<11, nTSWorld::cIWallSection *>(layer, i)); // receiver: the layer (section i)
        if (layer) layer->Release();
    }
    cIGZUnknown *const layer = RawSlot<40, cIGZUnknown *>(partitions, 0u); // receiver: the partitions
    if (layer) layer->AddRef();
    RawSlot<14, uint32_t>(layer); // receiver: the layer
    for (uint32_t i = 0; i < RawSlot<55, uint32_t>(partitions); ++i) { // receiver: the partitions (the segment count)
        // The record slot 56 fills: 0x18 bytes (0x81a130 writes six words, two positions). The original's frame shows
        // only 8 bytes before the next local it writes, but that local (the kinds) is dead by then: the size is the
        // callee's, not the frame's (an 8-byte buffer here let the game's slot 56 overwrite our return address).
        uint32_t segment[6];
        RawSlot<56, uint32_t>(partitions, segment, i); // receiver: the partitions (segment i)
        if (RawSlot<50, uint32_t>(partitions, static_cast<const uint32_t *>(segment)) != kWallSegmentId)
            continue; // receiver: the partitions (the segment's catalog id)
        // receiver: the layer (the segment's section)
        if (nTSWorld::cIWallSection *const section = RawSlot<12, nTSWorld::cIWallSection *>(layer, segment))
            BuildRuns(section);
    }
    if (layer) layer->Release();
}

__declspec(noinline) void cWallDiffusionManager::LinkLevels()
{
    nTSWorld::cIWorldDB *const world = World();
    const int32_t maxLevel = world->GetMaxLevel();
    const int32_t minLevel = world->GetMinLevel();
    for (uint32_t r = 0; r < static_cast<uint32_t>(mRuns.size()); ++r) {
        WallDiffusionRun &run = mRuns.data()[r];
        if (run.SegmentCount() == 0) continue;
        uint32_t i = 0;
        do {
            uint32_t location[5];
            std::memcpy(location, run.LocationAt(i), sizeof(location));
            uint32_t up[5];
            std::memcpy(up, location, sizeof(up));
            up[2] = location[2] + 1u;
            WallSegmentRef copy;
            if (static_cast<int32_t>(up[2]) <= maxLevel) {
                worldmath::TileQuadrant tile;
                worldrect::CellToQuadrants(&tile, reinterpret_cast<const int32_t *>(up));
                if (!world->HasFloorOrGround(reinterpret_cast<const nTSWorld::WorldTile *>(&tile),
                                             static_cast<int8_t>(tile.quadrant), true)) {
                    WallSegmentRef found;
                    run.SetAboveAt(i, FindSegment(&found, up, &copy));
                }
            }
            uint32_t down[5];
            std::memcpy(down, location, sizeof(down));
            down[2] = location[2] - 1u;
            if (static_cast<int32_t>(down[2]) >= minLevel) {
                worldmath::TileQuadrant tile;
                worldrect::CellToQuadrants(&tile, reinterpret_cast<const int32_t *>(location));
                if (!world->HasFloorOrGround(reinterpret_cast<const nTSWorld::WorldTile *>(&tile),
                                             static_cast<int8_t>(tile.quadrant), true)) {
                    WallSegmentRef found;
                    run.SetBelowAt(i, FindSegment(&found, down, &copy));
                }
            }
            ++i;
        } while (i < static_cast<uint32_t>(run.SegmentCount()));
    }
}

__declspec(noinline) void cWallDiffusionManager::Rebuild()
{
    mRuns.clear();
    BuildAll();
    LinkLevels();
}

// ---- row LD2f (lane 261110i): Step and the blob pass ----

namespace {

constexpr float kBlendHalf = 0.5f;      // 0xd9c694
constexpr float kBlendOne = 1.0f;       // 0xd9c698
constexpr float kBlendNear = 0.8f;      // 0x3f4ccccd, BlobPass's rows next to the corner
constexpr float kBlendFar = 0.4f;       // 0x3ecccccd, the rows after them
constexpr float kBlobPi = 3.14159274f;  // 0xda77d8
constexpr double kBlobHalf = 0.5;       // 0xda1870
constexpr double kLevelOne = 1.0;       // 0xda1878

// One blob of the corner where `from` ends and `to` starts.
WallBlob MakeBlob(WallDiffusionRun *from, WallDiffusionRun *to)
{
    WallBlob blob{};
    std::memcpy(blob.mBack, from->BackLocation(), sizeof(blob.mBack));
    std::memcpy(blob.mFront, to->FrontLocation(), sizeof(blob.mFront));
    const float sum = AddSs(SubSs(from->mAngle, kBlobPi), to->mAngle);
    blob.mOctant = WallOctant(NarrowSd(MulSd(WidenSs(sum), kBlobHalf)));
    blob.mSingle = from->SegmentCount() == 1 || to->SegmentCount() == 1;
    return blob;
}

void AppendBlob(std::vector<DiffusionBlob44> *out, const WallBlob &blob)
{
    static_assert(sizeof(DiffusionBlob44) == sizeof(WallBlob), "the blob records");
    DiffusionBlob44 record;
    std::memcpy(&record, &blob, sizeof(record));
    out->push_back(record);
}

} // namespace

void WallDiffusionSegment::BlendRow(const float *rgb, float scale, int32_t row)
{
    const float scaled[3] = {MulSs(rgb[0], scale), MulSs(rgb[1], scale), MulSs(rgb[2], scale)};
    const uint32_t packed = PackWallColor(scaled);
    const float weight = AddSs(MulSs(MulSs(SubSs(scale, kBlendOne), kBlendHalf), kFixedOne), kRoundMagic);
    uint32_t weightBits;
    std::memcpy(&weightBits, &weight, sizeof(weightBits));
    const int32_t w = static_cast<int16_t>(weightBits & 0xffffu);
    uint32_t *words = reinterpret_cast<uint32_t *>(mCurrent + static_cast<uint32_t>(row) * 0x20u);
    for (int k = 0; k < 8; ++k) {
        uint32_t out = 0;
        for (uint32_t shift = 0; shift < 32; shift += 8) {
            const int32_t b = static_cast<int32_t>((words[k] >> shift) & 0xffu);
            const int32_t p = static_cast<int32_t>((packed >> shift) & 0xffu);
            int32_t v = p - ((w * (2 * b)) >> 16);
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            out |= static_cast<uint32_t>(v) << shift;
        }
        words[k] = out;
    }
}

void WallDiffusionRun::BlendRowAt(const float *rgb, float scale, uint32_t k)
{
    reinterpret_cast<WallDiffusionSegment *>(reinterpret_cast<uintptr_t>(mSegments.data()) +
                                             (k >> 2) * static_cast<uint32_t>(sizeof(WallDiffusionSegment)))
        ->BlendRow(rgb, scale, static_cast<int32_t>(k & 3u));
}

__declspec(noinline) void cWallDiffusionManager::Step(const std::vector<WallLight> *lights)
{
    nTSWorld::cIWorldDB *const world = worldsvc::CachedWorldDB();
    for (uint32_t l = 0; l < static_cast<uint32_t>(lights->size()); ++l) {
        const WallLight *const light = lights->data() + l;
        for (uint32_t r = 0; r < static_cast<uint32_t>(mRuns.size()); ++r) {
            WallDiffusionRun *const run = RunAt(&mRuns, static_cast<int32_t>(r));
            if (static_cast<int32_t>(run->FrontLocationWord2()) > light->mLevel) continue;
            if (static_cast<uint16_t>(run->FrontCellQuery()) != static_cast<uint16_t>(light->mLevelId)) continue;
            float ambient[3];
            AmbientFor(ambient, run); // the colour is not used (the original's)
            const float point[2] = {MulSs(AddSs(light->mMin[0], light->mMax[0]), kBlendHalf),
                                    MulSs(AddSs(light->mMin[1], light->mMax[1]), kBlendHalf)};
            float intensity = light->mIntensity;
            if (static_cast<int32_t>(run->FrontLocationWord2()) < light->mLevel) {
                float position[3] = {point[0], point[1], 0.0f};
                int32_t level = light->mLevel;
                std::memcpy(&position[2], &level, sizeof(level));
                if (static_cast<int32_t>(run->FrontLocationWord2()) < light->mLevel) {
                    do {
                        worldmath::TileQuadrant tile;
                        worldmath::PositionToTileQuadrant(&tile, position);
                        if (world->HasFloorOrGround(reinterpret_cast<const nTSWorld::WorldTile *>(&tile),
                                                    static_cast<int8_t>(tile.quadrant), true))
                            break;
                        --level;
                        std::memcpy(&position[2], &level, sizeof(level));
                        intensity = NarrowSd(MulSd(SubSd(kLevelOne, WidenSs(light->mFalloff)), WidenSs(intensity)));
                    } while (level > static_cast<int32_t>(run->FrontLocationWord2()));
                }
            }
            if (!run->Light(intensity, MulSs(light->mSpread, mThreshold), point, light)) continue;
            run->Walk(&mRuns, 1, 1);
            mMarkedRuns.insert(static_cast<int32_t>(r));
        }
    }
}

__declspec(noinline) void cWallDiffusionManager::BlobPass()
{
    for (uint32_t i = 0; i < static_cast<uint32_t>(mRuns.size()); ++i) {
        WallDiffusionRun *const run = RunAt(&mRuns, static_cast<int32_t>(i));
        if (!run->mFlag19) continue;
        float ambient[3];
        AmbientFor(ambient, run);
        if (run->mPrev >= 0) {
            WallDiffusionRun *const prev = RunAt(&mRuns, run->mPrev);
            if (prev->mFlag19 != run->mFlag19 && WallAnglesQuarterApart(prev->mAngle, run->mAngle)) {
                run->BlendRowAt(ambient, kBlendNear, 0);
                run->BlendRowAt(ambient, kBlendNear, 1);
                run->BlendRowAt(ambient, kBlendFar, 2);
                run->BlendRowAt(ambient, kBlendFar, static_cast<uint32_t>(run->SegmentCount()) > 1u ? 4u : 3u);
            }
        }
        if (run->mNext >= 0) {
            WallDiffusionRun *const next = RunAt(&mRuns, run->mNext);
            if (next->mFlag19 != run->mFlag19 && WallAnglesQuarterApart(run->mAngle, next->mAngle)) {
                const uint32_t last = static_cast<uint32_t>(run->SegmentCount()) * 4u - 1u;
                run->BlendRowAt(ambient, kBlendNear, last);
                run->BlendRowAt(ambient, kBlendNear, last - 1u);
                run->BlendRowAt(ambient, kBlendFar, last - 2u);
                run->BlendRowAt(ambient, kBlendFar, static_cast<uint32_t>(run->SegmentCount()) > 1u ? last - 4u : last - 3u);
            }
        }
    }
}

__declspec(noinline) void cWallDiffusionManager::CollectBlobs(std::vector<DiffusionBlob44> *out)
{
    out->clear();
    for (uint32_t i = 0; i < static_cast<uint32_t>(mRuns.size()); ++i) {
        WallDiffusionRun *const run = RunAt(&mRuns, static_cast<int32_t>(i));
        run->SetFlag1A(0);
        run->SetFlag1B(0);
    }
    for (uint32_t i = 0; i < static_cast<uint32_t>(mRuns.size()); ++i) {
        WallDiffusionRun *const run = RunAt(&mRuns, static_cast<int32_t>(i));
        if (run->mNext >= 0) {
            WallDiffusionRun *const next = RunAt(&mRuns, run->mNext);
            if (!run->mFlag1B && !(run->mFlag19 && next->mFlag19) && WallAnglesQuarterApart(run->mAngle, next->mAngle)) {
                run->SetFlag1B(1);
                next->SetFlag1A(1);
                AppendBlob(out, MakeBlob(run, next));
            }
        }
        if (run->mPrev >= 0) {
            WallDiffusionRun *const prev = RunAt(&mRuns, run->mPrev);
            if (!run->mFlag1A && !(prev->mFlag19 && run->mFlag19) && WallAnglesQuarterApart(prev->mAngle, run->mAngle)) {
                prev->SetFlag1B(1);
                run->SetFlag1A(1);
                AppendBlob(out, MakeBlob(prev, run));
            }
        }
    }
}

// ---- row LD2l (lane 261110j): the blends --------------------------------------------------------------------------------

namespace {

constexpr float kWeightHalf = 0.5f;   // 0xd9c694
constexpr double kHalfStep = 0.5;     // 0xda1870
constexpr float kFloorOffset = 0.125f; // PointAlong's offset, pushed as 0x3e000000

void *SceneGraphSystem() { return OriginalFunction<void *(__cdecl *)(), 0x20defb>()(); }

// BlendLightMap 0x40f390 and BlendFloors 0x40f1c0 alike: `sample(out, position)` is the light map's slot 16 or the floor
// colours' slot 23. Both write exactly the 12-byte colour at out (the image's bodies store out+0, +4, +8 and nothing
// else: GetFloorLight 0x2c7880 / 0x2c788a or 0x2c78c4 / 0x2c78ce, GetFloorColor's 0x408e30 at 0x409045-0x40904e or
// 0x40906c / 0x409070); the callers' frames hold 12 bytes there with the step's double right after. The position is the
// three words they read: x, y and the level word.
template <class Sample> bool BlendHalves(WallDiffusionRun *run, float weight, Sample &&sample)
{
    const uint32_t count = static_cast<uint32_t>(run->SegmentCount());
    // The side count as unsigned (cvtdq2pd plus the 2^32 table by the sign bit), to float, then 1 / it (divss).
    const float step = DivSs(1.0f, static_cast<float>(static_cast<double>(2u * count)));
    // ORIGINAL-BUG: read before the count is tested (0x40f213 / 0x40f41c): an empty run's first segment unchecked.
    const uint32_t level = run->FrontLocationWord2();
    bool changed = false;
    for (uint32_t i = 0; i < static_cast<uint32_t>(run->SegmentCount()); ++i) {
        if (run->BelowAt(i)->IsValid()) continue;
        const float scale = MulSs(weight, kWeightHalf);
        const double stepWide = WidenSs(step);
        for (int32_t half = 0; half < 2; ++half) {
            const double along = static_cast<double>(static_cast<int32_t>(2u * i) + half) + kHalfStep;
            float point[2];
            run->PointAlong(point, NarrowSd(MulSd(along, stepWide)), kFloorOffset);
            float position[3] = {point[0], point[1], 0.0f};
            std::memcpy(&position[2], &level, sizeof(level));
            float color[3];
            sample(color, position);
            for (float &c : color) c = MulSs(c, scale);
            changed |= run->mSegments.data()[i].MergeMax(color, half);
        }
    }
    return changed;
}

} // namespace

__declspec(noinline) bool WallDiffusionRun::BlendLightMap(float weight)
{
    cILightMapManager *const lights =
        static_cast<cILightMapManager *>(RawSlot<48, void *>(SceneGraphSystem())); // receiver: nTSSG::cTSSGSystem
    if (lights) lights->AddRef();
    // ORIGINAL-BUG: a null light map is not checked before its slot 16 (0x40f4d7): a run with a segment to blend crashes.
    const bool changed =
        BlendHalves(this, weight, [lights](float *out, const float *position) { lights->GetFloorLight(out, position); });
    if (lights) lights->Release();
    return changed;
}

__declspec(noinline) bool WallDiffusionRun::BlendFloors(cIDiffusionManager *floors, float weight)
{
    return BlendHalves(this, weight,
                       [floors](float *out, const float *position) { floors->GetFloorColor(out, position); });
}

__declspec(noinline) void cWallDiffusionManager::Blend(float bright, float dark)
{
    for (uint32_t r = 0; r < static_cast<uint32_t>(mRuns.size()); ++r) {
        WallDiffusionRun *const run = mRuns.data() + r;
        const float weight = run->mFlag19 ? bright : dark;
        if (weight != 0.0f && run->BlendLightMap(weight)) mMarkedRuns.insert(static_cast<int32_t>(r));
    }
}

__declspec(noinline) void cWallDiffusionManager::BlendUnlit(float bright, float dark, cIDiffusionManager *floors)
{
    for (uint32_t r = 0; r < static_cast<uint32_t>(mRuns.size()); ++r) {
        WallDiffusionRun *const run = mRuns.data() + r;
        const float weight = run->mFlag19 ? bright : dark;
        if (weight != 0.0f && run->BlendFloors(floors, weight)) mMarkedRuns.insert(static_cast<int32_t>(r));
    }
}

// ---- row LD2m (lane 261110k): the segments' colours into the wall images ----------------------------------------------

uint32_t __cdecl PackLightColor(const float *rgb) { return PackWallColor(rgb); }

namespace {

constexpr float kChannelScale = 255.0f; // 0xd9c79c
constexpr float kBlockScale = 0.0625f;  // 0xda7698: the 4 x 8 block's scale factor
constexpr float kStartU = 0.5f;         // 0xd9c694 (also the rounding's half)
constexpr float kStartV = 6.5f;         // 0xdc2578
constexpr float kSpanU = 2.0f;          // 0xd9c69c
constexpr float kSpanV = 6.0f;          // 0xda7868
constexpr double kTailBase = 0.0625;    // 0xdc2550
constexpr double kTailThreshold = 0.4;  // 0xdc1f40
constexpr double kTailOne = 1.0;        // 0xda1878
constexpr uint32_t kCatalogShaded = 3;  // the partitions' catalog id whose tail rows are masked off

// The six words of a wall piece's CellToSegment, as the local set holds them (nodes 0x28 bytes from the allocator
// service, the head freed through 0x197100; cLightingManager::SegmentLess 0x257cf0 the order).
struct LitSegment {
    float v[6];
};
static_assert(sizeof(LitSegment) == 24, "LitSegment");
struct LitSegmentOrder {
    bool operator()(const LitSegment &a, const LitSegment &b) const { return cLightingManager::SegmentLess(a.v, b.v); }
};
using LitSegmentSet = std::set<LitSegment, LitSegmentOrder, GZServiceAllocator<LitSegment>>;

// x rounded to the integer below and above, each by the magic sum of x -/+ 0.5 (nearest even), moved back by one when the
// neighbour lands exactly on x (ucomiss, ordered equal: an x at an integer gives x itself both ways).
int32_t FloorAt(float x)
{
    int32_t lo = MagicToInt(AddSs(SubSs(x, kStartU), kRoundMagic));
    const int32_t next = static_cast<int32_t>(static_cast<uint32_t>(lo) + 1u);
    if (static_cast<float>(next) == x) lo = next;
    return lo;
}
int32_t CeilAt(float x)
{
    int32_t hi = MagicToInt(AddSs(AddSs(x, kStartU), kRoundMagic));
    const int32_t before = static_cast<int32_t>(static_cast<uint32_t>(hi) - 1u);
    if (static_cast<float>(before) == x) hi = before;
    return hi;
}

// mCurrent's word `index` (unchecked: 32-bit address arithmetic, as the original's).
uint32_t CurrentWord(const WallDiffusionSegment *segment, int32_t index)
{
    uint32_t word;
    std::memcpy(&word,
                reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(segment->mCurrent) +
                                               static_cast<uintptr_t>(static_cast<uint32_t>(index) * 4u)),
                sizeof(word));
    return word;
}
float ChannelOf(uint32_t word, uint32_t shift)
{
    return DivSs(static_cast<float>(static_cast<int32_t>((word >> shift) & 0xffu)), kChannelScale);
}

// Pixel `x` of the row at `row` (32-bit address arithmetic).
uint32_t *PixelAt(uintptr_t row, uint32_t x) { return reinterpret_cast<uint32_t *>(row + static_cast<uintptr_t>(x * 4u)); }

// One pixel of the fill: mCurrent's bilinear at column u, row v (word row + column * 8), less the colour, times scale.
void Bilinear(const WallDiffusionSegment *segment, float u, float v, const float *color, float scale, float *out)
{
    const int32_t ulo = FloorAt(u);
    const int32_t uhi = CeilAt(u);
    const int32_t vlo = FloorAt(v);
    const int32_t vhi = CeilAt(v);
    const float fu = SubSs(u, static_cast<float>(ulo));
    const float fv = SubSs(v, static_cast<float>(vlo));
    auto at = [segment](int32_t row, int32_t column) {
        return CurrentWord(segment, static_cast<int32_t>(static_cast<uint32_t>(row) + static_cast<uint32_t>(column) * 8u));
    };
    const uint32_t a = at(vhi, uhi);
    const uint32_t b = at(vhi, ulo);
    const uint32_t c = at(vlo, uhi);
    const uint32_t d = at(vlo, ulo);
    for (uint32_t k = 0; k < 3; ++k) {
        const uint32_t shift = 16u - 8u * k;
        const float dk = ChannelOf(d, shift);
        const float bk = ChannelOf(b, shift);
        const float lo = AddSs(MulSs(SubSs(ChannelOf(c, shift), dk), fu), dk);
        const float hi = AddSs(MulSs(SubSs(ChannelOf(a, shift), bk), fu), bk);
        out[k] = MulSs(SubSs(AddSs(MulSs(SubSs(hi, lo), fv), lo), color[k]), scale);
    }
}

// `rows` rows (none when not above 0) from `row` on, each pixel left..right (unsigned) the bilinear: u from 0.5 by
// uStep, v from 6.5 down by vStep a row. Returns the row after the last.
uintptr_t BilinearFill(const WallDiffusionSegment *segment, uintptr_t row, uint32_t pitchBytes, int32_t rows,
                       uint32_t left, uint32_t right, float uStep, float vStep, const float *color, float scale)
{
    float v = kStartV;
    for (; rows > 0; --rows) {
        float u = kStartU;
        for (uint32_t x = left; x < right; ++x) {
            float rgb[3];
            Bilinear(segment, u, v, color, scale, rgb);
            *PixelAt(row, x) = PackLightColor(rgb);
            u = AddSs(u, uStep);
        }
        v = SubSs(v, vStep);
        row += pitchBytes;
    }
    return row;
}

// The MMX scaling of one word (punpcklbw with zero, psubw the packed colour's bytes, psllw 4, pmulhw by the factor word,
// packuswb), a byte at a time: ((b - p) << 4, in 16 bits) times the word, high half, saturated to 0..255; alpha 0xff.
uint32_t ScaleWord(uint32_t word, uint32_t packed, int16_t factor)
{
    uint32_t out = 0;
    for (uint32_t shift = 0; shift < 32; shift += 8) {
        const int32_t diff = static_cast<int32_t>((word >> shift) & 0xffu) - static_cast<int32_t>((packed >> shift) & 0xffu);
        const int16_t wide = static_cast<int16_t>(static_cast<uint16_t>(static_cast<uint32_t>(diff) << 4));
        int32_t scaled = (static_cast<int32_t>(wide) * factor) >> 16;
        if (scaled < 0) scaled = 0;
        if (scaled > 255) scaled = 255;
        out |= static_cast<uint32_t>(scaled) << shift;
    }
    return out | 0xff000000u;
}

int16_t FactorWord(float f)
{
    Fixed16 fixed;
    fixed.SetFromFloat(f);
    return static_cast<int16_t>(fixed.mValue & 0xffff);
}

} // namespace

__declspec(noinline) void cWallDiffusionManager::ApplyLights(cILightMapManager *lights, float scale, uint8_t changedOnly)
{
    float outside[3];
    for (int k = 0; k < 3; ++k) outside[k] = MulSs(mOutsideAmbientColor[k], mOutsideWallAmbient);
    mChanges2.clear();
    mChanges.clear();
    void *const partitions = Partitions();
    LitSegmentSet seen;

    // The image cache: none at first (atlas -1, base and pitch 0).
    // ORIGINAL-BUG: an atlas of -1 from GetWallRect matches the empty cache: the rows are written from address 0.
    uint32_t cachedAtlas = 0xffffffffu;
    uintptr_t base = 0;
    uint32_t pitchWords = 0; // the negated words per row
    // GetWallRect's outs (the original's are its frame's, unset at first, kept from one segment to the next).
    int32_t left = 0, top = 0, right = 0, bottom = 0;
    uint32_t atlas = 0;

    for (uint32_t r = 0; r < static_cast<uint32_t>(mRuns.size()); ++r) {
        WallDiffusionRun &run = *RunAt(&mRuns, static_cast<int32_t>(r));
        float color[3] = {0.0f, 0.0f, 0.0f}; // the static 0x10da730 (zero, never written)
        const uint16_t level = static_cast<uint16_t>(run.FrontCellQuery());
        if (mOutsideAmbientEnabled != 0 && level == 0) {
            for (int k = 0; k < 3; ++k) color[k] = outside[k];
        }
        std::vector<WallDiffusionChange> *const list =
            static_cast<uint16_t>(run.FrontCellQuery()) != 0 ? &mChanges : &mChanges2;

        for (uint32_t s = 0; s < static_cast<uint32_t>(run.SegmentCount()); ++s) {
            WallDiffusionSegment *const segment = SegmentAt(run, static_cast<int32_t>(s));
            LitSegment key;
            worldrect::CellToSegment(key.v, reinterpret_cast<const int32_t *>(segment->mLocation));
            if (changedOnly) {
                if (std::memcmp(segment->mPrevious, segment->mCurrent, sizeof(segment->mCurrent)) == 0) continue;
                WallDiffusionChange change;
                std::memcpy(&change, key.v, sizeof(change));
                list->push_back(change);
            }
            const auto found = seen.lower_bound(key);
            if (found != seen.end() && !LitSegmentOrder()(key, *found)) continue;
            seen.insert(found, key);

            if (!lights->GetWallRect(reinterpret_cast<const WallKey *>(key.v), &left, &top, &right, &bottom, &atlas))
                continue;
            if (atlas != cachedAtlas) {
                cachedAtlas = atlas;
                // ORIGINAL-BUG: the image is not checked (0x40e158): a null one crashes in its slot 9.
                nGZSceneGraph::cIImageData *const image = lights->GetImage(0, atlas);
                uint8_t *const data = image->LevelData(0);
                const uint32_t words = static_cast<uint32_t>(image->LevelPitch(0)) >> 2;
                const uint32_t height = static_cast<uint32_t>(image->LevelHeight(0));
                base = reinterpret_cast<uintptr_t>(data) + static_cast<uintptr_t>((height - 1u) * words * 4u);
                pitchWords = 0u - words;
            }
            const uint32_t pitchBytes = pitchWords * 4u;
            const int32_t w = static_cast<int32_t>(static_cast<uint32_t>(right) - static_cast<uint32_t>(left));
            const int32_t h = static_cast<int32_t>(static_cast<uint32_t>(bottom) - static_cast<uint32_t>(top));
            uintptr_t row = base + static_cast<uintptr_t>(static_cast<uint32_t>(top) * pitchWords * 4u);
            const int32_t w3 = static_cast<int32_t>(static_cast<uint32_t>(w) * 3u);

            if (w == 4 && h == 8) {
                const int16_t factor = FactorWord(MulSs(scale, kBlockScale));
                const uint32_t packed = PackWallColor(color);
                int32_t word = 7; // mCurrent's column 0 of rect row 0: word (x - left) * 8 + 7 - (y - top)
                for (uint32_t y = static_cast<uint32_t>(top); y < static_cast<uint32_t>(bottom); ++y, --word) {
                    int32_t column = word;
                    for (uint32_t x = static_cast<uint32_t>(left); x < static_cast<uint32_t>(right); ++x, column += 8)
                        *PixelAt(row, x) = ScaleWord(CurrentWord(segment, column), packed, factor);
                    row += pitchBytes;
                }
            } else if (w3 == h) {
                const float uStep = DivSs(kSpanU, static_cast<float>(static_cast<int32_t>(static_cast<uint32_t>(w) - 1u)));
                const float vStep = DivSs(kSpanV, static_cast<float>(static_cast<int32_t>(static_cast<uint32_t>(h) - 1u)));
                BilinearFill(segment, row, pitchBytes, h, static_cast<uint32_t>(left), static_cast<uint32_t>(right),
                             uStep, vStep, color, scale);
            } else {
                // ORIGINAL-BUG: a signed idiv by w (0x40e757): a rect of width 0 (or -1 with h INT_MIN) faults.
                const int32_t rest = h % w;
                if (static_cast<int32_t>(static_cast<uint32_t>(h) - static_cast<uint32_t>(rest)) != h) continue;
                // receiver: the partitions service (0x20de97's object): the catalog id of the piece.
                const uint32_t kind = RawSlot<50, uint32_t>(partitions, key.v);
                const float uStep = DivSs(kSpanU, static_cast<float>(static_cast<int32_t>(static_cast<uint32_t>(w) - 1u)));
                const float vStep = DivSs(kSpanV, static_cast<float>(static_cast<int32_t>(static_cast<uint32_t>(w3) - 1u)));
                const int32_t rows = h < w3 ? h : w3;
                row = BilinearFill(segment, row, pitchBytes, rows, static_cast<uint32_t>(left),
                                   static_cast<uint32_t>(right), uStep, vStep, color, scale);
                const int16_t factor =
                    FactorWord(NarrowSd(kTailBase / (MulSd(WidenSs(mThreshold), kTailThreshold) + kTailOne)));
                uint32_t mask = 0xffffffffu;
                if (level != 0 && kind == kCatalogShaded) {
                    if (changedOnly) continue;
                    mask = 0;
                }
                if (w3 < h) {
                    uintptr_t previous = row - pitchBytes;
                    uint32_t count = static_cast<uint32_t>(h) - static_cast<uint32_t>(w3);
                    do {
                        for (uint32_t x = static_cast<uint32_t>(left); x < static_cast<uint32_t>(right); ++x)
                            *PixelAt(row, x) = ScaleWord(*PixelAt(previous, x), 0, factor) & mask;
                        row += pitchBytes;
                        previous += pitchBytes;
                    } while (--count != 0);
                }
            }
        }
    }
}

__declspec(noinline) void cWallDiffusionManager::ApplyAllLights(cILightMapManager *lights, float scale)
{
    ApplyLights(lights, scale, 0);
}

__declspec(noinline) void cWallDiffusionManager::ApplyChangedLights(cILightMapManager *lights, float scale)
{
    ApplyLights(lights, scale, 1);
}

} // namespace nTSSG
