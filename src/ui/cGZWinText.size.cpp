#include "ui/cGZWinText.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZWin.h"
#include "rz/MiscHelpersB.h"
#include "rz/QueryInterfaces.h"
#include "ui/GZWinHelpers.h"

#include <string>

// cGZWinText (src/ui/cGZWinText.h). Calls the original makes through a vtable go through the object's own vtables
// (VirtualCall), so subclasses and the tests' recording vtables see them.

namespace {

using Fn0 = uint32_t(__thiscall *)(void *);
using Fn1 = uint32_t(__thiscall *)(void *, uint32_t);
using Fn2 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
using Fn3 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
using Fn9 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                   uint32_t, uint32_t);
using Bool0 = bool(__thiscall *)(void *);
using Bool1 = bool(__thiscall *)(void *, uint32_t);

// cIGZWin slots (the cGZWin face at +0).
enum WinSlot : int {
    kWinAddRef = 1,
    kWinDoMessage = 3,
    kWinGetWidth = 49,
    kWinGetHeight = 50,
    kWinGetLeft = 51,
    kWinGetTop = 52,
    kWinGetArea = 55,
    kWinSetSize = 62,
    kWinIsFlagSet = 82,
    kWinSetFlag = 83,
    kWinInvalidate = 105,
    kWinGetFillColor = 142,
    kWinColorFromRGB = 143,
    kWinColorToRGB = 144,
};

// cIGZWinText slots (the face at +0xf8).
enum TextSlot : int {
    kTextIsStyleSet = 6,
    kTextSetStyle = 7,
    kTextSetColorRGB = 8,
    kTextSetColor = 9,
    kTextGetColor = 12,
    kTextResetColor = 14,
    kTextSetFont = 18,
};

// cIGZFont slots.
enum FontSlot : int {
    kFontColor = 12,
    kFontShadowColor = 13,
    kFontLineHeight = 35,
    kFontLineCount = 44,   // (text, length, width): lines when wrapped to width
    kFontExtent = 48,      // (text, length, rect *out)
    kFontWidth = 49,       // (text, length): width on one line
    kFontDrawBox = 53,     // (context, rect, text, length, format, colour, shadow, clip, 0)
};

constexpr uint32_t kDefaultFont = 0x68963c4c;
constexpr uint32_t kColorsAreFont = 0x10000000;
constexpr uint32_t kFontColorUnset = 0x8000000;

uint32_t U(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

template <typename T> T &At(void *base, uint32_t offset)
{
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(base) + offset);
}

cGZWin *AsGZWin(void *win) { return static_cast<cGZWin *>(win); }

} // namespace

// ---------------------------------------------------------------------------------------------------------
// Construction and destruction

cGZWinText *cGZWinText::Construct()
{
    OriginalFunction<void *(__thiscall *)(void *), 0x92ff2a>()(this); // cGZWin constructor
    At<const void *>(this, 0) = GameAddress(kVTable);
    mTextVTable = GameAddress(kTextVTable);
    mFont.mObject = nullptr;
    mTextRect[2] = 8;
    mTextRect[3] = 8;
    mTextRect[0] = 0;
    mTextRect[1] = 0;
    mFontID = 0;
    mMarginX = 2;
    mMarginY = 2;
    mExtra[0] = 0;
    mExtra[1] = 0;
    mStyle = 6;
    mFrameColor = 0;
    mAlignment = 0;
    At<uint32_t>(this, 0xe8) &= ~0x8000u;
    mFormat = 0;
    mColor[0] = kUnset;
    mColor[1] = kUnset;
    return this;
}

void cGZWinText::Destruct()
{
    At<const void *>(this, 0) = GameAddress(kVTable);
    mTextVTable = GameAddress(kTextVTable);
    Teardown();
    if (mFont.mObject) VirtualCall<Fn0, 2>(mFont.mObject);
    OriginalFunction<void(__thiscall *)(void *), 0x930094>()(this); // cGZWin destructor
}

cGZWinText *cGZWinText::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

// ---------------------------------------------------------------------------------------------------------
// cIGZWin slots

bool cGZWinText::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid != kIid) return qi::GZWinQueryInterface(this, nullptr, riid, ppv);
    *ppv = this ? Text() : nullptr;
    VirtualCall<Fn0, kWinAddRef>(this);
    return true;
}

