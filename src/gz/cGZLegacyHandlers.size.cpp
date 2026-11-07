// The window classes' handlers of cGZLegacyControl (lane MF, foundations F6 part 2): src/gz/cGZLegacyHandlers.h. Built
// for size, as the game's.
#include "gz/cGZLegacyHandlersInternal.h"

#include "gz/cGZPersistResourceManager.h" // cGZPersistResourceKey
#include "gz/cGZUIScriptService.h"        // cIGZUIScriptService
#include "gz/cIGZFrameWork.h"
#include "ifaces/cIGZGraphicSystem.h"
#include "ifaces/cIGZPersistResourceManager.h"
#include "ifaces/cIGZWin.h"
#include "rz/MiscHelpersB.h"      // GetCachedServiceObjectB
#include "rz/Singleton_20ddfb.h" // GetServiceObject
#include "rz/cRZVariant.h"

#include <algorithm>
#include <cstdio>

#pragma warning(disable : 4996) // sscanf, as the game calls it

namespace {

constexpr uint32_t kImageResourceType = 0x856ddbac;
constexpr uint32_t kImageResourceIid = 0x06f1568e;
constexpr uint32_t kResourceManagerId = 0x056b906e;
constexpr uint32_t kResourceManagerIid = 0x656b8efc;
constexpr uint32_t kUntypedBytes = 0x8000; // the bytes variants' tag (cRZVariant ORs in the array bit)

// ---- the properties the handlers set on the window ----
constexpr uint32_t kImageKeyProperty = 0xab428c5d;   // the image's resource key (16 bytes)
constexpr uint32_t kImagePairProperty = 0xdaa3b860;  // + index: the image's group and instance (8 bytes)

// ---- GZWinBMP's attributes (RegisterBitmap 0x85da3d) ----
constexpr uint32_t kAlphaAttribute = 0xb00;
constexpr uint32_t kTransparentBackAttribute = 0xb01;
constexpr uint32_t kEdgeImageAttribute = 0xb02;
constexpr uint32_t kMaxAlpha = 0xff;

// ---- GZWinFlatRect's attributes (RegisterFlatRect 0x86024e) ----
constexpr uint32_t kUint32VariantType = 6;
constexpr uint32_t kColorLeftAttribute = 0x200; // colorleft, colortop, colorright, colorbottom: consecutive
constexpr uint32_t kColorCount = 4;
constexpr uint32_t kStyleTop = 0x204;
constexpr uint32_t kStyleBottom = 0x205;
constexpr uint32_t kStyleAll = 0x206;
constexpr uint32_t kStyleLeftRightBottom = 0x207;
constexpr uint32_t kStyleTopRightBottom = 0x208;

// ---- GZWinText's attributes (RegisterText 0x86249d) ----
constexpr uint32_t kWrappedAttribute = 0x300;
constexpr uint32_t kOpaqueAttribute = 0x301;
constexpr uint32_t kAutofitAttribute = 0x304;
constexpr uint32_t kTextOffsetsAttribute = 0x305;
constexpr uint32_t kWindowFlagTransparentToMouse = 0x20000; // set on the window when the text is not opaque
constexpr uint32_t kFontProperty = 0xfaa3be85;              // + index: the window's font (a number or a name)
constexpr uint32_t kAutofitProperty = 0xdacf8f9c;
constexpr uint32_t kTextColor = 0;
constexpr uint32_t kBackgroundColor = 1;

constexpr uint32_t kTextVariantType = 0x800e;
constexpr uint32_t kTextSeparator = 0x20; // GetValString's separator for a value of several parts

// ---- GZWinOutline's attributes (RegisterOutline 0x861999) ----
constexpr uint32_t kOutlineDarkAttribute = 0x1100;  // colordark
constexpr uint32_t kOutlineLightAttribute = 0x1101; // colorlight
constexpr uint32_t kOutlineNodeAttribute = 0x1102;  // node (repeated)
constexpr uint32_t kOutlineDarkColor = 1;           // the face's colour indexes
constexpr uint32_t kOutlineLightColor = 2;
constexpr uint32_t kMaxOutlineNodes = 32;

// ---- GZWinScrollbar2's attributes (RegisterScrollbar2 0x861b05) ----
constexpr uint32_t kOrientationAttribute = 0x1700;
constexpr uint32_t kScrollValueAttribute = 0x1701;
constexpr uint32_t kMinValueAttribute = 0x1702;
constexpr uint32_t kMaxValueAttribute = 0x1703;
constexpr uint32_t kVisibleRangeAttribute = 0x1704;
constexpr uint32_t kPageOverlapAttribute = 0x1705;
constexpr uint32_t kIncrementAttribute = 0x1706;
constexpr uint32_t kArrowsImageAttribute = 0x1707;
constexpr uint32_t kContainerImageAttribute = 0x1708;
constexpr uint32_t kThumbImageAttribute = 0x1709;
constexpr uint32_t kArrowsKeyProperty = 0x0bfa11d7; // the images' resource keys as window properties
constexpr uint32_t kContainerKeyProperty = 0x6bfa11e1;
constexpr uint32_t kThumbKeyProperty = 0x0bfa11e2;

// ---- GZWinSlider's and GZWinScrollbar's attributes (RegisterSlider 0x861fd1, RegisterScrollbar 0x861e67) ----
constexpr uint32_t kMinMaxValueAttribute = 0xc00;  // minmaxvalue: "(low,high)"
constexpr uint32_t kDirectionAttribute = 0xc01;    // direction: horizontal 0xc02 or vertical 0xc03
constexpr uint32_t kDirectionVertical = 0xc03;
constexpr uint32_t kPageSizeAttribute = 0xd00;
constexpr uint32_t kLineSizeAttribute = 0xd01;
constexpr uint32_t kLinePageCountAttribute = 0xd02; // linepagecount: "(lines,pages)"
constexpr uint32_t kInitValueAttribute = 0x40b;     // initvalue (the second pass)
constexpr uint32_t kControlImageKeyProperty = 0x6b4bcbd4; // the image's resource key as a property (both classes)

} // namespace

