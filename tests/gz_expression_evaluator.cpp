// Differential tests for nGZCommandParser's expression evaluator (src/gz/ExpressionEvaluator.h, lane NO): the parse
// levels, their helpers, the 25 built-in functions (cEval*) and rztext::FindNoCase.
//
// Two function tables run side by side. The original's: our EvalFunctionTable constructor (verified, lane HL) over the
// image's 27 built-ins, so its entries point at the game's static cEval objects and their game vtables; the original
// evaluator called on it reaches only original code. Ours: the same names in the same order, each pointing at a stand-in
// object whose one-slot vtable holds our member for that class (what the hooks do to the game's objects in game), so
// our evaluator called on it reaches only our code.
//
// Every case compares, from the same starting state: the result buffer (all 0x14 bytes, pre-filled with the same
// random bytes, so partial writes before an error show), the cursor, the returned pointer (as an offset), what was
// thrown (the cError's vtable word, message and type list), and rnd's generator state. Floats are compared by bits.
//
// The image's CRT math entries (exp, log, sin, ...: 0xc4aff0 and its neighbours) choose an SSE2 or an x87 body by the
// flag at 0x110a38c, which the game's CRT start-up sets on any SSE2 machine and the harness (no initialisation) leaves
// 0. The tests set it to 1, the game's state, for their duration.
//
// Left out: strstr with an empty first text (ORIGINAL-BUG in ExpressionFunctions.cpp: a string of length -1, so each
// side throws its own STL's std::length_error, an object the cError-shaped exception capture cannot read); see
// ReachesEmptyStrstr.
#define NOMINMAX
#include "command_parser.h"
#include "difftest.h"

#include "gz/EvalFunctionTable.h"
#include "gz/ExpressionEvaluator.h"
#include "hook/hooks.h"
#include "rz/RZStringSearch.h"
#include "rz/cRZRandom.h"

#include <bit>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace nGZCommandParser;

