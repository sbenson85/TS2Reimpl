// The legacy script writer's dispatcher and class header (lane FI, foundations): src/gz/cGZLegacyScriptWriter.h. Built for
// size, as the game's.
#include "gz/cGZLegacyScriptWriter.h"

#include "game/game.h"
#include "game/original.h"
#include "gz/cGZLegacyControl.h" // LegacyText
#include "gz/cGZLegacyHandlers.h" // the window classes' faces
#include "gz/cIGZUnknown.h"
#include "ifaces/cIGZWin.h"
#include "rz/cIGZVariant.h"

namespace {

constexpr uint32_t kClassIdProperty = 0xe85eebde;
constexpr uint32_t kInterfaceIdProperty = 0x08d6e462;
constexpr uint32_t kDefaultClassId = 0xe2ba00ee; // GZWin
constexpr uint32_t kDefaultInterfaceId = 0x22ba0121;

// The legacy window classes, in the game's order (its table of class id, interface id and name): the header asks the
// window for each interface in turn, and names the ids it writes from it.
struct LegacyClass {
    uint32_t clsid;
    uint32_t iid;
    const char *name;
};
constexpr LegacyClass kClasses[] = {
    {0xc2afa76e, 0xc2afa76f, "GZWinFlatRect"},   {0x00000592, 0x212cdc1f, "GZWinText"},
    {0x231a1c58, 0x231a1c57, "GZWinTextEdit"},   {0x00000598, 0x4132242b, "GZWinListBox"},
    {0x21335c5a, 0x21335c59, "GZWinLineInput"},  {0x0000059b, 0x412ce496, "GZWinCombo"},
    {0x22ecfc47, 0x00008810, "GZWinBtn"},        {0x82fe68c4, 0xc12cea13, "GZWinBMP"},
    {0x61325a2e, 0x61325a2d, "GZWinScrollbar"},  {0x21325208, 0x21325207, "GZWinSlider"},
    {0xa1336cc1, 0xa1336cc0, "GZWinOptGrp"},     {0x612ce0c4, 0x612ce0c3, "GZWinSpinner"},
    {0x4303e5b6, 0xe303e5ae, "GZWinOutline"},    {0xe32f0b31, 0x032f0b3c, "GZWinTextTicker"},
    {0x4386d516, 0x5386d516, "GZWinGen"},        {0x1aa52ea4, 0x3aa52e64, "GZWinFileBrowser"},
    {0x3ae8bae1, 0x3ae8bad2, "GZWinTreeView"},   {0x078e8055, 0xa13351b8, "GZWinFolders"},
    {0xdaa6b9bf, 0xdaa6b9be, "GZWinGrid"},       {0x4b42da5d, 0x4b42da53, "GZWinScrollbar2"},
    {0x478d1e6f, 0x678d1e84, "GZWinCustom"},     {kDefaultClassId, kDefaultInterfaceId, "GZWin"},
};

// A uint32 property of the window, 0 without it (or when it is 0).
uint32_t IdProperty(ifaces::cIGZWin *window, uint32_t id)
{
    cIGZVariant *value = nullptr;
    if (!window->GetProperty(id, &value)) return 0;
    const uint32_t result = *value->AsUint32Ref() ? *value->AsUint32Ref() : 0;
    value->Release();
    return result;
}

using ClassWriterFn = void(__cdecl *)(void *face, cRZString *out);

// Whether the class of an interface id has a writer (not GZWin, GZWinCustom or an unknown id).
bool HasClassWriter(uint32_t iid)
{
    switch (iid) {
    case 0x21335c59: // LineInput
    case 0x21325207: // Slider
    case 0x212cdc1f: // Text
    case 0x032f0b3c: // TextTicker (ours)
    case 0x00008810: // Btn
    case 0x231a1c57: // TextEdit
    case 0x4132242b: // ListBox
    case 0x412ce496: // Combo
    case 0x3ae8bad2: // TreeView
    case 0x3aa52e64: // FileBrowser
    case 0x4b42da53: // Scrollbar2 (ours)
    case 0xa13351b8: // Folders (ours)
    case 0x61325a2d: // Scrollbar (ours)
    case 0x612ce0c3: // Spinner
    case 0x5386d516: // Gen
    case 0xa1336cc0: // OptGrp
    case 0xe303e5ae: // Outline (ours)
    case 0xdaa6b9be: // Grid
    case 0xc2afa76f: // FlatRect (ours)
    case 0xc12cea13: // BMP (ours)
        return true;
    default:
        return false;
    }
}

// Runs the writer of the class of an interface id on that face. The ones not yet ours are lanes FJ..FL's.
void RunClassWriter(uint32_t iid, void *face, cRZString *out)
{
    switch (iid) {
    case 0x21335c59: WriteLineInputAttributes(static_cast<cIGZWinLineInput *>(face), out); break; // LineInput 0x87551d
    case 0x21325207: WriteSliderAttributes(static_cast<cIGZWinSlider *>(face), out); break; // Slider 0x8777a6
    case 0x212cdc1f: WriteTextAttributes(static_cast<cIGZWinText *>(face), out); break; // Text 0x87822e
    case 0x032f0b3c: WriteTextTickerAttributes(static_cast<cIGZWinTextTicker *>(face), out); break; // TextTicker 0x8791f3
    case 0x00008810: WriteBtnAttributes(static_cast<cIGZWinButton *>(face), out); break; // Btn 0x86ff03
    case 0x231a1c57: WriteTextEditAttributes(static_cast<cIGZWinTextEdit *>(face), out); break; // TextEdit 0x878762
    case 0x4132242b: WriteListBoxAttributes(static_cast<cIGZWinListBox *>(face), out); break; // ListBox 0x875ccb
    case 0x412ce496: WriteComboAttributes(static_cast<cIGZWinCombo *>(face), out); break; // Combo 0x870b57
    case 0x3ae8bad2: WriteTreeViewAttributes(static_cast<cIGZWinTreeView *>(face), out); break; // TreeView 0x8792f2
    case 0x3aa52e64: WriteFileBrowserAttributes(static_cast<cIGZWinFileBrowser *>(face), out); break; // FileBrowser
    case 0x4b42da53: WriteScrollbar2Attributes(static_cast<cIGZWinScrollbar2 *>(face), out); break; // Scrollbar2 0x877189
    case 0xa13351b8: WriteFoldersAttributes(static_cast<cIGZWinFolders *>(face), out); break; // Folders 0x872f45
    case 0x61325a2d: WriteScrollbarAttributes(static_cast<cIGZWinScrollbar *>(face), out); break; // Scrollbar 0x87751e
    case 0x612ce0c3: WriteSpinnerAttributes(static_cast<cIGZWinSpinner *>(face), out); break; // Spinner 0x877979
    case 0x5386d516: WriteGenAttributes(static_cast<cIGZWinGen *>(face), out); break; // Gen 0x8730b3
    case 0xa1336cc0: WriteOptGrpAttributes(static_cast<cIGZWinOptGrp *>(face), out); break; // OptGrp 0x8764bf
    case 0xe303e5ae: WriteOutlineAttributes(static_cast<cIGZWinOutline *>(face), out); break; // Outline 0x876ea4
    case 0xdaa6b9be: WriteGridAttributes(static_cast<cIGZWinGrid *>(face), out); break; // Grid
    case 0xc2afa76f: WriteFlatRectAttributes(static_cast<cIGZWinFlatRect *>(face), out); break; // FlatRect 0x872b33
    case 0xc12cea13: WriteBitmapAttributes(static_cast<cIGZWinBitmap *>(face), out); break; // BMP 0x86fc7d
    }
}

} // namespace

