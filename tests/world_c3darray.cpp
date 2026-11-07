// nTSWorld::c3DArray<T> (lane VO, src/world/c3DArray.h): differential tests of the ten copies, one pattern run over each
// (tests/world_grid_fx.h holds the per-copy RVAs, values and records). Every case runs three sides on equal grids:
// - the original: a grid with the game's vtable, the function called by RVA;
// - the vtable route: our grid (our vtable), the slot called through our vtable at the slot where the game's vtable of
//   the copy holds the RVA (SlotOf);
// - the adapter route: a grid with a copy of the game's vtable whose tested slot is a counting trap, and the hook's adapter
//   called (an adapter that dispatched virtually would land on the trap; in game, on its own hook).
// Compared: the result, whether a stream call threw, the stream log (every call with its slot and value), the script
// cursor, the frees, the trap's count and the grid (vtable as a code, count, sizes, vector size and capacity, elements).
// Functions: Write, Read and slot 6 of each copy, the Resize members (pattern, visibility, byte, awning, wall), the byte
// copy's destructor body 0x81d2f0, GetGZCLSID 0x806aa0 (every copy's slot 5), and the folded cIGZUnknown slots on our
// vtable against the shared bodies (QueryInterface 0x178b20, AddRef 0x148fb0, Release 0x4b1d00).
#include "difftest.h"
#include "free_recorder.h"
#include "shape.h"
#include "world_grid_fx.h"

#include <windows.h>

#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {
using namespace gridfx;
using shape::P;

constexpr const uint32_t kFace[] = {0};
constexpr unsigned kSlotWords[7] = {2, 0, 0, 1, 1, 0, 1}; // stack words of slots 0-6

int gTrapHits;

template <class T> void **OurVT() { return shape::Ours<c3DArray<T>>(kFace).face[0]; }

// A copy of the game's vtable of T with `slot` a counting trap.
template <class T> void *const *CopyVT(unsigned slot)
{
    static void *vt[7];
    for (unsigned s = 0; s < 7; ++s) vt[s] = GameGridVTable<T>()[s];
    vt[slot] = shape::TrapFor<&shape::Count<gTrapHits>>(kSlotWords[slot]);
    return vt;
}

// The grid of a side: 0 the game's vtable, 1 ours, 2 the trap copy for `slot`.
template <class T> c3DArray<T> *SideGrid(int side, unsigned slot)
{
    return side == 0 ? NewGrid<T>(GameGridVTable<T>()) : side == 1 ? NewGrid<T>() : NewGrid<T>(CopyVT<T>(slot));
}

template <class T> void CaptureSide(Blob &out, const c3DArray<T> *grid, unsigned slot)
{
    Capture(out, grid, {GameGridVTable<T>(), OurVT<T>(), CopyVT<T>(slot)});
}

template <class Fn> Fn Slot(const void *grid, unsigned slot)
{
    return reinterpret_cast<Fn>(shape::VTableAt(const_cast<void *>(grid), 0)[slot]);
}

unsigned SlotOfRva(difftest::Context &ctx, uint32_t vtable, uint32_t rva)
{
    const unsigned slot = shape::SlotOf(vtable, 7, rva);
    if (slot == ~0u) ctx.Fail("0x%x is in no slot of vtable 0x%x", rva, vtable);
    return slot;
}

struct Shape3 {
    int32_t x, y, z;
};
const Shape3 kShapes[] = {{0, 0, 0}, {1, 1, 1}, {2, 3, 1}, {3, 2, 2}, {4, 4, 1}, {1, 5, 3}, {5, 0, 2}, {6, 6, 4}};

const char *SideName(int side) { return side == 0 ? "original" : side == 1 ? "vtable" : "adapter"; }

