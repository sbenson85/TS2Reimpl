#pragma once
// gridfx (lane VO, campaign world-lot WL-0): the fixture for nTSWorld::c3DArray<T> (src/world/c3DArray.h), one typed
// helper set run over all ten copies (tests/world_c3darray.cpp), and the planted grids later fixtures build on (WL-1's
// lotfx: a cWorldDB or lot object holding grids with known contents).
// fakes: cIGZIStream, cIGZOStream
//
// - Streams: slot tables with the cRZIStream / cRZOStream layout (5 u8, 6 s16, 7 u16, 8 s32, 9 u32, 12 f32, 15 GZStr; any
//   other slot logs an unexpected call). Every call is logged with its slot, so a call through the wrong slot (SetSint32
//   for SetUint32) shows. A call can be made to throw (the EH unwinding of Write / Read's local cRZString). A read past the
//   script fails: mode A (the default) writes zero into a stack local (the original reads uninitialised stack; the fake
//   gives it a defined value) and leaves a heap field as it was; mode B (gKeepOnFail) leaves every target as it was, for
//   scripts whose header is whole (the locals the original leaves uninitialised are then all written).
// - Copies: Copy<T> holds each copy's RVAs (0: none in the image), its element generator and its record encoding.
// - Grids: Plant fills a grid (sizes, elements, random bytes in the spare capacity); NewGrid builds one with our class's
//   layout and the vtable asked for (the game's, a test copy of it, or ours); Capture dumps one (vtable as a code, count,
//   sizes, vector size and capacity, every element's bytes); FreeGrid frees one without running any slot.
#include "game/game.h"
#include "rz/cRZString.h"
#include "shape.h"
#include "world/c3DArray.h"

#include <intrin.h>

#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

