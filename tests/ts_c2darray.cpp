// nTSWorld::c2DArray<float> / <unsigned char> (0x1c bytes, cIGZSerializable): differential tests of Resize, Write, Read and the
// deleting destructor, original (by RVA) against ours on equal arrays. The fake streams log every call and can throw at a
// chosen call (the local cRZString of Write / Read is destroyed by the unwinding).
#include "difftest.h"
#include "free_recorder.h"
#include "game/game.h"
#include "ifaces/cIGZIStream.h"
#include "ifaces/cIGZOStream.h"
#include "ts/c2DArray.h"
#include "ts_nhood_fakes.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace {
using namespace nhoodfx;
using Blob = std::vector<uint32_t>;

template <class T> struct Rvas;
template <> struct Rvas<float> { static constexpr uint32_t resize = 0x4b1db0, write = 0x4b2e90, read = 0x4b0870, dtor = 0x4ad220; };
template <> struct Rvas<uint8_t> { static constexpr uint32_t resize = 0x4b1d20, write = 0x4b2d80, read = 0x4b0770, dtor = 0x4ad200; };

template <class T> T Value(std::mt19937 &rng)
{
    if constexpr (std::is_same_v<T, float>) return float(int(rng() % 20001) - 10000) / 7.0f;
    else return uint8_t(rng());
}

template <class T> nTSWorld::c2DArray<T> *Make(uint32_t seed, uint32_t w, uint32_t h, uint32_t extra)
{
    std::mt19937 rng(seed);
    auto *a = static_cast<nTSWorld::c2DArray<T> *>(::operator new(sizeof(nTSWorld::c2DArray<T>)));
    a->mVTable = GameAddress<void>(nTSWorld::c2DArray<T>::kVTable);
    a->mRefCount = 1 + seed % 3;
    a->mWidth = w;
    a->mHeight = h;
    new (&a->mData) std::vector<T>();
    a->mData.reserve(w * h + extra);
    for (uint32_t i = 0; i < w * h; ++i) a->mData.push_back(Value<T>(rng));
    // Spare capacity holds random bytes (fresh heap is often zero): a grow within capacity must store its zeros itself.
    auto *spare = reinterpret_cast<uint8_t *>(a->mData.data() + a->mData.size());
    for (size_t i = 0; i < (a->mData.capacity() - a->mData.size()) * sizeof(T); ++i) spare[i] = uint8_t(rng() | 1);
    return a;
}

template <class T> void Capture(Blob &out, const nTSWorld::c2DArray<T> *a)
{
    out.insert(out.end(), {uint32_t(a->mVTable == GameAddress<void>(nTSWorld::c2DArray<T>::kVTable)), a->mRefCount, a->mWidth,
        a->mHeight, uint32_t(a->mData.size()), uint32_t(a->mData.capacity())});
    for (T v : a->mData) {
        uint32_t bits = 0;
        std::memcpy(&bits, &v, sizeof(T));
        out.push_back(bits);
    }
}

template <class T> void Destroy(nTSWorld::c2DArray<T> *a)
{
    a->mData.~vector();
    ::operator delete(a, sizeof(nTSWorld::c2DArray<T>));
}

template <class T> using Shape = std::pair<uint32_t, uint32_t>;
const uint32_t kShapes[][2] = {{0, 0}, {1, 1}, {3, 2}, {2, 3}, {4, 4}, {9, 9}, {0, 5}, {5, 0}, {17, 3}, {33, 33}};

