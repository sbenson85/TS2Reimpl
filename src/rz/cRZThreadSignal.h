#pragma once
#include "rz/cRZCriticalSection.h"
#include "rz/cRZString.h"

#include <cstddef>
#include <cstdint>

// The thread signal: a Win32 event behind cIGZThreadSignal (vtable 0xe61b7c, 8 slots) and a
// separately allocated state block. Construction is here; the signalling slots (SetEvent, PulseEvent,
// WaitForSingleObject, ResetEvent) are in cRZThreadSignalSlots.size.cpp. RVAs are 1.18.0.187.

// The state block (0x30 bytes, constructor 0x9cca8b).
struct cRZThreadSignalState {
    static constexpr uint32_t kSize = 0x30;
    cRZThreadSignalState *Construct();
    void Destruct();            // 0x9ccaf8: the lock, then the name

    void *mEvent;               // 0x00 the event handle (cleared by the constructor, set by the signal's)
    uint32_t mManualReset;      // 0x04 the constructor argument as given (cleared by the block's constructor)
    cRZString mName;            // 0x08
    cRZCriticalSection mLock;   // 0x28
};
static_assert(sizeof(cRZThreadSignalState) == cRZThreadSignalState::kSize, "cRZThreadSignalState size");
static_assert(offsetof(cRZThreadSignalState, mName) == 8 && offsetof(cRZThreadSignalState, mLock) == 0x28, "cRZThreadSignalState layout");

// The table (owned.h, OwnedVTable): the game's in hooked builds; in an image-free program ts2_own_vt_e61b7c
// (src/rz/cRZThreadSignalSlots.size.cpp, aliased in standalone/owned_statics.cpp).
TS2_OWNED_STATIC(const OwnedVTable, e61b7c);

class cRZThreadSignal {
public:
    static constexpr uint32_t kVTable = 0xe61b7c; // its RVA, for the tests; our code stores ts2_rva_e61b7c
    static constexpr uint32_t kIatCreateEventA = 0xd9a244; // the game's import slot

    // 0x9ccaa9 (ret 4): vtable, a new state block (operator new 0x30, constructed), the argument
    // stored in it, and CreateEventA(NULL, argument != 0, FALSE, NULL) as its event.
    cRZThreadSignal *Construct(uint32_t manualReset);

    static constexpr uint32_t kIatCloseHandle = 0xd9a17c;
    // 0x9ccb13: vtable, CloseHandle on the state's event (through the game's import slot), the state's
    // destructor, then a sized delete of the state block (0x30).
    void Destruct();
    // Slot 5, 0x9ccb38 (ret 4): Destruct, then with flags & 1 a sized delete (8). Returns this.
    cRZThreadSignal *DeletingDestructor(uint32_t flags);

    // The signalling slots (lane LO, src/rz/cRZThreadSignalSlots.size.cpp), through the game's import slots. Slots 0
    // (delete through slot 5) and 7 (the event handle) are bodies folded with cRZMutex's (0x9ccb85, 0x9ccb69).
    // Slot 1, 0x9ccb93: an auto-reset signal pulses (PulseEvent; true on success), a manual-reset one is set (SetEvent;
    // its result dropped: false).
    bool Signal();
    // Slot 2, 0x9ccb57: PulseEvent; true on success.
    bool Pulse();
    // Slot 3, 0x9ccbb9 (ret 4): waits microseconds / 1000 ms (0xffffffff: INFINITE); 1 signalled, 2 timed out, 0 failed.
    uint32_t Wait(uint32_t microseconds);
    // Slot 4, 0x9ccb7a: whether the event was created.
    bool IsValid();
    // Slot 6, 0x9ccbfb: ResetEvent; true on success.
    bool Reset();

    const void *mVTable;            // 0x00
    cRZThreadSignalState *mState;   // 0x04
};
static_assert(sizeof(cRZThreadSignal) == 8, "cRZThreadSignal size");
