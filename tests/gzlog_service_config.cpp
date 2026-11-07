// Differential tests for nGZLog::cService's output adding slot (11, 0xc9cc74), the group level setters by wildcard
// (slots 9 and 20, 0xc9e2da and 0xc9e6d7), the quoted text helper (0xc9d07a), the presets writer (0xc9ead7) and
// the configuration text slot (24, 0xc9eded). Each side gets its own world of fakes built from the same seed:
// filters / formatters and output back ends whose vtable slots log their calls (objects by id), a stream that logs
// what it is given, and lock objects that log against the service block. The service's name table and alias table
// are real (our standard library, the same layout as the original's); the lists hold the fakes.
#include "gzlog_support.h" // first: it defines NOMINMAX before windows.h
#include "difftest.h"
#include "game/vtable.h"
#include "rz/nGZLog.h"

#include <cstdarg>
#include <cstdio>
#include <deque>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace gzlog_test;
using namespace nGZLog;

namespace {

constexpr uint32_t kAddOutput = 0xc9cc74, kSetFilterGroupLevel = 0xc9e2da, kSetOutputGroupLevel = 0xc9e6d7;
constexpr uint32_t kQuoteText = 0xc9d07a, kWritePresets = 0xc9ead7, kWriteConfig = 0xc9eded;
constexpr uint32_t kNoGroupPointer = 0x1085d98, kAnyGroupPointer = 0x1085d9c;
constexpr size_t kSub = cLogService::kServiceSubobjectOffset;
// The service bytes that differ by nature: the name table (heap nodes) and the three lists.
const std::vector<std::pair<size_t, size_t>> kMasks = {{0x30, 0x20}, {0x60, 36}, {0x88, 8}, {0xf8, 4}};

std::string gLog;
const uint8_t *gBase = nullptr;

void Log(const char *fmt, ...)
{
    char buf[300];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    gLog += buf;
    gLog += ';';
}

void *gNamedVTable[12], *gOutVTable[24], *gStreamVTable[20], *gLockVTable[4];

struct Obj {
    void *const *vtable;
    int id;
    std::string name;
    Obj *filter = nullptr;    // an output's
    Obj *formatter = nullptr; // an output's
    bool answers = true;      // a named object's answer to the filter interface query
    int refs = 1;
    struct World *world = nullptr;
};

struct World {
    std::deque<Obj> objects;
    int nextId = 1;
    Obj *Make(void *const *vtable, std::string name)
    {
        objects.push_back(Obj{vtable, nextId++, std::move(name)});
        objects.back().world = this;
        return &objects.back();
    }
    std::string Canon() const
    {
        std::string s;
        for (const Obj &o : objects) s += "#" + std::to_string(o.id) + "'" + o.name + "'" + std::to_string(o.refs) + " ";
        return s;
    }
};

std::string Id(const void *p)
{
    if (!p) return "null";
    auto *o = static_cast<const Obj *>(p);
    if (o->vtable == gNamedVTable || o->vtable == gOutVTable) return "#" + std::to_string(o->id);
    return "other";
}

// A named object (filter or formatter).
bool __fastcall NamedQuery(Obj *self, void *, uint32_t riid, void **ppv)
{
    Log("#%d.query(%x)", self->id, riid);
    if (!self->answers) return false;
    *ppv = self;
    ++self->refs;
    return true;
}
uint32_t __fastcall AddRefFn(Obj *self, void *)
{
    Log("#%d.addref", self->id);
    return static_cast<uint32_t>(++self->refs);
}
uint32_t __fastcall ReleaseFn(Obj *self, void *)
{
    Log("#%d.release", self->id);
    return static_cast<uint32_t>(--self->refs);
}
void __fastcall NamedWrite(Obj *self, void *, void *stream) { Log("#%d.write(%s)", self->id, stream ? "stream" : "null"); }
void __fastcall NamedSetName(Obj *self, void *, const char *name)
{
    Log("#%d.setname('%s')", self->id, name);
    self->name = name;
}
const char *__fastcall NamedName(Obj *self, void *)
{
    Log("#%d.name", self->id);
    return self->name.c_str();
}
Obj *__fastcall NamedClone(Obj *self, void *)
{
    Obj *copy = self->world->Make(gNamedVTable, self->name);
    copy->answers = self->answers;
    Log("#%d.clone>#%d", self->id, copy->id);
    return copy;
}
void __fastcall NamedSetGroupLevel(Obj *self, void *, const char *group, int32_t level) { Log("#%d.grouplevel('%s', %d)", self->id, group, level); }

// An output back end.
Obj *__fastcall OutGetFormatter(Obj *self, void *)
{
    Log("#%d.getformatter", self->id);
    return self->formatter;
}
void __fastcall OutSetFormatter(Obj *self, void *, Obj *formatter) { Log("#%d.setformatter(%s)", self->id, Id(formatter).c_str()); }
Obj *__fastcall OutGetFilter(Obj *self, void *)
{
    Log("#%d.getfilter", self->id);
    return self->filter;
}
void __fastcall OutSetFilter(Obj *self, void *, Obj *filter) { Log("#%d.setfilter(%s)", self->id, Id(filter).c_str()); }
void __fastcall OutSetText(Obj *self, void *, const char *text)
{
    Log("#%d.settext('%s')", self->id, text);
    self->name = text;
}
const char *__fastcall OutText(Obj *self, void *)
{
    Log("#%d.text", self->id);
    return self->name.c_str();
}
void __fastcall OutSetEnabled(Obj *self, void *, uint8_t on) { Log("#%d.enable(%u)", self->id, on); }
void __fastcall OutWrite(Obj *self, void *, void *stream) { Log("#%d.writeconfig(%s)", self->id, stream ? "stream" : "null"); }

// The stream: slot 14 text, slot 15 a string object.
void __fastcall StreamText(void *, void *, const char *text) { Log("t'%s'", text); }
bool __fastcall StreamString(void *, void *, const cRZString *text)
{
    Log("s'%s'", text->mString.c_str());
    return true;
}
struct StreamFake {
    void *const *vtable = gStreamVTable;
} gStream;

uint32_t __fastcall LockFn(void *self, void *)
{
    Log("lock(+%x)", static_cast<uint32_t>(static_cast<const uint8_t *>(self) - gBase));
    return 1;
}
uint32_t __fastcall UnlockFn(void *self, void *)
{
    Log("unlock(+%x)", static_cast<uint32_t>(static_cast<const uint8_t *>(self) - gBase));
    return 2;
}

struct Setup {
    Setup()
    {
        gNamedVTable[0] = reinterpret_cast<void *>(&NamedQuery);
        gNamedVTable[1] = reinterpret_cast<void *>(&AddRefFn);
        gNamedVTable[2] = reinterpret_cast<void *>(&ReleaseFn);
        gNamedVTable[5] = reinterpret_cast<void *>(&NamedWrite);
        gNamedVTable[6] = reinterpret_cast<void *>(&NamedSetName);
        gNamedVTable[7] = reinterpret_cast<void *>(&NamedName);
        gNamedVTable[8] = reinterpret_cast<void *>(&NamedClone);
        gNamedVTable[9] = reinterpret_cast<void *>(&NamedSetGroupLevel);
        gOutVTable[1] = reinterpret_cast<void *>(&AddRefFn);
        gOutVTable[2] = reinterpret_cast<void *>(&ReleaseFn);
        gOutVTable[6] = reinterpret_cast<void *>(&OutGetFormatter);
        gOutVTable[7] = reinterpret_cast<void *>(&OutSetFormatter);
        gOutVTable[8] = reinterpret_cast<void *>(&OutGetFilter);
        gOutVTable[9] = reinterpret_cast<void *>(&OutSetFilter);
        gOutVTable[10] = reinterpret_cast<void *>(&OutSetText);
        gOutVTable[11] = reinterpret_cast<void *>(&OutText);
        gOutVTable[12] = reinterpret_cast<void *>(&OutSetEnabled);
        gOutVTable[19] = reinterpret_cast<void *>(&OutWrite);
        gStreamVTable[14] = reinterpret_cast<void *>(&StreamText);
        gStreamVTable[15] = reinterpret_cast<void *>(&StreamString);
        gLockVTable[1] = reinterpret_cast<void *>(&LockFn);
        gLockVTable[2] = reinterpret_cast<void *>(&UnlockFn);
    }
} gSetup;

const char *const kNames[] = {"Main", "main", "Alt", "Debug", "debugger", "x", ""};
constexpr size_t kNameCount = sizeof(kNames) / sizeof(kNames[0]);
const char *const kGroups[] = {"NoGroup", "nogroup", "AnyGroup", "ANYGROUP", "Sim", "sim", "Lot", ""};
constexpr size_t kGroupCount = sizeof(kGroups) / sizeof(kGroups[0]);
const char *const kPatterns[] = {"*", "Ma*", "m?in", "MAIN", "main", "*g*", "?", "", "x*", "Debug?*", "*r"};
constexpr size_t kPatternCount = sizeof(kPatterns) / sizeof(kPatterns[0]);
const char *const kTexts[] = {"", "plain", "two\nlines", "tab\there", "\t\n\n\t", "a \"quoted\" one", "#%r %t\n", "long text that needs the heap\n\tfor sure"};
constexpr size_t kTextCount = sizeof(kTexts) / sizeof(kTexts[0]);

// A description of a world, drawn once and built on both sides.
struct Plan {
    struct Item {
        int name;
        bool answers;
        int filter = -1;    // an output's: index into the filters (-1 none)
        int formatter = -1; // an output's: index into the formatters
    };
    std::vector<Item> outputs, filters, formatters;
    Item extra;             // the output to add (AddOutput)
    bool defaultsAnswer = true;
    uint8_t flag = 1;
    std::vector<int> groupNames;   // names already in the name table (indices into kGroups)
    std::vector<std::pair<int, int>> aliases; // (name, text)
    std::vector<uint32_t> seed;

