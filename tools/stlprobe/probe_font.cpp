// WL scratch probe: font / text containers (c++14).
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <list>
#include <vector>
#include <map>
#include <set>

#ifndef OP
#define OP 99
#endif
#define WANT(n) (OP == 99 || OP == (n))

#if WANT(0)
void U16Assign(std::u16string &s, const char16_t *p, size_t n) { s.assign(p, n); }
#endif
#if WANT(1)
std::u16string *U16Ctor(void *m, const char16_t *p, size_t n) { return new (m) std::u16string(p, n); }
#endif
#if WANT(2)
using UM1 = std::unordered_map<uint32_t, std::string>;
UM1 *Um1Ctor(void *m) { return new (m) UM1(); }
std::string &Um1Index(UM1 &m, uint32_t k) { return m[k]; }
#endif
#if WANT(3)
using UM2 = std::unordered_map<std::string, uint32_t>;
UM2 *Um2Ctor(void *m) { return new (m) UM2(); }
uint32_t &Um2Index(UM2 &m, const std::string &k) { return m[k]; }
#endif
#if WANT(4)
using UM3 = std::unordered_map<uint32_t, uint32_t>;
UM3 *Um3Ctor(void *m) { return new (m) UM3(); }
uint32_t &Um3Index(UM3 &m, uint32_t k) { return m[k]; }
#endif
#if WANT(5)
using US = std::unordered_set<uint32_t>;
US *UsCtor(void *m) { return new (m) US(); }
std::pair<US::iterator, bool> UsIns(US &s, uint32_t k) { return s.insert(k); }
#endif
#if WANT(6)
using UM4 = std::unordered_map<std::string, std::string>;
UM4 *Um4Ctor(void *m) { return new (m) UM4(); }
std::string &Um4Index(UM4 &m, const std::string &k) { return m[k]; }
#endif
#if WANT(7)
std::vector<int16_t>::iterator VInsert(std::vector<int16_t> &v, std::vector<int16_t>::iterator at, const int16_t &x) { return v.insert(at, x); }
#endif
#if WANT(8)
std::pair<std::unordered_map<uint32_t, std::string>::iterator, bool> Um1Try(std::unordered_map<uint32_t, std::string> &m, const uint32_t &k) { return m.try_emplace(k); }
#endif
#if WANT(9)
std::pair<std::unordered_map<uint32_t, std::string>::iterator, bool> Um1Emp(std::unordered_map<uint32_t, std::string> &m, uint32_t k, const std::string &v) { return m.emplace(k, v); }
#endif
#if WANT(10)
std::pair<std::unordered_set<uint32_t>::iterator, bool> UsEmp(std::unordered_set<uint32_t> &s, uint32_t k) { return s.emplace(k); }
#endif
#if WANT(11)
std::pair<std::unordered_map<uint32_t, uint32_t>::iterator, bool> Um3Try(std::unordered_map<uint32_t, uint32_t> &m, const uint32_t &k) { return m.try_emplace(k); }
#endif
#if WANT(20)  // the node builder of an unordered_set of 16-bit characters: a small unit inlines it, so ask for it
using NodeAlloc = std::allocator<std::_List_node<uint16_t, void *>>;
template std::_List_node_emplace_op2<NodeAlloc>::_List_node_emplace_op2(NodeAlloc &, const uint16_t &);
#endif
