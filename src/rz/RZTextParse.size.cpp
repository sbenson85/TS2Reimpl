// The game's shared text parsers (src/rz/RZTextParse.h): a size-built module of the original.
#include "rz/RZTextParse.h"

#include "rz/cRZString.h"

#include <cctype>
#include <cmath>
#include <string>

namespace rztext {

namespace {

bool IsBlank(char c) { return c == ' ' || c == '\t'; }

// A part in double quotes loses them: the last character first, then (if anything is left) the first.
void StripQuotes(std::string &part)
{
    if (part[0] == '"' && part[part.size() - 1] == '"') {
        part.erase(part.size() - 1, 1);
        if (!part.empty()) part.erase(0, 1);
    }
}

} // namespace

uint64_t __cdecl ParseHex64(const char *text)
{
    while (isspace(*text)) ++text;
    const int start = text[0] == '0' && (text[1] == 'x' || text[1] == 'X') ? 2 : 0;
    // At most 16 digits, so shifting each in is the original's sum of digit * 16^k.
    uint64_t value = 0;
    for (int i = start; i < start + 16; ++i) {
        const int c = text[i];
        uint32_t digit;
        if (static_cast<unsigned>(c - '0') <= 9) digit = c - '0';
        else if (static_cast<unsigned>(c - 'a') <= 5) digit = c - 'a' + 10;
        else if (static_cast<unsigned>(c - 'A') <= 5) digit = c - 'A' + 10;
        else break;
        value = (value << 4) | digit;
    }
    return value;
}

uint64_t __cdecl ParseInt64(const char *text)
{
    while (isspace(*text)) ++text;
    char sign = '+';
    int c = *text++;
    if (c == '-' || c == '+') {
        sign = static_cast<char>(c);
        c = *text++;
    }
    uint32_t base;
    if (c != '0') {
        base = 10;
    } else if (*text == 'x' || *text == 'X') {
        base = 16;
        c = text[1];
        text += 2;
    } else {
        base = 8; // the leading '0' is read as the first (octal) digit
    }
    uint64_t value = 0;
    for (;;) {
        uint32_t digit;
        if (isdigit(c)) digit = c - '0';
        else if (isalpha(c)) digit = toupper(c) - ('A' - 10);
        else break;
        if (digit >= base) break;
        value = value * base + digit;
        c = *text++;
    }
    return sign == '-' ? 0 - value : value;
}

double __cdecl PowerOf(int base, int exponent)
{
    return std::pow(static_cast<double>(base), static_cast<double>(exponent));
}

double __cdecl ReadDouble(const char *text, const char **end)
{
    while (isspace(*text)) ++text;
    char sign = '+';
    const char *at = text; // the character in c: the first one not taken yet
    int c = *at;
    if (c == '-' || c == '+') {
        sign = static_cast<char>(c);
        c = *++at;
    }
    double value = 0.0;
    while (c >= '0' && c <= '9') {
        value = value * 10.0 + (c - '0');
        c = *++at;
    }
    if (c == '.') {
        c = *++at;
        double scale = 1.0;
        while (c >= '0' && c <= '9') {
            scale *= 0.1;
            value += (c - '0') * scale;
            c = *++at;
        }
    }
    if (c == 'e' || c == 'E') {
        char exponentSign = '+';
        c = *++at;
        if (c == '-' || c == '+') {
            exponentSign = static_cast<char>(c);
            c = *++at;
        }
        uint32_t exponent = 0; // wraps as the original's 32-bit multiply
        while (c >= '0' && c <= '9') {
            exponent = exponent * 10 + (c - '0');
            c = *++at;
        }
        double factor = PowerOf(10, static_cast<int>(exponent));
        if (exponentSign == '-') factor = 1.0 / factor;
        value = factor * value;
    }
    if (end) *end = at;
    return sign == '-' ? -value : value;
}

double __cdecl ParseDouble(const char *text)
{
    const char *end;
    return ReadDouble(text, &end);
}

bool __cdecl SplitText(const char *text, uint32_t size, char separator, cRZString *parts, uint32_t *count)
{
    const char *const textEnd = text + size;
    const char *first = text;
    while (first < textEnd && IsBlank(*first)) ++first;
    if (first == textEnd) {
        *count = 0;
        return true;
    }
    // ORIGINAL-BUG: the trailing white space loop tests the first character (never blank here) instead of the last,
    // so the end is never trimmed and the last part keeps its trailing white space.
    const char *last = textEnd - 1;
    while (last >= text && IsBlank(*first)) --last;
    const std::string whole(first, last - first + 1);

    const uint32_t room = *count;
    *count = 0;
    bool complete = true;
    uint32_t quotes = 0;
    uint32_t start = 0;
    uint32_t i = 0;
    for (; i < whole.size(); ++i) {
        if (*count >= room) break;
        const char c = whole[i];
        const bool isSeparator = separator == ' ' ? IsBlank(c) : c == separator;
        if (!isSeparator) {
            if (c == '"') ++quotes;
            continue;
        }
        if (quotes & 1) continue;
        std::string &part = parts[*count].mString;
        part.assign(whole.data() + start, i - start);
        if (separator != ' ' && !part.empty()) {
            int j = static_cast<int>(part.size()) - 1;
            while (j >= 0 && IsBlank(part[j])) --j;
            if (j < static_cast<int>(part.size()) - 1) part.resize(j + 1, '\0');
        }
        if (!part.empty()) StripQuotes(part);
        while (i + 1 < whole.size() && IsBlank(whole[i + 1])) ++i;
        ++*count;
        start = i + 1;
        quotes = 0;
    }
    if (*count < room && i - start != 0) {
        std::string &part = parts[*count].mString;
        part.assign(whole.data() + start, i - start);
        StripQuotes(part);
        ++*count;
        start = i + 1;
    }
    if (i < whole.size() || start < i) complete = false;
    return complete;
}

} // namespace rztext
