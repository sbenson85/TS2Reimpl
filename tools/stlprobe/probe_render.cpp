// The rendering campaign's container instantiations (lane WJ, rendering row RL): std containers over element types
// laid out as the game's (sizes, member order, which members have constructors), with comparators shaped as the
// original's (inline strcmp, extern thiscall functors, an extern cdecl key). Game functions a template calls (element
// copy constructors and assignments, the comparators) are declared and never defined, so they stay calls (relocations,
// masked). One member per unit (-DOP=n) or all of a family's (-DOP=99), as probe_one.cpp, with stlprobe.ps1's
// $RenderFlagSets, in C++14 mode (/std:c++14: the game's units; under C++17 pair's operator< and several loops differ).
//   -DFAM=0  vector<ConstRec> (0x44: a 0x2c-byte descriptor, then a std::string); odd TI: vector<SampRec> (100 bytes);
//            TI 0/1 compare names inline (strcmp), TI 2/3 out of line
//   -DFAM=1  vector<Shader*> sorted and searched by an extern KeyLess over keys built inline (even TI vertex, odd pixel);
//            TI 0/1 define the (Shader*, Shader*) comparison inline, TI 2/3 declare it
//   -DFAM=2  vector<CountedRef<I>> (AddRef / Release through slots 1 / 2); TI 2, 3: a COM interface (stdcall);
//            OP 4: explicit vector instantiation
//   -DFAM=3  set<Str*, NameLess> (Str: a vtable, then a std::string; length first, then the text)
//   -DFAM=4  vector<vector<Elem16>> and vector<Elem16> sorted by a cdecl SortKey (TI 0 extern, TI 1 defined in the unit:
//            the original's partition matches only so); TI bits 1..3 keep the allocate helper, the value construct and
//            _Destroy_range as calls, 4 and 5 define the value construct and the helper, not inlined: the reallocation
//            matches only with both defined (TI 56), since the compiler then knows there is no throw and the count holds
//   -DFAM=5  vector<Record60> (a 0x3c-byte record holding a vector) and vector<pair<uint32, uint32>> sorted by greater<>
//   -DFAM=6  vector<FPair> (0x48: a vtable, a float, two 0x20-byte strings with extern copy constructor and assignment);
//            TI 1: FPair's copy and assignment written out; TI 2: the string's copy constructor inline, delegating
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <new>
#include <set>
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
struct ConstDesc {
    const char *name;
    uint32_t v[10];
};
struct ConstRec {
    ConstDesc desc;
    std::string name;
};
struct SampDesc {
    const char *name;
    uint32_t count;
    const uint32_t *registers;
};
struct SampRec {
    SampDesc desc;
    std::string name;
    uint32_t registers[16];
};
static_assert(sizeof(ConstRec) == 0x44 && sizeof(SampRec) == 100, "records");
using T = std::conditional_t<TI % 2 == 0, ConstRec, SampRec>;
#if TI < 2
inline bool NameBefore(const char *a, const char *b) { return std::strcmp(a, b) < 0; }
struct ByName {
    bool operator()(const T &r, const char *name) const { return NameBefore(r.name.c_str(), name); }
    bool operator()(const char *name, const T &r) const { return NameBefore(name, r.name.c_str()); }
    bool operator()(const T &a, const T &b) const { return NameBefore(a.name.c_str(), b.name.c_str()); }
};
#else
struct ByName {  // TI 2/3: the comparisons out of line
    bool operator()(const T &r, const char *name) const;
    bool operator()(const char *name, const T &r) const;
    bool operator()(const T &a, const T &b) const;
};
#endif
using V = std::vector<T>;
#if WANT(0)
void PushBack(V &v, const T &x) { v.push_back(x); }
#endif
#if WANT(1)
void PushBackMove(V &v, T &&x) { v.push_back(std::move(x)); }
#endif
#if WANT(2)
void Sort(V &v) { std::sort(v.begin(), v.end(), ByName{}); }
#endif
#if WANT(3)
std::pair<V::iterator, V::iterator> Range(V &v, const char *name) { return std::equal_range(v.begin(), v.end(), name, ByName{}); }
#endif
#if WANT(4)
void Destroy(V *v) { v->~V(); }
#endif
#if WANT(5)
void Clear(V &v) { v.clear(); }
#endif
#if WANT(6)
V::iterator Lower(V &v, const char *name) { return std::lower_bound(v.begin(), v.end(), name, ByName{}); }
#endif
#endif

