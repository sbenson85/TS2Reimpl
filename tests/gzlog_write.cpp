// Differential tests for nGZLog::cService's write path and its helpers (src/rz/nGZLogLifetime.size.cpp, lane LZ):
// cLogTimestamp::CopyFrom (0xc9e950) and ElapsedMilliseconds (0xc9d4ef), cLogProcessInfo::Sample (0xc9cf24) and
// CpuTime (0xc9d2a2), cLogRecord::Set (0xc9e57b), FormatText (0xc9d22a), the time-stamped file name (slot 23,
// 0xc9d505) and the write path (slot 28, 0xc9dd56).
//
// The service's collaborators are fakes whose slots log their calls (`this` relative to the service block where it is
// part of it): the service's own vtables, the locks, the output back ends (whose slots log the record they are given,
// field by field, its texts as text). The import slots the code reads (GetCurrentThreadId, GetProcessTimes, the
// clocks) are pointed at scripted fakes for both sides (gzlog_test::Slots / ClockScope). The image's C runtime is
// the image's own (the harness lets it run on the process heap); time texts go through the stand-ins of
// tests/gzlog_formatter.cpp's kind (strftime, _localtime64 / _gmtime64, sprintf) on the original's side.
#include "gzlog_support.h" // first: it defines NOMINMAX before windows.h
#include "code_patch.h"
#include "difftest.h"
#include "free_recorder.h"
#include "game/vtable.h"
#include "rz/cRZStringSub.h"
#include "rz/nGZLog.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <malloc.h>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace gzlog_test;
using namespace nGZLog;

namespace {

constexpr uint32_t kCopyFrom = 0xc9e950, kElapsed = 0xc9d4ef, kSample = 0xc9cf24, kCpuTime = 0xc9d2a2;
constexpr uint32_t kRecordSet = 0xc9e57b, kFormatText = 0xc9d22a, kTimestampedName = 0xc9d505, kWrite = 0xc9dd56;
constexpr uint32_t kTimestampCtor = 0xc9c291;
constexpr uint32_t kIatGetCurrentThreadId = 0xd9a194, kIatGetProcessTimes = 0xd9a42c;
constexpr uint32_t kImageSprintf = 0x14487d, kImageStrftime = 0x841593, kImageGmtime = 0x847b95, kImageLocaltime = 0x82fb65;
constexpr size_t kSub = cLogService::kServiceSubobjectOffset;

std::vector<std::string> gLog;
const uint8_t *gBase = nullptr;

void Log(const char *fmt, ...)
{
    char buf[400];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (gLog.capacity() < 8192) gLog.reserve(8192); // no growth (and frees of the old array) while a side runs
    gLog.push_back(buf);
}

std::string Joined()
{
    std::string s;
    for (const std::string &l : gLog) s += l + "; ";
    return s;
}

int32_t Rel(const void *p)
{
    return static_cast<int32_t>(reinterpret_cast<const uint8_t *>(p) - gBase);
}

// Saves and restores 32-bit words of the mapped image.
class Words {
public:
    explicit Words(difftest::Context &c) : ctx(c) {}
    ~Words()
    {
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) *ctx.image.At<uint32_t>(it->first) = it->second;
    }
    void Set(uint32_t rva, uint32_t value)
    {
        uint32_t *slot = ctx.image.At<uint32_t>(rva);
        saved.emplace_back(rva, *slot);
        *slot = value;
    }
    void SetFloat(uint32_t rva, float value)
    {
        uint32_t bits;
        std::memcpy(&bits, &value, 4);
        Set(rva, bits);
    }

private:
    difftest::Context &ctx;
    std::vector<std::pair<uint32_t, uint32_t>> saved;
};

// The stopwatch's calibration globals: no overhead, and fixed scale factors for modes 2..6.
void SetStopwatchScales(Words &words)
{
    words.Set(cRZStopwatch::kOverheadRva, 0);
    words.Set(cRZStopwatch::kOverheadRva + 4, 0);
    const float scales[5] = {0.5f, 0.001f, 1.0f / 1024, 3.25f, 1.0f};
    for (int k = 0; k < 5; ++k) {
        words.SetFloat(cRZStopwatch::kScalesRva + 4 * k, scales[k]);
        words.SetFloat(cRZStopwatch::kInverseScalesRva + 4 * k, 1.0f / scales[k]);
    }
}

// The C runtime's time pieces on the original's side (as in tests/gzlog_formatter.cpp: state the harness has not
// initialised); ours calls the same functions.
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

