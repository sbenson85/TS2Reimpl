// The window classes' handlers of cGZLegacyControl, F6 part 5 (lane MK): the option group, then (further down) the rest of
// the list. Built for size, as the game's. src/gz/cGZLegacyHandlers.h has the faces and the handlers' notes.
#include "gz/cGZLegacyHandlersInternal.h"

#include "game/original.h"
#include "gz/cGZPersistResourceManager.h" // cGZPersistResourceKey
#include "gz/cIGZCOMDirector.h"
#include "rz/StaticGetters.h" // GetCOMDirector
#include "ifaces/cIGZWin.h"
#include "rz/cIGZVariant.h"
#include "rz/cRZIniFile.h" // cRZIniStringHelpers::SplitOff
#include "rz/cRZString.h"
#include "rz/cRZVariant.h"

#include <cstdio>

#pragma warning(disable : 4996) // sscanf, as the game calls it

using namespace legacyhandlers;

namespace {

// ---- GZWinOptGrp's attributes (RegisterOptGrp 0x8614af) ----
constexpr uint32_t kMainCaptionAttribute = 0xf00;
constexpr uint32_t kItemCaptionAttribute = 0xf01;
constexpr uint32_t kDrawFillAttribute = 0xf02;
constexpr uint32_t kOutlineAttribute = 0xf019;
constexpr uint32_t kLabelFontAttribute = 0xf03;
constexpr uint32_t kButtonFontAttribute = 0xf04;
constexpr uint32_t kLabelColorAttribute = 0xf05; // 0xf05..0xf09: consecutive
constexpr uint32_t kColorCount = 5;
constexpr uint32_t kCaptionResourceAttribute = 0xf0a;
constexpr uint32_t kBackImageAttribute = 0xf0b;
constexpr uint32_t kAutofillTypeAttribute = 0xf0c;
constexpr uint32_t kOptionAttribute = 0xf0d;
constexpr uint32_t kAutomaticAttribute = 0xf0e;
constexpr uint32_t kAutofillSizeAttribute = 0xf10;
constexpr uint32_t kOptionMoveToAttribute = 0xf11;
constexpr uint32_t kOptionSetSizeAttribute = 0xf12;
constexpr uint32_t kGroupColorsAttribute = 0x40c;
constexpr uint32_t kInitSelectionAttribute = 0x501;
constexpr uint32_t kCaptionKeyProperty = 0x4a4af4ac;
constexpr uint32_t kBackImageKeyProperty = 0x2b343bec;
constexpr uint32_t kOptionMarkProperty = 0xdab5167e;
constexpr uint32_t kUntypedBytes = 0x8000; // the bytes variants' tag (cRZVariant ORs in the array bit)
constexpr uint32_t kAutofillAutomatic = 0;
constexpr uint32_t kAutofillManual = 1;
constexpr char kOptionSeparator[] = ":";
constexpr int32_t kDefaultImageRect[4] = {0, 0, 1, 1};

struct FlagAttribute {
    uint32_t attribute;
    uint32_t mask;
};
constexpr FlagAttribute kFlagAttributes[] = {
    {kMainCaptionAttribute, cIGZWinOptGrp::kFlagMainCaption},
    {kItemCaptionAttribute, cIGZWinOptGrp::kFlagItemCaption},
    {kDrawFillAttribute, cIGZWinOptGrp::kFlagDrawFill},
    {kOutlineAttribute, cIGZWinOptGrp::kFlagOutline},
};

// SplitOff's out: raw memory until the callee constructs it.
struct SplitPiece {
    union {
        cRZString mText;
    };
    SplitPiece() {}
    SplitPiece(const SplitPiece &) = delete;
    SplitPiece &operator=(const SplitPiece &) = delete;
    ~SplitPiece() { mText.Destruct(); }
};

// The text of `rest` up to the first separator goes to `piece` (and leaves `rest`).
void TakePiece(LegacyText &rest, LegacyText &piece)
{
    SplitPiece split;
    reinterpret_cast<cRZIniStringHelpers *>(&rest.mText)->SplitOff(&split.mText, kOptionSeparator);
    piece.mText.mString = split.mText.mString;
}

// A font attribute of the group (a number: the UI defaults service's font on the face and the number kept as the window's
// font property `index`; a text: the property only; none: nothing).
template <class Set>
void ApplyGroupFont(ifaces::cIGZWin *window, const cGZScriptAttributes *attributes, uint32_t attribute, int32_t index,
                    LegacyText &text, Set set)
{
    uint32_t value;
    if (FindUint32Attribute(attributes, attribute, &value)) {
        set(UIDefaults()->GetFont(value));
        SetUint32WindowProperty(window, value, index);
    } else if (FindTextAttribute(attributes, attribute, &text.mText)) {
        SetTextWindowProperty(window, &text.mText, index);
    }
}

void ApplyGroupFirstPass(ifaces::cIGZWin *window, const cGZScriptAttributes *attributes, cIGZWinOptGrp *face)
{
    uint32_t value;
    for (const FlagAttribute &flag : kFlagAttributes) {
        if (FindUint32Attribute(attributes, flag.attribute, &value)) face->SetFlag(flag.mask, value == 1);
    }
    int32_t x, y;
    if (FindPairAttribute(attributes, legacyattr::kGutters, &x, &y)) face->SetGutters(x, y);

    LegacyText fontText;
    ApplyGroupFont(window, attributes, kLabelFontAttribute, 0, fontText,
                   [face](uint32_t font) { face->SetLabelFont(font); });
    ApplyGroupFont(window, attributes, kButtonFontAttribute, 1, fontText,
                   [face](uint32_t font) { face->SetButtonFont(font); });

    uint8_t red, green, blue;
    for (uint32_t i = 0; i < kColorCount; ++i) {
        if (FindColorAttribute(attributes, kLabelColorAttribute + i, &red, &green, &blue))
            face->SetColor(i, NativeColor(red, green, blue));
    }

    LegacyText captionKeyText;
    if (FindTextAttribute(attributes, kCaptionResourceAttribute, &captionKeyText.mText)) {
        // The parse is not checked: a text with no numbers leaves stack values in the original (0 here).
        uint32_t group = 0, instance = 0;
        std::sscanf(captionKeyText.Chars(), "{%x,%x}", &group, &instance);
        LegacyText caption;
        if (LoadLocalizedText(group, instance, caption.mText.AsInterface())) face->GetWindow()->SetCaption(&caption.mText);
        const uint32_t pair[2] = {group, instance};
        window->SetProperty(kCaptionKeyProperty, new cRZVariant(pair, sizeof(pair), kUntypedBytes));
    }

    LegacyText text;
    if (FindTextAttribute(attributes, kBackImageAttribute, &text.mText)) {
        uint32_t group, instance;
        if (std::sscanf(text.Chars(), "{%x,%x}", &group, &instance) == 2) {
            cIGZLegacyImage *const image = LoadImageResource(group, instance);
            if (image) face->SetBackImage(image);
            SetImagePairProperty(window, group, instance, 0);
            const cGZPersistResourceKey key{kResourceTypeImage, group, instance, 0};
            SetResourceKeyProperty(window, kBackImageKeyProperty, &key);
            if (image) image->Release();
        }
    }
    if (FindTextAttribute(attributes, kGroupColorsAttribute, &text.mText)) {
        int32_t c[12];
        if (std::sscanf(text.Chars(), "(%d,%d,%d)(%d,%d,%d)(%d,%d,%d)(%d,%d,%d)", &c[0], &c[1], &c[2], &c[3], &c[4], &c[5],
                        &c[6], &c[7], &c[8], &c[9], &c[10], &c[11]) == 12) {
            // The original converts the last colour first.
            const uint32_t fourth = NativeColor(uint8_t(c[9]), uint8_t(c[10]), uint8_t(c[11]));
            const uint32_t third = NativeColor(uint8_t(c[6]), uint8_t(c[7]), uint8_t(c[8]));
            const uint32_t second = NativeColor(uint8_t(c[3]), uint8_t(c[4]), uint8_t(c[5]));
            const uint32_t first = NativeColor(uint8_t(c[0]), uint8_t(c[1]), uint8_t(c[2]));
            face->SetOutlineColors(first, second, third, fourth);
        }
    }

    if (FindUint32Attribute(attributes, kAutofillTypeAttribute, &value)) {
        if (value == kAutomaticAttribute) {
            if (FindUint32Attribute(attributes, kAutofillSizeAttribute, &value)) face->SetAutofill(kAutofillAutomatic, value);
        } else {
            face->SetAutofill(kAutofillManual, 0);
        }
    }
}

// The second pass: the group's options. The original's locals outlive the loops (the strings, the numbers), and its first
// number is stored in the argument slot of the window pointer.
void ApplyGroupSecondPass(ifaces::cIGZWin *window, const cGZScriptAttributes *attributes, cIGZWinOptGrp *face)
{
    LegacyText piece, label, rest;
    int32_t first = static_cast<int32_t>(reinterpret_cast<uintptr_t>(window));
    int32_t second = 0;
    uint32_t group = 0, instance = 0, groupCopy = 0;

    const uint32_t count = attributes->Size();
    for (uint32_t i = 0; i < count; ++i) {
        const cGZScriptAttribute &attribute = attributes->mBegin[i];
        if (attribute.mId != kOptionAttribute) continue;
        cIGZVariant *const item = reinterpret_cast<cIGZVariant *>(attribute.mValue);
        if (item->GetType() != kTextVariantType) continue;
        item->GetValString(*rest.mText.AsInterface(), 0, kTextSeparator);
        TakePiece(rest, piece);
        std::sscanf(piece.Chars(), "%d", &first);
        TakePiece(rest, label);
        TakePiece(rest, piece);
        std::sscanf(piece.Chars(), "%d", &second);
        TakePiece(rest, piece);
        cIGZLegacyImage *image = nullptr;
        if (std::sscanf(piece.Chars(), "{%x,%x}", &group, &instance) == 2) {
            groupCopy = group;
            image = LoadImageResource(group, instance);
        }
        piece.mText.mString = rest.mText.mString;
        int32_t rect[4] = {kDefaultImageRect[0], kDefaultImageRect[1], kDefaultImageRect[2], kDefaultImageRect[3]};
        int32_t parsed[4];
        if (std::sscanf(piece.Chars(), "(%d,%d,%d,%d)", &parsed[0], &parsed[1], &parsed[2], &parsed[3]) == 4) {
            for (int part = 0; part < 4; ++part) rect[part] = parsed[part];
        }
        face->AddOption(first, &label.mText, second, image, rect);
        cIGZWinOptionItem *option = nullptr;
        if (face->GetOption(first, &option)) {
            SetImagePairProperty(option->GetWindow(), groupCopy, instance, 0);
            cRZVariant *const mark = new cRZVariant(true);
            option->GetWindow()->SetProperty(kOptionMarkProperty, mark);
            option->Release();
            option = nullptr;
        }
        if (image) image->Release();
    }

    for (uint32_t i = 0; i < count; ++i) {
        const cGZScriptAttribute &attribute = attributes->mBegin[i];
        if (attribute.mId == kOptionMoveToAttribute) {
            cIGZVariant *const item = reinterpret_cast<cIGZVariant *>(attribute.mValue);
            if (item->GetType() == kTextVariantType) {
                item->GetValString(*rest.mText.AsInterface(), 0, kTextSeparator);
                TakePiece(rest, piece);
                std::sscanf(piece.Chars(), "%d", &first);
                int32_t x, y;
                if (std::sscanf(rest.Chars(), "(%d,%d)", &x, &y) == 2) face->MoveOption(first, x, y);
            }
        }
        if (attribute.mId == kOptionSetSizeAttribute) {
            cIGZVariant *const item = reinterpret_cast<cIGZVariant *>(attribute.mValue);
            if (item->GetType() == kTextVariantType) {
                item->GetValString(*rest.mText.AsInterface(), 0, kTextSeparator);
                TakePiece(rest, piece);
                std::sscanf(piece.Chars(), "%d", &first);
                int32_t x, y;
                if (std::sscanf(rest.Chars(), "(%d,%d)", &x, &y) == 2) face->SetOptionSize(first, x, y);
            }
        }
    }
    uint32_t value;
    if (FindUint32Attribute(attributes, kInitSelectionAttribute, &value)) face->SetInitialSelection(value);
}

} // namespace

