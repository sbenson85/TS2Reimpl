// The effects collection's containers (lane XI, rendering row E11's leftovers): std::vector over the effect records, and
// the name hash, with the records' real layouts. Same shape as probe_render.cpp: one member per unit (-DOP=n) or all
// (-DOP=99), C++14, with $Rl2FlagSets.
//   -DFAM=0..5: the vector's element: 0 component (0x88), 1 effect (0x28, a vector of components inside),
//   2 counted description pointer, 3 description table (0x2c: a hash and a vector), 4 message alias (0x20),
//   5 message trigger (0x1c)
//   -DFAM=6: the unordered_multimap<string, uint32_t> of the effect names, -DTI as below
//   -DFAM=7: all of those in one unit, as the game's
//   -DFAM=8: cTSEffectsExtension's registry, an unordered_map over a key of two strings and a number
//   -DTI: bit 0: the elements' non-trivial special members out of line where the original calls them (else compiler-written);
//   bits 1..4: the over-aligned allocation, string's copy constructor, its copy assignment, its assign(ptr, n) stay calls;
//   bits 5..8: the hash table's uninitialized_fill, fill, string hash and bucket vector growth stay calls;
//   bit 9: string operator==, 10: the integer hash, 13: _Traits_equal stay calls; 11, 12, 14: see HASH_KNOBS;
//   bit 15: uninitialized_fill, 19: fill, 20: _Traits_equal, 21: string hash DEFINED, not inlined (declared only is not
//   enough: the original's compiler knew these do not throw and what they clobber; see rendering.md, "XI result");
//   bits 16..18 (FAM 3): value-construct of tables, the hash's copy constructor and the Ref copy stay calls;
//   bit 22: string's _Tidy_deallocate stays a call (a stand-in for the original's out-of-line string destructor).
//   The TI values that match are listed in stlprobe.ps1's $Tuned; the rest are search space, not coverage.
#include <cstdint>
#include <new>
#include <string>
#include <unordered_map>
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

// What the game's units keep out of line although a small unit inlines it: declared here and defined nowhere, so the call
// stays (the objects are only read, never linked).
#if TI & 4194304
template <> void std::string::_Tidy_deallocate() noexcept;
#endif
#if TI & 2
template <> void *std::_Allocate_manually_vector_aligned<std::_Default_allocate_traits>(const size_t);
#endif
#if TI & 4
template <> std::string::basic_string(const std::string &);
#endif
#if TI & 8
template <> std::string &std::string::operator=(const std::string &);
#endif
#if TI & 16
template <> std::string &std::string::assign(const char *const, const size_t);
#endif
using HashValue = std::pair<const std::string, uint32_t>;
using HashScary = std::_List_val<std::_List_simple_types<HashValue>>;
using HashIter = std::_List_unchecked_iterator<HashScary>;
using HashConstIter = std::_List_unchecked_const_iterator<HashScary>;
#if TI & 32768
template <> __declspec(noinline) void std::uninitialized_fill<HashIter *, HashIter>(HashIter *const first, HashIter *const last, const HashIter &value)
{
    for (HashIter *i = first; i != last; ++i) ::new (static_cast<void *>(i)) HashIter(value);
}
#elif TI & 32
template <> void std::uninitialized_fill<HashIter *, HashIter>(HashIter *const, HashIter *const, const HashIter &);
template <> void std::uninitialized_fill<HashConstIter *, HashConstIter>(HashConstIter *const, HashConstIter *const, const HashConstIter &);
#endif
#if TI & 524288
template <> __declspec(noinline) void std::fill<HashIter *, HashIter>(HashIter *const first, HashIter *const last, const HashIter &value)
{
    for (HashIter *i = first; i != last; ++i) *i = value;
}
#elif TI & 64
template <> void std::fill<HashIter *, HashIter>(HashIter *const, HashIter *const, const HashIter &);
template <> void std::fill<HashConstIter *, HashConstIter>(HashConstIter *const, HashConstIter *const, const HashConstIter &);
#endif
#if TI & 2097152
template <> __declspec(noinline) size_t std::_Hash_array_representation<char>(const char *const first, const size_t count) noexcept
{
    return std::_Fnv1a_append_bytes(std::_FNV_offset_basis, reinterpret_cast<const unsigned char *>(first), count);
}
#elif TI & 128
template <> size_t std::_Hash_array_representation<char>(const char *const, const size_t) noexcept;
#endif
#if TI & 512
template <> bool std::operator==<char, std::char_traits<char>, std::allocator<char>>(const std::string &, const std::string &) noexcept;
#endif
#if TI & 1048576
template <> __declspec(noinline) bool std::_Traits_equal<std::char_traits<char>>(const char *const left, const size_t leftSize, const char *const right, const size_t rightSize) noexcept
{
    return leftSize == rightSize && std::char_traits<char>::compare(left, right, leftSize) == 0;
}
#elif TI & 8192
template <> bool std::_Traits_equal<std::char_traits<char>>(const char *const, const size_t, const char *const, const size_t) noexcept;
#endif
#if TI & 1024
template <> size_t std::_Hash_representation<uint32_t>(const uint32_t &) noexcept;
#endif
#if TI & 256
template <> void std::_Hash_vec<std::allocator<HashIter>>::_Assign_grow(const size_t, const HashIter);
#endif

