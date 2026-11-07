// Behavioural evidence for db/crt-equivalents.jsonl: CRT functions in the game that don't match our
// toolchain's bytes (EA's CRT is a different version) but must behave the same, so our code can call
// our CRT instead of reimplementing them.
#include "difftest.h"
#include "free_recorder.h"

#include <windows.h>

#include <cerrno>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <intrin.h>
#include <limits>
#include <random>
#include <stdlib.h>
#include <string>
#include <utility>
#include <vector>

// Helpers for the math tests below. Doubles are compared by bit pattern; errno is compared too,
// read from each side's own CRT (the original's through its _errno at 0x831b17).
namespace {

double FromBits(uint64_t b)
{
    double d;
    std::memcpy(&d, &b, sizeof d);
    return d;
}

uint64_t Bits(double d)
{
    uint64_t b;
    std::memcpy(&b, &d, sizeof b);
    return b;
}

// A random double with a random exponent in [lo, hi] (unbiased), random mantissa and sign.
double RandomInRange(std::mt19937 &rng, int lo, int hi)
{
    uint64_t sign = static_cast<uint64_t>(rng() & 1) << 63;
    uint64_t exp = static_cast<uint64_t>(1023 + lo + static_cast<int>(rng() % static_cast<unsigned>(hi - lo + 1))) << 52;
    uint64_t man = ((static_cast<uint64_t>(rng()) << 32) | rng()) & ((1ull << 52) - 1);
    return FromBits(sign | exp | man);
}

// Edge values: zeros, denormals, extremes, infinities, NaNs (quiet, signalling, payloads), the
// rounding boundaries near 2^52/2^53/2^63, and multiples of pi with their neighbours.
std::vector<double> Edges()
{
    std::vector<double> v = {
        0.0, -0.0, FromBits(1), FromBits(0x8000000000000001), FromBits(0x000fffffffffffff), FromBits(0x800fffffffffffff),
        DBL_MIN, -DBL_MIN, DBL_MAX, -DBL_MAX, 1.0, -1.0, 0.5, -0.5, 0.25, -0.25, 1.5, -1.5, 2.5, -2.5,
        0.49999999999999994, -0.49999999999999994, 1.0 - DBL_EPSILON / 2, -(1.0 - DBL_EPSILON / 2),
        4503599627370495.5, -4503599627370495.5, 4503599627370496.0, -4503599627370496.0, 4503599627370497.0,
        9007199254740992.0, -9007199254740992.0, 2147483647.5, -2147483648.5, 2147483648.0, -2147483649.0,
        4294967296.5, -4294967296.5, 9223372036854775807.0, -9223372036854775808.0, 9223372036854777856.0,
        1e19, -1e19, 1e300, -1e300, 1e-300, -1e-300, 709.0, 709.78, 710.0, -745.2, 1e10, -1e10, 1e15 + 0.5,
        std::ldexp(1.0, -27), -std::ldexp(1.0, -27), std::ldexp(1.0, -26), std::ldexp(1.0, -28),
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
        FromBits(0x7ff8000000000000), FromBits(0xfff8000000000000), FromBits(0x7ff0000000000001),
        FromBits(0x7ff8000000000123), FromBits(0xfff0000000000777),
    };
    const double pi = 3.14159265358979323846;
    for (int k = -400; k <= 400; ++k) {
        double m = k * pi;
        v.push_back(m);
        v.push_back(std::nextafter(m, 1e300));
        v.push_back(std::nextafter(m, -1e300));
        v.push_back(k * pi / 2);
        v.push_back(k * pi / 64);
        v.push_back(k * pi / 64 + 1e-12);
    }
    return v;
}

// Test-only ABI shims. The originals take or return doubles in xmm0 (MSVC's SSE2 libm and
// conversion helpers), so each shim loads xmm0 from the cdecl argument, calls the original through
// g_crt_fn, and hands the result back in the form the C++ side expects.
void *g_crt_fn;

__declspec(naked) double __cdecl CallOriginalXmmToSt0(double)
{
    __asm {
        movsd xmm0, qword ptr [esp + 4]
        call dword ptr [g_crt_fn]
        movsd qword ptr [esp + 4], xmm0
        fld qword ptr [esp + 4]
        ret
    }
}

__declspec(naked) int64_t __cdecl CallOriginalXmmToInt64(double)
{
    __asm {
        movsd xmm0, qword ptr [esp + 4]
        call dword ptr [g_crt_fn]
        ret // result in edx:eax
    }
}

int *OriginalErrno(const difftest::Context &ctx)
{
    return ctx.Original<int *(__cdecl *)()>(0x831b17)();
}

} // namespace

