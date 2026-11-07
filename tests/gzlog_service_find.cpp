// Differential tests for the service's lookups by name and the cIService slots built on them
// (src/rz/nGZLog.cpp): FindOutput / FindFilter / FindFormatter (0xc9d4bc / 0xc9d2bc / 0xc9d2ef), the
// counted-reference vector erase (0xc9f33e) and slots 7, 12, 13 and 15 to 19 of the cIService vtable. The
// output back ends, filters and formatters are fakes with recording vtables (each slot logs the object's id
// and its arguments), the lock at +0x90 is a recording fake too; the original and ours get identical
// service blocks and the logs, results and list contents are compared.
#include "difftest.h"
#include "game/vtable.h"
#include "gzlog_support.h"
#include "rz/nGZLog.h"

#include <windows.h>

#include <cstdarg>
#include <deque>
#include <memory>

using namespace gzlog_test;
using namespace nGZLog;

namespace {

constexpr uint32_t kFindOutput = 0xc9d4bc, kFindFilter = 0xc9d2bc, kFindFormatter = 0xc9d2ef, kErase = 0xc9f33e;
constexpr uint32_t kSetFormat = 0xc9e3ea, kSlot12 = 0xc9e5fb, kSlot16 = 0xc9e669, kSlot14 = 0xc9e7f2;
constexpr uint32_t kSetFilter = 0xc9e62b, kSetFormatter = 0xc9e699, kQueryOutput = 0xc9d3f5, kRemoveOutput = 0xc9e101;

std::vector<std::string> gLog;

void Log(const char *fmt, ...)
{
    char buf[100];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    gLog.push_back(buf);
}

std::string Joined()
{
    std::string s;
    for (const std::string &l : gLog) s += l + "; ";
    return s;
}

struct Fake {
    const void *vtable;
    int id;
    std::string name;
};

Fake *Self(void *p) { return static_cast<Fake *>(p); }

// ---- output back end fake -------------------------------------------------------------------------
uint8_t gQiAnswer = 0;
uint8_t __fastcall OutQi(void *self, void *, uint32_t riid, void **ppv)
{
    Log("out%d.qi(%x,%p)", Self(self)->id, riid, static_cast<void *>(ppv));
    return gQiAnswer;
}
uint32_t __fastcall OutAddRef(void *self, void *) { Log("out%d.addref", Self(self)->id); return 1; }
uint32_t __fastcall OutRelease(void *self, void *) { Log("out%d.release", Self(self)->id); return 1; }
void __fastcall OutSetFormatter(void *self, void *, void *arg) { Log("out%d.setFormatter(%d)", Self(self)->id, arg ? Self(arg)->id : -1); }
void __fastcall OutSetFilter(void *self, void *, void *arg) { Log("out%d.setFilter(%d)", Self(self)->id, arg ? Self(arg)->id : -1); }
const char *__fastcall OutText(void *self, void *) { Log("out%d.text", Self(self)->id); return Self(self)->name.c_str(); }
void __fastcall OutSlot12(void *self, void *, uint32_t arg) { Log("out%d.s12(%u)", Self(self)->id, arg); }
void __fastcall OutSlot14(void *self, void *, uint32_t arg) { Log("out%d.s14(%u)", Self(self)->id, arg); }
void __fastcall OutSlot16(void *self, void *, uint32_t arg) { Log("out%d.s16(%u)", Self(self)->id, arg); }

// ---- filter / formatter fakes -----------------------------------------------------------------------
uint32_t __fastcall ObjAddRef(void *self, void *) { Log("obj%d.addref", Self(self)->id); return 1; }
uint32_t __fastcall ObjRelease(void *self, void *) { Log("obj%d.release", Self(self)->id); return 1; }
const char *__fastcall ObjName(void *self, void *) { Log("obj%d.name", Self(self)->id); return Self(self)->name.c_str(); }
void __fastcall ObjSetFormat(void *self, void *, uint32_t index, const char *name) { Log("obj%d.setFormat(%u,%s)", Self(self)->id, index, name ? name : "null"); }

// ---- lock fake ------------------------------------------------------------------------------------------
uint32_t __fastcall LockFn(void *, void *) { Log("lock"); return 1; }
uint32_t __fastcall UnlockFn(void *, void *) { Log("unlock"); return 1; }

void *gOutVTable[24], *gObjVTable[12], *gLockVTable[4];

struct VTableSetup {
    VTableSetup()
    {
        gOutVTable[0] = reinterpret_cast<void *>(&OutQi);
        gOutVTable[1] = reinterpret_cast<void *>(&OutAddRef);
        gOutVTable[2] = reinterpret_cast<void *>(&OutRelease);
        gOutVTable[7] = reinterpret_cast<void *>(&OutSetFormatter);
        gOutVTable[9] = reinterpret_cast<void *>(&OutSetFilter);
        gOutVTable[11] = reinterpret_cast<void *>(&OutText);
        gOutVTable[12] = reinterpret_cast<void *>(&OutSlot12);
        gOutVTable[14] = reinterpret_cast<void *>(&OutSlot14);
        gOutVTable[16] = reinterpret_cast<void *>(&OutSlot16);
        gObjVTable[1] = reinterpret_cast<void *>(&ObjAddRef);
        gObjVTable[2] = reinterpret_cast<void *>(&ObjRelease);
        gObjVTable[3] = reinterpret_cast<void *>(&ObjSetFormat);
        gObjVTable[7] = reinterpret_cast<void *>(&ObjName);
        gLockVTable[1] = reinterpret_cast<void *>(&LockFn);
        gLockVTable[2] = reinterpret_cast<void *>(&UnlockFn);
    }
} gVTableSetup;

const char *const kNames[] = {"Console", "console", "FILE", "File", "debug", "Debug", "net", "Net2", "x", "", "Default", "DEFAULT"};

// A list of fakes with names drawn from a small pool so case variants and duplicates occur.
struct Lists {
    std::deque<Fake> fakes;
    std::vector<void *> outputs, filters, formatters;
    Lists(std::mt19937 &rng)
    {
        int id = 1;
        auto fill = [&](std::vector<void *> &list, const void *vtable) {
            int n = rng() % 7;
            for (int k = 0; k < n; ++k) {
                fakes.push_back({vtable, id++, kNames[rng() % 12]});
                list.push_back(&fakes.back());
            }
        };
        fill(outputs, gOutVTable);
        fill(filters, gObjVTable);
        fill(formatters, gObjVTable);
    }
};

// A pair of service blocks with the lists installed (in the same fake objects) and the lock faked.
struct Fixture {
    Pair pair;
    Lists &lists;
    Fixture(std::mt19937 &rng, Lists &l, int extra) : pair(cLogService::kSize, rng), lists(l)
    {
        for (uint8_t *obj : {pair.a, pair.b}) {
            *reinterpret_cast<void **>(obj + offsetof(cLogService, mLock90)) = gLockVTable;
            auto *s = reinterpret_cast<cLogService *>(obj);
            new (&s->mOutputs) std::vector<RefHolder>();
            new (&s->mFilters) std::vector<RefHolder>();
            new (&s->mFormatters) std::vector<RefHolder>();
            for (void *o : lists.outputs) s->mOutputs.push_back(reinterpret_cast<const RefHolder &>(o));
            for (void *o : lists.filters) s->mFilters.push_back(reinterpret_cast<const RefHolder &>(o));
            for (void *o : lists.formatters) s->mFormatters.push_back(reinterpret_cast<const RefHolder &>(o));
            s->mOutputs.reserve(s->mOutputs.size() + extra);
        }
    }
    cLogService *A() { return reinterpret_cast<cLogService *>(pair.a); }
    cLogService *B() { return reinterpret_cast<cLogService *>(pair.b); }
    ~Fixture()
    {
        gLog.clear();
        for (cLogService *s : {A(), B()}) {
            s->mOutputs.~vector();
            s->mFilters.~vector();
            s->mFormatters.~vector();
        }
    }
};

int IdOf(void *p) { return p ? Self(p)->id : -1; }

std::string Vec(const std::vector<RefHolder> &v)
{
    std::string s = "[";
    for (const RefHolder &r : v) s += std::to_string(IdOf(r.mObject)) + ",";
    return s + "] cap" + std::to_string(v.capacity());
}

std::string Name(std::mt19937 &rng, std::string &store)
{
    // Mostly names that exist (any case), sometimes one that doesn't.
    store = rng() % 6 == 0 ? "zzz-missing" : kNames[rng() % 12];
    if (rng() % 3 == 0)
        for (char &c : store) c = static_cast<char>(rng() % 2 ? toupper(static_cast<unsigned char>(c)) : tolower(static_cast<unsigned char>(c)));
    return store;
}

template <typename OrigFn, typename OursFn> void RunFind(difftest::Context &ctx, uint32_t rva, OursFn ours, int listKind, const char *what)
{
    auto original = ctx.Original<OrigFn>(rva);
    std::mt19937 rng(rva);
    for (int i = 0; i < 300; ++i) {
        Lists lists(rng);
        Fixture f(rng, lists, 0);
        std::string store;
        std::string name = Name(rng, store);
        gLog.clear();
        void *ra = original(f.pair.a, name.c_str());
        std::string logA = Joined();
        gLog.clear();
        void *rb = ours(reinterpret_cast<cLogService *>(f.pair.b), nullptr, name.c_str());
        std::string logB = Joined();
        ++ctx.cases;
        if (ra != rb) ctx.Fail("%s \"%s\": found %d vs %d", what, name.c_str(), IdOf(ra), IdOf(rb));
        if (logA != logB) ctx.Fail("%s \"%s\": calls differ: %.150s / %.150s", what, name.c_str(), logA.c_str(), logB.c_str());
        if (FirstDifference(f.pair.a, f.pair.b, cLogService::kSize, {{0x60, 36}}) >= 0) ctx.Fail("%s: service bytes changed", what);
        (void)listKind;
    }
}

} // namespace

