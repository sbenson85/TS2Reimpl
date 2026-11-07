// The legacy script writer's FileBrowser and Grid writers (lane WF, foundations): src/gz/cGZLegacyScriptWriter.h. Built for
// size, as the game's.
#include "gz/cGZLegacyScriptWriter.h"

#include "gz/cGZLegacyGridMatrix.h"
#include "gz/cGZLegacyHandlers.h"
#include "gz/cGZLegacyHandlersInternal.h" // GetGraphicSystem, UIDefaults
#include "gz/cGZLegacyScriptWriterInternal.h"
#include "ifaces/cIGZWin.h"
#include "rz/cIGZVariant.h"
#include "rz/cRZUnknownAutoPtr.h"

#include <cstdint>

namespace {

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

// "<name>=text ": both are the game's literals.
void TextAttribute(cRZString *out, const char *name, const char *value)
{
    Text text(value);
    AppendTextAttribute(out, Of(Text(name)), Of(text));
}

void IntAttribute(cRZString *out, const char *name, int32_t value) { AppendIntAttribute(out, Of(Text(name)), value); }

// "<name>=(r,g,b) " of a native colour, through the graphics system; the bytes are the caller's.
void NativeColor(cRZString *out, const char *name, uint32_t native)
{
    uint8_t red, green, blue;
    GetGraphicSystem()->ConvertNativeToRGB(native, &red, &green, &blue);
    AppendColorAttribute(out, Of(Text(name)), red, green, blue);
}

// "yes" for a byte of exactly 1, else "no".
void YesNo(cRZString *out, const char *name, uint8_t answer) { TextAttribute(out, name, answer == 1 ? "yes" : "no"); }

// ---- FileBrowser ------------------------------------------------------------------------------------------------------

// The names of one section's attributes (file name, extension, drive name): the font, six colours, the alignment.
struct Section {
    const char *font;
    const char *colors[6];
    const char *alignment;
};
constexpr Section kSections[3] = {
    {"filenamefont",
     {"filenameforegroundcolor", "filenamebackgroundcolor", "filenameforegroundcolordisabled", "filenamebackgroundcolordisabled",
      "filenameforegroundcolorhilited", "filenamebackgroundcolorhilited"},
     "filenamealignment"},
    {"fileextensionfont",
     {"fileextensionforegroundcolor", "fileextensionbackgroundcolor", "fileextensionforegroundcolordisabled",
      "fileextensionbackgroundcolordisabled", "fileextensionforegroundcolorhilited", "fileextensionbackgroundcolorhilited"},
     "fileextensionalignment"},
    {"drivenamefont",
     {"drivenameforegroundcolor", "drivenamebackgroundcolor", "drivenameforegroundcolordisabled",
      "drivenamebackgroundcolordisabled", "drivenameforegroundcolorhilited", "drivenamebackgroundcolorhilited"},
     "drivenamealignment"},
};

// The sort and column words by their number: null for a number the game does not name.
const char *Pick(int32_t value, const char *const *names, int32_t count)
{
    return uint32_t(value) < uint32_t(count) ? names[value] : nullptr;
}

void WriteSortAttribute(cRZString *out, const char *name, int32_t value)
{
    static const char *const kNames[4] = {"bynameascending", "bynamedescending", "byextensionascending",
                                          "byextensiondescending"};
    if (const char *word = Pick(value, kNames, 4)) TextAttribute(out, name, word);
}

void WriteColumnAttribute(cRZString *out, const char *name, int32_t value)
{
    const char *word;
    switch (value) {
    case 0: word = "icon"; break;
    case 1: word = "name"; break;
    case 2: word = "extension"; break;
    case 5: word = "empty"; break;
    default: return;
    }
    TextAttribute(out, name, word);
}

} // namespace

