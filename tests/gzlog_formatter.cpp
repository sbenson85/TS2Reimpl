// Differential tests for nGZLog::cGZLogFormatterText's slots and helpers and the time stamp's texts
// (src/rz/nGZLogFormatter.cpp, lane LY): SetFormat (slot 3, 0xc9e426), Format (slot 4, 0xc9d121), Compose (0xc9d711),
// Expand (0xc9d807), Write (slot 5, 0xc9eb68), Clone (slot 8, 0xc9cfa1), LevelIndex (0xc9d3bb), LookupAlias
// (0xc9d452), cLogTimestamp::Update (0xc9d137), ThreeDigits (0xc9cee5), FormatElapsed (0xc9d1d8) and MemoryReport
// (0xc6aa35).
//
// Side A is a formatter built by the image's constructor (0xc9c164) and driven by the image's code, side B one built
// by ours and driven by ours; each side has its own time stamp object, filled alike. The record and the output stream
// are fakes whose slots log their calls and answer from the case's script. The image's C runtime pieces the code
// reaches (sprintf 0x14487d, strftime 0x841593, _gmtime64 0x847b95, _localtime64 0x82fb65) are pointed at this
// executable's (state the harness has not initialised; ours calls the same functions), and cRZString::Sprintf
// (0x1ad6f7 and ours) is stood in on both sides. The level text pointers and the "no preset" pointer (0x1085da0)
// are pointed at test texts; MemoryReport's two imports (GetModuleHandleA, GetProcAddress) at fakes.
#include "gzlog_support.h" // first: it defines NOMINMAX before windows.h
#include "code_patch.h"
#include "difftest.h"
#include "game/vtable.h"
#include "rz/nGZLog.h"

#include <climits>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace nGZLog;

namespace {

constexpr uint32_t kSetFormat = 0xc9e426, kFormat = 0xc9d121, kCompose = 0xc9d711, kExpand = 0xc9d807, kWrite = 0xc9eb68;
constexpr uint32_t kClone = 0xc9cfa1, kLevelIndex = 0xc9d3bb, kLookupAlias = 0xc9d452, kUpdate = 0xc9d137;
constexpr uint32_t kThreeDigits = 0xc9cee5, kFormatElapsed = 0xc9d1d8, kMemoryReport = 0xc6aa35;
constexpr uint32_t kFormatterCtor = 0xc9c164, kFormatterDtor = 0xc9c75e;
constexpr uint32_t kImageSprintf = 0x14487d, kImageStrftime = 0x841593, kImageGmtime = 0x847b95, kImageLocaltime = 0x82fb65;
constexpr uint32_t kStringSprintf = 0x1ad6f7;
constexpr uint32_t kIatGetModuleHandleA = 0xd9a114, kIatGetProcAddress = 0xd9a124;
constexpr uint32_t kNoPresetPointer = 0x1085da0;
constexpr uint32_t kLevelPointers[7] = {0x1085d94, 0x1085d6c, 0x1085d74, 0x1085d7c, 0x1085d84, 0x1085d8c, 0x1085d64}; // levels 0, 2..7
const char *const kLevelTexts[7] = {"L0", "L2", "L3", "L4", "L5", "L6", "L7"};

std::string gLog;

void Log(const char *fmt, ...)
{
    char buf[200];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    gLog += buf;
    gLog += ';';
}

// Saves and restores pointer globals of the mapped image.
class Globals {
public:
    explicit Globals(difftest::Context &c) : ctx(c) {}
    ~Globals()
    {
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) *ctx.image.At<uint32_t>(it->first) = it->second;
    }
    void Set(uint32_t rva, const void *pointer)
    {
        uint32_t *slot = ctx.image.At<uint32_t>(rva);
        saved.emplace_back(rva, *slot);
        *slot = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer));
    }

private:
    difftest::Context &ctx;
    std::vector<std::pair<uint32_t, uint32_t>> saved;
};

void PointTexts(Globals &globals, const char *noPreset)
{
    for (int i = 0; i < 7; ++i) globals.Set(kLevelPointers[i], kLevelTexts[i]);
    globals.Set(kNoPresetPointer, noPreset);
}

// ---- the C runtime stand-ins ----