// ---- helpers ------------------------------------------------------------------------------------------------------------

bool __cdecl FindRectAttribute(const cGZScriptAttributes *attributes, uint32_t id, int32_t *rect)
{
    LegacyText text;
    if (!FindTextAttribute(attributes, id, &text.mText)) return false;
    return std::sscanf(text.Chars(), "(%d,%d,%d,%d)", &rect[0], &rect[1], &rect[2], &rect[3]) == 4;
}

cIGZLegacyImage *__cdecl LoadImageResource(uint32_t group, uint32_t instance)
{
    const cGZPersistResourceKey key{kImageResourceType, group, instance, 0};
    return LoadImageByKey(&key);
}

cIGZLegacyImage *__cdecl LoadImageByKey(const cGZPersistResourceKey *key)
{
    auto *const framework = static_cast<cIGZFrameWork *>(GetServiceObject());
    cIGZUIScriptService *service = nullptr;
    if (framework)
        framework->GetSystemService(cGZUIScriptService::kServiceId, cIGZUIScriptService::kIID,
                                    reinterpret_cast<void **>(&service));

    cIGZLegacyImage *image = nullptr;
    cIGZUnknown *const context = service ? service->GetContext() : nullptr;
    if (context) {
        image = static_cast<cIGZLegacyImage *>(static_cast<cIGZUIScriptContext *>(context)->LoadImage(key));
    } else {
        ifaces::cIGZPersistResourceManager *manager = nullptr;
        if (framework) framework->GetSystemService(kResourceManagerId, kResourceManagerIid, reinterpret_cast<void **>(&manager));
        cIGZLegacyImageResource *resource = nullptr;
        // No test for a missing resource manager, as the original's.
        if (manager->GetResource(key, kImageResourceIid, reinterpret_cast<void **>(&resource), 0, 0))
            image = resource->GetImage();
        reinterpret_cast<cIGZUnknown *>(manager)->Release(); // slot 2 (the generated interface leaves it unnamed)
        if (resource) resource->Release();
    }
    if (service) service->Release();
    return image;
}

bool __cdecl SetImagePairProperty(ifaces::cIGZWin *window, uint32_t group, uint32_t instance, int32_t index)
{
    const uint32_t pair[2] = {group, instance};
    window->SetProperty(kImagePairProperty + index, new cRZVariant(pair, sizeof(pair), kUntypedBytes));
    return true;
}

bool __cdecl SetUint32WindowProperty(ifaces::cIGZWin *window, uint32_t value, int32_t index)
{
    window->SetProperty(kFontProperty + index, new cRZVariant(value));
    return true;
}

bool __cdecl SetTextWindowProperty(ifaces::cIGZWin *window, const cRZString *text, int32_t index)
{
    window->SetProperty(kFontProperty + index, new cRZVariant(*text->AsInterface()));
    return true;
}

// ---- the handlers -------------------------------------------------------------------------------------------------------

bool __cdecl ApplyBitmapProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply)
{
    cIGZWinBitmap *face = nullptr;
    if (window->QueryInterface(cIGZWinBitmap::kIID, reinterpret_cast<void **>(&face))) {
        if (apply) {
            uint32_t value = 0;
            if (FindUint32Attribute(attributes, kAlphaAttribute, &value))
                face->SetAlpha(true, static_cast<float>(std::min(value, kMaxAlpha)) / 255.0f);
            if (FindUint32Attribute(attributes, legacyattr::kNotify, &value))
                face->SetFlag(cIGZWinBitmap::kFlagNotify, value != 0);
            value = 0;
            if (FindUint32Attribute(attributes, kTransparentBackAttribute, &value))
                face->SetFlag(cIGZWinBitmap::kFlagTransparentBack, value != 0);
            value = 0;
            if (FindUint32Attribute(attributes, kEdgeImageAttribute, &value))
                face->SetFlag(cIGZWinBitmap::kFlagEdgeImage, value != 0);

            LegacyText text;
            if (FindTextAttribute(attributes, legacyattr::kImage, &text.mText)) {
                // The parse is not checked: the instance keeps the last value read above (the edge image's, else 0);
                // the group of a text with no number is uninitialised stack in the original, 0 here.
                uint32_t group = 0;
                std::sscanf(text.Chars(), "{%x,%x}", &group, &value);
                cIGZLegacyImage *const image = LoadImageResource(group, value);
                if (image) {
                    face->SetImage(image);
                    const int32_t height = image->GetHeight();
                    const int32_t width = image->GetWidth();
                    window->SetSize(width, height);
                }
                const cGZPersistResourceKey key{kImageResourceType, group, value, 0};
                SetResourceKeyProperty(window, kImageKeyProperty, &key);
                SetImagePairProperty(window, group, value, 0);
                if (image) image->Release();
            }
        } else {
            int32_t rect[4];
            const bool found = FindRectAttribute(attributes, legacyattr::kImageRect, rect);
            if (found) face->SetImageRect(rect);
            face->SetFlag(cIGZWinBitmap::kFlagImageRect, found);
        }
    }
    if (face) face->Release();
    return true;
}