bool __cdecl ApplyOptionGroupProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply)
{
    cIGZWinOptGrp *face = nullptr;
    if (window->QueryInterface(cIGZWinOptGrp::kIID, reinterpret_cast<void **>(&face))) {
        if (apply)
            ApplyGroupFirstPass(window, attributes, face);
        else
            ApplyGroupSecondPass(window, attributes, face);
    }
    if (face) face->Release();
    return true;
}

// ---- cGZDummyWinProc (vtable 0xe49270) ---------------------------------------------------------------------------------

bool cGZDummyWinProc::QueryInterface(uint32_t riid, void **out)
{
    if (riid != 1 && riid != kQueryId) return false;
    *out = this;
    AddRef();
    return true;
}

uint32_t cGZDummyWinProc::AddRef()
{
    reinterpret_cast<cIGZCOMDirector *>(GetCOMDirector())->AddRef();
    return ++mRefCount;
}

uint32_t cGZDummyWinProc::Release()
{
    reinterpret_cast<cIGZCOMDirector *>(GetCOMDirector())->RemoveRef();
    if (mRefCount != 0) {
        --mRefCount;
        if (mRefCount != 0) return mRefCount;
    }
    delete this;
    return 0;
}

bool cGZDummyWinProc::vf3_cGZDummyWinProc(uint32_t, uint32_t)
{
    return false;
}