DIFF_TEST(GZLog_FindOutput, kFindOutput)
{
    RunFind<void *(__thiscall *)(void *, const char *)>(ctx, kFindOutput, &FindOutput, 0, "FindOutput");
}

DIFF_TEST(GZLog_FindFilter, kFindFilter)
{
    RunFind<void *(__thiscall *)(void *, const char *)>(ctx, kFindFilter, &FindFilter, 1, "FindFilter");
}

DIFF_TEST(GZLog_FindFormatter, kFindFormatter)
{
    RunFind<void *(__thiscall *)(void *, const char *)>(ctx, kFindFormatter, &FindFormatter, 2, "FindFormatter");
}

DIFF_TEST(GZLog_EraseRef, kErase)
{
    auto original = ctx.Original<void(__thiscall *)(void *, RefHolder **, RefHolder *)>(kErase);
    std::mt19937 rng(kErase);
    for (int i = 0; i < 300; ++i) {
        Lists lists(rng);
        if (lists.outputs.empty()) continue;
        Fixture f(rng, lists, rng() % 3);
        size_t at = rng() % lists.outputs.size();
        RefHolder *outA = nullptr, *outB = nullptr;
        gLog.clear();
        original(&f.A()->mOutputs, &outA, f.A()->mOutputs.data() + at);
        std::string logA = Joined();
        gLog.clear();
        EraseRef(&f.B()->mOutputs, nullptr, &outB, f.B()->mOutputs.data() + at);
        std::string logB = Joined();
        ++ctx.cases;
        if (logA != logB) ctx.Fail("erase at %zu of %zu: calls differ: %.150s / %.150s", at, lists.outputs.size(), logA.c_str(), logB.c_str());
        if (Vec(f.A()->mOutputs) != Vec(f.B()->mOutputs)) ctx.Fail("erase at %zu: lists differ %s / %s", at, Vec(f.A()->mOutputs).c_str(), Vec(f.B()->mOutputs).c_str());
        if (outA != f.A()->mOutputs.data() + at || outB != f.B()->mOutputs.data() + at) ctx.Fail("erase at %zu: iterator out wrong", at);
        if (f.A()->mOutputs.size() + 1 != lists.outputs.size()) ctx.Fail("erase at %zu: size %zu", at, f.A()->mOutputs.size());
    }
}

