// Differential tests for the snap error report (src/simantics/SnapReport.*, lane AJ): the message lookup 0x7f0c00 and the report
// built from the state 0x7f0ca0. The cached singleton C (game global 0x10cc290) and the report's object are fakes with
// recording vtables; the state's first container is a fake tree (a head node whose left link is the first node).
#include "difftest.h"

#include "rz/StaticGetters2.h"
#include "simantics/SnapReport.h"

#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace {

constexpr uint32_t kCacheC = 0x10cc290;

std::vector<uint32_t> gLog;
void L(uint32_t w) { gLog.push_back(w); }
uint32_t gAnswer = 0; // what the lookups return in eax

uint32_t __fastcall ServiceLookup(void *, void *, uint32_t code, void **message)
{
    L(0x100);
    L(code);
    L(message ? 1 : 0);
    return gAnswer;
}
uint32_t __fastcall ObjectLookup194(void *, void *, uint32_t code, void **message)
{
    L(0x200);
    L(code);
    L(message ? 1 : 0);
    return gAnswer;
}
uint32_t __fastcall ObjectLookup195(void *, void *, void **message)
{
    L(0x300);
    L(message ? 1 : 0);
    return gAnswer;
}
uint32_t __fastcall ObjectAddRef(void *, void *)
{
    L(0x400);
    return 0;
}

void *gServiceVt[40];
void *gObjectVt[200];
struct Fake {
    void **vt;
    uint32_t data[4];
};
Fake gService = {gServiceVt, {}};
Fake gObject = {gObjectVt, {}};

void BuildFakes()
{
    gServiceVt[32] = reinterpret_cast<void *>(&ServiceLookup);
    gObjectVt[1] = reinterpret_cast<void *>(&ObjectAddRef);
    gObjectVt[194] = reinterpret_cast<void *>(&ObjectLookup194);
    gObjectVt[195] = reinterpret_cast<void *>(&ObjectLookup195);
}

// A tree node: three links and a flag word, then the value.
struct Node {
    Node *left, *parent, *right;
    uint32_t flags;
    SnapErrorReport value;
};

uint32_t Mix(std::mt19937 &r) { return r() % 3 ? r() : 0xffffffffu; }

} // namespace

DIFF_TEST(SnapReport_LookupMessage, 0x7f0c00)
{
    BuildFakes();
    void **cache = ctx.image.At<void *>(kCacheC);
    void *saved = *cache;
    *cache = &gService;
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, void **)>(0x7f0c00);
    std::mt19937 r(0x7f0c00);
    uint32_t byKind[4] = {}, nullObject = 0;
    for (int i = 0; i < 6000; ++i) {
        SnapErrorReport report;
        std::memset(&report, 0, sizeof(report));
        report.mCode = Mix(r);
        const uint32_t kinds[] = {0, 1, 2, 3, 4, 0xffffffffu, 7};
        report.mKind = kinds[r() % 7];
        report.mObject = r() % 4 == 0 ? nullptr : &gObject;
        gAnswer = r() % 2 ? r() : (r() & ~0xffu);
        void *message = nullptr;
        void **const target = r() % 5 == 0 ? nullptr : &message;
        SnapErrorReport copy = report;

        gLog.clear();
        const uint32_t a = original(&report, target);
        const std::vector<uint32_t> logA = gLog;
        gLog.clear();
        const uint8_t b = copy.LookupMessage(target);
        ++ctx.cases;
        if (report.mKind < 4) ++byKind[report.mKind];
        nullObject += report.mObject == nullptr;
        // Callers read al only; the original's upper bytes follow the callee (or the kind) and are not compared.
        if ((a & 0xff) != b) ctx.Fail("case %d kind %u: original %02x ours %02x", i, report.mKind, a & 0xff, b);
        if (logA != gLog) ctx.Fail("case %d kind %u: the lookup calls differ", i, report.mKind);
        if (std::memcmp(&report, &copy, sizeof(report)) != 0) ctx.Fail("case %d: report changed", i);
    }
    *cache = saved;
    if (!byKind[0] || !byKind[1] || !byKind[2] || !byKind[3] || !nullObject) ctx.Fail("coverage %u %u %u %u null %u", byKind[0], byKind[1], byKind[2], byKind[3], nullObject);
}

DIFF_TEST(SnapReport_BuildReport, 0x7f0ca0)
{
    BuildFakes();
    auto original = ctx.Original<SnapErrorReport *(__thiscall *)(void *, SnapErrorReport *)>(0x7f0ca0);
    std::mt19937 r(0x7f0ca0);
    uint32_t withObject = 0, empty = 0, filled = 0;
    for (int i = 0; i < 6000; ++i) {
        // The head node of the first container: its left link is the first node; the other links point at nodes too.
        Node head{}, first{}, second{};
        head.left = &first;
        head.right = &second;
        head.parent = &first;
        first.left = &head;
        first.parent = &head;
        first.right = &head;
        first.value.mCode = Mix(r);
        for (uint8_t &b : first.value.mDetail) b = static_cast<uint8_t>(r());
        first.value.mObject = r() % 3 == 0 ? nullptr : &gObject;
        first.value.mKind = Mix(r);
        second.value.mCode = Mix(r);
        second.value.mObject = &gObject;

        SnapStateReports state;
        std::memset(&state, 0x5a, sizeof(state));
        state.mReportsHead = &head;
        state.mReportCount = r() % 4 == 0 ? 0 : 1 + r() % 3;
        for (uint32_t &v : state.mOtherFields) v = Mix(r);

        SnapErrorReport outA, outB;
        std::memset(&outA, 0xc3, sizeof(outA));
        std::memset(&outB, 0xc3, sizeof(outB));
        // Fill the out objects with the same random bytes: the empty report leaves the detail bytes alone.
        for (size_t k = 0; k < sizeof(outA); ++k) reinterpret_cast<uint8_t *>(&outA)[k] = reinterpret_cast<uint8_t *>(&outB)[k] = static_cast<uint8_t>(r());

        gLog.clear();
        SnapErrorReport *ra = original(&state, &outA);
        const std::vector<uint32_t> logA = gLog;
        gLog.clear();
        SnapErrorReport *rb = state.BuildReport(&outB);
        ++ctx.cases;
        if (ra != &outA || rb != &outB) ctx.Fail("case %d: return values", i);
        if (std::memcmp(&outA, &outB, sizeof(outA)) != 0) ctx.Fail("case %d: reports differ (count %u)", i, state.mReportCount);
        if (logA != gLog) ctx.Fail("case %d: AddRef calls differ", i);
        if (state.mReportCount == 0) ++empty; else ++filled;
        withObject += state.mReportCount != 0 && first.value.mObject != nullptr;
    }
    if (!empty || !filled || !withObject) ctx.Fail("coverage: empty %u filled %u with object %u", empty, filled, withObject);
}
