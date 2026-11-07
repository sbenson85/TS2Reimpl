// Self-tests of the drawfx / winfx recording fakes (tests/drawfx_core.h, tests/winfx.h): argument normalisation, the
// comparison naming the first differing call, float returns, unsettled slots, the state-backed window slots. No
// original code runs here; each check fails the test with what went wrong.
#include "difftest.h"
#include "drawfx.h"
#include "winfx.h"

#include <cmath>
#include <cstddef>
#include <cstring>

namespace {

using drawfx::Ret;
// A test interface exercising each argument kind and a float return.
inline constexpr drawfx::Slot kProbeSlots[] = {
    {2, Ret::Int, true, "QueryInterface", "ua"},
    {0, Ret::Int, true, "AddRef", ""},
    {0, Ret::Int, true, "Release", ""},
    {3, Ret::Int, true, "Mixed", "bfs"},  // 3
    {1, Ret::Int, true, "Rect", "r"},     // 4
    {0, Ret::Float, true, "Scale", ""},   // 5
    {1, Ret::Int, false, "Unsure", "u"},  // 6
    {2, Ret::Int, true, "Floats", "F3u"}, // 7
    {1, Ret::Int, true, "Text", "S"},     // 8
    {5, Ret::Int, true, "Five", "aaauu"}, // 9
    {1, Ret::Int, true, "Undeclared", ""}, // 10
    {1, Ret::Int, true, "Data", "u"},      // 11
};
inline constexpr drawfx::Iface kProbe{"Probe", 12, kProbeSlots, false, drawfx::kTestTable};

// The same table not converted (no Iface::declare): the legacy guess for its 'i' arguments (DrawFx_StrictWords).
inline constexpr drawfx::Iface kLegacyProbe{"LegacyProbe", 12, kProbeSlots};

drawfx::Fake gA, gB, gL;

// A COM (__stdcall, `this` on the stack) test interface: a counted array, a rect, a float return.
inline constexpr drawfx::Slot kComSlots[] = {
    {2, Ret::Int, true, "QueryInterface", ""},
    {0, Ret::Int, true, "AddRef", ""},
    {0, Ret::Int, true, "Release", ""},
    {3, Ret::Int, true, "Consts", "u@2x2u"}, // 3
    {1, Ret::Int, true, "Rect", "r"},       // 4
    {0, Ret::Float, true, "Scale", ""},     // 5
};
inline constexpr drawfx::Iface kComProbe{"ComProbe", 6, kComSlots, true, drawfx::kTestTable};
drawfx::Fake gC;
void ResetCom() { gC.Reset<kComProbe>(203); }
using ConstsFn = uint32_t(__stdcall *)(void *, uint32_t, const uint32_t *, uint32_t);
using ComRectFn = uint32_t(__stdcall *)(void *, const int32_t *);
using ComScaleFn = float(__stdcall *)(void *);

template <class Fn, class... A> auto Slot(drawfx::Fake &f, unsigned slot, A... args)
{
    return reinterpret_cast<Fn>(f.vt[slot])(&f, args...);
}
using RectFn = void(__thiscall *)(void *, const int32_t *);
using MixedFn = void(__thiscall *)(void *, uint32_t, float, const char *);
using ScaleFn = float(__thiscall *)(void *);
using OneFn = uint32_t(__thiscall *)(void *, uint32_t);
using FloatsFn = void(__thiscall *)(void *, const float *, uint32_t);
using FiveFn = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

void Reset()
{
    gA.Reset<kProbe>(201);
    gB.Reset<kProbe>(202);
}

__declspec(noinline) void DrawRect(int32_t l, int32_t t, int32_t r, int32_t b)
{
    volatile int32_t pad[7] = {}; // a different frame from the other caller
    (void)pad;
    int32_t rect[4] = {l, t, r, b};
    Slot<RectFn>(gA, 4, rect);
}
__declspec(noinline) void DrawRectOther(int32_t l, int32_t t, int32_t r, int32_t b)
{
    int32_t rect[4] = {l, t, r, b};
    Slot<RectFn>(gA, 4, rect);
}

// Runs Same on a scratch context and returns its failure text ("" when the logs are equal).
std::string Compare(difftest::Context &ctx, const drawfx::Log &a, const drawfx::Log &b,
                    const std::vector<std::string> &errors = {})
{
    difftest::Context probe{ctx.image};
    drawfx::Same(probe, a, b, "probe", 0, errors);
    return probe.failures.empty() ? "" : probe.failures[0];
}

void Expect(difftest::Context &ctx, bool ok, const char *what, const std::string &detail = "")
{
    ++ctx.cases;
    if (!ok) ctx.Fail("%s %s", what, detail.c_str());
}

// A minimal cIGZString stand-in: slot 6 Data, slot 7 Strlen.
struct FakeString {
    void **vt;
    const char *data;
    uint32_t len;
};
const char *__fastcall StringData(FakeString *s, void *) { return s->data; }
uint32_t __fastcall StringLen(FakeString *s, void *) { return s->len; }
void *gStringVt[8] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, reinterpret_cast<void *>(&StringData),
                      reinterpret_cast<void *>(&StringLen)};

} // namespace

