// cTSNeighborManager in the standalone shape (tests/ts_neighbor_manager_shape.h): the slots no other test calls, each called on
// each side's own object through its own vtable: the folded bodies (AddRef, IsDirty 0x410230, the two do-nothing slots, the
// persist-face getter, the persist face's key accessors and GetRefCount) and the secondary faces' adjustor thunks (QueryInterface
// 0x4bd4de / 0x4bd4e6 / 0x4bd4ee, Release 0x4be14a / 0x4be152 / 0x4be15a; the AddRef thunks are folded). Route 0: an object of
// the game's shape, the game's vtable slot (checked to hold the listed RVA). Route 1: our object, our vtable's slot. Compared:
// the result (the bits the slot returns), out-parameters, the fields, the vtable words (coded) and the deleting destructor calls
// (slot 34, a recording fake patched over the copy, which Release reaches at count 1).
#include "difftest.h"
#include "ts_neighbor_manager_shape.h"

#include <cstring>
#include <random>
#include <vector>

namespace {
using namespace nbrmgrshape;

std::vector<uint32_t> gLog;
alignas(8) uint8_t gBuffer[sizeof(Mgr)];

void *__fastcall RecordingDelete(Mgr *m, void *, uint32_t flags)
{
    gLog.push_back(0xde1);
    gLog.push_back(flags);
    return m;
}

enum Kind { kNoArg, kOneArg, kGetKey, kQuery };
struct SlotCase {
    unsigned face, slot;
    uint32_t rva;
    Kind kind;
    uint32_t mask;   // the result bits the slot returns (0: none, a void slot)
    bool pointer;    // the result is a pointer (compared as an offset into the object)
};
constexpr SlotCase kCases[] = {
    {0, 1, 0x146a40, kNoArg, ~0u, false},   // AddRef (folded)
    {0, 11, 0x410230, kNoArg, 0xff, false}, // IsDirty: the raw byte
    {0, 13, 0x300a0, kOneArg, 0, false},    // does nothing (ret 4)
    {0, 31, 0x140fe0, kNoArg, 0, false},    // does nothing
    {0, 33, 0x1c04e0, kNoArg, ~0u, true},   // the persist face
    {1, 0, 0x4bd4de, kQuery, 0xff, false},  // QueryInterface thunk
    {1, 1, 0x37b3c3, kNoArg, ~0u, false},   // AddRef thunk (folded)
    {1, 2, 0x4be14a, kNoArg, ~0u, false},   // Release thunk
    {1, 3, 0x4bcd20, kGetKey, ~0u, true},   // GetKey (hidden result pointer)
    {1, 5, 0x1674c0, kNoArg, ~0u, false},   // GetType
    {1, 6, 0x1674d0, kNoArg, ~0u, false},   // GetGroup
    {1, 7, 0x145520, kNoArg, ~0u, false},   // GetInstance
    {1, 8, 0x435250, kOneArg, 0xff, false}, // SetType
    {1, 9, 0x435290, kOneArg, 0xff, false}, // SetGroup
    {1, 10, 0x1f0f90, kOneArg, 0xff, false}, // SetInstance
    {1, 11, 0x145770, kNoArg, ~0u, false},  // GetRefCount
    {2, 0, 0x4bd4e6, kQuery, 0xff, false},  // QueryInterface thunk
    {2, 1, 0x473592, kNoArg, ~0u, false},   // AddRef thunk (folded)
    {2, 2, 0x4be152, kNoArg, ~0u, false},   // Release thunk
    {3, 0, 0x4bd4ee, kQuery, 0xff, false},  // QueryInterface thunk
    {3, 1, 0x47359a, kNoArg, ~0u, false},   // AddRef thunk (folded)
    {3, 2, 0x4be15a, kNoArg, ~0u, false},   // Release thunk
};

uint32_t Offset(uint32_t p)
{
    const uint32_t base = shape::P(gBuffer);
    return p - base < sizeof(Mgr) ? 0x2000 + (p - base) : p;
}

std::vector<uint32_t> Run(const SlotCase &c, uint32_t seed)
{
    std::mt19937 r(seed * 2654435761u + c.rva);
    for (auto &b : gBuffer) b = uint8_t(r());
    Mgr *m = Construct(gBuffer);
    m->mRefCount = r() % 3 ? r() % 4 : r();
    m->mUserFileCounter = r();
    m->mNextNeighborId = uint16_t(r());
    m->mDirty = uint8_t(r() % 3 ? r() % 2 : r());
    m->mResourceKey = {r(), r(), r(), r()};
    ClearPatches();
    gPatch[0][34] = reinterpret_cast<void *>(&RecordingDelete);
    Install(gBuffer);
    gLog.clear();

    void *const fn = gRoute == 0 ? GameVt(c.face)[c.slot] : OurVt(c.face)[c.slot];
    void *const self = gBuffer + kFaceOffset[c.face];
    const uint32_t arg = r() % 4 ? r() : r() % 3;
    constexpr uint32_t ids[] = {1, 0xcc2a6a30, 0xe4fda3d4, 0x456b8f1d, 0x8db5bf48, 0, 2, 0xcc2a6a34, 0x456b8f1c, 0xffffffff};
    const uint32_t id = ids[r() % (sizeof(ids) / 4)];
    std::vector<uint32_t> b;
    uint32_t result = 0;
    uint32_t key[4] = {0x5e5e5e5e, 0x5e5e5e5e, 0x5e5e5e5e, 0x5e5e5e5e};
    void *out = reinterpret_cast<void *>(0x5a5a5a5a);
    switch (c.kind) {
    case kNoArg: result = reinterpret_cast<uint32_t(__thiscall *)(void *)>(fn)(self); break;
    case kOneArg: result = reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t)>(fn)(self, arg); break;
    case kGetKey: result = reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t *)>(fn)(self, key); break;
    case kQuery: result = reinterpret_cast<uint32_t(__thiscall *)(void *, uint32_t, void **)>(fn)(self, id, &out); break;
    }
    result &= c.mask;
    if (c.kind == kGetKey) result = result == shape::P(key) ? 0x6e7 : result; // the hidden pointer comes back
    b.push_back(c.pointer ? Offset(result) : result);
    for (uint32_t w : key) b.push_back(w);
    b.push_back(Offset(shape::P(out)));
    uint8_t copy[sizeof(Mgr)];
    std::memcpy(copy, gBuffer, sizeof(copy));
    CodeVtableWords(copy);
    for (unsigned off = 0; off < 0x1c; off += 4) b.push_back(shape::Word(copy, off));
    for (unsigned off = 0x44; off < 0x54; off += 4) b.push_back(shape::Word(copy, off));
    b.insert(b.end(), gLog.begin(), gLog.end());
    Destroy(gBuffer);
    return b;
}
} // namespace