struct Crt {
    shape::CodePatch sprintfPatch{kImageSprintf, shape::CodeOf(&StandSprintf)};
    shape::CodePatch strftimePatch{kImageStrftime, shape::CodeOf(&StandStrftime)};
    shape::CodePatch gmtimePatch{kImageGmtime, shape::CodeOf(&StandGmtime)};
    shape::CodePatch localtimePatch{kImageLocaltime, shape::CodeOf(&StandLocaltime)};
};

// ---- the import fakes ----

uint32_t gThreadId = 0;
DWORD WINAPI FakeThreadId()
{
    return gThreadId;
}

// GetProcessTimes: logs the handle, writes the four times from the script (advancing per call).
uint64_t gTimesBase = 0;
int gTimesCalls = 0;
BOOL WINAPI FakeProcessTimes(HANDLE process, LPFILETIME creation, LPFILETIME exit, LPFILETIME kernel, LPFILETIME user)
{
    Log("GetProcessTimes(%p)", process);
    const uint64_t k = gTimesBase + 0x1234567ull * static_cast<uint64_t>(gTimesCalls);
    const uint64_t u = (gTimesBase >> 3) + 0x89abcull * static_cast<uint64_t>(gTimesCalls);
    ++gTimesCalls;
    creation->dwLowDateTime = 0x11111111;
    creation->dwHighDateTime = 0x22222222;
    exit->dwLowDateTime = 0x33333333;
    exit->dwHighDateTime = 0x44444444;
    kernel->dwLowDateTime = static_cast<DWORD>(k);
    kernel->dwHighDateTime = static_cast<DWORD>(k >> 32);
    user->dwLowDateTime = static_cast<DWORD>(u);
    user->dwHighDateTime = static_cast<DWORD>(u >> 32);
    return TRUE;
}

struct Stamp {
    alignas(8) uint8_t bytes[cLogTimestamp::kSize];
    cLogTimestamp *Get() { return reinterpret_cast<cLogTimestamp *>(bytes); }
};

} // namespace

// ---- the time stamp helpers ------------------------------------------------------------------------------------

DIFF_TEST(GZLog_TimestampCopyFrom, kCopyFrom)
{
    Words words(ctx);
    SetStopwatchScales(words);
    Clocks clocks;
    ClockScope scope(ctx, clocks);
    auto copy = ctx.Original<void(__thiscall *)(void *, const void *)>(kCopyFrom);
    auto elapsed = ctx.Original<uint32_t(__thiscall *)(void *)>(kElapsed);
    std::mt19937 rng(kCopyFrom);
    for (int i = 0; i < 3000; ++i) {
        Stamp a, b, source;
        for (size_t k = 0; k < cLogTimestamp::kSize; ++k) {
            a.bytes[k] = b.bytes[k] = static_cast<uint8_t>(rng());
            source.bytes[k] = static_cast<uint8_t>(rng());
        }
        // Mode 0 reads the time-stamp counter directly in the original: modes 1..7 only.
        const int32_t mode = 1 + static_cast<int32_t>(rng() % 7);
        a.Get()->mStopwatch.mMode = b.Get()->mStopwatch.mMode = mode;
        if (rng() % 3 == 0) source.Get()->mElapsedLow = 0x80000000u | static_cast<uint32_t>(rng()); // negative as a signed count
        clocks.counter = {rng() % 0x100000000ull, (rng() % 0x100000000ull) << 4, rng()};
        clocks.Rewind();
        copy(a.bytes, source.bytes);
        uint32_t ra = elapsed(a.bytes);
        clocks.Rewind();
        b.Get()->CopyFrom(source.Get());
        uint32_t rb = b.Get()->ElapsedMilliseconds();
        ++ctx.cases;
        int at = FirstDifference(a.bytes, b.bytes, cLogTimestamp::kSize);
        if (at >= 0) ctx.Fail("mode %d, start %08x: bytes differ at 0x%x", mode, source.Get()->mElapsedLow, at);
        if (ra != rb) ctx.Fail("mode %d, start %08x: elapsed %u vs %u", mode, source.Get()->mElapsedLow, ra, rb);
        if (b.Get()->mTime != source.Get()->mTime || b.Get()->mElapsedLow != source.Get()->mElapsedLow) ctx.Fail("fields not copied");
    }
}

