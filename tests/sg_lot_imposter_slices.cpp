// Differential tests for the lot imposter manager's slice texture code (lane 261111r, scenegraph row LT2b2a;
// src/sg/cLotImposterManager.h): ScaleImage 0x36c690, CopySliceBorders 0x366fe0 and PlaceSlice 0x3632b0.
//
// The images are logging fakes sharing one vtable (cIImageData slots 19 SetPixelOpaque, 20 SetPixel, 26 LevelWidth,
// 27 LevelHeight, 38 GetPixelRGB, 39 GetPixel; any other slot aborts). One ordered log per side holds every call with
// its arguments, the colour words the callee sees and where the colour lives (the last GetPixel's buffer, the fake's
// own buffer, the stack). GetPixel sometimes answers in its own buffer instead of the caller's.
//
// ImposterSlices_Scale: the scene graph system (0x20defb stood in) answers a source (slot 48) whose slot 23 gives the
// divisor for the asked index (NaN-dense floats: zeros, infinities, NaNs, denormals); every pixel scaled by 1/divisor.
// ImposterSlices_Borders: a manager image whose +0x5c holds three image fakes and whose +0x140 holds 0-5 slice records
// (texture index -1..2, NaN-dense texture coordinates and ones on the texel grid), with the flags at +0x198..+0x19b,
// the ring count +0x150 (0, 1-3, negative) and the asked index varied.
// ImposterSlices_Place: a manager image with 0-3 atlases already partly allocated (+0x154), the texture size +0xa4,
// the tile size +0x14c and the border +0x150 varied; the record's row count comes from a stand-in for 0x3f6a30, which
// logs the record's texture word when called. Compared: the log, the record's 20 words, the whole atlas vector (every
// node and free list) and each side's heap blocks.
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "shape.h"

#include "gz/cTextureAtlas.h"
#include "hook/hooks.h"
#include "sg/cLotImposterManager.h"

#include <Windows.h>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <random>
#include <vector>

