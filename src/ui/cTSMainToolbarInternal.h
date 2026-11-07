#pragma once
// Helpers shared by the cTSMainToolbar source files (lane JE): virtual calls on the objects the slots reach, the game's
// cached getters still original, the trace calls, and the standard message the slots post.
#include "game/game.h"
#include "game/vtable.h"
#include "gz/cIGZUnknown.h"
#include "ifaces/cIGZMessage2Standard.h"
#include "rz/RZMemory.h"
#include "rz/cRZMessage2Standard.h"

#include <cstdint>

namespace toolbar {

inline uint32_t Word(const void *p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// The game's cached getters nobody has replaced (each returns its cell, filling it from the framework when empty), defined
// in cTSMainToolbar.size.cpp (whose status entry lists them in original_calls).
void *StateService();  // 0x211da3, cell 0x10cc458
void *Clock();         // 0x20df9a, cell 0x10cc2ac
void *Notifier();      // 0x9c5ab4, cell 0x10f5198
void *MessageServer(); // 0x9c5a89, cell 0x10f519c
void *SoundProbe();    // 0x9c5b7e, cell 0x10f51b4
void *Framework();     // 0x211051 (the db calls it library code; a getter): cell 0x10f5194, else the service object

// The trace calls (EA's event log: two words; or a slot word and the address of a value or of a pair of descriptors).
void TraceA(uint32_t a, uint32_t b); // 0x61ea5d
void TraceB(uint32_t a, uint32_t b); // 0x61eb0d
// 0x620af9 / 0x620b15 / 0x620b31 / 0x620b4d / 0x620b69 / 0x620b85 / 0x620ba1 / 0x620bbd (__cdecl): the second argument
// read, the first unused.
void Trace(uint32_t rva, const void *value);

// An object reached through a virtual base: +4 holds the vbtable pointer, whose second entry is the offset of the
// cIGZUnknown base from +4.
inline void *VirtualBase(void *object)
{
    auto *const vbptr = static_cast<uint8_t *>(object) + 4;
    return vbptr + (*reinterpret_cast<int32_t *const *>(vbptr))[1];
}
inline void ReleaseVirtual(void *object)
{
    if (object) As<cIGZUnknown>(VirtualBase(object))->Release();
}

// The state service's slot 53 object's slot 37, asked for 0xeaf75329 (0x61838f: the out pointer cleared, then the
// QueryInterface of the object's virtual base; the object is used unchecked).
inline void *StateHolder()
{
    void *const view = RawSlot<37, void *>(RawSlot<53, void *>(StateService()));
    void *holder = nullptr;
    As<cIGZUnknown>(VirtualBase(view))->QueryInterface(0xeaf75329u, &holder);
    return holder;
}

// A standard message (RZMemory 0x2c bytes, built as 0x1c03b7 / 0x1c033d build one), counted once when allocated, its
// id set (used unchecked, as the original does).
inline cRZMessage2Standard *NewMessage(uint32_t id)
{
    auto *const message = static_cast<cRZMessage2Standard *>(RZMemory::Malloc(cRZMessage2Standard::kSize));
    if (message) {
        message->mVTable = GameAddress<void>(cRZMessage2Standard::kVTable);
        message->mRefCount = 0;
        message->mFlags = 0;
        message->mId = 0;
        for (uint32_t &v : message->mValue) v = 0;
        message->mPtr20 = message->mPtr24 = message->mPtr28 = nullptr;
        As<ifaces::cIGZMessage2Standard>(message)->AddRef();
    }
    As<ifaces::cIGZMessage2Standard>(message)->SetType(id);
    return message;
}
inline void ReleaseOf(void *object)
{
    if (object) As<cIGZUnknown>(object)->Release();
}

} // namespace toolbar
