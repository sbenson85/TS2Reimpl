// The Type 1 program as Font Fusion read it (src/font/FontFusionType1.h).
#include "font/FontFusionType1.h"

#include <cmath>
#include <cstring>

namespace fontfusion {

uint8_t Type1Decrypt(uint8_t cipher, uint16_t *r)
{
    const uint8_t plain = static_cast<uint8_t>(cipher ^ (*r >> 8));
    *r = static_cast<uint16_t>((cipher + *r) * 52845u + 22719u);
    return plain;
}

namespace {

constexpr size_t kNone = static_cast<size_t>(-1);
constexpr uint16_t kEexecKey = 55665;
constexpr uint16_t kCharstringKey = 4330;

bool IsDigit(uint8_t c)
{
    return c >= '0' && c <= '9';
}

bool IsHexDigit(uint8_t c)
{
    return IsDigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

uint8_t HexValue(uint8_t c)
{
    if (c <= '9') return static_cast<uint8_t>(c - '0');
    if (c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
    return static_cast<uint8_t>(c - 'a' + 10);
}

class Text {
public:
    std::vector<uint8_t> bytes; // with a terminating 0 past `end`
    size_t end = 0;             // the program's length after decryption

    uint8_t At(size_t i) const { return i < bytes.size() ? bytes[i] : 0; }

    // The offset just past the first `needle` starting in [from, to), or kNone.
    size_t Find(const char *needle, size_t from, size_t to) const
    {
        const size_t n = std::strlen(needle);
        for (size_t i = from; i < to; ++i) {
            if (bytes[i] != static_cast<uint8_t>(needle[0])) continue;
            size_t k = 1;
            while (k < n && At(i + k) == static_cast<uint8_t>(needle[k])) ++k;
            if (k == n) return i + n;
        }
        return kNone;
    }

    // A whole number (16 bits, as Font Fusion keeps them): skips to the first digit or '-', then reads.
    int16_t Integer(size_t at) const
    {
        while (at < bytes.size() && !IsDigit(bytes[at]) && bytes[at] != '-') ++at;
        const bool negative = At(at) == '-';
        if (negative) ++at;
        uint16_t value = 0;
        while (IsDigit(At(at))) value = static_cast<uint16_t>(value * 10 + (bytes[at++] - '0'));
        return static_cast<int16_t>(negative ? -static_cast<int16_t>(value) : value);
    }

    // The whole number ending at or before `at` (spaces before it skipped): the length in "<length> RD ".
    int16_t IntegerBefore(size_t at) const
    {
        size_t i = at;
        while (i > 0 && At(i) == ' ') --i;
        while (i > 0 && IsDigit(At(i))) --i;
        if (!IsDigit(At(i))) ++i;
        uint16_t value = 0;
        while (IsDigit(At(i))) value = static_cast<uint16_t>(value * 10 + (bytes[i++] - '0'));
        return static_cast<int16_t>(value);
    }

    // A decimal number (with optional fraction and exponent) times 10^`shift`, in 16.16.
    int32_t Fixed(size_t at, int shift) const
    {
        while (at < bytes.size() && !IsDigit(bytes[at]) && bytes[at] != '-' && bytes[at] != '.') ++at;
        double sign = 1.0;
        if (At(at) == '-') {
            sign = -1.0;
            ++at;
        }
        double value = 0.0;
        while (IsDigit(At(at))) value = value * 10.0 + (bytes[at++] - '0');
        if (At(at) == '.') {
            ++at;
            double scale = 0.1;
            while (IsDigit(At(at))) {
                value += (bytes[at++] - '0') * scale;
                scale *= 0.1;
            }
        }
        if (At(at) == 'e' || At(at) == 'E') shift += Integer(at + 1);
        return static_cast<int32_t>(std::lround(sign * value * std::pow(10.0, shift) * 65536.0));
    }

    // The name at `at` (its bytes above a space).
    size_t NameLength(size_t at) const
    {
        size_t n = 0;
        while (At(at + n) > 0x20) ++n;
        return n;
    }
};

// Decrypts the eexec part at `start` in place (binary or, when its first four bytes are hex digits, hexadecimal),
// and every charstring and subroutine in it ("<length> RD " / "<length> -| " then the bytes, under a second key).
// Returns the plaintext's length.
size_t DecryptPrivate(Text &text, size_t start)
{
    const size_t length = text.end - start;
    const bool hex = length >= 4 && IsHexDigit(text.bytes[start]) && IsHexDigit(text.bytes[start + 1]) &&
                     IsHexDigit(text.bytes[start + 2]) && IsHexDigit(text.bytes[start + 3]);
    uint16_t r = kEexecKey;
    uint16_t charstringKey = 0;
    int32_t charstringLeft = 0;
    uint8_t back1 = 0, back2 = 0, back3 = 0; // the plaintext before the current byte
    size_t in = start, out = start;
    const size_t stop = text.end;
    auto next = [&](uint8_t *cipher) {
        if (!hex) {
            if (in >= stop) return false;
            *cipher = text.bytes[in++];
            return true;
        }
        uint8_t digits[2];
        for (uint8_t &d : digits) {
            while (in < stop && (text.bytes[in] == ' ' || text.bytes[in] == '\n' || text.bytes[in] == '\r' ||
                                 text.bytes[in] == '\t'))
                ++in;
            if (in >= stop) return false;
            d = text.bytes[in++];
        }
        *cipher = static_cast<uint8_t>(HexValue(digits[0]) << 4 | HexValue(digits[1]));
        return true;
    };
    uint8_t cipher;
    while (next(&cipher)) {
        uint8_t plain = Type1Decrypt(cipher, &r);
        if (charstringLeft > 0) {
            plain = Type1Decrypt(plain, &charstringKey);
            --charstringLeft;
        } else if (plain == ' ' && back3 == ' ' && ((back2 == 'R' && back1 == 'D') || (back2 == '-' && back1 == '|'))) {
            text.bytes[out] = plain;
            charstringLeft = out >= 4 ? text.IntegerBefore(out - 3) : 0;
            charstringKey = kCharstringKey;
        }
        text.bytes[out++] = plain;
        back3 = back2;
        back2 = back1;
        back1 = plain;
    }
    return out - start;
}

} // namespace

void Type1Program::Parse(const uint8_t *data, uint32_t length, const StandardCode &standardCode)
{
    *this = Type1Program();
    Text text;
    text.bytes.assign(data, data + length);
    text.bytes.push_back(0);
    text.end = length;

    // The private part: after "currentfile eexec" and its line ends.
    size_t privateStart = 0;
    const size_t eexec = text.Find("ntfile eexec", 0, text.end);
    if (eexec != kNone) {
        size_t at = eexec;
        while (at < text.end && (text.bytes[at] == '\r' || text.bytes[at] == '\n')) ++at;
        privateStart = at;
        if (privateStart > 0) text.end = privateStart + DecryptPrivate(text, privateStart);
        text.bytes.resize(text.end);
        text.bytes.push_back(0);
    }

    const size_t charStrings = text.Find("/CharStrings ", privateStart, text.end);
    if (charStrings == kNone) return;
    valid = true;
    const int16_t glyphCount = text.Integer(charStrings);

    // The encoding.
    const size_t encodingAt = text.Find("/Encoding ", 0, text.end);
    uint32_t nextCode = 0x100;
    int16_t encodingSize = 0;
    if (encodingAt != kNone && IsDigit(text.At(encodingAt))) {
        encoding = Encoding::kCustom;
        encodingSize = text.Integer(encodingAt);
        nextCode = static_cast<uint16_t>(encodingSize);
    } else if (encodingAt != kNone && text.At(encodingAt) == 'I') {
        encoding = Encoding::kIsoLatin1;
    }

    // The code of the first "dup <code> ... /name" among the next `encodingSize` after the encoding.
    auto codeInEncoding = [&](const std::string &name) -> int32_t {
        size_t from = encodingAt;
        for (int32_t k = 0; k < encodingSize; ++k) {
            const size_t dup = text.Find("dup ", from, text.end);
            if (dup == kNone) return -1;
            const int16_t code = text.Integer(dup);
            size_t slash = dup;
            while (slash < text.bytes.size() && text.bytes[slash] != '/') ++slash;
            if (slash >= text.bytes.size()) return -1;
            const size_t at = slash + 1;
            const size_t n = text.NameLength(at);
            if (n == name.size() && std::memcmp(&text.bytes[at], name.data(), n) == 0) return code;
            from = at;
        }
        return -1;
    };

    // The glyphs: "/name <length> RD " after "/CharStrings ", skipping each charstring.
    uint8_t back1 = 0, back2 = 0, back3 = 0;
    int32_t skip = 0;
    size_t nameAt = kNone;
    for (size_t pos = charStrings; pos < text.end; ++pos) {
        const uint8_t c = text.bytes[pos];
        if (skip > 0) {
            --skip;
        } else if (c == '/') {
            nameAt = pos + 1;
        } else if (c == ' ' && back3 == ' ' && ((back2 == 'R' && back1 == 'D') || (back2 == '-' && back1 == '|')) &&
                   nameAt != kNone) {
            const int16_t charstringLength = pos >= 4 ? text.IntegerBefore(pos - 3) : 0;
            size_t n = 0;
            while (text.At(nameAt + n) > 0x20 && n < 0x3f) ++n;
            std::string name(reinterpret_cast<const char *>(&text.bytes[nameAt]), n);
            if (nameAt + n < text.bytes.size()) text.bytes[nameAt + n] = 0; // Font Fusion ends the name in place
            int32_t code = -1;
            if (encoding == Encoding::kCustom) {
                code = codeInEncoding(name);
            } else if (standardCode) {
                code = standardCode(name, encoding == Encoding::kIsoLatin1);
            }
            if (static_cast<int16_t>(code) == -1 || static_cast<int16_t>(code) == -11) {
                code = static_cast<uint16_t>(nextCode++);
                unencoded.emplace_back(name, static_cast<uint16_t>(code));
            } else {
                codeByName[name] = static_cast<uint16_t>(code);
            }
            glyphNames.push_back(std::move(name));
            glyphCodes.push_back(static_cast<uint16_t>(code));
            if (static_cast<int32_t>(glyphNames.size()) >= glyphCount) break;
            skip = charstringLength;
        }
        back3 = back2;
        back2 = back1;
        back1 = c;
    }
    const uint16_t next = static_cast<uint16_t>(nextCode);
    if (next > 1) lastCharCode = static_cast<uint16_t>(next - 1);

    // The font-wide values, each searched for before "/CharStrings ".
    auto value = [&](const char *key, int32_t fallback) -> int32_t {
        const size_t at = text.Find(key, 0, charStrings);
        return at == kNone ? fallback : text.Integer(at);
    };
    unitsPerEm = value("/em ", 1000);
    ascent = value("/ascent ", 0x7fff);
    descent = -value("/descent ", -0x7fff);
    underlinePosition = value("/UnderlinePosition ", 0);
    underlineThickness = value("/UnderlineThickness ", 0);
    if (const size_t at = text.Find("/ItalicAngle ", 0, charStrings); at != kNone) italicAngle = text.Fixed(at, 0);
    if (const size_t at = text.Find("/isFixedPitch ", 0, charStrings); at != kNone) isFixedPitch = text.At(at) == 't';
    if (size_t at = text.Find("/FontMatrix ", 0, charStrings); at != kNone) {
        while (at < charStrings && text.At(at) != '[') ++at;
        // [a b c d tx ty]: Font Fusion keeps a, b, c, d times 1000.
        for (int i = 0; i < 4 && at < charStrings; ++i) {
            ++at;
            while (text.At(at) == ' ') ++at;
            fontMatrix[i] = text.Fixed(at, 3);
            while (text.At(at) != ' ' && text.At(at) != ']' && at < charStrings) ++at;
        }
    }
    if (size_t at = text.Find("/FullName", 0, charStrings); at != kNone) {
        for (int i = 0; i < 3; ++i)
            if (text.At(at) != '(') ++at;
        if (text.At(at) == '(') {
            ++at;
            size_t n = 0;
            while (text.At(at + n) != ')' && n < 0x50) ++n;
            hasFullName = true;
            fullName.assign(reinterpret_cast<const char *>(&text.bytes[at]), n);
        }
    }
}

uint32_t Type1Program::GlyphIndex(uint16_t code, bool *found) const
{
    for (size_t i = 0; i < glyphCodes.size(); ++i) {
        if (glyphCodes[i] == code) {
            *found = true;
            return static_cast<uint32_t>(i);
        }
    }
    *found = false;
    return 0;
}

uint16_t Type1Program::CodeByName(const char *name) const
{
    const auto known = codeByName.find(name);
    if (known != codeByName.end()) return known->second;
    for (const auto &entry : unencoded)
        if (entry.first == name) return entry.second;
    return 0;
}

int32_t Type1Program::GlyphForMetrics(uint16_t code) const
{
    bool found = false;
    const uint32_t index = GlyphIndex(code, &found);
    return index != 0 ? static_cast<int32_t>(index) : -1;
}

} // namespace fontfusion
