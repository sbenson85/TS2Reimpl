#include "ui/cGZWinBtn.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZWin.h"
#include "misc/UtilitiesC.h"
#include "rz/MiscHelpersB.h"
#include "rz/QueryInterfaces.h"
#include "rz/cRZCallbackTimer.h"
#include "ui/GZWinHelpers.h"

#include <string>
#include "gz/cGZClipboardManagerClipboard.h"

// cGZWinBtn (src/ui/cGZWinBtn.h). Calls the original makes through a vtable go through the object's own vtables
// (VirtualCall), so subclasses (cTSWinBtn, cTSWinTextLabel) and the tests' recording vtables see them.

namespace {

using Fn0 = uint32_t(__thiscall *)(void *);
using Fn1 = uint32_t(__thiscall *)(void *, uint32_t);
using Fn2 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
using Fn3 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
using Fn4 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t);
using Fn5 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using Fn7 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using Fn9 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                   uint32_t, uint32_t);
using Bool0 = bool(__thiscall *)(void *);
using Bool1 = bool(__thiscall *)(void *, uint32_t);
using Bool2 = bool(__thiscall *)(void *, uint32_t, uint32_t);
using Bool3 = bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
using Bool5 = bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using Bool7 = bool(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

// cIGZWin slots used here (the cGZWin face at +4).
enum WinSlot : int {
    kWinDoMessage = 3,
    kWinGetWidth = 49,
    kWinGetHeight = 50,
    kWinSetSize = 62,
    kWinClientToScreen = 75, // (int *x, int *y): adds the window's screen position
    kWinIsFlagSet = 82,
    kWinHide = 85,
    kWinInvalidate = 105,
    kWinHitTest = 134,
    kWinGetBackgroundColor = 141,
    kWinColorFromRGB = 143,
    kWinColorToRGB = 144,
    kWinSendToTarget = 166,  // (target, kind, a, b, c)
    kWinNotify = 168,        // (target, kind, message, id, value)
};

// cIGZWinBtn slots called on the object itself.
enum BtnSlot : int {
    kBtnAsWin = 3,
    kBtnSetStyle = 5,
    kBtnSetImage = 12,
    kBtnSetFont = 19,
    kBtnSetColor = 23,
    kBtnSetMargins = 29,
    kBtnSizeToFit = 33,
    kBtnGetSizeLimits = 34,
    kBtnClearTooltip = 37,
    kBtnSetSound = 45,
    kBtnGetID = 48,
    kBtnSetCaption = 49,
    kBtnGetCaption = 50,
    kBtnNotifyToggled = 51,
    kBtnNotifyClicked = 52,
};

// cRZCallbackTimer (vtable 0xe61bb8) slots.
enum TimerSlot : int {
    kTimerStart = 5,       // (interval, object)
    kTimerStop = 7,
    kTimerIsRunning = 8,
    kTimerSetCallback = 9, // (fn, data)
};

constexpr uint32_t kNotifyToggled = 0x287259f6;
constexpr uint32_t kNotifyClicked = 0x287259f0;
constexpr uint32_t kNotifyOn = 0x287259f7;
constexpr uint32_t kNotifyPressed = 0x287259f8;
constexpr uint32_t kNotifyMouseOver = 0x287259f9;
constexpr uint32_t kDefaultFont = 0x68963c4c;
constexpr uint32_t kFallbackFont = 0x68963c54;
constexpr uint32_t kDefaultCheckImage = 0xe8963ef1;
constexpr uint32_t kShowTooltipMessage = 0x22c010d0;
constexpr uint32_t kHideTooltipMessage = 0x22c010d8;
constexpr int32_t kNoLimit = 0x7fffffff;

uint32_t U(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

template <typename T> T &At(void *base, uint32_t offset)
{
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(base) + offset);
}

cGZWin *AsGZWin(void *win) { return static_cast<cGZWin *>(win); }

// The cGZWin face's notification target (+0x5c of the face) and draw rect (+0x34 of the face).
uint32_t Target(cGZWinBtn *self) { return At<uint32_t>(self, 0x60); }
int32_t *DrawRect(cGZWinBtn *self) { return &At<int32_t>(self, 0x38); }

// The message the tooltip functions send to the window manager's tooltip window (0x9c5ab4's object, slot 3).
struct TooltipMessage {
    uint32_t id;
    uint32_t data;
    uint32_t zero1;
    uint32_t zero2;
};

// The tooltip request ShowTooltip builds (0x741869 initialises it; 0x2c bytes).
struct TooltipRequest {
    uint32_t version;   // +0x00 1
    uint32_t flags;     // +0x04
    void *text;         // +0x08 the tooltip string (a cIGZString)
    int32_t area[4];    // +0x0c anchor rect (screen)
    uint32_t color[3];  // +0x1c
    uint32_t id;        // +0x28 written by the receiver
};
static_assert(sizeof(TooltipRequest) == 0x2c);

} // namespace

// ---------------------------------------------------------------------------------------------------------
// Construction and destruction

cGZWinBtn *cGZWinBtn::Construct()
{
    mVTable = GameAddress(kInterfaceVTable);
    OriginalFunction<void *(__thiscall *)(void *), 0x92ff2a>()(Win()); // cGZWin constructor
    mVTable = GameAddress(kVTable);
    At<const void *>(Win(), 0) = GameAddress(kWinVTable);
    mStyle = 0;
    mName.Construct();
    At<uint32_t>(this, 0x120) = 0; // mTextX, mTextY
    At<uint32_t>(this, 0x124) = 0; // mTextWidth, mTextHeight
    mAlignment = 0;
    At<uint32_t>(this, 0x12e) = 0; // the four margins
    mFont.mObject = nullptr;
    mFontID = 0;
    mType = 0;
    mImageMode = 0;
    mImageRect[0] = 0;
    mImageRect[1] = 0;
    mImageRect[2] = 0;
    mImageRect[3] = 0;
    mImageX = 0;
    mImageOffsetX = 0;
    mImageOffsetY = 0;
    mImage.mObject = nullptr;
    mTooltip.Construct();
    mTooltipID = 0;
    mTooltipFlags = 0;
    At<uint32_t>(this, 0x1b0) = 0; // mTooltipX, mTooltipY
    mFlashCount = 0;
    reinterpret_cast<cRZCallbackTimer *>(mTimer)->Construct();
    mStyle |= 0x6a0;
    reinterpret_cast<cGZWinBtnWin *>(Win())->SetFlag(0x800, 0);
    AsGZWin(Win())->SetF4Ok(0xffc0c0c0);
    for (int32_t &color : mColor) color = kUnsetColor;
    for (uint32_t &sound : mSound) sound = 0;
    mTooltipColor[0] = 0;
    mTooltipColor[2] = 0;
    mTooltipColor[1] = 0xffffe0;
    return this;
}