DIFF_TEST(GZLog_ProcessInfoTimes, kSample)
{
    Slots slots(ctx);
    slots.Set(kIatGetProcessTimes, reinterpret_cast<void *>(&FakeProcessTimes));
    auto sample = ctx.Original<void(__thiscall *)(void *)>(kSample);
    auto cpu = ctx.Original<uint32_t(__thiscall *)(void *)>(kCpuTime);
    std::mt19937 rng(kSample);
    for (int i = 0; i < 3000; ++i) {
        Pair p(cLogProcessInfo::kSize, rng);
        const uint8_t has = i % 4 == 0 ? 0 : (i % 4 == 1 ? 1 : static_cast<uint8_t>(rng()));
        int64_t scaled = static_cast<int64_t>(rng() % 64 + 1) * 10000;
        if (i % 7 == 0) scaled = static_cast<int64_t>((static_cast<uint64_t>(rng()) << 32) | rng()) | 1; // above 32 bits, or negative
        gTimesBase = (static_cast<uint64_t>(rng()) << (rng() % 33)) ^ rng();
        for (uint8_t *o : {p.a, p.b}) {
            auto *info = reinterpret_cast<cLogProcessInfo *>(o);
            info->mHasProcessTimes = has;
            info->mScaled = scaled;
        }
        gLog.clear();
        gTimesCalls = 0;
        sample(p.a);
        uint32_t ra = cpu(p.a);
        std::string logA = Joined();
        gLog.clear();
        gTimesCalls = 0;
        reinterpret_cast<cLogProcessInfo *>(p.b)->Sample();
        uint32_t rb = reinterpret_cast<cLogProcessInfo *>(p.b)->CpuTime();
        std::string logB = Joined();
        ++ctx.cases;
        if (logA != logB) ctx.Fail("flag %u: calls %s / %s", has, logA.c_str(), logB.c_str());
        if (FirstDifference(p.a, p.b, cLogProcessInfo::kSize) >= 0 || !p.CanariesIntact()) ctx.Fail("flag %u: bytes differ", has);
        if (ra != rb) ctx.Fail("flag %u, scale %llx: cpu %u vs %u", has, static_cast<unsigned long long>(scaled), ra, rb);
    }
}

// ---- the record --------------------------------------------------------------------------------------------------

namespace {

std::string Texts(const cLogRecord *r)
{
    char head[64];
    std::snprintf(head, sizeof(head), "%zu/%zu %zu/%zu ", r->mText.mString.size(), r->mText.mString.capacity(), r->mText2.mString.size(),
                  r->mText2.mString.capacity());
    return head + std::string(r->mText.mString.c_str()) + "|" + r->mText2.mString.c_str();
}

std::string RandomText(std::mt19937 &rng)
{
    static const size_t kLengths[] = {0, 1, 15, 16, 31, 40};
    return std::string(kLengths[rng() % 6], static_cast<char>('a' + rng() % 26));
}

} // namespace

DIFF_TEST(GZLog_RecordSet, kRecordSet)
{
    using SetFn = void(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                     uint32_t);
    auto original = ctx.Original<SetFn>(kRecordSet);
    std::mt19937 rng(kRecordSet);
    for (int i = 0; i < 2000; ++i) {
        Pair p(cLogRecord::kSize, rng);
        auto *a = reinterpret_cast<cLogRecord *>(p.a);
        auto *b = reinterpret_cast<cLogRecord *>(p.b);
        a->Construct();
        b->Construct();
        const std::string t1 = RandomText(rng), t2 = RandomText(rng);
        a->mText.mString = t1;
        b->mText.mString = t1;
        a->mText2.mString = t2;
        b->mText2.mString = t2;
        uint32_t v[11];
        for (uint32_t &x : v) x = rng();
        original(a, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10]);
        b->Set(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10]);
        ++ctx.cases;
        // The strings' bytes hold heap pointers that differ: compared by content and size below.
        if (FirstDifference(p.a, p.b, cLogRecord::kSize, {{0x14, 0x18}, {0x38, 0x18}}) >= 0 || !p.CanariesIntact()) ctx.Fail("bytes differ");
        if (Texts(a) != Texts(b)) ctx.Fail("strings %s / %s", Texts(a).c_str(), Texts(b).c_str());
        if (b->mLevel != v[0] || b->mGroup != v[1] || b->mMessage != v[2] || b->mThreadId != v[3] || b->mNumber != v[4] || b->mTime != v[5] ||
            b->mCpuTime != v[6] || b->mField60 != v[7] || b->mFrame != v[8] || b->mFile != v[9] || b->mLine != v[10])
            ctx.Fail("fields not where the getters read them");
        a->Destruct();
        b->Destruct();
    }
}