DIFF_TEST(CRT_memcpy, 0x8264a0)
{
    auto orig = ctx.Original<void *(__cdecl *)(void *, const void *, size_t)>(0x8264a0);
    std::mt19937 rng(0x3e3c);
    for (int i = 0; i < 20000; ++i) {
        size_t size = rng() % 5 == 0 ? rng() % 70000 : rng() % 300;
        size_t total = size * 2 + 128;
        std::vector<uint8_t> a(total), b;
        for (auto &x : a) x = static_cast<uint8_t>(rng());
        b = a;
        // Disjoint or overlapping, at assorted alignments (MSVC's memcpy also handles overlap).
        size_t src = rng() % 64, dst = rng() % 2 ? src + rng() % (size + 1) : rng() % 64 + size + 64;
        if (dst + size > total) dst = total - size;
        void *ra = orig(a.data() + dst, a.data() + src, size);
        void *rb = std::memcpy(b.data() + dst, b.data() + src, size); // NOLINT: overlap on purpose
        ++ctx.cases;
        if (ra != a.data() + dst || rb != b.data() + dst || a != b) ctx.Fail("memcpy(size %zu, src %zu, dst %zu) differs", size, src, dst);
    }
}

// 0x826b4b (5638 bytes, called by the STL's char_traits compare/equal/find/rfind and cRZVariant::==)
// is memcmp: exact return values compared, not only signs.
DIFF_TEST(CRT_memcmp, 0x826b4b)
{
    auto orig = ctx.Original<int(__cdecl *)(const void *, const void *, size_t)>(0x826b4b);
    std::mt19937 rng(0x3c3b);
    for (int i = 0; i < 100000; ++i) {
        size_t size = rng() % 6 == 0 ? rng() % 5000 : rng() % 70;
        std::vector<uint8_t> a(size + 64), b;
        for (auto &x : a) x = static_cast<uint8_t>(rng());
        b = a;
        size_t offA = rng() % 32, offB = rng() % 32;
        // Make b's window equal a's, then maybe change one or two bytes anywhere in it.
        std::memmove(b.data() + offB, a.data() + offA, size);
        if (size && rng() % 4 != 0) b[offB + rng() % size] = static_cast<uint8_t>(rng());
        if (size && rng() % 4 == 0) b[offB + rng() % size] = static_cast<uint8_t>(rng());
        int ra = orig(a.data() + offA, b.data() + offB, size);
        int rb = std::memcmp(a.data() + offA, b.data() + offB, size);
        ++ctx.cases;
        if (ra != rb) ctx.Fail("memcmp(size %zu, offsets %zu/%zu): original %d, ours %d", size, offA, offB, ra, rb);
    }
}

DIFF_TEST(CRT_stricmp, 0x845799)
{
    auto orig = ctx.Original<int(__cdecl *)(const char *, const char *)>(0x845799);
    std::mt19937 rng(0x571c);
    auto sign = [](int v) { return (v > 0) - (v < 0); };
    for (int i = 0; i < 100000; ++i) {
        std::string x, y;
        size_t n = rng() % 12;
        for (size_t k = 0; k < n; ++k) x += static_cast<char>(rng() % 3 ? 0x40 + rng() % 0x3f : 1 + rng() % 255);
        y = x;
        for (char &c : y) {
            if (rng() % 3 == 0) c = static_cast<char>(c ^ 0x20);
            if (rng() % 20 == 0) c = static_cast<char>(1 + rng() % 255);
        }
        if (rng() % 10 == 0) y.resize(rng() % (y.size() + 1));
        int ra = orig(x.c_str(), y.c_str()), rb = _stricmp(x.c_str(), y.c_str());
        ++ctx.cases;
        // The exact value is a difference of folded characters in both; compare it fully.
        if (ra != rb) ctx.Fail("_stricmp('%s', '%s'): %d vs %d (signs %d %d)", x.c_str(), y.c_str(), ra, rb, sign(ra), sign(rb));
    }
}

