// cGZFont (lane OH): the Thai shaper and its two helpers (src/gz/cGZFont.h).
#include "gz/cGZFont.h"

#include <algorithm>

namespace {

// The character class of a Thai-block code point (0x1b1d25 builds a map of these on first use): 1 consonants, 2 leading
// vowels, 3 sara a / aa / am, 4 lakkhangyao, 5 the two obsolete consonants, 6 and 7 the vowels below (sara u, uu), 8 phinthu,
// 9 the tone marks, 10 thanthakhat and nikhahit, 11 maitaikhu, 12 yamakkan, 13 sara i, 14 mai han-akat and sara ue,
// 15 sara ii and uee, 16 digits, 17 the signs.
struct ThaiClassTable {
    int8_t cls[0x80];
    constexpr ThaiClassTable() : cls()
    {
        for (uint32_t c = 0xe01; c <= 0xe23; ++c) cls[c - 0xe00] = 1;
        cls[0xe25 - 0xe00] = 1;
        for (uint32_t c = 0xe27; c <= 0xe2e; ++c) cls[c - 0xe00] = 1;
        for (uint32_t c = 0xe40; c <= 0xe44; ++c) cls[c - 0xe00] = 2;
        cls[0xe30 - 0xe00] = 3;
        cls[0xe32 - 0xe00] = 3;
        cls[0xe33 - 0xe00] = 3;
        cls[0xe45 - 0xe00] = 4;
        cls[0xe24 - 0xe00] = 5;
        cls[0xe26 - 0xe00] = 5;
        cls[0xe38 - 0xe00] = 6;
        cls[0xe39 - 0xe00] = 7;
        cls[0xe3a - 0xe00] = 8;
        for (uint32_t c = 0xe48; c <= 0xe4b; ++c) cls[c - 0xe00] = 9;
        cls[0xe4c - 0xe00] = 10;
        cls[0xe4d - 0xe00] = 10;
        cls[0xe47 - 0xe00] = 11;
        cls[0xe4e - 0xe00] = 12;
        cls[0xe34 - 0xe00] = 13;
        cls[0xe31 - 0xe00] = 14;
        cls[0xe36 - 0xe00] = 14;
        cls[0xe35 - 0xe00] = 15;
        cls[0xe37 - 0xe00] = 15;
        for (uint32_t c = 0xe50; c <= 0xe59; ++c) cls[c - 0xe00] = 16;
        for (uint32_t c : {0xe2fu, 0xe3fu, 0xe46u, 0xe4fu, 0xe5au, 0xe5bu}) cls[c - 0xe00] = 17;
    }
};
constexpr ThaiClassTable kClasses;

// The four consonants with a tall stem (po pla, fo fa, fo fan, lo chula): a vowel or mark after one takes its raised form.
bool IsTallConsonant(uint32_t unit) { return unit == 0xe1b || unit == 0xe1d || unit == 0xe1f || unit == 0xe2c; }

} // namespace

int32_t cGZFont::ThaiClass(uint32_t ch)
{
    if (ch < 0xe00 || ch > 0xe7f) return 0;
    return kClasses.cls[(ch & 0xffff) - 0xe00];
}

uint32_t cGZFont::ThaiForm(uint32_t ch, uint32_t form)
{
    const uint32_t unit = ch & 0xffff;
    switch (unit) {
    case 0xe47: if (form == 1) return 0xf712; break;
    case 0xe0d: if (form == 1) return 0xf70f; break;
    case 0xe31: if (form == 1) return 0xf710; break;
    case 0xe34: if (form == 1) return 0xf701; break;
    case 0xe35: if (form == 1) return 0xf702; break;
    case 0xe36: if (form == 1) return 0xf703; break;
    case 0xe37: if (form == 1) return 0xf704; break;
    case 0xe4d: if (form == 1) return 0xf711; break;
    case 0xe48: case 0xe49: case 0xe4a: case 0xe4b: case 0xe4c: {
        // The tone marks: 1 above a letter with a stem, 2 shifted left, 3 low.
        const uint32_t tone = unit - 0xe48;
        if (form == 1) return 0xf713 + tone;
        if (form == 2) return 0xf70a + tone;
        if (form == 3) return 0xf705 + tone;
        break;
    }
    default: break;
    }
    return unit;
}

