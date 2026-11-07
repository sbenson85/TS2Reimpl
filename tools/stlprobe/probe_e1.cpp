// The effect descriptions' part vectors (lane WO, rendering row E1): vector<part> over the particle parts' shapes. Same
// shape as probe_render.cpp: one member per unit (-DOP=n) or all (-DOP=99), C++14.
//   -DFAM=0 attractor (0x34, a curve inside), 1 collider (0x84, three strings), 2 warp (0x1c), 3 path point (0x1c, plain),
//   4 colour (three floats), 5 path (0x10, a vector inside)
//   6 attractor state (0x14), 7 a 4-byte element (TI 7: two 16-bit fields, the only shape with the original's
//   value-init loop; the other TIs are the shapes that failed), 8 an 84-byte element, 9 attractor and 10 collider with
//   their real members (lane XI)
//   -DTI=0: the element's special members are out-of-line calls; 1: compiler-written; 2: defaulted, not inlined
//   FAM 9, 10, by bit: 0 _Destroy_range, 1 value construct, 2 uninitialized copy, 3 uninitialized copy_n stay calls;
//   4: element assignment inline; FAM 9, 5: default constructor declared; FAM 10, 5: floats as scalars, 6 destructor
//   declared, 7 copy constructor declared. OP 7: explicit vector instantiation, 8: explicit value construct.
//   The combinations that match are stlprobe.ps1's $Tuned.
#include <cstdint>
#include <new>
#include <string>
#include <vector>
#include <cstring>

#ifndef FAM
#define FAM 0
#endif
#ifndef TI
#define TI 0
#endif
#ifndef OP
#define OP 99
#endif
#define WANT(n) (OP == 99 || OP == (n))

struct V3 {
    float x, y, z;
};
#if TI == 0
#define MEMBERS(T) \
    T(); \
    T(const T &); \
    T &operator=(const T &); \
    ~T();
#elif TI == 2
#define MEMBERS(T) \
    __declspec(noinline) T(const T &) = default; \
    __declspec(noinline) T &operator=(const T &) = default; \
    __declspec(noinline) ~T() = default;
#else
#define MEMBERS(T)
#endif

#if FAM == 0
struct Curve {
    std::vector<float> v;
    uint32_t w;
};
struct E {
    MEMBERS(E)
#if TI != 0
    E() : b(0), c{0, 0, 0}, f{1, 0, 0}, s(1), t(-1) {}
#endif
    uint8_t b;
    V3 c;
    float f[3];
    Curve k;
    float s, t;
};
#elif FAM == 1
struct E {
    MEMBERS(E)
#if TI != 0
    E() : a(2), f{1, 1, 0, 0, 0} {}
#endif
    uint32_t a;
    std::string n;
    V3 p[3];
    float f[5];
    std::string s, u;
};
#elif FAM == 2
struct E {
#if TI != 0
    E() : v(0) {}
#else
    E();
#endif
    float v;
    V3 a, b;
};
#elif FAM == 3
struct E {
    V3 a, b;
    float t;
};
#elif FAM == 4
using E = V3;
#elif FAM == 6
struct E {
    __declspec(noinline) E() : w{} {}
    uint32_t w[5];
};
#elif FAM == 9
struct Curve {
    Curve() = default;
    Curve(const Curve &) = default;
    Curve(Curve &&) = default;
#if TI & 16
    __declspec(noinline) Curve &operator=(const Curve &) = default;
#else
    Curve &operator=(const Curve &) = default;
#endif
    uint32_t a;
    std::vector<float> v;
};
struct E {
#if TI & 32
    E();
#else
    __declspec(noinline) E() : b(0), c{0, 0, 0}, f{1, 0, 0}, s(1), t(-1) {}
#endif
    __declspec(noinline) E(const E &) = default;
    E(E &&) = default;
#if TI & 16
    E &operator=(const E &) = default;
#else
    __declspec(noinline) E &operator=(const E &) = default;
#endif
    uint8_t b;
    V3 c;
    V3 f;
    Curve k;
    float s, t;
};
#elif FAM == 10
struct E {
    __declspec(noinline) E() : a(2) {}
#if TI & 128
    E(const E &);
#else
    __declspec(noinline) E(const E &) = default;
#endif
#if TI & 16
    E &operator=(const E &) = default;
#else
    __declspec(noinline) E &operator=(const E &) = default;
#endif
#if TI & 64
    ~E();
#else
    __declspec(noinline) ~E() = default;
#endif
    uint32_t a;
    std::string n;
    V3 p[3];
#if TI & 32
    float f0 = 1, f1 = 1, f2 = 0, f3 = 0, f4 = 0;
#else
    float f[5];
#endif
    std::string s, u;
};
#elif FAM == 7
struct E {
#if TI == 2
    uint32_t v = 0;
#elif TI == 3
    E() : v(nullptr) {}
    void *v;
#elif TI == 4
    E() : v(0.0f) {}
    float v;
#elif TI == 5
    E() : v(0.0f) {}
    union { float v; uint32_t u; };
#elif TI == 6
    E() : v(0) {}
    uint16_t v, w;
#elif TI == 7
    E() : v(0), w(0) {}
    uint16_t v, w;
#else
    E() : v(0) {}
    uint32_t v;
#endif
};
#elif FAM == 8
struct E {
    E();
    E(const E &);
    E &operator=(const E &);
    ~E();
    uint32_t w[21];
};
#else
struct P {
    V3 a, b;
    float t;
};
struct E {
    std::vector<P> v;
    uint32_t f = 0;
};
#endif

