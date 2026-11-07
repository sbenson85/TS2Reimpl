// Neighborhood_Slots (lane EM): the slots of cTSNeighborhood's four vtables that no other test calls, in the standalone shape
// (docs/standalone-shape.md): the folded getters and setters (primary slots 1, 11, 13, 15, 16, 31, 52, 53, 64, 72; persist face
// 5-9 and 11; container face 3 and 4) and the secondary faces' QueryInterface / AddRef / Release adjustor thunks (QI 0x47721e,
// 0x477226, 0x47722e; Release 0x4786fa, 0x478702, 0x47870a; the AddRef thunks are folded across classes). Each slot is called on
// each side's own object through that object's vtable: the game's on an object from the original constructor 0x471d90, ours
// (pristine) on an object from our constructor. Compared: the result (pointers as offsets into the object), the out-parameter,
// every byte of the object (vtable words normalised, the map head pointers set aside) and the guard bytes around it.
#include "difftest.h"
#include "ts_neighborhood_shape.h"

#include <cstring>
#include <random>
#include <vector>

namespace {
using Nb = cTSNeighborhood;
using Blob = std::vector<uint32_t>;

struct Guarded {
    uint8_t before[16];
    alignas(8) uint8_t bytes[0xcc];
    uint8_t after[16];
};

enum Kind { kNone, kWord, kByte, kPointer, kQuery, kArgByte, kArgWord, kArgPointer, kName };
struct SlotCase {
    unsigned face, slot;
    Kind kind;
};
constexpr SlotCase kCases[] = {
    {0, 1, kWord},     {0, 11, kPointer}, {0, 13, kPointer}, {0, 15, kPointer}, {0, 16, kPointer}, {0, 31, kWord},
    {0, 52, kWord},    {0, 53, kWord},    {0, 64, kName},    {0, 72, kArgByte},
    {1, 0, kQuery},    {1, 1, kWord},     {1, 2, kWord},     {1, 5, kWord},     {1, 6, kWord},     {1, 7, kWord},
    {1, 8, kArgWord},  {1, 9, kArgWord},  {1, 11, kWord},
    {2, 0, kQuery},    {2, 1, kWord},     {2, 2, kWord},     {2, 3, kArgPointer}, {2, 4, kArgPointer},
    {3, 0, kQuery},    {3, 1, kWord},     {3, 2, kWord},
};
constexpr uint32_t kIds[] = {1, 0x7aa51c46, 0xe4fda3d4, 0x456b8f1d, 0x8db5bf48, 0, 0x7aa51c47, 0xffffffff};
const char *const kNames[] = {"", "Pleasantview", "a neighbourhood name longer than the small buffer"};

uint32_t Offset(const void *object, uint32_t value)
{
    const uint32_t base = nbshape::P(object);
    return value - base < 0xcc ? 0x0ff00000u + (value - base) : value;
}

Nb *Make(difftest::Context &ctx, bool original, Guarded &g, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + 5);
    for (auto &b : g.before) b = uint8_t(r());
    for (auto &b : g.after) b = uint8_t(r());
    for (auto &b : g.bytes) b = uint8_t(r());
    Nb *n = nbshape::Build(ctx, original, g.bytes);
    for (auto &w : n->mCode) w = uint16_t(r());
    n->mWidth = r();
    n->mHeight = r();
    for (auto &k : n->mKey) k = r();
    n->mFlags = r();
    n->mFlagByte = uint8_t(r());
    n->mInitialized = uint8_t(r());
    n->mField6C = r();
    n->mRefCount = 2 + r() % 6; // Release through a thunk never reaches the destructor
    // Raw values: no slot under test touches them (cleared again before destruction).
    n->mDelegate = reinterpret_cast<void *>(uintptr_t(0x5100 + r() % 16));
    n->mSecondOwner = reinterpret_cast<void *>(uintptr_t(0x5200 + r() % 16));
    n->mOwner = reinterpret_cast<void *>(uintptr_t(0x5300 + r() % 16));
    return n;
}

