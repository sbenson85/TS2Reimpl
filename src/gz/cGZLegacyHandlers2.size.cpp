// The window classes' handlers of cGZLegacyControl, F6 part 4 (lane MI): src/gz/cGZLegacyHandlers.h. Built for size, as the
// game's. The earlier handlers and the shared readers are in cGZLegacyHandlers.size.cpp.
#include "gz/cGZLegacyHandlersInternal.h"

#include "gz/cGZPersistResourceManager.h" // cGZPersistResourceKey
#include "ifaces/cIGZWin.h"
#include "rz/cIGZVariant.h"
#include "rz/cRZString.h"
#include "rz/cRZVariant.h"

#include <cstdio>
#include <cstring>

#pragma warning(disable : 4996) // sscanf, as the game calls it

using namespace legacyhandlers;

namespace {

// ---- GZWinSpinner's attributes (RegisterSpinner 0x862173) ----
constexpr uint32_t kDigitsAttribute = 0x1000;
constexpr uint32_t kAutoNumberAttribute = 0x1001;
constexpr uint32_t kAutoNumberCommaAttribute = 0x1002;
constexpr uint32_t kAutoNumberCurrencyAttribute = 0x1003;
constexpr uint32_t kMinMaxAttribute = 0x1004;
constexpr uint32_t kStepSizeAttribute = 0x1005;
constexpr uint32_t kOutlineColorAll = 0x1006; // coloroutline: the default of the four sides
constexpr uint32_t kOutlineColorLeft = 0x1007;
constexpr uint32_t kOutlineColorTop = 0x1008;
constexpr uint32_t kOutlineColorRight = 0x1009;
constexpr uint32_t kOutlineColorBottom = 0x100a;
constexpr uint32_t kOutlineFlagAttribute = 0xf019;
constexpr uint32_t kFillFlagAttribute = 0xf00d;
constexpr uint32_t kInitValueAttribute = 0x40b;
constexpr uint32_t kSpinnerImageKeyProperty = 0x8bd1333a;
constexpr uint32_t kColorF010Index = 2; // the colour attributes 0xf00f, 0xf010, 0xf011 take the face's indexes 0, 2, 1
constexpr uint32_t kColorF011Index = 1;
constexpr uint32_t kSpinnerDigitsLeft = 1;  // the face's side words
constexpr uint32_t kSpinnerDigitsRight = 2;
constexpr uint32_t kSpinnerAlignLeft = 0;
constexpr uint32_t kSpinnerAlignCenter = 1;
constexpr uint32_t kSpinnerAlignRight = 2;

} // namespace