void cGZWinBtn::Destruct()
{
    mVTable = GameAddress(kVTable);
    At<const void *>(Win(), 0) = GameAddress(kWinVTable);
    reinterpret_cast<cGZWinBtnWin *>(Win())->Teardown();
    reinterpret_cast<cRZCallbackTimer *>(mTimer)->Destruct();
    mTooltip.Destruct();
    if (mImage.mObject) VirtualCall<Fn0, 2>(mImage.mObject);
    if (mFont.mObject) VirtualCall<Fn0, 2>(mFont.mObject);
    mName.Destruct();
    OriginalFunction<void(__thiscall *)(void *), 0x930094>()(Win()); // cGZWin destructor
}

cGZWinBtn *cGZWinBtn::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

// ---------------------------------------------------------------------------------------------------------
// cIGZWinBtn slots

bool cGZWinBtn::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid == kIid) {
        *ppv = this;
        VirtualCall<Fn0, 1>(this);
        return true;
    }
    return qi::GZWinQueryInterface(Win(), nullptr, riid, ppv);
}

bool cGZWinBtn::IsStyleSet(uint32_t mask) { return Style(mask); }
bool cGZWinBtn::SetStyle(uint32_t mask, uint32_t on) { return SetStyleBits(mask, on); }
bool cGZWinBtn::SetOn() { return SetStyleBits(1, 1); }
bool cGZWinBtn::SetOff() { return SetStyleBits(1, 0); }
bool cGZWinBtn::IsOn() { return Style(1); }
bool cGZWinBtn::SetOnState(uint32_t on) { return SetStyleBits(1, on); }

bool cGZWinBtn::SetImage(uint32_t type, void *image, const int32_t *rect)
{
    if (type == 0) return false;
    mType = type;
    int32_t states;
    switch (type) {
    case 1:
        mImageMode = 0;
        return true;
    case 2:
    case 3:
        mImageMode = 1;
        states = 4;
        break;
    case 4:
        mImageMode = 2;
        VirtualCall<Fn2, kBtnSetStyle>(this, 0x80, 0);
        states = 8;
        break;
    default:
        return false;
    }
    if (image != mImage.mObject) mImage.Assign(image);
    if (rect) {
        mImageRect[0] = rect[0];
        mImageRect[1] = rect[1];
        mImageRect[2] = (rect[2] - rect[0]) / states + rect[0];
        mImageRect[3] = rect[3] - rect[1] + rect[1];
    } else {
        if (!image) {
            mImageMode = 0;
            return false;
        }
        mImageRect[0] = 0;
        mImageRect[1] = 0;
        mImageRect[2] = static_cast<int32_t>(VirtualCall<Fn0, 9>(image) / static_cast<uint32_t>(states));
        mImageRect[3] = static_cast<int32_t>(VirtualCall<Fn0, 10>(image));
    }
    const int32_t height = static_cast<int32_t>(VirtualCall<Fn0, kWinGetHeight>(Win()));
    const int32_t width = static_cast<int32_t>(VirtualCall<Fn0, kWinGetWidth>(Win()));
    return Layout(width, height);
}

bool cGZWinBtn::SetType(uint32_t type)
{
    int32_t rect[4] = {mImageRect[0], mImageRect[1], mImageRect[2], mImageRect[3]};
    int32_t states = 1;
    if (mType == 2 || mType == 3) states = 4;
    else if (mType == 4) states = 8;
    rect[2] = (rect[2] - rect[0]) * states + rect[0];
    VirtualCall<Fn3, kBtnSetImage>(this, type, U(mImage.mObject), U(rect));
    VirtualCall<Fn0, kWinInvalidate>(Win());
    return true;
}

int32_t cGZWinBtn::GetImageWidth() { return mImageRect[2] - mImageRect[0]; }
int32_t cGZWinBtn::GetImageHeight() { return mImageRect[3] - mImageRect[1]; }

bool cGZWinBtn::GetImageRect(int32_t *out)
{
    out[0] = mImageRect[0];
    out[1] = mImageRect[1];
    out[2] = mImageRect[2];
    out[3] = mImageRect[3];
    return true;
}

bool cGZWinBtn::SetImageResource(void *image)
{
    VirtualCall<Fn3, kBtnSetImage>(this, mType, U(image), 0);
    VirtualCall<Fn0, kWinInvalidate>(Win());
    return true;
}

bool cGZWinBtn::GetImage(void **out)
{
    *out = mImage.mObject;
    return true;
}

bool cGZWinBtn::SetFont(void *font)
{
    if (mFont.mObject != font) {
        mFont.Assign(font);
        if (mFont.mObject) {
            const int32_t first = static_cast<int32_t>(VirtualCall<Fn0, 12>(mFont.mObject));
            const int32_t second = static_cast<int32_t>(VirtualCall<Fn0, 13>(mFont.mObject));
            for (int i = 0; i < 6; i += 2) {
                if (mColor[i] == kUnsetColor) mColor[i] = first;
                if (mColor[i + 1] == kUnsetColor) mColor[i + 1] = second;
            }
        }
        const int32_t height = static_cast<int32_t>(VirtualCall<Fn0, kWinGetHeight>(Win()));
        const int32_t width = static_cast<int32_t>(VirtualCall<Fn0, kWinGetWidth>(Win()));
        Layout(width, height);
    }
    return true;
}

bool cGZWinBtn::SetFontID(uint32_t id)
{
    mFontID = id;
    if (AsGZWin(Win())->TestFlag4000() || mFont.mObject) {
        const uint32_t wanted = mFontID ? mFontID : kDefaultFont;
        void *service = GetCachedServiceObjectB();
        const uint32_t font = VirtualCall<Fn1, 5>(service, wanted);
        VirtualCall<Fn1, kBtnSetFont>(this, font);
    }
    return true;
}

bool cGZWinBtn::GetFontID(uint32_t *out)
{
    const uint32_t id = mFontID;
    if (id) *out = id;
    return id != 0;
}

