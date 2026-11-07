// The second library pass of the rendering campaign (lane WM, row RL2): the instantiations no earlier probe reproduced.
// Same shape as probe_render.cpp: stand-in element types, game functions declared and never defined so they stay calls,
// one member per unit (-DOP=n) or all of a family's (-DOP=99), C++14, with stlprobe.ps1's $Rl2FlagSets.
//   -DFAM=0  vector<Fmt*> sorted through a virtual comparison (cPixelFormatsManagerDX::SortFormats); TI 1: arguments
//            the other way round; TI 2: the comparison functor derives from binary_function
//   -DFAM=1  a 16-byte record heaped by (float)count * weight (cDeviceResourceManagerDX::ManageTextureMemory); TI 0: a
//            functor, TI 1: operator<, TI 2: a binary_function
//   -DFAM=2  map<int, Value> over Values with constructors of their own (cGZFontSys's maps): TI picks the size
//   -DFAM=3  map<Str, ...> where Str is a vtable and a std::string, ordered by the string (cRZString keys); TI 0:
//            operator<, TI 1: a comparison functor
//   -DFAM=4  unordered_set: TI 0 short, 1 unsigned short, 2 wchar_t, 3 uint8, 4 uint32, 5 uint64, 6 and 7 12 and 16 bytes
//            compared as bytes under an extern hash
//   -DFAM=5  vectors of trivial records: TI 0 16 bytes, 1 24 bytes, 2 a pointer
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <new>
#include <queue>
#include <string>
#include <unordered_set>
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
struct Fmt {
    virtual void S0();
    virtual void S1();
    virtual void S2();
    virtual void S3();
    virtual void S4();
    virtual void S5();
    virtual void S6();
    virtual void S7();
    virtual void S8();
    virtual void S9();
    virtual bool Better(const Fmt *other, int flags) const;
};
struct ByFormat
#if TI == 2
    : std::binary_function<Fmt *, Fmt *, bool>
#endif
{
#if TI == 1
    bool operator()(const Fmt *a, const Fmt *b) const { return b->Better(a, 0); }
#else
    bool operator()(const Fmt *a, const Fmt *b) const { return a->Better(b, 0); }
#endif
};
using V = std::vector<Fmt *>;
#if WANT(0)
void Sort(V &v) { std::sort(v.begin(), v.end(), ByFormat{}); }
#endif
#if WANT(1)
void SortRaw(Fmt **first, Fmt **last) { std::sort(first, last, ByFormat{}); }
#endif
#endif

#if FAM == 1
struct Rec {
    uint32_t a, b;
    int count;
    float weight;
    float Area() const { return static_cast<float>(count) * weight; }
};
#if TI == 1
inline bool operator<(const Rec &a, const Rec &b) { return a.Area() < b.Area(); }
using Less = std::less<Rec>;
#else
struct Less
#if TI == 2
    : std::binary_function<Rec, Rec, bool>
#endif
{
    bool operator()(const Rec &a, const Rec &b) const { return a.Area() < b.Area(); }
};
#endif
using V = std::vector<Rec>;
#if WANT(0)
void Push(V &v) { std::push_heap(v.begin(), v.end(), Less{}); }
#endif
#if WANT(1)
void Pop(V &v) { std::pop_heap(v.begin(), v.end(), Less{}); }
#endif
#if WANT(2)
void Make(V &v) { std::make_heap(v.begin(), v.end(), Less{}); }
#endif
#if WANT(3)
void SortIt(V &v) { std::sort(v.begin(), v.end(), Less{}); }
#endif
#if WANT(4)
void SortHeap(V &v) { std::sort_heap(v.begin(), v.end(), Less{}); }
#endif
#if WANT(5)
void QPush(std::priority_queue<Rec, V, Less> &q, const Rec &r) { q.push(r); }
#endif
#if WANT(6)
void QPop(std::priority_queue<Rec, V, Less> &q) { q.pop(); }
#endif
#endif

