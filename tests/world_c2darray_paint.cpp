// nTSWorld::c2DArray<cWorldTerrain::tVertPaintInfo> (lane VP, campaign world-lot WL-0b): differential tests of Resize
// 0x8040b0, Write 0x804d40, Read 0x803340, slot 6 0x801730, the destructor body 0x801440 and slot 5 0x4af570 (shared by
// the three c2DArray copies), original (by RVA) against ours on equal arrays. Streams: gridfx's slot-tagged fakes
// (tests/world_grid_fx.h), mode A (a failed read zeroes a stack local, leaves a heap byte as it was), a throw at every
// stream call in turn. Read runs under both values of the terrain record version it consults (0x10df504: < 4 reads each
// paint byte as a u16).
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "ts/c2DArray.h"
#include "world_grid_fx.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace {
using namespace gridfx;
using nTSWorld::tVertPaintInfo;
using Paint = nTSWorld::c2DArray<tVertPaintInfo>;

constexpr uint32_t kResize = 0x8040b0, kWrite = 0x804d40, kRead = 0x803340, kVf6 = 0x801730, kBody = 0x801440,
                   kClsid = 0x4af570, kTerrainVersion = 0x10df504;

uint8_t PaintByte(std::mt19937 &rng)
{
    static const uint8_t kBytes[] = {0xff, 0xff, 0, 1, 7, 0x80, 0xfe};
    return kBytes[rng() % 7];
}

tVertPaintInfo Value(std::mt19937 &rng)
{
    tVertPaintInfo v;
    for (uint8_t &b : v.mPaint) b = PaintByte(rng);
    v.mFlag = uint8_t(rng() % 3 ? 1 : rng());
    return v;
}

Paint *Make(uint32_t seed, uint32_t w, uint32_t h, uint32_t extra)
{
    std::mt19937 rng(seed);
    auto *a = static_cast<Paint *>(::operator new(sizeof(Paint)));
    a->mVTable = GameAddress<void>(Paint::kVTable);
    a->mRefCount = 1 + seed % 3;
    a->mWidth = w;
    a->mHeight = h;
    new (&a->mData) std::vector<tVertPaintInfo>();
    a->mData.reserve(w * h + extra);
    for (uint32_t i = 0; i < w * h; ++i) a->mData.push_back(Value(rng));
    // Spare capacity holds random bytes: a grow within capacity must store the defaults itself.
    auto *spare = reinterpret_cast<uint8_t *>(a->mData.data() + a->mData.size());
    for (size_t i = 0; i < (a->mData.capacity() - a->mData.size()) * 4; ++i) spare[i] = uint8_t(rng() | 1);
    return a;
}

void CaptureArray(Blob &out, const Paint *a)
{
    out.insert(out.end(), {uint32_t(a->mVTable == GameAddress<void>(Paint::kVTable)), a->mRefCount, a->mWidth, a->mHeight,
        uint32_t(a->mData.size()), uint32_t(a->mData.capacity()), uint32_t(a->mData.data() != nullptr)});
    for (const tVertPaintInfo &v : a->mData) {
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        out.push_back(bits);
    }
}

void Destroy(Paint *a)
{
    a->mData.~vector();
    ::operator delete(a, sizeof(Paint));
}

const uint32_t kShapes[][2] = {{0, 0}, {1, 1}, {3, 2}, {2, 3}, {4, 4}, {0, 5}, {5, 0}, {17, 3}, {33, 33}};

void ResizeCases(difftest::Context &ctx)
{
    for (auto &from : kShapes) for (auto &to : kShapes) for (uint32_t extra : {0u, 7u, 2000u}) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            auto *a = Make(from[0] * 7 + to[1], from[0], from[1], extra);
            difftest::FreeRecorder rec;
            if (side) a->Resize(to[0], to[1]);
            else ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t)>(kResize)(a, to[0], to[1]);
            rec.Stop();
            blobs[side] = {uint32_t(rec.Count())};
            CaptureArray(blobs[side], a);
            Destroy(a);
        }
        if (blobs[0] != blobs[1]) ctx.Fail("Resize %ux%u -> %ux%u extra %u: array or frees differ", from[0], from[1], to[0], to[1], extra);
        ++ctx.cases;
    }
}