bool __cdecl ApplySpinnerProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply)
{
    cIGZWinSpinner *face = nullptr;
    if (window->QueryInterface(cIGZWinSpinner::kIID, reinterpret_cast<void **>(&face))) {
        if (apply) {
            uint32_t value;
            if (FindUint32Attribute(attributes, kDigitsAttribute, &value)) {
                if (value == 0)
                    face->SetDigits(0);
                else if (value == legacyattr::kStyleLeft)
                    face->SetDigits(kSpinnerDigitsLeft);
                else if (value == legacyattr::kStyleRight)
                    face->SetDigits(kSpinnerDigitsRight);
            }
            if (FindUint32Attribute(attributes, kOutlineFlagAttribute, &value))
                face->SetFlag(cIGZWinSpinner::kFlagOutline, value != 0);
            if (FindUint32Attribute(attributes, kFillFlagAttribute, &value))
                face->SetFlag(cIGZWinSpinner::kFlagFill, value != 0);
            if (FindUint32Attribute(attributes, kAutoNumberAttribute, &value))
                face->SetFlag(cIGZWinSpinner::kFlagAutoNumber, value != 0);
            if (FindUint32Attribute(attributes, kAutoNumberCommaAttribute, &value))
                face->SetFlag(cIGZWinSpinner::kFlagAutoNumberComma, value != 0);
            if (FindUint32Attribute(attributes, kAutoNumberCurrencyAttribute, &value))
                face->SetFlag(cIGZWinSpinner::kFlagAutoNumberCurrency, value != 0);

            ApplyDefaultingFont(window, attributes, face);

            uint8_t red, green, blue;
            if (FindColorAttribute(attributes, kColorAttribute, &red, &green, &blue))
                face->SetColor(0, NativeColor(red, green, blue));
            if (FindColorAttribute(attributes, kColorAttribute + 1, &red, &green, &blue))
                face->SetColor(kColorF010Index, NativeColor(red, green, blue));
            if (FindColorAttribute(attributes, kColorAttribute + 2, &red, &green, &blue))
                face->SetColor(kColorF011Index, NativeColor(red, green, blue));

            LegacyText imageText;
            if (FindTextAttribute(attributes, legacyattr::kImage, &imageText.mText)) {
                // The parse is not checked: a text without both numbers leaves stack values in the original.
                uint32_t group = 0, instance = 0;
                std::sscanf(imageText.Chars(), "{%x,%x}", &group, &instance);
                cIGZLegacyImage *const image = LoadImageResource(group, instance);
                if (image) face->SetImage(image, nullptr);
                const cGZPersistResourceKey key{kResourceTypeImage, group, instance, 0};
                SetResourceKeyProperty(window, kSpinnerImageKeyProperty, &key);
                SetImagePairProperty(window, group, instance, 0);
                if (image) image->Release();
            }

            if (FindUint32Attribute(attributes, legacyattr::kAlign, &value)) {
                if (value == legacyattr::kStyleLeft)
                    face->SetAlign(kSpinnerAlignLeft);
                else if (value == legacyattr::kStyleRight)
                    face->SetAlign(kSpinnerAlignRight);
                else if (value == legacyattr::kAlignCenterWord)
                    face->SetAlign(kSpinnerAlignCenter);
            }
            int32_t x, y;
            if (FindPairAttribute(attributes, legacyattr::kGutters, &x, &y)) face->SetGutters(x, y);

            // The outline colours: the all-sides colour is the default of each side.
            bool any = false;
            uint32_t all = 0;
            if (FindColorAttribute(attributes, kOutlineColorAll, &red, &green, &blue)) {
                all = NativeColor(red, green, blue);
                any = true;
            }
            uint32_t left = all;
            if (FindColorAttribute(attributes, kOutlineColorLeft, &red, &green, &blue)) {
                left = NativeColor(red, green, blue);
                any = true;
            }
            uint32_t top = all;
            if (FindColorAttribute(attributes, kOutlineColorTop, &red, &green, &blue)) {
                top = NativeColor(red, green, blue);
                any = true;
            }
            uint32_t right = all;
            if (FindColorAttribute(attributes, kOutlineColorRight, &red, &green, &blue)) {
                right = NativeColor(red, green, blue);
                any = true;
            }
            uint32_t bottom = all;
            if (FindColorAttribute(attributes, kOutlineColorBottom, &red, &green, &blue)) {
                bottom = NativeColor(red, green, blue);
                any = true;
            }
            if (any) face->SetOutlineColors(left, top, right, bottom);
        } else {
            int32_t first, second;
            if (FindPairAttribute(attributes, kMinMaxAttribute, &first, &second)) face->SetRange(first, second);
            int32_t value;
            if (FindSint32Attribute(attributes, kStepSizeAttribute, &value)) face->SetStepSize(value);
            if (FindSint32Attribute(attributes, kInitValueAttribute, &value)) face->SetValue(value);
        }
    }
    if (face) face->Release();
    return true;
}

namespace {

// ---- GZWinTextTicker's attributes (RegisterTextTicker 0x862fa9) ----
constexpr uint32_t kSpeedAttribute = 0x1200;
constexpr uint32_t kFrequencyAttribute = 0x1201;
constexpr uint32_t kMessageAttribute = 0x1202;
constexpr uint32_t kBackColorAttribute = 0xf001;
constexpr uint32_t kForeColorAttribute = 0xf002;
constexpr uint32_t kTickerNoForeColor = 0xffffffff; // the colour a message gets before any forecolor attribute

// Whether an attribute's value is a text variant (not null).
bool IsTextValue(const cGZScriptAttribute &attribute)
{
    cIGZVariant *const value = reinterpret_cast<cIGZVariant *>(attribute.mValue);
    return value && value->GetType() == kTextVariantType;
}

// The text of an attribute's value (a text variant) into `out`.
void ReadValueText(const cGZScriptAttribute &attribute, cIGZString *out)
{
    reinterpret_cast<cIGZVariant *>(attribute.mValue)->GetValString(*out, 0, kTextSeparator);
}

} // namespace