int __cdecl StandSprintf(char *buffer, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    const int n = std::vsprintf(buffer, format, args);
    va_end(args);
    return n;
}
size_t __cdecl StandStrftime(char *buffer, size_t size, const char *format, const tm *parts)
{
    return std::strftime(buffer, size, format, parts);
}
tm *__cdecl StandGmtime(const __time64_t *when) { return _gmtime64(when); }
tm *__cdecl StandLocaltime(const __time64_t *when) { return _localtime64(when); }
cRZString *__cdecl StandStringSprintf(cRZString *self, const char *fmt, ...)
{
    char text[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    self->mString = text;
    return self;
}

struct Crt {
    shape::CodePatch sprintfPatch{kImageSprintf, shape::CodeOf(&StandSprintf)};
    shape::CodePatch strftimePatch{kImageStrftime, shape::CodeOf(&StandStrftime)};
    shape::CodePatch gmtimePatch{kImageGmtime, shape::CodeOf(&StandGmtime)};
    shape::CodePatch localtimePatch{kImageLocaltime, shape::CodeOf(&StandLocaltime)};
    shape::StandIn stringSprintf{kStringSprintf, &cRZString::Sprintf, &StandStringSprintf};
};

// ---- the fakes ----

struct FakeRecord {
    void *const *vtable;
    int32_t words[14];
    const char *texts[14];
};
template <int N> int32_t __fastcall RecordWordSlot(FakeRecord *self, void *)
{
    Log("r%d=%d", N, self->words[N]);
    return self->words[N];
}
template <int N> const char *__fastcall RecordTextSlot(FakeRecord *self, void *)
{
    Log("r%d", N);
    return self->texts[N];
}
void *gRecordVTable[14];

uint32_t gWriteCount = 0;
struct FakeStream {
    void *const *vtable;
};
uint32_t __fastcall StreamText(FakeStream *, void *, const char *text)
{
    Log("t'%s'", text);
    return gWriteCount++;
}
uint32_t __fastcall StreamString(FakeStream *, void *, const cRZString *text)
{
    Log("s'%s'", text->mString.c_str());
    return gWriteCount++;
}
void *gStreamVTable[16];

// The memory report: GetModuleHandleA / GetProcAddress fakes and a report object whose slot 9 prints.
struct FakeReport {
    void *const *vtable;
};
void __fastcall ReportPrint(FakeReport *, void *, cRZString *out, uint32_t a, uint32_t b, uint32_t c)
{
    Log("report.print(%u, %u, %u)", a, b, c);
    out->mString = "MEM " + std::to_string(a + b + c);
}
void *gReportVTable[12];
FakeReport gReport{gReportVTable};
bool gHasReport = false;
void *__cdecl ReportExport()
{
    Log("export");
    return &gReport;
}
HMODULE WINAPI FakeGetModuleHandleA(const char *name)
{
    Log("GetModuleHandleA(%s)", name ? name : "null");
    return reinterpret_cast<HMODULE>(static_cast<uintptr_t>(0x400000));
}
FARPROC WINAPI FakeGetProcAddress(HMODULE module, const char *name)
{
    Log("GetProcAddress(%p, %s)", static_cast<void *>(module), name);
    return gHasReport ? reinterpret_cast<FARPROC>(&ReportExport) : nullptr;
}

struct Setup {
    Setup()
    {
        gRecordVTable[3] = reinterpret_cast<void *>(&RecordWordSlot<3>);
        gRecordVTable[4] = reinterpret_cast<void *>(&RecordTextSlot<4>);
        gRecordVTable[5] = reinterpret_cast<void *>(&RecordTextSlot<5>);
        gRecordVTable[6] = reinterpret_cast<void *>(&RecordWordSlot<6>);
        gRecordVTable[7] = reinterpret_cast<void *>(&RecordWordSlot<7>);
        gRecordVTable[8] = reinterpret_cast<void *>(&RecordWordSlot<8>);
        gRecordVTable[9] = reinterpret_cast<void *>(&RecordWordSlot<9>);
        gRecordVTable[10] = reinterpret_cast<void *>(&RecordWordSlot<10>);
        gRecordVTable[11] = reinterpret_cast<void *>(&RecordWordSlot<11>);
        gRecordVTable[12] = reinterpret_cast<void *>(&RecordTextSlot<12>);
        gRecordVTable[13] = reinterpret_cast<void *>(&RecordWordSlot<13>);
        gStreamVTable[14] = reinterpret_cast<void *>(&StreamText);
        gStreamVTable[15] = reinterpret_cast<void *>(&StreamString);
        gReportVTable[9] = reinterpret_cast<void *>(&ReportPrint);
    }
} gSetup;

class Imports {
public:
    explicit Imports(difftest::Context &ctx) : slots(ctx)
    {
        slots.Set(kIatGetModuleHandleA, reinterpret_cast<void *>(&FakeGetModuleHandleA));
        slots.Set(kIatGetProcAddress, reinterpret_cast<void *>(&FakeGetProcAddress));
    }

private:
    gzlog_test::Slots slots;
};

// ---- scripts ----

const char *const kMessages[] = {"hello", "", "two\nlines\n", "\n", "tab\there", "end\n\n", "x"};
const char *const kFiles[] = {"file.cpp", "", nullptr, "C:\\src\\a.cpp"};
const char *const kGroups[] = {"Net", "", "Script"};

int32_t RandomWord(std::mt19937 &rng)
{
    static const int32_t kWords[] = {0, 1, -1, 7, 999, 1000, 1001, 59999, 3600000, 86399999, INT_MAX, INT_MIN, -1000, 123456789};
    return rng() % 3 ? kWords[rng() % (sizeof(kWords) / sizeof(kWords[0]))] : static_cast<int32_t>(rng());
}

FakeRecord RandomRecord(std::mt19937 &rng)
{
    FakeRecord r{gRecordVTable, {}, {}};
    for (int i = 0; i < 14; ++i) r.words[i] = RandomWord(rng);
    r.words[3] = static_cast<int32_t>(rng() % 10) - 1;
    r.texts[4] = kMessages[rng() % (sizeof(kMessages) / sizeof(kMessages[0]))];
    r.texts[5] = kGroups[rng() % 3];
    r.texts[12] = kFiles[rng() % 4];
    return r;
}

// A time stamp both sides start from: start time, start reading, last reading, flags; the texts filled alike.
struct Stamp {
    alignas(8) uint8_t bytes[cLogTimestamp::kSize];
    cLogTimestamp *Get() { return reinterpret_cast<cLogTimestamp *>(bytes); }
};
void FillStamp(std::mt19937 &rng, Stamp &a, Stamp &b)
{
    for (size_t i = 0; i < cLogTimestamp::kSize; ++i) a.bytes[i] = b.bytes[i] = static_cast<uint8_t>(rng());
    // The texts a flag turns off keep what they held: end each within its 16 bytes, so reading one stays inside the
    // object (the game's flags are always on).
    for (size_t end : {0x1f, 0x2f, 0x3f}) a.bytes[end] = b.bytes[end] = 0;
    const uint8_t date = static_cast<uint8_t>(rng() % 3 ? 1 : 0);
    const uint8_t clock = static_cast<uint8_t>(rng() % 3 ? 1 : 0);
    const uint8_t utc = static_cast<uint8_t>(rng() % 2 ? 0x80 : 0);
    const uint32_t start = 0x40000000u + static_cast<uint32_t>(rng() % 0x30000000u);
    const uint32_t reading = static_cast<uint32_t>(rng());
    const uint32_t last = rng() % 4 == 0 ? reading : 5;
    for (Stamp *s : {&a, &b}) {
        cLogTimestamp *t = s->Get();
        t->mFlag0 = date;
        t->mFlag1 = clock;
        t->mFlag2 = utc;
        t->mTime = start;
        t->mElapsedLow = reading;
        t->mElapsedHigh = last;
    }
}
// A record time near the time stamp's start reading (so the wall-clock time stays valid).
int32_t NearTime(std::mt19937 &rng, const cLogTimestamp *t)
{
    static const int32_t kOffsets[] = {0, 1, 999, 1000, 1001, -1, -999, -1000, -1001, 86400000, -86400000, 3599999, 0x7fffffff};
    const int32_t offset = rng() % 2 ? kOffsets[rng() % (sizeof(kOffsets) / sizeof(kOffsets[0]))] : static_cast<int32_t>(rng() % 200000000) - 100000000;
    return static_cast<int32_t>(t->mElapsedLow + static_cast<uint32_t>(offset));
}

// A pair of formatters, A built by the image's constructor, B by ours, with the same strings.
struct Formatters {
    difftest::Context &ctx;
    cLogFormatterText *a, *b;
    Formatters(difftest::Context &c) : ctx(c)
    {
        a = static_cast<cLogFormatterText *>(::operator new(cLogFormatterText::kSize));
        b = static_cast<cLogFormatterText *>(::operator new(cLogFormatterText::kSize));
        ctx.Original<cLogFormatterText *(__thiscall *)(void *)>(kFormatterCtor)(a);
        b->Construct();
    }
    ~Formatters()
    {
        ctx.Original<void(__thiscall *)(void *)>(kFormatterDtor)(a);
        ::operator delete(a);
        b->Destruct();
        ::operator delete(b);
    }
    Formatters(const Formatters &) = delete;
    Formatters &operator=(const Formatters &) = delete;
    template <class F> void Both(F f)
    {
        f(a);
        f(b);
    }
};

std::string StringsOf(const cLogFormatterText *f)
{
    std::string s = "name '" + f->mName.mString + "'";
    for (const cRZString &x : f->mFormats) s += " f'" + x.mString + "'";
    for (const cRZString &x : f->mPresets) s += " p'" + x.mString + "'";
    return s + " out '" + f->mOutput.mString + "' scratch '" + f->mScratch.mString + "'";
}

const char *const kPresetNames[] = {"DebugFormat", "Plain", "NoPreset", "missing", "", "debugformat"};
constexpr size_t kPresetCount = sizeof(kPresetNames) / sizeof(kPresetNames[0]);

std::unique_ptr<AliasMap> MakeAliases()
{
    auto m = std::make_unique<AliasMap>();
    AddAliasText(m.get(), nullptr, "DebugFormat", "#%r %t %G: %M\\n");
    AddAliasText(m.get(), nullptr, "Plain", "%M");
    AddAliasText(m.get(), nullptr, "NoPreset", "");
    AddAliasText(m.get(), nullptr, "", "empty-key");
    return m;
}

const char kCodes[] = "NMGLFDtecpfrCR%xZ\0n\\";

std::string RandomFormat(std::mt19937 &rng)
{
    std::string s;
    const int n = static_cast<int>(rng() % 12);
    for (int i = 0; i < n; ++i) {
        switch (rng() % 4) {
        case 0: s += '%'; s += kCodes[rng() % (sizeof(kCodes) - 1)]; break;
        case 1: s += '\\'; s += "nt\\x%"[rng() % 5]; break;
        default: s += "ab :#()-"[rng() % 8]; break;
        }
    }
    if (rng() % 4 == 0) s += rng() % 2 ? '%' : '\\';
    // A NUL inside the text cuts nothing: the format is read up to its size.
    return s;
}

} // namespace