bool __cdecl ApplyFlatRectProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply)
{
    cIGZWinFlatRect *face = nullptr;
    if (window->QueryInterface(cIGZWinFlatRect::kIID, reinterpret_cast<void **>(&face)) && apply) {
        uint32_t bits = 0;
        const uint32_t count = attributes->Size();
        for (uint32_t i = 0; i < count; ++i) {
            const cGZScriptAttribute &attribute = attributes->mBegin[i];
            if (attribute.mId != legacyattr::kStyle || !attribute.mValue) continue;
            cIGZVariant *const value = reinterpret_cast<cIGZVariant *>(attribute.mValue);
            if (value->GetType() != kUint32VariantType) continue;
            switch (*value->AsUint32Ref()) {
            case legacyattr::kStyleLeft: bits |= cIGZWinFlatRect::kBorderLeft; break;
            case kStyleTop: bits |= cIGZWinFlatRect::kBorderTop; break;
            case legacyattr::kStyleRight: bits |= cIGZWinFlatRect::kBorderRight; break;
            case kStyleBottom: bits |= cIGZWinFlatRect::kBorderBottom; break;
            case kStyleAll:
                bits |= cIGZWinFlatRect::kBorderLeft | cIGZWinFlatRect::kBorderTop | cIGZWinFlatRect::kBorderRight |
                        cIGZWinFlatRect::kBorderBottom;
                break;
            case kStyleLeftRightBottom:
                bits |= cIGZWinFlatRect::kBorderLeft | cIGZWinFlatRect::kBorderRight | cIGZWinFlatRect::kBorderBottom;
                break;
            case kStyleTopRightBottom:
                bits |= cIGZWinFlatRect::kBorderTop | cIGZWinFlatRect::kBorderRight | cIGZWinFlatRect::kBorderBottom;
                break;
            case legacyattr::kStyleSunken: bits |= cIGZWinFlatRect::kSunken; break;
            case legacyattr::kStyleRaised: bits |= cIGZWinFlatRect::kRaised; break;
            case legacyattr::kStyleNoFill: bits |= cIGZWinFlatRect::kNoFill; break;
            default: break;
            }
        }
        face->SetStyle(bits);
        for (uint32_t index = 0; index < kColorCount; ++index) {
            uint8_t red, green, blue;
            if (FindColorAttribute(attributes, kColorLeftAttribute + index, &red, &green, &blue))
                face->SetColor(index, red, green, blue);
        }
    }
    if (face) face->Release();
    return true;
}

bool __cdecl ApplyTextProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply)
{
    cIGZWinText *face = nullptr;
    if (window->QueryInterface(cIGZWinText::kIID, reinterpret_cast<void **>(&face))) {
        if (apply) {
            uint32_t value = 0;
            if (FindUint32Attribute(attributes, legacyattr::kNotify, &value))
                face->SetFlag(cIGZWinText::kFlagNotify, value != 0);
            if (FindUint32Attribute(attributes, kWrappedAttribute, &value))
                face->SetFlag(cIGZWinText::kFlagWrapped, value != 0);
            if (FindUint32Attribute(attributes, kOpaqueAttribute, &value)) {
                face->SetFlag(cIGZWinText::kFlagOpaque, value != 0);
                if (value == 0) window->SetFlag(kWindowFlagTransparentToMouse, 1);
            }

            LegacyText text;
            uint32_t font;
            if (FindUint32Attribute(attributes, legacyattr::kFont, &font)) {
                face->SetFont(font);
                SetUint32WindowProperty(window, font, 0);
            } else if (FindTextAttribute(attributes, legacyattr::kFont, &text.mText)) {
                SetTextWindowProperty(window, &text.mText, 0);
            }

            uint8_t red, green, blue;
            if (FindColorAttribute(attributes, legacyattr::kForeColor, &red, &green, &blue))
                face->SetColor(kTextColor, NativeColor(red, green, blue));
            if (FindColorAttribute(attributes, legacyattr::kBackColor, &red, &green, &blue))
                face->SetColor(kBackgroundColor, NativeColor(red, green, blue));

            int32_t x, y;
            if (FindPairAttribute(attributes, legacyattr::kGutters, &x, &y))
                face->SetGutters(x, y);
            else
                face->SetGutters(0, 0);
            if (FindPairAttribute(attributes, kTextOffsetsAttribute, &x, &y)) face->SetTextOffsets(x, y);
        } else {
            uint32_t align;
            if (FindUint32Attribute(attributes, legacyattr::kAlign, &align))
                face->SetAlign(align == legacyattr::kAlignCenterWord ? cIGZWinText::kAlignCenter : align);
        }
        if (HasAttribute(attributes, kAutofitAttribute)) {
            window->SetProperty(kAutofitProperty, new cRZVariant(true));
            face->FitToText(0, 0);
        } else {
            window->SetProperty(kAutofitProperty, new cRZVariant(false));
        }
    }
    if (face) face->Release();
    return true;
}