namespace {

constexpr uint32_t kSse2Flag = 0x110a38c;  // the CRT's "use the SSE2 math bodies" flag
constexpr uint32_t kRndObject = 0x10faa48; // rnd's function-local cRZRandom
constexpr uint32_t kRndGuard = 0x10faa60;  // its thread-safe-static guard (INT_MIN: never initialised by the call)

// The built-ins: vtable, slot-0 body, our member.
struct EvalClass {
    uint32_t vtable;
    uint32_t slot0;
    void *ours;
    const char *name;
};

const std::vector<EvalClass> &EvalClasses()
{
    static const std::vector<EvalClass> classes = {
        {0xe7f4b8, 0xb07bb0, MemberAddress(&cEvalTrue::Evaluate), "true"},
        {0xe7f4c0, 0xb07bd0, MemberAddress(&cEvalFalse::Evaluate), "false"},
        {0xe7f4c8, 0xb07bf0, MemberAddress(&cEvalPI::Evaluate), "pi"},
        {0xe7f4d0, 0xb07c10, MemberAddress(&cEvalRnd::Evaluate), "rnd"},
        {0xe7f4d8, 0xb07d20, MemberAddress(&cEvalExp::Evaluate), "exp"},
        {0xe7f4e0, 0xb07d90, MemberAddress(&cEvalLog::Evaluate), "log"},
        {0xe7f4e8, 0xb07e00, MemberAddress(&cEvalAbs::Evaluate), "abs"},
        {0xe7f4f0, 0xb07e70, MemberAddress(&cEvalSin::Evaluate), "sin"},
        {0xe7f4f8, 0xb07ee0, MemberAddress(&cEvalCos::Evaluate), "cos"},
        {0xe7f500, 0xb07f50, MemberAddress(&cEvalTan::Evaluate), "tan"},
        {0xe7f508, 0xb07fc0, MemberAddress(&cEvalSqr::Evaluate), "sqr"},
        {0xe7f510, 0xb08090, MemberAddress(&cEvalPow::Evaluate), "pow"},
        {0xe7f518, 0xb08110, MemberAddress(&cEvalSqrt::Evaluate), "sqrt"},
        {0xe7f520, 0xb08180, MemberAddress(&cEvalCeil::Evaluate), "ceil"},
        {0xe7f528, 0xb081f0, MemberAddress(&cEvalAsin::Evaluate), "asin"},
        {0xe7f530, 0xb08260, MemberAddress(&cEvalAcos::Evaluate), "acos"},
        {0xe7f538, 0xb082d0, MemberAddress(&cEvalAtan::Evaluate), "atan"},
        {0xe7f540, 0xb08340, MemberAddress(&cEvalFloor::Evaluate), "floor"},
        {0xe7f548, 0xb083b0, MemberAddress(&cEvalAtan2::Evaluate), "atan2"},
        {0xe7f550, 0xb08450, MemberAddress(&cEvalStrcmp::Evaluate), "strcmp"},
        {0xe7f558, 0xb08510, MemberAddress(&cEvalStrstr::Evaluate), "strstr"},
        {0xe7f560, 0xb086d0, MemberAddress(&cEvalRadiansToTurns::Evaluate), "rad2turn"},
        {0xe7f568, 0xb08730, MemberAddress(&cEvalDegreesToTurns::Evaluate), "deg2turn"},
        {0xe7f570, 0xb08790, MemberAddress(&cEvalRadiansToDegrees::Evaluate), "rad2deg"},
        {0xe7f578, 0xb08860, MemberAddress(&cEvalDegreesToRadians::Evaluate), "deg2rad"},
    };
    return classes;
}

// A stand-in for one of the game's cEval objects: a vtable whose slot 0 is our member.
struct StandIn {
    void *const *vtable;
};

struct Tables {
    EvalFunctionTable original;                         // the game's objects
    std::vector<EvalFunctionTable::Entry> oursEntries;  // our stand-ins (laid out as the table)
    std::vector<void *> vtables;                        // one slot each
    std::vector<StandIn> standIns;
    std::vector<void *> gameObjects;                    // per class, the game's object
    explicit Tables(difftest::Context &ctx)
    {
        const auto &classes = EvalClasses();
        vtables.resize(classes.size());
        standIns.resize(classes.size());
        gameObjects.resize(classes.size(), nullptr);
        for (size_t i = 0; i < classes.size(); ++i) {
            vtables[i] = classes[i].ours;
            standIns[i].vtable = &vtables[i];
        }
        const uintptr_t base = reinterpret_cast<uintptr_t>(ctx.image.At<uint8_t>(0));
        for (const auto &entry : original.mEntries) {
            const uint32_t vtable = static_cast<uint32_t>(*reinterpret_cast<const uintptr_t *>(entry.second) - base);
            size_t which = classes.size();
            for (size_t i = 0; i < classes.size(); ++i)
                if (classes[i].vtable == vtable) which = i;
            if (which == classes.size()) {
                ctx.Fail("built-in '%s': unknown vtable %08x", entry.first, vtable);
                oursEntries.push_back(entry);
                continue;
            }
            gameObjects[which] = entry.second;
            oursEntries.push_back({entry.first, reinterpret_cast<cEvalFunction *>(&standIns[which])});
        }
    }
    void *OriginalTable() { return &original; }
    ExpressionEvaluator *Ours() { return reinterpret_cast<ExpressionEvaluator *>(&oursEntries); }
};

// The game's CRT state for the duration of a test (see the file comment).
struct Sse2Math {
    uint32_t *flag;
    uint32_t saved;
    explicit Sse2Math(difftest::Context &ctx) : flag(ctx.image.At<uint32_t>(kSse2Flag)), saved(*flag) { *flag = 1; }
    ~Sse2Math() { *flag = saved; }
    Sse2Math(const Sse2Math &) = delete;
    Sse2Math &operator=(const Sse2Math &) = delete;
};

// rnd's generators: before each side runs, both start from the same state; afterwards their states are compared.
struct RandomPair {
    cRZRandom *original;
    cRZRandom *ours;
    cRZRandom start;
    explicit RandomPair(difftest::Context &ctx)
        : original(ctx.image.At<cRZRandom>(kRndObject)), ours(&EvalRandomGenerator()), start(*ours)
    {
        *ctx.image.At<int32_t>(kRndGuard) = INT_MIN;
    }
    void Begin(uint32_t seed)
    {
        start.mIntegerSeed = seed;
        start.mRealSeed = static_cast<int32_t>(seed * 2654435761u);
        *original = start;
        *ours = start;
    }
    bool Same() const { return std::memcmp(original, ours, sizeof(cRZRandom)) == 0; }
};

bool SameOutcome(const cptest::Outcome &a, const cptest::Outcome &b) { return a.threw == b.threw && a.detail == b.detail; }

// strstr (or a prefix of its name that finds it: "strs", "strst") with an empty first text: each side throws its own
// STL's std::length_error, which the exception capture (made for cError's layout) cannot read. Such texts are left
// out (over-broadly: any "strs..." name followed by ("").
bool ReachesEmptyStrstr(const std::string &text)
{
    for (size_t at = text.find("strs"); at != std::string::npos; at = text.find("strs", at + 1)) {
        size_t p = at + 4;
        while (p < text.size() && std::isalnum(static_cast<unsigned char>(text[p]))) ++p;
        auto blanks = [&] {
            while (p < text.size() && std::strchr(" \t\r\n", text[p]) != nullptr && text[p] != '\0') ++p;
        };
        blanks();
        if (p >= text.size() || text[p] != '(') continue;
        ++p;
        blanks();
        if (p + 1 < text.size() && text[p] == '"' && text[p + 1] == '"') return true;
    }
    return false;
}

std::string Hex(const void *p, size_t n)
{
    std::string s;
    char buf[4];
    for (size_t i = 0; i < n; ++i) {
        std::snprintf(buf, sizeof(buf), "%02x", static_cast<const uint8_t *>(p)[i]);
        s += buf;
    }
    return s;
}

// One case of a function taking (out, cursor) on a text: both sides from the same buffer bytes; compares the outcome,
// the returned offset, the buffer and the cursor's offset.
template <class Orig, class Ours>
void CompareOnText(difftest::Context &ctx, const char *what, const std::string &text, uint32_t fill, RandomPair *rnd, Orig orig,
                   Ours ours)
{
    std::mt19937 bytes(fill);
    uint8_t initial[sizeof(Value)];
    for (uint8_t &b : initial) b = static_cast<uint8_t>(bytes());
    Value outA, outB;
    std::memcpy(&outA, initial, sizeof(Value));
    std::memcpy(&outB, initial, sizeof(Value));
    const char *cursorA = text.c_str(), *cursorB = text.c_str();
    if (rnd) rnd->Begin(fill);
    cptest::Outcome oa = cptest::RunGuarded([&] { return reinterpret_cast<uintptr_t>(orig(&outA, &cursorA)); });
    cRZRandom afterA = rnd ? *rnd->original : cRZRandom{};
    if (rnd) *rnd->ours = rnd->start;
    cptest::Outcome ob = cptest::RunGuarded([&] { return reinterpret_cast<uintptr_t>(ours(&outB, &cursorB)); });
    ++ctx.cases;
    const bool retOk = oa.threw || (oa.ret == reinterpret_cast<uintptr_t>(&outA) && ob.ret == reinterpret_cast<uintptr_t>(&outB));
    const bool rndOk = !rnd || std::memcmp(&afterA, rnd->ours, sizeof(cRZRandom)) == 0;
    if (!SameOutcome(oa, ob) || !retOk || std::memcmp(&outA, &outB, sizeof(Value)) != 0 || cursorA - text.c_str() != cursorB - text.c_str() ||
        !rndOk) {
        ctx.Fail("%s('%s'): out %s / %s, cursor +%d / +%d, ret %s, rnd %s", what, cptest::Printable(text).c_str(), Hex(&outA, sizeof(Value)).c_str(),
                 Hex(&outB, sizeof(Value)).c_str(), static_cast<int>(cursorA - text.c_str()), static_cast<int>(cursorB - text.c_str()),
                 retOk ? "ok" : "differs", rndOk ? "ok" : "differs");
        if (!SameOutcome(oa, ob)) {
            ctx.Fail("   original: %s%.200s", oa.threw ? "threw " : "returned ", cptest::Printable(oa.detail).c_str());
            ctx.Fail("   ours:     %s%.200s", ob.threw ? "threw " : "returned ", cptest::Printable(ob.detail).c_str());
        }
    }
}

// ---- texts ---------------------------------------------------------------------------------------------------------

const char *const kFixed[] = {
    "1", "  1.5  ", "1+2*3", "(1+2)*3", "1-2-3", "2/3", "1/0", "1%0", "7%3", "-7.5%2", "7.5%-2", "1/(1e-11)", "1/2e-10",
    "5 % 0.0000000001", "1e30*1e30", "-(1e30*1e30)", "1e39", "1e-50", "0.1+0.2", "1/3*3", "16777217+1", "-0", "0*-1",
    "1,2,3", "1,2,3,4", "1,2,3,4,5", "(1,2),(3,4)", "(1,2),(3,4,5)", "(1,2,3)+(1)", "(1,2)*(3,4,5)", "(1,2,3)/2", "(1,2,3)/(2,0)",
    "(4,8)%(3)", "(4,8,9)%(3,0.5)", "(1,2)-(5,6,7,8)", "-(1,2,3)", "+(1,2)", "--1", "+-+1", "- - 2",
    "1<2", "2<=2", "1=1", "1==1", "1!=2", "1!2", "1 ! = 2", "(1,2)<(1,3)", "(1,2)<(1)", "(1)<(1,2)", "1>2>0", "(1,2)=(1,2)",
    "1 < 2 = 1", "2>=3", "3>=3", "4>3", "(1,2,3)>(1,2)", "(1,2)>=(1,2,0)", "1<>2", "1=<2", "1=>2",
    "1 and 0", "1 or 0", "1and0", "1 andx 0", "(1,2) and 1", "1 and (1,2)", "0 or 0", "2 or 0 and 1", "1 and", "1 or",
    "not 1", "not 0", "not (1,2)", "notx", "not1", "not not 2", "not", "not(0)", "nota",
    "1?2:3", "0?2:3", "(1,2)?3:4", "1?(1,2):3", "1?2", "1?2:", "1 ? 2 : 3 ? 4 : 5", "0?1:(1,2,3)", "1?2;3", "1,2?3:4",
    "(1,2,3,4).wzyx", "(1,2,3).xxxxx", "(1,2,3).xxxx", "(1,2).q", "(1,2). x", "(1,2) .x", "(1,2).", "1.x", "(5).yzw", "pi.xy",
    "(1,2,3).zyx.xy", "-(1,2).yx", "sin(1,2).y", "(1,2).xy+(3).x", "(1,2,3,4).",
    "true", "on", "false", "off", "pi", "pi*2", "true+1", "tru", "o", "of", "p",
    "rnd(1,2)", "rnd(5)", "rnd(2,1)", "rnd(0,0)", "rnd(1,2)+rnd(3,4)", "rnd()", "rnd(1,2", "rnd",
    "exp(1)", "exp(100)", "exp(-100)", "log(0)", "log(-1)", "log(2,3)", "abs(-2,3)", "abs(-0)", "sin(1,2,3,4)", "cos(pi)",
    "tan(1)", "tan(pi/2)", "sqr(3,4)", "sqr(1e30)", "pow(2,10)", "pow(2)", "pow(-8,1/3)", "pow(0,-1)", "sqrt(2)", "sqrt(-1)",
    "ceil(1.5,-1.5)", "asin(2)", "asin(0.5)", "acos(0.5)", "acos(-2)", "atan(1)", "atan(1e30)", "floor(-0.5)",
    "floor(2.5,3.5,-4.5,5)", "atan2(1,2)", "atan2(1)", "atan2(-1,-2)", "atan2(0,-1)",
    "strcmp(\"a\",\"b\")", "strcmp(\"abc\",\"ab\")", "strcmp(\"ab\",\"abc\")", "strcmp(\"\",\"\")", "strcmp(\"b\", \"a\")",
    "strcmp( \"x\" , \"x\" )", "strcmp(\"a\")", "strcmp(\"a\"", "strcmp(\"a", "strcmp(a,b)", "strcmp(\"\xe9\",\"e\")",
    "strstr(\"hello\",\"ELL\")", "strstr(\"hello\",\"h\")", "strstr(\"hello\",\"e\")", "strstr(\"x\",\"\")",
    "strstr(\"abcabc\",\"CA\")", "strstr(\"a\",\"a\")", "strstr(\"\",\"a\")", "strstr(\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab\",\"AB\")",
    "rad2turn(1,2)", "deg2turn(360)", "deg2turn(90,180,270,-45)", "rad2deg(pi)", "deg2rad(180)", "deg2rad(1,2,3,4)",
    "rad2deg(1e38)", "s(1)", "sq(3)", "sqrt(4)+sqr(2)", "at(1)", "atan2", "st(\"a\",\"b\")", "r(1,2)", "d(1)", "deg(90)",
    "unknown", "zzz(1)", "sin", "sin 1", "sin(1", "sin()", "sin((1))", "sin(1,2,3,4,5)", "Sin(1)", "PI",
    "", "   ", "*1", "1 2", "1)", "(1", "#", "1 +", "1.2.3", ".", ".5", "-.5", "+.5e1", "1e", "1e+", "5.e-1", "(", ")",
    "\t1\r\n+\n2 ", "( 1 , 2 ) . xy", "\x80", "1+\xff", "_a", "a_b", "1 ,", ",1", "1,,2", "((((1))))", "(((1)", "1 % 1e-10",
    "0/0", "sqrt(-1)", "not sqrt(-1)", "sqrt(-1) and 1", "0 or sqrt(-1)", "sqrt(-1) < 1", "sqrt(-1) = sqrt(-1)",
    "sqrt(-1)?1:2", "1/sqrt(-1)", "5%sqrt(-1)", "(1,sqrt(-1))<(1,2)", "(sqrt(-1),1)>(1,2)", "1 % 0.5 % 0.3",
};

const char *const kNames[] = {"true", "on", "false", "off", "pi", "rnd", "exp", "log", "abs", "sin", "cos", "tan", "sqr",
                              "pow", "sqrt", "ceil", "asin", "acos", "atan", "floor", "atan2", "rad2turn", "deg2turn",
                              "rad2deg", "deg2rad", "si", "a", "x", "notan", "pie"};

// Random expressions: numbers, names, operators, sequences, swizzles, calls and blanks, from the grammar's pieces;
// some then get a character replaced, inserted or removed.
struct Generator {
    std::mt19937 &g;
    explicit Generator(std::mt19937 &gen) : g(gen) {}
    unsigned Pick(unsigned n) { return static_cast<unsigned>(g() % n); }
    std::string Blank()
    {
        static const char *const blanks[] = {"", "", "", " ", "  ", "\t", "\n", "\r\n "};
        return blanks[Pick(8)];
    }
    std::string Number()
    {
        static const char *const special[] = {"0", "1", "2", "0.5", "1e-10", "1.2e-10", "1e-11", "3e38", "1e39", "16777216",
                                              "0.1", ".25", "7.", "1e5", "123456789", "0.000001", "2.5e+2"};
        if (Pick(3) == 0) return special[Pick(sizeof(special) / sizeof(special[0]))];
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.*f", static_cast<int>(Pick(4)), (g() % 20000) / 100.0);
        return buf;
    }
    std::string Quoted()
    {
        static const char *const texts[] = {"", "a", "A", "ab", "aB", "abc", "hello", "HeLLo", "lo", "LL", "xyz", "b", "abcabc"};
        return std::string("\"") + texts[Pick(sizeof(texts) / sizeof(texts[0]))] + "\"";
    }
    std::string Expr(int depth)
    {
        if (depth <= 0) return Number();
        switch (Pick(14)) {
        case 0: case 1: return Number();
        case 2: return Blank() + Expr(depth - 1) + Blank() + "+-*/%"[Pick(5)] + Blank() + Expr(depth - 1);
        case 3: {
            static const char *const ops[] = {"<", "<=", "=", ">=", ">", "!=", " and ", " or "};
            return Expr(depth - 1) + Blank() + ops[Pick(8)] + Blank() + Expr(depth - 1);
        }
        case 4: {
            std::string s = "(" + Expr(depth - 1);
            for (unsigned n = Pick(4); n > 0; --n) s += "," + Blank() + Expr(depth - 1);
            return s + ")";
        }
        case 5: return "-" + Expr(depth - 1);
        case 6: return (Pick(2) ? "not " : "+") + Expr(depth - 1);
        case 7: {
            std::string s = "(" + Expr(depth - 1) + ")" + Blank() + ".";
            for (unsigned n = Pick(6); n > 0; --n) s += "xyzwq"[Pick(5)];
            return s;
        }
        case 8: return Expr(depth - 1) + Blank() + "?" + Blank() + Expr(depth - 1) + Blank() + ":" + Blank() + Expr(depth - 1);
        case 9: case 10: {
            std::string s = kNames[Pick(sizeof(kNames) / sizeof(kNames[0]))];
            if (Pick(5) != 0) s += Blank() + "(" + Expr(depth - 1) + (Pick(2) ? "," + Expr(depth - 1) : "") + ")";
            return s;
        }
        case 11: return std::string(Pick(2) ? "strcmp" : "strstr") + "(" + Blank() + Quoted() + Blank() + "," + Quoted() + Blank() + ")";
        case 12: return "(" + Expr(depth - 1) + ")";
        default: return Expr(depth - 1) + "," + Expr(depth - 1);
        }
    }
    std::string Text()
    {
        std::string s = Blank() + Expr(1 + Pick(4)) + Blank();
        if (Pick(4) == 0 && !s.empty()) {
            static const char junk[] = "()+-*/%,.?:!<>=\"xyzw 0123456789aeinost\x80";
            const size_t at = Pick(static_cast<unsigned>(s.size()));
            switch (Pick(3)) {
            case 0: s[at] = junk[Pick(sizeof(junk) - 1)]; break;
            case 1: s.insert(s.begin() + at, junk[Pick(sizeof(junk) - 1)]); break;
            default: s.erase(at, 1); break;
            }
        }
        return s;
    }
};

std::vector<std::string> Texts(uint32_t seed, int random)
{
    std::vector<std::string> texts(std::begin(kFixed), std::end(kFixed));
    std::mt19937 g(seed);
    Generator gen(g);
    for (int i = 0; i < random; ++i) texts.push_back(gen.Text());
    std::erase_if(texts, ReachesEmptyStrstr);
    return texts;
}

using OrigLevel = Value *(__thiscall *)(void *, Value *, const char **);
using OursLevel = Value *(ExpressionEvaluator::*)(Value *, const char **);

void CompareLevel(difftest::Context &ctx, const char *what, uint32_t rva, OursLevel ours, int random, uint32_t seed)
{
    Sse2Math sse2(ctx);
    Tables tables(ctx);
    RandomPair rnd(ctx);
    auto orig = ctx.Original<OrigLevel>(rva);
    uint32_t fill = seed;
    for (const std::string &text : Texts(seed, random)) {
        CompareOnText(
            ctx, what, text, ++fill, &rnd, [&](Value *out, const char **cursor) { return orig(tables.OriginalTable(), out, cursor); },
            [&](Value *out, const char **cursor) { return (tables.Ours()->*ours)(out, cursor); });
    }
}

} // namespace