DIFF_TEST(GZLogFormatter_ThreeDigits, kThreeDigits)
{
    auto original = ctx.Original<void(__thiscall *)(void *, char *, uint32_t)>(kThreeDigits);
    std::mt19937 rng(kThreeDigits);
    for (int i = 0; i < 4000; ++i) {
        const uint32_t value = i < 1100 ? static_cast<uint32_t>(i) : (rng() % 2 ? static_cast<uint32_t>(rng()) : static_cast<uint32_t>(-(i % 1000)));
        char a[6], b[6];
        std::memset(a, 'q', sizeof(a));
        std::memset(b, 'q', sizeof(b));
        original(reinterpret_cast<void *>(static_cast<uintptr_t>(rng())), a, value);
        cLogTimestamp::ThreeDigits(b, value);
        ++ctx.cases;
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("%u: '%.6s' / '%.6s'", value, a, b);
    }
}

DIFF_TEST(GZLogFormatter_FormatElapsed, kFormatElapsed)
{
    Crt crt;
    auto original = ctx.Original<void(__thiscall *)(void *, int32_t, char *)>(kFormatElapsed);
    std::mt19937 rng(kFormatElapsed);
    for (int i = 0; i < 4000; ++i) {
        const int32_t ms = RandomWord(rng);
        char a[24], b[24];
        std::memset(a, 'q', sizeof(a));
        std::memset(b, 'q', sizeof(b));
        original(nullptr, ms, a);
        cLogTimestamp::FormatElapsed(ms, b);
        ++ctx.cases;
        if (std::memcmp(a, b, sizeof(a)) != 0) ctx.Fail("%d: '%.24s' / '%.24s'", ms, a, b);
    }
}

