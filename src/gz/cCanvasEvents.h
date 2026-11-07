#pragma once
// The events the window service sends (lane OQ, rendering campaign R2b step 4b): nGZGraphic4::cCanvasEvent,
// cCanvasSizeMoveEvent, cMouseEvent and cKeyEvent, each an event body (RTTI: cEventBodyBase<T> on nGZEvent::cIEventHeader,
// 7 slots: the deleting destructor, the copy (slots 1 and 2, one body), the body's event type, its size and the size
// past the vtable, and a copy built in caller memory). Listeners get them by pointer through NotifyListeners and read
// the fields; game code that keeps one copies it through slots 4 and 6.
//
// Standalone shape: our window code builds these (our vtables). The game's vtables (0xd9caac, 0xd9cacc, 0xd9caec,
// 0xd9cb0c) are still stored by other classes' code that copies a received event onto its stack (cTSAudioSoundManager,
// cTSMainToolbarEventSink, cTSCameraControllerManager, nTSSG::cCameraEffectsSupport, cTSCameraSmoothFirstPersonController
// and each class's own slot 6), so the slots each class has to itself are hooked (adapters below); the folded ones
// (shared with other event classes: 0x154ce0, 0x155080, 0x1559c0, 0x155a00, 0x154eb0, 0x1550b0, 0x1559e0, 0x1561d0,
// 0x1559f0, 0x1561e0) stay game code on the game's objects.
#include "gz/cCanvasW32.h" // cCanvasRect
#include "gz/cInputDeviceMap.h" // nGZEvent::cIEventBody

#include <cstdint>

namespace nGZGraphic4 {

// The window's state changes (vtable 0xd9caac, 8 bytes), sent with event id 0x1b637474: the code says what happened
// (0 the window goes, 1 activated, 2 deactivated, 3 painted, 4 shown, 5 / 6 the application activated / deactivated,
// 7 the timer, 8 the display changed, 9 activated while minimized).
class cCanvasEvent : public nGZEvent::cIEventBody {
public:
    static constexpr uint32_t kType = 0x7b637514;
    static constexpr uint32_t kGameVTable = 0xd9caac;
    explicit cCanvasEvent(uint32_t code) : mCode(code) {}
    ~cCanvasEvent() override {}                                       // 0: shared 0x154ce0
    cIEventBody *Assign(const cIEventBody *other) override;           // 1, 2: shared 0x155080
    cIEventBody *Slot2(const cIEventBody *other) override;
    uint32_t GetEventType() const override;                           // 3: 0x1576d0
    uint32_t GetSize() const override;                                // 4: shared 0x1559c0
    uint32_t GetBodySize() const override;                            // 5: shared 0x155a00
    cIEventBody *CloneInto(void *memory) const override;              // 6: 0x155900

    uint32_t mCode; // +4
};
static_assert(sizeof(cCanvasEvent) == 8, "cCanvasEvent layout");

// The window moved or was sized (vtable 0xd9cacc, 0x18 bytes), sent with event id 0x3b6decd1: the client rectangle in
// screen coordinates and a code (1 moved, 2 sized, 6 maximized, 0xa minimized (an empty rectangle), bit 0x10 while the
// user drags; at the end of a drag 0x20 with bit 1 for "sized", bit 0 for "moved").
class cCanvasSizeMoveEvent : public nGZEvent::cIEventBody {
public:
    static constexpr uint32_t kType = 0xfb6dec65;
    static constexpr uint32_t kGameVTable = 0xd9cacc;
    cCanvasSizeMoveEvent(const cCanvasRect &rect, uint32_t code) : mRect(rect), mCode(code) {}
    ~cCanvasSizeMoveEvent() override {}                               // 0: 0x154d10
    cIEventBody *Assign(const cIEventBody *other) override;           // 1, 2: 0x155090
    cIEventBody *Slot2(const cIEventBody *other) override;
    uint32_t GetEventType() const override;                           // 3: 0x1576e0
    uint32_t GetSize() const override;                                // 4: 0x1559d0
    uint32_t GetBodySize() const override;                            // 5: shared 0x1559e0
    cIEventBody *CloneInto(void *memory) const override;              // 6: 0x155920

