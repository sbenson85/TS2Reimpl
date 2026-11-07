#pragma once
// legacy_win_faces: drawfx slot tables of the legacy window faces (cIGZWin* interfaces of the cGZWin* classes) and the
// services their handlers call, for difftests of cGZLegacyControl's handlers (tests/gz_legacy_handlers.cpp, lane MF) and
// of the legacy script writer (foundations FI..FL: the writer 0x87a9a9 and its per-class writers call the same faces).
// Moved out of gz_legacy_handlers.cpp by lane VY (2026-11-03), unchanged. Each slot gives the words it takes (from the
// callers' code), its name and its argument kinds (the letters are drawfx_core.h's); unnamed slots are ones no tested
// caller reaches. A lane that finds a slot's words or kinds wrong fixes it here, for
// both sides; a new face goes here too.
// fakes: cIGZWinBitmap, cIGZLegacyImage, cIGZGraphicSystem, cIGZUIDefaults, cIGZUIFontEntry, cIGZWinOptionItem, cIGZWinFlatRect, cIGZWinText, cIGZWinSlider, cIGZWinScrollbar, cIGZWinOutline, cIGZWinScrollbar2, cIGZWinFolders, cIGZWinCombo, cIGZWinListBox, cIGZWinLineInput, cIGZWinSpinner, cIGZWinTextTicker, cIGZWinButton, cIGZWinTextEdit, cIGZWinOptGrp, cIGZWinGen, cIGZWinFileBrowser, cIGZWinGrid, cIGZWinTreeView (getters and tables: TreeTables, TreeState, lane WD), the tooltip source (cIGZWin property 0xbaca6879) and its text set (lane WA)
#include "drawfx.h"

#include "gz/cGZLegacyControl.h"
#include "gz/cGZLegacyTreeTables.h"
#include "rz/cRZString.h"

#include <array>
#include <cstdint>
#include <iterator>
#include <memory>

