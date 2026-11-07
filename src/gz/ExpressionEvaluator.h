#pragma once
// nGZCommandParser's expression evaluator (lane NO): the arithmetic the scripts' numeric arguments go through (the
// material, effects, camera and option commands, the parser's eval slot and its if / while conditions). RVAs are
// 1.18.0.187.
//
// A value is up to four floats (gzexpr::Value: v[0..3] and `type`, which is the element count, 1 to 4; 0 after an empty
// swizzle). The grammar, lowest precedence first, one member per level:
//
//   expression := sequence                                   (first character a letter, digit, '.', '-', '+' or '(')
//   sequence   := logical { ',' logical } [ '?' expression ':' expression ]
//   logical    := comparison { ("and" | "or") comparison }   (keywords not followed by a letter or digit)
//   comparison := sum { ("!=" | "<" | "<=" | "=" | ">=" | ">") sum }
//   sum        := product { ('+' | '-') product }
//   product    := unary { ('*' | '/' | '%') unary }
//   unary      := '+' unary | '-' unary | "not" unary | primary [ '.' { 'x' | 'y' | 'z' | 'w' } ]
//   primary    := number | '(' expression ')' | name ...     (a name: a built-in function, see below)
//
// Blanks (tab, line feed, carriage return, space) may separate the tokens. Sums, differences, products and negation
// work on all four lanes (unused lanes included); quotients and remainders on the larger element count; comparisons
// compare lexicographically and give one element; and / or / not / '?' need one element. Errors throw cError.
//
// The evaluator object is the function table (EvalFunctionTable: a sorted vector of (name, function object)); every
// level is __thiscall on it with the result through a hidden pointer, made explicit here as `out` (ret 8: out and the
// cursor), and returns out. Names are matched as prefixes: "sq" finds the first built-in starting with "sq".
//
// The built-in functions (the static objects of the anonymous-namespace classes cEval*, RTTI
// .?AUcEval*@?A0x293d2d98@@, one-slot vtables 0xe7f4b8..0xe7f578) take the cursor just after their name and the
// evaluator. Hook shape: the game's objects keep the game's vtables and each slot body is hooked to the member here.
#include "gz/EvalFunctionTable.h"
#include "gz/ExpressionFunctionBase.h"
#include "rz/cRZRandom.h"

#include <cstdint>
#include <vector>

namespace nGZCommandParser {

using gzexpr::Value;

// A name between double quotes, as ReadQuotedName finds it: the text after the opening quote and its length.
struct QuotedName {
    const char *text; // +0
    uint32_t length;  // +4
};
static_assert(sizeof(QuotedName) == 8, "QuotedName layout");

class ExpressionEvaluator;

// A function object as the evaluator calls it (gzexpr::cFunction's slot 0, with the hidden result pointer explicit):
// the evaluator passes its own result buffer on, so a function that throws part-way leaves what it wrote there.
class __declspec(novtable) cIEvalFunction {
public:
    // 0 (__thiscall, ret 0xc): the value of the call; `cursor` is just after the function's name.
    virtual Value *Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator) = 0;
};

// The evaluator: a view of EvalFunctionTable with its parse members (never constructed as such).
class ExpressionEvaluator {
public:
    ExpressionEvaluator() = delete;
    ExpressionEvaluator(const ExpressionEvaluator &) = delete;
    static ExpressionEvaluator *Of(EvalFunctionTable *table) { return reinterpret_cast<ExpressionEvaluator *>(table); }

    // 0xb0a870 (ret 8): the whole of `text` as an expression; throws when anything but blanks follows it.
    Value *Evaluate(Value *out, const char *text);
    // 0xb09940: skips blanks; throws unless a letter, digit, '.', '-', '+' or '(' starts the expression; a sequence.
    Value *ParseExpression(Value *out, const char **cursor);
    // 0xb097e0: logical terms joined by ',' (at most four elements in all), then an optional "? a : b".
    Value *ParseSequence(Value *out, const char **cursor);
    // 0xb099e0: comparisons joined by "and" / "or" (both sides always evaluated; NaN counts as true).
    Value *ParseLogical(Value *out, const char **cursor);
    // 0xb0a200: sums joined by "!=", "<", "<=", "=", ">=", ">" (Compare3; the result is 1 or 0, one element).
    Value *ParseComparison(Value *out, const char **cursor);
    // 0xb09740: products joined by '+' / '-' (all four lanes; the larger element count).
    Value *ParseSum(Value *out, const char **cursor);
    // 0xb09c50: unary terms joined by '*' (all four lanes), '/' and '%' (lanes below the larger count; a divisor
    // element near zero throws).
    Value *ParseProduct(Value *out, const char **cursor);
    // 0xb0a660: '+', '-' (all four lanes negated) and "not" prefixes; a primary and its swizzle ('.' then x y z w).
    Value *ParseUnary(Value *out, const char **cursor);
    // 0xb09f50 (EH frame): a number (rztext::ReadDouble), a parenthesised expression or a built-in's call.
    Value *ParsePrimary(Value *out, const char **cursor);
    // 0xb0a580 (`this` unused): '"' expected (blanks first), then the text up to the closing quote, which is passed.
    QuotedName *ReadQuotedName(QuotedName *out, const char **cursor);

    std::vector<EvalFunctionTable::Entry> mEntries; // +0 (the table's vector)
};
static_assert(sizeof(ExpressionEvaluator) == sizeof(EvalFunctionTable), "a view of the table");