uint32_t __cdecl WriteLegacyClassHeader(ifaces::cIGZWin *window, cRZString *out)
{
    uint32_t clsid = IdProperty(window, kClassIdProperty);
    uint32_t iid = IdProperty(window, kInterfaceIdProperty);
    if (!clsid || !iid) {
        // The first class whose interface the window answers fills what is missing. The answer is not kept: the
        // window's own count is released, as the original does.
        void *face = nullptr;
        for (const LegacyClass &c : kClasses) {
            if (!window->QueryInterface(c.iid, &face)) continue;
            window->Release();
            if (!iid) iid = c.iid;
            if (!clsid) clsid = c.clsid;
        }
        if (!iid) iid = kDefaultInterfaceId;
        if (!clsid) clsid = kDefaultClassId;
    }
    LegacyText iidHolder, clsidHolder;
    cRZString &iidText = iidHolder.mText, &clsidText = clsidHolder.mText;
    iidText.Sprintf("0x%08lx", iid);
    clsidText.Sprintf("0x%08lx", clsid);
    for (const LegacyClass &c : kClasses) {
        if (c.clsid == clsid) clsidText.mString.assign(c.name);
        if (c.iid == iid) iidText.mString = std::string("I") + c.name;
    }
    out->mString.append("clsid=", 6);
    out->mString.append(clsidText.mString);
    out->mString.push_back(' ');
    if (iid) {
        out->mString.append("iid=", 4);
        out->mString.append(iidText.mString);
        out->mString.push_back(' ');
    }
    return iid;
}

bool __cdecl WriteLegacyScript(ifaces::cIGZWin *window, cRZString *out)
{
    out->mString.append(std::string("<LEGACY "));
    const uint32_t iid = WriteLegacyClassHeader(window, out);
    WriteWindowAttributes(window, out);
    if (HasClassWriter(iid)) {
        void *face = nullptr;
        if (window->QueryInterface(iid, &face)) {
            RunClassWriter(iid, face, out);
            static_cast<cIGZUnknown *>(face)->Release();
        }
    }
    out->mString.append(std::string(">\n"));
    return true;
}
