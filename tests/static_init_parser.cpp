// Differential tests for lane HP's statics (src/app/StaticInitParser.{h,cpp}): XC slot 675 (the evaluator's name
// table at 0x10fb30c) and slot 773 (cGZLogParser at 0x1107510), and the flag constructor 0x90a915.
//
// Each entry against the image's initialiser in its slot, from junk in the object and 8 bytes on each side: the
// registration (exactly one thunk: the image's own for the image, ours for ours), the object as built, then the
// registered thunk run, the object afterwards and the blocks freed. For slot 773 the parser's virtual calls go to
// recorders (tests/gz_log_parser_fakes.h), so the built-in commands are not registered and the parser outlives the
// thunk; it is compared with ParserForm and freed by the test.
#include "difftest.h"
#include "free_recorder.h"
#include "gz_base_parser_form.h"
#include "gz_log_parser_fakes.h"
#include "app/StaticInit.h"
#include "app/StaticInitParser.h"
#include "gz/EvalFunctionTable.h"
#include "gz/cGZLogParser.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <random>
#include <vector>

using nGZCommandParser::cBaseParser;
using nGZCommandParser::cGZLogParser;
using nGZCommandParser::EvalFunctionTable;
using staticinit::ParserStatic;

namespace {

constexpr uint32_t kAtExit = 0x824cbe;
constexpr size_t kMargin = 8;

using Thunk = void(__cdecl *)();
std::vector<Thunk> gRegistered;
int __cdecl RecordAtExit(Thunk f)
{
    gRegistered.push_back(f);
    return 0;
}

// A 5-byte jmp over an image function's entry for the duration of a test.
struct Redirect {
    uint8_t *site;
    uint8_t saved[5];
    Redirect(const difftest::Context &ctx, uint32_t rva, const void *target) : site(ctx.image.At<uint8_t>(rva))
    {
        std::memcpy(saved, site, 5);
        DWORD old;
        VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old);
        int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(site) + 5));
        site[0] = 0xE9;
        std::memcpy(site + 1, &rel, 4);
    }
    ~Redirect() { std::memcpy(site, saved, 5); }
};

// Both sides' atexit registrations go to gRegistered: the image's atexit redirected, ours bound.
struct AtExitCapture {
    Redirect image;
    staticinit::RegisterAtExit saved;
    explicit AtExitCapture(const difftest::Context &ctx)
        : image(ctx, kAtExit, reinterpret_cast<const void *>(&RecordAtExit)), saved(staticinit::BoundAtExit())
    {
        staticinit::BindAtExit(&RecordAtExit);
        gRegistered.clear();
    }
    ~AtExitCapture() { staticinit::BindAtExit(saved); }
};

const ParserStatic &Row(difftest::Context &ctx, uint32_t slot)
{
    size_t n = 0;
    const ParserStatic *rows = staticinit::ParserStatics(&n);
    for (size_t i = 0; i < n; ++i)
        if (rows[i].slot == slot) return rows[i];
    ctx.Fail("no row for slot %u", slot);
    return rows[0];
}

uint8_t *Window(difftest::Context &ctx, const ParserStatic &e) { return ctx.image.At<uint8_t>(e.storage) - kMargin; }

// Runs one side's entry from `start` (the window's bytes); returns the one registered thunk (or null, failing).
Thunk RunEntry(difftest::Context &ctx, const ParserStatic &e, bool image, const std::vector<uint8_t> &start)
{
    std::memcpy(Window(ctx, e), start.data(), start.size());
    gRegistered.clear();
    if (image) ctx.Original<void(__cdecl *)()>(e.imageRva)();
    else staticinit::InitParserStatic(&e);
    if (gRegistered.size() != 1) {
        ctx.Fail("slot %u %s: %zu thunks registered (1)", e.slot, image ? "image" : "ours", gRegistered.size());
        return nullptr;
    }
    const Thunk expected = reinterpret_cast<Thunk>(ctx.image.At<uint8_t>(e.thunk));
    if (image != (gRegistered[0] == expected)) ctx.Fail("slot %u %s: registered %p", e.slot, image ? "image" : "ours", gRegistered[0]);
    return gRegistered[0];
}

std::vector<uint8_t> Bytes(const uint8_t *p, size_t n) { return std::vector<uint8_t>(p, p + n); }

// The name table: the margins as bytes, size, capacity and the entries; after the thunk, the window as bytes.
std::vector<uint8_t> EvalForm(const uint8_t *window)
{
    constexpr size_t kObject = sizeof(EvalFunctionTable);
    std::vector<uint8_t> out(window, window + kMargin);
    out.insert(out.end(), window + kMargin + kObject, window + 2 * kMargin + kObject);
    const auto *t = reinterpret_cast<const EvalFunctionTable *>(window + kMargin);
    const size_t size = t->mEntries.size(), capacity = t->mEntries.capacity();
    parserform::Append(out, static_cast<uint32_t>(size));
    parserform::Append(out, static_cast<uint32_t>(capacity));
    if (size < 100) out.insert(out.end(), reinterpret_cast<const uint8_t *>(t->mEntries.data()),
                               reinterpret_cast<const uint8_t *>(t->mEntries.data() + size));
    return out;
}

} // namespace