// Members of one hash table: bit 11 linking a new node, 12 the one-step rehash, 14 the forced rehash stay calls.
#define COMMA ,
template <class K, class V, class Tr, bool Multi> struct HashOf {
    using Value = std::pair<const K, V>;
    using Type = std::_Hash<std::_Umap_traits<K, V, Tr, std::allocator<Value>, Multi>>;
    using Node = std::_List_node<Value, void *>;
};
#if TI & 2048
#define HASH_KNOB_NODE(K, V, Tr, Multi) \
    template <> HashOf<K, V, Tr, Multi>::Node *HashOf<K, V, Tr, Multi>::Type::_Insert_new_node_before( \
        const size_t, HashOf<K, V, Tr, Multi>::Node *const, HashOf<K, V, Tr, Multi>::Node *const) noexcept;
#else
#define HASH_KNOB_NODE(K, V, Tr, Multi)
#endif
#if TI & 4096
#define HASH_KNOB_RE1(K, V, Tr, Multi) template <> void HashOf<K, V, Tr, Multi>::Type::_Rehash_for_1();
#else
#define HASH_KNOB_RE1(K, V, Tr, Multi)
#endif
#if TI & 16384
#define HASH_KNOB_RE(K, V, Tr, Multi) template <> void HashOf<K, V, Tr, Multi>::Type::_Forced_rehash(size_t);
#else
#define HASH_KNOB_RE(K, V, Tr, Multi)
#endif
#define HASH_KNOBS(K, V, Tr, Multi) HASH_KNOB_NODE(K, V, Tr, Multi) HASH_KNOB_RE1(K, V, Tr, Multi) HASH_KNOB_RE(K, V, Tr, Multi)
using StringCompare = std::_Uhash_compare<std::string, std::hash<std::string>, std::equal_to<std::string>>;
#if FAM != 8
HASH_KNOBS(std::string, uint32_t, StringCompare, false)
HASH_KNOBS(std::string, uint32_t, StringCompare, true)
#endif

struct DwordPair {
    uint32_t mFirst = 0, mSecond = 0;
};
struct Vector3 {
    float x, y, z;
};
struct Matrix3 {
    float m[9];
};
// The placement (0x38): a copy leaves the byte at +1 and restarts the version word; assignment takes the whole block.
struct Transform {
    uint8_t mFlags, mUnused;
    uint16_t mVersion;
    Matrix3 mRotation;
    float mScale;
    Vector3 mTranslation;
    Transform() : mFlags(0), mVersion(0)
    {
        float *r = mRotation.m;
        r[1] = r[2] = r[3] = 0;
        r[5] = r[6] = r[7] = 0;
        r[0] = r[4] = r[8] = 1.0f;
        mScale = 1.0f;
        mTranslation.x = mTranslation.y = mTranslation.z = 0;
    }
    Transform(const Transform &other)
        : mFlags(other.mFlags), mVersion(0), mRotation(other.mRotation), mScale(other.mScale),
          mTranslation(other.mTranslation)
    {
    }
    Transform &operator=(const Transform &other) = default;
};
struct Component {
    std::string mName;
    int32_t mType = 1;
    uint32_t mWord1c = 0;
    Transform mTransform;
    uint8_t mByte58 = 0, mByte59 = 0;
    uint32_t mW5c = 0, mW60 = 0, mW64 = 0, mW68 = 0, mW6c = 0, mW70 = 0;
    uint32_t mW74 = 0, mW78 = 0;
    uint16_t mHalf7c = 0, mHalf7e = 0;
    uint32_t mW80 = 0;
    int32_t mIndex = -1;
};
struct Entry {
    uint32_t mFlags = 0;
    uint16_t mVariant = 1;
    uint32_t mParam8 = 0, mParamC = 0, mParam10 = 0;
    DwordPair mRange;
    std::vector<Component> mComponents;
#if TI & 1
    Entry();
#else
    Entry() {}
#endif
};
struct Description {
    virtual int32_t GetType() = 0;
    virtual void *CreateEffect() = 0;
    virtual void CopyTo(Description *to) = 0;
    virtual void *GetDescription() = 0;
    virtual void Read(void *stream) = 0;
    virtual void Write(void *stream) = 0;
    virtual uint32_t GetSize() = 0;
    virtual uint32_t AddRef() = 0;
    virtual uint32_t Release() = 0;
};
class Ref {
public:
    Ref() : mObject(nullptr) {}
    Ref(Description *description) : mObject(description)
    {
        if (mObject) mObject->AddRef();
    }
    Ref(const Ref &other) : Ref(other.mObject) {}
    Ref &operator=(const Ref &other)
    {
        if (&other != this) {
            Description *const next = other.mObject;
            Description *const old = mObject;
            if (next != old) {
                if (next) next->AddRef();
                mObject = next;
                if (old) old->Release();
            }
        }
        return *this;
    }
#if TI & 1
    __declspec(noinline)
#endif
    ~Ref()
    {
        if (mObject) mObject->Release();
    }

private:
    Description *mObject;
};
struct Table {
    std::unordered_map<std::string, uint32_t> mNames;
    std::vector<Ref> mDescriptions;
};
struct Alias {
    std::string mEffect;
    DwordPair mRange;
};
struct Trigger {
    uint32_t mMessage;
    std::string mEffect;
};