DIFF_TEST(GZLogFormatter_TimestampUpdate, kUpdate)
{
    Crt crt;
    auto original = ctx.Original<void(__thiscall *)(void *, int32_t)>(kUpdate);
    std::mt19937 rng(kUpdate);
    for (int i = 0; i < 3000; ++i) {
        Stamp a, b;
        FillStamp(rng, a, b);
        const int calls = 1 + static_cast<int>(rng() % 3);
        for (int k = 0; k < calls; ++k) {
            const int32_t ms = rng() % 5 == 0 ? static_cast<int32_t>(a.Get()->mElapsedHigh) : NearTime(rng, a.Get());
            original(a.Get(), ms);
            b.Get()->Update(ms);
            ++ctx.cases;
            if (std::memcmp(a.bytes, b.bytes, 0x40) != 0)
                ctx.Fail("case %d (flags %u%u%u, ms %d): '%.16s' '%.16s' '%.16s' / '%.16s' '%.16s' '%.16s'", i, a.Get()->mFlag0, a.Get()->mFlag1, a.Get()->mFlag2, ms,
                         a.Get()->mDate, a.Get()->mClock, a.Get()->mElapsed, b.Get()->mDate, b.Get()->mClock, b.Get()->mElapsed);
        }
    }
}

DIFF_TEST(GZLogFormatter_LevelIndex, kLevelIndex)
{
    auto original = ctx.Original<int32_t(__thiscall *)(void *, int32_t)>(kLevelIndex);
    std::mt19937 rng(kLevelIndex);
    for (int i = 0; i < 2000; ++i) {
        const int32_t level = i < 20 ? i - 5 : static_cast<int32_t>(rng());
        ++ctx.cases;
        const int32_t ra = original(nullptr, level), rb = cLogFormatterText::LevelIndex(level);
        if (ra != rb) ctx.Fail("%d: %d / %d", level, ra, rb);
    }
}