bool cGZWinBtn::SetColorRGB(uint32_t index, uint32_t rgb)
{
    const int32_t color = static_cast<int32_t>(VirtualCall<Fn1, kWinColorFromRGB>(Win(), rgb));
    if (mColor[index] != color) {
        mColor[index] = color;
        VirtualCall<Fn0, kWinInvalidate>(Win());
    }
    return true;
}

bool cGZWinBtn::SetColor(uint32_t index, int32_t argb)
{
    if (mColor[index] != argb) {
        mColor[index] = argb;
        VirtualCall<Fn0, kWinInvalidate>(Win());
    }
    return true;
}

bool cGZWinBtn::GetColorRGB(uint32_t index, uint32_t *out)
{
    const int32_t color = mColor[index];
    if (color != kUnsetColor) *out = VirtualCall<Fn1, kWinColorToRGB>(Win(), static_cast<uint32_t>(color));
    return color != kUnsetColor;
}

bool cGZWinBtn::GetColor(uint32_t index, int32_t *out)
{
    const int32_t color = mColor[index];
    if (color != kUnsetColor) *out = color;
    return color != kUnsetColor;
}

bool cGZWinBtn::ResetColor(uint32_t index)
{
    VirtualCall<Fn2, kBtnSetColor>(this, index, static_cast<uint32_t>(kUnsetColor));
    return true;
}

bool cGZWinBtn::SetAlignment(uint32_t alignment)
{
    mAlignment = alignment;
    if (alignment == 0) mTextFormat = 1;
    else if (alignment == 1) mTextFormat = 0;
    else if (alignment == 2) mTextFormat = 2;
    const int32_t height = static_cast<int32_t>(VirtualCall<Fn0, kWinGetHeight>(Win()));
    const int32_t width = static_cast<int32_t>(VirtualCall<Fn0, kWinGetWidth>(Win()));
    Layout(width, height);
    return true;
}

bool cGZWinBtn::GetAlignment(uint32_t *out)
{
    *out = mAlignment;
    return true;
}

bool cGZWinBtn::SetMargins(uint8_t left, uint8_t top, uint8_t right, uint8_t bottom)
{
    mMargin[0] = left;
    mMargin[1] = top;
    mMargin[2] = right;
    mMargin[3] = bottom;
    const int32_t height = static_cast<int32_t>(VirtualCall<Fn0, kWinGetHeight>(Win()));
    const int32_t width = static_cast<int32_t>(VirtualCall<Fn0, kWinGetWidth>(Win()));
    Layout(width, height);
    return true;
}

uint32_t cGZWinBtn::SetMargins2(uint32_t horizontal, uint32_t vertical)
{
    return VirtualCall<Fn4, kBtnSetMargins>(this, horizontal, vertical, horizontal, vertical);
}

bool cGZWinBtn::GetMargins(uint32_t *left, uint32_t *top, uint32_t *right, uint32_t *bottom)
{
    *left = mMargin[0];
    *top = mMargin[1];
    *bottom = mMargin[3];
    *right = mMargin[2];
    return true;
}

bool cGZWinBtn::GetMargins2(uint32_t *left, uint32_t *top)
{
    *left = mMargin[0];
    *top = mMargin[1];
    return true;
}

bool cGZWinBtn::SizeToFit()
{
    int32_t limits[4];
    if (!VirtualCall<Bool1, kBtnGetSizeLimits>(this, U(limits))) return false;
    if (limits[0] != 0 || limits[1] != 0)
        VirtualCall<Fn2, kWinSetSize>(Win(), static_cast<uint32_t>(limits[0]), static_cast<uint32_t>(limits[1]));
    return true;
}

bool cGZWinBtn::GetSizeLimits(int32_t *out)
{
    const int32_t imageWidth = mImageRect[2] - mImageRect[0];
    const int32_t imageHeight = mImageRect[3] - mImageRect[1];
    const int32_t textWidth = mTextWidth;
    const int32_t captionWidth = textWidth + mMargin[2] + mMargin[0];
    const int32_t captionHeight = mMargin[3] + mMargin[1] + mTextHeight;
    const int32_t *area = DrawRect(this);
    int32_t width = imageWidth;
    int32_t height = imageHeight;
    switch (mType) {
    case 1:
        out[0] = captionWidth;
        out[1] = captionHeight;
        out[2] = kNoLimit;
        out[3] = kNoLimit;
        return true;
    case 2:
    case 3:
        if (Style(4)) {
            if (Style(0x40)) {
                out[0] = area[2] - area[0];
            } else {
                out[0] = captionWidth;
                if (captionWidth < imageWidth) out[0] = imageWidth;
            }
            out[1] = captionHeight;
            if (captionHeight < imageHeight) out[1] = imageHeight;
        } else {
            out[0] = imageWidth;
            out[1] = imageHeight;
        }
        out[2] = kNoLimit;
        out[3] = kNoLimit;
        return true;
    case 4:
        if (Style(4)) {
            out[2] = kNoLimit;
            if (height < captionHeight) height = captionHeight;
            out[0] = captionWidth - mImageRect[0] + mImageRect[2];
            out[1] = height;
        } else {
            out[0] = width;
            height = mImageRect[3] - mImageRect[1];
            out[1] = height;
            out[2] = width > 0 ? width : kNoLimit;
        }
        out[3] = height > 0 ? height : kNoLimit;
        return true;
    default:
        if (mImage.mObject) {
            out[0] = width;
            height = mImageRect[3] - mImageRect[1];
            out[1] = height;
            if (Style(4)) {
                if (width < textWidth) {
                    out[0] = textWidth;
                    width = textWidth;
                }
                if (height < mTextHeight) {
                    out[1] = mTextHeight;
                    height = mTextHeight;
                }
            }
        } else {
            if (Style(4)) {
                width = textWidth;
                out[0] = width;
                height = mTextHeight;
            } else {
                width = area[2] - area[0];
                out[0] = width;
                height = area[3] - area[1];
            }
            out[1] = height;
        }
        out[2] = width > 0 ? width : kNoLimit;
        out[3] = height > 0 ? height : kNoLimit;
        return true;
    }
}

bool cGZWinBtn::SetTooltip(void *text)
{
    if (VirtualCall<Fn0, 7>(text) == 0) {
        VirtualCall<Fn2, kBtnSetStyle>(this, 0x100, 0);
        VirtualCall<Fn0, kBtnClearTooltip>(this);
    } else {
        VirtualCall<Fn1, 17>(&mTooltip, U(text));
        VirtualCall<Fn2, kBtnSetStyle>(this, 0x100, 1);
    }
    return true;
}

