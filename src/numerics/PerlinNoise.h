#pragma once
// nGZNumerics::cPerlinNoise as a whole class (lane PN, scenegraph G0b step 6): gradient noise in 1, 2 or 3 dimensions from a
// seeded table, and the fractal (summed octaves) forms. Made by the nGZNumerics COM director's factory 0x98c450 (class
// registered by the director's constructor 0x98be40), used through the interface by the roof manager (0x3130d0), the scene
// DB animation system (0x9db240) and 0xaec6b0 (interface id 0xbd33210e).
//
// The game's object (0x1924 bytes; RTTI bases nGZNumerics::cIPerlinNoise, cIGZUnknown):
//   +0x00 vptr (vtable 0xe5d0dc: one slot, the deleting destructor 0x98c3b0)
//   +0x04 vbptr (vbtable 0xe5d130: cIGZUnknown at +0x1914, cIPerlinNoise at +0x191c)
//   +0x08 mCount (the reference count, 0 at first)    +0x0c mDimensions (set by Init)
//   +0x10 mGradient1[256] (floats)       +0x410 mGradient2[256][2] (cos, sin)    +0xc10 mGradient3[256][3] (unit)
//   +0x1810 mPermutation[256] (bytes)
//   +0x1910 vtordisp, +0x1914 cIGZUnknown (vtable 0xe5d0e4: QueryInterface 0x98d330, AddRef 0x98c3e0, Release 0x98d3ab)
//   +0x1918 vtordisp, +0x191c cIPerlinNoise (vtable 0xe5d0f4: 15 slots), +0x1920 its own vbptr (vbtable 0xe5d13c)
// Ours is our compiler's class with the same bases: cPerlinNoise : virtual cIPerlinNoise : virtual cIGZUnknown, which
// gives the same layout (docs/standalone-shape.md, "Virtual bases and vtordisp"); the test checks the offsets, the
// vtordisps and the vbtables against the game's object. The slot bodies are the game's thunk + body pairs (a thunk is
// `sub ecx, [ecx-4]` and a jump; ours are the compiler's vtordisp thunks). It is a leaf: the factory is the one hook.
//
// Float code: the cell functions are SSE float arithmetic in the original and here; the fractal sums add `result * amplitude`
// on the x87 (the result arrives in st0), which x87::Rounded models at the thread's precision.
#include "gz/cIGZUnknown.h"

#include <cstdint>

namespace nGZNumerics {

// nGZNumerics::cIPerlinNoise (interface id 0xbd33210e). Its QueryInterface / AddRef / Release are the game's pure
// re-declarations of cIGZUnknown's (the vtable 0xe5d0cc the constructor stores on the way).
class cIPerlinNoise : public virtual cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0xbd33210e;

    bool QueryInterface(uint32_t iid, void **out) override = 0;
    uint32_t AddRef() override = 0;
    uint32_t Release() override = 0;