void Capture(Blob &b, Guarded &g)
{
    uint8_t copy[0xcc];
    std::memcpy(copy, g.bytes, sizeof(copy));
    nbshape::Norm(copy);
    Nb *n = reinterpret_cast<Nb *>(g.bytes);
    for (unsigned off : {0x5cu, 0x64u, 0x70u, 0x78u, 0x80u, 0x88u, 0xa8u, 0xb4u}) *reinterpret_cast<uint32_t *>(copy + off) = 0;
    if (n->mName.mString.capacity() > 15) *reinterpret_cast<uint32_t *>(copy + 0x40) = 0xbadbad;
    for (unsigned i = 0; i < sizeof(copy) / 4; ++i) b.push_back(reinterpret_cast<uint32_t *>(copy)[i]);
    for (char c : n->mName.mString) b.push_back(uint8_t(c));
    for (auto v : g.before) b.push_back(v);
    for (auto v : g.after) b.push_back(v);
}

uint32_t Run(bool original, Nb *n, const SlotCase &c, uint32_t seed)
{
    using namespace nbshape;
    switch (c.kind) {
    case kNone: return 0;
    case kWord: return CallOwn<uint32_t(__thiscall *)(void *)>(original, n, c.face, c.slot);
    case kByte: return CallOwn<uint32_t(__thiscall *)(void *)>(original, n, c.face, c.slot) & 0xff;
    case kPointer: return Offset(n, CallOwn<uint32_t(__thiscall *)(void *)>(original, n, c.face, c.slot));
    case kQuery: {
        struct Output { uint32_t before; uint32_t value; uint32_t after; } out{0x98ab76cd, 0x1234abcd, 0xcdef4321};
        const uint32_t r = CallOwn<uint32_t(__thiscall *)(void *, uint32_t, uint32_t *)>(original, n, c.face, c.slot, kIds[seed % 8], &out.value);
        return (r & 0xff) ^ (Offset(n, out.value) * 3) ^ (out.before * 5) ^ (out.after * 7);
    }
    case kArgByte: {
        // A dword whose upper bytes are garbage: only its low byte is the argument.
        const uint32_t arg = (seed * 0x9e3779b9u) & 0xffffff00u | (seed % 3 == 0 ? 0x80 : seed % 2);
        CallOwn<uint32_t(__thiscall *)(void *, uint32_t)>(original, n, c.face, c.slot, arg);
        return 0; // no result
    }
    case kArgWord: return CallOwn<uint32_t(__thiscall *)(void *, uint32_t)>(original, n, c.face, c.slot, seed * 0x01000193u) & 0xff;
    case kArgPointer:
        return CallOwn<uint32_t(__thiscall *)(void *, void *)>(original, n, c.face, c.slot, reinterpret_cast<void *>(uintptr_t(0x7700 + seed))) & 0xff;
    case kName:
        return Offset(n, CallOwn<uint32_t(__thiscall *)(void *, const char *)>(original, n, c.face, c.slot, kNames[seed % 3]));
    }
    return 0;
}
} // namespace

DIFF_TEST(Neighborhood_Slots, 0x47721e)
{
    unsigned compared = 0;
    for (const SlotCase &c : kCases) {
        for (uint32_t seed = 0; seed < 24; ++seed) {
            Blob blobs[2];
            for (int side = 0; side < 2; ++side) {
                const bool original = side == 0;
                Guarded g;
                Nb *n = Make(ctx, original, g, seed + c.face * 1000 + c.slot * 31);
                blobs[side].push_back(Run(original, n, c, seed));
                Capture(blobs[side], g);
                n->mDelegate = n->mSecondOwner = n->mOwner = nullptr;
                nbshape::Destroy(ctx, original, n);
            }
            ++ctx.cases;
            ++compared;
            if (blobs[0] != blobs[1]) {
                size_t at = 0;
                while (at < blobs[0].size() && at < blobs[1].size() && blobs[0][at] == blobs[1][at]) ++at;
                ctx.Fail("face %u slot %u seed %u: word %zu differs (original %x, ours %x)", c.face, c.slot, seed, at,
                         at < blobs[0].size() ? blobs[0][at] : 0, at < blobs[1].size() ? blobs[1][at] : 0);
            }
        }
    }
    // The thunks named for the record: QI 0x477226 / 0x47722e, Release 0x4786fa / 0x478702 / 0x47870a (with 0x47721e above).
    std::printf("  Slots: %u slot calls compared\n", compared);
}
