// The legacy script writer's class writers, part 2 (lane FJ, foundations): src/gz/cGZLegacyScriptWriter.h. Built for size,
// as the game's.
#include "gz/cGZLegacyScriptWriter.h"

#include "gz/cGZLegacyHandlersInternal.h" // GetGraphicSystem
#include "gz/cGZLegacyScriptWriterInternal.h"
#include "ifaces/cIGZWin.h"
#include "rz/cIGZVariant.h"

#include <cstdint>

namespace {

using legacy_script::AsWin;
using legacy_script::FlagAttribute;
using legacy_script::Of;
using legacy_script::Text;

constexpr uint32_t kTextAutofitProperty = 0xdacf8f9c;
constexpr uint32_t kSliderImageProperty = 0x6b4bcbd4;
constexpr uint32_t kGenImageProperty = 0xab428c5d;
constexpr uint32_t kSpinnerImageProperty = 0x8bd1333a;

// "<name>=(r,g,b) " of a native colour, through the graphics system; the bytes are the caller's.
void NativeColorAttribute(cRZString *out, const char *name, uint32_t native, uint8_t *red, uint8_t *green, uint8_t *blue)
{
    GetGraphicSystem()->ConvertNativeToRGB(native, red, green, blue);
    AppendColorAttribute(out, Of(Text(name)), *red, *green, *blue);
}

} // namespace

void __cdecl WriteTextAttributes(cIGZWinText *face, cRZString *out)
{
    LegacyText font;
    uint32_t fontId;
    if (face->GetFont(&fontId) && FormatFontId(fontId, font.mText.AsInterface()))
        AppendTextAttribute(out, Of(Text("font")), &font.mText);
    uint32_t align;
    if (face->GetAlign(&align)) {
        static const char *const kAligns[] = {"lefttop",      "leftcenter", "leftbottom",  "centertop",   "center",
                                              "centerbottom", "righttop",   "rightcenter", "rightbottom"};
        if (align <= 8)
            AppendTextAttribute(out, Of(Text("align")), Of(Text(kAligns[align])));
        else
            AppendHexAttribute(out, Of(Text("align")), align);
    }
    FlagAttribute(out, "notify", face->IsFlagSet(cIGZWinText::kFlagNotify));
    FlagAttribute(out, "wrapped", face->IsFlagSet(cIGZWinText::kFlagWrapped));
    FlagAttribute(out, "opaque", face->IsFlagSet(cIGZWinText::kFlagOpaque));
    // One native colour and one set of bytes for both colours: a getter that answers without writing repeats them.
    uint32_t native;
    uint8_t red, green, blue;
    if (face->GetColor(0, &native)) NativeColorAttribute(out, "forecolor", native, &red, &green, &blue);
    if (face->GetColor(1, &native)) NativeColorAttribute(out, "bkgcolor", native, &red, &green, &blue);
    // One pair of signed bytes for the gutters and the text offsets.
    int8_t x, y;
    if (face->GetGutters(&x, &y)) AppendPairAttribute(out, Of(Text("gutters")), x, y);
    if (face->GetTextOffsets(&x, &y)) AppendPairAttribute(out, Of(Text("textoffsets")), x, y);
    cIGZVariant *autofit = nullptr;
    if (AsWin(face->GetWindow())->GetProperty(kTextAutofitProperty, &autofit)) {
        if (*autofit->AsBoolRef()) AppendWordAttribute(out, Of(Text("autofit")));
        autofit->Release();
    }
}

void __cdecl WriteSliderAttributes(cIGZWinSlider *face, cRZString *out)
{
    const int32_t low = face->GetMin();
    const int32_t high = face->GetMax();
    AppendPairAttribute(out, Of(Text("minmaxvalue")), low, high);
    // The answer of GetDirection is ignored: the word is left as it was when the getter writes nothing.
    uint32_t direction = 0;
    face->GetDirection(&direction);
    if (direction == 2)
        AppendTextAttribute(out, Of(Text("direction")), Of(Text("vertical")));
    else if (direction == 1)
        AppendTextAttribute(out, Of(Text("direction")), Of(Text("horizontal")));
    uint32_t key[4] = {};
    if (GetKeyProperty(AsWin(face->GetWindow()), kSliderImageProperty, key))
        AppendKeyAttribute(out, Of(Text("image")), key[1], key[2]);
    AppendIntAttribute(out, Of(Text("initvalue")), face->GetValue());
}