#if FAM == 1
struct Ref {
    void *p;
};
struct Key {
    void *device;
    uint32_t count;
    const Ref *names;
    void *buffer;
    uint8_t software, hardware;
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
inline Key KeyOf(const S *s)
{
    Key k;
    k.device = s->device;
    k.count = static_cast<uint32_t>(s->names.size());
    k.names = s->names.empty() ? nullptr : s->names.data();
    k.buffer = s->buffer;
#if TI % 2 == 0
    k.software = s->software;
    k.hardware = s->hardware;
#else
    k.software = 0;
    k.hardware = 1;
#endif
    return k;
}
struct Compare {
    bool Less(const Key &a, const Key &b) const;  // the original's 0x1747d0 (thiscall)
    bool operator()(const S *s, const Key &k) const { return Less(KeyOf(s), k); }
    bool operator()(const Key &k, const S *s) const { return Less(k, KeyOf(s)); }
#if TI < 2  // 0x1749b0 / 0x174920: defined inline, kept out of line where the compiler chose
    bool operator()(const S *a, const S *b) const { return Less(KeyOf(a), KeyOf(b)); }
#else
    bool operator()(const S *a, const S *b) const;
#endif
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
#endif

#if FAM == 2
#if TI & 2
struct I {  // a COM interface: stdcall
    virtual long __stdcall QueryInterface(uint32_t, void **) = 0;
    virtual unsigned long __stdcall AddRef() = 0;
    virtual unsigned long __stdcall Release() = 0;
};
#else
struct I {
    virtual bool QueryInterface(uint32_t, void **) = 0;
    virtual uint32_t AddRef() = 0;
    virtual uint32_t Release() = 0;
};
#endif
struct CountedRef {
    I *p = nullptr;
    CountedRef() = default;
    explicit CountedRef(I *q) : p(q)
    {
        if (p) p->AddRef();
    }
    CountedRef(const CountedRef &o) : p(o.p)
    {
        if (p) p->AddRef();
    }
    CountedRef &operator=(const CountedRef &o)
    {
        if (o.p) o.p->AddRef();
        I *old = p;
        p = o.p;
        if (old) old->Release();
        return *this;
    }
    ~CountedRef()
    {
        if (p) p->Release();
    }
};
using V = std::vector<CountedRef>;
#if WANT(0)
void EmplaceBack(V &v, I *p) { v.emplace_back(p); }
#endif
#if WANT(1)
void PushBack(V &v, const CountedRef &r) { v.push_back(r); }
#endif
#if WANT(2)
void Destroy(V *v) { v->~V(); }
#endif
#if WANT(3)
void Clear(V &v) { v.clear(); }
#endif
#if OP == 4
template class std::vector<CountedRef>;
#endif
#endif

#if FAM == 3
struct Str {
    void *vtbl;
    std::string s;
    uint32_t refs;
};
struct NameLess {
    bool operator()(const Str *a, const Str *b) const
    {
        if (a->s.size() != b->s.size()) return a->s.size() < b->s.size();
        return a->s < b->s;
    }
};
using Set = std::set<Str *, NameLess>;
#if WANT(0)
std::pair<Set::iterator, bool> Insert(Set &s, Str *x) { return s.insert(x); }
#endif
#if WANT(1)
Set::iterator Find(Set &s, Str *x) { return s.find(x); }
#endif
#if WANT(2)
void Destroy(Set *s) { s->~Set(); }
#endif
#if WANT(3)
void Clear(Set &s) { s.clear(); }
#endif
#endif

#if FAM == 4
struct Elem16 {
    uint32_t usage;
    uint8_t index;
    uint32_t type;
    uint8_t stream, position;
};
static_assert(sizeof(Elem16) == 16, "element");
#if !(TI & 1)
int32_t SortKey(const Elem16 &e);
#else  // TI 1: defined in the unit (not inlined), so the compiler knows it writes no memory
__declspec(noinline) int32_t SortKey(const Elem16 &e) { return e.usage == 1u ? static_cast<int32_t>(e.index) : -1; }
#endif
using Stream = std::vector<Elem16>;
using V = std::vector<Stream>;
#if TI & 2
template <> Stream *std::_Allocate_at_least_helper<std::allocator<Stream>>(std::allocator<Stream> &, size_t &);
#endif
#if TI & 32  // defined, so the compiler knows the count comes back unchanged
template <> __declspec(noinline) Stream *std::_Allocate_at_least_helper<std::allocator<Stream>>(std::allocator<Stream> &al, size_t &count)
{
    return al.allocate(count);
}
#endif
#if TI & 4
template <> Stream *std::_Uninitialized_value_construct_n<std::allocator<Stream>>(Stream *, size_t, std::allocator<Stream> &);
#endif
#if TI & 16  // defined, so the compiler knows it does not throw: the reallocation then needs no handler
template <> __declspec(noinline) Stream *std::_Uninitialized_value_construct_n<std::allocator<Stream>>(Stream *first, size_t n, std::allocator<Stream> &)
{
    for (; n != 0; --n, ++first) new (first) Stream();
    return first;
}
#endif
#if TI & 8
template <> void std::_Destroy_range<std::allocator<Stream>>(Stream *, Stream *const, std::allocator<Stream> &) noexcept;
#endif
#if WANT(0)
void Copy(V *to, const V &from) { new (to) V(from); }
#endif
#if WANT(1)
void Resize(V &v, size_t n) { v.resize(n); }
#endif
#if WANT(2)
void Destroy(V *v) { v->~V(); }
#endif
#if WANT(3)
void PushBack(Stream &s, const Elem16 &e) { s.push_back(e); }
#endif
#if WANT(4)
void Sort(Stream &s) { std::sort(s.begin(), s.end(), [](const Elem16 &a, const Elem16 &b) { return SortKey(a) < SortKey(b); }); }
#endif
#if WANT(5)
void Assign(V &v, const V &from) { v = from; }
#endif
#if WANT(6)
void PushStream(V &v, const Stream &s) { v.push_back(s); }
#endif
#endif

#if FAM == 5
struct CellRef {
    uint32_t atlas, region;
};
struct Record {
    int32_t refCount;
    uint32_t width, height, c, d;
    uint8_t bufferUncounted;
    void *buffer;
    uint32_t word1c;
    uint32_t columns, rows, pool;
    std::vector<CellRef> cells;
    uint8_t borderX, borderY;
};
static_assert(sizeof(Record) == 0x3c, "record");
using V = std::vector<Record>;
using P = std::vector<std::pair<uint32_t, uint32_t>>;
#if WANT(0)
void EmplaceBack(V &v) { v.emplace_back(); }
#endif
#if WANT(1)
void PushBackMove(V &v, Record &&r) { v.push_back(std::move(r)); }
#endif
#if WANT(2)
void Destroy(V *v) { v->~V(); }
#endif
#if WANT(3)
void Sort(P &p) { std::sort(p.begin(), p.end(), std::greater<>()); }
#endif
#if WANT(4)
void PushPair(P &p, const std::pair<uint32_t, uint32_t> &x) { p.push_back(x); }
#endif
#if WANT(5)
void PushBack(V &v, const Record &r) { v.push_back(r); }
#endif
#if WANT(6)
void SortPairsLess(P &p) { std::sort(p.begin(), p.end(), std::greater<std::pair<uint32_t, uint32_t>>()); }
#endif
#endif

#if FAM == 6
struct ZStr {
    void *vtbl;
    char body[0x18];
    uint32_t refs;
#if TI < 2
    ZStr(const ZStr &o, uint32_t pos = 0, uint32_t count = 0xffffffff);
#else  // TI 2: an inline copy constructor delegating to the substring one
    ZStr(const ZStr &o, uint32_t pos, uint32_t count);
    ZStr(const ZStr &o) : ZStr(o, 0, 0xffffffff) {}
#endif
    ZStr &operator=(const ZStr &o);
    ~ZStr();
};
struct FPair {
    virtual void vf0();
    virtual void vf1();
    float value;
    ZStr name, desc;
#if TI == 1
    FPair(const FPair &o) : value(o.value), name(o.name), desc(o.desc) {}
    FPair &operator=(const FPair &o)
    {
        value = o.value;
        name = o.name;
        desc = o.desc;
        return *this;
    }
#endif
    ~FPair();
};
static_assert(sizeof(FPair) == 0x48, "pair");
using V = std::vector<FPair>;
#if WANT(0)
void PushBack(V &v, const FPair &x) { v.push_back(x); }
#endif
#if WANT(1)
void InsertN(V &v, V::const_iterator at, size_t n, const FPair &x) { v.insert(at, n, x); }
#endif
#if WANT(2)
void Destroy(V *v) { v->~V(); }
#endif
#if WANT(3)
void Insert(V &v, V::const_iterator at, const FPair &x) { v.insert(at, x); }
#endif
#if WANT(4)
void Assign(V &v, const V &from) { v = from; }
#endif
#if WANT(5)
void Copy(V *to, const V &from) { new (to) V(from); }
#endif
#if WANT(6)
void Resize(V &v, size_t n, const FPair &x) { v.resize(n, x); }
#endif
#endif