bool cGZDummyWinProc::HandlesMessage(uint32_t, uint32_t message, uint32_t, uint32_t, uint32_t)
{
    return message == kMessageA || message == kMessageB;
}

cGZDummyWinProc::~cGZDummyWinProc() {}

// ---- SC3WinGen (0x86d5ee) ----------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kGenUserDataAttribute = 0x10000;
constexpr uint32_t kGenBlitTypeAttribute = 0x10001;
constexpr uint32_t kGenDividerAttribute = 0x10002;
constexpr uint32_t kGenBlueBarAttribute = 0x10003;
constexpr uint32_t kGenFillAttribute = 0xf00d;
constexpr uint32_t kGenImageKeyProperty = 0xab428c5d;

// A flag attribute: its uint32 sets the mask by being non-zero, or by being zero.
struct GenFlag {
    uint32_t attribute;
    uint32_t mask;
    bool onWhenZero;
};
constexpr GenFlag kGenFlags[] = {
    {0x10004, cIGZWinGen::kFlagMoveable, false},
    {0x10005, cIGZWinGen::kFlagSizeable, false},
    {0x10006, cIGZWinGen::kFlagDefaultKeys, false},
    {0x10007, cIGZWinGen::kFlagCloseVisible, false},
    {0x10008, cIGZWinGen::kFlagGoBackVisible, false},
    {0x10009, cIGZWinGen::kFlagMinMaxVisible, false},
    {0x1000a, cIGZWinGen::kFlagCloseDisabled, false},
    {0x1000b, cIGZWinGen::kFlagGoBackDisabled, false},
    {0x1000c, cIGZWinGen::kFlagMinMaxDisabled, false},
    {0x1000d, cIGZWinGen::kFlagNoTitleBar, true},
    {kGenFillAttribute, cIGZWinGen::kFlagNoFill, true},
    {kOutlineAttribute, cIGZWinGen::kFlagNoOutline, true},
    {0x1000e, cIGZWinGen::kFlagNoPaint, true},
    {0x1000f, cIGZWinGen::kFlagSideBar, false},
};

