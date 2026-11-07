// Differential tests for cRZLanguageUtility in the standalone shape (docs/standalone-shape.md "Tests"). The original
// side is an object built by the original constructor (0xc6ed94, the game's vtable 0xe9f014); our side one built by the
// constructor adapter (our vtable). Every slot is called through each object's own vtable.
//
// Out and in strings are cRZStrings carrying a copy of the game's string vtable whose mutating slots log (slot, the
// count arguments, the text after the call) before and after forwarding to the game's bodies, so the comparison covers
// every write the slot makes to its strings and their order. The three user32 / kernel32 imports (CharLowerBuffA,
// CharUpperBuffA, CompareStringA) are impfx fakes that log their arguments and texts into the same log; the settings
// service FormatTime reads is a fake framework (game global 0x10c9378) handing out a fake property set. The system code
// page (game global 0xff23a4) is seeded to the default encoding's (8, UTF-8) so the conversions are copies; the
// conversions themselves are tested in tests/rz_text.cpp.
//
// Slots covered by a test registered under another slot's RVA (the per-slot tests of the earlier hook-shaped class
// became these groups):
//   cRZLanguageUtility_Slots: QueryInterface 0xc747f9, Release 0xc748d9;
//   cRZLanguageUtility_Currency: CurrencySymbolFirst 0xc6f11b, CurrencySymbolSpaced 0xc7349d, GetThousandsSeparator
//     0xc733c2, GetDecimalSeparator 0xc7112f;
//   cRZLanguageUtility_Case: ToUpper 0xc6f018;
//   cRZLanguageUtility_CodePages: SupportsCodePage 0xc6f16a;
//   cRZLanguageUtility_Rules: CountryIsNotDefault 0xc724ca, FamilyNameFirst 0xc6ee8c, vf29 0xc6f2d3, vf30 0xc6f2f0;
//   cRZLanguageUtility_Destruct: the deleting destructor 0xc6ee49.
#include "difftest.h"
#include "free_scope.h"
#include "impfx.h"
#include "rz_recording_string.h"
#include "gz/cIGZPropertySet.h"
#include "rz/cRZLanguageUtility.h"
#include "rz/cRZString.h"

#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kCtor = 0xc6ed94;
constexpr uint32_t kDtor = 0xc6ee0d;
constexpr uint32_t kToDefaultEncoding = 0xc6f07a;
constexpr uint32_t kCodePageGlobal = 0xff23a4;
constexpr uint32_t kFrameworkGlobal = 0x10c9378;
constexpr uint32_t kGameAnyBooleanVTable = 0xd9c670;
constexpr uint32_t kSeededCodePage = 8; // RZText::DefaultEncoding(): the conversions copy

using namespace recstr;

// ---- imports -------------------------------------------------------------------------------------------------------

// CharLowerBuffA / CharUpperBuffA: the ASCII letters of the first `length` bytes, stopping at a NUL (the buffer the
// slots pass is the converted text, whose length may be shorter than the count they pass).
uint32_t MapCase(const uint32_t *args, bool upper)
{
    char *text = reinterpret_cast<char *>(static_cast<uintptr_t>(args[0]));
    const uint32_t length = args[1];
    uint32_t i = 0;
    for (; i < length && text[i] != 0; ++i) {
        const char c = text[i];
        if (upper && c >= 'a' && c <= 'z') text[i] = static_cast<char>(c - 0x20);
        if (!upper && c >= 'A' && c <= 'Z') text[i] = static_cast<char>(c + 0x20);
    }
    Note("%s(%u) -> [%s]", upper ? "CharUpperBuffA" : "CharLowerBuffA", length, Shown(text, i).c_str());
    return length;
}
uint32_t OnCharLower(drawfx::Fake &, const uint32_t *args) { return MapCase(args, false); }
uint32_t OnCharUpper(drawfx::Fake &, const uint32_t *args) { return MapCase(args, true); }

bool gCompareForced = false; // answer gCompareAnswer as it is (0 included) instead of computing
uint32_t gCompareAnswer = 0;

uint32_t OnCompareString(drawfx::Fake &, const uint32_t *args)
{
    const char *a = reinterpret_cast<const char *>(static_cast<uintptr_t>(args[2]));
    const char *b = reinterpret_cast<const char *>(static_cast<uintptr_t>(args[4]));
    const int32_t aLength = static_cast<int32_t>(args[3]);
    const int32_t bLength = static_cast<int32_t>(args[5]);
    Note("CompareStringA(0x%x, 0x%x, [%s] %d, [%s] %d)", args[0], args[1], Shown(a, aLength < 0 ? 0 : aLength).c_str(),
         aLength, Shown(b, bLength < 0 ? 0 : bLength).c_str(), bLength);
    if (gCompareForced) return gCompareAnswer;
    const bool fold = (args[1] & 1) != 0;
    const int32_t n = aLength < bLength ? aLength : bLength;
    for (int32_t i = 0; i < n; ++i) {
        int ca = static_cast<unsigned char>(a[i]);
        int cb = static_cast<unsigned char>(b[i]);
        if (fold && ca >= 'A' && ca <= 'Z') ca += 0x20;
        if (fold && cb >= 'A' && cb <= 'Z') cb += 0x20;
        if (ca != cb) return ca < cb ? 1 : 3;
    }
    return aLength == bLength ? 2 : aLength < bLength ? 1 : 3;
}

