// The legacy script writer's class writers, part 3 (lane WA, foundations): src/gz/cGZLegacyScriptWriter.h. Built for size,
// as the game's.
#include "gz/cGZLegacyScriptWriter.h"

#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZLegacyHandlers.h"
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

constexpr uint32_t kOptGrpCaptionResourceProperty = 0x4a4af4ac;
constexpr uint32_t kOptGrpBackImageProperty = 0x2b343bec;
constexpr uint32_t kAutofitProperty = 0xdacf8f9c;
constexpr uint32_t kOptionVisitorKind = 0x8810;
constexpr uint32_t kVariantIsArray = 0x8000;

// "<name>=(r,g,b) " of a native colour, through the graphics system; the bytes are the caller's.
void NativeColorAttribute(cRZString *out, const char *name, uint32_t native, uint8_t *red, uint8_t *green, uint8_t *blue)
{
    GetGraphicSystem()->ConvertNativeToRGB(native, red, green, blue);
    AppendColorAttribute(out, Of(Text(name)), *red, *green, *blue);
}

// Window slot 37: `visit(kind, callback, out)` calls the callback for each child item of that kind.
using OptionVisitor = bool(__cdecl *)(void *item, void *context);

// TextEdit's flags (0x878762): mask, name, written even when the flag has its default (0x11ff). The table ends with a null name.
constexpr LegacyFlagName kTextEditFlags[] = {
    {0x1, "editable", true},     {0x2, "wrapped", true},        {0x4, "hscrollbar", true},
    {0x8, "vscrollbar", true},   {0x10, "outline", true},       {0x20, "opaque", true},
    {0x40, "caretvisible", true}, {0x80, "allowinsert", true},  {0x100, "allowundo", true},
    {0x200, "singleline", true}, {0x400, "notifyonreturn", false}, {0x800, "notifyonchange", false},
    {0x8000, "notifyonlostfocus", false}, {0x1000, "enableclipboard", false}, {0x2000, "passwordmode", false},
    {0, nullptr, false}};
constexpr uint32_t kTextEditDefaultFlags = 0x11ff;
constexpr uint32_t kHScrollImageProperty = 0x8b4bfefc;
constexpr uint32_t kVScrollImageProperty = 0x8b4bfefd;
constexpr uint32_t kPromptResourceProperty = 0xb1a4e021;
constexpr uint32_t kBtnImageProperty = 0x2b343bec;
constexpr uint32_t kBtnTipResourceProperty = 0x2b340ffe;

// "<name>=<key>" of a window property that holds a resource key: the group and instance are its second and third words.
void KeyPropertyAttribute(cRZString *out, const char *name, const uint32_t *key)
{
    AppendKeyAttribute(out, Of(Text(name)), key[1], key[2]);
}

} // namespace