// ---- the entry point and the levels ------------------------------------------------------------------------------

DIFF_TEST(ExpressionEvaluator_Evaluate, 0xb0a870)
{
    Sse2Math sse2(ctx);
    Tables tables(ctx);
    RandomPair rnd(ctx);
    auto orig = ctx.Original<Value *(__thiscall *)(void *, Value *, const char *)>(0xb0a870);
    uint32_t fill = 0xa870;
    // The text is passed by value: the cursor argument is unused (the same text pointer each side).
    for (const std::string &text : Texts(0xa870, 30000)) {
        CompareOnText(
            ctx, "Evaluate", text, ++fill, &rnd, [&](Value *out, const char **cursor) { return orig(tables.OriginalTable(), out, *cursor); },
            [&](Value *out, const char **cursor) { return tables.Ours()->Evaluate(out, *cursor); });
    }
}

DIFF_TEST(ExpressionEvaluator_ParseExpression, 0xb09940)
{
    CompareLevel(ctx, "ParseExpression", 0xb09940, &ExpressionEvaluator::ParseExpression, 6000, 0x9940);
}

DIFF_TEST(ExpressionEvaluator_ParseSequence, 0xb097e0)
{
    CompareLevel(ctx, "ParseSequence", 0xb097e0, &ExpressionEvaluator::ParseSequence, 6000, 0x97e0);
}