// ---- FormatText ----------------------------------------------------------------------------------------------------

namespace {

using FormatFn = char *(__cdecl *)(char *, uint32_t, const char *, va_list);

char *CallFormat(FormatFn fn, char *buffer, uint32_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    char *r = fn(buffer, size, format, args);
    va_end(args);
    return r;
}

// The text a result holds: up to `size` characters of the buffer, or the heap block's whole size.
std::string Held(const char *result, const char *buffer, uint32_t size)
{
    if (!result) return "(null)";
    if (result == buffer) return "buffer:" + std::string(buffer, strnlen(buffer, size));
    const size_t block = _msize(const_cast<char *>(result));
    // Up to the terminator (the rest of a malloc block is uninitialised), or the whole block when the given-up attempt
    // is unterminated.
    const size_t text = strnlen(result, block);
    return "block " + std::to_string(block) + (text == block ? " unterminated:" : ":") + std::string(result, text);
}

} // namespace

DIFF_TEST(GZLog_FormatText, kFormatText)
{
    auto original = ctx.Original<FormatFn>(kFormatText);
    std::mt19937 rng(kFormatText);
    int grown = 0, cut = 0, freeing = 0, edge = 0;
    for (int i = 0; i < 1500; ++i) {
        // 0x400 is the write path's size; 3125 doubles to exactly 100000 (the give-up bound).
        uint32_t size = i % 5 == 0 ? 0x400 : i % 25 == 1 ? 3125 : 1 + rng() % 48;
        size_t length;
        switch (rng() % 6) {
        case 0: length = size - 1; break;
        case 1: length = size; break;
        case 2: length = size + 1; break;
        case 3: length = size * 2 + rng() % 7; break;
        case 4: length = rng() % (size + 1); break;
        default: length = rng() % 3000; break;
        }
        if (i % 300 == 7) length = 70000 + rng() % 30000; // past the last block size: the loop gives up
        if (i % 300 == 8) length = 40000;
        if (size == 3125) length = 50000 + rng() % 60000; // doubled to 100000: given up there (at, not past, the bound)
        const std::string text(length, static_cast<char>('A' + rng() % 26));
        const int number = static_cast<int>(rng());
        std::vector<char> bufferA(size + 8, '\x5a'), bufferB(size + 8, '\x5a');
        difftest::FreeRecorder freesA;
        char *ra = CallFormat(original, bufferA.data(), size, "%s#%d", text.c_str(), number);
        freesA.Stop();
        difftest::FreeRecorder freesB;
        char *rb = CallFormat(&FormatText, bufferB.data(), size, "%s#%d", text.c_str(), number);
        freesB.Stop();
        ++ctx.cases;
        const std::string ha = Held(ra, bufferA.data(), size), hb = Held(rb, bufferB.data(), size);
        if (ha != hb) ctx.Fail("size %u, length %zu: %.60s / %.60s", size, length, ha.c_str(), hb.c_str());
        if (bufferA != bufferB) ctx.Fail("size %u, length %zu: buffers differ", size, length);
        size_t sa[64], sb[64];
        const size_t na = freesA.SortedSizes(sa, 64), nb = freesB.SortedSizes(sb, 64);
        if (na != nb || !std::equal(sa, sa + na, sb)) ctx.Fail("size %u, length %zu: frees %zu / %zu", size, length, na, nb);
        if (na > 1) ++freeing;
        if (size == 3125 && length >= 50000) ++edge;
        if (ra != bufferA.data()) {
            ++grown;
            std::free(ra);
        }
        if (rb != bufferB.data()) std::free(rb);
        if (length >= 70000) ++cut;
    }
    if (grown < 300 || cut < 5 || freeing < 100 || edge < 5) ctx.Fail("only %d grown, %d cut, %d freeing, %d at the bound", grown, cut, freeing, edge);
}

// ---- the time-stamped file name (slot 23) ----------------------------------------------------------------------

