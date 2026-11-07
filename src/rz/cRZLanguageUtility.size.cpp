#include "rz/cRZLanguageUtility.h"

#include "app/AppServices2.h"
#include "game/game.h"
#include "game/import.h"
#include "game/vtable.h"
#include "gz/cGZAny.h"
#include "gz/cIGZPropertySet.h"
#include "ifaces/cIGZFrameWork.h"
#include "ifaces/cIGZPropertySetString.h"
#include "rz/RZTextConvert.h"
#include "rz/Singleton_20ddfb.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>

namespace {

// Game texts (RVAs of C strings in the image).
constexpr uint32_t kNoBreakSpaceText = 0xe9f7f4; // U+00A0 in UTF-8 (2 bytes)
constexpr uint32_t kUntranslatedText = 0xe9f4a4;
constexpr uint32_t kCommaText = 0xdaa6c0;
constexpr uint32_t kPeriodText = 0xd9f880;
constexpr uint32_t kMinusText = 0xd9f8f8;
constexpr uint32_t kSpaceText = 0xda4ca4;
constexpr uint32_t kMiddleDotText = 0xe9f7f8;     // U+00B7, between Simplified Chinese names
constexpr uint32_t kInt64Format = 0xda298c;       // "%lld"
constexpr uint32_t kPercentText = 0xe06864;       // "%"
constexpr uint32_t kDoublePercentText = 0xea01ec; // "%%"
constexpr uint32_t kConversionChars = 0xea01cc;   // every printf conversion and size letter
constexpr uint32_t kFloatConversions = 0xea01e4;  // "fegEG"
constexpr uint32_t kDigitsAndSpace = 0xea01f0;    // "0123456789 "
constexpr uint32_t kOneText = 0xda4fac;           // "1"
constexpr uint32_t kAmText = 0xea020c;
constexpr uint32_t kPmText = 0xea0210;
constexpr uint32_t kForce12HourSetting = 0xea01fc; // "force12HrTime"

// The settings service: the framework's slot 5 with these ids gives the string-keyed property set.
constexpr uint32_t kSettingsId = 0x0ac1887e;
constexpr uint32_t kSettingsIid = 0x4bb0d269;

// Import address table slots.
constexpr uint32_t kIatCharLowerBuffA = 0xd9a508;
constexpr uint32_t kIatCharUpperBuffA = 0xd9a504;
constexpr uint32_t kIatCompareStringA = 0xd9a3c8;
using CharBuffFn = uint32_t(__stdcall *)(char *text, uint32_t length);
using CompareStringFn = int32_t(__stdcall *)(uint32_t locale, uint32_t flags, const char *a, int32_t aLength, const char *b,
                                             int32_t bLength);
constexpr uint32_t kLocaleUserDefault = 0x400;

const char *Text(uint32_t rva) { return GameAddress<const char>(rva); }

// A cRZString in the frame (the original's locals carry the game's string vtable, as these do).
struct LocalText {
    union {
        cRZString s;
    };
    LocalText() { s.Construct(); }
    explicit LocalText(const char *text) { s.Construct(text); }
    ~LocalText() { s.Destruct(); }
    LocalText(const LocalText &) = delete;
    LocalText &operator=(const LocalText &) = delete;
    cIGZString *I() { return s.AsInterface(); }
};

// ---- the name tables (slots 5 to 8) --------------------------------------------------------------------------------
// Rows of text RVAs (entry i is index i + 1; 0: no text) and, per language id, the row it uses (-1: none). Read from the
// original's jump tables.

constexpr uint32_t kDayNames[][7] = {
    {0xe095d0, 0xe095d8, 0xe095e0, 0xe095ec, 0xe095f8, 0xe09600, 0xe095c8},
    {0xe9f7fc, 0xe9f804, 0xe9f80c, 0xe9f818, 0xe9f820, 0xe9f82c, 0xe9f834},
    {0xe9f840, 0xe9f848, 0xe9f854, 0xe9f860, 0xe9f86c, 0xe9f874, 0xe9f87c},
    {0xe9f954, 0xe9f95c, 0xe9f968, 0xe9f974, 0xe9f980, 0xe9f98c, 0xe9f994},
    {0xe9f884, 0xe9f88c, 0xe9f894, 0xe9f8a0, 0xe9f8a8, 0xe9f8b0, 0xe9f8b8},
    {0xe9f910, 0xe9f918, 0xe9f920, 0xe9f92c, 0xe9f938, 0xe9f940, 0xe9f94c},
    {0xe9f9ec, 0xe9f9f4, 0xe9f9fc, 0xe9fa04, 0xe9fa0c, 0xe9fa14, 0xe9fa1c},
    {0xe9f9a0, 0xe9f9ac, 0xe9f9b4, 0xe9f9c0, 0xe9f9c8, 0xe9f9d4, 0xe9f9e0},
    {0xe9f8c0, 0xe9f8d0, 0xe9f8dc, 0xe9f8ec, 0xe9f8fc, 0xe9f908, 0xe9f8b8},
    {0xe9fa24, 0xe9fa30, 0xe9fa3c, 0xe9fa48, 0xe9fa54, 0xe9fa60, 0xe9fa6c},
    {0xe9fa78, 0xe9fa84, 0xe9fa90, 0xe9fa9c, 0xe9faa8, 0xe9fab4, 0xe9fac0},
    {0xe9facc, 0xe9fad8, 0xe9fae4, 0xe9faf0, 0xe9fafc, 0xe9fb08, 0xe9fb14},
};
constexpr int8_t kDayNamesRow[0x24] = {-1, 0,  0,  1,  2,  3,  4,  5,  6,  0,  6,  7,  0,  0,  8,  9,  0,  10,
                                       10, 0,  11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 8};

constexpr uint32_t kShortDayNames[][7] = {
    {0xe095b0, 0xe095b4, 0xe095b8, 0xe9fb20, 0xe095c0, 0xe095c4, 0xe095ac},
    {0xe9fb28, 0xe9fb2c, 0xe9fb30, 0xe9fb34, 0xe9fb38, 0xe9fb3c, 0xe9fb40},
    {0xe9fb44, 0xe9fb48, 0xe9fb4c, 0xe9fb50, 0xe9fb54, 0xe9fb58, 0xe9fb5c},
    {0xe9fbb8, 0xda30f0, 0xe9fbbc, 0xe9fbc0, 0xe9fbc4, 0xe9fb74, 0xe9fb78},
    {0xd9d538, 0xdf3b38, 0xd9d54c, 0xe51e74, 0xe51e90, 0xd9d548, 0xd9d544},
    {0xe9fba4, 0xe9fb48, 0xe9fba8, 0xe9fb50, 0xe9fbac, 0xe9fbb0, 0xe9fbb4},
    {0xe9fc48, 0xe9fc4c, 0xe9fb88, 0xe9fb8c, 0xe9fb90, 0xe9fc50, 0xe9fc58},
    {0xe9fb7c, 0xe9fb84, 0xe9fb88, 0xe9fb8c, 0xe9fb90, 0xe9fb94, 0xe9fb9c},
    {0xe9fbf8, 0xe9fbfc, 0xe9fc00, 0xe9fc04, 0xe9fc08, 0xe9f538, 0xe9fc0c},
    {0xe9fbc8, 0xe9fbcc, 0xe9fbd0, 0xe9fbd4, 0xe9fbd8, 0xe9fbdc, 0xe9fbe0},
    {0xe9fb60, 0xe9fb64, 0xe9fb68, 0xe9fb6c, 0xe9fb70, 0xe9fb74, 0xe9fb78},
    {0xe9fa24, 0xe9fa30, 0xe9fa3c, 0xe9fa48, 0xe9fa54, 0xe9fa60, 0xe9fa6c},
    {0xe9fbe4, 0xe9fbe8, 0xe9fbec, 0xe9fbf0, 0xe9fbf4, 0xe9fb5c, 0xdb0888},
    {0xe9fa78, 0xe9fa84, 0xe9fa90, 0xe9fa9c, 0xe9faa8, 0xe9fab4, 0xe9fac0},
    {0xe9fc10, 0xe9fc18, 0xe9fc1c, 0xe9fc20, 0xe9fc24, 0xe9fc2c, 0xe9fc30},
    {0xe9facc, 0xe9fad8, 0xe9fae4, 0xe9faf0, 0xe9fafc, 0xe9fb08, 0xe9fb14},
    {0xe9fbe4, 0xe9fc34, 0xe9fc38, 0xe9fc3c, 0xe9fc40, 0xe9fb5c, 0xe9fc44},
};
constexpr int8_t kShortDayNamesRow[0x24] = {-1, 0,  0,  1,  2,  3,  4,  5,  6,  7,  -1, 8,  -1, 9,  10, 11, 12, 13,
                                            13, 14, 15, -1, -1, -1, -1, -1, 16, -1, -1, -1, -1, -1, -1, -1, -1, 10};

constexpr uint32_t kMonthNames[][12] = {
    {0xe0960c, 0xe09614, 0xe09620, 0xe09628, 0xda30f8, 0xe09630, 0xe09638, 0xe09640, 0xe09648, 0xe09654, 0xe0965c, 0xe09668},
    {0xe9fc60, 0xe9fc68, 0xe9fc70, 0xe9fc78, 0xe9fc80, 0xe9fc84, 0xe9fc8c, 0xe9fc94, 0xe9fc9c, 0xe9fca8, 0xe9fcb0, 0xe9fcbc},
    {0xe9fcc8, 0xe9fcd0, 0xe9fcd8, 0xe09628, 0xe9fce0, 0xe9fce4, 0xe9fcec, 0xe09640, 0xe09648, 0xe9fcf4, 0xe0965c, 0xe9fcfc},
    {0xe9fe14, 0xe9fe1c, 0xe9fd18, 0xe9fe28, 0xe9fe30, 0xe9fe38, 0xe9fe40, 0xe9fd40, 0xe9fe48, 0xe9fe54, 0xe9fe5c, 0xe9fe68},
    {0xe9fd08, 0xe9fd10, 0xe9fd18, 0xe9fd20, 0xe9fd28, 0xe9fd30, 0xe9fd38, 0xe9fd40, 0xe9fd48, 0xe9fd54, 0xe9fd5c, 0xe9fd68},
    {0xe9fdd4, 0xe9fddc, 0xe9fdfc, 0xe09628, 0xe9fe04, 0xe9fce4, 0xe9fcec, 0xe9fe08, 0xe09648, 0xe9fcf4, 0xe0965c, 0xe09668},
    {0xe9fef0, 0xe9fef8, 0xe9ff00, 0xe9ff08, 0xe9ff10, 0xe9ff14, 0xe9ff1c, 0xe9ff24, 0xe9ff2c, 0xe9ff38, 0xe9ff40, 0xe9ff4c},
    {0xe9fdd4, 0xe9fddc, 0xe9fde8, 0xe09628, 0xe9fdf0, 0xe9fce4, 0xe9fcec, 0xe9fdf4, 0xe09648, 0xe9fcf4, 0xe0965c, 0xe09668},
    {0xe9fe74, 0xe9fe7c, 0xe9fe88, 0xe9fe94, 0xe9fea0, 0xe9feac, 0xe9feb4, 0xe9fec0, 0xe9fec8, 0xe9fed0, 0xe9fed8, 0xe9fee4},
    {0xe9fd74, 0xe9fd7c, 0xe9fd88, 0xe9fd20, 0xe9fd90, 0xe9fd98, 0xe9fda0, 0xe9fd40, 0xe9fda8, 0xe9fdb4, 0xe9fdbc, 0xe9fdc8},
    // Japanese: no text for February (the original's table has no case for it).
    {0xe9ffd0, 0x0, 0xe9ffd8, 0xe9ffe0, 0xe9ffe8, 0xe9fff0, 0xe9fff8, 0xea0000, 0xea0008, 0xea0010, 0xea0018, 0xea0020},
    {0xe9ff58, 0xe9ff60, 0xe9ff68, 0xe9ff70, 0xe9fdf0, 0xe9ff7c, 0xe9ff88, 0xe9ff90, 0xe9ff9c, 0xe9ffa8, 0xe9ffb8, 0xe9ffc4},
    {0xea0028, 0xea0030, 0xea0038, 0xea0040, 0xea0048, 0xea0050, 0xea0058, 0xea0060, 0xea0068, 0xea0070, 0xea0078, 0xea0080},
    {0xea0088, 0xea0090, 0xea0098, 0xea00a0, 0xea00a8, 0xea00b0, 0xea00b8, 0xea00c0, 0xea00c8, 0xea00d0, 0xea00d8, 0xea00e0},
};
constexpr int8_t kMonthNamesRow[0x24] = {-1, 0,  0,  1,  2,  3,  4,  5,  6,  7,  -1, 8,  0,  0,  9,  10, 11, 12,
                                         12, 0,  13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 9};

constexpr uint32_t kShortMonthNames[][12] = {
    {0xda30e8, 0xda30ec, 0xda30f0, 0xda30f4, 0xda30f8, 0xda30fc, 0xda3100, 0xda3104, 0xda3108, 0xda310c, 0xda3110, 0xda3114},
    {0xea00ec, 0xea00f0, 0xe9fb2c, 0xea00f4, 0xe9fc80, 0xe9fc84, 0xea00f8, 0xe9fc94, 0xea0100, 0xea0108, 0xea010c, 0xea0110},
    {0xda30e8, 0xda30ec, 0xea0114, 0xda30f4, 0xe9fce0, 0xda30fc, 0xda3100, 0xda3104, 0xda3108, 0xea0118, 0xda3110, 0xea011c},
    {0xea0144, 0xda30ec, 0xda30f0, 0xda30f4, 0xea0148, 0xea014c, 0xea0150, 0xea0134, 0xea0138, 0xea0154, 0xda3110, 0xea012c},
    {0xea0120, 0xda30ec, 0xda30f0, 0xea0124, 0xe9fd28, 0xda30fc, 0xda3100, 0xea0128, 0xda3108, 0xda310c, 0xda3110, 0xea012c},
    {0xda30e8, 0xda30ec, 0xea0140, 0xda30f4, 0xe9fe04, 0xda30fc, 0xda3100, 0xda3104, 0xda3108, 0xea0118, 0xda3110, 0xda3114},
    {0xda30e8, 0xda30ec, 0xda30f0, 0xda30f4, 0xe9fdf0, 0xda30fc, 0xda3100, 0xda3104, 0xda3108, 0xea0118, 0xda3110, 0xda3114},
    {0xda30e8, 0xea0130, 0xda30f0, 0xea0124, 0xe9fce0, 0xda30fc, 0xda3100, 0xea0134, 0xea0138, 0xea013c, 0xda3110, 0xea011c},
    {0xe9ffd0, 0xea0180, 0xe9ffd8, 0xe9ffe0, 0xe9ffe8, 0xe9fff0, 0xe9fff8, 0xea0000, 0xea0008, 0xea0010, 0xea0018, 0xea0020},
    {0xea0158, 0xea015c, 0xda30f0, 0xea0160, 0xe9fdf0, 0xea0164, 0xea0168, 0xea016c, 0xea0170, 0xea0174, 0xea0178, 0xea017c},
    {0xea0028, 0xea0030, 0xea0038, 0xea0040, 0xea0048, 0xea0050, 0xea0058, 0xea0060, 0xea0068, 0xea0070, 0xea0078, 0xea0080},
    {0xea0088, 0xea0090, 0xea0098, 0xea00a0, 0xea00a8, 0xea00b0, 0xea00b8, 0xea00c0, 0xea00c8, 0xea00d0, 0xea00d8, 0xea00e0},
};
constexpr int8_t kShortMonthNamesRow[0x24] = {-1, 0,  0,  1,  2,  3,  4,  5,  0,  6,  -1, 0,  0,  0,  7,  8,  9,  10,
                                              10, 0,  11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 7};

// The currency symbols (slot 11) by language id - 1 (0: none).
constexpr uint32_t kCurrencySymbols[0x26] = {
    0xea0188, 0xea018c, 0xea01c4, 0xea01c4, 0xea01c4, 0xea01c4, 0xea01c4, 0xea01c4, // 1..8
    0xe9f59c, 0xea01c4, 0xea01c4, 0xea0194, 0xea0190, 0xea01c4, 0xea01a4, 0xea01a0, // 9..0x10
    0xea0188, 0xea0188, 0xea01a8, 0xda31d4, 0,        0,        0,        0,        // 0x11..0x18
    0,        0xea01bc, 0xea01c4, 0xea01ac, 0xe9f59c, 0xea01b0, 0,        0,        // 0x19..0x20
    0xea01b4, 0xea01b8, 0xea0198, 0,        0,        0xea01c4,                     // 0x21..0x26
};
constexpr uint32_t kDollarSymbol = 0xea0188;

// After a table text was written (or not): an empty result becomes "Untranslated" and the call fails; then the text is
// brought to the default encoding.
bool FinishName(cIGZString *out)
{
    bool found = true;
    if (out->Strlen() == 0) {
        out->FromChar(Text(kUntranslatedText));
        found = false;
    }
    cRZLanguageUtility::ToDefaultEncoding(out);
    return found;
}

template <size_t Width, size_t Rows, size_t Languages>
bool TableName(const uint32_t (&rows)[Rows][Width], const int8_t (&rowOf)[Languages], uint32_t language, uint8_t index,
               cIGZString *out)
{
    if (language < Languages && rowOf[language] >= 0 && index >= 1 && index <= Width) {
        const uint32_t rva = rows[rowOf[language]][index - 1];
        if (rva != 0) out->FromChar(Text(rva));
    }
    return FinishName(out);
}

// ---- times and dates (slots 9 and 10) ------------------------------------------------------------------------------

// The 24-hour formats of a language group: with seconds, with minutes, the hour alone.
struct TimeFormats {
    uint32_t seconds;
    uint32_t minutes;
    uint32_t hours;
};
constexpr TimeFormats k12HourTime = {0xea0214, 0xea0224, 0xea0230}; // "%u:%.2u:%.2u %s" ... (with am / pm)
constexpr TimeFormats kTimeP = {0xea025c, 0xde4850, 0xea026c};      // 2, 3, 4, 5, 0x10, 0x1d
constexpr TimeFormats kTimeQ = {0xea0238, 0xea0248, 0xea0254};      // 8, 0x24, 0x27, 0x28
constexpr TimeFormats kTimeR = {0xea0274, 0xea0284, 0xea028c};      // 0xb, 0x1c
constexpr TimeFormats kTimeS = {0xea0294, 0xea0284, 0xea028c};      // 0x1a (its own with seconds, R's otherwise)
constexpr TimeFormats kTimeOther = {0xea02a4, 0xea02b4, 0xea02bc};


const TimeFormats &TimeFormatsOf(uint32_t language)
{
    switch (language) {
    case 2:
    case 3:
    case 4:
    case 5:
    case 0x10:
    case 0x1d:
        return kTimeP;
    case 8:
    case 0x24:
    case 0x27:
    case 0x28:
        return kTimeQ;
    case 0xb:
    case 0x1c:
        return kTimeR;
    case 0x1a:
        return kTimeS;
    default:
        return kTimeOther;
    }
}

// A date format: the text and its arguments (the month and day numbers, the year text, the long or short month name).
enum DateArg : uint8_t { kMonth, kDay, kYear, kLong, kShort };
struct DateFormat {
    uint32_t format;
    DateArg args[3]; // the third unused by the two-argument formats
};
using DateStyles = DateFormat[6];

// By style: 0 numeric, 1 numeric month and year, 2 long name with day, 3 long name, 4 short name with day, 5 short name.
constexpr DateStyles kDatesOther = {
    {0xea02e4, {kMonth, kDay, kYear}}, {0xea02f0, {kMonth, kYear, kYear}}, {0xea02f8, {kLong, kDay, kYear}},
    {0xdd4128, {kLong, kYear, kYear}}, {0xea0304, {kShort, kDay, kYear}},  {0xea0310, {kShort, kYear, kYear}},
};
constexpr DateStyles kDatesC = { // 2, 3, 5, 6, 0xd, 0x13, 0x23
    {0xea02e4, {kDay, kMonth, kYear}}, {0xea02f0, {kMonth, kYear, kYear}}, {0xea0318, {kDay, kLong, kYear}},
    {0xdd4128, {kLong, kYear, kYear}}, {0xea0324, {kDay, kShort, kYear}},  {0xea0310, {kShort, kYear, kYear}},
};
constexpr DateStyles kDatesB = { // 4, 0xa, 0xb, 0x10: C's names
    {0xea0374, {kDay, kMonth, kYear}}, {0xea0380, {kMonth, kYear, kYear}}, {0xea0318, {kDay, kLong, kYear}},
    {0xdd4128, {kLong, kYear, kYear}}, {0xea0324, {kDay, kShort, kYear}},  {0xea0310, {kShort, kYear, kYear}},
};
constexpr DateStyles kDatesD = { // 7, 8, 0xe: C's names
    {0xea0330, {kDay, kMonth, kYear}}, {0xea033c, {kMonth, kYear, kYear}}, {0xea0318, {kDay, kLong, kYear}},
    {0xdd4128, {kLong, kYear, kYear}}, {0xea0324, {kDay, kShort, kYear}},  {0xea0310, {kShort, kYear, kYear}},
};
constexpr DateStyles kDatesE = { // 9
    {0xea0344, {kYear, kMonth, kDay}}, {0xea0354, {kYear, kMonth, kMonth}}, {0xea035c, {kDay, kLong, kYear}},
    {0xdd4128, {kLong, kYear, kYear}}, {0xea0368, {kDay, kShort, kYear}},   {0xea0310, {kShort, kYear, kYear}},
};
constexpr DateStyles kDatesF = { // 0xf
    {0xea0388, {kYear, kMonth, kDay}}, {0xea0394, {kYear, kMonth, kMonth}}, {0xea039c, {kYear, kLong, kDay}},
    {0xea03a8, {kYear, kLong, kLong}}, {0xea039c, {kYear, kShort, kDay}},   {0xea03a8, {kYear, kShort, kShort}},
};
constexpr DateStyles kDatesG = { // 0x11, 0x12, 0x14
    {0xea03b0, {kYear, kMonth, kDay}}, {0xea03bc, {kYear, kMonth, kMonth}}, {0xea03c4, {kYear, kLong, kDay}},
    {0xdd4128, {kYear, kLong, kLong}}, {0xea03c4, {kYear, kShort, kDay}},   {0xdd4128, {kYear, kShort, kShort}},
};

const DateStyles &DateStylesOf(uint32_t language)
{
    switch (language) {
    case 2:
    case 3:
    case 5:
    case 6:
    case 0xd:
    case 0x13:
    case 0x23:
        return kDatesC;
    case 4:
    case 0xa:
    case 0xb:
    case 0x10:
        return kDatesB;
    case 7:
    case 8:
    case 0xe:
        return kDatesD;
    case 9:
        return kDatesE;
    case 0xf:
        return kDatesF;
    case 0x11:
    case 0x12:
    case 0x14:
        return kDatesG;
    default:
        return kDatesOther;
    }
}

// The year as text: two to five digits, zero-padded.
const char *YearFormat(uint32_t year)
{
    if (year < 100) return Text(0xea02c4);
    if (year < 1000) return Text(0xea02cc);
    if (year < 10000) return Text(0xea02d4);
    return Text(0xea02dc);
}

// ---- numbers (slots 16 to 18) --------------------------------------------------------------------------------------

// The digits with `separator` between groups of three from the right (the first group 1 to 3 digits).
void GroupDigits(const std::string &digits, const std::string &separator, std::string &grouped)
{
    const int32_t size = static_cast<int32_t>(digits.size());
    int32_t rest = size;
    while (rest > 3) {
        int32_t take = rest % 3;
        if (take == 0) take = 3;
        grouped += digits.substr(static_cast<size_t>(size - rest), static_cast<size_t>(take));
        grouped += separator;
        rest -= take;
    }
    grouped += digits.substr(static_cast<size_t>(size - rest));
}

// The magnitude of a 64-bit value (the most negative stays as it is) and whether it was negative.
int64_t Magnitude(int64_t value, bool &negative)
{
    negative = value < 0;
    return negative ? static_cast<int64_t>(0 - static_cast<uint64_t>(value)) : value;
}

// Every "%%" from position 1 on becomes "%" (a doubled percent at the very start is kept).
void UndoublePercents(cRZString &text)
{
    for (int32_t at = text.Find(Text(kDoublePercentText), 1, true); at != -1;
         at = text.Find(Text(kDoublePercentText), static_cast<uint32_t>(at) + 1, true))
        text.Erase(static_cast<uint32_t>(at), 1);
}

} // namespace