// ---- the settings service ------------------------------------------------------------------------------------------

uint8_t gForce12Hour = 0;
void *gSettingsVTable[12];
void *gFrameworkVTable[12];
struct FakeObject {
    void *vtable;
};
FakeObject gSettings{gSettingsVTable};
FakeObject gFramework{gFrameworkVTable};

uint32_t __fastcall FakeAddRef(void *self, void *)
{
    Note("%s AddRef", self == &gSettings ? "settings" : "framework");
    return 2;
}
uint32_t __fastcall FakeRelease(void *self, void *)
{
    Note("%s Release", self == &gSettings ? "settings" : "framework");
    return 1;
}
bool __fastcall FakeGetSystemService(void *, void *, uint32_t id, uint32_t iid, void **out)
{
    Note("framework GetSystemService(0x%08x, 0x%08x)", id, iid);
    *out = &gSettings;
    return true;
}
// The property set's GetPropertyValue: assigns the stored boolean to the caller's any through its slot 1 (Assign), as
// the property set does, from a game AnyBoolean.
bool __fastcall FakeGetPropertyValue(void *, void *, const void *key, void *out)
{
    const GZServiceString *name = static_cast<const GZServiceString *>(key);
    Note("settings GetPropertyValue([%s])", Shown(name->data(), name->size()).c_str());
    struct {
        const void *vtable;
        uint8_t value;
        uint8_t pad[3];
    } source = {GameAddress(kGameAnyBooleanVTable), gForce12Hour, {0, 0, 0}};
    void **vtable = *static_cast<void ***>(out);
    reinterpret_cast<bool(__thiscall *)(void *, const void *)>(vtable[1])(out, &source);
    return true;
}
uint32_t __fastcall FakeUnexpected(void *self, void *)
{
    Note("%s unexpected slot", self == &gSettings ? "settings" : "framework");
    return 0;
}

// Points game global 0x10c9378 at the fake framework while alive.
struct FakeServices {
    void *saved;
    FakeServices()
    {
        for (void *&slot : gSettingsVTable) slot = reinterpret_cast<void *>(&FakeUnexpected);
        for (void *&slot : gFrameworkVTable) slot = reinterpret_cast<void *>(&FakeUnexpected);
        gSettingsVTable[1] = gFrameworkVTable[1] = reinterpret_cast<void *>(&FakeAddRef);
        gSettingsVTable[2] = gFrameworkVTable[2] = reinterpret_cast<void *>(&FakeRelease);
        gFrameworkVTable[5] = reinterpret_cast<void *>(&FakeGetSystemService);
        gSettingsVTable[9] = reinterpret_cast<void *>(&FakeGetPropertyValue);
        saved = *GameAddress<void *>(kFrameworkGlobal);
        *GameAddress<void *>(kFrameworkGlobal) = &gFramework;
    }
    ~FakeServices() { *GameAddress<void *>(kFrameworkGlobal) = saved; }
};

// ---- the two objects -----------------------------------------------------------------------------------------------

template <class Fn> Fn SlotOf(void *object, unsigned slot) { return reinterpret_cast<Fn>((*static_cast<void ***>(object))[slot]); }

void *NewOriginal(difftest::Context &ctx)
{
    void *memory = ::operator new(cRZLanguageUtility::kSize);
    std::memset(memory, 0xcd, cRZLanguageUtility::kSize);
    return ctx.Original<void *(__thiscall *)(void *)>(kCtor)(memory);
}
void *NewOurs()
{
    void *memory = ::operator new(cRZLanguageUtility::kSize);
    std::memset(memory, 0xcd, cRZLanguageUtility::kSize);
    return cRZLanguageUtilityAdapters::Construct(memory, nullptr);
}
void FreeOriginal(difftest::Context &ctx, void *object)
{
    ctx.Original<void(__thiscall *)(void *)>(kDtor)(object);
    ::operator delete(object);
}
void FreeOurs(void *object)
{
    static_cast<cRZLanguageUtility *>(object)->~cRZLanguageUtility();
    ::operator delete(object);
}

uint32_t Word(const void *object, uint32_t offset)
{
    uint32_t value;
    std::memcpy(&value, static_cast<const uint8_t *>(object) + offset, 4);
    return value;
}
const cRZString &GroupSpace(const void *object)
{
    return *reinterpret_cast<const cRZString *>(static_cast<const uint8_t *>(object) + 0x14);
}
cRZString &GroupSpace(void *object) { return *reinterpret_cast<cRZString *>(static_cast<uint8_t *>(object) + 0x14); }