DIFF_TEST(GZLogFormatter_LookupAlias, kLookupAlias)
{
    auto original = ctx.Original<uint32_t(__thiscall *)(void *, const char *, cRZString *)>(kLookupAlias);
    std::mt19937 rng(kLookupAlias);
    auto aliases = MakeAliases();
    for (int i = 0; i < 600; ++i) {
        const char *name = kPresetNames[rng() % kPresetCount];
        LogString outA, outB;
        outA.mString = outB.mString = rng() % 2 ? "previous text" : "";
        const uint8_t ra = static_cast<uint8_t>(original(aliases.get(), name, &outA));
        const uint8_t rb = LookupAlias(aliases.get(), nullptr, name, &outB);
        ++ctx.cases;
        if (ra != rb || outA.mString != outB.mString) ctx.Fail("'%s': %u '%s' / %u '%s'", name, ra, outA.mString.c_str(), rb, outB.mString.c_str());
    }
}

DIFF_TEST(GZLogFormatter_SetFormat, kSetFormat)
{
    Globals globals(ctx);
    auto original = ctx.Original<void(__thiscall *)(void *, int32_t, const char *)>(kSetFormat);
    std::mt19937 rng(kSetFormat);
    auto aliases = MakeAliases();
    static const char *const kNoPresets[] = {"NoPreset", "none!"};
    for (int i = 0; i < 400; ++i) {
        PointTexts(globals, kNoPresets[i % 2]);
        Formatters f(ctx);
        AliasMap *map = rng() % 6 ? aliases.get() : nullptr;
        f.Both([&](cLogFormatterText *x) { x->mAliases = map; });
        const int calls = 1 + static_cast<int>(rng() % 6);
        for (int k = 0; k < calls; ++k) {
            const int32_t level = static_cast<int32_t>(rng() % 10) - 1;
            const char *preset = kPresetNames[rng() % kPresetCount];
            original(f.a, level, preset);
            f.b->SetFormat(level, preset);
            ++ctx.cases;
            const std::string sa = StringsOf(f.a), sb = StringsOf(f.b);
            if (sa != sb) ctx.Fail("case %d (%d, '%s'): %.220s / %.220s", i, level, preset, sa.c_str(), sb.c_str());
        }
    }
}

