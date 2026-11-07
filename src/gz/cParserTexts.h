#pragma once
// The command parser's texts, owned statics (src/game/owned.h; lane RQ): in the hooked game each is the image's string at
// that RVA; an image-free program defines them (standalone/owned_statics.cpp). The comments give the image's text; the
// separators and keywords are the language's own and must be exact, the messages carry the same printf arguments.
#include "game/owned.h"

// Shared pieces.
TS2_OWNED_STATIC(const char, d9e644)[]; // "\n"
TS2_OWNED_STATIC(const char, d9f86c)[]; // "'\n"
TS2_OWNED_STATIC(const char, da9e7c)[]; // ":" the namespace path separator
TS2_OWNED_STATIC(const char, db12b0)[]; // ")"
TS2_OWNED_STATIC(const char, db12b4)[]; // "("
TS2_OWNED_STATIC(const char, dddf9c)[]; // "'"
TS2_OWNED_STATIC(const char, df7240)[]; // "\n\t"
TS2_OWNED_STATIC(const char, e7eb00)[]; // "' = '"

// The run path (cParserRun.cpp).
TS2_OWNED_STATIC(const char, e7f458)[]; // "'#>' without matching '#<'"
TS2_OWNED_STATIC(const char, e7f2a4)[]; // "Unknown command"
TS2_OWNED_STATIC(const char, e7f2b4)[]; // "%s\n      in command '%s'. ..." (the error's text, the line)
TS2_OWNED_STATIC(const char, e7f2f4)[]; // "%s (unknown exception type)\n      in command '%s'"
TS2_OWNED_STATIC(const char, e7f1fc)[]; // "%s(%d): error: " (file name, line)
TS2_OWNED_STATIC(const char, e7f20c)[]; // "Unbalanced block command in '%s': ..." (file name)
TS2_OWNED_STATIC(const char, e7f250)[]; // "Unbalanced meta command in '%s': ..." (file name)
TS2_OWNED_STATIC(const char, e7f1a4)[]; // "Path type not supported: %s"
TS2_OWNED_STATIC(const char, e7f1c0)[]; // "File not found"
TS2_OWNED_STATIC(const char, e7f1d0)[]; // "Errors parsing files!\n"
TS2_OWNED_STATIC(const char, e7f1e8)[]; // "\n\tUnknown exception"

// The slots (cParserSlots.cpp, cParserNamespaces.cpp) and the variables' trace (cParserVariables.cpp).
TS2_OWNED_STATIC(const char, e7f328)[]; // "Not in a meta command parse block"
TS2_OWNED_STATIC(const char, e7f34c)[]; // "entering namespace '"
TS2_OWNED_STATIC(const char, e7f364)[]; // "leaving namespace '"
TS2_OWNED_STATIC(const char, e7f378)[]; // "purging namespace '"
TS2_OWNED_STATIC(const char, e7f38c)[]; // "checking for var '"
TS2_OWNED_STATIC(const char, e7f3a0)[]; // " -- found, value = '"
TS2_OWNED_STATIC(const char, e7f3b8)[]; // " -- not found!\n"
TS2_OWNED_STATIC(const char, e7e210)[]; // "INTERNAL BUFFER OVERFLOW"
TS2_OWNED_STATIC(const char, e7f3c8)[]; // "missing variable name"
TS2_OWNED_STATIC(const char, e7f3e0)[]; // "bad variable name"
TS2_OWNED_STATIC(const char, e7f3f4)[]; // "missing '}'"
TS2_OWNED_STATIC(const char, e7f400)[]; // "Unknown variable: '%s'" (the name)
TS2_OWNED_STATIC(const char, e79e14)[]; // "not a scalar expression"
TS2_OWNED_STATIC(const char, e7f418)[]; // "too many elements in expression"
TS2_OWNED_STATIC(const char, e7f438)[]; // "too few elements in expression"
TS2_OWNED_STATIC(const char, de7c1c)[]; // "not a 3 or 4 element float expression"

