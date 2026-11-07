// The legacy script writer's TreeView writer (lane WD, foundations): src/gz/cGZLegacyScriptWriter.h. Built for size, as the
// game's.
#include "gz/cGZLegacyScriptWriter.h"

#include "gz/cGZLegacyHandlers.h"
#include "gz/cGZLegacyHandlersInternal.h" // GetGraphicSystem, UIDefaults
#include "gz/cGZLegacyScriptWriterInternal.h"
#include "gz/cGZLegacyTreeTables.h"
#include "ifaces/cIGZWin.h"
#include "rz/cIGZVariant.h"

#include <cstdint>

namespace {

using namespace legacy_tree;
using legacy_script::AsWin;
using legacy_script::FlagAttribute;
using legacy_script::Of;
using legacy_script::Text;

// A UI defaults font entry (the defaults' FindFontEntry answer): slot 5 gives its name.
class cIGZUIFontEntry {
public:
    virtual void vf0() = 0;
    virtual void vf1() = 0;
    virtual void vf2() = 0;
    virtual void vf3() = 0;
    virtual void vf4() = 0;
    virtual const char *GetName() = 0; // 5
};

constexpr uint32_t kFlagDisplayRoot = 2;
constexpr uint32_t kFlagDisplayNodeIcons = 4;
constexpr uint32_t kFlagDisplayTreeLines = 8;
constexpr uint32_t kFlagMultiSelect = 0x10;
constexpr uint32_t kSlotCount = 4;

// A table of the window's: the property's number as the address of the map, null when the window has none or it is not a number.
template <class Map> Map *Table(cIGZWinTreeView *face, uint32_t property)
{
    cIGZVariant *variant = nullptr;
    uintptr_t address = 0;
    if (AsWin(face->GetWindow())->GetProperty(property, &variant)) {
        uint32_t value;
        if (variant->GetValUint32(&value)) address = value;
        variant->Release();
    }
    return reinterpret_cast<Map *>(address);
}

// "<name>=(r,g,b) " of a native colour, through the graphics system; the bytes are the caller's.
void NativeColor(cRZString *out, const char *name, uint32_t native, uint8_t *red, uint8_t *green, uint8_t *blue)
{
    GetGraphicSystem()->ConvertNativeToRGB(native, red, green, blue);
    AppendColorAttribute(out, Of(Text(name)), *red, *green, *blue);
}

// The expanded and selected answers are a byte the game compares with exactly 1.
const char *YesNo(uint8_t answer) { return answer == 1 ? "yes" : "no"; }

} // namespace