bool cGZWinText::DoMessage(uint32_t *message)
{
    if (message[0] == 5 || message[0] == 6)
        return OriginalFunction<bool(__thiscall *)(void *, uint32_t *), 0x930c91>()(this, message); // cGZWin's
    if (!VirtualCall<Bool1, kTextIsStyleSet>(Text(), 1) || !At<void *>(this, 0x58)) return false;
    if (message[0] - 7 <= 7) {
        message[1] += VirtualCall<Fn0, kWinGetLeft>(this);
        message[2] += VirtualCall<Fn0, kWinGetTop>(this);
    }
    return VirtualCall<Bool1, kWinDoMessage>(At<void *>(this, 0x58), U(message));
}

bool cGZWinText::Activate()
{
    cGZWin *win = AsGZWin(this);
    if (win->TestFlag4000()) return true;
    win->Activate();
    if (mFont.mObject) return true;
    void *service = GetCachedServiceObjectB();
    if (!service) return true;
    uint32_t font = VirtualCall<Fn1, 5>(service, mFontID ? mFontID : kDefaultFont);
    if (!font) font = VirtualCall<Fn1, 5>(service, kDefaultFont);
    VirtualCall<Fn1, kTextSetFont>(Text(), font);
    return true;
}

bool cGZWinText::Teardown()
{
    cGZWin *win = AsGZWin(this);
    if (!win->TestFlag4000()) return true;
    mFont.Assign(nullptr);
    return win->Teardown();
}

void cGZWinText::SetArea(int32_t left, int32_t top, int32_t right, int32_t bottom)
{
    const int32_t oldWidth = static_cast<int32_t>(VirtualCall<Fn0, kWinGetWidth>(this));
    const int32_t oldHeight = static_cast<int32_t>(VirtualCall<Fn0, kWinGetHeight>(this));
    OriginalFunction<void(__thiscall *)(void *, int32_t, int32_t, int32_t, int32_t), 0x932914>()(this, left, top, right,
                                                                                                 bottom); // cGZWin's
    const int32_t dx = static_cast<int32_t>(static_cast<uint32_t>(right) - static_cast<uint32_t>(left));
    const int32_t dy = static_cast<int32_t>(static_cast<uint32_t>(bottom) - static_cast<uint32_t>(top));
    const int32_t width = dx >= 0 ? dx : 0;
    const int32_t height = dy >= 0 ? dy : 0;
    if (width != oldWidth || height != oldHeight) Layout();
}

uint32_t cGZWinText::SetCaption(void *text)
{
    OriginalFunction<void(__thiscall *)(void *, void *), 0x932bdd>()(this, text); // cGZWin::SetCaption
    Layout();
    return VirtualCall<Fn0, kWinInvalidate>(this);
}

bool cGZWinText::Draw()
{
    int32_t *area = &At<int32_t>(this, 0x34);
    if (mStyle & 4) {
        const uint32_t color = VirtualCall<Fn0, kWinGetFillColor>(this);
        VirtualCall<Fn1, 21>(At<void *>(this, 0x88), color);
        VirtualCall<Fn1, 36>(At<void *>(this, 0x88), U(area));
    }
    void *font = mFont.mObject;
    const uint32_t length = At<uint32_t>(this, 0x74);
    if (font && length) {
        const std::string &caption = At<std::string>(this, 0x64);
        int32_t box[4] = {mTextRect[0], mTextRect[1], mTextRect[2], mTextRect[3]};
        box[1] += area[1];
        box[3] += area[1];
        box[0] += area[0];
        box[2] += area[0];
        uint32_t color;
        uint32_t shadow;
        if (mStyle & kColorsAreFont) {
            color = kFontColorUnset;
            shadow = kFontColorUnset;
        } else {
            const uint32_t first = mColor[0];
            shadow = VirtualCall<Fn1, kWinColorFromRGB>(this, mColor[1]);
            color = VirtualCall<Fn1, kWinColorFromRGB>(this, first);
        }
        VirtualCall<Fn9, kFontDrawBox>(font, At<uint32_t>(this, 0x84), U(box), U(caption.data()), length, mFormat, color,
                                       shadow, 0, 0);
    }
    if (mStyle & 8) GZWinDrawFrame(At<void *>(this, 0x88), area, mFrameColor, 1);
    return true;
}

uint32_t cGZWinText::SetBackgroundColor(uint32_t value)
{
    OriginalFunction<void(__thiscall *)(void *, uint32_t), 0x932c3a>()(this, value); // cGZWin's slot 119
    return VirtualCall<Fn0, kWinInvalidate>(this);
}

// ---------------------------------------------------------------------------------------------------------
// Non-virtual members

