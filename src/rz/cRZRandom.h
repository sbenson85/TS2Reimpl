#pragma once
#include <cstdint>
#include <ctime>

// Linear congruential generator used throughout the game (genetics, autonomy, ...).
// RVAs are 1.18.0.187.
class cRZRandom {
public:
    cRZRandom *Construct(uint32_t seed);                      // 0x1b8bc8 (constructor body)
    // Construct for a replacement of original code that calls the constructor at a known call site
    // (originalSite: the RVA the original's call returns to). Same as Construct, except that seed
    // control (gRZRandomClockSeedOverride) keys a UINT32_MAX seed by that site, so reference and
    // replacement runs get the same seed.
    cRZRandom *ConstructAt(uint32_t seed, uint32_t originalSite);
    void Seed(uint32_t seed);                                 // 0x1b8ecb; UINT32_MAX seeds from time/clock
    uint32_t RandomUint32Uniform();                           // 0x1b8d75
    uint32_t RandomUint32Uniform(uint32_t scale);             // 0x1b8d5c
    int32_t RandomSint32RangeUniform(int32_t min, int32_t max);  // 0x1b8d47
    double RandomDoubleUniform();                             // 0x1b8c78
    double RandomDoubleRangeUniform(double min, double max);  // 0x1b8c48

    uint32_t mIntegerSeed;
    int32_t mRealSeed;
    uint32_t mUnknown; // zeroed by the constructor; unused by these methods
};

// Mersenne Twister (MT19937 output, Cokus-style reload) used by some game systems.
class cRZRandomSuper {
public:
    static constexpr int kN = 624;

    cRZRandomSuper *Construct(uint32_t seed);                 // 0x1b8bde (constructor body)
    void Seed(uint32_t seed);                                 // 0x1b8efa; UINT32_MAX seeds from time/clock
    void Reload();                                            // 0x1b8deb
    uint32_t RandomUint32Uniform();                           // 0x1b8d9b
    uint32_t RandomUint32Uniform(uint32_t scale);             // 0x1b8d8d
    double RandomDoubleUniform();                             // 0x1b8cba
    double RandomDoubleRangeUniform(double min, double max);  // 0x1b8c60
    // Mixes two values into a seed; counts its calls in a static. 0x1b8c01, __stdcall.
    static uint32_t __stdcall Hash(uint32_t a, uint32_t b);

    uint32_t mState[kN];
    uint32_t *mNext;
    int32_t mLeft;
};

// Test-control seam (milestone B; set only by host/control_seed.cpp under TS2HOST_SEED, null otherwise):
// when set, a UINT32_MAX seed takes its clock reading from here instead of time()/clock(). It
// replaces an input, like the host's detours at the image entry points do for the original; it is
// needed because our own code seeds internally (e.g. cGZCOM's generator from our constructors)
// without passing through those entry points. kind: 0 cRZRandom, 1 cRZRandomSuper; site: the
// original's call site (ConstructAt), 0 when unknown.
extern uint32_t (*gRZRandomClockSeedOverride)(int kind, uint32_t site);

// A clock reading used as a seed by a replacement of original code that seeds from clock() at a known call site
// (originalSite: the RVA the original's seeding call returns to; host/control_seed.cpp kClockSeedCallers). Seed control
// replaces the original's clock seeds at those sites; ours must take the same replacement, or controlled runs diverge
// (2026-11-04: the CAS system's and skin catalog's seeds, ours since lane IU, made the generated townie's name differ).
inline uint32_t ClockSeedAt(uint32_t originalSite)
{
    return gRZRandomClockSeedOverride ? gRZRandomClockSeedOverride(0, originalSite) : static_cast<uint32_t>(std::clock());
}

static_assert(sizeof(cRZRandom) == 12, "cRZRandom layout");
static_assert(sizeof(cRZRandomSuper) == 0x9c8, "cRZRandomSuper layout: state, next at 0x9c0, left at 0x9c4");
