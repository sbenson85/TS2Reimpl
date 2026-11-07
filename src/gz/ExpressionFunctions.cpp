// The expression evaluator's built-in functions (the cEval* classes; lane NO). RVAs are 1.18.0.187.
//
// The functions taking arguments read "(" expression ")" (or two quoted names) after their name. The per-element
// ones apply a CRT function in double precision to each of the value's elements and round the results to float; the
// lanes past the element count keep what the expression left there.
#include "gz/ExpressionEvaluator.h"

#include "rz/RZStringSearch.h"

#include <algorithm>
#include <bit>
#include <climits>
#include <cmath>
#include <cstring>
#include <string>

namespace nGZCommandParser {

namespace {

// pi as a float (0x40490fdb).
const float kPi = std::bit_cast<float>(0x40490fdbu);
// The angle conversion factors: 1 / (2 pi), 1 / 360, 180 / pi, pi / 180.
const float kTurnsPerRadian = std::bit_cast<float>(0x3e22f983u);
const float kTurnsPerDegree = std::bit_cast<float>(0x3b360b61u);
const float kDegreesPerRadian = std::bit_cast<float>(0x42652ee0u);
const float kRadiansPerDegree = std::bit_cast<float>(0x3c8efa35u);

void SetScalar(Value *out, float x)
{
    out->v[0] = x;
    out->v[1] = 0.0f;
    out->v[2] = 0.0f;
    out->v[3] = 0.0f;
    out->type = 1;
}

// "(" expression ")" into out.
Value *ParseArgument(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    ExpectChar(cursor, '(');
    evaluator->ParseExpression(out, cursor);
    ExpectChar(cursor, ')');
    return out;
}

// "(" expression ")" with fn applied to each element.
template <class Fn> Value *ApplyEach(Value *out, const char **cursor, ExpressionEvaluator *evaluator, Fn fn)
{
    ParseArgument(out, cursor, evaluator);
    for (int32_t i = 0; i < out->type; ++i) out->v[i] = static_cast<float>(fn(static_cast<double>(out->v[i])));
    return out;
}

// '(' a quoted name ',' a quoted name ')'.
void ParseTwoNames(const char **cursor, ExpressionEvaluator *evaluator, QuotedName *a, QuotedName *b)
{
    ExpectChar(cursor, '(');
    evaluator->ReadQuotedName(a, cursor);
    ExpectChar(cursor, ',');
    evaluator->ReadQuotedName(b, cursor);
    ExpectChar(cursor, ')');
}

// The generator rnd draws from: seeded from the clock on first use (the original's call to the constructor returns to
// 0xb07cfd; seed control keys the clock seed by that site).
struct ClockSeededRandom {
    cRZRandom random;
    ClockSeededRandom() { random.ConstructAt(UINT32_MAX, 0xb07cfd); }
};

} // namespace

cRZRandom &EvalRandomGenerator()
{
    static ClockSeededRandom generator;
    return generator.random;
}

Value *cEvalTrue::Evaluate(Value *out, const char **, ExpressionEvaluator *)
{
    SetScalar(out, 1.0f);
    return out;
}

Value *cEvalFalse::Evaluate(Value *out, const char **, ExpressionEvaluator *)
{
    SetScalar(out, 0.0f);
    return out;
}

Value *cEvalPI::Evaluate(Value *out, const char **, ExpressionEvaluator *)
{
    for (float &x : out->v) x = kPi;
    out->type = 1;
    return out;
}

Value *cEvalRnd::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    cRZRandom &random = EvalRandomGenerator();
    Value range;
    ParseArgument(&range, cursor, evaluator);
    SetScalar(out, static_cast<float>(random.RandomDoubleRangeUniform(range.v[0], range.v[1])));
    return out;
}

Value *cEvalExp::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    return ApplyEach(out, cursor, evaluator, [](double x) { return std::exp(x); });
}

Value *cEvalLog::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    return ApplyEach(out, cursor, evaluator, [](double x) { return std::log(x); });
}

Value *cEvalAbs::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    return ApplyEach(out, cursor, evaluator, [](double x) { return std::fabs(x); });
}