namespace lotimpslices_test {

using nTSSG::cLotImposterManager;
using Mgr = cLotImposterManager;
using Atlases = std::vector<nGZGraphic4::cTextureAtlas>;
using Records = std::vector<std::array<uint8_t, 0x50>>;
using Log = std::vector<uint32_t>;

Log gLog;
void L(uint32_t v) { gLog.push_back(v); }
uint32_t Bits(float f) { return std::bit_cast<uint32_t>(f); }
float Fb(uint32_t b) { return std::bit_cast<float>(b); }

const uint32_t kFloatBits[] = {
    0x00000000, 0x80000000, 0x3f800000, 0xbf800000, 0x3f000000, 0xbf000000, 0x3dcccccd, 0x7f800000, 0xff800000,
    0x7fc00000, 0xffc00000, 0x7fc12345, 0x7f800001, 0x00000001, 0x807fffff, 0x40200000, 0x4b000001, 0x7f7fffff,
    0x3fc00000, 0x3e800000, 0x3f400000, 0x3eaaaaab, 0x40400000, 0x00800000,
};
constexpr unsigned kFloatCount = sizeof(kFloatBits) / 4;

// ---- the image fakes ----
struct FakeImage {
    void **vt;
    uint32_t id;
};
void *gImageVT[48];
FakeImage gImages[3];
struct ImageScript {
    int32_t width, height;
    bool elsewhere; // GetPixel answers in its own buffer on some calls
    // Borders: on SetPixel call `changeAt` (-1: never) the manager's slice records shrink to `keep` (when fewer than
    // now) and its ring count +0x150 becomes `rings` (the original reads both again: each slice, each ring).
    int32_t changeAt;
    uint32_t keep;
    int32_t rings;
};
ImageScript gS;
uint8_t *gBorderMgr;
uint32_t gGetCalls, gSetCalls;
void ChangeManager();
const float *gLastOut;
float gOther[4];

bool IsStack(const void *p)
{
    const auto *tib = reinterpret_cast<const NT_TIB *>(NtCurrentTeb());
    return p >= tib->StackLimit && p < tib->StackBase;
}
uint32_t Where(const void *p)
{
    if (!p) return 0;
    if (p == gLastOut) return 0xc01;
    if (p == gOther) return 0x07e;
    if (IsStack(p)) return 0x57ac;
    return 0xbad;
}
uint32_t Id(void *self) { return static_cast<FakeImage *>(self)->id; }

void __fastcall SetPixel(void *self, void *, int32_t x, int32_t y, const float *colour, int32_t level)
{
    L(0x20), L(Id(self)), L(uint32_t(x)), L(uint32_t(y)), L(Where(colour)), L(uint32_t(level));
    for (int k = 0; k < 4; ++k) L(Bits(colour[k]));
    if (gBorderMgr && int32_t(gSetCalls) == gS.changeAt) ChangeManager();
    ++gSetCalls;
}
void __fastcall SetPixelOpaque(void *self, void *, int32_t x, int32_t y, const float *colour, int32_t level)
{
    L(0x19), L(Id(self)), L(uint32_t(x)), L(uint32_t(y)), L(Where(colour) == 0x57ac ? 0x57ac : 0xbad), L(uint32_t(level));
    for (int k = 0; k < 3; ++k) L(Bits(colour[k]));
    ++gSetCalls;
}
int32_t __fastcall Width(void *self, void *, uint32_t level)
{
    L(0x26), L(Id(self)), L(level);
    return gS.width;
}
int32_t __fastcall Height(void *self, void *, uint32_t level)
{
    L(0x27), L(Id(self)), L(level);
    return gS.height;
}
float *Answer(float *out, uint32_t tag, int32_t x, int32_t y, uint32_t channels)
{
    const uint32_t n = gGetCalls++;
    float *const into = gS.elsewhere && n % 3 != 1 ? gOther : out;
    // Only `channels` floats into the caller's buffer: GetPixelRGB's is three floats in the original (0x36c690), and a
    // fourth landed on a register it saved (its caller's ebp in some builds of this test).
    for (uint32_t k = 0; k < (into == out ? channels : 4u); ++k) {
        const uint32_t pick = (n * 7 + k * 5 + uint32_t(x) * 3 + uint32_t(y) + tag) % 41;
        into[k] = pick < kFloatCount ? Fb(kFloatBits[pick]) : float(int32_t(pick) - 20) / 3.0f;
    }
    if (into != out)
        for (uint32_t k = 0; k < channels; ++k) out[k] = -1000.0f - float(k);
    gLastOut = out;
    return into;
}
float *__fastcall GetPixel(void *self, void *, float *out, int32_t x, int32_t y, int32_t level)
{
    L(0x39), L(Id(self)), L(IsStack(out) ? 0x57ac : 0xbad), L(uint32_t(x)), L(uint32_t(y)), L(uint32_t(level));
    return Answer(out, 0x39, x, y, 4);
}
float *__fastcall GetPixelRGB(void *self, void *, float *out, int32_t x, int32_t y, int32_t level)
{
    L(0x38), L(Id(self)), L(IsStack(out) ? 0x57ac : 0xbad), L(uint32_t(x)), L(uint32_t(y)), L(uint32_t(level));
    return Answer(out, 0x38, x, y, 3);
}
void __fastcall BadSlot(void *, void *)
{
    std::fprintf(stderr, "slices test: an unexpected slot\n");
    std::abort();
}

void SetupImages()
{
    for (void *&s : gImageVT) s = reinterpret_cast<void *>(&BadSlot);
    gImageVT[19] = reinterpret_cast<void *>(&SetPixelOpaque);
    gImageVT[20] = reinterpret_cast<void *>(&SetPixel);
    gImageVT[26] = reinterpret_cast<void *>(&Width);
    gImageVT[27] = reinterpret_cast<void *>(&Height);
    gImageVT[38] = reinterpret_cast<void *>(&GetPixelRGB);
    gImageVT[39] = reinterpret_cast<void *>(&GetPixel);
    for (uint32_t i = 0; i < 3; ++i) gImages[i] = {gImageVT, 0x1a0 + i};
}
void BeginLog()
{
    gLog.clear();
    gGetCalls = gSetCalls = 0;
    gLastOut = nullptr;
}

// ---- the scene graph system (0x20defb) and its source ----
struct FakeObj {
    void **vt;
} gSystem, gSource;
void *gSystemVT[64], *gSourceVT[32];
float gDivisor;
void *__fastcall SystemSource(void *self, void *)
{
    L(0x48), L(self == &gSystem);
    return &gSource;
}
float __fastcall SourceValue(void *self, void *, int32_t which)
{
    L(0x23), L(self == &gSource), L(uint32_t(which)), L(Bits(gDivisor));
    return gDivisor;
}
void *__cdecl StandSystem()
{
    L(0xdefb);
    return &gSystem;
}

// ---- 0x3f6a30, the record's row count ----
int32_t gRows;
const uint8_t *gRecord;
int32_t __fastcall StandRows(void *self, void *)
{
    int32_t texture;
    std::memcpy(&texture, static_cast<uint8_t *>(self) + 0x38, 4);
    L(0x6a30), L(self == gRecord), L(uint32_t(texture));
    return gRows;
}

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

void DumpAtlases(const uint8_t *o)
{
    const auto &atlases = *reinterpret_cast<const Atlases *>(o + 0x154);
    uint32_t words[3];
    std::memcpy(words, o + 0x154, 12);
    L(0xa71a5), L(uint32_t(atlases.size())), L((words[2] - words[0]) / 0x20);
    for (const auto &a : atlases) {
        L(a.mRegions), L(uint32_t(a.mFreeArea)), L(uint32_t(a.mNodes.size())), L(uint32_t(a.mFreeNodes.size()));
        for (const auto &n : a.mNodes) {
            uint32_t w[8];
            std::memcpy(w, &n, 0x20);
            w[7] &= 0xff; // the bool's padding
            for (uint32_t v : w) L(v);
        }
        for (uint32_t f : a.mFreeNodes) L(f);
    }
}

bool Compare(difftest::Context &ctx, const char *what, uint32_t seed, const Log (&logs)[2])
{
    ++ctx.cases;
    if (logs[0] == logs[1]) return true;
    size_t k = 0;
    while (k < logs[0].size() && k < logs[1].size() && logs[0][k] == logs[1][k]) ++k;
    ctx.Fail("%s seed %u: the logs differ at word %zu of %zu / %zu (original %08x, ours %08x)", what, seed, k,
             logs[0].size(), logs[1].size(), k < logs[0].size() ? logs[0][k] : 0u, k < logs[1].size() ? logs[1][k] : 0u);
    return false;
}

using ScaleFn = void(__cdecl *)(void *, int32_t);
using BordersFn = void(__thiscall *)(void *, int32_t);
using PlaceFn = void(__thiscall *)(void *, void *);

} // namespace lotimpslices_test

