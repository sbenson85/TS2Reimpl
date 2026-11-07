// The Type 1 program as Font Fusion read it (src/font/FontFusion.h): the character codes it gives each glyph, the
// names it maps to codes, and the font-wide values it takes from the font's text. FreeType reads the outlines; this is
// the part of Font Fusion's behaviour FreeType has no equivalent for, kept as Font Fusion had it because the codes
// decide which glyph a character shows.
//
// Font Fusion decrypts the private part (eexec) and every charstring in place, then scans the text:
// - the glyphs are the "/name <length> RD " entries after "/CharStrings ", in file order (the glyph index);
// - with a custom encoding ("/Encoding <n> array"), a glyph's code is that of the first of the next n "dup <code>
//   ... /name" matches after "/Encoding " whose name is the glyph's. The scan isn't bound to the encoding: when the
//   encoding has fewer than n entries it runs on into the private dictionary (its "dup /Private", the Subrs
//   entries), so a glyph can take a code from there (HelveticaNeueLT Std Medium's .notdef gets 9);
// - with StandardEncoding or ISOLatin1Encoding, the code that encoding gives the name;
// - a glyph without a code gets the next free one from n (256 for the named encodings) on, in glyph order.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace fontfusion {

struct Type1Program {
    enum class Encoding { kCustom, kStandard, kIsoLatin1 };

    bool valid = false;          // false: no "/CharStrings " (Font Fusion's error 0x2729)
    Encoding encoding = Encoding::kStandard;
    std::vector<std::string> glyphNames;   // by glyph index
    std::vector<uint16_t> glyphCodes;      // by glyph index
    std::unordered_map<std::string, uint16_t> codeByName; // encoded names (later glyphs of the same name overwrite)
    std::vector<std::pair<std::string, uint16_t>> unencoded; // names given a code from n on, in glyph order
    uint16_t lastCharCode = 0;   // the highest code handed out (n - 1 at least)

    // Font-wide values (font units, or as noted); each search stops at "/CharStrings ".
    int32_t unitsPerEm = 1000;   // "/em "
    int32_t ascent = 0x7fff;     // "/ascent " (0x7fff: none; filled from the glyph for 'f' later)
    int32_t descent = 0x7fff;    // minus "/descent " (0x7fff: none; from 'g')
    int32_t underlinePosition = 0;
    int32_t underlineThickness = 0;
    int32_t italicAngle = 0;     // 16.16 degrees
    uint32_t isFixedPitch = 0;   // "/isFixedPitch " followed by 't'
    int32_t fontMatrix[4] = {0x10000, 0, 0, 0x10000}; // "/FontMatrix " times the em, 16.16 (xx, xy, yx, yy)
    bool hasFullName = false;
    std::string fullName;        // "/FullName (...)" (up to 80 characters)

    // The code StandardEncoding (iso false) or ISOLatin1Encoding (true) gives a glyph name, or -1.
    using StandardCode = std::function<int32_t(const std::string &name, bool iso)>;

    // Parses the pure Type 1 program (the output of ExtractPureT1FromPCType1).
    void Parse(const uint8_t *data, uint32_t length, const StandardCode &standardCode);

    // T2K_GetGlyphIndex: the first glyph whose code is `code`, else 0. `found` says whether any glyph has it (Font
    // Fusion's "found" flag, which T2K_GlyphExists falls back on: true for a code only glyph 0 has).
    uint32_t GlyphIndex(uint16_t code, bool *found) const;
    // T2K_GetCharCodeByName: the code of the glyph named `name` (0: none).
    uint16_t CodeByName(const char *name) const;
    // The glyph index of the code's glyph for the font-wide metrics ('f', 'g'), or -1.
    int32_t GlyphForMetrics(uint16_t code) const;
};

// Eexec decryption (Adobe Type 1 Font Format, chapter 7): one byte with key `r`, which it advances.
uint8_t Type1Decrypt(uint8_t cipher, uint16_t *r);

} // namespace fontfusion
