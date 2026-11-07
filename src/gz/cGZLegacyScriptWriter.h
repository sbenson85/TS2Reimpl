#pragma once
// The legacy control's script writer (foundations FI..FL): cGZLegacyControl::GetScript 0x86e758 has the writer 0x87a9a9
// put a window back into UI script text, "<LEGACY clsid=... iid=... ...attributes... >\n". The dispatcher writes the
// class header (0x87ae44) and the common attributes (0x86f2b8, lane FJ's), then asks the window for its class's face and
// runs that class's writer (one per cGZWin* class). The writers build each attribute through the formatting helpers below:
// each appends "name=value " to the script text (the text's std::string; never its cIGZString slots), formatted with a
// temporary cRZString's Sprintf. Every helper and writer is __cdecl; the bool ones answer in al.
//
// The names a writer passes are stack-built cRZStrings ("image", "notify"...); the values are what the face's getters
// give. Lane FI wrote the helpers, the dispatcher, the header writer and the smallest writers; the rest are FJ..FL's.
#include "gz/cIGZString.h"
#include "rz/cRZString.h"

#include <cstdint>

class cIGZUnknown;
class cIGZWinBitmap;
class cIGZWinTextTicker;
class cIGZWinFlatRect;
class cIGZWinOutline;
class cIGZWinFolders;
class cIGZWinScrollbar;
class cIGZWinScrollbar2;
class cIGZWinText;
class cIGZWinSlider;
class cIGZWinSpinner;
class cIGZWinCombo;
class cIGZWinLineInput;
class cIGZWinListBox;
class cIGZWinGen;
class cIGZWinOptGrp;
class cIGZWinTextEdit;
class cIGZWinButton;
class cIGZWinTreeView;
class cIGZWinFileBrowser;
class cIGZWinGrid;
namespace ifaces {
class cIGZWin;
}

// A row of a flag table (0x86e546): the flag's mask, its attribute name, and whether it is written even when the flag
// has its default value. The table ends with a null name.
struct LegacyFlagName {
    uint32_t mask;
    const char *name;
    bool always;
};

// ---- the formatting helpers (0x86e546..0x86f25b) --------------------------------------------------------------------
// 0x86f25b: "name=value " (the value's text as it is).
void __cdecl AppendTextAttribute(cRZString *out, const cRZString *name, const cRZString *value);
// 0x86e546: for each row of `table` whose flag differs from `defaults` (or that is written always): "name=yes " when
// the flag is set in `flags`, "name=no " when not.
void __cdecl AppendFlagAttributes(cRZString *out, uint32_t flags, uint32_t defaults, const LegacyFlagName *table);
// 0x86ec88: "name=yes " / "name=no ".
void __cdecl AppendBoolAttribute(cRZString *out, const cRZString *name, bool value);
// 0x86ece8: as 0x86ec88, but nothing when `value` is `defaultValue`.
void __cdecl AppendChangedBoolAttribute(cRZString *out, const cRZString *name, bool value, bool defaultValue);
// 0x86ed50: "name=(r,g,b) " (bytes).
void __cdecl AppendColorAttribute(cRZString *out, const cRZString *name, uint8_t r, uint8_t g, uint8_t b);
// 0x86edad: two colours, "name=(r,g,b)(r,g,b) ".
void __cdecl AppendColor2Attribute(cRZString *out, const cRZString *name, uint8_t r0, uint8_t g0, uint8_t b0, uint8_t r1,
                                   uint8_t g1, uint8_t b1);
// 0x86ee19: four colours, "name=(r,g,b)(r,g,b)(r,g,b)(r,g,b) ".
void __cdecl AppendColor4Attribute(cRZString *out, const cRZString *name, uint8_t r0, uint8_t g0, uint8_t b0, uint8_t r1,
                                   uint8_t g1, uint8_t b1, uint8_t r2, uint8_t g2, uint8_t b2, uint8_t r3, uint8_t g3,
                                   uint8_t b3);
// 0x86eea3: "name=0x%08x ".
void __cdecl AppendHexAttribute(cRZString *out, const cRZString *name, uint32_t value);
// 0x86eef4: "name={%08x,%08x} " (an image's group and instance).
void __cdecl AppendKeyAttribute(cRZString *out, const cRZString *name, uint32_t group, uint32_t instance);
// 0x86ef48: "name=%d ".
void __cdecl AppendIntAttribute(cRZString *out, const cRZString *name, int32_t value);
// 0x86ef99: an item of a list with an image, "name=\"id:text:flags:{group,instance}:(l,t,r,b)\" " (the text escaped);
// without an image (group or instance 0) "name=\"id:text:flags::\" ".
void __cdecl AppendImageItemAttribute(cRZString *out, const cRZString *name, uint32_t id, const cRZString *text,
                                      uint32_t flags, uint32_t group, uint32_t instance, int32_t left, int32_t top,
                                      int32_t right, int32_t bottom);
