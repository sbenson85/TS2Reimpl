// The command parser's own expression functions (lane NV). See gz/ParserFunctions.h.
#include "gz/ParserFunctions.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/CommandParser.h"
#include "gz/cBaseParser.h"
#include "gz/cParserTexts.h"
#include "gz/cIGZPropertySet.h"
#include "ifaces/cIBaseParser.h"
#include "rz/RZWildcard.h"

#include <string>

namespace nGZCommandParser {

namespace {


// The parser's character classes (the game's table at 0xe7d528: bit 1 blanks, bit 2 letters, bit 4 digits; bytes from
// 0x80 up are in none).
bool IsBlank(unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool IsLetterOrDigit(unsigned char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

// A value of one element; the other lanes zero.
Value *SetScalar(Value *out, float x)
{
    out->type = 1;
    out->v[0] = x;
    out->v[1] = 0.0f;
    out->v[2] = 0.0f;
    out->v[3] = 0.0f;
    return out;
}

// "(name)": the name read between the parentheses.
std::string ReadParenthesisedName(const char **cursor, ExpressionEvaluator *evaluator)
{
    ExpectChar(cursor, '(');
    NameToken name;
    ReadName(evaluator, nullptr, &name, cursor);
    ExpectChar(cursor, ')');
    return std::string(name.text, name.length);
}

ifaces::cIBaseParser *ParserFace(cBaseParser *parser) { return As<ifaces::cIBaseParser>(parser->ParserFace()); }

} // namespace

NameToken *__fastcall ReadName(ExpressionEvaluator * /*evaluator*/, void * /*edx*/, NameToken *out, const char **cursor)
{
    while (IsBlank(static_cast<unsigned char>(**cursor))) ++*cursor;
    out->text = *cursor;
    out->length = 0;
    while (IsLetterOrDigit(static_cast<unsigned char>(**cursor))) {
        ++*cursor;
        ++out->length;
    }
    return out;
}

Value *cMinVersionFunction::Evaluate(Value *out, const char **cursor, ExpressionEvaluator * /*evaluator*/)
{
    ExpectChar(cursor, '(');
    ExpectChar(cursor, ')');
    return SetScalar(out, static_cast<float>(mParser->mLowestVersion));
}

Value *cMaxVersionFunction::Evaluate(Value *out, const char **cursor, ExpressionEvaluator * /*evaluator*/)
{
    ExpectChar(cursor, '(');
    ExpectChar(cursor, ')');
    return SetScalar(out, static_cast<float>(mParser->mHighestVersion));
}

Value *cVarExistsFunction::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    const std::string name = ReadParenthesisedName(cursor, evaluator);
    const bool found = ParserFace(mParser)->FindVariable(name.c_str()) != nullptr;
    return SetScalar(out, found ? 1.0f : 0.0f);
}

Value *cCommandExistsFunction::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    const std::string name = ReadParenthesisedName(cursor, evaluator);
    return SetScalar(out, ParserFace(mParser)->HasCommand(name.c_str()) ? 1.0f : 0.0f);
}

Value *cDefinitionExistsFunction::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    const std::string name = ReadParenthesisedName(cursor, evaluator);
    return SetScalar(out, ParserFace(mParser)->HasDefinition(name.c_str()) ? 1.0f : 0.0f);
}

Value *cMatchFunction::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    // match("text", "pattern"): the quoted texts (the original copies each whole rest of the line into a cRZString and
    // keeps its first `length` characters: the same text).
    ExpectChar(cursor, '(');
    QuotedName textName, patternName;
    evaluator->ReadQuotedName(&textName, cursor);
    ExpectChar(cursor, ',');
    evaluator->ReadQuotedName(&patternName, cursor);
    ExpectChar(cursor, ')');
    const std::string text(textName.text, textName.length);
    const std::string pattern(patternName.text, patternName.length);
    return SetScalar(out, RZWildcard::MatchCase(text.c_str(), pattern.c_str(), 0) ? 1.0f : 0.0f);
}

Value *cPropExistsFunction::Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator)
{
    if (!mPropertySet) ThrowFormatted(ts2_rva_e83ec4);
    ExpectChar(cursor, '(');
    NameToken name;
    ReadName(evaluator, nullptr, &name, cursor);
    ExpectChar(cursor, ')');
    const GZServiceString key(name.text, name.length);
    return SetScalar(out, static_cast<::cIGZPropertySetString *>(mPropertySet)->HasProperty(&key) ? 1.0f : 0.0f);
}

// ---- names and help texts (the face's slots 5 and 6; `this` is not used) ----------------------------------------------

#define NGZ_FUNCTION_STRINGS(cls, name, help)                                                             \
    const char *cls::GetName() const { return ts2_rva_##name; }                                        \
    const char *cls::GetHelp(uint32_t) const { return ts2_rva_##help; }

NGZ_FUNCTION_STRINGS(cMinVersionFunction, e7f090, e7f09c)
NGZ_FUNCTION_STRINGS(cMaxVersionFunction, e7f104, e7f110)
NGZ_FUNCTION_STRINGS(cVarExistsFunction, e7ef44, e7ef50)
NGZ_FUNCTION_STRINGS(cCommandExistsFunction, e7f028, e7f038)
NGZ_FUNCTION_STRINGS(cDefinitionExistsFunction, e7efb8, e7efcc)
NGZ_FUNCTION_STRINGS(cMatchFunction, e7f178, e7f180)
NGZ_FUNCTION_STRINGS(cPropExistsFunction, e8401c, e84028)
#undef NGZ_FUNCTION_STRINGS

} // namespace nGZCommandParser