bool __cdecl FindSint32Attribute(const cGZScriptAttributes *attributes, uint32_t id, int32_t *out)
{
    const uint32_t count = attributes->Size();
    for (uint32_t i = 0; i < count; ++i) {
        const cGZScriptAttribute &attribute = attributes->mBegin[i];
        if (attribute.mId != id || !attribute.mValue) continue;
        cIGZVariant *const value = reinterpret_cast<cIGZVariant *>(attribute.mValue);
        if (value->GetType() == kUint32VariantType) {
            *out = *value->AsSint32Ref();
            return true;
        }
    }
    return false;
}

namespace {

constexpr uint32_t kDirectionHorizontal = 1; // the faces' SetDirection words
constexpr uint32_t kDirectionVerticalWord = 2;

// The (low, high) pair of the minmaxvalue attribute, put in order.
bool FindOrderedRange(const cGZScriptAttributes *attributes, int32_t *low, int32_t *high)
{
    if (!FindPairAttribute(attributes, kMinMaxValueAttribute, low, high)) return false;
    if (*low > *high) std::swap(*low, *high);
    return true;
}

// The direction attribute as the faces' word (0xc03 vertical is 2, any other value 1); false without the attribute.
bool FindDirection(const cGZScriptAttributes *attributes, uint32_t *direction)
{
    uint32_t value;
    if (!FindUint32Attribute(attributes, kDirectionAttribute, &value)) return false;
    *direction = value == kDirectionVertical ? kDirectionVerticalWord : kDirectionHorizontal;
    return true;
}

// The image attribute of the slider and the scroll bar: "{group,instance}" read as two numbers (a text that does not give
// both is ignored); the image resource goes to `setImage` (when there is one) and its key stays on the window as
// properties.
template <class SetImage>
void ApplyImageAttribute(ifaces::cIGZWin *window, const cGZScriptAttributes *attributes, SetImage setImage)
{
    LegacyText text;
    uint32_t group, instance;
    if (!FindTextAttribute(attributes, legacyattr::kImage, &text.mText) ||
        std::sscanf(text.Chars(), "{%x,%x}", &group, &instance) != 2)
        return;
    cIGZLegacyImage *const image = LoadImageResource(group, instance);
    if (image) setImage(image);
    const cGZPersistResourceKey key{kImageResourceType, group, instance, 0};
    SetResourceKeyProperty(window, kControlImageKeyProperty, &key);
    SetImagePairProperty(window, group, instance, 0);
    if (image) image->Release();
}

// The second pass of both: the face's value from the initvalue attribute.
template <class Face> void ApplyInitValue(Face *face, const cGZScriptAttributes *attributes)
{
    int32_t value;
    if (FindSint32Attribute(attributes, kInitValueAttribute, &value)) face->SetValue(value);
}

} // namespace

bool __cdecl ApplySliderProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply)
{
    cIGZWinSlider *face = nullptr;
    if (window->QueryInterface(cIGZWinSlider::kIID, reinterpret_cast<void **>(&face))) {
        if (apply) {
            int32_t low, high;
            if (FindOrderedRange(attributes, &low, &high)) {
                face->SetMax(high);
                face->SetMin(low);
            }
            uint32_t direction;
            if (FindDirection(attributes, &direction)) face->SetDirection(direction);
            ApplyImageAttribute(window, attributes, [&](cIGZLegacyImage *image) {
                int32_t rect[4];
                face->SetImage(image, FindRectAttribute(attributes, legacyattr::kImageRect, rect) ? rect : nullptr);
            });
        } else {
            ApplyInitValue(face, attributes);
        }
    }
    if (face) face->Release();
    return true;
}

bool __cdecl ApplyScrollbarProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply)
{
    cIGZWinScrollbar *face = nullptr;
    if (window->QueryInterface(cIGZWinScrollbar::kIID, reinterpret_cast<void **>(&face))) {
        if (apply) {
            int32_t low, high;
            if (FindOrderedRange(attributes, &low, &high)) face->SetRange(low, high);
            uint32_t value;
            if (FindDirection(attributes, &value)) face->SetDirection(value);
            if (FindUint32Attribute(attributes, kPageSizeAttribute, &value)) face->SetPageSize(value);
            if (FindUint32Attribute(attributes, kLineSizeAttribute, &value)) face->SetLineSize(value);
            int32_t lines, pages;
            if (FindPairAttribute(attributes, kLinePageCountAttribute, &lines, &pages)) face->SetLinePageCount(lines, pages);
            ApplyImageAttribute(window, attributes, [&](cIGZLegacyImage *image) {
                int32_t rect[4];
                if (FindRectAttribute(attributes, legacyattr::kImageRect, rect))
                    face->SetImage(rect, image, 0);
                else
                    face->SetImage(image->GetRect(), image, 0);
            });
        } else {
            ApplyInitValue(face, attributes);
        }
    }
    if (face) face->Release();
    return true;
}