void __cdecl WriteSpinnerAttributes(cIGZWinSpinner *face, cRZString *out)
{
    uint32_t key[4] = {};
    if (GetKeyProperty(AsWin(face->GetWindow()), kSpinnerImageProperty, key))
        AppendKeyAttribute(out, Of(Text("image")), key[1], key[2]);
    LegacyText font;
    if (FormatFontProperty(AsWin(face->GetWindow()), font.mText.AsInterface(), 0))
        AppendTextAttribute(out, Of(Text("font")), &font.mText);
    FlagAttribute(out, "outline", face->IsFlagSet(cIGZWinSpinner::kFlagOutline));
    FlagAttribute(out, "fill", face->IsFlagSet(cIGZWinSpinner::kFlagFill));
    FlagAttribute(out, "autonumber", face->IsFlagSet(cIGZWinSpinner::kFlagAutoNumber));
    FlagAttribute(out, "autonumbercomma", face->IsFlagSet(cIGZWinSpinner::kFlagAutoNumberComma));
    // ORIGINAL-BUG: the currency flag is read and the attribute is written "yes" whatever it answers.
    face->IsFlagSet(cIGZWinSpinner::kFlagAutoNumberCurrency);
    FlagAttribute(out, "autonumbercurrency", true);
    AppendIntAttribute(out, Of(Text("initvalue")), face->GetValue());
    // The answers of the range, step size, outline colour and gutter getters are ignored: they are taken to write.
    int32_t low = 0, high = 0;
    face->GetRange(&low, &high);
    AppendPairAttribute(out, Of(Text("minmax")), low, high);
    int32_t step = 0;
    face->GetStepSize(&step);
    AppendIntAttribute(out, Of(Text("stepsize")), step);
    uint32_t outline[4] = {};
    face->GetOutlineColors(&outline[0], &outline[1], &outline[2], &outline[3]);
    // One set of bytes for every colour of the writer.
    uint8_t red, green, blue;
    NativeColorAttribute(out, "coloroutlinel", outline[0], &red, &green, &blue);
    NativeColorAttribute(out, "coloroutlinet", outline[1], &red, &green, &blue);
    NativeColorAttribute(out, "coloroutliner", outline[2], &red, &green, &blue);
    NativeColorAttribute(out, "coloroutlineb", outline[3], &red, &green, &blue);
    uint32_t digits;
    if (face->GetDigits(&digits)) {
        switch (digits) {
        case 0: AppendTextAttribute(out, Of(Text("digits")), Of(Text("no"))); break;
        case 1: AppendTextAttribute(out, Of(Text("digits")), Of(Text("left"))); break;
        case 2: AppendTextAttribute(out, Of(Text("digits")), Of(Text("right"))); break;
        }
    }
    uint32_t align;
    if (face->GetAlign(&align)) {
        switch (align) {
        case 0: AppendTextAttribute(out, Of(Text("align")), Of(Text("left"))); break;
        case 1: AppendTextAttribute(out, Of(Text("align")), Of(Text("center"))); break;
        case 2: AppendTextAttribute(out, Of(Text("align")), Of(Text("right"))); break;
        }
    }
    // One native colour for the three font colours (a getter that answers without writing repeats it).
    uint32_t native;
    if (face->GetColor(0, &native)) NativeColorAttribute(out, "colorfontnormal", native, &red, &green, &blue);
    if (face->GetColor(2, &native)) NativeColorAttribute(out, "colorfontdisabled", native, &red, &green, &blue);
    if (face->GetColor(1, &native)) NativeColorAttribute(out, "colorfonthilited", native, &red, &green, &blue);
    uint8_t x = 0, y = 0;
    face->GetGutters(&x, &y);
    AppendPairAttribute(out, Of(Text("gutters")), x, y);
}