// The object's state apart from its vtable word: the count, language, country, flag byte (and the three bytes after
// it), and the separator string's vtable word (the game's on both sides), text and count.
std::string StateOf(const void *object)
{
    char head[96];
    std::snprintf(head, sizeof(head), "refs %u lang 0x%x country 0x%x flag 0x%08x svt %d [", Word(object, 4),
                  Word(object, 8), Word(object, 0xc), Word(object, 0x10),
                  GroupSpace(object).mVTable == GameAddress(cRZString::kVTable) ? 1 : 2);
    return head + TextOf(GroupSpace(object)) + "] srefs " + std::to_string(GroupSpace(object).mRefCount);
}

struct Setup {
    uint32_t language = 1;
    uint32_t country = 0x36;
    uint8_t flag = 0;
};

void Configure(void *object, const Setup &setup)
{
    SlotOf<void(__thiscall *)(void *, uint32_t)>(object, 32)(object, setup.language);
    SlotOf<void(__thiscall *)(void *, uint32_t)>(object, 33)(object, setup.country);
    static_cast<uint8_t *>(object)[0x10] = setup.flag;
}

struct SideResult {
    std::string result;
    std::vector<std::string> log;
    std::string state;
};

// Runs `body` on a fresh object of each side configured by `setup` and compares the result text, the log (string
// writes, imports, service calls) and the object's state afterwards.
void Compare(difftest::Context &ctx, impfx::Imports &imp, const Setup &setup, const char *what,
             const std::function<std::string(void *)> &body)
{
    SideResult sides[2];
    for (int side = 0; side < 2; ++side) {
        void *object = side == 0 ? NewOriginal(ctx) : NewOurs();
        Configure(object, setup);
        imp.BeginSide();
        drawfx::ResetAll();
        gLog.clear();
        sides[side].result = body(object);
        sides[side].log = gLog;
        sides[side].state = StateOf(object);
        if (side == 0)
            FreeOriginal(ctx, object);
        else
            FreeOurs(object);
    }
    ++ctx.cases;
    const SideResult &a = sides[0];
    const SideResult &b = sides[1];
    if (a.result != b.result)
        ctx.Fail("%s lang 0x%x flag %u: result [%s] / [%s]", what, setup.language, setup.flag, a.result.c_str(),
                 b.result.c_str());
    if (a.state != b.state)
        ctx.Fail("%s lang 0x%x: state [%s] / [%s]", what, setup.language, a.state.c_str(), b.state.c_str());
    if (a.log != b.log) {
        size_t i = 0;
        while (i < a.log.size() && i < b.log.size() && a.log[i] == b.log[i]) ++i;
        ctx.Fail("%s lang 0x%x flag %u: log %zu / %zu entries, first difference at %zu: [%s] / [%s]", what,
                 setup.language, setup.flag, a.log.size(), b.log.size(), i,
                 i < a.log.size() ? a.log[i].c_str() : "(end)", i < b.log.size() ? b.log[i].c_str() : "(end)");
    }
}

// The fixture of every test: the recording strings, the imports, the code page, the fake settings service.
struct Fixture {
    impfx::Imports imp;
    FakeServices services;
    int32_t savedCodePage;
    explicit Fixture(difftest::Context &ctx) : imp(ctx)
    {
        SetUpStrings(ctx);
        IMPFX_ADD(imp, CharLowerBuffA).On(&OnCharLower);
        IMPFX_ADD(imp, CharUpperBuffA).On(&OnCharUpper);
        IMPFX_ADD(imp, CompareStringA).On(&OnCompareString);
        savedCodePage = *GameAddress<int32_t>(kCodePageGlobal);
        *GameAddress<int32_t>(kCodePageGlobal) = static_cast<int32_t>(kSeededCodePage);
        gCompareForced = false;
        gCompareAnswer = 0;
        gForce12Hour = 0;
    }
    ~Fixture() { *GameAddress<int32_t>(kCodePageGlobal) = savedCodePage; }
};

std::string Hex(uint32_t value)
{
    char text[16];
    std::snprintf(text, sizeof(text), "0x%x", value);
    return text;
}

// The result of a slot writing a string: its return value, then the string's text.
std::string WithText(uint32_t value, const RecString &out) { return Hex(value) + " [" + TextOf(out.s) + "]"; }

// Every language id from 0 to 0x45, the edges, and a few random values.
std::vector<uint32_t> Languages(std::mt19937 &rng, int randoms = 8)
{
    std::vector<uint32_t> out;
    for (uint32_t id = 0; id <= 0x45; ++id) out.push_back(id);
    const uint32_t extra[] = {0x7f, 0x80, 0xff, 0x100, 0x7fffffffu, 0x80000000u, 0xfffffffeu, 0xffffffffu};
    for (uint32_t id : extra) out.push_back(id);
    for (int i = 0; i < randoms; ++i) out.push_back(rng());
    return out;
}