// ---- construction --------------------------------------------------------------------------------------------------

cRZLanguageUtility::cRZLanguageUtility()
    : mRefCount(0), mLanguage(kDefaultLanguage), mCountry(kDefaultCountry), mLocalCurrency(false)
{
    mGroupSpace.Construct(Text(kNoBreakSpaceText));
}

cRZLanguageUtility::~cRZLanguageUtility()
{
    mGroupSpace.Destruct();
}

// ---- cIGZUnknown ---------------------------------------------------------------------------------------------------

bool cRZLanguageUtility::QueryInterface(uint32_t riid, void **ppv)
{
    // No null check on ppv for the matching ids (the original faults on null there as well).
    if (riid == cIGZLanguageUtility::kIid || riid == 1) {
        *ppv = static_cast<cIGZLanguageUtility *>(this);
        AddRef();
        return true;
    }
    return false;
}

uint32_t cRZLanguageUtility::AddRef()
{
    return ++mRefCount;
}

uint32_t cRZLanguageUtility::Release()
{
    const uint32_t count = mRefCount;
    if (count <= 1) {
        delete this;
        return 0;
    }
    mRefCount = count - 1;
    return count - 1;
}

// ---- slots 3 to 8 --------------------------------------------------------------------------------------------------

bool cRZLanguageUtility::Init()
{
    mGroupSpace.mString.assign(Text(kNoBreakSpaceText), 2);
    ToDefaultEncoding(mGroupSpace.AsInterface());
    return true;
}