namespace legacy_win_faces {

using drawfx::Ret;
using drawfx::Slot;

inline constexpr Slot kBitmapSlots[] = {
    {2, Ret::Int, true, "", ""},            // 0 QueryInterface
    {0, Ret::Int, true, "", ""},            // 1
    {0, Ret::Int, true, "", ""},            // 2
    {0, Ret::Int, true, "GetWindow", ""},   // 3
    {1, Ret::Int, true, "SetImage", "i"},   // 4
    {0, Ret::Int, true, "", ""},            // 5
    {2, Ret::Int, true, "SetAlpha", "bf"},  // 6 (a byte, a float)
    {2, Ret::Int, true, "GetAlpha", "aa"},  // 7 (out: a byte, a float)
    {1, Ret::Int, true, "SetImageRect", "r"}, // 8
    {1, Ret::Int, true, "GetImageRect", "a"}, // 9 (out: four words)
    {1, Ret::Int, true, "IsFlagSet", "u"},  // 10
    {2, Ret::Int, true, "SetFlag", "ub"},   // 11
};
inline constexpr drawfx::Iface kBitmapFace{"cIGZWinBitmap", 12, kBitmapSlots};

inline constexpr Slot kImageSlots[] = {
    {2, Ret::Int, true, "", ""},           // 0
    {0, Ret::Int, true, "", ""},           // 1
    {0, Ret::Int, true, "", ""},           // 2
    {0, Ret::Int, true, "", ""},           // 3
    {0, Ret::Int, true, "", ""},           // 4
    {0, Ret::Int, true, "", ""},           // 5
    {0, Ret::Int, true, "", ""},           // 6
    {0, Ret::Int, true, "", ""},           // 7
    {0, Ret::Int, true, "", ""},           // 8
    {0, Ret::Int, true, "GetWidth", ""},   // 9
    {0, Ret::Int, true, "GetHeight", ""},  // 10
    {0, Ret::Int, true, "", ""},           // 11
    {0, Ret::Int, true, "GetRect", ""},    // 12: the image's own rectangle (a pointer; the faces log what it points at)
};
inline constexpr drawfx::Iface kImageInterface{"cIGZLegacyImage", 13, kImageSlots};

constexpr std::array<Slot, 37> MakeGraphicsSlots()
{
    std::array<Slot, 37> slots{};
    for (Slot &s : slots) s = {0, Ret::Int, true, "", ""};
    slots[0].words = 2;
    slots[34] = {3, Ret::Int, true, "ConvertRGBToNative", "bbb"};
    slots[36] = {4, Ret::Int, true, "ConvertNativeToRGB", "uaaa"}; // the script writer's colours (GZWinText)
    return slots;
}
inline constexpr std::array<Slot, 37> kGraphicsSlots = MakeGraphicsSlots();
inline constexpr drawfx::Iface kGraphicsFace{"cIGZGraphicSystem", 37, kGraphicsSlots.data()};

constexpr std::array<Slot, 39> MakeDefaultsSlots()
{
    std::array<Slot, 39> slots{};
    for (Slot &s : slots) s = {0, Ret::Int, true, "", ""};
    slots[0].words = 2;
    slots[5] = {1, Ret::Int, true, "GetFont", "u"};
    slots[37] = {1, Ret::Int, true, "FindFontEntry", "u"}; // the script writer's font names (0x86e5f8)
    slots[38] = {1, Ret::Int, true, "FindFontId", "S"};
    return slots;
}
inline constexpr std::array<Slot, 39> kDefaultsSlots = MakeDefaultsSlots();
inline constexpr drawfx::Iface kDefaultsFace{"cIGZUIDefaults", 39, kDefaultsSlots.data()};

// A UI defaults font entry (the defaults' slot 37 hands it out; the script writer 0x86e5f8 reads its name): slot 5 gives
// the name (a const char *).
inline constexpr Slot kFontEntrySlots[] = {
    {2, Ret::Int, true, "", ""},        // 0
    {0, Ret::Int, true, "", ""},        // 1
    {0, Ret::Int, true, "", ""},        // 2
    {0, Ret::Int, true, "", ""},        // 3
    {0, Ret::Int, true, "", ""},        // 4
    {0, Ret::Int, true, "GetName", ""}, // 5
};
inline constexpr drawfx::Iface kFontEntryFace{"cIGZUIFontEntry", 6, kFontEntrySlots};

// An option of the option group (the group's slot 27 hands it out): slot 3 gives its window.
inline constexpr Slot kOptionItemSlots[] = {
    {2, Ret::Int, true, "", ""},          // 0 QueryInterface
    {0, Ret::Int, true, "", ""},          // 1
    {0, Ret::Int, true, "", ""},          // 2
    {0, Ret::Int, true, "GetWindow", ""}, // 3
};
inline constexpr drawfx::Iface kOptionItemFace{"cIGZWinOptionItem", 4, kOptionItemSlots};

inline constexpr Slot kFlatRectSlots[] = {
    {2, Ret::Int, true, "", ""},                // 0 QueryInterface
    {0, Ret::Int, true, "", ""},                // 1
    {0, Ret::Int, true, "", ""},                // 2
    {0, Ret::Int, true, "GetWindow", ""},       // 3
    {0, Ret::Int, true, "", ""},                // 4
    {0, Ret::Int, true, "", ""},                // 5
    {0, Ret::Int, true, "", ""},                // 6
    {0, Ret::Int, true, "", ""},                // 7
    {0, Ret::Int, true, "", ""},                // 8
    {0, Ret::Int, true, "", ""},                // 9
    {4, Ret::Int, true, "SetColor", "ubbb"},    // 10: the bytes are the low byte of each word (movzx in 0x93a857)
    {4, Ret::Int, true, "GetColor", "uaaa"},    // 11
    {1, Ret::Int, true, "SetStyle", "u"},       // 12
    {0, Ret::Int, true, "GetStyle", ""},        // 13
};
inline constexpr drawfx::Iface kFlatRectFace{"cIGZWinFlatRect", 14, kFlatRectSlots};

constexpr std::array<Slot, 29> MakeTextSlots()
{
    std::array<Slot, 29> slots{};
    for (Slot &s : slots) s = {0, Ret::Int, true, "", ""};
    slots[0].words = 2;
    slots[3].name = "GetWindow";
    slots[5] = {2, Ret::Int, true, "FitToText", "ii"};
    slots[6] = {1, Ret::Int, true, "IsFlagSet", "u"};
    slots[7] = {2, Ret::Int, true, "SetFlag", "ub"};
    slots[9] = {2, Ret::Int, true, "SetColor", "uu"};
    slots[12] = {2, Ret::Int, true, "GetColor", "ua"};
    slots[19] = {1, Ret::Int, true, "SetFont", "u"};
    slots[20] = {1, Ret::Int, true, "GetFont", "a"};
    slots[21] = {1, Ret::Int, true, "SetAlign", "u"};
    slots[22] = {1, Ret::Int, true, "GetAlign", "a"};
    slots[23] = {2, Ret::Int, true, "SetGutters", "ii"};
    slots[24] = {2, Ret::Int, true, "GetGutters", "aa"};
    slots[25] = {2, Ret::Int, true, "SetTextOffsets", "ii"};
    slots[26] = {2, Ret::Int, true, "GetTextOffsets", "aa"};
    return slots;
}
inline constexpr std::array<Slot, 29> kTextSlots = MakeTextSlots();
inline constexpr drawfx::Iface kTextFace{"cIGZWinText", 29, kTextSlots.data()};

inline constexpr Slot kSliderSlots[] = {
    {2, Ret::Int, true, "", ""},                     // 0 QueryInterface
    {0, Ret::Int, true, "", ""},                     // 1
    {0, Ret::Int, true, "", ""},                     // 2
    {0, Ret::Int, true, "GetWindow", ""},            // 3
    {0, Ret::Int, true, "GetValue", ""},          // 4
    {1, Ret::Int, true, "SetValue", "i"},            // 5
    {1, Ret::Int, true, "SetMin", "i"},              // 6
    {0, Ret::Int, true, "GetMin", ""},            // 7
    {1, Ret::Int, true, "SetMax", "i"},              // 8
    {0, Ret::Int, true, "GetMax", ""},            // 9
    {1, Ret::Int, true, "SetDirection", "u"},        // 10
    {1, Ret::Int, true, "GetDirection", "a"},    // 11
    {2, Ret::Int, true, "SetImage", "ir"},           // 12 (the image, a rectangle or null)
};
inline constexpr drawfx::Iface kSliderFace{"cIGZWinSlider", 13, kSliderSlots};

constexpr std::array<Slot, 18> MakeScrollbarSlots()
{
    std::array<Slot, 18> slots{};
    for (Slot &s : slots) s = {0, Ret::Int, true, "", ""};
    slots[0].words = 2;
    slots[3].name = "GetWindow";
    slots[5] = {1, Ret::Int, true, "SetValue", "i"};
    slots[7] = {2, Ret::Int, true, "SetRange", "ii"};
    slots[9] = {1, Ret::Int, true, "SetDirection", "u"};
    slots[11] = {1, Ret::Int, true, "SetPageSize", "u"};
    slots[13] = {1, Ret::Int, true, "SetLineSize", "u"};
    slots[15] = {2, Ret::Int, true, "SetLinePageCount", "ii"};
    slots[17] = {3, Ret::Int, true, "SetImage", "riu"}; // a rectangle, the image, 0
    slots[6].name = "GetValue";
    slots[8] = {2, Ret::Int, true, "GetRange", "aa"};
    slots[10].name = "GetDirection";
    slots[12].name = "GetPageSize";
    slots[14].name = "GetLineSize";
    slots[16] = {2, Ret::Int, true, "GetLinePageCount", "aa"};
    return slots;
}
inline constexpr std::array<Slot, 18> kScrollbarSlots = MakeScrollbarSlots();
inline constexpr drawfx::Iface kScrollbarFace{"cIGZWinScrollbar", 18, kScrollbarSlots.data()};

constexpr std::array<Slot, 19> MakeOutlineSlots()
{
    std::array<Slot, 19> slots{};
    for (Slot &s : slots) s = {0, Ret::Int, true, "", ""};
    slots[0].words = 2;
    slots[3] = {1, Ret::Int, true, "SetStyle", "u"};
    slots[5] = {4, Ret::Int, true, "SetColor", "ubbb"}; // the bytes are read with movzx (0x95fbdf)
    slots[4] = {0, Ret::Int, true, "GetStyle", ""};
    slots[6] = {4, Ret::Int, true, "GetColor", "uaaa"};
    slots[13] = {4, Ret::Int, true, "SetNodes", "u@0x2uu"}; // the count, that many (x, y) pairs, two flags
    slots[16] = {0, Ret::Int, true, "GetNodeCount", ""};
    slots[17] = {1, Ret::Int, true, "GetNodes", "a"};
    return slots;
}
inline constexpr std::array<Slot, 19> kOutlineSlots = MakeOutlineSlots();
inline constexpr drawfx::Iface kOutlineFace{"cIGZWinOutline", 19, kOutlineSlots.data()};

constexpr std::array<Slot, 24> MakeScrollbar2Slots()
{
    std::array<Slot, 24> slots{};
    for (Slot &s : slots) s = {0, Ret::Int, true, "", ""};
    slots[0].words = 2;
    slots[3].name = "GetWindow";
    slots[4] = {1, Ret::Int, true, "SetVertical", "u"};
    slots[6] = {2, Ret::Int, true, "SetValue", "ii"};
    slots[9] = {2, Ret::Int, true, "SetMinValue", "ii"};
    slots[11] = {2, Ret::Int, true, "SetMaxValue", "ii"};
    slots[13] = {2, Ret::Int, true, "SetVisibleRange", "ii"};
    slots[15] = {1, Ret::Int, true, "SetPageOverlap", "i"};
    slots[17] = {1, Ret::Int, true, "SetIncrement", "i"};
    slots[19] = {1, Ret::Int, true, "SetArrowsImage", "i"};
    slots[21] = {1, Ret::Int, true, "SetContainerImage", "i"};
    slots[23] = {1, Ret::Int, true, "SetThumbImage", "i"};
    slots[5] = {1, Ret::Int, true, "GetVertical", "a"};
    slots[7] = {1, Ret::Int, true, "GetValue", "a"};
    slots[10] = {1, Ret::Int, true, "GetMinValue", "a"};
    slots[12] = {1, Ret::Int, true, "GetMaxValue", "a"};
    slots[14] = {1, Ret::Int, true, "GetVisibleRange", "a"};
    slots[16] = {1, Ret::Int, true, "GetPageOverlap", "a"};
    slots[18] = {1, Ret::Int, true, "GetIncrement", "a"};
    return slots;
}
inline constexpr std::array<Slot, 24> kScrollbar2Slots = MakeScrollbar2Slots();
inline constexpr drawfx::Iface kScrollbar2Face{"cIGZWinScrollbar2", 24, kScrollbar2Slots.data()};

constexpr std::array<Slot, 21> MakeFoldersSlots()
{
    std::array<Slot, 21> slots{};
    for (Slot &s : slots) s = {0, Ret::Int, true, "", ""};
    slots[0].words = 2;
    slots[3].name = "GetWindow";
    slots[4] = {2, Ret::Int, true, "AddElement", "Su"};
    slots[9] = {0, Ret::Int, true, "ClearElements", ""};
    slots[13] = {1, Ret::Int, true, "GetFont", "a"};
    slots[14] = {1, Ret::Int, true, "SetFont", "u"};
    slots[15] = {1, Ret::Int, true, "SetImage", "i"};
    slots[20] = {2, Ret::Int, true, "GetElement", "ia"};
    return slots;
}
inline constexpr std::array<Slot, 21> kFoldersSlots = MakeFoldersSlots();
inline constexpr drawfx::Iface kFoldersFace{"cIGZWinFolders", 21, kFoldersSlots.data()};

constexpr std::array<Slot, 51> MakeComboSlots()
{
    std::array<Slot, 51> slots{};
    for (Slot &s : slots) s = {0, Ret::Int, true, "", ""};
    slots[0].words = 2;
    slots[3].name = "GetWindow";
    slots[12] = {1, Ret::Int, true, "GetFont", "a"};
    slots[13] = {1, Ret::Int, true, "SetFont", "u"};
    slots[17] = {0, Ret::Int, true, "IsEditable", ""};
    slots[18] = {1, Ret::Int, true, "SetEditable", "u"}; // the flag is a full word (movzx of a sete)
    slots[23] = {3, Ret::Int, true, "AddElement", "Sii"};
    slots[24] = {2, Ret::Int, true, "SetInitialSelection", "ui"};
    slots[26] = {0, Ret::Int, true, "GetInitialSelection", ""};
    slots[28] = {2, Ret::Int, true, "GetElement", "ia"};
    slots[32] = {2, Ret::Int, true, "SetArrowImage", "ir"}; // the image, a rectangle or null
    slots[33] = {2, Ret::Int, true, "GetArrowImage", "aa"};
    slots[34] = {1, Ret::Int, true, "SetArrowColor", "u"};
    slots[35] = {1, Ret::Int, true, "GetArrowColor", "a"};
    slots[36] = {1, Ret::Int, true, "SetButtonGutter", "u"};
    slots[37] = {1, Ret::Int, true, "GetButtonGutter", "a"};
    slots[38] = {2, Ret::Int, true, "SetGutters", "ii"};
    slots[39] = {2, Ret::Int, true, "GetGutters", "aa"};
    slots[41] = {2, Ret::Int, true, "SetColor", "uu"};
    slots[42] = {2, Ret::Int, true, "GetColor", "ua"};
    slots[45] = {1, Ret::Int, true, "SetHighlightColor", "u"};
    slots[46] = {1, Ret::Int, true, "GetHighlightColor", "a"};
    slots[49] = {1, Ret::Int, true, "SetOutlineColor", "u"};
    slots[50] = {1, Ret::Int, true, "GetOutlineColor", "a"};
    return slots;
}
inline constexpr std::array<Slot, 51> kComboSlots = MakeComboSlots();
inline constexpr drawfx::Iface kComboFace{"cIGZWinCombo", 51, kComboSlots.data()};

constexpr std::array<Slot, 72> MakeListBoxSlots()
{
    std::array<Slot, 72> slots{};
    for (Slot &s : slots) s = {0, Ret::Int, true, "", ""};
    slots[0].words = 2;
    slots[3].name = "GetWindow";
    slots[7] = {3, Ret::Int, true, "AddElement", "Sii"};
    slots[13] = {0, Ret::Int, true, "GetInitialSelection", ""};
    slots[16] = {0, Ret::Int, true, "IsSorted", ""};
    slots[19] = {0, Ret::Int, true, "IsDrop", ""};
    slots[31] = {0, Ret::Int, true, "GetElementCount", ""};
    slots[10] = {0, Ret::Int, true, "FitStep1", ""};
    slots[11] = {2, Ret::Int, true, "SetInitialSelection", "ui"};
    slots[15] = {0, Ret::Int, true, "SortStep1", ""};
    slots[17] = {0, Ret::Int, true, "SortStep2", ""};
    slots[18] = {1, Ret::Int, true, "SetDrop", "u"};
    slots[28] = {1, Ret::Int, true, "SetLineVisible", "u"};
    slots[30] = {1, Ret::Int, true, "SetLineOnTop", "u"};
    slots[36] = {2, Ret::Int, true, "SetScrollbars", "bb"}; // dwords of which the original sets one byte each
    slots[37] = {2, Ret::Int, true, "GetScrollbars", "aa"};
    slots[39] = {1, Ret::Int, true, "SetFont", "u"};
    slots[40] = {1, Ret::Int, true, "GetFont", "a"};
    slots[42] = {2, Ret::Int, true, "SetColor", "uu"};
    slots[43] = {2, Ret::Int, true, "GetColor", "ua"};
    slots[48] = {0, Ret::Int, true, "FitStep2", ""};
    slots[49] = {0, Ret::Int, true, "FitStep3", ""};
    slots[50] = {3, Ret::Int, true, "GetElement", "iai"};
    slots[52] = {1, Ret::Int, true, "SetOutline", "u"};
    slots[53] = {0, Ret::Int, true, "IsOutline", ""};
    slots[54] = {1, Ret::Int, true, "SetSunken", "u"};
    slots[55] = {0, Ret::Int, true, "IsSunken", ""};
    slots[56] = {2, Ret::Int, true, "SetScrollbarGutters", "ii"};
    slots[57] = {2, Ret::Int, true, "GetScrollbarGutters", "aa"};
    slots[58] = {2, Ret::Int, true, "SetGutters", "ii"};
    slots[59] = {2, Ret::Int, true, "GetGutters", "aa"};
    slots[68] = {1, Ret::Int, true, "SetHighlightColor", "u"};
    slots[69] = {1, Ret::Int, true, "GetHighlightColor", "a"};
    slots[70] = {2, Ret::Int, true, "SetOutlineColors", "uu"};
    slots[71] = {2, Ret::Int, true, "GetOutlineColors", "aa"};
    return slots;
}
inline constexpr std::array<Slot, 72> kListBoxSlots = MakeListBoxSlots();
inline constexpr drawfx::Iface kListBoxFace{"cIGZWinListBox", 72, kListBoxSlots.data()};

constexpr std::array<Slot, 35> MakeLineInputSlots()
{
    std::array<Slot, 35> slots{};
    for (Slot &s : slots) s = {0, Ret::Int, true, "", ""};
    slots[0].words = 2;
    slots[3].name = "GetWindow";
    slots[4] = {1, Ret::Int, true, "SetFont", "u"};
    slots[5] = {2, Ret::Int, true, "SetColor", "uu"};
    slots[7] = {2, Ret::Int, true, "GetColor", "ua"};
    slots[9] = {1, Ret::Int, true, "SetFilled", "u"};
    slots[10] = {0, Ret::Int, true, "IsFilled", ""};
    slots[11] = {1, Ret::Int, true, "SetOutlineStyle", "u"};
    slots[12] = {1, Ret::Int, true, "GetOutlineStyle", "a"};
    slots[14] = {2, Ret::Int, true, "SetBorderColor", "uu"};
    slots[15] = {2, Ret::Int, true, "GetBorderColor", "ua"};
    slots[17] = {1, Ret::Int, true, "SetHighlightFillColor", "u"};
    slots[18] = {1, Ret::Int, true, "GetHighlightFillColor", "a"};
    slots[19] = {1, Ret::Int, true, "SetHighlightFillAlpha", "f"};
    slots[20] = {1, Ret::Int, true, "GetHighlightFillAlpha", "a"};
    slots[25] = {1, Ret::Int, true, "SetCaretPosition", "u"};
    slots[26] = {1, Ret::Int, true, "GetCaretPosition", "a"};
    slots[27] = {0, Ret::Int, true, "IsEditable", ""};
    slots[28] = {1, Ret::Int, true, "SetEditable", "u"};
    slots[29] = {1, Ret::Int, true, "SetCharLimit", "u"};
    slots[30] = {1, Ret::Int, true, "GetCharLimit", "a"};
    slots[31] = {2, Ret::Int, true, "SetSelection", "ii"};
    slots[32] = {2, Ret::Int, true, "GetSelection", "aa"};
    slots[33] = {4, Ret::Int, true, "SetGutterRect", "iiii"};
    slots[34] = {4, Ret::Int, true, "GetGutterRect", "aaaa"};
    return slots;
}
inline constexpr std::array<Slot, 35> kLineInputSlots = MakeLineInputSlots();
inline constexpr drawfx::Iface kLineInputFace{"cIGZWinLineInput", 35, kLineInputSlots.data()};

constexpr std::array<Slot, 33> MakeSpinnerSlots()
{
    std::array<Slot, 33> slots{};
    for (Slot &s : slots) s = {0, Ret::Int, true, "", ""};
    slots[0].words = 2;
    slots[3].name = "GetWindow";
    slots[4] = {1, Ret::Int, true, "SetDigits", "u"};
    slots[6] = {2, Ret::Int, true, "SetFlag", "ub"}; // the mask, a flag the original pushes as 0 / 1
    slots[9] = {1, Ret::Int, true, "SetValue", "i"};
    slots[10] = {2, Ret::Int, true, "SetRange", "ii"};
    slots[12] = {1, Ret::Int, true, "SetStepSize", "i"};
    slots[14] = {2, Ret::Int, true, "SetImage", "ir"}; // the image, no rectangle
    slots[19] = {1, Ret::Int, true, "SetFont", "u"};
    slots[23] = {2, Ret::Int, true, "SetColor", "uu"};
    slots[27] = {1, Ret::Int, true, "SetAlign", "u"};
    slots[29] = {2, Ret::Int, true, "SetGutters", "ii"};
    slots[31] = {4, Ret::Int, true, "SetOutlineColors", "uuuu"};
    // The getters of the script writer (lane FJ).
    slots[5] = {1, Ret::Int, true, "GetDigits", "a"};
    slots[7] = {1, Ret::Int, true, "IsFlagSet", "u"};
    slots[8].name = "GetValue";
    slots[11] = {2, Ret::Int, true, "GetRange", "aa"};
    slots[13] = {1, Ret::Int, true, "GetStepSize", "a"};
    slots[25] = {2, Ret::Int, true, "GetColor", "ua"};
    slots[28] = {1, Ret::Int, true, "GetAlign", "a"};
    slots[30] = {2, Ret::Int, true, "GetGutters", "aa"};
    slots[32] = {4, Ret::Int, true, "GetOutlineColors", "aaaa"};
    return slots;
}
inline constexpr std::array<Slot, 33> kSpinnerSlots = MakeSpinnerSlots();
inline constexpr drawfx::Iface kSpinnerFace{"cIGZWinSpinner", 33, kSpinnerSlots.data()};

constexpr std::array<Slot, 13> MakeTextTickerSlots()
{
    std::array<Slot, 13> slots{};
    for (Slot &s : slots) s = {0, Ret::Int, true, "", ""};
    slots[0].words = 2;
    slots[3] = {5, Ret::Int, true, "SetMessage", "Suuii"}; // the text, the two colours, two words
    slots[7] = {1, Ret::Int, true, "SetFont", "u"};
    slots[8] = {1, Ret::Int, true, "SetSpeed", "u"};
    slots[9] = {1, Ret::Int, true, "SetFrequency", "u"};
    slots[10] = {0, Ret::Int, true, "GetSpeed", ""};
    slots[11] = {0, Ret::Int, true, "GetFrequency", ""};
    slots[12] = {0, Ret::Int, true, "GetWindow", ""};
    return slots;
}
inline constexpr std::array<Slot, 13> kTextTickerSlots = MakeTextTickerSlots();
inline constexpr drawfx::Iface kTextTickerFace{"cIGZWinTextTicker", 13, kTextTickerSlots.data()};

constexpr std::array<Slot, 47> MakeButtonSlots()
{
    std::array<Slot, 47> slots{};
    for (Slot &s : slots) s = {0, Ret::Int, true, "", ""};
    slots[0].words = 2;
    slots[3].name = "GetWindow";
    slots[5] = {2, Ret::Int, true, "SetFlag", "ub"};
    slots[12] = {3, Ret::Int, true, "SetImage", "uir"}; // the look, the image, its rectangle (or none)
    slots[19] = {1, Ret::Int, true, "SetFontObject", "u"};
    slots[20] = {1, Ret::Int, true, "SetFontId", "u"};
    slots[23] = {2, Ret::Int, true, "SetColor", "uu"};
    slots[27] = {1, Ret::Int, true, "SetAlign", "u"};
    slots[29] = {4, Ret::Int, true, "SetGutterRect", "iiii"};
    slots[30] = {2, Ret::Int, true, "SetGutters", "ii"};
    slots[35] = {1, Ret::Int, true, "SetTipText", "S"};
    slots[38] = {1, Ret::Int, true, "SetTipFlags", "u"};
    slots[40] = {2, Ret::Int, true, "SetTipOffsets", "ii"};
    slots[45] = {2, Ret::Int, true, "SetSound", "uu"};
    // the getters the script writer (0x86ff03) reads
    slots[4] = {1, Ret::Int, true, "IsFlagSet", "u"};
    slots[11] = {0, Ret::Int, true, "GetLook", ""};
    slots[21] = {1, Ret::Int, true, "GetFontId", "a"};
    slots[25] = {2, Ret::Int, true, "GetColor", "ua"};
    slots[28] = {1, Ret::Int, true, "GetAlign", "a"};
    slots[31] = {4, Ret::Int, true, "GetGutterRect", "aaaa"};
    slots[36] = {1, Ret::Int, true, "GetTipText", "a"};
    slots[39] = {1, Ret::Int, true, "GetTipFlags", "a"};
    slots[41] = {2, Ret::Int, true, "GetTipOffsets", "aa"};
    slots[46] = {1, Ret::Int, true, "GetSound", "u"};
    return slots;
}
inline constexpr std::array<Slot, 47> kButtonSlots = MakeButtonSlots();
inline constexpr drawfx::Iface kButtonFace{"cIGZWinButton", 47, kButtonSlots.data()};

constexpr std::array<Slot, 95> MakeTextEditSlots()
{
    std::array<Slot, 95> slots{};
    for (Slot &s : slots) s = {0, Ret::Int, true, "", ""};
    slots[0].words = 2;
    slots[3].name = "GetWindow";
    slots[5].name = "GetWindowForWriter";
    slots[11] = {1, Ret::Int, true, "GetFont", "a"};
    slots[14] = {2, Ret::Int, true, "GetColor", "ua"};
    slots[19] = {1, Ret::Int, true, "GetCaretColor", "a"};
    slots[22] = {0, Ret::Int, true, "GetHighlightColor", ""};
    slots[32] = {0, Ret::Int, true, "GetMaxText", ""};
    slots[34] = {0, Ret::Int, true, "GetMaxGlyphs", ""};
    slots[40] = {2, Ret::Int, true, "GetOutlineColor", "aa"};
    slots[41] = {0, Ret::Int, true, "GetGutterX", ""};
    slots[43] = {0, Ret::Int, true, "GetGutterY", ""};
    slots[48] = {0, Ret::Int, true, "GetInitialValue", ""};
    slots[60] = {1, Ret::Int, true, "GetPrompt", "a"};
    slots[71] = {0, Ret::Int, true, "GetInsertMode", ""};
    slots[73] = {1, Ret::Int, true, "GetInsertIndex", "a"};
    slots[75] = {2, Ret::Int, true, "GetInsertPos", "aa"};
    slots[79] = {1, Ret::Int, true, "GetCaretPeriod", "a"};
    slots[88] = {0, Ret::Int, true, "GetMaxUndo", ""};
    slots[91] = {2, Ret::Int, true, "GetVScrollImage", "aa"};
    slots[92] = {2, Ret::Int, true, "GetHScrollImage", "aa"};
    slots[10] = {1, Ret::Int, true, "SetFont", "u"};
    slots[12] = {2, Ret::Int, true, "SetColor", "uu"};
    slots[18] = {1, Ret::Int, true, "SetCaretColor", "u"};
    slots[23] = {1, Ret::Int, true, "SetHighlightColor", "u"};
    slots[33] = {1, Ret::Int, true, "SetMaxText", "u"};
    slots[35] = {1, Ret::Int, true, "SetMaxGlyphs", "u"};
    slots[39] = {2, Ret::Int, true, "SetOutlineColor", "bu"};
    slots[42] = {1, Ret::Int, true, "SetGutterX", "i"};
    slots[44] = {1, Ret::Int, true, "SetGutterY", "i"};
    slots[45] = {0, Ret::Int, true, "Autofit", ""};
    slots[61] = {2, Ret::Int, true, "SetPrompt", "Su"};
    slots[70] = {1, Ret::Int, true, "SetInsertMode", "b"};
    slots[72] = {1, Ret::Int, true, "SetInsertIndex", "u"};
    slots[74] = {2, Ret::Int, true, "SetInsertPos", "ii"};
    slots[78] = {1, Ret::Int, true, "SetCaretPeriod", "u"};
    slots[83] = {1, Ret::Int, true, "SetFlags", "u"};
    slots[86] = {0, Ret::Int, true, "GetFlags", ""};
    slots[87] = {1, Ret::Int, true, "SetMaxUndo", "u"};
    slots[93] = {2, Ret::Int, true, "SetVScrollImage", "ir"};
    slots[94] = {2, Ret::Int, true, "SetHScrollImage", "ir"};
    return slots;
}
inline constexpr std::array<Slot, 95> kTextEditSlots = MakeTextEditSlots();
inline constexpr drawfx::Iface kTextEditFace{"cIGZWinTextEdit", 95, kTextEditSlots.data()};

constexpr std::array<Slot, 38> MakeOptGrpSlots()
{
    std::array<Slot, 38> slots{};
    for (Slot &s : slots) s = {0, Ret::Int, true, "", ""};
    slots[0].words = 2;
    slots[3].name = "GetWindow";
    slots[4] = {1, Ret::Int, true, "IsFlagSet", "u"};
    slots[5] = {2, Ret::Int, true, "SetFlag", "ub"};
    slots[6] = {2, Ret::Int, true, "SetGutters", "uu"};
    slots[7] = {2, Ret::Int, true, "GetGutters", "aa"};
    slots[15] = {2, Ret::Int, true, "GetColor", "ua"};
    slots[21] = {0, Ret::Int, true, "GetInitialSelection", ""};
    slots[30] = {4, Ret::Int, true, "GetOutlineColors", "aaaa"};
    slots[37] = {2, Ret::Int, true, "GetAutofill", "aa"};
    slots[8] = {1, Ret::Int, true, "SetLabelFont", "u"};
    slots[9] = {1, Ret::Int, true, "SetButtonFont", "u"};
    slots[14] = {2, Ret::Int, true, "SetColor", "uu"};
    slots[22] = {1, Ret::Int, true, "SetInitialSelection", "u"};
    slots[23] = {5, Ret::Int, true, "AddOption", "uSuir"};
    slots[25] = {3, Ret::Int, true, "MoveOption", "uuu"};
    slots[26] = {3, Ret::Int, true, "SetOptionSize", "uuu"};
    slots[27] = {2, Ret::Int, true, "GetOption", "ui"};
    slots[28] = {1, Ret::Int, true, "SetBackImage", "i"};
    slots[29] = {4, Ret::Int, true, "SetOutlineColors", "uuuu"};
    slots[36] = {2, Ret::Int, true, "SetAutofill", "uu"};
    return slots;
}
inline constexpr std::array<Slot, 38> kOptGrpSlots = MakeOptGrpSlots();
inline constexpr drawfx::Iface kOptGrpFace{"cIGZWinOptGrp", 38, kOptGrpSlots.data()};

constexpr std::array<Slot, 31> MakeGenSlots()
{
    std::array<Slot, 31> slots{};
    for (Slot &s : slots) s = {0, Ret::Int, true, "", ""};
    slots[0].words = 2;
    slots[3].name = "GetWindow";
    slots[5] = {1, Ret::Int, true, "IsFlagSet", "u"};
    slots[6] = {2, Ret::Int, true, "SetFlag", "ub"};
    slots[7] = {1, Ret::Int, true, "SetWinProc", "i"};
    slots[13] = {2, Ret::Int, true, "SetBackImage", "ii"};
    slots[15] = {2, Ret::Int, true, "GetBackImageType", "aa"};
    slots[19] = {1, Ret::Int, true, "SetUserData", "u"};
    slots[22] = {0, Ret::Int, true, "GetUserData", ""};
    slots[26] = {2, Ret::Int, true, "SetGutters", "ii"};
    slots[28] = {2, Ret::Int, true, "GetGutters", "aa"};
    slots[29] = {4, Ret::Int, true, "SetBlueBar", "uuur"};
    slots[30] = {2, Ret::Int, true, "SetDivider", "ui"};
    return slots;
}
inline constexpr std::array<Slot, 31> kGenSlots = MakeGenSlots();
inline constexpr drawfx::Iface kGenFace{"cIGZWinGen", 31, kGenSlots.data()};

constexpr std::array<Slot, 108> MakeBrowserSlots()
{
    std::array<Slot, 108> slots{};
    for (Slot &s : slots) s = {0, Ret::Int, true, "", ""};
    slots[0].words = 2;
    slots[15] = {1, Ret::Int, true, "SetCaption", "S"};
    slots[22] = {4, Ret::Int, true, "AddEntry", "Siuu"};
    slots[30] = {3, Ret::Int, true, "SetModeTexts", "uSS"};
    slots[36] = {2, Ret::Int, true, "GetCorners", "aa"};
    slots[37] = {2, Ret::Int, true, "SetCorners", "uu"};
    slots[39] = {1, Ret::Int, true, "SetFilter", "u"};
    slots[40] = {3, Ret::Int, true, "GetBars", "aaa"};
    slots[42] = {3, Ret::Int, true, "SetBars", "uuu"};
    slots[49] = {1, Ret::Int, true, "SetFontA", "u"};
    slots[52] = {2, Ret::Int, true, "SetColorA", "uu"};
    slots[53] = {1, Ret::Int, true, "SetModeA", "u"};
    slots[59] = {1, Ret::Int, true, "SetFontB", "u"};
    slots[62] = {2, Ret::Int, true, "SetColorB", "uu"};
    slots[63] = {1, Ret::Int, true, "SetModeB", "u"};
    slots[69] = {1, Ret::Int, true, "SetFontC", "u"};
    slots[72] = {2, Ret::Int, true, "SetColorC", "uu"};
    slots[73] = {1, Ret::Int, true, "SetModeC", "u"};
    slots[75] = {1, Ret::Int, true, "SetValueA", "u"};
    slots[77] = {1, Ret::Int, true, "SetValueB", "u"};
    slots[79] = {1, Ret::Int, true, "SetValueC", "u"};
    slots[80] = {2, Ret::Int, true, "GetSelection", "aa"};
    slots[81] = {2, Ret::Int, true, "SetSelection", "bu"};
    slots[84] = {2, Ret::Int, true, "GetCursor", "aa"};
    slots[85] = {2, Ret::Int, true, "SetCursor", "bu"};
    slots[88] = {2, Ret::Int, true, "GetBorder", "aa"};
    slots[89] = {2, Ret::Int, true, "SetBorder", "uu"};
    slots[95] = {1, Ret::Int, true, "SetPath", "S"};
    slots[97] = {1, Ret::Int, true, "SetOutline", "b"};
    slots[99] = {1, Ret::Int, true, "SetValueD", "u"};
    slots[103] = {1, Ret::Int, true, "SetColorD", "u"};
    slots[105] = {1, Ret::Int, true, "SetBackImage", "i"};
    slots[107] = {1, Ret::Int, true, "SetFlagE", "b"};
    // The script writer's getters (lane WF, 0x87117a).
    slots[3] = {0, Ret::Int, true, "GetWindow", ""};
    slots[14] = {1, Ret::Int, true, "GetDefaultPath", "a"};
    slots[21] = {5, Ret::Int, true, "GetEntry", "uaaau"};
    slots[29] = {3, Ret::Int, true, "GetDefaultSelection", "aaa"};
    slots[38] = {1, Ret::Int, true, "GetDirectorySort", "a"};
    slots[47] = {1, Ret::Int, true, "GetFileNameColor", "u"};
    slots[48] = {0, Ret::Int, true, "GetFileNameAlignment", ""};
    slots[57] = {1, Ret::Int, true, "GetFileExtensionColor", "u"};
    slots[58] = {0, Ret::Int, true, "GetFileExtensionAlignment", ""};
    slots[67] = {1, Ret::Int, true, "GetDriveNameColor", "u"};
    slots[68] = {0, Ret::Int, true, "GetDriveNameAlignment", ""};
    slots[74] = {0, Ret::Int, true, "GetIconColumnWidth", ""};
    slots[76] = {0, Ret::Int, true, "GetExtensionColumnWidth", ""};
    slots[78] = {0, Ret::Int, true, "GetRowHeight", ""};
    slots[94] = {1, Ret::Int, true, "GetBackDirText", "a"};
    slots[96] = {0, Ret::Int, true, "IsOutline", ""};
    slots[98] = {0, Ret::Int, true, "GetDriveListSize", ""};
    slots[101] = {0, Ret::Int, true, "GetBackgroundColor", ""};
    slots[106] = {0, Ret::Int, true, "IsBackgroundTransparent", ""};
    return slots;
}
inline constexpr std::array<Slot, 108> kBrowserSlots = MakeBrowserSlots();
inline constexpr drawfx::Iface kBrowserFace{"cIGZWinFileBrowser", 108, kBrowserSlots.data()};

constexpr std::array<Slot, 121> MakeGridSlots()
{
    std::array<Slot, 121> slots{};
    for (Slot &s : slots) s = {0, Ret::Int, true, "", ""};
    slots[0].words = 2;
    slots[5] = {0, Ret::Int, true, "GetWindow", ""};
    slots[10] = {1, Ret::Int, true, "SetFlags", "u"};
    slots[12] = {1, Ret::Int, true, "ClearFlags", "u"};
    slots[15] = {1, Ret::Int, true, "SetFont", "u"};
    slots[20] = {2, Ret::Int, true, "SetColor", "uu"};
    slots[25] = {1, Ret::Int, true, "SetFlag800", "b"};
    slots[28] = {4, Ret::Int, true, "SetMargins", "uuuu"};
    slots[31] = {2, Ret::Int, true, "SetHighlight", "bu"};
    slots[45] = {2, Ret::Int, true, "SetSpacing", "uu"};
    slots[64] = {2, Ret::Int, true, "SetBackColors", "uu"};
    slots[66] = {1, Ret::Int, true, "SetValue66", "u"};
    slots[70] = {1, Ret::Int, true, "SetSizeA", "u"};
    slots[71] = {3, Ret::Int, true, "SetColumn", "uuu"};
    slots[72] = {1, Ret::Int, true, "SetSizeB", "u"};
    slots[73] = {3, Ret::Int, true, "SetRows", "uuu"};
    slots[80] = {2, Ret::Int, true, "SetScrollbars", "bb"};
    slots[82] = {2, Ret::Int, true, "SetOffset", "uu"};
    slots[91] = {3, Ret::Int, true, "SetCellFont", "uuW9"};
    slots[118] = {6, Ret::Int, true, "SetCellText", "uusubb"};
    slots[120] = {4, Ret::Int, true, "SetCellImage", "uuib"};
    // The script writer's getters (lane WF, 0x873913).
    slots[13] = {1, Ret::Int, true, "IsFlagSet", "u"};
    slots[22] = {2, Ret::Int, true, "GetFontColor", "au"};
    slots[26] = {0, Ret::Int, true, "GetTextWrapping", ""};
    slots[27] = {4, Ret::Int, true, "GetGutters", "aaaa"};
    slots[33] = {0, Ret::Int, true, "GetOutlineColor", ""};
    slots[44] = {2, Ret::Int, true, "GetSelection", "aa"};
    slots[65] = {2, Ret::Int, true, "GetGridColors", "aa"};
    slots[69] = {0, Ret::Int, true, "GetTextAlign", ""};
    slots[74] = {0, Ret::Int, true, "GetDefaultColumnWidth", ""};
    slots[75] = {0, Ret::Int, true, "GetDefaultRowHeight", ""};
    slots[81] = {2, Ret::Int, true, "GetHeaderSizes", "aa"};
    return slots;
}
inline constexpr std::array<Slot, 121> kGridSlots = MakeGridSlots();
inline constexpr drawfx::Iface kGridFace{"cIGZWinGrid", 121, kGridSlots.data()};

constexpr std::array<Slot, 60> MakeTreeSlots()
{
    std::array<Slot, 60> slots{};
    for (Slot &s : slots) s = {0, Ret::Int, true, "", ""};
    slots[0].words = 2;
    slots[3] = {2, Ret::Int, true, "SetFlag", "ub"};
    slots[4] = {1, Ret::Int, true, "IsFlagSet", "u"};
    slots[7] = {1, Ret::Int, true, "SetIconGutter", "u"};
    slots[8] = {0, Ret::Int, true, "GetIconGutter", ""};
    slots[12] = {0, Ret::Int, true, "GetRowHeight", ""};
    slots[14] = {0, Ret::Int, true, "GetMinMaxBoxSize", ""};
    slots[16] = {0, Ret::Int, true, "GetBackgroundColor", ""};
    slots[18] = {0, Ret::Int, true, "GetHighlightColor", ""};
    slots[21] = {3, Ret::Int, true, "GetImage", "uaa"};
    slots[31] = {1, Ret::Int, true, "GetNodeText", "u"};
    slots[35] = {2, Ret::Int, true, "GetNodeImage", "uu"};
    slots[42] = {2, Ret::Int, true, "GetNodeColor", "uu"};
    slots[44] = {1, Ret::Int, true, "IsNodeExpanded", "u"};
    slots[50] = {1, Ret::Int, true, "IsNodeSelected", "u"};
    slots[51] = {1, Ret::Int, true, "GetNodeParent", "u"};
    slots[52] = {1, Ret::Int, true, "GetNodePosition", "u"};
    slots[59] = {0, Ret::Int, true, "GetWindow", ""};
    slots[9] = {2, Ret::Int, true, "SetBaseColor", "uu"};
    slots[11] = {1, Ret::Int, true, "SetValue1336", "u"};
    slots[13] = {1, Ret::Int, true, "SetMinMaxBoxSize", "u"};
    slots[15] = {1, Ret::Int, true, "SetColor133f", "u"};
    slots[17] = {1, Ret::Int, true, "SetHighlightColor", "u"};
    slots[20] = {2, Ret::Int, true, "AddImage", "ir"};
    slots[24] = {4, Ret::Int, true, "AddNode", "uuSu"};
    slots[32] = {2, Ret::Int, true, "SetNodeText", "uS"};
    slots[36] = {3, Ret::Int, true, "SetNodeImage", "uuu"};
    slots[39] = {2, Ret::Int, true, "SetNodeFont", "uu"};
    slots[43] = {3, Ret::Int, true, "SetNodeColor", "uuu"};
    slots[45] = {2, Ret::Int, true, "SetNodeExpanded", "ub"};
    slots[56] = {2, Ret::Int, true, "SetNodeSelected", "uu"};
    return slots;
}
inline constexpr std::array<Slot, 60> kTreeSlots = MakeTreeSlots();
inline constexpr drawfx::Iface kTreeFace{"cIGZWinTreeView", 60, kTreeSlots.data()};

// Self-check (compile time): each face's count is its table's size.
static_assert(kBitmapFace.count == std::size(kBitmapSlots));
static_assert(kImageInterface.count == std::size(kImageSlots));
static_assert(kGraphicsFace.count == std::size(kGraphicsSlots));
static_assert(kDefaultsFace.count == std::size(kDefaultsSlots));
static_assert(kFontEntryFace.count == std::size(kFontEntrySlots));
static_assert(kOptionItemFace.count == std::size(kOptionItemSlots));
static_assert(kFlatRectFace.count == std::size(kFlatRectSlots));
static_assert(kTextFace.count == std::size(kTextSlots));
static_assert(kSliderFace.count == std::size(kSliderSlots));
static_assert(kScrollbarFace.count == std::size(kScrollbarSlots));
static_assert(kOutlineFace.count == std::size(kOutlineSlots));
static_assert(kScrollbar2Face.count == std::size(kScrollbar2Slots));
static_assert(kFoldersFace.count == std::size(kFoldersSlots));
static_assert(kComboFace.count == std::size(kComboSlots));
static_assert(kListBoxFace.count == std::size(kListBoxSlots));
static_assert(kLineInputFace.count == std::size(kLineInputSlots));
static_assert(kSpinnerFace.count == std::size(kSpinnerSlots));
static_assert(kTextTickerFace.count == std::size(kTextTickerSlots));
static_assert(kButtonFace.count == std::size(kButtonSlots));
static_assert(kTextEditFace.count == std::size(kTextEditSlots));
static_assert(kOptGrpFace.count == std::size(kOptGrpSlots));
static_assert(kGenFace.count == std::size(kGenSlots));
static_assert(kBrowserFace.count == std::size(kBrowserSlots));
static_assert(kGridFace.count == std::size(kGridSlots));
static_assert(kTreeFace.count == std::size(kTreeSlots));

// ---- the window's tooltip property (lane WA, the script writer's common attributes 0x86f2b8) ------------------------
// The property 0xbaca6879 gives a counted source: slot 3 its flags (0x10: has texts), slot 62 (1, &set) a counted set.
constexpr std::array<Slot, 63> MakeTooltipSourceSlots()
{
    std::array<Slot, 63> slots{};
    for (Slot &s : slots) s = {0, Ret::Int, true, "", ""};
    slots[0].words = 2;
    slots[62] = {2, Ret::Int, true, "GetTexts", "ua"};
    return slots;
}
inline constexpr auto kTooltipSourceSlots = MakeTooltipSourceSlots();
inline constexpr drawfx::Iface kTooltipSourceFace{"tooltip source", 63, kTooltipSourceSlots.data()};
static_assert(kTooltipSourceFace.count == std::size(kTooltipSourceSlots));

// The set: a counted object whose word at +4 points at eight cRZStrings (0x20 apart). Plain virtuals (QueryInterface,
// AddRef, Release) give the vtable at +0; the releases are counted for the test to compare.
struct TooltipSetFake {
    cRZString *texts = &mTexts[0].mText; // +4
    int32_t releases = 0;
    LegacyText mTexts[8];       // constructed (the vtable), 0x20 apart
    virtual uint32_t QueryInterface(uint32_t, void **) { return 0; }
    virtual uint32_t AddRef() { return 1; }
    virtual uint32_t Release()
    {
        ++releases;
        return 1;
    }
};

// ---- the tree view's tables and getters (lane WD, the script writer 0x8792f2) --------------------------------------------
// The six tables a tree view keeps on its window (src/gz/cGZLegacyTreeTables.h: real std::maps of the game's layout) and the
// face's getters, driven by one state a test fills from its seed. Nodes and images are numbered 0..15 (0 is the root).
// Getters answer from the state; the ones that answer through a pointer write it only when the state says so.
struct TreeTables {
    legacy_tree::NameNumberMap imageNumbers, nodeFonts, nodeNumbers;
    legacy_tree::NumberNameMap numberImages, numberNodes;
    legacy_tree::NameResourceMap imageResources;
    void Clear()
    {
        imageNumbers.clear();
        nodeFonts.clear();
        nodeNumbers.clear();
        numberImages.clear();
        numberNodes.clear();
        imageResources.clear();
    }
};

struct TreeState {
    uint32_t flags = 0;
    int32_t gutter = 0, rowHeight = 0, boxSize = 0;
    uint32_t background = 0, highlight = 0;
    uint32_t imageAnswered = 0;     // bit (number % 16): GetImage answers true
    uint32_t rectWords = 0;         // bits 0..3 (+ 4 * number % 16 is not used): which rectangle words GetImage writes
    int32_t rects[16][4] = {};
    std::unique_ptr<LegacyText[]> texts; // GetNodeText's answers: cRZStrings, made by Begin (not at static initialisation)
    void Begin() { texts.reset(new LegacyText[16]); }
    void End() { texts.reset(); }
    uint32_t images[16][4] = {};    // GetNodeImage
    uint32_t colors[16][4] = {};    // GetNodeColor (native)
    uint32_t parents[16] = {};
    uint32_t expanded[16] = {};     // the words the getters answer: only a low byte of exactly 1 counts
    uint32_t selected[16] = {};
    int32_t positions[16] = {};
    void *window = nullptr;         // GetWindow's answer
};
inline TreeState gTreeState;

inline uint32_t TreeIsFlagSet(drawfx::Fake &, const uint32_t *a) { return (gTreeState.flags & a[0]) != 0; }
inline uint32_t TreeGetIconGutter(drawfx::Fake &, const uint32_t *) { return uint32_t(gTreeState.gutter); }
inline uint32_t TreeGetRowHeight(drawfx::Fake &, const uint32_t *) { return uint32_t(gTreeState.rowHeight); }
inline uint32_t TreeGetBoxSize(drawfx::Fake &, const uint32_t *) { return uint32_t(gTreeState.boxSize); }
inline uint32_t TreeGetBackground(drawfx::Fake &, const uint32_t *) { return gTreeState.background; }
inline uint32_t TreeGetHighlight(drawfx::Fake &, const uint32_t *) { return gTreeState.highlight; }
// GetImage(number, &image, rect): the answer is ignored by the writer; the image is not used; the rectangle's words are
// written only for the bits of rectWords (the writer starts them at zero).
inline uint32_t TreeGetImage(drawfx::Fake &, const uint32_t *a)
{
    const uint32_t n = a[0] & 15;
    int32_t *rect = reinterpret_cast<int32_t *>(uintptr_t(a[2]));
    for (int i = 0; i < 4; ++i)
        if (gTreeState.rectWords >> i & 1) rect[i] = gTreeState.rects[n][i];
    return gTreeState.imageAnswered >> n & 1 ? 0x7701 : 0x7700;
}
inline uint32_t TreeGetNodeText(drawfx::Fake &, const uint32_t *a)
{
    return uint32_t(uintptr_t(&gTreeState.texts[a[0] & 15].mText));
}
inline uint32_t TreeGetNodeImage(drawfx::Fake &, const uint32_t *a) { return gTreeState.images[a[0] & 15][a[1] & 3]; }
inline uint32_t TreeGetNodeColor(drawfx::Fake &, const uint32_t *a) { return gTreeState.colors[a[0] & 15][a[1] & 3]; }
inline uint32_t TreeIsNodeExpanded(drawfx::Fake &, const uint32_t *a) { return gTreeState.expanded[a[0] & 15]; }
inline uint32_t TreeIsNodeSelected(drawfx::Fake &, const uint32_t *a) { return gTreeState.selected[a[0] & 15]; }
inline uint32_t TreeGetNodeParent(drawfx::Fake &, const uint32_t *a) { return gTreeState.parents[a[0] & 15]; }
inline uint32_t TreeGetNodePosition(drawfx::Fake &, const uint32_t *a) { return uint32_t(gTreeState.positions[a[0] & 15]); }
inline uint32_t TreeGetWindow(drawfx::Fake &, const uint32_t *) { return uint32_t(uintptr_t(gTreeState.window)); }
// Installs the getters on a face Reset to kTreeFace.
inline void InstallTreeGetters(drawfx::Fake &f)
{
    f.On(4, &TreeIsFlagSet);
    f.On(8, &TreeGetIconGutter);
    f.On(12, &TreeGetRowHeight);
    f.On(14, &TreeGetBoxSize);
    f.On(16, &TreeGetBackground);
    f.On(18, &TreeGetHighlight);
    f.On(21, &TreeGetImage);
    f.On(31, &TreeGetNodeText);
    f.On(35, &TreeGetNodeImage);
    f.On(42, &TreeGetNodeColor);
    f.On(44, &TreeIsNodeExpanded);
    f.On(50, &TreeIsNodeSelected);
    f.On(51, &TreeGetNodeParent);
    f.On(52, &TreeGetNodePosition);
    f.On(59, &TreeGetWindow);
}

// ---- the file browser's and the grid's getters (lane WF, the script writers 0x87117a and 0x873913) -------------------------
// One state each, filled from the test's seed; the getters answer from it and write their out words always (the writers'
// locals have no value before: a getter that answered without writing would leave stack garbage in the original), except
// where noted. Strings are the char pointers the getters copy to the writer's cRZString.
struct FlBrowserState {
    const char *path = "", *selName = "", *selExt = "", *backText = "";
    int32_t selType = 0;
    uint32_t sorts[2] = {}, directorySort = 0, columns[3] = {};
    uint32_t colors[3][6] = {};
    int32_t alignments[3] = {};
    int32_t iconWidth = 0, extensionWidth = 0, rowHeight = 0, driveListSize = 0;
    uint32_t lineEnabled = 0, lineColor = 0, columnEnabled = 0, columnColor = 0;
    uint32_t highlight[2] = {};
    uint32_t background = 0, transparent = 0, outline = 0;
    uint32_t entryCount = 0, entryWrites = 0; // GetEntry answers true for the first entryCount; later numbers written only per bit
    const char *labels[10] = {};
    uint32_t images[10] = {};
    int32_t numbers[10] = {};
    void *window = nullptr;
};
inline FlBrowserState gFlBrowserState;

inline void WriteGzString(uint32_t where, const char *text) { reinterpret_cast<cRZString *>(uintptr_t(where))->FromChar(text); }
inline uint32_t FlBrowserGetWindow(drawfx::Fake &, const uint32_t *) { return uint32_t(uintptr_t(gFlBrowserState.window)); }
inline uint32_t FlBrowserGetDefaultPath(drawfx::Fake &, const uint32_t *a)
{
    WriteGzString(a[0], gFlBrowserState.path);
    return 0;
}
inline uint32_t FlBrowserGetEntry(drawfx::Fake &, const uint32_t *a)
{
    const FlBrowserState &s = gFlBrowserState;
    const uint32_t i = a[0];
    if (i >= s.entryCount) return 0;
    WriteGzString(a[1], s.labels[i]);
    *reinterpret_cast<uint32_t *>(uintptr_t(a[2])) = s.images[i];
    if (i == 0 || (s.entryWrites >> i & 1)) *reinterpret_cast<int32_t *>(uintptr_t(a[3])) = s.numbers[i];
    return 1;
}
inline uint32_t FlBrowserGetDefaultSelection(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<int32_t *>(uintptr_t(a[0])) = gFlBrowserState.selType;
    WriteGzString(a[1], gFlBrowserState.selName);
    WriteGzString(a[2], gFlBrowserState.selExt);
    return 0;
}
inline uint32_t FlBrowserGetCorners(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = gFlBrowserState.sorts[0];
    *reinterpret_cast<uint32_t *>(uintptr_t(a[1])) = gFlBrowserState.sorts[1];
    return 0;
}
inline uint32_t FlBrowserGetDirectorySort(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = gFlBrowserState.directorySort;
    return 0;
}
inline uint32_t FlBrowserGetBars(drawfx::Fake &, const uint32_t *a)
{
    for (int i = 0; i < 3; ++i) *reinterpret_cast<uint32_t *>(uintptr_t(a[i])) = gFlBrowserState.columns[i];
    return 0;
}
template <unsigned Section> uint32_t FlBrowserGetColor(drawfx::Fake &, const uint32_t *a) { return gFlBrowserState.colors[Section][a[0] % 6]; }
template <unsigned Section> uint32_t FlBrowserGetAlignment(drawfx::Fake &, const uint32_t *) { return uint32_t(gFlBrowserState.alignments[Section]); }
inline uint32_t FlBrowserGetIconWidth(drawfx::Fake &, const uint32_t *) { return uint32_t(gFlBrowserState.iconWidth); }
inline uint32_t FlBrowserGetExtensionWidth(drawfx::Fake &, const uint32_t *) { return uint32_t(gFlBrowserState.extensionWidth); }
inline uint32_t FlBrowserGetRowHeight(drawfx::Fake &, const uint32_t *) { return uint32_t(gFlBrowserState.rowHeight); }
// The two grids and the highlight: a byte and a colour (the byte as the low byte of a word: only exactly 1 is yes).
inline uint32_t FlBrowserGetLineGrid(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<uint8_t *>(uintptr_t(a[0])) = uint8_t(gFlBrowserState.lineEnabled);
    *reinterpret_cast<uint32_t *>(uintptr_t(a[1])) = gFlBrowserState.lineColor;
    return 0;
}
inline uint32_t FlBrowserGetColumnGrid(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<uint8_t *>(uintptr_t(a[0])) = uint8_t(gFlBrowserState.columnEnabled);
    *reinterpret_cast<uint32_t *>(uintptr_t(a[1])) = gFlBrowserState.columnColor;
    return 0;
}
inline uint32_t FlBrowserGetHighlight(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = gFlBrowserState.highlight[0];
    *reinterpret_cast<uint32_t *>(uintptr_t(a[1])) = gFlBrowserState.highlight[1];
    return 0;
}
inline uint32_t FlBrowserGetBackDirText(drawfx::Fake &, const uint32_t *a)
{
    WriteGzString(a[0], gFlBrowserState.backText);
    return 0;
}
inline uint32_t FlBrowserIsOutline(drawfx::Fake &, const uint32_t *) { return gFlBrowserState.outline; }
inline uint32_t FlBrowserGetDriveListSize(drawfx::Fake &, const uint32_t *) { return uint32_t(gFlBrowserState.driveListSize); }
inline uint32_t FlBrowserGetBackground(drawfx::Fake &, const uint32_t *) { return gFlBrowserState.background; }
inline uint32_t FlBrowserIsTransparent(drawfx::Fake &, const uint32_t *) { return gFlBrowserState.transparent; }
// Installs the getters on a face Reset to kBrowserFace.
inline void InstallFlBrowserGetters(drawfx::Fake &f)
{
    f.On(3, &FlBrowserGetWindow);
    f.On(14, &FlBrowserGetDefaultPath);
    f.On(21, &FlBrowserGetEntry);
    f.On(29, &FlBrowserGetDefaultSelection);
    f.On(36, &FlBrowserGetCorners);
    f.On(38, &FlBrowserGetDirectorySort);
    f.On(40, &FlBrowserGetBars);
    f.On(47, &FlBrowserGetColor<0>);
    f.On(48, &FlBrowserGetAlignment<0>);
    f.On(57, &FlBrowserGetColor<1>);
    f.On(58, &FlBrowserGetAlignment<1>);
    f.On(67, &FlBrowserGetColor<2>);
    f.On(68, &FlBrowserGetAlignment<2>);
    f.On(74, &FlBrowserGetIconWidth);
    f.On(76, &FlBrowserGetExtensionWidth);
    f.On(78, &FlBrowserGetRowHeight);
    f.On(80, &FlBrowserGetLineGrid);
    f.On(84, &FlBrowserGetColumnGrid);
    f.On(88, &FlBrowserGetHighlight);
    f.On(94, &FlBrowserGetBackDirText);
    f.On(96, &FlBrowserIsOutline);
    f.On(98, &FlBrowserGetDriveListSize);
    f.On(101, &FlBrowserGetBackground);
    f.On(106, &FlBrowserIsTransparent);
}

struct FlGridState {
    uint32_t flags = 0, flagNoise = 0; // IsFlagSet answers (flags & mask) != 0 in the low byte; flagNoise above it
    uint32_t fontColor = 0, outlineColor = 0, gridColors[2] = {};
    int32_t textAlign = 0;
    uint32_t wrapping = 0; // a word: only its low byte counts
    int32_t gutters[4] = {}, selection[2] = {}, defaultWidth = 0, defaultHeight = 0, headers[2] = {};
    uint32_t headerWrites = 3; // bits 0, 1: which header sizes GetHeaderSizes writes (the writer starts them at zero)
    void *window = nullptr;
};
inline FlGridState gFlGridState;

inline uint32_t FlGridGetWindow(drawfx::Fake &, const uint32_t *) { return uint32_t(uintptr_t(gFlGridState.window)); }
inline uint32_t FlGridIsFlagSet(drawfx::Fake &, const uint32_t *a)
{
    return ((gFlGridState.flags & a[0]) != 0 ? 1u : 0u) | (gFlGridState.flagNoise & ~0xffu);
}
inline uint32_t FlGridGetFontColor(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = gFlGridState.fontColor;
    return 0;
}
inline uint32_t FlGridGetTextWrapping(drawfx::Fake &, const uint32_t *) { return gFlGridState.wrapping; }
inline uint32_t FlGridGetGutters(drawfx::Fake &, const uint32_t *a)
{
    for (int i = 0; i < 4; ++i) *reinterpret_cast<int32_t *>(uintptr_t(a[i])) = gFlGridState.gutters[i];
    return 0;
}
inline uint32_t FlGridGetOutlineColor(drawfx::Fake &, const uint32_t *) { return gFlGridState.outlineColor; }
inline uint32_t FlGridGetSelection(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<int32_t *>(uintptr_t(a[0])) = gFlGridState.selection[0];
    *reinterpret_cast<int32_t *>(uintptr_t(a[1])) = gFlGridState.selection[1];
    return 0;
}
inline uint32_t FlGridGetGridColors(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = gFlGridState.gridColors[0];
    *reinterpret_cast<uint32_t *>(uintptr_t(a[1])) = gFlGridState.gridColors[1];
    return 0;
}
inline uint32_t FlGridGetTextAlign(drawfx::Fake &, const uint32_t *) { return uint32_t(gFlGridState.textAlign); }
inline uint32_t FlGridGetDefaultWidth(drawfx::Fake &, const uint32_t *) { return uint32_t(gFlGridState.defaultWidth); }
inline uint32_t FlGridGetDefaultHeight(drawfx::Fake &, const uint32_t *) { return uint32_t(gFlGridState.defaultHeight); }
inline uint32_t FlGridGetHeaderSizes(drawfx::Fake &, const uint32_t *a)
{
    for (int i = 0; i < 2; ++i)
        if (gFlGridState.headerWrites >> i & 1) *reinterpret_cast<int32_t *>(uintptr_t(a[i])) = gFlGridState.headers[i];
    return 0;
}
// Installs the getters on a face Reset to kGridFace.
inline void InstallFlGridGetters(drawfx::Fake &f)
{
    f.On(5, &FlGridGetWindow);
    f.On(13, &FlGridIsFlagSet);
    f.On(22, &FlGridGetFontColor);
    f.On(26, &FlGridGetTextWrapping);
    f.On(27, &FlGridGetGutters);
    f.On(33, &FlGridGetOutlineColor);
    f.On(44, &FlGridGetSelection);
    f.On(65, &FlGridGetGridColors);
    f.On(69, &FlGridGetTextAlign);
    f.On(74, &FlGridGetDefaultWidth);
    f.On(75, &FlGridGetDefaultHeight);
    f.On(81, &FlGridGetHeaderSizes);
}

} // namespace legacy_win_faces
