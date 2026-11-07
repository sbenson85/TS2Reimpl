// Differential tests for the QueryInterface slots of nGZLog's helper objects and service (src/rz/nGZLog.cpp)
// and the thunks that reach them from the secondary vtables. The objects are blocks of random bytes whose
// vtable words point at recording fakes (AddRef is slot 1 of each vtable), so the object an AddRef goes to
// and the order of calls are compared along with the result, the stored pointer and a canary after it.
#include "difftest.h"
#include "game/vtable.h"
#include "gzlog_support.h"
#include "rz/nGZLog.h"

#include <windows.h>

using namespace gzlog_test;
using namespace nGZLog;

namespace {

constexpr uint32_t kRecordQI = 0xc9e06e, kFormatterQI = 0xc9e035, kFilterQI = 0xc9dff2, kServiceQI = 0xc9e0a7;
constexpr uint32_t kRecordThunk = 0xc9e09f, kFormatterThunk = 0xc9e066, kFilterThunk = 0xc9e02d, kServiceThunk4 = 0xc9e0d2, kServiceThunk1c = 0xc9e0da;

std::vector<uint32_t> gAddRefs; // `this` of each AddRef, relative to the object
const uint8_t *gObject = nullptr;

uint32_t __fastcall FakeAddRef(void *self, void *)
{
    gAddRefs.push_back(static_cast<uint32_t>(static_cast<const uint8_t *>(self) - gObject));
    return 1;
}

void *gFakeVTable[4] = {nullptr, reinterpret_cast<void *>(&FakeAddRef), nullptr, nullptr};

std::string Join(const std::vector<uint32_t> &v)
{
    std::string s;
    for (uint32_t x : v) s += std::to_string(x) + " ";
    return s;
}

using QiFn = uint8_t(__thiscall *)(void *, uint32_t, void **);
using QiOurs = bool(__fastcall *)(void *, void *, uint32_t, void **);

// One object's worth of cases: ids that match, ids that fall through (1, the system service id, random).
void RunQiTests(difftest::Context &ctx, uint32_t rva, QiOurs ours, size_t objectSize, size_t thisOffset, std::initializer_list<uint32_t> special, const char *what)
{
    auto original = ctx.Original<QiFn>(rva);
    std::mt19937 rng(rva);
    std::vector<uint32_t> ids(special);
    ids.insert(ids.end(), {0, 1, 2, 0x287fb697, 0x09b69c46, 0xffffffff});
    for (int i = 0; i < 400; ++i) {
        uint32_t riid = i < static_cast<int>(ids.size() * 8) ? ids[i % ids.size()] : (i % 5 == 0 ? rng() : ids[rng() % ids.size()]);
        // The service stores through ppv without a null check (also in cRZSystemService's QueryInterface), so a null ppv would crash both.
        bool nullPpv = i % 7 == 3 && std::strncmp(what, "service", 7) != 0;
        Pair p(objectSize, rng);
        // Vtable words of the object: the primary one and (for the base at +4) the second.
        for (uint8_t *obj : {p.a, p.b}) {
            *reinterpret_cast<void **>(obj) = gFakeVTable;
            *reinterpret_cast<void **>(obj + 4) = gFakeVTable;
        }
        uint8_t slotA[12], slotB[12];
        for (int k = 0; k < 12; ++k) slotA[k] = slotB[k] = static_cast<uint8_t>(rng());
        void **ppvA = nullPpv ? nullptr : reinterpret_cast<void **>(slotA + 4);
        void **ppvB = nullPpv ? nullptr : reinterpret_cast<void **>(slotB + 4);
        gObject = p.a;
        gAddRefs.clear();
        uint8_t ra = original(p.a + thisOffset, riid, ppvA);
        std::vector<uint32_t> callsA = gAddRefs;
        gObject = p.b;
        gAddRefs.clear();
        bool rb = ours(p.b + thisOffset, nullptr, riid, ppvB);
        ++ctx.cases;
        uint32_t storedA = nullPpv ? 0 : *reinterpret_cast<uint32_t *>(slotA + 4) - static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p.a));
        uint32_t storedB = nullPpv ? 0 : *reinterpret_cast<uint32_t *>(slotB + 4) - static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p.b));
        if (ra != static_cast<uint8_t>(rb)) ctx.Fail("%s riid %08x: result %u/%u", what, riid, ra, rb);
        if (callsA != gAddRefs) ctx.Fail("%s riid %08x: AddRef calls [%s] vs [%s]", what, riid, Join(callsA).c_str(), Join(gAddRefs).c_str());
        if (std::memcmp(slotA, slotB, 4) != 0 || std::memcmp(slotA + 8, slotB + 8, 4) != 0) ctx.Fail("%s riid %08x: bytes around ppv changed", what, riid);
        // The pointer stored is compared relative to its object (the offset of the subobject handed out).
        if (!nullPpv && ra && (storedA != storedB)) ctx.Fail("%s riid %08x: stored offset %x/%x", what, riid, storedA, storedB);
        if (!nullPpv && !ra && std::memcmp(slotA + 4, slotB + 4, 4) != 0) ctx.Fail("%s riid %08x: ppv written on failure", what, riid);
        if (FirstDifference(p.a, p.b, objectSize) >= 0) ctx.Fail("%s riid %08x: object changed", what, riid);
    }
}

} // namespace