bool __cdecl ApplyTextTickerProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply)
{
    cIGZWinTextTicker *face = nullptr;
    if (window->QueryInterface(cIGZWinTextTicker::kIID, reinterpret_cast<void **>(&face)) && apply) {
        uint32_t value;
        if (FindUint32Attribute(attributes, kSpeedAttribute, &value)) face->SetSpeed(value);
        if (FindUint32Attribute(attributes, kFrequencyAttribute, &value)) face->SetFrequency(value);
        ApplyDefaultingFont(window, attributes, face);

        uint32_t backColor = 0;
        uint32_t foreColor = kTickerNoForeColor;
        const uint32_t count = attributes->Size();
        for (uint32_t i = 0; i < count; ++i) {
            const cGZScriptAttribute &attribute = attributes->mBegin[i];
            if (attribute.mId == kMessageAttribute) {
                if (!IsTextValue(attribute)) continue;
                // A heap string the face is handed and the handler lets go of (the original does not test the allocation).
                cRZString *const made = static_cast<cRZString *>(::operator new(sizeof(cRZString)));
                cIGZString *const text = made ? made->Construct()->AsInterface() : nullptr;
                text->AddRef();
                ReadValueText(attribute, text);
                face->SetMessage(text, foreColor, backColor, 0, 0);
                text->Release();
            } else if (attribute.mId == kBackColorAttribute || attribute.mId == kForeColorAttribute) {
                if (!IsTextValue(attribute)) continue;
                LegacyText colorText;
                ReadValueText(attribute, colorText.mText.AsInterface());
                // The parse is not checked: a text without three numbers leaves stack values in the original.
                int32_t red = 0, green = 0, blue = 0;
                std::sscanf(colorText.Chars(), "(%d,%d,%d)", &red, &green, &blue);
                const uint32_t color = NativeColor(static_cast<uint8_t>(red), static_cast<uint8_t>(green), static_cast<uint8_t>(blue));
                (attribute.mId == kBackColorAttribute ? backColor : foreColor) = color;
            }
        }
    }
    if (face) face->Release();
    return true;
}

namespace {

// ---- GZWinBtn's attributes (RegisterButton 0x85dba9) ----
constexpr uint32_t kToggleAttribute = 0xa00;
constexpr uint32_t kTriggerOnDownAttribute = 0xa01;
constexpr uint32_t kShowCaptionAttribute = 0xa02;
constexpr uint32_t kAutoSizeAttribute = 0xa03;
constexpr uint32_t kWrapCaptionAttribute = 0xa04;
constexpr uint32_t kShiftCaptionAttribute = 0xa05;
constexpr uint32_t kTipsAttribute = 0xa06;
constexpr uint32_t kTipsDelayAttribute = 0xa07;
constexpr uint32_t kTipsTimeoutAttribute = 0xa08;
constexpr uint32_t kLabelStyle = 0xa09; // the style words (values of a `style` attribute)
constexpr uint32_t kStandardStyle = 0xa0a;
constexpr uint32_t kStandardFixedStyle = 0xa0b;
constexpr uint32_t kToggleButtonStyle = 0xa0c;
constexpr uint32_t kToggleButtonFixedStyle = 0xa0d;
constexpr uint32_t kCheckBoxStyle = 0xa0e;
constexpr uint32_t kRadioButtonStyle = 0xa0f;
constexpr uint32_t kRadioCheckStyle = 0xa1b;
constexpr uint32_t kTipTextAttribute = 0xa10;
constexpr uint32_t kTipOffsetsAttribute = 0xa11;
constexpr uint32_t kTipResourceAttribute = 0xa12;
constexpr uint32_t kTipFlagAttribute = 0xa18;
constexpr uint32_t kClickSoundAttribute = 0xa19;
constexpr uint32_t kReleaseSoundAttribute = 0xa1a;
constexpr uint32_t kButtonColorAttribute = 0xf00f; // 0xf00f..0xf014, to these face indexes
constexpr uint32_t kButtonColorIndexes[] = {0, 4, 2, 1, 5, 3};
constexpr uint32_t kButtonLookNone = 0;
constexpr uint32_t kButtonLookLabel = 1;
constexpr uint32_t kButtonLookStandard = 2;
constexpr uint32_t kButtonLookToggle = 3;
constexpr uint32_t kButtonLookCheck = 4;
constexpr uint32_t kButtonAlignCenter = 0;
constexpr uint32_t kButtonAlignLeft = 1;
constexpr uint32_t kButtonAlignRight = 2;
constexpr uint32_t kDefaultTipFlags = 0x1000000;
constexpr uint32_t kButtonImageKeyProperty = 0x2b343bec;
constexpr uint32_t kTipResourceKeyProperty = 0x2b340ffe;
constexpr uint32_t kTextResourceType = 0x2026960b;
constexpr uint32_t kClickSoundProperty = 0x0a5be39d;
constexpr uint32_t kReleaseSoundProperty = 0x4a5be2f8;
constexpr uint32_t kUntypedBytesTag = 0x8000;
constexpr uint32_t kClickSound = 0;
constexpr uint32_t kReleaseSound = 1;

uint32_t ButtonLook(uint32_t style)
{
    switch (style) {
    case kLabelStyle: return kButtonLookLabel;
    case kStandardStyle:
    case kStandardFixedStyle: return kButtonLookStandard;
    case kToggleAttribute:
    case kToggleButtonStyle:
    case kToggleButtonFixedStyle: return kButtonLookToggle;
    case kCheckBoxStyle:
    case kRadioButtonStyle:
    case kRadioCheckStyle: return kButtonLookCheck;
    default: return kButtonLookNone;
    }
}

// A sound attribute: its "{group,instance}" text kept as the window's property (an 8-byte variant of the pair) and the
// instance given to the face (0 without the attribute). The parse is not checked, as the original's.
void ApplySound(ifaces::cIGZWin *window, const cGZScriptAttributes *attributes, cIGZWinButton *face, uint32_t attribute,
                uint32_t property, uint32_t which)
{
    LegacyText text;
    uint32_t instance = 0;
    if (FindTextAttribute(attributes, attribute, &text.mText)) {
        uint32_t pair[2] = {0, 0};
        std::sscanf(text.Chars(), "{%x,%x}", &pair[0], &pair[1]);
        window->SetProperty(property, new cRZVariant(pair, sizeof(pair), kUntypedBytesTag));
        instance = pair[1];
    }
    face->SetSound(which, instance);
}

} // namespace