namespace {

// Compares one unary math function (result in st0, cdecl) on the same inputs: result bits and errno.
template <typename Ours>
void CompareUnaryMath(difftest::Context &ctx, double (*orig)(double), Ours ours, const std::vector<double> &inputs)
{
    for (double x : inputs) {
        *OriginalErrno(ctx) = 0;
        errno = 0;
        double ra = orig(x);
        int ea = *OriginalErrno(ctx);
        double rb = ours(x);
        int eb = errno;
        ++ctx.cases;
        if (Bits(ra) != Bits(rb) || ea != eb)
            ctx.Fail("(%.17g = %016llx): %016llx (errno %d) vs %016llx (errno %d)", x,
                     static_cast<unsigned long long>(Bits(x)), static_cast<unsigned long long>(Bits(ra)), ea,
                     static_cast<unsigned long long>(Bits(rb)), eb);
    }
}

std::vector<double> RoundingInputs(uint32_t seed)
{
    std::mt19937 rng(seed);
    std::vector<double> v = Edges();
    for (int i = 0; i < 150000; ++i) v.push_back(RandomInRange(rng, -3, 60));
    for (int i = 0; i < 30000; ++i) v.push_back(RandomInRange(rng, -1074, 1023));
    for (int i = 0; i < 30000; ++i) v.push_back(FromBits((static_cast<uint64_t>(rng()) << 32) | rng()));
    return v;
}

} // namespace

// floor (0x838c10): SSE2 body with an x87 result. A flag at 0xa110a38c (dword) selects an x87
// path when zero; the test runs whichever path the harness's CRT state selects (floor is exact, so
// both paths must agree with ours anyway).
DIFF_TEST(CRT_floor, 0x838c10)
{
    auto orig = ctx.Original<double(__cdecl *)(double)>(0x838c10);
    CompareUnaryMath(ctx, orig, [](double x) { return std::floor(x); }, RoundingInputs(0xf100));
}

// ceil (0x82f6e0): the same shape as floor, rounding towards +inf.
DIFF_TEST(CRT_ceil, 0x82f6e0)
{
    auto orig = ctx.Original<double(__cdecl *)(double)>(0x82f6e0);
    CompareUnaryMath(ctx, orig, [](double x) { return std::ceil(x); }, RoundingInputs(0xce11));
}

// sin (0x830b80): table-driven SSE2 sin (xmm0 in, xmm0 out), x87 fallback for NaN/inf/huge.
DIFF_TEST(CRT_sin, 0x830b80)
{
    g_crt_fn = ctx.image.At<void>(0x830b80);
    std::mt19937 rng(0x51a1);
    std::vector<double> v = Edges();
    for (int i = 0; i < 150000; ++i) v.push_back(RandomInRange(rng, -30, 30));
    for (int i = 0; i < 30000; ++i) v.push_back(RandomInRange(rng, 30, 1023));
    for (int i = 0; i < 20000; ++i) v.push_back(RandomInRange(rng, -1074, -20));
    for (int i = 0; i < 30000; ++i) v.push_back(FromBits((static_cast<uint64_t>(rng()) << 32) | rng()));
    CompareUnaryMath(ctx, CallOriginalXmmToSt0, [](double x) { return std::sin(x); }, v);
}

// _dtoi64 (0x824940): double -> int64, xmm0 in, edx:eax out. With bit 0x20 of the byte at
// 0xa104f510 clear it uses vcvttpd2qq (AVX-512DQ); set, an SSE2/integer fallback. The test runs
// the path the harness's CRT state selects (truncation is exact, so both must agree with ours).
DIFF_TEST(CRT_dtoi64, 0x824940)
{
    g_crt_fn = ctx.image.At<void>(0x824940);
    std::mt19937 rng(0x64c1);
    std::vector<double> v = Edges();
    for (int i = 0; i < 100000; ++i) v.push_back(RandomInRange(rng, -2, 70));
    for (int i = 0; i < 30000; ++i) v.push_back(FromBits((static_cast<uint64_t>(rng()) << 32) | rng()));
    for (double x : v) {
        int64_t ra = CallOriginalXmmToInt64(x);
        int64_t rb = static_cast<int64_t>(x);
        ++ctx.cases;
        if (ra != rb)
            ctx.Fail("(%.17g): %016llx vs %016llx", x, static_cast<unsigned long long>(ra), static_cast<unsigned long long>(rb));
    }
}

