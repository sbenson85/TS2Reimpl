#include "ts/cTSEffectCurve.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/CommandParser.h" // cIGZExceptionBase
#include "rz/cRZString.h"

#include <cfloat>
#include <cmath>
#include <new>
#include <utility>

namespace {

constexpr float kTwoPi = 6.28318548f; // 0x40c90fdb, also the constant at 0xda20f0

// The stream's getters and setters (cIGZIStream / cIGZOStream slots 9 and 12).
void GetUint32(void *stream, uint32_t *value)
{
    VirtualSlot<bool(__thiscall *)(void *, uint32_t *)>(stream, 9)(stream, value);
}
void GetFloat(void *stream, float *value)
{
    VirtualSlot<bool(__thiscall *)(void *, float *)>(stream, 12)(stream, value);
}
void PutUint32(void *stream, uint32_t value)
{
    VirtualSlot<bool(__thiscall *)(void *, uint32_t)>(stream, 9)(stream, value);
}
void PutFloat(void *stream, float value)
{
    VirtualSlot<bool(__thiscall *)(void *, float)>(stream, 12)(stream, value);
}

} // namespace

// 0xbdc8f0
cTSEffectCurve::cTSEffectCurve() : mKind(kList)
{
    Reset();
}

// 0xbdc880
cTSEffectCurve::cTSEffectCurve(float value) : mKind(kList)
{
    SetConstant(value);
}

// 0xbdcb80
void cTSEffectCurve::Reset()
{
    if (mKind != kWave) {
        SetConstant(0.0f);
        return;
    }
    mValues.resize(4, 0.0f);
    mValues[0] = 1.0f;
    mValues[1] = kTwoPi;
}

// 0xbdcd10. The size test is on the bytes rounded down to whole values.
void cTSEffectCurve::SetConstant(float value)
{
    mKind = kList;
    if (mValues.size() != 1) mValues.resize(1);
    mValues[0] = value;
}

// 0xbdcd40
void cTSEffectCurve::SetKind(uint32_t kind)
{
    if (mKind == kind) return;
    mKind = kind;
    Reset();
}

// 0xbdcd60
void cTSEffectCurve::Simplify()
{
    if (mKind != kList) return;
    const size_t count = mValues.size();
    if (count <= 1) return;
    const float first = mValues[0];
    for (size_t i = 1; i < count; ++i)
        if (mValues[i] != first) return;
    SetConstant(first);
}

// 0xbdcbd0
void cTSEffectCurve::SetValues(const std::vector<float> &values)
{
    OriginalFunction<void(__thiscall *)(cTSEffectCurve *, const std::vector<float> *), 0xbdcdb0>()(this, &values);
    Reset();
    if (mKind == kWave) {
        size_t count = values.size();
        if (count > 4) count = 4;
        for (size_t i = 0; i < count; ++i) mValues[i] = values[i];
        if (count >= 2) mValues[1] = mValues[1] * kTwoPi;
    } else if (mKind == kList && !values.empty() && &mValues != &values) {
        mValues.assign(values.begin(), values.end());
    }
    Simplify();
}

// 0xbdca50
float cTSEffectCurve::GetMax() const
{
    float result = -FLT_MAX;
    if (mKind == kWave) return std::fabs(mValues[0]) + mValues[3];
    if (mKind != kList) return result;
    for (const float v : mValues) result = v > result ? v : result;
    return result;
}

// 0xbdcac0. The word is read over a temporary that starts as the stream pointer (the original reads into its own stream
// argument's stack slot): a read that fails without writing leaves that.
void cTSEffectCurve::Read(void *stream)
{
    uint32_t word = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stream));
    GetUint32(stream, &word);
    uint32_t count = word & ~kKindMask;
    mKind = word & kKindMask;
    if (mKind == kWave) {
        mValues.resize(4, 0.0f);
        if (count > 4) count = 4;
        for (uint32_t i = 0; i < count; ++i) GetFloat(stream, &mValues[i]);
        return;
    }
    if (mKind == kList) {
        if (count != 0) {
            mValues.resize(count);
            for (uint32_t i = 0; i < count; ++i) GetFloat(stream, &mValues[i]);
            return;
        }
    } else {
        mKind = kList;
    }
    Reset();
}

// 0xbdce40
void cTSEffectCurve::Write(void *stream) const
{
    const uint32_t count = static_cast<uint32_t>(mValues.size());
    PutUint32(stream, mKind | count);
    for (uint32_t i = 0; i < count; ++i) PutFloat(stream, mValues[i]);
}

// ---- lane IX ----

namespace {

// What the curve throws (0x24 bytes): the game's cTSCurveError (base cIGZExceptionBase; vtable 0xe8e88c, its one slot the
// message) with the message in a cRZString at +4. Named and laid out as the game's, so the game's handlers that catch it by
// type (the match is by decorated name) catch ours; the vtable is a plain field (src/gz/CommandParser.h, cError).
class cTSCurveError : public cIGZExceptionBase {
public:
    cRZString mMessage; // +4
};
static_assert(sizeof(cTSCurveError) == 0x24);

// The game's constructor (0xbdc6f0, cdecl: the object, a format and its arguments) builds it from the message (one of the
// game's texts); it is then moved into the type the compiler throws.
[[noreturn]] void ThrowCurveError(uint32_t messageRva)
{
    alignas(cTSCurveError) unsigned char raw[sizeof(cTSCurveError)];
    OriginalFunction<void *(__cdecl *)(void *, const char *, ...), 0xbdc6f0>()(raw, GameAddress<char>(messageRva));
    cTSCurveError error(std::move(*reinterpret_cast<cTSCurveError *>(raw)));
    reinterpret_cast<cTSCurveError *>(raw)->mMessage.mString.~basic_string();
    throw error;
}

} // namespace

// 0xbdc950. The list's size is taken again on each step (the values are not resized here).
cTSEffectCurve &cTSEffectCurve::Scale(float factor)
{
    if (mKind == kWave) {
        if (factor == 0.0f) ThrowCurveError(0xe8e890); // "Invalid multiplication of sine by 0!"
        mValues[0] = factor * mValues[0];
    } else if (mKind == kList) {
        for (size_t i = 0; i < mValues.size(); ++i) mValues[i] = factor * mValues[i];
    }
    return *this;
}

// 0xbdc9f0
cTSEffectCurve &cTSEffectCurve::Divide(float divisor)
{
    if (divisor == 0.0f) ThrowCurveError(0xe8e8b8); // "Invalid division by 0!"
    return Scale(1.0f / divisor);
}

void __fastcall cTSEffectCurveAdapters::Destroy(cTSEffectCurve *self, void *)
{
    self->~cTSEffectCurve();
}

cTSEffectCurve *__fastcall cTSEffectCurveAdapters::Assign(cTSEffectCurve *self, void *, const cTSEffectCurve *from)
{
    *self = *from;
    return self;
}

cTSEffectCurve *__fastcall cTSEffectCurveAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cTSEffectCurve;
}

cTSEffectCurve *__fastcall cTSEffectCurveAdapters::ConstructValue(void *memory, void *, float value)
{
    return ::new (memory) cTSEffectCurve(value);
}