template <class T, size_t N> T Pick(std::mt19937 &rng, const T (&values)[N]) { return values[rng() % N]; }

using NameFn = bool(__thiscall *)(void *, uint8_t, cIGZString *);
using TimeFn = bool(__thiscall *)(void *, int32_t, int32_t, int32_t, cIGZString *);
using DateFn = bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, cIGZString *, uint32_t);
using OutFn = bool(__thiscall *)(void *, cIGZString *);
using BoolFn = bool(__thiscall *)(void *);
using WordFn = uint32_t(__thiscall *)(void *);
using MoneyFn = bool(__thiscall *)(void *, int64_t, cIGZString *, const cIGZString *);
using MoneyFloatFn = bool(__thiscall *)(void *, double, cIGZString *, const cIGZString *, const cIGZString *);
using IntegerFn = bool(__thiscall *)(void *, int64_t, cIGZString *);
using FloatFn = bool(__thiscall *)(void *, double, cIGZString *, const cIGZString *);
using CaseFn = void(__thiscall *)(void *, const cIGZString *, cIGZString *);
using CompareFn = int32_t(__thiscall *)(void *, const cIGZString *, const cIGZString *, bool);
using CodePageFn = bool(__thiscall *)(void *, uint32_t);
using FullNameFn = void(__thiscall *)(void *, const cIGZString *, const cIGZString *, cIGZString *);

} // namespace

DIFF_TEST(cRZLanguageUtility_Names, 0xc70b3b)
{
    // Slots 5 to 8 (0xc70b3b, 0xc6f35f, 0xc724d4, 0xc6fb1f): every language, every index from 0 to 15 and the byte's top.
    Fixture fx(ctx);
    std::mt19937 rng(7101);
    static const char *const kNames[] = {"GetDayName", "GetShortDayName", "GetMonthName", "GetShortMonthName"};
    for (uint32_t language : Languages(rng)) {
        for (unsigned slot = 5; slot <= 8; ++slot) {
            for (uint32_t index : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u, 0x80u, 0xffu}) {
                Setup setup;
                setup.language = language;
                Compare(ctx, fx.imp, setup, kNames[slot - 5], [&](void *object) {
                    RecString out("seed");
                    const bool r = SlotOf<NameFn>(object, slot)(object, static_cast<uint8_t>(index), out.I());
                    return WithText(r, out);
                });
            }
        }
    }
}

DIFF_TEST(cRZLanguageUtility_FormatTime, 0xc74531)
{
    Fixture fx(ctx);
    std::mt19937 rng(7109);
    static const int32_t kHours[] = {-5, -1, 0, 1, 9, 11, 12, 13, 20, 23, 24, 25, 36, 100, INT_MAX, INT_MIN};
    static const int32_t kMinutes[] = {-100, -1, 0, 5, 30, 59, 60, 61, 125, INT_MAX, INT_MIN};
    for (uint32_t language : Languages(rng)) {
        for (int i = 0; i < 40; ++i) {
            Setup setup;
            setup.language = language;
            const int32_t hour = Pick(rng, kHours);
            const int32_t minute = Pick(rng, kMinutes);
            const int32_t second = Pick(rng, kMinutes);
            const uint8_t force = static_cast<uint8_t>(i % 3 == 0 ? 1 : 0);
            Compare(ctx, fx.imp, setup, "FormatTime", [&](void *object) {
                gForce12Hour = force;
                RecString out("seed text");
                const bool r = SlotOf<TimeFn>(object, 9)(object, hour, minute, second, out.I());
                return WithText(r, out) + " " + std::to_string(hour) + ":" + std::to_string(minute) + ":" +
                       std::to_string(second) + " f" + std::to_string(force);
            });
        }
    }
}

DIFF_TEST(cRZLanguageUtility_FormatDate, 0xc73504)
{
    Fixture fx(ctx);
    std::mt19937 rng(7110);
    static const uint32_t kMonths[] = {0, 1, 2, 6, 11, 12, 13, 0x100, 0xffffffffu};
    static const uint32_t kDays[] = {0, 1, 9, 10, 28, 31, 32, 0xffffffffu};
    static const uint32_t kYears[] = {0, 5, 99, 100, 999, 1000, 2004, 9999, 10000, 99999, 123456, 0xffffffffu};
    for (uint32_t language : Languages(rng)) {
        for (uint32_t style = 0; style <= 7; ++style) {
            for (int i = 0; i < 4; ++i) {
                Setup setup;
                setup.language = language;
                const uint32_t month = Pick(rng, kMonths);
                const uint32_t day = Pick(rng, kDays);
                const uint32_t year = Pick(rng, kYears);
                Compare(ctx, fx.imp, setup, "FormatDate", [&](void *object) {
                    RecString out("seed");
                    const bool r = SlotOf<DateFn>(object, 10)(object, month, day, year, out.I(), style);
                    return WithText(r, out) + " style " + std::to_string(style);
                });
            }
        }
    }
}