    static Plan Draw(std::mt19937 &rng, bool outputFilters)
    {
        Plan p;
        auto item = [&](bool withLinks, size_t filters, size_t formatters) {
            Item it{static_cast<int>(rng() % kNameCount), rng() % 3 != 0};
            if (withLinks) {
                if (filters && (outputFilters || rng() % 2)) it.filter = static_cast<int>(rng() % filters);
                if (formatters && rng() % 2) it.formatter = static_cast<int>(rng() % formatters);
            }
            return it;
        };
        int nf = rng() % 4, nm = rng() % 4, no = rng() % 4;
        if (outputFilters && nf == 0) nf = 1;
        for (int k = 0; k < nf; ++k) p.filters.push_back(item(false, 0, 0));
        for (int k = 0; k < nm; ++k) p.formatters.push_back(item(false, 0, 0));
        for (int k = 0; k < no; ++k) p.outputs.push_back(item(true, p.filters.size(), p.formatters.size()));
        p.extra = item(true, p.filters.size(), p.formatters.size());
        p.defaultsAnswer = rng() % 4 != 0;
        p.flag = rng() % 3 == 0 ? 0 : static_cast<uint8_t>(1 + rng() % 255);
        int names = rng() % 4;
        for (int k = 0; k < names; ++k) p.groupNames.push_back(4 + static_cast<int>(rng() % (kGroupCount - 4)));
        int aliases = rng() % 4;
        for (int k = 0; k < aliases; ++k) p.aliases.emplace_back(static_cast<int>(rng() % kNameCount), static_cast<int>(rng() % kTextCount));
        return p;
    }
};

// One side: a service block (from the pair), its world and its tables.
struct Side {
    uint8_t *block;
    World world;
    std::unique_ptr<AliasMap> aliases = std::make_unique<AliasMap>();
    Obj *extra = nullptr;
    Obj *defaultFilter = nullptr, *defaultFormatter = nullptr;

