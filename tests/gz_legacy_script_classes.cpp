// Differential test of the legacy script writer's class writers (lane FI, foundations): src/gz/cGZLegacyScriptWriter.h,
// src/gz/cGZLegacyScriptWriterClasses.size.cpp. The writers run 0 times in game; this is their evidence.
//
// LegacyScriptWriter_Classes: each seed runs one writer, the original against ours, on a recording fake of its class's
// face (tests/legacy_win_faces.h) whose getters answer drawn values, and a winfx window (the face's GetWindow) whose
// GetProperty (slot 126) hands out one stored value under the property the writer reads or another id. The formatting
// helpers run for real on both sides (the original's on the original's side). Compared: the script text, the stored
// value's count afterwards and the fakes' log.
//   0x86fc7d GZWinBMP: the image key property 0xab428c5d (16 bytes, other sizes, absent), the image rectangle when flag
//            0x10 is set, the flags 1, 2, 8, the alpha (on or off; 0..1 and past both ends).
//   0x8791f3 GZWinTextTicker: the font property 0xfaa3be85 (a number with or without a defaults name, a text, another
//            type, absent), the speed and the frequency.
//   0x872b33 GZWinFlatRect: the four colours (the fake writes each index's bytes) and the style bits (a drawn word).
//   0x876ea4 GZWinOutline: the dark and light colours, the style bits, and the nodes: 0..40 pairs, copied or the copy
//            refused.
//   0x872f45 GZWinFolders: the font (answered or not; a defaults name, an id, the unnamed 0x68963c4c), the image key
//            property 0x2b343bec, and 0..6 elements, some not written (the text before repeats).
//   0x87751e GZWinScrollbar: the range (answered or not), directions 0..3, the sizes, the line and page counts, the
//            image key property 0x6b4bcbd4 and the value.
//   0x87822e GZWinText (lane FJ): the font (answered or not, defaults names, ids, the unnamed 0x68963c4c), the align
//            (answered or not, 0..8 and past 8), the three flags, the two colours (each answering or not, writing or
//            not: the native colour and the converted bytes are shared), the gutters and text offsets (signed bytes
//            shared, answering or not, writing or not), the autofit property 0xdacf8f9c (0, nonzero, absent).
//   0x8777a6 GZWinSlider (lane FJ): the min and max, the direction word (0..3 or any, always written: the original
//            ignores the getter's answer), the image key property 0x6b4bcbd4 and the value.
//   0x877979 GZWinSpinner (lane FJ): the image key property 0x8bd1333a or the font property 0xfaa3be85 (one stored), the
//            flags, value, range, step, the four outline colours (the converted bytes shared, written or not per call),
//            digits and align words (answered or not; 0..2 and others), the three font colours (answering or not,
//            writing or not: the native colour is shared) and the gutters (unsigned bytes).
//   0x870b57 GZWinCombo (lane FJ): the font, the six colours (font normal / disabled / hilited, highlight, outline,
//            arrow: answering or not, writing or not, the native colour and converted bytes shared), editable, the
//            initial selection, the arrow image pair property 0xdaa3b860 (8 or 16 bytes, a number, absent), the arrow
//            rectangle and button gutter and gutters (answering or not; always written), and 0..6 elements, some not
//            written (the text before repeats).
//   0x87551d GZWinLineInput (lane FJ): the font property 0xfaa3be85, eight colours (font 0..2, border 0..3, highlight
//            fill: answering or not, writing or not; the native colour and converted bytes shared), the gutter bytes
//            and the style word and alpha float (answers ignored: always written; the style 0..4 or any, the alpha
//            including huge ones), filled / editable, and the caret, limit and selection start (a shared number:
//            answering or not, writing or not).
//   0x8730b3 GZWinGen (lane FJ): the image key property 0xab428c5d, the blt type (answering or not; 0..3 or any, always
//            written), the user data, fourteen flags (random masks) and the gutters (ignored answer: always written).
//   0x8764bf GZWinOptGrp (lane WA): four flags (random masks), the gutters (ignored answer: always written), the two font
//            properties 0xfaa3be85 / 6, the caption resource 0x4a4af4ac (a number, or 2..4 words some zero), the back image
//            key 0x2b343bec, five font colours and the outline colours (the shared native colour and bytes as the
//            others; three own natives whose conversions are forced written), the initial selection, the autofill (type 0,
//            other or any; answering or not; always written), autofit 0xdacf8f9c, and the window's slot 37 option walk
//            (logged; its callback is the original 0x86ea36 on both sides, never run).
//   0x878762 GZWinTextEdit (lane WA): the flag word (the default 0x11ff with one bit flipped, or any), the font (answered
//            or not), the initial value text, the six font colours, caret colour and outline colour (answering or not,
//            writing or not: one shared native colour and bytes; the highlight colour is the getter's value and always
//            converted, the outline byte any value), the limits, gutters, insert mode (true with noise above al),
//            insert index / position / caret period and the two scroll rectangles and the prompt (each answering or
//            not), the scroll image key properties 0x8b4bfefc / d (one buffer shared with the prompt key 0xb1a4e021,
//            whose group or instance is sometimes zero), the autofit property and the prompt text.
//   0x86ff03 GZWinBtn (lane WA): the image key 0x2b343bec and the tooltip resource key 0x2b340ffe (one buffer; the
//            group or instance of the second sometimes zero), the font (answered or not), the six font colours (one
//            shared native colour and bytes), ten flags (random masks, some written inverted), the look 0..6 or any,
//            the gutter rectangle (ignored answer: always written; its first two words are also the tooltip offsets'
//            locals, which that getter sometimes leaves), the tooltip text, flags, the alignment (0..3 or any, always
//            written) and the two sounds (zero or not).
//   0x877189 GZWinScrollbar2: the orientation word (0, 1, any), the six value getters (each answering or not, and the
//            later five sometimes true without writing: the value before repeats), one of the three image key
//            properties 0x0bfa11d7, 0x6bfa11e1, 0x0bfa11e2.
//   0x8792f2 GZWinTreeView (lane WD): four flags, the three ints, the background and highlight colours (the native
//            colour and converted bytes shared), the six table properties (absent, a non-pointer number or real std::maps
//            of the name pool), the image loop (resource table forced present: the original does not guard it), the root
//            (text, four icons only with the number-image table, four colours, font, expanded and selected as bytes the
//            game compares with exactly 1) and 0..N nodes (parent default Root, positions -4..-2 and others, default
//            image and font names, the font through the defaults, the node colours' persistent bytes).
//   0x87117a GZWinFileBrowser (lane WF): the default path and selection (type 0..2 or any), the two file sorts, the
//            directory sort and the three column words (0..3 / 0,1,2,5 or any: nothing written for the rest), the three
//            sections (the font property read once per section, a different value each time; six colours; the
//            alignment 0..4 or any), the three widths, the two grids (the byte compared with exactly 1) and highlight,
//            the back text, drive list size, background colour, transparent and outline words, the background key
//            property 0xec64c1b3 and 0..N filter entries (the key list property 0x2c64e5c4: the number of an entry the
//            getter answers without writing repeats the one before).
//   0x873913 GZWinGrid (lane WF): the font, colours, text align and wrapping (a word: its low byte counts), gutters,
//            selection, widths, header sizes (answered without writing: zero), the flag words (the low byte of the
//            answer; noise above), and the cell, column and row matrices of the window's properties: absent, a number,
//            an unknown without holder, or a real holder of std::maps (cells with or without the font flag or text,
//            empty lines, indexes apart from the map keys; the holder's count is compared).
#include "code_patch.h"
#include "difftest.h"
#include "legacy_win_faces.h"
#include "shape.h"
#include "ts_services_fixture.h"
#include "winfx.h"

#include "game/game.h"
#include "gz/cGZLegacyControl.h"
#include "gz/cGZLegacyGridMatrix.h"
#include "gz/cGZLegacyHandlers.h"
#include "gz/cGZLegacyScriptWriter.h"
#include "gz/cGZLegacyTreeTables.h"
#include "ifaces/cIGZWin.h"
#include "rz/cIGZVariant.h"
#include "rz/cRZString.h"
#include "rz/cRZUnknownAutoPtr.h"
#include "rz/cRZVariant.h"

#include <bit>
#include <random>
#include <string>
#include <vector>

namespace legacy_script_classes_test {

using svcfx::Blob;

constexpr uint32_t kDefaultsCell = 0x10f51b0;

std::mt19937 gRng;
uint32_t Draw(uint32_t n) { return uint32_t(gRng() % n); }

void *__cdecl GameNew(size_t n) { return ::operator new(n); }
void __cdecl GameDelete(void *p, size_t) { ::operator delete(p); }

void PushText(Blob &b, const std::string &s)
{
    b.push_back(uint32_t(s.size()));
    for (char c : s) b.push_back(uint8_t(c));
}

winfx::World gWorld;
drawfx::Fake gFace, gDefaults, gFontEntry;
uint32_t gStoreId;
cIGZVariant *gStored;
uint32_t gFlags;
int32_t gRect[4];
uint32_t gAlphaOn;
float gAlpha;
uint8_t gColors[4][3];
int32_t gNodes[80];
uint32_t gNodesCopied;
uint32_t gNodeCount;
uint32_t gFontId, gFontAnswered;
uint32_t gElementCount, gElementWritten;
const char *gElements[8];
int32_t gPairs[4];
uint32_t gPairAnswered;
uint32_t gVertical;
uint32_t gDirection;
int32_t gValues[19];
uint32_t gValueAnswered, gValueWritten, gValueEverWritten;

uint32_t GetStoredProperty(drawfx::Fake &, const uint32_t *a)
{
    if (a[0] != gStoreId || !gStored) return 0;
    gStored->AddRef();
    *reinterpret_cast<void **>(uintptr_t(a[1])) = gStored;
    return 1;
}
uint32_t FindFontEntry(drawfx::Fake &, const uint32_t *a) { return (a[0] & 4) ? 0 : drawfx::P(&gFontEntry); }
uint32_t IsFlagSet(drawfx::Fake &, const uint32_t *a) { return (gFlags & a[0]) != 0; }
uint32_t GetImageRect(drawfx::Fake &, const uint32_t *a)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), gRect, sizeof gRect);
    return 0;
}
uint32_t GetAlpha(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<uint8_t *>(uintptr_t(a[0])) = uint8_t(gAlphaOn);
    *reinterpret_cast<float *>(uintptr_t(a[1])) = gAlpha;
    return 0;
}