namespace {

// x87-convention shims (MSVC's _CI* math entries, e.g. atan2 at 0x831270 and fmod at 0x82fd60):
// the arguments are on the x87 stack with st(0) = x (the second C argument) and st(1) = y, and the
// result is returned in st(0). Found by running it: the other order swaps the operands (atan2(y, x)
// came back as atan2(x, y)). The cdecl caller pushes y at [esp+4] and x at [esp+12]; y is loaded
// first so that x ends up on top.
__declspec(naked) double __cdecl CallOriginalX87Binary(double /*y*/, double /*x*/)
{
    __asm {
        fld qword ptr [esp + 4]
        fld qword ptr [esp + 12]
        call dword ptr [g_crt_fn]
        ret
    }
}

// Stack-argument, st(0)-result shim (tan at 0x83fd10): the wrapper reads its argument from its own
// stack, so the double is pushed again for the call (high dword first, so the low one is at [esp+4]).
__declspec(naked) double __cdecl CallOriginalStackToSt0(double)
{
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        call dword ptr [g_crt_fn]
        add esp, 8
        ret
    }
}

// Two doubles in xmm0 and xmm1, result in st(0) (the SSE2 libm convention, as in CallOriginalXmmToSt0).
__declspec(naked) double __cdecl CallOriginalXmmBinaryToSt0(double /*a*/, double /*b*/)
{
    __asm {
        movsd xmm0, qword ptr [esp + 4]
        movsd xmm1, qword ptr [esp + 12]
        call dword ptr [g_crt_fn]
        movsd qword ptr [esp + 4], xmm0
        fld qword ptr [esp + 4]
        ret
    }
}

// Two-argument compare: result bits and errno, like CompareUnaryMath.
template <typename Ours>
void CompareBinaryMath(difftest::Context &ctx, double (*orig)(double, double), Ours ours,
                       const std::vector<std::pair<double, double>> &inputs)
{
    for (const auto &in : inputs) {
        double y = in.first, x = in.second;
        *OriginalErrno(ctx) = 0;
        errno = 0;
        double ra = orig(y, x);
        int ea = *OriginalErrno(ctx);
        double rb = ours(y, x);
        int eb = errno;
        ++ctx.cases;
        if (Bits(ra) != Bits(rb) || ea != eb)
            ctx.Fail("(y %.17g, x %.17g): %016llx (errno %d) vs %016llx (errno %d)", y, x,
                     static_cast<unsigned long long>(Bits(ra)), ea, static_cast<unsigned long long>(Bits(rb)), eb);
    }
}

std::vector<std::pair<double, double>> BinaryInputs(uint32_t seed)
{
    std::mt19937 rng(seed);
    std::vector<double> e = Edges();
    std::vector<std::pair<double, double>> v;
    for (double a : e)
        for (int k = 0; k < 40; ++k) v.push_back({a, e[rng() % e.size()]});
    for (int i = 0; i < 100000; ++i) v.push_back({RandomInRange(rng, -3, 60), RandomInRange(rng, -3, 60)});
    for (int i = 0; i < 50000; ++i) v.push_back({RandomInRange(rng, -1074, 1023), RandomInRange(rng, -1074, 1023)});
    for (int i = 0; i < 30000; ++i)
        v.push_back({FromBits((static_cast<uint64_t>(rng()) << 32) | rng()), FromBits((static_cast<uint64_t>(rng()) << 32) | rng())});
    return v;
}

// The original's _invalid_parameter_noinfo (0x82a787) fast-fails unless a handler is installed in its
// per-thread data, which the harness never sets up. While the string tests run, it is redirected to
// this counting stub (5-byte jmp, restored afterwards); ours goes through a counting handler set with
// _set_invalid_parameter_handler, so both sides' invalid-parameter calls can be compared.
int gInvalidOriginal = 0;
int gInvalidOurs = 0;
void __cdecl StubInvalidNoinfo()
{
    ++gInvalidOriginal;
}
void __cdecl OurInvalidHandler(const wchar_t *, const wchar_t *, const wchar_t *, unsigned int, uintptr_t)
{
    ++gInvalidOurs;
}