void __cdecl WriteFileBrowserAttributes(cIGZWinFileBrowser *face, cRZString *out)
{
    {
        LegacyText path;
        face->GetDefaultPath(&path.mText);
        AppendQuotedAttribute(out, Of(Text("defaultpath")), &path.mText);
    }
    {
        LegacyText name, extension;
        int32_t type;
        face->GetDefaultSelection(&type, &name.mText, &extension.mText);
        switch (type) {
        case 0: TextAttribute(out, "defaultselectiontype", "backdir"); break;
        case 1: TextAttribute(out, "defaultselectiontype", "dir"); break;
        case 2: TextAttribute(out, "defaultselectiontype", "file"); break;
        default: break;
        }
        AppendQuotedAttribute(out, Of(Text("defaultselectionname")), &name.mText);
        AppendQuotedAttribute(out, Of(Text("defaultselectionextension")), &extension.mText);
    }
    {
        uint32_t primary, secondary;
        face->GetCorners(&primary, &secondary);
        WriteSortAttribute(out, "filesortprimary", int32_t(primary));
        WriteSortAttribute(out, "filesortsecondary", int32_t(secondary));
    }
    {
        int32_t sort;
        face->GetDirectorySort(&sort);
        static const char *const kNames[4] = {"topascending", "topdescending", "bottomascending", "bottomdescending"};
        if (const char *word = Pick(sort, kNames, 4)) TextAttribute(out, "directorysort", word);
    }
    {
        uint32_t first, second, third;
        face->GetBars(&first, &second, &third);
        WriteColumnAttribute(out, "column1data", int32_t(first));
        WriteColumnAttribute(out, "column2data", int32_t(second));
        WriteColumnAttribute(out, "column3data", int32_t(third));
    }

    // The three sections. One text for the font of them all.
    LegacyText font;
    for (int32_t index = 0; index < 3; ++index) {
        const Section &section = kSections[index];
        if (FormatFontProperty(AsWin(face->GetWindow()), font.mText.AsInterface(), index))
            AppendTextAttribute(out, Of(Text(section.font)), &font.mText);
        for (uint32_t color = 0; color < 6; ++color) {
            const uint32_t native = index == 0   ? face->GetFileNameColor(color)
                                    : index == 1 ? face->GetFileExtensionColor(color)
                                                 : face->GetDriveNameColor(color);
            NativeColor(out, section.colors[color], native);
        }
        const int32_t alignment = index == 0   ? face->GetFileNameAlignment()
                                  : index == 1 ? face->GetFileExtensionAlignment()
                                               : face->GetDriveNameAlignment();
        static const char *const kAlignments[5] = {"unused", "default", "left", "center", "right"};
        if (const char *word = Pick(alignment, kAlignments, 5)) TextAttribute(out, section.alignment, word);
    }

    IntAttribute(out, "iconcolumnwidth", face->GetIconColumnWidth());
    IntAttribute(out, "extensioncolumnwidth", face->GetExtensionColumnWidth());
    IntAttribute(out, "rowheight", face->GetRowHeight());
    {
        bool enabled;
        uint32_t color;
        face->GetSelection(&enabled, &color);
        TextAttribute(out, "linegridenabled", uint8_t(enabled) == 1 ? "yes" : "no");
        NativeColor(out, "linegridcolor", color);
    }
    {
        bool enabled;
        uint32_t color;
        face->GetCursor(&enabled, &color);
        TextAttribute(out, "columngridenabled", uint8_t(enabled) == 1 ? "yes" : "no");
        NativeColor(out, "columngridcolor", color);
    }
    {
        uint32_t foreground, background;
        face->GetBorder(&foreground, &background);
        NativeColor(out, "highlightcolorforeground", foreground);
        NativeColor(out, "highlightcolorbackground", background);
    }
    {
        LegacyText text;
        face->GetBackDirText(&text.mText);
        AppendQuotedAttribute(out, Of(Text("backdirtext")), &text.mText);
    }
    IntAttribute(out, "drivelistsize", face->GetDriveListSize());
    NativeColor(out, "backgroundcolor", face->GetBackgroundColor());
    YesNo(out, "backgroundtransparent", face->IsBackgroundTransparent());
    YesNo(out, "outline", face->IsOutline());

    uint32_t key[4] = {0, 0, 0, 0};
    if (GetKeyProperty(AsWin(face->GetWindow()), 0xec64c1b3, key)) AppendKeyAttribute(out, Of(Text("backgroundimage")), key[1], key[2]);

    // The entries: each one's name, number and key. The answer of the key list is not looked at.
    LegacyText name;
    uint32_t unused;
    int32_t number;
    for (uint32_t index = 0; face->GetEntry(index, &name.mText, &unused, &number, 0); ++index) {
        key[1] = 0;
        key[2] = 0;
        GetKeyListEntry(AsWin(face->GetWindow()), 0x2c64e5c4, int32_t(index), key);
        LegacyText line;
        line.mText.Sprintf("%s:%d:{%08x,%08x}", name.mText.mString.c_str(), number, key[1], key[2]);
        AppendQuotedAttribute(out, Of(Text("filter")), &line.mText);
    }
}