// GetColor(index, &red, &green, &blue): the drawn bytes of index 0..3; any other index leaves them.
uint32_t GetColor(drawfx::Fake &, const uint32_t *a)
{
    if (a[0] >= 4) return 0;
    for (int i = 0; i < 3; ++i) *reinterpret_cast<uint8_t *>(uintptr_t(a[1 + i])) = gColors[a[0]][i];
    return 0;
}
uint32_t GetNodes(drawfx::Fake &, const uint32_t *a)
{
    if (!gNodesCopied) return 0;
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[0])), gNodes, 8 * gNodeCount);
    return 1;
}
uint32_t GetFont(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = gFontId;
    return gFontAnswered;
}
// GetElement(index, text): the drawn element's text into the cRZString (when it is written), false past the last.
uint32_t GetElement(drawfx::Fake &, const uint32_t *a)
{
    if (a[0] >= gElementCount) return 0;
    if (gElementWritten >> a[0] & 1) reinterpret_cast<cRZString *>(uintptr_t(a[1]))->mString.assign(gElements[a[0]]);
    return 1;
}
template <unsigned First> uint32_t GetPair(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<int32_t *>(uintptr_t(a[0])) = gPairs[First];
    *reinterpret_cast<int32_t *>(uintptr_t(a[1])) = gPairs[First + 1];
    return gPairAnswered;
}
// The slider's GetDirection: the original ignores the answer, so the word is always written.
uint32_t GetDirection(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = gDirection;
    return Draw(2);
}
uint32_t GetVertical(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = gVertical;
    return 0;
}
// Scrollbar2's value getter at slot N: answers bit N; writes when bit N of gValueWritten is set, and always while
// nothing has been written (the original's variable starts as its own argument, ours undefined).
template <unsigned N> uint32_t GetValue(drawfx::Fake &, const uint32_t *a)
{
    if (!gValueEverWritten || (gValueWritten >> N & 1)) {
        *reinterpret_cast<int32_t *>(uintptr_t(a[0])) = gValues[N];
        gValueEverWritten = 1;
    }
    return gValueAnswered >> N & 1;
}
// ---- GZWinText (lane FJ): the shared locals are the native colour, its three bytes and the two signed bytes ----------------
uint32_t gAlign, gAlignAnswered;
uint32_t gNative[2], gColorAnswered, gColorWritten, gColorEver;
uint32_t gRgbWritten, gRgbCalls, gRgbEver;
uint32_t gRgbForce = ~0u; // the call whose bytes are written whatever the mask says (a local that has no earlier value)
uint32_t gRgbSpan = 1;    // ... and the calls after it: gRgbSpan calls from gRgbForce on are forced
int8_t gXy[2][2];
uint32_t gXyAnswered, gXyWritten, gXyEver;
drawfx::Fake gGraphics;
constexpr uint32_t kGraphicsCell = 0x10f51a4;

uint32_t GetAlign(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = gAlign;
    return gAlignAnswered;
}
// GetColor(index, &native): answers bit `index`; writes when its bit of gColorWritten is set, and always while nothing has
// been written (the original's variable starts undefined).
uint32_t GetTextColor(drawfx::Fake &, const uint32_t *a)
{
    if (!gColorEver || (gColorWritten >> (a[0] & 31) & 1)) {
        *reinterpret_cast<uint32_t *>(uintptr_t(a[1])) = gNative[a[0] & 1];
        gColorEver = 1;
    }
    return gColorAnswered >> (a[0] & 31) & 1;
}
// ConvertNativeToRGB(native, &r, &g, &b): the bytes of the native colour, written on call N when bit N of gRgbWritten is
// set and always on the first.
uint32_t ConvertNativeToRGB(drawfx::Fake &, const uint32_t *a)
{
    if (!gRgbEver || gRgbCalls - gRgbForce < gRgbSpan || (gRgbWritten >> gRgbCalls & 1)) {
        *reinterpret_cast<uint8_t *>(uintptr_t(a[1])) = uint8_t(a[0] >> 16);
        *reinterpret_cast<uint8_t *>(uintptr_t(a[2])) = uint8_t(a[0] >> 8);
        *reinterpret_cast<uint8_t *>(uintptr_t(a[3])) = uint8_t(a[0]);
        gRgbEver = 1;
    }
    ++gRgbCalls;
    return 1;
}
// The gutters (slot 24, N 0) and the text offsets (slot 26, N 1): two signed bytes for both, written as the colours are.
template <unsigned N> uint32_t GetXy(drawfx::Fake &, const uint32_t *a)
{
    if (!gXyEver || (gXyWritten >> N & 1)) {
        *reinterpret_cast<int8_t *>(uintptr_t(a[0])) = gXy[N][0];
        *reinterpret_cast<int8_t *>(uintptr_t(a[1])) = gXy[N][1];
        gXyEver = 1;
    }
    return gXyAnswered >> N & 1;
}
// ---- GZWinSpinner (lane FJ): its getters whose answer the writer ignores always write ------------------------------------
uint32_t gDigits, gDigitsAnswered;
int32_t gStep;
uint32_t gOutline[4];
uint32_t GetDigits(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = gDigits;
    return gDigitsAnswered;
}
uint32_t GetStep(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<int32_t *>(uintptr_t(a[0])) = gStep;
    return Draw(2);
}
uint32_t GetOutlineColors(drawfx::Fake &, const uint32_t *a)
{
    for (int i = 0; i < 4; ++i) *reinterpret_cast<uint32_t *>(uintptr_t(a[i])) = gOutline[i];
    return Draw(2);
}
// ---- GZWinCombo (lane FJ): the colours after the font ones answer bit N of the shared masks ---------------------------------
uint32_t gArrowAnswered, gButtonAnswered;
int8_t gButton;
template <unsigned N> uint32_t GetNative(drawfx::Fake &, const uint32_t *a)
{
    if (!gColorEver || (gColorWritten >> N & 1)) {
        *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = gNative[N & 1];
        gColorEver = 1;
    }
    return gColorAnswered >> N & 1;
}
// GetArrowImage(&image, rect): the original does not use the image; the rectangle is always written (an unwritten one
// would print the stack).
uint32_t GetArrowImage(drawfx::Fake &, const uint32_t *a)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[1])), gRect, sizeof gRect);
    return gArrowAnswered;
}
uint32_t GetButtonGutter(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<int8_t *>(uintptr_t(a[0])) = gButton;
    return gButtonAnswered;
}
// ---- GZWinLineInput (lane FJ) -------------------------------------------------------------------------------------------
// GetBorderColor(index, &native): as GetTextColor, on bits 3 + index of the masks.
uint32_t GetBorderColor(drawfx::Fake &, const uint32_t *a)
{
    const uint32_t bit = 3 + (a[0] & 3);
    if (!gColorEver || (gColorWritten >> bit & 1)) {
        *reinterpret_cast<uint32_t *>(uintptr_t(a[1])) = gNative[a[0] & 1];
        gColorEver = 1;
    }
    return gColorAnswered >> bit & 1;
}
int8_t gGutterBytes[4];
// The gutter rectangle: the answer is ignored by the original, so the four bytes are always written.
uint32_t GetGutterRect(drawfx::Fake &, const uint32_t *a)
{
    for (int i = 0; i < 4; ++i) *reinterpret_cast<int8_t *>(uintptr_t(a[i])) = gGutterBytes[i];
    return Draw(2);
}
// The fill alpha: the answer is not asked for; the float is always written.
uint32_t GetFillAlpha(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<float *>(uintptr_t(a[0])) = gAlpha;
    return 0;
}
// GetSelection(&start, &length): the start is the shared number of slot N 2; the length is always written.
uint32_t GetSelection(drawfx::Fake &f, const uint32_t *a)
{
    const uint32_t answer = GetValue<2>(f, a);
    *reinterpret_cast<int32_t *>(uintptr_t(a[1])) = gValues[3];
    return answer;
}
// ---- GZWinListBox (lane FJ) ---------------------------------------------------------------------------------------------
// GetScrollbars(&vertical, &horizontal): both bytes always written (nothing else uses those locals).
int8_t gScrollbars[2];
uint32_t gScrollbarsAnswered;
uint32_t GetScrollbars(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<int8_t *>(uintptr_t(a[0])) = gScrollbars[0];
    *reinterpret_cast<int8_t *>(uintptr_t(a[1])) = gScrollbars[1];
    return gScrollbarsAnswered;
}
// GetOutlineColors(&first, &second): the first is the shared native colour (bit 4 of the masks); the second has its own
// local, which the writer converts whatever happened, so it is always written.
uint32_t GetListOutline(drawfx::Fake &f, const uint32_t *a)
{
    const uint32_t answer = GetNative<4>(f, a);
    *reinterpret_cast<uint32_t *>(uintptr_t(a[1])) = gNative[1];
    if (answer) gRgbForce = gRgbCalls + 1; // the second colour's bytes are a local of their own: its conversion writes them
    return answer;
}
// ---- GZWinGen (lane FJ) -------------------------------------------------------------------------------------------------
// GetBackImageType(&type, &other): the type is written whatever the answer (the original reads it only after a yes).
int32_t gGenType;
uint32_t gGenTypeAnswered;
int32_t gGenGutters[2];
uint32_t GetBackImageType(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<int32_t *>(uintptr_t(a[0])) = gGenType;
    return gGenTypeAnswered;
}
// GetGutters(&x, &y): the answer is ignored by the original, so both words are always written.
uint32_t GetGenGutters(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<int32_t *>(uintptr_t(a[0])) = gGenGutters[0];
    *reinterpret_cast<int32_t *>(uintptr_t(a[1])) = gGenGutters[1];
    return Draw(2);
}
// ---- GZWinOptGrp (lane WA) ----------------------------------------------------------------------------------------------
// The window's five properties the writer reads: the two fonts, the caption resource, the back image and the autofit flag.
// (TextEdit's: 4 the autofit flag, 5 and 6 the scroll image keys, 7 the prompt resource key; Btn's: 3 the image, 8 the
// tooltip resource key.)
// (TreeView's: 0 the root font, 9..14 the six tables, as legacy_tree's property ids.)
constexpr uint32_t kOptIds[15] = {0xfaa3be85, 0xfaa3be86, 0x4a4af4ac, 0x2b343bec, 0xdacf8f9c, 0x8b4bfefc, 0x8b4bfefd, 0xb1a4e021, 0x2b340ffe,
                                  legacy_tree::kImageNumbersProperty, legacy_tree::kNumberImagesProperty, legacy_tree::kImageResourcesProperty,
                                  legacy_tree::kNodeFontsProperty, legacy_tree::kNodeNumbersProperty, legacy_tree::kNumberNodesProperty};