void __cdecl WriteOptGrpAttributes(cIGZWinOptGrp *face, cRZString *out)
{
    FlagAttribute(out, "maincaption", face->IsFlagSet(cIGZWinOptGrp::kFlagMainCaption));
    FlagAttribute(out, "itemcaption", face->IsFlagSet(cIGZWinOptGrp::kFlagItemCaption));
    FlagAttribute(out, "drawfill", face->IsFlagSet(cIGZWinOptGrp::kFlagDrawFill));
    FlagAttribute(out, "outline", face->IsFlagSet(cIGZWinOptGrp::kFlagOutline));

    // The answer of GetGutters is ignored: both numbers are written.
    int32_t x, y;
    face->GetGutters(&x, &y);
    AppendPairAttribute(out, Of(Text("gutters")), x, y);

    LegacyText font;
    if (FormatFontProperty(AsWin(face->GetWindow()), font.mText.AsInterface(), 0))
        AppendTextAttribute(out, Of(Text("labelfont")), &font.mText);
    if (FormatFontProperty(AsWin(face->GetWindow()), font.mText.AsInterface(), 1))
        AppendTextAttribute(out, Of(Text("buttonfont")), &font.mText);

    cIGZVariant *resource = nullptr;
    if (AsWin(face->GetWindow())->GetProperty(kOptGrpCaptionResourceProperty, &resource)) {
        if (resource->GetType() & kVariantIsArray) {
            const uint32_t *words = resource->GetUint32Array();
            if (words[0] != 0 || words[1] != 0) AppendKeyAttribute(out, Of(Text("captiontextresid")), words[0], words[1]);
        }
        resource->Release();
    }

    uint32_t key[4] = {};
    if (GetKeyProperty(AsWin(face->GetWindow()), kOptGrpBackImageProperty, key))
        AppendKeyAttribute(out, Of(Text("backimage")), key[1], key[2]);

    // One native colour and one set of bytes for the five colours: a getter that answers without writing repeats them.
    uint32_t native;
    uint8_t red, green, blue;
    static const char *const kColorNames[5] = {"labelfontcolornormal", "labelfontcolordisabled", "buttonfontcolornormal",
                                               "buttonfontcolorhilited", "buttonfontcolordisabled"};
    for (uint32_t i = 0; i < 5; ++i)
        if (face->GetColor(i, &native)) NativeColorAttribute(out, kColorNames[i], native, &red, &green, &blue);

    // The four outline colours: the first goes through the same bytes as above, the others have their own.
    uint32_t second, third, fourth;
    if (face->GetOutlineColors(&native, &second, &third, &fourth)) {
        uint8_t r1, g1, b1, r2, g2, b2, r3, g3, b3;
        GetGraphicSystem()->ConvertNativeToRGB(native, &red, &green, &blue);
        GetGraphicSystem()->ConvertNativeToRGB(second, &r1, &g1, &b1);
        GetGraphicSystem()->ConvertNativeToRGB(third, &r2, &g2, &b2);
        GetGraphicSystem()->ConvertNativeToRGB(fourth, &r3, &g3, &b3);
        AppendColor4Attribute(out, Of(Text("outlinecolor")), red, green, blue, r1, g1, b1, r2, g2, b2, r3, g3, b3);
    }

    AppendIntAttribute(out, Of(Text("initselection")), face->GetInitialSelection());

    uint32_t type, size;
    if (face->GetAutofill(&type, &size)) {
        if (type == 0) {
            AppendTextAttribute(out, Of(Text("autofilltype")), Of(Text("automatic")));
            AppendIntAttribute(out, Of(Text("autofillsize")), size);
        } else {
            AppendTextAttribute(out, Of(Text("autofilltype")), Of(Text("manual")));
        }
    }

    cIGZVariant *autofit = nullptr;
    if (AsWin(face->GetWindow())->GetProperty(kAutofitProperty, &autofit)) {
        if (*autofit->AsBoolRef()) AppendWordAttribute(out, Of(Text("autofit")));
        autofit->Release();
    }

    // The option items: window slot 37 calls the original callback 0x86ea36 for each (option, optionmoveto, optionsetsize).
    RawSlot<37, bool>(AsWin(face->GetWindow()), kOptionVisitorKind, OriginalAddress<OptionVisitor, 0x86ea36>(), out);
}