DIFF_TEST(DrawFx_SelfTest, 0)
{
    // The same rect from two different frames logs the same; a different coordinate is named in the failure.
    auto a = drawfx::Side(Reset, [] { DrawRect(1, 2, 3, 4); });
    auto b = drawfx::Side(Reset, [] { DrawRectOther(1, 2, 3, 4); });
    Expect(ctx, Compare(ctx, a.first, b.first).empty(), "rect contents, different frames:", Compare(ctx, a.first, b.first));
    b = drawfx::Side(Reset, [] { DrawRectOther(1, 2, 3, 5); });
    std::string f = Compare(ctx, a.first, b.first);
    Expect(ctx, f.find("call 0 of 1/1") != std::string::npos && f.find("Probe.Rect(rect{1, 2, 3, 4})") != std::string::npos &&
                    f.find("rect{1, 2, 3, 5}") != std::string::npos,
           "rect difference not named:", f);

    // Draw order: the same calls in another order fail at the first one that moved.
    a = drawfx::Side(Reset, [] { DrawRect(1, 1, 1, 1), DrawRect(2, 2, 2, 2); });
    b = drawfx::Side(Reset, [] { DrawRect(2, 2, 2, 2), DrawRect(1, 1, 1, 1); });
    f = Compare(ctx, a.first, b.first);
    Expect(ctx, f.find("call 0 of 2/2") != std::string::npos, "order difference not found:", f);
    // A missing call: the shorter side "has no more calls".
    b = drawfx::Side(Reset, [] { DrawRect(1, 1, 1, 1); });
    f = Compare(ctx, a.first, b.first);
    Expect(ctx, f.find("(no more calls)") != std::string::npos, "missing call not named:", f);

    // 'b' compares the low byte only; 'f' is exact; 's' compares the text, not the pointer.
    char text1[] = "caption", text2[] = "caption";
    a = drawfx::Side(Reset, [&] { Slot<MixedFn>(gA, 3, 0x12345601u, 0.1f, text1); });
    b = drawfx::Side(Reset, [&] { Slot<MixedFn>(gA, 3, 0xabcdef01u, 0.1f, text2); });
    Expect(ctx, Compare(ctx, a.first, b.first).empty(), "b / f / s equal:", Compare(ctx, a.first, b.first));
    b = drawfx::Side(Reset, [&] { Slot<MixedFn>(gA, 3, 0x01u, std::nextafter(0.1f, 1.0f), text2); });
    Expect(ctx, !Compare(ctx, a.first, b.first).empty(), "a float one ulp off passed", "");
    text2[0] = 'C';
    b = drawfx::Side(Reset, [&] { Slot<MixedFn>(gA, 3, 0x01u, 0.1f, text2); });
    f = Compare(ctx, a.first, b.first);
    Expect(ctx, f.find("\"Caption\"") != std::string::npos, "text difference not named:", f);

    // Fakes become their ids, registered ranges tag | offset, stack addresses a marker.
    static uint8_t block[64];
    uint32_t got[3] = {};
    drawfx::Side(Reset, [&] {
        drawfx::AddRange(block, sizeof(block), 0x7a000000);
        int32_t local = 0;
        Slot<FiveFn>(gA, 9, drawfx::P(&gB), drawfx::P(block + 12), drawfx::P(&local), 0u, 77u);
        for (int i = 0; i < 3; ++i) got[i] = drawfx::gLog.back().words[i];
    });
    Expect(ctx, got[0] == 202 && got[1] == 0x7a00000c && got[2] == drawfx::kStackMarker, "normalisation",
           std::to_string(got[0]) + " " + std::to_string(got[1]) + " " + std::to_string(got[2]));

    // A five-word slot returns its value with the stack balanced (the call returns at all), floats come back in ST0.
    uint32_t five = 0;
    float scale = 0;
    drawfx::Side(Reset, [&] {
        gA.Returns(9, 0x5150u);
        gA.ReturnsFloat(5, 1.5f);
        five = Slot<FiveFn>(gA, 9, drawfx::P(&gB), 0u, 0u, 4u, 5u);
        scale = Slot<ScaleFn>(gA, 5);
    });
    Expect(ctx, five == 0x5150 && scale == 1.5f, "returns", std::to_string(five) + " " + std::to_string(scale));

    // Pointer to n floats, then a word.
    const float coeffs[3] = {0.25f, -1.0f, 3.0f};
    a = drawfx::Side(Reset, [&] { Slot<FloatsFn>(gA, 7, coeffs, 9u); });
    Expect(ctx, drawfx::Describe(a.first[0]) == "#201 Probe.Floats({0.25, -1, 3}, 0x9)", "F3 description:",
           drawfx::Describe(a.first[0]));

    // cIGZString text through its slots 6 and 7.
    FakeString s{gStringVt, "hello world", 5};
    a = drawfx::Side(Reset, [&] { Slot<OneFn>(gA, 8, drawfx::P(&s)); });
    Expect(ctx, drawfx::Describe(a.first[0]) == "#201 Probe.Text(\"hello\")", "S description:", drawfx::Describe(a.first[0]));

    // An unsettled slot reports itself; Settle quiets it.
    a = drawfx::Side(Reset, [] { Slot<OneFn>(gA, 6, 1u); });
    Expect(ctx, a.second.size() == 1 && a.second[0].find("slot 6") != std::string::npos, "unsettled slot not reported", "");
    a = drawfx::Side([] { Reset(), gA.Settle<6, 1>(); }, [] { Slot<OneFn>(gA, 6, 1u); });
    Expect(ctx, a.second.empty(), "Settle did not settle the slot", "");
    Expect(ctx, !Compare(ctx, a.first, a.first, {"x"}).empty(), "errors are not reported by Same", "");

    // QueryInterface answers the faces, AddRefs them.
    drawfx::Side(Reset, [&] {
        gA.AddFace(0x1234, gB);
        void *out = nullptr;
        const uint32_t hit = Slot<uint32_t(__thiscall *)(void *, uint32_t, void **)>(gA, 0, 0x1234u, &out);
        const uint32_t miss = Slot<uint32_t(__thiscall *)(void *, uint32_t, void **)>(gA, 0, 0x9999u, &out);
        Expect(ctx, hit == 1 && miss == 0 && out == &gB && gB.refs == 1001, "faces", "");
    });

    // COM fakes: __stdcall thunks (the result comes back with the stack balanced, floats in ST0), a counted array
    // logged by the count argument, and pointer kinds that also compare where the pointer points.
    static uint32_t consts[6] = {1, 2, 3, 4, 5, 6};
    uint32_t got3 = 0;
    float gotScale = 0;
    a = drawfx::Side(ResetCom, [&] {
        drawfx::AddRange(consts, sizeof(consts), 0x7d000000);
        gC.Returns(3, 0x77u);
        gC.ReturnsFloat(5, 2.5f);
        got3 = reinterpret_cast<ConstsFn>(gC.vt[3])(&gC, 9u, consts, 2u);
        gotScale = reinterpret_cast<ComScaleFn>(gC.vt[5])(&gC);
    });
    Expect(ctx, got3 == 0x77 && gotScale == 2.5f, "COM returns", std::to_string(got3) + " " + std::to_string(gotScale));
    Expect(ctx, drawfx::Describe(a.first[0]) == "#203 ComProbe.Consts(0x9, [0x1, 0x2, 0x3, 0x4], 0x2)",
           "@ description:", drawfx::Describe(a.first[0]));
    b = drawfx::Side(ResetCom, [&] {
        drawfx::AddRange(consts, sizeof(consts), 0x7d000000);
        reinterpret_cast<ConstsFn>(gC.vt[3])(&gC, 9u, consts, 3u);
    });
    Expect(ctx, !Compare(ctx, a.first, b.first).empty(), "a longer counted array passed", "");
    static int32_t rects[8] = {1, 2, 3, 4, 1, 2, 3, 4};
    auto rectAt = [&](int at) {
        return drawfx::Side(ResetCom, [&] {
            drawfx::AddRange(rects, sizeof(rects), 0x7b000000);
            reinterpret_cast<ComRectFn>(gC.vt[4])(&gC, rects + at);
        });
    };
    Expect(ctx, Compare(ctx, rectAt(0).first, rectAt(0).first).empty(), "COM rect equal", "");
    Expect(ctx, !Compare(ctx, rectAt(0).first, rectAt(4).first).empty(), "COM rect elsewhere passed", "");
}