void cGZWinText::Layout()
{
    if (At<uint32_t>(this, 0x74) == 0 || mAlignment > 8) return;
    int32_t top;
    int32_t extent[4];
    switch (mAlignment % 3) {
    case 0: {
        // The whole area, less the margins.
        VirtualCall<Fn1, kWinGetArea>(this, U(mTextRect));
        const int32_t x = mMarginX;
        const int32_t y = mMarginY;
        const int32_t left = mTextRect[0];
        const int32_t width = mTextRect[2] - x - left;
        const int32_t height = mTextRect[3] - y - mTextRect[1];
        mTextRect[0] = x;
        mTextRect[1] = y;
        mTextRect[2] = width;
        mTextRect[3] = height;
        return;
    }
    case 1: {
        Measure(extent, 0, 0);
        const int32_t textHeight = extent[3] - extent[1];
        top = (static_cast<int32_t>(VirtualCall<Fn0, kWinGetHeight>(this)) - textHeight) / 2;
        break;
    }
    default: {
        Measure(extent, 0, 0);
        const int32_t textHeight = extent[3] - extent[1];
        top = static_cast<int32_t>(VirtualCall<Fn0, kWinGetHeight>(this)) - mMarginY - textHeight;
        break;
    }
    }
    const int32_t textHeight = extent[3] - extent[1];
    const int32_t x = mMarginX;
    mTextRect[2] = static_cast<int32_t>(VirtualCall<Fn0, kWinGetWidth>(this)) - x;
    mTextRect[3] = textHeight + top;
    mTextRect[1] = top;
    mTextRect[0] = x;
}

void cGZWinText::Measure(int32_t *out, uint32_t wrapToContent, uint32_t splitLines)
{
    void *font = mFont.mObject;
    if (!font) {
        out[0] = 0;
        out[1] = 0;
        out[2] = 1;
        out[3] = 1;
        return;
    }
    const std::string &caption = At<std::string>(this, 0x64);
    const uint32_t length = At<uint32_t>(this, 0x74);
    if (!(mStyle & 2)) {
        VirtualCall<Fn3, kFontExtent>(font, U(caption.data()), length, U(out));
        return;
    }
    int32_t width;
    int32_t height;
    if (!static_cast<uint8_t>(wrapToContent)) {
        // Wrapped to the window's width less the margins.
        width = static_cast<int32_t>(VirtualCall<Fn0, kWinGetWidth>(this)) - 2 * mMarginX;
        if (width < 1) width = 1;
        const int32_t lines = static_cast<int32_t>(
            VirtualCall<Fn3, kFontLineCount>(mFont.mObject, U(caption.data()), length, static_cast<uint32_t>(width)));
        height = lines * static_cast<int32_t>(VirtualCall<Fn0, kFontLineHeight>(mFont.mObject));
    } else if (static_cast<uint8_t>(splitLines)) {
        // As wide as the widest line (each line after the first measured from its '\n').
        width = 1;
        size_t next = caption.find('\n', 0);
        if (next >= At<uint32_t>(this, 0x74)) {
            width = static_cast<int32_t>(VirtualCall<Fn2, kFontWidth>(mFont.mObject, U(caption.data()),
                                                                      At<uint32_t>(this, 0x74)));
        } else {
            size_t start = 0;
            do {
                const size_t end = next;
                const int32_t w = static_cast<int32_t>(VirtualCall<Fn2, kFontWidth>(
                    mFont.mObject, U(caption.data() + start), static_cast<uint32_t>(end - start)));
                if (width < w) width = w;
                start = end;
                next = caption.find('\n', end + 1);
                const uint32_t total = At<uint32_t>(this, 0x74);
                if (next >= total) {
                    const int32_t last = static_cast<int32_t>(VirtualCall<Fn2, kFontWidth>(
                        mFont.mObject, U(caption.data() + start), static_cast<uint32_t>(total - start)));
                    if (width < last) width = last;
                }
            } while (next < At<uint32_t>(this, 0x74));
        }
        const int32_t lines = static_cast<int32_t>(VirtualCall<Fn3, kFontLineCount>(
            mFont.mObject, U(caption.data()), At<uint32_t>(this, 0x74), static_cast<uint32_t>(width)));
        height = static_cast<int32_t>(VirtualCall<Fn0, kFontLineHeight>(mFont.mObject)) * lines;
    } else {
        // At most 60 line heights wide.
        const int32_t lineHeight = static_cast<int32_t>(VirtualCall<Fn0, kFontLineHeight>(font));
        const int32_t limit = lineHeight * 60;
        const int32_t lines = static_cast<int32_t>(
            VirtualCall<Fn3, kFontLineCount>(mFont.mObject, U(caption.data()), length, static_cast<uint32_t>(limit)));
        height = lines * lineHeight;
        const int32_t w =
            static_cast<int32_t>(VirtualCall<Fn2, kFontWidth>(mFont.mObject, U(caption.data()), length));
        width = limit;
        if (lines <= 1) width = limit < w ? limit : w;
    }
    out[0] = 0;
    out[1] = 0;
    out[2] = width;
    out[3] = height;
}