bool __cdecl ApplyOutlineProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply)
{
    cIGZWinOutline *face = nullptr;
    if (window->QueryInterface(cIGZWinOutline::kIID, reinterpret_cast<void **>(&face)) && apply) {
        uint32_t bits = 0;
        const uint32_t count = attributes->Size();
        for (uint32_t i = 0; i < count; ++i) {
            const cGZScriptAttribute &attribute = attributes->mBegin[i];
            if (attribute.mId != legacyattr::kStyle || !attribute.mValue) continue;
            cIGZVariant *const value = reinterpret_cast<cIGZVariant *>(attribute.mValue);
            if (value->GetType() != kUint32VariantType) continue;
            switch (*value->AsUint32Ref()) {
            case legacyattr::kStyleFlat: bits |= cIGZWinOutline::kStyleFlat; break;
            case legacyattr::kStyleSunken: bits |= cIGZWinOutline::kStyleSunken; break;
            case legacyattr::kStyleRaised: bits |= cIGZWinOutline::kStyleRaised; break;
            case legacyattr::kStyleFill: bits |= cIGZWinOutline::kStyleFill; break;
            default: break;
            }
        }
        if (bits != 0) face->SetStyle(bits);

        uint8_t red, green, blue;
        if (FindColorAttribute(attributes, kOutlineDarkAttribute, &red, &green, &blue))
            face->SetColor(kOutlineDarkColor, red, green, blue);
        if (FindColorAttribute(attributes, kOutlineLightAttribute, &red, &green, &blue))
            face->SetColor(kOutlineLightColor, red, green, blue);

        // The nodes: the first 32 `node` texts. A text that does not give two numbers keeps the previous pair's (the
        // original reuses its locals); here 0.
        int32_t nodes[kMaxOutlineNodes][2];
        uint32_t nodeCount = 0;
        int32_t x = 0, y = 0;
        for (uint32_t i = 0; i < count && nodeCount < kMaxOutlineNodes; ++i) {
            const cGZScriptAttribute &attribute = attributes->mBegin[i];
            if (attribute.mId != kOutlineNodeAttribute || !attribute.mValue) continue;
            cIGZVariant *const value = reinterpret_cast<cIGZVariant *>(attribute.mValue);
            if (value->GetType() != kTextVariantType) continue;
            LegacyText text;
            value->GetValString(*text.mText.AsInterface(), 0, kTextSeparator);
            std::sscanf(text.Chars(), "(%d,%d)", &x, &y);
            nodes[nodeCount][0] = x;
            nodes[nodeCount][1] = y;
            ++nodeCount;
        }
        if (nodeCount != 0) face->SetNodes(nodeCount, nodes[0], 1, 1);
    }
    if (face) face->Release();
    return true;
}

namespace {

// One of the scroll bar 2's image attributes: "{type,group,instance}" (hex); the image loaded by that key goes to
// `setImage`, the key stays on the window as the property `property`. Without the attribute the face's image is cleared;
// a text that does not hold three numbers does nothing.
template <class SetImage>
void ApplyKeyedImage(ifaces::cIGZWin *window, const cGZScriptAttributes *attributes, uint32_t attribute, uint32_t property,
                     SetImage setImage)
{
    LegacyText text;
    if (!FindTextAttribute(attributes, attribute, &text.mText)) {
        setImage(nullptr);
        return;
    }
    cGZPersistResourceKey key{0, 0, 0, 0};
    if (std::sscanf(text.Chars(), "{%x,%x,%x}", &key.type, &key.group, &key.instance) != 3) return;
    cIGZLegacyImage *const image = LoadImageByKey(&key);
    if (image) setImage(image);
    SetResourceKeyProperty(window, property, &key);
    if (image) image->Release();
}

} // namespace

bool __cdecl ApplyScrollbar2Properties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply)
{
    cIGZWinScrollbar2 *face = nullptr;
    if (window->QueryInterface(cIGZWinScrollbar2::kIID, reinterpret_cast<void **>(&face)) && apply) {
        uint32_t orientation;
        if (FindUint32Attribute(attributes, kOrientationAttribute, &orientation))
            face->SetVertical(orientation == kDirectionVertical);
        int32_t value;
        if (FindSint32Attribute(attributes, kScrollValueAttribute, &value)) face->SetValue(value, 0);
        if (FindSint32Attribute(attributes, kMinValueAttribute, &value)) face->SetMinValue(value, 1);
        if (FindSint32Attribute(attributes, kMaxValueAttribute, &value)) face->SetMaxValue(value, 1);
        if (FindSint32Attribute(attributes, kVisibleRangeAttribute, &value)) face->SetVisibleRange(value, 1);
        if (FindSint32Attribute(attributes, kPageOverlapAttribute, &value)) face->SetPageOverlap(value);
        if (FindSint32Attribute(attributes, kIncrementAttribute, &value)) face->SetIncrement(value);
        ApplyKeyedImage(window, attributes, kArrowsImageAttribute, kArrowsKeyProperty,
                        [&](cIGZLegacyImage *image) { face->SetArrowsImage(image); });
        ApplyKeyedImage(window, attributes, kContainerImageAttribute, kContainerKeyProperty,
                        [&](cIGZLegacyImage *image) { face->SetContainerImage(image); });
        ApplyKeyedImage(window, attributes, kThumbImageAttribute, kThumbKeyProperty,
                        [&](cIGZLegacyImage *image) { face->SetThumbImage(image); });
    }
    if (face) face->Release();
    return true;
}