namespace {
bool Has(const std::vector<std::string> &errors, const char *a, const char *b = "")
{
    for (const std::string &e : errors)
        if (e.find(a) != std::string::npos && e.find(b) != std::string::npos) return true;
    return false;
}
} // namespace

// Strict words (lane QT): no argument's kind is guessed from its value. The adversarial case is the one that flaked
// GZDraw2D_Slots and cDeviceStateDX_SetUp: a data word that lands inside the block one side registered.
DIFF_TEST(DrawFx_StrictWords, 0)
{
    normwords::TakeErrors();
    static uint8_t blockOrig[64], blockOurs[64]; // the "same" object, at another address on each side
    const uint32_t data = drawfx::P(blockOrig + 8); // a data word (a colour, a count) equal to an address on one side
    using OneFn = uint32_t(__thiscall *)(void *, uint32_t);

    // The old 'i' (a table not converted: drawfx::Norm guesses): the original's side rewrites the word as tag | offset,
    // ours leaves it, and the logs differ though both sides behaved the same (the runner adds normaudit's note to such a
    // failure: "lay inside a block registered on the other side").
    auto guess = [&](uint8_t *block) {
        return drawfx::Side([] { Reset(), gL.Reset<kLegacyProbe>(204); }, [&] {
            drawfx::AddRange(block, 64, 0x7c000000);
            Slot<OneFn>(gL, 10, data);
        });
    };
    auto a = guess(blockOrig), b = guess(blockOurs);
    std::string f = Compare(ctx, a.first, b.first);
    Expect(ctx, a.first[0].words[0] == 0x7c000008 && b.first[0].words[0] == data && !f.empty() && normwords::TakeErrors().empty(),
           "the old guess did not fail one-sided:", f);

    // The test's own pointer logging through Ptr (what a dumper's pointer words use in place of Norm, lane RK): a data
    // word fails, on either side, and the same pointer into each side's block is the same record.
    auto own = [&](uint8_t *block, uint32_t word) {
        return drawfx::Side(Reset, [&] {
            drawfx::AddRange(block, 64, 0x7c000000);
            drawfx::Note("own", drawfx::Ptr(word));
        });
    };
    a = own(blockOrig, data);
    Expect(ctx, a.first[0].words[0] == 0x7c000008 && normwords::TakeErrors().empty(), "Ptr of a pointer into the block", "");
    b = own(blockOurs, 0x00012345u); // a count, a colour: points into nothing
    std::vector<std::string> errors = normwords::TakeErrors();
    Expect(ctx, Has(errors, "drawfx::Ptr at drawfx_selftest.cpp", "points into no registered object"),
           "Ptr accepted a data word:", errors.empty() ? "(no error)" : errors[0]);
    a = own(blockOrig, drawfx::P(blockOrig + 12)), b = own(blockOurs, drawfx::P(blockOurs + 12));
    Expect(ctx, Compare(ctx, a.first, b.first).empty() && normwords::TakeErrors().empty(), "Ptr of the same pointer differed",
           Compare(ctx, a.first, b.first));
    // A word holding one of the test's fakes or data: the fake by identity, data as it is even inside the side's block.
    auto either = [&](uint8_t *block, uint32_t word) {
        return drawfx::Side(Reset, [&] {
            drawfx::AddRange(block, 64, 0x7c000000);
            drawfx::Call c;
            drawfx::FakeOrData(c.words, word);
            drawfx::gLog.push_back(std::move(c));
        });
    };
    a = either(blockOrig, data), b = either(blockOurs, data);
    Expect(ctx, Compare(ctx, a.first, b.first).empty() && a.first[0].words[0] == 0 && a.first[0].words[1] == data,
           "FakeOrData of a data word", Compare(ctx, a.first, b.first));
    a = either(blockOrig, drawfx::P(&gA)), b = either(blockOurs, drawfx::P(&gA));
    Expect(ctx, Compare(ctx, a.first, b.first).empty() && a.first[0].words[0] == 1 && a.first[0].words[1] == gA.id,
           "FakeOrData of a fake", Compare(ctx, a.first, b.first));
    // Words by declared kind: data as it is (even when it lies inside the side's block), pointers strict, '-' left out.
    auto words = [&](uint8_t *block) {
        return drawfx::Side(Reset, [&] {
            drawfx::AddRange(block, 64, 0x7c000000);
            uint32_t object[4] = {0xdeadbeef, data, drawfx::P(block + 4), 7};
            drawfx::Call c;
            drawfx::Words(c.words, object, 4, "-uau");
            drawfx::gLog.push_back(std::move(c));
        });
    };
    a = words(blockOrig), b = words(blockOurs);
    Expect(ctx, Compare(ctx, a.first, b.first).empty() && normwords::TakeErrors().empty() && a.first[0].words.size() == 3 &&
                    a.first[0].words[0] == data && a.first[0].words[1] == 0x7c000004 && a.first[0].words[2] == 7,
           "Words by kind", Compare(ctx, a.first, b.first));
    // A COM pointer kind's presence word is a pointer record (the Direct3D code hands the device pointers into its own
    // state): an unregistered one fails the test.
    int32_t *const unregistered = new int32_t[4]{1, 2, 3, 4}; // allocated before the side started
    drawfx::Side(ResetCom, [&] { Slot<ComRectFn>(gC, 4, unregistered); });
    delete[] unregistered;
    errors = normwords::TakeErrors();
    Expect(ctx, Has(errors, "ComProbe.Rect (slot 4) argument 0", "points into no registered object"),
           "a COM pointer kind's unregistered presence word was not reported:", errors.empty() ? "(no error)" : errors[0]);

    // The same word declared data ('u'): logged as it is on both sides, equal.
    auto declared = [&](uint8_t *block, unsigned slot) {
        return drawfx::Side(Reset, [&] {
            drawfx::AddRange(block, 64, 0x7c000000);
            Slot<OneFn>(gA, slot, data);
        });
    };
    a = declared(blockOrig, 11), b = declared(blockOurs, 11);
    Expect(ctx, Compare(ctx, a.first, b.first).empty() && normwords::TakeErrors().empty(), "a data word ('u') differed",
           Compare(ctx, a.first, b.first));

    // Undeclared ('i'): both sides log the same placeholder and the test fails, naming the slot, the argument and
    // where to declare it; zero needs no kind.
    a = declared(blockOrig, 10), b = declared(blockOurs, 10);
    errors = normwords::TakeErrors();
    Expect(ctx, Compare(ctx, a.first, b.first).empty() && Has(errors, "Probe.Undeclared (slot 10) argument 0", "has no declared kind") &&
                    Has(errors, "drawfx::Slot table"),
           "an undeclared word was not reported:", errors.empty() ? "(no error)" : errors[0]);
    drawfx::Side(Reset, [&] { Slot<OneFn>(gA, 10, 0u); });
    Expect(ctx, normwords::TakeErrors().empty(), "an undeclared zero failed", "");

    // A pointer ('a') into nothing registered fails the test, naming the slot and argument; a heap block allocated in
    // the side is a record (the same on both sides, whatever its address); a pointer into the other fake's interior too.
    uint8_t *before = new uint8_t[32]; // allocated before the side started: not registered
    drawfx::Side(Reset, [&] { Slot<FiveFn>(gA, 9, drawfx::P(before + 4), 0u, 0u, 0u, 0u); });
    errors = normwords::TakeErrors();
    Expect(ctx, Has(errors, "Probe.Five (slot 9) argument 0", "points into no registered object"),
           "an unregistered pointer was not reported:", errors.empty() ? "(no error)" : errors[0]);
    uint8_t *held[2] = {};
    auto heap = [&](int side) {
        return drawfx::Side(Reset, [&] {
            held[side] = new uint8_t[32];
            Slot<FiveFn>(gA, 9, drawfx::P(held[side] + 4), drawfx::P(&gB.value[1]), 0u, 0u, 0u);
        });
    };
    a = heap(0), b = heap(1);
    Expect(ctx, held[0] != held[1] && Compare(ctx, a.first, b.first).empty() && normwords::TakeErrors().empty() &&
                    a.first[0].words[0] == (normwords::kHeapTag | 4) &&
                    a.first[0].words[1] == (normwords::kFakeInterior | 1u << 12 | unsigned(offsetof(drawfx::Fake, value) + 4)),
           "heap / fake interior records", Compare(ctx, a.first, b.first));
    delete[] held[0];
    delete[] held[1];
    delete[] before;
    // The strict pointer for a test's own logging: the same records, and the same failure.
    drawfx::Side(Reset, [&] { drawfx::Note("p", drawfx::Ptr(0x1234u)); });
    Expect(ctx, Has(normwords::TakeErrors(), "drawfx::Ptr at drawfx_selftest.cpp", "below 0x10000"),
           "Ptr accepted a small integer", "");
}