DIFF_TEST(NeighborManager_Slots, 0x4bd4de)
{
    for (const SlotCase &c : kCases) {
        ++ctx.cases;
        if (GameVt(c.face)[c.slot] != ctx.image.At(c.rva)) ctx.Fail("face %u slot %u is not %x", c.face, c.slot, c.rva);
        if (OurVt(c.face)[c.slot] == GameVt(c.face)[c.slot]) ctx.Fail("face %u slot %u: our vtable holds the game's", c.face, c.slot);
        for (uint32_t seed = 0; seed < 40; ++seed) {
            std::vector<uint32_t> blobs[2];
            for (int route = 0; route < 2; ++route) {
                gRoute = route;
                blobs[route] = Run(c, seed);
            }
            gRoute = 0;
            ++ctx.cases;
            if (blobs[0] != blobs[1]) {
                size_t at = 0;
                while (at < blobs[0].size() && at < blobs[1].size() && blobs[0][at] == blobs[1][at]) ++at;
                ctx.Fail("face %u slot %u (%x) seed %u differs at word %zu (%x / %x)", c.face, c.slot, c.rva, seed, at,
                    at < blobs[0].size() ? blobs[0][at] : 0xdeadbeef, at < blobs[1].size() ? blobs[1][at] : 0xdeadbeef);
            }
        }
    }
}
