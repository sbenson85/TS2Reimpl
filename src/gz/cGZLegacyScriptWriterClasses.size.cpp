// The legacy script writer's class writers (lane FI, foundations): src/gz/cGZLegacyScriptWriter.h. Built for size, as
// the game's.
#include "gz/cGZLegacyScriptWriter.h"

#include "gz/cGZLegacyHandlers.h" // the window classes' faces
#include "gz/cGZLegacyScriptWriterInternal.h"
#include "ifaces/cIGZWin.h"

#include <cstdint>
#include <new>

namespace {

constexpr uint32_t kBitmapImageProperty = 0xab428c5d;
constexpr uint32_t kFoldersImageProperty = 0x2b343bec;
constexpr uint32_t kScrollbarImageProperty = 0x6b4bcbd4;
constexpr uint32_t kScrollbar2ArrowsProperty = 0x0bfa11d7;
constexpr uint32_t kScrollbar2ContainerProperty = 0x6bfa11e1;
constexpr uint32_t kScrollbar2ThumbProperty = 0x0bfa11e2;

using legacy_script::AsWin;
using legacy_script::FlagAttribute;
using legacy_script::Of;
using legacy_script::Text;

} // namespace

void __cdecl WriteBitmapAttributes(cIGZWinBitmap *face, cRZString *out)
{
    uint32_t key[4] = {};
    if (GetKeyProperty(AsWin(face->GetWindow()), kBitmapImageProperty, key))
        AppendKeyAttribute(out, Of(Text("image")), key[1], key[2]);
    if (face->IsFlagSet(cIGZWinBitmap::kFlagImageRect)) {
        int32_t rect[4];
        face->GetImageRect(rect);
        AppendRectAttribute(out, Of(Text("imagerect")), rect[0], rect[1], rect[2], rect[3]);
    }
    FlagAttribute(out, "notify", face->IsFlagSet(cIGZWinBitmap::kFlagNotify));
    FlagAttribute(out, "transparentbkg", face->IsFlagSet(cIGZWinBitmap::kFlagTransparentBack));
    FlagAttribute(out, "edgeimage", face->IsFlagSet(cIGZWinBitmap::kFlagEdgeImage));
    bool enabled;
    float alpha;
    face->GetAlpha(&enabled, &alpha);
    if (enabled) {
        const float scaled = alpha * 255.0f;
        AppendIntAttribute(out, Of(Text("alpha")), uint8_t(int64_t(scaled))); // the low byte of the truncated value
    }
}

void __cdecl WriteTextTickerAttributes(cIGZWinTextTicker *face, cRZString *out)
{
    LegacyText font;
    if (FormatFontProperty(AsWin(face->GetWindow()), font.mText.AsInterface(), 0))
        AppendTextAttribute(out, Of(Text("font")), &font.mText);
    const int32_t speed = int32_t(face->GetSpeed());
    AppendIntAttribute(out, Of(Text("speed")), speed);
    const int32_t frequency = int32_t(face->GetFrequency());
    AppendIntAttribute(out, Of(Text("frequency")), frequency);
}

namespace {

// A style bit and the word a writer names it with.
struct StyleWord {
    uint32_t bit;
    const char *word;
};

// A "style=<word> " attribute for each bit of `style` set, in the table's order.
template <size_t N> void StyleAttributes(cRZString *out, uint32_t style, const StyleWord (&table)[N])
{
    for (const StyleWord &s : table)
        if (style & s.bit) AppendTextAttribute(out, Of(Text("style")), Of(Text(s.word)));
}

} // namespace

void __cdecl WriteFlatRectAttributes(cIGZWinFlatRect *face, cRZString *out)
{
    static const char *const kColorNames[] = {"colorleft", "colortop", "colorright", "colorbottom"};
    for (uint32_t side = 0; side < 4; ++side) {
        uint8_t red, green, blue;
        face->GetColor(side, &red, &green, &blue);
        AppendColorAttribute(out, Of(Text(kColorNames[side])), red, green, blue);
    }
    static const StyleWord kStyles[] = {
        {cIGZWinFlatRect::kBorderLeft, "left"},     {cIGZWinFlatRect::kBorderTop, "top"},
        {cIGZWinFlatRect::kBorderRight, "right"},   {cIGZWinFlatRect::kBorderBottom, "bottom"},
        {cIGZWinFlatRect::kRaised, "raised"},       {cIGZWinFlatRect::kSunken, "sunken"},
        {cIGZWinFlatRect::kNoFill, "nofill"},
    };
    StyleAttributes(out, face->GetStyle(), kStyles);
}