DIFF_TEST(cRZLanguageUtility_Currency, 0xc709ef)
{
    // Slots 11 to 15: the symbol (with the flag clear and set), its placement, the two separators.
    Fixture fx(ctx);
    std::mt19937 rng(7111);
    for (uint32_t language : Languages(rng, 32)) {
        for (uint8_t flag : {uint8_t(0), uint8_t(1), uint8_t(0x80)}) {
            Setup setup;
            setup.language = language;
            setup.flag = flag;
            Compare(ctx, fx.imp, setup, "GetCurrencySymbol", [&](void *object) {
                RecString out("seed");
                return WithText(SlotOf<OutFn>(object, 11)(object, out.I()), out);
            });
        }
        Setup setup;
        setup.language = language;
        Compare(ctx, fx.imp, setup, "CurrencySymbolFirst/Spaced", [&](void *object) {
            return Hex(SlotOf<BoolFn>(object, 12)(object)) + Hex(SlotOf<BoolFn>(object, 13)(object));
        });
        Compare(ctx, fx.imp, setup, "GetThousandsSeparator", [&](void *object) {
            RecString out("seed");
            return WithText(SlotOf<OutFn>(object, 14)(object, out.I()), out);
        });
        Compare(ctx, fx.imp, setup, "GetDecimalSeparator", [&](void *object) {
            RecString out("seed");
            return WithText(SlotOf<OutFn>(object, 15)(object, out.I()), out);
        });
    }
}

namespace {
std::vector<int64_t> Amounts(std::mt19937 &rng)
{
    std::vector<int64_t> out = {0,       1,        -1,        9,         10,         99,          100,
                                999,     1000,     -1000,     12345,     -123456,    1234567,     -9999999,
                                INT32_MAX, INT32_MIN, 0x100000000ll, INT64_MAX,
                                INT64_MIN, INT64_MIN + 1};
    for (int i = 0; i < 6; ++i) {
        const int64_t wide = static_cast<int64_t>((static_cast<uint64_t>(rng()) << 32) | rng());
        out.push_back(wide >> (rng() % 63));
    }
    return out;
}
} // namespace

DIFF_TEST(cRZLanguageUtility_FormatMoney, 0xc73f26)
{
    // Slot 16, with no symbol (the language's), an empty one and a text; the flag clear and set.
    Fixture fx(ctx);
    std::mt19937 rng(7116);
    for (uint32_t language : Languages(rng, 4)) {
        for (int64_t value : Amounts(rng)) {
            for (int symbol = 0; symbol < 3; ++symbol) {
                Setup setup;
                setup.language = language;
                setup.flag = static_cast<uint8_t>(rng() & 1);
                Compare(ctx, fx.imp, setup, "FormatMoney", [&](void *object) {
                    RecString out("seed");
                    RecString given(symbol == 1 ? "" : "Kr");
                    const bool r = SlotOf<MoneyFn>(object, 16)(object, value, out.I(), symbol ? given.I() : nullptr);
                    return WithText(r, out) + " " + std::to_string(value) + " s" + std::to_string(symbol);
                });
            }
        }
    }
}

DIFF_TEST(cRZLanguageUtility_FormatMoneyFloat, 0xc73a5b)
{
    // Slot 17: formats with the conversion first, last, in text, after doubled percent signs, without a floating-point
    // conversion, with none at all, and a percent sign among the last two characters.
    Fixture fx(ctx);
    std::mt19937 rng(7117);
    static const char *const kFormats[] = {"%.2f",       "%f",         "%10.3f",   "a%%b %8.3f%%c", "%%%.1f",
                                           "x%%",        "%%",         "%d",       "abc",           "%",
                                           "a%",         "%e",         "%G",       "%%%%%g tail",   "%5.0f%%",
                                           "%s",         "",           "%-12.4f!", "%%x%%y%.1E",     "%.0f"};
    static const double kValues[] = {0.0,     1.0,        -1.0,    0.5,         -0.004,     12.345,   -999.999,
                                     1000.0,  1234567.891, -1e9,   123456789012.5, 1e-7,    99.995,   -0.0};
    for (uint32_t language : Languages(rng, 2)) {
        for (const char *format : kFormats) {
            for (int i = 0; i < 3; ++i) {
                Setup setup;
                setup.language = language;
                setup.flag = static_cast<uint8_t>(rng() & 1);
                const double value = Pick(rng, kValues);
                const int symbol = static_cast<int>(rng() % 3);
                Compare(ctx, fx.imp, setup, "FormatMoneyFloat", [&](void *object) {
                    RecString out("seed text");
                    RecString shape(format);
                    RecString given(symbol == 1 ? "" : "EUR");
                    const bool r = SlotOf<MoneyFloatFn>(object, 17)(object, value, out.I(), shape.I(),
                                                                    symbol ? given.I() : nullptr);
                    return WithText(r, out) + " [" + format + "] s" + std::to_string(symbol);
                });
            }
        }
    }
}