bool cRZLanguageUtility::Shutdown()
{
    return true;
}

bool cRZLanguageUtility::GetDayName(uint8_t day, cIGZString *out)
{
    return TableName(kDayNames, kDayNamesRow, mLanguage, day, out);
}

bool cRZLanguageUtility::GetShortDayName(uint8_t day, cIGZString *out)
{
    return TableName(kShortDayNames, kShortDayNamesRow, mLanguage, day, out);
}

bool cRZLanguageUtility::GetMonthName(uint8_t month, cIGZString *out)
{
    return TableName(kMonthNames, kMonthNamesRow, mLanguage, month, out);
}

bool cRZLanguageUtility::GetShortMonthName(uint8_t month, cIGZString *out)
{
    return TableName(kShortMonthNames, kShortMonthNamesRow, mLanguage, month, out);
}

// ---- times and dates -----------------------------------------------------------------------------------------------

bool cRZLanguageUtility::FormatTime(int32_t hour, int32_t minute, int32_t second, cIGZString *out)
{
    LocalText text;
    // Values past the range wrap (as unsigned); negative ones are kept and drop the minutes or seconds below.
    if (hour >= 24) hour = static_cast<int32_t>(static_cast<uint32_t>(hour) % 24u);
    if (minute >= 60) minute = static_cast<int32_t>(static_cast<uint32_t>(minute) % 60u);
    if (second >= 60) second = static_cast<int32_t>(static_cast<uint32_t>(second) % 60u);

    // The "force12HrTime" setting. ORIGINAL-BUG: the settings service is used without a null test (no framework, or no
    // settings service: an access violation).
    ScopedRef settings{nullptr};
    if (void *framework = GetServiceObject())
        As<ifaces::cIGZFrameWork>(framework)->GetSystemService(kSettingsId, kSettingsIid, &settings.mObject);
    gz::AnyBoolean force12Hour(0);
    {
        GZServiceString name(Text(kForce12HourSetting));
        As<ifaces::cIGZPropertySetString>(settings.mObject)->GetPropertyValue(&name, static_cast<cIGZAny *>(&force12Hour));
    }

    if (mLanguage == 1 || mLanguage == 0x2a || force12Hour.mValue != 0) {
        const char *half = Text(hour >= 12 ? kPmText : kAmText);
        if (hour == 0)
            hour = 12;
        else if (hour > 12)
            hour -= 12;
        if (second >= 0)
            text.s.Sprintf(Text(k12HourTime.seconds), hour, minute, second, half);
        else if (minute >= 0)
            text.s.Sprintf(Text(k12HourTime.minutes), hour, minute, half);
        else
            text.s.Sprintf(Text(k12HourTime.hours), hour, half);
    } else {
        const TimeFormats &formats = TimeFormatsOf(mLanguage);
        if (second >= 0)
            text.s.Sprintf(Text(formats.seconds), hour, minute, second);
        else if (minute >= 0)
            text.s.Sprintf(Text(formats.minutes), hour, minute);
        else
            text.s.Sprintf(Text(formats.hours), hour);
    }
    out->FromChar(text.s.ToChar());
    settings.ReleaseObject();
    return true;
}