DIFF_TEST(WinFx_SelfTest, 0)
{
    static winfx::World w;
    using IdFn = void *(__thiscall *)(void *, uint32_t);
    drawfx::Side([] {
        w.Build(7, 1);
        w.count = 4;
        for (unsigned i = 1; i < 4; ++i) w[i].Reset(300 + i, 0);
        w[0].id = 5;
        w[1].id = 5; // a child with its parent's id: the child wins (children are asked first)
        w[2].id = 6;
        w[3].id = 7;
        w[0].Add(w[1]);
        w[0].Add(w[2]);
        w[2].Add(w[3]);
    },
                 [&] {
                     auto *root = &w[0].f;
                     Expect(ctx, Slot<IdFn>(*root, 39, 5u) == &w[1].f, "recursive lookup prefers children", "");
                     Expect(ctx, Slot<IdFn>(*root, 39, 7u) == &w[3].f, "recursive lookup reaches grandchildren", "");
                     Expect(ctx, Slot<IdFn>(*root, 38, 7u) == nullptr, "direct lookup does not recurse", "");
                     Expect(ctx, Slot<uint32_t(__thiscall *)(void *)>(*root, 15) == 2, "child count", "");
                     Expect(ctx, Slot<void *(__thiscall *)(void *)>(w[3].f, 11) == &w[2].f, "parent", "");
                     // SetFlag takes the low byte of the value.
                     Slot<uint32_t(__thiscall *)(void *, uint32_t, uint32_t)>(*root, 83, 0x40u, 0x100u);
                     Expect(ctx, (w[0].flags & 0x40) == 0, "SetFlag with a zero low byte set the flag", "");
                     Slot<uint32_t(__thiscall *)(void *, uint32_t, uint32_t)>(*root, 83, 0x40u, 0x101u);
                     Expect(ctx, Slot<uint32_t(__thiscall *)(void *, uint32_t)>(*root, 82, 0x40u) == 1, "IsFlagSet", "");
                     Slot<void(__thiscall *)(void *, int32_t, int32_t, int32_t, int32_t)>(*root, 64, 10, 20, 110, 70);
                     int32_t area[4] = {};
                     Slot<void(__thiscall *)(void *, int32_t *)>(*root, 55, area);
                     Expect(ctx, area[0] == 10 && area[3] == 70 && Slot<int32_t(__thiscall *)(void *)>(*root, 49) == 100,
                            "area", "");
                     // Every call above was logged, the lookups included.
                     Expect(ctx, drawfx::gLog.size() == 11, "log size", std::to_string(drawfx::gLog.size()));
                 });
}