void Compare(difftest::Context &ctx, const Blob (&blobs)[3], const char *name, const char *what, int a, int b, int c)
{
    for (int side = 1; side < 3; ++side) {
        if (blobs[side] == blobs[0]) continue;
        size_t at = 0;
        while (at < blobs[0].size() && at < blobs[side].size() && blobs[0][at] == blobs[side][at]) ++at;
        const uint32_t want = at < blobs[0].size() ? blobs[0][at] : ~0u, got = at < blobs[side].size() ? blobs[side][at] : ~0u;
        ctx.Fail("%s %s (%d %d %d) %s: word %zu 0x%x != 0x%x", name, what, a, b, c, SideName(side), at, got, want);
    }
    ++ctx.cases;
}

// ---- Resize (the copies with a Resize member) ----

template <class T> void ResizeCases(difftest::Context &ctx)
{
    using Fn = void(__thiscall *)(void *, int32_t, int32_t, int32_t);
    for (const Shape3 &from : kShapes) for (const Shape3 &to : kShapes) for (uint32_t extra : {0u, 5u, 300u}) {
        Blob blobs[3];
        for (int side = 0; side < 3; ++side) {
            c3DArray<T> *grid = side == 1 ? NewGrid<T>() : NewGrid<T>(GameGridVTable<T>());
            Plant(grid, uint32_t(from.x * 7 + to.y + extra), from.x, from.y, from.z, extra);
            difftest::FreeRecorder rec;
            if (side == 0) ctx.Original<Fn>(Copy<T>::resize)(grid, to.x, to.y, to.z);
            else if (side == 1) grid->Resize(to.x, to.y, to.z);
            else c3DArrayAdapters<T>::Resize(grid, nullptr, to.x, to.y, to.z);
            rec.Stop();
            blobs[side] = {uint32_t(rec.Count())};
            CaptureSide(blobs[side], grid, 6);
            FreeGrid(grid);
        }
        Compare(ctx, blobs, Copy<T>::name, "Resize", from.x * 100 + from.y * 10 + from.z, to.x * 100 + to.y * 10 + to.z, int(extra));
    }
}

// ---- Write ----

template <class T> Blob RunWrite(difftest::Context &ctx, int side, unsigned slot, uint32_t seed, const Shape3 &s, int64_t count,
    int fail, int *total)
{
    Begin({}, fail);
    gTrapHits = 0;
    c3DArray<T> *grid = SideGrid<T>(side, slot);
    Plant(grid, seed, s.x, s.y, s.z, seed % 3, count);
    grid->mRefCount = 1 + seed % 3;
    uint8_t returned = 0;
    bool threw = false;
    try {
        using Fn = uint8_t(__thiscall *)(void *, void *);
        if (side == 0) returned = ctx.Original<Fn>(Copy<T>::write)(grid, OutStream());
        else if (side == 1) returned = Slot<Fn>(grid, slot)(grid, OutStream());
        else returned = c3DArrayAdapters<T>::Write(grid, nullptr, OutStream());
    } catch (int) { threw = true; }
    gFailAt = -1;
    if (total) *total = gTick;
    Blob blob{returned, uint32_t(threw), uint32_t(gTrapHits)};
    blob.insert(blob.end(), gLog.begin(), gLog.end());
    CaptureSide(blob, grid, slot);
    FreeGrid(grid);
    return blob;
}

template <class T> void WriteCases(difftest::Context &ctx)
{
    const unsigned slot = SlotOfRva(ctx, c3DArrayElement<T>::kVTable, Copy<T>::write);
    if (slot == ~0u) return;
    const uint64_t before = gThrown;
    uint32_t seed = 0;
    for (const Shape3 &s : kShapes) for (int64_t delta : {0, -1, 2}) {
        // The element count Write writes is the vector's size, not x * y * z.
        const int64_t count = int64_t(s.x) * s.y * s.z + delta;
        if (count < 0) continue;
        int total = 0;
        ++seed;
        for (int fail = -1; fail < total; ++fail) {
            Blob blobs[3];
            for (int side = 0; side < 3; ++side) blobs[side] = RunWrite<T>(ctx, side, slot, seed, s, count, fail, side == 0 && fail < 0 ? &total : nullptr);
            Compare(ctx, blobs, Copy<T>::name, "Write", s.x * 100 + s.y * 10 + s.z, int(delta), fail);
        }
    }
    std::printf("  %s Write: exceptions injected %llu\n", Copy<T>::name, gThrown - before);
}