DIFF_TEST(StaticInitParser_EvalTable, 0x13ff80)
{
    const ParserStatic &e = Row(ctx, 675);
    const size_t windowSize = sizeof(EvalFunctionTable) + 2 * kMargin;
    AtExitCapture capture(ctx);
    std::mt19937 gen(0x13ff80);
    const std::vector<uint8_t> saved = Bytes(Window(ctx, e), windowSize);
    for (int round = 0; round < 10; ++round) {
        std::vector<uint8_t> start(windowSize);
        for (auto &c : start) c = static_cast<uint8_t>(gen());
        std::vector<uint8_t> built[2], after[2];
        size_t sizes[2][4] = {}, counts[2] = {};
        for (int side = 0; side < 2; ++side) {
            Thunk thunk = RunEntry(ctx, e, side == 0, start);
            built[side] = EvalForm(Window(ctx, e));
            if (!thunk) continue;
            difftest::FreeRecorder rec;
            thunk();
            rec.Stop();
            counts[side] = rec.SortedSizes(sizes[side], 4);
            after[side] = Bytes(Window(ctx, e), windowSize);
        }
        ++ctx.cases;
        if (built[0] != built[1]) ctx.Fail("round %d: the tables differ", round);
        if (after[0] != after[1]) ctx.Fail("round %d: the windows after the thunk differ", round);
        if (counts[0] != 1 || counts[0] != counts[1] || std::memcmp(sizes[0], sizes[1], sizeof(sizes[0])) != 0)
            ctx.Fail("round %d: %zu / %zu blocks freed (1), or other sizes", round, counts[0], counts[1]);
    }
    std::memcpy(Window(ctx, e), saved.data(), saved.size());
}

DIFF_TEST(StaticInitParser_LogParser, 0x1409f0)
{
    using namespace logfakes;
    const ParserStatic &e = Row(ctx, 773);
    const size_t windowSize = sizeof(cGZLogParser) + 2 * kMargin;
    AtExitCapture capture(ctx);
    PatchedParserVTables patched(ctx);
    std::mt19937 gen(0x1409f0);
    uint8_t *window = Window(ctx, e);
    auto *log = reinterpret_cast<cGZLogParser *>(window + kMargin);
    const std::vector<uint8_t> saved = Bytes(window, windowSize);
    for (int round = 0; round < 10; ++round) {
        std::vector<uint8_t> start(windowSize);
        for (auto &c : start) c = static_cast<uint8_t>(gen());
        std::vector<uint32_t> calls[2], exitCalls[2];
        std::vector<uint8_t> state[2], parser[2], margins[2];
        size_t sizes[2][4] = {}, counts[2] = {};
        for (int side = 0; side < 2; ++side) {
            gCalls.clear();
            Thunk thunk = RunEntry(ctx, e, side == 0, start);
            cBaseParser *built = log->mParser;
            const void *builtState = log->mState;
            calls[side] = Normal(gCalls, built, builtState);
            margins[side] = Bytes(window, kMargin);
            margins[side].insert(margins[side].end(), window + kMargin + sizeof(cGZLogParser), window + windowSize);
            if (!built || !builtState) {
                ctx.Fail("round %d side %d: no parser or no state", round, side);
                continue;
            }
            state[side] = StateForm(log->mState);
            // A heap block per side: the bytes the constructor leaves alone are left out.
            parser[side] = parserform::ParserForm(reinterpret_cast<const uint8_t *>(built), sizeof(cBaseParser), true);
            if (thunk) {
                gCalls.clear();
                difftest::FreeRecorder rec;
                thunk();
                rec.Stop();
                counts[side] = rec.SortedSizes(sizes[side], 4);
                exitCalls[side] = Normal(gCalls, built, builtState);
                if (log->mParser != nullptr) ctx.Fail("round %d side %d: the reference is kept", round, side);
            }
            parserform::TearDownParser(*built);
            ::operator delete(built);
        }
        ++ctx.cases;
        if (calls[0].size() != 16 * kWordsPerCall) ctx.Fail("round %d: the image's entry made %zu calls (16)", round, calls[0].size() / kWordsPerCall);
        if (calls[0] != calls[1]) ctx.Fail("round %d: the construction calls differ", round);
        if (margins[0] != margins[1]) ctx.Fail("round %d: the margins differ", round);
        if (state[0] != state[1]) ctx.Fail("round %d: the states differ", round);
        if (parser[0] != parser[1]) ctx.Fail("round %d: the parsers differ (from byte %ld)", round, parserform::FirstDiff(parser[0], parser[1]));
        if (exitCalls[0].size() != 2 * kWordsPerCall || exitCalls[0] != exitCalls[1]) ctx.Fail("round %d: the thunks' calls differ", round);
        if (counts[0] != 1 || counts[0] != counts[1] || std::memcmp(sizes[0], sizes[1], sizeof(sizes[0])) != 0)
            ctx.Fail("round %d: %zu / %zu blocks freed by the thunk (the state: 1), or other sizes", round, counts[0], counts[1]);
    }
    std::memcpy(window, saved.data(), saved.size());
}

// 0x90a915 (the flag objects of slots 507 and 508): the byte set to 1, the rest untouched, this returned.
DIFF_TEST(StaticInitParser_FlagConstruct, 0x90a915)
{
    auto orig = ctx.Original<void *(__thiscall *)(void *)>(0x90a915);
    std::mt19937 gen(0x90a915);
    for (int round = 0; round < 50; ++round) {
        uint8_t a[4], b[4];
        for (auto &c : a) c = static_cast<uint8_t>(gen());
        std::memcpy(b, a, 4);
        void *ra = orig(a + 1);
        void *rb = reinterpret_cast<staticinit::StaticFlag *>(b + 1)->Construct();
        ++ctx.cases;
        if (ra != a + 1 || rb != b + 1 || std::memcmp(a, b, 4) != 0 || a[1] != 1) ctx.Fail("round %d: differ", round);
    }
}
