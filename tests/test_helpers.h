// Small helpers several differential tests repeat (docs/lane/test.md names each where it applies). Header only, for tests/.
#pragma once

#include "difftest.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <random>

namespace testhelp {

// The image's atexit (library code at RVA 0x824cbe) works on its CRT's exit table, which the harness never
// initialises (the real one corrupts the heap). While an AtexitRedirect lives, a 5-byte jmp over the image's atexit
// entry sends registrations to a stub; the destructor restores the bytes. The default stub counts the calls and keeps
// the last handler (AtexitCalls, AtexitHandler); pass your own `int __cdecl (void (*)())` to record differently.
inline int gAtexitCalls = 0;
inline void (*gAtexitHandler)() = nullptr;

inline int __cdecl RecordAtexit(void (*handler)())
{
    ++gAtexitCalls;
    gAtexitHandler = handler;
    return 0;
}

struct AtexitRedirect {
    static constexpr uint32_t kImageAtexit = 0x824cbe;
    uint8_t *site;
    uint8_t saved[5];
    explicit AtexitRedirect(difftest::Context &ctx, int(__cdecl *stub)(void (*)()) = &RecordAtexit)
        : site(ctx.image.At<uint8_t>(kImageAtexit))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stub) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    ~AtexitRedirect() { std::memcpy(site, saved, 5); }
    AtexitRedirect(const AtexitRedirect &) = delete;
    AtexitRedirect &operator=(const AtexitRedirect &) = delete;

    // Clears the default stub's record (before the run whose registrations you check).
    static void Clear()
    {
        gAtexitCalls = 0;
        gAtexitHandler = nullptr;
    }
};

// Game code that takes a block the first time it runs (a lazy one-off: a static table, a pool's first page) makes the
// first counted run of each side differ from the rest. A WarmOnce runs a warm-up once per side, outside the counts:
//     static testhelp::WarmOnce warm;
//     warm(gOriginal, [&] { Drop(Make()); });
// Returns true when it ran the warm-up this time.
class WarmOnce {
public:
    template <class F> bool operator()(bool original, F &&warmUp)
    {
        bool &done = done_[original ? 1 : 0];
        if (done) return false;
        done = true;
        difftest::NotCounted quiet;
        warmUp();
        return true;
    }
    void Reset() { done_[0] = done_[1] = false; }

private:
    bool done_[2] = {false, false};
};

// A float from a few values: the ones code under test treats specially (zero of both signs, one, a half, the
// rotation cosines, a small and a large magnitude). Exact values make equal inputs, ties and branch edges common,
// which uniform floats almost never hit. The second form picks from your own list.
inline float FewValues(std::mt19937 &rng, std::initializer_list<float> values)
{
    return values.begin()[rng() % values.size()];
}

inline float FewValues(std::mt19937 &rng)
{
    return FewValues(rng, {0.0f, -0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 2.0f, -2.0f, 0.866f, -0.866f, 1e-6f, 1e6f});
}

// A few values one time in `oneIn`, otherwise uniform in [lo, hi).
inline float FewOrUniform(std::mt19937 &rng, float lo, float hi, unsigned oneIn = 2)
{
    if (rng() % oneIn == 0) return FewValues(rng);
    return std::uniform_real_distribution<float>(lo, hi)(rng);
}

} // namespace testhelp