DIFF_TEST(cRZLanguageUtility_FormatInteger, 0xc74318)
{
    Fixture fx(ctx);
    std::mt19937 rng(7118);
    for (uint32_t language : Languages(rng, 4)) {
        for (int64_t value : Amounts(rng)) {
            Setup setup;
            setup.language = language;
            Compare(ctx, fx.imp, setup, "FormatInteger", [&](void *object) {
                RecString out("seed");
                return WithText(SlotOf<IntegerFn>(object, 18)(object, value, out.I()), out) + " " +
                       std::to_string(value);
            });
        }
    }
}

DIFF_TEST(cRZLanguageUtility_FormatFloat, 0xc73ddf)
{
    Fixture fx(ctx);
    std::mt19937 rng(7119);
    static const char *const kFormats[] = {"%f", "%.3f", "x%gy", "%e", "%10.1f", "%-8.2f|", "plain", "%%%.2f%%", ""};
    static const double kValues[] = {0.0, 1.5, -2.25, 1e10, -1e-5, 123456.789, 0.125};
    for (uint32_t language : {1u, 3u, 0xfu, 0x50u}) {
        for (const char *format : kFormats) {
            for (double value : kValues) {
                Setup setup;
                setup.language = language;
                Compare(ctx, fx.imp, setup, "FormatFloat", [&](void *object) {
                    RecString out("seed");
                    RecString shape(format);
                    return WithText(SlotOf<FloatFn>(object, 19)(object, value, out.I(), shape.I()), out);
                });
            }
        }
    }
    (void)rng;
}

DIFF_TEST(cRZLanguageUtility_Case, 0xc6efb6)
{
    // Slots 20 and 21, with destinations shorter than, as long as and longer than the source (the count the slots pass
    // is the destination's length).
    Fixture fx(ctx);
    static const char *const kSources[] = {"", "a", "Hello World", "MiXeD 123 _x_", "ALLUPPER", "lower case text here"};
    static const char *const kDestinations[] = {"", "dd", "destination", "a much longer destination text than most"};
    for (unsigned slot : {20u, 21u}) {
        for (const char *source : kSources) {
            for (const char *destination : kDestinations) {
                Setup setup;
                Compare(ctx, fx.imp, setup, slot == 20 ? "ToLower" : "ToUpper", [&](void *object) {
                    RecString src(source);
                    RecString dst(destination);
                    SlotOf<CaseFn>(object, slot)(object, src.I(), dst.I());
                    return "[" + TextOf(dst.s) + "] [" + TextOf(src.s) + "]";
                });
            }
        }
    }
}

DIFF_TEST(cRZLanguageUtility_Compare, 0xc6eed9)
{
    // Slot 22: both cases, the fake's computed answers (1 to 3) and forced ones (0, the failure, and values outside 1
    // to 3).
    Fixture fx(ctx);
    static const char *const kTexts[] = {"", "a", "A", "abc", "ABD", "abcd", "zebra", "Zebra"};
    static const int64_t kAnswers[] = {-1, 0, 1, 2, 3, 0x10, 0xffffffff}; // -1: computed
    for (int64_t answer : kAnswers) {
        for (const char *a : kTexts) {
            for (const char *b : kTexts) {
                for (bool caseSensitive : {false, true}) {
                    Setup setup;
                    Compare(ctx, fx.imp, setup, "Compare", [&](void *object) {
                        gCompareForced = answer >= 0;
                        gCompareAnswer = static_cast<uint32_t>(answer);
                        RecString first(a);
                        RecString second(b);
                        const int32_t r = SlotOf<CompareFn>(object, 22)(object, first.I(), second.I(), caseSensitive);
                        gCompareForced = false;
                        return std::to_string(r) + " [" + a + "] [" + b + "] " + std::to_string(caseSensitive);
                    });
                }
            }
        }
    }
}

DIFF_TEST(cRZLanguageUtility_CodePages, 0xc704ed)
{
    // Slots 23 and 24.
    Fixture fx(ctx);
    std::mt19937 rng(7123);
    static const uint32_t kPages[] = {0,     0x36a, 0x3a4, 0x3a8, 0x3b5, 0x3b6, 0x4e2, 0x4e3, 0x4e4, 0x4e5,
                                      0x4e6, 0x4e7, 0x4e8, 0x4e9, 0x4ea, 0x4b0, 0xfde9, 1, 0xffffffffu};
    for (uint32_t language : Languages(rng)) {
        Setup setup;
        setup.language = language;
        Compare(ctx, fx.imp, setup, "CodePages", [&](void *object) {
            std::string r = Hex(SlotOf<WordFn>(object, 23)(object)) + ":";
            for (uint32_t page : kPages) r += Hex(SlotOf<CodePageFn>(object, 24)(object, page) ? 1 : 0);
            r += Hex(SlotOf<CodePageFn>(object, 24)(object, rng()) ? 1 : 0);
            return r;
        });
    }
}