    Side(uint8_t *b, const Plan &plan) : block(b)
    {
        cLogService *s = Service();
        *reinterpret_cast<void **>(block + offsetof(cLogService, mLock28)) = gLockVTable;
        *reinterpret_cast<void **>(block + offsetof(cLogService, mLock90)) = gLockVTable;
        new (&s->mNames) NameSet();
        new (&s->mOutputs) std::vector<RefHolder>();
        new (&s->mFilters) std::vector<RefHolder>();
        new (&s->mFormatters) std::vector<RefHolder>();
        for (int g : plan.groupNames) s->mNames.insert(LogString(kGroups[g]));
        for (auto [n, t] : plan.aliases) AddAliasText(aliases.get(), nullptr, kNames[n], kTexts[t]);
        s->mAliases = aliases.get();
        s->mCollectGroups = plan.flag;
        defaultFilter = world.Make(gNamedVTable, "DefaultFilter");
        defaultFilter->answers = plan.defaultsAnswer;
        defaultFormatter = world.Make(gNamedVTable, "DefaultFormatter");
        s->mFilter.mObject = defaultFilter;
        s->mFormatter.mObject = defaultFormatter;
        std::vector<Obj *> filters, formatters;
        auto named = [&](const Plan::Item &it) {
            Obj *o = world.Make(gNamedVTable, kNames[it.name]);
            o->answers = it.answers;
            return o;
        };
        auto output = [&](const Plan::Item &it) {
            Obj *o = world.Make(gOutVTable, kNames[it.name]);
            if (it.filter >= 0) o->filter = filters[it.filter];
            if (it.formatter >= 0) o->formatter = formatters[it.formatter];
            return o;
        };
        for (const Plan::Item &it : plan.filters) filters.push_back(named(it));
        for (const Plan::Item &it : plan.formatters) formatters.push_back(named(it));
        for (Obj *o : filters) Push(s->mFilters, o);
        for (Obj *o : formatters) Push(s->mFormatters, o);
        for (const Plan::Item &it : plan.outputs) Push(s->mOutputs, output(it));
        extra = output(plan.extra);
        extra->name = "unset";
        gLog.clear();
    }
    ~Side()
    {
        cLogService *s = Service();
        s->mOutputs.~vector();
        s->mFilters.~vector();
        s->mFormatters.~vector();
        s->mNames.~NameSet();
        gLog.clear();
    }
    static void Push(std::vector<RefHolder> &list, Obj *o)
    {
        void *p = o;
        list.push_back(reinterpret_cast<const RefHolder &>(p));
    }
    cLogService *Service() { return reinterpret_cast<cLogService *>(block); }
    void *Sub() { return block + kSub; }
    std::string Lists()
    {
        std::string s;
        cLogService *service = Service();
        for (auto *list : {&service->mOutputs, &service->mFilters, &service->mFormatters}) {
            for (const RefHolder &h : *list) s += Id(h.mObject) + ",";
            s += "|";
        }
        return s + Id(service->mFilter.mObject) + Id(service->mFormatter.mObject);
    }
    std::string Names()
    {
        std::string s;
        for (const LogString &n : Service()->mNames) s += "'" + n.mString + "'";
        return s;
    }
};

// Both sides of one case, the call's log for each, and the comparison of everything after.
struct Case {
    Pair pair;
    Plan plan;
    std::unique_ptr<Side> a, b;
    std::string logA, logB;
    Case(std::mt19937 &rng, bool outputFilters) : pair(cLogService::kSize, rng), plan(Plan::Draw(rng, outputFilters))
    {
        a = std::make_unique<Side>(pair.a, plan);
        b = std::make_unique<Side>(pair.b, plan);
    }
    template <class F, class G>
    void Run(F original, G ours)
    {
        gBase = pair.a;
        gLog.clear();
        original(*a);
        logA = gLog;
        gBase = pair.b;
        gLog.clear();
        ours(*b);
        logB = gLog;
    }
    void Compare(difftest::Context &ctx, const char *what)
    {
        ++ctx.cases;
        if (logA != logB) ctx.Fail("%s: calls differ: %.300s / %.300s", what, logA.c_str(), logB.c_str());
        if (a->Lists() != b->Lists()) ctx.Fail("%s: lists %s / %s", what, a->Lists().c_str(), b->Lists().c_str());
        if (a->Names() != b->Names()) ctx.Fail("%s: names %s / %s", what, a->Names().c_str(), b->Names().c_str());
        if (a->world.Canon() != b->world.Canon()) ctx.Fail("%s: objects %.200s / %.200s", what, a->world.Canon().c_str(), b->world.Canon().c_str());
        if (int at = FirstDifference(pair.a, pair.b, cLogService::kSize, kMasks); at >= 0) ctx.Fail("%s: service bytes differ at %x", what, at);
        if (!pair.CanariesIntact()) ctx.Fail("%s: canaries", what);
    }
};

// The two reserved group names, pointed at our own texts (the image's pointers are not relocated data we rely on).
struct GroupGlobals {
    Slots slots;
    explicit GroupGlobals(difftest::Context &ctx) : slots(ctx)
    {
        slots.Set(kNoGroupPointer, const_cast<char *>("NoGroup"));
        slots.Set(kAnyGroupPointer, const_cast<char *>("AnyGroup"));
    }
};

} // namespace

