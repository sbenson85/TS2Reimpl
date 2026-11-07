// Differential tests for the lane IC members that don't byte-match (src/misc/ClassesRound3.h): the roof extents
// (tRoofObject, the four getters and both vertex counts through them) and three inventory token filters (register
// choice and vtable-call form only).
#include "difftest.h"
#include "misc/ClassesRound3.h"

#include <cstring>
#include <random>
#include <vector>

namespace {

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

float RandomCoordinate(std::mt19937 &rng)
{
    switch (rng() % 8) {
    case 0: return static_cast<float>(rng() % 64) * 0.5f;             // on the half-tile grid
    case 1: return -static_cast<float>(rng() % 64) * 0.25f;
    case 2: {                                                           // any bit pattern (NaN, infinities, denormals)
        uint32_t u = rng();
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }
    default: return static_cast<float>(static_cast<int32_t>(rng() % 20000) - 10000) / 97.0f;
    }
}

void RandomRoof(std::mt19937 &rng, nTSWorld::tRoofObject &roof)
{
    auto *bytes = reinterpret_cast<uint8_t *>(&roof);
    for (size_t i = 0; i < sizeof roof; ++i) bytes[i] = static_cast<uint8_t>(rng());
    roof.mX0 = RandomCoordinate(rng);
    roof.mX1 = rng() % 6 == 0 ? roof.mX0 : RandomCoordinate(rng);
    roof.mY0 = RandomCoordinate(rng);
    roof.mY1 = rng() % 6 == 0 ? roof.mY0 : RandomCoordinate(rng);
    static const int32_t kTypes[] = {1, 0x12, 2, 0, 3, 0x11, 0x13};
    roof.mType = rng() % 8 == 0 ? static_cast<int32_t>(rng()) : kTypes[rng() % 7];
    // Flags: 0, 1 or any other byte (the original tests each against 0).
    uint8_t *flags[] = {&roof.mHasEaves, &roof.mFlag25, &roof.mNoEavesX, &roof.mNoEavesY};
    for (uint8_t *flag : flags) *flag = rng() % 3 == 0 ? static_cast<uint8_t>(rng()) : static_cast<uint8_t>(rng() % 2);
    roof.mEavesX = rng() % 4 == 0 ? RandomCoordinate(rng) : static_cast<float>(rng() % 8) * 0.25f;
    roof.mEavesY = rng() % 4 == 0 ? RandomCoordinate(rng) : static_cast<float>(rng() % 8) * 0.25f;
}

// ---- token filter fakes: every virtual call is logged (slot, receiver, argument) and answered from a script ----

struct Call {
    int slot;
    const void *self;
    uint32_t arg;
    bool operator==(const Call &o) const { return slot == o.slot && self == o.self && arg == o.arg; }
};
std::vector<Call> gCalls;

uint32_t gFlags = 0, gGuid = 0, gCount = 0;
void *gObject = nullptr; // slot 14's answer
void *gData = nullptr;   // the object's slot 4 answer
uint32_t gField4b = 0, gField4c = 0, gFieldOther = 0;

uint32_t __fastcall TokenGuid(const void *self, void *) { gCalls.push_back({6, self, 0}); return gGuid; }
uint32_t __fastcall TokenFlags(const void *self, void *) { gCalls.push_back({8, self, 0}); return gFlags; }
uint32_t __fastcall TokenCount(const void *self, void *) { gCalls.push_back({15, self, 0}); return gCount; }
void *__fastcall ManagerObject(const void *self, void *, uint32_t guid) { gCalls.push_back({14, self, guid}); return gObject; }
void *__fastcall ObjectData(const void *self, void *) { gCalls.push_back({4, self, 0}); return gData; }
uint32_t __fastcall DataField(const void *self, void *, uint32_t field)
{
    gCalls.push_back({3, self, field});
    return field == 0x4b ? gField4b : field == 0x4c ? gField4c : gFieldOther;
}

void *gTokenVTable[16] = {};
void *gManagerVTable[16] = {};
void *gObjectVTable[16] = {};
void *gDataVTable[16] = {};

void InstallFakes()
{
    gTokenVTable[6] = reinterpret_cast<void *>(&TokenGuid);
    gTokenVTable[8] = reinterpret_cast<void *>(&TokenFlags);
    gTokenVTable[15] = reinterpret_cast<void *>(&TokenCount);
    gManagerVTable[14] = reinterpret_cast<void *>(&ManagerObject);
    gObjectVTable[4] = reinterpret_cast<void *>(&ObjectData);
    gDataVTable[3] = reinterpret_cast<void *>(&DataField);
}

uint32_t RandomField(std::mt19937 &rng)
{
    static const uint32_t kValues[] = {0x7c, 0xaa, 0x25, 0, 1, 0x7d, 0xab, 0x24};
    return rng() % 5 == 0 ? rng() : kValues[rng() % 8];
}

// Runs one filter on both sides for random scripts. allowNullData: the filter checks the data pointer (only Leftover;
// the others call through it unchecked, as the originals do).
template <typename Filter>
void RunFilter(difftest::Context &ctx, uint32_t rva, uint32_t seed, bool allowNullData)
{
    std::mt19937 rng(seed);
    InstallFakes();
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, void *)>(rva);
    const void *manager[2] = {gManagerVTable, nullptr};
    const void *token[2] = {gTokenVTable, nullptr};
    const void *object[2] = {gObjectVTable, nullptr};
    const void *data[2] = {gDataVTable, nullptr};
    for (int i = 0; i < 20000; ++i) {
        // Flag words: the interesting low bits, with garbage above the 16 bits the originals read.
        uint32_t flags = (rng() & 0xfff80000u) | (rng() % 4 == 0 ? rng() & 0xffff : rng() % 8);
        uint32_t count = (rng() & 0xffff0000u) | (rng() % 3 == 0 ? rng() & 0xffff : rng() % 3);
        uint32_t guid = rng();
        void *objectAnswer = rng() % 5 == 0 ? nullptr : object;
        void *dataAnswer = allowNullData && rng() % 5 == 0 ? nullptr : data;
        uint32_t f4b = RandomField(rng), f4c = RandomField(rng), fOther = rng();
        bool nullToken = rng() % 20 == 0;
        Filter filter[2];
        filter[0].mVTable = reinterpret_cast<void *>(static_cast<uintptr_t>(rng()));
        filter[0].mObjectManager = manager;
        filter[1] = filter[0];
        uint32_t results[2];
        std::vector<Call> calls[2];
        for (int side = 0; side < 2; ++side) {
            gFlags = flags;
            gCount = count;
            gGuid = guid;
            gObject = objectAnswer;
            gData = dataAnswer;
            gField4b = f4b;
            gField4c = f4c;
            gFieldOther = fOther;
            gCalls.clear();
            void *t = nullToken ? nullptr : token;
            results[side] = side == 0 ? orig(&filter[0], t) & 0xff : static_cast<uint32_t>(filter[1].Accept(t));
            calls[side] = gCalls;
        }
        ++ctx.cases;
        if (results[0] != results[1] || calls[0] != calls[1] || std::memcmp(&filter[0], &filter[1], sizeof filter[0]) != 0)
            ctx.Fail("flags 0x%x count 0x%x object %p data %p fields 0x%x 0x%x: original %u (%zu calls), ours %u (%zu calls)",
                     flags, count, objectAnswer, dataAnswer, f4b, f4c, results[0], calls[0].size(), results[1],
                     calls[1].size());
    }
}

} // namespace