struct InvalidNoinfoRedirect {
    uint8_t *site;
    uint8_t saved[5];
    explicit InvalidNoinfoRedirect(difftest::Context &ctx) : site(ctx.image.At<uint8_t>(0x82a787))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&StubInvalidNoinfo) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    ~InvalidNoinfoRedirect() { std::memcpy(site, saved, 5); }
};

} // namespace

// calloc (0x83a371 -> 0x849a00): zeroed block of num*size bytes; NULL with ENOMEM on overflow or
// failure. Before each call a dirty block of the same size is freed, so a calloc that failed to zero
// would show garbage.
DIFF_TEST(CRT_calloc, 0x83a371)
{
    auto orig = ctx.Original<void *(__cdecl *)(size_t, size_t)>(0x83a371);
    auto origFree = ctx.Original<void(__cdecl *)(void *)>(0x831a54);
    std::mt19937 rng(0xca11);
    std::vector<std::pair<size_t, size_t>> cases = {
        {0, 0}, {0, 5}, {5, 0}, {1, 0}, {0x10000, 0x10000}, {0x80000000u, 2}, {2, 0x80000000u}, {0xffffffffu, 1},
        {1, 0xffffffffu}, {1, 0xffffffe0u}, {1, 0xffffffe1u}, {0xffffffe0u, 1}, {0xffffffe1u, 1}, {0x40, 0x40},
    };
    for (int i = 0; i < 20000; ++i) {
        size_t num = rng() % 3 == 0 ? rng() % 0x200 : rng() % 8;
        size_t size = rng() % 3 == 0 ? rng() % 0x200 : rng() % 8;
        // Near the overflow boundary: num * size just below or above 0xffffffe0.
        if (rng() % 10 == 0) size = 0xffffffe0u / (num ? num : 1) + rng() % 3;
        cases.push_back({num, size});
    }
    for (size_t i = 0; i < cases.size(); ++i) {
        size_t num = cases[i].first, size = cases[i].second;
        uint64_t total = static_cast<uint64_t>(num) * size;
        // Zero check covers at most 2 MB (the random sizes are small; overflow cases return NULL).
        size_t check = total ? static_cast<size_t>(total < 0x200000 ? total : 0x200000) : 1;
        // Dirty the heap with a block of the same size, so zeroing can't be luck.
        void *junk = std::malloc(check + 16);
        if (junk) std::memset(junk, 0xab, check + 16);
        std::free(junk);
        *OriginalErrno(ctx) = 0;
        errno = 0;
        void *pa = orig(num, size);
        int ea = *OriginalErrno(ctx);
        void *pb = std::calloc(num, size);
        int eb = errno;
        bool zeroA = true, zeroB = true;
        if (pa)
            for (size_t k = 0; k < check; ++k) zeroA = zeroA && static_cast<uint8_t *>(pa)[k] == 0;
        if (pb)
            for (size_t k = 0; k < check; ++k) zeroB = zeroB && static_cast<uint8_t *>(pb)[k] == 0;
        ++ctx.cases;
        if ((pa == nullptr) != (pb == nullptr) || ea != eb || !zeroA || !zeroB)
            ctx.Fail("calloc(%zu, %zu): original %p (errno %d, zeroed %d) vs %p (errno %d, zeroed %d)", num, size, pa, ea,
                     zeroA, pb, eb, zeroB);
        if (pa) origFree(pa);
        std::free(pb);
    }
}

