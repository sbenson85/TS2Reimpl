#pragma once
// nGZEvent::cVectorEvent<cVector<4, float>, 0x5b5b331a> (lane FD): the four-float event (a colour, or four scales) the
// shader sources fill through cIShaderEventSource::GetEvent. An event body on nGZEvent::cIEventHeader (0x14 bytes:
// vtable, four floats), seven slots as the window events' (gz/cCanvasEvents.h): the deleting destructor, the copy (slots
// 1 and 2, one body), the event type, the size and the size past the vtable, and a copy built in caller memory.
//
// The vtable (0xdb07d4) is owned: our code stores &ts2_rva_db07d4 (hooked builds: the game's table; an image-free program:
// ts2_own_vt_db07d4 below, standalone/owned_statics.cpp's alias). The slots only this class has (1/2 0x258ca0, 3
// 0x269ba0, 6 0x25b130) are hooked to ours (src/hook/tables/cVector4fEvent.cpp); the folded ones (0 0x154eb0, shared with
// cKeyEvent and cGraphConnChanged; 4 0x1559e0 and 5 0x1561d0, the window events' sizes) stay game code on the game's
// objects and are ours in our table.
#include "game/owned.h"
#include "misc/DeletingDestructors.h" // ts2_rva_d9ca58, nGZEvent::cIEventHeader's table (what the destructor leaves)

#include <cstdint>

TS2_OWNED_STATIC(const OwnedVTable, db07d4); // cVectorEvent<cVector<4, float>>
extern "C" const void *ts2_own_vt_db07d4[7];
extern "C" const void *ts2_own_vt_d9ca58[7]; // cIEventHeader's (its deleting destructor, then six pure slots)

namespace nGZEvent {

struct cVector4fEvent {
    static constexpr uint32_t kVTable = 0xdb07d4;
    static constexpr uint32_t kType = 0x5b5b331a;

    cVector4fEvent(float x, float y, float z, float w) : mVTable(&ts2_rva_db07d4), mValue{x, y, z, w} {}

    cVector4fEvent *DeletingDestructor(uint32_t flags); // 0: folded 0x154eb0 (frees 0x14 on flags & 1)
    cVector4fEvent *Assign(const cVector4fEvent *other); // 1, 2: 0x258ca0 (the four floats; the original is void)
    uint32_t GetEventType() const;                       // 3: 0x269ba0
    uint32_t GetSize() const;                            // 4: folded 0x1559e0
    uint32_t GetBodySize() const;                        // 5: folded 0x1561d0
    cVector4fEvent *CloneInto(void *memory) const;       // 6: 0x25b130 (memory is never null; the original is void)

    const OwnedVTable *mVTable; // +0
    float mValue[4];            // +4
};
static_assert(sizeof(cVector4fEvent) == 0x14, "cVector4fEvent layout");

} // namespace nGZEvent