#if FAM == 9 || FAM == 10
#if TI & 1
template <> void std::_Destroy_range<std::allocator<E>>(E *, E *const, std::allocator<E> &) noexcept;
#endif
#if TI & 2
template <> E *std::_Uninitialized_value_construct_n<std::allocator<E>>(E *, size_t, std::allocator<E> &);
#endif
#if TI & 4
template <> E *std::_Uninitialized_copy<E *, E *, std::allocator<E>>(E *, E *, E *, std::allocator<E> &);
#endif
#if TI & 8
template <> E *std::_Uninitialized_copy_n<E *, std::allocator<E>>(E *, size_t, E *, std::allocator<E> &);
#endif
#endif
#if FAM == 8
template <> void std::_Destroy_range<std::allocator<E>>(E *, E *const, std::allocator<E> &) noexcept;
#endif
#if FAM == 7 && TI == 1
template <> E *std::_Copy_memmove<E *, E *>(E *, E *, E *);
#elif FAM == 6 || FAM == 7
template <> __declspec(noinline) E *std::_Copy_memmove<E *, E *>(E *first, E *last, E *out)
{
    memmove(out, first, reinterpret_cast<char *>(last) - reinterpret_cast<char *>(first));
    return out + (last - first);
}
#endif
using Vec = std::vector<E>;
#if WANT(0)
void Op0(Vec &v, size_t n) { v.resize(n); }
#endif
#if WANT(1)
void Op1(Vec &v, size_t n) { v.reserve(n); }
#endif
#if WANT(2)
void Op2(Vec &v, const Vec &o) { v = o; }
#endif
#if WANT(3)
void Op3(Vec &v, const E &e) { v.push_back(e); }
#endif
#if WANT(4)
void Op4(Vec *v) { v->~Vec(); }
#endif
#if WANT(5)
void Op5(void *p, const Vec &o) { new (p) Vec(o); }
#endif
#if WANT(6)
void Op6(Vec &v) { v.clear(); }
#endif
#if OP == 7
template class std::vector<E>;
#endif
#if OP == 8 && (FAM == 9 || FAM == 10)
template E *std::_Uninitialized_value_construct_n<std::allocator<E>>(E *, size_t, std::allocator<E> &);
#endif