namespace {

template <typename OrigFn, typename Run> void RunSlotTest(difftest::Context &ctx, uint32_t rva, const char *what, bool locked, Run run)
{
    auto original = ctx.Original<OrigFn>(rva);
    std::mt19937 rng(rva);
    for (int i = 0; i < 300; ++i) {
        Lists lists(rng);
        Fixture f(rng, lists, rng() % 3);
        gQiAnswer = static_cast<uint8_t>(rng() % 4 == 0 ? 0 : 1 + rng() % 255);
        std::string store, store2;
        std::string name = Name(rng, store), other = Name(rng, store2);
        uint32_t arg = rng();
        void *ppv = reinterpret_cast<void *>(static_cast<uintptr_t>(rng()));
        gLog.clear();
        uint32_t ra = run(true, original, f.pair.a, name.c_str(), other.c_str(), arg, &ppv);
        std::string logA = Joined();
        std::string listsA = Vec(f.A()->mOutputs);
        gLog.clear();
        uint32_t rb = run(false, original, f.pair.b, name.c_str(), other.c_str(), arg, &ppv);
        std::string logB = Joined();
        ++ctx.cases;
        if (ra != rb) ctx.Fail("%s \"%s\": result %x vs %x", what, name.c_str(), ra, rb);
        if (logA != logB) ctx.Fail("%s \"%s\": calls differ: %.170s / %.170s", what, name.c_str(), logA.c_str(), logB.c_str());
        if (listsA != Vec(f.B()->mOutputs)) ctx.Fail("%s \"%s\": output lists differ: %s / %s", what, name.c_str(), listsA.c_str(), Vec(f.B()->mOutputs).c_str());
        if (locked && (logA.find("lock") == std::string::npos)) ctx.Fail("%s: lock never taken", what);
        if (FirstDifference(f.pair.a, f.pair.b, cLogService::kSize, {{0x60, 36}}) >= 0) ctx.Fail("%s: service bytes changed", what);
    }
}

constexpr size_t kSub = cLogService::kServiceSubobjectOffset;

} // namespace

