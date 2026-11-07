// Differential tests for the small helpers of the SimAntics operand reader (src/simantics/OperandReader.h): the data
// reference binders 0x53d9c0 and 0x52ccc0, the float rounding 0x53f7a0 and the object-data binder 0x53f7d0, plus a check that
// the game's stack-error exception type (ThrowInfo 0xf803fc) is the type ours throws.
#include "difftest.h"
#include "simantics/OperandReader.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <typeinfo>
#include <vector>

namespace {

using BindFn = void(__thiscall *)(SimDataRef *self, int16_t *ptr, int32_t index, int32_t length);
using MakeFn = SimDataRef *(__thiscall *)(SimDataRef *self, int16_t *ptr, int32_t index, int32_t length);

int16_t gCells[1024];

const int32_t kEdge[] = {0,      1,      2,      3,      4,       5,       7,       8,       9,       10,      0x6b,     0x6c,     0x6d,    0xc8,
                         0xc9,   0xca,   0xe6,   0xe7,   0x7ffe,  0x7fff,  0x8000,  0x8001,  0xfffe,  0xffff,  0x10000,  0x10001,  0x18000, 0x7fffffff,
                         -1,     -2,     -3,     -0x7fff, -0x8000, -0x8001, -0x10000, -0x10001, static_cast<int32_t>(0x80000000), 0x12345678};

bool Same(const SimDataRef &a, const SimDataRef &b)
{
    return std::memcmp(&a, &b, sizeof(SimDataRef)) == 0;
}

} // namespace

DIFF_TEST(OperandRef_Bind, 0x53d9c0)
{
    auto original = ctx.Original<BindFn>(0x53d9c0);
    std::mt19937 r(7);
    auto run = [&](int16_t *ptr, int32_t index, int32_t length) {
        SimDataRef a, b;
        std::memset(&a, 0x5a, sizeof(a));
        std::memset(&b, 0x5a, sizeof(b));
        original(&a, ptr, index, length);
        static_cast<OperandRef *>(&b)->Bind(ptr, index, length);
        ++ctx.cases;
        if (!Same(a, b))
            ctx.Fail("Bind ptr %p index %d length %d: %d/%d/%p vs %d/%d/%p", ptr, index, length, a.mLength, a.mIndex, a.mPtr, b.mLength, b.mIndex, b.mPtr);
    };
    for (int16_t *ptr : {static_cast<int16_t *>(nullptr), &gCells[100]})
        for (int32_t index : kEdge)
            for (int32_t length : kEdge) run(ptr, index, length);
    for (int i = 0; i < 100000; ++i) run((r() % 5 == 0) ? nullptr : &gCells[r() % 512], static_cast<int32_t>(r()), static_cast<int32_t>(r() % 64 - 8));
}

DIFF_TEST(OperandRef_MakeFromBase, 0x52ccc0)
{
    auto original = ctx.Original<MakeFn>(0x52ccc0);
    std::mt19937 r(11);
    auto run = [&](int16_t *ptr, int32_t index, int32_t length) {
        SimDataRef a, b;
        std::memset(&a, 0x5a, sizeof(a));
        std::memset(&b, 0x5a, sizeof(b));
        SimDataRef *ra = original(&a, ptr, index, length);
        SimDataRef *rb = static_cast<OperandRef *>(&b)->MakeFromBase(ptr, index, length);
        ++ctx.cases;
        if (!Same(a, b) || ra != &a || rb != &b)
            ctx.Fail("MakeFromBase ptr %p index %d length %d: %d/%d/%p vs %d/%d/%p", ptr, index, length, a.mLength, a.mIndex, a.mPtr, b.mLength, b.mIndex, b.mPtr);
    };
    for (int16_t *ptr : {static_cast<int16_t *>(nullptr), &gCells[100]})
        for (int32_t index : kEdge)
            for (int32_t length : kEdge) run(ptr, index, length);
    for (int i = 0; i < 100000; ++i) run((r() % 5 == 0) ? nullptr : &gCells[r() % 512], static_cast<int32_t>(r()), static_cast<int32_t>(r() % 64 - 8));
}

