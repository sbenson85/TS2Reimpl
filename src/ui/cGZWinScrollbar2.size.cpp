#include "ui/cGZWinScrollbar2.h"

#include "game/game.h"
#include "game/original.h"
#include "game/vtable.h"
#include "gz/cGZWin.h"
#include "rz/cRZCallbackTimer.h"
#include "ui/GZWinHelpers.h"

// cGZWinScrollbar2 (src/ui/cGZWinScrollbar2.h). Calls the original makes through a vtable go through the object's own
// vtables (VirtualCall), so subclasses and the tests' recording vtables see them.

namespace {

using Fn0 = uint32_t(__thiscall *)(void *);
using Fn1 = uint32_t(__thiscall *)(void *, uint32_t);
using Fn2 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t);
using Fn3 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t);
using Fn4 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t);
using Fn5 = uint32_t(__thiscall *)(void *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using Bool0 = bool(__thiscall *)(void *);

// cIGZWin slots (the cGZWin face at +0).
enum WinSlot : int {
    kWinAddRef = 1,
    kWinGetID = 78,
    kWinInvalidate = 105,
    kWinNotify = 166,       // (target, message, window, id, data)
    kWinPost = 168,         // (target, message, 0, 0, 0)
};

// cIGZWinScrollbar2 slots (the face at +0xf8).
enum BarSlot : int {
    kBarWindow = 3, // the object (shared lea eax, [ecx - 0xf8])
    kBarSetValue = 6,
};

// cRZCallbackTimer slots (the timer at +0x14c).
enum TimerSlot : int {
    kTimerSetInterval = 3,
    kTimerStart = 5,
    kTimerStop = 7,
    kTimerIsRunning = 8,
    kTimerSetCallback = 9,
};

// Image slots (the three images).
enum ImageSlot : int {
    kImageWidth = 9,
    kImageHeight = 10,
};

constexpr uint32_t kFirstRepeat = 300; // ms before an arrow or the track starts repeating
constexpr uint32_t kRepeat = 70;       // ms between repeats

uint32_t U(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

template <typename T> T &At(void *base, uint32_t offset)
{
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(base) + offset);
}

cGZWin *AsGZWin(void *win) { return static_cast<cGZWin *>(win); }

// 32-bit wrapping arithmetic, as the original's imul.
int32_t Mul(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b)); }

bool HasCapture(void *win) { return OriginalFunction<bool(__thiscall *)(void *), 0x930f07>()(win); }
void ReleaseCapture(void *win) { OriginalFunction<void(__thiscall *)(void *), 0x930f61>()(win); }
bool SetCapture(void *win) { return OriginalFunction<bool(__thiscall *)(void *), 0x930f6d>()(win); }

// Stops the repeat timer if it runs.
void StopRepeat(void *timer)
{
    if (VirtualCall<Bool0, kTimerIsRunning>(timer)) VirtualCall<Fn0, kTimerStop>(timer);
}

} // namespace

// ---------------------------------------------------------------------------------------------------------
// Construction and destruction

cGZWinScrollbar2 *cGZWinScrollbar2::Construct()
{
    OriginalFunction<void *(__thiscall *)(void *), 0x92ff2a>()(this); // cGZWin constructor
    At<const void *>(this, 0) = GameAddress(kVTable);
    mBarVTable = GameAddress(kBarVTable);
    mValue = 0;
    mMin = 0;
    mMax = 0x100;
    mPageSize = 0x10;
    mPageOverlap = 1;
    mLineStep = 1;
    for (RZUnknownSlot &image : mImage) image.mObject = nullptr;
    mArrowLength = 0;
    mTrackEnd = 0;
    mTrackMiddle = 0;
    mThumbEnd = 0;
    mThumbMiddle = 0;
    mThumbPos = 0;
    mThumbLength = 1;
    mDirty = 0;
    mVertical = 1;
    mHotPart = kNone;
    mPressedPart = kNone;
    reinterpret_cast<cRZCallbackTimer *>(mTimer)->Construct();
    At<uint32_t>(this, 0xe8) &= ~0x8000u;
    mMinWidth = 0;
    mMinHeight = 0;
    SetArea(0, 0, 0x20, 0x60);
    return this;
}