void __cdecl WriteLineInputAttributes(cIGZWinLineInput *face, cRZString *out)
{
    LegacyText font;
    if (FormatFontProperty(AsWin(face->GetWindow()), font.mText.AsInterface(), 0))
        AppendTextAttribute(out, Of(Text("font")), &font.mText);
    // One native colour and one set of bytes for every colour of the writer.
    uint32_t native;
    uint8_t red, green, blue;
    if (face->GetColor(0, &native)) NativeColorAttribute(out, "colorfontnormal", native, &red, &green, &blue);
    if (face->GetColor(1, &native)) NativeColorAttribute(out, "colorfontdisabled", native, &red, &green, &blue);
    if (face->GetColor(2, &native)) NativeColorAttribute(out, "colorfonthilited", native, &red, &green, &blue);
    if (face->GetBorderColor(0, &native)) NativeColorAttribute(out, "colorLT", native, &red, &green, &blue);
    if (face->GetBorderColor(1, &native)) NativeColorAttribute(out, "colorRB", native, &red, &green, &blue);
    if (face->GetBorderColor(2, &native)) NativeColorAttribute(out, "colorLTinside", native, &red, &green, &blue);
    if (face->GetBorderColor(3, &native)) NativeColorAttribute(out, "colorRBinside", native, &red, &green, &blue);
    if (face->GetHighlightFillColor(&native)) NativeColorAttribute(out, "fillcolorhighlite", native, &red, &green, &blue);
    // The answer of the gutter getter is ignored: the bytes are taken to be written.
    int8_t left = 0, top = 0, right = 0, bottom = 0;
    face->GetGutterRect(&left, &top, &right, &bottom);
    AppendRectAttribute(out, Of(Text("gutters")), left, top, right, bottom);
    FlagAttribute(out, "filled", face->IsFilled());
    FlagAttribute(out, "editable", face->IsEditable());
    // The answer of the style getter is ignored too.
    uint32_t style = 0;
    face->GetOutlineStyle(&style);
    switch (style) {
    case 1: AppendTextAttribute(out, Of(Text("style")), Of(Text("normal"))); break;
    case 2: AppendTextAttribute(out, Of(Text("style")), Of(Text("outline3d"))); break;
    case 3: AppendTextAttribute(out, Of(Text("style")), Of(Text("outline3dEx"))); break;
    }
    float alpha = 0.0f;
    face->GetHighlightFillAlpha(&alpha);
    AppendIntAttribute(out, Of(Text("fillalphahilite")), int32_t(int64_t(alpha * 255.0f)));
    // One number for the caret, the character limit and the selection's start (a getter that answers without writing
    // repeats it).
    int32_t value;
    if (face->GetCaretPosition(&value)) AppendIntAttribute(out, Of(Text("caretpos")), value);
    if (face->GetCharLimit(&value)) AppendIntAttribute(out, Of(Text("charlimit")), value);
    int32_t length = 0;
    if (face->GetSelection(&value, &length))
        AppendPairAttribute(out, Of(Text("selection")), value, int32_t(uint32_t(value) + uint32_t(length)));
}