void __cdecl WriteTreeViewAttributes(cIGZWinTreeView *face, cRZString *out)
{
    FlagAttribute(out, "displayroot", face->IsFlagSet(kFlagDisplayRoot));
    FlagAttribute(out, "displaynodeicons", face->IsFlagSet(kFlagDisplayNodeIcons));
    FlagAttribute(out, "displaytreelines", face->IsFlagSet(kFlagDisplayTreeLines));
    FlagAttribute(out, "multiselect", face->IsFlagSet(kFlagMultiSelect));

    AppendIntAttribute(out, Of(Text("icongutter")), face->GetIconGutter());
    AppendIntAttribute(out, Of(Text("rowheight")), face->GetRowHeight());
    AppendIntAttribute(out, Of(Text("minmaxboxsize")), face->GetMinMaxBoxSize());

    // The bytes of every colour but the nodes' are one set.
    uint8_t red, green, blue;
    NativeColor(out, "backgroundcolor", face->GetBackgroundColor(), &red, &green, &blue);
    NativeColor(out, "highlightcolor", face->GetHighlightColor(), &red, &green, &blue);

    NameNumberMap *imageNumbers = Table<NameNumberMap>(face, kImageNumbersProperty);
    NumberNameMap *numberImages = Table<NumberNameMap>(face, kNumberImagesProperty);
    NameResourceMap *imageResources = Table<NameResourceMap>(face, kImageResourcesProperty);

    if (imageNumbers) {
        for (const auto &entry : *imageNumbers) {
            uint32_t group = 0, instance = 0;
            // No guard for a missing resource table: the applier always makes all six (the original would fault).
            const auto found = imageResources->find(entry.first);
            if (found != imageResources->end()) {
                group = found->second.mGroup;
                instance = found->second.mInstance;
            }
            cIGZLegacyImage *image;
            int32_t rect[4] = {0, 0, 0, 0};
            face->GetImage(entry.second, &image, rect);
            LegacyText line;
            line.mText.Sprintf("%s:{%08x,%08x}:(%d,%d,%d,%d)", entry.first.mString.c_str(), group, instance, rect[0], rect[1],
                               rect[2], rect[3]);
            AppendQuotedAttribute(out, Of(Text("image")), &line.mText);
        }
    }

    // The root.
    {
        LegacyText text;
        text.mText.Assign(*face->GetNodeText(0));
        AppendQuotedAttribute(out, Of(Text("roottext")), &text.mText);
    }
    static const char *const kRootIcons[kSlotCount] = {"rooticon", "rooticonselected", "rooticonexpanded",
                                                       "rooticonexpandedselected"};
    if (numberImages) {
        for (uint32_t i = 0; i < kSlotCount; ++i) {
            const auto found = numberImages->find(int32_t(face->GetNodeImage(0, i)));
            if (found != numberImages->end())
                AppendQuotedAttribute(out, Of(Text(kRootIcons[i])), reinterpret_cast<const cRZString *>(&found->second));
        }
    }
    static const char *const kRootColors[kSlotCount] = {"rootcolorforeground", "rootcolorbackground",
                                                        "rootcolorforegroundhilited", "rootcolorbackgroundhilited"};
    for (uint32_t i = 0; i < kSlotCount; ++i) NativeColor(out, kRootColors[i], face->GetNodeColor(0, i), &red, &green, &blue);
    {
        LegacyText font;
        if (FormatFontProperty(AsWin(face->GetWindow()), font.mText.AsInterface(), 0))
            AppendTextAttribute(out, Of(Text("rootfont")), &font.mText);
    }
    {
        Text value(YesNo(face->IsNodeExpanded(0)));
        AppendTextAttribute(out, Of(Text("rootexpanded")), Of(value));
    }
    {
        Text value(YesNo(face->IsNodeSelected(0)));
        AppendTextAttribute(out, Of(Text("rootselected")), Of(value));
    }

    NameNumberMap *nodeFonts = Table<NameNumberMap>(face, kNodeFontsProperty);
    NameNumberMap *nodeNumbers = Table<NameNumberMap>(face, kNodeNumbersProperty);
    NumberNameMap *numberNodes = Table<NumberNameMap>(face, kNumberNodesProperty);
    if (!nodeNumbers) return;

    uint8_t colors[3 * kSlotCount]; // one set of bytes, kept from node to node: blue, green, red of each colour
    for (const auto &entry : *nodeNumbers) {
        const uint32_t node = entry.second;
        LegacyText text;
        text.mText.Assign(*face->GetNodeText(node));

        const char *parent = "Root";
        const uint32_t parentNumber = face->GetNodeParent(node);
        const auto parentFound = numberNodes->find(int32_t(parentNumber));
        if (parentFound != numberNodes->end()) parent = parentFound->second.mString.c_str();

        LegacyText images[kSlotCount];
        for (uint32_t i = 0; i < kSlotCount; ++i) images[i].mText.FromChar("Default");
        if (numberImages) {
            for (uint32_t i = 0; i < kSlotCount; ++i) {
                const auto found = numberImages->find(int32_t(face->GetNodeImage(node, i)));
                if (found != numberImages->end()) images[i].mText.FromChar(found->second.mString.c_str());
            }
        }
        for (uint32_t i = 0; i < kSlotCount; ++i) {
            const uint32_t native = face->GetNodeColor(node, i);
            GetGraphicSystem()->ConvertNativeToRGB(native, &colors[3 * i + 2], &colors[3 * i + 1], &colors[3 * i]);
        }

        LegacyText font;
        font.mText.FromChar("main9");
        if (nodeFonts) {
            const auto found = nodeFonts->find(entry.first);
            if (found != nodeFonts->end()) {
                if (auto *fontEntry = static_cast<cIGZUIFontEntry *>(UIDefaults()->FindFontEntry(found->second))) {
                    font.mText.FromChar("");
                    font.mText.Sprintf("%s", fontEntry->GetName());
                }
            }
        }
        const char *expanded = YesNo(face->IsNodeExpanded(node));
        const char *selected = YesNo(face->IsNodeSelected(node));
        const char *position = "Last";
        switch (face->GetNodePosition(node)) {
        case -4: position = "sorted"; break;
        case -3: position = "last"; break;
        case -2: position = "first"; break;
        default: break;
        }

        LegacyText line;
        line.mText.Sprintf("%s:%s:%s:%s:%s:%s:%s:%s:(%d,%d,%d):(%d,%d,%d):(%d,%d,%d):(%d,%d,%d):%s:%s:%s",
                           entry.first.mString.c_str(), text.Chars(), parent, position, images[0].Chars(), images[1].Chars(),
                           images[2].Chars(), images[3].Chars(), colors[2], colors[1], colors[0], colors[5], colors[4], colors[3],
                           colors[8], colors[7], colors[6], colors[11], colors[10], colors[9], font.Chars(), expanded, selected);
        AppendQuotedAttribute(out, Of(Text("node")), &line.mText);
    }
}