void ApplyGenFirstPass(ifaces::cIGZWin *window, const cGZScriptAttributes *attributes, cIGZWinGen *face)
{
    uint32_t value;
    for (const GenFlag &flag : kGenFlags) {
        if (FindUint32Attribute(attributes, flag.attribute, &value)) face->SetFlag(flag.mask, (value == 0) == flag.onWhenZero);
    }
    if (FindUint32Attribute(attributes, kGenUserDataAttribute, &value)) face->SetUserData(value);

    LegacyText text;
    if (FindTextAttribute(attributes, legacyattr::kImage, &text.mText)) {
        uint32_t group, instance;
        if (std::sscanf(text.Chars(), "{%x,%x}", &group, &instance) == 2) {
            cIGZLegacyImage *const image = LoadImageResource(group, instance);
            uint32_t type = 0;
            FindUint32Attribute(attributes, kGenBlitTypeAttribute, &type);
            if (image) face->SetBackImage((type == 0 || type == 2) ? int32_t(type) : 1, image);
            const cGZPersistResourceKey key{kResourceTypeImage, group, instance, 0};
            SetResourceKeyProperty(window, kGenImageKeyProperty, &key);
            SetImagePairProperty(window, group, instance, 0);
            if (image) image->Release();
        }
    }
    int32_t x, y;
    if (FindPairAttribute(attributes, legacyattr::kGutters, &x, &y)) face->SetGutters(x, y);
    face->SetWinProc(new cGZDummyWinProc);
}

