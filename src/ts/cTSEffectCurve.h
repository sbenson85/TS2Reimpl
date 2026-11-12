#pragma once
// The effects code's value curve (lane IW; no RTTI, the name is ours): 0x10 bytes, a kind in the top byte of the first word
// and the values. Two kinds: a list of values (kList; one value is a constant) and a wave (kWave; four values: amplitude,
// angular frequency, a third value, offset). Every particle, decal, camera and screen description holds a few (size, alpha,
// rotation, the camera's heading, the screen's strength, ...); the effects parser fills them (0xbd1a50, not ours) and the
// description serializers read and write them. RVAs 1.18.0.187, all /O2.
#include "gz/CommandParser.h" // cIGZExceptionBase
#include "rz/cRZString.h"

#include <cstdint>
#include <vector>

// What the curve throws (0x24 bytes): the game's cTSCurveError (base cIGZExceptionBase; vtable 0xe8e88c, its one slot the
// message) with the message in a cRZString at +4. Named and laid out as the game's, so the game's handlers that catch it by
// type (the match is by decorated name) catch ours; the vtable is a plain field (src/gz/CommandParser.h, cError).
class cTSCurveError : public cIGZExceptionBase {
public:
    static constexpr uint32_t kVTable = 0xe8e88c;
    static constexpr uint32_t kFormatBufferSize = 0x800;

    __declspec(noinline) cTSCurveError(const char *format, ...); // 0xbdc6f0 (cdecl, as any variadic member)
    __declspec(noinline) cTSCurveError(const cTSCurveError &other); // 0xbdc760
    cTSCurveError &operator=(const cTSCurveError &) = delete;

    cRZString mMessage; // +4
};
static_assert(sizeof(cTSCurveError) == 0x24);

class cTSEffectCurve {
public:
    enum : uint32_t {
        kList = 0,           // the values in order (one: a constant)
        kWave = 0x81000000u, // amplitude, angular frequency (the parsed frequency times 2 pi), a third value, offset
        kKindMask = 0xff000000u,
    };

    cTSEffectCurve();                     // 0xbdc8f0: a list with the one value 0
    explicit cTSEffectCurve(float value); // 0xbdc880: a list with the one value

    // 0xbdcb80: the kind's defaults (a wave: four values, the first two 1 and 2 pi, the others kept or 0; anything else: the
    // constant 0, which also makes it a list).
    void Reset();
    // 0xbdcd10: a list with the one value. Out of line (as SetValues and the adapters): the parser commands' tests stand
    // in for it.
    __declspec(noinline) void SetConstant(float value);
    // 0xbdcd40: a new kind starts from its defaults; the same kind is left alone.
    void SetKind(uint32_t kind);
    // 0xbdcd60: a list of equal values (two or more) becomes their constant.
    void Simplify();
    // 0xbdcdb0: a wave's amplitude and frequency among the values must be non-zero (throws cTSCurveError).
    __declspec(noinline) void CheckSinCoefficients(const std::vector<float> &values) const;
    // 0xbdcbd0: the parsed values (checked by CheckSinCoefficients first); a wave
    // takes up to four, the frequency times 2 pi; a list takes them all.
    __declspec(noinline) void SetValues(const std::vector<float> &values);
    // 0xbdca50: the largest value a list holds (-FLT_MAX for none), a wave's |amplitude| + offset; other kinds -FLT_MAX.
    float GetMax() const;
    // 0xbdcac0 / 0xbdce40: the kind and count in one word (the count in the low 24 bits), then the values as floats.
    void Read(void *stream);
    void Write(void *stream) const;
    // 0xbdc950 (lane IX): a list's values times `factor`, a wave's amplitude (a factor of 0 throws the game's cTSCurveError);
    // other kinds unchanged.
    __declspec(noinline) cTSEffectCurve &Scale(float factor);
    // 0xbdc9f0 (lane IX): Scale by 1 / `divisor` (0 throws).
    __declspec(noinline) cTSEffectCurve &Divide(float divisor);

    uint32_t mKind;             // +0x00
    std::vector<float> mValues; // +0x04
};
static_assert(sizeof(cTSEffectCurve) == 0x10);

// The RVA hooks' constructors (this in ecx), and lane IX's: the destructor 0xbe1b80 and copy assignment 0xbe21f0 (the
// compiler's, out of line in the original).
struct cTSEffectCurveAdapters {
    __declspec(noinline) static cTSEffectCurve *__fastcall Construct(void *memory, void *);
    __declspec(noinline) static cTSEffectCurve *__fastcall ConstructValue(void *memory, void *, float value);
    __declspec(noinline) static void __fastcall Destroy(cTSEffectCurve *self, void *);
    __declspec(noinline) static cTSEffectCurve *__fastcall Assign(cTSEffectCurve *self, void *, const cTSEffectCurve *from);
};