// ---- Read ----

template <class T> Blob RunRead(difftest::Context &ctx, int side, unsigned slot, const std::vector<Item> &script, uint32_t seed,
    const Shape3 &start, bool keep, int fail, int *total, ifaces::cIGZIStream *in = InStream())
{
    Begin(script, fail, keep);
    gTrapHits = 0;
    c3DArray<T> *grid = SideGrid<T>(side, slot);
    // Every third start has room to grow in place: a short stream then shows the defaults Read's resize stores.
    Plant(grid, seed, start.x, start.y, start.z, seed % 3 == 2 ? 40 : seed % 5);
    grid->mRefCount = 1 + seed % 3;
    uint8_t returned = 0;
    uint32_t threw = 0;
    difftest::FreeRecorder rec;
    try {
        using Fn = uint8_t(__thiscall *)(void *, void *);
        if (side == 0) returned = ctx.Original<Fn>(Copy<T>::read)(grid, in);
        else if (side == 1) returned = Slot<Fn>(grid, slot)(grid, in);
        else returned = c3DArrayAdapters<T>::Read(grid, nullptr, in);
    } catch (int) {
        threw = 1;
    } catch (const std::length_error &) {
        threw = 2;
    }
    rec.Stop();
    gFailAt = -1;
    if (total) *total = gTick;
    Blob blob{returned, uint32_t(threw), uint32_t(gCursor), uint32_t(gTrapHits), uint32_t(rec.Count())};
    blob.insert(blob.end(), gLog.begin(), gLog.end());
    CaptureSide(blob, grid, slot);
    FreeGrid(grid);
    return blob;
}

