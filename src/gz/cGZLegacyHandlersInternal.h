#pragma once
// What the handler files of cGZLegacyControl share (src/gz/cGZLegacyHandlers.size.cpp and cGZLegacyHandlers2.size.cpp,
// ...): not part of the interface other code uses.
#include "gz/cGZLegacyHandlers.h"

#include "ifaces/cIGZGraphicSystem.h"
#include "rz/MiscHelpersB.h" // GetCachedServiceObjectB

// 0x9c5a05 (src/gz/cRZCursor.h, ours): the cached graphics system. Declared here: that header defines cGZRect a second
// time next to the script control set's.
ifaces::cIGZGraphicSystem *GetGraphicSystem();

// The native colour of an "(r,g,b)" attribute (the graphics system's active buffer converts it).
inline uint32_t NativeColor(uint8_t red, uint8_t green, uint8_t blue)
{
    return GetGraphicSystem()->ConvertRGBToNative(red, green, blue);
}

// The UI defaults service (cell 0x10f51b0).
inline cIGZUIDefaults *UIDefaults() { return static_cast<cIGZUIDefaults *>(GetCachedServiceObjectB()); }

// The default font's name (looked up by the UI defaults service when a window has no font attribute).
inline constexpr char kDefaultFontName[] = "main9";

// The number of `character` in the text.
inline uint32_t CountCharacters(const char *text, uint32_t length, char character)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < length; ++i) {
        if (text[i] == character) ++count;
    }
    return count;
}

// The font attribute (0xf000) of a face with a SetFont(font object) slot, as GZWinLineInput's: a uint32 gives the UI
// defaults service's font for it (and is kept as the window's font property); a text is kept as the property only; none
// sets the default font "main9" looked up by name.
template <class Face>
void ApplyDefaultingFont(ifaces::cIGZWin *window, const cGZScriptAttributes *attributes, Face *face)
{
    uint32_t value;
    LegacyText fontText;
    if (FindUint32Attribute(attributes, legacyattr::kFont, &value)) {
        face->SetFont(UIDefaults()->GetFont(value));
        SetUint32WindowProperty(window, value, 0);
    } else if (FindTextAttribute(attributes, legacyattr::kFont, &fontText.mText)) {
        SetTextWindowProperty(window, &fontText.mText, 0);
    } else {
        LegacyText name;
        name.mText.FromChar(kDefaultFontName);
        const uint32_t id = UIDefaults()->FindFontId(&name.mText);
        face->SetFont(UIDefaults()->GetFont(id));
    }
}

namespace legacyhandlers {
constexpr uint32_t kTextVariantType = 0x800e; // a text value's variant type
constexpr uint32_t kTextSeparator = 0x20;     // GetValString's separator for a value of several parts
constexpr uint32_t kListElementAttribute = 0x502; // listelement (repeated)
constexpr uint32_t kResourceTypeImage = 0x856ddbac;
constexpr uint32_t kFontProperty = 0xfaa3be85;      // + index: the window's font (a number or a name)
constexpr uint32_t kImagePairProperty = 0xdaa3b860; // + index: the image's group and instance
constexpr uint32_t kHighlightColorAttribute = 0x408;
constexpr uint32_t kOutlineColorAttribute = 0x40c;
constexpr uint32_t kColorAttribute = 0xf00f; // 0xf00f, 0xf010, 0xf011: consecutive
} // namespace legacyhandlers