namespace {

constexpr uint32_t kListElementAttribute = 0x502; // listelement (repeated; Folders, Combo and ListBox)

// ---- GZWinCombo's attributes (RegisterCombo 0x85e28d) ----
constexpr uint32_t kEditableAttribute = 0x400;
constexpr uint32_t kHighlightColorAttribute = 0x408;
constexpr uint32_t kOutlineColorAttribute = 0x40c;
constexpr uint32_t kInitSelectionAttribute = 0x501;
constexpr uint32_t kComboArrowImageAttribute = 0x800;
constexpr uint32_t kComboArrowRectAttribute = 0x801;
constexpr uint32_t kComboArrowColorAttribute = 0x802;
constexpr uint32_t kButtonGutterAttribute = 0x803;
constexpr uint32_t kComboColorAttribute = 0xf00f; // 0xf00f, 0xf010, 0xf011: consecutive
constexpr uint32_t kComboColorCount = 3;
constexpr int32_t kAppendPosition = -2;

} // namespace

bool __cdecl ApplyComboProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply)
{
    cIGZWinCombo *face = nullptr;
    if (window->QueryInterface(cIGZWinCombo::kIID, reinterpret_cast<void **>(&face))) {
        uint8_t red, green, blue;
        if (apply) {
            LegacyText fontText;
            uint32_t group = 0; // the original reads the image's group into the font's variable
            if (FindUint32Attribute(attributes, legacyattr::kFont, &group)) {
                face->SetFont(group);
                SetUint32WindowProperty(window, group, 0);
            } else if (FindTextAttribute(attributes, legacyattr::kFont, &fontText.mText)) {
                SetTextWindowProperty(window, &fontText.mText, 0);
            }

            LegacyText text;
            uint32_t instance;
            if (FindTextAttribute(attributes, kComboArrowImageAttribute, &text.mText) &&
                std::sscanf(text.Chars(), "{%x,%x}", &group, &instance) == 2) {
                cIGZLegacyImage *const image = LoadImageResource(group, instance);
                if (image) {
                    int32_t rect[4];
                    face->SetArrowImage(image, FindRectAttribute(attributes, kComboArrowRectAttribute, rect) ? rect : nullptr);
                }
                SetImagePairProperty(window, group, instance, 0);
                if (image) image->Release();
            }

            uint32_t gutter;
            if (FindUint32Attribute(attributes, kButtonGutterAttribute, &gutter)) face->SetButtonGutter(gutter);
            int32_t x, y;
            if (FindPairAttribute(attributes, legacyattr::kGutters, &x, &y)) face->SetGutters(x, y);
        } else {
            for (uint32_t index = 0; index < kComboColorCount; ++index) {
                if (FindColorAttribute(attributes, kComboColorAttribute + index, &red, &green, &blue))
                    face->SetColor(index, NativeColor(red, green, blue));
            }
            if (FindColorAttribute(attributes, kHighlightColorAttribute, &red, &green, &blue))
                face->SetHighlightColor(NativeColor(red, green, blue));
            if (FindColorAttribute(attributes, kOutlineColorAttribute, &red, &green, &blue))
                face->SetOutlineColor(NativeColor(red, green, blue));
            if (FindColorAttribute(attributes, kComboArrowColorAttribute, &red, &green, &blue))
                face->SetArrowColor(NativeColor(red, green, blue));
            uint32_t value;
            if (FindUint32Attribute(attributes, kEditableAttribute, &value)) face->SetEditable(value == 1);

            LegacyText element;
            const uint32_t count = attributes->Size();
            for (uint32_t i = 0; i < count; ++i) {
                const cGZScriptAttribute &attribute = attributes->mBegin[i];
                if (attribute.mId != kListElementAttribute) continue;
                cIGZVariant *const item = reinterpret_cast<cIGZVariant *>(attribute.mValue);
                if (item->GetType() != kTextVariantType) continue;
                item->GetValString(*element.mText.AsInterface(), 0, kTextSeparator);
                face->AddElement(&element.mText, kAppendPosition, 0);
            }
            if (FindUint32Attribute(attributes, kInitSelectionAttribute, &value)) face->SetInitialSelection(value, 1);
        }
    }
    if (face) face->Release();
    return true;
}

namespace {

// ---- GZWinListBox's attributes (RegisterListBox 0x861263) ----
constexpr uint32_t kHScrollbarAttribute = 0x401;
constexpr uint32_t kVScrollbarAttribute = 0x402;
constexpr uint32_t kScrollbarGuttersAttribute = 0x500;
constexpr uint32_t kSortAttribute = 0x503;
constexpr uint32_t kDropAttribute = 0x504;
constexpr uint32_t kLineVisibleAttribute = 0x505;
constexpr uint32_t kLineOnTopAttribute = 0x506;
constexpr uint32_t kOutlineFlagAttribute = 0xf019;
constexpr uint32_t kListColorAttribute = 0xf00f; // 0xf00f, 0xf010, 0xf011: consecutive
constexpr uint32_t kListColorCount = 3;

} // namespace