DIFF_TEST(cRZLanguageUtility_Rules, 0xc7224e)
{
    // Slots 25, 26 (the country, a whole word), 27, 29 and 30.
    Fixture fx(ctx);
    std::mt19937 rng(7125);
    for (uint32_t language : Languages(rng)) {
        for (uint32_t country : {0u, 0x35u, 0x36u, 0x37u, 0xffffffffu}) {
            Setup setup;
            setup.language = language;
            setup.country = country;
            Compare(ctx, fx.imp, setup, "Rules", [&](void *object) {
                return Hex(SlotOf<BoolFn>(object, 25)(object)) + Hex(SlotOf<WordFn>(object, 26)(object)) +
                       Hex(SlotOf<BoolFn>(object, 27)(object)) + Hex(SlotOf<BoolFn>(object, 29)(object)) +
                       Hex(SlotOf<BoolFn>(object, 30)(object));
            });
        }
    }
}

DIFF_TEST(cRZLanguageUtility_FormatFullName, 0xc73e0c)
{
    Fixture fx(ctx);
    std::mt19937 rng(7128);
    static const char *const kGiven[] = {"", "Ann"};
    static const char *const kFamily[] = {"", "Lee Smith"};
    for (uint32_t language : Languages(rng, 4)) {
        for (const char *given : kGiven) {
            for (const char *family : kFamily) {
                Setup setup;
                setup.language = language;
                Compare(ctx, fx.imp, setup, "FormatFullName", [&](void *object) {
                    RecString a(given);
                    RecString b(family);
                    a.s.mRefCount = 0x61; // tags: which name a slot is handed
                    b.s.mRefCount = 0x62;
                    RecString out("old text");
                    SlotOf<FullNameFn>(object, 28)(object, a.I(), b.I(), out.I());
                    return "[" + TextOf(out.s) + "]";
                });
            }
        }
    }
}

DIFF_TEST(cRZLanguageUtility_Slots, 0xc7347d)
{
    // Init (slot 3, after the separator text was changed), Shutdown (4), AddRef (1), QueryInterface (0), the setters
    // (32, 33), and Release (2) with the frees it makes.
    Fixture fx(ctx);
    static const char *const kOldTexts[] = {"", "x", "a longer text than the buffer holds"};
    for (const char *old : kOldTexts) {
        Setup setup;
        Compare(ctx, fx.imp, setup, "Init/Shutdown", [&](void *object) {
            GroupSpace(object).mString = old;
            const bool init = SlotOf<BoolFn>(object, 3)(object);
            const bool shutdown = SlotOf<BoolFn>(object, 4)(object);
            return Hex(init) + Hex(shutdown);
        });
    }
    std::mt19937 rng(7130);
    std::vector<uint32_t> iids = {0, 1, 2, cIGZLanguageUtility::kIid, 0x287fb697, 0x54b7d5};
    for (int bit = 0; bit < 32; ++bit) iids.push_back(cIGZLanguageUtility::kIid ^ (1u << bit));
    for (int i = 0; i < 64; ++i) iids.push_back(rng());
    for (uint32_t iid : iids) {
        Setup setup;
        Compare(ctx, fx.imp, setup, "QueryInterface", [&](void *object) {
            void *out = reinterpret_cast<void *>(0xface0000u);
            const bool r = SlotOf<bool(__thiscall *)(void *, uint32_t, void **)>(object, 0)(object, iid, &out);
            const std::string where = out == reinterpret_cast<void *>(0xface0000u) ? "unchanged" :
                                      out == object                                 ? "self" : "other";
            return Hex(r) + " " + where;
        });
    }
    Setup setup;
    Compare(ctx, fx.imp, setup, "AddRef", [&](void *object) {
        const uint32_t a = SlotOf<WordFn>(object, 1)(object);
        const uint32_t b = SlotOf<WordFn>(object, 1)(object);
        return Hex(a) + Hex(b);
    });
    Compare(ctx, fx.imp, setup, "Setters", [&](void *object) {
        SlotOf<void(__thiscall *)(void *, uint32_t)>(object, 32)(object, 0x1234);
        SlotOf<void(__thiscall *)(void *, uint32_t)>(object, 33)(object, 0x5678);
        return std::string();
    });

    // Release: counts 0 to 4 and two huge ones. A deleting release frees the object; it is rebuilt for the shared
    // bookkeeping (Compare frees what it built).
    for (uint32_t refs : {0u, 1u, 2u, 3u, 4u, 0x80000000u, 0xffffffffu}) {
        uint32_t results[2];
        bool freed[2];
        std::string states[2];
        for (int side = 0; side < 2; ++side) {
            void *object = side == 0 ? NewOriginal(ctx) : NewOurs();
            std::memcpy(static_cast<uint8_t *>(object) + 4, &refs, 4);
            difftest::BeginFrees();
            results[side] = SlotOf<WordFn>(object, 2)(object);
            freed[side] = difftest::FreedSinceBegin(object);
            if (!freed[side]) {
                states[side] = StateOf(object);
                if (side == 0)
                    FreeOriginal(ctx, object);
                else
                    FreeOurs(object);
            }
        }
        ++ctx.cases;
        if (results[0] != results[1] || freed[0] != freed[1] || states[0] != states[1])
            ctx.Fail("Release refs 0x%x: 0x%x freed %d / 0x%x freed %d", refs, results[0], freed[0], results[1], freed[1]);
    }
}