DIFF_TEST(GZLogFormatter_Expand, kExpand)
{
    Crt crt;
    Globals globals(ctx);
    Imports imports(ctx);
    PointTexts(globals, "NoPreset");
    auto original = ctx.Original<void(__thiscall *)(void *, void *, char)>(kExpand);
    std::mt19937 rng(kExpand);
    for (int i = 0; i < 3000; ++i) {
        Formatters f(ctx);
        Stamp sa, sb;
        FillStamp(rng, sa, sb);
        f.a->mTimestamp = sa.Get();
        f.b->mTimestamp = sb.Get();
        const std::string start = rng() % 2 ? "prefix " : "";
        const std::string scratch = rng() % 2 ? "old" : "";
        f.Both([&](cLogFormatterText *x) { x->mOutput.mString = start; x->mScratch.mString = scratch; });
        FakeRecord record = RandomRecord(rng);
        record.words[6] = NearTime(rng, sa.Get());
        gHasReport = rng() % 2 != 0;
        const char code = kCodes[rng() % (sizeof(kCodes) - 1)];
        gLog.clear();
        original(f.a, &record, code);
        const std::string logA = gLog;
        gLog.clear();
        f.b->Expand(&record, code);
        ++ctx.cases;
        const std::string a = StringsOf(f.a), b = StringsOf(f.b);
        if (a != b) ctx.Fail("case %d '%c': %.200s / %.200s", i, code, a.c_str(), b.c_str());
        if (logA != gLog) ctx.Fail("case %d '%c': calls %.200s / %.200s", i, code, logA.c_str(), gLog.c_str());
        if (std::memcmp(sa.bytes, sb.bytes, 0x40) != 0) ctx.Fail("case %d '%c': time stamps differ", i, code);
    }
}

namespace {

// Compose and Format: random formats at random levels.
void RunCompose(difftest::Context &ctx, bool viaFormat, uint32_t seed)
{
    Crt crt;
    Globals globals(ctx);
    Imports imports(ctx);
    PointTexts(globals, "NoPreset");
    auto compose = ctx.Original<void(__thiscall *)(void *, void *)>(kCompose);
    auto format = ctx.Original<cRZString *(__thiscall *)(void *, void *)>(kFormat);
    std::mt19937 rng(seed);
    for (int i = 0; i < 2000; ++i) {
        Formatters f(ctx);
        Stamp sa, sb;
        FillStamp(rng, sa, sb);
        f.a->mTimestamp = sa.Get();
        f.b->mTimestamp = sb.Get();
        for (int k = 0; k < 5; ++k) {
            const std::string text = rng() % 5 ? RandomFormat(rng) : "";
            f.Both([&](cLogFormatterText *x) { x->mFormats[k].mString = text; });
        }
        const std::string start = rng() % 2 ? "stale output" : "";
        f.Both([&](cLogFormatterText *x) { x->mOutput.mString = start; });
        FakeRecord record = RandomRecord(rng);
        record.words[6] = NearTime(rng, sa.Get());
        gHasReport = rng() % 2 != 0;
        gLog.clear();
        cRZString *ra = nullptr, *rb = nullptr;
        if (viaFormat) ra = format(f.a, &record);
        else compose(f.a, &record);
        const std::string logA = gLog;
        gLog.clear();
        if (viaFormat) rb = f.b->Format(&record);
        else f.b->Compose(&record);
        ++ctx.cases;
        const std::string a = StringsOf(f.a), b = StringsOf(f.b);
        if (a != b) ctx.Fail("case %d (level %d): %.220s / %.220s", i, record.words[3], a.c_str(), b.c_str());
        if (logA != gLog) ctx.Fail("case %d: calls %.200s / %.200s", i, logA.c_str(), gLog.c_str());
        if (viaFormat && (ra != &f.a->mOutput || rb != &f.b->mOutput)) ctx.Fail("case %d: result is not the output string", i);
        if (std::memcmp(sa.bytes, sb.bytes, 0x40) != 0) ctx.Fail("case %d: time stamps differ", i);
    }
}

} // namespace

