// The window classes' handlers of cGZLegacyControl, F6 part 4 (lane MI), continued: the text edit (and the flag table it
// reads). Built for size, as the game's. src/gz/cGZLegacyHandlers.h has the faces and the handlers' notes.
#include "gz/cGZLegacyHandlersInternal.h"

#include "gz/cGZPersistResourceManager.h" // cGZPersistResourceKey
#include "ifaces/cIGZWin.h"
#include "rz/cIGZVariant.h"
#include "rz/cRZString.h"
#include "rz/cRZVariant.h"

#include <cstdio>

#pragma warning(disable : 4996) // sscanf, as the game calls it

using namespace legacyhandlers;

uint32_t __cdecl ApplyFlagTable(const LegacyFlagTableEntry *table, const cGZScriptAttributes *attributes, uint32_t flags)
{
    for (; table->attribute != 0; ++table) {
        uint32_t value;
        if (FindUint32Attribute(attributes, table->attribute, &value)) {
            flags &= ~table->mask;
            if (value == 1) flags |= table->mask;
        }
    }
    return flags;
}

namespace {

// ---- GZWinTextEdit's attributes (RegisterTextEdit) ----
constexpr LegacyFlagTableEntry kTextEditFlags[] = {
    {0x400, 0x1},    {0x300, 0x2},    {0x401, 0x4},    {0x402, 0x8},    {0xf019, 0x10}, {0x301, 0x20},
    {0x403, 0x40},   {0x404, 0x80},   {0x405, 0x100},  {0x406, 0x200},  {0x416, 0x400}, {0x417, 0x800},
    {0x418, 0x8000}, {0x419, 0x1000}, {0x41a, 0x2000}, {0, 0},
};
constexpr uint32_t kTextEditColorIndexes[] = {0, 2, 4, 1, 3, 5}; // for 0xf00f..0xf014
constexpr uint32_t kOutlineFlagAttribute = 0xf019;
constexpr uint32_t kCaretColorAttribute = 0x407;
constexpr uint32_t kMaxTextAttribute = 0x409;
constexpr uint32_t kMaxGlyphsAttribute = 0x40a;
constexpr uint32_t kInsertModeAttribute = 0x40d;
constexpr uint32_t kInsertIndexAttribute = 0x40e;
constexpr uint32_t kInsertPosAttribute = 0x40f;
constexpr uint32_t kCaretPeriodAttribute = 0x410;
constexpr uint32_t kMaxUndoAttribute = 0x411;
constexpr uint32_t kVScrollImageAttribute = 0x412;
constexpr uint32_t kVScrollRectAttribute = 0x413;
constexpr uint32_t kHScrollImageAttribute = 0x414;
constexpr uint32_t kHScrollRectAttribute = 0x415;
constexpr uint32_t kPromptAttribute = 0x41b;
constexpr uint32_t kPromptResourceAttribute = 0x41c;
constexpr uint32_t kTextResourceType = 0x2026960b;
constexpr uint32_t kPromptKeyProperty = 0xb1a4e021;
constexpr uint32_t kHScrollKeyProperty = 0x8b4bfefc;
constexpr uint32_t kVScrollKeyProperty = 0x8b4bfefd;
constexpr uint32_t kAutofitProperty = 0xdacf8f9c;
constexpr uint32_t kAutofitAttribute = 0x304;

// A scroll image attribute: "{group,instance}" (both numbers read), the image from the resource (given to the face with the
// rectangle attribute, or an empty one, when found), then its key and pair kept as the window's properties.
template <class Set>
void ApplyScrollImage(ifaces::cIGZWin *window, const cGZScriptAttributes *attributes, uint32_t imageAttribute,
                      uint32_t rectAttribute, uint32_t keyProperty, int32_t pairIndex, Set set)
{
    LegacyText text;
    if (!FindTextAttribute(attributes, imageAttribute, &text.mText)) return;
    uint32_t group = 0, instance = 0;
    if (std::sscanf(text.Chars(), "{%x,%x}", &group, &instance) != 2) return;
    cIGZLegacyImage *const image = LoadImageResource(group, instance);
    if (image) {
        int32_t found[4];
        int32_t empty[4] = {0, 0, 0, 0};
        set(image, FindRectAttribute(attributes, rectAttribute, found) ? found : empty);
    }
    const cGZPersistResourceKey key{kResourceTypeImage, group, instance, 0};
    SetResourceKeyProperty(window, keyProperty, &key);
    SetImagePairProperty(window, group, instance, pairIndex);
    if (image) image->Release();
}

} // namespace