// 0x86f075: "name=\"%u:(%d,%d)\" ".
void __cdecl AppendNumberedPairAttribute(cRZString *out, const cRZString *name, uint32_t id, int32_t x, int32_t y);
// 0x86f0cc: "name=(%d,%d) ".
void __cdecl AppendPairAttribute(cRZString *out, const cRZString *name, int32_t x, int32_t y);
// 0x86f120: "name=(%d,%d,%d,%d) ".
void __cdecl AppendRectAttribute(cRZString *out, const cRZString *name, int32_t left, int32_t top, int32_t right,
                                 int32_t bottom);
// 0x86f17a: name="text" with the text escaped (0x86ebb8), then a space.
void __cdecl AppendQuotedAttribute(cRZString *out, const cRZString *name, const cRZString *text);
// 0x86f1dc: "name={%08x,%08x,%08x} " (a resource key's type, group and instance).
void __cdecl AppendKey3Attribute(cRZString *out, const cRZString *name, const uint32_t *key);
// 0x86f235: "name " (a word attribute with no value).
void __cdecl AppendWordAttribute(cRZString *out, const cRZString *name);
// 0x86ebb8: builds `out` (raw memory) as a copy of `text` with & < > ' " written as XML entities; returns out.
cRZString *__cdecl EscapeScriptText(cRZString *out, const cRZString *text);

// 0x86e5f8: the UI defaults' name of a font id ("%s"), or the id as "0x%08x" when it has none; false (nothing written)
// for the id 0x68963c4c without a name.
bool __cdecl FormatFontId(uint32_t fontId, cIGZString *out);
// 0x86e655: the window's font property `index` (0xfaa3be85 + index): a number as its font name (or "%d" without one), a
// text as it is; false without the property or for another type.
bool __cdecl FormatFontProperty(ifaces::cIGZWin *window, cIGZString *out, int32_t index);
// 0x86e6f8: the window's image pair property `index` (0xdaa3b860 + index): its two words when it is an untyped value.
bool __cdecl GetImagePairProperty(ifaces::cIGZWin *window, uint32_t *group, uint32_t *instance, int32_t index);
// 0x87b080: the window's 16-byte property `id` (a resource key) into `key` when its size is 16.
bool __cdecl GetKeyProperty(ifaces::cIGZWin *window, uint32_t id, uint32_t *key);
// 0x87b0d4: entry `index` of the window's untyped property `id`, an array of 16-byte entries, into `entry`.
bool __cdecl GetKeyListEntry(ifaces::cIGZWin *window, uint32_t id, int32_t index, uint32_t *entry);

// ---- the dispatcher and the class header ------------------------------------------------------------------------------
// 0x87ae44: "clsid=<name> iid=I<name> " for the window: the ids from its properties 0xe85eebde and 0x08d6e462, or (when
// either is missing or 0) from the first class of the game's table whose interface the window answers (each answer
// releases the window), else GZWin's; an id outside the table is written "0x%08lx". Returns the interface id.
uint32_t __cdecl WriteLegacyClassHeader(ifaces::cIGZWin *window, cRZString *out);
// 0x87a9a9: "<LEGACY ", the header, the common attributes (0x86f2b8), the class's attributes through its face, ">\n".
// Always true.
bool __cdecl WriteLegacyScript(ifaces::cIGZWin *window, cRZString *out);

// 0x86f2b8 the attributes every window has: id (when set), area, fillcolor, caption, captionres, the tooltip texts, the
// window's other properties (through the original callback 0x86e7aa), then its yes/no flags.
void __cdecl WriteWindowAttributes(ifaces::cIGZWin *window, cRZString *out);