Value *cEvalSin::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    return ApplyEach(out, cursor, evaluator, [](double x) { return std::sin(x); });
}

Value *cEvalCos::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    return ApplyEach(out, cursor, evaluator, [](double x) { return std::cos(x); });
}

Value *cEvalTan::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    return ApplyEach(out, cursor, evaluator, [](double x) { return std::tan(x); });
}

Value *cEvalSqr::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    ParseArgument(out, cursor, evaluator);
    for (int32_t i = 0; i < out->type; ++i) out->v[i] *= out->v[i];
    return out;
}

Value *cEvalPow::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    Value operands;
    ParseArgument(&operands, cursor, evaluator);
    SetScalar(out, static_cast<float>(std::pow(static_cast<double>(operands.v[0]), static_cast<double>(operands.v[1]))));
    return out;
}

Value *cEvalSqrt::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    return ApplyEach(out, cursor, evaluator, [](double x) { return std::sqrt(x); });
}

Value *cEvalCeil::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    return ApplyEach(out, cursor, evaluator, [](double x) { return std::ceil(x); });
}

Value *cEvalAsin::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    return ApplyEach(out, cursor, evaluator, [](double x) { return std::asin(x); });
}

Value *cEvalAcos::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    return ApplyEach(out, cursor, evaluator, [](double x) { return std::acos(x); });
}

Value *cEvalAtan::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    return ApplyEach(out, cursor, evaluator, [](double x) { return std::atan(x); });
}

Value *cEvalFloor::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    return ApplyEach(out, cursor, evaluator, [](double x) { return std::floor(x); });
}

Value *cEvalAtan2::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    Value operands;
    ParseArgument(&operands, cursor, evaluator);
    SetScalar(out, static_cast<float>(std::atan2(static_cast<double>(operands.v[0]), static_cast<double>(operands.v[1]))));
    return out;
}

Value *cEvalStrcmp::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    QuotedName a, b;
    ParseTwoNames(cursor, evaluator, &a, &b);
    const int32_t lengthA = static_cast<int32_t>(a.length), lengthB = static_cast<int32_t>(b.length);
    int order = std::strncmp(a.text, b.text, static_cast<size_t>(std::min(lengthA, lengthB)));
    if (order == 0 && lengthA != lengthB) order = lengthA >= lengthB ? 1 : -1;
    SetScalar(out, static_cast<float>(order));
    return out;
}

Value *cEvalStrstr::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    QuotedName text, part;
    ParseTwoNames(cursor, evaluator, &text, &part);
    // ORIGINAL-BUG: the text is taken from its second character on, so a match at its start is missed
    // (strstr("abc", "a") is 0), and an empty text ("") asks for a string of length -1, which throws std::length_error.
    const std::string haystack(text.text + 1, text.length - 1);
    const std::string needle(part.text, part.length);
    SetScalar(out, rztext::FindNoCase(haystack.c_str(), needle.c_str()) != nullptr ? 1.0f : 0.0f);
    return out;
}

Value *cEvalRadiansToTurns::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    ParseArgument(out, cursor, evaluator);
    ScaleRange(out->v, out->v + out->type, out->v, ScaleBy{0, kTurnsPerRadian});
    return out;
}

Value *cEvalDegreesToTurns::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    ParseArgument(out, cursor, evaluator);
    ScaleRange(out->v, out->v + out->type, out->v, ScaleBy{0, kTurnsPerDegree});
    return out;
}

Value *cEvalRadiansToDegrees::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    ParseArgument(out, cursor, evaluator);
    for (int32_t i = 0; i < out->type; ++i) out->v[i] *= kDegreesPerRadian;
    return out;
}

Value *cEvalDegreesToRadians::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    ParseArgument(out, cursor, evaluator);
    for (int32_t i = 0; i < out->type; ++i) out->v[i] *= kRadiansPerDegree;
    return out;
}

// ---- our own built-in objects and table (lane RQ) ---------------------------------------------------------------------
//
// The game's table (0xe7f590) pairs each name with one of its static objects; the hooked game keeps those (their slot 0
// is hooked to the members above). An image-free program's EvalFunctionTable copies this one instead (ts2_rva_e7f590 is
// aliased to it, standalone/owned_statics.cpp): the same names in the same order, each with one of these objects, whose
// slot 0 is the same member.

