// Differential tests for the foundations helpers in src/rz/FoundationsHelpers.h (lane WG):
//   0x99e700 ServiceRefHolder::Destroy, 0xc260bc ServiceRefHolder::AcquireSettingsDelegate,
//   0x141533 KeyRefPair::Construct, 0xc9bff9 nGZLog::FindLogTableEntry, 0xca4ac0 FindOptionEntry.
//
// The held objects and the framework service are fakes whose slots log their calls (object, arguments), so the call
// sequences are compared along with results and memory. Destroy's handler is checked with a Release that raises an SEH
// exception and one that throws a C++ exception: the original's catch (...) is /EHa, so neither escapes. The two finds
// run on containers the test builds with our types (LevelMap, std::list<OptionEntry>, the game's cRZString vtable, so
// the original's text slots run on both sides), queried for present and absent keys over random contents and ranges.
#include "difftest.h"
#include "rz/FoundationsHelpers.h"

#include <windows.h>

#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kServiceGlobal = 0x10c9378; // the framework service object GetServiceObject (0x1427b5) returns

struct Call {
    int slot;
    const void *self;
    uint32_t a, b;
    const void *out;
};
std::vector<Call> gCalls;
void *gQueryResult = nullptr; // what the fake's slot 5 stores

uint32_t __fastcall FakeAddRef(void *self, void *)
{
    gCalls.push_back({1, self, 0, 0, nullptr});
    return 7;
}
uint32_t __fastcall FakeRelease(void *self, void *)
{
    gCalls.push_back({2, self, 0, 0, nullptr});
    return 3;
}
uint32_t __fastcall FakeQuery(void *self, void *, uint32_t a, uint32_t b, void **out)
{
    gCalls.push_back({5, self, a, b, out});
    *out = gQueryResult;
    return 1;
}

void *gFakeVTable[8] = {nullptr, reinterpret_cast<void *>(&FakeAddRef), reinterpret_cast<void *>(&FakeRelease),
                        nullptr, nullptr, reinterpret_cast<void *>(&FakeQuery), nullptr, nullptr};
struct FakeObject {
    void *vtable = gFakeVTable;
    uint32_t pad = 0;
};
FakeObject gObjectA, gObjectB, gService;

bool SameCalls(const std::vector<Call> &x, const std::vector<Call> &y)
{
    if (x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); ++i)
        if (x[i].slot != y[i].slot || x[i].self != y[i].self || x[i].a != y[i].a || x[i].b != y[i].b || x[i].out != y[i].out)
            return false;
    return true;
}

// Restores the service global when a test ends (the next test, in any order, sees the image's value).
struct ServiceGlobal {
    difftest::Context &ctx;
    uint32_t saved;
    explicit ServiceGlobal(difftest::Context &c) : ctx(c), saved(*c.image.At<uint32_t>(kServiceGlobal)) {}
    ~ServiceGlobal() { *ctx.image.At<uint32_t>(kServiceGlobal) = saved; }
    void Set(void *service) { *ctx.image.At<void *>(kServiceGlobal) = service; }
};

// A cRZString on the test's stack, built and destroyed by its own members (game vtable).
struct Text {
    union {
        cRZString s;
    };
    explicit Text(const char *t) { s.Construct(t); }
    ~Text() { s.Destruct(); }
};

} // namespace

DIFF_TEST(FoundationsHelpers_Destroy, 0x99e700)
{
    auto orig = ctx.Original<void(__thiscall *)(ServiceRefHolder *)>(0x99e700);
    for (void *held : {static_cast<void *>(nullptr), static_cast<void *>(&gObjectA), static_cast<void *>(&gObjectB)}) {
        ServiceRefHolder a{held}, b{held};
        gCalls.clear();
        orig(&a);
        std::vector<Call> callsA = gCalls;
        gCalls.clear();
        b.Destroy();
        ++ctx.cases;
        if (a.mObject != b.mObject || !SameCalls(callsA, gCalls))
            ctx.Fail("Destroy %p: original left %p (%zu calls), ours %p (%zu calls)", held, a.mObject, callsA.size(),
                     b.mObject, gCalls.size());
    }
}