void ApplyGenSecondPass(const cGZScriptAttributes *attributes, cIGZWinGen *face)
{
    const uint32_t count = attributes->Size();
    for (uint32_t i = 0; i < count; ++i) {
        const cGZScriptAttribute &attribute = attributes->mBegin[i];
        if (attribute.mId != kGenDividerAttribute && attribute.mId != kGenBlueBarAttribute) continue;
        cIGZVariant *const item = reinterpret_cast<cIGZVariant *>(attribute.mValue);
        if (!item || item->GetType() != kTextVariantType) continue;
        LegacyText text;
        item->GetValString(*text.mText.AsInterface(), 0, kTextSeparator);
        if (attribute.mId == kGenDividerAttribute) {
            uint32_t value;
            int32_t amount;
            if (std::sscanf(text.Chars(), "(%u,%d)", &value, &amount) == 2) face->SetDivider(value, amount);
        } else {
            uint32_t value;
            int32_t rect[4];
            if (std::sscanf(text.Chars(), "(%u,%d,%d,%d,%d)", &value, &rect[0], &rect[1], &rect[2], &rect[3]) == 5)
                face->SetBlueBar(value, 0, 0, rect);
        }
    }
}

} // namespace

bool __cdecl ApplyGenProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply)
{
    cIGZWinGen *face = nullptr;
    if (window->QueryInterface(cIGZWinGen::kIID, reinterpret_cast<void **>(&face))) {
        if (apply)
            ApplyGenFirstPass(window, attributes, face);
        else
            ApplyGenSecondPass(attributes, face);
    }
    if (face) face->Release();
    return true;
}