// ---------------------------------------------------------------------------------------------------------
// The cIGZWinText face

bool cGZWinTextFace::SizeToText(uint32_t wrapToContent, uint32_t splitLines)
{
    cGZWinText *text = Obj();
    int32_t extent[4];
    text->Measure(extent, wrapToContent, splitLines);
    extent[2] += text->mMarginX * 2;
    extent[3] += text->mMarginY * 2;
    VirtualCall<Fn2, kWinSetSize>(text, static_cast<uint32_t>(extent[2] - extent[0]),
                                  static_cast<uint32_t>(extent[3] - extent[1]));
    return true;
}

bool cGZWinTextFace::SetStyle(uint32_t mask, uint32_t on)
{
    cGZWinText *text = Obj();
    const uint8_t set = static_cast<uint8_t>(on);
    text->mStyle = set ? text->mStyle | mask : text->mStyle & ~mask;
    if (mask == 2) {
        text->Layout();
        VirtualCall<Fn0, kWinInvalidate>(text);
    } else if (mask == 4) {
        // A filled label is opaque: window flag 0x20000 off, and 0x40000 follows it.
        VirtualCall<Fn2, kWinSetFlag>(text, 0x20000, static_cast<uint8_t>(set ^ 1));
        const uint8_t transparent = static_cast<uint8_t>(VirtualCall<Fn1, kWinIsFlagSet>(text, 0x20000));
        VirtualCall<Fn2, kWinSetFlag>(text, 0x40000, transparent);
        VirtualCall<Fn0, kWinInvalidate>(text);
    }
    return true;
}

bool cGZWinTextFace::SetColorRGB(uint32_t index, uint32_t rgb)
{
    cGZWinText *text = Obj();
    if (index == 0) text->mColor[0] = rgb;
    else if (index == 1) text->mColor[1] = rgb;
    else return false;
    void *font = text->mFont.mObject;
    if (font) {
        bool same = false;
        const uint32_t first = text->mColor[0];
        const uint32_t fontFirst = VirtualCall<Fn0, kFontColor>(font);
        if (first == VirtualCall<Fn1, kWinColorToRGB>(text, fontFirst)) {
            const uint32_t second = text->mColor[1];
            const uint32_t fontSecond = VirtualCall<Fn0, kFontShadowColor>(text->mFont.mObject);
            same = second == VirtualCall<Fn1, kWinColorToRGB>(text, fontSecond);
        }
        VirtualCall<Fn2, kTextSetStyle>(this, kColorsAreFont, same);
    } else {
        VirtualCall<Fn2, kTextSetStyle>(this, kColorsAreFont, 0);
    }
    VirtualCall<Fn0, kWinInvalidate>(text);
    return true;
}

uint32_t cGZWinTextFace::SetColor(uint32_t index, uint32_t color)
{
    const uint32_t rgb = VirtualCall<Fn1, kWinColorToRGB>(Obj(), color);
    return VirtualCall<Fn2, kTextSetColorRGB>(this, index, rgb);
}

uint32_t cGZWinTextFace::SetTextColor(uint32_t color) { return VirtualCall<Fn2, kTextSetColor>(this, 0, color); }

bool cGZWinTextFace::GetColorRGB(uint32_t index, uint32_t *out)
{
    if (index > 1) return false;
    const uint32_t rgb = Obj()->mColor[index];
    if (rgb == cGZWinText::kUnset) return false;
    *out = rgb;
    return true;
}

bool cGZWinTextFace::GetColor(uint32_t index, uint32_t *out)
{
    if (index > 1) return false;
    cGZWinText *text = Obj();
    if (text->mColor[index] == cGZWinText::kUnset) return false;
    *out = VirtualCall<Fn1, kWinColorFromRGB>(text, text->mColor[index]);
    return true;
}

