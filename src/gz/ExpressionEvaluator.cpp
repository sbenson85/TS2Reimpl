// nGZCommandParser's expression evaluator: the parse levels and their helpers (lane NO). RVAs are 1.18.0.187.
#include "gz/ExpressionEvaluator.h"

#include "game/game.h"
#include "gz/CommandParser.h"
#include "gz/cParserTexts.h"
#include "rz/RZTextParse.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <string>
#include <type_traits>

namespace nGZCommandParser {

namespace {

// The game's strings.
constexpr const char *kGarbageAtEnd = ts2_rva_e7f6ac;       // "garbage at end of expression:'%s'"
constexpr const char *kExpressionStarts = ts2_rva_e7f6d0;   // ".-+(": what may start an expression besides a letter or digit
constexpr const char *kBadExpression = ts2_rva_e7f6d8;      // "bad numerical expression '%s'"
constexpr const char *kTooManyElements = ts2_rva_e7f6f8;    // "too many elements in sequence"
constexpr const char *kAnd = ts2_rva_e7f718;                // "and"
constexpr const char *kOr = ts2_rva_e7f71c;                 // "or"
constexpr const char *kExpectedEquals = ts2_rva_e7f720;     // "expected '='"
constexpr const char *kDivisionByZero = ts2_rva_e7f730;     // "division by zero"
constexpr const char *kUnknownOperand = ts2_rva_e7f744;     // "Unknown operand: "
constexpr const char *kInvalidConstant = ts2_rva_e7f758;    // "invalid constant"
constexpr const char *kMissingQuote = ts2_rva_e7f76c;       // "missing '\"'"
constexpr const char *kExpectedChar = ts2_rva_e7f778;       // "expected '%c'"
constexpr const char *kNotScalar = ts2_rva_e79e14;          // "not a scalar expression"
constexpr const char *kNot = ts2_rva_e5c4d4;                // "not"

// The divisor threshold (at 0xda1a28).
const float kNearZero = std::bit_cast<float>(0x2f03126fu);

// The parser's character classes (the game's table at 0xe7d528: bit 1 blanks, bit 2 letters, bit 4 digits; bytes from
// 0x80 up are in none).
bool IsBlank(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool IsDigit(unsigned char c) { return c >= '0' && c <= '9'; }
bool IsLetter(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
bool IsWordChar(unsigned char c) { return IsLetter(c) || IsDigit(c); }

void SkipBlanks(const char **cursor)
{
    while (IsBlank(static_cast<unsigned char>(**cursor))) ++*cursor;
}

// The keyword at the cursor (all of it, and not followed by a letter or digit): the cursor is moved past it.
bool TakeKeyword(const char **cursor, const char *keyword)
{
    const char *p = *cursor;
    for (; *keyword != '\0'; ++keyword, ++p)
        if (*p != *keyword) return false;
    if (IsWordChar(static_cast<unsigned char>(*p))) return false;
    *cursor = p;
    return true;
}

// A value of one element; the other lanes zero.
void SetScalar(Value *out, float x)
{
    out->v[0] = x;
    out->v[1] = 0.0f;
    out->v[2] = 0.0f;
    out->v[3] = 0.0f;
    out->type = 1;
}

// x != 0, with NaN counting as true (the original's ucomiss / lahf / test ah, 0x44).
bool NonZero(float x) { return !(x == 0.0f); }

} // namespace

void __cdecl ExpectChar(const char **cursor, char expected)
{
    SkipBlanks(cursor);
    if (**cursor != expected) ThrowFormatted(kExpectedChar, static_cast<int>(expected));
    ++*cursor;
}

int __cdecl Compare3(const float *first1, const float *last1, const float *first2, const float *last2)
{
    const ptrdiff_t shorter = std::min(last1 - first1, last2 - first2);
    for (ptrdiff_t i = 0; i < shorter && *first1 == *first2; ++i) {
        ++first1;
        ++first2;
    }
    if (first1 == last1) return first2 != last2 ? -1 : 0;
    if (first2 == last2) return 1;
    return *first2 > *first1 ? -1 : 1;
}

bool __cdecl IsNearZero(float x) { return kNearZero >= std::fabs(x); }

bool __cdecl IsTrue(const Value *value)
{
    if (value->type != 1) ThrowFormatted(kNotScalar);
    return NonZero(value->v[0]);
}

float *__cdecl ScaleRange(const float *first, const float *last, float *dest, ScaleBy scale)
{
    return std::transform(first, last, dest, [factor = scale.factor](float x) { return factor * x; });
}

Value *ExpressionEvaluator::Evaluate(Value *out, const char *text)
{
    ParseExpression(out, &text);
    if (*text != '\0') ThrowFormatted(kGarbageAtEnd, text);
    return out;
}

Value *ExpressionEvaluator::ParseExpression(Value *out, const char **cursor)
{
    SkipBlanks(cursor);
    const char c = **cursor;
    if (c == '\0' || (!IsWordChar(static_cast<unsigned char>(c)) && std::strchr(kExpressionStarts, c) == nullptr))
        ThrowFormatted(kBadExpression, *cursor);
    return ParseSequence(out, cursor);
}

Value *ExpressionEvaluator::ParseSequence(Value *out, const char **cursor)
{
    ParseLogical(out, cursor);
    for (;;) {
        SkipBlanks(cursor);
        if (**cursor != ',') break;
        ++*cursor;
        Value element;
        ParseLogical(&element, cursor);
        if (out->type + element.type > 4) ThrowFormatted(kTooManyElements);
        for (int32_t i = 0; i < element.type; ++i) out->v[out->type + i] = element.v[i];
        out->type += element.type;
    }
    if (**cursor == '?') {
        ++*cursor;
        Value ifTrue, ifFalse;
        ParseExpression(&ifTrue, cursor);
        ExpectChar(cursor, ':');
        ParseExpression(&ifFalse, cursor);
        *out = IsTrue(out) ? ifTrue : ifFalse;
    }
    return out;
}

Value *ExpressionEvaluator::ParseLogical(Value *out, const char **cursor)
{
    ParseComparison(out, cursor);
    for (;;) {
        SkipBlanks(cursor);
        bool isAnd;
        if (TakeKeyword(cursor, kAnd)) isAnd = true;
        else if (TakeKeyword(cursor, kOr)) isAnd = false;
        else return out;
        // Both sides are evaluated whatever the left one is.
        if (out->type != 1) ThrowFormatted(kNotScalar);
        const bool left = NonZero(out->v[0]);
        Value rhs;
        ParseComparison(&rhs, cursor);
        if (rhs.type != 1) ThrowFormatted(kNotScalar);
        const bool right = NonZero(rhs.v[0]);
        SetScalar(out, (isAnd ? left && right : left || right) ? 1.0f : 0.0f);
    }
}

Value *ExpressionEvaluator::ParseComparison(Value *out, const char **cursor)
{
    ParseSum(out, cursor);
    for (;;) {
        SkipBlanks(cursor);
        enum { NotEqual, Less, LessEqual, Equal, GreaterEqual, Greater } op;
        switch (**cursor) {
        case '!':
            ++*cursor;
            if (**cursor != '=') ThrowFormatted(kExpectedEquals);
            ++*cursor;
            op = NotEqual;
            break;
        case '<':
            ++*cursor;
            op = Less;
            if (**cursor == '=') {
                ++*cursor;
                op = LessEqual;
            }
            break;
        case '=':
            ++*cursor;
            op = Equal;
            break;
        case '>':
            ++*cursor;
            op = Greater;
            if (**cursor == '=') {
                ++*cursor;
                op = GreaterEqual;
            }
            break;
        default:
            return out;
        }
        Value rhs;
        ParseSum(&rhs, cursor);
        const int order = Compare3(out->v, out->v + out->type, rhs.v, rhs.v + rhs.type);
        bool result = false;
        switch (op) {
        case NotEqual: result = order != 0; break;
        case Less: result = order < 0; break;
        case LessEqual: result = order <= 0; break;
        case Equal: result = order == 0; break;
        case GreaterEqual: result = order >= 0; break;
        case Greater: result = order > 0; break;
        }
        SetScalar(out, result ? 1.0f : 0.0f);
    }
}

Value *ExpressionEvaluator::ParseSum(Value *out, const char **cursor)
{
    ParseProduct(out, cursor);
    for (;;) {
        SkipBlanks(cursor);
        const char op = **cursor;
        if (op != '+' && op != '-') return out;
        ++*cursor;
        Value rhs;
        ParseProduct(&rhs, cursor);
        for (int i = 0; i < 4; ++i) out->v[i] = op == '+' ? out->v[i] + rhs.v[i] : out->v[i] - rhs.v[i];
        out->type = std::max(out->type, rhs.type);
    }
}

Value *ExpressionEvaluator::ParseProduct(Value *out, const char **cursor)
{
    ParseUnary(out, cursor);
    for (;;) {
        SkipBlanks(cursor);
        const char op = **cursor;
        if (op != '*' && op != '/' && op != '%') return out;
        ++*cursor;
        Value rhs;
        ParseUnary(&rhs, cursor);
        if (op == '*') {
            for (int i = 0; i < 4; ++i) out->v[i] *= rhs.v[i];
            out->type = std::max(out->type, rhs.type);
            continue;
        }
        for (int32_t i = 0; i < rhs.type; ++i)
            if (IsNearZero(rhs.v[i])) ThrowFormatted(kDivisionByZero);
        out->type = std::max(out->type, rhs.type);
        for (int32_t i = 0; i < out->type; ++i) {
            if (op == '/') out->v[i] /= rhs.v[i];
            else out->v[i] = static_cast<float>(std::fmod(static_cast<double>(out->v[i]), static_cast<double>(rhs.v[i])));
        }
    }
}

Value *ExpressionEvaluator::ParseUnary(Value *out, const char **cursor)
{
    SkipBlanks(cursor);
    if (**cursor == '+') {
        ++*cursor;
        return ParseUnary(out, cursor);
    }
    if (**cursor == '-') {
        ++*cursor;
        Value operand;
        ParseUnary(&operand, cursor);
        for (int i = 0; i < 4; ++i) out->v[i] = -operand.v[i];
        out->type = operand.type;
        return out;
    }
    if (TakeKeyword(cursor, kNot)) {
        Value operand;
        ParseUnary(&operand, cursor);
        SetScalar(out, IsTrue(&operand) ? 0.0f : 1.0f);
        return out;
    }
    ParsePrimary(out, cursor);
    SkipBlanks(cursor);
    if (**cursor != '.') return out;
    // A swizzle: the elements named by x, y, z and w (any lane, whatever the element count), at most four.
    ++*cursor;
    const Value source = *out;
    for (int i = 0; i < 4; ++i) out->v[i] = 0.0f;
    out->type = 0;
    for (;;) {
        int lane;
        switch (**cursor) {
        case 'x': lane = 0; break;
        case 'y': lane = 1; break;
        case 'z': lane = 2; break;
        case 'w': lane = 3; break;
        default: return out;
        }
        if (out->type >= 4) ThrowFormatted(kTooManyElements);
        ++*cursor;
        out->v[out->type++] = source.v[lane];
    }
}

Value *ExpressionEvaluator::ParsePrimary(Value *out, const char **cursor)
{
    SkipBlanks(cursor);
    const unsigned char first = static_cast<unsigned char>(**cursor);
    if (IsDigit(first) || first == '.') {
        SetScalar(out, 0.0f);
        const char *end = nullptr;
        out->v[0] = static_cast<float>(rztext::ReadDouble(*cursor, &end));
        if (end == *cursor) ThrowFormatted(kInvalidConstant);
        *cursor = end;
        return out;
    }
    const char *name = *cursor;
    while (IsWordChar(static_cast<unsigned char>(**cursor))) ++*cursor;
    const size_t length = static_cast<size_t>(*cursor - name);
    if (length == 0) {
        ExpectChar(cursor, '(');
        ParseExpression(out, cursor);
        ExpectChar(cursor, ')');
        return out;
    }
    // The names are matched on the name's length: a built-in whose name starts with it is found.
    auto range = std::equal_range(mEntries.begin(), mEntries.end(), name, [length](const auto &a, const auto &b) {
        if constexpr (std::is_same_v<std::decay_t<decltype(a)>, EvalFunctionTable::Entry>) return std::strncmp(a.first, b, length) < 0;
        else return std::strncmp(a, b.first, length) < 0;
    });
    if (range.first == range.second) {
        // The message is the format, with no arguments (the name holds only letters and digits).
        std::string message(kUnknownOperand);
        message.append(name, length);
        ThrowFormatted(message.c_str());
    }
    reinterpret_cast<cIEvalFunction *>(range.first->second)->Evaluate(out, cursor, this);
    return out;
}

QuotedName *ExpressionEvaluator::ReadQuotedName(QuotedName *out, const char **cursor)
{
    ExpectChar(cursor, '"');
    out->text = *cursor;
    out->length = 0;
    while (**cursor != '\0' && **cursor != '"') {
        ++*cursor;
        ++out->length;
    }
    if (**cursor == '\0') ThrowFormatted(kMissingQuote);
    ++*cursor;
    return out;
}

} // namespace nGZCommandParser