void __cdecl WriteTextEditAttributes(cIGZWinTextEdit *face, cRZString *out)
{
    AppendFlagAttributes(out, face->GetFlags(), kTextEditDefaultFlags, kTextEditFlags);

    LegacyText fontText;
    uint32_t fontId;
    if (face->GetFont(&fontId) && FormatFontId(fontId, fontText.mText.AsInterface()))
        AppendTextAttribute(out, Of(Text("font")), &fontText.mText);

    LegacyText initial;
    initial.mText.FromChar(face->GetInitialValue());
    AppendQuotedAttribute(out, Of(Text("initvalue")), &initial.mText);

    // One native colour and one set of bytes for every colour: a getter that answers without writing repeats them.
    uint32_t native;
    uint8_t red, green, blue;
    static const struct {
        uint32_t index;
        const char *name;
    } kColors[6] = {{0, "colorfontnormal"},    {2, "colorfontdisabled"},    {4, "colorfonthilited"},
                    {1, "colorfontnormalbkg"}, {3, "colorfontdisabledbkg"}, {5, "colorfonthilitedbkg"}};
    for (const auto &c : kColors)
        if (face->GetColor(c.index, &native)) NativeColorAttribute(out, c.name, native, &red, &green, &blue);
    if (face->GetCaretColor(&native)) NativeColorAttribute(out, "caretcolor", native, &red, &green, &blue);
    native = face->GetHighlightColor();
    NativeColorAttribute(out, "highlightcolor", native, &red, &green, &blue);
    uint8_t outlined;
    face->GetOutlineColor(&outlined, &native);
    if (outlined) NativeColorAttribute(out, "outlinecolor", native, &red, &green, &blue);

    AppendIntAttribute(out, Of(Text("maxtext")), face->GetMaxText());
    AppendIntAttribute(out, Of(Text("maxglyphs")), face->GetMaxGlyphs());
    const int32_t gutterX = face->GetGutterX();
    const int32_t gutterY = face->GetGutterY();
    AppendPairAttribute(out, Of(Text("gutters")), gutterX, gutterY);

    // The insert mode is written inverted, as "overwrite".
    AppendTextAttribute(out, Of(Text("overwrite")), Of(Text(face->GetInsertMode() ? "no" : "yes")));
    uint32_t insertIndex;
    if (face->GetInsertIndex(&insertIndex)) AppendIntAttribute(out, Of(Text("insertindex")), insertIndex);
    int32_t insertX, insertY;
    if (face->GetInsertPos(&insertX, &insertY)) AppendPairAttribute(out, Of(Text("insertpos")), insertX, insertY);
    uint32_t period;
    if (face->GetCaretPeriod(&period)) AppendIntAttribute(out, Of(Text("caretperiod")), period);
    AppendIntAttribute(out, Of(Text("maxundo")), face->GetMaxUndo());

    // The three resource keys share one buffer: a lookup that finds nothing leaves what the one before wrote.
    uint32_t key[4] = {};
    if (GetKeyProperty(AsWin(face->GetWindow()), kHScrollImageProperty, key)) KeyPropertyAttribute(out, "hscrollimage", key);
    if (GetKeyProperty(AsWin(face->GetWindow()), kVScrollImageProperty, key)) KeyPropertyAttribute(out, "vscrollimage", key);

    cIGZLegacyImage *image;
    int32_t rect[4];
    if (face->GetHScrollImage(&image, rect))
        AppendRectAttribute(out, Of(Text("hscrollimageRect")), rect[0], rect[1], rect[2], rect[3]);
    if (face->GetVScrollImage(&image, rect))
        AppendRectAttribute(out, Of(Text("vscrollimageRect")), rect[0], rect[1], rect[2], rect[3]);

    cIGZVariant *autofit = nullptr;
    if (AsWin(face->GetWindow())->GetProperty(kAutofitProperty, &autofit)) {
        if (*autofit->AsBoolRef()) AppendWordAttribute(out, Of(Text("autofit")));
        autofit->Release();
    }

    LegacyText prompt;
    if (face->GetPrompt(&prompt.mText)) AppendQuotedAttribute(out, Of(Text("prompt")), &prompt.mText);
    if (GetKeyProperty(AsWin(face->GetWindow()), kPromptResourceProperty, key) && (key[2] != 0 || key[1] != 0))
        KeyPropertyAttribute(out, "promptres", key);
}