// 0xb096d0 (__cdecl): blanks skipped, then `expected` passed; a cError "expected '%c'" otherwise.
void __cdecl ExpectChar(const char **cursor, char expected);
// 0xb09380 (__cdecl): three-way lexicographic comparison of two float ranges: -1, 0 or 1. The first unequal pair
// decides (NaN: 1); otherwise the shorter range is less.
int __cdecl Compare3(const float *first1, const float *last1, const float *first2, const float *last2);
// 0xb09660 (__cdecl): |x| <= 1.192093e-10f (false for NaN): the divisor check.
bool __cdecl IsNearZero(float x);
// 0xb09680 (__cdecl): a one-element value's truth (anything but 0; NaN is true); throws for another element count.
bool __cdecl IsTrue(const Value *value);

// The factor the angle conversions multiply by, passed by value as the original's (empty function object, factor)
// pair: 8 bytes, the factor in the second word.
struct ScaleBy {
    uint32_t unused; // the function object's padding (whatever the caller's stack held)
    float factor;    // +4
};
static_assert(sizeof(ScaleBy) == 8, "ScaleBy layout");
// 0xb09400 (__cdecl): dest[i] = factor * first[i] over [first, last); returns the end of dest.
float *__cdecl ScaleRange(const float *first, const float *last, float *dest, ScaleBy scale);

// The built-ins (hook shape: no members, the game's objects keep their vtables; slot 0 hooked to Evaluate, __thiscall,
// ret 0xc). `evaluator` is the table the call came from.
#define NGZ_EVAL_FUNCTION(Name) \
    struct Name { \
        Value *Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator); \
    }
NGZ_EVAL_FUNCTION(cEvalTrue);             // 0xb07bb0 "true", "on": 1
NGZ_EVAL_FUNCTION(cEvalFalse);            // 0xb07bd0 "false", "off": 0
NGZ_EVAL_FUNCTION(cEvalPI);               // 0xb07bf0 "pi": pi in all four lanes, one element
NGZ_EVAL_FUNCTION(cEvalRnd);              // 0xb07c10 "rnd(lo, hi)": uniform in [lo, hi) from its own cRZRandom
NGZ_EVAL_FUNCTION(cEvalExp);              // 0xb07d20 "exp(v)", per element (the same for the rest of this list)
NGZ_EVAL_FUNCTION(cEvalLog);              // 0xb07d90 "log(v)"
NGZ_EVAL_FUNCTION(cEvalAbs);              // 0xb07e00 "abs(v)"
NGZ_EVAL_FUNCTION(cEvalSin);              // 0xb07e70 "sin(v)"
NGZ_EVAL_FUNCTION(cEvalCos);              // 0xb07ee0 "cos(v)"
NGZ_EVAL_FUNCTION(cEvalTan);              // 0xb07f50 "tan(v)"
NGZ_EVAL_FUNCTION(cEvalSqr);              // 0xb07fc0 "sqr(v)": v * v
NGZ_EVAL_FUNCTION(cEvalPow);              // 0xb08090 "pow(x, y)": one element
NGZ_EVAL_FUNCTION(cEvalSqrt);             // 0xb08110 "sqrt(v)"
NGZ_EVAL_FUNCTION(cEvalCeil);             // 0xb08180 "ceil(v)"
NGZ_EVAL_FUNCTION(cEvalAsin);             // 0xb081f0 "asin(v)"
NGZ_EVAL_FUNCTION(cEvalAcos);             // 0xb08260 "acos(v)"
NGZ_EVAL_FUNCTION(cEvalAtan);             // 0xb082d0 "atan(v)"
NGZ_EVAL_FUNCTION(cEvalFloor);            // 0xb08340 "floor(v)"
NGZ_EVAL_FUNCTION(cEvalAtan2);            // 0xb083b0 "atan2(y, x)": one element
NGZ_EVAL_FUNCTION(cEvalStrcmp);           // 0xb08450 "strcmp(\"a\", \"b\")": strncmp's value over the shorter length, else +-1 by length
NGZ_EVAL_FUNCTION(cEvalStrstr);           // 0xb08510 "strstr(\"text\", \"part\")": 1 when found (case ignored)
NGZ_EVAL_FUNCTION(cEvalRadiansToTurns);   // 0xb086d0 "rad2turn(v)": v / (2 pi)
NGZ_EVAL_FUNCTION(cEvalDegreesToTurns);   // 0xb08730 "deg2turn(v)": v / 360
NGZ_EVAL_FUNCTION(cEvalRadiansToDegrees); // 0xb08790 "rad2deg(v)"
NGZ_EVAL_FUNCTION(cEvalDegreesToRadians); // 0xb08860 "deg2rad(v)"
#undef NGZ_EVAL_FUNCTION

// rnd's generator: made on first use (seeded from the clock, as the original's function-local static at 0x10faa48).
cRZRandom &EvalRandomGenerator();

// An entry of our own built-in table (EvalFunctionTable::Entry's layout, constant-initialisable).
struct OwnEvalBuiltin {
    const char *name;
    cIEvalFunction *function;
};
static_assert(sizeof(OwnEvalBuiltin) == sizeof(EvalFunctionTable::Entry), "the table's element layout");

} // namespace nGZCommandParser

// Our own built-in table (ExpressionFunctions.cpp): the game's 27 names in the game's order, each with our object.
extern "C" const nGZCommandParser::OwnEvalBuiltin ts2_own_eval_builtins[nGZCommandParser::EvalFunctionTable::kBuiltinCount];
