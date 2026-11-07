// The window classes' handlers of cGZLegacyControl, F6 part 6 (lane MM): the tree view, the last of the list. Built for size, as
// the game's. src/gz/cGZLegacyHandlers.h has the face and the handler's notes.
#include "gz/cGZLegacyHandlersInternal.h"
#include "gz/cGZLegacyTreeTables.h"

#include "game/game.h"
#include "ifaces/cIGZWin.h"
#include "rz/cIGZVariant.h"
#include "rz/cRZIniFile.h" // cRZIniStringHelpers::SplitOff
#include "rz/cRZString.h"
#include "rz/cRZVariant.h"

#include <cstdio>
#include <list>
#include <map>
#include <new>
#include <string>
#include <utility>
#include <vector>

#pragma warning(disable : 4996) // sscanf, as the game calls it

using namespace legacyhandlers;
using namespace legacy_tree;

namespace {

// ---- GZWinTreeView's attributes (RegisterTreeView 0x863115) ----
constexpr uint32_t kDisplayRootAttribute = 0x1401;
constexpr uint32_t kDisplayNodeIconsAttribute = 0x1402;
constexpr uint32_t kDisplayTreeLinesAttribute = 0x1403;
constexpr uint32_t kMultiSelectAttribute = 0x1404;
constexpr uint32_t kIconGutterAttribute = 0x1405;
constexpr uint32_t kValue1336Attribute = 0x1336;
constexpr uint32_t kMinMaxBoxSizeAttribute = 0x1406;
constexpr uint32_t kColor133fAttribute = 0x133f;
constexpr uint32_t kRootTextAttribute = 0x1407;
constexpr uint32_t kRootImageAttribute = 0x1408; // 0x1408 .. 0x140b: consecutive
constexpr uint32_t kRootColorAttribute = 0x140c; // 0x140c .. 0x140f: consecutive
constexpr uint32_t kRootFontAttribute = 0x1410;
constexpr uint32_t kRootExpandedAttribute = 0x1411;
constexpr uint32_t kRootSelectedAttribute = 0x1412;
constexpr uint32_t kNodeAttribute = 0x1102;
constexpr uint32_t kImageAttribute = 0xf016;
constexpr uint32_t kSlotCount = 4; // the images and the colours of a node: 0 .. 3

constexpr char kSeparator[] = ":";
constexpr char kRootName[] = "Root";
constexpr char kYes[] = "yes";
constexpr char kFirst[] = "first";
constexpr char kLast[] = "last";
constexpr char kSorted[] = "sorted";
constexpr int32_t kPositionFirst = -2;
constexpr int32_t kPositionLast = -3;
constexpr int32_t kPositionSorted = -4;
constexpr uint32_t kRootNode = 0;

struct FlagAttribute {
    uint32_t attribute;
    uint32_t mask;
};
constexpr FlagAttribute kFlagAttributes[] = {
    {kDisplayRootAttribute, cIGZWinTreeView::kFlagRoot},
    {kDisplayNodeIconsAttribute, cIGZWinTreeView::kFlagNodeIcons},
    {kDisplayTreeLinesAttribute, cIGZWinTreeView::kFlagTreeLines},
    {kMultiSelectAttribute, cIGZWinTreeView::kFlagMultiSelect},
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

// The text of `rest` up to the first separator, which leaves `rest`.
std::string TakePiece(LegacyText &rest)
{
    SplitPiece split;
    reinterpret_cast<cRZIniStringHelpers *>(&rest.mText)->SplitOff(&split.mText, kSeparator);
    return split.mText.mString;
}

// A table kept on the window as a number.
void KeepTable(ifaces::cIGZWin *window, uint32_t property, const void *table)
{
    window->SetProperty(property, new cRZVariant(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(table))));
}

// The image number a name has in the table; `fallback` for a name that has none.
uint32_t ImageNumber(const NameNumberMap &images, const std::string &name, uint32_t fallback)
{
    const TreeName key(name);
    const auto found = images.find(key);
    return found == images.end() ? fallback : found->second;
}

// One node's description (0x1102): kept until its parent has been made.
struct TreeRecord {
    std::string mName;
    LegacyText mText;
    std::string mParent;
    int32_t mPosition = 0;
    uint32_t mImages[kSlotCount] = {};
    uint32_t mColors[kSlotCount] = {};
    uint32_t mFont = 0;
    bool mExpanded = false;
    bool mSelected = false;
};

// The images (0xf016): "name:{group,instance}:(left,top,right,bottom)", the rectangle optional (all zero: the image's own).
void LoadTreeImages(const cGZScriptAttributes *attributes, cIGZWinTreeView *face, NameNumberMap *numbers, NumberNameMap *names,
                    NameResourceMap *resources)
{
    LegacyText text;
    const uint32_t count = attributes->Size();
    for (uint32_t i = 0; i < count; ++i) {
        const cGZScriptAttribute &attribute = attributes->mBegin[i];
        if (attribute.mId != kImageAttribute) continue;
        cIGZVariant *const item = reinterpret_cast<cIGZVariant *>(attribute.mValue);
        if (item->GetType() != kTextVariantType) continue;
        item->GetValString(*text.mText.AsInterface(), 0, kTextSeparator);
        const std::string name = TakePiece(text);
        const std::string spec = TakePiece(text);
        const std::string rest = text.mText.mString;

        uint32_t group = 0, instance = 0;
        cIGZLegacyImage *image = nullptr;
        if (std::sscanf(spec.c_str(), "{%x,%x}", &group, &instance) == 2) image = LoadImageResource(group, instance);
        resources->try_emplace(TreeName(name), ImageResource{group, instance});

        int32_t rect[4] = {};
        std::sscanf(rest.c_str(), "(%d,%d,%d,%d)", &rect[0], &rect[1], &rect[2], &rect[3]);
        if (!image) continue;
        const bool ownRect = rect[0] == 0 && rect[1] == 0 && rect[2] == 0 && rect[3] == 0;
        const uint32_t number = face->AddImage(image, ownRect ? image->GetRect() : rect);
        numbers->try_emplace(TreeName(name), number);
        names->try_emplace(static_cast<int32_t>(number), TreeName(name));
        image->Release();
    }
}

// The root node's own attributes.
void ApplyTreeRoot(ifaces::cIGZWin *window, const cGZScriptAttributes *attributes, cIGZWinTreeView *face,
                   const NameNumberMap &images)
{
    LegacyText text;
    if (FindTextAttribute(attributes, kRootTextAttribute, &text.mText)) face->SetNodeText(kRootNode, &text.mText);
    for (uint32_t index = 0; index < kSlotCount; ++index) {
        if (FindTextAttribute(attributes, kRootImageAttribute + index, &text.mText))
            face->SetNodeImage(kRootNode, index, ImageNumber(images, text.mText.mString, index));
    }
    uint8_t red, green, blue;
    for (uint32_t index = 0; index < kSlotCount; ++index) {
        if (FindColorAttribute(attributes, kRootColorAttribute + index, &red, &green, &blue))
            face->SetNodeColor(kRootNode, index, NativeColor(red, green, blue));
    }

    LegacyText fontText;
    uint32_t value;
    if (FindUint32Attribute(attributes, kRootFontAttribute, &value)) {
        face->SetNodeFont(kRootNode, UIDefaults()->GetFont(value));
        SetUint32WindowProperty(window, value, 0);
    } else if (FindTextAttribute(attributes, kRootFontAttribute, &fontText.mText)) {
        SetTextWindowProperty(window, &fontText.mText, 0);
    }
    if (FindUint32Attribute(attributes, kRootExpandedAttribute, &value)) face->SetNodeExpanded(kRootNode, value == 1);
    if (FindUint32Attribute(attributes, kRootSelectedAttribute, &value) && value == 1) face->SetNodeSelected(kRootNode, 0);
    face->SetBaseColor(NativeColor(0, 0, 0), 1);
}

// The nodes (0x1102): "name:text:parent:position:image0:image1:image2:image3:(color0):(color1):(color2):(color3):font:
// expanded:selected". The font names (with their numbers) go to `fonts`.
void ReadTreeNodes(const cGZScriptAttributes *attributes, const NameNumberMap &images, NameNumberMap *fonts,
                   std::list<TreeRecord> *records)
{
    LegacyText text;
    // ORIGINAL-BUG: a position that is none of the three words keeps what the record's place on the stack held: the previous
    // node's, and for the first stack leftovers; zero here.
    int32_t position = 0;
    const uint32_t count = attributes->Size();
    for (uint32_t i = 0; i < count; ++i) {
        const cGZScriptAttribute &attribute = attributes->mBegin[i];
        if (attribute.mId != kNodeAttribute) continue;
        cIGZVariant *const item = reinterpret_cast<cIGZVariant *>(attribute.mValue);
        if (item->GetType() != kTextVariantType) continue;
        item->GetValString(*text.mText.AsInterface(), 0, kTextSeparator);

        TreeRecord &record = records->emplace_back();
        record.mName = TakePiece(text);
        record.mText.mText.mString = TakePiece(text);
        record.mParent = TakePiece(text);
        const std::string where = TakePiece(text);
        if (where == kFirst) position = kPositionFirst;
        if (where == kLast) position = kPositionLast;
        if (where == kSorted) position = kPositionSorted;
        record.mPosition = position;
        for (uint32_t index = 0; index < kSlotCount; ++index) record.mImages[index] = ImageNumber(images, TakePiece(text), index);
        for (uint32_t index = 0; index < kSlotCount; ++index) {
            const std::string piece = TakePiece(text);
            int32_t color[3] = {};
            std::sscanf(piece.c_str(), "(%d,%d,%d)", &color[0], &color[1], &color[2]);
            record.mColors[index] = NativeColor(static_cast<uint8_t>(color[0]), static_cast<uint8_t>(color[1]),
                                                static_cast<uint8_t>(color[2]));
        }
        LegacyText fontName;
        fontName.mText.mString = TakePiece(text);
        const uint32_t fontId = UIDefaults()->FindFontId(&fontName.mText);
        record.mFont = UIDefaults()->GetFont(fontId);
        fonts->try_emplace(TreeName(record.mName), fontId);
        record.mExpanded = TakePiece(text) == kYes;
        record.mSelected = text.mText.mString == kYes;
    }
}

// Makes the nodes depth first from "Root": a node's children in the records' order, each one's own children before the next.
void BuildTreeNodes(cIGZWinTreeView *face, std::list<TreeRecord> *records, NameNumberMap *numbers, NumberNameMap *names)
{
    std::vector<std::pair<std::string, uint32_t>> pending;
    pending.emplace_back(kRootName, kRootNode);
    while (!pending.empty()) {
        const std::string parentName = pending.back().first;
        const uint32_t parentNode = pending.back().second;
        auto child = records->begin();
        while (child != records->end() && child->mParent != parentName) ++child;
        if (child == records->end()) {
            pending.pop_back();
            continue;
        }
        const TreeRecord &record = *child;
        const uint32_t node = face->AddNode(parentNode, record.mPosition, &record.mText.mText, 0);
        for (uint32_t index = 0; index < kSlotCount; ++index) face->SetNodeImage(node, index, record.mImages[index]);
        face->SetNodeFont(node, record.mFont);
        for (uint32_t index = 0; index < kSlotCount; ++index) face->SetNodeColor(node, index, record.mColors[index]);
        face->SetNodeExpanded(node, record.mExpanded);
        if (record.mSelected) face->SetNodeSelected(node, 0);
        numbers->try_emplace(TreeName(record.mName), node);
        names->try_emplace(static_cast<int32_t>(node), TreeName(record.mName));
        pending.emplace_back(record.mName, node);
        records->erase(child);
    }
}

void ApplyTreeFlags(const cGZScriptAttributes *attributes, cIGZWinTreeView *face)
{
    uint32_t value;
    for (const FlagAttribute &flag : kFlagAttributes) {
        if (FindUint32Attribute(attributes, flag.attribute, &value)) face->SetFlag(flag.mask, value != 0);
    }
    if (FindUint32Attribute(attributes, kIconGutterAttribute, &value)) face->SetIconGutter(value);
    if (FindUint32Attribute(attributes, kValue1336Attribute, &value)) face->SetValue1336(value);
    if (FindUint32Attribute(attributes, kMinMaxBoxSizeAttribute, &value)) face->SetMinMaxBoxSize(value);
    uint8_t red, green, blue;
    if (FindColorAttribute(attributes, kColor133fAttribute, &red, &green, &blue))
        face->SetColor133f(NativeColor(red, green, blue));
    if (FindColorAttribute(attributes, kHighlightColorAttribute, &red, &green, &blue))
        face->SetHighlightColor(NativeColor(red, green, blue));
}

void BuildTree(ifaces::cIGZWin *window, const cGZScriptAttributes *attributes, cIGZWinTreeView *face)
{
    auto *const imageNumbers = new NameNumberMap;
    auto *const numberImages = new NumberNameMap;
    auto *const imageResources = new NameResourceMap;
    LoadTreeImages(attributes, face, imageNumbers, numberImages, imageResources);
    KeepTable(window, kImageNumbersProperty, imageNumbers);
    KeepTable(window, kNumberImagesProperty, numberImages);
    KeepTable(window, kImageResourcesProperty, imageResources);

    ApplyTreeRoot(window, attributes, face, *imageNumbers);

    auto *const nodeFonts = new NameNumberMap;
    std::list<TreeRecord> records;
    ReadTreeNodes(attributes, *imageNumbers, nodeFonts, &records);
    KeepTable(window, kNodeFontsProperty, nodeFonts);

    auto *const nodeNumbers = new NameNumberMap;
    auto *const numberNodes = new NumberNameMap;
    BuildTreeNodes(face, &records, nodeNumbers, numberNodes);
    KeepTable(window, kNodeNumbersProperty, nodeNumbers);
    KeepTable(window, kNumberNodesProperty, numberNodes);
}

} // namespace

bool __cdecl ApplyTreeViewProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply)
{
    cIGZWinTreeView *face = nullptr;
    if (window->QueryInterface(cIGZWinTreeView::kIID, reinterpret_cast<void **>(&face))) {
        if (apply)
            ApplyTreeFlags(attributes, face);
        else
            BuildTree(window, attributes, face);
    }
    if (face) face->Release();
    return true;
}
