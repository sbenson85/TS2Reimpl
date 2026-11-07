#pragma once
#include <cstdint>

// The game's lock interface (RTTI cIGZCriticalSection; not a cIGZUnknown). Slot order is ABI (db/interface-slots.jsonl;
// checked against cRZCriticalSection's vtable 0xda2708). Implementations add the deleting destructor at slot 6.
// Callers that only hold a game object use the generated ifaces::cIGZCriticalSection (ifaces/cIGZCriticalSection.h).
class cIGZCriticalSection {
public:
    virtual int32_t Release() = 0;  // slot 0: destroys the lock (no reference count); returns 0
    virtual int32_t Lock() = 0;     // slot 1: the new lock count
    virtual int32_t Unlock() = 0;   // slot 2: the new lock count
    virtual int32_t TryLock() = 0;  // slot 3: 0 if another thread holds it, else the new lock count
    virtual int32_t IsValid() = 0;  // slot 4: always 1 in cRZCriticalSection
    virtual int32_t IsLocked() = 0; // slot 5: the lock count
};