DIFF_TEST(GZLog_ServiceAddOutput, kAddOutput)
{
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, const char *, void *)>(kAddOutput);
    std::mt19937 rng(kAddOutput);
    int added = 0, cloned = 0, refused = 0;
    for (int i = 0; i < 600; ++i) {
        Case c(rng, false);
        const char *name = kNames[rng() % kNameCount];
        uint8_t ra = 0, rb = 0;
        c.Run([&](Side &s) { ra = original(s.Sub(), name, s.extra); }, [&](Side &s) { rb = ServiceAddOutput(s.Sub(), nullptr, name, s.extra); });
        if (ra != rb) ctx.Fail("\"%s\": result %u / %u", name, ra, rb);
        c.Compare(ctx, name);
        if (ra) ++added;
        else ++refused;
        if (c.logA.find("clone") != std::string::npos) ++cloned;
    }
    if (added < 150 || refused < 50 || cloned < 100) ctx.Fail("coverage: %d added, %d refused, %d cloned", added, refused, cloned);
}

DIFF_TEST(GZLog_ServiceSetFilterGroupLevel, kSetFilterGroupLevel)
{
    GroupGlobals globals(ctx);
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, const char *, const char *, int32_t)>(kSetFilterGroupLevel);
    std::mt19937 rng(kSetFilterGroupLevel);
    int matched = 0, recorded = 0;
    for (int i = 0; i < 800; ++i) {
        Case c(rng, false);
        const char *pattern = kPatterns[rng() % kPatternCount];
        const char *group = kGroups[rng() % kGroupCount];
        int32_t level = static_cast<int32_t>(rng() % 10) - 2;
        uint8_t ra = 0, rb = 0;
        std::string before = c.a->Names();
        c.Run([&](Side &s) { ra = original(s.Sub(), pattern, group, level); },
              [&](Side &s) { rb = ServiceSetFilterGroupLevel(s.Sub(), nullptr, pattern, group, level); });
        if (ra != rb) ctx.Fail("\"%s\" \"%s\": result %u / %u", pattern, group, ra, rb);
        c.Compare(ctx, pattern);
        if (ra) ++matched;
        if (c.a->Names() != before) ++recorded;
    }
    if (matched < 150 || recorded < 100) ctx.Fail("coverage: %d matched, %d recorded a name", matched, recorded);
}