using namespace lotimpslices_test;

DIFF_TEST(ImposterSlices_Scale, 0x36c690)
{
    gLog.reserve(1u << 20);
    SetupImages();
    for (void *&s : gSystemVT) s = reinterpret_cast<void *>(&BadSlot);
    for (void *&s : gSourceVT) s = reinterpret_cast<void *>(&BadSlot);
    gSystemVT[48] = reinterpret_cast<void *>(&SystemSource);
    gSourceVT[23] = reinterpret_cast<void *>(&SourceValue);
    gSystem.vt = gSystemVT;
    gSource.vt = gSourceVT;
    shape::CodePatch system{0x20defbu, shape::CodeOf(&StandSystem)};
    static const int32_t kWhich[] = {0, 1, 2, -1, 0x7fffffff, int32_t(0x80000000), 0x101};
    int painted = 0, empty = 0;
    for (uint32_t seed = 0; seed < 1500; ++seed) {
        Log logs[2];
        for (int side = 0; side < 2; ++side) {
            std::mt19937 r(seed * 2654435761u + 0x36c690u);
            BeginLog();
            gS = {};
            gS.width = r() % 6 == 0 ? -int32_t(r() % 3) : int32_t(r() % 9);
            gS.height = r() % 6 == 0 ? -int32_t(r() % 3) : int32_t(r() % 9);
            gS.elsewhere = r() % 3 == 0;
            gDivisor = r() % 3 ? Fb(kFloatBits[r() % kFloatCount]) : float(int32_t(r() % 2001) - 1000) / 8.0f;
            const int32_t which = kWhich[r() % 7];
            FakeImage *const image = &gImages[r() % 3];
            if (side == 0) ctx.Original<ScaleFn>(0x36c690)(image, which);
            else Mgr::ScaleImage(image, which);
            L(0xe0d), L(gSetCalls), L(gGetCalls);
            logs[side] = gLog;
            if (side == 0) (gSetCalls ? painted : empty)++;
        }
        if (!Compare(ctx, "scale", seed, logs) && ctx.failures.size() >= 6) return;
    }
    if (painted < 600 || empty < 100) ctx.Fail("coverage: painted %d, empty %d", painted, empty);
}