bool cGZWinBtn::GetTooltip(void *out)
{
    VirtualCall<Fn1, 17>(out, U(&mTooltip));
    return true;
}

bool cGZWinBtn::ClearTooltip()
{
    HideTooltip();
    mTooltip.mString.erase(0);
    return true;
}

bool cGZWinBtn::SetTooltipFlags(uint32_t flags)
{
    mTooltipFlags = flags;
    return true;
}

bool cGZWinBtn::GetTooltipFlags(uint32_t *out)
{
    *out = mTooltipFlags;
    return true;
}

bool cGZWinBtn::SetTooltipOffset(int16_t x, int16_t y)
{
    mTooltipX = x;
    mTooltipY = y;
    return true;
}

bool cGZWinBtn::GetTooltipOffset(int32_t *x, int32_t *y)
{
    *x = mTooltipX;
    *y = mTooltipY;
    return true;
}

bool cGZWinBtn::SetTooltipColor(uint32_t index, uint8_t r, uint8_t g, uint8_t b)
{
    mTooltipColor[index] = static_cast<uint32_t>(r) << 16 | static_cast<uint32_t>(g) << 8 | b;
    return true;
}

bool cGZWinBtn::GetTooltipColor(uint32_t index, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = static_cast<uint8_t>(mTooltipColor[index] >> 16);
    *g = static_cast<uint8_t>(mTooltipColor[index] >> 8);
    *b = static_cast<uint8_t>(mTooltipColor[index]);
    return true;
}

uint32_t cGZWinBtn::Flash(int32_t count, uint32_t interval)
{
    if (VirtualCall<Bool0, kTimerIsRunning>(mTimer)) VirtualCall<Fn0, kTimerStop>(mTimer);
    mFlashCount = count;
    const int32_t height = static_cast<int32_t>(VirtualCall<Fn0, kWinGetHeight>(Win()));
    const int32_t width = static_cast<int32_t>(VirtualCall<Fn0, kWinGetWidth>(Win()));
    Layout(width, height);
    VirtualCall<Fn2, kTimerSetCallback>(mTimer, U(reinterpret_cast<void *>(&cGZWinBtn::FlashTick)), U(this));
    return VirtualCall<Fn2, kTimerStart>(mTimer, interval, 0);
}

bool cGZWinBtn::SetSound(uint32_t index, uint32_t sound)
{
    mSound[index] = sound;
    return true;
}

uint32_t cGZWinBtn::GetSound(uint32_t index) { return mSound[index]; }
uint32_t cGZWinBtn::SetID(uint32_t id) { return At<uint32_t>(this, 0x14) = id; }
uint32_t cGZWinBtn::GetID() { return At<uint32_t>(this, 0x14); }

bool cGZWinBtn::SetCaption(void *text)
{
    OriginalFunction<void(__thiscall *)(void *, void *), 0x932bdd>()(Win(), text); // cGZWin::SetCaption
    int32_t height;
    int32_t width;
    if (VirtualCall<Fn0, 7>(text) != 0) {
        VirtualCall<Fn2, kBtnSetStyle>(this, 4, 1);
        UpdateTextExtent();
    } else {
        VirtualCall<Fn2, kBtnSetStyle>(this, 4, 0);
    }
    height = static_cast<int32_t>(VirtualCall<Fn0, kWinGetHeight>(Win()));
    width = static_cast<int32_t>(VirtualCall<Fn0, kWinGetWidth>(Win()));
    return Layout(width, height);
}

void *cGZWinBtn::GetCaption() { return reinterpret_cast<uint8_t *>(this) + 0x64; }

bool cGZWinBtn::NotifyToggled()
{
    const uint32_t on = Style(1);
    const uint32_t id = VirtualCall<Fn0, kBtnGetID>(this);
    VirtualCall<Fn5, kWinNotify>(Win(), Target(this), 3, kNotifyToggled, id, on);
    return true;
}

bool cGZWinBtn::NotifyClicked()
{
    const uint32_t id = VirtualCall<Fn0, kBtnGetID>(this);
    VirtualCall<Fn5, kWinNotify>(Win(), Target(this), 3, kNotifyClicked, id, 0);
    return true;
}

// ---------------------------------------------------------------------------------------------------------
// Non-virtual members

bool cGZWinBtn::SetStyleBits(uint32_t mask, uint32_t on)
{
    const uint8_t set = static_cast<uint8_t>(on);
    const uint32_t old = mStyle;
    const uint32_t now = set ? old | mask : old & ~mask;
    if (now == old) return true;
    mStyle = now;
    uint32_t message;
    switch (mask) {
    case 1: message = kNotifyOn; break;
    case 0x1000: message = kNotifyPressed; break;
    case 0x2000: message = kNotifyMouseOver; break;
    case 4:
    case 0x20:
    case 0x40: message = 0; break;
    case 0x1000000:
        VirtualCall<Fn0, kWinInvalidate>(Win());
        return true;
    default: return true;
    }
    if (message) {
        const uint32_t id = VirtualCall<Fn0, kBtnGetID>(this);
        VirtualCall<Fn5, kWinNotify>(Win(), Target(this), 3, message, id, set);
    }
    const int32_t height = static_cast<int32_t>(VirtualCall<Fn0, kWinGetHeight>(Win()));
    const int32_t width = static_cast<int32_t>(VirtualCall<Fn0, kWinGetWidth>(Win()));
    Layout(width, height);
    return true;
}

bool cGZWinBtn::HideTooltip()
{
    void *tooltips = static_cast<void *>(GetMessageServer());
    if (tooltips) {
        TooltipMessage message;
        message.id = kHideTooltipMessage;
        message.data = mTooltipID;
        message.zero1 = 0;
        message.zero2 = 0;
        VirtualCall<Fn1, kWinDoMessage>(tooltips, U(&message));
    }
    return true;
}