cRZVariant *gOptValues[15];
uint32_t gAutoType, gAutoSize, gAutoAnswered;
uint32_t GetOptProperty(drawfx::Fake &, const uint32_t *a)
{
    for (int i = 0; i < 15; ++i) {
        if (a[0] != kOptIds[i] || !gOptValues[i]) continue;
        gOptValues[i]->AddRef();
        *reinterpret_cast<void **>(uintptr_t(a[1])) = gOptValues[i];
        return 1;
    }
    return 0;
}
// GetAutofill(&type, &size): both words written whatever the answer (the original reads them only after a yes).
uint32_t GetOptAutofill(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<uint32_t *>(uintptr_t(a[0])) = gAutoType;
    *reinterpret_cast<uint32_t *>(uintptr_t(a[1])) = gAutoSize;
    return gAutoAnswered;
}
// GetOutlineColors(&first, &second, &third, &fourth): the first is the shared native colour (bit 5 of the masks); the
// other three have locals of their own, which the writer converts whatever happened, so they are always written, and so
// are their three conversions.
uint32_t GetOptOutline(drawfx::Fake &f, const uint32_t *a)
{
    const uint32_t answer = GetNative<5>(f, a);
    for (int i = 1; i < 4; ++i) *reinterpret_cast<uint32_t *>(uintptr_t(a[i])) = gOutline[i];
    if (answer) {
        gRgbForce = gRgbCalls + 1;
        gRgbSpan = 3;
    }
    return answer;
}
// ---- GZWinTextEdit (lane WA) ----------------------------------------------------------------------------------------------
// The getters the writer reads only after a yes always write what they hand out; their answers are the bits of gTeAnswers
// (0 insert index, 1 insert position, 2 caret period, 3 horizontal scroll rectangle, 4 vertical one, 5 prompt).
uint32_t gTeAnswers, gTeWords[4];
uint8_t gOutlined;
const char *gPromptText;
template <unsigned N> uint32_t GetTeWords(drawfx::Fake &, const uint32_t *a)
{
    const uint32_t pointers = N == 1 ? 2 : 1;
    const uint32_t base = N == 2 ? 2 : 0;
    for (uint32_t i = 0; i < pointers; ++i) *reinterpret_cast<uint32_t *>(uintptr_t(a[i])) = gTeWords[base + i];
    return gTeAnswers >> N & 1 ? 0x7701 : 0x7700;
}
// GetHScrollImage / GetVScrollImage (&image, rect): the image is not used by the writer; the rectangle is written.
template <unsigned N> uint32_t GetTeRect(drawfx::Fake &, const uint32_t *a)
{
    std::memcpy(reinterpret_cast<void *>(uintptr_t(a[1])), gRect, sizeof gRect);
    return gTeAnswers >> N & 1 ? 0x7701 : 0x7700;
}
uint32_t GetTePrompt(drawfx::Fake &, const uint32_t *a)
{
    reinterpret_cast<cRZString *>(uintptr_t(a[0]))->mString.assign(gPromptText);
    return gTeAnswers >> 5 & 1 ? 0x7701 : 0x7700;
}
// GetOutlineColor(&outlined, &native): the answer is not read; the byte is always written, the colour as the shared one.
uint32_t GetTeOutline(drawfx::Fake &, const uint32_t *a)
{
    *reinterpret_cast<uint8_t *>(uintptr_t(a[0])) = gOutlined;
    if (!gColorEver || (gColorWritten >> 7 & 1)) {
        *reinterpret_cast<uint32_t *>(uintptr_t(a[1])) = gNative[1];
        gColorEver = 1;
    }
    return 0;
}
// ---- GZWinBtn (lane WA) ----
// GetGutterRect(&a, &b, &c, &d): the answer is ignored, so the four words are always written.
int32_t gBtnOffsets[2];
uint32_t gBtnOffsetsWritten, gBtnSounds[2];
uint32_t GetBtnGutters(drawfx::Fake &, const uint32_t *a)
{
    for (int i = 0; i < 4; ++i) *reinterpret_cast<int32_t *>(uintptr_t(a[i])) = gRect[i];
    return Draw(2);
}
// GetTipOffsets(&x, &y): the two words are the gutter getter's first two locals, written only sometimes (the answer is
// bit 1 of gTeAnswers).
uint32_t GetBtnTipOffsets(drawfx::Fake &, const uint32_t *a)
{
    if (gBtnOffsetsWritten) {
        *reinterpret_cast<int32_t *>(uintptr_t(a[0])) = gBtnOffsets[0];
        *reinterpret_cast<int32_t *>(uintptr_t(a[1])) = gBtnOffsets[1];
    }
    return gTeAnswers >> 1 & 1 ? 0x7701 : 0x7700;
}
uint32_t GetBtnSound(drawfx::Fake &, const uint32_t *a) { return gBtnSounds[a[0] & 1]; }
// The prompt resource key: 16 bytes whose second and third words are each zero now and then.
cRZVariant *PromptKeyValue()
{
    uint32_t words[4];
    for (uint32_t &w : words) w = Draw(3) ? uint32_t(gRng()) : 0;
    auto *v = new cRZVariant;
    v->SetVoidArray(words, 16);
    return v;
}

// The caption resource: a number (no array bit), or an array of 2..4 words some zero.
cRZVariant *ResourceValue()
{
    if (!Draw(4)) return new cRZVariant(uint32_t(gRng()));
    uint32_t words[4];
    for (uint32_t &w : words) w = Draw(3) ? uint32_t(gRng()) : 0;
    auto *v = new cRZVariant;
    v->SetUint32Array(words, 2 + Draw(3));
    return v;
}

// The image pair property: 8 or 16 bytes untyped, or a number.
cRZVariant *PairValue()
{
    if (!Draw(5)) return new cRZVariant(uint32_t(gRng()));
    std::vector<uint8_t> bytes(Draw(2) ? 8 : 16);
    for (uint8_t &b : bytes) b = uint8_t(gRng());
    auto *v = new cRZVariant;
    v->SetVoidArray(bytes.data(), uint32_t(bytes.size()));
    return v;
}
cRZVariant *AutofitValue()
{
    switch (Draw(3)) {
    case 0: return new cRZVariant(uint32_t(0));
    case 1: return new cRZVariant(uint32_t(1 + Draw(255)));
    default: return new cRZVariant(int32_t(gRng()));
    }
}

int32_t DrawInt() { return Draw(3) ? int32_t(Draw(2000)) - 100 : int32_t(gRng()); }

cRZVariant *KeyValue()
{
    std::vector<uint8_t> bytes(Draw(4) ? 16 : Draw(32));
    for (uint8_t &b : bytes) b = uint8_t(gRng());
    auto *v = new cRZVariant;
    v->SetVoidArray(bytes.data(), uint32_t(bytes.size()));
    return v;
}
cRZVariant *FontValue()
{
    switch (Draw(4)) {
    case 0: return new cRZVariant(uint32_t(Draw(16)));
    case 1: return new cRZVariant(uint32_t(gRng()));
    case 2: {
        static const char *const kTexts[] = {"Arial Bold 12", "x", ""};
        LegacyText s;
        s.mText.FromChar(kTexts[Draw(3)]);
        return new cRZVariant(*s.mText.AsInterface());
    }
    default: return new cRZVariant(int32_t(gRng()));
    }
}

float DrawAlpha()
{
    static const float kEdges[] = {0.0f, 1.0f, 0.5f, 0.999f, -0.5f, 1.5f, 0.00196f, 0.0039215f};
    return Draw(2) ? kEdges[Draw(8)] : float(Draw(10001)) / 10000.0f;
}

using WriterFn = void(__cdecl *)(void *, cRZString *);
legacy_win_faces::TreeTables gTables;

// A table's property: absent, a number that is 0, or the address of the map.
template <class Map> cRZVariant *TableValue(const Map &map, bool *real = nullptr)
{
    switch (Draw(8)) {
    case 0: return nullptr;
    case 1: return new cRZVariant(uint32_t(0));
    default:
        if (real) *real = true;
        return new cRZVariant(uint32_t(uintptr_t(&map)));
    }
}