namespace lotimpslices_test {

struct BorderCase {
    uint8_t flags[4];
    int32_t rings;
    int32_t index;
    unsigned records;
    struct Rec {
        int32_t texture;
        float uv[4];
    } recs[5];
};

float Coordinate(std::mt19937 &r, int32_t size)
{
    switch (r() % 5) {
    case 0: return Fb(kFloatBits[r() % kFloatCount]);
    case 1: return float(int32_t(r() % 7) - 2) / 4.0f;
    default: return size > 0 ? float(int32_t(r() % uint32_t(size + 1))) / float(size) : 0.5f;
    }
}

uint8_t *BuildBorderManager(const BorderCase &c)
{
    auto *o = static_cast<uint8_t *>(::operator new(Mgr::kSize));
    std::memset(o, 0xc5, Mgr::kSize);
    auto *images = new (o + 0x5c) std::vector<void *>();
    for (auto &image : gImages) images->push_back(&image);
    auto *records = new (o + 0x140) Records(c.records);
    for (unsigned i = 0; i < c.records; ++i) {
        uint8_t *rec = (*records)[i].data();
        for (uint32_t k = 0; k < 0x50; ++k) rec[k] = uint8_t(0x40 + k + i);
        std::memcpy(rec + 0x38, &c.recs[i].texture, 4);
        std::memcpy(rec + 0x3c, c.recs[i].uv, 16);
    }
    std::memcpy(o + 0x150, &c.rings, 4);
    std::memcpy(o + 0x198, c.flags, 4);
    return o;
}
void ChangeManager()
{
    auto *records = reinterpret_cast<Records *>(gBorderMgr + 0x140);
    L(0xc4a), L(uint32_t(records->size())), L(gS.keep), L(uint32_t(gS.rings));
    if (gS.keep < records->size()) records->resize(gS.keep); // shrinking: no reallocation under the caller
    std::memcpy(gBorderMgr + 0x150, &gS.rings, 4);
}
void FreeBorderManager(uint8_t *o)
{
    std::destroy_at(reinterpret_cast<std::vector<void *> *>(o + 0x5c));
    std::destroy_at(reinterpret_cast<Records *>(o + 0x140));
    ::operator delete(o);
}

} // namespace lotimpslices_test