bool cGZWinBtn::Layout(int32_t width, int32_t height)
{
    void *win = Win();
    VirtualCall<Fn0, kWinInvalidate>(win);
    UpdateTextExtent();
    const int32_t imageWidth = mImageRect[2] - mImageRect[0];
    const int32_t imageHeight = mImageRect[3] - mImageRect[1];
    const bool lit = (mFlashCount & 1) != 0;
    int32_t text[4];
    switch (mImageMode) {
    case 0:
        if (!Style(4)) return true;
        text[3] = static_cast<int32_t>(VirtualCall<Fn0, kWinGetHeight>(win)) - mMargin[3];
        text[2] = static_cast<int32_t>(VirtualCall<Fn0, kWinGetWidth>(win)) - mMargin[2];
        text[0] = mMargin[0];
        text[1] = mMargin[1];
        PlaceText(text);
        return true;
    case 1:
        mImageX = mImageRect[0];
        if (mType == 2) {
            if (VirtualCall<Bool1, kWinIsFlagSet>(win, 2)) {
                if (lit) mImageX += imageWidth * 3;
                else if (Style(0x1000)) mImageX = mImageX + imageWidth * 2;
                else if (Style(0x2000)) mImageX = imageWidth * 3 + mImageX;
                else mImageX = mImageX + imageWidth;
            }
        } else if (VirtualCall<Bool1, kWinIsFlagSet>(win, 2)) {
            if (lit) mImageX += imageWidth * 3;
            else if (Style(0x1000) || Style(1)) mImageX += imageWidth + imageWidth;
            else if (Style(0x2000)) mImageX = imageWidth * 3 + mImageX;
            else mImageX = mImageX + imageWidth;
        }
        mImageOffsetX = 0;
        mImageOffsetY = 0;
        if (!Style(4)) return true;
        text[0] = mMargin[0];
        text[1] = mMargin[1];
        text[2] = width - mMargin[2];
        text[3] = height - mMargin[3];
        PlaceText(text);
        return true;
    case 2: {
        mImageX = mImageRect[0];
        int32_t frame;
        if (!VirtualCall<Bool1, kWinIsFlagSet>(win, 2)) {
            frame = Style(1) ? 6 : 7;
        } else if (lit) {
            frame = Style(1) ? 2 : 3;
        } else if (Style(0x1000)) {
            frame = Style(1) ? 4 : 5;
        } else if (Style(0x2000)) {
            frame = Style(1) ? 2 : 3;
        } else {
            const int32_t x = mImageX;
            if (Style(1)) mImageX = x;
            else mImageX = x + imageWidth;
            frame = 0;
        }
        if (frame) mImageX += imageWidth * frame;
        break;
    }
    case 3:
        mImageX = mImageRect[0];
        if (VirtualCall<Bool1, kWinIsFlagSet>(win, 2)) {
            if (lit) {
                mImageX += imageWidth;
            } else if (Style(0x1000)) {
                mImageX = mImageX + imageWidth * 2;
            } else {
                const int32_t x = mImageX;
                Style(0x2000);
                mImageX = imageWidth + x;
            }
        }
        mImageOffsetX = 0;
        mImageOffsetY = (height - imageHeight) / 2;
        if (!Style(4)) return true;
        text[3] = static_cast<int32_t>(VirtualCall<Fn0, kWinGetHeight>(win)) - mMargin[3];
        text[2] = static_cast<int32_t>(VirtualCall<Fn0, kWinGetWidth>(win)) - mMargin[2];
        text[0] = mMargin[0];
        text[1] = mMargin[1];
        PlaceText(text);
        return true;
    case 4:
        mImageX = mImageRect[0];
        if (VirtualCall<Bool1, kWinIsFlagSet>(win, 2)) {
            if (Style(0x1000)) {
                mImageX = mImageX + imageWidth * 2;
            } else {
                const int32_t x = mImageX;
                if (Style(1)) mImageX = imageWidth * 3 + x;
                else mImageX = x + imageWidth;
            }
        }
        break;
    default:
        return false;
    }
    // Modes 2 and 4: the image beside the caption, by the alignment.
    switch (mAlignment) {
    case 0:
    case 2:
        mImageOffsetX = 0;
        mImageOffsetY = (height - imageHeight) / 2;
        if (!Style(4)) return true;
        text[0] = mMargin[0] + imageWidth;
        break;
    case 1:
        mImageOffsetX = width - imageWidth;
        mImageOffsetY = (height - imageHeight) / 2;
        if (!Style(4)) return true;
        text[0] = mMargin[0];
        break;
    default:
        return false;
    }
    text[1] = mMargin[1];
    text[2] = mTextWidth + text[0];
    text[3] = mTextHeight + text[1];
    PlaceText(text);
    return true;
}

void cGZWinBtn::PlayEffect(uint32_t index)
{
    if (mSound[index] == 0) return;
    void *audio = OriginalFunction<void *(__cdecl *)(), 0x9c5b7e>()();
    if (!audio) return;
    void *sound = nullptr;
    if (VirtualCall<Bool7, 13>(audio, 1, 0x5b523ba4, U(&sound), mSound[index], 0, 0, 0))
        VirtualCall<Fn1, 12>(audio, U(sound));
    if (sound) VirtualCall<Fn0, 2>(sound);
}

bool cGZWinBtn::ShowTooltip()
{
    if (!Style(0x100) || mTooltip.mString.size() == 0) return false;
    TooltipRequest request;
    OriginalFunction<void *(__thiscall *)(void *), 0x741869>()(&request);
    request.text = &mTooltip;
    uint32_t flags = request.flags;
    if (Style(0x200)) flags |= 1;
    if (Style(0x400)) flags |= 2;
    flags |= mTooltipFlags;
    request.flags = flags;
    if (flags & 0x10000) {
        int32_t x = mTooltipX;
        int32_t y = mTooltipY;
        VirtualCall<Fn2, kWinClientToScreen>(Win(), U(&x), U(&y));
        request.area[0] = x;
        request.area[1] = y;
        request.area[2] = x;
        request.area[3] = y;
    } else {
        const int32_t *area = &At<int32_t>(this, 0x28);
        request.area[0] = area[0];
        request.area[1] = area[1];
        request.area[2] = area[2];
        request.area[3] = area[3];
    }
    request.color[0] = mTooltipColor[0];
    request.color[1] = mTooltipColor[1];
    request.color[2] = mTooltipColor[2];
    void *tooltips = static_cast<void *>(GetMessageServer());
    if (tooltips) {
        TooltipMessage message;
        message.id = kShowTooltipMessage;
        message.data = U(&request);
        message.zero1 = 0;
        message.zero2 = 0;
        VirtualCall<Fn1, kWinDoMessage>(tooltips, U(&message));
    }
    mTooltipID = request.id;
    return true;
}

