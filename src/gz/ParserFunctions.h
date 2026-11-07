#pragma once
// The command parser's own expression functions (lane NV): minVersion, maxVersion, varExists, commandExists,
// definitionExists and match (the six cBaseParser::Initialise registers through face slot 7), and the effects
// scripts' propExists. RVAs are 1.18.0.187.
//
// Hook shape: the game's objects keep the game's vtables; slot 0 of the function vtable (Evaluate, __thiscall, ret 0xc,
// the result through `out`) and face slots 5 / 6 (GetName, GetHelp: `this` is the face, not used) are hooked to the
// members here. The shared base slots (QueryInterface 0xaf7c60, AddRef 0x148fb0, Release 0xaf7d80, SetContext,
// GetFunction 0x142700, the deleting destructors 0x247a80 / 0xb3b4b0) are folded across the expression functions and
// stay with the cExpressionFunction work.
//
// Layout of cFunctionT<cBaseParser> (0x10 bytes): +0 the cFunction vtable, +4 the cIExpressionFunction face, +8 the
// reference count, +0xc the parser (SetContext's state: the whole cBaseParser). cPropExistsFunction is a
// cFunctionT<cNullState> (0x14 bytes): +0xc the null state, +0x10 the property set it asks (cIGZPropertySetString,
// given when it is made, 0xb3bc40).
#include "gz/ExpressionEvaluator.h"

#include <cstdint>

namespace nGZCommandParser {

class cBaseParser;

// The common layout of the function objects (the members above; nothing here is virtual).
class cParserFunctionBase {
public:
    const void *mVTable;     // +0 cFunction
    const void *mFaceVTable; // +4 cIExpressionFunction
    int32_t mRefCount;       // +8
};

// cFunctionT<cBaseParser>.
class cParserFunction : public cParserFunctionBase {
public:
    cBaseParser *mParser; // +0xc
};
static_assert(sizeof(cParserFunction) == 0x10, "cFunctionT<cBaseParser> layout");

#define NGZ_PARSER_FUNCTION(Name, Base)                                                                \
    class Name : public Base {                                                                          \
    public:                                                                                             \
        Value *Evaluate(Value *out, const char **cursor, ExpressionEvaluator *evaluator);              \
        const char *GetName() const;                                                                    \
        const char *GetHelp(uint32_t index) const;                                                      \
    }

// "minVersion()" (0xafeeb0): the parser's lowest accepted version (+0x60, set by face slot 2) as one element.
// GetName 0xb03cf0, GetHelp 0xaff750.
NGZ_PARSER_FUNCTION(cMinVersionFunction, cParserFunction);
// "maxVersion()" (0xafee50): the highest accepted version (+0x64). GetName 0xb03ce0, GetHelp 0xaff740.
NGZ_PARSER_FUNCTION(cMaxVersionFunction, cParserFunction);
// "varExists(name)" (0xafef10): 1 when the parser finds the variable (face slot 27), else 0. GetName 0xb03df0, GetHelp
// 0xaff850.
NGZ_PARSER_FUNCTION(cVarExistsFunction, cParserFunction);
// "commandExists(name)" (0xafe850): face slot 49. GetName 0xb03bb0, GetHelp 0xaff610.
NGZ_PARSER_FUNCTION(cCommandExistsFunction, cParserFunction);
// "definitionExists(name)" (0xafe970): face slot 48. GetName 0xb03c00, GetHelp 0xaff660.
NGZ_PARSER_FUNCTION(cDefinitionExistsFunction, cParserFunction);
// "match(\"text\", \"pattern\")" (0xafeb90): RZWildcard::MatchCase(text, pattern, not case-sensitive) as 1 or 0.
// GetName 0xb03cd0, GetHelp 0xaff730.
NGZ_PARSER_FUNCTION(cMatchFunction, cParserFunction);

// cFunctionT<cNullState> with the property set.
class cPropertySetFunction : public cParserFunctionBase {
public:
    void *mState;       // +0xc (the null state; not read)
    void *mPropertySet; // +0x10 cIGZPropertySetString
};
static_assert(sizeof(cPropertySetFunction) == 0x14, "cPropExistsFunction layout");

// "propExists(name)" (0xb3b1e0): "no property set" without one; else 1 when the set has the property (its slot 10).
// GetName 0xb3bb90, GetHelp 0xb3b520.
NGZ_PARSER_FUNCTION(cPropExistsFunction, cPropertySetFunction);
#undef NGZ_PARSER_FUNCTION

// A word as ReadName finds it: its first character and its length (not terminated).
struct NameToken {
    const char *text; // +0
    uint32_t length;  // +4
};
static_assert(sizeof(NameToken) == 8, "NameToken layout");

// 0xb0a600 (__thiscall on the evaluator, `this` not used, ret 8): blanks skipped (the cursor moves past them), then the
// letters and digits that follow (the cursor moves past them too). An empty name is not an error here.
NameToken *__fastcall ReadName(ExpressionEvaluator *evaluator, void *edx, NameToken *out, const char **cursor);

} // namespace nGZCommandParser
