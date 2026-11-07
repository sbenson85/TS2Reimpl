// The window service's events (lane OQ): src/gz/cCanvasEvents.h.
#include "gz/cCanvasEvents.h"

#include "game/game.h"
#include "misc/DeletingDestructors.h" // nGZEvent::cIEventHeader::kVTable

#include <new>

namespace nGZGraphic4 {

using nGZEvent::cIEventBody;

// ---- cCanvasEvent ----
// Each class's copy (slots 1 and 2) and clone (slot 6) take the fields one by one; the originals' result register holds
// the last word copied (they are void), ours gives the body.
cIEventBody *cCanvasEvent::Assign(const cIEventBody *other)
{
    mCode = static_cast<const cCanvasEvent *>(other)->mCode;
    return this;
}
cIEventBody *cCanvasEvent::Slot2(const cIEventBody *other) { return cCanvasEvent::Assign(other); }
uint32_t cCanvasEvent::GetEventType() const { return kType; }
uint32_t cCanvasEvent::GetSize() const { return sizeof(cCanvasEvent); }
uint32_t cCanvasEvent::GetBodySize() const { return sizeof(cCanvasEvent) - 4; }
cIEventBody *cCanvasEvent::CloneInto(void *memory) const { return ::new (memory) cCanvasEvent(mCode); }

// ---- cCanvasSizeMoveEvent ----
cIEventBody *cCanvasSizeMoveEvent::Assign(const cIEventBody *other)
{
    const auto *const event = static_cast<const cCanvasSizeMoveEvent *>(other);
    mRect = event->mRect;
    mCode = event->mCode;
    return this;
}
cIEventBody *cCanvasSizeMoveEvent::Slot2(const cIEventBody *other) { return cCanvasSizeMoveEvent::Assign(other); }
uint32_t cCanvasSizeMoveEvent::GetEventType() const { return kType; }
uint32_t cCanvasSizeMoveEvent::GetSize() const { return sizeof(cCanvasSizeMoveEvent); }
uint32_t cCanvasSizeMoveEvent::GetBodySize() const { return sizeof(cCanvasSizeMoveEvent) - 4; }
cIEventBody *cCanvasSizeMoveEvent::CloneInto(void *memory) const
{
    return ::new (memory) cCanvasSizeMoveEvent(mRect, mCode);
}

// ---- cMouseEvent ----
cIEventBody *cMouseEvent::Assign(const cIEventBody *other)
{
    const auto *const event = static_cast<const cMouseEvent *>(other);
    mKind = event->mKind;
    mX = event->mX;
    mY = event->mY;
    mModifiers = event->mModifiers;
    mDoubleClick = event->mDoubleClick;
    mWheelDelta = event->mWheelDelta;
    mWheelLines = event->mWheelLines;
    return this;
}
cIEventBody *cMouseEvent::Slot2(const cIEventBody *other) { return cMouseEvent::Assign(other); }
uint32_t cMouseEvent::GetEventType() const { return kType; }
uint32_t cMouseEvent::GetSize() const { return sizeof(cMouseEvent); }
uint32_t cMouseEvent::GetBodySize() const { return sizeof(cMouseEvent) - 4; }
cIEventBody *cMouseEvent::CloneInto(void *memory) const
{
    return ::new (memory) cMouseEvent(mKind, mX, mY, mModifiers, mDoubleClick, mWheelDelta, mWheelLines);
}

// ---- cKeyEvent ----
cIEventBody *cKeyEvent::Assign(const cIEventBody *other)
{
    const auto *const event = static_cast<const cKeyEvent *>(other);
    mKind = event->mKind;
    mKey = event->mKey;
    mRepeat = event->mRepeat;
    mModifiers = event->mModifiers;
    return this;
}
cIEventBody *cKeyEvent::Slot2(const cIEventBody *other) { return cKeyEvent::Assign(other); }
uint32_t cKeyEvent::GetEventType() const { return kType; }
uint32_t cKeyEvent::GetSize() const { return sizeof(cKeyEvent); }
uint32_t cKeyEvent::GetBodySize() const { return sizeof(cKeyEvent) - 4; }
cIEventBody *cKeyEvent::CloneInto(void *memory) const { return ::new (memory) cKeyEvent(mKind, mKey, mRepeat, mModifiers); }

// ---- the adapters (the game's vtables' own slots) ----
namespace {
// A copy of one of the game's objects keeps the game's vtable (0x155900 and the other slot 6 bodies store it before the
// fields), so code that copies a received event gets the object it got before.
template <class T> cIEventBody *GameClone(const T *self, void *memory)
{
    [[msvc::forceinline_calls]] cIEventBody *const copy = self->T::CloneInto(memory);
    *reinterpret_cast<const void **>(copy) = GameAddress<void>(T::kGameVTable);
    return copy;
}
} // namespace

uint32_t __fastcall cCanvasEventsAdapters::CanvasEventType(const cCanvasEvent *self, void *)
{
    [[msvc::forceinline_calls]] return self->cCanvasEvent::GetEventType();
}
cIEventBody *__fastcall cCanvasEventsAdapters::CanvasCloneInto(const cCanvasEvent *self, void *, void *memory)
{
    return GameClone(self, memory);
}
// 0x154d10 (reached only through the game's vtable): the base's destructor leaves the game's event-header vtable
// (0xd9ca58), then with flags & 1 the 0x18 bytes are freed.
cCanvasSizeMoveEvent *__fastcall cCanvasEventsAdapters::SizeMoveDeletingDestructor(cCanvasSizeMoveEvent *self, void *,
                                                                                   uint32_t flags)
{
    *reinterpret_cast<const void **>(self) = GameAddress<void>(nGZEvent::cIEventHeader::kVTable);
    if (flags & 1) ::operator delete(self, sizeof(cCanvasSizeMoveEvent));
    return self;
}
cIEventBody *__fastcall cCanvasEventsAdapters::SizeMoveAssign(cCanvasSizeMoveEvent *self, void *, const cIEventBody *other)
{
    [[msvc::forceinline_calls]] return self->cCanvasSizeMoveEvent::Assign(other);
}
uint32_t __fastcall cCanvasEventsAdapters::SizeMoveEventType(const cCanvasSizeMoveEvent *self, void *)
{
    [[msvc::forceinline_calls]] return self->cCanvasSizeMoveEvent::GetEventType();
}
uint32_t __fastcall cCanvasEventsAdapters::SizeMoveSize(const cCanvasSizeMoveEvent *self, void *)
{
    [[msvc::forceinline_calls]] return self->cCanvasSizeMoveEvent::GetSize();
}
cIEventBody *__fastcall cCanvasEventsAdapters::SizeMoveCloneInto(const cCanvasSizeMoveEvent *self, void *, void *memory)
{
    return GameClone(self, memory);
}
// 0x154ee0: as 0x154d10, 0x20 bytes.
cMouseEvent *__fastcall cCanvasEventsAdapters::MouseDeletingDestructor(cMouseEvent *self, void *, uint32_t flags)
{
    *reinterpret_cast<const void **>(self) = GameAddress<void>(nGZEvent::cIEventHeader::kVTable);
    if (flags & 1) ::operator delete(self, sizeof(cMouseEvent));
    return self;
}
cIEventBody *__fastcall cCanvasEventsAdapters::MouseAssign(cMouseEvent *self, void *, const cIEventBody *other)
{
    [[msvc::forceinline_calls]] return self->cMouseEvent::Assign(other);
}
uint32_t __fastcall cCanvasEventsAdapters::MouseEventType(const cMouseEvent *self, void *)
{
    [[msvc::forceinline_calls]] return self->cMouseEvent::GetEventType();
}
cIEventBody *__fastcall cCanvasEventsAdapters::MouseCloneInto(const cMouseEvent *self, void *, void *memory)
{
    return GameClone(self, memory);
}
uint32_t __fastcall cCanvasEventsAdapters::KeyEventType(const cKeyEvent *self, void *)
{
    [[msvc::forceinline_calls]] return self->cKeyEvent::GetEventType();
}
cIEventBody *__fastcall cCanvasEventsAdapters::KeyCloneInto(const cKeyEvent *self, void *, void *memory)
{
    return GameClone(self, memory);
}

} // namespace nGZGraphic4