void __cdecl WriteBtnAttributes(cIGZWinButton *face, cRZString *out)
{
    // One key buffer for the image and the tooltip resource.
    uint32_t key[4] = {};
    if (GetKeyProperty(AsWin(face->GetWindow()), kBtnImageProperty, key)) KeyPropertyAttribute(out, "image", key);

    LegacyText fontText;
    uint32_t fontId;
    if (face->GetFontId(&fontId) && FormatFontId(fontId, fontText.mText.AsInterface()))
        AppendTextAttribute(out, Of(Text("font")), &fontText.mText);

    // One native colour and one set of bytes for the six colours: a getter that answers without writing repeats them.
    uint32_t native;
    uint8_t red, green, blue;
    static const struct {
        uint32_t index;
        const char *name;
    } kColors[6] = {{0, "colorfontnormal"},    {4, "colorfontdisabled"},    {2, "colorfonthilited"},
                    {1, "colorfontnormalbkg"}, {5, "colorfontdisabledbkg"}, {3, "colorfonthilitedbkg"}};
    for (const auto &c : kColors)
        if (face->GetColor(c.index, &native)) NativeColorAttribute(out, c.name, native, &red, &green, &blue);

    // The flags: the first two are on / off, the others yes / no, and fill, tipsdelay and tipstimeout are written inverted
    // (their flags are the "no" ones).
    AppendTextAttribute(out, Of(Text("toggle")), Of(Text(face->IsFlagSet(cIGZWinButton::kFlagToggle) ? "on" : "off")));
    AppendTextAttribute(out, Of(Text("triggerondown")),
                        Of(Text(face->IsFlagSet(cIGZWinButton::kFlagTriggerOnDown) ? "on" : "off")));
    AppendTextAttribute(out, Of(Text("showcaption")), Of(Text(face->IsFlagSet(cIGZWinButton::kFlagShowCaption) ? "yes" : "no")));
    AppendTextAttribute(out, Of(Text("fill")), Of(Text(face->IsFlagSet(cIGZWinButton::kFlagNoFill) ? "no" : "yes")));
    AppendTextAttribute(out, Of(Text("autosize")), Of(Text(face->IsFlagSet(cIGZWinButton::kFlagAutoSize) ? "yes" : "no")));
    AppendTextAttribute(out, Of(Text("wrapcaption")), Of(Text(face->IsFlagSet(cIGZWinButton::kFlagWrapCaption) ? "yes" : "no")));
    AppendTextAttribute(out, Of(Text("shiftcaption")), Of(Text(face->IsFlagSet(cIGZWinButton::kFlagShiftCaption) ? "yes" : "no")));
    AppendTextAttribute(out, Of(Text("tips")), Of(Text(face->IsFlagSet(cIGZWinButton::kFlagTips) ? "yes" : "no")));
    AppendTextAttribute(out, Of(Text("tipsdelay")), Of(Text(face->IsFlagSet(cIGZWinButton::kFlagNoTipsDelay) ? "no" : "yes")));
    AppendTextAttribute(out, Of(Text("tipstimeout")), Of(Text(face->IsFlagSet(cIGZWinButton::kFlagNoTipsTimeout) ? "no" : "yes")));

    // The look: 1 label, 2 standard, 3 toggle, 4 radiocheck; any other writes nothing.
    switch (face->GetLook()) {
    case 1: AppendTextAttribute(out, Of(Text("style")), Of(Text("label"))); break;
    case 2: AppendTextAttribute(out, Of(Text("style")), Of(Text("standard"))); break;
    case 3: AppendTextAttribute(out, Of(Text("style")), Of(Text("toggle"))); break;
    case 4: AppendTextAttribute(out, Of(Text("style")), Of(Text("radiocheck"))); break;
    default: break;
    }

    // The gutter getter's answer is ignored. Its first two words are also the tooltip offsets' locals.
    int32_t first, second, third, fourth;
    face->GetGutterRect(&first, &second, &third, &fourth);
    AppendRectAttribute(out, Of(Text("gutters")), first, second, third, fourth);

    LegacyText tip;
    if (face->GetTipText(&tip.mText)) AppendQuotedAttribute(out, Of(Text("tiptext")), &tip.mText);
    if (GetKeyProperty(AsWin(face->GetWindow()), kBtnTipResourceProperty, key) && (key[2] != 0 || key[1] != 0))
        KeyPropertyAttribute(out, "tipres", key);

    if (face->GetTipOffsets(&first, &second)) AppendPairAttribute(out, Of(Text("tipoffsets")), first, second);
    uint32_t tipFlags;
    if (face->GetTipFlags(&tipFlags)) AppendHexAttribute(out, Of(Text("tipflag")), tipFlags);

    uint32_t align;
    if (face->GetAlign(&align)) {
        switch (align) {
        case 0: AppendTextAttribute(out, Of(Text("align")), Of(Text("center"))); break;
        case 1: AppendTextAttribute(out, Of(Text("align")), Of(Text("left"))); break;
        case 2: AppendTextAttribute(out, Of(Text("align")), Of(Text("right"))); break;
        default: break;
        }
    }

    if (const uint32_t click = face->GetSound(0)) AppendKeyAttribute(out, Of(Text("btnclicksnd")), 0, click);
    if (const uint32_t release = face->GetSound(1)) AppendKeyAttribute(out, Of(Text("btnupsnd")), 0, release);
}
