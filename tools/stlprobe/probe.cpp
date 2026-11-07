// Explicitly instantiates common STL templates so tools/libmatch.py can find the game's own copies
// of them (the game compiled these from the same MSVC 14.40 headers). Compiled by stlprobe.ps1.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <list>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

template class std::basic_string<char>;
template class std::basic_string<wchar_t>;
template class std::basic_string<char16_t>;

template class std::vector<char>;
template class std::vector<uint8_t>;
template class std::vector<int16_t>;
template class std::vector<uint16_t>;
template class std::vector<int32_t>;
template class std::vector<uint32_t>;
template class std::vector<uint64_t>;
template class std::vector<float>;
template class std::vector<double>;
template class std::vector<void *>;
template class std::vector<const char *>;
template class std::vector<std::string>;
template class std::vector<bool>;

template class std::list<uint32_t>;
template class std::list<void *>;
template class std::deque<uint32_t>;
template class std::deque<void *>;

template class std::map<uint32_t, uint32_t>;
template class std::map<uint32_t, void *>;
template class std::map<std::string, uint32_t>;
template class std::map<std::string, std::string>;
template class std::map<std::string, void *>;
template class std::map<int32_t, int32_t>;
template class std::map<int32_t, uint32_t>;
template class std::map<int32_t, void *>;
template class std::map<uint16_t, uint32_t>;
template class std::map<uint16_t, void *>;
template class std::map<uint64_t, uint32_t>;
template class std::map<uint64_t, void *>;
template class std::map<uint32_t, std::string>;
template class std::map<std::wstring, uint32_t>;
template class std::map<std::wstring, void *>;
template class std::set<int32_t>;
template class std::set<uint16_t>;
template class std::set<uint64_t>;
template class std::multimap<int32_t, void *>;
template class std::multimap<std::string, void *>;
template class std::set<uint32_t>;
template class std::set<void *>;
template class std::set<std::string>;
template class std::multimap<uint32_t, uint32_t>;
template class std::multimap<uint32_t, void *>;

template class std::unordered_map<uint32_t, uint32_t>;
template class std::unordered_map<uint32_t, void *>;
template class std::unordered_map<std::string, uint32_t>;
template class std::unordered_set<uint32_t>;

// An explicit instantiation of map/set/multimap covers only the derived class's own members; the bulk
// of the code (erase, insert, _Extract, _Find_hint, ...) lives in the _Tree base, so instantiate that.
#define TREE_MAP(K, V) \
    template class std::_Tree<std::_Tmap_traits<K, V, std::less<K>, std::allocator<std::pair<const K, V>>, false>>;
#define TREE_MMAP(K, V) \
    template class std::_Tree<std::_Tmap_traits<K, V, std::less<K>, std::allocator<std::pair<const K, V>>, true>>;
#define TREE_SET(K) \
    template class std::_Tree<std::_Tset_traits<K, std::less<K>, std::allocator<K>, false>>;
#define TREE_MSET(K) \
    template class std::_Tree<std::_Tset_traits<K, std::less<K>, std::allocator<K>, true>>;

TREE_MAP(uint32_t, uint32_t)
TREE_MAP(uint32_t, void *)
TREE_MAP(std::string, uint32_t)
TREE_MAP(std::string, std::string)
TREE_MAP(std::string, void *)
TREE_MAP(int32_t, int32_t)
TREE_MAP(int32_t, uint32_t)
TREE_MAP(int32_t, void *)
TREE_MAP(uint16_t, uint32_t)
TREE_MAP(uint16_t, void *)
TREE_MAP(uint64_t, uint32_t)
TREE_MAP(uint64_t, void *)
TREE_MAP(uint32_t, std::string)
TREE_MAP(std::wstring, uint32_t)
TREE_MAP(std::wstring, void *)
TREE_MMAP(int32_t, void *)
TREE_MMAP(std::string, void *)
TREE_MMAP(uint32_t, uint32_t)
TREE_MMAP(uint32_t, void *)
TREE_SET(int32_t)
TREE_SET(uint16_t)
TREE_SET(uint64_t)
TREE_SET(uint32_t)
TREE_SET(void *)
TREE_SET(std::string)

// Member templates (resize, append, operator[]), unordered multi containers and two of the game's own
// std::sort instantiations (lane HL). Explicit class instantiation does not instantiate member
// templates, so these are reached through small functions that use them the way the game does.
#pragma intrinsic(strcmp)

// NameValueTable (0xaf6560): vector<pair<string, uint32_t>>, push_back, std::sort with an out-of-line comparator.
using NvtEntry = std::pair<std::string, uint32_t>;
struct NvtLess {
    bool operator()(const NvtEntry &a, const NvtEntry &b) const;
};
void ProbeNameValueTable(std::vector<NvtEntry> &v, const char *name, uint32_t value)
{
    v.push_back(NvtEntry(std::string(name), value));
    std::sort(v.begin(), v.end(), NvtLess());
}