DIFF_TEST(cRZLanguageUtility_Destruct, 0xc6ee0d)
{
    // The destructor (0xc6ee0d, ours ~cRZLanguageUtility) and the deleting destructor (slot 31, flags 0 and 1): the
    // separator string's text is freed, the object only with bit 0.
    Fixture fx(ctx);
    for (int route = 0; route < 3; ++route) {
        bool freedObject[2];
        bool freedText[2];
        uint32_t results[2];
        for (int side = 0; side < 2; ++side) {
            void *object = side == 0 ? NewOriginal(ctx) : NewOurs();
            GroupSpace(object).mString = "a text long enough to live on the heap";
            const void *text = GroupSpace(object).mString.data();
            difftest::BeginFrees();
            uint32_t r = 0;
            if (route == 0) {
                if (side == 0)
                    ctx.Original<void(__thiscall *)(void *)>(kDtor)(object);
                else
                    static_cast<cRZLanguageUtility *>(object)->~cRZLanguageUtility();
            } else {
                void *returned = SlotOf<void *(__thiscall *)(void *, uint32_t)>(object, 31)(object, route == 1 ? 0u : 1u);
                r = returned == object ? 1 : 2;
            }
            freedObject[side] = difftest::FreedSinceBegin(object);
            freedText[side] = difftest::CurrentFrees()->Freed(text);
            results[side] = r;
            if (!freedObject[side]) ::operator delete(object);
        }
        ++ctx.cases;
        if (freedObject[0] != freedObject[1] || results[0] != results[1] || freedText[0] != freedText[1])
            ctx.Fail("destruction route %d: freed %d text %d returned %u / freed %d text %d returned %u", route,
                     freedObject[0], freedText[0], results[0], freedObject[1], freedText[1], results[1]);
        if (!freedText[0]) ctx.Fail("destruction route %d: the original kept the text", route);
    }
}

DIFF_TEST(cRZLanguageUtility_ToDefaultEncoding, 0xc6f07a)
{
    // The helper on texts the tables hold (ASCII, UTF-8 with the no-break space and a CJK name): no write.
    Fixture fx(ctx);
    static const char *const kTexts[] = {"", "Monday", "1\xc2\xa0" "000", "\xe6\x97\xa5\xe6\x9b\x9c\xe6\x97\xa5"};
    for (const char *text : kTexts) {
        std::vector<std::string> logs[2];
        std::string results[2];
        for (int side = 0; side < 2; ++side) {
            RecString s(text);
            gLog.clear();
            if (side == 0)
                ctx.Original<void(__cdecl *)(cIGZString *)>(kToDefaultEncoding)(s.I());
            else
                cRZLanguageUtility::ToDefaultEncoding(s.I());
            logs[side] = gLog;
            results[side] = TextOf(s.s);
        }
        ++ctx.cases;
        if (logs[0] != logs[1] || results[0] != results[1])
            ctx.Fail("ToDefaultEncoding [%s]: %zu writes [%s] / %zu writes [%s]", text, logs[0].size(), results[0].c_str(),
                     logs[1].size(), results[1].c_str());
    }
}

DIFF_TEST(cRZLanguageUtility_Adapters, 0xc6ed94)
{
    // The constructor: every word but the vtable's equal (the separator string compared by its vtable word, text, and
    // count); the vtable word ours, the original's the game's.
    Fixture fx(ctx);
    void *a = NewOriginal(ctx);
    void *b = NewOurs();
    ++ctx.cases;
    if (Word(a, 0) != static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GameAddress(cRZLanguageUtility::kVTable))))
        ctx.Fail("original constructor: vtable word not the game's");
    if (*static_cast<void **>(b) == GameAddress(cRZLanguageUtility::kVTable)) ctx.Fail("ours: the game's vtable");
    if (StateOf(a) != StateOf(b)) ctx.Fail("constructor state [%s] / [%s]", StateOf(a).c_str(), StateOf(b).c_str());
    FreeOriginal(ctx, a);
    FreeOurs(b);
}