DIFF_TEST(GZLog_ServiceSetFormatterFormat, kSetFormat)
{
    using Fn = void(__thiscall *)(void *, const char *, uint32_t, const char *);
    RunSlotTest<Fn>(ctx, kSetFormat, "SetFormatterFormat", false, [](bool orig, Fn original, uint8_t *obj, const char *name, const char *other, uint32_t arg, void **) -> uint32_t {
        if (orig) original(obj + kSub, name, arg, other);
        else ServiceSetFormatterFormat(obj + kSub, nullptr, name, arg, other);
        return 0;
    });
}

DIFF_TEST(GZLog_ServiceOutputSlot12, kSlot12)
{
    using Fn = void(__thiscall *)(void *, const char *, uint32_t);
    RunSlotTest<Fn>(ctx, kSlot12, "OutputSlot12", false, [](bool orig, Fn original, uint8_t *obj, const char *name, const char *, uint32_t arg, void **) -> uint32_t {
        if (orig) original(obj + kSub, name, arg);
        else ServiceOutputSlot12(obj + kSub, nullptr, name, arg);
        return 0;
    });
}

DIFF_TEST(GZLog_ServiceOutputSlot16, kSlot16)
{
    using Fn = void(__thiscall *)(void *, const char *, uint32_t);
    RunSlotTest<Fn>(ctx, kSlot16, "OutputSlot16", false, [](bool orig, Fn original, uint8_t *obj, const char *name, const char *, uint32_t arg, void **) -> uint32_t {
        if (orig) original(obj + kSub, name, arg);
        else ServiceOutputSlot16(obj + kSub, nullptr, name, arg);
        return 0;
    });
}

DIFF_TEST(GZLog_ServiceOutputSlot14, kSlot14)
{
    using Fn = void(__thiscall *)(void *, const char *, uint32_t);
    RunSlotTest<Fn>(ctx, kSlot14, "OutputSlot14", false, [](bool orig, Fn original, uint8_t *obj, const char *name, const char *, uint32_t arg, void **) -> uint32_t {
        if (orig) original(obj + kSub, name, arg);
        else ServiceOutputSlot14(obj + kSub, nullptr, name, arg);
        return 0;
    });
}

DIFF_TEST(GZLog_ServiceSetOutputFilter, kSetFilter)
{
    using Fn = void(__thiscall *)(void *, const char *, const char *);
    RunSlotTest<Fn>(ctx, kSetFilter, "SetOutputFilter", false, [](bool orig, Fn original, uint8_t *obj, const char *name, const char *other, uint32_t, void **) -> uint32_t {
        if (orig) original(obj + kSub, name, other);
        else ServiceSetOutputFilter(obj + kSub, nullptr, name, other);
        return 0;
    });
}

DIFF_TEST(GZLog_ServiceSetOutputFormatter, kSetFormatter)
{
    using Fn = void(__thiscall *)(void *, const char *, const char *);
    RunSlotTest<Fn>(ctx, kSetFormatter, "SetOutputFormatter", false, [](bool orig, Fn original, uint8_t *obj, const char *name, const char *other, uint32_t, void **) -> uint32_t {
        if (orig) original(obj + kSub, name, other);
        else ServiceSetOutputFormatter(obj + kSub, nullptr, name, other);
        return 0;
    });
}

DIFF_TEST(GZLog_ServiceQueryOutput, kQueryOutput)
{
    using Fn = uint8_t(__thiscall *)(void *, const char *, void **);
    RunSlotTest<Fn>(ctx, kQueryOutput, "QueryOutput", true, [](bool orig, Fn original, uint8_t *obj, const char *name, const char *, uint32_t, void **ppv) -> uint32_t {
        // Only the low byte is the result in the original (al).
        if (orig) return original(obj + kSub, name, ppv);
        return ServiceQueryOutput(obj + kSub, nullptr, name, ppv);
    });
}

DIFF_TEST(GZLog_ServiceRemoveOutput, kRemoveOutput)
{
    using Fn = uint8_t(__thiscall *)(void *, const char *);
    RunSlotTest<Fn>(ctx, kRemoveOutput, "RemoveOutput", true, [](bool orig, Fn original, uint8_t *obj, const char *name, const char *, uint32_t, void **) -> uint32_t {
        if (orig) return original(obj + kSub, name);
        return ServiceRemoveOutput(obj + kSub, nullptr, name) ? 1 : 0;
    });
}