template <class T> void ReadCases(difftest::Context &ctx)
{
    const unsigned slot = SlotOfRva(ctx, c3DArrayElement<T>::kVTable, Copy<T>::read);
    if (slot == ~0u) return;
    constexpr uint32_t kId = c3DArray<T>::kClassId;
    // Headers cut short, a wrong class id or version, a name that is not text, whole records (also with a longer stream
    // than x * y * z), and records cut inside the elements.
    std::vector<std::vector<Item>> scripts{{}, {U(kId)}, {U(kId), U(1)}, {U(kId + 1), U(1)}, {U(0), U(1)},
        {U(kId), U(1), S("c3DArray")}, {U(kId), U(1), S("c3DArray"), U(2)}, {U(kId), U(1), S("c3DArray"), U(2), U(2)}};
    for (uint32_t version : {0u, 2u, 0xffffffffu}) scripts.push_back(Record<T>(5, 2, 2, 1, 4, kId, version));
    scripts.push_back(Record<T>(6, 3, 2, 1, 6, kId, 1, false));
    for (const Shape3 &s : kShapes) {
        if (s.x * s.y * s.z > 40) continue;
        for (uint32_t extra : {0u, 1u}) scripts.push_back(Record<T>(uint32_t(s.x + s.y), s.x, s.y, s.z, uint32_t(s.x * s.y * s.z) + extra));
    }
    {
        const auto whole = Record<T>(9, 3, 2, 2, 12);
        for (size_t cut : {size_t(6), size_t(7), size_t(9), whole.size() / 2, whole.size() - 1}) {
            auto s = whole;
            s.resize(cut);
            scripts.push_back(s);
        }
    }
    // Mode B (a failed read leaves its target as it was) only where every local the original leaves uninitialised has been
    // written: the header whole. Not for the object lists (the first id count) or the wall tiles (the byte ORed in).
    constexpr bool kModeB = !std::is_same_v<T, ObjectList> && !std::is_same_v<T, WallTile>;
    const Shape3 starts[] = {{0, 0, 0}, {2, 1, 1}, {3, 3, 2}};
    const uint64_t before = gThrown;
    uint32_t index = 0;
    for (const auto &script : scripts) {
        for (int mode = 0; mode < (kModeB && script.size() >= 6 ? 2 : 1); ++mode)
            for (const Shape3 &start : starts) {
                int total = 0;
                for (int fail = -1; fail < total; ++fail) {
                    Blob blobs[3];
                    for (int side = 0; side < 3; ++side)
                        blobs[side] = RunRead<T>(ctx, side, slot, script, index, start, mode == 1, fail, side == 0 && fail < 0 ? &total : nullptr);
                    Compare(ctx, blobs, Copy<T>::name, "Read", int(index), mode * 100 + start.x, fail);
                }
            }
        ++index;
    }
    if constexpr (std::is_same_v<T, ObjectList>) {
        // The first tile's id count starts as the stream pointer: a failed first count read (mode B) resizes the list to
        // that many ids. Nothing below the harness's image is free, so the stream sits at 0x40000000 or above instead: the
        // count is then past the vector's max_size and the resize throws (a count of 0 would not). A copy of the input
        // stub there, header whole, no elements.
        static Stub *high = nullptr;
        for (uintptr_t at = 0x50000000u; at >= 0x40000000u && !high; at -= 0x10000)
            high = static_cast<Stub *>(VirtualAlloc(reinterpret_cast<void *>(at), 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!high) {
            ctx.Fail("no address at 0x40000000 or above for the stream stub");
            return;
        }
        *high = gIn;
        const std::vector<Item> header{U(kId), U(1), S("c3DArray"), U(1), U(1), U(1)};
        for (const Shape3 &start : starts) {
            Blob blobs[3];
            for (int side = 0; side < 3; ++side)
                blobs[side] = RunRead<T>(ctx, side, slot, header, index, start, true, -1, nullptr, reinterpret_cast<ifaces::cIGZIStream *>(high));
            Compare(ctx, blobs, Copy<T>::name, "Read", int(index), 200 + start.x, -1);
        }
        ++index;
    }
    std::printf("  %s Read: %u scripts, exceptions injected %llu\n", Copy<T>::name, index, gThrown - before);
}

// ---- slot 6 (the deleting destructor) ----

template <class T> void DtorCases(difftest::Context &ctx)
{
    const unsigned slot = SlotOfRva(ctx, c3DArrayElement<T>::kVTable, Copy<T>::vf6);
    if (slot == ~0u) return;
    for (uint32_t flags : {0u, 1u, 2u, 3u}) for (const Shape3 &s : kShapes) {
        Blob blobs[3];
        for (int side = 0; side < 3; ++side) {
            gTrapHits = 0;
            c3DArray<T> *grid = SideGrid<T>(side, slot);
            Plant(grid, flags + uint32_t(s.x), s.x, s.y, s.z, flags);
            difftest::FreeRecorder rec;
            using Fn = void *(__thiscall *)(void *, uint32_t);
            void *r = side == 0 ? ctx.Original<Fn>(Copy<T>::vf6)(grid, flags)
                : side == 1   ? Slot<Fn>(grid, slot)(grid, flags)
                              : c3DArrayAdapters<T>::DeletingDestructor(grid, nullptr, flags);
            rec.Stop();
            size_t sizes[4]{};
            const size_t freed = rec.SortedSizes(sizes, 4);
            blobs[side] = {uint32_t(r == grid), uint32_t(rec.Freed(grid)), uint32_t(freed), uint32_t(sizes[0]), uint32_t(sizes[1]),
                uint32_t(sizes[2]), uint32_t(gTrapHits)};
            if (!rec.Freed(grid)) {
                CaptureSide(blobs[side], grid, slot);
                FreeGrid(grid);
            }
        }
        Compare(ctx, blobs, Copy<T>::name, "DeletingDestructor", int(flags), s.x * 100 + s.y * 10 + s.z, 0);
    }
}

// ---- the byte copy's destructor body (cWorldOutsideSupport's destructor calls it on its embedded grid) ----

void DestructCases(difftest::Context &ctx)
{
    for (const Shape3 &s : kShapes) for (uint32_t extra : {0u, 9u}) {
        Blob blobs[3];
        for (int side = 0; side < 3; side += 2) {
            c3DArray<uint8_t> *grid = NewGrid<uint8_t>(GameGridVTable<uint8_t>());
            Plant(grid, uint32_t(s.x + extra), s.x, s.y, s.z, extra);
            difftest::FreeRecorder rec;
            if (side == 0) ctx.Original<void(__thiscall *)(void *)>(Copy<uint8_t>::body)(grid);
            else c3DArrayAdapters<uint8_t>::Destruct(grid, nullptr);
            rec.Stop();
            blobs[side] = {uint32_t(rec.Count()), uint32_t(rec.Freed(grid))};
            CaptureSide(blobs[side], grid, 6);
            FreeGrid(grid);
        }
        blobs[1] = blobs[0];
        Compare(ctx, blobs, "Byte", "Destruct", s.x * 100 + s.y * 10 + s.z, int(extra), 0);
    }
}

// ---- GetGZCLSID (slot 5, one body for every copy) ----

template <class T> void ClassIdCases(difftest::Context &ctx)
{
    const unsigned slot = SlotOfRva(ctx, c3DArrayElement<T>::kVTable, kGetGZCLSID);
    if (slot == ~0u) return;
    Blob blobs[3];
    for (int side = 0; side < 3; ++side) {
        gTrapHits = 0;
        c3DArray<T> *grid = SideGrid<T>(side, slot);
        using Fn = uint32_t(__thiscall *)(void *);
        const uint32_t id = side == 0 ? ctx.Original<Fn>(kGetGZCLSID)(grid)
            : side == 1               ? Slot<Fn>(grid, slot)(grid)
                                      : c3DArrayAdapters<T>::GetGZCLSID(grid, nullptr);
        blobs[side] = {id, uint32_t(gTrapHits)};
        CaptureSide(blobs[side], grid, slot);
        FreeGrid(grid);
    }
    Compare(ctx, blobs, Copy<T>::name, "GetGZCLSID", 0, 0, 0);
}

// ---- the folded cIGZUnknown slots: ours (our vtable) against the shared bodies on a game grid ----

template <class T> void UnknownCases(difftest::Context &ctx)
{
    constexpr uint32_t kQueryInterface = 0x178b20, kAddRef = 0x148fb0, kRelease = 0x4b1d00;
    for (uint32_t count : {1u, 2u, 5u}) for (uint32_t riid : {0u, 1u, 2u, 0x2a51171bu}) for (int nullOut = 0; nullOut < 2; ++nullOut) {
        Blob blobs[3];
        for (int side = 0; side < 2; ++side) {
            c3DArray<T> *grid = side == 0 ? NewGrid<T>(GameGridVTable<T>()) : NewGrid<T>();
            Plant(grid, count + riid, 2, 2, 1, 1);
            grid->mRefCount = count;
            using QI = uint8_t(__thiscall *)(void *, uint32_t, void **);
            using Ref = uint32_t(__thiscall *)(void *);
            void *out = reinterpret_cast<void *>(uintptr_t(0x1234));
            void **outArg = nullOut ? nullptr : &out;
            const uint8_t ok = side == 0 ? ctx.Original<QI>(kQueryInterface)(grid, riid, outArg) : Slot<QI>(grid, 0)(grid, riid, outArg);
            const uint32_t added = side == 0 ? ctx.Original<Ref>(kAddRef)(grid) : Slot<Ref>(grid, 1)(grid);
            blobs[side] = {ok, uint32_t(out == grid), P(out) == 0x1234, added};
            // Release down to zero: the last deletes the grid through slot 6.
            difftest::FreeRecorder rec;
            bool freed = false;
            while (!freed) {
                const uint32_t left = side == 0 ? ctx.Original<Ref>(kRelease)(grid) : Slot<Ref>(grid, 2)(grid);
                blobs[side].push_back(left);
                freed = left == 0;
                if (blobs[side].size() > 20) break;
            }
            rec.Stop();
            blobs[side].push_back(uint32_t(rec.Freed(grid)));
            blobs[side].push_back(uint32_t(rec.Count()));
            if (!freed) FreeGrid(grid);
        }
        blobs[2] = blobs[1];
        Compare(ctx, blobs, Copy<T>::name, "cIGZUnknown", int(count), int(riid), nullOut);
    }
}
} // namespace