void __cdecl WriteOutlineAttributes(cIGZWinOutline *face, cRZString *out)
{
    static const char *const kColorNames[] = {"colordark", "colorlight"}; // colours 1 and 2
    for (uint32_t i = 0; i < 2; ++i) {
        uint8_t red, green, blue;
        face->GetColor(i + 1, &red, &green, &blue);
        AppendColorAttribute(out, Of(Text(kColorNames[i])), red, green, blue);
    }
    static const StyleWord kStyles[] = {
        {cIGZWinOutline::kStyleFlat, "flat"},
        {cIGZWinOutline::kStyleSunken, "sunken"},
        {cIGZWinOutline::kStyleRaised, "raised"},
        {cIGZWinOutline::kStyleFill, "fill"},
    };
    StyleAttributes(out, face->GetStyle(), kStyles);
    // The nodes are copied out into a buffer of the count's pairs; a size past 32 bits asks for everything, which fails,
    // and a failed allocation or copy writes no node.
    const uint32_t count = face->GetNodeCount();
    const size_t bytes = count > SIZE_MAX / 8 ? SIZE_MAX : size_t(count) * 8;
    auto *pairs = static_cast<int32_t *>(::operator new[](bytes, std::nothrow));
    if (pairs && face->GetNodes(pairs)) {
        for (uint32_t i = 0; i < count; ++i)
            AppendPairAttribute(out, Of(Text("node")), pairs[2 * i], pairs[2 * i + 1]);
    }
    ::operator delete[](pairs);
}

void __cdecl WriteFoldersAttributes(cIGZWinFolders *face, cRZString *out)
{
    LegacyText font;
    uint32_t fontId;
    if (face->GetFont(&fontId) && FormatFontId(fontId, font.mText.AsInterface()))
        AppendTextAttribute(out, Of(Text("font")), &font.mText);
    uint32_t key[4] = {};
    if (GetKeyProperty(AsWin(face->GetWindow()), kFoldersImageProperty, key))
        AppendKeyAttribute(out, Of(Text("image")), key[1], key[2]);
    // One text for all the elements: an element the face does not write repeats the one before it.
    LegacyText element;
    for (int32_t i = 0; face->GetElement(i, element.mText.AsInterface()); ++i)
        AppendQuotedAttribute(out, Of(Text("listelement")), &element.mText);
}

void __cdecl WriteScrollbarAttributes(cIGZWinScrollbar *face, cRZString *out)
{
    int32_t low, high;
    if (face->GetRange(&low, &high)) AppendPairAttribute(out, Of(Text("minmaxvalue")), low, high);
    switch (face->GetDirection()) {
    case cIGZWinScrollbar::kVertical: AppendTextAttribute(out, Of(Text("direction")), Of(Text("vertical"))); break;
    case cIGZWinScrollbar::kHorizontal: AppendTextAttribute(out, Of(Text("direction")), Of(Text("horizontal"))); break;
    default: break; // any other direction is not written
    }
    AppendIntAttribute(out, Of(Text("pagesize")), int32_t(face->GetPageSize()));
    AppendIntAttribute(out, Of(Text("linesize")), int32_t(face->GetLineSize()));
    int32_t lines, pages;
    face->GetLinePageCount(&lines, &pages);
    AppendPairAttribute(out, Of(Text("linepagecount")), lines, pages);
    uint32_t key[4] = {};
    if (GetKeyProperty(AsWin(face->GetWindow()), kScrollbarImageProperty, key))
        AppendKeyAttribute(out, Of(Text("image")), key[1], key[2]);
    AppendIntAttribute(out, Of(Text("initvalue")), face->GetValue());
}

void __cdecl WriteScrollbar2Attributes(cIGZWinScrollbar2 *face, cRZString *out)
{
    uint32_t vertical;
    face->GetVertical(&vertical);
    AppendTextAttribute(out, Of(Text("orientation")), Of(Text(vertical ? "vertical" : "horizontal")));
    // One value for all the getters, each writing it when it answers true.
    int32_t value;
    using Getter = bool (cIGZWinScrollbar2::*)(int32_t *);
    static const struct {
        Getter get;
        const char *name;
    } kValues[] = {
        {&cIGZWinScrollbar2::GetMinValue, "minvalue"},         {&cIGZWinScrollbar2::GetMaxValue, "maxvalue"},
        {&cIGZWinScrollbar2::GetVisibleRange, "visiblerange"}, {&cIGZWinScrollbar2::GetPageOverlap, "pageoverlap"},
        {&cIGZWinScrollbar2::GetIncrement, "increment"},       {&cIGZWinScrollbar2::GetValue, "value"},
    };
    for (const auto &v : kValues)
        if ((face->*v.get)(&value)) AppendIntAttribute(out, Of(Text(v.name)), value);
    // One key for the three images, cleared once: a property that is not a key leaves it.
    static const struct {
        uint32_t property;
        const char *name;
    } kImages[] = {
        {kScrollbar2ArrowsProperty, "arrowsimage"},
        {kScrollbar2ContainerProperty, "containerimage"},
        {kScrollbar2ThumbProperty, "thumbimage"},
    };
    uint32_t key[4] = {};
    for (const auto &image : kImages)
        if (GetKeyProperty(AsWin(face->GetWindow()), image.property, key))
            AppendKey3Attribute(out, Of(Text(image.name)), key);
}