void cGZWinScrollbar2::Destruct()
{
    At<const void *>(this, 0) = GameAddress(kVTable);
    mBarVTable = GameAddress(kBarVTable);
    reinterpret_cast<cRZCallbackTimer *>(mTimer)->Destruct();
    for (int i = 2; i >= 0; --i)
        if (mImage[i].mObject) VirtualCall<Fn0, 2>(mImage[i].mObject);
    OriginalFunction<void(__thiscall *)(void *), 0x930094>()(this); // cGZWin destructor
}

cGZWinScrollbar2 *cGZWinScrollbar2::DeletingDestructor(uint32_t flags)
{
    Destruct();
    if (flags & 1) ::operator delete(this, kSize);
    return this;
}

// ---------------------------------------------------------------------------------------------------------
// cIGZWin slots

bool cGZWinScrollbar2::QueryInterface(uint32_t riid, void **ppv)
{
    if (riid == kIid) {
        *ppv = this ? Bar() : nullptr;
    } else if (riid == 1 || riid == 0x22ba0121 || riid == 0xe98b2f57) {
        *ppv = this;
    } else {
        return false;
    }
    VirtualCall<Fn0, kWinAddRef>(this);
    return true;
}

bool cGZWinScrollbar2::DoMessage(const uint32_t *message)
{
    int32_t page = mPageSize - mPageOverlap;
    if (page < 1) page = 1;
    if (message[0] != kRepeatMessage)
        return OriginalFunction<bool(__thiscall *)(void *, const uint32_t *), 0x930c91>()(this, message); // cGZWin's
    // Repeat the pressed part while the pointer is still on it (the part is cleared during the call, so the
    // redraw it causes shows it released).
    int32_t value;
    switch (mPressedPart) {
    case kBackArrow: value = mValue - mLineStep; break;
    case kPageBack: value = mValue - page; break;
    case kPageForward: value = mValue + page; break;
    case kForwardArrow: value = mLineStep + mValue; break;
    default: return true;
    }
    const uint8_t part = mPressedPart;
    if (mHotPart != part) return true;
    mHotPart = kNone;
    VirtualCall<Fn2, kBarSetValue>(Bar(), static_cast<uint32_t>(value), 1);
    mHotPart = part;
    return true;
}

bool cGZWinScrollbar2::Activate()
{
    cGZWin *win = AsGZWin(this);
    if (!win->TestFlag4000()) {
        VirtualCall<Fn2, kTimerSetCallback>(mTimer, U(reinterpret_cast<void *>(&RepeatTick)), U(this));
        MeasureParts();
    }
    return win->Activate();
}

bool cGZWinScrollbar2::Teardown()
{
    VirtualCall<Fn0, kTimerStop>(mTimer);
    VirtualCall<Fn2, kTimerSetCallback>(mTimer, 0, U(this));
    for (RZUnknownSlot &image : mImage) image.Assign(nullptr);
    return AsGZWin(this)->Teardown();
}

uint32_t cGZWinScrollbar2::SetArea(int32_t left, int32_t top, int32_t right, int32_t bottom)
{
    if (mDirty & 1) MeasureParts();
    // A bar keeps its thickness: the minimum across it, when there is one.
    if (!mVertical) {
        if (mMinHeight != 0) bottom = mMinHeight + top;
    } else if (mMinWidth != 0) {
        right = mMinWidth + left;
    }
    const uint32_t result = OriginalFunction<uint32_t(__thiscall *)(void *, int32_t, int32_t, int32_t, int32_t), 0x932914>()(
        this, left, top, right, bottom); // cGZWin's
    mDirty |= 2;
    return result;
}

bool cGZWinScrollbar2::OnMouseDown(int32_t x, int32_t y, uint32_t)
{
    const uint8_t part = HitTest(x, y);
    int32_t page = mPageSize - mPageOverlap;
    if (page < 1) page = 1;
    StopRepeat(mTimer);
    if (!SetCapture(this)) return false;
    int32_t value;
    switch (part) {
    case kBackArrow: value = mValue - mLineStep; break;
    case kPageBack: value = mValue - page; break;
    case kPageForward: value = mValue + page; break;
    case kForwardArrow: value = mLineStep + mValue; break;
    case kThumb:
        mDragOffset = (mVertical ? y : x) - mThumbPos;
        [[fallthrough]];
    default:
        mPressedPart = part;
        VirtualCall<Fn0, kWinInvalidate>(this);
        return false;
    }
    VirtualCall<Fn2, kBarSetValue>(Bar(), static_cast<uint32_t>(value), 1);
    const uint32_t window = VirtualCall<Fn0, kBarWindow>(Bar());
    VirtualCall<Fn2, kTimerStart>(mTimer, kFirstRepeat, window);
    mPressedPart = part;
    VirtualCall<Fn0, kWinInvalidate>(this);
    return false;
}