// The expression evaluator's keywords (ExpressionEvaluator.cpp).
TS2_OWNED_STATIC(const char, e7f6d0)[]; // ".-+(": what may start an expression besides a letter or digit
TS2_OWNED_STATIC(const char, e7f718)[]; // "and"
TS2_OWNED_STATIC(const char, e7f71c)[]; // "or"
TS2_OWNED_STATIC(const char, e5c4d4)[]; // "not"
// Its errors.
TS2_OWNED_STATIC(const char, e7f6ac)[]; // "garbage at end of expression:'%s'" (the rest of the text)
TS2_OWNED_STATIC(const char, e7f6d8)[]; // "bad numerical expression '%s'" (the text)
TS2_OWNED_STATIC(const char, e7f6f8)[]; // "too many elements in sequence"
TS2_OWNED_STATIC(const char, e7f720)[]; // "expected '='"
TS2_OWNED_STATIC(const char, e7f730)[]; // "division by zero"
TS2_OWNED_STATIC(const char, e7f744)[]; // "Unknown operand: " (the name is appended)
TS2_OWNED_STATIC(const char, e7f758)[]; // "invalid constant"
TS2_OWNED_STATIC(const char, e7f76c)[]; // "missing '\"'"
TS2_OWNED_STATIC(const char, e7f778)[]; // "expected '%c'" (the character)