    virtual void Init(uint32_t dimensions, uint32_t seed) = 0;                                   // 0: 0x98c4a0
    virtual bool IsValidDimension(uint32_t dimension) = 0;                                       // 1: 0x98c6b0
    virtual float Noise1Cell(uint32_t ix, float fx) = 0;                                         // 2: 0x98c6d0
    virtual float Noise1(float x) = 0;                                                           // 3: 0x98c740
    virtual float Noise2Cell(uint32_t ix, float fx, int32_t iy, float fy) = 0;                   // 4: 0x98c7a0
    virtual float Noise2(float x, float y) = 0;                                                  // 5: 0x98c8e0
    virtual float Noise3Cell(uint32_t ix, float fx, int32_t iy, float fy, int32_t iz, float fz) = 0; // 6: 0x98c980
    virtual float Noise3(float x, float y, float z) = 0;                                         // 7: 0x98cd30
    virtual float Fractal1Cell(int32_t ix, float fx, float amplitude, float gain, int32_t octaves) = 0; // 8: 0x98ce00
    virtual float Fractal1(float x, float amplitude, float gain, int32_t octaves) = 0;           // 9: 0x98cec0
    virtual float Fractal2Cell(int32_t ix, float fx, int32_t iy, float fy, float amplitude, float gain,
                               int32_t octaves) = 0;                                             // 10: 0x98cf40
    virtual float Fractal2(float x, float y, float amplitude, float gain, int32_t octaves) = 0;  // 11: 0x98d050
    virtual float Fractal3Cell(int32_t ix, float fx, int32_t iy, float fy, int32_t iz, float fz, float amplitude,
                               float gain, int32_t octaves) = 0;                                 // 12: 0x98d100
    virtual float Fractal3(float x, float y, float z, float amplitude, float gain, int32_t octaves) = 0; // 13: 0x98d260
    // 14: wrapper 0x98c400 over the static helper 0x98d3f0 (no `this`): the octaves that bring `amplitude` (times `gain` each
    // octave) under `minAmplitude`, or that fit `scale` down to `limit`; the smaller, at least 1, rounded.
    virtual int32_t OctaveCount(float limit, float amplitude, float gain, float scale, float minAmplitude) = 0;
};

#pragma warning(push)
#pragma warning(disable : 4250) // via dominance: our bodies are the final overriders of the virtual bases' slots
class cPerlinNoise final : public virtual cIPerlinNoise {
public:
    // 0x98c450 (__cdecl): the director's factory: a new object (count 0), as its cIGZUnknown face (the director asks it for the interface).
    static cIGZUnknown *__cdecl Create();

    cPerlinNoise() = default; // 0x98c2c0 sets the count and dimensions to 0 and the vtables; the tables are left as they are
    virtual ~cPerlinNoise(); // 0x98c360 (deleting 0x98c3b0)

    bool QueryInterface(uint32_t iid, void **out) override; // 0x98d330
    uint32_t AddRef() override;                             // 0x98c3e0
    uint32_t Release() override;                            // 0x98d3ab: the last one deletes

    void Init(uint32_t dimensions, uint32_t seed) override;
    bool IsValidDimension(uint32_t dimension) override;
    float Noise1Cell(uint32_t ix, float fx) override;
    float Noise1(float x) override;
    float Noise2Cell(uint32_t ix, float fx, int32_t iy, float fy) override;
    float Noise2(float x, float y) override;
    float Noise3Cell(uint32_t ix, float fx, int32_t iy, float fy, int32_t iz, float fz) override;
    float Noise3(float x, float y, float z) override;
    float Fractal1Cell(int32_t ix, float fx, float amplitude, float gain, int32_t octaves) override;
    float Fractal1(float x, float amplitude, float gain, int32_t octaves) override;
    float Fractal2Cell(int32_t ix, float fx, int32_t iy, float fy, float amplitude, float gain, int32_t octaves) override;
    float Fractal2(float x, float y, float amplitude, float gain, int32_t octaves) override;
    float Fractal3Cell(int32_t ix, float fx, int32_t iy, float fy, int32_t iz, float fz, float amplitude, float gain,
                       int32_t octaves) override;
    float Fractal3(float x, float y, float z, float amplitude, float gain, int32_t octaves) override;
    int32_t OctaveCount(float limit, float amplitude, float gain, float scale, float minAmplitude) override;

    // The static helper 0x98d3f0 (cdecl, five floats): slot 14's body.
    static int32_t OctaveCountOf(float limit, float amplitude, float gain, float scale, float minAmplitude);

    uint32_t mCount = 0;      // +0x08
    uint32_t mDimensions = 0; // +0x0c
    float mGradient1[256];    // +0x10
    float mGradient2[256][2]; // +0x410
    float mGradient3[256][3]; // +0xc10
    uint8_t mPermutation[256]; // +0x1810
};
#pragma warning(pop)

} // namespace nGZNumerics