namespace {
uint32_t __fastcall RaisingRelease(void *, void *)
{
    RaiseException(0xE0001234, 0, 0, nullptr);
    return 0;
}
uint32_t __fastcall ThrowingRelease(void *, void *)
{
    throw 42;
}
uint32_t __fastcall FaultingRelease(void *, void *)
{
    return *reinterpret_cast<volatile uint32_t *>(uintptr_t{8}); // an access violation
}

// Runs fn(holder) and returns the code of an SEH exception that escaped it (a C++ one shows as 0xE06D7363), else 0.
// Free of C++ objects, as __try requires.
using DestroyFn = void (*)(ServiceRefHolder *);
unsigned long EscapedCode(DestroyFn fn, ServiceRefHolder *holder)
{
    __try {
        fn(holder);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}
void *gOrigDestroy;
void CallOriginalDestroy(ServiceRefHolder *h) { reinterpret_cast<void(__thiscall *)(ServiceRefHolder *)>(gOrigDestroy)(h); }
void CallOurDestroy(ServiceRefHolder *h) { h->Destroy(); }
} // namespace

DIFF_TEST(FoundationsHelpers_DestroyThrows, 0x99e700)
{
    gOrigDestroy = ctx.image.At(0x99e700);
    const struct {
        void *fn;
        const char *what;
    } kinds[] = {{reinterpret_cast<void *>(&RaisingRelease), "raising SEH"},
                 {reinterpret_cast<void *>(&ThrowingRelease), "throwing C++"},
                 {reinterpret_cast<void *>(&FaultingRelease), "faulting"}};
    for (const auto &kind : kinds) {
        void *vtable[3] = {nullptr, nullptr, kind.fn};
        void *object = vtable;
        ServiceRefHolder a{&object}, b{&object};
        unsigned long ea = EscapedCode(&CallOriginalDestroy, &a), eb = EscapedCode(&CallOurDestroy, &b);
        ++ctx.cases;
        if (ea != eb || a.mObject != b.mObject)
            ctx.Fail("Destroy, Release %s: escaped 0x%08lx from the original, 0x%08lx from ours", kind.what, ea, eb);
    }
}

DIFF_TEST(FoundationsHelpers_AcquireSettingsDelegate, 0xc260bc)
{
    ServiceGlobal global(ctx);
    auto orig = ctx.Original<ServiceRefHolder *(__thiscall *)(ServiceRefHolder *)>(0xc260bc);
    for (void *service : {static_cast<void *>(nullptr), static_cast<void *>(&gService)})
        for (void *result : {static_cast<void *>(nullptr), static_cast<void *>(&gObjectA)})
            for (void *before : {static_cast<void *>(nullptr), static_cast<void *>(&gObjectB)}) {
                global.Set(service);
                gQueryResult = result;
                ServiceRefHolder a{before}, b{before};
                gCalls.clear();
                ServiceRefHolder *ra = orig(&a);
                std::vector<Call> callsA = gCalls;
                // The fake records the output pointer; compare it relative to each side's holder.
                for (Call &c : callsA)
                    if (c.out == &a) c.out = &b;
                gCalls.clear();
                ServiceRefHolder *rb = b.AcquireSettingsDelegate();
                ++ctx.cases;
                if ((ra == &a) != (rb == &b) || a.mObject != b.mObject || !SameCalls(callsA, gCalls))
                    ctx.Fail("Acquire service=%p result=%p before=%p: original %p (%zu calls), ours %p (%zu calls)",
                             service, result, before, a.mObject, callsA.size(), b.mObject, gCalls.size());
            }
}

DIFF_TEST(FoundationsHelpers_KeyRefPairConstruct, 0x141533)
{
    auto orig = ctx.Original<KeyRefPair *(__thiscall *)(void *, const uint32_t *, void *const *)>(0x141533);
    std::mt19937 rng(0x141533);
    for (int i = 0; i < 64; ++i) {
        const uint32_t key[2] = {rng(), rng()};
        void *const ref = (i % 3 == 0) ? nullptr : (i % 3 == 1 ? &gObjectA : &gObjectB);
        // 0x14 bytes of pattern; the pair goes at +4, so stray writes around it show.
        uint32_t a[5], b[5];
        for (int w = 0; w < 5; ++w) a[w] = b[w] = 0xcdcd0000u + w;
        gCalls.clear();
        KeyRefPair *ra = orig(&a[1], key, &ref);
        std::vector<Call> callsA = gCalls;
        gCalls.clear();
        KeyRefPair *rb = reinterpret_cast<KeyRefPair *>(&b[1])->Construct(key, &ref);
        ++ctx.cases;
        if (std::memcmp(a, b, sizeof(a)) != 0 || reinterpret_cast<void *>(ra) != &a[1] ||
            reinterpret_cast<void *>(rb) != &b[1] || !SameCalls(callsA, gCalls))
            ctx.Fail("KeyRefPair %08x %08x ref %p: original %08x %08x %08x (%zu calls), ours %08x %08x %08x (%zu calls)",
                     key[0], key[1], ref, a[1], a[2], a[3], callsA.size(), b[1], b[2], b[3], gCalls.size());
    }
}

DIFF_TEST(FoundationsHelpers_FindLogTableEntry, 0xc9bff9)
{
    using namespace nGZLog;
    auto orig = ctx.Original<LevelMap::iterator *(__thiscall *)(LevelMap *, LevelMap::iterator *, const LogString &)>(0xc9bff9);
    std::mt19937 rng(0xc9bff9);
    const char *const kNames[] = {"", "a", "A", "b", "ab", "ba", "Render", "render", "Sound", "Sim", "Lot", "UI",
                                  "a longer group name past the small-string buffer"};
    constexpr int kNameCount = int(sizeof(kNames) / sizeof(kNames[0]));
    for (int round = 0; round < 40; ++round) {
        LevelMap table;
        const int count = round == 0 ? 0 : int(rng() % kNameCount);
        for (int i = 0; i < count; ++i) table[LogString(kNames[rng() % kNameCount])] = rng() % 8;
        for (int q = 0; q < kNameCount; ++q) {
            LogString key(kNames[q]);
            LevelMap::iterator a{}, b{};
            LevelMap::iterator *ra = orig(&table, &a, key);
            LevelMap::iterator *rb = FindLogTableEntry(&table, nullptr, &b, key);
            ++ctx.cases;
            uint32_t wa, wb;
            std::memcpy(&wa, &a, 4);
            std::memcpy(&wb, &b, 4);
            if (wa != wb || ra != &a || rb != &b)
                ctx.Fail("find '%s' in %u entries: original node %08x, ours %08x", kNames[q], unsigned(table.size()), wa, wb);
        }
    }
}

DIFF_TEST(FoundationsHelpers_FindOptionEntry, 0xca4ac0)
{
    auto orig = ctx.Original<OptionEntryNode **(__cdecl *)(OptionEntryNode **, OptionEntryNode *, OptionEntryNode *,
                                                           const ts::OptionEntry &)>(0xca4ac0);
    std::mt19937 rng(0xca4ac0);
    const char *const kTexts[] = {"", "a", "A", "b", "ab", "aB", "a much longer option text beyond the buffer"};
    constexpr int kTextCount = int(sizeof(kTexts) / sizeof(kTexts[0]));
    for (int round = 0; round < 60; ++round) {
        std::list<ts::OptionEntry> entries;
        const int count = int(rng() % 7);
        for (int i = 0; i < count; ++i) {
            Text s(kTexts[rng() % kTextCount]);
            entries.emplace_back(s.s, rng() % 3);
        }
        std::vector<OptionEntryNode *> nodes;
        OptionEntryNode *const head = reinterpret_cast<OptionEntryNode *>(entries._Unchecked_end()._Ptr);
        for (OptionEntryNode *n = head->_Next; n != head; n = n->_Next) nodes.push_back(n);
        nodes.push_back(head);
        for (int q = 0; q < 6; ++q) {
            const size_t from = rng() % nodes.size();
            const size_t to = from + rng() % (nodes.size() - from); // last at or after first
            Text s(kTexts[rng() % kTextCount]);
            ts::OptionEntry value(s.s, rng() % 3);
            OptionEntryNode *a = nullptr, *b = nullptr;
            OptionEntryNode **ra = orig(&a, nodes[from], nodes[to], value);
            OptionEntryNode **rb = FindOptionEntry(&b, nodes[from], nodes[to], value);
            ++ctx.cases;
            if (a != b || ra != &a || rb != &b)
                ctx.Fail("find '%s'/%u in [%zu, %zu) of %d: original %p, ours %p", value.mText.mString.c_str(), value.mMask,
                         from, to, count, a, b);
        }
    }
}
