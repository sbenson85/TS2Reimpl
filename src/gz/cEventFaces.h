#pragma once
// Three event classes the graphics layer builds on its stack and hands to listeners, whose vtables are owned statics
// (docs/standalone-shape.md, "Vtables of hook-shaped classes"): nGZEvent::cNullEvent (0xd9ca78, 4 bytes: the window's
// close query, the graphics system's and the scene graph's notifications), cScalarEvent<unsigned long> (0xd9cb2c,
// 8 bytes: the window's message 0x702) and nGZEvent::cConnectionChangedEvent (0xda239c, 0x18 bytes: the connection
// manager's). Our code stores NullEventVTable() and so on: the game's tables in hooked builds and the difftest harness
// (objects and words as before), our own faces' (rzface below) in an image-free program.
//
// Each face is an event body (nGZEvent::cIEventBody, 7 slots): the deleting destructor, the copy (slots 1 and 2), the
// event type, the size and the size past the vtable, and a copy built in caller memory. The game's slots are partly
// folded across event and pixel-format classes (0x300a0, 0x6c6b0, 0x154ce0, 0x155080, 0x1559c0, 0x155a00), so the faces
// have their own bodies; tests/gz_event_faces.cpp compares every slot with the game's through OwnNullEventVTable() and
// the others.
#include "game/owned.h"
#include "gz/cInputDeviceMap.h" // nGZEvent::cIEventBody

#include <cstdint>

TS2_OWNED_STATIC(const OwnedVTable, d9ca78); // nGZEvent::cNullEvent
TS2_OWNED_STATIC(const OwnedVTable, d9cb2c); // nGZEvent::cScalarEvent<unsigned long>
TS2_OWNED_STATIC(const OwnedVTable, da239c); // nGZEvent::cConnectionChangedEvent

namespace rzface {

// nGZEvent::cNullEvent: no fields; type 0x7b256782.
class cNullEvent final : public nGZEvent::cIEventBody {
public:
    static constexpr uint32_t kEventType = 0x7b256782;
    ~cNullEvent() override {}
    cIEventBody *Assign(const cIEventBody *other) override;
    cIEventBody *Slot2(const cIEventBody *other) override;
    uint32_t GetEventType() const override;
    uint32_t GetSize() const override;
    uint32_t GetBodySize() const override;
    cIEventBody *CloneInto(void *memory) const override;
};

// nGZEvent::cScalarEvent<T>: one value; the unsigned long instantiation's type is 0x7b22dd0f.
template <typename T> class cScalarEvent final : public nGZEvent::cIEventBody {
public:
    explicit cScalarEvent(T value) : mValue(value) {}
    ~cScalarEvent() override {}
    cIEventBody *Assign(const cIEventBody *other) override;
    cIEventBody *Slot2(const cIEventBody *other) override;
    uint32_t GetEventType() const override;
    uint32_t GetSize() const override;
    uint32_t GetBodySize() const override;
    cIEventBody *CloneInto(void *memory) const override;

    T mValue; // +4
};
template <> uint32_t cScalarEvent<unsigned long>::GetEventType() const;
extern template class cScalarEvent<unsigned long>; // cEventFaces.cpp

// nGZEvent::cConnectionChangedEvent: the five words of nGZEvent::cConnectionChangedEvent (cConnectionManager.h) past the
// vtable; type 0xdb27d6d4 (slot 3's; OnEvent is called with 0x5b51e806).
class cConnectionChangedEvent final : public nGZEvent::cIEventBody {
public:
    static constexpr uint32_t kEventType = 0xdb27d6d4;
    ~cConnectionChangedEvent() override {}
    cIEventBody *Assign(const cIEventBody *other) override;
    cIEventBody *Slot2(const cIEventBody *other) override;
    uint32_t GetEventType() const override;
    uint32_t GetSize() const override;
    uint32_t GetBodySize() const override;
    cIEventBody *CloneInto(void *memory) const override;

    uint32_t mWords[5]; // +4
};

} // namespace rzface

static_assert(sizeof(rzface::cNullEvent) == 4, "cNullEvent face layout");
static_assert(sizeof(rzface::cScalarEvent<unsigned long>) == 8, "cScalarEvent face layout");
static_assert(sizeof(rzface::cConnectionChangedEvent) == 0x18, "cConnectionChangedEvent face layout");

namespace nGZEvent {

// The words our code stores (the game's tables in hooked builds, the faces' in an image-free program).
inline const void *NullEventVTable() { return &ts2_rva_d9ca78; }
inline const void *ScalarEventVTable() { return &ts2_rva_d9cb2c; }
inline const void *ConnectionChangedEventVTable() { return &ts2_rva_da239c; }

// The faces' own tables as link-time constants (a local face's vptr; no object is kept).
inline const void *OwnNullEventVTable()
{
    rzface::cNullEvent face;
    return *reinterpret_cast<const void *const *>(&face);
}
inline const void *OwnScalarEventVTable()
{
    rzface::cScalarEvent<unsigned long> face(0);
    return *reinterpret_cast<const void *const *>(&face);
}
inline const void *OwnConnectionChangedEventVTable()
{
    rzface::cConnectionChangedEvent face;
    return *reinterpret_cast<const void *const *>(&face);
}

} // namespace nGZEvent