template <class T> void ResizeCases(difftest::Context &ctx)
{
    for (auto &from : kShapes) for (auto &to : kShapes) for (uint32_t extra : {0u, 7u, 2000u}) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            auto *a = Make<T>(from[0] * 7 + to[1], from[0], from[1], extra);
            difftest::FreeRecorder rec;
            if (side) a->Resize(to[0], to[1]);
            else ctx.Original<void(__thiscall *)(void *, uint32_t, uint32_t)>(Rvas<T>::resize)(a, to[0], to[1]);
            rec.Stop();
            blobs[side] = {uint32_t(rec.Count())};
            Capture(blobs[side], a);
            Destroy(a);
        }
        if (blobs[0] != blobs[1]) ctx.Fail("Resize %ux%u -> %ux%u extra %u: array or frees differ", from[0], from[1], to[0], to[1], extra);
        ++ctx.cases;
    }
}

template <class T> Blob RunWrite(difftest::Context &ctx, bool ours, uint32_t seed, uint32_t w, uint32_t h, int fail, int *total)
{
    Begin({}, fail);
    auto *a = Make<T>(seed, w, h, 0);
    uint8_t returned = 0;
    bool threw = false;
    try {
        auto *stream = reinterpret_cast<ifaces::cIGZOStream *>(&gOut);
        returned = ours ? a->Write(stream) : ctx.Original<uint8_t(__thiscall *)(void *, void *)>(Rvas<T>::write)(a, stream);
    } catch (int) { threw = true; }
    gFailAt = -1;
    if (total) *total = gTick;
    Blob blob{returned, uint32_t(threw)};
    blob.insert(blob.end(), gLog.begin(), gLog.end());
    Capture(blob, a);
    Destroy(a);
    return blob;
}

template <class T> Blob RunRead(difftest::Context &ctx, bool ours, const std::vector<Item> &script, uint32_t seed, uint32_t w, uint32_t h,
    int fail, int *total)
{
    Begin(script, fail);
    // Every third start has room to grow in place (DL): a short stream then shows the zeros Read's resize stores.
    auto *a = Make<T>(seed, w, h, seed % 3 == 2 ? 40 : seed % 5);
    uint8_t returned = 0;
    bool threw = false;
    try {
        auto *stream = reinterpret_cast<ifaces::cIGZIStream *>(&gIn);
        returned = ours ? a->Read(stream) : ctx.Original<uint8_t(__thiscall *)(void *, void *)>(Rvas<T>::read)(a, stream);
    } catch (int) { threw = true; }
    gFailAt = -1;
    if (total) *total = gTick;
    Blob blob{returned, uint32_t(threw), uint32_t(gCursor)};
    blob.insert(blob.end(), gLog.begin(), gLog.end());
    Capture(blob, a);
    Destroy(a);
    return blob;
}

template <class T> std::vector<Item> Record(uint32_t classId, uint32_t version, uint32_t w, uint32_t h, uint32_t count, bool text = true)
{
    std::vector<Item> s{U(classId), U(version)};
    s.push_back(text ? S("c2DArray") : U(5));
    s.push_back(U(w));
    s.push_back(U(h));
    std::mt19937 rng(w * 31 + h);
    for (uint32_t i = 0; i < count; ++i) {
        if constexpr (std::is_same_v<T, float>) s.push_back(F(Value<T>(rng)));
        else s.push_back(U(rng() & 0xff));
    }
    return s;
}

template <class T> void WriteCases(difftest::Context &ctx)
{
    uint64_t before = gThrown;
    for (auto &shape : kShapes) {
        int total = 0;
        for (int fail = -1; fail < total; ++fail) {
            Blob original = RunWrite<T>(ctx, false, shape[0] + 3, shape[0], shape[1], fail, fail < 0 ? &total : nullptr);
            Blob ours = RunWrite<T>(ctx, true, shape[0] + 3, shape[0], shape[1], fail, nullptr);
            if (original != ours) ctx.Fail("Write %ux%u fail %d: result / calls / array differ", shape[0], shape[1], fail);
            ++ctx.cases;
        }
    }
    std::printf("  Write: exceptions injected %llu\n", gThrown - before);
}