bool cRZLanguageUtility::FormatDate(uint32_t month, uint32_t day, uint32_t year, cIGZString *out, uint32_t style)
{
    LocalText result;
    LocalText longName;
    LocalText shortName;
    if (style != 0 && style != 1) {
        GetMonthName(static_cast<uint8_t>(month), longName.I());
        GetShortMonthName(static_cast<uint8_t>(month), shortName.I());
    }
    LocalText yearText;
    yearText.s.Sprintf(YearFormat(year), year);

    if (style > 5) return false;
    const DateFormat &format = DateStylesOf(mLanguage)[style];
    const uintptr_t values[] = {
        month,
        day,
        reinterpret_cast<uintptr_t>(yearText.s.ToChar()),
        reinterpret_cast<uintptr_t>(longName.s.mString.c_str()),
        reinterpret_cast<uintptr_t>(shortName.s.mString.c_str()),
    };
    // Every argument is one stack word; a two-argument format ignores the third.
    result.s.Sprintf(Text(format.format), values[format.args[0]], values[format.args[1]], values[format.args[2]]);
    out->FromChar(result.s.ToChar());
    return true;
}

// ---- currency and separators ---------------------------------------------------------------------------------------

bool cRZLanguageUtility::GetCurrencySymbol(cIGZString *out)
{
    if (!mLocalCurrency) {
        out->FromChar(Text(kDollarSymbol));
    } else {
        const uint32_t index = mLanguage - 1;
        if (index < sizeof(kCurrencySymbols) / sizeof(kCurrencySymbols[0]) && kCurrencySymbols[index] != 0)
            out->FromChar(Text(kCurrencySymbols[index]));
    }
    return FinishName(out);
}