bool __cdecl ApplyListBoxProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply)
{
    cIGZWinListBox *face = nullptr;
    if (window->QueryInterface(cIGZWinListBox::kIID, reinterpret_cast<void **>(&face))) {
        uint32_t value;
        if (apply) {
            bool vertical = true, horizontal = false;
            if (FindUint32Attribute(attributes, kVScrollbarAttribute, &value)) vertical = value == 1;
            if (FindUint32Attribute(attributes, kHScrollbarAttribute, &value)) horizontal = value == 1;
            face->SetScrollbars(vertical, horizontal);

            LegacyText fontText;
            uint32_t font;
            if (FindUint32Attribute(attributes, legacyattr::kFont, &font)) {
                face->SetFont(font);
                SetUint32WindowProperty(window, font, 0);
            } else if (FindTextAttribute(attributes, legacyattr::kFont, &fontText.mText)) {
                SetTextWindowProperty(window, &fontText.mText, 0);
            }

            uint8_t red, green, blue;
            for (uint32_t index = 0; index < kListColorCount; ++index) {
                if (FindColorAttribute(attributes, kListColorAttribute + index, &red, &green, &blue))
                    face->SetColor(index, NativeColor(red, green, blue));
            }
            if (FindColorAttribute(attributes, kHighlightColorAttribute, &red, &green, &blue))
                face->SetHighlightColor(NativeColor(red, green, blue));
            if (FindUint32Attribute(attributes, kOutlineFlagAttribute, &value)) face->SetOutline(value == 1);

            LegacyText colors;
            int32_t c[6];
            if (FindTextAttribute(attributes, kOutlineColorAttribute, &colors.mText) &&
                std::sscanf(colors.Chars(), "(%d,%d,%d)(%d,%d,%d)", &c[0], &c[1], &c[2], &c[3], &c[4], &c[5]) == 6) {
                // The original converts the second triple first.
                const uint32_t second = NativeColor(c[3], c[4], c[5]);
                const uint32_t first = NativeColor(c[0], c[1], c[2]);
                face->SetOutlineColors(first, second);
            }
            if (FindUint32Attribute(attributes, legacyattr::kStyleSunken, &value)) face->SetSunken(value == 1);
            int32_t x, y;
            if (FindPairAttribute(attributes, kScrollbarGuttersAttribute, &x, &y)) face->SetScrollbarGutters(x, y);
            if (FindPairAttribute(attributes, legacyattr::kGutters, &x, &y)) face->SetGutters(x, y);
            if (FindUint32Attribute(attributes, kDropAttribute, &value)) face->SetDrop(value == 1);
        } else {
            const uint32_t count = attributes->Size();
            for (uint32_t i = 0; i < count; ++i) {
                const cGZScriptAttribute &attribute = attributes->mBegin[i];
                if (attribute.mId != kListElementAttribute) continue;
                cIGZVariant *const item = reinterpret_cast<cIGZVariant *>(attribute.mValue);
                if (item->GetType() != kTextVariantType) continue;
                LegacyText element;
                item->GetValString(*element.mText.AsInterface(), 0, kTextSeparator);
                face->AddElement(&element.mText, kAppendPosition, 0);
            }
            if (FindUint32Attribute(attributes, kSortAttribute, &value) && value == 1) {
                face->SortStep1();
                face->SortStep2();
            }
            if (FindUint32Attribute(attributes, kInitSelectionAttribute, &value)) face->SetInitialSelection(value, 1);
            if (FindUint32Attribute(attributes, kLineVisibleAttribute, &value)) face->SetLineVisible(value);
            if (FindUint32Attribute(attributes, kLineOnTopAttribute, &value)) face->SetLineOnTop(value);
            if (HasAttribute(attributes, kAutofitAttribute)) {
                window->SetProperty(kAutofitProperty, new cRZVariant(true));
                face->FitStep1();
                face->FitStep2();
                face->FitStep3();
            } else {
                window->SetProperty(kAutofitProperty, new cRZVariant(false));
            }
        }
    }
    if (face) face->Release();
    return true;
}

namespace {

// ---- GZWinLineInput's attributes (RegisterLineInput 0x860f01) ----
constexpr uint32_t kFilledAttribute = 0xe00;
constexpr uint32_t kOutline3dAttribute = 0xe01;
constexpr uint32_t kOutline3dExAttribute = 0xe02;
constexpr uint32_t kBorderColorAttribute = 0xe03; // colorlt, colorrb, colorltinside, colorrbinside: consecutive
constexpr uint32_t kBorderColorCount = 4;
constexpr uint32_t kHighlightFillAttribute = 0xe07;
constexpr uint32_t kHighlightAlphaAttribute = 0xe08;
constexpr uint32_t kCaretPositionAttribute = 0xe09;
constexpr uint32_t kCharLimitAttribute = 0xe0a;
constexpr uint32_t kSelectionAttribute = 0xe0b;
constexpr uint32_t kStyleNormal = 0xf00a;
constexpr uint32_t kOutlineStyleNormal = 1; // the face's outline style words
constexpr uint32_t kOutlineStyle3d = 2;
constexpr uint32_t kOutlineStyle3dEx = 3;
constexpr uint32_t kMaxHighlightAlpha = 0xff;
constexpr char kGutterSeparator = ',';

} // namespace