bool cGZWinScrollbar2::OnMouseUp(int32_t x, int32_t y, uint32_t)
{
    if (HasCapture(this)) {
        ReleaseCapture(this);
        mPressedPart = kNone;
        mHotPart = HitTest(x, y);
        VirtualCall<Fn0, kWinInvalidate>(this);
        const int32_t value = mValue;
        const uint32_t id = VirtualCall<Fn0, kWinGetID>(this);
        VirtualCall<Fn5, kWinNotify>(this, At<uint32_t>(this, 0x5c), kNotifyRelease, U(this), id,
                                     static_cast<uint32_t>(value));
    }
    StopRepeat(mTimer);
    return false;
}

bool cGZWinScrollbar2::OnMouseMove(int32_t x, int32_t y, uint32_t)
{
    const bool dragging = HasCapture(this) && mPressedPart != kBackArrow && mPressedPart != kPageBack &&
                          mPressedPart != kPageForward && mPressedPart != kForwardArrow;
    if (!dragging) {
        const uint8_t part = HitTest(x, y);
        if (mHotPart != part) {
            mHotPart = part;
            VirtualCall<Fn0, kWinInvalidate>(this);
        }
        return false;
    }
    if (mPressedPart != kThumb || mThumbLength <= 0) return false;
    // The thumb follows the pointer: its position along the free track, scaled to the range less a page.
    int32_t range = mMax - mMin;
    if (range < 0) range = 0;
    int32_t span = range - mPageSize;
    if (span < 0) span = 0;
    int32_t pos;
    int32_t track;
    if (!mVertical) {
        pos = x - mDragOffset;
        track = Width();
    } else {
        pos = y - mDragOffset;
        track = Height();
    }
    track = track - 2 * mArrowLength - mThumbLength;
    const int32_t value = track < 1 ? 0 : Mul(pos, span) / track;
    VirtualCall<Fn2, kBarSetValue>(Bar(), static_cast<uint32_t>(value), 1);
    return false;
}

bool cGZWinScrollbar2::OnMouseExit(uint32_t)
{
    if (!HasCapture(this) && mHotPart != kNone) {
        mHotPart = kNone;
        VirtualCall<Fn0, kWinInvalidate>(this);
    }
    StopRepeat(mTimer);
    return false;
}

namespace {

// cIGZDrawContext slots.
enum DrawSlot : int {
    kDrawSetColor = 21,
    kDrawFillRect = 36,
    kDrawImage = 39,          // (image, source rect, target rect)
    kDrawImageStretched = 40, // (image, source rect, target rect, 0, 0)
    kDrawPrimitives = 45,     // (6: a triangle, float points, 3)
};

constexpr uint32_t kFaceDisabled = 0xff808080;
constexpr uint32_t kFace = 0xffc8c8ff;
constexpr uint32_t kFaceHot = 0xffdcdcff;
constexpr uint32_t kFacePressed = 0xffb4b4ff;
constexpr uint32_t kArrowColor = 0xff646482;
constexpr uint32_t kTrackColor = 0xff9696be;
constexpr uint32_t kThumbDisabled = 0xffbebebe;

using Rect = int32_t[4];

// Rects given along and across the bar.
struct Axis {
    bool vertical;
    void Set(Rect &r, int32_t along0, int32_t across0, int32_t along1, int32_t across1) const
    {
        if (vertical) {
            r[0] = across0;
            r[1] = along0;
            r[2] = across1;
            r[3] = along1;
        } else {
            r[0] = along0;
            r[1] = across0;
            r[2] = along1;
            r[3] = across1;
        }
    }
};

// A triangle in the middle of `r` pointing left, right, up or down (the points as the original orders them).
void Triangle(void *dc, const Rect &r, int direction)
{
    const int32_t w = r[2] - r[0];
    const int32_t h = r[3] - r[1];
    float p[6];
    const auto F = [](int32_t v) { return static_cast<float>(v); };
    switch (direction) {
    case 0: // left
        p[0] = F(r[0] + w / 4), p[1] = F(r[1] + h / 2);
        p[2] = F(r[2] - w / 4), p[3] = F(r[3] - h / 4);
        p[4] = F(r[2] - w / 4), p[5] = F(r[1] + h / 4);
        break;
    case 1: // right
        p[0] = F(r[2] - w / 4), p[1] = F(r[1] + h / 2);
        p[2] = F(r[0] + w / 4), p[3] = F(r[1] + h / 4);
        p[4] = F(r[0] + w / 4), p[5] = F(r[3] - h / 4);
        break;
    case 2: // up
        p[0] = F(r[0] + w / 2), p[1] = F(r[1] + h / 4);
        p[2] = F(r[0] + w / 4), p[3] = F(r[3] - h / 4);
        p[4] = F(r[2] - w / 4), p[5] = F(r[3] - h / 4);
        break;
    default: // down
        p[0] = F(r[0] + w / 2), p[1] = F(r[3] - h / 4);
        p[2] = F(r[2] - w / 4), p[3] = F(r[1] + h / 4);
        p[4] = F(r[0] + w / 4), p[5] = F(r[1] + h / 4);
        break;
    }
    VirtualCall<Fn3, kDrawPrimitives>(dc, 6, U(p), 3);
}

void Blit(void *dc, void *image, const Rect &source, const Rect &target)
{
    VirtualCall<Fn3, kDrawImage>(dc, U(image), U(source), U(target));
}

void Stretch(void *dc, void *image, const Rect &source, const Rect &target)
{
    VirtualCall<Fn5, kDrawImageStretched>(dc, U(image), U(source), U(target), 0, 0);
}

} // namespace