bool __cdecl ApplyTextEditProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply)
{
    cIGZWinTextEdit *face = nullptr;
    if (window->QueryInterface(cIGZWinTextEdit::kIID, reinterpret_cast<void **>(&face))) {
        if (apply) {
            face->SetFlags(ApplyFlagTable(kTextEditFlags, attributes, face->GetFlags()));

            uint32_t value;
            LegacyText fontText;
            if (FindUint32Attribute(attributes, legacyattr::kFont, &value)) {
                face->SetFont(value);
                SetUint32WindowProperty(window, value, 0);
            } else if (FindTextAttribute(attributes, legacyattr::kFont, &fontText.mText)) {
                SetTextWindowProperty(window, &fontText.mText, 0);
            }

            uint8_t red, green, blue;
            for (uint32_t i = 0; i < sizeof(kTextEditColorIndexes) / sizeof(kTextEditColorIndexes[0]); ++i) {
                if (FindColorAttribute(attributes, kColorAttribute + i, &red, &green, &blue))
                    face->SetColor(kTextEditColorIndexes[i], NativeColor(red, green, blue));
            }
            if (FindColorAttribute(attributes, kCaretColorAttribute, &red, &green, &blue))
                face->SetCaretColor(NativeColor(red, green, blue));
            if (FindColorAttribute(attributes, kHighlightColorAttribute, &red, &green, &blue))
                face->SetHighlightColor(NativeColor(red, green, blue));
            if (FindColorAttribute(attributes, kOutlineColorAttribute, &red, &green, &blue)) {
                // The outline flag is read unchecked: without a number the original uses a stack value.
                uint32_t outlined = 0;
                FindUint32Attribute(attributes, kOutlineFlagAttribute, &outlined);
                face->SetOutlineColor(outlined != 0, NativeColor(red, green, blue));
            }

            LegacyText prompt;
            if (FindTextAttribute(attributes, kPromptAttribute, &prompt.mText)) face->SetPrompt(&prompt.mText, 0);
            LegacyText promptResource;
            if (FindTextAttribute(attributes, kPromptResourceAttribute, &promptResource.mText)) {
                uint32_t group = 0, instance = 0;
                std::sscanf(promptResource.Chars(), "{%x,%x}", &group, &instance);
                LegacyText localized;
                if (LoadLocalizedText(group, instance, localized.mText.AsInterface())) face->SetPrompt(&localized.mText, 0);
                const cGZPersistResourceKey key{kTextResourceType, group, instance, 0};
                SetResourceKeyProperty(window, kPromptKeyProperty, &key);
            }

            if (FindUint32Attribute(attributes, kMaxTextAttribute, &value)) face->SetMaxText(value);
            if (FindUint32Attribute(attributes, kMaxGlyphsAttribute, &value)) face->SetMaxGlyphs(value);
            int32_t x, y;
            if (FindPairAttribute(attributes, legacyattr::kGutters, &x, &y)) {
                face->SetGutterX(x);
                face->SetGutterY(y);
            }
            if (FindUint32Attribute(attributes, kInsertModeAttribute, &value)) face->SetInsertMode(value == 0);
            if (FindUint32Attribute(attributes, kInsertIndexAttribute, &value)) face->SetInsertIndex(value);
            if (FindPairAttribute(attributes, kInsertPosAttribute, &x, &y)) face->SetInsertPos(x, y);
            if (FindUint32Attribute(attributes, kCaretPeriodAttribute, &value)) face->SetCaretPeriod(value);
            if (FindUint32Attribute(attributes, kMaxUndoAttribute, &value)) face->SetMaxUndo(value);

            ApplyScrollImage(window, attributes, kHScrollImageAttribute, kHScrollRectAttribute, kHScrollKeyProperty, 0,
                             [face](cIGZLegacyImage *image, const int32_t *rect) { face->SetHScrollImage(image, rect); });
            ApplyScrollImage(window, attributes, kVScrollImageAttribute, kVScrollRectAttribute, kVScrollKeyProperty, 1,
                             [face](cIGZLegacyImage *image, const int32_t *rect) { face->SetVScrollImage(image, rect); });
        } else {
            const bool autofit = HasAttribute(attributes, kAutofitAttribute);
            window->SetProperty(kAutofitProperty, new cRZVariant(autofit));
            if (autofit) face->Autofit();
        }
    }
    if (face) face->Release();
    return true;
}