DIFF_TEST(C3DArray_PatternWrite, 0xd14160) { WriteCases<FloorPatternTile>(ctx); }
DIFF_TEST(C3DArray_PatternRead, 0xd11a70) { ReadCases<FloorPatternTile>(ctx); }
DIFF_TEST(C3DArray_PatternDeletingDestructor, 0xd0bf50) { DtorCases<FloorPatternTile>(ctx); }
DIFF_TEST(C3DArray_PatternUnknown, 0x178b20) { UnknownCases<FloorPatternTile>(ctx); ClassIdCases<FloorPatternTile>(ctx); }
DIFF_TEST(C3DArray_RotationWrite, 0xd14040) { WriteCases<FloorRotationTile>(ctx); }
DIFF_TEST(C3DArray_RotationRead, 0xd11930) { ReadCases<FloorRotationTile>(ctx); }
DIFF_TEST(C3DArray_RotationDeletingDestructor, 0xd0bee0) { DtorCases<FloorRotationTile>(ctx); }
DIFF_TEST(C3DArray_RotationUnknown, 0x178b20) { UnknownCases<FloorRotationTile>(ctx); ClassIdCases<FloorRotationTile>(ctx); }
DIFF_TEST(C3DArray_FloatWrite, 0xd13b30) { WriteCases<float>(ctx); }
DIFF_TEST(C3DArray_FloatRead, 0xd113e0) { ReadCases<float>(ctx); }
DIFF_TEST(C3DArray_FloatDeletingDestructor, 0xd0bd60) { DtorCases<float>(ctx); }
DIFF_TEST(C3DArray_FloatUnknown, 0x178b20) { UnknownCases<float>(ctx); ClassIdCases<float>(ctx); }
DIFF_TEST(C3DArray_VisibilityWrite, 0xd142a0) { WriteCases<FloorVisibilityTile>(ctx); }
DIFF_TEST(C3DArray_VisibilityRead, 0xd11ba0) { ReadCases<FloorVisibilityTile>(ctx); }
DIFF_TEST(C3DArray_VisibilityDeletingDestructor, 0xd0bfc0) { DtorCases<FloorVisibilityTile>(ctx); }
DIFF_TEST(C3DArray_VisibilityUnknown, 0x178b20) { UnknownCases<FloorVisibilityTile>(ctx); ClassIdCases<FloorVisibilityTile>(ctx); }
DIFF_TEST(C3DArray_ByteWrite, 0x81e6e0) { WriteCases<uint8_t>(ctx); }
DIFF_TEST(C3DArray_ByteRead, 0x81df60) { ReadCases<uint8_t>(ctx); }
DIFF_TEST(C3DArray_ByteDeletingDestructor, 0x81d3d0) { DtorCases<uint8_t>(ctx); }
DIFF_TEST(C3DArray_ByteUnknown, 0x178b20) { UnknownCases<uint8_t>(ctx); ClassIdCases<uint8_t>(ctx); }
DIFF_TEST(C3DArray_ObjectsWrite, 0xd13c60) { WriteCases<ObjectList>(ctx); }
DIFF_TEST(C3DArray_ObjectsRead, 0xd11520) { ReadCases<ObjectList>(ctx); }
DIFF_TEST(C3DArray_ObjectsDeletingDestructor, 0xd0bdd0) { DtorCases<ObjectList>(ctx); }
DIFF_TEST(C3DArray_ObjectsUnknown, 0x178b20) { UnknownCases<ObjectList>(ctx); ClassIdCases<ObjectList>(ctx); }
DIFF_TEST(C3DArray_DeckWrite, 0xd13f00) { WriteCases<DeckFoundationTile>(ctx); }
DIFF_TEST(C3DArray_DeckRead, 0xd117d0) { ReadCases<DeckFoundationTile>(ctx); }
DIFF_TEST(C3DArray_DeckDeletingDestructor, 0xd0be70) { DtorCases<DeckFoundationTile>(ctx); }
DIFF_TEST(C3DArray_DeckUnknown, 0x178b20) { UnknownCases<DeckFoundationTile>(ctx); ClassIdCases<DeckFoundationTile>(ctx); }
DIFF_TEST(C3DArray_AwningWrite, 0xd13dc0) { WriteCases<AwningTile>(ctx); }
DIFF_TEST(C3DArray_AwningRead, 0xd116a0) { ReadCases<AwningTile>(ctx); }
DIFF_TEST(C3DArray_AwningDeletingDestructor, 0xd0be00) { DtorCases<AwningTile>(ctx); }
DIFF_TEST(C3DArray_AwningUnknown, 0x178b20) { UnknownCases<AwningTile>(ctx); ClassIdCases<AwningTile>(ctx); }
DIFF_TEST(C3DArray_WallWrite, 0x8099b0) { WriteCases<WallTile>(ctx); }
DIFF_TEST(C3DArray_WallRead, 0x806cd0) { ReadCases<WallTile>(ctx); }
DIFF_TEST(C3DArray_WallDeletingDestructor, 0x805910) { DtorCases<WallTile>(ctx); }
DIFF_TEST(C3DArray_WallUnknown, 0x178b20) { UnknownCases<WallTile>(ctx); ClassIdCases<WallTile>(ctx); }
DIFF_TEST(C3DArray_RoomIDWrite, 0x80c490) { WriteCases<RoomIDTile>(ctx); }
DIFF_TEST(C3DArray_RoomIDRead, 0x80bc90) { ReadCases<RoomIDTile>(ctx); }
DIFF_TEST(C3DArray_RoomIDDeletingDestructor, 0x80a700) { DtorCases<RoomIDTile>(ctx); }
DIFF_TEST(C3DArray_RoomIDUnknown, 0x178b20) { UnknownCases<RoomIDTile>(ctx); ClassIdCases<RoomIDTile>(ctx); }

DIFF_TEST(C3DArray_PatternResize, 0xd12260) { ResizeCases<FloorPatternTile>(ctx); }
DIFF_TEST(C3DArray_VisibilityResize, 0xd122e0) { ResizeCases<FloorVisibilityTile>(ctx); }
DIFF_TEST(C3DArray_ByteResize, 0x81e070) { ResizeCases<uint8_t>(ctx); }
DIFF_TEST(C3DArray_AwningResize, 0xd121e0) { ResizeCases<AwningTile>(ctx); }
DIFF_TEST(C3DArray_WallResize, 0x8072b0) { ResizeCases<WallTile>(ctx); }
DIFF_TEST(C3DArray_ByteDestruct, 0x81d2f0) { DestructCases(ctx); }
DIFF_TEST(C3DArray_GetGZCLSID, 0x806aa0) { ClassIdCases<WallTile>(ctx); ClassIdCases<uint8_t>(ctx); }