#if FAM == 2
constexpr size_t kSize[] = {32, 36, 40, 80};
struct Value {
    Value();
    Value(const Value &);
    ~Value();
    Value &operator=(const Value &);
    char bytes[kSize[TI]];
};
using M = std::map<int, Value>;
#if WANT(0)
Value &Index(M &m, int k) { return m[k]; }
#endif
#if WANT(1) && _HAS_CXX17
std::pair<M::iterator, bool> Try(M &m, const int &k) { return m.try_emplace(k); }
#endif
#if WANT(2)
std::pair<M::iterator, bool> Emplace(M &m, int k, const Value &x) { return m.emplace(k, x); }
#endif
#if WANT(3)
std::pair<M::iterator, bool> Insert(M &m, const std::pair<const int, Value> &x) { return m.insert(x); }
#endif
#if WANT(4) && _HAS_CXX17
void Assign(M &m, const int &k, const Value &x) { m.insert_or_assign(k, x); }
#endif
#endif

#if FAM == 3
struct Str {
    virtual ~Str();
    std::string text;
    Str();
    Str(const Str &);
};
#if TI == 1
struct StrLess {
    bool operator()(const Str &a, const Str &b) const { return a.text < b.text; }
};
using M = std::map<Str, int, StrLess>;
#else
inline bool operator<(const Str &a, const Str &b) { return a.text < b.text; }
using M = std::map<Str, int>;
#endif
#if WANT(0)
std::pair<M::iterator, M::iterator> Range(M &m, const Str &k) { return m.equal_range(k); }
#endif
#if WANT(1)
size_t Erase(M &m, const Str &k) { return m.erase(k); }
#endif
#if WANT(2)
M::iterator Find(M &m, const Str &k) { return m.find(k); }
#endif
#if WANT(3)
size_t Count(const M &m, const Str &k) { return m.count(k); }
#endif
#endif

#if FAM == 4
#if TI < 6
using K = std::conditional_t<TI == 0, short, std::conditional_t<TI == 1, unsigned short, std::conditional_t<TI == 2, wchar_t,
          std::conditional_t<TI == 3, uint8_t, std::conditional_t<TI == 4, uint32_t, uint64_t>>>>>;
using U = std::unordered_set<K>;
#else
struct K {
    uint32_t v[TI == 6 ? 3 : 4];
    bool operator==(const K &o) const { return std::memcmp(v, o.v, sizeof v) == 0; }
};
struct KeyHash {
    size_t operator()(const K &k) const;
};
using U = std::unordered_set<K, KeyHash>;
#endif
#if WANT(0)
U *Ctor(void *m) { return new (m) U(); }
#endif
#if WANT(1)
std::pair<U::iterator, bool> Insert(U &s, K k) { return s.insert(k); }
#endif
#if WANT(2)
std::pair<U::iterator, bool> Emplace(U &s, K k) { return s.emplace(k); }
#endif
#if WANT(3)
U::iterator Find(U &s, K k) { return s.find(k); }
#endif
#if WANT(4)
size_t Count(const U &s, K k) { return s.count(k); }
#endif
#if WANT(5)
size_t Erase(U &s, K k) { return s.erase(k); }
#endif
#endif

#if FAM == 5
struct R16 {
    uint32_t v[4];
};
struct R24 {
    uint32_t v[4];
    double d;
};
using T = std::conditional_t<TI == 0, R16, std::conditional_t<TI == 1, R24, void *>>;
using V = std::vector<T>;
#if WANT(0)
void Reserve(V &v, size_t n) { v.reserve(n); }
#endif
#if WANT(1)
void Push(V &v, const T &x) { v.push_back(x); }
#endif
#if WANT(2)
void Resize(V &v, size_t n) { v.resize(n); }
#endif
#if WANT(3)
V::iterator Insert(V &v, V::const_iterator at, const T &x) { return v.insert(at, x); }
#endif
#if WANT(4)
void Emplace(V &v) { v.emplace_back(); }
#endif
#endif