// ---- GZWinFileBrowser and GZWinGrid (lane WF) --------------------------------------------------------------------------
// The window's properties they read: the background key, the key list, the three grid matrices; the font property (read
// once per section, three values in turn).
constexpr uint32_t kFlIds[5] = {0xec64c1b3, 0x2c64e5c4, 0x287a7136, 0xc88ccb6d, 0xc88ccb6e};
cRZVariant *gFlValues[5];
cRZVariant *gFlFonts[3];
uint32_t gFlFontCalls;
cIGZUnknown *gFlKeepers[3]; // the holders, one extra reference each for the test (their counts are compared)
uint32_t GetFlProperty(drawfx::Fake &, const uint32_t *a)
{
    cRZVariant *v = nullptr;
    if (a[0] == 0xfaa3be85)
        v = gFlFonts[gFlFontCalls++ % 3];
    else
        for (int i = 0; i < 5; ++i)
            if (a[0] == kFlIds[i]) v = gFlValues[i];
    if (!v) return 0;
    v->AddRef();
    *reinterpret_cast<void **>(uintptr_t(a[1])) = v;
    return 1;
}

template <class Holder, class Matrix> cRZVariant *HolderValue(Matrix *matrix, cIGZUnknown **keeper)
{
    Holder *holder = new Holder(matrix);
    cIGZUnknown *unknown = holder;
    unknown->AddRef();
    auto *v = new cRZVariant;
    v->SetValUnknown(unknown);
    unknown->Release();
    unknown->AddRef();
    *keeper = unknown;
    return v;
}