// ---- GZWinFileBrowser (0x866410) ---------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kBrowserCaptionAttribute = 0x1301;
constexpr uint32_t kBrowserKindAttribute = 0x1305;
constexpr uint32_t kBrowserFirstTextAttribute = 0x1306;
constexpr uint32_t kBrowserSecondTextAttribute = 0x1307;
constexpr uint32_t kBrowserCornerFirstAttribute = 0x130c;
constexpr uint32_t kBrowserCornerSecondAttribute = 0x130d;
constexpr uint32_t kBrowserFilterAttribute = 0x1312;
constexpr uint32_t kBrowserBarFirstAttribute = 0x1319;
constexpr uint32_t kBrowserBarSecondAttribute = 0x131a;
constexpr uint32_t kBrowserBarThirdAttribute = 0x131b;
constexpr uint32_t kBrowserValueAAttribute = 0x1334;
constexpr uint32_t kBrowserValueBAttribute = 0x1335;
constexpr uint32_t kBrowserValueCAttribute = 0x1336;
constexpr uint32_t kBrowserSelectionFlagAttribute = 0x1337;
constexpr uint32_t kBrowserSelectionColorAttribute = 0x1338;
constexpr uint32_t kBrowserCursorFlagAttribute = 0x1339;
constexpr uint32_t kBrowserCursorColorAttribute = 0x133a;
constexpr uint32_t kBrowserBorderFirstAttribute = 0x133b;
constexpr uint32_t kBrowserBorderSecondAttribute = 0x133c;
constexpr uint32_t kBrowserPathAttribute = 0x133d;
constexpr uint32_t kBrowserValueDAttribute = 0x133e;
constexpr uint32_t kBrowserColorDAttribute = 0x133f;
constexpr uint32_t kBrowserImageAttribute = 0x1340;
constexpr uint32_t kBrowserFlagEAttribute = 0x1341;
constexpr uint32_t kBrowserEntryAttribute = 0x1342;
constexpr uint32_t kBrowserImageKeyProperty = 0xec64c1b3;
constexpr uint32_t kBrowserEntryKeyProperty = 0x2c64e5c4;
constexpr uint32_t kBrowserColorsPerSection = 6;

// The sections: a font attribute (the window's font property `property`), six consecutive colour attributes and a mode.
struct BrowserSection {
    uint32_t fontAttribute;
    uint32_t colorAttribute;
    uint32_t modeAttribute;
    int32_t property;
};
constexpr BrowserSection kBrowserSectionA = {0x131c, 0x131d, 0x1323, 0};
constexpr BrowserSection kBrowserSectionB = {0x1324, 0x1325, 0x132b, 1};
constexpr BrowserSection kBrowserSectionC = {0x132c, 0x132d, 0x1333, 2};

// An attribute whose uint32 names a choice by an id from a table: the number the id stands for.
struct ValueChoice {
    uint32_t id;
    uint32_t value;
};
constexpr ValueChoice kKindChoices[] = {
    {0x1302, 0},
    {0x1303, 1},
    {0x1304, 2},
};
constexpr ValueChoice kCornerChoices[] = {
    {0x1308, 0},
    {0x1309, 1},
    {0x130a, 2},
    {0x130b, 3},
};
constexpr ValueChoice kFilterChoices[] = {
    {0x130e, 0},
    {0x130f, 1},
    {0x1310, 2},
    {0x1311, 3},
};
constexpr ValueChoice kBarChoices[] = {
    {0x1313, 0},
    {0x1314, 1},
    {0x1315, 2},
    {0x1316, 5},
};
constexpr ValueChoice kModeChoices[] = {
    {0x1317, 0},
    {0x1318, 1},
    {0xf006, 2},
    {0xf007, 4},
    {0xf008, 3},
};

// True with `value` set when the attribute is a uint32 that is one of the choices.
template <size_t N>
bool ChooseValue(const cGZScriptAttributes *attributes, uint32_t attribute, const ValueChoice (&choices)[N], uint32_t *value)
{
    uint32_t id;
    if (!FindUint32Attribute(attributes, attribute, &id)) return false;
    for (const ValueChoice &choice : choices) {
        if (choice.id == id) {
            *value = choice.value;
            return true;
        }
    }
    return false;
}

// A section: the font (set from the UI defaults service and kept as the window's property), the six colours, the mode.
template <class SetFont, class SetColor, class SetMode>
void ApplyBrowserSection(ifaces::cIGZWin *window, const cGZScriptAttributes *attributes, LegacyText &fontText,
                         const BrowserSection &section, SetFont setFont, SetColor setColor, SetMode setMode)
{
    ApplyGroupFont(window, attributes, section.fontAttribute, section.property, fontText, setFont);
    uint8_t red, green, blue;
    for (uint32_t i = 0; i < kBrowserColorsPerSection; ++i) {
        if (FindColorAttribute(attributes, section.colorAttribute + i, &red, &green, &blue))
            setColor(i, NativeColor(red, green, blue));
    }
    uint32_t mode;
    if (ChooseValue(attributes, section.modeAttribute, kModeChoices, &mode)) setMode(mode);
}