bool cRZLanguageUtility::CurrencySymbolFirst()
{
    // True for ids 1 to 0x23 in this set (bit n: id n). Ids 0 and 0x24 up are false.
    constexpr uint64_t kTrue = (1ull << 1) | (1ull << 2) | (1ull << 5) | (1ull << 7) | (1ull << 8) | (1ull << 12) |
                               (1ull << 14) | (1ull << 15) | (1ull << 17) | (1ull << 18) | (1ull << 19) | (1ull << 20) |
                               (1ull << 34) | (1ull << 35);
    if (mLanguage >= 64) return false;
    return ((kTrue >> mLanguage) & 1) != 0;
}

bool cRZLanguageUtility::CurrencySymbolSpaced()
{
    // Ids from 0x15 up are true; below: 0, 3 to 0xb, 0xd, 0xe, 0x10 (bit n: id n).
    if (mLanguage >= 0x15) return true;
    constexpr uint64_t kTrue = (1ull << 0) | 0xff8ull | (1ull << 13) | (1ull << 14) | (1ull << 16);
    return ((kTrue >> mLanguage) & 1) != 0;
}

bool cRZLanguageUtility::GetThousandsSeparator(cIGZString *out)
{
    switch (mLanguage) {
    case 1:
    case 2:
    case 0xc:
    case 0xf:
    case 0x11:
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x1c:
        out->FromChar(Text(kCommaText));
        return true;
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 0xe:
    case 0x23:
        out->FromChar(Text(kPeriodText));
        return true;
    case 3:
    case 9:
    case 0xa:
    case 0xb:
    case 0xd:
    case 0x10:
    case 0x1a:
        out->FromChar(mGroupSpace.mString.c_str(), static_cast<uint32_t>(mGroupSpace.mString.size()));
        return true;
    default:
        out->FromChar(Text(kCommaText));
        return false;
    }
}