uint32_t cGZWinTextFace::GetTextColor(uint32_t *out) { return VirtualCall<Fn2, kTextGetColor>(this, 0, U(out)); }

uint32_t cGZWinTextFace::ResetColor(uint32_t index)
{
    void *font = Obj()->mFont.mObject;
    if (index == 0) return VirtualCall<Fn2, kTextSetColor>(this, 0, VirtualCall<Fn0, kFontColor>(font));
    if (index == 1) return VirtualCall<Fn2, kTextSetColor>(this, 1, VirtualCall<Fn0, kFontShadowColor>(font));
    return 0;
}

uint32_t cGZWinTextFace::ResetTextColor() { return VirtualCall<Fn1, kTextResetColor>(this, 0); }
uint32_t cGZWinTextFace::SetFill(uint32_t on) { return VirtualCall<Fn2, kTextSetStyle>(this, 4, on); }
uint32_t cGZWinTextFace::IsFilled() { return VirtualCall<Fn1, kTextIsStyleSet>(this, 4); }

bool cGZWinTextFace::SetFont(void *font)
{
    cGZWinText *text = Obj();
    if (font == text->mFont.mObject) return true;
    text->mFont.Assign(font);
    if (text->mFont.mObject) {
        if (text->mColor[0] == cGZWinText::kUnset)
            VirtualCall<Fn2, kTextSetColor>(this, 0, VirtualCall<Fn0, kFontColor>(text->mFont.mObject));
        if (text->mColor[1] == cGZWinText::kUnset)
            VirtualCall<Fn2, kTextSetColor>(this, 1, VirtualCall<Fn0, kFontShadowColor>(text->mFont.mObject));
    }
    VirtualCall<Fn2, kTextSetColorRGB>(this, 0, text->mColor[0]);
    VirtualCall<Fn2, kTextSetColorRGB>(this, 1, text->mColor[1]);
    text->Layout();
    VirtualCall<Fn0, kWinInvalidate>(text);
    return true;
}

bool cGZWinTextFace::SetFontID(uint32_t id)
{
    cGZWinText *text = Obj();
    text->mFontID = id;
    if (AsGZWin(text)->TestFlag4000()) {
        const uint32_t wanted = text->mFontID ? text->mFontID : kDefaultFont;
        void *service = GetCachedServiceObjectB();
        const uint32_t font = VirtualCall<Fn1, 5>(service, wanted);
        if (font) VirtualCall<Fn1, kTextSetFont>(this, font);
    }
    return true;
}

bool cGZWinTextFace::GetFontID(uint32_t *out)
{
    const uint32_t id = Obj()->mFontID;
    if (!id) return false;
    *out = id;
    return true;
}

bool cGZWinTextFace::SetAlignment(uint32_t alignment)
{
    cGZWinText *text = Obj();
    if (alignment == text->mAlignment) return true;
    if (alignment > 8) return false;
    text->mFormat = alignment / 3;
    text->mAlignment = alignment;
    text->Layout();
    VirtualCall<Fn0, kWinInvalidate>(text);
    return true;
}

bool cGZWinTextFace::GetAlignment(uint32_t *out)
{
    *out = Obj()->mAlignment;
    return true;
}

bool cGZWinTextFace::SetMargins(int8_t x, int8_t y)
{
    cGZWinText *text = Obj();
    text->mMarginX = x;
    text->mMarginY = y;
    text->Layout();
    return true;
}

bool cGZWinTextFace::GetMargins(int8_t *x, int8_t *y)
{
    *x = Obj()->mMarginX;
    *y = Obj()->mMarginY;
    return true;
}

bool cGZWinTextFace::SetExtra(int8_t a, int8_t b)
{
    Obj()->mExtra[0] = a;
    Obj()->mExtra[1] = b;
    return true;
}

bool cGZWinTextFace::GetExtra(int8_t *a, int8_t *b)
{
    *a = Obj()->mExtra[0];
    *b = Obj()->mExtra[1];
    return true;
}

uint32_t cGZWinTextFace::SetFrame(uint32_t on, uint32_t color)
{
    cGZWinText *text = Obj();
    text->mStyle = static_cast<uint8_t>(on) ? text->mStyle | 8 : text->mStyle & ~8u;
    text->mFrameColor = color;
    return color;
}

uint32_t *cGZWinTextFace::GetFrame(uint8_t *on, uint32_t *color)
{
    *on = static_cast<uint8_t>((Obj()->mStyle >> 3) & 1);
    *color = Obj()->mFrameColor;
    return color;
}
