// Rendering's library instances left after RL2 (lane XI, rendering row RL). FAM 1: a 16-byte vector whose element
// constructor does nothing. One member per unit (-DOP=n) or all of a
// family's (-DOP=99), as probe_render.cpp, with stlprobe.ps1's $Rl2FlagSets, in C++14 mode.
//   -DFAM=0  vector<Shader*> sorted by a stateless comparator (even TI vertex, odd TI pixel). RL2 read the slot pushed for
//            the predicate as state, but it is uninitialised at the call: the comparator is empty. The vertex sort needs
//            both comparisons' bodies visible in the unit and out of line: the compiler then knows `this` goes unused and
//            drops its copies of the predicate (extern, as in probe_render.cpp's family 1, Sort is 651 B against 619).
//            The pixel side (0x171ff0, 0x170070, 0x171880) and equal_range (0x1730c0, 0x1732e0) are not reproduced: they
//            inline the comparison, and how the game fills the key is not settled. A delegating constructor gives the
//            game's sizes for both comparisons (154, 140 B) but not their field order; see rendering.md, "XI result".
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <new>
#include <string>
#include <utility>
#include <vector>

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

#if FAM == 0
struct Ref {
    void *p;
};
struct alignas(4) VShader {
    void *vtbl;
    uint32_t refs;
    void *device;
    void *d3d;
    void *buffer;
    std::vector<Ref> names; // +0x14
    uint8_t info[6];        // +0x20
    uint8_t software, hardware; // +0x26
};
struct PShader {
    void *vtbl;
    uint32_t refs;
    void *device;
    void *d3d;
    void *buffer;
    std::vector<Ref> names;
};
using S = std::conditional_t<TI % 2 == 0, VShader, PShader>;
struct Key {  // built in place: a by-value helper adds a copy the game's bodies don't have
    void *device;
    uint32_t count;
    const Ref *names;
    void *buffer;
    uint8_t software, hardware;
    explicit Key(const S *s)
        : device(s->device), count(static_cast<uint32_t>(s->names.size())),
          names(s->names.empty() ? nullptr : s->names.data()), buffer(s->buffer),
#if TI % 2 == 0
          software(s->software), hardware(s->hardware)
#else
          software(0), hardware(1)
#endif
    {
    }
};
struct Compare {
    bool Less(const Key &a, const Key &b) const;
    bool operator()(const S *s, const Key &k) const { return Less(Key(s), k); }
    bool operator()(const Key &k, const S *s) const { return Less(k, Key(s)); }
    bool operator()(const S *a, const S *b) const;
};
using V = std::vector<S *>;
#if WANT(0)
void Sort(V &v) { std::sort(v.begin(), v.end(), Compare{}); }
#endif
#if WANT(1)
std::pair<V::iterator, V::iterator> Range(V &v, const Key &k) { return std::equal_range(v.begin(), v.end(), k, Compare{}); }
#endif
#if WANT(2)
void Insert(V &v, V::const_iterator at, S *s) { v.insert(at, s); }
#endif
#if WANT(3)
V::iterator Lower(V &v, const Key &k) { return std::lower_bound(v.begin(), v.end(), k, Compare{}); }
#endif
#if WANT(4)
void PushBack(V &v, S *s) { v.push_back(s); }
#endif
uint32_t BufSize(void *);  // a virtual call in the game; only that it is a call matters here
__declspec(noinline)
bool Compare::operator()(const S *a, const S *b) const
{
    Key ka(a), kb(b);
    return Less(ka, kb);
}
__declspec(noinline) bool Compare::Less(const Key &a, const Key &b) const
{
    if (a.device != b.device) return a.device < b.device;
    if (a.count != b.count) return a.count < b.count;
    if (a.software != b.software) return a.software < b.software;
    if (a.hardware != b.hardware) return a.hardware < b.hardware;
    if (a.count == 0) {
        if (BufSize(a.buffer) == BufSize(b.buffer)) return BufSize(a.buffer) < 7;
        return BufSize(a.buffer) < BufSize(b.buffer);
    }
    for (uint32_t i = 0; i < a.count; ++i)
        if (a.names[i].p != b.names[i].p) return a.names[i].p < b.names[i].p;
    return false;
}
#endif

#if FAM == 1  // vector of a 16-byte element whose default constructor does nothing: a resize's reallocation constructs nothing
struct E {
    E() {}
    float v[4];
};
#if WANT(0)
void Resize(std::vector<E> &v, size_t n) { v.resize(n); }
#endif
#endif
