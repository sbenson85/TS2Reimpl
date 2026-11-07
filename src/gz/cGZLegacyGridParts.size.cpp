// The grid's helpers of cGZLegacyControl (lane MM, F6 part 6): the cell text parser. Built for size, as the game's.
#include "gz/cGZLegacyHandlersInternal.h"

#include "misc/HexParse.h"
#include "rz/RZTextParse.h"
#include "rz/StringKeyParser.h"
#include "rz/cRZString.h"

namespace {

constexpr uint32_t kCellParts = 11; // the most parts a cell text is split into
constexpr uint32_t kMinimumParts = 4;
constexpr char kCellSeparator = ',';
constexpr uint32_t kCellText = 0x40000;
constexpr uint32_t kCellFont = 0x1000;
constexpr char kJoinFormat[] = "%s,%s";

} // namespace

bool __cdecl ParseGridCell(cRZString *text, uint32_t *column, uint32_t *row, uint32_t *flags, cRZString *resource,
                           uint32_t *fontId, uint32_t *fontWord0, uint32_t *fontWord1, uint32_t *fontWord2,
                           uint32_t *fontWord3, uint32_t *fontWord4)
{
    LegacyText parts[kCellParts];
    uint32_t count = kCellParts;
    rztext::SplitText(text->ToChar(), static_cast<uint32_t>(text->mString.size()), kCellSeparator, &parts[0].mText, &count);
    if (count < kMinimumParts) return false;
    *column = StringKeyParser::ParseNumber(parts[0].Chars());
    *row = StringKeyParser::ParseNumber(parts[1].Chars());
    *flags = hexparse::ParseHex32(parts[2].Chars());
    uint32_t next;
    if (*flags & kCellText) {
        resource->mString = parts[3].mText.mString;
        next = 4;
    } else {
        resource->Sprintf(kJoinFormat, parts[3].Chars(), parts[4].Chars());
        next = 5;
    }
    if (*flags & kCellFont) {
        *fontId = UIDefaults()->FindFontId(&parts[next].mText);
        *fontWord0 = StringKeyParser::ParseNumber(parts[next + 1].Chars());
        *fontWord1 = StringKeyParser::ParseNumber(parts[next + 2].Chars());
        *fontWord2 = StringKeyParser::ParseNumber(parts[next + 3].Chars());
        *fontWord3 = StringKeyParser::ParseNumber(parts[next + 4].Chars());
        *fontWord4 = StringKeyParser::ParseNumber(parts[next + 5].Chars());
    }
    return true;
}