bool cRZLanguageUtility::GetDecimalSeparator(cIGZString *out)
{
    switch (mLanguage) {
    case 1:
    case 2:
    case 0xc:
    case 0xf:
    case 0x11:
    case 0x12:
    case 0x13:
    case 0x14:
        out->FromChar(Text(kPeriodText));
        return true;
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 0xa:
    case 0xb:
    case 0xd:
    case 0xe:
    case 0x10:
    case 0x1c:
    case 0x23:
        out->FromChar(Text(kCommaText));
        return true;
    default:
        out->FromChar(Text(kPeriodText));
        return false;
    }
}

bool cRZLanguageUtility::FormatMoney(int64_t value, cIGZString *out, const cIGZString *symbol)
{
    LocalText digits;
    LocalText separator(Text(kCommaText));
    LocalText grouped;
    LocalText amount;   // the symbol and the digits in the language's order
    LocalText trailing; // a symbol that goes after the digits
    LocalText minus(Text(kMinusText));
    LocalText result;

    bool negative;
    digits.s.Sprintf(Text(kInt64Format), Magnitude(value, negative));
    GetThousandsSeparator(separator.I());
    GroupDigits(digits.s.mString, separator.s.mString, grouped.s.mString);

    if (CurrencySymbolFirst()) {
        if (symbol)
            amount.s.Assign(*symbol);
        else
            GetCurrencySymbol(amount.I());
        if (CurrencySymbolSpaced()) amount.s.mString += mGroupSpace.mString;
        amount.s.mString += grouped.s.mString;
    } else {
        amount.s.mString = grouped.s.mString;
        if (CurrencySymbolSpaced()) amount.s.mString += mGroupSpace.mString;
        if (symbol)
            trailing.s.Assign(*symbol);
        else
            GetCurrencySymbol(trailing.I());
        amount.s.mString += trailing.s.mString;
    }

    if (!negative) {
        result.s.mString = amount.s.mString;
    } else if (mLanguage == 8 || mLanguage == 0xc) {
        // Danish and Hebrew: the symbol, the space, the minus, the digits.
        if (symbol)
            result.s.Assign(*symbol);
        else
            GetCurrencySymbol(result.I());
        result.s.mString += mGroupSpace.mString;
        result.s.mString += minus.s.mString;
        result.s.mString += grouped.s.mString;
    } else if (mLanguage == 0xa) {
        // Norwegian: the minus, the digits, the symbol (no space).
        result.s.mString = minus.s.mString;
        result.s.mString += grouped.s.mString;
        if (symbol)
            trailing.s.Assign(*symbol);
        else
            GetCurrencySymbol(trailing.I());
        result.s.mString += trailing.s.mString;
    } else {
        result.s.mString = minus.s.mString + amount.s.mString;
    }
    out->FromChar(result.s.ToChar());
    return true;
}

