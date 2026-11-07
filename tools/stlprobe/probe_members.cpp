// Container members that are templates (lane WB): an explicit class instantiation (probe.cpp, probe_sizes.cpp) never
// instantiates them, so the game's out-of-line copies of push_back's _Emplace_reallocate, insert's _Insert_range,
// emplace / try_emplace / insert on trees and hashes, assign and the range constructors had nothing to match. Small
// functions call each the way game code does; the bodies they leave out of line are what libmatch.py matches.
// Compiled by stlprobe.ps1 with $MemberFlagSets.
//   -DPART=0  vectors: scalar elements and trivial 8..64-byte stand-ins
//   -DPART=1  vectors: trivial 68..128-byte stand-ins, and stand-ins with an out-of-line destructor (NONTRIVIAL)
//   -DPART=2  trees: map / multimap over 4-byte keys and N-byte values, set / multiset over scalars
//   -DPART=3  hashes: unordered_map / unordered_multimap over 4-byte keys and N-byte values, string keys
#include <cstdint>
#include <cstring>
#include <list>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#ifndef PART
#define PART 0
#endif

template <size_t N> struct Blob {  // trivial, as probe.cpp's
    alignas(4) char b[N];
};
template <size_t N> struct Blob4 {  // destructor declared, never defined (probe_sizes.cpp's -DNONTRIVIAL=1)
    alignas(4) char b[N];
    ~Blob4();
};

template <class T> struct VecOps {
    using V = std::vector<T>;
    static void PushBack(V &v, const T &x) { v.push_back(x); }
    static void PushBackMove(V &v, T &&x) { v.push_back(std::move(x)); }
    static void EmplaceBack(V &v) { v.emplace_back(); }
    static void Insert(V &v, typename V::const_iterator at, const T &x) { v.insert(at, x); }
    static void InsertN(V &v, typename V::const_iterator at, size_t n, const T &x) { v.insert(at, n, x); }
    static void InsertRange(V &v, typename V::const_iterator at, const T *a, const T *b) { v.insert(at, a, b); }
    static void AssignN(V &v, size_t n, const T &x) { v.assign(n, x); }
    static void AssignRange(V &v, const T *a, const T *b) { v.assign(a, b); }
    static void Resize(V &v, size_t n) { v.resize(n); }
    static void ResizeFill(V &v, size_t n, const T &x) { v.resize(n, x); }
    static V *ConstructRange(void *p, const T *a, const T *b) { return new (p) V(a, b); }
    static V *ConstructN(void *p, size_t n) { return new (p) V(n); }
};

template <class M> struct TreeOps {
    using K = typename M::key_type;
    using VT = typename M::value_type;
    static auto Insert(M &m, const VT &v) { return m.insert(v); }
    static auto InsertMove(M &m, VT &&v) { return m.insert(std::move(v)); }
    static auto InsertHint(M &m, typename M::const_iterator h, const VT &v) { return m.insert(h, v); }
    static auto Emplace(M &m, const VT &v) { return m.emplace(v); }
    static auto Erase(M &m, const K &k) { return m.erase(k); }
    static auto Find(M &m, const K &k) { return m.find(k); }
    static auto LowerBound(M &m, const K &k) { return m.lower_bound(k); }
    static auto UpperBound(M &m, const K &k) { return m.upper_bound(k); }
    static auto EqualRange(M &m, const K &k) { return m.equal_range(k); }
    static auto Count(const M &m, const K &k) { return m.count(k); }
};
template <class M> struct MapOps : TreeOps<M> {
    using K = typename M::key_type;
    static auto TryEmplace(M &m, const K &k) { return m.try_emplace(k); }
    static auto Index(M &m, const K &k) -> decltype(m[k]) { return m[k]; }
};
template <class M> struct HashOps {
    using K = typename M::key_type;
    using VT = typename M::value_type;
    static auto Insert(M &m, const VT &v) { return m.insert(v); }
    static auto InsertMove(M &m, VT &&v) { return m.insert(std::move(v)); }
    static auto Emplace(M &m, const VT &v) { return m.emplace(v); }
    static auto Erase(M &m, const K &k) { return m.erase(k); }
    static auto EraseAt(M &m, typename M::const_iterator it) { return m.erase(it); }
    static auto Find(M &m, const K &k) { return m.find(k); }
    static auto Count(const M &m, const K &k) { return m.count(k); }
    static auto EqualRange(M &m, const K &k) { return m.equal_range(k); }
    static void Rehash(M &m, size_t n) { m.rehash(n); }
    static M *Construct(void *p) { return new (p) M(); }
    static M *Copy(void *p, const M &o) { return new (p) M(o); }
    static void Assign(M &m, const M &o) { m = o; }
};
template <class M> struct UmapOps : HashOps<M> {
    using K = typename M::key_type;
    static auto TryEmplace(M &m, const K &k) { return m.try_emplace(k); }
    static auto Index(M &m, const K &k) -> decltype(m[k]) { return m[k]; }
};