// The entries: "label:number:{group,instance}" texts, each given with its image; the image and the key kept per entry.
void ApplyBrowserEntries(ifaces::cIGZWin *window, const cGZScriptAttributes *attributes, cIGZWinFileBrowser *face,
                         LegacyText &text)
{
    const uint32_t count = attributes->Size();
    for (uint32_t i = 0; i < count; ++i) {
        const cGZScriptAttribute &attribute = attributes->mBegin[i];
        if (attribute.mId != kBrowserEntryAttribute) continue;
        cIGZVariant *const item = reinterpret_cast<cIGZVariant *>(attribute.mValue);
        if (item->GetType() != kTextVariantType) continue;
        item->GetValString(*text.mText.AsInterface(), 0, kTextSeparator);
        LegacyText label, numberText;
        TakePiece(text, label);
        TakePiece(text, numberText);
        int32_t number = 0;
        std::sscanf(numberText.Chars(), "%d", &number);
        uint32_t group, instance;
        cIGZLegacyImage *image = nullptr;
        if (std::sscanf(text.Chars(), "{%x,%x}", &group, &instance) == 2) {
            image = LoadImageResource(group, instance);
            SetImagePairProperty(window, group, instance, int32_t(i + 1));
            const cGZPersistResourceKey key{kResourceTypeImage, group, instance, 0};
            AppendFileBrowserKey(window, kBrowserEntryKeyProperty, int32_t(i), &key);
        }
        face->AddEntry(&label.mText, image, number, 0);
        if (image) image->Release();
    }
}