Blob RunWrite(difftest::Context &ctx, bool ours, uint32_t seed, uint32_t w, uint32_t h, int fail, int *total)
{
    Begin({}, fail);
    auto *a = Make(seed, w, h, 0);
    uint8_t returned = 0;
    bool threw = false;
    try {
        returned = ours ? a->Write(OutStream()) : ctx.Original<uint8_t(__thiscall *)(void *, void *)>(kWrite)(a, OutStream());
    } catch (int) { threw = true; }
    gFailAt = -1;
    if (total) *total = gTick;
    Blob blob{returned, uint32_t(threw)};
    blob.insert(blob.end(), gLog.begin(), gLog.end());
    CaptureArray(blob, a);
    Destroy(a);
    return blob;
}

void WriteCases(difftest::Context &ctx)
{
    const uint64_t before = gThrown;
    for (auto &shape : kShapes) for (uint32_t seed : {1u, 2u}) {
        int total = 0;
        const bool sweep = shape[0] * shape[1] <= 16;
        for (int fail = -1; fail < (sweep ? total : 0); ++fail) {
            const uint32_t s = shape[0] * 13 + shape[1] + seed * 101;
            Blob original = RunWrite(ctx, false, s, shape[0], shape[1], fail, fail < 0 ? &total : nullptr);
            Blob ours = RunWrite(ctx, true, s, shape[0], shape[1], fail, nullptr);
            if (original != ours) ctx.Fail("Write %ux%u seed %u fail %d: result / calls / array differ", shape[0], shape[1], seed, fail);
            ++ctx.cases;
        }
    }
    std::printf("  Write: exceptions injected %llu\n", gThrown - before);
}

// A paint record: header, then per element a count and that many bytes. Counts over 3 (the original's unchecked count)
// only where the bytes stay inside the array (`elements` of them); `wideValues`: bytes above 0xff (read as u16).
std::vector<Item> Record(uint32_t seed, uint32_t w, uint32_t h, uint32_t elements, bool overflow, bool wideValues,
    uint32_t classId = Paint::kClassId, uint32_t version = 1, bool text = true)
{
    std::vector<Item> s{U(classId), U(version), text ? S("c2DArray") : U(5), U(w), U(h)};
    std::mt19937 rng(seed);
    for (uint32_t i = 0; i < elements; ++i) {
        uint32_t count = rng() % 4;
        if (overflow && rng() % 3 == 0) {
            const uint32_t room = (w * h - i) * 4; // bytes from this element to the array's end
            count = 4 + rng() % 6;
            if (count > room) count = room;
        }
        s.push_back(U(count));
        for (uint32_t k = 0; k < count; ++k) s.push_back(U(wideValues && rng() % 2 ? 0x100u | rng() % 0x10000 : PaintByte(rng)));
    }
    return s;
}

Blob RunRead(difftest::Context &ctx, bool ours, const std::vector<Item> &script, uint32_t seed, uint32_t w, uint32_t h,
    int fail, int *total, bool keep = false)
{
    Begin(script, fail, keep);
    auto *a = Make(seed, w, h, seed % 3 == 2 ? 40 : seed % 5);
    uint8_t returned = 0;
    bool threw = false;
    try {
        returned = ours ? a->Read(InStream()) : ctx.Original<uint8_t(__thiscall *)(void *, void *)>(kRead)(a, InStream());
    } catch (int) { threw = true; }
    gFailAt = -1;
    if (total) *total = gTick;
    Blob blob{returned, uint32_t(threw), uint32_t(gCursor)};
    blob.insert(blob.end(), gLog.begin(), gLog.end());
    CaptureArray(blob, a);
    Destroy(a);
    return blob;
}