DIFF_TEST(ExpressionEvaluator_ParseLogical, 0xb099e0)
{
    CompareLevel(ctx, "ParseLogical", 0xb099e0, &ExpressionEvaluator::ParseLogical, 6000, 0x99e0);
}

DIFF_TEST(ExpressionEvaluator_ParseComparison, 0xb0a200)
{
    CompareLevel(ctx, "ParseComparison", 0xb0a200, &ExpressionEvaluator::ParseComparison, 6000, 0xa200);
}

DIFF_TEST(ExpressionEvaluator_ParseSum, 0xb09740)
{
    CompareLevel(ctx, "ParseSum", 0xb09740, &ExpressionEvaluator::ParseSum, 6000, 0x9740);
}

DIFF_TEST(ExpressionEvaluator_ParseProduct, 0xb09c50)
{
    CompareLevel(ctx, "ParseProduct", 0xb09c50, &ExpressionEvaluator::ParseProduct, 6000, 0x9c50);
}

DIFF_TEST(ExpressionEvaluator_ParseUnary, 0xb0a660)
{
    CompareLevel(ctx, "ParseUnary", 0xb0a660, &ExpressionEvaluator::ParseUnary, 6000, 0xa660);
}

DIFF_TEST(ExpressionEvaluator_ParsePrimary, 0xb09f50)
{
    CompareLevel(ctx, "ParsePrimary", 0xb09f50, &ExpressionEvaluator::ParsePrimary, 6000, 0x9f50);
}