// Random roofs (corners in either order, equal corners, any float bit pattern, every roof type the getters test plus
// others, flag bytes 0, 1 and other values): the four getters' results compared bit for bit, and both vertex counts
// (which call the getters). The roof must be unchanged.
DIFF_TEST(RoofObject_Extents, 0x3efff0)
{
    std::mt19937 rng(0x49430001);
    auto maxX = ctx.Original<float(__thiscall *)(const void *)>(0x3efff0);
    auto maxY = ctx.Original<float(__thiscall *)(const void *)>(0x3f0060);
    auto minX = ctx.Original<float(__thiscall *)(const void *)>(0x3f00d0);
    auto minY = ctx.Original<float(__thiscall *)(const void *)>(0x3f0140);
    auto countX = ctx.Original<int32_t(__thiscall *)(const void *)>(0x3f3e00);
    auto countY = ctx.Original<int32_t(__thiscall *)(const void *)>(0x3f3e40);
    for (int i = 0; i < 50000; ++i) {
        nTSWorld::tRoofObject roof[2];
        RandomRoof(rng, roof[0]);
        roof[1] = roof[0];
        uint32_t o[6] = {Bits(maxX(&roof[0])), Bits(maxY(&roof[0])), Bits(minX(&roof[0])), Bits(minY(&roof[0])),
                         static_cast<uint32_t>(countX(&roof[0])), static_cast<uint32_t>(countY(&roof[0]))};
        uint32_t u[6] = {Bits(roof[1].GetMaxX()), Bits(roof[1].GetMaxY()), Bits(roof[1].GetMinX()), Bits(roof[1].GetMinY()),
                         static_cast<uint32_t>(roof[1].GetXVertexCount()), static_cast<uint32_t>(roof[1].GetYVertexCount())};
        ctx.cases += 6;
        static const char *const kNames[] = {"GetMaxX 0x3efff0", "GetMaxY 0x3f0060", "GetMinX 0x3f00d0",
                                             "GetMinY 0x3f0140", "GetXVertexCount 0x3f3e00", "GetYVertexCount 0x3f3e40"};
        for (int k = 0; k < 6; ++k)
            if (o[k] != u[k])
                ctx.Fail("%s: type 0x%x flags %u %u %u %u: original 0x%08x, ours 0x%08x", kNames[k], roof[0].mType,
                         roof[0].mHasEaves, roof[0].mFlag25, roof[0].mNoEavesX, roof[0].mNoEavesY, o[k], u[k]);
        if (std::memcmp(&roof[0], &roof[1], sizeof roof[0]) != 0) ctx.Fail("roof changed differently");
    }
}