// realloc (0x8413b0 -> 0x85217f): NULL block = malloc; size 0 frees and returns NULL; failure keeps
// the block. Compares null-ness, errno, the preserved bytes, and whether the block was freed/kept.
DIFF_TEST(CRT_realloc, 0x8413b0)
{
    auto orig = ctx.Original<void *(__cdecl *)(void *, size_t)>(0x8413b0);
    auto origFree = ctx.Original<void(__cdecl *)(void *)>(0x831a54);
    std::mt19937 rng(0x7e4a);
    static const size_t kHuge[] = {0xffffffe0u, 0xffffffe1u, 0xffffffffu, 0x7fffffffu};
    for (int i = 0; i < 20000; ++i) {
        size_t oldn = rng() % 4 == 0 ? rng() % 70000 : rng() % 300;
        size_t newn = rng() % 4 == 0 ? kHuge[rng() % 4] : rng() % 6 == 0 ? 0 : rng() % 2 ? rng() % 70000 : rng() % 300;
        bool nullBlock = rng() % 10 == 0;
        std::vector<uint8_t> content(oldn);
        for (auto &c : content) c = static_cast<uint8_t>(rng());
        void *a = nullBlock ? nullptr : std::malloc(oldn);
        void *b = nullBlock ? nullptr : std::malloc(oldn);
        if (a) std::memcpy(a, content.data(), oldn);
        if (b) std::memcpy(b, content.data(), oldn);
        *OriginalErrno(ctx) = 0;
        errno = 0;
        difftest::FreeRecorder recA;
        void *ra = orig(a, newn);
        recA.Stop();
        int ea = *OriginalErrno(ctx);
        // A failed realloc keeps its block (live: HeapValidate); a zero-size one frees it (FreeRecorder).
        bool keptA = a && newn != 0 && !ra;
        bool freedA = a && newn == 0 && recA.Freed(a);
        bool intactA = !keptA || (HeapValidate(GetProcessHeap(), 0, a) && std::memcmp(a, content.data(), oldn) == 0);
        difftest::FreeRecorder recB;
        void *rb = std::realloc(b, newn);
        recB.Stop();
        int eb = errno;
        bool keptB = b && newn != 0 && !rb;
        bool freedB = b && newn == 0 && recB.Freed(b);
        bool intactB = !keptB || (HeapValidate(GetProcessHeap(), 0, b) && std::memcmp(b, content.data(), oldn) == 0);
        // A NULL block has no content to keep (the new block is uninitialised, so it isn't compared).
        size_t keep = nullBlock ? 0 : (oldn < newn ? oldn : newn);
        bool dataA = !ra || std::memcmp(ra, content.data(), keep) == 0;
        bool dataB = !rb || std::memcmp(rb, content.data(), keep) == 0;
        ++ctx.cases;
        if ((ra == nullptr) != (rb == nullptr) || ea != eb || !dataA || !dataB || keptA != keptB || freedA != freedB ||
            !intactA || !intactB)
            ctx.Fail("realloc(old %zu, new %zu, null %d): original %p (errno %d, kept %d, freed %d) vs %p (errno %d, kept %d, freed %d)",
                     oldn, newn, nullBlock, ra, ea, keptA, freedA, rb, eb, keptB, freedB);
        if (ra) origFree(ra);
        if (keptA) origFree(a);
        if (rb) std::free(rb);
        if (keptB) std::free(b);
    }
}

// strcpy_s (0x845af9) and wcscpy_s (0x849acc): (dst, size, src) -> errno_t, with the same NUL-filling
// and ERANGE/EINVAL rules. Compares the result, errno, invalid-parameter calls and the whole buffer
// (canaries included); nulls and too-small sizes are included.
namespace {

template <typename Ch, typename Fn, typename Ours>
void CopySCases(difftest::Context &ctx, Fn orig, Ours ours, uint32_t seed)
{
    std::mt19937 rng(seed);
    for (int i = 0; i < 30000; ++i) {
        bool srcNull = rng() % 40 == 0;
        size_t len = rng() % 61;
        std::vector<Ch> src(len + 1);
        for (size_t k = 0; k < len; ++k) src[k] = static_cast<Ch>(1 + rng() % (sizeof(Ch) == 1 ? 255 : 0xfffe));
        src[len] = 0;
        size_t size = rng() % 8 == 0 ? 0 : rng() % (len + 4);
        if (rng() % 10 == 0) size = 0x7fffffffu; // huge size with a short source: must just copy
        size_t cap = (size > len + 9 ? len + 1 : (size > len + 1 ? size : len + 1)) + 16;
        bool dstNull = rng() % 30 == 0;
        size_t off = rng() % 4;
        std::vector<Ch> bufA(cap + 8, static_cast<Ch>(0x5a)), bufB = bufA;
        Ch *dstA = dstNull ? nullptr : bufA.data() + off;
        Ch *dstB = dstNull ? nullptr : bufB.data() + off;
        const Ch *srcPtr = srcNull ? nullptr : src.data();
        *OriginalErrno(ctx) = 0;
        errno = 0;
        int invA0 = gInvalidOriginal, invB0 = gInvalidOurs;
        int ra = orig(dstA, size, srcPtr);
        int ea = *OriginalErrno(ctx);
        int invA = gInvalidOriginal - invA0;
        int rb = ours(dstB, size, srcPtr);
        int eb = errno;
        int invB = gInvalidOurs - invB0;
        ++ctx.cases;
        if (ra != rb || ea != eb || invA != invB || bufA != bufB)
            ctx.Fail("copy_s(size %zu, src len %zu, dst null %d, src null %d): %d (errno %d, invalid %d) vs %d (errno %d, invalid %d), buffers %s",
                     size, len, dstNull, srcNull, ra, ea, invA, rb, eb, invB, bufA == bufB ? "equal" : "differ");
    }
}

} // namespace