// ---- the built-ins, called directly with the cursor after their name -------------------------------------------------

DIFF_TEST(ExpressionEvaluator_Builtins, 0xb07c10)
{
    // Slot bodies: 0xb07bb0 0xb07bd0 0xb07bf0 0xb07c10 0xb07d20 0xb07d90 0xb07e00 0xb07e70 0xb07ee0 0xb07f50 0xb07fc0
    // 0xb08090 0xb08110 0xb08180 0xb081f0 0xb08260 0xb082d0 0xb08340 0xb083b0 0xb08450 0xb08510 0xb086d0 0xb08730
    // 0xb08790 0xb08860 (the table in EvalClasses).
    Sse2Math sse2(ctx);
    Tables tables(ctx);
    RandomPair rnd(ctx);
    std::mt19937 g(0x7c10);
    Generator gen(g);
    using Slot = Value *(__thiscall *)(void *, Value *, const char **, void *);
    const auto &classes = EvalClasses();
    for (size_t i = 0; i < classes.size(); ++i) {
        if (!tables.gameObjects[i]) {
            ctx.Fail("%s: no game object in the table", classes[i].name);
            continue;
        }
        auto orig = ctx.Original<Slot>(classes[i].slot0);
        auto ours = reinterpret_cast<Slot>(classes[i].ours);
        std::vector<std::string> args = {"", "()", "(1)", "(1,2)", "(1,2,3)", "(1,2,3,4)", "( 0.5 , -0.25 )", "(", "(1", "1)",
                                         " (2)", "(\"a\",\"b\")", "(\"abc\", \"B\")", "(\"\",\"\")", "(\"x\",\"\")", "(\"a\"",
                                         "(\"ab\",\"b\"", "(-1,-2,-3,-4)", "(1e30,1e-30)", "(sqrt(-1),1)", "(pi,-pi)", "(1,2).x"};
        for (int n = 0; n < 600; ++n) {
            switch (g() % 3) {
            case 0: args.push_back(gen.Blank() + "(" + gen.Expr(1 + g() % 3) + ")" + gen.Blank()); break;
            case 1: args.push_back("(" + gen.Number() + "," + gen.Number() + ")"); break;
            default: args.push_back("(" + gen.Quoted() + gen.Blank() + "," + gen.Blank() + gen.Quoted() + ")"); break;
            }
        }
        // Full-precision arguments (four elements, mostly in the math functions' useful ranges), so the per-element CRT
        // math is compared on values where float and double evaluation round differently.
        std::uniform_real_distribution<float> wide(-12.0f, 12.0f), unit(-1.0f, 1.0f);
        for (int n = 0; n < 300; ++n) {
            std::string arg = "(";
            for (int e = 0; e < 4; ++e) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%s%.9g", e ? "," : "", static_cast<double>(n % 2 ? unit(g) : wide(g)));
                arg += buf;
            }
            args.push_back(arg + ")");
        }
        uint32_t fill = 0x7c10 + static_cast<uint32_t>(i) * 1000;
        for (const std::string &arg : args) {
            if (ReachesEmptyStrstr(classes[i].name + arg)) continue;
            CompareOnText(
                ctx, classes[i].name, arg, ++fill, &rnd,
                [&](Value *out, const char **cursor) { return orig(tables.gameObjects[i], out, cursor, tables.OriginalTable()); },
                [&](Value *out, const char **cursor) { return ours(&tables.standIns[i], out, cursor, tables.Ours()); });
        }
    }
}

