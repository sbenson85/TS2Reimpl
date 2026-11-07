// Differential tests for the number formatting of the set-float and set-vector commands
// (FormatDecimal 0x1b111e, FormatGeneral 0x1b128f, PrintDoubles 0xb02740) and for those commands.
#include "command_parser_env.h"

#include <cmath>
#include <limits>

namespace {

using FormatFn = uint8_t(__cdecl *)(double, char *, int, int);
using PrintFn = uint8_t(__cdecl *)(char *, int, const char *, ...);

constexpr size_t kBuf = 96; // a buffer with room after `size`, to see writes past it

double RandomValue(std::mt19937 &gen)
{
    static const double fixed[] = {0.0,
                                   -0.0,
                                   1.0,
                                   -1.0,
                                   0.5,
                                   0.05,
                                   0.001,
                                   0.0001,
                                   0.00001,
                                   123.456,
                                   -123.456,
                                   999999.0,
                                   1000000.0,
                                   1234567.0,
                                   0.1,
                                   0.3,
                                   100.0,
                                   10.0,
                                   1e5,
                                   1e6,
                                   1e7,
                                   123456789.0,
                                   1e15,
                                   1e22,
                                   1e-5,
                                   1e-7,
                                   1.5e-9,
                                   -2.5e-12,
                                   1e-100,
                                   1e-300,
                                   5e-324,
                                   1.7976931348623157e308,
                                   -1.7976931348623157e308,
                                   1.0000001,
                                   0.99999995,
                                   9.9999995,
                                   99999.95,
                                   0.000099999995,
                                   std::numeric_limits<double>::infinity(),
                                   -std::numeric_limits<double>::infinity(),
                                   std::numeric_limits<double>::quiet_NaN()};
    uint32_t pick = gen() % 8;
    if (pick < 3) return fixed[gen() % (sizeof(fixed) / sizeof(fixed[0]))];
    if (pick == 3) return std::pow(10.0, static_cast<int>(gen() % 40) - 12) * (gen() % 2 ? 1 : -1);        // exact powers of ten
    if (pick == 4) return static_cast<double>(static_cast<int32_t>(gen())) / (1 + gen() % 1000);               // ratios
    double mantissa = (gen() % 1000000) / 1000.0;
    return mantissa * std::pow(10.0, static_cast<int>(gen() % 30) - 12) * (gen() % 2 ? 1 : -1);
}

const int kSizes[] = {-3, 0, 1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 32, 40, 64};
const int kPrecisions[] = {-1, 0, 1, 2, 3, 5, 6, 8, 12};

template <class Ours> void FormatTest(difftest::Context &ctx, uint32_t rva, const char *name, Ours ours, int cases)
{
    auto orig = ctx.Original<FormatFn>(rva);
    std::mt19937 gen(rva);
    for (int i = 0; i < cases; ++i) {
        alignas(8) uint8_t buf[kBuf];
        for (auto &b : buf) b = static_cast<uint8_t>(gen());
        double value = RandomValue(gen);
        int size = kSizes[gen() % (sizeof(kSizes) / sizeof(kSizes[0]))];
        // Most calls use the precision the commands do.
        int precision = gen() % 3 ? 6 : kPrecisions[gen() % (sizeof(kPrecisions) / sizeof(kPrecisions[0]))];
        char what[120];
        std::snprintf(what, sizeof(what), "%s(%.17g, size %d, precision %d)", name, value, size, precision);
        Differential(
            ctx, what, {{buf, kBuf}},
            [&] {
                Outcome o;
                o.ret = orig(value, reinterpret_cast<char *>(buf), size, precision) & 0xff;
                return o;
            },
            [&] {
                Outcome o;
                o.ret = ours(value, reinterpret_cast<char *>(buf), size, precision) ? 1 : 0;
                return o;
            });
    }
}

} // namespace

DIFF_TEST(CommandParser_FormatDecimal, 0x1b111e)
{
    FormatTest(ctx, 0x1b111e, "FormatDecimal", &cp::FormatDecimal, 6000);
}

DIFF_TEST(CommandParser_FormatGeneral, 0x1b128f)
{
    FormatTest(ctx, 0x1b128f, "FormatGeneral", &cp::FormatGeneral, 8000);
}

DIFF_TEST(CommandParser_PrintDoubles, 0xb02740)
{
    auto orig = ctx.Original<PrintFn>(0xb02740);
    std::mt19937 gen(0xb02740);
    static const char *const formats[] = {"(%g,%g)", "(%g,%g,%g)", "(%g,%g,%g,%g)", "", "plain text", "a%gb", "%g%g", "%g", "x=%g y=%g z=%g",
                                          "%gX%gX%gX%g"};
    for (int i = 0; i < 3000; ++i) {
        alignas(8) uint8_t buf[kBuf];
        for (auto &b : buf) b = static_cast<uint8_t>(gen());
        const char *fmt = formats[gen() % (sizeof(formats) / sizeof(formats[0]))];
        double v[4];
        for (auto &d : v) d = gen() % 3 == 0 ? static_cast<double>(static_cast<float>(RandomValue(gen))) : RandomValue(gen);
        static const int sizes[] = {0, 1, 2, 5, 8, 13, 20, 30, 44, 65, 85};
        int size = sizes[gen() % (sizeof(sizes) / sizeof(sizes[0]))];
        char what[120];
        std::snprintf(what, sizeof(what), "PrintDoubles(size %d, \"%s\", %.9g ..)", size, fmt, v[0]);
        Differential(
            ctx, what, {{buf, kBuf}},
            [&] {
                Outcome o;
                o.ret = orig(reinterpret_cast<char *>(buf), size, fmt, v[0], v[1], v[2], v[3]) & 0xff;
                return o;
            },
            [&] {
                Outcome o;
                o.ret = cp::PrintDoubles(reinterpret_cast<char *>(buf), size, fmt, v[0], v[1], v[2], v[3]) ? 1 : 0;
                return o;
            });
    }
}