DIFF_TEST(GZLog_ServiceTimestampedName, kTimestampedName)
{
    Crt crt;
    Words words(ctx);
    SetStopwatchScales(words);
    Clocks clocks;
    ClockScope scope(ctx, clocks);
    auto original = ctx.Original<void(__thiscall *)(void *, cIGZString *)>(kTimestampedName);
    std::mt19937 rng(kTimestampedName);
    static const char *const kNames[] = {"log.txt", "a.b.c", "noext", ".hidden", "x.", "", "dir/file.name.log", "a", "ab.c",
                                         "a long file name that is not small.extension", "..", "z.z"};
    for (int i = 0; i < 1200; ++i) {
        // The service's time stamp (both sides read the same one): a start reading and a wall-clock time in range.
        Stamp source;
        for (uint8_t &x : source.bytes) x = static_cast<uint8_t>(rng());
        source.Get()->mTime = 0x40000000u + static_cast<uint32_t>(rng() % 0x30000000u);
        source.Get()->mElapsedLow = rng() % 3 == 0 ? static_cast<uint32_t>(rng()) : static_cast<uint32_t>(rng() % 100000000);
        Pair p(cLogService::kSize, rng);
        for (uint8_t *o : {p.a, p.b}) reinterpret_cast<cLogService *>(o)->mTimestamp = source.Get();
        const std::string name = kNames[rng() % (sizeof(kNames) / sizeof(kNames[0]))];
        cRZString na, nb;
        na.Construct(name.c_str());
        nb.Construct(name.c_str());
        clocks.fileTime = 0x01d0000000000000ull + (static_cast<uint64_t>(rng()) << 20);
        clocks.counter = {static_cast<uint64_t>(rng()), static_cast<uint64_t>(rng()) << 8};
        clocks.Rewind();
        original(p.a + kSub, na.AsInterface());
        clocks.Rewind();
        ServiceTimestampedName(p.b + kSub, nullptr, nb.AsInterface());
        ++ctx.cases;
        if (na.mString != nb.mString) ctx.Fail("'%s': '%s' / '%s'", name.c_str(), na.mString.c_str(), nb.mString.c_str());
        if (FirstDifference(p.a, p.b, cLogService::kSize) >= 0 || !p.CanariesIntact()) ctx.Fail("service bytes changed");
        if (na.mString.size() < name.size()) ctx.Fail("'%s': result '%s' too short", name.c_str(), na.mString.c_str());
        na.Destruct();
        nb.Destruct();
    }
}

// ---- the write path (slot 28) ------------------------------------------------------------------------------------