void cGZWinBtn::UpdateTextExtent()
{
    if (!mFont.mObject) {
        void *service = GetCachedServiceObjectB();
        if (service) {
            if (mFontID) mFont.Assign(reinterpret_cast<void *>(VirtualCall<Fn1, 5>(service, mFontID)));
            if (!mFont.mObject) {
                mFont.Assign(reinterpret_cast<void *>(VirtualCall<Fn1, 5>(service, kFallbackFont)));
                if (!mFont.mObject) mFont.Assign(reinterpret_cast<void *>(VirtualCall<Fn1, 5>(service, kDefaultFont)));
            }
        }
    }
    void *font = mFont.mObject;
    if (!font || !Style(4)) {
        At<uint32_t>(this, 0x124) = 0; // both mTextWidth and mTextHeight
        return;
    }
    void *caption = GetCaption();
    if (Style(0x40)) {
        int32_t available;
        if (mType == 4) {
            const int32_t imageWidth = mImageRect[2] - mImageRect[0];
            available = static_cast<int32_t>(VirtualCall<Fn0, kWinGetWidth>(Win())) - mMargin[2] - mMargin[0] -
                        imageWidth;
        } else {
            available = static_cast<int32_t>(VirtualCall<Fn0, kWinGetWidth>(Win())) - mMargin[2] - mMargin[0];
        }
        uint8_t truncated;
        const uint32_t length = VirtualCall<Fn0, 7>(caption);
        const uint32_t data = VirtualCall<Fn0, 6>(caption);
        const int32_t lines = static_cast<int32_t>(
            VirtualCall<Fn5, 43>(mFont.mObject, data, length, static_cast<uint32_t>(available), 0, U(&truncated)));
        const uint32_t lineHeight = VirtualCall<Fn0, 35>(mFont.mObject);
        void *lineFont = mFont.mObject;
        const uint16_t height = static_cast<uint16_t>(lineHeight * static_cast<uint32_t>(lines));
        mTextHeight = static_cast<int16_t>(height);
        uint16_t width;
        if (lines == 1) {
            int32_t extent[4];
            const uint32_t length1 = VirtualCall<Fn0, 7>(caption);
            const uint32_t data1 = VirtualCall<Fn0, 6>(caption);
            VirtualCall<Fn3, 48>(lineFont, data1, length1, U(extent));
            width = static_cast<uint16_t>(extent[2] - extent[0]);
        } else {
            const uint32_t length1 = VirtualCall<Fn0, 7>(caption);
            const uint32_t data1 = VirtualCall<Fn0, 6>(caption);
            width = static_cast<uint16_t>(VirtualCall<Fn3, 45>(
                lineFont, data1, length1, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(height)))));
        }
        mTextWidth = static_cast<int16_t>(width);
    } else {
        int32_t extent[4];
        const uint32_t length = VirtualCall<Fn0, 7>(caption);
        const uint32_t data = VirtualCall<Fn0, 6>(caption);
        VirtualCall<Fn3, 48>(font, data, length, U(extent));
        mTextHeight = static_cast<int16_t>(extent[3] - extent[1]);
        mTextWidth = static_cast<int16_t>(extent[2] - extent[0]);
    }
}

void cGZWinBtn::PlaceText(const int32_t *rect)
{
    switch (mAlignment) {
    case 0:
        mTextX = static_cast<int16_t>((rect[2] - mTextWidth - rect[0]) / 2 + rect[0]);
        break;
    case 1:
        mTextX = static_cast<int16_t>(rect[0]);
        break;
    case 2:
        mTextX = static_cast<int16_t>(rect[2] - mTextWidth);
        break;
    default:
        mTextX = 0;
        mTextY = 0;
        return;
    }
    mTextY = static_cast<int16_t>((rect[3] - mTextHeight - rect[1]) / 2 + rect[1]);
}

bool cGZWinBtn::DrawImage()
{
    const int32_t imageWidth = mImageRect[2] - mImageRect[0];
    const int32_t imageHeight = mImageRect[3] - mImageRect[1];
    const int32_t source[4] = {mImageX, mImageRect[1], mImageX + imageWidth, mImageRect[3]};
    int32_t *area = DrawRect(this);
    void *context = At<void *>(this, 0x8c);
    const bool stretch = !Style(0x20) || Style(4);
    if (stretch && area[2] - area[0] != imageWidth && mType != 4) {
        // Nine-slice: the corner cell is a third of the state's image.
        int32_t corner[4];
        corner[0] = mImageX;
        corner[2] = imageWidth / 3 + mImageX;
        corner[1] = mImageRect[1];
        corner[3] = imageHeight / 3 + mImageRect[1];
        OriginalFunction<void(__cdecl *)(void *, void *, const int32_t *, const int32_t *, uint32_t), 0x92bd70>()(
            context, mImage.mObject, corner, area, 1);
        return true;
    }
    int32_t target[4];
    target[0] = mImageOffsetX + area[0];
    target[1] = mImageOffsetY + area[1];
    target[2] = target[0] + imageWidth;
    target[3] = target[1] + imageHeight;
    VirtualCall<Fn3, 39>(context, U(mImage.mObject), U(source), U(target));
    return true;
}

bool cGZWinBtn::DrawBevel()
{
    if (Style(0x10)) return false;
    uint8_t raised = 1;
    switch (mType) {
    case 2: raised = !Style(0x1000); break;
    case 3:
    case 4: raised = !(Style(0x1000) || Style(1)); break;
    default: break;
    }
    int32_t *area = DrawRect(this);
    // The original pushes two arguments (raised, 1) to this slot, which takes none (cGZWin's 0x254bc0 returns +0xf4).
    const uint32_t color = VirtualCall<Fn0, kWinGetBackgroundColor>(Win());
    void *context = At<void *>(this, 0x8c);
    GZWinDrawBevel(context, area, color, raised);
    if (Style(0x2000)) {
        const int32_t inner[4] = {area[0] + 1, area[1] + 1, area[2] - 1, area[3] - 1};
        GZWinDrawFrame(context, inner, 0xffffffff, 1);
    }
    return true;
}