#if FAM == 7  // every family in one unit, as the game's: what is inlined depends on how often a body is used
template <class E> struct All {
    #if FAM == 3
using NameHash = HashOf<std::string, uint32_t, StringCompare, false>;
using NameAlnode = std::allocator<NameHash::Node>;
#if TI & 65536
template <> Table *std::_Uninitialized_value_construct_n<std::allocator<Table>>(Table *, size_t, std::allocator<Table> &);
#endif
#if TI & 131072
template <> template <> NameHash::Type::_Hash(const NameHash::Type &, const NameAlnode &);
#endif
#if TI & 262144
template <> Ref *std::_Uninitialized_copy<Ref *, Ref *, std::allocator<Ref>>(Ref *, Ref *, Ref *, std::allocator<Ref> &);
#endif
#endif
using Vec = std::vector<E>;
#if FAM == 3 && WANT(10)
template Table *std::_Uninitialized_value_construct_n<std::allocator<Table>>(Table *, size_t, std::allocator<Table> &);
#endif
    static void Run(Vec &v, Vec &o, const E &e, E &&m, size_t n, void *p)
    {
        v.resize(n);
        v.reserve(n);
        v = o;
        v.push_back(e);
#if OP != 1
        v.clear();
#endif
        v.insert(v.end(), o.begin(), o.end());
        v.push_back(static_cast<E &&>(m));
        v.erase(v.begin());
        new (p) Vec(o);
        v.~Vec();
    }
};
void Op0(std::vector<Component> &v, std::vector<Component> &o, Component &e, size_t n, void *p) { All<Component>::Run(v, o, e, static_cast<Component &&>(e), n, p); }
void Op1(std::vector<Entry> &v, std::vector<Entry> &o, Entry &e, size_t n, void *p) { All<Entry>::Run(v, o, e, static_cast<Entry &&>(e), n, p); }
void Op2(std::vector<Ref> &v, std::vector<Ref> &o, Ref &e, size_t n, void *p) { All<Ref>::Run(v, o, e, static_cast<Ref &&>(e), n, p); }
void Op3(std::vector<Table> &v, std::vector<Table> &o, Table &e, size_t n, void *p) { All<Table>::Run(v, o, e, static_cast<Table &&>(e), n, p); }
void Op4(std::vector<Alias> &v, std::vector<Alias> &o, Alias &e, size_t n, void *p) { All<Alias>::Run(v, o, e, static_cast<Alias &&>(e), n, p); }
void Op5(std::vector<Trigger> &v, std::vector<Trigger> &o, Trigger &e, size_t n, void *p) { All<Trigger>::Run(v, o, e, static_cast<Trigger &&>(e), n, p); }
using M = std::unordered_multimap<std::string, uint32_t>;
void Op6(M &m, M &o, const std::pair<const std::string, uint32_t> &e, const std::string &k)
{
    m.insert(e);
    auto r = m.equal_range(k);
    m.erase(r.first, r.second);
    m.clear();
    m = o;
    m.swap(o);
    m.~M();
}
#elif FAM == 8  // cTSEffectsExtension's registry: unordered_map over a key of two strings and a number
struct ZString {
    virtual ~ZString();
    std::string mText;
    uint32_t mRefs;
};
struct Key {
    Key(const Key &);
#if !(TI & 1)
    ~Key();
#endif
    ZString mFirst, mSecond;
    uint32_t mNumber;
};
struct KeyHash {
    size_t operator()(const Key &k) const
    {
        const size_t first = std::hash<std::string>()(k.mFirst.mText);
        size_t second = std::hash<std::string>()(k.mSecond.mText);
        second |= std::hash<uint32_t>()(k.mNumber);
        return second | first;
    }
};
struct KeyEqual {
    bool operator()(const Key &a, const Key &b) const
    {
        return a.mFirst.mText == b.mFirst.mText && a.mSecond.mText == b.mSecond.mText && a.mNumber == b.mNumber;
    }
};
struct Counted {
    virtual void Slot0();
    virtual void AddRef();
    virtual void Release();
};
struct CountedRef {
    CountedRef(const CountedRef &o) : mObject(o.mObject)
    {
        if (mObject) mObject->AddRef();
    }
    ~CountedRef()
    {
        if (mObject) mObject->Release();
    }
    Counted *mObject;
};
struct Record {
    CountedRef mInstance;
    uint32_t mKind;
    uint8_t mFlag;
};
using R = std::unordered_map<Key, Record, KeyHash, KeyEqual>;
HASH_KNOBS(Key, Record, std::_Uhash_compare<Key COMMA KeyHash COMMA KeyEqual>, false)
#if WANT(0)
std::pair<R::iterator, bool> Op0(R &r, const R::value_type &v) { return r.insert(v); }
#endif
#if WANT(1)
R::iterator Op1(R &r, R::iterator i) { return r.erase(i); }
#endif
#if WANT(2)
R::iterator Op2(R &r, const Key &k) { return r.find(k); }
#endif
#if WANT(3)
void Op3(R *r) { r->~R(); }
#endif
#if WANT(4)
void Op4(R &r) { r.clear(); }
#endif
#if WANT(5)  // the table's implicit destructor, out of line as the original keeps it
struct TableOfRecords : R {
    __declspec(noinline) ~TableOfRecords() {}
};
void Op5(TableOfRecords *r) { r->~TableOfRecords(); }
#endif
#elif FAM == 6
using M = std::unordered_multimap<std::string, uint32_t>;
#if WANT(0)
void Op0(M &m, const std::pair<const std::string, uint32_t> &e) { m.insert(e); }
#endif
#if WANT(1)
void Op1(M &m, const std::string &k) { auto r = m.equal_range(k); m.erase(r.first, r.second); }
#endif
#if WANT(2)
void Op2(M &m) { m.clear(); }
#endif
#if WANT(3)
void Op3(M *m) { m->~M(); }
#endif
#if WANT(4)
void Op4(M &m, const M &o) { m = o; }
#endif
#if WANT(5)
void Op5(M &m, M &o) { m.swap(o); }
#endif
#if WANT(6)
using MHash = HashOf<std::string, uint32_t, StringCompare, true>;
template MHash::Node *MHash::Type::_Find_first<std::string>(const std::string &, size_t) const;
#endif
#else
#if FAM == 0
using E = Component;
#elif FAM == 1
using E = Entry;
#elif FAM == 2
using E = Ref;
#elif FAM == 3
using E = Table;
#elif FAM == 4
using E = Alias;
#else
using E = Trigger;
#endif
#if FAM == 3
using NameHash = HashOf<std::string, uint32_t, StringCompare, false>;
using NameAlnode = std::allocator<NameHash::Node>;
#if TI & 65536
template <> Table *std::_Uninitialized_value_construct_n<std::allocator<Table>>(Table *, size_t, std::allocator<Table> &);
#endif
#if TI & 131072
template <> template <> NameHash::Type::_Hash(const NameHash::Type &, const NameAlnode &);
#endif
#if TI & 262144
template <> Ref *std::_Uninitialized_copy<Ref *, Ref *, std::allocator<Ref>>(Ref *, Ref *, Ref *, std::allocator<Ref> &);
#endif
#endif
#if FAM == 0 && OP == 11
template void std::_Destroy_range<std::allocator<E>>(E *, E *const, std::allocator<E> &) noexcept;
#endif
using Vec = std::vector<E>;
#if FAM == 3 && WANT(10)
template Table *std::_Uninitialized_value_construct_n<std::allocator<Table>>(Table *, size_t, std::allocator<Table> &);
#endif
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
#if WANT(7)
void Op7(Vec &v, const Vec &o) { v.insert(v.end(), o.begin(), o.end()); }
#endif
#if WANT(8)
void Op8(Vec &v, E &&e) { v.push_back(static_cast<E &&>(e)); }
#endif
#if WANT(9)
void Op9(Vec &v) { v.erase(v.begin()); }
#endif
#endif