void ReadCases(difftest::Context &ctx)
{
    const uint32_t id = Paint::kClassId;
    std::vector<std::vector<Item>> scripts{{}, {U(id)}, {U(id), U(1)}, {U(id + 1), U(1)}, {U(0), U(1)}};
    for (uint32_t version : {0u, 2u, 0xffffffffu}) scripts.push_back(Record(3, 2, 2, 4, false, false, id, version));
    for (auto &shape : kShapes) {
        const uint32_t n = shape[0] * shape[1];
        if (n > 300) continue;
        scripts.push_back(Record(n + 1, shape[0], shape[1], n, false, false));
        scripts.push_back(Record(n + 2, shape[0], shape[1], n, true, true));
    }
    // Short streams (the header, a count without its bytes, part of the elements) and a long one.
    for (uint32_t cut : {4u, 5u, 6u, 7u, 9u, 14u}) {
        auto s = Record(77, 3, 3, 9, true, false);
        if (cut < s.size()) s.resize(cut);
        scripts.push_back(s);
    }
    scripts.push_back(Record(78, 2, 2, 7, false, false));
    scripts.push_back(Record(79, 3, 3, 9, false, false, id, 1, false)); // the name is a number, not text: still consumed
    uint32_t &terrainVersion = *GameAddress<uint32_t>(kTerrainVersion);
    const uint32_t saved = terrainVersion;
    const uint64_t before = gThrown;
    size_t index = 0;
    for (uint32_t version : {3u, 4u, 0u, 7u}) {
        terrainVersion = version;
        for (auto &script : scripts) {
            // Mode B (a failed read leaves its target as it was) where every local the original reads uninitialised is
            // written first: a whole header and the first count, and no u16 bytes (version >= 4). A failed count then
            // keeps the previous element's.
            const bool modeB = version >= 4 && script.size() >= 6;
            for (int keep = 0; keep <= int(modeB); ++keep) for (uint32_t start = 0; start < 3; ++start) {
                int total = 0;
                const bool sweep = script.size() <= 60;
                for (int fail = -1; fail < (sweep ? total : 0); ++fail) {
                    const uint32_t w = start * 2, h = start + 1;
                    Blob original = RunRead(ctx, false, script, uint32_t(index), w, h, fail, fail < 0 ? &total : nullptr, keep);
                    Blob ours = RunRead(ctx, true, script, uint32_t(index), w, h, fail, nullptr, keep);
                    if (original != ours)
                        ctx.Fail("Read version %u script %zu mode %c start %ux%u fail %d: result / calls / array differ", version,
                            index, keep ? 'B' : 'A', w, h, fail);
                    ++ctx.cases;
                }
            }
            ++index;
        }
    }
    terrainVersion = saved;
    std::printf("  Read: exceptions injected %llu\n", gThrown - before);
}

void DtorCases(difftest::Context &ctx)
{
    for (uint32_t flags : {0u, 1u, 2u, 3u}) for (auto &shape : kShapes) for (int body = 0; body < 2; ++body) {
        if (body && flags) continue;
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            auto *a = Make(flags + shape[0], shape[0], shape[1], flags);
            a->mVTable = nullptr; // the original stores its vtable itself
            difftest::FreeRecorder rec;
            void *r = nullptr;
            if (body) {
                if (side) a->Destruct();
                else ctx.Original<void(__thiscall *)(void *)>(kBody)(a);
            } else {
                r = side ? a->DeletingDestructor(flags) : ctx.Original<void *(__thiscall *)(void *, uint32_t)>(kVf6)(a, flags);
            }
            rec.Stop();
            size_t sizes[4]{};
            const size_t freedCount = rec.SortedSizes(sizes, 4);
            blobs[side] = {uint32_t(r == a), uint32_t(rec.Freed(a)), uint32_t(freedCount), uint32_t(sizes[0]), uint32_t(sizes[1])};
            if (!rec.Freed(a)) {
                CaptureArray(blobs[side], a);
                Destroy(a);
            }
        }
        if (blobs[0] != blobs[1]) ctx.Fail("%s flags %u %ux%u: frees / state differ", body ? "Destruct" : "DeletingDestructor", flags, shape[0], shape[1]);
        ++ctx.cases;
    }
}

void ClassIdCases(difftest::Context &ctx)
{
    // Slot 5 of every copy: the same body; called on each copy's layout.
    auto *paint = Make(1, 2, 2, 0);
    const uint32_t original = ctx.Original<uint32_t(__thiscall *)(void *)>(kClsid)(paint);
    if (original != paint->GetGZCLSID()) ctx.Fail("GetGZCLSID: 0x%08x against ours 0x%08x", original, paint->GetGZCLSID());
    nTSWorld::c2DArray<float> heights{};
    nTSWorld::c2DArray<uint8_t> bytes{};
    if (original != heights.GetGZCLSID() || original != bytes.GetGZCLSID()) ctx.Fail("GetGZCLSID: the float / u8 copies differ");
    Destroy(paint);
    ++ctx.cases;
}
} // namespace

DIFF_TEST(C2DArrayPaint_Resize, 0x8040b0) { ResizeCases(ctx); }
DIFF_TEST(C2DArrayPaint_Write, 0x804d40) { WriteCases(ctx); }
DIFF_TEST(C2DArrayPaint_Read, 0x803340) { ReadCases(ctx); }
DIFF_TEST(C2DArrayPaint_Destructors, 0x801730) { DtorCases(ctx); } // and the body 0x801440
DIFF_TEST(C2DArrayPaint_GetGZCLSID, 0x4af570) { ClassIdCases(ctx); }