bool __cdecl ApplyLineInputProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply)
{
    cIGZWinLineInput *face = nullptr;
    if (window->QueryInterface(cIGZWinLineInput::kIID, reinterpret_cast<void **>(&face))) {
        uint32_t value;
        if (apply) {
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

            uint8_t red, green, blue;
            for (uint32_t index = 0; index < kListColorCount; ++index) {
                if (FindColorAttribute(attributes, kListColorAttribute + index, &red, &green, &blue))
                    face->SetColor(index, NativeColor(red, green, blue));
            }
            for (uint32_t index = 0; index < kBorderColorCount; ++index) {
                if (FindColorAttribute(attributes, kBorderColorAttribute + index, &red, &green, &blue))
                    face->SetBorderColor(index, NativeColor(red, green, blue));
            }
            if (FindColorAttribute(attributes, kHighlightFillAttribute, &red, &green, &blue))
                face->SetHighlightFillColor(NativeColor(red, green, blue));
            if (FindUint32Attribute(attributes, kHighlightAlphaAttribute, &value))
                face->SetHighlightFillAlpha(static_cast<float>(std::min(value, kMaxHighlightAlpha)) / 255.0f);
            if (FindUint32Attribute(attributes, kFilledAttribute, &value)) face->SetFilled(value != 0);
            if (FindUint32Attribute(attributes, legacyattr::kStyle, &value)) {
                if (value == kOutline3dAttribute)
                    face->SetOutlineStyle(kOutlineStyle3d);
                else if (value == kOutline3dExAttribute)
                    face->SetOutlineStyle(kOutlineStyle3dEx);
                else if (value == kStyleNormal)
                    face->SetOutlineStyle(kOutlineStyleNormal);
            }

            LegacyText gutters;
            if (FindTextAttribute(attributes, legacyattr::kGutters, &gutters.mText)) {
                // One comma: "(a,b)" gives (a, b, a, b); three: "(a,b,c,d)"; any other count does nothing. A text that does
                // not give its numbers keeps stack values in the original, which the tests do not make.
                const uint32_t commas = CountCharacters(gutters.Chars(), gutters.mText.Strlen(), kGutterSeparator);
                int32_t a = 0, b = 0, c = 0, d = 0;
                if (commas == 1) {
                    std::sscanf(gutters.Chars(), "(%d,%d)", &a, &b);
                    face->SetGutterRect(a, b, a, b);
                } else if (commas == 3) {
                    std::sscanf(gutters.Chars(), "(%d,%d,%d,%d)", &a, &b, &c, &d);
                    face->SetGutterRect(a, b, c, d);
                }
            }
        } else {
            if (FindUint32Attribute(attributes, kCaretPositionAttribute, &value)) face->SetCaretPosition(value);
            if (FindUint32Attribute(attributes, kEditableAttribute, &value)) face->SetEditable(value != 0);
            if (FindUint32Attribute(attributes, kCharLimitAttribute, &value)) face->SetCharLimit(value);
            int32_t first, second;
            if (FindPairAttribute(attributes, kSelectionAttribute, &first, &second))
                face->SetSelection(first, second - first);
        }
    }
    if (face) face->Release();
    return true;
}

namespace {

constexpr uint32_t kFoldersKeyProperty = 0x2b4e9920; // the folders' image key (16 bytes)

} // namespace

bool __cdecl ApplyFoldersProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply)
{
    cIGZWinFolders *face = nullptr;
    if (window->QueryInterface(cIGZWinFolders::kIID, reinterpret_cast<void **>(&face))) {
        if (apply) {
            LegacyText fontText;
            uint32_t group = 0; // the original reads the image's group into the font's variable
            if (FindUint32Attribute(attributes, legacyattr::kFont, &group)) {
                face->SetFont(group);
                SetUint32WindowProperty(window, group, 0);
            } else if (FindTextAttribute(attributes, legacyattr::kFont, &fontText.mText)) {
                SetTextWindowProperty(window, &fontText.mText, 0);
            }

            LegacyText text;
            if (FindTextAttribute(attributes, legacyattr::kImage, &text.mText)) {
                uint32_t instance = 0;
                std::sscanf(text.Chars(), "{%x,%x}", &group, &instance);
                cIGZLegacyImage *const image = LoadImageResource(group, instance);
                if (image) face->SetImage(image);
                const cGZPersistResourceKey key{kImageResourceType, group, instance, 0};
                SetResourceKeyProperty(window, kFoldersKeyProperty, &key);
                SetImagePairProperty(window, group, instance, 0);
                if (image) image->Release();
            }
        } else {
            face->ClearElements();
            const uint32_t count = attributes->Size();
            for (uint32_t i = 0; i < count; ++i) {
                const cGZScriptAttribute &attribute = attributes->mBegin[i];
                if (attribute.mId != kListElementAttribute) continue;
                cIGZVariant *const value = reinterpret_cast<cIGZVariant *>(attribute.mValue);
                if (value->GetType() != kTextVariantType) continue;
                LegacyText element;
                value->GetValString(*element.mText.AsInterface(), 0, kTextSeparator);
                face->AddElement(&element.mText, 0);
            }
        }
    }
    if (face) face->Release();
    return true;
}

