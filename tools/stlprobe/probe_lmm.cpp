// nTSSG::cLightMapManager's two hash maps (lane 261108q, scenegraph LG6), over the game's real keys, values and functors:
// map A unordered_map<FloorKey, FloorTile, FloorKeyHash, FloorKeyEqual>, map B unordered_map<WallKey, WallRegion,
// WallKeyHash, WallKeyEqual>. The hashes (0x2c5270, 0x2c5290) and the wall equality (0x2c5100) are the game's own
// functions, called out of line: declared here and defined nowhere (the objects are only read, never linked). One
// member per unit (-DOP=n) or all of a family's (-DOP=99), as probe_rl2.cpp, with stlprobe.ps1's $Rl2FlagSets, C++14.
//   -DFAM=0  map A, -DFAM=1  map B
//   -DTI: bit 0: the functors noexcept; bit 1 (map A): the floor equality a byte compare rather than field by field;
//   bits 2..4: the bucket vector's _Assign_grow, _Forced_rehash and _Find_last stay calls; bit 5: see below
//   -DOP: 0 insert(const value_type &), 1 find, 2 clear, 3 erase(key)
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <utility>

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
#if TI & 1
#define LMM_NOEXCEPT noexcept
#else
#define LMM_NOEXCEPT
#endif

struct FloorKey {
    int32_t x, y, level;
};
struct FloorTile {
    uint8_t x, y;
    int8_t image;
    uint8_t flags;
};
struct WallKey {
    float x0, y0;
    uint32_t word2;
    float x1, y1;
    uint32_t word5;
};
struct WallRegion {
    int32_t node;
    int8_t atlas;
    uint8_t border;
    uint16_t count;
};
struct FloorKeyHash {
    size_t operator()(const FloorKey &key) const LMM_NOEXCEPT;
};
struct FloorKeyEqual {
    bool operator()(const FloorKey &a, const FloorKey &b) const LMM_NOEXCEPT
    {
#if TI & 2
        return std::memcmp(&a, &b, sizeof a) == 0;
#else
        return a.x == b.x && a.y == b.y && a.level == b.level;
#endif
    }
};
struct WallKeyHash {
    size_t operator()(const WallKey &key) const LMM_NOEXCEPT;
};
struct WallKeyEqual {
    bool operator()(const WallKey &a, const WallKey &b) const LMM_NOEXCEPT;
};
// Bit 5: the family's own functors defined in the unit but not inlined (the bodies as src/sg/cLightMapManager.cpp has
// them): the compiler then knows what the calls do. Only the family's, so no unit defines a functor its maps don't use
// (FloorKeyHash's body is the game's 0x2c5270, game code, never a library row; a map A unit with bit 5 would place it).
#if TI & 32
#if FAM == 0
__declspec(noinline) size_t FloorKeyHash::operator()(const FloorKey &key) const LMM_NOEXCEPT
{
    return (static_cast<uint32_t>(key.level) * 0x1000u + static_cast<uint32_t>(key.y)) * 0x1000u +
           static_cast<uint32_t>(key.x);
}
#else
static uint32_t Bits(float v)
{
    uint32_t u;
    std::memcpy(&u, &v, sizeof u);
    return u;
}
static uint32_t Monotonic(uint32_t bits)
{
    const uint32_t sign = static_cast<uint32_t>(static_cast<int32_t>(bits) >> 31);
    return ((sign >> 1) ^ bits) - sign;
}
static bool Near(float a, float b)
{
    const uint32_t d = Monotonic(Bits(a)) - Monotonic(Bits(b));
    return ((static_cast<uint32_t>(static_cast<int32_t>(d) >> 26) ^ d) & 0xffffffe0u) == 0;
}
__declspec(noinline) size_t WallKeyHash::operator()(const WallKey &key) const LMM_NOEXCEPT
{
    uint32_t h = Bits(key.y1 + 12582912.0f) - 0x4b400000u;
    h = h * 0x100u + (Bits(key.x1 + 12582912.0f) & 0xffu);
    h = h * 0x100u + (Bits(key.y0 + 12582912.0f) & 0xffu);
    return h * 0x100u + (Bits(key.x0 + 12582912.0f) & 0xffu);
}
__declspec(noinline) bool WallKeyEqual::operator()(const WallKey &a, const WallKey &b) const LMM_NOEXCEPT
{
    return a.word2 == b.word2 && Near(a.x0, b.x0) && Near(a.y0, b.y0) && a.word5 == b.word5 && Near(a.x1, b.x1) &&
           Near(a.y1, b.y1);
}
#endif
#endif

#if FAM == 0
using M = std::unordered_map<FloorKey, FloorTile, FloorKeyHash, FloorKeyEqual>;
#else
using M = std::unordered_map<WallKey, WallRegion, WallKeyHash, WallKeyEqual>;
#endif

// What the game's unit keeps out of line although /O2 inlines it here: declared and defined nowhere, so the call stays.
using Iter = std::_List_unchecked_iterator<std::_List_val<std::_List_simple_types<M::value_type>>>;
#if TI & 4
template <> void std::_Hash_vec<std::allocator<Iter>>::_Assign_grow(const size_t, const Iter);
#endif
#if TI & 8
template <> void M::_Mybase::_Forced_rehash(size_t);
#endif
#if TI & 16
template <> template <> std::_Hash_find_last_result<M::_Mybase::_Nodeptr> M::_Mybase::_Find_last<M::key_type>(
    const M::key_type &, const size_t) const;
#endif

#if WANT(0)
std::pair<M::iterator, bool> Insert(M &m, const M::value_type &v) { return m.insert(v); }
#endif
#if WANT(1)
M::iterator Find(M &m, const M::key_type &k) { return m.find(k); }
#endif
#if WANT(2)
void Clear(M &m) { m.clear(); }
#endif
#if WANT(3)
size_t Erase(M &m, const M::key_type &k) { return m.erase(k); }
#endif
