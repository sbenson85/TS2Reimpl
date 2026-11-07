// One container member per translation unit (lane WB). The compiler inlines a helper (_Change_array,
// _Allocate_at_least_helper, _Tree_temp_node's constructor...) into its caller when the unit calls it from one place
// only, so the game's out-of-line copies of push_back's _Emplace_reallocate, insert's _Emplace, try_emplace and the
// like come in several shapes for one element type, depending on what else their unit used: 0x2b36f0, 0x4bfda0 and
// 0xc0b53a are all vector<12-byte trivial>::_Emplace_reallocate. probe_members.cpp gives the many-members shape; this
// gives the shapes with one member (-DOP=n) or all of a family's members (-DOP=99) for one type (-DTI=n).
//   -DFAM=0  vector<T>                    T: scalars, then trivial N-byte stand-ins, then ones with a destructor
//   -DFAM=1  map<uint32_t, Blob<N>>       2: map<int32_t, ...>   3: multimap<uint32_t, ...>   4: multimap<int32_t, ...>
//   -DFAM=5  unordered_map<uint32_t, Blob<N>>                    6: unordered_multimap<uint32_t, ...>
//   -DFAM=7  set<K> / multiset<K> over scalars (TI picks K and the multi flag)
//   -DFAM=8  std::string's members (TI 0 only; OP picks the member)
//   -DFAM=9  vector<T> over std::string and N-byte elements that start with one (StrTypes)
//   -DFAM=10 map<K, V> with string and scalar keys and values      11: unordered_map<K, V> over the same pairs
// stlprobe.ps1 builds every (FAM, TI, OP) its tables name; the sizes are the TI-th of SIZES below.
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
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

template <size_t N> struct Blob {  // trivial, as probe.cpp's
    alignas(4) char b[N];
};
template <size_t N> struct Blob4 {  // destructor declared, never defined (probe_sizes.cpp's -DNONTRIVIAL=1)
    alignas(4) char b[N];
    ~Blob4();
};
template <class... T> struct TypeList {
    template <size_t I> using at = std::tuple_element_t<I, std::tuple<T...>>;
};
// The N of Blob<N> for the map and hash families, and of the vector family's stand-ins (TI - 11 for trivial,
// TI - 11 - 16 for the ones with a destructor).
#define BLOBS(B) B<4>, B<8>, B<12>, B<16>, B<20>, B<24>, B<28>, B<32>, B<36>, B<40>, B<44>, B<48>, B<56>, B<64>, B<80>, B<96>
using Values = TypeList<BLOBS(Blob)>;
using VecTypes = TypeList<char, uint8_t, int16_t, uint16_t, int32_t, uint32_t, float, double, uint64_t, void *,
                          const char *, BLOBS(Blob), BLOBS(Blob4)>;
template <size_t N> struct StrBlob {  // an N-byte element with a std::string first (its destructor is the string's)
    std::string s;
    alignas(4) char b[N - sizeof(std::string)];
};
using StrTypes = TypeList<std::string, StrBlob<28>, StrBlob<32>, StrBlob<36>, StrBlob<40>, StrBlob<48>, StrBlob<56>,
                          StrBlob<64>>;