DIFF_TEST(GZLogFormatter_Compose, kCompose)
{
    RunCompose(ctx, false, kCompose);
}

DIFF_TEST(GZLogFormatter_Format, kFormat)
{
    RunCompose(ctx, true, kFormat);
}

DIFF_TEST(GZLogFormatter_Write, kWrite)
{
    Globals globals(ctx);
    PointTexts(globals, "NoPreset");
    auto original = ctx.Original<void(__thiscall *)(void *, void *)>(kWrite);
    std::mt19937 rng(kWrite);
    FakeStream stream{gStreamVTable};
    for (int i = 0; i < 300; ++i) {
        Formatters f(ctx);
        const std::string name = rng() % 3 ? "Default" : "";
        std::string presets[5];
        for (std::string &p : presets) p = kPresetNames[rng() % kPresetCount];
        f.Both([&](cLogFormatterText *x) {
            x->mName.mString = name;
            for (int k = 0; k < 5; ++k) x->mPresets[k].mString = presets[k];
        });
        gLog.clear();
        gWriteCount = 0;
        original(f.a, &stream);
        const std::string logA = gLog;
        gLog.clear();
        gWriteCount = 0;
        f.b->Write(&stream);
        ++ctx.cases;
        if (logA != gLog) ctx.Fail("case %d: %.240s / %.240s", i, logA.c_str(), gLog.c_str());
    }
}

DIFF_TEST(GZLogFormatter_Clone, kClone)
{
    Globals globals(ctx);
    auto original = ctx.Original<cLogFormatterText *(__thiscall *)(void *)>(kClone);
    std::mt19937 rng(kClone);
    auto aliases = MakeAliases();
    static const char *const kNoPresets[] = {"NoPreset", "none!"};
    for (int i = 0; i < 300; ++i) {
        PointTexts(globals, kNoPresets[i % 2]);
        Formatters f(ctx);
        AliasMap *map = rng() % 4 ? aliases.get() : nullptr;
        cLogTimestamp *stamp = reinterpret_cast<cLogTimestamp *>(static_cast<uintptr_t>(0x10000 + rng() % 0x10000));
        std::string texts[11];
        for (std::string &t : texts) t = rng() % 3 ? RandomFormat(rng) : "";
        f.Both([&](cLogFormatterText *x) {
            x->mAliases = map;
            x->mTimestamp = stamp;
            x->mName.mString = texts[10];
            for (int k = 0; k < 5; ++k) {
                x->mFormats[k].mString = texts[k];
                x->mPresets[k].mString = texts[5 + k];
            }
        });
        cLogFormatterText *ca = original(f.a);
        cLogFormatterText *cb = f.b->Clone();
        ++ctx.cases;
        const std::string sa = StringsOf(ca), sb = StringsOf(cb);
        if (sa != sb) ctx.Fail("case %d: %.220s / %.220s", i, sa.c_str(), sb.c_str());
        if (ca->mAliases != cb->mAliases || ca->mTimestamp != cb->mTimestamp || cb->mAliases != map || cb->mTimestamp != stamp) ctx.Fail("case %d: table or time stamp", i);
        if (ca->mVTable != cb->mVTable || ca->mUnknown.mVTable != cb->mUnknown.mVTable || ca->mUnknown.mRefCount != cb->mUnknown.mRefCount) ctx.Fail("case %d: vtables or count", i);
        ctx.Original<void(__thiscall *)(void *)>(kFormatterDtor)(ca);
        ::operator delete(ca);
        cb->Destruct();
        ::operator delete(cb);
    }
}

DIFF_TEST(GZLogFormatter_MemoryReport, kMemoryReport)
{
    Imports imports(ctx);
    auto original = ctx.Original<void *(__cdecl *)()>(kMemoryReport);
    for (int i = 0; i < 4; ++i) {
        gHasReport = i % 2 != 0;
        gLog.clear();
        void *ra = original();
        const std::string logA = gLog;
        gLog.clear();
        void *rb = MemoryReport();
        ++ctx.cases;
        if (ra != rb || ra != (gHasReport ? &gReport : nullptr)) ctx.Fail("%d: %p / %p", i, ra, rb);
        if (logA != gLog) ctx.Fail("%d: %.200s / %.200s", i, logA.c_str(), gLog.c_str());
    }
}