namespace gridfx {

using Blob = std::vector<uint32_t>;
using namespace nTSWorld;

// ---- streams ----

enum : uint32_t { evGet = 0x70, evSet = 0x71, evGetStr = 0x72, evSetStr = 0x73, evOther = 0x76 };

struct Item {
    uint32_t bits = 0;
    std::string text;
    bool isText = false;
};
inline Item U(uint32_t v) { Item i; i.bits = v; return i; }
inline Item F(float v) { Item i; std::memcpy(&i.bits, &v, 4); return i; }
inline Item S(const char *t) { Item i; i.text = t; i.isText = true; return i; }

struct Stub {
    void **vt;
    uint32_t id;
};

inline std::vector<uint32_t> gLog;
inline std::vector<Item> gScript;
inline size_t gCursor;
inline bool gKeepOnFail; // mode B
inline int gFailAt = -1; // the n-th stream call throws
inline int gTick;
inline uint64_t gThrown;
inline void *gInVT[24]{}, *gOutVT[24]{};
inline Stub gIn{gInVT, 1}, gOut{gOutVT, 2};

inline void Note(uint32_t event, uint32_t slot, uint32_t value)
{
    gLog.push_back(event);
    gLog.push_back(slot);
    gLog.push_back(value);
}
inline void Step()
{
    if (gTick++ == gFailAt) {
        ++gThrown;
        throw 73;
    }
}
inline uint32_t Hash(const std::string &s)
{
    uint32_t h = 2166136261u;
    for (unsigned char c : s) h = (h ^ c) * 16777619u;
    return h ^ uint32_t(s.size());
}
inline bool OnStack(const void *p)
{
    const uintptr_t at = reinterpret_cast<uintptr_t>(p);
    return at >= __readfsdword(8) && at < __readfsdword(4);
}

template <unsigned Slot, class T> uint8_t __fastcall Get(Stub *, void *, T *out)
{
    Step();
    if (gCursor < gScript.size()) {
        const Item &item = gScript[gCursor++];
        if constexpr (std::is_same_v<T, float>) std::memcpy(out, &item.bits, 4);
        else *out = static_cast<T>(item.bits);
        uint32_t stored = 0;
        std::memcpy(&stored, out, sizeof(T));
        Note(evGet, Slot, stored);
        return 0x41;
    }
    if (!gKeepOnFail && OnStack(out)) std::memset(out, 0, sizeof(T));
    Note(evGet, Slot, 0xdead);
    return 0;
}
inline uint8_t __fastcall GetText(Stub *, void *, cRZString *out)
{
    Step();
    if (gCursor < gScript.size()) {
        const Item &item = gScript[gCursor++];
        out->mString = item.isText ? item.text : std::string();
        Note(evGetStr, 15, Hash(out->mString));
        return 1;
    }
    if (!gKeepOnFail) out->mString.clear();
    Note(evGetStr, 15, 0xdead);
    return 0;
}
// The value word as pushed, masked to the slot's width (a caller may leave the upper bits of a narrow argument unset).
template <unsigned Slot, uint32_t Mask> uint8_t __fastcall Set(Stub *, void *, uint32_t value)
{
    Step();
    Note(evSet, Slot, value & Mask);
    return 0x41;
}
inline uint8_t __fastcall SetF32(Stub *, void *, float value)
{
    Step();
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    Note(evSet, 12, bits);
    return 0x41;
}
inline uint8_t __fastcall SetText(Stub *, void *, const cRZString *value)
{
    Step();
    Note(evSetStr, 15, value ? Hash(value->mString) : 0xdead0000u);
    return 0x41;
}
// Any slot the copies are not expected to call: logged (the logs then differ from a correct side's, or both sides call it).
template <unsigned Slot> uint32_t Other(void *, unsigned)
{
    Note(evOther, Slot, 0);
    return 0;
}
template <unsigned... S> void FillOther(void **vt, std::integer_sequence<unsigned, S...>)
{
    ((vt[S] = shape::TrapFor<&Other<S>>(1)), ...);
}

inline void SetupStreams()
{
    FillOther(gInVT, std::make_integer_sequence<unsigned, 24>());
    FillOther(gOutVT, std::make_integer_sequence<unsigned, 24>());
    gInVT[5] = reinterpret_cast<void *>(&Get<5, uint8_t>);
    gInVT[6] = reinterpret_cast<void *>(&Get<6, int16_t>);
    gInVT[7] = reinterpret_cast<void *>(&Get<7, uint16_t>);
    gInVT[8] = reinterpret_cast<void *>(&Get<8, int32_t>);
    gInVT[9] = reinterpret_cast<void *>(&Get<9, uint32_t>);
    gInVT[12] = reinterpret_cast<void *>(&Get<12, float>);
    gInVT[15] = reinterpret_cast<void *>(&GetText);
    gOutVT[5] = reinterpret_cast<void *>(&Set<5, 0xff>);
    gOutVT[6] = reinterpret_cast<void *>(&Set<6, 0xffff>);
    gOutVT[7] = reinterpret_cast<void *>(&Set<7, 0xffff>);
    gOutVT[8] = reinterpret_cast<void *>(&Set<8, ~0u>);
    gOutVT[9] = reinterpret_cast<void *>(&Set<9, ~0u>);
    gOutVT[12] = reinterpret_cast<void *>(&SetF32);
    gOutVT[15] = reinterpret_cast<void *>(&SetText);
}

// Resets the log, the script and the failure injection for one run.
inline void Begin(const std::vector<Item> &script, int failAt, bool keepOnFail = false)
{
    SetupStreams();
    gLog.clear();
    gLog.reserve(1 << 19); // the log never reallocates during a run (a frees count stays the code under test's)
    gScript = script;
    gCursor = 0;
    gKeepOnFail = keepOnFail;
    gTick = 0;
    gFailAt = failAt;
}
inline ifaces::cIGZIStream *InStream() { return reinterpret_cast<ifaces::cIGZIStream *>(&gIn); }
inline ifaces::cIGZOStream *OutStream() { return reinterpret_cast<ifaces::cIGZOStream *>(&gOut); }

// ---- the copies ----

// Per copy: write, read and slot 6 (every copy), the Resize member and the destructor body where the image has them.
template <class T> struct Copy;

#define GRIDFX_COPY(Type, Name, write_, read_, vf6_, resize_, body_)                                                       \
    template <> struct Copy<Type> {                                                                                        \
        static constexpr const char *name = Name;                                                                          \
        static constexpr uint32_t write = write_, read = read_, vf6 = vf6_, resize = resize_, body = body_;                \
        static Type Value(std::mt19937 &rng);                                                                              \
        static void Items(std::vector<Item> &out, const Type &v);                                                          \
        static void Bytes(Blob &out, const Type &v);                                                                       \
    }
GRIDFX_COPY(FloorPatternTile, "Pattern", 0xd14160, 0xd11a70, 0xd0bf50, 0xd12260, 0);
GRIDFX_COPY(FloorRotationTile, "Rotation", 0xd14040, 0xd11930, 0xd0bee0, 0, 0);
GRIDFX_COPY(float, "Float", 0xd13b30, 0xd113e0, 0xd0bd60, 0, 0);
GRIDFX_COPY(FloorVisibilityTile, "Visibility", 0xd142a0, 0xd11ba0, 0xd0bfc0, 0xd122e0, 0);
GRIDFX_COPY(uint8_t, "Byte", 0x81e6e0, 0x81df60, 0x81d3d0, 0x81e070, 0x81d2f0);
GRIDFX_COPY(ObjectList, "Objects", 0xd13c60, 0xd11520, 0xd0bdd0, 0, 0);
GRIDFX_COPY(DeckFoundationTile, "Deck", 0xd13f00, 0xd117d0, 0xd0be70, 0, 0);
GRIDFX_COPY(AwningTile, "Awning", 0xd13dc0, 0xd116a0, 0xd0be00, 0xd121e0, 0);
GRIDFX_COPY(WallTile, "Wall", 0x8099b0, 0x806cd0, 0x805910, 0x8072b0, 0);
GRIDFX_COPY(RoomIDTile, "RoomID", 0x80c490, 0x80bc90, 0x80a700, 0, 0);
#undef GRIDFX_COPY

constexpr uint32_t kGetGZCLSID = 0x806aa0; // slot 5 of every copy

inline uint32_t Pick(std::mt19937 &rng, std::initializer_list<uint32_t> values)
{
    return values.begin()[rng() % values.size()];
}
inline uint16_t Half(std::mt19937 &rng) { return uint16_t(rng() % 3 ? rng() % 9 : rng()); }
inline uint32_t Wide(std::mt19937 &rng) { return rng() % 3 ? Pick(rng, {0u, ~0u, 1u, 7u}) : uint32_t(rng()); }

inline FloorPatternTile Copy<FloorPatternTile>::Value(std::mt19937 &rng)
{
    FloorPatternTile t;
    for (uint16_t &s : t.segment) s = Half(rng);
    return t;
}
inline void Copy<FloorPatternTile>::Items(std::vector<Item> &out, const FloorPatternTile &v)
{
    for (uint16_t s : v.segment) out.push_back(U(s));
}
inline void Copy<FloorPatternTile>::Bytes(Blob &out, const FloorPatternTile &v)
{
    for (uint16_t s : v.segment) out.push_back(s);
}

inline FloorRotationTile Copy<FloorRotationTile>::Value(std::mt19937 &rng) { return Half(rng); }
inline void Copy<FloorRotationTile>::Items(std::vector<Item> &out, const FloorRotationTile &v) { out.push_back(U(v)); }
inline void Copy<FloorRotationTile>::Bytes(Blob &out, const FloorRotationTile &v) { out.push_back(v); }

inline float Copy<float>::Value(std::mt19937 &rng) { return float(int(rng() % 20001) - 10000) / 7.0f; }
inline void Copy<float>::Items(std::vector<Item> &out, const float &v) { out.push_back(F(v)); }
inline void Copy<float>::Bytes(Blob &out, const float &v)
{
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    out.push_back(bits);
}

// Stored flags are 0 / 1 in game; other bytes test the (byte != 0) of Read and the plain bytes of Write.
inline FloorVisibilityTile Copy<FloorVisibilityTile>::Value(std::mt19937 &rng)
{
    FloorVisibilityTile t;
    for (uint8_t &s : t.segment) s = uint8_t(Pick(rng, {0, 1, 1, 2, 0x80, 0xff}));
    return t;
}
inline void Copy<FloorVisibilityTile>::Items(std::vector<Item> &out, const FloorVisibilityTile &v)
{
    for (uint8_t s : v.segment) out.push_back(U(s));
}
inline void Copy<FloorVisibilityTile>::Bytes(Blob &out, const FloorVisibilityTile &v)
{
    for (uint8_t s : v.segment) out.push_back(s);
}

inline uint8_t Copy<uint8_t>::Value(std::mt19937 &rng) { return uint8_t(rng() % 3 ? rng() % 4 : rng()); }
inline void Copy<uint8_t>::Items(std::vector<Item> &out, const uint8_t &v) { out.push_back(U(v)); }
inline void Copy<uint8_t>::Bytes(Blob &out, const uint8_t &v) { out.push_back(v); }

// Zero to three ids (negative: a footprint), with spare capacity sometimes.
inline ObjectList Copy<ObjectList>::Value(std::mt19937 &rng)
{
    ObjectList ids;
    const uint32_t n = rng() % 4;
    if (rng() % 3 == 0) ids.reserve(n + rng() % 3);
    for (uint32_t i = 0; i < n; ++i) ids.push_back(int32_t(rng() % 5 ? int32_t(rng() % 200) - 100 : int32_t(rng())));
    return ids;
}
inline void Copy<ObjectList>::Items(std::vector<Item> &out, const ObjectList &v)
{
    out.push_back(U(uint32_t(v.size())));
    for (int32_t id : v) out.push_back(U(uint32_t(id)));
}
inline void Copy<ObjectList>::Bytes(Blob &out, const ObjectList &v)
{
    out.push_back(uint32_t(v.size()));
    out.push_back(uint32_t(v.capacity()));
    for (int32_t id : v) out.push_back(uint32_t(id));
}

inline DeckFoundationTile Copy<DeckFoundationTile>::Value(std::mt19937 &rng)
{
    DeckFoundationTile t;
    for (uint32_t &s : t.segment) s = Wide(rng);
    return t;
}
inline void Copy<DeckFoundationTile>::Items(std::vector<Item> &out, const DeckFoundationTile &v)
{
    for (uint32_t s : v.segment) out.push_back(U(s));
}
inline void Copy<DeckFoundationTile>::Bytes(Blob &out, const DeckFoundationTile &v)
{
    for (uint32_t s : v.segment) out.push_back(s);
}

inline AwningTile Copy<AwningTile>::Value(std::mt19937 &rng) { return {Wide(rng), Wide(rng)}; }
inline void Copy<AwningTile>::Items(std::vector<Item> &out, const AwningTile &v)
{
    out.push_back(U(v.first));
    out.push_back(U(v.second));
}
inline void Copy<AwningTile>::Bytes(Blob &out, const AwningTile &v)
{
    out.push_back(v.first);
    out.push_back(v.second);
}

inline WallTile Copy<WallTile>::Value(std::mt19937 &rng) { return {uint8_t(rng() % 3 ? 1u << (rng() % 8) : rng())}; }
inline void Copy<WallTile>::Items(std::vector<Item> &out, const WallTile &v) { out.push_back(U(v.bits)); }
inline void Copy<WallTile>::Bytes(Blob &out, const WallTile &v) { out.push_back(v.bits); }

inline RoomIDTile Copy<RoomIDTile>::Value(std::mt19937 &rng)
{
    RoomIDTile t;
    for (int16_t &s : t.segment) s = int16_t(rng() % 3 ? int(rng() % 20) - 2 : int(rng()));
    return t;
}
// The script carries the 16 bits sign-extended (the fake stores the low half).
inline void Copy<RoomIDTile>::Items(std::vector<Item> &out, const RoomIDTile &v)
{
    for (int16_t s : v.segment) out.push_back(U(uint32_t(int32_t(s))));
}
inline void Copy<RoomIDTile>::Bytes(Blob &out, const RoomIDTile &v)
{
    for (int16_t s : v.segment) out.push_back(uint16_t(s));
}

// ---- grids ----

// A grid with our class's layout (count 0, sizes 0, no elements) whose vtable word is `vtable` (null: ours).
template <class T> c3DArray<T> *NewGrid(void *const *vtable = nullptr)
{
    auto *grid = new c3DArray<T>();
    if (vtable) shape::VTableAt(grid, 0) = const_cast<void **>(vtable);
    return grid;
}
template <class T> void *const *GameGridVTable() { return shape::GameVTable(c3DArrayElement<T>::kVTable); }

// Fills a grid: sizes x, y, z, x * y * z random elements from `seed` (or `count` when given), `extra` spare capacity
// holding random bytes (a resize within capacity must store its defaults itself). The reference count is left as it is.
template <class T> void Plant(c3DArray<T> *grid, uint32_t seed, int32_t x, int32_t y, int32_t z, uint32_t extra = 0,
    int64_t count = -1)
{
    std::mt19937 rng(seed);
    grid->mSizeX = x;
    grid->mSizeY = y;
    grid->mSizeZ = z;
    const uint32_t n = count >= 0 ? uint32_t(count) : uint32_t(x) * uint32_t(y) * uint32_t(z);
    std::vector<T>().swap(grid->mData);
    grid->mData.reserve(n + extra);
    for (uint32_t i = 0; i < n; ++i) grid->mData.push_back(Copy<T>::Value(rng));
    if constexpr (std::is_trivially_copyable_v<T>) {
        auto *spare = reinterpret_cast<uint8_t *>(grid->mData.data() + grid->mData.size());
        for (size_t i = 0; i < (grid->mData.capacity() - grid->mData.size()) * sizeof(T); ++i) spare[i] = uint8_t(rng() | 1);
    }
}

// The grid's record as Write writes it (and Read reads it): class id, version, name, sizes, then `count` elements (the
// element count Read reads is x * y * z; another count makes a long or short stream).
template <class T> std::vector<Item> Record(uint32_t seed, int32_t x, int32_t y, int32_t z, uint32_t count,
    uint32_t classId = c3DArray<T>::kClassId, uint32_t version = 1, bool text = true)
{
    std::vector<Item> s{U(classId), U(version), text ? S("c3DArray") : U(5), U(uint32_t(x)), U(uint32_t(y)), U(uint32_t(z))};
    std::mt19937 rng(seed);
    for (uint32_t i = 0; i < count; ++i) Copy<T>::Items(s, Copy<T>::Value(rng));
    return s;
}

// The vtable word as a code: 1 for any of `vtables` (the game's, ours, a test copy), else the word itself.
template <class T> void Capture(Blob &out, const c3DArray<T> *grid, std::initializer_list<const void *> vtables)
{
    const void *word = shape::VTableAt(const_cast<c3DArray<T> *>(grid), 0);
    uint32_t code = shape::P(word);
    for (const void *v : vtables)
        if (v == word) code = 1;
    out.insert(out.end(), {code, grid->mRefCount, uint32_t(grid->mSizeX), uint32_t(grid->mSizeY), uint32_t(grid->mSizeZ),
        uint32_t(grid->mData.size()), uint32_t(grid->mData.capacity())});
    for (const T &v : grid->mData) Copy<T>::Bytes(out, v);
}

// Frees a grid without running a slot (its vtable may be a test copy).
template <class T> void FreeGrid(c3DArray<T> *grid)
{
    grid->mData.~vector();
    ::operator delete(grid, sizeof(c3DArray<T>));
}

} // namespace gridfx