// ---- the class writers (each the attributes of one cGZWin* class, from its face) ---------------------------------------
// 0x86fc7d GZWinBMP: image (key property 0xab428c5d), imagerect (when given), notify, transparentbkg, edgeimage, alpha
// (when on: the low byte of alpha * 255 truncated).
void __cdecl WriteBitmapAttributes(cIGZWinBitmap *face, cRZString *out);
// 0x8791f3 GZWinTextTicker: font (font property 0), speed, frequency.
void __cdecl WriteTextTickerAttributes(cIGZWinTextTicker *face, cRZString *out);
// 0x872b33: GZWinFlatRect's: its four colours (left, top, right, bottom) and a style= word per style bit set.
void __cdecl WriteFlatRectAttributes(cIGZWinFlatRect *face, cRZString *out);
// 0x876ea4: GZWinOutline's: its dark and light colours, a style= word per style bit set, and a node= pair per node.
void __cdecl WriteOutlineAttributes(cIGZWinOutline *face, cRZString *out);
// 0x872f45: GZWinFolders': its font (by name), its image key and a listelement= quoted text per element.
void __cdecl WriteFoldersAttributes(cIGZWinFolders *face, cRZString *out);
// 0x87751e: GZWinScrollbar's: its range, direction, page and line sizes, line and page counts, image key and value.
void __cdecl WriteScrollbarAttributes(cIGZWinScrollbar *face, cRZString *out);
// 0x877189: GZWinScrollbar2's: its orientation, the six values it answers and its three image keys.
void __cdecl WriteScrollbar2Attributes(cIGZWinScrollbar2 *face, cRZString *out);
// 0x87822e GZWinText's (lane FJ): its font (by name), align word (or hex past 8), notify / wrapped / opaque, fore and
// background colours (native colours as bytes), gutters and text offsets (signed bytes) and autofit (property 0xdacf8f9c).
void __cdecl WriteTextAttributes(cIGZWinText *face, cRZString *out);
// 0x8777a6 GZWinSlider's (lane FJ): its range, direction (the getter's answer is ignored), image key and initial value.
void __cdecl WriteSliderAttributes(cIGZWinSlider *face, cRZString *out);
// 0x877979 GZWinSpinner's (lane FJ): image key, font, the five flags (autonumbercurrency is always "yes"), value, range,
// step size, the four outline colours, digits side, align, the three font colours and the gutters.
void __cdecl WriteSpinnerAttributes(cIGZWinSpinner *face, cRZString *out);
// 0x870b57 GZWinCombo's (lane FJ): font, the font, highlight, outline and arrow colours, editable, initial selection, the
// arrow image key (property pair 0) and rectangle, the button gutter and gutters, and the list elements.
void __cdecl WriteComboAttributes(cIGZWinCombo *face, cRZString *out);
// 0x87551d GZWinLineInput's (lane FJ): font, the font, border, highlight fill colours, the gutter rectangle, filled,
// editable, the style word, the fill alpha, caret position, character limit and selection.
void __cdecl WriteLineInputAttributes(cIGZWinLineInput *face, cRZString *out);
// 0x875ccb GZWinListBox's (lane FJ): the scroll bars, the font, the font, highlight and outline colours, sunken, outline,
// the scroll bar gutters and gutters, sort, drop, initial selection, the list elements and autofit.
void __cdecl WriteListBoxAttributes(cIGZWinListBox *face, cRZString *out);
// 0x8730b3 GZWinGen's (lane FJ): the image key, the blt type, the user data, the flags (moveable .. sidebar; titlebar,
// fill, outline and paint are written inverted) and the gutters.
void __cdecl WriteGenAttributes(cIGZWinGen *face, cRZString *out);
// 0x8764bf GZWinOptGrp's (lane WA): maincaption / itemcaption / drawfill / outline yes-no, the gutters (the getter's answer
// is ignored), the label and button fonts, the caption text resource and back image keys, the five font colours, the four
// outline colours, the initial selection, the autofill and autofit, then the option items (window slot 37 with the
// original callback 0x86ea36).
void __cdecl WriteOptGrpAttributes(cIGZWinOptGrp *face, cRZString *out);
// 0x878762 GZWinTextEdit's (lane WA): the flag table (default 0x11ff), the font, the initial value, the six font colours
// (indexes 0, 2, 4, 1, 3, 5), the caret, highlight (no answer) and outline colours, the limits, gutters, "overwrite" (the
// insert mode inverted), insert index and position, caret period, undo limit, the two scroll image keys and rectangles,
// autofit, the prompt and its resource key. All colours go through one native colour and one set of bytes.
void __cdecl WriteTextEditAttributes(cIGZWinTextEdit *face, cRZString *out);
// 0x86ff03 GZWinBtn's (lane WA): the image key, the font, the six font colours (indexes 0, 4, 2, 1, 5, 3), the flags
// (toggle, triggerondown on / off; the rest yes / no, fill, tipsdelay and tipstimeout inverted), the look, the gutters
// (the getter's answer is ignored), the tooltip text and resource key, offsets and flags, the alignment and the two sounds.
void __cdecl WriteBtnAttributes(cIGZWinButton *face, cRZString *out);
// 0x8792f2 GZWinTreeView's (lane WD): the flags, icon gutter, row height, min/max box size, the background and highlight
// colours, one "image" per entry of the image table, the root's text, icons, colours, font, expanded and selected, then one
// "node" per entry of the node table. The tables are window properties holding std::map addresses (src/gz/cGZLegacyTreeTables.h).
void __cdecl WriteTreeViewAttributes(cIGZWinTreeView *face, cRZString *out);

// 0x87117a GZWinFileBrowser's (lane WF): default path and selection, sorts, column data, the three sections (file name,
// extension, drive name: font, six colours, alignment), widths, grids, highlight, back text, background, the key and the
// filter entries.
void __cdecl WriteFileBrowserAttributes(cIGZWinFileBrowser *face, cRZString *out);

// 0x873913 GZWinGrid's (lane WF): font, colours, text settings, gutters, selection, sizes, the flags, and the cell, column
// and row matrices of the window's properties.
void __cdecl WriteGridAttributes(cIGZWinGrid *face, cRZString *out);