// Token filters: random flag words (garbage above 16 bits), counts, GUIDs, null objects / data, data fields around the
// values the filters test: the result (al), the sequence of virtual calls (slot, receiver, argument) and the filter.
DIFF_TEST(FridgeTokenFilter_Accept, 0x6fd791) { RunFilter<cTSFridgeTokenFilter>(ctx, 0x6fd791, 0x49430002, false); }
DIFF_TEST(JuicerTokenFilter_Accept, 0x700698) { RunFilter<cTSJuicerTokenFilter>(ctx, 0x700698, 0x49430003, false); }
DIFF_TEST(LeftoverTokenFilter_Accept, 0x700c64) { RunFilter<cTSLeftoverTokenFilter>(ctx, 0x700c64, 0x49430004, true); }

namespace {
uint32_t gInnerAnswer = 0;
uint32_t __fastcall InnerAccept(const void *self, void *, uint32_t file)
{
    gCalls.push_back({10, self, file});
    return gInnerAnswer;
}
} // namespace

// The image-file filter (both anonymous copies share 0x6f877e): null and non-null inner filters, the inner filter's
// slot 10 answering 0, 1, other low bytes, and garbage above al. The result byte, the calls made and the filter.
DIFF_TEST(ImgFileFilter_Accept, 0x6f877e)
{
    std::mt19937 rng(0x49430005);
    auto orig = ctx.Original<uint32_t(__thiscall *)(void *, void *)>(0x6f877e);
    void *innerVTable[16] = {};
    innerVTable[10] = reinterpret_cast<void *>(&InnerAccept);
    const void *inner[2] = {innerVTable, nullptr};
    for (int i = 0; i < 20000; ++i) {
        uint32_t answer = (rng() & 0xffffff00u) | (rng() % 3 == 0 ? rng() & 0xff : rng() % 2);
        uint32_t file = rng();
        cTSImgFileFilter filter[2];
        filter[0].mVTable = reinterpret_cast<void *>(static_cast<uintptr_t>(rng()));
        filter[0].mInner = rng() % 5 == 0 ? nullptr : inner;
        filter[1] = filter[0];
        uint32_t results[2];
        std::vector<Call> calls[2];
        for (int side = 0; side < 2; ++side) {
            gInnerAnswer = answer;
            gCalls.clear();
            void *f = reinterpret_cast<void *>(static_cast<uintptr_t>(file));
            results[side] = side == 0 ? orig(&filter[0], f) & 0xff : static_cast<uint32_t>(filter[1].Accept(f));
            calls[side] = gCalls;
        }
        ++ctx.cases;
        if (results[0] != results[1] || calls[0] != calls[1] || std::memcmp(&filter[0], &filter[1], sizeof filter[0]) != 0)
            ctx.Fail("inner %p answer 0x%x: original %u (%zu calls), ours %u (%zu calls)", filter[0].mInner, answer,
                     results[0], calls[0].size(), results[1], calls[1].size());
    }
}
