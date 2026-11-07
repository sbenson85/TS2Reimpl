#pragma once
// nGZEvent::cInputDeviceMap (lane LW, foundations F1): the event system's input map, class 0xbc168268 of the event COM
// director (factory 0x1a8d95, registered by the director's constructor 0x1a8346). It turns raw input records into events
// for a behaviour: buttons through a map from (device, control, button) to a stored event, mouse motion through a map from
// (mode, phase, modifiers) to an event type that gets the position as a cVectorEvent. RVAs are 1.18.0.187; the code is
// the speed profile (/O2).
//
// Standalone shape (docs/standalone-shape.md): one face, cIInputDeviceMap (vtable 0xda223c, 11 slots). The factory is
// hooked and the constructor 0x1a8af6 is reached only through it (its jmp), the vtable is stored by nothing else but the
// destructor 0x1a8b75, so every map carries ours and the slots are ours in effect through the factory.
#include "gz/cGZEventReflector.h" // cIAbstractBehavior
#include "gz/cIGZUnknown.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace nGZEvent {

// An event body as the input map stores it (RTTI: nGZEvent::cIEventHeader, slots 0-6; the name is taken by a hook-shaped
// struct in misc/DeletingDestructors.h). Slot 0 is the destructor (called with 0: the map owns the memory).
class cIEventBody {
public:
    virtual ~cIEventBody() {}                      // 0
    virtual cIEventBody *Assign(const cIEventBody *other) = 0; // 1
    virtual cIEventBody *Slot2(const cIEventBody *other) = 0;  // 2
    virtual uint32_t GetEventType() const = 0;     // 3
    virtual uint32_t GetSize() const = 0;          // 4: the bytes a copy takes
    virtual uint32_t GetBodySize() const = 0;      // 5
    virtual cIEventBody *CloneInto(void *memory) const = 0; // 6: a copy built in `memory` (GetSize bytes)
};

// A button's key (16 bytes, by value on the stack): ordered by the code alone.
struct cButtonKey {
    uint32_t mDevice;
    uint32_t mControl;
    uint32_t mButton; // 1 and 2 (press kinds 5: state 1 adds 1), 4 (kind 6)
    uint32_t mCode;   // mButton + (mDevice * 9 + mControl) * 8
};
static_assert(sizeof(cButtonKey) == 0x10, "cButtonKey size");

// A mouse motion's key (20 bytes): ordered by the code alone. Mode 8 and modifiers 8 stand for "any".
struct cMotionKey {
    uint32_t mMode;      // 0..7 (HandleMouse's modes), 8 any
    uint32_t mPhase;     // 1 begin, 2 move, 3 end
    uint32_t mModifiers; // the input flags' bits 16-18, 8 any
    float mScale;        // the position's factor (1 from MapMotion)
    uint32_t mCode;      // mMode + (mPhase + mModifiers * 4) * 9
};
static_assert(sizeof(cMotionKey) == 0x14, "cMotionKey size");

// The record slot 3 translates (16 bytes).
struct cInputRecord {
    uint32_t mKind; // 4..6 buttons (slot 8), 7..14 mouse (slot 9)
    uint32_t mA;
    uint32_t mB;
    uint32_t mC;
};

// cVectorEvent<cVector<2, long>, ...> (game vtable 0xda226c, 12 bytes): the position a motion event carries. Built on
// the stack for the behaviour's OnQuery; its class is the game's.
struct cVectorEvent {
    static constexpr uint32_t kVTable = 0xda226c;
    const void *mVTable;
    int32_t mX;
    int32_t mY;
};
static_assert(sizeof(cVectorEvent) == 0xc, "cVectorEvent size");

// The mapped value of a button (16 bytes): an event type and a copy of an event body kept in a byte buffer.
class cStoredEvent {
public:
    explicit cStoredEvent(uint32_t type) : mType(type) {} // 0x1a8ae0
    cStoredEvent(const cStoredEvent &other);               // 0x1a8a9d: the body cloned into a buffer of its size
    ~cStoredEvent();                                       // 0x1a8b5a: the body destroyed (slot 0), the buffer freed
    cStoredEvent &operator=(const cStoredEvent &) = delete;
    // 0x1a8d03: a copy of `event` built in the buffer, resized to its size (an earlier copy is not destroyed).
    void Assign(const cIEventBody *event);
    const cIEventBody *Body() const { return reinterpret_cast<const cIEventBody *>(mBody.data()); }

    uint32_t mType;          // +0
    std::vector<char> mBody; // +4 the copy's bytes
};
static_assert(sizeof(cStoredEvent) == 0x10, "cStoredEvent size");

struct cButtonKeyLess {
    bool operator()(const cButtonKey &a, const cButtonKey &b) const { return a.mCode < b.mCode; }
};
struct cMotionKeyLess {
    bool operator()(const cMotionKey &a, const cMotionKey &b) const { return a.mCode < b.mCode; }
};