#if FAM == 0 || FAM == 9
#if FAM == 0
using T = VecTypes::at<TI>;
#else
using T = StrTypes::at<TI>;
#endif
using V = std::vector<T>;
#if WANT(0)
void PushBack(V &v, const T &x) { v.push_back(x); }
#endif
#if WANT(1)
void PushBackMove(V &v, T &&x) { v.push_back(std::move(x)); }
#endif
#if WANT(2)
void EmplaceBack(V &v) { v.emplace_back(); }
#endif
#if WANT(3)
void Insert(V &v, V::const_iterator at, const T &x) { v.insert(at, x); }
#endif
#if WANT(4)
void InsertN(V &v, V::const_iterator at, size_t n, const T &x) { v.insert(at, n, x); }
#endif
#if WANT(5)
void InsertRange(V &v, V::const_iterator at, const T *a, const T *b) { v.insert(at, a, b); }
#endif
#if WANT(6)
void AssignN(V &v, size_t n, const T &x) { v.assign(n, x); }
#endif
#if WANT(7)
void AssignRange(V &v, const T *a, const T *b) { v.assign(a, b); }
#endif
#if WANT(8)
void Resize(V &v, size_t n) { v.resize(n); }
#endif
#if WANT(9)
void ResizeFill(V &v, size_t n, const T &x) { v.resize(n, x); }
#endif
#if WANT(10)
V *ConstructRange(void *p, const T *a, const T *b) { return new (p) V(a, b); }
#endif
#if WANT(11)
V *ConstructN(void *p, size_t n) { return new (p) V(n); }
#endif
#if WANT(12)
void Copy(V &v, const V &o) { v = o; }
#endif
#if WANT(13)
V *CopyConstruct(void *p, const V &o) { return new (p) V(o); }
#endif
#if WANT(14)
void Reserve(V &v, size_t n) { v.reserve(n); }
#endif
#if WANT(15)
void Erase(V &v, V::const_iterator a, V::const_iterator b) { v.erase(a, b); }
#endif
#if WANT(16)
void Destroy(V &v) { v.~V(); }
#endif
#if WANT(17)
void Clear(V &v) { v.clear(); }
#endif

#elif FAM == 8
using S = std::string;
using P = S::size_type;
#if WANT(0)
P FindCh(const S &s, char c, P at) { return s.find(c, at); }
#endif
#if WANT(1)
P FindSz(const S &s, const char *p, P at) { return s.find(p, at); }
#endif
#if WANT(2)
P FindN(const S &s, const char *p, P at, P n) { return s.find(p, at, n); }
#endif
#if WANT(3)
P FindStr(const S &s, const S &o, P at) { return s.find(o, at); }
#endif
#if WANT(4)
P RfindCh(const S &s, char c, P at) { return s.rfind(c, at); }
#endif
#if WANT(5)
P RfindSz(const S &s, const char *p, P at) { return s.rfind(p, at); }
#endif
#if WANT(6)
P FirstOf(const S &s, const char *p, P at) { return s.find_first_of(p, at); }
#endif
#if WANT(7)
P LastOf(const S &s, const char *p, P at) { return s.find_last_of(p, at); }
#endif
#if WANT(8)
P FirstNotOf(const S &s, const char *p, P at) { return s.find_first_not_of(p, at); }
#endif
#if WANT(9)
P LastNotOf(const S &s, const char *p, P at) { return s.find_last_not_of(p, at); }
#endif
#if WANT(10)
int CompareStr(const S &s, const S &o) { return s.compare(o); }
#endif
#if WANT(11)
int CompareSz(const S &s, const char *p) { return s.compare(p); }
#endif
#if WANT(12)
int CompareSub(const S &s, P at, P n, const char *p) { return s.compare(at, n, p); }
#endif
#if WANT(13)
S &InsertStr(S &s, P at, const S &o) { return s.insert(at, o); }
#endif
#if WANT(14)
S &InsertSz(S &s, P at, const char *p) { return s.insert(at, p); }
#endif
#if WANT(15)
S &AppendSz(S &s, const char *p) { return s.append(p); }
#endif
#if WANT(16)
S &AppendStr(S &s, const S &o) { return s.append(o); }
#endif
#if WANT(17)
S &AssignSz(S &s, const char *p) { return s.assign(p); }
#endif
#if WANT(18)
S &AssignStr(S &s, const S &o) { return s.assign(o); }
#endif
#if WANT(19)
S &ReplaceSz(S &s, P at, P n, const char *p) { return s.replace(at, n, p); }
#endif
#if WANT(20)
S Substr(const S &s, P at, P n) { return s.substr(at, n); }
#endif
#if WANT(21)
S &EraseAt(S &s, P at, P n) { return s.erase(at, n); }
#endif
#if WANT(22)
void Resize(S &s, P n) { s.resize(n); }
#endif
#if WANT(23)
S &AppendN(S &s, P n, char c) { return s.append(n, c); }
#endif
#if WANT(24)
void PushBack(S &s, char c) { s.push_back(c); }
#endif
#if WANT(25)
S PlusSz(const S &s, const char *p) { return s + p; }
#endif
#if WANT(26)
P FirstOfStr(const S &s, const S &o, P at) { return s.find_first_of(o, at); }
#endif
#if WANT(27)
S *ConstructSz(void *m, const char *p) { return new (m) S(p); }
#endif