#define SIZES_A(X) X(8) X(12) X(16) X(20) X(24) X(28) X(32) X(36) X(40) X(44) X(48) X(52) X(56) X(60) X(64)
#define SIZES_B(X) X(68) X(72) X(76) X(80) X(84) X(88) X(92) X(96) X(100) X(104) X(108) X(112) X(116) X(120) X(124) X(128)
#define SIZES_ALL(X) X(4) SIZES_A(X) SIZES_B(X)

#if PART == 0
template struct VecOps<char>;
template struct VecOps<uint8_t>;
template struct VecOps<int16_t>;
template struct VecOps<uint16_t>;
template struct VecOps<int32_t>;
template struct VecOps<uint32_t>;
template struct VecOps<float>;
template struct VecOps<double>;
template struct VecOps<uint64_t>;
template struct VecOps<void *>;
template struct VecOps<const char *>;
#define VEC_T(N) template struct VecOps<Blob<N>>;
SIZES_A(VEC_T)
#elif PART == 1
#define VEC_T(N) template struct VecOps<Blob<N>>;
SIZES_B(VEC_T)
#define VEC_NT(N) template struct VecOps<Blob4<N>>;
SIZES_ALL(VEC_NT)
template struct VecOps<std::string>;
#elif PART == 2
#define TREES(N) \
    template struct MapOps<std::map<uint32_t, Blob<N>>>; template struct MapOps<std::map<int32_t, Blob<N>>>; \
    template struct TreeOps<std::multimap<uint32_t, Blob<N>>>; template struct TreeOps<std::multimap<int32_t, Blob<N>>>; \
    template struct MapOps<std::map<uint16_t, Blob<N>>>;
SIZES_ALL(TREES)
template struct TreeOps<std::set<uint32_t>>;
template struct TreeOps<std::set<int32_t>>;
template struct TreeOps<std::set<uint16_t>>;
template struct TreeOps<std::set<void *>>;
template struct TreeOps<std::set<uint64_t>>;
template struct TreeOps<std::multiset<uint32_t>>;
template struct TreeOps<std::multiset<int32_t>>;
template struct TreeOps<std::multiset<void *>>;
template struct MapOps<std::map<std::string, uint32_t>>;
template struct MapOps<std::map<std::string, std::string>>;
template struct MapOps<std::map<uint32_t, std::string>>;
template struct MapOps<std::map<int32_t, std::string>>;
template struct TreeOps<std::set<std::string>>;
#elif PART == 3
#define HASHES(N) \
    template struct UmapOps<std::unordered_map<uint32_t, Blob<N>>>; \
    template struct HashOps<std::unordered_multimap<uint32_t, Blob<N>>>; \
    template struct UmapOps<std::unordered_map<int32_t, Blob<N>>>;
SIZES_ALL(HASHES)
template struct HashOps<std::unordered_set<uint32_t>>;
template struct HashOps<std::unordered_set<void *>>;
template struct HashOps<std::unordered_multiset<uint32_t>>;
template struct UmapOps<std::unordered_map<std::string, uint32_t>>;
template struct UmapOps<std::unordered_map<std::string, std::string>>;
template struct UmapOps<std::unordered_map<std::string, void *>>;
template struct UmapOps<std::unordered_map<uint32_t, std::string>>;
#endif