namespace {

using WriteFn = void(__thiscall *)(void *, const char *, uint32_t, const char *, int32_t, const char *, va_list);
WriteFn gWrite = nullptr; // the side's write path, for re-entry from an output

struct Script {
    uint8_t running = 1;
    uint8_t enabled = 1;
    int reenter = -1;  // the output (index) whose Output re-enters the write path once
    int throwAt = -1;  // the output (index) whose Accept (even case) or Output (odd) throws
    int throwKind = 0; // 0: Accept throws, 1: Output throws
    std::vector<uint8_t> accept;
    std::vector<uint32_t> last; // what each output's slot 22 answers (its last time)
    int depth = 0;
} gScript;

struct FakeOut {
    const void *vtable;
    int id;
    uint32_t last;
};

const char *TextOf(uint32_t p)
{
    return p ? reinterpret_cast<const char *>(static_cast<uintptr_t>(p)) : "(null)";
}

std::string RecordText(const cLogRecord *r, bool full)
{
    char buf[300];
    std::snprintf(buf, sizeof(buf), "level %u group '%s' msg '%.40s' (len %zu) tid %x no %u time %u cpu %u f60 %u frame %u file '%s' line %u since %u",
                  r->mLevel, TextOf(r->mGroup), TextOf(r->mMessage), std::strlen(TextOf(r->mMessage)), r->mThreadId, r->mNumber, r->mTime,
                  r->mCpuTime, r->mField60, r->mFrame, TextOf(r->mFile), r->mLine, full ? r->mSinceLast : 0);
    return buf;
}

uint8_t __fastcall OutAccept(FakeOut *self, void *, cLogRecord *record)
{
    Log("out%d.accept(%s)", self->id, RecordText(record, false).c_str());
    if (gScript.throwAt == self->id && gScript.throwKind == 0) {
        gScript.throwAt = -1;
        throw 3;
    }
    return gScript.accept[self->id];
}
uint32_t __fastcall OutCount(FakeOut *, void *) { return 1; }
uint32_t __fastcall OutLast(FakeOut *self, void *)
{
    Log("out%d.last -> %u", self->id, self->last);
    return self->last;
}
void __fastcall OutSetLast(FakeOut *self, void *, uint32_t time)
{
    Log("out%d.setlast(%u)", self->id, time);
    self->last = time;
}
void ReenterVia(void *sub, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    gWrite(sub, "nested.cpp", 77, "Nested", 3, format, args);
    va_end(args);
}
void *gSub = nullptr;
void __fastcall OutOutput(FakeOut *self, void *, cLogRecord *record)
{
    Log("out%d.output(%s)", self->id, RecordText(record, true).c_str());
    if (gScript.throwAt == self->id && gScript.throwKind == 1) {
        gScript.throwAt = -1;
        throw 4;
    }
    if (gScript.reenter == self->id && gScript.depth < 3) {
        ++gScript.depth;
        Log("reenter");
        ReenterVia(gSub, "inner %d", gScript.depth);
        Log("back");
        --gScript.depth;
    }
}

uint32_t __fastcall LockFake(void *self, void *) { Log("lock(%+d)", Rel(self)); return 1; }
uint32_t __fastcall UnlockFake(void *self, void *) { Log("unlock(%+d)", Rel(self)); return 0; }
uint8_t __fastcall RunningFake(void *self, void *) { Log("running?(%+d)", Rel(self)); return gScript.running; }
uint8_t __fastcall EnabledFake(void *self, void *, const char *group, int32_t level)
{
    Log("enabled?(%+d, '%s', %d)", Rel(self), group, level);
    return gScript.enabled;
}
uint32_t __fastcall FlushFake(void *self, void *) { Log("flush(%+d)", Rel(self)); return 9; }
void __fastcall WriteFake(void *self, void *, const char *file, uint32_t line, const char *group, int32_t level, const char *format, va_list args)
{
    Log("write(%+d)", Rel(self));
    gWrite(self, file, line, group, level, format, args);
}

void *gOutVTable[24], *gLockVTable[4], *gSvcVTable[8], *gSubVTable[32];

struct Setup {
    Setup()
    {
        gOutVTable[1] = reinterpret_cast<void *>(&OutCount); // the list's holders count their outputs
        gOutVTable[2] = reinterpret_cast<void *>(&OutCount);
        gOutVTable[4] = reinterpret_cast<void *>(&OutAccept);
        gOutVTable[5] = reinterpret_cast<void *>(&OutOutput);
        gOutVTable[22] = reinterpret_cast<void *>(&OutLast);
        gOutVTable[23] = reinterpret_cast<void *>(&OutSetLast);
        gLockVTable[1] = reinterpret_cast<void *>(&LockFake);
        gLockVTable[2] = reinterpret_cast<void *>(&UnlockFake);
        gSvcVTable[6] = reinterpret_cast<void *>(&RunningFake);
        gSubVTable[27] = reinterpret_cast<void *>(&EnabledFake);
        gSubVTable[28] = reinterpret_cast<void *>(&WriteFake);
        gSubVTable[29] = reinterpret_cast<void *>(&FlushFake);
    }
} gSetup;

// One side's service: a random block with the fakes, real name table and nesting map, its own process probe and
// time stamp (same bytes on both sides).
struct Side {
    std::vector<uint8_t> store;
    uint8_t *block;
    std::deque<FakeOut> outs;
    alignas(8) uint8_t info[cLogProcessInfo::kSize];
    Stamp stamp;
    cLogService *S() { return reinterpret_cast<cLogService *>(block); }
    Side(const std::vector<uint8_t> &bytes) : store(bytes.size() + 16)
    {
        block = store.data() + 8 - reinterpret_cast<uintptr_t>(store.data()) % 8;
        std::memcpy(block, bytes.data(), bytes.size());
        *reinterpret_cast<void **>(block) = gSvcVTable;
        *reinterpret_cast<void **>(block + kSub) = gSubVTable;
        for (size_t off : {offsetof(cLogService, mLock28), offsetof(cLogService, mLock90), offsetof(cLogService, mLockCC)})
            *reinterpret_cast<void **>(block + off) = gLockVTable;
        new (&S()->mNames) NameSet();
        new (&S()->mOutputs) std::vector<RefHolder>();
        new (&S()->mNesting) std::map<uint32_t, uint32_t>();
        S()->mProcessInfo = reinterpret_cast<cLogProcessInfo *>(info);
        S()->mTimestamp = stamp.Get();
    }
    ~Side()
    {
        S()->mNames.~NameSet();
        S()->mOutputs.~vector();
        S()->mNesting.~map();
    }
    std::string Names()
    {
        std::set<std::string> sorted;
        for (const LogString &n : S()->mNames) sorted.insert(n.mString);
        std::string s = std::to_string(S()->mNames.size()) + ":";
        for (const std::string &n : sorted) s += n + ",";
        return s;
    }
    std::string Nesting()
    {
        std::string s;
        for (auto &e : S()->mNesting) s += std::to_string(e.first) + "=" + std::to_string(e.second) + ",";
        return s;
    }
};

void CallWrite(WriteFn fn, void *sub, const char *file, uint32_t line, const char *group, int32_t level, bool withArgs, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    fn(sub, file, line, group, level, format, withArgs ? args : nullptr);
    va_end(args);
}

void __fastcall OurWrite(void *sub, void *, const char *file, uint32_t line, const char *group, int32_t level, const char *format, va_list args)
{
    ServiceWrite(sub, nullptr, file, line, group, level, format, args);
}

} // namespace