// Our own built-in table (ts2_own_eval_builtins, lane RQ: what an image-free program's EvalFunctionTable reads in place of
// the image's 0xe7f590): the image's names in the image's order, and each entry's object evaluating as the image's object
// of that entry does, called through its own vtable on calls of every shape.
DIFF_TEST(ExpressionEvaluator_OwnBuiltins, 0xe7f590)
{
    Sse2Math sse2(ctx);
    Tables tables(ctx);
    RandomPair rnd(ctx);
    std::mt19937 g(0xf590);
    Generator gen(g);
    using Slot = Value *(__thiscall *)(void *, Value *, const char **, void *);
    const auto *image = ctx.image.At<EvalFunctionTable::Entry>(EvalFunctionTable::kBuiltinsRva);
    const uintptr_t base = reinterpret_cast<uintptr_t>(ctx.image.At<uint8_t>(0));
    const auto &classes = EvalClasses();
    for (size_t i = 0; i < EvalFunctionTable::kBuiltinCount; ++i) {
        const OwnEvalBuiltin &own = ts2_own_eval_builtins[i];
        ++ctx.cases;
        if (std::strcmp(own.name, image[i].first) != 0) ctx.Fail("entry %zu: ours is '%s', the image's '%s'", i, own.name, image[i].first);
        if (!own.function) {
            ctx.Fail("entry %zu: no object", i);
            continue;
        }
        const uint32_t vtable = static_cast<uint32_t>(*reinterpret_cast<const uintptr_t *>(image[i].second) - base);
        size_t which = classes.size();
        for (size_t k = 0; k < classes.size(); ++k)
            if (classes[k].vtable == vtable) which = k;
        if (which == classes.size()) {
            ctx.Fail("entry %zu: the image's object has an unknown vtable %08x", i, vtable);
            continue;
        }
        auto orig = ctx.Original<Slot>(classes[which].slot0);
        std::vector<std::string> args = {"", "()", "(1)", "(1,2)", "(1,2,3)", "(1,2,3,4)", "( 0.5 , -0.25 )", "(", "(1", " (2)",
                                         "(\"abc\", \"B\")", "(\"\",\"\")", "(\"ab\",\"b\"", "(-1,-2,-3,-4)", "(pi,-pi)", "(0.3,0.7,0.1,0.9)"};
        for (int n = 0; n < 60; ++n) {
            switch (g() % 3) {
            case 0: args.push_back("(" + gen.Expr(1 + g() % 3) + ")"); break;
            case 1: args.push_back("(" + gen.Number() + "," + gen.Number() + ")"); break;
            default: args.push_back("(" + gen.Quoted() + "," + gen.Quoted() + ")"); break;
            }
        }
        uint32_t fill = 0xf590 + static_cast<uint32_t>(i) * 1000;
        for (const std::string &arg : args) {
            if (ReachesEmptyStrstr(std::string(own.name) + arg)) continue;
            CompareOnText(
                ctx, own.name, arg, ++fill, &rnd,
                [&](Value *out, const char **cursor) { return orig(image[i].second, out, cursor, tables.OriginalTable()); },
                [&](Value *out, const char **cursor) { return own.function->Evaluate(out, cursor, tables.Ours()); });
        }
    }
}