DIFF_TEST(GZLog_RecordQI, kRecordQI)
{
    RunQiTests(ctx, kRecordQI, &RecordQueryInterface, cLogRecord::kSize, 0, {0xcca124a1, 0xcca124b6, 0x2ca92615}, "record");
}

DIFF_TEST(GZLog_FormatterQI, kFormatterQI)
{
    RunQiTests(ctx, kFormatterQI, &FormatterQueryInterface, cLogFormatterText::kSize, 0, {0xcca124b6, 0xcca124a1, 0xaca92697}, "formatter");
}

DIFF_TEST(GZLog_FilterQI, kFilterQI)
{
    RunQiTests(ctx, kFilterQI, &FilterQueryInterface, cLogFilterGroupLevels::kSize, 0, {0x2ca92615, 0xaca92697, 0xcca124a1}, "filter");
}

DIFF_TEST(GZLog_ServiceQI, kServiceQI)
{
    RunQiTests(ctx, kServiceQI, &ServiceQueryInterface, cLogService::kSize, 0, {0x09b69c46, 0x287fb697, 0x09b69c47}, "service");
}

DIFF_TEST(GZLog_RecordQIThunk, kRecordThunk)
{
    RunQiTests(ctx, kRecordThunk, &thunks::RecordQueryInterfaceFromUnknown, cLogRecord::kSize, 4, {0xcca124a1}, "record thunk");
}

DIFF_TEST(GZLog_FormatterQIThunk, kFormatterThunk)
{
    RunQiTests(ctx, kFormatterThunk, &thunks::FormatterQueryInterfaceFromUnknown, cLogFormatterText::kSize, 4, {0xcca124b6}, "formatter thunk");
}

DIFF_TEST(GZLog_FilterQIThunk, kFilterThunk)
{
    RunQiTests(ctx, kFilterThunk, &thunks::FilterQueryInterfaceFromUnknown, cLogFilterGroupLevels::kSize, 4, {0x2ca92615, 0xaca92697}, "filter thunk");
}

DIFF_TEST(GZLog_ServiceQIThunk4, kServiceThunk4)
{
    RunQiTests(ctx, kServiceThunk4, &thunks::ServiceQueryInterfaceFromUnknown, cLogService::kSize, 4, {0x09b69c46, 0x287fb697}, "service thunk +4");
}

DIFF_TEST(GZLog_ServiceQIThunk1c, kServiceThunk1c)
{
    RunQiTests(ctx, kServiceThunk1c, &thunks::ServiceQueryInterfaceFromService, cLogService::kSize, 0x1c, {0x09b69c46, 0x287fb697}, "service thunk +1c");
}