bool cGZWinScrollbar2::Draw()
{
    if (mDirty & 2) PlaceThumb();
    void *dc = At<void *>(this, 0x88);
    const bool enabled = (At<uint32_t>(this, 0xe8) & 2) != 0;
    const Axis axis{mVertical != 0};
    const int32_t length = mVertical ? Height() : Width(); // along the bar
    const int32_t thickness = mVertical ? Width() : Height();
    const int32_t a = mArrowLength;

    if (!mImage[0].mObject && !mImage[1].mObject && !mImage[2].mObject) {
        // Drawn: bevelled arrows with triangles, a flat track and a bevelled thumb.
        Rect back, forward, thumb, track;
        axis.Set(back, 0, 0, a, thickness);
        axis.Set(forward, length - a, 0, length, thickness);
        axis.Set(thumb, a + mThumbPos, 0, mThumbLength + a + mThumbPos, thickness);
        axis.Set(track, a, 0, length - a, thickness);
        if (!enabled) {
            GZWinDrawBevel(dc, forward, kFaceDisabled, 1);
            GZWinDrawBevel(dc, back, kFaceDisabled, 1);
        } else if (mHotPart == kBackArrow) {
            GZWinDrawBevel(dc, forward, kFace, 1);
            const bool pressed = mPressedPart == kBackArrow;
            GZWinDrawBevel(dc, back, pressed ? kFacePressed : kFaceHot, !pressed);
        } else if (mHotPart == kForwardArrow) {
            GZWinDrawBevel(dc, back, kFace, 1);
            const bool pressed = mPressedPart == kForwardArrow;
            GZWinDrawBevel(dc, forward, pressed ? kFacePressed : kFaceHot, !pressed);
        } else {
            GZWinDrawBevel(dc, forward, kFace, 1);
            GZWinDrawBevel(dc, back, kFace, 1);
        }
        VirtualCall<Fn1, kDrawSetColor>(dc, kArrowColor);
        if (!mVertical) {
            Triangle(dc, forward, 1);
            Triangle(dc, back, 0);
        } else {
            Triangle(dc, back, 2);
            Triangle(dc, forward, 3);
        }
        VirtualCall<Fn1, kDrawSetColor>(dc, kTrackColor);
        VirtualCall<Fn1, kDrawFillRect>(dc, U(track));
        if (!enabled) {
            VirtualCall<Fn1, kDrawSetColor>(dc, kThumbDisabled);
            VirtualCall<Fn1, kDrawFillRect>(dc, U(thumb));
        } else if (mPressedPart == kThumb) {
            GZWinDrawBevel(dc, thumb, kFacePressed, 0);
        } else {
            GZWinDrawBevel(dc, thumb, mHotPart == kThumb ? kFaceHot : kFace, 1);
        }
        return true;
    }

    // From the images. Each has four states side by side across the bar: disabled, normal, under the pointer,
    // pressed. The arrows image holds the back arrow then the forward arrow along it; the track and the thumb are
    // drawn as two end caps and a stretched middle.
    const auto Along = [&](void *image) {
        return static_cast<int32_t>(mVertical ? VirtualCall<Fn0, kImageHeight>(image) : VirtualCall<Fn0, kImageWidth>(image));
    };
    const auto Across = [&](void *image) {
        return static_cast<int32_t>(mVertical ? VirtualCall<Fn0, kImageWidth>(image) : VirtualCall<Fn0, kImageHeight>(image));
    };
    Rect source, target;
    if (void *arrows = mImage[0].mObject) {
        const int32_t q = static_cast<int32_t>(static_cast<uint32_t>(Across(arrows)) >> 2);
        int32_t backState = 0;
        int32_t forwardState = 0;
        if (enabled) {
            backState = q;
            forwardState = q;
            if (mHotPart == kBackArrow) backState = mPressedPart == kBackArrow ? q * 3 : q * 2;
            else if (mHotPart == kForwardArrow) forwardState = mPressedPart == kForwardArrow ? q * 3 : q * 2;
        }
        axis.Set(source, 0, backState, a, backState + q);
        axis.Set(target, 0, 0, a, q);
        Blit(dc, arrows, source, target);
        axis.Set(source, a, forwardState, a + a, forwardState + q);
        axis.Set(target, length - a, 0, length, q);
        Blit(dc, arrows, source, target);
    }
    if (void *track = mImage[1].mObject) {
        // Width then height, whichever is along the bar.
        const int32_t width = static_cast<int32_t>(VirtualCall<Fn0, kImageWidth>(track));
        const int32_t height = static_cast<int32_t>(VirtualCall<Fn0, kImageHeight>(track));
        const int32_t along = mVertical ? height : width;
        const int32_t across = mVertical ? width : height;
        const int32_t end = mTrackEnd;
        axis.Set(source, 0, 0, end, across);
        axis.Set(target, a, 0, a + end, across);
        Blit(dc, track, source, target);
        axis.Set(source, along - end, 0, along, across);
        axis.Set(target, length - a - end, 0, length - a, across);
        Blit(dc, track, source, target);
        axis.Set(source, end, 0, along - end, across);
        axis.Set(target, a + end, 0, length - a - end, across);
        Stretch(dc, track, source, target);
    }
    if (void *thumbImage = mImage[2].mObject) {
        const uint32_t width = VirtualCall<Fn0, kImageWidth>(thumbImage);
        const uint32_t height = VirtualCall<Fn0, kImageHeight>(thumbImage);
        const int32_t along = static_cast<int32_t>(mVertical ? height : width);
        const int32_t q = static_cast<int32_t>((mVertical ? width : height) >> 2);
        int32_t state = 0;
        if (enabled) state = mPressedPart == kThumb ? q * 3 : mHotPart == kThumb ? q * 2 : q;
        const int32_t end = mThumbEnd;
        const int32_t start = a + mThumbPos;
        const int32_t stop = a + mThumbPos + mThumbLength;
        axis.Set(source, 0, state, end, state + q);
        axis.Set(target, start, 0, start + end, q);
        Blit(dc, thumbImage, source, target);
        axis.Set(source, along - end, state, along, state + q);
        axis.Set(target, stop - end, 0, stop, q);
        Blit(dc, thumbImage, source, target);
        axis.Set(source, end, state, along - end, state + q);
        axis.Set(target, start + end, 0, stop - end, q);
        Stretch(dc, thumbImage, source, target);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------
// Non-virtual members

void cGZWinScrollbar2::MeasureParts()
{
    mDirty &= ~1;
    mMinWidth = 0;
    mMinHeight = 0;
    if (!mImage[0].mObject && !mImage[1].mObject && !mImage[2].mObject) {
        // Drawn: 18-pixel arrows, an 8-pixel minimum thumb.
        mArrowLength = 0x12;
        mThumbEnd = 4;
        mMinWidth = mVertical ? 0x12 : 0x36;
        mMinHeight = mVertical ? 0x36 : 0x12;
        return;
    }
    // From the images: `along` is the image's size along the bar, `across` across it. The minimum across the bar
    // is the largest image's.
    const int along = mVertical ? kImageHeight : kImageWidth;
    const int across = mVertical ? kImageWidth : kImageHeight;
    int32_t &minAcross = mVertical ? mMinWidth : mMinHeight;
    const auto Size = [](void *image, int slot) {
        return slot == kImageWidth ? VirtualCall<Fn0, kImageWidth>(image) : VirtualCall<Fn0, kImageHeight>(image);
    };
    if (mImage[0].mObject) {
        const int32_t a = static_cast<int32_t>(Size(mImage[0].mObject, across) >> 2); // four arrow states
        if (minAcross < a) minAcross = a;
        mArrowLength = static_cast<int32_t>(Size(mImage[0].mObject, along) >> 1);  // two arrows
    } else {
        mArrowLength = 0;
    }
    if (mImage[1].mObject) {
        const int32_t a = static_cast<int32_t>(Size(mImage[1].mObject, across));
        if (minAcross < a) minAcross = a;
        mTrackMiddle = static_cast<int32_t>(Size(mImage[1].mObject, along) >> 1);
        mTrackEnd = static_cast<int32_t>(Size(mImage[1].mObject, along) >> 2);
    } else {
        mTrackMiddle = 0;
        mTrackEnd = 0;
    }
    if (mImage[2].mObject) {
        const int32_t a = static_cast<int32_t>(Size(mImage[2].mObject, across) >> 2);
        if (minAcross < a) minAcross = a;
        mThumbMiddle = static_cast<int32_t>(Size(mImage[2].mObject, along) >> 1);
        mThumbEnd = static_cast<int32_t>(Size(mImage[2].mObject, along) >> 2);
    } else {
        mThumbMiddle = 0;
        mThumbEnd = 0;
    }
    const int32_t end = Mul(mTrackEnd, 2) < Mul(mThumbEnd, 2) ? mThumbEnd : mTrackEnd;
    (mVertical ? mMinHeight : mMinWidth) = Mul(end + mArrowLength, 2);
}

void cGZWinScrollbar2::PlaceThumb()
{
    if (mDirty & 1) MeasureParts();
    mDirty &= ~2;
    const int32_t track = (mVertical ? Height() : Width()) - Mul(mArrowLength, 2);
    int32_t range = mMax - mMin;
    if (range < 0) range = 0;
    const int32_t minThumb = Mul(mThumbEnd, 2);
    int32_t pos = 0;
    if (minThumb + 2 >= track) {
        mThumbLength = 0;
    } else if (mPageSize < range && range > 0) {
        int32_t length = Mul(mPageSize, track) / range;
        mThumbLength = length;
        if (length < minThumb) {
            mThumbLength = minThumb;
            length = minThumb;
        }
        const int32_t span = range - mPageSize;
        pos = (span / 2 + Mul(mValue - mMin, track - length)) / span; // rounded to the nearest
    } else {
        mThumbLength = track;
    }
    mThumbPos = pos;
}

uint8_t cGZWinScrollbar2::HitTest(int32_t x, int32_t y)
{
    if (mDirty & 2) PlaceThumb();
    if (x < 0 || x >= Width() || y < 0 || y >= Height()) return kNone;
    const int32_t at = mVertical ? y : x;
    const int32_t length = mVertical ? Height() : Width();
    if (at < mArrowLength) return kBackArrow;
    if (at < mThumbPos + mArrowLength) return kPageBack;
    if (at < mThumbLength + mThumbPos + mArrowLength) return kThumb;
    return at < length - mArrowLength ? kPageForward : kForwardArrow;
}

void __cdecl cGZWinScrollbar2::RepeatTick(cGZWinScrollbar2 *self)
{
    VirtualCall<Fn1, kTimerSetInterval>(self->mTimer, kRepeat);
    VirtualCall<Fn5, kWinPost>(self, U(self), kRepeatMessage, 0, 0, 0);
}

// ---------------------------------------------------------------------------------------------------------
// The cIGZWinScrollbar2 face

// ORIGINAL-BUG: 1 (vertical) is refused, so a bar made horizontal can't be made vertical again; any other value is
// stored as the orientation byte (0 horizontal, anything else vertical).
bool cGZWinScrollbar2Face::SetOrientation(uint32_t vertical)
{
    cGZWinScrollbar2 *bar = Obj();
    if (vertical != 0 && vertical == 1) return true;
    if (vertical == bar->mVertical) return true;
    bar->mDirty |= 3;
    bar->mVertical = static_cast<uint8_t>(vertical);
    return true;
}

bool cGZWinScrollbar2Face::GetOrientation(uint32_t *out)
{
    *out = Obj()->mVertical;
    return true;
}

bool cGZWinScrollbar2Face::SetValue(int32_t value, uint32_t notify)
{
    cGZWinScrollbar2 *bar = Obj();
    const int32_t last = bar->mMax - bar->mPageSize;
    if (value > last) value = last;
    if (value < bar->mMin) value = bar->mMin;
    if (value == bar->mValue) return true;
    bar->mDirty |= 2;
    VirtualCall<Fn0, kWinInvalidate>(bar);
    bar->mValue = value;
    if (static_cast<uint8_t>(notify) && AsGZWin(bar)->TestFlag4000()) {
        const uint32_t id = VirtualCall<Fn0, kWinGetID>(bar);
        VirtualCall<Fn5, kWinNotify>(bar, At<uint32_t>(bar, 0x5c), cGZWinScrollbar2::kNotifyValue, U(bar), id,
                                     static_cast<uint32_t>(value));
    }
    return true;
}

bool cGZWinScrollbar2Face::GetValue(int32_t *out)
{
    *out = Obj()->mValue;
    return true;
}

// SetMin, SetMax and SetPageSize clamp the value again (SetValue through the face's vtable) and redraw.
bool cGZWinScrollbar2Face::SetMin(int32_t value, uint32_t notify)
{
    cGZWinScrollbar2 *bar = Obj();
    if (bar->mMin == value) return true;
    bar->mMin = value;
    VirtualCall<Fn2, kBarSetValue>(this, static_cast<uint32_t>(bar->mValue), notify);
    bar->mDirty |= 2;
    VirtualCall<Fn0, kWinInvalidate>(bar);
    return true;
}

bool cGZWinScrollbar2Face::SetMax(int32_t value, uint32_t notify)
{
    cGZWinScrollbar2 *bar = Obj();
    if (bar->mMax == value) return true;
    bar->mMax = value;
    VirtualCall<Fn2, kBarSetValue>(this, static_cast<uint32_t>(bar->mValue), notify);
    bar->mDirty |= 2;
    VirtualCall<Fn0, kWinInvalidate>(bar);
    return true;
}

bool cGZWinScrollbar2Face::GetMax(int32_t *out)
{
    *out = Obj()->mMax;
    return true;
}

bool cGZWinScrollbar2Face::SetPageSize(int32_t value, uint32_t notify)
{
    cGZWinScrollbar2 *bar = Obj();
    if (value < 0) value = 0;
    if (bar->mPageSize == value) return true;
    bar->mPageSize = value;
    VirtualCall<Fn2, kBarSetValue>(this, static_cast<uint32_t>(bar->mValue), notify);
    bar->mDirty |= 2;
    VirtualCall<Fn0, kWinInvalidate>(bar);
    return true;
}

bool cGZWinScrollbar2Face::SetPageOverlap(int32_t value)
{
    Obj()->mPageOverlap = value > 0 ? value : 0;
    return true;
}

bool cGZWinScrollbar2Face::SetLineStep(int32_t value)
{
    Obj()->mLineStep = value > 0 ? value : 0;
    return true;
}

namespace {
bool SetImage(cGZWinScrollbar2 *bar, int index, void *image)
{
    bar->mDirty |= 3;
    bar->mImage[index].Assign(image);
    VirtualCall<Fn0, kWinInvalidate>(bar);
    return true;
}
} // namespace

bool cGZWinScrollbar2Face::SetArrowImage(void *image) { return SetImage(Obj(), 0, image); }
bool cGZWinScrollbar2Face::SetTrackImage(void *image) { return SetImage(Obj(), 1, image); }
bool cGZWinScrollbar2Face::SetThumbImage(void *image) { return SetImage(Obj(), 2, image); }

bool cGZWinScrollbar2Face::GetMinSize(int32_t *out)
{
    cGZWinScrollbar2 *bar = Obj();
    if (bar->mDirty & 1) bar->MeasureParts();
    out[0] = bar->mMinWidth;
    out[1] = bar->mMinHeight;
    return true;
}