DIFF_TEST(GZLog_ServiceSetOutputGroupLevel, kSetOutputGroupLevel)
{
    GroupGlobals globals(ctx);
    auto original = ctx.Original<uint8_t(__thiscall *)(void *, const char *, const char *, int32_t)>(kSetOutputGroupLevel);
    std::mt19937 rng(kSetOutputGroupLevel);
    int matched = 0, recorded = 0, refused = 0;
    for (int i = 0; i < 800; ++i) {
        Case c(rng, true); // every output has a filter (a null one faults in both)
        const char *pattern = kPatterns[rng() % kPatternCount];
        const char *group = kGroups[rng() % kGroupCount];
        int32_t level = static_cast<int32_t>(rng() % 10) - 2;
        uint8_t ra = 0, rb = 0;
        std::string before = c.a->Names();
        c.Run([&](Side &s) { ra = original(s.Sub(), pattern, group, level); },
              [&](Side &s) { rb = ServiceSetOutputGroupLevel(s.Sub(), nullptr, pattern, group, level); });
        if (ra != rb) ctx.Fail("\"%s\" \"%s\": result %u / %u", pattern, group, ra, rb);
        c.Compare(ctx, pattern);
        if (ra) ++matched;
        if (c.a->Names() != before) ++recorded;
        if (ra && c.logA.find("grouplevel") == std::string::npos) ++refused;
    }
    if (matched < 150 || recorded < 100 || refused < 10) ctx.Fail("coverage: %d matched, %d recorded a name, %d matched without a filter answer", matched, recorded, refused);
}