DIFF_TEST(CRT_strcpy_s, 0x845af9)
{
    auto orig = ctx.Original<int(__cdecl *)(char *, size_t, const char *)>(0x845af9);
    InvalidNoinfoRedirect redirect(ctx);
    auto prev = _set_invalid_parameter_handler(OurInvalidHandler);
    CopySCases<char>(ctx, orig, [](char *d, size_t s, const char *p) { return ::strcpy_s(d, s, p); }, 0x5c01);
    _set_invalid_parameter_handler(prev);
}

DIFF_TEST(CRT_wcscpy_s, 0x849acc)
{
    auto orig = ctx.Original<int(__cdecl *)(wchar_t *, size_t, const wchar_t *)>(0x849acc);
    InvalidNoinfoRedirect redirect(ctx);
    auto prev = _set_invalid_parameter_handler(OurInvalidHandler);
    CopySCases<wchar_t>(ctx, orig, [](wchar_t *d, size_t s, const wchar_t *p) { return ::wcscpy_s(d, s, p); }, 0x5c02);
    _set_invalid_parameter_handler(prev);
}

// atan2 (0x831270, x87 convention, 56 callers): bit-exact result and errno vs std::atan2.
DIFF_TEST(CRT_atan2, 0x831270)
{
    g_crt_fn = ctx.image.At<void>(0x831270);
    CompareBinaryMath(ctx, CallOriginalX87Binary, [](double y, double x) { return std::atan2(y, x); }, BinaryInputs(0xa7a2));
}

// fmod (0x82fd60, x87 convention, fprem loop): bit-exact result and errno vs std::fmod.
DIFF_TEST(CRT_fmod, 0x82fd60)
{
    g_crt_fn = ctx.image.At<void>(0x82fd60);
    CompareBinaryMath(ctx, CallOriginalX87Binary, [](double y, double x) { return std::fmod(y, x); }, BinaryInputs(0xf3d0));
}

// pow (0x8380c0, SSE2 convention: base in xmm0, exponent in xmm1): bit-exact result and errno vs
// std::pow. Identity is by the test alone (no name in db/), so it is only recorded if this passes.
DIFF_TEST(CRT_pow, 0x8380c0)
{
    g_crt_fn = ctx.image.At<void>(0x8380c0);
    CompareBinaryMath(ctx, CallOriginalXmmBinaryToSt0, [](double a, double b) { return std::pow(a, b); }, BinaryInputs(0x90a0));
}

// tan (0x83fd10, stack argument, st(0) result): bit-exact result and errno vs std::tan.
DIFF_TEST(CRT_tan, 0x83fd10)
{
    g_crt_fn = ctx.image.At<void>(0x83fd10);
    std::mt19937 rng(0x7a17);
    std::vector<double> v = Edges();
    for (int i = 0; i < 150000; ++i) v.push_back(RandomInRange(rng, -30, 30));
    for (int i = 0; i < 30000; ++i) v.push_back(RandomInRange(rng, 30, 1023));
    for (int i = 0; i < 30000; ++i) v.push_back(FromBits((static_cast<uint64_t>(rng()) << 32) | rng()));
    CompareUnaryMath(ctx, CallOriginalXmmToSt0, [](double x) { return std::tan(x); }, v);
}