bool cRZLanguageUtility::FormatMoneyFloat(double value, cIGZString *out, const cIGZString *format, const cIGZString *symbol)
{
    out->Erase(0, 0xffffffffu);
    LocalText prefix;
    LocalText body;
    LocalText suffix;

    // The first '%' that is not a doubled one (a '%' among the last two characters is taken as it is).
    const uint32_t lastPair = format->Strlen() - 2;
    int32_t at = format->Find(Text(kPercentText), 0, true);
    if (at == -1) return false;
    while (static_cast<uint32_t>(at) < lastPair) {
        if (format->ToChar()[at + 1] != '%') break;
        at = format->Find(Text(kPercentText), static_cast<uint32_t>(at) + 2, true);
        if (at == -1) return false;
    }

    // prefix: the text before it; body: the conversion, which must be a floating-point one; suffix: the rest.
    if (at != 0) prefix.s.FromChar(format->ToChar(), static_cast<uint32_t>(at));
    body.s.FromChar(format->ToChar() + at);
    const size_t conversion = body.s.mString.find_first_of(Text(kConversionChars), 0);
    if (conversion == std::string::npos) return false;
    if (!std::strchr(Text(kFloatConversions), body.s.ToChar()[conversion])) return false;
    const uint32_t end = static_cast<uint32_t>(conversion) + 1;
    suffix.s.FromChar(body.s.ToChar() + end);
    body.s.Erase(end, 0xffffffffu);
    UndoublePercents(prefix.s);
    UndoublePercents(suffix.s);

    // The number, its integer digits grouped in threes with the language's separator.
    LocalText number;
    number.s.Sprintf(body.s.ToChar(), std::fabs(value));
    LocalText separator;
    GetThousandsSeparator(separator.I());
    if (separator.s.mString.size() != 0) {
        size_t point = number.s.mString.find_first_not_of(Text(kDigitsAndSpace), 0);
        if (point == std::string::npos) point = number.s.Strlen();
        for (int32_t i = static_cast<int32_t>(point) - 3; i > 0 && number.s.ToChar()[i - 1] != ' '; i -= 3)
            number.s.Insert(static_cast<uint32_t>(i), *separator.I());
    }

    // The money form of 1 or -1 with the symbol, its "1" replaced by the number.
    LocalText money;
    if (!FormatMoney(value < 0.0 ? -1 : 1, money.I(), symbol)) return false;
    out->Append(*prefix.I());
    const int32_t one = money.s.Find(Text(kOneText), 0, true);
    money.s.Replace(static_cast<uint32_t>(one), number.s.ToChar(), 1);
    out->Append(*money.I());
    out->Append(*suffix.I());
    return true;
}

bool cRZLanguageUtility::FormatInteger(int64_t value, cIGZString *out)
{
    LocalText digits;
    LocalText separator;
    separator.s.mString.assign(1, ',');
    LocalText grouped;
    LocalText minus;
    minus.s.mString.assign(1, '-');
    LocalText result;

    bool negative;
    digits.s.Sprintf(Text(kInt64Format), Magnitude(value, negative));
    GetThousandsSeparator(separator.I());
    GroupDigits(digits.s.mString, separator.s.mString, grouped.s.mString);
    if (negative)
        result.s.mString = minus.s.mString + grouped.s.mString;
    else
        result.s.mString = grouped.s.mString;
    out->FromChar(result.s.ToChar());
    return true;
}

bool cRZLanguageUtility::FormatFloat(double value, cIGZString *out, const cIGZString *format)
{
    out->Sprintf(format->ToChar(), value);
    return true;
}

// ---- case, comparison, code pages ----------------------------------------------------------------------------------

void cRZLanguageUtility::ToLower(const cIGZString *src, cIGZString *dst)
{
    LocalText text;
    RZText::ConvertStringToString(*src, *text.I(), RZText::kSystemCodePage);
    // ORIGINAL-BUG: the length is dst's, not the converted text's (a longer dst runs past the text).
    ImportFunction<CharBuffFn, kIatCharLowerBuffA>()(text.s.mString.data(), dst->Strlen());
    as2::ConvertStringFrom(text.I(), RZText::kSystemCodePage, dst);
}

void cRZLanguageUtility::ToUpper(const cIGZString *src, cIGZString *dst)
{
    LocalText text;
    RZText::ConvertStringToString(*src, *text.I(), RZText::kSystemCodePage);
    // ORIGINAL-BUG: the length is dst's, as ToLower's.
    ImportFunction<CharBuffFn, kIatCharUpperBuffA>()(text.s.mString.data(), dst->Strlen());
    as2::ConvertStringFrom(text.I(), RZText::kSystemCodePage, dst);
}