// ---- the commands that use them ------------------------------------------------------------------

namespace {

// Parser slots that read a vector of floats from the argument text (slots 34 to 37), or one double
// (slot 31, the result in st(0)); the values they give are configured.
float gFloats[4];
double gDouble;

template <int S, int N> uint32_t __fastcall ParserVector(void *self, void *, float *out, uint32_t text, uint32_t count)
{
    Record(self, S, 0, text, count); // the output pointer is a stack address
    for (int i = 0; i < N; ++i) out[i] = gFloats[i];
    return 0;
}

uint32_t __fastcall ParserVector37(void *self, void *, float *out, uint32_t text)
{
    Record(self, 37, 0, text);
    for (int i = 0; i < 4; ++i) out[i] = gFloats[i];
    return 0;
}

double __fastcall ParserDouble(void *self, void *, uint32_t text)
{
    Record(self, 31, text);
    return gDouble;
}

// The text the state's slot 1 gets (a stack buffer: the address is not logged, the text is compared).
uint32_t __fastcall StateSetText(void *self, void *, uint32_t name, uint32_t value)
{
    Record(self, 1, name, 0);
    const char *s = reinterpret_cast<const char *>(value);
    gExtra.insert(gExtra.end(), s, s + std::strlen(s) + 1);
    return 0;
}

void RandomFloats(std::mt19937 &gen)
{
    static const float values[] = {0.0f, 1.0f, -1.0f, 0.5f, 1.5f, -2.25f, 123.456f, 1e-5f, 3.4e38f, 1e-38f, 100000.0f, 1234567.0f, 0.1f, -0.0f};
    for (auto &f : gFloats) f = gen() % 4 ? values[gen() % (sizeof(values) / sizeof(values[0]))] : static_cast<float>(RandomValue(gen));
}

} // namespace

DIFF_TEST(cSetV2Command_Execute, 0xb01d40)
{
    ExecuteTest<cp::cSetV2Command>(
        ctx, 0xb01d40, "cSetV2Command::Execute", &cp::cSetV2Command::Execute,
        [](Env &e) {
            RandomFloats(e.gen);
            e.parserVt.slots[34] = reinterpret_cast<void *>(&ParserVector<34, 2>);
            e.stateVt.slots[1] = reinterpret_cast<void *>(&StateSetText);
        },
        200);
}

DIFF_TEST(cSetV3Command_Execute, 0xb01df0)
{
    ExecuteTest<cp::cSetV3Command>(
        ctx, 0xb01df0, "cSetV3Command::Execute", &cp::cSetV3Command::Execute,
        [](Env &e) {
            RandomFloats(e.gen);
            e.parserVt.slots[35] = reinterpret_cast<void *>(&ParserVector<35, 3>);
            e.stateVt.slots[1] = reinterpret_cast<void *>(&StateSetText);
        },
        200);
}

DIFF_TEST(cSetV4Command_Execute, 0xb01eb0)
{
    ExecuteTest<cp::cSetV4Command>(
        ctx, 0xb01eb0, "cSetV4Command::Execute", &cp::cSetV4Command::Execute,
        [](Env &e) {
            RandomFloats(e.gen);
            e.parserVt.slots[36] = reinterpret_cast<void *>(&ParserVector<36, 4>);
            e.stateVt.slots[1] = reinterpret_cast<void *>(&StateSetText);
        },
        200);
}

DIFF_TEST(cSetCCommand_Execute, 0xb01a30)
{
    ExecuteTest<cp::cSetCCommand>(
        ctx, 0xb01a30, "cSetCCommand::Execute", &cp::cSetCCommand::Execute,
        [](Env &e) {
            RandomFloats(e.gen);
            e.parserVt.slots[37] = reinterpret_cast<void *>(&ParserVector37);
            e.stateVt.slots[1] = reinterpret_cast<void *>(&StateSetText);
        },
        200);
}

DIFF_TEST(cSetFCommand_Execute, 0xb01c40)
{
    ExecuteTest<cp::cSetFCommand>(
        ctx, 0xb01c40, "cSetFCommand::Execute", &cp::cSetFCommand::Execute,
        [](Env &e) {
            gDouble = RandomValue(e.gen);
            e.parserVt.slots[31] = reinterpret_cast<void *>(&ParserDouble);
            e.stateVt.slots[1] = reinterpret_cast<void *>(&StateSetText);
        },
        300);
}