DIFF_TEST(GZLog_ServiceWrite, kWrite)
{
    Words words(ctx);
    SetStopwatchScales(words);
    Clocks clocks;
    ClockScope scope(ctx, clocks);
    Slots slots(ctx);
    slots.Set(kIatGetCurrentThreadId, reinterpret_cast<void *>(&FakeThreadId));
    slots.Set(kIatGetProcessTimes, reinterpret_cast<void *>(&FakeProcessTimes));
    auto original = ctx.Original<WriteFn>(kWrite);
    WriteFn ours = reinterpret_cast<WriteFn>(&OurWrite);
    std::mt19937 rng(kWrite);
    static const char *const kGroups[] = {"Alpha", "beta", "Gamma", "Alpha", ""};
    int written = 0, reentered = 0, thrown = 0, nested = 0;
    for (int i = 0; i < 1500; ++i) {
        std::vector<uint8_t> bytes(cLogService::kSize);
        for (uint8_t &x : bytes) x = static_cast<uint8_t>(rng());
        Side a(bytes), b(bytes);
        const int outputs = static_cast<int>(rng() % 4);
        std::vector<uint32_t> lasts;
        for (int k = 0; k < outputs; ++k) lasts.push_back(rng() % 3 == 0 ? 0 : static_cast<uint32_t>(rng() % 5000));
        for (Side *s : {&a, &b}) {
            for (int k = 0; k < outputs; ++k) s->outs.push_back({gOutVTable, k, lasts[k]});
            for (FakeOut &o : s->outs) {
                void *object = &o;
                s->S()->mOutputs.push_back(reinterpret_cast<const RefHolder &>(object));
            }
        }
        // The process probe and the time stamp: the same bytes on both sides.
        uint8_t infoBytes[cLogProcessInfo::kSize], stampBytes[cLogTimestamp::kSize];
        for (uint8_t &x : infoBytes) x = static_cast<uint8_t>(rng());
        for (uint8_t &x : stampBytes) x = static_cast<uint8_t>(rng());
        reinterpret_cast<cLogProcessInfo *>(infoBytes)->mHasProcessTimes = static_cast<uint8_t>(rng() % 2);
        reinterpret_cast<cLogProcessInfo *>(infoBytes)->mScaled = static_cast<int64_t>(1 + rng() % 16) * 10000;
        reinterpret_cast<cLogTimestamp *>(stampBytes)->mStopwatch.mMode = 1 + static_cast<int32_t>(rng() % 6);
        std::memcpy(a.info, infoBytes, sizeof(infoBytes));
        std::memcpy(b.info, infoBytes, sizeof(infoBytes));
        std::memcpy(a.stamp.bytes, stampBytes, sizeof(stampBytes));
        std::memcpy(b.stamp.bytes, stampBytes, sizeof(stampBytes));

        const uint32_t me = 0x7000 + rng() % 4;
        const uint32_t outputThread = rng() % 6 == 0 ? me : 0x7000 + rng() % 4;
        const uint32_t constructing = rng() % 2 ? me : 0x7100;
        const uint8_t collect = static_cast<uint8_t>(rng() % 3 ? rng() % 256 : 0);
        const bool preset = rng() % 2 == 0;
        for (Side *s : {&a, &b}) {
            s->S()->mOutputThreadId = outputThread;
            s->S()->mThreadId = constructing;
            s->S()->mCollectGroups = collect;
            if (preset) s->S()->mNesting[me] = 0;
        }
        if (rng() % 4 == 0) {
            const uint32_t pre = rng() % 3;
            a.S()->mNesting[me] = pre;
            b.S()->mNesting[me] = pre;
        }
        const uint32_t maxNesting = rng() % 5 == 0 ? 0 : 1 + rng() % 4;
        words.Set(cLogService::kMaxNestingRva, maxNesting);

        Script script;
        script.running = static_cast<uint8_t>(rng() % 8 ? 1 + rng() % 255 : 0);
        script.enabled = static_cast<uint8_t>(rng() % 6 ? 1 + rng() % 255 : 0);
        script.accept.resize(outputs);
        for (uint8_t &x : script.accept) x = static_cast<uint8_t>(rng() % 3 ? 1 + rng() % 255 : 0);
        script.reenter = outputs && rng() % 4 == 0 ? static_cast<int>(rng() % outputs) : -1;
        if (outputs && rng() % 5 == 0) {
            script.throwAt = static_cast<int>(rng() % outputs);
            script.throwKind = static_cast<int>(rng() % 2);
        }
        const char *group = kGroups[rng() % 5];
        const int32_t level = static_cast<int32_t>(rng() % 9) - 1;
        const bool withArgs = rng() % 3 != 0;
        const std::string longText(rng() % 3 == 0 ? 0x3f8 + rng() % 16 : rng() % 30, 'q');
        const char *file = rng() % 2 ? "file.cpp" : nullptr;
        const uint32_t line = rng();
        gThreadId = me;
        gTimesBase = static_cast<uint64_t>(rng()) << (rng() % 20);
        clocks.counter = {rng() % 100000, 100000 + rng() % 100000, 300000 + rng() % 100000, 500000};

        std::string logs[2], frees[2];
        Side *sides[2] = {&a, &b};
        for (int side = 0; side < 2; ++side) {
            Side &s = *sides[side];
            gScript = script;
            gWrite = side == 0 ? original : ours;
            gSub = s.block + kSub;
            gBase = s.block;
            gLog.clear();
            gTimesCalls = 0;
            clocks.Rewind();
            difftest::FreeRecorder recorder;
            CallWrite(gWrite, s.block + kSub, file, line, group, level, withArgs, "%s<%d>", longText.c_str(), 42);
            recorder.Stop();
            logs[side] = Joined();
            size_t sizes[64];
            const size_t n = recorder.SortedSizes(sizes, 64);
            for (size_t k = 0; k < n; ++k) frees[side] += std::to_string(sizes[k]) + " ";
        }
        ++ctx.cases;
        if (logs[0] != logs[1]) {
            size_t at = 0;
            while (at < logs[0].size() && at < logs[1].size() && logs[0][at] == logs[1][at]) ++at;
            const size_t from = at > 60 ? at - 60 : 0;
            ctx.Fail("case %d: calls differ at %zu: ...%.160s / ...%.160s", i, at, logs[0].c_str() + from, logs[1].c_str() + from);
        }
        if (frees[0] != frees[1]) ctx.Fail("case %d: frees %s / %s", i, frees[0].c_str(), frees[1].c_str());
        // The containers' and the two objects' pointers differ by side; their contents are compared below.
        const std::vector<std::pair<size_t, size_t>> skip = {{0x30, 0x20}, {0x60, 0xc}, {0xc4, 8}, {0xd4, 8}};
        int at = FirstDifference(a.block, b.block, cLogService::kSize, skip);
        if (at >= 0) ctx.Fail("case %d: service bytes differ at 0x%x", i, at);
        if (std::memcmp(a.info, b.info, sizeof(a.info)) != 0) ctx.Fail("case %d: process probes differ", i);
        if (std::memcmp(a.stamp.bytes, b.stamp.bytes, sizeof(a.stamp.bytes)) != 0) ctx.Fail("case %d: time stamps differ", i);
        if (a.Names() != b.Names()) ctx.Fail("case %d: names %s / %s", i, a.Names().c_str(), b.Names().c_str());
        if (a.Nesting() != b.Nesting()) ctx.Fail("case %d: nesting %s / %s", i, a.Nesting().c_str(), b.Nesting().c_str());
        for (int k = 0; k < outputs; ++k)
            if (a.outs[k].last != b.outs[k].last) ctx.Fail("case %d: output %d last time %u / %u", i, k, a.outs[k].last, b.outs[k].last);
        if (logs[0].find(".output(") != std::string::npos) ++written;
        if (logs[0].find("reenter") != std::string::npos) ++reentered;
        if (logs[0].find("back") != std::string::npos && logs[0].find("accept(level 3 group 'Nested'") != std::string::npos) ++nested;
        if (script.throwAt >= 0 && logs[0].find(".accept(") != std::string::npos) ++thrown;
    }
    if (written < 300 || reentered < 40 || nested < 15 || thrown < 40) ctx.Fail("coverage: %d written, %d re-entered, %d nested, %d thrown", written, reentered, nested, thrown);
}