// ---- Grid -------------------------------------------------------------------------------------------------------------

namespace {

constexpr uint32_t kCellMatrixProperty = 0x287a7136;
constexpr uint32_t kColumnMatrixProperty = 0xc88ccb6d;
constexpr uint32_t kRowMatrixProperty = 0xc88ccb6e;
constexpr uint32_t kCellText = 0x40000;
constexpr uint32_t kCellFont = 0x1000;

// The matrix the window keeps under a property (a variant of an unknown whose id-1 interface is the holder), handed to
// `visit`; the holder and the variant are released after it. No guard for a holder without a matrix (the original would fault).
template <class Holder, class Visit> void WithMatrix(ifaces::cIGZWin *window, uint32_t property, Visit visit)
{
    cIGZVariant *variant = nullptr;
    if (!window->GetProperty(property, &variant)) return;
    Holder *holder = nullptr;
    if ((variant->GetType() & 0x10) && variant->GetValUnknown(1, reinterpret_cast<void **>(&holder))) {
        visit(*holder->mMatrix);
        holder->Release();
    }
    variant->Release();
}

struct FlagName {
    uint32_t mask;
    const char *name;
};
constexpr FlagName kGridFlags[] = {
    {0, "fnone"},           {1, "fedit"},           {2, "fhscroll"},        {4, "fvscroll"},       {8, "fcolheading"},
    {0x10, "frowheading"},  {0x20, "fcolgrid"},     {0x40, "frowgrid"},     {0x80, "finsmode"},    {0x100, "fopqbkgnd"},
    {0x200, "fdoutline"},   {0x400, "fdgridoutline"}, {0x800, "fcellwrap"}, {0x1000, "fallownosel"}, {0x2000, "ffixcolcnt"},
    {0x4000, "ffixrowcnt"}, {0x8000, "fdpastlastcol"}, {0x10000, "fdpastlastrow"}, {0x20000, "fcelloverlap"},
    {0x40000, "fdrpdnmenu"}, {0x80000, "fhlcelldata"}, {0x19bff, "fdefault"}, {0xffffffff, "fall"},
};

} // namespace