bool __cdecl ApplyButtonProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply)
{
    cIGZWinButton *face = nullptr;
    if (window->QueryInterface(cIGZWinButton::kIID, reinterpret_cast<void **>(&face))) {
        uint32_t value;
        if (apply) {
            if (FindUint32Attribute(attributes, kToggleAttribute, &value))
                face->SetFlag(cIGZWinButton::kFlagToggle, value != 0);
            if (FindUint32Attribute(attributes, kTriggerOnDownAttribute, &value))
                face->SetFlag(cIGZWinButton::kFlagTriggerOnDown, value != 0);
            if (FindUint32Attribute(attributes, kShowCaptionAttribute, &value))
                face->SetFlag(cIGZWinButton::kFlagShowCaption, value != 0);
            if (FindUint32Attribute(attributes, kFillFlagAttribute, &value))
                face->SetFlag(cIGZWinButton::kFlagNoFill, value == 0);
            if (FindUint32Attribute(attributes, kAutoSizeAttribute, &value))
                face->SetFlag(cIGZWinButton::kFlagAutoSize, value != 0);
            if (FindUint32Attribute(attributes, kWrapCaptionAttribute, &value))
                face->SetFlag(cIGZWinButton::kFlagWrapCaption, value != 0);
            if (FindUint32Attribute(attributes, kShiftCaptionAttribute, &value))
                face->SetFlag(cIGZWinButton::kFlagShiftCaption, value != 0);
            if (FindUint32Attribute(attributes, kTipsAttribute, &value))
                face->SetFlag(cIGZWinButton::kFlagTips, value != 0);
            if (FindUint32Attribute(attributes, kTipsDelayAttribute, &value))
                face->SetFlag(cIGZWinButton::kFlagNoTipsDelay, value == 0);
            if (FindUint32Attribute(attributes, kTipsTimeoutAttribute, &value))
                face->SetFlag(cIGZWinButton::kFlagNoTipsTimeout, value == 0);

            uint32_t look = kButtonLookNone;
            if (FindUint32Attribute(attributes, legacyattr::kStyle, &value)) look = ButtonLook(value);

            if (FindUint32Attribute(attributes, legacyattr::kFont, &value)) {
                face->SetFontId(value);
                SetUint32WindowProperty(window, value, 0);
            } else {
                LegacyText fontText;
                if (FindTextAttribute(attributes, legacyattr::kFont, &fontText.mText)) {
                    SetTextWindowProperty(window, &fontText.mText, 0);
                } else {
                    LegacyText name;
                    name.mText.FromChar(kDefaultFontName);
                    const uint32_t id = UIDefaults()->FindFontId(&name.mText);
                    face->SetFontObject(UIDefaults()->GetFont(id));
                }
            }

            uint8_t red, green, blue;
            for (uint32_t i = 0; i < sizeof(kButtonColorIndexes) / sizeof(kButtonColorIndexes[0]); ++i) {
                if (FindColorAttribute(attributes, kButtonColorAttribute + i, &red, &green, &blue))
                    face->SetColor(kButtonColorIndexes[i], NativeColor(red, green, blue));
            }

            LegacyText gutters;
            if (FindTextAttribute(attributes, legacyattr::kGutters, &gutters.mText)) {
                // One comma "(a,b)", three "(a,b,c,d)"; any other count does nothing. A text that does not give its numbers
                // keeps stack values in the original, which the tests do not make.
                const uint32_t commas = CountCharacters(gutters.Chars(), gutters.mText.Strlen(), ',');
                int32_t a = 0, b = 0, c = 0, d = 0;
                if (commas == 1) {
                    std::sscanf(gutters.Chars(), "(%d,%d)", &a, &b);
                    face->SetGutters(a, b);
                } else if (commas == 3) {
                    std::sscanf(gutters.Chars(), "(%d,%d,%d,%d)", &a, &b, &c, &d);
                    face->SetGutterRect(a, b, c, d);
                }
            }

            LegacyText imageText;
            if (FindTextAttribute(attributes, legacyattr::kImage, &imageText.mText)) {
                // The parse is not checked: a text without both numbers leaves stack values in the original.
                uint32_t group = 0, instance = 0;
                std::sscanf(imageText.Chars(), "{%x,%x}", &group, &instance);
                cIGZLegacyImage *const image = LoadImageResource(group, instance);
                int32_t rect[4];
                if (image) std::memcpy(rect, image->GetRect(), sizeof(rect));
                face->SetImage(look, image, image ? rect : nullptr);
                const cGZPersistResourceKey key{kResourceTypeImage, group, instance, 0};
                SetResourceKeyProperty(window, kButtonImageKeyProperty, &key);
                SetImagePairProperty(window, group, instance, 0);
                if (image) image->Release();
            } else {
                face->SetImage(look, nullptr, nullptr);
            }

            if (FindUint32Attribute(attributes, legacyattr::kAlign, &value)) {
                if (value == legacyattr::kStyleLeft)
                    face->SetAlign(kButtonAlignLeft);
                else if (value == legacyattr::kStyleRight)
                    face->SetAlign(kButtonAlignRight);
                else if (value == legacyattr::kAlignCenterWord)
                    face->SetAlign(kButtonAlignCenter);
            }

            LegacyText tipText;
            if (FindTextAttribute(attributes, kTipTextAttribute, &tipText.mText)) face->SetTipText(&tipText.mText);
            LegacyText tipResource;
            if (FindTextAttribute(attributes, kTipResourceAttribute, &tipResource.mText)) {
                uint32_t group = 0, instance = 0;
                std::sscanf(tipResource.Chars(), "{%x,%x}", &group, &instance);
                LegacyText localized;
                if (LoadLocalizedText(group, instance, localized.mText.AsInterface())) face->SetTipText(&localized.mText);
                const cGZPersistResourceKey key{kTextResourceType, group, instance, 0};
                SetResourceKeyProperty(window, kTipResourceKeyProperty, &key);
            }
            int32_t x, y;
            if (FindPairAttribute(attributes, kTipOffsetsAttribute, &x, &y)) face->SetTipOffsets(x, y);
            face->SetTipFlags(FindUint32Attribute(attributes, kTipFlagAttribute, &value) ? value : kDefaultTipFlags);

            ApplySound(window, attributes, face, kClickSoundAttribute, kClickSoundProperty, kClickSound);
            ApplySound(window, attributes, face, kReleaseSoundAttribute, kReleaseSoundProperty, kReleaseSound);
        } else {
            if (FindUint32Attribute(attributes, kShowCaptionAttribute, &value))
                face->SetFlag(cIGZWinButton::kFlagShowCaption, value != 0);
        }
    }
    if (face) face->Release();
    return true;
}