DIFF_TEST(ImposterSlices_Borders, 0x366fe0)
{
    gLog.reserve(1u << 22);
    SetupImages();
    const auto bordersO = ctx.Original<BordersFn>(0x366fe0);
    const auto bordersU = reinterpret_cast<BordersFn>(MemberAddress(&Mgr::CopySliceBorders));
    static const int32_t kRings[] = {0, 1, 2, 3, 1, 2, -1, int32_t(0x80000000)};
    int drawn = 0, skipped = 0, clipped = 0;
    for (uint32_t seed = 0; seed < 2500; ++seed) {
        std::mt19937 r(seed * 40503u + 0x366fe0u);
        BorderCase c{};
        for (uint8_t &f : c.flags) f = r() % 4 == 0 ? 0 : uint8_t(1 + r() % 255);
        c.rings = kRings[r() % 8];
        c.index = int32_t(r() % 3);
        c.records = r() % 6;
        gS = {};
        gS.width = 1 + int32_t(r() % 20);
        gS.height = r() % 3 ? gS.width : 1 + int32_t(r() % 20);
        gS.elsewhere = r() % 3 == 0;
        gS.changeAt = -1;
        if (r() % 3 == 0) {
            gS.changeAt = int32_t(r() % 40);
            gS.keep = r() % 2 ? 6 : r() % 4; // 6: the records stay
            gS.rings = r() % 2 ? c.rings : kRings[r() % 8];
        }
        for (unsigned i = 0; i < c.records; ++i) {
            c.recs[i].texture = r() % 3 ? c.index : int32_t(r() % 4) - 1;
            c.recs[i].uv[0] = Coordinate(r, gS.width);
            c.recs[i].uv[1] = Coordinate(r, gS.height);
            c.recs[i].uv[2] = r() % 4 ? c.recs[i].uv[0] + float(1 + r() % 6) / float(gS.width) : Coordinate(r, gS.width);
            c.recs[i].uv[3] = r() % 4 ? c.recs[i].uv[1] + float(1 + r() % 6) / float(gS.height) : Coordinate(r, gS.height);
        }
        Log logs[2];
        for (int side = 0; side < 2; ++side) {
            BeginLog();
            uint8_t *o = BuildBorderManager(c);
            gBorderMgr = o;
            (side ? bordersU : bordersO)(o, c.index);
            gBorderMgr = nullptr;
            L(0xe0d), L(gSetCalls), L(gGetCalls);
            for (uint32_t k = 0; k < Mgr::kSize; k += 4) {
                if (k == 0x5c || k == 0x60 || k == 0x64 || k == 0x140 || k == 0x144 || k == 0x148) continue;
                uint32_t w;
                std::memcpy(&w, o + k, 4);
                L(w);
            }
            for (const auto &rec : *reinterpret_cast<Records *>(o + 0x140))
                for (uint32_t k = 0; k < 0x50; k += 4) {
                    uint32_t w;
                    std::memcpy(&w, rec.data() + k, 4);
                    L(w);
                }
            FreeBorderManager(o);
            logs[side] = gLog;
            if (side == 0) {
                (gSetCalls ? drawn : skipped)++;
                for (size_t k = 0; k + 2 < gLog.size(); ++k)
                    if (gLog[k] == 0x20 && (gLog[k + 2] == 0 || gLog[k + 2] == uint32_t(gS.width - 1))) {
                        ++clipped;
                        break;
                    }
            }
        }
        if (!Compare(ctx, "borders", seed, logs) && ctx.failures.size() >= 6) return;
    }
    if (drawn < 500 || skipped < 500 || clipped < 200)
        ctx.Fail("coverage: drawn %d, skipped %d, at an edge %d", drawn, skipped, clipped);
}

namespace lotimpslices_test {

struct PlaceCase {
    int32_t size, tile, border, columns, rows;
    unsigned atlases;
    uint32_t taken[3][4]; // per atlas: up to four (w, h) regions taken before the call, packed 16:16
};

uint8_t *BuildPlaceManager(const PlaceCase &c)
{
    auto *o = static_cast<uint8_t *>(::operator new(Mgr::kSize));
    std::memset(o, 0xc5, Mgr::kSize);
    std::memcpy(o + 0xa4, &c.size, 4);
    std::memcpy(o + 0x14c, &c.tile, 4);
    std::memcpy(o + 0x150, &c.border, 4);
    auto *atlases = new (o + 0x154) Atlases();
    for (unsigned i = 0; i < c.atlases; ++i) {
        auto &a = atlases->emplace_back();
        a.Init(c.size, c.size);
        for (uint32_t t : c.taken[i])
            if (t) a.Allocate(int32_t(t >> 16), int32_t(t & 0xffff));
    }
    return o;
}
void FreePlaceManager(uint8_t *o)
{
    std::destroy_at(reinterpret_cast<Atlases *>(o + 0x154));
    ::operator delete(o);
}

} // namespace lotimpslices_test