// ---- helpers -------------------------------------------------------------------------------------------------------

DIFF_TEST(ExpressionEvaluator_ExpectChar, 0xb096d0)
{
    auto orig = ctx.Original<void(__cdecl *)(const char **, char)>(0xb096d0);
    std::mt19937 g(0x96d0);
    static const char pool[] = " \t\r\n()\",:x1\x80\xff";
    for (int n = 0; n < 4000; ++n) {
        std::string text;
        for (unsigned k = g() % 5; k > 0; --k) text += pool[g() % (sizeof(pool) - 1)];
        const char expected = n % 50 == 0 ? static_cast<char>(0x80 + g() % 0x80) : pool[g() % (sizeof(pool) - 1)];
        const char *cursorA = text.c_str(), *cursorB = text.c_str();
        cptest::Outcome oa = cptest::RunGuarded([&] { orig(&cursorA, expected); });
        cptest::Outcome ob = cptest::RunGuarded([&] { ExpectChar(&cursorB, expected); });
        ++ctx.cases;
        if (!(oa == ob) || cursorA != cursorB)
            ctx.Fail("ExpectChar('%s', %02x): cursor +%d / +%d, %s / %s", cptest::Printable(text).c_str(), static_cast<uint8_t>(expected),
                     static_cast<int>(cursorA - text.c_str()), static_cast<int>(cursorB - text.c_str()), cptest::Printable(oa.detail).c_str(),
                     cptest::Printable(ob.detail).c_str());
    }
}

DIFF_TEST(ExpressionEvaluator_ReadQuotedName, 0xb0a580)
{
    Tables tables(ctx);
    auto orig = ctx.Original<QuotedName *(__thiscall *)(void *, QuotedName *, const char **)>(0xb0a580);
    std::mt19937 g(0xa580);
    static const char pool[] = " \t\"\"ab\\,)";
    for (int n = 0; n < 4000; ++n) {
        std::string text;
        for (unsigned k = g() % 8; k > 0; --k) text += pool[g() % (sizeof(pool) - 1)];
        QuotedName a{reinterpret_cast<const char *>(0x11111111), 0x22222222}, b = a;
        const char *cursorA = text.c_str(), *cursorB = text.c_str();
        cptest::Outcome oa = cptest::RunGuarded([&] { return reinterpret_cast<uintptr_t>(orig(tables.OriginalTable(), &a, &cursorA)) == reinterpret_cast<uintptr_t>(&a); });
        cptest::Outcome ob = cptest::RunGuarded([&] { return reinterpret_cast<uintptr_t>(tables.Ours()->ReadQuotedName(&b, &cursorB)) == reinterpret_cast<uintptr_t>(&b); });
        ++ctx.cases;
        if (!(oa == ob) || cursorA != cursorB || a.text != b.text || a.length != b.length)
            ctx.Fail("ReadQuotedName('%s'): cursor +%d / +%d, length %u / %u, %s / %s", cptest::Printable(text).c_str(),
                     static_cast<int>(cursorA - text.c_str()), static_cast<int>(cursorB - text.c_str()), a.length, b.length,
                     cptest::Printable(oa.detail).c_str(), cptest::Printable(ob.detail).c_str());
    }
}

namespace {

float SpecialFloat(std::mt19937 &g)
{
    static const uint32_t bits[] = {0x00000000, 0x80000000, 0x3f800000, 0xbf800000, 0x7f800000, 0xff800000, 0x7fc00000,
                                    0xffc00000, 0x7fc12345, 0x00000001, 0x80000001, 0x2f03126f, 0xaf03126f, 0x2f031270,
                                    0x2f03126e, 0x7f7fffff, 0x40000000};
    switch (g() % 4) {
    case 0: return std::bit_cast<float>(bits[g() % (sizeof(bits) / sizeof(bits[0]))]);
    case 1: return static_cast<float>(static_cast<int>(g() % 5) - 2);
    case 2: return std::bit_cast<float>(static_cast<uint32_t>(g()));
    default: return std::bit_cast<float>(0x2f03126fu + static_cast<uint32_t>(g() % 5) - 2u) * ((g() & 1) ? -1.0f : 1.0f);
    }
}

} // namespace