class cIInputDeviceMap : public cIGZUnknown {
public:
    static constexpr uint32_t kIid = 0x9c168262;
    // 3: kinds 4..6 to HandleButton, 7..14 to HandleMouse (the record's three words as their arguments), else false.
    virtual bool Translate(const cInputRecord *record, cIAbstractBehavior *target) = 0;
    // 4: MapButtonKey with the code made from the three.
    virtual void MapButton(uint32_t device, uint32_t control, uint32_t button, uint32_t type, const cIEventBody *event) = 0;
    // 5: the key mapped to (type, a copy of event) unless its code is mapped already.
    virtual void MapButtonKey(cButtonKey key, uint32_t type, const cIEventBody *event) = 0;
    // 6: MapMotionKey with scale 1 and the code made from the three.
    virtual void MapMotion(uint32_t mode, uint32_t phase, uint32_t modifiers, uint32_t type) = 0;
    // 7: the key mapped to the type unless its code is mapped already.
    virtual void MapMotionKey(cMotionKey key, uint32_t type) = 0;
    // 8: kind 5 (press): button 1 (when state is 1) and button 2 sent, true when either was handled; kind 6: button 4.
    virtual bool HandleButton(cIAbstractBehavior *target, uint32_t kind, uint32_t device, uint32_t control, uint32_t state) = 0;
    // 9: the mouse: kinds 7-9 a button down (left, middle, right), 10-12 all up, 14 the wheel (flags' low word).
    virtual bool HandleMouse(cIAbstractBehavior *target, uint32_t kind, int32_t x, int32_t y, uint32_t flags) = 0;
    virtual ~cIInputDeviceMap() {} // 10: the deleting destructor 0x1a8bb2
};

class cInputDeviceMap : public cIInputDeviceMap {
public:
    static constexpr uint32_t kVTable = 0xda223c;
    static constexpr uint32_t kClassId = 0xbc168268;
    static constexpr uint32_t kGameSize = 0x30;
    static constexpr uint32_t kNoMode = 3; // the mode with no button and no flag (also the constructor's)
    static constexpr uint32_t kWheelMode = 4;
    static constexpr uint32_t kAny = 8;    // a motion key's mode or modifiers matching any

    cInputDeviceMap() = default;  // 0x1a8af6
    ~cInputDeviceMap() override;  // 0x1a8b75
    cInputDeviceMap(const cInputDeviceMap &) = delete;
    cInputDeviceMap &operator=(const cInputDeviceMap &) = delete;

    // 0x1a8d95 (cdecl, the event COM director's factory): a new map (null when new failed), not counted.
    static cIInputDeviceMap *__cdecl Create();

    bool QueryInterface(uint32_t riid, void **out) override; // 0x1a9204: kIid only
    uint32_t AddRef() override;                              // folded 0x18fd70
    uint32_t Release() override;                             // 0x1a9226

    bool Translate(const cInputRecord *record, cIAbstractBehavior *target) override;                // 0x1a8d4e
    void MapButton(uint32_t device, uint32_t control, uint32_t button, uint32_t type,
                   const cIEventBody *event) override;                                              // 0x1a8bd1
    void MapButtonKey(cButtonKey key, uint32_t type, const cIEventBody *event) override;            // 0x1a8c13
    void MapMotion(uint32_t mode, uint32_t phase, uint32_t modifiers, uint32_t type) override;      // 0x1a8c9f
    void MapMotionKey(cMotionKey key, uint32_t type) override;                                      // 0x1a8c6e
    bool HandleButton(cIAbstractBehavior *target, uint32_t kind, uint32_t device, uint32_t control,
                      uint32_t state) override;                                                     // 0x1a8db1
    bool HandleMouse(cIAbstractBehavior *target, uint32_t kind, int32_t x, int32_t y, uint32_t flags) override; // 0x1a8ec0

    // 0x1a923d: the button's stored event to the target's OnQuery (type, 0, the copy); false when the code is not mapped.
    bool SendButton(cIAbstractBehavior *target, cButtonKey key);
    // 0x1a926d: SendMotion for the key, then for (any mode, any modifiers), (the mode, any modifiers) and (any mode, the
    // modifiers) of its phase; the first one's answer.
    bool SendMotionAll(cIAbstractBehavior *target, cMotionKey key, int32_t x, int32_t y);
    // 0x1a935a: the position times the mapped key's scale (truncated) to the target's OnQuery (type, 0, a cVectorEvent);
    // false when the code is not mapped.
    bool SendMotion(cIAbstractBehavior *target, cMotionKey key, int32_t x, int32_t y);

    std::map<cButtonKey, cStoredEvent, cButtonKeyLess> mButtons; // +0x04 (0x30-byte nodes)
    std::map<cMotionKey, uint32_t, cMotionKeyLess> mMotions;     // +0x0c (0x28-byte nodes)
    uint32_t mMode = kNoMode; // +0x14 the last mode (0-2 one flag, 3 none, 4 wheel, 5-7 two buttons or flags)
    uint32_t mModifiers = 0;  // +0x18 the last flags' bits 16-18
    int32_t mX = -1;          // +0x1c the last position
    int32_t mY = -1;          // +0x20
    int32_t mWheel = 0;       // +0x24 the wheel position (summed while the mode stays the wheel's)
    bool mDown[3] = {};       // +0x28 left, middle, right held (kinds 7-9 set, 10-12 clear all)
    uint32_t mRefCount = 0;   // +0x2c
};
static_assert(sizeof(cInputDeviceMap) == cInputDeviceMap::kGameSize, "cInputDeviceMap size");
static_assert(offsetof(cInputDeviceMap, mButtons) == 4, "cInputDeviceMap layout");
static_assert(offsetof(cInputDeviceMap, mMotions) == 0xc, "cInputDeviceMap layout");
static_assert(offsetof(cInputDeviceMap, mMode) == 0x14, "cInputDeviceMap layout");
static_assert(offsetof(cInputDeviceMap, mWheel) == 0x24, "cInputDeviceMap layout");
static_assert(offsetof(cInputDeviceMap, mDown) == 0x28, "cInputDeviceMap layout");
static_assert(offsetof(cInputDeviceMap, mRefCount) == 0x2c, "cInputDeviceMap layout");

// The hooks' entry points.
struct cInputDeviceMapAdapters {
    static cInputDeviceMap *__fastcall Construct(void *memory, void *edx); // 0x1a8af6 (tests: our side's constructor)
};

} // namespace nGZEvent