// The expression evaluator's function table (0xb094b0): vector<pair<const char*, T*>> sorted by strcmp.
struct ProbeEval;
using NameEval = std::pair<const char *, ProbeEval *>;
struct NameEvalLess {
    bool operator()(const NameEval &a, const NameEval &b) const { return strcmp(a.first, b.first) < 0; }
};
void ProbeNameEval(std::vector<NameEval> &v, size_t n)
{
    v.resize(n);
    std::sort(v.begin(), v.end(), NameEvalLess());
}

template <size_t N> struct Blob {
    alignas(4) char b[N];
};

// vector::resize (value-init reallocate) for trivial element sizes.
template <class T> void ProbeResize(std::vector<T> &v, size_t n) { v.resize(n); }
template void ProbeResize(std::vector<char> &, size_t);
template void ProbeResize(std::vector<unsigned char> &, size_t);
template void ProbeResize(std::vector<int16_t> &, size_t);
template void ProbeResize(std::vector<uint32_t> &, size_t);
template void ProbeResize(std::vector<float> &, size_t);
template void ProbeResize(std::vector<double> &, size_t);
template void ProbeResize(std::vector<void *> &, size_t);
template void ProbeResize(std::vector<Blob<8>> &, size_t);
template void ProbeResize(std::vector<Blob<12>> &, size_t);
template void ProbeResize(std::vector<Blob<16>> &, size_t);
template void ProbeResize(std::vector<Blob<20>> &, size_t);
template void ProbeResize(std::vector<Blob<24>> &, size_t);
template void ProbeResize(std::vector<Blob<32>> &, size_t);
template void ProbeResize(std::vector<std::string> &, size_t);

// basic_string members that are templates (append(n, ch) -> _Reallocate_grow_by).
void ProbeStringAppend(std::string &s, size_t n, char c) { s.append(n, c); }
void ProbeStringResize(std::string &s, size_t n, char c) { s.resize(n, c); }
void ProbeStringAppendPtr(std::string &s, const char *p, size_t n) { s.append(p, n); }
void ProbeStringInsert(std::string &s, size_t at, const char *p, size_t n) { s.insert(at, p, n); }

// map<string,string> operator[] with an rvalue key (_Tree_temp_node with piecewise_construct).
std::string &ProbeMapIndex(std::map<std::string, std::string> &m, std::string &&k) { return m[std::move(k)]; }
std::string &ProbeMapIndexL(std::map<std::string, std::string> &m, const std::string &k) { return m[k]; }
uint32_t &ProbeMapIndexU(std::map<uint32_t, uint32_t> &m, const uint32_t &k) { return m[k]; }
std::string &ProbeMapIndexUS(std::map<uint32_t, std::string> &m, const uint32_t &k) { return m[k]; }

template class std::unordered_multimap<uint32_t, uint32_t>;
template class std::unordered_multimap<uint32_t, void *>;
template class std::unordered_multiset<uint32_t>;
template class std::unordered_set<void *>;
template class std::unordered_map<uint32_t, std::string>;
template class std::unordered_map<std::string, std::string>;
template class std::unordered_map<std::string, void *>;
template class std::_Hash<std::_Umap_traits<uint32_t, uint32_t, std::_Uhash_compare<uint32_t, std::hash<uint32_t>, std::equal_to<uint32_t>>, std::allocator<std::pair<const uint32_t, uint32_t>>, true>>;
template class std::_Hash<std::_Umap_traits<uint32_t, void *, std::_Uhash_compare<uint32_t, std::hash<uint32_t>, std::equal_to<uint32_t>>, std::allocator<std::pair<const uint32_t, void *>>, true>>;
template class std::_Hash<std::_Umap_traits<uint32_t, uint32_t, std::_Uhash_compare<uint32_t, std::hash<uint32_t>, std::equal_to<uint32_t>>, std::allocator<std::pair<const uint32_t, uint32_t>>, false>>;
template class std::_Hash<std::_Umap_traits<uint32_t, void *, std::_Uhash_compare<uint32_t, std::hash<uint32_t>, std::equal_to<uint32_t>>, std::allocator<std::pair<const uint32_t, void *>>, false>>;
template class std::_Hash<std::_Umap_traits<std::string, uint32_t, std::_Uhash_compare<std::string, std::hash<std::string>, std::equal_to<std::string>>, std::allocator<std::pair<const std::string, uint32_t>>, false>>;
template class std::_Hash<std::_Uset_traits<uint32_t, std::_Uhash_compare<uint32_t, std::hash<uint32_t>, std::equal_to<uint32_t>>, std::allocator<uint32_t>, false>>;
template class std::_Hash<std::_Uset_traits<uint32_t, std::_Uhash_compare<uint32_t, std::hash<uint32_t>, std::equal_to<uint32_t>>, std::allocator<uint32_t>, true>>;