void __cdecl WriteComboAttributes(cIGZWinCombo *face, cRZString *out)
{
    LegacyText font;
    uint32_t fontId;
    if (face->GetFont(&fontId) && FormatFontId(fontId, font.mText.AsInterface()))
        AppendTextAttribute(out, Of(Text("font")), &font.mText);
    // One native colour and one set of bytes for every colour of the writer.
    uint32_t native;
    uint8_t red, green, blue;
    if (face->GetColor(0, &native)) NativeColorAttribute(out, "colorfontnormal", native, &red, &green, &blue);
    if (face->GetColor(1, &native)) NativeColorAttribute(out, "colorfontdisabled", native, &red, &green, &blue);
    if (face->GetColor(2, &native)) NativeColorAttribute(out, "colorfonthilited", native, &red, &green, &blue);
    if (face->GetHighlightColor(&native)) NativeColorAttribute(out, "highlightcolor", native, &red, &green, &blue);
    FlagAttribute(out, "editable", face->IsEditable());
    if (face->GetOutlineColor(&native)) NativeColorAttribute(out, "outlinecolor", native, &red, &green, &blue);
    AppendIntAttribute(out, Of(Text("initselection")), face->GetInitialSelection());
    uint32_t group, instance;
    if (GetImagePairProperty(AsWin(face->GetWindow()), &group, &instance, 0))
        AppendKeyAttribute(out, Of(Text("combodownarrowimage")), group, instance);
    // The image is not used (and not released); the rectangle is the getter's to write.
    cIGZLegacyImage *image = nullptr;
    int32_t rect[4];
    if (face->GetArrowImage(&image, rect))
        AppendRectAttribute(out, Of(Text("combodownarrowrect")), rect[0], rect[1], rect[2], rect[3]);
    if (face->GetArrowColor(&native)) NativeColorAttribute(out, "combodowncolor", native, &red, &green, &blue);
    int8_t buttonGutter;
    if (face->GetButtonGutter(&buttonGutter)) AppendIntAttribute(out, Of(Text("buttongutter")), buttonGutter);
    int8_t x, y;
    if (face->GetGutters(&x, &y)) AppendPairAttribute(out, Of(Text("gutters")), x, y);
    // One text for all the elements: an element the face does not write repeats the one before it.
    LegacyText element;
    for (int32_t i = 0; face->GetElement(i, element.mText.AsInterface()); ++i)
        AppendQuotedAttribute(out, Of(Text("listelement")), &element.mText);
}

void __cdecl WriteListBoxAttributes(cIGZWinListBox *face, cRZString *out)
{
    int8_t vertical = 0, horizontal = 0;
    if (face->GetScrollbars(&vertical, &horizontal)) {
        FlagAttribute(out, "vscrollbar", vertical != 0);
        FlagAttribute(out, "hscrollbar", horizontal != 0);
    }
    LegacyText font;
    uint32_t fontId = 0;
    if (face->GetFont(&fontId) && FormatFontId(fontId, font.mText.AsInterface()))
        AppendTextAttribute(out, Of(Text("font")), &font.mText);
    // One native colour and one set of bytes for every colour of the writer.
    uint32_t native;
    uint8_t red, green, blue;
    if (face->GetColor(0, &native)) NativeColorAttribute(out, "colorfontnormal", native, &red, &green, &blue);
    if (face->GetColor(1, &native)) NativeColorAttribute(out, "colorfontdisabled", native, &red, &green, &blue);
    if (face->GetColor(2, &native)) NativeColorAttribute(out, "colorfonthilited", native, &red, &green, &blue);
    if (face->GetHighlightColor(&native)) NativeColorAttribute(out, "highlightcolor", native, &red, &green, &blue);
    // The pair of outline colours: the first goes through the shared native colour and bytes, the second has its own.
    uint32_t second = 0;
    uint8_t red2 = 0, green2 = 0, blue2 = 0;
    if (face->GetOutlineColors(&native, &second)) {
        GetGraphicSystem()->ConvertNativeToRGB(native, &red, &green, &blue);
        GetGraphicSystem()->ConvertNativeToRGB(second, &red2, &green2, &blue2);
        AppendColor2Attribute(out, Of(Text("outlinecolor")), red, green, blue, red2, green2, blue2);
    }
    FlagAttribute(out, "sunken", face->IsSunken());
    FlagAttribute(out, "outline", face->IsOutline());
    // One pair of signed bytes for the scroll bar gutters and the gutters.
    int8_t x = 0, y = 0;
    if (face->GetScrollbarGutters(&x, &y)) AppendPairAttribute(out, Of(Text("scrollbargutters")), x, y);
    if (face->GetGutters(&x, &y)) AppendPairAttribute(out, Of(Text("gutters")), x, y);
    FlagAttribute(out, "sort", face->IsSorted());
    FlagAttribute(out, "drop", face->IsDrop());
    AppendIntAttribute(out, Of(Text("initselection")), face->GetInitialSelection());
    // A fresh text for every element.
    const int32_t count = face->GetElementCount();
    for (int32_t i = 0; i < count; ++i) {
        LegacyText element;
        if (face->GetElement(i, element.mText.AsInterface(), 0))
            AppendQuotedAttribute(out, Of(Text("listelement")), &element.mText);
    }
    cIGZVariant *autofit = nullptr;
    if (AsWin(face->GetWindow())->GetProperty(kTextAutofitProperty, &autofit)) {
        if (*autofit->AsBoolRef()) AppendWordAttribute(out, Of(Text("autofit")));
        autofit->Release();
    }
}

