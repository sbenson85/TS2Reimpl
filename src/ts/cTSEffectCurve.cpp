#include "ts/cTSEffectCurve.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"

#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>
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
    CheckSinCoefficients(values);
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

// 0xbdc6f0. The original formats into a static 2 KB buffer (0x10fc930) nothing else reads; here it is the stack's, a byte
// longer, so that a text that fills it (no terminator from the legacy formatter) ends as it does over the image's zeros.
cTSCurveError::cTSCurveError(const char *format, ...)
{
    mVTable = GameAddress<void>(kVTable);
    mMessage.mVTable = cRZString::VTable();
    mMessage.mRefCount = 0;
    char buffer[kFormatBufferSize + 1];
    buffer[kFormatBufferSize] = 0;
    va_list args;
    va_start(args, format);
    _vsnprintf_l(buffer, kFormatBufferSize, format, nullptr, args);
    va_end(args);
    mMessage.mString.assign(buffer);
}

// 0xbdc760: the handlers' copy; a new string with no references.
cTSCurveError::cTSCurveError(const cTSCurveError &other) : mMessage{cRZString::VTable(), other.mMessage.mString, 0}
{
    mVTable = GameAddress<void>(kVTable);
}

// 0xbdcdb0
void cTSEffectCurve::CheckSinCoefficients(const std::vector<float> &values) const
{
    if (mKind != kWave) return;
    const size_t count = values.size();
    if (count != 0 && values[0] == 0.0f) throw cTSCurveError("Invalid sin coefficient! First coefficient must be non-zero");
    if (count > 1 && values[1] == 0.0f) throw cTSCurveError("Invalid sin coefficient! Second coefficient must be non-zero");
}

// 0xbdc950. The list's size is taken again on each step (the values are not resized here).
cTSEffectCurve &cTSEffectCurve::Scale(float factor)
{
    if (mKind == kWave) {
        if (factor == 0.0f) throw cTSCurveError("Invalid multiplication of sine by 0!");
        mValues[0] = factor * mValues[0];
    } else if (mKind == kList) {
        for (size_t i = 0; i < mValues.size(); ++i) mValues[i] = factor * mValues[i];
    }
    return *this;
}

// 0xbdc9f0
cTSEffectCurve &cTSEffectCurve::Divide(float divisor)
{
    if (divisor == 0.0f) throw cTSCurveError("Invalid division by 0!");
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