namespace {

template <class C>
struct OwnEvalFunction final : cIEvalFunction {
    Value *Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator) override
    {
        C function;
        return function.Evaluate(out, cursor, evaluator);
    }
};

// One object per class, as the game's ("true"/"on" and "false"/"off" share theirs).
constinit OwnEvalFunction<cEvalTrue> gTrue;
constinit OwnEvalFunction<cEvalFalse> gFalse;
constinit OwnEvalFunction<cEvalPI> gPi;
constinit OwnEvalFunction<cEvalRnd> gRnd;
constinit OwnEvalFunction<cEvalExp> gExp;
constinit OwnEvalFunction<cEvalLog> gLog;
constinit OwnEvalFunction<cEvalAbs> gAbs;
constinit OwnEvalFunction<cEvalSin> gSin;
constinit OwnEvalFunction<cEvalCos> gCos;
constinit OwnEvalFunction<cEvalTan> gTan;
constinit OwnEvalFunction<cEvalSqr> gSqr;
constinit OwnEvalFunction<cEvalPow> gPow;
constinit OwnEvalFunction<cEvalSqrt> gSqrt;
constinit OwnEvalFunction<cEvalCeil> gCeil;
constinit OwnEvalFunction<cEvalAsin> gAsin;
constinit OwnEvalFunction<cEvalAcos> gAcos;
constinit OwnEvalFunction<cEvalAtan> gAtan;
constinit OwnEvalFunction<cEvalFloor> gFloor;
constinit OwnEvalFunction<cEvalAtan2> gAtan2;
constinit OwnEvalFunction<cEvalStrcmp> gStrcmp;
constinit OwnEvalFunction<cEvalStrstr> gStrstr;
constinit OwnEvalFunction<cEvalRadiansToTurns> gRadiansToTurns;
constinit OwnEvalFunction<cEvalDegreesToTurns> gDegreesToTurns;
constinit OwnEvalFunction<cEvalRadiansToDegrees> gRadiansToDegrees;
constinit OwnEvalFunction<cEvalDegreesToRadians> gDegreesToRadians;

} // namespace

} // namespace nGZCommandParser

// Constant-initialised (no dynamic initialiser to order against the tables built at start-up).
extern "C" constinit const nGZCommandParser::OwnEvalBuiltin ts2_own_eval_builtins[nGZCommandParser::EvalFunctionTable::kBuiltinCount] = {
    {"true", &nGZCommandParser::gTrue},
    {"on", &nGZCommandParser::gTrue},
    {"false", &nGZCommandParser::gFalse},
    {"off", &nGZCommandParser::gFalse},
    {"pi", &nGZCommandParser::gPi},
    {"rnd", &nGZCommandParser::gRnd},
    {"exp", &nGZCommandParser::gExp},
    {"log", &nGZCommandParser::gLog},
    {"abs", &nGZCommandParser::gAbs},
    {"sin", &nGZCommandParser::gSin},
    {"cos", &nGZCommandParser::gCos},
    {"tan", &nGZCommandParser::gTan},
    {"sqr", &nGZCommandParser::gSqr},
    {"pow", &nGZCommandParser::gPow},
    {"sqrt", &nGZCommandParser::gSqrt},
    {"ceil", &nGZCommandParser::gCeil},
    {"asin", &nGZCommandParser::gAsin},
    {"acos", &nGZCommandParser::gAcos},
    {"atan", &nGZCommandParser::gAtan},
    {"floor", &nGZCommandParser::gFloor},
    {"atan2", &nGZCommandParser::gAtan2},
    {"strcmp", &nGZCommandParser::gStrcmp},
    {"strstr", &nGZCommandParser::gStrstr},
    {"rad2turn", &nGZCommandParser::gRadiansToTurns},
    {"deg2turn", &nGZCommandParser::gDegreesToTurns},
    {"rad2deg", &nGZCommandParser::gRadiansToDegrees},
    {"deg2rad", &nGZCommandParser::gDegreesToRadians},
};