    cCanvasRect mRect; // +4
    uint32_t mCode;    // +0x14
};
static_assert(sizeof(cCanvasSizeMoveEvent) == 0x18, "cCanvasSizeMoveEvent layout");

// A mouse action (vtable 0xd9caec, 0x20 bytes), sent with event id 0x1b637482. Kinds: 7 left, 8 right, 9 middle button
// down; 0xa / 0xb / 0xc left / right / middle up; 0xd moved; 0xe the wheel; 0x14 the mouse left the window.
class cMouseEvent : public nGZEvent::cIEventBody {
public:
    static constexpr uint32_t kType = 0xfb637610;
    static constexpr uint32_t kGameVTable = 0xd9caec;
    cMouseEvent(uint32_t kind, int32_t x, int32_t y, uint32_t modifiers, bool doubleClick, int32_t wheelDelta,
                float wheelLines)
        : mKind(kind), mX(x), mY(y), mModifiers(modifiers), mDoubleClick(doubleClick), mWheelDelta(wheelDelta),
          mWheelLines(wheelLines)
    {
    }
    ~cMouseEvent() override {}                                        // 0: 0x154ee0
    cIEventBody *Assign(const cIEventBody *other) override;           // 1, 2: 0x1550d0
    cIEventBody *Slot2(const cIEventBody *other) override;
    uint32_t GetEventType() const override;                           // 3: 0x157700
    uint32_t GetSize() const override;                                // 4: shared 0x1559f0
    uint32_t GetBodySize() const override;                            // 5: shared 0x1561e0
    cIEventBody *CloneInto(void *memory) const override;              // 6: 0x155970

    uint32_t mKind;      // +4
    int32_t mX;          // +8: client coordinates
    int32_t mY;          // +0xc
    uint32_t mModifiers; // +0x10: 1 shift, 2 control, 4 alt, 0x10 / 0x20 / 0x40 the left / right / middle button held
    bool mDoubleClick;   // +0x14 (the three bytes after it are never written)
    int32_t mWheelDelta; // +0x18: the wheel's HIWORD(wParam)
    float mWheelLines;   // +0x1c: delta * lines per notch / 120
};
static_assert(sizeof(cMouseEvent) == 0x20, "cMouseEvent layout");

// A key (vtable 0xd9cb0c, 0x14 bytes), sent with event id 0x5b63748b. Kinds: 4 a character (the byte, sign-extended),
// 5 a key down, 6 a key up (the virtual key).
class cKeyEvent : public nGZEvent::cIEventBody {
public:
    static constexpr uint32_t kType = 0x1b6377b0;
    static constexpr uint32_t kGameVTable = 0xd9cb0c;
    cKeyEvent(uint32_t kind, int32_t key, int32_t repeat, uint32_t modifiers)
        : mKind(kind), mKey(key), mRepeat(repeat), mModifiers(modifiers)
    {
    }
    ~cKeyEvent() override {}                                          // 0: shared 0x154eb0
    cIEventBody *Assign(const cIEventBody *other) override;           // 1, 2: shared 0x1550b0
    cIEventBody *Slot2(const cIEventBody *other) override;
    uint32_t GetEventType() const override;                           // 3: 0x1576f0
    uint32_t GetSize() const override;                                // 4: shared 0x1559e0
    uint32_t GetBodySize() const override;                            // 5: shared 0x1561d0
    cIEventBody *CloneInto(void *memory) const override;              // 6: 0x155940

    uint32_t mKind;      // +4
    int32_t mKey;        // +8
    int32_t mRepeat;     // +0xc: LOWORD(lParam), sign-extended
    uint32_t mModifiers; // +0x10: KeyModifiers (0 for characters)
};
static_assert(sizeof(cKeyEvent) == 0x14, "cKeyEvent layout");

// The hooked slots of the game's vtables (the bodies each class has to itself).
struct cCanvasEventsAdapters {
    static uint32_t __fastcall CanvasEventType(const cCanvasEvent *self, void *);
    static nGZEvent::cIEventBody *__fastcall CanvasCloneInto(const cCanvasEvent *self, void *, void *memory);
    static cCanvasSizeMoveEvent *__fastcall SizeMoveDeletingDestructor(cCanvasSizeMoveEvent *self, void *, uint32_t flags);
    static nGZEvent::cIEventBody *__fastcall SizeMoveAssign(cCanvasSizeMoveEvent *self, void *,
                                                            const nGZEvent::cIEventBody *other);
    static uint32_t __fastcall SizeMoveEventType(const cCanvasSizeMoveEvent *self, void *);
    static uint32_t __fastcall SizeMoveSize(const cCanvasSizeMoveEvent *self, void *);
    static nGZEvent::cIEventBody *__fastcall SizeMoveCloneInto(const cCanvasSizeMoveEvent *self, void *, void *memory);
    static cMouseEvent *__fastcall MouseDeletingDestructor(cMouseEvent *self, void *, uint32_t flags);
    static nGZEvent::cIEventBody *__fastcall MouseAssign(cMouseEvent *self, void *, const nGZEvent::cIEventBody *other);
    static uint32_t __fastcall MouseEventType(const cMouseEvent *self, void *);
    static nGZEvent::cIEventBody *__fastcall MouseCloneInto(const cMouseEvent *self, void *, void *memory);
    static uint32_t __fastcall KeyEventType(const cKeyEvent *self, void *);
    static nGZEvent::cIEventBody *__fastcall KeyCloneInto(const cKeyEvent *self, void *, void *memory);
};

} // namespace nGZGraphic4
