// The window classes' handlers of cGZLegacyControl, F6 part 5 (lane MK), continued: the grid. Built for size, as the game's.
// src/gz/cGZLegacyHandlers.h has the faces and the handlers' notes.
#include "gz/cGZLegacyHandlersInternal.h"

#include "game/game.h"
#include "game/vtable.h"
#include "gz/cGZLegacyGridMatrix.h"
#include "ifaces/cIGZWin.h"
#include "rz/cIGZVariant.h"
#include "rz/cRZString.h"
#include "rz/cRZUnknownAutoPtr.h"
#include "rz/cRZVariant.h"

#include <cstdio>
#include <new>

#pragma warning(disable : 4996) // sscanf, as the game calls it

using namespace legacyhandlers;

uint32_t WindowNativeColor(ifaces::cIGZWin *window, uint8_t red, uint8_t green, uint8_t blue)
{
    return VirtualSlot<uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t)>(window, 121)(window, red, green, blue);
}

namespace {

// ---- GZWinGrid's attributes (RegisterGrid 0x867000..) ----
constexpr uint32_t kHeaderColorAttribute = 0x1601;
constexpr uint32_t kValue66Attribute = 0x1602;
constexpr uint32_t kFlag800Attribute = 0x1603;
constexpr uint32_t kHighlightColorAttribute = 0x1604;
constexpr uint32_t kSpacingFirstAttribute = 0x1605;
constexpr uint32_t kSpacingSecondAttribute = 0x1606;
constexpr uint32_t kSizeAAttribute = 0x1607;
constexpr uint32_t kSizeBAttribute = 0x1608;
constexpr uint32_t kScrollbarsAttribute = 0x1609;
constexpr uint32_t kOffsetFirstAttribute = 0x160a;
constexpr uint32_t kOffsetSecondAttribute = 0x160b;
constexpr uint32_t kHighlightModeAttribute = 0x1616;
constexpr uint32_t kBackColorFirstAttribute = 0x1623;
constexpr uint32_t kBackColorSecondAttribute = 0x1624;
constexpr uint32_t kCellAttribute = 0x1625;
constexpr uint32_t kColumnAttribute = 0x1626;
constexpr uint32_t kRowAttribute = 0x1627;
constexpr uint32_t kDefaultFont = 0x68963c4c; // the UI defaults service's font when the grid names none
constexpr uint32_t kDefaultSizeA = 100;
constexpr uint32_t kDefaultSizeB = 20;
constexpr int32_t kDefaultSpacingFirst = 2;
constexpr int32_t kDefaultSpacingSecond = 1;
constexpr uint32_t kHighlightOn = 1;
constexpr uint32_t kInvalidateFlag = 4; // clearing this flag also invalidates the window

// The flag word's bits, one attribute each (0x160c ..).
struct GridFlag {
    uint32_t attribute;
    uint32_t mask;
};
constexpr GridFlag kGridFlags[] = {
    {0x160c, 0x0},
    {0x160d, 0x1},
    {0x160e, 0x2},
    {0x160f, 0x4},
    {0x1610, 0x8},
    {0x1611, 0x10},
    {0x1612, 0x20},
    {0x1613, 0x40},
    {0x1614, 0x80},
    {0x1615, 0x100},
    {0x1616, 0x200},
    {0x1617, 0x400},
    {0x1618, 0x800},
    {0x1619, 0x1000},
    {0x161a, 0x2000},
    {0x161b, 0x4000},
    {0x161c, 0x8000},
    {0x161d, 0x10000},
    {0x161e, 0x20000},
    {0x161f, 0x40000},
    {0x1620, 0x80000},
    {0x1621, 0x19bff},
    {0x1622, 0xffffffff},
};

// The cell flags (the third field of a cell text) and the cell's data.
constexpr uint32_t kCellFont = 0x1000;
constexpr uint32_t kCellImage = 0x2000;
constexpr uint32_t kCellLocalizedText = 0x10000;
constexpr uint32_t kCellText = 0x40000;
constexpr uint32_t kCellFlagTwo = 0x80000;
constexpr uint32_t kCellStyleBit = 0x100000;
constexpr uint8_t kStyleOn = 1;
constexpr uint8_t kStyleOff = 0xff;

// The matrices kept on the window as properties, each in a counted holder (rz/cRZUnknownAutoPtr.h, ours: the matrix, a
// count, whether it owns the matrix).
constexpr uint32_t kCellMatrixProperty = 0x287a7136;
constexpr uint32_t kColumnMatrixProperty = 0xc88ccb6d;
constexpr uint32_t kRowMatrixProperty = 0xc88ccb6e;
constexpr size_t kMatrixSize = 0x20;

using legacygrid::CellMatrix;
using legacygrid::GridCell;
using legacygrid::GridLineData;
using legacygrid::LineMatrix;

// A new empty matrix, kept on the window (as a variant of its holder) under `property`.
template <class Holder, class Matrix> Matrix *NewMatrix(ifaces::cIGZWin *window, uint32_t property)
{
    static_assert(sizeof(Matrix) == kMatrixSize, "matrix size");
    Matrix *const matrix = new Matrix;
    Holder *const holder = new Holder(matrix);
    cIGZUnknown *const unknown = holder;
    unknown->AddRef();
    cRZVariant *const variant = new cRZVariant;
    variant->SetValUnknown(unknown);
    window->SetProperty(property, variant);
    unknown->Release();
    return matrix;
}

uint32_t WindowColor(cIGZWinGrid *face, uint8_t red, uint8_t green, uint8_t blue)
{
    return WindowNativeColor(face->GetWindow(), red, green, blue);
}

// One cell attribute's text: its column and row, flags and data, shown in the face and kept in the cell matrix.
// `scanFlags` is the value the original tests (a leftover of the flag attributes) to decide whether to read the resource.
void ApplyGridCells(ifaces::cIGZWin *window, const cGZScriptAttributes *attributes, cIGZWinGrid *face, int32_t scanFlags)
{
    LegacyText text, resource;
    CellMatrix *matrix = nullptr;
    // ORIGINAL-BUG: the original never sets these before the first scan, so a cell that needs them without a scan (or after
    // a failed one) uses stack leftovers; zeros here.
    uint32_t group = 0, instance = 0;
    const uint32_t count = attributes->Size();
    for (uint32_t i = 0; i < count; ++i) {
        const cGZScriptAttribute &attribute = attributes->mBegin[i];
        if (attribute.mId != kCellAttribute) continue;
        cIGZVariant *const item = reinterpret_cast<cIGZVariant *>(attribute.mValue);
        if (item->GetType() != kTextVariantType) continue;
        item->GetValString(*text.mText.AsInterface(), 0, kTextSeparator);

        GridCell cell;
        uint32_t column = 0, row = 0, flags = 0;
        if (!ParseGridCell(&text.mText, &column, &row, &flags, &resource.mText, &cell.mFontId, &cell.mFontWords[0],
                           &cell.mFontWords[1], &cell.mFontWords[2], &cell.mFontWords[3], &cell.mFontWords[4]))
            continue;
        // ORIGINAL-BUG: the test is on the last flag attribute's value (the same stack slot), not on the cell's flags.
        if (!(scanFlags & kCellText)) std::sscanf(resource.Chars(), "%08x,%08x", &group, &instance);
        if (!matrix) matrix = NewMatrix<cRZUnknownAutoPtrWinGridCellData, CellMatrix>(window, kCellMatrixProperty);
        cell.mFlags = flags;
        if (flags & kCellText) {
            cell.mText.Sprintf("%s", resource.Chars());
            face->SetCellText(column, row, cell.mText.ToChar(), cell.mText.Strlen(), true, (flags & kCellFlagTwo) != 0);
        } else {
            cell.mGroup = group;
            cell.mInstance = instance;
            if (flags & kCellImage) {
                cIGZLegacyImage *const image = LoadImageResource(group, instance);
                if (image) {
                    face->SetCellImage(column, row, image, true);
                    image->Release();
                }
            } else if (flags & kCellLocalizedText) {
                LegacyText localized;
                if (LoadLocalizedText(group, instance, localized.mText.AsInterface()))
                    face->SetCellText(column, row, localized.Chars(), localized.mText.Strlen(), true,
                                      (flags & kCellFlagTwo) != 0);
            }
        }
        if (flags & kCellFont) {
            GridCellFont font;
            if (cell.mFontId) font.font = UIDefaults()->GetFont(cell.mFontId);
            font.words[0] = cell.mFontWords[0];
            font.style[1] = uint8_t(cell.mFontWords[1]);
            font.style[2] = uint8_t(cell.mFontWords[2]);
            font.style[0] = (flags & kCellStyleBit) ? kStyleOn : kStyleOff;
            font.style[3] = uint8_t(cell.mFontWords[3]);
            font.last = cell.mFontWords[4];
            face->SetCellFont(column, row, &font);
        }
        matrix->Set(column, row, &cell, nullptr);
    }
}

// The column attributes (0x1626) or the row attributes (0x1627), "first,last,value": kept in a matrix of their own and
// shown in the face.
template <bool Rows>
void ApplyGridLines(ifaces::cIGZWin *window, const cGZScriptAttributes *attributes, cIGZWinGrid *face)
{
    constexpr uint32_t attributeId = Rows ? kRowAttribute : kColumnAttribute;
    LegacyText text;
    LineMatrix *matrix = nullptr;
    const uint32_t count = attributes->Size();
    for (uint32_t i = 0; i < count; ++i) {
        const cGZScriptAttribute &attribute = attributes->mBegin[i];
        if (attribute.mId != attributeId) continue;
        cIGZVariant *const item = reinterpret_cast<cIGZVariant *>(attribute.mValue);
        if (item->GetType() != kTextVariantType) continue;
        item->GetValString(*text.mText.AsInterface(), 0, kTextSeparator);
        int32_t data[3] = {int32_t(0xdeadbeef), int32_t(0xdeadbeef), 0};
        if (std::sscanf(text.Chars(), "%d,%d,%d", &data[0], &data[1], &data[2]) != 3) continue;
        if (!matrix)
            matrix = Rows ? NewMatrix<cRZUnknownAutoPtrWinGridRowData, LineMatrix>(window, kRowMatrixProperty)
                          : NewMatrix<cRZUnknownAutoPtrWinGridColData, LineMatrix>(window, kColumnMatrixProperty);
        // The entry's key is the first number; a column puts it in the first slot, a row in the second.
        const GridLineData line = {data[0], data[1], data[2]};
        if (Rows)
            matrix->Set(0, data[0], &line, nullptr);
        else
            matrix->Set(data[0], 0, &line, nullptr);
        if (Rows)
            face->SetRows(data[0], data[1] - data[0] + 1, data[2]);
        else
            face->SetColumn(data[0], 1, data[2]);
    }
}

void ApplyGridFirstPass(ifaces::cIGZWin *window, const cGZScriptAttributes *attributes, cIGZWinGrid *face)
{
    uint8_t red, green, blue;
    if (FindColorAttribute(attributes, kHeaderColorAttribute, &red, &green, &blue))
        face->SetColor(WindowColor(face, red, green, blue), 0);

    LegacyText fontText;
    uint32_t font;
    if (FindUint32Attribute(attributes, legacyattr::kFont, &font)) {
        face->SetFont(UIDefaults()->GetFont(font));
        SetUint32WindowProperty(window, font, 0);
    } else if (FindTextAttribute(attributes, legacyattr::kFont, &fontText.mText)) {
        SetTextWindowProperty(window, &fontText.mText, 0);
    } else {
        face->SetFont(UIDefaults()->GetFont(kDefaultFont));
    }

    int32_t number;
    if (FindSint32Attribute(attributes, kValue66Attribute, &number)) face->SetValue66(number);

    uint32_t value = 0;
    const bool found = FindUint32Attribute(attributes, kFlag800Attribute, &value);
    face->SetFlag800(found && value != 0);

    int32_t rect[4];
    if (FindRectAttribute(attributes, legacyattr::kGutters, rect))
        face->SetMargins(rect[0], rect[1], rect[2], rect[3]);
    else
        face->SetMargins(0, 0, 0, 0);

    int32_t spacingFirst = kDefaultSpacingFirst, spacingSecond = kDefaultSpacingSecond;
    FindSint32Attribute(attributes, kSpacingFirstAttribute, &spacingFirst);
    FindSint32Attribute(attributes, kSpacingSecondAttribute, &spacingSecond);
    face->SetSpacing(spacingFirst, spacingSecond);

    uint32_t highlight = 0;
    FindUint32Attribute(attributes, kHighlightModeAttribute, &highlight);
    red = green = blue = 0;
    FindColorAttribute(attributes, kHighlightColorAttribute, &red, &green, &blue);
    face->SetHighlight(highlight == kHighlightOn, WindowColor(face, red, green, blue));

    int32_t sizeA = kDefaultSizeA, sizeB = kDefaultSizeB;
    FindSint32Attribute(attributes, kSizeAAttribute, &sizeA);
    face->SetSizeA(sizeA);
    FindSint32Attribute(attributes, kSizeBAttribute, &sizeB);
    face->SetSizeB(sizeB);

    // ORIGINAL-BUG: the result is not read, so without the attribute the first size is taken for the scrollbar mode.
    uint32_t scrollbars = uint32_t(sizeA);
    FindUint32Attribute(attributes, kScrollbarsAttribute, &scrollbars);
    switch (scrollbars) {
    case 0:
        face->SetScrollbars(true, true);
        break;
    case 1:
        face->SetScrollbars(true, false);
        break;
    case 2:
        face->SetScrollbars(false, true);
        break;
    default:
        face->SetScrollbars(false, false);
        break;
    }

    int32_t offsetFirst = 0, offsetSecond = 0;
    FindSint32Attribute(attributes, kOffsetFirstAttribute, &offsetFirst);
    FindSint32Attribute(attributes, kOffsetSecondAttribute, &offsetSecond);
    face->SetOffset(offsetFirst, offsetSecond);

    // One number serves every flag attribute (the cell attributes read it later).
    // ORIGINAL-BUG: the original leaves it unset when no flag attribute is found, so the cells then test stack leftovers.
    int32_t flagValue = 0;
    for (const GridFlag &flag : kGridFlags) {
        if (!FindSint32Attribute(attributes, flag.attribute, &flagValue)) continue;
        if (flagValue != 0) {
            face->SetFlags(flag.mask);
        } else {
            face->ClearFlags(flag.mask);
            if (flag.mask == kInvalidateFlag) face->GetWindow()->Invalidate();
        }
    }

    ApplyGridCells(window, attributes, face, flagValue);
    ApplyGridLines<false>(window, attributes, face);
    ApplyGridLines<true>(window, attributes, face);
}

} // namespace

bool __cdecl ApplyGridProperties(ifaces::cIGZWin *window, cGZScriptAttributes *attributes, bool apply)
{
    cIGZWinGrid *face = nullptr;
    if (window->QueryInterface(cIGZWinGrid::kIID, reinterpret_cast<void **>(&face))) {
        if (apply) {
            ApplyGridFirstPass(window, attributes, face);
        } else {
            uint8_t red = 0, green = 0, blue = 0;
            FindColorAttribute(attributes, kBackColorFirstAttribute, &red, &green, &blue);
            const uint32_t first = WindowColor(face, red, green, blue);
            red = green = blue = 0;
            FindColorAttribute(attributes, kBackColorSecondAttribute, &red, &green, &blue);
            const uint32_t second = WindowColor(face, red, green, blue);
            face->SetBackColors(first, second);
        }
    }
    if (face) face->Release();
    return true;
}