// The command parser's commands (CommandParser.cpp; lane RS).
TS2_OWNED_STATIC(const char, da2cd8)[]; // ": "
TS2_OWNED_STATIC(const char, da4ca4)[]; // " "
TS2_OWNED_STATIC(const char, df732c)[]; // ")\n"
TS2_OWNED_STATIC(const char, dfd044)[]; // "   "
TS2_OWNED_STATIC(const char, e7e520)[]; // "}\n"
TS2_OWNED_STATIC(const char, e7e524)[]; // "if {"
TS2_OWNED_STATIC(const char, e7e5c8)[]; // "else {"
TS2_OWNED_STATIC(const char, e7e6bc)[]; // "endif\n"
TS2_OWNED_STATIC(const char, e7e7b0)[]; // "enddef\n"
TS2_OWNED_STATIC(const char, e7ec08)[]; // "arguments:\n"
TS2_OWNED_STATIC(const char, e7e2f8)[]; // "(%g,%g)"
TS2_OWNED_STATIC(const char, e7e368)[]; // "(%g,%g,%g)"
TS2_OWNED_STATIC(const char, e7d48c)[]; // "(%g,%g,%g,%g)"
TS2_OWNED_STATIC(const char, e7e9f0)[]; // "%s %s(%d %d)"
TS2_OWNED_STATIC(const char, e7e818)[]; // "silent"
TS2_OWNED_STATIC(const char, e7eaf4)[]; // "variables"
TS2_OWNED_STATIC(const char, e7eb08)[]; // "definitions"
TS2_OWNED_STATIC(const char, e7eb14)[]; // "scope"
TS2_OWNED_STATIC(const char, e7eb68)[]; // "varAssignment"
TS2_OWNED_STATIC(const char, e7eb78)[]; // "varLookup"
TS2_OWNED_STATIC(const char, e7eb84)[]; // "conditionals"
TS2_OWNED_STATIC(const char, e7eb94)[]; // "instantiations"
TS2_OWNED_STATIC(const char, daa7c8)[]; // "alias"
TS2_OWNED_STATIC(const char, dbc1a4)[]; // "version"
TS2_OWNED_STATIC(const char, dbc374)[]; // "end"
TS2_OWNED_STATIC(const char, dcde40)[]; // "debug"
TS2_OWNED_STATIC(const char, dd0b2c)[]; // "trace"
TS2_OWNED_STATIC(const char, e09304)[]; // "log"
TS2_OWNED_STATIC(const char, e48120)[]; // "define"
TS2_OWNED_STATIC(const char, e7ae7c)[]; // "else"
TS2_OWNED_STATIC(const char, e7dc94)[]; // "include"
TS2_OWNED_STATIC(const char, e7dce4)[]; // "sinclude"
TS2_OWNED_STATIC(const char, e7dd60)[]; // "wildInclude"
TS2_OWNED_STATIC(const char, e7de60)[]; // "ignoreErrors"
TS2_OWNED_STATIC(const char, e7dec8)[]; // "list"
TS2_OWNED_STATIC(const char, e7df44)[]; // "eval"
TS2_OWNED_STATIC(const char, e7dfa0)[]; // "assert"
TS2_OWNED_STATIC(const char, e7e02c)[]; // "set"
TS2_OWNED_STATIC(const char, e7e07c)[]; // "sete"
TS2_OWNED_STATIC(const char, e7e108)[]; // "setb"
TS2_OWNED_STATIC(const char, e7e164)[]; // "seti"
TS2_OWNED_STATIC(const char, e7e1d8)[]; // "setf"
TS2_OWNED_STATIC(const char, e7e258)[]; // "setc"
TS2_OWNED_STATIC(const char, e7e2bc)[]; // "setv2"
TS2_OWNED_STATIC(const char, e7e32c)[]; // "setv3"
TS2_OWNED_STATIC(const char, e7e3a0)[]; // "setv4"
TS2_OWNED_STATIC(const char, e7e414)[]; // "namespace"
TS2_OWNED_STATIC(const char, e7e46c)[]; // "purge"
TS2_OWNED_STATIC(const char, e7e500)[]; // "if"
TS2_OWNED_STATIC(const char, e7e52c)[]; // "elseif"
TS2_OWNED_STATIC(const char, e7e534)[]; // "endif"
TS2_OWNED_STATIC(const char, e7e73c)[]; // "enddef"
TS2_OWNED_STATIC(const char, e7e7e4)[]; // "undefine"
TS2_OWNED_STATIC(const char, e7e878)[]; // "create"
TS2_OWNED_STATIC(const char, e7e9a8)[]; // "arrayCreate"
TS2_OWNED_STATIC(const char, e7eab8)[]; // "dump"
TS2_OWNED_STATIC(const char, e7ebd0)[]; // "showArguments"
TS2_OWNED_STATIC(const char, e7ec40)[]; // "debuggerBreak"
TS2_OWNED_STATIC(const char, e7dc9c)[]; // help
TS2_OWNED_STATIC(const char, e7dcf0)[]; // help
TS2_OWNED_STATIC(const char, e7dd70)[]; // help
TS2_OWNED_STATIC(const char, e7de10)[]; // help
TS2_OWNED_STATIC(const char, e7de70)[]; // help
TS2_OWNED_STATIC(const char, e7ded0)[]; // help
TS2_OWNED_STATIC(const char, e7df4c)[]; // help
TS2_OWNED_STATIC(const char, e7dfa8)[]; // help
TS2_OWNED_STATIC(const char, e7e030)[]; // help
TS2_OWNED_STATIC(const char, e7e088)[]; // help
TS2_OWNED_STATIC(const char, e7e110)[]; // help
TS2_OWNED_STATIC(const char, e7e16c)[]; // help
TS2_OWNED_STATIC(const char, e7e1e0)[]; // help
TS2_OWNED_STATIC(const char, e7e260)[]; // help
TS2_OWNED_STATIC(const char, e7e2c4)[]; // help
TS2_OWNED_STATIC(const char, e7e334)[]; // help
TS2_OWNED_STATIC(const char, e7e3a8)[]; // help
TS2_OWNED_STATIC(const char, e7e420)[]; // help
TS2_OWNED_STATIC(const char, e7e474)[]; // help
TS2_OWNED_STATIC(const char, e7e504)[]; // help
TS2_OWNED_STATIC(const char, e7e568)[]; // help
TS2_OWNED_STATIC(const char, e7e5fc)[]; // help
TS2_OWNED_STATIC(const char, e7e680)[]; // help
TS2_OWNED_STATIC(const char, e7e6f8)[]; // help
TS2_OWNED_STATIC(const char, e7e770)[]; // help
TS2_OWNED_STATIC(const char, e7e7f0)[]; // help
TS2_OWNED_STATIC(const char, e7e880)[]; // help
TS2_OWNED_STATIC(const char, e7e9b4)[]; // help
TS2_OWNED_STATIC(const char, e7ea2c)[]; // help
TS2_OWNED_STATIC(const char, e7ea74)[]; // help
TS2_OWNED_STATIC(const char, e7eac0)[]; // help
TS2_OWNED_STATIC(const char, e7eb48)[]; // help
TS2_OWNED_STATIC(const char, e7ebe0)[]; // help
TS2_OWNED_STATIC(const char, e7ec50)[]; // help
TS2_OWNED_STATIC(const char, e7ec98)[]; // help
TS2_OWNED_STATIC(const char, e7eda0)[]; // help
TS2_OWNED_STATIC(const char, e7ee18)[]; // help
TS2_OWNED_STATIC(const char, e7eeb8)[]; // help, the alias text
TS2_OWNED_STATIC(const char, e7eec8)[]; // help, the alias name and text
TS2_OWNED_STATIC(const char, db59cc)[]; // message
TS2_OWNED_STATIC(const char, e7d628)[]; // message, the name
TS2_OWNED_STATIC(const char, e7de20)[]; // message
TS2_OWNED_STATIC(const char, e7e4b0)[]; // message, the scope
TS2_OWNED_STATIC(const char, e7ecbc)[]; // message, have and need
TS2_OWNED_STATIC(const char, e7ed00)[]; // message, have and limit
TS2_OWNED_STATIC(const char, e7e0cc)[]; // message
TS2_OWNED_STATIC(const char, e7e820)[]; // message
TS2_OWNED_STATIC(const char, e7e834)[]; // message, the name
TS2_OWNED_STATIC(const char, e7e620)[]; // message
TS2_OWNED_STATIC(const char, e7e640)[]; // message
TS2_OWNED_STATIC(const char, e7e58c)[]; // message
TS2_OWNED_STATIC(const char, e7e5b0)[]; // message
TS2_OWNED_STATIC(const char, e7e698)[]; // message
TS2_OWNED_STATIC(const char, e7dfe8)[]; // message, the argument
TS2_OWNED_STATIC(const char, e7e790)[]; // message
TS2_OWNED_STATIC(const char, e7e8a4)[]; // message, the name
TS2_OWNED_STATIC(const char, e7e8c0)[]; // message
TS2_OWNED_STATIC(const char, e7e954)[]; // message, error/definition/line
TS2_OWNED_STATIC(const char, e7e924)[]; // message, file/line/error/definition/line

