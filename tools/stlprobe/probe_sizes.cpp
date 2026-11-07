// Sweeps container instantiations over stand-in element types of every plausible size. Most of the
// game's tree/list/vector/deque code is for its own structs; the generated code depends only on the
// element's size, alignment and triviality (node sizes, strides, whether a destructor is called), so an
// N-byte blob stands in for any struct of that size. Compiled by stlprobe.ps1 like probe.cpp.
//   -DPART=n       selects 8 of the sizes 4..128 (one object file takes only so many sections)
//   -DNONTRIVIAL=1 gives the blob a destructor that is declared but never defined, so containers call it
//                  out of line the way they call a game class's destructor
//   -DALLOC=1      builds the containers over ProbeAlloc, a stand-in for the game's allocator-service allocator (our
//                  GZServiceAllocator): allocate(n, hint = 0) and deallocate(p, n) are thiscall members declared but
//                  never defined, so the containers call them out of line as the game's do (lane MJ)
//   -DMAPCLASS=1   instantiates std::map itself instead (its own members: constructors, operator[], the _Try_emplace
//                  they call), for 16 sizes at once (-DPART=4: 4..64, 5: 68..128). In a separate object, because instantiating map
//                  beside _Tree changes how /O2 inlines _Tree's (its destructor becomes a call to a shared body)
#include <cstdint>
#include <cstring>
#include <deque>
#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifndef NONTRIVIAL
#define NONTRIVIAL 0
#endif
#ifndef PART
#define PART 0
#endif
#ifndef ALLOC
#define ALLOC 0
#endif
#ifndef MAPCLASS
#define MAPCLASS 0
#endif

template <class T> struct ProbeAlloc {
    using value_type = T;
    ProbeAlloc() noexcept {}
    template <class U> ProbeAlloc(const ProbeAlloc<U> &) noexcept {}
    T *allocate(size_t n, const void *hint = 0);
    void deallocate(T *p, size_t n);
    template <class U> bool operator==(const ProbeAlloc<U> &) const noexcept { return true; }
    template <class U> bool operator!=(const ProbeAlloc<U> &) const noexcept { return false; }
};
#if ALLOC
#define AL(...) ProbeAlloc<__VA_ARGS__>
#else
#define AL(...) std::allocator<__VA_ARGS__>
#endif

// 4-byte aligned struct of N bytes (N % 4 == 0); the comparisons let set's and list's members instantiate
template <size_t N> struct Blob4 {
    alignas(4) char b[N];
#if NONTRIVIAL
    ~Blob4();
#endif
    bool operator==(const Blob4 &o) const { return memcmp(b, o.b, N) == 0; }
    bool operator<(const Blob4 &o) const { return memcmp(b, o.b, N) < 0; }
};

#if PART == 0
#define SIZES4(X) X(4) X(8) X(12) X(16) X(20) X(24) X(28) X(32)
#elif PART == 1
#define SIZES4(X) X(36) X(40) X(44) X(48) X(52) X(56) X(60) X(64)
#elif PART == 2
#define SIZES4(X) X(68) X(72) X(76) X(80) X(84) X(88) X(92) X(96)
#elif PART == 3
#define SIZES4(X) X(100) X(104) X(108) X(112) X(116) X(120) X(124) X(128)
#elif PART == 4
#define SIZES4(X) X(4) X(8) X(12) X(16) X(20) X(24) X(28) X(32) X(36) X(40) X(44) X(48) X(52) X(56) X(60) X(64)
#elif PART == 5
#define SIZES4(X) X(68) X(72) X(76) X(80) X(84) X(88) X(92) X(96) X(100) X(104) X(108) X(112) X(116) X(120) X(124) X(128)
#endif

#if MAPCLASS
#define MAPS(N) \
    TMAPC(uint32_t, Blob4<N>) TMAPC(int32_t, Blob4<N>) TMAPC(uint16_t, Blob4<N>) TMAPC(uint64_t, Blob4<N>) \
    TMAPC(void *, Blob4<N>) TMAPC(std::string, Blob4<N>) TMAPC(std::wstring, Blob4<N>) TMAPC(float, Blob4<N>)
#define TMAPC(K, V) template class std::map<K, V, std::less<K>, AL(std::pair<K const, V>)>;
SIZES4(MAPS)
#else
// The _Tree base holds the bulk of the code.
#define TMAP(K, V) template class std::_Tree<std::_Tmap_traits<K, V, std::less<K>, AL(std::pair<K const, V>), false>>;
#define TMMAP(K, V) template class std::_Tree<std::_Tmap_traits<K, V, std::less<K>, AL(std::pair<K const, V>), true>>;
#define TSET(K) template class std::_Tree<std::_Tset_traits<K, std::less<K>, AL(K), false>>;
#define TMSET(K) template class std::_Tree<std::_Tset_traits<K, std::less<K>, AL(K), true>>;

#define MAPS(N) \
    TMAP(uint32_t, Blob4<N>) TMAP(int32_t, Blob4<N>) TMAP(uint16_t, Blob4<N>) TMAP(uint64_t, Blob4<N>) \
    TMAP(void *, Blob4<N>) TMAP(std::string, Blob4<N>) TMAP(std::wstring, Blob4<N>) TMAP(float, Blob4<N>) \
    TMMAP(uint32_t, Blob4<N>) TMMAP(std::string, Blob4<N>) TSET(Blob4<N>) TMSET(Blob4<N>)
SIZES4(MAPS)

#define VECS(N) \
    template class std::vector<Blob4<N>, AL(Blob4<N>)>; template class std::list<Blob4<N>, AL(Blob4<N>)>; \
    template class std::deque<Blob4<N>, AL(Blob4<N>)>;
SIZES4(VECS)
#endif
