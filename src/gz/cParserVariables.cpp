// nGZCommandParser::cBaseParser's variables (lane HU): cIBaseParser slot 27 finds a variable through the open
// namespaces, slot 29 expands the $name / ${name} references in a text. RVAs are 1.18.0.187.
#include "gz/cBaseParser.h"
#include "gz/cParserTexts.h"

#include "game/game.h"
#include "game/vtable.h"

#include <algorithm>
#include <cstring>

namespace nGZCommandParser {

namespace {

inline void Write(void *output, const char *text) { VirtualCall<void(__thiscall *)(void *, const char *), 14>(output, text); }

constexpr const char *kCheckingVariable = ts2_rva_e7f38c; // "checking for var '"
constexpr const char *kQuote = ts2_rva_dddf9c;            // "'"
constexpr const char *kFoundValue = ts2_rva_e7f3a0;       // " -- found, value = '"
constexpr const char *kQuoteNewline = ts2_rva_d9f86c;     // "'\n"
constexpr const char *kNotFound = ts2_rva_e7f3b8;         // " -- not found!\n"
constexpr const char *kBufferOverflow = ts2_rva_e7e210;   // "INTERNAL BUFFER OVERFLOW"
constexpr const char *kMissingName = ts2_rva_e7f3c8;      // "missing variable name"
constexpr const char *kBadName = ts2_rva_e7f3e0;          // "bad variable name"
constexpr const char *kMissingBrace = ts2_rva_e7f3f4;     // "missing '}'"
constexpr const char *kUnknownVariable = ts2_rva_e7f400;  // "Unknown variable: '%s'"
constexpr const char *kOpenParenthesis = ts2_rva_db12b4;  // "("
constexpr const char *kCloseParenthesis = ts2_rva_db12b0; // ")"
// The parser's character classes (the game's table at 0xe7d528: bit 1 blanks, bit 2 letters, bit 4 digits; bytes from
// 0x80 up are in none).
bool IsDigit(uint8_t c) { return c >= '0' && c <= '9'; }
bool IsLetter(uint8_t c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

[[noreturn]] void ThrowText(const char *text) { ThrowFormatted(text); }

} // namespace

const char *cBaseParser::FindVariable(const char *name)
{
    if (mTraceVariables) {
        void *output = mOutput;
        Write(output, kCheckingVariable);
        Write(output, name);
        Write(output, kQuote);
    }
    // A leading ':' looks in the outermost namespace only; otherwise the open namespaces are tried innermost first.
    const bool global = name[0] == ':';
    const char *const start = global ? name + 1 : name;
    ParserNamespace **const first = mNamespaces.data();
    ParserNamespace **it = global ? first + 1 : first + mNamespaces.size();
    const size_t length = std::strlen(start);
    while (it != first) {
        ParserNamespace *ns = *--it;
        // The names before each ':' lead through the child namespaces; the last one is the variable.
        const char *ptr = start;
        size_t size = length;
        bool missing = false;
        while (const void *colon = std::memchr(ptr, ':', size)) {
            const size_t at = static_cast<size_t>(static_cast<const char *>(colon) - ptr);
            const KeyView key{ptr, static_cast<uint32_t>(std::min(at, size))};
            FindResult found;
            ns->mCommands.Find(&found, &key, HashName(key.ptr, key.length));
            CommandNode *node = found.found ? found.found : ns->mCommands.head;
            if (node == ns->mCommands.head) {
                missing = true;
                break;
            }
            ns = static_cast<ParserNamespace *>(node->value);
            const size_t step = std::min(at + 1, size);
            ptr += step;
            size -= step;
        }
        if (missing || !ns) continue;
        const KeyView key{ptr, static_cast<uint32_t>(size)};
        FindResult found;
        // The variable table has the command table's layout (the game runs the same lookup on it).
        reinterpret_cast<CommandTable *>(&ns->mVariables)->Find(&found, &key, HashName(key.ptr, key.length));
        auto *node = reinterpret_cast<VariableNode *>(found.found);
        if (!node || node == ns->mVariables.head) continue;
        const char *value = node->value.c_str();
        if (mTraceVariables) {
            void *output = mOutput;
            Write(output, kFoundValue);
            Write(output, value);
            Write(output, kQuoteNewline);
        }
        return value;
    }
    if (mTraceVariables) Write(mOutput, kNotFound);
    return nullptr;
}

void cBaseParser::ExpandVariables(void *text, bool parenthesize)
{
    const uint32_t length = VirtualCall<uint32_t(__thiscall *)(void *), 7>(text); // Strlen
    if (length == 0) return;
    const char *data = VirtualCall<const char *(__thiscall *)(void *), 5>(text); // ToChar
    uint32_t dollar = static_cast<uint32_t>(std::find(data, data + length, '$') - data);
    if (dollar == length) return;
    if (length > 0x7ff) ThrowText(kBufferOverflow);
    char buffer[0x800];
    std::memcpy(buffer, data, length + 1);
    VirtualCall<void(__thiscall *)(void *, uint32_t), 18>(text, 0); // emptied, then rebuilt piece by piece
    auto append = [text](const char *p, uint32_t n) { VirtualCall<void(__thiscall *)(void *, const char *, uint32_t), 20>(text, p, n); };
    auto nameChar = [](uint8_t c) { return IsLetter(c) || IsDigit(c) || c == '_' || c == ':'; };
    uint32_t pos = 0;
    do {
        append(buffer + pos, dollar - pos);
        if (dollar == length) return;
        const char kind = buffer[dollar + 1];
        uint32_t searchFrom;
        if (kind == '$') {
            // "$$": one '$' kept (it starts the next piece), the search goes on after it.
            pos = dollar + 1;
            searchFrom = dollar + 2;
        } else {
            const uint32_t start = kind == '{' ? dollar + 2 : dollar + 1;
            if (start >= length) ThrowText(kMissingName);
            const uint8_t firstChar = static_cast<uint8_t>(buffer[start]);
            if (!nameChar(firstChar) || IsDigit(firstChar)) ThrowText(kBadName);
            uint32_t end = start + 1;
            while (end < length && nameChar(static_cast<uint8_t>(buffer[end]))) ++end;
            if (kind == '{' && (end >= length || buffer[end] != '}')) ThrowText(kMissingBrace);
            char *const cut = buffer + end;
            const char saved = *cut;
            *cut = 0;
            const char *value = VirtualCall<const char *(__thiscall *)(void *, const char *), 27>(ParserFace(), buffer + start);
            if (!value) ThrowFormatted(kUnknownVariable, buffer + start);
            *cut = saved;
            if (kind != '{' && parenthesize) append(kOpenParenthesis, 1);
            append(value, static_cast<uint32_t>(std::strlen(value)));
            if (kind == '{') {
                pos = end + 1;
            } else {
                if (parenthesize) append(kCloseParenthesis, 1);
                pos = end;
            }
            searchFrom = pos;
        }
        dollar = static_cast<uint32_t>(std::find(buffer + searchFrom, buffer + length, '$') - buffer);
    } while (pos < length);
}

const char *__fastcall cBaseParserAdapters::FindVariable(void *face, void *, const char *name)
{
    return cBaseParser::FromParserFace(face)->FindVariable(name);
}
void __fastcall cBaseParserAdapters::ExpandVariables(void *face, void *, void *text, bool parenthesize)
{
    cBaseParser::FromParserFace(face)->ExpandVariables(text, parenthesize);
}

} // namespace nGZCommandParser