DIFF_TEST(ExpressionEvaluator_Compare3, 0xb09380)
{
    auto orig = ctx.Original<int(__cdecl *)(const float *, const float *, const float *, const float *)>(0xb09380);
    std::mt19937 g(0x9380);
    for (int n = 0; n < 20000; ++n) {
        float a[4], b[4];
        const unsigned na = g() % 5, nb = g() % 5;
        for (int i = 0; i < 4; ++i) {
            a[i] = SpecialFloat(g);
            b[i] = g() % 2 ? a[i] : SpecialFloat(g);
        }
        const int ra = orig(a, a + na, b, b + nb), rb = Compare3(a, a + na, b, b + nb);
        ++ctx.cases;
        if (ra != rb) ctx.Fail("Compare3(%s[%u], %s[%u]): %d / %d", Hex(a, 16).c_str(), na, Hex(b, 16).c_str(), nb, ra, rb);
    }
}

DIFF_TEST(ExpressionEvaluator_IsNearZero, 0xb09660)
{
    auto orig = ctx.Original<bool(__cdecl *)(float)>(0xb09660);
    std::mt19937 g(0x9660);
    for (int n = 0; n < 20000; ++n) {
        const float x = SpecialFloat(g);
        const bool ra = orig(x), rb = IsNearZero(x);
        ++ctx.cases;
        if (ra != rb) ctx.Fail("IsNearZero(%08x): %d / %d", std::bit_cast<uint32_t>(x), ra, rb);
    }
}

DIFF_TEST(ExpressionEvaluator_IsTrue, 0xb09680)
{
    auto orig = ctx.Original<bool(__cdecl *)(const Value *)>(0xb09680);
    std::mt19937 g(0x9680);
    for (int n = 0; n < 5000; ++n) {
        Value v;
        for (float &x : v.v) x = SpecialFloat(g);
        v.type = n % 4 == 0 ? static_cast<int32_t>(g() % 6) - 1 : 1;
        cptest::Outcome oa = cptest::RunGuarded([&] { return orig(&v) ? 1u : 0u; });
        cptest::Outcome ob = cptest::RunGuarded([&] { return IsTrue(&v) ? 1u : 0u; });
        ++ctx.cases;
        if (!(oa == ob)) ctx.Fail("IsTrue(%s): %u / %u, %s / %s", Hex(&v, sizeof v).c_str(), oa.ret, ob.ret, oa.detail.c_str(), ob.detail.c_str());
    }
}

DIFF_TEST(ExpressionEvaluator_ScaleRange, 0xb09400)
{
    auto orig = ctx.Original<float *(__cdecl *)(const float *, const float *, float *, ScaleBy)>(0xb09400);
    std::mt19937 g(0x9400);
    for (int n = 0; n < 4000; ++n) {
        const unsigned count = n % 3 == 0 ? g() % 70 : g() % 6; // 16 and more takes the original's four-lane loop
        std::vector<float> src(count + 4);
        for (float &x : src) x = SpecialFloat(g);
        const float factor = n % 5 == 0 ? SpecialFloat(g) : std::bit_cast<float>(0x3e22f983u);
        const bool inPlace = g() % 2 == 0;
        std::vector<float> dstA = inPlace ? src : std::vector<float>(count + 4, 7.0f), dstB = dstA;
        std::vector<float> srcA = src, srcB = src;
        const float *firstA = inPlace ? dstA.data() : srcA.data();
        const float *firstB = inPlace ? dstB.data() : srcB.data();
        float *ra = orig(firstA, firstA + count, dstA.data(), ScaleBy{0x5a5a5a5a, factor});
        float *rb = ScaleRange(firstB, firstB + count, dstB.data(), ScaleBy{0x5a5a5a5a, factor});
        ++ctx.cases;
        if (ra - dstA.data() != rb - dstB.data() || std::memcmp(dstA.data(), dstB.data(), dstA.size() * 4) != 0)
            ctx.Fail("ScaleRange(%u floats, %08x, %s): end %d / %d or contents differ", count, std::bit_cast<uint32_t>(factor),
                     inPlace ? "in place" : "apart", static_cast<int>(ra - dstA.data()), static_cast<int>(rb - dstB.data()));
    }
}

DIFF_TEST(RZText_FindNoCase, 0x1b1a52)
{
    auto orig = ctx.Original<const char *(__cdecl *)(const char *, const char *)>(0x1b1a52);
    std::mt19937 g(0x1a52);
    static const char pool[] = "aAbB@[`{ 1";
    // ASCII only: the CRT's tolower is given each byte as a signed char, and bytes from 0x80 up are negative numbers
    // outside its domain (each side's own CRT, so not compared).
    for (int n = 0; n < 20000; ++n) {
        std::string text, part;
        for (unsigned k = g() % 9; k > 0; --k) text += pool[g() % (sizeof(pool) - 1)];
        for (unsigned k = g() % (n % 4 == 0 ? 9 : 4); k > 0; --k) part += pool[g() % (sizeof(pool) - 1)];
        const char *ra = orig(text.c_str(), part.c_str()), *rb = rztext::FindNoCase(text.c_str(), part.c_str());
        ++ctx.cases;
        const long oa = ra ? static_cast<long>(ra - text.c_str()) : -1, ob = rb ? static_cast<long>(rb - text.c_str()) : -1;
        if (oa != ob) ctx.Fail("FindNoCase('%s', '%s'): %ld / %ld", text.c_str(), part.c_str(), oa, ob);
    }
}
