// nGZEvent::cInputDeviceMap (lane LW): see the header.
#include "gz/cInputDeviceMap.h"

#include "game/game.h"

#include <new>
#include <tuple>
#include <utility>

namespace nGZEvent {

namespace {

// The motion key of (mode, phase, modifiers) with scale 1, as MapMotion and HandleMouse make it.
cMotionKey MotionKey(uint32_t mode, uint32_t phase, uint32_t modifiers)
{
    return cMotionKey{mode, phase, modifiers, 1.0f, mode + (phase + modifiers * 4) * 9};
}

// The phases of a motion.
constexpr uint32_t kBegin = 1;
constexpr uint32_t kMove = 2;
constexpr uint32_t kEnd = 3;

} // namespace

// ---- cStoredEvent ----

// 0x1a8a9d: the type, then (when the other holds a body) a zeroed buffer of its size with the other's body cloned in.
cStoredEvent::cStoredEvent(const cStoredEvent &other) : mType(other.mType)
{
    if (other.mBody.size() != 0) {
        mBody.resize(other.mBody.size());
        other.Body()->CloneInto(mBody.data());
    }
}

// 0x1a8b5a.
cStoredEvent::~cStoredEvent()
{
    if (mBody.size() != 0) const_cast<cIEventBody *>(Body())->~cIEventBody();
}

// 0x1a8d03.
void cStoredEvent::Assign(const cIEventBody *event)
{
    mBody.resize(event->GetSize());
    event->CloneInto(mBody.data());
}

// ---- cInputDeviceMap ----

// 0x1a8b75: the motions, then the buttons (members in reverse order).
cInputDeviceMap::~cInputDeviceMap() = default;

cIInputDeviceMap *__cdecl cInputDeviceMap::Create() { return new cInputDeviceMap; }

cInputDeviceMap *__fastcall cInputDeviceMapAdapters::Construct(void *memory, void *)
{
    return ::new (memory) cInputDeviceMap;
}

// 0x1a9204.
bool cInputDeviceMap::QueryInterface(uint32_t riid, void **out)
{
    if (!out || riid != kIid) return false;
    *out = static_cast<cIInputDeviceMap *>(this);
    AddRef();
    return true;
}

// 0x18fd70 (folded: the count at +0x2c plus one).
uint32_t cInputDeviceMap::AddRef() { return ++mRefCount; }

// 0x1a9226: the last reference deletes the map (slot 10).
uint32_t cInputDeviceMap::Release()
{
    if (mRefCount == 1) {
        delete this;
        return 0;
    }
    return --mRefCount;
}

// 0x1a8d4e.
bool cInputDeviceMap::Translate(const cInputRecord *record, cIAbstractBehavior *target)
{
    const uint32_t kind = record->mKind;
    if (kind - 4 < 3) return HandleButton(target, kind, record->mA, record->mB, record->mC);
    if (kind - 7 < 8)
        return HandleMouse(target, kind, static_cast<int32_t>(record->mA), static_cast<int32_t>(record->mB), record->mC);
    return false;
}

// 0x1a8bd1.
void cInputDeviceMap::MapButton(uint32_t device, uint32_t control, uint32_t button, uint32_t type, const cIEventBody *event)
{
    MapButtonKey(cButtonKey{device, control, button, button + (device * 9 + control) * 8}, type, event);
}

// 0x1a8c13: the stored event is built (the body cloned into it), then copied into the map's node (a second clone), which
// is dropped again when the code is mapped already (piecewise: MSVC 14.40's emplace(key, value) would look the key up
// before building the node, and so skip that clone).
void cInputDeviceMap::MapButtonKey(cButtonKey key, uint32_t type, const cIEventBody *event)
{
    cStoredEvent stored(type);
    stored.Assign(event);
    mButtons.emplace(std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple(stored));
}

// 0x1a8c9f.
void cInputDeviceMap::MapMotion(uint32_t mode, uint32_t phase, uint32_t modifiers, uint32_t type)
{
    MapMotionKey(MotionKey(mode, phase, modifiers), type);
}

// 0x1a8c6e.
void cInputDeviceMap::MapMotionKey(cMotionKey key, uint32_t type)
{
    mMotions.emplace(std::piecewise_construct, std::forward_as_tuple(key), std::forward_as_tuple(type));
}

// 0x1a8db1.
bool cInputDeviceMap::HandleButton(cIAbstractBehavior *target, uint32_t kind, uint32_t device, uint32_t control,
                                   uint32_t state)
{
    const uint32_t base = (device * 9 + control) * 8;
    if (kind == 5) {
        bool first = false;
        if (state == 1) first = SendButton(target, cButtonKey{device, control, 1, base + 1});
        const bool second = SendButton(target, cButtonKey{device, control, 2, base + 2});
        return second || first;
    }
    if (kind == 6) return SendButton(target, cButtonKey{device, control, 4, base + 4});
    return false;
}

// 0x1a923d.
bool cInputDeviceMap::SendButton(cIAbstractBehavior *target, cButtonKey key)
{
    const auto it = mButtons.find(key);
    if (it == mButtons.end()) return false;
    return target->OnQuery(it->second.mType, 0, const_cast<cIEventBody *>(it->second.Body()));
}

// 0x1a935a.
bool cInputDeviceMap::SendMotion(cIAbstractBehavior *target, cMotionKey key, int32_t x, int32_t y)
{
    const auto it = mMotions.find(key);
    if (it == mMotions.end()) return false;
    const float scale = it->first.mScale;
    cVectorEvent event{GameAddress<const void>(cVectorEvent::kVTable), static_cast<int32_t>(static_cast<float>(x) * scale),
                       static_cast<int32_t>(static_cast<float>(y) * scale)};
    return target->OnQuery(it->second, 0, &event);
}

// 0x1a926d.
bool cInputDeviceMap::SendMotionAll(cIAbstractBehavior *target, cMotionKey key, int32_t x, int32_t y)
{
    const bool handled = SendMotion(target, key, x, y);
    SendMotion(target, MotionKey(kAny, key.mPhase, kAny), x, y);
    SendMotion(target, MotionKey(key.mMode, key.mPhase, kAny), x, y);
    SendMotion(target, MotionKey(kAny, key.mPhase, key.mModifiers), x, y);
    return handled;
}

// 0x1a8ec0: the mode from the held buttons (kinds 7-9), the wheel (kind 14, unless the mode is 2) or the flags' bits
// 20-22; an unchanged mode and modifiers send a move when the position changed; a change ends the old mode (at the old
// position when the wheel is involved) and begins the new one (a wheel's begin at 0, 0, followed by a move when handled).
bool cInputDeviceMap::HandleMouse(cIAbstractBehavior *target, uint32_t kind, int32_t x, int32_t y, uint32_t flags)
{
    const uint32_t high = flags >> 16;
    const uint32_t modifiers = high & 7;
    bool &left = mDown[0];
    bool &middle = mDown[1];
    bool &right = mDown[2];
    if (kind == 7) {
        left = true;
    } else if (kind == 8) {
        middle = true;
    } else if (kind == 9) {
        right = true;
    } else if (kind == 10 || kind == 11 || kind == 12) {
        left = middle = right = false;
    }

    uint32_t mode = kNoMode;
    if (kind == 7) {
        if (right) mode = 5;
        else if (middle) mode = 7;
    } else if (kind == 9) {
        if (left) mode = 5;
        else if (middle) mode = 6;
    } else if (kind == 8) {
        if (right) mode = 6;
        else if (left) mode = 7;
    } else if (mMode != 2 && kind == 14) {
        mode = kWheelMode;
        int32_t wheel = static_cast<int16_t>(flags);
        if (mMode == kWheelMode) wheel += mWheel;
        y = wheel;
        mWheel = wheel;
        x = 0;
    } else {
        const uint32_t bits = high & 0xff;
        if ((bits & 0x50) == 0x50) mode = 5;
        else if ((bits & 0x60) == 0x60) mode = 6;
        else if ((bits & 0x30) == 0x30) mode = 7;
        else if (bits & 0x10) mode = 0;
        else if (bits & 0x20) mode = 1;
        else if (mMode == kWheelMode) mode = kNoMode;
        else if (bits & 0x40) mode = 2;
    }

    bool handled;
    if (mode == mMode && modifiers == mModifiers) {
        if (x == mX && y == mY) return false;
        handled = SendMotionAll(target, MotionKey(mode, kMove, modifiers), x, y);
    } else {
        const uint32_t oldMode = mMode;
        const bool wheelInvolved = mode == kWheelMode || oldMode == kWheelMode;
        const bool wheelBegins = mode == kWheelMode && oldMode != kWheelMode;
        SendMotionAll(target, MotionKey(oldMode, kEnd, mModifiers), wheelInvolved ? mX : x, wheelInvolved ? mY : y);
        if (!wheelBegins) {
            handled = SendMotionAll(target, MotionKey(mode, kBegin, modifiers), x, y);
        } else {
            handled = SendMotionAll(target, MotionKey(mode, kBegin, modifiers), 0, 0);
            if (handled) SendMotionAll(target, MotionKey(mode, kMove, modifiers), x, y);
        }
        mMode = mode;
        mModifiers = modifiers;
    }
    mX = x;
    mY = y;
    return handled;
}

} // namespace nGZEvent
