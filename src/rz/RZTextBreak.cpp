#include "rz/RZTextBreak.h"

#include "rz/RZTextChars.h"
#include "rz/RZTextConvert.h"

#include <algorithm>
#include <cstddef>
#include <iterator>

namespace RZText {

namespace {

const uint16_t kNoLineStart[] = {
    0x0021, 0x0025, 0x0029, 0x002c, 0x002e, 0x003a, 0x003b, 0x003f,
    0x005d, 0x007d, 0x00a8, 0x00b0, 0x00b1, 0x00b7, 0x00f7, 0x02c7,
    0x02c9, 0x02cd, 0x0394, 0x039b, 0x2015, 0x2016, 0x2019, 0x201d,
    0x2025, 0x2026, 0x2027, 0x2030, 0x2032, 0x2033, 0x203b, 0x2103,
    0x2190, 0x2191, 0x2192, 0x2196, 0x2197, 0x2198, 0x2199, 0x221f,
    0x2220, 0x2225, 0x222b, 0x2234, 0x2235, 0x2236, 0x2252, 0x2266,
    0x2267, 0x2295, 0x2299, 0x22bf, 0x2502, 0x250c, 0x2514, 0x2518,
    0x2524, 0x252c, 0x253c, 0x2551, 0x2552, 0x2553, 0x2554, 0x2558,
    0x2559, 0x255a, 0x255b, 0x255c, 0x255d, 0x2561, 0x2562, 0x2563,
    0x2565, 0x2566, 0x256a, 0x256b, 0x256c, 0x256d, 0x256f, 0x2570,
    0x2595, 0x25cb, 0x25cf, 0x25e2, 0x25e3, 0x25e4, 0x25e5, 0x2606,
    0x2640, 0x2642, 0x3001, 0x3002, 0x3003, 0x3005, 0x3008, 0x3009,
    0x300a, 0x300b, 0x300d, 0x300f, 0x3011, 0x3015, 0x3017, 0x301d,
    0x3041, 0x3043, 0x3045, 0x3047, 0x3049, 0x3063, 0x3083, 0x3085,
    0x3087, 0x308e, 0x309b, 0x309c, 0x309d, 0x309e, 0x30a1, 0x30a3,
    0x30a5, 0x30a7, 0x30a9, 0x30c3, 0x30e3, 0x30e5, 0x30e7, 0x30ee,
    0x30f5, 0x30f6, 0x30fb, 0x30fc, 0x30fd, 0x30fe, 0x311d, 0x3121,
    0x3124, 0xfe31, 0xfe33, 0xfe34, 0xfe35, 0xfe36, 0xfe37, 0xfe38,
    0xfe3d, 0xfe3e, 0xfe3f, 0xfe40, 0xfe4b, 0xfe50, 0xfe51, 0xfe52,
    0xfe54, 0xfe56, 0xfe59, 0xfe5a, 0xfe5b, 0xfe5c, 0xfe5f, 0xfe60,
    0xfe62, 0xfe64, 0xfe65, 0xfe69, 0xfe6a, 0xfe6b, 0xff01, 0xff02,
    0xff05, 0xff07, 0xff09, 0xff0c, 0xff0e, 0xff1a, 0xff1b, 0xff1f,
    0xff3d, 0xff40, 0xff5c, 0xff5d, 0xff61, 0xff63, 0xff64, 0xff65,
    0xff67, 0xff68, 0xff69, 0xff6a, 0xff6b, 0xff6c, 0xff6d, 0xff6e,
    0xff6f, 0xff70, 0xff9e, 0xff9f, 0xffe0,
};

const uint16_t kNoLineEnd[] = {
    0x0024, 0x0028, 0x005b, 0x005c, 0x007b, 0x00a3, 0x00a5, 0x2018,
    0x201c, 0x2035, 0x3008, 0x300a, 0x300c, 0x300e, 0x3010, 0x3014,
    0x3016, 0x301d, 0xfe5b, 0xfe5d, 0xff04, 0xff08, 0xff3a, 0xff3b,
    0xff5b, 0xff62, 0xffe1, 0xffe5,
};

template <size_t N> bool InTable(const uint16_t (&table)[N], uint32_t ch)
{
    return std::binary_search(std::begin(table), std::end(table), static_cast<uint16_t>(ch));
}

} // namespace

bool CanStartLine(uint32_t ch)
{
    return !InTable(kNoLineStart, ch);
}

bool CanEndLine(uint32_t ch)
{
    return !InTable(kNoLineEnd, ch);
}

bool IsLineBreakChar(uint32_t ch)
{
    ch &= 0xffff; // the set holds 16-bit keys
    return ch == 10 || ch == 13 || ch == 0x85 || ch == 0x2028 || ch == 0x2029;
}

bool IsBreakSpace(uint32_t ch)
{
    static const uint16_t kSpaces[] = {0x20,   0x2d,   0x82,   0x2002, 0x2003, 0x2004, 0x2005, 0x2006, 0x2007, 0x2008,
                                       0x2009, 0x200a, 0x200b, 0x2010, 0x2012, 0x2013, 0x2014, 0x2015, 0x203e, 0x3000,
                                       0x301c, 0xfe31, 0xfe32, 0xfe58};
    if (ch <= 0xffff && std::binary_search(std::begin(kSpaces), std::end(kSpaces), static_cast<uint16_t>(ch))) return true;
    const uint16_t unit = static_cast<uint16_t>(ch);
    return unit <= 0x20 && unit != 10 && unit != 13;
}

bool IsThaiMark(uint32_t ch)
{
    if (ch < 0xe31 || ch > 0xe4e) return false;
    return ch == 0xe31 || (ch >= 0xe34 && ch <= 0xe3a) || ch >= 0xe47;
}

uint32_t ThaiClusterLength(const uint16_t *text, uint32_t count)
{
    if (count < 1) return 0;
    if (count < 2) return 1;
    if (text[1] == 0xe33) return 2;
    if (!IsThaiMark(text[1])) return 1;
    if (count < 3) return 2;
    if (text[2] == 0xe33) return 3;
    if (!IsThaiMark(text[2])) return 2;
    return 3 + (text[3] == 0xe33 ? 1 : 0); // ORIGINAL-BUG: reads text[3] even when count is 3
}

uint32_t ClusterLength(const uint16_t *text, uint32_t count)
{
    if (count == 0) return 0;
    const uint16_t *const end = text + count;
    uint32_t previous = *text;
    uint32_t units = 1;
    // What the previous character was: one a line may end after, a break, a space, an ideograph (not Hangul).
    bool previousCanEnd = CanEndLine(previous);
    bool previousBreak = IsLineBreakChar(previous);
    bool previousSpace = IsBreakSpace(previous);
    bool previousIdeograph = IsIdeograph(previous) && !IsHangul(previous);
    for (const uint16_t *at = text + 1; at < end;) {
        if (static_cast<uint16_t>(previous) == 0xe46) break;
        const uint32_t ch = *at++;
        const bool canStart = CanStartLine(ch);
        const bool canEnd = CanEndLine(ch);
        const bool lineBreak = IsLineBreakChar(ch);
        const bool space = IsBreakSpace(ch);
        const bool ideograph = IsIdeograph(ch) && !IsHangul(ch);
        if (previousBreak || lineBreak) break;
        if (canStart && previousCanEnd) {
            if (previousIdeograph || ideograph) break;
            if (previousSpace && !space) break;
        }
        ++units;
        previous = ch;
        previousCanEnd = canEnd;
        previousBreak = false;
        previousSpace = space;
        previousIdeograph = ideograph;
    }
    return units;
}

uint32_t CharUnits(const void *text, uint32_t count, int32_t encoding)
{
    if (count == 0) return 0;
    if (encoding == kUtf8) {
        if (*static_cast<const uint8_t *>(text) <= 0x7f) return 1;
        uint16_t converted[4] = {0, 0, 0, 0};
        uint32_t room = 3;
        if (Convert(text, count, kUtf8, converted, &room, kUtf16) != 0 && IsThaiBlock(converted[0]))
            return CharsLength(text, ThaiClusterLength(converted, room), kUtf8);
    } else if (encoding == kUtf16) {
        const uint16_t *const units = static_cast<const uint16_t *>(text);
        return IsThaiBlock(*units) ? ThaiClusterLength(units, count) : 1;
    }
    return CharLength(text, count, encoding);
}

} // namespace RZText