template <class T> void ReadCases(difftest::Context &ctx)
{
    constexpr uint32_t kId = 0x6b943b43;
    std::vector<std::vector<Item>> scripts{{}, {U(kId)}, {U(kId), U(1)}, {U(kId + 1), U(1)}, {U(0), U(1)}};
    for (uint32_t version : {0u, 1u, 2u, 0xffffffffu}) scripts.push_back(Record<T>(kId, version, 2, 2, 4));
    for (auto &shape : kShapes) for (uint32_t extra : {0u, 1u}) scripts.push_back(Record<T>(kId, 1, shape[0], shape[1], shape[0] * shape[1] + extra));
    // The dimensions do not need to agree with the element count: a short stream leaves the zero-filled tail.
    for (uint32_t cut : {5u, 6u, 8u, 12u}) {
        auto s = Record<T>(kId, 1, 4, 4, 16);
        s.resize(cut);
        scripts.push_back(s);
    }
    scripts.push_back(Record<T>(kId, 1, 3, 3, 9, false)); // the name is a number, not text: still consumed
    uint64_t before = gThrown;
    size_t index = 0;
    for (auto &script : scripts) for (uint32_t shape = 0; shape < 4; ++shape) {
        int total = 0;
        for (int fail = -1; fail < total; ++fail) {
            const uint32_t w = shape * 2, h = shape + 1;
            Blob original = RunRead<T>(ctx, false, script, uint32_t(index), w, h, fail, fail < 0 ? &total : nullptr);
            Blob ours = RunRead<T>(ctx, true, script, uint32_t(index), w, h, fail, nullptr);
            if (original != ours) ctx.Fail("Read script %zu start %ux%u fail %d: result / calls / array differ", index, w, h, fail);
            ++ctx.cases;
        }
        ++index;
    }
    std::printf("  Read: exceptions injected %llu\n", gThrown - before);
}

template <class T> void DtorCases(difftest::Context &ctx)
{
    for (uint32_t flags : {0u, 1u, 2u, 3u}) for (auto &shape : kShapes) {
        Blob blobs[2];
        for (int side = 0; side < 2; ++side) {
            auto *a = Make<T>(flags + shape[0], shape[0], shape[1], flags);
            difftest::FreeRecorder rec;
            void *r = side ? a->DeletingDestructor(flags) : ctx.Original<void *(__thiscall *)(void *, uint32_t)>(Rvas<T>::dtor)(a, flags);
            rec.Stop();
            size_t sizes[4]{};
            const size_t freedCount = rec.SortedSizes(sizes, 4);
            blobs[side] = {uint32_t(r == a), uint32_t(rec.Freed(a)), uint32_t(freedCount), uint32_t(sizes[0]), uint32_t(sizes[1])};
            if (!rec.Freed(a)) { Capture(blobs[side], a); Destroy(a); }
        }
        if (blobs[0] != blobs[1]) ctx.Fail("DeletingDestructor flags %u %ux%u: frees / state differ", flags, shape[0], shape[1]);
        ++ctx.cases;
    }
}
} // namespace

DIFF_TEST(C2DArray_FloatResize, 0x4b1db0) { ResizeCases<float>(ctx); }
DIFF_TEST(C2DArray_ByteResize, 0x4b1d20) { ResizeCases<uint8_t>(ctx); }
DIFF_TEST(C2DArray_FloatWrite, 0x4b2e90) { WriteCases<float>(ctx); }
DIFF_TEST(C2DArray_ByteWrite, 0x4b2d80) { WriteCases<uint8_t>(ctx); }
DIFF_TEST(C2DArray_FloatRead, 0x4b0870) { ReadCases<float>(ctx); }
DIFF_TEST(C2DArray_ByteRead, 0x4b0770) { ReadCases<uint8_t>(ctx); }
DIFF_TEST(C2DArray_FloatDeletingDestructor, 0x4ad220) { DtorCases<float>(ctx); }
DIFF_TEST(C2DArray_ByteDeletingDestructor, 0x4ad200) { DtorCases<uint8_t>(ctx); }