void __cdecl WriteGenAttributes(cIGZWinGen *face, cRZString *out)
{
    uint32_t key[4] = {};
    if (GetKeyProperty(AsWin(face->GetWindow()), kGenImageProperty, key))
        AppendKeyAttribute(out, Of(Text("image")), key[1], key[2]);
    // The type is read only when the face answers; the second word is a scratch the original passes.
    int32_t type = 0, other = 0;
    if (face->GetBackImageType(&type, &other)) {
        if (type == 0)
            AppendTextAttribute(out, Of(Text("blttype")), Of(Text("tiled")));
        else if (type == 1)
            AppendTextAttribute(out, Of(Text("blttype")), Of(Text("normal")));
        else if (type == 2)
            AppendTextAttribute(out, Of(Text("blttype")), Of(Text("edge")));
    }
    AppendIntAttribute(out, Of(Text("userdata")), int32_t(face->GetUserData()));
    FlagAttribute(out, "moveable", face->IsFlagSet(cIGZWinGen::kFlagMoveable));
    FlagAttribute(out, "sizeable", face->IsFlagSet(cIGZWinGen::kFlagSizeable));
    FlagAttribute(out, "defaultkeys", face->IsFlagSet(cIGZWinGen::kFlagDefaultKeys));
    FlagAttribute(out, "closevisible", face->IsFlagSet(cIGZWinGen::kFlagCloseVisible));
    FlagAttribute(out, "gobackvisible", face->IsFlagSet(cIGZWinGen::kFlagGoBackVisible));
    FlagAttribute(out, "minmaxvisible", face->IsFlagSet(cIGZWinGen::kFlagMinMaxVisible));
    FlagAttribute(out, "closedisabled", face->IsFlagSet(cIGZWinGen::kFlagCloseDisabled));
    FlagAttribute(out, "gobackdisabled", face->IsFlagSet(cIGZWinGen::kFlagGoBackDisabled));
    FlagAttribute(out, "minmaxdisabled", face->IsFlagSet(cIGZWinGen::kFlagMinMaxDisabled));
    // The four "no" flags are set to hide the part: they are written inverted.
    FlagAttribute(out, "titlebar", !face->IsFlagSet(cIGZWinGen::kFlagNoTitleBar));
    FlagAttribute(out, "fill", !face->IsFlagSet(cIGZWinGen::kFlagNoFill));
    FlagAttribute(out, "outline", !face->IsFlagSet(cIGZWinGen::kFlagNoOutline));
    FlagAttribute(out, "paint", !face->IsFlagSet(cIGZWinGen::kFlagNoPaint));
    FlagAttribute(out, "sidebar", face->IsFlagSet(cIGZWinGen::kFlagSideBar));
    // The answer is ignored: the two words are taken to be written.
    int32_t x = 0, y = 0;
    face->GetGutters(&x, &y);
    AppendPairAttribute(out, Of(Text("gutters")), x, y);
}