// ORIGINAL-BUG: the grid writer's walk over a matrix never ends once the matrix has an entry. Its end test is "the line
// iterator is at the end and the entry iterator is null", but the advance (0x86e505) leaves the entry iterator on the last
// line's head node when it runs off the end, so the loop goes on formatting that node for ever. Ours stops after the last
// entry. The test therefore gives the original at most one matrix with entries (the others have none, or only empty lines,
// which it skips), and on the original's side ends the walk by a fault: the last entry's successor is made a node whose
// fields lie on a page nothing may read (the root's parent link points to it), so the first read of the extra round faults,
// before anything of that round is written or asked. What the original wrote up to then is what ours writes in all.
uint8_t *GuardNode()
{
    static uint8_t *node = nullptr;
    if (!node) {
        uint8_t *base = static_cast<uint8_t *>(VirtualAlloc(nullptr, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        DWORD old;
        VirtualProtect(base + 0x1000, 0x1000, PAGE_NOACCESS, &old);
        node = base + 0x1000 - 0x10; // the head of the node (links, colour, nil flag) readable, everything after it not
        node[0xd] = 1;               // a nil node, so the climb from the last entry ends on it
    }
    return node;
}
uint8_t *gGuardRoot; // the root whose parent link was redirected (restored after the call)
uint8_t *gGuardOld;
void *gFlMatrix[3];
void (*gFlArm[3])(void *);
bool gFlHasEntries[3];
template <class Matrix> void ArmMatrix(void *p)
{
    auto &m = *static_cast<Matrix *>(p);
    for (auto it = m.mLines.rbegin(); it != m.mLines.rend(); ++it) {
        if (it->second.mEntries.empty()) continue;
        uint8_t *head = *reinterpret_cast<uint8_t **>(&it->second.mEntries);
        uint8_t *root = *reinterpret_cast<uint8_t **>(head + 4);
        gGuardRoot = root;
        gGuardOld = *reinterpret_cast<uint8_t **>(root + 4);
        *reinterpret_cast<uint8_t **>(root + 4) = GuardNode();
        return;
    }
}
void DisarmMatrix()
{
    if (gGuardRoot) *reinterpret_cast<uint8_t **>(gGuardRoot + 4) = gGuardOld;
    gGuardRoot = nullptr;
}

// A matrix property: absent, a number, an unknown that holds nothing, or a real holder (made by `fill`; `entries` says
// whether it may put entries in).
template <class Holder, class Matrix, class Fill> cRZVariant *MatrixValue(unsigned index, bool entries, Fill fill)
{
    switch (Draw(8)) {
    case 0: return nullptr;
    case 1: return new cRZVariant(uint32_t(gRng()));
    case 2: {
        auto *v = new cRZVariant;
        v->SetValUnknown(nullptr);
        return v;
    }
    default: {
        auto *matrix = new Matrix;
        fill(*matrix, entries);
        gFlMatrix[index] = matrix;
        gFlArm[index] = &ArmMatrix<Matrix>;
        gFlHasEntries[index] = matrix->mCount != 0;
        return HolderValue<Holder, Matrix>(matrix, &gFlKeepers[index]);
    }
    }
}

// Now and then an empty line, and indexes that are not the map keys (the writers print the indexes kept in the values).
template <class Matrix> void Disturb(Matrix &m)
{
    for (int k = 0; k < 2; ++k)
        if (!Draw(3)) {
            const int32_t key = int32_t(Draw(12)) - 3;
            m.mLines.emplace(key, typename Matrix::Lines::mapped_type(key));
        }
    for (auto &line : m.mLines) {
        if (!Draw(6)) line.second.mIndex += 1 + int32_t(Draw(5));
        for (auto &entry : line.second.mEntries)
            if (!Draw(6)) entry.second.mIndex += 1 + int32_t(Draw(5));
    }
}

void FillCells(legacygrid::CellMatrix &m, bool entries)
{
    static const char *const kTexts[] = {"", "hello", "a,b:c", "100%", "a longer cell text past sixteen chars"};
    static const uint32_t kFlagsKinds[] = {0, 0x1000, 0x40000, 0x41000, 0x2000, 0x41fff};
    const uint32_t count = entries ? 1 + (Draw(3) ? Draw(4) : Draw(14)) : 0;
    for (uint32_t k = 0; k < count; ++k) {
        legacygrid::GridCell cell;
        cell.mGroup = gRng();
        cell.mInstance = gRng();
        cell.mFlags = Draw(4) ? kFlagsKinds[Draw(6)] : uint32_t(gRng());
        cell.mText.FromChar(kTexts[Draw(5)]);
        cell.mFontId = Draw(3) ? Draw(8) : uint32_t(gRng());
        for (uint32_t &w : cell.mFontWords) w = gRng();
        m.Set(int32_t(Draw(7)) - 2, int32_t(Draw(7)) - 2, &cell, nullptr);
    }
    Disturb(m);
}

void FillLines(legacygrid::LineMatrix &m, bool entries)
{
    const uint32_t count = entries ? 1 + (Draw(3) ? Draw(4) : Draw(14)) : 0;
    for (uint32_t k = 0; k < count; ++k) {
        legacygrid::GridLineData data;
        data.mFirst = int32_t(Draw(7)) - 2;
        data.mLast = Draw(3) ? int32_t(Draw(20)) - 3 : int32_t(gRng());
        data.mValue = Draw(3) ? int32_t(Draw(300)) - 3 : int32_t(gRng());
        // Columns key by their first number (line 0), rows by theirs (entry 0): the test fills both ways.
        if (Draw(2))
            m.Set(data.mFirst, 0, &data, nullptr);
        else
            m.Set(int32_t(Draw(4)), data.mFirst, &data, nullptr);
    }
    Disturb(m);
}

void BuildFlProperties(bool grid)
{
    for (cRZVariant *&v : gFlValues) v = nullptr;
    for (cRZVariant *&v : gFlFonts) v = Draw(3) ? FontValue() : nullptr;
    for (cIGZUnknown *&k : gFlKeepers) k = nullptr;
    gFlFontCalls = 0;
    for (int i = 0; i < 3; ++i) {
        gFlMatrix[i] = nullptr;
        gFlArm[i] = nullptr;
        gFlHasEntries[i] = false;
    }
    gGuardRoot = nullptr;
    if (grid) {
        // At most one of the three has entries (ORIGINAL-BUG above); a third of the cases have none.
        const unsigned only = Draw(3) ? Draw(3) : 3;
        gFlValues[2] = MatrixValue<cRZUnknownAutoPtrWinGridCellData, legacygrid::CellMatrix>(0, only == 0, FillCells);
        gFlValues[3] = MatrixValue<cRZUnknownAutoPtrWinGridColData, legacygrid::LineMatrix>(1, only == 1, FillLines);
        gFlValues[4] = MatrixValue<cRZUnknownAutoPtrWinGridRowData, legacygrid::LineMatrix>(2, only == 2, FillLines);
    } else {
        gFlValues[0] = Draw(2) ? KeyValue() : nullptr;
        if (Draw(2)) {
            const uint32_t count = Draw(8);
            std::vector<uint32_t> words(count * 4);
            for (uint32_t &w : words) w = Draw(3) ? uint32_t(gRng()) : 0;
            auto *v = new cRZVariant;
            v->SetVoidArray(words.data(), count * 16);
            gFlValues[1] = v;
        }
    }
    // A new variant counts 0: the test holds one reference to each (the writers' lookups add and drop their own).
    for (cRZVariant *v : gFlFonts)
        if (v) v->AddRef();
    for (cRZVariant *v : gFlValues)
        if (v) v->AddRef();
}

constexpr unsigned kWriters = 20;

// No C++ objects here (__try): the code of the access violation that ends the call, or 0.
uint32_t CallWriter(WriterFn fn, cRZString *out)
{
    __try {
        fn(&gFace, out);
        return 0;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return GetExceptionCode();
    }
}

DIFF_TEST(LegacyScriptWriter_Classes, 0x86fc7d)
{
    svcfx::Globals globals;
    svcfx::AllocatorScope allocator;
    shape::CodePatch gameNew(0x824632, shape::CodeOf(&GameNew));
    shape::CodePatch gameDelete(0x824624, shape::CodeOf(&GameDelete));
    globals.Set(kDefaultsCell, &gDefaults);
    globals.Set(kGraphicsCell, &gGraphics);
    for (uint32_t seed = 1; seed <= 14000; ++seed) {
        drawfx::Log logs[2];
        Blob results[2];
        bool endedByFault = false; // the original's walk ended by the fault: it asked nothing of the sections after it
        for (int side = 0; side < 2; ++side) {
            drawfx::ResetAll();
            gRng.seed(seed);
            gDefaults.Reset<legacy_win_faces::kDefaultsFace>(560);
            gDefaults.On(37, &FindFontEntry);
            gFontEntry.Reset<legacy_win_faces::kFontEntryFace>(561);
            gGraphics.Reset<legacy_win_faces::kGraphicsFace>(540);
            gGraphics.On(36, &ConvertNativeToRGB);
            gFontEntry.Returns(5, Draw(2) ? "Comic Sans" : "");
            gWorld.Build(seed, 2);
            winfx::Win &w = gWorld[1];
            w.f.On(126, &GetStoredProperty);
            const unsigned which = seed % kWriters;
            LegacyText out;
            out.mText.FromChar(Draw(2) ? "" : "<LEGACY clsid=GZWin ");
            gStored = nullptr;
            gRgbForce = ~0u;
            gRgbSpan = 1;
            switch (which) {
            case 0: // GZWinBMP
                gFace.Reset<legacy_win_faces::kBitmapFace>(600);
                gFace.Returns(3, &w.f);
                gFace.On(7, &GetAlpha);
                gFace.On(9, &GetImageRect);
                gFace.On(10, &IsFlagSet);
                gStoreId = Draw(5) ? 0xab428c5d : gRng();
                if (Draw(5)) gStored = KeyValue();
                gFlags = gRng();
                for (int32_t &r : gRect) r = Draw(3) ? int32_t(Draw(2000)) - 100 : int32_t(gRng());
                gAlphaOn = Draw(3) ? 1 : (Draw(2) ? 0 : 0x100); // a high byte only is off
                gAlpha = DrawAlpha();
                break;
            case 1: // GZWinTextTicker
                gFace.Reset<legacy_win_faces::kTextTickerFace>(600);
                gFace.Returns(12, &w.f);
                gFace.Returns(10, Draw(3) ? Draw(1000) : uint32_t(gRng()));
                gFace.Returns(11, Draw(3) ? Draw(1000) : uint32_t(gRng()));
                gStoreId = Draw(5) ? 0xfaa3be85 : gRng();
                if (Draw(6)) gStored = FontValue();
                break;
            case 2: // GZWinFlatRect
                gFace.Reset<legacy_win_faces::kFlatRectFace>(600);
                gFace.On(11, &GetColor);
                gFace.Returns(13, Draw(2) ? Draw(0x80) : uint32_t(gRng()));
                break;
            case 3: // GZWinOutline
                gFace.Reset<legacy_win_faces::kOutlineFace>(600);
                gFace.On(6, &GetColor);
                gFace.Returns(4, Draw(2) ? Draw(0x40) : uint32_t(gRng()));
                gNodeCount = Draw(4) ? Draw(8) : Draw(41);
                gFace.Returns(16, gNodeCount);
                gFace.On(17, &GetNodes);
                gNodesCopied = Draw(5) != 0;
                for (int32_t &n : gNodes) n = Draw(3) ? int32_t(Draw(1000)) - 100 : int32_t(gRng());
                break;
            case 4: { // GZWinFolders
                gFace.Reset<legacy_win_faces::kFoldersFace>(600);
                gFace.Returns(3, &w.f);
                gFace.On(13, &GetFont);
                gFace.On(20, &GetElement);
                static const uint32_t kFonts[] = {0, 3, 4, 0x68963c4c};
                gFontId = Draw(2) ? kFonts[Draw(4)] : gRng();
                gFontAnswered = Draw(4) != 0;
                static const char *const kTexts[] = {"Desktop", "", "a \"quoted\" & <name>", "long folder name past sixteen"};
                gElementCount = Draw(7);
                for (const char *&e : gElements) e = kTexts[Draw(4)];
                gElementWritten = Draw(3) ? 0xff : gRng();
                gStoreId = Draw(5) ? 0x2b343bec : gRng();
                if (Draw(5)) gStored = KeyValue();
                break;
            }
            case 5: // GZWinScrollbar
                gFace.Reset<legacy_win_faces::kScrollbarFace>(600);
                gFace.Returns(3, &w.f);
                gFace.Returns(6, uint32_t(DrawInt()));
                gFace.On(8, &GetPair<0>);
                gFace.Returns(10, Draw(4));
                gFace.Returns(12, uint32_t(DrawInt()));
                gFace.Returns(14, uint32_t(DrawInt()));
                gFace.On(16, &GetPair<2>);
                for (int32_t &p : gPairs) p = DrawInt();
                gPairAnswered = Draw(4) != 0;
                gStoreId = Draw(5) ? 0x6b4bcbd4 : gRng();
                if (Draw(5)) gStored = KeyValue();
                break;
            case 6: { // GZWinScrollbar2
                gFace.Reset<legacy_win_faces::kScrollbar2Face>(600);
                gFace.Returns(3, &w.f);
                gFace.On(5, &GetVertical);
                gFace.On(7, &GetValue<7>);
                gFace.On(10, &GetValue<10>);
                gFace.On(12, &GetValue<12>);
                gFace.On(14, &GetValue<14>);
                gFace.On(16, &GetValue<16>);
                gFace.On(18, &GetValue<18>);
                gVertical = Draw(3) ? Draw(2) : gRng();
                for (int32_t &v : gValues) v = DrawInt();
                gValueAnswered = Draw(3) ? ~0u : gRng();
                gValueWritten = Draw(3) ? ~0u : gRng();
                gValueEverWritten = 0;
                static const uint32_t kImages[] = {0x0bfa11d7, 0x6bfa11e1, 0x0bfa11e2};
                gStoreId = Draw(5) ? kImages[Draw(3)] : gRng();
                if (Draw(5)) gStored = KeyValue();
                break;
            }
            case 7: { // GZWinText
                gFace.Reset<legacy_win_faces::kTextFace>(600);
                gFace.Returns(3, &w.f);
                gFace.On(6, &IsFlagSet);
                gFace.On(12, &GetTextColor);
                gFace.On(20, &GetFont);
                gFace.On(22, &GetAlign);
                gFace.On(24, &GetXy<0>);
                gFace.On(26, &GetXy<1>);
                gFlags = gRng();
                static const uint32_t kFonts[] = {0, 3, 4, 0x68963c4c};
                gFontId = Draw(2) ? kFonts[Draw(4)] : gRng();
                gFontAnswered = Draw(4) != 0;
                gAlign = Draw(4) ? Draw(10) : gRng();
                gAlignAnswered = Draw(5) != 0;
                gNative[0] = gRng();
                gNative[1] = gRng();
                gColorAnswered = Draw(3) ? 3 : Draw(4);
                gColorWritten = Draw(3) ? 3 : Draw(4);
                gColorEver = 0;
                gRgbWritten = Draw(3) ? ~0u : gRng();
                gRgbCalls = 0;
                gRgbEver = 0;
                for (auto &pair : gXy)
                    for (int8_t &v : pair) v = int8_t(gRng());
                gXyAnswered = Draw(3) ? 3 : Draw(4);
                gXyWritten = Draw(3) ? 3 : Draw(4);
                gXyEver = 0;
                gStoreId = Draw(5) ? 0xdacf8f9c : gRng();
                if (Draw(5)) gStored = AutofitValue();
                break;
            }
            case 9: { // GZWinSpinner
                gFace.Reset<legacy_win_faces::kSpinnerFace>(600);
                gFace.Returns(3, &w.f);
                gFace.On(5, &GetDigits);
                gFace.On(7, &IsFlagSet);
                gFace.Returns(8, uint32_t(DrawInt()));
                gFace.On(11, &GetPair<0>);
                gFace.On(13, &GetStep);
                gFace.On(25, &GetTextColor);
                gFace.On(28, &GetAlign);
                gFace.On(30, &GetXy<0>);
                gFace.On(32, &GetOutlineColors);
                gFlags = gRng();
                gDigits = Draw(4) ? Draw(4) : gRng();
                gDigitsAnswered = Draw(4) != 0;
                gAlign = Draw(4) ? Draw(4) : gRng();
                gAlignAnswered = Draw(4) != 0;
                for (int32_t &p : gPairs) p = DrawInt();
                gPairAnswered = 0;
                gStep = DrawInt();
                for (uint32_t &o : gOutline) o = gRng();
                gNative[0] = gRng();
                gNative[1] = gRng();
                gColorAnswered = Draw(3) ? 7 : Draw(8);
                gColorWritten = Draw(3) ? 7 : Draw(8);
                gColorEver = 0;
                gRgbWritten = Draw(3) ? ~0u : gRng();
                gRgbCalls = 0;
                gRgbEver = 0;
                for (auto &pair : gXy)
                    for (int8_t &v : pair) v = int8_t(gRng());
                gXyAnswered = 0;
                gXyWritten = ~0u;
                gXyEver = 0;
                static const uint32_t kProperties[] = {0x8bd1333a, 0xfaa3be85};
                gStoreId = Draw(6) ? kProperties[Draw(2)] : gRng();
                if (Draw(6)) gStored = gStoreId == 0xfaa3be85 ? FontValue() : KeyValue();
                break;
            }
            case 10: { // GZWinCombo
                gFace.Reset<legacy_win_faces::kComboFace>(600);
                gFace.Returns(3, &w.f);
                gFace.On(12, &GetFont);
                gFace.Returns(17, Draw(3) ? Draw(2) : uint32_t(gRng()));
                gFace.Returns(26, uint32_t(DrawInt()));
                gFace.On(28, &GetElement);
                gFace.On(33, &GetArrowImage);
                gFace.On(35, &GetNative<5>);
                gFace.On(37, &GetButtonGutter);
                gFace.On(39, &GetXy<0>);
                gFace.On(42, &GetTextColor);
                gFace.On(46, &GetNative<3>);
                gFace.On(50, &GetNative<4>);
                static const uint32_t kFonts[] = {0, 3, 4, 0x68963c4c};
                gFontId = Draw(2) ? kFonts[Draw(4)] : gRng();
                gFontAnswered = Draw(4) != 0;
                gNative[0] = gRng();
                gNative[1] = gRng();
                gColorAnswered = Draw(3) ? 63 : Draw(64);
                gColorWritten = Draw(3) ? 63 : Draw(64);
                gColorEver = 0;
                gRgbWritten = Draw(3) ? ~0u : gRng();
                gRgbCalls = 0;
                gRgbEver = 0;
                for (int32_t &r : gRect) r = DrawInt();
                gArrowAnswered = Draw(4) != 0;
                gButton = int8_t(gRng());
                gButtonAnswered = Draw(4) != 0;
                for (auto &pair : gXy)
                    for (int8_t &v : pair) v = int8_t(gRng());
                gXyAnswered = Draw(4) != 0;
                gXyWritten = ~0u;
                gXyEver = 0;
                static const char *const kTexts[] = {"Small", "", "a \"quoted\" & <name>", "a long element past sixteen"};
                gElementCount = Draw(7);
                for (const char *&e : gElements) e = kTexts[Draw(4)];
                gElementWritten = Draw(3) ? 0xff : gRng();
                gStoreId = Draw(5) ? 0xdaa3b860 : gRng();
                if (Draw(5)) gStored = PairValue();
                break;
            }
            case 11: { // GZWinLineInput
                gFace.Reset<legacy_win_faces::kLineInputFace>(600);
                gFace.Returns(3, &w.f);
                gFace.On(7, &GetTextColor);
                gFace.Returns(10, Draw(3) ? Draw(2) : uint32_t(gRng()));
                gFace.On(12, &GetDirection);
                gFace.On(15, &GetBorderColor);
                gFace.On(18, &GetNative<7>);
                gFace.On(20, &GetFillAlpha);
                gFace.On(26, &GetValue<0>);
                gFace.Returns(27, Draw(3) ? Draw(2) : uint32_t(gRng()));
                gFace.On(30, &GetValue<1>);
                gFace.On(32, &GetSelection);
                gFace.On(34, &GetGutterRect);
                gFontId = 0;
                gFontAnswered = 0;
                gNative[0] = gRng();
                gNative[1] = gRng();
                gColorAnswered = Draw(3) ? 255 : Draw(256);
                gColorWritten = Draw(3) ? 255 : Draw(256);
                gColorEver = 0;
                gRgbWritten = Draw(3) ? ~0u : gRng();
                gRgbCalls = 0;
                gRgbEver = 0;
                gDirection = Draw(4) ? Draw(5) : gRng();
                gAlpha = Draw(4) ? DrawAlpha() : float(int32_t(gRng()));
                for (int32_t &v : gValues) v = DrawInt();
                gValueAnswered = Draw(3) ? ~0u : gRng();
                gValueWritten = Draw(3) ? ~0u : gRng();
                gValueEverWritten = 0;
                for (int8_t &b : gGutterBytes) b = int8_t(gRng());
                gStoreId = Draw(5) ? 0xfaa3be85 : gRng();
                if (Draw(6)) gStored = FontValue();
                break;
            }
            case 12: { // GZWinListBox
                gFace.Reset<legacy_win_faces::kListBoxFace>(600);
                gFace.Returns(3, &w.f);
                gFace.Returns(13, uint32_t(DrawInt()));
                gFace.Returns(16, Draw(3) ? Draw(2) : uint32_t(gRng()));
                gFace.Returns(19, Draw(3) ? Draw(2) : uint32_t(gRng()));
                gFace.Returns(53, Draw(3) ? Draw(2) : uint32_t(gRng()));
                gFace.Returns(55, Draw(3) ? Draw(2) : uint32_t(gRng()));
                gFace.On(37, &GetScrollbars);
                gFace.On(40, &GetFont);
                gFace.On(43, &GetTextColor);
                gFace.On(50, &GetElement);
                gFace.On(57, &GetXy<1>);
                gFace.On(59, &GetXy<0>);
                gFace.On(69, &GetNative<3>);
                gFace.On(71, &GetListOutline);
                static const char *const kTexts[] = {"Small", "", "a \"quoted\" & <name>", "a long element past sixteen"};
                gElementCount = Draw(7);
                // The count the face reports: the elements it has, more (the extra ones answer false) or negative.
                gFace.Returns(31, Draw(4) ? gElementCount : (Draw(2) ? gElementCount + Draw(3) : uint32_t(-int32_t(Draw(3)))));
                for (const char *&e : gElements) e = kTexts[Draw(4)];
                gElementWritten = Draw(3) ? 0xff : gRng();
                static const uint32_t kFonts[] = {0, 3, 4, 0x68963c4c};
                gFontId = Draw(2) ? kFonts[Draw(4)] : gRng();
                gFontAnswered = Draw(4) != 0;
                gNative[0] = gRng();
                gNative[1] = gRng();
                gColorAnswered = Draw(3) ? 31 : Draw(32);
                gColorWritten = Draw(3) ? 31 : Draw(32);
                gColorEver = 0;
                gRgbWritten = Draw(3) ? ~0u : gRng();
                gRgbCalls = 0;
                gRgbEver = 0;
                for (auto &pair : gXy)
                    for (int8_t &v : pair) v = int8_t(gRng());
                gXyAnswered = Draw(3) ? 3 : Draw(4);
                gXyWritten = Draw(3) ? ~0u : gRng();
                gXyEver = 0;
                for (int8_t &v : gScrollbars) v = Draw(3) ? int8_t(Draw(2)) : int8_t(gRng());
                gScrollbarsAnswered = Draw(4) != 0;
                gStoreId = Draw(5) ? 0xdacf8f9c : gRng();
                if (Draw(5)) gStored = AutofitValue();
                break;
            }
            case 13: // GZWinGen
                gFace.Reset<legacy_win_faces::kGenFace>(600);
                gFace.Returns(3, &w.f);
                gFace.On(5, &IsFlagSet);
                gFace.On(15, &GetBackImageType);
                gFace.Returns(22, uint32_t(DrawInt()));
                gFace.On(28, &GetGenGutters);
                gFlags = gRng();
                gGenType = Draw(4) ? int32_t(Draw(4)) : int32_t(gRng());
                gGenTypeAnswered = Draw(4) != 0;
                for (int32_t &g : gGenGutters) g = DrawInt();
                gStoreId = Draw(5) ? 0xab428c5d : gRng();
                if (Draw(5)) gStored = KeyValue();
                break;
            case 14: { // GZWinOptGrp
                gFace.Reset<legacy_win_faces::kOptGrpFace>(600);
                gFace.Returns(3, &w.f);
                gFace.On(4, &IsFlagSet);
                gFace.On(7, &GetGenGutters);
                gFace.On(15, &GetTextColor);
                gFace.Returns(21, uint32_t(DrawInt()));
                gFace.On(30, &GetOptOutline);
                gFace.On(37, &GetOptAutofill);
                w.f.On(126, &GetOptProperty);
                w.f.Settle<37, 3>("uua");
                gFlags = gRng();
                for (int32_t &g : gGenGutters) g = DrawInt();
                for (int i = 0; i < 2; ++i) gOptValues[i] = Draw(3) ? FontValue() : nullptr;
                gOptValues[2] = Draw(3) ? ResourceValue() : nullptr;
                gOptValues[3] = Draw(4) ? KeyValue() : nullptr;
                gOptValues[4] = Draw(3) ? AutofitValue() : nullptr;
                gNative[0] = gRng();
                gNative[1] = gRng();
                for (uint32_t &o : gOutline) o = gRng();
                gColorAnswered = Draw(3) ? 63 : Draw(64);
                gColorWritten = Draw(3) ? 63 : Draw(64);
                gColorEver = 0;
                gRgbWritten = Draw(3) ? ~0u : gRng();
                gRgbCalls = 0;
                gRgbEver = 0;
                gAutoType = Draw(3) ? Draw(3) : gRng();
                gAutoSize = DrawInt();
                gAutoAnswered = Draw(4) != 0;
                gFontId = 0;
                gFontAnswered = 0;
                break;
            }
            case 15: { // GZWinTextEdit
                gFace.Reset<legacy_win_faces::kTextEditFace>(600);
                gFace.Returns(5, &w.f);
                gFace.Returns(86, Draw(2) ? uint32_t(0x11ff ^ (1u << Draw(16))) : uint32_t(gRng()));
                gFace.On(11, &GetFont);
                static const char *const kInitial[] = {"", "hello", "a \"quoted\" & <text>", "line\nbreak"};
                gFace.Returns(48, kInitial[Draw(4)]);
                gFace.On(14, &GetTextColor);
                gFace.On(19, &GetNative<6>);
                gFace.Returns(22, uint32_t(gRng()));
                gFace.On(40, &GetTeOutline);
                gFace.Returns(32, uint32_t(DrawInt()));
                gFace.Returns(34, uint32_t(DrawInt()));
                gFace.Returns(41, uint32_t(DrawInt()));
                gFace.Returns(43, uint32_t(DrawInt()));
                gFace.Returns(71, uint32_t(Draw(2) ? 0x7701 : 0x7700));
                gFace.On(73, &GetTeWords<0>);
                gFace.On(75, &GetTeWords<1>);
                gFace.On(79, &GetTeWords<2>);
                gFace.Returns(88, uint32_t(DrawInt()));
                gFace.On(92, &GetTeRect<3>);
                gFace.On(91, &GetTeRect<4>);
                gFace.On(60, &GetTePrompt);
                w.f.On(126, &GetOptProperty);
                static const uint32_t kFonts[] = {0, 3, 4, 0x68963c4c};
                gFontId = Draw(2) ? kFonts[Draw(4)] : gRng();
                gFontAnswered = Draw(4) != 0;
                for (uint32_t &t : gTeWords) t = uint32_t(DrawInt());
                gTeAnswers = Draw(3) ? 63 : Draw(64);
                for (int32_t &r : gRect) r = DrawInt();
                static const char *const kPrompts[] = {"", "Type here", "a \"quoted\" & <prompt>"};
                gPromptText = kPrompts[Draw(3)];
                gOutlined = Draw(2) ? uint8_t(Draw(3)) : uint8_t(gRng());
                gOptValues[4] = Draw(3) ? AutofitValue() : nullptr;
                gOptValues[5] = Draw(4) ? KeyValue() : nullptr;
                gOptValues[6] = Draw(4) ? KeyValue() : nullptr;
                gOptValues[7] = Draw(4) ? PromptKeyValue() : nullptr;
                gNative[0] = gRng();
                gNative[1] = gRng();
                gColorAnswered = Draw(3) ? 127 : Draw(128);
                gColorWritten = Draw(3) ? 255 : Draw(256);
                gColorEver = 0;
                gRgbWritten = Draw(3) ? ~0u : gRng();
                gRgbCalls = 0;
                gRgbEver = 0;
                break;
            }
            case 16: { // GZWinBtn
                gFace.Reset<legacy_win_faces::kButtonFace>(600);
                gFace.Returns(3, &w.f);
                gFace.On(4, &IsFlagSet);
                gFace.Returns(11, Draw(3) ? Draw(7) : uint32_t(gRng()));
                gFace.On(21, &GetFont);
                gFace.On(25, &GetTextColor);
                gFace.On(28, &GetAlign);
                gFace.On(31, &GetBtnGutters);
                gFace.On(36, &GetTePrompt);
                gFace.On(39, &GetTeWords<2>);
                gFace.On(41, &GetBtnTipOffsets);
                gFace.On(46, &GetBtnSound);
                w.f.On(126, &GetOptProperty);
                gFlags = gRng();
                static const uint32_t kFonts[] = {0, 3, 4, 0x68963c4c};
                gFontId = Draw(2) ? kFonts[Draw(4)] : gRng();
                gFontAnswered = Draw(4) != 0;
                gAlign = Draw(2) ? Draw(4) : gRng();
                gAlignAnswered = Draw(4) != 0;
                for (int32_t &r : gRect) r = DrawInt();
                for (int32_t &o : gBtnOffsets) o = DrawInt();
                gBtnOffsetsWritten = Draw(2);
                for (uint32_t &t : gTeWords) t = uint32_t(DrawInt());
                gTeAnswers = Draw(3) ? 63 : Draw(64);
                static const char *const kTips[] = {"", "Click me", "a \"quoted\" & <tip>"};
                gPromptText = kTips[Draw(3)];
                for (uint32_t &snd : gBtnSounds) snd = Draw(3) ? 0 : uint32_t(gRng());
                gOptValues[3] = Draw(4) ? KeyValue() : nullptr;
                gOptValues[8] = Draw(4) ? PromptKeyValue() : nullptr;
                gNative[0] = gRng();
                gNative[1] = gRng();
                gColorAnswered = Draw(3) ? 63 : Draw(64);
                gColorWritten = Draw(3) ? 63 : Draw(64);
                gColorEver = 0;
                gRgbWritten = Draw(3) ? ~0u : gRng();
                gRgbCalls = 0;
                gRgbEver = 0;
                break;
            }
            case 17: { // GZWinTreeView (lane WD)
                gFace.Reset<legacy_win_faces::kTreeFace>(600);
                legacy_win_faces::InstallTreeGetters(gFace);
                w.f.On(126, &GetOptProperty);
                legacy_win_faces::TreeState &t = legacy_win_faces::gTreeState;
                t.Begin();
                t.window = &w.f;
                t.flags = gRng();
                t.gutter = DrawInt();
                t.rowHeight = DrawInt();
                t.boxSize = DrawInt();
                t.background = gRng();
                t.highlight = gRng();
                t.imageAnswered = gRng();
                t.rectWords = Draw(3) ? 15 : Draw(16);
                static const char *const kTexts[] = {"", "hello", "a \"quoted\" & <text>", "x:y", "line\nbreak", "a longer node text past sixteen"};
                static const uint32_t kAnswers[] = {0, 1, 2, 0x7701, 0x7700, 0xffffff01, 0x100};
                static const int32_t kPositions[] = {-4, -3, -2, -1, 0, 1, -5};
                for (int n = 0; n < 16; ++n) {
                    t.texts[n].mText.FromChar(kTexts[Draw(6)]);
                    for (int i = 0; i < 4; ++i) {
                        t.rects[n][i] = DrawInt();
                        t.images[n][i] = Draw(3) ? Draw(8) : uint32_t(gRng());
                        t.colors[n][i] = gRng();
                    }
                    t.parents[n] = Draw(3) ? Draw(8) : uint32_t(gRng());
                    t.expanded[n] = Draw(3) ? kAnswers[Draw(7)] : uint32_t(gRng());
                    t.selected[n] = Draw(3) ? kAnswers[Draw(7)] : uint32_t(gRng());
                    t.positions[n] = Draw(4) ? kPositions[Draw(7)] : int32_t(gRng());
                }
                static const char *const kNames[] = {"alpha", "beta", "gamma", "delta", "a:b", "", "Root", "zeta", "eta", "theta"};
                legacy_win_faces::TreeTables &tb = gTables;
                tb.Clear();
                const uint32_t imageCount = Draw(3) ? Draw(5) : Draw(10);
                for (uint32_t k = 0; k < imageCount; ++k) {
                    const char *name = kNames[Draw(10)];
                    tb.imageNumbers.emplace(legacy_tree::TreeName(name), Draw(16));
                    tb.numberImages.emplace(int32_t(Draw(8)), legacy_tree::TreeName(kNames[Draw(10)]));
                    if (Draw(2)) tb.imageResources.emplace(legacy_tree::TreeName(name), legacy_tree::ImageResource{uint32_t(gRng()), uint32_t(gRng())});
                }
                const uint32_t nodeCount = Draw(3) ? Draw(5) : Draw(10);
                for (uint32_t k = 0; k < nodeCount; ++k) {
                    const char *name = kNames[Draw(10)];
                    tb.nodeNumbers.emplace(legacy_tree::TreeName(name), Draw(16));
                    tb.numberNodes.emplace(int32_t(Draw(8)), legacy_tree::TreeName(kNames[Draw(10)]));
                    if (Draw(2)) tb.nodeFonts.emplace(legacy_tree::TreeName(name), Draw(8));
                }
                gOptValues[0] = Draw(3) ? FontValue() : nullptr;
                bool imageLoop = false;
                gOptValues[9] = TableValue(tb.imageNumbers, &imageLoop);
                gOptValues[10] = TableValue(tb.numberImages);
                // The image loop looks every name up in the resource table: no guard in the original either.
                gOptValues[11] = imageLoop ? new cRZVariant(uint32_t(uintptr_t(&tb.imageResources))) : TableValue(tb.imageResources);
                gOptValues[12] = TableValue(tb.nodeFonts);
                bool nodeLoop = false;
                gOptValues[13] = TableValue(tb.nodeNumbers, &nodeLoop);
                // The node loop looks every parent up in the number table: the original has no guard for its absence.
                gOptValues[14] = nodeLoop ? new cRZVariant(uint32_t(uintptr_t(&tb.numberNodes))) : TableValue(tb.numberNodes);
                gRgbWritten = Draw(3) ? ~0u : gRng();
                gRgbCalls = 0;
                gRgbEver = 0;
                gRgbForce = 6; // the first node's four colours: the bytes are locals with no earlier value
                gRgbSpan = 4;
                break;
            }
            case 18: { // GZWinFileBrowser (lane WF)
                gFace.Reset<legacy_win_faces::kBrowserFace>(600);
                legacy_win_faces::InstallFlBrowserGetters(gFace);
                w.f.On(126, &GetFlProperty);
                legacy_win_faces::FlBrowserState &b = legacy_win_faces::gFlBrowserState;
                b = legacy_win_faces::FlBrowserState();
                b.window = &w.f;
                static const char *const kStrings[] = {"", "C:\\Users", "a \"quoted\" & <text>", "x:y", "name.ext", "a longer string past sixteen"};
                auto str = [&] { return kStrings[Draw(6)]; };
                b.path = str();
                b.selName = str();
                b.selExt = str();
                b.backText = str();
                b.selType = Draw(4) ? int32_t(Draw(3)) : int32_t(Draw(2) ? Draw(8) : gRng());
                for (uint32_t &v : b.sorts) v = Draw(4) ? Draw(4) : (Draw(2) ? Draw(9) : uint32_t(gRng()));
                b.directorySort = Draw(4) ? Draw(4) : (Draw(2) ? Draw(9) : uint32_t(gRng()));
                static const uint32_t kColumns[] = {0, 1, 2, 5, 3, 4, 6, 0xffffffff};
                for (uint32_t &v : b.columns) v = Draw(4) ? kColumns[Draw(8)] : uint32_t(gRng());
                for (auto &set : b.colors)
                    for (uint32_t &v : set) v = gRng();
                for (int32_t &v : b.alignments) v = Draw(4) ? int32_t(Draw(5)) : int32_t(Draw(2) ? Draw(9) : gRng());
                b.iconWidth = DrawInt();
                b.extensionWidth = DrawInt();
                b.rowHeight = DrawInt();
                b.driveListSize = DrawInt();
                static const uint32_t kBytes[] = {0, 1, 2, 0xff, 0x100, 0x101, 0xffffff01};
                b.lineEnabled = Draw(3) ? kBytes[Draw(7)] : uint32_t(gRng());
                b.columnEnabled = Draw(3) ? kBytes[Draw(7)] : uint32_t(gRng());
                b.transparent = Draw(3) ? kBytes[Draw(7)] : uint32_t(gRng());
                b.outline = Draw(3) ? kBytes[Draw(7)] : uint32_t(gRng());
                b.lineColor = gRng();
                b.columnColor = gRng();
                b.highlight[0] = gRng();
                b.highlight[1] = gRng();
                b.background = gRng();
                b.entryCount = Draw(3) ? Draw(3) : Draw(10);
                b.entryWrites = gRng();
                for (uint32_t i = 0; i < 10; ++i) {
                    b.labels[i] = str();
                    b.images[i] = gRng();
                    b.numbers[i] = DrawInt();
                }
                BuildFlProperties(false);
                gRgbWritten = ~0u;
                gRgbCalls = 0;
                gRgbEver = 0;
                break;
            }
            case 19: { // GZWinGrid (lane WF)
                gFace.Reset<legacy_win_faces::kGridFace>(600);
                legacy_win_faces::InstallFlGridGetters(gFace);
                w.f.On(126, &GetFlProperty);
                legacy_win_faces::FlGridState &g = legacy_win_faces::gFlGridState;
                g = legacy_win_faces::FlGridState();
                g.window = &w.f;
                g.flags = Draw(3) ? uint32_t(gRng()) : (Draw(2) ? 0x19bffu : 0u);
                g.flagNoise = gRng();
                g.fontColor = gRng();
                g.outlineColor = gRng();
                g.gridColors[0] = gRng();
                g.gridColors[1] = gRng();
                g.textAlign = DrawInt();
                static const uint32_t kBytes[] = {0, 1, 2, 0xff, 0x100, 0x101, 0xffffff01};
                g.wrapping = Draw(3) ? kBytes[Draw(7)] : uint32_t(gRng());
                for (int32_t &v : g.gutters) v = DrawInt();
                for (int32_t &v : g.selection) v = DrawInt();
                g.defaultWidth = DrawInt();
                g.defaultHeight = DrawInt();
                for (int32_t &v : g.headers) v = DrawInt();
                g.headerWrites = Draw(3) ? 3 : Draw(4);
                BuildFlProperties(true);
                gRgbWritten = ~0u;
                gRgbCalls = 0;
                gRgbEver = 0;
                break;
            }
            case 8: // GZWinSlider
                gFace.Reset<legacy_win_faces::kSliderFace>(600);
                gFace.Returns(3, &w.f);
                gFace.Returns(7, uint32_t(DrawInt()));
                gFace.Returns(9, uint32_t(DrawInt()));
                gFace.On(11, &GetDirection);
                gFace.Returns(4, uint32_t(DrawInt()));
                gDirection = Draw(4) ? Draw(4) : gRng();
                gStoreId = Draw(5) ? 0x6b4bcbd4 : gRng();
                if (Draw(5)) gStored = KeyValue();
                break;
            }
            for (auto &c : gColors)
                for (uint8_t &b : c) b = uint8_t(gRng());
            if (gStored) gStored->AddRef();
            for (cRZVariant *v : gOptValues)
                if (v) v->AddRef();
            static const uint32_t kOriginals[kWriters] = {0x86fc7d, 0x8791f3, 0x872b33, 0x876ea4, 0x872f45, 0x87751e, 0x877189, 0x87822e, 0x8777a6, 0x877979, 0x870b57, 0x87551d, 0x875ccb, 0x8730b3, 0x8764bf, 0x878762, 0x86ff03, 0x8792f2, 0x87117a, 0x873913};
            if (side) {
                switch (which) {
                case 0: WriteBitmapAttributes(reinterpret_cast<cIGZWinBitmap *>(&gFace), &out.mText); break;
                case 1: WriteTextTickerAttributes(reinterpret_cast<cIGZWinTextTicker *>(&gFace), &out.mText); break;
                case 2: WriteFlatRectAttributes(reinterpret_cast<cIGZWinFlatRect *>(&gFace), &out.mText); break;
                case 3: WriteOutlineAttributes(reinterpret_cast<cIGZWinOutline *>(&gFace), &out.mText); break;
                case 4: WriteFoldersAttributes(reinterpret_cast<cIGZWinFolders *>(&gFace), &out.mText); break;
                case 5: WriteScrollbarAttributes(reinterpret_cast<cIGZWinScrollbar *>(&gFace), &out.mText); break;
                case 6: WriteScrollbar2Attributes(reinterpret_cast<cIGZWinScrollbar2 *>(&gFace), &out.mText); break;
                case 7: WriteTextAttributes(reinterpret_cast<cIGZWinText *>(&gFace), &out.mText); break;
                case 8: WriteSliderAttributes(reinterpret_cast<cIGZWinSlider *>(&gFace), &out.mText); break;
                case 9: WriteSpinnerAttributes(reinterpret_cast<cIGZWinSpinner *>(&gFace), &out.mText); break;
                case 10: WriteComboAttributes(reinterpret_cast<cIGZWinCombo *>(&gFace), &out.mText); break;
                case 11: WriteLineInputAttributes(reinterpret_cast<cIGZWinLineInput *>(&gFace), &out.mText); break;
                case 12: WriteListBoxAttributes(reinterpret_cast<cIGZWinListBox *>(&gFace), &out.mText); break;
                case 13: WriteGenAttributes(reinterpret_cast<cIGZWinGen *>(&gFace), &out.mText); break;
                case 14: WriteOptGrpAttributes(reinterpret_cast<cIGZWinOptGrp *>(&gFace), &out.mText); break;
                case 15: WriteTextEditAttributes(reinterpret_cast<cIGZWinTextEdit *>(&gFace), &out.mText); break;
                case 16: WriteBtnAttributes(reinterpret_cast<cIGZWinButton *>(&gFace), &out.mText); break;
                case 17: WriteTreeViewAttributes(reinterpret_cast<cIGZWinTreeView *>(&gFace), &out.mText); break;
                case 18: WriteFileBrowserAttributes(reinterpret_cast<cIGZWinFileBrowser *>(&gFace), &out.mText); break;
                case 19: WriteGridAttributes(reinterpret_cast<cIGZWinGrid *>(&gFace), &out.mText); break;
                }
            } else {
                // The grid walk ends by the fault set up in the fixture when a matrix has entries (ORIGINAL-BUG above).
                int armed = -1;
                if (which == 19)
                    for (int i = 0; i < 3; ++i)
                        if (gFlHasEntries[i] && gFlArm[i]) {
                            gFlArm[i](gFlMatrix[i]);
                            armed = i;
                        }
                const uint32_t code = CallWriter(ctx.Original<WriterFn>(kOriginals[which]), &out.mText);
                DisarmMatrix();
                results[side].push_back(code == (armed >= 0 ? EXCEPTION_ACCESS_VIOLATION : 0u) ? 1u : 0u);
                endedByFault = armed >= 0;
                if (armed >= 0) {
                    // The unwound call keeps its references to the matrix's property and holder.
                    gFlValues[2 + armed]->Release();
                    gFlKeepers[armed]->Release();
                }
            }
            if (side) results[side].push_back(1u);
            PushText(results[side], out.mText.mString);
            if (gStored) {
                results[side].push_back(gStored->AddRef());
                gStored->Release();
                gStored->Release();
                gStored = nullptr;
            }
            if (which >= 18) {
                // The holders' counts (the writer's own releases), then the properties.
                for (cIGZUnknown *&k : gFlKeepers) {
                    if (!k) continue;
                    results[side].push_back(k->AddRef());
                    k->Release();
                }
                for (cRZVariant *&v : gFlFonts) {
                    if (!v) continue;
                    results[side].push_back(v->AddRef());
                    v->Release();
                }
                for (cRZVariant *&v : gFlValues) {
                    if (!v) continue;
                    results[side].push_back(v->AddRef());
                    v->Release();
                }
                for (cRZVariant *&v : gFlFonts) {
                    if (v) v->Release();
                    v = nullptr;
                }
                for (cRZVariant *&v : gFlValues) {
                    if (v) v->Release();
                    v = nullptr;
                }
                for (cIGZUnknown *&k : gFlKeepers) {
                    if (k) k->Release();
                    k = nullptr;
                }
            }
            for (cRZVariant *&v : gOptValues) {
                if (!v) continue;
                results[side].push_back(v->AddRef());
                v->Release();
                v->Release();
                v = nullptr;
            }
            logs[side] = drawfx::gLog;
            // Ours goes on to the sections after the one the original's fault ended in (they hold nothing): their
            // lookups (the window, the property) come after everything the original asked, which is compared whole.
            if (side && endedByFault && logs[1].size() > logs[0].size()) logs[1].erase(logs[1].begin() + logs[0].size(), logs[1].end());
            if (which == 17) {
                legacy_win_faces::gTreeState.End();
                gTables.Clear();
            }
        }
        if (!svcfx::Same(ctx, results[0], results[1], "LegacyScriptWriter_Classes", seed)) break;
        if (!drawfx::Same(ctx, logs[0], logs[1], "LegacyScriptWriter_Classes log", seed)) break;
    }
}

} // namespace legacy_script_classes_test
