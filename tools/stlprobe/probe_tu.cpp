// Small translation units for the STL code that matches only when its translation unit instantiates little: in the
// big probes the compiler's inlining budget is spent elsewhere and these come out different. Compiled by stlprobe.ps1,
// once per part, with the usual flag sets and /O1 /Oi.
//   -DPART=0  vector<char>::resize's reallocation (_Resize_reallocate<_Value_init_tag>; the game's /O2 copy 0x152c50)
//   -DPART=1  unordered containers with 4-byte keys (_Hash::_Min_load_factor_buckets; the game's /O1 /Oi copy 0x1440b8)
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifndef PART
#define PART 0
#endif

#if PART == 0
template <class T> void ProbeResize(std::vector<T> &v, size_t n) { v.resize(n); }
template void ProbeResize(std::vector<char> &, size_t);
template void ProbeResize(std::vector<unsigned char> &, size_t);
template void ProbeResize(std::vector<signed char> &, size_t);
#else
template class std::unordered_multimap<uint32_t, uint32_t>;
template class std::unordered_map<uint32_t, uint32_t>;
template class std::unordered_multimap<void *, uint32_t>;
template class std::unordered_map<void *, void *>;
template class std::unordered_multimap<int, int8_t>;
template class std::unordered_multiset<void *>;
template class std::unordered_set<void *>;
#endif