int32_t cRZLanguageUtility::Compare(const cIGZString *a, const cIGZString *b, bool caseSensitive)
{
    LocalText first;
    LocalText second;
    RZText::ConvertStringToString(*a, *first.I(), RZText::kSystemCodePage);
    RZText::ConvertStringToString(*b, *second.I(), RZText::kSystemCodePage);
    // NORM_IGNORECASE (1) unless case-sensitive; CSTR_LESS_THAN / EQUAL / GREATER_THAN (1, 2, 3) become -1, 0, 1.
    const int32_t order = ImportFunction<CompareStringFn, kIatCompareStringA>()(
        kLocaleUserDefault, static_cast<uint32_t>(caseSensitive) ^ 1u, first.s.mString.c_str(),
        static_cast<int32_t>(first.s.mString.size()), second.s.mString.c_str(), static_cast<int32_t>(second.s.mString.size()));
    return order == 0 ? 0 : order - 2;
}

uint32_t cRZLanguageUtility::GetCodePage()
{
    // The first code page the language supports, in this order (0x4e3 is asked twice); Western European otherwise.
    static constexpr uint32_t kOrder[] = {0x4e4, 0x4e2, 0x36a, 0x3a4, 0x3a8, 0x3b6, 0x3b5,
                                          0x4e3, 0x4e3, 0x4e5, 0x4e6, 0x4e7, 0x4e8, 0x4e9};
    for (uint32_t codePage : kOrder)
        if (SupportsCodePage(codePage)) return codePage;
    return 0x4e4;
}

bool cRZLanguageUtility::SupportsCodePage(uint32_t codePage)
{
    const uint32_t language = mLanguage;
    switch (codePage) {
    case 0x4e4: // Western European
        switch (language) {
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 0xa:
        case 0xb:
        case 0xe:
        case 0x1c:
        case 0x1d:
        case 0x1e:
        case 0x1f:
        case 0x23:
        case 0x24:
        case 0x25:
        case 0x26:
        case 0x27:
        case 0x28:
        case 0x29:
        case 0x2a:
            return true;
        default:
            return false;
        }
    case 0x4e2: // Central European
        return language == 0x10 || language == 0xd || language == 0x21 || language == 0x1a || language == 0x1c ||
               language == 0x1e || language == 0x20;
    case 0x3a4: // Japanese
        return language == 0xf;
    case 0x3b5: // Korean
        return language == 0x14;
    case 0x3a8: // Simplified Chinese
        return language == 0x11;
    case 0x3b6: // Traditional Chinese
        return language == 0x12;
    case 0x36a: // Thai
        return language == 0x13;
    case 0x4e3: // Cyrillic
        return language == 0xd || language == 0x18 || language == 0x17;
    case 0x4e5: // Greek
        return language == 0x1b;
    case 0x4e6: // Turkish
        return language == 0x22;
    case 0x4e7: // Hebrew
        return language == 0xc;
    case 0x4e8: // Arabic
        return language == 0x16;
    default:
        return false;
    }
}

// ---- language rules ------------------------------------------------------------------------------------------------

bool cRZLanguageUtility::vf25()
{
    return mLanguage != 2 && mLanguage != 0xf;
}

uint32_t cRZLanguageUtility::CountryIsNotDefault()
{
    return mCountry != kDefaultCountry ? 1u : 0u;
}

bool cRZLanguageUtility::FamilyNameFirst()
{
    return mLanguage == 0xf || mLanguage == 0x11 || mLanguage == 0x12 || mLanguage == 0x14;
}

void cRZLanguageUtility::FormatFullName(const cIGZString *given, const cIGZString *family, cIGZString *out)
{
    out->Erase(0, 0xffffffffu);
    if (family->Strlen() != 0 && given->Strlen() != 0) {
        LocalText separator(Text(kSpaceText));
        if (mLanguage == 0x11) {
            LocalText dot(Text(kMiddleDotText));
            separator.s.mString = dot.s.mString;
        } else if (mLanguage == 0x12) {
            LocalText none;
            separator.s.mString = none.s.mString;
        }
        if (FamilyNameFirst())
            out->Append(*family)->Append(*separator.I())->Append(*given);
        else
            out->Append(*given)->Append(*separator.I())->Append(*family);
    } else {
        *out = family->Strlen() != 0 ? *family : *given;
    }
}

bool cRZLanguageUtility::vf29()
{
    return mLanguage == 0xf || mLanguage == 0x11 || mLanguage == 0x12 || mLanguage == 0x14;
}

bool cRZLanguageUtility::vf30()
{
    switch (RZText::DefaultEncoding()) {
    case RZText::kAscii:
        return false;
    case RZText::kUtf8:
        return mLanguage != 1 && mLanguage != 2;
    case RZText::kUtf16:
        return false;
    default:
        return mLanguage == 0xf || mLanguage == 0x11 || mLanguage == 0x12 || mLanguage == 0x14 || mLanguage == 0x13;
    }
}

void cRZLanguageUtility::SetLanguage(uint32_t language)
{
    mLanguage = language;
}

void cRZLanguageUtility::SetCountry(uint32_t country)
{
    mCountry = country;
}

void __cdecl cRZLanguageUtility::ToDefaultEncoding(cIGZString *)
{
    // The original converts the text from UTF-8 when the default encoding is ASCII or a code page; it is UTF-8 here
    // (RZText::DefaultEncoding), so, as the original, nothing is done.
}

// ---- adapters ------------------------------------------------------------------------------------------------------

cRZLanguageUtility *__fastcall cRZLanguageUtilityAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cRZLanguageUtility;
}