void cGZWinBtn::DrawCaption()
{
    if (!Style(4) || !mFont.mObject) return;
    uint32_t first;
    uint32_t second;
    if (VirtualCall<Bool1, kWinIsFlagSet>(Win(), 2)) {
        if (Style(0x2000)) {
            first = static_cast<uint32_t>(mColor[2]);
            second = static_cast<uint32_t>(mColor[3]);
        } else {
            first = static_cast<uint32_t>(mColor[0]);
            second = static_cast<uint32_t>(mColor[1]);
        }
    } else {
        first = static_cast<uint32_t>(mColor[4]);
        second = static_cast<uint32_t>(mColor[5]);
    }
    const int32_t shift = Style(0x80) && Style(0x1000) ? 1 : 0;
    const std::string &caption = At<std::string>(this, 0x68);
    const uint32_t text = U(caption.data());
    const uint32_t length = static_cast<uint32_t>(caption.size());
    void *textContext = At<void *>(this, 0x88);
    int32_t *area = DrawRect(this);
    if (Style(0x40)) {
        int32_t box[4];
        const int32_t width = static_cast<int32_t>(VirtualCall<Fn0, kWinGetWidth>(Win()));
        box[0] = mMargin[0] + shift;
        box[2] = width - mMargin[2] + shift;
        int32_t top = mTextY + shift;
        box[3] = mTextY + (mTextHeight + shift);
        if (top < area[1]) top = area[1];
        box[1] = top;
        VirtualCall<Fn9, 53>(mFont.mObject, U(textContext), U(box), text, length, mTextFormat, first, second, U(area),
                             0);
    } else {
        const int32_t x = mTextX + shift + area[0];
        const int32_t y = mTextY + area[1] + shift;
        VirtualCall<Fn9, 52>(mFont.mObject, U(textContext), static_cast<uint32_t>(x), static_cast<uint32_t>(y), text,
                             length, first, second, U(area), 0);
    }
}

void __cdecl cGZWinBtn::FlashTick(cGZWinBtn *self)
{
    void *manager = GetCachedServiceObjectD();
    const uint32_t win = VirtualCall<Fn0, kBtnAsWin>(self);
    VirtualCall<Fn5, 12>(manager, win, kTimerMessage, 0, 0, 0);
}

// ---------------------------------------------------------------------------------------------------------
// The cGZWin face

bool cGZWinBtnWin::DoMessage(const uint32_t *message)
{
    if (message[0] != cGZWinBtn::kTimerMessage)
        return OriginalFunction<bool(__thiscall *)(void *, const uint32_t *), 0x930c91>()(this, message);
    cGZWinBtn *btn = Btn();
    const int32_t height = static_cast<int32_t>(VirtualCall<Fn0, kWinGetHeight>(this));
    const int32_t width = static_cast<int32_t>(VirtualCall<Fn0, kWinGetWidth>(this));
    btn->Layout(width, height);
    int32_t count = btn->mFlashCount;
    if (count != 0) {
        int32_t next = -1;
        if (count == -1) count = -2;
        if (count != -2) next = count - 1;
        btn->mFlashCount = next;
    } else if (VirtualCall<Bool0, kTimerIsRunning>(btn->mTimer)) {
        VirtualCall<Fn0, kTimerStop>(btn->mTimer);
    }
    return true;
}

bool cGZWinBtnWin::Activate()
{
    cGZWin *win = AsGZWin(this);
    if (win->TestFlag4000()) return true;
    win->Activate();
    void *service = GetCachedServiceObjectB();
    cGZWinBtn *btn = Btn();
    if (btn->Style(4) && !btn->mFont.mObject && service) {
        const uint32_t wanted = btn->mFontID ? btn->mFontID : kDefaultFont;
        const uint32_t font = VirtualCall<Fn1, 5>(service, wanted);
        VirtualCall<Fn1, kBtnSetFont>(btn, font);
    }
    if (btn->mType == 0) {
        void *image = nullptr;
        if (service) image = reinterpret_cast<void *>(VirtualCall<Fn1, 7>(service, kDefaultCheckImage));
        if (image) {
            const uint32_t rect = VirtualCall<Fn0, 12>(image);
            VirtualCall<Fn3, kBtnSetImage>(btn, 2, U(image), rect);
        } else {
            VirtualCall<Fn3, kBtnSetImage>(btn, 2, 0, 0);
        }
    }
    if (btn->Style(0x20)) VirtualCall<Fn0, kBtnSizeToFit>(btn);
    if (service) {
        for (uint32_t i = 0; i < 3; ++i) {
            if (btn->mSound[i] == 0) {
                const uint32_t sound = VirtualCall<Fn1, 29>(service, i);
                VirtualCall<Fn2, kBtnSetSound>(btn, i, sound);
            }
        }
    }
    return true;
}

bool cGZWinBtnWin::Teardown()
{
    cGZWin *win = AsGZWin(this);
    if (!win->TestFlag4000()) return true;
    cGZWinBtn *btn = Btn();
    if (VirtualCall<Bool0, kTimerIsRunning>(btn->mTimer)) VirtualCall<Fn0, kTimerStop>(btn->mTimer);
    btn->HideTooltip();
    VirtualCall<Fn0, kWinHide>(this);
    btn->mImage.Assign(nullptr);
    btn->mFont.Assign(nullptr);
    win->Teardown();
    return true;
}

bool cGZWinBtnWin::SetArea(int32_t left, int32_t top, int32_t right, int32_t bottom)
{
    const int32_t dx = static_cast<int32_t>(static_cast<uint32_t>(right) - static_cast<uint32_t>(left));
    const int32_t dy = static_cast<int32_t>(static_cast<uint32_t>(bottom) - static_cast<uint32_t>(top));
    const int32_t width = dx >= 0 ? dx : 0;
    const int32_t height = dy >= 0 ? dy : 0;
    OriginalFunction<void(__thiscall *)(void *, int32_t, int32_t, int32_t, int32_t), 0x932914>()(
        this, left, top, width + left, height + top); // cGZWin::SetArea
    return Btn()->Layout(width, height);
}

uint32_t cGZWinBtnWin::SetFlag(uint32_t mask, uint32_t on)
{
    const uint32_t before = At<uint32_t>(this, 0xe8);
    const uint32_t result =
        OriginalFunction<uint32_t(__thiscall *)(void *, uint32_t, uint32_t), 0x932c88>()(this, mask, on); // cGZWin::SetFlag
    if (before != At<uint32_t>(this, 0xe8) && mask == 2) {
        const int32_t height = static_cast<int32_t>(VirtualCall<Fn0, kWinGetHeight>(this));
        const int32_t width = static_cast<int32_t>(VirtualCall<Fn0, kWinGetWidth>(this));
        Btn()->Layout(width, height);
        VirtualCall<Fn0, kWinInvalidate>(this);
    }
    return result;
}