void ApplyBrowserFirstPass(ifaces::cIGZWin *window, const cGZScriptAttributes *attributes, cIGZWinFileBrowser *face)
{
    LegacyText text;
    if (FindTextAttribute(attributes, kBrowserCaptionAttribute, &text.mText)) face->SetCaption(&text.mText);

    LegacyText firstText, secondText;
    FindTextAttribute(attributes, kBrowserFirstTextAttribute, &firstText.mText);
    FindTextAttribute(attributes, kBrowserSecondTextAttribute, &secondText.mText);
    uint32_t value;
    if (ChooseValue(attributes, kBrowserKindAttribute, kKindChoices, &value))
        face->SetModeTexts(value, &firstText.mText, &secondText.mText);

    uint32_t cornerFirst = 0, cornerSecond = 0;
    face->GetCorners(&cornerFirst, &cornerSecond);
    ChooseValue(attributes, kBrowserCornerFirstAttribute, kCornerChoices, &cornerFirst);
    ChooseValue(attributes, kBrowserCornerSecondAttribute, kCornerChoices, &cornerSecond);
    face->SetCorners(cornerFirst, cornerSecond);

    if (ChooseValue(attributes, kBrowserFilterAttribute, kFilterChoices, &value)) face->SetFilter(value);

    uint32_t barFirst = 0, barSecond = 0, barThird = 0;
    face->GetBars(&barFirst, &barSecond, &barThird);
    ChooseValue(attributes, kBrowserBarFirstAttribute, kBarChoices, &barFirst);
    ChooseValue(attributes, kBrowserBarSecondAttribute, kBarChoices, &barSecond);
    ChooseValue(attributes, kBrowserBarThirdAttribute, kBarChoices, &barThird);
    face->SetBars(barFirst, barSecond, barThird);

    LegacyText fontText;
    ApplyBrowserSection(
        window, attributes, fontText, kBrowserSectionA, [face](uint32_t font) { face->SetFontA(font); },
        [face](uint32_t index, uint32_t color) { face->SetColorA(index, color); }, [face](uint32_t mode) { face->SetModeA(mode); });
    ApplyBrowserSection(
        window, attributes, fontText, kBrowserSectionB, [face](uint32_t font) { face->SetFontB(font); },
        [face](uint32_t index, uint32_t color) { face->SetColorB(index, color); }, [face](uint32_t mode) { face->SetModeB(mode); });
    ApplyBrowserSection(
        window, attributes, fontText, kBrowserSectionC, [face](uint32_t font) { face->SetFontC(font); },
        [face](uint32_t index, uint32_t color) { face->SetColorC(index, color); }, [face](uint32_t mode) { face->SetModeC(mode); });

    if (FindUint32Attribute(attributes, kBrowserValueAAttribute, &value)) face->SetValueA(value);
    if (FindUint32Attribute(attributes, kBrowserValueBAttribute, &value)) face->SetValueB(value);
    if (FindUint32Attribute(attributes, kBrowserValueCAttribute, &value)) face->SetValueC(value);

    uint8_t red, green, blue;
    bool enabled = false;
    uint32_t color = 0;
    face->GetSelection(&enabled, &color);
    if (FindUint32Attribute(attributes, kBrowserSelectionFlagAttribute, &value)) enabled = value != 0;
    if (FindColorAttribute(attributes, kBrowserSelectionColorAttribute, &red, &green, &blue))
        color = NativeColor(red, green, blue);
    face->SetSelection(enabled, color);

    face->GetCursor(&enabled, &color);
    if (FindUint32Attribute(attributes, kBrowserCursorFlagAttribute, &value)) enabled = value != 0;
    if (FindColorAttribute(attributes, kBrowserCursorColorAttribute, &red, &green, &blue))
        color = NativeColor(red, green, blue);
    face->SetCursor(enabled, color);

    uint32_t borderFirst = 0, borderSecond = 0;
    face->GetBorder(&borderFirst, &borderSecond);
    if (FindColorAttribute(attributes, kBrowserBorderFirstAttribute, &red, &green, &blue))
        borderFirst = NativeColor(red, green, blue);
    if (FindColorAttribute(attributes, kBrowserBorderSecondAttribute, &red, &green, &blue))
        borderSecond = NativeColor(red, green, blue);
    face->SetBorder(borderFirst, borderSecond);

    if (FindTextAttribute(attributes, kBrowserPathAttribute, &text.mText)) face->SetPath(&text.mText);
    if (FindUint32Attribute(attributes, kBrowserValueDAttribute, &value)) face->SetValueD(value);
    if (FindColorAttribute(attributes, kBrowserColorDAttribute, &red, &green, &blue))
        face->SetColorD(NativeColor(red, green, blue));
    if (FindUint32Attribute(attributes, kBrowserFlagEAttribute, &value)) face->SetFlagE(value != 0);
    if (FindUint32Attribute(attributes, kOutlineAttribute, &value)) face->SetOutline(value != 0);

    if (FindTextAttribute(attributes, kBrowserImageAttribute, &text.mText)) {
        uint32_t group, instance;
        if (std::sscanf(text.Chars(), "{%x,%x}", &group, &instance) == 2) {
            cIGZLegacyImage *const image = LoadImageResource(group, instance);
            if (image) face->SetBackImage(image);
            SetImagePairProperty(window, group, instance, 0);
            const cGZPersistResourceKey key{kResourceTypeImage, group, instance, 0};
            SetResourceKeyProperty(window, kBrowserImageKeyProperty, &key);
            if (image) image->Release();
        }
        ApplyBrowserEntries(window, attributes, face, text);
    }
}

} // namespace

bool __cdecl ApplyFileBrowserProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply)
{
    cIGZWinFileBrowser *face = nullptr;
    if (window->QueryInterface(cIGZWinFileBrowser::kIID, reinterpret_cast<void **>(&face))) {
        if (apply) ApplyBrowserFirstPass(window, attributes, face);
    }
    if (face) face->Release();
    return true;
}