DIFF_TEST(ImposterSlices_Place, 0x3632b0)
{
    gLog.reserve(1u << 20);
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    shape::StandIn rows{0x3f6a30u, &nTSSG::cImposterSliceRecord::RowCount, &StandRows};
    const auto placeO = ctx.Original<PlaceFn>(0x3632b0);
    const auto placeU = reinterpret_cast<PlaceFn>(MemberAddress(&Mgr::PlaceSlice));
    static const int32_t kSizes[] = {0x80, 0x40, 0x100, 16, 32, 0x80, 0, 24};
    static const int32_t kTiles[] = {8, 4, 1, 0, 16, 8, 3};
    static const int32_t kBorders[] = {1, 0, 2, 3, 1};
    int existing = 0, fresh = 0, failed = 0;
    for (uint32_t seed = 0; seed < 2500; ++seed) {
        std::mt19937 r(seed * 2246822519u + 0x3632b0u);
        PlaceCase c{};
        c.size = kSizes[r() % 8];
        c.tile = kTiles[r() % 7];
        c.border = kBorders[r() % 5];
        c.columns = int32_t(r() % 10);
        c.rows = int32_t(r() % 10);
        if (r() % 8 == 0) c.columns = int32_t(r() % 40), c.rows = int32_t(r() % 40);
        c.atlases = r() % 4;
        for (auto &a : c.taken)
            for (uint32_t &t : a)
                t = r() % 3 ? ((8 + r() % 64) << 16) | (8 + r() % 64) : 0;
        Log logs[2];
        long long blocks[2] = {};
        for (int side = 0; side < 2; ++side) {
            BeginLog();
            difftest::AllocationCounter counter;
            uint8_t *o = BuildPlaceManager(c);
            alignas(4) uint8_t rec[0x50];
            for (uint32_t k = 0; k < 0x50; ++k) rec[k] = uint8_t(0x90 + k);
            std::memcpy(rec + 0x30, &c.columns, 4);
            gRecord = rec;
            gRows = c.rows;
            const size_t before = reinterpret_cast<Atlases *>(o + 0x154)->size();
            (side ? placeU : placeO)(o, rec);
            {
                difftest::NotCounted bookkeeping;
                L(0xe0d);
                for (uint32_t k = 0; k < 0x50; k += 4) {
                    uint32_t w;
                    std::memcpy(&w, rec + k, 4);
                    L(w);
                }
                DumpAtlases(o);
                for (uint32_t k : {0xa4u, 0x14cu, 0x150u}) {
                    uint32_t w;
                    std::memcpy(&w, o + k, 4);
                    L(w);
                }
                logs[side] = gLog;
            }
            const size_t after = reinterpret_cast<Atlases *>(o + 0x154)->size();
            int32_t texture;
            std::memcpy(&texture, rec + 0x38, 4);
            FreePlaceManager(o);
            blocks[side] = counter.Blocks();
            if (side == 0) {
                if (texture == -1) ++failed;
                else (after > before ? fresh : existing)++;
            }
        }
        if (!Compare(ctx, "place", seed, logs) && ctx.failures.size() >= 6) return;
        if (blocks[0] != blocks[1]) ctx.Fail("seed %u: heap blocks left %lld vs %lld", seed, blocks[0], blocks[1]);
    }
    if (existing < 300 || fresh < 300 || failed < 100)
        ctx.Fail("coverage: existing atlas %d, new atlas %d, no room %d", existing, fresh, failed);
}