void cGZFont::ShapeThai(uint16_t *text, uint32_t count, const uint16_t **outText, uint32_t *outCount, uint32_t recordWords)
{
    *outText = text;
    *outCount = count;
    if (count == 0) return;
    const bool record = (recordWords & 0xff) != 0;
    uint16_t *units = text;
    uint32_t n = count;
    // Sara am (U+0E33) is taken apart into nikhahit (U+0E4D) and sara aa (U+0E32); after a tone mark the nikhahit goes in
    // front of it. The result is built in layout buffer 5 (mLayout[5]).
    if (std::find(units, units + n, uint16_t{0xe33}) != units + n) {
        std::vector<int16_t> &out = mLayout[5];
        if (out.capacity() < n * 2) out.reserve(n * 2);
        out.clear();
        for (uint32_t i = 0; i < n; ++i) {
            const int16_t unit = static_cast<int16_t>(units[i]);
            if (units[i] == 0xe33) {
                if (i > 0 && ThaiClass(units[i - 1]) == 9) {
                    out.insert(out.end() - 1, static_cast<int16_t>(0xe4d));
                    if (record) mLayoutWords.insert(mLayoutWords.end() - 1, i);
                } else {
                    out.push_back(static_cast<int16_t>(0xe4d));
                    if (record) mLayoutWords.push_back(i);
                }
                out.push_back(static_cast<int16_t>(0xe32));
            } else {
                out.push_back(unit);
            }
            if (record) mLayoutWords.push_back(i);
        }
        // ORIGINAL-BUG (a quirk): the source length goes onto the words even when none were asked for.
        mLayoutWords.push_back(n);
        units = reinterpret_cast<uint16_t *>(out.data());
        n = static_cast<uint32_t>(out.size());
        *outText = units;
        *outCount = n;
    }
    // Marks take the forms that sit right: by the class of the letter before, of the one itself and of the one after. The
    // text is rewritten in place.
    int32_t classNext = ThaiClass(units[0]);
    uint32_t flag = 0;           // what the previous unit asked of the next (1: a low base or a lowered mark, 3: also a tone)
    int32_t classBefore = 0;     // the class of the unit before the previous one... of the previous unit
    uint16_t before = 0;         // the previous unit
    uint16_t nextUnit = units[0];
    for (uint32_t i = 0; i < n; ++i) {
        const uint16_t unit = nextUnit;
        const int32_t classPrevious = classBefore;
        int32_t classHere = classNext;
        if (i + 1 < n) {
            nextUnit = units[i + 1];
            classNext = nextUnit != 0 ? ThaiClass(nextUnit) : 0;
        } else {
            nextUnit = 0;
            classNext = 0;
        }
        const uint32_t previousFlag = flag;
        flag = 0;
        if (unit == 0xe0d && static_cast<uint32_t>(classNext - 6) < 3) {
            // Yo ying before a lower vowel loses its tail.
            units[i] = static_cast<uint16_t>(ThaiForm(0xe0d, 1));
        } else if (classHere == 6 || classHere == 7) {
            if (IsTallConsonant(before)) flag = 1;
        } else if (classHere == 9 || classHere == 10 || classHere == 11 || classHere == 13 || classHere == 14 || classHere == 15) {
            uint32_t form = 0;
            if (previousFlag == 1 || IsTallConsonant(before)) {
                form = 1;
                flag = 1;
            }
            if ((classHere == 9 || unit == 0xe4c) && classPrevious < 10) {
                form += 2;
                flag = form;
            }
            if (form != 0) units[i] = static_cast<uint16_t>(ThaiForm(unit, form));
        }
        classBefore = classHere;
        before = unit;
    }
}