// The macro definitions (MacroDefinition.cpp; lane RU). Besides these: "\n", " ", "(", ")", "   ", "enddef" and
// "missing '}'" above.
TS2_OWNED_STATIC(const char, e7e718)[]; // message: define inside a definition
TS2_OWNED_STATIC(const char, e7e72c)[]; // message: the name is defined already
TS2_OWNED_STATIC(const char, e7f474)[]; // message: '&' or "&{" ends the text
TS2_OWNED_STATIC(const char, e7f48c)[]; // message: the name after '&' starts with no letter or '_'
TS2_OWNED_STATIC(const char, e7f4a0)[]; // message, the parameter's name appended (the result is a format string)

// The parser functions (ParserFunctions.cpp; lane RS).
TS2_OWNED_STATIC(const char, e83ec4)[]; // message
TS2_OWNED_STATIC(const char, e7f090)[]; // "minVersion"
TS2_OWNED_STATIC(const char, e7f104)[]; // "maxVersion"
TS2_OWNED_STATIC(const char, e7ef44)[]; // "varExists"
TS2_OWNED_STATIC(const char, e7f028)[]; // "commandExists"
TS2_OWNED_STATIC(const char, e7efb8)[]; // "definitionExists"
TS2_OWNED_STATIC(const char, e7f178)[]; // "match"
TS2_OWNED_STATIC(const char, e8401c)[]; // "propExists"
TS2_OWNED_STATIC(const char, e7f09c)[]; // help
TS2_OWNED_STATIC(const char, e7f110)[]; // help
TS2_OWNED_STATIC(const char, e7ef50)[]; // help
TS2_OWNED_STATIC(const char, e7f038)[]; // help
TS2_OWNED_STATIC(const char, e7efcc)[]; // help
TS2_OWNED_STATIC(const char, e7f180)[]; // help
TS2_OWNED_STATIC(const char, e84028)[]; // help