uint32_t cGZWinBtnWin::SetFontStyle(uint32_t value)
{
    At<uint32_t>(this, 0xbc) = value;
    cGZWinBtn *btn = Btn();
    const uint32_t caption = VirtualCall<Fn0, kBtnGetCaption>(btn);
    return VirtualCall<Fn1, kBtnSetCaption>(btn, caption);
}

bool cGZWinBtnWin::Draw()
{
    cGZWinBtn *btn = Btn();
    if (btn->mImage.mObject) btn->DrawImage();
    else btn->DrawBevel();
    if (btn->Style(4)) btn->DrawCaption();
    return true;
}

bool cGZWinBtnWin::OnKeyDown(uint32_t key, uint32_t modifiers)
{
    cGZWinBtn *btn = Btn();
    if (key == 0xd) {
        VirtualCall<Fn0, kBtnNotifyToggled>(btn);
        return true;
    }
    if (key != 0x20)
        return VirtualCall<Bool5, kWinSendToTarget>(this, At<uint32_t>(this, 0x5c), 5, key, modifiers, 0);
    if (!btn->Style(0x4000)) {
        btn->SetStyleBits(0x4000, 1);
        VirtualCall<Fn5, kWinSendToTarget>(this, U(static_cast<void *>(btn ? this : nullptr)), 7, 1, 1, modifiers);
    }
    return true;
}

uint32_t cGZWinBtnWin::OnKeyUp(uint32_t key, uint32_t modifiers)
{
    if (key != 0x20) return VirtualCall<Fn5, kWinSendToTarget>(this, At<uint32_t>(this, 0x5c), 6, key, modifiers, 0);
    cGZWinBtn *btn = Btn();
    btn->SetStyleBits(0x4000, 0);
    return VirtualCall<Fn5, kWinSendToTarget>(this, U(static_cast<void *>(btn ? this : nullptr)), 10, 1, 1, modifiers);
}

namespace {

// Toggles the on state as a click does: the click sound, the state, the toggled notification (slot 51).
void ToggleOnClick(cGZWinBtn *btn)
{
    btn->PlayEffect(2);
    btn->SetStyleBits(1, !btn->Style(1));
    VirtualCall<Fn0, kBtnNotifyToggled>(btn);
}

void SetCapture(void *win) { OriginalFunction<void(__thiscall *)(void *), 0x930f6d>()(win); }
void ReleaseCapture(void *win) { OriginalFunction<void(__thiscall *)(void *), 0x930f61>()(win); }
bool HasCapture(void *win) { return OriginalFunction<bool(__thiscall *)(void *), 0x930f07>()(win); }

} // namespace

bool cGZWinBtnWin::OnMouseDown(uint32_t, uint32_t, uint32_t)
{
    cGZWinBtn *btn = Btn();
    if (btn->Style(0x1000)) return true;
    btn->PlayEffect(0);
    SetCapture(this);
    btn->HideTooltip();
    btn->SetStyleBits(0x1000, 1);
    if (btn->Style(2) && !btn->Style(0x800)) ToggleOnClick(btn);
    VirtualCall<Fn0, kWinInvalidate>(this);
    return true;
}

bool cGZWinBtnWin::OnMouseDownR(uint32_t, uint32_t, uint32_t)
{
    cGZWinBtn *btn = Btn();
    if (btn->Style(0x1000)) return true;
    btn->PlayEffect(0);
    SetCapture(this);
    btn->HideTooltip();
    btn->SetStyleBits(0x1000, 1);
    if (btn->Style(2)) {
        if (btn->Style(0x800)) ToggleOnClick(btn);
        else VirtualCall<Fn0, kBtnNotifyClicked>(btn);
    }
    VirtualCall<Fn0, kWinInvalidate>(this);
    return true;
}

bool cGZWinBtnWin::OnMouseUp(uint32_t x, uint32_t y, uint32_t)
{
    cGZWinBtn *btn = Btn();
    const bool pressed = btn->Style(0x1000);
    if (pressed) {
        btn->PlayEffect(1);
        btn->SetStyleBits(0x1000, 0);
    }
    if (HasCapture(this)) {
        if (!btn->Style(2) && VirtualCall<Bool2, kWinHitTest>(this, x, y) && pressed) ToggleOnClick(btn);
        ReleaseCapture(this);
    }
    if (pressed) VirtualCall<Fn0, kWinInvalidate>(this);
    return true;
}

bool cGZWinBtnWin::OnMouseUpR(uint32_t x, uint32_t y, uint32_t)
{
    cGZWinBtn *btn = Btn();
    const bool pressed = btn->Style(0x1000);
    if (pressed) {
        btn->PlayEffect(1);
        btn->SetStyleBits(0x1000, 0);
    }
    if (HasCapture(this)) {
        if (!btn->Style(2) && VirtualCall<Bool2, kWinHitTest>(this, x, y) && pressed) {
            if (btn->Style(0x800)) ToggleOnClick(btn);
            else VirtualCall<Fn0, kBtnNotifyClicked>(btn);
        }
        ReleaseCapture(this);
    }
    if (pressed) VirtualCall<Fn0, kWinInvalidate>(this);
    return true;
}

bool cGZWinBtnWin::OnMouseMove(uint32_t x, uint32_t y, uint32_t)
{
    cGZWinBtn *btn = Btn();
    if (!btn->Style(0x10000000) || !HasCapture(this)) return true;
    if (btn->Style(0x1000)) {
        if (!VirtualCall<Bool2, kWinHitTest>(this, x, y)) btn->SetStyleBits(0x1000, 0);
    } else if (VirtualCall<Bool2, kWinHitTest>(this, x, y)) {
        btn->SetStyleBits(0x1000, 1);
    }
    return true;
}

bool cGZWinBtnWin::OnFocusChange(uint32_t, void *gained)
{
    cGZWinBtn *btn = Btn();
    if (gained != (btn ? static_cast<void *>(this) : nullptr)) {
        btn->SetStyleBits(0x2000, 0);
        btn->SetStyleBits(0x1000, 0);
        btn->HideTooltip();
    }
    return true;
}

bool cGZWinBtnWin::OnMouseEnter(uint32_t)
{
    cGZWinBtn *btn = Btn();
    if (!btn->Style(0x1000)) {
        btn->SetStyleBits(0x2000, 1);
        btn->ShowTooltip();
    }
    return true;
}

bool cGZWinBtnWin::OnMouseExit(uint32_t)
{
    cGZWinBtn *btn = Btn();
    btn->SetStyleBits(0x2000, 0);
    btn->HideTooltip();
    return true;
}