#elif (FAM >= 1 && FAM <= 7) || FAM == 10 || FAM == 11
#if FAM == 1
using M = std::map<uint32_t, Values::at<TI>>;
#elif FAM == 2
using M = std::map<int32_t, Values::at<TI>>;
#elif FAM == 3
using M = std::multimap<uint32_t, Values::at<TI>>;
#elif FAM == 4
using M = std::multimap<int32_t, Values::at<TI>>;
#elif FAM == 5
using M = std::unordered_map<uint32_t, Values::at<TI>>;
#elif FAM == 6
using M = std::unordered_multimap<uint32_t, Values::at<TI>>;
#elif FAM == 7
using M = TypeList<std::set<uint32_t>, std::set<int32_t>, std::set<uint16_t>, std::set<void *>, std::set<uint64_t>,
                   std::multiset<uint32_t>, std::multiset<int32_t>, std::multiset<void *>>::at<TI>;
#elif FAM == 10
using M = TypeList<std::map<uint32_t, std::string>, std::map<int32_t, std::string>, std::map<int32_t, int32_t>,
                   std::map<uint32_t, uint32_t>, std::map<std::string, uint32_t>, std::map<std::string, std::string>,
                   std::map<std::string, void *>, std::map<std::string, Blob<8>>>::at<TI>;
#elif FAM == 11
using M = TypeList<std::unordered_map<uint32_t, std::string>, std::unordered_map<int32_t, std::string>,
                   std::unordered_map<int32_t, int32_t>, std::unordered_map<uint32_t, uint32_t>,
                   std::unordered_map<std::string, uint32_t>, std::unordered_map<std::string, std::string>,
                   std::unordered_map<std::string, void *>, std::unordered_map<std::string, Blob<8>>>::at<TI>;
#endif
using K = M::key_type;
using VT = M::value_type;
#if WANT(0)
auto Insert(M &m, const VT &v) { return m.insert(v); }
#endif
#if WANT(1)
auto InsertMove(M &m, VT &&v) { return m.insert(std::move(v)); }
#endif
#if WANT(2)
auto Emplace(M &m, const VT &v) { return m.emplace(v); }
#endif
#if WANT(3)
auto Erase(M &m, const K &k) { return m.erase(k); }
#endif
#if WANT(4)
auto EraseAt(M &m, M::const_iterator it) { return m.erase(it); }
#endif
#if WANT(5)
auto Find(M &m, const K &k) { return m.find(k); }
#endif
#if WANT(6)
auto EqualRange(M &m, const K &k) { return m.equal_range(k); }
#endif
#if WANT(7)
M *CopyConstruct(void *p, const M &o) { return new (p) M(o); }
#endif
#if WANT(8)
void Assign(M &m, const M &o) { m = o; }
#endif
#if WANT(9)
void Destroy(M &m) { m.~M(); }
#endif
#if WANT(10)
void Clear(M &m) { m.clear(); }
#endif
#if FAM == 1 || FAM == 2 || FAM == 5 || FAM == 10 || FAM == 11
#if WANT(11)
auto TryEmplace(M &m, const K &k) { return m.try_emplace(k); }
#endif
#if WANT(12)
auto Index(M &m, const K &k) -> decltype(m[k]) { return m[k]; }
#endif
#endif
#if FAM < 5 || FAM == 7 || FAM == 10
#if WANT(13)
auto InsertHint(M &m, M::const_iterator h, const VT &v) { return m.insert(h, v); }
#endif
#if WANT(14)
auto LowerBound(M &m, const K &k) { return m.lower_bound(k); }
#endif
#else
#if WANT(13)
M *Construct(void *p) { return new (p) M(); }
#endif
#if WANT(14)
void Rehash(M &m, size_t n) { m.rehash(n); }
#endif
#endif
#endif