DIFF_TEST(OperandReader_RoundHalfAway, 0x53f7a0)
{
    auto original = ctx.Original<int32_t(__cdecl *)(float)>(0x53f7a0);
    std::mt19937 r(13);
    auto run = [&](float f) {
        ++ctx.cases;
        const int32_t a = original(f), b = RoundHalfAway(f);
        if (a != b) {
            uint32_t bits;
            std::memcpy(&bits, &f, 4);
            ctx.Fail("RoundHalfAway(0x%08x = %g): 0x%x vs 0x%x", bits, f, a, b);
        }
    };
    const float special[] = {0.0f,   -0.0f,  0.5f,  -0.5f,  1.5f,   -1.5f,  2.5f,   -2.5f,  0.49999997f, -0.49999997f, 0.25f,
                             100.0f, 99.5f,  -99.5f, 32767.5f, 32768.5f, -32768.5f, 2147483520.0f, 2147483648.0f, -2147483648.0f, -2147483904.0f,
                             1e10f,  -1e10f, 3e38f,  -3e38f, std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::denorm_min()};
    for (float f : special) run(f);
    for (int i = 0; i < 400000; ++i) {
        uint32_t bits = r();
        float f;
        if (i % 3 == 0) {
            f = static_cast<float>(static_cast<int>(r() % 400001) - 200000) / 8.0f; // many exact halves
        } else {
            std::memcpy(&f, &bits, 4);
        }
        run(f);
    }
}

namespace {

// A fake object whose slot 7 (one argument: the id) logs the id and returns the scripted pointer.
int32_t gSeenId;
int gSeenCalls;
void *gSlot7Result;

void *__fastcall Slot7(void *, void *, int32_t id)
{
    gSeenId = id;
    ++gSeenCalls;
    return gSlot7Result;
}

struct Fake7 {
    void **vt;
};

} // namespace

DIFF_TEST(OperandReader_BindObjectData, 0x53f7d0)
{
    auto original = ctx.Original<void(__cdecl *)(void *, int32_t, SimDataRef *)>(0x53f7d0);
    void *vt[16] = {};
    vt[7] = reinterpret_cast<void *>(&Slot7);
    Fake7 fake{vt};
    std::mt19937 r(17);
    auto run = [&](int32_t id, bool hasData) {
        SimDataRef a, b;
        std::memset(&a, 0x5a, sizeof(a));
        std::memset(&b, 0x5a, sizeof(b));
        gSlot7Result = hasData ? static_cast<void *>(&gCells[50]) : nullptr;
        gSeenCalls = 0;
        original(&fake, id, &a);
        const int callsA = gSeenCalls, seenA = gSeenId;
        gSeenCalls = 0;
        BindObjectData(&fake, id, &b);
        ++ctx.cases;
        if (!Same(a, b) || callsA != gSeenCalls || (callsA && seenA != gSeenId))
            ctx.Fail("BindObjectData id %d data %d: %d/%d/%p (slot7 %d x%d) vs %d/%d/%p (slot7 %d x%d)", id, hasData, a.mLength, a.mIndex, a.mPtr, seenA,
                     callsA, b.mLength, b.mIndex, b.mPtr, gSeenId, gSeenCalls);
    };
    // every id of the low 16 bits, with and without data, plain and with random high bits
    for (int32_t id = -0x8000; id <= 0x7fff; ++id) {
        run(id, true);
        run(id, false);
        run(static_cast<int32_t>((r() & 0xffff0000u) | static_cast<uint16_t>(id)), true);
    }
}

DIFF_TEST(OperandReader_StackErrorType, 0x531700)
{
    // The game throws a 4-byte unnamed enum of cTSTreeStack (ThrowInfo 0xf803fc); ours must be that type.
    const char *name = typeid(cTSTreeStack::kStackErrorNoFrame).raw_name();
    ++ctx.cases;
    if (std::strcmp(name, ".?AW4<unnamed-enum-kStackErrorBase>@cTSTreeStack@@") != 0) ctx.Fail("exception type name: %s", name);
    if (sizeof(cTSTreeStack::kStackErrorNoFrame) != 4) ctx.Fail("exception type size");
}