void __cdecl WriteGridAttributes(cIGZWinGrid *face, cRZString *out)
{
    {
        LegacyText font;
        if (FormatFontProperty(face->GetWindow(), font.mText.AsInterface(), 0)) AppendTextAttribute(out, Of(Text("font")), &font.mText);
    }
    {
        uint32_t native;
        face->GetFontColor(&native, 0);
        NativeColor(out, "fontcolor", native);
    }
    IntAttribute(out, "textalign", face->GetTextAlign());
    IntAttribute(out, "textwrapping", face->GetTextWrapping() ? 1 : 0);
    {
        int32_t left, top, right, bottom;
        face->GetGutters(&left, &top, &right, &bottom);
        AppendRectAttribute(out, Of(Text("gutters")), left, top, right, bottom);
    }
    {
        int32_t rule, maximum;
        face->GetSelection(&rule, &maximum);
        IntAttribute(out, "selrule", rule);
        IntAttribute(out, "maxselcount", maximum);
    }
    NativeColor(out, "olinecolor", face->GetOutlineColor());
    IntAttribute(out, "dcolwidth", face->GetDefaultColumnWidth());
    IntAttribute(out, "drowheight", face->GetDefaultRowHeight());
    // The two header flags are asked and the answers dropped.
    face->IsFlagSet(8);
    face->IsFlagSet(0x10);
    {
        int32_t column = 0, row = 0;
        face->GetHeaderSizes(&column, &row);
        IntAttribute(out, "colhdrsz", column);
        IntAttribute(out, "rowhdrsz", row);
    }
    for (const FlagName &flag : kGridFlags) FlagAttribute(out, flag.name, face->IsFlagSet(flag.mask));
    {
        uint32_t column, row;
        face->GetGridColors(&column, &row);
        NativeColor(out, "colgridclr", column);
        NativeColor(out, "rowgridclr", row);
    }

    WithMatrix<cRZUnknownAutoPtrWinGridCellData>(face->GetWindow(), kCellMatrixProperty, [&](legacygrid::CellMatrix &cells) {
        for (auto &line : cells.mLines) {
            for (auto &entry : line.second.mEntries) {
                const legacygrid::GridCell &cell = entry.second.mValue;
                LegacyText data;
                if (!(cell.mFlags & kCellText))
                    data.mText.Sprintf("%08x,%08x", cell.mGroup, cell.mInstance);
                else
                    data.mText.Sprintf("%s", cell.mText.mString.c_str());
                auto *fontEntry = static_cast<cIGZUIFontEntry *>(UIDefaults()->FindFontEntry(cell.mFontId));
                LegacyText result;
                if (cell.mFlags & kCellFont) {
                    result.mText.Sprintf("%d,%d,%08x,%s,%s,%08x,%08x,%08x,%08x,%08x", entry.second.mIndex, line.second.mIndex,
                                         cell.mFlags, data.Chars(), fontEntry ? fontEntry->GetName() : "NONAME", cell.mFontWords[0],
                                         cell.mFontWords[1], cell.mFontWords[2], cell.mFontWords[3], cell.mFontWords[4]);
                } else {
                    result.mText.Sprintf("%d,%d,%08x,%s", entry.second.mIndex, line.second.mIndex, cell.mFlags, data.Chars());
                }
                AppendQuotedAttribute(out, Of(Text("wingridcell")), &result.mText);
            }
        }
    });
    WithMatrix<cRZUnknownAutoPtrWinGridColData>(face->GetWindow(), kColumnMatrixProperty, [&](legacygrid::LineMatrix &columns) {
        for (auto &line : columns.mLines) {
            for (auto &entry : line.second.mEntries) {
                LegacyText result;
                result.mText.Sprintf("%d,%d,%d ", entry.second.mIndex, entry.second.mValue.mLast, entry.second.mValue.mValue);
                AppendQuotedAttribute(out, Of(Text("wingridcol")), &result.mText);
            }
        }
    });
    WithMatrix<cRZUnknownAutoPtrWinGridRowData>(face->GetWindow(), kRowMatrixProperty, [&](legacygrid::LineMatrix &rows) {
        for (auto &line : rows.mLines) {
            for (auto &entry : line.second.mEntries) {
                LegacyText result;
                result.mText.Sprintf("%d,%d,%d ", line.second.mIndex, entry.second.mValue.mLast, entry.second.mValue.mValue);
                AppendQuotedAttribute(out, Of(Text("wingridrow")), &result.mText);
            }
        }
    });
}