DIFF_TEST(GZLog_QuoteText, kQuoteText)
{
    auto original = ctx.Original<cRZString *(__thiscall *)(void *, const cRZString *, cRZString *)>(kQuoteText);
    std::mt19937 rng(kQuoteText);
    for (int i = 0; i < 500; ++i) {
        std::string text;
        int parts = rng() % 6;
        for (int k = 0; k < parts; ++k) text += kTexts[rng() % kTextCount];
        LogString in(text.c_str());
        LogString outA(kTexts[rng() % kTextCount]), outB(outA.mString.c_str());
        cRZString *ra = original(nullptr, &in, &outA);
        cRZString *rb = QuoteText(nullptr, nullptr, &in, &outB);
        ++ctx.cases;
        if (ra != &outA || rb != &outB) ctx.Fail("result not the out string");
        if (outA.mString != outB.mString) ctx.Fail("\"%s\": '%s' / '%s'", text.c_str(), outA.mString.c_str(), outB.mString.c_str());
        if (outA.mString.capacity() != outB.mString.capacity()) ctx.Fail("\"%s\": capacity %zu / %zu", text.c_str(), outA.mString.capacity(), outB.mString.capacity());
        if (in.mString != text) ctx.Fail("input changed");
    }
}

DIFF_TEST(GZLog_WritePresets, kWritePresets)
{
    auto original = ctx.Original<void(__thiscall *)(AliasMap *, void *)>(kWritePresets);
    std::mt19937 rng(kWritePresets);
    for (int i = 0; i < 300; ++i) {
        AliasMap aliases;
        int n = rng() % 6;
        for (int k = 0; k < n; ++k) AddAliasText(&aliases, nullptr, kNames[rng() % kNameCount], kTexts[rng() % kTextCount]);
        gLog.clear();
        original(&aliases, &gStream);
        std::string logA = gLog;
        gLog.clear();
        WritePresets(&aliases, nullptr, &gStream);
        std::string logB = gLog;
        ++ctx.cases;
        if (logA != logB) ctx.Fail("%d presets: %.300s / %.300s", n, logA.c_str(), logB.c_str());
        if (n && logA.find("logPreset") == std::string::npos) ctx.Fail("no preset written");
    }
}

DIFF_TEST(GZLog_ServiceWriteConfig, kWriteConfig)
{
    auto original = ctx.Original<void(__thiscall *)(void *, void *)>(kWriteConfig);
    std::mt19937 rng(kWriteConfig);
    for (int i = 0; i < 300; ++i) {
        Case c(rng, false);
        c.Run([&](Side &s) { original(s.Sub(), &gStream); }, [&](Side &s) { ServiceWriteConfig(s.Sub(), nullptr, &gStream); });
        c.Compare(ctx, "config");
    }
}
